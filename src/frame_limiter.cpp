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

struct DebugSauloTimingState {
  std::atomic<uint64_t> loop_frames{0};
  std::atomic<uint64_t> presents{0};
  std::atomic<uint64_t> sim_ticks{0};
  std::atomic<uint64_t> skipped{0};
  std::atomic<uint64_t> visual_calls{0};
  std::atomic<uint64_t> visual_updates{0};
  std::atomic<uint64_t> enemy_updates{0};
  std::atomic<int64_t> last_report_ms{0};
};

DebugSauloTimingState g_debug_saulo;

int64_t DebugSauloNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(LimiterClock::now().time_since_epoch()).count();
}

double SecondsSinceEpoch(LimiterClock::time_point now) {
  return std::chrono::duration<double>(now.time_since_epoch()).count();
}

double InterpolationStep(uint8_t* base) {
  const uint32_t bits = LoadBE32(base, kGameDeltaAddress);
  const float step = bits != 0 ? std::bit_cast<float>(bits) : float(kVisualStep);
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

void DebugSauloTimingLoopFrame() {
  g_debug_saulo.loop_frames.fetch_add(1, std::memory_order_relaxed);
}

void DebugSauloTimingVisualClock(bool advanced) {
  g_debug_saulo.visual_calls.fetch_add(1, std::memory_order_relaxed);
  if (advanced) {
    g_debug_saulo.visual_updates.fetch_add(1, std::memory_order_relaxed);
  }
}

void DebugSauloTimingEnemyUpdate() {
  g_debug_saulo.enemy_updates.fetch_add(1, std::memory_order_relaxed);
}

void DebugSauloTimingSimulationTick() {
  g_debug_saulo.sim_ticks.fetch_add(1, std::memory_order_relaxed);
}

void DebugSauloTimingSkipped() {
  g_debug_saulo.skipped.fetch_add(1, std::memory_order_relaxed);
}

void DebugSauloTimingPresent(uint8_t* base) {
  g_debug_saulo.presents.fetch_add(1, std::memory_order_relaxed);
  const int64_t now = DebugSauloNowMs();
  int64_t last = g_debug_saulo.last_report_ms.load(std::memory_order_relaxed);
  if (last == 0) {
    g_debug_saulo.last_report_ms.store(now, std::memory_order_relaxed);
    return;
  }
  if (now - last < 1000) {
    return;
  }
  if (!g_debug_saulo.last_report_ms.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
    return;
  }
  const double seconds = double(now - last) / 1000.0;
  if (seconds <= 0.0) {
    return;
  }
  const uint64_t loop_frames = g_debug_saulo.loop_frames.exchange(0, std::memory_order_relaxed);
  const uint64_t presents = g_debug_saulo.presents.exchange(0, std::memory_order_relaxed);
  const uint64_t sim_ticks = g_debug_saulo.sim_ticks.exchange(0, std::memory_order_relaxed);
  const uint64_t skipped = g_debug_saulo.skipped.exchange(0, std::memory_order_relaxed);
  const uint64_t visual_calls = g_debug_saulo.visual_calls.exchange(0, std::memory_order_relaxed);
  const uint64_t visual_updates = g_debug_saulo.visual_updates.exchange(0, std::memory_order_relaxed);
  const uint64_t enemy_updates = g_debug_saulo.enemy_updates.exchange(0, std::memory_order_relaxed);
  const InterpolationStats interpolation = TakeInterpolationStats();
  const uint32_t delta_bits = LoadBE32(base, kGameDeltaAddress);
  const float delta = delta_bits != 0 ? std::bit_cast<float>(delta_bits) : 0.0f;
  REXLOG_INFO(
      "DEBUG_SAULO timing selected={} render_fps={:.1f} present_fps={:.1f} presents={} sim_tps={:.1f} sim_ticks={} "
      "skipped={} visual_calls={} visual_updates={} enemy_updates={} interp_frames={} interp_objects={} "
      "interp_snaps={} interp_alpha={:.3f} delta_ms={:.3f}",
      ConfiguredFrameRate(), double(loop_frames) / seconds, double(presents) / seconds, presents,
      double(sim_ticks) / seconds, sim_ticks, skipped, visual_calls, visual_updates, enemy_updates,
      interpolation.frames, interpolation.objects, interpolation.snaps, interpolation.alpha, delta * 1000.0f);
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
  gw2::DebugSauloTimingLoopFrame();
  if (gw2::BeginSimulationTick(base, gw2::LimiterClock::now())) {
    gw2::DebugSauloTimingSimulationTick();
    __imp__sub_820366A0(ctx, base);
    gw2::VisualInterpolationSimulationTick(gw2::SecondsSinceEpoch(gw2::LimiterClock::now()),
                                           gw2::SimulationRemainder());
  } else {
    gw2::DebugSauloTimingSkipped();
  }
}

// The scene renderer rebuilds its vertex batches from object transforms every frame.
REX_EXTERN(__imp__sub_82065E38);
REX_HOOK_RAW(sub_82065E38) {
  const int fps = gw2::ConfiguredFrameRate();
  gw2::VisualInterpolationBeginFrame(gw2::SecondsSinceEpoch(gw2::LimiterClock::now()),
                                    gw2::GatesSimulation(fps), gw2::InterpolationStep(base),
                                    gw2::InterpolationInitialInterval(fps));
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
  const bool advance = gw2::BeginVisualClockTick(gw2::LimiterClock::now());
  gw2::DebugSauloTimingVisualClock(advance);
  if (advance) {
    __imp__sub_82090CD8(ctx, base);
    return;
  }
  const gw2::VisualClockSnapshot snapshot = gw2::SaveVisualClock(base);
  __imp__sub_82090CD8(ctx, base);
  gw2::RestoreVisualClock(base, snapshot);
}

REX_EXTERN(__imp__sub_820435F0);
REX_HOOK_RAW(sub_820435F0) {
  gw2::DebugSauloTimingEnemyUpdate();
  __imp__sub_820435F0(ctx, base);
}

REX_EXTERN(__imp__sub_82056A18);
REX_HOOK_RAW(sub_82056A18) {
  gw2::DebugSauloTimingEnemyUpdate();
  __imp__sub_82056A18(ctx, base);
}

REX_EXTERN(__imp__sub_820453E0);
REX_HOOK_RAW(sub_820453E0) {
  gw2::DebugSauloTimingEnemyUpdate();
  __imp__sub_820453E0(ctx, base);
}
