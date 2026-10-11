# Offline profile and save

The game runs as one local, offline player. Nothing needs Xbox Live or an Xbox 360 Memory Unit.

## What the game uses

The game keeps its profile (48 bytes of options and defaults) in the gamer profile's title-specific setting `0x63E83FFF`. It reads it with `XamUserReadProfileSettings` and writes it with `XamUserWriteProfileSettings`.

Its other user imports are sign-in queries: `XamUserGetSigninState`, `XamUserGetXUID`, `XamUserGetName` and `XamShowSigninUI`. It imports no `XamContent*` function and no device selector, so no storage device is involved. Leaderboards and other Xbox Live features are not provided.

The game loads and creates a profile only in full-license mode. `license_mask = 1` in `config/geometry_wars_retro_evolved_2.toml` provides that through `XamContentGetLicenseMask`; in trial mode the game neither loads nor creates a profile.

## Where it lives

ReXGlue provides one local user: index 0, signed in locally, with a fixed XUID and the name `User`. Indexes 1 to 3 are not signed in. The profile file is:

```
<user_data_root>/584108FF/profile/User/63E83FFF
```

`tools\build.ps1 -Run` passes `build\user_data` as the user data root, and `-Clean` keeps that folder. Delete `build\user_data`, or only its `584108FF\profile` folder, to start with a fresh profile.

## First run

ReXGlue answers a read of a never-saved title setting with source 0 (no value), which the game treats as a failed load: "unable to load your profile ... Memory Unit". A console answers source 1 (default) and the game then creates a new profile.

`src/profile_fix.cpp` hooks the game's read wrapper (`sub_820E89B8`) and gives unsaved title-specific settings the console's answer. The game then writes its default profile, ReXGlue saves it, and the next launch reads it back as saved data.

A saved profile, including a damaged one, is left alone. The game's own prompt still appears for it, and RESET PROFILE TO DEFAULT still works.

## Diagnostics

The hook logs `[DEBUG_SAULO][Save]` lines, at most eight per run: `0 saved title setting(s), 1 unsaved reported as default` on a first run, and `1 saved ... 0 unsaved ...` on later runs.
