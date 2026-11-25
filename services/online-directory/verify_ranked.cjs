// The verifier uses private bindings. There is no public HTTP attestation route.
const { execFile } = require('node:child_process');
const { createHash } = require('node:crypto');
const { mkdir, mkdtemp, readFile, writeFile } = require('node:fs/promises');
const { promisify } = require('node:util');
const path = require('node:path');

const execute = promisify(execFile);
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const verifier = path.resolve(__dirname, '../../scripts/verify_time_trial.py');

async function verifyNext(db, bucket, options) {
  const pending = await db.prepare(`SELECT r.id,r.sha256,r.manifest,b.engine,b.kind
    FROM speedrun_runs r JOIN speedrun_boards b ON b.id=r.board_id
    WHERE r.state='queued' AND b.engine=? AND (
      (b.kind='vanilla' AND b.ruleset IN ('ancient-lake-us80-practice-2','vanilla-time-trial-us80-1'))
      OR (? AND b.kind='full-game' AND b.ruleset='adventure-us80-7')
      OR (? AND b.kind='custom' AND b.ruleset IN ('custom-time-trial-us80-1','custom-time-trial-us80-2')))
    ORDER BY r.created_at,r.id LIMIT 1`).bind(options.engine,options.adventure ? 1 : 0,options.trackPackage ? 1 : 0).first();
  if (!pending) return null;
  const adventure = pending.kind === 'full-game';
  const submitted = JSON.parse(pending.manifest);
  const object = await bucket.get('runs/' + pending.id + '.dkrreplay');
  if (!object || object.size > (adventure ? 3 : 1) * 1024 * 1024)
    throw new Error('Queued evidence is missing or oversized.');
  const bytes = Buffer.from(await object.arrayBuffer());
  if (digest(bytes) !== pending.sha256) throw new Error('Private evidence failed its storage digest.');
  await mkdir(options.jobs, { recursive: true });
  const job = await mkdtemp(path.join(options.jobs, 'verify-'));
  const replay = path.join(job, 'queued.dkrreplay');
  await writeFile(replay, bytes, { flag: 'wx' });
  const timeout = adventure ? Math.max(240, Math.ceil(submitted.ticks / 6) + 180) : 240;
  const args = [verifier, '--runtime', options.runtime, '--rom', options.rom,
    '--replay', replay, '--output', path.join(job, 'result'), '--timeout', String(timeout)];
  if (!adventure) args.push('--emit-ghost');
  if (pending.kind === 'custom') {
    const track = await options.trackPackage(submitted);
    if (!track) throw new Error('No exact local package is registered for this custom track.');
    args.push('--track-package',track);
  }
  if (options.xvfb) args.push('--xvfb');
  let verification;
  try {
    const { stdout } = await execute(options.python, args, { timeout: (timeout+50)*1000, windowsHide: true });
    verification = JSON.parse(stdout);
  } catch (error) {
    if (error.code !== 2) throw new Error('Verifier infrastructure failed. Check the local job logs in ' + job, { cause: error });
    await db.prepare(`UPDATE speedrun_runs SET state='rejected',updated_at=unixepoch()
      WHERE id=? AND sha256=? AND state='queued'`).bind(pending.id, pending.sha256).run();
    return { id: pending.id, state: 'rejected' };
  }
  if (verification.replaySha256 !== pending.sha256 || verification.runtimeSha256 !== options.runtimeSha256 ||
      verification.clockTicks !== submitted.clockTicks || verification.updates !== (submitted.finishTick ?? submitted.ticks) ||
      JSON.stringify(verification.splits) !== JSON.stringify(submitted.splits) ||
      JSON.stringify(verification.checkpoints) !== JSON.stringify(submitted.checkpoints) ||
      (adventure ? verification.category !== submitted.category :
        !Number.isInteger(verification.bestLapTicks) || verification.bestLapTicks < 1) ||
      (submitted.trackId && (verification.trackId !== submitted.trackId ||
        verification.trackHash !== submitted.trackHash || verification.vehicle !== submitted.vehicle)) ||
      (submitted.course && (verification.course !== submitted.course || verification.vehicle !== submitted.vehicle))) {
    throw new Error('Unexpected verifier output. Check the local job logs in ' + job);
  }
  if (!adventure)
    await bucket.put('runs/' + pending.id + '.dkrghost', await readFile(path.join(job, 'result/verified.dkrghost')));
  const result = await db.prepare(`UPDATE speedrun_runs SET state='verified',updated_at=unixepoch(),
    verified_ticks=?,verified_best_ticks=?,verifier_digest=? WHERE id=? AND sha256=? AND state='queued'`)
    .bind(verification.clockTicks, adventure ? null : verification.bestLapTicks, digest(JSON.stringify(verification)), pending.id,
      pending.sha256).run();
  if (!result.meta.changes) return null;
  return { id: pending.id, state: 'verified', ticks: verification.clockTicks, bestLap: verification.bestLapTicks };
}

module.exports = { verifyNext, digest };
