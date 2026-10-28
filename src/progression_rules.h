#pragma once

// Pilot progression (progression.h): a level from XP, a mastery tier per leaderboard, three open bounties and the
// cosmetic rewards they unlock, with lifetime totals for the stats page. Pure, so it is unit tested.
//
// XP comes from time played, a new best and each tier climbed, and finished bounties; never from a share of the
// score, so no mode pays better than another. Nothing goes down or runs out: bests, tiers, XP and unlocks only grow,
// and bounties wait for as long as they take. Existing bests count: seeding them gives their tiers' XP once.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gw2::progression {

// The leaderboards (boards.h): 0 Deadline .. 5 King, 6 Master Deadline, 7 Master Evolved.
inline constexpr uint32_t kBoards = 8;
inline constexpr uint32_t kModes = 6;
inline constexpr uint32_t kSequence = 2;
inline constexpr int32_t kSequenceLevels = 20;

inline constexpr uint64_t kXpPerSecond = 10;
inline constexpr uint64_t kBestXp = 1'000;
// A custom mode's or practice level's best: short runs with noisy scores, and a mod's rules make a new mode, so less.
inline constexpr uint64_t kCustomBestXp = 250;
inline constexpr uint64_t kTierXp = 2'000;
inline constexpr uint64_t kBountyXp = 1'500;
inline constexpr size_t kOpenBounties = 3;

// The game's enemy types (run_stats.h).
inline constexpr int32_t kBlackHole = 3;
inline constexpr int32_t kGate = 24;
inline constexpr int32_t kRocket = 22;

enum class Tier : uint8_t { kNone, kBronze, kSilver, kGold, kNeon, kMaster };
inline constexpr uint8_t kTopTier = uint8_t(Tier::kMaster);

std::string_view TierName(Tier tier);
std::string_view BoardName(uint32_t board);
// A board's tier for a score: the same everywhere, so a leaderboard row's tier comes from its score alone.
Tier TierFor(uint32_t board, uint64_t score);
// The score that reaches the next tier, or none at the top.
std::optional<uint64_t> NextTierScore(uint32_t board, uint64_t score);

// Level n costs 500 + 250 (n - 1) XP to leave; there is no top level.
uint64_t XpToNext(uint32_t level);
struct Level {
  uint32_t level = 1;
  uint64_t into = 0;    // XP into this level
  uint64_t needed = 0;  // XP this level takes
};
Level LevelFor(uint64_t total_xp);

// A finished single-player run, as progression sees it.
struct Run {
  uint32_t mode = 0;               // the game mode played, 0-5
  std::optional<uint32_t> board;   // its leaderboard, 0-7; none for a custom mode or practice
  std::string custom_id;           // a custom mode's id, for its own best
  std::optional<int32_t> level;    // Sequence practice: the level played
  double seconds = 0;
  uint64_t score = 0;
  std::map<int32_t, int32_t> kills;  // by enemy type
  int32_t best_blast = 0;            // kills in the run's biggest gate blast
  int32_t deaths = 0;
  int32_t bombs = 0;
  int64_t geoms = 0;
  bool finished = true;  // ended in play (its last life, its time or its level), not quit from the pause menu
};

enum class Goal : uint8_t {
  kScore,       // score `target` in one run
  kSurvive,     // last `target` seconds in one run
  kNoBomb,      // score `target` in one run without a bomb
  kKills,       // destroy `target` of enemy `type` (0: any enemy), over any number of runs
  kBlast,       // catch `target` enemies in one gate blast
  kPractice,    // clear Sequence level `target` in practice
  kGeoms,       // collect `target` geoms, over any number of runs
};

struct Bounty {
  Goal goal = Goal::kScore;
  uint32_t board = 0;  // where it counts (kPractice: Sequence practice)
  int32_t type = 0;
  uint64_t target = 0;
  uint64_t progress = 0;  // kKills only: the count so far
  bool operator==(const Bounty&) const = default;
};
std::string BountyText(const Bounty& bounty);
// "120 of 400" for a bounty that counts up over runs, otherwise empty.
std::string BountyProgress(const Bounty& bounty);

struct ModeTotals {
  uint32_t runs = 0;
  double seconds = 0;
  int64_t kills = 0;
  int32_t best_blast = 0;
  int64_t geoms = 0;
  bool operator==(const ModeTotals&) const = default;
};

struct Lifetime {
  std::array<ModeTotals, kModes> modes{};
  std::map<int32_t, int64_t> kills;  // by enemy type, black holes and gates included
  int64_t deaths = 0;
  int64_t bombs = 0;
  int64_t geoms = 0;
  int32_t best_blast = 0;
  uint32_t bounties = 0;
  uint32_t practice_clears = 0;
  bool operator==(const Lifetime&) const = default;
};

enum class RewardKind : uint8_t { kPalette, kCrosshair, kTitle, kShipColour };

struct Reward {
  RewardKind kind;
  std::string_view id;    // kept in the save and in settings
  std::string_view name;  // as shown
  std::string_view how;   // how it is earned, as shown while locked
};
const std::vector<Reward>& Rewards();
const Reward* FindReward(std::string_view id);

struct State {
  uint64_t xp = 0;
  std::array<uint64_t, kBoards> bests{};
  std::map<std::string, uint64_t> custom_bests;  // custom modes and practice levels, by id
  Lifetime life;
  std::vector<Bounty> bounties;
  uint64_t seed = 0x9E3779B97F4A7C15ull;
  std::set<std::string> announced;  // rewards already shown as unlocked
  std::string title;                // the title worn, if any
  bool operator==(const State&) const = default;
};

Tier TierOf(const State& state, uint32_t board);
uint32_t LevelOf(const State& state);
bool Unlocked(const State& state, const Reward& reward);

// A best known from elsewhere (the saved local bests, or the player's own row on an online board): its tiers' XP, and
// no announcement. Returns the XP given.
uint64_t NoteBest(State& state, uint32_t board, uint64_t score);
// The same for a custom mode's best, by its id: no XP, only so its next run isn't a new best by mistake.
void NoteCustomBest(State& state, const std::string& id, uint64_t score);

struct TierUp {
  uint32_t board;
  Tier from;
  Tier to;
};

struct Outcome {
  uint64_t time_xp = 0;
  uint64_t best_xp = 0;
  uint64_t tier_xp = 0;
  uint64_t bounty_xp = 0;
  bool new_best = false;
  Level before;
  Level after;
  std::vector<TierUp> tiers;
  std::vector<Bounty> finished;
  std::vector<const Reward*> unlocked;  // newly, to announce
  uint64_t total() const { return time_xp + best_xp + tier_xp + bounty_xp; }
};

// Counts a finished run: XP, bests, tiers, lifetime totals, bounties (refilled), and rewards newly unlocked.
Outcome Apply(State& state, const Run& run);

// Fills the open bounties up to kOpenBounties, each on a board the others don't use.
void FillBounties(State& state);
// Swaps bounty `index` for a new one, free.
void Reroll(State& state, size_t index);
// Marks every reward unlocked now as announced, so seeding never fires a burst of them.
void MarkAnnounced(State& state);

// The save file (TOML). Reading is strict about types; unknown keys are kept out, damaged values refused.
std::string Write(const State& state);
std::optional<State> Read(std::string_view text);

}  // namespace gw2::progression
