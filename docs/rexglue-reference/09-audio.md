# 09 Audio

No reusable audio implementation was identified in this repository. Audio is entirely ReXGlue's.

Evidence:
- No source file under `src/` references an audio API, device selection, or an audio hook. (A case-insensitive search of `src/`, `tools/` scripts and `CMakeLists.txt` for audio terms returns nothing.)
- First boot (`docs/first-boot.md`, commit `38a1d61`): the SDK audio endpoint opened at 2 channels and 48 kHz and submitted audio; audible output was not verified in that pass. The game's audio assets (`audio/*.baf`, `.dat`) are read from the game folder by the guest.
- Shutdown: patch `0033` explicitly leaves runtime audio threads to their subsystems ("vblank, audio, kernel dispatch are not title threads"), see [13](13-crash-hardening-threading-memory.md).
- Linux: the SDK build requires ALSA, PulseAudio and PipeWire dev packages ([02](02-build-system.md)); audio on Linux was not verified on real hardware (`docs/linux.md`, `UNCERTAIN`).

Related, game-specific and not audio infrastructure: the game's last-seconds tick in Deadline is counted per update, so
high-refresh presentation gates the update at 60 Hz to avoid repeated ticks ([07](07-display-window-frame-pacing.md)).

For another project: nothing to port. Check the new game's first boot log for the audio endpoint line and test audible output manually.
