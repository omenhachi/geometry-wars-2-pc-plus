# Geometry Wars: Retro Evolved 2 Recompiled v0.5.0

v0.5.0 brings major improvements to high frame rate gameplay, presentation, performance and usability. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

## Highlights

- **High frame rate gameplay with smooth interpolation.** The game's own simulation stays at its original 60 updates per second, so gameplay timing stays correct at every frame rate. Above 60 FPS, the frames between updates are interpolated, so motion is smoother on high-refresh displays. This replaces the v0.4.0 high-FPS timing, where enemies and timers could speed up with the frame rate.
  - Fixed rates of 90, 120, 144, 165 and 240 FPS start interpolation immediately. Unlimited turns interpolation on or off based on how fast the game renders.
  - Interpolation adds about one 60 Hz update (about 17 ms) of visual delay. Particles, trails and other effects are not interpolated.
  - This addresses the enemy behavior problems reported in [#5](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/5) above 60 FPS; the reporter has not confirmed the fix yet.
- **Performance and frame pacing.** v0.5.0 reduces rendering and runtime overhead, with the largest changes in the Windows Direct3D 12 renderer, and paces frames more evenly at high frame rates, for smoother high-refresh gameplay.
- **Exit game in the main menu.** The original main menu's last item now reads **Exit game**, so you can leave the game directly from its own menu.

## Launcher and controls

- **Launcher self-update.** The launcher checks the project's GitHub releases once per start. When a newer release exists, a banner appears in the header; nothing is installed until you choose **UPDATE NOW** and then **RESTART TO UPDATE**. The download is verified with its SHA-256 before anything is replaced, and only the port's own program files, including the shipped `geometry_wars_retro_evolved_2.toml`, are replaced: the `game` folder, `user_data`, saves, settings, logs and caches are kept. On Windows the update is installed by the included `trg-updater.exe`; the Linux AppImage replaces its own file. If the check cannot reach GitHub, the launcher simply continues.
- **Keyboard remapping** (Controls page): every keyboard control can be changed by clicking it and pressing the new key, and **Keyboard defaults** restores the standard layout. The Keyboard toggle is still Off by default.
- **Controller navigation**: Xbox, PlayStation and other controllers that SDL recognizes navigate the launcher and the in-game overlays.
- **Input-aware cursor**: mouse activity shows the cursor, and keyboard or controller activity hides it, in windowed and fullscreen modes. The launcher, the cheat menu and the Achievements overlay always show it, and so does the desktop when the window loses focus. This replaces the 5-second idle timer of earlier versions.
- **Display scaling**: the launcher follows each monitor's scaling and redraws its text when you move it to a monitor with a different scale (Windows).
- New header art, mouse cursor shapes for links and text, and links to the author and the repository.
- **About** page: a **Detailed logging** toggle (on by default) and an **Open logs folder** button.

## Reliability and diagnostics

- **Windows crash reporting**: if the game crashes, a `CRASH REPORT` block with the exception and the faulting module is added to the session log.
- **Improved session logging**: the launcher and the game write one log per session, including the restart that opens the game window.
- At startup the log lists the system, graphics card, display, controller models and the settings in use, to help with bug reports. This information does not include user names or folder paths.
- **Detailed logging**: with it Off, a session that ends normally leaves no log; a session that crashes keeps its log.
- **Improved shutdown reliability**: closing the game window no longer shuts down logging while game threads may still be writing to it.

## Windows and Linux

- **Windows**: less rendering overhead in the Direct3D 12 renderer, from command submission on a separate thread, reuse of unchanged sampler and render target state, and cheaper checks of already uploaded memory. The asynchronous submission is adapted from [Unowa001/crackdown-recomp](https://github.com/Unowa001/crackdown-recomp) (BSD 3-Clause); its notice is in `licenses/THIRD-PARTY-LICENSES.txt`.
- **Windows**: the ZIP includes the update helper `trg-updater.exe`, and both packages include the launcher header art.
- **Linux**: the AppImage is built on Ubuntu 20.04: it needs glibc 2.31 or newer and a Vulkan driver.
- **Linux**: when no display can be opened, the terminal now shows SDL's error and a hint about `DISPLAY` and `WAYLAND_DISPLAY`.

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
| `GeometryWarsRetroEvolved2-v0.5.0-windows-x64.zip` | `249c38ae688477d5d755ce5a26a9f1d18f50897e8ba9beceed621b8af741df8b` |
| `GeometryWarsRetroEvolved2Recompiled-v0.5.0-x86_64.AppImage` | `afe84166029458897a6f1c8222f954c44687d89aa3c39d0ddbc6a46d0281fd14` |
