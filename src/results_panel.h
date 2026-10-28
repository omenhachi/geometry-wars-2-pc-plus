#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2 {

// The results screen after a single-player run (results_panel.cpp), shown while gw2_results_screen is on.
void CreateResultsPanel(rex::ui::ImGuiDrawer* drawer);

// While results are up, LB (1 on the keyboard) or F5 shows or hides them, and the choice is kept with `save`.
void SetResultsScreenSaver(std::function<void(std::string_view key, std::string value)> save);
// The game read pad `user` into `state` (XInputGetState): a new LB press while results are up asks for the toggle.
void FilterResultsPad(const uint8_t* base, uint32_t user, uint32_t state);

}  // namespace gw2
