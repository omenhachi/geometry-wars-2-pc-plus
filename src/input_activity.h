#pragma once

#include <functional>

#include <rex/input/device.h>
#include <rex/input/input.h>

namespace rex::input {

// A passive hook added by patches/rexglue-sdk/0047-input-activity-callback.patch.
// Called under the runtime's activity lock, on the guest thread whose poll saw a device's state change.
void SetInputActivityCallback(
    std::function<void(DeviceId device, const X_INPUT_GAMEPAD& gamepad)> callback);

}  // namespace rex::input
