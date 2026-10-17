# 08 Input: gamepad, keyboard, mouse

The game's own input (controller, keyboard emulation) is entirely ReXGlue's SDL input system. This project adds
UI-side input only: launcher/menu navigation, hotkeys, cursor hiding, and input gating while overlays are open.

## Game input (SDK)
- Controller: automatic through SDL (first boot log: "Xbox One S Controller" detected). A missing `gamecontrollerdb.txt` is only a warning.
- Keyboard/mouse emulation: SDK cvar `mnk_mode` ("Enable keyboard/mouse controller emulation"); the Controls page lists `Input/Keybinds/Controller` entries read-only.
- Rumble: not implemented at this HEAD. A branch `feature/controller-rumble` has one commit of temporary diagnostic logging (`3e9d942`) and is not part of the reference.
- Shutdown order: input drivers must be shut down before the window is destroyed, see [13](13-crash-hardening-threading-memory.md).

## Gamepad into ImGui (`REUSABLE_INFRASTRUCTURE`, `PLATFORM_*`)
`gw2::LauncherGamepad` (declared in `src/launcher_platform.h`, implemented per platform):
- Used by the launcher and the cheat menu while ImGui owns navigation. Sets `ImGuiBackendFlags_HasGamepad`.
- Buttons: Start, Back, face buttons (A/B/X/Y), D-pad; left stick as four analog keys with the XInput left-stick dead zone (7849) and a 0.1 activation threshold.
- Windows: dynamically loads `xinput1_4.dll`/`1_3`/`9_1_0`; polls the connected slot, scans for a pad once per second (polling empty slots is slow).
- Linux: SDL gamepad; `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` on first scan, `SDL_QuitSubSystem` in `Release`; SDL refcounts so the game's own input is unaffected. Opens the first gamepad.
- Navigation conventions: A/Enter accept, B/Escape back, Start = Play (launcher footer: Enter Play, Esc Quit, Ctrl+S Save).
- Port: copy; the only coupling is ImGui and (Windows) `trg::ShiftHeld`.

## Hotkeys
| Key | Owner | Behavior | File |
| --- | --- | --- | --- |
| F11 | `WindowHost` listener, z-order 1000 | Toggle Windowed/Borderless in game; launcher-only toggle in launcher; key is marked handled so the game never sees it; ignores key repeat (`prev_state()`) | `src/window_host.cpp` |
| F10 | `CheatMenu` key listener (z-order 1000, above ImGui drawer 64) | Open/close cheat menu; Escape closes; consumed before the drawer/game | `src/cheat_menu.cpp` |
| F7 | ReXApp's achievements overlay hook | `CreateAchievementsOverlay()` is overridden to toggle the project's own overlay; ignored while the launcher is open | app header |
| F2 | ReXGlue keybind system (`rex/ui/keybinds.h`) used by the frame counter | Toggle counter, not saved | `src/frame_counter.cpp` |
| F3, backtick, F4 | ReXGlue overlays (debug, console, settings) | Can still open over the launcher (known limit) | SDK |

Z-order convention: app listener 0, ImGui drawer 64, project listeners 1000 so hotkeys are consumed first.
`GENERIC_REXGLUE` pattern: `window->AddInputListener(&listener, z)` and `e.set_handled(true)`.

## Gating game input while overlays are open
- `rex::input::InputSystem::SetActiveCallback(gw2::GameInputActive)` replaces ReXApp's own callback (the project's version repeats it): `GameInputAllowed(cheat_menu_open, achievements_holds_input, pointer_over_overlay)` in `src/achievements_overlay_rules.h`.
- Cheat menu: while open the game reads nothing; after closing, the game gets input back only once everything pressed in the menu is released (`CloseGate`, timeout 1 s so an unplugged pad cannot block the game) - so the B/Escape that closes it is never also a game press.
- Achievements overlay: the guest's `XInputGetState` results are filtered in the hook (`FilterGuestInput`): the whole pad is neutral while open, the left stick/D-pad scrolls, B closes (the real B is read from the unfiltered state), and a B still held after closing is cleared from the next polls. `GEOMETRY_WARS_SPECIFIC` hook address, `GAME_ADAPTATION` technique.
- A pointer over an ImGui overlay keeps clicks from the game.

## Mouse and cursor
- `CursorHider` (`src/window_host.h`) is a pure state machine: in fullscreen gameplay with focus, the cursor hides after `kCursorHideDelay` (5 s) without motion; any motion shows it. Not active in windowed mode, without focus, or while the launcher is open (`gameplay_active` callback).
- The game owns the idle timer and uses `Window::CursorVisibility::kVisible/kHidden`, not the SDK's `kAutoHidden`. `OnMouseMove`, `OnGotFocus`, `OnLostFocus` and an ImGui dialog tick (`CursorTick::OnDraw`, run every frame on the UI thread) drive `UpdateCursor`. The cursor is restored on window closing and on `DetachWindowHost`.
- Origin `16a97f6`. Tests: `tests/window_host_tests.cpp` (state machine). Runtime behavior on real windows was pending a manual test when written (`UNCERTAIN`).
- Launcher mouse: TRG; pointer clicks over overlays are not forwarded to the game.

## Focus
Focus gates cursor hiding; there is no code that pauses the game on focus loss (SDK behavior). Shift-at-start needs focus on Linux ([05](05-platform-linux.md)).
