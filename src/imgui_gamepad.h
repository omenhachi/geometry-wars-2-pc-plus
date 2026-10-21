#pragma once

#include <vector>

#include <SDL3/SDL_gamepad.h>

struct ImGuiIO;

namespace gw2 {

// Feeds every connected SDL controller to Dear ImGui navigation. Release() lets go of every key, so nothing read here
// stays held in the game.
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
