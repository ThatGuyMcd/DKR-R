// Disposable fixtures on loopback. This process never connects to deployed services.
const assert = require('node:assert/strict');
const { randomBytes, createHash } = require('node:crypto');
const { createServer } = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');
const { parseArgs } = require('node:util');
const { Miniflare } = require('../test-tools/node_modules/miniflare');
const { schema } = require('../library-workflow/tests/support.cjs');
const { verifyNext, digest } = require('./verify_ranked.cjs');

const origin = 'https://positron-portal.com';
const rules = 'ancient-lake-us80-practice-2';
const engine = digest(Buffer.from('DKR-R Ancient Lake US v1.1 ranked pilot / ' + rules));
const route = '/v1/speedruns';

async function makePilot(bundle, overrides = {}, outboundService) {
  const mf = new Miniflare({
    modules: true, modulesRoot: bundle, scriptPath: path.join(bundle, 'PRIVATE-API/worker-single-file.mjs'),
    compatibilityDate: '2026-07-30',
    d1Databases: ['DB'], r2Buckets: ['SPEEDRUN_REPLAYS', 'SPEEDRUN_VIDEOS'],
    ...(outboundService ? { outboundService } : {}),
    bindings: { FRONTEND_URL: origin + '/dkr-r/index.html', SPEEDRUN_UPLOADS_ENABLED: 'true',
      SPEEDRUN_TICKET_SECRET: randomBytes(32).toString('hex'), ...overrides }
  });
  try {
    const db = await mf.getD1Database('DB');
    const bucket = await mf.getR2Bucket('SPEEDRUN_REPLAYS');
    await db.exec(schema);
    await db.prepare('CREATE TABLE library_request_events (actor_id TEXT, actor_name TEXT)').run();
    const migration = await fs.readFile(path.join(bundle, 'PRIVATE-API/003-speedruns.sql'), 'utf8');
    const delimiter = migration.indexOf('CREATE TRIGGER');
    await db.exec(migration.slice(0, delimiter).replace(/^--.*$/gm, '').replace(/\s+/g, ' ').trim());
    for (const [trigger] of migration.slice(delimiter).matchAll(/CREATE TRIGGER\b.*?\r?\nEND;/gs))
      await db.prepare(trigger).run();
    const tokens = {};
    const now = Math.floor(Date.now() / 1000);
    for (const [id, role] of [['alice', 'player'], ['bob', 'player'], ['reviewer', 'moderator']]) {
      tokens[id] = randomBytes(32).toString('base64url');
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(id, id, id, role, 'active', now, now).run();
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(
        createHash('sha256').update(tokens[id]).digest('base64url'), id, now, now, now + 14400, now + 86400).run();
    }
    const fetch = (suffix, options = {}) => mf.dispatchFetch('https://ranked-test.invalid' + route + suffix, {
      ...options, headers: { Origin: origin, ...(options.user ? { Authorization: 'Bearer ' + tokens[options.user] } : {}),
        ...options.headers }
    });
    const addBoard = (id, kind, title, release = null, sha = null) =>
      db.prepare('INSERT INTO speedrun_boards VALUES(?,?,?,?,?,?,?,?,?)')
        .bind(id, kind, title, 'Time Trial', 'car', rules, engine, release, sha).run();
    return { mf, db, bucket, tokens, fetch, addBoard };
  } catch (error) {
    await mf.dispose();
    throw error;
  }
}

// Synthetic automated proof only. The standard test autopilot cannot issue or record a ranked run.
function attachFixtureTicket(bytes, { board, engine, ticket }) {
  assert.equal(bytes.toString('ascii', 0, 8), 'DKRREPL2');
  const start = 12 + bytes.readUInt32LE(8);
  const manifest = JSON.parse(bytes.toString('utf8', 12, start));
  assert.equal(manifest.practice, true);
  const envelope = Buffer.from(JSON.stringify({ ...manifest, board, engine, ticket, practice: false,
    platform: 'windows', arch: 'x86_64', releaseHash: null }));
  const header = Buffer.from(bytes.subarray(0, 12));
  header.writeUInt32LE(envelope.length, 8);
  return Buffer.concat([header, envelope, bytes.subarray(start)]);
}

async function selfTest(pilot, options) {
  const { fetch, db, bucket } = pilot;
  const source = await fs.readFile(options.fixture);
  const post = async (suffix, user, body, type = 'application/json') => {
    const response = await fetch(suffix, { method: 'POST', user, body, headers: { 'Content-Type': type } });
    assert.ok(response.ok, response.status + ' ' + await response.clone().text());
    return response.json();
  };
  const { ancientLakeRacers, parseReplay } = await import('../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js');
  const manifest = parseReplay(source);
  const [legacyBoard] = ancientLakeRacers[manifest.racer ?? 9];
  let board = legacyBoard;
  if (manifest.course) {
    const { vanillaTracks } = await import('./api/speedrun_tracks.mjs');
    const course = vanillaTracks.find(candidate => candidate.id === manifest.course);
    assert.ok(course?.vehicles.includes(manifest.vehicle));
    board = course.id + '-' + manifest.vehicle + legacyBoard.slice('ancient-lake-car'.length);
    options.engine = digest(Buffer.from('DKR-R replay fixture / ' + manifest.ruleset + ' / ' + board));
    await db.prepare(`INSERT INTO speedrun_boards(id,kind,title,category,vehicle,ruleset,engine)
      VALUES(?,'vanilla',?,'Time Trial',?,?,?)`).bind(board,course.title,manifest.vehicle,manifest.ruleset,options.engine).run();
  }
  const ticket = async () => post('/attempts', 'alice', JSON.stringify({ board }));
  const evidence = attachFixtureTicket(source, await ticket());
  const send = body => post('/runs', 'alice', body, 'application/octet-stream');
  const { run } = await send(evidence);
  assert.equal(run.state, 'queued');
  assert.equal((await send(evidence)).run.id, run.id);
  assert.equal((await fetch('/runs/' + run.id + '/replay')).status, 404);
  const invalid = attachFixtureTicket(source, await ticket());
  // Flip recorded steering before the first checkpoint, but keep the envelope valid.
  invalid.fill(0, 12 + invalid.readUInt32LE(8));
  const changed = await send(invalid);
  const outcomes = [];
  for (let index = 0; index < 2; index++) outcomes.push(await verifyNext(db, bucket, options));
  const verified = outcomes.find(value => value?.id === run.id);
  assert.equal(verified?.state, 'verified');
  assert.equal(outcomes.find(value => value?.id === changed.run.id)?.state, 'rejected');
  assert.equal((await (await fetch('/boards/' + board)).json()).entries.length, 0);
  await post('/runs/' + run.id + '/review', 'reviewer', JSON.stringify({ decision: 'published' }));
  for (const [score, ticks] of [['race', verified.ticks], ['lap', verified.bestLap]]) {
    const listing = await (await fetch('/boards/' + board + '?score=' + score)).json();
    assert.equal(listing.entries[0].id, run.id);
    assert.equal(listing.entries[0].ticks, ticks);
  }
  const published = await fetch('/runs/' + run.id + '/replay');
  assert.deepEqual(Buffer.from(await published.arrayBuffer()), evidence);
  console.log(JSON.stringify({ verified, altered: 'rejected', queueAndModeration: 'passed' }, null, 2));
}

async function main() {
  const string = { type: 'string' };
  const { values: args } = parseArgs({ options: {
    bundle: string, profile: string, runtime: string, rom: string, jobs: string, python: string,
    fixture: string, xvfb: { type: 'boolean' } } });
  for (const required of ['bundle', 'runtime', 'rom', 'jobs'])
    if (!args[required]) throw new Error('Provide --' + required);
  const options = {
    runtime: path.resolve(args.runtime), rom: path.resolve(args.rom), jobs: path.resolve(args.jobs),
    python: args.python || (process.platform === 'win32' ? 'python' : 'python3'), xvfb: args.xvfb,
    fixture: args.fixture, engine,
  };
  const inner = path.join(path.dirname(options.runtime), 'libexec', 'dkr-r', 'DKR-R-Runtime.exe');
  const executable = path.basename(options.runtime) === 'DKR-R.exe' &&
    await fs.stat(inner).then(stat => stat.isFile(), () => false) ? inner : options.runtime;
  options.runtimeSha256 = digest(await fs.readFile(executable));
  const pilot = await makePilot(path.resolve(args.bundle));
  const { ancientLakeRacers } = await import('../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js');
  for (const [id] of ancientLakeRacers)
    await pilot.addBoard(id, 'vanilla', 'Ancient Lake');
  if (args.fixture) {
    try { await selfTest(pilot, options); }
    finally { await pilot.mf.dispose(); }
    return;
  }
  if (!args.profile) {
    await pilot.mf.dispose();
    throw new Error('Provide --profile for a new disposable launcher profile.');
  }
  // The local playtest includes the independently reproduced Fossil Canyon course.
  const { vanillaRules } = await import('./api/speedrun_rules.mjs');
  await pilot.db.prepare(`INSERT INTO speedrun_boards(id,kind,title,category,vehicle,ruleset,engine)
    VALUES('fossil-canyon-car','vanilla','Fossil Canyon','Time Trial','car',?,?)`).bind(vanillaRules,engine).run();
  const profile = path.resolve(args.profile);
  const config = path.join(profile, 'ranked-local.json');
  const server = createServer(async (request, response) => {
    try {
      const uri = new URL(request.url, 'http://127.0.0.1');
      if (uri.origin !== 'http://127.0.0.1' || ![route, route + '/attempts', route + '/runs', route + '/mine'].includes(uri.pathname) &&
          !/^\/v1\/speedruns\/((boards|tracks)\/[A-Za-z0-9_-]+|runs\/[A-Za-z0-9_-]+\/(replay|ghost))$/.test(uri.pathname)) {
        response.writeHead(404).end();
        return;
      }
      const method = request.method;
      const headers = {};
      if (request.headers.authorization === 'Bearer ' + pilot.tokens.alice)
        headers.Authorization = request.headers.authorization;
      if (request.headers['content-type'])
        headers['Content-Type'] = request.headers['content-type'];
      const chunks = [];
      let length = 0;
      for await (const chunk of request) {
        length += chunk.length;
        if (length > 1024 * 1024) {
          response.writeHead(413).end();
          return;
        }
        chunks.push(chunk);
      }
      const result = await pilot.fetch(uri.pathname.slice(route.length) + uri.search,
        { method, headers, ...(method === 'POST' ? { body: Buffer.concat(chunks) } : {}) });
      response.writeHead(result.status, Object.fromEntries(result.headers));
      response.end(Buffer.from(await result.arrayBuffer()));
    } catch {
      response.writeHead(503).end();
    }
  });
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    await fs.mkdir(profile, { recursive: true });
    await fs.writeFile(config, JSON.stringify({ port: server.address().port, token: pilot.tokens.alice }),
      { flag: 'wx', mode: 0o600 });
  } catch (error) {
    server.close();
    await pilot.mf.dispose();
    throw error;
  }
  console.log('Local ranked API ready. Start the launcher with --config "' + profile + '".');
  console.log('Queued replays are independently verified. Enter "publish RUN_ID" to simulate a moderator after reviewing.');
  let pending = false;
  const timer = setInterval(async () => {
    if (pending) return;
    pending = true;
    try {
      const result = await verifyNext(pilot.db, pilot.bucket, options);
      if (result) console.log(result);
    } catch (error) {
      console.error(error.message);
      clearInterval(timer);
    } finally {
      pending = false;
    }
  }, 1500);
  const input = require('node:readline').createInterface({ input: process.stdin });
  input.on('line', async line => {
    const id = /^publish ([A-Za-z0-9_-]{1,96})$/.exec(line.trim())?.[1];
    if (!id) return;
    const result = await pilot.fetch('/runs/' + id + '/review', { method: 'POST', user: 'reviewer',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ decision: 'published' }) });
    console.log(result.status, await result.text());
  });
  process.once('SIGINT', async () => {
    clearInterval(timer);
    input.close();
    server.close();
    await fs.rm(config, { force: true });
    await pilot.mf.dispose();
  });
}

module.exports = { makePilot, engine, rules };
if (require.main === module)
  main().catch(error => {
  console.error(error.message);
  if (error.cause?.stderr) console.error(error.cause.stderr.trim());
  process.exitCode = 1;
});
