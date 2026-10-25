#include <algorithm>
#include <charconv>
#include <cstdio>
#include <vector>

#include "boards.h"
#include "online_leaderboard.h"
#include "updater/json.h"

// The server's board, kept apart from the client so the tests can build it without the runtime.
namespace gw2::online {

std::optional<live::StatsView> ParseBoard(std::string_view json, uint32_t view, std::string_view my_id,
                                          uint64_t my_xuid) {
  const auto root = trg::update::json::Parse(json);
  if (!root || !root->IsObject()) return std::nullopt;
  const auto* total = root->Find("total");
  const auto* rows = root->Find("rows");
  if (!total || !total->AsInt64() || !rows || !rows->IsArray()) return std::nullopt;
  live::StatsView board;
  board.id = view;
  board.total_rows = uint32_t(std::max<int64_t>(0, *total->AsInt64()));
  for (const auto& row : rows->AsArray()) {
    const auto* rank = row.Find("rank");
    const auto* id = row.Find("id");
    const auto* name = row.Find("name");
    const auto* score = row.Find("score");
    if (!rank || !rank->AsInt64() || !id || !id->IsString() || !name || !name->IsString() || !score ||
        !score->AsInt64()) {
      return std::nullopt;
    }
    const uint64_t xuid = !my_id.empty() && id->AsString() == my_id ? my_xuid : XuidForPublicId(id->AsString());
    const std::string shown = ValidName(name->AsString());
    if (xuid == 0 || shown.empty() || board.rows.size() >= kMaxRows) return std::nullopt;
    board.rows.push_back({xuid, uint32_t(*rank->AsInt64()), *score->AsInt64(), shown});
  }
  return board;
}

std::string FormatNearby(const std::vector<live::StatsView>& boards) {
  std::string out = "# Players around you on the PC Plus leaderboards, as last fetched. Safe to delete.\n";
  char xuid[17];
  for (const live::StatsView& board : boards) {
    for (const live::StatsRow& row : board.rows) {
      std::snprintf(xuid, sizeof xuid, "%016llx", static_cast<unsigned long long>(row.xuid));
      out += std::to_string(board.id) + " " + xuid + " " + std::to_string(row.rating) + " " + row.gamertag + "\n";
    }
  }
  return out;
}

std::vector<live::StatsView> ParseNearby(std::string_view text) {
  std::vector<live::StatsView> boards;
  const auto field = [](std::string_view& line) {
    const size_t space = line.find(' ');
    const std::string_view value = line.substr(0, space);
    line.remove_prefix(space == std::string_view::npos ? line.size() : space + 1);
    return value;
  };
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;
    const std::string_view view_text = field(line), xuid_text = field(line), score_text = field(line);
    uint32_t view = 0;
    uint64_t xuid = 0;
    int64_t score = 0;
    const auto parsed = [](std::string_view s, auto& value, int base = 10) {
      const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, base);
      return ec == std::errc() && ptr == s.data() + s.size() && !s.empty();
    };
    const std::string name = ValidName(line);
    if (!parsed(view_text, view) || view >= boards::kBoards || !parsed(xuid_text, xuid, 16) || xuid == 0 ||
        !parsed(score_text, score) || score < 0 || name.empty() || name != line) {
      continue;
    }
    auto board = std::find_if(boards.begin(), boards.end(), [view](const auto& b) { return b.id == view; });
    if (board == boards.end()) {
      boards.push_back({});
      board = boards.end() - 1;
      board->id = view;
    }
    if (board->rows.size() >= kMaxRows) continue;
    board->rows.push_back({xuid, 0, score, name});
    board->total_rows = uint32_t(board->rows.size());
  }
  return boards;
}

live::Nearby PickNearby(const live::StatsView& board, int64_t my_score, uint64_t my_xuid) {
  // 1 + how many score more; the player counts when they have a score.
  const auto rank_of = [&](int64_t score) {
    uint32_t higher = my_score > score ? 1 : 0;
    for (const live::StatsRow& row : board.rows) {
      if (row.xuid != my_xuid && row.rating > score) ++higher;
    }
    return higher + 1;
  };
  std::vector<live::StatsRow> above, below;
  for (const live::StatsRow& row : board.rows) {
    if (row.xuid == my_xuid) continue;
    // Equal scores go below: the game's score to beat is the lowest one strictly above.
    (row.rating > my_score ? above : below).push_back(row);
  }
  std::sort(above.begin(), above.end(), [](const auto& a, const auto& b) { return a.rating < b.rating; });
  std::sort(below.begin(), below.end(), [](const auto& a, const auto& b) { return a.rating > b.rating; });
  constexpr size_t kAbove = 3;
  const size_t take_below = std::min(below.size(), kNearbyRows - std::min(above.size(), kAbove));
  const size_t take_above = std::min(above.size(), kNearbyRows - take_below);
  live::Nearby picked;
  picked.rows.assign(above.begin(), above.begin() + take_above);
  picked.rows.insert(picked.rows.end(), below.begin(), below.begin() + take_below);
  for (live::StatsRow& row : picked.rows) row.rank = rank_of(row.rating);
  if (my_score > 0) picked.my_rank = rank_of(my_score);
  return picked;
}

}  // namespace gw2::online
