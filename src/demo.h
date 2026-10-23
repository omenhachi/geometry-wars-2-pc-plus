#pragma once

// Replays (demo.cpp): every single-player run is recorded to <user_data>/demos, and a saved one can be watched. While
// one plays, Watching() is true and nothing of it counts: no score is kept or sent, and nothing unlocks or saves.

#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/ppc/context.h>

namespace gw2::demo {

// Where replays are saved: <user_data>/demos.
void SetFolder(std::filesystem::path folder);
std::filesystem::path Folder();

// True from the moment a replay starts until the game is back in its menus after it.
bool Watching();

enum class WatchResult { kStarted, kUnreadable, kNewer, kOtherVersion };
// Plays a saved replay: its mode starts from the menus on the next frame.
WatchResult Watch(const std::filesystem::path& path);

struct ReplayStatus {
  bool watching = false;
  uint32_t mode = 0;
  std::string player_name;
  uint32_t tick = 0;
  uint32_t ticks = 0;
  bool out_of_step = false;
  uint32_t out_of_step_tick = 0;
};
ReplayStatus Status();

// The game's events, from the hooks that wrap them.
// Before the round setup (sub_8203E538): a new recording, or the replay's starting state put back.
void BeforeRoundStart(PPCContext& ctx, uint8_t* base);
// Just before each simulation tick (sub_820366A0): its step and pads, recorded or played.
void BeforeSimTick(uint8_t* base);
void OnSimTick(uint8_t* base);
// After the frame's pad poll (sub_82038300) filled the slots, and on the game thread a requested replay starts.
void AfterPadPoll(uint8_t* base);
void StartRequested(PPCContext& ctx, uint8_t* base);
// Player::Fire, with the aim direction it is about to use.
void OnFire(uint8_t* base, uint32_t player, uint32_t direction);
// The run finished (run_stats_hooks.cpp): a recording is saved.
void OnRunEnd(const uint8_t* base);

// F9 watches the newest replay while gw2_replay_test_keys is on.
void RegisterKeys();

}  // namespace gw2::demo
