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

}  // namespace gw2
