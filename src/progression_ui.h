#pragma once

// Drawing pilot progression (progression.h) in the neon style: tier colours and badges, the level badge and its XP
// bar. Shared by the results card, the Single Player tiles, the leaderboards and the Pilot screen.

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <imgui.h>

#include "line_font.h"
#include "neon_ui.h"
#include "progression_rules.h"

namespace gw2::progression_ui {

using progression::Tier;
using namespace gw2::neon;

// Bronze, Silver and Gold as metals; Neon pulses pink and cyan; Master runs through the rainbow.
inline ImU32 TierColour(Tier tier, float alpha = 1.0f) {
  const float t = float(Seconds());
  switch (tier) {
    case Tier::kBronze: return Rgba(0.95f, 0.55f, 0.25f, alpha);
    case Tier::kSilver: return Rgba(0.78f, 0.86f, 1.0f, alpha);
    case Tier::kGold: return Rgba(1.0f, 0.83f, 0.25f, alpha);
    case Tier::kNeon: {
      const float k = 0.5f + 0.5f * std::sin(t * 3.0f);
      return Rgba(1.0f - 0.7f * k, 0.3f + 0.6f * k, 1.0f, alpha);
    }
    case Tier::kMaster: {
      const float h = std::fmod(t * 0.25f, 1.0f);
      float r, g, b;
      ImGui::ColorConvertHSVtoRGB(h, 0.75f, 1.0f, r, g, b);
      return Rgba(r, g, b, alpha);
    }
    case Tier::kNone: break;
  }
  return Rgba(0.6f, 0.65f, 0.75f, alpha * 0.6f);
}

// A hexagon in the tier's colour with a pip for each step (Bronze one .. Gold three), a star for Neon and a star in a
// double ring for Master. Unranked: a faint empty hexagon.
inline void DrawTierBadge(ImDrawList* draw, ImVec2 c, float r, Tier tier, float alpha = 1.0f) {
  const ImU32 colour = TierColour(tier, alpha);
  const float thick = std::max(1.2f, r * 0.12f);
  Neon(draw, Ring(c, r, 6, 0.5f), true, colour, thick);
  const int steps = int(tier);
  if (steps >= 1 && steps <= 3) {
    for (int i = 0; i < steps; ++i) {
      const float x = c.x + (float(i) - float(steps - 1) * 0.5f) * r * 0.55f;
      draw->AddCircleFilled(ImVec2(x, c.y), r * 0.16f, colour);
    }
  } else if (steps >= 4) {
    DrawStar(draw, c, r * 0.55f, colour);
    if (steps >= 5) Neon(draw, Ring(c, r * 1.3f, 6, 0.5f), true, colour, thick * 0.7f);
  }
}

// "LV 17" with the XP bar beneath it, `fill` from 0 to 1. Returns the height drawn.
inline float DrawLevel(ImDrawList* draw, ImVec2 at, float size, float width, uint32_t level, float fill, ImU32 colour,
                       float alpha = 1.0f) {
  const std::string text = std::format("LV {}", level);
  line_font::GameText(draw, at, size, text, WithAlpha(colour, alpha), std::max(1.2f, size * 0.08f));
  const float bar_y = at.y + size * 1.35f;
  const float bar_h = std::max(3.0f, size * 0.22f);
  draw->AddRectFilled(ImVec2(at.x, bar_y), ImVec2(at.x + width, bar_y + bar_h), Rgba(1, 1, 1, 0.12f * alpha),
                      bar_h * 0.5f);
  const float f = std::clamp(fill, 0.0f, 1.0f);
  if (f > 0) {
    draw->AddRectFilled(ImVec2(at.x, bar_y), ImVec2(at.x + width * f, bar_y + bar_h), WithAlpha(colour, alpha),
                        bar_h * 0.5f);
    draw->AddRectFilled(ImVec2(at.x, bar_y - bar_h), ImVec2(at.x + width * f, bar_y + bar_h * 2.0f),
                        WithAlpha(colour, 0.15f * alpha), bar_h);
  }
  return size * 1.35f + bar_h;
}

}  // namespace gw2::progression_ui
