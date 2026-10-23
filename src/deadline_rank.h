#pragma once

// Deadline's spawn rank, read from the game for the optional HUD readout (deadline_rank_hooks.cpp).
//
// Deadline keeps one block at kBlockAddress: K, the weighted kills since the last wave (1 per wanderer, 2 per grunt,
// 4 per weaver or spinner, 8 per snake), G, the geom value collected, the rank, and a count of waves. At each wave
// (sub_8203DED0) the rank becomes the next wave's spawn budget:
//   target = K - trunc(G * -0.5) + 2, limit = trunc(rank * 1.5 + 5), new rank = rank > target ? rank : min(target, limit)
// then K and G clear. Bombs (sub_8203E4B0) and deaths (sub_8203E2B8) cut the rank and clear K and G too.

#include <cstdint>
#include <optional>

namespace gw2::deadline_rank {

inline constexpr uint32_t kBlockAddress = 0x8219DB48;
inline constexpr uint32_t kKOffset = 8;
inline constexpr uint32_t kGOffset = 12;
inline constexpr uint32_t kRankOffset = 16;
inline constexpr uint32_t kWaveOffset = 20;
inline constexpr uint32_t kDeadlineMode = 0;  // at gw2::king::kModeAddress

struct Block {
  int32_t k = 0;
  int32_t g = 0;
  int32_t rank = 0;
  int32_t waves = 0;
};

// The rank the next wave sets, with the game's own single-precision arithmetic.
int32_t NextRank(const Block& block);

// Watches the waves land, for the short "+12" after a wave raises the rank. Bomb and death cuts show no pop.
class WavePop {
 public:
  static constexpr double kSeconds = 1.6;

  void Observe(const Block& block, double now);
  // The gain being shown, while it shows.
  std::optional<int32_t> Gain(double now) const;
  // 1 as the pop appears, easing to 0 when it goes.
  float Strength(double now) const;
  void Reset() { seen_ = false; gain_ = 0; }

 private:
  bool seen_ = false;
  Block last_{};
  int32_t gain_ = 0;
  double start_ = 0;
};

}  // namespace gw2::deadline_rank
