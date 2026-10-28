#pragma once

// A custom mode (custom_modes.h): one of the game's six modes with its own rules, read from a mode.toml. Pure, so it
// is unit tested.
//
// A mode file names a base mode and changes some of its rules; everything it leaves out stays as the base mode has it.
// Reading is strict: a key it doesn't know, a value of the wrong type or a value out of range is reported with its
// line, so a typo never silently does nothing. A value outside its range is brought into it, with a warning.
//
//   format = 1
//   name = "Pure Pacifism"
//   author = "Light"
//   description = "No gates. Score is how long you last."
//   base = "pacifism"
//
//   [rules]
//   gates = false
//   score = "time"
//
// A mode is known by its id, a hash of its base and rules only, so fixing a typo in its name keeps its best.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gw2::custom_mode {

inline constexpr int64_t kFormat = 1;
inline constexpr uint32_t kModeCount = 6;
// The game's own names, in its mode order (the id at 0x8219D254).
inline constexpr std::array<std::string_view, kModeCount> kBaseNames = {"deadline", "evolved", "sequence",
                                                                         "waves",    "pacifism", "king"};

enum class Score : uint8_t { kPoints, kTime };

inline constexpr int32_t kMaxLives = 99;
inline constexpr int32_t kMaxBombs = 99;
inline constexpr float kMinTimeLimit = 10.0f;
inline constexpr float kMaxTimeLimit = 3600.0f;
inline constexpr float kMinSpeed = 0.5f;
inline constexpr float kMaxSpeed = 1.5f;
inline constexpr size_t kMaxBonusBombs = 8;
inline constexpr uint64_t kMaxBonusScore = 1'000'000'000'000;
inline constexpr uint32_t kSequenceBase = 2;
inline constexpr int32_t kSequenceLevels = 20;

struct Rules {
  uint32_t base = 0;
  std::optional<int32_t> lives;       // ships in all: 1 is a single ship, with none to spare
  std::optional<int32_t> bombs;       // at the start of the round
  std::optional<float> time_limit;    // seconds; the round ends when it runs out
  float game_speed = 1.0f;
  bool gates = true;
  bool infinite_bombs = false;
  Score score = Score::kPoints;
  bool extra_lives = true;            // the game's own extra lives at score milestones
  bool extra_bombs = true;            // the game's own extra bombs at score milestones
  std::vector<uint64_t> bonus_bombs_at;  // a bomb each time the score passes one of these, ascending
  std::optional<int32_t> level;       // Sequence only: just this level (1-20), and the round ends once it is cleared
  // Read from a replay made before lives meant ships in all (rule bytes version 1): `lives` there was spare ships.
  bool legacy_spare_lives = false;

  bool operator==(const Rules&) const = default;

  // The game's own count of spare ships for the start of the round.
  int32_t SpareLives() const { return lives ? (legacy_spare_lives ? *lives : std::max(*lives - 1, 0)) : 0; }
};

struct Mode {
  std::string name;
  std::string author;
  std::string description;
  Rules rules;
};

struct Message {
  uint32_t line = 0;  // 0 when it is about the whole file
  std::string text;
};

struct ParseResult {
  std::optional<Mode> mode;            // empty when the file can't be used
  std::vector<Message> errors;         // why it can't
  std::vector<Message> warnings;       // it can, but something was changed (a value brought into range)
  bool newer_format = false;           // made for a newer PC Plus
};

ParseResult Parse(std::string_view toml_text);

// The id: 16 hex digits from the base and the rules.
std::string Id(const Rules& rules);

// Short labels for the list's chips ("1 life", "No gates", "Score: time"), in a fixed order. Only rules that differ
// from the base mode's own are shown.
std::vector<std::string> Chips(const Rules& rules);

// The rules as bytes, for a replay of a custom run (demo_file.h), and back. Reading is strict. Version 1 (24 bytes) is
// written while no rule needs more, so a mode's id stays the same; version 2 adds the newer rules.
std::vector<uint8_t> Write(const Rules& rules);
std::optional<Rules> Read(std::span<const uint8_t> bytes);
inline constexpr size_t kRulesBytes = 24;
inline constexpr size_t kMaxRulesBytes = kRulesBytes + 1 + kMaxBonusBombs * 8;

// A score as a short label: 100K, 1M, 2.5M.
std::string ShortScore(uint64_t score);

// "did you mean": the known word nearest to `word`, when it is close enough.
std::optional<std::string_view> Nearest(std::string_view word, std::span<const std::string_view> known);

// A time score: hundredths of a second survived.
std::string TimeText(uint64_t hundredths);

}  // namespace gw2::custom_mode
