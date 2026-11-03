#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <Windows.h>
#include <atomic>
#include <bit>
#include <cstring>
#include <string>
#include <map>
#include <mutex>
#include <fstream>

REX_EXTERN(__imp__sub_82586878);
REX_EXTERN(__imp__sub_825D4000);
REX_EXTERN(__imp__sub_8299DE08);
REX_EXTERN(__imp__sub_8299D768);
REX_EXTERN(__imp__sub_829AB138);
REX_EXTERN(__imp__sub_829A01E8);
namespace {
bool Trace() {
  static const bool enabled=[] {char flag[8]{};return GetEnvironmentVariableA("FIFA07_TRACE_CINEMATICS",flag,sizeof(flag))==1 && flag[0]=='1';}();
  return enabled;
}
uint32_t Word(uint32_t address) {
  uint32_t value;std::memcpy(&value,REX_KERNEL_MEMORY()->TranslateVirtual(address),4);
  return std::byteswap(value);
}
std::string Text(uint32_t address) {
  if(!address)return {};
  auto* value=REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(address);
  return std::string(value,strnlen(value,256));
}
std::string Ansi(uint32_t address) {
  return address?Text(Word(address+4)):std::string{};
}
PPCFunc* Original(const char* name) {
  return reinterpret_cast<PPCFunc*>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),name));
}
std::string Callers(uint32_t frame) {
  std::string result;
  for(unsigned i=0;i<10;++i) {
    const uint32_t parent=Word(frame);
    if(parent<=frame || parent-frame>1048576 || (parent&15))break;
    const uint32_t caller=Word(parent-8);
    if(caller<0x82000000 || caller>=0x83000000)break;
    char address[16];snprintf(address,sizeof(address),"%08X,",caller);result+=address;
    frame=parent;
  }
  return result;
}
}

REX_EXTERN(sub_82586878) {
  const auto object=ctx.r3.u32,index=ctx.r4.u32,caller=uint32_t(ctx.lr);
  if(Trace())REXLOG_INFO("[cinematic-select] object={:08X} index={} caller={:08X}",object,index,caller);
  __imp__sub_82586878(ctx,base);
  if(Trace())REXLOG_INFO("[cinematic-select-result] movie_id={:08X}",Word(object+212));
}
REX_EXTERN(sub_825D4000) {
  const auto path=Trace()?Text(ctx.r4.u32):std::string{};
  if(Trace())REXLOG_INFO("[cinematic-open] path={}",path);
  __imp__sub_825D4000(ctx,base);
  if(Trace())REXLOG_INFO("[cinematic-open-result] path={} result={:08X}",path,ctx.r3.u32);
}
REX_EXTERN(sub_8299DE08) {
  const auto output=ctx.r5.u32;
  __imp__sub_8299DE08(ctx,base);
  if(Trace()) {
    const auto object=Word(output);
    REXLOG_INFO("[cinematic-decoder-create] status={:08X} object={:08X} vtable={:08X}",ctx.r3.u32,object,object?Word(object):0);
  }
}
REX_EXTERN(sub_8299D768) {
  const auto object=ctx.r3.u32,flags=ctx.r4.u32;
  __imp__sub_8299D768(ctx,base);
  if(Trace()) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static std::map<uint32_t,uint32_t> counts;
    const auto count=++counts[object];
    if(count<=6 || count%120==0 || ctx.r3.u32!=0)
      REXLOG_INFO("[cinematic-frame] object={:08X} frame={} flags={} status={:08X} width={} height={} time_hi={} time_lo={} pipeline={:08X}",object,count,flags,ctx.r3.u32,Word(object+232),Word(object+236),Word(object+288),Word(object+292),Word(object+44));
  }
}
REX_EXTERN(sub_829A01E8) {
  const auto renderer=ctx.r3.u32;
  __imp__sub_829A01E8(ctx,base);
  if(!Trace())return;
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::map<uint32_t,uint32_t> counts;
  const auto count=++counts[renderer];
  if(count>2 && count%120!=0)return;
  auto value=[&](uint32_t offset){return std::bit_cast<float>(Word(renderer+offset));};
  REXLOG_INFO("[cinematic-render] renderer={:08X} frame={} status={:08X} dimensions={}x{} screen={}x{} aspect={} rect={},{},{},{} vertices={},{},{},{},{},{} vs={:08X} ps={:08X} textures={:08X},{:08X},{:08X}",renderer,count,ctx.r3.u32,Word(renderer+124),Word(renderer+128),value(132),value(136),value(140),Word(renderer+96),Word(renderer+100),Word(renderer+104),Word(renderer+108),value(144),value(148),value(164),value(168),value(184),value(188),Word(renderer+72),Word(renderer+76),Word(renderer+288),Word(renderer+292),Word(renderer+296));
  if(count==1) {
    std::ofstream output("V:/logs/cinematic-renderer-"+std::to_string(renderer)+".bin",std::ios::binary);
    output.write(reinterpret_cast<const char*>(REX_KERNEL_MEMORY()->TranslateVirtual(renderer)),376);
  }
}
REX_EXTERN(sub_829AB138) {
  const auto owner=ctx.r31.u32;
  __imp__sub_829AB138(ctx,base);
  if(!Trace() || !owner || Word(owner)!=0x82127EB0)return;
  const auto sample=ctx.r3.u32;
  if(!sample || Word(sample)!=0x82128040)return;
  // This shared queue also returns audio. Capture only a complete 720p YUV420
  // video sample. +44 is the allocation base, +48 capacity, +52 cursor;
  // the cursor is reset by the consumer after this function returns.
  const auto data=Word(sample+44),size=Word(sample+48);
  if(!data || size!=1280*720*3/2+8 || Word(data)!=0)return;
  const auto timestamp=Word(data+4);
  if(timestamp>120000)return;
  const auto bucket=timestamp/2000;
  if(bucket>4)return;
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::map<uint32_t,uint32_t> captured;
  if(captured[owner]&(1u<<bucket))return;
  const auto* source=REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(data);
  auto cursor=reinterpret_cast<uintptr_t>(source);
  const auto end=cursor+size;
  while(cursor<end) {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(cursor),&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return;
    cursor=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
  }
  const auto name="V:/logs/cinematic-yuv-"+std::to_string(owner)+"-"+std::to_string(timestamp)+".bin";
  std::ofstream output(name,std::ios::binary);
  output.write(source,size);
  captured[owner]|=1u<<bucket;
  REXLOG_INFO("[cinematic-yuv-dump] owner={:08X} time={} data={:08X} size={} width={} height={}",owner,timestamp,data,size,Word(owner+232),Word(owner+236));
}

REX_EXTERN(__imp__NtCreateFile) {
  static auto* original=Original("__imp__NtCreateFile");
  const auto path=Trace() && ctx.r5.u32?Ansi(Word(ctx.r5.u32+4)):std::string{};
  const auto caller=uint32_t(ctx.lr);
  const auto callers=path.find("movies")!=std::string::npos?Callers(ctx.r1.u32):std::string{};
  original(ctx,base);
  if(!path.empty())REXLOG_INFO("[cinematic-file-create] path={} status={:08X} caller={:08X}",path,ctx.r3.u32,caller);
  if(!callers.empty())REXLOG_INFO("[cinematic-file-callers] path={} callers={}",path,callers);
}
REX_EXTERN(__imp__NtOpenFile) {
  static auto* original=Original("__imp__NtOpenFile");
  const auto path=Trace() && ctx.r5.u32?Ansi(Word(ctx.r5.u32+4)):std::string{};
  const auto caller=uint32_t(ctx.lr);
  original(ctx,base);
  if(!path.empty())REXLOG_INFO("[cinematic-file-open] path={} status={:08X} caller={:08X}",path,ctx.r3.u32,caller);
}
REX_EXTERN(__imp__NtQueryDirectoryFile) {
  static auto* original=Original("__imp__NtQueryDirectoryFile");
  const auto pattern=Trace()?Ansi(ctx.r10.u32):std::string{};
  const auto handle=ctx.r3.u32,caller=uint32_t(ctx.lr);
  original(ctx,base);
  static std::atomic<unsigned> count{0};
  if(Trace() && count.fetch_add(1)<400)REXLOG_INFO("[cinematic-directory] handle={:08X} pattern={} status={:08X} caller={:08X}",handle,pattern,ctx.r3.u32,caller);
}
