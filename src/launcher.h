#pragma once

#include <filesystem>
#include <functional>

#include "user_data.h"

struct ImFont;
struct ImFontAtlas;
struct ImVec4;

namespace rex {
class Runtime;
namespace system {
class AchievementManager;
}  // namespace system
namespace ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace ui
}  // namespace rex

namespace gw2 {

class GameSettings;

void LoadLauncherFonts(ImFontAtlas* atlas, float density);

// The launcher's UI fonts and accent, also used by the in-game cheat menu. The fonts are null when the system has none.
ImFont* LauncherRegularFont();
ImFont* LauncherSemiboldFont();
ImVec4 LauncherAccent();

struct LauncherPaths {
  std::filesystem::path game_dir;
  std::filesystem::path user_data_root;
  UserDataOutcome user_data_outcome = UserDataOutcome::kPortable;
};

// Empty when the launcher opens before the runtime exists, which is only when the game files are not installed.
struct LauncherGameContext {
  rex::Runtime* runtime = nullptr;
  rex::ui::ImmediateDrawer* immediate_drawer = nullptr;
  rex::system::AchievementManager* achievements = nullptr;
};

struct LauncherCallbacks {
  std::function<void()> play;     // starts the game in this window, once the launcher has closed
  std::function<bool()> restart;  // starts the game in a new window; false when that could not start
  std::function<void()> quit;
  std::function<void*()> window_handle;      // the native window the launcher is drawn in
  std::function<bool()> window_fullscreen;
  std::function<void(bool)> set_window_fullscreen;  // the launcher's own F11 state, not the game's Display mode
};

// Creates the launcher dialog; it deletes itself when closed.
void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherGameContext game, LauncherCallbacks callbacks);

}  // namespace gw2
