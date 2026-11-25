CREATE TABLE IF NOT EXISTS launcher_cloud_saves (
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  save_key TEXT NOT NULL CHECK (save_key IN (
    'offline-adventure','offline-pak-1','offline-pak-2','offline-pak-3','offline-pak-4',
    'modded-records','online-adventure','online-records','online-paks'
  )),
  revision INTEGER NOT NULL CHECK (revision > 0),
  digest TEXT NOT NULL CHECK (length(digest) = 64),
  image BLOB NOT NULL CHECK (length(image) BETWEEN 20 AND 622612),
  updated_at INTEGER NOT NULL,
  pilot INTEGER NOT NULL DEFAULT 0 CHECK (pilot IN (0,1)),
  PRIMARY KEY (account_id, pilot, save_key)
);
