# 15 Debugging and logging

## Logs
- ReXGlue writes `<exe dir>/logs/<name>_NNN.log` by default (first-boot notes; `build\logs\geometry_wars_retro_evolved_2_NNN.log` in development builds).
- `OnConfigurePaths` redirects `log_file` (via `rex::cvar::SetFlagByName`) when the executable folder is not the port folder - the AppImage case, whose mount is read-only. `gw2::NextLogFile(folder, name)` picks the next free `<name>_NNN.log`, creating the folder; tries `<port folder>/logs`, then `<user_data>/logs`. An explicit `log_file` cvar wins.
- Startup lines worth keeping in a new project: `User data: ...` (one per start), `Player settings: <file>`, `Multisampling: ...`, `Profile read: ...` (max 8), `Local Live services ready, stats file loaded|not created yet|was corrupt`, `Window reduced from WxH to WxH to fit the display`, `Restarted in a new window; hard-exiting this process.`
- Console: `rex::FlushLogging()` before any hard exit.
- No Vulkan on Linux: the log has `Graphics presentation setup failed: <status>` and then `Graphics unavailable; opening the launcher without graphics`; the process exits with status 1 after the software launcher closes.
- Users are asked to attach the newest file from `logs` to issues (`packaging/README.txt`).

## SDK-side diagnostics added by patches
- `0028`: both render target caches log once which host sample count 2x MSAA targets get and why.
- Perf counters: non-Release builds compile the game with `REXGLUE_ENABLE_PERF_COUNTERS`; `0008` makes the counter cheap.
- `STUB` warnings for unimplemented imports (`XamShowGamerCardUIForXUID`, `XamShowMarketplaceUI`, ...).

## Runtime overlays (SDK)
F3 debug overlay (frame stats are fed from the project via `SetGuestFrameStats(DebugOverlayFrameStats)`), backtick console, F4 settings. The project adds F2 frame counter and the window-title FPS (`gw2_show_fps`).

## Debug builds
`Debug` and `RelWithDebInfo` use the `d`/`rd` runtime variants and need the patched runtime prepared for that configuration ([02](02-build-system.md)). `RelWithDebInfo` is the default of `build.ps1`; Debug skips ThinLTO.

## RenderDoc
Linux: detection only attaches to an already loaded RenderDoc (`0003`); use `renderdoccmd capture` or preload to capture. AppImage builds must not contain RenderDoc or layer JSON. Windows needs no patch: `LoadLibraryW` only searches the program folder, the system folders and `PATH`.

## Environment variables
`VK_ICD_FILENAMES` (lavapipe for tests), `SDL_VIDEO_DRIVER` (`offscreen`/`x11` for tests), `DRI_PRIME`/`MESA_VK_DEVICE_SELECT`/`VK_LOADER_DEVICE_SELECT`/`__NV_PRIME_RENDER_OFFLOAD` (respected by `0026`), `GW2_WORK`, `GW2_TRG_LAUNCHER_DIR` (tests), `REXGLUE_PLATFORM`.

## Tests as diagnostics
Tests that run against the stock runtime too (control run must fail) are the most valuable reproductions:

| Target / file | Purpose | Platforms | Command |
| --- | --- | --- | --- |
| `gw2_physical_heap_stress` (`tests/physical_heap_stress.cpp`) | 0001 | both | `build-patched-rexruntime.ps1 -StressTest -SkipBuild`; `...sh --stress <sdk> <build>` |
| `gw2_renderdoc_detect_tests` | 0003, three cases + stock control | Linux | `...sh --loader-test <sdk> <build>` |
| `gw2_presenter_pacing_tests` | 0029, runs the real Vulkan presenter on Xvfb with lavapipe | Linux | `VK_ICD_FILENAMES=... ...sh --pacing-test <sdk> <build>` |
| `gw2_title_thread_shutdown_tests`, `gw2_shutdown_order_tests` | 0033, shutdown order | both | `cmake --build <dir> --target <name>` |
| `gw2_restart_state_tests` | PLAY restart decision vs real cvar registry and shipped config | both | target, needs the runtime |
| `gw2_tests` aggregate: `live_tests`, `achievements_overlay_rules_tests`, `launcher_tests`, `launcher_settings_tests`, `launcher_placement_tests`, `window_host_tests`, `frame_counter_tests`, `frame_limiter_tests`, `user_data_tests`, `cheats_tests` | pure logic, no game files | both | `tools/test.ps1`, or `cmake --build <preset> --target gw2_tests` |

## Codegen/analysis diagnostics
`tools/dump_image` writes the decrypted image; `tools/find-missing-functions.py` (`--runtime-address 0xADDR` explains one address, `--verify`, `--self-test`) writes `build/missing-functions-report.txt`. `docs/package-analysis.md` records the package/XEX structure (title id, media id, hashes); reuse its format for each game.

## Historical mechanisms (do not recommend)
- `DEBUG_SAULO` markers: the repository convention for temporary diagnostics (`DEBUG_SAULO` in comments and logs marks code that must be removed in final review). The guest-fault locator `src/debug_saulo_fault.cpp` (Windows vectored exception handler writing `debug_saulo_fault.txt`) was added in `38a1d61` and removed in `9e6f05b`.
- Page-level write tracing of the guest stack and a PowerPC probe of the decrypted image were used during first boot and kept outside the repository.
- `feature/controller-rumble`: temporary rumble diagnostic logging (not at HEAD).
- Obsolete branches: `build/rexglue-auto-rebuild` and `fix/renderdoc-linux-autoload` were superseded by `a86c63f` and the `0003` patch (see [18](18-change-history.md)).
