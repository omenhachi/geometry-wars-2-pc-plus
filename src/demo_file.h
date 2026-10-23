#pragma once

// A recorded single-player run (.gw2demo), and its file format. Pure, so it is unit tested; demo.cpp records and plays.
//
// A demo holds only numbers: the round's random states and input state at the start, then each tick's step and pad
// slots, and the aim of each shot. Nothing in it is run or used as an address. Reading is strict: every count is
// capped, every field bounded, the file must end exactly where its contents do, and anything else is refused.
//
// Layout, all integers little-endian:
//   "GW2DEMO" 0x1A, u32 format version, u32 replay rules
//   string port version, string game SHA-256, string player name   (u8 length, then that many bytes)
//   u32 mode, u64 final score, i64 recorded at (Unix seconds), u32 flags (bit 0: recorded with cheats on, bit 1: a
//   custom mode)
//   format 2, a custom mode only: string name, string author, 24 bytes of its rules (custom_mode_rules.h)
//   2500 bytes twister, u32 rand seed, u32 particle seed, 16 bytes input state
//   u32 ticks, then ticks x u32 step
//   u32 pad changes, then each: u32 tick, 80 bytes pads (the pads hold from that tick until the next change)
//   u32 shots, then each: u32 tick, u32 x, u32 y (float bits)
//   u32 checkpoints, then each: u64 score (one per kCheckpointTicks)

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gw2::demo_file {

inline constexpr uint32_t kFormatVersion = 2;  // 1 had no custom modes, and still reads
// Bumped whenever a change to the port could make an older demo play differently. Demos with other rules are refused.
inline constexpr uint32_t kReplayRules = 1;

inline constexpr uint32_t kTwisterSize = 2500;
inline constexpr uint32_t kInputStateSize = 16;
inline constexpr uint32_t kMaxPads = 4;
inline constexpr uint32_t kPadSize = 20;
inline constexpr uint32_t kPadsSize = kMaxPads * kPadSize;
inline constexpr uint32_t kCheckpointTicks = 60;

// Limits: an hour of play at 60 ticks a second, and far more shots than a ship can fire.
inline constexpr uint32_t kMaxTicks = 60 * 60 * 60;
inline constexpr uint32_t kMaxShotsPerTick = 8;
inline constexpr size_t kMaxString = 64;
inline constexpr size_t kMaxFileSize = 64u << 20;
inline constexpr size_t kCustomRulesSize = 24;

using Pads = std::array<uint8_t, kPadsSize>;

struct PadChange {
  uint32_t tick = 0;
  Pads pads{};
};

struct Shot {
  uint32_t tick = 0;
  uint32_t x = 0;
  uint32_t y = 0;
};

struct Demo {
  uint32_t replay_rules = kReplayRules;
  std::string port_version;
  std::string game_sha256;
  std::string player_name;
  uint32_t mode = 0;
  uint64_t final_score = 0;
  int64_t recorded_at = 0;
  uint32_t flags = 0;
  // A custom mode's run (kFlagCustom): its name and author, and its rules as custom_mode::Write makes them.
  std::string custom_name;
  std::string custom_author;
  std::array<uint8_t, kCustomRulesSize> custom_rules{};
  std::array<uint8_t, kTwisterSize> twister{};
  uint32_t rand_seed = 0;
  uint32_t particle_seed = 0;
  std::array<uint8_t, kInputStateSize> input_state{};
  std::vector<uint32_t> steps;          // one per tick
  std::vector<PadChange> pad_changes;   // ascending ticks, the first at tick 0
  std::vector<Shot> shots;              // ascending ticks
  std::vector<uint64_t> checkpoints;    // the score after every kCheckpointTicks ticks

  uint32_t ticks() const { return uint32_t(steps.size()); }
  // The pads during `tick` (the latest change at or before it).
  const Pads& PadsAt(uint32_t tick) const;
};

inline constexpr uint32_t kFlagCheats = 1;
inline constexpr uint32_t kFlagCustom = 2;

std::vector<uint8_t> Write(const Demo& demo);

enum class ReadError { kNone, kNotADemo, kNewerFormat, kOtherRules, kDamaged };
struct ReadResult {
  std::optional<Demo> demo;
  ReadError error = ReadError::kNone;
};
ReadResult Read(std::span<const uint8_t> bytes);

// The pads seen at `tick` (ticks in order): a change is kept only when they differ from the last one.
void RecordPads(Demo& demo, uint32_t tick, const Pads& pads);

}  // namespace gw2::demo_file
