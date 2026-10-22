// Launcher shortcut and Controls-page behaviour: Escape never quits the launcher (it still cancels a keyboard
// capture), the footer advertises Enter/Alt+F4/Ctrl+S, and the Controls page keeps the keyboard remapping layout.
// The decisions themselves live in TRG Launcher, so its dispatch is checked from source together with the project's
// Escape filter. Run: gw2_launcher_shortcut_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "launcher_quit.h"

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadSource(const char* name) {
  return ReadFile(std::string(GW2_SRC_DIR) + "/" + name);
}

std::string ReadTrgSource(const char* name) {
  return ReadFile(std::string(GW2_TRG_SRC_DIR) + "/" + name);
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestHeaderBranding() {
  std::printf("header subtitle is PC PORT / BY SAULOB, with the GW title and accent\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "c.branding.title = \"GEOMETRY WARS: RETRO EVOLVED 2\";"));
  CHECK(Has(launcher, "{\"PC PLUS   \\xC2\\xB7   \", \"\"},"));
  CHECK(Has(launcher, "{kAuthorText, kAuthorUrl},"));
  CHECK(Has(launcher, "{kOriginalAuthorText, kOriginalAuthorUrl},"));
  CHECK(!Has(launcher, "LAUNCHER   \\xC2\\xB7"));
  CHECK(Has(launcher, "c.branding.subtitle_color = kAccent;"));
  CHECK(Has(launcher, "constexpr char kAuthorUrl[] = \"https://github.com/omenhachi\";"));
  CHECK(Has(launcher, "constexpr char kRepositoryUrl[] = \"https://github.com/omenhachi/geometry-wars-2-pc-plus\";"));
  CHECK(Has(launcher,
            "constexpr char kOriginalAuthorUrl[] = "
            "\"https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled\";"));
}

void TestEscapeDecision() {
  std::printf("Escape does not quit, an explicit Quit does\n");
  CHECK(gw2::LauncherQuitAllowed(false));
  CHECK(!gw2::LauncherQuitAllowed(true));
}

void TestProjectQuitWiring() {
  std::printf("the Quit result is filtered by Escape while a real Quit still runs\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))"));
  CHECK(Has(launcher, "Finish(callbacks_.quit);"));
  CHECK(Has(launcher, "Finish(callbacks_.play);"));
}

void TestFooterHint() {
  std::printf("the footer advertises Alt+F4, not Escape\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "c.hints = \"Enter  Play     Alt+F4  Quit     Ctrl+S  Save\";"));
  CHECK(!Has(launcher, "Esc  Quit"));
  CHECK(Has(launcher, "c.note = \"Play applies the changes. Save keeps them without starting.\";"));
}

void TestPlaySaveSemantics() {
  std::printf("Play saves then applies; Save only writes\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "c.save_on_play = false;"));
  CHECK(Has(launcher, "launcher_->Save()"));
  CHECK(Has(launcher, "settings_.Apply()"));

  const std::string trg = ReadTrgSource("launcher.cpp");
  CHECK(Has(trg, "bool Launcher::Save()"));
  CHECK(Has(trg, "config_.settings && config_.settings->Save()"));
}

void TestTrgHotkeys() {
  std::printf("TRG centralizes Enter, Ctrl+S and Escape; capture suppresses them\n");
  const std::string trg = ReadTrgSource("launcher.cpp");
  CHECK(Has(trg, "void Launcher::HandleHotkeys()"));
  CHECK(Has(trg, "if (capturing_key() || io.WantTextInput || ImGui::IsPopupOpen("));
  CHECK(Has(trg, "ImGui::IsKeyPressed(ImGuiKey_Enter, false)"));
  CHECK(Has(trg, "if (!io.NavVisible && !ImGui::IsAnyItemActive()) want_play_ = true;"));
  CHECK(Has(trg, "io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)"));
  CHECK(Has(trg, "Save();"));
  CHECK(Has(trg, "ImGui::IsKeyPressed(ImGuiKey_Escape, false)"));
  CHECK(Has(trg, "want_quit_ = true;"));
}

void TestCaptureEscape() {
  std::printf("Escape still cancels a keyboard capture without closing the launcher\n");
  const std::string trg = ReadTrgSource("launcher.cpp");
  CHECK(Has(trg, "if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))"));
  CHECK(Has(trg, "capturing_.clear();"));
  CHECK(Has(trg, "Press a key...  (Esc cancels)"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);"));
  CHECK(Has(launcher, "if (!key) {"));
}

void TestKeyboardSection() {
  std::printf("Controls page matches the keyboard section layout\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Info(\"Controller\", \"Automatic: Xbox controllers work without setup.\")"));
  CHECK(Has(launcher, "ui.Toggle(\"Keyboard\","));
  CHECK(Has(launcher, "Use the keyboard for gameplay controls together with the controller."));
  CHECK(Has(launcher, "ui.ConfirmRow(\"Keyboard defaults\""));
  CHECK(Has(launcher, "Puts the keyboard controls back to their standard layout."));
  CHECK(Has(launcher, "\"Reset to defaults\""));
  CHECK(Has(launcher, "\"Put every key back to its default?\", \"Reset\""));
  CHECK(Has(launcher, "ResetKeyboardDefaults(ui.settings())"));
  CHECK(Has(launcher, "ui.Section(\"Keyboard controls\", true)"));
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
  CHECK(Has(launcher, "ui.settings().GetBool(kKeyboardKey, true)"));
  CHECK(Has(launcher, "ImGui::BeginDisabled()"));
  CHECK(Has(launcher, "ui.KeyBindRow(action.label, nullptr, shown)"));
}

void TestFallbackLauncher() {
  std::printf("the no-graphics launcher does not treat Escape as Quit\n");
  const std::string platform = ReadSource("launcher_platform_linux.cpp");
  CHECK(!Has(platform, "SDLK_ESCAPE"));
  CHECK(Has(platform, "SDL_EVENT_WINDOW_CLOSE_REQUESTED"));
  CHECK(Has(platform, "Button::kQuit"));
}

void TestWindowClose() {
  std::printf("the window close button still follows the Quit path\n");
  const std::string app = ReadSource("geometry_wars_retro_evolved_2_app.h");
  CHECK(Has(app, "app_context().QuitFromUIThread();"));
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
}

}  // namespace

int main() {
  TestHeaderBranding();
  TestEscapeDecision();
  TestProjectQuitWiring();
  TestFooterHint();
  TestPlaySaveSemantics();
  TestTrgHotkeys();
  TestCaptureEscape();
  TestKeyboardSection();
  TestFallbackLauncher();
  TestWindowClose();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
