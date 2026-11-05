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
// Format 2 adds tables beside [rules] that tune the game itself, like [enemies] (which ones spawn). A format 1 file
// reads exactly as before.
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

inline constexpr int64_t kFormat = 2;
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
inline constexpr uint32_t kWavesBase = 3;

// The enemies a mode can turn off, in [enemies], by their names there.
// New ones only ever go at the end: each one's bit is in replays and mode ids.
enum class Enemy : uint8_t { kWanderer, kGrunt, kWeaver, kSpinner, kSnake, kRepulsar, kBlackHole, kRocket, kDuck, kCount };
inline constexpr size_t kEnemyCount = size_t(Enemy::kCount);
inline constexpr std::array<std::string_view, kEnemyCount> kEnemyNames = {
    "wanderer", "grunt", "weaver", "spinner", "snake", "repulsar", "black_hole", "rocket", "duck"};
inline constexpr uint16_t kAllEnemies = uint16_t((1u << kEnemyCount) - 1);
static_assert(kEnemyCount <= 16);

// The enemy the spawner (sub_8203BEE8) makes for its type r3, from each case's constructor and the enemy type it
// passes to the base constructor (sub_82047030, r5): 1 wanderer (18), 2 grunt (2), 3 weaver (6), 4 spinner (9),
// 5 black hole (3), 6 repulsar (11), 8 snake (13), 12 and 13 rocket (22; the game's sound names), 22 duck (33).
// Other types (gates, protons, 21's enemy 32...) aren't in [enemies].
constexpr std::optional<Enemy> EnemyForSpawn(uint32_t type) {
  switch (type) {
    case 1: return Enemy::kWanderer;
    case 2: return Enemy::kGrunt;
    case 3: return Enemy::kWeaver;
    case 4: return Enemy::kSpinner;
    case 5: return Enemy::kBlackHole;
    case 6: return Enemy::kRepulsar;
    case 8: return Enemy::kSnake;
    case 12:
    case 13: return Enemy::kRocket;
    case 22: return Enemy::kDuck;
    default: return std::nullopt;
  }
}

// [enemies.swap]: what an enemy can be swapped into, by name. The nine above except black_hole and rocket (their spawns
// read arguments the caller set for the enemy it asked for: a flag, a direction), and the game's unused enemies by
// spawn type, type_7 to type_25, except type_14 (it reads the caller's direction too) and the gate pair (16). The
// unused ones are the game's own leftovers, documented as untested.
struct SwapTarget {
  std::string_view name;
  uint8_t spawn;
};
inline constexpr std::array<SwapTarget, 20> kSwapTargets = {{
    {"wanderer", 1}, {"grunt", 2},    {"weaver", 3},   {"spinner", 4},  {"repulsar", 6},
    {"snake", 8},    {"duck", 22},    {"type_7", 7},   {"type_9", 9},   {"type_10", 10},
    {"type_11", 11}, {"type_15", 15}, {"type_17", 17}, {"type_18", 18}, {"type_19", 19},
    {"type_20", 20}, {"type_21", 21}, {"type_23", 23}, {"type_24", 24}, {"type_25", 25},
}};
constexpr bool IsSwapTarget(uint32_t spawn) {
  for (const SwapTarget& t : kSwapTargets) {
    if (t.spawn == spawn) return true;
  }
  return false;
}

// How the tuning tables behave. A replay keeps the version it was made with; if a tuning hook ever has to change how
// it plays, this goes up and the old behaviour stays for old replays, so the game-wide replay rules never need to.
inline constexpr uint8_t kTuningVersion = 1;

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
  // Format 2's tuning. Each one left as the game has it is not written, so it changes neither the bytes nor the id.
  uint16_t enemies_off = 0;           // a bit per Enemy that never spawns
  // [enemies.swap]: for each Enemy, the spawn type made in its place (0: none). One pass on the enemy the game asked
  // for: grunt = "weaver" with weaver = "grunt" trades them, it never follows on.
  std::array<uint8_t, kEnemyCount> swap_to{};
  uint8_t tuning_version = kTuningVersion;

  bool Swapped() const {
    return std::any_of(swap_to.begin(), swap_to.end(), [](uint8_t t) { return t != 0; });
  }
  bool Tuned() const { return enemies_off != 0 || Swapped(); }
  bool EnemyOff(Enemy e) const { return enemies_off & (1u << uint8_t(e)); }

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

// A mode as a mode.toml (format 2) that Parse reads back as the same mode: only what differs from the base mode is
// written, so its id is the one a hand-written file with the same rules has. `builder` adds the mode builder's note at
// the top (kBuilderNote), which marks a file the builder may rewrite.
std::string WriteModeFile(const Mode& mode, bool builder);
inline constexpr std::string_view kBuilderNote =
    "# Made in the game's mode builder. Changing it there rewrites this file.";

// The id: 16 hex digits from the base and the rules.
std::string Id(const Rules& rules);

// Short labels for the list's chips ("1 life", "No gates", "Score: time"), in a fixed order. Only rules that differ
// from the base mode's own are shown.
std::vector<std::string> Chips(const Rules& rules);

// The chips for a row in the list: at most `most`, the last one "+N more" when there are more.
std::vector<std::string> ListChips(const Rules& rules, size_t most = 4);

// The rules as bytes, for a replay of a custom run (demo_file.h), and back. Reading is strict. Version 1 (24 bytes) is
// written while no rule needs more, so a mode's id stays the same; version 2 adds the newer rules; version 3 is
// version 2 followed by the tuning: its version (u8), a count (u8), then each entry as a tag (u8) and a value (u32).
std::vector<uint8_t> Write(const Rules& rules);
std::optional<Rules> Read(std::span<const uint8_t> bytes);
inline constexpr size_t kRulesBytes = 24;
inline constexpr size_t kMaxTuningEntries = 16;
inline constexpr size_t kMaxRulesBytes = kRulesBytes + 1 + kMaxBonusBombs * 8 + 2 + kMaxTuningEntries * 5;

// A score as a short label: 100K, 1M, 2.5M.
std::string ShortScore(uint64_t score);

// "did you mean": the known word nearest to `word`, when it is close enough.
std::optional<std::string_view> Nearest(std::string_view word, std::span<const std::string_view> known);

// A time score: hundredths of a second survived.
std::string TimeText(uint64_t hundredths);

}  // namespace gw2::custom_mode
