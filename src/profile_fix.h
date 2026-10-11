#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

namespace gw2 {

struct TitleSettingsSummary {
  uint32_t saved = 0;
  uint32_t defaulted = 0;
};

inline uint32_t LoadBE32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

inline void StoreBE32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(base + addr, &v, sizeof(v));
}

// The console answers a title-specific setting that was never saved with XSOURCE_DEFAULT and an
// empty value; the game then starts a new profile. ReXGlue answers XSOURCE_NO_VALUE, which the game
// treats as a failed load.
inline TitleSettingsSummary ReportUnsavedTitleSettingsAsDefault(uint8_t* base, uint32_t buffer) {
  constexpr uint32_t kHeaderSize = 8;
  constexpr uint32_t kSettingSize = 0x28;
  constexpr uint32_t kMaxSettings = 32;
  constexpr uint32_t kSourceNoValue = 0;
  constexpr uint32_t kSourceDefault = 1;
  constexpr uint32_t kSourceTitle = 2;
  constexpr uint8_t kTypeBinary = 6;

  TitleSettingsSummary summary;
  const uint32_t count = LoadBE32(base, buffer);
  const uint32_t settings = LoadBE32(base, buffer + 4);
  if (count > kMaxSettings || settings != buffer + kHeaderSize) return summary;

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = settings + i * kSettingSize;
    const uint32_t source = LoadBE32(base, entry);
    const bool title_specific = (LoadBE32(base, entry + 16) & 0x3F00) == 0x3F00;
    if (!title_specific) continue;
    if (source == kSourceTitle) {
      ++summary.saved;
    } else if (source == kSourceNoValue) {
      StoreBE32(base, entry, kSourceDefault);
      base[entry + 24] = kTypeBinary;
      StoreBE32(base, entry + 32, 0);
      StoreBE32(base, entry + 36, 0);
      ++summary.defaulted;
    }
  }
  return summary;
}

}  // namespace gw2
