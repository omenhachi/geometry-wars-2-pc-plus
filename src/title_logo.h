// PC PLUS+ on the title: the menu logo (game/logo0.tga, "GEOMETRY WARS 2 / RETRO EVOLVED" in one texture) gets
// "PC PLUS+" under GEOMETRY, in RETRO EVOLVED's lettering and GEOMETRY's colours. The player's own logo0.tga is read
// and a stamped copy written next to it as logo0_pcplus.tga, which the texture loader hook loads in its place
// (keyboard_prompts.cpp). logo0.tga itself is never changed, and nothing of the game's is shipped.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gw2 {

inline constexpr char kTitleLogoSource[] = "logo0.tga";
inline constexpr char kTitleLogoFile[] = "logo0_pcplus.tga";

// A 24-bit image, top row first, 3 bytes per pixel in R, G, B order. The descriptor byte is kept so the file is
// written back with the original's origin.
struct TgaImage {
  int width = 0;
  int height = 0;
  uint8_t descriptor = 0;
  std::vector<uint8_t> rgb;
};

// Uncompressed (type 2) or run-length (type 10) 24-bit true colour; anything else is empty.
std::optional<TgaImage> DecodeTga(std::string_view bytes);
// Uncompressed 24-bit, same origin as the descriptor says.
std::string EncodeTga(const TgaImage& image);

// Stamps PC PLUS+ when the image is the 1920x300 logo and the space under GEOMETRY is empty; otherwise leaves the
// image alone and says why.
bool StampTitleLogo(TgaImage& image, std::string* why = nullptr);

struct TitleLogoInstall {
  bool ready = false;    // logo0_pcplus.tga is in place and current
  bool written = false;  // it was (re)written this time
  std::string detail;    // why it is not ready
};

// Before the runtime mounts the game folder (it lists it once). Rewrites logo0_pcplus.tga only when it would change.
TitleLogoInstall InstallTitleLogo(const std::filesystem::path& game_data_root);

// Whether the loader should swap logo0.tga for the stamped copy: set by InstallTitleLogo.
bool TitleLogoReady();

}  // namespace gw2
