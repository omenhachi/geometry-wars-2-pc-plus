#pragma once

// The Pilot screen (pilot_menu.cpp), opened from the main menu's left panel: the pilot's level, and three tabs, Stats,
// Bounties and Rewards (progression.h).

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::pilot_menu {

void CreatePilotMenu(rex::ui::ImGuiDrawer* drawer);

void Open();
bool IsOpen();

// The game read pad `user` into `state`: while the screen is open it reads the pad and the game is given a neutral one.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

// Keeps a setting a reward is worn by (a palette, a crosshair, a ship colour), as the Colour row does.
void SetSaver(std::function<void(std::string_view key, std::string value)> save);

}  // namespace gw2::pilot_menu
