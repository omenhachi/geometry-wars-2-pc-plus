// Aspect Ratio tests: the format table, the canvas and video mode sizes, the Window size choices and their repair,
// the Internal render resolution maths per format, and the removal of the retired Letterbox/Stretch setting.
// Temporary folders only; no runtime and no game files.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "aspect_ratio.h"
#include "internal_render_scale.h"
#include "retired_settings.h"

using namespace gw2;
namespace fs = std::filesystem;

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

#ifdef GW2_SRC_DIR
const fs::path kSrcDir = GW2_SRC_DIR;
#else
const fs::path kSrcDir = fs::path(__FILE__).parent_path().parent_path() / "src";
#endif

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestTable() {
  std::printf("table\n");
  CHECK(std::size(kAspectRatios) == 6);
  const char* keys[] = {"16:9", "16:10", "21:9", "32:9", "4:3", "5:4"};
  for (size_t i = 0; i < std::size(keys); ++i) {
    CHECK(std::string_view(kAspectRatios[i].key) == keys[i]);
    CHECK(ParseAspectRatio(keys[i]) == kAspectRatios[i].aspect);
    CHECK(InfoFor(kAspectRatios[i].aspect).key == kAspectRatios[i].key);
  }
  CHECK(std::string_view(InfoFor(AspectRatio::k16x9).label) == "16:9 Original");
  CHECK(std::string_view(InfoFor(AspectRatio::k21x9).label) == "21:9 Ultrawide");
  CHECK(std::string_view(InfoFor(AspectRatio::k32x9).label) == "32:9 Super Ultrawide");
  for (const char* odd : {"", "16:9 ", "21x9", "garbage", "true", "false", "0"}) {
    CHECK(ParseAspectRatio(odd) == AspectRatio::k16x9);
  }
}

void TestCanvas() {
  std::printf("canvas and video mode\n");
  struct Row {
    AspectRatio aspect;
    int width;
  };
  const Row rows[] = {{AspectRatio::k16x9, 1280}, {AspectRatio::k16x10, 1152}, {AspectRatio::k21x9, 1680},
                      {AspectRatio::k32x9, 2560}, {AspectRatio::k4x3, 960},     {AspectRatio::k5x4, 896}};
  for (const Row& row : rows) {
    const AspectRatioInfo& info = InfoFor(row.aspect);
    CHECK(CanvasWidth(row.aspect) == row.width);
    CHECK(CanvasWidth(row.aspect) % kCanvasWidthStep == 0);
    // At most half a step away from the exact format.
    const double exact = double(kCanvasHeight) * info.ratio_x / info.ratio_y;
    CHECK(std::abs(CanvasWidth(row.aspect) - exact) <= kCanvasWidthStep / 2.0);
    CHECK(std::abs(CanvasAspect(row.aspect) * info.ratio_y / info.ratio_x - 1.0) < 0.005);

    const WindowSize mode = VideoModeSize(row.aspect);
    // Valid for the SDK's cvars, and different from their 1280x720 defaults, which the SDK would ignore.
    CHECK(mode.width >= 640 && mode.width <= 0x0FFF && mode.height >= 480 && mode.height <= 0x0FFF);
    CHECK(mode.width != 1280 && mode.height != 720);
    // Exactly the canvas shape, so the presenter keeps it as it is.
    CHECK(mode.width * kCanvasHeight == mode.height * CanvasWidth(row.aspect));
    CHECK(std::abs(double(mode.width) / mode.height * info.ratio_y / info.ratio_x - 1.0) < 0.005);
  }
  CHECK(CanvasWidth(AspectRatio::k16x9) == 1280 && kCanvasHeight == 720);
}

void TestRenderSize() {
  std::printf("render resolution size\n");
  const WindowSize original = RenderSize(AspectRatio::k21x9, 1);
  CHECK(original.width == 1680 && original.height == 720);
  const WindowSize doubled = RenderSize(AspectRatio::k21x9, 2);
  CHECK(doubled.width == 3360 && doubled.height == 1440);
  const WindowSize quadrupled = RenderSize(AspectRatio::k21x9, 4);
  CHECK(quadrupled.width == 6720 && quadrupled.height == 2880);
  const WindowSize originalFormatDoubled = RenderSize(AspectRatio::k16x9, 2);
  CHECK(originalFormatDoubled.width == 2560 && originalFormatDoubled.height == 1440);
}

void TestWindowSizes() {
  std::printf("window sizes\n");
  for (const AspectRatioInfo& info : kAspectRatios) {
    int listed = 0;
    bool default_listed = false;
    for (const AspectWindowSize& size : kWindowSizes) {
      if (size.aspect != info.aspect) {
        CHECK(!WindowMatchesAspect({size.width, size.height}, info.aspect));
        continue;
      }
      ++listed;
      CHECK(size.width > 0 && size.height > 0);
      CHECK(WindowMatchesAspect({size.width, size.height}, info.aspect));
      default_listed = default_listed || (size.width == info.default_window.width && size.height == info.default_window.height);
    }
    CHECK(listed >= 2);
    CHECK(default_listed);
  }
  // 16:9 keeps the sizes of earlier launchers, in the same order.
  const AspectWindowSize expected[] = {{853, 480, AspectRatio::k16x9},
                                       {1280, 720, AspectRatio::k16x9},
                                       {1600, 900, AspectRatio::k16x9},
                                       {1920, 1080, AspectRatio::k16x9}};
  for (size_t i = 0; i < std::size(expected); ++i) {
    CHECK(kWindowSizes[i].width == expected[i].width && kWindowSizes[i].height == expected[i].height &&
          kWindowSizes[i].aspect == AspectRatio::k16x9);
  }
  // A custom size of the right shape belongs to the format; the wrong shape, or no size, does not.
  CHECK(WindowMatchesAspect({1366, 768}, AspectRatio::k16x9));
  CHECK(!WindowMatchesAspect({1366, 768}, AspectRatio::k16x10));
  CHECK(!WindowMatchesAspect({0, 0}, AspectRatio::k16x9));
  CHECK(!WindowMatchesAspect({1280, -720}, AspectRatio::k16x9));
  CHECK(!WindowMatchesAspect({-1280, 720}, AspectRatio::k16x9));
}

void TestWindowSizeRepair() {
  std::printf("window size repair\n");
  const auto fits_all = [](WindowSize) { return true; };
  const auto fits_none = [](WindowSize) { return false; };
  const auto fits_small = [](WindowSize size) { return size.width <= 1700; };

  // The size already has the format's shape: kept, even when it is not listed.
  CHECK(WindowSizeForAspect(AspectRatio::k21x9, {2100, 900}, fits_all).width == 2100);
  const WindowSize custom = WindowSizeForAspect(AspectRatio::k16x9, {1366, 768}, fits_all);
  CHECK(custom.width == 1366 && custom.height == 768);

  // Changing the format moves to that format's default.
  for (const AspectRatioInfo& info : kAspectRatios) {
    const WindowSize chosen = WindowSizeForAspect(info.aspect, {1280, 720 + 1000}, fits_all);
    CHECK(chosen.width == info.default_window.width && chosen.height == info.default_window.height);
    CHECK(WindowMatchesAspect(chosen, info.aspect));
  }
  CHECK(WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, fits_all).width == 1680);
  CHECK(WindowSizeForAspect(AspectRatio::k16x9, {1680, 720}, fits_all).width == 1280);

  // Saved zero, negative or missing sizes are replaced.
  for (const WindowSize bad : {WindowSize{0, 0}, WindowSize{-1, -1}, WindowSize{1280, 0}, WindowSize{0, 720}}) {
    const WindowSize chosen = WindowSizeForAspect(AspectRatio::k16x9, bad, fits_all);
    CHECK(chosen.width == 1280 && chosen.height == 720);
  }

  // A default that does not fit the display gives the largest listed size that does.
  const WindowSize largest = WindowSizeForAspect(AspectRatio::k32x9, {1280, 720}, fits_small);
  CHECK(largest.width == 1600 && largest.height == 450);
  const WindowSize ultrawide = WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, [](WindowSize s) {
    return s.width < 1680;
  });
  CHECK(ultrawide.width == 1260 && ultrawide.height == 540);

  // Nothing fits: still a valid size of the format; the window fit shrinks it when the game starts.
  for (const AspectRatioInfo& info : kAspectRatios) {
    const WindowSize chosen = WindowSizeForAspect(info.aspect, {640, 100}, fits_none);
    CHECK(chosen.width > 0 && chosen.height > 0 && WindowMatchesAspect(chosen, info.aspect));
  }
}

void TestInternalRenderPerAspect() {
  std::printf("internal render resolution per aspect ratio\n");
  struct Row {
    AspectRatio aspect;
    int scale, percent, width, height;
  };
  const Row rows[] = {
      {AspectRatio::k16x9, 1, 100, 1280, 720},  {AspectRatio::k16x10, 1, 75, 864, 540},
      {AspectRatio::k21x9, 1, 75, 1260, 540},   {AspectRatio::k32x9, 1, 50, 1280, 360},
      {AspectRatio::k4x3, 1, 75, 720, 540},     {AspectRatio::k5x4, 1, 50, 448, 360},
      {AspectRatio::k21x9, 2, 75, 2520, 1080},  {AspectRatio::k16x9, 4, 50, 2560, 1440},
  };
  for (const Row& row : rows) {
    CHECK(InternalRenderSize(CanvasWidth(row.aspect), row.scale, row.percent) == row.width);
    CHECK(InternalRenderSize(kCanvasHeight, row.scale, row.percent) == row.height);
    const InternalRenderSplit split = SplitInternalRenderScale(row.scale, row.percent);
    CHECK(CanvasWidth(row.aspect) * split.host_quarters / 4 == row.width);
    CHECK(kCanvasHeight * split.host_quarters / 4 == row.height);
  }
  // Every format, Render resolution and share gives whole host pixels, in the shape of the format.
  for (const AspectRatioInfo& info : kAspectRatios) {
    for (int scale = 1; scale <= 4; ++scale) {
      for (const int percent : {100, 75, 50}) {
        const InternalRenderSplit split = SplitInternalRenderScale(scale, percent);
        CHECK(CanvasWidth(info.aspect) * split.host_quarters % 4 == 0);
        CHECK(kCanvasHeight * split.host_quarters % 4 == 0);
        const long long width = CanvasWidth(info.aspect) * split.host_quarters / 4;
        const long long height = kCanvasHeight * split.host_quarters / 4;
        CHECK(width * kCanvasHeight == height * CanvasWidth(info.aspect));
        CHECK(width == InternalRenderSize(CanvasWidth(info.aspect), scale, percent));
      }
    }
  }
}

void TestRetiredSetting() {
  std::printf("retired letterbox setting\n");
  CHECK(RemoveSettingLines("a = 1\npresent_letterbox = true\nb = 2\n", "present_letterbox") == "a = 1\nb = 2\n");
  CHECK(RemoveSettingLines("present_letterbox=false\r\nx = 1\r\n", "present_letterbox") == "x = 1\r\n");
  CHECK(RemoveSettingLines("  present_letterbox\t= true", "present_letterbox").empty());
  CHECK(RemoveSettingLines("present_letterbox_x = 1\n# present_letterbox = true\nxpresent_letterbox = 1\n",
                           "present_letterbox") ==
        "present_letterbox_x = 1\n# present_letterbox = true\nxpresent_letterbox = 1\n");
  CHECK(RemoveSettingLines("", "present_letterbox").empty());

  const fs::path dir = fs::temp_directory_path() / "gw2_aspect_tests";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  const fs::path file = dir / "settings.toml";
  for (const char* value : {"true", "false"}) {
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        << "# note\nresolution_scale = 2\npresent_letterbox = " << value << "\ngw2_bloom = \"off\"\n";
    CHECK(RemoveRetiredSetting(file, "present_letterbox") == RetiredSettingResult::kRemoved);
    CHECK(ReadText(file) == "# note\nresolution_scale = 2\ngw2_bloom = \"off\"\n");
    CHECK(!fs::exists(dir / "settings.toml.tmp"));
    const std::string after = ReadText(file);
    CHECK(RemoveRetiredSetting(file, "present_letterbox") == RetiredSettingResult::kAbsent);
    CHECK(ReadText(file) == after);
  }
  CHECK(RemoveRetiredSetting(dir / "missing.toml", "present_letterbox") == RetiredSettingResult::kAbsent);
  fs::remove_all(dir, ec);
}

double ScreenHalfWidth(const Projection& projection) {
  return std::tan(projection.fov / 2.0) * projection.aspect;
}

void TestProjection() {
  std::printf("projection\n");
  const double fov = 36.0 * 3.14159265358979323846 / 180.0;
  const double half_height = std::tan(fov / 2.0);
  // 16:9 is the game's own camera.
  const Projection original = ProjectionFor(fov, kGameAspect);
  CHECK(original.fov == fov && original.aspect == kGameAspect);
  for (const AspectRatioInfo& info : kAspectRatios) {
    const Projection projection = ProjectionFor(fov, CanvasAspect(info.aspect));
    CHECK(projection.aspect == CanvasAspect(info.aspect));
    if (CanvasAspect(info.aspect) >= kGameAspect) {
      // Wider: same vertical view, more of the world at the sides.
      CHECK(projection.fov == fov);
      CHECK(std::tan(projection.fov / 2.0) == half_height);
      CHECK(ScreenHalfWidth(projection) >= ScreenHalfWidth(original) * 0.999999);
    } else {
      // Narrower: the 16:9 horizontal view is kept, so nothing the 16:9 picture shows is cut off at the sides.
      CHECK(projection.fov > fov);
      CHECK(std::abs(ScreenHalfWidth(projection) - ScreenHalfWidth(original)) < 1e-9);
      CHECK(std::tan(projection.fov / 2.0) > half_height);
    }
  }
  // Also at the game's zoomed-in fields of view.
  for (const double zoom : {0.7, 1.0, 1.4286}) {
    const double zoomed = fov / zoom;
    const Projection narrow = ProjectionFor(zoomed, CanvasAspect(AspectRatio::k4x3));
    CHECK(std::abs(ScreenHalfWidth(narrow) - ScreenHalfWidth(ProjectionFor(zoomed, kGameAspect))) < 1e-9);
  }
  CHECK(kGameAspect == double(float(16.0 / 9.0)));
}

void TestSpriteFit() {
  std::printf("full-screen sprite\n");
  for (const AspectRatioInfo& info : kAspectRatios) {
    const double aspect = CanvasAspect(info.aspect);
    const SpriteRect rect = FitSpriteTo16x9(aspect);
    // The sprite space is mapped onto the whole canvas: one unit is canvas_width/640 wide and canvas_height/360 tall.
    const double shape = (rect.width / kSpriteSpaceWidth * aspect) / (rect.height / kSpriteSpaceHeight);
    CHECK(std::abs(shape - kGameAspect) < 1e-4);
    CHECK(rect.x >= 0 && rect.y >= 0);
    CHECK(rect.x + rect.width <= kSpriteSpaceWidth + 1e-3f && rect.y + rect.height <= kSpriteSpaceHeight + 1e-3f);
    // Centered, and touching the full width or the full height.
    CHECK(std::abs(rect.x - (kSpriteSpaceWidth - rect.width - rect.x)) < 1e-3f);
    CHECK(std::abs(rect.y - (kSpriteSpaceHeight - rect.height - rect.y)) < 1e-3f);
    CHECK(rect.width == kSpriteSpaceWidth || rect.height == kSpriteSpaceHeight);
  }
  const SpriteRect original = FitSpriteTo16x9(kGameAspect);
  CHECK(original.x == 0 && original.y == 0 && original.width == kSpriteSpaceWidth && original.height == kSpriteSpaceHeight);
}

void TestHookSources() {
  std::printf("game hooks\n");
  const std::string hooks = ReadText(kSrcDir / "aspect_render.cpp");
  // The functions of the pinned executable: the graphics init's device creation (single caller sub_8205D538), the
  // perspective builder (cameras at sub_82067A20, sub_82067E40 and sub_82068408) and the full-screen sprite queue
  // whose splash call returns to 0x8207E9E0.
  CHECK(hooks.find("REX_HOOK_RAW(sub_820A1D58)") != std::string::npos);
  CHECK(hooks.find("REX_EXTERN(__imp__sub_820A1D58)") != std::string::npos);
  CHECK(hooks.find("REX_HOOK_RAW(sub_8206A300)") != std::string::npos);
  CHECK(hooks.find("REX_EXTERN(__imp__sub_8206A300)") != std::string::npos);
  CHECK(hooks.find("REX_HOOK_RAW(sub_8205D8F8)") != std::string::npos);
  CHECK(hooks.find("REX_EXTERN(__imp__sub_8205D8F8)") != std::string::npos);
  CHECK(hooks.find("REX_HOOK_RAW(__imp__XGetVideoMode)") != std::string::npos);
  CHECK(hooks.find("kCanvasWidthAddress = 0x821A1D7C") != std::string::npos);
  CHECK(hooks.find("kSplashSpriteReturn = 0x8207E9E0") != std::string::npos);
  // Each hook leaves the game alone unless the state is the one analysed.
  CHECK(hooks.find("ctx.f2.f64 == gw2::kGameAspect") != std::string::npos);
  CHECK(hooks.find("ctx.lr == kSplashSpriteReturn") != std::string::npos);
  CHECK(hooks.find("not the expected 1280x720") != std::string::npos);
  CHECK(hooks.find("DEBUG_SAULO") == std::string::npos);
  CHECK(ReadText(kSrcDir / ".." / "CMakeLists.txt").find("src/aspect_render.cpp") != std::string::npos);
  CHECK(ReadText(kSrcDir / ".." / "CMakeLists.txt").find("debug_saulo") == std::string::npos);
}

}  // namespace

int main() {
  TestTable();
  TestCanvas();
  TestRenderSize();
  TestWindowSizes();
  TestWindowSizeRepair();
  TestInternalRenderPerAspect();
  TestProjection();
  TestSpriteFit();
  TestHookSources();
  TestRetiredSetting();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
