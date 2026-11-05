#include "custom_mode_rules.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>

#include <toml++/toml.hpp>

namespace gw2::custom_mode {
namespace {

constexpr std::string_view kTopKeys[] = {"format", "name", "author", "description", "base", "rules", "enemies"};
constexpr std::string_view kRuleKeys[] = {"lives",          "bombs", "time_limit",  "game_speed",  "gates",
                                          "infinite_bombs", "score", "extra_lives", "extra_bombs", "bonus_bombs_at",
                                          "level"};
constexpr std::string_view kScores[] = {"points", "time"};
constexpr size_t kMaxText = 64;
constexpr size_t kMaxDescription = 200;

constexpr std::string_view kEnemyPlurals[kEnemyCount] = {"wanderers", "grunts",      "weavers", "spinners", "snakes",
                                                         "repulsars", "black holes", "rockets", "ducks"};

// Each enemy's own spawn type (EnemyForSpawn's first), so a swap into itself is known.
constexpr uint8_t kOwnSpawn[kEnemyCount] = {1, 2, 3, 4, 8, 6, 5, 12, 22};

constexpr std::array<std::string_view, kSwapTargets.size()> SwapTargetNames() {
  std::array<std::string_view, kSwapTargets.size()> names{};
  for (size_t i = 0; i < kSwapTargets.size(); ++i) names[i] = kSwapTargets[i].name;
  return names;
}
constexpr auto kSwapTargetNames = SwapTargetNames();

// A swap target as a chip says it: "weavers", "type 17".
std::string TargetPlural(uint8_t spawn) {
  if (const auto enemy = EnemyForSpawn(spawn)) return std::string(kEnemyPlurals[size_t(*enemy)]);
  return std::format("type {}", spawn);
}

// Each base mode's own bombs (its setup function, 0x82034EA0..0x82035130), so chips show only changes.
constexpr int32_t kBaseBombs[kModeCount] = {3, 3, 3, 0, 0, 0};

uint32_t LineOf(const toml::node& node) {
  return node.source().begin.line;
}

class Reader {
 public:
  explicit Reader(ParseResult& result) : result_(result) {}

  void Error(uint32_t line, std::string text) { result_.errors.push_back({line, std::move(text)}); }
  void Warn(uint32_t line, std::string text) { result_.warnings.push_back({line, std::move(text)}); }

  void Unknown(const toml::key& key, std::span<const std::string_view> known, std::string_view where) {
    std::string text = std::format("\"{}\" isn't a {} key", key.str(), where);
    if (const auto near = Nearest(key.str(), known)) text += std::format(" (did you mean \"{}\"?)", *near);
    Error(key.source().begin.line, std::move(text));
  }

  std::optional<std::string> Text(const toml::node& node, std::string_view key, size_t max) {
    const auto* value = node.as_string();
    if (!value) {
      Error(LineOf(node), std::format("\"{}\" should be text in quotes", key));
      return std::nullopt;
    }
    std::string text = value->get();
    if (text.size() > max) {
      Warn(LineOf(node), std::format("\"{}\" is cut to {} characters", key, max));
      text.resize(max);
    }
    // Only printable ASCII is drawn by the menu's line font.
    for (char& c : text) {
      if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = '?';
    }
    return text;
  }

  std::optional<int32_t> Whole(const toml::node& node, std::string_view key, int32_t lo, int32_t hi) {
    const auto* value = node.as_integer();
    if (!value) {
      Error(LineOf(node), std::format("\"{}\" should be a whole number", key));
      return std::nullopt;
    }
    const int64_t raw = value->get();
    const int64_t clamped = std::clamp<int64_t>(raw, lo, hi);
    if (clamped != raw) Warn(LineOf(node), std::format("{} {} is out of range, so it's {}", key, raw, clamped));
    return int32_t(clamped);
  }

  std::optional<float> Number(const toml::node& node, std::string_view key, float lo, float hi) {
    double raw = 0;
    if (const auto* f = node.as_floating_point()) raw = f->get();
    else if (const auto* i = node.as_integer()) raw = double(i->get());
    else {
      Error(LineOf(node), std::format("\"{}\" should be a number", key));
      return std::nullopt;
    }
    if (!std::isfinite(raw)) {
      Error(LineOf(node), std::format("\"{}\" should be a number", key));
      return std::nullopt;
    }
    const double clamped = std::clamp<double>(raw, lo, hi);
    if (clamped != raw) Warn(LineOf(node), std::format("{} {:g} is out of range, so it's {:g}", key, raw, clamped));
    return float(clamped);
  }

  std::optional<bool> Flag(const toml::node& node, std::string_view key) {
    const auto* value = node.as_boolean();
    if (!value) {
      Error(LineOf(node), std::format("\"{}\" should be true or false", key));
      return std::nullopt;
    }
    return value->get();
  }

 private:
  ParseResult& result_;
};

void ReadRules(Reader& reader, const toml::table& table, Rules& rules) {
  for (const auto& [key, node] : table) {
    const std::string_view k = key.str();
    if (k == "lives") {
      rules.lives = reader.Whole(node, k, 1, kMaxLives);
    } else if (k == "bombs") {
      rules.bombs = reader.Whole(node, k, 0, kMaxBombs);
    } else if (k == "time_limit") {
      rules.time_limit = reader.Number(node, k, kMinTimeLimit, kMaxTimeLimit);
    } else if (k == "game_speed") {
      rules.game_speed = reader.Number(node, k, kMinSpeed, kMaxSpeed).value_or(1.0f);
    } else if (k == "gates") {
      rules.gates = reader.Flag(node, k).value_or(true);
    } else if (k == "infinite_bombs") {
      rules.infinite_bombs = reader.Flag(node, k).value_or(false);
    } else if (k == "extra_lives") {
      rules.extra_lives = reader.Flag(node, k).value_or(true);
    } else if (k == "extra_bombs") {
      rules.extra_bombs = reader.Flag(node, k).value_or(true);
    } else if (k == "level") {
      rules.level = reader.Whole(node, k, 1, kSequenceLevels);
    } else if (k == "bonus_bombs_at") {
      const auto* list = node.as_array();
      if (!list) {
        reader.Error(LineOf(node), "\"bonus_bombs_at\" should be a list of scores, like [100000, 1000000]");
        continue;
      }
      std::vector<uint64_t> scores;
      for (const toml::node& item : *list) {
        const auto* value = item.as_integer();
        if (!value || value->get() < 1 || uint64_t(value->get()) > kMaxBonusScore) {
          reader.Error(LineOf(item), std::format("\"bonus_bombs_at\" scores should be whole numbers from 1 to {}",
                                                 kMaxBonusScore));
          continue;
        }
        scores.push_back(uint64_t(value->get()));
      }
      std::sort(scores.begin(), scores.end());
      scores.erase(std::unique(scores.begin(), scores.end()), scores.end());
      if (scores.size() > kMaxBonusBombs) {
        reader.Warn(LineOf(node), std::format("bonus_bombs_at keeps its first {} scores", kMaxBonusBombs));
        scores.resize(kMaxBonusBombs);
      }
      rules.bonus_bombs_at = std::move(scores);
    } else if (k == "score") {
      const auto text = reader.Text(node, k, kMaxText);
      if (!text) continue;
      if (*text == "points") rules.score = Score::kPoints;
      else if (*text == "time") rules.score = Score::kTime;
      else {
        std::string message = std::format("score \"{}\" should be \"points\" or \"time\"", *text);
        if (const auto near = Nearest(*text, kScores)) message += std::format(" (did you mean \"{}\"?)", *near);
        reader.Error(LineOf(node), std::move(message));
      }
    } else {
      reader.Unknown(key, kRuleKeys, "rules");
    }
  }
}

// [enemies.swap]: each enemy the game asks for, made as another (kSwapTargets).
void ReadSwaps(Reader& reader, const toml::table& table, Rules& rules) {
  for (const auto& [key, node] : table) {
    const auto from = std::find(kEnemyNames.begin(), kEnemyNames.end(), key.str());
    if (from == kEnemyNames.end()) {
      reader.Unknown(key, kEnemyNames, "enemies.swap");
      continue;
    }
    const size_t source = size_t(from - kEnemyNames.begin());
    if (node.as_array()) {
      reader.Error(LineOf(node), std::format("\"{}\" swaps into one enemy for now, like {} = \"weaver\"", key.str(),
                                             key.str()));
      continue;
    }
    const auto text = reader.Text(node, key.str(), kMaxText);
    if (!text) continue;
    const auto to = std::find_if(kSwapTargets.begin(), kSwapTargets.end(),
                                 [&](const SwapTarget& t) { return t.name == *text; });
    if (to == kSwapTargets.end()) {
      std::string message;
      if (*text == "black_hole" || *text == "rocket" || *text == "type_14") {
        message = std::format("nothing can be swapped into \"{}\" yet", *text);
      } else {
        message = std::format("\"{}\" isn't an enemy a swap can make", *text);
        if (const auto near = Nearest(*text, kSwapTargetNames)) message += std::format(" (did you mean \"{}\"?)", *near);
      }
      reader.Error(LineOf(node), std::move(message));
      continue;
    }
    if (to->spawn == kOwnSpawn[source]) {
      reader.Warn(LineOf(node), std::format("{} = \"{}\" swaps it into itself, so it changes nothing", key.str(), *text));
      rules.swap_to[source] = 0;
      continue;
    }
    rules.swap_to[source] = to->spawn;
  }
}

// [enemies]: each one the mode turns off, by name; true leaves it as the game has it. [enemies.swap] is beside them.
void ReadEnemies(Reader& reader, const toml::table& table, Rules& rules) {
  for (const auto& [key, node] : table) {
    if (key.str() == "swap") {
      if (const auto* swaps = node.as_table()) {
        ReadSwaps(reader, *swaps, rules);
      } else {
        reader.Error(LineOf(node), "\"swap\" should be an [enemies.swap] section");
      }
      continue;
    }
    const auto at = std::find(kEnemyNames.begin(), kEnemyNames.end(), key.str());
    if (at == kEnemyNames.end()) {
      reader.Unknown(key, kEnemyNames, "enemies");
      continue;
    }
    const auto on = reader.Flag(node, key.str());
    if (!on) continue;
    const uint16_t bit = uint16_t(1u << (at - kEnemyNames.begin()));
    rules.enemies_off = *on ? uint16_t(rules.enemies_off & ~bit) : uint16_t(rules.enemies_off | bit);
  }
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

uint32_t Get32(std::span<const uint8_t> bytes, size_t at) {
  return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 | uint32_t(bytes[at + 2]) << 16 |
         uint32_t(bytes[at + 3]) << 24;
}

constexpr uint8_t kLivesSet = 1, kBombsSet = 2, kTimeSet = 4, kGates = 8, kInfiniteBombs = 16;
constexpr uint8_t kNoExtraLives = 32, kNoExtraBombs = 64;  // version 2
constexpr uint8_t kKnownFlags = kLivesSet | kBombsSet | kTimeSet | kGates | kInfiniteBombs;
constexpr uint8_t kKnownFlags2 = kKnownFlags | kNoExtraLives | kNoExtraBombs;
// Version 1: 24 bytes, its lives the game's spare ships. Version 2: lives are ships in all, and a list of bonus
// scores follows (u8 count, then each u64). Version 3: version 2, then the tuning (custom_mode_rules.h).
constexpr uint8_t kBytesVersion1 = 1;
constexpr uint8_t kBytesVersion2 = 2;
constexpr uint8_t kBytesVersion3 = 3;
// The tuning's tags. A tag is never reused for something else.
constexpr uint8_t kTagEnemiesOff = 1;
// [enemies.swap]: one tag per swapped enemy (this + its Enemy index), the value its target's spawn type.
constexpr uint8_t kTagSwapFirst = 16;

void Put64(std::vector<uint8_t>& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

uint64_t Get64(std::span<const uint8_t> bytes, size_t at) {
  return uint64_t(Get32(bytes, at)) | uint64_t(Get32(bytes, at + 4)) << 32;
}

}  // namespace

ParseResult Parse(std::string_view toml_text) {
  ParseResult result;
  Reader reader(result);
  toml::table table;
  try {
    table = toml::parse(toml_text);
  } catch (const toml::parse_error& error) {
    reader.Error(error.source().begin.line, std::string(error.description()));
    return result;
  }

  // The format first: a newer one may use keys this version doesn't know, and that is not the author's mistake.
  int64_t format_number = 1;
  if (const toml::node* format = table.get("format")) {
    const auto* value = format->as_integer();
    if (!value || value->get() < 1) {
      reader.Error(LineOf(*format), "\"format\" should be 1 or 2");
      return result;
    }
    if (value->get() > kFormat) {
      result.newer_format = true;
      reader.Error(LineOf(*format), "This mode needs a newer version of PC Plus");
      return result;
    }
    format_number = value->get();
  } else {
    reader.Error(0, "Add format = 1 at the top");
  }

  Mode mode;
  bool has_base = false;
  for (const auto& [key, node] : table) {
    const std::string_view k = key.str();
    if (k == "format") continue;
    if (k == "name") {
      mode.name = reader.Text(node, k, kMaxText).value_or("");
    } else if (k == "author") {
      mode.author = reader.Text(node, k, kMaxText).value_or("");
    } else if (k == "description") {
      mode.description = reader.Text(node, k, kMaxDescription).value_or("");
    } else if (k == "base") {
      const auto text = reader.Text(node, k, kMaxText);
      if (!text) continue;
      std::string lower = *text;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      const auto at = std::find(kBaseNames.begin(), kBaseNames.end(), lower);
      if (at == kBaseNames.end()) {
        std::string message = std::format("base \"{}\" isn't one of the game's modes", *text);
        if (const auto near = Nearest(lower, kBaseNames)) message += std::format(" (did you mean \"{}\"?)", *near);
        reader.Error(LineOf(node), std::move(message));
        continue;
      }
      mode.rules.base = uint32_t(at - kBaseNames.begin());
      has_base = true;
    } else if (k == "rules") {
      if (const auto* rules = node.as_table()) {
        ReadRules(reader, *rules, mode.rules);
      } else {
        reader.Error(LineOf(node), "\"rules\" should be a [rules] section");
      }
    } else if (k == "enemies") {
      if (format_number < 2) {
        reader.Error(LineOf(node), "[enemies] needs format = 2 at the top");
      } else if (const auto* enemies = node.as_table()) {
        ReadEnemies(reader, *enemies, mode.rules);
      } else {
        reader.Error(LineOf(node), "\"enemies\" should be an [enemies] section");
      }
    } else {
      reader.Unknown(key, kTopKeys, "mode");
    }
  }
  if (mode.name.empty()) reader.Error(0, "Give the mode a name, like name = \"My mode\"");
  if (has_base && mode.rules.level && mode.rules.base != kSequenceBase) {
    reader.Error(0, "\"level\" is only for Sequence, like base = \"sequence\"");
  }
  // Swaps that can't do what they say: from an enemy that is off, or into one that is.
  for (size_t i = 0; i < kEnemyCount; ++i) {
    const uint8_t to = mode.rules.swap_to[i];
    if (!to) continue;
    if (mode.rules.EnemyOff(Enemy(i))) {
      reader.Warn(0, std::format("{} is off, so its swap does nothing", kEnemyNames[i]));
    } else if (const auto target = EnemyForSpawn(to); target && mode.rules.EnemyOff(*target)) {
      reader.Warn(0, std::format("{} is off, but {} swaps into it, so {} still appear", kEnemyNames[size_t(*target)],
                                 kEnemyNames[i], kEnemyPlurals[size_t(*target)]));
    }
  }
  if (has_base && mode.rules.Tuned() &&
      (mode.rules.base == kSequenceBase || mode.rules.base == kWavesBase)) {
    // Their levels and waves are set pieces: one with a missing enemy might never end.
    reader.Error(0, std::format("[enemies] can't change {} yet; try evolved, deadline, pacifism or king",
                                mode.rules.base == kSequenceBase ? "Sequence" : "Waves"));
  }
  if (mode.rules.enemies_off == kAllEnemies) reader.Error(0, "[enemies] turns every enemy off; leave at least one on");
  if (!has_base && std::none_of(result.errors.begin(), result.errors.end(),
                                [](const Message& m) { return m.text.starts_with("base"); })) {
    reader.Error(0, "Choose a base mode, like base = \"evolved\"");
  }
  if (result.errors.empty()) result.mode = std::move(mode);
  return result;
}

std::vector<uint8_t> Write(const Rules& rules) {
  // Version 1 while nothing needs more (a mode's id is a hash of these bytes, so it stays the same), and for a replay's
  // rules from before lives meant ships in all.
  const bool newer = !rules.extra_lives || !rules.extra_bombs || !rules.bonus_bombs_at.empty() || rules.Tuned();
  const bool version1 = !newer && (!rules.lives || rules.legacy_spare_lives);
  std::vector<uint8_t> out;
  out.reserve(kMaxRulesBytes);
  out.push_back(version1 ? kBytesVersion1 : rules.Tuned() ? kBytesVersion3 : kBytesVersion2);
  out.push_back(uint8_t(rules.base));
  uint8_t flags = 0;
  if (rules.lives) flags |= kLivesSet;
  if (rules.bombs) flags |= kBombsSet;
  if (rules.time_limit) flags |= kTimeSet;
  if (rules.gates) flags |= kGates;
  if (rules.infinite_bombs) flags |= kInfiniteBombs;
  if (!version1 && !rules.extra_lives) flags |= kNoExtraLives;
  if (!version1 && !rules.extra_bombs) flags |= kNoExtraBombs;
  out.push_back(flags);
  out.push_back(uint8_t(rules.score));
  Put32(out, uint32_t(rules.lives.value_or(0)));
  Put32(out, uint32_t(rules.bombs.value_or(0)));
  Put32(out, std::bit_cast<uint32_t>(rules.time_limit.value_or(0.0f)));
  Put32(out, std::bit_cast<uint32_t>(rules.game_speed));
  Put32(out, uint32_t(rules.level.value_or(0)));  // once always 0, so older builds refuse a level's replay
  if (!version1) {
    const size_t count = std::min(rules.bonus_bombs_at.size(), kMaxBonusBombs);
    out.push_back(uint8_t(count));
    for (size_t i = 0; i < count; ++i) Put64(out, rules.bonus_bombs_at[i]);
  }
  if (rules.Tuned()) {
    std::vector<std::pair<uint8_t, uint32_t>> entries;
    if (rules.enemies_off) entries.push_back({kTagEnemiesOff, rules.enemies_off});
    for (size_t i = 0; i < kEnemyCount; ++i) {
      if (rules.swap_to[i]) entries.push_back({uint8_t(kTagSwapFirst + i), rules.swap_to[i]});
    }
    out.push_back(rules.tuning_version);
    out.push_back(uint8_t(entries.size()));
    for (const auto& [tag, value] : entries) {
      out.push_back(tag);
      Put32(out, value);
    }
  }
  return out;
}

std::optional<Rules> Read(std::span<const uint8_t> bytes) {
  if (bytes.size() < kRulesBytes) return std::nullopt;
  const uint8_t version = bytes[0];
  if (version != kBytesVersion1 && version != kBytesVersion2 && version != kBytesVersion3) return std::nullopt;
  if (version == kBytesVersion1 && bytes.size() != kRulesBytes) return std::nullopt;
  Rules rules;
  if (bytes[1] >= kModeCount) return std::nullopt;
  rules.base = bytes[1];
  const uint8_t flags = bytes[2];
  if (flags & ~(version == kBytesVersion1 ? kKnownFlags : kKnownFlags2)) return std::nullopt;
  if (bytes[3] > uint8_t(Score::kTime)) return std::nullopt;
  rules.score = Score(bytes[3]);
  const int32_t lives = int32_t(Get32(bytes, 4));
  const int32_t bombs = int32_t(Get32(bytes, 8));
  const float time = std::bit_cast<float>(Get32(bytes, 12));
  const float speed = std::bit_cast<float>(Get32(bytes, 16));
  const uint32_t level = Get32(bytes, 20);
  if (level != 0) {
    if (level > uint32_t(kSequenceLevels) || rules.base != kSequenceBase) return std::nullopt;
    rules.level = int32_t(level);
  }
  if (flags & kLivesSet) {
    if (lives < 1 || lives > kMaxLives) return std::nullopt;
    rules.lives = lives;
    rules.legacy_spare_lives = version == kBytesVersion1;
  } else if (lives != 0) {
    return std::nullopt;
  }
  if (flags & kBombsSet) {
    if (bombs < 0 || bombs > kMaxBombs) return std::nullopt;
    rules.bombs = bombs;
  } else if (bombs != 0) {
    return std::nullopt;
  }
  if (flags & kTimeSet) {
    if (!(time >= kMinTimeLimit && time <= kMaxTimeLimit)) return std::nullopt;
    rules.time_limit = time;
  } else if (std::bit_cast<uint32_t>(time) != 0) {
    return std::nullopt;
  }
  if (!(speed >= kMinSpeed && speed <= kMaxSpeed)) return std::nullopt;
  rules.game_speed = speed;
  rules.gates = flags & kGates;
  rules.infinite_bombs = flags & kInfiniteBombs;
  rules.extra_lives = !(flags & kNoExtraLives);
  rules.extra_bombs = !(flags & kNoExtraBombs);
  if (version >= kBytesVersion2) {
    if (bytes.size() < kRulesBytes + 1) return std::nullopt;
    const size_t count = bytes[kRulesBytes];
    const size_t end = kRulesBytes + 1 + count * 8;
    if (count > kMaxBonusBombs || bytes.size() < end) return std::nullopt;
    if (version == kBytesVersion2 && bytes.size() != end) return std::nullopt;
    for (size_t i = 0; i < count; ++i) {
      const uint64_t score = Get64(bytes, kRulesBytes + 1 + i * 8);
      if (score < 1 || score > kMaxBonusScore || (i > 0 && score <= rules.bonus_bombs_at.back())) return std::nullopt;
      rules.bonus_bombs_at.push_back(score);
    }
    if (version == kBytesVersion3) {
      // The tuning: a version this build knows, then each tag once, in the order they are written.
      if (bytes.size() < end + 2) return std::nullopt;
      const uint8_t tuning = bytes[end];
      const size_t entries = bytes[end + 1];
      if (tuning < 1 || tuning > kTuningVersion || entries == 0 || entries > kMaxTuningEntries) return std::nullopt;
      if (bytes.size() != end + 2 + entries * 5) return std::nullopt;
      rules.tuning_version = tuning;
      uint8_t last_tag = 0;
      for (size_t i = 0; i < entries; ++i) {
        const size_t at = end + 2 + i * 5;
        const uint8_t tag = bytes[at];
        const uint32_t value = Get32(bytes, at + 1);
        if (tag <= last_tag) return std::nullopt;
        last_tag = tag;
        if (tag == kTagEnemiesOff) {
          if (value == 0 || value >= kAllEnemies) return std::nullopt;  // never written: all off is refused
          rules.enemies_off = uint16_t(value);
        } else if (tag >= kTagSwapFirst && tag < kTagSwapFirst + kEnemyCount) {
          const size_t source = tag - kTagSwapFirst;
          if (!IsSwapTarget(value) || value == kOwnSpawn[source]) return std::nullopt;
          rules.swap_to[source] = uint8_t(value);
        } else {
          return std::nullopt;  // a tag this build doesn't know
        }
      }
    } else if (rules.extra_lives && rules.extra_bombs && rules.bonus_bombs_at.empty() && !rules.lives) {
      // Version 2 is only written when version 1 can't hold the rules.
      return std::nullopt;
    }
  }
  return rules;
}

std::string Id(const Rules& rules) {
  uint64_t hash = 0xCBF29CE484222325ull;  // FNV-1a
  for (const uint8_t byte : Write(rules)) {
    hash ^= byte;
    hash *= 0x100000001B3ull;
  }
  return std::format("{:016x}", hash);
}

std::vector<std::string> Chips(const Rules& rules) {
  std::vector<std::string> chips;
  if (rules.lives) {
    const int32_t ships = rules.legacy_spare_lives ? *rules.lives + 1 : *rules.lives;
    chips.push_back(ships == 1 ? "1 life" : std::format("{} lives", ships));
  }
  if (!rules.extra_lives) chips.push_back("No extra lives");
  if (rules.infinite_bombs) {
    chips.push_back("Infinite bombs");
  } else if (rules.bombs && *rules.bombs != kBaseBombs[rules.base]) {
    chips.push_back(*rules.bombs == 0 ? "No bombs" : *rules.bombs == 1 ? "1 bomb" : std::format("{} bombs", *rules.bombs));
  }
  if (rules.level) chips.push_back(std::format("Level {}", *rules.level));
  if (rules.time_limit) {
    const int seconds = int(std::lround(*rules.time_limit));
    chips.push_back(seconds % 60 == 0 ? std::format("{} min", seconds / 60)
                                      : std::format("{}:{:02}", seconds / 60, seconds % 60));
  }
  if (rules.game_speed != 1.0f) chips.push_back(std::format("Speed x{:g}", std::round(rules.game_speed * 100) / 100));
  if (!rules.gates) chips.push_back("No gates");
  if (rules.score == Score::kTime) chips.push_back("Score: time");
  if (!rules.extra_bombs) chips.push_back("No milestone bombs");
  if (!rules.bonus_bombs_at.empty()) {
    std::string text = "Bombs at";
    for (const uint64_t score : rules.bonus_bombs_at) text += " " + ShortScore(score);
    chips.push_back(std::move(text));
  }
  if (rules.enemies_off) {
    // "No snakes", "No snakes or grunts"; with most of them off, the few that are left: "Only wanderers".
    std::vector<std::string_view> off, on;
    for (size_t i = 0; i < kEnemyCount; ++i) (rules.EnemyOff(Enemy(i)) ? off : on).push_back(kEnemyPlurals[i]);
    const auto join = [](const std::vector<std::string_view>& names, std::string_view last) {
      std::string text;
      for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) text += i + 1 == names.size() ? last : std::string_view(", ");
        text += names[i];
      }
      return text;
    };
    chips.push_back(off.size() > on.size() ? "Only " + join(on, " and ") : "No " + join(off, " or "));
  }
  for (size_t i = 0; i < kEnemyCount; ++i) {
    if (!rules.swap_to[i]) continue;
    std::string from(kEnemyPlurals[i]);
    from[0] = char(std::toupper(static_cast<unsigned char>(from[0])));
    chips.push_back(std::format("{} as {}", from, TargetPlural(rules.swap_to[i])));
  }
  return chips;
}

std::vector<std::string> ListChips(const Rules& rules, size_t most) {
  std::vector<std::string> chips = Chips(rules);
  if (most >= 1 && chips.size() > most) {
    const size_t more = chips.size() - (most - 1);
    chips.resize(most - 1);
    chips.push_back(std::format("+{} more", more));
  }
  return chips;
}

std::string ShortScore(uint64_t score) {
  const auto scaled = [](uint64_t value, uint64_t unit, const char* suffix) {
    const uint64_t tenths = value * 10 / unit;
    return tenths % 10 == 0 ? std::format("{}{}", tenths / 10, suffix)
                            : std::format("{}.{}{}", tenths / 10, tenths % 10, suffix);
  };
  if (score >= 1'000'000'000) return scaled(score, 1'000'000'000, "B");
  if (score >= 1'000'000) return scaled(score, 1'000'000, "M");
  if (score >= 1'000) return scaled(score, 1'000, "K");
  return std::to_string(score);
}

std::optional<std::string_view> Nearest(std::string_view word, std::span<const std::string_view> known) {
  std::optional<std::string_view> best;
  size_t best_distance = SIZE_MAX;
  for (const std::string_view candidate : known) {
    // Edit distance, case-insensitive, with two swapped letters as one edit (the commonest typo).
    const auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
    const size_t w = word.size(), c = candidate.size();
    std::vector<size_t> d((w + 1) * (c + 1));
    const auto at = [&](size_t i, size_t j) -> size_t& { return d[i * (c + 1) + j]; };
    for (size_t i = 0; i <= w; ++i) at(i, 0) = i;
    for (size_t j = 0; j <= c; ++j) at(0, j) = j;
    for (size_t i = 1; i <= w; ++i) {
      for (size_t j = 1; j <= c; ++j) {
        const bool same = lower(word[i - 1]) == lower(candidate[j - 1]);
        at(i, j) = std::min({at(i - 1, j) + 1, at(i, j - 1) + 1, at(i - 1, j - 1) + (same ? 0 : 1)});
        if (i > 1 && j > 1 && lower(word[i - 1]) == lower(candidate[j - 2]) &&
            lower(word[i - 2]) == lower(candidate[j - 1])) {
          at(i, j) = std::min(at(i, j), at(i - 2, j - 2) + 1);
        }
      }
    }
    if (at(w, c) < best_distance) {
      best_distance = at(w, c);
      best = candidate;
    }
  }
  // Close enough: a third of the word, and at least one edit.
  if (!best || best_distance == 0 || best_distance > std::max<size_t>(1, best->size() / 3)) return std::nullopt;
  return best;
}

std::string TimeText(uint64_t hundredths) {
  const uint64_t minutes = hundredths / 6000;
  const uint64_t seconds = hundredths / 100 % 60;
  return std::format("{}:{:02}.{:02}", minutes, seconds, hundredths % 100);
}

}  // namespace gw2::custom_mode
