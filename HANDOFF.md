# HANDOFF: score review, THE COUNCIL, linked PCs (written 2026-11-17 by Light)

Read this first. The user ran low on tokens mid-build; everything below is the true state.

## Where the work lives

- **Worktree:** `F:\CODING FOLDER\geometry-wars-2\release-1.5`, branch **`verified-scores`**, branched from 1.4.2 (`9c49d4a`).
  The user asked to keep the arcade spectating work out of this release: it is **uncommitted** in `source\` on
  `arcade-spectate` (live_stream, ship_choice, demo.cpp/frame_limiter/replays_menu edits). Don't touch it.
- Build inputs are **junctions** into `source\` (git-ignored): `game`, `generated`, `out`, `tools\rexglue`. The launcher
  submodule was initialised here. `.\tools\build.ps1` builds `release-1.5\build\` (baseline 1.4.2 built cleanly).
  The user's real save is in `source\build-release` — never use it for tests.
- Commits (local, **not pushed**): `12b60bb` bloom, `c599515` score review, `f7f4735` linked PCs / Pilot / race.
  The working tree is clean apart from this file; the server README is current.

## Live on Cloudflare (gw2-pc-plus-leaderboards.gw2pcplus.workers.dev)

- **Score review:** a score above its board's limit waits for a reviewer. Limits (in D1 `settings`, editable in the
  panel): Deadline 332M, Evolved 4.6B, Sequence 400M, Waves 212M, Pacifism 23.5B, King 92M (XBLA records),
  Master Deadline 100M, Master Evolved 4.6B, Master Pacifism 5B. Queue cap 25 runs / 100 MB replays.
- **Top 10 needs proof:** a kept replay or a reviewer's approval (`scores.verified`). On deploy day three scores had no
  replay and dropped to 11th: the user's Evolved 64M, Lajin CE (Sequence), thefifthexotic (King). The user was asked
  whether to mark them verified — **no answer yet**.
- **Panel:** `/admin`. Keys: `server\leaderboard\.reviewer-keys.txt` (git-ignored): Reviewer 1/2/3. The user hands
  them out privately. Secret `ADMIN_KEYS` = `Name:key,...`.
- **Discord:** `DISCORD_REVIEW_WEBHOOK` (private: "waiting") and `DISCORD_WEBHOOK` (public #records: "Verified! History
  made!") are **not set live yet**. Tested only in the user's private test server. The user's admins are making the
  real channels; set the secrets when the user sends the real URLs (review first, then records).
- **New endpoints, live but unused until the game update:** `/v1/link/{code,claim,devices,remove}`,
  `/v1/pilot/{get,put}`, `/v1/me`; board answers carry `race: {line, beaten?}` (no race on Master Evolved); the first
  approved run past a line gets a `records` row (THE COUNCIL's plaque). Panel switch: "Tell players when a run isn't
  confirmed" (`settings.tell_rejected`).
- D1 restore points: before review `0000087c-...dad90`, before linking `00000883-...8ae` (full ids in this session).
- Local testing: `npx wrangler dev --port 8799 --persist-to .wrangler-test` (reads `.dev.vars`: test key + webhooks
  pointed at `127.0.0.1:8798`, a stand-in). **Never point `.dev.vars` at real Discord while running smoke.mjs** — it
  posted ~35 test messages into the user's test server once. `--var` mangles URLs; use `.dev.vars`.
  `BASE=http://127.0.0.1:8799 ADMIN_KEY=<.wrangler-test/test-key.txt> node test/smoke.mjs` → 145 checks.
  TaskStop leaves wrangler/workerd running: kill processes whose command line contains
  `release-1.5\server\leaderboard\node_modules`.

## Done in the game (committed)

- Bloom: `draw_resolution_scaled_texture_offsets = false` in `config/geometry_wars_retro_evolved_2.toml` (measured:
  glow within 1-2% of 1x at 2x/3x; 10%/16% narrower before). Credit the community write-up in release notes.

## Next: the game update (approved by the user: "do everything")

1. **Launcher link screen** (was starting): in `src/launcher.cpp` `PageOnlineLeaderboards` (~line 953). Toolkit
   (`third_party/trg-launcher/include/trg/launcher.h`): `Info`, `ButtonRow`, `ConfirmRow`, `Row` + raw ImGui,
   `SetStatus`; async pattern = `online::DeleteMyScores` (thread + callback + `g_online_status`). Add to
   `src/online_leaderboard.*`: `LinkCode`, `ClaimLink`, `LinkedDevices`, `RemoveDevice` (POST JSON with the token).
   UI: "Use this profile on another PC" → code + countdown, copy: "Anyone who types this code becomes you."
   "Link this PC to my profile" → a code box that is NOT a settings key, a confirm saying this PC's online scores
   join that profile and its name becomes the profile's; on success set `kPlayerNameKey` to the returned name
   (else names flip-flop). Linked PCs list ("PC linked 17 Nov", never the hostname) with Remove; the first PC can't
   be removed. Only available while Online leaderboards is on.
2. **Pilot sync** (`src/progression*.cpp`, save `progression.toml`; State at `progression_rules.h:134`, Write/Read TOML).
   Three-way merge (base = last synced, mine, theirs) with **Luna's fixes**: tier XP is derived, never summed
   (`nontier = xp - TierXpOf(bests)`, merge nontier additively, re-add tier XP from merged bests); merge under
   `g_mutex` against the live state; `pilot-pending.toml` before each put (crash safety); synced file stores the profile
   id (link/unlink detection; a removed PC starts fresh, keeps `progression.linked-backup.toml`); bounties merged
   one by one (match goal/board/type/target; gone in base = gone; progress additive; stir the seed); Pilot menu
   rerolls by value, not index; `MarkAnnounced` after a link merge + one line "Pilots joined: level N"; launcher shows
   "not syncing (online leaderboards are off)" rather than a stale time. Put with version CAS; 409 → refetch, merge again.
3. **THE COUNCIL row:** leaderboard rows are 40-byte records at `0x821EA128` (xuid, score i64, rank, name), selected
   `0x821EB5C0`, count `0x821EB5C4`, Watch Replay hook `sub_82094DF0` in `src/live_fix.cpp`. Show a rainbow row above
   #1 when the player's tier on that board is **Neon+** (user's call, overruling Luna's higher gate), with a "Show the
   race" setting (default on). Research first whether one row can be recoloured in the game's renderer (see
   `hud_colour_hooks.cpp`); fallback: our own neon banner. Keep it out of Friends rows, the HUD score-to-beat, ranks and
   paging; pressing it shows a cryptic flavour line per board (tips as prophecy, no puzzles), never a replay. Passing
   the line: "Augh, I've been defeated. The council shall decide on your legitimacy." After approval: permanent plaque
   "THE COUNCIL · bested by NAME" (first name kept).
4. **Game Over** for a run past the line: celebrate; if replay sharing is off say "Share this replay so the council can
   see it" (vital, the user said). Pilot page: "The council couldn't confirm that run. Your next one will be watched
   too." once, only if `/v1/me` reports it.
5. Earlier release items: replays longer than 1 hour (`kMaxTicks` in `src/demo_file.h`; ~320 KB/min raw, gzip 7-14x;
   Cloudflare upload limit 100 MB), the slow-down heartbeat (ticks per real minute, server-side), refresh boards after a
   replay uploads, an in-game "sent for review" message, then G2 (a replay verifier that plays runs back in the game).

Test the game on the second monitor (DISPLAY1, x=1920), muted (`--audio_mute=true`), with PrintWindow captures; never
send keys while the user is at the PC; close only processes you started. Scratchpad capture scripts are gone after the
session (bloom-shot.ps1, glow.ps1, panel-shots.mjs were there).

## Discord post

Written in chat (THE COUNCIL IS WATCHING). It describes the race, plaque and linked PCs, which aren't built yet: post
it when the update ships, or add "coming in the next update".
