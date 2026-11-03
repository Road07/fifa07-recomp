"""Reproduce the local XMA fix without modifying the pinned SDK or its ABI.

Keep the complete reference state machine together: partial backports did not
fix FIFA 07 menu effects. Compiled sources are checked in; normal builds do not
require this preparation script.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
destination = root / 'port/src/xma_fix'
sdk = root / 'tools/rexglue-source/src/audio'
reference = (root / 'tools/xma_reference/xma_context_new.cc').read_text()


def method(text, cls, name):
    found = re.search(rf'^[^\n]*\b{cls}::{name}\([^{{]*\{{.*?^\}}', text, re.M | re.S)
    assert found, (cls, name)
    return found


context = (sdk / 'xma_context.cpp').read_text()
context = context.replace('  set_is_enabled(true);\n}',
                          '  if(work_completion_event_)work_completion_event_->Reset();\n'
                          '  set_is_enabled(true);\n}', 1)
for name in ('Work', 'Decode', 'UpdateLoopStatus', 'GetPacketInfo'):
    transplant = method(reference, 'XmaContextNew', name).group()
    transplant = transplant.replace('XmaContextNew::', 'XmaContext::')
    transplant = re.sub(r'\bXELOG(?:APU|E|W)\(.*?\);', '', transplant, flags=re.S)
    transplant = transplant.replace('  SCOPE_profile_cpu_f("apu");', '')
    transplant = transplant.replace('std::lock_guard<xe_mutex>', 'std::lock_guard<std::mutex>')
    transplant = transplant.replace('RingBuffer output_rb', 'memory::RingBuffer output_rb')
    transplant = transplant.replace('const kPacketInfo XmaContext::', 'kPacketInfo XmaContext::')
    if name == 'Decode':
        # The SDK header lacks the reference overload. Preserve its semantics
        # with a local adapter rather than changing the class layout.
        transplant = re.sub(r'GetNextPacketReadOffset\((\s*)data,', r'next_offset(\1data,', transplant)
        adapter = '''
  auto next_offset=[&](XMA_CONTEXT_DATA* state,uint32_t index,uint32_t count) {
    uint8_t buffer=state->current_buffer;
    if(index>=count){buffer^=1;index-=count;}
    if(!state->IsInputBufferValid(buffer) || !state->GetInputBufferAddress(buffer)
        || index>=state->GetInputBufferPacketCount(buffer))return kBitsPerPacketHeader;
    return GetNextPacketReadOffset(memory()->TranslatePhysical(state->GetInputBufferAddress(buffer)),index,state->GetInputBufferPacketCount(buffer));
  };
'''
        transplant = transplant.replace('{', '{' + adapter, 1)
    old = method(context, 'XmaContext', name)
    context = context[:old.start()] + transplant + context[old.end():]

# Reference packet lookup, including the sub-stream index across buffers.
old = method(context, 'XmaContext', 'GetNextPacket')
context = context[:old.start()] + '''const uint8_t* XmaContext::GetNextPacket(
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
}''' + context[old.end():]
context = context.replace('    next_packet_index++;', '''    const uint8_t skip=xma::GetPacketSkipCount(next_packet);
    if(skip==0xff)break;
    next_packet_index+=skip+1;''', 1)
context = re.sub(r'\n[ \t]+\n', '\n\n', context)
context = re.sub(r'\n{3,}', '\n\n', context)

decoder = (sdk / 'xma_decoder.cpp').read_text()
decoder = decoder.replace('ffmpeg_verbose', 'fifa07_ffmpeg_verbose')
start = decoder.index('    // Decode inline. waiting on the worker sweep stalls the realtime')
end = decoder.index('\n  } else if (r >= XmaRegister::Context0Lock', start)
decoder = decoder[:start] + '''    // Match the verified reference: one worker, waiting until every kicked
    // context has published its output before returning to the guest.
    work_event_->Set();
    for(unsigned i=0;kicked_value && i<32;++i,kicked_value>>=1)
      if(kicked_value&1)contexts_[base_context_id+i].WaitForWorkDone();
''' + decoder[end:]

destination.mkdir(exist_ok=True)
(destination / 'xma_context.cpp').write_text(context)
(destination / 'xma_decoder.cpp').write_text(decoder)
print('Prepared complete reference XMA state machine and dedicated worker')
