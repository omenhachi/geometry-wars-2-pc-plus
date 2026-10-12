// Launcher window placement tests: what is saved, what is restored, when it is saved, and off-screen recovery.
// Pure logic with synthetic window states; the Win32 calls need a real window.

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "launcher_placement.h"

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

struct FakeSettings {
  std::map<std::string, std::string> values;

  int GetInt(const char* key, int def) const {
    const auto it = values.find(key);
    return it == values.end() ? def : std::stoi(it->second);
  }
  bool GetBool(const char* key, bool def) const {
    const auto it = values.find(key);
    return it == values.end() ? def : it->second == "true";
  }
};

const Rect kScreen{0, 0, 1920, 1040};
const std::vector<Rect> kAreas{kScreen};
const Rect kNormal{200, 100, 1480, 820};

WindowState Normal(Rect rect) {
  return {rect, false, false};
}

void TestValues() {
  Section("stored values");
  FakeSettings settings;
  for (const auto& [key, value] : PlacementValues({kNormal, true})) {
    settings.values[key] = value;
  }
  CHECK(settings.values.size() == 6);
  CHECK(settings.values.at("gw2_launcher_fullscreen") == "false");
  CHECK(settings.values.at("gw2_launcher_x") == "200");
  CHECK(settings.values.at("gw2_launcher_y") == "100");
  CHECK(settings.values.at("gw2_launcher_width") == "1280");
  CHECK(settings.values.at("gw2_launcher_height") == "720");
  CHECK(settings.values.at("gw2_launcher_maximized") == "true");
  const WindowPlacement read = ReadPlacement(settings);
  CHECK(read.normal == kNormal);
  CHECK(read.maximized);
  CHECK(!PlacementIsSet(ReadPlacement(FakeSettings{})));
}

bool Visible(const Rect& rect, bool maximized) {
  return PlacementIsVisible(WindowPlacement{rect, maximized}, kAreas);
}

void TestVisibility() {
  Section("off-screen recovery");
  CHECK(Visible(kNormal, false));
  const std::vector<Rect> two{{-1920, 0, 0, 1040}, kScreen};
  const WindowPlacement straddling{{-600, 100, 680, 820}, false};
  CHECK(PlacementIsVisible(straddling, two));
  CHECK(PlacementIsVisible(straddling, kAreas));
  CHECK(!Visible({3000, 100, 4280, 820}, false));
  CHECK(!Visible({-1500, 100, -220, 820}, false));
  CHECK(!Visible({200, 1030, 1480, 1750}, false));
  CHECK(!Visible({0, 0, 0, 0}, true));
  CHECK(!Visible({200, 100, 300, 150}, false));
  CHECK(!PlacementIsVisible(WindowPlacement{kNormal, false}, std::span<const Rect>()));
}

void TestMoveAndResize() {
  Section("move and resize");
  PlacementTracker tracker;
  tracker.Observe(Normal(kNormal), false, 0.0);
  CHECK(!tracker.TakeSettled(10.0).has_value());

  const Rect moved{300, 150, 1580, 870};
  tracker.Observe(Normal(moved), false, 11.0);
  CHECK(!tracker.TakeSettled(11.2).has_value());
  const auto saved = tracker.TakeSettled(11.6);
  CHECK(saved.has_value());
  CHECK(saved && saved->normal == moved && !saved->maximized);
  CHECK(!tracker.TakeSettled(20.0).has_value());

  const Rect first{300, 150, 1000, 700};
  const Rect second{300, 150, 1100, 750};
  tracker.Observe(Normal(first), false, 21.0);
  tracker.Observe(Normal(second), false, 21.3);
  CHECK(!tracker.TakeSettled(21.6).has_value());
  const auto resized = tracker.TakeSettled(21.9);
  CHECK(resized && resized->normal == second);
}

void TestMaximize() {
  Section("maximize and restore");
  PlacementTracker tracker(WindowPlacement{kNormal, false});
  tracker.Observe(Normal(kNormal), false, 0.0);
  tracker.Observe(WindowState{kScreen, true, false}, false, 1.0);
  const auto maximized = tracker.TakeSettled(2.0);
  CHECK(maximized.has_value());
  CHECK(maximized && maximized->maximized);
  CHECK(maximized && maximized->normal == kNormal);

  tracker.Observe(Normal(kNormal), false, 3.0);
  const auto restored = tracker.TakeSettled(4.0);
  CHECK(restored && !restored->maximized && restored->normal == kNormal);
}

void TestMinimizeAndFullscreen() {
  Section("minimized and fullscreen");
  PlacementTracker tracker(WindowPlacement{kNormal, true});
  const WindowState minimized_maximized{{-32000, -32000, -31840, -31972}, true, true};
  tracker.Observe(minimized_maximized, false, 1.0);
  CHECK(!tracker.TakeSettled(5.0).has_value());
  CHECK(!tracker.Flush().has_value());

  PlacementTracker normal(WindowPlacement{kNormal, false});
  normal.Observe(WindowState{{-32000, -32000, -31840, -31972}, false, true}, false, 1.0);
  CHECK(!normal.Flush().has_value());

  normal.Observe(Normal(kScreen), true, 2.0);
  const auto fullscreen = normal.Flush();
  CHECK(fullscreen && fullscreen->fullscreen && fullscreen->normal == kNormal);
}

void TestFirstObservation() {
  Section("first observation");
  PlacementTracker tracker;
  tracker.Observe(Normal(kNormal), false, 0.0);
  CHECK(!tracker.Flush().has_value());

  PlacementTracker maximized_first;
  maximized_first.Observe(WindowState{kScreen, true, false}, false, 0.0);
  CHECK(!maximized_first.Flush().has_value());
  maximized_first.Observe(Normal(kNormal), false, 1.0);
  const auto restored = maximized_first.Flush();
  CHECK(restored && restored->normal == kNormal && !restored->maximized);
}

void TestFlushOnClose() {
  Section("flush on close");
  PlacementTracker tracker(WindowPlacement{kNormal, false});
  const Rect closed{10, 10, 1290, 730};
  tracker.Observe(Normal(closed), false, 5.0);
  const auto flushed = tracker.Flush();
  CHECK(flushed && flushed->normal == closed);
  CHECK(!tracker.Flush().has_value());
}

void TestPreShowPlan() {
  Section("pre-show plan");
  const PreShowPlan normal = PlanPreShow(WindowPlacement{kNormal, false}, kAreas);
  CHECK(normal.Needed() && normal.normal == kNormal && !normal.maximized);

  const PreShowPlan maximized = PlanPreShow(WindowPlacement{kNormal, true}, kAreas);
  CHECK(maximized.normal == kNormal && maximized.maximized);

  const Rect gone{3000, 100, 4280, 820};
  const PreShowPlan recovered = PlanPreShow(WindowPlacement{gone, true}, kAreas);
  CHECK(!recovered.normal.has_value() && recovered.maximized && recovered.Needed());

  CHECK(!PlanPreShow(WindowPlacement{gone, false}, kAreas).Needed());
  CHECK(!PlanPreShow(WindowPlacement{}, kAreas).Needed());
}

void TestRestoreAfterMaximizedStart() {
  Section("restore after a maximized start");
  const WindowPlacement saved{kNormal, true};
  PlacementTracker tracker(saved);
  tracker.Observe(WindowState{kScreen, true, false}, false, 0.0);
  CHECK(!tracker.Flush().has_value());
  tracker.Observe(Normal(kNormal), false, 1.0);
  const auto restored = tracker.Flush();
  CHECK(restored && !restored->maximized && restored->normal == kNormal);
}

const WindowState kDesktop{kScreen, false, false};

WindowPlacement Reopen(const std::optional<WindowPlacement>& saved) {
  FakeSettings settings;
  for (const auto& [key, value] : PlacementValues(*saved)) {
    settings.values[key] = value;
  }
  return ReadPlacement(settings);
}

void TestLauncherFullscreenPersists() {
  Section("launcher fullscreen persists");
  PlacementTracker normal(WindowPlacement{kNormal, false});
  const Rect moved{300, 150, 1500, 950};
  normal.Observe(Normal(moved), false, 1.0);
  const WindowPlacement closed_normal = Reopen(normal.Flush());
  CHECK(closed_normal.normal == moved && !closed_normal.maximized && !closed_normal.fullscreen);

  PlacementTracker maximized(WindowPlacement{kNormal, false});
  maximized.Observe(WindowState{kScreen, true, false}, false, 1.0);
  const WindowPlacement closed_maximized = Reopen(maximized.Flush());
  CHECK(closed_maximized.maximized && !closed_maximized.fullscreen && closed_maximized.normal == kNormal);

  PlacementTracker from_normal(WindowPlacement{kNormal, false});
  from_normal.Observe(kDesktop, true, 1.0);
  const WindowPlacement closed_fullscreen = Reopen(from_normal.Flush());
  CHECK(closed_fullscreen.fullscreen && !closed_fullscreen.maximized && closed_fullscreen.normal == kNormal);

  PlacementTracker second_start(closed_fullscreen);
  second_start.Observe(kDesktop, true, 0.0);
  CHECK(!second_start.Flush().has_value());
  second_start.Observe(Normal(kNormal), false, 1.0);
  const auto back = second_start.Flush();
  CHECK(back && !back->fullscreen && !back->maximized && back->normal == kNormal);

  PlacementTracker from_maximized(WindowPlacement{kNormal, true});
  from_maximized.Observe(WindowState{kScreen, true, false}, false, 0.0);
  from_maximized.Observe(kDesktop, true, 1.0);
  const WindowPlacement closed_both = Reopen(from_maximized.Flush());
  CHECK(closed_both.fullscreen && closed_both.maximized && closed_both.normal == kNormal);

  PlacementTracker reopened(closed_both);
  reopened.Observe(kDesktop, true, 0.0);
  CHECK(!reopened.Flush().has_value());
  reopened.Observe(WindowState{kScreen, true, false}, false, 1.0);
  const auto maximized_again = reopened.Flush();
  CHECK(maximized_again && !maximized_again->fullscreen && maximized_again->maximized &&
        maximized_again->normal == kNormal);
  reopened.Observe(Normal(kNormal), false, 2.0);
  const auto restored = reopened.Flush();
  CHECK(restored && !restored->maximized && restored->normal == kNormal);

  const Rect game{0, 0, 3840, 2160};
  PlacementTracker game_sized(WindowPlacement{kNormal, false});
  game_sized.Observe(WindowState{game, false, false}, true, 1.0);
  const auto kept = game_sized.Flush();
  CHECK(kept && kept->normal == kNormal);
}

void TestUserExitVersusPlayTransition() {
  Section("user F11 exit versus leaving for Play");
  PlacementTracker tracker(WindowPlacement{kNormal, false, true});
  tracker.Observe(kDesktop, true, 0.0);
  CHECK(!tracker.Flush().has_value());
  CHECK(tracker.Current() && tracker.Current()->fullscreen);

  // The window leaves fullscreen for Play without the tracker being told: the saved choice stays.
  CHECK(!tracker.Flush().has_value());
  CHECK(Reopen(tracker.Current()).fullscreen);

  tracker.Observe(Normal(kNormal), false, 1.0);
  const auto user_exit = tracker.Flush();
  CHECK(user_exit && !user_exit->fullscreen);
  CHECK(!Reopen(user_exit).fullscreen);

  tracker.Observe(kDesktop, true, 2.0);
  CHECK(tracker.TakeSettled(2.2) == std::nullopt);
  const auto settled = tracker.TakeSettled(3.0);
  CHECK(settled && settled->fullscreen);
}

void TestLauncherStateIsNotGameState() {
  Section("launcher state is not the game's");
  const auto values = PlacementValues({kNormal, true, true});
  for (const char* key : {"fullscreen", "monitor", "window_width", "window_height"}) {
    CHECK(values.find(key) == values.end());
  }
  CHECK(values.size() == 6);

  FakeSettings game_only;
  game_only.values["fullscreen"] = "true";
  game_only.values["monitor"] = "2";
  game_only.values["window_width"] = "853";
  game_only.values["window_height"] = "480";
  const WindowPlacement read = ReadPlacement(game_only);
  CHECK(!read.fullscreen && !read.maximized && !PlacementIsSet(read));

  const WindowPlacement fullscreen_launcher{kNormal, false, true};
  FakeSettings launcher_only;
  for (const auto& [key, value] : PlacementValues(fullscreen_launcher)) {
    launcher_only.values[key] = value;
  }
  CHECK(launcher_only.values.find("fullscreen") == launcher_only.values.end());

  const Rect negative{-1700, 80, -420, 800};
  const std::vector<Rect> two{{-1920, 0, 0, 1040}, kScreen};
  CHECK(PlanPreShow(WindowPlacement{negative, false}, two).normal == negative);
  CHECK(!PlanPreShow(WindowPlacement{negative, true, true}, two).Needed());
}

}  // namespace

int main() {
  TestPreShowPlan();
  TestRestoreAfterMaximizedStart();
  TestValues();
  TestVisibility();
  TestMoveAndResize();
  TestMaximize();
  TestMinimizeAndFullscreen();
  TestLauncherFullscreenPersists();
  TestUserExitVersusPlayTransition();
  TestLauncherStateIsNotGameState();
  TestFirstObservation();
  TestFlushOnClose();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
