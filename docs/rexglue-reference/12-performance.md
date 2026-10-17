# 12 Performance

Chronology (all from the commit history):

| When | Commit | Change |
| --- | --- | --- |
| 2026-10-11 | `ab4631e` | Frame limiter + high-frame-rate present path (Windows) |
| 2026-10-11 | `9e6f05b` | Limiter wait split into `src/frame_wait.cpp` (portable) |
| 2026-10-14 | `a86c63f` | Patched-runtime infrastructure (enables everything below) |
| 2026-10-15 | `4067893` | Patches 0004-0033 and 0035 ported from Hard Corps: Uprising Recompiled |
| 2026-10-15 | `273dc0c` | Game-side hooks (`gpu_fence_wait.cpp`, `crt_native.cpp`), repeated-present exclusion, ThinLTO |

Validation available in the repository: allocator, shutdown, RenderDoc and presenter pacing tests, unit tests for the
limiter. **No benchmark numbers (FPS, CPU %, frame times) are recorded anywhere in the repository**, so quantitative
claims are `UNCERTAIN`; the "why it helps" below comes from the patch descriptions and code. The patches came from a
donor project pinned to the same ReXGlue commit. Before adopting in another game, measure CPU usage and frame times
before/after and run the game's own regression pass.

Class for the whole series: `GENERIC_REXGLUE`, adopt per item; adopt-risk is noted.

## Frame pacing and presentation
See [07](07-display-window-frame-pacing.md) for: the limiter (CPU wait: high-resolution timer then short spin instead of
busy loops), `0025` (wake `WAIT_REG_MEM` from the vblank instead of a blind 1 ms sleep), `0029` (one present per guest
frame instead of two while ImGui dialogs exist), `0008` vblank thread (sleep until due), repeated-present exclusion.

## SDK CPU-side changes

| Patch | Before / problem | Change | Why it helps | Side effects / adopt |
| --- | --- | --- | --- | --- |
| 0004 | Each register write looked up `RegisterFile::GetRegisterInfo` (a switch over the full table) just to decide whether to log an unknown register; one virtual `WriteRegister` per register | Table built once for the unknown-register check; render-state registers `0x2000`-`0x3FFF` copied as a block; type-0 packets of 3+ registers within one block-copyable span applied as one range | Less per-register overhead in the GPU command thread | Registers with side effects (scratch, coherency status, gamma ramp) are below `0x2000` and keep the ordered path. Adopt |
| 0005 | Idle command thread spun through 500 `SwitchToThread` calls before sleeping; `WAIT_REG_MEM` slept the whole guest-supplied value | 32 yields then sleep; `WAIT_REG_MEM` re-polls 16 times and sleeps at most 1 ms; new virtual `PollBackgroundWork` (last virtual) for deferred work | Less CPU while idle; the ring is not blocked after the condition is met | Wait values shortened; adopt |
| 0006 | Draw with clipping disabled and an 8192 scissor (a D3D9 clear) was assumed to cover the whole EDRAM; frame end forgot GPU copy validity for all guest pages; index cache lookups cost more than conversion below a size; `SharedMemory::RequestRanges` allocated a vector per call | `execute_unclipped_draw_vs_on_cpu` on (as in Xenia); `clear_memory_page_state` off; `primitive_processor_cache_min_indices` 4096; invalidation test fixed (compared entry end with range end instead of start); member vector reuse | Fewer needless ownership transfers and re-uploads | `clear_memory_page_state = true` in `settings.toml` restores old behavior if stale data ever appears. Contains a genuine cache invalidation bug fix |
| 0007/0013 | Depth transfers copied stencil one bit at a time (clear plus up to 8 full-rect draws); clears/full-screen passes were transferred before being overwritten | Per-render-target tracking of stencil bits that may be set; draw-overwrite proof for rectangle-list draws that fully replace a rect (shrunk by 1 pixel so it holds at any scale) | Games that never write stencil skip the 8 draws; no useless transfers | Proof is conservative; adopt |
| 0009/0014/0018 | Stencil bits were per target, not per tile; claims were whole target | Bits per EDRAM tile, tile runs; first-row claim in whole tile rows | Smaller transfers | Needs the donor tests; adopt with care |
| 0010/0015/0019 | A resolve dumped host render targets over the whole source rectangle every time | One dirty bit per EDRAM tile; dumps shrunk to the dirty rectangle or skipped | Fewer EDRAM dump dispatches | Guest-visible resolve unchanged |
| 0011 + `src/gpu_fence_wait.cpp` | Game's D3D fence wait (`sub_820A4CE0` with begin/poll/end helpers) spun on the fence word | Command processor wakes waiters on the written address (`WakeByAddressAll` / futex) after each guest memory write; the hook waits on the same physical-view address with the last seen value, bounded to 2 ms; after 8 timeouts with the word already changed, or a failed wait, blocking turns off permanently | CPU sleeps instead of spinning on the fence | Any other caller, a stopped device, unaligned/unmapped word keep the guest's own polling. `GAME_ADAPTATION`: offsets `kBeginSite`, `kPollSite`, `kDeviceFenceOffset` |
| 0008 | Builds other than Release compiled the game with `REXGLUE_ENABLE_PERF_COUNTERS`, so each guest indirect call did a locked add on a shared counter; vblank thread polled `Sleep(1)`; timer queue thread woke ~500 times/s with spin waits; guest critical sections went to the kernel immediately | Per-thread count published every 1024; vblank sleeps until due; condition variable for `QueueTimer`; a critical-section waiter polls up to 256 pauses before registering as waiter | Less shared-cache-line contention and wake-ups | Timer callbacks may run up to one tick late. Only a free lock without waiters is taken, so waiters are not overtaken |
| 0012/0017 | Ending a guest occlusion query ended the submission and waited for all GPU work; submit at every primary buffer end | Results written when the submission completes (guest sees unfinished query meanwhile); no submission ended mid-query; submit-on-primary-end only while fewer than 3 submissions are in flight; redundant `direct_host_resolve` preflight skipped | Fewer GPU stalls | A previous bug, losing the query and turning host queries off for the session, is avoided by not ending submissions mid-query |
| 0016 (W) | `Sleep` lasted up to a timer tick longer | Waitable high-resolution timer from 1 ms up | Accurate sleeps | Waits with a timeout still follow the system timer |
| 0020 (L) | POSIX alertable waits are polled in 1 ms slices; the idle command thread's 5 ms wait woke 5 times | Not alertable | Fewer wake-ups | Guest alertable waits keep slices |
| 0021 (L) | `WaitMultiple` yielded and retried at once when a handle's mutex was held; allocated its lock vector and read the clock each pass | Sleep 100 us before retry, reuse the vector, read the clock only with a timeout | No spinning | Behavior otherwise unchanged |
| 0022-0024 | Resolved render targets were decoded again into textures (compute decode + copy) | Copy render target into the texture when exactly equivalent (single sample, same family, single-level tiled 2D, page-aligned, up to date; half-pixel fill reproduced; red/blue swap tracked) | Skips a decode per resolve | `resolve_copy_into_textures` turns it off. Anything else (MSAA, depth, other formats, offsets, aliasing) takes the old path |
| 0030-0032 | With resolution scaling, a resolve copied into the one texture sampling it still dumped to EDRAM and wrote scaled resolve memory that nothing read | `resolve_defer_writeback` skips the dump and the write and keeps the texture as the only copy; memory is written before a texture load reads it, before another resolve overwrites part of it, or when the resolve does not repeat next frame | Large saving at higher render resolutions | Not used without resolution scaling or with resolve readback. Texture can't be evicted while deferred. Needs careful validation per game |
| 0026/0027 | Integrated GPU chosen on laptops | See [06](06-graphics-vulkan.md) | | |

## Game-side CPU paths (`GAME_ADAPTATION`)
- `src/crt_native.cpp`: hook the guest CRT `memcpy` (`sub_8212EDC0`) and `memset` (`sub_82130E10`) and run host `memmove`/`memset` when one host pointer covers the range and it is not the GPU register window (addresses at/above `0xE0000000` are offset on some hosts; the file's `IsNativeRange` rule). Forward copies that overlap with the destination inside the source keep the guest code (a guest forward copy repeats bytes). The `memcpy` hook stores the return value at `r1 - 8` to mimic the original's frame. Same functions in Hard Corps (same CRT). To port: locate the CRT functions by pattern; verify the ABI registers and any stack side effect.
- ThinLTO for the game and recompiled sources ([02](02-build-system.md)).
- Frame-limiter wait strategy ([07](07-display-window-frame-pacing.md)).

## Shutdown performance
No dedicated optimization; shutdown work is correctness-oriented ([13](13-crash-hardening-threading-memory.md)). The only intentional speedups: `std::_Exit(0)` after relaunch, and process end without teardown if a guest thread cannot be stopped within 2 s.

## What was not done
No CPU/GPU profiling harness is committed. Perf counters exist in the SDK for non-Release builds (see 0008). Memory-use work: none beyond `SharedMemory::RequestRanges` vector reuse.
