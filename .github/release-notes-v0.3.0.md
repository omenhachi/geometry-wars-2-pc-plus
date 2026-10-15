## Geometry Wars: Retro Evolved 2 v0.3.0 - Performance and Stability Update

v0.3.0 is a major performance, frame-pacing and stability update for Windows and the experimental Linux / Steam Deck build. This is an unofficial fan-made port, not affiliated with or endorsed by the game's makers or publishers.

**No game files are included.** You need your own Geometry Wars: Retro Evolved 2 Xbox Live Arcade package.

### Performance

- Improved frame pacing and removed unnecessary repeated presents.
- Reduced unnecessary CPU and GPU waiting.
- Reduced unnecessary GPU and render-target work, especially at higher render resolutions.
- Reduced CPU overhead in several frequently used guest-memory and synchronization paths.
- Release builds are now optimized with ThinLTO.

### Graphics

- **Corrected the Multisampling option.** The old **Off** mode did not turn multisampling off: it used the heavier 4x compatibility path. The launcher now offers **2x MSAA** (the default and recommended choice) and **4x MSAA (Compatibility)**, which you can pick if 2x shows graphics problems on your GPU or driver.
- Your old Multisampling setting is mapped to **2x MSAA**. Changing the option restarts the game once when you press **PLAY**, so the graphics system starts with the new value.

### Windows

- Better automatic selection of the high-performance GPU on hybrid systems. An adapter chosen explicitly still wins.
- Frame-pacing improvements.
- Exit Game and shutdown are more stable.

### Linux / Vulkan

- The Vulkan output pipeline is now reused instead of being rebuilt every frame.
- Improved frame timing and wait scheduling, with less CPU spinning.
- Better automatic selection of a discrete GPU. An explicitly chosen Vulkan device still wins.
- Linux remains **experimental**.

Linux frame pacing and Vulkan performance received major improvements. The previously reported half-speed issue ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)) may be affected by these changes, but it has **not yet been confirmed fixed** on the reporter's Steam Machine. If you can, please try **Deadline** and report whether its 3-minute song and the game timer now finish together, and the FPS shown by the game and by Steam. Feedback on v0.3.0 is welcome.

### Stability

- Input is shut down before the window is destroyed.
- Guest title threads are stopped safely before the runtime is torn down.
- Fixed teardown races that could cause hangs or crashes when exiting the game.

### For people building from source

- Improved ReXGlue build verification and stale-runtime detection.
- Linux builds now need `lld-20` (ThinLTO).

v0.3.0 includes everything from v0.2.3 and earlier: the Linux Vulkan brightness fix (confirmed on a Steam Machine), Bloom control, cheats, portable `user_data`, the launcher improvements, high frame rate support (30 to Unlimited, 60 is the original), the in-game frame counter (**F2**), offline achievements (all 12, 200G, **F7** overlay), local scores and leaderboards, and the local profile and progression.

### Windows download

**GeometryWarsRetroEvolved2-v0.3.0-windows-x64.zip** needs Windows 10 or 11 (64-bit), a DirectX 12 graphics card and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

1. Extract the ZIP anywhere you can write to and run **geometry_wars_retro_evolved_2.exe**. The launcher opens.
2. On the **Play** page click **Install from package...** and pick your own package.
3. Wait for **Ready to play**, then press **PLAY**.

To update from an older version, extract over the old folder. Your `game` and `user_data` folders are kept.

The Windows build is not digitally signed, so SmartScreen may warn on first run: choose **More info**, then **Run anyway**.

### Linux / Steam Deck download

**GeometryWarsRetroEvolved2-v0.3.0-linux-x86_64.AppImage** is a **native** Linux x86_64 build using Vulkan. It is **not** Proton and **not** Wine. It needs glibc 2.35 or newer and a Vulkan driver. Steam Deck support is intended, not confirmed. See [docs/linux.md](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/blob/main/docs/linux.md) for requirements and known limits.

### Required game version

The port is built for this exact version and refuses any other:

| | |
| --- | --- |
| Game | Geometry Wars: Retro Evolved 2, Xbox Live Arcade |
| Title ID | `584108FF` |
| Media ID | `191FF297` |
| Game version | `0.0.1.2` |
| `default.xex` SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |

### Known limitations

- **Linux / Steam Deck remains experimental** and still needs broader testing on real hardware.
- **The Linux half-speed report ([#1](https://github.com/saulob/geometry-wars-retro-evolved-2-recompiled/issues/1)) still awaits confirmation** on the reporter's hardware.
- **Online Xbox Live services are not supported**, and there is no online multiplayer. The profile, progression, scores, leaderboards and achievements are handled locally and offline, on your PC only. The leaderboards show your own scores, never other players.
- Above 60 FPS the last-seconds tick of Deadline may sound more than once.

### Checksums

Each download has a matching `.sha256` file. Compare the SHA-256 of the file you downloaded with its `.sha256` file.

| File | SHA-256 |
| --- | --- |
| `GeometryWarsRetroEvolved2-v0.3.0-windows-x64.zip` | `0592815a2358d019e045aa3aeb7e4f29d57bcd0859566efcb41933a4c6ce0dd4` |
| `GeometryWarsRetroEvolved2-v0.3.0-linux-x86_64.AppImage` | `6828e28cdacfa102bde2bb4d87d7bf155ca61ff244d77e5c8cea0dd7d87cdecf` |
