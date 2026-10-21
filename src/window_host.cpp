#include "window_host.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "cursor_input.h"
#include "frame_limiter.h"
#include "input_activity.h"
#include "launcher_cvars.h"
#include "launcher_settings.h"
#include "rumble.h"

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
std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::function<void()> g_toggle_window_mode;
FrameRateSampler g_sampler;
GuestFrameStats g_stats;
std::string g_title = kWindowTitle;
std::function<bool()> g_gameplay_active;
std::function<bool()> g_ui_active;
// UI thread only.
CursorHider g_cursor;
// Guest threads that poll input, serialized by the runtime's activity lock; cleared after the callback is unregistered.
std::unordered_map<uint64_t, ControllerActivityFilter> g_controller_filters;
bool g_pointer_over_ui = false;

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

void ApplyCursor(rex::ui::Window* window, bool gameplay, bool ui_active) {
  if (const auto visible = g_cursor.Update(gameplay, window->HasFocus(), ui_active)) {
    window->SetCursorVisibility(*visible ? rex::ui::Window::CursorVisibility::kVisible
                                         : rex::ui::Window::CursorVisibility::kHidden);
  }
}

void UpdateCursor() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::function<bool()> gameplay_active;
  std::function<bool()> ui_active;
  rex::ui::Window* window = nullptr;
  {
    std::lock_guard lock(g_mutex);
    window = g_window;
    gameplay_active = g_gameplay_active;
    ui_active = g_ui_active;
  }
  if (!window) {
    return;
  }
  ApplyCursor(window, gameplay_active && gameplay_active(),
              g_pointer_over_ui || (ui_active && ui_active()));
}

void RestoreCursor() {
  if (rex::ui::Window* window = CurrentWindow()) {
    ApplyCursor(window, false, false);
  }
}

void OnControllerActivity(rex::input::DeviceId device,
                          const rex::input::X_INPUT_GAMEPAD& gamepad) {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  if (!g_controller_filters[static_cast<uint64_t>(device)].Update(gamepad)) {
    return;
  }
  rex::ui::WindowedAppContext* app_context = nullptr;
  {
    std::lock_guard lock(g_mutex);
    app_context = g_app_context;
  }
  if (!app_context) {
    return;
  }
  app_context->CallInUIThread([] {
    if (!g_active.load(std::memory_order_relaxed)) {
      return;
    }
    g_cursor.ControllerInput();
    UpdateCursor();
  });
}

bool IsMeaningfulKey(const rex::ui::KeyEvent& e) {
  const rex::ui::VirtualKey key = e.virtual_key();
  return !e.prev_state() && key != rex::ui::VirtualKey::kNone &&
         key != rex::ui::VirtualKey::kPacket && key != rex::ui::VirtualKey::kProcessKey;
}

class WindowHost final : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
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
    if (IsMeaningfulKey(e)) {
      g_cursor.KeyboardInput();
      UpdateCursor();
    }
    if (e.virtual_key() != rex::ui::VirtualKey::kF11) {
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
    g_cursor.MouseMoved();
    UpdateCursor();
  }

  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (e.target() != CurrentWindow()) {
      return;
    }
    g_cursor.MouseButtonPressed();
    UpdateCursor();
  }

  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    if (e.target() != CurrentWindow()) {
      return;
    }
    g_cursor.MouseWheelMoved();
    UpdateCursor();
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { UpdateCursor(); }

  void OnClosing(rex::ui::UIEvent&) override {
    rex::input::SetInputActivityCallback(nullptr);
    RestoreCursor();
    g_active.store(false, std::memory_order_relaxed);
  }
};

WindowHost g_window_host;

// ImGui overlays that do not go through the app's ui_active callback still capture the mouse.
class CursorSync final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorSync(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_active.load(std::memory_order_relaxed)) {
      return;
    }
    g_pointer_over_ui = io.WantCaptureMouse;
    UpdateCursor();
  }
};

}  // namespace

WindowSize CurrentDrawableSize(rex::ui::Window* window) {
  if (window == nullptr) {
    return {};
  }
#if defined(_WIN32)
  if (const HWND handle = static_cast<HWND>(window->GetNativeWindowHandle()); handle != nullptr) {
    RECT rect{};
    if (GetClientRect(handle, &rect)) {
      const int width = rect.right - rect.left;
      const int height = rect.bottom - rect.top;
      if (width > 0 && height > 0) {
        return {width, height};
      }
    }
  }
#endif
  return {int(window->GetActualPhysicalWidth()), int(window->GetActualPhysicalHeight())};
}

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<void()> toggle_window_mode, std::function<bool()> gameplay_active,
                      std::function<bool()> ui_active) {
  {
    std::lock_guard lock(g_mutex);
    g_window = window;
    g_app_context = &app_context;
    g_toggle_window_mode = std::move(toggle_window_mode);
    g_gameplay_active = std::move(gameplay_active);
    g_ui_active = std::move(ui_active);
    g_title = ComposeTitle(std::nullopt);
    window->SetTitle(g_title);
  }
  window->AddInputListener(&g_window_host, kWindowHostZOrder);
  window->AddListener(&g_window_host);
  rex::input::SetInputActivityCallback(nullptr);
  g_cursor = CursorHider{};
  g_controller_filters.clear();
  g_active.store(true, std::memory_order_relaxed);
  rex::input::SetInputActivityCallback(OnControllerActivity);
  UpdateCursor();
}

void CreateCursorSync(rex::ui::ImGuiDrawer* drawer) {
  new CursorSync(drawer);  // retained by the drawer for the session
}

// The guest can end without the window closing, so the listeners stay.
// The cursor is restored, then only the pointers are cleared.
void DetachWindowHost() {
  rex::input::SetInputActivityCallback(nullptr);
  RestoreCursor();
  g_active.store(false, std::memory_order_relaxed);
  g_controller_filters.clear();
  std::lock_guard lock(g_mutex);
  g_window = nullptr;
  g_app_context = nullptr;
  g_toggle_window_mode = nullptr;
  g_gameplay_active = nullptr;
  g_ui_active = nullptr;
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
    gw2::rumble::ServiceFrame(ctx, base);
  }
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  if (repeat) {
    return;
  }
  gw2::SyncGameDelta(base, gw2::LimiterClock::now());
  gw2::CountGuestFrame();
}
