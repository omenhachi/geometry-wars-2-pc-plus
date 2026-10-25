// Watch replays list input tests: new presses become actions, held ones do not, the game sees a neutral pad while the
// list is open and until the button that closed it is let go, and a pad that never polls cannot keep it held.

#include <cstdio>

#include "replays_menu_rules.h"

using namespace gw2::replays_menu;

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

Pad Buttons(uint16_t buttons) {
  return {buttons, 0, 0};
}

void TestOpening() {
  ListInput input;
  CHECK(!input.HidesInput());
  CHECK(!input.Poll(0, Buttons(kA)).hide);  // closed: the game has the pad
  input.SetOpen(true);
  CHECK(input.HidesInput());
  // The A that chose the row is still held: noted, not a press.
  auto v = input.Poll(0, Buttons(kA));
  CHECK(v.hide && v.action == Action::kNone);
  CHECK(input.Poll(0, Buttons(kA)).action == Action::kNone);
  CHECK(input.Poll(0, Buttons(0)).action == Action::kNone);
  CHECK(input.open());
}

void TestNavigation() {
  ListInput input;
  input.SetOpen(true);
  input.Poll(0, Buttons(0));
  CHECK(input.Poll(0, Buttons(kDpadDown)).action == Action::kDown);
  CHECK(input.Poll(0, Buttons(kDpadDown)).action == Action::kNone);  // held
  CHECK(input.Poll(0, Buttons(0)).action == Action::kNone);
  CHECK(input.Poll(0, {0, 20000, 0}).action == Action::kUp);         // left stick
  CHECK(input.Poll(0, {0, 20000, 0}).action == Action::kNone);
  CHECK(input.Poll(0, {0, 0, 0}).action == Action::kNone);
  CHECK(input.Poll(0, {0, 0, -20000}).action == Action::kDown);      // the arrow keys, on the right stick
  CHECK(input.Poll(0, {0, 5000, 0}).action == Action::kNone);        // inside the deadzone
  CHECK(input.Poll(0, {0, 0, 0}).action == Action::kNone);
  CHECK(input.Poll(0, Buttons(kDpadRight)).action == Action::kRight);
  CHECK(input.Poll(0, Buttons(kDpadRight)).action == Action::kNone);  // held
  CHECK(input.Poll(0, {0, 0, 0, 0, -20000}).action == Action::kLeft);  // the left arrow key
  CHECK(input.Poll(0, {0, 0, 0, 20000, 0}).action == Action::kRight);  // the left stick, the other way
  CHECK(input.Poll(0, Buttons(0)).action == Action::kNone);
  CHECK(input.Poll(0, Buttons(kY)).action == Action::kStar);
  CHECK(input.Poll(0, Buttons(0)).action == Action::kNone);
  CHECK(input.Poll(0, Buttons(kX)).action == Action::kDelete);
  CHECK(input.Poll(0, Buttons(kX | kRightShoulder)).action == Action::kNextTab);
  CHECK(input.Poll(0, Buttons(kLeftShoulder)).action == Action::kPreviousTab);
  CHECK(input.open());
}

void TestClosing() {
  ListInput input;
  input.SetOpen(true);
  input.Poll(0, Buttons(0));
  input.Poll(1, Buttons(0));
  const auto v = input.Poll(0, Buttons(kB));
  CHECK(v.action == Action::kClose && v.hide && !input.open());
  // The B is still held: the game must not see it.
  CHECK(input.Poll(0, Buttons(kB)).hide);
  CHECK(input.Poll(1, Buttons(0)).hide);
  CHECK(input.HidesInput());
  // Let go: the game has its pad back.
  CHECK(input.Poll(0, Buttons(0)).hide);
  CHECK(!input.HidesInput());
  CHECK(!input.Poll(0, Buttons(kA)).hide);
  // Watching closes it the same way, with A or Start (Enter).
  input.SetOpen(true);
  input.Poll(2, Buttons(0));
  CHECK(input.Poll(2, Buttons(kStart)).action == Action::kWatch);
  CHECK(input.HidesInput());
  input.Poll(2, Buttons(0));
  CHECK(!input.HidesInput());
}

void TestSilentPads() {
  // Pads 1 to 3 never poll (disconnected): they must not keep the list holding the game's input.
  ListInput input;
  input.SetOpen(true);
  input.Poll(0, Buttons(0));
  input.Poll(0, Buttons(kBack));
  input.Poll(0, Buttons(0));
  CHECK(!input.HidesInput());
  CHECK(input.Poll(9, Buttons(kA)).action == Action::kNone);  // out of range
}

}  // namespace

int main() {
  TestOpening();
  TestNavigation();
  TestClosing();
  TestSilentPads();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
