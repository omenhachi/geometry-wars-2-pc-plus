// Deadline rank tests: the next wave's rank, checked against the worked examples in the rank cards, and the pop after a
// wave. Pure maths; no game or runtime.

#include <cstdio>

#include "deadline_rank.h"

using namespace gw2::deadline_rank;

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

void TestNextRank() {
  // The card's example: old rank 20, K 25, G 10: target 25 + 5 + 2 = 32, limit 35.
  CHECK(NextRank({.k = 25, .g = 10, .rank = 20}) == 32);
  // The limit holds the target back: 20 * 1.5 + 5 = 35.
  CHECK(NextRank({.k = 100, .g = 0, .rank = 20}) == 35);
  // G is halved and rounded down.
  CHECK(NextRank({.k = 0, .g = 11, .rank = 0}) == 5);
  // A quiet wave keeps the old rank rather than lowering it.
  CHECK(NextRank({.k = 0, .g = 0, .rank = 40}) == 40);
  // From a fresh start the limit is 5, and the bare +2 is the floor.
  CHECK(NextRank({.k = 0, .g = 0, .rank = 0}) == 2);
  CHECK(NextRank({.k = 9, .g = 0, .rank = 0}) == 5);
  // Odd ranks round the limit down: 7 * 1.5 + 5 = 15.5.
  CHECK(NextRank({.k = 50, .g = 0, .rank = 7}) == 15);
  // A mixed trio is 7 K (4 + 2 + 1).
  CHECK(NextRank({.k = 4 + 2 + 1, .g = 0, .rank = 4}) == 9);
  CHECK(NextRank({.k = 4 + 2 + 1, .g = 0, .rank = 10}) == 10);
}

void TestWavePop() {
  WavePop pop;
  pop.Observe({.k = 25, .g = 10, .rank = 20, .waves = 3}, 0.0);
  CHECK(!pop.Gain(0.0));
  // A wave lands and raises the rank by 12.
  pop.Observe({.k = 0, .g = 0, .rank = 32, .waves = 4}, 1.0);
  CHECK(pop.Gain(1.0) == 12);
  CHECK(pop.Strength(1.0) > 0.99f);
  CHECK(pop.Strength(1.8) > 0.0f && pop.Strength(1.8) < 1.0f);
  CHECK(!pop.Gain(1.0 + WavePop::kSeconds));
  // A bomb cuts the rank: no pop.
  pop.Observe({.k = 0, .g = 0, .rank = 22, .waves = 4}, 5.0);
  CHECK(!pop.Gain(5.0));
  // A wave that keeps the rank: no pop.
  pop.Observe({.k = 0, .g = 0, .rank = 22, .waves = 5}, 6.0);
  CHECK(!pop.Gain(6.0));
  // The first look at a round in progress never pops.
  WavePop fresh;
  fresh.Observe({.rank = 50, .waves = 9}, 0.0);
  CHECK(!fresh.Gain(0.0));
  // A new round resets the count without a pop.
  pop.Observe({.k = 0, .g = 0, .rank = 0, .waves = 0}, 7.0);
  CHECK(!pop.Gain(7.0));
  pop.Observe({.k = 3, .g = 0, .rank = 5, .waves = 1}, 8.0);
  CHECK(pop.Gain(8.0) == 5);
}

}  // namespace

int main() {
  TestNextRank();
  TestWavePop();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
