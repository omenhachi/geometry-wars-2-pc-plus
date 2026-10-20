// Startup diagnostic formatting: version/build identity, selected versus resolved GPU, frame-rate and interpolation
// policy, display fields, and privacy (no absolute user paths). Run: gw2_diagnostics_tests

#include <cstdio>
#include <string>
#include <vector>

#include "diagnostics.h"

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

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string Join(const std::vector<std::string>& lines) {
  std::string text;
  for (const std::string& line : lines) {
    text += line;
    text += '\n';
  }
  return text;
}

gw2::StartupDiagnostics Sample() {
  gw2::StartupDiagnostics diagnostics;
  diagnostics.launcher_display.mode = "Borderless Fullscreen";
  diagnostics.launcher_display.resolution = "1280x720";
  diagnostics.launcher_display.aspect_ratio = "16:9 Original";
  diagnostics.launcher_display.monitor = "1";
  diagnostics.launcher_gameplay.frame_rate = "240";
  diagnostics.launcher_gameplay.vsync = "Off";
  diagnostics.launcher_gameplay.frame_rate_in_title = "On";
  diagnostics.launcher_gameplay.render_resolution_in_title = "Off";
  diagnostics.launcher_gameplay.frame_counter = "Hidden";
  diagnostics.launcher_graphics.graphics_card = "Automatic";
  diagnostics.launcher_graphics.render_resolution = "2x (2560x1440)";
  diagnostics.launcher_graphics.internal_render_resolution = "100%";
  diagnostics.launcher_graphics.msaa = "2x";
  diagnostics.launcher_graphics.anti_aliasing = "Off";
  diagnostics.launcher_graphics.texture_filtering = "Game";
  diagnostics.launcher_graphics.bloom = "Original";
  diagnostics.system.os = "Windows 11 64-bit (build 22631)";
  diagnostics.system.cpu = "Test CPU";
  diagnostics.system.logical_processors = 16;
  diagnostics.gpu.selected = "Automatic";
  diagnostics.gpu.resolved = "NVIDIA GeForce RTX 4070";
  diagnostics.gpu.adapter = 0;
  diagnostics.gpu.vendor_id = 0x10DE;
  diagnostics.gpu.device_id = 0x2786;
  diagnostics.gpu.resolved_known = true;
  diagnostics.display.monitor = 1;
  diagnostics.display.width = 2560;
  diagnostics.display.height = 1440;
  diagnostics.display.refresh_hz = 144;
  diagnostics.display.scale_percent = 225;
  diagnostics.display.fullscreen = true;
  diagnostics.display.configured_width = 1280;
  diagnostics.display.configured_height = 720;
  diagnostics.display.output_width = 2560;
  diagnostics.display.output_height = 1440;
  diagnostics.display.aspect = "16:9";
  diagnostics.game.title_id = 0x584108FF;
  diagnostics.game.executable = "default.xex";
  diagnostics.game.size = 704512;
  diagnostics.game.sha256 = "ABCDEF";
  diagnostics.game.fingerprint = "verified";
  diagnostics.gameplay.simulation = "60Hz";
  diagnostics.gameplay.interpolation = "Automatic, pending measurement";
  diagnostics.gameplay.present = "guest Immediate | host VBlank (VSync On)";
  diagnostics.graphics.render_percent = 100;
  diagnostics.graphics.render_width = 1280;
  diagnostics.graphics.render_height = 720;
  diagnostics.graphics.guest_width = 1280;
  diagnostics.graphics.guest_height = 720;
  diagnostics.graphics.host_quarters = 4;
  diagnostics.graphics.resolve_scale = 1;
  diagnostics.graphics.msaa = "2x native";
  diagnostics.input.keyboard_mode = "Off";
  diagnostics.input.controller = "Xbox One S Controller";
  return diagnostics;
}

void TestIdentity() {
  std::printf("version and build identity\n");
  const gw2::ApplicationIdentity identity{
      .product = "Geometry Wars: Retro Evolved 2",
      .version = "0.4.0",
      .build_config = "Release",
      .architecture = "x86-64",
      .sdk = "0.10.0",
      .backend = "Direct3D 12",
  };
  CHECK(gw2::FormatApplicationVersion(identity) == "Geometry Wars: Retro Evolved 2 port 0.4.0");
  const std::string build = gw2::FormatBuildIdentity(identity);
  CHECK(Has(build, "Build: Release x86-64"));
  CHECK(Has(build, "ReXGlue SDK 0.10.0"));
  CHECK(Has(build, "Direct3D 12"));
}

void TestStartupBlock() {
  std::printf("startup snapshot carries runtime fields\n");
  const std::string text = Join(gw2::FormatStartupDiagnostics(Sample()));
  CHECK(Has(text, "Runtime configuration:"));
  CHECK(Has(text, "Launcher Display: Mode=Borderless Fullscreen | Resolution=1280x720 | "
                  "Aspect Ratio=16:9 Original | Monitor=1"));
  CHECK(Has(text, "Launcher Gameplay: Frame Rate=240 | VSync=Off | Frame rate in title=On | "
                  "Render resolution in title=Off | Frame counter=Hidden"));
  CHECK(Has(text, "Launcher Graphics: Graphics Card=Automatic | Render Resolution=2x (2560x1440) | "
                  "Internal Render Resolution=100% | MSAA=2x | Anti-aliasing=Off | Texture Filtering=Game | "
                  "Bloom=Original"));
  CHECK(Has(text, "Runtime OS: Windows 11 64-bit (build 22631)"));
  CHECK(Has(text, "Runtime CPU: Test CPU | logical processors=16"));
  CHECK(Has(text, "Runtime GPU: selected=Automatic | resolved=NVIDIA GeForce RTX 4070 | adapter=0 | "
                  "vendor/device=10DE/2786"));
  CHECK(Has(text, "Runtime display: 2560x1440@144Hz | scale=225% | monitor=1 | fullscreen | window=1280x720 | "
                  "output=2560x1440 | aspect=16:9"));
  CHECK(Has(text, "Runtime game: title_id=584108FF | default.xex | size=704512 | fingerprint=verified"));
  CHECK(Has(text, "Runtime game sha256: ABCDEF"));
  CHECK(Has(text, "Runtime timing: simulation=60Hz | interpolation=Automatic, pending measurement | "
                  "present=guest Immediate | host VBlank (VSync On)"));
  CHECK(Has(text, "Runtime renderer: render=100% 1280x720 | guest=1280x720 | host_targets=4/4 | resolve=1x | "
                  "msaa=2x native"));
  CHECK(!Has(text, "Runtime timing: frame_rate="));
  CHECK(Has(text, "Runtime input: keyboard_mode=Off | controller=Xbox One S Controller"));
}

void TestSelectedWindowResolution() {
  std::printf("selected window resolution differs from the adjusted window\n");
  gw2::StartupDiagnostics diagnostics = Sample();
  diagnostics.launcher_display.mode = "Windowed";
  diagnostics.launcher_display.resolution = "1260x540";
  diagnostics.launcher_display.aspect_ratio = "21:9 Ultrawide";
  diagnostics.display.fullscreen = false;
  diagnostics.display.configured_width = 1152;
  diagnostics.display.configured_height = 493;
  diagnostics.display.output_width = 1728;
  diagnostics.display.output_height = 740;
  diagnostics.display.aspect = "21:9";
  const std::string text = Join(gw2::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "Launcher Display: Mode=Windowed | Resolution=1260x540 | "
                  "Aspect Ratio=21:9 Ultrawide | Monitor=1"));
  CHECK(Has(text, "Runtime display: 2560x1440@144Hz | scale=225% | monitor=1 | windowed | "
                  "window=1152x493 | output=1728x740 | aspect=21:9"));
}

void TestSelectedAndResolvedDiffer() {
  std::printf("selected and resolved GPU stay distinguishable\n");
  gw2::StartupDiagnostics diagnostics = Sample();
  diagnostics.launcher_graphics.graphics_card = "Intel UHD 730";
  diagnostics.gpu.selected = "Intel UHD 730";
  diagnostics.gpu.resolved = "NVIDIA GeForce RTX 4070";
  const std::string text = Join(gw2::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "Launcher Graphics: Graphics Card=Intel UHD 730"));
  CHECK(Has(text, "Runtime GPU: selected=Intel UHD 730"));
  CHECK(Has(text, "resolved=NVIDIA GeForce RTX 4070"));

  diagnostics.gpu.resolved_known = false;
  CHECK(Has(Join(gw2::FormatStartupDiagnostics(diagnostics)), "resolved=unknown"));

  gw2::StartupDiagnostics automatic = Sample();
  automatic.launcher_graphics.graphics_card = "Automatic";
  automatic.gpu.selected = "Automatic";
  automatic.gpu.resolved = "NVIDIA GeForce RTX 4070";
  const std::string automatic_text = Join(gw2::FormatStartupDiagnostics(automatic));
  CHECK(Has(automatic_text, "Launcher Graphics: Graphics Card=Automatic"));
  CHECK(Has(automatic_text, "Runtime GPU: selected=Automatic | resolved=NVIDIA GeForce RTX 4070"));
}

void TestFrameRatePolicy() {
  std::printf("frame rate, simulation and interpolation policy\n");
  CHECK(gw2::FrameRateText(60) == "60");
  CHECK(gw2::FrameRateText(0) == "Unlimited");
  CHECK(gw2::SimulationRateText(60) == "60Hz");
  CHECK(gw2::SimulationRateText(30) == "30Hz");
  CHECK(gw2::SimulationRateText(144) == "60Hz");
  CHECK(gw2::SimulationRateText(0) == "60Hz");
  CHECK(gw2::InterpolationPolicyText(60) == "Inactive");
  CHECK(gw2::InterpolationPolicyText(30) == "Inactive");
  CHECK(gw2::InterpolationPolicyText(144) == "Automatic, pending measurement");
  CHECK(gw2::InterpolationPolicyText(0) == "Automatic, pending measurement");
  CHECK(gw2::PresentModeText(60) == "VBlank");
  CHECK(gw2::PresentModeText(144) == "Immediate");
  CHECK(gw2::PresentationText(0, false) == "guest Immediate | host VBlank (VSync On)");
  CHECK(gw2::PresentationText(0, true) == "guest Immediate | host Immediate (tearing allowed)");
  CHECK(gw2::PresentationText(240, false) == "guest Immediate | host VBlank (VSync On)");
  CHECK(gw2::PresentationText(60, false) == "guest VBlank | host VBlank (VSync On)");
  CHECK(gw2::PresentationText(60, true) == "guest VBlank | host Immediate (tearing allowed)");
}

void TestPrivacy() {
  std::printf("absolute user paths are reduced to file names\n");
  gw2::StartupDiagnostics diagnostics = Sample();
  diagnostics.game.executable = "C:\\Users\\Saulo\\Documents\\Games\\default.xex";
  const std::string text = Join(gw2::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "default.xex"));
  CHECK(!Has(text, "C:\\Users"));
  CHECK(!Has(text, "Saulo"));
  CHECK(!Has(text, "Documents"));

  CHECK(gw2::FileNameOnly("C:\\Users\\Saulo\\Documents\\Games\\default.xex") == "default.xex");
  CHECK(gw2::FileNameOnly("/home/saulo/games/default.xex") == "default.xex");
  CHECK(gw2::FileNameOnly("default.xex") == "default.xex");
}

}  // namespace

int main() {
  TestIdentity();
  TestStartupBlock();
  TestSelectedWindowResolution();
  TestSelectedAndResolvedDiffer();
  TestFrameRatePolicy();
  TestPrivacy();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
