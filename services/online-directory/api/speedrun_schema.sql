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
CREATE TABLE IF NOT EXISTS speedrun_runs (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL REFERENCES accounts(id),
  board_id TEXT NOT NULL REFERENCES speedrun_boards(id),
  sha256 TEXT NOT NULL,
  manifest TEXT NOT NULL,
  notes TEXT NOT NULL,
  state TEXT NOT NULL CHECK (state IN ('uploading','queued','verified','published','rejected')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  verified_ticks INTEGER CHECK (verified_ticks IS NULL OR verified_ticks > 0),
  verifier_digest TEXT,
  UNIQUE (account_id,sha256)
);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_account ON speedrun_runs(account_id,created_at DESC,id DESC);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_board ON speedrun_runs(board_id,state,verified_ticks);
CREATE INDEX IF NOT EXISTS speedrun_runs_by_state ON speedrun_runs(state,created_at,id);
CREATE TABLE IF NOT EXISTS speedrun_moderation (
  run_id TEXT PRIMARY KEY REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  moderator_id TEXT NOT NULL REFERENCES accounts(id),
  decision TEXT NOT NULL CHECK (decision IN ('published','rejected')),
  reason TEXT NOT NULL,
  decided_at INTEGER NOT NULL
);

CREATE TRIGGER IF NOT EXISTS speedrun_apply_moderation
AFTER INSERT ON speedrun_moderation
BEGIN
  UPDATE speedrun_runs SET state=NEW.decision,updated_at=NEW.decided_at
  WHERE id=NEW.run_id AND state='verified';
END;
