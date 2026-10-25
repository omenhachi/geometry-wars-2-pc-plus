#pragma once

// Controller rules for the Watch replays list (replays_menu.cpp). While it is open the game sees a neutral pad, and the
// list reads the real one: new presses become actions. The button that closed the list is held back from the game
// until it is let go, so it does not press anything on the menu behind. The keyboard reaches the list the same way,
// through the game's keyboard-to-pad bindings.

#include <array>
#include <cstdint>
#include <cstdlib>

namespace gw2::replays_menu {

inline constexpr uint16_t kDpadUp = 0x0001;
inline constexpr uint16_t kDpadDown = 0x0002;
inline constexpr uint16_t kDpadLeft = 0x0004;
inline constexpr uint16_t kDpadRight = 0x0008;
inline constexpr uint16_t kStart = 0x0010;
inline constexpr uint16_t kBack = 0x0020;
inline constexpr uint16_t kLeftShoulder = 0x0100;
inline constexpr uint16_t kRightShoulder = 0x0200;
inline constexpr uint16_t kA = 0x1000;
inline constexpr uint16_t kB = 0x2000;
inline constexpr uint16_t kX = 0x4000;
inline constexpr uint16_t kY = 0x8000;
inline constexpr int kStickDeadzone = 12000;

enum class Action { kNone, kUp, kDown, kLeft, kRight, kPreviousTab, kNextTab, kWatch, kStar, kDelete, kClose };

struct Pad {
  uint16_t buttons = 0;
  int left_y = 0;   // up is positive
  int right_y = 0;  // the arrow keys reach the right stick
  int left_x = 0;   // right is positive
  int right_x = 0;
};

class ListInput {
 public:
  static constexpr uint32_t kUsers = 4;

  void SetOpen(bool open) {
    open_ = open;
    // Whatever each pad holds as the list opens (the A that chose the row) is not a new press: its first poll only
    // takes note.
    if (open) seen_.fill({});
  }
  bool open() const { return open_; }

  // The game must see a neutral pad: the list is open, or the button that closed it is still held.
  bool HidesInput() const { return open_ || holding_; }

  struct Verdict {
    Action action = Action::kNone;
    bool hide = false;
  };

  Verdict Poll(uint32_t user, const Pad& pad) {
    Verdict verdict;
    if (user >= kUsers) return verdict;
    Seen& seen = seen_[user];
    const bool up = (pad.buttons & kDpadUp) || pad.left_y > kStickDeadzone || pad.right_y > kStickDeadzone;
    const bool down = (pad.buttons & kDpadDown) || pad.left_y < -kStickDeadzone || pad.right_y < -kStickDeadzone;
    const bool left = (pad.buttons & kDpadLeft) || pad.left_x < -kStickDeadzone || pad.right_x < -kStickDeadzone;
    const bool right = (pad.buttons & kDpadRight) || pad.left_x > kStickDeadzone || pad.right_x > kStickDeadzone;
    if (open_ && !seen.noted) {
      seen = {true, pad.buttons, up, down, left, right};
      verdict.hide = true;
      return verdict;
    }
    const uint16_t pressed = uint16_t(pad.buttons & ~seen.buttons);
    seen.buttons = pad.buttons;
    const bool new_up = up && !seen.up, new_down = down && !seen.down;
    const bool new_left = left && !seen.left, new_right = right && !seen.right;
    seen.up = up;
    seen.down = down;
    seen.left = left;
    seen.right = right;
    if (open_) {
      if (pressed & (kB | kBack)) verdict.action = Action::kClose;
      else if (pressed & (kA | kStart)) verdict.action = Action::kWatch;
      else if (pressed & kY) verdict.action = Action::kStar;
      else if (pressed & kX) verdict.action = Action::kDelete;
      else if (pressed & kLeftShoulder) verdict.action = Action::kPreviousTab;
      else if (pressed & kRightShoulder) verdict.action = Action::kNextTab;
      else if (new_up) verdict.action = Action::kUp;
      else if (new_down) verdict.action = Action::kDown;
      else if (new_left) verdict.action = Action::kLeft;
      else if (new_right) verdict.action = Action::kRight;
      if (verdict.action == Action::kClose || verdict.action == Action::kWatch) {
        open_ = false;
        holding_ = true;
        holder_ = user;
      }
      verdict.hide = true;
      return verdict;
    }
    if (holding_) {
      verdict.hide = true;
      if (user == holder_ && !(pad.buttons & (kA | kB | kStart | kBack))) holding_ = false;
      return verdict;
    }
    return verdict;
  }

 private:
  struct Seen {
    bool noted = false;
    uint16_t buttons = 0;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
  };

  bool open_ = false;
  bool holding_ = false;
  uint32_t holder_ = 0;  // the pad whose press closed the list
  std::array<Seen, kUsers> seen_{};
};

}  // namespace gw2::replays_menu
