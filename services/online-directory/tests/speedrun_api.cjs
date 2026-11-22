// Local Workers, SQLite-backed D1, and private R2 integration test.
const assert = require('node:assert/strict');
const { createHash, randomBytes } = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { Miniflare } = require('../../test-tools/node_modules/miniflare');
const { schema } = require('../../library-workflow/tests/support.cjs');

const origin = 'https://positron-portal.com';
const engine = 'a'.repeat(64);
const releaseHash = 'b'.repeat(64);
const verifierDigest = 'c'.repeat(64);

function replay(overrides = {}, samples = Buffer.alloc(240)) {
  const manifest = Buffer.from(JSON.stringify({
    board:'ancient-lake-car', engine, ruleset:'tt-1', releaseHash:null, rom:80,
    platform:'linux', arch:'x86_64', ticks:60, clockTicks:60, startSeed:7,
    checkpoints:[[60,'0'.repeat(64)]], splits:[60], ...overrides,
  }));
  const prefix = Buffer.alloc(12);
  prefix.write('DKRREPL1');
  prefix.writeUInt32LE(manifest.length, 8);
  return Buffer.concat([prefix, manifest, samples]);
}

async function main() {
  const root = path.resolve(process.argv[2]);
  const mf = new Miniflare({
    modules:true, modulesRoot:root, scriptPath:path.join(root,'PRIVATE-API/worker-single-file.mjs'),
    d1Databases:['DB'], r2Buckets:['SPEEDRUN_REPLAYS'],
    bindings:{FRONTEND_URL:origin + '/dkr-r/index.html', SPEEDRUN_UPLOADS_ENABLED:'true'}
  });
  try {
    const db = await mf.getD1Database('DB');
    await db.exec(schema);
    const migration = fs.readFileSync(path.join(root,'PRIVATE-API/003-speedruns.sql'),'utf8');
    const delimiter = migration.indexOf('CREATE TRIGGER');
    await db.exec(migration.slice(0, delimiter).replace(/^--.*$/gm, '').replace(/\s+/g, ' ').trim());
    await db.prepare(migration.slice(delimiter)).run();
    const tokens = {};
    const stamp = Math.floor(Date.now() / 1000);
    for (const [id, role] of [['alice','player'],['bob','player'],['reviewer','moderator']]) {
      tokens[id] = randomBytes(32).toString('base64url');
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(id,id,id,role,'active',stamp,stamp).run();
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(
        createHash('sha256').update(tokens[id]).digest('base64url'), id, stamp, stamp, stamp + 3600, stamp + 86400).run();
    }
    const fetch = (route, options = {}) => mf.dispatchFetch('https://review.invalid/v1/speedruns' + route, {
      ...options, headers:{Origin:origin,...(options.user ? {Authorization:'Bearer ' + tokens[options.user]} : {}),...options.headers}
    });
    const post = (id, body, from = origin) => fetch('/runs',{method:'POST',user:id,body,
      headers:{Origin:from,'Content-Type':'application/octet-stream'}});
    const review = (id, user, decision, reason = '') => fetch('/runs/' + id + '/review',{method:'POST',user,
      headers:{'Content-Type':'application/json'},body:JSON.stringify({decision,reason})});
    const submit = async (id, body) => {
      const response = await post(id,body);
      assert.equal(response.status,202,await response.clone().text());
      return (await response.json()).run;
    };
    const catalogue = async () => (await fetch('')).json();
    assert.equal((await catalogue()).canSubmit,false);
    await db.prepare("INSERT INTO library_items(id,title,category,archived,current_release_id) VALUES('custom','Custom course','tracks',0,'release1')").run();
    await db.prepare("INSERT INTO library_releases(id,item_id,version) VALUES('release1','custom','1.0')").run();
    await db.prepare("INSERT INTO library_releases(id,item_id,version) VALUES('release2','custom','1.1')").run();
    const addBoard = (id, kind, title, release = null, sha = null) => db.prepare('INSERT INTO speedrun_boards VALUES(?,?,?,?,?,?,?,?,?)')
      .bind(id,kind,title,'Time Trial','car','tt-1',engine,release,sha).run();
    await addBoard('ancient-lake-car','vanilla','Ancient Lake');
    await addBoard('custom-car','custom','Custom course','release1',releaseHash);
    let state = await catalogue();
    assert.equal(state.canSubmit,true);
    assert.equal(state.boards.length,1);
    assert.equal(state.customTracks[0].qualified,false);
    await db.prepare("INSERT INTO launcher_release_integrity VALUES('release1',?,?,?)").bind(releaseHash,128,stamp).run();
    state = await catalogue();
    assert.equal(state.boards.length,2);
    assert.equal(state.customTracks[0].qualified,true);
    await db.prepare("UPDATE library_items SET current_release_id='release2' WHERE id='custom'").run();
    assert.equal((await catalogue()).customTracks[0].qualified,false,'new version has separate qualification');

    assert.equal((await post(null,replay())).status,401);
    assert.equal((await post('alice',replay(),'https://foreign.invalid')).status,403);
    for (const bad of [Buffer.alloc(4), Buffer.alloc(1024*1024+1), replay({ticks:61}), replay({rom:1}),
      replay({checkpoints:[[59,'0'.repeat(64)]]}), replay({verified:true}), replay({splits:[60,60]})])
      assert.equal((await post('alice',bad)).status,400);
    assert.equal((await post('alice',replay({engine:'f'.repeat(64)}))).status,409);
    assert.equal((await post('alice',replay({board:'custom-car'}))).status,409);
    const proof = replay();
    const alice = await submit('alice',proof);
    assert.equal(alice.state,'queued');
    assert.equal((await submit('alice',proof)).id,alice.id,'resubmission is idempotent');
    const alice2 = await submit('alice',replay({ clockTicks:59, splits:[59] }));
    const bob = await submit('bob',proof);
    assert.notEqual(alice.id,bob.id,'private replay identity is account scoped');
    assert.equal((await fetch('/runs/'+alice.id+'/replay')).status,404);
    assert.equal((await fetch('/runs/'+alice.id+'/replay',{user:'bob'})).status,404);
    assert.deepEqual(Buffer.from(await (await fetch('/runs/'+alice.id+'/replay',{user:'alice'})).arrayBuffer()),proof);
    assert.equal((await review(alice.id,'alice','published')).status,403);
    assert.equal((await review(alice.id,'reviewer','published')).status,409,'a moderator cannot bypass verification');
    assert.equal((await fetch('/review',{user:'alice'})).status,403);
    assert.equal((await (await fetch('/mine',{user:'alice'})).json()).runs.length,2);
    assert.equal((await (await fetch('/review',{user:'reviewer'})).json()).runs.length,0);
    const entries = async () => (await (await fetch('/boards/ancient-lake-car')).json()).entries;
    assert.deepEqual(await entries(),[]);
    // Seed only the trust boundary result. This fixture does not attest engine determinism.
    for (const [id,ticks] of [[alice.id,65],[alice2.id,64],[bob.id,64]]) {
      await db.prepare("UPDATE speedrun_runs SET state='verified',verified_ticks=?,verifier_digest=? WHERE id=?")
        .bind(ticks,verifierDigest,id).run();
      assert.equal((await review(id,'reviewer','published')).status,200);
    }
    const leaderboard = await entries();
    assert.equal(leaderboard.length,2,'one PB per account');
    assert.equal(leaderboard[0].ticks,64,'time comes from verifier result, not upload');
    assert.equal(leaderboard[1].place,1,'equal times share rank');
    assert.deepEqual(Buffer.from(await (await fetch('/runs/'+alice2.id+'/replay')).arrayBuffer()),replay({clockTicks:59,splits:[59]}));
    assert.equal((await review(alice2.id,'reviewer','published')).status,409);
    await db.prepare("UPDATE accounts SET status='deleted' WHERE id='alice'").run();
    assert.equal((await entries()).length,1);
    assert.equal((await fetch('/runs/'+alice2.id+'/replay')).status,404);
    console.log('Ranked API passed: catalogue, version gate, bounded replay upload, retry, privacy, verification gate, moderation and PB ranking.');
  } finally { await mf.dispose(); }
}
main().catch(error => { console.error(error); process.exitCode=1; });
