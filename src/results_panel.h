#pragma once

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2 {

// The results screen after a single-player run (results_panel.cpp), shown while gw2_results_screen is on.
void CreateResultsPanel(rex::ui::ImGuiDrawer* drawer);

}  // namespace gw2
