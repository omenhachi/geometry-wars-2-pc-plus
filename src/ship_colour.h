#pragma once

// The ship's colour, a pilot reward (progression_rules.h) worn as gw2_ship_colour: as the Player constructor
// (sub_82051448) builds a ship it copies the colour from the ship's mesh to +40..+48 (red, green, blue, each scaled by
// the mesh's brightness), and the ship is drawn in it. A worn colour replaces it there, at the same brightness, for
// player 1 of a one-player game; multiplayer keeps each player's own colour.

#include <cstdint>

namespace gw2::ship_colour {

// After the constructor: `player` the ship object, `index` its player.
void AfterPlayerBuilt(uint8_t* base, uint32_t player, uint32_t index);

}  // namespace gw2::ship_colour
