#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace gw2 {

inline constexpr uint32_t kTitleId = 0x584108FF;
inline constexpr uint32_t kGameContentType = 0x000D0000;

// The default.xex the game was recompiled from. The SHA-256 is the authoritative fingerprint; the size only
// rejects files that cannot match before they are read.
struct GameFingerprint {
  uint64_t xex_size = 0;
  std::string xex_sha256;  // upper-case hex
};
inline const GameFingerprint kSupportedGame{
    704512, "6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88"};

enum class GameFileState { kMissing, kValid, kDifferentVersion, kReadError };

struct GameCheck {
  GameFileState state = GameFileState::kMissing;
  uint64_t xex_size = 0;   // set when default.xex was measured
  std::string xex_sha256;  // set when default.xex was read
};

// The one readiness check: the launcher, Play, install verification and the startup decision all use it.
GameCheck CheckGameFiles(const std::filesystem::path& game_dir, const GameFingerprint& expected = kSupportedGame);

// Upper-case hex SHA-256 of a file, empty when it cannot be read.
std::string Sha256Hex(const std::filesystem::path& file);

enum class InstallStatus {
  kInstalled,
  kCancelled,
  kNoGameFolder,
  kForeignFolder,
  kOpenFailed,
  kNotPackage,
  kWrongTitle,
  kNotGamePackage,
  kUnsupportedPackage,
  kDamagedPackage,
  kNoSpace,
  kWriteFailed,
  kWrongVersion,
  kIncomplete,
};

struct InstallResult {
  InstallStatus status = InstallStatus::kInstalled;
  std::string detail;  // for the log, not the UI
  uint64_t bytes = 0;
  size_t files = 0;
  bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
  std::function<void(const char* phase)> phase;
  // Returns false to cancel. Cancelling is possible until the new files are activated.
  std::function<bool(uint64_t done, uint64_t total)> progress;
};

// Extracts the package into a staging folder next to `game_dir`, checks it with CheckGameFiles and only then
// swaps it in. On any failure or cancellation `game_dir` is left as it was.
InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks = {}, const GameFingerprint& expected = kSupportedGame);

struct InstallMessage {
  const char* title;
  const char* text;
};
InstallMessage DescribeInstall(InstallStatus status);

}  // namespace gw2
