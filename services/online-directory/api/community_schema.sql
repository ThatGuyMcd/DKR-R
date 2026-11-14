-- Additive website features. Existing catalogue, accounts and reviews are retained.
CREATE TABLE IF NOT EXISTS library_download_totals (
  release_id TEXT PRIMARY KEY REFERENCES library_releases(id) ON DELETE CASCADE,
  total INTEGER NOT NULL DEFAULT 0 CHECK(total >= 0),
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS library_download_attempts (
  id TEXT PRIMARY KEY,
  release_id TEXT NOT NULL REFERENCES library_releases(id) ON DELETE CASCADE,
  mutation TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS library_download_attempts_age ON library_download_attempts(created_at);
CREATE TABLE IF NOT EXISTS library_download_limits (
  visitor TEXT NOT NULL,
  window_start INTEGER NOT NULL,
  count INTEGER NOT NULL,
  PRIMARY KEY(visitor, window_start)
);
CREATE TABLE IF NOT EXISTS library_comments (
  id TEXT PRIMARY KEY,
  item_id TEXT NOT NULL REFERENCES library_items(id) ON DELETE CASCADE,
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  operation_id TEXT NOT NULL,
  body TEXT NOT NULL CHECK(length(body) BETWEEN 1 AND 2000),
  hidden INTEGER NOT NULL DEFAULT 0 CHECK(hidden IN (0, 1)),
  deleted INTEGER NOT NULL DEFAULT 0 CHECK(deleted IN (0, 1)),
  hidden_reason TEXT NOT NULL DEFAULT '',
  revision INTEGER NOT NULL DEFAULT 1,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  last_mutation TEXT NOT NULL,
  UNIQUE(account_id, operation_id)
);
CREATE INDEX IF NOT EXISTS library_comments_public ON library_comments(item_id, hidden, deleted, created_at, id);
CREATE TABLE IF NOT EXISTS library_comment_reports (
  id TEXT PRIMARY KEY,
  comment_id TEXT NOT NULL REFERENCES library_comments(id) ON DELETE CASCADE,
  reporter_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  reason TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open', 'resolved')),
  action TEXT NOT NULL DEFAULT '',
  resolution TEXT NOT NULL DEFAULT '',
  resolved_by TEXT REFERENCES accounts(id) ON DELETE SET NULL,
  revision INTEGER NOT NULL DEFAULT 1,
  created_at INTEGER NOT NULL,
  resolved_at INTEGER,
  last_mutation TEXT NOT NULL,
  UNIQUE(comment_id, reporter_id)
);
CREATE INDEX IF NOT EXISTS library_comment_reports_queue ON library_comment_reports(status, created_at, id);
