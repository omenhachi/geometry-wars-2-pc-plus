-- Geometry Wars 2 PC Plus online leaderboards (Cloudflare D1).
-- A player is known only by a public id derived from a secret token that never leaves their PC, and a display name.
-- No IP addresses or other personal data are stored.

CREATE TABLE IF NOT EXISTS players (
  id TEXT PRIMARY KEY,               -- first 16 hex digits of SHA-256(token)
  name TEXT NOT NULL,
  hidden INTEGER NOT NULL DEFAULT 0, -- 1 hides the player from every board (moderation)
  created INTEGER NOT NULL,          -- unix seconds
  last_submit INTEGER NOT NULL DEFAULT 0
);

-- Best score per player and mode. Modes are the game's view ids: 0 Deadline, 1 Evolved, 2 Sequence, 3 Waves,
-- 4 Pacifism, 5 King, then PC Plus's own Master modes: 6 Master Deadline, 7 Master Evolved.
CREATE TABLE IF NOT EXISTS scores (
  player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
  mode INTEGER NOT NULL,
  score INTEGER NOT NULL,
  input TEXT NOT NULL,               -- mouse, keyboard or controller: lets boards be split later
  version TEXT NOT NULL,             -- the PC Plus version that set the score
  updated INTEGER NOT NULL,
  verified INTEGER NOT NULL DEFAULT 0, -- 1: a reviewer approved it, which counts as proof for the top 10 like a replay
  PRIMARY KEY (player_id, mode)
);

CREATE INDEX IF NOT EXISTS scores_by_mode ON scores (mode, score DESC, updated ASC);

-- A replay of a player's best run on a board, kept while that best is in the board's top 50 (REPLAY_RANKS in
-- src/index.js). The file is the game's own .gw2demo, kept in pieces because a D1 row holds at most 2 MB.
CREATE TABLE IF NOT EXISTS replays (
  player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
  mode INTEGER NOT NULL,
  score INTEGER NOT NULL,            -- the best it was recorded for: a newer best makes it stale
  game_sha256 TEXT NOT NULL,         -- the game package it was recorded on: another package may play out of step
  size INTEGER NOT NULL,
  pieces INTEGER NOT NULL,
  uploaded INTEGER NOT NULL,
  PRIMARY KEY (player_id, mode)
);

CREATE TABLE IF NOT EXISTS replay_pieces (
  player_id TEXT NOT NULL,
  mode INTEGER NOT NULL,
  piece INTEGER NOT NULL,
  bytes BLOB NOT NULL,
  PRIMARY KEY (player_id, mode, piece)
);

-- A score above its board's review limit (REVIEW_LIMITS in src/index.js, or the admin panel's own) waits here for a
-- reviewer instead of reaching the board: one per player and board, the highest. Approving moves it and its replay onto
-- the board; rejecting keeps it, so the same score is not asked about again and Undo can bring it back.
CREATE TABLE IF NOT EXISTS reviews (
  player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
  mode INTEGER NOT NULL,
  score INTEGER NOT NULL,
  input TEXT NOT NULL,
  version TEXT NOT NULL,
  submitted INTEGER NOT NULL,        -- unix seconds: the board's tie order once approved
  status TEXT NOT NULL,              -- pending or rejected
  decided INTEGER,
  decided_by TEXT,                   -- the reviewer's name from ADMIN_KEYS
  replay_game TEXT,                  -- the replay, once uploaded: as in replays
  replay_size INTEGER,
  replay_pieces INTEGER,
  replay_uploaded INTEGER,
  PRIMARY KEY (player_id, mode)
);

CREATE TABLE IF NOT EXISTS review_pieces (
  player_id TEXT NOT NULL,
  mode INTEGER NOT NULL,
  piece INTEGER NOT NULL,
  bytes BLOB NOT NULL,
  PRIMARY KEY (player_id, mode, piece)
);

-- Values the admin panel can change: review_limits, a JSON array of one limit per board.
CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- What reviewers did, by name from ADMIN_KEYS. Ids and numbers only.
CREATE TABLE IF NOT EXISTS admin_log (
  at INTEGER NOT NULL,
  admin TEXT NOT NULL,
  action TEXT NOT NULL,
  player_id TEXT,
  mode INTEGER,
  score INTEGER,
  detail TEXT
);
