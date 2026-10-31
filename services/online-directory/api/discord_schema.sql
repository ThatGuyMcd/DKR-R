CREATE TABLE IF NOT EXISTS launcher_discord_profiles (
  account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  display_name TEXT NOT NULL,
  avatar_url TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);
