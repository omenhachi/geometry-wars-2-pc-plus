#include "cheats.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(gw2_cheat_invincible, gw2::cheats::kCheatDefault, "Geometry Wars/Cheats", "Invincible");
REXCVAR_DEFINE_BOOL(gw2_cheat_infinite_shield, gw2::cheats::kCheatDefault, "Geometry Wars/Cheats",
                    "Infinite Shield");
REXCVAR_DEFINE_BOOL(gw2_cheat_infinite_bombs, gw2::cheats::kCheatDefault, "Geometry Wars/Cheats", "Infinite Bombs");

namespace gw2::cheats {
namespace {

SessionCheats g_session;

bool SavedValue(Cheat cheat) {
  switch (cheat) {
    case Cheat::Invincible:
      return REXCVAR_GET(gw2_cheat_invincible);
    case Cheat::InfiniteShield:
      return REXCVAR_GET(gw2_cheat_infinite_shield);
    case Cheat::InfiniteBombs:
      return REXCVAR_GET(gw2_cheat_infinite_bombs);
  }
  return false;
}

}  // namespace

bool Enabled(Cheat cheat) {
  return g_session.Effective(cheat, SavedValue(cheat));
}

void ToggleForSession(Cheat cheat) {
  g_session.Toggle(cheat, SavedValue(cheat));
}

}  // namespace gw2::cheats
