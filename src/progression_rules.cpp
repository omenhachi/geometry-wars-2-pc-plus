#include "progression_rules.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include <toml++/toml.hpp>

#include "custom_mode_rules.h"

namespace gw2::progression {
namespace {

using custom_mode::ShortScore;

constexpr int64_t kFormat = 1;

// Each board's tier thresholds, Bronze to Master: about even steps on a log scale, set against the online boards'
// spread in October 2026 (Bronze: the mode properly played; Silver: about the middle; Neon: the top ten; Master: rare).
constexpr std::array<std::array<uint64_t, kTopTier>, kBoards> kTiers = {{
    {500'000, 2'000'000, 8'000'000, 25'000'000, 75'000'000},                // Deadline
    {1'000'000, 5'000'000, 20'000'000, 75'000'000, 250'000'000},            // Evolved
    {2'000'000, 10'000'000, 30'000'000, 60'000'000, 100'000'000},           // Sequence
    {250'000, 1'000'000, 5'000'000, 20'000'000, 50'000'000},                // Waves
    {1'000'000, 5'000'000, 50'000'000, 500'000'000, 2'500'000'000},         // Pacifism
    {250'000, 1'000'000, 3'000'000, 8'000'000, 12'000'000},                 // King
    {250'000, 1'000'000, 5'000'000, 15'000'000, 50'000'000},                // Master Deadline
    {500'000, 2'000'000, 8'000'000, 25'000'000, 50'000'000},                // Master Evolved
}};

constexpr std::string_view kBoardNames[kBoards] = {"Deadline", "Evolved",  "Sequence",        "Waves",
                                                   "Pacifism", "King",     "Master Deadline", "Master Evolved"};

// How a reward is earned.
struct Condition {
  enum Kind : uint8_t { kLevel, kTier, kTierCount, kBounties, kKills, kPractice, kGeoms } kind;
  uint32_t board = 0;  // kTier: the board, or kBoards for any Master board
  Tier tier = Tier::kNone;
  uint64_t count = 0;  // the level, the number of boards, bounties, kills or clears
  int32_t type = 0;    // kKills: the enemy type
};

struct Entry {
  Reward reward;
  Condition condition;
};

const std::vector<Entry>& Entries() {
  using C = Condition;
  static const std::vector<Entry> entries = {
      // Interface palettes that move (hud_colour.h). The game's own colours, Rainbow and Custom stay free.
      {{RewardKind::kPalette, "synthwave", "Synthwave", "Reach level 5"}, {C::kLevel, 0, Tier::kNone, 5}},
      {{RewardKind::kPalette, "aurora", "Aurora", "Reach level 12"}, {C::kLevel, 0, Tier::kNone, 12}},
      {{RewardKind::kPalette, "plasma", "Plasma", "Gold in any 3 modes"}, {C::kTierCount, 0, Tier::kGold, 3}},
      // Crosshairs for mouse aim (aim_camera.h).
      {{RewardKind::kCrosshair, "diamond", "Diamond", "Reach level 3"}, {C::kLevel, 0, Tier::kNone, 3}},
      {{RewardKind::kCrosshair, "reticle", "Reticle", "Reach level 8"}, {C::kLevel, 0, Tier::kNone, 8}},
      {{RewardKind::kCrosshair, "starburst", "Starburst", "Reach level 15"}, {C::kLevel, 0, Tier::kNone, 15}},
      {{RewardKind::kCrosshair, "halo", "Halo", "Finish 5 bounties"}, {C::kBounties, 0, Tier::kNone, 5}},
      // Titles, worn on the main menu and the results.
      {{RewardKind::kTitle, "rookie", "Rookie", "Reach level 2"}, {C::kLevel, 0, Tier::kNone, 2}},
      {{RewardKind::kTitle, "deadline-dancer", "Deadline Dancer", "Gold in Deadline"}, {C::kTier, 0, Tier::kGold}},
      {{RewardKind::kTitle, "evolutionary", "Evolutionary", "Gold in Evolved"}, {C::kTier, 1, Tier::kGold}},
      {{RewardKind::kTitle, "sequence-breaker", "Sequence Breaker", "Survive 10 Sequence levels in practice"},
       {C::kPractice, 0, Tier::kNone, 10}},
      {{RewardKind::kTitle, "wave-rider", "Wave Rider", "Gold in Waves"}, {C::kTier, 3, Tier::kGold}},
      {{RewardKind::kTitle, "pacifist", "Pacifist", "Gold in Pacifism"}, {C::kTier, 4, Tier::kGold}},
      {{RewardKind::kTitle, "king-of-the-hill", "King of the Hill", "Gold in King"}, {C::kTier, 5, Tier::kGold}},
      {{RewardKind::kTitle, "gate-crasher", "Gate Crasher", "Destroy 250 gates"},
       {C::kKills, 0, Tier::kNone, 250, kGate}},
      {{RewardKind::kTitle, "black-hole-gourmet", "Black Hole Gourmet", "Destroy 100 black holes"},
       {C::kKills, 0, Tier::kNone, 100, kBlackHole}},
      {{RewardKind::kTitle, "geom-hoarder", "Geom Hoarder", "Collect 10,000 geoms"}, {C::kGeoms, 0, Tier::kNone, 10'000}},
      {{RewardKind::kTitle, "globetrotter", "Globetrotter", "Bronze on all 8 boards"},
       {C::kTierCount, 0, Tier::kBronze, kBoards}},
      {{RewardKind::kTitle, "bounty-hunter", "Bounty Hunter", "Finish 10 bounties"}, {C::kBounties, 0, Tier::kNone, 10}},
      {{RewardKind::kTitle, "master-pilot", "Master Pilot", "Neon on a Master board"}, {C::kTier, kBoards, Tier::kNeon}},
      {{RewardKind::kTitle, "neon-legend", "Neon Legend", "Reach level 30"}, {C::kLevel, 0, Tier::kNone, 30}},
      // Ship colours (ship_colour.h).
      {{RewardKind::kShipColour, "ship-pink", "Hot Pink ship", "Reach level 6"}, {C::kLevel, 0, Tier::kNone, 6}},
      {{RewardKind::kShipColour, "ship-gold", "Gold ship", "Reach level 10"}, {C::kLevel, 0, Tier::kNone, 10}},
      {{RewardKind::kShipColour, "ship-cyan", "Ice ship", "Silver in all six modes"},
       {C::kTierCount, kModes, Tier::kSilver, kModes}},
  };
  return entries;
}

// Bounties each board can draw: the score bounty on every board, and the mode's own kind of challenge.
struct Template {
  Goal goal;
  uint32_t board;
  int32_t type;
  uint64_t target;  // 0: worked out from the player's tier (score bounties) or drawn (practice)
};

constexpr Template kTemplates[] = {
    {Goal::kScore, 0, 0, 0},       {Goal::kBlast, 0, 0, 20},          {Goal::kKills, 0, 0, 1'500},
    {Goal::kGeoms, 0, 0, 1'000},
    {Goal::kScore, 1, 0, 0},       {Goal::kSurvive, 1, 0, 300},       {Goal::kKills, 1, kBlackHole, 15},
    {Goal::kGeoms, 1, 0, 1'500},
    {Goal::kScore, 2, 0, 0},       {Goal::kPractice, 2, 0, 0},
    {Goal::kScore, 3, 0, 0},       {Goal::kKills, 3, kRocket, 600},   {Goal::kSurvive, 3, 0, 150},
    {Goal::kScore, 4, 0, 0},       {Goal::kKills, 4, kGate, 150},     {Goal::kSurvive, 4, 0, 180},
    {Goal::kGeoms, 4, 0, 600},
    {Goal::kScore, 5, 0, 0},       {Goal::kNoBomb, 5, 0, 0},          {Goal::kKills, 5, 0, 1'000},
    {Goal::kScore, 6, 0, 0},       {Goal::kScore, 7, 0, 0},           {Goal::kSurvive, 7, 0, 240},
};

uint64_t NextRandom(State& state) {
  uint64_t x = state.seed;  // xorshift64*
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  state.seed = x;
  return x * 0x2545F4914F6CDD1Dull;
}

// The first of 1, 1.5, 2, 2.5, 3, 4, 5, 6, 8 times a power of ten at or above `value`: a round number, never lower.
uint64_t Nice(uint64_t value) {
  uint64_t unit = 1;
  while (unit * 10 <= value) unit *= 10;
  for (const uint64_t tenths : {10, 15, 20, 25, 30, 40, 50, 60, 80, 100}) {
    if (unit * tenths / 10 >= value) return unit * tenths / 10;
  }
  return unit * 10;
}

// A score to aim for on `board`: halfway to the next tier, or a tenth past the best at the top; always above the best,
// so a bounty is never done before it starts. Without a bomb: half of that, which may be below the best.
uint64_t ScoreTarget(const State& state, uint32_t board, bool no_bomb) {
  const uint64_t best = state.bests[board];
  const std::optional<uint64_t> next = NextTierScore(board, best);
  uint64_t target = std::max(next ? *next / 2 : 0, best + best / 10 + 1);
  target = std::max<uint64_t>(target, kTiers[board][0] / 2);
  if (no_bomb) target /= 2;
  return Nice(target);
}

Bounty Draw(State& state) {
  std::vector<uint32_t> boards;
  for (uint32_t board = 0; board < kBoards; ++board) {
    if (std::none_of(state.bounties.begin(), state.bounties.end(), [&](const Bounty& b) { return b.board == board; })) {
      boards.push_back(board);
    }
  }
  if (boards.empty()) boards.push_back(0);
  const uint32_t board = boards[NextRandom(state) % boards.size()];
  std::vector<const Template*> choices;
  for (const Template& t : kTemplates) {
    if (t.board == board) choices.push_back(&t);
  }
  const Template& t = *choices[NextRandom(state) % choices.size()];
  Bounty bounty{t.goal, t.board, t.type, t.target, 0};
  if (t.goal == Goal::kScore || t.goal == Goal::kNoBomb) bounty.target = ScoreTarget(state, board, t.goal == Goal::kNoBomb);
  if (t.goal == Goal::kPractice) bounty.target = 5 + NextRandom(state) % (kSequenceLevels - 4);
  return bounty;
}

int64_t EnemyKills(const std::map<int32_t, int32_t>& kills) {
  int64_t total = 0;
  for (const auto& [type, count] : kills) {
    if (type != kGate) total += count;
  }
  return total;
}

int32_t KillsOf(const std::map<int32_t, int32_t>& kills, int32_t type) {
  const auto it = kills.find(type);
  return it == kills.end() ? 0 : it->second;
}

bool PracticeClear(const Run& run) {
  return run.level && run.finished && run.deaths == 0;
}

// Whether `run` finishes `bounty`; a counting bounty takes the run's kills first.
bool Finishes(Bounty& bounty, const Run& run) {
  if (bounty.goal == Goal::kPractice) return PracticeClear(run) && uint64_t(*run.level) == bounty.target;
  if (run.board != bounty.board) return false;
  switch (bounty.goal) {
    case Goal::kScore:
      return run.score >= bounty.target;
    case Goal::kSurvive:
      return run.seconds >= double(bounty.target);
    case Goal::kNoBomb:
      return run.bombs == 0 && run.score >= bounty.target;
    case Goal::kBlast:
      return uint64_t(std::max(run.best_blast, 0)) >= bounty.target;
    case Goal::kGeoms:
      bounty.progress = std::min<uint64_t>(bounty.target, bounty.progress + uint64_t(std::max<int64_t>(run.geoms, 0)));
      return bounty.progress >= bounty.target;
    case Goal::kKills: {
      const int64_t got = bounty.type == 0 ? EnemyKills(run.kills) : KillsOf(run.kills, bounty.type);
      bounty.progress = std::min<uint64_t>(bounty.target, bounty.progress + uint64_t(std::max<int64_t>(got, 0)));
      return bounty.progress >= bounty.target;
    }
    case Goal::kPractice:
      break;
  }
  return false;
}

uint64_t RaiseBest(State& state, uint32_t board, uint64_t score) {
  if (board >= kBoards || score <= state.bests[board]) return 0;
  const Tier from = TierFor(board, state.bests[board]);
  state.bests[board] = score;
  const Tier to = TierFor(board, score);
  return kTierXp * (uint8_t(to) - uint8_t(from));
}

// 1,500.
std::string Commas(uint64_t value) {
  std::string digits = std::to_string(value);
  for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ",");
  return digits;
}

std::string_view EnemyName(int32_t type, bool plural) {
  switch (type) {
    case kBlackHole: return plural ? "black holes" : "black hole";
    case kGate: return plural ? "gates" : "gate";
    case kRocket: return plural ? "rockets" : "rocket";
    default: return plural ? "enemies" : "enemy";
  }
}

}  // namespace

std::string_view TierName(Tier tier) {
  constexpr std::string_view kNames[] = {"Unranked", "Bronze", "Silver", "Gold", "Neon", "Master"};
  return kNames[std::min<uint8_t>(uint8_t(tier), kTopTier)];
}

std::string_view BoardName(uint32_t board) {
  return board < kBoards ? kBoardNames[board] : std::string_view("?");
}

Tier TierFor(uint32_t board, uint64_t score) {
  if (board >= kBoards) return Tier::kNone;
  uint8_t tier = 0;
  while (tier < kTopTier && score >= kTiers[board][tier]) ++tier;
  return Tier(tier);
}

std::optional<uint64_t> NextTierScore(uint32_t board, uint64_t score) {
  if (board >= kBoards) return std::nullopt;
  const uint8_t tier = uint8_t(TierFor(board, score));
  if (tier >= kTopTier) return std::nullopt;
  return kTiers[board][tier];
}

uint64_t XpToNext(uint32_t level) {
  return 500 + 250 * uint64_t(std::max(level, 1u) - 1);
}

Level LevelFor(uint64_t total_xp) {
  Level level;
  uint64_t left = total_xp;
  while (left >= XpToNext(level.level)) {
    left -= XpToNext(level.level);
    ++level.level;
  }
  level.into = left;
  level.needed = XpToNext(level.level);
  return level;
}

std::string BountyText(const Bounty& b) {
  const std::string board(BoardName(b.board));
  switch (b.goal) {
    case Goal::kScore: return std::format("Score {} in {}", ShortScore(b.target), board);
    case Goal::kSurvive: return std::format("Last {}:{:02} in {}", b.target / 60, b.target % 60, board);
    case Goal::kNoBomb: return std::format("Score {} in {} without a bomb", ShortScore(b.target), board);
    case Goal::kKills: return std::format("Destroy {} {} in {}", Commas(b.target), EnemyName(b.type, true), board);
    case Goal::kBlast: return std::format("Catch {} enemies in one gate blast in {}", b.target, board);
    case Goal::kPractice: return std::format("Survive Sequence level {} in practice", b.target);
    case Goal::kGeoms: return std::format("Collect {} geoms in {}", Commas(b.target), board);
  }
  return {};
}

std::string BountyProgress(const Bounty& b) {
  return b.goal == Goal::kKills || b.goal == Goal::kGeoms
             ? std::format("{} of {}", Commas(b.progress), Commas(b.target))
             : std::string();
}

const std::vector<Reward>& Rewards() {
  static const std::vector<Reward> rewards = [] {
    std::vector<Reward> out;
    for (const Entry& e : Entries()) out.push_back(e.reward);
    return out;
  }();
  return rewards;
}

const Reward* FindReward(std::string_view id) {
  for (const Reward& r : Rewards()) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

Tier TierOf(const State& state, uint32_t board) {
  return board < kBoards ? TierFor(board, state.bests[board]) : Tier::kNone;
}

uint32_t LevelOf(const State& state) {
  return LevelFor(state.xp).level;
}

bool Unlocked(const State& state, const Reward& reward) {
  for (const Entry& e : Entries()) {
    if (e.reward.id != reward.id) continue;
    const Condition& c = e.condition;
    switch (c.kind) {
      case Condition::kLevel:
        return LevelOf(state) >= c.count;
      case Condition::kTier:
        if (c.board == kBoards) {
          return TierOf(state, 6) >= c.tier || TierOf(state, 7) >= c.tier;
        }
        return TierOf(state, c.board) >= c.tier;
      case Condition::kTierCount: {
        const uint32_t boards = c.board == kModes ? kModes : kBoards;  // kModes: the six game modes only
        uint64_t n = 0;
        for (uint32_t board = 0; board < boards; ++board) n += TierOf(state, board) >= c.tier;
        return n >= c.count;
      }
      case Condition::kBounties:
        return state.life.bounties >= c.count;
      case Condition::kKills: {
        const auto it = state.life.kills.find(c.type);
        return it != state.life.kills.end() && uint64_t(it->second) >= c.count;
      }
      case Condition::kPractice:
        return state.life.practice_clears >= c.count;
      case Condition::kGeoms:
        return uint64_t(state.life.geoms) >= c.count;
    }
  }
  return false;
}

uint64_t NoteBest(State& state, uint32_t board, uint64_t score) {
  const uint64_t xp = RaiseBest(state, board, score);
  state.xp += xp;
  return xp;
}

void NoteCustomBest(State& state, const std::string& id, uint64_t score) {
  if (id.empty() || score == 0) return;
  uint64_t& best = state.custom_bests[id];
  best = std::max(best, score);
}

void FillBounties(State& state) {
  while (state.bounties.size() < kOpenBounties) state.bounties.push_back(Draw(state));
}

void Reroll(State& state, size_t index) {
  if (index >= state.bounties.size()) return;
  const Bounty old = state.bounties[index];
  state.bounties.erase(state.bounties.begin() + ptrdiff_t(index));
  // Another board where one is free, so a reroll always changes something.
  Bounty fresh = Draw(state);
  for (int tries = 0; tries < 8 && fresh.board == old.board && fresh.goal == old.goal; ++tries) fresh = Draw(state);
  state.bounties.insert(state.bounties.begin() + ptrdiff_t(index), fresh);
}

void MarkAnnounced(State& state) {
  for (const Reward& r : Rewards()) {
    if (Unlocked(state, r)) state.announced.insert(std::string(r.id));
  }
}

Outcome Apply(State& state, const Run& run) {
  Outcome out;
  out.before = LevelFor(state.xp);
  out.time_xp = uint64_t(std::max(run.seconds, 0.0)) * kXpPerSecond;

  if (run.board && *run.board < kBoards) {
    const uint32_t board = *run.board;
    if (run.score > state.bests[board]) {
      out.new_best = true;
      out.best_xp = kBestXp;
      const Tier from = TierOf(state, board);
      out.tier_xp = RaiseBest(state, board, run.score);
      const Tier to = TierOf(state, board);
      if (to > from) out.tiers.push_back({board, from, to});
    }
  } else if (!run.custom_id.empty() && run.score > 0) {
    uint64_t& best = state.custom_bests[run.custom_id];
    if (run.score > best) {
      best = run.score;
      out.new_best = true;
      out.best_xp = kCustomBestXp;
    }
  }

  // Lifetime totals.
  if (run.mode < kModes) {
    ModeTotals& m = state.life.modes[run.mode];
    ++m.runs;
    m.seconds += std::max(run.seconds, 0.0);
    m.kills += EnemyKills(run.kills);
    m.best_blast = std::max(m.best_blast, run.best_blast);
    m.geoms += std::max<int64_t>(run.geoms, 0);
  }
  state.life.geoms += std::max<int64_t>(run.geoms, 0);
  for (const auto& [type, count] : run.kills) state.life.kills[type] += count;
  state.life.deaths += run.deaths;
  state.life.bombs += run.bombs;
  state.life.best_blast = std::max(state.life.best_blast, run.best_blast);
  if (PracticeClear(run)) ++state.life.practice_clears;

  // Bounties.
  for (size_t i = 0; i < state.bounties.size();) {
    if (Finishes(state.bounties[i], run)) {
      out.finished.push_back(state.bounties[i]);
      out.bounty_xp += kBountyXp;
      ++state.life.bounties;
      state.bounties.erase(state.bounties.begin() + ptrdiff_t(i));
    } else {
      ++i;
    }
  }
  state.xp += out.total();
  FillBounties(state);
  out.after = LevelFor(state.xp);

  for (const Reward& r : Rewards()) {
    if (!state.announced.contains(std::string(r.id)) && Unlocked(state, r)) {
      state.announced.insert(std::string(r.id));
      out.unlocked.push_back(FindReward(r.id));
    }
  }
  return out;
}

std::string Write(const State& s) {
  toml::table root;
  root.insert("format", kFormat);
  root.insert("xp", int64_t(s.xp));
  root.insert("seed", std::format("{:016x}", s.seed));
  root.insert("title", s.title);
  toml::array bests;
  for (const uint64_t b : s.bests) bests.push_back(int64_t(b));
  root.insert("bests", bests);
  toml::table custom;
  for (const auto& [id, best] : s.custom_bests) custom.insert(id, int64_t(best));
  root.insert("custom_bests", custom);
  toml::array announced;
  for (const std::string& id : s.announced) announced.push_back(id);
  root.insert("announced", announced);

  toml::table life;
  life.insert("deaths", s.life.deaths);
  life.insert("bombs", s.life.bombs);
  life.insert("geoms", s.life.geoms);
  life.insert("best_blast", int64_t(s.life.best_blast));
  life.insert("bounties", int64_t(s.life.bounties));
  life.insert("practice_clears", int64_t(s.life.practice_clears));
  toml::table kills;
  for (const auto& [type, count] : s.life.kills) kills.insert(std::to_string(type), count);
  life.insert("kills", kills);
  toml::array modes;
  for (const ModeTotals& m : s.life.modes) {
    modes.push_back(toml::table{{"runs", int64_t(m.runs)}, {"seconds", m.seconds}, {"kills", m.kills},
                                {"best_blast", int64_t(m.best_blast)}, {"geoms", m.geoms}});
  }
  life.insert("modes", modes);
  root.insert("life", life);

  toml::array bounties;
  for (const Bounty& b : s.bounties) {
    bounties.push_back(toml::table{{"goal", int64_t(b.goal)}, {"board", int64_t(b.board)}, {"type", int64_t(b.type)},
                                   {"target", int64_t(b.target)}, {"progress", int64_t(b.progress)}});
  }
  root.insert("bounties", bounties);

  std::ostringstream out;
  out << "# Geometry Wars 2 PC Plus: pilot level, bests, lifetime totals, bounties and rewards (progression.h).\n"
      << root << "\n";
  return out.str();
}

std::optional<State> Read(std::string_view text) {
  toml::table root;
  try {
    root = toml::parse(text);
  } catch (const toml::parse_error&) {
    return std::nullopt;
  }
  const auto whole = [](const toml::node* node, int64_t lo, int64_t hi) -> std::optional<int64_t> {
    const auto* v = node ? node->as_integer() : nullptr;
    if (!v || v->get() < lo || v->get() > hi) return std::nullopt;
    return v->get();
  };
  constexpr int64_t kMax = int64_t(1) << 62;
  if (whole(root.get("format"), 1, 1) != kFormat) return std::nullopt;
  State s;
  const auto xp = whole(root.get("xp"), 0, kMax);
  const auto* seed = root.get("seed") ? root.get("seed")->as_string() : nullptr;
  const auto* title = root.get("title") ? root.get("title")->as_string() : nullptr;
  const auto* bests = root.get("bests") ? root.get("bests")->as_array() : nullptr;
  const auto* custom = root.get("custom_bests") ? root.get("custom_bests")->as_table() : nullptr;
  const auto* announced = root.get("announced") ? root.get("announced")->as_array() : nullptr;
  const auto* life = root.get("life") ? root.get("life")->as_table() : nullptr;
  const auto* bounties = root.get("bounties") ? root.get("bounties")->as_array() : nullptr;
  if (!xp || !seed || !title || !bests || bests->size() != kBoards || !custom || !announced || !life || !bounties) {
    return std::nullopt;
  }
  s.xp = uint64_t(*xp);
  try {
    s.seed = std::stoull(seed->get(), nullptr, 16);
  } catch (...) {
    return std::nullopt;
  }
  if (s.seed == 0) return std::nullopt;
  s.title = title->get();
  if (!s.title.empty() && (!FindReward(s.title) || FindReward(s.title)->kind != RewardKind::kTitle)) s.title.clear();
  for (size_t i = 0; i < kBoards; ++i) {
    const auto b = whole(bests->get(i), 0, kMax);
    if (!b) return std::nullopt;
    s.bests[i] = uint64_t(*b);
  }
  for (const auto& [id, node] : *custom) {
    const auto b = whole(&node, 0, kMax);
    if (!b) return std::nullopt;
    s.custom_bests[std::string(id.str())] = uint64_t(*b);
  }
  for (const auto& node : *announced) {
    const auto* id = node.as_string();
    if (!id) return std::nullopt;
    if (FindReward(id->get())) s.announced.insert(id->get());
  }

  const auto deaths = whole(life->get("deaths"), 0, kMax);
  const auto bombs = whole(life->get("bombs"), 0, kMax);
  const auto blast = whole(life->get("best_blast"), 0, INT32_MAX);
  const auto done = whole(life->get("bounties"), 0, UINT32_MAX);
  const auto clears = whole(life->get("practice_clears"), 0, UINT32_MAX);
  const auto* kills = life->get("kills") ? life->get("kills")->as_table() : nullptr;
  const auto* modes = life->get("modes") ? life->get("modes")->as_array() : nullptr;
  if (!deaths || !bombs || !blast || !done || !clears || !kills || !modes || modes->size() != kModes) return std::nullopt;
  // Geoms came later than the rest: a file without them has none yet.
  const auto geoms = life->get("geoms") ? whole(life->get("geoms"), 0, kMax) : std::optional<int64_t>(0);
  if (!geoms) return std::nullopt;
  s.life.geoms = *geoms;
  s.life.deaths = *deaths;
  s.life.bombs = *bombs;
  s.life.best_blast = int32_t(*blast);
  s.life.bounties = uint32_t(*done);
  s.life.practice_clears = uint32_t(*clears);
  for (const auto& [type, node] : *kills) {
    const auto count = whole(&node, 0, kMax);
    int32_t t = 0;
    try {
      t = std::stoi(std::string(type.str()));
    } catch (...) {
      return std::nullopt;
    }
    if (!count) return std::nullopt;
    s.life.kills[t] = *count;
  }
  for (size_t i = 0; i < kModes; ++i) {
    const auto* m = modes->get(i) ? modes->get(i)->as_table() : nullptr;
    if (!m) return std::nullopt;
    const auto runs = whole(m->get("runs"), 0, UINT32_MAX);
    const auto* seconds = m->get("seconds") ? m->get("seconds")->as_floating_point() : nullptr;
    const auto mk = whole(m->get("kills"), 0, kMax);
    const auto mb = whole(m->get("best_blast"), 0, INT32_MAX);
    if (!runs || !seconds || !std::isfinite(seconds->get()) || seconds->get() < 0 || !mk || !mb) return std::nullopt;
    const auto mg = m->get("geoms") ? whole(m->get("geoms"), 0, kMax) : std::optional<int64_t>(0);
    if (!mg) return std::nullopt;
    s.life.modes[i] = {uint32_t(*runs), seconds->get(), *mk, int32_t(*mb), *mg};
  }
  for (const auto& node : *bounties) {
    const auto* b = node.as_table();
    if (!b) return std::nullopt;
    const auto goal = whole(b->get("goal"), 0, int64_t(Goal::kGeoms));
    const auto board = whole(b->get("board"), 0, kBoards - 1);
    const auto type = whole(b->get("type"), 0, 255);
    const auto target = whole(b->get("target"), 1, kMax);
    const auto progress = whole(b->get("progress"), 0, kMax);
    if (!goal || !board || !type || !target || !progress || *progress > *target) return std::nullopt;
    s.bounties.push_back({Goal(*goal), uint32_t(*board), int32_t(*type), uint64_t(*target), uint64_t(*progress)});
  }
  if (s.bounties.size() > kOpenBounties) return std::nullopt;
  return s;
}

}  // namespace gw2::progression
