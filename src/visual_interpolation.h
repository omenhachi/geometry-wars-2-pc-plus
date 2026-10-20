#pragma once

#include <cstddef>
#include <cstdint>

namespace gw2 {

struct TransformSample {
  float x = 0.0f;
  float y = 0.0f;
  float angle = 0.0f;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
};

// Signed shortest distance from `from` to `to`, in [-pi, pi].
float ShortestAngleDelta(float from, float to);

TransformSample InterpolateTransform(const TransformSample& previous, const TransformSample& current, float alpha);

bool IsTransformDiscontinuity(const TransformSample& previous, const TransformSample& current, float max_distance);

// Decides when presentation may interpolate and how far between the last two completed simulation states it is.
// The tick frame shows the previous state, so interpolation adds one simulation tick of visual latency.
class InterpolationScheduler {
 public:
  static constexpr double kHistoryGap = 0.25;

  struct Frame {
    bool interpolating = false;
    float alpha = 0.0f;
    uint32_t tick = 0;
    uint32_t epoch = 1;
    double measured_hz = 0.0;
    uint64_t frame_count = 0;
  };

  void Reset(double initial_interval);
  void Prepare(double initial_interval);
  void SimulationTick(double now, double remainder = 0.0);
  Frame BeginFrame(double now, bool gates, double step);

 private:
  bool has_tick_ = false;
  double last_tick_ = 0.0;
  double tick_remainder_ = 0.0;
  bool new_tick_ = false;
  uint32_t frame_index_ = 0;
  uint32_t frames_until_tick_ = 1;
  bool has_frame_ = false;
  double last_frame_ = 0.0;
  double smoothed_ = 1.0 / 60.0;
  uint32_t frame_count_ = 0;
  bool enabled_ = false;
  uint32_t tick_ = 0;
  uint32_t epoch_ = 1;
};

// Previous/current transforms for objects that survive more than one simulation tick.
class TransformHistory {
 public:
  struct Result {
    bool override_value = false;
    bool snapped = false;
    TransformSample value;
  };

  Result Update(uint32_t object, uint32_t type, uint32_t tick, uint32_t epoch, const TransformSample& live,
                float alpha, float max_distance);

 private:
  static constexpr size_t kCapacity = 2048;
  static constexpr size_t kMask = kCapacity - 1;

  struct Entry {
    uint32_t object = 0;
    uint32_t type = 0;
    uint32_t tick = 0;
    uint32_t last_seen = 0;
    uint32_t epoch = 0;
    bool snapped = true;
    TransformSample previous;
    TransformSample current;
  };

  static size_t Slot(uint32_t object);

  Entry entries_[kCapacity];
};

struct InterpolationStats {
  uint64_t frames = 0;
  uint64_t objects = 0;
  uint64_t snaps = 0;
  float alpha = 0.0f;
};

// Render-only object transform override. `saved` is the authoritative value restored after the draw.
struct ObjectTransformOverride {
  uint32_t object = 0;
  bool active = false;
  TransformSample saved;
};

void VisualInterpolationReset(double initial_interval);
void VisualInterpolationSimulationTick(double now, double remainder);
InterpolationScheduler::Frame VisualInterpolationBeginFrame(double now, bool gates, double step,
                                                            double initial_interval);
ObjectTransformOverride VisualInterpolationPrepareObject(uint8_t* base, uint32_t object);
void VisualInterpolationRestoreObject(uint8_t* base, const ObjectTransformOverride& override);
InterpolationStats TakeInterpolationStats();

}  // namespace gw2
