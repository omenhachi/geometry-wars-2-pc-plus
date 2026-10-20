#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gw2 {

inline constexpr int kFrameRateDefault = 60;
inline constexpr int kFrameRateUnlimited = 0;
inline constexpr int kFrameRates[] = {30, 60, 90, 120, 144, 165, 240, kFrameRateUnlimited};

inline bool IsFrameRateChoice(int fps) {
  for (const int rate : kFrameRates) {
    if (rate == fps) {
      return true;
    }
  }
  return false;
}

inline bool IsHighFrameRate(int fps) {
  return fps == kFrameRateUnlimited || fps > kFrameRateDefault;
}

inline bool GatesSimulation(int fps) {
  return IsHighFrameRate(fps);
}

// Original: the game presents at its guest vblank, which always stays at the SDK's 60 Hz. Immediate: the game's
// present callback swaps at once, so only the rates above 60 leave the vblank pacing.
enum class PresentMode { kOriginal, kImmediate };

inline PresentMode PresentModeFor(int fps) {
  return IsHighFrameRate(fps) ? PresentMode::kImmediate : PresentMode::kOriginal;
}

// 60 is held by the original pacing and Unlimited by nothing; the limiter paces the other rates.
inline int ProjectLimiterRate(int fps) {
  return fps == kFrameRateDefault ? kFrameRateUnlimited : fps;
}

using LimiterClock = std::chrono::steady_clock;

inline constexpr double kGameDeltaMin = 1.0 / 1000.0;
inline constexpr double kGameDeltaMax = 1.0 / 20.0;

// The time step the game's update receives for a frame that took `interval_seconds`.
inline float GameFrameDelta(double interval_seconds) {
  return float(std::clamp(interval_seconds, kGameDeltaMin, kGameDeltaMax));
}

// Only 60 keeps the game's own time step while frames are on time (GameDeltaClock).
inline bool KeepsOriginalGameDelta(int fps) {
  return fps == kFrameRateDefault;
}

// Three steps of 1/60 are the 50 ms limit of GameFrameDelta.
inline constexpr int kMaxGameSteps = 3;

// The time step for a frame that took `interval` seconds, given the game's own `step`: a whole number of steps when the
// frame is close to one (the game's own speed), otherwise the real interval. The difference carries over, so the steps
// add up to real time; a frame past the cap loses the excess, and a frame shorter than 1 ms is padded up to it.
class GameDeltaClock {
 public:
  // Also the most, in steps, that the game's time can lead or lag real time (frames of 1 ms or more, no time dropped).
  static constexpr double kSnapTolerance = 0.2;

  double Take(double interval, double step) {
    const double target = interval + owed_;
    const double steps = target / step;
    const double nearest = std::clamp(std::round(steps), 1.0, double(kMaxGameSteps));
    const double cap = kMaxGameSteps * step;
    double delta = std::fabs(steps - nearest) <= kSnapTolerance ? nearest * step : target;
    delta = std::clamp(delta, kGameDeltaMin, cap);
    owed_ = target > cap ? 0.0 : std::clamp(target - delta, -0.5 * step, 0.5 * step);
    return delta;
  }

  void Reset() { owed_ = 0.0; }

  // Real time not yet in the steps given so far; negative when they are ahead of it.
  double owed() const { return owed_; }

 private:
  double owed_ = 0.0;
};

// Runs the original fixed step while presentation is faster. Frames at or below the step use the GameDeltaClock
// correction; shorter frames accumulate until a step is due.
class SimulationPacer {
 public:
  // Returns true when simulation should run; `delta` is the time step for that tick.
  bool Tick(double interval, double step, double& delta) {
    if (interval >= step) {
      delta = clock_.Take(interval + remainder_, step);
      remainder_ = clock_.owed();
      clock_.Reset();
      return true;
    }
    remainder_ += interval;
    if (remainder_ + kTickTolerance < step) {
      return false;
    }
    remainder_ -= step;
    delta = step;
    return true;
  }

  void Reset() {
    remainder_ = 0.0;
    clock_.Reset();
  }

 private:
  static constexpr double kTickTolerance = 1e-9;

  double remainder_ = 0.0;
  GameDeltaClock clock_;
};

// Runs a fixed-rate visual update while presentation is faster, without catch-up.
class FixedStepGate {
 public:
  bool Tick(double interval, double step) {
    if (interval >= step) {
      remainder_ = 0.0;
      return true;
    }
    remainder_ += interval;
    if (remainder_ + kTickTolerance < step) {
      return false;
    }
    remainder_ -= step;
    return true;
  }

  void Reset() { remainder_ = 0.0; }

 private:
  static constexpr double kTickTolerance = 1e-9;

  double remainder_ = 0.0;
};

inline LimiterClock::duration FramePeriod(int fps) {
  if (fps <= 0) {
    return LimiterClock::duration::zero();
  }
  return std::chrono::duration_cast<LimiterClock::duration>(std::chrono::duration<double>(1.0 / fps));
}

// The schedule of the frame limiter. Next() gives the time the caller has to wait for, if any.
class FrameLimiter {
 public:
  std::optional<LimiterClock::time_point> Next(int fps, LimiterClock::time_point now) {
    const auto period = FramePeriod(fps);
    if (period == LimiterClock::duration::zero()) {
      armed_ = false;
      return std::nullopt;
    }
    if (!armed_ || fps != fps_) {
      armed_ = true;
      fps_ = fps;
      target_ = now + period;
      return std::nullopt;
    }
    const auto deadline = target_;
    target_ = now - deadline > period ? now + period : deadline + period;
    if (deadline <= now) {
      return std::nullopt;
    }
    return deadline;
  }

 private:
  bool armed_ = false;
  int fps_ = 0;
  LimiterClock::time_point target_;
};

// Blocks the calling thread until the deadline: a high-resolution timer for most of it, then a short spin.
void WaitUntil(LimiterClock::time_point deadline);

// The selected frame rate, or the default when the cvar holds something else.
int ConfiguredFrameRate();

// Called by the swap hook before the game's swap: waits for the next frame time.
void PaceGuestFrame();

// Sets the game's per-update time step for the next update: its own step while frames are on time at 60, otherwise the
// real frame interval. High-refresh rates leave the step to BeginSimulationTick.
void SyncGameDelta(uint8_t* base, LimiterClock::time_point now);

// Returns whether the game's update should run on this outer-loop frame, writing its time step when it should.
bool BeginSimulationTick(uint8_t* base, LimiterClock::time_point now);

// Returns whether the effect clock should advance on this outer-loop frame.
bool BeginVisualClockTick(LimiterClock::time_point now);

struct VisualClockSnapshot {
  uint32_t time;
  uint32_t phase;
  uint32_t pulse;
};

VisualClockSnapshot SaveVisualClock(uint8_t* base);
void RestoreVisualClock(uint8_t* base, const VisualClockSnapshot& snapshot);

// DEBUG_SAULO: temporary fixed-rate timing counters; remove with the diagnostics.
void DebugSauloTimingLoopFrame();
void DebugSauloTimingSimulationTick();
void DebugSauloTimingSkipped();
void DebugSauloTimingPresent(uint8_t* base);
void DebugSauloTimingVisualClock(bool advanced);
void DebugSauloTimingEnemyUpdate();

}  // namespace gw2
