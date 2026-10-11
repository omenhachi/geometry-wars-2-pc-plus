#include "launcher_options.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "frame_limiter.h"

namespace gw2 {
namespace {

constexpr char kTimes[] = " \xC3\x97 ";
constexpr char kDash[] = " \xE2\x80\x94 ";

std::string Dimensions(int width, int height) {
  return std::to_string(width) + kTimes + std::to_string(height);
}

}  // namespace

std::string_view PageName(PageId page) {
  switch (page) {
    case PageId::kPlay:
      return "Play";
    case PageId::kDisplay:
      return "Display";
    case PageId::kGraphics:
      return "Graphics";
    case PageId::kGameplay:
      return "Gameplay";
    case PageId::kControls:
      return "Controls";
    case PageId::kAchievements:
      return "Achievements";
    case PageId::kAbout:
      return "About";
  }
  return "";
}

bool WindowSizeFits(WindowSize size, const MonitorInfo& display) {
  if (display.width == 0) {
    return true;
  }
  return size.width * display.scale < display.width && size.height * display.scale < display.height;
}

std::string WindowSizeLabel(WindowSize size, float scale) {
  std::string label = Dimensions(size.width, size.height);
  if (scale != 1.0f) {
    label += "   (" + Dimensions(int(std::lround(size.width * scale)), int(std::lround(size.height * scale))) +
             " on screen)";
  }
  return label;
}

std::string MonitorLabel(int monitor, const std::vector<MonitorInfo>& monitors) {
  if (monitor <= 0) {
    return "Default (primary)";
  }
  const std::string number = std::to_string(monitor);
  if (monitor > int(monitors.size())) {
    return "Display " + number + " (not connected)";
  }
  const MonitorInfo& display = monitors[size_t(monitor - 1)];
  return "Display " + number + (display.primary ? " (primary)" : "") + "   " +
         Dimensions(display.width, display.height);
}

std::string RenderScaleLabel(int scale) {
  std::string label = scale == 1 ? "Original" : std::to_string(scale) + "x";
  label += kDash + Dimensions(kPresentedWidth * scale, kPresentedHeight * scale);
  if (scale == 2) {
    label += kDash + std::string("Recommended for 1440p");
  }
  return label;
}

std::vector<SettingChoice> RenderScaleChoices() {
  std::vector<SettingChoice> choices;
  for (int scale = 1; scale <= 4; ++scale) {
    choices.push_back({std::to_string(scale), RenderScaleLabel(scale)});
  }
  return choices;
}

std::vector<SettingChoice> AntiAliasingChoices() {
  return {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}};
}

std::vector<SettingChoice> TextureFilteringChoices() {
  return {{"-1", "Game"}, {"0", "Off"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}};
}

std::vector<SettingChoice> FrameRateChoices() {
  std::vector<SettingChoice> choices;
  for (const int fps : kFrameRates) {
    choices.push_back({std::to_string(fps), fps == kFrameRateUnlimited ? "Unlimited" : std::to_string(fps)});
  }
  return choices;
}

std::string KeyList(std::string keys) {
  if (keys.empty()) {
    return "None";
  }
  for (std::size_t i = keys.find(','); i != std::string::npos; i = keys.find(',', i)) {
    keys.replace(i, 1, " or ");
  }
  return keys;
}

}  // namespace gw2
