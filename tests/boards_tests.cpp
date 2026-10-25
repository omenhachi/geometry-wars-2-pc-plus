// Boards tests: an official run's scores go to their own view; a Master mode's go to its own board, only from its base
// mode's view, and nothing else of it is kept; a replay's or a custom mode's go nowhere. Pure; no game.

#include <cstdio>

#include "boards.h"

using namespace gw2::boards;

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

}  // namespace

int main() {
  for (uint32_t view = 0; view < kGameBoards; ++view) {
    CHECK(ScoreBoard(Counting::kOfficial, view, 0, 0) == view);
    CHECK(!ScoreBoard(Counting::kNone, view, 0, 0));
  }
  // Master Deadline: Deadline's score write goes to board 6, never to Deadline's board.
  CHECK(ScoreBoard(Counting::kOwnBoard, 0, 0, kMasterDeadlineBoard) == kMasterDeadlineBoard);
  CHECK(ScoreBoard(Counting::kOwnBoard, 0, 1, kMasterEvolvedBoard) == std::nullopt);
  CHECK(ScoreBoard(Counting::kOwnBoard, 1, 1, kMasterEvolvedBoard) == kMasterEvolvedBoard);
  for (uint32_t view = 1; view < kGameBoards; ++view) {
    CHECK(!ScoreBoard(Counting::kOwnBoard, view, 0, kMasterDeadlineBoard));
  }
  CHECK(KeepsProgress(Counting::kOfficial));
  CHECK(!KeepsProgress(Counting::kOwnBoard));
  CHECK(!KeepsProgress(Counting::kNone));
  CHECK(kMasterDeadlineBoard >= kGameBoards && kMasterEvolvedBoard >= kGameBoards && kMasterEvolvedBoard < kBoards);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
