# 02 Build system

Authoritative files: `CMakeLists.txt`, `CMakePresets.json`, `tools/build.ps1`, `tools/build.sh`, `tools/codegen.ps1`,
`tools/codegen.sh`, `tools/build-patched-rexruntime.{sh,ps1}`, `tools/rexruntime-patch.ps1`.

## Pipeline

```
game package (STFS for Geometry Wars) -> tools/extract_stfs.{ps1,sh} -> game/extracted/default.xex (SHA-256 checked)
              -> tools/codegen.{ps1,sh} (pinned SDK zip, `rexglue codegen <manifest>`) -> generated/default
              -> tools/build.{ps1,sh} (prepare patched runtime, configure, build)
```

`REUSABLE_INFRASTRUCTURE` for the shape; hashes and the manifest name are `GAME_ADAPTATION`.

## SDK discovery and pinning
- `GW2_SDK_DIR` = `tools/rexglue/win-amd64` or `tools/rexglue/linux-amd64`, appended to `CMAKE_PREFIX_PATH`; `generated/rexglue.cmake` (codegen output) supplies `rexglue_setup_target` and the `rex::runtime`, `rex::gpu-xenos`, `rex::renderdoc` targets.
- `codegen.ps1`/`codegen.sh` download the official zip only if `bin/rexglue` is missing, verify the zip SHA-256, check `rexglue --version`, check the XEX hash, then run codegen and log to `generated/codegen.log` (judge by exit code; Windows PowerShell 5.1 turns stderr into errors).
- `tools/package-linux.sh` pins its own copy of the zip hash, appimagetool and the AppImage runtime (see [14](14-packaging-release.md)).
- The SDK folder and `generated/` are git-ignored; `patches/` and `baseline.env` are tracked.

## Patched runtime is part of the normal build (`GENERIC_REXGLUE`)
- `tools/build.ps1` calls `Update-PatchedRuntime` (from `tools/rexruntime-patch.ps1`); `tools/build.sh` runs `build-patched-rexruntime.sh --check` and builds if it fails.
- CMake does the same independently (so a plain `cmake --build` is safe): custom target `gw2_rexglue_check` runs before the executable; `gw2_rexglue_staging` (ALL) copies `rex::runtime` and `rex::gpu-xenos` next to the program and runs the check with `-Staged`/`--staged`.
- `CMAKE_BUILD_TYPE` must be Debug, RelWithDebInfo or Release on single-config generators (`FATAL_ERROR` otherwise), because runtime files are prepared and checked per configuration. Suffixes: Release none, RelWithDebInfo `rd`, Debug `d` (`rexruntimerd.dll`, `librexruntimed.so`).
- Details of the check, stamp and refusal rules: [01](01-rexglue-core.md#the-patch-model-reusable_infrastructure-copy-first).
- Release libraries are compiled with `-ffile-prefix-map`; the scripts reject a Release file that embeds the SDK, source or repo path, or that equals the stock hash. The result is not bit-for-bit reproducible; inputs and installed-file hashes are what is verified.
- Linux build of the SDK itself needs a long list of dev packages (GTK3, X11/XCB, Wayland, xkbcommon, libdecor, ALSA, PulseAudio, PipeWire, udev, Vulkan, Mesa GL); it is listed in `docs/rexglue-runtime-patch.md` and must be present on the packaging machine ([14](14-packaging-release.md)).

## Presets and compilers
- `CMakePresets.json` v6: hidden `windows-base` (clang/clang++, Ninja, `binaryDir out/build/<preset>`) and `linux-base` (`clang-20`/`clang++-20`), each guarded by `hostSystemName`. Presets `win-amd64-{debug,release,relwithdebinfo}` and `linux-amd64-*`.
- Windows uses Visual Studio's bundled clang (found with `vswhere`, requires `VC.Tools.x86.x64` and `VC.Llvm.Clang`), `lld-link`, VS's CMake and Ninja. `tools/build.ps1` loads `VsDevCmd.bat`, strips MSYS2 directories from `PATH`, and restores the environment in `finally`.
- Default configuration for `build.ps1` is RelWithDebInfo; `package-release.ps1` builds Release into `build-release/` so the development `build/` is untouched.
- `CMAKE_CXX_STANDARD 23`. TRG launcher is forced to C++17 (its `u8string()` use breaks in C++20+) and gets `_CRT_SECURE_NO_WARNINGS` explicitly because clang-cl does not receive TRG's cl.exe-only define.
- `-fmacro-prefix-map=<src>=.` in Release keeps `__FILE__` from leaking the build machine's folders.

## ThinLTO (`273dc0c`, `PLATFORM_*`)
`gw2_enable_thinlto()` applies to the executable and the generated `_recomp` target in Release and RelWithDebInfo only.
Windows: `-flto=thin`, lld-link `/opt:lldlto=3` (Release; lld-link defaults to O2) and an LTO cache in the build tree. Linux: `-flto=thin -fuse-ld=lld` with `--thinlto-cache-dir`. Debug is skipped because `-O0` marks functions optnone, so LTO would do nothing. Consequence: Linux packaging needs `lld-20` (`ld.lld-20` is checked by `package-linux.sh`). `CONFIRMED` by build configuration; the benefit was not measured in the repository (`UNCERTAIN`).

## Targets and resources
- Executable `geometry_wars_retro_evolved_2`; `WIN32` subsystem on Windows, plain on Linux.
- Windows: `enable_language(RC)`, resource `packaging/windows/geometry_wars_retro_evolved_2.rc` with `OBJECT_DEPENDS` on the `.ico` so an icon change relinks. Resource ID 1 is the first icon: Explorer and SDL's window class use it. Link libs: `bcrypt shcore synchronization` (`synchronization` is for `WaitOnAddress`).
- Linux: post-build copies `rex::runtime` beside the program because the SDK stages it only on Windows/macOS; the executable RPATH is `$ORIGIN`.
- The shipped cvar file `config/geometry_wars_retro_evolved_2.toml` is copied next to the executable after each build.
- `GW2_PORT_VERSION` (from `project(VERSION)`) and `GW2_TRG_LAUNCHER_VERSION` are compile definitions; packagers parse the same `project(... VERSION x.y.z)` line.
- TRG launcher: git submodule `third_party/trg-launcher` (pin `1a6ef30`, version 1.2.0). It must use the runtime's own ImGui: an `INTERFACE` library `gw2_imgui` links `rex::runtime`, and TRG options set `TRG_LAUNCHER_IMGUI_TARGET`, `TRG_LAUNCHER_STANDALONE=OFF`, `TRG_LAUNCHER_FETCH_DEPS=OFF` as forced cache values.

## Tests in the build
- `gw2_tests` (EXCLUDE_FROM_ALL): `gw2_add_test()` builds `tests/<name>.cpp` with only what it needs from `src/`; no game files, no runtime. `tools/test.ps1` builds the same set on Windows with clang++ and also runs `package-release.ps1 -VerifyOnly` on scratch folders.
- Runtime-linked tests (`gw2_physical_heap_stress`, `gw2_restart_state_tests`, `gw2_title_thread_shutdown_tests`, `gw2_shutdown_order_tests`, Linux-only `gw2_renderdoc_detect_tests`, `gw2_presenter_pacing_tests`) copy `rex::runtime` beside themselves; on Linux they use `BUILD_RPATH $ORIGIN`. A fake `librenderdoc.so` is built into `renderdoc/` so the runtime's `$ORIGIN` RUNPATH cannot find it accidentally. See [15](15-debugging-logging.md).

## Common failures
| Failure | Meaning / fix |
| --- | --- |
| `... is the stock ReXGlue release file` / `does not match the patched build` | Wrong or stale SDK. Run `tools/build.ps1` / `tools/build.sh` (or the builder script) for that configuration |
| `<patch> no longer applies to ReXGlue v0.10.0` | Patch edited against a different tree; the work tree is reset every run, re-diff against the pinned tag |
| `patches.txt does not list ...` / `numbered more than once` / `numeric order` | Manifest rules |
| TRG Launcher not found | `git submodule update --init third_party/trg-launcher` |
| `generated/default` missing | Run codegen first |
| `GLIBCXX_3.4.32` missing at runtime on Linux | System libstdc++ older than GCC 13; AppRun prepends the bundled copy only in that case ([05](05-platform-linux.md)) |
| Codegen warnings after `tools/find-missing-functions.py` | Re-run `--verify`; review LOW/EH candidates by hand, do not bulk-apply |

## Porting notes
- Copy: the CMake checks, `gw2_add_test`, `gw2_enable_thinlto`, presets, scripts renamed for the new project.
- Replace: project name, XEX hash, SDK zip hashes (per OS), manifest filename, `GW2_*` identifiers, title ID.
- Keep the SDK version identical to the donor project's, or re-validate every patch with `git apply --check` first.
