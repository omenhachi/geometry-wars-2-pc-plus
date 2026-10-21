#include "log_filter.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

REXCVAR_DEFINE_BOOL(detailed_logging, true, "Geometry Wars",
                    "Keep logs for normal sessions (crash logs are always preserved)")
    .allowed({"true", "false"});

namespace gw2 {
namespace {

// Informational lines kept in Minimal mode: the first-line facts a failure report needs.
constexpr std::string_view kEssentialInfo[] = {
    "Geometry Wars: Retro Evolved 2 port ",
    "Build:",
    "Launcher Display:",
    "Launcher Gameplay:",
    "Launcher Graphics:",
    "Runtime ",
    "Visual interpolation ",
    "User data:",
    "Player settings:",
    "Game files:",
    "Graphics card:",
    "Internal render resolution:",
    "Aspect ratio:",
    "Guest back buffer:",
    "Multisampling:",
    "DXGI adapter:",
    "Vulkan device:",
    "Vulkan instance API version",
    "SDL video driver:",
    "Windows crash reporting enabled for current log.",
    "Execution complete",
    "Window closing",
    "Title terminated",
    "Restarted in a new window",
};

std::atomic<bool> g_minimal{false};

// Drops what Minimal mode leaves out while the mode is on. The target does the locking.
class MinimalLogSink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
 public:
  explicit MinimalLogSink(spdlog::sink_ptr target) : target_(std::move(target)) {}

  const spdlog::sink_ptr& target() const { return target_; }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    const std::string_view category(msg.logger_name.data(), msg.logger_name.size());
    const std::string_view message(msg.payload.data(), msg.payload.size());
    if (!g_minimal.load(std::memory_order_relaxed) || ShouldLogInMinimalMode(msg.level, category, message)) {
      target_->log(msg);
    }
  }

  void flush_() override { target_->flush(); }
  void set_pattern_(const std::string& pattern) override { target_->set_pattern(pattern); }
  void set_formatter_(std::unique_ptr<spdlog::formatter> formatter) override {
    target_->set_formatter(std::move(formatter));
  }

 private:
  spdlog::sink_ptr target_;
};

// The SDK logs bare "Execution complete" when the guest thread ends; the port writes the final line with uptime.
class ExitMessageSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit ExitMessageSink(spdlog::sink_ptr target) : target_(std::move(target)) {}

  const spdlog::sink_ptr& target() const { return target_; }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    const std::string_view category(msg.logger_name.data(), msg.logger_name.size());
    const std::string_view message(msg.payload.data(), msg.payload.size());
    if (category == "core" && message == "Execution complete") {
      return;
    }
    target_->log(msg);
  }

  void flush_() override { target_->flush(); }
  void set_pattern_(const std::string& pattern) override { target_->set_pattern(pattern); }
  void set_formatter_(std::unique_ptr<spdlog::formatter> formatter) override {
    target_->set_formatter(std::move(formatter));
  }

 private:
  spdlog::sink_ptr target_;
};

bool g_installed = false;

// The GPU plugin registers its category on first use; do it here so its logger takes the filter.
void RegisterBuiltInCategories() {
  (void)rex::log::core();
  (void)rex::log::cpu();
  (void)rex::log::apu();
  (void)rex::log::gpu();
  (void)rex::log::krnl();
  (void)rex::log::sys();
  (void)rex::log::fs();
}

spdlog::sink_ptr FindFileSink() {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (const spdlog::sink_ptr& sink : entry.logger->sinks()) {
      const spdlog::sink_ptr file = UnwrapLogSink(sink);
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(file.get()) != nullptr) {
        return file;
      }
    }
  }
  if (spdlog::logger* core = rex::GetLoggerRaw(rex::log::core()); core != nullptr) {
    for (const spdlog::sink_ptr& sink : core->sinks()) {
      const spdlog::sink_ptr file = UnwrapLogSink(sink);
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(file.get()) != nullptr) {
        return file;
      }
    }
  }
  return {};
}

void ReplaceFileChain(const spdlog::sink_ptr& file, const spdlog::sink_ptr& top) {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (spdlog::sink_ptr& sink : entry.logger->sinks()) {
      if (UnwrapLogSink(sink) == file) {
        sink = top;
      }
    }
  }
}

// Logger sink lists are read without a lock by every logging thread, so they are rewritten only here and in
// ReleaseMinimalLogFilter, when no other thread logs. Later mode changes only flip g_minimal.
void InstallFileFilter() {
  RegisterBuiltInCategories();
  const spdlog::sink_ptr file = FindFileSink();
  if (!file) {
    return;
  }

  auto exit_filter = std::make_shared<ExitMessageSink>(file);
  exit_filter->set_level(file->level());
  auto minimal = std::make_shared<MinimalLogSink>(exit_filter);
  minimal->set_level(file->level());
  ReplaceFileChain(file, minimal);
  g_installed = true;
}

}  // namespace

spdlog::sink_ptr UnwrapLogSink(const spdlog::sink_ptr& sink) {
  if (const auto* filter = dynamic_cast<MinimalLogSink*>(sink.get()); filter != nullptr) {
    return UnwrapLogSink(filter->target());
  }
  if (const auto* filter = dynamic_cast<ExitMessageSink*>(sink.get()); filter != nullptr) {
    return UnwrapLogSink(filter->target());
  }
  return sink;
}

bool ShouldLogInMinimalMode(spdlog::level::level_enum level, std::string_view category,
                            std::string_view message) {
  if (level >= spdlog::level::warn) {
    return true;
  }
  if (level < spdlog::level::info) {
    return false;
  }
  if (category == "gpu" && message.starts_with("DXGI adapter:")) {
    return true;
  }
  for (const std::string_view prefix : kEssentialInfo) {
    if (message.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

bool DetailedLoggingEnabled() {
  return rex::cvar::GetFlagByName("detailed_logging") != "false";
}

void ApplyDetailedLogging() {
  if (!g_installed) {
    InstallFileFilter();
  }
  g_minimal.store(!DetailedLoggingEnabled(), std::memory_order_relaxed);
}

bool MinimalLogFilterInstalled() {
  return g_installed && g_minimal.load(std::memory_order_relaxed);
}

void ReleaseMinimalLogFilter() {
  const spdlog::sink_ptr file = FindFileSink();
  if (file) {
    ReplaceFileChain(file, file);
  }
  g_installed = false;
}

}  // namespace gw2
