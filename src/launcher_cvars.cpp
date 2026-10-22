#include "launcher_cvars.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <rex/logging.h>

#include "aspect_ratio.h"
#include "crash_report.h"
#include "diagnostics.h"
#include "frame_limiter.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "internal_render_scale.h"
#include "launcher_options.h"
#include "launcher_platform.h"
#include "retired_settings.h"
#include "system_info.h"

REXCVAR_DEFINE_BOOL(show_launcher, true, "Launcher",
                    "Show the launcher before the game starts (hold Shift at start to force it)");
REXCVAR_DEFINE_BOOL(gw2_skip_launcher, false, "Launcher",
                    "Skip the launcher once (set when the launcher restarts the game)");
REXCVAR_DEFINE_INT32(gw2_frame_rate, gw2::kFrameRateDefault, "Geometry Wars",
                     "Frame-rate limit: 30, 60, 90, 120, 144, 165, 240 or 0 for Unlimited");
REXCVAR_DEFINE_BOOL(gw2_vibration, gw2::kVibrationDefault, "Geometry Wars", "Controller vibration");
REXCVAR_DEFINE_BOOL(gw2_online_leaderboards, gw2::kOnlineLeaderboardsDefault, "Geometry Wars",
                    "Send finished runs' scores to the PC Plus leaderboard server and read the Everyone boards from it");
REXCVAR_DEFINE_STRING(gw2_player_name, "", "Geometry Wars", "The name shown on the online leaderboards");
REXCVAR_DEFINE_BOOL(gw2_player_name_asked, false, "Geometry Wars",
                    "Whether the launcher has asked for a player name (it asks once)");
REXCVAR_DEFINE_STRING(gw2_hud_colour, "green", "Geometry Wars",
                      "Menu and HUD colour: green, blue, purple, pink, gold, rainbow or custom");
REXCVAR_DEFINE_STRING(gw2_hud_colour_custom, "", "Geometry Wars", "The custom menu and HUD colour, as #RRGGBB");
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

std::string TextureFilteringName(std::string_view value) {
  if (value == "-1") {
    return "Game";
  }
  if (value == "0") {
    return "Off";
  }
  if (value == "2") {
    return "2x";
  }
  if (value == "3") {
    return "4x";
  }
  if (value == "4") {
    return "8x";
  }
  if (value == "5") {
    return "16x";
  }
  return std::string(value);
}

std::string SelectedGraphicsCardLabel() {
  const std::string setting = REXCVAR_GET(gw2_graphics_adapter);
  const std::optional<int> index = ParseGraphicsAdapter(setting);
  if (!index) {
    return "Automatic";
  }
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  for (const GraphicsAdapter& adapter : adapters) {
    if (adapter.index != *index) {
      continue;
    }
    const auto same_name = std::count_if(adapters.begin(), adapters.end(), [&](const GraphicsAdapter& other) {
      return other.name == adapter.name;
    });
    return same_name > 1 ? adapter.name + " (" + std::to_string(adapter.index) + ")" : adapter.name;
  }
  return "Graphics card " + setting + " (not found)";
}

std::string ChoiceLabel(const std::vector<SettingChoice>& choices, std::string_view value,
                        std::string_view fallback) {
  for (const SettingChoice& choice : choices) {
    if (choice.value == value) {
      return choice.label;
    }
  }
  return std::string(fallback);
}

std::string OnOff(bool value) {
  return value ? "On" : "Off";
}

std::string RenderResolutionLabel(int scale, AspectRatio aspect) {
  const WindowSize size = RenderSize(aspect, scale);
  return (scale == 1 ? std::string("Original") : std::to_string(scale) + "x") + " (" +
         std::to_string(size.width) + "x" + std::to_string(size.height) + ")";
}

std::string CvarOr(std::string_view name, std::string_view fallback) {
  const std::string value = rex::cvar::GetFlagByName(name);
  return value.empty() ? std::string(fallback) : value;
}

std::string GameStateName(GameFileState state) {
  switch (state) {
    case GameFileState::kValid:
      return "verified";
    case GameFileState::kDifferentVersion:
      return "different version";
    case GameFileState::kReadError:
      return "read error";
    case GameFileState::kMissing:
      return "missing";
  }
  return "unknown";
}

}  // namespace

void LoadPlayerSettings(const std::filesystem::path& player_file) {
  std::error_code ec;
  if (!std::filesystem::exists(player_file, ec)) {
    return;
  }
  // The retired Letterbox/Stretch line goes whatever its value: the picture always keeps the chosen format.
  const std::string settings_name = FileNameOnly(Utf8(player_file));
  switch (RemoveRetiredSetting(player_file, kRetiredAspectKey)) {
    case RetiredSettingResult::kRemoved:
      REXLOG_INFO("Removed the retired {} setting from {}", kRetiredAspectKey, settings_name);
      break;
    case RetiredSettingResult::kFailed:
      REXLOG_WARN("Could not remove the retired {} setting from {}", kRetiredAspectKey, settings_name);
      break;
    case RetiredSettingResult::kAbsent:
      break;
  }
  rex::cvar::LoadConfig(player_file);
  REXLOG_INFO("Player settings: {}", settings_name);
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

void LogStartupDiagnostics(const MonitorInfo& display, int monitor, const std::filesystem::path& game_dir,
                           const StartupWindowInfo& window) {
  const AspectRatio aspect = ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio));
  const int scale = std::max(1, IntCvar(kRenderScaleKey));
  const int percent = NormalizeInternalRenderScale(IntCvar(kInternalRenderScaleKey));
  const InternalRenderSplit split = SplitInternalRenderScale(scale, percent);
  const int fps = ConfiguredFrameRate();
  const bool vsync_off = rex::cvar::GetFlagByName(kVSyncKey) == "true";
  const bool selected_fullscreen = rex::cvar::GetFlagByName(kFullscreenKey) == "true";
  const std::string selected_card = SelectedGraphicsCardLabel();

  StartupDiagnostics diagnostics;
  diagnostics.launcher_display.mode = selected_fullscreen ? "Borderless Fullscreen" : "Windowed";
  const int selected_width = window.selected_width >= 0 ? window.selected_width : window.configured_width;
  const int selected_height = window.selected_height >= 0 ? window.selected_height : window.configured_height;
  diagnostics.launcher_display.resolution =
      selected_width > 0 && selected_height > 0
          ? std::to_string(selected_width) + "x" + std::to_string(selected_height)
          : "Default";
  diagnostics.launcher_display.aspect_ratio = InfoFor(aspect).label;
  diagnostics.launcher_display.monitor = monitor > 0 ? std::to_string(monitor) : "Default (primary)";

  diagnostics.launcher_gameplay.frame_rate = FrameRateText(fps);
  diagnostics.launcher_gameplay.vsync = vsync_off ? "Off" : "On";
  diagnostics.launcher_gameplay.frame_rate_in_title = OnOff(REXCVAR_GET(gw2_show_fps));
  diagnostics.launcher_gameplay.render_resolution_in_title = OnOff(REXCVAR_GET(gw2_show_render_resolution));
  diagnostics.launcher_gameplay.frame_counter = REXCVAR_GET(gw2_show_frame_counter) ? "Shown" : "Hidden";

  diagnostics.launcher_graphics.graphics_card = selected_card;
  diagnostics.launcher_graphics.render_resolution = RenderResolutionLabel(scale, aspect);
  diagnostics.launcher_graphics.internal_render_resolution = std::to_string(percent) + "%";
  diagnostics.launcher_graphics.msaa = REXCVAR_GET(gw2_msaa_compatibility) ? "4x (Compatibility)" : "2x";
  diagnostics.launcher_graphics.anti_aliasing =
      ChoiceLabel(AntiAliasingChoices(), CvarOr(kAntiAliasingKey, "none"), "Off");
  diagnostics.launcher_graphics.texture_filtering = TextureFilteringName(CvarOr(kTextureFilteringKey, "-1"));
  diagnostics.launcher_graphics.bloom = ChoiceLabel(BloomChoices(), CvarOr(kBloomKey, "original"), "Original");

  diagnostics.system.os = OsDescription();
  diagnostics.system.cpu = CpuDescription();
  diagnostics.system.logical_processors = LogicalProcessorCount();

  diagnostics.gpu.selected = selected_card;
  const GraphicsAdapterDetails adapter =
      EffectiveGraphicsAdapter(std::atoi(CvarOr(kD3D12AdapterKey, "-1").c_str()));
  diagnostics.gpu.resolved = adapter.name;
  diagnostics.gpu.adapter = adapter.index;
  diagnostics.gpu.vendor_id = adapter.vendor_id;
  diagnostics.gpu.device_id = adapter.device_id;
  diagnostics.gpu.resolved_known = adapter.known;

  diagnostics.display.monitor = monitor;
  diagnostics.display.width = display.width;
  diagnostics.display.height = display.height;
  diagnostics.display.refresh_hz = display.refresh_hz;
  diagnostics.display.scale_percent = int(std::lround(display.scale * 100.0f));
  diagnostics.display.fullscreen = window.fullscreen;
  diagnostics.display.configured_width = window.configured_width;
  diagnostics.display.configured_height = window.configured_height;
  diagnostics.display.output_width = window.output_width;
  diagnostics.display.output_height = window.output_height;
  diagnostics.display.aspect = InfoFor(aspect).key;

  const GameCheck game = CheckGameFiles(game_dir);
  diagnostics.game.title_id = kTitleId;
  diagnostics.game.executable = "default.xex";
  diagnostics.game.size = game.xex_size;
  diagnostics.game.sha256 = game.xex_sha256;
  diagnostics.game.fingerprint = GameStateName(game.state);

  diagnostics.gameplay.simulation = SimulationRateText(fps);
  diagnostics.gameplay.interpolation = InterpolationPolicyText(fps);
  diagnostics.gameplay.present = PresentationText(fps, vsync_off);

  diagnostics.graphics.render_percent = percent;
  diagnostics.graphics.render_width = InternalRenderSize(CanvasWidth(aspect), scale, percent);
  diagnostics.graphics.render_height = InternalRenderSize(kCanvasHeight, scale, percent);
  diagnostics.graphics.guest_width = CanvasWidth(aspect);
  diagnostics.graphics.guest_height = kCanvasHeight;
  diagnostics.graphics.host_quarters = split.host_quarters;
  diagnostics.graphics.resolve_scale = split.resolve_scale;
  diagnostics.graphics.msaa = REXCVAR_GET(gw2_msaa_compatibility) ? "4x compatibility" : "2x native";

  diagnostics.input.keyboard_mode = CvarOr("mnk_mode", "false") == "true" ? "On" : "Off";
  diagnostics.input.controller = window.controller;

  for (const std::string& line : FormatStartupDiagnostics(diagnostics)) {
    REXLOG_INFO("{}", line);
  }
  SetCrashRuntimeState(diagnostics.launcher_gameplay.frame_rate.c_str(), diagnostics.gameplay.simulation.c_str(),
                       diagnostics.gameplay.interpolation.c_str(), diagnostics.gameplay.present.c_str(),
                       diagnostics.gpu.resolved_known ? diagnostics.gpu.resolved.c_str() : "unknown");
}

}  // namespace gw2
