// Launcher settings tests: the settings model over a fake cvar registry, the PLAY gate, the option models and the
// page list. Temporary folders only; no runtime, game files or user data.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aspect_ratio.h"
#include "bloom_rules.h"
#include "cheats.h"
#include "frame_limiter.h"
#include "graphics_adapters.h"
#include "internal_render_scale.h"
#include "launcher_options.h"
#include "launcher_play.h"
#include "launcher_platform.h"
#include "launcher_settings.h"
#include "multisampling.h"
#include "retired_settings.h"

namespace fs = std::filesystem;
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

const fs::path kRoot = fs::temp_directory_path() / "gw2_settings_tests";

#ifdef GW2_SRC_DIR
const fs::path kSrcDir = GW2_SRC_DIR;
#else
const fs::path kSrcDir = fs::path(__FILE__).parent_path().parent_path() / "src";
#endif

fs::path FreshDir(const std::string& name) {
  const fs::path dir = kRoot / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

void WriteText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The running game's registry: which cvars exist, their values and defaults, and what a restart still needs.
class FakeCvars final : public CvarAccess {
 public:
  bool IsRegistered(std::string_view name) const override { return registered.count(std::string(name)) != 0; }

  std::string Value(std::string_view name) const override {
    const auto it = values.find(std::string(name));
    return it == values.end() ? std::string() : it->second;
  }

  std::string DefaultValue(std::string_view name) const override {
    const auto it = defaults.find(std::string(name));
    return it == defaults.end() ? std::string() : it->second;
  }

  bool Assign(std::string_view name, std::string_view value) override {
    ++assigns;
    if (refused.count(std::string(name)) != 0) {
      return false;
    }
    values[std::string(name)] = std::string(value);
    restart_pending = restart_pending || restart_only.count(std::string(name)) != 0;
    return true;
  }

  bool RestartPending() const override { return restart_pending; }

  void Register(std::string_view name, std::string value) {
    registered.insert(std::string(name));
    defaults[std::string(name)] = value;
    values[std::string(name)] = std::move(value);
  }

  std::set<std::string> registered;
  std::map<std::string, std::string> values;
  std::map<std::string, std::string> defaults;
  std::set<std::string> refused;
  std::set<std::string> restart_only;
  bool restart_pending = false;
  int assigns = 0;
};

void RegisterGameDefaults(FakeCvars& cvars) {
  cvars.Register(kShowLauncherKey, "true");
  cvars.Register(kFullscreenKey, "false");
  cvars.Register(kWindowWidthKey, "1280");
  cvars.Register(kWindowHeightKey, "720");
  cvars.Register(kMonitorKey, "1");
  cvars.Register(kVSyncKey, "true");
  cvars.Register(kAspectKey, "16:9");
  cvars.Register(kGraphicsAdapterKey, kGraphicsAdapterAuto);
  cvars.Register(kRenderScaleKey, "1");
  cvars.Register(kInternalRenderScaleKey, "100");
  cvars.Register(kAntiAliasingKey, "none");
  cvars.Register(kMultisamplingKey, "false");
  cvars.Register(kNative2xMsaaKey, "true");
  cvars.Register(kTextureFilteringKey, "3");
  cvars.Register(kBloomKey, "original");
  cvars.Register(kKeyboardKey, "true");
  cvars.Register(kVibrationKey, kVibrationDefault ? "true" : "false");
  cvars.Register(kFrameRateKey, std::to_string(kFrameRateDefault));
  cvars.Register(kLauncherXKey, "0");
  cvars.Register(kLauncherYKey, "0");
  cvars.Register(kLauncherWidthKey, "0");
  cvars.Register(kLauncherHeightKey, "0");
  cvars.Register(kLauncherMaximizedKey, "false");
  cvars.Register(kLauncherFullscreenKey, "false");
  cvars.Register(kShowFpsKey, kShowFpsDefault ? "true" : "false");
  cvars.Register(kShowResolutionKey, kShowResolutionDefault ? "true" : "false");
  cvars.Register(kShowFrameCounterKey, kShowFrameCounterDefault ? "true" : "false");
  for (const cheats::CheatInfo& cheat : cheats::kCheats) {
    cvars.Register(cheat.key, cheats::kCheatDefault ? "true" : "false");
  }
}

void TestShippedValues() {
  Section("shipped config");
  const fs::path dir = FreshDir("shipped");
  WriteText(dir / "shipped.toml",
            "# shipped\nwindow_width = 1280\nfullscreen = false\nswap_post_effect = \"fxaa\"\n"
            "anisotropic_override = -1\n[section]\nignored = 2\n");
  const Values values = ReadSettingsValues(dir / "shipped.toml");
  CHECK(values.at("window_width") == "1280");
  CHECK(values.at("fullscreen") == "false");
  CHECK(values.at("swap_post_effect") == "fxaa");
  CHECK(values.at("anisotropic_override") == "-1");
  CHECK(values.find("ignored") == values.end());
  CHECK(ReadSettingsValues(dir / "absent.toml").empty());
}

void TestSaveWritesChangedValuesOnly() {
  Section("save without play");
  const fs::path dir = FreshDir("save");
  const fs::path file = LauncherSettingsPath(dir);
  WriteText(file, "# my note\nunrelated = 7\nfullscreen = false\n");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.Set(kFullscreenKey, "true");
  settings.Set(kWindowWidthKey, "853");
  settings.Set(kAntiAliasingKey, "fxaa_extreme");
  settings.Set(kTextureFilteringKey, "-1");
  CHECK(settings.Save());
  CHECK(cvars.assigns == 0);

  const std::string text = ReadText(file);
  CHECK(text.find("# my note") != std::string::npos);
  CHECK(text.find("unrelated = 7") != std::string::npos);
  CHECK(text.find("fullscreen = true") != std::string::npos);
  CHECK(text.find("window_width = 853") != std::string::npos);
  CHECK(text.find("swap_post_effect = \"fxaa_extreme\"") != std::string::npos);
  CHECK(text.find("anisotropic_override = -1") != std::string::npos);
  CHECK(text.find("show_launcher") == std::string::npos);

  const std::string before = ReadText(file);
  CHECK(settings.Save());
  CHECK(ReadText(file) == before);
}

void TestSaveRefusesUnregisteredSetting() {
  Section("save refuses unregistered settings");
  const fs::path dir = FreshDir("unregistered");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.registered.erase(kRenderScaleKey);
  GameSettings settings(cvars, LauncherSettingsPath(dir), dir / "missing.toml");
  settings.Capture();
  settings.Set(kRenderScaleKey, "2");
  CHECK(!settings.Save());
  CHECK(!fs::exists(LauncherSettingsPath(dir)));
}

void TestLateRegisteredSettings() {
  Section("late registered settings");
  const fs::path dir = FreshDir("late");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.registered.erase(kRenderScaleKey);
  cvars.values.erase(kRenderScaleKey);
  cvars.defaults.erase(kRenderScaleKey);
  GameSettings settings(cvars, LauncherSettingsPath(dir), dir / "missing.toml");
  settings.Capture();
  CHECK(settings.Get(kRenderScaleKey, "1") == "1");
  cvars.Register(kRenderScaleKey, "2");
  settings.CaptureLateRegistered();
  CHECK(settings.Get(kRenderScaleKey, "1") == "2");
}

void TestApplyAndRestartNeeds() {
  Section("apply");
  const fs::path dir = FreshDir("apply");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, LauncherSettingsPath(dir), dir / "missing.toml");
  settings.Capture();
  settings.Set(kAntiAliasingKey, "fxaa");
  settings.Set(kWindowWidthKey, "853");
  CHECK(!settings.Apply());
  CHECK(cvars.values[kAntiAliasingKey] == "fxaa");
  CHECK(cvars.values[kWindowWidthKey] == "1280");
  CHECK(cvars.assigns == 1);

  settings.Set(kMultisamplingKey, "true");
  cvars.refused.insert(kMultisamplingKey);
  CHECK(settings.Apply());

  cvars.refused.clear();
  settings.Set(kRenderScaleKey, "3");
  cvars.restart_pending = true;
  CHECK(settings.Apply());
}

void TestWindowChangeDetection() {
  Section("window change detection");
  const fs::path dir = FreshDir("window");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, LauncherSettingsPath(dir), dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(!settings.WindowChanged());

  settings.Set(kMonitorKey, "0");
  CHECK(!settings.WindowChanged());
  settings.Set(kFullscreenKey, "true");
  CHECK(!settings.WindowChanged());
  settings.Set(kAntiAliasingKey, "fxaa");
  CHECK(!settings.WindowChanged());

  settings.Set(kWindowWidthKey, "853");
  CHECK(settings.WindowChanged());
  settings.Set(kWindowWidthKey, "1280");
  CHECK(!settings.WindowChanged());

  settings.Set(kVSyncKey, "false");
  CHECK(settings.WindowChanged());
  settings.Set(kVSyncKey, "true");
  CHECK(!settings.WindowChanged());

  settings.Set(kMonitorKey, "2");
  CHECK(settings.WindowChanged());
}

void TestWindowChangeIgnoresFitAndCapture() {
  Section("requested window size");
  const fs::path dir = FreshDir("requested");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, LauncherSettingsPath(dir), dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  cvars.values[kWindowWidthKey] = "1024";
  cvars.values[kWindowHeightKey] = "576";
  CHECK(!settings.WindowChanged());
}

void TestResetToDefaults() {
  Section("reset settings");
  const fs::path dir = FreshDir("reset");
  const fs::path user = dir / "user_data";
  const fs::path file = LauncherSettingsPath(user);
  WriteText(user / "584108FF.profile", "profile");
  WriteText(user / "stats.toml", "scores");
  WriteText(user / "achievements" / "584108FF.toml", "unlocked");
  WriteText(file, "fullscreen = true\nwindow_width = 853\n");
  WriteText(dir / "shipped.toml", "window_width = 1000\nfullscreen = false\n");

  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.values[kFullscreenKey] = "true";
  cvars.values[kWindowWidthKey] = "853";
  GameSettings settings(cvars, file, dir / "shipped.toml");
  settings.Capture();
  settings.ResetToDefaults();
  CHECK(settings.Get(kFullscreenKey) == "false");
  CHECK(settings.Get(kWindowWidthKey) == "1000");
  CHECK(settings.Get(kShowLauncherKey) == "true");
  CHECK(settings.Save());

  const std::string text = ReadText(file);
  CHECK(text.find("fullscreen = false") != std::string::npos);
  CHECK(text.find("window_width = 1000") != std::string::npos);
  CHECK(ReadText(user / "584108FF.profile") == "profile");
  CHECK(ReadText(user / "stats.toml") == "scores");
  CHECK(ReadText(user / "achievements" / "584108FF.toml") == "unlocked");
}

void TestSaveNowKeepsOtherPendingEdits() {
  Section("window mode toggle keeps pending edits");
  const fs::path dir = FreshDir("toggle");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.Set(kRenderScaleKey, "3");
  CHECK(settings.SaveNow(kFullscreenKey, "true"));
  CHECK(settings.Get(kFullscreenKey) == "true");
  CHECK(settings.Get(kRenderScaleKey) == "3");
  const std::string text = ReadText(file);
  CHECK(text.find("fullscreen = true") != std::string::npos);
  CHECK(text.find("resolution_scale") == std::string::npos);
}

void TestPlayGate() {
  Section("play gate");
  PlayGate gate;
  int saves = 0;
  const auto save_ok = [&saves] {
    ++saves;
    return true;
  };
  const auto save_fails = [&saves] {
    ++saves;
    return false;
  };
  CHECK(gate.Request(false, false, save_ok) == PlayRequest::kFilesNotReady);
  CHECK(saves == 0);
  CHECK(gate.Request(true, true, save_ok) == PlayRequest::kBusy);
  CHECK(saves == 0);
  CHECK(gate.Request(false, true, save_fails) == PlayRequest::kSaveFailed);
  CHECK(saves == 1);
  CHECK(!gate.starting());
  CHECK(gate.Request(false, true, save_ok) == PlayRequest::kStarting);
  CHECK(saves == 2);
  CHECK(gate.starting());
  CHECK(gate.Request(false, true, save_ok) == PlayRequest::kBusy);
  CHECK(saves == 2);
  gate.Cancel();
  CHECK(!gate.starting());
  CHECK(gate.Request(false, true, save_ok) == PlayRequest::kStarting);
  CHECK(saves == 3);
}

void TestLauncherPolicy() {
  Section("launcher policy");
  CHECK(LauncherWanted(false, false, false, false));
  CHECK(LauncherWanted(false, false, false, true));
  CHECK(LauncherWanted(true, true, false, false));
  CHECK(!LauncherWanted(true, false, false, false));
  CHECK(LauncherWanted(true, false, true, false));
  CHECK(!LauncherWanted(true, false, true, true));
  CHECK(!LauncherWanted(true, true, false, true));
}

void TestWindowSizeOptions() {
  Section("window size options");
  const MonitorInfo qhd{2560, 1440, 2560, 1400, 2.25f, true};
  CHECK(!WindowSizeFits(WindowSize{1280, 720}, qhd));
  CHECK(WindowSizeFits(WindowSize{853, 480}, qhd));
  CHECK(WindowSizeFits(WindowSize{1920, 1080}, MonitorInfo{}));
  CHECK(WindowSizeLabel(WindowSize{1280, 720}, 1.0f) == "1280 \xC3\x97 720");
  CHECK(WindowSizeLabel(WindowSize{1280, 720}, 2.25f) == "1280 \xC3\x97 720   (2880 \xC3\x97 1620 on screen)");
  const std::vector<MonitorInfo> monitors = {{2560, 1440, 2560, 1400, 2.25f, true},
                                             {1920, 1080, 1920, 1040, 1.0f, false}};
  CHECK(MonitorLabel(0, monitors) == "Default (primary)");
  CHECK(MonitorLabel(1, monitors) == "Display 1 (primary)   2560 \xC3\x97 1440");
  CHECK(MonitorLabel(2, monitors) == "Display 2   1920 \xC3\x97 1080");
  CHECK(MonitorLabel(3, monitors) == "Display 3 (not connected)");
}

void TestGraphicsOptions() {
  Section("graphics options");
  const std::vector<SettingChoice> scales = RenderScaleChoices();
  CHECK(scales.size() == 4);
  CHECK(scales[0].value == "1" && scales[3].value == "4");
  CHECK(RenderScaleLabel(1) == "Original \xE2\x80\x94 1280 \xC3\x97 720");
  CHECK(RenderScaleLabel(2) == "2x \xE2\x80\x94 2560 \xC3\x97 1440 \xE2\x80\x94 Recommended for 1440p");
  CHECK(RenderScaleLabel(4) == "4x \xE2\x80\x94 5120 \xC3\x97 2880");

  const std::vector<SettingChoice> aa = AntiAliasingChoices();
  CHECK(aa.size() == 3);
  CHECK(aa[0].value == "none" && aa[1].value == "fxaa" && aa[2].value == "fxaa_extreme");

  const std::vector<SettingChoice> filtering = TextureFilteringChoices();
  CHECK(filtering.size() == 6);
  CHECK(filtering[0].value == "-1" && filtering[0].label == "Game");
  CHECK(filtering[1].value == "0" && filtering[1].label == "Off");
  CHECK(filtering[5].value == "5" && filtering[5].label == "16x");
}

void TestPagesAndKeyList() {
  Section("pages and key list");
  CHECK(std::size(kPageOrder) == 9);
  CHECK(PageName(kPageOrder[0]) == "Play");
  CHECK(PageName(kPageOrder[1]) == "Display");
  CHECK(PageName(kPageOrder[2]) == "Graphics");
  CHECK(PageName(kPageOrder[3]) == "Gameplay");
  CHECK(PageName(kPageOrder[4]) == "Cheats");
  CHECK(PageName(kPageOrder[5]) == "Controls");
  CHECK(PageName(kPageOrder[6]) == "Achievements");
  CHECK(PageName(kPageOrder[7]) == "About");
  CHECK(PageName(kPageOrder[8]) == "Discord");
  CHECK(KeyList("Semicolon,Space") == "Semicolon or Space");
  CHECK(KeyList("") == "None");
}

void TestSettingsFileValues() {
  Section("settings file values");
  const fs::path dir = FreshDir("values");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.Set(kFullscreenKey, "true");
  settings.Set(kRenderScaleKey, "2");
  settings.Set(kKeyboardKey, "false");
  CHECK(settings.Save());
  const std::string text = ReadText(file);
  CHECK(text.find("fullscreen = true") != std::string::npos);
  CHECK(text.find("resolution_scale = 2") != std::string::npos);
  CHECK(text.find("mnk_mode = false") != std::string::npos);
  CHECK(ShowLauncherSetting(settings));
}

void TestFrameRateSetting() {
  Section("frame rate in title");
  CHECK(kShowFpsDefault);
  CHECK(std::string_view(kShowFpsKey) == "gw2_show_fps");
  bool managed = false;
  for (const ManagedSetting& setting : ManagedSettings()) {
    managed = managed || setting.key == kShowFpsKey;
  }
  CHECK(managed);

  const fs::path dir = FreshDir("frame_rate");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.values[kShowFpsKey] = "true";
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();

  settings.Set(kShowFpsKey, "false");
  CHECK(!settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kShowFpsKey] == "false");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_fps = false") != std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.Get(kShowFpsKey) == "true");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_fps = true") != std::string::npos);
  CHECK(!settings.WindowChanged());
}

void TestRenderResolutionSetting() {
  Section("render resolution in title");
  CHECK(!kShowResolutionDefault);
  CHECK(std::string_view(kShowResolutionKey) == "gw2_show_render_resolution");
  bool managed = false;
  for (const ManagedSetting& setting : ManagedSettings()) {
    managed = managed || setting.key == kShowResolutionKey;
  }
  CHECK(managed);

  const fs::path dir = FreshDir("render_resolution");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kShowResolutionKey) == "false");

  settings.Set(kShowResolutionKey, "true");
  CHECK(!settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kShowResolutionKey] == "true");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_render_resolution = true") != std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.Get(kShowResolutionKey) == "false");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_render_resolution = false") != std::string::npos);
}

void TestFrameCounterSetting() {
  Section("frame counter");
  CHECK(!kShowFrameCounterDefault);
  CHECK(std::string_view(kShowFrameCounterKey) == "gw2_show_frame_counter");
  bool managed = false;
  for (const ManagedSetting& setting : ManagedSettings()) {
    managed = managed || setting.key == kShowFrameCounterKey;
  }
  CHECK(managed);

  const fs::path dir = FreshDir("frame_counter");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kShowFrameCounterKey) == "false");

  settings.Set(kShowFrameCounterKey, "true");
  CHECK(!settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kShowFrameCounterKey] == "true");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_frame_counter = true") != std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.Get(kShowFrameCounterKey) == "false");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_show_frame_counter = false") != std::string::npos);
}

void TestFrameLimitSetting() {
  Section("frame rate limit");
  CHECK(std::string_view(kFrameRateKey) == "gw2_frame_rate");
  CHECK(kFrameRateDefault == 60);
  int managed = 0;
  for (const ManagedSetting& setting : ManagedSettings()) {
    managed += setting.key == kFrameRateKey;
    CHECK(setting.key != "gw2_high_fps_acknowledged");
  }
  CHECK(managed == 1);
  CHECK(!IsWindowSetting(kFrameRateKey));

  const std::vector<SettingChoice> choices = FrameRateChoices();
  CHECK(choices.size() == 8);
  CHECK(choices.front().value == "30" && choices.front().label == "30");
  CHECK(choices.back().value == "0" && choices.back().label == "Unlimited");

  const fs::path dir = FreshDir("frame_limit");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kFrameRateKey) == "60");

  settings.Set(kFrameRateKey, "144");
  CHECK(!settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kFrameRateKey] == "144");
  CHECK(settings.Save());
  const std::string text = ReadText(file);
  CHECK(text.find("gw2_frame_rate = 144") != std::string::npos);
  CHECK(text.find("gw2_high_fps_acknowledged") == std::string::npos);

  settings.Set(kFrameRateKey, "0");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_frame_rate = 0") != std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.Get(kFrameRateKey) == "60");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_frame_rate = 60") != std::string::npos);
}

void TestBloomSetting() {
  Section("bloom setting");
  CHECK(std::string_view(kBloomKey) == "gw2_bloom");
  int managed = 0;
  for (const ManagedSetting& setting : ManagedSettings()) {
    if (setting.key == kBloomKey) {
      ++managed;
      CHECK(setting.kind == ValueKind::kString);
    }
  }
  CHECK(managed == 1);
  CHECK(!IsWindowSetting(kBloomKey));

  const std::vector<SettingChoice> choices = BloomChoices();
  CHECK(choices.size() == 3);
  CHECK(choices[0].value == "original" && choices[0].label == "Original");
  CHECK(choices[1].value == "reduced" && choices[1].label == "Reduced");
  CHECK(choices[2].value == "off" && choices[2].label == "Off");
  for (const SettingChoice& choice : choices) {
    CHECK(bloom::ModeName(bloom::ParseMode(choice.value)) == choice.value);
  }
  CHECK(bloom::Scale(bloom::ParseMode("original")) == 1.0f);
  CHECK(bloom::Scale(bloom::ParseMode("reduced")) == 0.5f);
  CHECK(bloom::Scale(bloom::ParseMode("off")) == 0.0f);
  CHECK(bloom::ParseMode("") == bloom::Mode::kOriginal);
  CHECK(bloom::ParseMode("Off") == bloom::Mode::kOriginal);

  const fs::path dir = FreshDir("bloom");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kBloomKey) == "original");

  settings.Set(kBloomKey, "reduced");
  CHECK(!settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kBloomKey] == "reduced");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_bloom = \"reduced\"") != std::string::npos);

  settings.Set(kBloomKey, "off");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_bloom = \"off\"") != std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.Get(kBloomKey) == "original");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_bloom = \"original\"") != std::string::npos);
}

void TestMultisamplingSetting() {
  Section("multisampling");
  CHECK(std::string_view(kMultisamplingKey) == "gw2_msaa_compatibility");
  int managed = 0;
  for (const ManagedSetting& setting : ManagedSettings()) {
    if (setting.key == kMultisamplingKey) {
      ++managed;
      CHECK(setting.kind == ValueKind::kBool);
    }
    CHECK(setting.key != kNative2xMsaaKey);
  }
  CHECK(managed == 1);
  CHECK(!IsWindowSetting(kMultisamplingKey));

  // Guest 2x render targets are host 2x only with the 2x choice on a GPU that has it; 1x and 4x never change.
  for (const bool supported : {false, true}) {
    for (const bool compatibility : {false, true}) {
      CHECK(HostMsaaSamples(1, Native2xMsaaFor(compatibility), supported) == 1);
      CHECK(HostMsaaSamples(4, Native2xMsaaFor(compatibility), supported) == 4);
    }
  }
  CHECK(HostMsaaSamples(2, Native2xMsaaFor(false), true) == 2);
  CHECK(HostMsaaSamples(2, Native2xMsaaFor(true), true) == 4);
  CHECK(HostMsaaSamples(2, Native2xMsaaFor(false), false) == 4);

  // Player files from launchers with Off and 2x MSAA have only native_2x_msaa: both start at 2x.
  for (const char* old_value : {"true", "false"}) {
    FakeCvars cvars;
    RegisterGameDefaults(cvars);
    cvars.values[kNative2xMsaaKey] = old_value;
    ApplyMultisampling(cvars);
    CHECK(cvars.values[kNative2xMsaaKey] == "true");
  }

  const fs::path dir = FreshDir("multisampling");
  const fs::path file = LauncherSettingsPath(dir);
  WriteText(file, "native_2x_msaa = false\n");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.restart_only.insert(kMultisamplingKey);
  cvars.values[kNative2xMsaaKey] = "false";
  ApplyMultisampling(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kMultisamplingKey) == "false");
  CHECK(!settings.Apply());
  CHECK(cvars.values[kNative2xMsaaKey] == "true");

  settings.Set(kMultisamplingKey, "true");
  CHECK(!settings.WindowChanged());
  CHECK(settings.Save());
  CHECK(settings.Apply());
  CHECK(cvars.values[kNative2xMsaaKey] == "false");
  CHECK(ReadText(file).find("gw2_msaa_compatibility = true") != std::string::npos);

  // The next start loads the file over the defaults; the choice decides, not the old native_2x_msaa line.
  const auto start = [&] {
    FakeCvars next;
    RegisterGameDefaults(next);
    for (const auto& [key, value] : ReadSettingsValues(file)) {
      next.values[key] = value;
    }
    ApplyMultisampling(next);
    const std::string first = next.values[kNative2xMsaaKey];
    ApplyMultisampling(next);
    CHECK(next.values[kNative2xMsaaKey] == first);
    return first;
  };
  CHECK(start() == "false");

  settings.ResetToDefaults();
  CHECK(settings.Get(kMultisamplingKey) == "false");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_msaa_compatibility = false") != std::string::npos);
  CHECK(start() == "true");

  // Anything but a true value is 2x, and without the GPU plugin's cvar nothing is assigned.
  FakeCvars odd;
  RegisterGameDefaults(odd);
  odd.values[kMultisamplingKey] = "maybe";
  odd.values[kNative2xMsaaKey] = "false";
  ApplyMultisampling(odd);
  CHECK(odd.values[kNative2xMsaaKey] == "true");
  FakeCvars no_gpu;
  RegisterGameDefaults(no_gpu);
  no_gpu.registered.erase(kNative2xMsaaKey);
  ApplyMultisampling(no_gpu);
  CHECK(no_gpu.assigns == 0);

  const std::string launcher = ReadText(kSrcDir / "launcher.cpp");
  CHECK(launcher.find("kMultisamplingKey, false, \"2x MSAA\", \"4x MSAA (Compatibility)\"") != std::string::npos);
  CHECK(launcher.find("\"Off\", \"2x MSAA\"") == std::string::npos);
  const std::string cvar_source = ReadText(kSrcDir / "launcher_cvars.cpp");
  const std::size_t cvar = cvar_source.find("REXCVAR_DEFINE_BOOL(gw2_msaa_compatibility, false,");
  CHECK(cvar != std::string::npos);
  CHECK(cvar_source.find(".lifecycle(rex::cvar::Lifecycle::kRequiresRestart);", cvar) <
        cvar_source.find("REXCVAR_DEFINE", cvar + 1));
  const std::string app = ReadText(kSrcDir / "geometry_wars_retro_evolved_2_app.h");
  CHECK(app.find("gw2::ApplyMultisampling(cvars_);") > app.find("std::optional<rex::PathConfig> OnFinalizePaths("));
  CHECK(app.find("gw2::ApplyMultisampling(cvars_);") < app.find("void LaunchModule()"));
}

int ManagedCount(std::string_view key, ValueKind kind) {
  int count = 0;
  for (const ManagedSetting& setting : ManagedSettings()) {
    if (setting.key == key) {
      ++count;
      CHECK(setting.kind == kind);
    }
  }
  return count;
}

// Render path: Automatic keeps the old behaviour (host render targets only for a reduced scale, any configured path
// kept), Fast always asks for host render targets, Compatible for the rasterizer-ordered path at 100%.
void TestRenderPathSetting() {
  CHECK(std::string_view(kRenderPathKey) == "gw2_render_path");
  CHECK(ManagedCount(kRenderPathKey, ValueKind::kString) == 1);
  CHECK(ParseRenderPath("fast") == RenderPath::kFast);
  CHECK(ParseRenderPath("compatible") == RenderPath::kCompatible);
  CHECK(ParseRenderPath("auto") == RenderPath::kAutomatic);
  CHECK(ParseRenderPath("") == RenderPath::kAutomatic);
  CHECK(ParseRenderPath("sparkly") == RenderPath::kAutomatic);

  RenderPathPlan plan = PlanRenderPath(RenderPath::kAutomatic, 100, "");
  CHECK(!plan.target_path && !plan.internal_scale);
  plan = PlanRenderPath(RenderPath::kAutomatic, 50, "");
  CHECK(plan.target_path && *plan.target_path == "rtv" && !plan.internal_scale);
  plan = PlanRenderPath(RenderPath::kAutomatic, 50, "rov");
  CHECK(!plan.target_path && !plan.internal_scale);

  plan = PlanRenderPath(RenderPath::kFast, 100, "");
  CHECK(plan.target_path && *plan.target_path == "rtv" && !plan.internal_scale);
  plan = PlanRenderPath(RenderPath::kFast, 100, "rov");
  CHECK(plan.target_path && *plan.target_path == "rtv");
  plan = PlanRenderPath(RenderPath::kFast, 50, "rtv");
  CHECK(!plan.target_path && !plan.internal_scale);

  plan = PlanRenderPath(RenderPath::kCompatible, 100, "");
  CHECK(plan.target_path && *plan.target_path == "rov" && !plan.internal_scale);
  plan = PlanRenderPath(RenderPath::kCompatible, 50, "");
  CHECK(plan.target_path && *plan.target_path == "rov" && plan.internal_scale && *plan.internal_scale == 100);
}

void TestInternalRenderScaleSetting() {
  Section("internal render resolution");
  CHECK(std::string_view(kInternalRenderScaleKey) == "gw2_internal_render_scale");
  CHECK(ManagedCount(kInternalRenderScaleKey, ValueKind::kInt) == 1);
  CHECK(!IsWindowSetting(kInternalRenderScaleKey));

  // The SDK's split: host render targets in quarter pixels, resolves at the smallest whole scale above them. The
  // host size is exactly percent of the render resolution for both 720p and 1080p guest pictures.
  for (int scale = 1; scale <= 4; ++scale) {
    for (const int percent : {100, 75, 50}) {
      const InternalRenderSplit split = SplitInternalRenderScale(scale, percent);
      for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080}}) {
        CHECK(width * split.host_quarters % 4 == 0 && height * split.host_quarters % 4 == 0);
        CHECK(width * split.host_quarters / 4 == width * scale * percent / 100);
        CHECK(height * split.host_quarters / 4 == height * scale * percent / 100);
      }
      CHECK(split.resolve_scale >= 1 && split.resolve_scale <= scale);
      CHECK(split.host_quarters <= split.resolve_scale * 4 && split.host_quarters > (split.resolve_scale - 1) * 4);
      if (percent == 100) {
        CHECK(split.host_quarters == scale * 4 && split.resolve_scale == scale);
      }
    }
  }
  CHECK(SplitInternalRenderScale(1, 75).host_quarters == 3 && SplitInternalRenderScale(1, 75).resolve_scale == 1);
  CHECK(SplitInternalRenderScale(4, 75).host_quarters == 12 && SplitInternalRenderScale(4, 75).resolve_scale == 3);
  CHECK(SplitInternalRenderScale(2, 50).host_quarters == 4 && SplitInternalRenderScale(2, 50).resolve_scale == 1);
  for (const int odd : {0, -1, 1, 25, 60, 90, 101}) {
    CHECK(NormalizeInternalRenderScale(odd) == 100);
  }

  // 75% is only faster where it lowers the resolve scale (measured: slower at 3x, no gain at 2x, +55% at 4x).
  CHECK(!InternalRenderScaleHelps(1, 75) && !InternalRenderScaleHelps(2, 75) && !InternalRenderScaleHelps(3, 75));
  CHECK(InternalRenderScaleHelps(4, 75));
  CHECK(InternalRenderScaleHelps(2, 50) && InternalRenderScaleHelps(3, 50) && InternalRenderScaleHelps(4, 50));
  CHECK(InternalRenderScaleHelps(1, 100));
  CHECK(InternalRenderScaleChoices(1).size() == 2);
  CHECK(InternalRenderScaleChoices(3).size() == 2);
  const std::vector<SettingChoice> kept = InternalRenderScaleChoices(3, 75);
  CHECK(kept.size() == 3 && kept[1].value == "75" && kept[1].label == "75% (no faster at this resolution)");

  const std::vector<SettingChoice> choices = InternalRenderScaleChoices(4);
  CHECK(choices.size() == 3);
  CHECK(choices[0].value == "100" && choices[0].label == "100% Native");
  CHECK(choices[1].value == "75" && choices[1].label == "75% Performance");
  CHECK(choices[2].value == "50" && choices[2].label == "50% Low-end");

  CHECK(!RequiredRenderTargetPath(100, ""));
  CHECK(RequiredRenderTargetPath(75, "") == "rtv");
  CHECK(RequiredRenderTargetPath(50, "") == "rtv");
  CHECK(!RequiredRenderTargetPath(60, ""));
  CHECK(!RequiredRenderTargetPath(75, "rov"));
  CHECK(!RequiredRenderTargetPath(50, "rtv"));

  // Vulkan builds have no D3D12 path: nothing is assigned.
  FakeCvars vulkan;
  RegisterGameDefaults(vulkan);
  vulkan.values[kInternalRenderScaleKey] = "50";
  CHECK(!ApplyInternalRenderPath(vulkan));
  CHECK(vulkan.assigns == 0);

  FakeCvars d3d12;
  RegisterGameDefaults(d3d12);
  d3d12.Register(kD3D12RenderTargetPathKey, "");
  CHECK(!ApplyInternalRenderPath(d3d12));
  CHECK(d3d12.values[kD3D12RenderTargetPathKey].empty());
  d3d12.values[kInternalRenderScaleKey] = "75";
  CHECK(ApplyInternalRenderPath(d3d12));
  CHECK(d3d12.values[kD3D12RenderTargetPathKey] == "rtv");
  CHECK(!d3d12.restart_pending);
  CHECK(!ApplyInternalRenderPath(d3d12));
  d3d12.values[kD3D12RenderTargetPathKey] = "rov";
  CHECK(!ApplyInternalRenderPath(d3d12));
  CHECK(d3d12.values[kD3D12RenderTargetPathKey] == "rov");

  // Older player files have no key: 100%. A change takes a new process, like Render resolution.
  const fs::path dir = FreshDir("internal_render");
  const fs::path file = LauncherSettingsPath(dir);
  WriteText(file, "resolution_scale = 2\n");
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.Register(kD3D12RenderTargetPathKey, "");
  cvars.restart_only.insert(kInternalRenderScaleKey);
  cvars.registered.erase(kInternalRenderScaleKey);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  cvars.registered.insert(kInternalRenderScaleKey);
  settings.CaptureLateRegistered();
  CHECK(settings.Get(kInternalRenderScaleKey) == "100");
  CHECK(!settings.Apply());

  settings.Set(kInternalRenderScaleKey, "50");
  CHECK(!settings.WindowChanged());
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_internal_render_scale = 50") != std::string::npos);
  CHECK(ReadText(file).find("resolution_scale = 2") != std::string::npos);
  CHECK(settings.Apply());
  CHECK(cvars.values[kInternalRenderScaleKey] == "50");
  CHECK(cvars.values[kD3D12RenderTargetPathKey] == "rtv");

  settings.ResetToDefaults();
  CHECK(settings.Get(kInternalRenderScaleKey) == "100");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_internal_render_scale = 100") != std::string::npos);

  const std::string launcher = ReadText(kSrcDir / "launcher.cpp");
  const std::size_t card = launcher.find("ui.Combo(\"Graphics card\"");
  const std::size_t render = launcher.find("ui.Combo(\"Render resolution\"");
  const std::size_t internal = launcher.find("ui.Combo(\"Internal render resolution\"");
  const std::size_t aa = launcher.find("ui.Choice(\"Anti-aliasing\"");
  const std::size_t msaa = launcher.find("ui.Toggle(\"Multisampling\"");
  const std::size_t filtering = launcher.find("ui.Choice(\"Texture filtering\"");
  const std::size_t bloom = launcher.find("ui.Choice(\"Bloom\"");
  CHECK(card < render && render < internal && internal < aa && aa < msaa && msaa < filtering && filtering < bloom &&
        bloom != std::string::npos);
  const std::string app = ReadText(kSrcDir / "geometry_wars_retro_evolved_2_app.h");
  const std::size_t applied = app.find("gw2::ApplyInternalRenderPath(cvars_)");
  CHECK(applied > app.find("std::optional<rex::PathConfig> OnFinalizePaths("));
  CHECK(applied < app.find("if (!show || !imgui_drawer())"));
  const std::string patch = ReadText(kSrcDir / ".." / "patches" / "rexglue-sdk" / "0036-internal-render-scale.patch");
  const std::size_t cvar = patch.find("+REXCVAR_DEFINE_INT32(gw2_internal_render_scale, 100,");
  CHECK(cvar != std::string::npos);
  CHECK(patch.find("rex::cvar::Lifecycle::kRequiresRestart", cvar) < patch.find("+REXCVAR", cvar + 1));
  CHECK(patch.find("hcu_") == std::string::npos);
}

void TestAspectRatioSetting() {
  Section("aspect ratio");
  CHECK(std::string_view(kAspectKey) == "gw2_aspect_ratio");
  CHECK(std::string_view(kRetiredAspectKey) == "present_letterbox");
  CHECK(ManagedCount(kAspectKey, ValueKind::kString) == 1);
  CHECK(ManagedCount(kRetiredAspectKey, ValueKind::kBool) == 0);
  CHECK(IsWindowSetting(kAspectKey));

  const std::vector<SettingChoice> choices = AspectRatioChoices();
  CHECK(choices.size() == 6);
  CHECK(choices[0].value == "16:9" && choices[0].label == "16:9 Original");
  CHECK(choices[1].value == "16:10" && choices[1].label == "16:10");
  CHECK(choices[2].value == "21:9" && choices[2].label == "21:9 Ultrawide");
  CHECK(choices[3].value == "32:9" && choices[3].label == "32:9 Super Ultrawide");
  CHECK(choices[4].value == "4:3" && choices[5].value == "5:4");

  // Render resolution shows the picture the game renders for the format.
  CHECK(RenderScaleLabel(1) == RenderScaleLabel(1, AspectRatio::k16x9));
  CHECK(RenderScaleLabel(1, AspectRatio::k21x9) == "Original \xE2\x80\x94 1680 \xC3\x97 720");
  CHECK(RenderScaleLabel(2, AspectRatio::k21x9) == "2x \xE2\x80\x94 3360 \xC3\x97 1440");
  CHECK(RenderScaleLabel(4, AspectRatio::k5x4) == "4x \xE2\x80\x94 3584 \xC3\x97 2880");
  CHECK(RenderScaleChoices(AspectRatio::k4x3)[0].label == "Original \xE2\x80\x94 960 \xC3\x97 720");

  // The format is applied while the process starts, so a different one restarts the game; Apply never assigns it.
  const fs::path dir = FreshDir("aspect");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kAspectKey) == "16:9");
  CHECK(!settings.WindowChanged());
  settings.Set(kAspectKey, "21:9");
  CHECK(settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kAspectKey] == "16:9");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_aspect_ratio = \"21:9\"") != std::string::npos);
  settings.Set(kAspectKey, "16:9");
  CHECK(!settings.WindowChanged());
  settings.Set(kAspectKey, "32:9");
  settings.ResetToDefaults();
  CHECK(settings.Get(kAspectKey) == "16:9");

  // A player file from the Letterbox/Stretch launcher loses that line, whichever its value, and keeps the rest.
  for (const char* legacy : {"true", "false"}) {
    const fs::path old_dir = FreshDir("aspect_legacy");
    const fs::path old_file = LauncherSettingsPath(old_dir);
    WriteText(old_file, std::string("resolution_scale = 2\npresent_letterbox = ") + legacy + "\ngw2_bloom = \"off\"\n");
    CHECK(RemoveRetiredSetting(old_file, kRetiredAspectKey) == RetiredSettingResult::kRemoved);
    const Values values = ReadSettingsValues(old_file);
    CHECK(values.find("present_letterbox") == values.end());
    CHECK(values.at("resolution_scale") == "2" && values.at("gw2_bloom") == "off");
    CHECK(values.find(kAspectKey) == values.end());
  }

  const std::string launcher = ReadText(kSrcDir / "launcher.cpp");
  const std::size_t window_size = launcher.find("ui.Row(\"Window size\"");
  const std::size_t monitor = launcher.find("ui.Row(\"Monitor\"");
  const std::size_t aspect = launcher.find("ui.Combo(\"Aspect Ratio\"");
  const std::size_t vsync = launcher.find("ui.Choice(\"VSync\"");
  CHECK(window_size < monitor && monitor < aspect && aspect < vsync && window_size != std::string::npos &&
        vsync != std::string::npos);
  CHECK(launcher.find("Sets the game's target rendering format and the Window size choices.") != std::string::npos);
  CHECK(launcher.find("Letterbox") == std::string::npos && launcher.find("Stretch") == std::string::npos);
  CHECK(launcher.find("RenderScaleChoices(SelectedAspectRatio())") != std::string::npos);
  CHECK(launcher.find("size.aspect != aspect") != std::string::npos);

  const std::string app = ReadText(kSrcDir / "geometry_wars_retro_evolved_2_app.h");
  const std::size_t load = app.find("gw2::LoadPlayerSettings(");
  const std::size_t adapter = app.find("gw2::ApplyGraphicsAdapter();");
  const std::size_t apply = app.find("gw2::ApplyAspectRatio();");
  const std::size_t settle = app.find("gw2::SettleStartupCvars();");
  CHECK(load < adapter && adapter < apply && apply < settle && load != std::string::npos);
  CHECK(app.find("gw2::ApplyAspectRatio();", apply + 1) == std::string::npos);

  const std::string cvar_source = ReadText(kSrcDir / "launcher_cvars.cpp");
  CHECK(cvar_source.find("RemoveRetiredSetting(player_file, kRetiredAspectKey)") <
        cvar_source.find("rex::cvar::LoadConfig(player_file)"));
  CHECK(cvar_source.find("SetFlagByName(\"present_letterbox\", \"true\")") != std::string::npos);
  CHECK(cvar_source.find("REXCVAR_DEFINE_STRING(gw2_aspect_ratio, \"16:9\"") != std::string::npos);
  const std::string config = ReadText(kSrcDir / ".." / "config" / "geometry_wars_retro_evolved_2.toml");
  CHECK(!config.empty() && config.find("video_mode_") == std::string::npos && config.find("present_letterbox") == std::string::npos);
}

void TestGraphicsAdapterSetting() {
  Section("graphics card");
  CHECK(std::string_view(kGraphicsAdapterKey) == "gw2_graphics_adapter");
  CHECK(ManagedCount(kGraphicsAdapterKey, ValueKind::kString) == 1);
  CHECK(IsWindowSetting(kGraphicsAdapterKey));

  const std::vector<GraphicsAdapter> adapters = {{0, "NVIDIA GeForce RTX 4070"}, {1, "Intel UHD Graphics 730"}};
  std::vector<SettingChoice> choices = GraphicsAdapterChoices(adapters, kGraphicsAdapterAuto);
  CHECK(choices.size() == 3);
  CHECK(choices[0].value == "auto" && choices[0].label == "Automatic (Recommended)");
  CHECK(choices[1].value == "0" && choices[1].label == "NVIDIA GeForce RTX 4070");
  CHECK(choices[2].value == "1" && choices[2].label == "Intel UHD Graphics 730");
  choices = GraphicsAdapterChoices({{0, "Same"}, {2, "Same"}}, "0");
  CHECK(choices.size() == 3 && choices[1].label == "Same (0)" && choices[2].label == "Same (2)");
  choices = GraphicsAdapterChoices(adapters, "5");
  CHECK(choices.size() == 4 && choices[3].value == "5" && choices[3].label == "Graphics card 5 (not found)");
  CHECK(GraphicsAdapterChoices({}, "garbage").size() == 1);

  // The device exists before the launcher shows, so a different card restarts the game; Apply never assigns it.
  const fs::path dir = FreshDir("graphics_card");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.Get(kGraphicsAdapterKey) == "auto");
  CHECK(!settings.WindowChanged());
  settings.Set(kGraphicsAdapterKey, "1");
  CHECK(settings.WindowChanged());
  CHECK(!settings.Apply());
  CHECK(cvars.values[kGraphicsAdapterKey] == "auto");
  CHECK(settings.Save());
  CHECK(ReadText(file).find("gw2_graphics_adapter = \"1\"") != std::string::npos);
  CHECK(ReadSettingsValues(file).at(kGraphicsAdapterKey) == "1");
  settings.ResetToDefaults();
  CHECK(settings.Get(kGraphicsAdapterKey) == "auto");

  const std::string app = ReadText(kSrcDir / "geometry_wars_retro_evolved_2_app.h");
  const std::size_t load = app.find("gw2::LoadPlayerSettings(");
  const std::size_t apply = app.find("gw2::ApplyGraphicsAdapter();");
  CHECK(load != std::string::npos && apply != std::string::npos && load < apply);
  CHECK(apply < app.find("void OnPreSetup("));
}

void TestLauncherPlacementSettings() {
  Section("launcher placement");
  for (const char* key : {kLauncherXKey, kLauncherYKey, kLauncherWidthKey, kLauncherHeightKey, kLauncherMaximizedKey,
                          kLauncherFullscreenKey}) {
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
      managed = managed || setting.key == key;
    }
    CHECK(managed);
    CHECK(IsLauncherPlacementSetting(key));
    CHECK(!IsWindowSetting(key));
  }
  CHECK(!IsLauncherPlacementSetting(kWindowWidthKey));
  CHECK(!IsLauncherPlacementSetting(kWindowHeightKey));

  const fs::path dir = FreshDir("launcher_placement");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  CHECK(settings.GetInt(kLauncherWidthKey, -1) == 0);

  CHECK(settings.SaveNow({{kLauncherXKey, "-250"},
                          {kLauncherYKey, "120"},
                          {kLauncherWidthKey, "1280"},
                          {kLauncherHeightKey, "720"},
                          {kLauncherMaximizedKey, "true"}}));
  const std::string text = ReadText(file);
  CHECK(text.find("gw2_launcher_x = -250") != std::string::npos);
  CHECK(text.find("gw2_launcher_width = 1280") != std::string::npos);
  CHECK(text.find("gw2_launcher_maximized = true") != std::string::npos);
  CHECK(settings.GetInt(kLauncherXKey, 0) == -250);
  CHECK(settings.GetBool(kLauncherMaximizedKey, false));
  CHECK(!settings.WindowChanged());
  CHECK(settings.GetInt(kWindowWidthKey, 0) == 1280);

  settings.Set(kShowFpsKey, "false");
  CHECK(settings.SaveNow({{kLauncherXKey, "10"}, {kLauncherYKey, "20"}}));
  CHECK(settings.Get(kShowFpsKey) == "false");
  CHECK(ReadText(file).find("gw2_show_fps") == std::string::npos);

  settings.ResetToDefaults();
  CHECK(settings.GetInt(kLauncherXKey, 0) == 10);
  CHECK(settings.GetInt(kLauncherWidthKey, 0) == 1280);
  CHECK(settings.GetBool(kLauncherMaximizedKey, false));
  CHECK(settings.Get(kShowFpsKey) == "true");
}

void TestCheatSettings() {
  Section("cheat settings");
  int cheat_keys = 0;
  for (const ManagedSetting& setting : ManagedSettings()) {
    if (setting.key.starts_with("gw2_cheat_")) {
      ++cheat_keys;
      CHECK(setting.kind == ValueKind::kBool);
      CHECK(!IsWindowSetting(setting.key));
      CHECK(!IsLauncherPlacementSetting(setting.key));
    }
  }
  CHECK(cheat_keys == 3);
  for (const cheats::CheatInfo& cheat : cheats::kCheats) {
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
      managed = managed || setting.key == cheat.key;
    }
    CHECK(managed);
  }

  const fs::path dir = FreshDir("cheats");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();
  for (const cheats::CheatInfo& cheat : cheats::kCheats) {
    CHECK(settings.Get(cheat.key) == "false");
    CHECK(!settings.GetBool(cheat.key, true));
  }

  settings.Set(cheats::kInfiniteShieldKey, "true");
  CHECK(settings.GetBool(cheats::kInfiniteShieldKey, false));
  CHECK(!settings.GetBool(cheats::kInvincibleKey, true));
  CHECK(!settings.GetBool(cheats::kInfiniteBombsKey, true));
  CHECK(!settings.WindowChanged());

  CHECK(settings.Save());
  CHECK(cvars.assigns == 0);
  const std::string text = ReadText(file);
  CHECK(text.find("gw2_cheat_infinite_shield = true") != std::string::npos);
  CHECK(text.find("gw2_cheat_invincible") == std::string::npos);
  CHECK(text.find("gw2_cheat_infinite_bombs") == std::string::npos);

  CHECK(!settings.Apply());
  CHECK(cvars.values[cheats::kInfiniteShieldKey] == "true");
  CHECK(cvars.values[cheats::kInvincibleKey] == "false");
  CHECK(cvars.assigns == 1);

  const Values stored = ReadSettingsValues(file);
  CHECK(stored.at(cheats::kInfiniteShieldKey) == "true");
  CHECK(stored.find(cheats::kInvincibleKey) == stored.end());
  FakeCvars restarted;
  RegisterGameDefaults(restarted);
  for (const auto& [key, value] : stored) {
    if (restarted.IsRegistered(key)) {
      restarted.values[key] = value;
    }
  }
  GameSettings reloaded(restarted, file, dir / "missing.toml");
  reloaded.Capture();
  CHECK(reloaded.GetBool(cheats::kInfiniteShieldKey, false));
  CHECK(!reloaded.GetBool(cheats::kInvincibleKey, true));
  CHECK(!reloaded.GetBool(cheats::kInfiniteBombsKey, true));

  settings.Set(cheats::kInvincibleKey, "true");
  settings.Set(cheats::kInfiniteBombsKey, "true");
  CHECK(settings.Save());
  settings.ResetToDefaults();
  for (const cheats::CheatInfo& cheat : cheats::kCheats) {
    CHECK(settings.Get(cheat.key) == "false");
  }
  CHECK(settings.Save());
  const std::string reset = ReadText(file);
  CHECK(reset.find("gw2_cheat_invincible = false") != std::string::npos);
  CHECK(reset.find("gw2_cheat_infinite_shield = false") != std::string::npos);
  CHECK(reset.find("gw2_cheat_infinite_bombs = false") != std::string::npos);
  CHECK(reset.find("= true") == std::string::npos);
}

void TestUnregisteredCheatIsRejected() {
  Section("unregistered cheat key");
  const fs::path dir = FreshDir("cheats_unregistered");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.Set("gw2_cheat_god_mode", "true");
  CHECK(!settings.Save());
  CHECK(!settings.SaveNow("gw2_cheat_unknown", "true"));
  CHECK(!fs::exists(file));

  for (const ManagedSetting& setting : ManagedSettings()) {
    CHECK(setting.key != "gw2_cheat_god_mode");
    CHECK(setting.key != "gw2_cheat_unknown");
  }

  cvars.registered.erase(cheats::kInfiniteBombsKey);
  GameSettings missing(cvars, file, dir / "missing.toml");
  missing.Capture();
  missing.Set(cheats::kInfiniteBombsKey, "true");
  CHECK(!missing.Save());
  CHECK(!fs::exists(file));
}

void TestSettingsFollowTheUserDataRoot() {
  Section("settings and launcher placement are written below the user data root");
  const fs::path dir = FreshDir("user_data_root");
  for (const char* name : {"user_data", "chosen"}) {
    const fs::path root = dir / name;
    const fs::path file = LauncherSettingsPath(root);
    CHECK(file == root / "launcher" / "settings.toml");
    FakeCvars cvars;
    RegisterGameDefaults(cvars);
    GameSettings settings(cvars, file, dir / "missing.toml");
    settings.Capture();
    settings.MarkWindowCreated();
    settings.Set(kFullscreenKey, "true");
    CHECK(settings.Save());
    CHECK(settings.SaveNow({{kLauncherXKey, "40"}, {kLauncherYKey, "60"}}));
    CHECK(ReadText(file).find("gw2_launcher_x = 40") != std::string::npos);
  }
  int files = 0;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    files += entry.is_regular_file() ? 1 : 0;
  }
  CHECK(files == 2);
}

void TestDisplayModeStaysWithTheGame() {
  Section("display mode is the game's");
  const fs::path dir = FreshDir("display_mode");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  cvars.values[kFullscreenKey] = "false";
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();

  settings.Set(kFullscreenKey, "true");
  settings.Set(kAntiAliasingKey, "fxaa");
  CHECK(!settings.Apply());
  CHECK(cvars.values[kFullscreenKey] == "false");
  CHECK(cvars.values[kAntiAliasingKey] == "fxaa");
  CHECK(!settings.WindowChanged());
  CHECK(settings.ApplyDisplayMode());
  CHECK(cvars.values[kFullscreenKey] == "true");
  const int assigns = cvars.assigns;
  CHECK(settings.ApplyDisplayMode());
  CHECK(cvars.assigns == assigns);

  // The launcher window was opened windowed while the saved Display mode stays Borderless Fullscreen.
  const fs::path saved_dir = FreshDir("display_mode_saved");
  WriteText(LauncherSettingsPath(saved_dir), "fullscreen = true\ngw2_launcher_width = 1200\n");
  FakeCvars next;
  RegisterGameDefaults(next);
  next.values[kFullscreenKey] = "true";
  GameSettings reopened(next, LauncherSettingsPath(saved_dir), saved_dir / "missing.toml");
  reopened.Capture();
  next.values[kFullscreenKey] = "false";
  CHECK(reopened.Get(kFullscreenKey) == "true");
  CHECK(reopened.Save());
  CHECK(ReadText(LauncherSettingsPath(saved_dir)).find("fullscreen = true") != std::string::npos);
  CHECK(reopened.ApplyDisplayMode());
  CHECK(next.values[kFullscreenKey] == "true");

  CHECK(!IsLauncherPlacementSetting(kFullscreenKey));
  CHECK(!IsLauncherPlacementSetting(kMonitorKey));
  CHECK(!IsWindowSetting(kLauncherXKey));
  CHECK(!IsWindowSetting(kLauncherWidthKey));
}

void TestLauncherFullscreenIsSeparate() {
  Section("launcher fullscreen is separate");
  const fs::path dir = FreshDir("launcher_fullscreen");
  const fs::path file = LauncherSettingsPath(dir);
  FakeCvars cvars;
  RegisterGameDefaults(cvars);
  GameSettings settings(cvars, file, dir / "missing.toml");
  settings.Capture();
  settings.MarkWindowCreated();

  CHECK(IsLauncherPlacementSetting(kLauncherFullscreenKey));
  CHECK(!IsWindowSetting(kLauncherFullscreenKey));
  CHECK(!IsLauncherPlacementSetting(kFullscreenKey));

  settings.Set(kFullscreenKey, "true");
  CHECK(settings.Save());
  CHECK(settings.Get(kLauncherFullscreenKey, "false") == "false");
  CHECK(ReadText(file).find("gw2_launcher_fullscreen = true") == std::string::npos);

  CHECK(settings.SaveNow({{kLauncherFullscreenKey, "true"}}));
  CHECK(ReadText(file).find("gw2_launcher_fullscreen = true") != std::string::npos);
  CHECK(settings.Get(kFullscreenKey) == "true");
  CHECK(!settings.WindowChanged());

  settings.Set(kFullscreenKey, "false");
  CHECK(settings.Save());
  CHECK(settings.Get(kLauncherFullscreenKey) == "true");

  settings.ResetToDefaults();
  CHECK(settings.Get(kLauncherFullscreenKey) == "true");
  CHECK(settings.Get(kFullscreenKey) == "false");

  settings.Set(kMonitorKey, "2");
  settings.Set(kWindowWidthKey, "853");
  CHECK(settings.Get(kLauncherFullscreenKey) == "true");
  CHECK(settings.Get(kLauncherWidthKey, "0") == "0");
}

}  // namespace

int main() {
  std::error_code ec;
  fs::remove_all(kRoot, ec);
  TestShippedValues();
  TestSaveWritesChangedValuesOnly();
  TestSaveRefusesUnregisteredSetting();
  TestLateRegisteredSettings();
  TestApplyAndRestartNeeds();
  TestWindowChangeDetection();
  TestWindowChangeIgnoresFitAndCapture();
  TestResetToDefaults();
  TestSaveNowKeepsOtherPendingEdits();
  TestPlayGate();
  TestLauncherPolicy();
  TestWindowSizeOptions();
  TestGraphicsOptions();
  TestPagesAndKeyList();
  TestSettingsFileValues();
  TestFrameRateSetting();
  TestRenderResolutionSetting();
  TestFrameCounterSetting();
  TestFrameLimitSetting();
  TestBloomSetting();
  TestMultisamplingSetting();
  TestInternalRenderScaleSetting();
  TestRenderPathSetting();
  TestAspectRatioSetting();
  TestGraphicsAdapterSetting();
  TestLauncherPlacementSettings();
  TestCheatSettings();
  TestUnregisteredCheatIsRejected();
  TestSettingsFollowTheUserDataRoot();
  TestDisplayModeStaysWithTheGame();
  TestLauncherFullscreenIsSeparate();
  fs::remove_all(kRoot, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
