// The Single Player screen's second page (master_page.h).
//
// The screen (built by sub_8208ED00) is the game's menu rows, one per mode tile, added with sub_820900A8 like any menu
// row: a text id (30 Deadline, 28 Evolved, 33 King, 32 Pacifism, 31 Waves, 29 Sequence) and the same action,
// sub_8208D548, which opens a newly unlocked mode's rules first (sub_82093748) and starts it (sub_8208D410). The tile
// drawer reads the tile's scores with XUserReadStats. Every mode is unlocked and counts as played (custom_modes.cpp).
//
// Page two rebuilds the screen with only the Deadline and Evolved rows. While it shows, their names are the Master
// modes', their views are read from the Master boards
// (live_fix.cpp), and starting one starts the Master mode (custom_modes.h) instead of the game's.

#include "master_page.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>

#include "custom_modes.h"
#include "guest_memory.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"

REXCVAR_DEFINE_INT32(gw2_dev_master_page, 0, "Geometry Wars",
                     "Open the Single Player screen as the menus come up, on page 1 or 2; 3: page 1, then page 2, then "
                     "start Master Deadline (for testing; 0: no)");

REX_EXTERN(sub_8208ED00);  // builds the Single Player screen and switches to it

namespace gw2::master_page {
namespace {

// Screens, by their draw function (sub_8208FFF0's r5).
constexpr uint32_t kSinglePlayerScreen = 0x8208D5B8;  // the one sub_8208ED00 switches to
constexpr uint32_t kRulesScreen = 0x8207B058;         // a mode's rules, between its tile and its round
constexpr uint32_t kIntroScreen = 0x8207ACD0;         // the other screen between a tile and its round (Evolved's)
constexpr uint32_t kMainMenuScreen = 0x8208F9B8;
constexpr uint32_t kTileAction = 0x8208D548;          // every Single Player tile's action
constexpr uint32_t kDeadlineText = 30;
constexpr uint32_t kEvolvedText = 28;
constexpr uint32_t kDeadlineLeaderboardText = 61;  // "Deadline Leaderboard", the leaderboard screen's title
constexpr uint32_t kEvolvedLeaderboardText = 59;
constexpr uint32_t kStartState = 0x821993F0;  // 0 or 3 in the menus
constexpr uint16_t kRightShoulder = 0x0200;
// The Single Player screen's score reads (sub_8208BA90's state at 0x821EBE78): the byte at +36 asks for them again,
// which the game otherwise only does when the signed-in player changes.
constexpr uint32_t kScoresDirty = 0x821EBE78 + 36;
constexpr uint32_t kGamepadOffset = 4;  // XINPUT_STATE: packet number, then the gamepad

std::atomic<uint32_t> g_screen{0};     // the screen last switched to
std::atomic<bool> g_page_two{false};  // from RB until the main menu shows
std::atomic<uint32_t> g_chosen{0};     // the text id of the Master tile pressed (its action's row, +4)
std::atomic<bool> g_turn{false};       // asked for by RB, made on the game thread
std::atomic<bool> g_in_menus{false};
std::mutex g_mutex;
std::array<uint16_t, 4> g_buttons{};  // each pad's buttons the poll before
bool g_holding = false;               // the RB that turned the page is kept from the game until let go

bool OnScreen() {
  return g_screen.load(std::memory_order_relaxed) == kSinglePlayerScreen;
}

// The Master page or a screen it led to before the round.
bool PageTwo() {
  const uint32_t screen = g_screen.load(std::memory_order_relaxed);
  return g_page_two.load(std::memory_order_relaxed) &&
         (screen == kSinglePlayerScreen || screen == kRulesScreen || screen == kIntroScreen);
}

uint32_t GuestText(const char* text) {
  const uint32_t slot = rex::system::kernel_memory()->SystemHeapAlloc(520);  // a whole slot, as the game's are
  if (slot == 0) return 0;
  uint8_t* host = rex::system::kernel_memory()->TranslateVirtual<uint8_t*>(slot);
  std::memset(host, 0, 520);
  std::memcpy(host, text, std::strlen(text));
  return slot;
}

const custom_modes::Master* MasterForText(uint32_t id) {
  if (id == kDeadlineText) return custom_modes::MasterFor(0);
  if (id == kEvolvedText) return custom_modes::MasterFor(1);
  return nullptr;
}

// Builds the Single Player screen again for the page now chosen, reading every tile's scores afresh (through
// ViewSource, so page two's come from the Master boards and the HUD's score to beat follows them).
void Rebuild(PPCContext& ctx, uint8_t* base) {
  base[kScoresDirty] = 1;
  PPCContext call = ctx;
  call.r3.u64 = 1;  // with its scores
  sub_8208ED00(call, base);
}

class Overlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Overlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!OnScreen() || !g_in_menus.load(std::memory_order_relaxed)) return;
    using namespace gw2::neon;
    const bool two = g_page_two.load(std::memory_order_relaxed);
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 o((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float pulse = 0.7f + 0.3f * std::sin(float(Seconds()) * 4.0f);

    // The page, top right beside the title: which one shows, and RB (3) to the other, with a bobbing arrow.
    {
      // The line font has no slash.
      const std::string page = two ? "Master modes  2 of 2" : "Game modes  1 of 2";
      // The key, then where it goes, as the game's own hints are written.
      const bool keyboard = rex::cvar::GetFlagByName(kKeyboardKey) == "true";
      const std::string key = std::string(keyboard ? "3" : "RB") + (two ? "  Game modes" : "  Master modes");
      const float size = u * 2.2f, key_size = u * 1.5f;
      const float right = o.x + cw * 0.955f, top = o.y + ch * 0.045f;
      const float w = std::max(line_font::GameWidth(page, size), line_font::GameWidth(key, key_size) + u * 4.0f);
      const ImVec2 p0(right - w - u * 2.0f, top), p1(right, top + size + key_size + u * 3.2f);
      draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 0.85f), u);
      draw->AddRect(p0, p1, Theme(0.0, pulse), u, 0, std::max(1.5f, u * 0.3f));
      line_font::GameText(draw, ImVec2(p0.x + u, top + u), size, page, Theme(), std::max(1.2f, size * 0.08f));
      const float ky = top + size + u * 2.0f;
      const float dir = two ? -1.0f : 1.0f;
      const float bob = std::sin(float(Seconds()) * 5.0f) * u * 0.35f * dir;
      const float ax = two ? p0.x + u * 1.8f : p1.x - u * 1.8f;
      const float ay = ky + key_size * 0.5f;
      draw->AddTriangleFilled(ImVec2(ax + bob + dir * u * 0.9f, ay), ImVec2(ax + bob - dir * u * 0.4f, ay - u * 0.8f),
                              ImVec2(ax + bob - dir * u * 0.4f, ay + u * 0.8f), Theme());
      const float kx = two ? p0.x + u * 3.6f : p1.x - u * 3.6f - line_font::GameWidth(key, key_size);
      line_font::GameText(draw, ImVec2(kx, ky), key_size, key, Rgba(0.9f, 0.93f, 1.0f, 0.9f), std::max(1.0f, key_size * 0.07f));
    }
    if (!two) return;

    // Page two's right half, where the game has no tiles: what the Master modes are.
    const ImVec2 p0(o.x + cw * 0.53f, o.y + ch * 0.18f), p1(o.x + cw * 0.935f, o.y + ch * 0.66f);
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 0.75f), u * 1.2f);
    draw->AddRect(p0, p1, Theme(0.0, 0.3f), u * 1.2f, 0, u * 0.9f);
    draw->AddRect(p0, p1, Theme(0.0, pulse), u * 1.2f, 0, u * 0.25f);
    float y = p0.y + u * 3.0f;
    const float x = p0.x + u * 3.0f;
    const float title = u * 5.0f;
    line_font::GameText(draw, ImVec2(x + u * 0.15f, y + u * 0.15f), title, "Master modes", Theme(0.0, 0.3f),
                    std::max(2.0f, title * 0.12f));
    line_font::GameText(draw, ImVec2(x, y), title, "Master modes", Theme(), std::max(1.5f, title * 0.07f));
    y += title + u * 3.0f;
    const ImU32 white = Rgba(1, 1, 1, 0.95f);
    const ImU32 dim = Rgba(0.8f, 0.85f, 0.95f, 0.85f);
    constexpr const char* kRules[] = {"One ship. No extra lives, ever.", "A bomb at 100K, 1M, 10M and 100M.",
                                      "Three bombs to start, and no others."};
    constexpr const char* kNotes[] = {"Their own leaderboards.", "No achievements or unlocks."};
    // Each line as big as fits the panel.
    const float room = p1.x - x - u * 3.0f;
    float widest = 0;
    for (const char* text : kRules) widest = std::max(widest, line_font::GameWidth(text, 1.0f));
    const float line = std::min(u * 2.4f, room / widest);
    for (const char* text : kRules) {
      line_font::GameText(draw, ImVec2(x, y), line, text, white, std::max(1.0f, line * 0.07f));
      y += line + u * 1.8f;
    }
    y += u * 1.5f;
    for (const char* text : kNotes) {
      line_font::GameText(draw, ImVec2(x, y), line * 0.8f, text, dim, std::max(1.0f, line * 0.06f));
      y += line * 0.8f + u * 1.4f;
    }
  }
};

}  // namespace

bool SkipRow(const PPCContext& row) {
  if (!g_page_two.load(std::memory_order_relaxed) || row.r4.u32 != kTileAction) return false;
  return row.r3.u32 != kDeadlineText && row.r3.u32 != kEvolvedText;
}

std::optional<uint32_t> Text(uint32_t id) {
  if (g_page_two.load(std::memory_order_relaxed)) {
    static const uint32_t deadline_board = GuestText("Master Deadline Leaderboard");
    static const uint32_t evolved_board = GuestText("Master Evolved Leaderboard");
    if (id == kDeadlineLeaderboardText && deadline_board) return deadline_board;
    if (id == kEvolvedLeaderboardText && evolved_board) return evolved_board;
  }
  if (!PageTwo()) return std::nullopt;
  static const uint32_t deadline = GuestText("Master Deadline");
  static const uint32_t evolved = GuestText("Master Evolved");
  if (id == kDeadlineText && deadline) return deadline;
  if (id == kEvolvedText && evolved) return evolved;
  return std::nullopt;
}

uint32_t LeaderboardSource(uint32_t view) {
  if (!g_page_two.load(std::memory_order_relaxed)) return view;
  const custom_modes::Master* master = custom_modes::MasterFor(view);
  return master ? master->board : view;
}

bool StartTile(const PPCContext&, const uint8_t*) {
  // A Master tile pressed on page two holds until its round starts, whichever screens the game shows between: the
  // page stays two until the main menu shows.
  if (!g_page_two.load(std::memory_order_relaxed)) return false;
  const custom_modes::Master* master = MasterForText(g_chosen.load(std::memory_order_relaxed));
  if (!master) return false;
  REXLOG_INFO("Master modes: {} starts", master->mode.name);
  custom_modes::PlayMaster(*master);
  return true;
}

void FilterPad(uint8_t* base, uint32_t user, uint32_t state) {
  if (state == 0 || user >= g_buttons.size()) return;
  const uint32_t pad = state + kGamepadOffset;
  const uint16_t buttons = LoadBE16(base, pad);
  std::lock_guard lock(g_mutex);
  const uint16_t pressed = uint16_t(buttons & ~g_buttons[user]);
  g_buttons[user] = buttons;
  if (g_holding) {
    if (!(buttons & kRightShoulder)) g_holding = false;
    StoreBE<uint16_t>(base, pad, uint16_t(buttons & ~kRightShoulder));
    return;
  }
  if (!OnScreen() || !g_in_menus.load(std::memory_order_relaxed) || !(pressed & kRightShoulder)) return;
  g_turn.store(true, std::memory_order_relaxed);
  g_holding = true;
  StoreBE<uint16_t>(base, pad, uint16_t(buttons & ~kRightShoulder));
}

void EachFrame(PPCContext& ctx, uint8_t* base) {
  const uint32_t start = LoadBE32(base, kStartState);
  g_in_menus.store(start == 0 || start == 3, std::memory_order_relaxed);
  static bool dev_opened = false;
  static int dev_frames = 0;  // --gw2_dev_master_page=3: frames since page 1 opened
  if (dev_frames > 0 && ++dev_frames == 120) g_turn.store(true, std::memory_order_relaxed);
  if (dev_frames == 240) {
    dev_frames = 0;
    if (const custom_modes::Master* master = custom_modes::MasterFor(0)) custom_modes::PlayMaster(*master);
  }
  if (REXCVAR_GET(gw2_dev_master_page) != 0 && !dev_opened && g_screen.load(std::memory_order_relaxed) != 0 &&
      (start == 0 || start == 3)) {
    dev_opened = true;
    g_page_two.store(REXCVAR_GET(gw2_dev_master_page) == 2, std::memory_order_relaxed);
    dev_frames = REXCVAR_GET(gw2_dev_master_page) == 3 ? 1 : 0;
    REXLOG_INFO("Master modes: opening Single Player page {} (--gw2_dev_master_page)", REXCVAR_GET(gw2_dev_master_page));
    Rebuild(ctx, base);
    return;
  }
  if (!g_turn.exchange(false, std::memory_order_relaxed) || !OnScreen()) return;
  const bool two = !g_page_two.load(std::memory_order_relaxed);
  g_page_two.store(two, std::memory_order_relaxed);
  REXLOG_INFO("Master modes: Single Player page {}", two ? 2 : 1);
  Rebuild(ctx, base);
}

void CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  new Overlay(drawer);  // retained by the drawer for the session
}

void OnScreenSwitch(uint32_t screen) {
  g_screen.store(screen, std::memory_order_relaxed);
  if (screen == kMainMenuScreen) g_page_two.store(false, std::memory_order_relaxed);
}

bool OnMainMenu() {
  return g_screen.load(std::memory_order_relaxed) == kMainMenuScreen && g_in_menus.load(std::memory_order_relaxed);
}

void OnTile(const uint8_t* base, uint32_t row) {
  if (g_page_two.load(std::memory_order_relaxed) && row != 0) {
    g_chosen.store(LoadBE32(base, row + 4), std::memory_order_relaxed);
  }
}

}  // namespace gw2::master_page

// Switches the screen: r5 is the screen's draw function.
REX_EXTERN(__imp__sub_8208FFF0);
REX_HOOK_RAW(sub_8208FFF0) {
  gw2::master_page::OnScreenSwitch(ctx.r5.u32);
  __imp__sub_8208FFF0(ctx, base);
}

// A Single Player tile's action (r4 its row, the text id at +4): on the Master page, which Master mode was chosen.
REX_EXTERN(__imp__sub_8208D548);
REX_HOOK_RAW(sub_8208D548) {
  gw2::master_page::OnTile(base, ctx.r4.u32);
  __imp__sub_8208D548(ctx, base);
}

