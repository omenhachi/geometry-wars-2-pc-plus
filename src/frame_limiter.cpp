#include "frame_limiter.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "guest_memory.h"
#include "launcher_cvars.h"

namespace gw2 {
namespace {

using namespace std::chrono_literals;

constexpr auto kSpinWindow = 500us;
constexpr uint32_t kPresentIntervalMask = 0xF00;
// The float the game's update reads as its time step, set once from the video mode's refresh rate.
constexpr uint32_t kGameDeltaAddress = 0x8219D280;

FrameLimiter g_limiter;

struct GameDeltaState {
  std::optional<uint32_t> original_bits;
  std::optional<LimiterClock::time_point> previous_frame;
  bool modified = false;
};

GameDeltaState g_delta;

}  // namespace

void WaitUntil(LimiterClock::time_point deadline) {
  static const HANDLE timer =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto coarse = deadline - LimiterClock::now() - kSpinWindow;
  if (coarse > LimiterClock::duration::zero()) {
    if (timer) {
      LARGE_INTEGER due;
      due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(coarse).count() / 100;
      if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
      }
    } else {
      std::this_thread::sleep_for(coarse);
    }
  }
  while (LimiterClock::now() < deadline) {
    std::this_thread::yield();
  }
}

int ConfiguredFrameRate() {
  const int fps = REXCVAR_GET(gw2_frame_rate);
  return IsFrameRateChoice(fps) ? fps : kFrameRateDefault;
}

void PaceGuestFrame() {
  if (const auto deadline = g_limiter.Next(ProjectLimiterRate(ConfiguredFrameRate()), LimiterClock::now())) {
    WaitUntil(*deadline);
  }
}

void SyncGameDelta(uint8_t* base, LimiterClock::time_point now) {
  GameDeltaState& state = g_delta;
  if (!state.original_bits) {
    const uint32_t bits = LoadBE32(base, kGameDeltaAddress);
    if (bits == 0) {
      return;
    }
    state.original_bits = bits;
  }
  const int fps = ConfiguredFrameRate();
  const auto previous = state.previous_frame;
  state.previous_frame = now;
  if (KeepsOriginalGameDelta(fps)) {
    if (state.modified) {
      StoreBE32(base, kGameDeltaAddress, *state.original_bits);
      state.modified = false;
    }
    return;
  }
  if (!previous) {
    return;
  }
  const float delta = GameFrameDelta(std::chrono::duration<double>(now - *previous).count());
  StoreBE32(base, kGameDeltaAddress, std::bit_cast<uint32_t>(delta));
  state.modified = true;
}

}  // namespace gw2

// The game's present callback. Its argument carries the present interval in bits 8-11: zero swaps at once instead of
// at the next guest vblank, so higher rates do not need a faster vblank.
REX_EXTERN(__imp__sub_820A1FA0);
REX_HOOK_RAW(sub_820A1FA0) {
  if (gw2::PresentModeFor(gw2::ConfiguredFrameRate()) == gw2::PresentMode::kImmediate) {
    ctx.r3.u32 &= ~gw2::kPresentIntervalMask;
  }
  __imp__sub_820A1FA0(ctx, base);
}
