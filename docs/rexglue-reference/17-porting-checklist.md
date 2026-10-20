# 17 Porting checklist for a new Xbox 360 ReXGlue project

Tick items as you go. Each item names the document with the details. "Gate" lines are the minimum evidence before the
next phase; they are phase-level checks, not a request to run every test after each edit. During a phase validate only the
current change (see [19](19-cross-project-porting-guide.md#validation-policy)); the comprehensive run belongs to Phase 11.

## PHASE 1 - Initial recompilation
- [ ] Extract the game package and record title id, media id, version, XEX SHA-256 ([02](02-build-system.md), `docs/package-analysis.md` format)
- [ ] Pin the ReXGlue SDK by tag, commit and zip SHA-256 per OS ([01](01-rexglue-core.md))
- [ ] Run codegen with the manifest; if the game uses `setjmp`/`longjmp`, set `setjmp_address` / `longjmp_address` to its CRT functions ([01](01-rexglue-core.md#codegen-integration-generic_rexglue-project-data-is-game_adaptation))
- [ ] Dump the decrypted image (`tools/dump_image`) and run `tools/find-missing-functions.py` (`--apply-high`, `--verify`, `--self-test`); keep manual boundaries in `config/overrides.toml`
- [ ] Set `gpu_plugin = "xenos"` in the shipped toml (the Xbox 360 GPU plugin; the only plugin this repository uses)
- [ ] Inspect the title's license/trial behavior and choose its `license_mask`; `license_mask = 1` (full license) is right only when that matches the intended behavior. Geometry Wars needs it because it neither loads nor creates a profile in trial mode, and the SDK default `0` means trial (`GAME_ADAPTATION`, [11](11-offline-services-profile-achievements.md))
- [ ] Decide what the guest video mode should be (`video_mode_width/height`). The SDK ignores a value equal to its default (1280x720) and follows the window size instead, so a game that picks its canvas from the video mode needs a non-default value or a hook. Geometry Wars sets it from the Aspect Ratio and hooks `XGetVideoMode` so the game always builds its 720-line canvas (`GAME_ADAPTATION`, [06](06-graphics-vulkan.md))
- [ ] Check how the game reacts to an unsaved title-specific profile setting; add the default-value hook only if it rejects the SDK's NO_VALUE answer ([11](11-offline-services-profile-achievements.md))
- [ ] Gate: first visible frame, no `[error]` log lines for a few minutes

## PHASE 2 - Build and dependency cleanup
- [ ] Copy `CMakeLists.txt` structure, presets, `tools/build.*`, `tools/codegen.*` ([02](02-build-system.md))
- [ ] Add `patches/rexglue-sdk/` with `patches.txt` and `baseline.env`; start with `0001` ([01](01-rexglue-core.md))
- [ ] Copy `tools/build-patched-rexruntime.{sh,ps1}` and `tools/rexruntime-patch.ps1`; make a plain `cmake --build` fail on a stock runtime
- [ ] Add `.gitattributes` (LF for scripts, patches, manifest) and `.gitignore` (`/tools/rexglue/`, `/generated/`, `user_data/`, `dist/`)
- [ ] Add ThinLTO for Release/RelWithDebInfo if desired
- [ ] Gate: Debug, RelWithDebInfo and Release build; `--check` passes

## PHASE 3 - Launcher integration
- [ ] Add the TRG submodule and the ImGui interface target ([03](03-launcher.md))
- [ ] Port `ReXApp` hooks in the documented order ([01](01-rexglue-core.md#rexapp-integration-points-reusable_infrastructure))
- [ ] Implement the game-data installer/importer for how the title is distributed, reusing the STFS implementation when it ships in a compatible STFS package; keep the fingerprint check, temporary extraction, verification and safe swap with rollback ([03](03-launcher.md)). Replace `GameFingerprint`, title id and content type
- [ ] Port `GameSettings`, `PlayGate`, `LauncherWanted`, restart path (`--<prefix>_skip_launcher`)
- [ ] Gate: install from a package, press PLAY, change a restart-only setting and see one relaunch

## PHASE 4 - User data and settings
- [ ] Decide the final data root before the first public release ([10](10-user-data-settings-migration.md))
- [ ] Port `SelectUserData`, `PortFolder`, logs redirection, cache root follow
- [ ] Write the migration test if any legacy location exists; keep failing-step tests
- [ ] Define the settings file keys and any value migrations (like `native_2x_msaa`) up front
- [ ] Gate: delete `user_data` -> fresh start; copy folder -> data follows

## PHASE 5 - Graphics and display
- [ ] Add `0002` (if Linux Vulkan), `0026`/`0027`, `0035` ([06](06-graphics-vulkan.md))
- [ ] Launcher display page: mode, size, monitor, VSync, aspect, render scale, AA, texture filtering
- [ ] MSAA: map options to what the SDK can really do; add `0028` log ([06](06-graphics-vulkan.md))
- [ ] Separate launcher window state from game display state ([03](03-launcher.md), [07](07-display-window-frame-pacing.md))
- [ ] Gate: compare D3D12 and Vulkan brightness; test a small display and a hi-DPI display

## PHASE 6 - Input
- [ ] Port `LauncherGamepad`, F11 window host, input z-order convention ([08](08-input-gamepad-mouse.md))
- [ ] Last-input cursor visibility (optional) with the pure `CursorHider`
- [ ] Input gating for overlays with release gate
- [ ] Gate: launcher fully usable with controller only; no key reaches the game while an overlay is open

## PHASE 7 - Performance and frame pacing
- [ ] Measure first: CPU use idle/in game, frame times, GPU use ([12](12-performance.md))
- [ ] Apply the patches in the groups of [19](19-cross-project-porting-guide.md#recommended-port-order) (waits and timers `0005`, `0008`, `0025`, `0016` Windows, `0020`/`0021` Linux; register and draw path `0004`, `0006`; render target and EDRAM `0007`, `0009`, `0010` with the D3D12/Vulkan halves; fence wake `0011`; then `0029`), re-measure after each group and run only the focused check for that group
- [ ] Find the game's present callback, time-step variable, repeated-present path ([07](07-display-window-frame-pacing.md))
- [ ] Only if the game needs extra frame-rate options: hook `VdSwap` and the simulation update; add limiter, fixed-rate simulation gate, sampler, title FPS, optional counter (`GAME_ADAPTATION`)
- [ ] Optional: CRT `memcpy`/`memset`, fence wait hook ([12](12-performance.md))
- [ ] Optional high-risk: `0022`-`0024`, `0030`-`0032` with the donor tests
- [ ] Gate: frame rate options behave at 30/60/uncapped; game speed follows wall clock below 60 and simulation stays at the original rate above 60

## PHASE 8 - Crash hardening
- [ ] Add `0033` and the `OnShutdown` input shutdown ([13](13-crash-hardening-threading-memory.md))
- [ ] Add allocator stress, title-thread shutdown and shutdown-order tests
- [ ] Review long-delay guest threads at exit; immortal state for hooks
- [ ] Gate: 20 consecutive start/quit cycles on each OS without hang or crash

## PHASE 9 - Linux support
- [ ] `launcher_platform_linux.cpp` equivalents ([05](05-platform-linux.md)): app id, icon, relaunch, monitors, fonts, gamepad
- [ ] RenderDoc `0003`, `$ORIGIN` RUNPATH check, glibc floor
- [ ] AppRun with libstdc++ policy, desktop file named after the app id
- [ ] Picker policy: `zenity`/`kdialog`, and think about a fallback
- [ ] Gate: run on a clean Ubuntu 20.04 container/VM and on a real machine with Vulkan

## PHASE 10 - Packaging
- [ ] Port both packagers with release-folder / AppDir scans ([14](14-packaging-release.md))
- [ ] Icon pipeline, licenses, `README.txt`, checksum files
- [ ] Gate: `-VerifyOnly` passes; deliberately add a forbidden file and see it rejected

## PHASE 11 - Release validation
- [ ] `tools/test.ps1` and the runtime-linked tests ([15](15-debugging-logging.md))
- [ ] Manual list in `docs/launcher.md` adapted for the game
- [ ] Fresh-machine run of both artifacts; logs reviewed for `warn`/`error`
- [ ] Release notes with the checksum table; known limits written down honestly
- [ ] Update this reference ([README](README.md#maintenance))
