// The results screen: after a single-player run, a neon panel over the game's post-game screen with the score over time
// (deaths, bombs and the run's best moment marked on it) beside the enemies killed, black holes and gates. It takes no
// input, so the game's own buttons (Retry above all) work at once; it goes when the game leaves the round.

#include "results_panel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "custom_modes.h"
#include "hud_colour.h"
#include "launcher_cvars.h"
#include "line_font.h"
#include "neon_ui.h"
#include "run_stats.h"
#include "run_stats_hooks.h"

namespace gw2 {
namespace {

namespace stats = gw2::run_stats;
using namespace gw2::neon;

std::string Short(int64_t value) {
  char text[32];
  if (value >= 1'000'000) {
    std::snprintf(text, sizeof(text), "%.1fM", double(value) / 1e6);
  } else if (value >= 1'000) {
    std::snprintf(text, sizeof(text), "%.0fK", double(value) / 1e3);
  } else {
    std::snprintf(text, sizeof(text), "%d", int(value));
  }
  return text;
}

// Each enemy as the results list names and draws it: a small vector icon in its own colour.
struct EnemyLook {
  int32_t type;
  const char* name;
  float r, g, b;
};

constexpr EnemyLook kEnemies[] = {
    {18, "Wanderer", 0.65f, 0.35f, 1.0f}, {2, "Grunt", 0.3f, 0.75f, 1.0f},     {6, "Weaver", 0.35f, 1.0f, 0.45f},
    {9, "Spinner", 1.0f, 0.35f, 0.85f},   {10, "Mini spinner", 1.0f, 0.55f, 0.9f},
    {13, "Snake", 1.0f, 0.9f, 0.3f},      {22, "Rocket", 1.0f, 0.6f, 0.2f},    {33, "Duck", 0.5f, 0.95f, 1.0f},
    {stats::kBlackHole, "Black hole", 1.0f, 0.3f, 0.25f},
    {stats::kGate, "Gate", 1.0f, 0.85f, 0.25f},
};

const EnemyLook& LookOf(int32_t type) {
  static const EnemyLook kOther{-1, "Other", 0.8f, 0.8f, 0.85f};
  for (const EnemyLook& look : kEnemies) {
    if (look.type == type) return look;
  }
  return kOther;
}

void DrawIcon(ImDrawList* draw, int32_t type, ImVec2 c, float r, float alpha) {
  const EnemyLook& look = LookOf(type);
  const ImU32 colour = Rgba(look.r, look.g, look.b, alpha);
  const float t = 1.6f;
  switch (type) {
    case 2:  // grunt: a diamond in a diamond
      Neon(draw, Ring(c, r, 4), true, colour, t);
      Neon(draw, Ring(c, r * 0.45f, 4), true, colour, t);
      break;
    case 6:  // weaver: a square with a cross
      Neon(draw, Ring(c, r, 4, 0.785398f), true, colour, t);
      Neon(draw, {ImVec2(c.x - r * 0.7f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f)}, false, colour, t);
      Neon(draw, {ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), ImVec2(c.x - r * 0.7f, c.y + r * 0.7f)}, false, colour, t);
      break;
    case 9:
    case 10: {  // spinners: a split square
      const float s = type == 10 ? r * 0.65f : r;
      Neon(draw, Ring(c, s, 4, 0.785398f), true, colour, t);
      Neon(draw, {ImVec2(c.x - s * 0.7f, c.y - s * 0.7f), ImVec2(c.x + s * 0.7f, c.y + s * 0.7f)}, false, colour, t);
      break;
    }
    case 18:  // wanderer: a pinwheel
      for (int i = 0; i < 4; ++i) {
        const float a = 1.5707963f * float(i);
        const ImVec2 tip(c.x + r * std::cos(a), c.y + r * std::sin(a));
        const ImVec2 side(c.x + r * 0.7f * std::cos(a + 0.9f), c.y + r * 0.7f * std::sin(a + 0.9f));
        Neon(draw, {c, tip, side}, true, colour, t);
      }
      break;
    case 13: {  // snake: a head and a wavy tail
      Neon(draw, Ring(ImVec2(c.x + r * 0.55f, c.y), r * 0.4f, 6), true, colour, t);
      std::vector<ImVec2> tail;
      for (int i = 0; i <= 8; ++i) {
        tail.push_back(ImVec2(c.x + r * 0.15f - r * 1.1f * float(i) / 8.0f, c.y + r * 0.35f * std::sin(float(i) * 1.2f)));
      }
      Neon(draw, tail, false, colour, t);
      break;
    }
    case 22:  // rocket: an arrow
      Neon(draw, {ImVec2(c.x + r, c.y), ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x - r * 0.5f, c.y),
                  ImVec2(c.x - r, c.y + r * 0.6f)},
           true, colour, t);
      break;
    case 33:  // duck: a chevron
      Neon(draw, {ImVec2(c.x - r, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.6f), ImVec2(c.x + r, c.y - r * 0.5f)}, false,
           colour, t);
      break;
    case stats::kBlackHole:
      Neon(draw, Ring(c, r, 20), true, colour, t);
      Neon(draw, Ring(c, r * 0.45f, 14), true, colour, t);
      break;
    case stats::kGate:
      Neon(draw, {ImVec2(c.x - r * 0.75f, c.y + r * 0.75f), ImVec2(c.x + r * 0.75f, c.y - r * 0.75f)}, false, colour, t);
      Neon(draw, Ring(ImVec2(c.x - r * 0.8f, c.y + r * 0.8f), r * 0.25f, 8), true, colour, t);
      Neon(draw, Ring(ImVec2(c.x + r * 0.8f, c.y - r * 0.8f), r * 0.25f, 8), true, colour, t);
      break;
    default:
      Neon(draw, Ring(c, r * 0.5f, 8), true, colour, t);
      break;
  }
}

// A row of the list: icon, name, kills and points. `highlight` frames the row (black holes and gates).
void DrawRow(ImDrawList* draw, ImVec2 at, float width, float height, int32_t type, const std::string& name,
             int32_t kills, int64_t points, bool highlight, float alpha) {
  const float text = height * 0.38f;
  const float mid = at.y + height * 0.5f;
  if (highlight) {
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height - 2), Theme(0.0, 0.10f * alpha), height * 0.2f);
    draw->AddRect(at, ImVec2(at.x + width, at.y + height - 2), Theme(0.0, 0.55f * alpha), height * 0.2f, 0, 1.2f);
  }
  DrawIcon(draw, type, ImVec2(at.x + height * 0.55f, mid), height * 0.28f, alpha);
  const ImU32 white = Rgba(1, 1, 1, 0.92f * alpha);
  const float thickness = std::max(1.0f, text * 0.07f);
  line_font::GameText(draw, ImVec2(at.x + height * 1.15f, mid - text * 0.5f), text, name, white, thickness);
  const std::string count = "x" + Grouped(kills);
  const float cw = line_font::GameWidth(count, text);
  line_font::GameText(draw, ImVec2(at.x + width * 0.64f - cw, mid - text * 0.5f), text, count,
                  Rgba(0.75f, 0.8f, 0.9f, alpha), thickness);
  const std::string score = Grouped(points);
  const float w = line_font::GameWidth(score, text);
  line_font::GameText(draw, ImVec2(at.x + width - w - height * 0.25f, mid - text * 0.5f), text, score, Theme(0.0, alpha),
                  thickness);
}

class ResultsPanel final : public rex::ui::ImGuiDialog {
 public:
  explicit ResultsPanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const std::optional<stats::Run> run = REXCVAR_GET(gw2_results_screen) ? run::ResultsToShow() : std::nullopt;
    if (!run) {
      shown_ = false;
      return;
    }
    const double now = Seconds();
    if (!shown_) {
      shown_ = true;
      appeared_ = now;
      // A Master mode's run is named as the mode, not its base mode; decided once as the panel appears.
      const std::optional<custom_modes::Current> active = custom_modes::Active();
      title_ = active && active->board ? active->mode.name : ModeName(run->mode());
    }
    const float age = float(now - appeared_);
    const float appear = std::clamp(age / 0.35f, 0.0f, 1.0f);
    const float ease = 1.0f - (1.0f - appear) * (1.0f - appear);

    // The game's 16:9 canvas inside the window. The panel covers the Game Over screen's middle boxes (Previous Scores,
    // Nearby Players, Best This Session) and leaves its title, Final Score and "Continue" line in view.
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;  // one hundredth of the canvas height
    const ImVec2 p0(origin.x + cw * 0.04f, origin.y + ch * (0.335f + 0.04f * (1.0f - ease)));
    const ImVec2 p1(origin.x + cw * 0.96f, p0.y + ch * 0.54f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, ease), u * 1.2f);
    draw->AddRect(p0, p1, Theme(0.0, 0.25f * ease), u * 1.2f, 0, u * 0.9f);
    draw->AddRect(p0, p1, Theme(0.0, 0.9f * ease), u * 1.2f, 0, u * 0.25f);

    const float pad = u * 3.0f;
    const float title = u * 4.2f;
    line_font::GameText(draw, ImVec2(p0.x + pad, p0.y + pad), title, title_, Theme(0.0, ease),
                    std::max(1.2f, title * 0.06f));
    const std::string final_score = Grouped(run->final_score());
    const float fw = line_font::GameWidth(final_score, title);
    line_font::GameText(draw, ImVec2(p1.x - pad - fw, p0.y + pad), title, final_score, Rgba(1, 1, 1, ease),
                    std::max(1.2f, title * 0.06f));

    // Run facts under the mode's name: how long it lasted, and the biggest gate blast when there was one.
    const float fact = u * 2.2f;
    const float fact_thin = std::max(1.0f, fact * 0.07f);
    float fx = p0.x + pad;
    const float fy = p0.y + pad + title + u * 1.4f;
    const auto put = [&](const std::string& text, ImU32 colour, float gap) {
      fx += line_font::GameText(draw, ImVec2(fx, fy), fact, text, colour, fact_thin) + gap;
    };
    const ImU32 label = Rgba(0.7f, 0.75f, 0.85f, 0.8f * ease);
    put("Time", label, fact * 0.6f);
    put(Clock2(run->duration()), Theme(0.0, ease), fact * 2.0f);
    if (run->best_blast().kills > 0) {
      put("Best gate blast", label, fact * 0.6f);
      put("x" + Grouped(run->best_blast().kills), Rgba(1, 1, 1, ease), fact * 0.8f);
      put(Grouped(run->best_blast().points), Theme(0.0, ease), 0.0f);
    }

    const float top = fy + fact + pad * 0.6f;
    const ImVec2 list0(p0.x + pad, top);
    const float list_w = (p1.x - p0.x) * 0.43f;
    DrawList(draw, *run, list0, list_w, p1.y - pad - top, u, ease);
    const ImVec2 g0(list0.x + list_w + pad * 1.5f, top + u * 1.0f);
    const ImVec2 g1(p1.x - pad, p1.y - pad - u * 3.5f);
    DrawGraph(draw, *run, g0, g1, u, ease, std::clamp((age - 0.2f) / 1.3f, 0.0f, 1.0f));
  }

 private:
  void DrawList(ImDrawList* draw, const stats::Run& run, ImVec2 at, float width, float height, float u, float alpha) {
    constexpr size_t kTop = 5;
    const float row = std::min(u * 5.6f, height / 8.0f);
    const auto ranked = run.RankedEnemies();
    float y = at.y;
    for (size_t i = 0; i < ranked.size() && i < kTop; ++i) {
      const auto& r = ranked[i];
      DrawRow(draw, ImVec2(at.x, y), width, row, r.type, LookOf(r.type).name, r.tally.kills, r.tally.points, false,
              alpha);
      y += row;
    }
    if (ranked.size() > kTop) {
      int32_t kills = 0;
      int64_t points = 0;
      for (size_t i = kTop; i < ranked.size(); ++i) {
        kills += ranked[i].tally.kills;
        points += ranked[i].tally.points;
      }
      DrawRow(draw, ImVec2(at.x, y), width, row, -1, "+" + std::to_string(ranked.size() - kTop) + " more", kills,
              points, false, alpha * 0.8f);
      y += row;
    }
    y += row * 0.35f;
    const auto& types = run.by_type();
    const auto kills_of = [&](int32_t type) {
      const auto it = types.find(type);
      return it == types.end() ? 0 : it->second.kills;
    };
    DrawRow(draw, ImVec2(at.x, y), width, row, stats::kBlackHole, "Black holes", kills_of(stats::kBlackHole),
            run.black_hole_points(), true, alpha);
    y += row;
    DrawRow(draw, ImVec2(at.x, y), width, row, stats::kGate, "Gates", kills_of(stats::kGate), run.gate_points(), true,
            alpha);
  }

  void DrawGraph(ImDrawList* draw, const stats::Run& run, ImVec2 g0, ImVec2 g1, float u, float alpha, float reveal) {
    const auto& samples = run.samples();
    if (samples.size() < 2) return;
    const float t0 = samples.front().time;
    const float t1 = std::max(samples.back().time, t0 + 1.0f);
    const double top = std::max<double>(double(run.final_score()), 1.0) * 1.08;
    const auto at = [&](float time, double score) {
      return ImVec2(g0.x + (g1.x - g0.x) * (time - t0) / (t1 - t0), g1.y - float((g1.y - g0.y) * (score / top)));
    };
    const float label = u * 2.2f;
    const float thin = std::max(1.0f, label * 0.07f);
    // Grid: four score lines and the run's clock along the bottom.
    for (int i = 1; i <= 4; ++i) {
      const double score = top / 1.08 * i / 4.0;
      const float y = at(t0, score).y;
      draw->AddLine(ImVec2(g0.x, y), ImVec2(g1.x, y), Theme(0.0, 0.10f * alpha), 1.0f);
      line_font::GameText(draw, ImVec2(g0.x + u * 0.6f, y - label - u * 0.4f), label, Short(int64_t(score)),
                      Rgba(0.7f, 0.75f, 0.85f, 0.55f * alpha), thin);
    }
    draw->AddLine(ImVec2(g0.x, g1.y), ImVec2(g1.x, g1.y), Theme(0.0, 0.45f * alpha), 1.5f);
    for (int i = 0; i <= 3; ++i) {
      const float time = t0 + (t1 - t0) * float(i) / 3.0f;
      const std::string text = Clock2(time - t0);
      const float w = line_font::GameWidth(text, label);
      const float x = std::clamp(at(time, 0).x - w * 0.5f, g0.x, g1.x - w);
      line_font::GameText(draw, ImVec2(x, g1.y + u * 1.0f), label, text, Rgba(0.7f, 0.75f, 0.85f, 0.7f * alpha), thin);
    }

    // The score line, drawn in from the left as the panel appears; Rainbow drifts along it.
    const float until = t0 + (t1 - t0) * reveal;
    // One point per bucket of the run (its last score there), so the line is smooth rather than every quarter second.
    constexpr int kBuckets = 160;
    std::vector<ImVec2> line;
    int bucket = -1;
    for (const stats::Sample& s : samples) {
      if (s.time > until) break;
      const int b = int(float(kBuckets) * (s.time - t0) / (t1 - t0));
      const ImVec2 p = at(s.time, double(s.score));
      if (b == bucket && !line.empty()) {
        line.back() = p;
      } else {
        line.push_back(p);
        bucket = b;
      }
    }
    if (line.size() >= 2) {
      for (size_t i = 1; i < line.size(); ++i) {
        const ImVec2& a = line[i - 1];
        const ImVec2& b = line[i];
        draw->AddQuadFilled(a, b, ImVec2(b.x, g1.y), ImVec2(a.x, g1.y),
                            Theme(double(i) * 0.02, 0.07f * alpha));
      }
      const bool rainbow = hud_colour::CurrentPreset() == hud_colour::Preset::kRainbow;
      if (rainbow) {
        for (size_t i = 1; i < line.size(); ++i) {
          Neon(draw, {line[i - 1], line[i]}, false, Theme(double(i) * 0.02, alpha), u * 0.45f);
        }
      } else {
        Neon(draw, line, false, Theme(0.0, alpha), u * 0.45f);
      }
      const ImVec2 head = line.back();
      draw->AddCircleFilled(head, u * 0.7f, Rgba(1, 1, 1, alpha));
    }

    // Deaths and bombs, as small marks on the bottom line: part of the story, never alarms.
    for (const float death : run.deaths()) {
      if (death > until) continue;
      const float x = at(death, 0).x;
      draw->AddLine(ImVec2(x, g0.y), ImVec2(x, g1.y), Theme(0.0, 0.12f * alpha), 1.0f);
      draw->AddTriangle(ImVec2(x, g1.y - u * 1.6f), ImVec2(x - u * 0.8f, g1.y - u * 0.2f),
                        ImVec2(x + u * 0.8f, g1.y - u * 0.2f), Theme(0.0, 0.9f * alpha), 1.5f);
    }
    for (const float bomb : run.bombs()) {
      if (bomb > until) continue;
      draw->AddCircle(ImVec2(at(bomb, 0).x, g1.y - u * 0.9f), u * 0.7f, Rgba(1, 1, 1, 0.8f * alpha), 12, 1.5f);
    }

    // The best moment: a star on the line, and what it was.
    const stats::Moment& best = run.best();
    if (best.points > 0 && best.time <= until) {
      double score = 0;
      for (const stats::Sample& s : samples) {
        if (s.time > best.time) break;
        score = double(s.score);
      }
      const ImVec2 p = at(best.time, score);
      const float pulse = 1.0f + 0.12f * std::sin(float(Seconds()) * 5.0f);
      DrawStar(draw, p, u * 1.6f * pulse, Rgba(1, 0.95f, 0.6f, alpha));
      const std::string text = std::string(LookOf(best.type).name) + " +" + Grouped(best.points);
      const float w = line_font::GameWidth(text, label);
      const float x = std::clamp(p.x - w * 0.5f, g0.x, g1.x - w);
      const float y = std::max(g0.y - label * 0.5f, p.y - u * 2.4f - label);
      line_font::GameText(draw, ImVec2(x, y), label, text, Rgba(1, 0.95f, 0.7f, alpha), thin);
    }
  }

  bool shown_ = false;
  double appeared_ = 0;
  std::string title_;
};

}  // namespace

void CreateResultsPanel(rex::ui::ImGuiDrawer* drawer) {
  new ResultsPanel(drawer);  // retained by the drawer for the session
}

}  // namespace gw2
