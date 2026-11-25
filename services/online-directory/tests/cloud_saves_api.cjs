const assert = require('node:assert/strict');
const {randomBytes, createHash} = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {schema} = require('../../library-workflow/tests/support.cjs');

const bundle = path.resolve(process.argv[2]);
const {Miniflare} = require(path.resolve(process.argv[3]));
const route = '/v1/launcher/cloud-saves';
const origin = 'https://positron-portal.com';
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

async function main() {
  const common = {
    modules: true, modulesRoot: bundle,
    scriptPath: path.join(bundle, 'PRIVATE-API/worker-single-file.mjs'),
    d1Databases: {DB: 'cloud-saves-isolation-test'},
  };
  const mf = new Miniflare({workers: [
    {...common, name: 'public', routes: ['test.invalid/*'], bindings: {
      CLOUD_SAVES_ENABLED: 'true', FRONTEND_URL: origin + '/dkr-r/index.html',
    }},
    {...common, name: 'pilot', routes: ['pilot.test.invalid/*'], bindings: {
      CLOUD_SAVES_ENABLED: 'pilot', CLOUD_SAVES_PILOT_ACCOUNT: 'one', FRONTEND_URL: origin + '/dkr-r/index.html',
    }},
  ]});
  try {
    const pilotWorker = 'https://pilot.test.invalid';
    const db = await mf.getD1Database('DB', 'public');
    await db.exec(schema);
    await db.exec(fs.readFileSync(path.join(bundle, 'PRIVATE-API/005-cloud-saves.sql'), 'utf8').replace(/\s+/g, ' '));
    await db.exec(fs.readFileSync(path.join(bundle, 'PRIVATE-API/006-modded-adventure.sql'), 'utf8').replace(/\s+/g, ' '));
    const now = Math.floor(Date.now()/1000);
    for (const id of ['one', 'two'])
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)')
        .bind(id, id, id, 'player', 'active', now, now).run();
    async function signIn(account) {
      const token = randomBytes(32).toString('base64url');
      const tokenHash = createHash('sha256').update(token).digest('base64url');
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)')
        .bind(tokenHash, account, now, now, now + 3600, now + 86400).run();
      return token;
    }
    async function request(suffix='', token='', bytes, host='https://test.invalid') {
      return mf.dispatchFetch(host + route + suffix, {
        method: bytes === undefined ? 'GET' : 'PUT',
        headers: {Origin:origin,
          ...(token ? {Authorization:'Bearer ' + token} : {}),
          ...(bytes === undefined ? {} : {'Content-Type':'application/octet-stream'})},
        body: bytes,
      });
    }
    const desktop = await signIn('one'), deck = await signIn('one'), other = await signIn('two');
    const original = Buffer.alloc(512, 18);
    assert.equal((await request()).status, 401);
    assert.equal((await request('/offline-adventure?revision=0', other, Buffer.alloc(513))).status, 413);
    assert.equal((await request('/unknown?revision=0', other, original)).status, 404);
    assert.equal((await request('/offline-adventure?revision=0', desktop, original)).status, 200);
    const list = await request('', deck);
    assert.equal(list.headers.get('Cache-Control'), 'private, no-store');
    assert.deepEqual((await list.json()).saves.map(({key, revision, digest}) => ({key, revision, digest})),
      [{key:'offline-adventure', revision:1, digest:hash(original)}]);
    assert.deepEqual(Buffer.from(await (await request('/offline-adventure?revision=1', deck)).arrayBuffer()), original);
    assert.deepEqual((await (await request('', other)).json()).saves, []);
    assert.equal((await request('/offline-adventure?revision=1', other)).status, 409);
    const modified = Buffer.alloc(512, 38), rival = Buffer.alloc(512, 53);
    const races = await Promise.all([
      request('/offline-adventure?revision=1', deck, modified),
      request('/offline-adventure?revision=1', desktop, rival),
    ]);
    assert.deepEqual(races.map(result => result.status).sort(), [200, 409]);
    const winning = races[0].status === 200 ? modified : rival;
    assert.equal((await request('/offline-adventure?revision=1', deck)).status, 409);
    assert.deepEqual(Buffer.from(await (await request('/offline-adventure?revision=2', deck)).arrayBuffer()), winning);
    for (const [key, size] of [['modded-records', 622612], ['online-adventure', 512],
      ['online-records', 200], ['online-paks', 131072], ['offline-pak-4', 32768]]) {
      const save = Buffer.alloc(size, 41);
      assert.equal((await request('/' + key + '?revision=0', desktop, save)).status, 200);
      assert.deepEqual(Buffer.from(await (await request('/' + key + '?revision=1', deck)).arrayBuffer()), save);
    }
    const modded = Buffer.alloc(512, 63);
    const moddedNext = Buffer.alloc(512, 72);
    assert.equal((await request('/modded-adventure?revision=0', desktop, Buffer.alloc(513))).status, 413);
    assert.equal((await request('/modded-adventure?revision=0', desktop, modded)).status, 200);
    assert.deepEqual(Buffer.from(await (await request('/modded-adventure?revision=1', deck)).arrayBuffer()), modded);
    assert.equal((await request('/modded-adventure?revision=1', deck, moddedNext)).status, 200);
    assert.equal((await request('/modded-adventure?revision=1', desktop, modded)).status, 409);
    assert.deepEqual(Buffer.from(await (await request('/modded-adventure?revision=2', desktop)).arrayBuffer()), moddedNext);
    assert.deepEqual(Buffer.from(await (await request('/offline-adventure?revision=2', deck)).arrayBuffer()), winning);
    const splitInventory = (await (await request('?format=2', deck)).json()).saves;
    assert.equal(splitInventory.find(r => r.key === 'modded-adventure').digest, hash(moddedNext));
    assert.equal(splitInventory.find(r => r.key === 'offline-adventure').digest, hash(winning));
    assert.ok(!(await (await request('', deck)).json()).saves.some(r => r.key === 'modded-adventure'));
    assert.equal((await request('/modded-adventure?revision=2', other)).status, 409);
    assert.equal((await request('/modded-adventure?revision=2', deck, undefined, pilotWorker)).status, 409);
    const pilotImage = Buffer.alloc(512, 109);
    assert.equal((await request('', '', undefined, pilotWorker)).status, 401);
    assert.equal((await request('', other, undefined, pilotWorker)).status, 403);
    assert.deepEqual((await (await request('', deck, undefined, pilotWorker)).json()).saves, []);
    assert.equal((await request('/offline-adventure?revision=0', deck, pilotImage, pilotWorker)).status, 200);
    assert.deepEqual(Buffer.from(await (await request('/offline-adventure?revision=1', desktop, undefined, pilotWorker)).arrayBuffer()), pilotImage);
    assert.deepEqual(Buffer.from(await (await request('/offline-adventure?revision=2', desktop)).arrayBuffer()), winning);
    assert.equal((await request('/offline-adventure?revision=1', other, undefined, pilotWorker)).status, 403);
    await db.prepare("UPDATE accounts SET status='suspended' WHERE id='one'").run();
    assert.equal((await request('', deck)).status, 401);
    console.log('Cloud Saves: authenticated two-device sync, concurrent updates, payload limits, account isolation and private pilot separation passed.');
  } finally {
    await mf.dispose();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
