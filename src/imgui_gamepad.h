#pragma once

#include <vector>

#include <SDL3/SDL_gamepad.h>

struct ImGuiIO;

namespace gw2 {

// Feeds the controllers SDL recognizes to Dear ImGui, so the launcher and the in-game overlays navigate on any pad
// the game itself does: an Xbox pad, a DualSense, a Steam Controller. Every connected controller is read and their
// inputs combined, so any of them navigates, and pads added or removed while it is open are followed.
//
// Call Update() once per frame while the dialog is open and Release() when it closes. Release() lets go of every
// key, so nothing read here is left held in the game.
class ImGuiGamepad {
 public:
  ImGuiGamepad() = default;
  ~ImGuiGamepad();
  ImGuiGamepad(const ImGuiGamepad&) = delete;
  ImGuiGamepad& operator=(const ImGuiGamepad&) = delete;

  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  void DrainEvents();
  void CloseAll();

  std::vector<SDL_Gamepad*> pads_;
  bool subsystem_ = false;
};

}  // namespace gw2
