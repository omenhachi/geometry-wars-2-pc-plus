#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace gw2::cheats {

enum class Cheat : uint8_t { Invincible, InfiniteShield, InfiniteBombs };
inline constexpr size_t kCheatCount = 3;
inline constexpr bool kCheatDefault = false;

inline constexpr char kInvincibleKey[] = "gw2_cheat_invincible";
inline constexpr char kInfiniteShieldKey[] = "gw2_cheat_infinite_shield";
inline constexpr char kInfiniteBombsKey[] = "gw2_cheat_infinite_bombs";

// The one list the launcher page, the in-game menu and the settings are built from.
struct CheatInfo {
  Cheat cheat;
  const char* key;
  const char* label;
  const char* description;
};

inline constexpr CheatInfo kCheats[] = {
    {Cheat::Invincible, kInvincibleKey, "Invincible", "Prevents the player from dying"},
    {Cheat::InfiniteShield, kInfiniteShieldKey, "Infinite Shield", "Keeps the player's protective shield active"},
    {Cheat::InfiniteBombs, kInfiniteBombsKey, "Infinite Bombs", "Bombs are not consumed when used"},
};
static_assert(std::size(kCheats) == kCheatCount);

// The launcher's saved value of a cheat, or the in-game menu's override of it for this session.
// Defined in cheats.cpp.
bool Enabled(Cheat cheat);
// Never writes the saved settings.
void ToggleForSession(Cheat cheat);

// What the in-game menu changed. It only records differences from the saved value, so the saved value is
// never altered and a cheat toggled back follows the saved value again.
class SessionCheats {
 public:
  bool Effective(Cheat cheat, bool saved) const {
    const uint8_t state = states_[Index(cheat)].load(std::memory_order_relaxed);
    return state == kFollowsSaved ? saved : state == kOn;
  }

  void Set(Cheat cheat, bool saved, bool enabled) {
    states_[Index(cheat)].store(enabled == saved ? kFollowsSaved : (enabled ? kOn : kOff),
                                std::memory_order_relaxed);
  }

  void Toggle(Cheat cheat, bool saved) { Set(cheat, saved, !Effective(cheat, saved)); }

 private:
  static constexpr uint8_t kFollowsSaved = 0;
  static constexpr uint8_t kOff = 1;
  static constexpr uint8_t kOn = 2;

  static size_t Index(Cheat cheat) { return static_cast<size_t>(cheat); }

  std::array<std::atomic<uint8_t>, kCheatCount> states_{};
};

}  // namespace gw2::cheats
