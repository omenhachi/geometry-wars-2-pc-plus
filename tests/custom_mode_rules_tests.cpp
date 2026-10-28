// Custom mode rules tests: mode files read strictly (unknown keys, wrong types and bad bases are errors with their line
// and a suggestion; out-of-range values are brought into range with a warning; a newer format says so), the id
// ignores the name, and the rules survive writing and reading while damaged bytes are refused. Pure; no game.

#include <cstdio>
#include <random>
#include <string>

#include "custom_mode_rules.h"

using namespace gw2::custom_mode;

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

bool HasError(const ParseResult& r, uint32_t line, const std::string& part) {
  for (const auto& e : r.errors) {
    if (e.line == line && e.text.find(part) != std::string::npos) return true;
  }
  return false;
}

void PurePacifism() {
  const ParseResult r = Parse(R"(format = 1
name = "Pure Pacifism"
author = "Light"
description = "No gates."
base = "pacifism"

[rules]
gates = false
score = "time"
)");
  CHECK(r.errors.empty());
  CHECK(r.warnings.empty());
  CHECK(r.mode.has_value());
  if (!r.mode) return;
  CHECK(r.mode->name == "Pure Pacifism");
  CHECK(r.mode->author == "Light");
  CHECK(r.mode->rules.base == 4);
  CHECK(!r.mode->rules.gates);
  CHECK(r.mode->rules.score == Score::kTime);
  CHECK(!r.mode->rules.lives);
  CHECK(r.mode->rules.game_speed == 1.0f);
  const auto chips = Chips(r.mode->rules);
  CHECK(chips.size() == 2 && chips[0] == "No gates" && chips[1] == "Score: time");
}

void Strictness() {
  const ParseResult typo = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nlifes = 1\n");
  CHECK(!typo.mode);
  CHECK(HasError(typo, 5, "did you mean \"lives\""));

  const ParseResult top = Parse("format = 1\nnmae = \"x\"\nbase = \"evolved\"\n");
  CHECK(!top.mode);
  CHECK(HasError(top, 2, "did you mean \"name\""));

  const ParseResult base = Parse("format = 1\nname = \"x\"\nbase = \"pacifsm\"\n");
  CHECK(!base.mode);
  CHECK(HasError(base, 3, "did you mean \"pacifism\""));
  CHECK(base.errors.size() == 1);  // no second "choose a base" error

  const ParseResult upper = Parse("format = 1\nname = \"x\"\nbase = \"Deadline\"\n");
  CHECK(upper.mode && upper.mode->rules.base == 0);

  const ParseResult type = Parse("format = 1\nname = \"x\"\nbase = \"waves\"\n[rules]\nlives = \"three\"\n");
  CHECK(!type.mode);
  CHECK(HasError(type, 5, "whole number"));

  const ParseResult score = Parse("format = 1\nname = \"x\"\nbase = \"waves\"\n[rules]\nscore = \"tim\"\n");
  CHECK(HasError(score, 5, "did you mean \"time\""));

  const ParseResult broken = Parse("format = 1\nname = \"x\n");
  CHECK(!broken.mode);
  CHECK(!broken.errors.empty() && broken.errors[0].line == 2);

  const ParseResult missing = Parse("name = \"x\"\n");
  CHECK(!missing.mode);
  CHECK(HasError(missing, 0, "format = 1"));
  CHECK(HasError(missing, 0, "base mode"));

  const ParseResult newer = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\nsparkles = true\n");
  CHECK(!newer.mode);
  CHECK(newer.newer_format);
  CHECK(newer.errors.size() == 1);  // the unknown key is not blamed on the author
}

void Clamping() {
  const ParseResult r = Parse(
      "format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nlives = 500\nbombs = -2\ngame_speed = 9\n"
      "time_limit = 1\n");
  CHECK(r.errors.empty());
  CHECK(r.warnings.size() == 4);
  CHECK(r.mode && *r.mode->rules.lives == kMaxLives && *r.mode->rules.bombs == 0);
  CHECK(r.mode && r.mode->rules.game_speed == kMaxSpeed && *r.mode->rules.time_limit == kMinTimeLimit);
  const ParseResult whole = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\ngame_speed = 1\n");
  CHECK(whole.mode && whole.mode->rules.game_speed == 1.0f && whole.warnings.empty());
  const ParseResult name = Parse(std::string("format = 1\nbase = \"evolved\"\nname = \"") + std::string(100, 'a') +
                                 "é\"\n");
  CHECK(name.mode && name.mode->name.size() == 64 && name.warnings.size() == 1);
}

void Ids() {
  Rules a;
  a.base = 1;
  a.lives = 1;
  Rules b = a;
  CHECK(Id(a) == Id(b));
  CHECK(Id(a).size() == 16);
  b.lives = 2;
  CHECK(Id(a) != Id(b));
  const ParseResult one = Parse("format = 1\nname = \"One\"\nbase = \"evolved\"\n[rules]\nlives = 1\n");
  const ParseResult two = Parse("format = 1\nname = \"Two\"\ndescription = \"z\"\nbase = \"evolved\"\n[rules]\nlives = 1\n");
  CHECK(one.mode && two.mode && Id(one.mode->rules) == Id(two.mode->rules));
}

void Bytes() {
  Rules r;
  r.base = 5;
  r.lives = 7;
  r.bombs = 0;
  r.time_limit = 120.0f;
  r.game_speed = 1.25f;
  r.gates = false;
  r.infinite_bombs = true;
  r.score = Score::kTime;
  r.extra_lives = false;
  r.extra_bombs = false;
  r.bonus_bombs_at = {100'000, 1'000'000, 10'000'000, 100'000'000};
  const auto bytes = Write(r);
  CHECK(bytes.size() == kRulesBytes + 1 + 4 * 8);
  CHECK(bytes.size() <= kMaxRulesBytes);
  const auto back = Read(bytes);
  CHECK(back && *back == r);
  CHECK(Read(Write(Rules{})) == Rules{});
  // Every single-byte change is either refused or reads back as some valid rules; never a crash.
  std::mt19937 random(7);
  for (size_t i = 0; i < bytes.size(); ++i) {
    for (int n = 0; n < 32; ++n) {
      auto damaged = bytes;
      damaged[i] = uint8_t(random());
      if (const auto read = Read(damaged)) CHECK(Write(*read) == damaged);
    }
  }
  auto short_bytes = bytes;
  short_bytes.pop_back();
  CHECK(!Read(short_bytes));
}

// Rules a version 1 can hold are still written as version 1, so the ids of existing modes don't change; lives are
// ships in all from version 2, and a version 1 replay's lives keep their old meaning (spare ships).
void Versions() {
  Rules pure;
  pure.base = 4;
  pure.gates = false;
  pure.score = Score::kTime;
  CHECK(Write(pure).size() == kRulesBytes && Write(pure)[0] == 1);

  Rules one;
  one.base = 1;
  one.lives = 1;
  CHECK(Write(one)[0] == 2);
  CHECK(one.SpareLives() == 0);
  const auto again = Read(Write(one));
  CHECK(again && !again->legacy_spare_lives && again->SpareLives() == 0);

  auto old = Write(one);
  old[0] = 1;
  old.resize(kRulesBytes);
  const auto legacy = Read(old);
  CHECK(legacy && legacy->legacy_spare_lives && legacy->SpareLives() == 1);
  CHECK(Write(*legacy) == old);
  CHECK(Chips(*legacy)[0] == "2 lives");
  CHECK(Chips(one)[0] == "1 life");

  // A version 2 block that version 1 could have held is refused: it is never written.
  auto padded = Write(pure);
  padded[0] = 2;
  padded.push_back(0);
  CHECK(!Read(padded));
  // Bonus scores must ascend.
  Rules bonus;
  bonus.bonus_bombs_at = {5, 9};
  auto swapped = Write(bonus);
  std::swap(swapped[kRulesBytes + 1], swapped[kRulesBytes + 9]);
  CHECK(!Read(swapped));
}

void NewRules() {
  const ParseResult r = Parse(R"(format = 1
name = "Master Evolved"
base = "evolved"
[rules]
lives = 1
extra_lives = false
extra_bombs = false
bonus_bombs_at = [100000000, 100000, 10000000, 1000000, 100000]
)");
  CHECK(r.errors.empty());
  CHECK(r.mode && r.mode->rules.bonus_bombs_at == std::vector<uint64_t>({100'000, 1'000'000, 10'000'000, 100'000'000}));
  CHECK(r.mode && !r.mode->rules.extra_lives && !r.mode->rules.extra_bombs);
  const auto chips = r.mode ? Chips(r.mode->rules) : std::vector<std::string>{};
  CHECK(chips.size() == 4 && chips[0] == "1 life" && chips[1] == "No extra lives" && chips[2] == "No milestone bombs" &&
        chips[3] == "Bombs at 100K 1M 10M 100M");

  const ParseResult bad = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nbonus_bombs_at = 100\n");
  CHECK(!bad.mode && HasError(bad, 5, "list of scores"));
  const ParseResult zero = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nbonus_bombs_at = [0, 5]\n");
  CHECK(!zero.mode && HasError(zero, 5, "whole numbers"));
  const ParseResult many = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nbonus_bombs_at = [1,2,3,4,5,6,7,8,9,10]\n");
  CHECK(many.mode && many.mode->rules.bonus_bombs_at.size() == kMaxBonusBombs && many.warnings.size() == 1);
  const ParseResult typo = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nextra_live = false\n");
  CHECK(HasError(typo, 5, "did you mean \"extra_lives\""));

  // A Sequence level: just that level. Only Sequence has levels; the level rides in the once-reserved word.
  const ParseResult level = Parse("format = 1
name = \"x\"
base = \"sequence\"
[rules]
level = 14
");
  CHECK(level.mode && level.mode->rules.level == 14);
  CHECK(level.mode && Chips(level.mode->rules) == std::vector<std::string>({"Level 14"}));
  if (level.mode) {
    const auto bytes = Write(level.mode->rules);
    CHECK(bytes.size() == kRulesBytes && bytes[20] == 14);
    CHECK(Read(bytes) == level.mode->rules);
    CHECK(Id(level.mode->rules) != Id(Rules{.base = kSequenceBase}));
    auto elsewhere = bytes;
    elsewhere[1] = 1;
    CHECK(!Read(elsewhere));  // a level outside Sequence
    auto past = bytes;
    past[20] = 21;
    CHECK(!Read(past));
  }
  const ParseResult high = Parse("format = 1
name = \"x\"
base = \"sequence\"
[rules]
level = 25
");
  CHECK(high.mode && high.mode->rules.level == 20 && high.warnings.size() == 1);
  const ParseResult evolved = Parse("format = 1
name = \"x\"
base = \"evolved\"
[rules]
level = 3
");
  CHECK(!evolved.mode && HasError(evolved, 0, "only for Sequence"));

  CHECK(ShortScore(100'000) == "100K" && ShortScore(1'000'000) == "1M" && ShortScore(2'500'000) == "2.5M");
  CHECK(ShortScore(999) == "999" && ShortScore(1'500) == "1.5K" && ShortScore(3'000'000'000) == "3B");
}

void Text() {
  CHECK(TimeText(0) == "0:00.00");
  CHECK(TimeText(12345) == "2:03.45");
  CHECK(TimeText(360000) == "60:00.00");
  constexpr std::string_view known[] = {"lives", "bombs"};
  CHECK(Nearest("lifes", known) == "lives");
  CHECK(!Nearest("sparkles", known));
  CHECK(!Nearest("lives", known));
}

}  // namespace

int main() {
  PurePacifism();
  Strictness();
  Clamping();
  Ids();
  Bytes();
  Versions();
  NewRules();
  Text();
  std::printf("custom_mode_rules_tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
