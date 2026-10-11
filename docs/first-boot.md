# First Boot

## Result

**FIRST VISIBLE FRAME REACHED** (confirmed in a manual test). The first recognizable screen is the Geometry Wars title art: the neon "GEOMETRY WARS" logo over a starfield. It comes from the recompiled game through the ReXGlue Xenos Direct3D 12 path (NVIDIA GeForce RTX 4070). The starfield moves between captures, so the frame keeps rendering.

After the first frame, the process stayed responsive with no `[error]` or `[critical]` log entries. It was still running 142 s after launch when last checked. Audio and controller detection are recorded below. The later manual test, reported by the user, completed without crashes. Development runs did not exercise gameplay or menus.

## Startup sequence

Observed in the run logs (`build/logs/geometry_wars_retro_evolved_2_NNN.log`):

1. ReXGlue starts, selects the NVIDIA adapter and creates the Direct3D 12 device.
2. The guest memory arena is mapped. The virtual base is chosen at run time (`0x100000000` in the final runs).
3. The function table is initialized for code `82030000`–`8217A1A4` and image `82000000`–`82220000`. 5,972 functions are registered, with 0 duplicates and 0 rejected.
4. SDL input starts. A missing `gamecontrollerdb.txt` is only a warning.
5. Shader storage is initialized for title `584108FF`.
6. The guest main thread starts at the entry point `0x82095FD8` (`xstart`). It creates two worker threads that start at `0x820B54B0`.
7. The guest opens `GAME:\Text-en.loc` (15,919 bytes, read in three requests), then title textures such as `retrobizarre.tga`.
8. The guest registers its graphics interrupt callback (`VdSetGraphicsInterruptCallback`, `SetInterruptCallback(820A4300, 40002080)`).
9. The audio endpoint opens at 2 channels and 48 kHz and submits audio. SDL reports an "Xbox One S Controller" connected.
10. The title screen is rendered.

## Blockers fixed in this pass

1. **Unregistered indirect call at `0x820CEF18`** (`Call to invalid or unregistered function`).
   - Cause: codegen did not register functions reached only through `lis`/`addi` pairs or data pointers. Codegen first failed on three direct branches into this-adjusting thunk targets (`0x821454D0`, `0x8214DF28`, `0x82144BE0`).
   - Fix: three boundary hints in `config/overrides.toml`, then the 33 HIGH entries of `tools/find-missing-functions.py --apply-high` in `config/discovered-functions.toml`. `--verify` passes on the generated output.

2. **Null state object, then a read at guest `0x1A4`** (`Unhandled guest access violation: read of guest 0x000001A4`).
   - Symptom: `sub_820CE338` read `[r31+420]` with `r31 = 0`. Its saved `r31` was zero on restore. The saved slot was overwritten by a store in `sub_820D4DF8` that used a stack-resident object pointer.
   - Cause (inferred, not traced): `sub_820B9CE0` calls the game's `setjmp` (`0x82131CD0`), and `0x82131890` is its matching `longjmp`. The longjmp was not captured on the crash path. Without `setjmp_address`/`longjmp_address` in the manifest, a non-local jump can return into the wrong host frame, and later calls then write into live frames. The fix is confirmed by the outcome, not by tracing the jump.
   - Fix: `setjmp_address = 0x82131CD0` and `longjmp_address = 0x82131890` in the manifest. The SDK supports these keys, and the Aegis Wing and GoldenEye reference projects use them.
   - After this fix the run passes the crash point, and the title screen renders.

## Temporary diagnostics

DEBUG_SAULO diagnostics in the repository (remove during the final code review):

- `src/debug_saulo_fault.cpp`: a vectored exception handler that logs guest-memory access violations to `build/logs/debug_saulo_fault.txt`. It logs the host RIP and RVA, the guest registers, the number of non-zero words in the stack window, and the host call frames inside the executable. It returns `EXCEPTION_CONTINUE_SEARCH` and does not change behaviour.

Analysis aids used during this pass were kept outside the repository: a PowerPC probe for the decrypted image, and a page-level write trace on the guest stack that was removed after use.

## Remaining blockers and known gaps

- Four stubbed imports: `XamShowGamerCardUIForXUID`, `XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamUserCreateStatsEnumerator`. Each logs `STUB` if called. None were called during boot.
- 156 LOW and 53 EH discovery candidates are not applied. Each needs a per-case check before it is added. The hardening pass dropped 10 LOW candidates that existed only because of false lis/addi pairs.
- SEH handlers are not generated (`generate_exception_handlers` is off). Not hit during boot.
- Audio: the endpoint opens and submits audio. Audible output was not verified.
- Input: the controller is detected through SDL. Keyboard detection is not logged, and no input was sent to the game.
- Gameplay, menus and attract mode beyond the title screen were not exercised.
- F11, FPS counter, launcher, installer and packaging are out of scope for this phase.
