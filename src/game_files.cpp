#include "game_files.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include "stfs.h"

namespace gw2 {
namespace {

constexpr const char* kStagingFolder = "game-install-temp";
constexpr const char* kBackupFolder = "game-install-backup";
constexpr const char* kExecutableName = "default.xex";
constexpr const char* kDataName = "Data.bin";
constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr int kRenameAttempts = 10;
constexpr size_t kHashChunkBytes = 1u << 20;

// The antivirus scanner can hold a freshly written folder for a moment.
bool RenameWithRetry(const std::filesystem::path& from, const std::filesystem::path& to) {
  for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  return false;
}

// An existing folder is only replaced when it is empty or already holds game files.
bool IsReplaceable(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }
  return std::filesystem::is_empty(dir, ec) || std::filesystem::exists(dir / kExecutableName, ec) ||
         std::filesystem::exists(dir / kDataName, ec);
}

class StagingGuard {
 public:
  explicit StagingGuard(std::filesystem::path dir) : dir_(std::move(dir)) {}
  ~StagingGuard() {
    if (armed_) {
      std::error_code ec;
      std::filesystem::remove_all(dir_, ec);
    }
  }
  void Release() { armed_ = false; }

 private:
  std::filesystem::path dir_;
  bool armed_ = true;
};

InstallResult Failure(InstallStatus status, std::string detail = {}) {
  InstallResult result;
  result.status = status;
  result.detail = std::move(detail);
  return result;
}

InstallStatus FromStfs(stfs::Status status) {
  switch (status) {
    case stfs::Status::kOpenFailed:
      return InstallStatus::kOpenFailed;
    case stfs::Status::kNotPackage:
      return InstallStatus::kNotPackage;
    case stfs::Status::kUnsupported:
      return InstallStatus::kUnsupportedPackage;
    case stfs::Status::kCorrupt:
    case stfs::Status::kUnsafePath:
      return InstallStatus::kDamagedPackage;
    case stfs::Status::kCancelled:
      return InstallStatus::kCancelled;
    case stfs::Status::kWriteFailed:
    case stfs::Status::kOk:
      break;
  }
  return InstallStatus::kWriteFailed;
}

}  // namespace

std::string Sha256Hex(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return {};
  }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  ULONG object_size = 0;
  ULONG digest_size = 0;
  ULONG property_size = 0;
  bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
            BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                                             sizeof(object_size), &property_size, 0)) &&
            BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size),
                                             sizeof(digest_size), &property_size, 0));
  std::vector<UCHAR> object(object_size);
  std::vector<UCHAR> digest(digest_size);
  if (ok) {
    ok = BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0));
  }
  std::vector<char> chunk(kHashChunkBytes);
  while (ok && in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    if (const std::streamsize read = in.gcount(); read > 0) {
      ok = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()), ULONG(read), 0));
    }
  }
  ok = ok && in.eof() && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), digest_size, 0));
  if (hash) {
    BCryptDestroyHash(hash);
  }
  if (algorithm) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
  }
  if (!ok) {
    return {};
  }
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string hex;
  for (const UCHAR byte : digest) {
    hex.push_back(kDigits[byte >> 4]);
    hex.push_back(kDigits[byte & 0xF]);
  }
  return hex;
}

GameCheck CheckGameFiles(const std::filesystem::path& game_dir, const GameFingerprint& expected) {
  GameCheck check;
  if (game_dir.empty()) {
    return check;
  }
  std::error_code ec;
  const std::filesystem::path xex = game_dir / kExecutableName;
  if (!std::filesystem::is_regular_file(xex, ec) || !std::filesystem::is_regular_file(game_dir / kDataName, ec)) {
    return check;
  }
  const uintmax_t size = std::filesystem::file_size(xex, ec);
  if (ec) {
    check.state = GameFileState::kReadError;
    return check;
  }
  check.xex_size = size;
  if (size != expected.xex_size) {
    check.state = GameFileState::kDifferentVersion;
    return check;
  }
  check.xex_sha256 = Sha256Hex(xex);
  if (check.xex_sha256.empty()) {
    check.state = GameFileState::kReadError;
    return check;
  }
  check.state = check.xex_sha256 == expected.xex_sha256 ? GameFileState::kValid : GameFileState::kDifferentVersion;
  return check;
}

InstallResult InstallFromPackage(const std::filesystem::path& package, const std::filesystem::path& game_dir,
                                 const InstallHooks& hooks, const GameFingerprint& expected) {
  const auto phase = [&](const char* text) {
    if (hooks.phase) {
      hooks.phase(text);
    }
  };

  std::error_code ec;
  std::filesystem::path target = std::filesystem::absolute(game_dir, ec).lexically_normal();
  if (!target.has_filename()) {
    target = target.parent_path();
  }
  const std::filesystem::path parent = target.parent_path();
  if (game_dir.empty() || ec || parent.empty() || parent == target) {
    return Failure(InstallStatus::kNoGameFolder);
  }

  phase("Reading package...");
  stfs::Package pkg;
  stfs::Result read = pkg.Open(package);
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }
  if (pkg.title_id() != kTitleId) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "title ID %08X", pkg.title_id());
    return Failure(InstallStatus::kWrongTitle, detail);
  }
  if (pkg.content_type() != kGameContentType) {
    char detail[48];
    std::snprintf(detail, sizeof(detail), "content type %08X", pkg.content_type());
    return Failure(InstallStatus::kNotGamePackage, detail);
  }
  read = pkg.LoadFiles();
  if (!read.ok()) {
    return Failure(FromStfs(read.status), read.detail);
  }

  std::filesystem::create_directories(parent, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + parent.string() + ": " + ec.message());
  }
  if (std::filesystem::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, target.string());
  }

  const std::filesystem::path staging = parent / kStagingFolder;
  const std::filesystem::path backup = parent / kBackupFolder;
  std::filesystem::remove_all(staging, ec);
  if (ec || std::filesystem::exists(staging, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + staging.string());
  }
  const auto space = std::filesystem::space(parent, ec);
  if (!ec && space.available < pkg.total_bytes() + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace, "needs " + std::to_string(pkg.total_bytes()) + " bytes, " +
                                                std::to_string(space.available) + " available");
  }
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + staging.string() + ": " + ec.message());
  }
  StagingGuard guard(staging);

  phase("Extracting game files...");
  const stfs::Result extracted = pkg.ExtractTo(staging, hooks.progress);
  if (!extracted.ok()) {
    return Failure(FromStfs(extracted.status), extracted.detail);
  }

  phase("Verifying installation...");
  switch (CheckGameFiles(staging, expected).state) {
    case GameFileState::kValid:
      break;
    case GameFileState::kDifferentVersion:
      return Failure(InstallStatus::kWrongVersion);
    case GameFileState::kMissing:
      return Failure(InstallStatus::kIncomplete);
    case GameFileState::kReadError:
      return Failure(InstallStatus::kWriteFailed, "cannot read the extracted default.xex");
  }
  if (hooks.progress && !hooks.progress(pkg.total_bytes(), pkg.total_bytes())) {
    return Failure(InstallStatus::kCancelled);
  }

  phase("Finishing...");
  const bool replacing = std::filesystem::exists(target, ec);
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
    if (!RenameWithRetry(target, backup)) {
      return Failure(InstallStatus::kWriteFailed, "cannot move aside " + target.string());
    }
  }
  if (!RenameWithRetry(staging, target)) {
    if (replacing) {
      RenameWithRetry(backup, target);
    }
    return Failure(InstallStatus::kWriteFailed, "cannot activate " + target.string());
  }
  guard.Release();
  if (replacing) {
    std::filesystem::remove_all(backup, ec);
  }

  InstallResult result;
  result.bytes = pkg.total_bytes();
  result.files = pkg.file_count();
  return result;
}

InstallMessage DescribeInstall(InstallStatus status) {
  switch (status) {
    case InstallStatus::kInstalled:
      return {"Game installed", "Geometry Wars is ready to play."};
    case InstallStatus::kCancelled:
      return {"Installation cancelled", "The game files were not changed."};
    case InstallStatus::kNoGameFolder:
      return {"Could not install the game", "The game folder is not set."};
    case InstallStatus::kForeignFolder:
      return {"Could not install the game",
              "The game folder holds files that are not Geometry Wars. Move them out and try again."};
    case InstallStatus::kOpenFailed:
    case InstallStatus::kDamagedPackage:
      return {"Could not read the package", "Check that the file is readable and not damaged, then try again."};
    case InstallStatus::kNotPackage:
      return {"Not an Xbox 360 package", "Select the package file inside the 584108FF\\000D0000 folder."};
    case InstallStatus::kWrongTitle:
      return {"Wrong game package", "This package is not Geometry Wars: Retro Evolved 2."};
    case InstallStatus::kNotGamePackage:
      return {"Wrong game package", "Select the game package, not other content."};
    case InstallStatus::kUnsupportedPackage:
      return {"Unsupported package", "This package layout is not supported."};
    case InstallStatus::kNoSpace:
      return {"Not enough free space", "Free up disk space and try again."};
    case InstallStatus::kWriteFailed:
      return {"Could not install the game", "The game files could not be written."};
    case InstallStatus::kWrongVersion:
      return {"Different version of Geometry Wars: Retro Evolved 2",
              "The package does not contain the supported version of the game."};
    case InstallStatus::kIncomplete:
      return {"Could not install the game", "The package is missing required game files."};
  }
  return {"Could not install the game", "An unexpected error occurred."};
}

}  // namespace gw2
