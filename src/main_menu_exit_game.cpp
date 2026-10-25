#include <cstdint>

#include <rex/hook.h>

#include "hud_colour_hooks.h"
#include "master_page.h"

namespace {

constexpr uint32_t kReturnToArcadeTextId = 7;
constexpr uint32_t kExitGameTextId = 9;
constexpr uint32_t kReturnToArcadeAction = 0x8208EFC0;

}  // namespace

// Adds a menu row: r3 the text id, r4 the input callback, r5 the draw callback, r6-r10 the rest.
// Only the main menu's last item is built with this text id and callback. Keep its existing title-exit action
// and show the pause menu's Exit game caption instead of Return to arcade. Help and options also gains its Colour
// row here (hud_colour_hooks.cpp). The main menu keeps the game's own rows: Watch replays and Custom modes are in the
// panels beside it (main_menu_panels.h).
REX_EXTERN(__imp__sub_820900A8);
REX_HOOK_RAW(sub_820900A8) {
  if (ctx.r3.u32 == kReturnToArcadeTextId && ctx.r4.u32 == kReturnToArcadeAction) {
    ctx.r3.u64 = kExitGameTextId;
  }
  const PPCContext row = ctx;
  if (gw2::master_page::SkipRow(row)) return;  // the Master page has two tiles
  __imp__sub_820900A8(ctx, base);
  gw2::AfterMenuRowAdded(row, base);
}
