#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

void Fifa07ConfigureGraphics(rex::RuntimeConfig& config) {
  if(config.gpu_plugin!="xenos")return;
  // Plugin CVars are registered when the plugin is loaded. Load at the same
  // pre-presentation phase as ReXApp, then select the path before GPU setup.
  if(!config.graphics)config.graphics=rex::system::LoadGpuPlugin(config.gpu_plugin);
  if(!config.graphics)return; // ReXApp reports the original plugin-load failure.
  constexpr auto flag="render_target_path_d3d12";
  if(rex::cvar::GetFlagByName(flag).empty()) {
    // The pinned RTV path draws black for XMedia YUV videos despite valid
    // decoded frames, geometry and shaders. ROV is user-verified to render them.
    // Explicit configuration / CLI choices (including RTV for A/B) still win.
    if(!rex::cvar::SetFlagByName(flag,"rov"))
      REXLOG_ERROR("[cinematic-fix] could not select the D3D12 ROV path");
  }
  REXLOG_INFO("[cinematic-fix] D3D12 render target path={}",rex::cvar::GetFlagByName(flag));
}
