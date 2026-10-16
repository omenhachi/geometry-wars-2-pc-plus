#include "window_host.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "frame_limiter.h"
#include "launcher_cvars.h"
#include "launcher_settings.h"

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
std::function<bool()> g_gameplay_active;
// UI thread only.
CursorHider g_cursor;

WindowSize RenderResolution() {
  const int scale = std::max(1, std::atoi(rex::cvar::GetFlagByName(kRenderScaleKey).c_str()));
  return RenderSize(ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio)), scale);
}

std::string ComposeTitle(std::optional<double> fps) {
  return WindowTitle(REXCVAR_GET(gw2_show_render_resolution), RenderResolution(), REXCVAR_GET(gw2_show_fps), fps);
}

// Runs on the UI thread. The guest thread only queues the sampled rate.
void ShowFrameRate(double fps) {
  std::lock_guard lock(g_mutex);
  if (!g_window) {
    return;
  }
  std::string title = ComposeTitle(fps);
  if (title != g_title) {
    g_title = std::move(title);
    g_window->SetTitle(g_title);
  }
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

rex::ui::Window* CurrentWindow() {
  std::lock_guard lock(g_mutex);
  return g_window;
}

void ApplyCursor(rex::ui::Window* window, bool gameplay) {
  if (const auto visible = g_cursor.Update(gameplay, window->IsFullscreen(), window->HasFocus(),
                                           CursorHider::Clock::now())) {
    window->SetCursorVisibility(*visible ? rex::ui::Window::CursorVisibility::kVisible
                                         : rex::ui::Window::CursorVisibility::kHidden);
  }
}

void UpdateCursor() {
  std::function<bool()> gameplay_active;
  rex::ui::Window* window = nullptr;
  {
    std::lock_guard lock(g_mutex);
    window = g_window;
    gameplay_active = g_gameplay_active;
  }
  if (!window) {
    return;
  }
  ApplyCursor(window, gameplay_active && gameplay_active());
}

void RestoreCursor() {
  if (rex::ui::Window* window = CurrentWindow()) {
    ApplyCursor(window, false);
  }
}

class WindowHost final : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
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
    UpdateCursor();
    std::lock_guard lock(g_mutex);
    if (g_window) {
      g_window->SetTitle(g_title);
    }
  }

  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (e.target() != CurrentWindow()) {
      return;
    }
    g_cursor.MouseMoved(CursorHider::Clock::now());
    UpdateCursor();
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }
  void OnClosing(rex::ui::UIEvent&) override { RestoreCursor(); }
};

WindowHost g_window_host;

// Dialogs are drawn on the UI thread every frame, which gives the idle timer a periodic check.
class CursorTick final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorTick(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO&) override { UpdateCursor(); }
};

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<void()> toggle_window_mode, std::function<bool()> gameplay_active) {
  {
    std::lock_guard lock(g_mutex);
    g_window = window;
    g_app_context = &app_context;
    g_toggle_window_mode = std::move(toggle_window_mode);
    g_gameplay_active = std::move(gameplay_active);
    g_title = ComposeTitle(std::nullopt);
    window->SetTitle(g_title);
  }
  window->AddInputListener(&g_window_host, kWindowHostZOrder);
  window->AddListener(&g_window_host);
}

void CreateCursorTick(rex::ui::ImGuiDrawer* drawer) {
  new CursorTick(drawer);  // retained by the drawer for the session
}

// The guest can end without the window closing, so the listeners stay.
// The cursor is restored, then only the pointers are cleared.
void DetachWindowHost() {
  RestoreCursor();
  std::lock_guard lock(g_mutex);
  g_window = nullptr;
  g_app_context = nullptr;
  g_toggle_window_mode = nullptr;
  g_gameplay_active = nullptr;
}

GuestFrameStats GetGuestFrameStats() {
  std::lock_guard lock(g_mutex);
  return g_stats;
}

}  // namespace gw2

namespace {

thread_local bool t_repeat_present = false;

}  // namespace

// Called only by the D3D thread that presents the last frame again when the game has not swapped for 30 ms, right
// before that swap. Such a swap is not a game frame.
REX_EXTERN(__imp__sub_820A25D8);
REX_HOOK_RAW(sub_820A25D8) {
  t_repeat_present = true;
  __imp__sub_820A25D8(ctx, base);
}

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  const bool repeat = std::exchange(t_repeat_present, false);
  if (!repeat) {
    gw2::PaceGuestFrame();
  }
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  if (repeat) {
    return;
  }
  gw2::SyncGameDelta(base, gw2::LimiterClock::now());
  gw2::CountGuestFrame();
}
