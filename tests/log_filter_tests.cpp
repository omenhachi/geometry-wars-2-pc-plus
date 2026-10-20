// Detailed logging: the setting value and fallback, the Minimal filter's classification and its effect on the
// rotating log file, and the session-log lifecycle that keeps a crash log but discards a normal-session log when
// Detailed logging is off. Run: gw2_log_filter_tests

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "active_log.h"
#include "crash_report.h"
#include "log_filter.h"
#include "session_log.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(GW2_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteText(const std::filesystem::path& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

void SetDetailedLogging(bool enabled) {
  rex::cvar::SetFlagByName("detailed_logging", enabled ? "true" : "false");
  gw2::ApplyDetailedLogging();
}

void TestClassification() {
  std::printf("Minimal mode keeps failures and first-line diagnostics\n");
  using spdlog::level::debug;
  using spdlog::level::err;
  using spdlog::level::info;
  using spdlog::level::trace;
  using spdlog::level::warn;

  CHECK(!gw2::ShouldLogInMinimalMode(info, "core", "Launcher shown after the runtime"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Game files: valid (default.xex 704512 bytes, SHA-256 ABC)"));
  CHECK(!gw2::ShouldLogInMinimalMode(info, "gpu", "GPU wait for idle = 00000000"));
  CHECK(!gw2::ShouldLogInMinimalMode(info, "krnl", "Function table initialized for module: code=..."));
  CHECK(!gw2::ShouldLogInMinimalMode(debug, "core", "RexApp starting"));
  CHECK(!gw2::ShouldLogInMinimalMode(trace, "core", "per-instruction detail"));

  CHECK(gw2::ShouldLogInMinimalMode(warn, "core", "Window opened on another display"));
  CHECK(gw2::ShouldLogInMinimalMode(err, "gpu", "Failed to create a Direct3D 12 device"));
  CHECK(gw2::ShouldLogInMinimalMode(spdlog::level::critical, "core", "fatal"));

  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Geometry Wars: Retro Evolved 2 port 0.4.0"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Build: Release x86-64 | ReXGlue SDK 0.10.0 | Direct3D 12"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Runtime OS: Windows 11 64-bit (build 22631)"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Runtime GPU: selected=Automatic | resolved=Test GPU"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Visual interpolation enabled: measured presentation 143.9 Hz"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "User data: portable path selected: user_data"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Player settings: settings.toml"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Graphics card: Test GPU (adapter 0)"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Internal render resolution: 75% 960x540 -> 1280x720"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "gpu", "DXGI adapter: Test GPU (vendor 0x1002, device 0x1234)"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Aspect ratio: 16:9 (1280x720)"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Multisampling: 2x MSAA"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Windows crash reporting enabled for current log."));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Execution complete: clean exit, uptime 00:12:43"));
  CHECK(gw2::ShouldLogInMinimalMode(info, "core", "Window closing, hard-exiting process."));
}

void TestSettingValue(const std::filesystem::path& dir) {
  std::printf("setting default, persistence and malformed fallback\n");
  const std::filesystem::path file = dir / "settings.toml";

  rex::cvar::ResetToDefault("detailed_logging");
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(gw2::DetailedLoggingEnabled());

  WriteText(file, "detailed_logging = false\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "false");
  CHECK(!gw2::DetailedLoggingEnabled());

  WriteText(file, "detailed_logging = true\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");

  rex::cvar::ResetToDefault("detailed_logging");
  WriteText(file, "# no detailed_logging here\nwindow_width = 1280\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");

  rex::cvar::ResetToDefault("detailed_logging");
  WriteText(file, "detailed_logging = \"garbage\"\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
}

void TestCrashReportIndependent() {
  std::printf("CRASH REPORT formatting does not depend on the setting\n");
  gw2::CrashReport report;
  report.time = {2026, 10, 19, 12, 0, 0, 0};
  report.code = 0xC0000005u;
  report.address = 0x00007FF6A1B2C3D4ull;
  report.thread_id = 7;
  report.process_id = 9;
  report.architecture = "x86-64";
  report.game_version = "0.4.0";

  char detailed[8192];
  const size_t detailed_length = gw2::FormatCrashReport(report, detailed, sizeof(detailed));

  SetDetailedLogging(false);
  char minimal[8192];
  const size_t minimal_length = gw2::FormatCrashReport(report, minimal, sizeof(minimal));
  CHECK(detailed_length == minimal_length);
  CHECK(std::string(detailed, detailed_length) == std::string(minimal, minimal_length));
  CHECK(Has(std::string(detailed, detailed_length), "CRASH REPORT\n"));
  CHECK(Has(std::string(detailed, detailed_length), "Exception: ACCESS_VIOLATION"));
  CHECK(Has(std::string(detailed, detailed_length), "Exception code: 0xC0000005"));
}

void TestSessionLifecycle(const std::filesystem::path& dir) {
  std::printf("Detailed retains the session log, Minimal discards only the active log\n");
  rex::LogConfig config;
  config.app_name = "gw2_session_test";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  REXLOG_INFO("Geometry Wars: Retro Evolved 2 port 0.4.0");
  REXLOG_INFO("Build: Release x86-64 | ReXGlue SDK 0.10.0 | Direct3D 12");
  rex::FlushLogging();

  const std::filesystem::path active = gw2::ActiveLogFile();
  CHECK(!active.empty());
  CHECK(std::filesystem::exists(active));

  SetDetailedLogging(true);
  CHECK(!gw2::MinimalLogFilterInstalled());
  REXLOG_INFO("launcher detailed segment");
  REXLOG_INFO("Execution complete");
  REXLOG_INFO("Execution complete: clean exit, uptime 00:00:01");
  rex::FlushLogging();
  CHECK(gw2::ActiveLogFile() == active);

  SetDetailedLogging(false);
  CHECK(gw2::MinimalLogFilterInstalled());
  REXLOG_INFO("launcher minimal segment");
  REXLOG_INFO("Execution complete");
  REXLOG_INFO("Windows crash reporting enabled for current log.");
  REXLOG_INFO("ordinary detailed chatter");
  CHECK(gw2::ActiveLogFile() == active);
  REXLOG_INFO("game minimal segment");
  REXLOG_WARN("a warning survives");
  REXLOG_INFO("Graphics card: Test GPU (adapter 0)");
  rex::FlushLogging();

  const std::string log = ReadText(active);
  CHECK(Has(log, "launcher detailed segment"));
  CHECK(!Has(log, "launcher minimal segment"));
  CHECK(!Has(log, "ordinary detailed chatter"));
  CHECK(!Has(log, "game minimal segment"));
  CHECK(Has(log, "a warning survives"));
  CHECK(Has(log, "Graphics card: Test GPU"));
  CHECK(!Has(log, "Execution complete\n"));
  CHECK(!Has(log, "Execution complete\r\n"));
  CHECK(Has(log, "Execution complete: clean exit, uptime 00:00:01"));

  SetDetailedLogging(true);
  gw2::FinalizeSessionLog();
  CHECK(std::filesystem::exists(active));

  // A log that already has a crash block is kept even with Detailed logging off.
  const std::filesystem::path crashed = dir / "crashed.log";
  WriteText(crashed, "run started\nCRASH REPORT\n");
  rex::cvar::SetFlagByName("log_file", crashed.string());
  SetDetailedLogging(false);
  gw2::FinalizeSessionLog();
  CHECK(std::filesystem::exists(crashed));
  rex::cvar::SetFlagByName("log_file", "");

  const std::filesystem::path decoy = dir / "geometry_wars_retro_evolved_2_900.log";
  WriteText(decoy, "decoy\n");
  SetDetailedLogging(false);
  gw2::FinalizeSessionLog();
  CHECK(!std::filesystem::exists(active));
  CHECK(std::filesystem::exists(decoy));
}

#if defined(_WIN32)

std::wstring SelfPath() {
  wchar_t path[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

bool StartChild(const std::wstring& arguments) {
  std::wstring command = L"\"" + SelfPath() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

DWORD RunChild(const std::wstring& arguments) {
  std::wstring command = L"\"" + SelfPath() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    return 0xFFFFFFFFu;
  }
  DWORD exit_code = 0xFFFFFFFFu;
  if (WaitForSingleObject(process.hProcess, 30000) == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, exit_code);
  } else {
    GetExitCodeProcess(process.hProcess, &exit_code);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exit_code;
}

bool WaitForLogText(const std::filesystem::path& dir, const char* needle) {
  for (int attempt = 0; attempt < 500; ++attempt) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".log" &&
          Has(ReadText(entry.path()), needle)) {
        return true;
      }
    }
    Sleep(20);
  }
  return false;
}

int RunChildMode(const std::wstring& mode, const std::filesystem::path& dir) {
  if (mode == L"handoff-child") {
    const auto inherited = gw2::TakeInheritedSessionLog();
    if (!inherited) {
      return 3;
    }
    rex::LogConfig config;
    const std::string path = inherited->string();
    config.log_file = path.c_str();
    config.default_level = spdlog::level::info;
    rex::InitLogging(config);
    SetDetailedLogging(true);
    REXLOG_INFO("game detailed segment");
    return 0;
  }

  rex::LogConfig config;
  config.app_name = "gw2_handoff_parent";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  gw2::InstallSessionLogCleanup();
  SetDetailedLogging(true);
  REXLOG_INFO("launcher detailed segment");
  gw2::BeginSessionLogHandoff();
  if (!StartChild(L"--child handoff-child \"" + dir.wstring() + L"\"")) {
    gw2::CancelSessionLogHandoff();
    return 2;
  }
  return 0;
}

void TestHandoff(const std::filesystem::path& root) {
  std::printf("a relaunch appends to the same session log\n");
  const std::filesystem::path dir = root / "handoff";
  std::filesystem::create_directories(dir);
  CHECK(RunChild(L"--child handoff-parent \"" + dir.wstring() + L"\"") == 0);
  CHECK(WaitForLogText(dir, "game detailed segment"));

  std::filesystem::path log;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      log = entry.path();
      break;
    }
  }
  CHECK(!log.empty());
  if (!log.empty()) {
    const std::string text = ReadText(log);
    CHECK(Has(text, "launcher detailed segment"));
    CHECK(Has(text, "game detailed segment"));
  }
}

#endif

void TestWiring() {
  std::printf("wiring in the app and launcher\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Heading(\"Diagnostics\")"));
  CHECK(Has(launcher, "ui.Toggle(\"Detailed logging\""));
  CHECK(Has(launcher, "rex::cvar::SetFlagByName(\"detailed_logging\""));
  CHECK(Has(launcher, "ApplyDetailedLogging();"));

  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "gw2::ApplyDetailedLogging();"));
  CHECK(Has(app, "gw2::InstallSessionLogCleanup();"));
  CHECK(Has(app, "gw2::FinalizeSessionLog();"));
  CHECK(Has(app, "gw2::TakeInheritedSessionLog()"));

  const std::string launcher_source = ReadSource("launcher.cpp");
  CHECK(Has(launcher_source, "Game files: {} (default.xex"));
  CHECK(!Has(launcher_source, "Game files in {}"));
  const std::string cvars_source = ReadSource("launcher_cvars.cpp");
  CHECK(Has(cvars_source, "\"Player settings: {}\", settings_name"));
  CHECK(!Has(cvars_source, "\"Player settings: {}\", Utf8(player_file)"));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "DetailedLoggingEnabled()"));
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "ShutdownLogging()"));
  CHECK(Has(session, "GW2_SESSION_LOG"));
  CHECK(!Has(session, "remove_all"));
}

}  // namespace

int main() {
#if defined(_WIN32)
  int wide_argc = 0;
  LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv != nullptr && wide_argc >= 4 && std::wstring_view(wide_argv[1]) == L"--child") {
    const std::wstring mode = wide_argv[2];
    const std::filesystem::path dir = wide_argv[3];
    LocalFree(wide_argv);
    return RunChildMode(mode, dir);
  }
  if (wide_argv != nullptr) {
    LocalFree(wide_argv);
  }
#endif

  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "gw2_log_filter_tests";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  TestClassification();
  TestSettingValue(dir);
  TestCrashReportIndependent();
  TestSessionLifecycle(dir);
#if defined(_WIN32)
  TestHandoff(dir);
#endif
  TestWiring();

  std::filesystem::remove_all(dir, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
