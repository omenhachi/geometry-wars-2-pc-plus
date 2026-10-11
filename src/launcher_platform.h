#pragma once

#include <algorithm>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "launcher_placement.h"

struct ImGuiIO;

namespace gw2 {

// TRG Launcher and the settings file take UTF-8 paths.
inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

inline std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

struct MonitorInfo {
  int width = 0;        // physical pixels
  int height = 0;
  int work_width = 0;   // without the taskbar
  int work_height = 0;
  float scale = 1.0f;   // display scaling
  bool primary = false;
};

// In the order ReXGlue's `monitor` setting numbers them: the primary display first.
std::vector<MonitorInfo> ListMonitors();

// The display a `monitor` setting names. 0 and 1 are the primary display, as the runtime treats them.
MonitorInfo MonitorForSetting(int monitor);

// The window's logical size, shrunk so it stays within this share of the display's work area.
inline constexpr double kWindowDisplayShare = 0.9;

inline std::pair<int, int> FitWindowSize(int width, int height, const MonitorInfo& display) {
  if (display.work_width <= 0 || display.work_height <= 0 || display.scale <= 0.0f) {
    return {width, height};
  }
  const int max_width = int(display.work_width * kWindowDisplayShare / display.scale);
  const int max_height = int(display.work_height * kWindowDisplayShare / display.scale);
  return {std::min(width, max_width), std::min(height, max_height)};
}


// Display work areas in screen pixels.
std::vector<Rect> WorkAreas();

// The native window's state, or nothing when the handle is not a window.
std::optional<WindowState> QueryWindowState(void* native_handle);

// Puts the window at the placement: the restored rectangle first, then maximized when asked.
void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement);

// Applies the plan to the next top-level window of this thread, before it is first shown. The window's size
// before the change is kept for TakePreShowResult.
void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved);

struct PreShowResult {
  WindowPlacement startup;
  WindowPlacement applied;
};

// Set once the window has been shown; empty when no window was placed.
std::optional<PreShowResult> TakePreShowResult();

// Starts a new instance with this run's command line plus `extra_args`.
bool RelaunchSelf(std::wstring_view extra_args);

struct UiFontFiles {
  std::filesystem::path regular;
  std::filesystem::path semibold;
  std::filesystem::path bold;
};
UiFontFiles FindUiFonts();

// Feeds an Xbox controller to Dear ImGui while the launcher is open.
class LauncherGamepad {
 public:
  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  int pad_ = -1;
  double next_scan_ = 0.0;
};

}  // namespace gw2
