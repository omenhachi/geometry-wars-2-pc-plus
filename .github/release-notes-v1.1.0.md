# Geometry Wars 2 PC Plus 1.1.0

This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## New

- **Sequence practice:** play any one Sequence level, 1 to 20. Open **Custom modes** from the main menu, pick **Sequence practice**, choose the level with **Left/Right** and press **A**. The run ends with the level, cleared or not, so the next try is one press away. Each level keeps its own best, and practice runs never touch the leaderboards, achievements or unlocks. The Single Player screen points the way.
- **For modders:** a new `level = 1-20` rule for Sequence-based modes (see docs/MODDING.md).

## Fixed

- The Custom modes list now reads a stick or WASD pushed left and right, not just the D-pad.

## Leaderboards

- Scores and replays now need **1.0.1 or newer**: before it, watching a replay to its end could send its score as yours. Older versions can still read the boards.
- A score that's an exact copy of another player's replayed run is turned away, and two copied scores have been removed.
- One-ship Master runs can end in seconds, so the server now takes a new score every 5 seconds instead of every 20, and no quick retry's score is turned away.

## Installing

1. Download **GeometryWarsRetroEvolved2-v1.1.0-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v1.1.0-windows-x64.zip` | `96fa4a26febaf2184c956e7bdda63fcb1f5018fa99255472ade60f6b6b4dc880` |
