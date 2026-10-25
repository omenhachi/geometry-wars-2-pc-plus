// Geometry Wars 2 PC Plus online leaderboards: a Cloudflare Worker over a D1 database (schema.sql).
//
//   POST   /v1/scores          {token, name, mode, score, input, version}  best score per player and mode
//   GET    /v1/boards/:mode    ?from=1&count=10                            ranked rows
//   DELETE /v1/players         {token}                                     removes the player and every score
//   POST   /v1/replays/:mode   Authorization: Bearer <token>, a .gw2demo   the replay of the player's best run
//   POST   /v1/replays/wanted  {token}                                     the bests the server would take a replay of
//   GET    /v1/replays/:mode/:id                                           that player's replay, as uploaded
//
// The token is a secret the client generates once and keeps; the public id is derived from it, so seeing a board
// never lets anyone post as someone else. Runs with cheats on are never sent by the client.

// 0-5 the game's own modes (Deadline, Evolved, Sequence, Waves, Pacifism, King), 6 Master Deadline and 7 Master
// Evolved, PC Plus's own (src/boards.h in the game).
const MODES = 8;
const GAME_MODES = 6;
const NAME = /^[A-Za-z0-9 _.\-]{1,15}$/;
const TOKEN = /^[0-9a-f]{64}$/;
const INPUTS = new Set(["mouse", "keyboard", "controller"]);
// Builds older than this are refused, in case scoring ever changes. The Master modes came with 0.11.0.
const MIN_VERSION = [0, 6, 7];
const MASTER_MIN_VERSION = [0, 11, 0];
// Far above any real score; tighten per mode as the boards show what is plausible.
const MAX_SCORE = [
  100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000, 100_000_000_000,
  100_000_000_000, 100_000_000_000,
];
// One submission per player this often, which a finished game never comes close to.
const SUBMIT_INTERVAL_SECONDS = 20;
const MAX_COUNT = 50;
// Replays are kept for the top of each board only, and taken only from builds that ask before sharing them.
const REPLAY_RANKS = 50;
const REPLAY_MIN_VERSION = [0, 12, 0];
const MAX_REPLAY_BYTES = 24 << 20;
const PIECE_BYTES = 1 << 20; // a D1 row holds at most 2 MB
// The game's replay rules (kReplayRules in src/demo_file.h): a replay with others would play differently.
const REPLAY_RULES = 1;
const MASTER_NAMES = { 6: "Master Deadline", 7: "Master Evolved" };

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
}

function error(status, message) {
  return json({ error: message }, status);
}

async function publicId(token) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(token));
  return [...new Uint8Array(digest)].slice(0, 8).map((b) => b.toString(16).padStart(2, "0")).join("");
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

async function readJson(request) {
  try {
    return await request.json();
  } catch {
    return null;
  }
}

async function submitScore(request, env) {
  const body = await readJson(request);
  if (!body || typeof body !== "object") return error(400, "expected a JSON body");
  const { token, name, mode, score, input, version } = body;
  if (typeof token !== "string" || !TOKEN.test(token)) return error(400, "bad token");
  if (typeof name !== "string" || !NAME.test(name) || name.trim() !== name) return error(400, "bad name");
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  if (!Number.isSafeInteger(score) || score < 0 || score > MAX_SCORE[mode]) return error(400, "bad score");
  if (!INPUTS.has(input)) return error(400, "bad input");
  if (!versionAllowed(version, mode >= GAME_MODES ? MASTER_MIN_VERSION : MIN_VERSION)) {
    return error(426, "please update Geometry Wars 2 PC Plus");
  }

  const id = await publicId(token);
  const now = Math.floor(Date.now() / 1000);
  const player = await env.DB.prepare("SELECT last_submit FROM players WHERE id = ?").bind(id).first();
  if (player && now - player.last_submit < SUBMIT_INTERVAL_SECONDS) return error(429, "too many submissions");

  await env.DB.batch([
    env.DB.prepare(
      "INSERT INTO players (id, name, created, last_submit) VALUES (?1, ?2, ?3, ?3) " +
        "ON CONFLICT (id) DO UPDATE SET name = ?2, last_submit = ?3",
    ).bind(id, name, now),
    env.DB.prepare(
      "INSERT INTO scores (player_id, mode, score, input, version, updated) VALUES (?1, ?2, ?3, ?4, ?5, ?6) " +
        "ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, input = ?4, version = ?5, updated = ?6 " +
        "WHERE excluded.score > scores.score",
    ).bind(id, mode, score, input, version, now),
  ]);
  const best = await env.DB.prepare("SELECT score FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  await pruneReplays(env, mode); // a new best makes the player's old replay stale, and can push another's out
  const replay = best?.score === score && (await replayWanted(env, id, mode)) !== null;
  return json({ id, best: best?.score ?? score, replay });
}

async function readBoard(mode, url, env) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const from = Math.max(1, Number.parseInt(url.searchParams.get("from") ?? "1", 10) || 1);
  const count = Math.min(MAX_COUNT, Math.max(1, Number.parseInt(url.searchParams.get("count") ?? "10", 10) || 10));
  const { results } = await env.DB.prepare(
    "SELECT p.id, p.name, s.score, s.input, r.game_sha256 AS game FROM scores s JOIN players p ON p.id = s.player_id " +
      "LEFT JOIN replays r ON r.player_id = s.player_id AND r.mode = s.mode AND r.score = s.score " +
      "WHERE s.mode = ? AND p.hidden = 0 ORDER BY s.score DESC, s.updated ASC LIMIT ? OFFSET ?",
  ).bind(mode, count, from - 1).all();
  const total = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = ? AND p.hidden = 0",
  ).bind(mode).first();
  return json({
    mode,
    total: total?.n ?? 0,
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
async function pruneReplays(env, mode) {
  const stale =
    "SELECT r.player_id FROM replays r WHERE r.mode = ?1 AND (NOT EXISTS (SELECT 1 FROM scores s WHERE " +
    "s.player_id = r.player_id AND s.mode = r.mode AND s.score = r.score) OR r.player_id NOT IN (SELECT s.player_id " +
    "FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = ?1 AND p.hidden = 0 " +
    "ORDER BY s.score DESC, s.updated ASC LIMIT ?2))";
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
  if (!versionAllowed(request.headers.get("x-gw2-version"), REPLAY_MIN_VERSION)) {
    return error(426, "please update Geometry Wars 2 PC Plus");
  }
  if (Number(request.headers.get("content-length") ?? "0") > MAX_REPLAY_BYTES) return error(413, "replay too large");
  const bytes = new Uint8Array(await request.arrayBuffer());
  if (bytes.length === 0 || bytes.length > MAX_REPLAY_BYTES) return error(413, "replay too large");

  const id = await publicId(token);
  const best = await replayWanted(env, id, mode);
  if (!best) return error(409, "no replay wanted for this board");
  const demo = readDemoHeader(bytes);
  if (demo.refused) return error(400, demo.refused);
  if (demo.cheats) return error(400, "a run with cheats on");
  const base = mode >= GAME_MODES ? mode - GAME_MODES : mode;
  if (demo.mode !== base || (MASTER_NAMES[mode] ?? null) !== demo.custom) return error(400, "a replay of another mode");
  if (demo.score !== BigInt(best.score)) return error(409, "not the replay of your best");

  const now = Math.floor(Date.now() / 1000);
  const pieces = Math.ceil(bytes.length / PIECE_BYTES);
  const writes = [
    env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
    env.DB.prepare(
      "INSERT INTO replays (player_id, mode, score, game_sha256, size, pieces, uploaded) " +
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, " +
        "game_sha256 = ?4, size = ?5, pieces = ?6, uploaded = ?7",
    ).bind(id, mode, best.score, demo.game, bytes.length, pieces, now),
  ];
  for (let i = 0; i < pieces; i++) {
    const piece = bytes.slice(i * PIECE_BYTES, (i + 1) * PIECE_BYTES);
    writes.push(
      env.DB.prepare("INSERT INTO replay_pieces (player_id, mode, piece, bytes) VALUES (?, ?, ?, ?)").bind(
        id, mode, i, piece.buffer,
      ),
    );
  }
  await env.DB.batch(writes);
  await pruneReplays(env, mode);
  return json({ kept: true, mode, score: best.score, size: bytes.length });
}

async function wantedReplays(request, env) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return error(400, "bad token");
  const id = await publicId(body.token);
  const wanted = [];
  for (let mode = 0; mode < MODES; mode++) {
    const best = await replayWanted(env, id, mode);
    if (best) wanted.push({ mode, score: best.score });
  }
  return json({ wanted });
}

async function downloadReplay(env, mode, id) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const replay = await env.DB.prepare(
    "SELECT r.score, r.game_sha256, r.size, r.pieces FROM replays r JOIN players p ON p.id = r.player_id " +
      "WHERE r.player_id = ? AND r.mode = ? AND p.hidden = 0",
  ).bind(id, mode).first();
  if (!replay) return error(404, "no replay");
  const { results } = await env.DB.prepare(
    "SELECT bytes FROM replay_pieces WHERE player_id = ? AND mode = ? ORDER BY piece",
  ).bind(id, mode).all();
  if (results.length !== replay.pieces) return error(404, "no replay");
  const bytes = new Uint8Array(replay.size);
  let at = 0;
  for (const row of results) {
    const piece = new Uint8Array(row.bytes);
    if (at + piece.length > bytes.length) return error(404, "no replay");
    bytes.set(piece, at);
    at += piece.length;
  }
  if (at !== replay.size) return error(404, "no replay");
  return new Response(bytes, {
    headers: {
      "content-type": "application/octet-stream",
      "cache-control": "no-store",
      "x-gw2-score": String(replay.score),
      "x-gw2-game": replay.game_sha256,
    },
  });
}

async function deletePlayer(request, env) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return error(400, "bad token");
  const id = await publicId(body.token);
  await env.DB.batch([
    env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM replays WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM scores WHERE player_id = ?").bind(id),
    env.DB.prepare("DELETE FROM players WHERE id = ?").bind(id),
  ]);
  return json({ deleted: id });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    try {
      if (url.pathname === "/v1/scores" && request.method === "POST") return await submitScore(request, env);
      const board = /^\/v1\/boards\/(\d)$/.exec(url.pathname);
      if (board && request.method === "GET") return await readBoard(Number(board[1]), url, env);
      if (url.pathname === "/v1/players" && request.method === "DELETE") return await deletePlayer(request, env);
      if (url.pathname === "/v1/replays/wanted" && request.method === "POST") return await wantedReplays(request, env);
      const upload = /^\/v1\/replays\/(\d)$/.exec(url.pathname);
      if (upload && request.method === "POST") return await uploadReplay(request, env, Number(upload[1]));
      const replay = /^\/v1\/replays\/(\d)\/([0-9a-f]{16})$/.exec(url.pathname);
      if (replay && request.method === "GET") return await downloadReplay(env, Number(replay[1]), replay[2]);
      if (url.pathname === "/" && request.method === "GET") return json({ service: "Geometry Wars 2 PC Plus leaderboards" });
      return error(404, "not found");
    } catch (e) {
      console.error(e);
      return error(500, "server error");
    }
  },
};
