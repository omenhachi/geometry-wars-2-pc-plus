# 20 Source map

Subsystem to files that implement reusable infrastructure or fixes. Paths are repository-relative.

## ReXGlue patches and patch infrastructure
- `patches/rexglue-sdk/*.patch`, `patches.txt`, `baseline.env`
- `tools/build-patched-rexruntime.sh`, `tools/build-patched-rexruntime.ps1`, `tools/rexruntime-patch.ps1`
- `docs/rexglue-runtime-patch.md`, `docs/rexglue-gpu-patch.md`
- `CMakeLists.txt` (`gw2_rexglue_check`, `gw2_rexglue_staging`)

## Build and codegen
- `CMakeLists.txt`, `CMakePresets.json`
- `tools/build.ps1`, `tools/build.sh`, `tools/codegen.ps1`, `tools/codegen.sh`
- `geometry_wars_retro_evolved_2_manifest.toml`, `config/overrides.toml`, `config/discovered-functions.toml`, `config/geometry_wars_retro_evolved_2.toml`
- `tools/find-missing-functions.py`, `tools/dump_image/`
- `tools/extract_stfs.ps1`, `tools/extract_stfs.sh`, `tools/StfsExtract.cs`, `tools/extract_stfs/main.cpp`
- `.gitattributes`, `.gitignore`, `.gitmodules`

## App skeleton (ReXApp hooks)
- `src/main.cpp`, `src/geometry_wars_retro_evolved_2_app.h`

## Launcher
- `src/launcher.{h,cpp}`, `src/launcher_settings.{h,cpp}`, `src/launcher_options.{h,cpp}`, `src/launcher_cvars.{h,cpp}`, `src/launcher_play.h`, `src/launcher_placement.h`, `src/launcher_achievements.{h,cpp}`
- `src/game_files.{h,cpp}`, `src/stfs.{h,cpp}`, `src/sha256.h`
- `third_party/trg-launcher` (submodule)
- Tests: `tests/launcher_tests.cpp`, `tests/launcher_settings_tests.cpp`, `tests/launcher_placement_tests.cpp`, `tests/restart_state_tests.cpp`

## Windows
- `src/launcher_platform_win32.cpp`, `src/frame_wait.cpp`, `packaging/windows/geometry_wars_retro_evolved_2.rc`, `tools/package-release.ps1`
- Patches `0012`-`0016`, `0023`, `0027`, `0031`

## Linux
- `src/launcher_platform_linux.cpp`, `src/launcher_platform.cpp`, `packaging/linux/AppRun`, `packaging/linux/io.github.saulob.geometry-wars-retro-evolved-2.desktop`
- `tools/package-linux.sh`, `tools/build.sh`, `tools/codegen.sh`
- Patches `0002`, `0003`, `0017`-`0021`, `0024`, `0026`, `0032`, `0035`
- `tests/renderdoc_detect_tests.cpp`, `tests/fake_renderdoc.cpp`, `tests/presenter_pacing_tests.cpp`

## User data and settings
- `src/user_data.{h,cpp}`, `src/launcher_platform.{h,cpp}` (`PortFolder`, `NextLogFile`), `src/launcher_settings.{h,cpp}`, `src/launcher_cvars.cpp`
- `tests/user_data_tests.cpp`, `docs/user-data.md`

## Display, frame pacing
- `src/frame_limiter.{h,cpp}`, `src/frame_wait.cpp`, `src/window_host.{h,cpp}`, `src/frame_counter.{h,cpp}`, `src/launcher_placement.h`
- Patches `0008`, `0025`, `0029`
- Tests: `tests/frame_limiter_tests.cpp`, `tests/window_host_tests.cpp`, `tests/frame_counter_tests.cpp`, `tests/presenter_pacing_tests.cpp`

## Graphics
- Patches `0002`, `0003`, `0026`-`0028`, `0035`; `src/multisampling.h`; `src/bloom_fix.cpp`, `src/bloom_rules.h`

## Input
- `LauncherGamepad` in `src/launcher_platform_win32.cpp` / `launcher_platform_linux.cpp` (declared in `src/launcher_platform.h`)
- `src/window_host.cpp` (F11, cursor), `src/cheat_menu.{h,cpp}`, `src/cheat_menu_rules.h`, `src/achievements_overlay_rules.h`

## Offline services
- `src/profile_fix.{h,cpp}`, `src/live_fix.{h,cpp}`, `src/live_stats.{h,cpp}`, `src/local_stats.{h,cpp}`, `src/achievements_overlay.{h,cpp}`, `src/guest_memory.h`
- `tests/live_tests.cpp`, `tests/achievements_overlay_rules_tests.cpp`, `docs/offline-live.md`, `docs/offline-profile.md`

## Performance
- Patches `0004`-`0025` (including the platform halves `0012`-`0021`) and `0030`-`0032`; `src/gpu_fence_wait.cpp`, `src/crt_native.cpp`; ThinLTO in `CMakeLists.txt`

## Crash hardening
- Patches `0001`, `0033`; `OnShutdown` in `src/geometry_wars_retro_evolved_2_app.h`; `src/live_fix.cpp` (immortal state)
- `tests/physical_heap_stress.cpp`, `tests/title_thread_shutdown_tests.cpp`, `tests/shutdown_order_tests.cpp`

## Game features
- `src/cheats.{h,cpp}`, `src/cheat_rules.h`, `src/cheat_hooks.cpp`, `src/cheat_menu*.{h,cpp}`, `tests/cheats_tests.cpp`, `docs/cheats.md`

## Packaging
- `tools/package-release.ps1`, `tools/package-linux.sh`, `tools/make-app-icon.py`, `tools/test.ps1`
- `packaging/` (README.txt, icons, licenses, linux, windows), `.recomp.json`, `.github/release-notes-*.md`
