# Replays that pin how the game plays

Each replay here must play back to the score in its name. If one drifts, something changed how the game plays (a
hook, a custom mode rule, the replay format) and the boards built on it are no longer fair.

| Replay | Mode | Score |
|---|---|---|
| `master-pacifism-26183625.gw2demo` | Master Pacifism (board 8), by Omenhachi | 26,183,625 |

The Deadline benchmark (`assets/benchmark/benchmark.gw2demo`, 18,626,515) does the same for the game's own rules.

To check one, play it through the benchmark player and read the log:

```
geometry_wars_retro_evolved_2.exe --gw2_skip_launcher=true --audio_mute=true --gw2_benchmark=tests/replays/master-pacifism-26183625.gw2demo
```

It should end with `Replays: replay ended after N of N ticks, in step throughout` and the score in the replay's name.
