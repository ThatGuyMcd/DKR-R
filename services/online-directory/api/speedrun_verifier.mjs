import { __dkrCommon, HttpError } from './library_api.mjs';
import { ftpReplayEnabled, replayStorage, secretValid } from './speedrun_storage.mjs';
import { ghostKey, ghostRace, validGhost, MAX_GHOST_BYTES } from './speedrun_ghost.mjs';
import { adventureRules, customPackageJoin, customRules, legacyRules, vanillaRules } from './speedrun_rules.mjs';
import {
  MAX_VIDEO_BYTES, probeHostedVideo, replayVideoDurationMatches, videoArchiveEnabled, videoRacerCredit,
} from './speedrun_video.mjs';

const { json, now, randomToken, readJSON } = __dkrCommon;
const prefix = '/v1/speedruns/verifier/';
const leaseSeconds = 360;
const fail = (status, code, message) => { throw new HttpError(status, message, code); };
const one = (db, sql, ...args) => db.prepare(sql).bind(...args).first();
const key = id => 'runs/' + id + '.dkrreplay';
const encoder = new TextEncoder();
const hash = async value => new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(value)));
const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
const validId = value => typeof value === 'string' && /^[A-Za-z0-9_-]{24}$/.test(value);
const matchesRom = (proof, manifest) => (proof.rom ?? 80) === manifest.rom;

async function authorized(request, env) {
  const supplied = request.headers.get('Authorization') || '';
  if (!secretValid(env.SPEEDRUN_VERIFIER_SECRET) || !/^Bearer [a-f0-9]{64}$/.test(supplied)) return false;
  const [actual, expected] = await Promise.all([hash(supplied.slice(7)), hash(env.SPEEDRUN_VERIFIER_SECRET)]);
  let mismatch = 0;
  for (let i = 0; i < expected.length; i++) mismatch |= actual[i] ^ expected[i];
  return mismatch === 0;
}

function replayTrack(db, id) {
  return one(db, `SELECT l.file_url AS url,q.release_sha256 AS sha256,i.bytes,
    q.track_id AS trackId,q.track_sha256 AS trackHash
    FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
    ${customPackageJoin} JOIN library_releases l ON l.id=i.release_id
    WHERE r.id=? AND b.kind='custom' AND b.ruleset IN (?,?)`, id, ...customRules);
}

async function videoJob(db, env, operation, input) {
  if (!videoArchiveEnabled(env)) fail(503, 'video_unavailable', 'The hosted video archive is unavailable.');
  const eligible = "r.state IN ('verified','published') AND a.status='active' AND r.verified_ticks>0 AND length(r.verifier_digest)=64";
  if (operation === 'video-claim') {
    if (input.queue !== undefined && !['race','adventure'].includes(input.queue))
      fail(400, 'video_queue', 'Choose a video queue.');
    const adventure = input.queue === 'adventure';
    const kind = adventure ? "b.kind='full-game'" : "b.kind IN ('vanilla','custom')";
    await db.prepare(`INSERT OR IGNORE INTO speedrun_video_jobs(run_id)
      SELECT r.id FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
      JOIN accounts a ON a.id=r.account_id WHERE ${eligible} AND ${kind}
        AND NOT EXISTS (SELECT 1 FROM speedrun_video_assets v WHERE v.run_id=r.id)
        AND NOT EXISTS (SELECT 1 FROM speedrun_video_jobs j WHERE j.run_id=r.id)
      ORDER BY r.state='verified' DESC,r.created_at,r.id LIMIT 64`).run();
    const deletions = adventure ? [] : (await db.prepare('SELECT run_id AS id,video_sha256 AS sha FROM speedrun_video_deletions LIMIT 16').all()).results;
    const claim = randomToken(24);
    const job = await db.prepare(`UPDATE speedrun_video_jobs SET claim=?,lease_until=unixepoch()+3600,attempts=attempts+1
      WHERE run_id=(SELECT j.run_id FROM speedrun_video_jobs j JOIN speedrun_runs r ON r.id=j.run_id
        JOIN speedrun_boards b ON b.id=r.board_id JOIN accounts a ON a.id=r.account_id
        WHERE ${kind} AND j.rendered_at IS NULL AND j.retry_at<=unixepoch()
        AND j.lease_until<=unixepoch() AND ${eligible}
        ORDER BY r.state='verified' DESC,r.created_at,r.id LIMIT 1) RETURNING run_id`).bind(claim).first();
    const run = job && await one(db, `SELECT r.sha256,r.manifest,COALESCE(c.credited_name,a.display_name) AS name
      FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      LEFT JOIN speedrun_capture_credits c ON c.run_id=r.id WHERE r.id=? AND ${eligible}`,job.run_id);
    const track = run && await replayTrack(db,job.run_id);
    return json({job:run ? {id:job.run_id,claim,sha256:run.sha256,ticks:JSON.parse(run.manifest).ticks,
      racerCredit:videoRacerCredit(run.name),
      ...(track ? {track} : {})} : null,deletions});
  }
  if (!validId(input.id)) fail(400, 'video_job', 'A valid video job is required.');
  if (operation === 'video-probe') {
    if (!secretValid(input.sha) || !Number.isSafeInteger(input.bytes) || input.bytes<1 || input.bytes>MAX_VIDEO_BYTES)
      fail(400, 'video_digest', 'A valid hosted movie is required.');
    const run = await one(db, `SELECT r.sha256 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      WHERE r.id=? AND ${eligible}`,input.id);
    if (!run) fail(404, 'video_job', 'The verified run is unavailable.');
    try {
      const ready = await probeHostedVideo(env.SPEEDRUN_FTP_VIDEO_KEY,
        {id:input.id,replaySha256:run.sha256,videoSha256:input.sha,size:input.bytes});
      return json({ready});
    } catch (error) {
      return json({ready:false,issue:error.name,details:error.message},502);
    }
  }
  if (operation === 'video-prune') {
    if (!secretValid(input.sha)) fail(400, 'video_digest', 'A video digest is required.');
    await db.prepare('DELETE FROM speedrun_video_deletions WHERE run_id=? AND video_sha256=?').bind(input.id,input.sha).run();
    return json({removed:true});
  }
  if (typeof input.claim !== 'string' || !/^[A-Za-z0-9_-]{32}$/.test(input.claim))
    fail(400, 'video_job', 'A video claim is required.');
  if (operation === 'video-renew') {
    const result = await db.prepare(`UPDATE speedrun_video_jobs SET lease_until=unixepoch()+3600
      WHERE run_id=? AND claim=? AND lease_until>unixepoch() AND rendered_at IS NULL
      AND EXISTS(SELECT 1 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      WHERE r.id=speedrun_video_jobs.run_id AND ${eligible})`)
      .bind(input.id,input.claim).run();
    if (!result.meta.changes) fail(409, 'video_job', 'The video claim expired.');
    return json({renewed:true});
  }
  if (operation === 'video-fail') {
    await db.prepare(`UPDATE speedrun_video_jobs SET claim=NULL,lease_until=0,retry_at=unixepoch()+MIN(3600,60* (1 << MIN(attempts-1,6)))
      WHERE run_id=? AND claim=?`).bind(input.id,input.claim).run();
    return json({retry:true});
  }
  if (!['video-evidence','video-complete'].includes(operation)) fail(404, 'not_found', 'This video route does not exist.');
  const run = await one(db, `SELECT r.sha256,r.manifest,r.verified_ticks,r.verified_best_ticks,b.kind,
      COALESCE(c.credited_name,a.display_name) AS name
    FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id JOIN speedrun_boards b ON b.id=r.board_id
    LEFT JOIN speedrun_capture_credits c ON c.run_id=r.id
    JOIN speedrun_video_jobs j ON j.run_id=r.id
    WHERE r.id=? AND j.claim=? AND j.lease_until>unixepoch() AND ${eligible}`,input.id,input.claim);
  if (operation === 'video-evidence') {
    if (!run) fail(409, 'video_job', 'The video claim expired.');
    return replayEvidence(env, input.id, run.sha256);
  }
  // A rejection can race with the renderer finishing its FTPS upload.
  const proof = input.proof;
  const discardUnavailableMovie = async () => {
    if (!secretValid(proof?.videoSha256)) return;
    await db.prepare(`INSERT OR IGNORE INTO speedrun_video_deletions(run_id,video_sha256)
      SELECT ?,? WHERE NOT EXISTS (SELECT 1 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
        WHERE r.id=? AND ${eligible})
      AND NOT EXISTS (SELECT 1 FROM speedrun_video_assets WHERE run_id=? AND video_sha256=?)`)
      .bind(input.id,proof.videoSha256,input.id,input.id,proof.videoSha256).run();
  };
  if (!run) await discardUnavailableMovie();
  const manifest = run && JSON.parse(run.manifest);
  if (!run || !proof || proof.replaySha256!==run.sha256 || proof.runtimeSha256!==input.runtime ||
      !matchesRom(proof,manifest) ||
      proof.clockTicks!==run.verified_ticks ||
      (run.kind==='full-game' ? proof.category!==manifest.category || proof.videoSplitsOverlay!=='adventure-live-v1' :
        proof.bestLapTicks!==run.verified_best_ticks) ||
      proof.updates!==(manifest.finishTick ?? manifest.ticks) || proof.videoInputOverlay!=='n64-v1' ||
      !replayVideoDurationMatches(proof,manifest.ticks) ||
      (proof.videoBranding==='dkr-r-logo-v2' && proof.videoRacerCredit!==videoRacerCredit(run.name)) ||
      JSON.stringify(proof.checkpoints)!==JSON.stringify(manifest.checkpoints) ||
      JSON.stringify(proof.splits)!==JSON.stringify(manifest.splits) || !secretValid(proof.videoSha256) ||
      !Number.isSafeInteger(input.bytes) || input.bytes<1 || input.bytes>MAX_VIDEO_BYTES)
    fail(409, 'video_proof', 'The movie does not match this verified replay.');
  let ready;
  try {
    ready = await probeHostedVideo(env.SPEEDRUN_FTP_VIDEO_KEY,
      {id:input.id,replaySha256:run.sha256,videoSha256:proof.videoSha256,size:input.bytes});
  } catch (error) {
    console.error('Hosted video archive check failed:',error.name,error.message);
    fail(502, 'video_archive_fetch', 'The encrypted video could not be read.');
  }
  if (!ready)
    fail(409, 'video_archive', 'The encrypted video is unavailable.');
  const results = await db.batch([
    db.prepare(`INSERT OR IGNORE INTO speedrun_video_assets(run_id,replay_sha256,video_sha256,bytes)
      SELECT r.id,r.sha256,?,? FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      JOIN speedrun_video_jobs j ON j.run_id=r.id WHERE r.id=? AND r.sha256=? AND j.claim=?
        AND j.lease_until>unixepoch() AND ${eligible}`)
      .bind(proof.videoSha256,input.bytes,input.id,run.sha256,input.claim),
    db.prepare(`UPDATE speedrun_video_jobs SET rendered_at=unixepoch(),claim=NULL,lease_until=0
      WHERE run_id=? AND claim=? AND EXISTS(SELECT 1 FROM speedrun_video_assets WHERE run_id=?)`)
      .bind(input.id,input.claim,input.id),
  ]);
  if (!results[1].meta.changes) {
    await discardUnavailableMovie();
    fail(409, 'video_job', 'The video claim expired.');
  }
  return json({ready:true});
}

async function replayEvidence(env, id, digest) {
  const object = await replayStorage(env).get(key(id));
  if (!object) fail(503, 'archive_missing', 'The claimed replay is unavailable.');
  return new Response(object.body, { headers:{
    'Content-Type':'application/octet-stream', 'X-DKRR-Replay-SHA256':digest, 'Cache-Control':'no-store',
  } });
}

export async function verifierReady(db, env) {
  if (!ftpReplayEnabled(env)) return true;
  if (!secretValid(env.SPEEDRUN_VERIFIER_RUNTIME) || !secretValid(env.SPEEDRUN_VERIFIER_SECRET)) return false;
  const row = await one(db, 'SELECT seen FROM speedrun_verifier_health WHERE id=1 AND runtime=?', env.SPEEDRUN_VERIFIER_RUNTIME);
  return !!row && row.seen >= now() - 150;
}

export async function adventureVerifierReady(db, env) {
  if (!secretValid(env.SPEEDRUN_VERIFIER_RUNTIME) || !secretValid(env.SPEEDRUN_VERIFIER_SECRET)) return false;
  const row = await one(db, 'SELECT seen FROM speedrun_adventure_verifier_health WHERE runtime=?', env.SPEEDRUN_VERIFIER_RUNTIME);
  return !!row && row.seen >= now() - 150;
}

function updateHealth(db, runtime, stamp, adventure) {
  return db.prepare(adventure ? 'INSERT OR REPLACE INTO speedrun_adventure_verifier_health(runtime,seen) VALUES(?,?)' :
    'INSERT OR REPLACE INTO speedrun_verifier_health(id,runtime,seen) VALUES(1,?,?)').bind(runtime, stamp);
}

export async function handleSpeedrunVerifier(request, env, db, path) {
  if (!path.startsWith(prefix)) return null;
  const operation = path.slice(prefix.length);
  if (request.method !== 'POST' || request.headers.has('Origin') || !await authorized(request, env))
    fail(404, 'not_found', 'This speedrun page does not exist.');
  const storage = replayStorage(env);
  const video = operation.startsWith('video-');
  const runtime = video && env.SPEEDRUN_VIDEO_RUNTIME !== undefined ? env.SPEEDRUN_VIDEO_RUNTIME : env.SPEEDRUN_VERIFIER_RUNTIME;
  if (!storage || !secretValid(runtime))
    fail(503, 'verifier_unavailable', 'Replay verification is unavailable.');
  const input = await readJSON(request, operation === 'finish' ? 65536 + 4 * Math.ceil(MAX_GHOST_BYTES / 3) :
    operation === 'video-complete' ? 65536 : 2048);
  if (input.runtime !== runtime)
    fail(409, 'verifier_runtime', 'This verifier build is not configured.');
  if (video) return videoJob(db, env, operation, input);

  if (operation === 'probe') {
    // Test the configured storage without creating a public run.
    const token = 'probe_' + randomToken(18).slice(0, 18);
    const bytes = encoder.encode('DKR-R encrypted archive round trip ' + token);
    const archived = key(token);
    let stage = 'write';
    let issue = null;
    try {
      await storage.put(archived, bytes);
      stage = 'read';
      const object = await storage.get(archived);
      if (new TextDecoder().decode(await object?.arrayBuffer()) !== new TextDecoder().decode(bytes))
        throw new Error('Archive bytes differ.');
    } catch (error) {
      issue = { stage, code: typeof error.code === 'string' ? error.code : 'storage_error' };
    }
    try {
      await storage.delete([archived]);
    } catch (error) {
      if (!issue) issue = { stage:'cleanup', code:typeof error.code === 'string' ? error.code : 'storage_error' };
    }
    if (issue) return json({ ready:false, ...issue }, 502);
    return json({ ready:true });
  }
  const verifiedTimeTrialState = "r.state IN ('verified','published')";
  if (operation === 'poll') {
    const stamp = now();
    const claim = randomToken(18);
    if (input.queue !== undefined && !['race','adventure'].includes(input.queue))
      fail(400, 'verifier_queue', 'Choose a verification queue.');
    const adventure = input.queue === 'adventure';
    const backfill = input.backfill;
    if (backfill !== undefined && (!validId(backfill) || adventure))
      fail(400, 'verifier_backfill', 'Choose a verified Time Trial run to backfill.');
    const eligible = backfill ? "r.id=? AND " + verifiedTimeTrialState +
      " AND r.verified_ticks>0 AND length(r.verifier_digest)=64" : "r.state='queued'";
    const kind = adventure ? "b.kind='full-game' AND b.ruleset=?" :
      "((b.kind='vanilla' AND b.ruleset IN (?,?)) OR (b.kind='custom' AND b.ruleset IN (?,?) AND i.release_id IS NOT NULL))";
    const rules = adventure ? [adventureRules] : [legacyRules, vanillaRules, ...customRules];
    const result = await db.batch([
      updateHealth(db, input.runtime, stamp, adventure),
      db.prepare(`INSERT INTO speedrun_verifier_claims(run_id,claim,until)
        SELECT r.id,?,? FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
        ${customPackageJoin}
        JOIN accounts a ON a.id=r.account_id
        LEFT JOIN speedrun_verifier_claims c ON c.run_id=r.id
        WHERE (${eligible}) AND a.status='active' AND (c.until IS NULL OR c.until<=?)
          AND (${kind})
        ORDER BY COALESCE(c.until,0),r.created_at,r.id LIMIT 1
        ON CONFLICT(run_id) DO UPDATE SET claim=excluded.claim,until=excluded.until
        RETURNING run_id`).bind(claim, stamp + leaseSeconds, ...(backfill ? [backfill] : []), stamp, ...rules),
    ]);
    const id = result[1].results[0]?.run_id;
    if (!id) return json({ job:null });
    const track = await replayTrack(db,id);
    return json({ job:{ id, claim, ...(track ? { track } : {}) } });
  }

  if (!validId(input.id) || !validId(input.claim))
    fail(400, 'verifier_claim', 'A replay claim is required.');
  if (input.backfill !== undefined && input.backfill !== true)
    fail(400, 'verifier_backfill', 'Invalid backfill claim.');
  const state = input.backfill ? verifiedTimeTrialState : "r.state='queued'";
  if (operation === 'renew') {
    const stamp = now();
    const renewal = await db.prepare(`UPDATE speedrun_verifier_claims SET until=?
      WHERE run_id=? AND claim=? AND until>?
      AND EXISTS (SELECT 1 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
                  WHERE r.id=speedrun_verifier_claims.run_id AND ${state} AND a.status='active')`)
      .bind(stamp + leaseSeconds, input.id, input.claim, stamp).run();
    if (!renewal.meta.changes) fail(409, 'verifier_claim', 'This verification claim is no longer active.');
    const kind = await one(db, `SELECT b.kind FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
      WHERE r.id=?`, input.id);
    await updateHealth(db, input.runtime, stamp, kind?.kind === 'full-game').run();
    return json({ renewed:true });
  }

  const run = await one(db, `SELECT r.manifest,r.sha256,r.verified_ticks,r.verified_best_ticks FROM speedrun_runs r
    JOIN speedrun_verifier_claims c ON c.run_id=r.id
    WHERE r.id=? AND ${state} AND c.claim=? AND c.until>?`, input.id, input.claim, now());
  if (!run) fail(409, 'verifier_claim', 'This verification claim is no longer active.');
  if (operation === 'evidence') return replayEvidence(env, input.id, run.sha256);
  if (operation === 'finish') {
    let result;
    const claimed = ' AND EXISTS(SELECT 1 FROM speedrun_verifier_claims WHERE run_id=? AND claim=? AND until>unixepoch())';
    if (input.rejected === true) {
      if (input.backfill) fail(409, 'verifier_backfill', 'A backfill cannot change the verified run.');
      result = await db.prepare(`UPDATE speedrun_runs SET state='rejected',updated_at=unixepoch()
        WHERE id=? AND state='queued'` + claimed).bind(input.id, input.id, input.claim).run();
    } else {
      const proof = input.proof;
      const manifest = JSON.parse(run.manifest);
      const adventure = manifest.ruleset === adventureRules;
      const splits = manifest.splits;
      const bestLap = adventure ? null : Math.min(...splits.map((end, i) => end - (i ? splits[i - 1] : 0)));
      if (!proof || proof.replaySha256 !== run.sha256 || proof.runtimeSha256 !== input.runtime ||
          !matchesRom(proof, manifest) ||
          proof.clockTicks !== manifest.clockTicks || proof.updates !== (manifest.finishTick ?? manifest.ticks) ||
          (input.backfill && (proof.clockTicks !== run.verified_ticks || bestLap !== run.verified_best_ticks)) ||
          (adventure ? proof.category !== manifest.category || JSON.stringify(proof.splits) !== JSON.stringify(splits) :
            proof.laps !== splits.length || proof.bestLapTicks !== bestLap || JSON.stringify(proof.splits) !== JSON.stringify(splits)) ||
          JSON.stringify(proof.checkpoints) !== JSON.stringify(manifest.checkpoints) ||
          proof.finalCheckpoint !== manifest.checkpoints.at(-1)[1] ||
          (manifest.ruleset === vanillaRules && (proof.course !== manifest.course || proof.vehicle !== manifest.vehicle)) ||
          (customRules.includes(manifest.ruleset) && (proof.trackId !== manifest.trackId || proof.trackHash !== manifest.trackHash ||
            proof.vehicle !== manifest.vehicle || proof.releaseSha256 !== manifest.releaseHash)))
        fail(409, 'verifier_proof', 'This proof does not match the recorded race.');
      if (input.ghost !== undefined || input.backfill || (ghostRace(manifest) && videoArchiveEnabled(env))) {
        let bytes;
        if (typeof input.ghost === 'string' && input.ghost.length <= 4 * Math.ceil(MAX_GHOST_BYTES / 3)) {
          try { bytes = Uint8Array.from(atob(input.ghost), char => char.charCodeAt(0)); } catch { /* Invalid data. */ }
        }
        if (!bytes || !await validGhost(bytes, run.sha256, manifest))
          fail(409, 'verifier_ghost', 'The prepared ghost does not match the verified replay.');
        try {
          await storage.put(ghostKey(input.id), bytes);
        } finally {
          // A run deleted during the upload must not leave either archive orphaned.
          await db.prepare(`INSERT OR REPLACE INTO speedrun_replay_deletions(run_id,removed_at)
            SELECT ?,unixepoch() WHERE NOT EXISTS(SELECT 1 FROM speedrun_runs WHERE id=?)`)
            .bind(input.id, input.id).run();
        }
      }
      if (input.backfill) {
        result = await db.prepare(`DELETE FROM speedrun_verifier_claims WHERE run_id=? AND claim=? AND until>unixepoch()`)
          .bind(input.id, input.claim).run();
      } else {
        result = await db.prepare(`UPDATE speedrun_runs SET state='verified',updated_at=unixepoch(),
          verified_ticks=?,verified_best_ticks=?,verifier_digest=? WHERE id=? AND sha256=? AND state='queued'` + claimed)
          .bind(proof.clockTicks, bestLap, hex(await hash(JSON.stringify(proof))), input.id, run.sha256,
            input.id, input.claim).run();
      }
    }
    if (!result.meta.changes) fail(409, 'verifier_claim', 'This verification claim is no longer active.');
    if (!input.backfill)
      await db.prepare('DELETE FROM speedrun_verifier_claims WHERE run_id=? AND claim=?').bind(input.id, input.claim).run();
    return json({ saved:true });
  }
  fail(404, 'not_found', 'This speedrun page does not exist.');
}

