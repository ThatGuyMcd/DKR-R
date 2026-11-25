const assert = require('node:assert/strict');
const { randomBytes, createHash } = require('node:crypto');
const fs = require('node:fs/promises');
const path = require('node:path');
const { makePilot } = require('../run_local_ranked.cjs');

const origin = 'https://positron-portal.com';
async function main() {
  const bundle = path.resolve(process.argv[2]);
  const { db, mf, tokens, addBoard } = await makePilot(bundle);
  try {
    const stamp = Math.floor(Date.now()/1000);
    await db.prepare('ALTER TABLE library_requests ADD COLUMN title TEXT').run();
    await db.prepare("INSERT INTO library_requests VALUES('existing','alice',?,'review','Existing mod')").bind(stamp).run();
    const schema = await fs.readFile(path.join(bundle,'PRIVATE-API/004-notifications.sql'),'utf8');
    const triggers = /CREATE TRIGGER\b.*?\r?\nEND;/gs;
    async function migrate() {
      for (const statement of schema.replace(triggers,'').replace(/^--.*$/gm,'').split(';').map(sql=>sql.trim()).filter(Boolean))
        await db.prepare(statement).run();
      for (const [trigger] of schema.matchAll(triggers)) await db.prepare(trigger).run();
    }
    await migrate();
    await migrate();
    await addBoard('ancient-lake-car','vanilla','Ancient Lake');

    const request = (suffix, token, body, from = origin) =>
      mf.dispatchFetch('https://test.invalid/v1/notifications'+suffix,{
        method:body === undefined ? 'GET' : 'POST',
        headers:{Origin:from, ...(token ? {Authorization:'Bearer '+token}:{}),
          ...(body === undefined ? {} : {'Content-Type':'application/json'})},
        ...(body === undefined ? {} : {body:JSON.stringify(body)}),
      });
    const page = async (user, suffix = '') => {
      const response = await request(suffix,tokens[user]);
      assert.equal(response.status,200,await response.clone().text());
      assert.equal(response.headers.get('Cache-Control'),'no-store');
      return response.json();
    };

    assert.equal((await request('',null)).status,401);
    assert.equal((await request('',tokens.alice,undefined,'https://foreign.invalid')).status,403);
    assert.equal((await page('alice')).unread,0);
    assert.equal((await page('reviewer')).unread,1,'migration only seeds the pending queue once');
    const existing = (await page('reviewer')).events[0];
    assert.equal((await request('/read',tokens.alice,{id:existing.id})).status,404);
    await db.prepare("INSERT INTO library_requests VALUES('incoming','bob',?,'pending','Racing mod')").bind(stamp).run();
    const before = await page('reviewer');
    assert.equal(before.unread,2);
    assert.equal((await page('bob')).unread,0,'a new submission is a staff queue item');
    await db.prepare("UPDATE library_requests SET status='review' WHERE id='incoming'").run();
    await db.prepare("UPDATE library_requests SET status='review' WHERE id='incoming'").run();
    const personal = await page('bob');
    assert.deepEqual(personal.events.map(event=>[event.type,event.staff,event.status]),[['mod',false,'review']]);
    assert.equal((await page('alice')).unread,0);
    assert.equal((await request('/read',tokens.alice,{id:personal.events[0].id})).status,404);
    const secondDevice = randomBytes(32).toString('base64url');
    await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(
      createHash('sha256').update(secondDevice).digest('base64url'),'bob',stamp,stamp,stamp+3600,stamp+86400).run();
    assert.equal((await request('/read',tokens.bob,{id:personal.events[0].id})).status,200);
    assert.equal((await request('/read',tokens.bob,{id:personal.events[0].id})).status,200);
    assert.equal((await (await request('',secondDevice)).json()).unread,0,'reads sync between signed-in devices');

    const insertRun = "INSERT INTO speedrun_runs(id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at) VALUES('run','alice','ancient-lake-car','sha','ticket','{}','','queued',?,?)";
    await db.prepare(insertRun).bind(stamp,stamp).run();
    await db.prepare("UPDATE speedrun_runs SET state='verified',verified_ticks=100,verifier_digest=? WHERE id='run'").bind('a'.repeat(64)).run();
    const review = await page('reviewer');
    assert.equal(review.unread,3);
    assert.deepEqual(review.events[0].title,'Ancient Lake');
    const marked = await (await request('/read-all',tokens.reviewer,{through:before.through})).json();
    assert.equal(marked.unread,1,'a newer event stays unread after marking a snapshot');
    assert.equal(marked.events[0].type,'run');
    assert.equal((await page('alice')).events[0].status,'verified');
    await db.prepare("UPDATE accounts SET role='player' WHERE id='reviewer'").run();
    assert.equal((await page('reviewer')).events.length,0,'staff items disappear when access is removed');
    await db.prepare("UPDATE accounts SET role='admin' WHERE id='reviewer'").run();
    assert.equal((await page('reviewer')).unread,1,'admins see pending reviews');
    await db.prepare("UPDATE speedrun_runs SET state='published' WHERE id='run'").run();
    assert.equal((await page('alice')).events[0].status,'published');
    assert.equal((await page('reviewer')).unread,0,'published runs exit the staff queue');
    await db.prepare("UPDATE library_requests SET status='changes' WHERE id='incoming'").run();
    await db.prepare("UPDATE library_requests SET status='pending' WHERE id='incoming'").run();
    assert.equal((await page('reviewer')).unread,1,'a revised mod gets a new staff notification');
    assert.equal((await page('bob')).events[0].status,'pending');
    const newest = (await page('bob')).events[0].id;
    assert.equal((await page('bob','?before='+newest)).events[0].status,'changes');
    await db.prepare("DELETE FROM speedrun_runs WHERE id='run'").run();
    assert.equal((await page('alice')).events.length,0,'deletion cascades into notifications');
    console.log('Notifications passed: scope, role changes, queue transitions, read sync, pagination, and deletion.');
  } finally {
    await mf.dispose();
  }
}
main().catch(error=>{console.error(error);process.exitCode=1;});

