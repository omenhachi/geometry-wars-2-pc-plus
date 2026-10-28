#include "ship_colour.h"

#include <algorithm>
#include <bit>
#include <map>
#include <string>

#include <rex/cvar.h>

#include "guest_memory.h"
#include "launcher_cvars.h"
#include "player_records.h"

namespace gw2::ship_colour {
namespace {

constexpr uint32_t kMeshOffset = 240;       // the ship's mesh: a table pointer, then a u16 key and a u16 offset
constexpr uint32_t kMeshColourOffset = 28;  // red, green, blue floats in the mesh's entry, then +40 its brightness

struct Colour {
  const char* id;
  float r, g, b;
};

// Deep, because the game draws a line with a near-white core and its colour mostly in the glow.
constexpr Colour kColours[] = {
    {"ship-pink", 1.0f, 0.05f, 0.55f},
    {"ship-gold", 1.0f, 0.6f, 0.0f},
    {"ship-cyan", 0.2f, 0.75f, 1.0f},
};

struct Rgb {
  uint32_t r, g, b;
};
std::map<uint32_t, Rgb> g_original;  // each mesh entry's own colour, as first seen

void StoreF(uint8_t* base, uint32_t address, float value) {
  StoreBE32(base, address, std::bit_cast<uint32_t>(value));
}

// The mesh entry the constructor took the colour from (sub_82051448 after its sub_82031D40 lookup), or 0.
uint32_t MeshEntry(const uint8_t* base, uint32_t player) {
  const uint32_t table = LoadBE32(base, player + kMeshOffset);
  if (table == 0 || LoadBE16(base, table) != LoadBE16(base, player + kMeshOffset + 4)) return 0;
  const uint32_t entries = LoadBE32(base, table + 4);
  return entries ? entries + LoadBE16(base, player + kMeshOffset + 6) : 0;
}

}  // namespace

void AfterPlayerBuilt(uint8_t* base, uint32_t player, uint32_t index) {
  if (player == 0 || index != 0) return;
  const uint32_t entry = MeshEntry(base, player);
  if (entry == 0) return;
  const uint32_t at = entry + kMeshColourOffset;
  const auto [it, first] = g_original.try_emplace(entry, Rgb{LoadBE32(base, at), LoadBE32(base, at + 4),
                                                             LoadBE32(base, at + 8)});
  // The game's own colour first, so taking a colour off (or multiplayer) puts it back.
  StoreBE32(base, at, it->second.r);
  StoreBE32(base, at + 4, it->second.g);
  StoreBE32(base, at + 8, it->second.b);
  if (LoadBE32(base, players::kPlayerCountAddress) != 1) return;
  const std::string worn = REXCVAR_GET(gw2_ship_colour);
  for (const Colour& c : kColours) {
    if (worn != c.id) continue;
    StoreF(base, at, c.r);
    StoreF(base, at + 4, c.g);
    StoreF(base, at + 8, c.b);
    return;
  }
}

}  // namespace gw2::ship_colour
