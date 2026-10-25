// Watch replays: the list the main menu's left panel opens (main_menu_panels.h), and the badge while a replay plays.
//
// The list is drawn over the main menu; while it is open the game is given a neutral pad and the list reads the real
// one (replays_menu_rules.h), so a controller and the keyboard (through the game's keyboard-to-pad bindings) both work,
// and nothing reaches the menu behind.
//
// Newest first, on an All tab with the cursor on the top row: Enter or A watches, nothing to decide. Y stars a replay,
// which keeps it; X asks once, then deletes. Nothing counts replays or asks for tidying.
//
// With the online boards on, the last tab, Online, lists a board's top 50 (online_leaderboard.h). It opens on the board
// of the mode tab last looked at, with the cursor on the run just above yours when there is one to watch. LB and RB (1
// and 3), or left and right, step round the boards, each Master board beside its base mode, so none is more than four
// steps away; B (Esc) goes back to the player's own replays. A watches a run with a replay: it is downloaded, held in memory and played. Rows without one simply
// have no mark.

#include "replays_menu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
#include "online_leaderboard.h"
#include "replays_menu_rules.h"

namespace gw2::replays_menu {
namespace {

using namespace gw2::neon;
namespace library = gw2::demo_library;
namespace custom_mode = gw2::custom_mode;

constexpr uint32_t kGamepadOffset = 4;      // XINPUT_STATE: packet number, then the gamepad
constexpr uint32_t kGamepadSize = 12;

std::mutex g_mutex;
ListInput g_input;
std::vector<Action> g_actions;     // from the game thread, applied as the list draws
bool g_opened = false;             // the list was opened and has not drawn yet

constexpr int kOnlineTab = 100;
// Seconds a fetched board is shown before it is asked for again.
constexpr double kBoardFresh = 60.0;

// The Online tab's boards and download, filled by their threads (under g_online_mutex) and read as the list draws.
struct OnlineBoard {
  std::optional<std::vector<online::TopRun>> runs;
  bool loading = false;
  bool failed = false;
  double fetched_at = -1e9;
};
std::mutex g_online_mutex;
std::array<OnlineBoard, boards::kBoards> g_boards;
std::string g_downloading;  // the id being downloaded, or empty
std::optional<std::vector<uint8_t>> g_downloaded;
bool g_download_failed = false;

// Starts the download of a player's replay on a board; false when one is already under way.
bool StartDownload(uint32_t board, const std::string& id) {
  std::lock_guard lock(g_online_mutex);
  if (!g_downloading.empty()) return false;
  g_downloading = id;
  std::thread([board, id] {
    auto bytes = online::DownloadReplay(board, id);
    std::lock_guard lock(g_online_mutex);
    g_downloading.clear();
    g_download_failed = !bytes;
    g_downloaded = std::move(bytes);
  }).detach();
  return true;
}

double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void FetchBoard(uint32_t board, bool force) {
  {
    std::lock_guard lock(g_online_mutex);
    OnlineBoard& b = g_boards[board];
    if (b.loading || (!force && b.runs && Now() - b.fetched_at < kBoardFresh)) return;
    b.loading = true;
  }
  std::thread([board] {
    auto runs = online::FetchTopRuns(board);
    std::lock_guard lock(g_online_mutex);
    OnlineBoard& b = g_boards[board];
    b.loading = false;
    b.failed = !runs;
    if (runs) {
      b.runs = std::move(runs);
      b.fetched_at = Now();
    }
  }).detach();
}

// The order the boards are stepped through, round and round: each Master board beside its base mode.
constexpr std::array<uint32_t, boards::kBoards> kBoardRing = {
    boards::kMasterDeadlineBoard, 0, 1, boards::kMasterEvolvedBoard, 2, 3, 4, 5};

uint32_t StepBoard(uint32_t board, int step) {
  const auto at = std::find(kBoardRing.begin(), kBoardRing.end(), board);
  const int index = at == kBoardRing.end() ? 0 : int(at - kBoardRing.begin());
  const int n = int(kBoardRing.size());
  return kBoardRing[size_t(((index + step) % n + n) % n)];
}

const char* BoardName(uint32_t board) {
  if (board == boards::kMasterDeadlineBoard) return "Master Deadline";
  if (board == boards::kMasterEvolvedBoard) return "Master Evolved";
  return ModeName(board);
}

// The run just above the player's own on a board, the one to beat: its index, or -1.
int JustAbove(const std::vector<online::TopRun>& runs) {
  for (size_t i = 1; i < runs.size(); ++i) {
    if (runs[i].mine) return int(i) - 1;
  }
  return -1;
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
    TakeDownload();
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (open) {
      DrawList(io);
    } else {
      DrawBadge(io);
    }
    DrawSharedNotice(io);
    // The foreground list's indices are 16 bits: past 65536 vertices its triangles wrap into garbage.
    static bool warned = false;
    if (!warned && ImGui::GetForegroundDrawList()->VtxBuffer.Size > 60000) {
      warned = true;
      REXLOG_WARN("Replays: the overlay drew {} vertices, near ImGui's 65536 limit", ImGui::GetForegroundDrawList()->VtxBuffer.Size);
    }
  }

 private:
  // The tabs: All, then each mode that has a replay, then Online with the online boards on.
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
    if (online::Enabled()) tabs.push_back(kOnlineTab);
    return tabs;
  }

  bool OnOnline() const { return tab_ == kOnlineTab; }

  // The Online tab's rows for the board shown, as last fetched.
  std::vector<online::TopRun> Runs() const {
    std::lock_guard lock(g_online_mutex);
    return g_boards[board_].runs.value_or(std::vector<online::TopRun>{});
  }

  // A board shown afresh: fetched if it is old, the cursor on the run just above yours once it is in.
  void ShowBoard(uint32_t board) {
    board_ = board;
    cursor_ = 0;
    first_ = 0;
    place_cursor_ = true;
    message_.clear();
    FetchBoard(board_, false);
  }

  void TakeDownload() {
    std::optional<std::vector<uint8_t>> bytes;
    bool failed = false;
    {
      std::lock_guard lock(g_online_mutex);
      if (!g_downloading.empty()) return;
      bytes.swap(g_downloaded);
      failed = g_download_failed;
      g_download_failed = false;
    }
    bool open = false;
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (failed) {
      Say(open, "That replay could not be downloaded. Try again in a moment.");
      return;
    }
    if (!bytes) return;
    const demo::WatchResult result = demo::Watch(*bytes);
    if (result == demo::WatchResult::kStarted) {
      std::lock_guard lock(g_mutex);
      g_input.SetOpen(false);
      return;
    }
    Say(open, WatchMessage(result));
  }

  // In the list's message line while it is open, otherwise in a note over the game for a few seconds.
  void Say(bool open, std::string text) {
    if (open) {
      message_ = std::move(text);
    } else {
      note_ = std::move(text);
      note_until_ = Now() + 4.0;
    }
  }

  static std::string WatchMessage(demo::WatchResult result) {
    switch (result) {
      case demo::WatchResult::kUnreadable:
        return "That replay can't be read.";
      case demo::WatchResult::kOtherGame:
        return "Recorded on a different game version.";
      default:
        return "That replay was made with another version of PC Plus.";
    }
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
    if (OnOnline()) return;
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
    if (OnOnline()) {
      ApplyOnline(action);
      return;
    }
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
        const int from = tab_;
        tab_ = tabs[index];
        cursor_ = 0;
        if (OnOnline()) ShowBoard(from >= 0 ? uint32_t(from) : board_);
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
        message_ = WatchMessage(result);
        Reopen();
        break;
      }
      case Action::kLeft:
      case Action::kRight:
      case Action::kClose:
      case Action::kNone:
        break;
    }
  }

  void ApplyOnline(Action action) {
    const auto runs = Runs();
    switch (action) {
      case Action::kUp:
        if (cursor_ > 0) --cursor_;
        break;
      case Action::kDown:
        if (cursor_ + 1 < int(runs.size())) ++cursor_;
        break;
      case Action::kPreviousTab:
      case Action::kLeft:
        ShowBoard(StepBoard(board_, -1));
        break;
      case Action::kNextTab:
      case Action::kRight:
        ShowBoard(StepBoard(board_, 1));
        break;
      case Action::kClose:  // back to the player's own replays, the list still open
        tab_ = -1;
        cursor_ = 0;
        first_ = 0;
        message_.clear();
        Reload(false);
        Reopen();
        break;
      case Action::kWatch: {
        Reopen();  // the list stays open while the replay downloads
        if (cursor_ >= int(runs.size()) || runs[cursor_].game.empty()) break;
        if (StartDownload(board_, runs[cursor_].id)) message_.clear();
        break;
      }
      case Action::kStar:
      case Action::kDelete:
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
    line_font::GameText(draw, ImVec2(p0.x + pad, p0.y + pad), title, "Watch replays", Theme(), std::max(1.2f, title * 0.06f));

    // Tabs.
    const float tab_size = u * 2.4f;
    const float thin = std::max(1.0f, tab_size * 0.07f);
    float x = p0.x + pad;
    const float tab_y = p0.y + pad + title + u * 2.0f;
    for (const int tab : Tabs()) {
      const std::string name = tab < 0 ? "All" : tab == kOnlineTab ? "Online" : ModeName(uint32_t(tab));
      const bool current = tab == tab_;
      const float w = line_font::GameWidth(name, tab_size);
      if (current) {
        draw->AddRectFilled(ImVec2(x - u * 0.8f, tab_y - u * 0.7f), ImVec2(x + w + u * 0.8f, tab_y + tab_size + u * 0.7f),
                            Theme(0.0, 0.18f), u * 0.5f);
      }
      line_font::GameText(draw, ImVec2(x, tab_y), tab_size, name, current ? Theme() : Rgba(0.7f, 0.75f, 0.85f, 0.8f), thin);
      x += w + u * 3.0f;
    }

    const float list_top = tab_y + tab_size + u * 3.0f;
    const float footer = u * 6.0f;
    const float row_h = u * 5.4f;
    if (OnOnline()) {
      DrawOnline(io, draw, u, p0, p1, list_top, footer, row_h);
      return;
    }

    // Rows.
    const auto shown = Shown();
    const int visible = std::max(1, int((p1.y - footer - list_top) / row_h));
    first_ = std::clamp(first_, std::max(0, cursor_ - visible + 1), cursor_);
    const float text = row_h * 0.42f;
    const float row_thin = std::max(1.0f, text * 0.07f);
    if (shown.empty()) {
      const char* empty = entries_.empty() ? "No replays yet. Finish a run and it'll be waiting here."
                                           : "No replays in this mode yet.";
      line_font::GameText(draw, ImVec2(p0.x + pad, list_top + u * 2.0f), text, empty, Rgba(0.8f, 0.85f, 0.95f, 0.9f),
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
        line_font::GameText(draw, ImVec2(left + u * 4.5f, mid - text * 0.5f), text,
                        std::string("Delete this replay? Press ") + DeleteKey() + " again", Rgba(1, 0.85f, 0.6f),
                        row_thin);
        continue;
      }
      const std::string mode = e.custom_name.empty() ? std::string(ModeName(e.mode)) : e.custom_name;
      line_font::GameText(draw, ImVec2(left + u * 4.5f, mid - text * 0.5f), text, mode, Theme(), row_thin);
      const auto custom = e.custom_name.empty() ? std::nullopt : custom_mode::Read(e.custom_rules);
      const std::string score = custom ? custom_modes::ScoreText(*custom, e.score) : Grouped(int64_t(e.score));
      const float score_right = left + (right - left) * 0.58f;
      line_font::GameText(draw, ImVec2(score_right - line_font::GameWidth(score, text), mid - text * 0.5f), text, score, white,
                      row_thin);
      line_font::GameText(draw, ImVec2(left + (right - left) * 0.64f, mid - text * 0.5f), text, When(e.recorded_at), dim,
                      row_thin);
      const std::string length = Clock2(float(e.ticks) / 60.0f);
      line_font::GameText(draw, ImVec2(right - u * 1.5f - line_font::GameWidth(length, text), mid - text * 0.5f), text, length,
                      dim, row_thin);
    }

    // Message and hints.
    const float hint = u * 2.0f;
    const float hint_thin = std::max(1.0f, hint * 0.07f);
    if (!message_.empty()) {
      line_font::GameText(draw, ImVec2(left, p1.y - footer - hint * 0.6f), hint, message_, Rgba(1, 0.85f, 0.6f), hint_thin);
    }
    const bool keys = KeyboardMode();
    const std::string hints =
        keys ? std::string(KeyFor("keybind_start", "Enter")) + " Watch   " + KeyFor("keybind_y", "L") + " Star   " +
                   DeleteKey() + " Delete   " + KeyFor("keybind_left_shoulder", "1") + " " +
                   KeyFor("keybind_right_shoulder", "3") + " Mode   Esc Back"
             : "A Watch   Y Star   X Delete   LB RB Mode   B Back";
    line_font::GameText(draw, ImVec2(left, p1.y - pad - hint), hint, hints, Rgba(0.7f, 0.75f, 0.85f, 0.85f), hint_thin);
  }

  std::string DeleteKey() const { return KeyboardMode() ? KeyFor("keybind_x", "H") : "X"; }

  void DrawOnline(ImGuiIO& io, ImDrawList* draw, float u, ImVec2 p0, ImVec2 p1, float list_top, float footer,
                  float row_h) {
    const float pad = u * 3.0f;
    const float left = p0.x + pad, right = p1.x - pad;
    std::vector<online::TopRun> runs;
    bool loading = false, failed = false;
    std::string downloading;
    {
      std::lock_guard lock(g_online_mutex);
      const OnlineBoard& b = g_boards[board_];
      runs = b.runs.value_or(std::vector<online::TopRun>{});
      loading = b.loading;
      failed = b.failed && !b.runs;
      downloading = g_downloading;
    }
    if (place_cursor_ && !loading) {
      // Opened on the run just above yours when it can be watched, the one to beat; otherwise the top.
      const int above = JustAbove(runs);
      cursor_ = above >= 0 && !runs[above].game.empty() ? above : 0;
      place_cursor_ = false;
    }
    cursor_ = std::clamp(cursor_, 0, std::max(0, int(runs.size()) - 1));

    // The board in the middle, the ones a step either side beside it, and the keys that step.
    const float board_size = u * 2.8f;
    const float side = board_size * 0.62f;
    const float thin = std::max(1.0f, board_size * 0.07f);
    const float mid_x = (left + right) * 0.5f;
    const float by = list_top - u * 0.6f;
    const std::string board = BoardName(board_);
    const float bw = line_font::GameWidth(board, board_size);
    line_font::GameText(draw, ImVec2(mid_x - bw * 0.5f, by), board_size, board, Theme(), thin);
    const ImU32 beside = Rgba(0.7f, 0.75f, 0.85f, 0.75f);
    const bool keys = KeyboardMode();
    const std::string before = std::string(keys ? KeyFor("keybind_left_shoulder", "1") : "LB") + "   " +
                               BoardName(StepBoard(board_, -1));
    const std::string after = std::string(BoardName(StepBoard(board_, 1))) + "   " +
                              (keys ? KeyFor("keybind_right_shoulder", "3") : "RB");
    const float sy = by + (board_size - side) * 0.5f;
    const float gap = u * 4.0f;
    line_font::GameText(draw, ImVec2(mid_x - bw * 0.5f - gap - line_font::GameWidth(before, side), sy), side, before, beside, thin);
    line_font::GameText(draw, ImVec2(mid_x + bw * 0.5f + gap, sy), side, after, beside, thin);
    const float ay = by + board_size * 0.5f;
    const float aw = u * 1.0f;
    draw->AddTriangleFilled(ImVec2(mid_x - bw * 0.5f - gap * 0.5f - aw * 0.5f, ay),
                            ImVec2(mid_x - bw * 0.5f - gap * 0.5f + aw * 0.5f, ay - aw),
                            ImVec2(mid_x - bw * 0.5f - gap * 0.5f + aw * 0.5f, ay + aw), Theme());
    draw->AddTriangleFilled(ImVec2(mid_x + bw * 0.5f + gap * 0.5f + aw * 0.5f, ay),
                            ImVec2(mid_x + bw * 0.5f + gap * 0.5f - aw * 0.5f, ay - aw),
                            ImVec2(mid_x + bw * 0.5f + gap * 0.5f - aw * 0.5f, ay + aw), Theme());

    const float top = list_top + board_size + u * 1.8f;
    const int visible = std::max(1, int((p1.y - footer - top) / row_h));
    first_ = std::clamp(first_, std::max(0, cursor_ - visible + 1), cursor_);
    const float text = row_h * 0.42f;
    const float row_thin = std::max(1.0f, text * 0.07f);
    const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
    if (runs.empty()) {
      const char* empty = loading  ? "Fetching the board..."
                          : failed ? "The online boards can't be reached right now."
                                   : "Nobody has a score on this board yet.";
      line_font::GameText(draw, ImVec2(left, top + u * 2.0f), text, empty, Rgba(0.8f, 0.85f, 0.95f, 0.9f), row_thin);
    }
    const int above = JustAbove(runs);
    for (int i = first_; i < int(runs.size()) && i < first_ + visible; ++i) {
      const online::TopRun& run = runs[i];
      const float y = top + float(i - first_) * row_h;
      const ImVec2 r0(left, y), r1(right, y + row_h - u * 0.6f);
      const bool hovered = ImGui::IsMouseHoveringRect(r0, r1, false);
      if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) cursor_ = i;
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        cursor_ = i;
        Apply(Action::kWatch);
      }
      const bool current = i == cursor_;
      if (current) {
        draw->AddRectFilled(r0, r1, Theme(0.0, 0.12f), u * 0.6f);
        draw->AddRect(r0, r1, Theme(0.0, 0.8f), u * 0.6f, 0, 1.5f);
      }
      const float mid = y + (row_h - u * 0.6f) * 0.5f;
      const float ty = mid - text * 0.5f;
      if (!run.game.empty()) DrawPlay(draw, ImVec2(left + u * 2.0f, mid), u * 1.1f, Theme());
      const std::string rank = std::to_string(run.rank);
      line_font::GameText(draw, ImVec2(left + u * 7.5f - line_font::GameWidth(rank, text), ty), text, rank, dim, row_thin);
      line_font::GameText(draw, ImVec2(left + u * 9.5f, ty), text, run.name,
                      run.mine ? Theme() : Rgba(1, 1, 1, 0.92f), row_thin);
      const std::string score = Grouped(run.score);
      const float score_right = left + (right - left) * 0.68f;
      line_font::GameText(draw, ImVec2(score_right - line_font::GameWidth(score, text), ty), text, score,
                      Rgba(1, 1, 1, 0.92f), row_thin);
      const char* note = run.id == downloading ? "Downloading..."
                         : run.mine            ? "You"
                         : i == above          ? "Just above you"
                                               : "";
      if (*note) {
        line_font::GameText(draw, ImVec2(right - u * 1.5f - line_font::GameWidth(note, text * 0.85f), ty + text * 0.08f),
                        text * 0.85f, note, i == above || run.mine ? Theme() : dim, row_thin);
      }
    }

    const float hint = u * 2.0f;
    const float hint_thin = std::max(1.0f, hint * 0.07f);
    if (!message_.empty()) {
      line_font::GameText(draw, ImVec2(left, p1.y - footer - hint * 0.6f), hint, message_, Rgba(1, 0.85f, 0.6f), hint_thin);
    }
    const std::string hints =
        KeyboardMode() ? std::string(KeyFor("keybind_start", "Enter")) + " Watch   " +
                             KeyFor("keybind_left_shoulder", "1") + " " + KeyFor("keybind_right_shoulder", "3") +
                             " Board   Esc My replays"
                       : "A Watch   LB RB Board   B My replays";
    line_font::GameText(draw, ImVec2(left, p1.y - pad - hint), hint, hints, Rgba(0.7f, 0.75f, 0.85f, 0.85f), hint_thin);
  }

  // A small play mark: the row's replay can be watched.
  static void DrawPlay(ImDrawList* draw, ImVec2 centre, float r, ImU32 colour) {
    draw->AddTriangleFilled(ImVec2(centre.x - r * 0.6f, centre.y - r), ImVec2(centre.x - r * 0.6f, centre.y + r),
                            ImVec2(centre.x + r, centre.y), colour);
  }

  // Notes over the game: once ever, as the first replay of the player's is shared, who can watch it and where to turn
  // it off; and, from the game's leaderboard screen, a replay downloading or what went wrong.
  void DrawSharedNotice(ImGuiIO& io) {
    if (online::TakeSharedNotice()) notice_until_ = Now() + 9.0;
    bool downloading = false, open = false;
    {
      std::lock_guard lock(g_online_mutex);
      downloading = !g_downloading.empty();
    }
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (Now() <= notice_until_) {
      DrawNote(io, "Your run made the top 50, so others can watch its replay now.",
               "Share my top runs in the launcher turns this off.");
    } else if (downloading && !open) {
      DrawNote(io, "Downloading the replay...", "");
    } else if (Now() <= note_until_) {
      DrawNote(io, note_.c_str(), "");
    }
  }

  void DrawNote(ImGuiIO& io, const char* first, const char* second) {
    const float u = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f) * 9.0f / 16.0f / 100.0f;
    const float size = u * 2.0f;
    const bool two = *second != 0;
    const float w = std::max(line_font::GameWidth(first, size), two ? line_font::GameWidth(second, size) : 0.0f);
    const ImVec2 at((io.DisplaySize.x - w) * 0.5f, io.DisplaySize.y - u * 14.0f);
    const ImVec2 b0(at.x - u * 1.5f, at.y - u), b1(at.x + w + u * 1.5f, at.y + size * (two ? 2.0f : 1.0f) + u * (two ? 2.0f : 1.0f));
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(b0, b1, Rgba(0.02f, 0.03f, 0.08f, 0.9f), u * 0.8f);
    draw->AddRect(b0, b1, Theme(0.0, 0.8f), u * 0.8f, 0, 1.5f);
    line_font::GameText(draw, at, size, first, Theme(), std::max(1.0f, size * 0.07f));
    if (two) {
      line_font::GameText(draw, ImVec2(at.x, at.y + size + u), size, second, Rgba(0.75f, 0.8f, 0.9f, 0.9f),
                      std::max(1.0f, size * 0.07f));
    }
  }

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
    const float w = line_font::GameWidth(text, size);
    const ImVec2 at((io.DisplaySize.x - w) * 0.5f, u * 2.0f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u),
                        Rgba(0.02f, 0.03f, 0.08f, 0.85f), u * 0.8f);
    draw->AddRect(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u), Theme(0.0, 0.8f),
                  u * 0.8f, 0, 1.5f);
    line_font::GameText(draw, at, size, text, Theme(), std::max(1.0f, size * 0.07f));
  }

  std::vector<library::Entry> entries_;
  int tab_ = -1;  // -1 is All
  int cursor_ = 0;
  int first_ = 0;
  bool confirm_delete_ = false;
  std::string message_;
  uint32_t board_ = 0;          // the Online tab's board
  bool place_cursor_ = false;   // the board was just shown: the cursor goes to the run just above yours
  double notice_until_ = 0;
  std::string note_;
  double note_until_ = 0;
};

}  // namespace

void Open() {
  std::lock_guard lock(g_mutex);
  g_input.SetOpen(true);
  g_actions.clear();
  g_opened = true;
}

bool IsOpen() {
  std::lock_guard lock(g_mutex);
  return g_input.open();
}

bool WatchOnline(uint32_t board, const std::string& id) {
  return StartDownload(board, id);
}

void CreateReplaysMenu(rex::ui::ImGuiDrawer* drawer) {
  new ReplaysMenu(drawer);  // retained by the drawer for the session
}

void FilterPad(uint8_t* base, uint32_t user, uint32_t state) {
  if (state == 0) return;
  std::lock_guard lock(g_mutex);
  if (!g_input.HidesInput()) return;
  const uint32_t pad = state + kGamepadOffset;
  const Pad read{LoadBE16(base, pad), int16_t(LoadBE16(base, pad + 6)), int16_t(LoadBE16(base, pad + 10)),
                 int16_t(LoadBE16(base, pad + 4)), int16_t(LoadBE16(base, pad + 8))};
  const ListInput::Verdict verdict = g_input.Poll(user, read);
  if (verdict.action != Action::kNone) g_actions.push_back(verdict.action);
  if (verdict.hide) std::memset(base + pad, 0, kGamepadSize);
}

}  // namespace gw2::replays_menu
