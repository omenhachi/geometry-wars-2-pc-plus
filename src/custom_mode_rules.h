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

struct Rules {
  uint32_t base = 0;
  std::optional<int32_t> lives;       // at the start of the round
  std::optional<int32_t> bombs;       // at the start of the round
  std::optional<float> time_limit;    // seconds; the round ends when it runs out
  float game_speed = 1.0f;
  bool gates = true;
  bool infinite_bombs = false;
  Score score = Score::kPoints;

  bool operator==(const Rules&) const = default;
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

// The rules as bytes, for a replay of a custom run (demo_file.h), and back. Reading is strict.
std::vector<uint8_t> Write(const Rules& rules);
std::optional<Rules> Read(std::span<const uint8_t> bytes);
inline constexpr size_t kRulesBytes = 24;

// "did you mean": the known word nearest to `word`, when it is close enough.
std::optional<std::string_view> Nearest(std::string_view word, std::span<const std::string_view> known);

// A time score: hundredths of a second survived.
std::string TimeText(uint64_t hundredths);

}  // namespace gw2::custom_mode
