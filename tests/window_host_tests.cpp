// Window title tests: the canonical title, the frame-rate format and the one-second sampler, with synthetic time.
// Pure logic only: the window, the input listener and the VdSwap hook need the running runtime.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include "window_host.h"

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

void TestTitles() {
  Section("window titles");
  CHECK(std::string(kWindowTitle) == "Geometry Wars: Retro Evolved 2");
  CHECK(WindowTitle(false, 60.0) == "Geometry Wars: Retro Evolved 2");
  CHECK(WindowTitle(false, 59.94) == "Geometry Wars: Retro Evolved 2");
  CHECK(WindowTitle(true, 60.0) == "Geometry Wars: Retro Evolved 2 | FPS: 60.00");
  CHECK(WindowTitle(true, 59.94) == "Geometry Wars: Retro Evolved 2 | FPS: 59.94");
  CHECK(WindowTitle(true, 0.5) == "Geometry Wars: Retro Evolved 2 | FPS: 0.50");
}

void TestSamplerWaitsForOneSecond() {
  Section("frame rate sampler");
  using Clock = FrameRateSampler::Clock;
  const Clock::time_point origin = Clock::now();
  FrameRateSampler sampler;
  CHECK(!sampler.Count(origin).has_value());
  for (int frame = 1; frame < 60; ++frame) {
    CHECK(!sampler.Count(origin + std::chrono::milliseconds(frame * 1000 / 60)).has_value());
  }
  const auto first = sampler.Count(origin + std::chrono::milliseconds(1000));
  CHECK(first.has_value() && first->fps == 60.0);
  CHECK(first.has_value() && std::fabs(first->frame_time_ms - 1000.0 / 60.0) < 1e-9);
  CHECK(first.has_value() && first->frame_count == 60);
}

void TestSamplerRestartsEachInterval() {
  Section("frame rate sampler restarts");
  using Clock = FrameRateSampler::Clock;
  const Clock::time_point origin = Clock::now();
  FrameRateSampler sampler;
  CHECK(!sampler.Count(origin).has_value());
  for (int frame = 1; frame <= 60; ++frame) {
    sampler.Count(origin + std::chrono::milliseconds(frame * 1000 / 60));
  }
  for (int frame = 61; frame < 120; ++frame) {
    CHECK(!sampler.Count(origin + std::chrono::milliseconds(1000 + (frame - 60) * 1000 / 60)).has_value());
  }
  const auto second = sampler.Count(origin + std::chrono::milliseconds(2000));
  CHECK(second.has_value() && second->fps == 60.0);
  CHECK(second.has_value() && second->frame_count == 120);
}

void TestSamplerReportsSlowerRates() {
  Section("frame rate sampler, slower game");
  using Clock = FrameRateSampler::Clock;
  const Clock::time_point origin = Clock::now();
  FrameRateSampler sampler;
  CHECK(!sampler.Count(origin).has_value());
  std::optional<GuestFrameStats> stats;
  for (int frame = 1; frame <= 15 && !stats; ++frame) {
    stats = sampler.Count(origin + std::chrono::milliseconds(frame * 1000 / 15));
  }
  CHECK(stats.has_value() && stats->fps == 15.0);
  CHECK(stats.has_value() && std::fabs(stats->frame_time_ms - 1000.0 / 15.0) < 1e-9);
  CHECK(stats.has_value() && stats->frame_count == 15);
}

void TestCursorHider() {
  Section("cursor hider");
  using Clock = CursorHider::Clock;
  const auto at = [](int ms) { return Clock::time_point{} + std::chrono::milliseconds(ms); };
  const std::optional<bool> hide = false, show = true;

  CursorHider launcher;
  CHECK(!launcher.Update(false, true, true, at(0)) && !launcher.Update(false, true, true, at(60000)));
  CursorHider windowed;
  CHECK(!windowed.Update(true, false, true, at(0)) && !windowed.Update(true, false, true, at(60000)));

  CursorHider c;
  CHECK(!c.Update(true, true, true, at(0)) && c.visible());
  CHECK(!c.Update(true, true, true, at(4999)));
  CHECK(c.Update(true, true, true, at(5000)) == hide);
  CHECK(!c.Update(true, true, true, at(5100)));

  c.MouseMoved(at(6000));
  CHECK(c.Update(true, true, true, at(6000)) == show);
  c.MouseMoved(at(8000));
  CHECK(!c.Update(true, true, true, at(12999)) && c.Update(true, true, true, at(13000)) == hide);

  CHECK(c.Update(true, false, true, at(13100)) == show);
  CHECK(!c.Update(true, true, true, at(20000)) && !c.Update(true, true, true, at(24999)));
  CHECK(c.Update(true, true, true, at(25000)) == hide);

  CHECK(c.Update(true, true, false, at(25100)) == show);
  CHECK(!c.Update(true, true, true, at(30000)) && !c.Update(true, true, true, at(34999)));
  CHECK(c.Update(true, true, true, at(35000)) == hide);

  c.MouseMoved(at(36000));
  CHECK(c.Update(true, true, true, at(36000)) == show);
  CHECK(c.Update(true, true, true, at(41000)) == hide);
  CHECK(c.Update(false, true, true, at(41100)) == show);
}

}  // namespace

int main() {
  TestCursorHider();
  TestTitles();
  TestSamplerWaitsForOneSecond();
  TestSamplerRestartsEachInterval();
  TestSamplerReportsSlowerRates();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
