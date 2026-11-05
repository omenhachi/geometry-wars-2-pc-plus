#include "camera_view.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "camera_rules.h"
#include "custom_modes.h"
#include "guest_memory.h"
#include "player_records.h"

REXCVAR_DEFINE_STRING(gw2_camera, "classic", "Geometry Wars", "The view of a round: classic (top-down) or chase");
REXCVAR_DEFINE_STRING(gw2_dev_camera, "", "Geometry Wars", "Camera experiments (log: the camera and Chase's numbers)");
REXCVAR_DEFINE_STRING(gw2_dev_chase_turn, "", "Geometry Wars",
                      "Chase turning, to try: arena (faces the arena's middle), ship (turns with the ship) or look "
                      "(the mouse turns it; W goes where you look, shots go ahead)");
REXCVAR_DEFINE_INT32(gw2_dev_look_speed, 40, "Geometry Wars", "Look mode: turn per mouse pixel, in 1/10000 radian");
REXCVAR_DEFINE_BOOL(gw2_dev_look_mouse, true, "Geometry Wars",
                    "Look mode: the mouse turns the view (false: never hold the cursor, for automated tests)");
REXCVAR_DEFINE_STRING(gw2_dev_secret_view, "", "Geometry Wars",
                      "Testing: start in a secret view (chase or first), as if the Konami code had been entered");
REXCVAR_DEFINE_INT32(gw2_dev_chase_behind, 0, "Geometry Wars", "Chase tuning for testing: eye distance behind (0: as shipped)");
REXCVAR_DEFINE_INT32(gw2_dev_chase_height, 0, "Geometry Wars", "Chase tuning for testing: eye height (0: as shipped)");
REXCVAR_DEFINE_INT32(gw2_dev_chase_ahead, 0, "Geometry Wars", "Chase tuning for testing: look-ahead (0: as shipped)");

namespace gw2::camera_view {
namespace {

namespace cam = gw2::camera;

constexpr uint32_t kStartState = 0x821993F0;  // 2 while a round is in play (custom_modes.cpp)
constexpr uint32_t kPlaying = 2;
constexpr float kBlendSeconds = 0.4f;   // the glide into and out of Chase
constexpr float kFollowStiffness = 9.0f;  // how quickly the eye catches up with the ship
// A move this far in one frame is a respawn, not flying: the eye goes straight there instead of swooping across.
constexpr float kJumpDistance = 120.0f;
// Turning: how quickly the camera eases round, and its top speed, so even a half turn takes about a second.
constexpr float kTurnEase = 3.0f;
constexpr float kTurnMaxRate = 3.2f;
// Turning with the ship only follows its heading while it is moving; parked, the camera stays put.
constexpr float kTurnMinSpeed = 40.0f;

std::atomic<float> g_player_x{0.0f};
std::atomic<float> g_player_y{0.0f};
std::atomic<float> g_heading{0.0f};
std::atomic<float> g_speed{0.0f};
// Screen up as an arena direction, for turning the sticks (TurnSticks); (0, -1) when the game's own camera shows.
std::atomic<float> g_up_x{0.0f};
std::atomic<float> g_up_y{-1.0f};
std::atomic<bool> g_chase_on{false};  // Chase shows, at least partly: the sticks go through the latches
// Look mode: the camera's yaw is the player's own, turned by the mouse (AddLook), and it is shown (nearly) in full.
std::atomic<float> g_look_yaw{-1.5707963f};
std::atomic<bool> g_look_on{false};

// Every enemy is linked into one list as its base constructor (sub_82047030) runs: a circular, doubly linked list
// whose head is here, each node {object, next, previous}; an enemy's position is +4, +8 as a player's is, its type
// +68, and +36 bit 0 marks it removed.
constexpr uint32_t kEnemyList = 0x8218139C;
constexpr uint32_t kEnemyTypeOffset = 68;
constexpr int32_t kGateType = 24;
constexpr uint32_t kMaxEnemies = 1024;
std::mutex g_blips_mutex;
std::vector<Blip> g_blips;

void GatherEnemies(const uint8_t* base) {
  std::vector<Blip> blips;
  uint32_t node = LoadBE32(base, kEnemyList + 4);
  for (uint32_t i = 0; node != 0 && node != kEnemyList && i < kMaxEnemies; ++i) {
    const uint32_t object = LoadBE32(base, node);
    if (object != 0 && !(LoadBE32(base, object + players::kFlagsOffset) & players::kRemovedBit) &&
        int32_t(LoadBE32(base, object + kEnemyTypeOffset)) != kGateType) {
      blips.push_back({std::bit_cast<float>(LoadBE32(base, object + 4)), std::bit_cast<float>(LoadBE32(base, object + 8))});
    }
    node = LoadBE32(base, node + 4);
  }
  std::lock_guard lock(g_blips_mutex);
  g_blips = std::move(blips);
}
// One latch per stick (left, right), used only by the input hook's thread.
cam::StickLatch g_latches[2];

// Only the game's render thread builds the gameplay camera, but keep it simple and safe.
std::mutex g_mutex;
float g_blend = 0.0f;  // 0: the game's own camera, 1: Chase
cam::Follow g_follow;
cam::YawFollow g_yaw;
float g_last_x = 0.0f, g_last_y = 0.0f;
std::chrono::steady_clock::time_point g_last_frame{};
std::chrono::steady_clock::time_point g_last_log{};

// The secret view, chosen with the Konami code on the pause screen, for this session only.
std::atomic<uint8_t> g_secret{uint8_t(cam::SecretView::kClassic)};
std::atomic<int64_t> g_secret_changed_ms{-100000};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

cam::SecretView Secret() {
  static const bool from_cvar = [] {
    const std::string start = REXCVAR_GET(gw2_dev_secret_view);
    if (start == "chase") g_secret.store(uint8_t(cam::SecretView::kChase));
    if (start == "first") g_secret.store(uint8_t(cam::SecretView::kFirstPerson));
    return true;
  }();
  (void)from_cvar;
  return cam::SecretView(g_secret.load(std::memory_order_relaxed));
}

bool LookChosen() {
  return REXCVAR_GET(gw2_dev_chase_turn) == "look" || Secret() != cam::SecretView::kClassic;
}

cam::ChaseTuning Tuning() {
  cam::ChaseTuning t;
  // Look mode sits low and close behind the ship, so what's near is big and the far arena stays out of sight. First
  // person sits just above the ship itself, looking ahead.
  if (LookChosen()) t = {150.0f, 220.0f, 40.0f};
  if (Secret() == cam::SecretView::kFirstPerson) t = {0.0f, 28.0f, 260.0f};
  if (const int v = REXCVAR_GET(gw2_dev_chase_behind)) t.behind = float(v);
  if (const int v = REXCVAR_GET(gw2_dev_chase_height)) t.height = float(v);
  if (const int v = REXCVAR_GET(gw2_dev_chase_ahead)) t.ahead = float(v);
  return t;
}

bool ChaseChosen() {
  return REXCVAR_GET(gw2_camera) == "chase" || REXCVAR_GET(gw2_dev_camera) == "chase" ||
         Secret() != cam::SecretView::kClassic;
}

// The Konami code, read from new presses while a round is paused.
cam::KonamiCode g_code;
uint16_t g_last_buttons = 0;
bool g_last_up = false, g_last_down = false, g_last_left = false, g_last_right = false;
// Look mode on a controller: what the triggers asked for this poll (BeforeTurn), applied after the sticks turn.
bool g_pad_fire = false, g_pad_bomb = false;
int64_t g_last_pad_ms = 0;
constexpr int32_t kStickPress = 16000;    // a stick flick that counts as a press for the code
constexpr int32_t kRightDeadZone = 8689;  // XInput's own for the right stick
constexpr float kPadTurnRate = 3.0f;      // radians a second, with the right stick all the way over
constexpr uint8_t kTriggerPress = 64;

void LogNumbers(const cam::Pose& pose, const aim::Mat4& view, float ship_x, float ship_y) {
  const auto now = std::chrono::steady_clock::now();
  if (now - g_last_log < std::chrono::seconds(1)) return;
  g_last_log = now;
  const auto proj = aim::GameplayProjection();
  if (!proj) return;
  // How much floor shows behind the ship and ahead of it: how far from the ship the bottom and top edges of the
  // screen meet the floor, in the middle of the screen.
  const auto ship_ndc = aim::ProjectToNdc(view, *proj, ship_x, ship_y, 0.0f);
  const auto bottom = aim::UnprojectToArena(view, *proj, {0.0f, -1.0f});
  const auto top = aim::UnprojectToArenaBelowHorizon(view, *proj, {0.0f, 1.0f});
  REXLOG_INFO("Camera: blend {:.2f} eye ({:.0f}, {:.0f}, {:.0f}) ship ({:.0f}, {:.0f}) at screen y {:.2f}; floor behind "
              "{:.0f}, ahead {:.0f}; fov y {:.1f} deg",
              g_blend, pose.eye.x, pose.eye.y, pose.eye.z, ship_x, ship_y, ship_ndc ? ship_ndc->y : 9.0f,
              bottom ? std::hypot(bottom->x - ship_x, bottom->y - ship_y) : -1.0f,
              top ? std::hypot(top->x - ship_x, top->y - ship_y) : -1.0f,
              2.0 * std::atan(1.0 / (*proj)[5]) * 57.29578);
}

}  // namespace

void ObservePlayer(float x, float y, float heading, float vx, float vy) {
  g_player_x.store(x, std::memory_order_relaxed);
  g_player_y.store(y, std::memory_order_relaxed);
  g_heading.store(heading, std::memory_order_relaxed);
  g_speed.store(std::hypot(vx, vy), std::memory_order_relaxed);
}

bool LookActive() {
  return g_look_on.load(std::memory_order_relaxed);
}

uint8_t SecretViewNow() {
  return uint8_t(Secret());
}

int64_t SecretChangedMs() {
  return NowMs() - g_secret_changed_ms.load(std::memory_order_relaxed);
}

void BeforeTurn(uint8_t* base, uint32_t pad) {
  const uint16_t buttons = LoadBE16(base, pad);
  // The d-pad or either stick: on the keyboard WASD is the left stick and the arrow keys are the right.
  const int16_t lx = int16_t(LoadBE16(base, pad + 4)), ly = int16_t(LoadBE16(base, pad + 6));
  const int16_t sx = int16_t(LoadBE16(base, pad + 8)), sy = int16_t(LoadBE16(base, pad + 10));
  const bool up = (buttons & 0x0001) || ly > kStickPress || sy > kStickPress;
  const bool down = (buttons & 0x0002) || ly < -kStickPress || sy < -kStickPress;
  const bool left = (buttons & 0x0004) || lx < -kStickPress || sx < -kStickPress;
  const bool right = (buttons & 0x0008) || lx > kStickPress || sx > kStickPress;
  const uint16_t pressed = uint16_t(buttons & ~g_last_buttons);
  const bool new_up = up && !g_last_up, new_down = down && !g_last_down;
  const bool new_left = left && !g_last_left, new_right = right && !g_last_right;
  g_last_buttons = buttons;
  g_last_up = up;
  g_last_down = down;
  g_last_left = left;
  g_last_right = right;

  // The code, only on the pause screen: a round is on and the game isn't ticking.
  const bool paused = LoadBE32(base, kStartState) == kPlaying && !custom_modes::Ticking();
  if (paused) {
    std::optional<cam::Press> press;
    if (new_up) press = cam::Press::kUp;
    else if (new_down) press = cam::Press::kDown;
    else if (new_left) press = cam::Press::kLeft;
    else if (new_right) press = cam::Press::kRight;
    else if (pressed & 0x4000) press = cam::Press::kX;
    else if (pressed & 0x8000) press = cam::Press::kY;
    else if (pressed) press = cam::Press::kOther;
    if (press && g_code.Feed(*press, double(NowMs()) / 1000.0)) {
      const cam::SecretView next = cam::NextView(Secret());
      g_secret.store(uint8_t(next), std::memory_order_relaxed);
      g_secret_changed_ms.store(NowMs(), std::memory_order_relaxed);
      REXLOG_INFO("Camera: the Konami code: {}", next == cam::SecretView::kChase       ? "chase camera"
                                                 : next == cam::SecretView::kFirstPerson ? "first person"
                                                                                         : "classic view");
    }
  }

  // Look mode on a controller: the right stick turns the view, RT fires ahead and LT bombs.
  const int64_t now = NowMs();
  const float dt = std::clamp(float(now - g_last_pad_ms) / 1000.0f, 0.0f, 0.1f);
  g_last_pad_ms = now;
  g_pad_fire = g_pad_bomb = false;
  if (!g_look_on.load(std::memory_order_relaxed)) return;
  const int16_t rx = int16_t(LoadBE16(base, pad + 8));
  if (std::abs(int32_t(rx)) > kRightDeadZone) AddLook(float(rx) / 32767.0f * kPadTurnRate * dt);
  g_pad_fire = base[pad + 3] > kTriggerPress;
  g_pad_bomb = base[pad + 2] > kTriggerPress;
  StoreBE<int16_t>(base, pad + 8, 0);
  StoreBE<int16_t>(base, pad + 10, 0);
  base[pad + 2] = 0;
  base[pad + 3] = 0;
}

void AfterTurn(uint8_t* base, uint32_t pad) {
  if (!g_look_on.load(std::memory_order_relaxed)) return;
  if (g_pad_fire) {  // straight ahead, as the arena has it (the sticks were turned already)
    const float yaw = LookYaw();
    StoreBE<int16_t>(base, pad + 8, int16_t(std::lround(std::cos(yaw) * 32767.0f)));
    StoreBE<int16_t>(base, pad + 10, int16_t(std::lround(-std::sin(yaw) * 32767.0f)));
  }
  if (g_pad_bomb) base[pad + 3] = 255;  // the game bombs on the right trigger
}

std::vector<Blip> Enemies() {
  std::lock_guard lock(g_blips_mutex);
  return g_blips;
}

void AddLook(float radians) {
  g_look_yaw.store(g_look_yaw.load(std::memory_order_relaxed) + radians, std::memory_order_relaxed);
}

bool LookHoldsMouse() {
  return REXCVAR_GET(gw2_dev_look_mouse);
}

float LookPerPixel() {
  return float(REXCVAR_GET(gw2_dev_look_speed)) / 10000.0f;
}

float LookYaw() {
  return g_look_yaw.load(std::memory_order_relaxed);
}

void TurnSticks(uint8_t* base, uint32_t pad) {
  if (!g_chase_on.load(std::memory_order_relaxed)) {
    g_latches[0].held = g_latches[1].held = false;
    return;
  }
  const float ux = g_up_x.load(std::memory_order_relaxed);
  const float uy = g_up_y.load(std::memory_order_relaxed);
  // Look mode turns only when the player turns it, so a held key follows the view as it turns: no latch.
  const bool look = g_look_on.load(std::memory_order_relaxed);
  // The pad after its buttons and triggers: left stick x, y, right stick x, y (big-endian int16, y up).
  for (int i = 0; i < 2; ++i) {
    const uint32_t at = pad + 4 + uint32_t(i) * 4;
    const int16_t sx = int16_t(LoadBE16(base, at)), sy = int16_t(LoadBE16(base, at + 2));
    const auto turned = look ? cam::TurnStick(sx, sy, ux, uy) : g_latches[i].Apply(sx, sy, ux, uy);
    StoreBE<int16_t>(base, at, turned.x);
    StoreBE<int16_t>(base, at + 2, turned.y);
  }
}

void OnGameplayView(uint8_t* base, uint32_t out, const aim::Mat4& world) {
  const std::string dev = REXCVAR_GET(gw2_dev_camera);
  std::lock_guard lock(g_mutex);
  const auto now = std::chrono::steady_clock::now();
  const float dt = g_last_frame == std::chrono::steady_clock::time_point{}
                       ? 0.0f
                       : std::chrono::duration<float>(now - g_last_frame).count();
  g_last_frame = now;

  // Chase only while a round is in play with one player; menus, Game Over and multiplayer keep the game's camera.
  const bool playing = LoadBE32(base, kStartState) == kPlaying &&
                       LoadBE32(base, players::kPlayerCountAddress) == 1;
  const float target = ChaseChosen() && playing ? 1.0f : 0.0f;
  g_blend = cam::StepBlend(g_blend, target, std::clamp(dt, 0.0f, 0.1f), kBlendSeconds);
  if (g_blend <= 0.0f) {
    g_follow.placed = false;  // the next Chase starts on the ship, not where the last one ended
    g_yaw.yaw = cam::kFacingUp;
    g_look_yaw.store(cam::kFacingUp, std::memory_order_relaxed);  // each round starts facing up the arena
    g_chase_on.store(false, std::memory_order_relaxed);
    g_look_on.store(false, std::memory_order_relaxed);
    return;  // the game's own camera, untouched
  }

  const float x = g_player_x.load(std::memory_order_relaxed);
  const float y = g_player_y.load(std::memory_order_relaxed);
  const float jump = std::hypot(x - g_last_x, y - g_last_y);
  g_last_x = x;
  g_last_y = y;
  if (jump > kJumpDistance) {
    g_follow.Snap(x, y);
  } else {
    g_follow.Step(x, y, dt, kFollowStiffness);
  }

  // Which way it faces: up the arena, into the arena, or the ship's way.
  const std::string turn = LookChosen() ? std::string("look") : REXCVAR_GET(gw2_dev_chase_turn);
  if (turn == "look") {
    g_yaw.yaw = g_look_yaw.load(std::memory_order_relaxed);
  } else if (turn == "arena") {
    if (const auto yaw = cam::FaceArenaYaw(g_follow.x, g_follow.y)) g_yaw.Step(*yaw, dt, kTurnEase, kTurnMaxRate);
  } else if (turn == "ship") {
    if (g_speed.load(std::memory_order_relaxed) > kTurnMinSpeed) {
      g_yaw.Step(g_heading.load(std::memory_order_relaxed), dt, kTurnEase, kTurnMaxRate);
    }
  } else {
    g_yaw.Step(cam::kFacingUp, dt, kTurnEase, kTurnMaxRate);
  }

  const cam::Pose chase = cam::ChasePose(g_follow.x, g_follow.y, Tuning(), g_yaw.yaw);
  const cam::Pose pose = cam::Blend(cam::PoseFromWorld(world), chase, g_blend);
  const cam::Mat4 view = cam::ViewFrom(pose);
  // Screen up on the arena is the view's up row (column 1 of the view), flattened onto the floor.
  const float up_x = view[1], up_y = view[5];
  const float up_length = std::hypot(up_x, up_y);
  if (up_length > 1e-4f) {
    g_up_x.store(up_x / up_length, std::memory_order_relaxed);
    g_up_y.store(up_y / up_length, std::memory_order_relaxed);
  }
  g_chase_on.store(true, std::memory_order_relaxed);
  g_look_on.store(turn == "look" && g_blend > 0.5f, std::memory_order_relaxed);
  if (turn == "look") GatherEnemies(base);
  for (uint32_t i = 0; i < 16; ++i) StoreBE32(base, out + i * 4, std::bit_cast<uint32_t>(view[i]));
  if (dev == "log") LogNumbers(pose, view, x, y);
}

}  // namespace gw2::camera_view
