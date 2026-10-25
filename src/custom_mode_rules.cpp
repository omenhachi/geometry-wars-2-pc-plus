#include "custom_mode_rules.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>

#include <toml++/toml.hpp>

namespace gw2::custom_mode {
namespace {

constexpr std::string_view kTopKeys[] = {"format", "name", "author", "description", "base", "rules"};
constexpr std::string_view kRuleKeys[] = {"lives",          "bombs", "time_limit",  "game_speed",  "gates",
                                          "infinite_bombs", "score", "extra_lives", "extra_bombs", "bonus_bombs_at"};
constexpr std::string_view kScores[] = {"points", "time"};
constexpr size_t kMaxText = 64;
constexpr size_t kMaxDescription = 200;

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
// scores follows (u8 count, then each u64).
constexpr uint8_t kBytesVersion1 = 1;
constexpr uint8_t kBytesVersion2 = 2;

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
  if (const toml::node* format = table.get("format")) {
    const auto* value = format->as_integer();
    if (!value || value->get() < 1) {
      reader.Error(LineOf(*format), "\"format\" should be 1");
      return result;
    }
    if (value->get() > kFormat) {
      result.newer_format = true;
      reader.Error(LineOf(*format), "This mode needs a newer version of PC Plus");
      return result;
    }
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
    } else {
      reader.Unknown(key, kTopKeys, "mode");
    }
  }
  if (mode.name.empty()) reader.Error(0, "Give the mode a name, like name = \"My mode\"");
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
  const bool newer = !rules.extra_lives || !rules.extra_bombs || !rules.bonus_bombs_at.empty();
  const bool version1 = !newer && (!rules.lives || rules.legacy_spare_lives);
  std::vector<uint8_t> out;
  out.reserve(kMaxRulesBytes);
  out.push_back(version1 ? kBytesVersion1 : kBytesVersion2);
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
  Put32(out, 0);
  if (!version1) {
    const size_t count = std::min(rules.bonus_bombs_at.size(), kMaxBonusBombs);
    out.push_back(uint8_t(count));
    for (size_t i = 0; i < count; ++i) Put64(out, rules.bonus_bombs_at[i]);
  }
  return out;
}

std::optional<Rules> Read(std::span<const uint8_t> bytes) {
  if (bytes.size() < kRulesBytes) return std::nullopt;
  const uint8_t version = bytes[0];
  if (version != kBytesVersion1 && version != kBytesVersion2) return std::nullopt;
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
  if (Get32(bytes, 20) != 0) return std::nullopt;
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
  if (version == kBytesVersion2) {
    if (bytes.size() < kRulesBytes + 1) return std::nullopt;
    const size_t count = bytes[kRulesBytes];
    if (count > kMaxBonusBombs || bytes.size() != kRulesBytes + 1 + count * 8) return std::nullopt;
    for (size_t i = 0; i < count; ++i) {
      const uint64_t score = Get64(bytes, kRulesBytes + 1 + i * 8);
      if (score < 1 || score > kMaxBonusScore || (i > 0 && score <= rules.bonus_bombs_at.back())) return std::nullopt;
      rules.bonus_bombs_at.push_back(score);
    }
    // Version 2 is only written when version 1 can't hold the rules.
    if (rules.extra_lives && rules.extra_bombs && rules.bonus_bombs_at.empty() && !rules.lives) return std::nullopt;
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
