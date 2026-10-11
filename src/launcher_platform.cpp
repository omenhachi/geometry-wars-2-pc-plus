#include "launcher_platform.h"

#include <algorithm>
#include <cstdlib>

#include <rex/filesystem.h>

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

}  // namespace gw2
