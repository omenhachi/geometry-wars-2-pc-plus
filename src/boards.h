#pragma once

// The leaderboards PC Plus keeps: one per game mode (its view, the mode's id), then one per Master mode
// (custom_modes.h), which are PC Plus's own. The server (server/leaderboard/src/index.js) keeps the same numbers.

#include <cstdint>
#include <optional>

namespace gw2::boards {

inline constexpr uint32_t kGameBoards = 6;             // 0 Deadline .. 5 King, the game's own
inline constexpr uint32_t kMasterDeadlineBoard = 6;
inline constexpr uint32_t kMasterEvolvedBoard = 7;
inline constexpr uint32_t kBoards = 8;

// How the run being played counts (demo::HowRunCounts):
//  - kOfficial: the game's own mode, on its own board, with achievements, unlocks and the profile;
//  - kOwnBoard: a Master mode: its score goes to its own board only, and nothing else of it is kept;
//  - kNone: a replay or a custom mode: nothing of it is kept, sent, unlocked or saved.
enum class Counting { kOfficial, kOwnBoard, kNone };

// Where the game's score write for `view` goes: officially the view itself; for a Master mode on base mode `base`, its
// own `board`, and only for the base mode's view; otherwise nowhere.
constexpr std::optional<uint32_t> ScoreBoard(Counting counting, uint32_t view, uint32_t base, uint32_t board) {
  switch (counting) {
    case Counting::kOfficial:
      return view;
    case Counting::kOwnBoard:
      if (view == base) return board;
      return std::nullopt;
    case Counting::kNone:
      return std::nullopt;
  }
  return std::nullopt;
}

// Only the game's own modes unlock achievements, move unlock progress and save to the profile.
constexpr bool KeepsProgress(Counting counting) {
  return counting == Counting::kOfficial;
}

}  // namespace gw2::boards
