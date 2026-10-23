#include "deadline_rank.h"

#include <algorithm>

namespace gw2::deadline_rank {

int32_t NextRank(const Block& block) {
  const int32_t target = block.k - int32_t(float(block.g) * -0.5f) + 2;
  const int32_t limit = int32_t(float(block.rank) * 1.5f + 5.0f);
  return block.rank > target ? block.rank : std::min(target, limit);
}

void WavePop::Observe(const Block& block, double now) {
  if (seen_ && block.waves != last_.waves && block.rank > last_.rank) {
    gain_ = block.rank - last_.rank;
    start_ = now;
  }
  if (block.waves < last_.waves) gain_ = 0;  // a new round
  last_ = block;
  seen_ = true;
}

std::optional<int32_t> WavePop::Gain(double now) const {
  if (gain_ <= 0 || now < start_ || now - start_ >= kSeconds) return std::nullopt;
  return gain_;
}

float WavePop::Strength(double now) const {
  if (!Gain(now)) return 0.0f;
  const float t = float((now - start_) / kSeconds);
  return (1.0f - t) * (1.0f - t);
}

}  // namespace gw2::deadline_rank
