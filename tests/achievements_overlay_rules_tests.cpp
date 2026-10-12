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
    Check(!v.close && v.clear_b && !v.hide_input, "B still held after close is removed from the state, nothing else");
    v = gate.Poll(0, kNone, 0);
    Check(!v.hide_input && !v.clear_b, "input returns once B is released");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(0, kNone, 0);
    gate.SetOpen(false);
    auto v = gate.Poll(0, kNone, 0);
    Check(!v.hide_input && !v.clear_b && !v.close, "closing without B returns input at once");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    Check(gate.HoldsInput(), "the open overlay holds the input");
    gate.Poll(0, kNone, 0);
    auto v = gate.Poll(0, kXinputButtonB, 0);
    Check(v.close && v.hide_input && !v.clear_b, "new B with the overlay open: close requested and B consumed");
    gate.SetOpen(false);
    Check(gate.HoldsInput(), "the gate holds the input while the closing B is held");
    v = gate.Poll(0, kXinputButtonB | kXinputDpadDown, 0);
    Check(v.clear_b && !v.hide_input && !v.close, "B held after the overlay closed is still consumed");
    Check(v.scroll == 0.0f, "no scrolling once closed");
    v = gate.Poll(0, kXinputButtonB, 0);
    Check(v.clear_b, "B stays consumed on every read while held");
    v = gate.Poll(0, kNone, 0);
    Check(!v.clear_b && !v.hide_input && !gate.HoldsInput(), "releasing B ends the suppression");
    v = gate.Poll(0, kXinputButtonB, 0);
    Check(!v.clear_b && !v.hide_input && !v.close, "the next B press reaches the game");
    Check(!gate.HoldsInput(), "a game B press does not hold the input");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(0, kNone, 0);
    gate.Poll(0, kXinputButtonB, 0);
    gate.SetOpen(false);
    auto v = gate.Poll(0, kXinputDpadUp | kXinputButtonB, 0);
    Check(v.clear_b && !v.hide_input, "only B is flagged for removal, other buttons stay");
  }

  {
    AchievementsOverlayGate gate;
    gate.SetOpen(true);
    gate.Poll(0, kNone, 0);
    gate.SetOpen(false);
    Check(!gate.HoldsInput(), "closing without B, as with Escape or the X, holds nothing");
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

  Check(gw2::GameInputAllowed(false, false, false), "game input flows with nothing open");
  Check(!gw2::GameInputAllowed(false, false, true), "a pointer over an overlay keeps clicks from the game");
  Check(gw2::GameInputAllowed(false, true, true), "B reaches the achievements gate with the pointer over the overlay");
  Check(gw2::GameInputAllowed(false, true, false), "B reaches the achievements gate with the pointer elsewhere");
  Check(!gw2::GameInputAllowed(true, false, false), "the cheat menu blocks the game");
  Check(!gw2::GameInputAllowed(true, true, true), "the cheat menu blocks the game over the achievements overlay");

  if (failures) {
    std::printf("ACHIEVEMENTS OVERLAY RULES: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("ACHIEVEMENTS OVERLAY RULES OK: 0 failure(s)\n");
  return 0;
}
