# 10 User data, settings and migration

Long-form: `docs/user-data.md`. Implementation: `src/user_data.{h,cpp}`, `src/launcher_platform.cpp` (`PortFolder`),
`src/geometry_wars_retro_evolved_2_app.h` (`OnConfigurePaths`, `ChooseUserData`), `src/launcher_settings.cpp`,
`src/launcher_cvars.cpp`. Origin: `650b8c4` (portable user data with automatic migration). Tests:
`tests/user_data_tests.cpp`. Class: `REUSABLE_INFRASTRUCTURE`, copy before the first public release.

## Why do this before release
The port first shipped (v0.1.0) with ReXGlue's default root: `Documents\<app>` on Windows,
`$XDG_DATA_HOME/<app>` on Linux. v0.2.0 moved everything to a portable `user_data` next to the program, which required
a migration that must keep working forever for people on v0.1.0. A new project that starts portable has no legacy
branch to carry. Also decide: logs, cache and game folder locations (same reasoning).

## Final layout
```
<port folder>/            exe folder on Windows; AppImage's folder on Linux AppImage; exe folder otherwise
  game/ logs/ user_data/
    <titleid>/profile/    game profile files
    <titleid>/live/       stats.toml (local scores)
    achievements/         <titleid>.toml
    cache/                ReXGlue shader cache
    launcher/settings.toml
```
The layout inside `user_data` is ReXGlue's; only the root moved. `user_data` is created on demand and is never part of a release.

## Path resolution
1. `--user_data_root=<dir>` (explicit): used as is, nothing migrated, `user_data` not created (`UserDataOutcome::kExplicit`). `tools/build.ps1 -Run` uses it for `build/user_data`.
2. Otherwise `SelectUserData({port_folder, system_root, explicit})` chooses (`ChooseUserData` in the app maps the cache root: if `cache_root` is unset it becomes `<user_data>/cache`; `--cache_root` wins).
3. `PortFolder()`: `$APPIMAGE` parent directory on Linux when set, else the executable folder.

## Decision table (`SelectUserData`)
| System folder (legacy) | `user_data` | Result | Outcome |
| --- | --- | --- | --- |
| no data | none/empty | create and use `user_data` | `kPortable` |
| has files | none, or only empty folders | move in (below) | `kMigrated` / `kMigratedLegacyKept` / `kMigrationFailed` |
| has files | has files | use `user_data`; old left untouched; one warning | `kBothExist` |
| any | not writable (read-only location, Program Files) | system folder for this run; one warning | `kPortableUnavailable` |

`HoldsFiles` counts anything that is not a plain directory as data; a folder that cannot be read counts as non-empty so it is never mistaken for empty.

## Migration algorithm
1. If `user_data` exists but is only empty folders, remove it; refuse if it exists and is not a directory.
2. `rename(system, user_data)` (same volume).
3. If rename fails (different drive): remove any earlier `user_data.migrating`, copy the tree (files, symlinks copied as symlinks, other types abort), compare a listing (relative path, file type, size) of original and copy, rename `user_data.migrating` -> `user_data`.
4. Only then `remove_all(system)`. If that alone fails: `user_data` is used and the outcome records "old folder remains" (`kMigratedLegacyKept`).
5. Any earlier failure: staging removed, old folder untouched and used for this run, one warning logged, retried at next start. A partial `user_data` is never active.
File operations (`rename`, `copy_file`, `remove_all`) go through `UserDataFileOps` so tests inject failures at each step.

Logging: one `User data:` line per start (`DescribeUserData`); the About page explains the active folder (`UserDataFolderHelp`).

## Settings storage
- Launcher/game player settings: `<user_data>/launcher/settings.toml`; shipped defaults `geometry_wars_retro_evolved_2.toml` next to the executable (copied by CMake). `LoadPlayerSettings` runs `rex::cvar::LoadConfig(player_file)` in `OnPostInitLogging`, so player values override shipped values before `OnPreSetup` captures them.
- Only changed keys are written; unknown lines and comments are preserved (TRG text settings; atomic replace).
- `GameSettings` model, the PLAY gate and the restart decision: [03](03-launcher.md). The set of keys is `ManagedSettings()`.
- Launcher window state keys are separate and survive "Reset all settings".
- F11 in game and the cheat settings can write a single key immediately (`SaveNow`).
- Settings migration example: `ApplyMultisampling` (see [06](06-graphics-vulkan.md)) replaces an old `native_2x_msaa = false` saved by v0.2.x's "Off" choice. Do the same whenever you rename or re-define an option: write the migration in the same place that reads the value, and test it.
- Game profile (48 bytes), local stats and achievements: [11](11-offline-services-profile-achievements.md).

## Source-folder behavior
Running from the build tree (`build/`) keeps `game`, `logs`, `user_data` next to the program. `.gitignore` excludes `user_data/` and `user_data.migrating/`. Developers should pass `--user_data_root=build/user_data` (as `-Run` does) to avoid creating data in source folders.

## Packaging rules
Packagers reject `user_data`, `user_data.migrating`, `cache`, `logs`, `<titleid>` folders, `settings.toml`, game files and save/log/shader-cache file types in staged output ([14](14-packaging-release.md)); `tools\test.ps1` finishes by running `package-release.ps1 -VerifyOnly` against scratch folders.

## Update behavior
Extract a newer ZIP over the old folder, or move `user_data` and `game` into the new one; deleting the old folder deletes the saves. The AppImage keeps `user_data` beside the AppImage file, so replacing the AppImage keeps data.

## Platform locations
| | Windows | Linux |
| --- | --- | --- |
| Legacy | `Documents\geometry_wars_retro_evolved_2` | `$XDG_DATA_HOME/geometry_wars_retro_evolved_2` (normally `~/.local/share/...`) |
| Current | `<exe folder>\user_data` | `<AppImage folder>/user_data`, or `<exe folder>/user_data` |
| Fallback when not writable | legacy folder for that run | legacy XDG folder for that run |

## Pitfalls and confidence
- ReXGlue's cache root derives from the system root, so moving the user root must also move `cache_root` unless the user set one (`ChooseUserData`).
- Moving data before anything reads or writes it: the selection happens in `OnConfigurePaths`, before logging and cvar loading. The log file goes to `<port folder>/logs` (falling back to `<user_data>/logs`) when the executable folder is not the port folder.
- Portable data and migration are covered by unit tests with temporary folders only; `CONFIRMED` for logic, `UNCERTAIN` for AppImage and Deck behavior on real hardware, and a manual Windows test was pending when written.
