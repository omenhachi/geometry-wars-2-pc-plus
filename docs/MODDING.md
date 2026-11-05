# Custom modes

A custom mode is one of the game's six modes with your own rules: Deadline at speed, Evolved with a single ship, Pacifism with no gates where the score is how long you last. Open **Custom modes** (press right on the main menu, or click its panel), choose one, and play.

Custom runs never count officially. Nothing goes to the online leaderboards or the local boards, nothing unlocks (achievements or modes), and nothing is saved to the game's profile. Each custom mode keeps its own best, and its runs are kept as replays like any other. They do earn Pilot XP, which only ever shows on your own screen: full XP for time survived, and 250 for a new best in the mode.

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
| `format` | yes | `1` or `2`. Format 2 lets a mode use the tables below `[rules]`, like `[enemies]`; a format 1 file reads exactly as it always has. A newer number means the mode needs a newer PC Plus, and the list says so. |
| `name` | yes | Up to 64 characters. |
| `author` | no | Up to 64 characters. |
| `description` | no | One line, up to 200 characters, shown under the list. |
| `base` | yes | `"deadline"`, `"evolved"`, `"sequence"`, `"waves"`, `"pacifism"` or `"king"`. |

Every rule is optional. A rule you leave out stays as the base mode has it.

| Rule | Range | What it does |
|---|---|---|
| `lives` | 1 to 99 | Ships in all: `1` is a single ship, with none to spare. Also works in Deadline, which then ends on your last death instead of giving you ships forever. |
| `extra_lives` | true / false | `false`: the game never gives extra ships at score milestones (Evolved normally does). |
| `bombs` | 0 to 99 | Bombs at the start of the round. |
| `extra_bombs` | true / false | `false`: the game never gives extra bombs at score milestones. |
| `bonus_bombs_at` | up to 8 scores | A bomb each time your score passes one of these, like `[100000, 1000000, 10000000, 100000000]`. |
| `level` | 1 to 20 | Sequence only: just this level, from its start. The run ends when the level does (cleared, or a death). Custom modes' Sequence practice is this rule. |
| `infinite_bombs` | true / false | Bombs are never used up. |
| `time_limit` | 10 to 3600 | Seconds. The round ends when it runs out, in any mode. The tag at the bottom counts down. |
| `game_speed` | 0.5 to 1.5 | Everything runs this much faster or slower, timers included: Deadline at 1.3 is over in 2:18. |
| `gates` | true / false | `false`: no gates spawn. |
| `score` | `"points"` / `"time"` | `"time"`: your score is how long you survive, in hundredths of a second, shown as a time. |

### [enemies] (format 2)

Which enemies spawn. Every one is on unless you turn it off:

```toml
format = 2
name = "No Snakes"
base = "evolved"

[enemies]
snake = false
black_hole = false
```

The names are `wanderer`, `grunt`, `weaver`, `spinner`, `snake`, `repulsar`, `black_hole`, `rocket` and `duck`. Gates stay under `[rules]` as `gates`. `[enemies]` works in Evolved, Deadline, Pacifism and King. Sequence and Waves are made of set levels and waves that could never end with an enemy missing, so they say so instead. Turning every enemy off is an error.

### [enemies.swap] (format 2)

Every time the game asks for one enemy, it gets another instead, in the same place:

```toml
format = 2
name = "Green Pacifism"
base = "pacifism"

[enemies.swap]
grunt = "weaver"
```

- The left side is any of the nine `[enemies]` names. The right side is the enemy made in its place.
- It happens once, on the enemy the game asked for: `grunt = "weaver"` with `weaver = "grunt"` trades the two, it never goes round in circles.
- Turning an enemy off wins: if `grunt = false`, a grunt swap does nothing (the list warns you). Swapping into an enemy you turned off still makes it, and the list warns you about that too.
- One enemy per swap for now; a list gives an error.
- Like `[enemies]`, it works in Evolved, Deadline, Pacifism and King.

Enemies a swap can make: `wanderer`, `grunt`, `weaver`, `spinner`, `repulsar`, `snake` and `duck`. Not `black_hole` or `rocket`: the game hands them details meant for the enemy it asked for (a direction, a flag), so they would come out wrong.

#### The game's unused enemies

The game still has enemies no mode ever uses, and a swap can make them too, by their number in the game's spawner: `type_7`, `type_9`, `type_10`, `type_11`, `type_15`, `type_17`, `type_18`, `type_19`, `type_20`, `type_21`, `type_23`, `type_24` and `type_25`. They have no names because the game never shows them, and they are **untested**: each one ran for half a minute without trouble, but nobody knows how they score, whether they behave, or how many the game can hold. Here is what they looked like:

| | |
|---|---|
| `type_7` | pairs of small blue arrowheads |
| `type_9` | clusters of coloured bubbles that pile up |
| `type_10` | a small blue spinning atom |
| `type_11` | big blobs of blue cells |
| `type_15` | white six-pointed stars |
| `type_17` | green rounded hexagons |
| `type_18` | white shapes that shoot orange pellets |
| `type_19` | long pink zigzags |
| `type_20` | a small blue star |
| `type_21`, `type_23`, `type_25` | pink crosses with trails, a pink square, a blue ring |
| `type_24` | tiny green dots |

`type_14` is left out: it reads a direction the game only gives rockets, and it kills almost instantly.

Text in a mode file is shown with the game's line font, which draws letters, digits and simple punctuation.

A value out of range is brought into range, and the list shows a yellow note about it. A key the game doesn't know, or a value of the wrong type, is an error: nothing is ever silently ignored.

## How it works

The game only knows its six modes, so a custom mode is its base mode's round with changes:

- Each mode's setup function writes a few settings (starting lives and bombs, a time limit) into one block as a round is prepared. A custom mode's values are written straight after it. Before any setup runs, whatever a custom mode changed is put back, so an official round always finds the settings as the game left them.
- Without gates, the enemy spawner skips its gate pair, it skips any enemy `[enemies]` turns off, and it makes a swapped enemy's target instead. The game asks for each enemy by a type number, and none of the places that ask use what they get back, so a skipped enemy is simply never made.
- The game gives extra ships and bombs only from its kill award, so without them, whatever that award adds is taken back straight away. Bonus bombs are given after each game tick, from the score.
- Game speed scales the game's step for each tick. Replays record the scaled steps, so they play back exactly.
- While a custom mode or a replay plays, the profile's unlocks and unlock progress are held as they were before it began.

A custom mode ends when its round is over and the game is back in its menus, so the next run you start from the Single Player screen is always the game's own. A mode's best is keyed by a hash of its base and rules (`user_data/mods/bests.txt`), so fixing a typo in its name or description keeps its best, while changing a rule starts a fresh one.

## What's next

Rules are added one at a time, each checked in the game before it ships, and only listed here once they work. Being looked at next: geoms, the ship's guns, black hole gravity, enemy speed and spawn rate.
