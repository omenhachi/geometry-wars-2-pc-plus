#include "window_host.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
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

#include "frame_limiter.h"
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

// Observes the SDL gamepads in the host's own SDL instance without polling the game's input system.
class GamepadWatcher {
 public:
  ~GamepadWatcher() { Stop(); }

  void Start() {
    if (started_ || !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      return;
    }
    started_ = true;
    SDL_UpdateJoysticks();
    RefreshPads();
  }

  void Stop() {
    for (Pad& pad : pads_) {
      SDL_CloseGamepad(pad.handle);
    }
    pads_.clear();
    if (started_) {
      SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
      started_ = false;
    }
  }

  bool Poll() {
    if (!started_) {
      return false;
    }
    SDL_UpdateJoysticks();
    RefreshPads();
    bool activity = false;
    for (Pad& pad : pads_) {
      const ControllerActivity current = Read(pad.handle);
      activity = current.HasNewInput(pad.previous) || activity;
      pad.previous = current;
    }
    return activity;
  }

 private:
  struct Pad {
    SDL_Gamepad* handle = nullptr;
    ControllerActivity previous;
  };

  static ControllerActivity Read(SDL_Gamepad* pad) {
    static_assert(SDL_GAMEPAD_BUTTON_COUNT <= 32);
    ControllerActivity activity;
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
      if (!SDL_GetGamepadButton(pad, SDL_GamepadButton(button))) {
        continue;
      }
      if (button >= SDL_GAMEPAD_BUTTON_DPAD_UP && button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
        activity.dpad |= 1u << (button - SDL_GAMEPAD_BUTTON_DPAD_UP);
      } else {
        activity.buttons |= 1u << button;
      }
    }
    activity.left_trigger =
        ControllerTriggerPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    activity.right_trigger =
        ControllerTriggerPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    activity.left_stick = ControllerStickMoved(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX),
                                               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    activity.right_stick = ControllerStickMoved(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX),
                                                SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
    return activity;
  }

  void RefreshPads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) {
      return;
    }
    std::vector<SDL_JoystickID> present(ids, ids + count);
    SDL_free(ids);

    pads_.erase(std::remove_if(pads_.begin(), pads_.end(),
                               [&](const Pad& pad) {
                                 const bool connected =
                                     std::find(present.begin(), present.end(),
                                               SDL_GetGamepadID(pad.handle)) != present.end();
                                 if (!connected) {
                                   SDL_CloseGamepad(pad.handle);
                                 }
                                 return !connected;
                               }),
                pads_.end());

    for (SDL_JoystickID id : present) {
      const bool tracked = std::any_of(pads_.begin(), pads_.end(), [&](const Pad& pad) {
        return SDL_GetGamepadID(pad.handle) == id;
      });
      if (!tracked) {
        if (SDL_Gamepad* handle = SDL_OpenGamepad(id)) {
          pads_.push_back({handle, Read(handle)});
        }
      }
    }
  }

  std::vector<Pad> pads_;
  bool started_ = false;
};

std::mutex g_mutex;
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
GamepadWatcher g_gamepads;
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
  void OnClosing(rex::ui::UIEvent&) override { RestoreCursor(); }
};

WindowHost g_window_host;

// Dialogs are drawn on the UI thread every frame, which gives controller input a periodic check.
class CursorTick final : public rex::ui::ImGuiDialog {
 public:
  explicit CursorTick(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    g_pointer_over_ui = io.WantCaptureMouse;
    if (g_gamepads.Poll()) {
      g_cursor.ControllerInput();
    }
    UpdateCursor();
  }
};

}  // namespace

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
  g_gamepads.Start();
  UpdateCursor();
}

void CreateCursorTick(rex::ui::ImGuiDrawer* drawer) {
  new CursorTick(drawer);  // retained by the drawer for the session
}

// The guest can end without the window closing, so the listeners stay.
// The cursor is restored, then only the pointers are cleared.
void DetachWindowHost() {
  RestoreCursor();
  g_gamepads.Stop();
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
