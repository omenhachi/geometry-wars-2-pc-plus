# Geometry Wars: Retro Evolved 2 Recompilation

Experimental static recompilation of the Xbox Live Arcade version of Geometry Wars: Retro Evolved 2, built with ReXGlue 0.10.0.

## Status

Phase 1, minimal native build. The title screen renders from the recompiled game and was confirmed in a manual test that completed without crashes. See [docs/first-boot.md](docs/first-boot.md).

## Clone

TRG Launcher is a git submodule: `git clone --recursive`, or `git submodule update --init third_party/trg-launcher` after cloning.

## Game files

Original game files are not included. Supply your own legally obtained copy of the Xbox 360 package. The launcher can install it, or extract it into the project:

```powershell
.\tools\extract_stfs.ps1 -Package "<path to the package file>" -OutDir game\extracted
```

## Build

```powershell
.\tools\codegen.ps1
.\tools\build.ps1 -Run
```

The toolchain is Visual Studio 2026 with the C++ Clang tools. See [docs/minimal-native-build.md](docs/minimal-native-build.md).

## Documentation

- [Package analysis](docs/package-analysis.md)
- [Minimal native build](docs/minimal-native-build.md)
- [First boot](docs/first-boot.md)
- [Offline profile and save](docs/offline-profile.md)
- [Offline Xbox Live services](docs/offline-live.md)
- [Launcher](docs/launcher.md)
