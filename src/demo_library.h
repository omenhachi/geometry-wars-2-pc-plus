#pragma once

// The saved replays (<user_data>/demos): what is kept, what is starred, and the files' names.
//
// Kept: the newest kRecent, each mode's best, and every starred one. Older ones go quietly when a new run is saved;
// there is nothing to tidy and no count to watch. Stars are kept in favourites.txt beside the replays, one file name
// per line.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "demo_file.h"

namespace gw2::demo_library {

inline constexpr size_t kRecent = 10;
inline constexpr char kExtension[] = ".gw2demo";
inline constexpr char kFavouritesFile[] = "favourites.txt";

struct Entry {
  std::string file;  // the name inside the folder
  uint32_t mode = 0;
  uint64_t score = 0;
  int64_t recorded_at = 0;  // Unix seconds
  uint32_t ticks = 0;
  bool starred = false;
  std::string custom_name;  // a custom mode's run (custom_modes.h); empty for the game's own modes
  std::vector<uint8_t> custom_rules;
};

// The files to delete so only the newest `recent`, each mode's best and the starred remain.
std::vector<std::string> ToPrune(const std::vector<Entry>& entries, size_t recent = kRecent);

// The saved replay of a run in `mode` (and custom mode `custom_name`, empty for the game's own) that scored exactly
// `score`: the newest such file, or none.
std::optional<std::string> FindRun(const std::vector<Entry>& entries, uint32_t mode, std::string_view custom_name,
                                   uint64_t score);

// "20261023-053012-pacifism-72826275.gw2demo" (UTC), so names sort by time and say what they hold.
std::string FileName(uint32_t mode, uint64_t score, int64_t recorded_at);
std::string_view ModeSlug(uint32_t mode);

std::set<std::string> ParseFavourites(std::string_view text);
std::string FormatFavourites(const std::set<std::string>& favourites);

// The replays in `folder`, newest first. Unreadable files are left out (and left alone).
std::vector<Entry> Scan(const std::filesystem::path& folder);
// Deletes what ToPrune names. Returns how many went.
size_t Prune(const std::filesystem::path& folder);
bool SetStarred(const std::filesystem::path& folder, const std::string& file, bool starred);

}  // namespace gw2::demo_library
