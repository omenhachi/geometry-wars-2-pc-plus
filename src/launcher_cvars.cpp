#include "launcher_cvars.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

#include <rex/logging.h>

#include "aspect_ratio.h"
#include "frame_limiter.h"
#include "graphics_adapters.h"
#include "internal_render_scale.h"
#include "launcher_options.h"
#include "launcher_platform.h"
#include "retired_settings.h"

REXCVAR_DEFINE_BOOL(show_launcher, true, "Launcher",
                    "Show the launcher before the game starts (hold Shift at start to force it)");
REXCVAR_DEFINE_BOOL(gw2_skip_launcher, false, "Launcher",
                    "Skip the launcher once (set when the launcher restarts the game)");
REXCVAR_DEFINE_INT32(gw2_frame_rate, gw2::kFrameRateDefault, "Geometry Wars",
                     "Frame-rate limit: 30, 60, 90, 120, 144, 165, 240 or 0 for Unlimited");
REXCVAR_DEFINE_BOOL(gw2_vibration, gw2::kVibrationDefault, "Geometry Wars", "Controller vibration");
REXCVAR_DEFINE_BOOL(gw2_show_fps, gw2::kShowFpsDefault, "Geometry Wars",
                    "Show the frame rate in the window title");
REXCVAR_DEFINE_BOOL(gw2_show_render_resolution, gw2::kShowResolutionDefault, "Geometry Wars",
                    "Show the render resolution in the window title");
REXCVAR_DEFINE_BOOL(gw2_show_frame_counter, gw2::kShowFrameCounterDefault, "Geometry Wars",
                    "Show the frame counter in the corner (F2 toggles it)");
// The launcher usually opens after the render target caches have read native_2x_msaa, so a change takes a new process.
REXCVAR_DEFINE_BOOL(gw2_msaa_compatibility, false, "Geometry Wars",
                    "Draw the game's 2x MSAA render targets with 4x MSAA, for GPUs or drivers whose native 2x MSAA "
                    "misrenders (sets native_2x_msaa)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gw2_graphics_adapter, gw2::kGraphicsAdapterAuto, "Geometry Wars",
                      "Graphics card: auto, or the index d3d12_adapter takes (applied at start)");
REXCVAR_DEFINE_STRING(gw2_aspect_ratio, "16:9", "Geometry Wars",
                      "Aspect ratio the game renders in: 16:9, 16:10, 21:9, 32:9, 4:3 or 5:4 (applied at start)");
REXCVAR_DEFINE_INT32(gw2_launcher_x, 0, "Launcher", "Launcher window position, left edge (set by the launcher)");
REXCVAR_DEFINE_INT32(gw2_launcher_y, 0, "Launcher", "Launcher window position, top edge (set by the launcher)");
REXCVAR_DEFINE_INT32(gw2_launcher_width, 0, "Launcher", "Launcher window width; 0 uses the default (set by the launcher)");
REXCVAR_DEFINE_INT32(gw2_launcher_height, 0, "Launcher",
                     "Launcher window height; 0 uses the default (set by the launcher)");
REXCVAR_DEFINE_BOOL(gw2_launcher_maximized, false, "Launcher", "Open the launcher maximized (set by the launcher)");
REXCVAR_DEFINE_BOOL(gw2_launcher_fullscreen, false, "Launcher",
                    "Open the launcher borderless fullscreen (set by the launcher)");

namespace gw2 {
namespace {

int IntCvar(std::string_view name) {
  return std::atoi(rex::cvar::GetFlagByName(name).c_str());
}

}  // namespace

void LoadPlayerSettings(const std::filesystem::path& player_file) {
  std::error_code ec;
  if (!std::filesystem::exists(player_file, ec)) {
    return;
  }
  // The retired Letterbox/Stretch line goes whatever its value: the picture always keeps the chosen format.
  switch (RemoveRetiredSetting(player_file, kRetiredAspectKey)) {
    case RetiredSettingResult::kRemoved:
      REXLOG_INFO("Removed the retired {} setting from {}", kRetiredAspectKey, Utf8(player_file));
      break;
    case RetiredSettingResult::kFailed:
      REXLOG_WARN("Could not remove the retired {} setting from {}", kRetiredAspectKey, Utf8(player_file));
      break;
    case RetiredSettingResult::kAbsent:
      break;
  }
  rex::cvar::LoadConfig(player_file);
  REXLOG_INFO("Player settings: {}", Utf8(player_file));
}

bool TakeSkipLauncher() {
  const bool skip = REXCVAR_GET(gw2_skip_launcher);
  rex::cvar::ResetToDefault("gw2_skip_launcher");
  return skip;
}

void FitWindowToDisplay() {
  const int width = IntCvar(kWindowWidthKey);
  const int height = IntCvar(kWindowHeightKey);
  if (width <= 0 || height <= 0) {
    return;
  }
  const MonitorInfo display = MonitorForSetting(IntCvar(kMonitorKey));
  if (display.width == 0) {
    return;
  }
  const auto [fit_width, fit_height] = FitWindowSize(width, height, display);
  if (fit_width == width && fit_height == height) {
    return;
  }
  REXLOG_INFO("Window reduced from {}x{} to {}x{} to fit the display", width, height, fit_width, fit_height);
  rex::cvar::SetFlagByName(kWindowWidthKey, std::to_string(fit_width));
  rex::cvar::SetFlagByName(kWindowHeightKey, std::to_string(fit_height));
}

void SettleStartupCvars() {
  FitWindowToDisplay();
  // The SDK marks a restart-only cvar as pending whenever a config file sets it, but nothing it configures exists
  // yet. Starting from none makes PLAY restart only for what the launcher changes.
  rex::cvar::ClearPendingRestartFlags();
}

void ApplyGraphicsAdapter() {
#if defined(_WIN32)
  const std::string setting = REXCVAR_GET(gw2_graphics_adapter);
  if (!ParseGraphicsAdapter(setting)) {
    REXLOG_INFO("Graphics card: Automatic");
    return;
  }
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  const int index = ResolveGraphicsAdapter(setting, adapters);
  if (index < 0) {
    REXLOG_WARN("Graphics card {} is not available, using Automatic", setting);
    return;
  }
  rex::cvar::SetFlagByName(kD3D12AdapterKey, std::to_string(index));
  for (const GraphicsAdapter& adapter : adapters) {
    if (adapter.index == index) {
      REXLOG_INFO("Graphics card: {} (adapter {})", adapter.name, index);
    }
  }
#endif
}

void ApplyAspectRatio() {
  const AspectRatioInfo& info = InfoFor(ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio)));
  const WindowSize mode = VideoModeSize(info.aspect);
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(mode.width));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(mode.height));
  rex::cvar::SetFlagByName("present_letterbox", "true");
  REXLOG_INFO("Aspect ratio: {} ({}x{})", info.key, CanvasWidth(info.aspect), kCanvasHeight);
}

int ConfiguredFrameRate() {
  const int fps = REXCVAR_GET(gw2_frame_rate);
  return IsFrameRateChoice(fps) ? fps : kFrameRateDefault;
}

void LogGameSettings() {
  const AspectRatio aspect = ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio));
  const int scale = std::max(1, IntCvar(kRenderScaleKey));
  const int percent = NormalizeInternalRenderScale(IntCvar(kInternalRenderScaleKey));
  const InternalRenderSplit split = SplitInternalRenderScale(scale, percent);
  REXLOG_INFO("Internal render resolution: {}% {}x{} -> {}x{} (host render targets {}/4, resolves {}x)", percent,
              InternalRenderSize(CanvasWidth(aspect), scale, percent),
              InternalRenderSize(kCanvasHeight, scale, percent), CanvasWidth(aspect) * scale,
              kCanvasHeight * scale, split.host_quarters, split.resolve_scale);
  const int fps = ConfiguredFrameRate();
  REXLOG_INFO(
      "Game settings: render resolution {}x ({}x{}), window {}x{}{}, frame rate {}, VSync {}, multisampling {}",
      scale, CanvasWidth(aspect) * scale, kCanvasHeight * scale, IntCvar(kWindowWidthKey), IntCvar(kWindowHeightKey),
      rex::cvar::GetFlagByName(kFullscreenKey) == "true" ? " fullscreen" : "",
      fps == kFrameRateUnlimited ? std::string("Unlimited") : std::to_string(fps),
      rex::cvar::GetFlagByName(kVSyncKey) == "true" ? "Off" : "On",
      REXCVAR_GET(gw2_msaa_compatibility) ? "4x" : "2x");
}

}  // namespace gw2
