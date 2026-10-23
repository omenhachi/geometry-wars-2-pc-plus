# Geometry Wars 2 PC Plus v0.9.0

**Watch your runs again.** This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers, and **no game files are included**: you need your own Xbox Live Arcade package.

## What's new

- **Replays.** Every single-player run you finish is saved as a replay, and **Watch replays** on the main menu (between Multiplayer and Leaderboards) plays it back exactly, shot for shot, at any frame rate.
  - Newest first, with a tab for each mode you've played. **A** or **Enter** watches, **Y** stars a replay so it's kept, **X** deletes after asking once, **LB**/**RB** change mode, **B** or **Esc** goes back. Keyboard hints follow your own key bindings.
  - The newest ten, your best in each mode and every starred replay are kept; older ones go quietly. They live in `user_data/demos`.
  - A replay never counts: its score isn't kept or sent online, and it unlocks and saves nothing. A badge shows while one plays; **Start** stops it, and the rest of that round is yours to play, still for nothing.
  - Replays are plain numbers (button presses and the run's starting state) in a strict, versioned file. Nothing in one is ever run.
  - Runs with cheats on aren't saved.
- **Sparks no longer touch the game's dice.** Explosion sparks and glows now draw their own random numbers, so how busy the screen is can never change what spawns next. That's what makes replays exact; the game plays the same as ever.

Everything from v0.8.2 is still here: the results screen, Deadline rank info, your colour everywhere, nearby players and steadier sound.

## Installing

1. Download **GeometryWarsRetroEvolved2-v0.9.0-windows-x64.zip**, extract it anywhere you can write to, and run `geometry_wars_retro_evolved_2.exe`. Updating: extract over your current folder, or use the launcher's update.
2. On the **Play** page click **Install from package...** and pick your Geometry Wars: Retro Evolved 2 package (Title ID `584108FF`, game version 0.0.1.2).
3. Press **PLAY**.

Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64) are needed. The build is not digitally signed, so SmartScreen may warn: choose **More info**, then **Run anyway**.

## Files

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.9.0-windows-x64.zip` | `f5a3757effedd2131a0bcd37663725eeafad044e4adf27dfde7a89da68cd0c81` |
