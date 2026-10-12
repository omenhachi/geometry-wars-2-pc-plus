#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "launcher_platform.h"

namespace gw2 {

// One choice of a launcher row: the text the settings store keeps and the label the player reads.
struct SettingChoice {
  std::string value;
  std::string label;
};

struct WindowSize {
  int width = 0;
  int height = 0;
};

inline constexpr WindowSize kWindowSizes[] = {{853, 480}, {1280, 720}, {1600, 900}, {1920, 1080}};

// The size the Xenos presents the game at, before Render resolution scales it. The guest video mode does not
// change it.
inline constexpr int kPresentedWidth = 1280;
inline constexpr int kPresentedHeight = 720;

enum class PageId { kPlay, kDisplay, kGraphics, kGameplay, kCheats, kControls, kAchievements, kAbout };

inline constexpr PageId kPageOrder[] = {PageId::kPlay,   PageId::kDisplay,  PageId::kGraphics,     PageId::kGameplay,
                                        PageId::kCheats, PageId::kControls, PageId::kAchievements, PageId::kAbout};

std::string_view PageName(PageId page);

// Whether the size fits the display at its scaling, so the game window is not larger than the screen.
bool WindowSizeFits(WindowSize size, const MonitorInfo& display);

// "1280 × 720", followed by the on-screen size when the display is scaled.
std::string WindowSizeLabel(WindowSize size, float scale);

// "Display 2   2560 × 1440". Monitor 0 is the default, which is the primary display.
std::string MonitorLabel(int monitor, const std::vector<MonitorInfo>& monitors);

std::vector<SettingChoice> RenderScaleChoices();
std::string RenderScaleLabel(int scale);

std::vector<SettingChoice> AntiAliasingChoices();
std::vector<SettingChoice> TextureFilteringChoices();
std::vector<SettingChoice> FrameRateChoices();

std::string KeyList(std::string keys);

}  // namespace gw2
