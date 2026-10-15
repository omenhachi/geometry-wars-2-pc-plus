# 05 Linux platform

Long-form: `docs/linux.md`. Status when written: experimental; real-hardware testing limited to one reporter's Steam
Machine (issue #1). Audio, controllers, performance and >60 FPS were validated on Windows only (`CONFIRMED` by
`docs/linux.md`).

Implementation: `src/launcher_platform_linux.cpp`, `src/launcher_platform.cpp` (`PortFolder`), `packaging/linux/*`,
`tools/package-linux.sh`, `tools/build.sh`, `tools/codegen.sh`, `tools/build-patched-rexruntime.sh`, SDK patches `0002`,
`0003`, `0017`-`0021`, `0024`, `0026`, `0032`, `0035`. Origin: `9e6f05b` (first Linux port), then `b362de8`,
`378f609`, `a86c63f`, `852b042`, `a576d87`, `4067893`.

## What Linux is
A native x86_64 program, not Wine/Proton: the recompiled game links ReXGlue's Linux runtime (`librexruntime.so`) and
Xenos GPU plugin (`librexgpu-xenos.so`, Vulkan backend), SDL3 for window/input, the same TRG launcher. Delivered as
one AppImage that doubles as the Steam Deck build (intended, not confirmed on hardware).

## Source-level differences (`PLATFORM_LINUX`)

| Topic | Implementation | Port notes |
| --- | --- | --- |
| Application id | `SetApplicationId()` sets `SDL_PROP_APP_METADATA_IDENTIFIER_STRING` to `io.github.saulob.geometry-wars-retro-evolved-2` before the window exists; SDL uses it as Wayland `app_id` and X11 `WM_CLASS` class (`b362de8`). The `.desktop` file is named after it and sets `StartupWMClass` to the same string | `package-linux.sh` greps `kApplicationId` out of `launcher_platform_linux.cpp` and fails if the desktop file name, `StartupWMClass` or the string inside the binary differ. Keep the declaration on one line |
| Window icon | `ApplyWindowIcon()` loads `../share/icons/hicolor/256x256/apps/geometry_wars_retro_evolved_2.png` relative to the executable with `SDL_LoadPNG`, so it works without desktop integration. Running outside an AppImage keeps the default icon | Wayland needs the app id + desktop file for taskbar icons |
| Port folder | `PortFolder()`: `$APPIMAGE`'s parent directory when set (the AppImage mount is read-only and disappears on exit), else the executable folder | Core of portable data ([10](10-user-data-settings-migration.md)) |
| Logs | `OnConfigurePaths` redirects `log_file` to `<port folder>/logs` (fallback `user_data/logs`) with `NextLogFile` when the executable folder differs from the port folder | |
| Monitors | `SDL_GetDisplays`, `SDL_GetDisplayBounds`, `SDL_GetDisplayUsableBounds`, `SDL_GetDisplayContentScale`; numbering is SDL's order | Under Wayland with scaling >100% window sizes can come out larger than chosen (ReXGlue sizes its window this way) |
| Launcher window handle | ReXGlue's native handle is empty here; `LauncherWindow()` takes the first SDL window | |
| Placement | `ApplyWindowPlacement`: `SDL_RestoreWindow`, `SDL_SyncWindow`, size, position, maximize. No pre-show hook, so a flash at default size is possible; Wayland cannot restore position | |
| Relaunch | `posix_spawn` of the AppImage path (`$APPIMAGE`) or `/proc/self/exe`, with `/proc/self/cmdline` args plus extra args. In an AppImage this starts the AppImage again because the old mount closes on exit | |
| Shift at start | `SDL_PumpEvents` + `SDL_GetModState` works only after the window has focus | Document to users: keep Shift held until the launcher appears |
| Gamepad (launcher/menus) | `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` started in `Update`, `SDL_QuitSubSystem` in `Release`; SDL counts the starts, so game input is unaffected | [08](08-input-gamepad-mouse.md) |
| Fonts | Probes Noto Sans, DejaVu and Liberation locations in order | |
| Pickers | `zenity`/`kdialog`/`xdg-open` through TRG, not bundled | [03](03-launcher.md) |
| VSync | `vulkan_allow_present_mode_immediate` (Off allows tearing; On uses mailbox/FIFO) | |
| Frame limiter | `std::this_thread::sleep_until(deadline - 500us)` then yield loop (`src/frame_wait.cpp`) | [07](07-display-window-frame-pacing.md) |
| futex fence wait | `src/gpu_fence_wait.cpp` uses `FUTEX_WAIT_PRIVATE`/`FUTEX_WAKE_PRIVATE` | |

## Library layout, RUNPATH and RPATH
- Program RPATH is `$ORIGIN` (the SDK stages the runtime beside the program only on Windows/macOS, so CMake copies `librexruntime.so` beside it).
- Patched libraries must have only `$ORIGIN` RUNPATH entries: `check_runpath` in `tools/build-patched-rexruntime.sh` rejects anything else. The libraries are built with `CMAKE_BUILD_WITH_INSTALL_RPATH=ON` because they are taken from the SDK build tree, which would otherwise carry an absolute build RUNPATH (`a576d87` fixed a missing `$ORIGIN`).
- AppImage layout: everything in `usr/bin` (program, `librexruntime.so`, `librexgpu-xenos.so`, toml); bundled `libstdc++.so.6` and `libgcc_s.so.1` in `usr/lib`.

## C++ runtime and glibc strategy
- ReXGlue needs `GLIBCXX_3.4.32` (GCC 13). `packaging/linux/AppRun` looks up the system `libstdc++.so.6` (`ldconfig -p`, then standard paths) and only if it lacks `GLIBCXX_3.4.32` prepends `$HERE/usr/lib` to `LD_LIBRARY_PATH`. Rationale in the script: a newer system copy must win because graphics drivers may need it.
- Release builds run on Ubuntu 22.04 (glibc 2.35) so the AppImage's glibc floor is 2.35; `package-linux.sh` warns when the host glibc is newer. The packager prints the highest `GLIBC_*` symbol each shipped binary needs.
- Toolchain on 22.04: clang-20/lld-20 from apt.llvm.org, GCC 13 runtime from `ubuntu-toolchain-r/test`, CMake 3.25+ from Kitware.

## Vulkan loader and driver behavior
- Device selection: patch `0026` prefers a discrete GPU among equally capable devices unless `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT` or `__NV_PRIME_RENDER_OFFLOAD` already chose; `vulkan_device` cvar still picks by index.
- Lavapipe (`VK_ICD_FILENAMES=.../lvp_icd.x86_64.json`) is used to run the presenter pacing test on Xvfb without a GPU.
- The Vulkan backend rendering far too dark was `0002`, see [06](06-graphics-vulkan.md).
- No usable Vulkan driver: ReXGlue only logs the failed presentation setup and `OnInitialize` returns false (exit status 1). `GeometryWarsRetroEvolved2App::SetupPresentation` runs `gw2::RunLauncherWithoutGraphics` first (`src/launcher_platform_linux.cpp`): an SDL window with a software surface, text from ImGui's built-in font in a private atlas, a game-files and graphics status, PLAY and QUIT. PLAY shows the failure text inside the same window. It does not use `SDL_ShowSimpleMessageBox`, which runs `zenity` first when it is installed and can stall before any window appears. The window title is the Recompiled name, the icon is the AppImage's own. Windows has an empty implementation.

## RenderDoc handling (`852b042`, patch `0003`)
- Problem: ReXGlue called `RenderDocAPI::CreateIfConnected` at startup with `DynamicLibrary::Load` (normal `dlopen`), which loaded `librenderdoc.so` whenever the loader could find it. Detecting RenderDoc turned RenderDoc on wherever it is installed in a standard library folder.
- Solution: `DynamicLibrary::LoadExisting` = `dlopen(RTLD_NOLOAD)` (POSIX) / `GetModuleHandleExW` (Windows) so only an already attached or preloaded RenderDoc is used.
- Test: `tests/renderdoc_detect_tests.cpp` + `tests/fake_renderdoc.cpp` (three cases, plus a control run against the stock runtime that must fail). `CONFIRMED`.
- Packaging guard: `package-linux.sh` rejects any AppDir file/dynamic section mentioning RenderDoc and Vulkan layer JSON files.
- Port: `GENERIC_REXGLUE`; copy `0003` and the guard.

## Packaging and desktop integration
See [14](14-packaging-release.md). Desktop file `packaging/linux/io.github.saulob.geometry-wars-retro-evolved-2.desktop` (`Categories=Game`, `StartupWMClass`), icons hicolor 16-512 + scalable SVG, `.DirIcon` and root PNG from the 256 icon.

## Environment variables seen in the code/scripts
`APPIMAGE` (set by the AppImage runtime), `XDG_DATA_HOME` (legacy v0.1.0 folder only), `PATH` (picker lookup), `LD_LIBRARY_PATH` (AppRun), `GW2_WORK` (packaging work dir, default `~/gw2-linux-build`), `REXGLUE_PLATFORM` (codegen override), `VK_ICD_FILENAMES`, `SDL_VIDEO_DRIVER=offscreen|x11` (tests).

## Steam Deck
Documented flow: add the AppImage as a non-Steam game; launcher accepts D-pad, left stick, A/B and Start (Start presses Play per `docs/linux.md`). `UNCERTAIN`: never run on a Deck at the time of writing.

## Known limits (`CONFIRMED` from docs)
Issue #1 reported slow motion while the counter showed 60 on a Steam Machine; v0.3.0 improves pacing but the fix is unconfirmed by the reporter. The same report's too-dark image was fixed by `0002` and confirmed.

## Verify
`tools/package-linux.sh` (its checks), `tools/build-patched-rexruntime.sh --loader-test|--pacing-test|--stress`, `ldd` for unresolved libraries, `readelf -d` for RPATH/RUNPATH.
