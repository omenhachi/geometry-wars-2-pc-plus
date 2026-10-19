#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gw2 {

inline constexpr int kBaseDpi = 96;

// Unusable DPI and scale values fall back to 1 so callers never divide by zero.
inline float DpiScaleFromDpi(std::uint32_t dpi) {
  return dpi ? float(dpi) / float(kBaseDpi) : 1.0f;
}

inline float ValidDpiScale(float scale) {
  return scale > 0.0f ? scale : 1.0f;
}

inline int ScaleLogicalToPhysical(int logical, float scale) {
  return int(std::lround(double(logical) * double(ValidDpiScale(scale))));
}

inline int ScalePhysicalToLogical(int physical, float scale) {
  return int(std::lround(double(physical) / double(ValidDpiScale(scale))));
}

}  // namespace gw2
