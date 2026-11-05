#pragma once

// The mode builder's screen (mode_builder_rules.h has its rows), inside the Custom modes list (custom_modes_menu.cpp),
// which gives it the pad's actions while it shows.
//
//   A plays (saving first), B saves and goes back to the list, X puts the row back to the game's own, LB and RB change
//   tab, Left and Right change the row. On the name row, Enter (with the keyboard) types a name: while typing, every
//   key is a letter and nothing reaches the game or the list's controls; Enter keeps it, Esc puts the old one back.
//
// A builder file starts with custom_mode::kBuilderNote, and only such files are rewritten. A new mode that was never
// changed writes no file. A builder file can be deleted from its last row, with a second press to confirm.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "custom_modes.h"
#include "mode_builder_rules.h"
#include "replays_menu_rules.h"

namespace gw2::mode_builder {

struct Outcome {
  enum Kind { kStay, kPlay, kBack } kind = kStay;
  std::optional<custom_mode::Mode> mode;  // kPlay: the mode to play
  std::string folder;                      // the builder file now holding it, if any
  std::string message;                     // for the list (kBack)
};

class Screen {
 public:
  // `folder`: the builder file it rewrites, or empty for a new mode. `entries`: the list's modes, for names, folders
  // and "same rules as".
  Screen(const custom_mode::Mode& from, std::string folder, std::vector<custom_modes::Entry> entries, bool fresh_name);

  // `keyboard`: A on the name row types a name instead of playing.
  Outcome Apply(replays_menu::Action action, bool keyboard);
  // Draws the screen and reads typing; the mouse can pick a row and change it.
  Outcome Draw(ImGuiIO& io, bool keyboard);
  bool Typing() const { return typing_; }
  void SetTab(Tab tab);
  const std::string& folder() const { return folder_; }

 private:
  std::vector<RowRef> CurrentRows() const;
  bool OnDeleteRow() const;
  bool StartTyping();
  void Changed();
  // Writes the file if anything changed; returns false (with message_) when it couldn't.
  bool Save();

  Builder builder_;
  std::string folder_;
  std::vector<custom_modes::Entry> entries_;
  std::map<std::string, uint64_t> bests_;
  Tab tab_ = Tab::kRules;
  int row_ = 0;
  bool dirty_ = false;
  bool typing_ = false;
  std::string typed_;
  bool enter_released_ = false;  // the Enter that began typing must be let go before an Enter keeps the name
  bool delete_armed_ = false;
  std::string message_;
  double pop_ = -10.0;  // when a value last changed, for the chips' pop
};

}  // namespace gw2::mode_builder
