#pragma once

// Pilot progression in the running game (progression_rules.h): the save file, how each round counts, and what the
// results screen, the menus and the online boards show of it.
//
// A round's context (official, Master, custom or practice; a replay; cheats) is taken as it starts, because a run
// quit from the pause menu is only reported when the next round starts. Replays and runs with cheats earn nothing.

#include <cstdint>
#include <filesystem>
#include <optional>

#include "progression_rules.h"

namespace gw2::run_stats {
class Run;
}

namespace gw2::progression {

// <user_data>/progression.toml; a damaged file is moved aside to .corrupt and a new one begun.
void Load(const std::filesystem::path& file);

// A best known from elsewhere: the local stats file as it loads, or the player's own row on an online board. Its
// tiers' XP, silently.
void NoteKnownBest(uint32_t board, uint64_t score);

// Hooks (run_stats_hooks.cpp): a round starts, and a run ends (`finished`: in play, not quit from the pause menu).
void OnRoundStart();
void OnRunEnd(const run_stats::Run& run, bool finished);

// The last run's outcome, for the results screen, with a number that changes each run.
struct Shown {
  uint32_t serial = 0;
  Outcome outcome;
};
std::optional<Shown> LastOutcome();

State Snapshot();
void RerollBounty(size_t index);
// Wears a title, or none with an empty id; only an unlocked title.
void SetTitle(std::string_view id);

}  // namespace gw2::progression
