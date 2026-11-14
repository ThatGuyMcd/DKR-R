/* Public catalogue client. All authority and persistence live in the private API.
 * Tokens are obtained from DKRAuth and never written to browser storage. */
(function (root) {
  'use strict';
  root.createLibraryAPI = function createLibraryAPI() {
    const auth = root.DKRAuth;
    const listeners = new Set();
    let generation = 0, refreshSequence = 0, loadingCount = 0;
    let ownReviews = [], staffReviews = [], publicReviews = new Map();
    let self = null, disposed = false;
    let identity = auth && auth.getUser ? auth.getUser() : null;
    const D = { items: [], managedItems: [], requests: [], reviews: [], reports: [], audit: [], catalogueDrafts: [],
      accounts: [], userId: null, error: '', configured: false, loading: false, reviewCursors: {}, ready: null };
    let apiBase = '';
    try {
      const configured = new URL((root.DKR_CONFIG || {}).apiBase);
      if (configured.protocol !== 'https:' || configured.username || configured.password || configured.search || configured.hash) throw new Error();
      apiBase = configured.href.replace(/\/+$/, ''); D.configured = true;
    } catch (_) { D.error = 'The website library service address is missing or invalid. Reload this page or contact the site owner.'; }
    function notify() { listeners.forEach(function (callback) { try { callback(D); } catch (_) { /* UI failures do not affect store state. */ } }); }
    function loading(change) { loadingCount = Math.max(0, loadingCount + change); D.loading = loadingCount > 0; notify(); }
    function unique(records) { const map = new Map(); records.forEach(function (record) { map.set(record.id, record); }); return Array.from(map.values()); }
    function rebuildReviews() {
      const visible = []; publicReviews.forEach(function (rows) { visible.push.apply(visible, rows); });
      D.reviews = unique(visible.concat(ownReviews, staffReviews));
    }
    function clearPrivate() {
      self = null; D.userId = null; D.accounts = []; D.managedItems = [];
      D.requests = []; D.reports = []; D.audit = []; D.catalogueDrafts = [];
      ownReviews = []; staffReviews = []; rebuildReviews();
    }
    function invalidated(epoch) { return disposed || epoch !== generation; }
    function staleError() { const error = new Error('The signed-in account changed. Please try again.'); error.code = 'account_changed'; return error; }
    function cachedRevision(value, fallback) { return value === undefined ? fallback : value; }
    async function ensureAuth() {
      try { if (auth && auth.ensureSession) await auth.ensureSession(); }
      catch (error) { D.error = error.message; notify(); throw error; }
    }
    async function requestJSON(path, token, body, epoch) {
      if (!D.configured) throw new Error('The library service has not been configured yet.');
      const controller = new AbortController();
      const timeout = root.setTimeout(function () { controller.abort(); }, 20000);
      const headers = { Accept: 'application/json' };
      if (token) headers.Authorization = 'Bearer ' + token;
      if (body !== undefined) headers['Content-Type'] = 'application/json';
      try {
        const response = await fetch(apiBase + path, { method: body === undefined ? 'GET' : 'POST', headers,
          body: body === undefined ? undefined : JSON.stringify(body), credentials: 'omit', cache: 'no-store',
          redirect: 'error', signal: controller.signal });
        let data; try { data = await response.json(); } catch (_) { throw new Error('The library service returned an invalid response.'); }
        if (invalidated(epoch)) throw staleError();
        if (!response.ok) {
          const detail = data && data.error;
          const error = new Error(detail && detail.message || data.message || 'The library request could not be completed.');
          error.status = response.status; error.code = detail && detail.code || '';
          if ((response.status === 401 || response.status === 403) && token && auth && auth.getToken() === token) {
            // Renew the session for the next deliberate attempt. This request,
            // especially a write, is never automatically sent a second time.
            if (auth.ensureSession) {
              try { await auth.ensureSession({force: true}); } catch (_) { /* Keep the actual request failure. */ }
            } else if (response.status === 401) auth.clear();
          }
          throw error;
        }
        return data;
      } catch (error) {
        if (error && error.name === 'AbortError') throw new Error('The library service took too long to respond. Please try again.');
        throw error;
      } finally { root.clearTimeout(timeout); }
    }
    async function paged(path, fields, token, epoch) {
      const result = {}; fields.forEach(function (field) { result[field] = []; });
      let cursor = null, pages = 0;
      do {
        const page = await requestJSON(path + (cursor ? '?cursor=' + encodeURIComponent(cursor) : ''), token, undefined, epoch);
        fields.forEach(function (field) { if (Array.isArray(page[field])) result[field].push.apply(result[field], page[field]); });
        if (page.user) result.user = page.user;
        cursor = page.nextCursor || null;
        pages += 1;
        if (cursor && pages >= 1000) throw new Error('The catalogue is too large to load in one session. Contact the site administrator.');
      } while (cursor);
      fields.forEach(function (field) { result[field] = unique(result[field]); });
      return result;
    }
    D.user = function () { return self; };
    D.isStaff = function () { return !!self && ['moderator', 'admin'].includes(self.role); };
    D.isAdmin = function () { return !!self && self.role === 'admin'; };
    D.item = function (id) { return D.items.find(function (item) { return item.id === id; }) || (D.isStaff() ? D.managedItems.find(function (item) { return item.id === id; }) : null) || null; };
    D.request = function (id) { return D.requests.find(function (request) { return request.id === id; }) || null; };
    D.rating = function (id) { const item = D.item(id); return item && item.rating || { average: 0, count: 0, distribution: [0, 0, 0, 0, 0] }; };
    D.visibleReviews = function (id) { return (publicReviews.get(id) || []).slice(); };
    D.ownReview = function (id) { return self ? ownReviews.find(function (review) { return review.itemId === id; }) || null : null; };
    D.onChange = function (callback) { if (typeof callback !== 'function') throw new TypeError('Expected a listener.'); listeners.add(callback); return function () { listeners.delete(callback); }; };
    D.refresh = async function () {
      // Start loading synchronously: a login listener can immediately wait for
      // this refresh, even while its session-validation promise is pending.
      const epoch = generation, run = ++refreshSequence;
      loading(1); D.error = '';
      try {
        await ensureAuth();
        if (invalidated(epoch) || run !== refreshSequence) return D;
        const token = auth && auth.getToken();
        if (!D.configured) { notify(); return D; }
        const catalogue = await paged('/v1/catalogue', ['items'], null, epoch);
        if (invalidated(epoch) || run !== refreshSequence) return D;
        D.items = catalogue.items;
        let personal = null, staff = null, admin = null;
        if (token) {
          personal = await paged('/v1/me', ['requests', 'reviews'], token, epoch);
          if (personal.user && ['moderator', 'admin'].includes(personal.user.role)) staff = await paged('/v1/staff', ['requests', 'reports', 'reviews'], token, epoch);
          if (personal.user && ['moderator','admin'].includes(personal.user.role)) admin = await paged(personal.user.role === 'admin' ? '/v1/admin' : '/v1/catalogue-tools', ['accounts', 'drafts', 'audit', 'items'], token, epoch);
        }
        if (invalidated(epoch) || run !== refreshSequence) return D;
        clearPrivate();
        if (personal && personal.user) {
          self = personal.user; D.userId = self.id; D.accounts = [self];
          D.requests = personal.requests; ownReviews = personal.reviews;
          if (staff) { D.requests = staff.requests; D.reports = staff.reports; staffReviews = staff.reviews; }
          if (admin) { D.accounts = admin.accounts; D.catalogueDrafts = admin.drafts; D.audit = admin.audit; D.managedItems = admin.items; }
        }
        rebuildReviews();
        // An archived mod must not leave an earlier public review page displayed.
        for (const id of publicReviews.keys()) if (!D.items.some(function (item) { return item.id === id; })) { publicReviews.delete(id); delete D.reviewCursors[id]; }
        rebuildReviews();
        return D;
      } catch (error) {
        if (!invalidated(epoch) && run === refreshSequence) D.error = error.message;
        throw error;
      } finally { loading(-1); }
    };
    D.loadReviews = async function (itemId, cursor) {
      const epoch = generation; loading(1);
      try {
        const page = await requestJSON('/v1/mods/' + encodeURIComponent(itemId) + '/reviews' + (cursor ? '?cursor=' + encodeURIComponent(cursor) : ''), null, undefined, epoch);
        if (invalidated(epoch)) throw staleError();
        const rows = cursor ? (publicReviews.get(itemId) || []).concat(page.reviews) : page.reviews;
        publicReviews.set(itemId, unique(rows)); D.reviewCursors[itemId] = page.nextCursor || null;
        [D.items, D.managedItems].forEach(function (items) { const item = items.find(function (item) { return item.id === itemId; }); if (item) item.rating = page.rating; });
        rebuildReviews(); D.error = '';
        return page;
      } catch (error) { if (!invalidated(epoch)) D.error = error.message; throw error; }
      finally { loading(-1); }
    };
    async function mutate(action, input, reviewItemId) {
      const epoch = generation;
      const expectedUser = auth && auth.getUser();
      await ensureAuth();
      const currentUser = auth && auth.getUser(), token = auth && auth.getToken();
      // Another tab can replace a browser cookie with a different account.
      // An old form must never be submitted under that newly restored identity.
      if (invalidated(epoch) || !expectedUser || !currentUser || currentUser.id !== expectedUser.id) {
        if (!expectedUser && !currentUser) throw new Error('Please sign in to continue.');
        throw staleError();
      }
      if (!token) throw new Error('Please sign in to continue.');
      loading(1);
      try {
        const data = await requestJSON('/v1/actions/' + action, token, { input: input || {} }, epoch);
        if (invalidated(epoch)) throw staleError();
        // Never replay a write. A failed refresh does not undo a committed action.
        try {
          await D.refresh();
          const loadedIds = Array.from(publicReviews.keys());
          if (reviewItemId && !loadedIds.includes(reviewItemId)) loadedIds.push(reviewItemId);
          for (const id of loadedIds) if (D.items.some(function (item) { return item.id === id; })) await D.loadReviews(id);
        } catch (error) { if (!invalidated(epoch)) D.error = 'Your change was saved, but the latest view could not be reloaded. ' + error.message; }
        if (invalidated(epoch)) throw staleError();
        return data.result;
      } catch (error) { if (!invalidated(epoch)) D.error = error.message; throw error; }
      finally { loading(-1); }
    }
    function requestMutation(action, id, data) {
      const input = Object.assign({}, data || {}, { id: id });
      input.expectedRevision = cachedRevision(input.expectedRevision, (D.request(id) || {}).revision);
      return mutate(action, input);
    }
    D.saveReview = function (data) { return mutate('review.save', data, data.itemId); };
    D.reportReview = function (data) { return mutate('review.report', data); };
    D.resolveReport = function (data) {
      const report = D.reports.find(function (report) { return report.id === data.reportId; });
      const review = report && D.reviews.find(function (review) { return review.id === report.reviewId; });
      return mutate('report.resolve', Object.assign({}, data, { expectedRevision: cachedRevision(data.expectedRevision, report && report.revision),
        expectedReviewRevision: cachedRevision(data.expectedReviewRevision, review && review.revision) }), report && report.itemId);
    };
    D.submitRequest = function (data) { return mutate('request.submit', data); };
    D.resubmitRequest = function (id, data) { return requestMutation('request.resubmit', id, data); };
    D.startReview = function (id, expectedRevision) {
      return requestMutation('request.start', id, typeof expectedRevision === 'object' ? expectedRevision : { expectedRevision: expectedRevision });
    };
    D.addStaffNote = function (id, value, expectedRevision) { return requestMutation('request.note', id, { value: value, expectedRevision: expectedRevision }); };
    D.decideRequest = function (id, data) { return requestMutation('request.decide', id, data); };
    D.publishRequest = function (id, draft) { return requestMutation('request.publish', id, { draft: draft, expectedRevision: draft.expectedRevision }); };
    D.saveCatalogueDraft = function (draft) {
      const saved = D.catalogueDrafts.find(function (record) { return record.id === draft.id; });
      return mutate('catalogue.save', { id: draft.id, draft: draft,
        expectedRevision: cachedRevision(draft.expectedRevision, cachedRevision(draft.revision, saved && saved.revision)) });
    };
    D.publishCatalogueDraft = function (id, expectedRevision) {
      const draft = D.catalogueDrafts.find(function (record) { return record.id === id; });
      return mutate('catalogue.publish', { id: id, expectedRevision: cachedRevision(expectedRevision, draft && draft.revision) });
    };
    D.archiveCatalogueItem = function (id, archived, expectedRevision) {
      return mutate('catalogue.archive', { id: id, archived: archived === undefined ? true : archived,
        expectedRevision: cachedRevision(expectedRevision, (D.item(id) || {}).revision) });
    };
    D.updateCatalogueItem = function (id, data) {
      const input = Object.assign({}, data || {}, { id: id });
      input.expectedRevision = cachedRevision(input.expectedRevision, (D.item(id) || {}).revision);
      return mutate('catalogue.update', input);
    };
    D.setStaffRole = async function (accountId, role) {
      const result = await mutate('admin.role', { accountId: accountId, role: role });
      if (auth && auth.getUser() && auth.getUser().id === accountId && auth.refreshUser) {
        try { await auth.refreshUser(); } catch (_) { /* The saved role is already reflected by /v1/me. */ }
      }
      return result;
    };
    D.updateProfile = async function (data) {
      const result = await mutate('account.profile', data);
      if (auth && auth.refreshUser) { try { await auth.refreshUser(); } catch (_) { /* Profile save succeeded; do not repeat it. */ } }
      return result;
    };
    D.deleteAccount = async function (data) {
      const epoch = generation, expectedUser = auth && auth.getUser();
      await ensureAuth();
      const currentUser = auth && auth.getUser(), token = auth && auth.getToken();
      if (invalidated(epoch) || !expectedUser || !currentUser || expectedUser.id !== currentUser.id) throw staleError();
      if (!token) throw new Error('Please sign in to continue.');
      const result = await requestJSON('/v1/actions/account.delete', token, { input: data }, epoch);
      if (auth && auth.getToken() === token) auth.clear();
      return result.result;
    };
    const unsubscribe = auth && auth.onChange ? auth.onChange(function (user) {
      const next = user || null;
      const changed = (!identity !== !next) || (identity && next &&
        (identity.id !== next.id || identity.role !== next.role || identity.status !== next.status));
      identity = next;
      if (!changed) {
        // Access-token renewal, cookie diagnostics and alias updates preserve
        // the current private cache. Authority/identity changes still clear it.
        if (self && next && self.id === next.id) {
          self = Object.assign({}, self, next);
          D.accounts = D.accounts.map(function (account) { return account.id === next.id ? Object.assign({}, account, next) : account; });
        }
        notify();
        return;
      }
      generation += 1; refreshSequence += 1;
      clearPrivate(); D.error = '';
      // Clearing happens synchronously, before any request for the next identity.
      notify();
      D.refresh().catch(function () { /* D.error already describes the failure. */ });
    }) : function () {};
    D.dispose = function () { disposed = true; generation += 1; clearPrivate(); publicReviews.clear(); D.reviews = []; unsubscribe(); listeners.clear(); };
    D.ready = Promise.resolve(auth && auth.ready).then(function () { return D.refresh(); }).catch(function (error) { D.error = error.message; notify(); return D; });
    return D;
  };
}(window));
