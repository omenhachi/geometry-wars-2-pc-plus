#include "mode_builder.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <imgui_internal.h>

#include "key_hints.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"

namespace gw2::mode_builder {
namespace {

using namespace gw2::neon;
using replays_menu::Action;
namespace rules = gw2::custom_mode;

constexpr size_t kMaxName = 40;  // fits the list's row beside the base mode and the best

// The line font draws printable ASCII; a typed name keeps to it.
bool Typeable(ImWchar c) {
  return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\';
}

// The first key a keyboard binding names, for the hints.
std::string KeyName(const char* binding, const char* fallback) {
  std::string keys = rex::cvar::GetFlagByName(binding);
  if (keys.empty()) return fallback;
  return keys.substr(0, keys.find(','));
}

std::vector<std::string> Folders(const std::vector<custom_modes::Entry>& entries) {
  std::vector<std::string> folders;
  for (const custom_modes::Entry& e : entries) {
    if (!e.built_in) folders.push_back(e.folder);
  }
  // Folders the list leaves out (names starting with _ or .) still exist on disk.
  std::error_code error;
  for (const auto& item : std::filesystem::directory_iterator(custom_modes::UserFolder(), error)) {
    folders.push_back(item.path().filename().string());
  }
  return folders;
}

}  // namespace

Screen::Screen(const rules::Mode& from, std::string folder, std::vector<custom_modes::Entry> entries, bool fresh_name)
    : builder_(from), folder_(std::move(folder)), entries_(std::move(entries)), bests_(custom_modes::SavedBests()) {
  if (fresh_name || builder_.name().empty()) builder_.Roll(+1);
  // A copy is the player's own: their name as its author.
  if (folder_.empty()) {
    const rules::Mode m = builder_.Mode();
    rules::Mode with_author = m;
    with_author.author = rex::cvar::GetFlagByName(kPlayerNameKey);
    const std::string name = builder_.name();
    builder_ = Builder(with_author);
    builder_.set_name(name);
  }
}

std::vector<RowRef> Screen::CurrentRows() const {
  return builder_.Rows(tab_);
}

// The Rules tab ends with "Delete this mode" for a builder file.
bool Screen::OnDeleteRow() const {
  return tab_ == Tab::kRules && !folder_.empty() && row_ == int(CurrentRows().size());
}

void Screen::Changed() {
  dirty_ = true;
  delete_armed_ = false;
  message_.clear();
  pop_ = Seconds();
}

bool Screen::Save() {
  if (!dirty_) return true;
  const rules::Mode mode = builder_.Mode();
  const std::string text = rules::WriteModeFile(mode, true);
  if (!rules::Parse(text).mode) {
    message_ = "This mode can't be saved yet.";
    return false;
  }
  if (folder_.empty()) folder_ = FolderFor(mode.name, Folders(entries_));
  const std::filesystem::path dir = custom_modes::UserFolder() / folder_;
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const std::filesystem::path file = dir / custom_modes::kModeFile;
  const std::filesystem::path temp = dir / (std::string(custom_modes::kModeFile) + ".tmp");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) {
      message_ = "Couldn't save the mode: is the mods folder writable?";
      return false;
    }
  }
  std::filesystem::rename(temp, file, error);
  if (error) {
    message_ = "Couldn't save the mode: is the mods folder writable?";
    return false;
  }
  REXLOG_INFO("Mode builder: saved \"{}\" to mods/{}", mode.name, folder_);
  dirty_ = false;
  return true;
}

Outcome Screen::Apply(Action action, bool keyboard) {
  if (typing_) return {};
  if (action == Action::kWatch && keyboard && StartTyping()) return {};
  const auto rows = CurrentRows();
  const int count = int(rows.size()) + (tab_ == Tab::kRules && !folder_.empty() ? 1 : 0);
  if (action != Action::kWatch) delete_armed_ = false;
  switch (action) {
    case Action::kUp:
      if (row_ > 0) --row_;
      break;
    case Action::kDown:
      if (row_ + 1 < count) ++row_;
      break;
    case Action::kPreviousTab:
    case Action::kNextTab:
      tab_ = tab_ == Tab::kRules ? Tab::kEnemies : Tab::kRules;
      row_ = 0;
      break;
    case Action::kLeft:
    case Action::kRight:
      if (row_ < int(rows.size())) {
        builder_.Step(rows[size_t(row_)], action == Action::kLeft ? -1 : 1);
        Changed();
      }
      break;
    case Action::kDelete:  // X: back to the game's own
      if (row_ < int(rows.size()) && !builder_.View(rows[size_t(row_)]).own) {
        builder_.Reset(rows[size_t(row_)]);
        Changed();
      }
      break;
    case Action::kWatch: {  // A: play, or on the delete row, delete (twice)
      if (OnDeleteRow()) {
        if (!delete_armed_) {
          delete_armed_ = true;
          message_ = "Press A again to delete this mode for good.";
          return {};
        }
        std::error_code error;
        std::filesystem::remove_all(custom_modes::UserFolder() / folder_, error);
        REXLOG_INFO("Mode builder: deleted mods/{}", folder_);
        Outcome out{Outcome::kBack, std::nullopt, {}, error ? "Couldn't delete the mode's folder." : "Mode deleted."};
        return out;
      }
      if (!Save()) return {};
      return {Outcome::kPlay, builder_.Mode(), folder_, {}};
    }
    case Action::kClose: {  // B: save and go back
      if (!Save()) return {};
      return {Outcome::kBack, std::nullopt, folder_, {}};
    }
    default:
      break;
  }
  return {};
}

Outcome Screen::Draw(ImGuiIO& io, bool keyboard) {
  Outcome outcome;
  // Typing a name: every key is a letter until Enter keeps it or Esc puts the old one back.
  if (typing_) {
    if (!ImGui::IsKeyDown(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiKey_KeypadEnter)) enter_released_ = true;
    for (const ImWchar c : io.InputQueueCharacters) {
      if (Typeable(c) && typed_.size() < kMaxName) typed_ += char(c);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !typed_.empty()) typed_.pop_back();
    if (enter_released_ && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
      typing_ = false;
      std::string name = typed_;
      while (!name.empty() && name.back() == ' ') name.pop_back();
      while (!name.empty() && name.front() == ' ') name.erase(name.begin());
      if (!name.empty() && name != builder_.name()) {
        builder_.set_name(name);
        Changed();
      }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      typing_ = false;
    }
  }

  const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
  const float ch = cw * 9.0f / 16.0f;
  const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
  const float u = ch / 100.0f;
  const ImVec2 p0(origin.x + cw * 0.12f, origin.y + ch * 0.07f);
  const ImVec2 p1(origin.x + cw * 0.88f, origin.y + ch * 0.95f);
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Rgba(0, 0, 0, 0.45f));
  draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 1.0f), u * 1.2f);
  TileGrid(draw, p0, p1, u);
  draw->AddRect(p0, p1, Theme(0.0, 0.25f), u * 1.2f, 0, u * 0.9f);
  draw->AddRect(p0, p1, Theme(0.0, 0.9f), u * 1.2f, 0, u * 0.25f);

  const float pad = u * 3.0f;
  const float left = p0.x + pad, right = p1.x - pad;
  const ImU32 white = Rgba(1, 1, 1, 0.92f);
  const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
  const ImU32 amber = Rgba(1.0f, 0.82f, 0.35f);
  const auto text = [&](ImVec2 at, float size, const std::string& s, ImU32 colour) {
    return line_font::GameText(draw, at, size, s, colour, std::max(1.0f, size * 0.07f));
  };

  // The title, and the two tabs with LB and RB between them.
  const float title = u * 4.2f;
  line_font::GameText(draw, ImVec2(left, p0.y + pad), title, "Mode builder", Theme(), std::max(1.2f, title * 0.06f));
  const float tab_size = u * 2.4f;
  float tx = right;
  const char* tab_names[] = {"Rules", "Enemies"};
  for (int t = 1; t >= 0; --t) {
    const bool on = int(tab_) == t;
    const float w = line_font::GameWidth(tab_names[t], tab_size) + u * 3.0f;
    tx -= w;
    const ImVec2 a(tx, p0.y + pad), b(tx + w - u * 0.8f, p0.y + pad + tab_size + u * 1.6f);
    if (on) draw->AddRectFilled(a, b, Theme(0.0, 0.22f), u * 0.5f);
    draw->AddRect(a, b, Theme(0.0, on ? 0.95f : 0.35f), u * 0.5f, 0, on ? 2.0f : 1.0f);
    text(ImVec2(a.x + u * 1.1f, a.y + u * 0.8f), tab_size, tab_names[t], on ? white : dim);
    if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !on) {
      tab_ = Tab(t);
      row_ = 0;
    }
  }
  (void)tx;

  // The rows.
  const auto rows = CurrentRows();
  const bool delete_row = tab_ == Tab::kRules && !folder_.empty();
  const int count = int(rows.size()) + (delete_row ? 1 : 0);
  row_ = std::clamp(row_, 0, std::max(0, count - 1));
  const float list_top = p0.y + pad + title + u * 4.0f;
  const float row_h = u * 4.2f;
  const float size = u * 2.2f;
  const float value_x = left + (right - left) * 0.42f;
  if (rows.empty() && tab_ == Tab::kEnemies) {
    text(ImVec2(left + u * 2.0f, list_top + u * 1.5f), size, builder_.EnemiesReason(), dim);
  }
  for (int i = 0; i < count; ++i) {
    const float y = list_top + float(i) * row_h;
    const ImVec2 r0(left, y), r1(right, y + row_h - u * 0.6f);
    const bool current = i == row_;
    const bool hovered = ImGui::IsMouseHoveringRect(r0, r1, false);
    if (hovered && !typing_ && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) row_ = i;
    if (current) {
      draw->AddRectFilled(r0, r1, Theme(0.0, 0.12f), u * 0.5f);
      draw->AddRect(r0, r1, Theme(0.0, 0.8f), u * 0.5f, 0, 1.5f);
    }
    const float ty = y + (row_h - u * 0.6f - size) * 0.5f;
    if (i == int(rows.size())) {  // the delete row
      text(ImVec2(left + u * 2.0f, ty), size, delete_armed_ ? "Press A again to delete" : "Delete this mode",
           Rgba(1.0f, 0.42f, 0.45f));
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) outcome = Apply(Action::kWatch, false);
      continue;
    }
    const RowRef ref = rows[size_t(i)];
    RowView v = builder_.View(ref);
    if (ref.row == Row::kName && typing_) v.value = typed_ + (std::fmod(Seconds(), 1.0) < 0.5 ? "_" : " ");
    text(ImVec2(left + u * 2.0f, ty), size, v.label, current ? white : dim);
    // The value between its arrows, bright when it's this mode's own and dim when it's the game's.
    const float vw = line_font::GameWidth(v.value, size);
    const ImU32 value_colour = typing_ && ref.row == Row::kName ? amber : v.own ? dim : Theme(double(i) * 0.1);
    if (current && !typing_) {
      const float ay = ty + size * 0.5f, ar = size * 0.35f;
      draw->AddTriangleFilled(ImVec2(value_x - u * 2.2f, ay), ImVec2(value_x - u * 1.2f, ay - ar),
                              ImVec2(value_x - u * 1.2f, ay + ar), Theme());
      const float rx = value_x + vw + u * 1.2f;
      draw->AddTriangleFilled(ImVec2(rx + u * 1.0f, ay), ImVec2(rx, ay - ar), ImVec2(rx, ay + ar), Theme());
    }
    text(ImVec2(value_x, ty), size, v.value, value_colour);
    if (!v.note.empty()) {
      const float ns = u * 1.6f;
      text(ImVec2(right - u * 1.5f - line_font::GameWidth(v.note, ns), ty + (size - ns) * 0.5f), ns, v.note, amber);
    }
    // The mouse: a click on the value side changes it (right half forward, left of it back).
    if (hovered && !typing_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      row_ = i;
      builder_.Step(ref, io.MousePos.x < value_x ? -1 : 1);
      Changed();
    }
  }

  // The footer, from the bottom up: the key hints, then the mode as it stands (its chips, popping when something
  // changes), its best, and what the file check says, each wrapped to the panel.
  std::vector<key_hints::Hint> hints;
  const bool name_row = tab_ == Tab::kRules && row_ < int(rows.size()) && rows[size_t(row_)].row == Row::kName;
  if (typing_) {
    hints = {{"Enter", "Keep this name"}, {"Esc", "Cancel"}, {"Backspace", "Delete a letter"}};
  } else if (keyboard) {
    hints.push_back(name_row ? key_hints::Hint{"Enter", "Type a name"} : key_hints::Hint{"Enter", "Play"});
    hints.push_back({"<>", name_row ? "New name" : "Change"});
    hints.push_back({KeyName("keybind_left_shoulder", "1") + " " + KeyName("keybind_right_shoulder", "3"), "Tabs"});
    hints.push_back({KeyName("keybind_x", "H"), "Game's own"});
    hints.push_back({"Esc", "Save and back"});
  } else {
    hints = {{"A", "Play"}, {"<>", name_row ? "New name" : "Change"}, {"LB RB", "Tabs"}, {"X", "Game's own"},
             {"B", "Save and back"}};
  }
  const float hint = u * 1.7f;
  float fy = p1.y - pad - key_hints::Draw(draw, hints, left, right, p1.y - pad, hint) - u * 2.2f;

  const rules::Mode mode = builder_.Mode();
  const rules::ParseResult check = rules::Parse(rules::WriteModeFile(mode, true));
  const float note = u * 1.75f;
  const auto lines_up = [&](const std::string& s, ImU32 colour) {
    const auto lines = key_hints::Wrap(s, note, right - left, 2);
    for (size_t i = lines.size(); i-- > 0;) {
      fy -= note + u * 0.9f;
      text(ImVec2(left, fy), note, lines[i], colour);
    }
  };
  if (!message_.empty()) {
    lines_up(message_, amber);
  } else if (!check.warnings.empty()) {
    lines_up(check.warnings.front().text, amber);
  }
  const std::string id = rules::Id(mode.rules);
  const auto best = bests_.find(id);
  lines_up(best != bests_.end() ? "Best for these rules: " + custom_modes::ScoreText(mode.rules, best->second)
                                : std::string("Best for these rules: none yet"),
           white);
  for (const custom_modes::Entry& e : entries_) {
    if (e.mode && e.id == id && e.folder != folder_) {
      lines_up("Same rules as " + e.mode->name + ", so it shares its best", dim);
      break;
    }
  }

  // The chips, laid out first so their rows can sit above the best line.
  const float chip = u * 1.7f;
  const float pop = float(std::max(0.0, 1.0 - (Seconds() - pop_) / 0.3));
  const auto chips = rules::Chips(mode.rules);
  std::vector<std::vector<size_t>> chip_rows(1);
  float cx = left;
  for (size_t i = 0; i < chips.size(); ++i) {
    const float w = line_font::GameWidth(chips[i], chip) + chip * 1.1f;
    if (cx > left && cx + w > right) {
      chip_rows.emplace_back();
      cx = left;
    }
    chip_rows.back().push_back(i);
    cx += w + u;
  }
  const float chip_h = chip * 2.4f;
  fy -= u * 1.4f + chip_h * float(chip_rows.size());
  if (chips.empty()) text(ImVec2(left, fy + chip * 0.45f), chip, "Plays exactly as " + std::string(ModeName(mode.rules.base)), dim);
  for (size_t r = 0; r < chip_rows.size(); ++r) {
    cx = left;
    const float cy = fy + chip * 0.45f + chip_h * float(r);
    for (const size_t i : chip_rows[r]) {
      const float w = line_font::GameWidth(chips[i], chip) + chip * 1.1f;
      const ImVec2 a(cx, cy - chip * 0.45f), b(cx + w, cy + chip * 1.45f);
      draw->AddRectFilled(a, b, Theme(double(i) * 0.2, 0.10f + 0.5f * pop), chip * 0.5f);
      draw->AddRect(a, b, Theme(double(i) * 0.2, 0.95f), chip * 0.5f, 0, std::max(1.0f, chip * 0.08f));
      text(ImVec2(cx + chip * 0.55f, cy), chip, chips[i], Theme(double(i) * 0.2, 0.95f));
      cx += w + u;
    }
  }
  // A faint line between the rows and the footer.
  draw->AddLine(ImVec2(left, fy - u * 1.4f), ImVec2(right, fy - u * 1.4f), Theme(0.0, 0.3f), 1.0f);

  // While typing, the window takes text: SDL only sends characters while ImGui says it wants text input, and the
  // builder draws its own field instead of an ImGui text box.
  if (typing_) ImGui::GetCurrentContext()->PlatformImeData.WantTextInput = true;
  return outcome;
}

void Screen::SetTab(Tab tab) {
  tab_ = tab;
  row_ = 0;
}

bool Screen::StartTyping() {
  const auto rows = CurrentRows();
  if (tab_ != Tab::kRules || row_ >= int(rows.size()) || rows[size_t(row_)].row != Row::kName) return false;
  typing_ = true;
  typed_ = builder_.name();
  enter_released_ = false;
  return true;
}

}  // namespace gw2::mode_builder
