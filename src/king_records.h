#pragma once

#include <cstdint>

namespace gw2::king {

inline constexpr uint32_t kModeAddress = 0x8219D254;
inline constexpr uint32_t kKingMode = 5;
inline constexpr uint32_t kFullRadiusAddress = 0x8219EE34;
inline constexpr uint32_t kZoneCenterXOffset = 4;
inline constexpr uint32_t kZoneCenterYOffset = 8;
inline constexpr uint32_t kZoneRadiusOffset = 32;
inline constexpr uint32_t kZoneStateOffset = 140;
inline constexpr uint32_t kZoneShrinkingState = 3;

}  // namespace gw2::king
