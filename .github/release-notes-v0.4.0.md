# Geometry Wars: Retro Evolved 2 PC Port v0.4.0 - Controller Rumble, Graphics Options and Linux Improvements

v0.4.0 is the first stable release since v0.3.1. It adds controller vibration, new display and graphics options, a new game-timing clock for Linux and a more robust AppImage. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

## Highlights

- **Controller vibration** for gameplay events. It is **Off by default**: turn it on under **Controls > Vibration**.
- **Aspect Ratio** selector, including 21:9 Ultrawide and 32:9 Super Ultrawide.
- **Internal render resolution** (100%, 75% or 50%) and, on Windows, a **Graphics card** choice.
- **Render resolution in the window title** (optional).
- A new game-timing clock for the Linux speed-ups seen when the frame rate varies.
- The Linux AppImage now starts by itself when FUSE cannot mount it.

## Controller / Input

- **Vibration** (Controls page): Off or On, described as "Controller rumble". The default is **Off**, and there is no strength setting. Turning it Off stops any rumble in progress.
- When On, the controller vibrates for: bombs, death, respawn, game over, extra life, extra bomb, shield impacts, the King zone, Gravity Well explosions, Gravity Well overload (Proton burst) and crossing a gate.
- This is an addition of this PC port. It is not a restoration of the original game's rumble, and the events are not claimed to match the retail game.
- Vibration is included in the Windows and Linux builds. **Windows has been tested with a physical controller; Linux and Steam Deck vibration still needs confirmation on real hardware.**

## Graphics / Display

- **Aspect Ratio** (Display page): 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4. The game renders in the chosen shape instead of stretching or letterboxing: it always renders 720 lines and the width follows the format. Window size lists the sizes of the chosen format, and a window of another shape shows black bars. Changing it restarts the game once when you press PLAY. The menus, the HUD and the splash image are still laid out for 16:9.
- The old **Letterbox / Stretch** option is gone. Saved values from it are removed and start as 16:9 Original.
- **Render resolution** (Original, 2x, 3x, 4x) follows the chosen Aspect Ratio.
- **Internal render resolution** (Graphics page): 100% Native, 75% Performance or 50% Low-end. The GPU draws the game at that share of the Render resolution and scales it up, which lowers the GPU work at some cost in sharpness. It works on Windows (Direct3D 12) and Linux (Vulkan). Based on work from Hard Corps Uprising v0.6.0.
- **Graphics card** (Windows only): Automatic (recommended) or a specific Direct3D 12 GPU. Based on work from Hard Corps Uprising v0.6.0.
- **Render resolution in title** (Gameplay page, off by default): shows the render resolution in the window title, for example `Geometry Wars: Retro Evolved 2 | 2560x1440`. With **Frame rate in title** on too, the resolution comes first.

## Performance / Timing

- A new timing clock keeps the game's speed in step with real time when the frame rate varies. A stable 60 FPS keeps the game's original time step. It is aimed at the short speed-ups some Linux users saw when the frame rate rose toward about 45 FPS ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)).
- **This has not been confirmed on the affected Linux machine yet**, so the issue is not claimed to be fixed. Windows behavior is not intentionally changed.

## Linux / Steam Deck

- **AppImage fallback when FUSE fails.** The AppImage uses a patched build of the upstream AppImage runtime. Where FUSE works it is mounted as usual. Where the mount fails (for example in a restricted environment), it unpacks itself into a private temporary folder, runs from there with the same arguments, passes on the exit status and removes the folder afterwards. You do not need `APPIMAGE_EXTRACT_AND_RUN=1`. This was reproduced and validated by forcing the mount to fail on a Linux test machine; it has not been tried on a SteamOS device.
- Still built on Ubuntu 20.04: glibc 2.31 or newer, and a Vulkan driver.
- Linux remains **experimental**.

## Downloads

- **GeometryWarsRetroEvolved2-v0.4.0-windows-x64.zip**: Windows 10 or 11 (64-bit), DirectX 12 graphics card, latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Extract anywhere you can write to and run `geometry_wars_retro_evolved_2.exe`. To update, extract over the old folder; your `game` and `user_data` folders are kept. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.
- **GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.AppImage**: native Linux x86_64 build using Vulkan (not Proton, not Wine). Make it executable and run it. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md).

Everything from v0.3.1 is included. v0.4.0 also contains the changes of the v0.3.2-rc1 prerelease, which it replaces.

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

- Linux and Steam Deck remain **experimental** and need broader testing on real hardware.
- Controller vibration on Linux and Steam Deck has not been confirmed on a real controller.
- The new Linux timing clock has not been confirmed on the machine that reported [#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1), and some Linux systems still render around 30 FPS; the cause is not found yet.
- The AppImage's automatic extraction when FUSE fails has not been tried on SteamOS hardware.
- Non-16:9 formats are new: the menus, HUD and splash image stay laid out for 16:9.
- Online Xbox Live services are not supported, and there is no online multiplayer. The profile, scores, leaderboards and achievements are local and offline.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

## Reporting problems

Please open an [issue](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues) and attach the newest `geometry_wars_retro_evolved_2_*.log` file from the `logs` folder next to the program.

## Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.4.0-windows-x64.zip` | `53a66624925df488456bf36c971a4a0db0b6659878eeaad1ac471abebae0b8e5` |
| `GeometryWarsRetroEvolved2Recompiled-v0.4.0-x86_64.AppImage` | `49fca6dbf8ffb5c62141ec145e19a59509cf08a7ebca91c26f74ec29f47ff4df` |
