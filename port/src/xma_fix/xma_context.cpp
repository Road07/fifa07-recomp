/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2021 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*
* @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
*/

#include <algorithm>
#include <cstring>

#include <rex/audio/xma/context.h>
#include <rex/audio/xma/decoder.h>
#include <rex/audio/xma/helpers.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/memory/ring_buffer.h>
#include <rex/platform.h>
#include <rex/stream.h>

extern "C" {
#if REX_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "libavcodec/avcodec.h"
#include "libavutil/error.h"
#if REX_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

// Credits for most of this code goes to:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c

namespace rex::audio {

using stream::BitStream;

const uint32_t XmaContext::kBitsPerPacketHeader;
const uint32_t XmaContext::kOutputMaxSizeBytes;

XmaContext::XmaContext()
    : work_completion_event_(rex::thread::Event::CreateAutoResetEvent(false)) {}

XmaContext::~XmaContext() {
  if (av_context_) {
    avcodec_free_context(&av_context_);
  }
  if (av_frame_) {
    av_frame_free(&av_frame_);
  }
}

int XmaContext::Setup(uint32_t id, memory::Memory* memory, uint32_t guest_ptr) {
  id_ = id;
  memory_ = memory;
  guest_ptr_ = guest_ptr;

  // Allocate ffmpeg stuff:
  av_packet_ = av_packet_alloc();
  assert_not_null(av_packet_);
  av_packet_->buf = av_buffer_alloc(128 * 1024);

  // find the XMA2 audio decoder
  av_codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!av_codec_) {
    REXAPU_ERROR("XmaContext {}: Codec not found", id);
    return 1;
  }

  av_context_ = avcodec_alloc_context3(av_codec_);
  if (!av_context_) {
    REXAPU_ERROR("XmaContext {}: Couldn't allocate context", id);
    return 1;
  }

  // Initialize these to 0. They'll actually be set later.
  av_context_->channels = 0;
  av_context_->sample_rate = 0;

  av_frame_ = av_frame_alloc();
  if (!av_frame_) {
    REXAPU_ERROR("XmaContext {}: Couldn't allocate frame", id);
    return 1;
  }

  // FYI: We're purposely not opening the codec here. That is done later.
  return 0;
}

bool XmaContext::Work() {
  if (!is_enabled() || !is_allocated()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  const XMA_CONTEXT_DATA initial_data = data;

  if (!data.output_buffer_valid) {
    return true;
  }

  memory::RingBuffer output_rb = PrepareOutputRingBuffer(&data);

  if (data.IsConsumeOnlyContext()) {
    // Nothing to drain — don't touch the context or we'll reset the
    // game's output buffer offsets, causing stale PCM to be re-read.
    if (current_frame_remaining_subframes_ == 0) {
      return true;
    }

    Consume(&output_rb, &data);
    data.output_buffer_write_offset =
        output_rb.write_offset() / kOutputBytesPerBlock;

    if (output_rb.empty()) {
      data.output_buffer_valid = false;
    }
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }

  // Minimum free blocks needed before attempting a decode.
  // Use the number of subframes Consume() will actually write per iteration
  // (= subframe_decode_count, clamped to 1) plus any headroom requested by
  // output_buffer_padding.  Using a full-frame worth of space (the old
  // formula) was far too restrictive: games like TGM Ace use
  // subframe_decode_count=2 on a small ring buffer and never had 8 free
  // blocks available, causing the decoder to permanently stall.
  const uint32_t effective_sdc =
      std::max(static_cast<uint32_t>(1), data.subframe_decode_count);
  const int32_t minimum_subframe_decode_count =
      static_cast<int32_t>(effective_sdc) + data.output_buffer_padding;

  // We don't have enough space to even make one pass
  // Waiting for decoder to return more space.
  if (minimum_subframe_decode_count >
      remaining_subframe_blocks_in_output_buffer_) {

    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }

  while (remaining_subframe_blocks_in_output_buffer_ >=
         minimum_subframe_decode_count) {

    const uint32_t pre_decode_offset = data.input_buffer_read_offset;
    const uint8_t pre_remaining_subframes = current_frame_remaining_subframes_;

    Decode(&data);
    Consume(&output_rb, &data);

    // Don't abandon a partially consumed frame. Consume() hands over at most
    // subframe_decode_count blocks per pass so the pass that exhausts the
    // input typically leaves the rest of the frame undelivered. Work() has
    // already cleared is_enabled_ and only XMAEnableContext sets it again.
    // Game polling for the remainder therefore never kicks and nothing
    // else can ever deliver it so keep draining until the frame is out.
    if ((!data.IsAnyInputBufferValid() || data.error_status == 4) &&
        current_frame_remaining_subframes_ == 0) {

      break;
    }

    // If Decode didn't advance the read offset and produced no new frame,
    // we can't make progress. Break to avoid spinning.
    // Only check when there were no pending subframes — if we entered this
    // iteration with subframes remaining, Decode() intentionally skipped
    // (offset unchanged) while Consume() drained the frame.
    if (pre_remaining_subframes == 0 &&
        data.input_buffer_read_offset == pre_decode_offset &&
        current_frame_remaining_subframes_ == 0) {

      break;
    }
  }

  if (initial_data.IsAnyInputBufferValid()) {
    data.output_buffer_write_offset =
        output_rb.write_offset() / kOutputBytesPerBlock;

  } else {
    // NFS: Carbon and NFS: MW use write_offset as a stall detector
    // make sure that they are equal if we're starved on input.
    if (data.output_buffer_write_offset != data.output_buffer_read_offset) {
      data.output_buffer_write_offset = data.output_buffer_read_offset;
      data.output_buffer_valid = 0;
    }
  }

  // Signal the game that the output buffer is full
  if (remaining_subframe_blocks_in_output_buffer_ == 0 && output_rb.empty()) {

    data.output_buffer_valid = 0;
  }

  StoreContextMerged(data, initial_data, context_ptr);
  return true;
}

void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);
  if(work_completion_event_)work_completion_event_->Reset();
  set_is_enabled(true);
}

bool XmaContext::Block(bool poll) {
  if (!lock_.try_lock()) {
    if (poll) {
      return false;
    }
    lock_.lock();
  }
  lock_.unlock();
  return true;
}

void XmaContext::Clear() {
  std::lock_guard<std::mutex> lock(lock_);
  REXAPU_NOISY_DEBUG("XmaContext: reset context {}", id());

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  ClearLocked(&data);
  data.Store(context_ptr);
}

void XmaContext::ClearLocked(XMA_CONTEXT_DATA* data) {
  data->input_buffer_0_valid = 0;
  data->input_buffer_1_valid = 0;
  data->output_buffer_valid = 0;

  data->input_buffer_read_offset = kBitsPerPacketHeader;
  data->output_buffer_read_offset = 0;
  data->output_buffer_write_offset = 0;

  ResetDecoderState();
}

void XmaContext::ResetDecoderState() {
  // A freed or re-initialized context is a new logical stream, so the previous
  // wave's MDCT overlap-add tail must not survive into frame 0 of the next one.
  // avcodec_flush_buffers() cannot drop it: ff_xmaframes_decoder declares no
  // flush callback, so the call never reaches the code clearing channel[].out.
  // Invalidating the cached format makes PrepareDecoder reopen the codec on the
  // next decode, which does discard the history.
  if (av_context_) {
    av_context_->sample_rate = 0;
    av_context_->channels = 0;
  }
  raw_frame_.fill(0);
  decoded_frame_.fill(0);
  carry_frame_.fill(0);
  carry_valid_ = false;
  pending_output_limit_ = 0;
  pending_start_skip_ = 0;
  current_frame_remaining_subframes_ = 0;
  loop_frame_output_limit_ = 0;
  loop_start_skip_pending_ = false;
}

void XmaContext::Disable() {
  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(false);
}

void XmaContext::Release() {
  std::lock_guard<std::mutex> lock(lock_);
  assert_true(is_allocated());

  set_is_allocated(false);
  ResetDecoderState();
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
}

void XmaContext::SwapInputBuffer(XMA_CONTEXT_DATA* data) {
  if (data->current_buffer == 0) {
    data->input_buffer_0_valid = 0;
  } else {
    data->input_buffer_1_valid = 0;
  }
  data->current_buffer ^= 1;
  data->input_buffer_read_offset = kBitsPerPacketHeader;
}

void XmaContext::UpdateLoopStatus(XMA_CONTEXT_DATA* data) {
  if (data->loop_count == 0) {
    return;
  }

  const uint32_t loop_start = std::max(kBitsPerPacketHeader, data->loop_start);
  const uint32_t loop_end = std::max(kBitsPerPacketHeader, data->loop_end);

  if (data->input_buffer_read_offset != loop_end) {
    return;
  }

  data->input_buffer_read_offset = loop_start;
  loop_start_skip_pending_ = true;

  if (data->loop_count != 255) {
    data->loop_count--;
  }
}

int XmaContext::GetSampleRate(int id) {
  return kIdToSampleRate[std::min(id, 3)];
}

int16_t XmaContext::GetPacketNumber(size_t size, size_t bit_offset) {
  if (bit_offset < kBitsPerPacketHeader) {
    assert_always();
    return -1;
  }
  if (bit_offset >= (size << 3)) {
    assert_always();
    return -1;
  }
  size_t byte_offset = bit_offset >> 3;
  size_t packet_number = byte_offset / kBytesPerPacket;
  return static_cast<int16_t>(packet_number);
}

uint32_t XmaContext::GetCurrentInputBufferSize(XMA_CONTEXT_DATA* data) {
  return data->GetCurrentInputBufferPacketCount() * kBytesPerPacket;
}

uint8_t* XmaContext::GetCurrentInputBuffer(XMA_CONTEXT_DATA* data) {
  return memory()->TranslatePhysical(data->GetCurrentInputBufferAddress());
}

uint32_t XmaContext::GetAmountOfBitsToRead(uint32_t remaining_stream_bits, uint32_t frame_size) {
  return std::min(remaining_stream_bits, frame_size);
}

const uint8_t* XmaContext::GetNextPacket(
    XMA_CONTEXT_DATA* data, uint32_t next_packet_index,
    uint32_t current_input_packet_count) {
  uint8_t buffer=data->current_buffer;
  if(next_packet_index>=current_input_packet_count) {
    buffer^=1;
    next_packet_index-=current_input_packet_count;
  }
  if(!data->IsInputBufferValid(buffer) || !data->GetInputBufferAddress(buffer)
      || next_packet_index>=data->GetInputBufferPacketCount(buffer))return nullptr;
  return memory()->TranslatePhysical(data->GetInputBufferAddress(buffer))+
         next_packet_index*kBytesPerPacket;
}

uint32_t XmaContext::GetNextPacketReadOffset(uint8_t* buffer, uint32_t next_packet_index,
                                             uint32_t current_input_packet_count) {
  while (next_packet_index < current_input_packet_count) {
    uint8_t* next_packet = buffer + (next_packet_index * kBytesPerPacket);
    const uint32_t packet_frame_offset = xma::GetPacketFrameOffset(next_packet);

    if (packet_frame_offset <= kMaxFrameSizeinBits) {
      return (next_packet_index * kBitsPerPacket) + packet_frame_offset;
    }
    const uint8_t skip=xma::GetPacketSkipCount(next_packet);
    if(skip==0xff)break;
    next_packet_index+=skip+1;
  }

  return kBitsPerPacketHeader;
}

memory::RingBuffer XmaContext::PrepareOutputRingBuffer(XMA_CONTEXT_DATA* data) {
  const uint32_t output_capacity = data->output_buffer_block_count * kOutputBytesPerBlock;
  const uint32_t output_read_offset = data->output_buffer_read_offset * kOutputBytesPerBlock;
  const uint32_t output_write_offset = data->output_buffer_write_offset * kOutputBytesPerBlock;

  if (output_capacity > kOutputMaxSizeBytes) {
    REXAPU_WARN(
        "XmaContext {}: Output buffer exceeds expected size! "
        "(Actual: {} Max: {})",
        id(), output_capacity, kOutputMaxSizeBytes);
  }

  uint8_t* output_buffer = memory()->TranslatePhysical(data->output_buffer_ptr);

  memory::RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);
  remaining_subframe_blocks_in_output_buffer_ =
      static_cast<int32_t>(output_rb.write_count()) / kOutputBytesPerBlock;

  return output_rb;
}

kPacketInfo XmaContext::GetPacketInfo(uint8_t* packet,
                                               uint32_t frame_offset) {
  kPacketInfo packet_info = {};

  const uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
  BitStream stream(packet, kBitsPerPacket);
  stream.SetOffset(first_frame_offset);

  // Handling of splitted frame
  if (frame_offset < first_frame_offset) {
    packet_info.current_frame_ = 0;
    packet_info.current_frame_size_ = first_frame_offset - frame_offset;
  }

  while (true) {
    if (stream.BitsRemaining() < kBitsPerFrameHeader) {
      // This frame's 15-bit header runs into the next packet, so its size
      // isn't readable yet. Count it anyway, or the caller takes the previous
      // frame for the packet's last and skips straight past this one. Size 0
      // sends it to the split-header path.
      if (stream.BitsRemaining() > 0) {
        if (stream.offset_bits() == frame_offset) {
          packet_info.current_frame_ = packet_info.frame_count_;
          packet_info.current_frame_size_ = 0;
        }
        packet_info.frame_count_++;
      }
      break;
    }

    const uint64_t frame_size = stream.Peek(kBitsPerFrameHeader);
    if (frame_size == 0 || frame_size == xma::kMaxFrameLength) {
      break;
    }

    if (stream.offset_bits() == frame_offset) {
      packet_info.current_frame_ = packet_info.frame_count_;
      packet_info.current_frame_size_ = (uint32_t)frame_size;
    }

    packet_info.frame_count_++;

    if (frame_size > stream.BitsRemaining()) {
      // Last frame.
      break;
    }

    stream.Advance(frame_size - 1);

    // Read the trailing bit to see if frames follow
    if (stream.Read(1) == 0) {
      break;
    }
  }

  if (xma::IsPacketXma2Type(packet)) {
    const uint8_t xma2_frame_count = xma::GetPacketFrameCount(packet);
    if (xma2_frame_count > packet_info.frame_count_) {
      // Frame header split across packet boundary — scanner couldn't
      // peek the full 15-bit header. Trust the XMA2 header count.
      if (packet_info.current_frame_size_ == 0) {
        // Current frame is the split-header frame
        packet_info.current_frame_ = packet_info.frame_count_;
      }
      packet_info.frame_count_ = xma2_frame_count;
    } else if (xma2_frame_count != packet_info.frame_count_) {

    }
  }
  return packet_info;
}

void XmaContext::StoreContextMerged(const XMA_CONTEXT_DATA& data,
                                    const XMA_CONTEXT_DATA& initial_data, uint8_t* context_ptr) {
  XMA_CONTEXT_DATA fresh(context_ptr);

  fresh.loop_count = data.loop_count;
  fresh.output_buffer_write_offset = data.output_buffer_write_offset;
  if (initial_data.input_buffer_0_valid && !data.input_buffer_0_valid) {
    fresh.input_buffer_0_valid = 0;
  }
  if (initial_data.input_buffer_1_valid && !data.input_buffer_1_valid) {
    fresh.input_buffer_1_valid = 0;
  }

  if (initial_data.output_buffer_valid && !data.output_buffer_valid) {
    fresh.output_buffer_valid = 0;
  }

  fresh.input_buffer_read_offset = data.input_buffer_read_offset;
  fresh.error_status = data.error_status;
  fresh.current_buffer = data.current_buffer;
  fresh.output_buffer_read_offset = data.output_buffer_read_offset;

  fresh.Store(context_ptr);
}

void XmaContext::Consume(memory::RingBuffer* output_rb, const XMA_CONTEXT_DATA* data) {
  if (!current_frame_remaining_subframes_) {
    return;
  }

  if (loop_frame_output_limit_ > 0) {
    const uint8_t total_subframes = (kBytesPerFrameChannel / kOutputBytesPerBlock)
                                    << data->is_stereo;
    const uint8_t consumed = total_subframes - current_frame_remaining_subframes_;
    if (consumed >= loop_frame_output_limit_) {
      remaining_subframe_blocks_in_output_buffer_ -= data->output_buffer_padding;
      current_frame_remaining_subframes_ = 0;
      loop_frame_output_limit_ = 0;
      return;
    }
  }

  const uint8_t effective_sdc = std::max(static_cast<uint32_t>(1), data->subframe_decode_count);
  int8_t subframes_to_write = std::min(static_cast<int8_t>(current_frame_remaining_subframes_),
                                       static_cast<int8_t>(effective_sdc));

  if (loop_frame_output_limit_ > 0) {
    const uint8_t total_subframes = (kBytesPerFrameChannel / kOutputBytesPerBlock)
                                    << data->is_stereo;
    const uint8_t consumed = total_subframes - current_frame_remaining_subframes_;
    const int8_t remaining_until_limit = static_cast<int8_t>(loop_frame_output_limit_ - consumed);
    if (subframes_to_write > remaining_until_limit) {
      subframes_to_write = remaining_until_limit;
    }
  }

  const int8_t raw_frame_read_offset =
      ((kBytesPerFrameChannel / kOutputBytesPerBlock) << data->is_stereo) -
      current_frame_remaining_subframes_;

  output_rb->Write(raw_frame_.data() + (kOutputBytesPerBlock * raw_frame_read_offset),
                   subframes_to_write * kOutputBytesPerBlock);

  const int8_t headroom = (current_frame_remaining_subframes_ - subframes_to_write == 0)
                              ? data->output_buffer_padding
                              : 0;

  remaining_subframe_blocks_in_output_buffer_ -= subframes_to_write + headroom;
  current_frame_remaining_subframes_ -= subframes_to_write;
}

int XmaContext::PrepareDecoder(int sample_rate, bool is_two_channel) {
  sample_rate = GetSampleRate(sample_rate);

  uint32_t channels = is_two_channel ? 2 : 1;
  if (av_context_->sample_rate != sample_rate ||
      av_context_->channels != static_cast<int>(channels)) {
    REXAPU_NOISY_DEBUG("XmaContext {}: Codec reinit: rate {} -> {}, channels {} -> {}", id(),
                       av_context_->sample_rate, sample_rate, av_context_->channels, channels);
    avcodec_free_context(&av_context_);
    av_context_ = avcodec_alloc_context3(av_codec_);

    av_context_->sample_rate = sample_rate;
    av_context_->channels = channels;
    av_context_->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;

    if (avcodec_open2(av_context_, av_codec_, NULL) < 0) {
      REXAPU_ERROR("XmaContext: Failed to reopen FFmpeg context");
      return -1;
    }
    return 1;
  }
  return 0;
}

void XmaContext::PreparePacket(uint32_t frame_size, uint32_t frame_padding) {
  av_packet_->data = xma_frame_.data();
  av_packet_->size = static_cast<int>(1 + ((frame_padding + frame_size) / 8) +
                                      (((frame_padding + frame_size) % 8) ? 1 : 0));

  auto padding_end = av_packet_->size * 8 - (8 + frame_padding + frame_size);
  assert_true(padding_end < 8);
  xma_frame_[0] = ((frame_padding & 7) << 5) | ((padding_end & 7) << 2);
}

bool XmaContext::DecodePacket(AVCodecContext* av_context, const AVPacket* av_packet,
                              AVFrame* av_frame) {
  auto ret = avcodec_send_packet(av_context, av_packet);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    REXAPU_ERROR("XmaContext {}: Error sending packet for decoding: {} ({})", id(), errbuf, ret);
    return false;
  }
  ret = avcodec_receive_frame(av_context, av_frame);

  if (ret == AVERROR(EAGAIN)) {
    return false;
  }
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    REXAPU_ERROR("XmaContext {}: Error during decoding: {} ({})", id(), errbuf, ret);
    return false;
  }
  return true;
}

void XmaContext::Decode(XMA_CONTEXT_DATA* data) {
  auto next_offset=[&](XMA_CONTEXT_DATA* state,uint32_t index,uint32_t count) {
    uint8_t buffer=state->current_buffer;
    if(index>=count){buffer^=1;index-=count;}
    if(!state->IsInputBufferValid(buffer) || !state->GetInputBufferAddress(buffer)
        || index>=state->GetInputBufferPacketCount(buffer))return kBitsPerPacketHeader;
    return GetNextPacketReadOffset(memory()->TranslatePhysical(state->GetInputBufferAddress(buffer)),index,state->GetInputBufferPacketCount(buffer));
  };

  // No available data.
  if (!data->IsAnyInputBufferValid()) {

    // data->error_status = 4;
    return;
  }

  if (current_frame_remaining_subframes_ > 0) {
    return;
  }

  if (!data->IsCurrentInputBufferValid()) {

    SwapInputBuffer(data);
    if (!data->IsCurrentInputBufferValid()) {

      return;
    }
  }

  uint8_t* current_input_buffer = GetCurrentInputBuffer(data);

  input_buffer_.fill(0);

  // Detect if we're about to decode the loop end frame (before
  // UpdateLoopStatus may reset the offset).
  bool is_loop_end_frame = false;
  if (data->loop_count > 0) {
    const uint32_t loop_end = std::max(kBitsPerPacketHeader, data->loop_end);
    is_loop_end_frame = (data->input_buffer_read_offset == loop_end);
  }

  UpdateLoopStatus(data);

  if (!data->output_buffer_block_count) {

    return;
  }

  // Games like Dirt 2 can kick the decoder with read offset 0 (pointing into
  // the packet header) before filling in a valid offset. Clamp to the first
  // valid data position to avoid rejecting the packet entirely.
  if (data->input_buffer_read_offset < kBitsPerPacketHeader) {

    data->input_buffer_read_offset = kBitsPerPacketHeader;
  }

  const uint32_t current_input_size = GetCurrentInputBufferSize(data);
  const uint32_t current_input_packet_count =
      current_input_size / kBytesPerPacket;

  const int16_t packet_index =
      GetPacketNumber(current_input_size, data->input_buffer_read_offset);

  if (packet_index == -1) {

    return;
  }

  uint8_t* packet = current_input_buffer + (packet_index * kBytesPerPacket);
  const uint32_t packet_first_frame_offset = xma::GetPacketFrameOffset(packet);
  uint32_t relative_offset = data->input_buffer_read_offset % kBitsPerPacket;

  // If the read offset is before the first frame in this packet we're in the
  // tail of a split frame from the previous packet.  We don't have the
  // beginning of that frame so skip ahead to the first complete frame.
  // This also guards against games that kick the decoder with an offset
  // pointing into the packet header (e.g. Dirt 2).
  if (relative_offset < packet_first_frame_offset) {

    data->input_buffer_read_offset =
        (packet_index * kBitsPerPacket) + packet_first_frame_offset;
    relative_offset = packet_first_frame_offset;
  }
  const uint8_t skip_count = xma::GetPacketSkipCount(packet);

  // Full packet skip — no new frames begin in this packet (XMA2: 0xFF,
  // XMA1: lower 8 bits of 0x7FF also reads as 0xFF).  Advance to the
  // next sequential packet instead of trying to parse frames.
  if (skip_count == 0xFF) {

    const uint32_t next_packet_index_skip = packet_index + 1;
    uint32_t next_input_offset = next_offset(
        data, next_packet_index_skip, current_input_packet_count);
    if (next_packet_index_skip >= current_input_packet_count ||
        next_input_offset == kBitsPerPacketHeader) {
      SwapInputBuffer(data);
    }
    data->input_buffer_read_offset = next_input_offset;
    return;
  }

  kPacketInfo packet_info = GetPacketInfo(packet, relative_offset);
  const uint32_t packet_to_skip = skip_count + 1;
  const uint32_t next_packet_index = packet_index + packet_to_skip;

  // Frame header split across packet boundary — combine packets to read
  // the full 15-bit header and resolve the real frame size.
  if (packet_info.current_frame_size_ == 0) {

    const uint8_t* next_packet =
        GetNextPacket(data, next_packet_index, current_input_packet_count);
    if (!next_packet) {
      // Next buffer not available yet.  We can't resolve the split header
      // without it, so consume (swap) the current buffer and move on.

      SwapInputBuffer(data);
      return;
    }
    std::memcpy(input_buffer_.data(), packet + kBytesPerPacketHeader,
                kBytesPerPacketData);
    std::memcpy(input_buffer_.data() + kBytesPerPacketData,
                next_packet + kBytesPerPacketHeader, kBytesPerPacketData);

    BitStream combined(input_buffer_.data(),
                       (kBitsPerPacket - kBitsPerPacketHeader) * 2);
    combined.SetOffset(relative_offset - kBitsPerPacketHeader);

    uint64_t frame_size = combined.Peek(kBitsPerFrameHeader);
    if (frame_size == xma::kMaxFrameLength) {

      // Matching split-body error handling below; correct error code unknown.
      data->error_status = 4;
      return;
    }
    packet_info.current_frame_size_ = (uint32_t)frame_size;
  }

  BitStream stream =
      BitStream(current_input_buffer, (packet_index + 1) * kBitsPerPacket);
  stream.SetOffset(data->input_buffer_read_offset);

  const uint64_t bits_to_copy = GetAmountOfBitsToRead(
      (uint32_t)stream.BitsRemaining(), packet_info.current_frame_size_);

  if (bits_to_copy == 0) {

    SwapInputBuffer(data);
    return;
  }

  if (packet_info.isLastFrameInPacket()) {
    // Frame is a splitted frame
    if (stream.BitsRemaining() < packet_info.current_frame_size_) {
      const uint8_t* next_packet =
          GetNextPacket(data, next_packet_index, current_input_packet_count);

      if (!next_packet) {
        // Error path
        // Decoder probably should return error here
        // Not sure what error code should be returned
        data->error_status = 4;
        return;
      }
      // Copy next packet to buffer
      std::memcpy(input_buffer_.data() + kBytesPerPacketData,
                  next_packet + kBytesPerPacketHeader, kBytesPerPacketData);
    }
  }

  // Copy current packet to buffer
  std::memcpy(input_buffer_.data(), packet + kBytesPerPacketHeader,
              kBytesPerPacketData);

  stream = BitStream(input_buffer_.data(),
                     (kBitsPerPacket - kBitsPerPacketHeader) * 2);
  stream.SetOffset(relative_offset - kBitsPerPacketHeader);

  xma_frame_.fill(0);

  const uint32_t padding_start = static_cast<uint8_t>(
      stream.Copy(xma_frame_.data() + 1, packet_info.current_frame_size_));

  raw_frame_.fill(0);

  PrepareDecoder(data->sample_rate, bool(data->is_stereo));
  PreparePacket(packet_info.current_frame_size_, padding_start);
  if (DecodePacket(av_context_, av_packet_, av_frame_)) {
    // dump_raw(av_frame_, id());
    ConvertFrame(reinterpret_cast<const uint8_t**>(&av_frame_->data),
                 bool(data->is_stereo), raw_frame_.data());
    current_frame_remaining_subframes_ = 4 << data->is_stereo;

    // Loop end: limit output to subframes 0..loop_subframe_end.
    if (is_loop_end_frame) {
      loop_frame_output_limit_ = (data->loop_subframe_end + 1)
                                 << data->is_stereo;

    } else {
      loop_frame_output_limit_ = 0;
    }

    // Loop start: skip leading subframes per loop_subframe_skip.
    // Reducing remaining shifts the read offset forward in Consume().
    if (loop_start_skip_pending_) {
      const uint8_t skip = data->loop_subframe_skip << data->is_stereo;
      if (skip < current_frame_remaining_subframes_) {

        current_frame_remaining_subframes_ -= skip;
      }
      loop_start_skip_pending_ = false;
    }
  }

  // Compute where to go next.
  if (!packet_info.isLastFrameInPacket()) {
    const uint32_t next_frame_offset =
        (data->input_buffer_read_offset + bits_to_copy) % kBitsPerPacket;

    data->input_buffer_read_offset =
        (packet_index * kBitsPerPacket) + next_frame_offset;
    return;
  }

  const uint8_t* next_packet =
      GetNextPacket(data, next_packet_index, current_input_packet_count);

  if (!next_packet) {

  }

  uint32_t next_input_offset = next_offset(
      data, next_packet_index, current_input_packet_count);

  if (next_packet_index >= current_input_packet_count ||
      next_input_offset == kBitsPerPacketHeader) {
    SwapInputBuffer(data);
  }

  if (next_input_offset == kBitsPerPacketHeader) {
    // We're at start of next buffer
    // If it have any frame in this packet decoder should go to first frame in
    // packet If it doesn't have any frame then it should immediatelly go to
    // next packet
    if (data->IsAnyInputBufferValid()) {
      next_input_offset = xma::GetPacketFrameOffset(
          memory()->TranslatePhysical(data->GetCurrentInputBufferAddress()));

      if (next_input_offset > kMaxFrameSizeinBits) {

        SwapInputBuffer(data);
        return;
      }

    }
  }
  data->input_buffer_read_offset = next_input_offset;
  return;
}

void XmaContext::ConvertFrame(const uint8_t** samples, bool is_two_channel,
                              uint8_t* output_buffer) {
  // Loop through every sample, convert and drop it into the output array.
  // If more than one channel, we need to interleave the samples from each
  // channel next to each other. Always saturate because FFmpeg output is
  // not limited to [-1, 1] (for example 1.095 as seen in 5454082B).
  constexpr float scale = (1 << 15) - 1;
  auto out = reinterpret_cast<int16_t*>(output_buffer);

  // For testing of vectorized versions, stereo audio is common in 4D5307E6,
  // since the first menu frame; the intro cutscene also has more than 2
  // channels.
#if REX_ARCH_AMD64
  static_assert(kSamplesPerFrame % 8 == 0);
  const auto in_channel_0 = reinterpret_cast<const float*>(samples[0]);
  const __m128 scale_mm = _mm_set1_ps(scale);
  if (is_two_channel) {
    const auto in_channel_1 = reinterpret_cast<const float*>(samples[1]);
    const __m128i shufmask = _mm_set_epi8(14, 15, 6, 7, 12, 13, 4, 5, 10, 11, 2, 3, 8, 9, 0, 1);
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 4) {
      // Load 8 samples, 4 for each channel.
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_1[i]);
      // Rescale.
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      // Cast to int32.
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      // Saturated cast and pack to int16.
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      // Interleave channels and byte swap.
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      // Store, as [out + i * 4] movdqu.
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i * 2]), out_mm);
    }
  } else {
    const __m128i shufmask = _mm_set_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 8) {
      // Load 8 samples, as [in_channel_0 + i * 4] and
      // [in_channel_0 + i * 4 + 16] movups.
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_0[i + 4]);
      // Rescale.
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      // Cast to int32.
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      // Saturated cast and pack to int16.
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      // Byte swap.
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      // Store, as [out + i * 2] movdqu.
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]), out_mm);
    }
  }
#else
  uint32_t o = 0;
  for (uint32_t i = 0; i < kSamplesPerFrame; i++) {
    for (uint32_t j = 0; j <= uint32_t(is_two_channel); j++) {
      // Select the appropriate array based on the current channel.
      auto in = reinterpret_cast<const float*>(samples[j]);

      // Raw samples sometimes aren't within [-1, 1]
      float scaled_sample = rex::clamp_float(in[i], -1.0f, 1.0f) * scale;

      // Convert the sample and output it in big endian.
      auto sample = static_cast<int16_t>(scaled_sample);
      out[o++] = rex::byte_swap(sample);
    }
  }
#endif
}

}  // namespace rex::audio
