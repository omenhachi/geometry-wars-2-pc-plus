// Window title, frame-rate sampler and last-input cursor tests.
// Pure logic and source checks only: the window, the input listener and the VdSwap hook need the running runtime.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "cursor_input.h"
#include "input_activity.h"
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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadSource(const char* name) {
  return ReadFile(std::string(GW2_SRC_DIR) + "/" + name);
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

rex::input::X_INPUT_GAMEPAD Pad(uint16_t buttons = 0, int16_t thumb_lx = 0, int16_t thumb_ly = 0,
                                int16_t thumb_rx = 0, int16_t thumb_ry = 0,
                                uint8_t left_trigger = 0, uint8_t right_trigger = 0) {
  rex::input::X_INPUT_GAMEPAD pad{};
  pad.buttons.set(buttons);
  pad.thumb_lx.set(thumb_lx);
  pad.thumb_ly.set(thumb_ly);
  pad.thumb_rx.set(thumb_rx);
  pad.thumb_ry.set(thumb_ry);
  pad.left_trigger = left_trigger;
  pad.right_trigger = right_trigger;
  return pad;
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
  for (int repeat = 0; repeat < 100; ++repeat) {
    c.KeyboardInput();
    CHECK(!c.Update(true, true, false).has_value());
  }
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

void TestControllerActivityFilter() {
  Section("controller activity filter");
  using namespace rex::input;

  ControllerActivityFilter buttons;
  CHECK(!buttons.Update(Pad()));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_B)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(!buttons.Update(Pad()));

  ControllerActivityFilter dpad;
  CHECK(dpad.Update(Pad(X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(!dpad.Update(Pad(X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(dpad.Update(Pad(X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_DPAD_RIGHT)));
  CHECK(!dpad.Update(Pad(X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_DPAD_RIGHT)));

  ControllerActivityFilter thumbs;
  CHECK(!thumbs.Update(Pad()));
  CHECK(!thumbs.Update(Pad(0, kThumbDeadzone - 1, 0, 0, 0)));
  CHECK(thumbs.Update(Pad(0, kThumbDeadzone, 0, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, kThumbDeadzone + 2000, 0, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, 0, 0, 0, 0)));
  CHECK(thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, 0, -kThumbDeadzone, kThumbDeadzone - 1, 0)));
  CHECK(thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, kThumbDeadzone)));
  CHECK(!thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, kThumbDeadzone + 4000)));

  ControllerActivityFilter triggers;
  CHECK(!triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold - 1, kTriggerThreshold - 1)));
  CHECK(triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!triggers.Update(Pad()));
  CHECK(triggers.Update(Pad(0, 0, 0, 0, 0, 0, kTriggerThreshold)));

  ControllerActivityFilter drift;
  for (int16_t value : {0, 100, -100, 1000, -1000, kThumbDeadzone - 1, -(kThumbDeadzone - 1), 0}) {
    CHECK(!drift.Update(Pad(0, value, 0, 0, 0)));
  }
}

void TestControllerActivityDrivesCursor() {
  Section("controller activity hides the cursor once per press");
  using namespace rex::input;

  ControllerActivityFilter filter;
  CursorHider c;

  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_A)));
  c.ControllerInput();
  CHECK(c.Update(true, true, false) == hide);

  c.MouseMoved();
  CHECK(c.Update(true, true, false) == show);
  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(c.visible());

  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_B)));
  c.ControllerInput();
  CHECK(c.Update(true, true, false) == hide);

  c.MouseMoved();
  CHECK(c.Update(true, true, false) == show);
  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_B)));
  CHECK(c.visible());

  CHECK(!filter.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(filter.Update(Pad(X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_DPAD_LEFT)));
  c.ControllerInput();
  CHECK(c.Update(true, true, false) == hide);

  ControllerActivityFilter stick;
  CursorHider stick_cursor;
  stick_cursor.KeyboardInput();
  CHECK(stick_cursor.Update(true, true, false) == hide);
  CHECK(!stick.Update(Pad(0, kThumbDeadzone - 1, 0, 0, 0)));
  stick_cursor.MouseMoved();
  CHECK(stick_cursor.Update(true, true, false) == show);
  CHECK(!stick.Update(Pad(0, kThumbDeadzone - 1, 0, 0, 0)));
  CHECK(stick_cursor.visible());
  CHECK(stick.Update(Pad(0, kThumbDeadzone, 0, 0, 0)));
  stick_cursor.ControllerInput();
  CHECK(stick_cursor.Update(true, true, false) == hide);
  for (int held = 0; held < 100; ++held) {
    CHECK(!stick.Update(Pad(0, kThumbDeadzone + 5000, 0, 0, 0)));
  }
  stick_cursor.MouseMoved();
  CHECK(stick_cursor.Update(true, true, false) == show);
  CHECK(!stick.Update(Pad(0, kThumbDeadzone + 5000, 0, 0, 0)));
  CHECK(stick_cursor.visible());
  CHECK(!stick.Update(Pad(0, 0, 0, 0, 0)));
  CHECK(stick.Update(Pad(0, kThumbDeadzone, 0, 0, 0)));
  stick_cursor.ControllerInput();
  CHECK(stick_cursor.Update(true, true, false) == hide);

  ControllerActivityFilter trigger;
  CursorHider trigger_cursor;
  trigger_cursor.KeyboardInput();
  CHECK(trigger_cursor.Update(true, true, false) == hide);
  CHECK(!trigger.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold - 1, 0)));
  trigger_cursor.MouseMoved();
  CHECK(trigger_cursor.Update(true, true, false) == show);
  CHECK(!trigger.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold - 1, 0)));
  CHECK(trigger_cursor.visible());
  CHECK(trigger.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  trigger_cursor.ControllerInput();
  CHECK(trigger_cursor.Update(true, true, false) == hide);
  for (int held = 0; held < 100; ++held) {
    CHECK(!trigger.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  }
  trigger_cursor.MouseMoved();
  CHECK(trigger_cursor.Update(true, true, false) == show);
  CHECK(!trigger.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(trigger_cursor.visible());
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

void TestCursorWiring() {
  Section("cursor wiring");
  const std::string host = ReadSource("window_host.cpp");
  const std::string header = ReadSource("window_host.h");
  const std::string cursor = ReadSource("cursor_input.h");
  const std::string activity = ReadSource("input_activity.h");

  CHECK(Has(host, "SetInputActivityCallback(OnControllerActivity)"));
  CHECK(Has(host, "SetInputActivityCallback(nullptr)"));
  CHECK(Has(host, "g_controller_filters"));
  CHECK(Has(host, "CallInUIThread"));
  CHECK(Has(host, "ControllerActivityFilter"));
  CHECK(Has(header, "CreateCursorSync"));
  CHECK(Has(activity, "SetInputActivityCallback"));

  CHECK(Has(host, "void OnMouseMove(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseDown(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseWheel(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnKeyDown(rex::ui::KeyEvent& e) override"));
  CHECK(Has(host, "void OnLostFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "void OnGotFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "!e.prev_state()"));

  CHECK(Has(cursor, "rex::input::kThumbDeadzone"));
  CHECK(Has(cursor, "rex::input::kTriggerThreshold"));
  CHECK(Has(cursor, "buttons & ~buttons_"));
  CHECK(Has(cursor, "active && !was_active"));
  CHECK(!Has(cursor, "steady_clock"));
  CHECK(!Has(cursor, "last_motion"));

  CHECK(!Has(host, "GamepadWatcher"));
  CHECK(!Has(host, "g_gamepads"));
  CHECK(!Has(host, "CursorTick"));
  CHECK(!Has(host, "SDL_InitSubSystem"));
  CHECK(!Has(host, "SDL_OpenGamepad"));
  CHECK(!Has(host, "SDL_GetGamepad"));
  CHECK(!Has(host, "SDL_UpdateJoysticks"));
  CHECK(!Has(host, "SDL_INIT_GAMEPAD"));
  CHECK(!Has(host, "kCursorHideDelay"));
  CHECK(!Has(host, "CursorHider::Clock"));
  CHECK(!Has(host, "last_motion_"));
  CHECK(!Has(cursor, "kCursorHideDelay"));
  CHECK(!Has(cursor, "last_motion_"));

  const size_t update_start = host.find("void UpdateCursor()");
  const size_t restore_start = host.find("void RestoreCursor()", update_start);
  CHECK(update_start != std::string::npos && restore_start != std::string::npos);
  if (update_start != std::string::npos && restore_start != std::string::npos) {
    CHECK(!Has(host.substr(update_start, restore_start - update_start), "IsFullscreen"));
  }

  const size_t detach = host.find("void DetachWindowHost()");
  const size_t unregister = host.find("SetInputActivityCallback(nullptr)", detach);
  const size_t clear_filters = host.find("g_controller_filters.clear()", detach);
  const size_t clear_window = host.find("g_window = nullptr;", detach);
  CHECK(detach != std::string::npos && unregister != std::string::npos);
  CHECK(clear_filters != std::string::npos && clear_window != std::string::npos);
  CHECK(unregister < clear_filters && clear_filters < clear_window);

  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "gw2::CreateCursorSync(drawer)"));
  CHECK(Has(app, "CursorUiActive"));
  CHECK(Has(app, "gw2::DetachWindowHost();"));

  const std::string cheat = ReadSource("cheat_menu.cpp");
  CHECK(Has(cheat, "GameInputActive()"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(!Has(launcher, "AttachWindowHost"));
  CHECK(!Has(launcher, "SetInputActivityCallback"));
}

void TestInputActivityPatch() {
  Section("input activity patch");
  const std::string root = std::string(GW2_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string patch = ReadFile(root + "0047-input-activity-callback.patch");
  CHECK(Has(patch, "SetInputActivityCallback"));
  CHECK(Has(patch, "NotifyInputActivity"));
  CHECK(Has(patch, "std::memcmp"));
  CHECK(Has(patch, "g_input_activity_callback(id, gamepad)"));
  CHECK(Has(ReadFile(root + "patches.txt"), "0047-input-activity-callback.patch"));
}

}  // namespace

int main() {
  TestCursorInitialState();
  TestCursorMouseShows();
  TestCursorKeyboardHides();
  TestCursorControllerHides();
  TestControllerActivityFilter();
  TestControllerActivityDrivesCursor();
  TestCursorOverlay();
  TestCursorFocus();
  TestCursorWindowModes();
  TestCursorLauncher();
  TestCursorWiring();
  TestInputActivityPatch();
  TestTitles();
  TestSamplerWaitsForOneSecond();
  TestSamplerRestartsEachInterval();
  TestSamplerReportsSlowerRates();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
