#pragma once

// SPIKE: demo recording and playback, to find out whether a run replays exactly from its inputs. gw2_demo = "record"
// saves each single-player round's inputs to <user_data>/demos/last.gw2demo; "replay" plays that file back into the
// next round and logs the first tick where the game's state differs from the recording.

#include <cstdint>

#include <rex/ppc/context.h>

namespace gw2::demo {

// Before the round setup (sub_8203E538) runs: the random states are saved, or restored for playback.
void BeforeRoundStart(PPCContext& ctx, uint8_t* base);
// Just before each simulation tick: the step it will take (set by the frame pacer from real time) is recorded, or
// replaced with the recorded one.
void BeforeSimTick(uint8_t* base);
// After each simulation tick (sub_820366A0).
void OnSimTick(uint8_t* base);
// After the frame's pad poll (sub_82038300) filled the slots.
void AfterPadPoll(uint8_t* base);
// On the game thread each frame: starts a requested demo's mode.
void StartWatching(PPCContext& ctx, uint8_t* base);
// Player::Fire, with the aim direction it is about to use.
void OnFire(uint8_t* base, uint32_t player, uint32_t direction);
// A draw from the Mersenne Twister, by the function at `caller`.
void OnTwisterDraw(uint32_t caller);
// SPIKE: F9 starts the demo's mode from the menus, as the Single Player screen would (replay mode only).
void RegisterKeys();
// The run finished (run_stats_hooks.cpp).
void OnRunEnd();

}  // namespace gw2::demo
