#pragma once

// A stroke font in the style of the game's vector lettering: square, angular capitals made of straight lines, drawn as
// neon (a wide faint glow under a crisp line). Diagonals stay short (M, W and V are bevelled), since long sharp points
// spike the glow and clash with the boxy letters. Each glyph is polylines on a grid 4 wide and 6 tall, y down.

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

namespace gw2::line_font {

struct Point {
  float x;
  float y;
};
using Stroke = std::vector<Point>;

struct Glyph {
  float width;  // in grid units
  std::vector<Stroke> strokes;
};

inline const Glyph& GlyphFor(char c) {
  static const Glyph kSpace{2.5f, {}};
  static const Glyph kUnknown{4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}, {0, 0}}}};
  static const std::vector<std::pair<char, Glyph>> kGlyphs = {
      {'A', {4, {{{0, 6}, {0, 2}, {2, 0}, {4, 2}, {4, 6}}, {{0, 3.6f}, {4, 3.6f}}}}},
      {'B', {4, {{{0, 0}, {3, 0}, {4, 1}, {4, 2}, {3, 3}, {4, 4}, {4, 5}, {3, 6}, {0, 6}, {0, 0}}, {{0, 3}, {3, 3}}}}},
      {'C', {4, {{{4, 0}, {0, 0}, {0, 6}, {4, 6}}}}},
      {'D', {4, {{{0, 0}, {3, 0}, {4, 1}, {4, 5}, {3, 6}, {0, 6}, {0, 0}}}}},
      {'E', {4, {{{4, 0}, {0, 0}, {0, 6}, {4, 6}}, {{0, 3}, {3, 3}}}}},
      {'F', {4, {{{4, 0}, {0, 0}, {0, 6}}, {{0, 3}, {3, 3}}}}},
      {'G', {4, {{{4, 0}, {0, 0}, {0, 6}, {4, 6}, {4, 3}, {2, 3}}}}},
      {'H', {4, {{{0, 0}, {0, 6}}, {{4, 0}, {4, 6}}, {{0, 3}, {4, 3}}}}},
      {'I', {2, {{{1, 0}, {1, 6}}}}},
      {'J', {4, {{{4, 0}, {4, 6}, {0, 6}, {0, 4}}}}},
      {'K', {4, {{{0, 0}, {0, 6}}, {{4, 0}, {0, 3.2f}, {4, 6}}}}},
      {'L', {4, {{{0, 0}, {0, 6}, {4, 6}}}}},
      {'M', {4.6f, {{{0, 6}, {0, 0}, {1, 0}, {2.3f, 1.8f}, {3.6f, 0}, {4.6f, 0}, {4.6f, 6}}}}},
      {'N', {4, {{{0, 6}, {0, 0}, {4, 6}, {4, 0}}}}},
      {'O', {4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}, {0, 0}}}}},
      {'P', {4, {{{0, 6}, {0, 0}, {4, 0}, {4, 3}, {0, 3}}}}},
      {'Q', {4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}, {0, 0}}, {{2.4f, 4.2f}, {4.6f, 6.6f}}}}},
      {'R', {4, {{{0, 6}, {0, 0}, {4, 0}, {4, 3}, {0, 3}}, {{1.6f, 3}, {4, 6}}}}},
      {'S', {4, {{{4, 0}, {0, 0}, {0, 3}, {4, 3}, {4, 6}, {0, 6}}}}},
      {'T', {4, {{{0, 0}, {4, 0}}, {{2, 0}, {2, 6}}}}},
      {'U', {4, {{{0, 0}, {0, 6}, {4, 6}, {4, 0}}}}},
      {'V', {4, {{{0, 0}, {0, 3.5f}, {2, 6}, {4, 3.5f}, {4, 0}}}}},
      {'W', {4.8f, {{{0, 0}, {0, 6}, {1, 6}, {2.4f, 4.2f}, {3.8f, 6}, {4.8f, 6}, {4.8f, 0}}}}},
      {'X', {4, {{{0, 0}, {4, 6}}, {{4, 0}, {0, 6}}}}},
      {'Y', {4, {{{0, 0}, {2, 3}, {4, 0}}, {{2, 3}, {2, 6}}}}},
      {'Z', {4, {{{0, 0}, {4, 0}, {0, 6}, {4, 6}}}}},
      {'0', {4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}, {0, 0}}, {{4, 0}, {0, 6}}}}},
      {'1', {2, {{{0, 1}, {1, 0}, {1, 6}}}}},
      {'2', {4, {{{0, 0}, {4, 0}, {4, 3}, {0, 3}, {0, 6}, {4, 6}}}}},
      {'3', {4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}}, {{1, 3}, {4, 3}}}}},
      {'4', {4, {{{0, 0}, {0, 3}, {4, 3}}, {{4, 0}, {4, 6}}}}},
      {'5', {4, {{{4, 0}, {0, 0}, {0, 3}, {4, 3}, {4, 6}, {0, 6}}}}},
      {'6', {4, {{{4, 0}, {0, 0}, {0, 6}, {4, 6}, {4, 3}, {0, 3}}}}},
      {'7', {4, {{{0, 0}, {4, 0}, {1.5f, 6}}}}},
      {'8', {4, {{{0, 0}, {4, 0}, {4, 6}, {0, 6}, {0, 0}}, {{0, 3}, {4, 3}}}}},
      {'9', {4, {{{4, 3}, {0, 3}, {0, 0}, {4, 0}, {4, 6}, {0, 6}}}}},
      {':', {1, {{{0.5f, 1.6f}, {0.5f, 2.4f}}, {{0.5f, 4.2f}, {0.5f, 5.0f}}}}},
      {'.', {1, {{{0.5f, 5.2f}, {0.5f, 6}}}}},
      {'-', {3, {{{0, 3}, {3, 3}}}}},
      {',', {1, {{{0.7f, 5.0f}, {0.2f, 6.8f}}}}},
      {'+', {3, {{{0, 3}, {3, 3}}, {{1.5f, 1.5f}, {1.5f, 4.5f}}}}},
      {'\'', {1, {{{0.5f, 0}, {0.5f, 1.5f}}}}},
  };
  if (c == ' ') return kSpace;
  const char upper = char(std::toupper(static_cast<unsigned char>(c)));
  for (const auto& [ch, glyph] : kGlyphs) {
    if (ch == upper) return glyph;
  }
  return kUnknown;
}

// Grid units between letters.
inline constexpr float kSpacing = 1.6f;

// The width of text drawn `size` pixels tall.
inline float Width(std::string_view text, float size) {
  const float unit = size / 6.0f;
  float w = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    w += GlyphFor(text[i]).width * unit + (i + 1 < text.size() ? kSpacing * unit : 0.0f);
  }
  return w;
}

// Draws text with its top-left at pos, `size` pixels tall, as neon lines. Returns the width.
inline float Draw(ImDrawList* draw, ImVec2 pos, float size, std::string_view text, ImU32 color, float thickness) {
  const ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  const ImU32 glow_wide = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * 0.14f));
  const ImU32 glow = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * 0.32f));
  const ImU32 core = ImGui::ColorConvertFloat4ToU32(
      ImVec4(c.x + (1 - c.x) * 0.35f, c.y + (1 - c.y) * 0.35f, c.z + (1 - c.z) * 0.35f, c.w));
  const float unit = size / 6.0f;
  const struct {
    ImU32 color;
    float width;
  } passes[] = {{glow_wide, thickness * 4.0f}, {glow, thickness * 2.2f}, {color, thickness}, {core, thickness * 0.45f}};
  for (const auto& pass : passes) {
    float x = pos.x;
    for (const char ch : text) {
      const Glyph& glyph = GlyphFor(ch);
      for (const Stroke& stroke : glyph.strokes) {
        std::vector<ImVec2> points;
        points.reserve(stroke.size());
        for (const Point& p : stroke) points.push_back(ImVec2(x + p.x * unit, pos.y + p.y * unit));
        const bool closed = stroke.size() > 2 && stroke.front().x == stroke.back().x &&
                            stroke.front().y == stroke.back().y;
        draw->AddPolyline(points.data(), int(points.size()) - (closed ? 1 : 0), pass.color,
                          closed ? ImDrawFlags_Closed : ImDrawFlags_None, pass.width);
      }
      x += (glyph.width + kSpacing) * unit;
    }
  }
  return Width(text, size);
}

}  // namespace gw2::line_font
