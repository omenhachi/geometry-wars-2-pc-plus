// The Pilot screen (pilot_menu.h): the level and title across the top, then three tabs.
//
//   Stats     every board's tier, best and the score to the next tier; time and runs per mode; lifetime totals.
//   Bounties  the three open bounties; Y swaps one for another, free.
//   Rewards   everything to earn, worn with A (A again takes it off); locked ones say how they are earned.
//
// The pad is read as Watch replays' list reads it (replays_menu_rules.h): LB and RB (1 and 3) change the tab, and
// while the screen is open the game is given a neutral pad. The mouse works too.

#include "pilot_menu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "custom_mode_rules.h"
#include "guest_memory.h"
#include "hud_colour.h"
#include "launcher_settings.h"
#include "line_font.h"
#include "neon_ui.h"
#include "progression.h"
#include "progression_ui.h"
#include "replays_menu.h"
#include "replays_menu_rules.h"

namespace gw2::pilot_menu {
namespace {

using namespace gw2::neon;
using replays_menu::Action;
using replays_menu::ListInput;
namespace pr = gw2::progression;
namespace ui = gw2::progression_ui;

constexpr uint32_t kGamepadOffset = 4;  // XINPUT_STATE: packet number, then the gamepad
constexpr uint32_t kGamepadSize = 12;
constexpr const char* kTabs[] = {"Stats", "Bounties", "Rewards"};
constexpr int kTabCount = 3;

std::mutex g_mutex;
ListInput g_input;
std::vector<Action> g_actions;
bool g_opened = false;
std::function<void(std::string_view, std::string)> g_save;
std::string g_palette_before;  // the free colour worn before a pilot palette, this session

bool KeyboardMode() {
  return rex::cvar::GetFlagByName(kKeyboardKey) == "true";
}

// The enemies as the results name them (results_panel.cpp).
std::string_view EnemyName(int32_t type) {
  switch (type) {
    case 18: return "Wanderer";
    case 2: return "Grunt";
    case 6: return "Weaver";
    case 9: return "Spinner";
    case 10: return "Mini spinner";
    case 13: return "Snake";
    case 22: return "Rocket";
    case 33: return "Duck";
    default: return {};
  }
}

std::string Hours(double seconds) {
  const int64_t s = int64_t(seconds);
  if (s >= 3600) return std::format("{}h {:02}m", s / 3600, (s / 60) % 60);
  return std::format("{}m {:02}s", s / 60, s % 60);
}

std::string_view KindName(pr::RewardKind kind) {
  switch (kind) {
    case pr::RewardKind::kPalette: return "Palette";
    case pr::RewardKind::kCrosshair: return "Crosshair";
    case pr::RewardKind::kTitle: return "Title";
    case pr::RewardKind::kShipColour: return "Ship";
  }
  return {};
}

// The setting a kind of reward is worn by, and its value when none of the rewards is.
const char* SettingFor(pr::RewardKind kind) {
  switch (kind) {
    case pr::RewardKind::kPalette: return kHudColourKey;
    case pr::RewardKind::kCrosshair: return kCrosshairKey;
    case pr::RewardKind::kShipColour: return kShipColourKey;
    case pr::RewardKind::kTitle: break;
  }
  return nullptr;
}

const char* Unworn(pr::RewardKind kind) {
  switch (kind) {
    case pr::RewardKind::kPalette: return "green";
    case pr::RewardKind::kCrosshair: return "classic";
    default: return "";
  }
}

bool Worn(const pr::State& state, const pr::Reward& r) {
  if (r.kind == pr::RewardKind::kTitle) return state.title == r.id;
  return rex::cvar::GetFlagByName(SettingFor(r.kind)) == r.id;
}

class PilotScreen final : public rex::ui::ImGuiDialog {
 public:
  explicit PilotScreen(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::vector<Action> actions;
    bool open = false, opened = false;
    {
      std::lock_guard lock(g_mutex);
      actions.swap(g_actions);
      opened = g_opened;
      g_opened = false;
      open = g_input.open();
    }
    if (opened) {
      cursor_ = 0;
      first_ = 0;
      message_.clear();
    }
    state_ = pr::Snapshot();
    for (const Action action : actions) Apply(action);
    {
      std::lock_guard lock(g_mutex);
      open = g_input.open();
    }
    if (open) Draw(io);
  }

 private:
  int Rows() const {
    if (tab_ == 1) return int(state_.bounties.size());
    if (tab_ == 2) return int(pr::Rewards().size());
    return 0;
  }

  void Reopen() {
    std::lock_guard lock(g_mutex);
    g_input.SetOpen(true);
  }

  void Apply(Action action) {
    if (action != Action::kStar) confirm_ = -1;
    switch (action) {
      case Action::kPreviousTab:
      case Action::kNextTab:
        tab_ = (tab_ + (action == Action::kNextTab ? 1 : kTabCount - 1)) % kTabCount;
        cursor_ = 0;
        first_ = 0;
        message_.clear();
        break;
      case Action::kUp:
        if (cursor_ > 0) --cursor_;
        break;
      case Action::kDown:
        if (cursor_ + 1 < Rows()) ++cursor_;
        break;
      case Action::kStar:  // Y: a different bounty; one with progress asks twice, so none is lost by a slip
        if (tab_ == 1 && cursor_ < int(state_.bounties.size())) {
          const pr::Bounty& b = state_.bounties[size_t(cursor_)];
          if (b.progress > 0 && confirm_ != cursor_) {
            confirm_ = cursor_;
            message_ = std::format("Press {} again to swap it. The {} you've done would start over.",
                                   KeyboardMode() ? "L" : "Y", Grouped(int64_t(b.progress)));
            break;
          }
          confirm_ = -1;
          pr::RerollBounty(size_t(cursor_));
          state_ = pr::Snapshot();
          message_ = "Swapped. Swaps are always free.";
        }
        break;
      case Action::kWatch:  // A: wear or take off a reward; the list stays open
        if (tab_ == 2) Wear(size_t(cursor_));
        Reopen();
        break;
      default:
        break;
    }
  }

  void Wear(size_t index) {
    if (index >= pr::Rewards().size()) return;
    const pr::Reward& r = pr::Rewards()[index];
    if (!pr::Unlocked(state_, r)) {
      message_ = std::format("Not yet: {}.", r.how);
      return;
    }
    const bool worn = Worn(state_, r);
    if (r.kind == pr::RewardKind::kTitle) {
      pr::SetTitle(worn ? std::string_view() : r.id);
    } else {
      // Taking a palette off goes back to the colour worn before it (Rainbow, Custom ...), not always Green.
      if (!worn && r.kind == pr::RewardKind::kPalette) {
        const std::string current = rex::cvar::GetFlagByName(kHudColourKey);
        const pr::Reward* earned = pr::FindReward(current);
        if (!earned || earned->kind != pr::RewardKind::kPalette) g_palette_before = current;
      }
      const std::string value =
          worn ? (r.kind == pr::RewardKind::kPalette && !g_palette_before.empty() ? g_palette_before
                                                                                  : std::string(Unworn(r.kind)))
               : std::string(r.id);
      rex::cvar::SetFlagByName(SettingFor(r.kind), value);
      if (g_save) g_save(SettingFor(r.kind), value);
      if (r.kind == pr::RewardKind::kPalette) {  // the colour in effect, as the Colour row sets it
        hud_colour::SetCurrent(hud_colour::Parse(value).value_or(hud_colour::Preset::kGreen),
                               hud_colour::CurrentCustom());
      }
    }
    state_ = pr::Snapshot();
    message_ = worn ? std::format("Took off {}.", r.name) : std::format("Wearing {}.", r.name);
  }

  void Draw(ImGuiIO& io) {
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    const ImVec2 p0(origin.x + cw * 0.08f, origin.y + ch * 0.07f);
    const ImVec2 p1(origin.x + cw * 0.92f, origin.y + ch * 0.94f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Rgba(0, 0, 0, 0.5f));
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 1.0f), u * 1.2f);
    draw->AddRect(p0, p1, Theme(0.0, 0.25f), u * 1.2f, 0, u * 0.9f);
    draw->AddRect(p0, p1, Theme(0.0, 0.9f), u * 1.2f, 0, u * 0.25f);
    const float pad = u * 3.0f;
    const float left = p0.x + pad, right = p1.x - pad;

    // The pilot: level and bar, title, total XP.
    const pr::Level level = pr::LevelFor(state_.xp);
    const float lsize = u * 4.6f;
    ui::DrawLevel(draw, ImVec2(left, p0.y + pad), lsize, cw * 0.22f, level.level,
                  float(double(level.into) / double(std::max<uint64_t>(level.needed, 1))), Theme());
    const float tx = left + cw * 0.25f;
    const pr::Reward* title = state_.title.empty() ? nullptr : pr::FindReward(state_.title);
    line_font::GameText(draw, ImVec2(tx, p0.y + pad), u * 3.2f, title ? title->name : "Pilot", Rgba(1, 1, 1, 0.95f),
                        std::max(1.2f, u * 0.25f));
    const std::string xp = std::format("{} of {} XP to level {}", Grouped(int64_t(level.into)),
                                       Grouped(int64_t(level.needed)), level.level + 1);
    line_font::GameText(draw, ImVec2(tx, p0.y + pad + u * 4.6f), u * 1.7f, xp, Rgba(0.8f, 0.85f, 0.95f, 0.8f),
                        std::max(1.0f, u * 0.12f));

    // The tabs, top right.
    float x = right;
    for (int i = kTabCount - 1; i >= 0; --i) {
      const float size = u * 2.6f;
      const float w = line_font::GameWidth(kTabs[i], size) + u * 3.0f;
      x -= w;
      const ImVec2 t0(x, p0.y + pad), t1(x + w, p0.y + pad + size + u * 2.0f);
      const bool hovered = ImGui::IsMouseHoveringRect(t0, t1, false);
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        tab_ = i;
        cursor_ = 0;
        first_ = 0;
        message_.clear();
      }
      if (i == tab_) draw->AddRectFilled(t0, t1, Theme(0.0, 0.18f), u * 0.6f);
      draw->AddRect(t0, t1, Theme(0.0, i == tab_ ? 0.9f : 0.3f), u * 0.6f, 0, 1.5f);
      line_font::GameText(draw, ImVec2(x + u * 1.5f, p0.y + pad + u * 1.0f), size, kTabs[i],
                          i == tab_ ? Theme() : Rgba(0.8f, 0.85f, 0.95f, hovered ? 0.9f : 0.6f), std::max(1.0f, size * 0.07f));
      x -= u * 1.0f;
    }

    const float top = p0.y + pad + u * 10.0f;
    const float bottom = p1.y - pad - u * 6.0f;
    if (tab_ == 0) DrawStats(draw, u, left, right, top, bottom);
    if (tab_ == 1) DrawBounties(draw, u, left, right, top, bottom);
    if (tab_ == 2) DrawRewards(draw, u, left, right, top, bottom);

    const float hint = u * 2.0f;
    const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
    if (!message_.empty()) {
      line_font::GameText(draw, ImVec2(left, p1.y - pad - hint * 2.6f), hint, message_, Rgba(1.0f, 0.82f, 0.35f),
                          std::max(1.0f, hint * 0.07f));
    }
    const bool keys = KeyboardMode();
    std::string hints = std::string(keys ? "1 3" : "LB RB") + " Tabs   ";
    if (tab_ == 1) hints += std::string(keys ? "L" : "Y") + " Swap bounty   ";
    if (tab_ == 2) hints += std::string(keys ? "Enter" : "A") + " Wear   ";
    hints += keys ? "Esc Back" : "B Back";
    line_font::GameText(draw, ImVec2(left, p1.y - pad - hint), hint, hints, dim, std::max(1.0f, hint * 0.07f));
  }

  void DrawStats(ImDrawList* draw, float u, float left, float right, float top, float bottom) {
    const ImU32 white = Rgba(1, 1, 1, 0.92f);
    const ImU32 dim = Rgba(0.7f, 0.75f, 0.85f, 0.85f);
    const float text = u * 2.0f;
    const float thin = std::max(1.0f, text * 0.07f);
    // Every board: tier, best, next tier; time and runs for the game's six modes.
    const float table_right = left + (right - left) * 0.66f;
    const float cols[] = {left + u * 5.0f, left + (table_right - left) * 0.6f, left + (table_right - left) * 0.75f,
                          left + (table_right - left) * 0.85f, table_right};
    const auto right_text = [&](float edge, float y, float size, const std::string& s, ImU32 c) {
      line_font::GameText(draw, ImVec2(edge - line_font::GameWidth(s, size), y), size, s, c, std::max(1.0f, size * 0.07f));
    };
    const float head = u * 1.6f;
    right_text(cols[1], top, head, "Best", dim);
    right_text(cols[2], top, head, "Next tier", dim);
    right_text(cols[3], top, head, "Runs", dim);
    right_text(cols[4], top, head, "Time", dim);
    const float row = (bottom - top - u * 3.0f) / float(pr::kBoards);
    for (uint32_t board = 0; board < pr::kBoards; ++board) {
      const float y = top + u * 3.0f + row * float(board);
      const pr::Tier tier = pr::TierOf(state_, board);
      ui::DrawTierBadge(draw, ImVec2(left + u * 2.0f, y + text * 0.5f), text * 0.55f, tier);
      line_font::GameText(draw, ImVec2(cols[0], y), text, pr::BoardName(board), ui::TierColour(tier, 1.0f), thin);
      const uint64_t best = state_.bests[board];
      right_text(cols[1], y, text, best ? Grouped(int64_t(best)) : std::string("-"), white);
      const std::optional<uint64_t> next = pr::NextTierScore(board, best);
      right_text(cols[2], y, text * 0.85f, next ? std::string(custom_mode::ShortScore(*next)) : std::string("Top"), dim);
      if (board < pr::kModes) {
        const pr::ModeTotals& m = state_.life.modes[board];
        right_text(cols[3], y, text * 0.85f, std::to_string(m.runs), dim);
        right_text(cols[4], y, text * 0.85f, Hours(m.seconds), dim);
      }
    }
    // Lifetime totals, on the right.
    const float lx = table_right + u * 4.0f;
    double seconds = 0;
    uint32_t runs = 0;
    for (const pr::ModeTotals& m : state_.life.modes) {
      seconds += m.seconds;
      runs += m.runs;
    }
    const auto kills_of = [&](int32_t type) {
      const auto it = state_.life.kills.find(type);
      return it == state_.life.kills.end() ? int64_t(0) : it->second;
    };
    int64_t enemies = 0;
    int32_t favourite = 0;
    int64_t favourite_kills = 0;
    for (const auto& [type, n] : state_.life.kills) {
      if (type == pr::kGate || type == pr::kBlackHole) continue;
      enemies += n;
      if (!EnemyName(type).empty() && n > favourite_kills) {
        favourite = type;
        favourite_kills = n;
      }
    }
    const std::pair<std::string, std::string> lines[] = {
        {"Total XP", Grouped(int64_t(state_.xp))},
        {"Time played", Hours(seconds)},
        {"Runs", std::to_string(runs)},
        {"Enemies destroyed", Grouped(enemies)},
        {"Black holes", Grouped(kills_of(pr::kBlackHole))},
        {"Gates", Grouped(kills_of(pr::kGate))},
        {"Biggest gate blast", std::to_string(state_.life.best_blast)},
        {"Favourite target", favourite ? std::string(EnemyName(favourite)) : std::string("-")},
        {"Bombs used", Grouped(state_.life.bombs)},
        {"Bounties", std::to_string(state_.life.bounties)},
        {"Practice clears", std::to_string(state_.life.practice_clears)},
    };
    const float line = (bottom - top) / float(std::size(lines));
    for (size_t i = 0; i < std::size(lines); ++i) {
      const float y = top + line * float(i);
      line_font::GameText(draw, ImVec2(lx, y), text * 0.85f, lines[i].first, dim, thin);
      right_text(right, y, text * 0.95f, lines[i].second, i == 0 ? Theme() : white);
    }
  }

  void DrawBounties(ImDrawList* draw, float u, float left, float right, float top, float bottom) {
    const float row_h = std::min(u * 12.0f, (bottom - top) / float(pr::kOpenBounties));
    for (int i = 0; i < int(state_.bounties.size()); ++i) {
      const pr::Bounty& b = state_.bounties[size_t(i)];
      const ImVec2 r0(left, top + row_h * float(i)), r1(right, r0.y + row_h - u * 1.5f);
      Hover(r0, r1, i, Action::kNone);
      const bool current = i == cursor_;
      draw->AddRectFilled(r0, r1, current ? Theme(0.0, 0.12f) : Rgba(1, 1, 1, 0.03f), u * 0.8f);
      draw->AddRect(r0, r1, Theme(double(i) * 0.2, current ? 0.9f : 0.3f), u * 0.8f, 0, current ? 2.0f : 1.2f);
      const pr::Tier tier = pr::TierOf(state_, b.board);
      ui::DrawTierBadge(draw, ImVec2(r0.x + u * 4.0f, (r0.y + r1.y) * 0.5f), u * 2.0f, tier);
      const float text = u * 2.8f;
      line_font::GameText(draw, ImVec2(r0.x + u * 8.0f, r0.y + u * 2.0f), text, pr::BountyText(b), Rgba(1, 1, 1, 0.95f),
                          std::max(1.2f, text * 0.07f));
      const std::string progress = pr::BountyProgress(b);
      const std::string sub = progress.empty() ? std::string("In one run") : progress;
      line_font::GameText(draw, ImVec2(r0.x + u * 8.0f, r0.y + u * 2.0f + text + u * 1.4f), u * 1.8f, sub,
                          Rgba(0.75f, 0.8f, 0.9f, 0.85f), std::max(1.0f, u * 0.13f));
      if (b.goal == pr::Goal::kKills && b.target > 0) {  // its bar, after the count
        const float bx0 = r0.x + u * 10.0f + line_font::GameWidth(sub, u * 1.8f), bx1 = r1.x - u * 22.0f;
        const float by = r0.y + u * 2.0f + text + u * 2.0f;
        draw->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + u * 0.8f), Rgba(1, 1, 1, 0.1f), u * 0.4f);
        draw->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * float(double(b.progress) / double(b.target)),
                                                    by + u * 0.8f), Theme(), u * 0.4f);
      }
      const std::string reward = std::format("+{} XP", Grouped(int64_t(pr::kBountyXp)));
      const float rs = u * 2.6f;
      line_font::GameText(draw, ImVec2(r1.x - u * 2.0f - line_font::GameWidth(reward, rs), (r0.y + r1.y) * 0.5f - rs * 0.5f),
                          rs, reward, Rgba(1.0f, 0.85f, 0.35f), std::max(1.0f, rs * 0.08f));
    }
    const std::string note = "Bounties never run out of time. Finish one and another takes its place.";
    line_font::GameText(draw, ImVec2(left, top + row_h * float(pr::kOpenBounties) + u), u * 1.7f, note,
                        Rgba(0.7f, 0.75f, 0.85f, 0.7f), std::max(1.0f, u * 0.12f));
  }

  void DrawRewards(ImDrawList* draw, float u, float left, float right, float top, float bottom) {
    const auto& rewards = pr::Rewards();
    const float row_h = u * 4.4f;
    const int visible = std::max(1, int((bottom - top) / row_h));
    first_ = std::clamp(first_, std::max(0, cursor_ - visible + 1), cursor_);
    const float text = u * 2.3f;
    for (int i = first_; i < int(rewards.size()) && i < first_ + visible; ++i) {
      const pr::Reward& r = rewards[size_t(i)];
      const float y = top + row_h * float(i - first_);
      const ImVec2 r0(left, y), r1(right, y + row_h - u * 0.6f);
      Hover(r0, r1, i, Action::kWatch);
      const bool unlocked = pr::Unlocked(state_, r);
      const bool worn = unlocked && Worn(state_, r);
      const bool current = i == cursor_;
      if (current) draw->AddRectFilled(r0, r1, Theme(0.0, 0.12f), u * 0.6f);
      if (current || worn) draw->AddRect(r0, r1, Theme(0.0, worn ? 0.9f : 0.6f), u * 0.6f, 0, worn ? 2.0f : 1.2f);
      const std::string kind(KindName(r.kind));
      const float ks = u * 1.6f;
      line_font::GameText(draw, ImVec2(r0.x + u * 1.5f, y + (row_h - ks) * 0.4f), ks, kind,
                          Rgba(0.7f, 0.75f, 0.85f, unlocked ? 0.9f : 0.45f), std::max(1.0f, ks * 0.07f));
      const float nx = r0.x + u * 17.0f;
      line_font::GameText(draw, ImVec2(nx, y + (row_h - text) * 0.4f), text, r.name,
                          unlocked ? Theme(double(i) * 0.07) : Rgba(0.6f, 0.62f, 0.7f, 0.55f), std::max(1.0f, text * 0.07f));
      const std::string status = worn ? std::string("Wearing") : unlocked ? std::string("Unlocked") : std::string(r.how);
      const float ss = u * 1.8f;
      line_font::GameText(draw, ImVec2(r1.x - u * 1.5f - line_font::GameWidth(status, ss), y + (row_h - ss) * 0.4f), ss,
                          status, worn ? Theme() : unlocked ? Rgba(1, 1, 1, 0.85f) : Rgba(0.7f, 0.72f, 0.8f, 0.6f),
                          std::max(1.0f, ss * 0.07f));
    }
  }

  // The mouse: hovering moves the cursor, a click acts on the row.
  void Hover(ImVec2 r0, ImVec2 r1, int index, Action click) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseHoveringRect(r0, r1, false)) return;
    if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) cursor_ = index;
    if (click != Action::kNone && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      cursor_ = index;
      Apply(click);
    }
  }

  pr::State state_;
  int tab_ = 0;
  int cursor_ = 0;
  int first_ = 0;
  int confirm_ = -1;  // the bounty a swap was asked for once
  std::string message_;
};

}  // namespace

void CreatePilotMenu(rex::ui::ImGuiDrawer* drawer) {
  new PilotScreen(drawer);  // retained by the drawer for the session
}

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

void SetSaver(std::function<void(std::string_view key, std::string value)> save) {
  g_save = std::move(save);
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

}  // namespace gw2::pilot_menu
