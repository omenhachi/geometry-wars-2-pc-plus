// Frame limiter tests: the period of each rate, the schedule with a synthetic clock, resynchronisation, the
// high-frame-rate rules, the game steps at 60, and that the wait reaches its deadline.

#include <chrono>
#include <cmath>
#include <cstdio>

#include "frame_limiter.h"

using namespace gw2;
using std::chrono::duration;
using std::chrono::milliseconds;

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

double PeriodMs(int fps) {
  return duration<double, std::milli>(FramePeriod(fps)).count();
}

bool Near(double a, double b) {
  return std::fabs(a - b) < 0.001;
}

const LimiterClock::time_point kStart{};

LimiterClock::time_point At(double ms) {
  return kStart + std::chrono::duration_cast<LimiterClock::duration>(duration<double, std::milli>(ms));
}

void TestPeriods() {
  Section("frame periods");
  CHECK(Near(PeriodMs(30), 33.333));
  CHECK(Near(PeriodMs(60), 16.667));
  CHECK(Near(PeriodMs(90), 11.111));
  CHECK(Near(PeriodMs(120), 8.333));
  CHECK(Near(PeriodMs(144), 6.944));
  CHECK(Near(PeriodMs(165), 6.061));
  CHECK(Near(PeriodMs(240), 4.167));
  CHECK(FramePeriod(kFrameRateUnlimited) == LimiterClock::duration::zero());
  CHECK(FramePeriod(-5) == LimiterClock::duration::zero());
}

void TestChoices() {
  Section("frame rate choices");
  CHECK(std::size(kFrameRates) == 8);
  for (const int fps : {30, 60, 90, 120, 144, 165, 240, 0}) {
    CHECK(IsFrameRateChoice(fps));
  }
  for (const int fps : {-1, 1, 59, 61, 100, 1000}) {
    CHECK(!IsFrameRateChoice(fps));
  }
  CHECK(kFrameRateDefault == 60);
  CHECK(!IsHighFrameRate(30));
  CHECK(!IsHighFrameRate(60));
  CHECK(IsHighFrameRate(90));
  CHECK(IsHighFrameRate(240));
  CHECK(IsHighFrameRate(kFrameRateUnlimited));
}

void TestPacingPolicy() {
  Section("pacing policy");
  CHECK(PresentModeFor(60) == PresentMode::kOriginal);
  CHECK(ProjectLimiterRate(60) == kFrameRateUnlimited);
  CHECK(PresentModeFor(30) == PresentMode::kOriginal);
  CHECK(ProjectLimiterRate(30) == 30);
  for (const int fps : {90, 120, 144, 165, 240}) {
    CHECK(PresentModeFor(fps) == PresentMode::kImmediate);
    CHECK(ProjectLimiterRate(fps) == fps);
  }
  CHECK(PresentModeFor(kFrameRateUnlimited) == PresentMode::kImmediate);
  CHECK(ProjectLimiterRate(kFrameRateUnlimited) == kFrameRateUnlimited);
}

void TestGameDelta() {
  Section("game time step");
  CHECK(KeepsOriginalGameDelta(60));
  for (const int fps : {30, 90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(!KeepsOriginalGameDelta(fps));
  }
  CHECK(Near(GameFrameDelta(1.0 / 30) * 1000, 33.333));
  CHECK(Near(GameFrameDelta(1.0 / 120) * 1000, 8.333));
  CHECK(Near(GameFrameDelta(1.0 / 240) * 1000, 4.167));
  CHECK(GameFrameDelta(0.0) > 0.0f);
  CHECK(GameFrameDelta(-1.0) > 0.0f);
  CHECK(Near(GameFrameDelta(0.0001) * 1000, 1.0));
  CHECK(Near(GameFrameDelta(2.0) * 1000, 50.0));
  double game = 0;
  for (int i = 0; i < 120; ++i) {
    game += GameFrameDelta(1.0 / 120);
  }
  CHECK(std::fabs(game - 1.0) < 0.0001);
}

// Whether each of `frames` frames of `interval` steps, `jitter` late and early in turn, gets `expected` steps.
bool EveryFrameGets(GameStepCounter& counter, int expected, int frames, double interval, double jitter) {
  bool all = true;
  for (int i = 0; i < frames; ++i) {
    all &= counter.Take(interval + (i % 2 ? -jitter : jitter)) == expected;
  }
  return all;
}

void TestGameSteps() {
  Section("game steps at 60");
  GameStepCounter counter;
  CHECK(EveryFrameGets(counter, 1, 600, 1.0, 0.4));
  CHECK(EveryFrameGets(counter, 2, 600, 2.0, 0.4));
  CHECK(EveryFrameGets(counter, 3, 60, 3.0, 0.4));
  CHECK(EveryFrameGets(counter, 1, 60, 1.0, 0.4));
  CHECK(counter.Take(4.0) == kMaxGameSteps);
  CHECK(counter.Take(30.0) == kMaxGameSteps);
  CHECK(EveryFrameGets(counter, 1, 60, 1.0, 0.4));

  Section("game steps follow real time");
  GameStepCounter uneven;
  int steps = 0;
  for (int i = 0; i < 600; ++i) {
    steps += uneven.Take(1.2);
  }
  CHECK(steps == 720);
  GameStepCounter fast;
  CHECK(fast.Take(0.1) == 1);
  CHECK(fast.Take(0.1) == 1);
  CHECK(fast.Take(1.0) == 1);
  CHECK(fast.Take(2.0) == 2);

  Section("game steps reset");
  GameStepCounter reset;
  CHECK(reset.Take(1.4) == 1);
  reset.Reset();
  CHECK(reset.Take(1.2) == 1);
}

void TestSchedule() {
  Section("limiter schedule");
  FrameLimiter limiter;
  CHECK(!limiter.Next(120, At(0)).has_value());
  const auto deadline = limiter.Next(120, At(1));
  CHECK(deadline.has_value());
  CHECK(Near(duration<double, std::milli>(*deadline - kStart).count(), 8.333));
  const auto next = limiter.Next(120, At(8.4));
  CHECK(next.has_value());
  CHECK(Near(duration<double, std::milli>(*next - kStart).count(), 16.667));
  CHECK(!limiter.Next(120, At(30)).has_value());
  const auto after = limiter.Next(120, At(31));
  CHECK(after.has_value());
  CHECK(Near(duration<double, std::milli>(*after - kStart).count(), 33.333));
}

void TestResync() {
  Section("limiter resync");
  FrameLimiter limiter;
  limiter.Next(60, At(0));
  CHECK(!limiter.Next(60, At(500)).has_value());
  const auto next = limiter.Next(60, At(501));
  CHECK(next.has_value());
  CHECK(Near(duration<double, std::milli>(*next - kStart).count(), 500 + 16.667));
}

void TestTransitions() {
  Section("limiter transitions");
  FrameLimiter limiter;
  limiter.Next(60, At(0));
  CHECK(limiter.Next(60, At(5)).has_value());
  CHECK(!limiter.Next(kFrameRateUnlimited, At(6)).has_value());
  CHECK(!limiter.Next(kFrameRateUnlimited, At(7)).has_value());
  CHECK(!limiter.Next(144, At(8)).has_value());
  const auto deadline = limiter.Next(144, At(9));
  CHECK(deadline.has_value());
  CHECK(Near(duration<double, std::milli>(*deadline - kStart).count(), 8 + 6.944));
  CHECK(!limiter.Next(30, At(10)).has_value());
  const auto thirty = limiter.Next(30, At(11));
  CHECK(thirty.has_value());
  CHECK(Near(duration<double, std::milli>(*thirty - kStart).count(), 10 + 33.333));
}

void TestWait() {
  Section("wait until");
  using std::chrono::milliseconds;
  const auto start = LimiterClock::now();
  const auto deadline = start + milliseconds(20);
  WaitUntil(deadline);
  const auto reached = LimiterClock::now();
  CHECK(reached >= deadline);
  CHECK(reached - deadline < milliseconds(100));
  const auto before = LimiterClock::now();
  WaitUntil(before - milliseconds(5));
  CHECK(LimiterClock::now() - before < milliseconds(5));
}

}  // namespace

int main() {
  TestWait();
  TestPeriods();
  TestChoices();
  TestPacingPolicy();
  TestGameDelta();
  TestGameSteps();
  TestSchedule();
  TestResync();
  TestTransitions();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
