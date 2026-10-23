// Watch replays: a main menu row, the list it opens, and the badge while a replay plays.
//
// The row is added after Multiplayer by the one hook on sub_820900A8, with a text id the game does not have (answered
// by the text hook in live_fix.cpp) and a host function as its action. The list is drawn over the main menu; while it
// is open the game is given a neutral pad and the list reads the real one (replays_menu_rules.h), so a controller and
// the keyboard (through the game's keyboard-to-pad bindings) both work, and nothing reaches the menu behind.
//
// Newest first, on an All tab with the cursor on the top row: Enter or A watches, nothing to decide. Y stars a replay,
// which keeps it; X asks once, then deletes. Nothing counts replays or asks for tidying.

#include "replays_menu.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>

#include "custom_modes.h"
#include "demo.h"
#include "demo_library.h"
#include "guest_memory.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"
#include "replays_menu_rules.h"

REX_EXTERN(__imp__sub_820900A8);  // adds a menu row

namespace gw2::replays_menu {
namespace {

using namespace gw2::neon;
namespace library = gw2::demo_library;
namespace custom_mode = gw2::custom_mode;

constexpr uint32_t kMultiplayerTextId = 2;
constexpr uint32_t kMultiplayerAction = 0x8208F028;
constexpr uint32_t kMainMenu = 0x8208FD00;  // the caller the row's thunk is routed for
constexpr uint32_t kGamepadOffset = 4;      // XINPUT_STATE: packet number, then the gamepad
constexpr uint32_t kGamepadSize = 12;

std::mutex g_mutex;
ListInput g_input;
std::vector<Action> g_actions;     // from the game thread, applied as the list draws
bool g_opened = false;             // the list was opened and has not drawn yet
uint32_t g_row_action = 0;

void RowAction(PPCContext&, uint8_t*) {
  std::lock_guard lock(g_mutex);
  g_input.SetOpen(true);
  g_actions.clear();
  g_opened = true;
}

bool KeyboardMode() {
  return rex::cvar::GetFlagByName(kKeyboardKey) == "true";
}

// The friendliest key bound to a pad button, for the hints: Enter, Space and Backspace read better than ; or '.
std::string KeyFor(const char* binding, const char* fallback) {
  const std::string keys = rex::cvar::GetFlagByName(binding);
  std::string first;
  size_t start = 0;
  while (start < keys.size()) {
    size_t end = keys.find(',', start);
    if (end == std::string::npos) end = keys.size();
    std::string key = keys.substr(start, end - start);
    if (key == "Return") key = "Enter";
    if (key == "Enter" || key == "Space" || key == "Backspace") return key;
    if (first.empty()) first = key;
    start = end + 1;
  }
  return first.empty() ? fallback : first;
}

std::string When(int64_t recorded_at) {
  const std::time_t t = std::time_t(recorded_at);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%d %b %H:%M", &local);
  return text;
}

class ReplaysMenu final : public rex::ui::ImGuiDialog {
 public:
  explicit ReplaysMenu(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::vector<Action> actions;
    bool open = false, opened = false;
    {
      std::lock_guard lock(g_mutex);
      actions.swap(g_actions);
      open = g_input.open();
      opened = g_opened;
      g_opened = false;
    }
    if (opened) Reload(true);
    for (const Action action : actions) Apply(action);
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (open) {
      DrawList(io);
    } else {
      DrawBadge(io);
    }
  }

 private:
  // The tabs: All, then each mode that has a replay.
  std::vector<int> Tabs() const {
    std::vector<int> tabs = {-1};
    for (uint32_t mode = 0; mode < 6; ++mode) {
      for (const auto& e : entries_) {
        if (e.mode == mode) {
          tabs.push_back(int(mode));
          break;
        }
      }
    }
    return tabs;
  }

  std::vector<const library::Entry*> Shown() const {
    std::vector<const library::Entry*> shown;
    for (const auto& e : entries_) {
      if (tab_ < 0 || int(e.mode) == tab_) shown.push_back(&e);
    }
    return shown;
  }

  void Reload(bool fresh) {
    const std::string keep = Selected();
    entries_ = library::Scan(demo::Folder());
    if (fresh) {
      tab_ = -1;
      cursor_ = 0;
      message_.clear();
    }
    confirm_delete_ = false;
    const auto tabs = Tabs();
    if (std::find(tabs.begin(), tabs.end(), tab_) == tabs.end()) tab_ = -1;
    const auto shown = Shown();
    cursor_ = std::min<int>(cursor_, std::max<int>(0, int(shown.size()) - 1));
    for (size_t i = 0; !fresh && i < shown.size(); ++i) {
      if (shown[i]->file == keep) cursor_ = int(i);
    }
  }

  std::string Selected() const {
    const auto shown = Shown();
    return cursor_ >= 0 && cursor_ < int(shown.size()) ? shown[cursor_]->file : std::string();
  }

  void Apply(Action action) {
    const auto shown = Shown();
    if (action != Action::kDelete) confirm_delete_ = false;
    switch (action) {
      case Action::kUp:
        if (cursor_ > 0) --cursor_;
        break;
      case Action::kDown:
        if (cursor_ + 1 < int(shown.size())) ++cursor_;
        break;
      case Action::kPreviousTab:
      case Action::kNextTab: {
        const auto tabs = Tabs();
        const auto at = std::find(tabs.begin(), tabs.end(), tab_);
        int index = at == tabs.end() ? 0 : int(at - tabs.begin());
        index = (index + (action == Action::kNextTab ? 1 : int(tabs.size()) - 1)) % int(tabs.size());
        tab_ = tabs[index];
        cursor_ = 0;
        break;
      }
      case Action::kStar:
        if (!shown.empty()) {
          library::SetStarred(demo::Folder(), shown[cursor_]->file, !shown[cursor_]->starred);
          Reload(false);
        }
        break;
      case Action::kDelete:
        if (shown.empty()) break;
        if (!confirm_delete_) {
          confirm_delete_ = true;
        } else {
          const std::string file = shown[cursor_]->file;
          std::error_code error;
          std::filesystem::remove(demo::Folder() / file, error);
          library::SetStarred(demo::Folder(), file, false);
          Reload(false);
        }
        break;
      case Action::kWatch: {
        if (shown.empty()) {
          Reopen();
          break;
        }
        const demo::WatchResult result = demo::Watch(demo::Folder() / shown[cursor_]->file);
        if (result == demo::WatchResult::kStarted) break;  // the list has closed; the replay starts next frame
        message_ = result == demo::WatchResult::kUnreadable ? "That replay can't be read."
                                                            : "That replay was made with another version of PC Plus.";
        Reopen();
        break;
      }
      case Action::kClose:
      case Action::kNone:
        break;
    }
  }

  void Reopen() {
    std::lock_guard lock(g_mutex);
    g_input.SetOpen(true);
  }

  void DrawList(ImGuiIO& io) {
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    const ImVec2 p0(origin.x + cw * 0.14f, origin.y + ch * 0.12f);
    const ImVec2 p1(origin.x + cw * 0.86f, origin.y + ch * 0.90f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Rgba(0, 0, 0, 0.45f));
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 1.0f), u * 1.2f);
    draw->AddRect(p0, p1, Theme(0.0, 0.25f), u * 1.2f, 0, u * 0.9f);
    draw->AddRect(p0, p1, Theme(0.0, 0.9f), u * 1.2f, 0, u * 0.25f);
    const float pad = u * 3.0f;
    const float title = u * 4.6f;
    line_font::Draw(draw, ImVec2(p0.x + pad, p0.y + pad), title, "Watch replays", Theme(), std::max(1.2f, title * 0.06f));

    // Tabs.
    const float tab_size = u * 2.4f;
    const float thin = std::max(1.0f, tab_size * 0.07f);
    float x = p0.x + pad;
    const float tab_y = p0.y + pad + title + u * 2.0f;
    for (const int tab : Tabs()) {
      const std::string name = tab < 0 ? "All" : ModeName(uint32_t(tab));
      const bool current = tab == tab_;
      const float w = line_font::Width(name, tab_size);
      if (current) {
        draw->AddRectFilled(ImVec2(x - u * 0.8f, tab_y - u * 0.7f), ImVec2(x + w + u * 0.8f, tab_y + tab_size + u * 0.7f),
                            Theme(0.0, 0.18f), u * 0.5f);
      }
      line_font::Draw(draw, ImVec2(x, tab_y), tab_size, name, current ? Theme() : Rgba(0.7f, 0.75f, 0.85f, 0.8f), thin);
      x += w + u * 3.0f;
    }

    // Rows.
    const auto shown = Shown();
    const float list_top = tab_y + tab_size + u * 3.0f;
    const float footer = u * 6.0f;
    const float row_h = u * 5.4f;
    const int visible = std::max(1, int((p1.y - footer - list_top) / row_h));
    first_ = std::clamp(first_, std::max(0, cursor_ - visible + 1), cursor_);
    const float text = row_h * 0.42f;
    const float row_thin = std::max(1.0f, text * 0.07f);
    if (shown.empty()) {
      const char* empty = entries_.empty() ? "No replays yet. Finish a run and it'll be waiting here."
                                           : "No replays in this mode yet.";
      line_font::Draw(draw, ImVec2(p0.x + pad, list_top + u * 2.0f), text, empty, Rgba(0.8f, 0.85f, 0.95f, 0.9f),
                      row_thin);
    }
    const float left = p0.x + pad, right = p1.x - pad;
    for (int i = first_; i < int(shown.size()) && i < first_ + visible; ++i) {
      const library::Entry& e = *shown[i];
      const float y = list_top + float(i - first_) * row_h;
      const ImVec2 r0(left, y), r1(right, y + row_h - u * 0.6f);
      const bool hovered = ImGui::IsMouseHoveringRect(r0, r1, false);
      if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) cursor_ = i;
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        cursor_ = i;
        {
          std::lock_guard lock(g_mutex);
          g_input.SetOpen(false);
        }
        Apply(Action::kWatch);
      }
      const bool current = i == cursor_;
      if (current) {
        draw->AddRectFilled(r0, r1, Theme(0.0, 0.12f), u * 0.6f);
        draw->AddRect(r0, r1, Theme(0.0, 0.8f), u * 0.6f, 0, 1.5f);
      }
      const float mid = y + (row_h - u * 0.6f) * 0.5f;
      if (e.starred) DrawStar(draw, ImVec2(left + u * 2.0f, mid), u * 1.3f, Rgba(1, 0.92f, 0.5f));
      const ImU32 white = Rgba(1, 1, 1, 0.92f);
      const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
      if (current && confirm_delete_) {
        line_font::Draw(draw, ImVec2(left + u * 4.5f, mid - text * 0.5f), text,
                        std::string("Delete this replay? Press ") + DeleteKey() + " again", Rgba(1, 0.85f, 0.6f),
                        row_thin);
        continue;
      }
      const std::string mode = e.custom_name.empty() ? std::string(ModeName(e.mode)) : e.custom_name;
      line_font::Draw(draw, ImVec2(left + u * 4.5f, mid - text * 0.5f), text, mode, Theme(), row_thin);
      const auto custom = e.custom_name.empty() ? std::nullopt : custom_mode::Read(e.custom_rules);
      const std::string score = custom ? custom_modes::ScoreText(*custom, e.score) : Grouped(int64_t(e.score));
      const float score_right = left + (right - left) * 0.58f;
      line_font::Draw(draw, ImVec2(score_right - line_font::Width(score, text), mid - text * 0.5f), text, score, white,
                      row_thin);
      line_font::Draw(draw, ImVec2(left + (right - left) * 0.64f, mid - text * 0.5f), text, When(e.recorded_at), dim,
                      row_thin);
      const std::string length = Clock2(float(e.ticks) / 60.0f);
      line_font::Draw(draw, ImVec2(right - u * 1.5f - line_font::Width(length, text), mid - text * 0.5f), text, length,
                      dim, row_thin);
    }

    // Message and hints.
    const float hint = u * 2.0f;
    const float hint_thin = std::max(1.0f, hint * 0.07f);
    if (!message_.empty()) {
      line_font::Draw(draw, ImVec2(left, p1.y - footer - hint * 0.6f), hint, message_, Rgba(1, 0.85f, 0.6f), hint_thin);
    }
    const bool keys = KeyboardMode();
    const std::string hints =
        keys ? std::string(KeyFor("keybind_start", "Enter")) + " Watch   " + KeyFor("keybind_y", "L") + " Star   " +
                   DeleteKey() + " Delete   " + KeyFor("keybind_left_shoulder", "1") + " " +
                   KeyFor("keybind_right_shoulder", "3") + " Mode   Esc Back"
             : "A Watch   Y Star   X Delete   LB RB Mode   B Back";
    line_font::Draw(draw, ImVec2(left, p1.y - pad - hint), hint, hints, Rgba(0.7f, 0.75f, 0.85f, 0.85f), hint_thin);
  }

  std::string DeleteKey() const { return KeyboardMode() ? KeyFor("keybind_x", "H") : "X"; }

  // While a replay plays: a small badge at the top, so a replay is never mistaken for a run.
  void DrawBadge(ImGuiIO& io) {
    const demo::ReplayStatus status = demo::Status();
    if (!status.watching) return;
    const float u = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f) * 9.0f / 16.0f / 100.0f;
    std::string text = "Replay";
    if (!status.player_name.empty()) text += "  " + status.player_name;
    if (status.out_of_step) {
      text += "  out of step from " + Clock2(float(status.out_of_step_tick) / 60.0f);
    } else if (status.stopped) {
      text += "  stopped: this round won't count";
    } else {
      text += KeyboardMode() ? "   Enter to stop" : "   Start to stop";
    }
    const float size = u * 2.2f;
    const float w = line_font::Width(text, size);
    const ImVec2 at((io.DisplaySize.x - w) * 0.5f, u * 2.0f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u),
                        Rgba(0.02f, 0.03f, 0.08f, 0.85f), u * 0.8f);
    draw->AddRect(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u), Theme(0.0, 0.8f),
                  u * 0.8f, 0, 1.5f);
    line_font::Draw(draw, at, size, text, Theme(), std::max(1.0f, size * 0.07f));
  }

  std::vector<library::Entry> entries_;
  int tab_ = -1;  // -1 is All
  int cursor_ = 0;
  int first_ = 0;
  bool confirm_delete_ = false;
  std::string message_;
};

}  // namespace

uint32_t WatchReplaysText() {
  static uint32_t text = 0;
  if (text == 0) {
    // A whole slot, as the game's are, in case anything writes into it.
    constexpr uint32_t kSlot = 520;
    text = rex::system::kernel_memory()->SystemHeapAlloc(kSlot);
    if (text == 0) return 0;
    uint8_t* host = rex::system::kernel_memory()->TranslateVirtual<uint8_t*>(text);
    std::memset(host, 0, kSlot);
    std::memcpy(host, "Watch replays", std::strlen("Watch replays"));
  }
  return text;
}

void CreateReplaysMenu(rex::ui::ImGuiDrawer* drawer) {
  new ReplaysMenu(drawer);  // retained by the drawer for the session
}

void AfterMenuRowAdded(const PPCContext& row, uint8_t* base) {
  if (row.r3.u32 != kMultiplayerTextId || row.r4.u32 != kMultiplayerAction) return;
  if (g_row_action == 0) {
    auto* state = rex::system::kernel_state();
    auto* dispatcher = state ? state->function_dispatcher() : nullptr;
    if (!dispatcher) return;
    g_row_action = dispatcher->AllocateThunk(RowAction, kMainMenu);
    if (g_row_action == 0) {
      REXLOG_WARN("Replays: the Watch replays row could not be added");
      return;
    }
  }
  PPCContext replays_row = row;
  replays_row.r3.u64 = kWatchReplaysTextId;
  replays_row.r4.u64 = g_row_action;
  __imp__sub_820900A8(replays_row, base);
}

void FilterPad(uint8_t* base, uint32_t user, uint32_t state) {
  if (state == 0) return;
  std::lock_guard lock(g_mutex);
  if (!g_input.HidesInput()) return;
  const uint32_t pad = state + kGamepadOffset;
  const Pad read{LoadBE16(base, pad), int16_t(LoadBE16(base, pad + 6)), int16_t(LoadBE16(base, pad + 10))};
  const ListInput::Verdict verdict = g_input.Poll(user, read);
  if (verdict.action != Action::kNone) g_actions.push_back(verdict.action);
  if (verdict.hide) std::memset(base + pad, 0, kGamepadSize);
}

}  // namespace gw2::replays_menu
