# ReXGlue runtime patch

Geometry Wars: Retro Evolved 2 ships ReXGlue **v0.10.0** (tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) with
the local patches in `patches/rexglue-sdk`. On Windows they are built into `rexruntime` and the Xenos GPU plugin
(`rexgpu-xenos`); on Linux only `librexruntime.so` is patched.

| | |
| --- | --- |
| Patch | [patches/rexglue-sdk/0001-physical-release-order.patch](../patches/rexglue-sdk/0001-physical-release-order.patch) |
| Function | `PhysicalHeap::Release` in `src/system/xmemory.cpp` |
| Pins | [patches/rexglue-sdk/baseline.env](../patches/rexglue-sdk/baseline.env): tag, commit and the stock runtime hashes |

## Why

The guest's physical memory (`0xA0000000`, `0xC0000000` and `0xE0000000`) is tracked twice: in the shared parent
heap of physical pages and in the page table of each of those views. `PhysicalHeap::Release` freed the parent pages
first and cleared the view's entries last, and the callbacks in between give up the global lock. A second thread could
allocate the just-freed pages and pin them in the view over the stale entries, and the first release then erased the
new allocation's entries. The new owner's own release later failed with
`BaseHeap::Release failed because address is not a region start`, and the view's page table no longer matched the
memory in use. The patch releases the view first and the parent pages last, so the pages cannot be handed out again
before the view has dropped them. The callbacks still run first, while the region is intact, and `out_region_size`
still reports the parent's size.

The error needs two threads to hit the window between the two releases, so how often it happens in the game depends on
timing. The stock runtime fails the allocator stress test below on every run.

## Building the patched runtime

Windows (Visual Studio with the LLVM/Clang component, and git), after `tools\codegen.ps1` has put the SDK in
`tools\rexglue`: `.\tools\build.ps1` and `.\tools\build.ps1 -Run` rebuild the patched runtime and GPU plugin by
themselves whenever the installed ones do not match the current baseline, build settings and patches, and print
`ReXGlue patches: current` when they do. Adding, removing, renaming or editing a file in `patches/rexglue-sdk` is
enough; nothing else needs to change. The staged DLLs next to the executable are compared with the installed ones by
hash after every build and before `-Run` launches, and the launch is refused on a mismatch.

The builder can also be run by hand (advanced):

```powershell
.\tools\build-patched-rexruntime.ps1                 # fetch v0.10.0 (network), patch, build, install
.\tools\build-patched-rexruntime.ps1 -Restore        # put the stock runtime and GPU plugin back
```

It applies every `*.patch` in name order, builds the RelWithDebInfo and Release `rexruntime` and `rexgpu-xenos` into
`out\rexglue-sdk-src` (use `-WorkDir` for a shorter path) and replaces them in `tools\rexglue\win-amd64`. The Debug
runtime and GPU plugin stay stock, and `build.ps1 -Config Debug` does not check them.

Linux (Ubuntu 22.04, see [linux.md](linux.md)): `tools/package-linux.sh` runs `tools/build-patched-rexruntime.sh`
itself the first time. Besides the packages in linux.md it needs git, network access and the SDK's build
dependencies: `libgtk-3-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
libxfixes-dev libxrender-dev libxss-dev libvulkan-dev libwayland-dev libwayland-bin wayland-protocols
libxkbcommon-dev libdecor-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libgl1-mesa-dev libudev-dev
pkg-config`.

Both scripts check the source is exactly the pinned commit, stop if the patch no longer applies, compile with
`-ffile-prefix-map` so no build folder ends up in the library, and refuse a result that equals the stock file. The
binaries are not bit-for-bit reproducible; what is verified is the source commit, the patch hash and the hash of each
installed file.

## What packaging verifies

On Windows the builder records the installed files in `rexruntime-patch.txt` next to the SDK: a fingerprint of the
SDK version, tag and commit, the build flags, targets and configurations, and every patch's name and SHA-256 in name
order, plus the SHA-256 of each installed runtime, import library and GPU plugin file. The stamp is removed before the
files are replaced and written only after they are installed and verified. `build.ps1` and
`tools/package-release.ps1` accept the files only if the fingerprint equals the current one, each file matches its
recorded hash, and no runtime file is the stock release file. `package-release.ps1 -SkipBuild` does not rebuild; it
fails on a stale runtime. The packaged runtime must also be identical to the installed one.

`tools/package-linux.sh` still records the commit, the hash of `0001-physical-release-order.patch` and the hash of
`librexruntime.so`.

## Allocator stress test

`tests/physical_heap_stress.cpp` (CMake target `gw2_physical_heap_stress`, not part of the default build) has 8 threads
allocate and release physical memory at once on the `0xA0000000` and `0xE0000000` views, and passes only if no release
fails. With the stock runtime it fails; with the patched one it passes.

```powershell
.\tools\build.ps1 -Config Release -BuildDir build-release
.\tools\build-patched-rexruntime.ps1 -StressTest -SkipBuild    # test the installed runtime
```

On Linux: `tools/build-patched-rexruntime.sh --stress <sdk dir> <build dir>`, with a configured project build in
`<build dir>`. To compare, run `-Restore` first and repeat the test with the stock runtime.
