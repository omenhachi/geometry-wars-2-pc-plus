// The panels beside the main menu (main_menu_panels.h).
//
// The main menu is the game's screen 0x8208F9B8 (master_page.h tracks the screen switches). Its rows only go up and
// down, so left and right are free to step out of them. While a panel has the focus the game is given a neutral pad
// and the list this reads it, as the Watch replays list does (replays_menu_rules.h); the button that stepped back is
// held from the game until it is let go, so it never presses anything on the menu behind.

#include "main_menu_panels.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "custom_modes_menu.h"
#include "demo.h"
#include "guest_memory.h"
#include "line_font.h"
#include "master_page.h"
#include "neon_ui.h"
#include "replays_menu.h"

namespace gw2::main_menu_panels {
namespace {

using namespace gw2::neon;

constexpr uint16_t kDpadUp = 0x0001;
constexpr uint16_t kDpadDown = 0x0002;
constexpr uint16_t kDpadLeft = 0x0004;
constexpr uint16_t kDpadRight = 0x0008;
constexpr uint16_t kStart = 0x0010;
constexpr uint16_t kBack = 0x0020;
constexpr uint16_t kA = 0x1000;
constexpr uint16_t kB = 0x2000;
constexpr int kStickDeadzone = 12000;
constexpr uint32_t kGamepadOffset = 4;  // XINPUT_STATE: packet number, then the gamepad
constexpr uint32_t kGamepadSize = 12;
constexpr uint32_t kUsers = 4;

enum class Icon { kReplay, kKnobs };

struct Tile {
  const char* lines[2];  // the name, on one or two lines
  Icon icon;
  void (*open)();
};

// Left: what you've played. Right: new things to play.
const std::array<std::vector<Tile>, 2> kPanels = {{
    {{{"Watch", "replays"}, Icon::kReplay, replays_menu::Open}},
    {{{"Custom", "modes"}, Icon::kKnobs, custom_modes_menu::Open}},
}};

struct Seen {
  uint16_t buttons = 0;
  bool up = false, down = false, left = false, right = false;
};

std::mutex g_mutex;
int g_side = -1;   // the panel with the focus (0 left, 1 right), or -1: the game's menu has it
int g_index = 0;   // the tile with the focus in that panel
bool g_holding = false;  // the button that stepped back is kept from the game until let go
uint32_t g_holder = 0;
std::array<Seen, kUsers> g_seen{};

// The main menu is up with nothing of PC Plus's over it.
bool MenuShows() {
  return master_page::OnMainMenu() && !replays_menu::IsOpen() && !custom_modes_menu::IsOpen() && !demo::Watching();
}

void Open(int side, int index) {
  if (side < 0 || side > 1 || index < 0 || index >= int(kPanels[size_t(side)].size())) return;
  kPanels[size_t(side)][size_t(index)].open();
}

void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 centre, float r, ImU32 colour, float thickness) {
  std::vector<ImVec2> points;
  switch (icon) {
    case Icon::kReplay: {  // a ring round a play mark
      for (int i = 0; i < 40; ++i) {
        const float a = float(i) / 40.0f * 6.2831853f;
        points.push_back(ImVec2(centre.x + std::cos(a) * r, centre.y + std::sin(a) * r));
      }
      Neon(draw, points, true, colour, thickness);
      Neon(draw,
           {ImVec2(centre.x - r * 0.32f, centre.y - r * 0.45f), ImVec2(centre.x + r * 0.5f, centre.y),
            ImVec2(centre.x - r * 0.32f, centre.y + r * 0.45f)},
           true, colour, thickness);
      break;
    }
    case Icon::kKnobs: {  // three sliders, each knob set differently: a mode's rules
      const float knobs[] = {-0.35f, 0.45f, 0.05f};
      for (int i = 0; i < 3; ++i) {
        const float y = centre.y + (float(i) - 1.0f) * r * 0.62f;
        Neon(draw, {ImVec2(centre.x - r, y), ImVec2(centre.x + r, y)}, false, colour, thickness);
        const float x = centre.x + knobs[i] * r;
        const float k = r * 0.2f;
        Neon(draw, {ImVec2(x - k, y - k), ImVec2(x + k, y - k), ImVec2(x + k, y + k), ImVec2(x - k, y + k)}, true,
             colour, thickness);
      }
      break;
    }
  }
}

class Panels final : public rex::ui::ImGuiDialog {
 public:
  explicit Panels(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!MenuShows()) return;
    int side, index;
    {
      std::lock_guard lock(g_mutex);
      side = g_side;
      index = g_index;
    }
    // The game's 16:9 canvas inside the window; the panels sit in the black space either side of its menu.
    const float cw = std::min(io.DisplaySize.x, io.DisplaySize.y * 16.0f / 9.0f);
    const float ch = cw * 9.0f / 16.0f;
    const ImVec2 origin((io.DisplaySize.x - cw) * 0.5f, (io.DisplaySize.y - ch) * 0.5f);
    const float u = ch / 100.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // With a panel focused the game's menu dims, so only one thing reads as chosen.
    if (side >= 0) {
      draw->AddRectFilled(ImVec2(origin.x + cw * 0.31f, origin.y + ch * 0.38f),
                          ImVec2(origin.x + cw * 0.69f, origin.y + ch * 0.88f), Rgba(0, 0, 0, 0.55f), u * 1.0f);
    }

    // Centred in the game's own side frames, level with the menu's first and last rows.
    const float tile_w = cw * 0.14f;
    const float top = origin.y + ch * 0.388f, bottom = origin.y + ch * 0.866f;
    const float pulse = 0.5f + 0.5f * float(std::sin(Seconds() * 3.0));
    for (int s = 0; s < 2; ++s) {
      const auto& tiles = kPanels[size_t(s)];
      const float x0 = origin.x + cw * (s == 0 ? 0.155f : 0.845f) - tile_w * 0.5f;
      const float gap = u * 2.5f;
      const float tile_h = (bottom - top - gap * float(tiles.size() - 1)) / float(tiles.size());
      for (int i = 0; i < int(tiles.size()); ++i) {
        const Tile& tile = tiles[size_t(i)];
        const ImVec2 p0(x0, top + float(i) * (tile_h + gap)), p1(x0 + tile_w, p0.y + tile_h);
        const bool hovered = ImGui::IsMouseHoveringRect(p0, p1, false);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          {
            std::lock_guard lock(g_mutex);
            g_side = s;
            g_index = i;
          }
          Open(s, i);
        }
        const bool focused = s == side && i == index;
        const float glow = focused ? 0.75f + 0.25f * pulse : hovered ? 0.6f : 0.28f;
        draw->AddRectFilled(p0, p1, Rgba(0.02f, 0.03f, 0.08f, 0.85f), u * 1.2f);
        if (focused) draw->AddRectFilled(p0, p1, Theme(0.0, 0.10f + 0.06f * pulse), u * 1.2f);
        Neon(draw, {p0, ImVec2(p1.x, p0.y), p1, ImVec2(p0.x, p1.y)}, true, Theme(0.0, glow),
             focused ? u * 0.45f : u * 0.25f);
        const ImVec2 centre((p0.x + p1.x) * 0.5f, p0.y + tile_h * 0.38f);
        DrawIcon(draw, tile.icon, centre, std::min(tile_w, tile_h) * 0.22f, Theme(0.0, focused ? 1.0f : glow + 0.2f),
                 std::max(1.2f, u * (focused ? 0.35f : 0.25f)));
        const float text = u * 2.6f;
        const float thin = std::max(1.2f, text * 0.07f);
        float y = p0.y + tile_h * 0.68f;
        for (const char* line : tile.lines) {
          if (!line) continue;
          const float w = line_font::Width(line, text);
          line_font::Draw(draw, ImVec2(centre.x - w * 0.5f, y), text, line,
                          focused ? Theme() : Rgba(0.8f, 0.85f, 0.95f, 0.5f + glow * 0.5f), thin);
          y += text * 1.35f;
        }
      }
    }
  }
};

}  // namespace

void CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  new Panels(drawer);  // retained by the drawer for the session
}

void FilterPad(uint8_t* base, uint32_t user, uint32_t state) {
  if (state == 0 || user >= kUsers) return;
  const uint32_t pad = state + kGamepadOffset;
  const uint16_t buttons = LoadBE16(base, pad);
  const int lx = int16_t(LoadBE16(base, pad + 4)), ly = int16_t(LoadBE16(base, pad + 6));
  const int rx = int16_t(LoadBE16(base, pad + 8)), ry = int16_t(LoadBE16(base, pad + 10));
  // The arrow keys reach the right stick; the left stick and the d-pad work too.
  const bool up = (buttons & kDpadUp) || ly > kStickDeadzone || ry > kStickDeadzone;
  const bool down = (buttons & kDpadDown) || ly < -kStickDeadzone || ry < -kStickDeadzone;
  const bool left = (buttons & kDpadLeft) || lx < -kStickDeadzone || rx < -kStickDeadzone;
  const bool right = (buttons & kDpadRight) || lx > kStickDeadzone || rx > kStickDeadzone;

  std::lock_guard lock(g_mutex);
  Seen& seen = g_seen[user];
  const uint16_t pressed = uint16_t(buttons & ~seen.buttons);
  const bool new_up = up && !seen.up, new_down = down && !seen.down;
  const bool new_left = left && !seen.left, new_right = right && !seen.right;
  seen = {buttons, up, down, left, right};
  const auto hide = [&] { std::memset(base + pad, 0, kGamepadSize); };

  if (g_holding) {
    hide();
    if (user == g_holder && !(buttons & (kA | kB | kStart | kBack))) g_holding = false;
    return;
  }
  if (!MenuShows()) {
    if (!master_page::OnMainMenu()) g_side = -1;  // left the main menu: its menu has the focus when it shows again
    return;
  }
  if (g_side < 0) {
    if (new_left || new_right) {
      g_side = new_left ? 0 : 1;
      g_index = 0;
      hide();
    }
    return;
  }
  hide();
  const int count = int(kPanels[size_t(g_side)].size());
  const bool back = (pressed & (kB | kBack)) || (g_side == 0 && new_right) || (g_side == 1 && new_left);
  if (back) {
    g_side = -1;
    g_holding = (pressed & (kB | kBack)) != 0;
    g_holder = user;
  } else if (pressed & (kA | kStart)) {
    Open(g_side, g_index);  // the list notes the A still held as it opens, so it isn't a press there
  } else if (new_up && g_index > 0) {
    --g_index;
  } else if (new_down && g_index + 1 < count) {
    ++g_index;
  }
}

}  // namespace gw2::main_menu_panels
