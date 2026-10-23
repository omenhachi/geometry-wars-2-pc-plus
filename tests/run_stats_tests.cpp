// Run stats tests: kills and points by type, black hole and gate points, the best moment, and the score samples. Pure;
// no game or runtime.

#include <cstdio>

#include "run_stats.h"

using namespace gw2::run_stats;

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

constexpr int32_t kGrunt = 2;
constexpr int32_t kWanderer = 18;
constexpr int32_t kBullet = 7;  // any killer that is not a gate

void TestKills() {
  Run run;
  run.Kill(1.0f, kGrunt, kBullet, 50);  // before Start: ignored
  CHECK(run.by_type().empty());
  run.Start(0, 0.0f);
  run.Kill(1.0f, kGrunt, kBullet, 50);
  run.Kill(2.0f, kGrunt, kBullet, 100);
  run.Kill(3.0f, kWanderer, kBullet, 25);
  CHECK(run.by_type().at(kGrunt).kills == 2);
  CHECK(run.by_type().at(kGrunt).points == 150);
  CHECK(run.by_type().at(kWanderer).kills == 1);
  CHECK(run.kill_points() == 175);
  CHECK(run.black_hole_points() == 0 && run.gate_points() == 0);
  // A black hole, a gate, and a grunt caught in a gate's blast.
  run.Kill(4.0f, kBlackHole, kBullet, 6000);
  run.Kill(5.0f, kGate, kBullet, 400);
  run.Kill(5.0f, kGrunt, kGateBlast, 250);
  CHECK(run.black_hole_points() == 6000);
  CHECK(run.gate_points() == 650);
  CHECK(run.by_type().at(kGrunt).kills == 3);
  CHECK(run.best().type == kBlackHole && run.best().points == 6000 && run.best().time == 4.0f);
  // Grunts (400 points) before wanderers (25); black holes and gates have their own rows.
  const auto ranked = run.RankedEnemies();
  CHECK(ranked.size() == 2);
  CHECK(ranked[0].type == kGrunt && ranked[0].tally.points == 400);
  CHECK(ranked[1].type == kWanderer);
  // A new run starts clean.
  run.Start(4, 0.0f);
  CHECK(run.by_type().empty() && run.kill_points() == 0 && run.best().points == 0 && run.mode() == 4);
}

void TestSamples() {
  Run run;
  run.Start(0, 10.0f);
  CHECK(run.samples().size() == 1 && run.samples()[0].score == 0);
  run.Observe(10.1f, 100);
  CHECK(run.samples().size() == 2);
  // Within a quarter second the latest score replaces the newest sample.
  run.Observe(10.2f, 150);
  CHECK(run.samples().size() == 2 && run.samples().back().score == 150);
  run.Observe(10.3f, 200);
  CHECK(run.samples().size() == 3 && run.final_score() == 200);
  // A paused game repeats the same time: no new samples.
  for (int i = 0; i < 100; ++i) run.Observe(10.3f, 200);
  CHECK(run.samples().size() == 3);
  run.Death(11.0f);
  run.Bomb(12.0f);
  CHECK(run.deaths().size() == 1 && run.bombs().size() == 1);
  run.Finish();
  run.Observe(13.0f, 999);
  run.Death(13.0f);
  CHECK(!run.active() && run.final_score() == 200 && run.deaths().size() == 1);
}

void TestBlasts() {
  Run run;
  run.Start(4, 0.0f);
  // Two blasts: the first catches three grunts, the second five.
  for (int i = 0; i < 3; ++i) run.Kill(1.0f, kGrunt, kGateBlast, 100, 0x1000);
  CHECK(run.best_blast().kills == 3 && run.best_blast().points == 300);
  for (int i = 0; i < 5; ++i) run.Kill(2.0f, kGrunt, kGateBlast, 200, 0x2000);
  CHECK(run.best_blast().kills == 5 && run.best_blast().points == 1000);
  // A smaller third blast, even one reusing the first's address, leaves the best alone.
  run.Kill(3.0f, kGrunt, kGateBlast, 50, 0x1000);
  CHECK(run.best_blast().kills == 5);
  // The same blast object a while later is a new blast.
  for (int i = 0; i < 9; ++i) run.Kill(10.0f, kGrunt, kGateBlast, 10, 0x2000);
  CHECK(run.best_blast().kills == 5);
  // Bullet kills between blasts are not part of any.
  run.Kill(3.5f, kGrunt, kBullet, 9999, 0x3000);
  CHECK(run.best_blast().points == 1000);
  run.Observe(53.0f, 1);
  CHECK(run.duration() == 53.0f);
}

void TestDescribe() {
  Run run;
  run.Start(0, 0.0f);
  run.Kill(1.0f, kGrunt, kBullet, 50);
  run.Observe(1.0f, 75);
  const auto lines = run.Describe();
  CHECK(lines.size() == 4);
  CHECK(lines.back().find("other 25") != std::string::npos);
}

}  // namespace

int main() {
  TestKills();
  TestSamples();
  TestBlasts();
  TestDescribe();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
