// Deadline rank readout (deadline_rank.h), hidden unless gw2_deadline_rank_info is on. Under the score the HUD gains
// two lines, "K 25  G 10" and "Rank 20 > 32", drawn after the game's HUD (sub_8207CA48) with its own text routine and
// colour, so the font, the glow and the interface colour all match. Each field sits at a fixed place, so nothing shifts
// as the numbers change. When a wave raises the rank, "+12" pops beside it; bombs and deaths change the number quietly.
//
// The game is only read, never written. sub_8203DED0, the wave step, is wrapped to check the prediction against the
// rank each wave really sets, and logs any difference.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "deadline_rank.h"
#include "guest_memory.h"
#include "king_records.h"
#include "launcher_cvars.h"

namespace {

namespace rank = gw2::deadline_rank;
using gw2::LoadBE32;

using Clock = std::chrono::steady_clock;
const Clock::time_point g_start = Clock::now();

double Seconds() {
  return std::chrono::duration<double>(Clock::now() - g_start).count();
}

float LoadF(const uint8_t* base, uint32_t at) {
  const uint32_t bits = LoadBE32(base, at);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}

void StoreF(uint8_t* base, uint32_t at, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  gw2::StoreBE32(base, at, bits);
}

rank::Block ReadBlock(const uint8_t* base) {
  const uint32_t at = rank::kBlockAddress;
  return {int32_t(LoadBE32(base, at + rank::kKOffset)), int32_t(LoadBE32(base, at + rank::kGOffset)),
          int32_t(LoadBE32(base, at + rank::kRankOffset)), int32_t(LoadBE32(base, at + rank::kWaveOffset))};
}

// The HUD's text colour (premultiplied by its fade), as the score's multiplier uses it.
constexpr uint32_t kHudTextColour = 0x821F4630;

// Places on the HUD's 640x360 canvas, under the score. The game centres text on x, so numbers go one character at a
// time on a fixed pitch: they only ever grow rightward. Two columns: K over Rank, and G over the next rank.
constexpr double kLeft = 38.0;
constexpr double kFirstLine = 60.0;
constexpr double kLineHeight = 12.0;
constexpr double kScale = 5.0;
constexpr double kPopScale = 7.0;
constexpr double kPitch = 1.4;  // a character's advance, per unit of scale
constexpr double kNumbers = kLeft + 33;
constexpr double kSecondLabel = kLeft + 64;
constexpr double kSecondNumbers = kLeft + 70;
constexpr double kPop = kLeft + 98;

rank::WavePop g_pop;

}  // namespace

REX_EXTERN(__imp__sub_820764C0);  // draws HUD text: f1, f2 the place, f3, f4 the scale, r7 the colour, r8 the format

namespace {

// Text and colours in guest memory. The routine formats its text into its own buffer before drawing, so one buffer is
// reused for every call on the HUD's thread.
struct GuestScratch {
  uint32_t text = 0;
  uint32_t colour = 0;
};

GuestScratch& Scratch() {
  thread_local GuestScratch scratch;
  auto* memory = rex::system::kernel_memory();
  if (scratch.text == 0) scratch.text = memory->SystemHeapAlloc(64);
  if (scratch.colour == 0) scratch.colour = memory->SystemHeapAlloc(16);
  return scratch;
}

void DrawText(PPCContext ctx, uint8_t* base, const std::string& text, double x, double y, double scale,
              uint32_t colour) {
  GuestScratch& scratch = Scratch();
  if (scratch.text == 0 || colour == 0) return;
  std::memcpy(base + scratch.text, text.c_str(), std::min<size_t>(text.size() + 1, 63));
  base[scratch.text + 63] = 0;
  ctx.f1.f64 = x;
  ctx.f2.f64 = y;
  ctx.f3.f64 = scale;
  ctx.f4.f64 = scale;
  ctx.r7.u64 = colour;
  ctx.r8.u64 = scratch.text;
  __imp__sub_820764C0(ctx, base);
}

// The HUD colour brightened by `boost`, for the pop.
uint32_t Brightened(uint8_t* base, float boost) {
  GuestScratch& scratch = Scratch();
  if (scratch.colour == 0) return 0;
  for (uint32_t i = 0; i < 3; ++i) {
    StoreF(base, scratch.colour + i * 4, LoadF(base, kHudTextColour + i * 4) * (1.0f + boost));
  }
  StoreF(base, scratch.colour + 12, LoadF(base, kHudTextColour + 12));
  return scratch.colour;
}

// `text` left-aligned at `left`, one character per pitch.
void DrawLeft(const PPCContext& ctx, uint8_t* base, const std::string& text, double left, double y, double scale,
              uint32_t colour) {
  const double pitch = kPitch * scale;
  for (size_t i = 0; i < text.size(); ++i) {
    DrawText(ctx, base, std::string(1, text[i]), left + pitch * (double(i) + 0.5), y, scale, colour);
  }
}

void DrawReadout(const PPCContext& ctx, uint8_t* base) {
  const rank::Block block = ReadBlock(base);
  const double now = Seconds();
  g_pop.Observe(block, now);
  const double y1 = kFirstLine, y2 = kFirstLine + kLineHeight;
  const double gap = kPitch * kScale;
  DrawText(ctx, base, "K", kNumbers - gap, y1, kScale, kHudTextColour);
  DrawLeft(ctx, base, std::to_string(block.k), kNumbers, y1, kScale, kHudTextColour);
  DrawText(ctx, base, "G", kSecondLabel, y1, kScale, kHudTextColour);
  DrawLeft(ctx, base, std::to_string(block.g), kSecondNumbers, y1, kScale, kHudTextColour);
  DrawText(ctx, base, "Rank", kNumbers - gap * 2.6, y2, kScale, kHudTextColour);
  DrawLeft(ctx, base, std::to_string(block.rank), kNumbers, y2, kScale, kHudTextColour);
  DrawText(ctx, base, ">", kSecondLabel, y2, kScale, kHudTextColour);
  DrawLeft(ctx, base, std::to_string(rank::NextRank(block)), kSecondNumbers, y2, kScale, kHudTextColour);
  if (const auto gain = g_pop.Gain(now)) {
    const float strength = g_pop.Strength(now);
    const double scale = kScale + (kPopScale - kScale) * strength;
    DrawLeft(ctx, base, "+" + std::to_string(*gain), kPop, y2, scale, Brightened(base, 1.5f * strength));
  }
}

}  // namespace

// The in-game HUD.
REX_EXTERN(__imp__sub_8207CA48);
REX_HOOK_RAW(sub_8207CA48) {
  const PPCContext hud = ctx;
  __imp__sub_8207CA48(ctx, base);
  if (!REXCVAR_GET(gw2_deadline_rank_info) || LoadBE32(base, gw2::king::kModeAddress) != rank::kDeadlineMode) return;
  DrawReadout(hud, base);
}

// Deadline's wave step, run every frame of a round: checks the readout's prediction whenever a wave lands.
REX_EXTERN(__imp__sub_8203DED0);
REX_HOOK_RAW(sub_8203DED0) {
  const rank::Block before = ReadBlock(base);
  __imp__sub_8203DED0(ctx, base);
  const rank::Block after = ReadBlock(base);
  if (after.waves == before.waves) return;
  const int32_t predicted = rank::NextRank(before);
  if (predicted != after.rank) {
    REXLOG_WARN("Deadline rank: predicted {} but the wave set {} (K {}, G {}, rank {})", predicted, after.rank,
                before.k, before.g, before.rank);
  } else {
    REXLOG_INFO("Deadline rank: wave {} set rank {} (K {}, G {})", after.waves, after.rank, before.k, before.g);
  }
}
