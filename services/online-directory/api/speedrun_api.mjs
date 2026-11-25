import { __dkrAuth, __dkrCommon, HttpError, finish } from './library_api.mjs';
import { readReplayBody } from './speedrun_replay.mjs';
import { ftpReplayEnabled, replayStorage } from './speedrun_storage.mjs';
import { ghostKey, ghostRace, validGhost } from './speedrun_ghost.mjs';
import { runVideo, serveHostedVideo, serveRunVideo, videoArchiveEnabled, videoKey } from './speedrun_video.mjs';
import { adventureVerifierReady, handleSpeedrunVerifier, verifierReady } from './speedrun_verifier.mjs';
import { vanillaTracks } from './speedrun_tracks.mjs';
import { overallRankings, recordHistory } from './speedrun_rankings.mjs';
import { adventureCatalogue } from '../../../runtime-recomp/src/game/mod_library/website/assets/adventure-catalogue.js';
import { adventureBoard, boardColumns, customBoard, customPackageJoin, legacyRules, matchesRankedBoard, vanillaBoard } from './speedrun_rules.mjs';
import { ancientLakeRacers, parseReplay, rankedTicket, replayId } from '../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js';

const { json, requireOrigin, requireRole, safeText, readJSON, randomToken, now, sha256 } = __dkrCommon;
const fail = (status, code, message) => { throw new HttpError(status, message, code); };
const submissionLimit = { maxRuns:25, windowHours:24 };
const submissionWindowSeconds = submissionLimit.windowHours * 3600;
const maxPendingRuns = 1000;
const all = async (db, sql, ...args) => (await db.prepare(sql).bind(...args).all()).results;
const one = (db, sql, ...args) => db.prepare(sql).bind(...args).first();
async function recentSubmissionExpiries(db, account, stamp) {
  const rows = await all(db, `SELECT created_at FROM speedrun_runs WHERE account_id=? AND created_at>?
    ORDER BY created_at DESC LIMIT ?`, account, stamp - submissionWindowSeconds, submissionLimit.maxRuns);
  return rows.map(row => row.created_at + submissionWindowSeconds);
}
const submitEnabled = async (db, env) => env.SPEEDRUN_UPLOADS_ENABLED === 'true' &&
  typeof env.SPEEDRUN_TICKET_SECRET === 'string' && env.SPEEDRUN_TICKET_SECRET.length >= 32 &&
  !!replayStorage(env) && await verifierReady(db, env);
const localClaimsEnabled = env => env.SPEEDRUN_LOCAL_CLAIMS_ENABLED === 'true';
const encoder = new TextEncoder();
const hex = bytes => Array.from(new Uint8Array(bytes), value => value.toString(16).padStart(2, '0')).join('');
const signingKey = env => crypto.subtle.importKey('raw', encoder.encode(env.SPEEDRUN_TICKET_SECRET),
  { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
const ticketMessage = (user, row, id, expires) => {
  const fields = [user.id, row.id, row.engine, row.ruleset, row.release_sha256, id, expires];
  if (row.kind === 'custom') fields.push(row.track_id, row.track_sha256);
  return encoder.encode(JSON.stringify(fields));
};
const key = id => 'runs/' + id + '.dkrreplay';
const verified = "r.verified_ticks > 0 AND length(r.verifier_digest)=64";
const visible = "r.state='published' AND a.status='active' AND " + verified;
const hostedVideoExists = 'EXISTS (SELECT 1 FROM speedrun_video_assets v WHERE v.run_id=r.id AND v.replay_sha256=r.sha256)';
const validRelease = "(b.kind<>'custom' OR EXISTS (SELECT 1 FROM launcher_release_integrity i WHERE i.release_id=b.release_id AND i.sha256=b.release_sha256))";

const boardSelect = 'SELECT ' + boardColumns + ' FROM speedrun_boards b ' + customPackageJoin;
const customIntake = env => env.SPEEDRUN_CUSTOM_UPLOADS_ENABLED === 'true';
const adventureIntake = async (db, env) => env.SPEEDRUN_ADVENTURE_UPLOADS_ENABLED === 'true' &&
  await adventureVerifierReady(db, env);
const rankedBoard = row => vanillaBoard(row) || customBoard(row);

function board(row) {
  return { id: row.id, kind: row.kind, title: row.title, category: row.category, vehicle: row.vehicle,
    ruleset: row.ruleset, releaseId: row.release_id, releaseHash: row.release_sha256,
    racer: rankedBoard(row)?.name || '',
    trackId: customBoard(row)?.trackId,
    trackHash: customBoard(row)?.trackHash,
    ranked: !!rankedBoard(row) || adventureBoard(row) };
}

async function catalogueLeaders(db) {
  const rows = await all(db, `WITH times AS (
    SELECT b.kind,b.title,b.release_id,b.vehicle,b.id AS board_id,
      COALESCE(c.credited_name,a.display_name) AS name,
      r.verified_ticks AS race,r.verified_best_ticks AS lap,
      ROW_NUMBER() OVER (PARTITION BY b.kind,b.title,b.release_id,b.vehicle
        ORDER BY r.verified_ticks,r.created_at,r.id) AS race_order,
      ROW_NUMBER() OVER (PARTITION BY b.kind,b.title,b.release_id,b.vehicle
        ORDER BY (COALESCE(r.verified_best_ticks,0)<=0),r.verified_best_ticks,r.created_at,r.id) AS lap_order
    FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id JOIN accounts a ON a.id=r.account_id
    LEFT JOIN speedrun_capture_credits c ON c.run_id=r.id
    WHERE `+visible+` AND `+validRelease+`
  ) SELECT * FROM times WHERE race_order=1 OR (lap_order=1 AND lap>0)`);
  const leaders = new Map();
  for (const row of rows) {
    const id = row.kind === 'custom' ? row.release_id : row.kind === 'full-game' ? row.board_id : row.title;
    const key = row.kind + '|' + id;
    if (!leaders.has(key)) leaders.set(key, {});
    const byVehicle = leaders.get(key);
    const vehicle = byVehicle[row.vehicle] ??= {};
    if (row.race_order === 1) vehicle.race = {name:row.name,ticks:row.race};
    if (row.lap_order === 1 && row.lap > 0 && row.kind !== 'full-game')
      vehicle.lap = {name:row.name,ticks:row.lap};
  }
  return leaders;
}

async function catalogue(db, env, url) {
  const cursor = url.searchParams.get('cursor') || '';
  if (cursor && !replayId(cursor)) fail(400, 'cursor', 'Invalid track page.');
  const rows = await all(db, `SELECT i.id,i.title,r.id AS release_id,r.version,h.sha256
    FROM library_items i JOIN library_releases r ON r.id=i.current_release_id
    LEFT JOIN launcher_release_integrity h ON h.release_id=r.id
    WHERE i.category='tracks' AND i.archived=0 AND i.id>? ORDER BY i.id LIMIT 41`, cursor);
  const boards = await all(db, boardSelect + ' WHERE ' + validRelease + ' ORDER BY b.title,b.category,b.vehicle LIMIT 2000');
  const leaders = await catalogueLeaders(db);
  const records = (kind, id) => leaders.get(kind + '|' + id) || {};
  const intake = await submitEnabled(db, env);
  const canSubmitCustom = intake && customIntake(env) && boards.some(customBoard);
  const canSubmitAdventure = intake && boards.some(adventureBoard) && await adventureIntake(db, env);
  return json({ canSubmit: intake && (boards.some(vanillaBoard) || canSubmitCustom || canSubmitAdventure),
    vanillaTracks: vanillaTracks.map(track => ({...track, leaders:records('vanilla',track.title)})),
    adventureCatalogue: adventureCatalogue.map(category => ({...category, leaders:records('full-game','adventure-'+category.id)})),
    canClaimLocal: intake && localClaimsEnabled(env),
    canSubmitCustom, canSubmitAdventure, videoEnabled: !!env.SPEEDRUN_VIDEOS || videoArchiveEnabled(env),
    hostedVideos: videoArchiveEnabled(env), submissionLimit,
    boards: cursor ? [] : boards.map(board),
    customTracks: rows.slice(0,40).map(row => ({ id: row.id, title: row.title, releaseId: row.release_id,
      version: row.version, leaders:records('custom',row.release_id),
      vehicles: [...new Set(boards.filter(b => b.release_id === row.release_id && customBoard(b)).map(b => b.vehicle))],
      qualified: !!row.sha256 && boards.some(b => b.release_id === row.release_id && customBoard(b)) })),
    nextCursor: rows.length > 40 ? rows[39].id : '' });
}

async function rankedEntries(db, boards, score, limit = 100, offset = 0) {
  if (!boards.length) return [];
  const time = score === 'lap' ? 'verified_best_ticks' : 'verified_ticks';
  const byId = new Map(boards.map(row => [row.id, board(row)]));
  const rows = await all(db, `WITH eligible AS (
      SELECT r.id,r.board_id,r.account_id,r.`+time+` AS verified_ticks,r.created_at,
        CASE WHEN substr(r.board_id,1,10)='adventure-' THEN json_extract(r.manifest,'$.splits') END AS adventure_splits,
        COALESCE(c.credited_name,a.display_name) AS display_name,
        EXISTS (SELECT 1 FROM speedrun_video_assets v WHERE v.run_id=r.id AND v.replay_sha256=r.sha256) AS has_video,
        ROW_NUMBER() OVER (PARTITION BY r.account_id,r.board_id ORDER BY r.`+time+`,r.created_at,r.id) AS personal_order
      FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id JOIN speedrun_boards b ON b.id=r.board_id
      LEFT JOIN speedrun_capture_credits c ON c.run_id=r.id
      WHERE r.board_id IN (`+boards.map(() => '?').join(',')+`) AND `+visible+` AND r.`+time+`>0
        AND json_extract(r.manifest,'$.ruleset')=b.ruleset AND json_extract(r.manifest,'$.engine')=b.engine
    ) SELECT id,board_id,display_name,verified_ticks,adventure_splits,has_video,
        RANK() OVER (ORDER BY verified_ticks) AS place
      FROM eligible WHERE personal_order=1 ORDER BY verified_ticks,created_at,id LIMIT ? OFFSET ?`, ...boards.map(row => row.id), limit, offset);
  return rows.map(row => ({id:row.id, name:row.display_name, ticks:row.verified_ticks, place:row.place, hasVideo:!!row.has_video,
    racer:byId.get(row.board_id).racer, ...(row.adventure_splits ? {splits:JSON.parse(row.adventure_splits)} : {})}));
}

async function ranking(db, id, score) {
  const info = await one(db, boardSelect + ' WHERE b.id=? AND ' + validRelease, id);
  if (!info) fail(404, 'board_missing', 'That leaderboard is unavailable.');
  if (info.kind === 'full-game') score = 'race';
  return json({ board:board(info), score, entries:await rankedEntries(db,[info],score) });
}

async function trackRanking(db, id, url) {
  const kind = url.searchParams.get('kind');
  const offset = Number(url.searchParams.get('offset') || 0);
  if (!Number.isSafeInteger(offset) || offset < 0) fail(400, 'page', 'Invalid leaderboard page.');
  if (kind === 'full-game') {
    const row = await one(db, boardSelect + " WHERE b.kind='full-game' AND b.id=?", 'adventure-' + id);
    if (!adventureBoard(row)) fail(404, 'track_missing', 'That Adventure leaderboard is not qualified.');
    const [entries,history] = await Promise.all([rankedEntries(db,[row],'race',101,offset),
      offset ? null : recordHistory(db,[row],'race',now())]);
    return json({ track:{id, title:row.title}, score:'race', vehicle:'', vehicles:[], racers:[], racer:'',
      ranked:true, history, entries:entries.slice(0,100), nextOffset:entries.length > 100 ? offset+100 : null });
  }
  const custom = kind === 'custom';
  const track = custom ?
    await one(db, `SELECT i.id,i.title,r.id AS releaseId,r.version FROM library_items i
      JOIN library_releases r ON r.id=i.current_release_id
      WHERE i.category='tracks' AND i.archived=0 AND i.id=?`, id) :
    vanillaTracks.find(row => row.id === id);
  if (!track) fail(404, 'track_missing', 'That track is unavailable.');
  const boards = await all(db, boardSelect + ' WHERE b.kind=? AND ' + validRelease+
    (custom ? ' AND b.release_id=?' : ' AND b.title=?')+" ORDER BY b.vehicle,b.id LIMIT 90",
    custom ? 'custom' : 'vanilla', custom ? track.releaseId : track.title);
  const available = [...new Set(boards.filter(rankedBoard).map(row => row.vehicle))];
  const vehicles = track.vehicles || available;
  const requested = url.searchParams.get('vehicle');
  if (requested && !vehicles.includes(requested)) fail(400, 'vehicle', 'Choose a vehicle supported by this track.');
  const vehicle = requested || available[0] || vehicles[0] || '';
  const byVehicle = boards.filter(row => row.vehicle === vehicle && rankedBoard(row));
  const racers = byVehicle.map(row => ({id:row.id,name:board(row).racer})).filter(row => row.name)
    .sort((a,b) => a.name.localeCompare(b.name));
  const racer = url.searchParams.get('racer') || '';
  if (racer && !racers.some(row => row.id === racer)) fail(400, 'racer', 'Choose a racer available on this board.');
  const score = url.searchParams.get('score') === 'lap' ? 'lap' : 'race';
  const selected = racer ? byVehicle.filter(row=>row.id===racer) : byVehicle;
  const [entries,history] = await Promise.all([rankedEntries(db,selected,score,101,offset),
    offset ? null : recordHistory(db,selected,score,now())]);
  return json({track, score, vehicle, vehicles, racers, racer, ranked:byVehicle.length > 0,
    history,entries:entries.slice(0,100), nextOffset:entries.length > 100 ? offset+100 : null});
}

async function finishTargets(db, row, owner) {
  const match = row.kind === 'full-game' ? 'b.id=?' : row.kind === 'custom'
    ? "b.kind='custom' AND b.release_id=? AND b.vehicle=?"
    : "b.kind='vanilla' AND b.title=? AND b.vehicle=?";
  const params = row.kind === 'full-game' ? [row.id] :
    [row.kind === 'custom' ? row.release_id : row.title,row.vehicle];
  const rows = await all(db, `WITH times AS (
      SELECT r.account_id,r.board_id,min(r.verified_ticks) AS ticks
      FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id JOIN accounts a ON a.id=r.account_id
      WHERE `+visible+` AND `+validRelease+` AND `+match+`
      GROUP BY r.account_id,r.board_id
    ) SELECT ticks, (account_id=? AND board_id=?) AS personal FROM times
      ORDER BY personal DESC,ticks LIMIT 4`, ...params,owner,row.id);
  return {title:row.title, personal:rows.find(time => time.personal)?.ticks || 0,
    rivals:rows.filter(time => !time.personal).slice(0,3).map(time => time.ticks)};
}

async function issueAttempt(request, env, db) {
  if (!await submitEnabled(db, env)) fail(503, 'pilot_closed', 'Ranked attempts are closed.');
  const user = await __dkrAuth.requireUser(request, env);
  const body = await readJSON(request, 1024);
  if (!replayId(body.board)) fail(400, 'board_missing', 'Choose a ranked leaderboard.');
  const row = await one(db, boardSelect + ' WHERE b.id=?', body.board);
  const custom = customIntake(env) && customBoard(row);
  const adventure = adventureBoard(row) && await adventureIntake(db, env);
  if (!custom && !adventure && !vanillaBoard(row)) fail(404, 'board_missing', 'This track has not been qualified for ranked runs.');
  const id = randomToken(18);
  const expires = now() + 30 * 86400;
  const mac = await crypto.subtle.sign('HMAC', await signingKey(env), ticketMessage(user, row, id, expires));
  const target = custom ? { releaseId:row.release_id, releaseHash:row.release_sha256,
    trackId:custom.trackId, trackHash:custom.trackHash } : {};
  return json({ board:row.id, engine:row.engine, ticket:id + '.' + expires + '.' + hex(mac), ...target,
    finishTargets:await finishTargets(db,row,user.id) });
}

async function localClaimBoard(db, manifest) {
  if (manifest.category) return one(db, boardSelect + " WHERE b.kind='full-game' AND b.id=?",
    'adventure-' + manifest.category);
  const suffix = ancientLakeRacers[manifest.racer ?? 9][0].slice('ancient-lake-car'.length);
  if (manifest.trackId) {
    const candidates = await all(db, boardSelect + " WHERE b.kind='custom' AND " + validRelease +
      ' AND q.track_id=? AND q.track_sha256=? AND b.vehicle=? AND b.ruleset=?',
      manifest.trackId, manifest.trackHash, manifest.vehicle, manifest.ruleset);
    const matched = candidates.filter(row => customBoard(row)?.racer === (manifest.racer ?? 9));
    return matched.length === 1 ? matched[0] : null;
  }
  const course = manifest.ruleset === legacyRules ? 'ancient-lake-car' : manifest.course + '-' + manifest.vehicle;
  return one(db, boardSelect + " WHERE b.kind='vanilla' AND b.id=?", course + suffix);
}

async function enqueue(request, env, db, local = false) {
  if (!await submitEnabled(db, env)) fail(503, 'pilot_closed', 'Run submissions are closed while replay verification is being tested.');
  const user = await __dkrAuth.requireUser(request, env);
  const bytes = await readReplayBody(request);
  let manifest;
  try { if (bytes) manifest = parseReplay(bytes); } catch { /* Invalid replay evidence. */ }
  if (local) {
    if (!localClaimsEnabled(env)) fail(503, 'local_claims_closed', 'Linking local runs is currently unavailable.');
    if (!manifest || bytes[7] !== 50 || manifest.practice !== true ||
        ['ticket','board','engine','releaseHash'].some(field => Object.hasOwn(manifest, field)))
      fail(400, 'invalid_local_replay', 'Choose a completed local recording from a supported DKR-R build.');
  } else {
    if (!manifest || manifest.practice !== false || !rankedTicket(manifest.ticket))
      fail(400, 'invalid_replay', 'Choose a replay recorded using Start ranked run.');
    if (manifest.arch !== 'x86_64') fail(409, 'platform', 'This platform is awaiting replay qualification.');
  }
  const match = local ? await localClaimBoard(db, manifest) : await one(db, boardSelect + ' WHERE b.id=?', manifest.board);
  const adventure = match?.kind === 'full-game' && await adventureIntake(db, env);
  // Resolve qualified board metadata on the server. Keep the original evidence bytes.
  if (local && match) manifest = {...manifest, engine:match.engine, board:match.id, releaseHash:match.release_sha256};
  if (!matchesRankedBoard(manifest, match, customIntake(env), adventure))
    fail(409, 'board_mismatch', 'This build or track version is not qualified for that board.');

  let attempt, expiry;
  if (!local) {
    const [id, expires, signature] = manifest.ticket.split('.');
    attempt = id;
    expiry = Number(expires);
    const mac = Uint8Array.from(signature.match(/../g), value => Number.parseInt(value, 16));
    if (!await crypto.subtle.verify('HMAC', await signingKey(env), mac, ticketMessage(user, match, attempt, expiry)))
      fail(403, 'attempt_owner', 'This ranked attempt belongs to another account or build.');
  }
  const hash = hex(await crypto.subtle.digest('SHA-256', bytes));
  // The recorder's timing and input stream can be compared independently of
  // a username, note, board attempt nonce, or other client-chosen metadata.
  const inputStart = 12 + new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(8, true);
  const inputEnd = inputStart + 4 * (manifest.finishTick ?? manifest.ticks);
  const inputDigest = hex(await crypto.subtle.digest('SHA-256', bytes.subarray(inputStart, inputEnd)));
  if (local) {
    const route = manifest.trackHash
      ? [manifest.trackId, manifest.trackHash, manifest.vehicle, manifest.racer ?? 9] : [match.id];
    attempt = 'local-' + await sha256(JSON.stringify([route, inputDigest]));
  } else if (await one(db, `SELECT 1 FROM speedrun_capture_credits c JOIN speedrun_runs r ON r.id=c.run_id
        WHERE c.input_digest=? AND r.board_id=? AND r.account_id<>? AND r.state IN ('verified','published') LIMIT 1`,
        inputDigest, match.id, user.id)) {
    fail(409, 'input_duplicate', 'These recorded inputs were already verified for another player.');
  }
  const stamp = now();
  const windowStart = stamp - submissionWindowSeconds;
  const existing = () => one(db, 'SELECT id,state FROM speedrun_runs WHERE account_id=? AND sha256=?', user.id, hash);
  let run = await existing();
  if (!run) {
    if (!local && expiry <= stamp) fail(409, 'attempt_expired', 'This ranked attempt has expired.');
    const insert = db.prepare(`INSERT OR IGNORE INTO speedrun_runs(id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at)
      SELECT ?,?,?,?,?,?,?,'uploading',?,?
      WHERE (SELECT COUNT(*) FROM speedrun_runs WHERE account_id=? AND created_at>?)<?
        AND (SELECT COUNT(*) FROM speedrun_runs WHERE state IN ('uploading','queued','verified'))<?
        ${local ? `AND NOT EXISTS (SELECT 1 FROM speedrun_local_claims WHERE claim_key=?)
        AND NOT EXISTS (SELECT 1 FROM speedrun_capture_credits c JOIN speedrun_runs r ON r.id=c.run_id
          WHERE c.input_digest=? AND r.board_id=?)` : ''}`)
      .bind(randomToken(18), user.id, match.id, hash, attempt, JSON.stringify(manifest),
        safeText(manifest.notes, 600, 'Notes'), stamp, stamp, user.id, windowStart, submissionLimit.maxRuns,
        maxPendingRuns, ...(local ? [attempt, inputDigest, match.id] : []));
    if (local) {
      await db.batch([insert, db.prepare(`INSERT OR IGNORE INTO speedrun_local_claims(claim_key)
        SELECT attempt_id FROM speedrun_runs WHERE attempt_id=? AND account_id=?`).bind(attempt, user.id)]);
    } else {
      await insert.run();
    }
    run = await existing();
  }
  if (!run && local && (await one(db, 'SELECT 1 FROM speedrun_local_claims WHERE claim_key=?', attempt) ||
      await one(db, `SELECT 1 FROM speedrun_capture_credits c JOIN speedrun_runs r ON r.id=c.run_id
        WHERE c.input_digest=? AND r.board_id=?`, inputDigest, match.id)))
    fail(409, 'local_already_linked', 'This recording has already been linked or submitted.');
  if (!run && !local && await one(db, 'SELECT id FROM speedrun_runs WHERE attempt_id=?', attempt))
    fail(409, 'attempt_used', 'This ranked attempt already has a different replay.');
  if (!run) {
    const expiries = await recentSubmissionExpiries(db, user.id, stamp);
    if (expiries.length === submissionLimit.maxRuns) {
      const minutes = Math.ceil((expiries.at(-1) - stamp) / 60);
      const hours = Math.floor(minutes / 60);
      const wait = hours ? `${hours} h ${minutes % 60} min` : `${minutes} min`;
      fail(429, 'submission_limit', `You've submitted ${submissionLimit.maxRuns} runs in the last ${submissionLimit.windowHours} hours. A slot opens in about ${wait}.`);
    }
    fail(429, 'queue_limit', 'The leaderboard review queue is full. Try posting again later.');
  }
  await db.prepare(`INSERT OR IGNORE INTO speedrun_capture_credits(run_id,discord_id,credited_name,input_digest)
    SELECT r.id,a.discord_id,a.display_name,? FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE r.id=? AND r.account_id=?`).bind(inputDigest, run.id, user.id).run();
  if (run.state === 'uploading') {
    const lease = randomToken(18);
    const ftp = ftpReplayEnabled(env);
    if (ftp) {
      const held = await one(db, `INSERT INTO speedrun_ftp_upload_leases(run_id,lease,until)
        SELECT id,?,? FROM speedrun_runs WHERE id=? AND state='uploading'
        ON CONFLICT(run_id) DO UPDATE SET lease=excluded.lease,until=excluded.until
        WHERE speedrun_ftp_upload_leases.until<=? RETURNING run_id`, lease, stamp + 90, run.id, stamp);
      if (!held) fail(409, 'upload_in_progress', 'This replay is already uploading. Check My runs before trying again.');
    }
    try {
      await replayStorage(env).put(key(run.id), bytes, { httpMetadata: { contentType: 'application/octet-stream' } });
      await db.batch([
        db.prepare("UPDATE speedrun_runs SET state='queued',updated_at=? WHERE id=? AND state='uploading'").bind(now(), run.id),
        db.prepare(`INSERT OR REPLACE INTO speedrun_replay_deletions(run_id,removed_at)
          SELECT ?,unixepoch() WHERE NOT EXISTS (SELECT 1 FROM speedrun_runs WHERE id=?)`).bind(run.id, run.id)
      ]);
    } finally {
      if (ftp) await db.prepare('DELETE FROM speedrun_ftp_upload_leases WHERE run_id=? AND lease=?').bind(run.id,lease).run();
    }
    run = await existing();
    if (!run) fail(409, 'run_removed', 'This submission was removed before the upload finished.');
  }
  return json({ run }, 202);
}

const reviewVideoMessage = (id, session, expires) =>
  encoder.encode(JSON.stringify(['review-video-v1', id, session, expires]));
const videoMissing = () => fail(404, 'video_missing', 'The run video is not available yet.');

async function reviewVideoGrant(request, env, db, id) {
  requireRole(await __dkrAuth.requireUser(request, env), 'moderator');
  const row = await one(db, `SELECT r.sha256 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE r.id=? AND r.state='verified' AND a.status='active' AND ${verified}`, id);
  if (!row) videoMissing();
  const hosted = videoArchiveEnabled(env) && await one(db,
    'SELECT 1 FROM speedrun_video_assets WHERE run_id=? AND replay_sha256=?',id,row.sha256);
  if (!hosted && !await runVideo(env.SPEEDRUN_VIDEOS,id,row.sha256)) videoMissing();
  const session = await sha256(request.headers.get('Authorization').slice(7));
  const expires = now() + 30 * 60;
  const mac = await crypto.subtle.sign('HMAC', await signingKey(env), reviewVideoMessage(id,session,expires));
  return json({grant:session + '.' + expires + '.' + hex(mac), expires});
}

async function validReviewVideoGrant(request, env, db, id) {
  const url = new URL(request.url);
  const grant = /^([A-Za-z0-9_-]{43})\.(\d{10})\.([a-f0-9]{64})$/.exec(url.searchParams.get('review') || '');
  const website = new URL(env.FRONTEND_URL).origin;
  if (!grant || (request.headers.get('Origin') ?? url.origin) !== website) return false;
  const [, session, expiry, signature] = grant;
  const expires = Number(expiry);
  const stamp = now();
  if (expires <= stamp) return false;
  const bytes = Uint8Array.from(signature.match(/../g), byte => parseInt(byte,16));
  if (!await crypto.subtle.verify('HMAC', await signingKey(env), bytes, reviewVideoMessage(id,session,expires))) return false;
  return !!await one(db,`SELECT 1 FROM sessions s JOIN accounts a ON a.id=s.user_id
    WHERE s.token_hash=? AND s.revoked_at IS NULL AND s.idle_expires_at>? AND s.absolute_expires_at>?
      AND a.status='active' AND a.role IN ('moderator','admin')`,session,stamp,stamp);
}

async function video(request, env, db, id) {
  const row = await one(db, `SELECT r.sha256,r.state FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE r.id=? AND r.state IN ('verified','published') AND a.status='active' AND ${verified}`, id);
  if (!row || (row.state === 'verified' && !await validReviewVideoGrant(request,env,db,id))) videoMissing();
  const asset = await runVideo(env.SPEEDRUN_VIDEOS, id, row.sha256);
  if (asset) return serveRunVideo(request, env.SPEEDRUN_VIDEOS, asset);
  const hosted = videoArchiveEnabled(env) && await one(db, `SELECT video_sha256 AS videoSha256,bytes AS size
    FROM speedrun_video_assets WHERE run_id=? AND replay_sha256=?`,id,row.sha256);
  if (!hosted) videoMissing();
  return serveHostedVideo(request, env.SPEEDRUN_FTP_VIDEO_KEY, {id,replaySha256:row.sha256,...hosted});
}

async function download(request, env, db, id, ghost = false) {
  const row = await one(db, `SELECT r.*,a.status AS account_status,(${visible}) AS downloadable
    FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id WHERE r.id=?`, id);
  if (!row || row.account_status !== 'active' || row.state === 'uploading')
    fail(404, 'replay_missing', 'This replay is unavailable.');
  const manifest = ghost && row.downloadable ? JSON.parse(row.manifest) : null;
  if (ghost && (!manifest || !ghostRace(manifest)))
    fail(404, 'ghost_missing', 'This prepared ghost is unavailable.');
  if (!row.downloadable) {
    const user = request.headers.has('Authorization') ? await __dkrAuth.requireUser(request, env) : null;
    if (!user || user.id !== row.account_id && !['admin','moderator'].includes(user.role))
      fail(404, 'replay_missing', 'This replay is unavailable.');
  }
  const object = await replayStorage(env)?.get(ghost ? ghostKey(id) : key(id));
  if (ghost) {
    if (!object)
      fail(404, 'ghost_missing', 'This prepared ghost is unavailable.');
    const bytes = new Uint8Array(await object.arrayBuffer());
    if (!await validGhost(bytes, row.sha256, manifest))
      fail(404, 'ghost_missing', 'This prepared ghost is unavailable.');
    return new Response(bytes, { headers:{ 'Content-Type':'application/octet-stream', 'Cache-Control':'no-store',
      'Content-Disposition':'attachment; filename="DKR-R-' + id + '.dkrghost"' } });
  }
  if (!object) fail(404, 'replay_missing', 'This replay is unavailable.');
  return new Response(object.body, { headers: { 'Content-Type':'application/octet-stream',
    'Content-Disposition': 'attachment; filename="DKR-R-' + id + '.dkrreplay"' } });
}

async function myRuns(request, env, db, review, id) {
  const user = await __dkrAuth.requireUser(request, env);
  if (review) requireRole(user, 'moderator');
  if (id && !replayId(id)) fail(400, 'run', 'Invalid replay identifier.');
  const observedAt = now();
  const quota = !review && !id ? {
    maxRuns:submissionLimit.maxRuns, observedAt, releasesAt:await recentSubmissionExpiries(db, user.id, observedAt)
  } : null;
  const rows = await all(db, `SELECT r.id,r.state,r.verified_ticks,r.notes,r.created_at,r.manifest,r.attempt_id,
      b.title,b.category,b.vehicle,COALESCE(c.credited_name,a.display_name) AS display_name,m.reason,
      CASE WHEN ${hostedVideoExists} OR (? AND j.rendered_at IS NOT NULL) THEN 'ready'
        WHEN j.lease_until>unixepoch() THEN 'rendering'
        WHEN j.retry_at>unixepoch() THEN 'retrying'
        ELSE 'queued' END AS video_state,
      CASE WHEN r.state='published' AND r.verified_ticks>0 THEN 1 + (
        SELECT COUNT(DISTINCT faster.account_id) FROM speedrun_runs faster
        JOIN accounts active_account ON active_account.id=faster.account_id AND active_account.status='active'
        WHERE faster.board_id=r.board_id AND faster.state='published' AND faster.verifier_digest IS NOT NULL
          AND faster.verified_ticks>0 AND faster.verified_ticks<r.verified_ticks)
      END AS place,
      CASE WHEN r.state='published' AND r.verified_ticks>0 AND NOT EXISTS (
        SELECT 1 FROM speedrun_runs peer JOIN accounts peer_account ON peer_account.id=peer.account_id
        WHERE peer.board_id=r.board_id AND peer.id<>r.id AND peer.state='published' AND peer_account.status='active'
          AND length(peer.verifier_digest)=64 AND peer.verified_ticks>0 AND peer.verified_ticks<=r.verified_ticks)
      THEN 1 ELSE 0 END AS record
    FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id JOIN accounts a ON a.id=r.account_id
    LEFT JOIN speedrun_capture_credits c ON c.run_id=r.id
    LEFT JOIN speedrun_moderation m ON m.run_id=r.id
    LEFT JOIN speedrun_video_jobs j ON j.run_id=r.id
    WHERE ${review ? "r.state='verified' AND a.status='active' AND " + verified : "r.account_id=?"}
      AND (? IS NULL OR r.id=?)
    ORDER BY r.created_at DESC,r.id DESC LIMIT 50`, env.SPEEDRUN_VIDEOS ? 1 : 0, ...(review ? [] : [user.id]), id, id);
  return json({ runs: rows.map(row => {
    const { rom, platform, arch, clockTicks } = JSON.parse(row.manifest);
    return { id: row.id, state: row.state, ticks: row.verified_ticks, claimedTicks: clockTicks,
      title: row.title, category: row.category, vehicle: row.vehicle, notes: row.notes,
      name: row.display_name, createdAt: row.created_at, rom, platform, arch, place: row.place,
      record: Boolean(row.record), reason: row.reason, linkedLocal: row.attempt_id.startsWith('local-'),
      videoState: videoArchiveEnabled(env) || env.SPEEDRUN_VIDEOS ? row.video_state : 'unavailable',
      videoRequired: videoArchiveEnabled(env) };
  }), ...(quota ? { submissionQuota:quota } : {}) });
}

async function publishedTimes(request, env, db, url) {
  const user = await __dkrAuth.requireUser(request, env);
  const afterBoard = url.searchParams.get('afterBoard');
  const afterTicks = Number(url.searchParams.get('afterTicks'));
  if ((afterBoard !== null || url.searchParams.has('afterTicks')) &&
      (!afterBoard || !replayId(afterBoard) || !Number.isSafeInteger(afterTicks) || afterTicks < 1))
    fail(400, 'cursor', 'Invalid published time page.');
  const limit = 256;
  const scores = await all(db, `SELECT r.board_id AS board,r.verified_ticks AS ticks
    FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id JOIN speedrun_boards b ON b.id=r.board_id
    WHERE r.account_id=? AND ${visible} AND ${validRelease}
      AND (r.board_id,r.verified_ticks)>(?,?)
    GROUP BY r.board_id,r.verified_ticks ORDER BY r.board_id,r.verified_ticks LIMIT ?`,
    user.id, afterBoard ?? '', afterTicks, limit + 1);
  return json({ scores:scores.slice(0,limit), more:scores.length > limit });
}

async function moderate(request, env, db, id) {
  const user = await __dkrAuth.requireUser(request, env);
  requireRole(user, 'moderator');
  const body = await readJSON(request, 4096);
  const decision = body.decision;
  if (!['published','rejected'].includes(decision)) fail(400, 'decision', 'Choose Publish or Reject.');
  const reason = safeText(body.reason, 600, 'Review note', decision === 'rejected');
  // Both native and browser clients send the portal Origin. Browser fetch
  // metadata still identifies requests that need the hosted review video.
  const replayReview = body.evidence === 'replay' && !request.headers.has('Sec-Fetch-Site');
  const needsVideo = decision === 'published' && videoArchiveEnabled(env) && !replayReview;
  const result = await db.prepare(`INSERT OR IGNORE INTO speedrun_moderation(run_id,moderator_id,decision,reason,decided_at)
    SELECT r.id,?,?,?,? FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
    JOIN accounts a ON a.id=r.account_id WHERE r.id=? AND r.state='verified'
      AND a.status='active' AND ${verified} AND ${validRelease}
      AND (?=0 OR ${hostedVideoExists})
      AND (?='rejected' OR NOT EXISTS (
        SELECT 1 FROM speedrun_capture_credits original
        JOIN speedrun_capture_credits other ON other.input_digest=original.input_digest
        JOIN speedrun_runs published ON published.id=other.run_id
        WHERE original.run_id=r.id AND published.board_id=r.board_id
          AND published.account_id<>r.account_id AND published.state='published'
      ))`).bind(user.id, decision, reason, now(), id, needsVideo ? 1 : 0, decision).run();
  if (!result.meta.changes) {
    if (needsVideo && await one(db,`SELECT 1 FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
      WHERE r.id=? AND r.state='verified' AND a.status='active' AND ${verified} AND NOT ${hostedVideoExists}`, id))
      fail(409, 'video_pending', 'The review video is still preparing. Refresh the review list to check again.');
    fail(409, 'review_unavailable', 'This run is not independently verified or was already reviewed.');
  }
  return json({ saved: true });
}

// Deleting a run queues object cleanup in the same database transaction.
export async function cleanupSpeedruns(env) {
  const storage = replayStorage(env);
  if (!env.DB || !storage) return;
  const db = env.DB.withSession ? env.DB.withSession('first-primary') : env.DB;
  const stamp = now();
  await db.prepare(`DELETE FROM speedrun_runs WHERE id IN (
    SELECT r.id FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE a.status='deleted' OR (r.state='rejected' AND r.updated_at<?)
      OR (r.state='uploading' AND r.updated_at<?)
    ORDER BY r.updated_at LIMIT 50
  )`).bind(stamp - 30 * 86400, stamp - 86400).run();
  // Give uploads already in flight time to finish. A late writer also queues
  // a new deletion after its put if the run was removed during the request.
  const pending = await all(db, `SELECT run_id,removed_at FROM speedrun_replay_deletions
    WHERE removed_at<=? ORDER BY removed_at,run_id LIMIT ?`, stamp - 3600, ftpReplayEnabled(env) ? 4 : 50);
  if (!pending.length) return;
  await storage.delete(pending.flatMap(row => [key(row.run_id), ghostKey(row.run_id)]));
  if (env.SPEEDRUN_VIDEOS) await env.SPEEDRUN_VIDEOS.delete(pending.map(row => videoKey(row.run_id)));
  await db.batch(pending.map(row => db.prepare(
    'DELETE FROM speedrun_replay_deletions WHERE run_id=? AND removed_at=?').bind(row.run_id, row.removed_at)));
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
    const verifier = await handleSpeedrunVerifier(request, env, db, path);
    if (verifier) return verifier;
    const { method } = request;
    const run = path.match(/^\/v1\/speedruns\/runs\/([A-Za-z0-9_-]{1,96})\/(replay|review|ghost|video)$/);
    const boardPath = path.match(/^\/v1\/speedruns\/boards\/([A-Za-z0-9_-]{1,96})$/);
    const trackPath = path.match(/^\/v1\/speedruns\/tracks\/([A-Za-z0-9_-]{1,96})$/);
    const player = path.match(/^\/v1\/speedruns\/overall\/([A-Za-z0-9_-]{1,96})$/);
    if (method === 'OPTIONS') {
      requireOrigin(request, env);
      response = new Response(null, { status: 204 });
    } else if (method === 'GET' && path === '/v1/speedruns') response = await catalogue(db, env, url);
    else if (method === 'GET' && boardPath) response = await ranking(db, boardPath[1], url.searchParams.get('score') === 'lap' ? 'lap' : 'race');
    else if (method === 'GET' && trackPath) response = await trackRanking(db, trackPath[1], url);
    else if (method === 'GET' && path === '/v1/speedruns/overall') response = json(await overallRankings(db));
    else if (method === 'GET' && player) {
      const offset = Number(url.searchParams.get('offset') || 0);
      if (!Number.isSafeInteger(offset) || offset<0) fail(400,'page','Invalid player stats page.');
      const details = await overallRankings(db,player[1],offset);
      if (!details) fail(404,'player_missing','That player has no published results.');
      response = json(details);
    }
    else if (method === 'GET' && path === '/v1/speedruns/mine/published') response = await publishedTimes(request, env, db, url);
    else if (method === 'GET' && path === '/v1/speedruns/mine') response = await myRuns(request, env, db, false, url.searchParams.get('run'));
    else if (method === 'GET' && path === '/v1/speedruns/review') response = await myRuns(request, env, db, true, url.searchParams.get('run'));
    else if ((method === 'GET' || method === 'HEAD') && run?.[2] === 'video')
      response = await video(request, env, db, run[1]);
    else if (method === 'GET' && (run?.[2] === 'replay' || run?.[2] === 'ghost'))
      response = await download(request, env, db, run[1], run[2] === 'ghost');
    else if (method === 'POST') {
      // Authenticated operations also validate the portal Origin in requireUser.
      if (request.headers.has('Origin')) requireOrigin(request, env);
      if (path === '/v1/speedruns/attempts') response = await issueAttempt(request, env, db);
      else if (path === '/v1/speedruns/runs') response = await enqueue(request, env, db);
      else if (path === '/v1/speedruns/runs/claim') response = await enqueue(request, env, db, true);
      else if (run?.[2] === 'review') response = await moderate(request, env, db, run[1]);
      else if (run?.[2] === 'video') response = await reviewVideoGrant(request, env, db, run[1]);
    }
    if (!response) fail(404, 'not_found', 'This speedrun page does not exist.');
  } catch (error) {
    const known = error instanceof HttpError;
    response = json({ error: { code: known ? error.code : 'unavailable',
      message: known ? error.message : 'The leaderboards are temporarily unavailable.' } }, known ? error.status : 503);
  }
  return finish(response, request, env);
}
