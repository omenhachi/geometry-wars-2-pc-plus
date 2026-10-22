#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "local_stats.h"

namespace gw2::live {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInvalidParameter = 0x57;
constexpr uint32_t kErrorInsufficientBuffer = 0x7A;
constexpr uint32_t kErrorIoPending = 0x3E5;

// Guest layouts: XUSER_STATS_READ_RESULTS, XUSER_STATS_VIEW, XUSER_STATS_ROW, XUSER_STATS_SPEC.
constexpr uint32_t kResultsHeaderSize = 8;
constexpr uint32_t kViewSize = 16;
constexpr uint32_t kRowSize = 48;
constexpr uint32_t kSpecSize = 136;
constexpr uint32_t kMaxSpecs = 64;

// The only property the game writes: X_PROPERTY_SCORE, a 64-bit integer.
constexpr uint32_t kScorePropertyId = 0x20000001;

struct LocalUser {
  uint64_t xuid = 0;
  std::string name;
};

struct StatsRow {
  uint64_t xuid = 0;
  uint32_t rank = 0;
  int64_t rating = 0;
  std::string gamertag;
};

struct StatsView {
  uint32_t id = 0;
  uint32_t total_rows = 0;
  std::vector<StatsRow> rows;
};

// What an offline console knows: the local player's best score in each view and nobody else.
class LocalLeaderboard {
 public:
  LocalLeaderboard(const LocalStats& stats, LocalUser user) : stats_(stats), user_(std::move(user)) {}

  const LocalUser& user() const { return user_; }

  // The name the player's rows show instead of the profile's; empty goes back to the profile's. Any thread.
  void SetDisplayName(std::string name);

  StatsView Ranked(uint32_t view, uint32_t first_rank, uint32_t max_rows) const;
  StatsView Around(uint32_t view, uint64_t xuid, uint32_t max_rows) const;
  // A row for the local player when it is asked for, scored or not.
  StatsView Selected(uint32_t view, const std::vector<uint64_t>& xuids) const;

 private:
  StatsRow Row(uint32_t view) const;
  std::string DisplayName() const;

  const LocalStats& stats_;
  LocalUser user_;
  mutable std::mutex name_mutex_;
  std::string display_name_;  // guarded by name_mutex_
};

struct StatsQuery {
  enum class Kind { kRanked, kAround };
  Kind kind = Kind::kRanked;
  uint64_t pivot = 0;
  uint32_t max_rows = 0;
  std::vector<uint32_t> views;
};

// Handles for enumerators the port answers itself. The range never overlaps the runtime's object
// handles (0xF8xxxxxx), and a handle is retired once its one enumeration has been served.
class EnumeratorRegistry {
 public:
  static constexpr uint32_t kHandleBase = 0xEC000000;
  static constexpr size_t kMaxPending = 64;

  uint32_t Add(StatsQuery query);
  std::optional<StatsQuery> Take(uint32_t handle);
  size_t pending() const;

 private:
  mutable std::mutex mutex_;
  std::map<uint32_t, StatsQuery> queries_;
  std::deque<uint32_t> order_;
  uint32_t next_ = 1;
};

uint32_t StatsResultsSize(const std::vector<StatsView>& views);
bool WriteStatsResults(uint8_t* base, uint32_t buffer, uint32_t capacity, const std::vector<StatsView>& views);
void CompleteOverlapped(uint8_t* base, uint32_t overlapped, uint32_t result, uint32_t items);

std::vector<uint32_t> ReadSpecViews(const uint8_t* base, uint32_t specs, uint32_t count);

struct ScoreWrite {
  uint32_t view = 0;
  int64_t score = 0;
};
// XSESSION_VIEW_PROPERTIES[count]: the score written to each view.
std::vector<ScoreWrite> ParseScoreWrites(const uint8_t* base, uint32_t views, uint32_t count);

// XamUserCreateStatsEnumerator: validates, registers the query and writes the result size and the
// handle. Returns the status for r3.
uint32_t CreateEnumerator(uint8_t* base, EnumeratorRegistry& registry, StatsQuery::Kind kind, uint64_t pivot,
                          uint32_t max_rows, uint32_t spec_count, uint32_t specs, uint32_t size_ptr,
                          uint32_t handle_ptr, uint32_t* handle_out = nullptr);

// XamEnumerate on a registered handle: fills the results and completes the overlapped like the
// console does (the call returns IO_PENDING once the overlapped carries the result).
uint32_t ServeEnumeration(uint8_t* base, const LocalLeaderboard& board, const StatsQuery& query, uint32_t buffer,
                          uint32_t size, uint32_t items_ptr, uint32_t overlapped);

// Other players to show beside the requested ones in a view, given the local player's score there (0 when none).
using NearbyRows = std::function<std::vector<StatsRow>(uint32_t view, int64_t my_score)>;

// XUserReadStats: fills the results buffer with the requested players' rows and then any nearby rows that fit,
// dropping nearby rows from the last view back. False when the buffer is missing or too small for the requested.
bool ServeReadStats(uint8_t* base, const LocalLeaderboard& board, uint32_t xuid_count, uint32_t xuids,
                    uint32_t spec_count, uint32_t specs, uint32_t size, uint32_t results,
                    const NearbyRows& nearby = {});

}  // namespace gw2::live
