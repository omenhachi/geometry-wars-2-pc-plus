## Geometry Wars: Retro Evolved 2 PC Port v0.1.0

First public release of the native PC port of Geometry Wars: Retro Evolved 2, statically recompiled from the Xbox 360 Xbox Live Arcade version with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) 0.10.0. It runs natively on Windows, with an experimental native Linux and Steam Deck build, and comes with the TRG launcher, high frame rate support and modern PC options. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### Highlights

- Native Windows x64 build, and an experimental native Linux x86_64 / Steam Deck AppImage
- TRG launcher with Play, Display, Graphics, Gameplay, Controls, Achievements and About pages. It remembers its window position, size and maximized state on Windows.
- Installs the game from your own Xbox 360 package, checking the Title ID, content type and the exact `default.xex` before it plays
- Windowed or Borderless Fullscreen (**F11** switches while playing), window size, monitor selection, VSync and aspect ratio
- Render resolution up to 4x, FXAA and FXAA Extreme, 2x MSAA, and texture filtering up to 16x
- Frame rate 30, 60 (the original), 90, 120, 144, 165, 240 or Unlimited, without intentionally speeding up the game
- Frame rate in the window title, and an optional in-game frame rate and frame time counter (**F2** shows or hides it)
- Local profile and progression, local scores, and offline leaderboards with your own scores
- Local achievements: all 12 achievements (200G), saved between runs, with an in-game overlay (**F7**) and an Achievements page in the launcher
- Xbox controllers without setup, and keyboard support

### Windows

**GeometryWarsRetroEvolved2-v0.1.0-windows-x64.zip** needs Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

1. Extract the ZIP anywhere you can write to and run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
2. On the **Play** page click **Install from package...** and pick your own package.
3. Wait for **Ready to play**, then press **PLAY**.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

### Linux / Steam Deck

**GeometryWarsRetroEvolved2-v0.1.0-linux-x86_64.AppImage** is a **native** Linux x86_64 build using Vulkan. It is **not** Proton and **not** Wine. It needs glibc 2.35 or newer and a Vulkan driver.

**Experimental: it has been built and packaged, but not yet tested on real Linux hardware or on a Steam Deck.** Steam Deck support is intended, not confirmed. Reports are welcome.

1. Mark the AppImage executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`) and run it.
2. Install your package from the **Play** page, as on Windows. The package picker needs `zenity` or `kdialog`.
3. On a Steam Deck, do this in **Desktop Mode**, add the AppImage with **Games > Add a Non-Steam Game to My Library**, and play it from **Game Mode**.

See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md) for requirements and known limits.

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
- Linux and Steam Deck are untested on real hardware. The launcher window is placed after it opens, so a saved position, size or maximized state can show a moment late, and Wayland cannot restore the position.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.
