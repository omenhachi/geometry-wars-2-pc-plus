#pragma once

#include <functional>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

namespace gw2 {

// F10 opens and closes the in-game cheat menu. It changes the cheats for this session only; the launcher's saved
// settings are never written from here. `can_open` lets the app keep the menu away from other overlays.
void AttachCheatMenu(rex::ui::Window* window, rex::ui::ImGuiDrawer* drawer, std::function<bool()> can_open);
void DetachCheatMenu();

bool CheatMenuOpen();

// Whether the game may read the keyboard and controller. It may not while the menu is open or the keys and buttons
// that were pressed in it are still held.
bool GameInputActive();

}  // namespace gw2
