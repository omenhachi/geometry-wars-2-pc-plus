#pragma once

#include <filesystem>
#include <memory>

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/windowed_app_context.h>

#include "achievements_overlay.h"
#include "live_fix.h"

class GeometryWarsRetroEvolved2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GeometryWarsRetroEvolved2App>(
        new GeometryWarsRetroEvolved2App(ctx, "geometry_wars_retro_evolved_2", PPCImageConfig));
  }

 protected:
  void OnPostSetup() override {
    auto* kernel = runtime()->kernel_state();
    const auto* profile = kernel ? kernel->user_profile() : nullptr;
    if (!profile) return;
    gw2::live::Install({
        .stats_file = gw2::StatsFilePath(std::filesystem::absolute(user_data_root()), kernel->title_id()),
        .user = {profile->xuid(), profile->name()},
        .bridge = {
            .open_achievements = [this] { app_context().CallInUIThreadDeferred([this] { OpenAchievements(); }); },
            .close_achievements = [this] { app_context().CallInUIThreadDeferred([this] { CloseAchievements(); }); },
        },
    });
  }

  void OnShutdown() override { CloseAchievements(); }

  // F7 toggles the same overlay as the game's Achievements item. The overlay is owned here, not by ReXApp.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    if (achievements_) {
      CloseAchievements();
    } else {
      OpenAchievements();
    }
    return nullptr;
  }

 private:
  void OpenAchievements() {
    if (achievements_ || !runtime() || !runtime()->kernel_state() || !imgui_drawer() || !immediate_drawer()) return;
    achievements_ = std::make_unique<gw2::AchievementsOverlay>(
        imgui_drawer(), immediate_drawer(), runtime(), &achievements(),
        gw2::AchievementsOverlay::Controls{
            .request_close = [this] { app_context().CallInUIThreadDeferred([this] { CloseAchievements(); }); },
            .scroll_axis = [] { return gw2::live::AchievementsScrollAxis(); },
        });
    gw2::live::SetAchievementsModal(true);
  }

  void CloseAchievements() {
    if (!achievements_) return;
    achievements_.reset();
    gw2::live::SetAchievementsModal(false);
  }

  std::unique_ptr<gw2::AchievementsOverlay> achievements_;
};
