# 16 Reusable game-feature architecture

Game addresses, vtables and offsets here are `GEOMETRY_WARS_SPECIFIC` and must never be copied. The architecture is
`GAME_ADAPTATION`. Long-form: `docs/cheats.md`.

## Hook patterns used throughout

| Pattern | Example | Notes |
| --- | --- | --- |
| Wrap and post-process | `sub_820E89B8` (profile read), `sub_820E88F8` (score write) | `REX_EXTERN(__imp__sub_X); REX_HOOK_RAW(sub_X) { ...; __imp__sub_X(ctx, base); ...post... }`; read args from `ctx.rN` before the call because the callee may change them |
| Conditional replace | `sub_820956B8` etc. (stats enumerators) | Serve locally when the feature is active, else call the original |
| Scale-and-restore | `sub_820653C8` (bloom) | Temporarily modify guest memory around one call so persistent game state is unchanged |
| Filter results | `sub_82094B60` (`XInputGetState`) | Edit the guest buffer after the original returns |
| Fast-path native replacement | `sub_8212EDC0`/`sub_82130E10` (CRT) | Decline to the original when assumptions (ranges, overlap) fail |
| Hook an SDK import | `REX_HOOK_RAW(__imp__VdSwap)` | Call the SDK body by its entry function (`HostToGuestFunction<...>`), not by `REX_EXTERN` (would recurse). Needs the entry's declaration |
| Per-thread hook flag | `t_repeat_present` | Tie two hooks on the same thread |
| Hook a guest callee to attribute a caller | `sub_820A4CE0` hook sites | Hooks at call sites inside one loop (`kBeginSite`, `kPollSite`) so other callers of the helper are unaffected |

Helpers: `src/guest_memory.h` (`LoadBE*/StoreBE*` on guest base; note that for the physical views above `0xA0000000` the host offset must be added, `rex::memory::detail::PhysicalHostOffset`, as `gpu_fence_wait.cpp` and `crt_native.cpp` do).

## Cheats (origin `f9731f9`)

- One declarative list (`kCheats` in `src/cheats.h`): key, label, description. The launcher page, the in-game menu and `ManagedSettings()` are all generated from it, so adding a cheat is one entry plus one hook.
- Saved value = launcher setting (hot cvar); session layer `SessionCheats` records only differences from the saved value (`Effective`, `Set`, `Toggle`), so the menu never writes settings and toggling back follows the saved value again. Hooks call `gw2::cheats::Enabled(cheat)`.
- Safe activation: each hook acts only on objects whose first word equals the expected vtable (`IsPlayer`), reads its parameters before calling the original, and with the cheat Off calls the original unchanged. Invincible returns without calling the original only when the hit would kill (neither the post-bomb grace nor the shield above zero); Infinite Shield holds the timer to 10 s after `Update` returns; Infinite Bombs restores the count if the original took exactly one and never adds bombs.
- Tests (`tests/cheats_tests.cpp`): rules in `cheat_rules.h`, the session override, menu model and close gate, that each function is hooked once, that no hook reads the cvars or the score/achievement code directly, and that F10 is bound once while F2, F7, F11 stay as they were (the test receives `GW2_SRC_DIR`, so it most likely inspects the source text; LIKELY, not read in detail).
- Not changed by cheats: scores, leaderboards, achievements, progression, profile.

## In-game menu (ImGui dialog on the existing drawer)
`src/cheat_menu.cpp`: a `rex::ui::ImGuiDialog` on the drawer ReXApp already owns, nav flags set for keyboard and gamepad (`LauncherGamepad` feeds the gamepad), fonts and accent shared with the launcher (`LauncherRegularFont()`, `LauncherAccent()`), input listener at z-order 1000 so F10/Escape are consumed before the drawer, `g_open` atomic read by guest input threads, `SetActiveCallback(GameInputActive)` to block game input, `CloseGate` so the closing press is not a game press, and an exclusion with the achievements overlay (neither opens over the other). The menu is created only in `StartGame()`, so F10 does nothing over the launcher.

## Achievements overlay
Same dialog technique; rules (size, gate) in the header so they are testable. See [11](11-offline-services-profile-achievements.md).

## Runtime toggles with hot application
Pattern: a cvar read at use time (`REXCVAR_GET`) plus the launcher's PLAY applying changed cvars without restart - frame rate, FPS title, frame counter, bloom, cheats. Restart-only cvars are `Lifecycle::kRequiresRestart` (MSAA) or `kInitOnly` (`gw2_local_live`). Tell the player in the UI which is which.

## Frame counter
`src/frame_counter.cpp`: ImGui dialog drawing `NN FPS  X.X ms` from `GetGuestFrameStats()` once a measured rate exists; F2 toggle not saved. Tiny, copy as is.

## Game state checks
None generic exists in the repository: hooks identify the player via vtable; no "is gameplay active" detector exists except `launcher_open_` (the window host uses it to decide cursor hiding) and the cheat menu's `can_open` predicate. A new game needs its own state check if overlays should not open in certain screens.
