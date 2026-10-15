# 06 Graphics and Vulkan

Separation used here: **correctness fixes** change what is drawn to match the console; **visual options** change the
look on purpose; **performance** is in [12](12-performance.md).

## Correctness fixes

### 0002 Vulkan texture exponent bias (`GENERIC_REXGLUE`, `PLATFORM_LINUX`, CONFIRMED)
- Problem/symptom: with the Vulkan backend the game was far too dark (dim lines, most glow missing). Reported on a Steam Machine (issue #1); Xenia with Vulkan shows the same (game-compatibility #144). D3D12 unaffected.
- Root cause: a texture fetch constant carries `exp_adjust` (result multiplied by 2^exp_adjust) in dword 3 bits 13:18 (`xe_gpu_texture_fetch_t`, `xenos.h`). `SpirvShaderTranslator::ProcessTextureFetchInstruction` (`src/graphics/pipeline/shader/spirv_translator_fetch.cpp`) read those bits from dword 4, where they are part of `lod_bias`, so the bias was lost. The DXBC translator reads dword 3.
- Solution: load dword 3 for the bias; LOD bias (dword 4) is untouched. Patch `patches/rexglue-sdk/0002-spirv-texture-exp-adjust.patch`, commit `378f609` (merged via `fix/vulkan-dark-image-linux`, shipped v0.2.3; the reporter confirmed normal brightness).
- Upstream: known as rexglue-sdk#334/#456; Xenia Canary fixed it the same way (32889f51b). ReXGlue `main` still had it when written.
- Port: copy as-is if the pinned ReXGlue still has the bug; it affects any game that sets exponent bias. Verify by comparing brightness D3D12 vs Vulkan, or in RenderDoc compare the fetched value. Windows build does not need it.
- Build guards that go with it: the plugin must link the runtime, export `rex_gpu_abi_version` and `rex_gpu_create`, export the same strong symbols as the stock plugin (weak symbols depend on C++ headers), and carry only `$ORIGIN` RUNPATH.

### 0035 Cache the swapchain paint pipeline (`GENERIC_REXGLUE`, Linux, CONFIRMED by code)
- Problem: `VulkanPresenter::PaintAndPresentImpl` compared the swapchain format the final paint pipeline was created for but never recorded it, so every paint looked like a format change: wait for the last submission, destroy, recreate the pipeline.
- Solution: record the format after successful creation; reuse until it really changes. File `src/ui/vulkan/vulkan_presenter.cpp`. Reference: ReXGlue issue #507.
- Port: copy; D3D12's presenter has no such cache.

### 0003 / RenderDoc
See [05](05-platform-linux.md).

## GPU/adapter selection (`GENERIC_REXGLUE`)
- Vulkan, `0026` (`src/ui/vulkan/vulkan_provider.cpp`): among equally capable devices prefer a discrete GPU, except when an environment variable already chose one. Cvar `vulkan_device` picks by index.
- D3D12, `0027` (`src/ui/d3d12/d3d12_provider.cpp`): enumerate adapters by high-performance preference, or minimum power when Windows' per-app graphics setting says power saving; `d3d12_adapter` picks by index.
- App side on Windows: `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` exports ([04](04-platform-windows.md)).
- Symptom fixed: hybrid laptops used the integrated GPU (stock took the first DXGI adapter / first equal Vulkan device).

## Multisampling (`GENERIC_REXGLUE` mechanics, project wording `GAME_ADAPTATION`)
- Fact: `native_2x_msaa = false` does not turn multisampling off. Guest render targets with 2x MSAA get host 4x MSAA. Patch `0028` only adds a one-time log in both render target caches: which host sample count 2x targets get, and why (`HostMsaaSamples` in `src/multisampling.h` mirrors the rule).
- Earlier launcher bug (`588e1fd`): the Multisampling option "Off" actually selected the heavier 4x path; old saved `native_2x_msaa = false` values from "Off" kept it active. Fix: the launcher offers `2x MSAA` (default) and `4x MSAA (Compatibility)`, backed by project cvar `gw2_msaa_compatibility` (`kRequiresRestart`); `ApplyMultisampling()` runs in `OnFinalizePaths`, before the GPU starts, writes `native_2x_msaa = !compatibility`, and replaces the legacy saved value (migration of settings). The GPU reads it only at start, so changing it relaunches once on PLAY.
- Whether the game itself ever creates a 2x MSAA render target is `UNCERTAIN`; its back buffer is created without MSAA.
- Tests: `tests/launcher_settings_tests.cpp` (mapping + migration), `tests/restart_state_tests.cpp`.
- Port lesson: do not label a cvar "Off" unless the SDK can really disable the path; read the SDK's render target cache before naming the option.

## Visual options (not correctness)
| Option | Mechanism | Class |
| --- | --- | --- |
| Render resolution 1x-4x | cvar `resolution_scale`; labels assume the pinned 1280x720 guest mode (derived, not measured at run time) | `GENERIC_REXGLUE` |
| Anti-aliasing | `swap_post_effect` none/fxaa/fxaa_extreme | `GENERIC_REXGLUE` |
| Texture filtering | `anisotropic_override` | `GENERIC_REXGLUE` |
| Aspect | `present_letterbox` | `GENERIC_REXGLUE` |
| Bloom | `gw2_bloom` original/reduced/off, see below | `GEOMETRY_WARS_SPECIFIC` mechanism, `GAME_ADAPTATION` pattern |

### Bloom control (`074b563`, `src/bloom_fix.cpp`, `src/bloom_rules.h`)
Hook `sub_820653C8` (the game's bloom composite call). When not Original, multiply three game tuning floats
(`Render.BloomBg`, `Render.BloomFg`, `MapLight.Bloom`; addresses in `kBloomGainAddresses`) by 0.5 or 0, call the
original, then restore them, so the stored tuning values stay the game's. Scale applies per call, hot (no restart).
Anything not a known name means Original. User visual test passed (CONFIRMED by project notes); menus lose their glow too.
Pattern reuse: find the game's tuning variables (named globals if the game keeps a tuning registry), scale around one
call, restore. Do not reuse the addresses.

## Vulkan/D3D12 differences relevant to ports
| Topic | Windows (D3D12) | Linux (Vulkan) |
| --- | --- | --- |
| Texture exp bias | already correct | needs `0002` |
| Tearing/VSync cvar | `d3d12_allow_variable_refresh_rate_and_tearing` | `vulkan_allow_present_mode_immediate` |
| Resolve copy into textures | `CopyTextureRegion` (0023) | `vkCmdCopyImage` (0024, adds the transfer-source usage and the device function entry) |
| Stencil bits in transfers | 0013/0014 | 0018 (per-tile bits without `VK_EXT_shader_stencil_export`) |
| Presenter | no paint-pipeline cache | cached by 0035 |
| Idle command thread wait | common patch 0005 | plus 0020 (the wait is not alertable, so it is not polled in 1 ms slices) |

## Not found in the repository
No custom shader files, no `.xsh` shader cache management beyond ReXGlue's cache folder (`user_data/cache`, moved with the user data root, see [10](10-user-data-settings-migration.md)), and no Vulkan validation-layer handling beyond the packaging guard against shipping layer JSON.
