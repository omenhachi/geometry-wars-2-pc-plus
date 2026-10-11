# Geometry Wars: Retro Evolved 2 Package Analysis

Technical metadata only. No game data is stored in this repository. The user-supplied source is read-only; extracted copies live under the Git-ignored `game/` directory.

## Original package

The source is one STFS container in a `<source>/584108FF/000D0000/` folder, where `<source>` is the folder the user supplies.

| Field | Value |
|---|---|
| Path (relative to `<source>`) | `584108FF/000D0000/834312072F4985F9D33D0B9549FFAA32C505FDAD58` |
| Size | 42,266,624 bytes (0xB000 header + 0x2844000 content) |
| SHA-256 | `A13025782A142206425D4284D9A46B10B8446E8270031439692A2C8C7C857D10` |
| SHA-1 | `A6C6088FAF99C8697C0C266BBAFADEC9756549C0` |
| Format | Xbox 360 STFS, magic `LIVE` (Live-signed content package) |
| Metadata version | 2 (`0x348`), header size `0xAD0E`, first hash table at `0xB000` |
| Volume | read-only layout (flags `0x01`), one file-table block at block 0 |
| Content type | `0x000D0000` (Arcade) |
| Content ID | `834312072F4985F9D33D0B9549FFAA32C505FDAD58` (file name); SHA-1 over `0x344`–`0xB000` matches it |
| Title ID | `0x584108FF` (STFS `0x360`, XEX execution info, XDBF resource name) |
| Media ID | `0x191FF297` (STFS `0x354`, XEX execution info) |
| Version / base version | `0.0.1.2` / `0.0.1.2` (`0x00000102`) |
| Display name (EN slot) | `Geometry Wars Evolved²` |
| Signature | LIVE signature not verified |

`ArcadeInfo.xml` reports `TitleInfo ID="1480657151"` (`0x584A46FF`), which differs from the title ID above. The STFS header, the XEX execution info and the XDBF resource name all agree on `0x584108FF`, so that value is used. The XML's ID is recorded here and not used anywhere in the build.

The package was only read. Re-hashed after extraction and codegen, its SHA-256 and size are unchanged.

## Extracted files

Extraction uses `tools/extract_stfs.ps1` (a generic STFS reader that refuses any package whose title ID is not `0x584108FF`). It produced 53 files, 41,848,159 bytes, in `game/extracted/`:

| Location | Files | Bytes | Contents |
|---|---:|---:|---|
| root | 36 | 3,716,260 | `default.xex`, `ArcadeInfo.xml`, `Data.bin`, `Text-{de,en,es,fr,it}.loc`, 12 achievement PNGs, box art, ratings, TGA/PNG UI art |
| `audio/` | 17 | 38,131,899 | `GW3.baf`, `MenuBank.baf`, `Song1Bank.baf` to `Song6Bank.baf`, `GW3_FX.dat`, `Song_1.dat` to `Song_6.dat`, `System.dat`, `UIMusic.dat` |

## default.xex

| Field | Value |
|---|---|
| Size | 704,512 bytes (0xAC000) |
| SHA-256 | `6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88` |
| SHA-1 | `048B852E52A8C776C7AE752141591701B7EA5E69` |
| CRC32 | `5E9B6313` |
| Format | XEX2, module flags `0x00000001` (TITLE, the main executable) |
| Original PE name | `Gw3 Release_Final.exe` |
| Encryption | Normal (`1`), AES-128 with the retail key |
| Compression | Normal (`2`), LZX. Window size `0x8000`, first block 63,488 bytes. ReXGlue's enum is 0 none, 1 basic, 2 normal, 3 delta |
| PE data offset | `0x2000` |
| Security info offset / header size | `0x90` / `0x4B4` |
| Load address / image base | `0x82000000` / `0x82000000` |
| Image size | `0x220000` (2,228,224 bytes), 34 pages of 64 KiB |
| Entry point | `0x82095FD8` (`xstart`) |
| Execution info | media `0x191FF297`, version `0.0.1.2`, title `0x584108FF`, platform 0, executable type 0, disc 0 of 0, savegame ID 0 |
| Region | `0xFFFFFFFF` (all regions) |
| System flags | `0x00000004` |
| Default stack | `0x40000` |
| TLS | 64 slots, no TLS data |
| Resource | XDBF named `584108FF`, at `0x821FB480`, length `0x17465` |
| Import libraries | `xboxkrnl.exe`, `xam.xex` |
| Static libraries | XAPILIB, D3D9, D3DX9, XGRAPHC, XBOXKRNL, XNET, LIBCPMT, XONLINE, XAUD |
| Exports | none |
| Other headers | LAN key (16 bytes), game ratings (64 bytes), Xbox 360 logo (1,736 bytes) |

Page layout by address (from the security info, each page 64 KiB):

| Range | Pages | Use (by address and codegen) |
|---|---:|---|
| `0x82000000`–`0x82030000` | 3 | read-only headers and `.rdata`, `.pdata` |
| `0x82030000`–`0x82180000` | 21 | code (`.text`, codegen range `82030000`–`8217A1A4`) |
| `0x82180000`–`0x82220000` | 10 | read-write data |

## Analysis image

`tools/dump_image` loads the XEX through the ReXGlue runtime in tool mode and writes the decrypted, decompressed image. The image is `game/analysis/default.image.bin`.

| Field | Value |
|---|---|
| Size | 2,228,224 bytes (base `0x82000000`) |
| SHA-256 | `4168A02A62542D32660E63EFDE3244BB423D593C288402F177BF5E6F8C33B901` |

## Imports

Codegen registers 153 import thunks for `xboxkrnl.exe`, `xam.xex` and CRT names. Each one was checked against the ReXGlue 0.10.0 sources (`REX_EXPORT`, `REX_EXPORT_STUB`, `REX_HOOK_RAW`):

| Status | Count | Names |
|---|---:|---|
| implemented | 146 | all others |
| hooked (CRT) | 3 | `DbgPrint`, `sprintf`, `_vsnprintf` |
| stub | 4 | `XamShowGamerCardUIForXUID`, `XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamUserCreateStatsEnumerator` |
| missing | 0 | none |

The stubs log a `STUB` warning if called. None were called during the first boot.
