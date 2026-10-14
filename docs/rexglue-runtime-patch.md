# ReXGlue runtime patch

Geometry Wars: Retro Evolved 2 ships ReXGlue **v0.10.0** (tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) with
local patches, built by the normal build. This page covers the runtime patch and how every build gets the patched
files; the GPU plugin patch is in [rexglue-gpu-patch.md](rexglue-gpu-patch.md).

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

## Which patches each platform uses

[patches/rexglue-sdk/patches.txt](../patches/rexglue-sdk/patches.txt) lists every patch with the platforms that apply it
and the ReXGlue targets it changes. It is the only list; the Windows and Linux scripts both read it.

| Patch | Target | Windows | Linux | Why |
| --- | --- | --- | --- | --- |
| `0001-physical-release-order` | `rexruntime` | yes | yes | Shared runtime code. |
| `0002-spirv-texture-exp-adjust` | `rexgpu-xenos` | no | yes | Fixes the SPIR-V (Vulkan) translator. The Windows plugin is built without Vulkan, and its DXBC translator already reads the right field. |

A target that no patch for the platform changes must be the stock release file. Patch numbers must be unique, every
`.patch` file must be listed, and the list must be in numeric order; the scripts stop otherwise. A patch for one
platform can be named `NNNN-name.<platform>.patch` for clarity, but only `patches.txt` decides where it applies.

## How the normal build gets the patched files

Windows: `.\tools\build.ps1` (any `-Config`) first checks `tools\rexglue\win-amd64` for that configuration and, when
it does not match the branch, runs `tools\build-patched-rexruntime.ps1 -Config <config>`. Linux: `tools/build.sh`
does the same with `tools/build-patched-rexruntime.sh <sdk> --config <config>`. The scripts fetch v0.10.0 into
`out/rexglue-sdk-src` the first time (git and network access), check the pinned commit, reset every tracked file of
that tree (so another branch's patches cannot stay), apply this platform's patches, build the patched targets for the
configuration, install them, and put the stock files back for the other targets. Each configuration is prepared the
first time it is built; later builds only check.

`rexruntime-patch.txt` beside the SDK records the inputs: the SDK version, tag and commit, the platform, each applied
patch with its SHA-256 and targets, the patched targets, the compiler (and MSVC toolset on Windows) and the flags,
followed by the SHA-256 of every installed file. Any other input makes the installed files stale, so switching to a
branch with other patches rebuilds them on the next build.

The CMake build itself checks the same thing on Windows and Linux: before the program is compiled it stops when the
SDK does not match (also for a plain `cmake --build`), and after linking it copies the runtime and GPU plugin next to
the program and stops unless those copies are the SDK's. `tools/package-release.ps1` and `tools/package-linux.sh` run
the same checks again on what they package; they are not needed to get the patched files.

The Linux build also needs, besides the packages in [linux.md](linux.md), the SDK's build dependencies:
`libgtk-3-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
libxrender-dev libxss-dev libvulkan-dev libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev
libdecor-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libgl1-mesa-dev libudev-dev pkg-config`.

The Release libraries are compiled with `-ffile-prefix-map`, and the scripts refuse a Release file that embeds the
build folder or equals the stock one. The binaries are not bit-for-bit reproducible; what is verified is the inputs
and the hash of each installed file. `.\tools\build-patched-rexruntime.ps1 -Restore` puts every stock Windows file
back; the next build prepares the branch's files again.

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
