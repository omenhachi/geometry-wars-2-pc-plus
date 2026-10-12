# Cheats

Three cheats act on the running game through function hooks (`src/cheat_hooks.cpp`). Each reads `gw2::cheats::Enabled`, so the launcher's saved value and the F10 menu's session override both apply at once, with no restart. When a cheat is Off its hook calls the original function unchanged. Scores, leaderboards, achievements, progression and saves are not touched. The launcher page and the F10 menu are described in [launcher.md](launcher.md).

The game ships its own developer cheats (`Cheat.Invulnerable`, `Cheat.Shield`, `Cheat.ExtraBomb`), registered as global variables. They show where the real logic is, and the hooks follow them:

- `Cheat.Invulnerable` is a flag read only at the top of the player's hit handler, which then returns without doing anything.
- `Cheat.Shield` sets the player's shield timer to 10 seconds.
- `Cheat.ExtraBomb` adds one to the bomb count in the player's record.

All three cheats work on the `Player` class (vtable `0x82001EB8`, methods `0x8204E908`-`0x82051B18`). A hook acts only on an object whose first word is that vtable.

## Hooks

| Function | Role | Hook |
| --- | --- | --- |
| `sub_8204EEA8` | `Player::OnHit`, reached by every enemy, hazard and collision that can kill a player | Invincible |
| `sub_8204FF00` | `Player::Update`, once per frame per player | Infinite Shield |
| `sub_8204F288` | the player's bomb input, called by `Update`; the only place a bomb is taken | Infinite Bombs |

No other project hook covers these addresses.

### Invincible

`OnHit` checks, in order: the developer flag, the bomb grace (`+0x88`, 0.2 s after a bomb), the shield (`+0x8C`), and otherwise calls `Die`. With Invincible On the hook returns without calling the original only when the hit would kill, which is when neither the grace nor the shield is above zero. Hits that the shield or the grace handle stay the game's own, so the shield keeps its normal behavior. Enemy deaths, score events, bomb effects and timers do not pass through here. It applies to every player.

### Infinite Shield

The shield is a float of seconds at `player+0x8C`. The constructor `sub_82051448` sets it to 3.33 when a player spawns (the respawn shield), `Update` counts it down and the shield blinks in its last second. The countdown is inline in `Update`, so after the original returns the hook raises the timer to 10 seconds (the game's own `Cheat.Shield` value) whenever it is lower. A shield that is already running is kept, and one that is not running is started, as the game's own cheat does. With the cheat Off the timer simply counts down from where it was, so a shield that was held runs out after at most 10 more seconds.

### Infinite Bombs

A bomb count is a 32-bit value at `0x8219D09C + 0x78 * player index` (index at `player+0xA4`, four players). The bomb function takes one only when the count is above zero and the mode has bombs. The hook reads the count before the original and, when the original took exactly one, puts it back. It never adds a bomb: a player with no bombs still gets the game's dry-fire, and a mode without bombs returns from the function before the count is touched.

## Modes

The per-mode setup functions (`0x82034EA0`-`0x82035278`) set the starting bombs at `0x8219D268` and flags at `0x8219D26C` (bit 0: no extra lives, bit 1: no bombs). Mode ids are read from `0x8219D254`.

| Mode id | Start lives | Start bombs | Flags |
| --- | --- | --- | --- |
| 0 | 0 | 3 | no extra lives |
| 1, 2 | 3 | 3 | none |
| 3, 4, 5 | 0 | 0 | no extra lives, no bombs |
| 6, 7 | 0 | 0 | none |

Infinite Bombs has nothing to keep in the modes without bombs.

## Tests

`tests\cheats_tests.cpp` covers the rules in `src/cheat_rules.h`, the session override, and that each function is hooked once and no hook reads the cvars or the score and achievement code directly.
