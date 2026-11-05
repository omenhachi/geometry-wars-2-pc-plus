#pragma once

// The mode builder (mode_builder.cpp): a custom mode made from menus in the game, as rows that Left and Right change.
// Pure, so it is unit tested.
//
// Two tabs, Rules and Enemies. Values step through short lists instead of long ranges (the menus have no hold to
// repeat): lives the game's own, 1, 2, 3, 5, 10 or 99. A value a hand-written file brought in that isn't on a list
// stays until it is changed, and then steps to its neighbour on the list.
//
// Rules that don't apply to the base mode (enemies in Sequence and Waves, a level outside Sequence) are kept while
// the builder is open, so switching back restores them, but never reach the file: a builder file always reads.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "custom_mode_rules.h"

namespace gw2::mode_builder {

namespace rules = gw2::custom_mode;

enum class Tab : uint8_t { kRules, kEnemies };

enum class Row : uint8_t {
  kName,
  kBase,
  kLives,
  kExtraLives,
  kBombs,
  kExtraBombs,
  kBonusBombs,
  kInfiniteBombs,
  kTimeLimit,
  kSpeed,
  kGates,
  kScore,
  kLevel,  // Sequence only
  kEnemy,  // the Enemies tab: one per enemy, by index
};

struct RowRef {
  Row row;
  uint8_t enemy = 0;  // kEnemy: which
  bool operator==(const RowRef&) const = default;
};

inline constexpr std::array<int32_t, 6> kLives = {1, 2, 3, 5, 10, 99};     // after "Game's own"
inline constexpr std::array<int32_t, 7> kBombs = {0, 1, 2, 3, 5, 10, 99};  // after "Game's own"
inline constexpr std::array<float, 7> kTimes = {60, 120, 180, 300, 600, 1800, 3600};  // after "Off"
inline constexpr int32_t kSpeedSteps = 20;  // speed = steps / 20: 0.05 apart
inline const std::vector<uint64_t> kMasterBonus = {100'000, 1'000'000, 10'000'000, 100'000'000};

// What the game's unused enemies looked like (docs/MODDING.md), for the builder's amber "untested" note.
inline std::string_view UnusedLooks(uint8_t spawn) {
  switch (spawn) {
    case 7: return "pairs of small blue arrowheads";
    case 9: return "clusters of coloured bubbles";
    case 10: return "a small blue spinning atom";
    case 11: return "big blobs of blue cells";
    case 15: return "white six-pointed stars";
    case 17: return "green rounded hexagons";
    case 18: return "white shapes that shoot pellets";
    case 19: return "long pink zigzags";
    case 20: return "a small blue star";
    case 21: return "pink crosses with trails";
    case 23: return "a pink square";
    case 24: return "tiny green dots";
    case 25: return "a blue ring";
    default: return {};
  }
}

// One row as shown: its label, its value, whether it is the game's own (dimmed), and an amber note.
struct RowView {
  std::string label;
  std::string value;
  bool own = false;   // the base mode's own value: X has nothing to reset
  std::string note;   // amber: an unused enemy's looks, or why the row does nothing
};

class Builder {
 public:
  // From a mode (New copies the highlighted one; Edit opens a builder file).
  explicit Builder(const rules::Mode& from) : name_(from.name), author_(from.author), description_(from.description) {
    const rules::Rules& r = from.rules;
    base_ = r.base;
    lives_ = r.lives ? std::optional<int32_t>(r.legacy_spare_lives ? *r.lives + 1 : *r.lives) : std::nullopt;
    bombs_ = r.bombs;
    time_ = r.time_limit;
    speed_ = r.game_speed;
    gates_ = r.gates;
    infinite_ = r.infinite_bombs;
    score_ = r.score;
    extra_lives_ = r.extra_lives;
    extra_bombs_ = r.extra_bombs;
    bonus_ = r.bonus_bombs_at;
    level_ = r.level;
    off_ = r.enemies_off;
    swap_ = r.swap_to;
  }

  // The rows of a tab, in order. The Enemies tab has none in Sequence and Waves (EnemiesReason says why); Level only
  // shows in Sequence.
  std::vector<RowRef> Rows(Tab tab) const {
    std::vector<RowRef> rows;
    if (tab == Tab::kRules) {
      for (Row r : {Row::kName, Row::kBase, Row::kLives, Row::kExtraLives, Row::kBombs, Row::kExtraBombs,
                    Row::kBonusBombs, Row::kInfiniteBombs, Row::kTimeLimit, Row::kSpeed, Row::kGates, Row::kScore}) {
        rows.push_back({r});
      }
      if (base_ == rules::kSequenceBase) rows.push_back({Row::kLevel});
      return rows;
    }
    if (!EnemiesApply()) return rows;
    for (uint8_t i = 0; i < rules::kEnemyCount; ++i) rows.push_back({Row::kEnemy, i});
    return rows;
  }

  bool EnemiesApply() const { return base_ != rules::kSequenceBase && base_ != rules::kWavesBase; }
  std::string EnemiesReason() const {
    if (EnemiesApply()) return {};
    return std::string(base_ == rules::kSequenceBase ? "Sequence" : "Waves") +
           " is made of set levels and waves, so its enemies can't change.";
  }

  // Left (-1) or Right (+1) on a row. The name row rolls a new name (Roll).
  void Step(RowRef row, int dir) {
    switch (row.row) {
      case Row::kName: Roll(dir); break;
      case Row::kBase: base_ = uint32_t((int(base_) + dir + int(rules::kModeCount)) % int(rules::kModeCount)); break;
      case Row::kLives: lives_ = StepChoice(lives_, kLives, dir); break;
      case Row::kBombs: bombs_ = StepChoice(bombs_, kBombs, dir); break;
      case Row::kTimeLimit: time_ = StepChoice(time_, kTimes, dir); break;
      case Row::kExtraLives: extra_lives_ = !extra_lives_; break;
      case Row::kExtraBombs: extra_bombs_ = !extra_bombs_; break;
      case Row::kInfiniteBombs: infinite_ = !infinite_; break;
      case Row::kGates: gates_ = !gates_; break;
      case Row::kScore: score_ = score_ == rules::Score::kTime ? rules::Score::kPoints : rules::Score::kTime; break;
      case Row::kBonusBombs: bonus_ = bonus_.empty() ? kMasterBonus : std::vector<uint64_t>{}; break;
      case Row::kSpeed: {
        // From an off-list speed, the neighbouring step; never past the parser's range.
        const float steps = speed_ * kSpeedSteps;
        int next = dir > 0 ? int(std::floor(steps + 1e-4f)) + 1 : int(std::ceil(steps - 1e-4f)) - 1;
        next = std::clamp(next, int(rules::kMinSpeed * kSpeedSteps), int(rules::kMaxSpeed * kSpeedSteps));
        speed_ = float(next) / float(kSpeedSteps);
        break;
      }
      case Row::kLevel: {
        // 0 is the whole of Sequence, then levels 1 to 20, round and round.
        const int n = rules::kSequenceLevels + 1;
        const int level = (level_.value_or(0) + dir + n) % n;
        level_ = level == 0 ? std::nullopt : std::optional<int32_t>(level);
        break;
      }
      case Row::kEnemy: StepEnemy(row.enemy, dir); break;
    }
  }

  // X: back to the base mode's own value.
  void Reset(RowRef row) {
    switch (row.row) {
      case Row::kName: break;
      case Row::kBase: break;
      case Row::kLives: lives_.reset(); break;
      case Row::kBombs: bombs_.reset(); break;
      case Row::kTimeLimit: time_.reset(); break;
      case Row::kExtraLives: extra_lives_ = true; break;
      case Row::kExtraBombs: extra_bombs_ = true; break;
      case Row::kInfiniteBombs: infinite_ = false; break;
      case Row::kGates: gates_ = true; break;
      case Row::kScore: score_ = rules::Score::kPoints; break;
      case Row::kBonusBombs: bonus_.clear(); break;
      case Row::kSpeed: speed_ = 1.0f; break;
      case Row::kLevel: level_.reset(); break;
      case Row::kEnemy:
        off_ = uint16_t(off_ & ~(1u << row.enemy));
        swap_[row.enemy] = 0;
        break;
    }
  }

  RowView View(RowRef row) const {
    const auto own_or = [](bool own, std::string value) { return own ? std::string("Game's own") : value; };
    switch (row.row) {
      case Row::kName: return {"Name", name_.empty() ? std::string("(none)") : name_, false, {}};
      case Row::kBase: return {"Base mode", Capital(rules::kBaseNames[base_]), false, {}};
      case Row::kLives:
        return {"Lives", own_or(!lives_, lives_ ? (*lives_ == 1 ? "1 ship" : std::to_string(*lives_) + " ships") : ""),
                !lives_, {}};
      case Row::kBombs: return {"Bombs", own_or(!bombs_, bombs_ ? std::to_string(*bombs_) : ""), !bombs_, {}};
      case Row::kTimeLimit: return {"Time limit", time_ ? TimeText(*time_) : "Off", !time_, {}};
      case Row::kExtraLives: return {"Extra lives", extra_lives_ ? "Game's own" : "None", extra_lives_, {}};
      case Row::kExtraBombs: return {"Extra bombs", extra_bombs_ ? "Game's own" : "None", extra_bombs_, {}};
      case Row::kInfiniteBombs: return {"Infinite bombs", infinite_ ? "On" : "Off", !infinite_, {}};
      case Row::kGates: return {"Gates", gates_ ? "On" : "Off", gates_, {}};
      case Row::kScore: return {"Score", score_ == rules::Score::kTime ? "Time survived" : "Points", score_ == rules::Score::kPoints, {}};
      case Row::kBonusBombs:
        return {"Bonus bombs", bonus_.empty() ? "None" : bonus_ == kMasterBonus ? "100K, 1M, 10M, 100M" : "Custom",
                bonus_.empty(), {}};
      case Row::kSpeed: return {"Speed", std::format("x{:.2f}", speed_), speed_ == 1.0f, {}};
      case Row::kLevel:
        return {"Level", level_ ? std::to_string(*level_) : "All of Sequence", !level_, {}};
      case Row::kEnemy: {
        const size_t i = row.enemy;
        RowView v{Capital(rules::kEnemyNames[i]), "On", true, {}};
        if (off_ & (1u << i)) {
          v.value = "Off";
          v.own = false;
          if (swap_[i]) v.note = "Off wins: its swap does nothing";
        } else if (swap_[i]) {
          v.own = false;
          v.value = "As " + TargetName(swap_[i]);
          if (const auto looks = UnusedLooks(swap_[i]); !looks.empty()) v.note = "Untested: " + std::string(looks);
        }
        std::replace(v.label.begin(), v.label.end(), '_', ' ');
        return v;
      }
    }
    return {};
  }

  // The mode as the file holds it: only what applies to the base mode.
  rules::Mode Mode() const {
    rules::Mode m{name_, author_, description_, {}};
    rules::Rules& r = m.rules;
    r.base = base_;
    r.lives = lives_;
    r.bombs = bombs_;
    r.time_limit = time_;
    r.game_speed = speed_;
    r.gates = gates_;
    r.infinite_bombs = infinite_;
    r.score = score_;
    r.extra_lives = extra_lives_;
    r.extra_bombs = extra_bombs_;
    r.bonus_bombs_at = bonus_;
    if (base_ == rules::kSequenceBase) r.level = level_;
    if (EnemiesApply()) {
      r.enemies_off = off_ == rules::kAllEnemies ? uint16_t(0) : off_;
      r.swap_to = swap_;
    }
    return m;
  }

  const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }
  uint32_t base() const { return base_; }

  // A name from two short word lists, picked by a counter so Left and Right walk back and forth through the same ones.
  void Roll(int dir) {
    static constexpr std::array<std::string_view, 16> kFirst = {
        "Neon", "Hyper", "Grid", "Ultra", "Laser", "Quantum", "Turbo", "Plasma",
        "Vector", "Pixel", "Retro", "Nova", "Omega", "Chrome", "Static", "Prism"};
    static constexpr std::array<std::string_view, 16> kSecond = {
        "Rush", "Storm", "Swarm", "Frenzy", "Gauntlet", "Drift", "Siege", "Fever",
        "Mayhem", "Shift", "Surge", "Blitz", "Echo", "Bloom", "Rumble", "Chaos"};
    roll_ = (roll_ + dir + 256) % 256;
    // Steps through the pairs so neighbours differ in both words.
    const int a = (roll_ * 5) % 16, b = (roll_ * 11 + 3) % 16;
    name_ = std::string(kFirst[size_t(a)]) + " " + Middle() + std::string(kSecond[size_t(b)]);
  }

 private:
  template <typename T, size_t N>
  static std::optional<T> StepChoice(std::optional<T> value, const std::array<T, N>& list, int dir) {
    // nullopt (the game's own or Off) sits before the list's first value.
    if (dir > 0) {
      if (!value) return list[0];
      for (const T& v : list) {
        if (v > *value) return v;
      }
      return value;
    }
    if (!value) return value;
    for (size_t i = N; i-- > 0;) {
      if (list[i] < *value) return list[i];
    }
    return std::nullopt;
  }

  // Enemy states, in order: On, Off, then each swap target that isn't the enemy itself.
  void StepEnemy(uint8_t i, int dir) {
    std::vector<uint8_t> targets;
    for (const rules::SwapTarget& t : rules::kSwapTargets) {
      const auto e = rules::EnemyForSpawn(t.spawn);
      if (!e || size_t(*e) != i) targets.push_back(t.spawn);
    }
    const int states = int(targets.size()) + 2;
    int at = (off_ & (1u << i)) ? 1 : 0;
    if (!(off_ & (1u << i)) && swap_[i]) {
      at = 2 + int(std::find(targets.begin(), targets.end(), swap_[i]) - targets.begin());
    }
    at = (at + dir + states) % states;
    off_ = uint16_t(at == 1 ? off_ | (1u << i) : off_ & ~(1u << i));
    swap_[i] = at >= 2 ? targets[size_t(at - 2)] : uint8_t(0);
  }

  // The enemy word in a rolled name: what this mode is about, when it swaps everything into one enemy.
  std::string Middle() const {
    for (size_t i = 0; i < rules::kEnemyCount; ++i) {
      if (swap_[i] && !(off_ & (1u << i)) && rules::EnemyForSpawn(swap_[i])) {
        std::string word(rules::kEnemyNames[size_t(*rules::EnemyForSpawn(swap_[i]))]);
        word[0] = char(word[0] - 'a' + 'A');
        return word + " ";
      }
    }
    return {};
  }

  static std::string TargetName(uint8_t spawn) {
    if (const auto e = rules::EnemyForSpawn(spawn)) return std::string(rules::kEnemyNames[size_t(*e)]);
    return "type " + std::to_string(spawn);
  }
  static std::string Capital(std::string_view text) {
    std::string out(text);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');
    return out;
  }
  static std::string TimeText(float seconds) {
    const int s = int(seconds + 0.5f);
    return s % 60 == 0 ? std::to_string(s / 60) + " min" : std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") +
                                                           std::to_string(s % 60);
  }

  std::string name_, author_, description_;
  uint32_t base_ = 1;
  std::optional<int32_t> lives_, bombs_, level_;
  std::optional<float> time_;
  float speed_ = 1.0f;
  bool gates_ = true, infinite_ = false, extra_lives_ = true, extra_bombs_ = true;
  rules::Score score_ = rules::Score::kPoints;
  std::vector<uint64_t> bonus_;
  uint16_t off_ = 0;
  std::array<uint8_t, rules::kEnemyCount> swap_{};
  int roll_ = 0;
};

// A folder name for a mode's name: lower case letters, digits and hyphens, made unique against `taken`.
inline std::string FolderFor(std::string_view name, const std::vector<std::string>& taken) {
  std::string slug;
  for (const char c : name) {
    if (c >= 'A' && c <= 'Z') slug += char(c - 'A' + 'a');
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) slug += c;
    else if (!slug.empty() && slug.back() != '-') slug += '-';
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug.empty()) slug = "my-mode";
  slug = slug.substr(0, 40);
  std::string folder = slug;
  for (int n = 2; std::find(taken.begin(), taken.end(), folder) != taken.end(); ++n) {
    folder = slug + "-" + std::to_string(n);
  }
  return folder;
}

}  // namespace gw2::mode_builder
