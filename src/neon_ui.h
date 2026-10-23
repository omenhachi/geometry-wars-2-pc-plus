#pragma once

// Drawing in the game's neon style for the ImGui overlays (results_panel.cpp, replays_menu.cpp): glowing lines, the
// interface colour (hud_colour.h), numbers and times as the game writes them.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "hud_colour.h"

namespace gw2::neon {

using Clock = std::chrono::steady_clock;

inline const Clock::time_point g_neon_start = Clock::now();

inline double Seconds() {
  return std::chrono::duration<double>(Clock::now() - g_neon_start).count();
}

inline ImU32 Rgba(float r, float g, float b, float a = 1.0f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
                                               std::clamp(b, 0.0f, 1.0f), std::clamp(a, 0.0f, 1.0f)));
}

inline ImU32 WithAlpha(ImU32 colour, float alpha) {
  ImVec4 c = ImGui::ColorConvertU32ToFloat4(colour);
  c.w *= std::clamp(alpha, 0.0f, 1.0f);
  return ImGui::ColorConvertFloat4ToU32(c);
}

// The interface colour (hud_colour.h); Rainbow drifts along `offset`.
inline ImU32 Theme(double offset = 0.0, float alpha = 1.0f) {
  const hud_colour::Preset preset = hud_colour::CurrentPreset();
  const hud_colour::Rgb rgb = hud_colour::Swatch(preset, hud_colour::CurrentCustom(), Seconds() + offset);
  return Rgba(rgb.r, rgb.g, rgb.b, alpha);
}

inline std::string Grouped(int64_t value) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), ",");
  return value < 0 ? "-" + digits : digits;
}

inline std::string Clock2(float seconds) {
  const int s = std::max(0, int(seconds + 0.5f));
  char text[16];
  std::snprintf(text, sizeof(text), "%d:%02d", s / 60, s % 60);
  return text;
}

inline const char* ModeName(uint32_t mode) {
  static const char* const kNames[] = {"Deadline", "Evolved", "Sequence", "Waves", "Pacifism", "King"};
  return mode < std::size(kNames) ? kNames[mode] : "Results";
}

// A neon line: a wide faint glow, a glow, the line and a pale core.
inline void Neon(ImDrawList* draw, const std::vector<ImVec2>& points, bool closed, ImU32 colour, float thickness) {
  if (points.size() < 2) return;
  const ImVec4 c = ImGui::ColorConvertU32ToFloat4(colour);
  const ImU32 core = Rgba(c.x + (1 - c.x) * 0.4f, c.y + (1 - c.y) * 0.4f, c.z + (1 - c.z) * 0.4f, c.w);
  const struct {
    ImU32 colour;
    float width;
  } passes[] = {{WithAlpha(colour, 0.14f), thickness * 4.0f},
                {WithAlpha(colour, 0.32f), thickness * 2.2f},
                {colour, thickness},
                {core, thickness * 0.45f}};
  for (const auto& pass : passes) {
    draw->AddPolyline(points.data(), int(points.size()), pass.colour, closed ? ImDrawFlags_Closed : 0, pass.width);
  }
}

inline std::vector<ImVec2> Ring(ImVec2 c, float r, int sides, float turn = 0.0f) {
  std::vector<ImVec2> points;
  for (int i = 0; i < sides; ++i) {
    const float a = turn + 6.2831853f * float(i) / float(sides);
    points.push_back(ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a)));
  }
  return points;
}

inline void DrawStar(ImDrawList* draw, ImVec2 c, float r, ImU32 colour) {
  std::vector<ImVec2> points;
  for (int i = 0; i < 10; ++i) {
    const float a = -1.5707963f + 3.1415927f * float(i) / 5.0f;
    const float rr = i % 2 == 0 ? r : r * 0.45f;
    points.push_back(ImVec2(c.x + rr * std::cos(a), c.y + rr * std::sin(a)));
  }
  Neon(draw, points, true, colour, 1.8f);
}

}  // namespace gw2::neon
