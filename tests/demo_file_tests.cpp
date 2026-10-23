// Demo file tests: a demo survives writing and reading, and reading refuses anything damaged, truncated, padded,
// oversized, out of order, from a newer format or made under other replay rules. Random and mutated bytes never crash
// it. Pure; no game or runtime.

#include <cstdio>
#include <cstring>
#include <random>

#include "demo_file.h"

using namespace gw2::demo_file;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

Pads PadsWith(uint8_t value) {
  Pads pads{};
  pads.fill(value);
  return pads;
}

Demo Sample() {
  Demo demo;
  demo.port_version = "0.9.0";
  demo.game_sha256 = "6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88";
  demo.player_name = "Omenhachi";
  demo.mode = 4;
  demo.final_score = 72826275;
  demo.recorded_at = 1792720800;
  for (size_t i = 0; i < kTwisterSize; ++i) demo.twister[i] = uint8_t(i * 7);
  demo.rand_seed = 0x12345678;
  demo.particle_seed = 0x9ABCDEF0;
  demo.input_state[3] = 0x10;
  for (uint32_t t = 0; t < 300; ++t) {
    demo.steps.push_back(0x3C888889);
    RecordPads(demo, t, PadsWith(uint8_t(t / 50)));  // six distinct pad states
    if (t % 7 == 0) demo.shots.push_back({t, 0x3F800000, 0xBF800000});
    if ((t + 1) % kCheckpointTicks == 0) demo.checkpoints.push_back(uint64_t(t) * 100);
  }
  return demo;
}

bool Same(const Demo& a, const Demo& b) {
  if (a.pad_changes.size() != b.pad_changes.size() || a.shots.size() != b.shots.size()) return false;
  for (size_t i = 0; i < a.pad_changes.size(); ++i) {
    if (a.pad_changes[i].tick != b.pad_changes[i].tick || a.pad_changes[i].pads != b.pad_changes[i].pads) return false;
  }
  for (size_t i = 0; i < a.shots.size(); ++i) {
    if (a.shots[i].tick != b.shots[i].tick || a.shots[i].x != b.shots[i].x || a.shots[i].y != b.shots[i].y) return false;
  }
  return a.replay_rules == b.replay_rules && a.port_version == b.port_version && a.game_sha256 == b.game_sha256 &&
         a.player_name == b.player_name && a.mode == b.mode && a.final_score == b.final_score &&
         a.recorded_at == b.recorded_at && a.flags == b.flags && a.twister == b.twister && a.rand_seed == b.rand_seed &&
         a.particle_seed == b.particle_seed && a.input_state == b.input_state && a.steps == b.steps &&
         a.checkpoints == b.checkpoints && a.custom_name == b.custom_name && a.custom_author == b.custom_author &&
         a.custom_rules == b.custom_rules;
}

void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(v >> (8 * i));
}

void TestRoundTrip() {
  const Demo demo = Sample();
  CHECK(demo.pad_changes.size() == 6);  // only the changes are kept
  const std::vector<uint8_t> bytes = Write(demo);
  const ReadResult read = Read(bytes);
  CHECK(read.error == ReadError::kNone);
  CHECK(read.demo && Same(*read.demo, demo));
  // An empty run is a valid demo too.
  Demo empty;
  const ReadResult read_empty = Read(Write(empty));
  CHECK(read_empty.demo && read_empty.demo->ticks() == 0);
}

void TestPadsAt() {
  const Demo demo = Sample();
  CHECK(demo.PadsAt(0) == PadsWith(0));
  CHECK(demo.PadsAt(49) == PadsWith(0));
  CHECK(demo.PadsAt(50) == PadsWith(1));
  CHECK(demo.PadsAt(299) == PadsWith(5));
  CHECK(demo.PadsAt(100000) == PadsWith(5));
  // The same tick recorded twice keeps its latest pads.
  Demo again;
  RecordPads(again, 0, PadsWith(1));
  RecordPads(again, 0, PadsWith(2));
  CHECK(again.pad_changes.size() == 1 && again.PadsAt(0) == PadsWith(2));
}

void TestRefusals() {
  const std::vector<uint8_t> good = Write(Sample());
  // Every truncation is refused.
  bool all_truncations_refused = true;
  for (size_t n = 0; n < good.size(); ++n) {
    if (Read(std::span(good.data(), n)).demo) all_truncations_refused = false;
  }
  CHECK(all_truncations_refused);
  // So are trailing bytes.
  std::vector<uint8_t> padded = good;
  padded.push_back(0);
  CHECK(!Read(padded).demo && Read(padded).error == ReadError::kDamaged);
  // Not a demo at all.
  std::vector<uint8_t> text(good.size(), 'A');
  CHECK(Read(text).error == ReadError::kNotADemo);
  CHECK(Read({}).error == ReadError::kNotADemo);
  // A newer format, and other replay rules, are named so the player can be told.
  std::vector<uint8_t> newer = good;
  PutU32(newer, 8, kFormatVersion + 1);
  CHECK(Read(newer).error == ReadError::kNewerFormat);
  std::vector<uint8_t> other_rules = good;
  PutU32(other_rules, 12, kReplayRules + 1);
  CHECK(Read(other_rules).error == ReadError::kOtherRules);
}

void TestHostileCounts() {
  Demo demo = Sample();
  // Shots out of order, or after the run ends.
  Demo bad_shots = demo;
  std::swap(bad_shots.shots[0], bad_shots.shots[1]);
  CHECK(!Read(Write(bad_shots)).demo);
  bad_shots = demo;
  bad_shots.shots.back().tick = demo.ticks();
  CHECK(!Read(Write(bad_shots)).demo);
  // Pad changes out of order, not starting at tick 0, or past the end.
  Demo bad_pads = demo;
  std::swap(bad_pads.pad_changes[1], bad_pads.pad_changes[2]);
  CHECK(!Read(Write(bad_pads)).demo);
  bad_pads = demo;
  bad_pads.pad_changes[0].tick = 1;
  CHECK(!Read(Write(bad_pads)).demo);
  bad_pads = demo;
  bad_pads.pad_changes.back().tick = demo.ticks() + 5;
  CHECK(!Read(Write(bad_pads)).demo);
  // A run with ticks needs its pads.
  bad_pads = demo;
  bad_pads.pad_changes.clear();
  CHECK(!Read(Write(bad_pads)).demo);
  // A mode the game does not have.
  Demo bad_mode = demo;
  bad_mode.mode = 6;
  CHECK(!Read(Write(bad_mode)).demo);
  // A huge tick count with no data behind it is refused before anything is allocated for it.
  std::vector<uint8_t> bytes = Write(Demo{});
  const size_t ticks_at = bytes.size() - 16;  // u32 ticks, pad changes, shots, checkpoints, all zero
  PutU32(bytes, ticks_at, 0xFFFFFFFF);
  CHECK(!Read(bytes).demo);
  PutU32(bytes, ticks_at, kMaxTicks);
  CHECK(!Read(bytes).demo);
  // Too many shots for the ticks.
  Demo shot_spam = demo;
  shot_spam.shots.clear();
  for (uint32_t t = 0; t < demo.ticks(); ++t) {
    for (uint32_t i = 0; i <= kMaxShotsPerTick; ++i) shot_spam.shots.push_back({t, 0, 0});
  }
  CHECK(!Read(Write(shot_spam)).demo);
  // A string longer than allowed is cut when written, so the file stays readable.
  Demo long_name = demo;
  long_name.player_name = std::string(500, 'x');
  const ReadResult read_long = Read(Write(long_name));
  CHECK(read_long.demo && read_long.demo->player_name.size() == kMaxString);
}

void TestFuzz() {
  const std::vector<uint8_t> good = Write(Sample());
  std::mt19937 random(1234);
  int accepted = 0;
  for (int round = 0; round < 3000; ++round) {
    std::vector<uint8_t> bytes = good;
    const int flips = 1 + int(random() % 8);
    for (int i = 0; i < flips; ++i) bytes[random() % bytes.size()] = uint8_t(random());
    const ReadResult read = Read(bytes);
    if (read.demo) ++accepted;  // a flip inside the data can still be a valid demo; it must just never crash
  }
  CHECK(accepted < 3000);
  for (int round = 0; round < 500; ++round) {
    std::vector<uint8_t> noise(random() % 4096);
    for (uint8_t& b : noise) b = uint8_t(random());
    CHECK(!Read(noise).demo);
  }
}

}  // namespace

// A custom mode's run carries its name, author and rules; a format 1 file (no custom block) still reads, and one that
// claims to be custom is refused.
void TestCustom() {
  Demo demo = Sample();
  demo.flags = kFlagCustom;
  demo.custom_name = "Pure Pacifism";
  demo.custom_author = "Light";
  for (size_t i = 0; i < kCustomRulesSize; ++i) demo.custom_rules[i] = uint8_t(i * 7);
  const auto bytes = Write(demo);
  const ReadResult read = Read(bytes);
  CHECK(read.error == ReadError::kNone && read.demo && Same(*read.demo, demo));

  Demo plain = Sample();
  auto v1 = Write(plain);
  PutU32(v1, 8, 1);
  const ReadResult old = Read(v1);
  CHECK(old.error == ReadError::kNone && old.demo && Same(*old.demo, plain));

  auto claims = bytes;
  PutU32(claims, 8, 1);
  CHECK(Read(claims).error == ReadError::kDamaged);

  Demo unknown = Sample();
  unknown.flags = 8;
  CHECK(Read(Write(unknown)).error == ReadError::kDamaged);
}

int main() {
  TestRoundTrip();
  TestCustom();
  TestPadsAt();
  TestRefusals();
  TestHostileCounts();
  TestFuzz();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
