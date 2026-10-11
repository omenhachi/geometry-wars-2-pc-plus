// Launcher window placement tests: what is saved, what is restored, when it is saved, and off-screen recovery.
// Pure logic with synthetic window states; the Win32 calls need a real window.

#include <cstdio>
#include <map>
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
  CHECK(settings.values.size() == 5);
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
  CHECK(!normal.Flush().has_value());
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

}  // namespace

int main() {
  TestPreShowPlan();
  TestRestoreAfterMaximizedStart();
  TestValues();
  TestVisibility();
  TestMoveAndResize();
  TestMaximize();
  TestMinimizeAndFullscreen();
  TestFirstObservation();
  TestFlushOnClose();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
