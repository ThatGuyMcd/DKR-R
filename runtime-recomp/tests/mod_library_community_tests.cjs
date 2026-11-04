// Real Worker/D1 and real website scripts, with synthetic accounts and no external writes.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { randomBytes, createHash } = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const { Miniflare } = require(path.resolve(process.argv[2] || 'build/library-workflow-d1-runtime/node_modules/miniflare'));
const { JSDOM } = require(path.resolve(process.argv[3] || 'build/library-community-tools/node_modules/jsdom'));
const esbuild = require(path.join(root, 'build/library-community-tools/node_modules/esbuild'));
const { schema } = require('../../patches/library-workflow/tests/support.cjs');
const source = path.join(root, 'runtime-recomp/src/game/mod_library/website');
const backend = path.join(root, 'runtime-recomp/src/game/online_profile/backend');
const output = path.join(root, 'build/mod-library-community-20261029/tests');
let checks = 0;
const equal = (a, b, label) => { assert.equal(a, b, label); checks++; };
const check = (value, label) => { assert.ok(value, label); checks++; };
const operation = () => randomBytes(16).toString('hex');
const origin = 'https://positron-portal.com';
const tokens = {};
const accounts = {};
const contexts = [];
fs.mkdirSync(output, { recursive: true });
esbuild.buildSync({ entryPoints: [path.join(backend, 'worker.mjs')], bundle: true, format: 'esm', platform: 'neutral',
  target: 'es2022', external: ['cloudflare:*'], legalComments: 'inline', outfile: path.join(output, 'worker.mjs') });

async function main() {
  const mf = new Miniflare({ modules: true, scriptPath: path.join(output, 'worker.mjs'), compatibilityDate: '2026-07-30',
    d1Databases: ['DB'], bindings: { FRONTEND_URL: origin + '/dkr-r/index.html', DOWNLOAD_ORIGINS: JSON.stringify([origin + '/dkr-r/downloads/']),
      IMAGE_ORIGINS: JSON.stringify([origin + '/dkr-r/images/']) }, outboundService: async () => { throw new Error('External requests disabled in community tests.'); } });
  try {
    const db = await mf.getD1Database('DB');
    await db.exec(schema);
    await db.exec(fs.readFileSync(path.join(backend, 'community_schema.sql'), 'utf8').replace(/--[^\n]*/g, '').replace(/\s+/g, ' '));
    const stamp = Math.floor(Date.now() / 1000);
    for (const role of ['player', 'other', 'moderator', 'admin', 'banned']) {
      const id = 'community_' + role, token = randomBytes(32).toString('base64url'); tokens[role] = token;
      accounts[role] = { id, displayName: 'Fixture ' + role, role: ['admin', 'moderator'].includes(role) ? role : 'player', status: role === 'banned' ? 'banned' : 'active' };
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(id, id, accounts[role].displayName, accounts[role].role, accounts[role].status, stamp, stamp).run();
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(token).digest('base64url'), id, stamp, stamp, stamp + 3600, stamp + 86400).run();
    }
    for (let index = 0; index < 14; index++) {
      const id = index === 0 ? 'mario' : index === 1 ? 'farm' : 'track_' + index;
      const title = index === 0 ? 'Mario Circuit' : index === 1 ? 'Moo Moo Farm' : 'Fixture Track ' + index;
      await db.prepare(`INSERT INTO library_items(id,title,author,category,summary,image,tags_json,related_json,featured,current_release_id,created_at,updated_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?,?)`).bind(id, title, 'Fixture creator', 'tracks', 'A useful fixture track.', 'pattern', '["Multiplayer"]', '[]', index < 3 ? 1 : 0, 'release_' + id, stamp + index, stamp + index).run();
      await db.prepare('INSERT INTO library_releases(id,item_id,version,file_name,file_url,format,requirements,install,created_at) VALUES(?,?,?,?,?,?,?,?,?)')
        .bind('release_' + id, id, '1.0', id + '.zip', origin + '/dkr-r/downloads/Tracks/' + id + '.zip', '.zip', '', 'Import this package.', stamp).run();
    }
    await db.prepare('INSERT INTO library_releases(id,item_id,version,file_url,created_at) VALUES(?,?,?,?,?)')
      .bind('release_mario_old', 'mario', '0.9', origin + '/dkr-r/downloads/Tracks/mario-old.zip', stamp - 10).run();
    async function call(route, body, role, extra = {}) {
      return mf.dispatchFetch('https://test.invalid' + route, { method: extra.method || (body === undefined ? 'GET' : 'POST'), redirect: 'manual',
        headers: { Origin: origin, ...(role ? { Authorization: 'Bearer ' + tokens[role] } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...(extra.headers || {}) },
        ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
    }
    async function jsonCall(route, body, role, extra) { const response = await call(route, body, role, extra); return { status: response.status, data: await response.json() }; }
    async function total(itemId = 'mario') { const response = await jsonCall('/v1/catalogue'); equal(response.status, 200, 'catalogue available'); return response.data.items.find(item => item.id === itemId).downloadCount; }
    const health = await jsonCall('/health'); equal(health.data.community.ready, true, 'migration reported ready');
    equal(await total(), 0, 'new counters start at zero');
    let response = await call('/v1/downloads/release_mario?attempt=' + operation(), undefined, null, { method: 'HEAD' }); equal(response.status, 302, 'HEAD redirects');
    await call('/v1/downloads/release_mario?attempt=' + operation(), undefined, null, { headers: { 'Sec-Purpose': 'prefetch' } });
    await call('/v1/downloads/release_mario?attempt=' + operation(), undefined, null, { headers: { 'User-Agent': 'FixtureCrawler bot' } });
    equal(await total(), 0, 'HEAD, prefetch and obvious bots do not count');
    const downloadId = operation();
    response = await call('/v1/downloads/release_mario?attempt=' + downloadId); equal(response.status, 302, 'download redirects');
    equal(response.headers.get('Location'), origin + '/dkr-r/downloads/Tracks/mario.zip', 'stored approved target retained');
    await call('/v1/downloads/release_mario?attempt=' + downloadId); equal(await total(), 1, 'retry counted once');
    const parallel = await Promise.all(Array.from({ length: 12 }, () => call('/v1/downloads/release_mario?attempt=' + operation())));
    check(parallel.every(value => value.status === 302), 'concurrent downloads all redirect'); equal(await total(), 13, 'atomic increments retain every accepted request');
    await call('/v1/downloads/release_mario_old?attempt=' + operation()); equal(await total(), 14, 'old and current releases aggregate');
    equal((await call('/v1/downloads/missing')).status, 404, 'missing release rejected');
    equal((await call('/v1/downloads/release_mario?attempt=invalid')).status, 400, 'invalid retry identifier rejected');
    await db.prepare('UPDATE library_items SET archived=1 WHERE id=?').bind('track_13').run();
    equal((await call('/v1/downloads/release_track_13')).status, 404, 'archived release rejected');
    await db.prepare('UPDATE library_releases SET file_url=? WHERE id=?').bind('https://evil.invalid/file.zip', 'release_track_12').run();
    equal((await call('/v1/downloads/release_track_12')).status, 400, 'stored external redirect must still be approved');
    const rateResponses = await Promise.all(Array.from({ length: 40 }, () => call('/v1/downloads/release_farm?attempt=' + operation(), undefined, null,
      { headers: { 'CF-Connecting-IP': '203.0.113.42' } })));
    check(rateResponses.every(value => value.status === 302), 'rate suppression never blocks the release');
    const accepted = await total('farm'); check(accepted >= 30 && accepted <= 40, 'rate limit bounds accepted counts, including a possible minute boundary');
    equal((await jsonCall('/v1/mods/mario/comments')).status, 200, 'public discussion can be read anonymously');
    equal((await jsonCall('/v1/mods/mario/comments', { text: 'Hello', operationId: operation() })).status, 401, 'posting requires authentication');
    equal((await jsonCall('/v1/mods/mario/comments', { text: 'Hello', operationId: operation() }, 'banned')).status, 401, 'inactive account cannot establish a posting session');
    equal((await jsonCall('/v1/mods/mario/comments', { text: 'Hello', operationId: operation() }, 'player', { headers: { Origin: 'https://evil.invalid' } })).status, 403, 'comment write validates origin');
    equal((await jsonCall('/v1/mods/mario/comments', { text: ' ', operationId: operation() }, 'player')).status, 400, 'empty comments rejected');
    equal((await jsonCall('/v1/mods/mario/comments', { text: 'x'.repeat(2001), operationId: operation() }, 'player')).status, 400, 'comment length checked on server');
    const postOp = operation(), post = { text: '<img src=x onerror=alert(1)> A plain-text question.', operationId: postOp };
    let result = await jsonCall('/v1/mods/mario/comments', post, 'player'); equal(result.status, 200, 'comment created');
    const comment = result.data.comment; equal(comment.text, post.text, 'comment text retained as text'); equal(comment.isOwn, true, 'owner marker returned to authenticated owner');
    result = await jsonCall('/v1/mods/mario/comments', post, 'player'); equal(result.data.comment.id, comment.id, 'creation retry returns original record');
    equal((await jsonCall('/v1/mods/mario/comments', { ...post, text: 'Different' }, 'player')).status, 409, 'reused key with different body rejected');
    const publicComments = await jsonCall('/v1/mods/mario/comments'); equal(publicComments.data.comments[0].isOwn, false, 'anonymous readers do not get ownership');
    check(!('userId' in publicComments.data.comments[0]) && !('accountId' in publicComments.data.comments[0]), 'public DTO omits account identifiers');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Hijack', expectedRevision: 1, operationId: operation() }, 'other')).status, 403, 'another player cannot edit');
    const editOp = operation();
    result = await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Updated question', expectedRevision: 1, operationId: editOp }, 'player');
    equal(result.data.comment.revision, 2, 'owner edit increments revision');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Updated question', expectedRevision: 1, operationId: editOp }, 'player')).status, 200, 'edit retry is idempotent');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Changed key reuse', expectedRevision: 2, operationId: editOp }, 'player')).status, 409, 'edit key cannot silently discard a changed body');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Stale', expectedRevision: 1, operationId: operation() }, 'player')).status, 409, 'stale owner edit rejected');
    equal((await jsonCall('/v1/comments/' + comment.id + '/report', { reason: 'Spam or advertising' }, 'player')).status, 400, 'self-report redirects player to owner controls');
    result = await jsonCall('/v1/comments/' + comment.id + '/report', { reason: 'Spam or advertising' }, 'other'); equal(result.status, 200, 'player report accepted');
    await jsonCall('/v1/comments/' + comment.id + '/report', { reason: 'Spam or advertising' }, 'other');
    equal((await jsonCall('/v1/community/moderation', undefined, 'player')).status, 403, 'staff queue role enforced');
    result = await jsonCall('/v1/community/moderation', undefined, 'moderator'); equal(result.data.reports.length, 1, 'duplicate report is deduplicated');
    const report = result.data.reports[0];
    equal((await jsonCall('/v1/mods/mario/comments')).data.total, 1, 'report does not automatically hide a comment');
    const decision = { action: 'hide', reason: 'Fixture moderation reason', expectedRevision: 2, reportId: report.id, expectedReportRevision: 1, operationId: operation() };
    equal((await jsonCall('/v1/comments/' + comment.id + '/moderate', decision, 'player')).status, 403, 'ordinary player cannot moderate');
    equal((await jsonCall('/v1/comments/' + comment.id + '/moderate', { ...decision, reason: '' }, 'moderator')).status, 400, 'moderation reason required');
    equal((await jsonCall('/v1/comments/' + comment.id + '/moderate', decision, 'moderator')).status, 200, 'moderator hides reported comment');
    equal((await jsonCall('/v1/comments/' + comment.id + '/moderate', decision, 'moderator')).status, 200, 'moderation retry does not duplicate audit');
    equal((await jsonCall('/v1/mods/mario/comments')).data.total, 0, 'hidden comment excluded from public count');
    equal((await db.prepare('SELECT COUNT(*) AS n FROM library_audit WHERE action=?').bind('comment.hide').first()).n, 1, 'one audit entry for a retried hide');
    result = await jsonCall('/v1/comments/' + comment.id, { action: 'edit', text: 'Edited while hidden', expectedRevision: 3, operationId: operation() }, 'player');
    equal(result.status, 200, 'owner can edit their hidden record'); equal((await jsonCall('/v1/mods/mario/comments')).data.total, 0, 'owner edit cannot restore moderation visibility');
    equal((await jsonCall('/v1/comments/' + comment.id + '/moderate', { action: 'restore', reason: 'Fixture restored', expectedRevision: 4, operationId: operation() }, 'moderator')).status, 200, 'hidden comment restored by staff');
    equal((await jsonCall('/v1/mods/mario/comments')).data.total, 1, 'restored comment included in count');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'delete', expectedRevision: 5, operationId: operation() }, 'other')).status, 403, 'another account cannot delete');
    equal((await jsonCall('/v1/comments/' + comment.id, { action: 'delete', expectedRevision: 5, operationId: operation() }, 'player')).status, 200, 'owner deletion accepted');
    equal((await jsonCall('/v1/mods/mario/comments')).data.total, 0, 'deleted comment excluded from public count');
    const deletedReportComment = (await jsonCall('/v1/mods/mario/comments', { text: 'Deleted while awaiting moderation', operationId: operation() }, 'other')).data.comment;
    await jsonCall('/v1/comments/' + deletedReportComment.id + '/report', { reason: 'Off-topic or misleading content' }, 'player');
    await jsonCall('/v1/comments/' + deletedReportComment.id, { action: 'delete', expectedRevision: 1, operationId: operation() }, 'other');
    const deletedReport = (await jsonCall('/v1/community/moderation', undefined, 'moderator')).data.reports[0];
    equal(deletedReport.comment.deleted, true, 'pending report identifies an author-deleted comment');
    equal((await jsonCall('/v1/comments/' + deletedReportComment.id + '/moderate', { action: 'keep', reason: 'Player already removed the comment', expectedRevision: 2,
      reportId: deletedReport.id, expectedReportRevision: 1, operationId: operation() }, 'moderator')).status, 200, 'staff can dismiss a report after owner deletion');
    equal((await jsonCall('/v1/community/moderation', undefined, 'moderator')).data.reports.length, 0, 'deleted-comment report no longer clogs queue');
    equal((await jsonCall('/v1/mods/mario/comments?cursor=bad')).status, 400, 'invalid cursor rejected');
    await db.batch(Array.from({ length: 30 }, (_, index) => db.prepare(`INSERT INTO library_comments(id,item_id,account_id,operation_id,body,created_at,updated_at,last_mutation)
      VALUES(?,?,?,?,?,?,?,?)`).bind('page_' + String(index).padStart(2, '0'), 'mario', accounts.other.id, operation(), 'Paged comment ' + index, stamp + 100, stamp + 100, operation())));
    const pageOne = (await jsonCall('/v1/mods/mario/comments')).data;
    const pageTwo = (await jsonCall('/v1/mods/mario/comments?cursor=' + encodeURIComponent(pageOne.nextCursor))).data;
    equal(pageOne.comments.length, 25, 'first public page bounded'); equal(pageTwo.comments.length, 5, 'cursor returns remaining comments');
    equal(new Set(pageOne.comments.concat(pageTwo.comments).map(value => value.id)).size, 30, 'same-timestamp pagination has no duplicates or missing rows');
    await db.prepare('ALTER TABLE library_download_totals RENAME TO unavailable_download_totals').run();
    equal((await call('/v1/downloads/release_mario?attempt=' + operation())).status, 302, 'failed counter storage still redirects approved download');
    result = await jsonCall('/v1/catalogue'); equal(result.status, 200, 'counter outage does not break catalogue');
    equal(result.data.items.find(item => item.id === 'mario').downloadCount, null, 'unavailable counter is not fabricated as zero');
    await db.prepare('ALTER TABLE unavailable_download_totals RENAME TO library_download_totals').run();
    // Apply migration twice: deploy retries are additive and preserve discussion.
    await db.exec(fs.readFileSync(path.join(backend, 'community_schema.sql'), 'utf8').replace(/--[^\n]*/g, '').replace(/\s+/g, ' '));
    equal((await jsonCall('/v1/mods/mario/comments')).data.total, 30, 'migration retry retains data');
    await websiteTests(mf, db);
    console.log('Mod library community: ' + checks + ' checks passed (Worker/D1 and website).');
  } finally {
    for (const context of contexts) context.window.close();
    await mf.dispose();
  }
}

async function websiteTests(mf, db) {
  const dom = new JSDOM(fs.readFileSync(path.join(source, 'index.html'), 'utf8'), { url: origin + '/dkr-r/', runScripts: 'outside-only', pretendToBeVisual: true });
  contexts.push(dom); const w = dom.window;
  const listeners = new Set(), motionListeners = new Set(), heroTimers = new Map();
  let activeRole = 'admin', token = tokens.admin, media = false, sequence = 100000, D;
  const realSetTimeout = w.setTimeout.bind(w), realClearTimeout = w.clearTimeout.bind(w);
  w.setTimeout = (fn, ms, ...args) => { if (ms >= 25000 && ms <= 30000) { const id = sequence++; heroTimers.set(id, { fn: () => fn(...args), ms }); return id; } return realSetTimeout(fn, ms, ...args); };
  w.clearTimeout = id => { if (heroTimers.has(id)) heroTimers.delete(id); else realClearTimeout(id); };
  const mediaQuery = { get matches() { return media; }, addEventListener: (_name, fn) => motionListeners.add(fn), removeEventListener: (_name, fn) => motionListeners.delete(fn) };
  w.matchMedia = () => mediaQuery;
  w.DKR_CONFIG = { apiBase: 'https://test.invalid', discordInvite: 'https://discord.com/invite/fixture' };
  w.DKRAuth = { ready: Promise.resolve(), getUser: () => activeRole ? accounts[activeRole] : null, getToken: () => token,
    ensureSession: async () => {}, onChange: fn => { listeners.add(fn); return () => listeners.delete(fn); } };
  w.fetch = (url, options = {}) => mf.dispatchFetch(url, { method: options.method || 'GET', body: options.body, redirect: 'manual', headers: { ...options.headers, Origin: origin } });
  w.lucide = { createIcons() {} };
  for (const name of ['api-store.js', 'community-store.js', 'community-ui.js']) w.eval(fs.readFileSync(path.join(source, 'assets', name), 'utf8'));
  const originalCreate = w.createLibraryAPI;
  w.createLibraryAPI = function (...args) { D = originalCreate(...args); return D; };
  w.eval(fs.readFileSync(path.join(source, 'assets/app.js'), 'utf8'));
  const $ = selector => w.document.querySelector(selector);
  async function until(fn, label) { for (let n = 0; n < 180; n++) { if (fn()) return; await new Promise(resolve => setTimeout(resolve, 10)); } throw new Error(label + ': ' + w.document.body.textContent.slice(-1300)); }
  const click = async selector => { await until(() => $(selector) && !$(selector).disabled, 'control enabled: ' + selector); $(selector).click(); await new Promise(resolve => setTimeout(resolve, 0)); };
  const input = (element, value) => { element.value = value; element.dispatchEvent(new w.Event('input', { bubbles: true })); };
  const submit = (form, button) => form.dispatchEvent(new w.SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: button }));
  await until(() => $('.dkr-modcard'), 'initial catalogue loads');
  check(D.isAdmin(), 'website uses existing authenticated staff account');
  check($('.dkr-download-count'), 'card badges rendered'); equal(heroTimers.size, 1, 'one initial featured timer');
  const initialTitle = $('.dkr-feature-frame.is-current h1').textContent;
  const advance = [...heroTimers.values()][0]; check(advance.ms <= 30000 && advance.ms >= 29900, 'rotation scheduled for thirty seconds'); heroTimers.clear(); advance.fn();
  check($('.dkr-feature-frame.is-current h1').textContent !== initialTitle, 'timer advances featured content');
  equal(heroTimers.size, 1, 'rotation schedules exactly one next timer');
  const hero = $('[data-featured-hero]'); hero.dispatchEvent(new w.MouseEvent('mouseenter')); equal(heroTimers.size, 0, 'hover pauses auto rotation');
  hero.dispatchEvent(new w.MouseEvent('mouseleave')); equal(heroTimers.size, 1, 'leaving hero resumes rotation');
  await click('[data-action="category"][data-id="tracks"]'); equal(heroTimers.size, 1, 'category render does not accumulate timers');
  media = true; for (const fn of motionListeners) fn({ matches: true }); equal(heroTimers.size, 0, 'reduced motion disables autoplay');
  await click('[data-action="category"][data-id="characters"]'); equal(w.document.querySelectorAll('.dkr-enter').length, 0, 'reduced motion suppresses entrance animations');
  await click('[data-action="moderation"]'); await until(() => $('[data-action="admin"]'), 'moderation workspace loads');
  await click('[data-action="admin"]'); await until(() => $('[data-action="add-listing"]'), 'library manager loads');
  await click('[data-action="add-listing"]'); await until(() => $('form[data-form="publication"]'), 'publication form loads');
  const form = $('form[data-form="publication"]'), picker = $('.dkr-companions'), search = $('[data-companion-search]');
  const mario = picker.querySelector('input[value="mario"]'); mario.checked = true; mario.dispatchEvent(new w.Event('input', { bubbles: true }));
  input(search, 'MOO'); equal(picker.querySelectorAll('[data-companion-row]:not([hidden])').length, 1, 'companion search is case insensitive');
  const farm = picker.querySelector('input[value="farm"]'); farm.checked = true; farm.dispatchEvent(new w.Event('input', { bubbles: true }));
  input(search, 'not-a-mod'); check(!picker.querySelector('[data-companion-empty]').hidden, 'empty companion results have feedback');
  equal(new w.FormData(form).getAll('related').length, 2, 'filtered-out selections stay successful form controls');
  equal(picker.querySelectorAll('.dkr-companion-chip').length, 2, 'selected chips remain visible while filtering');
  input(search, '');
  for (const checkbox of picker.querySelectorAll('input[name="related"]')) if (!checkbox.checked) { checkbox.checked = true; checkbox.dispatchEvent(new w.Event('input', { bubbles: true })); }
  equal(picker.querySelectorAll('input:checked').length, 10, 'ten-companion cap enforced before serialization');
  for (const [name, value] of Object.entries({ title: 'Website fixture release', author: 'Fixture creator', summary: 'A complete local publication fixture.', version: '1.0', fileUrl: origin + '/dkr-r/downloads/Tracks/frontend.zip', install: 'Import this package.' })) input(form.elements.namedItem(name), value);
  const expectedCompanions = [...new w.FormData(form).getAll('related')].sort().join(',');
  submit(form, form.querySelector('button[value="preview"]')); await until(() => $('[data-action="back-editor"]'), 'publication preview loads');
  await click('[data-action="back-editor"]'); await until(() => $('form[data-form="publication"]'), 'back to publication form');
  const resumed = $('form[data-form="publication"]'); equal([...new w.FormData(resumed).getAll('related')].sort().join(','), expectedCompanions, 'preview round trip preserves full companion selection');
  submit(resumed, resumed.querySelector('button[value="draft"]')); await until(() => D.catalogueDrafts.some(draft => draft.title === 'Website fixture release'), 'draft persisted to real D1');
  equal(D.catalogueDrafts.find(draft => draft.title === 'Website fixture release').related.slice().sort().join(','), expectedCompanions, 'saved draft retains all companions');
  await until(() => $('[data-action="store"]'), 'return navigation available'); await click('[data-action="store"]');
  await click('[data-action="category"][data-id="tracks"]'); await until(() => $('[data-action="detail"][data-id="mario"]'), 'library returns');
  await click('[data-action="detail"][data-id="mario"]'); await until(() => $('[data-action="detail-tab"][data-id="comments"]'), 'mod details load');
  await click('[data-action="detail-tab"][data-id="comments"]'); await until(() => $('.dkr-comment'), 'comments read through real API');
  let composer = $('[data-form="community-comment"]'); input(composer.elements.text, '<img src=x onerror="window.bad=true"> A useful tip.');
  await click('[data-action="detail-tab"][data-id="overview"]'); await click('[data-action="detail-tab"][data-id="comments"]');
  composer = $('[data-form="community-comment"]'); check(composer.elements.text.value.includes('A useful tip'), 'comment draft retained across tab navigation');
  submit(composer, composer.querySelector('button[type="submit"]')); await until(() => [...w.document.querySelectorAll('.dkr-comment-content>p')].some(p => p.textContent.includes('A useful tip')), 'new comment displayed');
  equal(w.document.querySelectorAll('.dkr-comment-content img').length, 0, 'comment markup escaped in the real website'); check(!w.bad, 'comment code never executed');
  await click('[data-community-action="comment-edit"]'); composer = $('[data-form="community-comment"]'); input(composer.elements.text, 'Edited useful tip.'); submit(composer, composer.querySelector('button[type="submit"]'));
  await until(() => [...w.document.querySelectorAll('.dkr-comment-content>p')].some(p => p.textContent === 'Edited useful tip.'), 'comment edited in place');
  await click('[data-community-action="comment-delete"]'); check($('[data-community-action="comment-confirm-delete"]'), 'owner deletion requires inline confirmation');
  await click('[data-community-action="comment-confirm-delete"]'); await until(() => ![...w.document.querySelectorAll('.dkr-comment-content>p')].some(p => p.textContent === 'Edited useful tip.'), 'owner comment deleted');
  await until(() => $('[data-form="community-comment"]') && !$('[data-form="community-comment"] button[type="submit"]').disabled, 'composer enabled');
  const workingFetch = w.fetch;
  w.fetch = (url, options) => String(url).endsWith('/comments') && options?.method === 'POST' ? Promise.reject(new Error('Fixture connection failed')) : workingFetch(url, options);
  composer = $('[data-form="community-comment"]'); input(composer.elements.text, 'Draft retained after failure.'); submit(composer, composer.querySelector('button[type="submit"]'));
  await until(() => w.document.body.textContent.includes('Fixture connection failed'), 'posting failure displayed');
  equal($('[data-form="community-comment"] textarea').value, 'Draft retained after failure.', 'failed write keeps the comment draft');
  w.fetch = workingFetch;
  await until(() => !$('[data-form="community-comment"] button[type="submit"]').disabled, 'retry enabled');
  composer = $('[data-form="community-comment"]'); submit(composer, composer.querySelector('button[type="submit"]'));
  await until(() => [...w.document.querySelectorAll('.dkr-comment-content>p')].some(p => p.textContent === 'Draft retained after failure.'), 'failed comment can be retried');
  equal((await db.prepare('SELECT COUNT(*) AS n FROM library_comments WHERE body=?').bind('Draft retained after failure.').first()).n, 1, 'retry produces one comment');
  let releaseRead;
  const heldRead = new Promise(resolve => { releaseRead = resolve; });
  let held = false;
  w.fetch = async (url, options) => { const response = await workingFetch(url, options); if (!held && String(url).endsWith('/comments') && options?.method === 'GET') { held = true; const data = await response.json(); await heldRead; data.comments[0].text = 'Stale response must not overwrite a newer read'; return { ok: true, json: async () => data }; } return response; };
  const olderRead = D.loadComments('mario'); await until(() => held, 'older comment read held');
  await D.loadComments('mario'); releaseRead(); await olderRead;
  check(!D.commentsFor('mario').comments.some(comment => comment.text === 'Stale response must not overwrite a newer read'), 'out-of-order reads cannot replace newer comments');
  w.fetch = workingFetch;
  const originalEnsure = w.DKRAuth.ensureSession;
  w.DKRAuth.ensureSession = async () => { activeRole = 'other'; token = tokens.other; for (const fn of listeners) fn(accounts.other); };
  await assert.rejects(D.saveComment('mario', { text: 'Must not cross accounts', operationId: operation() }), /account changed/); checks++;
  w.DKRAuth.ensureSession = originalEnsure;
  equal((await db.prepare('SELECT COUNT(*) AS n FROM library_comments WHERE body=?').bind('Must not cross accounts').first()).n, 0, 'account swap prevents stale form transmission');
  D.dispose(); w.close();
}
main().catch(error => { console.error(error); process.exitCode = 1; });
