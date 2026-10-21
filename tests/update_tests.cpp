// Updater tests: semantic versions, release parsing, asset selection, SHA-256, ZIP safety, staging,
// manifest and rollback behavior. Run: gw2_update_tests

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "updater/github_release.h"
#include "updater/install_transaction.h"
#include "updater/sha256.h"
#include "updater/third_party/miniz/miniz.h"
#include "updater/update_job.h"
#include "updater/update_paths.h"
#include "updater/update_zip.h"
#include "updater/version.h"

using namespace trg::update;

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

class TempDir {
 public:
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("gw2-update-tests-" + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), std::streamsize(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
  mz_zip_archive archive{};
  if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE) {
    return {};
  }
  for (const auto& [name, content] : entries) {
    if (mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION) ==
        MZ_FALSE) {
      mz_zip_writer_end(&archive);
      return {};
    }
  }
  void* data = nullptr;
  size_t size = 0;
  if (mz_zip_writer_finalize_heap_archive(&archive, &data, &size) == MZ_FALSE) {
    mz_zip_writer_end(&archive);
    return {};
  }
  std::string result(static_cast<const char*>(data), size);
  mz_free(data);
  mz_zip_writer_end(&archive);
  return result;
}

void TestVersions() {
  std::printf("semantic versions\n");
  const auto v031 = ParseVersion("v0.3.1");
  const auto v032rc1 = ParseVersion("0.3.2-rc1");
  const auto v032rc2 = ParseVersion("0.3.2-rc2");
  const auto v032 = ParseVersion("0.3.2");
  const auto v0310 = ParseVersion("0.3.10");
  const auto v040 = ParseVersion("0.4.0");
  const auto v100 = ParseVersion("1.0.0");
  CHECK(v031 && v032rc1 && v032rc2 && v032 && v0310 && v040 && v100);
  CHECK(*v031 < *v032rc1);
  CHECK(*v032rc1 < *v032rc2);
  CHECK(*v032rc2 < *v032);
  CHECK(*v032 < *v0310);
  CHECK(*v0310 < *v040);
  CHECK(*v040 < *v100);
  CHECK(v032rc1->CoreString() == "0.3.2");
  CHECK(v032rc1->ToString() == "0.3.2-rc1");
  CHECK(v040->CoreString() == "0.4.0");

  const auto rc = ParseVersion("0.3.2-rc");
  const auto rc1 = ParseVersion("0.3.2-rc.1");
  const auto rc10 = ParseVersion("0.3.2-rc.10");
  CHECK(rc && rc1 && rc10);
  CHECK(*rc < *rc1);
  CHECK(*rc1 < *rc10);
  CHECK(*rc10 < *v032);
  CHECK(SemanticVersion::Compare(*ParseVersion("0.3.2+build.5"), *v032) == 0);
  CHECK(ParseVersion("0.3.2+build.5")->build == "build.5");
  CHECK(ParseVersion("0.3.2-rc1+build.5")->prerelease.size() == 1);

  CHECK(!ParseVersion("1.0"));
  CHECK(!ParseVersion("1.0.0.0"));
  CHECK(!ParseVersion("01.0.0"));
  CHECK(!ParseVersion("1.0.0-"));
  CHECK(!ParseVersion("v"));
  CHECK(ParseVersion("V0.4.0").has_value());
  CHECK(ParseVersion("0.3.2-RC1").has_value());
}

void TestReleases() {
  std::printf("release JSON and asset selection\n");
  constexpr const char* kJson = R"({
    "tag_name": "v0.4.0",
    "draft": false,
    "prerelease": false,
    "html_url": "https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/releases/tag/v0.4.0",
    "assets": [
      {"name": "GeometryWarsRetroEvolved2-v0.4.0-windows-x64.zip",
       "browser_download_url": "https://github.com/example/windows.zip",
       "size": 123456,
       "digest": "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
      {"name": "GeometryWarsRetroEvolved2-v0.4.0-windows-x64.sha256",
       "browser_download_url": "https://github.com/example/windows.sha256",
       "size": 115,
       "digest": "sha256:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"},
      {"name": "GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.AppImage",
       "browser_download_url": "https://github.com/example/app.AppImage",
       "size": 654321,
       "digest": "sha256:CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"},
      {"name": "GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.sha256",
       "browser_download_url": "https://github.com/example/app.sha256",
       "size": 125,
       "digest": "sha256:DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"}
    ]
  })";

  std::string error;
  const std::optional<Release> release = ParseRelease(kJson, &error);
  CHECK(release.has_value());
  CHECK(error.empty());
  if (!release) {
    return;
  }
  CHECK(release->tag == "v0.4.0");
  CHECK(release->version.CoreString() == "0.4.0");
  CHECK(IsInstallableRelease(*release));
  CHECK(release->html_url.rfind("https://", 0) == 0);
  CHECK(release->assets.size() == 4);

  const ReleaseAsset* windows =
      FindAsset(*release, "GeometryWarsRetroEvolved2-v0.4.0-windows-x64.zip");
  const ReleaseAsset* windows_sha =
      FindAsset(*release, "GeometryWarsRetroEvolved2-v0.4.0-windows-x64.sha256");
  const ReleaseAsset* linux =
      FindAsset(*release, "GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.AppImage");
  const ReleaseAsset* linux_sha =
      FindAsset(*release, "GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.sha256");
  CHECK(windows != nullptr);
  CHECK(windows_sha != nullptr);
  CHECK(linux != nullptr);
  CHECK(linux_sha != nullptr);
  CHECK(windows && windows->size == 123456);
  CHECK(windows && windows->sha256() ==
                        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(linux && linux->size == 654321);
  CHECK(FindAsset(*release, "GeometryWarsRetroEvolved2-v0.3.1-windows-x64.zip") == nullptr);
  CHECK(FindAsset(*release, "different.zip") == nullptr);

  const std::optional<Release> draft = ParseRelease(R"({
    "tag_name":"v0.4.1","draft":true,"prerelease":false,"assets":[]
  })");
  CHECK(draft && !IsInstallableRelease(*draft));
  const std::optional<Release> prerelease = ParseRelease(R"({
    "tag_name":"v0.4.1-rc1","draft":false,"prerelease":true,"assets":[]
  })");
  CHECK(prerelease && !IsInstallableRelease(*prerelease));
  const std::optional<Release> prerelease_tag = ParseRelease(R"({
    "tag_name":"v0.4.1-rc1","draft":false,"prerelease":false,"assets":[]
  })");
  CHECK(prerelease_tag && !IsInstallableRelease(*prerelease_tag));

  CHECK(!ParseRelease(R"({"tag_name": 42})").has_value());
  CHECK(!ParseRelease(R"({"tag_name": "not-a-version"})").has_value());
  CHECK(!ParseRelease("{").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.4.0","assets":{}})").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.4.0","assets":[{"name":"x"}]})").has_value());

  CHECK(ParseSha256Digest("sha256:abc") == std::nullopt);
  CHECK(ParseSha256Digest("md5:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == std::nullopt);
  CHECK(ParseSha256Digest("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ==
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(ParseSha256File("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789  file.zip\n") ==
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
  CHECK(ParseSha256File("not-a-hash  file.zip") == std::nullopt);
}

void TestSha256() {
  std::printf("SHA-256\n");
  CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string long_text(1000000, 'a');
  CHECK(Sha256Hex(long_text) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  TempDir temp;
  const std::filesystem::path file = temp.path / "hash.bin";
  WriteFile(file, "abc");
  std::string error;
  const std::string actual = Sha256FileHex(file, error);
  CHECK(actual == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(error.empty());
  CHECK(actual != "0000000000000000000000000000000000000000000000000000000000000000");
}

void TestZipAndStaging() {
  std::printf("ZIP extraction and staging\n");
  TempDir temp;
  const std::filesystem::path archive = temp.path / "safe.zip";
  const std::string safe = MakeZip({
      {"geometry_wars_retro_evolved_2.exe", "launcher"},
      {"rexruntime.dll", "runtime"},
      {"assets/launcher/header-art.png", "art"},
      {"licenses/ReXGlue-LICENSE.txt", "license"},
  });
  CHECK(!safe.empty());
  WriteFile(archive, safe);
  const std::filesystem::path staging = temp.path / "staging";
  std::string error;
  CHECK(ExtractZip(archive, staging, {}, error));
  CHECK(ReadFile(staging / "geometry_wars_retro_evolved_2.exe") == "launcher");
  CHECK(ReadFile(staging / "assets" / "launcher" / "header-art.png") == "art");
  CHECK(ValidateStaging(staging, {"geometry_wars_retro_evolved_2.exe", "assets/launcher/header-art.png"}, error));
  CHECK(ListStagingFiles(staging, error).size() == 4);

  WriteFile(staging / "game" / "default.xex", "game");
  CHECK(!ValidateStaging(staging, {"geometry_wars_retro_evolved_2.exe"}, error));
  std::filesystem::remove_all(staging / "game");

  WriteFile(staging / "settings.toml", "settings");
  CHECK(!ValidateStaging(staging, {"geometry_wars_retro_evolved_2.exe"}, error));
  std::filesystem::remove(staging / "settings.toml");

  WriteFile(staging / "logs" / "a.log", "log");
  CHECK(!ValidateStaging(staging, {"geometry_wars_retro_evolved_2.exe"}, error));
  std::filesystem::remove_all(staging / "logs");

  WriteFile(staging / "user_data" / "584108FF" / "profile" / "a", "profile");
  CHECK(std::filesystem::exists(staging / "user_data" / "584108FF" / "profile" / "a"));
  CHECK(IsProtectedRelativePath("user_data/584108FF/profile/a"));
  CHECK(!ValidateStaging(staging, {"geometry_wars_retro_evolved_2.exe"}, error));
  std::filesystem::remove_all(staging / "user_data");

  CHECK(!ValidateStaging(staging, {"missing.exe"}, error));

  const std::vector<std::string> unsafe_names = {
      "../evil",         "C:/evil",          "C:\\evil",        "\\\\server\\share", "a/../../b",
      "a\\..\\b",        "a//b",             "game/default.xex", "settings.toml",    "a/b.log",
      "default.xex",     "Data.bin",         "C:evil",          "a/b.",              "con",
  };
  int rejected = 0;
  for (const std::string& name : unsafe_names) {
    const std::filesystem::path unsafe_archive = temp.path / "unsafe.zip";
    const std::string bytes = MakeZip({{name, "payload"}});
    if (bytes.empty()) {
      std::printf("  zip writer rejected %s\n", name.c_str());
    }
    CHECK(!bytes.empty());
    WriteFile(unsafe_archive, bytes);
    const std::filesystem::path unsafe_staging = temp.path / "unsafe-staging";
    std::filesystem::remove_all(unsafe_staging);
    if (!ExtractZip(unsafe_archive, unsafe_staging, {}, error)) {
      ++rejected;
    }
    CHECK(!std::filesystem::exists(temp.path / "evil"));
  }
  CHECK(rejected == int(unsafe_names.size()));
  CHECK(IsSymlinkZipEntry(0120000u << 16));
  CHECK(!IsSymlinkZipEntry(0100644u << 16));
  CHECK(!IsSymlinkZipEntry(0));

  const std::filesystem::path dot_archive = temp.path / "dot.zip";
  WriteFile(dot_archive, MakeZip({{"./geometry_wars_retro_evolved_2.exe", "launcher"}}));
  const std::filesystem::path dot_staging = temp.path / "dot-staging";
  CHECK(ExtractZip(dot_archive, dot_staging, {}, error));
  CHECK(std::filesystem::exists(dot_staging / "geometry_wars_retro_evolved_2.exe"));
}

void TestJobs() {
  std::printf("update jobs\n");
  TempDir temp;
  const std::filesystem::path install = temp.path / "install dir";
  const std::filesystem::path staging = temp.path / "staging dir";
  const std::filesystem::path backup = temp.path / "backup dir";
  const std::filesystem::path manifest = temp.path / "manifest.txt";
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(manifest, "geometry_wars_retro_evolved_2.exe\n");

  UpdateJob job;
  job.parent_pid = 1234;
  job.install_dir = install;
  job.staging_dir = staging;
  job.backup_dir = backup;
  job.manifest_file = manifest;
  job.restart_executable = install / "geometry_wars_retro_evolved_2.exe";
  job.log_file = temp.path / "updater.log";
  job.restart_arguments = {"--user_data_root=with space", "--fullscreen=true", ""};

  std::string error;
  const std::filesystem::path job_file = temp.path / "job.txt";
  CHECK(WriteUpdateJob(job_file, job, error));
  const std::optional<UpdateJob> read = ReadUpdateJob(job_file, error);
  CHECK(read.has_value());
  if (read) {
    CHECK(read->parent_pid == 1234);
    CHECK(read->install_dir == install);
    CHECK(read->restart_arguments == job.restart_arguments);
  }

  WriteFile(staging / "geometry_wars_retro_evolved_2.exe", "launcher");
  CHECK(ValidateInstallJob(job, error));

  UpdateJob inside = job;
  inside.staging_dir = install / "staging";
  std::filesystem::create_directories(inside.staging_dir);
  CHECK(!ValidateInstallJob(inside, error));

  UpdateJob relative = job;
  relative.install_dir = "install";
  CHECK(!ValidateInstallJob(relative, error));

  UpdateJob no_pid = job;
  no_pid.parent_pid = 0;
  CHECK(!ValidateInstallJob(no_pid, error));

  UpdateJob outside = job;
  outside.restart_executable = temp.path / "geometry_wars_retro_evolved_2.exe";
  CHECK(!ValidateInstallJob(outside, error));

  UpdateJob no_manifest = job;
  no_manifest.manifest_file = temp.path / "missing-manifest.txt";
  CHECK(!ValidateInstallJob(no_manifest, error));
}

void TestInstallAndRollback() {
  std::printf("install and rollback\n");
  TempDir temp;
  const std::filesystem::path install = temp.path / "install";
  const std::filesystem::path staging = temp.path / "staging";
  const std::filesystem::path backup = temp.path / "backup";
  const std::filesystem::path manifest = temp.path / "manifest.txt";
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(install / "geometry_wars_retro_evolved_2.exe", "old launcher");
  WriteFile(install / "rexruntime.dll", "old runtime");
  WriteFile(install / "keep.txt", "not owned by the release");
  WriteFile(install / "user_data" / "launcher" / "settings.toml", "player settings");
  WriteFile(install / "game" / "default.xex", "game files");
  WriteFile(install / "game" / "Data.bin", "game data");
  WriteFile(install / "logs" / "session.log", "log");

  auto stage = [&] {
    WriteFile(staging / "geometry_wars_retro_evolved_2.exe", "new launcher");
    WriteFile(staging / "trg-updater.exe", "new helper");
    WriteFile(staging / "rexruntime.dll", "new runtime");
    WriteFile(staging / "assets" / "launcher" / "header-art.png", "new header art");
    WriteFile(manifest, "geometry_wars_retro_evolved_2.exe\ntrg-updater.exe\nrexruntime.dll\n"
                        "assets/launcher/header-art.png\n");
  };
  stage();

  UpdateJob job;
  job.parent_pid = 1234;
  job.install_dir = install;
  job.staging_dir = staging;
  job.backup_dir = backup;
  job.manifest_file = manifest;
  job.restart_executable = install / "geometry_wars_retro_evolved_2.exe";
  job.log_file = temp.path / "updater.log";

  int replacements = 0;
  InstallOptions fail;
  fail.replace = [&replacements](const std::filesystem::path& from, const std::filesystem::path& to) {
    ++replacements;
    if (replacements == 3) {
      return false;
    }
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
  };
  const InstallOutcome failed = InstallFiles(job, fail, false);
  CHECK(failed.status == InstallStatus::kInstallFailedRolledBack);
  CHECK(ReadFile(install / "geometry_wars_retro_evolved_2.exe") == "old launcher");
  CHECK(!std::filesystem::exists(install / "trg-updater.exe"));
  CHECK(ReadFile(install / "rexruntime.dll") == "old runtime");
  CHECK(ReadFile(install / "user_data" / "launcher" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
  CHECK(ReadFile(install / "game" / "Data.bin") == "game data");
  CHECK(ReadFile(install / "logs" / "session.log") == "log");
  CHECK(ReadFile(install / "keep.txt") == "not owned by the release");

  stage();
  replacements = 0;
  const InstallOutcome success = InstallFiles(job, {}, false);
  CHECK(success.status == InstallStatus::kSuccess);
  CHECK(ReadFile(install / "geometry_wars_retro_evolved_2.exe") == "new launcher");
  CHECK(ReadFile(install / "trg-updater.exe") == "new helper");
  CHECK(ReadFile(install / "rexruntime.dll") == "new runtime");
  CHECK(ReadFile(install / "assets" / "launcher" / "header-art.png") == "new header art");
  CHECK(ReadFile(install / "user_data" / "launcher" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
  CHECK(ReadFile(install / "game" / "Data.bin") == "game data");
  CHECK(ReadFile(install / "logs" / "session.log") == "log");
  CHECK(ReadFile(install / "keep.txt") == "not owned by the release");

  stage();
  WriteFile(manifest, "game/default.xex\n");
  CHECK(InstallFiles(job, {}, false).status == InstallStatus::kInvalidJob);
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
}

void TestPackagingNames() {
  std::printf("packaging names\n");
  const std::string config = ReadFile(std::string(GW2_SRC_DIR) + "/update_config.cpp");
  const std::string windows_package = ReadFile(std::string(GW2_SRC_DIR) + "/../tools/package-release.ps1");
  const std::string linux_package = ReadFile(std::string(GW2_SRC_DIR) + "/../tools/package-linux.sh");
  const std::string tests = ReadFile(std::string(GW2_SRC_DIR) + "/../tools/test.ps1");
  CHECK(config.find("GeometryWarsRetroEvolved2-v\" + version.CoreString() + \"-windows-x64.zip") !=
        std::string::npos);
  CHECK(config.find("GeometryWarsRetroEvolved2Recompiled-v\" + version.CoreString() + \"-x86_64.AppImage") !=
        std::string::npos);
  CHECK(windows_package.find("GeometryWarsRetroEvolved2-v$Version-windows-x64") != std::string::npos);
  CHECK(windows_package.find("trg-updater.exe") != std::string::npos);
  CHECK(linux_package.find("GeometryWarsRetroEvolved2Recompiled-v$VERSION-x86_64") != std::string::npos);
  CHECK(tests.find("trg-updater.exe") != std::string::npos);
  CHECK(tests.find("assets\\launcher\\header-art.png") != std::string::npos);
}

void TestPaths() {
  std::printf("path safety\n");
  CHECK(IsSafeRelativePath("geometry_wars_retro_evolved_2.exe"));
  CHECK(IsSafeRelativePath("assets/launcher/header-art.png"));
  CHECK(IsSafeRelativePath("./geometry_wars_retro_evolved_2.exe"));
  CHECK(!IsSafeRelativePath("../geometry_wars_retro_evolved_2.exe"));
  CHECK(!IsSafeRelativePath("/absolute"));
  CHECK(!IsSafeRelativePath("C:/geometry_wars_retro_evolved_2.exe"));
  CHECK(!IsSafeRelativePath("\\\\server\\share"));
  CHECK(!IsSafeRelativePath("a//b"));
  CHECK(!IsSafeRelativePath("a/./../b"));
  CHECK(IsProtectedRelativePath("game/default.xex"));
  CHECK(IsProtectedRelativePath("game/Data.bin"));
  CHECK(IsProtectedRelativePath("game-install-temp/default.xex"));
  CHECK(IsProtectedRelativePath("game-install-backup/default.xex"));
  CHECK(IsProtectedRelativePath("user_data/launcher/settings.toml"));
  CHECK(IsProtectedRelativePath("user_data.migrating/launcher/settings.toml"));
  CHECK(IsProtectedRelativePath("logs/a.log"));
  CHECK(IsProtectedRelativePath("cache/shaders/a.bin"));
  CHECK(IsProtectedRelativePath("settings.toml"));
  CHECK(IsProtectedRelativePath("settings.toml.bak"));
  CHECK(IsProtectedRelativePath("584108FF/profile/User/a"));
  CHECK(!IsProtectedRelativePath("geometry_wars_retro_evolved_2.exe"));
  CHECK(!IsProtectedRelativePath("geometry_wars_retro_evolved_2.toml"));
  CHECK(!IsProtectedRelativePath("licenses/THIRD-PARTY-LICENSES.txt"));
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TestVersions();
  TestReleases();
  TestSha256();
  TestZipAndStaging();
  TestJobs();
  TestInstallAndRollback();
  TestPackagingNames();
  TestPaths();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
