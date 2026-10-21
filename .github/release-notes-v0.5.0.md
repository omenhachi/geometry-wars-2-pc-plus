# Geometry Wars: Retro Evolved 2 PC Port v0.5.0 - High-Refresh Timing, Launcher Updates and Keyboard Remapping

v0.5.0 keeps the original game speed at high frame rates, adds automatic updates to the launcher, keyboard remapping and controller navigation in the launcher, an Exit game item in the main menu and Windows rendering performance work. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

## Highlights

- **High frame rates keep the original game speed.** Above 60 FPS the game still updates 60 times per second, and the frames in between are interpolated ([#5](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/5)).
- **Launcher self-update.** From this version on, the launcher can download and install newer releases for you.
- **Keyboard remapping** on the Controls page, and **controller navigation** in the launcher and the in-game overlays.
- **Exit game** in the main menu.
- The **mouse cursor hides while you play** with a controller or the keyboard and returns when you use the mouse.
- **Faster Direct3D 12 rendering** on Windows.

## Gameplay / High Refresh

- At 90, 120, 144, 165, 240 and Unlimited, the game's own update now stays at 60 updates per second while the screen refreshes at the selected rate. Enemy behavior, animations and timers keep their original timing instead of speeding up with the frame rate. This addresses the broken enemy behavior reported above 60 FPS in [#5](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/5); it has not been confirmed by the reporter yet.
- Frames between two updates show the moving gameplay objects interpolated between those updates. This adds one update (about 17 ms) of visual delay at high frame rates. Particles, trails and other effects are not interpolated.
- At 90, 120, 144, 165 and 240 FPS interpolation is on from the first frame, also with VSync on a 60 Hz display, where the frames the display shows still land between updates. With Unlimited it also starts on and turns off only while the game renders no faster than 60 FPS for about two seconds, so short drops or bursts do not switch it. The 30 and 60 FPS settings behave as in v0.4.0.

## Launcher

- **Updates**: the launcher checks the project's GitHub releases once per start. When a newer release exists, a banner appears in the header; nothing is installed until you choose **UPDATE NOW** and then **RESTART TO UPDATE**. The download is verified with its SHA-256 before anything is replaced, and only the port's own program files are replaced: the `game` folder, `user_data`, saves, settings, logs and caches are kept. On Windows the update is installed by the included `trg-updater.exe`; the Linux AppImage replaces its own file. If the check cannot reach GitHub, the launcher simply continues.
- **Keyboard remapping** (Controls page): every keyboard control can be changed by clicking it and pressing the new key, and **Keyboard defaults** restores the standard layout. The Keyboard toggle is still Off by default.
- **Controller navigation**: Xbox, PlayStation and other controllers that SDL recognizes navigate the launcher and the in-game overlays.
- **Display scaling**: the launcher follows each monitor's scaling and redraws its text when you move it to a monitor with a different scale (Windows).
- New header art, mouse cursor shapes for links and text, and links to the author and the repository.
- **About** page: a **Detailed logging** toggle (on by default) and an **Open logs folder** button.

## Input / UI

- Mouse activity shows the cursor, and keyboard or controller activity hides it, in windowed and fullscreen modes. The launcher, the cheat menu and the Achievements overlay always show it, and so does the desktop when the window loses focus. This replaces the 5-second idle timer of earlier versions.
- The main menu's **Return to arcade** item is now **Exit game**, with the same confirmation.

## Reliability / Diagnostics

- On Windows, if the game crashes, a `CRASH REPORT` block with the exception and the faulting module is added to the session log.
- The launcher and the game write one log per session, including the restart that opens the game window.
- At startup the log lists the system, graphics card, display, controller models and the settings in use, to help with bug reports. This information does not include user names or folder paths.
- With **Detailed logging** Off, a session that ends normally leaves no log; a session that crashes keeps its log.
- Linux: when no display can be opened, the terminal now shows SDL's error and a hint about `DISPLAY` and `WAYLAND_DISPLAY`.

## Platform / Packaging

- Windows rendering work in ReXGlue's Direct3D 12 renderer: command submission on a separate thread, reuse of unchanged sampler and render target state, and cheaper checks of already uploaded memory. The asynchronous submission is adapted from [Unowa001/crackdown-recomp](https://github.com/Unowa001/crackdown-recomp) (BSD 3-Clause); its notice is in `licenses/THIRD-PARTY-LICENSES.txt`.
- The Windows ZIP now also contains `trg-updater.exe`, and both packages contain the launcher header art.
- The Linux AppImage is still built on Ubuntu 20.04: glibc 2.31 or newer, and a Vulkan driver.

## Downloads

- **GeometryWarsRetroEvolved2-v0.5.0-windows-x64.zip**: Windows 10 or 11 (64-bit), DirectX 12 graphics card, latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Extract anywhere you can write to and run `geometry_wars_retro_evolved_2.exe`. To update from v0.4.0 or older, extract over the old folder; your `game` and `user_data` folders are kept. Later updates can be installed from the launcher. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.
- **GeometryWarsRetroEvolved2Recompiled-v0.5.0-x86_64.AppImage**: native Linux x86_64 build using Vulkan (not Proton, not Wine). Make it executable and run it. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md).

Everything from v0.4.0 is included.

## Requirements

- Windows 10 or 11 (64-bit) with DirectX 12, **or** 64-bit Linux / Steam Deck with glibc 2.31 or newer and Vulkan (experimental).
- Your own package, this exact game version:

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

## Known limitations

- The high-refresh timing has not been confirmed by the reporter of [#5](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/5) yet. Particles, trails and other effects are not interpolated.
- Automatic updates start with this version: updating from v0.4.0 to v0.5.0 is still done by hand.
- If an update cannot be installed (for example in a folder you cannot write to), the previous version starts again; the reason is only in the updater's log.
- Keyboard remapping: to bind a key with Shift, Ctrl or Alt, hold the modifier before you click the control. Pressing Enter while choosing a key may also start the game.
- Linux and Steam Deck remain **experimental**. Some Linux systems render around 30 FPS ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1), [#6](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/6)); the cause is not found yet.
- Crash reports in the log are written on Windows only.
- Online Xbox Live services are not supported, and there is no online multiplayer. The profile, scores, leaderboards and achievements are local and offline.

## Reporting problems

Please open an [issue](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues) and attach the newest `geometry_wars_retro_evolved_2_*.log` file from the `logs` folder next to the program (the launcher's **About > Open logs folder** opens it).

## Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.5.0-windows-x64.zip` | `d297c474661ef581e01a885c27814379c37bd886e42370bfe27fda034685b2ae` |
| `GeometryWarsRetroEvolved2Recompiled-v0.5.0-x86_64.AppImage` | `0d435771385b169d6c006e01c4783a8998e2b301bda50c26c54ce3cd208baf57` |
