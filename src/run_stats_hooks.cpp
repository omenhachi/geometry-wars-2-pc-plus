// Feeds the results screen's run from the game's own events. Single-player runs only: player 0's record.
//
// The run's clock is the round's elapsed time (a float the simulation advances, so pauses stop it), and its score is
// player 0's 64-bit score.

#include "run_stats_hooks.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <utility>

#include <rex/hook.h>
#include <rex/logging.h>

#include "custom_modes.h"
#include "deadline_rank.h"
#include "demo.h"
#include "guest_memory.h"
#include "king_records.h"
#include "player_records.h"
#include "run_stats.h"

namespace gw2::run {
namespace {

using gw2::LoadBE32;

constexpr uint32_t kElapsedAddress = 0x8219CE68;  // the round's elapsed seconds (float)
constexpr uint32_t kLimitAddress = 0x8219D258;    // the round's length in seconds, for timed modes (float)
constexpr uint32_t kScoreOffset = 40;             // the 64-bit score in a player's record
constexpr uint32_t kTypeOffset = 68;              // an object's type
constexpr uint32_t kPositionXOffset = 4;
constexpr uint32_t kPositionYOffset = 8;

float LoadF(const uint8_t* base, uint32_t at) {
  const uint32_t bits = LoadBE32(base, at);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}

float Elapsed(const uint8_t* base) {
  return LoadF(base, kElapsedAddress);
}

int64_t Score(const uint8_t* base) {
  return int64_t(gw2::LoadBE64(base, players::kFirstRecordAddress + kScoreOffset));
}

std::mutex g_mutex;
run_stats::Run g_run;
std::optional<run_stats::Run> g_results;  // the finished run, until the next round
std::chrono::steady_clock::time_point g_last_hud{};
std::chrono::steady_clock::time_point g_last_game_over{};
// Neither the HUD nor the Game Over screen drew for this long: the game has moved on.
constexpr auto kShownTimeout = std::chrono::milliseconds(400);
bool g_reported = false;
bool g_timer_logged = false;
std::set<std::pair<int32_t, int32_t>> g_first_kills;  // (type, killer type) pairs already logged this run

// `finished`: the run ended in play (its last life, or the timer), so its replay is kept.
void Report(const char* why, const uint8_t* base, bool finished) {
  if (g_reported || !g_run.active()) return;
  g_reported = true;
  g_run.Finish();
  g_results = g_run;
  if (finished) {
    custom_modes::OnRunEnd(base);
    demo::OnRunEnd(base);
  }
  REXLOG_INFO("Run stats ({}), mode {}:", why, g_run.mode());
  for (const std::string& line : g_run.Describe()) REXLOG_INFO("  {}", line);
}

bool LocalPlayer(const uint8_t* base, uint32_t player) {
  return players::IsPlayer(base, player) && LoadBE32(base, player + cheats::kPlayerIndexOffset) == 0 &&
         LoadBE32(base, players::kPlayerCountAddress) == 1;
}

}  // namespace

void OnRoundStart(const uint8_t* base) {
  std::lock_guard lock(g_mutex);
  Report("a new round started", base, false);
  const uint32_t mode = LoadBE32(base, king::kModeAddress);
  g_run.Start(mode, Elapsed(base));
  g_results.reset();
  g_reported = false;
  g_timer_logged = false;
  g_first_kills.clear();
  REXLOG_INFO("Run stats: round started, mode {}, players {}, elapsed {:.2f}, limit {:.2f}", mode,
              LoadBE32(base, players::kPlayerCountAddress), Elapsed(base), LoadF(base, kLimitAddress));
}

void OnKill(const uint8_t* base, uint32_t event, uint32_t enemy, uint32_t player, int64_t points) {
  if (!LocalPlayer(base, player)) return;
  const int32_t type = int32_t(LoadBE32(base, enemy + kTypeOffset));
  const uint32_t killer = event != 0 ? LoadBE32(base, event) : 0;
  const int32_t killer_type = killer != 0 ? int32_t(LoadBE32(base, killer + kTypeOffset)) : -1;
  const float time = Elapsed(base);
  std::lock_guard lock(g_mutex);
  g_run.Kill(time, type, killer_type, points, killer);
  if (g_first_kills.insert({type, killer_type}).second) {
    REXLOG_DEBUG("Run stats: first kill of type {} by type {} (cause {}) at {:.1f}s for {} points, at ({:.0f}, {:.0f})",
                type, killer_type, int32_t(LoadBE32(base, event + 8)), time, points,
                LoadF(base, enemy + kPositionXOffset), LoadF(base, enemy + kPositionYOffset));
  }
}

void OnDeath(const uint8_t* base, uint32_t player, bool final_life) {
  if (!LocalPlayer(base, player)) return;
  std::lock_guard lock(g_mutex);
  g_run.Death(Elapsed(base));
  // Deadline has unlimited lives, so every death there looks like the last: its runs end on the timer, unless a
  // custom mode sets its ships. Other modes can have a timer too (a custom mode's time limit), and their last death
  // still ends the run.
  if (final_life &&
      (LoadBE32(base, king::kModeAddress) != deadline_rank::kDeadlineMode || custom_modes::LivesLimited())) {
    REXLOG_INFO("Run stats: final life lost at {:.1f}s", Elapsed(base));
    Report("final life lost", base, true);
  }
}

void OnBomb(const uint8_t* base, uint32_t player) {
  if (!LocalPlayer(base, player)) return;
  std::lock_guard lock(g_mutex);
  g_run.Bomb(Elapsed(base));
}

void OnHud(const uint8_t* base) {
  if (LoadBE32(base, players::kPlayerCountAddress) != 1) return;
  const float elapsed = Elapsed(base), limit = LoadF(base, kLimitAddress);
  std::lock_guard lock(g_mutex);
  g_last_hud = std::chrono::steady_clock::now();
  g_run.Observe(elapsed, Score(base));
  if (!g_timer_logged && limit > 0 && elapsed >= limit) {
    g_timer_logged = true;
    REXLOG_INFO("Run stats: the round timer ran out at {:.1f}s", elapsed);
    Report("timer ran out", base, true);
  }
}

void OnScoreWritten(const uint8_t* base) {
  std::lock_guard lock(g_mutex);
  REXLOG_INFO("Run stats: score written, elapsed {:.1f}s, score {}", Elapsed(base), Score(base));
  Report("score written", base, true);
}

void OnGameOverScreen() {
  std::lock_guard lock(g_mutex);
  g_last_game_over = std::chrono::steady_clock::now();
}

std::optional<run_stats::Run> ResultsToShow() {
  std::lock_guard lock(g_mutex);
  const auto now = std::chrono::steady_clock::now();
  if (!g_results || (now - g_last_hud > kShownTimeout && now - g_last_game_over > kShownTimeout)) return std::nullopt;
  return g_results;
}

}  // namespace gw2::run

// The round setup: resets the round's state, including Deadline's rank (deadline_rank.h), before every mode's round.
REX_EXTERN(__imp__sub_8203E538);
REX_HOOK_RAW(sub_8203E538) {
  gw2::demo::BeforeRoundStart(ctx, base);
  __imp__sub_8203E538(ctx, base);
  gw2::custom_modes::OnRoundStart(base);
  gw2::run::OnRoundStart(base);
}

// The Game Over screen's body (Final Score, Previous Scores and Best This Session), every frame it shows: the results
// stay up with it. Its caller, sub_82089560, draws every menu screen's title, so it is not the signal.
REX_EXTERN(__imp__sub_82088AE0);
REX_HOOK_RAW(sub_82088AE0) {
  __imp__sub_82088AE0(ctx, base);
  gw2::run::OnGameOverScreen();
}
