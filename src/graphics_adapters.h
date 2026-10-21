#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gw2 {

// A DXGI adapter that can create a Direct3D 12 device. `index` is the one ReXGlue's d3d12_adapter takes.
struct GraphicsAdapter {
  int index;
  std::string name;
};

// Windows: the hardware adapters, in the EnumAdapters1 order d3d12_adapter counts. Empty elsewhere.
std::vector<GraphicsAdapter> ListGraphicsAdapters();

struct GraphicsAdapterDetails {
  int index = -1;
  std::string name;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  bool known = false;
};

// Windows: the adapter ReXGlue's d3d12_adapter selection will use for `configured_index` (-1 automatic, -2 WARP).
// Mirrors the runtime's enumeration, so Automatic reports the GPU that is actually picked.
GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index);

inline constexpr char kGraphicsAdapterAuto[] = "auto";

// The adapter index a gw2_graphics_adapter value names; empty for Automatic and for anything that is not an index.
inline std::optional<int> ParseGraphicsAdapter(std::string_view setting) {
  int index = 0;
  const char* end = setting.data() + setting.size();
  const auto [stop, error] = std::from_chars(setting.data(), end, index);
  if (setting.empty() || error != std::errc() || stop != end || index < 0) {
    return std::nullopt;
  }
  return index;
}

// The value for d3d12_adapter: -1 (automatic) unless the setting names an adapter that is present.
inline int ResolveGraphicsAdapter(std::string_view setting, const std::vector<GraphicsAdapter>& adapters) {
  const std::optional<int> index = ParseGraphicsAdapter(setting);
  for (const GraphicsAdapter& adapter : adapters) {
    if (index && adapter.index == *index) {
      return adapter.index;
    }
  }
  return -1;
}

}  // namespace gw2
