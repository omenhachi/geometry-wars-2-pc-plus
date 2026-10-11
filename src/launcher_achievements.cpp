#include "launcher_achievements.h"

namespace gw2 {

AchievementTotals TotalAchievements(const std::vector<AchievementEntry>& entries) {
  AchievementTotals totals;
  for (const AchievementEntry& entry : entries) {
    const int gamerscore = static_cast<int>(entry.info.gamerscore);
    ++totals.total;
    totals.gamerscore_total += gamerscore;
    if (entry.unlocked) {
      ++totals.unlocked;
      totals.gamerscore_earned += gamerscore;
    }
  }
  return totals;
}

const std::string& AchievementDescription(const AchievementEntry& entry) {
  if (entry.unlocked || entry.info.unachieved_description.empty()) {
    return entry.info.description;
  }
  return entry.info.unachieved_description;
}

}  // namespace gw2
