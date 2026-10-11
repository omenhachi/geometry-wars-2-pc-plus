#pragma once

#include <algorithm>
#include <chrono>
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

// Only 60 keeps the game's own time step.
inline bool KeepsOriginalGameDelta(int fps) {
  return fps == kFrameRateDefault;
}

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

// Sets the game's per-update time step for the next update. Rates other than 60 get the real frame interval.
void SyncGameDelta(uint8_t* base, LimiterClock::time_point now);

}  // namespace gw2
