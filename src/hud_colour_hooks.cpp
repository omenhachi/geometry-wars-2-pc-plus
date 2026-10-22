// Interface colour in the game (hud_colour.h). The menus and the HUD draw every character through two glyph
// functions, given a pointer to the RGBA colour (floats, premultiplied by the fade) that the line drawer copies into
// each vertex. The hooks point them at a recoloured copy instead, so the game's own colours are never written.
//
// Help and options gains a Colour row after Highscore Display. The options menu adds its rows with sub_820900A8
// (label, input callback, draw callback, ...), hooked in main_menu_exit_game.cpp; the extra row's callbacks are host functions behind guest thunks, and
// draw and step like the game's own rows (sub_820870C0 and sub_82086EC8).

#include "hud_colour_hooks.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"
#include "hud_colour.h"
#include "launcher_settings.h"

namespace {

using gw2::LoadBE32;
using gw2::StoreBE32;
namespace colour = gw2::hud_colour;

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
  StoreBE32(base, at, bits);
}

// The colour a glyph should draw with instead of the one at `colour`, or 0 to keep it. The glyph copies the colour into
// its vertices before it returns, so one buffer per drawing thread is enough.
uint32_t RecolouredText(uint8_t* base, uint32_t colour) {
  if (colour == 0) return 0;
  const auto look = colour::CurrentLook(Seconds());
  if (!look) return 0;
  const auto out = colour::Recolour({LoadF(base, colour), LoadF(base, colour + 4), LoadF(base, colour + 8)}, *look);
  if (!out) return 0;
  thread_local uint32_t buffer = 0;
  if (buffer == 0) buffer = rex::system::kernel_memory()->SystemHeapAlloc(16);
  if (buffer == 0) return 0;
  StoreF(base, buffer, out->r);
  StoreF(base, buffer + 4, out->g);
  StoreF(base, buffer + 8, out->b);
  StoreF(base, buffer + 12, LoadF(base, colour + 12));
  return buffer;
}

// The row's text, in guest memory for as long as the game runs.
uint32_t GuestText(std::string_view text) {
  static std::mutex mutex;
  static std::map<std::string, uint32_t, std::less<>> texts;
  std::lock_guard lock(mutex);
  if (const auto it = texts.find(text); it != texts.end()) return it->second;
  const uint32_t address = rex::system::kernel_memory()->SystemHeapAlloc(uint32_t(text.size()) + 1);
  if (address == 0) return 0;
  uint8_t* host = rex::system::kernel_memory()->TranslateVirtual<uint8_t*>(address);
  std::memcpy(host, text.data(), text.size());
  host[text.size()] = 0;
  texts.emplace(std::string(text), address);
  return address;
}

std::function<void(std::string_view, std::string)> g_save;

constexpr uint32_t kScoreDisplayLabel = 26;  // the Highscore Display row, which the Colour row follows
constexpr uint32_t kButtonLeft = 0x10;       // the row input's button bits, as sub_82086EC8 reads them
constexpr uint32_t kButtonRight = 0x20;
constexpr uint32_t kMenuClickSound = 24;

uint32_t g_row_input = 0;
uint32_t g_row_draw = 0;

int CurrentIndex(const std::vector<colour::Preset>& cycle) {
  const colour::Preset preset = colour::CurrentPreset();
  for (size_t i = 0; i < cycle.size(); ++i) {
    if (cycle[i] == preset) return int(i);
  }
  return 0;
}

}  // namespace

REX_EXTERN(__imp__sub_820900A8);  // adds an options menu row
REX_EXTERN(sub_82090A98);  // draws a row: label, arrows and value
REX_EXTERN(sub_8208FFD8);  // what the game's rows call on a change
REX_EXTERN(sub_820787F8);  // plays a sound

namespace {

void ColourRowDraw(PPCContext& ctx, uint8_t* base) {
  const auto cycle = colour::Cycle(colour::CurrentCustom().has_value());
  const int index = CurrentIndex(cycle);
  ctx.r4.u64 = GuestText("Colour");
  ctx.r5.u64 = GuestText(colour::Label(cycle[index]));
  ctx.r6.u64 = index > 0 ? 1 : 0;
  ctx.r7.u64 = index + 1 < int(cycle.size()) ? 1 : 0;
  sub_82090A98(ctx, base);
}

void ColourRowInput(PPCContext& ctx, uint8_t* base) {
  const uint32_t buttons = LoadBE32(base, ctx.r3.u32 + 8);
  const auto cycle = colour::Cycle(colour::CurrentCustom().has_value());
  const int index = CurrentIndex(cycle);
  int next = index;
  if ((buttons & kButtonLeft) && index > 0) next = index - 1;
  else if ((buttons & kButtonRight) && index + 1 < int(cycle.size())) next = index + 1;
  if (next == index) return;
  sub_8208FFD8(ctx, base);
  ctx.r3.u64 = kMenuClickSound;
  sub_820787F8(ctx, base);
  colour::SetCurrent(cycle[next], colour::CurrentCustom());
  REXLOG_INFO("Interface colour: {}", colour::Name(cycle[next]));
  if (g_save) g_save(gw2::kHudColourKey, std::string(colour::Name(cycle[next])));
}

bool EnsureRowCallbacks() {
  if (g_row_input != 0 && g_row_draw != 0) return true;
  auto* state = rex::system::kernel_state();
  auto* dispatcher = state ? state->function_dispatcher() : nullptr;
  if (!dispatcher) return false;
  constexpr uint32_t kOptionsMenu = 0x82087458;  // the caller the thunks are routed for
  g_row_input = dispatcher->AllocateThunk(ColourRowInput, kOptionsMenu);
  g_row_draw = dispatcher->AllocateThunk(ColourRowDraw, kOptionsMenu);
  if (g_row_input == 0 || g_row_draw == 0) {
    REXLOG_WARN("Interface colour: the Colour row could not be added to Help and options");
    return false;
  }
  return true;
}

}  // namespace

namespace gw2 {

void ApplyHudColourSetting() {
  const auto custom = hud_colour::CustomLook(rex::cvar::GetFlagByName(kHudColourCustomKey));
  const auto preset = hud_colour::Parse(rex::cvar::GetFlagByName(kHudColourKey)).value_or(hud_colour::Preset::kGreen);
  hud_colour::SetCurrent(preset, custom);
  REXLOG_INFO("Interface colour: {}", hud_colour::Name(hud_colour::CurrentPreset()));
}

void SetHudColourSaver(std::function<void(std::string_view key, std::string value)> save) {
  g_save = std::move(save);
}

// The Colour row goes straight after Highscore Display, with the same settings and its own callbacks.
void AfterMenuRowAdded(const PPCContext& row, uint8_t* base) {
  if (row.r3.u32 != kScoreDisplayLabel || !EnsureRowCallbacks()) return;
  PPCContext colour_row = row;
  colour_row.r4.u64 = g_row_input;
  colour_row.r5.u64 = g_row_draw;
  __imp__sub_820900A8(colour_row, base);
}

}  // namespace gw2

// The two glyph drawers: r7 the character, r8 the colour.
REX_EXTERN(__imp__sub_82075DC8);
REX_HOOK_RAW(sub_82075DC8) {
  if (const uint32_t recoloured = RecolouredText(base, ctx.r8.u32)) ctx.r8.u64 = recoloured;
  __imp__sub_82075DC8(ctx, base);
}

REX_EXTERN(__imp__sub_82075FA8);
REX_HOOK_RAW(sub_82075FA8) {
  if (const uint32_t recoloured = RecolouredText(base, ctx.r8.u32)) ctx.r8.u64 = recoloured;
  __imp__sub_82075FA8(ctx, base);
}

