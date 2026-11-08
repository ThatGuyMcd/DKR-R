/* Community data extends the existing catalogue client. Credentials remain in DKRAuth. */
(function (root) {
  'use strict';
  const create = root.createLibraryAPI;
  root.createLibraryAPI = function () {
    const D = create.apply(this, arguments), auth = root.DKRAuth;
    const comments = new Map(), commentLoads = new Map();
    let generation = 0, disposed = false, account = '', authority = '';
    let apiBase = '';
    try {
      const url = new URL((root.DKR_CONFIG || {}).apiBase);
      if (url.protocol === 'https:' && !url.username && !url.password && !url.search && !url.hash) apiBase = url.href.replace(/\/+$/, '');
    } catch (_) { /* The catalogue client reports invalid configuration. */ }
    D.commentReports = []; D.hiddenComments = []; D.commentModerationError = '';
    function sync() {
      const user = auth && auth.getUser(), next = user ? String(user.id) : '';
      const nextAuthority = user ? JSON.stringify([user.id, user.role, user.status]) : '';
      if (next !== account || nextAuthority !== authority) {
        generation++; account = next; authority = nextAuthority; comments.clear(); commentLoads.clear();
        D.commentReports = []; D.hiddenComments = []; D.commentModerationError = '';
      }
    }
    function stale(epoch) { sync(); return disposed || epoch !== generation; }
    const changed = () => Object.assign(new Error('Your signed-in account changed. Please try again.'), { code: 'account_changed' });
    const unique = values => [...new Map(values.map(value => [value.id, value])).values()];
    async function request(path, body, epoch) {
      if (!apiBase) throw new Error('The library service is unavailable. Reload the page and try again.');
      const controller = new AbortController(), timeout = root.setTimeout(() => controller.abort(), 20000);
      const token = auth && auth.getToken();
      try {
        const response = await root.fetch(apiBase + path, { method: body === undefined ? 'GET' : 'POST',
          headers: { Accept: 'application/json', ...(token ? { Authorization: 'Bearer ' + token } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
          ...(body === undefined ? {} : { body: JSON.stringify(body) }), credentials: 'omit', redirect: 'error', cache: 'no-store', signal: controller.signal });
        let data;
        try { data = await response.json(); }
        catch (_) { throw new Error('The library returned an invalid response. Your comment draft is still here.'); }
        if (stale(epoch)) throw changed();
        if (!response.ok) {
          const error = new Error(data.error && data.error.message || 'The comment action could not be completed.');
          error.code = data.error && data.error.code; error.status = response.status; throw error;
        }
        return data;
      } catch (error) {
        if (error.name === 'AbortError') throw new Error('The library took too long to respond. Your comment draft is still here.');
        throw error;
      } finally { root.clearTimeout(timeout); }
    }
    D.commentsFor = function (itemId) { sync(); return comments.get(itemId) || null; };
    D.loadComments = async function (itemId, cursor) {
      sync(); const epoch = generation;
      const run = (commentLoads.get(itemId) || 0) + 1; commentLoads.set(itemId, run);
      const data = await request('/v1/mods/' + encodeURIComponent(itemId) + '/comments' + (cursor ? '?cursor=' + encodeURIComponent(cursor) : ''), undefined, epoch);
      if (stale(epoch)) throw changed();
      if (commentLoads.get(itemId) !== run) return data;
      const previous = cursor && comments.get(itemId), values = unique((previous ? previous.comments : []).concat(data.comments));
      values.sort((a, b) => a.createdAt.localeCompare(b.createdAt) || a.id.localeCompare(b.id));
      comments.set(itemId, { comments: values, total: data.total, nextCursor: data.nextCursor || null });
      for (const collection of [D.items, D.managedItems]) {
        const item = collection.find(value => value.id === itemId); if (item) item.commentCount = data.total;
      }
      return data;
    };
    D.loadCommentModeration = async function () {
      sync(); const epoch = generation;
      if (!D.isStaff()) { D.commentReports = []; D.hiddenComments = []; return; }
      const reports = [], hidden = []; let cursor = null, pages = 0;
      do {
        const data = await request('/v1/community/moderation' + (cursor ? '?cursor=' + encodeURIComponent(cursor) : ''), undefined, epoch);
        reports.push(...data.reports); hidden.push(...data.hiddenComments); cursor = data.nextCursor;
        if (++pages >= 1000 && cursor) throw new Error('There are too many reports to load. Contact the site administrator.');
      } while (cursor);
      if (stale(epoch)) throw changed();
      D.commentReports = unique(reports); D.hiddenComments = unique(hidden); D.commentModerationError = '';
    };
    async function write(path, body, itemId, refreshModeration) {
      sync(); const epoch = generation, expected = account;
      if (auth && auth.ensureSession) await auth.ensureSession();
      if (stale(epoch) || account !== expected) throw changed();
      if (!expected || !auth.getToken() || !D.user() || String(D.user().id) !== expected) throw new Error('Sign in before sending a comment action.');
      const result = await request(path, body, epoch);
      // Committed writes are never replayed because a subsequent read failed.
      const reads = await Promise.allSettled([
        ...(itemId ? [D.loadComments(itemId)] : []),
        ...(refreshModeration && D.isStaff() ? [D.loadCommentModeration()] : [])
      ]);
      const failed = reads.find(read => read.status === 'rejected');
      if (failed) result.refreshError = 'Your change was saved, but the latest comments could not be reloaded. ' + failed.reason.message;
      if (stale(epoch)) throw changed();
      if (itemId && result.comment && !result.deleted && !result.comment.hidden) {
        const page = comments.get(itemId);
        if (page) {
          page.comments = unique(page.comments.concat(result.comment));
          page.comments.sort((a, b) => a.createdAt.localeCompare(b.createdAt) || a.id.localeCompare(b.id));
        }
      }
      return result;
    }
    D.saveComment = (itemId, body) => write('/v1/mods/' + encodeURIComponent(itemId) + '/comments', body, itemId);
    D.changeComment = (commentId, itemId, body) => write('/v1/comments/' + encodeURIComponent(commentId), body, itemId);
    D.reportComment = (commentId, body) => write('/v1/comments/' + encodeURIComponent(commentId) + '/report', body);
    D.moderateComment = (commentId, itemId, body) => write('/v1/comments/' + encodeURIComponent(commentId) + '/moderate', body, itemId, true);
    D.downloadURL = function (releaseId) {
      return apiBase && /^[A-Za-z0-9_-]{1,96}$/.test(releaseId || '') ? apiBase + '/v1/downloads/' + encodeURIComponent(releaseId)
        + '?attempt=' + Array.from(root.crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('') : '';
    };
    const refresh = D.refresh;
    D.refresh = async function () {
      const result = await refresh(); sync();
      if (D.isStaff()) {
        try { await D.loadCommentModeration(); }
        catch (error) { D.commentModerationError = error.message; }
      }
      return result;
    };
    const unsubscribe = auth && auth.onChange ? auth.onChange(sync) : () => {};
    const dispose = D.dispose;
    D.dispose = function () { disposed = true; generation++; comments.clear(); commentLoads.clear(); unsubscribe(); dispose(); };
    sync();
    return D;
  };
}(window));
