// Window title, frame-rate sampler and last-input cursor tests.
// Pure logic only: the window, the input listener, the controller watcher and the VdSwap hook need the running runtime.

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
  const std::string game = "Geometry Wars: Retro Evolved 2";
  const WindowSize render{3360, 1440};
  CHECK(std::string(kWindowTitle) == game);

  CHECK(WindowTitle(false, render, false, std::nullopt) == game);
  CHECK(WindowTitle(true, render, false, std::nullopt) == game + " | 3360x1440");
  CHECK(WindowTitle(false, render, true, std::nullopt) == game);
  CHECK(WindowTitle(true, render, true, std::nullopt) == game + " | 3360x1440");
  CHECK(WindowTitle(false, render, true, 60.0) == game + " | FPS: 60.00");
  CHECK(WindowTitle(true, render, true, 60.0) == game + " | 3360x1440 | FPS: 60.00");

  CHECK(WindowTitle(false, render, true, 59.94) == game + " | FPS: 59.94");
  CHECK(WindowTitle(true, render, true, 0.5) == game + " | 3360x1440 | FPS: 0.50");
  CHECK(WindowTitle(true, {1280, 720}, false, 60.0) == game + " | 1280x720");
  CHECK(WindowTitle(true, {2560, 1440}, true, 59.94) == game + " | 2560x1440 | FPS: 59.94");
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

const std::optional<bool> hide = false, show = true;

void TestCursorInitialState() {
  Section("cursor initial state");
  CursorHider c;
  CHECK(c.visible());
  CHECK(!c.Update(true, true, false).has_value());
  CHECK(c.visible());
}

void TestCursorMouseShows() {
  Section("mouse shows the cursor");
  CursorHider c;
  c.KeyboardInput();
  CHECK(c.Update(true, true, false) == hide);

  c.MouseMoved();
  CHECK(c.Update(true, true, false) == show);

  c.KeyboardInput();
  CHECK(c.Update(true, true, false) == hide);

  c.MouseButtonPressed();
  CHECK(c.Update(true, true, false) == show);

  c.KeyboardInput();
  CHECK(c.Update(true, true, false) == hide);

  c.MouseWheelMoved();
  CHECK(c.Update(true, true, false) == show);
}

void TestCursorKeyboardHides() {
  Section("keyboard hides the cursor");
  CursorHider c;
  c.KeyboardInput();
  CHECK(c.Update(true, true, false) == hide);
  CHECK(!c.visible());
  CHECK(!c.Update(true, true, false).has_value());
  CHECK(!c.visible());
}

void TestCursorControllerHides() {
  Section("controller hides the cursor");
  CursorHider c;
  c.ControllerInput();
  CHECK(c.Update(true, true, false) == hide);
  c.MouseMoved();
  CHECK(c.Update(true, true, false) == show);
}

void TestControllerActivity() {
  Section("controller activity");
  const ControllerActivity none;

  ControllerActivity button;
  button.buttons = 1u;
  CHECK(button.HasNewInput(none));
  CHECK(!button.HasNewInput(button));
  ControllerActivity second = button;
  second.buttons |= 1u << 1;
  CHECK(second.HasNewInput(button));
  CHECK(!button.HasNewInput(second));

  ControllerActivity dpad;
  dpad.dpad = 1u;
  CHECK(dpad.HasNewInput(none));
  CHECK(!dpad.HasNewInput(dpad));

  ControllerActivity trigger;
  trigger.left_trigger = ControllerTriggerPressed(kControllerTriggerThreshold - 1);
  CHECK(!trigger.left_trigger);
  trigger.left_trigger = ControllerTriggerPressed(kControllerTriggerThreshold);
  CHECK(trigger.left_trigger);
  CHECK(trigger.HasNewInput(none));
  CHECK(!trigger.HasNewInput(trigger));

  ControllerActivity right_trigger;
  right_trigger.right_trigger = ControllerTriggerPressed(kControllerTriggerThreshold);
  CHECK(right_trigger.HasNewInput(none));

  ControllerActivity stick;
  stick.left_stick = ControllerStickMoved(300, -400);
  CHECK(!stick.left_stick);
  stick.left_stick = ControllerStickMoved(kControllerStickDeadzone, 0);
  CHECK(stick.left_stick);
  stick.right_stick = ControllerStickMoved(0, -kControllerStickDeadzone);
  CHECK(stick.right_stick);
  CHECK(stick.HasNewInput(none));
  CHECK(!stick.HasNewInput(stick));

  ControllerActivity drift;
  drift.left_stick = ControllerStickMoved(kControllerStickDeadzone - 1, -(kControllerStickDeadzone - 1));
  CHECK(!drift.HasNewInput(none));
}

void TestCursorOverlay() {
  Section("overlay forces the cursor visible");
  CursorHider keyboard;
  keyboard.KeyboardInput();
  CHECK(keyboard.Update(true, true, false) == hide);
  CHECK(keyboard.Update(true, true, true) == show);
  keyboard.MouseMoved();
  CHECK(!keyboard.Update(true, true, true).has_value());
  CHECK(keyboard.Update(true, true, false) == hide);

  CursorHider mouse;
  mouse.ControllerInput();
  CHECK(mouse.Update(true, true, false) == hide);
  mouse.MouseMoved();
  CHECK(mouse.Update(true, true, false) == show);
  CHECK(!mouse.Update(true, true, true).has_value());
  mouse.KeyboardInput();
  CHECK(!mouse.Update(true, true, true).has_value());
  CHECK(!mouse.Update(true, true, false).has_value());
  CHECK(mouse.visible());
}

void TestCursorFocus() {
  Section("focus and the cursor");
  CursorHider c;
  c.KeyboardInput();
  CHECK(c.Update(true, true, false) == hide);
  CHECK(c.Update(true, false, false) == show);
  CHECK(c.Update(true, true, false) == hide);

  CHECK(c.Update(true, false, true) == show);
  CHECK(!c.Update(true, true, true).has_value());
  CHECK(c.Update(true, true, false) == hide);
}

void TestCursorWindowModes() {
  Section("windowed and borderless fullscreen gameplay");
  CursorHider windowed;
  windowed.KeyboardInput();
  CHECK(windowed.Update(true, true, false) == hide);

  CursorHider fullscreen;
  fullscreen.KeyboardInput();
  CHECK(fullscreen.Update(true, true, false) == hide);
  for (int update = 0; update < 300; ++update) {
    CHECK(!fullscreen.Update(true, true, false).has_value());
  }
}

void TestCursorLauncher() {
  Section("launcher keeps the cursor visible");
  CursorHider c;
  c.KeyboardInput();
  CHECK(!c.Update(false, true, false).has_value());
  CHECK(c.visible());
  c.ControllerInput();
  CHECK(!c.Update(false, true, false).has_value());
  CHECK(c.visible());
  CHECK(!c.Update(false, false, true).has_value());
  CHECK(c.visible());
}

}  // namespace

int main() {
  TestCursorInitialState();
  TestCursorMouseShows();
  TestCursorKeyboardHides();
  TestCursorControllerHides();
  TestControllerActivity();
  TestCursorOverlay();
  TestCursorFocus();
  TestCursorWindowModes();
  TestCursorLauncher();
  TestTitles();
  TestSamplerWaitsForOneSecond();
  TestSamplerRestartsEachInterval();
  TestSamplerReportsSlowerRates();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
