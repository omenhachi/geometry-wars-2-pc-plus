// Geometry Wars 2 PC Plus online leaderboards: a Cloudflare Worker over a D1 database (schema.sql).
//
//   POST   /v1/scores          {token, name, mode, score, input, version}  best score per player and mode
//   GET    /v1/boards/:mode    ?from=1&count=10                            ranked rows
//   DELETE /v1/players         {token}                                     removes the player and every score
//   POST   /v1/replays/:mode   Authorization: Bearer <token>, a .gw2demo   the replay of the player's best run
//   POST   /v1/replays/wanted  {token}                                     the bests the server would take a replay of
//   GET    /v1/replays/:mode/:id                                           that player's replay, as uploaded
//   POST   /v1/link/..., /v1/pilot/..., /v1/me                            linked PCs and Pilot progress (src/link.js)
//   GET    /admin                                                          the reviewers' panel (src/admin.js)
//   *      /v1/admin/...       Authorization: Bearer <admin key>           its API (src/admin.js)
//
// The token is a secret the client generates once and keeps; the public id is derived from it, so seeing a board
// never lets anyone post as someone else. Runs with cheats on are never sent by the client.
//
// Verification: a score above its board's review limit waits for a reviewer instead of reaching the board, with its
// replay asked for as usual, and a board's top 10 holds only scores whose replay is kept. Neither needs a newer game.

import { handleAdmin, adminAsset } from "./admin.js";
import { announcePassed } from "./discord.js";
import { handleLink } from "./link.js";

// 0-5 the game's own modes (Deadline, Evolved, Sequence, Waves, Pacifism, King), then PC Plus's own Master modes:
// 6 Master Deadline, 7 Master Evolved, 8 Master Pacifism (src/boards.h in the game).
export const MODES = 9;
const GAME_MODES = 6;
const NAME = /^[A-Za-z0-9 _.\-]{1,15}$/;
const TOKEN = /^[0-9a-f]{64}$/;
const INPUTS = new Set(["mouse", "keyboard", "controller"]);
// Builds older than this are refused, in case scoring ever changes. Before 1.0.1, watching a replay to its end sent its
// score as the viewer's own, so every board needs 1.0.1 or newer.
const MIN_VERSION = [1, 0, 1];
const MASTER_MIN_VERSION = MIN_VERSION;
// Each board's own minimum where it is newer than the rest: Master Pacifism came with 1.3.0.
const BOARD_MIN_VERSION = { 8: [1, 3, 0] };
function minimumFor(mode, minimum) {
  return BOARD_MIN_VERSION[mode] ?? minimum;
}
// Far above any real score: refused outright. Below it, scores past the review limit wait for a reviewer.
export const MAX_SCORE = [
  100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000,
  100_000_000_000, 100_000_000_000, 100_000_000_000,
];
// One submission per player this often. A one-ship Master run can end within seconds, Game Over screen and all, so
// this stays short enough that no finished run is turned away.
const SUBMIT_INTERVAL_SECONDS = 5;
const MAX_COUNT = 50;
// Scores above these wait for a reviewer (the admin panel can change them, kept in `settings`): the Xbox Live Arcade
// records, and lines of our own for Master Deadline and Master Pacifism. Past one is a moment in history.
export const REVIEW_LIMITS = [
  332_000_000, // Deadline
  4_600_000_000, // Evolved
  400_000_000, // Sequence
  212_000_000, // Waves
  23_500_000_000, // Pacifism
  92_000_000, // King
  100_000_000, // Master Deadline: its own line (Xbox Live had no Master modes)
  4_600_000_000, // Master Evolved
  5_000_000_000, // Master Pacifism: its own line
];
// A board's top places hold only scores with proof: a kept replay, or a reviewer's approval. The rest wait below them.
const REPLAY_REQUIRED_RANKS = 10;
// What the review queue can hold, so made-up tokens can't fill the database or bury the reviewers: runs waiting at
// once, and replay bytes kept for them. Past either, a run is answered as usual but nothing is kept.
const MAX_WAITING = 25;
// Boards with a race to their review line: THE COUNCIL waits at it. Not Master Evolved, which has no line of its own.
const RACE_BOARDS = new Set([0, 1, 2, 3, 4, 5, 6, 8]);
const MAX_REVIEW_BYTES = 100 << 20;
// Replays are kept for the top of each board only, and taken only from builds that ask before sharing them.
const REPLAY_RANKS = 50;
const REPLAY_MIN_VERSION = MIN_VERSION;
const MAX_REPLAY_BYTES = 24 << 20;
export const PIECE_BYTES = 1 << 20; // a D1 row holds at most 2 MB
// The game's replay rules (kReplayRules in src/demo_file.h): a replay with others would play differently.
const REPLAY_RULES = 1;
export const MASTER_NAMES = { 6: "Master Deadline", 7: "Master Evolved", 8: "Master Pacifism" };
// The game mode each Master board is played in.
export const MASTER_BASE = { 6: 0, 7: 1, 8: 4 };

export function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
}

export function error(status, message) {
  return json({ error: message }, status);
}

export async function tokenHash(token) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(token));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// A PC's own public id: the first 16 hex digits of its token's SHA-256.
export async function publicId(token) {
  return (await tokenHash(token)).slice(0, 16);
}

// The profile a PC plays as: the one it was linked to (`devices`), else its own public id. Linking never moves a token,
// so nothing secret leaves a PC, and every player who never linked is exactly as before.
export async function profileId(env, token) {
  const hash = await tokenHash(token);
  const linked = await env.DB.prepare("SELECT player_id FROM devices WHERE token_hash = ?").bind(hash).first();
  return linked?.player_id ?? hash.slice(0, 16);
}

function versionAllowed(version, minimum) {
  const parts = /^(\d+)\.(\d+)\.(\d+)$/.exec(version ?? "");
  if (!parts) return false;
  for (let i = 0; i < 3; i++) {
    const n = Number(parts[i + 1]);
    if (n !== minimum[i]) return n > minimum[i];
  }
  return true;
}

export async function readJson(request) {
  try {
    return await request.json();
  } catch {
    return null;
  }
}

async function submitScore(request, env, ctx) {
  const body = await readJson(request);
  if (!body || typeof body !== "object") return error(400, "expected a JSON body");
  const { token, name, mode, score, input, version } = body;
  if (typeof token !== "string" || !TOKEN.test(token)) return error(400, "bad token");
  if (typeof name !== "string" || !NAME.test(name) || name.trim() !== name) return error(400, "bad name");
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  if (!Number.isSafeInteger(score) || score < 0 || score > MAX_SCORE[mode]) return error(400, "bad score");
  if (!INPUTS.has(input)) return error(400, "bad input");
  if (!versionAllowed(version, minimumFor(mode, mode >= GAME_MODES ? MASTER_MIN_VERSION : MIN_VERSION))) {
    return error(426, "please update Geometry Wars 2 PC Plus");
  }

  const id = await profileId(env, token);
  // Exactly another player's best, with its replay kept: a replay watched to its end, sent as the viewer's own by an
  // older build or a changed one. Real runs never tie to the point.
  const copied = await env.DB.prepare(
    "SELECT 1 FROM replays WHERE mode = ? AND score = ? AND player_id <> ?",
  ).bind(mode, score, id).first();
  if (copied) return error(409, "that score is another player's replay");
  const now = Math.floor(Date.now() / 1000);
  const player = await env.DB.prepare("SELECT last_submit FROM players WHERE id = ?").bind(id).first();
  if (player && now - player.last_submit < SUBMIT_INTERVAL_SECONDS) return error(429, "too many submissions");
  if (score > (await reviewLimits(env))[mode]) {
    return await holdForReview(env, ctx, { id, name, mode, score, input, version, now });
  }

  await env.DB.batch([
    env.DB.prepare(
      "INSERT INTO players (id, name, created, last_submit) VALUES (?1, ?2, ?3, ?3) " +
        "ON CONFLICT (id) DO UPDATE SET name = ?2, last_submit = ?3",
    ).bind(id, name, now),
    env.DB.prepare(
      "INSERT INTO scores (player_id, mode, score, input, version, updated) VALUES (?1, ?2, ?3, ?4, ?5, ?6) " +
        "ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, input = ?4, version = ?5, updated = ?6, verified = 0 " +
        "WHERE excluded.score > scores.score",
    ).bind(id, mode, score, input, version, now),
  ]);
  const best = await env.DB.prepare("SELECT score FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  await pruneReplays(env, mode); // a new best makes the player's old replay stale, and can push another's out
  const replay = best?.score === score && (await replayWanted(env, id, mode)) !== null;
  return json({ id, best: best?.score ?? score, replay });
}

// Each board's review limit: the panel's, when it has set a whole valid list, else REVIEW_LIMITS.
export async function reviewLimits(env) {
  const row = await env.DB.prepare("SELECT value FROM settings WHERE key = 'review_limits'").first();
  try {
    const limits = row ? JSON.parse(row.value) : null;
    if (validLimits(limits)) return limits;
  } catch {}
  return REVIEW_LIMITS;
}

export function validLimits(limits) {
  return Array.isArray(limits) && limits.length === MODES &&
    limits.every((n, mode) => Number.isSafeInteger(n) && n >= 1 && n <= MAX_SCORE[mode]);
}

// A score past its board's limit waits for a reviewer. The answer is still 200, so the game asks nothing more of it than
// usual: `replay` makes it share the run's replay (when the player shares replays), and `review` says it is waiting,
// for a build that can tell the player. The board best stays as it was meanwhile.
async function holdForReview(env, ctx, { id, name, mode, score, input, version, now }) {
  await env.DB.prepare(
    "INSERT INTO players (id, name, created, last_submit) VALUES (?1, ?2, ?3, ?3) " +
      "ON CONFLICT (id) DO UPDATE SET name = ?2, last_submit = ?3",
  ).bind(id, name, now).run();
  const best = await env.DB.prepare("SELECT score FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  const boardBest = best?.score ?? 0;
  if (score <= boardBest) return json({ id, best: boardBest, replay: false });
  const held = await env.DB.prepare(
    "SELECT score, status, replay_size FROM reviews WHERE player_id = ? AND mode = ?",
  ).bind(id, mode).first();
  // One per player and board: the highest waiting one. A rejected one is replaced by any other score, so an honest run
  // after a rejected one is still looked at, but the same score again changes nothing.
  if (held && (held.status === "pending" ? score <= held.score : score === held.score)) {
    const wanted = held.score === score && held.status === "pending" && held.replay_size === null;
    return json({ id, best: boardBest, replay: wanted, review: true });
  }
  const waiting = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM reviews WHERE status = 'pending' AND NOT (player_id = ? AND mode = ?)",
  ).bind(id, mode).first();
  if (waiting.n >= MAX_WAITING) {
    console.warn(`review queue full: a score on board ${mode} was not kept`);
    return json({ id, best: boardBest, replay: false, review: true });
  }
  await env.DB.batch([
    env.DB.prepare("DELETE FROM review_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
    env.DB.prepare(
      "INSERT INTO reviews (player_id, mode, score, input, version, submitted, status) " +
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'pending') ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, " +
        "input = ?4, version = ?5, submitted = ?6, status = 'pending', decided = NULL, decided_by = NULL, " +
        "replay_game = NULL, replay_size = NULL, replay_pieces = NULL, replay_uploaded = NULL",
    ).bind(id, mode, score, input, version, now),
  ]);
  const player = await env.DB.prepare("SELECT hidden FROM players WHERE id = ?").bind(id).first();
  if (!player?.hidden) announcePassed(env, ctx, name, mode, score, (await reviewLimits(env))[mode]);
  return json({ id, best: boardBest, replay: true, review: true });
}

async function race(env, mode) {
  const line = (await reviewLimits(env))[mode];
  const record = await env.DB.prepare("SELECT name, score, approved FROM records WHERE mode = ?").bind(mode).first();
  return { line, ...(record ? { beaten: { name: record.name, score: record.score, at: record.approved } } : {}) };
}

// The order a board shows: by score, except that its top REPLAY_REQUIRED_RANKS places hold only scores with proof (a
// kept replay, or `verified` by a reviewer). A score without waits just below them (in score order) until it has.
export function replayFirst(rows) {
  const shown = [];
  const held = [];
  for (const row of rows) {
    if (shown.length < REPLAY_REQUIRED_RANKS && !row.game && !row.verified) held.push(row);
    else shown.push(row);
    if (shown.length === REPLAY_REQUIRED_RANKS && held.length) shown.push(...held.splice(0));
  }
  return shown.concat(held);
}

async function readBoard(mode, url, env) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const from = Math.max(1, Number.parseInt(url.searchParams.get("from") ?? "1", 10) || 1);
  const count = Math.min(MAX_COUNT, Math.max(1, Number.parseInt(url.searchParams.get("count") ?? "10", 10) || 10));
  // The whole board, for replayFirst's order: boards are a few hundred rows at most.
  const { results: all } = await env.DB.prepare(
    "SELECT p.id, p.name, s.score, s.input, s.verified, r.game_sha256 AS game FROM scores s " +
      "JOIN players p ON p.id = s.player_id " +
      "LEFT JOIN replays r ON r.player_id = s.player_id AND r.mode = s.mode AND r.score = s.score " +
      "WHERE s.mode = ? AND p.hidden = 0 ORDER BY s.score DESC, s.updated ASC",
  ).bind(mode).all();
  const results = replayFirst(all).slice(from - 1, from - 1 + count);
  return json({
    mode,
    total: all.length,
    // THE COUNCIL: the race line, and once someone has been approved past it, who first did ("bested by").
    ...(RACE_BOARDS.has(mode) ? { race: await race(env, mode) } : {}),
    // `replay`: the game package its replay was recorded on, when there is one.
    rows: results.map((row, i) => ({
      rank: from + i,
      id: row.id,
      name: row.name,
      score: row.score,
      input: row.input,
      ...(row.game ? { replay: row.game } : {}),
    })),
  });
}

// The player's rank on a board for their best there, or none: the order readBoard lists.
async function rankOf(env, id, mode) {
  const row = await env.DB.prepare(
    "SELECT s.score, (SELECT COUNT(*) FROM scores o JOIN players q ON q.id = o.player_id WHERE o.mode = s.mode AND " +
      "q.hidden = 0 AND (o.score > s.score OR (o.score = s.score AND o.updated < s.updated))) AS above " +
      "FROM scores s JOIN players p ON p.id = s.player_id WHERE s.player_id = ? AND s.mode = ? AND p.hidden = 0",
  ).bind(id, mode).first();
  return row ? { score: row.score, rank: row.above + 1 } : null;
}

// The player's best on `mode` when the server would take a replay of it: in the top REPLAY_RANKS, none kept yet.
async function replayWanted(env, id, mode) {
  const best = await rankOf(env, id, mode);
  if (!best || best.rank > REPLAY_RANKS) return null;
  const kept = await env.DB.prepare("SELECT score FROM replays WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  return kept && kept.score === best.score ? null : best;
}

// Lets replays go that no longer match their player's best, or whose best fell out of the top. Quietly: no notice.
// A hidden player's replay stays, so the reviewers can still watch it, and unhiding puts it back as it was.
export async function pruneReplays(env, mode) {
  const stale =
    "SELECT r.player_id FROM replays r WHERE r.mode = ?1 AND (NOT EXISTS (SELECT 1 FROM scores s WHERE " +
    "s.player_id = r.player_id AND s.mode = r.mode AND s.score = r.score) OR (r.player_id NOT IN (SELECT s.player_id " +
    "FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = ?1 AND p.hidden = 0 " +
    "ORDER BY s.score DESC, s.updated ASC LIMIT ?2) AND r.player_id NOT IN (SELECT id FROM players WHERE hidden = 1)))";
  await env.DB.batch([
    env.DB.prepare(`DELETE FROM replay_pieces WHERE mode = ?1 AND player_id IN (${stale})`).bind(mode, REPLAY_RANKS),
    env.DB.prepare(`DELETE FROM replays WHERE mode = ?1 AND player_id IN (${stale})`).bind(mode, REPLAY_RANKS),
  ]);
}

// The header of a .gw2demo (src/demo_file.h), or why it is refused. The rest is the game's to check: it reads every
// replay strictly before playing it.
function readDemoHeader(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let at = 0;
  const need = (n) => {
    if (at + n > bytes.length) throw new Error("short");
  };
  const u32 = () => (need(4), (at += 4), view.getUint32(at - 4, true));
  const u64 = () => (need(8), (at += 8), view.getBigUint64(at - 8, true));
  const text = () => {
    need(1);
    const n = bytes[at++];
    need(n);
    at += n;
    return new TextDecoder().decode(bytes.subarray(at - n, at));
  };
  try {
    const magic = [0x47, 0x57, 0x32, 0x44, 0x45, 0x4d, 0x4f, 0x1a];
    need(8);
    if (!magic.every((b, i) => bytes[i] === b)) return { refused: "not a replay" };
    at = 8;
    const format = u32();
    if (format < 1 || format > 3) return { refused: "unknown replay format" };
    if (u32() !== REPLAY_RULES) return { refused: "replay rules differ" };
    text(); // port version
    const game = text();
    text(); // player name: the board's is the one shown
    const mode = u32();
    const score = u64();
    u64(); // recorded at
    const flags = u32();
    const custom = format >= 2 && (flags & 2) !== 0 ? text() : null;
    if (!/^[0-9A-Fa-f]{64}$/.test(game)) return { refused: "bad game hash" };
    return { mode, score, cheats: (flags & 1) !== 0, custom, game: game.toUpperCase() };
  } catch {
    return { refused: "damaged replay" };
  }
}

async function uploadReplay(request, env, mode) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const token = /^Bearer ([0-9a-f]{64})$/.exec(request.headers.get("authorization") ?? "")?.[1];
  if (!token) return error(400, "bad token");
  if (!versionAllowed(request.headers.get("x-gw2-version"), minimumFor(mode, REPLAY_MIN_VERSION))) {
    return error(426, "please update Geometry Wars 2 PC Plus");
  }
  if (Number(request.headers.get("content-length") ?? "0") > MAX_REPLAY_BYTES) return error(413, "replay too large");
  const bytes = new Uint8Array(await request.arrayBuffer());
  if (bytes.length === 0 || bytes.length > MAX_REPLAY_BYTES) return error(413, "replay too large");

  const id = await profileId(env, token);
  // A run waiting for review wants its replay first: the reviewers watch it.
  const review = await reviewWanted(env, id, mode);
  const best = review ? null : await replayWanted(env, id, mode);
  if (!review && !best) return error(409, "no replay wanted for this board");
  const demo = readDemoHeader(bytes);
  if (demo.refused) return error(400, demo.refused);
  if (demo.cheats) return error(400, "a run with cheats on");
  const base = MASTER_BASE[mode] ?? mode;
  if (demo.mode !== base || (MASTER_NAMES[mode] ?? null) !== demo.custom) return error(400, "a replay of another mode");

  const now = Math.floor(Date.now() / 1000);
  const pieces = Math.ceil(bytes.length / PIECE_BYTES);
  if (review && demo.score === BigInt(review.score)) {
    const kept = await env.DB.prepare("SELECT COALESCE(SUM(replay_size), 0) AS n FROM reviews").first();
    if (kept.n + bytes.length > MAX_REVIEW_BYTES) return error(507, "no room for review replays just now");
    await env.DB.batch([
      env.DB.prepare(
        "UPDATE reviews SET replay_game = ?3, replay_size = ?4, replay_pieces = ?5, replay_uploaded = ?6 " +
          "WHERE player_id = ?1 AND mode = ?2",
      ).bind(id, mode, demo.game, bytes.length, pieces, now),
      ...pieceWrites(env, "review_pieces", id, mode, bytes),
    ]);
    return json({ kept: true, review: true, mode, score: review.score, size: bytes.length });
  }
  const wanted = best ?? (await replayWanted(env, id, mode));
  if (!wanted || demo.score !== BigInt(wanted.score)) return error(409, "not the replay of your best");

  await env.DB.batch([
    env.DB.prepare(
      "INSERT INTO replays (player_id, mode, score, game_sha256, size, pieces, uploaded) " +
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, " +
        "game_sha256 = ?4, size = ?5, pieces = ?6, uploaded = ?7",
    ).bind(id, mode, wanted.score, demo.game, bytes.length, pieces, now),
    ...pieceWrites(env, "replay_pieces", id, mode, bytes),
  ]);
  await pruneReplays(env, mode);
  return json({ kept: true, mode, score: wanted.score, size: bytes.length });
}

async function wantedReplays(request, env) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return error(400, "bad token");
  const id = await profileId(env, body.token);
  const wanted = [];
  for (let mode = 0; mode < MODES; mode++) {
    const review = await reviewWanted(env, id, mode);
    if (review) wanted.push({ mode, score: review.score });
    const best = await replayWanted(env, id, mode);
    if (best) wanted.push({ mode, score: best.score });
  }
  return json({ wanted });
}

// The player's run waiting for review on `mode`, while it has no replay yet.
async function reviewWanted(env, id, mode) {
  return await env.DB.prepare(
    "SELECT score FROM reviews WHERE player_id = ? AND mode = ? AND status = 'pending' AND replay_size IS NULL",
  ).bind(id, mode).first();
}

// Inserts of a replay's pieces into `table` (replay_pieces or review_pieces), after clearing the old ones.
export function pieceWrites(env, table, id, mode, bytes) {
  const writes = [env.DB.prepare(`DELETE FROM ${table} WHERE player_id = ? AND mode = ?`).bind(id, mode)];
  for (let i = 0; i * PIECE_BYTES < bytes.length; i++) {
    const piece = bytes.slice(i * PIECE_BYTES, (i + 1) * PIECE_BYTES);
    writes.push(
      env.DB.prepare(`INSERT INTO ${table} (player_id, mode, piece, bytes) VALUES (?, ?, ?, ?)`).bind(
        id, mode, i, piece.buffer,
      ),
    );
  }
  return writes;
}

// A replay put back together from `table`'s pieces, or null when they don't add up to `size`.
export async function readPieces(env, table, id, mode, size, pieces) {
  const { results } = await env.DB.prepare(
    `SELECT bytes FROM ${table} WHERE player_id = ? AND mode = ? ORDER BY piece`,
  ).bind(id, mode).all();
  if (results.length !== pieces) return null;
  const bytes = new Uint8Array(size);
  let at = 0;
  for (const row of results) {
    const piece = new Uint8Array(row.bytes);
    if (at + piece.length > bytes.length) return null;
    bytes.set(piece, at);
    at += piece.length;
  }
  return at === size ? bytes : null;
}

export function replayResponse(bytes, score, game) {
  return new Response(bytes, {
    headers: {
      "content-type": "application/octet-stream",
      "cache-control": "no-store",
      "x-gw2-score": String(score),
      "x-gw2-game": game,
    },
  });
}

async function downloadReplay(env, mode, id) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const replay = await env.DB.prepare(
    "SELECT r.score, r.game_sha256, r.size, r.pieces FROM replays r JOIN players p ON p.id = r.player_id " +
      "WHERE r.player_id = ? AND r.mode = ? AND p.hidden = 0",
  ).bind(id, mode).first();
  if (!replay) return error(404, "no replay");
  const bytes = await readPieces(env, "replay_pieces", id, mode, replay.size, replay.pieces);
  if (!bytes) return error(404, "no replay");
  return replayResponse(bytes, replay.score, replay.game_sha256);
}

async function deletePlayer(request, env) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return error(400, "bad token");
  const id = await profileId(env, body.token);
  await env.DB.batch([
    env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM replays WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM review_pieces WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM reviews WHERE player_id = ?").bind(id),
    // Linked PCs, codes, Pilot progress and plaques too: deleting from any PC deletes the whole profile.
    env.DB.prepare("DELETE FROM devices WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM link_codes WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM pilot WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM records WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM scores WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM players WHERE id = ?").bind(id),
  ]);
  return json({ deleted: id });
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    // Refused writes are logged (Workers Logs, wrangler.toml) so a board's upload troubles can be seen: the path, the
    // answer and, for a score, its mode. Never the token.
    const mode = url.pathname === "/v1/scores" ? (await readJson(request.clone()))?.mode : undefined;
    const response = await route(request, env, url, ctx);
    if (request.method !== "GET" && response.status >= 400) {
      console.warn(`refused ${request.method} ${url.pathname}${mode !== undefined ? ` mode ${mode}` : ""}: ` +
        `${response.status} ${await response.clone().text()}`);
    }
    return response;
  },
};

async function route(request, env, url, ctx) {
  try {
    if (url.pathname === "/v1/scores" && request.method === "POST") return await submitScore(request, env, ctx);
    const board = /^\/v1\/boards\/(\d)$/.exec(url.pathname);
    if (board && request.method === "GET") return await readBoard(Number(board[1]), url, env);
    if (url.pathname === "/v1/players" && request.method === "DELETE") return await deletePlayer(request, env);
    if (url.pathname === "/v1/replays/wanted" && request.method === "POST") return await wantedReplays(request, env);
    const upload = /^\/v1\/replays\/(\d)$/.exec(url.pathname);
    if (upload && request.method === "POST") return await uploadReplay(request, env, Number(upload[1]));
    const replay = /^\/v1\/replays\/(\d)\/([0-9a-f]{16})$/.exec(url.pathname);
    if (replay && request.method === "GET") return await downloadReplay(env, Number(replay[1]), replay[2]);
    if (url.pathname.startsWith("/v1/admin/")) return await handleAdmin(request, env, url, ctx);
    if (url.pathname.startsWith("/v1/link/") || url.pathname.startsWith("/v1/pilot/") || url.pathname === "/v1/me") {
      return await handleLink(request, env, url);
    }
    if (request.method === "GET") {
      const asset = adminAsset(url.pathname);
      if (asset) return asset;
    }
    if (url.pathname === "/" && request.method === "GET") return json({ service: "Geometry Wars 2 PC Plus leaderboards" });
    return error(404, "not found");
  } catch (e) {
    console.error(e);
    return error(500, "server error");
  }
}
