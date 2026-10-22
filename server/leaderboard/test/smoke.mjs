// Exercises the leaderboard API against a running Worker:
//   npx wrangler dev           (in another terminal, local D1)
//   node test/smoke.mjs        (BASE=http://127.0.0.1:8787 by default)
import { randomBytes } from "node:crypto";

const BASE = process.env.BASE ?? "http://127.0.0.1:8787";
let failures = 0;
let checks = 0;

function check(ok, what) {
  checks++;
  if (!ok) {
    failures++;
    console.log(`FAIL ${what}`);
  }
}

const token = () => randomBytes(32).toString("hex");
async function post(body) {
  const r = await fetch(`${BASE}/v1/scores`, { method: "POST", body: JSON.stringify(body) });
  return { status: r.status, body: await r.json() };
}
async function board(mode, query = "") {
  const r = await fetch(`${BASE}/v1/boards/${mode}${query}`);
  return { status: r.status, body: await r.json() };
}
const score = (t, name, mode, value, extra = {}) =>
  post({ token: t, name, mode, score: value, input: "mouse", version: "0.6.7", ...extra });

const alice = token();
const bob = token();
const mode = 5; // King

console.log("submission");
let r = await score(alice, "Alice", mode, 1000);
check(r.status === 200 && /^[0-9a-f]{16}$/.test(r.body.id), "a first score is accepted");
const aliceId = r.body.id;
check(!JSON.stringify(r.body).includes(alice), "the token is never echoed");
r = await score(bob, "Bob", mode, 5000);
check(r.status === 200, "a second player is accepted");
const bobId = r.body.id;

console.log("validation");
check((await score(token(), "Bad Name!!", mode, 1)).status === 400, "names outside the font's characters are refused");
check((await score(token(), "x".repeat(16), mode, 1)).status === 400, "names over 15 characters are refused");
check((await score(token(), " Lead", mode, 1)).status === 400, "names with outer spaces are refused");
check((await score("abc", "Eve", mode, 1)).status === 400, "malformed tokens are refused");
check((await score(token(), "Eve", 6, 1)).status === 400, "unknown modes are refused");
check((await score(token(), "Eve", mode, -1)).status === 400, "negative scores are refused");
check((await score(token(), "Eve", mode, 1e12)).status === 400, "absurd scores are refused");
check((await score(token(), "Eve", mode, 1, { input: "aimbot" })).status === 400, "unknown inputs are refused");
check((await score(token(), "Eve", mode, 1, { version: "0.5.0" })).status === 426, "old versions are refused");

console.log("rate limit and best score");
check((await score(alice, "Alice", mode, 9999)).status === 429, "a second submission within 20 s is refused");

console.log("boards");
r = await board(mode);
check(r.status === 200, "the board reads");
// Earlier runs leave their players on the local board, so this run's are matched by id.
const rows = r.body.rows.filter((row) => row.id === aliceId || row.id === bobId);
check(rows.length === 2 && rows[0].id === bobId && rows[1].id === aliceId, "higher scores rank first");
check(r.body.rows.every((row, i) => row.rank === i + 1), "ranks count from 1");
check(!JSON.stringify(r.body).includes(alice), "boards never show tokens");
check((await board(mode, "?from=2&count=1")).body.rows[0]?.rank === 2, "from and count page the board");
check((await board(9)).status === 400, "boards outside 0-5 are refused");

console.log("deletion");
const d = await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: alice }) });
check(d.status === 200 && (await d.json()).deleted === aliceId, "a player can delete themselves with their token");
r = await board(mode);
check(!r.body.rows.some((row) => row.id === aliceId), "deleted players leave the board");

// Leave nothing behind, so the test can run against the live server.
await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: bob }) });
check(!(await board(mode)).body.rows.some((row) => row.id === bobId), "the test removes its players");

console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
