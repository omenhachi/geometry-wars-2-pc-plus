#include <algorithm>

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

}  // namespace gw2::online
