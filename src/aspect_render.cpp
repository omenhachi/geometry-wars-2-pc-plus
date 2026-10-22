#include <bit>
#include <cstdint>
#include <mutex>
#include <set>
#include <tuple>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/xvideo.h>

#include "aim_camera.h"
#include "aspect_ratio.h"
#include "guest_memory.h"
#include "launcher_cvars.h"

namespace {

// The canvas width as a float, stored by the graphics init sub_8205D538 before it creates the device.
constexpr uint32_t kCanvasWidthAddress = 0x821A1D7C;
constexpr uint32_t kSplashSpriteReturn = 0x8207E9E0;

gw2::AspectRatio ConfiguredAspect() {
  static const gw2::AspectRatio aspect = gw2::ParseAspectRatio(REXCVAR_GET(gw2_aspect_ratio));
  return aspect;
}

void StoreFloat(uint8_t* base, uint32_t addr, float value) {
  gw2::StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

// Diagnostics for locating the keytip button glyphs: each distinct sprite once.
void LogSpriteOnce(uint32_t lr, uint32_t texture, double x, double y, double width, double height) {
  static std::mutex mutex;
  static std::set<std::tuple<uint32_t, uint32_t, int, int, int, int>> seen;
  const auto key = std::make_tuple(lr, texture, int(x), int(y), int(width), int(height));
  std::lock_guard lock(mutex);
  if (seen.size() < 2000 && seen.insert(key).second) {
    REXLOG_INFO("Sprite debug: lr={:08X} texture={:08X} rect=({:.1f}, {:.1f}, {:.1f}, {:.1f})", lr, texture, x, y,
                width, height);
  }
}

}  // namespace

REXCVAR_DEFINE_BOOL(gw2_sprite_debug, false, "Geometry Wars", "Log each distinct sprite the game queues, once");

// The game builds a 1920x1080 canvas for an HD answer. The canvas stays 720 lines, so the answer is never HD.
REX_HOOK_RAW(__imp__XGetVideoMode) {
  auto* mode = reinterpret_cast<rex::system::X_VIDEO_MODE*>(base + ctx.r3.u32);
  rex::kernel::xboxkrnl::VdQueryVideoMode(mode);
  mode->is_hi_def = 0;
}

// r7 holds the present parameters: back buffer width at +0 and height at +4. Their size, and the floats the game
// stores with it, are the canvas.
REX_EXTERN(__imp__sub_820A1D58);
REX_HOOK_RAW(sub_820A1D58) {
  const uint32_t parameters = ctx.r7.u32;
  const gw2::AspectRatio aspect = ConfiguredAspect();
  const uint32_t width = gw2::LoadBE32(base, parameters);
  const uint32_t height = gw2::LoadBE32(base, parameters + 4);
  if (width == uint32_t(gw2::CanvasWidth(gw2::AspectRatio::k16x9)) && height == uint32_t(gw2::kCanvasHeight)) {
    gw2::StoreBE32(base, parameters, uint32_t(gw2::CanvasWidth(aspect)));
    StoreFloat(base, kCanvasWidthAddress, float(gw2::CanvasWidth(aspect)));
    REXLOG_INFO("Guest back buffer: {}x{} ({})", gw2::CanvasWidth(aspect), gw2::kCanvasHeight,
                gw2::InfoFor(aspect).key);
  } else {
    REXLOG_WARN("Guest back buffer is {}x{}, not the expected 1280x720; keeping it", width, height);
  }
  __imp__sub_820A1D58(ctx, base);
}

// f1 is the vertical field of view in radians and f2 the aspect, 16:9 in every call the game makes.
REX_EXTERN(__imp__sub_8206A300);
REX_HOOK_RAW(sub_8206A300) {
  const gw2::AspectRatio aspect = ConfiguredAspect();
  if (aspect != gw2::AspectRatio::k16x9 && ctx.f2.f64 == gw2::kGameAspect) {
    const gw2::Projection projection = gw2::ProjectionFor(ctx.f1.f64, gw2::CanvasAspect(aspect));
    ctx.f1.f64 = projection.fov;
    ctx.f2.f64 = projection.aspect;
  }
  const uint32_t matrix = ctx.r3.u32;
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_8206A300(ctx, base);
  gw2::aim::CaptureProjection(base, matrix, lr);
}

// Queues a full-screen sprite given as x, y, width and height (f1 to f4) in a 640x360 space that the game maps onto the
// whole canvas. The other callers draw bands and overlays that should keep covering the canvas; the splash image is
// kept in 16:9.
REX_EXTERN(__imp__sub_8205D8F8);
REX_HOOK_RAW(sub_8205D8F8) {
  if (REXCVAR_GET(gw2_sprite_debug)) {
    LogSpriteOnce(uint32_t(ctx.lr), ctx.r8.u32, ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64);
  }
  const gw2::AspectRatio aspect = ConfiguredAspect();
  if (aspect != gw2::AspectRatio::k16x9 && ctx.lr == kSplashSpriteReturn && ctx.f1.f64 == 0.0 && ctx.f2.f64 == 0.0 &&
      ctx.f3.f64 == double(gw2::kSpriteSpaceWidth) && ctx.f4.f64 == double(gw2::kSpriteSpaceHeight)) {
    const gw2::SpriteRect rect = gw2::FitSpriteTo16x9(gw2::CanvasAspect(aspect));
    ctx.f1.f64 = rect.x;
    ctx.f2.f64 = rect.y;
    ctx.f3.f64 = rect.width;
    ctx.f4.f64 = rect.height;
  }
  __imp__sub_8205D8F8(ctx, base);
}
