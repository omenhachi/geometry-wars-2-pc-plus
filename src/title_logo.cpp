#include "title_logo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace gw2 {

namespace {

std::atomic<bool> g_ready{false};

// The logo is 1920x300. RETRO EVOLVED is 28x28 cells with 4 px strokes on rows 194-221, a 34 px letter pitch and a
// 66 px pitch across a space; PC PLUS+ copies it, right-aligned to end under the Y of GEOMETRY as RETRO EVOLVED ends
// under the S of WARS.
constexpr int kLogoWidth = 1920;
constexpr int kLogoHeight = 300;
constexpr int kTop = 194;
constexpr int kCell = 28;
constexpr int kPitch = 34;
constexpr int kSpacePitch = 32;  // added to the pitch for a space
constexpr int kRight = 1083;     // the Y's last column
constexpr int kMiddle = 11;      // the middle bar's top row in the cell, as in R, E and the Retro P

// The space above PC PLUS+ that GEOMETRY's outline colours are taken from, and how bright a colour must be to count.
constexpr int kOutlineTop = 60;
constexpr int kOutlineBottom = 184;
constexpr int kOutlineMinimum = 300;
// RETRO EVOLVED's grey (178 x 3): the lettering is brightened to at least this, so the bloom treats it alike.
constexpr int kBrightness = 534;

struct Bar {
  int x0, y0, x1, y1;  // inclusive, in the cell
};
constexpr Bar kTopBar{0, 0, 27, 3};
constexpr Bar kBottomBar{0, 24, 27, 27};
constexpr Bar kMiddleBar{0, kMiddle, 27, kMiddle + 3};
constexpr Bar kLeft{0, 0, 3, 27};
constexpr Bar kRightSide{24, 0, 27, 27};

struct Glyph {
  char letter;
  std::array<Bar, 5> bars;
  int count;
};
constexpr Glyph kGlyphs[] = {
    {'P', {kTopBar, kMiddleBar, kLeft, Bar{24, 0, 27, kMiddle + 3}}, 4},
    {'C', {kTopBar, kBottomBar, kLeft}, 3},
    {'L', {kLeft, kBottomBar}, 2},
    {'U', {kLeft, kRightSide, kBottomBar}, 3},
    {'S', {kTopBar, kMiddleBar, kBottomBar, Bar{0, 0, 3, kMiddle + 3}, Bar{24, kMiddle, 27, 27}}, 5},
    {'+', {Bar{12, 0, 15, 27}, Bar{0, 12, 27, 15}}, 2},
};
constexpr std::string_view kText = "PC PLUS+";

const Glyph* Find(char letter) {
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.letter == letter) return &glyph;
  }
  return nullptr;
}

int TextWidth() {
  int x = 0;
  for (char c : kText) x += c == ' ' ? kSpacePitch : kPitch;
  return x - (kPitch - kCell);
}

struct Rgb {
  int r = 0, g = 0, b = 0;
  int Sum() const { return r + g + b; }
};

Rgb Pixel(const TgaImage& image, int x, int y) {
  const uint8_t* p = &image.rgb[(size_t(y) * image.width + x) * 3];
  return {p[0], p[1], p[2]};
}

void SetPixel(TgaImage& image, int x, int y, Rgb c) {
  uint8_t* p = &image.rgb[(size_t(y) * image.width + x) * 3];
  p[0] = uint8_t(c.r);
  p[1] = uint8_t(c.g);
  p[2] = uint8_t(c.b);
}

// GEOMETRY's outline colour above each column, blended across the gaps between its letters, at RETRO EVOLVED's
// brightness or more. Empty when nothing above is bright enough.
std::vector<Rgb> Colours(const TgaImage& image, int left, int width) {
  std::vector<std::optional<Rgb>> found(width);
  for (int i = 0; i < width; ++i) {
    Rgb best;
    for (int y = kOutlineTop; y <= kOutlineBottom; ++y) {
      const Rgb c = Pixel(image, left + i, y);
      if (c.Sum() > best.Sum()) best = c;
    }
    if (best.Sum() >= kOutlineMinimum) found[i] = best;
  }
  std::vector<Rgb> colours(width);
  for (int i = 0; i < width; ++i) {
    int l = i, r = i;
    while (l >= 0 && !found[l]) --l;
    while (r < width && !found[r]) ++r;
    Rgb c;
    if (l < 0 && r >= width) return {};
    if (l < 0) {
      c = *found[r];
    } else if (r >= width || l == r) {
      c = *found[l];
    } else {
      const float t = float(i - l) / float(r - l);
      const auto mix = [t](int a, int b) { return int(std::lround(a * (1.0f - t) + b * t)); };
      c = {mix(found[l]->r, found[r]->r), mix(found[l]->g, found[r]->g), mix(found[l]->b, found[r]->b)};
    }
    const float k = c.Sum() > 0 ? std::max(1.0f, float(kBrightness) / float(c.Sum())) : 1.0f;
    const auto lift = [k](int v) { return std::min(255, int(std::lround(v * k))); };
    colours[i] = {lift(c.r), lift(c.g), lift(c.b)};
  }
  return colours;
}

std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

std::optional<TgaImage> DecodeTga(std::string_view bytes) {
  if (bytes.size() < 18) return std::nullopt;
  const auto u8 = [&](size_t i) { return uint8_t(bytes[i]); };
  const uint8_t id_length = u8(0);
  const uint8_t colour_map = u8(1);
  const uint8_t type = u8(2);
  TgaImage image;
  image.width = u8(12) | (u8(13) << 8);
  image.height = u8(14) | (u8(15) << 8);
  image.descriptor = u8(17);
  if (colour_map != 0 || (type != 2 && type != 10) || u8(16) != 24 || image.width == 0 || image.height == 0 ||
      (image.descriptor & 0x10) != 0) {
    return std::nullopt;
  }
  const size_t pixels = size_t(image.width) * image.height;
  std::vector<uint8_t> bgr;
  bgr.reserve(pixels * 3);
  size_t at = 18 + id_length;
  if (type == 2) {
    if (bytes.size() < at + pixels * 3) return std::nullopt;
    bgr.assign(bytes.begin() + at, bytes.begin() + at + pixels * 3);
  } else {
    while (bgr.size() < pixels * 3) {
      if (at >= bytes.size()) return std::nullopt;
      const uint8_t head = u8(at++);
      const size_t count = (head & 0x7F) + 1u;
      if (bgr.size() + count * 3 > pixels * 3) return std::nullopt;
      if (head & 0x80) {
        if (at + 3 > bytes.size()) return std::nullopt;
        for (size_t i = 0; i < count; ++i) bgr.insert(bgr.end(), bytes.begin() + at, bytes.begin() + at + 3);
        at += 3;
      } else {
        if (at + count * 3 > bytes.size()) return std::nullopt;
        bgr.insert(bgr.end(), bytes.begin() + at, bytes.begin() + at + count * 3);
        at += count * 3;
      }
    }
  }
  // Bit 5 set: the first stored row is the top one; clear: the bottom one.
  const bool top_first = (image.descriptor & 0x20) != 0;
  image.rgb.resize(pixels * 3);
  for (int row = 0; row < image.height; ++row) {
    const int y = top_first ? row : image.height - 1 - row;
    const uint8_t* from = &bgr[size_t(row) * image.width * 3];
    uint8_t* to = &image.rgb[size_t(y) * image.width * 3];
    for (int x = 0; x < image.width; ++x) {
      to[x * 3 + 0] = from[x * 3 + 2];
      to[x * 3 + 1] = from[x * 3 + 1];
      to[x * 3 + 2] = from[x * 3 + 0];
    }
  }
  return image;
}

std::string EncodeTga(const TgaImage& image) {
  std::string out(18, '\0');
  out[2] = 2;
  out[12] = char(image.width & 0xFF);
  out[13] = char(image.width >> 8);
  out[14] = char(image.height & 0xFF);
  out[15] = char(image.height >> 8);
  out[16] = 24;
  out[17] = char(image.descriptor & 0x20);
  const bool top_first = (image.descriptor & 0x20) != 0;
  out.reserve(out.size() + image.rgb.size());
  for (int row = 0; row < image.height; ++row) {
    const int y = top_first ? row : image.height - 1 - row;
    const uint8_t* from = &image.rgb[size_t(y) * image.width * 3];
    for (int x = 0; x < image.width; ++x) {
      out += char(from[x * 3 + 2]);
      out += char(from[x * 3 + 1]);
      out += char(from[x * 3 + 0]);
    }
  }
  return out;
}

bool StampTitleLogo(TgaImage& image, std::string* why) {
  const auto fail = [why](const char* reason) {
    if (why) *why = reason;
    return false;
  };
  if (image.width != kLogoWidth || image.height != kLogoHeight) return fail("not the 1920x300 logo");
  const int width = TextWidth();
  const int left = kRight - width + 1;
  // A margin of 4 around the lettering must be black: a different logo of the same size keeps its own look.
  for (int y = kTop - 4; y < kTop + kCell + 4; ++y) {
    for (int x = left - 4; x < left + width + 4; ++x) {
      if (Pixel(image, x, y).Sum() != 0) return fail("the space under GEOMETRY is not empty");
    }
  }
  const std::vector<Rgb> colours = Colours(image, left, width);
  if (colours.empty()) return fail("no GEOMETRY lettering above the space");
  int x = left;
  for (char c : kText) {
    if (c == ' ') {
      x += kSpacePitch;
      continue;
    }
    const Glyph* glyph = Find(c);
    for (int i = 0; i < glyph->count; ++i) {
      const Bar& bar = glyph->bars[i];
      for (int y = kTop + bar.y0; y <= kTop + bar.y1; ++y) {
        for (int px = x + bar.x0; px <= x + bar.x1; ++px) SetPixel(image, px, y, colours[px - left]);
      }
    }
    x += kPitch;
  }
  return true;
}

TitleLogoInstall InstallTitleLogo(const std::filesystem::path& game_data_root) {
  TitleLogoInstall result;
  g_ready = false;
  const std::filesystem::path source = game_data_root / kTitleLogoSource;
  const std::filesystem::path target = game_data_root / kTitleLogoFile;
  std::optional<TgaImage> image = DecodeTga(ReadAll(source));
  if (!image) {
    result.detail = source.string() + " is missing or not a 24-bit TGA";
    return result;
  }
  if (!StampTitleLogo(*image, &result.detail)) return result;
  const std::string wanted = EncodeTga(*image);
  std::error_code error;
  if (!std::filesystem::is_regular_file(target, error) || ReadAll(target) != wanted) {
    const std::filesystem::path staging = target.string() + ".tmp";
    {
      std::ofstream out(staging, std::ios::binary | std::ios::trunc);
      out.write(wanted.data(), std::streamsize(wanted.size()));
      if (!out) {
        result.detail = "cannot write " + staging.string();
        return result;
      }
    }
    std::filesystem::rename(staging, target, error);
    if (error) {
      std::filesystem::remove(staging, error);
      result.detail = "cannot write " + target.string();
      return result;
    }
    result.written = true;
  }
  result.ready = true;
  g_ready = true;
  return result;
}

bool TitleLogoReady() {
  return g_ready;
}

}  // namespace gw2
