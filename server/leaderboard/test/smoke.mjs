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
  check((await score(token(), "Nine", 9, 1, {})).status === 400, "board 9 does not exist");
  // Master Pacifism (board 8) came with 1.3.0.
  const pacifist = token();
  check((await score(pacifist, "Pacifist", 8, 4321)).status === 426, "a Master Pacifism score before 1.3.0 is refused");
  check((await score(pacifist, "Pacifist", 8, 4321, { version: "1.3.0" })).status === 200, "a 1.3.0 one is accepted");
  check((await board(8)).body.rows.some((row) => row.score === 4321), "board 8 lists it");
  check((await score(token(), "Pacifist", 8, 1e12, { version: "1.3.0" })).status === 400, "board 8 refuses absurd scores");
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
check((await score(token(), "Eve", 9, 1)).status === 400, "unknown modes are refused");
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
  const top = 300_000_000; // under Sequence's 400 million review limit

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

  // A Master board takes only that Master mode's runs. Master Deadline's review line is 100 million.
  const md = 90_000_000;
  const dave = token();
  players.push(dave);
  s = await score(dave, "Dave", 6, md);
  const daveId = s.body.id;
  check(s.body.replay === true, "a Master best asks for its replay");
  check((await upload(dave, 6, demo({ mode: 0, score: md }))).status === 400, "an official Deadline run is not Master");
  check((await upload(dave, 6, demo({ mode: 0, score: md, custom: "One Life" }))).status === 400, "nor a custom mode");
  check((await upload(dave, 6, demo({ mode: 0, score: md, custom: "Master Deadline" }))).status === 200, "Master Deadline is");

  // Master Pacifism: played in Pacifism (mode 4), not in the mode its board number would suggest, from 1.3.0.
  const frank = token();
  players.push(frank);
  s = await score(frank, "Frank", 8, top, { version: "1.3.0" });
  check(s.status === 200 && s.body.replay === true, "a Master Pacifism best asks for its replay");
  const mp = (extra) => demo({ mode: 4, score: top, custom: "Master Pacifism", ...extra });
  check((await upload(frank, 8, mp({}), "1.2.2")).status === 426, "builds before 1.3.0 can't upload to it");
  check((await upload(frank, 8, mp({ mode: 2 }), "1.3.0")).status === 400, "a Sequence run is not Master Pacifism");
  check((await upload(frank, 8, mp({ custom: "Green Pacifism" }), "1.3.0")).status === 400, "nor a custom mode");
  check((await upload(frank, 8, mp({}), "1.3.0")).status === 200, "Master Pacifism is");

  // A replay's score sent as someone else's own is refused; the same score with no replay behind it is a tie.
  const eve = token();
  players.push(eve);
  s = await score(eve, "Eve", 6, md);
  check(s.status === 409, "a score that is exactly another player's replayed best is refused");
  check(!(await board(6, "?count=50")).body.rows.some((r) => r.name === "Eve"), "and never reaches the board");
  check((await score(eve, "Eve", 7, md)).status === 200, "the same score on a board without that replay is taken");

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

  console.log("review");
  {
    // The reviewer key the local Worker was started with (README: Testing locally).
    const KEY = process.env.ADMIN_KEY ?? "";
    const admin = (path, options = {}, key = KEY) =>
      fetch(`${BASE}/v1/admin${path}`, { ...options, headers: { authorization: `Bearer ${key}` } });
    const reviews = async () => (await (await admin("/reviews")).json()).reviews;
    const pause = () => new Promise((done) => setTimeout(done, 5_500)); // one score per player every 5 s
    check((await admin("/me", {}, "")).status === 401, "the admin API needs a key");
    check((await admin("/me", {}, "f".repeat(64))).status === 401, "and a reviewer's key");
    check(KEY && (await admin("/me")).status === 200, "a reviewer's key opens it (set ADMIN_KEY to run these)");

    const rex = token();
    players.push(rex);
    const big = 2_000_000_000; // over Deadline's 332 million
    s = await score(rex, "Rex", 0, big);
    const rexId = s.body.id;
    check(s.status === 200 && s.body.review === true && s.body.replay === true, "a score over the limit waits, and asks for its replay");
    check(!(await board(0, "?count=50")).body.rows.some((r) => r.id === rexId), "it isn't on the board");
    check((await wanted(rex)).some((w) => w.mode === 0 && w.score === big), "wanted lists it for its replay");
    check((await upload(rex, 0, demo({ mode: 0, score: big - 1 }))).status === 409, "a replay of another score isn't taken for it");
    const rexDemo = demo({ mode: 0, score: big, size: 1_500_000 });
    let u = await upload(rex, 0, rexDemo);
    check(u.status === 200 && (await u.json()).review === true, "its replay is kept with the review, in pieces");
    check(!(await wanted(rex)).some((w) => w.mode === 0), "and isn't asked for again");
    let mine = (await reviews()).find((r) => r.id === rexId && r.mode === 0);
    check(mine?.status === "pending" && mine.replay_size === rexDemo.length && mine.board_best === null, "the panel lists it, with its replay");
    const got = Buffer.from(await (await admin(`/reviews/0/${rexId}/replay`)).arrayBuffer());
    check(got.equals(rexDemo), "the reviewer downloads the replay byte for byte");

    await pause();
    s = await score(rex, "Rex", 0, big - 500);
    check(s.body.review === true && s.body.replay === false, "a lower score over the limit changes nothing");
    check((await reviews()).find((r) => r.id === rexId).score === big, "the highest one stays waiting");

    const act = (mode, id, action) => admin(`/reviews/${mode}/${id}`, { method: "POST", body: JSON.stringify({ action }) });
    check((await act(0, rexId, "reject")).status === 200, "a reviewer rejects it");
    check((await reviews()).find((r) => r.id === rexId).status === "rejected", "it shows as rejected");
    await pause();
    s = await score(rex, "Rex", 0, big);
    check(s.body.review === true && s.body.replay === false, "the same score sent again stays rejected");
    check((await act(0, rexId, "approve")).status === 409, "a rejected run can't be approved without Undo");
    check((await act(0, rexId, "undo")).status === 200, "Undo brings it back");
    check((await reviews()).find((r) => r.id === rexId).status === "pending", "to waiting");
    check((await wanted(rex)).some((w) => w.mode === 0 && w.score === big), "and asks the game for its replay again");
    check((await upload(rex, 0, rexDemo)).status === 200, "which comes back");
    check((await act(0, rexId, "approve")).status === 200, "a reviewer approves it");
    let row0 = (await board(0, "?count=50")).body.rows.find((r) => r.id === rexId);
    check(row0?.score === big && row0.replay === GAME, "it reaches the board with its replay");
    check(!(await reviews()).some((r) => r.id === rexId && r.mode === 0), "and leaves the queue");
    const kept = Buffer.from(await (await fetch(`${BASE}/v1/replays/0/${rexId}`)).arrayBuffer());
    check(kept.equals(rexDemo), "anyone can watch the approved replay");

    // The limits are the panel's to change.
    const limits = (await (await admin("/reviews")).json()).limits;
    check(JSON.stringify(limits) === JSON.stringify([332e6, 4.6e9, 400e6, 212e6, 23.5e9, 92e6, 100e6, 4.6e9, 5e9]),
      "limits: the Xbox Live records, and 100M and 5B for Master Deadline and Master Pacifism");
    const put = (body) => admin("/limits", { method: "PUT", body: JSON.stringify(body) });
    check((await put({ limits: [1, 2] })).status === 400, "a short list of limits is refused");
    check((await put({ limits: limits.map(() => 0) })).status === 400, "limits of 0 are refused");
    check((await put({ limits: limits.map((n, i) => (i === 3 ? 5e9 : n)) })).status === 200, "a reviewer raises Waves to 5 billion");
    const wade = token();
    players.push(wade);
    s = await score(wade, "Wade", 3, 4e9);
    check(s.body.review === undefined && (await board(3, "?count=50")).body.rows.some((r) => r.score === 4e9),
      "a Waves score under the new limit goes straight up");
    check((await put({ limits })).status === 200, "and puts it back");

    // A board's top 10 holds only scores with a replay: ten with one, and a higher one without, below them.
    const tenIds = [];
    for (let i = 0; i < 10; i++) {
      const t = token();
      players.push(t);
      s = await score(t, `Ten ${i}`, 5, 90_000_000 + i);
      tenIds.push(s.body.id);
      await upload(t, 5, demo({ mode: 5, score: 90_000_000 + i }));
    }
    const shy = token();
    players.push(shy);
    s = await score(shy, "Shy", 5, 91_000_000);
    const shyId = s.body.id;
    const king = (await board(5, "?count=12")).body.rows;
    check(king.slice(0, 10).every((r) => r.replay) && king[10]?.id === shyId && king[10].rank === 11,
      "the top score without a replay waits at 11th, below ten with one");
    await upload(shy, 5, demo({ mode: 5, score: 91_000_000 }));
    check((await board(5, "?count=1")).body.rows[0]?.id === shyId, "its replay arriving puts it 1st");

    // Reviewers can remove a best, and hide a player.
    check((await admin(`/scores/5/${shyId}`, { method: "DELETE" })).status === 200, "a reviewer removes a best");
    check(!(await board(5, "?count=50")).body.rows.some((r) => r.id === shyId), "it leaves the board");
    const hide = (id, action) => admin(`/players/${id}`, { method: "POST", body: JSON.stringify({ action }) });
    check((await hide(rexId, "hide")).status === 200 && !(await board(0, "?count=50")).body.rows.some((r) => r.id === rexId),
      "a hidden player leaves every board");
    check((await (await admin(`/replays/0/${rexId}`)).arrayBuffer()).byteLength === rexDemo.length, "reviewers still see their replay");
    check((await hide(rexId, "unhide")).status === 200, "and can unhide them");
    const found = (await (await admin(`/players?q=Rex`)).json()).players.find((p) => p.id === rexId);
    check(found?.scores.some((x) => x.mode === 0 && x.score === big), "a player search shows their bests");

    check((await (await admin(`/players?q=${rexId}`)).json()).players.some((p) => p.id === rexId), "a player search by id finds them");

    // An approved run counts as proof without a replay (runs past an hour have none): it takes its place in the top 10.
    const lena = token();
    players.push(lena);
    s = await score(lena, "Lena", 3, 3e9);
    const lenaId = s.body.id;
    check((await act(3, lenaId, "approve")).status === 200, "a run with no replay can be approved");
    check((await board(3, "?count=1")).body.rows[0]?.id === lenaId, "and shows 1st on its board, verified");

    // Rejecting lets the replay go; Undo asks the game for it again.
    const ivy = token();
    players.push(ivy);
    s = await score(ivy, "Ivy", 2, 5e9);
    const ivyId = s.body.id;
    await upload(ivy, 2, demo({ mode: 2, score: 5e9 }));
    await act(2, ivyId, "reject");
    check((await reviews()).find((r) => r.id === ivyId).replay_size === null, "a rejected run keeps no replay");
    check((await admin(`/reviews/2/${ivyId}/replay`)).status === 404, "it can't be downloaded any more");
    await pause();
    s = await score(ivy, "Ivy", 2, 1_500_000_000);
    check(s.body.replay === true && (await reviews()).find((r) => r.id === ivyId)?.status === "pending",
      "after a rejection, a different honest run is looked at");

    // Approving an older run below the player's newer best leaves that best's replay alone.
    const otto = token();
    players.push(otto);
    s = await score(otto, "Otto", 0, 2.2e9);
    const ottoId = s.body.id;
    await upload(otto, 0, demo({ mode: 0, score: 2.2e9 }));
    check((await put({ limits: limits.map((n, i) => (i === 0 ? 5e9 : n)) })).status === 200, "Deadline's limit goes up");
    await pause();
    s = await score(otto, "Otto", 0, 3e9);
    check(s.body.review === undefined, "a 3 billion Deadline now goes straight up");
    check((await upload(otto, 0, demo({ mode: 0, score: 3e9 }))).status === 200, "with its replay");
    check((await act(0, ottoId, "approve")).status === 200, "the older 2.2 billion run is approved");
    const ottoRow = (await board(0, "?count=50")).body.rows.find((r) => r.id === ottoId);
    check(ottoRow?.score === 3e9 && ottoRow.replay === GAME, "the newer best and its replay stay");
    await put({ limits });

    // The queue holds 25 runs at most, so made-up tokens can't fill the database.
    let full = 0;
    for (let i = 0; i < 30; i++) {
      const t = token();
      players.push(t);
      const r1 = await score(t, `Flood ${i}`, 1, 20e9 + i);
      if (r1.body.review === true && r1.body.replay === false) full++;
    }
    const waitingNow = (await reviews()).filter((r) => r.status === "pending").length;
    check(waitingNow === 25 && full > 0, `the queue stops at 25 runs (${waitingNow} waiting, ${full} turned away)`);

    // Deleting yourself takes a waiting run too.
    const gus = token();
    players.push(gus);
    s = await score(gus, "Gus", 4, 30e9);
    const gusId = s.body.id;
    check(s.body.review === true, "30 billion in Pacifism waits too");
    await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: gus }) });
    check(!(await reviews()).some((r) => r.id === gusId), "deleting a player removes their waiting runs");
    const log = (await (await admin("/reviews")).json()).log;
    check(["approve", "reject", "undo reject", "limits", "remove score", "hide"].every((a) => log.some((l) => l.action === a && l.admin === "Tester")),
      "the log names the reviewer for each action");
  }

  const del = await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: dave }) });
  check(del.status === 200 && (await fetch(`${BASE}/v1/replays/6/${daveId}`)).status === 404, "deleting removes replays");
  for (const t of players) await fetch(`${BASE}/v1/players`, { method: "DELETE", body: JSON.stringify({ token: t }) });
}

console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
