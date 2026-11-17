// Posts to Discord through webhooks. Two secrets, both optional:
//   DISCORD_REVIEW_WEBHOOK  the reviewers' channel: a run has passed its board's review limit and is waiting
//   DISCORD_WEBHOOK         the community channel: a reviewer has approved one, a verified record
// So a fake never reaches the community before someone has looked at it. Point both at one channel to announce both.
// Nothing is ever posted for a rejection or a hidden player, and mentions are switched off, so no message can ping.

// Spelled out: index.js imports this module, so its constants aren't set yet while this one loads.
const BOARDS = ["Deadline", "Evolved", "Sequence", "Waves", "Pacifism", "King", "Master Deadline", "Master Evolved",
  "Master Pacifism"];
// A Discord webhook, or a local catcher for the tests (README: Testing locally).
const HOOK = /^(https:\/\/(?:(?:ptb|canary)\.)?discord(?:app)?\.com\/api\/webhooks\/\d+\/[\w-]+|http:\/\/127\.0\.0\.1:\d+\/[\w/-]*)$/;

function short(n) {
  if (n >= 1e9) return `${+(n / 1e9).toFixed(2)}B`;
  if (n >= 1e6) return `${+(n / 1e6).toFixed(2)}M`;
  return n.toLocaleString("en-GB");
}

// Names are letters, digits, spaces and _ . - (the NAME rule); _ would turn into italics.
const name = (text) => text.replace(/_/g, "\\_");

function post(url, secret, ctx, content) {
  if (!url || !ctx) return;
  if (!HOOK.test(url)) {
    console.warn(`discord webhook: ${secret} isn't a Discord webhook address (it starts "${String(url).slice(0, 12)}")`);
    return;
  }
  ctx.waitUntil(
    fetch(url, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ content, allowed_mentions: { parse: [] } }),
    })
      .then((r) => { if (!r.ok) console.warn(`discord webhook: ${secret} answered HTTP ${r.status}`); })
      .catch((e) => console.warn(`discord webhook: ${secret}: ${e.message}`)),
  );
}

export function announcePassed(env, ctx, player, mode, score, limit) {
  post(env.DISCORD_REVIEW_WEBHOOK, "DISCORD_REVIEW_WEBHOOK", ctx,
    `🌟 **${name(player)}** just scored **${score.toLocaleString("en-GB")}** on **${BOARDS[mode]}**, past the ` +
      `**${short(limit)}** record line! Waiting in the review panel. 👀`);
}

export function announceVerified(env, ctx, player, mode, score) {
  post(env.DISCORD_WEBHOOK, "DISCORD_WEBHOOK", ctx,
    `🏆 **Verified!** **${name(player)}**'s **${score.toLocaleString("en-GB")}** on **${BOARDS[mode]}** is officially ` +
      `on the board. History made! 🎉`);
}
