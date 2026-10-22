#include "aim_camera.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <bit>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/types.h>
#include <rex/ui/imgui_dialog.h>

#include "aspect_ratio.h"
#include "cheat_menu.h"
#include "guest_memory.h"
#include "launcher_cvars.h"
#include "launcher_settings.h"
#include "player_records.h"
#include "window_host.h"

REXCVAR_DEFINE_BOOL(gw2_aim_debug, false, "Geometry Wars",
                    "Draw a dot per camera where it puts the local ship, and log the positions");
REXCVAR_DEFINE_BOOL(gw2_crosshair_motion, true, "Geometry Wars",
                    "Rotate the mouse-aim crosshair and draw its arms in when it appears and while firing");

// The SDK's XamInputGetState body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xam {

u32 XamInputGetState_entry(u32 user_index, u32 flags, ppc_ptr_t<rex::input::X_INPUT_STATE> input_state);

}  // namespace rex::kernel::xam

namespace gw2::aim {
namespace {

using Clock = std::chrono::steady_clock;

struct CameraState {
  Mat4 view{};
  Mat4 proj{};
  bool has_view = false;
  bool has_proj = false;
  uint64_t frames = 0;  // projections captured, to tell the cameras that run every frame
};

struct State {
  std::array<CameraState, size_t(Camera::kCount)> cameras;
  float player_x = 0.0f;
  float player_y = 0.0f;
  Clock::time_point player_seen{};
};

std::mutex g_mutex;
State g_state;

// sub_82067A20 builds the gameplay camera; the others draw the menus and the attract mode.
constexpr Camera kGameplayCamera = Camera::kB;
// Player::Update runs every frame of a round, so a longer gap means a menu, a pause or no round.
constexpr auto kRoundTimeout = std::chrono::milliseconds(250);
// What the UI thread decided for the pad, applied by the game thread while it is fresh.
constexpr auto kOverrideTimeout = std::chrono::milliseconds(100);
// A press long enough for the game to read it at 30 FPS.
constexpr auto kPulseLength = std::chrono::milliseconds(80);

constexpr uint16_t kButtonStart = 0x0010;
constexpr uint16_t kButtonB = 0x2000;

std::atomic<int64_t> g_override_time{0};  // Clock ticks of the last decision
std::atomic<bool> g_fire{false};
std::atomic<bool> g_bomb{false};
std::atomic<int32_t> g_stick{0};  // x in the low 16 bits, y in the high
std::atomic<int64_t> g_pulse_until{0};
std::atomic<uint16_t> g_pulse_buttons{0};
std::atomic<bool> g_crosshair{false};
std::atomic<int64_t> g_mouse_fired{0};  // Clock ticks of the last mouse-aimed shot

int64_t Ticks(Clock::time_point t) { return t.time_since_epoch().count(); }

Mat4 LoadMatrix(const uint8_t* base, uint32_t addr) {
  Mat4 m;
  for (size_t i = 0; i < m.size(); ++i) {
    m[i] = std::bit_cast<float>(gw2::LoadBE32(base, addr + uint32_t(i) * 4));
  }
  return m;
}

}  // namespace

void CaptureProjection(const uint8_t* base, uint32_t matrix, uint32_t lr) {
  const Camera camera = CameraForReturn(lr);
  if (camera == Camera::kCount) {
    return;
  }
  const Mat4 proj = LoadMatrix(base, matrix);
  std::lock_guard lock(g_mutex);
  CameraState& state = g_state.cameras[size_t(camera)];
  state.proj = proj;
  state.has_proj = true;
  ++state.frames;
}

bool MouseAimedRecently() {
  const int64_t fired = g_mouse_fired.load(std::memory_order_relaxed);
  return fired != 0 &&
         Ticks(Clock::now()) - fired <= std::chrono::duration_cast<Clock::duration>(std::chrono::minutes(1)).count();
}

void ObservePlayer(float x, float y) {
  std::lock_guard lock(g_mutex);
  g_state.player_x = x;
  g_state.player_y = y;
  g_state.player_seen = Clock::now();
}

}  // namespace gw2::aim

// Player::Fire: r4 points at the aim direction, which Player::Update eases towards the right stick, and the bullets
// leave at its atan2. With the mouse the shots go straight at the cursor instead of sweeping round to it. The game's
// own direction is overwritten, so it eases on from there once the button is let go.
REX_EXTERN(__imp__sub_8204EFD0);
REX_HOOK_RAW(sub_8204EFD0) {
  using namespace gw2::aim;
  const uint32_t player = ctx.r3.u32;
  const uint32_t direction = ctx.r4.u32;
  const int64_t now = Ticks(Clock::now());
  if (direction != 0 && g_fire.load(std::memory_order_relaxed) &&
      now - g_override_time.load(std::memory_order_relaxed) <=
          std::chrono::duration_cast<Clock::duration>(kOverrideTimeout).count() &&
      gw2::players::IsPlayer(base, player) && gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset) == 0) {
    // The stick is the arena direction with y flipped (AimStick).
    const int32_t stick = g_stick.load(std::memory_order_relaxed);
    const float x = float(int16_t(stick & 0xFFFF)) / 32767.0f;
    const float y = -float(int16_t(uint32_t(stick) >> 16)) / 32767.0f;
    gw2::StoreBE32(base, direction, std::bit_cast<uint32_t>(x));
    gw2::StoreBE32(base, direction + 4, std::bit_cast<uint32_t>(y));
  }
  __imp__sub_8204EFD0(ctx, base);
}

// Builds a view matrix (r3) by inverting the camera's world matrix (r4).
REX_EXTERN(__imp__sub_8206A1C8);
REX_HOOK_RAW(sub_8206A1C8) {
  const uint32_t out = ctx.r3.u32;
  const gw2::aim::Camera camera = gw2::aim::CameraForReturn(uint32_t(ctx.lr));
  __imp__sub_8206A1C8(ctx, base);
  if (camera == gw2::aim::Camera::kCount) {
    return;
  }
  const gw2::aim::Mat4 view = gw2::aim::LoadMatrix(base, out);
  std::lock_guard lock(gw2::aim::g_mutex);
  gw2::aim::CameraState& state = gw2::aim::g_state.cameras[size_t(camera)];
  state.view = view;
  state.has_view = true;
}

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
// The mouse replaces the right stick while the left button is held instead of adding to it.
REX_HOOK_RAW(__imp__XamInputGetState) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t state_ptr = ctx.r5.u32;
  rex::ppc::HostToGuestFunction<rex::kernel::xam::XamInputGetState_entry>(ctx, base);
  if (ctx.r3.u32 != 0 || state_ptr == 0 || (user_index != 0 && (user_index & 0xFF) != 0xFF)) {
    return;
  }
  using namespace gw2::aim;
  const int64_t now = Ticks(Clock::now());
  // The gamepad follows the 4-byte packet number: buttons, triggers, then the four stick axes.
  const uint32_t pad = state_ptr + 4;
  if (now < g_pulse_until.load(std::memory_order_relaxed)) {
    const uint16_t buttons = gw2::LoadBE16(base, pad) | g_pulse_buttons.load(std::memory_order_relaxed);
    gw2::StoreBE<uint16_t>(base, pad, buttons);
  }
  if (now - g_override_time.load(std::memory_order_relaxed) >
      std::chrono::duration_cast<Clock::duration>(kOverrideTimeout).count()) {
    return;
  }
  if (g_bomb.load(std::memory_order_relaxed)) {
    base[pad + 3] = 255;  // right trigger
  }
  if (g_fire.load(std::memory_order_relaxed)) {
    const int32_t stick = g_stick.load(std::memory_order_relaxed);
    gw2::StoreBE<int16_t>(base, pad + 8, int16_t(stick & 0xFFFF));
    gw2::StoreBE<int16_t>(base, pad + 10, int16_t(uint32_t(stick) >> 16));
  }
}

namespace gw2 {
namespace {

using aim::Clock;

constexpr ImU32 kCameraColors[] = {IM_COL32(255, 60, 60, 255), IM_COL32(60, 255, 90, 255),
                                   IM_COL32(70, 160, 255, 255)};
constexpr const char* kCameraNames[] = {"A", "B", "C"};
// The game is a wall of neon, so the crosshair has to outshine it: arms as thick as the player's bullets, shifting
// through the spectrum, with a glow and a dark outline that keeps it readable on explosions.
constexpr ImU32 kCrosshairOutline = IM_COL32(0, 0, 0, 200);
// As shares of the canvas height: arms about as thick as a bullet, and the size of the whole crosshair.
constexpr float kCrosshairThicknessShare = 0.008f;
constexpr float kCrosshairSizeShare = 0.075f;
constexpr int kCrosshairSegments = 6;  // per arm, for the colour gradient
constexpr float kCrosshairTurnsPerSecond = 0.15f;
constexpr float kCrosshairHueCyclesPerSecond = 0.5f;
// How fast the arms ease towards their target spread, per second.
constexpr float kCrosshairEase = 14.0f;
// The crosshair stays this long after the mouse last moved, then the arrows or a controller have the aim.
constexpr auto kCrosshairLinger = std::chrono::seconds(3);
// As a share of the canvas height: about one ship radius at 720 lines.
constexpr float kDeadZoneShare = 0.025f;

// A fully saturated colour at hue h (wrapped to 0..1), mixed towards white by `white`.
ImU32 Neon(float h, float white, float alpha) {
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 1.0f, 1.0f, r, g, b);
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(r + (1.0f - r) * white, g + (1.0f - g) * white, b + (1.0f - b) * white, alpha));
}

// One pass of the four arms pointing at p, turned by angle. Each runs from `outer` in to `inner` from the centre; a
// negative inner carries the arms past the centre, so they cross. Each arm is a gradient a quarter of the spectrum
// on from the one before, starting at hue.
void DrawCrosshairArms(ImDrawList* draw, ImVec2 p, float angle, float inner, float outer, float hue, float white,
                       float alpha, float thickness, bool outline) {
  for (int i = 0; i < 4; ++i) {
    const float a = angle + float(i) * 1.5707964f;
    const float c = std::cos(a);
    const float s = std::sin(a);
    for (int k = 0; k < kCrosshairSegments; ++k) {
      const float r0 = outer + (inner - outer) * float(k) / kCrosshairSegments;
      const float r1 = outer + (inner - outer) * float(k + 1) / kCrosshairSegments;
      const ImU32 color = outline ? kCrosshairOutline : Neon(hue + float(i) * 0.25f + float(k) * 0.04f, white, alpha);
      draw->AddLine(ImVec2(p.x + c * r0, p.y + s * r0), ImVec2(p.x + c * r1, p.y + s * r1), color, thickness);
    }
  }
}

void DrawCrosshair(ImDrawList* draw, ImVec2 p, float size, float thickness, float angle, float spread, float hue) {
  // spread 1 is the resting gap, 0 the arms just meeting, below 0 crossing.
  const float inner = size * 0.3f * spread;
  const float outer = inner + size * 0.7f;
  DrawCrosshairArms(draw, p, angle, inner, outer, hue, 0.0f, 1.0f, thickness + 4.0f, true);
  DrawCrosshairArms(draw, p, angle, inner, outer, hue, 0.0f, 0.35f, thickness * 3.0f, false);
  DrawCrosshairArms(draw, p, angle, inner, outer, hue, 0.1f, 1.0f, thickness, false);
  DrawCrosshairArms(draw, p, angle, inner, outer, hue, 0.75f, 1.0f, thickness * 0.4f, false);
  draw->AddCircleFilled(p, thickness * 0.9f + 2.0f, kCrosshairOutline);
  draw->AddCircleFilled(p, thickness * 0.9f, Neon(hue + 0.5f, 0.6f, 1.0f));
}

class MouseAimOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit MouseAimOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    aim::State state;
    {
      std::lock_guard lock(aim::g_mutex);
      state = aim::g_state;
    }
    const auto now = Clock::now();
    const bool in_round = now - state.player_seen <= aim::kRoundTimeout;
    const bool ui_blocked = io.WantCaptureMouse || io.WantCaptureKeyboard || CheatMenuOpen() || !GameInputActive();
    const bool usable = !ui_blocked && WindowFocused();

    if (usable && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      aim::g_pulse_buttons.store(in_round ? aim::kButtonStart : aim::kButtonB, std::memory_order_relaxed);
      aim::g_pulse_until.store(aim::Ticks(now + aim::kPulseLength), std::memory_order_relaxed);
    }

    const double canvas_aspect = CanvasAspect(ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio)));
    const aim::Rect canvas = aim::FitCanvas(io.DisplaySize.x, io.DisplaySize.y, canvas_aspect);
    if (REXCVAR_GET(gw2_aim_debug) && in_round) {
      DrawDebug(state, canvas);
    }

    const bool keyboard = rex::cvar::GetFlagByName(kKeyboardKey) == "true";
    const bool mouse_valid = ImGui::IsMousePosValid();
    if (mouse_valid && (io.MousePos.x != last_mouse_.x || io.MousePos.y != last_mouse_.y)) {
      last_mouse_ = io.MousePos;
      mouse_moved_ = now;
    }
    const bool fire = io.MouseDown[0];
    const bool bomb = io.MouseDown[1];
    const aim::CameraState& camera = state.cameras[size_t(aim::kGameplayCamera)];
    const bool active = keyboard && in_round && usable && mouse_valid && camera.has_view && camera.has_proj;

    if (active && fire) {
      const auto target = aim::UnprojectToArena(camera.view, camera.proj,
                                                aim::WindowToNdc({io.MousePos.x, io.MousePos.y}, canvas));
      const auto ship_ndc = aim::ProjectToNdc(camera.view, camera.proj, state.player_x, state.player_y, 0.0f);
      if (target && ship_ndc) {
        // The dead zone is measured on screen, where the player sees the cursor resting on the ship.
        const aim::Vec2 ship_px = aim::NdcToWindow(*ship_ndc, canvas);
        const float dx = io.MousePos.x - ship_px.x;
        const float dy = io.MousePos.y - ship_px.y;
        if (dx * dx + dy * dy > (kDeadZoneShare * canvas.height) * (kDeadZoneShare * canvas.height)) {
          stick_ = aim::AimStick({state.player_x, state.player_y}, *target, 0.0f, stick_);
        }
      }
    }
    aim::g_stick.store(int32_t(uint16_t(stick_.x)) | (int32_t(uint16_t(stick_.y)) << 16), std::memory_order_relaxed);
    aim::g_fire.store(active && fire && (stick_.x != 0 || stick_.y != 0), std::memory_order_relaxed);
    if (active && fire) {
      aim::g_mouse_fired.store(aim::Ticks(now), std::memory_order_relaxed);
    }
    aim::g_bomb.store(active && bomb, std::memory_order_relaxed);
    aim::g_override_time.store(aim::Ticks(now), std::memory_order_relaxed);

    const bool crosshair = active && (fire || now - mouse_moved_ <= kCrosshairLinger);
    aim::g_crosshair.store(crosshair, std::memory_order_relaxed);
    if (!crosshair) {
      spread_ = kAppearSpread;  // the arms fly in again next time it shows
      return;
    }
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const float target = fire ? kFiringSpread : 1.0f;
    float angle = 0.785398f;  // still: an X
    float hue = 0.5f;         // still: cyan, the game's own grid colour shifted brighter
    if (REXCVAR_GET(gw2_crosshair_motion)) {
      angle_ = std::fmod(angle_ + dt * kCrosshairTurnsPerSecond * 6.2831855f, 6.2831855f);
      hue_ = std::fmod(hue_ + dt * kCrosshairHueCyclesPerSecond, 1.0f);
      angle = angle_;
      hue = hue_;
      spread_ += (target - spread_) * std::min(1.0f, dt * kCrosshairEase);
    } else {
      spread_ = 1.0f;
    }
    DrawCrosshair(ImGui::GetForegroundDrawList(), io.MousePos, canvas.height * kCrosshairSizeShare,
                  std::max(3.0f, canvas.height * kCrosshairThicknessShare), angle, spread_, hue);
  }

 private:
  void DrawDebug(const aim::State& state, const aim::Rect& canvas) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float size = ImGui::GetFontSize();
    const bool log = ++debug_frames_ % 120 == 0;
    for (size_t i = 0; i < state.cameras.size(); ++i) {
      const aim::CameraState& camera = state.cameras[i];
      if (!camera.has_view || !camera.has_proj) {
        continue;
      }
      const auto ndc = aim::ProjectToNdc(camera.view, camera.proj, state.player_x, state.player_y, 0.0f);
      if (log) {
        REXLOG_INFO("Aim debug: camera {} frames={} player=({:.2f}, {:.2f}) behind={} ndc=({:.3f}, {:.3f})",
                    kCameraNames[i], camera.frames, state.player_x, state.player_y, !ndc, ndc ? ndc->x : 0.0f,
                    ndc ? ndc->y : 0.0f);
      }
      if (!ndc) {
        continue;
      }
      const aim::Vec2 p = aim::NdcToWindow(*ndc, canvas);
      draw->AddCircleFilled(ImVec2(p.x, p.y), size * 0.35f, kCameraColors[i]);
      draw->AddText(ImVec2(p.x + size * 0.5f, p.y - size * 0.5f), kCameraColors[i], kCameraNames[i]);
    }
    if (log) {
      REXLOG_INFO("Aim debug: stick=({}, {}) fire={} bomb={}", stick_.x, stick_.y,
                  aim::g_fire.load(std::memory_order_relaxed), aim::g_bomb.load(std::memory_order_relaxed));
    }
  }

  // Where the arms start when the crosshair appears, and where they sit while firing: crossed at the centre.
  static constexpr float kAppearSpread = 3.0f;
  static constexpr float kFiringSpread = -0.35f;

  float spread_ = kAppearSpread;
  float angle_ = 0.0f;
  float hue_ = 0.0f;
  ImVec2 last_mouse_{-1.0f, -1.0f};
  Clock::time_point mouse_moved_{};
  aim::Stick stick_{0, 0};
  uint64_t debug_frames_ = 0;
};

}  // namespace

void CreateMouseAim(rex::ui::ImGuiDrawer* drawer) {
  new MouseAimOverlay(drawer);  // retained by the drawer for the session
}

bool CrosshairShown() {
  return aim::g_crosshair.load(std::memory_order_relaxed);
}

}  // namespace gw2
