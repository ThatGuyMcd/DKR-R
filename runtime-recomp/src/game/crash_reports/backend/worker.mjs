// Dedicated, anonymous crash collector. No library/online account integration.
// The webhook is an encrypted Worker secret, never part of client downloads.
export const MAX_BYTES = 8 * 1024 * 1024;
const DAY = 86400000;
const headers = { 'Cache-Control': 'no-store' };
const reply = (status, value, retry = 0) => Response.json(value, {
  status, headers: { ...headers, ...(retry ? { 'Retry-After': String(retry) } : {}) },
});
const digest = async bytes => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
  .map(value => value.toString(16).padStart(2, '0')).join('');
export async function boundedBody(request) {
  if (Number(request.headers.get('Content-Length') || 0) > MAX_BYTES) throw new Error('size');
  const reader = request.body?.getReader();
  if (!reader) throw new Error('body');
  const chunks = []; let size = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > MAX_BYTES) throw new Error('size');
      chunks.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  const result = new Uint8Array(size); let at = 0;
  for (const chunk of chunks) { result.set(chunk, at); at += chunk.byteLength; }
  return result;
}
// Accept only the small, fixed diagnostic archive shape. Never unpack files
// onto disk, allow archive paths, or accept executable/html attachments.
export function diagnosticZip(bytes) {
  if (bytes.byteLength < 22 || bytes.byteLength > MAX_BYTES) return false;
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const allowed = new Set(['manifest.json', 'runtime.log', 'runtime-previous.log', 'crash.dmp']);
  const names = new Set(); let eocd = bytes.byteLength - 22;
  if (data.getUint32(eocd, true) !== 0x06054b50 || data.getUint16(eocd + 20, true) !== 0) return false;
  const count = data.getUint16(eocd + 10, true), start = data.getUint32(eocd + 16, true);
  if (count < 3 || count > 4 || data.getUint32(eocd + 4, true) !== 0 || data.getUint16(eocd + 8, true) !== count) return false;
  let at = start, total = 0;
  try {
    for (let i = 0; i < count; ++i) {
      if (at + 46 > eocd || data.getUint32(at, true) !== 0x02014b50) return false;
      const method = data.getUint16(at + 10, true), compressed = data.getUint32(at + 20, true);
      const plain = data.getUint32(at + 24, true), length = data.getUint16(at + 28, true);
      const extra = data.getUint16(at + 30, true), comment = data.getUint16(at + 32, true);
      const offset = data.getUint32(at + 42, true);
      const name = new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(at + 46, at + 46 + length));
      if (!allowed.has(name) || names.has(name) || ![0, 8].includes(method) || data.getUint16(at + 8, true) & 1) return false;
      if (plain > (name === 'crash.dmp' ? 32 * 1024 * 1024 : name === 'manifest.json' ? 8192 : 2 * 1024 * 1024)) return false;
      if (offset + 30 > start || data.getUint32(offset, true) !== 0x04034b50) return false;
      const localLength = data.getUint16(offset + 26, true), localExtra = data.getUint16(offset + 28, true);
      const localName = new TextDecoder().decode(bytes.subarray(offset + 30, offset + 30 + localLength));
      if (localName !== name || offset + 30 + localLength + localExtra + compressed > start) return false;
      names.add(name); total += plain; at += 46 + length + extra + comment;
    }
  } catch { return false; }
  return at === eocd && total <= 36 * 1024 * 1024 &&
    names.has('manifest.json') && names.has('runtime.log') && names.has('runtime-previous.log');
}

export class CrashReportInbox {
  constructor(ctx, env) { this.ctx = ctx; this.env = env; }
  async fetch(request) {
    // Serialise receipt checks and Discord delivery across every client. A
    // stable report ID makes ordinary offline retries idempotent. Discord has
    // no idempotency key; a crash after send but before receipt can duplicate.
    return this.ctx.blockConcurrencyWhile(() => this.deliver(request));
  }
  async deliver(request) {
    const id = new URL(request.url).pathname.split('/').pop();
    if (request.method !== 'POST' || !/^[a-f0-9]{32}$/.test(id || '') || request.headers.get('Content-Type') !== 'application/zip')
      return reply(400, { error: 'invalid_report' });
    const webhook = this.env.DKR_CRASH_WEBHOOK || '';
    if (!/^https:\/\/discord\.com\/api\/webhooks\/[0-9]+\/[A-Za-z0-9_-]+$/.test(webhook))
      return reply(503, { error: 'collector_not_configured' }, 3600);
    const secretHash = await digest(new TextEncoder().encode(webhook));
    if (await this.ctx.storage.get('invalid_secret') === secretHash)
      return reply(503, { error: 'collector_needs_attention' }, 3600);
    let bytes;
    try { bytes = await boundedBody(request); }
    catch { return reply(413, { error: 'report_too_large_or_incomplete' }); }
    if (!diagnosticZip(bytes)) return reply(400, { error: 'invalid_diagnostic_archive' });
    const hash = await digest(bytes);
    const key = `receipt:${id}`, receipt = await this.ctx.storage.get(key);
    if (receipt) return receipt.hash === hash ? reply(200, { reportId: id, delivered: true }) : reply(409, { error: 'report_id_conflict' });
    const now = Date.now();
    if ((await this.ctx.storage.get('cooldown') || 0) > now)
      return reply(429, { error: 'collector_busy' }, Math.ceil(((await this.ctx.storage.get('cooldown')) - now) / 1000));
    const day = Math.floor(now / DAY), budget = await this.ctx.storage.get('budget');
    if (budget?.day === day && budget.count >= 500) return reply(429, { error: 'daily_capacity' }, Math.ceil(((day + 1) * DAY - now) / 1000));
    const form = new FormData();
    form.set('payload_json', JSON.stringify({ content: `DKR-R crash report ${id}\nRedacted logs; a raw dump is present only when separately enabled. Review the archive manifest.`,
      allowed_mentions: { parse: [] }, flags: 4096 }));
    form.set('files[0]', new Blob([bytes], { type: 'application/zip' }), `DKR-R-crash-${id}.zip`);
    let response;
    try {
      response = await fetch(`${webhook}?wait=true`, { method: 'POST', body: form, redirect: 'manual', signal: AbortSignal.timeout(20000) });
    } catch { return reply(503, { error: 'delivery_transport_error' }, 60); }
    if (response.status === 429) {
      const body = await response.json().catch(() => ({}));
      const seconds = Math.min(86400, Math.max(1, Math.ceil(Number(response.headers.get('Retry-After')) || Number(body.retry_after) || 60)));
      await this.ctx.storage.put('cooldown', now + seconds * 1000);
      return reply(429, { error: 'delivery_rate_limited' }, seconds);
    }
    if ([401, 403, 404].includes(response.status)) {
      await this.ctx.storage.put('invalid_secret', secretHash);
      return reply(503, { error: 'collector_needs_attention' }, 3600);
    }
    if (!response.ok) return reply(response.status === 413 ? 413 : 503, { error: 'delivery_failed' }, 60);
    const message = await response.json().catch(() => ({}));
    if (!/^[0-9]+$/.test(message.id || '')) return reply(503, { error: 'delivery_unconfirmed' }, 60);
    await this.ctx.storage.put(key, { hash, created: now });
    await this.ctx.storage.put('budget', { day, count: budget?.day === day ? budget.count + 1 : 1 });
    await this.ctx.storage.setAlarm(now + DAY);
    return reply(200, { reportId: id, delivered: true });
  }
  async alarm() {
    const receipts = await this.ctx.storage.list({ prefix: 'receipt:' });
    for (const [key, receipt] of receipts) if (Date.now() - receipt.created > 31 * DAY) await this.ctx.storage.delete(key);
    if (receipts.size) await this.ctx.storage.setAlarm(Date.now() + DAY);
  }
}
export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (path === '/health' && request.method === 'GET') return reply(200, { service: 'DKR-R crash collector', schema: 1 });
    if (!/^\/v1\/crash-reports\/[a-f0-9]{32}$/.test(path)) return reply(404, { error: 'not_found' });
    if (request.method !== 'POST') return reply(405, { error: 'method_not_allowed' });
    if (!env.CRASH_REPORT_RATE || !env.CRASH_REPORTS || !env.DKR_CRASH_WEBHOOK) return reply(503, { error: 'collector_not_configured' }, 3600);
    // Cloudflare's ephemeral limiter sees the connection IP. It is never put
    // in the diagnostic body, a receipt, or the Discord message.
    const limit = await env.CRASH_REPORT_RATE.limit({ key: request.headers.get('CF-Connecting-IP') || 'unknown' });
    if (!limit.success) return reply(429, { error: 'please_retry_later' }, 60);
    if (request.headers.get('Content-Type') !== 'application/zip') return reply(400, { error: 'invalid_report' });
    // Receive slow connections outside the DO's 30-second critical section.
    // Only a fully bounded body enters serialised receipt/Discord delivery.
    let bytes;
    try { bytes = await boundedBody(request); }
    catch { return reply(413, { error: 'report_too_large_or_incomplete' }); }
    const forwarded = new Request(request.url, { method: 'POST', body: bytes,
      headers: { 'Content-Type': 'application/zip', 'Content-Length': String(bytes.byteLength) } });
    return env.CRASH_REPORTS.get(env.CRASH_REPORTS.idFromName('inbox')).fetch(forwarded);
  },
};
