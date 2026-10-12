#pragma once

#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

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

// The plain title, or the title followed by the frame rate.
inline std::string WindowTitle(bool show_fps, double fps) {
  if (!show_fps) {
    return kWindowTitle;
  }
  char digits[32];
  const std::to_chars_result result = std::to_chars(digits, digits + sizeof(digits), fps, std::chars_format::fixed, 2);
  return std::string(kWindowTitle) + " | FPS: " + std::string(digits, result.ptr);
}

inline constexpr std::chrono::milliseconds kCursorHideDelay{5000};

// Hides the cursor in fullscreen gameplay with focus after kCursorHideDelay without mouse motion.
class CursorHider {
 public:
  using Clock = std::chrono::steady_clock;

  bool visible() const { return visible_; }

  void MouseMoved(Clock::time_point now) { last_motion_ = now; }

  // Returns the new visibility when it changes, otherwise nullopt.
  std::optional<bool> Update(bool gameplay, bool fullscreen, bool focused, Clock::time_point now) {
    const bool active = gameplay && fullscreen && focused;
    if (!active || !active_) {
      last_motion_ = now;
    }
    active_ = active;
    const bool visible = !active || now - last_motion_ < kCursorHideDelay;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

 private:
  Clock::time_point last_motion_{};
  bool active_ = false;
  bool visible_ = true;
};

// F11 calls `toggle_window_mode`. The key does not reach the game.
// The cursor auto-hides only while `gameplay_active` returns true, i.e. no launcher is shown.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<void()> toggle_window_mode, std::function<bool()> gameplay_active);
void DetachWindowHost();

// Checks the cursor's idle time on every UI frame.
void CreateCursorTick(rex::ui::ImGuiDrawer* drawer);

// The latest sample; all zero until the first one is measured.
GuestFrameStats GetGuestFrameStats();

}  // namespace gw2
