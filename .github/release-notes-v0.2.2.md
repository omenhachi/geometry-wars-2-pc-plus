## Geometry Wars: Retro Evolved 2 v0.2.2 - Bloom Control

v0.2.2 adds a Bloom option to the Graphics page. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### Bloom

- New **Bloom** setting on the **Graphics** page: **Original**, **Reduced** or **Off**.
- **Original** is the default and is the game's unchanged bloom.
- **Reduced** lowers the game's original bloom and keeps the intended glow.
- **Off** removes the bloom and glow, including in the menus and on the title logos. The geometry, colors and HUD are kept.
- It scales the intensity values of the game's own bloom; it is not an extra filter over the picture. Off does not change performance.
- The choice is saved with your other settings and applies when you press **PLAY**, without a restart.

### Everything else is unchanged

v0.2.2 includes everything from v0.2.1 and v0.2.0: the runtime stability fix, cheats, portable `user_data`, the launcher improvements, high frame rate support (30 to Unlimited, 60 is the original), the in-game frame counter (**F2**), offline achievements (all 12, 200G, **F7** overlay), local scores and leaderboards, the local profile and progression, and the native Windows build and the experimental native Linux / Steam Deck AppImage.

### Windows

**GeometryWarsRetroEvolved2-v0.2.2-windows-x64.zip** needs Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

1. Extract the ZIP anywhere you can write to and run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
2. On the **Play** page click **Install from package...** and pick your own package.
3. Wait for **Ready to play**, then press **PLAY**.

To update from an older version, extract over the old folder. Your `game` and `user_data` folders are kept.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

### Linux / Steam Deck

**GeometryWarsRetroEvolved2-v0.2.2-linux-x86_64.AppImage** is a **native** Linux x86_64 build using Vulkan. It is **not** Proton and **not** Wine. It needs glibc 2.35 or newer and a Vulkan driver.

**Experimental: it has been built and packaged, but not yet tested on real Linux hardware or on a Steam Deck.** Steam Deck support is intended, not confirmed. Reports are welcome. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md) for requirements and known limits.

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

- **Online Xbox Live services are not supported**, and there is no online multiplayer. The profile, progression, scores, leaderboards and achievements are handled locally and offline, on your PC only. The leaderboards show your own scores, never other players.
- Linux and Steam Deck are untested on real hardware.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.2.2-windows-x64.zip` | `FILL-AFTER-PACKAGING-WINDOWS` |
| `GeometryWarsRetroEvolved2-v0.2.2-linux-x86_64.AppImage` | `FILL-AFTER-PACKAGING-LINUX` |
