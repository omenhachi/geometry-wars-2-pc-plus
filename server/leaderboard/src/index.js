// Geometry Wars 2 PC Plus online leaderboards: a Cloudflare Worker over a D1 database (schema.sql).
//
//   POST   /v1/scores          {token, name, mode, score, input, version}  best score per player and mode
//   GET    /v1/boards/:mode    ?from=1&count=10                            ranked rows
//   DELETE /v1/players         {token}                                     removes the player and every score
//
// The token is a secret the client generates once and keeps; the public id is derived from it, so seeing a board
// never lets anyone post as someone else. Runs with cheats on are never sent by the client.

const MODES = 6;
const NAME = /^[A-Za-z0-9 _.\-]{1,15}$/;
const TOKEN = /^[0-9a-f]{64}$/;
const INPUTS = new Set(["mouse", "keyboard", "controller"]);
// Builds older than this are refused, in case scoring ever changes.
const MIN_VERSION = [0, 6, 7];
// Far above any real score; tighten per mode as the boards show what is plausible.
const MAX_SCORE = 100_000_000_000;
// One submission per player this often, which a finished game never comes close to.
const SUBMIT_INTERVAL_SECONDS = 20;
const MAX_COUNT = 50;

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

function versionAllowed(version) {
  const parts = /^(\d+)\.(\d+)\.(\d+)$/.exec(version ?? "");
  if (!parts) return false;
  for (let i = 0; i < 3; i++) {
    const n = Number(parts[i + 1]);
    if (n !== MIN_VERSION[i]) return n > MIN_VERSION[i];
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
  if (!Number.isSafeInteger(score) || score < 0 || score > MAX_SCORE) return error(400, "bad score");
  if (!INPUTS.has(input)) return error(400, "bad input");
  if (!versionAllowed(version)) return error(426, "please update Geometry Wars 2 PC Plus");

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
  return json({ id, best: best?.score ?? score });
}

async function readBoard(mode, url, env) {
  if (!Number.isInteger(mode) || mode < 0 || mode >= MODES) return error(400, "bad mode");
  const from = Math.max(1, Number.parseInt(url.searchParams.get("from") ?? "1", 10) || 1);
  const count = Math.min(MAX_COUNT, Math.max(1, Number.parseInt(url.searchParams.get("count") ?? "10", 10) || 10));
  const { results } = await env.DB.prepare(
    "SELECT p.id, p.name, s.score, s.input FROM scores s JOIN players p ON p.id = s.player_id " +
      "WHERE s.mode = ? AND p.hidden = 0 ORDER BY s.score DESC, s.updated ASC LIMIT ? OFFSET ?",
  ).bind(mode, count, from - 1).all();
  const total = await env.DB.prepare(
    "SELECT COUNT(*) AS n FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = ? AND p.hidden = 0",
  ).bind(mode).first();
  return json({
    mode,
    total: total?.n ?? 0,
    rows: results.map((row, i) => ({ rank: from + i, id: row.id, name: row.name, score: row.score, input: row.input })),
  });
}

async function deletePlayer(request, env) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return error(400, "bad token");
  const id = await publicId(body.token);
  await env.DB.batch([
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
      if (url.pathname === "/" && request.method === "GET") return json({ service: "Geometry Wars 2 PC Plus leaderboards" });
      return error(404, "not found");
    } catch (e) {
      console.error(e);
      return error(500, "server error");
    }
  },
};
