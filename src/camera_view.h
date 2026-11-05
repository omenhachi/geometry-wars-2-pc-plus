#pragma once

// The Chase camera (gw2_camera = "chase"): the gameplay camera's view matrix replaced as the game builds it, by one
// behind and above the ship looking up the arena (camera_rules.h). It glides in as a round starts and out at Game
// Over, and only plays with one player. It is only a view: the game, its input and replays are unchanged, and with
// "classic" the game's own matrix is never touched. gw2_dev_camera = "log" writes Chase's numbers once a second.

#include <cstdint>

#include "aim_camera.h"

namespace gw2::camera_view {

// After sub_8206A1C8 has written the gameplay camera's view matrix to `out` from its world matrix `world`.
void OnGameplayView(uint8_t* base, uint32_t out, const aim::Mat4& world);

// Turns the pad's sticks (XamInputGetState's gamepad at `pad`) so that up is up the screen while Chase faces another
// way, each stick latched to the facing it was pushed at until it is let go (camera_rules.h StickLatch). Facing up
// the arena nothing changes. The game records the turned sticks, so replays play the same.
void TurnSticks(uint8_t* base, uint32_t pad);

// The local player, each frame of a round (cheat_hooks.cpp): its centre, its heading (Player +20, radians, eased by
// Player::Update towards the atan2 of its velocity) and its velocity (+12, +16).
void ObservePlayer(float x, float y, float heading, float vx, float vy);

}  // namespace gw2::camera_view
