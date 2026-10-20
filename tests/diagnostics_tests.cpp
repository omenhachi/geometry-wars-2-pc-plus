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
  diagnostics.gameplay.frame_rate = "144";
  diagnostics.gameplay.simulation = "60Hz";
  diagnostics.gameplay.interpolation = "Automatic, pending measurement";
  diagnostics.gameplay.vsync = "Off";
  diagnostics.gameplay.present = "Immediate";
  diagnostics.graphics.render_percent = 100;
  diagnostics.graphics.render_width = 1280;
  diagnostics.graphics.render_height = 720;
  diagnostics.graphics.guest_width = 1280;
  diagnostics.graphics.guest_height = 720;
  diagnostics.graphics.host_quarters = 4;
  diagnostics.graphics.resolve_scale = 1;
  diagnostics.graphics.msaa = "2x native";
  diagnostics.graphics.anti_aliasing = "none";
  diagnostics.graphics.filtering = "16x";
  diagnostics.graphics.bloom = "original";
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
  CHECK(Has(text, "Runtime OS: Windows 11 64-bit (build 22631)"));
  CHECK(Has(text, "Runtime CPU: Test CPU | logical processors=16"));
  CHECK(Has(text, "Runtime GPU: selected=Automatic | resolved=NVIDIA GeForce RTX 4070 | adapter=0 | "
                  "vendor/device=10DE/2786"));
  CHECK(Has(text, "Runtime display: 2560x1440@144Hz | scale=225% | monitor=1 | fullscreen | window=1280x720 | "
                  "output=2560x1440 | aspect=16:9"));
  CHECK(Has(text, "Runtime game: title_id=584108FF | default.xex | size=704512 | fingerprint=verified"));
  CHECK(Has(text, "Runtime game sha256: ABCDEF"));
  CHECK(Has(text, "Runtime gameplay: frame_rate=144 | simulation=60Hz | "
                  "interpolation=Automatic, pending measurement | vsync=Off | present=Immediate"));
  CHECK(Has(text, "Runtime graphics: render=100% 1280x720 | guest=1280x720 | host_targets=4/4 | resolve=1x | "
                  "msaa=2x native | aa=none | filtering=16x | bloom=original"));
  CHECK(Has(text, "Runtime input: keyboard_mode=Off | controller=Xbox One S Controller"));
}

void TestSelectedAndResolvedDiffer() {
  std::printf("selected and resolved GPU stay distinguishable\n");
  gw2::StartupDiagnostics diagnostics = Sample();
  diagnostics.gpu.selected = "Intel UHD 730 (adapter 1)";
  diagnostics.gpu.resolved = "NVIDIA GeForce RTX 4070";
  const std::string text = Join(gw2::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "selected=Intel UHD 730 (adapter 1)"));
  CHECK(Has(text, "resolved=NVIDIA GeForce RTX 4070"));

  diagnostics.gpu.resolved_known = false;
  CHECK(Has(Join(gw2::FormatStartupDiagnostics(diagnostics)), "resolved=unknown"));
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
  TestSelectedAndResolvedDiffer();
  TestFrameRatePolicy();
  TestPrivacy();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
