// The reviewers' side: the panel's page (src/admin_page.js) and its API, for scores past a board's review limit.
//
//   GET    /v1/admin/me                                    the reviewer's name: checks a key
//   GET    /v1/admin/reviews                               runs waiting or rejected, the limits, the log
//   GET    /v1/admin/reviews/:mode/:id/replay              a waiting run's replay
//   POST   /v1/admin/reviews/:mode/:id   {action}          approve, reject or undo (a rejection)
//   PUT    /v1/admin/limits              {limits}          one review limit per board
//   GET    /v1/admin/players?q=name                        players by name, with their bests
//   POST   /v1/admin/players/:id         {action}          hide or unhide
//   DELETE /v1/admin/scores/:mode/:id                      removes a best and its replay
//   GET    /v1/admin/replays/:mode/:id                     a board replay, hidden players' too
//
// Every call needs `Authorization: Bearer <key>`, a key from the ADMIN_KEYS secret: comma-separated `Name:key` pairs,
// set with `npx wrangler secret put ADMIN_KEYS`. The name goes in the log. With no ADMIN_KEYS, the panel is shut.

import {
  MODES, MAX_SCORE, json, error, readJson, reviewLimits, validLimits, readPieces, replayResponse,
  pruneReplays,
} from "./index.js";
import { PAGE, STYLE, SCRIPT } from "./admin_page.js";
import { announceVerified } from "./discord.js";

// Spelled out: index.js imports this module, so its constants aren't set yet while this one loads.
const BOARDS = ["Deadline", "Evolved", "Sequence", "Waves", "Pacifism", "King", "Master Deadline", "Master Evolved",
  "Master Pacifism"];
const ID = /^[0-9a-f]{16}$/;
const KEY = /^Bearer ([A-Za-z0-9_-]{32,128})$/;

// 10B, 2.5B, 750M: how the limits read in the log.
function short(n) {
  if (n >= 1e9) return `${+(n / 1e9).toFixed(2)}B`;
  if (n >= 1e6) return `${+(n / 1e6).toFixed(2)}M`;
  return n.toLocaleString("en-GB");
}

async function digest(text) {
  return new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
}

// The reviewer a request's key belongs to, or null. Every key is compared, in constant time, whichever matches.
async function reviewer(request, env) {
  const given = KEY.exec(request.headers.get("authorization") ?? "")?.[1];
  if (!given || !env.ADMIN_KEYS) return null;
  const mine = await digest(given);
  let found = null;
  for (const entry of String(env.ADMIN_KEYS).split(",")) {
    const colon = entry.indexOf(":");
    const name = entry.slice(0, colon).trim();
    const key = entry.slice(colon + 1).trim();
    if (colon < 1 || key.length < 32) continue;
    if (crypto.subtle.timingSafeEqual(await digest(key), mine) && !found) found = name;
  }
  return found;
}

function log(env, admin, action, playerId = null, mode = null, score = null, detail = null) {
  return env.DB.prepare(
    "INSERT INTO admin_log (at, admin, action, player_id, mode, score, detail) VALUES (?, ?, ?, ?, ?, ?, ?)",
  ).bind(Math.floor(Date.now() / 1000), admin, action, playerId, mode, score, detail);
}

function boardAndId(modeText, id) {
  const mode = Number(modeText);
  return Number.isInteger(mode) && mode >= 0 && mode < MODES && ID.test(id) ? { mode, id } : null;
}

export async function handleAdmin(request, env, url, ctx) {
  if (!env.ADMIN_KEYS) return error(503, "the admin panel is not set up (ADMIN_KEYS)");
  const admin = await reviewer(request, env);
  if (!admin) return error(401, "not a reviewer key");
  const path = url.pathname.slice("/v1/admin".length);
  const method = request.method;

  if (path === "/me" && method === "GET") return json({ name: admin, boards: BOARDS });
  if (path === "/reviews" && method === "GET") return await listReviews(env);
  if (path === "/limits" && method === "PUT") return await setLimits(request, env, admin);
  if (path === "/settings" && method === "PUT") return await setSettings(request, env, admin);
  if (path === "/players" && method === "GET") return await findPlayers(env, url.searchParams.get("q") ?? "");

  let m = /^\/reviews\/(\d)\/([0-9a-f]{16})\/replay$/.exec(path);
  if (m && method === "GET") {
    const key = boardAndId(m[1], m[2]);
    return key ? await reviewReplay(env, key) : error(400, "bad board or id");
  }
  m = /^\/reviews\/(\d)\/([0-9a-f]{16})$/.exec(path);
  if (m && method === "POST") {
    const key = boardAndId(m[1], m[2]);
    return key ? await decide(request, env, ctx, admin, key) : error(400, "bad board or id");
  }
  m = /^\/players\/([0-9a-f]{16})$/.exec(path);
  if (m && method === "POST") return await setHidden(request, env, admin, m[1]);
  m = /^\/scores\/(\d)\/([0-9a-f]{16})$/.exec(path);
  if (m && method === "DELETE") {
    const key = boardAndId(m[1], m[2]);
    return key ? await removeScore(env, admin, key) : error(400, "bad board or id");
  }
  m = /^\/replays\/(\d)\/([0-9a-f]{16})$/.exec(path);
  if (m && method === "GET") {
    const key = boardAndId(m[1], m[2]);
    return key ? await boardReplay(env, key) : error(400, "bad board or id");
  }
  return error(404, "not found");
}

async function listReviews(env) {
  const { results } = await env.DB.prepare(
    "SELECT r.player_id AS id, p.name, p.hidden, r.mode, r.score, r.input, r.version, r.submitted, r.status, " +
      "r.decided, r.decided_by, r.replay_game, r.replay_size, r.replay_uploaded, " +
      "(SELECT s.score FROM scores s WHERE s.player_id = r.player_id AND s.mode = r.mode) AS board_best " +
      "FROM reviews r JOIN players p ON p.id = r.player_id " +
      "ORDER BY r.status = 'pending' DESC, r.submitted DESC LIMIT 200",
  ).all();
  // The game packages the kept replays were recorded on, most used first: a replay from another may play out of step.
  const { results: games } = await env.DB.prepare(
    "SELECT game_sha256 AS game, COUNT(*) AS n FROM replays GROUP BY game_sha256 ORDER BY n DESC LIMIT 5",
  ).all();
  const { results: entries } = await env.DB.prepare(
    "SELECT l.at, l.admin, l.action, l.player_id AS id, p.name, l.mode, l.score, l.detail FROM admin_log l " +
      "LEFT JOIN players p ON p.id = l.player_id ORDER BY l.at DESC, l.rowid DESC LIMIT 100",
  ).all();
  const tell = await env.DB.prepare("SELECT value FROM settings WHERE key = 'tell_rejected'").first();
  return json({
    reviews: results, limits: await reviewLimits(env), max: MAX_SCORE, games, log: entries,
    settings: { tell_rejected: tell?.value !== "0" },
  });
}

async function reviewReplay(env, { mode, id }) {
  const r = await env.DB.prepare(
    "SELECT score, replay_game, replay_size, replay_pieces FROM reviews WHERE player_id = ? AND mode = ?",
  ).bind(id, mode).first();
  if (!r || r.replay_size === null) return error(404, "no replay");
  const bytes = await readPieces(env, "review_pieces", id, mode, r.replay_size, r.replay_pieces);
  return bytes ? replayResponse(bytes, r.score, r.replay_game) : error(404, "no replay");
}

async function boardReplay(env, { mode, id }) {
  const r = await env.DB.prepare(
    "SELECT score, game_sha256, size, pieces FROM replays WHERE player_id = ? AND mode = ?",
  ).bind(id, mode).first();
  if (!r) return error(404, "no replay");
  const bytes = await readPieces(env, "replay_pieces", id, mode, r.size, r.pieces);
  return bytes ? replayResponse(bytes, r.score, r.game_sha256) : error(404, "no replay");
}

async function decide(request, env, ctx, admin, { mode, id }) {
  const action = (await readJson(request))?.action;
  const r = await env.DB.prepare("SELECT * FROM reviews WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  if (!r) return error(404, "no such run");
  const now = Math.floor(Date.now() / 1000);

  if (action === "reject") {
    if (r.status !== "pending") return error(409, "it is not waiting");
    await env.DB.batch([
      // Its replay goes: rejected runs keep no storage. Undo brings the run back, and the game sends the replay again.
      env.DB.prepare(
        "UPDATE reviews SET status = 'rejected', decided = ?3, decided_by = ?4, replay_game = NULL, replay_size = NULL, " +
          "replay_pieces = NULL, replay_uploaded = NULL WHERE player_id = ?1 AND mode = ?2",
      ).bind(id, mode, now, admin),
      env.DB.prepare("DELETE FROM review_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
      log(env, admin, "reject", id, mode, r.score),
    ]);
    return json({ ok: true });
  }
  if (action === "undo") {
    if (r.status !== "rejected") return error(409, "it is not rejected");
    await env.DB.batch([
      env.DB.prepare(
        "UPDATE reviews SET status = 'pending', decided = NULL, decided_by = NULL WHERE player_id = ? AND mode = ?",
      ).bind(id, mode),
      log(env, admin, "undo reject", id, mode, r.score),
    ]);
    return json({ ok: true });
  }
  if (action !== "approve") return error(400, "bad action");
  if (r.status !== "pending") return error(409, "it is not waiting");

  // Onto the board as if it had gone straight there: its own time for the tie order, a lower best replaced only, and
  // verified, which counts as proof for the top 10 even without a replay (runs past an hour have none).
  const best = await env.DB.prepare("SELECT score FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  const becomesBest = !best || r.score > best.score;
  const writes = [
    env.DB.prepare(
      "INSERT INTO scores (player_id, mode, score, input, version, updated, verified) VALUES (?1, ?2, ?3, ?4, ?5, ?6, 1) " +
        "ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, input = ?4, version = ?5, updated = ?6, verified = 1 " +
        "WHERE excluded.score > scores.score",
    ).bind(id, mode, r.score, r.input, r.version, r.submitted),
  ];
  // Only a new best's replay is kept: a run below a newer best would replace, then lose, the replay of that best.
  if (r.replay_size !== null && becomesBest) {
    writes.push(
      env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
      env.DB.prepare(
        "INSERT INTO replay_pieces (player_id, mode, piece, bytes) " +
          "SELECT player_id, mode, piece, bytes FROM review_pieces WHERE player_id = ? AND mode = ?",
      ).bind(id, mode),
      env.DB.prepare(
        "INSERT INTO replays (player_id, mode, score, game_sha256, size, pieces, uploaded) " +
          "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, " +
          "game_sha256 = ?4, size = ?5, pieces = ?6, uploaded = ?7",
      ).bind(id, mode, r.score, r.replay_game, r.replay_size, r.replay_pieces, r.replay_uploaded),
    );
  }
  writes.push(
    env.DB.prepare("DELETE FROM review_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
    env.DB.prepare("DELETE FROM reviews WHERE player_id = ? AND mode = ?").bind(id, mode),
    log(env, admin, "approve", id, mode, r.score,
      !becomesBest ? "below their newer best" : r.replay_size === null ? "no replay" : null),
  );
  await env.DB.batch(writes);
  await pruneReplays(env, mode); // the replay goes again if the run isn't the player's best or falls outside the top
  if (becomesBest) {
    const player = await env.DB.prepare("SELECT name, hidden FROM players WHERE id = ?").bind(id).first();
    if (player && !player.hidden) announceVerified(env, ctx, player.name, mode, r.score);
    // THE COUNCIL's plaque goes to the first approved run past the line, for good.
    if (player && r.score > (await reviewLimits(env))[mode]) {
      await env.DB.prepare(
        "INSERT INTO records (mode, player_id, name, score, approved) VALUES (?, ?, ?, ?, ?) ON CONFLICT (mode) DO NOTHING",
      ).bind(mode, id, player.name, r.score, now).run();
    }
  }
  return json({ ok: true });
}

async function setLimits(request, env, admin) {
  const limits = (await readJson(request))?.limits;
  if (!validLimits(limits)) return error(400, `limits: ${MODES} whole numbers, each from 1 to the board's maximum`);
  const before = await reviewLimits(env);
  await env.DB.batch([
    env.DB.prepare(
      "INSERT INTO settings (key, value) VALUES ('review_limits', ?1) ON CONFLICT (key) DO UPDATE SET value = ?1",
    ).bind(JSON.stringify(limits)),
    log(env, admin, "limits", null, null, null,
      limits.map((n, i) => (n === before[i] ? null : `${BOARDS[i]} ${short(before[i])} → ${short(n)}`)).filter(Boolean).join(", ")),
  ]);
  return json({ limits });
}

async function setSettings(request, env, admin) {
  const tell = (await readJson(request))?.tell_rejected;
  if (typeof tell !== "boolean") return error(400, "tell_rejected: true or false");
  await env.DB.batch([
    env.DB.prepare(
      "INSERT INTO settings (key, value) VALUES ('tell_rejected', ?1) ON CONFLICT (key) DO UPDATE SET value = ?1",
    ).bind(tell ? "1" : "0"),
    log(env, admin, tell ? "tell players about unconfirmed runs" : "stay silent about unconfirmed runs"),
  ]);
  return json({ tell_rejected: tell });
}

async function findPlayers(env, q) {
  const text = q.trim();
  const name = text.slice(0, 15);
  const like = `%${name.replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
  const { results: players } = await env.DB.prepare(
    "SELECT id, name, hidden, created, last_submit FROM players WHERE name LIKE ? ESCAPE '\\' OR id = ? " +
      "ORDER BY last_submit DESC LIMIT 25",
  ).bind(like, text.toLowerCase()).all();
  if (players.length === 0) return json({ players: [] });
  const ids = players.map((p) => p.id);
  const marks = ids.map(() => "?").join(",");
  const { results: scores } = await env.DB.prepare(
    "SELECT s.player_id AS id, s.mode, s.score, s.input, s.version, s.updated, r.size AS replay FROM scores s " +
      "LEFT JOIN replays r ON r.player_id = s.player_id AND r.mode = s.mode AND r.score = s.score " +
      `WHERE s.player_id IN (${marks}) ORDER BY s.mode`,
  ).bind(...ids).all();
  return json({ players: players.map((p) => ({ ...p, scores: scores.filter((s) => s.id === p.id) })) });
}

async function setHidden(request, env, admin, id) {
  const action = (await readJson(request))?.action;
  if (action !== "hide" && action !== "unhide") return error(400, "bad action");
  const done = await env.DB.prepare("UPDATE players SET hidden = ? WHERE id = ?").bind(action === "hide" ? 1 : 0, id).run();
  if (!done.meta.changes) return error(404, "no such player");
  await log(env, admin, action, id).run(); // their replays stay (pruneReplays keeps a hidden player's)
  return json({ ok: true });
}

async function removeScore(env, admin, { mode, id }) {
  const s = await env.DB.prepare("SELECT score FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode).first();
  if (!s) return error(404, "no such score");
  await env.DB.batch([
    env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ? AND mode = ?").bind(id, mode),
    env.DB.prepare("DELETE FROM replays WHERE player_id = ? AND mode = ?").bind(id, mode),
    env.DB.prepare("DELETE FROM scores WHERE player_id = ? AND mode = ?").bind(id, mode),
    log(env, admin, "remove score", id, mode, s.score),
  ]);
  await pruneReplays(env, mode);
  return json({ ok: true });
}

// The panel's page, stylesheet and script: same-origin files, so a strict policy can forbid anything else.
const HEADERS = {
  "cache-control": "no-store",
  "x-content-type-options": "nosniff",
  "referrer-policy": "no-referrer",
  "content-security-policy":
    "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; " +
    "frame-ancestors 'none'; base-uri 'none'; form-action 'none'",
};

export function adminAsset(pathname) {
  const files = {
    "/admin": [PAGE, "text/html; charset=utf-8"],
    "/admin/app.css": [STYLE, "text/css; charset=utf-8"],
    "/admin/app.js": [SCRIPT, "text/javascript; charset=utf-8"],
  };
  const file = files[pathname];
  return file ? new Response(file[0], { headers: { ...HEADERS, "content-type": file[1] } }) : null;
}
