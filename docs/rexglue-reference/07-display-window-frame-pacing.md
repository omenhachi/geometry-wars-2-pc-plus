# 07 Display, window and frame pacing

Implementation: `src/frame_limiter.{h,cpp}`, `src/frame_wait.cpp`, `src/window_host.{h,cpp}`, `src/frame_counter.{h,cpp}`,
`src/geometry_wars_retro_evolved_2_app.h`, `src/launcher_placement.h`, `src/launcher_platform*.cpp`; SDK patches `0008`,
`0025`, `0029`; tests `tests/frame_limiter_tests.cpp`, `tests/window_host_tests.cpp`, `tests/frame_counter_tests.cpp`,
`tests/presenter_pacing_tests.cpp`, `tests/launcher_placement_tests.cpp`.

## Window modes
- Two modes: Windowed and Borderless Fullscreen (`fullscreen` cvar). No exclusive fullscreen.
- Game F11 (`WindowHost::OnKeyDown`, z-order 1000 above the app's input listener 0 so the key never reaches the game): `ToggleWindowMode` calls `Window::SetFullscreen`, assigns the cvar, and `SaveNow`s it so the next start uses it; the window title is restored after each switch.
- Launcher F11 only toggles the launcher's own fullscreen (`gw2_launcher_fullscreen`) - [03](03-launcher.md). Launcher state vs game display state were deliberately split in `b46a22f`.
- Window size = requested logical size capped to 90% of the display work area (`FitWindowSize`); requested value is preserved in the player file. Defaults in the shipped config: 1280x720 window, `video_mode_width/height` 1280x720 to pin the guest video mode (`GAME_ADAPTATION`; whether another game needs it is a per-title decision), `monitor = 1`.
- Monitor numbering and startup position: see [03](03-launcher.md) and [04](04-platform-windows.md)/[05](05-platform-linux.md). The game starts in the window size it was created with (decided by the process that created the window), not the launcher's placement.
- Resize behavior is the SDK's; the project only restores placement for the launcher. Saved placement is ignored when no display shows it.
- Shutdown restoration: `DetachWindowHost` restores the cursor, clears pointers, and deliberately leaves the listeners installed (the guest can end without the window closing).

## Frame-rate options (`GAME_ADAPTATION`)
Choices 30, 60, 90, 120, 144, 165, 240, Unlimited (`kFrameRates`, cvar `gw2_frame_rate`, hot). 60 is the original game.

Mechanism, evolved over `ab4631e` -> `9e6f05b` -> `273dc0c`:
1. **Present hook**: the game's present callback `sub_820A1FA0` takes the present interval in bits 8-11 of `r3`; for rates above 60 or Unlimited the hook clears them (`kPresentIntervalMask = 0xF00`) so the game swaps at once instead of waiting for the guest vblank (`PresentMode::kImmediate`). The guest vblank stays at the SDK's 60 Hz.
2. **Limiter**: `__imp__VdSwap` hook (`src/window_host.cpp`) calls `PaceGuestFrame()` before the SDK's `VdSwap_entry`. `FrameLimiter::Next` keeps a schedule (target advances by one period; if more than one period late it resyncs from now). Rate 60 is held by the original pacing and Unlimited by nothing (`ProjectLimiterRate`).
3. **Waiting**: `WaitUntil` (`src/frame_wait.cpp`) waits coarsely until 500 us before the deadline, then yield-spins. Windows uses a high-resolution waitable timer; Linux uses `sleep_until`. Before `9e6f05b` this lived in the Windows-only `frame_limiter.cpp`; moving it made the limiter portable.
4. **Time step**: the game reads one float time step, set once at startup to 1/refresh (1/60) at `0x8219D280` (`kGameDeltaAddress`). For rates other than 60, `SyncGameDelta` after each swap overwrites it with the measured frame interval clamped to 1 ms-50 ms (`GameFrameDelta`), so game speed follows the wall clock. At 60 it keeps the original step for a frame on time and gives a frame that missed vblanks a whole multiple of it (`GameStepCounter`: the interval in steps, rounded, at most 3; the rounding remainder carries over, time past the cap is dropped), so a host that cannot hold 60 keeps the game's speed instead of halving it at 30 FPS. Interpolation is not implemented. Game-side frame counts (Deadline's last-seconds tick) are not adjusted, so above 60 FPS the tick may sound more than once (known limit).
5. **Repeated presents excluded** (`273dc0c`): the game's Direct3D layer has a thread that presents the last frame again when the game has not swapped for 30 ms (`sub_820A25D8`). A hook sets a thread-local flag so that swap is not paced, not counted by the frame counter and not used for the time step. Without this, stalls looked like extra frames.
6. **Frame counting**: `VdSwap` hook counts every game swap; `FrameRateSampler` samples about once a second; the same numbers feed the window title (`WindowTitle`, "| FPS: 60.00"), the F2 frame counter and the SDK's F3 overlay (`SetGuestFrameStats`). On a slow system this is the game's swap rate, not frames on screen.

Class: the structure (hook the swap, limiter schedule, wall-clock time step) is `GAME_ADAPTATION`; the schedule, sampler and
placement code are generic and tested. Addresses `0x820A1FA0`, `0x820A25D8`, `0x8219D280` are `GEOMETRY_WARS_SPECIFIC`.
Port requirements: find the present callback, the time-step variable, and the game's idea of a fixed step (many games
accumulate a fixed step; then wall-clock delta is wrong and the rate must be changed differently). `UNCERTAIN` for other
engines.
Validation: `tests/frame_limiter_tests.cpp` (periods, schedule, resync, policy, time step, game steps at 60, wait
reaches deadline), `window_host_tests.cpp` (sampler, title). Real timing validated on Windows only; the game steps at 60
are covered by the unit tests only, and the Linux reporter's slow motion (#1) is not yet confirmed fixed. The Linux
reporter's low frame rate (about 30 FPS) has no known cause.

## SDK pacing changes
### 0025 `WAIT_REG_MEM` woken by vblank
Present makes the GPU wait for a word the game's vblank interrupt handler clears. Past its yields the command
processor polled it in 1 ms sleeps, resuming up to 1 ms after the vblank. The vblank thread now signals an event after
running the interrupt and the wait waits on it for at most the same 1 ms; a signal set after the value was read stays
pending. Other waits keep the poll. Files `src/graphics/command_processor.cpp`, `graphics_system.cpp`.

### 0008 vblank thread
Polled with `Sleep(1)` (a vblank up to ~2 ms late); now sleeps until the next vblank is due; one priority level above
normal on Windows only.

### 0029 UI repaint with guest frames (`GENERIC_REXGLUE`, CONFIRMED by test)
- Problem: while any ImGui dialog exists (this project keeps the frame counter and cursor tick as dialogs) the presenter paints from the UI thread and the drawer asks for a repaint after every frame, so each guest frame was presented as it arrived and again at the next vblank: two presents per guest frame. On Linux without vblank ticks the repaint ran as fast as the GPU allowed.
- Solution: a UI-only repaint waits for the next guest frame (up to 50 ms) and is presented with it; with no guest frames (launcher, stall) it waits for a vertical blank tick on Windows or the display refresh interval elsewhere; resizes, surface changes, OS repaints and dialog changes paint immediately. `Window` gains the last virtual `GetDisplayRefreshRate`. Files `include/rex/ui/presenter.h`, `window.h`, `window_sdl.h`, `src/ui/presenter.cpp`, `window_sdl.cpp`.
- Verify: `tests/presenter_pacing_tests.cpp` (180 guest refreshes at 60 Hz, each painted about once; UI keeps repainting without frames; fails against stock).
- Port: copy; add `GetDisplayRefreshRate` last to keep the ABI.

## Cursor and focus
See [08](08-input-gamepad-mouse.md).

## High refresh rate
Rates up to 240 and Unlimited exist; the presenter's VSync setting can still cap them (VSync Off allows tearing). The
guest vblank is fixed at 60 Hz. Display refresh/monitor change handling is not implemented beyond SDK behavior
(`UNCERTAIN`; no repository code reacts to display changes).

## Pitfalls
- Hooks named like the SDK export: `REX_HOOK_RAW(__imp__VdSwap)` replaces the import thunk, so the original is called through `rex::ppc::HostToGuestFunction<VdSwap_entry>` instead of `REX_EXTERN` (which would re-enter the hook). Declaring `VdSwap_entry` yourself is needed because the SDK has no header for it.
- Guest threads call the hook; samplers use a mutex and push title updates to the UI thread with `CallInUIThread`.
