// Title logo tests: TGA decoding and writing (raw and run-length, both origins), stamping PC PLUS+ and when it must
// not, and installing logo0_pcplus.tga. A made-up logo in a temporary folder; the game's logo0.tga is never read.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "title_logo.h"

namespace fs = std::filesystem;
using namespace gw2;

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

void Section(const char* name) {
  std::printf("%s\n", name);
}

const fs::path kRoot = fs::temp_directory_path() / "gw2_title_logo_tests";

int Sum(const TgaImage& image, int x, int y) {
  const uint8_t* p = &image.rgb[(size_t(y) * image.width + x) * 3];
  return p[0] + p[1] + p[2];
}

void Set(TgaImage& image, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* p = &image.rgb[(size_t(y) * image.width + x) * 3];
  p[0] = r;
  p[1] = g;
  p[2] = b;
}

// Black, with a dim outline row under where GEOMETRY would be, broken by gaps like the spaces between its letters.
TgaImage FakeLogo(uint8_t descriptor = 0) {
  TgaImage image{1920, 300, descriptor, std::vector<uint8_t>(size_t(1920) * 300 * 3, 0)};
  for (int x = 700; x < 1100; ++x) {
    if (x % 60 < 10) continue;
    Set(image, x, 176, 150, 60, uint8_t(x / 8));
  }
  return image;
}

// A run-length TGA of the same image: runs of equal pixels, raw packets for the rest.
std::string EncodeRle(const TgaImage& image) {
  std::string raw = EncodeTga(image);
  std::string out = raw.substr(0, 18);
  out[2] = 10;
  const std::string pixels = raw.substr(18);
  const size_t count = pixels.size() / 3;
  for (size_t i = 0; i < count;) {
    size_t run = 1;
    while (i + run < count && run < 128 && pixels.compare(i * 3, 3, pixels, (i + run) * 3, 3) == 0) ++run;
    if (run > 1) {
      out += char(0x80 | (run - 1));
      out += pixels.substr(i * 3, 3);
    } else {
      out += char(0);
      out += pixels.substr(i * 3, 3);
    }
    i += run;
  }
  return out;
}

std::string ReadAll(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteAll(const fs::path& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary | std::ios::trunc).write(bytes.data(), std::streamsize(bytes.size()));
}

void TestTga() {
  Section("TGA");
  for (const uint8_t descriptor : {uint8_t(0x00), uint8_t(0x20)}) {
    TgaImage image = FakeLogo(descriptor);
    Set(image, 0, 0, 255, 0, 0);  // top-left red
    const std::string raw = EncodeTga(image);
    CHECK(raw.size() == 18 + image.rgb.size());
    CHECK(uint8_t(raw[17]) == descriptor);
    // Bottom-left origin stores the bottom row first, so the red pixel is in the last stored row.
    const size_t red_at = 18 + (descriptor ? 0 : size_t(299) * 1920 * 3);
    CHECK(uint8_t(raw[red_at + 2]) == 255 && raw[red_at] == 0);
    const auto back = DecodeTga(raw);
    CHECK(back && back->rgb == image.rgb && back->descriptor == descriptor);
    const auto rle = DecodeTga(EncodeRle(image));
    CHECK(rle && rle->rgb == image.rgb);
  }
  CHECK(!DecodeTga(""));
  std::string bad = EncodeTga(FakeLogo());
  bad[16] = 32;  // 32-bit is not the logo's format
  CHECK(!DecodeTga(bad));
  CHECK(!DecodeTga(EncodeTga(FakeLogo()).substr(0, 5000)));
  CHECK(!DecodeTga(EncodeRle(FakeLogo()).substr(0, 5000)));
}

void TestStamp() {
  Section("Stamp");
  TgaImage image = FakeLogo();
  const TgaImage before = image;
  std::string why;
  CHECK(StampTitleLogo(image, &why));
  // P's top-left corner and the + centre are lit at about RETRO EVOLVED's brightness (534, less a little where a
  // channel stops at 255); inside the C stays black.
  CHECK(Sum(image, 820, 194) >= 520);
  CHECK(Sum(image, 1083 - 13, 194 + 13) >= 520);
  CHECK(Sum(image, 854 + 10, 194 + 10) == 0);
  // Nothing outside the lettering's rows changes.
  bool outside_same = true;
  for (int y = 0; y < 300; ++y) {
    if (y >= 194 && y <= 221) continue;
    for (int x = 0; x < 1920; ++x) outside_same &= Sum(image, x, y) == Sum(before, x, y);
  }
  CHECK(outside_same);

  TgaImage busy = FakeLogo();
  Set(busy, 950, 200, 10, 10, 10);
  const TgaImage busy_before = busy;
  CHECK(!StampTitleLogo(busy, &why));
  CHECK(busy.rgb == busy_before.rgb);
  CHECK(why == "the space under GEOMETRY is not empty");

  TgaImage dark{1920, 300, 0, std::vector<uint8_t>(size_t(1920) * 300 * 3, 0)};
  CHECK(!StampTitleLogo(dark, &why));
  TgaImage small{64, 64, 0, std::vector<uint8_t>(size_t(64) * 64 * 3, 0)};
  CHECK(!StampTitleLogo(small, &why));
  CHECK(why == "not the 1920x300 logo");
}

void TestInstall() {
  Section("Install");
  std::error_code ec;
  fs::remove_all(kRoot, ec);
  fs::create_directories(kRoot);
  const fs::path source = kRoot / kTitleLogoSource;
  const fs::path target = kRoot / kTitleLogoFile;

  TitleLogoInstall missing = InstallTitleLogo(kRoot);
  CHECK(!missing.ready && !TitleLogoReady() && !fs::exists(target));

  const std::string original = EncodeRle(FakeLogo());
  WriteAll(source, original);
  TitleLogoInstall first = InstallTitleLogo(kRoot);
  CHECK(first.ready && first.written && TitleLogoReady());
  CHECK(ReadAll(source) == original);
  TgaImage expected = FakeLogo();
  StampTitleLogo(expected);
  CHECK(ReadAll(target) == EncodeTga(expected));
  CHECK(!fs::exists(target.string() + ".tmp"));

  const auto written_at = fs::last_write_time(target);
  TitleLogoInstall again = InstallTitleLogo(kRoot);
  CHECK(again.ready && !again.written && fs::last_write_time(target) == written_at);

  WriteAll(target, "stale");
  TitleLogoInstall repaired = InstallTitleLogo(kRoot);
  CHECK(repaired.ready && repaired.written && ReadAll(target) == EncodeTga(expected));

  TgaImage busy = FakeLogo();
  Set(busy, 950, 200, 10, 10, 10);
  WriteAll(source, EncodeTga(busy));
  TitleLogoInstall refused = InstallTitleLogo(kRoot);
  CHECK(!refused.ready && !TitleLogoReady());
  fs::remove_all(kRoot, ec);
}

}  // namespace

int main() {
  TestTga();
  TestStamp();
  TestInstall();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
