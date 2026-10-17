# 18 Curated change history

Only changes with reusable value. Chronological, grouped by evolution. Pure README/badge/release-note commits
(`3f8e959`, `ed26473`, `120011c`, `544f1cb`, `919c0be`, `6edeba1`, `9efdc99`, `b37702e`, `f31d9e8`, `3c43a55`) are
omitted. Dates are commit dates (2026). Merge commits: `1291e6b` (launcher window state), `2a13ea1` (release/v0.2.0),
`6cc12f1` (startup crash), `735dec9` (Vulkan dark image + RenderDoc).

| Group | Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- | --- |
| Bring-up (10-10) | `38a1d61` | initial native recompilation build | codegen, build, tools | Manifest, overrides, `setjmp`/`longjmp` addresses, STFS extraction, dump_image, discovery script, first-boot analysis; temporary fault locator (removed later) | [01](01-rexglue-core.md), [02](02-build-system.md) |
| | `3997f22` | harden missing function discovery | codegen tooling | Dropped false lis/addi candidates; `--verify`, `--self-test` | [01](01-rexglue-core.md) |
| Offline services (10-11) | `dbbf80f` | offline local profile persistence | profile hook | First-run "unable to load profile" fix | [11](11-offline-services-profile-achievements.md) |
| | `bead687` | offline Live services and achievements | live hooks, overlay, stats file | Local scores/leaderboards, achievements overlay, `test.ps1` | [11](11-offline-services-profile-achievements.md) |
| Launcher + fps (10-11) | `ab4631e` | launcher and high frame rate support | launcher, settings, STFS, limiter | TRG integration, `GameSettings`, PLAY gate, placement tracker, frame limiter (Windows), window host, sampler | [03](03-launcher.md), [07](07-display-window-frame-pacing.md) |
| Linux (10-11) | `9e6f05b` | Linux support and release packaging | platform split, packagers | `launcher_platform_{win32,linux}.cpp`, `frame_wait.cpp`, AppRun, desktop, `package-linux.sh`, `package-release.ps1`, removed the fault locator | [05](05-platform-linux.md), [14](14-packaging-release.md) |
| Icon (10-11) | `b362de8` | application icon | packaging | `.rc` + icon set, `make-app-icon.py`, Linux app id, desktop file renamed to the id, icon loading by SDL | [04](04-platform-windows.md), [05](05-platform-linux.md) |
| User data (10-11) | `650b8c4` | portable user data with migration | user data | `SelectUserData`, injectable file ops, tests; v0.1.0 -> v0.2.0 migration | [10](10-user-data-settings-migration.md) |
| Cheats (10-12) | `f9731f9` | gameplay cheats and in-game menu | game hooks, menu | Declarative cheat list, session layer, input gating | [16](16-reusable-game-features.md) |
| Cursor (10-12) | `16a97f6` | auto-hide mouse cursor in fullscreen | window host | `CursorHider` state machine | [08](08-input-gamepad-mouse.md) |
| Launcher state (10-12) | `b46a22f` (merged `1291e6b`) | keep launcher window state separate from game display mode | launcher | Separate placement keys, F11 semantics, apply Display on PLAY | [03](03-launcher.md) |
| | `cd818f3` (merged `2a13ea1`) | prepare v0.2.0 | release | First release with the above | [14](14-packaging-release.md) |
| Startup crash (10-12) | `aa64c6a` (merged `6cc12f1`) | patch ReXGlue physical heap release race | SDK patch model | First patch (0001), builder scripts, stress test; baseline pins | [01](01-rexglue-core.md), [13](13-crash-hardening-threading-memory.md) |
| Bloom (10-12) | `074b563` | Bloom control | game hook | Scale-and-restore of tuning floats; v0.2.2 | [06](06-graphics-vulkan.md) |
| Vulkan fixes (10-14) | `378f609` | correct Vulkan texture exponent bias | SDK patch | 0002; confirmed brightness fix | [06](06-graphics-vulkan.md) |
| | `a86c63f` | integrate patched ReXGlue into normal builds | build | `patches.txt`, per-configuration prepare/check, CMake guards, Linux build/codegen scripts | [02](02-build-system.md), [01](01-rexglue-core.md) |
| | `852b042` | prevent unintended RenderDoc loading on Linux | SDK patch, tests | 0003, fake RenderDoc test, packager guard | [05](05-platform-linux.md) |
| | `a576d87` (merged `735dec9`) | `$ORIGIN` RUNPATH for patched Linux libraries | build | Libraries from the SDK build tree otherwise carry an absolute RUNPATH | [05](05-platform-linux.md) |
| | `f4f4ef9` | prepare v0.2.3 | release | Linux brightness fix release | [14](14-packaging-release.md) |
| Performance + hardening (10-15) | `4067893` | port ReXGlue performance and hardening patches | SDK patches 0004-0033, 0035 | The 31-patch series from the donor project | [01](01-rexglue-core.md), [12](12-performance.md), [13](13-crash-hardening-threading-memory.md) |
| | `273dc0c` | improve frame pacing and native performance paths | game hooks, CMake | Fence wait hook, CRT native copy, repeated-present exclusion, ThinLTO, presenter pacing test | [07](07-display-window-frame-pacing.md), [12](12-performance.md) |
| | `d2ebd7e` | harden shutdown and title thread teardown | app, tests | Input `Shutdown()` before window destruction; shutdown tests. (Its diff also adds the `ApplyMultisampling` call in `OnFinalizePaths` that `588e1fd` relies on) | [13](13-crash-hardening-threading-memory.md) |
| | `588e1fd` | correct multisampling options and migration | launcher settings | `gw2_msaa_compatibility`, `ApplyMultisampling`, migration of the old "Off" value | [06](06-graphics-vulkan.md), [10](10-user-data-settings-migration.md) |
| | `0dde10c` | prepare v0.3.0 release | release | v0.3.0 | [14](14-packaging-release.md) |
| Linux compatibility (10-15) | `e0f0eef` | improve AppImage compatibility with older glibc systems | packaging | Ubuntu 20.04 build baseline, glibc 2.31 gate (`check-glibc-baseline.sh`), `setup-linux-build-env.sh` | [05](05-platform-linux.md), [14](14-packaging-release.md) |
| | `db1eb48` | rename Linux AppImage artifact | packaging | `GeometryWarsRetroEvolved2Recompiled-v<version>-x86_64.AppImage`, `--out-dir` | [14](14-packaging-release.md) |
| | `f4f8480` | show an error window when graphics initialization fails | launcher | Superseded by `24d05f2` | [05](05-platform-linux.md) |
| | `24612e3` | normalize AppImage application naming | packaging | Visible desktop name "Geometry Wars: Retro Evolved 2 Recompiled"; identifiers unchanged | [05](05-platform-linux.md) |
| | `cccdc91` | keep game speed at 60 FPS setting when frames miss vblanks | frame limiter | `GameStepCounter`: whole 1/60 steps, at most 3, for late frames at 60 (later `GameDeltaClock`: whole steps near a multiple, the real interval otherwise) | [07](07-display-window-frame-pacing.md) |
| | `24d05f2` | open a launcher window on Linux when Vulkan is unavailable | launcher | SDL software fallback launcher, no zenity or GTK; Reference HEAD | [05](05-platform-linux.md) |

## Superseded approaches worth remembering
- Windows-only `frame_limiter.cpp` wait (`ab4631e`) -> portable `frame_wait.cpp` (`9e6f05b`).
- Patched runtime only verified by the packagers (`aa64c6a`) -> verified by every build (`a86c63f`).
- Multisampling "Off" mapped to `native_2x_msaa = false` (v0.2.x) -> 2x default / 4x compatibility (`588e1fd`).
- Launcher state coupled to game display state -> separated (`b46a22f`).
- Local branches `build/rexglue-auto-rebuild` and `fix/renderdoc-linux-autoload` were earlier attempts at the build integration and the RenderDoc fix; the final designs are `a86c63f` and patch `0003`. `snapshot/vulkan-dark-image-final`, `pr2-tim`, `macos` and `release/*` branches were not analyzed for content (`UNCERTAIN`; `macos` is not part of HEAD).

## How to extend this file
After each reference update, add rows for new meaningful commits since the recorded HEAD, group related commits, and keep
older rows. Do not paste `git log`.
