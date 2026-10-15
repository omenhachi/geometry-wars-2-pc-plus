# 14 Packaging and release

Scripts: `tools/package-release.ps1` (Windows ZIP), `tools/package-linux.sh` (AppImage), `tools/make-app-icon.py`;
inputs `packaging/`. Both packagers are local only: they publish, tag and upload nothing, write to `dist/` (git-ignored),
and refuse to continue if anything forbidden is staged. Origin: `9e6f05b` (first release packaging), `b362de8` (icons),
`aa64c6a`/`a86c63f` (patched runtime verification).

## Windows ZIP
Name `GeometryWarsRetroEvolved2-v<VERSION>-windows-x64.zip` plus `.sha256` (`<hash>  <name>.zip`, lowercase, LF).
`VERSION` is parsed from the `project(... VERSION x.y.z)` line of `CMakeLists.txt`.

Steps (`package-release.ps1`): build Release into `build-release/` (`-Clean`, `-SkipBuild` available) -> verify the
patched runtime (`Test-PatchedRuntime`, `Test-StagedRuntime`) -> stage files -> check DLL hashes equal the SDK's -> copy
`LICENSE`, `licenses/*`, substitute `@VERSION@` in `packaging/README.txt` (CRLF, UTF-8 no BOM) -> run the folder check ->
write the ZIP with entries sorted and compared to the staged list, rejecting unsafe entry names -> write the hash file.

Contents (exactly): `geometry_wars_retro_evolved_2.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`,
`geometry_wars_retro_evolved_2.toml`, `README.txt`, `LICENSE`, `licenses/ReXGlue-LICENSE.txt`,
`licenses/THIRD-PARTY-LICENSES.txt`. No game files, no `user_data`, no logs, no symbols, no PDBs.

`-VerifyOnly <folder>` runs just the folder check; `tools/test.ps1` uses it on scratch folders.

### The release-folder check (`Test-ReleaseFolder`) - the scanning idea applies to every project; the forbidden-name list below is this game's and must be adapted
- Forbidden names: `default.xex`, `Data.bin`, `game`, `game-install-temp`, `game-install-backup`, `user_data`, `user_data.migrating`, `content`, `cache`, `logs`, `settings.toml`, `settings.toml.bak`, `ArcadeInfo.xml`, `nxeart`, the title id.
- Forbidden types: `*.xex *.log *.dmp *.pdb *.xsh *.xpso *.bak *.tmp *.sav`.
- Magic bytes `LIVE`, `PIRS`, `CON ` (Xbox 360 packages) rejected anywhere.
- Local repository path (as text and as UTF-16) rejected inside any file.
- Anything outside the expected file list rejected, and every expected file must exist.

## Linux AppImage
Name `GeometryWarsRetroEvolved2Recompiled-v<VERSION>-x86_64.AppImage` + `.sha256` (in `dist/`, or `--out-dir DIR`). Run on Linux/WSL from a clone with the
submodule and generated sources (`tools/codegen.ps1` outputs `generated/default`) and your own `default.xex`.

Pipeline (`tools/package-linux.sh`, optional `--clean`):
1. Tools check: `clang-20 clang++-20 ld.lld-20 cmake ninja rsync sha256sum strip readelf objdump ldd`, `wget|curl`, `unzip|python3`; warns when the host glibc is newer than 2.35.
2. Inputs: `generated/default/sources.cmake`, the TRG submodule, `game/extracted/default.xex` hash.
3. Sources are rsynced into `$GW2_WORK` (default `~/gw2-linux-build`, on the Linux filesystem for speed and case sensitivity); only needed folders are copied; the XEX goes only into the temp work folder as codegen input, never the AppDir.
4. Pinned downloads with SHA-256 checks (`download()` re-downloads on mismatch): ReXGlue SDK zip, appimagetool 1.9.1, AppImage type2 runtime 20251108.
5. Patched runtime: `--check`, build if needed, `--check` again.
6. Build the `linux-amd64-release` preset; verify outputs; `--check --staged` the build folder.
7. AppDir assembly: program (stripped with `strip --strip-unneeded`, because debug sections carry build folders), `librexruntime.so`, `librexgpu-xenos.so`, toml in `usr/bin`; `libstdc++.so.6` and `libgcc_s.so.1` (must contain `GLIBCXX_3.4.32`) in `usr/lib`; licenses (project, ReXGlue, third-party, Linux third-party, and the C++ runtime's `copyright`) in `usr/share/doc/...`; `AppRun`; `<app id>.desktop`; hicolor icons 16/24/32/48/64/128/256/512 + scalable SVG; root PNG and `.DirIcon` from the 256 icon. Permissions normalized (755 for AppRun/program, 644 for the rest).
8. AppDir check (below), then `appimagetool --appimage-extract-and-run --no-appstream --runtime-file <pinned runtime>`; `sha256sum` file.

### AppDir release check
- Forbidden names and types as on Windows plus: `*.json`, anything containing `renderdoc` or `vklayer`, symbolic links, Windows binaries (`MZ`), Xbox package magic, local paths (`$ROOT`, `$WORK`, `$HOME` and developer-folder regexes) inside any file.
- Exact file list (`EXPECTED`) compared with `find`: an unexpected file fails the build.
- Desktop file `StartupWMClass` must equal the application id; the id string must be inside the binary (so `SetApplicationId` and the desktop file cannot drift apart).
- No dynamic section mentions RenderDoc; program RPATH is `$ORIGIN`; `ldd` shows no unresolved libraries; the highest `GLIBC_*` of each shipped binary is printed.

## Icons and metadata
`packaging/icons/app-icon.svg` is the master; `tools/make-app-icon.py` renders PNGs and ICO with Edge/Chrome (committed, so builds need no converter) and refuses to run if the SVG does not use the launcher accent `kAccent` (`#943CFF`) - tying UI color and icon together. Windows: `.rc` ICON; Linux: hicolor theme + desktop file. `.recomp.json` is the recomp.fyi catalogue entry (status, targets, tags); `README` badges and `.github/release-notes-vX.Y.Z.md` (one per version, with checksum table filled after packaging, e.g. commit `b37702e` for v0.2.2). Licenses: `packaging/licenses/` (project GPL-2.0, ReXGlue license, third-party lists for Windows and Linux).

## Not intentionally shipped
Game files, packages, saves, logs, shader caches, symbols, Vulkan layers, RenderDoc, `zenity`/`kdialog`/`xdg-open`, any Windows binary in the AppImage.

## Dependencies to document for users
Windows: VC++ 2015-2022 x64 redistributable, DirectX 12 GPU, Windows 10/11. Linux: x86_64, glibc 2.35+, Vulkan driver, X11 or Wayland, `zenity` or `kdialog` for the picker, `xdg-open` for the About buttons.

## Validation commands
```
tools/test.ps1                                        # unit tests + release-folder check on scratch dirs
tools/package-release.ps1 -VerifyOnly <staged dir>
tools/package-linux.sh                                # runs its own checks
tools/build-patched-rexruntime.sh --check <sdk> --config Release --staged <dir>
sha256sum -c dist/<name>.sha256   (Windows: compare Get-FileHash with the .sha256 file)
readelf -d usr/bin/<exe> | grep -E 'R(UN)?PATH|NEEDED'; ldd usr/bin/<exe>
```

## Porting notes
Copy both packagers and rename identifiers; keep the forbidden-name lists; set the new `kApplicationId`; keep the
Release flow on Ubuntu 22.04 for glibc 2.35. Unverified on real systems at writing: the AppImage on a real Steam Deck.
