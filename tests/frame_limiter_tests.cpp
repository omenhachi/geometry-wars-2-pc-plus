// Frame limiter tests: the period of each rate, the schedule with a synthetic clock, resynchronisation, the
// high-frame-rate rules, the game time step, the simulation pacer, and that the wait reaches its deadline.

#include <algorithm>
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

constexpr double kStep = 1.0 / 60.0;
// How far the game's time may be from real time while no time is dropped.
constexpr double kLead = GameDeltaClock::kSnapTolerance * kStep;
constexpr double kEps = 1e-9;

struct Run {
  double real = 0.0;
  double sim = 0.0;
  double drift = 0.0;
  double deviation = 0.0;
  double smallest = 1e9;
  double largest = 0.0;
};

template <class Next>
void Feed(GameDeltaClock& clock, Run& run, double seconds, Next next) {
  const double end = run.real + seconds;
  while (run.real < end) {
    const double interval = next();
    const double delta = clock.Take(interval, kStep);
    run.real += interval;
    run.sim += delta;
    run.drift = std::max(run.drift, std::fabs(run.sim - run.real));
    run.deviation = std::max(run.deviation, std::fabs(delta - interval));
    run.smallest = std::min(run.smallest, delta);
    run.largest = std::max(run.largest, delta);
  }
}

template <class Next>
Run Simulate(GameDeltaClock& clock, double seconds, Next next) {
  Run run;
  Feed(clock, run, seconds, next);
  return run;
}

bool KeepsTime(const Run& run) {
  return run.drift <= kLead + kEps;
}

bool EveryDeltaIs(const Run& run, double delta) {
  return run.smallest == delta && run.largest == delta;
}

// Frame intervals of a clock that ticks every `period`, each tick up to `jitter` early or late.
class AnchoredTicks {
 public:
  AnchoredTicks(double period, double jitter) : period_(period), jitter_(jitter) {}

  double operator()() {
    state_ = state_ * 1664525u + 1013904223u;
    const double offset = (double(state_ >> 8) / double(1u << 24) * 2.0 - 1.0) * jitter_;
    const double interval = period_ + offset - previous_offset_;
    previous_offset_ = offset;
    return interval;
  }

 private:
  double period_;
  double jitter_;
  double previous_offset_ = 0.0;
  uint32_t state_ = 12345;
};

class Cycle {
 public:
  explicit Cycle(std::vector<double> intervals) : intervals_(std::move(intervals)) {}

  double operator()() { return intervals_[next_++ % intervals_.size()]; }

 private:
  std::vector<double> intervals_;
  std::size_t next_ = 0;
};

void TestGameDeltaSteady() {
  Section("game delta at steady rates");
  for (const double fps : {60.0, 50.0, 45.0, 40.0, 35.0, 30.0, 20.0}) {
    GameDeltaClock clock;
    const Run run = Simulate(clock, 10.0, [&] { return 1.0 / fps; });
    CHECK(KeepsTime(run));
    CHECK(run.deviation <= 2 * kLead + kEps);
  }
  GameDeltaClock sixty;
  CHECK(EveryDeltaIs(Simulate(sixty, 10.0, [] { return kStep; }), kStep));
  GameDeltaClock thirty;
  CHECK(EveryDeltaIs(Simulate(thirty, 10.0, [] { return 2 * kStep; }), 2 * kStep));
  GameDeltaClock twenty;
  CHECK(EveryDeltaIs(Simulate(twenty, 10.0, [] { return 3 * kStep; }), 3 * kStep));
  for (const double fps : {45.0, 40.0, 35.0}) {
    GameDeltaClock clock;
    const Run run = Simulate(clock, 10.0, [&] { return 1.0 / fps; });
    CHECK(run.deviation < kEps);
    CHECK(Near(run.smallest * 1000, 1000.0 / fps));
  }
}

void TestGameDeltaJitter() {
  Section("game delta with jitter");
  for (const double period : {kStep, 2 * kStep}) {
    GameDeltaClock clock;
    AnchoredTicks ticks(period, 0.5 * kLead);
    CHECK(EveryDeltaIs(Simulate(clock, 10.0, ticks), period));
  }
  for (const double period : {kStep, 2 * kStep}) {
    GameDeltaClock clock;
    AnchoredTicks ticks(period, 0.004);
    const Run run = Simulate(clock, 20.0, ticks);
    CHECK(KeepsTime(run));
    CHECK(run.smallest < period);
  }
  GameDeltaClock slipping;
  const Run slip = Simulate(slipping, 10.0, [] { return kStep * 1.003; });
  CHECK(KeepsTime(slip));
  CHECK(slip.smallest == kStep);
}

void TestGameDeltaTransitions() {
  Section("game delta across rate changes");
  GameDeltaClock clock;
  Run run;
  for (const double fps : {30.0, 40.0, 45.0, 35.0, 30.0}) {
    Feed(clock, run, 4.0, [&] { return 1.0 / fps; });
    CHECK(std::fabs(run.sim - run.real) <= kLead + kEps);
  }
  CHECK(KeepsTime(run));

  GameDeltaClock sudden;
  Run steps;
  Feed(sudden, steps, 2.0, [] { return kStep; });
  Feed(sudden, steps, 2.0, [] { return 2 * kStep; });
  Feed(sudden, steps, 2.0, [] { return kStep; });
  CHECK(KeepsTime(steps));
  CHECK(steps.smallest == kStep);
  CHECK(steps.largest == 2 * kStep);
}

struct PacerRun {
  int ticks = 0;
  double real = 0.0;
  double sim = 0.0;
  double smallest = 1e9;
  double largest = 0.0;
};

void AddTick(PacerRun& run, double delta) {
  ++run.ticks;
  run.sim += delta;
  run.smallest = std::min(run.smallest, delta);
  run.largest = std::max(run.largest, delta);
}

PacerRun SimulatePacer(SimulationPacer& pacer, int frames, double interval) {
  PacerRun run;
  double delta = kStep;
  if (frames <= 0) {
    return run;
  }
  AddTick(run, kStep);
  for (int i = 1; i < frames; ++i) {
    run.real += interval;
    if (pacer.Tick(interval, kStep, delta)) {
      AddTick(run, delta);
    }
  }
  return run;
}

template <class Next>
PacerRun SimulatePacer(double seconds, Next next) {
  SimulationPacer pacer;
  PacerRun run;
  double delta = kStep;
  AddTick(run, kStep);
  while (run.real < seconds) {
    const double interval = next();
    run.real += interval;
    if (pacer.Tick(interval, kStep, delta)) {
      AddTick(run, delta);
    }
  }
  return run;
}

void TestSimulationPacer() {
  Section("simulation pacer at render rates");
  for (const double fps : {60.0, 90.0, 120.0, 144.0, 165.0, 240.0, 1000.0}) {
    SimulationPacer pacer;
    const PacerRun run = SimulatePacer(pacer, int(10.0 * fps), 1.0 / fps);
    CHECK(run.ticks == 600 || run.ticks == 601);
    CHECK(run.sim >= run.real - kEps && run.sim <= run.real + (1.0 + kEarlyStepShare) * kStep + kEps);
    CHECK(run.largest == kStep);
  }

  Section("simulation pacer with irregular frames");
  const PacerRun jittered = SimulatePacer(10.0, AnchoredTicks(1.0 / 120.0, 0.002));
  CHECK(jittered.ticks <= 601);
  CHECK(jittered.sim <= jittered.real + kStep);
  CHECK(jittered.smallest >= kStep);

  const PacerRun mixed = SimulatePacer(10.0, Cycle({1.0 / 90.0, 1.0 / 165.0, 1.0 / 240.0, 1.0 / 120.0}));
  CHECK(mixed.ticks <= 601);
  CHECK(std::fabs(mixed.sim - mixed.real) <= kStep + kEps);

  Section("simulation pacer at a jittered 60 Hz cadence");
  SimulationPacer cadence;
  int one_step_frames = 0;
  for (int i = 0; i < 600; ++i) {
    double step_delta = 0.0;
    if (cadence.Tick(kStep + (i % 2 == 0 ? -0.00005 : 0.00005), kStep, step_delta) && Near(step_delta, kStep)) {
      ++one_step_frames;
    }
  }
  CHECK(one_step_frames == 600);

  Section("simulation pacer below 60");
  SimulationPacer slow;
  const PacerRun thirty = SimulatePacer(slow, 300, 1.0 / 30.0);
  CHECK(thirty.ticks == 300);
  CHECK(std::fabs(thirty.sim - thirty.real) <= kStep + kEps);
  CHECK(thirty.smallest == kStep);
  CHECK(Near(thirty.largest, 2 * kStep));

  SimulationPacer slipping;
  const PacerRun forty_five = SimulatePacer(slipping, 450, 1.0 / 45.0);
  CHECK(forty_five.ticks == 450);
  CHECK(std::fabs(forty_five.sim - forty_five.real) <= kStep + kEps);
  CHECK(Near(forty_five.largest, 1.0 / 45.0));

  Section("simulation pacer bounds catch-up");
  SimulationPacer stalled;
  double delta = kStep;
  stalled.Tick(kStep, kStep, delta);
  CHECK(stalled.Tick(0.5, kStep, delta));
  CHECK(Near(delta, 3 * kStep));
  CHECK(stalled.Tick(kStep, kStep, delta));
  CHECK(Near(delta, kStep));

  Section("simulation pacer across mode changes");
  SimulationPacer changing;
  const PacerRun first = SimulatePacer(changing, 600, kStep);
  changing.Reset();
  const PacerRun high = SimulatePacer(changing, 1200, 1.0 / 120.0);
  changing.Reset();
  const PacerRun last = SimulatePacer(changing, 600, kStep);
  CHECK(first.ticks + high.ticks + last.ticks == 1800);
  CHECK(Near(first.sim + high.sim + last.sim, 30.0));
}

void TestFixedStepGate() {
  Section("visual clock gate at render rates");
  for (const double fps : {60.0, 120.0, 144.0, 240.0, 1000.0}) {
    FixedStepGate gate;
    int ticks = 0;
    for (int i = 0; i < int(10.0 * fps); ++i) {
      if (gate.Tick(1.0 / fps, kStep)) {
        ++ticks;
      }
    }
    CHECK(ticks == 600);
  }

  Section("visual clock gate at a jittered 60 Hz cadence");
  FixedStepGate cadence;
  int cadence_ticks = 0;
  for (int i = 0; i < 600; ++i) {
    if (cadence.Tick(kStep + (i % 2 == 0 ? -0.00005 : 0.00005), kStep)) {
      ++cadence_ticks;
    }
  }
  CHECK(cadence_ticks == 600);

  Section("visual clock gate does not catch up");
  FixedStepGate stalled;
  CHECK(stalled.Tick(0.5, kStep));
  CHECK(!stalled.Tick(0.001, kStep));
  CHECK(!stalled.Tick(0.001, kStep));
  CHECK(stalled.Tick(kStep, kStep));
}

void TestSimulationGatePolicy() {
  Section("simulation gate policy");
  CHECK(!GatesSimulation(30));
  CHECK(!GatesSimulation(60));
  for (const int fps : {90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(GatesSimulation(fps));
  }
}

void TestGameDeltaBursts() {
  Section("game delta with bursts");
  GameDeltaClock after_stall;
  CHECK(Near(after_stall.Take(0.044, kStep) * 1000, 44.0));
  CHECK(Near(after_stall.Take(0.002, kStep) * 1000, 2.0));
  CHECK(Near(after_stall.Take(0.002, kStep) * 1000, 2.0));

  const std::vector<std::vector<double>> patterns = {
      {0.044, 0.002, 0.002}, {0.008, 0.036}, {1.0 / 30, 0.002, 0.002, kStep}, {0.032, 0.003, 0.003, 0.003, 0.032}};
  for (const auto& pattern : patterns) {
    GameDeltaClock clock;
    CHECK(KeepsTime(Simulate(clock, 10.0, Cycle(pattern))));
  }

  Section("game delta after a stall");
  GameDeltaClock stalled;
  CHECK(Near(stalled.Take(0.120, kStep) * 1000, 50.0));
  CHECK(stalled.owed() == 0.0);
  Run burst;
  Feed(stalled, burst, 0.010, [] { return 0.002; });
  CHECK(burst.deviation < kEps);
  CHECK(Near(stalled.Take(kStep, kStep) * 1000, 16.667));

  Section("game delta below the minimum step");
  GameDeltaClock tiny;
  double sim = 0.0;
  for (int i = 0; i < 1000; ++i) {
    sim += tiny.Take(0.0001, kStep);
  }
  CHECK(sim <= 1000 * kGameDeltaMin + kEps);
  CHECK(tiny.owed() >= -0.5 * kStep);
  GameDeltaClock invalid;
  CHECK(invalid.Take(0.0, kStep) == kGameDeltaMin);
  CHECK(invalid.Take(-1.0, kStep) == kGameDeltaMin);
  CHECK(std::fabs(invalid.owed()) <= 0.5 * kStep);
}

void TestGameDeltaCap() {
  Section("game delta cap");
  GameDeltaClock near_cap;
  CHECK(Near(near_cap.Take(0.049, kStep) * 1000, 50.0));
  CHECK(Near(near_cap.owed() * 1000, -1.0));
  GameDeltaClock over_cap;
  CHECK(Near(over_cap.Take(0.0501, kStep) * 1000, 50.0));
  CHECK(over_cap.owed() == 0.0);
  GameDeltaClock long_stall;
  CHECK(Near(long_stall.Take(30.0, kStep) * 1000, 50.0));
  CHECK(long_stall.owed() == 0.0);
  CHECK(long_stall.Take(kStep, kStep) == kStep);

  GameDeltaClock periodic;
  int frame = 0;
  int stalls = 0;
  const Run run = Simulate(periodic, 10.0, [&] {
    const bool stall = frame++ % 20 == 19;
    stalls += stall;
    return stall ? 0.07 : kStep;
  });
  CHECK(run.largest <= 3 * kStep + kEps);
  CHECK(std::fabs((run.real - run.sim) - stalls * 0.02) <= kLead + kEps);
}

void TestGameDeltaSnapBoundaries() {
  Section("game delta snap boundaries");
  struct Case {
    double steps;
    int snapped;
  };
  const Case cases[] = {{0.81, 1}, {0.79, 0}, {1.19, 1}, {1.21, 0}, {1.81, 2}, {1.79, 0},
                        {2.19, 2}, {2.21, 0}, {2.81, 3}, {2.79, 0}, {3.00, 3}};
  for (const Case& c : cases) {
    GameDeltaClock clock;
    const double delta = clock.Take(c.steps * kStep, kStep);
    if (c.snapped) {
      CHECK(delta == c.snapped * kStep);
      CHECK(Near(clock.owed() / kStep, c.steps - c.snapped));
    } else {
      CHECK(Near(delta, c.steps * kStep));
      CHECK(clock.owed() == 0.0);
    }
  }

  Section("game delta conserves time at every interval");
  bool conserved = true;
  for (const double lead : {-0.19, 0.0, 0.19}) {
    GameDeltaClock prepared;
    prepared.Take((1.0 + lead) * kStep, kStep);
    for (double steps = 0.5; steps < 3.3; steps += 0.01) {
      GameDeltaClock clock = prepared;
      const double target = steps * kStep + clock.owed();
      const double delta = clock.Take(steps * kStep, kStep);
      conserved &= delta >= kGameDeltaMin && delta <= 3 * kStep + kEps;
      if (target <= 3 * kStep) {
        conserved &= std::fabs(delta + clock.owed() - target) < kEps;
        conserved &= std::fabs(clock.owed()) <= kLead + kEps;
      } else {
        conserved &= clock.owed() == 0.0;
      }
    }
  }
  CHECK(conserved);

  Section("game delta carry");
  GameDeltaClock carried;
  CHECK(carried.Take(1.15 * kStep, kStep) == kStep);
  CHECK(Near(carried.owed() / kStep, 0.15));
  CHECK(carried.Take(1.15 * kStep, kStep) > 1.25 * kStep);
  CHECK(carried.owed() == 0.0);
  GameDeltaClock reset;
  reset.Take(1.15 * kStep, kStep);
  reset.Reset();
  CHECK(reset.owed() == 0.0);
  CHECK(reset.Take(1.15 * kStep, kStep) == kStep);
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
  TestGameDeltaSteady();
  TestGameDeltaJitter();
  TestGameDeltaTransitions();
  TestSimulationGatePolicy();
  TestFixedStepGate();
  TestSimulationPacer();
  TestGameDeltaBursts();
  TestGameDeltaCap();
  TestGameDeltaSnapBoundaries();
  TestSchedule();
  TestResync();
  TestTransitions();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
