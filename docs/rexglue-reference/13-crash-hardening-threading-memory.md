# 13 Crash hardening, threading, memory

Ordered by how likely another ReXGlue project hits the same problem. Every item has a regression test or a stated lack.

## A. Physical heap release race (patch 0001) - `GENERIC_REXGLUE`, high priority
See [01](01-rexglue-core.md#0001-physical-heap-release-order). Sequence: reports of a startup crash -> branch
`fix/startup-crash` -> `aa64c6a` (patch, stress test, v0.2.1). Only the allocator race was proven (stock fails
`tests/physical_heap_stress.cpp` on every run; patched passes). That it was *the* cause of the reported crash is
`UNCERTAIN`. Lesson: when a crash is timing-dependent, write a deterministic stress test of the suspected subsystem
first.

## B. Title termination and teardown order (patch 0033 + app `OnShutdown`) - `GENERIC_REXGLUE`
Evolved in two stages in one commit series: patch ported in `4067893`, app side and tests in `d2ebd7e`.

- Failure mode: exit ends in `XamLoaderLaunchTitle`, which runs `KernelState::TerminateTitle` on the main guest thread. It woke the other guest threads, gave them 200 ms to reach a kernel wait and exit, then cleared its flag and dropped the rest from its thread list. A thread asleep in a longer `KeDelayExecutionThread` survived and kept running guest code while `Runtime::Shutdown` destroyed the kernel state and unmapped guest memory -> crash or hang on exit.
- Fix (SDK): a title guest thread counts as running from its start until it exits, except while blocked in a kernel wait (`XObject::Wait`, `WaitMultiple`, `SignalAndWait`, `XThread::Delay`). `XThread::StopTitleThreads` waits until none runs and then closes a gate: a thread leaving its wait afterwards never returns to guest code and stays parked until process exit. Nothing is killed. `TerminateTitle` keeps its flag and the objects of threads that did not exit (their host threads still use them). `Runtime::Shutdown` stops the title's threads (terminating the title first if nothing did) and waits for the thread that terminated it. If a thread is still in guest code after 2 s it can be neither stopped nor killed safely, so the process ends at once without teardown (as on window close). Runtime threads (vblank, audio, kernel dispatch) are not title threads.
- Fix (app, `src/geometry_wars_retro_evolved_2_app.h` `OnShutdown`): shut the input system down before ReXApp destroys the window. Input drivers detach from the window in their destructors, which previously ran in `Runtime::Shutdown`, after the window was gone. The runtime's later `Shutdown` finds nothing left. Also `DetachCheatMenu`, `DetachWindowHost`, `CloseAchievements`.
- Tests (target names, not in default build): `gw2_title_thread_shutdown_tests` (worker in a 500 ms delay, one blocked on a cancelled timer, one running guest code; no guest code after the main thread finishes; a last case with a thread that never reaches a kernel wait must end the process; every case crashes against the stock runtime) and `gw2_shutdown_order_tests` (real UI loop, window and input drivers, quit from another thread, teardown in `ReXApp::OnDestroy` order; a probe driver must be released exactly once, before the window is destroyed; needs a display or `SDL_VIDEO_DRIVER=offscreen`).
- Port: copy 0033 and the `OnShutdown` input step together. Dependency: patch touches `kernel_state.h`/`xthread.h` (ABI rule, see 01).

## C. Relaunch / hard exit - `REUSABLE_INFRASTRUCTURE`
`RestartInNewWindow()` starts a new process with `--gw2_skip_launcher=true`, then the old process flushes the log and calls `std::_Exit(0)` on the UI thread (deferred, so the launcher's placement is saved first). No subsystem teardown is intended: the new process owns the game. On Linux in an AppImage the relaunch targets `$APPIMAGE`, because this process's mount disappears with it. Pitfall: never `_Exit` before logs are flushed (`rex::FlushLogging()`).

## D. Lifetimes deliberately not freed
- `g_services` in `src/live_fix.cpp` ("never freed: hooks may still run while the process exits").
- `CursorSync` and frame counter dialogs created with `new` and retained by the ImGui drawer (`src/window_host.cpp`, `src/frame_counter.cpp`).
- `WindowHost` listeners stay installed after `DetachWindowHost` because the guest can end without the window closing; only pointers are cleared and the cursor restored.
Pattern: shared state used by guest-thread hooks should either be immortal or be gated by a flag that the hook checks first.

## E. Startup failures solved at codegen (`GAME_ADAPTATION`)
- Unregistered indirect call (`0x820CEF18`) and a null-object read after `setjmp`/`longjmp` mishandling: see [01](01-rexglue-core.md#codegen-integration-generic_rexglue-project-data-is-game_adaptation). The temporary guest-fault locator (vectored exception handler writing `debug_saulo_fault.txt`, file `src/debug_saulo_fault.cpp`) existed in `38a1d61` and was removed in `9e6f05b` - do not recreate it as shipped code.
- Profile "unable to load" on first run: [11](11-offline-services-profile-achievements.md).

## F. Rendering/presentation lifetime and correctness items
- Vulkan paint pipeline destroyed and recreated every paint (0035): waited on the last submission each time.
- Occlusion query lost when a submission was ended mid-query, turning host queries off for the session (fixed by 0012/0017).
- `PollBackgroundWork`/deferred writeback: textures kept as the only copy cannot be evicted; memory is written before it is read (0030-0032).
- RenderDoc being loaded by detection (0003).

## G. Failure-path hardening in app code
- `PlayGate`: failed settings save keeps the launcher open and starts nothing; `Cancel()` when the new window cannot be created.
- Install: temp folder + verify + swap, previous folder kept until the swap; cancel/damaged/wrong version leaves the game intact.
- `SelectUserData`: failed migration leaves the old folder in use and retries; `.migrating` staging removed; unwritable root falls back for the run.
- Stats file: temp + flush + rename, corrupt file moved aside, in-memory fallback.
- Game input gating timeouts (`kReleaseTimeoutSeconds = 1.0`).
- GPU fence blocking self-disables after 8 missed wakes or a failed wait (`g_blocking_enabled`).
- `memcpy` hook declines overlapping forward copies and the GPU register window.

## Concurrency conventions in the project's own code
- Hook code runs on guest threads: use mutexes/atomics for shared state (`g_mutex` in `window_host.cpp`, `g_gate_mutex` in `live_fix.cpp`) and `CallInUIThread[Deferred]` for UI work.
- `thread_local` flags for hook-to-hook communication on one thread (`t_repeat_present`, `t_wait`).

## Not found
No race conditions or crashes in the SDL gamepad code, Vulkan device lifetime, or DLL/shared-library unload were fixed in this repository beyond the items above. DLL lifetime is only addressed by build/package checks (the runtime and plugin next to the program must match the patched SDK).
