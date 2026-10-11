#include "window_host.h"

#include <cstddef>
#include <mutex>
#include <utility>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "frame_limiter.h"
#include "launcher_cvars.h"

// The SDK's VdSwap body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xboxkrnl {

void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2, mapped_void unk3, mapped_void unk4,
                  mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr, mapped_u32 color_space_ptr,
                  mapped_u32 width, mapped_u32 height);

}  // namespace rex::kernel::xboxkrnl

namespace gw2 {
namespace {

// Above the app's own input listener (z-order 0), so F11 never reaches the game.
constexpr std::size_t kWindowHostZOrder = 1000;

std::mutex g_mutex;
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::function<void()> g_toggle_window_mode;
FrameRateSampler g_sampler;
GuestFrameStats g_stats;
std::string g_title = kWindowTitle;

// Runs on the UI thread. The guest thread only queues the sampled rate.
void ShowFrameRate(double fps) {
  std::lock_guard lock(g_mutex);
  if (!g_window) {
    return;
  }
  g_title = WindowTitle(REXCVAR_GET(gw2_show_fps), fps);
  g_window->SetTitle(g_title);
}

void CountGuestFrame() {
  std::lock_guard lock(g_mutex);
  if (!g_window) {
    return;
  }
  if (const auto stats = g_sampler.Count(FrameRateSampler::Clock::now())) {
    g_stats = *stats;
    g_app_context->CallInUIThread([fps = stats->fps] { ShowFrameRate(fps); });
  }
}

class WindowHost final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF11) {
      return;
    }
    rex::ui::Window* window = nullptr;
    std::function<void()> toggle;
    {
      std::lock_guard lock(g_mutex);
      window = g_window;
      toggle = g_toggle_window_mode;
    }
    if (e.target() != window) {
      return;
    }
    e.set_handled(true);
    if (e.prev_state() || !toggle) {
      return;
    }
    toggle();
    std::lock_guard lock(g_mutex);
    if (g_window) {
      g_window->SetTitle(g_title);
    }
  }
};

WindowHost g_window_host;

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<void()> toggle_window_mode) {
  {
    std::lock_guard lock(g_mutex);
    g_window = window;
    g_app_context = &app_context;
    g_toggle_window_mode = std::move(toggle_window_mode);
    g_title = kWindowTitle;
    window->SetTitle(g_title);
  }
  window->AddInputListener(&g_window_host, kWindowHostZOrder);
}

// The guest can end without the window closing, so the listener stays and only the pointers are cleared.
void DetachWindowHost() {
  std::lock_guard lock(g_mutex);
  g_window = nullptr;
  g_app_context = nullptr;
  g_toggle_window_mode = nullptr;
}

GuestFrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_mutex);
  return g_stats;
}

}  // namespace gw2

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  gw2::PaceGuestFrame();
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  gw2::SyncGameDelta(base, gw2::LimiterClock::now());
  gw2::CountGuestFrame();
}
