#include <bit>
#include <cstdint>

#include <rex/hook.h>

#include "cheat_rules.h"
#include "cheats.h"
#include "guest_memory.h"
#include "player_records.h"
#include "rumble.h"

namespace {

using gw2::cheats::Cheat;
using gw2::cheats::Enabled;

float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(gw2::LoadBE32(base, addr));
}

void StoreFloat(uint8_t* base, uint32_t addr, float value) {
  gw2::StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

using gw2::players::IsPlayer;

}  // namespace

// Player::Update, once per frame per player. The shield countdown is inline in it, so the timer is held afterwards.
REX_EXTERN(__imp__sub_8204FF00);
REX_HOOK_RAW(sub_8204FF00) {
  const uint32_t player = ctx.r3.u32;
  const bool is_player = IsPlayer(base, player);
  const bool infinite = is_player && Enabled(Cheat::InfiniteShield);
  __imp__sub_8204FF00(ctx, base);
  if (is_player && gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset) == 0) {
    gw2::rumble::ObserveLocalPlayer(LoadFloat(base, player + gw2::players::kCenterXOffset),
                                    LoadFloat(base, player + gw2::players::kCenterYOffset),
                                    LoadFloat(base, player + gw2::players::kRadiusOffset));
  }
  if (!infinite) {
    return;
  }
  const float shield = LoadFloat(base, player + gw2::cheats::kPlayerShieldOffset);
  const float held = gw2::cheats::HeldShield(shield);
  if (held != shield) {
    StoreFloat(base, player + gw2::cheats::kPlayerShieldOffset, held);
  }
}

// Player::OnHit, reached by every enemy, hazard and collision that can kill a player. The game's own Cheat.Invulnerable
// returns from here before anything else; this only does that for hits that would kill.
REX_EXTERN(__imp__sub_8204EEA8);
REX_HOOK_RAW(sub_8204EEA8) {
  const uint32_t player = ctx.r3.u32;
  const bool is_player = IsPlayer(base, player);
  if (is_player &&
      gw2::cheats::SuppressesHit(Enabled(Cheat::Invincible), LoadFloat(base, player + gw2::cheats::kPlayerBombGraceOffset),
                                 LoadFloat(base, player + gw2::cheats::kPlayerShieldOffset))) {
    return;
  }
  const uint32_t index = is_player ? gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset) : 0;
  const uint32_t counter = is_player ? gw2::players::ShieldHitCountAddress(index) : 0;
  const uint32_t before = counter != 0 ? gw2::LoadBE32(base, counter) : 0;
  __imp__sub_8204EEA8(ctx, base);
  if (counter != 0 && gw2::LoadBE32(base, counter) == before + 1) {
    gw2::rumble::RequestShieldHitPulse(index);
  }
}

// Player bomb input, called by Player::Update. When a bomb fires it takes one from the count at the player's record.
REX_EXTERN(__imp__sub_8204F288);
REX_HOOK_RAW(sub_8204F288) {
  const uint32_t player = ctx.r3.u32;
  const bool is_player = IsPlayer(base, player);
  const uint32_t index = is_player ? gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset) : 0;
  const uint32_t counter = is_player ? gw2::cheats::BombCountAddress(index) : 0;
  const bool infinite = counter != 0 && Enabled(Cheat::InfiniteBombs);
  const uint32_t before = counter != 0 ? gw2::LoadBE32(base, counter) : 0;
  __imp__sub_8204F288(ctx, base);
  if (counter == 0 || !gw2::cheats::BombWasConsumed(before, gw2::LoadBE32(base, counter))) {
    return;
  }
  gw2::rumble::RequestBombPulse(index);
  if (infinite) {
    gw2::StoreBE32(base, counter, before);
  }
}
