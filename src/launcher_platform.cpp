#include "launcher_platform.h"

#include <algorithm>
#include <cstdlib>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "active_log.h"
#include "crash_report.h"

namespace gw2 {

MonitorInfo MonitorForSetting(int monitor) {
  const std::vector<MonitorInfo> monitors = ListMonitors();
  if (monitors.empty()) {
    return {};
  }
  if (monitor >= 1 && monitor <= int(monitors.size())) {
    return monitors[size_t(monitor - 1)];
  }
  const auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return primary != monitors.end() ? *primary : monitors.front();
}

std::filesystem::path PortFolder() {
#if !defined(_WIN32)
  // An AppImage runs from a read-only mount that is gone once it exits.
  if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
    return std::filesystem::path(appimage).parent_path();
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}

void InstallCrashReports() {
#if defined(_WIN32)
  const std::filesystem::path file = ActiveLogFile();
  if (file.empty()) {
    REXLOG_WARN("Windows crash reporting disabled: the current log file could not be resolved");
    return;
  }
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
  InstallCrashReporting(Utf8(ec ? file : absolute).c_str());
  REXLOG_INFO("Windows crash reporting enabled for current log.");
#endif
}

}  // namespace gw2
