#pragma once

#include <cmath>
#include <cstdlib>
#include <string_view>

namespace gw2 {

enum class AspectRatio { k16x9, k16x10, k21x9, k32x9, k4x3, k5x4 };

struct WindowSize {
  int width = 0;
  int height = 0;
};

// The game renders 720 lines at every aspect ratio; only the width follows the format.
inline constexpr int kCanvasHeight = 720;
inline constexpr int kCanvasWidthStep = 16;

struct AspectRatioInfo {
  AspectRatio aspect;
  const char* key;
  const char* label;
  int ratio_x;
  int ratio_y;
  WindowSize default_window;
};

inline constexpr AspectRatioInfo kAspectRatios[] = {
    {AspectRatio::k16x9, "16:9", "16:9 Original", 16, 9, {1280, 720}},
    {AspectRatio::k16x10, "16:10", "16:10", 16, 10, {1280, 800}},
    {AspectRatio::k21x9, "21:9", "21:9 Ultrawide", 21, 9, {1680, 720}},
    {AspectRatio::k32x9, "32:9", "32:9 Super Ultrawide", 32, 9, {1920, 540}},
    {AspectRatio::k4x3, "4:3", "4:3", 4, 3, {1024, 768}},
    {AspectRatio::k5x4, "5:4", "5:4", 5, 4, {1280, 1024}},
};

struct AspectWindowSize {
  int width;
  int height;
  AspectRatio aspect;
};

inline constexpr AspectWindowSize kWindowSizes[] = {
    {853, 480, AspectRatio::k16x9},   {1280, 720, AspectRatio::k16x9},   {1600, 900, AspectRatio::k16x9},
    {1920, 1080, AspectRatio::k16x9}, {1280, 800, AspectRatio::k16x10},  {1440, 900, AspectRatio::k16x10},
    {1680, 1050, AspectRatio::k16x10}, {1920, 1200, AspectRatio::k16x10}, {1260, 540, AspectRatio::k21x9},
    {1680, 720, AspectRatio::k21x9},  {2100, 900, AspectRatio::k21x9},   {2520, 1080, AspectRatio::k21x9},
    {1600, 450, AspectRatio::k32x9},  {1920, 540, AspectRatio::k32x9},   {2560, 720, AspectRatio::k32x9},
    {3840, 1080, AspectRatio::k32x9}, {1024, 768, AspectRatio::k4x3},    {1280, 960, AspectRatio::k4x3},
    {1600, 1200, AspectRatio::k4x3},  {1000, 800, AspectRatio::k5x4},    {1280, 1024, AspectRatio::k5x4},
};

constexpr const AspectRatioInfo& InfoFor(AspectRatio aspect) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (info.aspect == aspect) {
      return info;
    }
  }
  return kAspectRatios[0];
}

// A missing or unknown value is the original 16:9 picture.
constexpr AspectRatio ParseAspectRatio(std::string_view key) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (key == info.key) {
      return info.aspect;
    }
  }
  return AspectRatio::k16x9;
}

// The width the game renders at: the format's width at 720 lines, rounded to the 16 pixels its render targets and
// resolves are aligned to.
constexpr int CanvasWidth(AspectRatio aspect) {
  const AspectRatioInfo& info = InfoFor(aspect);
  const int half_step = kCanvasWidthStep / 2;
  return (kCanvasHeight * info.ratio_x + half_step * info.ratio_y) / (kCanvasWidthStep * info.ratio_y) *
         kCanvasWidthStep;
}

constexpr double CanvasAspect(AspectRatio aspect) {
  return double(CanvasWidth(aspect)) / double(kCanvasHeight);
}

constexpr WindowSize RenderSize(AspectRatio aspect, int scale) {
  return {CanvasWidth(aspect) * scale, kCanvasHeight * scale};
}

// The guest video mode, which sets the display aspect the presenter keeps. It is 3/4 of the canvas so that the picture is
// not stretched, and neither side equals a default of the SDK's video mode (1280x720), because it ignores those.
constexpr WindowSize VideoModeSize(AspectRatio aspect) {
  return {CanvasWidth(aspect) * 3 / 4, kCanvasHeight * 3 / 4};
}

// The aspect the game builds every camera and its full-screen sprites for.
inline constexpr double kGameAspect = 1.7777777910232544;
inline constexpr float kSpriteSpaceWidth = 640.0f;
inline constexpr float kSpriteSpaceHeight = 360.0f;

struct Projection {
  double aspect;
  double fov;
};

// The camera for a canvas. Wider than 16:9 keeps the vertical field of view, so more of the world shows at the sides.
// Narrower keeps the 16:9 horizontal extent instead, so the arena and the HUD are not cut off.
inline Projection ProjectionFor(double fov, double canvas_aspect) {
  if (canvas_aspect >= kGameAspect) {
    return {canvas_aspect, fov};
  }
  return {canvas_aspect, 2.0 * std::atan(std::tan(fov / 2.0) * (kGameAspect / canvas_aspect))};
}

struct SpriteRect {
  float x;
  float y;
  float width;
  float height;
};

// A full-screen sprite (in the 640x360 sprite space, which the game maps onto the whole canvas) resized to the largest
// 16:9 picture that fits the canvas, centered.
inline SpriteRect FitSpriteTo16x9(double canvas_aspect) {
  if (canvas_aspect >= kGameAspect) {
    const float width = float(kSpriteSpaceWidth * kGameAspect / canvas_aspect);
    return {(kSpriteSpaceWidth - width) / 2.0f, 0.0f, width, kSpriteSpaceHeight};
  }
  const float height = float(kSpriteSpaceHeight * canvas_aspect / kGameAspect);
  return {0.0f, (kSpriteSpaceHeight - height) / 2.0f, kSpriteSpaceWidth, height};
}

// Whether a window has the shape of the format, to within 1%.
constexpr bool WindowMatchesAspect(WindowSize size, AspectRatio aspect) {
  if (size.width <= 0 || size.height <= 0) {
    return false;
  }
  const AspectRatioInfo& info = InfoFor(aspect);
  const long long window = (long long)size.width * info.ratio_y;
  const long long format = (long long)size.height * info.ratio_x;
  return std::llabs(window - format) * 100 <= format;
}

// The Window size to select for a format: the current one when it already has that shape, else the format's default,
// else the largest listed size that fits. `fits` says whether a size fits the selected display.
template <typename Fits>
constexpr WindowSize WindowSizeForAspect(AspectRatio aspect, WindowSize current, Fits fits) {
  if (WindowMatchesAspect(current, aspect)) {
    return current;
  }
  const WindowSize preferred = InfoFor(aspect).default_window;
  if (fits(preferred)) {
    return preferred;
  }
  WindowSize largest{};
  for (const AspectWindowSize& size : kWindowSizes) {
    if (size.aspect == aspect && fits({size.width, size.height}) && size.width > largest.width) {
      largest = {size.width, size.height};
    }
  }
  return largest.width > 0 ? largest : preferred;
}

}  // namespace gw2
