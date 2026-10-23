# Geometry Wars 2 PC Plus v0.10.0

**Make your own modes.** This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## What's new

- **Custom modes.** A new **Custom modes** row on the main menu (after Watch replays) plays the game's six modes with your own rules. Each round opens with a title card of its name and rules, and a tag at the bottom keeps your time or the countdown.
  - Five come with it:
    - **Pure Pacifism:** no gates, so you can't fight back, and your score is how long you last.
    - **Overdrive:** Deadline at 1.3x speed.
    - **One Life:** Evolved with a single ship and no bombs.
    - **Bomb Party:** Waves with bombs that never run out, for two minutes.
    - **Speed Test:** gateless Pacifism at 1.4x.
  - Rules so far: `lives`, `bombs`, `infinite_bombs`, `time_limit` (in any mode), `game_speed` (0.5 to 1.5), `gates` and `score = "time"`.
  - **Make your own.** On the list press **Y** to open `user_data/mods` (it comes with a template), copy the template, and edit its `mode.toml`. The list reads your modes fresh every time it opens, and a mistake is shown in red with its line and a suggestion ("lifes isn't a rules key - did you mean lives"). Every key is explained in [docs/MODDING.md](https://github.com/omenhachi/geometry-wars-2-pc-plus/blob/main/docs/MODDING.md).
  - **Custom runs never count.** Nothing goes to the online or local leaderboards, nothing unlocks (achievements or modes) and nothing is saved to the game's profile. Each custom mode keeps its own best instead.
  - **Custom runs are saved as replays**, with their rules inside, so they play back exactly even without the mode file.
- **Replays never move your unlock progress**, the same as custom modes.
- The main menu's rows are a touch smaller so all eight fit under the logo.

Everything from v0.9.0 is still here: replays, the results screen, Deadline rank info, your colour everywhere, nearby players and steadier sound.

## Installing

1. Download **GeometryWarsRetroEvolved2-v0.10.0-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.10.0-windows-x64.zip` | `1f0691e8eca9c6b0bdc0c10d0a2fe5346c54873241845d94ba33097abdd7ada3` |
