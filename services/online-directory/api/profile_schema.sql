CREATE TABLE IF NOT EXISTS launcher_profiles (
  account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  public_key TEXT NOT NULL,
  identity TEXT NOT NULL,
  friend_code TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT 'initials',
  avatar_url TEXT NOT NULL DEFAULT '',
  sharing INTEGER NOT NULL DEFAULT 1,
  picture_hash TEXT NOT NULL DEFAULT '',
  picture BLOB,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS launcher_picture_hash ON launcher_profiles(picture_hash);
CREATE TABLE IF NOT EXISTS launcher_social_links (
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  public_key TEXT NOT NULL,
  identity TEXT NOT NULL,
  friend_code TEXT NOT NULL,
  updated_at INTEGER NOT NULL,
  PRIMARY KEY(account_id,public_key)
);
CREATE TABLE IF NOT EXISTS launcher_profile_challenges (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  public_key TEXT NOT NULL,
  proof_hash TEXT NOT NULL,
  expires INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS launcher_profile_limits (
  account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  window INTEGER NOT NULL,
  count INTEGER NOT NULL
);
