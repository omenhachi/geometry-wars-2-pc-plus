#include <rex/hook.h>

#include "gravity_well_records.h"
#include "guest_memory.h"
#include "rumble.h"

// The well's per-frame update. Its exploding byte is set only by the overload branch, which releases protons unless silent.
REX_EXTERN(__imp__sub_82049370);
REX_HOOK_RAW(sub_82049370) {
  const uint32_t well = ctx.r3.u32;
  const bool was_exploding = base[well + gw2::gravity::kExplodingOffset] != 0;
  const bool silent = base[well + gw2::gravity::kSilentOffset] != 0;
  __imp__sub_82049370(ctx, base);
  if (!was_exploding && !silent && base[well + gw2::gravity::kExplodingOffset] != 0) {
    gw2::rumble::RequestGravityOverloadPulse();
  }
}

// The lethal-hit death. It is not called by overload, and a repeated hit on a dead well does not pulse again.
REX_EXTERN(__imp__sub_820497C0);
REX_HOOK_RAW(sub_820497C0) {
  const uint32_t well = ctx.r3.u32;
  const bool was_dead = (gw2::LoadBE32(base, well + gw2::gravity::kFlagsOffset) & gw2::gravity::kDeadBit) != 0;
  __imp__sub_820497C0(ctx, base);
  if (!was_dead && (gw2::LoadBE32(base, well + gw2::gravity::kFlagsOffset) & gw2::gravity::kDeadBit) != 0) {
    gw2::rumble::RequestGravityExplosionPulse();
  }
}
