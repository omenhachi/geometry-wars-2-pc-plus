// Exercises the leaderboard API against a running Worker:
//   npx wrangler dev           (in another terminal, local D1)
//   node test/smoke.mjs        (BASE=http://127.0.0.1:8787 by default)
import { randomBytes } from "node:crypto";

// The oldest build the server takes scores from (MIN_VERSION in src/index.js).
const CURRENT = "1.0.1";
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
  post({ token: t, name, mode, score: value, input: "mouse", version: CURRENT, ...extra });

const alice = token();
const bob = token();
const mode = 5; // King

console.log("master modes");
{
  const master = token();
  let m = await score(master, "Master", 6, 1234, { version: "0.11.0" });
  check(m.status === 426, "a Master Deadline score from a build before 1.0.1 is refused");
  m = await score(master, "Master", 6, 1234);
  check(m.status === 200, "a Master Deadline score from 1.0.1 is accepted");
  const six = await board(6);
  check(six.status === 200 && six.body.rows.some((row) => row.score === 1234), "board 6 lists it");
  check((await board(7)).status === 200, "board 7 (Master Evolved) can be read");
  check((await score(token(), "Eight", 8, 1, {})).status === 400, "board 8 does not exist");
}

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
check((await score(token(), "Eve", 8, 1)).status === 400, "unknown modes are refused");
check((await score(token(), "Eve", mode, -1)).status === 400, "negative scores are refused");
check((await score(token(), "Eve", mode, 1e12)).status === 400, "absurd scores are refused");
check((await score(token(), "Eve", mode, 1, { input: "aimbot" })).status === 400, "unknown inputs are refused");
check((await score(token(), "Eve", mode, 1, { version: "0.5.0" })).status === 426, "old versions are refused");
check((await score(token(), "Eve", mode, 1, { version: "1.0.0" })).status === 426, "1.0.0, which counted watched replays, is refused");

console.log("rate limit and best score");
check((await score(alice, "Alice", mode, 9999)).status === 429, "a second submission within 5 s is refused");

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

console.log("replays");
{
  const GAME = "6D5D9980A1D17DC74B9CCF540CEC16C2E144E7CEE3A74938E78C237B8ACEEB88";
  // A .gw2demo's header (src/demo_file.h), then `size` bytes of body the server never reads.
  const demo = ({ mode, score, flags = 0, custom = null, rules = 1, size = 1000, game = GAME }) => {
    const parts = [Buffer.from("GW2DEMO\x1a", "latin1")];
    const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); parts.push(b); };
    const u64 = (v) => { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(v)); parts.push(b); };
    const text = (t) => parts.push(Buffer.from([t.length]), Buffer.from(t));
    u32(3); u32(rules); text("0.12.0"); text(game); text("Someone"); u32(mode); u64(score); u64(1792891411);
    u32(flags | (custom ? 2 : 0));
    if (custom) { text(custom); text("PC Plus"); }
    parts.push(Buffer.alloc(size, 7));
    return Buffer.concat(parts);
  };
  const upload = (t, board, bytes, version = CURRENT) =>
    fetch(`${BASE}/v1/replays/${board}`, {
      method: "POST",
      headers: { authorization: `Bearer ${t}`, "x-gw2-version": version, "content-type": "application/octet-stream" },
      body: bytes,
    });
  const wanted = async (t) =>
    (await (await fetch(`${BASE}/v1/replays/wanted`, { method: "POST", body: JSON.stringify({ token: t }) })).json()).wanted;
  const players = [];
  const top = 90_000_000_000;

  const carol = token();
  players.push(carol);
  let s = await score(carol, "Carol", 2, top);
  check(s.status === 200 && s.body.replay === true, "a top-50 best asks for its replay");
  const carolId = s.body.id;
  check((await wanted(carol)).some((w) => w.mode === 2 && w.score === top), "wanted lists the board");
  check((await upload(carol, 2, demo({ mode: 2, score: top }), "1.0.0")).status === 426, "builds before 1.0.1 can't upload");
  check((await upload(carol, 2, demo({ mode: 2, score: top - 1 }))).status === 409, "a replay of another score is refused");
  check((await upload(carol, 2, demo({ mode: 3, score: top }))).status === 400, "a replay of another mode is refused");
  check((await upload(carol, 2, demo({ mode: 2, score: top, flags: 1 }))).status === 400, "a run with cheats is refused");
  check((await upload(carol, 2, demo({ mode: 2, score: top, rules: 2 }))).status === 400, "other replay rules are refused");
  check((await upload(carol, 2, Buffer.from("not a replay at all"))).status === 400, "something else is refused");
  check((await upload(carol, 2, demo({ mode: 2, score: top }).subarray(0, 40))).status === 400, "a cut header is refused");
  const big = demo({ mode: 2, score: top, size: 2_500_000 });
  const u = await upload(carol, 2, big);
  check(u.status === 200, "the replay of the best is kept, in pieces");
  check((await upload(carol, 2, big)).status === 409, "a second copy is not wanted");
  check(!(await wanted(carol)).some((w) => w.mode === 2), "wanted no longer lists it");
  let row = (await board(2, "?count=50")).body.rows.find((r) => r.id === carolId);
  check(row?.replay === GAME, "the board row says there is a replay, and its game");
  let g = await fetch(`${BASE}/v1/replays/2/${carolId}`);
  const back = Buffer.from(await g.arrayBuffer());
  check(g.status === 200 && back.equals(big), "the replay downloads byte for byte");
  check((await fetch(`${BASE}/v1/replays/2/0000000000000000`)).status === 404, "no replay is a 404");

  // A Master board takes only that Master mode's runs.
  const dave = token();
  players.push(dave);
  s = await score(dave, "Dave", 6, top);
  const daveId = s.body.id;
  check(s.body.replay === true, "a Master best asks for its replay");
  check((await upload(dave, 6, demo({ mode: 0, score: top }))).status === 400, "an official Deadline run is not Master");
  check((await upload(dave, 6, demo({ mode: 0, score: top, custom: "One Life" }))).status === 400, "nor a custom mode");
  check((await upload(dave, 6, demo({ mode: 0, score: top, custom: "Master Deadline" }))).status === 200, "Master Deadline is");

  // A replay's score sent as someone else's own is refused; the same score with no replay behind it is a tie.
  const eve = token();
  players.push(eve);
  s = await score(eve, "Eve", 6, top);
  check(s.status === 409, "a score that is exactly another player's replayed best is refused");
  check(!(await board(6, "?count=50")).body.rows.some((r) => r.name === "Eve"), "and never reaches the board");
  check((await score(eve, "Eve", 7, top)).status === 200, "the same score on a board without that replay is taken");

  // A new best makes the old replay stale.
  await new Promise((done) => setTimeout(done, 6_000));
  s = await score(carol, "Carol", 2, top + 5);
  check(s.body.replay === true, "a new best asks again");
  row = (await board(2, "?count=50")).body.rows.find((r) => r.id === carolId);
  check(row && !row.replay, "the stale replay is gone from the board");
  check((await fetch(`${BASE}/v1/replays/2/${carolId}`)).status === 404, "and from downloads");
  check((await upload(carol, 2, demo({ mode: 2, score: top + 5 }))).status === 200, "the new best's replay is kept");

  // Fifty better runs push it out of the top 50, quietly.
  for (let i = 0; i < 50; i++) {
    const t = token();
    players.push(t);
    await score(t, `Top ${i}`, 2, top + 100 + i);
  }
  check((await fetch(`${BASE}/v1/replays/2/${carolId}`)).status === 404, "a replay out of the top 50 goes");
  const late = token();
  players.push(late);
  s = await score(late, "Late", 2, 1);
  check(s.body.replay === false, "a best outside the top 50 asks for nothing");

  const del = await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: dave }) });
  check(del.status === 200 && (await fetch(`${BASE}/v1/replays/6/${daveId}`)).status === 404, "deleting removes replays");
  for (const t of players) await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: t }) });
}

console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
