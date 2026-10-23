// Visual effects draw from their own random numbers, so the gameplay generator only ever serves gameplay.
//
// The game has one Mersenne Twister (kGameplayTwister, seeded once at boot) for everything. Its effect spawners
// (sub_82030C58, sub_820310C8, sub_820314C8) first ask whether the effects buffer has room (sub_82062FC8), and the
// glow pool (sub_82074860) whether it has a free entry, before drawing from it. Those buffers empty as frames are
// drawn, so how many effects get made, and how many numbers they take, depends on the frame rate: one busy moment
// shifts every spawn after it. That makes a run impossible to replay (demo.cpp). Draws made inside those functions are
// redirected to a second twister that starts as a copy of the first; gameplay is as random as ever.

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "demo.h"

namespace {

constexpr uint32_t kGameplayTwister = 0x821A1E28;
constexpr uint32_t kTwisterSize = 2500;  // 624 words, then the next index

thread_local int g_effect_depth = 0;
uint32_t g_effect_twister = 0;

uint32_t EffectTwister(uint8_t* base) {
  if (g_effect_twister == 0) {
    const uint32_t twister = rex::system::kernel_memory()->SystemHeapAlloc(kTwisterSize);
    if (twister == 0) return 0;
    std::memcpy(base + twister, base + kGameplayTwister, kTwisterSize);
    g_effect_twister = twister;
  }
  return g_effect_twister;
}

}  // namespace

#define EFFECT_RANDOM_HOOK(spawner) \
  REX_EXTERN(__imp__##spawner);     \
  REX_HOOK_RAW(spawner) {           \
    ++g_effect_depth;               \
    __imp__##spawner(ctx, base);    \
    --g_effect_depth;               \
  }
EFFECT_RANDOM_HOOK(sub_82030C58)
EFFECT_RANDOM_HOOK(sub_820310C8)
EFFECT_RANDOM_HOOK(sub_820314C8)
EFFECT_RANDOM_HOOK(sub_82074860)

// The twister's draw (r3 its state).
REX_EXTERN(__imp__sub_82030058);
REX_HOOK_RAW(sub_82030058) {
  if (g_effect_depth > 0 && ctx.r3.u32 == kGameplayTwister) {
    if (const uint32_t effects = EffectTwister(base)) ctx.r3.u64 = effects;
  } else {
    gw2::demo::OnTwisterDraw(uint32_t(ctx.lr));
  }
  __imp__sub_82030058(ctx, base);
}
