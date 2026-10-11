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

#include "frame_limiter.h"
#include "launcher_options.h"
#include "launcher_play.h"
#include "launcher_platform.h"
#include "launcher_settings.h"

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
  cvars.Register(kAspectKey, "true");
  cvars.Register(kRenderScaleKey, "1");
  cvars.Register(kAntiAliasingKey, "none");
  cvars.Register(kMultisamplingKey, "true");
  cvars.Register(kTextureFilteringKey, "3");
  cvars.Register(kKeyboardKey, "true");
  cvars.Register(kFrameRateKey, std::to_string(kFrameRateDefault));
  cvars.Register(kLauncherXKey, "0");
  cvars.Register(kLauncherYKey, "0");
  cvars.Register(kLauncherWidthKey, "0");
  cvars.Register(kLauncherHeightKey, "0");
  cvars.Register(kLauncherMaximizedKey, "false");
  cvars.Register(kShowFpsKey, kShowFpsDefault ? "true" : "false");
  cvars.Register(kShowFrameCounterKey, kShowFrameCounterDefault ? "true" : "false");
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

  settings.Set(kMultisamplingKey, "false");
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
  CHECK(std::size(kPageOrder) == 7);
  CHECK(PageName(kPageOrder[0]) == "Play");
  CHECK(PageName(kPageOrder[1]) == "Display");
  CHECK(PageName(kPageOrder[2]) == "Graphics");
  CHECK(PageName(kPageOrder[3]) == "Gameplay");
  CHECK(PageName(kPageOrder[4]) == "Controls");
  CHECK(PageName(kPageOrder[5]) == "Achievements");
  CHECK(PageName(kPageOrder[6]) == "About");
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

void TestLauncherPlacementSettings() {
  Section("launcher placement");
  for (const char* key : {kLauncherXKey, kLauncherYKey, kLauncherWidthKey, kLauncherHeightKey, kLauncherMaximizedKey}) {
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
  TestFrameCounterSetting();
  TestFrameLimitSetting();
  TestLauncherPlacementSettings();
  fs::remove_all(kRoot, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
