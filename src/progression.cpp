#include "progression.h"

#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

#include <rex/logging.h>

#include "cheats.h"
#include "custom_modes.h"
#include "demo.h"
#include "run_stats.h"

namespace gw2::progression {
namespace {

// How the round being played counts, taken as it starts.
struct Context {
  bool counts = true;              // not a replay, no cheats
  std::optional<uint32_t> board;   // official or Master: its leaderboard
  std::string custom_id;
  std::optional<int32_t> level;    // Sequence practice
};

std::mutex g_mutex;
std::filesystem::path g_file;
State g_state;
bool g_loaded = false;
Context g_context;
std::optional<Shown> g_shown;
uint32_t g_serial = 0;

bool AnyCheat() {
  using cheats::Cheat;
  return cheats::Enabled(Cheat::Invincible) || cheats::Enabled(Cheat::InfiniteShield) ||
         cheats::Enabled(Cheat::InfiniteBombs);
}

// Written whole to a temporary file, then put in place, so a crash never leaves half a save.
void Save() {
  if (g_file.empty()) return;
  std::error_code error;
  std::filesystem::create_directories(g_file.parent_path(), error);
  const std::filesystem::path temp = g_file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << Write(g_state);
    if (!out.flush()) {
      REXLOG_WARN("Progression: could not write {}", temp.string());
      return;
    }
  }
  std::filesystem::rename(temp, g_file, error);
  if (error) REXLOG_WARN("Progression: could not save {}: {}", g_file.string(), error.message());
}

}  // namespace

void Load(const std::filesystem::path& file) {
  std::lock_guard lock(g_mutex);
  g_file = file;
  std::error_code error;
  if (std::filesystem::exists(file, error)) {
    std::ifstream in(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (std::optional<State> read = Read(text)) {
      g_state = std::move(*read);
      REXLOG_INFO("Progression: level {}, {} XP, {} bounties open", LevelOf(g_state), g_state.xp,
                  g_state.bounties.size());
    } else {
      std::filesystem::rename(file, file.string() + ".corrupt", error);
      REXLOG_WARN("Progression: {} could not be read; kept as .corrupt and begun again", file.string());
    }
  } else {
    REXLOG_INFO("Progression: a new pilot (no {} yet)", file.filename().string());
  }
  FillBounties(g_state);
  g_loaded = true;
  Save();
}

void NoteKnownBest(uint32_t board, uint64_t score) {
  std::lock_guard lock(g_mutex);
  if (!g_loaded) return;
  if (const uint64_t xp = NoteBest(g_state, board, score)) {
    MarkAnnounced(g_state);  // what an earlier best unlocks is simply there, with no fanfare
    REXLOG_INFO("Progression: an earlier best of {} on board {} gives {} XP (level {})", score, board, xp,
                LevelOf(g_state));
    Save();
  }
}

void OnRoundStart() {
  Context context;
  context.counts = !demo::Watching() && !AnyCheat();
  if (const std::optional<custom_modes::Current> active = custom_modes::Active()) {
    if (active->board) {
      context.board = *active->board;
    } else {
      context.custom_id = active->id;
      context.level = active->mode.rules.level;
    }
  }
  std::lock_guard lock(g_mutex);
  g_context = std::move(context);
}

void OnRunEnd(const run_stats::Run& r, bool finished) {
  std::lock_guard lock(g_mutex);
  if (!g_loaded || !g_context.counts || AnyCheat()) return;  // a replay is known from its context, not from now
  Run run;
  run.mode = r.mode();
  run.board = g_context.board;
  if (!run.board && g_context.custom_id.empty() && r.mode() < kModes) run.board = r.mode();
  run.custom_id = g_context.custom_id;
  run.level = g_context.level;
  run.seconds = r.duration();
  run.score = uint64_t(std::max<int64_t>(r.final_score(), 0));
  for (const auto& [type, tally] : r.by_type()) run.kills[type] = tally.kills;
  run.best_blast = r.best_blast().kills;
  run.deaths = int32_t(r.deaths().size());
  run.bombs = int32_t(r.bombs().size());
  run.finished = finished;
  Outcome outcome = Apply(g_state, run);
  REXLOG_INFO("Progression: +{} XP (time {}, best {}, tiers {}, bounties {}), level {}{}", outcome.total(),
              outcome.time_xp, outcome.best_xp, outcome.tier_xp, outcome.bounty_xp, outcome.after.level,
              outcome.unlocked.empty() ? "" : ", something unlocked");
  g_shown = Shown{++g_serial, std::move(outcome)};
  g_context.counts = false;  // a run is counted once
  Save();
}

std::optional<Shown> LastOutcome() {
  std::lock_guard lock(g_mutex);
  return g_shown;
}

State Snapshot() {
  std::lock_guard lock(g_mutex);
  return g_state;
}

void RerollBounty(size_t index) {
  std::lock_guard lock(g_mutex);
  Reroll(g_state, index);
  Save();
}

void SetTitle(std::string_view id) {
  std::lock_guard lock(g_mutex);
  if (!id.empty()) {
    const Reward* reward = FindReward(id);
    if (!reward || reward->kind != RewardKind::kTitle || !Unlocked(g_state, *reward)) return;
  }
  g_state.title = std::string(id);
  Save();
}

}  // namespace gw2::progression
