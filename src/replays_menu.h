#pragma once

// Watch replays (replays_menu.cpp): a main menu row after Multiplayer opens a list of saved replays (demo_library.h),
// newest first, with a tab per mode. A replay starts from the list and a badge shows while it plays.

#include <cstdint>
#include <string>

#include <rex/ppc/context.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::replays_menu {

// The row's text id. The game's own ids stop well below it; the text hook (live_fix.cpp) answers it.
inline constexpr uint32_t kWatchReplaysTextId = 0x400;
// The row's text in guest memory, for the text hook.
uint32_t WatchReplaysText();

void CreateReplaysMenu(rex::ui::ImGuiDrawer* drawer);

// Downloads a player's replay on an online board and plays it (from the game's own leaderboard screen), with a note
// over the game while it downloads. False when a download is already under way.
bool WatchOnline(uint32_t board, const std::string& id);

// A main menu row was just added with sub_820900A8 (its arguments in `row`); after Multiplayer this adds Watch replays.
// Called from the one hook on sub_820900A8 (main_menu_exit_game.cpp).
void AfterMenuRowAdded(const PPCContext& row, uint8_t* base);

// The game read pad `user` into `state` (its XInputGetState, live_fix.cpp): while the list is open it reads the pad
// and the game is given a neutral one.
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2::replays_menu
