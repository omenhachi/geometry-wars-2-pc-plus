#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gw2 {

struct ApplicationIdentity {
  std::string product;
  std::string version;
  std::string build_config;
  std::string architecture;
  std::string sdk;
  std::string backend;
};

struct SystemIdentity {
  std::string os;
  std::string cpu;
  int logical_processors = 0;
};

struct GraphicsAdapterIdentity {
  std::string selected;
  std::string resolved;
  int adapter = -1;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  bool resolved_known = false;
};

struct DisplayIdentity {
  int monitor = 0;
  int width = 0;
  int height = 0;
  int refresh_hz = 0;
  int scale_percent = 100;
  bool fullscreen = false;
  int configured_width = 0;
  int configured_height = 0;
  int output_width = 0;
  int output_height = 0;
  std::string aspect;
};

struct GameIdentity {
  uint32_t title_id = 0;
  std::string executable;
  uint64_t size = 0;
  std::string sha256;
  std::string fingerprint;
};

struct LauncherDisplayIdentity {
  std::string mode;
  std::string resolution;
  std::string aspect_ratio;
  std::string monitor;
};

struct LauncherGameplayIdentity {
  std::string frame_rate;
  std::string vsync;
  std::string frame_rate_in_title;
  std::string render_resolution_in_title;
  std::string frame_counter;
};

struct LauncherGraphicsIdentity {
  std::string graphics_card;
  std::string render_resolution;
  std::string internal_render_resolution;
  std::string msaa;
  std::string anti_aliasing;
  std::string texture_filtering;
  std::string bloom;
};

struct GameplayIdentity {
  std::string simulation;
  std::string interpolation;
  std::string present;
};

struct GraphicsIdentity {
  int render_percent = 100;
  int render_width = 0;
  int render_height = 0;
  int guest_width = 0;
  int guest_height = 0;
  int host_quarters = 0;
  int resolve_scale = 0;
  std::string msaa;
};

struct InputIdentity {
  std::string keyboard_mode;
  std::string controller;
};

struct StartupDiagnostics {
  LauncherDisplayIdentity launcher_display;
  LauncherGameplayIdentity launcher_gameplay;
  LauncherGraphicsIdentity launcher_graphics;
  SystemIdentity system;
  GraphicsAdapterIdentity gpu;
  DisplayIdentity display;
  GameIdentity game;
  GameplayIdentity gameplay;
  GraphicsIdentity graphics;
  InputIdentity input;
};

std::string FormatApplicationVersion(const ApplicationIdentity& identity);
std::string FormatBuildIdentity(const ApplicationIdentity& identity);
std::vector<std::string> FormatStartupDiagnostics(const StartupDiagnostics& diagnostics);

// File name without any folder part, so a diagnostic never carries an absolute path.
std::string FileNameOnly(std::string_view path);

std::string FrameRateText(int fps);
std::string SimulationRateText(int fps);
std::string InterpolationPolicyText(int fps);
std::string PresentModeText(int fps);
std::string PresentationText(int fps, bool vsync_off);

}  // namespace gw2
