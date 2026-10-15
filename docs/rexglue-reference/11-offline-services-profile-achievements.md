# 11 Offline services, profile and achievements

Long-form: `docs/offline-profile.md`, `docs/offline-live.md`. Origin: `dbbf80f` (profile persistence), `bead687`
(Live services and achievements), `f9731f9` (overlay touched by cheat menu input gating).

What is reusable: the technique (hook the game's wrappers around Xbox Live/XAM calls and answer from local data) and the
data layer (`LocalStats`, enumerator registry, achievements overlay rules). The hooked function addresses and the
stats schema are `GEOMETRY_WARS_SPECIFIC` / `GAME_ADAPTATION`.

## 1. Offline profile (first-run fix)

- Problem: the game reads its 48-byte profile from the gamer profile title-specific setting `0x63E83FFF` via `XamUserReadProfileSettings`.
- Symptom: on a first run the game showed "unable to load your profile ... Memory Unit".
- Root cause: ReXGlue answers a read of a never-saved title setting with source 0 (`XSOURCE_NO_VALUE`), which the game treats as a failed load. A console answers source 1 (`XSOURCE_DEFAULT`) with an empty value, and the game then creates a new profile.
- Solution: `src/profile_fix.cpp` hooks the game's wrapper (`sub_820E89B8`); after the original returns successfully, `ReportUnsavedTitleSettingsAsDefault` (`src/profile_fix.h`) rewrites unsaved title-specific entries (title-specific mask `0x3F00` in the id) to source default, binary type, zero length. The game then writes its default profile, ReXGlue saves it at `<user_data>/<titleid>/profile/User/63E83FFF`, and later runs read saved data. A saved (even damaged) profile is left alone, so the game's own prompt and "reset profile" still work.
- Also needed for this game (`GAME_ADAPTATION`: depends on the title's trial/license behavior, not a universal setting): `license_mask = 1` in the shipped config (`XamContentGetLicenseMask`; SDK default `0` means trial, and in trial mode this game neither loads nor creates a profile).
- ReXGlue provides one local user: index 0, signed in locally, fixed XUID, name `User`.
- Logging: at most eight `Profile read:` lines per run.
- Classification: the SDK behavior (an unsaved title-specific setting is answered NO_VALUE) is a generic investigation point, but the workaround is `GAME_ADAPTATION`: it is needed only if the individual game rejects NO_VALUE, as Geometry Wars does. The hook address (`sub_820E89B8`) and the profile id are `GEOMETRY_WARS_SPECIFIC`. Port: test the new game's own first run first (fresh `user_data`); implement an equivalent hook only if the game fails on that answer, and first check whether the SDK has since changed the answer. Verify with a fresh `user_data` and a second run.

## 2. Local Live services (scores, leaderboards)

Principle: all of this is fully local; no network code exists in the game besides `NetDll_XNetStartup`/`NetDll_WSAStartup`, which ReXGlue answers. Cvar `gw2_local_live` (default on, `kInitOnly`) turns it off.

| Game call (wrapper hooked) | Behavior | File |
| --- | --- | --- |
| `XUserGetSigninState` (`sub_820955C8`) | user 0: report signed in to Live (2) instead of locally (1) | `src/live_fix.cpp` |
| `XSessionWriteStats` (`sub_820E88F8`) | after the original succeeds, record each view's score for the local XUID | `src/live_fix.cpp`, `src/local_stats.cpp` |
| `XUserCreateStatsEnumeratorByRank/ByXuid` (`sub_820956B8`, `sub_82095718`) | create a local enumerator (ReXGlue only stubs it); handles start at `0xEC000000` | `src/live_stats.cpp` |
| `XEnumerate` (`sub_820958D0`) | serve rows for the local handles, else call ReXGlue's | |
| `XUserReadStats` (`sub_820955D0`) | fill ReXGlue's empty answer (preview, friends board) | |
| `XamShowAchievementsUI` (`sub_82094DF8`) | open the project's overlay | |
| `XInputGetState` (`sub_82094B60`) | neutralize pad input while the overlay is open | |

- Scores: property `0x20000001` signed 64-bit, larger is better; best per user and view, never lowered; submitted after every finished single-player game (the game's rule). View ids = the game's mode index (Deadline 0, Evolved 1, Sequence 2, Waves 3, Pacifism 4, King 5).
- File `<user_data>/<titleid>/live/stats.toml` (`version = 1`, `[users.<XUID>] view_N = score`); written to `stats.toml.tmp`, flushed, renamed over the original; unparsable file moved to `stats.toml.corrupt`; a failed write keeps the score in memory and logs a warning.
- Leaderboards: only the local player exists, so each board has at most one row; the "Friends" board and the preview show the player's row unranked with 0 for a never-played view. Enumerator handles are used once and capped at 64 unserved.
- Mode unlocks live in the 48-byte profile (words 4-10), not in stats; untouched.
- Port: the hooks are per game (find `XSessionWriteStats` etc. wrappers); the stats file, enumerator registry and guest-buffer writers in `live_stats.cpp` are reusable if the game uses the same Live APIs. `GAME_ADAPTATION`.
- Tests: `tests/live_tests.cpp` (stats file, corruption, interrupted saves, leaderboard answers, guest buffers). No game files needed.

## 3. Achievements

- Data: the 12 achievements (200 G) come from the XDBF resource in `default.xex` (names, descriptions, icons, gamerscore, localized to the configured language). The SDK's `XUserWriteAchievements` unlocks into ReXGlue's achievement store `<user_data>/achievements/<titleid>.toml` and raises the stock unlock notification; the game reads earned achievements back through ReXGlue's enumerator.
- Overlay (`src/achievements_overlay.cpp`, rules in `src/achievements_overlay_rules.h`): drawn in ReXGlue's style, size about 70% of the viewport clamped to 560x460; opens from the game's Achievements item (hook above) or F7; while up the game sees a neutral controller, left stick/D-pad scrolls, B/Escape/F7 close, and the B that closed it never reaches the game (`AchievementsOverlayGate`).
- Callbacks: `live::Install` takes an `AppBridge` (open/close) that the app marshals to the UI thread with `CallInUIThreadDeferred` (the hooks run on guest threads).
- Install timing: `OnPostSetup`, after the kernel state has a user profile.
- Launcher: read-only Achievements page uses the same manager and icon cache (`src/launcher_achievements.cpp`).
- Limits: gamer card and marketplace stay SDK stubs (logged `STUB`); friends enumerator fails so the game sees no friends.

## Lifetime notes
`g_services` is allocated once and intentionally never freed because hooks may run while the process exits. Do not "fix" this by deleting at shutdown without first proving no guest thread can still call the hooks (see [13](13-crash-hardening-threading-memory.md)).
