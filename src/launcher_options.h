#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "aspect_ratio.h"
#include "graphics_adapters.h"
#include "internal_render_scale.h"
#include "launcher_platform.h"

namespace gw2 {

// One choice of a launcher row: the text the settings store keeps and the label the player reads.
struct SettingChoice {
  std::string value;
  std::string label;
};

enum class PageId { kPlay, kDisplay, kGraphics, kGameplay, kCheats, kControls, kAchievements, kAbout, kDiscord };

inline constexpr PageId kPageOrder[] = {PageId::kPlay,     PageId::kDisplay,      PageId::kGraphics,
                                        PageId::kGameplay, PageId::kCheats,       PageId::kControls,
                                        PageId::kAchievements, PageId::kAbout,    PageId::kDiscord};

std::string_view PageName(PageId page);

// Whether the size fits the display at its scaling, so the game window is not larger than the screen.
bool WindowSizeFits(WindowSize size, const MonitorInfo& display);

// "1280 × 720", followed by the on-screen size when the display is scaled.
std::string WindowSizeLabel(WindowSize size, float scale);

// "Display 2   2560 × 1440". Monitor 0 is the default, which is the primary display.
std::string MonitorLabel(int monitor, const std::vector<MonitorInfo>& monitors);

std::vector<SettingChoice> AspectRatioChoices();

// The picture the game renders at one Render resolution step, for the selected Aspect Ratio.
std::vector<SettingChoice> RenderScaleChoices(AspectRatio aspect = AspectRatio::k16x9);
std::string RenderScaleLabel(int scale, AspectRatio aspect = AspectRatio::k16x9);

// The shares worth choosing at this Render resolution: 75% only where it is faster. The current one stays listed, marked,
// so a saved choice never vanishes from the list.
std::vector<SettingChoice> InternalRenderScaleChoices(int resolution_scale = 4, int current = kInternalRenderScaleNative);

// Automatic, then each adapter (numbered when two share a name), then the saved one if it is not present.
std::vector<SettingChoice> GraphicsAdapterChoices(const std::vector<GraphicsAdapter>& adapters,
                                                  std::string_view current);

std::vector<SettingChoice> AntiAliasingChoices();
std::vector<SettingChoice> TextureFilteringChoices();
std::vector<SettingChoice> BloomChoices();
std::vector<SettingChoice> FrameRateChoices();

std::string KeyList(std::string keys);

}  // namespace gw2
