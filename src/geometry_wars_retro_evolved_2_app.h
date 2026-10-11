#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/ui/windowed_app_context.h>

#include "trg/platform.h"

#include "achievements_overlay.h"
#include "frame_counter.h"
#include "game_files.h"
#include "launcher.h"
#include "launcher_cvars.h"
#include "launcher_platform.h"
#include "launcher_settings.h"
#include "live_fix.h"
#include "window_host.h"

inline rex::ui::FrameStats DebugOverlayFrameStats() {
  const gw2::GuestFrameStats stats = gw2::GetGuestFrameStats();
  return {stats.frame_time_ms, stats.fps, stats.frame_count};
}

class GeometryWarsRetroEvolved2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GeometryWarsRetroEvolved2App>(
        new GeometryWarsRetroEvolved2App(ctx, "geometry_wars_retro_evolved_2", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
    }
    shipped_config_ = paths.config_path;
  }

  void OnPostInitLogging() override { gw2::LoadPlayerSettings(gw2::LauncherSettingsPath(user_data_root())); }

  // The requested window is recorded before the display fit caps it, so a size the player saved is not reported as
  // changed.
  void OnPreSetup(rex::RuntimeConfig&) override {
    settings_ = std::make_unique<gw2::GameSettings>(cvars_, gw2::LauncherSettingsPath(user_data_root()),
                                                    shipped_config_);
    settings_->Capture();
    settings_->MarkWindowCreated();
    gw2::FitWindowToDisplay();
    SetGuestFrameStats(DebugOverlayFrameStats);
    PlaceLauncherWindow();
  }

  // The window is shown inside Open(), so a saved placement has to be in effect before it is created.
  void PlaceLauncherWindow() {
    const bool game_ready = gw2::CheckGameFiles(game_data_root()).state == gw2::GameFileState::kValid;
    if (rex::cvar::GetFlagByName("fullscreen") == "true" ||
        !gw2::LauncherWanted(game_ready, gw2::ShowLauncherSetting(*settings_), trg::ShiftHeld(),
                             REXCVAR_GET(gw2_skip_launcher))) {
      return;
    }
    const gw2::WindowPlacement saved = gw2::ReadPlacement(*settings_);
    gw2::InstallPreShowPlacement(gw2::PlanPreShow(saved, gw2::WorkAreas()), saved);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    if (drawer) {
      gw2::CreateFrameCounter(drawer);
    }
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    gw2::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  // Missing or invalid game files open the launcher before the runtime exists, since it is the only way to install.
  // Valid files open it after the runtime is built, so the Achievements page reads the game's own achievement data.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    gw2::AttachWindowHost(window(), app_context(), [this] { ToggleWindowMode(); });
    settings_->CaptureLateRegistered();
    const bool game_ready = gw2::CheckGameFiles(defaults.game_data_root).state == gw2::GameFileState::kValid;
    const bool show = gw2::LauncherWanted(game_ready, gw2::ShowLauncherSetting(*settings_), trg::ShiftHeld(),
                                          gw2::TakeSkipLauncher());
    if (!show || !imgui_drawer()) {
      return defaults;
    }
    if (game_ready) {
      show_launcher_after_runtime_ = true;
      return defaults;
    }
    launcher_open_ = true;
    gw2::ShowLauncher(imgui_drawer(), {defaults.game_data_root, defaults.user_data_root}, *settings_, {},
                      {.play = [this, resume, defaults] {
                         launcher_open_ = false;
                         app_context().CallInUIThreadDeferred([resume, defaults] { resume(defaults); });
                       },
                       .restart = [this] { return RestartInNewWindow(); },
                       .quit = [this] {
                         launcher_open_ = false;
                         app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
                       },
                       .window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; },
                       .window_fullscreen = [this] { return window() && window()->IsFullscreen(); }});
    return std::nullopt;
  }

  void LaunchModule() override {
    if (!show_launcher_after_runtime_) {
      ReXApp::LaunchModule();
      return;
    }
    show_launcher_after_runtime_ = false;
    launcher_open_ = true;
    gw2::ShowLauncher(imgui_drawer(), {game_data_root(), user_data_root()}, *settings_,
                      {runtime(), immediate_drawer(), &achievements()},
                      {.play = [this] {
                         launcher_open_ = false;
                         ReXApp::LaunchModule();
                       },
                       .restart = [this] { return RestartInNewWindow(); },
                       .quit = [this] {
                         launcher_open_ = false;
                         app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
                       },
                       .window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; },
                       .window_fullscreen = [this] { return window() && window()->IsFullscreen(); }});
  }

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

  void OnShutdown() override {
    gw2::DetachWindowHost();
    CloseAchievements();
  }

  // F7 toggles the same overlay as the game's Achievements item. The overlay is owned here, not by ReXApp.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    if (launcher_open_) {
      return nullptr;
    }
    if (achievements_) {
      CloseAchievements();
    } else {
      OpenAchievements();
    }
    return nullptr;
  }

 private:
  bool RestartInNewWindow() {
    if (!gw2::RelaunchSelf(L"--gw2_skip_launcher=true")) {
      return false;
    }
    app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
    return true;
  }

  // F11 changes the window at once and keeps the choice as the player's setting for the next start.
  void ToggleWindowMode() {
    const bool fullscreen = !window()->IsFullscreen();
    const std::string value = fullscreen ? "true" : "false";
    window()->SetFullscreen(fullscreen);
    cvars_.Assign(gw2::kFullscreenKey, value);
    settings_->SaveNow(gw2::kFullscreenKey, value);
  }

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
  std::filesystem::path shipped_config_;
  gw2::RexCvars cvars_;
  std::unique_ptr<gw2::GameSettings> settings_;
  bool show_launcher_after_runtime_ = false;
  bool launcher_open_ = false;
};
