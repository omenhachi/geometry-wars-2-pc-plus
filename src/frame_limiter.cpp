#include "frame_limiter.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

#include <rex/hook.h>

#include "crash_report.h"
#include "custom_modes.h"
#include "demo.h"
#include "guest_memory.h"
#include "visual_interpolation.h"

namespace gw2 {
namespace {

constexpr uint32_t kPresentIntervalMask = 0xF00;
// The float the game's update reads as its time step, set once from the video mode's refresh rate.
constexpr uint32_t kGameDeltaAddress = 0x8219D280;
// Effect clock advanced by sub_82090CD8 outside the simulation update.
constexpr uint32_t kVisualTimeAddress = 0x821EBA28;
constexpr uint32_t kVisualPhaseAddress = 0x821EBA34;
constexpr uint32_t kVisualPulseAddress = 0x821993D0;
constexpr double kVisualStep = 1.0 / 60.0;

FrameLimiter g_limiter;

struct GameDeltaState {
  std::optional<uint32_t> original_bits;
  std::optional<LimiterClock::time_point> previous_frame;
  std::optional<LimiterClock::time_point> previous_simulation;
  GameDeltaClock clock;
  SimulationPacer pacer;
  bool gated = false;
  bool modified = false;
};

GameDeltaState g_delta;
std::mutex g_delta_mutex;

struct VisualClockState {
  std::optional<LimiterClock::time_point> previous;
  FixedStepGate gate;
};

VisualClockState g_visual_clock;

double SecondsSinceEpoch(LimiterClock::time_point now) {
  return std::chrono::duration<double>(now.time_since_epoch()).count();
}

// The game's own step, not the delta written for the last tick, which can span several steps.
double InterpolationStep() {
  std::lock_guard lock(g_delta_mutex);
  const float step = g_delta.original_bits ? std::bit_cast<float>(*g_delta.original_bits) : float(kVisualStep);
  if (!(step > 0.0f)) {
    return kVisualStep;
  }
  return std::clamp(double(step), kGameDeltaMin, kGameDeltaMax);
}

double InterpolationInitialInterval(int fps) {
  if (fps == kFrameRateUnlimited) {
    return kVisualStep;
  }
  return fps > kFrameRateDefault ? 1.0 / double(fps) : kVisualStep;
}

bool CaptureOriginal(GameDeltaState& state, uint8_t* base) {
  if (state.original_bits) {
    return true;
  }
  const uint32_t bits = LoadBE32(base, kGameDeltaAddress);
  if (bits == 0) {
    return false;
  }
  state.original_bits = bits;
  return true;
}

void SetGated(GameDeltaState& state, uint8_t* base, bool gated) {
  if (state.gated == gated) {
    return;
  }
  state.gated = gated;
  state.previous_frame.reset();
  state.previous_simulation.reset();
  state.clock.Reset();
  state.pacer.Reset();
  if (state.modified && state.original_bits) {
    StoreBE32(base, kGameDeltaAddress, *state.original_bits);
    state.modified = false;
  }
}

}  // namespace

void PaceGuestFrame() {
  if (const auto deadline = g_limiter.Next(ProjectLimiterRate(ConfiguredFrameRate()), LimiterClock::now())) {
    WaitUntil(*deadline);
  }
}

void SyncGameDelta(uint8_t* base, LimiterClock::time_point now) {
  std::lock_guard lock(g_delta_mutex);
  GameDeltaState& state = g_delta;
  if (!CaptureOriginal(state, base)) {
    return;
  }
  const int fps = ConfiguredFrameRate();
  SetGated(state, base, GatesSimulation(fps));
  if (state.gated) {
    return;
  }
  const auto previous = state.previous_frame;
  state.previous_frame = now;
  const float original = std::bit_cast<float>(*state.original_bits);
  const double interval = previous ? std::chrono::duration<double>(now - *previous).count() : 0.0;
  if (KeepsOriginalGameDelta(fps)) {
    if (previous && original > 0.0f) {
      const double delta = state.clock.Take(interval, original);
      StoreBE32(base, kGameDeltaAddress, std::bit_cast<uint32_t>(float(delta)));
      state.modified = true;
    } else if (state.modified) {
      StoreBE32(base, kGameDeltaAddress, *state.original_bits);
      state.modified = false;
    }
    return;
  }
  state.clock.Reset();
  if (!previous) {
    return;
  }
  const float delta = GameFrameDelta(interval);
  StoreBE32(base, kGameDeltaAddress, std::bit_cast<uint32_t>(delta));
  state.modified = true;
}

bool BeginSimulationTick(uint8_t* base, LimiterClock::time_point now) {
  std::lock_guard lock(g_delta_mutex);
  GameDeltaState& state = g_delta;
  if (!CaptureOriginal(state, base)) {
    return true;
  }
  const int fps = ConfiguredFrameRate();
  SetGated(state, base, GatesSimulation(fps));
  if (!state.gated) {
    return true;
  }
  const float original = std::bit_cast<float>(*state.original_bits);
  if (!(original > 0.0f)) {
    return true;
  }
  if (!state.previous_simulation) {
    state.previous_simulation = now;
    return true;
  }
  const double interval = std::chrono::duration<double>(now - *state.previous_simulation).count();
  state.previous_simulation = now;
  double delta = original;
  if (!state.pacer.Tick(interval, original, delta)) {
    return false;
  }
  StoreBE32(base, kGameDeltaAddress, std::bit_cast<uint32_t>(float(delta)));
  state.modified = true;
  return true;
}

double SimulationRemainder() {
  std::lock_guard lock(g_delta_mutex);
  return g_delta.pacer.remainder();
}

bool BeginVisualClockTick(LimiterClock::time_point now) {
  if (!GatesSimulation(ConfiguredFrameRate())) {
    g_visual_clock.previous.reset();
    g_visual_clock.gate.Reset();
    return true;
  }
  if (!g_visual_clock.previous) {
    g_visual_clock.previous = now;
    g_visual_clock.gate.Reset();
    return true;
  }
  const double interval = std::chrono::duration<double>(now - *g_visual_clock.previous).count();
  g_visual_clock.previous = now;
  return g_visual_clock.gate.Tick(interval, kVisualStep);
}

VisualClockSnapshot SaveVisualClock(uint8_t* base) {
  return {LoadBE32(base, kVisualTimeAddress), LoadBE32(base, kVisualPhaseAddress),
          LoadBE32(base, kVisualPulseAddress)};
}

void RestoreVisualClock(uint8_t* base, const VisualClockSnapshot& snapshot) {
  StoreBE32(base, kVisualTimeAddress, snapshot.time);
  StoreBE32(base, kVisualPhaseAddress, snapshot.phase);
  StoreBE32(base, kVisualPulseAddress, snapshot.pulse);
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

// The game's simulation update; input sampling and the render/present path stay on the outer loop.
REX_EXTERN(__imp__sub_820366A0);
REX_HOOK_RAW(sub_820366A0) {
  if (gw2::BeginSimulationTick(base, gw2::LimiterClock::now())) {
    gw2::custom_modes::BeforeSimTick(base);
    gw2::demo::BeforeSimTick(base);
    __imp__sub_820366A0(ctx, base);
    gw2::custom_modes::AfterSimTick(base);
    gw2::demo::OnSimTick(base);
    gw2::VisualInterpolationSimulationTick(gw2::SecondsSinceEpoch(gw2::LimiterClock::now()),
                                           gw2::SimulationRemainder());
  }
}

// The scene renderer rebuilds its vertex batches from object transforms every frame.
REX_EXTERN(__imp__sub_82065E38);
REX_HOOK_RAW(sub_82065E38) {
  const int fps = gw2::ConfiguredFrameRate();
  const gw2::InterpolationScheduler::Frame interpolation =
      gw2::VisualInterpolationBeginFrame(gw2::SecondsSinceEpoch(gw2::LimiterClock::now()),
                                         gw2::InterpolationPolicyFor(fps), gw2::InterpolationStep(),
                                         gw2::InterpolationInitialInterval(fps));
  static bool reported = false;
  static bool reported_active = false;
  if (gw2::GatesSimulation(fps) && interpolation.render_hz > 0.0 &&
      (!reported || interpolation.wanted != reported_active)) {
    reported = true;
    reported_active = interpolation.wanted;
    REXLOG_INFO("Visual interpolation {}: guest render rate {:.1f} Hz", reported_active ? "enabled" : "inactive",
                interpolation.render_hz);
    gw2::SetCrashInterpolationState(reported_active, interpolation.render_hz);
  }
  __imp__sub_82065E38(ctx, base);
}

// One gameplay object's per-frame draw. Interpolated values are written and restored around the draw.
REX_EXTERN(__imp__sub_82045740);
REX_HOOK_RAW(sub_82045740) {
  const gw2::ObjectTransformOverride override = gw2::VisualInterpolationPrepareObject(base, ctx.r3.u32);
  __imp__sub_82045740(ctx, base);
  gw2::VisualInterpolationRestoreObject(base, override);
}

// The effect clock is advanced by sub_82090CD8; keep its state frozen on render-only frames.
REX_EXTERN(__imp__sub_82090CD8);
REX_HOOK_RAW(sub_82090CD8) {
  if (gw2::BeginVisualClockTick(gw2::LimiterClock::now())) {
    __imp__sub_82090CD8(ctx, base);
    return;
  }
  const gw2::VisualClockSnapshot snapshot = gw2::SaveVisualClock(base);
  __imp__sub_82090CD8(ctx, base);
  gw2::RestoreVisualClock(base, snapshot);
}
