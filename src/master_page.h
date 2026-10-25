#pragma once

// The Single Player screen's second page (master_page.cpp): RB (3 on the keyboard) turns from the game's six modes to
// the two Master modes and back. Page two is the game's own screen, built with two tiles, Deadline's and Evolved's,
// renamed, always open, their scores read from the Master boards, and starting the Master mode.

#include <cstdint>
#include <optional>

#include <rex/ppc/context.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::master_page {

// A Single Player row is about to be added (the hook on sub_820900A8): true to leave it out of page two.
bool SkipRow(const PPCContext& row);
// The game's text `id` on page two (the two tiles' names), or none.
std::optional<uint32_t> Text(uint32_t id);
// The board a view's scores are read from (a Master mode's for its base mode's view) while page two is chosen: on the
// Single Player screen, the screens it leads to and the leaderboard screen opened from it, until the main menu shows.
uint32_t LeaderboardSource(uint32_t view);
// The Single Player screen starts a mode (sub_8208D410, row in r4): on page two, the Master mode starts instead and
// this returns true.
bool StartTile(const PPCContext& ctx, const uint8_t* base);

// The game read pad `user` into `state`: on the Single Player screen, a new RB turns the page (and is kept from the
// game).
void FilterPad(uint8_t* base, uint32_t user, uint32_t state);
// Every frame (the pad poll): a page turn asked for is made here, on the game thread.
void EachFrame(PPCContext& ctx, uint8_t* base);
// The page marks over the Single Player screen.
void CreateOverlay(rex::ui::ImGuiDrawer* drawer);

// The screen switch (sub_8208FFF0), from its hook.
void OnScreenSwitch(uint32_t screen);
// A Single Player tile was pressed (sub_8208D548, its row in r4).
void OnTile(const uint8_t* base, uint32_t row);

}  // namespace gw2::master_page
