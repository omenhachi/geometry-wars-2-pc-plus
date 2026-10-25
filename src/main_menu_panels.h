#pragma once

// The panels beside the main menu (main_menu_panels.cpp): PC Plus's own screens live in the black space either side of
// the game's menu, so the menu keeps its own rows at their own size. Left, what you've played: Watch replays. Right,
// new things to play: Custom modes.
//
// Left or right on the main menu steps into a panel (the game's menu then gets a neutral pad and dims), up and down move
// inside it, A opens, and B or the other direction steps back. The mouse lights a tile and a click opens it.

#include <cstdint>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::main_menu_panels {

void CreateOverlay(rex::ui::ImGuiDrawer* drawer);

// The game read pad `user` into `state` (its XInputGetState, live_fix.cpp), after the lists' own filters.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2::main_menu_panels
