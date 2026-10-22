// Keyboard remapping: the launcher's action table (labels, ReXGlue cvars, defaults), capture into a ReXGlue binding
// (keys, modifiers, reserved keys), and the Keyboard defaults reset. Pure logic plus source checks; the capture widget
// and the launcher page need the running launcher. Run: gw2_keyboard_tests

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "keybinds.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(GW2_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

const gw2::KeyboardAction* Find(const char* key) {
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    if (std::string_view(action.key) == key) {
      return &action;
    }
  }
  return nullptr;
}

struct PendingStore : trg::SettingsStore {
  std::map<std::string, std::string, std::less<>> saved;
  std::map<std::string, std::string, std::less<>> pending;

  PendingStore() {
    true_value = "true";
    false_value = "false";
  }

  std::string Get(std::string_view key, std::string_view def = {}) const override {
    const auto it = pending.find(key);
    return it == pending.end() || it->second.empty() ? std::string(def) : it->second;
  }
  void Set(std::string_view key, std::string value) override { pending[std::string(key)] = std::move(value); }
  bool Save() override {
    saved = pending;
    return true;
  }
};

void TestTable() {
  std::printf("action table\n");
  CHECK(gw2::kKeyboardActions.size() == 24);
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    CHECK(std::string_view(action.key).starts_with("keybind_"));
    CHECK(action.label[0] != '\0');
    CHECK(action.default_binding[0] != '\0');
    CHECK(gw2::KeyboardBindingIsValid(action.default_binding));
    CHECK(std::string_view(action.key) != "keybind_guide");
  }
  for (std::size_t a = 0; a < gw2::kKeyboardActions.size(); ++a) {
    for (std::size_t b = a + 1; b < gw2::kKeyboardActions.size(); ++b) {
      CHECK(std::string_view(gw2::kKeyboardActions[a].key) != gw2::kKeyboardActions[b].key);
      CHECK(std::string_view(gw2::kKeyboardActions[a].label) != gw2::kKeyboardActions[b].label);
    }
  }
}

void TestMapping() {
  std::printf("ReXGlue setting, label and default\n");
  const struct {
    const char* key;
    const char* label;
    const char* default_binding;
  } kExpected[] = {
      {"keybind_a", "A", "Semicolon,Space"},
      {"keybind_b", "B", "Quote,Backspace"},
      {"keybind_x", "X", "H"},
      {"keybind_y", "Y", "L"},
      {"keybind_left_shoulder", "Left bumper", "1"},
      {"keybind_right_shoulder", "Right bumper", "3"},
      {"keybind_left_trigger", "Left trigger", "Q,I"},
      {"keybind_right_trigger", "Right trigger", "E,O"},
      {"keybind_lstick_up", "Left stick up", "W"},
      {"keybind_lstick_down", "Left stick down", "S"},
      {"keybind_lstick_left", "Left stick left", "A"},
      {"keybind_lstick_right", "Left stick right", "D"},
      {"keybind_lstick_press", "Left stick press", "F"},
      {"keybind_rstick_up", "Right stick up", "Up"},
      {"keybind_rstick_down", "Right stick down", "Down"},
      {"keybind_rstick_left", "Right stick left", "Left"},
      {"keybind_rstick_right", "Right stick right", "Right"},
      {"keybind_rstick_press", "Right stick press", "K"},
      {"keybind_dpad_up", "D-pad up", "Shift+Up"},
      {"keybind_dpad_down", "D-pad down", "Shift+Down"},
      {"keybind_dpad_left", "D-pad left", "Shift+Left"},
      {"keybind_dpad_right", "D-pad right", "Shift+Right"},
      {"keybind_back", "View / Back", "Z,Tab"},
      {"keybind_start", "Menu / Start", "X,Return"},
  };
  CHECK(sizeof(kExpected) / sizeof(kExpected[0]) == gw2::kKeyboardActions.size());
  for (const auto& expected : kExpected) {
    const gw2::KeyboardAction* action = Find(expected.key);
    CHECK(action != nullptr);
    if (!action) {
      continue;
    }
    CHECK(std::string_view(action->label) == expected.label);
    CHECK(std::string_view(action->default_binding) == expected.default_binding);
  }
}

void TestCapture() {
  std::printf("captured key and modifiers\n");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_D, false, false, false) == "D");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_Space, false, false, false) == "Space");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_UpArrow, true, false, false) == "Shift+Up");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_A, false, true, false) == "Ctrl+A");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_S, false, false, true) == "Alt+S");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_F5, true, true, true) == "Shift+Ctrl+Alt+F5");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_Keypad0, false, false, false) == "Numpad0");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_Equal, false, false, false) == "Plus");
  CHECK(gw2::KeyboardBindingFor(ImGuiKey_Semicolon, false, false, false) == "Semicolon");
  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_UpArrow, ImGuiKey_Space, ImGuiKey_F1, ImGuiKey_KeypadAdd}) {
    for (bool shift : {false, true}) {
      for (bool ctrl : {false, true}) {
        for (bool alt : {false, true}) {
          const std::string binding = gw2::KeyboardBindingFor(key, shift, ctrl, alt);
          CHECK(gw2::KeyboardBindingIsValid(binding));
        }
      }
    }
  }
}

void TestRejection() {
  std::printf("keys that cannot be bound\n");
  for (ImGuiKey key : {ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
                       ImGuiKey_LeftAlt, ImGuiKey_RightAlt}) {
    CHECK(gw2::KeyboardBindingFor(key, false, false, false).empty());
  }
  for (ImGuiKey key : {ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F7, ImGuiKey_F10, ImGuiKey_F11,
                       ImGuiKey_F12, ImGuiKey_Escape, ImGuiKey_GraveAccent}) {
    CHECK(gw2::KeyboardBindingIsReserved(key));
    CHECK(gw2::KeyboardBindingFor(key, false, false, false).empty());
    CHECK(gw2::KeyboardBindingFor(key, true, false, false).empty());
  }
  for (ImGuiKey key : {ImGuiKey_GamepadFaceDown, ImGuiKey_MouseLeft, ImGuiKey_KeypadDecimal, ImGuiKey_Oem102,
                       ImGuiKey_Menu}) {
    CHECK(gw2::KeyboardKeyName(key) == nullptr);
    CHECK(gw2::KeyboardBindingFor(key, false, false, false).empty());
  }
  CHECK(!gw2::KeyboardBindingIsValid("Escape"));
  CHECK(!gw2::KeyboardBindingIsValid("F12"));
  CHECK(!gw2::KeyboardBindingIsValid("Backtick"));
}

void TestValidity() {
  std::printf("a stored binding\n");
  for (const char* good : {"Space", "Semicolon,Space", "Shift+Up", "Ctrl+Shift+W", "NumpadPlus", "F1",
                           "Shift+Up,Shift+Down", " Z , Tab "}) {
    CHECK(gw2::KeyboardBindingIsValid(good));
  }
  for (const char* bad : {"", "NotAKey", "Shift", "Shift+", "Foo+Up", "Up,", ",Up", "Up,,Down", "Shift+Escape",
                          "Ctrl+F3", "Control+", "Return+"}) {
    CHECK(!gw2::KeyboardBindingIsValid(bad));
  }
}

void TestLabel() {
  std::printf("label shown for a binding\n");
  CHECK(gw2::KeyboardBindingLabel("Semicolon,Space") == "Semicolon or Space");
  CHECK(gw2::KeyboardBindingLabel("Shift+Up") == "Shift+Up");
  CHECK(gw2::KeyboardBindingLabel("Z,Tab") == "Z or Tab");
  CHECK(gw2::KeyboardBindingLabel("Q,I") == "Q or I");
  CHECK(gw2::KeyboardBindingLabel("") == "None");
}

void TestSaved() {
  std::printf("reading the stored binding\n");
  PendingStore store;
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    CHECK(gw2::SavedKeyboardBinding(store, action) == action.default_binding);
  }
  store.Set("keybind_a", "Shift+Up");
  CHECK(gw2::SavedKeyboardBinding(store, *Find("keybind_a")) == "Shift+Up");
  store.Set("keybind_b", "NotAKey");
  CHECK(gw2::SavedKeyboardBinding(store, *Find("keybind_b")) == "Quote,Backspace");
  store.Set("keybind_start", "F12");
  CHECK(gw2::SavedKeyboardBinding(store, *Find("keybind_start")) == "X,Return");
  CHECK(gw2::KeyboardBindingLabel(gw2::SavedKeyboardBinding(store, *Find("keybind_back"))) == "Z or Tab");
}

void TestPendingAndReset() {
  std::printf("pending edits and Keyboard defaults\n");
  PendingStore store;
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    store.pending[action.key] = action.default_binding;
    store.saved[action.key] = action.default_binding;
  }
  store.saved["gw2_show_fps"] = "true";
  store.pending["gw2_show_fps"] = "true";

  store.Set("keybind_a", "G");
  CHECK(gw2::SavedKeyboardBinding(store, *Find("keybind_a")) == "G");
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "G");

  gw2::ResetKeyboardDefaults(store);
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    CHECK(store.Get(action.key) == action.default_binding);
  }
  CHECK(store.Get("gw2_show_fps") == "true");
  CHECK(store.Save());
  CHECK(store.saved["keybind_a"] == "Semicolon,Space");
}

void TestSerialization() {
  std::printf("values a settings file can hold\n");
  const auto safe = [](std::string_view value) {
    return value.find('"') == std::string_view::npos && value.find('\\') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos && value.find('\r') == std::string_view::npos;
  };
  for (const gw2::KeyboardAction& action : gw2::kKeyboardActions) {
    CHECK(safe(action.key));
    CHECK(safe(action.default_binding));
  }
  for (ImGuiKey key : {ImGuiKey_A, ImGuiKey_Apostrophe, ImGuiKey_Backslash, ImGuiKey_UpArrow, ImGuiKey_KeypadAdd}) {
    CHECK(safe(gw2::KeyboardBindingFor(key, true, true, true)));
  }
}

void TestSources() {
  std::printf("wiring in the launcher and settings\n");
  const std::string settings = ReadSource("launcher_settings.cpp");
  CHECK(Has(settings, "#include \"keybinds.h\""));
  CHECK(Has(settings, "for (const KeyboardAction& action : kKeyboardActions)"));
  CHECK(Has(settings, "{kKeyboardKey, ValueKind::kBool}"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.KeyBindRow(label, help, shown)"));
  CHECK(Has(launcher, "ResetKeyboardDefaults(ui.settings())"));
  CHECK(Has(launcher, "KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt)"));
  CHECK(Has(launcher, "KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action))"));
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
  CHECK(Has(launcher, "\"Keyboard defaults\""));
  CHECK(Has(launcher, "ui.Toggle(\"Keyboard\","));
  CHECK(Has(launcher, "ui.settings().GetBool(kKeyboardKey, true)"));
  CHECK(Has(launcher, "ImGui::BeginDisabled()"));
}

void TestHotkeySeparation() {
  std::printf("port hotkeys do not depend on mnk_mode\n");
  CHECK(Has(ReadSource("frame_counter.cpp"), "rex::ui::RegisterBind(\"bind_gw2_frame_counter\", \"F2\""));
  CHECK(Has(ReadSource("cheat_menu.cpp"), "rex::ui::VirtualKey::kF10"));
  CHECK(Has(ReadSource("window_host.cpp"), "rex::ui::VirtualKey::kF11"));
  CHECK(Has(ReadSource("geometry_wars_retro_evolved_2_app.h"), "CreateAchievementsOverlay"));
  for (const char* file : {"frame_counter.cpp", "cheat_menu.cpp", "window_host.cpp", "achievements_overlay.cpp"}) {
    CHECK(!Has(ReadSource(file), "mnk_mode"));
  }
}

}  // namespace

int main() {
  TestTable();
  TestMapping();
  TestCapture();
  TestRejection();
  TestValidity();
  TestLabel();
  TestSaved();
  TestPendingAndReset();
  TestSerialization();
  TestSources();
  TestHotkeySeparation();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
