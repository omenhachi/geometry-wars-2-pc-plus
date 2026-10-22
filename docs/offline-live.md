# Offline Xbox Live services

The game's scores, leaderboards and achievements work offline for the one local player. Nothing is sent anywhere: there is no Xbox Live connection, no Microsoft sign-in, no remote player and no online multiplayer. The game stores no scores in its profile; the port keeps them in a local file and answers the game's Xbox Live calls from it.

## What the game uses

The game has one leaderboard per game mode, written and read through these calls:

| Purpose | Game call | Handled by |
|---|---|---|
| Sign-in state | `XUserGetSigninState` | the port: user 0 is reported as signed in to Live |
| Stats session | `XSessionCreate`, `JoinLocal`, `Start`, `End`, `Delete` | ReXGlue (accepts them) |
| Score write | `XSessionWriteStats` | the port records it |
| Leaderboard reads | `XUserCreateStatsEnumeratorByRank` / `ByXuid`, `XEnumerate` | the port |
| Scoreboard preview and friends board | `XUserReadStats` | the port fills ReXGlue's empty answer, with the online players around you in the friends' rows |
| Friends | `XFriendsCreateEnumerator` | ReXGlue fails it, so the game sees no friends |
| Achievements | `XUserWriteAchievements`, `XUserCreateAchievementEnumerator` | ReXGlue |
| Achievements screen | `XamShowAchievementsUI` | the port opens its achievements overlay |

The only network imports are `NetDll_XNetStartup` and `NetDll_WSAStartup`, which ReXGlue answers. The game has no socket, matchmaking or online multiplayer calls, so reporting a Live sign-in cannot start any network code. Only the leaderboard, score and scoreboard code tests the Live state; profile and achievements accept a local sign-in as well.

Not provided: the gamer card (`XamShowGamerCardUIForXUID`) and the marketplace stay ReXGlue stubs.

`gw2_local_live = false` (a command-line or config setting, read at start) turns the score and leaderboard part off; the player is then signed in locally again, as ReXGlue reports.

## Scores

A score is submitted after every finished single-player game outside the trial, except when it does not beat the best the game already sent in the same run. The property is `0x20000001`, a signed 64-bit integer, with no other columns; larger is better. The port keeps the best score per user and view and never lowers it.

| Mode | View id (the game's mode index) |
|---|---|
| Deadline | 0 |
| Evolved | 1 |
| Sequence | 2 |
| Waves | 3 |
| Pacifism | 4 |
| King | 5 |

The file is `<user_data_root>/584108FF/live/stats.toml`:

```toml
version = 1

[users.B13EBABEBABEBABE]
view_0 = 123456
view_2 = 9000
```

It is written next to the original as `stats.toml.tmp`, flushed to disk and renamed over it, so an interrupted save leaves the previous file. A file that cannot be parsed is moved to `stats.toml.corrupt` and the scores start empty. Deleting `<user_data_root>/584108FF/live` removes every score.

## Leaderboards

Only the local player exists, so each board has at most one row.

| Screen | Answer |
|---|---|
| Everyone | the player at rank 1 when the view has a score, else no rows ("no results") |
| My Score | the same row |
| Friends (shown as Nearby), and the preview on the mode select screen | the player's row, unranked with a score of zero for a view never played; with online leaderboards on, also the players around them (below) |

The personal best shown while playing comes from the same preview data. The handles the port gives out for these enumerations start at `0xEC000000`, away from the runtime's `0xF8xxxxxx`, are used once and then dropped, and are capped at 64 unserved.

## Players around you

With online leaderboards on (see the README), the friends' rows are filled with the PC Plus players nearest the player's score. The game reads its friends' scores with `XUserReadStats`, first asking for the size of the answer (no results buffer; it expects `ERROR_INSUFFICIENT_BUFFER`) and then allocating that much. The size answer makes room for 5 more rows per view, and the answer adds up to 3 players just above the player's score and 2 at or below it (equal scores count as below, because the game's score to beat is the lowest score strictly above), more from one side when the other runs short; a view never played gets the board's lowest scores. The game sorts the rows, highlights the player's own, shows them on the mode tiles and on the Nearby board, and during a run shows the lowest one above as the score to beat, moving up as each is passed. Ranks are counted on the client with the player's own score in.

The rows are picked from each mode's online board (the top 50), fetched in the background the first time the game asks after a start and again after each accepted score, and kept in `<user_data_root>/584108FF/live/nearby.txt` so they show at the next start before the fetch. The hook never waits on the network: without a cached board it answers as before, and rows that would not fit the buffer the game allocated are dropped. "Friends", "Friends Leaderboard" and "Next Friend" read "Nearby", "Nearby Players" and "Next Player" while online leaderboards are on. Choosing View Gamer Card on another player's row reaches ReXGlue's stub, which does nothing.

## Mode unlocks

Unlock progress is not part of the stats. It lives in the 48-byte profile (setting `0x63E83FFF`, see [offline-profile.md](offline-profile.md)): words 5 to 10 hold the progress of Evolved, Deadline, Pacifism, Waves, King and Sequence, and word 4 is the mask of unlocked modes. The chain is Deadline, King, Evolved, Pacifism, Waves, Sequence. The port does not touch it.

## Achievements

The 12 achievements (200 gamerscore) come from the XDBF inside `default.xex`: names, descriptions, gamerscore and icons, in the language ReXGlue is set to. The game's `XUserWriteAchievements` unlocks them in ReXGlue's achievement store, which saves `<user_data_root>/achievements/584108FF.toml` and raises the stock unlock notification. The game reads its earned achievements back through ReXGlue's enumerator when it starts.

Choosing Achievements in the game opens the achievements overlay: the 12 achievements in a window centred on the screen, about 70% of the viewport and no larger than 560 by 460. While it is up the game sees a neutral controller, the left stick or D-pad scrolls the list, and B closes it; Escape and F7 close it too. F7 also opens it at any time. The achievement data, icons and unlock toast are ReXGlue's; the window, the list and the B Back footer are drawn by the project's overlay in ReXGlue's style.

## Files

| Path under `<user_data_root>` | Content |
|---|---|
| `584108FF/profile/User/63E83FFF` | the game's 48-byte profile |
| `584108FF/live/stats.toml` | best score per view |
| `584108FF/live/nearby.txt` | the online boards the players around you were last picked from |
| `achievements/584108FF.toml` | unlocked achievements |

`<user_data_root>` is the `user_data` folder next to the program (see [user-data.md](user-data.md)). `tools\build.ps1 -Run` uses `build\user_data` as the user data root, and `-Clean` keeps it. Delete that folder for a fresh player.

## Diagnostics

The log says once at startup whether the local Live services are on and whether the stats file was loaded, missing or corrupt. A warning is logged when a new best score could not be written to the file.

## Tests

`.\tools\test.ps1` builds and runs `tests\live_tests.cpp`: the stats file (saving, restart, corruption, interrupted saves), the leaderboard answers and the guest buffers they write. It needs no game files.
