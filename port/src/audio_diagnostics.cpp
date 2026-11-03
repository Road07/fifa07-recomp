#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <Windows.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>

REX_EXTERN(__imp__sub_8297E220);
REX_EXTERN(__imp__sub_82825698);
REX_EXTERN(__imp__sub_8282B3D8);
namespace {
bool AudioTraceEnabled() {
  static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("FIFA07_TRACE_AUDIO",value,sizeof(value))==1 && value[0]=='1';}();
  return enabled;
}
uint32_t ReadAudioWord(uint32_t address) {
  uint32_t value;
  std::memcpy(&value,REX_KERNEL_MEMORY()->TranslateVirtual(address),4);
  return std::byteswap(value);
}
struct Capture {
  std::array<char,1024*1024> buffer{};
  std::ofstream file;
  uint32_t frames=0;
  Capture() {
    file.rdbuf()->pubsetbuf(buffer.data(),buffer.size());
    file.open("V:/logs/menu-audio-guest-6ch.f32be",std::ios::binary);
  }
};
void CheckXas(uint32_t state,uint32_t output,bool version1) {
  static constexpr float coefficients[4][2]={{0,0},{.9375f,0},{1.796875f,-.8125f},{1.53125f,-.859375f}};
  const auto* object=REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(state);
  const unsigned channels=object[46],samples=version1?128:32,block=version1?76:19;
  if (!channels || channels>6) return;
  const uint32_t source=ReadAudioWord(state+52)-block*channels;
  const uint32_t destination=ReadAudioWord(output+4);
  uint16_t stride;std::memcpy(&stride,REX_KERNEL_MEMORY()->TranslateVirtual(output+14),2);stride=std::byteswap(stride);
  if (!source || !destination || stride<samples) return;
  const auto* input=REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(source);
  uint32_t mismatches=0;float maximum=0;
  for(unsigned channel=0;channel<channels;++channel)for(unsigned group=0;group<(version1?4u:1u);++group) {
    const auto* head=version1?input+channel*76+group*4:input+channel*2;
    const uint16_t h2=uint16_t(head[0])|uint16_t(head[1])<<8;
    const auto* second=version1?head+2:head+channels*2;
    const uint16_t h1=uint16_t(second[0])|uint16_t(second[1])<<8;
    const unsigned filter=h2&15,shift=h1&15;
    if (filter>3) continue;
    float older=float(int16_t(h2&0xfff0))/32768.0f;
    float previous=float(int16_t(h1&0xfff0))/32768.0f;
    auto compare=[&](unsigned sample,float expected) {
      const float actual=std::bit_cast<float>(ReadAudioWord(destination+4*(channel*stride+group*32+sample)));
      const float difference=std::abs(actual-expected);maximum=std::max(maximum,difference);
      if (!std::isfinite(actual) || difference>0.00002f*(1+std::abs(expected)))++mismatches;
    };
    compare(0,older);compare(1,previous);
    for(unsigned i=0;i<30;++i) {
      const uint8_t packed=version1?input[channel*76+16+(i/2)*4+group]:input[4*channels+(i/2)*channels+channel];
      int nibble=(i&1)?packed&15:packed>>4;if(nibble&8)nibble-=16;
      const float residual=std::ldexp(float(nibble),-3-int(shift));
      const float value=residual+previous*coefficients[filter][0]+older*coefficients[filter][1];
      compare(i+2,value);older=previous;previous=value;
    }
  }
  static std::atomic<uint32_t> counts[2]{},reports{0};
  const auto count=counts[version1].fetch_add(1)+1;
  if(count<=3 || count%4096==0 || (mismatches && reports.fetch_add(1)<12))
    REXLOG_INFO("[xas-check] version={} blocks={} channels={} stride={} mismatch={} maximum={} source={:08X}",version1?1:0,count,channels,stride,mismatches,maximum,source);
  if(mismatches) {
    static std::atomic<bool> saved=false;
    if(!saved.exchange(true)) {
      std::ofstream file("V:/logs/xas-first-mismatch-input.bin",std::ios::binary);
      file.write(reinterpret_cast<const char*>(input),block*channels);
      std::ofstream result("V:/logs/xas-first-mismatch-output.bin",std::ios::binary);
      result.write(reinterpret_cast<const char*>(REX_KERNEL_MEMORY()->TranslateVirtual(destination)),stride*channels*4);
    }
  }
}
}

REX_EXTERN(sub_82825698) {
  const auto state=ctx.r3.u32,output=ctx.r4.u32;
  __imp__sub_82825698(ctx,base);
  if(AudioTraceEnabled())CheckXas(state,output,false);
}
REX_EXTERN(sub_8282B3D8) {
  const auto state=ctx.r3.u32,output=ctx.r4.u32;
  __imp__sub_8282B3D8(ctx,base);
  if(AudioTraceEnabled())CheckXas(state,output,true);
}

// Observe the guest buffer immediately before XAudioSubmitRenderDriverFrame.
// Preserve every guest register and sample. Disabled in ordinary launches.
REX_EXTERN(sub_8297E220) {
  const bool observe=AudioTraceEnabled() && ctx.lr==0x82977CE0;
  const uint32_t output=ctx.r4.u32;
  __imp__sub_8297E220(ctx,base);
  if (!observe || ctx.r3.s32<0) return;
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static auto capture=std::make_unique<Capture>();
  if (capture->frames>=18750 || !capture->file) return; // 100 s at 48k / 256.
  const uint32_t samples=ReadAudioWord(output+8);
  if (!samples) return;
  std::array<uint32_t,1536> data{};
  std::memcpy(data.data(),REX_KERNEL_MEMORY()->TranslateVirtual(samples),sizeof(data));
  capture->file.write(reinterpret_cast<const char*>(data.data()),sizeof(data));
  ++capture->frames;
  if (capture->frames%188==0) {
    std::array<float,6> peaks{};
    uint32_t nonfinite=0,overrange=0;
    for (uint32_t i=0;i<1536;++i) {
      const float sample=std::bit_cast<float>(std::byteswap(data[i]));
      if (!std::isfinite(sample)) ++nonfinite;
      else {peaks[i/256]=std::max(peaks[i/256],std::abs(sample));overrange+=std::abs(sample)>1;}
    }
    capture->file.flush();
    REXLOG_INFO("[audio-capture] frames={} sample_ptr={:08X} peaks={},{},{},{},{},{} nonfinite={} overrange={}",
      capture->frames,samples,peaks[0],peaks[1],peaks[2],peaks[3],peaks[4],peaks[5],nonfinite,overrange);
  }
}
