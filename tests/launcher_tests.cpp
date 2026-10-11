// Launcher tests: game-file validation, package reading and safe installation, launcher settings, the window fit
// and the achievement summary. They use synthetic packages and temporary folders only; no game files or user data.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "game_files.h"
#include "launcher_achievements.h"
#include "launcher_platform.h"
#include "stfs.h"

namespace fs = std::filesystem;
using namespace gw2;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

void Section(const char* name) {
  std::printf("%s\n", name);
}

using Bytes = std::vector<uint8_t>;

constexpr uint64_t kBase = 0xB000;
constexpr uint64_t kBlock = 0x1000;
constexpr char kAbcSha256[] = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
const fs::path kRoot = fs::temp_directory_path() / "gw2_launcher_tests";

// Expected fingerprint for a synthetic default.xex whose bytes are "abc".
GameFingerprint AbcFingerprint() {
  return {3, kAbcSha256};
}

Bytes Text(const std::string& text) {
  return Bytes(text.begin(), text.end());
}

struct Item {
  std::string name;
  int parent = -1;
  bool dir = false;
  Bytes data;
};

Item File(std::string name, Bytes data) {
  return {std::move(name), -1, false, std::move(data)};
}

struct BuildOptions {
  const char* magic = "LIVE";
  uint32_t title = kTitleId;
  uint32_t content_type = kGameContentType;
  bool read_only = true;
  size_t truncate = 0;
  bool consecutive = true;
};

void PutBe32(Bytes& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v >> 24);
  b[at + 1] = uint8_t(v >> 16);
  b[at + 2] = uint8_t(v >> 8);
  b[at + 3] = uint8_t(v);
}

void PutLe24(Bytes& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v);
  b[at + 1] = uint8_t(v >> 8);
  b[at + 2] = uint8_t(v >> 16);
}

uint64_t Physical(uint64_t block) {
  uint64_t index = block + block / 0xAA + 1;
  if (block >= 0xAA) {
    index += 1;
  }
  return kBase + index * kBlock;
}

// A read-only STFS package with the file table in block 0 and consecutive files after it.
Bytes BuildPackage(const std::vector<Item>& items, const BuildOptions& options = {}) {
  uint64_t next_block = 1;
  std::vector<uint64_t> start(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    start[i] = next_block;
    next_block += (items[i].data.size() + kBlock - 1) / kBlock;
  }
  Bytes image(Physical(next_block - 1) + kBlock);
  std::memcpy(image.data(), options.magic, 4);
  PutBe32(image, 0x340, 0xAD0E);
  PutBe32(image, 0x344, options.content_type);
  PutBe32(image, 0x360, options.title);
  image[0x379] = 0x24;
  image[0x37B] = options.read_only ? 1 : 0;
  image[0x37C] = 1;
  PutLe24(image, 0x37E, 0);
  for (size_t i = 0; i < items.size(); ++i) {
    uint8_t* e = &image[Physical(0) + i * 0x40];
    std::memcpy(e, items[i].name.data(), items[i].name.size());
    const uint32_t blocks = uint32_t((items[i].data.size() + kBlock - 1) / kBlock);
    e[0x28] = uint8_t(items[i].name.size() | (options.consecutive ? 0x40 : 0) | (items[i].dir ? 0x80 : 0));
    PutLe24(image, i * 0x40 + Physical(0) + 0x29, blocks);
    PutLe24(image, i * 0x40 + Physical(0) + 0x2C, blocks);
    PutLe24(image, i * 0x40 + Physical(0) + 0x2F, uint32_t(start[i]));
    const uint16_t parent = items[i].parent < 0 ? 0xFFFF : uint16_t(items[i].parent);
    e[0x32] = uint8_t(parent >> 8);
    e[0x33] = uint8_t(parent);
    PutBe32(image, i * 0x40 + Physical(0) + 0x34, uint32_t(items[i].data.size()));
    for (uint64_t b = 0; b * kBlock < items[i].data.size(); ++b) {
      const size_t n = std::min<size_t>(kBlock, items[i].data.size() - b * kBlock);
      std::memcpy(&image[Physical(start[i] + b)], &items[i].data[b * kBlock], n);
    }
  }
  image.resize(image.size() - options.truncate);
  return image;
}

// One file over two blocks that are not next to each other: block 5 holds the start, and its hash entry names
// block 9 as the continuation.
Bytes BuildFragmentedPackage(uint32_t continuation) {
  constexpr uint64_t kFirst = 5;
  constexpr uint64_t kSecond = 9;
  Bytes image(Physical(kSecond) + kBlock);
  std::memcpy(image.data(), "LIVE", 4);
  PutBe32(image, 0x340, 0xAD0E);
  PutBe32(image, 0x344, kGameContentType);
  PutBe32(image, 0x360, kTitleId);
  image[0x379] = 0x24;
  image[0x37B] = 1;
  image[0x37C] = 1;
  PutLe24(image, 0x37E, 0);
  uint8_t* entry = &image[Physical(0)];
  std::memcpy(entry, "default.xex", 11);
  entry[0x28] = 11;
  entry[0x32] = 0xFF;
  entry[0x33] = 0xFF;
  PutLe24(image, Physical(0) + 0x29, 2);
  PutLe24(image, Physical(0) + 0x2C, 2);
  PutLe24(image, Physical(0) + 0x2F, uint32_t(kFirst));
  PutBe32(image, Physical(0) + 0x34, uint32_t(kBlock + 100));
  std::fill_n(&image[Physical(kFirst)], kBlock, uint8_t(0xA1));
  std::fill_n(&image[Physical(kSecond)], 100, uint8_t(0xB2));
  const uint64_t hash_entry = kBase + kFirst * 0x18;
  image[hash_entry + 0x15] = uint8_t(continuation >> 16);
  image[hash_entry + 0x16] = uint8_t(continuation >> 8);
  image[hash_entry + 0x17] = uint8_t(continuation);
  return image;
}

void WriteFile(const fs::path& path, const Bytes& data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

Bytes ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Fresh, empty test folder. Only ever under kRoot.
fs::path FreshDir(const std::string& name) {
  const fs::path dir = kRoot / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

// A game folder with the supported files, as a valid install leaves it.
void MakeInstalledGame(const fs::path& game, const Bytes& xex = Text("abc")) {
  WriteFile(game / "default.xex", xex);
  WriteFile(game / "Data.bin", Text("data"));
}

std::vector<Item> GameItems(const Bytes& xex) {
  return {File("default.xex", xex), File("Data.bin", Text("data")), File("Text-en.loc", Text("text"))};
}

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

void TestSha256() {
  Section("sha256");
  CHECK(Sha256Hex(kRoot / "missing.bin").empty());
  const fs::path empty = FreshDir("sha") / "empty.bin";
  WriteFile(empty, {});
  CHECK(Sha256Hex(empty) == "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
  const fs::path abc = FreshDir("sha") / "abc.bin";
  WriteFile(abc, Text("abc"));
  CHECK(Sha256Hex(abc) == kAbcSha256);
  const fs::path large = FreshDir("sha") / "large.bin";
  WriteFile(large, Bytes(2500000, 0x61));
  CHECK(Sha256Hex(large) == "38A637965059125EEB67F54C30E7F48A61859A467A800BA09740BA48A924F2B9");
}

void TestGameFiles() {
  Section("game files");
  const GameFingerprint expected = AbcFingerprint();

  const fs::path empty = FreshDir("game_empty");
  CHECK(CheckGameFiles(empty, expected).state == GameFileState::kMissing);
  CHECK(CheckGameFiles(fs::path{}, expected).state == GameFileState::kMissing);

  const fs::path partial = FreshDir("game_partial");
  WriteFile(partial / "default.xex", Text("abc"));
  CHECK(CheckGameFiles(partial, expected).state == GameFileState::kMissing);

  const fs::path valid = FreshDir("game_valid");
  MakeInstalledGame(valid);
  const GameCheck ready = CheckGameFiles(valid, expected);
  CHECK(ready.state == GameFileState::kValid);
  CHECK(ready.xex_sha256 == kAbcSha256);

  const fs::path other_size = FreshDir("game_size");
  MakeInstalledGame(other_size, Text("abcd"));
  CHECK(CheckGameFiles(other_size, expected).state == GameFileState::kDifferentVersion);

  const fs::path other_bytes = FreshDir("game_bytes");
  MakeInstalledGame(other_bytes, Text("abd"));
  const GameCheck different = CheckGameFiles(other_bytes, expected);
  CHECK(different.state == GameFileState::kDifferentVersion);
  CHECK(different.xex_sha256 != kAbcSha256);

#if defined(_WIN32)
  const fs::path locked = FreshDir("game_locked");
  MakeInstalledGame(locked);
  HANDLE lock = CreateFileW((locked / "default.xex").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(lock != INVALID_HANDLE_VALUE);
  CHECK(CheckGameFiles(locked, expected).state == GameFileState::kReadError);
  if (lock != INVALID_HANDLE_VALUE) {
    CloseHandle(lock);
  }
#endif

  CHECK(kSupportedGame.xex_size == 704512);
  CHECK(kSupportedGame.xex_sha256 == "6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88");
}

void TestStfsParsing() {
  Section("stfs parsing");
  const fs::path dir = FreshDir("stfs_parse");

  WriteFile(dir / "not.bin", Text("this is not an STFS package and has no LIVE magic in it"));
  stfs::Package not_package;
  CHECK(not_package.Open(dir / "not.bin").status == stfs::Status::kNotPackage);

  stfs::Package missing;
  CHECK(missing.Open(dir / "absent.bin").status == stfs::Status::kOpenFailed);

  WriteFile(dir / "good.bin", BuildPackage(GameItems(Text("abc"))));
  stfs::Package good;
  CHECK(good.Open(dir / "good.bin").ok());
  CHECK(good.title_id() == kTitleId);
  CHECK(good.content_type() == kGameContentType);
  CHECK(good.LoadFiles().ok());
  CHECK(good.file_count() == 3);
  CHECK(good.total_bytes() == 3 + 4 + 4);

  for (const char* name : {"..", ".", "a\\b", "x:y", "CON", "NUL.txt", "bad.", "tab\tname"}) {
    WriteFile(dir / "unsafe.bin", BuildPackage({File(name, Text("x"))}));
    stfs::Package unsafe;
    CHECK(unsafe.Open(dir / "unsafe.bin").ok());
    CHECK(unsafe.LoadFiles().status == stfs::Status::kUnsafePath);
  }

  WriteFile(dir / "cut_data.bin", BuildPackage({File("default.xex", Text("abc"))}, {.truncate = 0x1000}));
  stfs::Package cut;
  CHECK(cut.Open(dir / "cut_data.bin").ok());
  CHECK(cut.LoadFiles().status == stfs::Status::kCorrupt);

  WriteFile(dir / "fragmented.bin", BuildFragmentedPackage(9));
  stfs::Package fragmented;
  CHECK(fragmented.Open(dir / "fragmented.bin").ok());
  CHECK(fragmented.LoadFiles().ok());
  CHECK(fragmented.ExtractTo(dir / "fragmented_out", {}).ok());
  Bytes joined(kBlock, 0xA1);
  joined.insert(joined.end(), 100, 0xB2);
  CHECK(ReadFile(dir / "fragmented_out" / "default.xex") == joined);

  WriteFile(dir / "broken_chain.bin", BuildFragmentedPackage(0xFFFFFF));
  stfs::Package broken;
  CHECK(broken.Open(dir / "broken_chain.bin").ok());
  CHECK(broken.LoadFiles().ok());
  CHECK(broken.ExtractTo(dir / "broken_out", {}).status == stfs::Status::kCorrupt);
}

void TestPackageInstall() {
  Section("package install");
  const GameFingerprint expected = AbcFingerprint();

  {
    const fs::path root = FreshDir("install_ok");
    const fs::path game = root / "game";
    WriteFile(root / "good.bin", BuildPackage(GameItems(Text("abc"))));
    const InstallResult result = InstallFromPackage(root / "good.bin", game, {}, expected);
    CHECK(result.ok());
    CHECK(result.files == 3);
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
    CHECK(ReadFile(game / "Text-en.loc") == Text("text"));
    CHECK(!Exists(root / "game-install-temp"));
    CHECK(!Exists(root / "game-install-backup"));
  }

  {
    const fs::path root = FreshDir("install_wrong_title");
    const fs::path game = root / "game";
    MakeInstalledGame(game);
    WriteFile(root / "wrong.bin", BuildPackage(GameItems(Text("abc")), {.title = 0x12345678}));
    CHECK(InstallFromPackage(root / "wrong.bin", game, {}, expected).status == InstallStatus::kWrongTitle);
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
  }

  {
    const fs::path root = FreshDir("install_dlc_type");
    WriteFile(root / "dlc.bin", BuildPackage(GameItems(Text("abc")), {.content_type = 0x00000002}));
    CHECK(InstallFromPackage(root / "dlc.bin", root / "game", {}, expected).status == InstallStatus::kNotGamePackage);
  }

  {
    const fs::path root = FreshDir("install_not_package");
    WriteFile(root / "junk.bin", Text(std::string(0x800, 'z')));
    CHECK(InstallFromPackage(root / "junk.bin", root / "game", {}, expected).status == InstallStatus::kNotPackage);
    CHECK(InstallFromPackage(root, root / "game", {}, expected).status == InstallStatus::kOpenFailed);
  }

  {
    const fs::path root = FreshDir("install_wrong_xex");
    const fs::path game = root / "game";
    MakeInstalledGame(game);
    WriteFile(root / "other.bin", BuildPackage(GameItems(Text("abd"))));
    CHECK(InstallFromPackage(root / "other.bin", game, {}, expected).status == InstallStatus::kWrongVersion);
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
    CHECK(!Exists(root / "game-install-temp"));
  }

  {
    const fs::path root = FreshDir("install_cancelled");
    const fs::path game = root / "game";
    MakeInstalledGame(game);
    WriteFile(root / "good.bin", BuildPackage(GameItems(Text("abc"))));
    InstallHooks hooks;
    hooks.progress = [](uint64_t, uint64_t) { return false; };
    CHECK(InstallFromPackage(root / "good.bin", game, hooks, expected).status == InstallStatus::kCancelled);
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
    CHECK(!Exists(root / "game-install-temp"));
  }

  {
    const fs::path root = FreshDir("reinstall_damaged");
    const fs::path game = root / "game";
    MakeInstalledGame(game);
    WriteFile(root / "cut.bin", BuildPackage(GameItems(Text("abc")), {.truncate = 0x3000}));
    CHECK(InstallFromPackage(root / "cut.bin", game, {}, expected).status == InstallStatus::kDamagedPackage);
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
    CHECK(ReadFile(game / "Data.bin") == Text("data"));
    CHECK(!Exists(root / "game-install-temp"));
  }

  {
    const fs::path root = FreshDir("reinstall_replaces");
    const fs::path game = root / "game";
    MakeInstalledGame(game, Text("abd"));
    WriteFile(root / "good.bin", BuildPackage(GameItems(Text("abc"))));
    CHECK(InstallFromPackage(root / "good.bin", game, {}, expected).ok());
    CHECK(CheckGameFiles(game, expected).state == GameFileState::kValid);
    CHECK(!Exists(root / "game-install-backup"));
  }

  {
    const fs::path root = FreshDir("foreign_folder");
    const fs::path game = root / "game";
    WriteFile(game / "notes.txt", Text("mine"));
    WriteFile(root / "good.bin", BuildPackage(GameItems(Text("abc"))));
    CHECK(InstallFromPackage(root / "good.bin", game, {}, expected).status == InstallStatus::kForeignFolder);
    CHECK(ReadFile(game / "notes.txt") == Text("mine"));
  }

  {
    const fs::path root = FreshDir("user_data_untouched");
    const fs::path game = root / "game";
    const fs::path user = root / "user_data";
    MakeInstalledGame(game);
    WriteFile(user / "584108FF.profile", Text("profile"));
    WriteFile(user / "achievements" / "584108FF.toml", Text("unlocked"));
    WriteFile(user / "launcher" / "settings.toml", Text("show_launcher = false\n"));
    WriteFile(root / "good.bin", BuildPackage(GameItems(Text("abc"))));
    CHECK(InstallFromPackage(root / "good.bin", game, {}, expected).ok());
    CHECK(InstallFromPackage(root / "good.bin", game, {}, expected).ok());
    CHECK(ReadFile(user / "584108FF.profile") == Text("profile"));
    CHECK(ReadFile(user / "achievements" / "584108FF.toml") == Text("unlocked"));
    CHECK(ReadFile(user / "launcher" / "settings.toml") == Text("show_launcher = false\n"));
  }
}

void TestWindowFit() {
  Section("window fit");
  const MonitorInfo fhd{1920, 1080, 1920, 1040, 1.0f, true};
  CHECK(FitWindowSize(1280, 720, fhd) == std::make_pair(1280, 720));
  const MonitorInfo fhd150{1920, 1080, 1920, 1040, 1.5f, true};
  CHECK(FitWindowSize(1280, 720, fhd150) == std::make_pair(1152, 624));
  const MonitorInfo fhd225{1920, 1080, 1920, 1040, 2.25f, true};
  CHECK(FitWindowSize(1280, 720, fhd225) == std::make_pair(768, 416));
  CHECK(FitWindowSize(1280, 720, MonitorInfo{}) == std::make_pair(1280, 720));
}

void TestAchievementModel() {
  Section("achievement model");
  std::vector<AchievementEntry> none;
  const AchievementTotals empty = TotalAchievements(none);
  CHECK(empty.total == 0 && empty.unlocked == 0 && empty.gamerscore_total == 0 && empty.gamerscore_earned == 0);

  AchievementEntry first;
  first.info.id = 1;
  first.info.gamerscore = 12;
  first.info.description = "Earned description";
  first.info.unachieved_description = "Locked description";
  first.unlocked = true;
  AchievementEntry second;
  second.info.id = 2;
  second.info.gamerscore = 30;
  second.info.description = "Second earned";
  second.info.unachieved_description = "Second locked";
  AchievementEntry third;
  third.info.id = 3;
  third.info.gamerscore = 50;
  third.info.description = "Third earned";
  third.unlocked = true;
  const std::vector<AchievementEntry> entries = {first, second, third};

  const AchievementTotals totals = TotalAchievements(entries);
  CHECK(totals.total == 3);
  CHECK(totals.unlocked == 2);
  CHECK(totals.gamerscore_total == 92);
  CHECK(totals.gamerscore_earned == 62);

  CHECK(AchievementDescription(first) == "Earned description");
  CHECK(AchievementDescription(second) == "Second locked");

  AchievementEntry no_locked_text = second;
  no_locked_text.info.unachieved_description.clear();
  CHECK(AchievementDescription(no_locked_text) == "Second earned");
}

void TestLogFiles() {
  Section("log files");
  const fs::path dir = FreshDir("logs") / "nested";
  CHECK(NextLogFile(dir, "gw2") == dir / "gw2_001.log");
  CHECK(fs::is_directory(dir));
  WriteFile(dir / "gw2_001.log", Text("a"));
  WriteFile(dir / "gw2_007.log", Text("b"));
  WriteFile(dir / "other_099.log", Text("c"));
  WriteFile(dir / "gw2_008.txt", Text("d"));
  CHECK(NextLogFile(dir, "gw2") == dir / "gw2_008.log");
  WriteFile(kRoot / "not_a_folder", Text("x"));
  CHECK(NextLogFile(kRoot / "not_a_folder", "gw2").empty());
}

}  // namespace

int main() {
  std::error_code ec;
  fs::remove_all(kRoot, ec);
  TestLogFiles();
  TestSha256();
  TestGameFiles();
  TestStfsParsing();
  TestPackageInstall();
  TestWindowFit();
  TestAchievementModel();
  fs::remove_all(kRoot, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
