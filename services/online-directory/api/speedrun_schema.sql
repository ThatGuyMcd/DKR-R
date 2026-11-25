-- Only the independent verifier may attest a replay. The public API can queue
-- submissions and moderate attested runs; it has no verification HTTP route.
CREATE TABLE IF NOT EXISTS speedrun_boards (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL CHECK (kind IN ('vanilla','custom','full-game')),
  title TEXT NOT NULL,
  category TEXT NOT NULL,
  vehicle TEXT NOT NULL CHECK (vehicle IN ('car','hovercraft','plane','mixed')),
  ruleset TEXT NOT NULL,
  engine TEXT NOT NULL,
  release_id TEXT REFERENCES library_releases(id),
  release_sha256 TEXT,
  CHECK ((kind='custom' AND release_id IS NOT NULL AND length(release_sha256)=64)
      OR (kind<>'custom' AND release_id IS NULL AND release_sha256 IS NULL))
);
-- One audited native track payload per immutable Mod Library release.
CREATE TABLE IF NOT EXISTS speedrun_custom_packages (
  release_id TEXT PRIMARY KEY REFERENCES library_releases(id) ON DELETE CASCADE,
  release_sha256 TEXT NOT NULL CHECK (length(release_sha256)=64),
  track_id TEXT NOT NULL,
  track_sha256 TEXT NOT NULL CHECK (length(track_sha256)=64),
  lap_count INTEGER NOT NULL DEFAULT 3 CHECK (lap_count BETWEEN 1 AND 5)
);
CREATE TABLE IF NOT EXISTS speedrun_runs (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  board_id TEXT NOT NULL REFERENCES speedrun_boards(id),
  sha256 TEXT NOT NULL,
  attempt_id TEXT NOT NULL UNIQUE,
  manifest TEXT NOT NULL,
  notes TEXT NOT NULL,
  state TEXT NOT NULL CHECK (state IN ('uploading','queued','verified','published','rejected')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  verified_ticks INTEGER CHECK (verified_ticks IS NULL OR verified_ticks > 0),
  verified_best_ticks INTEGER CHECK (verified_best_ticks IS NULL OR verified_best_ticks > 0),
  verifier_digest TEXT,
  UNIQUE (account_id,sha256)
);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_account ON speedrun_runs(account_id,created_at DESC,id DESC);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_board ON speedrun_runs(board_id,state,verified_ticks);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_state ON speedrun_runs(state,created_at,id);
-- Keep an input fingerprint reserved even if its linked submission is removed.
CREATE TABLE IF NOT EXISTS speedrun_local_claims (
  claim_key TEXT PRIMARY KEY
);
CREATE TABLE IF NOT EXISTS speedrun_capture_credits (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  discord_id TEXT NOT NULL,
  credited_name TEXT NOT NULL,
  input_digest TEXT NOT NULL CHECK (length(input_digest)=64)
);
CREATE INDEX IF NOT EXISTS speedrun_credits_by_input ON speedrun_capture_credits(input_digest);
CREATE TABLE IF NOT EXISTS speedrun_moderation (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  moderator_id TEXT REFERENCES accounts(id) ON DELETE SET NULL,
  decision TEXT NOT NULL CHECK (decision IN ('published','rejected')),
  reason TEXT NOT NULL,
  decided_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS speedrun_ftp_upload_leases (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  lease TEXT NOT NULL,
  until INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS speedrun_verifier_claims (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  claim TEXT NOT NULL,
  until INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS speedrun_video_jobs (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  attempts INTEGER NOT NULL DEFAULT 0,
  retry_at INTEGER NOT NULL DEFAULT 0,
  claim TEXT,
  lease_until INTEGER NOT NULL DEFAULT 0,
  rendered_at INTEGER
);
CREATE TABLE IF NOT EXISTS speedrun_video_assets (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  replay_sha256 TEXT NOT NULL CHECK (length(replay_sha256)=64),
  video_sha256 TEXT NOT NULL CHECK (length(video_sha256)=64),
  bytes INTEGER NOT NULL CHECK (bytes>0 AND bytes<=536870912)
);
CREATE TABLE IF NOT EXISTS speedrun_video_deletions (
  run_id TEXT NOT NULL,
  video_sha256 TEXT NOT NULL,
  PRIMARY KEY(run_id,video_sha256)
);
CREATE TABLE IF NOT EXISTS speedrun_verifier_health (
  id INTEGER PRIMARY KEY CHECK (id=1),
  runtime TEXT NOT NULL,
  seen INTEGER NOT NULL
);

-- The long-session worker has its own health record and queue.
CREATE TABLE IF NOT EXISTS speedrun_adventure_verifier_health (
  runtime TEXT PRIMARY KEY,
  seen INTEGER NOT NULL
);

-- Independent of the account and run so failed object cleanup can be retried.
CREATE TABLE IF NOT EXISTS speedrun_replay_deletions (
  run_id TEXT PRIMARY KEY,
  removed_at INTEGER NOT NULL
);
CREATE TRIGGER IF NOT EXISTS speedrun_queue_video_deletion
AFTER DELETE ON speedrun_video_assets
BEGIN
  INSERT OR IGNORE INTO speedrun_video_deletions(run_id,video_sha256) VALUES(OLD.run_id,OLD.video_sha256);
END;

CREATE TRIGGER IF NOT EXISTS speedrun_queue_replay_deletion
AFTER DELETE ON speedrun_runs
BEGIN
  INSERT OR REPLACE INTO speedrun_replay_deletions(run_id,removed_at) VALUES(OLD.id,unixepoch());
END;

CREATE TRIGGER IF NOT EXISTS speedrun_apply_moderation
AFTER INSERT ON speedrun_moderation
BEGIN
  UPDATE speedrun_runs SET state=NEW.decision,updated_at=NEW.decided_at
  WHERE id=NEW.run_id AND state='verified';
END;
