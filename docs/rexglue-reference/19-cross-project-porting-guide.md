# 19 Cross-project porting guide

Question answered: *I have a fresh ReXGlue project today. What do I port from this repository first?*
Source paths are in [20](20-source-map.md). Class tags as in the [README](README.md#classification-used-everywhere).

## Prerequisites assumed
Same pinned ReXGlue (v0.10.0, commit `f5337cdc`) or you re-validate every patch with `git apply --check`. Windows
(clang-cl via Visual Studio) and Linux (clang-20, Ubuntu 20.04) toolchains as in [02](02-build-system.md).

## MUST HAVE

| Component | Source | Depends on | Conflicts to expect | Assumptions | Validate |
| --- | --- | --- | --- | --- | --- |
| Patch infrastructure (`patches.txt`, `baseline.env`, builder scripts, stamp, CMake check/staging) | `patches/rexglue-sdk/{patches.txt,baseline.env}`, `tools/build-patched-rexruntime.*`, `tools/rexruntime-patch.ps1`, `CMakeLists.txt` check targets | pinned SDK | Different SDK commit; different target set | The project links `rex::runtime`, `rex::gpu-xenos` | `--check` fails on stock files; passes after prepare |
| `0001` physical heap release order | `patches/rexglue-sdk/0001-*.patch`, `tests/physical_heap_stress.cpp` | infrastructure | Already fixed upstream | none | stress test fails stock/passes patched |
| `0033` stop title threads + `OnShutdown` input shutdown | `patches/.../0033-*.patch`, app header | infrastructure | Touches `kernel_state.h`, `xthread.h` (ABI) | game exit through `XamLoaderLaunchTitle` | `gw2_title_thread_shutdown_tests`, `gw2_shutdown_order_tests` |
| User data root decision | `src/user_data.*`, `PortFolder` | none | Existing released data in another place | final root chosen | `tests/user_data_tests.cpp` |
| Codegen manifest practices (overrides, discovery tool; `setjmp`/`longjmp` addresses when the game uses them) | manifest, `config/*`, `tools/find-missing-functions.py` | decrypted image | none | the game's CRT functions identified if it uses them | codegen 0 warnings; `--verify` |
| Release-content scans | `tools/package-release.ps1` `Test-ReleaseFolder`, AppDir check in `package-linux.sh` | packagers | different layout | | deliberately add a forbidden file |

## HIGH VALUE

| Component | Source | Depends on | Conflicts | Assumptions | Validate |
| --- | --- | --- | --- | --- | --- |
| `0003` RenderDoc load-only (Linux) | patch + `tests/renderdoc_detect_tests.cpp` | infrastructure | | Linux builds | loader test (control run fails) |
| `0002` SPIR-V `exp_adjust` word 3 (Vulkan) | patch | infrastructure | maybe fixed upstream | Vulkan used | brightness vs D3D12 |
| `0035` cache paint pipeline (Linux) | patch | infrastructure | | | pacing/perf trace |
| `0026`/`0027` GPU preference | patches | infrastructure | | hybrid GPUs | laptop test |
| `0029` UI repaint with guest frames (+ window host accounting) | patch + `tests/presenter_pacing_tests.cpp` | infrastructure; ImGui dialogs present | `Window` ABI (new last virtual) | | pacing test |
| Conditional (`GAME_ADAPTATION`): first-run profile answer | `src/profile_fix.*` | game wrapper address | SDK may have changed the answer | First inspect how the new game reacts to an unsaved title-specific profile setting; implement the default-value hook only if the SDK's NO_VALUE answer makes the game fail | fresh `user_data`, 2 runs |
| Launcher (TRG) + settings model + PLAY gate + restart | `src/launcher*.{h,cpp}`, `src/game_files.*`, `src/stfs.*` (STFS installer only if the title ships as STFS), submodule | user data, ReXApp hooks | Window creation order, cvar names | game data arrives in a form the installer/importer handles (architecture: fingerprint, temporary extraction, verification, safe swap, rollback) | launcher test suites, manual list |
| Launcher/game window state separation | `src/launcher_placement.h`, app header | launcher | | | `launcher_placement_tests` |
| Linux platform layer + AppImage packaging | `src/launcher_platform_linux.cpp`, `packaging/linux/*`, `tools/package-linux.sh` | launcher | desktop/app id naming | Ubuntu 22.04 builder | packager checks; real hardware |
| Gamepad-to-ImGui | `LauncherGamepad` (win32/linux) | launcher | SDL subsystem counting | | controller-only use |
| Wait, timer, register-path and render target cost patches: `0004`-`0007`, `0008`, `0009`-`0011`, `0016` (Windows), `0020`/`0021` (Linux), `0025`, with the backend halves `0012`-`0015` (D3D12) / `0017`-`0019` (Vulkan) | patches | infrastructure | context drift in patches against your SDK | measured benefit | per-group measurement ([12](12-performance.md)) |
| Frame limiter + sampler + title/counter | `src/frame_limiter.*`, `src/frame_wait.cpp`, `src/window_host.*`, `src/frame_counter.*` | game addresses | | game's present/time-step identified (`GAME_ADAPTATION`) | `frame_limiter_tests`; manual |

## OPTIONAL

| Component | Source | Notes |
| --- | --- | --- |
| Resolve copy / deferred writeback `0022`-`0024`, `0030`-`0032` | patches | Highest complexity; take only with measurement and per-game visual checks |
| Fence wait hook | `src/gpu_fence_wait.cpp` | `GAME_ADAPTATION`; needs the game's fence loop |
| CRT native `memcpy`/`memset` | `src/crt_native.cpp` | `GAME_ADAPTATION`; confirm ABI and stack side effects |
| Cursor visibility | `src/window_host.*` | Last-input state machine is portable |
| ThinLTO | `CMakeLists.txt` `gw2_enable_thinlto` | Needs lld on Linux |
| Offline Live services | `src/live_*.{h,cpp}`, `src/local_stats.*`, `src/achievements_overlay*` | Only if the game uses Xbox Live stats/achievements |
| Icon pipeline | `tools/make-app-icon.py`, `packaging/icons` | |
| Analysis docs | `docs/package-analysis.md` | |

## GAME SPECIFIC (do not copy)
Addresses and vtables in `src/cheat_hooks.cpp`, `src/cheats.*` rules, `src/bloom_fix.cpp` addresses, `kGameDeltaAddress`,
fence-wait sites, `kSupportedGame` fingerprint, mode ids, `0x63E83FFF` profile layout, view ids/stats schema,
`config/overrides.toml`, `config/discovered-functions.toml`, achievements count/size choices.

## RECOMMENDED PORT ORDER
Minimize interacting systems; validate each step before the next.

1. Recompile and boot ([17](17-porting-checklist.md) phase 1).
2. Build system: presets, scripts, `.gitignore`/`.gitattributes`, patch infrastructure with an empty `patches.txt` (checks pass on stock files). Add `0001` and its stress test.
3. User data root + log folder + shipped toml (before any save exists).
4. ReXApp skeleton with hooks, settings model, `LauncherWanted`; launcher without cheats/achievements; the game-data installer/importer for the title's distribution (reuse the STFS implementation if it is a compatible STFS package).
5. Linux platform layer and its build (even if untested) so abstractions stay honest.
6. `0033` + shutdown tests; `OnShutdown` input shutdown. Run 20 start/quit cycles.
7. Graphics correctness patches: `0003`, `0002`, `0035`, `0026`/`0027`.
8. Window host (F11, title), frame sampler; then the limiter and time-step work if the game needs more than 60 FPS.
9. Performance batch 1 (register path, waits, timers): `0004`, `0005`, `0008`, `0025`, plus `0016` on Windows and `0020`, `0021` on Linux; measure.
10. Performance batch 2 (draw path, render target and EDRAM cost, fence wake): `0006`, `0007`, `0009`, `0010`, the D3D12 halves `0012`-`0015` (Windows) or the Vulkan counterparts `0017`-`0019` (Linux), then `0011` with the game's fence hook.
11. `0029` and repeated-present accounting.
12. Optional deep GPU work: `0022`-`0024`, `0030`-`0032`.
13. Packaging scripts and scans; icon; first release candidate on both OSes.
14. Offline services, cheats and other game features last.

Rationale: items 2-6 are low interaction and prevent unrecoverable mistakes (data location, stale runtime, exit
crashes). Items 9-12 touch the same GPU files and must be applied in groups so a visual regression can be bisected by
patch number.

## Validation policy
During porting validate only the current change; the comprehensive run is reserved for the final review of the branch (Phase 11 of [17](17-porting-checklist.md)).

| When you change | Run |
| --- | --- |
| an SDK patch or `patches.txt` | patch applicability and the patched-runtime check for the affected configuration, build of the affected targets |
| allocator behavior | `gw2_physical_heap_stress` |
| presenter or pacing | `gw2_presenter_pacing_tests` (Linux) |
| title-thread teardown or shutdown order | `gw2_title_thread_shutdown_tests`, `gw2_shutdown_order_tests` |
| RenderDoc/loader behavior | `gw2_renderdoc_detect_tests` (Linux) |
| launcher, settings or placement logic | the matching suite (`launcher_tests`, `launcher_settings_tests`, `launcher_placement_tests`) |
| user data | `user_data_tests` |
| anything | `git diff --check` and no new warnings in the touched targets |

Memory, ABI, hook, concurrency or save-data changes also need the focused tests for that specific risk.

## Integration checks between steps
- After a new patch: the patched-runtime check (changed stamp inputs mean a rebuild) and the focused test for that patch from the table above.
- After touching `Window`/`Presenter`/`KernelState` headers: rebuild the game and run the presenter pacing test (ABI).
- Compare patches with the Hard Corps repository through the numbering map in [01](01-rexglue-core.md#patch-dependencies) (0001 and 0021+ are identical; the rest are offset).
