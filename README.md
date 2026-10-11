<div align="center">

# Geometry Wars: Retro Evolved 2 — PC Port

### Geometry Wars: Retro Evolved 2 running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher, high frame rate support and modern PC options.

[![Latest release](https://img.shields.io/github/v/release/saulob/geometry-wars-retro-evolved-2-recompiled?style=for-the-badge&label=release&color=943cff&labelColor=07101f&include_prereleases)](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/geometry-wars-retro-evolved-2-recompiled/total?style=for-the-badge&color=943cff&labelColor=07101f)](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/releases)
![Platform](https://img.shields.io/badge/platforms-Windows%20x64%20%7C%20Linux%20x86__64%20(experimental)-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.</sub>

</div>

## What you need

- Windows 10 or 11, 64-bit, and a DirectX 12 graphics card
- The latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already have it.
- **Or** 64-bit Linux (x86_64, glibc 2.35 or newer) or a Steam Deck with a Vulkan driver (experimental, see [Linux and Steam Deck](#linux-and-steam-deck))
- **Your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package**, this exact game version:

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Content type | `000D0000` (Xbox Live Arcade), STFS `LIVE` package |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` | 704,512 bytes, CRC32 `5E9B6313`, SHA-1 `048B852E52A8C776C7AE752141591701B7EA5E69`, SHA-256 `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

The launcher checks the Title ID and content type when you pick the package, and checks `default.xex` after extracting it. Package containers can differ between copies, so `default.xex` is what identifies the version. Any other version is refused, because the port is built for this exact game code.

## Installing

1. Download **GeometryWarsRetroEvolved2-v0.1.0-windows-x64.zip** from the [latest release](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/releases/latest) and extract it anywhere you can write to.
2. Run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
3. On the **Play** page click **Install from package...** and pick your package. It is one file with a long hexadecimal name and no extension. On an Xbox 360 drive it is in `Content\0000000000000000\584108FF\000D0000\`.
4. Wait until **Ready to play** appears. The game files are copied into the `game` folder next to the program. The package file itself is not copied there.
5. Press **PLAY**.

**No game files are included** in the release.

**Reinstall from package...** repairs the game files and keeps your settings and saves.

### Linux and Steam Deck

**Experimental, not yet tested on real hardware.** This is a native Linux build (ReXGlue's Linux runtime with Vulkan), not Proton or Wine.

1. Download **GeometryWarsRetroEvolved2-v…-linux-x86_64.AppImage** from the release and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it, then install your package from the **Play** page as above. The game files go in a `game` folder next to the AppImage. The package picker needs `zenity` or `kdialog`.
3. On a Steam Deck (intended flow, not yet tried on a Deck), do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

Settings and saves are in `~/.local/share/geometry_wars_retro_evolved_2` (`$XDG_DATA_HOME` when set), logs in `logs` next to the AppImage. Requirements, known limits and building the AppImage are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. If you downloaded it from this repository's releases page, choose **More info**, then **Run anyway**.

## The launcher

The launcher opens before the game. Your choices are saved to `settings.toml` in the `launcher` folder of your user data (`Documents\geometry_wars_retro_evolved_2` on Windows). To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back. The launcher remembers its window position, size and whether it was maximized.

### Play
- **Install from package...** with the Title ID and game version checks, and **Reinstall from package...**
- The **Ready to play** status, and the **Show this launcher** setting

### Display
- **Display mode**: Windowed or Borderless Fullscreen. Press **F11** while playing to switch between them.
- **Window size** and **Monitor**
- **VSync** on or off
- **Aspect ratio**: Letterbox 16:9 keeps the game's picture on any window shape; Stretch fills the window

Window size, monitor and VSync take effect when you press **PLAY**. If one differs from the launcher's window, the game restarts once in a new window.

### Graphics
- **Render resolution**: Original (1280×720), 2x (2560×1440, recommended for 1440p), 3x (3840×2160) or 4x (5120×2880)
- **Anti-aliasing**: Off, FXAA or FXAA Extreme
- **Multisampling**: 2x MSAA on or off
- **Texture filtering**: Game, Off, 2x, 4x, 8x or 16x

### Gameplay
- **Frame rate**: 30, 60, 90, 120, 144, 165, 240 or Unlimited. 60 is the original game and the default. The game's speed stays the same at every rate; a slower graphics card or heavier graphics settings lower the frame rate without changing the speed.
- **Frame rate in title**: shows the frame rate in the window title, for example `Geometry Wars: Retro Evolved 2 | FPS: 60.00`
- **Frame counter**: shows the frame rate in the corner of the window. Press **F2** while playing to show or hide it.

### Controls
- Xbox controllers work without setup
- **Keyboard**: enabled or disabled. When enabled, the keyboard and mouse emulate the controller, and the keys are listed on the page.
- Online multiplayer is not supported.

### Achievements
The launcher's **Achievements** page lists the game's 12 achievements (200G in total) with their icons, gamerscore and unlocked state, and a progress summary. The list appears after you have pressed **PLAY** once, from the next time the launcher opens.

### Achievements, leaderboards and Xbox Live
Everything stays offline: there is no Xbox Live connection, Microsoft sign-in or online player.
- **Achievements**: the game's achievements menu item and **F7** open the same list in game. B, Escape or F7 closes it. Unlocks show the usual notification and are kept between runs on this PC, with no synchronization with Xbox Live.
- **Leaderboards**: the leaderboard screens work with your own scores only. Finished games are kept in a local file, and the boards show your row or no rows, never other players.
- **Profile**: the game's profile and options are saved locally.

[docs/offline-live.md](docs/offline-live.md) and [docs/offline-profile.md](docs/offline-profile.md) have the details.

### About
- The port version, game version and library versions
- Open the game folder, or the saves and settings folder
- **Show settings file**, and **Reset all settings**

## Reporting problems

If the game does not start or crashes, please open an [issue](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues). Attach the newest `geometry_wars_retro_evolved_2_*.log` file from the `logs` folder next to the program, and say what you were doing when the problem happened.

## Building it yourself

The recompiled C++ is generated from your own game files, so the repository does not contain it. Clone with submodules, so the TRG launcher is included:

```powershell
git clone --recurse-submodules https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled.git
```

In an existing clone, run `git submodule update --init --recursive`.

1. Install Visual Studio with the C++ and LLVM/Clang components. See [docs/minimal-native-build.md](docs/minimal-native-build.md).
2. Extract your package's files into `game\extracted` (`.\tools\extract_stfs.ps1 -Package "<package file>" -OutDir game\extracted`), so that `game\extracted\default.xex` exists.
3. `.\tools\codegen.ps1` downloads the pinned ReXGlue SDK 0.10.0, checks `default.xex` and generates the C++ into `generated\`.
4. `.\tools\build.ps1` builds `build\geometry_wars_retro_evolved_2.exe`. `.\tools\build.ps1 -Run` also starts it.
5. `.\tools\package-release.ps1` builds a Release into `build-release\` (add `-Clean` for a clean build) and makes the release ZIP and its `.sha256` file in `dist\`.

The Linux AppImage is built on Linux or WSL with `tools/package-linux.sh`, after the steps above have generated the C++. See [docs/linux.md](docs/linux.md).

Both packaging scripts are local only: they build and write files under `dist/`, and never upload, tag or publish anything. The release version is the `project()` VERSION in `CMakeLists.txt`.

How the launcher, settings and installer work is described in [docs/launcher.md](docs/launcher.md). The other technical notes:

- [Package analysis](docs/package-analysis.md)
- [Minimal native build](docs/minimal-native-build.md)
- [First boot](docs/first-boot.md)
- [Offline profile and save](docs/offline-profile.md)
- [Offline Xbox Live services](docs/offline-live.md)
- [Linux and Steam Deck](docs/linux.md)

## Credits

- Geometry Wars: Retro Evolved 2 by Bizarre Creations, published by Microsoft Game Studios for Xbox Live Arcade (2008). This is an unofficial fan-made PC port. It is not affiliated with or endorsed by them or by any rights holder, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0. The referenced revision contains no explicit license file.
- The launcher and package installer follow the approach of the [Hard Corps: Uprising PC port](https://github.com/saulob/hard-corps-uprising-recompiled) and the [Earthworm Jim HD PC port](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled)
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 only (GPL-2.0-only). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt) and [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Geometry Wars: Retro Evolved 2 and its assets are not covered by this project's license.
