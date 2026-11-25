// Loopback Worker and real native engine. The published package is read-only.
const assert = require('node:assert/strict');
const { createServer } = require('node:http');
const { execFile } = require('node:child_process');
const { once } = require('node:events');
const { randomBytes } = require('node:crypto');
const { promisify } = require('node:util');
const fs = require('node:fs/promises');
const path = require('node:path');
const { makePilot, engine } = require('../run_local_ranked.cjs');
const { digest } = require('../verify_ranked.cjs');

const run = promisify(execFile);
const script = path.resolve(__dirname, '../run_public_ranked.py');

function fixtureManifest(replay) {
  return JSON.parse(replay.subarray(12, 12 + replay.readUInt32LE(8)));
}

function evidence(replay, attempt, platform, altered = false) {
  const manifest = fixtureManifest(replay);
  const start = 12 + replay.readUInt32LE(8);
  const { board, engine, ticket, releaseHash } = attempt;
  const bytes = Buffer.from(JSON.stringify({ ...manifest, board, engine, ticket, releaseHash,
    practice: false, platform, arch: 'x86_64' }));
  const header = Buffer.from(replay.subarray(0, 12));
  header.writeUInt32LE(bytes.length, 8);
  const inputs = altered ? Buffer.alloc(replay.length - start) : replay.subarray(start);
  return Buffer.concat([header, bytes, inputs]);
}

async function main() {
  const [bundle, runtimeArg, romArg, fixture, descriptor, output] = process.argv.slice(2);
  assert.ok(output, 'Usage: custom_ranked_e2e.cjs BUNDLE RUNTIME ROM REPLAY RELEASE_JSON OUTPUT');
  const [replay, releaseText] = await Promise.all([fs.readFile(fixture), fs.readFile(descriptor, 'utf8')]);
  const release = JSON.parse(releaseText);
  const manifest = fixtureManifest(replay);
  assert.equal(manifest.practice, true);
  assert.equal(manifest.trackId, release.trackId);
  assert.equal(manifest.trackHash, release.trackHash);
  const runtime = path.resolve(runtimeArg);
  const rom = path.resolve(romArg);
  const embedded = path.join(path.dirname(runtime), 'libexec', 'dkr-r', 'DKR-R-Runtime.exe');
  const binary = path.basename(runtime) === 'DKR-R.exe' && await fs.stat(embedded).catch(() => null) ? embedded : runtime;
  const secret = randomBytes(32).toString('hex');
  const pilot = await makePilot(path.resolve(bundle), {
    SPEEDRUN_VERIFIER_SECRET: secret,
    SPEEDRUN_VERIFIER_RUNTIME: digest(await fs.readFile(binary)),
    SPEEDRUN_CUSTOM_UPLOADS_ENABLED: 'true',
  });
  const { db, mf, fetch } = pilot;
  const board = 'track-' + release.releaseId + '-' + manifest.vehicle;
  assert.equal(manifest.racer, 9, 'The qualification fixture uses Diddy.');
  const title = release.title || 'Ancient Lake DS';
  const now = Math.floor(Date.now() / 1000);
  const server = createServer(async (request, response) => {
    try {
      assert.match(request.url, /^\/v1\/speedruns\/verifier\/[a-z]+$/);
      const buffers = [];
      let length = 0;
      for await (const bytes of request) {
        length += bytes.length;
        if (length > 16384) {
          response.writeHead(413).end();
          return;
        }
        buffers.push(bytes);
      }
      const result = await mf.dispatchFetch('https://local-ranked.invalid' + request.url, {
        method: request.method, headers: request.headers, body: Buffer.concat(buffers),
      });
      response.writeHead(result.status, Object.fromEntries(result.headers));
      response.end(Buffer.from(await result.arrayBuffer()));
    } catch {
      response.writeHead(500).end();
    }
  });
  try {
    await db.prepare("INSERT INTO library_items(id,title,category,archived,current_release_id) VALUES('qualified-custom',?,'tracks',0,?)")
      .bind(title,release.releaseId).run();
    await db.prepare("INSERT INTO library_releases(id,item_id,version,file_url) VALUES(?,'qualified-custom','1.0',?)")
      .bind(release.releaseId,release.url).run();
    await db.prepare("INSERT INTO launcher_release_integrity VALUES(?,?,?,?)").bind(release.releaseId,release.sha256,release.bytes,now).run();
    await db.prepare('INSERT INTO speedrun_custom_packages VALUES(?,?,?,?,?)')
      .bind(release.releaseId,release.sha256,release.trackId,release.trackHash,manifest.laps ?? 3).run();
    await db.prepare("INSERT INTO speedrun_boards VALUES(?,'custom',?,'Time Trial',?,?,?,?,?)")
      .bind(board,title,manifest.vehicle,manifest.ruleset,engine,release.releaseId,release.sha256).run();
    const post = (url, user, payload, mime = 'application/json') =>
      fetch(url, { method:'POST', user, headers:{'Content-Type':mime}, body:payload });
    const requestAttempt = async () => {
      const response = await post('/attempts', 'alice', JSON.stringify({ board }));
      assert.equal(response.status, 200, await response.clone().text());
      const ticket = await response.json();
      assert.equal(ticket.releaseId, release.releaseId);
      assert.equal(ticket.releaseHash, release.sha256);
      assert.equal(ticket.trackHash, release.trackHash);
      return ticket;
    };
    const submit = async payload => {
      const response = await post('/runs', 'alice', payload, 'application/octet-stream');
      assert.equal(response.status, 202, await response.clone().text());
      return (await response.json()).run;
    };
    const platform = process.platform === 'win32' ? 'windows' : 'linux';
    const valid = evidence(replay, await requestAttempt(), platform);
    const payload = await submit(valid);
    const review = id => '/runs/' + id + '/review';
    assert.equal((await post(review(payload.id), 'reviewer', '{"decision":"published"}')).status, 409);
    assert.equal((await fetch('/runs/' + payload.id + '/replay')).status, 404);
    const invalid = evidence(replay, await requestAttempt(), platform, true);
    const wrong = await submit(invalid);
    await fs.mkdir(output, { recursive:true });
    const jobs = await fs.mkdtemp(path.join(path.resolve(output), 'custom-'));
    const token = path.join(jobs, 'verifier.token');
    await fs.writeFile(token, secret, { flag:'wx', mode:0o600 });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const api = 'http://127.0.0.1:' + server.address().port;
    const verifyNext = async () => {
      const python = process.platform === 'win32' ? 'python' : 'python3';
      const result = await run(python, [script, '--local-test', '--once', '--api', api, '--jobs', jobs,
        '--runtime', runtime, '--rom', rom, '--token-file', token, '--track-origin', new URL(release.url).origin],
        { timeout:345000, windowsHide:true });
      process.stdout.write(result.stdout);
    };
    try {
      await verifyNext();
      await verifyNext();
    } finally {
      await fs.unlink(token);
    }
    const state = async id => db.prepare('SELECT state FROM speedrun_runs WHERE id=?').bind(id).first();
    assert.equal((await state(payload.id)).state, 'verified');
    assert.equal((await state(wrong.id)).state, 'rejected');
    assert.equal((await post(review(payload.id), 'alice', '{"decision":"published"}')).status, 403);
    assert.equal((await post(review(payload.id), 'reviewer', '{"decision":"published"}')).status, 200);
    const bytes = await fetch('/runs/' + payload.id + '/replay');
    assert.equal(bytes.status, 200);
    assert.deepEqual(Buffer.from(await bytes.arrayBuffer()), valid);
    await fs.writeFile(path.join(jobs, 'verified-evidence.dkrreplay'), valid, { flag:'wx' });
    const ranking = await fetch('/tracks/qualified-custom?kind=custom&vehicle=' + encodeURIComponent(manifest.vehicle));
    assert.equal(ranking.status, 200);
    assert.equal((await ranking.json()).entries[0].ticks, manifest.clockTicks);
    console.log('Exact public archive, signed attempt, native proof, tamper rejection, moderator approval and replay download passed.');
  } finally {
    server.close();
    await mf.dispose();
  }
}

main().catch(error => {
  console.error(error.message);
  if (error.stderr) console.error(error.stderr);
  process.exitCode = 1;
});
