-- Additive website workflow metadata. Existing catalogue/FTP tables are retained.
CREATE TABLE IF NOT EXISTS library_workflow_batches (
    id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    content_json TEXT NOT NULL,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0),
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS library_workflow_batches_owner
    ON library_workflow_batches(owner_id, updated_at DESC);
