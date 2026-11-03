// fifa07 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
void Fifa07ResetCubeSharing();
void Fifa07ReportCubeSharing(rex::memory::Memory* memory);
void Fifa07ConfigureXmaFix(rex::RuntimeConfig& config);
void Fifa07ConfigureGraphics(rex::RuntimeConfig& config);

class Fifa07App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnPostLoadXexImage() override { Fifa07ResetCubeSharing(); }
  void OnPreSetup(rex::RuntimeConfig& config) override {
    Fifa07ConfigureXmaFix(config);
    Fifa07ConfigureGraphics(config);
  }
  void OnShutdown() override {
    if (runtime() && runtime()->memory()) Fifa07ReportCubeSharing(runtime()->memory());
  }


  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fifa07App>(new Fifa07App(ctx, "fifa07",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
