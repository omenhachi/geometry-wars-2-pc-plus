// Visual interpolation core: shortest-path angles, transform blending, discontinuity snapping, the
// interpolation policy and scheduler at 30 to 240 and Unlimited cadences, and previous/current object history.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "frame_limiter.h"
#include "visual_interpolation.h"

using namespace gw2;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

void Section(const char* name) {
  std::printf("%s\n", name);
}

bool Near(double a, double b, double epsilon = 1e-4) {
  return std::fabs(a - b) <= epsilon;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

TransformSample Sample(float x, float y, float angle = 0.0f, float sx = 1.0f, float sy = 1.0f) {
  return {x, y, angle, sx, sy};
}

void TestAngles() {
  Section("shortest-path angles");
  CHECK(Near(ShortestAngleDelta(0.0f, kPi * 0.5f), kPi * 0.5f));
  CHECK(Near(ShortestAngleDelta(kPi * 0.5f, 0.0f), -kPi * 0.5f));
  CHECK(Near(ShortestAngleDelta(3.0f, -3.0f), kTwoPi - 6.0f));
  CHECK(Near(ShortestAngleDelta(-3.0f, 3.0f), -(kTwoPi - 6.0f)));
  CHECK(Near(ShortestAngleDelta(0.0f, kTwoPi), 0.0f));
  CHECK(Near(ShortestAngleDelta(0.0f, 4.0f * kTwoPi + 1.0f), 1.0f));

  const TransformSample previous = Sample(0.0f, 0.0f, 3.0f);
  const TransformSample current = Sample(0.0f, 0.0f, -3.0f);
  const TransformSample mid = InterpolateTransform(previous, current, 0.5f);
  CHECK(Near(ShortestAngleDelta(previous.angle, mid.angle), (kTwoPi - 6.0f) * 0.5f, 1e-3));
  CHECK(Near(mid.angle, kPi, 1e-3));
}

void TestTransformBlend() {
  Section("transform blending");
  const TransformSample previous = Sample(-2.0f, 4.0f, 0.0f, 1.0f, 3.0f);
  const TransformSample current = Sample(6.0f, -4.0f, kPi * 0.5f, 3.0f, 1.0f);

  const TransformSample at_zero = InterpolateTransform(previous, current, 0.0f);
  CHECK(Near(at_zero.x, previous.x));
  CHECK(Near(at_zero.y, previous.y));
  CHECK(Near(at_zero.angle, previous.angle));
  CHECK(Near(at_zero.scale_x, previous.scale_x));
  CHECK(Near(at_zero.scale_y, previous.scale_y));

  const TransformSample at_half = InterpolateTransform(previous, current, 0.5f);
  CHECK(Near(at_half.x, 2.0f));
  CHECK(Near(at_half.y, 0.0f));
  CHECK(Near(at_half.angle, kPi * 0.25f));
  CHECK(Near(at_half.scale_x, 2.0f));
  CHECK(Near(at_half.scale_y, 2.0f));

  const TransformSample at_one = InterpolateTransform(previous, current, 1.0f);
  CHECK(Near(at_one.x, current.x));
  CHECK(Near(at_one.y, current.y));
  CHECK(Near(at_one.angle, current.angle));
  CHECK(Near(at_one.scale_x, current.scale_x));
  CHECK(Near(at_one.scale_y, current.scale_y));

  const TransformSample below = InterpolateTransform(previous, current, -4.0f);
  const TransformSample above = InterpolateTransform(previous, current, 9.0f);
  CHECK(Near(below.x, previous.x));
  CHECK(Near(above.x, current.x));
}

void TestDiscontinuity() {
  Section("discontinuity detection");
  CHECK(!IsTransformDiscontinuity(Sample(0.0f, 0.0f), Sample(1.0f, 0.0f), 8.0f));
  CHECK(IsTransformDiscontinuity(Sample(0.0f, 0.0f), Sample(9.0f, 0.0f), 8.0f));
  CHECK(!IsTransformDiscontinuity(Sample(0.0f, 0.0f), Sample(0.0f, 0.0f, 100.0f), 8.0f));
}

struct Schedule {
  int ticks = 0;
  int interpolated_frames = 0;
  float smallest = 1.0f;
  float largest = 0.0f;
  std::vector<float> alphas;
  std::vector<bool> tick_frames;
  std::vector<bool> interpolating_frames;
};

Schedule RunSchedule(double fps, double seconds, InterpolationPolicy policy, double initial) {
  constexpr double kStep = 1.0 / 60.0;
  InterpolationScheduler scheduler;
  scheduler.Reset(initial);
  SimulationPacer pacer;
  Schedule run;
  const int frames = int(seconds * fps);
  double now = 0.0;
  double delta = kStep;
  for (int frame = 0; frame < frames; ++frame) {
    bool tick = frame == 0;
    if (tick) {
      scheduler.SimulationTick(now, 0.0);
    } else if (pacer.Tick(1.0 / fps, kStep, delta)) {
      scheduler.SimulationTick(now, pacer.remainder());
      tick = true;
    }
    run.ticks += tick;
    const InterpolationScheduler::Frame result = scheduler.BeginFrame(now, policy, kStep);
    if (result.interpolating) {
      ++run.interpolated_frames;
    }
    run.smallest = std::min(run.smallest, result.alpha);
    run.largest = std::max(run.largest, result.alpha);
    run.alphas.push_back(result.alpha);
    run.tick_frames.push_back(tick);
    run.interpolating_frames.push_back(result.interpolating);
    now += 1.0 / fps;
  }
  return run;
}

struct Lcg {
  uint32_t state;
  double Unit() {
    state = state * 1664525u + 1013904223u;
    return double(state >> 8) / double(1u << 24);
  }
};

// Frames at `hz` with vblank-style jitter: each frame lands within `jitter` of its slot, so the average rate stays
// exact while individual intervals vary.
struct Jittered {
  double hz;
  double jitter;
  Lcg rng;
  double previous_offset = 0.0;
  double operator()() {
    const double offset = (rng.Unit() * 2.0 - 1.0) * jitter;
    const double interval = 1.0 / hz + offset - previous_offset;
    previous_offset = offset;
    return interval;
  }
};

// Alternates bursts faster and slower than `hz`, each shorter than a measurement window.
struct Bursty {
  double fast_hz;
  double slow_hz;
  double burst;
  Jittered jitter{0.0, 0.0005, {7}};
  double elapsed = 0.0;
  double operator()() {
    const bool fast = std::fmod(elapsed, 2.0 * burst) < burst;
    jitter.hz = fast ? fast_hz : slow_hz;
    const double interval = jitter();
    elapsed += interval;
    return interval;
  }
};

// Adds a `hitch` long frame every `every` seconds.
template <class Next>
struct Hitching {
  Next next;
  double hitch;
  double every;
  double since = 0.0;
  double operator()() {
    double interval = next();
    since += interval;
    if (since >= every) {
      since = 0.0;
      interval += hitch;
    }
    return interval;
  }
};

// Feeds a cadence through the simulation pacer and the scheduler, counting changes of the interpolation decision.
class DecisionRun {
 public:
  explicit DecisionRun(InterpolationPolicy policy) : policy_(policy) { scheduler_.Reset(1.0 / 240.0); }

  template <class Next>
  void Run(double seconds, Next next) {
    constexpr double kStep = 1.0 / 60.0;
    const double end = now_ + seconds;
    while (now_ < end) {
      if (frames_ == 0) {
        scheduler_.SimulationTick(now_, 0.0);
      } else if (pacer_.Tick(now_ - previous_, kStep, delta_)) {
        scheduler_.SimulationTick(now_, pacer_.remainder());
      }
      last_ = scheduler_.BeginFrame(now_, policy_, kStep);
      if (frames_ == 0) {
        wanted_ = last_.wanted;
        first_interpolating_ = last_.interpolating;
      } else if (last_.wanted != wanted_) {
        wanted_ = last_.wanted;
        ++changes_;
        last_change_ = now_;
      }
      if (last_.interpolating && last_.alpha > 0.0f && last_.alpha < 1.0f) {
        last_blend_ = now_;
      }
      ++frames_;
      previous_ = now_;
      now_ += next();
    }
  }

  int changes() const { return changes_; }
  bool wanted() const { return wanted_; }
  bool first_interpolating() const { return first_interpolating_; }
  bool interpolating() const { return last_.interpolating; }
  double last_change() const { return last_change_; }
  double last_blend() const { return last_blend_; }
  double now() const { return now_; }

 private:
  InterpolationPolicy policy_;
  InterpolationScheduler scheduler_;
  InterpolationScheduler::Frame last_;
  SimulationPacer pacer_;
  double delta_ = 1.0 / 60.0;
  double now_ = 0.0;
  double previous_ = 0.0;
  double last_change_ = -1.0;
  double last_blend_ = -1.0;
  bool wanted_ = false;
  bool first_interpolating_ = false;
  int frames_ = 0;
  int changes_ = 0;
};

void TestPolicy() {
  Section("interpolation policy per frame rate setting");
  CHECK(InterpolationPolicyFor(30) == InterpolationPolicy::kOff);
  CHECK(InterpolationPolicyFor(60) == InterpolationPolicy::kOff);
  for (const int fps : {90, 120, 144, 165, 240}) {
    CHECK(InterpolationPolicyFor(fps) == InterpolationPolicy::kOn);
  }
  CHECK(InterpolationPolicyFor(kFrameRateUnlimited) == InterpolationPolicy::kMeasured);
}

void TestStartup() {
  Section("30 and 60 start and stay inactive");
  for (const double fps : {30.0, 60.0}) {
    DecisionRun run(InterpolationPolicyFor(int(fps)));
    run.Run(10.0, Jittered{fps, 0.001, {1}});
    CHECK(!run.first_interpolating());
    CHECK(!run.wanted());
    CHECK(run.changes() == 0);
  }

  Section("fixed rates above 60 interpolate from the first frame and never change");
  for (const int fps : {90, 120, 144, 165, 240}) {
    DecisionRun run(InterpolationPolicyFor(fps));
    run.Run(20.0, Hitching<Jittered>{{double(fps), 0.001, {2}}, 0.12, 3.0});
    CHECK(run.first_interpolating());
    CHECK(run.changes() == 0);
    CHECK(run.interpolating());
  }

  Section("240 behind a 60 Hz VSync output: rendering below the target at startup does not delay interpolation");
  DecisionRun vsync(InterpolationPolicyFor(240));
  vsync.Run(1.0, Jittered{95.0, 0.003, {3}});
  vsync.Run(10.0, Hitching<Jittered>{{147.0, 0.003, {4}}, 0.09, 2.5});
  CHECK(vsync.first_interpolating());
  CHECK(vsync.changes() == 0);
  CHECK(vsync.last_blend() > vsync.now() - 0.1);

  Section("a fixed target the guest cannot reach keeps one state, with no blend between updates");
  const Schedule unreachable = RunSchedule(50.0, 10.0, InterpolationPolicyFor(144), 1.0 / 144.0);
  CHECK(unreachable.interpolated_frames > 0);
  CHECK(unreachable.largest == 0.0f);
}

void TestUnlimited() {
  const InterpolationPolicy unlimited = InterpolationPolicyFor(kFrameRateUnlimited);

  Section("Unlimited above 60 starts on and keeps interpolation through hitches and stalls");
  for (const double hz : {90.0, 144.0, 240.0, 500.0}) {
    DecisionRun run(unlimited);
    run.Run(20.0, Hitching<Jittered>{{hz, 0.001, {5}}, 0.12, 3.0});
    CHECK(run.first_interpolating());
    CHECK(run.changes() == 0);
  }

  Section("Unlimited at a jittered 60 turns off once and stays off");
  DecisionRun sixty(unlimited);
  sixty.Run(30.0, Jittered{60.0, 0.001, {6}});
  CHECK(sixty.changes() == 1);
  CHECK(!sixty.wanted());
  CHECK(sixty.last_change() <= 2.5);

  Section("Unlimited ignores bursts around the threshold and hitches at 60");
  DecisionRun bursty(unlimited);
  bursty.Run(60.0, Hitching<Bursty>{{75.0, 50.0, 0.3}, 0.12, 3.0});
  CHECK(bursty.changes() <= 1);

  Section("Unlimited keeps interpolation through short dips");
  DecisionRun dips(unlimited);
  dips.Run(5.0, Jittered{144.0, 0.001, {8}});
  dips.Run(0.6, Jittered{45.0, 0.001, {9}});
  dips.Run(5.0, Jittered{144.0, 0.001, {10}});
  CHECK(dips.changes() == 0);
  CHECK(dips.wanted());

  Section("Unlimited falls back after a sustained drop to 60 and recovers after a sustained return");
  DecisionRun fallback(unlimited);
  fallback.Run(5.0, Jittered{144.0, 0.001, {11}});
  double changed = fallback.now();
  fallback.Run(8.0, Jittered{55.0, 0.001, {12}});
  CHECK(fallback.changes() == 1);
  CHECK(!fallback.wanted());
  CHECK(fallback.last_change() - changed <= 3.0);
  changed = fallback.now();
  fallback.Run(8.0, Jittered{144.0, 0.001, {13}});
  CHECK(fallback.changes() == 2);
  CHECK(fallback.wanted());
  CHECK(fallback.last_change() - changed <= 3.0);

  Section("Unlimited settles once after startup transients");
  Lcg startup{14};
  const auto transient = [&startup] { return 0.005 + startup.Unit() * 0.075; };
  DecisionRun to_high(unlimited);
  to_high.Run(1.5, transient);
  to_high.Run(10.0, Jittered{144.0, 0.001, {15}});
  CHECK(to_high.changes() == 0);
  CHECK(to_high.wanted());
  DecisionRun to_sixty(unlimited);
  to_sixty.Run(1.5, transient);
  to_sixty.Run(10.0, Jittered{60.0, 0.001, {16}});
  CHECK(to_sixty.changes() == 1);
  CHECK(!to_sixty.wanted());
}

void TestStalls() {
  Section("a stall or pause keeps the decision and resumes blending at once");
  for (const InterpolationPolicy policy : {InterpolationPolicy::kOn, InterpolationPolicy::kMeasured}) {
    DecisionRun run(policy);
    run.Run(3.0, Jittered{144.0, 0.001, {17}});
    run.Run(2.0, [] { return 2.0; });
    const double resumed = run.now();
    run.Run(0.1, Jittered{144.0, 0.001, {18}});
    CHECK(run.last_blend() > resumed);
    run.Run(5.0, Jittered{144.0, 0.001, {19}});
    CHECK(run.changes() == 0);
    CHECK(run.wanted());
    CHECK(run.interpolating());
  }
}

void TestSchedulerCadence() {
  Section("scheduler at presentation cadences");
  struct Case {
    double fps;
    InterpolationPolicy policy;
    double initial;
  };
  const Case cases[] = {{90.0, InterpolationPolicy::kOn, 1.0 / 90.0},   {120.0, InterpolationPolicy::kOn, 1.0 / 120.0},
                        {144.0, InterpolationPolicy::kOn, 1.0 / 144.0}, {165.0, InterpolationPolicy::kOn, 1.0 / 165.0},
                        {240.0, InterpolationPolicy::kOn, 1.0 / 240.0}, {1000.0, InterpolationPolicy::kMeasured, 1.0 / 60.0}};
  for (const Case& c : cases) {
    const Schedule run = RunSchedule(c.fps, 10.0, c.policy, c.initial);
    CHECK(std::abs(run.ticks - 600) <= 1);
    CHECK(run.interpolating_frames.front());
    CHECK(run.interpolated_frames == int(run.alphas.size()));
    CHECK(run.smallest >= 0.0f);
    CHECK(run.largest <= 1.0f);
  }

  const Schedule slow = RunSchedule(60.0, 10.0, InterpolationPolicy::kMeasured, 1.0 / 60.0);
  CHECK(std::abs(slow.ticks - 600) <= 1);
  CHECK(slow.interpolated_frames <= int(2.5 * 60.0));
  CHECK(slow.largest == 0.0f);
}

void TestSchedulerFrames() {
  Section("scheduler distributes frames evenly between ticks from the first frame");
  for (const double fps : {120.0, 144.0, 165.0, 240.0}) {
    const Schedule run = RunSchedule(fps, 6.0, InterpolationPolicy::kOn, 1.0 / fps);
    size_t start = run.alphas.size();
    int segments = 0;
    int uneven = 0;
    for (size_t i = 0; i < run.alphas.size(); ++i) {
      if (!run.interpolating_frames[i] || !run.tick_frames[i]) {
        continue;
      }
      if (start < i) {
        const size_t length = i - start;
        for (size_t k = 0; k < length; ++k) {
          uneven += !Near(run.alphas[start + k], float(k) / float(length), 1e-3);
        }
        ++segments;
      }
      start = i;
    }
    CHECK(run.interpolating_frames.front() && run.tick_frames.front());
    CHECK(segments > 300);
    CHECK(uneven == 0);
  }
  const Schedule mixed = RunSchedule(144.0, 6.0, InterpolationPolicy::kOn, 1.0 / 144.0);
  int two = 0;
  int three = 0;
  size_t previous_tick = mixed.alphas.size();
  for (size_t i = 0; i < mixed.alphas.size(); ++i) {
    if (mixed.interpolating_frames[i] && mixed.tick_frames[i]) {
      if (previous_tick < i) {
        two += i - previous_tick == 2;
        three += i - previous_tick == 3;
      }
      previous_tick = i;
    }
  }
  CHECK(two > 0 && three > 0);
}

void TestSchedulerAlpha() {
  Section("scheduler alpha between ticks");
  constexpr double kStep = 1.0 / 60.0;
  InterpolationScheduler scheduler;
  scheduler.Reset(1.0 / 120.0);
  scheduler.SimulationTick(0.0);
  InterpolationScheduler::Frame frame = scheduler.BeginFrame(0.0, InterpolationPolicy::kOn, kStep);
  CHECK(frame.interpolating);
  CHECK(frame.wanted);
  CHECK(Near(frame.alpha, 0.0f));
  frame = scheduler.BeginFrame(1.0 / 120.0, InterpolationPolicy::kOn, kStep);
  CHECK(Near(frame.alpha, 0.5f));
  frame = scheduler.BeginFrame(1.0 / 60.0, InterpolationPolicy::kOn, kStep);
  CHECK(Near(frame.alpha, 1.0f));
  scheduler.SimulationTick(1.0 / 60.0);
  frame = scheduler.BeginFrame(1.0 / 60.0, InterpolationPolicy::kOn, kStep);
  CHECK(Near(frame.alpha, 0.0f));

  scheduler.Reset(1.0 / 120.0);
  frame = scheduler.BeginFrame(0.0, InterpolationPolicy::kOn, kStep);
  CHECK(!frame.interpolating);
  CHECK(frame.wanted);

  scheduler.SimulationTick(0.0);
  frame = scheduler.BeginFrame(0.0, InterpolationPolicy::kOff, kStep);
  CHECK(!frame.interpolating);
  CHECK(!frame.wanted);

  scheduler.Reset(1.0 / 120.0);
  scheduler.SimulationTick(0.0);
  frame = scheduler.BeginFrame(0.0, InterpolationPolicy::kOn, kStep);
  CHECK(frame.interpolating);
  const uint32_t epoch = frame.epoch;
  scheduler.SimulationTick(1.0);
  frame = scheduler.BeginFrame(1.0, InterpolationPolicy::kOn, kStep);
  CHECK(frame.alpha == 0.0f);
  CHECK(frame.epoch != epoch);
  CHECK(frame.wanted);

  scheduler.Reset(1.0 / 120.0);
  scheduler.SimulationTick(0.0);
  frame = scheduler.BeginFrame(0.0, InterpolationPolicy::kMeasured, kStep);
  CHECK(frame.interpolating);
  frame = scheduler.BeginFrame(1.0 / 120.0, InterpolationPolicy::kOff, kStep);
  CHECK(!frame.interpolating);
  CHECK(!frame.wanted);
  CHECK(frame.epoch != epoch);
}

void TestHistory() {
  Section("transform history");
  TransformHistory history;
  const TransformSample first = Sample(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
  const TransformSample second = Sample(2.0f, 4.0f, kPi * 0.5f, 2.0f, 2.0f);

  TransformHistory::Result result = history.Update(0x1000, 7, 1, 1, first, 0.5f, 8.0f);
  CHECK(result.snapped);
  CHECK(!result.override_value);

  result = history.Update(0x1000, 7, 2, 1, second, 0.0f, 8.0f);
  CHECK(!result.snapped);
  CHECK(result.override_value);
  CHECK(Near(result.value.x, first.x));
  CHECK(Near(result.value.y, first.y));

  result = history.Update(0x1000, 7, 2, 1, second, 0.5f, 8.0f);
  CHECK(result.override_value);
  CHECK(Near(result.value.x, 1.0f));
  CHECK(Near(result.value.y, 2.0f));
  CHECK(Near(result.value.scale_x, 1.5f));

  result = history.Update(0x1000, 7, 2, 1, second, 1.0f, 8.0f);
  CHECK(!result.override_value);
  CHECK(Near(result.value.x, second.x));

  result = history.Update(0x1000, 7, 2, 1, second, -3.0f, 8.0f);
  CHECK(result.override_value);
  CHECK(Near(result.value.x, first.x));

  result = history.Update(0x1000, 7, 2, 1, second, 9.0f, 8.0f);
  CHECK(!result.override_value);
  CHECK(Near(result.value.x, second.x));

  Section("transform history discontinuities");
  TransformHistory moving;
  moving.Update(0x2000, 7, 1, 1, first, 0.5f, 8.0f);
  const TransformSample teleported = Sample(100.0f, 0.0f);
  result = moving.Update(0x2000, 7, 2, 1, teleported, 0.5f, 8.0f);
  CHECK(result.snapped);
  CHECK(!result.override_value);
  result = moving.Update(0x2000, 7, 3, 1, Sample(102.0f, 0.0f), 0.5f, 8.0f);
  CHECK(!result.snapped);
  CHECK(result.override_value);

  TransformHistory retyped;
  retyped.Update(0x3000, 7, 1, 1, first, 0.5f, 8.0f);
  result = retyped.Update(0x3000, 8, 2, 1, second, 0.5f, 8.0f);
  CHECK(result.snapped);
  CHECK(!result.override_value);

  TransformHistory reused;
  reused.Update(0x4000, 7, 1, 1, first, 0.5f, 8.0f);
  result = reused.Update(0x4000, 7, 4, 1, second, 0.5f, 8.0f);
  CHECK(result.snapped);
  CHECK(!result.override_value);

  TransformHistory reset;
  reset.Update(0x5000, 7, 1, 1, first, 0.5f, 8.0f);
  result = reset.Update(0x5000, 7, 2, 2, second, 0.5f, 8.0f);
  CHECK(result.snapped);
  CHECK(!result.override_value);

  Section("transform history static object");
  TransformHistory still;
  still.Update(0x6000, 7, 1, 1, first, 0.5f, 8.0f);
  result = still.Update(0x6000, 7, 2, 1, first, 0.5f, 8.0f);
  CHECK(!result.snapped);
  CHECK(!result.override_value);
  CHECK(Near(result.value.x, first.x));

  Section("transform history reuses the slots of objects that are gone");
  TransformHistory churn;
  uint32_t tick = 1;
  for (uint32_t i = 0; i < 4096; ++i, ++tick) {
    churn.Update(0x10000 + i * 0x40, 7, tick, 1, first, 0.5f, 8.0f);
  }
  churn.Update(0x900000, 7, tick, 1, first, 0.5f, 8.0f);
  result = churn.Update(0x900000, 7, tick + 1, 1, second, 0.5f, 8.0f);
  CHECK(!result.snapped);
  CHECK(result.override_value);
}

}  // namespace

int main() {
  TestAngles();
  TestTransformBlend();
  TestDiscontinuity();
  TestSchedulerCadence();
  TestSchedulerFrames();
  TestSchedulerAlpha();
  TestPolicy();
  TestStartup();
  TestUnlimited();
  TestStalls();
  TestHistory();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
