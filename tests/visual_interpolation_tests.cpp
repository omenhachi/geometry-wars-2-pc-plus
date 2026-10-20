// Visual interpolation core: shortest-path angles, transform blending, discontinuity snapping, the
// interpolation scheduler at 120/144/240/Unlimited cadences, and previous/current object history.

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
};

Schedule RunSchedule(double fps, double seconds, bool gates, double initial) {
  constexpr double kStep = 1.0 / 60.0;
  InterpolationScheduler scheduler;
  scheduler.Reset(initial);
  SimulationPacer pacer;
  Schedule run;
  const int frames = int(seconds * fps);
  double now = 0.0;
  double delta = kStep;
  for (int frame = 0; frame < frames; ++frame) {
    if (frame == 0) {
      scheduler.SimulationTick(now, 0.0);
      ++run.ticks;
    } else if (pacer.Tick(1.0 / fps, kStep, delta)) {
      scheduler.SimulationTick(now, pacer.remainder());
      ++run.ticks;
    }
    const InterpolationScheduler::Frame result = scheduler.BeginFrame(now, gates, kStep);
    if (result.interpolating) {
      ++run.interpolated_frames;
    }
    run.smallest = std::min(run.smallest, result.alpha);
    run.largest = std::max(run.largest, result.alpha);
    run.alphas.push_back(result.alpha);
    now += 1.0 / fps;
  }
  return run;
}

void TestSchedulerCadence() {
  Section("scheduler at presentation cadences");
  struct Case {
    double fps;
    double initial;
  };
  const Case cases[] = {{90.0, 1.0 / 90.0},   {120.0, 1.0 / 120.0}, {144.0, 1.0 / 144.0},
                        {240.0, 1.0 / 240.0}, {165.0, 1.0 / 165.0}, {1000.0, 1.0 / 60.0}};
  for (const Case& c : cases) {
    const Schedule run = RunSchedule(c.fps, 10.0, true, c.initial);
    CHECK(std::abs(run.ticks - 600) <= 1);
    CHECK(run.interpolated_frames > 0);
    CHECK(run.smallest >= 0.0f);
    CHECK(run.largest <= 1.0f);
  }

  const Schedule slow = RunSchedule(60.0, 10.0, true, 1.0 / 60.0);
  CHECK(std::abs(slow.ticks - 600) <= 1);
  CHECK(slow.interpolated_frames == 0);
  CHECK(slow.smallest == 0.0f);
  CHECK(slow.largest == 0.0f);
}

void TestSchedulerFrames() {
  Section("scheduler distributes frames between ticks");
  struct Case {
    double fps;
    double initial;
    std::vector<float> expected;
  };
  const Case cases[] = {
      {120.0, 1.0 / 120.0, {0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f}},
      {144.0, 1.0 / 144.0, {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 0.0f, 0.5f, 0.0f, 1.0f / 3.0f, 2.0f / 3.0f}},
      {240.0, 1.0 / 240.0, {0.0f, 0.25f, 0.5f, 0.75f, 0.0f, 0.25f, 0.5f, 0.75f}},
  };
  for (const Case& c : cases) {
    const Schedule run = RunSchedule(c.fps, 0.1, true, c.initial);
    for (size_t i = 0; i < c.expected.size() && i < run.alphas.size(); ++i) {
      CHECK(Near(run.alphas[i], c.expected[i], 1e-3));
    }
  }
}

void TestSchedulerAlpha() {
  Section("scheduler alpha between ticks");
  constexpr double kStep = 1.0 / 60.0;
  InterpolationScheduler scheduler;
  scheduler.Reset(1.0 / 120.0);
  scheduler.SimulationTick(0.0);
  InterpolationScheduler::Frame frame = scheduler.BeginFrame(0.0, true, kStep);
  CHECK(frame.interpolating);
  CHECK(Near(frame.alpha, 0.0f));
  frame = scheduler.BeginFrame(1.0 / 120.0, true, kStep);
  CHECK(Near(frame.alpha, 0.5f));
  frame = scheduler.BeginFrame(1.0 / 60.0, true, kStep);
  CHECK(Near(frame.alpha, 1.0f));
  scheduler.SimulationTick(1.0 / 60.0);
  frame = scheduler.BeginFrame(1.0 / 60.0, true, kStep);
  CHECK(Near(frame.alpha, 0.0f));

  scheduler.Reset(1.0 / 120.0);
  frame = scheduler.BeginFrame(0.0, true, kStep);
  CHECK(!frame.interpolating);

  scheduler.SimulationTick(0.0);
  frame = scheduler.BeginFrame(0.0, false, kStep);
  CHECK(!frame.interpolating);

  scheduler.Reset(1.0 / 120.0);
  scheduler.SimulationTick(0.0);
  frame = scheduler.BeginFrame(0.0, true, kStep);
  CHECK(frame.interpolating);
  const uint32_t epoch = frame.epoch;
  scheduler.SimulationTick(1.0);
  frame = scheduler.BeginFrame(1.0, true, kStep);
  CHECK(frame.alpha == 0.0f);
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
}

void TestNoInterpolationAtSixty() {
  Section("no interpolation at 60 or below");
  const Schedule sixty = RunSchedule(60.0, 2.0, false, 1.0 / 60.0);
  CHECK(sixty.interpolated_frames == 0);
  const Schedule thirty = RunSchedule(30.0, 2.0, false, 1.0 / 30.0);
  CHECK(thirty.interpolated_frames == 0);
  const Schedule unlimited_sixty = RunSchedule(60.0, 2.0, true, 1.0 / 60.0);
  CHECK(unlimited_sixty.interpolated_frames == 0);
}

}  // namespace

int main() {
  TestAngles();
  TestTransformBlend();
  TestDiscontinuity();
  TestSchedulerCadence();
  TestSchedulerFrames();
  TestSchedulerAlpha();
  TestHistory();
  TestNoInterpolationAtSixty();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
