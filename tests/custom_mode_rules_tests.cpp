// Custom mode rules tests: mode files read strictly (unknown keys, wrong types and bad bases are errors with their line
// and a suggestion; out-of-range values are brought into range with a warning; a newer format says so), the id
// ignores the name, and the rules survive writing and reading while damaged bytes are refused. Pure; no game.

#include <cstdio>
#include <format>
#include <random>
#include <string>
#include <utility>

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

  const ParseResult newer = Parse("format = 3\nname = \"x\"\nbase = \"evolved\"\nsparkles = true\n");
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
  const ParseResult level = Parse("format = 1\nname = \"x\"\nbase = \"sequence\"\n[rules]\nlevel = 14\n");
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
  const ParseResult high = Parse("format = 1\nname = \"x\"\nbase = \"sequence\"\n[rules]\nlevel = 25\n");
  CHECK(high.mode && high.mode->rules.level == 20 && high.warnings.size() == 1);
  const ParseResult evolved = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[rules]\nlevel = 3\n");
  CHECK(!evolved.mode && HasError(evolved, 0, "only for Sequence"));

  CHECK(ShortScore(100'000) == "100K" && ShortScore(1'000'000) == "1M" && ShortScore(2'500'000) == "2.5M");
  CHECK(ShortScore(999) == "999" && ShortScore(1'500) == "1.5K" && ShortScore(3'000'000'000) == "3B");
}

// Format 2's [enemies]: names checked like any key, only turning one off changes the rules, and the set pieces
// (Sequence, Waves) and an empty arena are refused.
void Enemies() {
  const ParseResult r = Parse(R"(format = 2
name = "No Snakes"
base = "evolved"
[enemies]
snake = false
grunt = true
black_hole = false
)");
  CHECK(r.errors.empty());
  CHECK(r.mode && r.mode->rules.EnemyOff(Enemy::kSnake) && r.mode->rules.EnemyOff(Enemy::kBlackHole));
  CHECK(r.mode && !r.mode->rules.EnemyOff(Enemy::kGrunt) && !r.mode->rules.EnemyOff(Enemy::kWanderer));
  CHECK(r.mode && Chips(r.mode->rules) == std::vector<std::string>({"No snakes or black holes"}));

  // All on is no change at all: the same id as the plain base mode.
  const ParseResult same = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies]\nsnake = true\n");
  CHECK(same.mode && !same.mode->rules.Tuned() && Id(same.mode->rules) == Id(Rules{.base = 1}));
  CHECK(same.mode && Write(same.mode->rules).size() == kRulesBytes);

  const ParseResult old = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[enemies]\nsnake = false\n");
  CHECK(!old.mode && HasError(old, 4, "needs format = 2"));
  const ParseResult typo = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies]\nsnakes = false\n");
  CHECK(!typo.mode && HasError(typo, 5, "did you mean \"snake\""));
  const ParseResult word = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies]\nsnake = \"no\"\n");
  CHECK(!word.mode && HasError(word, 5, "true or false"));
  const ParseResult waves = Parse("format = 2\nname = \"x\"\nbase = \"waves\"\n[enemies]\ngrunt = false\n");
  CHECK(!waves.mode && HasError(waves, 0, "can't change Waves"));
  const ParseResult sequence = Parse("format = 2\nname = \"x\"\nbase = \"sequence\"\n[enemies]\ngrunt = false\n");
  CHECK(!sequence.mode && HasError(sequence, 0, "can't change Sequence"));
  const ParseResult empty = Parse(R"(format = 2
name = "x"
base = "evolved"
[enemies]
wanderer = false
grunt = false
weaver = false
spinner = false
snake = false
repulsar = false
black_hole = false
rocket = false
duck = false
)");
  CHECK(!empty.mode && HasError(empty, 0, "every enemy off"));
  const ParseResult newer = Parse("format = 3\nname = \"x\"\nbase = \"evolved\"\n");
  CHECK(newer.newer_format);

  // Most of them off: the chip names the few that are left.
  Rules few{.base = 1};
  few.enemies_off = kAllEnemies & ~(1u << uint8_t(Enemy::kGrunt)) & ~(1u << uint8_t(Enemy::kWanderer));
  CHECK(Chips(few) == std::vector<std::string>({"Only wanderers and grunts"}));

  // The spawner's types.
  CHECK(EnemyForSpawn(1) == Enemy::kWanderer && EnemyForSpawn(2) == Enemy::kGrunt && EnemyForSpawn(3) == Enemy::kWeaver);
  CHECK(EnemyForSpawn(4) == Enemy::kSpinner && EnemyForSpawn(5) == Enemy::kBlackHole);
  CHECK(EnemyForSpawn(6) == Enemy::kRepulsar && EnemyForSpawn(8) == Enemy::kSnake);
  CHECK(EnemyForSpawn(12) == Enemy::kRocket && EnemyForSpawn(13) == Enemy::kRocket && EnemyForSpawn(22) == Enemy::kDuck);
  CHECK(!EnemyForSpawn(16) && !EnemyForSpawn(7) && !EnemyForSpawn(0) && !EnemyForSpawn(21) && !EnemyForSpawn(26));
  // The newest enemies' bits are past the first byte's: they reach the bytes and come back.
  Rules ducks{.base = 1};
  ducks.enemies_off = 1u << uint8_t(Enemy::kDuck);
  CHECK(Read(Write(ducks)) == ducks);
  CHECK(Chips(ducks) == std::vector<std::string>({"No ducks"}));
  const ParseResult rockets = Parse("format = 2\nname = \"x\"\nbase = \"deadline\"\n[enemies]\nrocket = false\nduck = false\n");
  CHECK(rockets.mode && Chips(rockets.mode->rules) == std::vector<std::string>({"No rockets or ducks"}));
}

// Version 3: version 2 with the tuning after it. It survives a round trip, damage is refused or reads back as itself,
// and a tuning version or tag this build doesn't know is refused.
void TunedBytes() {
  Rules r;
  r.base = 1;
  r.lives = 3;
  r.enemies_off = 1u << uint8_t(Enemy::kSnake);
  const auto bytes = Write(r);
  CHECK(bytes[0] == 3);
  CHECK(bytes.size() == kRulesBytes + 1 + 2 + 5);
  CHECK(bytes.size() <= kMaxRulesBytes);
  CHECK(Read(bytes) == r);
  // A tuned mode is a different mode from its untuned rules.
  Rules plain = r;
  plain.enemies_off = 0;
  CHECK(Id(plain) != Id(r));
  CHECK(Write(plain)[0] == 2);

  std::mt19937 random(11);
  for (size_t i = 0; i < bytes.size(); ++i) {
    for (int n = 0; n < 32; ++n) {
      auto damaged = bytes;
      damaged[i] = uint8_t(random());
      if (const auto read = Read(damaged)) CHECK(Write(*read) == damaged);
    }
  }
  const size_t tuning = kRulesBytes + 1;
  auto future = bytes;
  future[tuning] = kTuningVersion + 1;
  CHECK(!Read(future));
  auto unknown_tag = bytes;
  unknown_tag[tuning + 2] = 99;
  CHECK(!Read(unknown_tag));
  auto all_off = bytes;
  all_off[tuning + 3] = uint8_t(kAllEnemies);
  all_off[tuning + 4] = uint8_t(kAllEnemies >> 8);
  CHECK(!Read(all_off));
  auto no_entries = bytes;
  no_entries[tuning + 1] = 0;
  no_entries.resize(tuning + 2);
  CHECK(!Read(no_entries));
  auto longer = bytes;
  longer.push_back(0);
  CHECK(!Read(longer));
  // Version 2 bytes with a tuning tail are refused.
  auto as_two = bytes;
  as_two[0] = 2;
  CHECK(!Read(as_two));
}

void ListChipsCap() {
  Rules r;
  r.base = 1;
  r.lives = 1;
  r.bombs = 0;
  r.time_limit = 120.0f;
  r.game_speed = 1.2f;
  r.enemies_off = 1u << uint8_t(Enemy::kSnake);
  CHECK(Chips(r).size() == 5);
  const auto list = ListChips(r);
  CHECK(list.size() == 4 && list[3] == "+2 more" && list[0] == "1 life");
  CHECK(ListChips(Rules{.base = 1}).empty());
}

// [enemies.swap]: one pass on the enemy the game asked for, only into enemies whose spawn ignores the caller's own
// arguments, with friendly errors and warnings, and the swaps survive the bytes.
void Swaps() {
  const ParseResult green = Parse(R"(format = 2
name = "Green Pacifism"
base = "pacifism"
[enemies.swap]
grunt = "weaver"
)");
  CHECK(green.errors.empty() && green.warnings.empty());
  CHECK(green.mode && green.mode->rules.swap_to[size_t(Enemy::kGrunt)] == 3);
  CHECK(green.mode && Chips(green.mode->rules) == std::vector<std::string>({"Grunts as weavers"}));
  if (green.mode) {
    const auto bytes = Write(green.mode->rules);
    CHECK(bytes[0] == 3);
    CHECK(Read(bytes) == green.mode->rules);
    CHECK(Id(green.mode->rules) != Id(Rules{.base = 4}));
  }

  // Trading two: each is one pass, never followed on.
  const ParseResult trade = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = \"weaver\"\n"
                                  "weaver = \"grunt\"\n");
  CHECK(trade.mode && trade.mode->rules.swap_to[size_t(Enemy::kGrunt)] == 3 &&
        trade.mode->rules.swap_to[size_t(Enemy::kWeaver)] == 2);
  // The game's unused enemies, by spawn type.
  const ParseResult cut = Parse("format = 2\nname = \"x\"\nbase = \"deadline\"\n[enemies.swap]\nwanderer = \"type_17\"\n");
  CHECK(cut.mode && cut.mode->rules.swap_to[size_t(Enemy::kWanderer)] == 17);
  CHECK(cut.mode && Chips(cut.mode->rules) == std::vector<std::string>({"Wanderers as type 17"}));

  // Refused: targets whose spawn reads the caller's arguments, unknown names, lists, the wrong type.
  for (const char* target : {"black_hole", "rocket", "type_14"}) {
    const ParseResult r = Parse(std::format("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = \"{}\"\n", target));
    CHECK(!r.mode && HasError(r, 5, "nothing can be swapped into"));
  }
  const ParseResult typo = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = \"weever\"\n");
  CHECK(!typo.mode && HasError(typo, 5, "did you mean \"weaver\""));
  const ParseResult list = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = [\"weaver\", \"snake\"]\n");
  CHECK(!list.mode && HasError(list, 5, "one enemy for now"));
  const ParseResult source = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunts = \"weaver\"\n");
  CHECK(!source.mode && HasError(source, 5, "did you mean \"grunt\""));
  const ParseResult waves = Parse("format = 2\nname = \"x\"\nbase = \"waves\"\n[enemies.swap]\nrocket = \"weaver\"\n");
  CHECK(!waves.mode && HasError(waves, 0, "can't change Waves"));
  const ParseResult old = Parse("format = 1\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = \"weaver\"\n");
  CHECK(!old.mode && HasError(old, 4, "needs format = 2"));

  // Warnings: into itself (nothing changes), from an enemy that is off, into one that is.
  const ParseResult self = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies.swap]\ngrunt = \"grunt\"\n");
  CHECK(self.mode && !self.mode->rules.Tuned() && self.warnings.size() == 1);
  const ParseResult from_off = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies]\ngrunt = false\n"
                                     "[enemies.swap]\ngrunt = \"weaver\"\n");
  CHECK(from_off.mode && from_off.warnings.size() == 1 && from_off.warnings[0].text.find("does nothing") != std::string::npos);
  const ParseResult into_off = Parse("format = 2\nname = \"x\"\nbase = \"evolved\"\n[enemies]\nweaver = false\n"
                                     "[enemies.swap]\ngrunt = \"weaver\"\n");
  CHECK(into_off.mode && into_off.warnings.size() == 1 &&
        into_off.warnings[0].text.find("still appear") != std::string::npos);

  // Bytes: a swap into an enemy no swap can make, or into itself, is refused; damage reads back as itself or not at all.
  Rules r{.base = 1};
  r.swap_to[size_t(Enemy::kSnake)] = 17;
  r.enemies_off = 1u << uint8_t(Enemy::kDuck);
  const auto bytes = Write(r);
  CHECK(Read(bytes) == r);
  const size_t last_value = bytes.size() - 4;
  auto bad = bytes;
  bad[last_value] = 14;
  CHECK(!Read(bad));
  bad[last_value] = 8;  // the snake's own spawn type
  CHECK(!Read(bad));
  std::mt19937 random(5);
  for (size_t i = 0; i < bytes.size(); ++i) {
    for (int n = 0; n < 32; ++n) {
      auto damaged = bytes;
      damaged[i] = uint8_t(random());
      if (const auto read = Read(damaged)) CHECK(Write(*read) == damaged);
    }
  }
  // Every swap target is something a swap can make, and none of them reads the caller's arguments.
  for (const SwapTarget& t : kSwapTargets) CHECK(t.spawn != 5 && t.spawn != 12 && t.spawn != 13 && t.spawn != 14 && t.spawn != 16);
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

// Every mode PC Plus ships keeps the id it has always had: a mode's best, its replays and the Master boards are found
// by it. Frozen from 1.2.2; a change to how rules are written must leave these alone.
std::string ShippedId(std::string_view base, std::string_view rules_text) {
  const std::string text = std::format("format = 1\nname = \"x\"\nbase = \"{}\"\n[rules]\n{}\n", base, rules_text);
  const ParseResult r = Parse(text);
  return r.mode ? Id(r.mode->rules) : std::string("unreadable");
}

void PinnedIds() {
  Rules master;
  master.lives = 1;
  master.extra_lives = false;
  master.extra_bombs = false;
  master.bonus_bombs_at = {100'000, 1'000'000, 10'000'000, 100'000'000};
  Rules master_deadline = master, master_evolved = master;
  master_deadline.base = 0;
  master_evolved.base = 1;
  // Speed Test, Bomb Party and Sequence practice 14 were checked against a real save's mods/bests.txt.
  CHECK(ShippedId("waves", "bombs = 3\ninfinite_bombs = true\ntime_limit = 120") == "49f64754fbb605df");  // Bomb Party
  CHECK(ShippedId("evolved", "lives = 1\nbombs = 0") == "373556f0ac22c96d");                              // One Life
  CHECK(ShippedId("deadline", "game_speed = 1.3") == "4a070a291f7bce5f");                                 // Overdrive
  CHECK(ShippedId("pacifism", "gates = false\nscore = \"time\"") == "5acc0a9087027482");               // Pure Pacifism
  CHECK(ShippedId("pacifism", "game_speed = 1.4\ngates = false\nscore = \"time\"") == "f1c454077cd4970b");  // Speed Test
  CHECK(Id(master_deadline) == "a89274a6a20fee0f");
  CHECK(Id(master_evolved) == "5fd1275bc87aa3c2");
  CHECK(Id(Rules{.base = kSequenceBase, .level = 1}) == "e7ec45e0b46650ba");
  // Master Pacifism (custom_modes.cpp Masters): Pacifism with grunts as weavers, as Green Pacifism was played.
  Rules master_pacifism{.base = 4};
  master_pacifism.swap_to[size_t(Enemy::kGrunt)] = 3;
  CHECK(Id(master_pacifism) == "37ec9b4747054c3d");
  CHECK(Id(Rules{.base = kSequenceBase, .level = 14}) == "c806e9b70559a1e5");
  // The same rules in a format 2 file: the same mode.
  CHECK(Id(Parse("format = 2\nname = \"x\"\nbase = \"deadline\"\n[rules]\ngame_speed = 1.3\n").mode.value_or(Mode{}).rules) ==
        "4a070a291f7bce5f");
}

}  // namespace

int main() {
  PinnedIds();
  PurePacifism();
  Strictness();
  Clamping();
  Ids();
  Bytes();
  Versions();
  NewRules();
  Enemies();
  TunedBytes();
  ListChipsCap();
  Swaps();
  Text();
  std::printf("custom_mode_rules_tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
