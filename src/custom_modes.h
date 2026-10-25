#pragma once

// Custom modes: one of the game's six modes with its own rules (custom_mode_rules.h), started from the Custom modes
// list (custom_modes_menu.cpp). The game only knows its six modes, so a custom mode is its base mode's round with
// changes: its setup function's settings, rewritten as it runs, and hooks that hold the rest (gates, speed, score).
//
// A custom run never counts officially: nothing is sent to the online boards, kept on the local boards, unlocked or
// saved to the profile (demo::HowRunCounts), except that a Master mode's score goes to its own board. Its best is kept per mode, by the mode's id. It ends when the game is
// back in its menus after its round (or the Single Player screen starts one), so the next run is always the game's own.
//
// Modes come from two folders: the examples beside the executable (assets/modes, replaced by updates) and the
// player's own (user_data/mods). Each mode is a folder with a mode.toml in it.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <rex/ppc/context.h>

#include "custom_mode_rules.h"

namespace gw2::custom_modes {

inline constexpr char kModeFile[] = "mode.toml";

void SetFolders(std::filesystem::path built_in, std::filesystem::path user);
std::filesystem::path UserFolder();

struct Entry {
  std::string folder;  // the folder's name
  bool built_in = false;
  std::optional<custom_mode::Mode> mode;
  std::string id;
  std::vector<custom_mode::Message> errors;
  std::vector<custom_mode::Message> warnings;
  bool newer_format = false;
  std::optional<uint64_t> best;
};

// Every mode, the player's own first, each folder in name order. Read fresh each time.
std::vector<Entry> Scan();

// Starts `mode` from the menus, on the game thread's next pad poll.
void Play(const custom_mode::Mode& mode);

// The Master modes: PC Plus's own modes, with their own leaderboards (boards.h). Their rules are here, not in a mode
// file, so a score on their board was made by these rules; they are frozen, and a change would be a new board.
struct Master {
  uint32_t board;
  custom_mode::Mode mode;
};
const std::vector<Master>& Masters();
// The Master mode of a base mode (0 Deadline, 1 Evolved), or none.
const Master* MasterFor(uint32_t base);
void PlayMaster(const Master& master);

// The board a Master mode's score goes to, and its base mode (whose score write it replaces).
struct Board {
  uint32_t base;
  uint32_t board;
};
std::optional<Board> OwnBoard();

struct Current {
  custom_mode::Mode mode;
  std::string id;
  std::optional<uint32_t> board;  // a Master mode: its own board
  bool replay = false;      // a replay of a custom run is playing
  uint32_t round = 0;       // counts the rounds begun in this mode, for the title card
  double elapsed = 0;       // game seconds in play this round
  bool run_over = false;    // the final life is lost or the time is up
  bool score_written = false;  // the game has sent this round's score (and it was kept from the boards)
  size_t next_bonus = 0;       // the next of the rules' bonus_bombs_at this round
};
std::optional<Current> Active();
bool IsActive();
bool InfiniteBombs();
// The rules set the ships in all, so a mode with unlimited lives of its own (Deadline) ends on its last death too.
bool LivesLimited();
// The game simulated a tick a moment ago: a round is in play, not paused and not in the menus.
bool Ticking();

// A replay starts: its custom mode, or none for an official run. Called by demo.cpp as it starts one.
void ForReplay(std::optional<custom_mode::Mode> mode);

// Formats a score of this mode: points, or a time.
std::string ScoreText(const custom_mode::Rules& rules, uint64_t score);

// Hooks (custom_modes.cpp, frame_limiter.cpp, run_stats_hooks.cpp, demo.cpp).
void StartRequested(PPCContext& ctx, uint8_t* base);
// Every frame (the pad poll): a custom mode ends once the game is back in its menus after its round and has sent that
// round's score (the Game Over screen is a menu, and the score is sent from it); and while play
// doesn't count (a custom mode or a replay), the profile's unlocks and unlock progress are held as they were before it
// began, so nothing unlocks from it.
void EachFrame(uint8_t* base);
void BeforeSimTick(uint8_t* base);
void AfterSimTick(uint8_t* base);
void OnRoundStart(uint8_t* base);
void OnRunEnd(const uint8_t* base);
// The game sent a finished round's score (XSessionWriteStats, live_fix.cpp), which was not kept or sent.
void OnScoreWritten();
// The game's kill award (sub_820408D0, player_rumble_hooks.cpp) has run for player record `index`, which had `lives`
// and `bombs` before it: the only path that gives extra lives and bombs, so rules without them take them back here.
void AfterKillAward(uint8_t* base, uint32_t index, uint32_t lives, uint32_t bombs);

void RegisterKeys();

}  // namespace gw2::custom_modes
