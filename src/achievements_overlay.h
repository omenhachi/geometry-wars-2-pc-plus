#pragma once

#include <functional>

#include <rex/system/achievement_manager.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

namespace rex {
class Runtime;
}  // namespace rex

namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace gw2 {

// ReXGlue's achievements list, sized and centred for the viewport, with a B Back footer. The owner closes it.
class AchievementsOverlay final : public rex::ui::ImGuiDialog {
 public:
  struct Controls {
    std::function<void()> request_close;  // Escape; called on the UI thread during a frame
    std::function<float()> scroll_axis;   // -1 up .. 1 down, read every frame
  };

  AchievementsOverlay(rex::ui::ImGuiDrawer* imgui_drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                      rex::Runtime* runtime, rex::system::AchievementManager* achievements, Controls controls);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  rex::system::AchievementManager* achievements_;
  rex::ui::AchievementIconCache icon_cache_;
  Controls controls_;
};

}  // namespace gw2
