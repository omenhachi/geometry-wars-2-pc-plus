# Geometry Wars: Retro Evolved 2 Recompiled v0.3.2-rc1

**Diagnostic prerelease of the upcoming v0.3.2.** It adds new display and graphics options and an experimental Linux frame-pacing change that still needs testing on the affected hardware. It is not the final v0.3.2. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

## Highlights

- **Aspect Ratio**, including ultrawide: 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4.
- **Internal render resolution** (100%, 75% or 50%) and, on Windows, a **Graphics card** choice. Both are based on work from Hard Corps Uprising v0.6.0.
- **Render resolution in the window title** (optional).
- An experimental timing change and temporary diagnostics for the remaining Linux game-speed issue ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)).

## Display and graphics

- **Aspect Ratio** (Display page). The game now renders in the chosen shape instead of stretching or letterboxing: it always renders 720 lines and the width follows the format (for example 1680x720 at 21:9, 2560x720 at 32:9). A wider format shows more at the sides; a narrower one shows more above and below. Window size lists the sizes of the chosen format, and a window of another shape shows black bars. Changing it restarts the game once when you press PLAY. The menus, the HUD and the splash image are still laid out for 16:9.
- The old **Letterbox / Stretch** option is gone. Saved values from it are removed and start as 16:9 Original.
- **Render resolution** (Original, 2x, 3x, 4x) now follows the chosen Aspect Ratio.
- **Internal render resolution**: 100% Native, 75% Performance or 50% Low-end. The GPU draws the game at that share of the Render resolution and scales it up, which lowers the GPU work at some cost in sharpness. 100% is the previous behavior. The render-target scaling comes from Hard Corps Uprising v0.6.0 and is included for both Windows (Direct3D 12) and Linux (Vulkan).
- **Graphics card** (Windows): Automatic (recommended) or a specific Direct3D 12 GPU.
- **Render resolution in title** (off by default): shows the render resolution in the window title, for example `Geometry Wars: Retro Evolved 2 | 2560x1440`. With the frame-rate title option on too, the resolution comes first.

## Frame pacing diagnostic (Linux, experimental)

- v0.3.1 already fixed the major slow motion around 30 FPS, and the Linux reporter confirmed that Deadline timing was correct at about 30 FPS.
- Short speed-up bursts were still reported when the frame rate rose toward about 45 FPS.
- v0.3.2-rc1 replaces the integer missed-frame compensation with a new timing clock (`GameDeltaClock`). In synthetic tests it keeps game time in step with real elapsed time at stable 30 FPS and at in-between rates and burst patterns, and a stable 60 FPS keeps the game's original 1/60 step.
- **This has not been validated on the affected Linux machine, and the Linux issue is not claimed to be fixed.** This is a diagnostic build for validating the remaining game-speed issue.
- On Windows the same timing code is present; Windows behavior is not intentionally changed.

## Diagnostic logging

This prerelease intentionally contains temporary `DEBUG_SAULO` frame-timing diagnostics. They write a limited number of timing lines to the log and will be removed before a stable release. If the speed-up still happens, please attach your log to [#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1).

## Request for the Linux reporter

1. Use Frame rate = 60 and VSync = On.
2. Start a fresh session.
3. Reproduce several 30 -> 45 -> 30 FPS transitions early in the run.
4. Note whether the game speed visibly increases when the FPS rises.
5. Play Deadline once if practical, and check whether the 3-minute music and timer finish together.
6. Exit normally and attach the resulting `geometry_wars_retro_evolved_2` log.

The diagnostic has a finite line budget, so reproducing the problem within the first few minutes is preferred.

## Downloads and checksums

- **GeometryWarsRetroEvolved2-v0.3.2-rc1-windows-x64.zip**: Windows 10 or 11 (64-bit), DirectX 12 graphics card, latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.
- **GeometryWarsRetroEvolved2Recompiled-v0.3.2-rc1-x86_64.AppImage**: native Linux x86_64 build using Vulkan (not Proton, not Wine). Needs glibc 2.31 or newer and a Vulkan driver. Linux remains experimental.

Each download has a matching `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.3.2-rc1-windows-x64.zip` | `1a6ceef64235a4eec0c693d11dfa08814f8852cc4f10755139aa3782b45cc6b6` |
| `GeometryWarsRetroEvolved2Recompiled-v0.3.2-rc1-x86_64.AppImage` | `0c5178daca95e432969d36d7120cdf6385980cc522ce9ae24a2c9792920f11ba` |

Everything from v0.3.1 is included.

## Required game version

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

## Known limitations

- Linux remains **experimental**. Some Linux systems still render around 30 FPS; the cause is not found yet.
- Non-16:9 formats are new: the menus, HUD and splash image stay laid out for 16:9.
- Online Xbox Live services are not supported; the profile, scores, leaderboards and achievements are local and offline.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.
