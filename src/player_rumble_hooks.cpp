#include <rex/hook.h>

#include "guest_memory.h"
#include "player_records.h"
#include "rumble.h"
#include "run_stats_hooks.h"

// Player::Kill. It takes no life, so the lives read before the call is the value the respawn decision uses.
REX_EXTERN(__imp__sub_8204E988);
REX_HOOK_RAW(sub_8204E988) {
  const uint32_t player = ctx.r3.u32;
  const uint32_t index = gw2::players::IsPlayer(base, player)
                             ? gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset)
                             : gw2::players::kRecordCount;
  if (index >= gw2::players::kRecordCount) {
    __imp__sub_8204E988(ctx, base);
    return;
  }
  const bool final_life = gw2::LoadBE32(base, gw2::players::LivesAddress(index)) == 0;
  const bool single_player = gw2::LoadBE32(base, gw2::players::kPlayerCountAddress) == 1;
  __imp__sub_8204E988(ctx, base);
  gw2::rumble::PlayerKilled(index, final_life, single_player);
  gw2::run::OnDeath(base, player, final_life);
}

// Score award for a kill. Its life and bomb thresholds are the only game paths that raise either count, so a
// raised count here is an earned reward. Infinite Bombs only restores the count inside the bomb hook.
REX_EXTERN(__imp__sub_820408D0);
REX_HOOK_RAW(sub_820408D0) {
  const uint32_t event = ctx.r3.u32;
  const uint32_t player = event != 0 ? gw2::LoadBE32(base, event + 4) : 0;
  const uint32_t index = gw2::players::IsPlayer(base, player)
                             ? gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset)
                             : gw2::players::kRecordCount;
  if (index >= gw2::players::kRecordCount) {
    __imp__sub_820408D0(ctx, base);
    return;
  }
  const uint32_t lives = gw2::LoadBE32(base, gw2::players::LivesAddress(index));
  const uint32_t bombs = gw2::LoadBE32(base, gw2::players::BombsAddress(index));
  const uint32_t score = gw2::players::kFirstRecordAddress + index * gw2::players::kRecordStride + 40;
  const int64_t score_before = int64_t(gw2::LoadBE64(base, score));
  const uint32_t enemy = ctx.r4.u32;
  __imp__sub_820408D0(ctx, base);
  gw2::run::OnKill(base, event, enemy, player, int64_t(gw2::LoadBE64(base, score)) - score_before);
  if (gw2::LoadBE32(base, gw2::players::LivesAddress(index)) == lives + 1) {
    gw2::rumble::RequestExtraLifePulse(index);
  }
  if (gw2::LoadBE32(base, gw2::players::BombsAddress(index)) == bombs + 1) {
    gw2::rumble::RequestExtraBombPulse(index);
  }
}

// Player constructor, called by the lives-decrement block of the frame update for each ship it spawns.
REX_EXTERN(__imp__sub_82051448);
REX_HOOK_RAW(sub_82051448) {
  const uint32_t index = ctx.r7.u32;
  __imp__sub_82051448(ctx, base);
  if (index < gw2::players::kRecordCount) {
    gw2::rumble::PlayerSpawned(index);
  }
}
