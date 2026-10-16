#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace gw2 {

inline bool IsSettingLine(std::string_view line, std::string_view key) {
  const std::size_t first = line.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return false;
  }
  std::string_view rest = line.substr(first);
  if (!rest.starts_with(key)) {
    return false;
  }
  rest.remove_prefix(key.size());
  const std::size_t next = rest.find_first_not_of(" \t");
  return next != std::string_view::npos && rest[next] == '=';
}

// Drops the lines that set key. Every other byte is kept, including CR and a missing final newline.
inline std::string RemoveSettingLines(std::string_view text, std::string_view key) {
  std::string kept;
  kept.reserve(text.size());
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::size_t length = end == std::string_view::npos ? text.size() : end + 1;
    const std::string_view line = text.substr(0, length);
    if (!IsSettingLine(line, key)) {
      kept.append(line);
    }
    text.remove_prefix(length);
  }
  return kept;
}

enum class RetiredSettingResult { kAbsent, kRemoved, kFailed };

// Rewrites the file without the retired key, through a temporary file in the same folder; the file is untouched when
// the key is absent or the rewrite fails.
inline RetiredSettingResult RemoveRetiredSetting(const std::filesystem::path& file, std::string_view key) {
  std::string text;
  {
    std::ifstream in(file, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const std::string kept = RemoveSettingLines(text, key);
  if (kept == text) {
    return RetiredSettingResult::kAbsent;
  }
  std::filesystem::path temp = file;
  temp += ".tmp";
  std::error_code ec;
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << kept;
    out.close();
    if (!out) {
      std::filesystem::remove(temp, ec);
      return RetiredSettingResult::kFailed;
    }
  }
  std::filesystem::rename(temp, file, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return RetiredSettingResult::kFailed;
  }
  return RetiredSettingResult::kRemoved;
}

}  // namespace gw2
