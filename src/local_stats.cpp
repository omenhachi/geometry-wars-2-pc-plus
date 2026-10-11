#include "local_stats.h"

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gw2 {
namespace {

constexpr uintmax_t kMaxFileSize = 1u << 20;

std::string_view Trim(std::string_view s) {
  constexpr std::string_view kSpace = " \t\r\n";
  const size_t first = s.find_first_not_of(kSpace);
  if (first == std::string_view::npos) return {};
  return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
}

template <typename T>
bool ParseNumber(std::string_view s, T& out, int base = 10) {
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
  return ec == std::errc() && end == s.data() + s.size();
}

bool WriteDurably(const std::filesystem::path& path, const std::string& text) {
#if defined(_WIN32)
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"wb") != 0) return false;
#else
  FILE* f = std::fopen(path.c_str(), "wb");
#endif
  if (!f) return false;
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size() && std::fflush(f) == 0;
#if defined(_WIN32)
  ok = ok && _commit(_fileno(f)) == 0;
#else
  ok = ok && fsync(fileno(f)) == 0;
#endif
  return std::fclose(f) == 0 && ok;
}

}  // namespace

std::filesystem::path StatsFilePath(const std::filesystem::path& user_data_root, uint32_t title_id) {
  char title[9];
  std::snprintf(title, sizeof(title), "%08" PRIX32, title_id);
  return user_data_root / title / "live" / "stats.toml";
}

LoadResult LocalStats::Load() {
  std::lock_guard lock(mutex_);
  best_.clear();

  std::error_code ec;
  if (!std::filesystem::is_regular_file(file_, ec)) return LoadResult::kMissing;

  std::string text;
  const uintmax_t size = std::filesystem::file_size(file_, ec);
  bool parsed = !ec && size <= kMaxFileSize;
  if (parsed) {
    std::ifstream in(file_, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    parsed = !in.bad();
  }
  std::map<uint64_t, Views> loaded;
  if (parsed && Parse(text, loaded)) {
    best_ = std::move(loaded);
    return LoadResult::kLoaded;
  }

  auto aside = file_;
  aside += ".corrupt";
  std::filesystem::rename(file_, aside, ec);
  return LoadResult::kCorrupt;
}

bool LocalStats::Parse(std::string_view text, std::map<uint64_t, Views>& out) const {
  constexpr std::string_view kSectionPrefix = "[users.";
  constexpr std::string_view kViewPrefix = "view_";
  Views* current = nullptr;

  for (size_t pos = 0; pos < text.size();) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view line = Trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line.front() == '#') continue;

    if (line.front() == '[') {
      if (!line.starts_with(kSectionPrefix) || line.back() != ']') return false;
      const std::string_view hex = line.substr(kSectionPrefix.size(), line.size() - kSectionPrefix.size() - 1);
      uint64_t xuid;
      if (hex.size() != 16 || !ParseNumber(hex, xuid, 16)) return false;
      current = &out[xuid];
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return false;
    const std::string_view key = Trim(line.substr(0, eq));
    const std::string_view value = Trim(line.substr(eq + 1));
    if (!current) {
      if (key != "version" || value != "1") return false;
      continue;
    }

    uint32_t view;
    int64_t score;
    if (!key.starts_with(kViewPrefix) || !ParseNumber(key.substr(kViewPrefix.size()), view) || view >= kMaxViews ||
        !ParseNumber(value, score)) {
      return false;
    }
    auto [it, inserted] = current->try_emplace(view, score);
    if (!inserted && score > it->second) it->second = score;
  }
  return true;
}

std::optional<int64_t> LocalStats::Best(uint64_t xuid, uint32_t view) const {
  std::lock_guard lock(mutex_);
  const auto user = best_.find(xuid);
  if (user == best_.end()) return std::nullopt;
  const auto score = user->second.find(view);
  if (score == user->second.end()) return std::nullopt;
  return score->second;
}

RecordResult LocalStats::Record(uint64_t xuid, uint32_t view, int64_t score) {
  if (score <= 0 || view >= kMaxViews) return RecordResult::kUnchanged;
  std::lock_guard lock(mutex_);
  auto& best = best_[xuid];
  const auto it = best.find(view);
  if (it != best.end() && it->second >= score) return RecordResult::kUnchanged;
  best[view] = score;
  return Save() ? RecordResult::kSaved : RecordResult::kNotSaved;
}

bool LocalStats::Save() const {
  std::string text =
      "# Offline scores for the local player: one best score per user and view. No Xbox Live.\n"
      "version = 1\n";
  char section[48];
  for (const auto& [xuid, views] : best_) {
    std::snprintf(section, sizeof(section), "\n[users.%016" PRIX64 "]\n", xuid);
    text += section;
    for (const auto& [view, score] : views) {
      text += "view_" + std::to_string(view) + " = " + std::to_string(score) + "\n";
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  if (ec) return false;
  auto temporary = file_;
  temporary += ".tmp";
  if (!WriteDurably(temporary, text)) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  std::filesystem::rename(temporary, file_, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  return true;
}

}  // namespace gw2
