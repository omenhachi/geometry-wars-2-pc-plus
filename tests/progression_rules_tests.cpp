// Progression rules tests: tiers from scores, the level curve, XP from a run (time, a new best, tiers climbed,
// bounties), seeding existing bests without a burst of announcements, bounties on different boards that never expire
// and reroll freely, rewards unlocking once, and the save file surviving a round trip while damaged ones are refused.
// Pure; no game.

#include <cstdio>
#include <string>

#include "progression_rules.h"

using namespace gw2::progression;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

Run Deadline(uint64_t score, double seconds = 180) {
  Run run;
  run.mode = 0;
  run.board = 0;
  run.score = score;
  run.seconds = seconds;
  return run;
}

void Tiers() {
  CHECK(TierFor(0, 0) == Tier::kNone);
  CHECK(TierFor(0, 499'999) == Tier::kNone);
  CHECK(TierFor(0, 500'000) == Tier::kBronze);
  CHECK(TierFor(0, 18'626'515) == Tier::kGold);  // the user's Deadline best
  CHECK(TierFor(0, 122'864'815) == Tier::kMaster);
  CHECK(TierFor(4, 72'826'275) == Tier::kGold);
  CHECK(TierFor(9, 1'000'000'000) == Tier::kNone);  // no such board
  CHECK(NextTierScore(0, 18'626'515) == 25'000'000u);
  CHECK(!NextTierScore(0, 200'000'000));
  CHECK(TierName(Tier::kNeon) == "Neon");
}

void Levels() {
  CHECK(LevelFor(0).level == 1 && LevelFor(0).needed == 500);
  CHECK(LevelFor(499).level == 1 && LevelFor(499).into == 499);
  CHECK(LevelFor(500).level == 2 && LevelFor(500).into == 0 && LevelFor(500).needed == 750);
  CHECK(LevelFor(500 + 750).level == 3);
  uint64_t total = 0;
  for (uint32_t level = 1; level < 50; ++level) total += XpToNext(level);
  CHECK(LevelFor(total).level == 50 && LevelFor(total - 1).level == 49);
}

void RunXp() {
  State s;
  FillBounties(s);
  const Outcome first = Apply(s, Deadline(600'000, 180.5));
  CHECK(first.time_xp == 1'800);
  CHECK(first.new_best && first.best_xp == kBestXp);
  CHECK(first.tier_xp == kTierXp && first.tiers.size() == 1 && first.tiers[0].to == Tier::kBronze);
  CHECK(s.xp == first.total());
  CHECK(first.before.level == 1 && first.after.level == LevelFor(s.xp).level);

  const Outcome worse = Apply(s, Deadline(100'000, 60));
  CHECK(!worse.new_best && worse.best_xp == 0 && worse.tier_xp == 0 && worse.time_xp == 600);
  CHECK(s.bests[0] == 600'000);

  // Two tiers in one run: twice the tier XP.
  const Outcome jump = Apply(s, Deadline(8'000'000));
  CHECK(jump.tier_xp == 2 * kTierXp && jump.tiers.size() == 1 && jump.tiers[0].from == Tier::kBronze &&
        jump.tiers[0].to == Tier::kGold);

  // A custom mode keeps its own best, with no tier.
  Run custom;
  custom.mode = 1;
  custom.custom_id = "c806e9b70559a1e5";
  custom.score = 5'000;
  custom.seconds = 30;
  const Outcome c1 = Apply(s, custom);
  CHECK(c1.new_best && c1.best_xp == kCustomBestXp && c1.tier_xp == 0 && s.custom_bests["c806e9b70559a1e5"] == 5'000);
  CHECK(!Apply(s, custom).new_best);

  // Lifetime totals.
  CHECK(s.life.modes[0].runs == 3 && s.life.modes[1].runs == 2);
  CHECK(s.life.modes[0].seconds > 420.4 && s.life.modes[0].seconds < 420.6);
}

void Seeding() {
  State s;
  // The user's local bests, as stats.toml has them.
  const uint64_t bests[] = {18'626'515, 64'071'720, 27'184'080, 3'030'910, 72'826'275, 2'354'265, 813'865, 2'913'520};
  uint64_t given = 0;
  for (uint32_t board = 0; board < kBoards; ++board) given += NoteBest(s, board, bests[board]);
  CHECK(given == s.xp);
  CHECK(TierOf(s, 0) == Tier::kGold && TierOf(s, 2) == Tier::kSilver && TierOf(s, 6) == Tier::kBronze);
  CHECK(LevelOf(s) >= 15);
  CHECK(NoteBest(s, 0, 1'000) == 0);       // lower: nothing
  CHECK(NoteBest(s, 0, 18'626'515) == 0);  // the same: nothing
  MarkAnnounced(s);
  FillBounties(s);
  // A run after seeding announces only what it newly unlocks.
  const Outcome out = Apply(s, Deadline(1'000));
  CHECK(out.unlocked.empty());
}

void Bounties() {
  State s;
  FillBounties(s);
  CHECK(s.bounties.size() == kOpenBounties);
  CHECK(s.bounties[0].board != s.bounties[1].board && s.bounties[1].board != s.bounties[2].board &&
        s.bounties[0].board != s.bounties[2].board);
  for (const Bounty& b : s.bounties) CHECK(!BountyText(b).empty() && b.target > 0);

  // A counting bounty adds up over runs.
  State k;
  k.bounties = {{Goal::kKills, 3, kRocket, 10, 0}};
  Run waves;
  waves.mode = 3;
  waves.board = 3;
  waves.kills = {{kRocket, 4}, {2, 50}};
  waves.seconds = 20;
  Outcome o = Apply(k, waves);
  CHECK(o.finished.empty());
  bool found = false;
  for (const Bounty& b : k.bounties) {
    if (b.goal == Goal::kKills && b.board == 3 && b.type == kRocket) {
      found = true;
      CHECK(b.progress == 4 && BountyProgress(b) == "4 of 10");
    }
  }
  CHECK(found && k.bounties.size() == kOpenBounties);
  waves.kills = {{kRocket, 9}};
  o = Apply(k, waves);
  CHECK(o.finished.size() == 1 && o.bounty_xp == kBountyXp && k.life.bounties == 1 && k.bounties.size() == kOpenBounties);

  // Only its own board counts; a practice clear needs no death and a finished run.
  State p;
  p.bounties = {{Goal::kPractice, 2, 0, 12, 0}};
  Run practice;
  practice.mode = 2;
  practice.level = 12;
  practice.seconds = 30;
  practice.deaths = 1;
  CHECK(Apply(p, practice).finished.empty());
  practice.deaths = 0;
  practice.finished = false;
  CHECK(Apply(p, practice).finished.empty());
  practice.finished = true;
  CHECK(Apply(p, practice).finished.size() == 1 && p.life.practice_clears == 1);

  // No bomb means none.
  State n;
  n.bounties = {{Goal::kNoBomb, 5, 0, 100'000, 0}};
  Run king;
  king.mode = 5;
  king.board = 5;
  king.score = 200'000;
  king.bombs = 1;
  CHECK(Apply(n, king).finished.empty());
  king.bombs = 0;
  CHECK(Apply(n, king).finished.size() == 1);

  // A score bounty is always above the best (Luna's 813,865 on Master Deadline once drew 500K), and a round number.
  State t;
  const uint64_t bests[] = {3'000'000, 64'071'720, 27'184'080, 3'030'910, 72'826'275, 2'354'265, 813'865, 150'000'000};
  for (uint32_t board = 0; board < kBoards; ++board) t.bests[board] = bests[board];
  int scores = 0;
  for (int i = 0; i < 400; ++i) {
    for (const Bounty& b : t.bounties) {
      if (b.goal != Goal::kScore) continue;
      ++scores;
      CHECK(b.target > t.bests[b.board]);
      if (b.target <= t.bests[b.board]) std::printf("  board %u best %llu target %llu\n", b.board,
                                                    (unsigned long long)t.bests[b.board], (unsigned long long)b.target);
    }
    Reroll(t, size_t(i) % kOpenBounties);
    FillBounties(t);
  }
  CHECK(scores > 50);

  // A custom best known from before is not a new best again.
  State c;
  NoteCustomBest(c, "a7f6edd007fad798", 7'000);
  Run again;
  again.mode = 2;
  again.custom_id = "a7f6edd007fad798";
  again.score = 6'000;
  again.seconds = 20;
  CHECK(!Apply(c, again).new_best);
  again.score = 8'000;
  CHECK(Apply(c, again).new_best);

  // Geoms add up over runs, in their own board, and to the lifetime total and the mode's.
  State g;
  g.bounties = {{Goal::kGeoms, 1, 0, 100, 0}};
  Run evolved;
  evolved.mode = 1;
  evolved.board = 1;
  evolved.seconds = 30;
  evolved.geoms = 60;
  CHECK(Apply(g, evolved).finished.empty());
  CHECK(g.life.geoms == 60 && g.life.modes[1].geoms == 60);
  CHECK(BountyText({Goal::kGeoms, 1, 0, 1'500, 0}) == "Collect 1,500 geoms in Evolved");
  CHECK(Apply(g, evolved).finished.size() == 1);
  CHECK(g.life.geoms == 120);
  g.life.geoms = 10'000;
  CHECK(Unlocked(g, *FindReward("geom-hoarder")));

  // A reroll changes the bounty and keeps three open.
  State r;
  FillBounties(r);
  const Bounty before = r.bounties[1];
  Reroll(r, 1);
  CHECK(r.bounties.size() == kOpenBounties && !(r.bounties[1] == before));
  Reroll(r, 7);  // out of range: nothing
  CHECK(r.bounties.size() == kOpenBounties);
}

void RewardsUnlock() {
  State s;
  FillBounties(s);
  const Reward* diamond = FindReward("diamond");
  CHECK(diamond && diamond->kind == RewardKind::kCrosshair && !Unlocked(s, *diamond));
  // Enough play for level 3 unlocks Diamond (and Rookie at 2), each announced once.
  const Outcome o = Apply(s, Deadline(0, 200));
  CHECK(LevelOf(s) >= 3);
  bool rookie = false, crosshair = false;
  for (const Reward* r : o.unlocked) {
    rookie |= r->id == "rookie";
    crosshair |= r->id == "diamond";
  }
  CHECK(rookie && crosshair);
  CHECK(Apply(s, Deadline(0, 1)).unlocked.empty());
  // Kill and tier rewards.
  s.life.kills[kGate] = 250;
  CHECK(Unlocked(s, *FindReward("gate-crasher")));
  NoteBest(s, 6, 15'000'000);
  CHECK(Unlocked(s, *FindReward("master-pilot")));
  for (const Reward& r : Rewards()) CHECK(!r.name.empty() && !r.how.empty() && FindReward(r.id) == &r);
}

void SaveFile() {
  State s;
  FillBounties(s);
  Apply(s, Deadline(9'000'000, 170));
  Run custom;
  custom.mode = 2;
  custom.custom_id = "a7f6edd007fad798";
  custom.level = 3;
  custom.score = 7'000;
  custom.seconds = 25.5;
  custom.kills = {{kBlackHole, 3}, {kGate, 2}};
  Apply(s, custom);
  s.title = "rookie";
  const std::string text = Write(s);
  const auto back = Read(text);
  CHECK(back && *back == s);
  CHECK(!Read(""));
  CHECK(!Read("format = 2\n"));
  CHECK(!Read("this is not toml ["));
  // A damaged value is refused, not guessed.
  std::string damaged = text;
  const size_t at = damaged.find("xp = ");
  CHECK(at != std::string::npos);
  if (at != std::string::npos) {
    damaged.replace(at, 5, "xp = -");
    CHECK(!Read(damaged));
  }
  // Geoms survive the round trip, and a save from before geoms reads as none.
  State with_geoms = s;
  with_geoms.life.geoms = 4'321;
  with_geoms.life.modes[0].geoms = 321;
  const auto geoms_back = Read(Write(with_geoms));
  CHECK(geoms_back && *geoms_back == with_geoms);
  std::string older = Write(s);
  for (size_t at; (at = older.find("geoms = ")) != std::string::npos;) older.erase(at, older.find('\n', at) - at + 1);
  for (size_t at; (at = older.find(", geoms = 0")) != std::string::npos;) older.erase(at, 11);
  const auto older_back = Read(older);
  CHECK(older_back && older_back->life.geoms == 0);

  // A title that isn't a title is dropped.
  State t = s;
  t.title = "diamond";
  const auto dropped = Read(Write(t));
  CHECK(dropped && dropped->title.empty());
}

}  // namespace

int main() {
  Tiers();
  Levels();
  RunXp();
  Seeding();
  Bounties();
  RewardsUnlock();
  SaveFile();
  std::printf("progression_rules_tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
