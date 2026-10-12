## Geometry Wars: Retro Evolved 2 PC Port v0.2.0 - Cheats, Portable Data and Launcher Improvements

v0.2.0 adds cheats, portable player data and several launcher and window usability improvements, while keeping the high frame rate, offline achievement and native Linux support introduced in v0.1.0. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### New: cheats

- Three cheats, all **Off by default**: **Invincible** (the player cannot die; a shield or the short protection after a bomb still works as usual), **Infinite Shield** (keeps the shield active) and **Infinite Bombs** (bombs are not used up; it never adds bombs you do not have).
- The launcher has a new **Cheats** page. Values set there are saved and take effect when you press **PLAY**.
- Press **F10** in game to open the Cheats menu. Move with Up and Down (arrow keys, D-pad or left stick), toggle with Enter or **A**, and close with **F10**, Escape or **B**. The game ignores input while the menu is open.
- Changes made in the F10 menu last for the current session only. They do not change the launcher's saved values.
- Cheats do not disable scores, leaderboards or achievements.

### New: portable user data

- Your settings, profile, scores, achievements, saves and progression, and the shader cache now live in a `user_data` folder next to the program on Windows, and next to the AppImage file on Linux. Copy the whole folder to another PC to take them with you.
- **Upgrading from v0.1.0 is automatic.** The first start moves your data from `Documents\geometry_wars_retro_evolved_2` (Windows) or `~/.local/share/geometry_wars_retro_evolved_2` (Linux) into `user_data`. A rename is used when possible, otherwise a verified copy, and the old folder is removed only after the move succeeds.
- If the move fails, the old folder is left untouched and used, and the move is tried again at the next start. If both locations already hold data, `user_data` is used and the old folder is left alone, never merged. If the program folder is read-only, the old location is used for that run.
- `--user_data_root=<folder>` still overrides everything. Details are in [docs/user-data.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/user-data.md).

### Launcher and window improvements

- The launcher remembers its window position, size, maximized state and its own borderless fullscreen state (**F11** in the launcher).
- A saved position that is no longer on any connected display is ignored, so the launcher never opens out of reach. Monitors to the left of or above the main one are supported.
- **F11** in the launcher and **F11** in the game are independent. The launcher's fullscreen never changes the game's Display mode, and the game's Display mode, Window size and Monitor never move or resize the launcher. Pressing **PLAY** does not erase the launcher's saved fullscreen choice.

### Mouse cursor improvements

- During fullscreen gameplay the mouse cursor hides after about 5 seconds without movement and returns the moment the mouse moves. It stays visible in the launcher, in windowed mode, and whenever the game window loses focus.

### Existing features

v0.2.0 continues to include everything from v0.1.0: the native Windows build and the experimental native Linux / Steam Deck AppImage, high frame rate support (30 to Unlimited, 60 is the original), the in-game frame counter (**F2**) and frame rate in the window title, render resolution up to 4x with the graphics options, the package installer, offline achievements (all 12, 200G, **F7** overlay), local scores and leaderboards, and the local profile and progression.

### Windows

**GeometryWarsRetroEvolved2-v0.2.0-windows-x64.zip** needs Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

1. Extract the ZIP anywhere you can write to and run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
2. On the **Play** page click **Install from package...** and pick your own package.
3. Wait for **Ready to play**, then press **PLAY**.

To update from v0.1.0, extract over the old folder, or move `game` into the new one. Your data is moved into `user_data` on the first start.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

### Linux / Steam Deck

**GeometryWarsRetroEvolved2-v0.2.0-linux-x86_64.AppImage** is a **native** Linux x86_64 build using Vulkan. It is **not** Proton and **not** Wine. It needs glibc 2.35 or newer and a Vulkan driver.

**Experimental: it has been built and packaged, but not yet tested on real Linux hardware or on a Steam Deck.** Steam Deck support is intended, not confirmed. Reports are welcome.

1. Mark the AppImage executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`) and run it.
2. Install your package from the **Play** page, as on Windows. The package picker needs `zenity` or `kdialog`.
3. On a Steam Deck, do this in **Desktop Mode**, add the AppImage with **Games > Add a Non-Steam Game to My Library**, and play it from **Game Mode**.

The portable `user_data` folder and the move from `~/.local/share` are covered by unit tests only, not yet tried with an AppImage on real hardware. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md) for requirements and known limits.

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

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.2.0-windows-x64.zip` | `6dc71da7b117a766c536f4cd68fccb6defbafab9e0782b515dbeb3d1d7148cd1` |
| `GeometryWarsRetroEvolved2-v0.2.0-linux-x86_64.AppImage` | `ee12f17cfb04005e4d4f96e43b8daa242f44a3583eec5f73fac78f65e8468924` |
