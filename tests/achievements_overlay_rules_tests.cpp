#include "achievements_overlay_rules.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void Check(bool passed, const char* name) {
  if (!passed) {
    ++failures;
    std::printf("FAIL: %s\n", name);
  }
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

constexpr uint16_t kNone = 0;

}  // namespace

int main() {
  using gw2::AchievementsOverlayGate;
  using gw2::kXinputButtonB;
  using gw2::kXinputDpadDown;
  using gw2::kXinputDpadUp;

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    auto v = gate.Poll(0, kNone, 0);
    Check(v.hide_input && !v.close, "open overlay hides input");
    v = gate.Poll(0, kXinputButtonB, 0);
    Check(v.close && v.hide_input, "B press closes the overlay");
    v = gate.Poll(0, kXinputButtonB, 0);
    Check(!v.close, "held B closes only once");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(0, kNone, 0);
    gate.Poll(0, kXinputButtonB, 0);
    gate.SetOpen(false);
    auto v = gate.Poll(0, kXinputButtonB, 0);
    Check(!v.close && v.hide_input, "B still held after close is not passed on");
    v = gate.Poll(0, kNone, 0);
    Check(!v.hide_input, "input returns once B is released");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(0, kNone, 0);
    gate.SetOpen(false);
    auto v = gate.Poll(0, kNone, 0);
    Check(!v.hide_input && !v.close, "closing without B returns input at once");
  }

  {
    AchievementsOverlayGate gate;
    gate.Poll(0, kXinputButtonB, 0);
    gate.SetOpen(true);
    auto v = gate.Poll(0, kXinputButtonB, 0);
    Check(!v.close && v.hide_input, "B held when the overlay opens does not close it");
    v = gate.Poll(0, kNone, 0);
    v = gate.Poll(0, kXinputButtonB, 0);
    Check(v.close, "a new B press after the overlay opened closes it");
  }

  {
    AchievementsOverlayGate gate;
    auto v = gate.Poll(0, kXinputButtonB, 0);
    Check(!v.close && !v.hide_input, "B while the overlay is closed does nothing");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(1, kNone, 0);
    auto v = gate.Poll(1, kXinputButtonB, 0);
    Check(v.close, "B on another controller closes the overlay");
    Check(gate.Poll(3, kNone, 20000).hide_input, "every controller is neutral while the overlay is open");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    Check(gate.Poll(0, kXinputDpadDown, 0).scroll == 1.0f, "D-pad down scrolls down");
    Check(gate.Poll(0, kXinputDpadUp, 0).scroll == -1.0f, "D-pad up scrolls up");
    Check(Near(gate.Poll(0, kNone, 20000).scroll, -12000.0f / 24767.0f), "left stick up scrolls up");
    Check(Near(gate.Poll(0, kNone, -20000).scroll, 12000.0f / 24767.0f), "left stick down scrolls down");
    Check(gate.Poll(0, kNone, 5000).scroll == 0.0f, "stick inside the deadzone does not scroll");
    Check(gate.Poll(0, kNone, -32768).scroll == 1.0f, "full stick deflection is clamped to 1");
    gate.SetOpen(false);
    Check(gate.Poll(0, kXinputDpadDown, 0).scroll == 0.0f, "closed overlay does not scroll");
  }

  Check(Near(gw2::FitWindowExtent(1280, 420, 560), 560), "1280 wide gives 560");
  Check(Near(gw2::FitWindowExtent(1920, 420, 560), 560), "1920 wide gives 560");
  Check(Near(gw2::FitWindowExtent(720, 320, 460), 460), "720 high gives 460");
  Check(Near(gw2::FitWindowExtent(1080, 320, 460), 460), "1080 high gives 460");
  Check(Near(gw2::FitWindowExtent(600, 420, 560), 420), "600 wide gives the 420 minimum");
  Check(Near(gw2::FitWindowExtent(360, 320, 460), 320), "360 high gives the 320 minimum");
  Check(Near(gw2::FitWindowExtent(300, 320, 460), 268), "300 high stays inside the viewport");

  if (failures) {
    std::printf("ACHIEVEMENTS OVERLAY RULES: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("ACHIEVEMENTS OVERLAY RULES OK: 0 failure(s)\n");
  return 0;
}
