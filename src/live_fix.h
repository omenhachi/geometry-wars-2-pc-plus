#pragma once

#include <filesystem>
#include <functional>

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

// Left stick or D-pad direction read from the game's controller while the overlay is up: -1 up .. 1 down.
float AchievementsScrollAxis();

}  // namespace gw2::live
