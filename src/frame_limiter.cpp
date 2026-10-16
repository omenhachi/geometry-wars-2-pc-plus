#include "frame_limiter.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"

namespace gw2 {
namespace {

constexpr uint32_t kPresentIntervalMask = 0xF00;
// The float the game's update reads as its time step, set once from the video mode's refresh rate.
constexpr uint32_t kGameDeltaAddress = 0x8219D280;

FrameLimiter g_limiter;

struct GameDeltaState {
  std::optional<uint32_t> original_bits;
  std::optional<LimiterClock::time_point> previous_frame;
  GameDeltaClock clock;
  bool modified = false;
};

GameDeltaState g_delta;

// DEBUG_SAULO: temporary timing log, at most one line per second of play. A window is logged when its frame rate
// moved by more than 10 percent, simulated time left real time by more than 3 percent, or (at 60) it held a short or
// capped frame; a steady rate between 35 and 55 FPS is logged every tenth window.
class DebugSauloFrameTiming {
 public:
  void Add(int mode, double interval, double delta, double step, double owed) {
    if (!announced_) {
      announced_ = true;
      REXLOG_INFO(
          "DEBUG_SAULO frame timing: step_ms={:.3f} snap_tolerance={} short=interval<{:.1f}ms capped=interval>{:.1f}ms",
          step * 1000.0, GameDeltaClock::kSnapTolerance, kShortSteps * step * 1000.0, kMaxGameSteps * step * 1000.0);
    }
    if (window_.frames > 0 && window_.mode != mode) {
      window_ = Window{};
    }
    window_.Add(mode, interval, delta, step, owed);
    if (window_.real >= 1.0) {
      Report();
      window_ = Window{};
    }
  }

 private:
  static constexpr double kShortSteps = 0.5;
  static constexpr int kMaxLines = 400;

  struct Window {
    int mode = 0;
    int frames = 0;
    int short_frames = 0;
    int capped = 0;
    int snapped[kMaxGameSteps] = {};
    int continuous = 0;
    double real = 0.0;
    double sim = 0.0;
    double shortest = 1e9;
    double longest = 0.0;
    double owed = 0.0;

    void Add(int configured_mode, double interval, double delta, double step, double carry) {
      mode = configured_mode;
      owed = carry;
      ++frames;
      short_frames += interval < kShortSteps * step;
      const double steps = delta / step;
      const double whole = std::round(steps);
      if (interval > kMaxGameSteps * step) {
        ++capped;
      } else if (std::fabs(steps - whole) < 1e-4) {
        ++snapped[std::clamp(int(whole), 1, kMaxGameSteps) - 1];
      } else {
        ++continuous;
      }
      real += interval;
      sim += delta;
      shortest = std::min(shortest, interval);
      longest = std::max(longest, interval);
    }
  };

  void Report() {
    const Window& w = window_;
    const double fps = w.frames / w.real;
    const double ratio = w.sim / w.real;
    const bool moved = std::fabs(fps - last_fps_) > 0.1 * last_fps_;
    last_fps_ = fps;
    const bool bursts = w.mode == kFrameRateDefault && (w.short_frames > 0 || w.capped > 0);
    const bool event = moved || bursts || std::fabs(ratio - 1.0) > 0.03;
    const bool steady_middle = fps >= 35.0 && fps < 55.0 && quiet_ >= 9;
    if (!event && !steady_middle) {
      ++quiet_;
      return;
    }
    quiet_ = 0;
    if (lines_ > kMaxLines) {
      return;
    }
    if (lines_++ == kMaxLines) {
      REXLOG_INFO("DEBUG_SAULO frame timing: line limit reached");
      return;
    }
    REXLOG_INFO(
        "DEBUG_SAULO frame timing: mode={} fps={:.1f} frames={} real_s={:.3f} sim_s={:.3f} sim/real={:.3f} "
        "interval_ms={:.1f}..{:.1f} short={} snapped(1x/2x/3x)={}/{}/{} continuous={} capped={} owed_ms={:+.1f}",
        w.mode, fps, w.frames, w.real, w.sim, ratio, w.shortest * 1000.0, w.longest * 1000.0, w.short_frames,
        w.snapped[0], w.snapped[1], w.snapped[2], w.continuous, w.capped, w.owed * 1000.0);
  }

  Window window_;
  bool announced_ = false;
  double last_fps_ = 0.0;
  int quiet_ = 0;
  int lines_ = 0;
};

DebugSauloFrameTiming g_debug_saulo;

}  // namespace

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
  const float original = std::bit_cast<float>(*state.original_bits);
  const double interval = previous ? std::chrono::duration<double>(now - *previous).count() : 0.0;
  if (KeepsOriginalGameDelta(fps)) {
    if (previous && original > 0.0f) {
      const double delta = state.clock.Take(interval, original);
      StoreBE32(base, kGameDeltaAddress, std::bit_cast<uint32_t>(float(delta)));
      state.modified = true;
      g_debug_saulo.Add(fps, interval, delta, original, state.clock.owed());
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
  if (original > 0.0f) {
    g_debug_saulo.Add(fps, interval, delta, original, 0.0);
  }
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
