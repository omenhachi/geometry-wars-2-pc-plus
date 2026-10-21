#include <cstdint>

#include <rex/hook.h>

namespace {

constexpr uint32_t kReturnToArcadeTextId = 7;
constexpr uint32_t kExitGameTextId = 9;
constexpr uint32_t kReturnToArcadeAction = 0x8208EFC0;

}  // namespace

// Only the main menu's last item is built with this text id and callback. Keep its existing title-exit action
// and show the pause menu's Exit game caption instead of Return to arcade.
REX_EXTERN(__imp__sub_820900A8);
REX_HOOK_RAW(sub_820900A8) {
  if (ctx.r3.u32 == kReturnToArcadeTextId && ctx.r4.u32 == kReturnToArcadeAction) {
    ctx.r3.u64 = kExitGameTextId;
  }
  __imp__sub_820900A8(ctx, base);
}
