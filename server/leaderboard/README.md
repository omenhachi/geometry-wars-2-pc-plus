# Online leaderboards server

A Cloudflare Worker with a D1 (SQLite) database that keeps each player's best score per game mode. The game sends a
score after a finished run with the **Online leaderboards** setting on, and reads the boards when a leaderboard screen
opens. If the server is ever gone, the game quietly falls back to its local boards.

Free tier limits (100,000 requests a day, 5 GB of D1 storage) are far beyond what this needs.

## What is stored

Per player: a public id (the first 16 hex digits of the SHA-256 of a secret token that stays on the player's PC), the
display name, and the best score per mode with the input used (mouse, keyboard or controller) and the PC Plus version,
plus the replay of a top 50 best when the player shares it.
No IP addresses, accounts or other personal data. A player can delete everything with `DELETE /v1/players`.

## Deploying

From this folder, with Node.js installed:

1. Create a free Cloudflare account at <https://dash.cloudflare.com/sign-up>.
2. `npm install`
3. `npx wrangler login` (opens a browser to authorise Wrangler on your account).
4. `npx wrangler d1 create gw2-pc-plus-leaderboards`, and put the `database_id` it prints into `wrangler.toml`.
5. `npx wrangler d1 execute gw2-pc-plus-leaderboards --remote --file schema.sql`
6. `npx wrangler deploy`. The first deploy asks you to choose a `workers.dev` subdomain. It prints the URL, such as
   `https://gw2-pc-plus-leaderboards.<subdomain>.workers.dev`, which goes into the game's configuration.

## Testing locally

With its own data folder and a throwaway reviewer key, so it never touches anything else:

```sh
npx wrangler d1 execute gw2-pc-plus-leaderboards --local --persist-to .wrangler-test --file schema.sql
npx wrangler dev --port 8799 --persist-to .wrangler-test --var "ADMIN_KEYS:Tester:<64 hex digits>"
BASE=http://127.0.0.1:8799 ADMIN_KEY=<the same 64 hex digits> node test/smoke.mjs     # in another terminal
```

The panel is then at <http://127.0.0.1:8799/admin>.

## Score review

A score above its board's review limit doesn't reach the board. It waits in the **admin panel** at `/admin` until a
reviewer approves it. The limits start at the Xbox Live Arcade records: Deadline 332 million, Evolved 4.6 billion,
Sequence 400 million, Waves 212 million, Pacifism 23.5 billion and King 92 million. Xbox Live had no Master modes, so
those have lines of their own: Master Deadline 100 million, Master Evolved 4.6 billion (Evolved's) and Master Pacifism
5 billion (`REVIEW_LIMITS` in `src/index.js`). Reviewers can change them in the panel.

- The game is answered as usual (HTTP 200), with `review: true`. It shares the run's replay when the player shares
  replays, and the replay waits with the run. One run waits per player and board: the highest.
- **Approve** puts it on the board, at the time it was sent, marked verified: that counts as proof for the top 10, with
  or without a replay (a run past an hour has none). Its replay goes with it when it is the player's best.
- **Reject** keeps the run but lets its replay go, so the same score isn't asked about again; **Undo** brings it back,
  and the game sends the replay again. Any other score from the same player replaces a rejected one.
- The queue holds at most 25 waiting runs and 100 MB of their replays (`MAX_WAITING`, `MAX_REVIEW_BYTES`), so
  made-up tokens can't fill the database or bury the reviewers. Past that, runs are answered as usual and not kept.
- **Check the replay** reads the file in the browser: the score in it, the length of play, the game's speed tick by
  tick (a tick slower than 1/60 s is flagged), whether the score ever goes down, and a chart of the score over time.
  **Download** saves it under the game's own file naming: put it in `user_data\demos` and open Watch replays.
- **Players** finds a player by name or id: hide or unhide them, remove a best, download a board replay. A hidden
  player's replays are kept, so unhiding restores everything.
- **Log** lists every reviewer action by name.

A board's **top 10 holds only scores with proof**: a kept replay, or a reviewer's approval. A higher score without
either waits just below them, and moves up by itself when its replay arrives.

### Discord announcements

Two optional webhooks (in Discord: the channel's **Edit Channel > Integrations > Webhooks > New Webhook > Copy
Webhook URL**), set as secrets:

```sh
npx wrangler secret put DISCORD_REVIEW_WEBHOOK   # the reviewers' channel: a run past its record line is waiting
npx wrangler secret put DISCORD_WEBHOOK          # the community channel: a reviewer approved it, a verified record
```

So a fake never reaches the community before someone has looked at it. Point both at one channel to announce both
moments there. Nothing is posted for a rejection or a hidden player, and mentions are off, so no message can ping
anyone. For local tests, put them in `.dev.vars` (wrangler's `--var` mangles a value with colons, like a URL).

### Updating a database made before score review

Once, then `schema.sql` as usual (it adds the new tables, and is safe to run again):

```sh
npx wrangler d1 execute gw2-pc-plus-leaderboards --remote --file migrations/2026-11-17-score-review.sql
npx wrangler d1 execute gw2-pc-plus-leaderboards --remote --file schema.sql
```

### Setting up reviewers

Each reviewer gets their own key: 64 random hex digits, for example from
`node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"`. Put every reviewer in one secret, as
comma-separated `Name:key` pairs:

```sh
npx wrangler secret put ADMIN_KEYS        # then paste: Light:3f9c...,Luna:a1b2...
```

Changing the secret replaces the whole list, so removing someone is putting it again without them. The panel keeps the
key in that browser tab only, and sends it in a header, never in a link. With no `ADMIN_KEYS`, the panel and its API
are shut (503).

## Moderation

The admin panel's Players tab does all of this. From the command line, hide a name from every board (the player's
scores stay, so unhiding restores them):

```sh
npx wrangler d1 execute gw2-pc-plus-leaderboards --remote --command "UPDATE players SET hidden = 1 WHERE name = 'TheName'"
```

Unhide with `hidden = 0`. Remove a score: `DELETE FROM scores WHERE player_id = '<id>' AND mode = <mode>`. List the top
of a board: `SELECT p.name, p.id, s.score FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = 0 ORDER BY
s.score DESC LIMIT 20`.

## Rules the server enforces

- Names: 1 to 15 characters of `A-Z a-z 0-9 space _ . -`, which the game's font can draw.
- Boards 0 to 8 (Deadline, Evolved, Sequence, Waves, Pacifism, King, then Master Deadline, Master Evolved and Master
  Pacifism), whole scores from 0 to 100,000,000,000. Above a board's review limit, a score waits for a reviewer.
- A board's top 10 shows only scores with a kept replay or a reviewer's approval.
- One submission per player every 5 seconds: a one-ship Master run can be over that quickly.
- A score that is exactly another player's best with a kept replay is refused: it came from watching that replay.
- Replays: only of a player's best (or of their run waiting for review), while it is in the board's top 50, from 1.0.1 or newer, with no cheats, of the
  board's own mode (a Master board takes only its Master mode), at most 24 MB. They are kept in 1 MB pieces and go
  quietly when the best is beaten or falls out of the top 50.
- Versions older than `MIN_VERSION` in `src/index.js` are refused, in case scoring ever changes. It is 1.0.1: before it,
  watching a replay to its end sent the replay's score as the viewer's own. Older builds still read the boards.
- Refused writes are logged to Workers Logs (path, answer, a score's mode; never the token), so upload troubles on
  a board can be looked up in the Cloudflare dashboard or with `npx wrangler tail`.

Scores come from players' own PCs, and the game is open source, so a determined player can fake one. The review limit
and the top-10 replay rule keep that from reaching the top of a board unseen: a fake has to come with a replay that
really plays out to its score, and someone watches it.
