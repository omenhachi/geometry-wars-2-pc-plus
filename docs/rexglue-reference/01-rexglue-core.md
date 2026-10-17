# 01 ReXGlue core: SDK patches and integration

Long-form per-patch detail: `docs/rexglue-runtime-patch.md` and `docs/rexglue-gpu-patch.md`. This file is the
porting view.

## The patch model (`REUSABLE_INFRASTRUCTURE`, copy first)

The project does not fork ReXGlue. It ships the stock v0.10.0 SDK zip (SHA-256 pinned in `tools/codegen.ps1`,
`tools/codegen.sh`, `tools/package-linux.sh`) and rebuilds only two targets, `rexruntime` and `rexgpu-xenos`, from the
pinned source with a numbered patch series.

| Piece | File | Role |
| --- | --- | --- |
| Patch series | `patches/rexglue-sdk/NNNN-name[.platform].patch` | `git apply`-able diffs against the pinned tag |
| Manifest | `patches/rexglue-sdk/patches.txt` | One line per patch: file, platforms (`windows,linux`), targets (`rexruntime,rexgpu-xenos`). The only list; scripts refuse unlisted, duplicate-numbered, or out-of-order patches |
| Pins | `patches/rexglue-sdk/baseline.env` | Tag, commit, SHA-256 of every stock runtime and plugin (Release, RelWithDebInfo `_RD`, Debug `_D`, per OS) |
| Builder | `tools/build-patched-rexruntime.sh`, `tools/build-patched-rexruntime.ps1`, `tools/rexruntime-patch.ps1` | Fetch tag into `out/rexglue-sdk-src`, verify commit, `git checkout -- .` + `git clean` so another branch's patches cannot linger, apply, build per configuration, install into `tools/rexglue/<platform>`, restore stock files for unpatched targets |
| Stamp | `<sdk>/rexruntime-patch.txt` | SDK version/tag/commit, platform, each patch with SHA-256 and targets, compiler, flags, then SHA-256 of installed files. A different input set marks the install stale |
| Checks | `--check` / `-Check` mode, CMake targets `gw2_rexglue_check`, `gw2_rexglue_staging` | Fail the build when the SDK or the copies next to the program are not the patched ones |

Origin: `aa64c6a` (first patch and scripts), generalized in `a86c63f` (normal builds prepare the patched SDK per
configuration; the earlier approach produced a patched runtime only for release packaging). Classification:
`REUSABLE_INFRASTRUCTURE`. History: in `aa64c6a` only the Windows and Linux packagers verified the patched runtime; `a86c63f` moved the check
into every build. LIKELY motive (inferred from the commit title and diff): a plain development build could otherwise
run a stock or other-branch library without noticing. The stamp and per-configuration check exist to prevent that.

### ABI rule for patches `GENERIC_REXGLUE`
The game is compiled against stock SDK headers. A patch must not move virtual functions or data members of classes the
game or `share/rexglue/rex_app.cpp` use (`Window`, public `Presenter`, `KernelState`). New virtuals go last
(`0029` adds `Window::GetDisplayRefreshRate` last); new members must not displace members the game reads inline.
Verify by running the unmodified-header game against the patched DLL (the presenter pacing test does this).

### Upstream candidates vs application integration
- Upstream candidates (generic correctness): `0001`, `0002`, `0003`, `0006` (draw extent and primitive cache invalidation bug), `0033`, `0035`. `0002` is already reported upstream (rexglue-sdk#334, #456) and fixed in Xenia Canary; ReXGlue `main` still lacked it when written.
- Performance and wait/pacing patches (`0004`-`0025` and `0030`-`0032`; `0016`, `0020`, `0021` are platform-specific; `0006` also contains the correctness fix above): ported from Hard Corps: Uprising Recompiled (same pinned commit). `0026`-`0029` are not performance patches: device preference, a diagnostic log, and presentation behavior. Validated here only through this game plus the tests in [15](15-debugging-logging.md); upstreaming needs broader validation.
- Application integration (not SDK): `src/geometry_wars_retro_evolved_2_app.h` hooks, `src/window_host.cpp`, `src/frame_limiter.cpp`.

## ReXApp integration points (`REUSABLE_INFRASTRUCTURE`)

`src/geometry_wars_retro_evolved_2_app.h` subclasses `rex::ReXApp` and uses these hooks. The lifecycle pattern and
ordering are `REUSABLE_INFRASTRUCTURE`; the actions attached to each hook are not copy-verbatim code. Each project should
attach only the services and game adaptations it needs. The right column documents what Geometry Wars does, and
rows are tagged where useful: items tagged `optional` or `GAME_ADAPTATION` depend on the project's features or the game.


| Hook | What this project does |
| --- | --- |
| `Create` | `gw2::SetApplicationId()` before the window exists (Linux app id; `REUSABLE_INFRASTRUCTURE`) |
| `OnConfigurePaths` | (`REUSABLE_INFRASTRUCTURE`, user-data policy per [10](10-user-data-settings-migration.md)) Set `game_data_root` to `<port folder>/game`; choose `user_data_root` (portable + migration); redirect `log_file` to a writable `logs` folder when the executable folder is not the port folder (AppImage) |
| `OnPostInitLogging` | Log the user-data decision; load the launcher's player settings over the shipped config |
| `OnPreSetup` | (launcher/settings, `optional`) Build `GameSettings`, `Capture()` cvars, `MarkWindowCreated()`, `SettleStartupCvars()` (fit window, clear pending-restart flags), place the launcher window |
| `OnCreateDialogs` / `OnConfigureFonts` | Frame counter, cursor tick dialog; launcher fonts (`optional`) |
| `OnFinalizePaths` | Attach window host (F11, cursor; `optional`), icon, `ApplyMultisampling` (`GAME_ADAPTATION`: this game's MSAA option), decide whether to show the launcher before the runtime exists (game files missing) |
| `LaunchModule` | Show the launcher after the runtime exists (valid files) or start the game |
| `OnPostSetup` | Install offline Live services once the user profile exists (`GAME_ADAPTATION`, only if the game uses Live stats) |
| `OnShutdown` | Shut input down, detach cheat menu/window host, close overlays |
| `CreateAchievementsOverlay` | Map F7 to the project's own overlay (`optional`, `GAME_ADAPTATION`) |

Related ReXGlue facts discovered and relied on:
- A restart-only cvar set from a config file is marked "pending restart" by the SDK even though nothing it configures exists yet; `rex::cvar::ClearPendingRestartFlags()` after load makes PLAY restart only for launcher edits (`gw2::SettleStartupCvars`, `src/launcher_cvars.cpp`). `CONFIRMED` by `tests/restart_state_tests.cpp`.
- `native_2x_msaa` is read once when the render target cache is created; changing it needs a new process (cvar `gw2_msaa_compatibility` is `Lifecycle::kRequiresRestart`).
- `--user_data_root` explicit overrides `cache_root` only if `cache_root` is unset (see [10](10-user-data-settings-migration.md)).
- ReXGlue's native window handle is empty on Linux; code that needs the SDL window enumerates `SDL_GetWindows` (`LauncherWindow()` in `src/launcher_platform_linux.cpp`).

## Codegen integration (`GENERIC_REXGLUE`, project data is `GAME_ADAPTATION`)

### Unregistered functions and `setjmp`/`longjmp`
- Problem: first boot failed with `Call to invalid or unregistered function` at an indirect call, then a null-object read at guest `0x1A4`.
- Cause: codegen does not register functions reached only through `lis`/`addi` pairs or data pointers; this-adjusting thunks were not split. The second failure was caused (LIKELY, inferred from the outcome, not traced) by a non-local jump returning into the wrong host frame.
- Solution: `config/overrides.toml` (function boundary hints), `config/discovered-functions.toml` generated by `tools/find-missing-functions.py --apply-high` (HIGH-confidence candidates only), and `setjmp_address`/`longjmp_address` in `geometry_wars_retro_evolved_2_manifest.toml`.
- Origin: `38a1d61`, `3997f22` (discovery hardening; dropped 10 LOW candidates caused by false lis/addi pairs). The script says it was ported from the Hard Corps project.
- Port: check whether the game uses `setjmp`/`longjmp` and, if so, find its CRT functions first (the SDK supports the manifest keys; Geometry Wars needed them, other titles may not). Re-run the discovery tool for each new game; its image offsets (`RDATA`, `TEXT`, `IMPORTS`, `CXX_EH_MAGIC`) are per-title constants. Pitfall: `.gitattributes` forces LF on the manifest because codegen rewrites it with LF every run.
- Verify: `python tools/find-missing-functions.py --verify` and `--self-test`; codegen with 0 warnings.

## Patch inventory

Platform column: B both, W Windows (D3D12), L Linux (Vulkan). Hard Corps numbering in `docs/rexglue-runtime-patch.md`.
All are `GENERIC_REXGLUE` unless noted; "needs validation" means the behavior change is performance/pacing related and
was validated on this game only.

| # | P | Target | Topic | Section |
| --- | --- | --- | --- | --- |
| 0001 | B | runtime | Physical heap release order | [13](13-crash-hardening-threading-memory.md) |
| 0002 | L | gpu | SPIR-V `exp_adjust` word 3 | [06](06-graphics-vulkan.md) |
| 0003 | L | both | RenderDoc detection load-only | [05](05-platform-linux.md) |
| 0004 | B | gpu | Register write bulk path | [12](12-performance.md) |
| 0005 | B | gpu | Idle command thread and `WAIT_REG_MEM` polling, `PollBackgroundWork` hook | [12](12-performance.md) |
| 0006 | B | gpu | Draw extent, page-state and primitive cache defaults, shared memory request allocation (includes a cache invalidation bug fix) | [12](12-performance.md) |
| 0007 | B | gpu | Render target cache: draw-overwrite proof, stencil bit tracking | [12](12-performance.md) |
| 0008 | B | runtime + gpu | Perf counter batching, guest vblank thread, timer queue thread, critical-section spin (touches `src/core`, `src/kernel`, `graphics_system.cpp`) | [12](12-performance.md), [07](07-display-window-frame-pacing.md) |
| 0009 | B | gpu | Render target cache: per-tile stencil regions, first row of a draw | [12](12-performance.md) |
| 0010 | B | gpu | Render target cache: dirty-tile EDRAM dumps | [12](12-performance.md) |
| 0011 | B | gpu | Guest fence wake (`guest_wake.{h,cpp}`) | [12](12-performance.md) |
| 0012 | W | gpu | D3D12 submission limit, deferred occlusion queries, resolve preflight (backend half of 0005) | [12](12-performance.md) |
| 0013 | W | gpu | D3D12 transfers (backend half of 0007) | [12](12-performance.md) |
| 0014 | W | gpu | D3D12 stencil regions (backend half of 0009) | [12](12-performance.md) |
| 0015 | W | gpu | D3D12 dirty-tile dumps (backend half of 0010) | [12](12-performance.md) |
| 0016 | W | runtime | High-resolution `Sleep` | [12](12-performance.md) |
| 0017 | L | gpu | Vulkan submission limit and deferred occlusion queries (counterpart of 0012) | [12](12-performance.md) |
| 0018 | L | gpu | Vulkan transfers, resolve preflight, per-tile stencil bits (counterpart of 0013/0014) | [12](12-performance.md) |
| 0019 | L | gpu | Vulkan dirty-tile dumps (counterpart of 0015) | [12](12-performance.md) |
| 0020 | L | gpu | Non-alertable idle command thread wait | [12](12-performance.md) |
| 0021 | L | runtime | `WaitMultiple` contention backoff | [12](12-performance.md) |
| 0022 | B | gpu | Resolve copy into textures (common half) | [12](12-performance.md) |
| 0023 | W | gpu | D3D12 resolve copies | [12](12-performance.md) |
| 0024 | L | runtime + gpu | Vulkan resolve copies (`vkCmdCopyImage`, device function entry) | [12](12-performance.md) |
| 0025 | B | gpu | `WAIT_REG_MEM` woken by vblank | [07](07-display-window-frame-pacing.md) |
| 0026 | L | runtime | Vulkan device preference | [06](06-graphics-vulkan.md) |
| 0027 | W | runtime | D3D12 adapter preference | [06](06-graphics-vulkan.md) |
| 0028 | B | gpu | Log host MSAA samples | [06](06-graphics-vulkan.md) |
| 0029 | B | runtime + gpu | UI repaint with guest frames | [07](07-display-window-frame-pacing.md) |
| 0030 | B | gpu | Deferred resolve writeback (common half) | [12](12-performance.md) |
| 0031 | W | gpu | D3D12 deferred writeback | [12](12-performance.md) |
| 0032 | L | gpu | Vulkan deferred writeback | [12](12-performance.md) |
| 0033 | B | runtime + gpu | Stop title threads (touches `src/system`, headers shared by both targets) | [13](13-crash-hardening-threading-memory.md) |
| 0035 | L | runtime | Cache swapchain paint pipeline | [06](06-graphics-vulkan.md) |

There is no 0034 (Hard Corps' 0034 is this project's 0002).

### 0001 Physical heap release order
- Problem/symptoms: `BaseHeap::Release failed because address is not a region start`, then a view page table that no longer matched memory in use. Reported externally as a startup crash (`fix/startup-crash`, shipped v0.2.1).
- Root cause: `PhysicalHeap::Release` freed the shared parent pages first and cleared the view entries last, with callbacks that drop the global lock in between; a second thread could reallocate the freed pages and pin them over stale view entries, and the first release then erased the new allocation's entries.
- Solution: release the view first, parent pages last; callbacks still run first. File `src/system/xmemory.cpp`. Origin `aa64c6a`.
- Test: `tests/physical_heap_stress.cpp` (target `gw2_physical_heap_stress`): 8 threads allocate/release on `0xA0000000` and `0xE0000000`. Fails on stock every run, passes patched. `CONFIRMED`.
- Is it the cause of the user's startup crash? `UNCERTAIN` (cause unproven; the allocator bug is real and fixed).
- Port: copy directly; check whether the SDK version already contains the fix (`git apply --check`).

### 0002 SPIR-V texture exponent bias
See [06](06-graphics-vulkan.md).

### 0003 RenderDoc detection
See [05](05-platform-linux.md). Adds `DynamicLibrary::LoadExisting` (`include/rex/platform/dynlib.h`, `dynlib_posix.cpp`, `dynlib_win.cpp`); `RenderDocAPI::CreateIfConnected` (`src/ui/renderdoc_api.cpp`) uses it.

### 0033 Stop title threads
See [13](13-crash-hardening-threading-memory.md). Touches `KernelState::TerminateTitle`, `XThread::StopTitleThreads`, `Runtime::Shutdown`, `XObject::Wait`.

### Performance series
See [12](12-performance.md) for before/problem/change/effect per patch group.

## Patch dependencies
- 0012-0015 and 0017-0019 depend on the common halves 0005-0010 (base class hooks like `PollBackgroundWork`).
- 0022 is the prerequisite of 0023/0024 and of 0030-0032.
- 0011 pairs with the game hook `src/gpu_fence_wait.cpp` (see [12](12-performance.md)).
- 0029 pairs with `src/window_host.cpp` frame accounting ([07](07-display-window-frame-pacing.md)).
- Numbering differs from the donor (Hard Corps): GW2 0001 = HC 0001; 0002 = HC 0034; 0003 = HC 0010; 0004-0011 = HC 0002-0009; 0012-0020 = HC 0011-0019; 0021-0033 and 0035 are identical. The full column is in `docs/rexglue-runtime-patch.md`. In a new project, adopt the donor's numbering for cherry-picking convenience, or keep a mapping table like that one.

## How to verify
Per-patch tests are listed in [15](15-debugging-logging.md#tests-as-diagnostics). Generic gate: `tools/build-patched-rexruntime.sh --check <sdk> --config <cfg>` or `-Check`.
