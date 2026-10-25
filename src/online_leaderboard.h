#pragma once

// Online leaderboards (Geometry Wars 2 PC Plus): with the Online leaderboards setting on and a player name chosen, a
// finished run's score goes to the PC Plus leaderboard server (server/leaderboard) and the game's Everyone board is
// read from it. Anything that fails - no network, the server gone, a bad answer - falls back to the local board, so
// switching it off or losing the server leaves the game exactly as it was.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "live_stats.h"

namespace gw2::online {

inline constexpr char kServerUrl[] = "https://gw2-pc-plus-leaderboards.gw2pcplus.workers.dev";
inline constexpr size_t kMaxNameLength = 15;
// Rows the port makes room for in one enumeration; the server serves at most 50.
inline constexpr uint32_t kMaxRows = 50;
// Online players' rows carry this tag in their xuid's top byte, so they never equal the local player's.
inline constexpr uint64_t kOnlineXuidTag = 0x00090000'00000000ull;

// The characters the game's menu font can draw and the server accepts: letters, digits, space, _ . -
inline bool NameCharacterAllowed(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
         c == '.' || c == '-';
}

// The name as it would be sent: outer spaces trimmed. Empty when it is empty, too long or has other characters.
inline std::string ValidName(std::string_view name) {
  while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
  while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
  if (name.empty() || name.size() > kMaxNameLength) return {};
  for (const char c : name) {
    if (!NameCharacterAllowed(c)) return {};
  }
  return std::string(name);
}

// An online player's xuid: the 16 hex digit public id, tagged. 0 when the id is malformed.
inline uint64_t XuidForPublicId(std::string_view id) {
  if (id.size() != 16) return 0;
  uint64_t value = 0;
  for (const char c : id) {
    const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0) return 0;
    value = (value << 4) | uint64_t(digit);
  }
  return kOnlineXuidTag | (value & 0x0000FFFF'FFFFFFFFull);
}

// A board from the server's JSON ({total, rows: [{rank, id, name, score}]}). The row whose id is `my_id` takes
// `my_xuid`, so the game highlights it as the player's own. Nullopt when the answer is not a board.
std::optional<live::StatsView> ParseBoard(std::string_view json, uint32_t view, std::string_view my_id,
                                          uint64_t my_xuid);

// Players shown around you: on each mode tile in place of the Xbox Live friends, and as the in-game "score to beat",
// which the game takes as the lowest score above yours. Up to 3 just above `my_score` and up to 2 at or below it,
// more from one side when the other runs short; a view never played (0) gets the board's lowest scores. Rows with
// `my_xuid` are left out: the game's own row for the player is already there. Ranks are counted on the board with
// the player's score in it (the server's may be older), so the Nearby board reads in order; my_rank is 0 for a
// view never played.
inline constexpr size_t kNearbyRows = 5;
live::Nearby PickNearby(const live::StatsView& board, int64_t my_score, uint64_t my_xuid);

// The boards the players around you were last picked from, kept in user data so the tiles are full from the first
// look at the next start: one "view xuid score name" line per row (names never hold a line break). Lines that do
// not read back are skipped.
std::string FormatNearby(const std::vector<live::StatsView>& boards);
std::vector<live::StatsView> ParseNearby(std::string_view text);

// Called once at start with the file that keeps this PC's secret token and the local player's xuid.
void Configure(std::filesystem::path token_file, uint64_t local_xuid);

// The setting is on, a valid name is set and this platform has the client.
bool Enabled();

// One line for the log at start: on or off, and why.
std::string Describe();

// Sends a finished run's score in the background. Runs with a cheat on are never sent.
void SubmitScore(uint32_t view, int64_t score);

// Reads ranks first_rank .. first_rank + count - 1 of a board. Blocks for up to a few seconds: call it off the game
// thread.
std::optional<live::StatsView> FetchBoard(uint32_t view, uint32_t first_rank, uint32_t count);

// The cached board a view's nearby players are picked from (from the last start until fresh ones arrive); nullopt
// when there is none. The first call each start fetches every board in the background, as does each accepted score.
std::optional<live::StatsView> NearbyBoard(uint32_t view);
void RefreshNearby();

// Online replays. While the player shares them (the launcher's "Share my top runs as replays"), a best that makes a
// board's top 50 sends its replay too: the very run that was scored, found among the saved replays by its mode and
// score. The server keeps it while that best is in the top 50, and anyone can watch it from Watch replays.
inline constexpr size_t kMaxReplayBytes = 24u << 20;  // the server's limit

// A row of a board's top runs: `game` is the game package its replay was recorded on, empty without a replay.
struct TopRun {
  uint32_t rank = 0;
  std::string id;
  std::string name;
  int64_t score = 0;
  std::string game;
  bool mine = false;
};
// The server's board with each row's replay ({total, rows: [{rank, id, name, score, replay?}]}); nullopt when the
// answer is not one.
std::optional<std::vector<TopRun>> ParseTopRuns(std::string_view json, std::string_view my_id);

bool SharesReplays();
// A board's top 50 with their replays. Blocks: call it off the game thread.
std::optional<std::vector<TopRun>> FetchTopRuns(uint32_t board);
// A player's replay on a board, as uploaded. Blocks: call it off the game thread.
std::optional<std::vector<uint8_t>> DownloadReplay(uint32_t board, const std::string& id);
// The public id of the player shown with `xuid` on `board` (as the boards were last fetched), when their best there
// has a replay to watch.
std::optional<std::string> ReplayIdFor(uint32_t board, uint64_t xuid);

// True once, the first time ever a replay of the player's was shared: the game says so then, and never again.
bool TakeSharedNotice();

// Deletes this PC's player and every score from the server, then calls done(success, message) on another thread.
void DeleteMyScores(std::function<void(bool, std::string)> done);

}  // namespace gw2::online
