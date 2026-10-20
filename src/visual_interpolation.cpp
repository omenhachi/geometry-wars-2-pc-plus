#include "visual_interpolation.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>

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
std::atomic<uint64_t> g_frames{0};
std::atomic<uint64_t> g_objects{0};
std::atomic<uint64_t> g_snaps{0};
std::atomic<uint32_t> g_alpha_bits{0};

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
  tick_remainder_ = std::max(0.0, remainder);
  has_tick_ = true;
  new_tick_ = true;
  ++tick_;
}

InterpolationScheduler::Frame InterpolationScheduler::BeginFrame(double now, bool gates, double step) {
  if (has_frame_) {
    const double interval = std::max(0.0, now - last_frame_);
    smoothed_ = frame_count_ < 8 ? (smoothed_ + interval) * 0.5 : smoothed_ * 0.9 + interval * 0.1;
    smoothed_ = std::max(smoothed_, 1e-9);
  }
  last_frame_ = now;
  has_frame_ = true;
  ++frame_count_;

  if (has_tick_ && now - last_tick_ > kHistoryGap) {
    has_tick_ = false;
    ++epoch_;
  }

  const bool can_interpolate = gates && step > 0.0 && has_tick_;
  const double enable_below = step * 0.85;
  const double disable_above = step * 0.95;
  const bool fast_enough = enabled_ ? smoothed_ < disable_above : smoothed_ < enable_below;
  const bool want = can_interpolate && fast_enough;
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
  frame.tick = tick_;
  frame.epoch = epoch_;
  frame.measured_hz = smoothed_ > 0.0 ? 1.0 / smoothed_ : 0.0;
  frame.frame_count = frame_count_;
  if (enabled_ && step > 0.0) {
    if (new_tick_) {
      const double remainder = std::clamp(tick_remainder_, 0.0, step);
      const double until_next = std::max(smoothed_, step - remainder);
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
  bool fresh = false;
  const size_t start = Slot(object);
  for (size_t i = 0; i < kCapacity; ++i) {
    Entry& candidate = entries_[(start + i) & kMask];
    if (candidate.epoch != epoch) {
      candidate = Entry{};
      candidate.object = object;
      candidate.type = type;
      candidate.tick = tick;
      candidate.last_seen = tick;
      candidate.epoch = epoch;
      candidate.previous = live;
      candidate.current = live;
      candidate.snapped = true;
      entry = &candidate;
      fresh = true;
      break;
    }
    if (candidate.object == object) {
      entry = &candidate;
      break;
    }
  }

  if (!entry) {
    return {false, true, live};
  }
  if (fresh) {
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

void VisualInterpolationReset(double initial_interval) {
  std::lock_guard lock(g_scheduler_mutex);
  g_scheduler.Reset(initial_interval);
  g_frame = {};
  g_initialized = true;
}

void VisualInterpolationSimulationTick(double now, double remainder) {
  std::lock_guard lock(g_scheduler_mutex);
  g_scheduler.SimulationTick(now, remainder);
}

InterpolationScheduler::Frame VisualInterpolationBeginFrame(double now, bool gates, double step,
                                                            double initial_interval) {
  std::lock_guard lock(g_scheduler_mutex);
  if (!g_initialized) {
    g_scheduler.Prepare(initial_interval);
    g_initialized = true;
  }
  g_frame = g_scheduler.BeginFrame(now, gates, step);
  g_alpha_bits.store(std::bit_cast<uint32_t>(g_frame.alpha), std::memory_order_relaxed);
  if (g_frame.interpolating) {
    g_frames.fetch_add(1, std::memory_order_relaxed);
  }
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
  if (updated.snapped) {
    g_snaps.fetch_add(1, std::memory_order_relaxed);
  }
  if (!updated.override_value) {
    return result;
  }

  result.object = object;
  result.active = true;
  result.saved = live;
  WriteTransform(base, object, updated.value);
  g_objects.fetch_add(1, std::memory_order_relaxed);
  return result;
}

void VisualInterpolationRestoreObject(uint8_t* base, const ObjectTransformOverride& override) {
  if (!override.active) {
    return;
  }
  WriteTransform(base, override.object, override.saved);
}

InterpolationStats TakeInterpolationStats() {
  InterpolationStats stats;
  stats.frames = g_frames.exchange(0, std::memory_order_relaxed);
  stats.objects = g_objects.exchange(0, std::memory_order_relaxed);
  stats.snaps = g_snaps.exchange(0, std::memory_order_relaxed);
  stats.alpha = std::bit_cast<float>(g_alpha_bits.load(std::memory_order_relaxed));
  return stats;
}

}  // namespace gw2
