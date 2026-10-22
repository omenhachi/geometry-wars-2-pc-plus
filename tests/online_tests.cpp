// Online leaderboards: player names, online xuids, reading the server's boards and picking the players around you.
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

// A board of players scoring 100, 200, ... (rank order), with the local player among them at `mine` when given.
gw2::live::StatsView Board(int players, int64_t mine = -1) {
  gw2::live::StatsView board;
  for (int i = players; i >= 1; --i) {
    board.rows.push_back({kOnlineXuidTag | uint64_t(i), 0, int64_t(i) * 100, "P" + std::to_string(i)});
  }
  if (mine >= 0) board.rows.push_back({kLocalXuid, 0, mine, "Me"});
  return board;
}

std::string Scores(const std::vector<gw2::live::StatsRow>& rows) {
  std::string out;
  for (const auto& row : rows) out += std::to_string(row.rating) + " ";
  return out;
}

void TestNearby() {
  // 3 just above, 2 at or below; the local player's own server row is never repeated.
  CHECK(Scores(PickNearby(Board(20, 1050), 1050, kLocalXuid)) == "1100 1200 1300 1000 900 ");
  // Ties go below: the score to beat is strictly above.
  CHECK(Scores(PickNearby(Board(20), 1000, kLocalXuid)) == "1100 1200 1300 1000 900 ");
  // At the top: the five just below. Near the top: what is above, the rest below.
  CHECK(Scores(PickNearby(Board(20), 5000, kLocalXuid)) == "2000 1900 1800 1700 1600 ");
  CHECK(Scores(PickNearby(Board(20), 1950, kLocalXuid)) == "2000 1900 1800 1700 1600 ");
  // At the bottom, or a mode never played: the lowest scores, the nearest first.
  CHECK(Scores(PickNearby(Board(20), 50, kLocalXuid)) == "100 200 300 400 500 ");
  CHECK(Scores(PickNearby(Board(20), 0, kLocalXuid)) == "100 200 300 400 500 ");
  // Small boards give what they have; an empty board or only the player gives nothing.
  CHECK(Scores(PickNearby(Board(2), 150, kLocalXuid)) == "200 100 ");
  CHECK(PickNearby(Board(0), 0, kLocalXuid).empty());
  CHECK(PickNearby(Board(0, 500), 500, kLocalXuid).empty());
  for (const auto& row : PickNearby(Board(3, 150), 150, kLocalXuid)) CHECK(row.xuid != kLocalXuid);
}

}  // namespace

int main() {
  TestNames();
  TestXuids();
  TestBoards();
  TestNearby();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
