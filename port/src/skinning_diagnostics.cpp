#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/thread_state.h>
#include <rex/runtime.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/pipeline/shader/shader.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <vector>

REX_EXTERN(__imp__sub_82585A20);
REX_EXTERN(__imp__sub_822519F0);
REX_EXTERN(__imp__sub_82584138);
REX_EXTERN(__imp__sub_821CF490);
REX_EXTERN(__imp__sub_821F12E8);
REX_EXTERN(__imp__sub_8270C388);

namespace {
bool Enabled() {
  static const bool enabled = [] {
    char* raw = nullptr;
    size_t size = 0;
    if (_dupenv_s(&raw, &size, "FIFA07_TRACE_SKINNING") || !raw) return false;
    std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    return std::string_view(raw) == "1";
  }();
  return enabled;
}
uint32_t Read32(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, REX_KERNEL_MEMORY()->TranslateVirtual(address), 4);
  return std::byteswap(value);
}
float ReadFloat(uint32_t address) { return std::bit_cast<float>(Read32(address)); }
struct Trace {
  bool active = false;
  uint32_t frame = 0, data = 0, pitch = 0;
  uint32_t matrices = 0, mismatch = 0, nonfinite = 0, outside = 0;
  float maximum = 0;
};
thread_local Trace trace;
thread_local std::vector<float> bone_products;
thread_local bool bone_products_written=false;
// One-shot, opt-in write trap on an ordinary writable pose page. Restore the
// original protection before resuming the write; never replace a runtime watch.
std::atomic<uintptr_t> pose_watch_page{0};
std::atomic<bool> pose_write_captured{false};
DWORD pose_watch_protection=0;
uintptr_t pose_write_pc=0;
uint64_t pose_write_lr=0;
std::array<uint32_t,9> pose_write_args{};
std::array<uint32_t,6> pose_write_callers{};
uint32_t pose_write_r12=0;
LONG CALLBACK PoseWriteTrap(EXCEPTION_POINTERS* exception) {
  auto* record=exception->ExceptionRecord;
  uintptr_t page=pose_watch_page.load();
  if (!page || record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION ||
      record->NumberParameters<2 || record->ExceptionInformation[0]!=1 ||
      (record->ExceptionInformation[1]&~uintptr_t(4095))!=page)
    return EXCEPTION_CONTINUE_SEARCH;
  if (!pose_watch_page.compare_exchange_strong(page,0))
    return EXCEPTION_CONTINUE_SEARCH;
  DWORD ignored=0;
  if (!VirtualProtect(reinterpret_cast<void*>(page),4096,pose_watch_protection,&ignored))
    return EXCEPTION_CONTINUE_SEARCH;
  pose_write_pc=reinterpret_cast<uintptr_t>(record->ExceptionAddress)-
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (auto* state=rex::runtime::ThreadState::Get()) {
    const auto* context=state->context();
    pose_write_lr=context->lr;
    pose_write_r12=context->r12.u32;
    const uint32_t args[]={context->r3.u32,context->r4.u32,context->r5.u32,
      context->r6.u32,context->r7.u32,context->r8.u32,context->r9.u32,
      context->r10.u32,context->r11.u32};
    std::copy(std::begin(args),std::end(args),pose_write_args.begin());
    uint32_t sp=context->r1.u32;
    for (auto& caller:pose_write_callers) {
      uint32_t parent=0,link=0; SIZE_T got=0;
      if (!ReadProcessMemory(GetCurrentProcess(),REX_KERNEL_MEMORY()->TranslateVirtual(sp),&parent,4,&got) || got!=4) break;
      parent=std::byteswap(parent);
      if (parent<=sp || parent-sp>1024*1024) break;
      if (!ReadProcessMemory(GetCurrentProcess(),REX_KERNEL_MEMORY()->TranslateVirtual(parent-8),&link,4,&got) || got!=4) break;
      caller=std::byteswap(link); sp=parent;
    }
  }
  pose_write_captured.store(true);
  return EXCEPTION_CONTINUE_EXECUTION;
}
void ObservePoseWriter() {
  static const bool enabled=[] {
    char value[8]{};
    return GetEnvironmentVariableA("FIFA07_TRACE_POSE_WRITER",value,sizeof(value))==1 && value[0]=='1';
  }();
  if (!enabled) return;
  static bool armed=false;
  static unsigned stage=0;
  uint32_t source=0;
  if (pose_write_captured.exchange(false)) {
    REXLOG_INFO("[pose-writer] host_rva={:X} guest_lr={:08X} args={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
      pose_write_pc,pose_write_lr,pose_write_args[0],pose_write_args[1],pose_write_args[2],pose_write_args[3],pose_write_args[4],pose_write_args[5],pose_write_args[6],pose_write_args[7],pose_write_args[8]);
    REXLOG_INFO("[pose-writer-stack] stage={} r12={:08X} callers={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",stage,pose_write_r12,pose_write_callers[0],pose_write_callers[1],pose_write_callers[2],pose_write_callers[3],pose_write_callers[4],pose_write_callers[5]);
    if (stage<4 && pose_write_args[2]==251*64) {
      source=pose_write_args[1]; armed=false;
    }
  }
  if (armed || !trace.matrices) return;
  const uint32_t object=Read32(0x82D40DD8);
  if (!object || Read32(object+124)<25) return;
  const uint32_t resource=Read32(Read32(object+116)+24*20);
  const uint32_t world=source ? source : Read32(resource+96);
  if (!world) return;
  // The first page also contains neighboring bookkeeping allocations. Use a
  // middle page that lies wholly inside this 251-matrix pose array.
  const auto page=reinterpret_cast<uintptr_t>(REX_KERNEL_MEMORY()->TranslateVirtual(world+112*64+48))&~uintptr_t(4095);
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(reinterpret_cast<void*>(page),&info,sizeof(info)) || info.Protect!=PAGE_READWRITE) return;
  if (!AddVectoredExceptionHandler(1,PoseWriteTrap)) return;
  pose_watch_protection=info.Protect;
  pose_watch_page.store(page);
  DWORD old=0;
  if (!VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_READONLY,&old)) {pose_watch_page.store(0);return;}
  armed=true;
  ++stage;
  REXLOG_INFO("[pose-watch] stage={} world={:08X} page={:X}",stage,world,page);
}
std::array<std::atomic<uint32_t>,3> palettes{};
std::atomic<uint32_t> binding_samples = 0;
std::mutex dump_mutex;
std::set<uint32_t> dumped_vertices;
bool SerialAnimation() {
  static const bool enabled=[] {
    char* raw=nullptr; size_t size=0;
    if (_dupenv_s(&raw,&size,"FIFA07_SERIAL_ANIMATION") || !raw) return false;
    std::unique_ptr<char,decltype(&std::free)> owned(raw,&std::free);
    return std::string_view(raw)=="1";
  }();
  return enabled;
}
}

// The title contains two pose decoders selected by this byte. This diagnostic
// selects its existing alternate path; it does not alter the animation assets.
REX_EXTERN(sub_8270C388) {
  static const bool alternate=[] {
    char value[8]{};
    return GetEnvironmentVariableA("FIFA07_ALTERNATE_POSE",value,sizeof(value))==1 && value[0]=='1';
  }();
  if (alternate) {
    auto* mode=REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(0x82CE9666);
    const auto original=*mode;
    *mode=0;
    static std::atomic<bool> reported=false;
    if (!reported.exchange(true)) REXLOG_INFO("[pose-decoder] original={} effective=0 alternate=1",original);
  }
  __imp__sub_8270C388(ctx,base);
}

// A/B diagnostic: use the game's existing synchronous animation branch.
// No change unless the dedicated environment switch is explicitly enabled.
REX_EXTERN(sub_821F12E8) {
  const uint32_t key=ctx.r4.u32;
  const bool observe=Enabled() || SerialAnimation();
  const char* selected=nullptr;
  if (observe && key) {
    for (const char* name:{"DISTRIBUTE_ANIM","ENABLE_FRAMEANIMATION","SKIP_BOOTFLOW"}) {
      if (std::memcmp(REX_KERNEL_MEMORY()->TranslateVirtual(key),name,std::strlen(name)+1)==0) { selected=name; break; }
    }
  }
  __imp__sub_821F12E8(ctx,base);
  if (selected) {
    const auto original=ctx.r3.u32;
    if (std::string_view(selected)=="DISTRIBUTE_ANIM" && SerialAnimation()) ctx.r3.u64=0;
    static std::atomic<uint32_t> config_reports=0;
    if (config_reports.fetch_add(1)<12) REXLOG_INFO("[game-config] key={} original={} effective={}",selected,original,ctx.r3.u32);
  }
}

// Observe only. Always run the original guest function and preserve its context.
REX_EXTERN(sub_82585A20) {
  if (!Enabled()) { __imp__sub_82585A20(ctx, base); return; }
  const auto frame = trace.frame + 1;
  trace = {};
  trace.active = true;
  trace.frame = frame;
  __imp__sub_82585A20(ctx, base);
  trace.active = false;
  ObservePoseWriter();
  if (!bone_products_written && trace.matrices) {
    std::ofstream file("V:/logs/port-bone-products.bin",std::ios::binary);
    file.write(reinterpret_cast<const char*>(bone_products.data()),bone_products.size()*sizeof(float));
    if (file) {
      bone_products_written=true;
      REXLOG_INFO("[skin-products] frame={} records={} floats_per_record=44",frame,trace.matrices);
    }
    bone_products.clear();
  }
  if (frame <= 3 || frame % 300 == 0 || trace.mismatch || trace.nonfinite || trace.outside)
    REXLOG_INFO("[skin-frame] frame={} matrices={} mismatch={} nonfinite={} outside={} maximum={}",
                frame, trace.matrices, trace.mismatch, trace.nonfinite, trace.outside, trace.maximum);
}

REX_EXTERN(sub_822519F0) {
  const bool observe = Enabled() && trace.active;
  const auto texture = ctx.r3.u32, output = ctx.r6.u32;
  __imp__sub_822519F0(ctx, base);
  if (!observe) return;
  trace.data = Read32(output + 4);
  trace.pitch = Read32(output + 8);
  palettes[(trace.frame+1)%3].store(texture, std::memory_order_relaxed);
  if (trace.frame <= 3 || trace.frame % 300 == 0)
    REXLOG_INFO("[skin-lock] frame={} texture={:08X} data={:08X} pitch={} fetch={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
                trace.frame, texture, trace.data, trace.pitch,
                Read32(texture+28), Read32(texture+32), Read32(texture+36),
                Read32(texture+40), Read32(texture+44), Read32(texture+48));
}

// D3D SetTexture converts a virtual E/F address to a physical fetch address.
// Observe the guest's final conversion instead of guessing from the header.
REX_EXTERN(sub_821CF490) {
  const uint32_t device=ctx.r3.u32, slot=ctx.r4.u32, texture=ctx.r5.u32;
  __imp__sub_821CF490(ctx, base);
  if (!Enabled() || !texture || slot>=32) return;
  bool palette=false;
  for (auto& p:palettes) palette |= p.load(std::memory_order_relaxed)==texture;
  if (!palette) return;
  const auto sample=binding_samples.fetch_add(1, std::memory_order_relaxed);
  if (sample%250==0 && sample<=100000) {
    // The selected plugin is xenos. Use the matching SDK's public register
    // layout. ReadProcessMemory observes the GPU worker without a C++ data race;
    // this is an asynchronous diagnostic sample, not an exact draw capture.
    auto* graphics=static_cast<rex::graphics::GraphicsSystem*>(REX_KERNEL_STATE()->emulator()->graphics_system());
    std::array<uint32_t, rex::graphics::RegisterFile::kRegisterCount> registers{};
    SIZE_T copied=0;
    if (ReadProcessMemory(GetCurrentProcess(),graphics->register_file()->values,registers.data(),sizeof(registers),&copied) && copied==sizeof(registers)) {
      const float packing=std::bit_cast<float>(registers[0x43FE]);
      static uint32_t captured=0;
      const uint32_t candidate=registers[0x48BE],candidate_size=((registers[0x48BF]>>2)&0xFFFFFFu)*4;
      if (sample<=10000) REXLOG_INFO("[skin-candidate] sample={} vf95={:08X} size={} c255={},{},{},{}",sample,candidate,candidate_size,std::bit_cast<float>(registers[0x43FC]),std::bit_cast<float>(registers[0x43FD]),packing,std::bit_cast<float>(registers[0x43FF]));
      if (!(packing>0 && packing<=1) || (candidate&3)!=3 || candidate_size<4400 || captured>=12) return;
      auto* shader=graphics->command_processor()->active_vertex_shader();
      if (!shader) return;
      const auto shader_hash=shader->ucode_data_hash();
      const auto& bindings=shader->vertex_bindings();
      uint32_t stride=0;
      for (const auto& binding:bindings) if (binding.fetch_constant==95) stride=binding.stride_words*4;
      if (stride<40 || stride>52) return;
      ++captured;
      REXLOG_INFO("[skin-layout] sample={} shader={:016X} stride={}",sample,shader_hash,stride);
      std::ofstream gpu("V:/logs/vf95-gpu-"+std::to_string(sample)+".bin",std::ios::binary);
      gpu.write(reinterpret_cast<const char*>(registers.data()),sizeof(registers));
      const uint32_t palette_address=registers[0x4861]&0x1FFFF000u;
      if ((registers[0x4861]&63)==38 && palette_address && palette_address+524288<=0x20000000u) {
        std::unique_ptr<char[]> palette_bytes(new char[524288]);
        if (ReadProcessMemory(GetCurrentProcess(),REX_KERNEL_MEMORY()->TranslatePhysical(palette_address),palette_bytes.get(),524288,&copied) && copied==524288) {
          std::ofstream palette_file("V:/logs/vf95-gpu-palette-"+std::to_string(sample)+".bin",std::ios::binary);
          palette_file.write(palette_bytes.get(),524288);
        }
      }
      const uint32_t vertex_word=registers[0x48BE],size_word=registers[0x48BF];
      const uint32_t address=vertex_word&0x1FFFFFFCu,size=((size_word>>2)&0xFFFFFFu)*4;
      if ((vertex_word&3)==3 && address>=0x10000 && size>=44 && size<=1024*1024 && uint64_t(address)+size<=0x20000000ull) {
        std::unique_ptr<char[]> vertices(new char[size]);
        if (ReadProcessMemory(GetCurrentProcess(),REX_KERNEL_MEMORY()->TranslatePhysical(address),vertices.get(),size,&copied) && copied==size) {
          std::ofstream vertex("V:/logs/vf95-gpu-vertices-"+std::to_string(sample)+".bin",std::ios::binary);
          vertex.write(vertices.get(),size);
        }
      }
      REXLOG_INFO("[skin-gpu-sample] sample={} vf95={:08X},{:08X} size={} endian={} tf16={:08X},{:08X}",sample,vertex_word,size_word,size,size_word&3,registers[0x4860],registers[0x4861]);
    }
  }
  if (sample>=12 && sample%3000!=0) return;
  const auto header=Read32(texture+32)&0xFFFFF000u;
  const auto physical=REX_KERNEL_MEMORY()->GetPhysicalAddress(header);
  const uint32_t fetch=device+1024+slot*24;
  REXLOG_INFO("[skin-bind] sample={} slot={} texture={:08X} cpu_physical={:08X} gpu_physical={:08X} fetch={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
              sample,slot,texture,physical,Read32(fetch+4)&0x1FFFF000u,
              Read32(fetch),Read32(fetch+4),Read32(fetch+8),Read32(fetch+12),Read32(fetch+16),Read32(fetch+20));
}

REX_EXTERN(sub_82584138) {
  if (!Enabled() || !trace.active) { __imp__sub_82584138(ctx, base); return; }
  const uint32_t output = ctx.r3.u32 & ~15u;
  const uint32_t left = ctx.r4.u32 & ~15u, right = ctx.r5.u32 & ~15u;
  std::array<float,16> a{}, b{};
  for (uint32_t i=0; i<16; ++i) { a[i]=ReadFloat(left+4*i); b[i]=ReadFloat(right+4*i); }
  __imp__sub_82584138(ctx, base);
  ++trace.matrices;
  if (!bone_products_written) {
    bone_products.insert(bone_products.end(),a.begin(),a.end());
    bone_products.insert(bone_products.end(),b.begin(),b.end());
    for (uint32_t i=0;i<12;++i) bone_products.push_back(ReadFloat(output+4*i));
  }
  if (!trace.data || output < trace.data || uint64_t(output)+48 > uint64_t(trace.data)+trace.pitch*128ull)
    ++trace.outside;
  // Independent scalar affine product and transpose, using PPC row ordering.
  for (uint32_t row=0; row<3; ++row) {
    for (uint32_t col=0; col<4; ++col) {
      float expected = a[col*4]*b[row];
      if (col==3) expected += b[12+row];
      expected += a[col*4+1]*b[4+row];
      expected += a[col*4+2]*b[8+row];
      const float actual = ReadFloat(output+4*(row*4+col));
      if (!std::isfinite(expected) || !std::isfinite(actual)) ++trace.nonfinite;
      else {
        trace.maximum = std::max(trace.maximum, std::abs(actual));
        if (std::abs(actual-expected) > 0.00001f*(1+std::abs(expected))) ++trace.mismatch;
      }
    }
  }
}
