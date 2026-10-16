## Geometry Wars: Retro Evolved 2 v0.3.2-rc1 - Linux Frame Pacing Diagnostic (prerelease)

**This is a diagnostic release candidate for testing the remaining Linux frame pacing issue ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)). It is not a stable release.** It is meant for affected Linux users; everyone else should stay on v0.3.1. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### What changed

- Improves game-speed synchronization when the actual frame rate fluctuates below the configured 60 FPS.
- Replaces the integer missed-frame compensation of v0.3.1 with timing that conserves real time: the game clock advances by the time that really passed, in whole and fractional steps.
- Keeps correct timing around a stable 30 FPS, and keeps the original 1/60 step at a stable 60 FPS.
- Targets the remaining Linux speed-up bursts reported in Issue #1.
- Includes **temporary `DEBUG_SAULO` timing diagnostics** for this test build. They write a limited number of timing lines to the log. They will be removed before a stable release.

**This does not claim to fix the Linux issue.** The new timing has automated tests, but it has **not been confirmed** on the reporter's hardware.

### Help wanted: Linux testers

If the game still visibly speeds up, please attach the generated `geometry_wars_retro_evolved_2` log to [#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1). The diagnostic has a finite line budget, so reproducing the problem within the first few minutes of a fresh session is best.

### Downloads

- **GeometryWarsRetroEvolved2Recompiled-v0.3.2-rc1-x86_64.AppImage**: native Linux x86_64 build using Vulkan (not Proton, not Wine). Needs glibc 2.31 or newer and a Vulkan driver. Mark it executable, run it, then install your package from the **Play** page.
- **GeometryWarsRetroEvolved2-v0.3.2-rc1-windows-x64.zip**: Windows 10 or 11 (64-bit), DirectX 12 graphics card, latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Included so the shared timing code is available on both platforms; Windows behavior is not intentionally changed.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

Everything from v0.3.1 is included: the Vulkan brightness fix, performance and stability work, Bloom control, cheats, portable `user_data`, high frame rate support, offline achievements, local scores and leaderboards, and the Linux launcher fallback when Vulkan is unavailable.

### Required game version

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

### Known limitations

- Linux remains **experimental**. Some Linux systems still render around 30 FPS; the cause is not found yet.
- Online Xbox Live services are not supported; the profile, scores, leaderboards and achievements are local and offline.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Checksums

Each download has a matching `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.3.2-rc1-windows-x64.zip` | `9ed7c95157aed5b5eab112949f9f576640ced002248b9db74836afff52f6b82e` |
| `GeometryWarsRetroEvolved2Recompiled-v0.3.2-rc1-x86_64.AppImage` | `32962b1c494110a861eae9119c1e4cad2030a678ccc1f4783c43861ccb37fdd4` |
