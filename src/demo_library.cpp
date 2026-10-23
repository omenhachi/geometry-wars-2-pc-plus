#include "demo_library.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

#include "demo_file.h"

namespace gw2::demo_library {
namespace {

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::set<std::string> LoadFavourites(const std::filesystem::path& folder) {
  return ParseFavourites(ReadText(folder / kFavouritesFile));
}

bool SaveFavourites(const std::filesystem::path& folder, const std::set<std::string>& favourites) {
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  const std::filesystem::path path = folder / kFavouritesFile;
  const std::filesystem::path temp = folder / (std::string(kFavouritesFile) + ".tmp");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << FormatFavourites(favourites);
    if (!out) return false;
  }
  std::filesystem::rename(temp, path, error);
  return !error;
}

}  // namespace

std::vector<std::string> ToPrune(const std::vector<Entry>& entries, size_t recent) {
  std::vector<const Entry*> by_time;
  for (const Entry& entry : entries) by_time.push_back(&entry);
  std::stable_sort(by_time.begin(), by_time.end(),
                   [](const Entry* a, const Entry* b) { return a->recorded_at > b->recorded_at; });
  std::set<std::string> keep;
  for (size_t i = 0; i < by_time.size() && i < recent; ++i) keep.insert(by_time[i]->file);
  std::map<std::pair<uint32_t, std::string>, const Entry*> best;  // each mode's, and each custom mode's, best
  for (const Entry* entry : by_time) {
    if (entry->starred) keep.insert(entry->file);
    auto [it, added] = best.emplace(std::make_pair(entry->mode, entry->custom_name), entry);
    if (!added && entry->score > it->second->score) it->second = entry;
  }
  for (const auto& [mode, entry] : best) keep.insert(entry->file);
  std::vector<std::string> prune;
  for (const Entry* entry : by_time) {
    if (!keep.count(entry->file)) prune.push_back(entry->file);
  }
  return prune;
}

std::string_view ModeSlug(uint32_t mode) {
  static constexpr std::string_view kSlugs[] = {"deadline", "evolved", "sequence", "waves", "pacifism", "king"};
  return mode < std::size(kSlugs) ? kSlugs[mode] : "unknown";
}

std::string FileName(uint32_t mode, uint64_t score, int64_t recorded_at) {
  const std::time_t t = std::time_t(recorded_at);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &t);
#else
  gmtime_r(&t, &utc);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
  return std::string(stamp) + "-" + std::string(ModeSlug(mode)) + "-" + std::to_string(score) + kExtension;
}

std::set<std::string> ParseFavourites(std::string_view text) {
  std::set<std::string> favourites;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string line(text.substr(start, end - start));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    // Only plain names of replays: never a path.
    if (line.size() > std::strlen(kExtension) && line.ends_with(kExtension) &&
        line.find_first_of("/\\:") == std::string::npos && line.find("..") == std::string::npos) {
      favourites.insert(line);
    }
    start = end + 1;
  }
  return favourites;
}

std::string FormatFavourites(const std::set<std::string>& favourites) {
  std::string text;
  for (const std::string& name : favourites) text += name + "\n";
  return text;
}

std::vector<Entry> Scan(const std::filesystem::path& folder) {
  std::vector<Entry> entries;
  std::error_code error;
  if (!std::filesystem::is_directory(folder, error)) return entries;
  const std::set<std::string> favourites = LoadFavourites(folder);
  for (const auto& item : std::filesystem::directory_iterator(folder, error)) {
    if (!item.is_regular_file(error) || item.path().extension() != kExtension) continue;
    const uintmax_t size = item.file_size(error);
    if (error || size > demo_file::kMaxFileSize) continue;
    std::ifstream in(item.path(), std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) continue;
    const demo_file::ReadResult read = demo_file::Read(bytes);
    if (!read.demo) continue;
    const std::string name = item.path().filename().string();
    entries.push_back({name, read.demo->mode, read.demo->final_score, read.demo->recorded_at, read.demo->ticks(),
                       favourites.count(name) > 0,
                       (read.demo->flags & demo_file::kFlagCustom) ? read.demo->custom_name : std::string(),
                       read.demo->custom_rules});
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& a, const Entry& b) { return a.recorded_at > b.recorded_at; });
  return entries;
}

size_t Prune(const std::filesystem::path& folder) {
  size_t removed = 0;
  for (const std::string& name : ToPrune(Scan(folder))) {
    std::error_code error;
    if (std::filesystem::remove(folder / name, error)) ++removed;
  }
  return removed;
}

bool SetStarred(const std::filesystem::path& folder, const std::string& file, bool starred) {
  std::set<std::string> favourites = LoadFavourites(folder);
  if (starred) favourites.insert(file);
  else favourites.erase(file);
  return SaveFavourites(folder, favourites);
}

}  // namespace gw2::demo_library
