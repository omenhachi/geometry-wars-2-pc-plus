## Geometry Wars: Retro Evolved 2 — PC Port 0.1.0

First release of the native PC port of the Xbox Live Arcade version of Geometry Wars: Retro Evolved 2, statically recompiled with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) 0.10.0. **No game files are included**: you need your own Xbox Live Arcade package (Title ID `584108FF`, game version `0.0.1.2`).

### Downloads

- **GeometryWarsRetroEvolved2-v0.1.0-windows-x64.zip** — Windows 10/11 x64, DirectX 12. Extract anywhere you can write to and run `geometry_wars_retro_evolved_2.exe`.
- **GeometryWarsRetroEvolved2-v0.1.0-linux-x86_64.AppImage** — **experimental** native Linux x86_64 build (Vulkan, glibc 2.35 or newer; not Proton or Wine), also intended for the Steam Deck. It has been built and packaged but **not yet tested on real Linux hardware or a Steam Deck**. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md).

Each file has a matching `.sha256` file.

### What it does

- TRG Launcher with Play, Display, Graphics, Gameplay, Controls, Achievements and About pages. It remembers its window position, size and maximized state.
- Installs the game from your own package, checking the Title ID, content type and the exact `default.xex` before it plays
- Windowed and Borderless Fullscreen (F11), window size, monitor, VSync, aspect ratio
- Render resolution up to 4x, anti-aliasing, 2x MSAA and texture filtering
- Frame rate 30, 60 (the original), 90, 120, 144, 165, 240 or Unlimited, with the game's speed unchanged; a frame rate in the window title and an optional frame counter (F2)
- Local profile and scores, offline leaderboards with your own scores, and achievements saved between runs, with a list in the launcher and in game (F7)
- Xbox controllers and keyboard

There is no Xbox Live connection and no online play.

### Known limits

- Linux and Steam Deck: untested on real hardware. The launcher window is placed after it opens, so a saved position, size or maximized state can show a moment late, and Wayland cannot restore the position. The package picker needs `zenity` or `kdialog`.
- The Windows build is not digitally signed, so SmartScreen may warn on first run.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Verify the downloads

Compare the SHA-256 of each file with its `.sha256` file.
