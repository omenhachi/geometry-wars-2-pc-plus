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
  CHECK(Scores(PickNearby(Board(20, 1050), 1050, kLocalXuid).rows) == "1100 1200 1300 1000 900 ");
  // Ties go below: the score to beat is strictly above.
  CHECK(Scores(PickNearby(Board(20), 1000, kLocalXuid).rows) == "1100 1200 1300 1000 900 ");
  // At the top: the five just below. Near the top: what is above, the rest below.
  CHECK(Scores(PickNearby(Board(20), 5000, kLocalXuid).rows) == "2000 1900 1800 1700 1600 ");
  CHECK(Scores(PickNearby(Board(20), 1950, kLocalXuid).rows) == "2000 1900 1800 1700 1600 ");
  // At the bottom, or a mode never played: the lowest scores, the nearest first.
  CHECK(Scores(PickNearby(Board(20), 50, kLocalXuid).rows) == "100 200 300 400 500 ");
  CHECK(Scores(PickNearby(Board(20), 0, kLocalXuid).rows) == "100 200 300 400 500 ");
  // Small boards give what they have; an empty board or only the player gives nothing.
  CHECK(Scores(PickNearby(Board(2), 150, kLocalXuid).rows) == "200 100 ");
  CHECK(PickNearby(Board(0), 0, kLocalXuid).rows.empty());
  CHECK(PickNearby(Board(0, 500), 500, kLocalXuid).rows.empty());
  for (const auto& row : PickNearby(Board(3, 150), 150, kLocalXuid).rows) CHECK(row.xuid != kLocalXuid);

  // Ranks are counted with the player in: 1100 1200 1300 are 10th, 9th and 8th of 20 plus the player at 1050.
  const auto ranked = PickNearby(Board(20, 900), 1050, kLocalXuid);
  CHECK(ranked.my_rank == 11);
  CHECK(ranked.rows.size() == 5 && ranked.rows[0].rank == 10 && ranked.rows[2].rank == 8 && ranked.rows[3].rank == 12);
  CHECK(PickNearby(Board(20), 5000, kLocalXuid).my_rank == 1);
  CHECK(PickNearby(Board(20), 5000, kLocalXuid).rows[0].rank == 2);
  CHECK(PickNearby(Board(20), 0, kLocalXuid).my_rank == 0);
  CHECK(PickNearby(Board(20), 0, kLocalXuid).rows[0].rank == 20);
}

void TestNearbyFile() {
  gw2::live::StatsView deadline = Board(3, 250);
  deadline.id = 0;
  gw2::live::StatsView king;
  king.id = 5;
  king.rows.push_back({kOnlineXuidTag | 0xABCDEFull, 0, 894030, "Light and Luna"});
  const std::string text = FormatNearby({deadline, king});
  const auto back = ParseNearby(text);
  CHECK(back.size() == 2 && back[0].id == 0 && back[1].id == 5);
  CHECK(back.size() == 2 && back[0].rows.size() == 4 && back[0].total_rows == 4);
  CHECK(back.size() == 2 && back[1].rows.size() == 1 && back[1].rows[0].gamertag == "Light and Luna" &&
        back[1].rows[0].rating == 894030 && back[1].rows[0].xuid == (kOnlineXuidTag | 0xABCDEFull));
  CHECK(back.size() == 2 && back[0].rows[3].xuid == kLocalXuid && back[0].rows[3].rating == 250);
  // What the player sees is what was saved: picking from the file matches picking from the boards.
  CHECK(back.size() == 2 && Scores(PickNearby(back[0], 250, kLocalXuid).rows) == Scores(PickNearby(deadline, 250, kLocalXuid).rows));

  // Damaged or edited lines are skipped, the rest still read; Windows line ends are fine.
  const auto mixed = ParseNearby(
      "# comment\r\n0 0009000000000001 100 Ok\r\n9 0009000000000001 100 BadView\n0 nothex 100 X\n"
      "0 0009000000000002 -5 Negative\n0 0009000000000003 7 Bad!Name\n0 0000000000000000 1 Zero\n"
      "0 0009000000000004 12x Junk\n0 0009000000000005 300\n1 0009000000000006 200 Two Words\n");
  CHECK(mixed.size() == 2 && mixed[0].rows.size() == 1 && mixed[0].rows[0].gamertag == "Ok");
  CHECK(mixed.size() == 2 && mixed[1].id == 1 && mixed[1].rows[0].gamertag == "Two Words");
  CHECK(ParseNearby("").empty());
  CHECK(ParseNearby("not a file at all").empty());
}

}  // namespace

int main() {
  TestNames();
  TestXuids();
  TestBoards();
  TestNearby();
  TestNearbyFile();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
