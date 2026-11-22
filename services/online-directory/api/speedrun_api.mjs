import { __dkrAuth, __dkrCommon, HttpError, finish } from './library_api.mjs';
import { readReplayBody } from './speedrun_replay.mjs';
import { parseReplay, replayId } from '../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js';

const { json, requireOrigin, requireRole, safeText, readJSON, randomToken, now } = __dkrCommon;
const fail = (status, code, message) => { throw new HttpError(status, message, code); };
const all = async (db, sql, ...args) => (await db.prepare(sql).bind(...args).all()).results;
const one = (db, sql, ...args) => db.prepare(sql).bind(...args).first();
const submitEnabled = env => env.SPEEDRUN_UPLOADS_ENABLED === 'true' && !!env.SPEEDRUN_REPLAYS;
const key = id => 'runs/' + id + '.dkrreplay';
const verified = "r.verified_ticks > 0 AND length(r.verifier_digest)=64";
const visible = "r.state='published' AND a.status='active' AND " + verified;
const validRelease = "(b.kind<>'custom' OR EXISTS (SELECT 1 FROM launcher_release_integrity i WHERE i.release_id=b.release_id AND i.sha256=b.release_sha256))";

function board(row) {
  return { id: row.id, kind: row.kind, title: row.title, category: row.category, vehicle: row.vehicle,
    ruleset: row.ruleset, releaseId: row.release_id, releaseHash: row.release_sha256 };
}

async function catalogue(db, env, url) {
  const cursor = url.searchParams.get('cursor') || '';
  if (cursor && !replayId(cursor)) fail(400, 'cursor', 'Invalid track page.');
  const rows = await all(db, `SELECT i.id,i.title,r.id AS release_id,r.version,h.sha256
    FROM library_items i JOIN library_releases r ON r.id=i.current_release_id
    LEFT JOIN launcher_release_integrity h ON h.release_id=r.id
    WHERE i.category='tracks' AND i.archived=0 AND i.id>? ORDER BY i.id LIMIT 41`, cursor);
  const boards = await all(db, `SELECT b.* FROM speedrun_boards b WHERE ${validRelease} ORDER BY b.title,b.category,b.vehicle LIMIT 2000`);
  return json({ canSubmit: submitEnabled(env) && boards.length > 0,
    boards: cursor ? [] : boards.map(board),
    customTracks: rows.slice(0,40).map(row => ({ id: row.id, title: row.title, releaseId: row.release_id,
      version: row.version, qualified: !!row.sha256 && boards.some(b => b.release_id === row.release_id && b.release_sha256 === row.sha256) })),
    nextCursor: rows.length > 40 ? rows[39].id : '' });
}

async function ranking(db, id) {
  const info = await one(db, `SELECT b.* FROM speedrun_boards b WHERE b.id=? AND ${validRelease}`, id);
  if (!info) fail(404, 'board_missing', 'That leaderboard is unavailable.');
  const rows = await all(db, `WITH eligible AS (
      SELECT r.id,r.account_id,r.verified_ticks,r.created_at,a.display_name,
        ROW_NUMBER() OVER (PARTITION BY r.account_id ORDER BY r.verified_ticks,r.created_at,r.id) AS personal_order
      FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      WHERE r.board_id=? AND ${visible}
    ) SELECT id,display_name,verified_ticks,
        RANK() OVER (ORDER BY verified_ticks) AS place
      FROM eligible WHERE personal_order=1 ORDER BY verified_ticks,created_at,id LIMIT 100`, id);
  return json({ board: board(info), entries: rows.map(row => ({ id: row.id, name: row.display_name, ticks: row.verified_ticks, place: row.place })) });
}

async function enqueue(request, env, db) {
  if (!submitEnabled(env)) fail(503, 'pilot_closed', 'Run submissions are closed while replay verification is being tested.');
  const user = await __dkrAuth.requireUser(request, env);
  const bytes = await readReplayBody(request);
  let manifest;
  try { if (bytes) manifest = parseReplay(bytes); } catch { /* Invalid replay evidence. */ }
  if (!manifest) fail(400, 'invalid_replay', 'Choose a valid DKR-R replay under 1 MiB.');
  const match = await one(db, `SELECT b.* FROM speedrun_boards b WHERE b.id=? AND ${validRelease}`, manifest.board);
  if (!match || match.engine !== manifest.engine || match.ruleset !== manifest.ruleset ||
      match.release_sha256 !== manifest.releaseHash) fail(409, 'board_mismatch', 'This build or track version is not qualified for that board.');

  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), value => value.toString(16).padStart(2,'0')).join('');
  const stamp = now();
  const existing = () => one(db, 'SELECT id,state FROM speedrun_runs WHERE account_id=? AND sha256=?', user.id, hash);
  let run = await existing();
  if (!run) {
    await db.prepare(`INSERT OR IGNORE INTO speedrun_runs(id,account_id,board_id,sha256,manifest,notes,state,created_at,updated_at)
      SELECT ?,?,?,?,?,?,'uploading',?,?
      WHERE (SELECT COUNT(*) FROM speedrun_runs WHERE account_id=? AND created_at>?)<10
        AND (SELECT COUNT(*) FROM speedrun_runs WHERE state IN ('uploading','queued','verified'))<1000`)
      .bind(randomToken(18), user.id, match.id, hash, JSON.stringify(manifest),
        safeText(manifest.notes, 600, 'Notes'), stamp, stamp, user.id, stamp - 86400).run();
    run = await existing();
  }
  if (!run) fail(429, 'queue_limit', 'The submission limit was reached. Try again later.');
  if (run.state === 'uploading') {
    await env.SPEEDRUN_REPLAYS.put(key(run.id), bytes, { httpMetadata: { contentType: 'application/octet-stream' } });
    await db.prepare("UPDATE speedrun_runs SET state='queued',updated_at=? WHERE id=? AND state='uploading'").bind(now(), run.id).run();
    run = await existing();
  }
  return json({ run }, 202);
}

async function download(request, env, db, id) {
  const row = await one(db, `SELECT r.*,a.status AS account_status,(${visible}) AS downloadable
    FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id WHERE r.id=?`, id);
  if (!row || row.account_status !== 'active' || row.state === 'uploading')
    fail(404, 'replay_missing', 'This replay is unavailable.');
  if (!row.downloadable) {
    const user = request.headers.has('Authorization') ? await __dkrAuth.requireUser(request, env) : null;
    if (!user || user.id !== row.account_id && !['admin','moderator'].includes(user.role))
      fail(404, 'replay_missing', 'This replay is unavailable.');
  }
  const object = await env.SPEEDRUN_REPLAYS?.get(key(id));
  if (!object) fail(404, 'replay_missing', 'This replay is unavailable.');
  return new Response(object.body, { headers: { 'Content-Type':'application/octet-stream',
    'Content-Disposition': 'attachment; filename="DKR-R-' + id + '.dkrreplay"' } });
}

async function myRuns(request, env, db, review) {
  const user = await __dkrAuth.requireUser(request, env);
  if (review) requireRole(user, 'moderator');
  const rows = await all(db, `SELECT r.id,r.state,r.verified_ticks,r.notes,r.created_at,r.manifest,
      b.title,b.category,b.vehicle,a.display_name,m.reason
    FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id JOIN accounts a ON a.id=r.account_id
    LEFT JOIN speedrun_moderation m ON m.run_id=r.id
    WHERE ${review ? "r.state='verified' AND a.status='active' AND " + verified : "r.account_id=?"}
    ORDER BY r.created_at DESC,r.id DESC LIMIT 50`, ...(review ? [] : [user.id]));
  return json({ runs: rows.map(row => {
    const { rom, platform, arch, clockTicks } = JSON.parse(row.manifest);
    return { id: row.id, state: row.state, ticks: row.verified_ticks, claimedTicks: clockTicks,
      title: row.title, category: row.category, vehicle: row.vehicle, notes: row.notes,
      name: row.display_name, createdAt: row.created_at, rom, platform, arch, reason: row.reason };
  }) });
}

async function moderate(request, env, db, id) {
  const user = await __dkrAuth.requireUser(request, env);
  requireRole(user, 'moderator');
  const body = await readJSON(request, 4096);
  const decision = body.decision;
  if (!['published','rejected'].includes(decision)) fail(400, 'decision', 'Choose Publish or Reject.');
  const reason = safeText(body.reason, 600, 'Review note', decision === 'rejected');
  const result = await db.prepare(`INSERT OR IGNORE INTO speedrun_moderation(run_id,moderator_id,decision,reason,decided_at)
    SELECT r.id,?,?,?,? FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
    JOIN accounts a ON a.id=r.account_id WHERE r.id=? AND r.state='verified'
      AND a.status='active' AND ${verified} AND ${validRelease}`).bind(user.id, decision, reason, now(), id).run();
  if (!result.meta.changes) fail(409, 'review_unavailable', 'This run is not independently verified or was already reviewed.');
  return json({ saved: true });
}

// The signed-in routes hide deleted accounts immediately. Scheduled cleanup
// removes their private replay bytes and old rejected submissions in batches.
export async function cleanupSpeedruns(env) {
  if (!env.DB || !env.SPEEDRUN_REPLAYS) return;
  const db = env.DB.withSession ? env.DB.withSession('first-primary') : env.DB;
  const expired = await all(db, `SELECT r.id FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE a.status='deleted' OR (r.state='rejected' AND r.updated_at<?)
    ORDER BY r.updated_at LIMIT 50`, now() - 30 * 86400);
  if (!expired.length) return;
  await env.SPEEDRUN_REPLAYS.delete(expired.map(row => key(row.id)));
  const ids = expired.map(row => row.id);
  await db.prepare(`DELETE FROM speedrun_runs WHERE id IN (${ids.map(() => '?').join(',')})`).bind(...ids).run();
}

export async function handleSpeedrun(request, env) {
  const url = new URL(request.url);
  const path = url.pathname;
  if (path !== '/v1/speedruns' && !path.startsWith('/v1/speedruns/')) return null;
  let response;
  try {
    if (url.protocol !== 'https:') fail(400, 'https', 'Use HTTPS.');
    const db = env.DB?.withSession ? env.DB.withSession('first-primary') : env.DB;
    if (!db) fail(503, 'unavailable', 'The leaderboards are temporarily unavailable.');
    const { method } = request;
    const run = path.match(/^\/v1\/speedruns\/runs\/([A-Za-z0-9_-]{1,96})\/(replay|review)$/);
    const boardPath = path.match(/^\/v1\/speedruns\/boards\/([A-Za-z0-9_-]{1,96})$/);
    if (method === 'OPTIONS') {
      requireOrigin(request, env);
      response = new Response(null, { status: 204 });
    } else if (method === 'GET' && path === '/v1/speedruns') response = await catalogue(db, env, url);
    else if (method === 'GET' && boardPath) response = await ranking(db, boardPath[1]);
    else if (method === 'GET' && path === '/v1/speedruns/mine') response = await myRuns(request, env, db, false);
    else if (method === 'GET' && path === '/v1/speedruns/review') response = await myRuns(request, env, db, true);
    else if (method === 'GET' && run?.[2] === 'replay') response = await download(request, env, db, run[1]);
    else if (method === 'POST') {
      // Native clients use the same bearer-token session without an Origin.
      if (request.headers.has('Origin')) requireOrigin(request, env);
      if (path === '/v1/speedruns/runs') response = await enqueue(request, env, db);
      else if (run?.[2] === 'review') response = await moderate(request, env, db, run[1]);
    }
    if (!response) fail(404, 'not_found', 'This speedrun page does not exist.');
  } catch (error) {
    const known = error instanceof HttpError;
    response = json({ error: { code: known ? error.code : 'unavailable',
      message: known ? error.message : 'The leaderboards are temporarily unavailable.' } }, known ? error.status : 503);
  }
  return finish(response, request, env);
}
