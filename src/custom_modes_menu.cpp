// Custom modes: the list the main menu's right panel opens (main_menu_panels.h), the title card that opens each round
// and the tag while it plays.
//
// The list reads the pad as Watch replays' does (replays_menu_rules.h): while it is open the game is given a neutral
// pad.
//
// The player's own modes first, then the examples, with the cursor on the top row: A plays, nothing to decide. A mode
// that can't be read stays in the list with its first error and the line it is on, so a typo is never silent. Y opens
// the mods folder (made with a copy of the template the first time). A best shows only once a mode has one.
// Sequence practice's row takes Left and Right for its level, and shows that level's best only.

#include "custom_modes_menu.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>
#include <trg/platform.h>

#include "custom_modes.h"
#include "guest_memory.h"
#include "launcher_platform.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"
#include "replays_menu.h"
#include "replays_menu_rules.h"

namespace gw2::custom_modes_menu {
namespace {

using namespace gw2::neon;
using replays_menu::Action;
using replays_menu::ListInput;
namespace rules = gw2::custom_mode;

constexpr uint32_t kGamepadOffset = 4;      // XINPUT_STATE: packet number, then the gamepad
constexpr uint32_t kGamepadSize = 12;
constexpr char kTitle[] = "Custom modes";

// The title card: in, held while the rules flash in, then out to the tag.
constexpr double kCardIn = 0.25;
constexpr double kCardHold = 2.0;
constexpr double kCardOut = 0.45;

std::mutex g_mutex;
ListInput g_input;
std::vector<Action> g_actions;  // from the game thread, applied as the list draws
bool g_opened = false;          // the list was opened and has not drawn yet

bool KeyboardMode() {
  return rex::cvar::GetFlagByName(kKeyboardKey) == "true";
}

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

std::string BaseName(uint32_t base) {
  return ModeName(base);
}

// A rounded chip with its text, from `x`; returns its width.
float DrawChip(ImDrawList* draw, ImVec2 at, float size, const std::string& text, ImU32 colour, float fill) {
  const float pad = size * 0.55f;
  const float w = line_font::GameWidth(text, size) + pad * 2.0f;
  const ImVec2 p0(at.x, at.y - size * 0.45f), p1(at.x + w, at.y + size * 1.45f);
  if (fill > 0.0f) draw->AddRectFilled(p0, p1, WithAlpha(colour, fill), size * 0.5f);
  draw->AddRect(p0, p1, colour, size * 0.5f, 0, std::max(1.0f, size * 0.08f));
  line_font::GameText(draw, ImVec2(at.x + pad, at.y), size, text, colour, std::max(1.0f, size * 0.07f));
  return w;
}

// The line font draws letters, digits and a little punctuation: quotes, question marks and brackets become plain text.
std::string Drawable(std::string text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '?') continue;
    if (c == '(') {
      if (!out.empty() && out.back() == ' ') out.pop_back();
      out += " - ";
      continue;
    }
    if (c == ')') continue;
    out += c;
  }
  return out;
}

std::string FirstProblem(const custom_modes::Entry& e) {
  if (e.errors.empty()) return {};
  const rules::Message& m = e.errors.front();
  std::string text = m.line ? std::format("Line {}: {}", m.line, m.text) : m.text;
  if (e.errors.size() > 1) text += std::format("   {} more", e.errors.size() - 1);
  return Drawable(text);
}

// The mods folder, made with the template the first time, so a new author starts from a file that works.
std::filesystem::path PrepareModsFolder() {
  const std::filesystem::path user = custom_modes::UserFolder();
  std::error_code error;
  std::filesystem::create_directories(user, error);
  const std::filesystem::path from =
      rex::filesystem::GetExecutableFolder() / "assets" / "modes" / "_template" / custom_modes::kModeFile;
  const std::filesystem::path to = user / "_template" / custom_modes::kModeFile;
  if (!std::filesystem::exists(to, error) && std::filesystem::exists(from, error)) {
    std::filesystem::create_directories(to.parent_path(), error);
    std::filesystem::copy_file(from, to, error);
  }
  return user;
}

class CustomModesMenu final : public rex::ui::ImGuiDialog {
 public:
  explicit CustomModesMenu(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::vector<Action> actions;
    bool open = false, opened = false;
    {
      std::lock_guard lock(g_mutex);
      actions.swap(g_actions);
      opened = g_opened;
      g_opened = false;
    }
    if (opened) Reload();
    for (const Action action : actions) Apply(action);
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (open) {
      DrawList(io);
      return;
    }
    DrawPlaying(io);
  }

 private:
  void Reload() {
    entries_ = custom_modes::Scan();
    cursor_ = 0;
    first_ = 0;
    message_.clear();
  }

  bool OnPractice() const {
    return cursor_ >= 0 && cursor_ < int(entries_.size()) && entries_[cursor_].built_in &&
           entries_[cursor_].folder == custom_modes::kPracticeFolder;
  }

  void Reopen() {
    std::lock_guard lock(g_mutex);
    g_input.SetOpen(true);
  }

  void Apply(Action action) {
    switch (action) {
      case Action::kUp:
        if (cursor_ > 0) --cursor_;
        break;
      case Action::kDown:
        if (cursor_ + 1 < int(entries_.size())) ++cursor_;
        break;
      case Action::kLeft:
      case Action::kRight:
        if (OnPractice()) {
          const int32_t step = action == Action::kLeft ? -1 : 1;
          custom_modes::SetPracticeLevel(std::clamp(custom_modes::PracticeLevel() + step, 1, rules::kSequenceLevels));
          entries_[cursor_] = custom_modes::PracticeEntry(custom_modes::PracticeLevel());
        }
        break;
      case Action::kStar: {  // Y: the mods folder
        const std::filesystem::path folder = PrepareModsFolder();
        trg::OpenInFileManager(Utf8(folder));
        message_ = "Opened your mods folder. Copy _template, rename the folder, and edit mode.toml.";
        break;
      }
      case Action::kWatch: {  // A: play
        if (cursor_ < 0 || cursor_ >= int(entries_.size())) {
          Reopen();
          break;
        }
        const custom_modes::Entry& e = entries_[cursor_];
        if (!e.mode) {
          message_ = e.newer_format ? "This mode needs a newer version of PC Plus." : "Fix this mode's file to play it.";
          Reopen();
          break;
        }
        custom_modes::Play(*e.mode);  // the list has closed; the round starts next frame
        break;
      }
      default:
        break;
    }
  }

  void DrawList(ImGuiIO& io) {
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    const ImVec2 p0(origin.x + cw * 0.12f, origin.y + ch * 0.10f);
    const ImVec2 p1(origin.x + cw * 0.88f, origin.y + ch * 0.92f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Rgba(0, 0, 0, 0.45f));
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 1.0f), u * 1.2f);
    draw->AddRect(p0, p1, Theme(0.0, 0.25f), u * 1.2f, 0, u * 0.9f);
    draw->AddRect(p0, p1, Theme(0.0, 0.9f), u * 1.2f, 0, u * 0.25f);
    const float pad = u * 3.0f;
    const float title = u * 4.6f;
    line_font::GameText(draw, ImVec2(p0.x + pad, p0.y + pad), title, kTitle, Theme(), std::max(1.2f, title * 0.06f));

    const float left = p0.x + pad, right = p1.x - pad;
    {
      const std::string note = "Custom runs never count for leaderboards, achievements or unlocks";
      const float size = u * 1.6f;
      line_font::GameText(draw, ImVec2(left, p0.y + pad + title + u * 1.4f), size, note,
                      Rgba(0.7f, 0.75f, 0.85f, 0.6f), std::max(1.0f, size * 0.07f));
    }
    const float list_top = p0.y + pad + title + u * 5.0f;
    const float footer = u * 13.0f;  // the selected mode's description, a message and the hints
    const float row_h = u * 7.2f;
    const int visible = std::max(1, int((p1.y - footer - list_top) / row_h));
    first_ = std::clamp(first_, std::max(0, cursor_ - visible + 1), cursor_);
    const float text = u * 2.5f;
    const float note_size = u * 1.7f;
    const float thin = std::max(1.0f, text * 0.07f);
    const float note_thin = std::max(1.0f, note_size * 0.07f);
    const ImU32 white = Rgba(1, 1, 1, 0.92f);
    const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
    const ImU32 red = Rgba(1.0f, 0.42f, 0.45f);
    const ImU32 amber = Rgba(1.0f, 0.82f, 0.35f);

    if (entries_.empty()) {
      line_font::GameText(draw, ImVec2(left, list_top + u * 2.0f), text, "No custom modes found.", white, thin);
      line_font::GameText(draw, ImVec2(left, list_top + u * 6.0f), note_size,
                      "Press Y to open your mods folder: copy _template and make one.", dim, note_thin);
    }
    for (int i = first_; i < int(entries_.size()) && i < first_ + visible; ++i) {
      const custom_modes::Entry& e = entries_[i];
      const float y = list_top + float(i - first_) * row_h;
      const ImVec2 r0(left, y), r1(right, y + row_h - u * 0.8f);
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
      const float x = left + u * 2.0f;
      const float top = y + u * 1.0f;
      if (!e.mode) {
        line_font::GameText(draw, ImVec2(x, top), text, e.folder, red, thin);
        line_font::GameText(draw, ImVec2(x, top + text + u * 1.2f), note_size, FirstProblem(e), red, note_thin);
        continue;
      }
      const rules::Mode& m = *e.mode;
      float name_w = line_font::GameText(draw, ImVec2(x, top), text, m.name, Theme(double(i) * 0.15), thin);
      std::string by = BaseName(m.rules.base);
      if (!m.author.empty()) by += "  by " + m.author;
      line_font::GameText(draw, ImVec2(x + name_w + u * 2.0f, top + (text - note_size) * 0.5f), note_size, by, dim, note_thin);
      if (e.best) {
        const std::string best = "Best " + custom_modes::ScoreText(m.rules, *e.best);
        line_font::GameText(draw, ImVec2(right - u * 2.0f - line_font::GameWidth(best, text), top), text, best, white, thin);
      }
      float cx = x;
      const float chip_y = top + text + u * 1.4f;
      for (const std::string& chip : rules::Chips(m.rules)) {
        cx += DrawChip(draw, ImVec2(cx, chip_y), note_size, chip, Theme(double(i) * 0.15, 0.95f), 0.10f) + u * 1.0f;
      }
      if (!e.warnings.empty()) {
        cx += DrawChip(draw, ImVec2(cx, chip_y), note_size, Drawable(e.warnings.front().text), amber, 0.0f) + u * 1.0f;
      }
    }

    // The selected mode's description, any message, and the hints.
    const float hint = u * 2.0f;
    const float hint_thin = std::max(1.0f, hint * 0.07f);
    float fy = p1.y - footer + u * 1.5f;
    if (cursor_ >= 0 && cursor_ < int(entries_.size())) {
      const custom_modes::Entry& e = entries_[cursor_];
      std::string line;
      if (e.mode) line = e.mode->description;
      if (e.mode && !e.warnings.empty() && line.empty()) line = std::format("Line {}: {}", e.warnings.front().line, e.warnings.front().text);
      if (!e.mode && e.errors.size() > 1) line = std::format("Line {}: {}", e.errors[1].line, e.errors[1].text);
      if (!line.empty()) line_font::GameText(draw, ImVec2(left, fy), note_size * 1.15f, Drawable(line), white, note_thin);
    }
    fy += u * 4.0f;
    if (!message_.empty()) line_font::GameText(draw, ImVec2(left, fy), hint, message_, amber, hint_thin);
    std::string hints = KeyboardMode() ? std::string(KeyFor("keybind_start", "Enter")) + " Play   " +
                                             KeyFor("keybind_y", "L") + " Mods folder   Esc Back"
                                       : "A Play   Y Mods folder   B Back";
    if (OnPractice()) hints = "Left Right Level   " + hints;
    line_font::GameText(draw, ImVec2(left, p1.y - pad - hint), hint, hints, dim, hint_thin);
  }

  // While a custom mode plays: its title card as each round begins, then a tag at the bottom.
  void DrawPlaying(ImGuiIO& io) {
    const std::optional<custom_modes::Current> current = custom_modes::Active();
    if (!current || current->round == 0) {
      card_round_ = 0;
      return;
    }
    const double now = Seconds();
    if (current->round != card_round_) {
      card_round_ = current->round;
      card_start_ = now;
    }
    if (!custom_modes::Ticking()) return;  // the menus, or the pause menu
    const float u = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f) * 9.0f / 16.0f / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const rules::Mode& m = current->mode;
    const double t = now - card_start_;
    if (t < kCardIn + kCardHold + kCardOut) {
      DrawCard(draw, io, u, m, current->replay, t);
      return;
    }
    DrawTag(draw, io, u, *current);
  }

  void DrawCard(ImDrawList* draw, ImGuiIO& io, float u, const rules::Mode& m, bool replay, double t) {
    const float in = float(std::clamp(t / kCardIn, 0.0, 1.0));
    const float out = float(std::clamp((t - kCardIn - kCardHold) / kCardOut, 0.0, 1.0));
    const float ease_in = 1.0f - (1.0f - in) * (1.0f - in);
    const float alpha = ease_in * (1.0f - out);
    const float cy = io.DisplaySize.y * (0.30f - 0.22f * out * out);  // above the ship as it spawns
    const float size = u * 9.0f * (1.25f - 0.25f * ease_in) * (1.0f - 0.5f * out);
    // A band across the screen, a flash as it lands.
    const float band = size * 2.6f;
    draw->AddRectFilled(ImVec2(0, cy - band * 0.5f), ImVec2(io.DisplaySize.x, cy + band * 0.5f),
                        Rgba(0.01f, 0.01f, 0.04f, 0.72f * alpha));
    draw->AddLine(ImVec2(0, cy - band * 0.5f), ImVec2(io.DisplaySize.x, cy - band * 0.5f), Theme(0.0, alpha), u * 0.3f);
    draw->AddLine(ImVec2(0, cy + band * 0.5f), ImVec2(io.DisplaySize.x, cy + band * 0.5f), Theme(0.3, alpha), u * 0.3f);
    const float flash = float(std::max(0.0, 1.0 - t / (kCardIn * 2.0)));
    if (flash > 0) draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Rgba(1, 1, 1, 0.18f * flash));
    const std::string& name = m.name;  // small capitals, as the game writes its own headings
    const float w = line_font::GameWidth(name, size);
    const ImVec2 at((io.DisplaySize.x - w) * 0.5f, cy - size * 0.75f);
    line_font::GameText(draw, ImVec2(at.x, at.y), size, name, Theme(0.0, alpha * 0.25f), std::max(2.0f, size * 0.11f));
    line_font::GameText(draw, at, size, name, Theme(0.0, alpha), std::max(1.5f, size * 0.07f));
    const float sub = size * 0.28f;
    std::string by = std::string(replay ? "Replay  " : "") + BaseName(m.rules.base);
    if (!m.author.empty()) by += "  by " + m.author;
    line_font::GameText(draw, ImVec2((io.DisplaySize.x - line_font::GameWidth(by, sub)) * 0.5f, cy + size * 0.45f), sub, by,
                    Rgba(1, 1, 1, 0.85f * alpha), std::max(1.0f, sub * 0.07f));
    // The rules flash in one by one.
    const auto chips = rules::Chips(m.rules);
    const float chip = size * 0.24f;
    float total = 0;
    for (const auto& c : chips) total += line_font::GameWidth(c, chip) + chip * 1.1f + u;
    float x = (io.DisplaySize.x - total) * 0.5f;
    for (size_t i = 0; i < chips.size(); ++i) {
      const double shown = t - kCardIn - 0.18 * double(i);
      if (shown < 0) break;
      const float pop = float(std::max(0.0, 1.0 - shown / 0.25));
      x += DrawChip(draw, ImVec2(x, cy + size * 0.95f), chip, chips[i], Theme(double(i) * 0.2, alpha), 0.15f + 0.6f * pop) + u;
    }
  }

  void DrawTag(ImDrawList* draw, ImGuiIO& io, float u, const custom_modes::Current& c) {
    std::string text = c.mode.name;
    if (c.replay) text = "Replay  " + text;
    if (c.mode.rules.score == rules::Score::kTime) {
      text += "   " + rules::TimeText(uint64_t(std::floor(c.elapsed * 100.0)));
    } else if (c.mode.rules.time_limit) {
      text += "   " + Clock2(float(std::max(0.0, double(*c.mode.rules.time_limit) - c.elapsed)) + 0.49f);
    }
    const float size = u * 2.2f;
    const float w = line_font::GameWidth(text, size);
    const ImVec2 at((io.DisplaySize.x - w) * 0.5f, io.DisplaySize.y - u * 5.0f);
    draw->AddRectFilled(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u),
                        Rgba(0.02f, 0.03f, 0.08f, 0.8f), u * 0.8f);
    draw->AddRect(ImVec2(at.x - u * 1.5f, at.y - u), ImVec2(at.x + w + u * 1.5f, at.y + size + u), Theme(0.0, 0.85f),
                  u * 0.8f, 0, 1.5f);
    line_font::GameText(draw, at, size, text, Theme(), std::max(1.0f, size * 0.07f));
  }

  std::vector<custom_modes::Entry> entries_;
  int cursor_ = 0;
  int first_ = 0;
  std::string message_;
  uint32_t card_round_ = 0;
  double card_start_ = 0;
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

void CreateCustomModesMenu(rex::ui::ImGuiDrawer* drawer) {
  new CustomModesMenu(drawer);  // retained by the drawer for the session
}

void FilterPad(uint8_t* base, uint32_t user, uint32_t state) {
  if (state == 0) return;
  std::lock_guard lock(g_mutex);
  if (!g_input.HidesInput()) return;
  const uint32_t pad = state + kGamepadOffset;
  const replays_menu::Pad read{LoadBE16(base, pad), int16_t(LoadBE16(base, pad + 6)), int16_t(LoadBE16(base, pad + 10)),
                               int16_t(LoadBE16(base, pad + 4)), int16_t(LoadBE16(base, pad + 8))};
  const ListInput::Verdict verdict = g_input.Poll(user, read);
  if (verdict.action != Action::kNone) g_actions.push_back(verdict.action);
  if (verdict.hide) std::memset(base + pad, 0, kGamepadSize);
}

}  // namespace gw2::custom_modes_menu
