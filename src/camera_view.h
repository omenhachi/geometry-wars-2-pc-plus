#pragma once

// The Chase camera (gw2_camera = "chase"): the gameplay camera's view matrix replaced as the game builds it, by one
// behind and above the ship looking up the arena (camera_rules.h). It glides in as a round starts and out at Game
// Over, and only plays with one player. It is only a view: the game, its input and replays are unchanged, and with
// "classic" the game's own matrix is never touched. gw2_dev_camera = "log" writes Chase's numbers once a second.

#include <cstdint>
#include <vector>

#include "aim_camera.h"

namespace gw2::camera_view {

// After sub_8206A1C8 has written the gameplay camera's view matrix to `out` from its world matrix `world`.
void OnGameplayView(uint8_t* base, uint32_t out, const aim::Mat4& world);

// Turns the pad's sticks (XamInputGetState's gamepad at `pad`) so that up is up the screen while Chase faces another
// way, each stick latched to the facing it was pushed at until it is let go (camera_rules.h StickLatch). Facing up
// the arena nothing changes. The game records the turned sticks, so replays play the same.
void TurnSticks(uint8_t* base, uint32_t pad);

// Look mode (gw2_dev_chase_turn = "look"), a prototype: the mouse turns the view, W goes where you look and shots
// go straight ahead. Whether it is showing now; turn it; the radians a mouse pixel turns; its yaw (an arena angle,
// -pi/2 facing up the arena).
bool LookActive();
void AddLook(float radians);
float LookPerPixel();
// Whether look mode may hold the cursor (gw2_dev_look_mouse; automated tests turn it off).
bool LookHoldsMouse();
float LookYaw();

// The Konami code on the pause screen (Up Up Down Down Left Right Left Right X Y, on the d-pad or either stick; on
// the keyboard the arrow keys or WASD, then H L) cycles the secret views: the chase
// camera you turn yourself, first person, then classic. For this session only. BeforeTurn reads it, and in look mode
// takes the controller's right stick (turning the view) and triggers; AfterTurn, once the sticks are turned, fires
// ahead on RT and bombs on LT. Both on the pad of XamInputGetState's gamepad at `pad`.
void BeforeTurn(uint8_t* base, uint32_t pad);
void AfterTurn(uint8_t* base, uint32_t pad);
// The secret view now (0 classic, 1 chase, 2 first person), and how long ago it last changed, in milliseconds.
uint8_t SecretViewNow();
int64_t SecretChangedMs();

// Look mode's edge arrows: the enemies on the arena, gathered on the game's thread each frame look mode shows.
struct Blip {
  float x, y;
};
std::vector<Blip> Enemies();

// The local player, each frame of a round (cheat_hooks.cpp): its centre, its heading (Player +20, radians, eased by
// Player::Update towards the atan2 of its velocity) and its velocity (+12, +16).
void ObservePlayer(float x, float y, float heading, float vx, float vy);

}  // namespace gw2::camera_view
