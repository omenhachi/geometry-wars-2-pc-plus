#pragma once

#include <cstdint>

#include "cheat_rules.h"
#include "guest_memory.h"

namespace gw2::players {

inline constexpr uint32_t kRecordCount = 4;
inline constexpr uint32_t kPlayerCountAddress = 0x8219D064;
inline constexpr uint32_t kFirstRecordAddress = 0x8219D068;
inline constexpr uint32_t kRecordStride = 0x78;
inline constexpr uint32_t kLivesOffset = 48;
inline constexpr uint32_t kBombsOffset = 52;
inline constexpr uint32_t kShieldHitCountOffset = 104;
inline constexpr uint32_t kCenterXOffset = 4;
inline constexpr uint32_t kCenterYOffset = 8;
inline constexpr uint32_t kRadiusOffset = 32;
inline constexpr uint32_t kListSentinelAddress = 0x821813A8;
inline constexpr uint32_t kFlagsOffset = 36;
inline constexpr uint32_t kRemovedBit = 0x1;

constexpr uint32_t LivesAddress(uint32_t index) {
  return index < kRecordCount ? kFirstRecordAddress + index * kRecordStride + kLivesOffset : 0;
}

constexpr uint32_t BombsAddress(uint32_t index) {
  return index < kRecordCount ? kFirstRecordAddress + index * kRecordStride + kBombsOffset : 0;
}

constexpr uint32_t ShieldHitCountAddress(uint32_t index) {
  return index < kRecordCount ? kFirstRecordAddress + index * kRecordStride + kShieldHitCountOffset : 0;
}

inline bool IsPlayer(const uint8_t* base, uint32_t object) {
  return object != 0 && gw2::LoadBE32(base, object) == cheats::kPlayerVtable;
}

}  // namespace gw2::players
