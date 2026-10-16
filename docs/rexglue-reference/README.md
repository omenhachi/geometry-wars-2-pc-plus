# ReXGlue engineering reference

Technical migration reference for Xbox 360 native recompilation projects built on ReXGlue. It records the
infrastructure, fixes, hardening, launcher work and SDK patches implemented in this repository so another developer
or AI can reproduce or port them. It is not a player README: user-facing text lives in `README.md`, `docs/`
and `.github/release-notes-*.md`.

| | |
| --- | --- |
| Generated from | Geometry Wars: Retro Evolved 2 Recompiled (title `584108FF`), `saulob/geometry-wars-retro-evolved-2-recompiled` |
| ReXGlue version | v0.10.0, tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` (pinned in `patches/rexglue-sdk/baseline.env`) |
| Branch when written | `docs/rexglue-reference` (created at the commit below, no code changes) |
| Reference HEAD | `24d05f2` Open a launcher window on Linux when Vulkan is unavailable (project VERSION 0.3.0; v0.3.1 is prepared on top) |
| Last updated | 2026-10-15 |
| History covered | all 45 commits, `38a1d61` (2026-10-10) to `24d05f2` (2026-10-15) |

Branches that exist locally but are not in the HEAD above (for example `feature/controller-rumble`, which holds one
commit of temporary rumble diagnostics, `3e9d942`) are not documented as implemented features.

## How to use these documents

1. Read this README, then [17-porting-checklist.md](17-porting-checklist.md) and
   [19-cross-project-porting-guide.md](19-cross-project-porting-guide.md).
2. For each item you port, open the topical document, read the final design and the *Pitfalls* line, then follow
   the paths in [20-source-map.md](20-source-map.md).
3. Treat the class tag on each item as a porting instruction (see below). Never copy a `GAME_ADAPTATION` or
   `GEOMETRY_WARS_SPECIFIC` address into another game.
4. Compare with the same-numbered document in the other repositories (file names and section structure are kept
   identical on purpose).

### Classification used everywhere

| Tag | Meaning |
| --- | --- |
| `GENERIC_REXGLUE` | Probably wanted by almost every ReXGlue project |
| `REUSABLE_INFRASTRUCTURE` | Launcher, user data, packaging, input and utilities that port with little change |
| `PLATFORM_WINDOWS` / `PLATFORM_LINUX` | Reusable platform-specific behavior |
| `GAME_ADAPTATION` | Good pattern, but needs the new game's offsets, symbols or state |
| `GEOMETRY_WARS_SPECIFIC` | Do not copy directly (addresses, mode ids, the XBLA package, cheats) |

Path convention: repository-relative paths are this repository. Paths starting with `src/system`, `src/graphics`, `src/ui`, `src/core`, `src/kernel` or `include/rex` that appear next to a patch number are inside the ReXGlue SDK source tree (tag `v0.10.0`), not this repository. `generated/`, `tools/rexglue/`, `out/`, `build/`, `dist/` and `user_data/` are local or git-ignored.

Confidence is written `CONFIRMED` (code and, where noted, a test or user report back it), `LIKELY` (consistent with the
code and history, not proven) or `UNCERTAIN`. Where nothing is written the statement is read directly from code in the
repository.

## Documents

| # | Document | Content |
| --- | --- | --- |
| 01 | [rexglue-core](01-rexglue-core.md) | The patch model and every SDK patch, what is generic |
| 02 | [build-system](02-build-system.md) | CMake, presets, SDK discovery, patched-runtime checks, ThinLTO, codegen |
| 03 | [launcher](03-launcher.md) | TRG launcher architecture, install, settings, restart, window state |
| 04 | [platform-windows](04-platform-windows.md) | Win32 specifics |
| 05 | [platform-linux](05-platform-linux.md) | SDL, AppImage, RUNPATH, RenderDoc, pickers |
| 06 | [graphics-vulkan](06-graphics-vulkan.md) | Vulkan/D3D12 fixes, MSAA, bloom, adapters |
| 07 | [display-window-frame-pacing](07-display-window-frame-pacing.md) | Window modes, frame limiter, present, time step |
| 08 | [input-gamepad-mouse](08-input-gamepad-mouse.md) | Gamepad to ImGui, hotkeys, cursor hiding, input gating |
| 09 | [audio](09-audio.md) | What exists (almost nothing custom) |
| 10 | [user-data-settings-migration](10-user-data-settings-migration.md) | Portable `user_data`, migration, settings file |
| 11 | [offline-services-profile-achievements](11-offline-services-profile-achievements.md) | Profile, Live scores, achievements |
| 12 | [performance](12-performance.md) | Chronological performance work |
| 13 | [crash-hardening-threading-memory](13-crash-hardening-threading-memory.md) | Crash and teardown fixes |
| 14 | [packaging-release](14-packaging-release.md) | ZIP and AppImage reproduction and validation |
| 15 | [debugging-logging](15-debugging-logging.md) | Logs, diagnostics, tests as diagnostics |
| 16 | [reusable-game-features](16-reusable-game-features.md) | Hook patterns, cheat/menu architecture |
| 17 | [porting-checklist](17-porting-checklist.md) | Phased checklist |
| 18 | [change-history](18-change-history.md) | Curated history |
| 19 | [cross-project-porting-guide](19-cross-project-porting-guide.md) | What to port first, order |
| 20 | [source-map](20-source-map.md) | Subsystem to files |

## Quick start for a new ReXGlue project

1. [19-cross-project-porting-guide.md](19-cross-project-porting-guide.md): the prioritized list and port order.
2. [01-rexglue-core.md](01-rexglue-core.md): the patch model and the exact per-patch inventory. The adoption tiers (MUST HAVE, HIGH VALUE, OPTIONAL, GAME SPECIFIC) are defined only in document 19; take the patch list for each tier from there.
3. [02-build-system.md](02-build-system.md): copy the SDK discovery, patched-runtime check and staging checks.
4. [10-user-data-settings-migration.md](10-user-data-settings-migration.md) *before the first public release*.
5. [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md) and
   [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md).
6. [03-launcher.md](03-launcher.md), [05-platform-linux.md](05-platform-linux.md), [14-packaging-release.md](14-packaging-release.md).
7. Walk [17-porting-checklist.md](17-porting-checklist.md) phase by phase.

## Recommended common ReXGlue baseline

Candidates for every new project. Each links to its authoritative section. "Game assumptions" are what must hold
for the item to be correct.

| Item | Why | Game assumptions | Where |
| --- | --- | --- | --- |
| Patch `0001` physical heap release order | Stock `PhysicalHeap::Release` can corrupt allocator state under two threads (CONFIRMED by the stress test; LIKELY as the cause of the reported startup crash, which was never proven) | none | [01](01-rexglue-core.md), [13](13-crash-hardening-threading-memory.md) |
| Patch `0033` stop title threads before teardown | Exit could unmap guest memory while a guest thread still ran | none; also needs the app to call input `Shutdown()` before the window dies | [13](13-crash-hardening-threading-memory.md) |
| Patch `0003` RenderDoc load-only detection (Linux) | RenderDoc detection itself loaded RenderDoc whenever installed | none | [01](01-rexglue-core.md), [05](05-platform-linux.md) |
| Patch `0002` SPIR-V `exp_adjust` from fetch word 3 (Linux Vulkan) | Dark image on Vulkan | any game that uses texture exponent bias | [06](06-graphics-vulkan.md) |
| Patch `0035` cache Vulkan swapchain paint pipeline | Pipeline rebuilt on every paint | none | [06](06-graphics-vulkan.md) |
| Patches `0004`-`0007`, `0008`, `0009`-`0011`, `0020`, `0021`, `0025` (register path, waits, render target cost, timers; `0020`/`0021` Linux only) | CPU and wait overhead | validated only on this game; see risk notes | [12](12-performance.md) |
| Patches `0026`/`0027` GPU preference | Hybrid laptops picked the iGPU | none | [06](06-graphics-vulkan.md) |
| Patch `0029` UI repaint with guest frames | Two presents per guest frame while an ImGui dialog exists | changes `Window` ABI (new last virtual) | [07](07-display-window-frame-pacing.md) |
| `patches.txt` + `baseline.env` + stamp-checked patched runtime build | Reproducible, stale-proof SDK patching on both OSes | SDK pinned by tag and commit | [02](02-build-system.md) |
| Portable `user_data` with verified migration | Avoids a painful migration after release | none | [10](10-user-data-settings-migration.md) |
| Launcher state kept separate from game display state | Fixed a real regression (`b46a22f`) | uses TRG launcher | [03](03-launcher.md), [07](07-display-window-frame-pacing.md) |
| `SetApplicationId`, AppImage `PortFolder()`, `$ORIGIN` RUNPATH check, release-content scan | Linux packaging that survives read-only mounts | AppImage | [05](05-platform-linux.md), [14](14-packaging-release.md) |
| Shutdown order: input `Shutdown()` in `OnShutdown` | Input drivers detached from an already destroyed window | none | [13](13-crash-hardening-threading-memory.md) |
| Fast CRT `memcpy`/`memset` hooks | Guest CRT copies dominate | `GAME_ADAPTATION`: addresses differ per game | [12](12-performance.md) |

## High-value items to check first in a new project

1. Does codegen register every function reached only through pointers? (`tools/find-missing-functions.py`, `setjmp_address`/`longjmp_address` in the manifest.) See [01](01-rexglue-core.md).
2. Does the game read user data anywhere ReXGlue defaults to? Fix the root before release ([10](10-user-data-settings-migration.md)).
3. Does the game poll a GPU fence or `WAIT_REG_MEM` and burn CPU? ([12](12-performance.md)).
4. What does the game do on exit? Check for threads in long delays ([13](13-crash-hardening-threading-memory.md)).
5. On a first run, does the game reject the SDK's "no value" profile answer? (Only then is a Geometry Wars-style hook needed.) ([11](11-offline-services-profile-achievements.md)).

## Maintenance

Re-running the generation prompt updates these files incrementally: read the HEAD recorded above, inspect
`git log <HEAD>..HEAD`, edit only the affected documents, and update the HEAD and date rows here and
[18-change-history.md](18-change-history.md). Existing project documents in `docs/` remain the long-form
source for individual features; this reference points into them instead of copying them.
