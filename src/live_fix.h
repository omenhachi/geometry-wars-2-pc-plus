#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "live_stats.h"

namespace gw2::live {

// The app side of what the game asks the host for. Both run on a guest thread.
struct AppBridge {
  std::function<void()> open_achievements;
  std::function<void()> close_achievements;
};

struct InstallParams {
  std::filesystem::path stats_file;
  LocalUser user;
  AppBridge bridge;
};

// Called once, before the game starts.
void Install(InstallParams params);

// Set while the achievements overlay is up: the game's controller input stays neutral, and B closes
// the overlay without the game seeing that press.
void SetAchievementsModal(bool modal);

// Whether the achievements gate needs the game's input unfiltered: the overlay is up, or the B that closed it is still
// held. Safe to read from any thread.
bool AchievementsHoldInput();

// Left stick or D-pad direction read from the game's controller while the overlay is up: -1 up .. 1 down.
float AchievementsScrollAxis();

// The game's leaderboard screen while it shows: its board and the scores of the rows on screen, top to bottom (it keeps
// only those), for their tier badges (progression_overlay.cpp).
struct ShownBoard {
  uint32_t board = 0;
  std::vector<uint64_t> scores;
};
std::optional<ShownBoard> LeaderboardOnScreen();

}  // namespace gw2::live
