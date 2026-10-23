#pragma once

// The game events that feed the results screen's run (run_stats.h), called from the hooks that already wrap them.

#include <cstdint>
#include <optional>

#include "run_stats.h"

namespace gw2::run {

// sub_8203E538, the round setup.
void OnRoundStart(const uint8_t* base);
// The kill award (sub_820408D0): the event and the enemy it was called with, and the score it added for `player`.
void OnKill(const uint8_t* base, uint32_t event, uint32_t enemy, uint32_t player, int64_t points);
void OnDeath(const uint8_t* base, uint32_t player, bool final_life);
void OnBomb(const uint8_t* base, uint32_t player);
// Every in-game HUD frame.
void OnHud(const uint8_t* base);
// The game sent a finished run's score (XSessionWriteStats).
void OnScoreWritten(const uint8_t* base);

// Every frame of the Game Over screen.
void OnGameOverScreen();

// The run that just finished, while the game still shows its end: the HUD, then the Game Over screen.
std::optional<run_stats::Run> ResultsToShow();

}  // namespace gw2::run
