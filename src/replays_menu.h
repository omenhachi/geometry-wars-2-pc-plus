#pragma once

// Watch replays (replays_menu.cpp): the main menu's left panel (main_menu_panels.h) opens a list of saved replays
// (demo_library.h), newest first, with a tab per mode. A replay starts from the list and a badge shows while it plays.

#include <cstdint>
#include <string>

#include <rex/ppc/context.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::replays_menu {

void CreateReplaysMenu(rex::ui::ImGuiDrawer* drawer);

// Opens the list over the main menu, and whether it is open.
void Open();
bool IsOpen();

// Downloads a player's replay on an online board and plays it (from the game's own leaderboard screen), with a note
// over the game while it downloads. False when a download is already under way.
bool WatchOnline(uint32_t board, const std::string& id);

// The game read pad `user` into `state` (its XInputGetState, live_fix.cpp): while the list is open it reads the pad
// and the game is given a neutral one.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2::replays_menu
