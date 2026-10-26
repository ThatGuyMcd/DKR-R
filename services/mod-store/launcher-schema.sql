CREATE TABLE IF NOT EXISTS launcher_flows (
  flow_hash TEXT PRIMARY KEY,
  client_challenge TEXT NOT NULL,
  client_state TEXT NOT NULL UNIQUE,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  last_poll INTEGER NOT NULL DEFAULT 0,
  grant_cipher TEXT,
  error TEXT NOT NULL DEFAULT '',
  consumed INTEGER NOT NULL DEFAULT 0 CHECK(consumed IN (0,1))
);
CREATE INDEX IF NOT EXISTS launcher_flows_expiry ON launcher_flows(expires_at);
CREATE TABLE IF NOT EXISTS launcher_release_integrity (
  release_id TEXT PRIMARY KEY REFERENCES library_releases(id) ON DELETE CASCADE,
  sha256 TEXT NOT NULL CHECK(length(sha256)=64),
  bytes INTEGER NOT NULL CHECK(bytes>0 AND bytes<=2147483648),
  verified_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS launcher_asset_integrity (
  file_url TEXT PRIMARY KEY,
  sha256 TEXT NOT NULL CHECK(length(sha256)=64),
  bytes INTEGER NOT NULL CHECK(bytes>0 AND bytes<=2147483648),
  verified_at INTEGER NOT NULL
);
