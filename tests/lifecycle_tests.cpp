// Window-close lifecycle and logging policy: a host window close hard-exits after the session log is finalized,
// while a normal guest exit keeps the graceful teardown in OnShutdown. The decision is read from the app source (the
// window calls need the running game). Run: gw2_lifecycle_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestHostWindowCloseHardExits() {
  std::printf("host window close finalizes the log and hard-exits without runtime teardown\n");
  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
  const std::string body = FunctionBody(app, "void OnClosing(");
  CHECK(Has(body, "gw2::DiscardSessionLogBeforeHardExit();"));
  CHECK(!Has(body, "FinalizeSessionLog"));
  CHECK(Has(body, "rex::FlushLogging();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "StopTitleThreads"));
  CHECK(!Has(body, "Shutdown("));
  CHECK(!Has(body, "DetachWindowHost"));
}

void TestNormalExitKeepsTeardown() {
  std::printf("normal guest exit keeps the graceful teardown\n");
  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  const std::string body = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(body, "input->Shutdown();"));
  CHECK(Has(body, "DetachWindowHost();"));
}

void TestLoggingWiring() {
  std::printf("logging is initialized once, with the launcher and relaunch sharing the log\n");
  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "gw2::TakeInheritedSessionLog()"));
  CHECK(Has(app, "SetFlagFromCommandLine(\"log_file\""));
  CHECK(Has(app, "gw2::InstallCrashReports();"));
  CHECK(Has(app, "gw2::ApplyDetailedLogging();"));
  CHECK(Has(app, "gw2::InstallSessionLogCleanup();"));
  CHECK(Has(app, "void OnDpiScaleChanged(float scale) override"));
  CHECK(!Has(app, "rex::InitLogging("));
  CHECK(!Has(app, "ShutdownLogging("));
  CHECK(!Has(app, "crash-"));
  CHECK(!Has(app, ".dmp"));

  const std::string platform = ReadSource("launcher_platform.cpp");
  CHECK(Has(platform, "InstallCrashReporting("));
  CHECK(Has(platform, "Windows crash reporting enabled for current log."));

  const std::string win32 = ReadSource("launcher_platform_win32.cpp");
  const std::string linux = ReadSource("launcher_platform_linux.cpp");
  for (const std::string& source : {win32, linux}) {
    CHECK(Has(source, "BeginSessionLogHandoff();"));
    CHECK(Has(source, "CancelSessionLogHandoff();"));
  }

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Heading(\"Diagnostics\")"));
  CHECK(Has(launcher, "ui.Toggle(\"Detailed logging\""));
  CHECK(Has(launcher, "ApplyDetailedLogging();"));
  CHECK(Has(launcher, "OpenLogsFolder();"));
}

void TestCrashReportIsSingleLog() {
  std::printf("a crash appends to the current log instead of writing a second file\n");
  const std::string crash = ReadSource("crash_report.cpp");
  CHECK(Has(crash, "CRASH REPORT"));
  CHECK(Has(crash, "FILE_APPEND_DATA"));
  CHECK(Has(crash, "CreateFileW(g_log_path"));
  CHECK(!Has(crash, "MiniDump"));
  CHECK(!Has(crash, "crash-"));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "ShutdownLogging()"));
  CHECK(Has(session, "GW2_SESSION_LOG"));
  CHECK(Has(session, "BeginSessionLogHandoff"));
  CHECK(Has(session, "TakeInheritedSessionLog"));
  CHECK(!Has(session, "remove_all"));
}

}  // namespace

int main() {
  TestHostWindowCloseHardExits();
  TestNormalExitKeepsTeardown();
  TestLoggingWiring();
  TestCrashReportIsSingleLog();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
