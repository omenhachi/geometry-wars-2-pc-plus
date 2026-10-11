#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>

namespace gw2 {

// <user_data_root>/<title id>/live/stats.toml
std::filesystem::path StatsFilePath(const std::filesystem::path& user_data_root, uint32_t title_id);

enum class LoadResult { kMissing, kLoaded, kCorrupt };
enum class RecordResult { kUnchanged, kSaved, kNotSaved };

// Best score per user and leaderboard view. Changes reach the disk before Record returns.
class LocalStats {
 public:
  static constexpr uint32_t kMaxViews = 16;

  explicit LocalStats(std::filesystem::path file) : file_(std::move(file)) {}

  // A file that cannot be parsed is moved aside to <file>.corrupt and the store starts empty.
  LoadResult Load();

  std::optional<int64_t> Best(uint64_t xuid, uint32_t view) const;

  // Keeps the higher score; scores of zero or less and unknown views are ignored.
  RecordResult Record(uint64_t xuid, uint32_t view, int64_t score);

 private:
  using Views = std::map<uint32_t, int64_t>;

  bool Parse(std::string_view text, std::map<uint64_t, Views>& out) const;
  bool Save() const;

  std::filesystem::path file_;
  mutable std::mutex mutex_;
  std::map<uint64_t, Views> best_;
};

}  // namespace gw2
