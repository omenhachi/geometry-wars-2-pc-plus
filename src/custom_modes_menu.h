#pragma once

// Custom modes (custom_modes_menu.cpp): a main menu row after Watch replays opens the list of custom modes
// (custom_modes.h). A mode starts from the list; a title card opens its round and a tag stays while it plays.

#include <cstdint>

#include <rex/ppc/context.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::custom_modes_menu {

// The row's text id, after Watch replays' (replays_menu.h). The text hook (live_fix.cpp) answers it.
inline constexpr uint32_t kCustomModesTextId = 0x401;
uint32_t CustomModesText();

void CreateCustomModesMenu(rex::ui::ImGuiDrawer* drawer);

// A main menu row was just added with sub_820900A8; after Multiplayer (and so after Watch replays) this adds Custom
// modes. Called from the one hook on sub_820900A8 (main_menu_exit_game.cpp), after replays_menu's.
void AfterMenuRowAdded(const PPCContext& row, uint8_t* base);

// The game read pad `user` into `state`: while the list is open it reads the pad and the game is given a neutral one.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2::custom_modes_menu
