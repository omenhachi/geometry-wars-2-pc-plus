// Keyboard prompts: with Keyboard on and the keyboard or mouse used last, the menus' button glyphs become key names
// ("Enter Select", "Esc Back"). The menu font draws " A ", " B ", " X " and " Y " as pad buttons; the same text call
// draws any other string in the menu's own lettering, so the key name simply replaces the glyph string. The help
// screens' controller diagram swaps for a keyboard and mouse one (assets/controls/keyboard_mouse.svg, shipped as
// assets/controls/controller_kbm.tga and installed next to controller.tga) the same way.

#include "keyboard_prompts.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"
#include "launcher_settings.h"
#include "window_host.h"

namespace {

enum class PadButton { kA, kB, kX, kY, kNone };

struct GlyphString {
  uint32_t address;
  PadButton button;
  bool leading_space;
};

// Every glyph string the menus pass to sub_82076E40. The arrows and shoulder glyphs (navigate, adjust) are kept.
constexpr GlyphString kGlyphStrings[] = {
    {0x82005D70, PadButton::kA, true},   // " A "
    {0x820048EC, PadButton::kA, false},  // "A "
    {0x820048E8, PadButton::kB, true},   // " B "
    {0x820048F0, PadButton::kB, false},  // "B "
    {0x82005F04, PadButton::kX, true},   // " X "
    {0x82006000, PadButton::kX, false},  // "X "
    {0x82005D7C, PadButton::kY, true},   // " Y "
};

// The first key of a ReXGlue binding ("Semicolon,Space" -> "Semicolon"), named as it reads on a keyboard.
std::string KeyName(std::string_view binding) {
  binding = binding.substr(0, binding.find(','));
  static const std::map<std::string_view, std::string_view> kNames = {
      {"Return", "Enter"},   {"Escape", "Esc"},     {"Semicolon", ";"},  {"Quote", "'"},
      {"Comma", ","},        {"Period", "."},       {"Slash", "/"},      {"Backslash", "\\"},
      {"Minus", "-"},        {"Plus", "="},         {"LBracket", "["},   {"RBracket", "]"},
      {"Backtick", "`"},     {"NumpadEnter", "Enter"}};
  const auto it = kNames.find(binding);
  return std::string(it != kNames.end() ? it->second : binding);
}

bool BindingHas(std::string_view binding, std::string_view key) {
  while (!binding.empty()) {
    const size_t comma = binding.find(',');
    if (binding.substr(0, comma) == key) {
      return true;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    binding.remove_prefix(comma + 1);
  }
  return false;
}

std::string LabelFor(PadButton button) {
  switch (button) {
    case PadButton::kA: {
      // Enter is Start by default, which the menus take as confirm too, and the key players reach for.
      const std::string a = rex::cvar::GetFlagByName("keybind_a");
      return BindingHas(rex::cvar::GetFlagByName("keybind_start"), "Return") || BindingHas(a, "Return")
                 ? std::string("Enter")
                 : KeyName(a);
    }
    case PadButton::kB:
      return "Esc";  // Escape presses B in menus (aim_camera.cpp)
    case PadButton::kX:
      return KeyName(rex::cvar::GetFlagByName("keybind_x"));
    case PadButton::kY:
      return KeyName(rex::cvar::GetFlagByName("keybind_y"));
    case PadButton::kNone:
      break;
  }
  return {};
}

// The help screens draw the texture whose pointer sub_8205E408 stores here after loading GAME:\controller.tga.
constexpr uint32_t kControllerPath = 0x82002920;
constexpr uint32_t kControllerTexture = 0x821A1D54;

std::mutex g_mutex;
std::map<std::string, uint32_t> g_guest_strings;
uint32_t g_pad_diagram = 0;
uint32_t g_keyboard_diagram = 0;

bool KeyboardPrompts() {
  return gw2::PromptsUseKeyboard() && rex::cvar::GetFlagByName(gw2::kKeyboardKey) == "true";
}

// A NUL-terminated copy of text in guest memory, made once per distinct text.
uint32_t GuestString(const std::string& text) {
  std::lock_guard lock(g_mutex);
  if (const auto it = g_guest_strings.find(text); it != g_guest_strings.end()) {
    return it->second;
  }
  const uint32_t address = rex::system::kernel_memory()->SystemHeapAlloc(uint32_t(text.size()) + 1);
  if (address != 0) {
    uint8_t* host = rex::system::kernel_memory()->TranslateVirtual<uint8_t*>(address);
    std::memcpy(host, text.data(), text.size());
    host[text.size()] = 0;
    g_guest_strings.emplace(text, address);
  }
  return address;
}

}  // namespace

namespace gw2 {

namespace {

std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

void InstallKeyboardDiagram(const std::filesystem::path& game_data_root) {
  const std::filesystem::path source = rex::filesystem::GetExecutableFolder() / "assets" / "controls" / kDiagramFile;
  const std::filesystem::path target = game_data_root / kDiagramFile;
  std::error_code error;
  if (!std::filesystem::is_regular_file(source, error) || !std::filesystem::is_directory(game_data_root, error)) {
    REXLOG_WARN("Keyboard diagram: {} or the game folder is missing; the help screens keep the controller",
                source.string());
    return;
  }
  const std::string wanted = ReadAll(source);
  if (std::filesystem::is_regular_file(target, error) && ReadAll(target) == wanted) {
    return;
  }
  std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
  if (error) {
    REXLOG_WARN("Keyboard diagram: could not install {}: {}", target.string(), error.message());
  } else {
    REXLOG_INFO("Keyboard diagram: installed {}", target.string());
  }
}

}  // namespace gw2

// D3DXCreateTextureFromFileEx: r4 the path, the texture pointer goes to the address in the stack argument at r1+124.
// Loading the controller diagram also loads the keyboard and mouse one into a slot of its own.
REX_EXTERN(__imp__sub_820B7C78);
REX_HOOK_RAW(sub_820B7C78) {
  if (ctx.r4.u32 != kControllerPath) {
    __imp__sub_820B7C78(ctx, base);
    return;
  }
  PPCContext second = ctx;
  const uint32_t out_arg = ctx.r1.u32 + 124;
  const uint32_t out = gw2::LoadBE32(base, out_arg);
  __imp__sub_820B7C78(ctx, base);
  if (int32_t(ctx.r3.u32) < 0) {
    return;
  }
  g_pad_diagram = gw2::LoadBE32(base, out);
  const uint32_t path = GuestString(std::string("GAME:\\") + gw2::kDiagramFile);
  const uint32_t slot = rex::system::kernel_memory()->SystemHeapAlloc(4);
  if (path == 0 || slot == 0) {
    return;
  }
  gw2::StoreBE32(base, slot, 0);
  gw2::StoreBE32(base, out_arg, slot);
  second.r4.u64 = path;
  __imp__sub_820B7C78(second, base);
  gw2::StoreBE32(base, out_arg, out);
  if (int32_t(second.r3.u32) >= 0) {
    g_keyboard_diagram = gw2::LoadBE32(base, slot);
  }
  REXLOG_INFO("Keyboard diagram: {} (controller texture {:08X}, keyboard {:08X})",
              g_keyboard_diagram != 0 ? "loaded" : "missing, the controller diagram stays", g_pad_diagram,
              g_keyboard_diagram);
}

// Adds a menu text item: r4 the rectangle, r5 a printf format and r6 on its arguments. Every help screen draws text,
// so the diagram is matched to the prompts here too.
REX_EXTERN(__imp__sub_82076E40);
REX_HOOK_RAW(sub_82076E40) {
  const bool keyboard = KeyboardPrompts();
  if (g_pad_diagram != 0 && g_keyboard_diagram != 0) {
    gw2::StoreBE32(base, kControllerTexture, keyboard ? g_keyboard_diagram : g_pad_diagram);
  }
  if (keyboard) {
    for (const GlyphString& glyph : kGlyphStrings) {
      if (ctx.r5.u32 != glyph.address) {
        continue;
      }
      const std::string label = LabelFor(glyph.button);
      if (!label.empty()) {
        if (const uint32_t text = GuestString((glyph.leading_space ? " " : "") + label + " "); text != 0) {
          ctx.r5.u64 = text;
        }
      }
      break;
    }
  }
  __imp__sub_82076E40(ctx, base);
}
