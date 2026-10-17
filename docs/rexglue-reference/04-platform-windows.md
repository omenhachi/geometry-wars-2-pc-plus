# 04 Windows platform

All items `PLATFORM_WINDOWS` unless noted. Implementation: `src/launcher_platform_win32.cpp`, `src/frame_wait.cpp`
(Windows branch), `packaging/windows/geometry_wars_retro_evolved_2.rc`, `CMakeLists.txt`, `tools/package-release.ps1`,
SDK patches `0012`-`0016`, `0027`.

## Win32 pieces

| Topic | Implementation | Port notes |
| --- | --- | --- |
| Executable metadata / icon | `.rc` containing `1 ICON "../icons/app-icon.ico"`; CMake `enable_language(RC)` + `OBJECT_DEPENDS` on the `.ico`. Resource 1 is what Explorer and SDL's window class pick up (`b362de8`) | Copy; `tools/make-app-icon.py` regenerates PNG/ICO from the SVG with Edge/Chrome |
| No console | Executable is linked `WIN32` | Logs go to the `logs` folder, see [15](15-debugging-logging.md) |
| Hybrid GPU hint | `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` exported `extern "C"` from `launcher_platform_win32.cpp` | Copy. The SDK patch `0027` additionally enumerates D3D12 adapters by high-performance preference unless Windows' graphics settings set power saving for the app; `d3d12_adapter` still wins |
| DPI / monitors | `EnumDisplayMonitors` + `GetDpiForMonitor(MDT_EFFECTIVE_DPI)`; scale = dpi/96; work area from `rcWork`; primary monitor sorted first to match ReXGlue's `monitor` numbering | Needs `shcore` |
| Window placement before first show | `SetWindowsHookExW(WH_CALLWNDPROC)` on the creating thread; on the first top-level `WM_SHOWWINDOW` it sets position/size and maximizes, then unhooks (`InstallPreShowPlacement`, `TakePreShowResult`) | `UnhookWindowsHookEx` is also called from `TakePreShowResult` if the window never showed |
| Placement query/apply | `GetWindowRect`, `IsIconic`, `IsZoomed`, `GetWindowPlacement` (restore-to-maximized flag when minimized); `ApplyWindowPlacement` = `SW_RESTORE`, `SetWindowPos`, `SW_MAXIMIZE` | |
| Relaunch | `CreateProcessW` with `GetCommandLineW()` + extra args (`--gw2_skip_launcher=true`); handles closed immediately | |
| Fonts | Segoe UI regular/semibold/bold from the Windows directory | |
| Controller for ImGui | Dynamic `xinput1_4.dll` / `xinput1_3.dll` / `xinput9_1_0.dll`, polling an empty slot once per second | Avoids a hard XInput import |
| Shift at startup | `trg::ShiftHeld()` | |
| High-resolution waits | App: `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` in `src/frame_wait.cpp::WaitUntil` (coarse wait until 500 us before deadline, then yield-spin). SDK: patch `0016` does the same for the guest `Sleep` from 1 ms up | Waits with a timeout (`WaitForSingleObject`) still follow the system timer |
| Futex equivalent | `WaitOnAddress`/`WakeByAddressAll` in `src/gpu_fence_wait.cpp` and SDK patch `0011` (link `synchronization`) | |
| User data / paths | Next to the executable: `game`, `logs`, `user_data` (`PortFolder()` = `rex::filesystem::GetExecutableFolder()`). Legacy v0.1.0 data was `Documents\geometry_wars_retro_evolved_2`, migrated by `SelectUserData` | See [10](10-user-data-settings-migration.md); do not install under "Program Files" (not writable, falls back to Documents for that run) |
| Open folder/file | TRG: Explorer | |
| Path text | `gw2::Utf8`/`PathFromUtf8` keep TRG and settings UTF-8 on both OSes | |

## D3D12 backend notes
The Windows SDK is built without the Vulkan backend (`REXGLUE_USE_VULKAN` off). Windows therefore does not need `0002`
(the DXBC translator already reads `exp_adjust` from fetch word 3) or `0003` (`LoadLibraryW` only searches the
program folder, system folders and `PATH`). D3D12 equivalents of the performance patches are `0012`-`0015`,
`0023`, `0031` ([12](12-performance.md)).

## Build environment
- Visual Studio 2026 (v18) with C++ x64 tools and the C++ Clang tools; Clang 22.1.3 in the recorded environment (`docs/minimal-native-build.md`). `tools/build.ps1` finds everything with `vswhere`.
- Release ZIP content: `geometry_wars_retro_evolved_2.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`, `geometry_wars_retro_evolved_2.toml`, `README.txt`, `LICENSE`, `licenses/{ReXGlue-LICENSE.txt,THIRD-PARTY-LICENSES.txt}` ([14](14-packaging-release.md)).
- Runtime requirement stated to users: VC++ 2015-2022 x64 redistributable (`packaging/README.txt`). The build is not code-signed, so SmartScreen warns.

## Startup / shutdown behavior (Windows specific parts)
- Vblank thread priority: patch `0008` runs the guest vblank thread one level above normal on Windows only; on Linux raising priority needs `CAP_SYS_NICE`, so the default is kept.
- Hard exit after relaunch uses `std::_Exit(0)` (no subsystem teardown) so the new process owns the game.

## Verification
`tools/test.ps1`; `tools/package-release.ps1 -VerifyOnly <folder>`; manual: icon in Explorer/taskbar, first-show placement without flash, F11 toggles, high refresh behavior.
Confidence: icon visuals and portable-folder behavior on Windows were marked "runtime test pending" in project notes when written (`UNCERTAIN`).
