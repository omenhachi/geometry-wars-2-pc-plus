#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>

#include <rex/input/state_merge.h>

namespace gw2 {

// Mouse activity shows the cursor; keyboard or controller activity hides it.
// Overlays and a lost focus force it visible until they go away.
class CursorHider {
 public:
  bool visible() const { return visible_; }

  void MouseMoved() { SetDevice(Device::kMouse); }
  void MouseButtonPressed() { SetDevice(Device::kMouse); }
  void MouseWheelMoved() { SetDevice(Device::kMouse); }
  void KeyboardInput() { SetDevice(Device::kKeyboard); }
  void ControllerInput() { SetDevice(Device::kController); }

  // Returns the new visibility when it changes, otherwise nullopt.
  std::optional<bool> Update(bool gameplay, bool focused, bool ui_active) {
    if (!gameplay) {
      device_ = Device::kMouse;
      device_before_ui_ = Device::kMouse;
    } else if (ui_active && !ui_active_) {
      device_before_ui_ = device_;
    } else if (!ui_active && ui_active_) {
      device_ = device_before_ui_;
    }
    ui_active_ = gameplay && ui_active;
    const bool active = gameplay && focused && !ui_active;
    const bool visible = !active || device_ == Device::kMouse;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

 private:
  enum class Device { kMouse, kKeyboard, kController };

  void SetDevice(Device device) { device_ = device; }

  Device device_ = Device::kMouse;
  Device device_before_ui_ = Device::kMouse;
  bool ui_active_ = false;
  bool visible_ = true;
};

// Reports only the start of meaningful controller input, not a held button,
// stick, or trigger, so mouse movement is not overridden by input already down.
class ControllerActivityFilter {
 public:
  bool Update(const rex::input::X_INPUT_GAMEPAD& gamepad);

 private:
  uint16_t buttons_ = 0;
  bool left_trigger_ = false;
  bool right_trigger_ = false;
  bool thumb_lx_ = false;
  bool thumb_ly_ = false;
  bool thumb_rx_ = false;
  bool thumb_ry_ = false;
};

inline bool ControllerActivityFilter::Update(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  const uint16_t buttons = static_cast<uint16_t>(gamepad.buttons);
  bool started = (buttons & ~buttons_) != 0;
  buttons_ = buttons;

  const auto trigger_started = [&started](uint8_t value, bool& was_active) {
    const bool active = value >= rex::input::kTriggerThreshold;
    started = started || (active && !was_active);
    was_active = active;
  };
  const auto thumb_started = [&started](rex::be<int16_t> value, bool& was_active) {
    const bool active = std::abs(static_cast<int>(value.get())) >= rex::input::kThumbDeadzone;
    started = started || (active && !was_active);
    was_active = active;
  };

  trigger_started(gamepad.left_trigger, left_trigger_);
  trigger_started(gamepad.right_trigger, right_trigger_);
  thumb_started(gamepad.thumb_lx, thumb_lx_);
  thumb_started(gamepad.thumb_ly, thumb_ly_);
  thumb_started(gamepad.thumb_rx, thumb_rx_);
  thumb_started(gamepad.thumb_ry, thumb_ry_);
  return started;
}

}  // namespace gw2
