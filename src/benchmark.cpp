// Benchmark (--gw2_benchmark=<replay file>): plays the replay as soon as the menus are up and times every frame while
// it plays, then writes the results to <user data>/benchmark.txt and the log. A replay plays the same frames on every
// run, so two settings can be compared fairly, on any PC, with nothing to press.

#include "benchmark.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "demo.h"
#include "guest_memory.h"

REXCVAR_DEFINE_STRING(gw2_benchmark, "", "Geometry Wars",
                      "A replay file to play as soon as the menus are up, timing every frame (results in benchmark.txt)");
REXCVAR_DEFINE_INT32(gw2_benchmark_seconds, 0, "Geometry Wars", "Stop timing the benchmark after this many seconds (0: the whole replay)");

namespace gw2::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kStartState = 0x821993F0;  // 0 or 3 in the menus (demo.cpp)
// The first second of the replay is left out: its round is still being set up.
constexpr double kSkipSeconds = 1.0;

enum class Phase { kWaiting, kRequested, kTiming, kDone };

std::mutex g_mutex;
Phase g_phase = Phase::kWaiting;
std::filesystem::path g_results;
Clock::time_point g_started;
Clock::time_point g_last;
std::vector<double> g_frames;  // milliseconds

double Percentile(std::vector<double> sorted, double share) {
  if (sorted.empty()) return 0;
  std::sort(sorted.begin(), sorted.end());
  return sorted[std::min(sorted.size() - 1, size_t(share * double(sorted.size())))];
}

void Report(const std::string& replay) {
  double total = 0;
  for (const double ms : g_frames) total += ms;
  const double average_ms = g_frames.empty() ? 0 : total / double(g_frames.size());
  const double slow_1 = Percentile(g_frames, 0.99);
  const double slow_01 = Percentile(g_frames, 0.999);
  const std::string text = std::format(
      "Benchmark: {}\nframes {}  seconds {:.1f}\naverage {:.1f} fps ({:.2f} ms)\n1% low {:.1f} fps ({:.2f} ms)\n"
      "0.1% low {:.1f} fps ({:.2f} ms)\n",
      replay, g_frames.size(), total / 1000.0, average_ms > 0 ? 1000.0 / average_ms : 0, average_ms,
      slow_1 > 0 ? 1000.0 / slow_1 : 0, slow_1, slow_01 > 0 ? 1000.0 / slow_01 : 0, slow_01);
  std::ofstream out(g_results, std::ios::trunc);
  out << text;
  REXLOG_INFO("{}", text);
}

}  // namespace

void SetResultsFile(std::filesystem::path file) {
  std::lock_guard lock(g_mutex);
  g_results = std::move(file);
}

void EachFrame(const uint8_t* base) {
  const std::string replay = REXCVAR_GET(gw2_benchmark);
  if (replay.empty()) return;
  std::lock_guard lock(g_mutex);
  const Clock::time_point now = Clock::now();
  switch (g_phase) {
    case Phase::kWaiting: {
      const uint32_t start = LoadBE32(base, kStartState);
      if (start != 0 && start != 3) return;
      const demo::WatchResult result = demo::Watch(replay);
      if (result != demo::WatchResult::kStarted) {
        REXLOG_WARN("Benchmark: {} can't be played", replay);
        g_phase = Phase::kDone;
        return;
      }
      g_phase = Phase::kRequested;
      return;
    }
    case Phase::kRequested:
      if (!demo::Watching()) return;
      g_phase = Phase::kTiming;
      g_started = now;
      g_last = now;
      return;
    case Phase::kTiming: {
      // Over when the replay reaches its end (it stops a tick or two short), or is no longer playing. Its Game Over
      // waits for a press that never comes, so the menus are not waited for.
      const demo::ReplayStatus status = demo::Status();
      const int limit = REXCVAR_GET(gw2_benchmark_seconds);
      const bool long_enough = limit > 0 && std::chrono::duration<double>(now - g_started).count() >= kSkipSeconds + limit;
      if (!status.watching || status.stopped || status.tick + 2 >= status.ticks || long_enough) {
        Report(replay);
        g_phase = Phase::kDone;
        return;
      }
      const double ms = std::chrono::duration<double, std::milli>(now - g_last).count();
      g_last = now;
      if (std::chrono::duration<double>(now - g_started).count() > kSkipSeconds) g_frames.push_back(ms);
      return;
    }
    case Phase::kDone:
      return;
  }
}

}  // namespace gw2::benchmark
