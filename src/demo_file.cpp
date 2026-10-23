#include "demo_file.h"

#include <algorithm>
#include <cstring>

namespace gw2::demo_file {
namespace {

constexpr uint8_t kMagic[8] = {'G', 'W', '2', 'D', 'E', 'M', 'O', 0x1A};

class Writer {
 public:
  void Bytes(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out_.insert(out_.end(), p, p + size);
  }
  void U8(uint8_t v) { out_.push_back(v); }
  void U32(uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(uint8_t(v >> (8 * i)));
  }
  void U64(uint64_t v) {
    for (int i = 0; i < 8; ++i) out_.push_back(uint8_t(v >> (8 * i)));
  }
  void String(const std::string& s) {
    const size_t n = std::min(s.size(), kMaxString);
    U8(uint8_t(n));
    Bytes(s.data(), n);
  }
  std::vector<uint8_t> Take() { return std::move(out_); }

 private:
  std::vector<uint8_t> out_;
};

// Every read is bounds-checked; after the first failure every later read fails too.
class Reader {
 public:
  explicit Reader(std::span<const uint8_t> in) : in_(in) {}
  bool ok() const { return ok_; }
  bool AtEnd() const { return ok_ && at_ == in_.size(); }
  size_t left() const { return ok_ ? in_.size() - at_ : 0; }

  bool Bytes(void* out, size_t size) {
    if (!ok_ || size > in_.size() - at_) return ok_ = false;
    std::memcpy(out, in_.data() + at_, size);
    at_ += size;
    return true;
  }
  uint8_t U8() {
    uint8_t v = 0;
    Bytes(&v, 1);
    return v;
  }
  uint32_t U32() {
    uint8_t b[4] = {};
    Bytes(b, 4);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
  }
  uint64_t U64() {
    const uint64_t lo = U32();
    return lo | uint64_t(U32()) << 32;
  }
  std::string String() {
    const size_t n = U8();
    if (n > kMaxString) {
      ok_ = false;
      return {};
    }
    std::string s(n, '\0');
    Bytes(s.data(), n);
    return s;
  }
  // A count of items of `item_size` bytes, no more than `limit` and no more than the bytes left could hold.
  uint32_t Count(uint32_t limit, size_t item_size) {
    const uint32_t n = U32();
    if (!ok_ || n > limit || size_t(n) * item_size > left()) {
      ok_ = false;
      return 0;
    }
    return n;
  }
  void Fail() { ok_ = false; }

 private:
  std::span<const uint8_t> in_;
  size_t at_ = 0;
  bool ok_ = true;
};

}  // namespace

const Pads& Demo::PadsAt(uint32_t tick) const {
  static const Pads kNone{};
  const auto after = std::upper_bound(pad_changes.begin(), pad_changes.end(), tick,
                                      [](uint32_t t, const PadChange& c) { return t < c.tick; });
  return after == pad_changes.begin() ? kNone : std::prev(after)->pads;
}

void RecordPads(Demo& demo, uint32_t tick, const Pads& pads) {
  if (!demo.pad_changes.empty() && demo.pad_changes.back().pads == pads) return;
  if (!demo.pad_changes.empty() && demo.pad_changes.back().tick == tick) {
    demo.pad_changes.back().pads = pads;  // the same tick seen again: its latest pads
    return;
  }
  demo.pad_changes.push_back({tick, pads});
}

std::vector<uint8_t> Write(const Demo& demo) {
  Writer w;
  w.Bytes(kMagic, sizeof(kMagic));
  w.U32(kFormatVersion);
  w.U32(demo.replay_rules);
  w.String(demo.port_version);
  w.String(demo.game_sha256);
  w.String(demo.player_name);
  w.U32(demo.mode);
  w.U64(demo.final_score);
  w.U64(uint64_t(demo.recorded_at));
  w.U32(demo.flags);
  if (demo.flags & kFlagCustom) {
    w.String(demo.custom_name);
    w.String(demo.custom_author);
    w.Bytes(demo.custom_rules.data(), kCustomRulesSize);
  }
  w.Bytes(demo.twister.data(), kTwisterSize);
  w.U32(demo.rand_seed);
  w.U32(demo.particle_seed);
  w.Bytes(demo.input_state.data(), kInputStateSize);
  w.U32(uint32_t(demo.steps.size()));
  for (const uint32_t step : demo.steps) w.U32(step);
  w.U32(uint32_t(demo.pad_changes.size()));
  for (const PadChange& change : demo.pad_changes) {
    w.U32(change.tick);
    w.Bytes(change.pads.data(), kPadsSize);
  }
  w.U32(uint32_t(demo.shots.size()));
  for (const Shot& shot : demo.shots) {
    w.U32(shot.tick);
    w.U32(shot.x);
    w.U32(shot.y);
  }
  w.U32(uint32_t(demo.checkpoints.size()));
  for (const uint64_t score : demo.checkpoints) w.U64(score);
  return w.Take();
}

ReadResult Read(std::span<const uint8_t> bytes) {
  if (bytes.size() > kMaxFileSize) return {std::nullopt, ReadError::kDamaged};
  Reader r(bytes);
  uint8_t magic[sizeof(kMagic)] = {};
  if (!r.Bytes(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
    return {std::nullopt, ReadError::kNotADemo};
  }
  const uint32_t version = r.U32();
  if (r.ok() && version > kFormatVersion) return {std::nullopt, ReadError::kNewerFormat};
  if (r.ok() && version != 1 && version != kFormatVersion) return {std::nullopt, ReadError::kDamaged};
  Demo demo;
  demo.replay_rules = r.U32();
  if (r.ok() && demo.replay_rules != kReplayRules) return {std::nullopt, ReadError::kOtherRules};
  demo.port_version = r.String();
  demo.game_sha256 = r.String();
  demo.player_name = r.String();
  demo.mode = r.U32();
  demo.final_score = r.U64();
  demo.recorded_at = int64_t(r.U64());
  demo.flags = r.U32();
  if (r.ok() && (demo.flags & ~(kFlagCheats | kFlagCustom) || (version == 1 && (demo.flags & kFlagCustom)))) r.Fail();
  if (demo.flags & kFlagCustom) {
    demo.custom_name = r.String();
    demo.custom_author = r.String();
    r.Bytes(demo.custom_rules.data(), kCustomRulesSize);
  }
  r.Bytes(demo.twister.data(), kTwisterSize);
  demo.rand_seed = r.U32();
  demo.particle_seed = r.U32();
  r.Bytes(demo.input_state.data(), kInputStateSize);

  const uint32_t ticks = r.Count(kMaxTicks, 4);
  demo.steps.resize(ticks);
  for (uint32_t& step : demo.steps) step = r.U32();

  const uint32_t changes = r.Count(ticks, 4 + kPadsSize);
  demo.pad_changes.resize(changes);
  for (size_t i = 0; i < changes && r.ok(); ++i) {
    PadChange& change = demo.pad_changes[i];
    change.tick = r.U32();
    r.Bytes(change.pads.data(), kPadsSize);
    // In order, inside the run, and the first one at the start.
    if (change.tick >= ticks || (i == 0 && change.tick != 0) || (i > 0 && change.tick <= demo.pad_changes[i - 1].tick)) {
      r.Fail();
    }
  }
  if (r.ok() && ticks > 0 && changes == 0) r.Fail();

  const uint32_t shots = r.Count(ticks * kMaxShotsPerTick, 12);
  demo.shots.resize(shots);
  for (size_t i = 0; i < shots && r.ok(); ++i) {
    Shot& shot = demo.shots[i];
    shot.tick = r.U32();
    shot.x = r.U32();
    shot.y = r.U32();
    if (shot.tick >= ticks || (i > 0 && shot.tick < demo.shots[i - 1].tick)) r.Fail();
  }

  const uint32_t checkpoints = r.Count(ticks / kCheckpointTicks + 1, 8);
  demo.checkpoints.resize(checkpoints);
  for (uint64_t& score : demo.checkpoints) score = r.U64();

  if (r.ok() && demo.mode > 5) r.Fail();
  if (!r.AtEnd()) return {std::nullopt, ReadError::kDamaged};
  return {std::move(demo), ReadError::kNone};
}

}  // namespace gw2::demo_file
