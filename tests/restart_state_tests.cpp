// Restart tracking at startup, against the real ReXGlue cvar registry and the shipped config: what PLAY decides
// before and after SettleStartupCvars, on a display that fits the window and on one that caps it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "aspect_ratio.h"
#include "launcher_cvars.h"
#include "launcher_platform.h"
#include "launcher_settings.h"

using namespace gw2;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

void Section(const char* name) {
  std::printf("%s\n", name);
}

MonitorInfo g_display;

}  // namespace

MonitorInfo gw2::MonitorForSetting(int) {
  return g_display;
}

namespace {

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestStartupDiagnostics(const std::filesystem::path& shipped) {
  Section("startup diagnostics mirror launcher selections");
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "gw2_restart_state_tests" / "diagnostics";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  rex::LogConfig config;
  config.app_name = "gw2_startup_diagnostics";
  config.log_dir = dir.string();
  rex::InitLogging(config);

  rex::cvar::LoadConfig(shipped);
  rex::cvar::SetFlagByName(kFullscreenKey, "false");
  rex::cvar::SetFlagByName(kAspectKey, "21:9");
  rex::cvar::SetFlagByName(kGraphicsAdapterKey, "auto");
  rex::cvar::SetFlagByName(kMultisamplingKey, "true");
  rex::cvar::SetFlagByName(kFrameRateKey, "0");
  rex::cvar::SetFlagByName(kVSyncKey, "false");
  rex::cvar::SetFlagByName(kShowFpsKey, "true");
  rex::cvar::SetFlagByName(kShowResolutionKey, "false");
  rex::cvar::SetFlagByName(kShowFrameCounterKey, "true");

  const MonitorInfo display{1920, 1080, 1920, 1046, 1.5f, true, 60};
  StartupWindowInfo window;
  window.selected_width = 1260;
  window.selected_height = 540;
  window.configured_width = 1152;
  window.configured_height = 493;
  window.output_width = 1728;
  window.output_height = 740;
  window.controller = "Test Controller";
  LogStartupDiagnostics(display, 1, {}, window);
  rex::FlushLogging();

  std::filesystem::path log;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      log = entry.path();
      break;
    }
  }
  CHECK(!log.empty());
  const std::string text = log.empty() ? std::string() : ReadText(log);
  CHECK(Has(text, "Launcher Display: Mode=Windowed | Resolution=1260x540 | "
                  "Aspect Ratio=21:9 Ultrawide | Monitor=1"));
  CHECK(Has(text, "Launcher Gameplay: Frame Rate=Unlimited | VSync=On | Frame rate in title=On | "
                  "Render resolution in title=Off | Frame counter=Shown"));
  CHECK(Has(text, "Launcher Graphics: Graphics Card=Automatic | Render Resolution=Original (1680x720) | "
                  "Internal Render Resolution=100% | MSAA=4x (Compatibility) | Anti-aliasing=Off | "
                  "Texture Filtering=Game | Bloom=Original"));
  CHECK(!Has(text, "Launcher Graphics: Graphics Card=NVIDIA"));
  CHECK(Has(text, "Runtime GPU: selected=Automatic | resolved="));
  CHECK(Has(text, "Runtime display: 1920x1080@60Hz | scale=150% | monitor=1 | windowed | "
                  "window=1152x493 | output=1728x740 | aspect=21:9"));
  CHECK(Has(text, "Runtime timing: simulation=60Hz | interpolation=Automatic, by guest render rate | "
                  "present=guest Immediate | host VBlank (VSync On)"));

  rex::ShutdownLogging();
  std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main() {
  const std::filesystem::path shipped = std::filesystem::path(GW2_SOURCE_DIR) / "config" / "geometry_wars_retro_evolved_2.toml";
  const std::filesystem::path player = std::filesystem::temp_directory_path() / "gw2_restart_state_tests" / "settings.toml";
  RexCvars cvars;

  Section("the shipped config marks restart-only cvars as pending");
  rex::cvar::LoadConfig(shipped);
  CHECK(cvars.Value(kWindowWidthKey) == "1280");
  CHECK(cvars.RestartPending());

  Section("PLAY without changes stays in this process when the window fits");
  g_display = {2560, 1440, 2560, 1400, 1.0f, true};
  GameSettings fits(cvars, player, shipped);
  fits.Capture();
  fits.MarkWindowCreated();
  SettleStartupCvars();
  CHECK(cvars.Value(kWindowWidthKey) == "1280");
  CHECK(!cvars.RestartPending());
  CHECK(!fits.WindowChanged());
  CHECK(!fits.Apply());

  Section("... and when the display caps the window");
  rex::cvar::LoadConfig(shipped);
  CHECK(cvars.RestartPending());
  g_display = {1440, 900, 1366, 875, 1.0f, true};
  GameSettings capped(cvars, player, shipped);
  capped.Capture();
  capped.MarkWindowCreated();
  SettleStartupCvars();
  // 1280x720 shrunk by the width limit keeps its 16:9 shape: 1229x691.
  CHECK(cvars.Value(kWindowWidthKey) == "1229");
  CHECK(cvars.Value(kWindowHeightKey) == "691");
  CHECK(!cvars.RestartPending());
  CHECK(!capped.WindowChanged());
  CHECK(!capped.Apply());

  Section("changes after startup still restart");
  capped.Set(kWindowWidthKey, "1600");
  CHECK(capped.WindowChanged());
  capped.Set(kWindowWidthKey, "1280");
  CHECK(!capped.WindowChanged());
  CHECK(rex::cvar::SetFlagByName("present_dither", "true"));
  CHECK(cvars.RestartPending());
  CHECK(capped.Apply());

  Section("the aspect ratio set at startup leaves no restart pending");
  for (const AspectRatioInfo& info : kAspectRatios) {
    rex::cvar::LoadConfig(shipped);
    CHECK(rex::cvar::SetFlagByName("gw2_aspect_ratio", info.key));
    ApplyAspectRatio();
    const WindowSize mode = VideoModeSize(info.aspect);
    CHECK(cvars.Value("video_mode_width") == std::to_string(mode.width));
    CHECK(cvars.Value("video_mode_height") == std::to_string(mode.height));
    CHECK(cvars.Value("present_letterbox") == "true");
    CHECK(cvars.RestartPending());
    g_display = {3840, 2160, 3840, 2100, 1.0f, true};
    GameSettings started(cvars, player, shipped);
    started.Capture();
    started.MarkWindowCreated();
    SettleStartupCvars();
    CHECK(!cvars.RestartPending());
    CHECK(!started.WindowChanged());
    CHECK(!started.Apply());
    CHECK(started.Get(kAspectKey) == info.key);

    // Choosing another format in the launcher starts a new process, which applies it; this one is not touched.
    started.Set(kAspectKey, info.aspect == AspectRatio::k21x9 ? "16:9" : "21:9");
    CHECK(started.WindowChanged());
    CHECK(!started.Apply());
    CHECK(cvars.Value(kAspectKey) == info.key);
    CHECK(cvars.Value("video_mode_width") == std::to_string(mode.width));
  }

  TestStartupDiagnostics(shipped);

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
