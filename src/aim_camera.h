#pragma once

// Where a point of the game world appears on screen, from the view and projection matrices the game builds for its
// cameras. Mouse aim needs the ship's screen position to point the right stick at the cursor.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gw2::aim {

// A D3D matrix as the game stores it: row-major, applied to row vectors (v * M).
using Mat4 = std::array<float, 16>;

struct Vec2 {
  float x;
  float y;
};

// The three functions that build a camera from sub_8206A1C8 (view) and sub_8206A300 (projection).
enum class Camera : uint8_t { kA, kB, kC, kCount };

// The camera whose code holds a return address, or kCount for another caller.
constexpr Camera CameraForReturn(uint32_t lr) {
  if (lr >= 0x82067A20 && lr < 0x82067E40) {
    return Camera::kB;  // sub_82067A20
  }
  if (lr >= 0x82067E40 && lr < 0x82068408) {
    return Camera::kA;  // sub_82067E40
  }
  if (lr >= 0x82068408 && lr < 0x82068C00) {
    return Camera::kC;  // sub_82068408
  }
  return Camera::kCount;
}

// The point in normalized device coordinates (-1 to 1, y up), or nullopt behind the camera.
inline std::optional<Vec2> ProjectToNdc(const Mat4& view, const Mat4& proj, float x, float y, float z) {
  float v[4];
  for (int c = 0; c < 4; ++c) {
    v[c] = x * view[c] + y * view[4 + c] + z * view[8 + c] + view[12 + c];
  }
  float clip[4];
  for (int c = 0; c < 4; ++c) {
    clip[c] = v[0] * proj[c] + v[1] * proj[4 + c] + v[2] * proj[8 + c] + v[3] * proj[12 + c];
  }
  if (clip[3] <= 1e-6f) {
    return std::nullopt;
  }
  return Vec2{clip[0] / clip[3], clip[1] / clip[3]};
}

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

// The canvas as the presenter shows it in a window: the largest rectangle of its aspect that fits, centered.
inline Rect FitCanvas(float window_width, float window_height, double canvas_aspect) {
  if (window_height <= 0.0f || window_width <= 0.0f) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }
  if (double(window_width) / double(window_height) > canvas_aspect) {
    const float width = float(double(window_height) * canvas_aspect);
    return {(window_width - width) / 2.0f, 0.0f, width, window_height};
  }
  const float height = float(double(window_width) / canvas_aspect);
  return {0.0f, (window_height - height) / 2.0f, window_width, height};
}

// Window pixels (y down) for a point in normalized device coordinates.
inline Vec2 NdcToWindow(const Vec2& ndc, const Rect& canvas) {
  return {canvas.x + (ndc.x * 0.5f + 0.5f) * canvas.width, canvas.y + (0.5f - ndc.y * 0.5f) * canvas.height};
}

inline Vec2 WindowToNdc(const Vec2& p, const Rect& canvas) {
  return {(p.x - canvas.x) / canvas.width * 2.0f - 1.0f, 1.0f - (p.y - canvas.y) / canvas.height * 2.0f};
}

// The arena point (z = 0) under a point in normalized device coordinates. On that plane view * proj reduces to a 3x3
// map from (x, y, 1) to (clip x, clip y, clip w), which is inverted here.
inline std::optional<Vec2> UnprojectToArena(const Mat4& view, const Mat4& proj, const Vec2& ndc) {
  Mat4 m{};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += view[r * 4 + k] * proj[k * 4 + c];
      }
      m[r * 4 + c] = sum;
    }
  }
  // Row vectors: (x, y, 1) times rows 0, 1 and 3, columns 0, 1 and 3.
  const double h[3][3] = {{m[0], m[1], m[3]}, {m[4], m[5], m[7]}, {m[12], m[13], m[15]}};
  const double det = h[0][0] * (h[1][1] * h[2][2] - h[1][2] * h[2][1]) -
                     h[0][1] * (h[1][0] * h[2][2] - h[1][2] * h[2][0]) +
                     h[0][2] * (h[1][0] * h[2][1] - h[1][1] * h[2][0]);
  if (det > -1e-12 && det < 1e-12) {
    return std::nullopt;
  }
  // inverse = adjugate / det; (u, v, 1) times the inverse gives (x, y, 1) up to scale.
  double inv[3][3];
  inv[0][0] = (h[1][1] * h[2][2] - h[1][2] * h[2][1]) / det;
  inv[0][1] = (h[0][2] * h[2][1] - h[0][1] * h[2][2]) / det;
  inv[0][2] = (h[0][1] * h[1][2] - h[0][2] * h[1][1]) / det;
  inv[1][0] = (h[1][2] * h[2][0] - h[1][0] * h[2][2]) / det;
  inv[1][1] = (h[0][0] * h[2][2] - h[0][2] * h[2][0]) / det;
  inv[1][2] = (h[0][2] * h[1][0] - h[0][0] * h[1][2]) / det;
  inv[2][0] = (h[1][0] * h[2][1] - h[1][1] * h[2][0]) / det;
  inv[2][1] = (h[0][1] * h[2][0] - h[0][0] * h[2][1]) / det;
  inv[2][2] = (h[0][0] * h[1][1] - h[0][1] * h[1][0]) / det;
  const double u = ndc.x;
  const double v = ndc.y;
  const double x = u * inv[0][0] + v * inv[1][0] + inv[2][0];
  const double y = u * inv[0][1] + v * inv[1][1] + inv[2][1];
  const double w = u * inv[0][2] + v * inv[1][2] + inv[2][2];
  if (w > -1e-12 && w < 1e-12) {
    return std::nullopt;
  }
  return Vec2{float(x / w), float(y / w)};
}

struct Stick {
  int16_t x;
  int16_t y;
};

// A full right-stick deflection from the ship towards the target, both on the arena. Arena y grows down the screen
// and stick y grows up, so y flips. Closer than dead_zone keeps the previous direction, so the shots do not spin
// around a cursor resting on the ship.
inline Stick AimStick(const Vec2& ship, const Vec2& target, float dead_zone, Stick previous) {
  const float dx = target.x - ship.x;
  const float dy = target.y - ship.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (!(length > dead_zone)) {
    return previous;
  }
  return {int16_t(std::lround(dx / length * 32767.0f)), int16_t(std::lround(-dy / length * 32767.0f))};
}

// Called from the game thread.
void CaptureProjection(const uint8_t* base, uint32_t matrix, uint32_t lr);
// The ship moves on the arena plane, z = 0.
void ObservePlayer(float x, float y);

// Whether the mouse aimed and fired in the last minute: what a run that just ended was played with. Any thread.
bool MouseAimedRecently();

}  // namespace gw2::aim

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace gw2 {

// Mouse aim with Keyboard on: while the left button is held, the right stick points from the ship to the cursor, and
// the right button holds the right trigger (bomb). A crosshair replaces the cursor during a round. Escape presses
// Start in a round (pause) and B elsewhere. gw2_aim_debug also draws a dot per camera where it puts the ship.
void CreateMouseAim(rex::ui::ImGuiDrawer* drawer);

// Whether the crosshair is drawn, so the window cursor hides behind it.
bool CrosshairShown();

}  // namespace gw2
