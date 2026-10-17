#include <bit>

#include <rex/hook.h>

#include "guest_memory.h"
#include "king_records.h"
#include "rumble.h"

namespace {

float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(gw2::LoadBE32(base, addr));
}

}  // namespace

// The King circle's update, reached through its vtable. It runs after the game has moved the circle this step.
REX_EXTERN(__imp__sub_8204AEE8);
REX_HOOK_RAW(sub_8204AEE8) {
  const uint32_t zone = ctx.r3.u32;
  __imp__sub_8204AEE8(ctx, base);
  if (gw2::LoadBE32(base, gw2::king::kModeAddress) != gw2::king::kKingMode ||
      gw2::LoadBE32(base, zone + gw2::king::kZoneStateOffset) != gw2::king::kZoneShrinkingState) {
    return;
  }
  const float full = LoadFloat(base, gw2::king::kFullRadiusAddress);
  const float radius = LoadFloat(base, zone + gw2::king::kZoneRadiusOffset);
  if (!(full > 1.0f)) {
    return;
  }
  gw2::rumble::ObserveKingZone(LoadFloat(base, zone + gw2::king::kZoneCenterXOffset),
                               LoadFloat(base, zone + gw2::king::kZoneCenterYOffset), radius,
                               (radius - 1.0f) / (full - 1.0f));
}
