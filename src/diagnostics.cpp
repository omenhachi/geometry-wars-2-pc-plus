#include "diagnostics.h"

#include <cstdio>

#include "frame_limiter.h"

namespace gw2 {
namespace {

std::string Hex(uint32_t value, int digits) {
  char text[16];
  std::snprintf(text, sizeof(text), "%0*X", digits, value);
  return text;
}

std::string DisplayText(const DisplayIdentity& display) {
  std::string text = std::to_string(display.width) + "x" + std::to_string(display.height);
  if (display.refresh_hz > 0) {
    text += "@" + std::to_string(display.refresh_hz) + "Hz";
  }
  text += " | scale=" + std::to_string(display.scale_percent) + "%";
  text += display.monitor > 0 ? " | monitor=" + std::to_string(display.monitor) : std::string(" | monitor=primary");
  text += display.fullscreen ? " | fullscreen" : " | windowed";
  if (display.configured_width > 0 && display.configured_height > 0) {
    text += " | window=" + std::to_string(display.configured_width) + "x" +
            std::to_string(display.configured_height);
  }
  if (display.output_width > 0 && display.output_height > 0) {
    text += " | output=" + std::to_string(display.output_width) + "x" + std::to_string(display.output_height);
  }
  if (!display.aspect.empty()) {
    text += " | aspect=" + display.aspect;
  }
  return text;
}

std::string GraphicsText(const GraphicsIdentity& graphics) {
  std::string text =
      "render=" + std::to_string(graphics.render_percent) + "% " + std::to_string(graphics.render_width) + "x" +
      std::to_string(graphics.render_height) + " | guest=" + std::to_string(graphics.guest_width) + "x" +
      std::to_string(graphics.guest_height) + " | host_targets=" + std::to_string(graphics.host_quarters) + "/4";
  if (graphics.resolve_scale > 0) {
    text += " | resolve=" + std::to_string(graphics.resolve_scale) + "x";
  }
  if (!graphics.msaa.empty()) {
    text += " | msaa=" + graphics.msaa;
  }
  return text;
}

}  // namespace

std::string FormatApplicationVersion(const ApplicationIdentity& identity) {
  return identity.product + " port " + identity.version;
}

std::string FormatBuildIdentity(const ApplicationIdentity& identity) {
  return "Build: " + identity.build_config + " " + identity.architecture + " | ReXGlue SDK " + identity.sdk + " | " +
         identity.backend;
}

std::vector<std::string> FormatStartupDiagnostics(const StartupDiagnostics& diagnostics) {
  std::vector<std::string> lines;
  lines.push_back("Runtime configuration:");
  lines.push_back("Launcher Display: Mode=" + diagnostics.launcher_display.mode +
                  " | Resolution=" + diagnostics.launcher_display.resolution +
                  " | Aspect Ratio=" + diagnostics.launcher_display.aspect_ratio +
                  " | Monitor=" + diagnostics.launcher_display.monitor);
  lines.push_back("Launcher Gameplay: Frame Rate=" + diagnostics.launcher_gameplay.frame_rate +
                  " | VSync=" + diagnostics.launcher_gameplay.vsync +
                  " | Frame rate in title=" + diagnostics.launcher_gameplay.frame_rate_in_title +
                  " | Render resolution in title=" + diagnostics.launcher_gameplay.render_resolution_in_title +
                  " | Frame counter=" + diagnostics.launcher_gameplay.frame_counter);
  lines.push_back("Launcher Graphics: Graphics Card=" + diagnostics.launcher_graphics.graphics_card +
                  " | Render Resolution=" + diagnostics.launcher_graphics.render_resolution +
                  " | Internal Render Resolution=" + diagnostics.launcher_graphics.internal_render_resolution +
                  " | MSAA=" + diagnostics.launcher_graphics.msaa +
                  " | Anti-aliasing=" + diagnostics.launcher_graphics.anti_aliasing +
                  " | Texture Filtering=" + diagnostics.launcher_graphics.texture_filtering +
                  " | Bloom=" + diagnostics.launcher_graphics.bloom);
  lines.push_back("Runtime OS: " + diagnostics.system.os);
  lines.push_back("Runtime CPU: " + diagnostics.system.cpu +
                  " | logical processors=" + std::to_string(diagnostics.system.logical_processors));

  std::string gpu = "Runtime GPU: selected=" + diagnostics.gpu.selected + " | resolved=";
  if (diagnostics.gpu.resolved_known && !diagnostics.gpu.resolved.empty()) {
    gpu += diagnostics.gpu.resolved;
  } else {
    gpu += "unknown";
  }
  if (diagnostics.gpu.adapter >= 0) {
    gpu += " | adapter=" + std::to_string(diagnostics.gpu.adapter);
  }
  if (diagnostics.gpu.vendor_id != 0 || diagnostics.gpu.device_id != 0) {
    gpu += " | vendor/device=" + Hex(diagnostics.gpu.vendor_id, 4) + "/" + Hex(diagnostics.gpu.device_id, 4);
  }
  lines.push_back(std::move(gpu));

  lines.push_back("Runtime display: " + DisplayText(diagnostics.display));

  std::string game = "Runtime game: title_id=" + Hex(diagnostics.game.title_id, 8) + " | " +
                     (diagnostics.game.executable.empty() ? std::string("unknown")
                                                          : FileNameOnly(diagnostics.game.executable));
  if (diagnostics.game.size > 0) {
    game += " | size=" + std::to_string(diagnostics.game.size);
  }
  if (!diagnostics.game.fingerprint.empty()) {
    game += " | fingerprint=" + diagnostics.game.fingerprint;
  }
  lines.push_back(std::move(game));
  if (!diagnostics.game.sha256.empty()) {
    lines.push_back("Runtime game sha256: " + diagnostics.game.sha256);
  }

  std::string timing = "Runtime timing: simulation=" + diagnostics.gameplay.simulation +
                       " | interpolation=" + diagnostics.gameplay.interpolation +
                       " | present=" + diagnostics.gameplay.present;
  lines.push_back(std::move(timing));

  lines.push_back("Runtime renderer: " + GraphicsText(diagnostics.graphics));

  std::string input = "Runtime input: keyboard_mode=" + diagnostics.input.keyboard_mode;
  if (!diagnostics.input.controller.empty()) {
    input += " | controller=" + diagnostics.input.controller;
  }
  lines.push_back(std::move(input));
  return lines;
}

std::string FileNameOnly(std::string_view path) {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
    --end;
  }
  const size_t separator = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
  if (separator == std::string_view::npos) {
    return std::string(path.substr(0, end));
  }
  return std::string(path.substr(separator + 1, end - separator - 1));
}

std::string FrameRateText(int fps) {
  return fps == kFrameRateUnlimited ? "Unlimited" : std::to_string(fps);
}

std::string SimulationRateText(int fps) {
  if (GatesSimulation(fps)) {
    return "60Hz";
  }
  return std::to_string(fps == kFrameRateUnlimited ? kFrameRateDefault : fps) + "Hz";
}

std::string InterpolationPolicyText(int fps) {
  return IsHighFrameRate(fps) ? "Automatic, pending measurement" : "Inactive";
}

std::string PresentModeText(int fps) {
  return PresentModeFor(fps) == PresentMode::kImmediate ? "Immediate" : "VBlank";
}

std::string PresentationText(int fps, bool vsync_off) {
  std::string text = PresentModeFor(fps) == PresentMode::kImmediate ? "guest Immediate" : "guest VBlank";
  text += vsync_off ? " | host Immediate (tearing allowed)" : " | host VBlank (VSync On)";
  return text;
}

}  // namespace gw2
