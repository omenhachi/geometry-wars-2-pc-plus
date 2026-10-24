// Benchmark (--gw2_benchmark=<replay file>): plays the replay as soon as the menus are up and times every frame while
// it plays, then writes the results to <user data>/benchmark.txt and the log. A replay plays the same frames on every
// run, so two settings can be compared fairly, on any PC, with nothing to press.
//
// The launcher's Run benchmark plays the replay that ships in assets/benchmark for a minute. Its results show over the
// game in one plain line, already copied, with what they were measured on (the graphics card, the render path and the
// resolution), so a result shared on the Discord can be compared with anyone else's.

#include "benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>

#include "demo.h"
#include "graphics_adapters.h"
#include "guest_memory.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"

REXCVAR_DEFINE_STRING(gw2_benchmark, "", "Geometry Wars",
                      "A replay file to play as soon as the menus are up, timing every frame (results in benchmark.txt)");
REXCVAR_DEFINE_INT32(gw2_benchmark_seconds, 0, "Geometry Wars", "Stop timing the benchmark after this many seconds (0: the whole replay)");

namespace gw2::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kStartState = 0x821993F0;  // 0 or 3 in the menus (demo.cpp)
// The first second of the replay is left out: its round is still being set up.
constexpr double kSkipSeconds = 1.0;
constexpr uint32_t kIntelVendorId = 0x8086;
constexpr double kShownSeconds = 20.0;  // after the replay, the results stay this long

enum class Phase { kWaiting, kRequested, kTiming, kDone };

std::mutex g_mutex;
Phase g_phase = Phase::kWaiting;
std::filesystem::path g_results;
Clock::time_point g_started;
Clock::time_point g_last;
std::vector<double> g_frames;  // milliseconds
std::string g_headline;        // "132 fps average, 67 at its slowest", once the benchmark has run
std::string g_details;         // what it ran on
Clock::time_point g_reported;

double Percentile(std::vector<double> sorted, double share) {
  if (sorted.empty()) return 0;
  std::sort(sorted.begin(), sorted.end());
  return sorted[std::min(sorted.size() - 1, size_t(share * double(sorted.size())))];
}

// The render path that drew: the setting, or for Automatic what ReXGlue takes (the rasterizer-ordered path on Intel).
std::string RenderPathText(const GraphicsAdapterDetails& gpu) {
  const std::string path = rex::cvar::GetFlagByName(kD3D12RenderTargetPathKey);
  if (path == "rtv") return "Fast render path";
  if (path == "rov") return "Compatible render path";
  return gpu.vendor_id == kIntelVendorId ? "Automatic render path (Compatible)" : "Automatic render path (Fast)";
}

void Report(const std::string& replay) {
  double total = 0;
  for (const double ms : g_frames) total += ms;
  const double average_ms = g_frames.empty() ? 0 : total / double(g_frames.size());
  const double slow_1 = Percentile(g_frames, 0.99);
  const double slow_01 = Percentile(g_frames, 0.999);
  const double average_fps = average_ms > 0 ? 1000.0 / average_ms : 0;
  const double low_fps = slow_1 > 0 ? 1000.0 / slow_1 : 0;
  const GraphicsAdapterDetails gpu =
      EffectiveGraphicsAdapter(std::atoi(rex::cvar::GetFlagByName(kD3D12AdapterKey).c_str()));
  // The line font has no %: the big line says "at its slowest", the copied details give the 1% low.
  g_headline = std::format("{:.0f} fps average, {:.0f} at its slowest", average_fps, low_fps);
  g_details = std::format("{:.1f} fps average, {:.1f} fps 1% low | GW2 PC Plus v{} | {} | {} | render {}x, internal {}% | "
                          "frame rate limit {} | {:.0f} s",
                          average_fps, low_fps, GW2_PORT_VERSION, gpu.known ? gpu.name : std::string("unknown graphics"),
                          RenderPathText(gpu), rex::cvar::GetFlagByName(kRenderScaleKey),
                          rex::cvar::GetFlagByName(kInternalRenderScaleKey),
                          rex::cvar::GetFlagByName(kFrameRateKey) == "0" ? std::string("Unlimited")
                                                                         : rex::cvar::GetFlagByName(kFrameRateKey),
                          total / 1000.0);
  g_reported = Clock::now();
  const std::string text = std::format(
      "Benchmark: {}\nframes {}  seconds {:.1f}\naverage {:.1f} fps ({:.2f} ms)\n1% low {:.1f} fps ({:.2f} ms)\n"
      "0.1% low {:.1f} fps ({:.2f} ms)\n{}\n",
      replay, g_frames.size(), total / 1000.0, average_fps, average_ms, low_fps, slow_1,
      slow_01 > 0 ? 1000.0 / slow_01 : 0, slow_01, g_details);
  std::ofstream out(g_results, std::ios::trunc);
  out << text;
  REXLOG_INFO("{}", text);
}

class Overlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Overlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::string headline, details;
    {
      std::lock_guard lock(g_mutex);
      if (g_headline.empty()) return;
      const double since = std::chrono::duration<double>(Clock::now() - g_reported).count();
      if (!demo::Watching() && since > kShownSeconds) return;
      headline = g_headline;
      details = g_details;
    }
    if (!copied_) {
      ImGui::SetClipboardText(details.c_str());
      copied_ = true;
    }
    using namespace gw2::neon;
    const float u = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f) * 9.0f / 16.0f / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float title = u * 2.4f, big = u * 4.0f, line_height = ImGui::GetFontSize();
    const float width = std::max({line_font::Width(headline, big), ImGui::CalcTextSize(details.c_str()).x,
                                  line_font::Width("Benchmark", title)}) + u * 6.0f;
    const ImVec2 p0((io.DisplaySize.x - width) * 0.5f, u * 8.0f);
    const ImVec2 p1(p0.x + width, p0.y + title + big + line_height * 2.6f + u * 7.0f);
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 0.92f), u);
    draw->AddRect(p0, p1, Theme(0.0, 0.9f), u, 0, std::max(1.5f, u * 0.25f));
    float y = p0.y + u * 2.0f;
    line_font::Draw(draw, ImVec2(p0.x + u * 3.0f, y), title, "Benchmark", Theme(), std::max(1.0f, title * 0.07f));
    y += title + u * 1.5f;
    line_font::Draw(draw, ImVec2(p0.x + u * 3.0f, y), big, headline, Rgba(1, 1, 1, 0.95f), std::max(1.2f, big * 0.07f));
    y += big + u * 1.5f;
    draw->AddText(ImVec2(p0.x + u * 3.0f, y), Rgba(0.8f, 0.85f, 0.95f, 0.95f), details.c_str());
    y += line_height * 1.3f;
    draw->AddText(ImVec2(p0.x + u * 3.0f, y), Theme(0.0, 0.95f),
                  "Copied. Paste it on the Discord to compare. Start or Enter stops the replay.");
  }

 private:
  bool copied_ = false;
};

}  // namespace

void SetResultsFile(std::filesystem::path file) {
  std::lock_guard lock(g_mutex);
  g_results = std::move(file);
}

std::filesystem::path BundledReplay() {
  const std::filesystem::path replay =
      rex::filesystem::GetExecutableFolder() / "assets" / "benchmark" / "benchmark.gw2demo";
  std::error_code error;
  return std::filesystem::is_regular_file(replay, error) ? replay : std::filesystem::path();
}

void Request() {
  const std::filesystem::path replay = BundledReplay();
  if (replay.empty()) return;
  rex::cvar::SetFlagByName("gw2_benchmark", replay.string());
  rex::cvar::SetFlagByName("gw2_benchmark_seconds", std::to_string(kLauncherSeconds));
  REXLOG_INFO("Benchmark: requested from the launcher");
}

std::wstring RelaunchArgs() {
  const std::string replay = REXCVAR_GET(gw2_benchmark);
  if (replay.empty()) return {};
  return L" --gw2_benchmark=\"" + std::filesystem::path(replay).wstring() + L"\" --gw2_benchmark_seconds=" +
         std::to_wstring(REXCVAR_GET(gw2_benchmark_seconds));
}

void CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  new Overlay(drawer);  // retained by the drawer for the session
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
