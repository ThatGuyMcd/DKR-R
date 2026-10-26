// Website workflow extension. FTP transport and native launcher endpoints are unchanged.
const workflowStores = new WeakMap();
async function workflowStorage(env) {
  if (!workflowStores.has(env.DB)) {
    const pending = env.DB.batch([
      env.DB.prepare(`CREATE TABLE IF NOT EXISTS library_workflow_batches (
        id TEXT PRIMARY KEY, owner_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        content_json TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
        created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)`),
      env.DB.prepare('CREATE INDEX IF NOT EXISTS library_workflow_batches_owner ON library_workflow_batches(owner_id,updated_at DESC)')
    ]).catch(error => { workflowStores.delete(env.DB); throw error; });
    workflowStores.set(env.DB, pending);
  }
  await workflowStores.get(env.DB);
}
function workflowKey(value) {
  if (typeof value !== 'string') return '';
  try {
    const u = new URL(value);
    if (u.protocol !== 'https:' || u.username || u.password || u.search || u.hash || /%(?:2f|5c|00|25)/i.test(u.pathname)) return '';
    return u.origin + decodeURIComponent(u.pathname);
  } catch (_) { return ''; }
}
async function workflowUsage(env, files) {
  await workflowStorage(env);
  const db = primary(env), used = new Set();
  // Include every release, archived listing, saved catalogue draft and saved batch.
  // Return only membership flags to staff, never someone else's private draft text.
  // D1 limits compound SELECT terms more strictly than ordinary SQLite.
  // Batch independent reads and inspect saved JSON here instead of joining six
  // SELECTs. Tolerate malformed older drafts without breaking the FTP picker.
  const groups = await db.batch([
    statement(db, 'SELECT file_url AS url FROM library_releases UNION ALL SELECT image AS url FROM library_items'),
    statement(db, "SELECT content_json FROM library_catalogue_drafts WHERE status='draft'"),
    statement(db, 'SELECT content_json FROM library_workflow_batches')
  ]);
  const remember = value => { const key = workflowKey(value); if (key) used.add(key); };
  for (const row of rowsOf(groups[0])) remember(row.url);
  for (const row of rowsOf(groups[1])) {
    const draft = parse(row.content_json, null);
    remember(draft?.fileUrl); remember(draft?.image);
  }
  for (const row of rowsOf(groups[2])) {
    const batch = parse(row.content_json, null);
    for (const entry of Array.isArray(batch?.rows) ? batch.rows : []) {
      remember(entry?.draft?.fileUrl); remember(entry?.draft?.image);
    }
  }
  return files.map(file => ({ ...file, inLibrary: used.has(workflowKey(file.url)) }));
}
function workflowId(value, label = 'Entry identifier') {
  if (typeof value !== 'string' || !/^[a-f0-9]{32}$/.test(value)) fail(label + ' is invalid.');
  return value;
}
async function workflowDraftId(user, operationId) {
  return 'draft_web_' + (await __dkrCommon.sha256(user.id + ':' + workflowId(operationId))).slice(0, 48);
}
async function workflowSaveDraft(db, env, user, input) {
  const operationId = workflowId(input.operationId);
  const id = await workflowDraftId(user, operationId);
  const draft = { ...cleanDraft(input.draft || {}, env, false), operationId };
  const content = JSON.stringify(draft), stamp = now();
  let row = await first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [id]);
  if (!row) {
    await db.batch([
      statement(db, 'INSERT OR IGNORE INTO library_catalogue_drafts(id,owner_id,content_json,created_at,updated_at) VALUES(?,?,?,?,?)', [id, user.id, content, stamp, stamp]),
      auditStatement(db, user, 'Catalogue draft saved', id, draft.title,
        'EXISTS(SELECT 1 FROM library_catalogue_drafts WHERE id=? AND owner_id=? AND created_at=?)', [id, user.id, stamp])
    ]);
    row = await first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [id]);
  }
  if (!row || row.owner_id !== user.id) fail('This entry belongs to another account.', 403, 'forbidden');
  // A lost publication response must resolve to its original listing, even if the
  // browser did not receive the saved revision or published result.
  if (row.status === 'published' || row.content_json === content) return row;
  checkRevision(row, input.expectedRevision);
  const result = await statement(db, `UPDATE library_catalogue_drafts SET content_json=?,revision=revision+1,updated_at=?
    WHERE id=? AND owner_id=? AND revision=? AND status='draft'`, [content, stamp, id, user.id, input.expectedRevision]).run();
  if (!changes(result)) fail('This entry changed. Reload its saved draft before retrying.', 409, 'stale_record');
  return first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [id]);
}
function workflowDescriptor(value) {
  if (value == null) return null;
  object(value);
  const name = safeText(value.name, 180, 'Original filename', true);
  if (/[\\/\u0000-\u001f\u007f]/.test(name) || !Number.isSafeInteger(value.size) || value.size < 1 || value.size > 2147483648) fail('Invalid queued file.');
  const jobId = value.jobId || '';
  if (jobId && !/^up_[a-f0-9]{32}$/.test(jobId)) fail('Invalid upload reference.');
  return { name, size: value.size, jobId, clientId: value.clientId ? workflowId(value.clientId) : '' };
}
function workflowBatchContent(input, env) {
  object(input);
  if (!Array.isArray(input.rows) || input.rows.length > 50) fail('A batch supports up to 50 entries.');
  const ids = new Set();
  const rows = input.rows.map(row => {
    object(row); const id = workflowId(row.id);
    if (ids.has(id)) fail('A batch contains duplicate entry identifiers.');
    ids.add(id);
    const sourceDraft = row.draft || {};
    const draft = {
      ...cleanDraft(sourceDraft, env, false),
      operationId: id,
      ...(sourceDraft.id ? { id: ident(sourceDraft.id) } : {}),
      ...(sourceDraft.revision != null ? { revision: revision(sourceDraft.revision) } : {})
    };
    return { id, draft, image: workflowDescriptor(row.image), download: workflowDescriptor(row.download),
      selected: row.selected !== false, publishedItemId: row.publishedItemId ? ident(row.publishedItemId) : '' };
  });
  const defaults = cleanDraft({ ...(input.defaults || {}), category: input.defaults?.category || 'tracks' }, env, false);
  return { title: safeText(input.title, 100, 'Batch name') || 'New batch', defaults, rows };
}
function workflowBatchDTO(row) {
  return { ...parse(row.content_json, {}), id: row.id, revision: row.revision, updatedAt: iso(row.updated_at) };
}
async function handleWorkflow(request, env) {
  const url = new URL(request.url), path = url.pathname;
  if (!path.startsWith('/v1/workflow/')) return null;
  const user = await requireUser(request, env); requireRole(user, 'moderator');
  await workflowStorage(env);
  const db = primary(env);
  if (path === '/v1/workflow/entry' && request.method === 'POST') {
    const input = object(await readJSON(request, 65536));
    if (input.publish !== undefined && typeof input.publish !== 'boolean') fail('Invalid publication choice.');
    await throttle(db, user, 'workflow.entry');
    let row = await workflowSaveDraft(db, env, user, input);
    let item = null;
    if (input.publish || row.status === 'published') {
      // Recheck authority at publication, then reuse the existing atomic and
      // idempotent publication transaction and uploaded-file integrity gate.
      const current = await requireUser(request, env); requireRole(current, 'moderator');
      item = await publish(db, env, current, 'draft', row, { expectedRevision: row.revision });
      row = await first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [row.id]);
    }
    return json({ draft: draftDTO(row), item });
  }
  if (path === '/v1/workflow/batches' && request.method === 'GET') {
    const page = pagination(url);
    const rows = await all(db, 'SELECT * FROM library_workflow_batches WHERE owner_id=? ORDER BY updated_at DESC,id DESC LIMIT ? OFFSET ?', [user.id, PAGE_SIZE + 1, page.offset]);
    return json({ batches: rows.slice(0, PAGE_SIZE).map(row => ({ id: row.id, title: parse(row.content_json, {}).title || 'Batch', revision: row.revision, updatedAt: iso(row.updated_at) })), nextCursor: nextPage(page.offset, [rows]) });
  }
  const match = /^\/v1\/workflow\/batches\/([a-f0-9]{32})$/.exec(path);
  if (match && ['GET', 'POST'].includes(request.method)) {
    const id = match[1];
    let row = await first(db, 'SELECT * FROM library_workflow_batches WHERE id=? AND owner_id=?', [id, user.id]);
    if (request.method === 'GET') {
      if (!row) fail('That saved batch could not be found.', 404, 'not_found');
      return json({ batch: workflowBatchDTO(row) });
    }
    const input = object(await readJSON(request, 1024 * 1024));
    const content = JSON.stringify(workflowBatchContent(input.batch, env));
    // References never grant file access: validate ownership of every saved job.
    const saved = JSON.parse(content);
    for (const entry of saved.rows) for (const kind of ['image', 'download']) {
      const ref = entry[kind]; if (!ref?.jobId) continue;
      const job = await first(db, 'SELECT owner_id,kind,total_size,original_name FROM library_upload_jobs WHERE id=?', [ref.jobId]);
      if (!job || job.owner_id !== user.id || job.kind !== kind || job.total_size !== ref.size || job.original_name !== ref.name) fail('A saved upload does not belong to this account or file.', 409, 'upload_reference_mismatch');
    }
    const stamp = now();
    if (!row) {
      if (input.expectedRevision != null && input.expectedRevision !== 0) fail('This batch must be reloaded.', 409, 'stale_record');
      await statement(db, 'INSERT OR IGNORE INTO library_workflow_batches(id,owner_id,content_json,created_at,updated_at) VALUES(?,?,?,?,?)', [id, user.id, content, stamp, stamp]).run();
      row = await first(db, 'SELECT * FROM library_workflow_batches WHERE id=? AND owner_id=?', [id, user.id]);
      if (!row) fail('Choose a new batch identifier.', 409, 'stale_record');
    }
    if (row.content_json !== content) {
      checkRevision(row, input.expectedRevision);
      const result = await statement(db, 'UPDATE library_workflow_batches SET content_json=?,revision=revision+1,updated_at=? WHERE id=? AND owner_id=? AND revision=?', [content, stamp, id, user.id, input.expectedRevision]).run();
      if (!changes(result)) fail('This batch changed in another tab. Reload it before saving.', 409, 'stale_record');
      row = await first(db, 'SELECT * FROM library_workflow_batches WHERE id=? AND owner_id=?', [id, user.id]);
    }
    return json({ batch: workflowBatchDTO(row) });
  }
  fail('That workflow action does not exist.', 404, 'not_found');
}
