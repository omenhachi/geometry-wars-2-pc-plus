#pragma once

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2 {

// The results' pilot card (progression_overlay.cpp): XP, level-ups, tiers, bounties and unlocks from the last run.
void CreateProgressionOverlay(rex::ui::ImGuiDrawer* drawer);

}  // namespace gw2
