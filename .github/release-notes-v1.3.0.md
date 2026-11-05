# Geometry Wars 2 PC Plus 1.3.0

This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## New: Master Pacifism

A third Master mode, for really good players. It's Pacifism, but **every grunt is a weaver**: quicker, and they dodge. You still can't shoot. Press **RB** (or **3** on the keyboard) on the Single Player screen to turn to the Master page, where it sits beside Master Deadline and Master Evolved.

- **Its own online leaderboard**, with the top 50 runs watchable as replays, from **Watch replays** (beside Pacifism) or from its leaderboard on the Master page.
- **Its own Bronze to Master tiers** on the Pilot screen: Bronze 500K, Silver 3M, Gold 15M, Neon 50M, Master 150M. Neon on it earns Master Pilot, like the other Master modes.
- Brutal, on purpose.

## New for mode makers: which enemies spawn

Custom modes can now say which enemies appear (`format = 2` in `mode.toml`; older mode files work exactly as before):

- **`[enemies]`** turns any of wanderer, grunt, weaver, spinner, snake, repulsar, black hole, rocket and duck off.
- **`[enemies.swap]`** makes one enemy wherever the game asks for another: `grunt = "weaver"` is all Master Pacifism is.
- **The game's unused enemies.** It still holds enemies no mode ever uses, and a swap can bring them in as `type_7`, `type_17` and so on. They're untested leftovers: they ran without trouble, but nobody knows how they score or behave. [docs/MODDING.md](https://github.com/omenhachi/geometry-wars-2-pc-plus/blob/main/docs/MODDING.md) lists what each one looks like.
- Typos get a "did you mean", and a swap that can't do what it says gets a friendly warning in the list.
- **Hyper Pacifism** joins the examples: Pacifism at 1.25x speed with no gates and three ships, scored by how long you last.

These work in Evolved, Deadline, Pacifism and King. Sequence and Waves are made of set levels and waves, so they say so instead.

## Good to know

- **Your saves are safe.** Your Pilot save stays readable by older versions. If you open it in an older version, that version forgets your Master Pacifism best on the Pilot screen, and it comes back from your stats the next time you start 1.3.0.
- The Master Pacifism leaderboard takes scores from 1.3.0 on, so it appears for everyone once they update.
- Custom runs made with the new rules play back as replays in 1.3.0 and later.

## Installing

1. Download **GeometryWarsRetroEvolved2-v1.3.0-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v1.3.0-windows-x64.zip` | `d0ba205084d705f7310db977889fbe0a44fd38d7394f3fc99cec4ad36ee514c0` |
