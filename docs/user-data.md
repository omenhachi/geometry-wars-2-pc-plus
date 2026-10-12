# Saves and settings: the portable `user_data` folder

Everything the port writes for the player lives in one folder, `user_data`, next to the program. There is no setting
for it and no second mode.

```
GeometryWarsRetroEvolved2\
    geometry_wars_retro_evolved_2.exe
    geometry_wars_retro_evolved_2.toml
    game\
    logs\
    user_data\
        584108FF\
            profile\            the game's profile and options
            live\               local scores and offline leaderboards (stats.toml)
        achievements\           unlocked achievements
        cache\                  ReXGlue's shader cache
        launcher\               launcher settings and window placement (settings.toml)
```

The layout inside `user_data` is ReXGlue's own, unchanged: only its root moved. Copying the whole program folder to
another PC carries the profile, scores, achievements and launcher settings with it. `user_data` is created the first
time it is needed and is never part of a release download.

## Where the folder is

| System | `user_data` is in |
| --- | --- |
| Windows | the folder that holds `geometry_wars_retro_evolved_2.exe` |
| Linux AppImage | the folder that holds the AppImage file (never inside its read-only mount) |
| Linux, outside an AppImage | the folder that holds the program |

This is the same folder as `game` and `logs` (`PortFolder()` in `src/launcher_platform.cpp`). `--user_data_root=<folder>`
still wins over everything: when it is given, that folder is used as it is, nothing is migrated and `user_data` is not
created. `tools\build.ps1 -Run` uses it to keep a development profile in `build\user_data`. `--cache_root` likewise
keeps the cache where it names; without it the cache follows the user data root.

The About page's **Saves and settings** row opens the folder in use and says which one it is: the portable
`user_data`, the system folder after a fallback, or the `--user_data_root` folder.

## Upgrading from v0.1.0

Version 0.1.0 kept this data in the system's user folder:

| System | v0.1.0 folder |
| --- | --- |
| Windows | `Documents\geometry_wars_retro_evolved_2` |
| Linux | `$XDG_DATA_HOME/geometry_wars_retro_evolved_2`, normally `~/.local/share/geometry_wars_retro_evolved_2` |

The first start of a newer version **moves** that folder into `user_data` before anything reads or writes it. After a
successful move the old folder no longer exists. Nothing is left in Documents or under XDG, and nothing is merged.

The move goes in this order (`SelectUserData` in `src/user_data.cpp`):

1. If `user_data` does not exist (or holds no files, only empty folders) and the old folder holds files, rename the old
   folder to `user_data`.
2. If the rename is not possible, for example because Documents and the game are on different drives, copy the old
   folder into `user_data.migrating` next to `user_data`, check that the copy has the same folders and files with the
   same sizes, then rename it to `user_data`.
3. Only then remove the old folder.

If any step fails, the old folder is left exactly as it was, no partial `user_data` is ever active, the temporary
`user_data.migrating` is removed, the old folder is used for that run, one warning is logged, and the move is tried again
at the next start. If only removing the old folder fails, `user_data` is used and the log says the old folder remains.

## Cases

| Situation | Result |
| --- | --- |
| No old data, no `user_data` | `user_data` is created and used |
| Old data, no `user_data` (or an empty one) | old data moves in, as above |
| `user_data` already holds data and the old folder does too | `user_data` is used. The old folder is neither merged nor deleted, and one warning says both exist |
| `user_data` cannot be created or written (a read-only location, or "Program Files") | the system folder is used for that run and one warning is logged |
| `--user_data_root` given | that folder is used, nothing moves |

The log (in `logs`) carries one `User data:` line for each start, for example `User data: portable path selected: ...`,
`User data: migrated legacy data from ... to ...` or `User data: portable path unavailable, using legacy system path for
this run`.

## Updating

To update by extracting a newer ZIP, extract it over the old folder, or move `user_data` (and `game`) into the new
folder. Deleting the old program folder deletes `user_data` with it.

## Packaging

`user_data` must never be in a release. `tools\package-release.ps1` and `tools\package-linux.sh` reject `user_data`,
`user_data.migrating`, `cache`, `logs`, `584108FF`, `settings.toml`, game files and save or log file types in the
staged files, and a fresh download has no `user_data` folder.

## Tests

`tests\user_data_tests.cpp` (run by `tools\test.ps1`) covers choosing the root, the move by rename and by copy, the
collision and empty-folder rules, failures at each step, the fallback and the `--user_data_root` precedence, using
temporary folders only. `launcher_settings_tests.cpp` checks that the settings file and the launcher window placement
are written below whichever root is chosen, and `live_tests.cpp` does the same for the scores file. The last step of
`tools\test.ps1` runs `package-release.ps1 -VerifyOnly` on scratch folders and checks that a fresh extraction passes
and that `user_data`, its profile, scores, achievements and settings, `cache`, `user_data.migrating`, logs and game
files are refused.
