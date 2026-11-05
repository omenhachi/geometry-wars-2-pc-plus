#pragma once

// The Chase camera's maths (camera_view.cpp): where the eye sits, how it glides between the game's own camera and
// Chase, and the view matrix it makes. Pure, so it is unit tested.
//
// The game's own gameplay camera (logged) sits 512 above the arena floor (z = 0) looking straight down: +x to the
// right, +y down the screen. Its world matrix has the rows right, up, forward and position (D3D: row vectors,
// left-handed, forward = right x up), and the view is that matrix inverted.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gw2::camera {

using Mat4 = std::array<float, 16>;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 Normalize(Vec3 a) {
  const float length = std::sqrt(Dot(a, a));
  return length > 1e-9f ? a * (1.0f / length) : a;
}
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// Where a camera is and which way it looks; `up` is a hint, made square to `forward` by ViewFrom.
struct Pose {
  Vec3 eye;
  Vec3 forward;
  Vec3 up;
};

// The pose in a camera's world matrix.
inline Pose PoseFromWorld(const Mat4& w) {
  return {{w[12], w[13], w[14]}, Normalize({w[8], w[9], w[10]}), Normalize({w[4], w[5], w[6]})};
}

// The view matrix for a pose: right = up x forward, then up made square again, so right x up = forward as the
// game's own matrices have it.
inline Mat4 ViewFrom(const Pose& pose) {
  const Vec3 f = Normalize(pose.forward);
  const Vec3 r = Normalize(Cross(pose.up, f));
  const Vec3 u = Cross(f, r);
  return {r.x, u.x, f.x, 0.0f,  //
          r.y, u.y, f.y, 0.0f,  //
          r.z, u.z, f.z, 0.0f,  //
          -Dot(pose.eye, r), -Dot(pose.eye, u), -Dot(pose.eye, f), 1.0f};
}

// Chase: behind the ship (down the screen) and above it, looking up the arena past it. Always facing the same way as
// the game's own camera does, so the sticks keep their meaning.
// Measured with the game's own 36 degree field of view: about 170 of floor shows behind the ship and 350 ahead (the
// arena is about 600 by 400), with the ship just below the middle of the screen.
struct ChaseTuning {
  float behind = 300.0f;  // eye distance behind the ship
  float height = 560.0f;  // eye height above the floor
  float ahead = 40.0f;    // the floor point it looks at, ahead of the ship
};

// `yaw` is the way the camera faces across the floor, as an arena angle (atan2(y, x), +y down the screen): -pi/2
// faces up the arena, as the game's own camera does.
inline constexpr float kFacingUp = -1.5707963f;

inline Pose ChasePose(float ship_x, float ship_y, const ChaseTuning& t, float yaw = kFacingUp) {
  const float dx = std::cos(yaw), dy = std::sin(yaw);
  const Vec3 eye{ship_x - dx * t.behind, ship_y - dy * t.behind, t.height};
  const Vec3 target{ship_x + dx * t.ahead, ship_y + dy * t.ahead, 0.0f};
  const Vec3 forward = Normalize(target - eye);
  // "Up" on screen is the way it faces, tipped back to be square to the view.
  return {eye, forward, {dx, dy, 0.0f}};
}

// The shortest turn from angle a to angle b, in -pi..pi.
inline float AngleDelta(float a, float b) {
  float d = std::fmod(b - a, 6.2831853f);
  if (d > 3.1415927f) d -= 6.2831853f;
  if (d < -3.1415927f) d += 6.2831853f;
  return d;
}

// A yaw that turns towards a target the short way round: quickly at first, then easing in, never faster than
// `max_rate` (radians a second), so even a half turn is a swing, not a snap.
struct YawFollow {
  float yaw = kFacingUp;

  void Step(float target, float dt, float ease_per_second, float max_rate) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float d = AngleDelta(yaw, target);
    float step = d * std::min(1.0f, ease_per_second * dt);
    const float limit = max_rate * dt;
    step = std::clamp(step, -limit, limit);
    yaw += step;
  }
};

// Facing into the arena: the way from the ship to the arena's centre. Close to the centre that way is unsteady, so
// there the camera keeps the way it faces (nullopt).
inline constexpr float kCentreDeadZone = 70.0f;
inline std::optional<float> FaceArenaYaw(float ship_x, float ship_y) {
  if (std::hypot(ship_x, ship_y) < kCentreDeadZone) return std::nullopt;
  return std::atan2(-ship_y, -ship_x);
}

// A stick as the player meant it on screen (y up), turned so that up is the way the camera faces on the arena
// (screen_up, a unit vector, +y down the screen). Facing up the arena (0, -1) leaves it as it is.
struct StickXY {
  int16_t x;
  int16_t y;
};
inline StickXY TurnStick(int16_t sx, int16_t sy, float up_x, float up_y) {
  // Screen right is screen up turned a quarter clockwise on the arena: (x, y) -> (-y, x).
  const float right_x = -up_y, right_y = up_x;
  const float ax = float(sx) * right_x + float(sy) * up_x;  // the arena direction, +y down
  const float ay = float(sx) * right_y + float(sy) * up_y;
  const auto to16 = [](float v) { return int16_t(std::clamp(std::lround(v), -32768l, 32767l)); };
  return {to16(ax), to16(-ay)};
}

// Smooth start and stop for a 0..1 blend.
inline float Ease(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Between two poses: eyes and directions mixed, then squared up again by ViewFrom.
inline Pose Blend(const Pose& a, const Pose& b, float t) {
  const float e = Ease(t);
  return {Lerp(a.eye, b.eye, e), Normalize(Lerp(a.forward, b.forward, e)), Normalize(Lerp(a.up, b.up, e))};
}

// How far the blend moves towards `target` (0 or 1) in `dt` seconds, taking `seconds` for the whole way.
inline float StepBlend(float blend, float target, float dt, float seconds) {
  const float step = seconds > 0.0f ? dt / seconds : 1.0f;
  return target > blend ? std::min(target, blend + step) : std::max(target, blend - step);
}

// A point that follows another without overshoot (a critically damped spring), so the camera trails the ship
// smoothly. `stiffness` is how quickly it closes in (per second).
struct Follow {
  float x = 0, y = 0;
  float vx = 0, vy = 0;
  bool placed = false;

  void Snap(float tx, float ty) {
    x = tx;
    y = ty;
    vx = vy = 0;
    placed = true;
  }

  void Step(float tx, float ty, float dt, float stiffness) {
    if (!placed) {
      Snap(tx, ty);
      return;
    }
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float k = stiffness;
    // Semi-implicit Euler on x'' = k^2 (target - x) - 2k x'.
    vx += (k * k * (tx - x) - 2.0f * k * vx) * dt;
    vy += (k * k * (ty - y) - 2.0f * k * vy) * dt;
    x += vx * dt;
    y += vy * dt;
  }
};

// Latched controls: while a stick is held, it keeps the camera's facing from the moment it was pushed, so a turning
// camera never twists a held direction (W stays the same way on the arena until it is let go). Changing direction
// without letting go keeps the latch; back inside the dead zone it lets go, and the next push takes the camera as it
// is then. The dead zone is XInput's own for the left stick.
inline constexpr int32_t kLatchDeadZone = 7849;

struct StickLatch {
  bool held = false;
  float up_x = 0.0f, up_y = -1.0f;

  StickXY Apply(int16_t sx, int16_t sy, float live_up_x, float live_up_y) {
    const bool pushed = int32_t(sx) * sx + int32_t(sy) * sy > kLatchDeadZone * kLatchDeadZone;
    if (!pushed) {
      held = false;
      return TurnStick(sx, sy, live_up_x, live_up_y);
    }
    if (!held) {
      held = true;
      up_x = live_up_x;
      up_y = live_up_y;
    }
    return TurnStick(sx, sy, up_x, up_y);
  }
};

}  // namespace gw2::camera
