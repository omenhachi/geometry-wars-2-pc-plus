// The reviewers' panel. Every text from the server is set with textContent, never as markup.
"use strict";

const BOARDS = ["Deadline", "Evolved", "Sequence", "Waves", "Pacifism", "King", "Master Deadline", "Master Evolved",
  "Master Pacifism"];
// The game's file names for replays (demo_library.cpp), so a downloaded one sits among the player's own.
const SLUGS = ["deadline", "evolved", "sequence", "waves", "pacifism", "king"];
const NORMAL_STEP = 0x3c888889; // 1/60 s as the game stores it: a tick at normal speed
const STORE = "gw2-review-key";

let key = "";
let state = { reviews: [], limits: [], max: [], games: [], log: [] };

const $ = (id) => document.getElementById(id);

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "class") node.className = v;
    else if (k === "text") node.textContent = v;
    else if (k.startsWith("on")) node.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) node.setAttribute(k, v === true ? "" : v);
  }
  for (const child of children.flat()) if (child !== null && child !== undefined && child !== false) {
    node.append(typeof child === "string" ? document.createTextNode(child) : child);
  }
  return node;
}

const whole = (n) => Number(n).toLocaleString("en-GB");
function short(n) {
  n = Number(n);
  if (n >= 1e9) return `${+(n / 1e9).toFixed(n >= 1e10 ? 1 : 2)}B`;
  if (n >= 1e6) return `${+(n / 1e6).toFixed(1)}M`;
  return whole(n);
}
const when = (s) => (s ? new Date(s * 1000).toLocaleString("en-GB", { dateStyle: "medium", timeStyle: "short" }) : "");
function clock(seconds) {
  const s = Math.round(seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h ? `${h}:` : ""}${String(m).padStart(h ? 2 : 1, "0")}:${String(s % 60).padStart(2, "0")}`;
}
const megabytes = (n) => `${(n / 1048576).toFixed(n < 1048576 ? 2 : 1)} MB`;

// Storage can be refused (private windows, blocked site data): then the key lives in memory for this page only.
function loadKey() {
  try { return sessionStorage.getItem(STORE) ?? ""; } catch { return ""; }
}
function saveKey(value) {
  try { value ? sessionStorage.setItem(STORE, value) : sessionStorage.removeItem(STORE); } catch {}
}

async function api(path, options = {}) {
  const response = await fetch(`/v1/admin${path}`, {
    ...options,
    headers: { authorization: `Bearer ${key}`, ...(options.body ? { "content-type": "application/json" } : {}) },
  });
  if (response.status === 401) {
    signOut("That key isn't a reviewer key (any more).");
    throw new Error("signed out");
  }
  return response;
}
async function apiJson(path, options) {
  const response = await api(path, options);
  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error ?? `HTTP ${response.status}`);
  return body;
}

// ---- signing in

function showSignIn(message = "") {
  $("app").hidden = true;
  $("who").hidden = true;
  $("sign-in").hidden = false;
  $("key-note").textContent = message;
  $("key-note").className = message ? "note bad" : "note";
  $("key").focus();
}

function signOut(message) {
  key = "";
  saveKey("");
  showSignIn(message);
}

async function signIn(candidate) {
  key = candidate.trim();
  if (!key) return showSignIn("Paste a key first.");
  try {
    const me = await apiJson("/me");
    saveKey(key);
    $("who-name").textContent = me.name;
    $("who").hidden = false;
    $("sign-in").hidden = true;
    $("app").hidden = false;
    $("key").value = "";
    await refresh();
  } catch (e) {
    if (e.message !== "signed out") showSignIn(`Couldn't sign in: ${e.message}`);
  }
}

// ---- data

async function refresh() {
  state = await apiJson("/reviews");
  const waiting = state.reviews.filter((r) => r.status === "pending" && !r.hidden);
  const rejected = state.reviews.filter((r) => r.status === "rejected");
  setCount("count-waiting", waiting.length);
  setCount("count-rejected", rejected.length);
  renderRuns($("view-waiting"), waiting, "Nothing waiting", "Every score on the boards is under its limit.");
  renderRuns($("view-rejected"), rejected, "Nothing rejected", "Rejected runs stay here, with Undo.");
  renderLimits();
  renderLog();
}

function setCount(id, n) {
  $(id).textContent = String(n);
  $(id).classList.toggle("some", n > 0 && id === "count-waiting");
}

// ---- runs

function renderRuns(view, runs, emptyTitle, emptyText) {
  view.replaceChildren();
  if (runs.length === 0) {
    view.append(el("div", { class: "panel empty" }, el("div", { class: "big", text: emptyTitle }), el("p", { text: emptyText })));
    return;
  }
  for (const run of runs) view.append(runCard(run));
}

function runCard(run) {
  const limit = state.limits[run.mode];
  const card = el("article", { class: `panel run${run.status === "rejected" ? " rejected" : ""}` });
  card.append(
    el("div", { class: "run-head" },
      el("div", {},
        el("div", { class: "board", text: BOARDS[run.mode] }),
        el("div", { class: "score", text: whole(run.score) })),
      el("div", { class: "times", text: run.score > limit ? `${(run.score / limit).toFixed(2)}× the ${short(limit)} limit`
        : `under today's ${short(limit)} limit: it was over the limit when sent` })),
    el("div", { class: "facts" },
      fact("Player", run.name),
      fact("Their best on the board", run.board_best ? whole(run.board_best) : "none yet"),
      fact("Played with", run.input),
      fact("Version", run.version),
      fact("Sent", when(run.submitted)),
      run.status === "rejected" ? fact("Rejected", `${when(run.decided)} by ${run.decided_by}`) : null),
    replayPart(run),
    actions(run),
  );
  return card;
}

function fact(label, value) {
  return el("div", {}, label, el("b", { text: String(value) }));
}

function replayPart(run) {
  const part = el("div", { class: "replay" });
  if (run.replay_size === null) {
    part.append(el("p", {},
      el("span", { class: "warn", text: "No replay yet. " }),
      "It arrives by itself when the player's game shares it (Share my top runs as replays, in the launcher). " +
        "Replays stop after an hour of play, so a longer run has none: ask for a recording. Approving counts as " +
        "proof, so an approved run takes its place in the top 10 with or without a replay."));
    return part;
  }
  const main = state.games[0]?.game;
  const same = run.replay_game === main;
  part.append(
    el("p", {},
      `Replay ${megabytes(run.replay_size)}, sent ${when(run.replay_uploaded)}. `,
      el("span", same ? { class: "ok", text: "Recorded on the same game package as most replays." }
        : { class: "warn", text: "Recorded on a different game package from most replays: it may play out of step." })),
    el("div", { class: "row" },
      el("button", { class: "hot", type: "button", onclick: (e) => checkReplay(run, part, e.currentTarget) }, "Check the replay"),
      el("button", { type: "button", onclick: (e) => download(run, e.currentTarget) }, "Download to watch in the game")),
  );
  return part;
}

function actions(run) {
  const box = el("div", { class: "actions" });
  if (run.status === "pending") {
    box.append(
      sureButton("Approve", "go", "Sure? Put it on the board", () => decide(run, "approve")),
      el("span", { class: "spacer" }),
      sureButton("Reject", "stop", "Sure? Reject it", () => decide(run, "reject")),
    );
  } else {
    box.append(el("button", { type: "button", onclick: () => decide(run, "undo") }, "Undo: back to waiting"));
  }
  return box;
}

// A button that asks once more before it acts: a second press within 4 seconds.
function sureButton(label, kind, sure, act) {
  let armed = null;
  const button = el("button", { class: kind, type: "button" }, label);
  button.addEventListener("click", async () => {
    if (!armed) {
      button.textContent = sure;
      button.classList.add("sure");
      armed = setTimeout(() => { armed = null; button.textContent = label; button.classList.remove("sure"); }, 4000);
      return;
    }
    clearTimeout(armed);
    armed = null;
    button.disabled = true;
    await act();
  });
  return button;
}

async function decide(run, action) {
  let problem = "";
  try {
    await apiJson(`/reviews/${run.mode}/${run.id}`, { method: "POST", body: JSON.stringify({ action }) });
  } catch (e) {
    if (e.message === "signed out") return;
    problem = `Couldn't ${action === "undo" ? "undo that" : action} ${run.name}'s run: ${e.message}`;
  }
  await refresh();
  if (problem) showProblem(problem);
}

// A message at the top of whichever view is open, until the next redraw of it.
function showProblem(message) {
  const view = [...document.querySelectorAll(".view")].find((v) => !v.hidden) ?? $("view-waiting");
  view.prepend(el("div", { class: "panel" }, el("p", { class: "bad", text: message })));
}

async function fetchReplay(run) {
  const response = await api(`/reviews/${run.mode}/${run.id}/replay`);
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return await response.arrayBuffer();
}

async function download(run, button) {
  button.disabled = true;
  try {
    const bytes = await fetchReplay(run);
    const info = readHeader(bytes);
    const stamp = new Date(info.recorded * 1000).toISOString().replace(/[-:]/g, "").replace("T", "-").slice(0, 15);
    const name = `${stamp}-${SLUGS[info.mode] ?? "run"}-${info.score}.gw2demo`;
    const link = el("a", { href: URL.createObjectURL(new Blob([bytes])), download: name });
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 60000);
    button.textContent = "Downloaded: put it in user_data\\demos, then Watch replays";
  } catch (e) {
    button.textContent = `Couldn't download: ${e.message}`;
  }
  button.disabled = false;
}

// ---- reading a replay (src/demo_file.h): header, then ticks, pads, shots and score checkpoints

function reader(bytes) {
  const view = new DataView(bytes);
  const raw = new Uint8Array(bytes);
  let at = 0;
  const need = (n) => { if (at + n > raw.length) throw new Error("the file ends early"); };
  return {
    get at() { return at; },
    skip(n) { need(n); at += n; },
    u8() { need(1); return raw[at++]; },
    u32() { need(4); at += 4; return view.getUint32(at - 4, true); },
    f32At(i) { return view.getFloat32(i, true); },
    u32At(i) { return view.getUint32(i, true); },
    u64() { need(8); at += 8; return view.getBigUint64(at - 8, true); },
    text() { const n = this.u8(); need(n); at += n; return new TextDecoder().decode(raw.subarray(at - n, at)); },
    end: raw.length,
  };
}

function readHeader(bytes) {
  const r = reader(bytes);
  const magic = [0x47, 0x57, 0x32, 0x44, 0x45, 0x4d, 0x4f, 0x1a];
  const head = new Uint8Array(bytes, 0, Math.min(8, bytes.byteLength));
  if (head.length < 8 || !magic.every((b, i) => head[i] === b)) throw new Error("not a replay");
  r.skip(8);
  const format = r.u32();
  const rules = r.u32();
  const port = r.text();
  const game = r.text();
  const player = r.text();
  const mode = r.u32();
  const score = r.u64();
  const recorded = Number(r.u64());
  const flags = r.u32();
  let custom = null;
  if (format >= 2 && (flags & 2)) {
    custom = r.text();
    r.text(); // author
    r.skip(format === 2 ? 24 : r.u8());
  }
  return { r, format, rules, port, game, player, mode, score, recorded, flags, custom };
}

function analyse(bytes) {
  const info = readHeader(bytes);
  const { r } = info;
  r.skip(2500 + 4 + 4 + 16); // random states and input state
  const ticks = r.u32();
  const stepsAt = r.at;
  r.skip(ticks * 4);
  const counts = new Map();
  let gameSeconds = 0;
  let slower = 0;
  let firstSlower = -1;
  for (let i = 0; i < ticks; i++) {
    const bits = r.u32At(stepsAt + i * 4);
    const step = r.f32At(stepsAt + i * 4);
    counts.set(bits, (counts.get(bits) ?? 0) + 1);
    gameSeconds += step;
    if (step < (1 / 60) * 0.999) {
      slower++;
      if (firstSlower < 0) firstSlower = i;
    }
  }
  const padChanges = r.u32();
  r.skip(padChanges * 84);
  const shots = r.u32();
  r.skip(shots * 12);
  const n = r.u32();
  const checkpoints = [];
  for (let i = 0; i < n; i++) checkpoints.push(Number(r.u64()));
  const usual = [...counts.entries()].sort((a, b) => b[1] - a[1])[0] ?? [NORMAL_STEP, 0];
  let drops = 0;
  for (let i = 1; i < checkpoints.length; i++) if (checkpoints[i] < checkpoints[i - 1]) drops++;
  return {
    ...info, ticks, gameSeconds, slower, firstSlower, usualBits: usual[0], usualShare: usual[1] / Math.max(1, ticks),
    padChanges, shots, checkpoints, drops, tidyEnd: r.at === r.end,
  };
}

async function checkReplay(run, part, button) {
  button.disabled = true;
  part.querySelector(".check")?.remove();
  const box = el("div", { class: "check" }, el("p", { text: "Reading the replay…" }));
  part.append(box);
  try {
    const a = analyse(await fetchReplay(run));
    const usualStep = new DataView(new Uint32Array([a.usualBits]).buffer).getFloat32(0, true);
    const items = [];
    const item = (good, text) => items.push(el("li", { class: good === null ? "" : good ? "ok" : "bad", text }));
    item(BigInt(run.score) === a.score, `Score in the file: ${whole(a.score)}${BigInt(run.score) === a.score ? ", the same as sent" : `, but ${whole(run.score)} was sent`}`);
    item(null, `${clock(a.ticks / 60)} of play (${whole(a.ticks)} ticks), ${whole(a.shots)} shots, recorded ${when(a.recorded)} on PC Plus ${a.port}`);
    item(a.usualBits === NORMAL_STEP,
      a.usualBits === NORMAL_STEP ? `Normal game speed on ${(a.usualShare * 100).toFixed(1)}% of ticks (1/60 s each); the rest are longer steps, as when a frame hitches`
        : `Most ticks step ${usualStep.toFixed(5)} s, not the normal 1/60 s (${(usualStep * 60).toFixed(2)}× speed)`);
    item(a.slower === 0, a.slower === 0 ? "No tick ran slower than normal speed"
      : `${whole(a.slower)} ticks ran slower than normal speed, the first at ${clock(a.firstSlower / 60)}`);
    item(a.drops === 0, a.drops === 0 ? "The score never goes down" : `The score goes down ${a.drops} times`);
    const last = a.checkpoints.at(-1) ?? 0;
    item(last <= Number(a.score), `Last checkpoint ${whole(last)}, ${clock(a.checkpoints.length)} in`);
    item(a.tidyEnd, a.tidyEnd ? "The file ends exactly where its contents do" : "The file has extra bytes at the end");
    if (a.flags & 1) item(false, "Recorded with cheats on");
    box.replaceChildren(
      el("ul", {}, items),
      chart(a.checkpoints, state.limits[run.mode]),
      el("p", { class: "note", text: a.checkpoints.at(-1) * 1.05 >= state.limits[run.mode] * 0.5
        ? "Score over time, with the board's limit dashed." : "Score over time." }),
      el("p", { class: "note", text: "Red means look closer, not reject: a slow PC or a hitch can explain a lot. These checks read the file the player's game wrote. Watching it in the game is the real test: a replay whose inputs don't really make its score goes out of step and stops." }),
    );
  } catch (e) {
    box.replaceChildren(el("p", { class: "bad", text: `Couldn't read it: ${e.message}` }));
  }
  button.disabled = false;
}

// Score over time, one point per checkpoint (a second of play), scaled to the run; the board's limit dashed when it
// is within reach of the scale.
function chart(points, limit) {
  const ns = "http://www.w3.org/2000/svg";
  const W = 600, H = 140, pad = 4;
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  svg.setAttribute("class", "chart");
  svg.setAttribute("preserveAspectRatio", "none");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", "Score over time");
  if (points.length < 2) return svg;
  const peak = Math.max(...points);
  const showLimit = peak * 1.05 >= limit * 0.5;
  const top = Math.max(peak, showLimit ? limit : 0) * 1.05 || 1;
  const step = Math.max(1, Math.floor(points.length / W));
  const xy = [];
  for (let i = 0; i < points.length; i += step) xy.push([(i / (points.length - 1)) * W, H - pad - (points[i] / top) * (H - 2 * pad)]);
  xy.push([W, H - pad - (points.at(-1) / top) * (H - 2 * pad)]);
  const line = xy.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(" ");
  const add = (tag, attrs) => { const n = document.createElementNS(ns, tag); for (const k in attrs) n.setAttribute(k, attrs[k]); svg.append(n); return n; };
  add("polygon", { class: "area", points: `0,${H} ${line} ${W},${H}` });
  add("polyline", { class: "line", points: line });
  if (showLimit) {
    const ly = H - pad - (limit / top) * (H - 2 * pad);
    add("line", { class: "limit", x1: 0, x2: W, y1: ly, y2: ly });
  }
  return svg;
}

// ---- players

async function searchPlayers() {
  const q = $("player-q").value.trim();
  const out = $("player-results");
  if (!q) return out.replaceChildren();
  out.replaceChildren(el("div", { class: "panel" }, el("p", { text: "Searching…" })));
  try {
    const { players } = await apiJson(`/players?q=${encodeURIComponent(q)}`);
    out.replaceChildren();
    if (players.length === 0) out.append(el("div", { class: "panel" }, el("p", { text: "Nobody by that name." })));
    for (const p of players) out.append(playerCard(p));
  } catch (e) {
    if (e.message !== "signed out") out.replaceChildren(el("div", { class: "panel" }, el("p", { class: "bad", text: e.message })));
  }
}

function playerCard(p) {
  const rows = p.scores.map((s) => el("tr", {},
    el("td", { text: BOARDS[s.mode] }),
    el("td", { text: whole(s.score) }),
    el("td", { text: s.replay ? megabytes(s.replay) : "none" }),
    el("td", { text: when(s.updated) }),
    el("td", {},
      s.replay ? el("button", { type: "button", onclick: (e) => downloadBoard(p, s, e.currentTarget) }, "Replay") : null,
      " ",
      sureButton("Remove", "stop", "Sure? Remove it", async () => {
        let problem = "";
        try { await apiJson(`/scores/${s.mode}/${p.id}`, { method: "DELETE" }); } catch (e) { problem = e.message; }
        await searchPlayers();
        if (problem && problem !== "signed out") showProblem(`Couldn't remove it: ${problem}`);
      }))));
  return el("div", { class: "panel" },
    el("div", { class: "player-head" },
      el("div", {},
        el("span", { class: "name", text: p.name }), " ",
        el("span", { class: `tag${p.hidden ? " hidden" : ""}`, text: p.hidden ? "hidden from the boards" : "on the boards" }),
        el("div", { class: "mono", text: `${p.id} · first seen ${when(p.created)} · last score ${when(p.last_submit)}` })),
      sureButton(p.hidden ? "Unhide" : "Hide", p.hidden ? "go" : "stop", p.hidden ? "Sure? Show again" : "Sure? Hide everywhere",
        async () => {
          let problem = "";
          try { await apiJson(`/players/${p.id}`, { method: "POST", body: JSON.stringify({ action: p.hidden ? "unhide" : "hide" }) }); } catch (e) { problem = e.message; }
          await searchPlayers();
          if (problem && problem !== "signed out") showProblem(`Couldn't change that: ${problem}`);
        })),
    rows.length ? el("table", {}, el("thead", {}, el("tr", {}, ["Board", "Best", "Replay", "Set", ""].map((h) => el("th", { text: h })))), el("tbody", {}, rows))
      : el("p", { text: "No scores." }));
}

async function downloadBoard(p, s, button) {
  button.disabled = true;
  try {
    const response = await api(`/replays/${s.mode}/${p.id}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const bytes = await response.arrayBuffer();
    const info = readHeader(bytes);
    const stamp = new Date(info.recorded * 1000).toISOString().replace(/[-:]/g, "").replace("T", "-").slice(0, 15);
    const link = el("a", { href: URL.createObjectURL(new Blob([bytes])), download: `${stamp}-${SLUGS[info.mode] ?? "run"}-${info.score}.gw2demo` });
    document.body.append(link);
    link.click();
    link.remove();
    button.textContent = "Downloaded";
  } catch (e) {
    button.textContent = e.message;
  }
  button.disabled = false;
}

// ---- limits and log

async function saveTell() {
  const box = $("tell-rejected");
  const note = $("tell-note");
  try {
    await apiJson("/settings", { method: "PUT", body: JSON.stringify({ tell_rejected: box.checked }) });
    note.textContent = box.checked ? "Players will be told, once, gently." : "The council stays silent.";
    note.className = "note good";
  } catch (e) {
    box.checked = !box.checked;
    note.textContent = e.message;
    note.className = "note bad";
  }
}

function renderLimits() {
  $("tell-rejected").checked = state.settings?.tell_rejected !== false;
  const box = $("limits");
  box.replaceChildren();
  state.limits.forEach((limit, mode) => {
    const hint = el("small", { text: short(limit) });
    const input = el("input", { type: "text", inputmode: "numeric", value: String(limit), "data-mode": mode, "aria-label": `${BOARDS[mode]} limit` });
    input.addEventListener("input", () => {
      const n = Number(input.value.replace(/[,\s_]/g, ""));
      hint.textContent = Number.isSafeInteger(n) && n > 0 ? short(n) : "a whole number, please";
    });
    box.append(el("label", {}, el("span", { text: BOARDS[mode] }), input, hint));
  });
}

async function saveLimits() {
  const limits = [...$("limits").querySelectorAll("input")].map((i) => Number(i.value.replace(/[,\s_]/g, "")));
  const note = $("limits-note");
  try {
    await apiJson("/limits", { method: "PUT", body: JSON.stringify({ limits }) });
    note.textContent = "Saved. New scores use these straight away.";
    note.className = "note good";
    await refresh();
  } catch (e) {
    note.textContent = e.message;
    note.className = "note bad";
  }
}

function renderLog() {
  $("log").replaceChildren(...state.log.map((l) => el("tr", {},
    el("td", { text: when(l.at) }),
    el("td", { text: l.admin }),
    el("td", { text: l.detail ? `${l.action}: ${l.detail}` : l.action }),
    el("td", { text: l.name ?? l.id ?? "" }),
    el("td", { text: l.mode === null ? "" : BOARDS[l.mode] }),
    el("td", { text: l.score === null ? "" : whole(l.score) }))));
  if (state.log.length === 0) $("log").append(el("tr", {}, el("td", { colspan: 6, text: "Nothing yet." })));
}

// ---- wiring

function showTab(name) {
  for (const b of document.querySelectorAll(".tabs button")) {
    const on = b.dataset.tab === name;
    b.classList.toggle("on", on);
    b.setAttribute("aria-selected", on ? "true" : "false");
  }
  for (const v of document.querySelectorAll(".view")) v.hidden = v.id !== `view-${name}`;
  if (name === "players") $("player-q").focus();
}

document.addEventListener("DOMContentLoaded", () => {
  for (const b of document.querySelectorAll(".tabs button")) b.addEventListener("click", () => showTab(b.dataset.tab));
  $("key-go").addEventListener("click", () => signIn($("key").value));
  $("key").addEventListener("keydown", (e) => { if (e.key === "Enter") signIn($("key").value); });
  $("sign-out").addEventListener("click", () => signOut(""));
  $("player-go").addEventListener("click", searchPlayers);
  $("player-q").addEventListener("keydown", (e) => { if (e.key === "Enter") searchPlayers(); });
  $("limits-save").addEventListener("click", saveLimits);
  $("tell-rejected").addEventListener("change", saveTell);
  const saved = loadKey();
  if (saved) signIn(saved);
  else showSignIn();
});
