#include "live_stats.h"

#include <algorithm>
#include <cstring>

#include "guest_memory.h"

namespace gw2::live {
namespace {

constexpr uint32_t kMaxXuids = 101;
constexpr uint32_t kMaxWrittenViews = 16;
constexpr uint32_t kMaxProperties = 16;
constexpr uint32_t kViewPropertiesSize = 12;
constexpr uint32_t kPropertySize = 24;
constexpr uint8_t kTypeInt64 = 2;
constexpr size_t kGamertagSize = 16;

}  // namespace

void LocalLeaderboard::SetDisplayName(std::string name) {
  std::lock_guard lock(name_mutex_);
  display_name_ = std::move(name);
}

std::string LocalLeaderboard::DisplayName() const {
  std::lock_guard lock(name_mutex_);
  return display_name_.empty() ? user_.name : display_name_;
}

StatsRow LocalLeaderboard::Row(uint32_t view) const {
  const auto score = stats_.Best(user_.xuid, view);
  const bool scored = score && *score > 0;
  return {user_.xuid, scored ? 1u : 0u, scored ? *score : 0, DisplayName()};
}

StatsView LocalLeaderboard::Ranked(uint32_t view, uint32_t first_rank, uint32_t max_rows) const {
  StatsView result;
  result.id = view;
  const auto score = stats_.Best(user_.xuid, view);
  if (!score || *score <= 0) return result;
  result.total_rows = 1;
  if (first_rank <= 1 && max_rows >= 1) result.rows.push_back(Row(view));
  return result;
}

StatsView LocalLeaderboard::Around(uint32_t view, uint64_t xuid, uint32_t max_rows) const {
  if (xuid != user_.xuid) {
    StatsView result;
    result.id = view;
    return result;
  }
  return Ranked(view, 1, max_rows);
}

StatsView LocalLeaderboard::Selected(uint32_t view, const std::vector<uint64_t>& xuids) const {
  StatsView result;
  result.id = view;
  if (std::find(xuids.begin(), xuids.end(), user_.xuid) != xuids.end()) result.rows.push_back(Row(view));
  result.total_rows = static_cast<uint32_t>(result.rows.size());
  return result;
}

uint32_t EnumeratorRegistry::Add(StatsQuery query) {
  std::lock_guard lock(mutex_);
  uint32_t handle;
  do {
    handle = kHandleBase | (next_++ & 0x00FFFFFF);
  } while (queries_.contains(handle));
  queries_.emplace(handle, std::move(query));
  order_.push_back(handle);
  while (queries_.size() > kMaxPending) {
    queries_.erase(order_.front());
    order_.pop_front();
  }
  return handle;
}

std::optional<StatsQuery> EnumeratorRegistry::Take(uint32_t handle) {
  std::lock_guard lock(mutex_);
  const auto it = queries_.find(handle);
  if (it == queries_.end()) return std::nullopt;
  StatsQuery query = std::move(it->second);
  queries_.erase(it);
  order_.erase(std::remove(order_.begin(), order_.end(), handle), order_.end());
  return query;
}

size_t EnumeratorRegistry::pending() const {
  std::lock_guard lock(mutex_);
  return queries_.size();
}

uint32_t StatsResultsSize(const std::vector<StatsView>& views) {
  uint32_t size = kResultsHeaderSize + kViewSize * static_cast<uint32_t>(views.size());
  for (const auto& view : views) size += kRowSize * static_cast<uint32_t>(view.rows.size());
  return size;
}

bool WriteStatsResults(uint8_t* base, uint32_t buffer, uint32_t capacity, const std::vector<StatsView>& views) {
  const uint32_t needed = StatsResultsSize(views);
  if (!buffer || capacity < needed) return false;
  std::memset(base + buffer, 0, needed);

  StoreBE32(base, buffer, static_cast<uint32_t>(views.size()));
  StoreBE32(base, buffer + 4, buffer + kResultsHeaderSize);
  uint32_t view_at = buffer + kResultsHeaderSize;
  uint32_t row_at = view_at + kViewSize * static_cast<uint32_t>(views.size());
  for (const auto& view : views) {
    StoreBE32(base, view_at, view.id);
    StoreBE32(base, view_at + 4, view.total_rows);
    StoreBE32(base, view_at + 8, static_cast<uint32_t>(view.rows.size()));
    StoreBE32(base, view_at + 12, view.rows.empty() ? 0 : row_at);
    for (const auto& row : view.rows) {
      StoreBE64(base, row_at, row.xuid);
      StoreBE32(base, row_at + 8, row.rank);
      StoreBE64(base, row_at + 16, static_cast<uint64_t>(row.rating));
      std::memcpy(base + row_at + 24, row.gamertag.data(), std::min(row.gamertag.size(), kGamertagSize - 1));
      row_at += kRowSize;
    }
    view_at += kViewSize;
  }
  return true;
}

void CompleteOverlapped(uint8_t* base, uint32_t overlapped, uint32_t result, uint32_t items) {
  StoreBE32(base, overlapped + 0x18, result);
  StoreBE32(base, overlapped + 4, items);
  StoreBE32(base, overlapped, result);
}

std::vector<uint32_t> ReadSpecViews(const uint8_t* base, uint32_t specs, uint32_t count) {
  std::vector<uint32_t> views;
  for (uint32_t i = 0; i < count && i < kMaxSpecs; ++i) views.push_back(LoadBE32(base, specs + i * kSpecSize));
  return views;
}

std::vector<ScoreWrite> ParseScoreWrites(const uint8_t* base, uint32_t views, uint32_t count) {
  std::vector<ScoreWrite> writes;
  if (!views || count > kMaxWrittenViews) return writes;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = views + i * kViewPropertiesSize;
    const uint32_t view = LoadBE32(base, entry);
    const uint32_t property_count = LoadBE32(base, entry + 4);
    const uint32_t properties = LoadBE32(base, entry + 8);
    if (!properties || property_count > kMaxProperties) continue;
    for (uint32_t j = 0; j < property_count; ++j) {
      const uint32_t property = properties + j * kPropertySize;
      if (LoadBE32(base, property) != kScorePropertyId || base[property + 8] != kTypeInt64) continue;
      writes.push_back({view, static_cast<int64_t>(LoadBE64(base, property + 16))});
    }
  }
  return writes;
}

uint32_t CreateEnumerator(uint8_t* base, EnumeratorRegistry& registry, StatsQuery::Kind kind, uint64_t pivot,
                          uint32_t max_rows, uint32_t spec_count, uint32_t specs, uint32_t size_ptr,
                          uint32_t handle_ptr, uint32_t* handle_out) {
  if (!spec_count || spec_count > kMaxSpecs || !specs || !size_ptr || !handle_ptr) return kErrorInvalidParameter;

  StatsQuery query;
  query.kind = kind;
  query.pivot = pivot;
  query.max_rows = max_rows;
  query.views = ReadSpecViews(base, specs, spec_count);
  const uint32_t handle = registry.Add(std::move(query));

  StoreBE32(base, size_ptr, kResultsHeaderSize + (kViewSize + kRowSize) * spec_count);
  StoreBE32(base, handle_ptr, handle);
  if (handle_out) *handle_out = handle;
  return kErrorSuccess;
}

uint32_t ServeEnumeration(uint8_t* base, const LocalLeaderboard& board, const StatsQuery& query, uint32_t buffer,
                          uint32_t size, uint32_t items_ptr, uint32_t overlapped) {
  std::vector<StatsView> views;
  for (const uint32_t view : query.views) {
    views.push_back(query.kind == StatsQuery::Kind::kRanked
                        ? board.Ranked(view, static_cast<uint32_t>(query.pivot), query.max_rows)
                        : board.Around(view, query.pivot, query.max_rows));
  }
  const uint32_t result = WriteStatsResults(base, buffer, size, views) ? kErrorSuccess : kErrorInsufficientBuffer;
  const uint32_t items = result == kErrorSuccess ? 1 : 0;
  if (items_ptr) StoreBE32(base, items_ptr, items);
  if (!overlapped) return result;
  CompleteOverlapped(base, overlapped, result, items);
  return kErrorIoPending;
}

bool ServeReadStats(uint8_t* base, const LocalLeaderboard& board, uint32_t xuid_count, uint32_t xuids,
                    uint32_t spec_count, uint32_t specs, uint32_t size, uint32_t results) {
  if (!results || !specs || spec_count > kMaxSpecs) return false;
  std::vector<uint64_t> requested;
  if (xuids) {
    for (uint32_t i = 0; i < xuid_count && i < kMaxXuids; ++i) requested.push_back(LoadBE64(base, xuids + i * 8));
  }
  std::vector<StatsView> views;
  for (const uint32_t view : ReadSpecViews(base, specs, spec_count)) views.push_back(board.Selected(view, requested));
  return WriteStatsResults(base, results, size, views);
}

}  // namespace gw2::live
