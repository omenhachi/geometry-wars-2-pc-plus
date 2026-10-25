#pragma once

// Custom modes (custom_modes_menu.cpp): the main menu's right panel (main_menu_panels.h) opens the list of custom modes
// (custom_modes.h). A mode starts from the list; a title card opens its round and a tag stays while it plays.

#include <cstdint>

#include <rex/ppc/context.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::custom_modes_menu {

void CreateCustomModesMenu(rex::ui::ImGuiDrawer* drawer);

// Opens the list over the main menu, and whether it is open.
void Open();
bool IsOpen();

// The game read pad `user` into `state`: while the list is open it reads the pad and the game is given a neutral one.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2::custom_modes_menu
