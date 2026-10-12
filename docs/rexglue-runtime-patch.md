# ReXGlue runtime patch

Geometry Wars: Retro Evolved 2 ships ReXGlue **v0.10.0** (tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) with
one local patch to the runtime library (`rexruntime.dll` / `librexruntime.so`). Everything else in the SDK, including
the Xenos GPU plugin, is the stock release.

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
`tools\rexglue`:

```powershell
.\tools\build-patched-rexruntime.ps1                 # fetch v0.10.0 (network), patch, build, install
.\tools\build-patched-rexruntime.ps1 -Restore        # put the stock runtime back
```

It builds the RelWithDebInfo and Release runtime into `out\rexglue-sdk-src` (use `-WorkDir` for a shorter path) and
replaces them in `tools\rexglue\win-amd64`. The Debug runtime stays stock.

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

The scripts record the installed runtime in `rexruntime-patch.txt` next to the SDK: the source commit, the SHA-256
of the patch file and the SHA-256 of each installed runtime file. `tools/package-release.ps1` and
`tools/package-linux.sh` accept the runtime only if the commit is the pinned one, the patch hash equals the tracked
patch, the files match the recorded hashes, and none of them is the stock release file. The packaged runtime must also
be identical to the installed one. Editing the patch therefore requires rebuilding the runtime before packaging.
`rexgpu-xenos` is still compared with the stock release file.

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
