#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "launcher_settings.h"

namespace gw2 {

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  int width() const { return right - left; }
  int height() const { return bottom - top; }
  bool operator==(const Rect&) const = default;
};

// The launcher window's restored rectangle in screen pixels, and whether it is maximized.
struct WindowPlacement {
  Rect normal;
  bool maximized = false;

  bool operator==(const WindowPlacement&) const = default;
};

// What the window is doing right now. The rectangle means nothing while minimized.
struct WindowState {
  Rect rect;
  bool maximized = false;
  bool minimized = false;
};

inline constexpr int kMinPlacementWidth = 320;
inline constexpr int kMinPlacementHeight = 240;
inline constexpr int kMinVisibleWidth = 100;
inline constexpr int kMinVisibleHeight = 50;
inline constexpr double kPlacementSaveDelaySeconds = 0.5;

inline bool PlacementIsSet(const WindowPlacement& placement) {
  return placement.normal.width() >= kMinPlacementWidth && placement.normal.height() >= kMinPlacementHeight;
}

// A saved window is only restored if enough of it lies on a display to be grabbed.
inline bool PlacementIsVisible(const WindowPlacement& placement, std::span<const Rect> work_areas) {
  if (!PlacementIsSet(placement)) {
    return false;
  }
  for (const Rect& area : work_areas) {
    const int width = std::min(placement.normal.right, area.right) - std::max(placement.normal.left, area.left);
    const int height = std::min(placement.normal.bottom, area.bottom) - std::max(placement.normal.top, area.top);
    if (width >= kMinVisibleWidth && height >= kMinVisibleHeight) {
      return true;
    }
  }
  return false;
}

// What to do to the window before it is first shown: the restored rectangle when it is on a display, and
// maximizing. A maximized window with an unusable rectangle is maximized where the window already is.
struct PreShowPlan {
  std::optional<Rect> normal;
  bool maximized = false;

  bool Needed() const { return normal.has_value() || maximized; }
};

inline PreShowPlan PlanPreShow(const WindowPlacement& saved, std::span<const Rect> work_areas) {
  PreShowPlan plan;
  if (PlacementIsVisible(saved, work_areas)) {
    plan.normal = saved.normal;
  }
  plan.maximized = saved.maximized;
  return plan;
}

template <typename Settings>
WindowPlacement ReadPlacement(const Settings& settings) {
  WindowPlacement placement;
  placement.normal.left = settings.GetInt(kLauncherXKey, 0);
  placement.normal.top = settings.GetInt(kLauncherYKey, 0);
  placement.normal.right = placement.normal.left + settings.GetInt(kLauncherWidthKey, 0);
  placement.normal.bottom = placement.normal.top + settings.GetInt(kLauncherHeightKey, 0);
  placement.maximized = settings.GetBool(kLauncherMaximizedKey, false);
  return placement;
}

inline Values PlacementValues(const WindowPlacement& placement) {
  return {
      {kLauncherXKey, std::to_string(placement.normal.left)},
      {kLauncherYKey, std::to_string(placement.normal.top)},
      {kLauncherWidthKey, std::to_string(placement.normal.width())},
      {kLauncherHeightKey, std::to_string(placement.normal.height())},
      {kLauncherMaximizedKey, placement.maximized ? "true" : "false"},
  };
}

// Follows the window and says when its placement has changed and settled, so it is saved once and not on every move.
// A maximized window keeps the restored rectangle; a minimized or fullscreen one changes nothing.
class PlacementTracker {
 public:
  explicit PlacementTracker(std::optional<WindowPlacement> saved = std::nullopt) : saved_(saved), current_(saved) {}

  void Observe(const WindowState& state, bool fullscreen, double now) {
    if (fullscreen) {
      return;
    }
    WindowPlacement next = current_.value_or(WindowPlacement{});
    next.maximized = state.maximized;
    if (!state.minimized && !state.maximized) {
      next.normal = state.rect;
    }
    if (!saved_) {
      saved_ = next;
    }
    if (!current_ || next != *current_) {
      current_ = next;
      changed_at_ = now;
    }
  }

  std::optional<WindowPlacement> TakeSettled(double now) {
    if (!Dirty() || now - changed_at_ < kPlacementSaveDelaySeconds) {
      return std::nullopt;
    }
    return Flush();
  }

  std::optional<WindowPlacement> Flush() {
    if (!Dirty()) {
      return std::nullopt;
    }
    saved_ = current_;
    return saved_;
  }

 private:
  bool Dirty() const { return current_ && current_ != saved_ && PlacementIsSet(*current_); }

  std::optional<WindowPlacement> saved_;
  std::optional<WindowPlacement> current_;
  double changed_at_ = 0.0;
};

}  // namespace gw2
