# Geometry Wars 2 PC Plus v0.10.2

**Measure it on your PC.** This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## What's new

- **Run benchmark**, at the bottom of the launcher's **Graphics** page. It plays the same one-minute replay on every PC (a busy Deadline run), times every frame with your current settings, and shows the result over the game: average frames per second and the slowest moments. The result is copied for you with what it ran on (graphics card, render path, resolution, frame rate limit), so you can paste it on the [Discord](https://discord.gg/ZJtcwYPN7) and compare. This is how we'll find what to speed up next on laptops, so please share yours.
- **Internal render resolution** only offers **75%** where it's actually faster. Measured on the same replay, 75% was slower than 100% at 3x Render resolution and no faster at 1x and 2x, because the game copies its picture at a whole-number scale; it's 55% faster at 4x. If you had 75% chosen where it doesn't help, it stays in the list, marked "no faster at this resolution". **50%** stays everywhere.

**Laptop or Intel graphics?** If you haven't yet, set **Graphics > Render path** to **Fast** (new in v0.10.1). On Intel the game otherwise uses a path that measured 34 to 45% slower. Then run the benchmark and tell us how it went.

Everything from v0.10.1 is still here: the Render path setting, the Discord tab, your name in the launcher, custom modes and replays.

## Installing

1. Download **GeometryWarsRetroEvolved2-v0.10.2-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.10.2-windows-x64.zip` | `e3755958ee65310970a33ea58655290895ed59ed2fea4c2f7585df2e3a750db4` |
