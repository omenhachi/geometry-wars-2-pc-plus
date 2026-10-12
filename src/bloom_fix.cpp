#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "bloom_rules.h"
#include "guest_memory.h"

REXCVAR_DEFINE_STRING(gw2_bloom, "original", "Geometry Wars", "Bloom glow: original, reduced or off");

namespace {

// Value fields of Render.BloomBg, Render.BloomFg and MapLight.Bloom.
constexpr std::array<uint32_t, 3> kBloomGainAddresses = {0x821B3A30, 0x821B3A64, 0x821B3D94};

float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(gw2::LoadBE32(base, addr));
}

void StoreFloat(uint8_t* base, uint32_t addr, float value) {
  gw2::StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

}  // namespace

// The scale applies to this call only, so the stored tuning values are the game's again afterwards.
REX_EXTERN(__imp__sub_820653C8);
REX_HOOK_RAW(sub_820653C8) {
  const float scale = gw2::bloom::Scale(gw2::bloom::ParseMode(REXCVAR_GET(gw2_bloom)));
  if (scale == 1.0f) {
    __imp__sub_820653C8(ctx, base);
    return;
  }

  std::array<float, kBloomGainAddresses.size()> original{};
  for (std::size_t i = 0; i < kBloomGainAddresses.size(); ++i) {
    original[i] = LoadFloat(base, kBloomGainAddresses[i]);
    StoreFloat(base, kBloomGainAddresses[i], original[i] * scale);
  }
  __imp__sub_820653C8(ctx, base);
  for (std::size_t i = 0; i < kBloomGainAddresses.size(); ++i) {
    StoreFloat(base, kBloomGainAddresses[i], original[i]);
  }
}
