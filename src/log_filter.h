#pragma once

#include <string_view>

#include <spdlog/spdlog.h>

namespace gw2 {

// Minimal mode keeps warnings, errors and the startup diagnostics; it drops the ordinary runtime chatter.
bool ShouldLogInMinimalMode(spdlog::level::level_enum level, std::string_view category,
                            std::string_view message);

// The `detailed_logging` setting: true unless it is explicitly off.
bool DetailedLoggingEnabled();

// Makes the file log follow the setting. The first call wraps the file sink in the filter and must run before
// other threads log; later calls only switch the filter and are safe at any time.
void ApplyDetailedLogging();

// Whether the filter is installed and Minimal mode is on.
bool MinimalLogFilterInstalled();

// Unwraps the file sink so ShutdownLogging can close its file. Only on the way out, once no other thread logs.
void ReleaseMinimalLogFilter();

// The sink a logger actually writes to, looking through the Minimal filter.
spdlog::sink_ptr UnwrapLogSink(const spdlog::sink_ptr& sink);

}  // namespace gw2
