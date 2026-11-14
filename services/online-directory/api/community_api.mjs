// Website-only features. The launcher catalogue and existing review model stay compatible.
import { __dkrAuth, __dkrCommon, HttpError, finish } from './library_api.mjs';

const { json, readJSON, safeText, requireOrigin, requireRole, validatePublicURL, now, randomToken, sha256 } = __dkrCommon;
export const COMMUNITY_REVISION = '20261029-1';
const PAGE_SIZE = 25;
const REPORT_REASONS = ['Spam or advertising', 'Harassment or abusive language', 'Off-topic or misleading content'];
const id = (value, label = 'Record') => {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{1,96}$/.test(value)) throw new HttpError(400, `${label} is invalid.`, 'invalid_input');
  return value;
};
const operation = value => {
  if (typeof value !== 'string' || !/^[a-f0-9]{32}$/.test(value)) throw new HttpError(400, 'Reload the form before trying again.', 'operation_required');
  return value;
};
const primary = env => env.DB?.withSession ? env.DB.withSession('first-primary') : env.DB;
const query = (db, sql, args = []) => db.prepare(sql).bind(...args);
const rows = async (db, sql, args = []) => (await query(db, sql, args).all()).results || [];
const first = (db, sql, args = []) => query(db, sql, args).first();
const changes = result => Number(result?.meta?.changes || 0);
const iso = stamp => new Date(Number(stamp) * 1000).toISOString();
const uid = prefix => prefix + '_' + randomToken(18);

async function publishedItem(db, itemId) {
  const item = await first(db, 'SELECT id,title FROM library_items WHERE id=? AND archived=0 AND current_release_id IS NOT NULL', [id(itemId)]);
  if (!item) throw new HttpError(404, 'That mod is no longer available.', 'not_found');
  return item;
}
async function commentRow(db, commentId) {
  const row = await first(db, `SELECT c.*,a.display_name,a.status AS account_status,i.title AS item_title,i.archived
    FROM library_comments c JOIN accounts a ON a.id=c.account_id JOIN library_items i ON i.id=c.item_id WHERE c.id=?`, [id(commentId)]);
  if (!row) throw new HttpError(404, 'That comment could not be found.', 'not_found');
  return row;
}
function commentDTO(row, viewer, staff = false) {
  const own = !!viewer && row.account_id === viewer.id;
  return { id: row.id, itemId: row.item_id, displayName: row.account_status === 'deleted' ? 'Deleted player' : row.display_name,
    text: row.body, createdAt: iso(row.created_at), updatedAt: iso(row.updated_at), revision: row.revision, isOwn: own,
    ...(own ? { hidden: !!row.hidden } : {}),
    ...(staff ? { itemTitle: row.item_title, hidden: !!row.hidden, deleted: !!row.deleted, hiddenReason: row.hidden_reason } : {}) };
}
function expected(row, value) {
  if (!Number.isSafeInteger(value) || value < 1 || value !== row.revision)
    throw new HttpError(409, 'This comment changed. Reload it before saving.', 'stale_record');
}
function cursor(url) {
  const encoded = url.searchParams.get('cursor');
  if (!encoded) return null;
  if (encoded.length > 220) throw new HttpError(400, 'Invalid comment cursor.', 'invalid_cursor');
  let value;
  try { value = JSON.parse(atob(encoded.replace(/-/g, '+').replace(/_/g, '/'))); }
  catch (_) { throw new HttpError(400, 'Invalid comment cursor.', 'invalid_cursor'); }
  if (!value || !Number.isSafeInteger(value.at) || value.at < 0) throw new HttpError(400, 'Invalid comment cursor.', 'invalid_cursor');
  id(value.id);
  return value;
}
function nextCursor(result, limit = PAGE_SIZE) {
  if (result.length <= limit) return null;
  const last = result[limit - 1];
  return btoa(JSON.stringify({ at: last.created_at, id: last.id })).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
async function throttle(db, user, bucket, maximum = 5) {
  const stamp = now(), window = Math.floor(stamp / 60) * 60;
  const result = await query(db, `INSERT INTO library_action_limits(account_id,bucket,window_start,count) VALUES(?,?,?,1)
    ON CONFLICT(account_id,bucket) DO UPDATE SET count=CASE WHEN window_start=excluded.window_start THEN count+1 ELSE 1 END,
    window_start=excluded.window_start RETURNING count`, [user.id, bucket, window]).first();
  if (result.count > maximum) throw new HttpError(429, 'Please wait a minute before sending another comment action.', 'rate_limited');
}
function audit(db, user, action, target, reason, mutation) {
  return query(db, `INSERT INTO library_audit(id,actor_id,actor_name,action,target_id,message,created_at)
    SELECT ?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM library_comments WHERE id=? AND last_mutation=?)`,
  [uid('audit'), user.id, user.displayName || user.display_name, action, target, reason, now(), target, mutation]);
}

async function download(request, env, releaseId, ctx) {
  if (!['GET', 'HEAD'].includes(request.method)) throw new HttpError(405, 'Use a download link to get this release.', 'method_not_allowed');
  const db = primary(env);
  const release = await first(db, `SELECT r.id,r.file_url FROM library_releases r JOIN library_items i ON i.id=r.item_id
    WHERE r.id=? AND i.archived=0 AND i.current_release_id IS NOT NULL`, [id(releaseId)]);
  if (!release) throw new HttpError(404, 'That release is no longer available.', 'not_found');
  const target = validatePublicURL(release.file_url, env, 'download');
  const url = new URL(request.url), supplied = url.searchParams.get('attempt');
  if (supplied !== null) operation(supplied);
  const purpose = request.headers.get('Sec-Purpose') || request.headers.get('Purpose') || '';
  const bot = request.cf?.botManagement?.verifiedBot || /bot|crawler|spider|preview/i.test(request.headers.get('User-Agent') || '');
  if (request.method === 'GET' && !/prefetch|prerender/i.test(purpose) && !bot) {
    try {
      const stamp = now(), attempt = supplied || randomToken(24), mutation = randomToken(18);
      const queries = [], ip = request.headers.get('CF-Connecting-IP');
      let visitor = null;
      if (ip) {
        visitor = await sha256('dkr-download:' + Math.floor(stamp / 86400) + ':' + ip);
        queries.push(query(db, `INSERT INTO library_download_limits(visitor,window_start,count) VALUES(?,?,1)
          ON CONFLICT(visitor,window_start) DO UPDATE SET count=count+1`, [visitor, Math.floor(stamp / 60) * 60]));
      }
      queries.push(query(db, `INSERT INTO library_download_attempts(id,release_id,mutation,created_at)
        SELECT ?,?,?,? WHERE ${visitor ? 'EXISTS(SELECT 1 FROM library_download_limits WHERE visitor=? AND window_start=? AND count<=30)' : '1'}
        ON CONFLICT(id) DO NOTHING`, [attempt, release.id, mutation, stamp, ...(visitor ? [visitor, Math.floor(stamp / 60) * 60] : [])]));
      queries.push(query(db, `INSERT INTO library_download_totals(release_id,total,updated_at)
        SELECT ?,1,? WHERE EXISTS(SELECT 1 FROM library_download_attempts WHERE id=? AND release_id=? AND mutation=?)
        ON CONFLICT(release_id) DO UPDATE SET total=total+1,updated_at=excluded.updated_at`, [release.id, stamp, attempt, release.id, mutation]));
      await db.batch(queries);
      // Cleanup is bounded and never delays a file redirect.
      if (ctx?.waitUntil && crypto.getRandomValues(new Uint8Array(1))[0] < 4)
        ctx.waitUntil(cleanupCommunity(env).catch(() => {}));
    } catch (_) { console.warn('Download counter unavailable; serving the validated release.'); }
  }
  return new Response(null, { status: 302, headers: { Location: target, 'Cache-Control': 'no-store' } });
}

async function comments(request, env, itemId) {
  const db = primary(env); await publishedItem(db, itemId);
  if (request.method === 'GET') {
    const viewer = request.headers.has('Authorization') ? await __dkrAuth.requireUser(request, env) : null;
    const after = cursor(new URL(request.url));
    const result = await rows(db, `SELECT c.*,a.display_name,a.status AS account_status FROM library_comments c
      JOIN accounts a ON a.id=c.account_id WHERE c.item_id=? AND c.hidden=0 AND c.deleted=0
      ${after ? 'AND (c.created_at>? OR (c.created_at=? AND c.id>?))' : ''}
      ORDER BY c.created_at,c.id LIMIT ?`, [itemId, ...(after ? [after.at, after.at, after.id] : []), PAGE_SIZE + 1]);
    const count = await first(db, 'SELECT COUNT(*) AS total FROM library_comments WHERE item_id=? AND hidden=0 AND deleted=0', [itemId]);
    return json({ comments: result.slice(0, PAGE_SIZE).map(row => commentDTO(row, viewer)), total: count.total, nextCursor: nextCursor(result) });
  }
  if (request.method !== 'POST') throw new HttpError(405, 'This comment action is not supported.', 'method_not_allowed');
  requireOrigin(request, env);
  const user = await __dkrAuth.requireUser(request, env), body = await readJSON(request, 12000);
  const op = operation(body.operationId), text = safeText(body.text, 2000, 'Comment', true);
  const previous = await first(db, 'SELECT id,item_id,body,deleted FROM library_comments WHERE account_id=? AND operation_id=?', [user.id, op]);
  if (previous) {
    if (previous.item_id !== itemId || previous.body !== text || previous.deleted)
      throw new HttpError(409, 'This comment was already submitted. Reload the discussion before posting again.', 'operation_conflict');
    return json({ comment: commentDTO(await commentRow(db, previous.id), user) });
  }
  await throttle(db, user, 'comment.create');
  const stamp = now(), commentId = uid('comment');
  await query(db, `INSERT INTO library_comments(id,item_id,account_id,operation_id,body,created_at,updated_at,last_mutation)
    VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(account_id,operation_id) DO NOTHING`, [commentId, itemId, user.id, op, text, stamp, stamp, op]).run();
  const saved = await first(db, 'SELECT id,item_id,body,deleted FROM library_comments WHERE account_id=? AND operation_id=?', [user.id, op]);
  if (saved.item_id !== itemId || saved.body !== text || saved.deleted) throw new HttpError(409, 'This submission changed. Reload the discussion.', 'operation_conflict');
  return json({ comment: commentDTO(await commentRow(db, saved.id), user) });
}

async function changeComment(request, env, commentId) {
  if (request.method !== 'POST') throw new HttpError(405, 'This comment action is not supported.', 'method_not_allowed');
  requireOrigin(request, env);
  const user = await __dkrAuth.requireUser(request, env), db = primary(env), body = await readJSON(request, 12000);
  const row = await commentRow(db, commentId);
  if (row.account_id !== user.id) throw new HttpError(403, 'You can only change your own comments.', 'forbidden');
  await publishedItem(db, row.item_id);
  const op = operation(body.operationId);
  if (!['edit', 'delete'].includes(body.action)) throw new HttpError(400, 'Choose a comment action.', 'invalid_input');
  if (row.last_mutation === op) {
    if (body.action === 'edit' && row.body !== safeText(body.text, 2000, 'Comment', true) || body.action === 'delete' && !row.deleted)
      throw new HttpError(409, 'This comment action was already used. Reload the discussion.', 'operation_conflict');
    return json({ comment: commentDTO(row, user), deleted: !!row.deleted });
  }
  if (body.action === 'delete' && row.deleted) return json({ deleted: true });
  if (row.deleted) throw new HttpError(404, 'That comment has been deleted.', 'not_found');
  expected(row, body.expectedRevision);
  const text = body.action === 'edit' ? safeText(body.text, 2000, 'Comment', true) : row.body;
  await throttle(db, user, 'comment.change', 12);
  const result = await query(db, `UPDATE library_comments SET body=?,deleted=?,revision=revision+1,updated_at=?,last_mutation=?
    WHERE id=? AND account_id=? AND revision=? AND deleted=0`, [text, body.action === 'delete' ? 1 : 0, now(), op, row.id, user.id, row.revision]).run();
  if (!changes(result)) throw new HttpError(409, 'This comment changed. Reload it before saving.', 'stale_record');
  return json({ comment: commentDTO(await commentRow(db, row.id), user), deleted: body.action === 'delete' });
}

async function reportComment(request, env, commentId) {
  if (request.method !== 'POST') throw new HttpError(405, 'Use the comment report form.', 'method_not_allowed');
  requireOrigin(request, env);
  const user = await __dkrAuth.requireUser(request, env), db = primary(env), body = await readJSON(request, 4000);
  const row = await commentRow(db, commentId);
  if (row.hidden || row.deleted || row.archived) throw new HttpError(404, 'That comment is no longer visible.', 'not_found');
  if (!REPORT_REASONS.includes(body.reason)) throw new HttpError(400, 'Choose a report reason.', 'invalid_input');
  if (row.account_id === user.id) throw new HttpError(400, 'You can edit or delete your own comment.', 'invalid_input');
  const existing = await first(db, 'SELECT id FROM library_comment_reports WHERE comment_id=? AND reporter_id=?', [row.id, user.id]);
  if (existing) return json({ reported: true });
  await throttle(db, user, 'comment.report', 10);
  await query(db, `INSERT INTO library_comment_reports(id,comment_id,reporter_id,reason,created_at,last_mutation)
    VALUES(?,?,?,?,?,?) ON CONFLICT(comment_id,reporter_id) DO NOTHING`, [uid('comment_report'), row.id, user.id, body.reason, now(), randomToken(18)]).run();
  return json({ reported: true });
}

async function moderation(request, env) {
  if (request.method !== 'GET') throw new HttpError(405, 'This moderation action is not supported.', 'method_not_allowed');
  const user = await __dkrAuth.requireUser(request, env); requireRole(user, 'moderator');
  const db = primary(env), url = new URL(request.url), offset = Number(url.searchParams.get('cursor') || 0);
  if (!Number.isSafeInteger(offset) || offset < 0 || offset > 100000) throw new HttpError(400, 'Invalid moderation cursor.', 'invalid_cursor');
  const reports = await rows(db, `SELECT p.*,a.display_name AS reporter_name FROM library_comment_reports p
    JOIN accounts a ON a.id=p.reporter_id WHERE p.status='open' ORDER BY p.created_at,p.id LIMIT ? OFFSET ?`, [PAGE_SIZE + 1, offset]);
  const hidden = await rows(db, `SELECT c.*,a.display_name,a.status AS account_status,i.title AS item_title,i.archived
    FROM library_comments c JOIN accounts a ON a.id=c.account_id JOIN library_items i ON i.id=c.item_id
    WHERE c.hidden=1 AND c.deleted=0 ORDER BY c.updated_at DESC,c.id LIMIT ? OFFSET ?`, [PAGE_SIZE + 1, offset]);
  const values = [];
  for (const report of reports.slice(0, PAGE_SIZE)) {
    const comment = await commentRow(db, report.comment_id);
    values.push({ id: report.id, commentId: comment.id, reason: report.reason, reporterName: report.reporter_name,
      revision: report.revision, createdAt: iso(report.created_at), comment: commentDTO(comment, user, true) });
  }
  return json({ reports: values, hiddenComments: hidden.slice(0, PAGE_SIZE).map(row => commentDTO(row, user, true)),
    nextCursor: reports.length > PAGE_SIZE || hidden.length > PAGE_SIZE ? String(offset + PAGE_SIZE) : null });
}

async function moderate(request, env, commentId) {
  if (request.method !== 'POST') throw new HttpError(405, 'Use the moderation form.', 'method_not_allowed');
  requireOrigin(request, env);
  const user = await __dkrAuth.requireUser(request, env); requireRole(user, 'moderator');
  const db = primary(env), body = await readJSON(request, 6000), row = await commentRow(db, commentId);
  const op = operation(body.operationId);
  if (!['hide', 'restore', 'keep'].includes(body.action)) throw new HttpError(400, 'Choose a moderation action.', 'invalid_input');
  const reason = safeText(body.reason, 1000, 'Moderation reason', true);
  if (row.last_mutation === user.id + ':' + op) return json({ saved: true });
  if (row.deleted && body.action !== 'keep') throw new HttpError(409, 'The player deleted this comment. Dismiss its report instead.', 'comment_deleted');
  expected(row, body.expectedRevision);
  await throttle(db, user, 'comment.moderate', 30);
  const report = body.reportId ? await first(db, 'SELECT * FROM library_comment_reports WHERE id=? AND comment_id=?', [id(body.reportId), row.id]) : null;
  if (body.reportId && (!report || report.status !== 'open' || report.revision !== body.expectedReportRevision))
    throw new HttpError(409, 'This report changed. Reload the queue.', 'stale_record');
  const mutation = user.id + ':' + op;
  const batch = [query(db, `UPDATE library_comments SET hidden=?,hidden_reason=?,revision=revision+1,last_mutation=?
    WHERE id=? AND revision=?${body.action === 'keep' ? '' : ' AND deleted=0'}`, [body.action === 'keep' ? row.hidden : body.action === 'hide' ? 1 : 0,
    body.action === 'hide' ? reason : body.action === 'keep' ? row.hidden_reason : '', mutation, row.id, row.revision])];
  batch.push(query(db, `UPDATE library_comment_reports SET status='resolved',action=?,resolution=?,resolved_by=?,resolved_at=?,
    revision=revision+1,last_mutation=? WHERE comment_id=? AND status='open'
    AND EXISTS(SELECT 1 FROM library_comments WHERE id=? AND last_mutation=?)`,
  [body.action, reason, user.id, now(), op, row.id, row.id, mutation]));
  batch.push(audit(db, user, 'comment.' + body.action, row.id, reason, mutation));
  const result = await db.batch(batch);
  if (!changes(result[0])) throw new HttpError(409, 'This comment changed. Reload the queue.', 'stale_record');
  return json({ saved: true });
}

export async function handleCommunity(request, env, ctx) {
  const url = new URL(request.url), path = url.pathname;
  const downloadPath = path.match(/^\/v1\/downloads\/([A-Za-z0-9_-]{1,96})$/);
  const commentsPath = path.match(/^\/v1\/mods\/([A-Za-z0-9_-]{1,96})\/comments$/);
  const commentPath = path.match(/^\/v1\/comments\/([A-Za-z0-9_-]{1,96})(?:\/(report|moderate))?$/);
  if (!downloadPath && !commentsPath && !commentPath && path !== '/v1/community/moderation') return null;
  let response;
  try {
    if (url.protocol !== 'https:') throw new HttpError(400, 'Use HTTPS.', 'https_required');
    if (!env.DB) throw new HttpError(503, 'The library is temporarily unavailable.', 'configuration_required');
    if (request.method === 'OPTIONS') { requireOrigin(request, env); response = new Response(null, { status: 204 }); }
    else if (downloadPath) response = await download(request, env, downloadPath[1], ctx);
    else if (commentsPath) response = await comments(request, env, commentsPath[1]);
    else if (commentPath) response = commentPath[2] === 'report' ? await reportComment(request, env, commentPath[1])
      : commentPath[2] === 'moderate' ? await moderate(request, env, commentPath[1]) : await changeComment(request, env, commentPath[1]);
    else response = await moderation(request, env);
  } catch (error) {
    response = json({ error: { code: error instanceof HttpError ? error.code : 'community_unavailable',
      message: error instanceof HttpError ? error.message : 'Comments are temporarily unavailable. Please try again.' } }, error instanceof HttpError ? error.status : 503);
  }
  return finish(response, request, env);
}

export async function communityCatalogue(response, request, env) {
  const path = new URL(request.url).pathname;
  if (!response.ok || request.method !== 'GET' || !['/v1/catalogue', '/v1/catalogue-tools', '/v1/admin', '/health'].includes(path)) return response;
  const data = await response.json(), db = primary(env);
  let ready = true;
  try {
    if (path === '/health') {
      await first(db, 'SELECT release_id FROM library_download_totals LIMIT 0');
      await first(db, 'SELECT id FROM library_download_attempts LIMIT 0');
      await first(db, 'SELECT visitor FROM library_download_limits LIMIT 0');
      await first(db, 'SELECT id FROM library_comments LIMIT 0');
      await first(db, 'SELECT id FROM library_comment_reports LIMIT 0');
    } else if (Array.isArray(data.items) && data.items.length) {
      const ids = data.items.map(item => item.id), slots = ids.map(() => '?').join(',');
      const [downloads, comments] = await db.batch([
        query(db, `SELECT r.item_id,COALESCE(SUM(t.total),0) AS total FROM library_releases r
          LEFT JOIN library_download_totals t ON t.release_id=r.id WHERE r.item_id IN (${slots}) GROUP BY r.item_id`, ids),
        query(db, `SELECT item_id,COUNT(*) AS total FROM library_comments WHERE hidden=0 AND deleted=0
          AND item_id IN (${slots}) GROUP BY item_id`, ids)
      ]);
      const counts = new Map((downloads.results || []).map(row => [row.item_id, row.total]));
      const discussions = new Map((comments.results || []).map(row => [row.item_id, row.total]));
      data.items = data.items.map(item => ({ ...item, downloadCount: counts.get(item.id) || 0, commentCount: discussions.get(item.id) || 0 }));
    }
  } catch (_) {
    ready = false;
    if (Array.isArray(data.items)) data.items = data.items.map(item => ({ ...item, downloadCount: null, commentCount: null }));
  }
  data.community = { revision: COMMUNITY_REVISION, ready, downloadScope: 'website requests since tracking began' };
  return new Response(JSON.stringify(data), { status: response.status, headers: response.headers });
}

export async function cleanupCommunity(env) {
  if (!env.DB) return;
  const stamp = now();
  await env.DB.batch([
    query(env.DB, 'DELETE FROM library_download_attempts WHERE created_at<?', [stamp - 86400]),
    query(env.DB, 'DELETE FROM library_download_limits WHERE window_start<?', [stamp - 7200])
  ]);
}
