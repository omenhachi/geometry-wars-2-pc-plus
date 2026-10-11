#pragma once

#include <string>
#include <vector>

#include <rex/system/achievement_store.h>

namespace gw2 {

struct AchievementEntry {
  rex::system::AchievementInfo info;
  bool unlocked = false;
};

struct AchievementTotals {
  int total = 0;
  int unlocked = 0;
  int gamerscore_total = 0;
  int gamerscore_earned = 0;
};

AchievementTotals TotalAchievements(const std::vector<AchievementEntry>& entries);

// The unlocked description once earned; the locked one while it is not, falling back to the unlocked
// description when the title has no locked text.
const std::string& AchievementDescription(const AchievementEntry& entry);

}  // namespace gw2
