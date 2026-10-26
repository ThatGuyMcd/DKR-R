// Exercise the deployed Worker entry point with Cloudflare's actual local D1
// implementation. Plain SQLite does not enforce all of D1's SQL limits.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const {schema, draft} = require('./support.cjs');
const {Miniflare} = require(path.resolve(process.argv[3] || 'build/library-workflow-d1-runtime/node_modules/miniflare'));
const stage = path.resolve(process.argv[2]);
let checks = 0;
function check(value, label) { assert.ok(value, label); checks++; }
function equal(actual, expected, label) { assert.equal(actual, expected, label); checks++; }

async function main() {
  const mf = new Miniflare({
    modules: true,
    scriptPath: path.join(stage, 'PRIVATE-API/worker-single-file.mjs'),
    compatibilityDate: '2026-07-30',
    d1Databases: ['DB'],
    bindings: {
      FRONTEND_URL: 'https://positron-portal.com/dkr-r/index.html',
      DOWNLOAD_ORIGINS: JSON.stringify(['https://positron-portal.com/dkr-r/downloads/']),
      IMAGE_ORIGINS: JSON.stringify(['https://positron-portal.com/dkr-r/images/'])
    }
  });
  try {
    const db = await mf.getD1Database('DB');
    await db.exec(schema);
    await db.exec(`ALTER TABLE library_upload_jobs ADD COLUMN category TEXT;
ALTER TABLE library_upload_jobs ADD COLUMN format TEXT;
ALTER TABLE library_upload_jobs ADD COLUMN file_name TEXT;
ALTER TABLE library_upload_jobs ADD COLUMN byte_offset INTEGER;`);
    const now = Math.floor(Date.now() / 1000), tokens = {};
    for (const role of ['admin', 'moderator', 'player']) {
      const id = 'local_' + role, token = id.padEnd(43, 'a');
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(id, id, 'Local ' + role, role, 'active', now, now).run();
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(token).digest('base64url'), id, now, now, now + 3600, now + 86400).run();
      tokens[role] = token;
    }
    // The snapshot has one image. Include completed website images as the live
    // inventory does, without opening an FTP connection or transferring bytes.
    for (let index = 1; index <= 3; index++) {
      const suffix = String(index).repeat(32), fileName = 'dkr-' + suffix + '-cover.png';
      await db.prepare(`INSERT INTO library_upload_jobs(id,owner_id,kind,total_size,original_name,state,
        completed_at,relative_path,category,format,file_name,byte_offset) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)`)
        .bind('up_' + suffix, 'local_admin', 'image', 10, 'cover.png', 'completed', now,
          'images/Tracks/' + fileName, 'tracks', '.png', fileName, 10).run();
    }
    async function call(route, body, role = 'moderator') {
      const headers = {Origin: 'https://positron-portal.com'};
      if (role) headers.Authorization = 'Bearer ' + tokens[role];
      if (body !== undefined) headers['Content-Type'] = 'application/json';
      const response = await mf.dispatchFetch('https://test.invalid' + route, {
        method: body === undefined ? 'GET' : 'POST', headers,
        body: body === undefined ? undefined : JSON.stringify(body)
      });
      return {status: response.status, data: await response.json()};
    }
    // Verify this test runtime enforces the limit that exposed the production bug.
    await assert.rejects(db.prepare('SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6').all(), /too many terms in compound SELECT/);
    checks++;
    equal((await call('/v1/assets', undefined, null)).status, 401, 'anonymous inventory access denied');
    equal((await call('/v1/assets', undefined, 'player')).status, 403, 'player inventory access denied');
    let result = await call('/v1/assets');
    equal(result.status, 200, 'FTP picker loads through the full Worker in D1: ' + JSON.stringify(result.data.error));
    const files = result.data.files;
    check(files.length > 0, 'existing FTP inventory retained');
    check(files.every(file => file.inLibrary === false), 'unused inventory initially has explicit false membership');
    const downloads = files.filter(file => file.kind === 'download');
    const images = files.filter(file => file.kind === 'image');
    check(downloads.length >= 5 && images.length >= 4, 'inventory contains the fixture selections');
    await db.prepare('INSERT INTO library_items(id,title,archived,image) VALUES(?,?,?,?)').bind('local_item', 'Archived local fixture', 1, images[0].url).run();
    for (const [index, file] of downloads.slice(0, 2).entries()) {
      await db.prepare('INSERT INTO library_releases(id,item_id,version,file_url) VALUES(?,?,?,?)').bind('local_release_' + index, 'local_item', String(index), file.url).run();
    }
    await db.prepare('INSERT INTO library_catalogue_drafts(id,owner_id,content_json,status) VALUES(?,?,?,?)').bind('local_draft', 'local_admin', JSON.stringify({fileUrl: downloads[2].url, image: images[1].url}), 'draft').run();
    const selected = downloads[3];
    result = await call('/v1/workflow/batches/' + 'a'.repeat(32), {
      expectedRevision: 0,
      batch: {title: 'D1 inventory fixture', rows: [{id: 'b'.repeat(32), draft: draft({
        category: selected.category, fileName: selected.fileName, fileUrl: selected.url,
        format: selected.format, image: images[2].url
      })}]}
    });
    equal(result.status, 200, 'saved batch works through D1: ' + JSON.stringify(result.data.error));
    // Older saved content must not make the entire picker fail to load.
    await db.prepare('INSERT INTO library_catalogue_drafts(id,owner_id,content_json,status) VALUES(?,?,?,?)').bind('legacy_broken_draft', 'local_admin', '{invalid', 'draft').run();
    await db.prepare('INSERT INTO library_workflow_batches(id,owner_id,content_json,created_at,updated_at) VALUES(?,?,?,?,?)').bind('c'.repeat(32), 'local_admin', '{invalid', now, now).run();
    await db.prepare('INSERT INTO library_workflow_batches(id,owner_id,content_json,created_at,updated_at) VALUES(?,?,?,?,?)').bind('d'.repeat(32), 'local_admin', JSON.stringify({rows: [null, 'old row']}), now, now).run();
    result = await call('/v1/assets');
    equal(result.status, 200, 'FTP picker loads with saved and malformed legacy content');
    const membership = new Map(result.data.files.map(file => [file.url, file.inLibrary]));
    for (const file of [...downloads.slice(0, 4), ...images.slice(0, 3)]) equal(membership.get(file.url), true, 'used file excluded from Not in Mod Library: ' + file.path);
    equal(membership.get(downloads[4].url), false, 'unused mod remains available');
    equal(membership.get(images[3].url), false, 'unused image remains available');
    equal(result.data.files.length, files.length, 'All filter retains every inventory file');
    result = await call('/health', undefined, null);
    equal(result.status, 200, 'health remains available');
    equal(result.data.workflowRevision, '20261023-2', 'corrected Worker revision reported');
    console.log('PASS:', checks, 'Cloudflare D1 runtime, full Worker entry point and FTP picker checks.');
  } finally {
    await mf.dispose();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
