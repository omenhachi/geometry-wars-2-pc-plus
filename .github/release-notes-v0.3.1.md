## Geometry Wars: Retro Evolved 2 v0.3.1 - Linux Timing and AppImage Compatibility

v0.3.1 is a small update after v0.3.0. It fixes the game speed when the Linux build cannot hold 60 FPS, makes the Linux AppImage start on older systems, and opens a launcher window when Vulkan cannot start. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### Linux timing

- At the **60 FPS** setting, a frame that misses one or more vblanks now advances the game by the number of 1/60 steps that really passed (at most three), instead of always one. A computer that renders around 30 FPS no longer makes the game itself run at half speed. The picture is less smooth, but the game keeps its speed.
- A frame on time still gets exactly the game's original step. The 30, 90 and higher frame rates keep their existing timing.

**This does not make the Linux frame rate faster.** Why some Linux systems render around 30 FPS ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)) is still under investigation. The timing fix has automated tests but has **not yet been confirmed** on the Steam Machine from that report.

### Linux / AppImage

- **A launcher window when Vulkan is unavailable.** If Vulkan cannot start (no driver or no GPU), the AppImage no longer just exits. It opens a small launcher window that shows the game files and the graphics status. **PLAY** explains that a Vulkan-compatible driver is required and points at the logs folder, and **QUIT** closes the program. It needs no Vulkan, `zenity` or GTK. With a working driver the normal launcher opens as before.
- **Older Linux systems.** The AppImage is now built on Ubuntu 20.04 and needs glibc 2.31 or newer (it was 2.35), so it starts on distributions that v0.3.0 could not. glibc itself is not bundled.
- **New AppImage name.** `GeometryWarsRetroEvolved2Recompiled-v0.3.1-x86_64.AppImage`. The `-linux-` part is gone.
- **Application name.** The desktop entry is now called **Geometry Wars: Retro Evolved 2 Recompiled**, which is what application menus and software catalogs show. Settings and saves are not affected.
- Linux remains **experimental**.

### Windows

Windows behavior is unchanged by the Linux-specific fixes in this release.

v0.3.1 includes everything from v0.3.0 and earlier: the performance and stability work, the Multisampling correction, the Linux Vulkan brightness fix (confirmed on a Steam Machine), Bloom control, cheats, portable `user_data`, the launcher improvements, high frame rate support (30 to Unlimited, 60 is the original), the in-game frame counter (**F2**), offline achievements (all 12, 200G, **F7** overlay), local scores and leaderboards, and the local profile and progression.

### Windows download

**GeometryWarsRetroEvolved2-v0.3.1-windows-x64.zip** needs Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

1. Extract the ZIP anywhere you can write to and run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
2. On the **Play** page click **Install from package...** and pick your own package.
3. Wait for **Ready to play**, then press **PLAY**.

To update from an older version, extract over the old folder. Your `game` and `user_data` folders are kept.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

### Linux / Steam Deck download

**GeometryWarsRetroEvolved2Recompiled-v0.3.1-x86_64.AppImage** is a **native** Linux x86_64 build using Vulkan. It is **not** Proton and **not** Wine. It needs glibc 2.31 or newer and a Vulkan driver. Steam Deck support is intended, not confirmed. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md) for requirements and known limits.

### Required game version

The port is built for this exact version and refuses any other:

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

### Known limitations

- **Linux / Steam Deck remains experimental** and still needs broader testing on real hardware.
- **Some Linux systems still render around 30 FPS.** The cause is not found yet. The game now keeps its speed at the 60 FPS setting, but the frame rate itself is unchanged.
- **The v0.3.1 timing fix ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)) still awaits confirmation** on the reporter's hardware.
- **Online Xbox Live services are not supported**, and there is no online multiplayer. The profile, progression, scores, leaderboards and achievements are handled locally and offline, on your PC only. The leaderboards show your own scores, never other players.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.3.1-windows-x64.zip` | `589884cf458ec169719a805f593d4c5f55506bab7d96dd8be3e93db1ba8c3c53` |
| `GeometryWarsRetroEvolved2Recompiled-v0.3.1-x86_64.AppImage` | `96b7be0242e4945943fddf12cd2afb7ec8a2f049d4b36d297a5f67bc1d7fed31` |
