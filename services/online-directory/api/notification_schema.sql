CREATE TABLE IF NOT EXISTS library_notification_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  audience TEXT NOT NULL CHECK (audience IN ('staff','owner')),
  owner_id TEXT REFERENCES accounts(id) ON DELETE CASCADE,
  request_id TEXT REFERENCES library_requests(id) ON DELETE CASCADE,
  run_id TEXT REFERENCES speedrun_runs(id) ON DELETE CASCADE,
  status TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  CHECK ((request_id IS NULL) <> (run_id IS NULL)),
  CHECK ((audience='owner') = (owner_id IS NOT NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS library_notification_mod_review
ON library_notification_events(request_id) WHERE audience='staff' AND request_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS library_notification_run_review
ON library_notification_events(run_id) WHERE audience='staff' AND run_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS library_notification_owner ON library_notification_events(owner_id,id);
CREATE INDEX IF NOT EXISTS library_notification_audience ON library_notification_events(audience,id);
CREATE INDEX IF NOT EXISTS library_notification_request ON library_notification_events(request_id);
CREATE INDEX IF NOT EXISTS library_notification_run ON library_notification_events(run_id);

CREATE TABLE IF NOT EXISTS library_notification_reads (
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  event_id INTEGER NOT NULL REFERENCES library_notification_events(id) ON DELETE CASCADE,
  PRIMARY KEY (account_id,event_id)
);
CREATE INDEX IF NOT EXISTS library_notification_reads_event ON library_notification_reads(event_id);
CREATE TABLE IF NOT EXISTS library_notification_watermarks (
  account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE PRIMARY KEY,
  owner_through INTEGER NOT NULL DEFAULT 0,
  staff_through INTEGER NOT NULL DEFAULT 0
);

CREATE TRIGGER IF NOT EXISTS library_notification_request_added
AFTER INSERT ON library_requests WHEN NEW.status='pending'
BEGIN
  INSERT INTO library_notification_events(audience,request_id,status,created_at)
    VALUES('staff',NEW.id,'pending',NEW.updated_at);
END;

CREATE TRIGGER IF NOT EXISTS library_notification_request_transition
AFTER UPDATE OF status ON library_requests WHEN OLD.status<>NEW.status
BEGIN
  INSERT INTO library_notification_events(audience,owner_id,request_id,status,created_at)
    SELECT 'owner',NEW.owner_id,NEW.id,NEW.status,NEW.updated_at WHERE NEW.owner_id IS NOT NULL;
  DELETE FROM library_notification_events WHERE audience='staff' AND request_id=NEW.id
    AND NEW.status NOT IN ('pending','review');
  INSERT OR IGNORE INTO library_notification_events(audience,request_id,status,created_at)
    SELECT 'staff',NEW.id,'pending',NEW.updated_at WHERE NEW.status='pending';
END;

CREATE TRIGGER IF NOT EXISTS library_notification_run_transition
AFTER UPDATE OF state ON speedrun_runs WHEN OLD.state<>NEW.state
BEGIN
  INSERT INTO library_notification_events(audience,owner_id,run_id,status,created_at)
    VALUES('owner',NEW.account_id,NEW.id,NEW.state,NEW.updated_at);
  DELETE FROM library_notification_events WHERE audience='staff' AND run_id=NEW.id AND NEW.state<>'verified';
  INSERT OR IGNORE INTO library_notification_events(audience,run_id,status,created_at)
    SELECT 'staff',NEW.id,'verified',NEW.updated_at WHERE NEW.state='verified'
      AND NEW.verified_ticks>0 AND length(NEW.verifier_digest)=64;
END;

-- Include the current queues when deploying to an existing library.
INSERT OR IGNORE INTO library_notification_events(audience,request_id,status,created_at)
  SELECT 'staff',r.id,r.status,r.updated_at FROM library_requests r
  JOIN accounts a ON a.id=r.owner_id AND a.status='active'
  WHERE r.status IN ('pending','review');
INSERT OR IGNORE INTO library_notification_events(audience,run_id,status,created_at)
  SELECT 'staff',r.id,'verified',r.updated_at FROM speedrun_runs r
  JOIN accounts a ON a.id=r.account_id AND a.status='active'
  WHERE r.state='verified' AND r.verified_ticks>0 AND length(r.verifier_digest)=64;

