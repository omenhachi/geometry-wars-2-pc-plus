# Linux and Steam Deck

> **Experimental.** The Linux build is a native x86_64 program, not a Windows build under Proton or Wine. Testing on
> real hardware is limited to one report so far (see [Known limits](#known-limits)). Reports are welcome.

The Linux build uses the same code as the Windows build: ReXGlue SDK 0.10.0's Linux runtime, the Xenos GPU
plugin's Vulkan backend, and the same TRG launcher. It ships as one AppImage, which is also the Steam Deck build.

## Requirements

- 64-bit x86 Linux (x86_64) with glibc 2.31 or newer, such as Ubuntu 20.04 or later (`ldd --version` shows it)
- A Vulkan driver (Mesa RADV on a Steam Deck and most AMD and Intel PCs)
- An X11 or Wayland desktop (the runtime loads `libX11`, `libX11-xcb`, `libxcb` and `libwayland-client`)
- `zenity` or `kdialog` for the package picker, and `xdg-open` for the About page's folder buttons. They are not
  bundled. Without both pickers the Play page says "No file picker found" and cannot install the game.
- Your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package (see the README for the exact version)

The AppImage carries the C++ runtime of its build machine (GCC 13 or newer). It is used only when the system's own copy
is older than GCC 13.

## Installing

1. Download **GeometryWarsRetroEvolved2Recompiled-v…-x86_64.AppImage** and put it in a folder you can write to.
2. Make it executable: right-click it > **Properties** > **Permissions** > **Is executable**, or
   `chmod +x GeometryWarsRetroEvolved2Recompiled-v*-x86_64.AppImage`.
3. Run it. The launcher opens.
4. On the **Play** page click **Install from package...** and pick your package. The game files are copied into a
   `game` folder next to the AppImage.
5. Press **PLAY**.

### Steam Deck

This is the intended flow. It has not been tried on a Steam Deck yet.

1. In **Desktop Mode**, do steps 1 to 5 above. Copy your package to the Deck first (USB stick, SD card or network).
2. In Steam: **Games** > **Add a Non-Steam Game to My Library**, and pick the AppImage.
3. Return to **Game Mode** and start it from your library.

The launcher takes mouse, keyboard and controller input: the D-pad, left stick, A and B move through it, and
**Start** presses Play. This has not been tried on a Deck yet.

## Where things are

| What | Where |
| --- | --- |
| Game files | `game` next to the AppImage |
| Logs | `logs` next to the AppImage (`logs` inside the user data folder in use if that folder is not writable) |
| Settings, profile, scores, achievements, shader cache | `user_data` next to the AppImage |

`user_data` sits beside the AppImage file, never inside its read-only mount, so it stays when the AppImage exits and
moves with the AppImage folder. A build run outside an AppImage keeps `game`, `logs` and `user_data` next to the
program instead. `--game_data_root` and `--user_data_root` override the folders, as on Windows.

XDG matters in two cases only. Version 0.1.0 kept everything in `$XDG_DATA_HOME/geometry_wars_retro_evolved_2`,
normally `~/.local/share/geometry_wars_retro_evolved_2`: the first start of a newer AppImage moves that folder into
`user_data` and removes it. And if `user_data` cannot be written (an AppImage on a read-only location), that XDG folder
is used for that run and a warning is logged. See [user-data.md](user-data.md) for the move, the collision rule and the
failure rules.

## How it differs from Windows

- **Launcher window placement.** Windows places a saved position, size and maximized state before the window is first
  shown. Linux places the window after SDL has shown it, so a moved, resized or maximized launcher can appear at its
  default size for a moment. Under Wayland the compositor decides the position, so only the size and the maximized
  state are restored.
- **Frame limiter.** Rates below Unlimited are paced with `std::chrono::steady_clock`, a sleep until shortly before
  the deadline, and a brief yield loop; Windows uses a high-resolution waitable timer. The rates, the game's time step
  and the high-frame-rate present change are the same code on both systems.
- **VSync.** The setting controls Vulkan's present mode (`vulkan_allow_present_mode_immediate`): **Off** allows
  immediate presentation (tearing), **On** uses mailbox or FIFO. Windows controls D3D12 tearing instead.
- **Window icon.** The program sets its window icon from the AppImage's own icon file, so it does not depend on the
  AppImage being integrated into the desktop. A build run outside an AppImage keeps the desktop's default icon.
- **Application ID.** The program tells the desktop it is `io.github.saulob.geometry-wars-retro-evolved-2` (the Wayland
  app_id and the X11 window class). The `.desktop` file in `packaging/linux` has the same name and sets it as
  `StartupWMClass`, and `tools/package-linux.sh` stops if they differ.
- **Monitor numbering** follows SDL's display order.
- **Without Vulkan.** The launcher is drawn by the graphics system, so when Vulkan cannot start (no driver, or no GPU)
  the program opens a small launcher window drawn by SDL on the CPU instead, with no `zenity`, GTK or Vulkan. It shows
  the game files and the game folder, the graphics status, and **PLAY** and **QUIT**. **PLAY** explains that a
  Vulkan-compatible driver is required and points at the logs folder, and **QUIT** or closing the window ends the
  program, which then exits with a nonzero status. Nothing from the game runs in this window. With a working driver
  the normal launcher opens and this window is never created.

## Known limits

- On a Steam Machine the game was reported to render around 30 FPS and to run slowly with it
  ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)). v0.3.1 keeps the game's speed
  at the 60 FPS setting when frames miss vblanks (see [launcher.md](launcher.md)), so a slow frame rate no longer slows
  the game itself. Why that system renders around 30 FPS is still under investigation, and the timing fix has not been
  confirmed on the reporter's hardware yet. The same report's very dark image was fixed in v0.2.3 (see
  [rexglue-gpu-patch.md](rexglue-gpu-patch.md)), and the reporter confirmed it.
- Otherwise untested on real hardware: audio, controllers, performance and the frame rates above 60 are unconfirmed on
  Linux. They were validated on Windows only.
- The portable `user_data` folder and the move from the XDG folder are covered by unit tests with temporary folders
  only. They have not been tried with an AppImage on a real Linux PC or Steam Deck.
- Holding **Shift** to bring the launcher back works only if the window gets keyboard focus while Shift is still
  held, because SDL cannot read the keyboard before the window has focus. Keep Shift held until the launcher appears.
- On a Wayland desktop with display scaling above 100%, window sizes can come out larger than chosen. ReXGlue
  sizes its window this way on Linux.

## Building

After `tools/codegen.sh` has put the SDK in `tools/rexglue/linux-amd64` and generated `generated/default`:

```sh
tools/build.sh                   # out/build/linux-amd64-release
tools/build.sh relwithdebinfo    # or debug
```

It first makes sure the SDK holds the patched ReXGlue runtime and GPU plugin this branch needs for that
configuration, building them from source when it does not (git and network access the first time, then a few minutes;
see [rexglue-runtime-patch.md](rexglue-runtime-patch.md)), then configures and builds the matching preset. A plain
`cmake --preset linux-amd64-release` and `cmake --build --preset linux-amd64-release` checks the same thing and stops
before compiling the program, with the command to run, when the SDK does not match. The unit tests build with
`cmake --build --preset linux-amd64-release --target gw2_tests` into `out/build/<preset>/tests`; they need no game
files and no graphics.

## Building the AppImage

Run on Linux or WSL, from a clone with the TRG launcher submodule, after `tools/codegen.ps1` has generated
`generated/default` and with your own `game/extracted/default.xex`:

```sh
tools/package-linux.sh            # incremental Release build, then package
tools/package-linux.sh --clean    # clean Release build first
```

It needs `clang-20`, `clang++-20`, `lld-20` (Release and RelWithDebInfo link with ThinLTO), CMake 3.25+, Ninja,
rsync, wget or curl, unzip or python3, binutils and the GCC 13 or newer C++ runtime and headers.

The AppImage needs the glibc of the machine that built it, so releases are built on Ubuntu 20.04 (glibc 2.31) and the
script refuses other hosts (`GW2_ALLOW_UNSUPPORTED_HOST=1` overrides that for experiments; the check below still
applies). `tools/setup-linux-build-env.sh` prepares a clean Ubuntu 20.04 machine for it, as root: clang 20 from
[apt.llvm.org](https://apt.llvm.org), the GCC 13 runtime from the `ubuntu-toolchain-r/test` PPA, CMake 3.31.8 and the
SDK's build dependencies. libdecor 0.2.2 and PipeWire 0.3.48 are not packaged for 20.04, so it builds them from their
pinned commits into `/usr/local`; SDL loads both at run time and needs only their headers, so the AppImage does not
carry them. To make the environment in WSL:

```sh
wsl --import GeometryFocalBuild <folder> ubuntu-base-20.04.5-base-amd64.tar.gz
wsl -d GeometryFocalBuild -u root -- bash tools/setup-linux-build-env.sh
wsl -d GeometryFocalBuild -u root -- bash tools/package-linux.sh
```

The base image is `ubuntu-base-20.04.5-base-amd64.tar.gz` from cdimage.ubuntu.com (SHA-256
`60e216b60947653dc8989be3821380268315b99b2959c29882781541bfe5a426`); a Docker `ubuntu:20.04` container works the same
way. The SDK's stock `rexglue` code generator needs glibc 2.35, so `tools/package-linux.sh` builds it from the pinned
SDK source on this baseline when the stock one cannot run.

`tools/check-glibc-baseline.sh` is the compatibility gate. It finds every ELF file by its magic bytes, wherever it is
and whatever it is called, and fails if one needs a glibc symbol version above 2.31 (compared numerically) or if a
glibc runtime library (`libc.so.6`, `ld-linux-x86-64.so.2`, `libm.so.6`, `libpthread.so.0`, ...) is among the files.
The packager runs it on the AppDir and again on the extracted final AppImage and the AppImage's own runtime. glibc is
never bundled. The bundled `libstdc++.so.6` and `libgcc_s.so.1` (GCC 13 for 20.04) are used by `AppRun` only when the
system's C++ runtime is older than GCC 13.

The script copies the sources to `~/gw2-linux-build` (set `GW2_WORK` to change it), downloads the pinned ReXGlue
SDK and appimagetool with their SHA-256 checks, prepares the patched ReXGlue runtime and GPU plugin
the same way `tools/build.sh` does, builds the AppImage runtime (below), builds the `linux-amd64-release` preset, checks the libraries again in the build
folder and the AppDir, checks that the AppDir holds no game files, packages, saves, logs or local paths, and writes
`GeometryWarsRetroEvolved2Recompiled-v<version>-x86_64.AppImage` with its `.sha256` in `dist/` (`--out-dir DIR` writes
them to DIR instead). The version is the
`project()` VERSION in `CMakeLists.txt`. The script publishes nothing, and the game's `default.xex` only enters the
temporary build folder as input for code generation, never the AppImage.

### The AppImage runtime

The runtime is the small program at the front of every AppImage that mounts it with FUSE and starts AppRun. The
released runtime stops with "Cannot mount AppImage, please check your FUSE setup" where the mount is not possible, so the
project builds its own from the upstream [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime) source:

- `patches/appimage-runtime/baseline.env` pins the upstream commit (`dd6cebe`, tag `20251108`), the Alpine 3.21.0 root
  file system the build runs in, and the SHA-256 of the reviewed result.
- `patches/appimage-runtime/0001-extract-and-run-when-fuse-fails.patch` makes the runtime unpack the AppImage into a
  private folder under `/tmp` when the FUSE mount fails, run AppRun from there with the same arguments, return its exit
  status and remove the folder. Where FUSE works the AppImage is mounted as before. `APPIMAGE_EXTRACT_AND_RUN=1` still
  forces the unpacking.
- `tools/build-appimage-runtime.sh` fetches that commit, applies the patch and runs upstream's chroot build into a static
  musl binary. It needs root or sudo, git, tar, unshare and network access on the first build, and rebuilds only when the
  pins or the patch change. `--check FILE` verifies a runtime or a finished AppImage.

A restart from the launcher starts the original AppImage again (`APPIMAGE`), never the unpacked copy. The runtime's
static components and their licenses are listed in `THIRD-PARTY-LICENSES-LINUX.txt` inside the AppImage.
