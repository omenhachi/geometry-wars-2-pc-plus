// Interface colour: the game draws its menus and HUD in lime green, and the player can pick another colour for them
// (Help and options in the game, or the launcher's Display page). Every text colour the game draws passes through
// Recolour: a colour in the green band takes the chosen hue, with its own brightness, fade and glow kept, so the
// game still animates it exactly as before; other colours (white, the enemies) are left alone. Green changes nothing.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gw2::hud_colour {

enum class Preset { kGreen, kBlue, kPurple, kPink, kGold, kRainbow, kCustom };

struct Rgb {
  float r = 0, g = 0, b = 0;
};

// What a recoloured green becomes: the hue in degrees, and scales for the colour's own saturation and value. The
// presets are tuned by eye, since green looks far brighter than blue or purple at the same value.
struct Look {
  float hue = 0;
  float saturation = 1;
  float value = 1;
};

// "green", "blue" ... as kept in the settings file; Parse is the reverse, nullopt for anything else.
std::string_view Name(Preset preset);
std::optional<Preset> Parse(std::string_view name);
// "Green", "Blue" ... as shown to the player.
std::string_view Label(Preset preset);

// A custom colour from "#RRGGBB": its hue and saturation, at the game's own brightness. Nullopt when malformed or
// without a hue to take (black).
std::optional<Look> CustomLook(std::string_view hex);
std::string FormatHex(Rgb rgb);

// The look to draw with: nullopt for Green, the custom look for Custom (Green when there is none), and for Rainbow a
// hue that drifts slowly through the spectrum with `seconds`.
std::optional<Look> LookFor(Preset preset, const std::optional<Look>& custom, double seconds);

// The game's text colour (premultiplied by its fade) in that look, or nullopt to draw it as it is.
std::optional<Rgb> Recolour(Rgb colour, const Look& look);

// What the game's full-strength menu green looks like in a preset, for a swatch (0..1 per channel).
Rgb Swatch(Preset preset, const std::optional<Look>& custom, double seconds);

// The order the in-game row steps through; Custom is a stop only when one is saved.
std::vector<Preset> Cycle(bool has_custom);

// The colour in effect, for the hooks and the crosshair: set at start from the settings and by the in-game row.
void SetCurrent(Preset preset, std::optional<Look> custom);
Preset CurrentPreset();
std::optional<Look> CurrentCustom();
std::optional<Look> CurrentLook(double seconds);

}  // namespace gw2::hud_colour
