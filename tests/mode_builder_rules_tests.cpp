// Mode builder tests: rows step through short lists and come back round, X resets to the game's own, rules that don't
// apply to the base are kept but never written, every state the builder can reach writes a file the parser reads back
// as the same mode, and folder names are safe and unique. Pure; no game.

#include <cstdio>
#include <random>
#include <string>

#include "mode_builder_rules.h"

using namespace gw2::mode_builder;
namespace rules = gw2::custom_mode;

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

rules::Mode Evolved() {
  return {"My mode", "Omenhachi", "", rules::Rules{.base = 1}};
}

// The file the builder would write, read back.
std::optional<rules::Mode> RoundTrip(const Builder& b) {
  const rules::ParseResult r = rules::Parse(rules::WriteModeFile(b.Mode(), true));
  return r.errors.empty() ? r.mode : std::nullopt;
}

void Steps() {
  Builder b(Evolved());
  const RowRef lives{Row::kLives};
  CHECK(b.View(lives).value == "Game's own" && b.View(lives).own);
  b.Step(lives, +1);
  CHECK(b.Mode().rules.lives == 1 && b.View(lives).value == "1 ship");
  for (int i = 0; i < 10; ++i) b.Step(lives, +1);
  CHECK(b.Mode().rules.lives == 99);  // stops at the top
  b.Step(lives, -1);
  CHECK(b.Mode().rules.lives == 10);
  b.Reset(lives);
  CHECK(!b.Mode().rules.lives);

  // A hand-written value off the list steps to its neighbours.
  rules::Mode seven = Evolved();
  seven.rules.lives = 7;
  seven.rules.game_speed = 1.23f;
  Builder s(seven);
  Builder s2 = s;
  s.Step(lives, +1);
  s2.Step(lives, -1);
  CHECK(s.Mode().rules.lives == 10 && s2.Mode().rules.lives == 5);
  s.Step({Row::kSpeed}, +1);
  s2.Step({Row::kSpeed}, -1);
  CHECK(s.Mode().rules.game_speed == 25.0f / 20.0f && s2.Mode().rules.game_speed == 24.0f / 20.0f);

  // Speed stays inside the parser's range and on its steps.
  Builder fast(Evolved());
  for (int i = 0; i < 40; ++i) fast.Step({Row::kSpeed}, +1);
  CHECK(fast.Mode().rules.game_speed == rules::kMaxSpeed);
  for (int i = 0; i < 40; ++i) fast.Step({Row::kSpeed}, -1);
  CHECK(fast.Mode().rules.game_speed == rules::kMinSpeed);

  // Time: Off, then minutes.
  Builder t(Evolved());
  t.Step({Row::kTimeLimit}, +1);
  CHECK(t.Mode().rules.time_limit == 60.0f && t.View({Row::kTimeLimit}).value == "1 min");
  t.Step({Row::kTimeLimit}, -1);
  CHECK(!t.Mode().rules.time_limit && t.View({Row::kTimeLimit}).value == "Off");

  // Base goes round all six.
  Builder base(Evolved());
  for (int i = 0; i < 6; ++i) base.Step({Row::kBase}, +1);
  CHECK(base.base() == 1);
  base.Step({Row::kBase}, -1);
  CHECK(base.base() == 0 && base.View({Row::kBase}).value == "Deadline");
}

void Enemies() {
  Builder b(Evolved());
  const RowRef grunt{Row::kEnemy, uint8_t(rules::Enemy::kGrunt)};
  CHECK(b.View(grunt).value == "On");
  b.Step(grunt, +1);
  CHECK(b.View(grunt).value == "Off" && b.Mode().rules.EnemyOff(rules::Enemy::kGrunt));
  b.Step(grunt, +1);  // the first swap target that isn't a grunt: wanderer
  CHECK(b.View(grunt).value == "As wanderer" && b.Mode().rules.swap_to[size_t(rules::Enemy::kGrunt)] == 1);
  CHECK(!b.Mode().rules.EnemyOff(rules::Enemy::kGrunt));
  b.Step(grunt, +1);  // grunt itself is skipped
  CHECK(b.View(grunt).value == "As weaver");
  b.Step(grunt, -1);
  b.Step(grunt, -1);
  b.Step(grunt, -1);  // back past Off to On, then round to the last target
  CHECK(b.View(grunt).value == "On" || b.View(grunt).value.starts_with("As type"));
  // An unused enemy shows its looks in amber.
  Builder u(Evolved());
  for (int i = 0; i < 20 && u.Mode().rules.swap_to[size_t(rules::Enemy::kGrunt)] != 17; ++i) u.Step(grunt, +1);
  CHECK(u.View(grunt).value == "As type 17" && u.View(grunt).note == "Untested: green rounded hexagons");
  b.Reset(grunt);
  CHECK(b.View(grunt).value == "On" && b.View(grunt).own);

  // Sequence and Waves: no enemy rows, a reason, and the enemy rules are kept for switching back but not written.
  Builder keep(Evolved());
  keep.Step(grunt, +1);  // grunts off
  keep.Step({Row::kBase}, +1);  // Sequence
  CHECK(keep.Rows(Tab::kEnemies).empty() && !keep.EnemiesReason().empty());
  CHECK(!keep.Mode().rules.Tuned());
  CHECK(RoundTrip(keep).has_value());
  keep.Step({Row::kBase}, -1);  // back to Evolved
  CHECK(keep.Mode().rules.EnemyOff(rules::Enemy::kGrunt));
  // Level only in Sequence, and never written elsewhere.
  Builder seq(Evolved());
  seq.Step({Row::kBase}, +1);
  CHECK(seq.Rows(Tab::kRules).back().row == Row::kLevel);
  seq.Step({Row::kLevel}, -1);
  CHECK(seq.Mode().rules.level == 20);
  seq.Step({Row::kBase}, +1);  // Waves
  CHECK(!seq.Mode().rules.level && RoundTrip(seq).has_value());
  // Turning every enemy off writes none of them off (the parser refuses an empty arena).
  Builder all(Evolved());
  for (uint8_t i = 0; i < rules::kEnemyCount; ++i) all.Step({Row::kEnemy, i}, +1);
  CHECK(all.Mode().rules.enemies_off == 0 && RoundTrip(all).has_value());
}

// Whatever a player presses, the file it writes reads back as the same mode.
void EveryState() {
  std::mt19937 random(9);
  int good = 0;
  constexpr int kRuns = 2000;
  for (int n = 0; n < kRuns; ++n) {
    Builder b(Evolved());
    for (int press = 0; press < 40; ++press) {
      const Tab tab = random() % 3 == 0 ? Tab::kEnemies : Tab::kRules;
      const auto rows = b.Rows(tab);
      if (rows.empty()) continue;
      const RowRef row = rows[random() % rows.size()];
      if (random() % 6 == 0) b.Reset(row);
      else b.Step(row, random() % 2 ? 1 : -1);
    }
    const auto back = RoundTrip(b);
    good += back && back->rules == b.Mode().rules && back->name == b.Mode().name &&
            rules::Id(back->rules) == rules::Id(b.Mode().rules);
  }
  CHECK(good == kRuns);
}

void Names() {
  Builder b(Evolved());
  b.Roll(+1);
  const std::string first = b.name();
  b.Roll(+1);
  CHECK(b.name() != first && !b.name().empty());
  b.Roll(-1);
  CHECK(b.name() == first);  // Left walks back
  // With grunts swapped into weavers, the name says so.
  b.Step({Row::kEnemy, uint8_t(rules::Enemy::kGrunt)}, +1);
  for (int i = 0; i < 2; ++i) b.Step({Row::kEnemy, uint8_t(rules::Enemy::kGrunt)}, +1);
  b.Roll(+1);
  CHECK(b.name().find("Weaver") != std::string::npos);

  CHECK(FolderFor("Neon Weaver Rush", {}) == "neon-weaver-rush");
  CHECK(FolderFor("Neon Weaver Rush", {"neon-weaver-rush"}) == "neon-weaver-rush-2");
  CHECK(FolderFor("  !!  ", {}) == "my-mode");
  CHECK(FolderFor("Héllo / World", {}) == "h-llo-world");
}

}  // namespace

int main() {
  Steps();
  Enemies();
  EveryState();
  Names();
  std::printf("mode_builder_rules_tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
