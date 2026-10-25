#pragma once

// A stroke font in the style of the game's vector lettering: square, angular capitals made of straight lines, drawn as
// neon (a wide faint glow under a crisp line). Diagonals stay short (M, W and V are bevelled), since long sharp points
// spike the glow and clash with the boxy letters. Each glyph is polylines on a grid 4 wide and 6 tall, y down.

#include <algorithm>
#include <cmath>
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
      {'1', {3, {{{0, 1}, {1.5f, 0}, {1.5f, 6}}, {{0, 6}, {3, 6}}}}},  // with a foot, so it holds its own space
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

// How text is laid out. The default is the launcher's title lettering: every letter a capital, tight. GameText is the
// lettering of the game's own menus and HUD: small capitals (a letter written lower case is a capital ~4/5 as tall,
// on the same baseline), digits all one width so numbers line up in columns, roomier, and a bolder line.
// The game's letters are also wide (an M as wide as it is tall), softly rounded where a letter curves, and drawn with
// an even line and a gentle core.
struct Style {
  bool small_caps = false;
  bool even_digits = false;
  float spacing = kSpacing;  // grid units between letters
  float space = 2.5f;        // a space's width, in grid units
  float weight = 1.0f;       // times the thickness asked for
  float wide = 1.0f;         // letters' width, times their grid width
  float round = 0.0f;        // the corners of round letters (O, C, S...), as a radius in grid units
  float core = 0.35f;        // how far the line's core is lightened towards white
};
inline constexpr Style kGameText{true, true, 1.7f, 3.6f, 1.05f, 1.4f, 1.1f, 0.18f};
inline constexpr float kSmallCap = 0.78f;
inline constexpr float kDigitWidth = 4.0f;

namespace detail {

// The letters whose corners are rounded in the game's lettering; B, D, P and R keep the corners of their upright. 5
// stays square, or it would read as S.
inline bool Rounded(char c) {
  const char upper = char(std::toupper(static_cast<unsigned char>(c)));
  return std::string_view("BCDGJOPQRSU023689").find(upper) != std::string_view::npos;
}

inline bool KeepsUpright(char c) {
  const char upper = char(std::toupper(static_cast<unsigned char>(c)));
  return upper == 'B' || upper == 'D' || upper == 'P' || upper == 'R';
}

// A stroke with its square corners rounded to radius `r` (in grid units), each corner a short curve.
inline Stroke RoundCorners(const Stroke& stroke, float r, bool keep_upright) {
  const bool closed = stroke.size() > 2 && stroke.front().x == stroke.back().x && stroke.front().y == stroke.back().y;
  const size_t n = closed ? stroke.size() - 1 : stroke.size();
  if (n < 3 || r <= 0) return stroke;
  Stroke out;
  for (size_t i = 0; i < n; ++i) {
    const Point p = stroke[i];
    const bool end = !closed && (i == 0 || i + 1 == n);
    const Point a = stroke[(i + n - 1) % n], b = stroke[(i + 1) % n];
    const float ax = a.x - p.x, ay = a.y - p.y, bx = b.x - p.x, by = b.y - p.y;
    const float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
    // Only a square corner between straight sides is rounded, and never on an upright that is kept.
    const bool square = la > 0 && lb > 0 && std::abs(ax * bx + ay * by) < 0.01f * la * lb &&
                        (ax == 0 || ay == 0) && (bx == 0 || by == 0);
    if (end || !square || (keep_upright && p.x == 0)) {
      out.push_back(p);
      continue;
    }
    const float k = std::min({r, la * 0.5f, lb * 0.5f});
    const Point from{p.x + ax / la * k, p.y + ay / la * k}, to{p.x + bx / lb * k, p.y + by / lb * k};
    constexpr int kSteps = 2;  // three points: enough at text sizes, and every point costs (see Draw)
    for (int step = 0; step <= kSteps; ++step) {  // a quadratic curve through the corner
      const float t = float(step) / kSteps, u = 1 - t;
      out.push_back({u * u * from.x + 2 * u * t * p.x + t * t * to.x, u * u * from.y + 2 * u * t * p.y + t * t * to.y});
    }
  }
  if (closed) out.push_back(out.front());
  return out;
}

// The glyph's strokes in a style: rounded where it rounds.
inline const std::vector<Stroke>& StrokesFor(const Style& style, char c) {
  const Glyph& glyph = GlyphFor(c);
  if (style.round <= 0 || !Rounded(c)) return glyph.strokes;
  static std::vector<std::pair<const Glyph*, std::vector<Stroke>>> cache;  // one radius is used: the game style's
  for (const auto& [g, strokes] : cache) {
    if (g == &glyph) return strokes;
  }
  std::vector<Stroke> rounded;
  for (const Stroke& stroke : glyph.strokes) rounded.push_back(RoundCorners(stroke, style.round, KeepsUpright(c)));
  cache.emplace_back(&glyph, std::move(rounded));
  return cache.back().second;
}

inline bool Small(const Style& style, char c) {
  return style.small_caps && c >= 'a' && c <= 'z';
}

// The glyph's width and its advance, in grid units at full size.
inline float Advance(const Style& style, char c) {
  if (c == ' ') return style.space;
  if (style.even_digits && c >= '0' && c <= '9') return kDigitWidth * style.wide;
  return GlyphFor(c).width * style.wide * (Small(style, c) ? kSmallCap : 1.0f);
}

}  // namespace detail

// The width of text drawn `size` pixels tall.
inline float Width(std::string_view text, float size, const Style& style = {}) {
  const float unit = size / 6.0f;
  float w = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    w += detail::Advance(style, text[i]) * unit + (i + 1 < text.size() ? style.spacing * unit : 0.0f);
  }
  return w;
}

// Draws text with its top-left at pos, `size` pixels tall (its capitals' height), as neon lines. Returns the width.
inline float Draw(ImDrawList* draw, ImVec2 pos, float size, std::string_view text, ImU32 color, float thickness,
                  const Style& style = {}) {
  const ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  const ImU32 glow_wide = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * 0.14f));
  const ImU32 glow = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * 0.32f));
  const ImU32 core = ImGui::ColorConvertFloat4ToU32(
      ImVec4(c.x + (1 - c.x) * style.core, c.y + (1 - c.y) * style.core, c.z + (1 - c.z) * style.core, c.w));
  const float unit = size / 6.0f;
  thickness *= style.weight;
  const struct {
    ImU32 color;
    float width;
  } passes[] = {{glow_wide, thickness * 4.0f}, {glow, thickness * 2.2f}, {color, thickness}, {core, thickness * 0.45f}};
  // An overlay's draw list holds at most 65536 vertices (16-bit indices), and a busy screen's text comes near it: as it
  // fills, text leaves out the widest glow, then the glow, whatever the resolution.
  const int used = draw->VtxBuffer.Size;
  for (const auto& pass : passes) {
    if ((used > 24000 && &pass == &passes[0]) || (used > 48000 && &pass == &passes[1])) continue;
    float x = pos.x;
    for (const char ch : text) {
      const Glyph& glyph = GlyphFor(ch);
      const float scale = detail::Small(style, ch) ? kSmallCap : 1.0f;
      const float advance = detail::Advance(style, ch);
      // A digit narrower than the rest sits in the middle of its column; a small capital on the baseline.
      const float left = x + (advance - glyph.width * style.wide * scale) * 0.5f * unit;
      const float top = pos.y + (1.0f - scale) * size;
      for (const Stroke& stroke : detail::StrokesFor(style, ch)) {
        std::vector<ImVec2> points;
        points.reserve(stroke.size());
        for (const Point& p : stroke) {
          points.push_back(ImVec2(left + p.x * unit * scale * style.wide, top + p.y * unit * scale));
        }
        const bool closed = stroke.size() > 2 && stroke.front().x == stroke.back().x &&
                            stroke.front().y == stroke.back().y;
        draw->AddPolyline(points.data(), int(points.size()) - (closed ? 1 : 0), pass.color,
                          closed ? ImDrawFlags_Closed : ImDrawFlags_None, pass.width * (scale < 1.0f ? 0.9f : 1.0f));
      }
      x += (advance + style.spacing) * unit;
    }
  }
  return Width(text, size, style);
}

// The game's lettering (kGameText), for PC Plus's screens over the game.
inline float GameWidth(std::string_view text, float size) {
  return Width(text, size, kGameText);
}
inline float GameText(ImDrawList* draw, ImVec2 pos, float size, std::string_view text, ImU32 color, float thickness) {
  return Draw(draw, pos, size, text, color, thickness, kGameText);
}

}  // namespace gw2::line_font
