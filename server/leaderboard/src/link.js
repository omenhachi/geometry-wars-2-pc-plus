// Linked PCs and Pilot progress: one profile on every PC. Each PC keeps its own token; linking only records that a PC
// plays as another profile (the devices table), so nothing secret ever leaves a PC and a linked PC can be removed.
//
//   POST /v1/link/code     {token}                  a code another PC can claim, for 10 minutes
//   POST /v1/link/claim    {token, code}            this PC joins the code's profile; its online bests come along
//   POST /v1/link/devices  {token}                  the profile's linked PCs
//   POST /v1/link/remove   {token, device}          a linked PC goes back to a fresh profile of its own
//   POST /v1/pilot/get     {token}                  the profile's Pilot progress: {profile, version, save}
//   POST /v1/pilot/put     {token, version, save}   a new save, if `version` is still the latest (else 409)
//   POST /v1/me            {token}                  the player's own runs waiting for the council, and their fate
//
// The Pilot save is the game's own progression.toml text. The server never reads it: the game merges it with its own
// copy and checks it strictly before using it.

import { json, error, readJson, tokenHash, profileId, pruneReplays } from "./index.js";

const TOKEN = /^[0-9a-f]{64}$/;
// No 0/O, 1/I/L or U: easy to read out and type. 7 characters: 30^7, about 22 billion codes.
const ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ";
const CODE_LENGTH = 7;
const CODE_SECONDS = 10 * 60;
// Wrong codes across the whole server in one minute before claims wait for the next: guessing stays hopeless (a few
// thousand tries against 22 billion codes in a code's 10 minutes), and no IP address is ever kept.
const MAX_FAILURES_PER_MINUTE = 300;
const MAX_SAVE = 64 * 1024;

function newCode() {
  // 256 isn't a multiple of 30: bytes from 240 up are dropped, so every character is equally likely.
  let code = "";
  while (code.length < CODE_LENGTH) {
    for (const b of crypto.getRandomValues(new Uint8Array(16))) {
      if (b < 240 && code.length < CODE_LENGTH) code += ALPHABET[b % 30];
    }
  }
  return code;
}

// "7kq-29fx", "7KQ29FX " and so on: the code's characters only, upper case.
function tidyCode(text) {
  return typeof text === "string" ? text.toUpperCase().replace(/[^0-9A-Z]/g, "") : "";
}

async function tokenOf(request) {
  const body = await readJson(request);
  if (!body || typeof body.token !== "string" || !TOKEN.test(body.token)) return { body, bad: error(400, "bad token") };
  return { body, token: body.token };
}

export async function handleLink(request, env, url) {
  if (request.method !== "POST") return error(404, "not found");
  const { body, token, bad } = await tokenOf(request);
  if (bad) return bad;
  switch (url.pathname) {
    case "/v1/link/code": return await makeCode(env, token);
    case "/v1/link/claim": return await claim(env, token, body.code);
    case "/v1/link/devices": return await devices(env, token);
    case "/v1/link/remove": return await remove(env, token, body.device);
    case "/v1/pilot/get": return await pilotGet(env, token);
    case "/v1/pilot/put": return await pilotPut(env, token, body.version, body.save);
    case "/v1/me": return await me(env, token);
  }
  return error(404, "not found");
}

async function makeCode(env, token) {
  const id = await profileId(env, token);
  const player = await env.DB.prepare("SELECT hidden FROM players WHERE id = ?").bind(id).first();
  if (!player) return error(409, "set a score online first, so there is a profile to share");
  if (player.hidden) return error(403, "this profile can't be shared");
  const code = newCode();
  const expires = Math.floor(Date.now() / 1000) + CODE_SECONDS;
  await env.DB.prepare(
    "INSERT INTO link_codes (player_id, code_hash, expires) VALUES (?1, ?2, ?3) " +
      "ON CONFLICT (player_id) DO UPDATE SET code_hash = ?2, expires = ?3",
  ).bind(id, await tokenHash(code), expires).run();
  return json({ code: `${code.slice(0, 3)}-${code.slice(3)}`, expires });
}

async function claim(env, token, text) {
  const now = Math.floor(Date.now() / 1000);
  const minute = Math.floor(now / 60);
  const failures = await env.DB.prepare("SELECT n FROM link_failures WHERE minute = ?").bind(minute).first();
  if ((failures?.n ?? 0) >= MAX_FAILURES_PER_MINUTE) return error(429, "too many tries just now: wait a minute");

  const code = tidyCode(text);
  const found = code.length === CODE_LENGTH
    ? await env.DB.prepare("SELECT player_id, expires FROM link_codes WHERE code_hash = ?").bind(await tokenHash(code)).first()
    : null;
  if (!found || found.expires < now) {
    await env.DB.batch([
      env.DB.prepare(
        "INSERT INTO link_failures (minute, n) VALUES (?, 1) ON CONFLICT (minute) DO UPDATE SET n = n + 1",
      ).bind(minute),
      env.DB.prepare("DELETE FROM link_failures WHERE minute < ?").bind(minute - 5),
    ]);
    return error(404, "that code isn't right, or it has run out: get a new one on the other PC");
  }

  const target = found.player_id;
  const hash = await tokenHash(token);
  const mine = await profileId(env, token);
  if (mine === target) return error(409, "this PC already plays as that profile");
  const linkedAlready = await env.DB.prepare("SELECT 1 FROM devices WHERE token_hash = ?").bind(hash).first();
  if (linkedAlready) return error(409, "this PC is linked to another profile: remove it from that one first");
  const othersLinked = await env.DB.prepare("SELECT 1 FROM devices WHERE player_id = ?").bind(mine).first();
  if (othersLinked) return error(409, "other PCs play as this PC's profile: link them to the new one instead");
  const { results: people } = await env.DB.prepare("SELECT id, name, hidden FROM players WHERE id IN (?, ?)")
    .bind(mine, target).all();
  const targetPlayer = people.find((p) => p.id === target);
  if (!targetPlayer || people.some((p) => p.hidden)) return error(403, "these profiles can't be joined");
  const waiting = await env.DB.prepare(
    "SELECT 1 FROM reviews WHERE player_id IN (?, ?) AND status = 'pending'",
  ).bind(mine, target).first();
  if (waiting) return error(409, "a run is waiting for the council: try again once it has decided");

  // This PC's online bests join the profile where they're higher, each with its replay and verified mark.
  const { results: theirs } = await env.DB.prepare("SELECT * FROM scores WHERE player_id = ?").bind(target).all();
  const { results: ours } = await env.DB.prepare("SELECT * FROM scores WHERE player_id = ?").bind(mine).all();
  const writes = [];
  const moved = [];
  for (const s of ours) {
    const there = theirs.find((t) => t.mode === s.mode);
    if (there && there.score >= s.score) continue;
    moved.push(s.mode);
    writes.push(
      env.DB.prepare(
        "INSERT INTO scores (player_id, mode, score, input, version, updated, verified) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) " +
          "ON CONFLICT (player_id, mode) DO UPDATE SET score = ?3, input = ?4, version = ?5, updated = ?6, verified = ?7",
      ).bind(target, s.mode, s.score, s.input, s.version, s.updated, s.verified),
      env.DB.prepare("DELETE FROM replay_pieces WHERE player_id = ? AND mode = ?").bind(target, s.mode),
      env.DB.prepare("DELETE FROM replays WHERE player_id = ? AND mode = ?").bind(target, s.mode),
      env.DB.prepare("UPDATE replay_pieces SET player_id = ? WHERE player_id = ? AND mode = ?").bind(target, mine, s.mode),
      env.DB.prepare("UPDATE replays SET player_id = ? WHERE player_id = ? AND mode = ?").bind(target, mine, s.mode),
    );
  }
  // Then this PC's old profile goes: its other replays, rejected runs, Pilot copy (the game adds this PC's progress to
  // the profile's itself), code and player row.
  for (const table of ["replay_pieces", "replays", "review_pieces", "reviews", "scores", "pilot", "link_codes"]) {
    writes.push(env.DB.prepare(`DELETE FROM ${table} WHERE player_id = ?`).bind(mine));
  }
  writes.push(
    env.DB.prepare("DELETE FROM players WHERE id = ?").bind(mine),
    env.DB.prepare("DELETE FROM link_codes WHERE player_id = ?").bind(target), // one use
    env.DB.prepare("INSERT INTO devices (token_hash, player_id, device_id, linked) VALUES (?, ?, ?, ?)")
      .bind(hash, target, newDeviceId(), now),
  );
  await env.DB.batch(writes);
  for (const mode of moved) await pruneReplays(env, mode);
  return json({ id: target, name: targetPlayer.name, joined: moved.length });
}

function newDeviceId() {
  return [...crypto.getRandomValues(new Uint8Array(8))].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function devices(env, token) {
  const id = await profileId(env, token);
  const hash = await tokenHash(token);
  const { results } = await env.DB.prepare(
    "SELECT device_id, linked, token_hash = ? AS this FROM devices WHERE player_id = ? ORDER BY linked",
  ).bind(hash, id).all();
  // The profile's own first PC has no row: it is always there, and can't be removed.
  return json({
    profile: id,
    first: hash.slice(0, 16) === id,
    devices: results.map((d) => ({ device: d.device_id, linked: d.linked, this: d.this === 1 })),
  });
}

async function remove(env, token, device) {
  if (typeof device !== "string" || !/^[0-9a-f]{16}$/.test(device)) return error(400, "bad device");
  const id = await profileId(env, token);
  const done = await env.DB.prepare("DELETE FROM devices WHERE device_id = ? AND player_id = ?").bind(device, id).run();
  return done.meta.changes ? json({ removed: device }) : error(404, "no such PC on this profile");
}

async function pilotGet(env, token) {
  const id = await profileId(env, token);
  const row = await env.DB.prepare("SELECT version, save FROM pilot WHERE player_id = ?").bind(id).first();
  return json({ profile: id, version: row?.version ?? 0, save: row?.save ?? null });
}

async function pilotPut(env, token, version, save) {
  if (!Number.isSafeInteger(version) || version < 0) return error(400, "bad version");
  if (typeof save !== "string" || save.length === 0 || save.length > MAX_SAVE) return error(400, "bad save");
  const id = await profileId(env, token);
  const player = await env.DB.prepare("SELECT 1 FROM players WHERE id = ?").bind(id).first();
  if (!player) return error(409, "set a score online first");
  const now = Math.floor(Date.now() / 1000);
  // Written only over the version the game read: two PCs at once can't overwrite each other, the later one merges again.
  const done = version === 0
    ? await env.DB.prepare("INSERT INTO pilot (player_id, version, save, updated) VALUES (?, 1, ?, ?) ON CONFLICT DO NOTHING")
      .bind(id, save, now).run()
    : await env.DB.prepare("UPDATE pilot SET version = version + 1, save = ?, updated = ? WHERE player_id = ? AND version = ?")
      .bind(save, now, id, version).run();
  if (!done.meta.changes) return error(409, "a newer save is there: merge with it and try again");
  return json({ profile: id, version: version + 1 });
}

// The player's own runs past a record line: waiting, or not confirmed (when the reviewers choose to say so).
async function me(env, token) {
  const id = await profileId(env, token);
  const tell = await env.DB.prepare("SELECT value FROM settings WHERE key = 'tell_rejected'").first();
  const { results } = await env.DB.prepare(
    "SELECT mode, score, status, decided FROM reviews WHERE player_id = ? ORDER BY mode",
  ).bind(id).all();
  return json({
    profile: id,
    reviews: results
      .filter((r) => r.status === "pending" || tell?.value !== "0")
      .map((r) => ({ mode: r.mode, score: r.score, status: r.status === "pending" ? "waiting" : "not confirmed" })),
  });
}
