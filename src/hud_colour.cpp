#include "hud_colour.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace gw2::hud_colour {

namespace {

struct PresetInfo {
  Preset preset;
  std::string_view name;
  std::string_view label;
  Look look;
};

// Hue, saturation scale, value scale. Blue and purple sit lighter than their pure hues, and with a touch of white,
// so they glow as brightly as the original green does.
constexpr std::array<PresetInfo, 7> kPresets = {{
    {Preset::kGreen, "green", "Green", {}},
    {Preset::kBlue, "blue", "Blue", {200, 0.85f, 1.15f}},
    {Preset::kPurple, "purple", "Purple", {270, 0.9f, 1.25f}},
    {Preset::kPink, "pink", "Pink", {325, 0.7f, 1.15f}},
    {Preset::kGold, "gold", "Gold", {34, 1.0f, 1.0f}},
    {Preset::kRainbow, "rainbow", "Rainbow", {0, 0.8f, 1.15f}},
    {Preset::kCustom, "custom", "Custom", {}},
}};

// The game's green sits near 97 degrees; this band takes its pale and dim shades too, and nothing yellow or cyan.
constexpr float kBandLow = 70;
constexpr float kBandHigh = 150;
// One trip round the spectrum, slow enough to read by.
constexpr double kRainbowSeconds = 12.0;

const PresetInfo& Info(Preset preset) {
  return kPresets[static_cast<size_t>(preset)];
}

Rgb FromHsv(float hue, float sat, float val) {
  hue = std::fmod(hue, 360.0f);
  if (hue < 0) hue += 360.0f;
  const float h6 = hue / 60.0f;
  const int sector = int(h6) % 6;
  const float f = h6 - std::floor(h6);
  const float p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
  switch (sector) {
    case 0: return {val, t, p};
    case 1: return {q, val, p};
    case 2: return {p, val, t};
    case 3: return {p, q, val};
    case 4: return {t, p, val};
    default: return {val, p, q};
  }
}

std::mutex g_mutex;
Preset g_preset = Preset::kGreen;
std::optional<Look> g_custom;

}  // namespace

std::string_view Name(Preset preset) {
  return Info(preset).name;
}

std::optional<Preset> Parse(std::string_view name) {
  for (const PresetInfo& info : kPresets) {
    if (info.name == name) return info.preset;
  }
  return std::nullopt;
}

std::string_view Label(Preset preset) {
  return Info(preset).label;
}

std::optional<Look> CustomLook(std::string_view hex) {
  if (hex.size() != 7 || hex[0] != '#') return std::nullopt;
  int channel[3];
  for (int i = 0; i < 3; ++i) {
    int value = 0;
    for (int j = 0; j < 2; ++j) {
      const char c = hex[1 + i * 2 + j];
      const int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
      if (digit < 0) return std::nullopt;
      value = value * 16 + digit;
    }
    channel[i] = value;
  }
  const float r = channel[0] / 255.0f, g = channel[1] / 255.0f, b = channel[2] / 255.0f;
  const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
  if (hi <= 0) return std::nullopt;
  const float d = hi - lo;
  float hue = 0;
  if (d > 0) {
    if (hi == r) hue = 60.0f * std::fmod((g - b) / d, 6.0f);
    else if (hi == g) hue = 60.0f * ((b - r) / d + 2.0f);
    else hue = 60.0f * ((r - g) / d + 4.0f);
    if (hue < 0) hue += 360.0f;
  }
  // Only the hue and saturation are taken: the brightness stays the game's, so a dark pick still glows.
  return Look{hue, d / hi, 1.0f};
}

std::string FormatHex(Rgb rgb) {
  const auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  char out[8];
  std::snprintf(out, sizeof out, "#%02X%02X%02X", byte(rgb.r), byte(rgb.g), byte(rgb.b));
  return out;
}

std::optional<Look> LookFor(Preset preset, const std::optional<Look>& custom, double seconds) {
  switch (preset) {
    case Preset::kGreen:
      return std::nullopt;
    case Preset::kCustom:
      return custom;
    case Preset::kRainbow: {
      Look look = Info(preset).look;
      look.hue = float(std::fmod(seconds / kRainbowSeconds, 1.0) * 360.0);
      return look;
    }
    default:
      return Info(preset).look;
  }
}

std::optional<Rgb> Recolour(Rgb colour, const Look& look) {
  const float hi = std::max({colour.r, colour.g, colour.b});
  const float lo = std::min({colour.r, colour.g, colour.b});
  const float d = hi - lo;
  if (hi <= 0 || d <= 0 || colour.g != hi) return std::nullopt;
  const float hue = 60.0f * ((colour.b - colour.r) / d + 2.0f);
  if (hue < kBandLow || hue > kBandHigh) return std::nullopt;
  const float sat = std::clamp(d / hi * look.saturation, 0.0f, 1.0f);
  return FromHsv(look.hue, sat, hi * look.value);
}

Rgb Swatch(Preset preset, const std::optional<Look>& custom, double seconds) {
  // The game's green at full strength, before its glow.
  constexpr Rgb kGameGreen{0.117f, 1.0f, 0.0f};
  Rgb out = kGameGreen;
  if (const auto look = LookFor(preset, custom, seconds)) {
    if (const auto recoloured = Recolour(kGameGreen, *look)) out = *recoloured;
  }
  out.r = std::clamp(out.r, 0.0f, 1.0f);
  out.g = std::clamp(out.g, 0.0f, 1.0f);
  out.b = std::clamp(out.b, 0.0f, 1.0f);
  return out;
}

std::vector<Preset> Cycle(bool has_custom) {
  std::vector<Preset> cycle = {Preset::kGreen, Preset::kBlue, Preset::kPurple, Preset::kPink, Preset::kGold,
                               Preset::kRainbow};
  if (has_custom) cycle.push_back(Preset::kCustom);
  return cycle;
}

void SetCurrent(Preset preset, std::optional<Look> custom) {
  std::lock_guard lock(g_mutex);
  g_preset = preset == Preset::kCustom && !custom ? Preset::kGreen : preset;
  g_custom = custom;
}

Preset CurrentPreset() {
  std::lock_guard lock(g_mutex);
  return g_preset;
}

std::optional<Look> CurrentCustom() {
  std::lock_guard lock(g_mutex);
  return g_custom;
}

std::optional<Look> CurrentLook(double seconds) {
  std::lock_guard lock(g_mutex);
  return LookFor(g_preset, g_custom, seconds);
}

}  // namespace gw2::hud_colour
