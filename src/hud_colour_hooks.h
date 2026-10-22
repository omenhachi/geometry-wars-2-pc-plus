#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <rex/ppc/context.h>

namespace gw2 {

// Takes the interface colour from the settings (gw2_hud_colour, gw2_hud_colour_custom). Called before the game runs
// and again when the launcher's PLAY applies changes.
void ApplyHudColourSetting();

// How the in-game Colour row keeps a new choice: the app sets the setting and saves it to the player's file.
void SetHudColourSaver(std::function<void(std::string_view key, std::string value)> save);

// A menu row was just added with sub_820900A8 (its arguments in `row`); after Highscore Display this adds the Colour
// row to Help and options. Called from the one hook on sub_820900A8 (main_menu_exit_game.cpp).
void AfterMenuRowAdded(const PPCContext& row, uint8_t* base);

}  // namespace gw2
