// Runs alongside the verifier with a private video bucket.
const { createHash, randomBytes } = require('node:crypto');
const { createReadStream } = require('node:fs');
const { mkdir, mkdtemp, readFile, stat, writeFile } = require('node:fs/promises');
const { Readable } = require('node:stream');
const path = require('node:path');

const maxReplayBytes = 1024 * 1024;
const maxVideoBytes = 512 * 1024 * 1024;
const inputOverlay = 'n64-v1';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');

async function fingerprint(file) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}

async function checkMovie(run, proof) {
  const { replayVideoDurationMatches } = await import('./api/speedrun_video.mjs');
  const manifest = JSON.parse(run.manifest);
  const finish = manifest.finishTick ?? manifest.ticks;
  return proof.replaySha256 === run.sha256 && proof.clockTicks === run.verified_ticks &&
    proof.updates === finish && proof.bestLapTicks === run.verified_best_ticks &&
    (!manifest.course || (proof.course === manifest.course && proof.vehicle === manifest.vehicle)) &&
    (!manifest.trackId || (proof.trackId === manifest.trackId && proof.trackHash === manifest.trackHash &&
      proof.vehicle === manifest.vehicle)) &&
    JSON.stringify(proof.splits) === JSON.stringify(manifest.splits) &&
    JSON.stringify(proof.checkpoints) === JSON.stringify(manifest.checkpoints) &&
    proof.videoInputOverlay === inputOverlay && replayVideoDurationMatches(proof,manifest.ticks);
}

async function renderNext(db, replays, videos, options) {
  await db.prepare(`INSERT OR IGNORE INTO speedrun_video_jobs(run_id)
    SELECT r.id FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
    JOIN accounts a ON a.id=r.account_id
    WHERE r.state IN ('verified','published') AND a.status='active'
      AND b.kind IN ('vanilla','custom') AND length(r.verifier_digest)=64
      AND NOT EXISTS (SELECT 1 FROM speedrun_video_jobs j WHERE j.run_id=r.id)
    ORDER BY r.created_at,r.id LIMIT 64`).run();
  const claim = randomBytes(16).toString('hex');
  const leaseSeconds = 2400;
  const job = await db.prepare(`UPDATE speedrun_video_jobs SET claim=?,lease_until=unixepoch()+?,attempts=attempts+1
    WHERE run_id=(SELECT j.run_id FROM speedrun_video_jobs j JOIN speedrun_runs r ON r.id=j.run_id
      JOIN accounts a ON a.id=r.account_id
      WHERE j.rendered_at IS NULL AND j.retry_at<=unixepoch() AND j.lease_until<=unixepoch()
        AND r.state IN ('verified','published') AND a.status='active'
      ORDER BY r.created_at,j.run_id LIMIT 1) RETURNING run_id,attempts`).bind(claim,leaseSeconds).first();
  if (!job) return null;
  const run = await db.prepare(`SELECT r.* FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
    WHERE r.id=? AND r.state IN ('verified','published') AND a.status='active'`).bind(job.run_id).first();
  const key = 'runs/' + job.run_id;
  const finish = async () => db.prepare(`UPDATE speedrun_video_jobs SET rendered_at=unixepoch(),claim=NULL,lease_until=0
    WHERE run_id=? AND claim=?`).bind(job.run_id,claim).run();
  try {
    if (!run) return null;
    const prior = await videos.head(key + '.mp4');
    if (prior?.customMetadata?.replaySha256 === run.sha256 && prior.customMetadata.clock === 'native-30hz' &&
        prior.customMetadata.inputOverlay === inputOverlay) {
      await finish();
      return { id: run.id, existing: true };
    }
    const object = await replays.get(key + '.dkrreplay');
    if (!object || object.size > maxReplayBytes) throw new Error('The private replay is missing or oversized.');
    const replay = Buffer.from(await object.arrayBuffer());
    if (digest(replay) !== run.sha256) throw new Error('The private replay failed its digest check.');
    await mkdir(options.jobs, { recursive: true });
    const directory = await mkdtemp(path.join(options.jobs, 'video-'));
    const evidence = path.join(directory, 'source.dkrreplay');
    const output = path.join(directory, 'output');
    await writeFile(evidence,replay,{flag:'wx'});
    await options.render({ evidence, output, run });
    const proof = JSON.parse(await readFile(path.join(output,'proof.json'),'utf8'));
    const movie = path.join(output,'verified.mp4');
    const info = await stat(movie);
    if (!await checkMovie(run,proof) || info.size < 1 || info.size > maxVideoBytes ||
        proof.videoSha256 !== await fingerprint(movie))
      throw new Error('The generated movie does not match the verified replay.');
    const eligible = await db.prepare(`SELECT 1 FROM speedrun_video_jobs j JOIN speedrun_runs r ON r.id=j.run_id
      JOIN accounts a ON a.id=r.account_id WHERE r.id=? AND r.sha256=? AND j.claim=?
        AND j.lease_until>unixepoch() AND r.state IN ('verified','published') AND a.status='active'`)
      .bind(run.id,run.sha256,claim).first();
    if (!eligible) return null;
    // Cloudflare requires a known stream size. The native queue adapter owns
    // the storage transport so it can choose a fixed-length stream or upload.
    const metadata = { replaySha256:run.sha256, videoSha256:proof.videoSha256, clock:'native-30hz', inputOverlay };
    const uploaded = options.putMovie ?
      options.putMovie(videos, key + '.mp4', movie, info.size, metadata) :
      videos.put(key + '.mp4', Readable.toWeb(createReadStream(movie)), {
        httpMetadata:{ contentType:'video/mp4' }, customMetadata:metadata,
      });
    try {
      await uploaded;
    } finally {
      await db.prepare(`INSERT OR REPLACE INTO speedrun_replay_deletions(run_id,removed_at)
        SELECT ?,unixepoch() WHERE NOT EXISTS (SELECT 1 FROM speedrun_runs WHERE id=?)`)
        .bind(run.id,run.id).run();
    }
    await finish();
    return { id:run.id, videoSha256:proof.videoSha256 };
  } catch (error) {
    await db.prepare(`UPDATE speedrun_video_jobs SET claim=NULL,lease_until=0,retry_at=unixepoch()+?
      WHERE run_id=? AND claim=?`).bind(Math.min(3600, 60 * 2 ** Math.min(job.attempts - 1, 6)),
        job.run_id,claim).run();
    throw error;
  }
}

module.exports = { renderNext };
