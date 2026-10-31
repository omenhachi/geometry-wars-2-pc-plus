// The results' pilot card (progression_overlay.h): either side of the Game Over screen's Final Score, the XP the run
// earned counting up into the level bar (a level-up flashes and bursts), and on the other side what changed: tiers
// climbed, bounties finished, rewards unlocked, one after another. It takes no input, so the game's buttons work at
// once, and it shows whether or not the results panel does.

#include "progression_overlay.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "line_font.h"
#include "live_fix.h"
#include "neon_ui.h"
#include "progression.h"
#include "progression_ui.h"
#include "run_stats_hooks.h"

namespace gw2 {
namespace {

using namespace gw2::neon;
namespace pr = gw2::progression;
namespace ui = gw2::progression_ui;

constexpr double kCount = 1.4;   // the XP counts up over this
constexpr double kStep = 0.45;   // between the changes on the right

std::string_view KindName(pr::RewardKind kind) {
  switch (kind) {
    case pr::RewardKind::kPalette: return "palette";
    case pr::RewardKind::kCrosshair: return "crosshair";
    case pr::RewardKind::kTitle: return "title";
    case pr::RewardKind::kShipColour: return "ship colour";
  }
  return {};
}

struct Line {
  enum Kind { kTier, kBounty, kUnlock } kind;
  std::string text;
  pr::Tier tier = pr::Tier::kNone;
};

class PilotCard final : public rex::ui::ImGuiDialog {
 public:
  explicit PilotCard(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!run::ResultsToShow()) {
      showing_ = false;
      return;
    }
    if (!showing_) {
      showing_ = true;
      shown_ = std::nullopt;
      // Only an outcome this screen hasn't shown: a replay's results have none.
      if (const std::optional<pr::Shown> last = pr::LastOutcome(); last && last->serial != last_serial_) {
        last_serial_ = last->serial;
        shown_ = last->outcome;
        start_ = Seconds();
        shown_level_ = 0;
        level_up_at_ = 0;
        lines_.clear();
        for (const pr::TierUp& t : shown_->tiers) {
          lines_.push_back({Line::kTier, std::format("{}  {}", pr::BoardName(t.board), pr::TierName(t.to)), t.to});
        }
        for (const pr::Bounty& b : shown_->finished) lines_.push_back({Line::kBounty, pr::BountyText(b)});
        for (const pr::Reward* r : shown_->unlocked) {
          lines_.push_back({Line::kUnlock, std::format("{} {}", r->name, KindName(r->kind))});
        }
      }
    }
    if (!shown_) return;
    Draw(io, *shown_, Seconds() - start_);
  }

 private:
  void Draw(ImGuiIO& io, const pr::Outcome& o, double t) {
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float in = float(std::clamp(t / 0.3, 0.0, 1.0));
    const float ease = 1.0f - (1.0f - in) * (1.0f - in);

    // Left of the Final Score: the level and its bar, the XP counting up.
    const ImVec2 l0(origin.x + cw * (0.065f - 0.03f * (1.0f - ease)), origin.y + ch * 0.195f);
    const ImVec2 l1(l0.x + cw * 0.30f, origin.y + ch * 0.315f);
    draw->AddRectFilled(l0, l1, Rgba(0.02f, 0.03f, 0.08f, 0.88f * ease), u);
    TileGrid(draw, l0, l1, u, ease);
    const float counted = float(std::clamp((t - 0.25) / kCount, 0.0, 1.0));
    const float eased = 1.0f - (1.0f - counted) * (1.0f - counted) * (1.0f - counted);
    const uint64_t before_total = TotalOf(o.before);
    const uint64_t now_total = before_total + uint64_t(double(o.total()) * eased);
    const pr::Level now = pr::LevelFor(now_total);
    // A level-up: a flash and a burst as the bar comes round.
    if (now.level > shown_level_ || shown_level_ == 0) {
      if (shown_level_ != 0) level_up_at_ = Seconds();
      shown_level_ = now.level;
    }
    const double since_up = level_up_at_ > 0 ? Seconds() - level_up_at_ : 99.0;
    const ImU32 colour = since_up < 0.6 ? Rgba(1, 1, 1, ease) : Theme(0.0, ease);
    const float pad = u * 1.4f;
    const float size = u * 3.4f;
    ui::DrawLevel(draw, ImVec2(l0.x + pad, l0.y + pad), size, l1.x - l0.x - pad * 2.0f,
                  now.level, float(double(now.into) / double(std::max<uint64_t>(now.needed, 1))), colour, ease);
    const std::string gained = std::format("+{} XP", Grouped(int64_t(double(o.total()) * eased)));
    const float gsize = u * 2.6f;
    line_font::GameText(draw, ImVec2(l1.x - pad - line_font::GameWidth(gained, gsize), l0.y + pad + size * 0.15f),
                        gsize, gained, Rgba(1, 1, 1, ease), std::max(1.2f, gsize * 0.08f));
    // What it was made of.
    std::string parts = std::format("Time +{}", Grouped(int64_t(o.time_xp)));
    if (o.best_xp) parts += std::format("   Best +{}", Grouped(int64_t(o.best_xp)));
    if (o.tier_xp) parts += std::format("   Tiers +{}", Grouped(int64_t(o.tier_xp)));
    if (o.bounty_xp) parts += std::format("   Bounty +{}", Grouped(int64_t(o.bounty_xp)));
    const float psize = u * 1.5f;
    line_font::GameText(draw, ImVec2(l0.x + pad, l1.y - pad - psize), psize, parts, Rgba(0.8f, 0.85f, 0.95f, 0.8f * ease),
                        std::max(1.0f, psize * 0.07f));
    if (since_up < 1.6) {
      const float k = float(since_up / 1.6);
      const ImVec2 c((l0.x + l1.x) * 0.5f, (l0.y + l1.y) * 0.5f);
      draw->AddRectFilled(l0, l1, Rgba(1, 1, 1, 0.25f * (1.0f - k)), u);
      for (int i = 0; i < 16; ++i) {  // the burst
        const float a = 6.2831853f * float(i) / 16.0f;
        const float r0 = u * (2.0f + 30.0f * k), r1 = r0 + u * 3.0f;
        draw->AddLine(ImVec2(c.x + std::cos(a) * r0, c.y + std::sin(a) * r0 * 0.5f),
                      ImVec2(c.x + std::cos(a) * r1, c.y + std::sin(a) * r1 * 0.5f), Theme(i * 0.06, 1.0f - k),
                      u * 0.35f);
      }
      const std::string up = "LEVEL UP";
      const float usize = u * 4.0f * (1.0f + 0.15f * (1.0f - k));
      line_font::GameText(draw, ImVec2(c.x - line_font::GameWidth(up, usize) * 0.5f, l1.y + u * 0.6f), usize, up,
                          Rgba(1, 1, 1, 1.0f - k * k), std::max(1.5f, usize * 0.09f));
    }

    // Right of the Final Score: what changed, one by one, loudest last.
    if (lines_.empty()) return;
    const ImVec2 r0(origin.x + cw * (0.635f + 0.03f * (1.0f - ease)), origin.y + ch * 0.195f);
    const ImVec2 r1(origin.x + cw * 0.935f, origin.y + ch * 0.315f);
    draw->AddRectFilled(r0, r1, Rgba(0.02f, 0.03f, 0.08f, 0.88f * ease), u);
    TileGrid(draw, r0, r1, u, ease);
    const size_t visible = std::min<size_t>(lines_.size(), 3);
    // Past three, the newest three, stepping through them all.
    const size_t shown = std::min<size_t>(lines_.size(), size_t(std::max(0.0, (t - 0.5) / kStep)) + 1);
    const size_t first = shown > visible ? shown - visible : 0;
    const float row = (r1.y - r0.y - pad * 2.0f) / float(visible);
    for (size_t i = first; i < shown; ++i) {
      const Line& line = lines_[i];
      const double age = t - 0.5 - kStep * double(i);
      const float pop = float(std::clamp(age / 0.2, 0.0, 1.0));
      const float y = r0.y + pad + row * float(i - first);
      const float lsize = std::min(row * 0.62f, u * 2.4f) * (1.0f + 0.25f * (1.0f - pop));
      float x = r0.x + pad;
      ImU32 c = Theme(double(i) * 0.2, pop);
      if (line.kind == Line::kTier) {
        ui::DrawTierBadge(draw, ImVec2(x + lsize * 0.6f, y + lsize * 0.5f), lsize * 0.55f, line.tier, pop);
        x += lsize * 1.6f;
        c = ui::TierColour(line.tier, pop);
      } else {
        const std::string tag = line.kind == Line::kBounty ? "BOUNTY" : "NEW";
        const float tsize = lsize * 0.7f;
        line_font::GameText(draw, ImVec2(x, y + (lsize - tsize) * 0.5f), tsize, tag,
                            line.kind == Line::kBounty ? Rgba(1, 0.85f, 0.35f, pop) : Rgba(1, 1, 1, pop),
                            std::max(1.0f, tsize * 0.08f));
        x += line_font::GameWidth(tag, tsize) + lsize * 0.6f;
      }
      if (pop < 1.0f) draw->AddRectFilled(ImVec2(r0.x, y - u * 0.3f), ImVec2(r1.x, y + lsize + u * 0.3f),
                                          Rgba(1, 1, 1, 0.2f * (1.0f - pop)), u * 0.5f);
      line_font::GameText(draw, ImVec2(x, y), lsize, line.text, c, std::max(1.0f, lsize * 0.08f));
    }
  }

  // The XP total at the start of `level`'s progress.
  static uint64_t TotalOf(const pr::Level& level) {
    uint64_t total = level.into;
    for (uint32_t l = 1; l < level.level; ++l) total += pr::XpToNext(l);
    return total;
  }

  bool showing_ = false;
  std::optional<pr::Outcome> shown_;
  uint32_t last_serial_ = 0;
  double start_ = 0;
  std::vector<Line> lines_;
  uint32_t shown_level_ = 0;
  double level_up_at_ = 0;
};

// The game's leaderboard screen: each row's tier for that board, from its score alone, between its rank and its name.
// The screen keeps only the ten rows it shows, top to bottom, so row i is slot i.
class BoardBadges final : public rex::ui::ImGuiDialog {
 public:
  explicit BoardBadges(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const std::optional<live::ShownBoard> shown = live::LeaderboardOnScreen();
    if (!shown) return;
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // The rows' centres, as shares of the canvas, beside the rank on the left where the curve is slight.
    constexpr float kFirstRow = 0.168f;
    constexpr float kRowStep = 0.0748f;
    constexpr float kBadgeX = 0.19f;
    for (size_t i = 0; i < shown->scores.size(); ++i) {
      const pr::Tier tier = pr::TierFor(shown->board, shown->scores[i]);
      if (tier == pr::Tier::kNone) continue;
      ui::DrawTierBadge(draw, ImVec2(origin.x + cw * kBadgeX, origin.y + ch * (kFirstRow + kRowStep * float(i))),
                        u * 1.5f, tier);
    }
  }
};

}  // namespace

void CreateProgressionOverlay(rex::ui::ImGuiDrawer* drawer) {
  new PilotCard(drawer);    // retained by the drawer for the session
  new BoardBadges(drawer);
}

}  // namespace gw2
