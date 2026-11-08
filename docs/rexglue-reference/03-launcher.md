# 03 Launcher

Long-form behavior doc: `docs/launcher.md`. This file is the architecture and porting view.

## Architecture (`REUSABLE_INFRASTRUCTURE` unless noted)

The launcher is [TRG Launcher](https://github.com/TekRantGaming/trg-launcher) (submodule `third_party/trg-launcher`,
pin `1a6ef30`, v1.2.0) drawn as an ImGui dialog inside the game's own ReXGlue window and ImGui frame. There is no
separate process or window. Consequences: it shares one ImGui context (so the CMake `gw2_imgui` shim, see
[02](02-build-system.md)), it can be shown before the runtime exists (for installation) or after (to read the
game's achievements), and "starting the game" means closing the dialog and calling `ReXApp::LaunchModule`.

| Layer | Files | Notes |
| --- | --- | --- |
| Entry + callbacks | `src/launcher.cpp`, `src/launcher.h` (`ShowLauncher`, `LauncherCallbacks`: play, restart, quit, window handle, fullscreen get/set) | The app owns the window; the launcher only calls back |
| Pure decision logic | `LauncherWanted`, `PlayGate` (`src/launcher_play.h`), `src/multisampling.h`, `src/launcher_placement.h` | Header-only, unit-tested with no ReXGlue |
| Settings model | `src/launcher_settings.{h,cpp}` (`GameSettings`), `src/launcher_options.{h,cpp}` (choice tables), `src/launcher_cvars.{h,cpp}` | Pending edits over saved over shipped defaults; `CvarAccess` interface lets tests fake the cvar registry |
| Platform | `src/launcher_platform.{h,cpp}`, `launcher_platform_win32.cpp`, `launcher_platform_linux.cpp` | Monitors, work areas, window placement, icon, relaunch, fonts, gamepad, file picker availability |
| Game files | `src/game_files.{h,cpp}`, `src/stfs.{h,cpp}`, `src/sha256.h` | `CheckGameFiles`, package reader and installer |
| Achievements page | `src/launcher_achievements.{h,cpp}` | Reads the same `AchievementManager` as the overlay |

## Startup decision

`LauncherWanted(game_ready, show_setting, shift_held, skip_once)` (`src/launcher_settings.cpp`): not ready always
shows (it is the only way to install); otherwise setting On or Shift held, unless a window restart set
`gw2_skip_launcher` (consumed once by `TakeSkipLauncher`). Two call sites in `OnFinalizePaths`:
- files not valid: launcher shown before the runtime exists, `play` resumes startup with `resume(defaults)`;
- files valid: `show_launcher_after_runtime_`, shown from `LaunchModule`.

Shift on Linux works only if SDL sees the key while the window has focus ([05](05-platform-linux.md)). Windows reads
it through `trg::ShiftHeld`.

## Game installation and validation (`GAME_ADAPTATION`)

- `CheckGameFiles` is the single readiness check used by the launcher, PLAY, install verification and startup: `default.xex` size + SHA-256 (`kSupportedGame`) and `Data.bin` present. States: missing, valid, different version, read error. Change the fingerprint, title id (`kTitleId`), content type and required files for a new game.
- Install from an Xbox 360 STFS package (`src/stfs.cpp`; Geometry Wars is distributed as an STFS Xbox Live Arcade package, other titles may not be): title and content type check, extract into `game-install-temp`, verify via `CheckGameFiles`, swap with the previous folder kept until the swap completes. Cancel, damaged package or wrong version leaves the old game intact; foreign folders are refused; path traversal rejected (tests in `tests/launcher_tests.cpp`).
- Dev tools with the same reader: `tools/extract_stfs.{ps1,sh}`, `tools/StfsExtract.cs`, `tools/extract_stfs/main.cpp`.
- Game folder: `game` beside the executable (beside the AppImage on Linux), `--game_data_root` overrides it.
- Reusable architecture: fingerprint validation, temporary extraction, verification, safe swap with the previous folder kept, rollback on failure. The STFS reader is title/distribution dependent: reuse it as is (new fingerprint, title id, content type) when the new game is also in a compatible STFS package; otherwise implement the matching importer and keep the rest.

## Settings, Save, PLAY, Apply

- File: `<user_data>/launcher/settings.toml`, edited through TRG's text settings (keeps comments/unknown lines, atomic replace, only changed keys written). Defaults: shipped `geometry_wars_retro_evolved_2.toml`, then ReXGlue's registered default.
- `GameSettings` holds shipped, saved, pending and "window created" value maps. `Capture()` reads cvars; `CaptureLateRegistered()` picks up Xenos graphics cvars that register after `Capture` (the cvars of the GPU plugin do not exist at `OnPreSetup`).
- `ManagedSettings()` is the list of keys the launcher owns; `IsWindowSetting` marks keys that need a new window (size, monitor, VSync); `IsLauncherPlacementSetting` marks the launcher's own placement keys, which `Reset all` must not touch.
- `PlayGate::Request`: rejects while an install runs or files are not ready; saves once; a failed save keeps the launcher open. A 0.4 s "Settings saved. Starting game..." state with controls disabled follows, then `Apply()` assigns changed cvars; `Apply()` returns true when ReXGlue reports restart-only cvars changed (or `WindowChanged()`), in which case `RestartInNewWindow()` relaunches with `--gw2_skip_launcher=true` and the old process leaves via `std::_Exit(0)` after flushing logs (deferred so the launcher saves its placement first).
- Display mode is applied separately (`ApplyDisplayMode`) after the launcher has left, so the launcher's window is never changed by the game's Display setting (see [07](07-display-window-frame-pacing.md)).
- Reset all (About) restores shipped/default values as pending; it never touches profile, scores, achievements or launcher placement.
- Port: copy `GameSettings` + `CvarAccess` + tests. Replace the key list and the choice tables in `launcher_options.cpp`. Pitfall: the SDK flags restart-only cvars as pending when a config sets them; call `SettleStartupCvars` equivalent after loading the player file.

## Pages and the options they map to
Play, Display, Graphics, Gameplay, Cheats, Controls, Achievements, About. Tables (cvar names, values) are in
`docs/launcher.md`; the generic mapping reusable by other ReXGlue games:

| UI | Cvar |
| --- | --- |
| Display mode (Windowed / Borderless Fullscreen) | `fullscreen` |
| Window size, Monitor | `window_width`, `window_height`, `monitor` |
| VSync | `d3d12_allow_variable_refresh_rate_and_tearing` (Windows), `vulkan_allow_present_mode_immediate` (Linux, inverted semantics: Off allows tearing) |
| Aspect Ratio | `gw2_aspect_ratio`; applied at start by `ApplyAspectRatio` (sets `video_mode_width`, `video_mode_height`, `present_letterbox`), a window-class setting, so a change restarts once. The retired `present_letterbox` line is removed from the player file |
| Render resolution | `resolution_scale` (1-4) |
| Anti-aliasing | `swap_post_effect` (`none`/`fxaa`/`fxaa_extreme`) |
| Texture filtering | `anisotropic_override` |
| Keyboard emulation | `mnk_mode` |
| Multisampling | project cvar `gw2_msaa_compatibility` -> `native_2x_msaa` ([06](06-graphics-vulkan.md)) |

`GENERIC_REXGLUE` mapping; the selection of exposed values is a product decision. Window sizes offered are filtered to
the selected display at its scale (`FitWindowSize`, 90% of work area, logical pixels).

## Window and display scaling
`FitWindowSize` caps requested logical size to 90% of the selected display's work area divided by display scale;
`FitWindowToDisplay` rewrites the cvars but the saved player value stays as requested, so a smaller display does not
permanently shrink the setting. ReXGlue converts logical to physical sizes; the launcher must not multiply by scale.
`MonitorForSetting` numbers monitors as ReXGlue does: primary first on Windows (`std::stable_partition`), SDL order on
Linux.

## Launcher window state (history: `b46a22f`)
- Problem: launcher position/size/maximized and F11 fullscreen state were coupled to the game's Display mode.
- Symptoms: after playing, the launcher reopened with the game's fullscreen/window state; F11 in the launcher changed the game's saved Display mode.
- Solution: separate keys `gw2_launcher_x/y/width/height/maximized/fullscreen` in the same player file; F11 in the launcher toggles only the launcher; Display mode is applied only when PLAY hands the window to the game; leaving for Play is not recorded as an F11 by the player (the saved fullscreen choice stays); nothing is saved while fullscreen; `PlacementTracker` saves ~0.5 s after the window settles; off-screen windows are ignored (`PlacementIsVisible`, minimum 100x50 visible).
- Files: `src/launcher_placement.h`, `src/launcher.cpp`, `src/geometry_wars_retro_evolved_2_app.h` (`ToggleWindowMode`, `PlaceLauncherWindow`), `tests/launcher_placement_tests.cpp`, `tests/launcher_settings_tests.cpp`.
- Platform placement: Windows installs a `WH_CALLWNDPROC` hook (`InstallPreShowPlacement`) that moves/maximizes the window on its first `WM_SHOWWINDOW`, before anything is visible. Linux has no hook: `ApplyWindowPlacement` runs after SDL shows the window (default size may flash; under Wayland the position cannot be restored).
- Copy directly (REUSABLE_INFRASTRUCTURE): placement model and tests; replace persistence keys if renamed.

## Input in the launcher
TRG handles keyboard and mouse. The controller is fed through `gw2::LauncherGamepad` (`src/launcher_platform.h`):
XInput on Windows (dynamic `xinput1_4/1_3/9_1_0`), SDL gamepad on Linux, into ImGui gamepad keys with a left-stick
dead zone and a once-per-second scan for a controller. See [08](08-input-gamepad-mouse.md).

## Launcher without graphics (Linux)
The ImGui launcher lives in the ReXGlue window, which needs Vulkan. When `ReXApp::SetupPresentation` fails, the app opens a
CPU-drawn SDL window instead (`gw2::RunLauncherWithoutGraphics`, `src/launcher_platform_linux.cpp`; the game-files wording is
`gw2::DescribeGameFilesWithoutGraphics`, `src/graphics_fallback.h`, covered by `tests/launcher_tests.cpp`). Its first screen
uses neutral wording ("Setup required", "Vulkan setup required") and the failure text appears only after PLAY. Windows has no such code.

## Linux file chooser behavior
TRG's `platform.cpp` runs `zenity --file-selection` and falls back to `kdialog`; folders and URLs open through
`xdg-open`. `PackagePickerAvailable()` (`OnPath("zenity") || OnPath("kdialog")`) lets the Play page say "No file picker
found" / "Install zenity or kdialog to choose the package, then try again." (`src/launcher.cpp`) and stay unable to
install. None of these is bundled in the AppImage. Gap: there is no built-in ImGui file browser, so a Linux machine
without either tool cannot install from the launcher (`CONFIRMED` from code). A port could add a text path field or a
drag-and-drop as a fallback.

## Fonts
`LoadLauncherFonts` merges Latin-1 + General Punctuation. Windows loads Segoe UI regular/semibold/bold from the
Windows directory; Linux probes a list of Noto Sans / DejaVu / Liberation paths (`FindUiFonts`). Both fall back to the
ImGui default if absent.

## About page and diagnostics
Rows: game files folder, saves/settings folder (with `UserDataFolderHelp` saying portable/system fallback/explicit
root), settings file (saves pending first, then opens it), Reset, port version (`GW2_PORT_VERSION`), libraries,
game, repository text. Window title shows FPS ([07](07-display-window-frame-pacing.md)).

## Reusability summary
| Feature | Reusable almost directly |
| --- | --- |
| Launcher inside the game window + callbacks | Yes |
| `GameSettings`, PLAY gate, restart decision | Yes (change key tables) |
| Placement tracker + pre-show hook | Yes |
| Install flow (fingerprint, temp extraction, verify, safe swap, rollback) | Yes |
| STFS reader | Only for STFS-distributed titles (new fingerprint) |
| Achievements page | Yes if you use `AchievementManager` |
| Cheats page / F10 menu | Architecture only ([16](16-reusable-game-features.md)) |
| Linux picker | Yes, with the missing-tool limitation above |
| Controller navigation | Yes (platform code is self-contained) |

## Pitfalls
- Do not draw launcher state into game cvars before PLAY; that was the cause of `b46a22f`.
- The launcher creates the window before the runtime in the "not installed" case; any code assuming `runtime()` is non-null in launcher callbacks breaks there (`LauncherGameContext` is empty).
- Verify: `tools/test.ps1` (launcher, settings, placement suites), `gw2_restart_state_tests` for the restart decision, and the manual list in `docs/launcher.md`.
