#pragma once

#include <array>
#include <cstddef>

#include "cheats.h"

namespace gw2 {

inline constexpr char kCheatMenuTitle[] = "CHEATS";
inline constexpr char kCheatMenuCloseHint[] = "F10 Close";
inline constexpr char kCheatMenuNote[] = "Not saved to the launcher";

struct CheatMenuRow {
  cheats::Cheat cheat;
  const char* label;
};

constexpr std::array<CheatMenuRow, cheats::kCheatCount> MakeCheatMenuRows() {
  std::array<CheatMenuRow, cheats::kCheatCount> rows{};
  for (size_t i = 0; i < cheats::kCheatCount; ++i) {
    rows[i] = {cheats::kCheats[i].cheat, cheats::kCheats[i].label};
  }
  return rows;
}

inline constexpr std::array<CheatMenuRow, cheats::kCheatCount> kCheatMenuRows = MakeCheatMenuRows();

// A button that is never reported released (a controller unplugged as the menu closes) cannot block the game forever.
inline constexpr double kReleaseTimeoutSeconds = 1.0;

// Whether the menu may release the game's input: nothing it received is still held, or the wait ran out.
inline bool InputReleased(bool anything_held, double waited_seconds, double timeout_seconds) {
  return !anything_held || waited_seconds >= timeout_seconds;
}

// Closing the menu (B, Escape, F10) starts a wait, and the game gets its input back only after what was
// pressed in the menu is released, so the closing press never also reaches the game.
class CloseGate {
 public:
  void Request() { requested_ = true; }
  bool requested() const { return requested_; }

  // Once per frame. True when the menu may go and the game may read its input again.
  bool Step(bool anything_held, double now) {
    if (!requested_) {
      return false;
    }
    if (since_ < 0.0) {
      since_ = now;
    }
    return InputReleased(anything_held, now - since_, kReleaseTimeoutSeconds);
  }

 private:
  bool requested_ = false;
  double since_ = -1.0;
};

}  // namespace gw2
