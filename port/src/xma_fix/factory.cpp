#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/audio/xma/decoder.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <bit>
#include <Windows.h>

namespace {
bool UseStockXma() {
  static const bool stock=[] {char flag[8]{};return GetEnvironmentVariableA("FIFA07_STOCK_XMA",flag,sizeof(flag))==1 && flag[0]=='1';}();
  return stock;
}
class FifaXmaAudioSystem final : public rex::audio::sdl::SDLAudioSystem {
 public:
  ~FifaXmaAudioSystem() override { xma_decoder_.reset(); }
  explicit FifaXmaAudioSystem(rex::runtime::FunctionDispatcher* dispatcher)
      : SDLAudioSystem(dispatcher) {
    // The base constructor creates an uninitialized decoder. Replace it before
    // Setup registers any MMIO range or starts a worker.
    xma_decoder_=std::make_unique<rex::audio::XmaDecoder>(dispatcher);
  }
  rex::X_STATUS Setup(rex::system::KernelState* kernel) override {
    const auto result=xma_decoder_->Setup(kernel);
    if(result)return result;
    worker_running_=true;
    worker_thread_=rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(kernel,128*1024,0,[this] {WorkerThreadMain();return 0;}));
    worker_thread_->set_name("Audio Worker");
    worker_thread_->Create();
    REXLOG_INFO("[xma-fix] reference state machine active; dedicated decode worker");
    return 0;
  }
};
}

void Fifa07ConfigureXmaFix(rex::RuntimeConfig& config) {
  if(UseStockXma())return;
  config.audio_factory=[](rex::runtime::FunctionDispatcher* dispatcher)
      ->std::unique_ptr<rex::system::IAudioSystem> {
    return std::make_unique<FifaXmaAudioSystem>(dispatcher);
  };
}

// The stock kernel executes kicked contexts inline inside its DLL. Route this
// entry point to the local decoder too, so every kick uses the corrected path.
REX_EXTERN(__imp__XMAEnableContext) {
  if(UseStockXma()) {
    static auto* original=reinterpret_cast<PPCFunc*>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),"__imp__XMAEnableContext"));
    original(ctx,base);return;
  }
  auto* audio=static_cast<rex::audio::AudioSystem*>(REX_KERNEL_STATE()->emulator()->audio_system());
  auto* decoder=audio->xma_decoder();
  const auto physical=REX_KERNEL_MEMORY()->GetPhysicalAddress(ctx.r3.u32);
  const auto index=(physical-decoder->context_array_ptr())/sizeof(rex::audio::XMA_CONTEXT_DATA);
  if(index>=320) {REXLOG_ERROR("[xma-fix] invalid context={:08X}",ctx.r3.u32);ctx.r3.u64=0;return;}
  decoder->WriteRegister(0x1940+(index>>5)*4,std::byteswap(uint32_t(1)<<(index&31)));
  ctx.r3.u64=0;
}
