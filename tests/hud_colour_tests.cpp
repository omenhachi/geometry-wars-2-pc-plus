// Interface colour tests: which of the game's colours are recoloured, what they become, the presets, the custom
// colour and the in-game cycle. Pure maths; no game or runtime.

#include <cmath>
#include <cstdio>
#include <string>

#include "hud_colour.h"

using namespace gw2::hud_colour;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

bool Near(float a, float b, float tolerance = 0.01f) {
  return std::fabs(a - b) <= tolerance;
}

// The game's menu and HUD green at full strength, and halfway through a fade (premultiplied).
constexpr Rgb kGreen{0.143f, 1.219f, 0.0f};
constexpr Rgb kGreenHalf{0.0715f, 0.6095f, 0.0f};
constexpr Rgb kPaleGreen{0.64f, 1.1f, 0.34f};

float HueOf(Rgb c) {
  const float hi = std::fmax(c.r, std::fmax(c.g, c.b)), lo = std::fmin(c.r, std::fmin(c.g, c.b));
  const float d = hi - lo;
  if (d <= 0) return -1;
  float h = hi == c.r ? std::fmod((c.g - c.b) / d, 6.0f) : hi == c.g ? (c.b - c.r) / d + 2.0f : (c.r - c.g) / d + 4.0f;
  h *= 60.0f;
  return h < 0 ? h + 360.0f : h;
}

void TestRecolour() {
  std::printf("Recolour\n");
  const Look blue{200, 1.0f, 1.0f};
  const auto out = Recolour(kGreen, blue);
  CHECK(out && Near(HueOf(*out), 200, 0.5f));
  // Brightness above 1 is the game's glow and is kept; so is the fade.
  CHECK(out && Near(std::fmax(out->r, std::fmax(out->g, out->b)), 1.219f));
  const auto half = Recolour(kGreenHalf, blue);
  CHECK(half && out && Near(half->b, out->b * 0.5f) && Near(half->g, out->g * 0.5f));
  // Saturation is the colour's own, scaled by the look: the pale green stays pale.
  const auto pale = Recolour(kPaleGreen, blue);
  CHECK(pale && Near(std::fmin(pale->r, std::fmin(pale->g, pale->b)) / 1.1f, 0.34f / 1.1f, 0.02f));
  CHECK(Recolour(kGreen, Look{200, 0.5f, 1.0f}) && Near(Recolour(kGreen, Look{200, 0.5f, 1.0f})->r, 1.219f * 0.5f, 0.02f));
  CHECK(Recolour(kGreen, Look{200, 1.0f, 1.2f}) && Near(Recolour(kGreen, Look{200, 1.0f, 1.2f})->b, 1.219f * 1.2f));
  // Hue 360 is red, the same as 0.
  const auto red = Recolour(kGreen, Look{360, 1, 1});
  CHECK(red && Near(red->r, 1.219f) && red->g < 0.2f && Near(red->b, 0));

  // Only greens: white, grey, black, faded to nothing, red, yellow, cyan and blue are left alone.
  CHECK(!Recolour({1, 1, 1}, blue));
  CHECK(!Recolour({0.5f, 0.5f, 0.5f}, blue));
  CHECK(!Recolour({0, 0, 0}, blue));
  CHECK(!Recolour({1, 0.2f, 0.2f}, blue));
  CHECK(!Recolour({1, 1, 0.1f}, blue));  // yellow, 60 degrees
  CHECK(!Recolour({0.1f, 1, 1}, blue));  // cyan, 180 degrees
  CHECK(!Recolour({0.2f, 0.2f, 1}, blue));
  // The band's edges, 70 and 150 degrees, are in; just outside is not.
  CHECK(Recolour({0.8333f, 1, 0}, blue));   // 70
  CHECK(Recolour({0, 1, 0.5f}, blue));      // 150
  CHECK(!Recolour({0.85f, 1, 0}, blue));    // 69
  CHECK(!Recolour({0, 1, 0.52f}, blue));    // 151
}

void TestPresets() {
  std::printf("Presets\n");
  // Green changes nothing at all.
  CHECK(!LookFor(Preset::kGreen, std::nullopt, 0));
  for (const Preset p : {Preset::kBlue, Preset::kPurple, Preset::kPink, Preset::kGold}) {
    const auto look = LookFor(p, std::nullopt, 0);
    CHECK(look.has_value());
    CHECK(look && Recolour(kGreen, *look).has_value());
  }
  CHECK(LookFor(Preset::kGold, std::nullopt, 0) && Near(LookFor(Preset::kGold, std::nullopt, 0)->hue, 34));
  // Rainbow drifts round the spectrum, slowly.
  const auto r0 = LookFor(Preset::kRainbow, std::nullopt, 0);
  const auto r1 = LookFor(Preset::kRainbow, std::nullopt, 1);
  const auto r12 = LookFor(Preset::kRainbow, std::nullopt, 12);
  CHECK(r0 && r1 && r12 && r1->hue > r0->hue && r1->hue - r0->hue < 45 && Near(r12->hue, r0->hue));
  // Custom uses the custom look, and without one there is nothing to change.
  CHECK(!LookFor(Preset::kCustom, std::nullopt, 0));
  CHECK(LookFor(Preset::kCustom, Look{10, 1, 1}, 0) && Near(LookFor(Preset::kCustom, Look{10, 1, 1}, 0)->hue, 10));

  for (const Preset p : {Preset::kGreen, Preset::kBlue, Preset::kPurple, Preset::kPink, Preset::kGold,
                         Preset::kRainbow, Preset::kCustom}) {
    CHECK(Parse(Name(p)) == p);
    CHECK(!Label(p).empty());
  }
  CHECK(!Parse("Blue"));
  CHECK(!Parse(""));
  CHECK(!Parse("teal"));
}

void TestCustom() {
  std::printf("Custom\n");
  const auto orange = CustomLook("#FF8000");
  CHECK(orange && Near(orange->hue, 30.1f, 0.3f) && Near(orange->saturation, 1));
  const auto white = CustomLook("#ffffff");
  CHECK(white && Near(white->saturation, 0));
  CHECK(white && Recolour(kGreen, *white) && Near(Recolour(kGreen, *white)->r, Recolour(kGreen, *white)->b));
  const auto dark_red = CustomLook("#400000");
  CHECK(dark_red && Near(dark_red->hue, 0) && Near(dark_red->saturation, 1) && Near(dark_red->value, 1));
  CHECK(!CustomLook("#000000"));
  CHECK(!CustomLook("FF8000"));
  CHECK(!CustomLook("#FF80"));
  CHECK(!CustomLook("#GG8000"));
  CHECK(!CustomLook(""));
  CHECK(FormatHex({1, 0.5f, 0}) == "#FF8000");
  CHECK(FormatHex({2, -1, 0.25f}) == "#FF0040");
}

void TestSwatch() {
  std::printf("Swatch\n");
  const Rgb green = Swatch(Preset::kGreen, std::nullopt, 0);
  CHECK(Near(green.g, 1) && green.r < 0.2f && Near(green.b, 0));
  const Rgb blue = Swatch(Preset::kBlue, std::nullopt, 0);
  CHECK(blue.b >= blue.g && blue.b >= blue.r && blue.b <= 1.0f);
  const Rgb custom = Swatch(Preset::kCustom, CustomLook("#FF0000"), 0);
  CHECK(Near(custom.r, 1) && custom.g < 0.2f);
  // Custom with nothing saved shows the game's green.
  CHECK(Near(Swatch(Preset::kCustom, std::nullopt, 0).g, 1));
}

void TestCycleAndCurrent() {
  std::printf("Cycle\n");
  const auto plain = Cycle(false);
  CHECK(plain.size() == 6 && plain.front() == Preset::kGreen && plain.back() == Preset::kRainbow);
  const auto with_custom = Cycle(true);
  CHECK(with_custom.size() == 7 && with_custom.back() == Preset::kCustom);

  SetCurrent(Preset::kBlue, std::nullopt);
  CHECK(CurrentPreset() == Preset::kBlue && CurrentLook(0).has_value());
  // Custom without a colour falls back to Green, and the custom colour survives other choices.
  SetCurrent(Preset::kCustom, std::nullopt);
  CHECK(CurrentPreset() == Preset::kGreen && !CurrentLook(0));
  SetCurrent(Preset::kGold, Look{120, 1, 1});
  CHECK(CurrentCustom() && Near(CurrentCustom()->hue, 120));
  SetCurrent(Preset::kGreen, std::nullopt);
}

}  // namespace

int main() {
  TestRecolour();
  TestPresets();
  TestCustom();
  TestSwatch();
  TestCycleAndCurrent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
