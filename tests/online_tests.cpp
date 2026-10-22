// Online leaderboards: player names, online xuids and reading the server's boards.
// Run: online_tests

#include <cstdio>
#include <string>

#include "online_leaderboard.h"

using namespace gw2::online;

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

constexpr uint64_t kLocalXuid = 0xB13EBABEBABEBABEull;

void TestNames() {
  std::printf("player names follow the server's rules\n");
  CHECK(ValidName("Omenhachi") == "Omenhachi");
  CHECK(ValidName("  Omen Hachi  ") == "Omen Hachi");
  CHECK(ValidName("a_b.c-d 1") == "a_b.c-d 1");
  CHECK(ValidName("123456789012345") == "123456789012345");
  CHECK(ValidName("1234567890123456").empty());
  CHECK(ValidName("").empty());
  CHECK(ValidName("   ").empty());
  CHECK(ValidName("Bad!").empty());
  CHECK(ValidName("Zo\xC3\xAB").empty());  // accents: the game's font cannot draw them
  CHECK(ValidName("a\"b").empty());        // nothing that could break the JSON body
}

void TestXuids() {
  std::printf("online players get tagged xuids\n");
  const uint64_t xuid = XuidForPublicId("3d1bce41a4d7f4de");
  CHECK(xuid != 0);
  CHECK((xuid & 0xFFFF0000'00000000ull) == kOnlineXuidTag);
  CHECK(xuid != kLocalXuid);
  CHECK(XuidForPublicId("3d1bce41a4d7f4de") == xuid);
  CHECK(XuidForPublicId("3d1bce41a4d7f4df") != xuid);
  CHECK(XuidForPublicId("3D1BCE41A4D7F4DE") == 0);
  CHECK(XuidForPublicId("short") == 0);
  CHECK(XuidForPublicId("3d1bce41a4d7f4dz") == 0);
}

void TestBoards() {
  std::printf("server boards become the game's rows\n");
  const std::string json =
      R"({"mode":0,"total":42,"rows":[)"
      R"({"rank":1,"id":"3d1bce41a4d7f4de","name":"Omenhachi","score":8626655,"input":"mouse"},)"
      R"({"rank":2,"id":"0123456789abcdef","name":"Rival","score":5000,"input":"controller"}]})";
  const auto board = ParseBoard(json, 0, "3d1bce41a4d7f4de", kLocalXuid);
  CHECK(board.has_value());
  if (board) {
    CHECK(board->id == 0);
    CHECK(board->total_rows == 42);
    CHECK(board->rows.size() == 2);
    CHECK(board->rows[0].xuid == kLocalXuid);  // the player's own row, so the game highlights it
    CHECK(board->rows[0].rank == 1);
    CHECK(board->rows[0].rating == 8626655);
    CHECK(board->rows[0].gamertag == "Omenhachi");
    CHECK(board->rows[1].xuid == XuidForPublicId("0123456789abcdef"));
    CHECK(board->rows[1].gamertag == "Rival");
  }

  std::printf("an empty board\n");
  const auto empty = ParseBoard(R"({"mode":3,"total":0,"rows":[]})", 3, "", kLocalXuid);
  CHECK(empty && empty->rows.empty() && empty->total_rows == 0 && empty->id == 3);

  std::printf("anything else falls back to the local board\n");
  CHECK(!ParseBoard("", 0, "", kLocalXuid));
  CHECK(!ParseBoard("<html>error</html>", 0, "", kLocalXuid));
  CHECK(!ParseBoard(R"({"error":"server error"})", 0, "", kLocalXuid));
  CHECK(!ParseBoard(R"({"total":1,"rows":[{"rank":1,"id":"bad","name":"X","score":1}]})", 0, "", kLocalXuid));
  CHECK(!ParseBoard(R"({"total":1,"rows":[{"rank":1,"id":"0123456789abcdef","name":"Bad!","score":1}]})", 0, "",
                    kLocalXuid));
  CHECK(!ParseBoard(R"({"total":1,"rows":[{"rank":1,"id":"0123456789abcdef","name":"X"}]})", 0, "", kLocalXuid));
}

}  // namespace

int main() {
  TestNames();
  TestXuids();
  TestBoards();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
