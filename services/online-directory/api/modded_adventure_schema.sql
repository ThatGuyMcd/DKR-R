CREATE TABLE IF NOT EXISTS launcher_modded_adventure (
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  save_key TEXT NOT NULL CHECK (save_key = 'modded-adventure'),
  revision INTEGER NOT NULL CHECK (revision > 0),
  digest TEXT NOT NULL CHECK (length(digest) = 64),
  image BLOB NOT NULL CHECK (length(image) = 512),
  updated_at INTEGER NOT NULL,
  pilot INTEGER NOT NULL DEFAULT 0 CHECK (pilot IN (0,1)),
  PRIMARY KEY (account_id, pilot, save_key)
);
