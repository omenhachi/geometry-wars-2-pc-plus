# Custom modes

A custom mode is one of the game's six modes with your own rules: Deadline at speed, Evolved with a single ship, Pacifism with no gates where the score is how long you last. Open **Custom modes** on the main menu, choose one, and play.

Custom runs never count officially. Nothing goes to the online leaderboards or the local boards, nothing unlocks (achievements or modes), and nothing is saved to the game's profile. Each custom mode keeps its own best, and its runs are kept as replays like any other.

## Making one

1. On the Custom modes list press **Y** (or the key the list shows) to open your mods folder, `user_data/mods`. The first time, it is made with a `_template` folder inside.
2. Copy `_template`, rename the copy (the folder name is only for you), and edit its `mode.toml`.
3. Open the list again. It reads every mode fresh each time it opens, so there is no need to restart the game.

A mode that can't be used stays in the list in red, with the line it's on and what's wrong: `Line 6: lifes isn't a rules key - did you mean lives`.

The examples that come with PC Plus live beside the game in `assets/modes`. Updates replace them, so edit copies in `user_data/mods` instead. Folders whose names start with `_` or `.` are skipped.

## mode.toml

```toml
format = 1
name = "Pure Pacifism"
author = "Light"
description = "No gates, so no way to fight back. Your score is how long you last."
base = "pacifism"

[rules]
gates = false
score = "time"
```

| Key | Required | Value |
|---|---|---|
| `format` | yes | `1`. A newer number means the mode needs a newer PC Plus, and the list says so. |
| `name` | yes | Up to 64 characters. |
| `author` | no | Up to 64 characters. |
| `description` | no | One line, up to 200 characters, shown under the list. |
| `base` | yes | `"deadline"`, `"evolved"`, `"sequence"`, `"waves"`, `"pacifism"` or `"king"`. |

Every rule is optional. A rule you leave out stays as the base mode has it.

| Rule | Range | What it does |
|---|---|---|
| `lives` | 1 to 99 | Ships at the start of the round. |
| `bombs` | 0 to 99 | Bombs at the start of the round. |
| `infinite_bombs` | true / false | Bombs are never used up. |
| `time_limit` | 10 to 3600 | Seconds. The round ends when it runs out, in any mode. The tag at the bottom counts down. |
| `game_speed` | 0.5 to 1.5 | Everything runs this much faster or slower, timers included: Deadline at 1.3 is over in 2:18. |
| `gates` | true / false | `false`: no gates spawn. |
| `score` | `"points"` / `"time"` | `"time"`: your score is how long you survive, in hundredths of a second, shown as a time. |

Text in a mode file is shown with the game's line font, which draws letters, digits and simple punctuation.

A value out of range is brought into range, and the list shows a yellow note about it. A key the game doesn't know, or a value of the wrong type, is an error: nothing is ever silently ignored.

## How it works

The game only knows its six modes, so a custom mode is its base mode's round with changes:

- Each mode's setup function writes a few settings (starting lives and bombs, a time limit) into one block as a round is prepared. A custom mode's values are written straight after it. Before any setup runs, whatever a custom mode changed is put back, so an official round always finds the settings as the game left them.
- Without gates, the enemy spawner skips its gate pair.
- Game speed scales the game's step for each tick. Replays record the scaled steps, so they play back exactly.
- While a custom mode or a replay plays, the profile's unlocks and unlock progress are held as they were before it began.

A custom mode ends when its round is over and the game is back in its menus, so the next run you start from the Single Player screen is always the game's own. A mode's best is keyed by a hash of its base and rules (`user_data/mods/bests.txt`), so fixing a typo in its name or description keeps its best, while changing a rule starts a fresh one.

## What's next

Rules are added one at a time, each checked in the game before it ships. Ideas being looked at: spawn rate and enemy mix, the multiplier, starting shields. Scripting (code that reacts as enemies die or waves arrive) may come later.
