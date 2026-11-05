#pragma once

// The button hints along the bottom of PC Plus's lists, drawn as key caps: each key in its own glowing box, its action
// beside it, wrapping onto another line when the panel is narrow. And word wrapping for the line font.

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "line_font.h"
#include "neon_ui.h"

namespace gw2::key_hints {

// A key and what it does. The key "<>" is drawn as a pair of arrows (Left and Right).
struct Hint {
  std::string key;
  std::string action;
};

// Lines of `text` no wider than `width` at `size`, broken between words; at most `most` lines, the last one ending in
// "..." when the text didn't fit.
inline std::vector<std::string> Wrap(std::string_view text, float size, float width, size_t most = 3) {
  std::vector<std::string> lines;
  std::string line;
  size_t at = 0;
  while (at < text.size()) {
    const size_t space = text.find(' ', at);
    const std::string word(text.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at));
    at = space == std::string_view::npos ? text.size() : space + 1;
    if (word.empty()) continue;
    const std::string tried = line.empty() ? word : line + " " + word;
    if (line.empty() || line_font::GameWidth(tried, size) <= width) {
      line = tried;
    } else {
      lines.push_back(line);
      line = word;
    }
  }
  if (!line.empty()) lines.push_back(line);
  if (lines.size() > most) {
    lines.resize(most);
    std::string& last = lines.back();
    while (!last.empty() && line_font::GameWidth(last + "...", size) > width) last.pop_back();
    last += "...";
  }
  return lines;
}

// Draws `hints` from (left, bottom) upwards: the last line sits on `bottom`. Returns the height used; with no `draw`,
// only measures it.
inline float Draw(ImDrawList* draw, const std::vector<Hint>& hints, float left, float right, float bottom, float size) {
  using namespace gw2::neon;
  const float cap_pad = size * 0.85f, gap = size * 0.7f, spacing = size * 2.0f;
  const float cap_h = size * 1.75f, line_h = cap_h + size * 0.7f;
  const auto cap_width = [&](const Hint& h) {
    return h.key == "<>" ? size * 2.6f : line_font::GameWidth(h.key, size) + cap_pad * 2.0f;
  };
  const auto hint_width = [&](const Hint& h) { return cap_width(h) + gap + line_font::GameWidth(h.action, size); };

  // Lines first, so the block can be placed from the bottom up.
  std::vector<std::vector<const Hint*>> lines(1);
  float x = left;
  for (const Hint& h : hints) {
    const float w = hint_width(h);
    if (x > left && x + w > right) {
      lines.emplace_back();
      x = left;
    }
    lines.back().push_back(&h);
    x += w + spacing;
  }
  const float height = float(lines.size()) * line_h - (line_h - cap_h);
  if (!draw) return height;
  float y = bottom - height;
  const float pulse = 0.75f + 0.25f * float(std::sin(Seconds() * 3.0));
  for (const auto& line : lines) {
    x = left;
    for (const Hint* h : line) {
      const float cw = cap_width(*h);
      const ImVec2 a(x, y), b(x + cw, y + cap_h);
      draw->AddRectFilled(a, b, Theme(0.0, 0.16f), size * 0.35f);
      draw->AddRect(a, b, Theme(0.0, 0.35f * pulse), size * 0.35f, 0, size * 0.32f);  // the glow
      draw->AddRect(a, b, Theme(0.0, 0.95f), size * 0.35f, 0, std::max(1.0f, size * 0.09f));
      const float ty = y + (cap_h - size) * 0.5f;
      if (h->key == "<>") {
        const float cy = y + cap_h * 0.5f, r = size * 0.38f, cx = x + cw * 0.5f;
        draw->AddTriangleFilled(ImVec2(cx - size * 0.95f, cy), ImVec2(cx - size * 0.25f, cy - r), ImVec2(cx - size * 0.25f, cy + r),
                                Rgba(1, 1, 1, 0.95f));
        draw->AddTriangleFilled(ImVec2(cx + size * 0.95f, cy), ImVec2(cx + size * 0.25f, cy - r), ImVec2(cx + size * 0.25f, cy + r),
                                Rgba(1, 1, 1, 0.95f));
      } else {
        line_font::GameText(draw, ImVec2(x + cap_pad, ty), size, h->key, Rgba(1, 1, 1, 0.97f), std::max(1.0f, size * 0.08f));
      }
      line_font::GameText(draw, ImVec2(x + cw + gap, ty), size, h->action, Rgba(0.78f, 0.82f, 0.92f, 0.9f),
                          std::max(1.0f, size * 0.07f));
      x += hint_width(*h) + spacing;
    }
    y += line_h;
  }
  return height;
}

}  // namespace gw2::key_hints
