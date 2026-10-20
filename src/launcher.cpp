#include "launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/version.h>

#include "trg/launcher.h"
#include "trg/platform.h"

#include "cheats.h"
#include "dpi.h"
#include "frame_limiter.h"
#include "game_files.h"
#include "imgui_gamepad.h"
#include "keybinds.h"
#include "launcher_achievements.h"
#include "launcher_options.h"
#include "launcher_placement.h"
#include "launcher_play.h"
#include "launcher_platform.h"
#include "launcher_quit.h"
#include "launcher_settings.h"
#include "log_filter.h"
#include "update_config.h"
#include "updater/update_ui.h"

namespace gw2 {
namespace {

// The violet of the Geometry Wars logo (#943CFF).
const ImVec4 kAccent = ImVec4(0.580f, 0.235f, 1.0f, 1.0f);
constexpr double kPlayFeedbackSeconds = 0.4;
constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
constexpr char kAuthorUrl[] = "https://github.com/saulob";
constexpr char kRepositoryUrl[] = "https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled";
constexpr char kAuthorText[] = "BY SAULOB";
constexpr char kShowLauncherHelp[] = "Off starts the game directly. Hold Shift while starting to bring it back.";
constexpr char kWindowSizeHelp[] =
    "The game window's size in Windowed mode. A new window: the game restarts once when you press Play.";
constexpr char kMonitorHelp[] = "The display the game opens on. A new window: the game restarts once when you press Play.";
constexpr char kVSyncHelp[] = "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.";
constexpr char kShowFpsHelp[] =
    "Shows the game's frame rate in the window title: Geometry Wars: Retro Evolved 2 | FPS: 60.00";
constexpr char kShowResolutionHelp[] = "Shows the game's render resolution in the window title";
constexpr char kFrameRateHelp[] =
    "60 FPS is the original game rate. Higher frame rates use automatic visual interpolation while gameplay simulation "
    "remains at 60 Hz.";
constexpr char kFrameCounterHelp[] = "Shows the game's frame rate in the corner. F2 toggles it while playing.";
constexpr char kCheatsHelp[] =
    "Press F10 while playing to open the Cheats menu. Changes made there apply only to the current session.";

trg::Theme LauncherTheme() {
  return trg::Theme::Midnight().WithAccent(kAccent);
}

constexpr float kHeaderArtAspect = 1600.0f / 180.0f;

trg::HeaderPainter LauncherHeader(ImTextureID art, float art_aspect) {
  return [art, art_aspect](const trg::HeaderContext& c) {
    c.draw->AddRectFilledMultiColor(c.min, c.max, IM_COL32(10, 18, 34, 255), IM_COL32(44, 18, 80, 255),
                                    IM_COL32(8, 26, 44, 255), IM_COL32(8, 10, 18, 255));
    if (art == ImTextureID{}) {
      return;
    }
    const float height = c.max.y - c.min.y;
    const float width = height * art_aspect;
    const ImVec2 top_left(c.max.x - width, c.min.y);
    c.draw->AddImage(ImTextureRef(art), top_left, c.max);
  };
}

std::unique_ptr<rex::ui::ImmediateTexture> LoadHeaderArt(rex::ui::ImmediateDrawer* drawer) {
  if (!drawer) {
    return nullptr;
  }
  const std::filesystem::path path =
      rex::filesystem::GetExecutableFolder() / "assets" / "launcher" / "header-art.png";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return nullptr;
  }
  const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  const std::vector<uint8_t> pixels = rex::ui::DecodeImageRGBA(encoded.data(), encoded.size(), width, height);
  if (pixels.empty() || width <= 0 || height <= 0) {
    REXLOG_WARN("Launcher header art could not be decoded: {}", Utf8(path));
    return nullptr;
  }
  return drawer->CreateTexture(uint32_t(width), uint32_t(height), rex::ui::ImmediateTextureFilter::kLinear, false,
                               pixels.data());
}

std::string DisplayUrl(const char* url) {
  const std::string text = url;
  const size_t scheme = text.find("://");
  return scheme == std::string::npos ? text : text.substr(scheme + 3);
}

trg::Fonts g_fonts;
float g_font_density = 1.0f;

const char* StateName(GameFileState state) {
  switch (state) {
    case GameFileState::kMissing:
      return "missing";
    case GameFileState::kValid:
      return "valid";
    case GameFileState::kDifferentVersion:
      return "different version";
    case GameFileState::kReadError:
      return "read error";
  }
  return "unknown";
}

GameFileState CheckAndLogGameFiles(const std::filesystem::path& game_dir) {
  const GameCheck check = CheckGameFiles(game_dir);
  REXLOG_INFO("Game files in {}: {} (default.xex {} bytes, SHA-256 {})", Utf8(game_dir), StateName(check.state),
              check.xex_size, check.xex_sha256.empty() ? std::string("not read") : check.xex_sha256);
  return check.state;
}

const char* StatusTitle(GameFileState state) {
  switch (state) {
    case GameFileState::kValid:
      return "Ready to play";
    case GameFileState::kMissing:
      return "Game files needed";
    case GameFileState::kDifferentVersion:
      return "Different version of Geometry Wars: Retro Evolved 2";
    case GameFileState::kReadError:
      return "Could not read the game files";
  }
  return "";
}

const char* StatusDetail(GameFileState state) {
  switch (state) {
    case GameFileState::kValid:
      return "";
    case GameFileState::kMissing:
      return "Install the game from your own Xbox 360 package.";
    case GameFileState::kDifferentVersion:
      return "Install the supported version from your own package.";
    case GameFileState::kReadError:
      return "Check that the game folder can be read, then reinstall.";
  }
  return "";
}

// Written by the install job, read once the Task has finished.
struct InstallOutcome {
  InstallResult result;
  GameFileState files = GameFileState::kMissing;
};

std::vector<AchievementEntry> LoadAchievementEntries(const rex::system::AchievementManager& manager) {
  std::vector<AchievementEntry> entries;
  for (rex::system::AchievementInfo& info : manager.ListAchievements()) {
    const bool unlocked = manager.IsUnlocked(info.id);
    entries.push_back({std::move(info), unlocked});
  }
  return entries;
}

std::vector<trg::Option> ToTrgOptions(const std::vector<SettingChoice>& choices) {
  std::vector<trg::Option> options;
  options.reserve(choices.size());
  for (const SettingChoice& choice : choices) {
    options.push_back({choice.value, choice.label});
  }
  return options;
}

void DrawFailure(trg::Ui& ui, const std::string& title, const std::string& text) {
  ui.Spacer(4);
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopStyleColor();
  ui.Help(text.c_str());
  ui.Spacer(4);
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                 LauncherGameContext game, LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        settings_(settings),
        game_(game),
        callbacks_(std::move(callbacks)),
        monitors_(ListMonitors()),
        graphics_adapters_(ListGraphicsAdapters()) {
    files_ = CheckAndLogGameFiles(paths_.game_dir);
    if (game_.achievements) {
      entries_ = LoadAchievementEntries(*game_.achievements);
    }
    if (game_.runtime && game_.immediate_drawer) {
      icons_ = std::make_unique<rex::ui::AchievementIconCache>(game_.immediate_drawer, game_.runtime);
    }
    header_art_ = LoadHeaderArt(paths_.immediate_drawer);
    if (SelectedAspectRatio() != AspectRatio::k16x9) {
      SelectWindowSizeForAspect();
    }
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    updater_ = std::make_unique<trg::update::UpdateController>(MakeUpdateConfig(PortFolder()));
    updater_->StartCheck();
    RestorePlacement();
    REXLOG_INFO("Launcher shown {} the runtime", game_.runtime ? "after" : "before");
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) {
      return;
    }
    PollInstall();
    PollUpdate();
    TrackPlacement();
    if (!nav_taken_) {
      saved_nav_flags_ = io.ConfigFlags & kNavFlags;
      saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
      io.ConfigFlags |= kNavFlags;
      nav_taken_ = true;
    }
    gamepad_.Update(io);
    if (play_.starting() && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds) {
      StartGame();
      return;
    }
    if (play_.starting()) {
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
    ImGui::BeginDisabled(play_.starting());
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (result == trg::Result::kNone && !play_.starting()) {
      HandleUpdateAction(
          trg::update::DrawUpdateBanner(updater_->snapshot(), launcher_->config().theme, g_fonts));
    }
    if (play_.starting()) {
      DrawStartingMessage();
    }
    switch (result) {
      case trg::Result::kPlay:
        RequestPlay();
        break;
      case trg::Result::kQuit:
        if (!play_.starting() && LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
          Finish(callbacks_.quit);
        }
        break;
      case trg::Result::kNone:
        break;
    }
  }

  void OnClose() override {
    if (updater_) {
      updater_->Cancel();
      if (updater_->Busy()) {
        updater_.release();
      }
    }
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    if (updater_) {
      updater_->Cancel();
      if (updater_->Busy()) {
        // Do not join a network call on the UI thread when the launcher leaves.
        updater_.release();
      }
    }
    if (!skip_finish_save_) {
      SavePlacement(placement_.Flush());
    }
    REXLOG_INFO("Launcher closed");
    Close();
    if (action) {
      action();
    }
  }

  // PLAY saves the settings once here, and only then starts the transition. A failed save keeps the launcher open.
  void RequestPlay() {
    if (play_.starting() || install_.running()) {
      return;
    }
    files_ = CheckAndLogGameFiles(paths_.game_dir);
    switch (play_.Request(false, files_ == GameFileState::kValid, [this] { return launcher_->Save(); })) {
      case PlayRequest::kStarting:
        play_started_ = ImGui::GetTime();
        launcher_->SetStatus("", 0.0);
        REXLOG_INFO("Settings saved, starting the game");
        break;
      case PlayRequest::kFilesNotReady:
        launcher_->SetStatus(std::string(StatusTitle(files_)) + ". Install the game files first.", 6.0);
        break;
      case PlayRequest::kBusy:
      case PlayRequest::kSaveFailed:
        break;
    }
  }

  // After the transition: applies the pending settings, then starts the game here or in a new window.
  void StartGame() {
    const bool applied_restart = settings_.Apply();
    if (settings_.WindowChanged() || applied_restart) {
      if (callbacks_.restart && callbacks_.restart()) {
        Finish(nullptr);
        return;
      }
      play_.Cancel();
      launcher_->SetStatus("Could not start the game in a new window. Press Play to try again.", 8.0);
      return;
    }
    LeaveLauncherFullscreen();
    SavePlacement(placement_.Flush());
    RestoreStartupWindow();
    ApplyGameDisplayMode();
    Finish(callbacks_.play);
  }

  void* WindowHandle() const { return callbacks_.window_handle ? callbacks_.window_handle() : nullptr; }

  bool WindowFullscreen() const { return callbacks_.window_fullscreen && callbacks_.window_fullscreen(); }

  void RestorePlacement() {
    if (const auto placed = TakePreShowResult()) {
      startup_ = placed->startup;
      tracking_ = true;
      placement_ = PlacementTracker(placed->applied);
      return;
    }
    void* window = WindowHandle();
    const auto state = QueryWindowState(window);
    if (!state) {
      return;
    }
    const WindowPlacement saved = ReadPlacement(settings_);
    const std::vector<Rect> areas = WorkAreas();
    const bool restorable = PlacementIsVisible(saved, areas);
    tracking_ = true;
    was_fullscreen_ = WindowFullscreen();
    placement_ = PlacementTracker(restorable ? std::optional<WindowPlacement>(saved) : std::nullopt);
    if (was_fullscreen_) {
      return;
    }
    startup_ = WindowPlacement{state->rect, state->maximized};
    if (restorable) {
      ApplyWindowPlacement(window, saved);
      REXLOG_INFO("Launcher window restored{}", saved.maximized ? " (maximized)" : "");
    }
  }

  void TrackPlacement() {
    if (!tracking_) {
      return;
    }
    const bool fullscreen = WindowFullscreen();
    if (was_fullscreen_ && !fullscreen && placement_.Current() && PlacementIsSet(*placement_.Current())) {
      ApplyWindowPlacement(WindowHandle(), *placement_.Current());
    }
    was_fullscreen_ = fullscreen;
    if (const auto state = QueryWindowState(WindowHandle())) {
      placement_.Observe(*state, fullscreen, ImGui::GetTime());
      SavePlacement(placement_.TakeSettled(ImGui::GetTime()));
    }
  }

  void SavePlacement(const std::optional<WindowPlacement>& placement) {
    if (placement && !settings_.SaveNow(PlacementValues(*placement))) {
      REXLOG_WARN("Could not save the launcher window placement");
    }
  }

  void ApplyGameDisplayMode() {
    settings_.ApplyDisplayMode();
    const bool fullscreen = settings_.GetBool(kFullscreenKey, false);
    if (callbacks_.set_window_fullscreen && WindowFullscreen() != fullscreen) {
      callbacks_.set_window_fullscreen(fullscreen);
    }
  }

  // Leaving for Play is not an F11 by the player: the tracker is not told, so the saved fullscreen choice stays.
  void LeaveLauncherFullscreen() {
    if (WindowFullscreen() && callbacks_.set_window_fullscreen) {
      callbacks_.set_window_fullscreen(false);
    }
  }

  // The game keeps the window it was created with, not the launcher's saved placement.
  void RestoreStartupWindow() {
    if (startup_ && !WindowFullscreen()) {
      ApplyWindowPlacement(WindowHandle(), *startup_);
    }
  }

  void DrawStartingMessage() {
    constexpr const char* kText = "Settings saved. Starting game...";
    ImFont* font = g_fonts.semibold ? g_fonts.semibold : ImGui::GetFont();
    const float size = g_fonts.size * 1.25f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kText);
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - text_size.x) * 0.5f, vp->Pos.y + (vp->Size.y - text_size.y) * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(kAccent), kText);
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    c.save_on_play = false;
    c.theme = LauncherTheme();
    c.fonts = g_fonts;
    c.branding.title = "GEOMETRY WARS: RETRO EVOLVED 2";
    c.branding.subtitle_segments = {
        {"PC PORT   \xC2\xB7   ", ""},
        {"v" GW2_PORT_VERSION "   \xC2\xB7   ", ""},
        {kAuthorText, kAuthorUrl},
    };
    c.branding.subtitle_color = kAccent;
    const float art_aspect =
        header_art_ ? float(header_art_->width) / float(header_art_->height) : kHeaderArtAspect;
    c.branding.background = LauncherHeader(header_art_ ? reinterpret_cast<ImTextureID>(header_art_.get())
                                                       : ImTextureID{},
                                           art_aspect);
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
    for (const PageId page : kPageOrder) {
      c.pages.push_back(MakePage(page));
    }
    c.can_play = [this] {
      if (install_.running()) {
        return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      }
      if (files_ == GameFileState::kValid) {
        return trg::PlayCheck{};
      }
      return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
    };
    return c;
  }

  trg::Page MakePage(PageId page) {
    const std::string name(PageName(page));
    switch (page) {
      case PageId::kPlay:
        return {name, "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
                [this] { return files_ != GameFileState::kValid; }};
      case PageId::kDisplay:
        return {name, "Window, monitor and how the picture fits your screen.",
                [this](trg::Ui& ui) { PageDisplay(ui); }, nullptr};
      case PageId::kGraphics:
        return {name, "Render resolution, anti-aliasing and texture filtering.",
                [this](trg::Ui& ui) { PageGraphics(ui); }, nullptr};
      case PageId::kGameplay:
        return {name, "Frame rate limit, the window title and the corner counter.",
                [this](trg::Ui& ui) { PageGameplay(ui); }, nullptr};
      case PageId::kControls:
        return {name, "Controller and keyboard.", [this](trg::Ui& ui) { PageControls(ui); }, nullptr};
      case PageId::kCheats:
        return {name, "Optional gameplay modifiers", [this](trg::Ui& ui) { PageCheats(ui); }, nullptr};
      case PageId::kAchievements:
        return {name, "Your achievements on this PC.", [this](trg::Ui& ui) { PageAchievements(ui); }, nullptr};
      case PageId::kAbout:
        return {name, "This port, and where your files and settings live.", [this](trg::Ui& ui) { PageAbout(ui); },
                nullptr};
    }
    return {};
  }

  void PagePlay(trg::Ui& ui) {
    if (install_.running()) {
      ui.StatusCard(trg::Status::kBusy, "Installing game files", nullptr, std::max(0.0f, install_.fraction()));
    } else {
      const trg::Status status = files_ == GameFileState::kValid ? trg::Status::kReady : trg::Status::kAttention;
      const std::string dir = Utf8(paths_.game_dir);
      ui.StatusCard(status, StatusTitle(files_), files_ == GameFileState::kValid ? dir.c_str() : StatusDetail(files_));
    }
    if (!install_failure_.empty()) {
      DrawFailure(ui, install_failure_, install_failure_text_);
    }
    ui.Row("Game package", "Title ID 584108FF. The game files are copied from your own package; none are included.");
    if (!ui.TaskProgress(install_)) {
      const bool clicked = files_ == GameFileState::kValid
                               ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                               : ui.AccentButton("Install from package...");
      if (clicked) {
        BrowseAndInstall();
      }
    }
    ui.Toggle("Show this launcher", kShowLauncherHelp, kShowLauncherKey, true, "Off", "On");
    ui.EndRows();
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode", "Borderless Fullscreen fills the monitor. F11 switches while playing.", kFullscreenKey,
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", kWindowSizeHelp);
    WindowSizeCombo();
    ui.Row("Monitor", kMonitorHelp);
    MonitorCombo();
    if (ui.Combo("Aspect Ratio", "Sets the game's target rendering format and the Window size choices.", kAspectKey,
                 "16:9", ToTrgOptions(AspectRatioChoices()))) {
      SelectWindowSizeForAspect();
    }
    ui.Choice("VSync", kVSyncHelp, kVSyncKey, "true", {{"true", "Off"}, {"false", "On"}}, 2);
    ui.EndRows();
  }

  AspectRatio SelectedAspectRatio() const { return ParseAspectRatio(settings_.Get(kAspectKey, "16:9")); }

  void SelectWindowSizeForAspect() {
    const WindowSize current{settings_.GetInt(kWindowWidthKey, 0), settings_.GetInt(kWindowHeightKey, 0)};
    const MonitorInfo& display = SelectedMonitor();
    const WindowSize chosen = WindowSizeForAspect(SelectedAspectRatio(), current, [&display](WindowSize size) {
      return WindowSizeFits(size, display);
    });
    if (chosen.width != current.width || chosen.height != current.height) {
      settings_.SetInt(kWindowWidthKey, chosen.width);
      settings_.SetInt(kWindowHeightKey, chosen.height);
    }
  }

  const MonitorInfo& SelectedMonitor() const {
    static const MonitorInfo none;
    const int index = settings_.GetInt(kMonitorKey, 1);
    if (index >= 1 && index <= int(monitors_.size())) {
      return monitors_[size_t(index - 1)];
    }
    return monitors_.empty() ? none : monitors_.front();
  }

  void WindowSizeCombo() {
    const int width = settings_.GetInt(kWindowWidthKey, 0);
    const int height = settings_.GetInt(kWindowHeightKey, 0);
    const MonitorInfo& display = SelectedMonitor();
    const float scale = display.scale;
    const auto label = [scale](int w, int h) {
      if (w <= 0 || h <= 0) {
        return std::string("Default");
      }
      return WindowSizeLabel(WindowSize{w, h}, scale);
    };
    const AspectRatio aspect = SelectedAspectRatio();
    if (ImGui::BeginCombo("##window_size", label(width, height).c_str())) {
      for (const AspectWindowSize& size : kWindowSizes) {
        const bool current = size.width == width && size.height == height;
        if (size.aspect != aspect || (!current && !WindowSizeFits({size.width, size.height}, display))) {
          continue;
        }
        if (ImGui::Selectable(label(size.width, size.height).c_str(), current) && !current) {
          settings_.SetInt(kWindowWidthKey, size.width);
          settings_.SetInt(kWindowHeightKey, size.height);
        }
      }
      ImGui::EndCombo();
    }
  }

  void MonitorCombo() {
    const int current = settings_.GetInt(kMonitorKey, 1);
    if (ImGui::BeginCombo("##monitor", MonitorLabel(current, monitors_).c_str())) {
      for (int i = 1; i <= int(monitors_.size()); ++i) {
        if (ImGui::Selectable(MonitorLabel(i, monitors_).c_str(), i == current) && i != current) {
          settings_.SetInt(kMonitorKey, i);
        }
      }
      ImGui::EndCombo();
    }
  }

  void PageGraphics(trg::Ui& ui) {
    if (!graphics_adapters_.empty()) {
      ui.Combo("Graphics card",
               "The GPU that renders the game. Automatic prefers the high-performance GPU. A change restarts the game "
               "once when you press Play.",
               kGraphicsAdapterKey, kGraphicsAdapterAuto,
               ToTrgOptions(GraphicsAdapterChoices(graphics_adapters_,
                                                   settings_.Get(kGraphicsAdapterKey, kGraphicsAdapterAuto))));
    }
    ui.Combo("Render resolution",
             "Draw resolution scale for both axes. Higher is sharper and costs more GPU time. Window size is separate.",
             kRenderScaleKey, "1", ToTrgOptions(RenderScaleChoices(SelectedAspectRatio())));
    ui.Combo("Internal render resolution",
             "Draws the game at a share of the render resolution, then scales it up. Lower values reduce GPU work "
             "but are less sharp.",
             kInternalRenderScaleKey, "100", ToTrgOptions(InternalRenderScaleChoices()));
    ui.Choice("Anti-aliasing", "Post-process anti-aliasing applied to the final picture.", kAntiAliasingKey, "none",
              ToTrgOptions(AntiAliasingChoices()));
    ui.Toggle("Multisampling", "Use 2x MSAA for best performance. Try 4x only if you see graphics problems with 2x.",
              kMultisamplingKey, false, "2x MSAA", "4x MSAA (Compatibility)");
    ui.Choice("Texture filtering",
              "Forces anisotropic filtering for eligible textures. Higher values keep textures sharper at oblique "
              "angles, but increase texture sampling cost.",
              kTextureFilteringKey, "-1", ToTrgOptions(TextureFilteringChoices()), 6);
    ui.Choice("Bloom", "Controls the intensity of the game's original bloom/glow effect.", kBloomKey, "original",
              ToTrgOptions(BloomChoices()));
    ui.EndRows();
  }

  void PageGameplay(trg::Ui& ui) {
    ui.Choice("Frame rate", kFrameRateHelp, kFrameRateKey, "60", ToTrgOptions(FrameRateChoices()), 4);
    ui.Toggle("Frame rate in title", kShowFpsHelp, kShowFpsKey, kShowFpsDefault, "Off", "On");
    ui.Toggle("Render resolution in title", kShowResolutionHelp, kShowResolutionKey, kShowResolutionDefault,
              "Off", "On");
    ui.Toggle("Frame counter", kFrameCounterHelp, kShowFrameCounterKey, kShowFrameCounterDefault, "Hidden", "Shown");
  }

  void PageControls(trg::Ui& ui) {
    ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
    ui.Toggle("Keyboard", "Use the keyboard for gameplay controls together with the controller.", kKeyboardKey,
              true, "Off", "On");
    ui.Toggle("Vibration", "Controller rumble.", kVibrationKey, kVibrationDefault, "Off", "On");
    if (ui.ConfirmRow("Keyboard defaults", "Puts the keyboard controls back to their standard layout.",
                      "Reset to defaults", "Put every key back to its default?", "Reset")) {
      ResetKeyboardDefaults(ui.settings());
      ui.SetStatus("Keyboard controls reset to defaults.");
    }
    if (ui.Section("Keyboard controls", true)) {
      ui.Help("Click a key, then press the new one. Esc cancels.");
      const bool enabled = ui.settings().GetBool(kKeyboardKey, true);
      if (!enabled) {
        ImGui::BeginDisabled();
      }
      for (const KeyboardAction& action : kKeyboardActions) {
        KeyboardRow(ui, action);
      }
      if (!enabled) {
        ImGui::EndDisabled();
      }
    }
  }

  // The key table holds the ReXGlue keybind_* settings, so a row edits the same store the rest of the launcher uses.
  // The store keeps the edit pending until Save or Play.
  static void KeyboardRow(trg::Ui& ui, const KeyboardAction& action) {
    const std::string shown = KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action));
    const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);
    if (!key) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const std::string binding = KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt);
    if (binding.empty()) {
      ui.SetStatus("That key cannot be used.");
      return;
    }
    ui.settings().Set(action.key, binding);
  }

  void PageCheats(trg::Ui& ui) {
    ui.Help(kCheatsHelp);
    ui.Spacer(8);
    for (const cheats::CheatInfo& cheat : cheats::kCheats) {
      ui.Toggle(cheat.label, cheat.description, cheat.key, cheats::kCheatDefault, "Off", "On");
    }
  }

  void PageAchievements(trg::Ui& ui) {
    if (!game_.achievements) {
      ui.Paragraph(files_ == GameFileState::kValid
                       ? "Press Play to start the game. Achievements are listed here the next time the launcher opens."
                       : "Install the game to see its achievements.");
      return;
    }
    if (entries_.empty()) {
      ui.Paragraph("This game lists no achievements.");
      return;
    }
    DrawAchievementSummary(ui);
    ui.AchievementGrid(BuildAchievementCards(), 1);
  }

  void DrawAchievementSummary(trg::Ui& ui) {
    const AchievementTotals totals = TotalAchievements(entries_);
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::Text("%d / %d unlocked", totals.unlocked, totals.total);
    ImGui::SameLine();
    ImGui::TextColored(kAccent, "%dG / %dG", totals.gamerscore_earned, totals.gamerscore_total);
    ImGui::PopFont();
    const float fraction = totals.total > 0 ? float(totals.unlocked) / float(totals.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f * ui.scale()), "");
    ImGui::PopStyleColor();
    ui.Spacer(8);
  }

  std::vector<trg::AchievementCard> BuildAchievementCards() {
    std::vector<trg::AchievementCard> cards;
    cards.reserve(entries_.size());
    for (const AchievementEntry& entry : entries_) {
      trg::AchievementCard card;
      if (icons_) {
        card.icon = reinterpret_cast<ImTextureID>(icons_->GetIcon(entry.info));
      }
      card.title = entry.info.label;
      card.description = AchievementDescription(entry);
      card.points = static_cast<int>(entry.info.gamerscore);
      card.unlocked = entry.unlocked;
      cards.push_back(std::move(card));
    }
    return cards;
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Geometry Wars: Retro Evolved 2 for Xbox Live Arcade, statically recompiled to run natively on PC with the "
        "ReXGlue SDK. No game code or assets are included; the game runs from the user's own game files.");
    ui.Spacer(4);
    std::error_code ec;
    if (std::filesystem::is_directory(paths_.game_dir, ec)) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(paths_.game_dir));
    }
    ui.FolderRow("Saves and settings", UserDataFolderHelp(paths_.user_data_outcome), "Open data folder",
                 Utf8(paths_.user_data_root));
    if (ui.ButtonRow("Settings file", "Launcher choices stored as plain text.", "Show settings file") &&
        launcher_->Save()) {
      const std::filesystem::path file = LauncherSettingsPath(paths_.user_data_root);
      trg::OpenInFileManager(Utf8(std::filesystem::exists(file, ec) ? file : paths_.user_data_root));
    }
    if (ui.ConfirmRow("Reset settings", "Put launcher/game settings back to their defaults.", "Reset all settings",
                      "Put every setting back to its default?", "Reset")) {
      settings_.ResetToDefaults();
      launcher_->SetStatus("Settings reset to defaults. Save or press Play to keep them.", 6.0);
    }
    ui.Heading("Diagnostics");
    if (ui.Toggle("Detailed logging", "Keeps logs for normal sessions. Crash diagnostics are always preserved.",
                  "detailed_logging", true)) {
      rex::cvar::SetFlagByName("detailed_logging", settings_.Get("detailed_logging", "true"));
      ApplyDetailedLogging();
    }
    if (ui.ButtonRow("Logs folder", "Open the folder containing session and crash logs", "Open logs folder")) {
      OpenLogsFolder();
    }
    ui.Info("Port version", GW2_PORT_VERSION);
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "  \xC2\xB7  TRG Launcher " GW2_TRG_LAUNCHER_VERSION);
    ui.Info("Game", "Xbox Live Arcade, title 584108FF, game version 0.0.1.2");
    ui.Link(DisplayUrl(kRepositoryUrl).c_str(), kRepositoryUrl);
  }

  void OpenLogsFolder() {
    std::error_code ec;
    std::filesystem::path folder = paths_.logs_dir;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) {
      REXLOG_WARN("Could not create the logs folder {}: {}", Utf8(folder), ec.message());
      folder = folder.parent_path();
    }
    trg::OpenInFileManager(Utf8(folder));
  }

  void BrowseAndInstall() {
    if (install_.running()) {
      return;
    }
    if (!PackagePickerAvailable()) {
      install_failure_ = "No file picker found";
      install_failure_text_ = "Install zenity or kdialog to choose the package, then try again.";
      launcher_->SetStatus(install_failure_ + ". " + install_failure_text_, 8.0);
      return;
    }
    const std::string picked =
        trg::BrowseForFile("Select your Geometry Wars: Retro Evolved 2 package", {{"Xbox 360 package", "*"}});
    if (picked.empty()) {
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    outcome_ = std::make_shared<InstallOutcome>();
    const bool started = install_.Start(
        "Reading package...",
        [outcome = outcome_, package = PathFromUtf8(picked), game_dir = paths_.game_dir](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.progress = [&task](uint64_t done, uint64_t total) {
            task.Progress(static_cast<long long>(done), static_cast<long long>(total));
            return !task.cancelled();
          };
          outcome->result = InstallFromPackage(package, game_dir, hooks);
          if (outcome->result.ok()) {
            outcome->files = CheckGameFiles(game_dir).state;
          }
          return outcome->result.ok() ? std::string() : std::string(DescribeInstall(outcome->result.status).title);
        });
    if (started) {
      REXLOG_INFO("Installing game files from {}", picked);
    }
  }

  void PollUpdate() {
    if (updater_) {
      updater_->Poll();
    }
  }

  void HandleUpdateAction(trg::update::UpdateUiAction action) {
    switch (action) {
      case trg::update::UpdateUiAction::kDownload:
        updater_->StartDownload();
        break;
      case trg::update::UpdateUiAction::kRetry:
        if (updater_->RetryNeedsInstall()) {
          if (install_.running() || play_.starting()) {
            launcher_->SetStatus("Wait for the current install to finish.", 5.0);
            break;
          }
          if (!launcher_->Save()) {
            break;
          }
          SavePlacement(placement_.Flush());
          if (updater_->Retry()) {
            skip_finish_save_ = true;
            Finish(callbacks_.quit);
          }
        } else {
          updater_->Retry();
        }
        break;
      case trg::update::UpdateUiAction::kRestart:
        if (install_.running() || play_.starting()) {
          launcher_->SetStatus("Wait for the current install to finish.", 5.0);
          break;
        }
        if (!launcher_->Save()) {
          break;
        }
        SavePlacement(placement_.Flush());
        if (updater_->BeginInstall()) {
          skip_finish_save_ = true;
          Finish(callbacks_.quit);
        }
        break;
      case trg::update::UpdateUiAction::kNone:
        break;
    }
  }

  // Runs every frame so a finished install is picked up whichever page is open.
  void PollInstall() {
    std::string error;
    if (install_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    const std::shared_ptr<InstallOutcome> outcome = std::move(outcome_);
    if (!outcome) {
      return;
    }
    const InstallResult& result = outcome->result;
    const InstallMessage message = DescribeInstall(result.status);
    if (result.ok()) {
      files_ = outcome->files;
      REXLOG_INFO("Game files installed: {} files, {} bytes", result.files, result.bytes);
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    if (result.status == InstallStatus::kCancelled) {
      REXLOG_INFO("Game install cancelled");
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("Game install failed: {} ({})", message.title, result.detail);
    install_failure_ = message.title;
    install_failure_text_ = message.text;
    launcher_->SetStatus(std::string("Install failed: ") + install_failure_, 8.0);
  }

  LauncherPaths paths_;
  GameSettings& settings_;
  LauncherGameContext game_;
  LauncherCallbacks callbacks_;
  GameFileState files_ = GameFileState::kMissing;
  std::vector<AchievementEntry> entries_;
  std::unique_ptr<rex::ui::AchievementIconCache> icons_;
  std::vector<MonitorInfo> monitors_;
  std::vector<GraphicsAdapter> graphics_adapters_;
  std::unique_ptr<trg::Launcher> launcher_;
  std::unique_ptr<rex::ui::ImmediateTexture> header_art_;
  ImGuiGamepad gamepad_;
  PlayGate play_;
  bool nav_taken_ = false;
  bool done_ = false;
  std::optional<WindowPlacement> startup_;
  bool tracking_ = false;
  bool was_fullscreen_ = false;
  PlacementTracker placement_;
  double play_started_ = 0.0;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  std::string install_failure_;
  std::string install_failure_text_;
  std::shared_ptr<InstallOutcome> outcome_;
  trg::Task install_;
  std::unique_ptr<trg::update::UpdateController> updater_;
  bool skip_finish_save_ = false;
};

}  // namespace

void LoadLauncherFonts(ImFontAtlas* atlas, float density) {
  // Latin-1 plus General Punctuation.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};
  density = ValidDpiScale(density);
  g_font_density = density;
  ImFontConfig config;
  config.RasterizerDensity = density;
  const auto load = [&](const std::filesystem::path& path) -> ImFont* {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
      return nullptr;
    }
    return atlas->AddFontFromFileTTF(Utf8(path).c_str(), g_fonts.size, &config, kRanges);
  };
  const UiFontFiles fonts = FindUiFonts();
  g_fonts.regular = load(fonts.regular);
  g_fonts.semibold = load(fonts.semibold);
  g_fonts.bold = load(fonts.bold);
}

void UpdateLauncherFonts(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, float density) {
  if (!drawer || !immediate_drawer || !g_fonts.regular) {
    return;
  }
  density = ValidDpiScale(density);
  if (std::abs(density - g_font_density) < 0.001f) {
    return;
  }

  ImGuiIO& io = drawer->GetIO();
  for (ImFont* font : {g_fonts.regular, g_fonts.semibold, g_fonts.bold}) {
    if (!font) {
      continue;
    }
    const ImWchar fallback = font->FallbackChar;
    const ImWchar ellipsis = font->EllipsisChar;
    font->ClearOutputData();
    font->FallbackChar = fallback;
    font->EllipsisChar = ellipsis;
    font->CurrentRasterizerDensity = density;
    for (ImFontConfig* source : font->Sources) {
      source->RasterizerDensity = density;
    }
  }
  io.Fonts->TexIsBuilt = false;
  // The drawer holds the uploaded atlas until the immediate drawer is replaced.
  drawer->SetImmediateDrawer(nullptr);
  drawer->SetImmediateDrawer(immediate_drawer);
  g_font_density = density;
}

ImFont* LauncherRegularFont() {
  return g_fonts.regular;
}

ImFont* LauncherSemiboldFont() {
  return g_fonts.semibold;
}

ImVec4 LauncherAccent() {
  return kAccent;
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherGameContext game, LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), settings, game, std::move(callbacks));
}

}  // namespace gw2
