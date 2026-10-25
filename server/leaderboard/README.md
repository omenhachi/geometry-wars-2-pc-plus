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

```sh
npx wrangler d1 execute gw2-pc-plus-leaderboards --local --file schema.sql
npx wrangler dev
node test/smoke.mjs     # in another terminal
```

## Moderation

Hide a name from every board (the player's scores stay, so unhiding restores them):

```sh
npx wrangler d1 execute gw2-pc-plus-leaderboards --remote --command "UPDATE players SET hidden = 1 WHERE name = 'TheName'"
```

Unhide with `hidden = 0`. Remove a score: `DELETE FROM scores WHERE player_id = '<id>' AND mode = <mode>`. List the top
of a board: `SELECT p.name, p.id, s.score FROM scores s JOIN players p ON p.id = s.player_id WHERE s.mode = 0 ORDER BY
s.score DESC LIMIT 20`.

## Rules the server enforces

- Names: 1 to 15 characters of `A-Z a-z 0-9 space _ . -`, which the game's font can draw.
- Modes 0 to 5 (Deadline, Evolved, Sequence, Waves, Pacifism, King), whole scores from 0 to 100,000,000,000.
- One submission per player every 20 seconds.
- Replays: only of a player's best, while it is in the board's top 50, from 0.12.0 or newer, with no cheats, of the
  board's own mode (a Master board takes only its Master mode), at most 24 MB. They are kept in 1 MB pieces and go
  quietly when the best is beaten or falls out of the top 50.
- Versions older than `MIN_VERSION` in `src/index.js` are refused, in case scoring ever changes.

Scores come from players' own PCs, and the game is open source, so a determined player can fake one. These are
community boards for fun, not a tournament.
