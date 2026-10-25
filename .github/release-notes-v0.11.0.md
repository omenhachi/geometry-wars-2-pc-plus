# Geometry Wars 2 PC Plus v0.11.0

**Two new modes, with their own leaderboards.** This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## What's new

- **Master Deadline and Master Evolved.** One ship, no spares. The only help you get is a bonus bomb when your score passes 100K, 1M, 10M and 100M. No extra ships or bombs come from score milestones.
- **A second page on the Single Player screen.** Press **RB** on a controller (or **3** on the keyboard) to turn to the Master page, and again to turn back. Both Master modes are always open: no unlocking needed.
- **Their own online leaderboards.** A Master run goes to its Master board only, never to Deadline's or Evolved's. The mode tiles, the HUD's score to beat and the players around you all come from the Master boards. The rules are fixed in the game, so every score on a Master board was made by the same rules.
- **Custom modes can do the same.** New rules for mode files: `extra_lives`, `extra_bombs` and `bonus_bombs_at`, and `lives` now means ships in all (so `lives = 1` is a single ship, and works in Deadline too). See [docs/MODDING.md](https://github.com/omenhachi/geometry-wars-2-pc-plus/blob/main/docs/MODDING.md). Older custom-mode replays still play the same.
- The results screen names the Master mode you played.

Online leaderboards only take Master scores from v0.11.0 or newer. Everything from v0.10.2 is still here: the benchmark, the Fast render path, custom modes and replays.

## Installing

1. Download **GeometryWarsRetroEvolved2-v0.11.0-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.11.0-windows-x64.zip` | `bf9b9ea6acc10c98bdc3843701d7792ab64c61c37f2c7aa8c0f4d3a1bda4574a` |
