#include "visual_interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <mutex>

#include "frame_limiter.h"
#include "guest_memory.h"

namespace gw2 {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kAlphaEpsilon = 1.0f / 4096.0f;
constexpr float kTransformEpsilon = 1.0f / 4096.0f;
constexpr uint32_t kObjectX = 4;
constexpr uint32_t kObjectY = 8;
constexpr uint32_t kObjectAngle = 20;
constexpr uint32_t kObjectScaleX = 24;
constexpr uint32_t kObjectScaleY = 28;
constexpr uint32_t kObjectType = 68;
// A teleport, respawn or level transition moves an object much farther than one 60 Hz step.
constexpr float kSnapDistance = 8.0f;

InterpolationScheduler g_scheduler;
TransformHistory g_history;
InterpolationScheduler::Frame g_frame;
bool g_initialized = false;
std::mutex g_scheduler_mutex;

TransformSample ReadTransform(uint8_t* base, uint32_t object) {
  return {
      std::bit_cast<float>(LoadBE32(base, object + kObjectX)),
      std::bit_cast<float>(LoadBE32(base, object + kObjectY)),
      std::bit_cast<float>(LoadBE32(base, object + kObjectAngle)),
      std::bit_cast<float>(LoadBE32(base, object + kObjectScaleX)),
      std::bit_cast<float>(LoadBE32(base, object + kObjectScaleY)),
  };
}

void WriteTransform(uint8_t* base, uint32_t object, const TransformSample& value) {
  StoreBE32(base, object + kObjectX, std::bit_cast<uint32_t>(value.x));
  StoreBE32(base, object + kObjectY, std::bit_cast<uint32_t>(value.y));
  StoreBE32(base, object + kObjectAngle, std::bit_cast<uint32_t>(value.angle));
  StoreBE32(base, object + kObjectScaleX, std::bit_cast<uint32_t>(value.scale_x));
  StoreBE32(base, object + kObjectScaleY, std::bit_cast<uint32_t>(value.scale_y));
}

bool NearlyEqual(float a, float b, float epsilon) {
  return std::fabs(a - b) <= epsilon;
}

bool SameTransform(const TransformSample& a, const TransformSample& b) {
  return NearlyEqual(a.x, b.x, kTransformEpsilon) && NearlyEqual(a.y, b.y, kTransformEpsilon) &&
         NearlyEqual(ShortestAngleDelta(a.angle, b.angle), 0.0f, kTransformEpsilon) &&
         NearlyEqual(a.scale_x, b.scale_x, kTransformEpsilon) &&
         NearlyEqual(a.scale_y, b.scale_y, kTransformEpsilon);
}

}  // namespace

float ShortestAngleDelta(float from, float to) {
  return std::remainder(to - from, kTwoPi);
}

TransformSample InterpolateTransform(const TransformSample& previous, const TransformSample& current, float alpha) {
  const float a = std::clamp(alpha, 0.0f, 1.0f);
  TransformSample value;
  value.x = previous.x + (current.x - previous.x) * a;
  value.y = previous.y + (current.y - previous.y) * a;
  value.angle = previous.angle + ShortestAngleDelta(previous.angle, current.angle) * a;
  value.scale_x = previous.scale_x + (current.scale_x - previous.scale_x) * a;
  value.scale_y = previous.scale_y + (current.scale_y - previous.scale_y) * a;
  return value;
}

bool IsTransformDiscontinuity(const TransformSample& previous, const TransformSample& current, float max_distance) {
  const float dx = current.x - previous.x;
  const float dy = current.y - previous.y;
  return dx * dx + dy * dy > max_distance * max_distance;
}

void InterpolationScheduler::Reset(double initial_interval) {
  has_tick_ = false;
  last_tick_ = 0.0;
  tick_remainder_ = 0.0;
  new_tick_ = false;
  frame_index_ = 0;
  frames_until_tick_ = 1;
  has_frame_ = false;
  last_frame_ = 0.0;
  smoothed_ = std::clamp(initial_interval, 1e-6, 1.0);
  frame_count_ = 0;
  window_start_ = 0.0;
  window_frames_ = 0;
  render_hz_ = 0.0;
  agreeing_windows_ = 0;
  policy_ = InterpolationPolicy::kOff;
  wanted_ = false;
  enabled_ = false;
  tick_ = 0;
  ++epoch_;
}

void InterpolationScheduler::Prepare(double initial_interval) {
  if (has_frame_) {
    return;
  }
  smoothed_ = std::clamp(initial_interval, 1e-6, 1.0);
}

void InterpolationScheduler::SimulationTick(double now, double remainder) {
  if (has_tick_ && now - last_tick_ > kHistoryGap) {
    ++epoch_;
  }
  last_tick_ = now;
  tick_remainder_ = remainder;
  has_tick_ = true;
  new_tick_ = true;
  ++tick_;
}

void InterpolationScheduler::MeasureRenderRate(double now, double interval, double step) {
  if (interval > kHistoryGap) {
    window_start_ = now;
    window_frames_ = 0;
    return;
  }
  ++window_frames_;
  const double elapsed = now - window_start_;
  if (elapsed < kRateWindow) {
    return;
  }
  render_hz_ = window_frames_ / elapsed;
  window_start_ = now;
  window_frames_ = 0;
  if (policy_ != InterpolationPolicy::kMeasured) {
    return;
  }
  const double enable_hz = 1.0 / (step * 0.85);
  const double disable_hz = 1.0 / (step * 0.95);
  const bool toward_change = wanted_ ? render_hz_ < disable_hz : render_hz_ >= enable_hz;
  agreeing_windows_ = toward_change ? agreeing_windows_ + 1 : 0;
  if (agreeing_windows_ >= kWindowsToChange) {
    wanted_ = !wanted_;
    agreeing_windows_ = 0;
  }
}

InterpolationScheduler::Frame InterpolationScheduler::BeginFrame(double now, InterpolationPolicy policy,
                                                                 double step) {
  if (step <= 0.0) {
    policy = InterpolationPolicy::kOff;
  }
  if (policy != policy_) {
    policy_ = policy;
    wanted_ = policy != InterpolationPolicy::kOff;
    window_start_ = now;
    window_frames_ = 0;
    agreeing_windows_ = 0;
  }
  // A stall is a discontinuity, not a frame rate: it is left out of both the frame spacing and the rate.
  const double interval = has_frame_ ? std::max(0.0, now - last_frame_) : 0.0;
  if (has_frame_ && interval <= kHistoryGap) {
    smoothed_ = frame_count_ < 8 ? (smoothed_ + interval) * 0.5 : smoothed_ * 0.9 + interval * 0.1;
    smoothed_ = std::max(smoothed_, 1e-9);
  }
  if (has_frame_ && policy_ != InterpolationPolicy::kOff) {
    MeasureRenderRate(now, interval, step);
  }
  last_frame_ = now;
  has_frame_ = true;
  ++frame_count_;

  if (has_tick_ && now - last_tick_ > kHistoryGap) {
    has_tick_ = false;
    ++epoch_;
  }

  const bool want = wanted_ && has_tick_;
  if (want != enabled_) {
    if (!want) {
      ++epoch_;
    }
    enabled_ = want;
    new_tick_ = true;
    frame_index_ = 0;
    frames_until_tick_ = 1;
  }

  Frame frame;
  frame.interpolating = enabled_;
  frame.wanted = wanted_;
  frame.tick = tick_;
  frame.epoch = epoch_;
  frame.render_hz = render_hz_;
  if (enabled_ && step > 0.0) {
    if (new_tick_) {
      const double remainder = std::clamp(tick_remainder_, -step, step);
      const double until_next = std::max(smoothed_, step * (1.0 - kEarlyStepShare) - remainder);
      frames_until_tick_ = uint32_t(std::max(1.0, std::ceil(until_next / smoothed_ - 1e-9)));
      frame_index_ = 0;
      new_tick_ = false;
    }
    frame.alpha = std::clamp(float(frame_index_) / float(frames_until_tick_), 0.0f, 1.0f);
    ++frame_index_;
  }
  return frame;
}

size_t TransformHistory::Slot(uint32_t object) {
  return ((object * 2654435761u) >> 21) & kMask;
}

TransformHistory::Result TransformHistory::Update(uint32_t object, uint32_t type, uint32_t tick, uint32_t epoch,
                                                  const TransformSample& live, float alpha, float max_distance) {
  Entry* entry = nullptr;
  Entry* reusable = nullptr;
  const size_t start = Slot(object);
  for (size_t i = 0; i < kMaxProbe; ++i) {
    Entry& candidate = entries_[(start + i) & kMask];
    if (candidate.epoch != epoch) {
      reusable = reusable ? reusable : &candidate;
      break;
    }
    if (candidate.object == object) {
      entry = &candidate;
      break;
    }
    // An entry missed for a whole tick would snap anyway, so its slot can be reused.
    if (!reusable && candidate.last_seen + 1 < tick) {
      reusable = &candidate;
    }
  }

  if (!entry) {
    if (reusable) {
      *reusable = Entry{object, type, tick, tick, epoch, true, live, live};
    }
    return {false, true, live};
  }

  const bool continuous = entry->type == type && tick <= entry->last_seen + 1 &&
                          !IsTransformDiscontinuity(entry->current, live, max_distance);
  if (tick != entry->tick) {
    entry->previous = continuous ? entry->current : live;
    entry->current = live;
    entry->tick = tick;
    entry->type = type;
    entry->snapped = !continuous;
  }
  entry->last_seen = tick;

  if (entry->snapped) {
    return {false, true, live};
  }
  if (SameTransform(entry->previous, entry->current)) {
    return {false, false, entry->current};
  }

  const float a = std::clamp(alpha, 0.0f, 1.0f);
  if (a >= 1.0f - kAlphaEpsilon) {
    return {false, false, entry->current};
  }
  if (a <= kAlphaEpsilon) {
    return {true, false, entry->previous};
  }
  return {true, false, InterpolateTransform(entry->previous, entry->current, a)};
}

void VisualInterpolationSimulationTick(double now, double remainder) {
  std::lock_guard lock(g_scheduler_mutex);
  g_scheduler.SimulationTick(now, remainder);
}

InterpolationScheduler::Frame VisualInterpolationBeginFrame(double now, InterpolationPolicy policy, double step,
                                                            double initial_interval) {
  std::lock_guard lock(g_scheduler_mutex);
  if (!g_initialized) {
    g_scheduler.Prepare(initial_interval);
    g_initialized = true;
  }
  g_frame = g_scheduler.BeginFrame(now, policy, step);
  return g_frame;
}

ObjectTransformOverride VisualInterpolationPrepareObject(uint8_t* base, uint32_t object) {
  ObjectTransformOverride result;
  if (!g_frame.interpolating || g_frame.tick == 0 || object == 0) {
    return result;
  }

  const TransformSample live = ReadTransform(base, object);
  const uint32_t type = LoadBE32(base, object + kObjectType);
  const TransformHistory::Result updated =
      g_history.Update(object, type, g_frame.tick, g_frame.epoch, live, g_frame.alpha, kSnapDistance);
  if (!updated.override_value) {
    return result;
  }

  result.object = object;
  result.active = true;
  result.saved = live;
  WriteTransform(base, object, updated.value);
  return result;
}

void VisualInterpolationRestoreObject(uint8_t* base, const ObjectTransformOverride& override) {
  if (!override.active) {
    return;
  }
  WriteTransform(base, override.object, override.saved);
}

}  // namespace gw2
