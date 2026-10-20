#pragma once

#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "aspect_ratio.h"

namespace rex::ui {
class ImGuiDrawer;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace gw2 {

inline constexpr char kWindowTitle[] = "Geometry Wars: Retro Evolved 2";

// The game's measured frame rate, from the swaps the game makes. The title, the frame counter and the SDK's F3
// overlay all read these values.
struct GuestFrameStats {
  double fps = 0;
  double frame_time_ms = 0;
  uint64_t frame_count = 0;
};

// Counts frames and returns the stats each time about a second has passed.
class FrameRateSampler {
 public:
  using Clock = std::chrono::steady_clock;

  std::optional<GuestFrameStats> Count(Clock::time_point now) {
    if (!start_) {
      start_ = now;
      return std::nullopt;
    }
    ++frames_total_;
    ++frames_;
    const double elapsed = std::chrono::duration<double>(now - *start_).count();
    if (elapsed < kSampleSeconds) {
      return std::nullopt;
    }
    const GuestFrameStats stats{frames_ / elapsed, elapsed * 1000.0 / frames_, frames_total_};
    start_ = now;
    frames_ = 0;
    return stats;
  }

 private:
  static constexpr double kSampleSeconds = 1.0;

  std::optional<Clock::time_point> start_;
  uint64_t frames_ = 0;
  uint64_t frames_total_ = 0;
};

// The game title, then the render resolution and the frame rate when shown. No frame rate before the first sample.
inline std::string WindowTitle(bool show_resolution, WindowSize render, bool show_fps, std::optional<double> fps) {
  std::string title = kWindowTitle;
  if (show_resolution) {
    title += " | " + std::to_string(render.width) + "x" + std::to_string(render.height);
  }
  if (show_fps && fps) {
    char digits[32];
    const std::to_chars_result result =
        std::to_chars(digits, digits + sizeof(digits), *fps, std::chars_format::fixed, 2);
    title += " | FPS: " + std::string(digits, result.ptr);
  }
  return title;
}

// F11 calls `toggle_window_mode`. The key does not reach the game.
// `gameplay_active` is false while the launcher is shown; `ui_active` is true while a mouse-driven overlay is up.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<void()> toggle_window_mode, std::function<bool()> gameplay_active,
                      std::function<bool()> ui_active);
void DetachWindowHost();

// Keeps the cursor in step with ImGui UI that captures the mouse.
void CreateCursorSync(rex::ui::ImGuiDrawer* drawer);

// The latest sample; all zero until the first one is measured.
GuestFrameStats GetGuestFrameStats();

}  // namespace gw2
