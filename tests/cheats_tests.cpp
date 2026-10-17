// Cheat tests: the shared cheat list, the session layer, the in-game menu model and the host hotkeys.
// Logic and source checks only: the menu, the cvars and the input listeners need the running runtime.

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cheat_menu_rules.h"
#include "cheat_rules.h"
#include "cheats.h"

namespace fs = std::filesystem;
using namespace gw2;
using namespace gw2::cheats;

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

#ifdef GW2_SRC_DIR
const fs::path kSrcDir = GW2_SRC_DIR;
#else
const fs::path kSrcDir = fs::path(__FILE__).parent_path().parent_path() / "src";
#endif

std::string ReadSource(const std::string& name) {
  std::ifstream in(kSrcDir / name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::vector<std::string> SourceNames() {
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(kSrcDir)) {
    const std::string extension = entry.path().extension().string();
    if (entry.is_regular_file() && (extension == ".cpp" || extension == ".h")) {
      names.push_back(entry.path().filename().string());
    }
  }
  return names;
}

int Count(const std::string& text, const std::string& needle) {
  int count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

int CountInSources(const std::string& needle) {
  int count = 0;
  for (const std::string& name : SourceNames()) {
    count += Count(ReadSource(name), needle);
  }
  return count;
}

void TestCheatList() {
  Section("the three cheats");
  CHECK(kCheatCount == 3);
  CHECK(std::size(kCheats) == 3);
  CHECK(!kCheatDefault);

  CHECK(std::string_view(kCheats[0].key) == "gw2_cheat_invincible");
  CHECK(std::string_view(kCheats[0].label) == "Invincible");
  CHECK(std::string_view(kCheats[0].description) == "Prevents the player from dying");
  CHECK(std::string_view(kCheats[1].key) == "gw2_cheat_infinite_shield");
  CHECK(std::string_view(kCheats[1].label) == "Infinite Shield");
  CHECK(std::string_view(kCheats[1].description) == "Keeps the player's protective shield active");
  CHECK(std::string_view(kCheats[2].key) == "gw2_cheat_infinite_bombs");
  CHECK(std::string_view(kCheats[2].label) == "Infinite Bombs");
  CHECK(std::string_view(kCheats[2].description) == "Bombs are not consumed when used");

  std::set<std::string> keys;
  std::set<std::string> labels;
  for (size_t i = 0; i < kCheatCount; ++i) {
    CHECK(static_cast<size_t>(kCheats[i].cheat) == i);
    keys.insert(kCheats[i].key);
    labels.insert(kCheats[i].label);
  }
  CHECK(keys.size() == kCheatCount);
  CHECK(labels.size() == kCheatCount);
}

void TestSessionLayer() {
  Section("session layer");
  const bool saved[kCheatCount] = {false, true, false};
  SessionCheats session;
  for (size_t i = 0; i < kCheatCount; ++i) {
    CHECK(session.Effective(static_cast<Cheat>(i), saved[i]) == saved[i]);
  }

  session.Toggle(Cheat::Invincible, saved[0]);
  CHECK(session.Effective(Cheat::Invincible, saved[0]));
  CHECK(session.Effective(Cheat::InfiniteShield, saved[1]) == saved[1]);
  CHECK(session.Effective(Cheat::InfiniteBombs, saved[2]) == saved[2]);

  session.Toggle(Cheat::InfiniteShield, saved[1]);
  CHECK(!session.Effective(Cheat::InfiniteShield, saved[1]));
  CHECK(session.Effective(Cheat::Invincible, saved[0]));
  CHECK(session.Effective(Cheat::InfiniteBombs, saved[2]) == saved[2]);

  session.Toggle(Cheat::Invincible, saved[0]);
  session.Toggle(Cheat::InfiniteShield, saved[1]);
  for (size_t i = 0; i < kCheatCount; ++i) {
    CHECK(session.Effective(static_cast<Cheat>(i), saved[i]) == saved[i]);
  }
  CHECK(session.Effective(Cheat::Invincible, true));
}

void TestEffectiveState() {
  Section("effective state follows the launcher value and the session override");
  for (const bool saved : {false, true}) {
    SessionCheats session;
    for (size_t i = 0; i < kCheatCount; ++i) {
      CHECK(session.Effective(static_cast<Cheat>(i), saved) == saved);
    }
    session.Set(Cheat::InfiniteBombs, saved, !saved);
    CHECK(session.Effective(Cheat::InfiniteBombs, saved) == !saved);
    CHECK(session.Effective(Cheat::Invincible, saved) == saved);
    CHECK(session.Effective(Cheat::InfiniteShield, saved) == saved);
    session.Set(Cheat::InfiniteBombs, saved, saved);
    CHECK(session.Effective(Cheat::InfiniteBombs, saved) == saved);
  }
  SessionCheats session;
  session.Set(Cheat::Invincible, false, true);
  CHECK(session.Effective(Cheat::Invincible, false));
  CHECK(session.Effective(Cheat::Invincible, true));
  session.Set(Cheat::Invincible, true, false);
  CHECK(!session.Effective(Cheat::Invincible, true));
  CHECK(!session.Effective(Cheat::Invincible, false));
  for (int round = 0; round < 5; ++round) {
    session.Toggle(Cheat::InfiniteShield, false);
    CHECK(session.Effective(Cheat::InfiniteShield, false) == (round % 2 == 0));
  }
}

void TestBombRules() {
  Section("bomb rules");
  CHECK(BombCountAddress(0) == 0x8219D09C);
  CHECK(BombCountAddress(1) == 0x8219D09C + 0x78);
  CHECK(BombCountAddress(kMaxPlayers - 1) == 0x8219D09C + 0x78 * (kMaxPlayers - 1));
  CHECK(BombCountAddress(kMaxPlayers) == 0);
  CHECK(BombCountAddress(0xFFFFFFFF) == 0);

  CHECK(BombWasConsumed(3, 2));
  CHECK(BombWasConsumed(1, 0));
  CHECK(!BombWasConsumed(0, 0));
  CHECK(!BombWasConsumed(0, 0xFFFFFFFF));
  CHECK(!BombWasConsumed(3, 3));
  CHECK(!BombWasConsumed(2, 3));
  CHECK(!BombWasConsumed(5, 2));
}

void TestShieldRules() {
  Section("shield rules");
  CHECK(kShieldSeconds == 10.0f);
  CHECK(HeldShield(0.0f) == kShieldSeconds);
  CHECK(HeldShield(-1.0f) == kShieldSeconds);
  CHECK(HeldShield(3.33f) == kShieldSeconds);
  CHECK(HeldShield(9.99f) == kShieldSeconds);
  CHECK(HeldShield(kShieldSeconds) == kShieldSeconds);
  CHECK(HeldShield(25.0f) == 25.0f);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(HeldShield(nan) == kShieldSeconds);
}

void TestHitRules() {
  Section("hit rules");
  CHECK(HitWouldKill(0.0f, 0.0f));
  CHECK(HitWouldKill(-0.5f, -0.5f));
  CHECK(!HitWouldKill(0.2f, 0.0f));
  CHECK(!HitWouldKill(0.0f, 3.33f));
  CHECK(!HitWouldKill(0.2f, 3.33f));
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(HitWouldKill(nan, nan));

  CHECK(!SuppressesHit(false, 0.0f, 0.0f));
  CHECK(SuppressesHit(true, 0.0f, 0.0f));
  CHECK(!SuppressesHit(true, 0.2f, 0.0f));
  CHECK(!SuppressesHit(true, 0.0f, 3.33f));
  CHECK(!SuppressesHit(false, 0.2f, 3.33f));
}

void TestCheatsAreIndependent() {
  Section("the three cheats are independent");
  for (const bool invincible : {false, true}) {
    for (const bool shield : {false, true}) {
      for (const bool bombs : {false, true}) {
        SessionCheats session;
        session.Set(Cheat::Invincible, false, invincible);
        session.Set(Cheat::InfiniteShield, false, shield);
        session.Set(Cheat::InfiniteBombs, false, bombs);
        CHECK(session.Effective(Cheat::Invincible, false) == invincible);
        CHECK(session.Effective(Cheat::InfiniteShield, false) == shield);
        CHECK(session.Effective(Cheat::InfiniteBombs, false) == bombs);
      }
    }
  }
  // Invincible leaves the hits the shield handles to the game, so both together keep the shield's behavior.
  CHECK(!SuppressesHit(true, 0.0f, kShieldSeconds));
  CHECK(SuppressesHit(true, 0.0f, 0.0f));
}

void TestMenuModel() {
  Section("in-game menu model");
  CHECK(std::string_view(kCheatMenuTitle) == "CHEATS");
  CHECK(std::string_view(kCheatMenuCloseHint) == "F10 Close");
  CHECK(std::string_view(kCheatMenuNote) == "Not saved to the launcher");
  CHECK(kCheatMenuRows.size() == kCheatCount);
  CHECK(kCheatMenuRows.size() == 3);
  for (size_t i = 0; i < kCheatCount; ++i) {
    CHECK(kCheatMenuRows[i].cheat == kCheats[i].cheat);
    CHECK(std::string_view(kCheatMenuRows[i].label) == kCheats[i].label);
  }

  const std::string menu = ReadSource("cheat_menu.cpp");
  CHECK(menu.find("RETURN") == std::string::npos);
  CHECK(ReadSource("cheat_menu_rules.h").find("RETURN") == std::string::npos);
  const size_t rows = menu.find("kCheatMenuRows.size()");
  const size_t separator = menu.find("ImGui::Separator();", rows);
  const size_t hint = menu.find("kCheatMenuCloseHint", separator);
  const size_t note = menu.find("kCheatMenuNote", hint);
  CHECK(rows != std::string::npos && separator != std::string::npos && hint != std::string::npos &&
        note != std::string::npos);
}

void TestCloseGate() {
  Section("close gate");
  CloseGate idle;
  CHECK(!idle.requested());
  CHECK(!idle.Step(false, 0.0));

  CloseGate gate;
  gate.Request();
  CHECK(gate.requested());
  CHECK(!gate.Step(true, 10.0));
  CHECK(!gate.Step(true, 10.5));
  CHECK(gate.Step(false, 10.6));

  CloseGate released;
  released.Request();
  CHECK(released.Step(false, 3.0));

  CloseGate stuck;
  stuck.Request();
  CHECK(!stuck.Step(true, 5.0));
  CHECK(!stuck.Step(true, 5.0 + kReleaseTimeoutSeconds * 0.9));
  CHECK(stuck.Step(true, 5.0 + kReleaseTimeoutSeconds));
}

void TestCheatsAreWiredToOneState() {
  Section("launcher, menu and cvars share one list");
  const std::string cheats_cpp = ReadSource("cheats.cpp");
  CHECK(!cheats_cpp.empty());
  for (const CheatInfo& cheat : kCheats) {
    CHECK(Count(cheats_cpp, std::string("REXCVAR_DEFINE_BOOL(") + cheat.key + ", gw2::cheats::kCheatDefault,") == 1);
    CHECK(Count(cheats_cpp, std::string("REXCVAR_GET(") + cheat.key + ")") == 1);
    const std::string label = std::string("\"") + cheat.label + "\"";
    CHECK(Count(ReadSource("cheats.h"), label) == 1);
    for (const char* file : {"launcher.cpp", "cheat_menu.cpp", "cheat_menu_rules.h", "launcher_settings.cpp"}) {
      CHECK(ReadSource(file).find(label) == std::string::npos);
    }
    CHECK(CountInSources(std::string("\"") + cheat.key + "\"") == 1);
  }
  CHECK(Count(cheats_cpp, "REXCVAR_DEFINE_BOOL(") == 3);

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Count(launcher, "for (const cheats::CheatInfo& cheat : cheats::kCheats)") == 1);
  CHECK(Count(launcher, "ui.Toggle(cheat.label, cheat.description, cheat.key, cheats::kCheatDefault, \"Off\", \"On\")") ==
        1);
  CHECK(launcher.find("PageId::kCheats") != std::string::npos);

  const std::string menu = ReadSource("cheat_menu.cpp");
  CHECK(menu.find("kCheatMenuRows") != std::string::npos);
  CHECK(menu.find("cheats::Enabled(row.cheat)") != std::string::npos);
  CHECK(menu.find("cheats::ToggleForSession(row.cheat)") != std::string::npos);

  const std::string settings = ReadSource("launcher_settings.cpp");
  for (const char* key : {"kInvincibleKey", "kInfiniteShieldKey", "kInfiniteBombsKey"}) {
    CHECK(Count(settings, std::string("{cheats::") + key + ", ValueKind::kBool}") == 1);
  }
}

void TestMenuHasNoPersistencePath() {
  Section("in-game menu is session only");
  for (const char* file : {"cheat_menu.cpp", "cheat_menu.h", "cheat_menu_rules.h", "cheats.cpp", "cheats.h"}) {
    const std::string text = ReadSource(file);
    CHECK(!text.empty());
    for (const char* persistence : {"launcher_settings.h", "GameSettings", "SaveNow", "SaveConfig", "SetFlagByName",
                                    "ResetToDefault", "REXCVAR_SET", "TextFileSettings"}) {
      CHECK(text.find(persistence) == std::string::npos);
    }
  }
}

void TestHookWiring() {
  Section("gameplay hooks");
  const std::string hooks = ReadSource("cheat_hooks.cpp");
  CHECK(!hooks.empty());
  for (const char* function : {"sub_8204F288", "sub_8204FF00", "sub_8204EEA8"}) {
    const std::string name = function;
    CHECK(Count(hooks, "REX_HOOK_RAW(" + name + ")") == 1);
    CHECK(Count(hooks, "REX_EXTERN(__imp__" + name + ")") == 1);
    CHECK(Count(hooks, "__imp__" + name + "(ctx, base)") == 1);
    CHECK(CountInSources("REX_HOOK_RAW(" + name + ")") == 1);
  }

  std::set<std::string> hooked;
  const std::string marker = "REX_HOOK_RAW(";
  for (const std::string& file : SourceNames()) {
    const std::string text = ReadSource(file);
    for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + marker.size())) {
      const size_t end = text.find(')', at);
      const std::string name = text.substr(at + marker.size(), end - at - marker.size());
      CHECK(hooked.insert(name).second);
    }
  }
  CHECK(hooked.count("sub_8204F288") == 1);
  CHECK(hooks.find("sub_8204E988") == std::string::npos);

  CHECK(Count(hooks, "Enabled(Cheat::Invincible)") >= 1);
  CHECK(Count(hooks, "Enabled(Cheat::InfiniteShield)") == 1);
  CHECK(Count(hooks, "Enabled(Cheat::InfiniteBombs)") == 1);
  CHECK(hooks.find("REXCVAR_GET") == std::string::npos);
  for (const char* untouched : {"achievement", "Achievement", "live_stats", "local_stats", "leaderboard", "score"}) {
    CHECK(hooks.find(untouched) == std::string::npos);
  }
  CHECK(CountInSources("REXCVAR_GET(gw2_cheat_") == 3);
}

void TestAchievementsOverlayWiring() {
  Section("achievements overlay close paths");
  const std::string overlay = ReadSource("achievements_overlay.cpp");
  CHECK(!overlay.empty());
  CHECK(Count(overlay, "\"B  Back\"") == 1);
  CHECK(Count(overlay, "ImGuiKey_Escape") == 1);
  CHECK(Count(overlay, "controls_.request_close()") == 2);
  CHECK(Count(overlay, "DrawCloseButton(style, header_row_y)") == 1);
  CHECK(Count(overlay, "ImGui::Button(\"X##close_achievements\"") == 1);
  CHECK(overlay.find("kF10") == std::string::npos && overlay.find("GamepadFaceRight") == std::string::npos);

  const std::string live = ReadSource("live_fix.cpp");
  CHECK(Count(live, "g_gate.Poll(user, buttons, thumb_y)") == 1);
  CHECK(Count(live, "g_bridge.close_achievements()") == 1);
  CHECK(Count(live, "buttons & ~kXinputButtonB") == 1);
  CHECK(Count(ReadSource("cheat_menu.cpp"), "live::AchievementsHoldInput()") == 1);
  CHECK(Count(ReadSource("cheat_menu.cpp"), "SetActiveCallback") == 0);
  CHECK(Count(ReadSource("geometry_wars_retro_evolved_2_app.h"), "SetActiveCallback(") == 1);
  CHECK(Count(ReadSource("geometry_wars_retro_evolved_2_app.h"), "SetAchievementsModal(false)") == 1);
  CHECK(Count(ReadSource("geometry_wars_retro_evolved_2_app.h"), "SetAchievementsModal(true)") == 1);
}

void TestHotkeys() {
  Section("host hotkeys");
  CHECK(CountInSources("kF10") == 1);
  CHECK(ReadSource("cheat_menu.cpp").find("rex::ui::VirtualKey::kF10") != std::string::npos);
  CHECK(CountInSources("\"F10\"") == 0);

  CHECK(CountInSources("\"F2\"") == 1);
  CHECK(CountInSources("kF2") == 0);
  CHECK(ReadSource("frame_counter.cpp").find("RegisterBind(\"bind_gw2_frame_counter\", \"F2\"") != std::string::npos);

  CHECK(CountInSources("kF11") == 1);
  CHECK(ReadSource("window_host.cpp").find("e.virtual_key() != rex::ui::VirtualKey::kF11") != std::string::npos);

  CHECK(CountInSources("kF7") == 0);
  CHECK(CountInSources("\"F7\"") == 0);
  CHECK(CountInSources("bind_achievements") == 0);
  CHECK(ReadSource("geometry_wars_retro_evolved_2_app.h").find("CreateAchievementsOverlay() override") !=
        std::string::npos);
}

}  // namespace

int main() {
  TestCheatList();
  TestSessionLayer();
  TestEffectiveState();
  TestBombRules();
  TestShieldRules();
  TestHitRules();
  TestCheatsAreIndependent();
  TestMenuModel();
  TestCloseGate();
  TestCheatsAreWiredToOneState();
  TestMenuHasNoPersistencePath();
  TestHookWiring();
  TestAchievementsOverlayWiring();
  TestHotkeys();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
