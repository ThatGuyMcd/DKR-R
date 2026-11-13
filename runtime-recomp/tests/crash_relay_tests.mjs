import { test } from 'node:test';
import assert from 'node:assert/strict';
import worker, { CrashReportInbox, diagnosticZip, MAX_BYTES } from '../src/game/crash_reports/backend/worker.mjs';

function zip(files = { 'manifest.json': '{}', 'runtime.log': '[boot] synthetic', 'runtime-previous.log': '[boot][crash] synthetic' }) {
  const local = [], central = []; let at = 0;
  for (const [filename, value] of Object.entries(files)) {
    const name = Buffer.from(filename), content = Buffer.from(value);
    const header = Buffer.alloc(30); header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4);
    header.writeUInt32LE(content.length, 18); header.writeUInt32LE(content.length, 22); header.writeUInt16LE(name.length, 26);
    const dir = Buffer.alloc(46); dir.writeUInt32LE(0x02014b50); dir.writeUInt16LE(20, 4); dir.writeUInt16LE(20, 6);
    dir.writeUInt32LE(content.length, 20); dir.writeUInt32LE(content.length, 24); dir.writeUInt16LE(name.length, 28); dir.writeUInt32LE(at, 42);
    local.push(header, name, content); central.push(dir, name); at += header.length + name.length + content.length;
  }
  const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50); end.writeUInt16LE(Object.keys(files).length, 8); end.writeUInt16LE(Object.keys(files).length, 10);
  end.writeUInt32LE(Buffer.concat(central).length, 12); end.writeUInt32LE(at, 16);
  return Buffer.concat([...local, ...central, end]);
}
function fixture() {
  const store = new Map(); let serial = Promise.resolve();
  const ctx = { storage: { get: async key => store.get(key), put: async (key, value) => { store.set(key, value); },
    delete: async key => store.delete(key), list: async ({ prefix }) => new Map([...store].filter(([key]) => key.startsWith(prefix))), setAlarm: async () => {} },
  blockConcurrencyWhile(action) { const next = serial.then(action); serial = next.catch(() => {}); return next; } };
  const env = { DKR_CRASH_WEBHOOK: 'https://discord.com/api/webhooks/123/TEST_ONLY' };
  return { store, ctx, env, inbox: new CrashReportInbox(ctx, env) };
}
const id = 'a'.repeat(32);
const request = (bytes = zip(), reportId = id) => new Request(`https://collector.test/v1/crash-reports/${reportId}`, {
  method: 'POST', headers: { 'Content-Type': 'application/zip', 'CF-Connecting-IP': '192.0.2.1' }, body: bytes,
});

test('archive admission rejects arbitrary files, traversal, truncation and oversized data', () => {
  assert(diagnosticZip(zip()));
  assert(diagnosticZip(zip({ 'manifest.json': '{}', 'runtime.log': '', 'runtime-previous.log': '', 'crash.dmp': 'MDMP synthetic' })));
  assert(!diagnosticZip(zip({ 'manifest.json': '{}', 'runtime.log': '', '../runtime-previous.log': '' })));
  assert(!diagnosticZip(zip({ 'manifest.json': '{}', 'runtime.log': '', 'runtime-previous.log': '', 'credentials.ini': '' })));
  assert(!diagnosticZip(zip().subarray(0, 40)));
  assert(!diagnosticZip(new Uint8Array(MAX_BYTES + 1)));
});
test('only confirmed Discord delivery acks, receipts deduplicate concurrent retries', async () => {
  const { inbox, store } = fixture(); const original = globalThis.fetch; let calls = 0;
  globalThis.fetch = async (url, init) => {
    ++calls; assert(url.endsWith('?wait=true')); assert.equal(init.redirect, 'manual');
    const form = init.body, payload = JSON.parse(form.get('payload_json'));
    assert.deepEqual(payload.allowed_mentions, { parse: [] });
    assert(!JSON.stringify(payload).includes('192.0.2.1')); assert(form.get('files[0]').size > 0);
    return Response.json({ id: '123456789' });
  };
  try {
    const responses = await Promise.all([inbox.fetch(request()), inbox.fetch(request())]);
    assert.equal(calls, 1); assert(responses.every(response => response.status === 200));
    assert.deepEqual(await responses[0].json(), { reportId: id, delivered: true });
    assert.equal((await inbox.fetch(request(zip({ 'manifest.json': '{}', 'runtime.log': 'different', 'runtime-previous.log': '' })))).status, 409);
    assert(!JSON.stringify([...store]).includes('192.0.2.1'));
  } finally { globalThis.fetch = original; }
});
test('network loss, unconfirmed responses and rate limits leave client retryable', async () => {
  const { inbox, store } = fixture(); const original = globalThis.fetch;
  try {
    globalThis.fetch = async () => { throw new Error('offline'); };
    assert.equal((await inbox.fetch(request())).status, 503); assert(!store.has(`receipt:${id}`));
    globalThis.fetch = async () => new Response(null, { status: 204 });
    assert.equal((await inbox.fetch(request())).status, 503);
    globalThis.fetch = async () => Response.json({ retry_after: 125.5 }, { status: 429 });
    const limited = await inbox.fetch(request()); assert.equal(limited.status, 429); assert.equal(limited.headers.get('Retry-After'), '126');
    let called = false; globalThis.fetch = async () => { called = true; return Response.json({ id: '1' }); };
    assert.equal((await inbox.fetch(request())).status, 429); assert(!called);
  } finally { globalThis.fetch = original; }
});
test('revoked webhook is suspended until an administrator changes its secret', async () => {
  const { inbox, env } = fixture(); const original = globalThis.fetch; let calls = 0;
  globalThis.fetch = async () => { ++calls; return new Response(null, { status: 404 }); };
  try {
    assert.equal((await inbox.fetch(request())).status, 503);
    assert.equal((await inbox.fetch(request())).status, 503); assert.equal(calls, 1);
    env.DKR_CRASH_WEBHOOK += '_ROTATED';
    await inbox.fetch(request()); assert.equal(calls, 2);
  } finally { globalThis.fetch = original; }
});
test('outer endpoint requires configured controls and rate limits without forwarding IP', async () => {
  assert.equal((await worker.fetch(request(), {})).status, 503);
  const env = { DKR_CRASH_WEBHOOK: 'configured', CRASH_REPORT_RATE: { limit: async () => ({ success: false }) }, CRASH_REPORTS: {} };
  assert.equal((await worker.fetch(request(), env)).status, 429);
  assert.equal((await worker.fetch(new Request('https://collector.test/health'), {})).status, 200);
  env.CRASH_REPORT_RATE = { limit: async () => ({ success: true }) };
  env.CRASH_REPORTS = { idFromName: name => name, get: () => ({ async fetch(forwarded) {
    assert.equal(forwarded.headers.get('CF-Connecting-IP'), null);
    assert(diagnosticZip(new Uint8Array(await forwarded.arrayBuffer())));
    return Response.json({ reportId: id, delivered: true });
  } }) };
  assert.equal((await worker.fetch(request(), env)).status, 200);
});
