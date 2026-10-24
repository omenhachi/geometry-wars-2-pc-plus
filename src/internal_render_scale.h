#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gw2 {

inline constexpr int kInternalRenderScaleNative = 100;
inline constexpr int kInternalRenderScalePerformance = 75;
inline constexpr int kInternalRenderScaleLowEnd = 50;

// The launcher's Internal render resolution, in percent of the render resolution. Anything but the two reduced
// choices draws at 100%, as patch 0036 does.
inline int NormalizeInternalRenderScale(int percent) {
  return percent == kInternalRenderScalePerformance || percent == kInternalRenderScaleLowEnd
             ? percent
             : kInternalRenderScaleNative;
}

// ReXGlue's split (TextureCache::GetConfigInternalRenderScale): the host render targets are host_quarters / 4 of the
// guest picture, while the EDRAM, resolves and textures keep the whole scale above it.
struct InternalRenderSplit {
  int host_quarters;
  int resolve_scale;
};

inline InternalRenderSplit SplitInternalRenderScale(int resolution_scale, int percent) {
  const int host_quarters = resolution_scale * NormalizeInternalRenderScale(percent) / 25;
  return {host_quarters, (host_quarters + 3) / 4};
}

// A side of the picture the host draws at: the canvas side times the Render resolution times the share.
inline int InternalRenderSize(int canvas_side, int resolution_scale, int percent) {
  return canvas_side * resolution_scale * NormalizeInternalRenderScale(percent) / 100;
}

// The render_target_path_d3d12 a reduced scale needs; none at 100% or when a path is already configured. The
// rasterizer-ordered path (Intel's default) draws into the EDRAM buffer and cannot use smaller render targets.
inline std::optional<std::string_view> RequiredRenderTargetPath(int percent, std::string_view configured) {
  if (NormalizeInternalRenderScale(percent) == kInternalRenderScaleNative || !configured.empty()) {
    return std::nullopt;
  }
  return "rtv";
}

// The launcher's Render path (gw2_render_path). Automatic leaves the choice to ReXGlue, which takes the slower
// rasterizer-ordered path on Intel (a 2021 driver workaround) and host render targets elsewhere; Fast always uses host
// render targets; Compatible always uses the rasterizer-ordered path, which draws at 100%.
enum class RenderPath { kAutomatic, kFast, kCompatible };
inline constexpr std::string_view kRenderPathAutomatic = "auto";
inline constexpr std::string_view kRenderPathFast = "fast";
inline constexpr std::string_view kRenderPathCompatible = "compatible";

inline RenderPath ParseRenderPath(std::string_view value) {
  if (value == kRenderPathFast) return RenderPath::kFast;
  if (value == kRenderPathCompatible) return RenderPath::kCompatible;
  return RenderPath::kAutomatic;
}

struct RenderPathPlan {
  std::optional<std::string_view> target_path;   // render_target_path_d3d12 to set, if any
  std::optional<int> internal_scale;              // gw2_internal_render_scale to use instead, if any
};

inline RenderPathPlan PlanRenderPath(RenderPath path, int percent, std::string_view configured) {
  switch (path) {
    case RenderPath::kFast:
      return {configured == "rtv" ? std::nullopt : std::optional<std::string_view>("rtv"), std::nullopt};
    case RenderPath::kCompatible:
      return {configured == "rov" ? std::nullopt : std::optional<std::string_view>("rov"),
              NormalizeInternalRenderScale(percent) == kInternalRenderScaleNative
                  ? std::nullopt
                  : std::optional<int>(kInternalRenderScaleNative)};
    case RenderPath::kAutomatic:
      break;
  }
  return {RequiredRenderTargetPath(percent, configured), std::nullopt};
}

}  // namespace gw2
