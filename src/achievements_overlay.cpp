#include "achievements_overlay.h"

#include <algorithm>
#include <string>
#include <utility>

#include <imgui.h>

#include <rex/runtime.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/style.h>

#include "achievements_overlay_rules.h"

namespace gw2 {
namespace {

constexpr float kMinWidth = 420.0f;
constexpr float kMaxWidth = 560.0f;
constexpr float kMinHeight = 320.0f;
constexpr float kMaxHeight = 460.0f;
constexpr float kScrollPixelsPerSecond = 480.0f;
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

void DrawHeader(const rex::ui::AchievementsStyle& style, int unlocked_count, int total_count, int earned_gs,
                int total_gs) {
  ImGui::PushStyleColor(ImGuiCol_Text, style.header_text);
  ImGui::Text("%d / %d unlocked", unlocked_count, total_count);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextColored(style.badge_gamerscore, "%dG / %dG", earned_gs, total_gs);

  const float fraction = total_count > 0 ? static_cast<float>(unlocked_count) / static_cast<float>(total_count) : 0.0f;
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, style.progress_bar);
  ImGui::ProgressBar(fraction, ImVec2(-1.0f, 6.0f), "");
  ImGui::PopStyleColor();

  ImGui::TextDisabled("Unlock progress is saved automatically");
  ImGui::Separator();
  ImGui::Spacing();
}

// Splits the draw list so the row band (channel 0) renders behind the text (channel 1).
void DrawRow(const rex::ui::AchievementsStyle& style, rex::ui::ImmediateTexture* icon,
             const rex::system::AchievementInfo& a, bool unlocked, ImDrawList* draw_list) {
  const std::string& description = unlocked ? a.description : a.unachieved_description;
  const float pad = 4.0f;

  ImGui::PushID(static_cast<int>(a.id));
  draw_list->ChannelsSplit(2);
  draw_list->ChannelsSetCurrent(1);

  const float row_start_y = ImGui::GetCursorScreenPos().y;
  ImGui::Dummy(ImVec2(0.0f, pad * 0.5f));

  if (icon) {
    const ImVec4 tint = unlocked ? style.unlocked_icon_tint : style.locked_icon_tint;
    ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(icon), ImVec2(style.icon_size, style.icon_size), ImVec2(0, 0),
                       ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
  } else {
    ImGui::Dummy(ImVec2(style.icon_size, style.icon_size));
  }
  ImGui::SameLine();

  ImGui::BeginGroup();
  const char* marker = unlocked ? "[*]" : "[ ]";
  const ImVec4& title_color = unlocked ? style.unlocked_title : style.locked_title;
  ImGui::TextColored(title_color, "%s", marker);
  ImGui::SameLine();
  ImGui::TextColored(style.badge_gamerscore, "%dG", static_cast<int>(a.gamerscore));
  ImGui::SameLine();
  ImGui::TextColored(title_color, "%s", a.label.c_str());
  ImGui::PushStyleColor(ImGuiCol_Text, unlocked ? style.unlocked_desc : style.locked_desc);
  ImGui::TextWrapped("%s", description.c_str());
  ImGui::PopStyleColor();
  ImGui::EndGroup();

  ImGui::Dummy(ImVec2(0.0f, pad * 0.5f));
  const float row_end_y = ImGui::GetCursorScreenPos().y;

  if (unlocked) {
    draw_list->ChannelsSetCurrent(0);
    const float x0 = ImGui::GetWindowPos().x + 2.0f;
    const float x1 = x0 + ImGui::GetWindowSize().x - 4.0f;
    draw_list->AddRectFilled(ImVec2(x0, row_start_y), ImVec2(x1, row_end_y),
                             ImGui::GetColorU32(style.row_unlocked_bg), style.row_rounding);
  }
  draw_list->ChannelsMerge();

  ImGui::Separator();
  ImGui::PopID();
}

}  // namespace

AchievementsOverlay::AchievementsOverlay(rex::ui::ImGuiDrawer* imgui_drawer,
                                         rex::ui::ImmediateDrawer* immediate_drawer, rex::Runtime* runtime,
                                         rex::system::AchievementManager* achievements, Controls controls)
    : ImGuiDialog(imgui_drawer),
      achievements_(achievements),
      icon_cache_(immediate_drawer, runtime),
      controls_(std::move(controls)) {}

void AchievementsOverlay::OnDraw(ImGuiIO& io) {
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) controls_.request_close();

  const auto& style = imgui_drawer()->style().achievements;
  const ImVec2 viewport = io.DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(viewport.x * 0.5f, viewport.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(FitWindowExtent(viewport.x, kMinWidth, kMaxWidth),
                                  FitWindowExtent(viewport.y, kMinHeight, kMaxHeight)),
                           ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.92f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.window_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.item_spacing);
  if (ImGui::Begin("Achievements##overlay", nullptr, kWindowFlags)) {
    const auto achievements = achievements_->ListAchievements();
    int unlocked_count = 0;
    int earned_gs = 0;
    int total_gs = 0;
    for (const auto& a : achievements) {
      total_gs += static_cast<int>(a.gamerscore);
      if (achievements_->IsUnlocked(a.id)) {
        ++unlocked_count;
        earned_gs += static_cast<int>(a.gamerscore);
      }
    }
    DrawHeader(style, unlocked_count, static_cast<int>(achievements.size()), earned_gs, total_gs);

    ImGui::BeginChild("##achlist", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
    if (const float axis = controls_.scroll_axis(); axis != 0.0f) {
      const float target = ImGui::GetScrollY() + axis * kScrollPixelsPerSecond * io.DeltaTime;
      ImGui::SetScrollY(std::clamp(target, 0.0f, ImGui::GetScrollMaxY()));
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (const auto& a : achievements) {
      DrawRow(style, icon_cache_.GetIcon(a), a, achievements_->IsUnlocked(a.id), draw_list);
    }
    ImGui::EndChild();

    ImGui::TextColored(style.header_text, "B  Back");
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

}  // namespace gw2
