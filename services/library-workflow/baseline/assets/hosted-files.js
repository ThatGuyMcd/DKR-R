/* Private existing-file picker client. No file uploads, browser storage, or DOM
 * changes occur here. The snapshot is loaded only after an explicit staff action.
 * DKRFiles.load({force}) -> files; DKRFiles.list({kind,category}) -> cached matches.
 * Getters: loaded, loading, error, inventoryRevision, note. clear() drops cache.
 */
(function (root) {
  'use strict';
  const EMPTY = Object.freeze([]);
  const FOLDERS = Object.freeze({ tracks: 'Tracks', characters: 'Characters', legacy: 'Legacy', magic: 'MagicCodes', textures: 'TexturePacks' });
  const FORMATS = Object.freeze({ download: ['.zip', '.rtz', '.dkrmap', '.xdelta', '.nrm', '.dkrcode', '.bps', '.ips', '.7z'], image: ['.png', '.jpg', '.jpeg', '.webp'] });
  let auth = null, unsubscribe = null, identity = null;
  let files = EMPTY, loaded = false, lastError = '', inventoryRevision = '', note = '';
  let generation = 0, preparing = 0, pending = null;

  function error(message, code, status) {
    const failure = new Error(message);
    if (code) failure.code = code;
    if (status) failure.status = status;
    return failure;
  }
  function accountKey(instance) {
    const user = instance && typeof instance.getUser === 'function' ? instance.getUser() : null;
    return user && typeof user.id === 'string' ? JSON.stringify([user.id, user.role, user.status]) : null;
  }
  function invalidate() {
    generation += 1;
    files = EMPTY; loaded = false; lastError = ''; inventoryRevision = ''; note = '';
    if (pending && pending.controller) pending.controller.abort();
  }
  function syncAccount() {
    const candidate = root.DKRAuth || null;
    if (candidate !== auth) {
      if (unsubscribe) unsubscribe();
      unsubscribe = null; auth = candidate; invalidate(); identity = accountKey(auth);
      if (auth && typeof auth.onChange === 'function') unsubscribe = auth.onChange(syncAccount);
    }
    const current = accountKey(auth);
    if (identity !== current) { invalidate(); identity = current; }
    return auth;
  }
  function stale() { return error('The signed-in account changed. Load the file list again.', 'account_changed'); }
  function apiBase() {
    let url;
    try { url = new URL((root.DKR_CONFIG || {}).apiBase); } catch (_) { /* Fixed error below. */ }
    if (!url || url.protocol !== 'https:' || url.username || url.password || url.search || url.hash) {
      throw error('The website library service address is missing or invalid.', 'configuration_required');
    }
    return url.href.replace(/\/+$/, '');
  }
  function validResponse(data) {
    if (!data || !Array.isArray(data.files) || data.files.length > 1000 ||
        typeof data.inventoryRevision !== 'string' || !data.inventoryRevision || data.inventoryRevision.length > 100 ||
        typeof data.note !== 'string' || data.note.length > 600) throw error('The file inventory service returned an invalid response.', 'invalid_response');
    const ids = new Set(), paths = new Set();
    const result = data.files.map(function (record) {
      if (!record || typeof record.id !== 'string' || !/^ftp_[a-f0-9]{24}$/.test(record.id) ||
          !Object.prototype.hasOwnProperty.call(FOLDERS, record.category) ||
          !Object.prototype.hasOwnProperty.call(FORMATS, record.kind) || record.source !== 'ftp' ||
          typeof record.path !== 'string' || record.path.length > 1000 || /[\\%?#\u0000-\u001f\u007f]/.test(record.path) ||
          typeof record.fileName !== 'string' || !record.fileName || record.fileName.length > 180 ||
          !Number.isSafeInteger(record.size) || record.size < 1) throw error('The file inventory service returned an invalid file record.', 'invalid_response');
      const parts = record.path.split('/');
      const extension = '.' + record.fileName.split('.').pop().toLowerCase();
      let url;
      try { url = new URL(record.url); } catch (_) { /* Fixed error below. */ }
      if (parts.length !== 3 || parts[0] !== (record.kind === 'image' ? 'images' : 'downloads') ||
          parts[1] !== FOLDERS[record.category] || parts[2] !== record.fileName ||
          parts.some(function (part) { return !part || part === '.' || part === '..'; }) ||
          record.format !== extension || !FORMATS[record.kind].includes(extension) ||
          !url || url.protocol !== 'https:' || url.username || url.password || url.search || url.hash ||
          ids.has(record.id) || paths.has(record.path)) throw error('The file inventory service returned an invalid file record.', 'invalid_response');
      ids.add(record.id); paths.add(record.path);
      return Object.freeze({ id: record.id, kind: record.kind, category: record.category,
        path: record.path, fileName: record.fileName, format: record.format, size: record.size,
        url: url.href, source: 'ftp' });
    });
    return Object.freeze(result);
  }

  async function requestInventory(instance, token, epoch) {
    const operation = { controller: new AbortController(), generation: epoch, promise: null };
    pending = operation;
    operation.promise = (async function () {
      const timer = root.setTimeout(function () { operation.controller.abort(); }, 20000);
      try {
        const response = await root.fetch(apiBase() + '/v1/assets', {
          method: 'GET', headers: { Accept: 'application/json', Authorization: 'Bearer ' + token },
          credentials: 'omit', cache: 'no-store', redirect: 'error', signal: operation.controller.signal
        });
        let data;
        try { data = await response.json(); } catch (_) { throw error('The file inventory service returned an invalid response.', 'invalid_response'); }
        syncAccount();
        if (generation !== epoch || auth !== instance) throw stale();
        if (!response.ok) {
          const detail = data && data.error;
          const failure = error(detail && typeof detail.message === 'string' && detail.message.length <= 500
            ? detail.message : 'The file inventory could not be loaded. Please try again.',
          detail && typeof detail.code === 'string' ? detail.code : 'request_failed', response.status);
          // An authoritative denial removes private cached files. Leave account
          // controls and unsaved forms to their owners; this client never clears auth.
          if (response.status === 401 || response.status === 403) { invalidate(); lastError = failure.message; }
          throw failure;
        }
        const checked = validResponse(data);
        files = checked; loaded = true; lastError = '';
        inventoryRevision = data.inventoryRevision; note = data.note;
        return files.slice();
      } catch (failure) {
        if (generation !== epoch || auth !== instance) {
          if (failure && (failure.status === 401 || failure.status === 403)) throw failure;
          throw stale();
        }
        if (failure && failure.name === 'AbortError') throw error('The file inventory took too long to respond. Please try again.', 'request_timeout');
        if (failure && failure.name === 'TypeError' && !failure.code) throw error('The file inventory could not be loaded. Check your connection and try again.', 'network_error');
        throw failure;
      } finally {
        root.clearTimeout(timer);
        if (pending === operation) pending = null;
      }
    }());
    return operation.promise;
  }

  async function load(options) {
    const instance = syncAccount(), startingIdentity = identity, startingGeneration = generation;
    let errorGeneration = generation;
    preparing += 1;
    try {
      if (!instance || typeof instance.getToken !== 'function' || typeof instance.getUser !== 'function') {
        throw error('The Discord sign-in script could not be loaded. Reload this page.', 'auth_unavailable');
      }
      await instance.ready;
      if (typeof instance.ensureSession === 'function') await instance.ensureSession();
      syncAccount();
      if (auth !== instance || startingIdentity !== null && (identity !== startingIdentity || generation !== startingGeneration)) throw stale();
      errorGeneration = generation;
      const user = instance.getUser(), token = instance.getToken();
      if (!user || !token) throw error('Sign in to choose an existing hosted file.', 'SIGN_IN_REQUIRED', 401);
      if (user.status !== 'active' || !['moderator', 'admin'].includes(user.role)) throw error('Moderator access is required to choose hosted files.', 'forbidden', 403);
      if (loaded && !(options && options.force === true)) return files.slice();
      lastError = '';
      if (pending && pending.generation === generation) return await pending.promise;
      return await requestInventory(instance, token, generation);
    } catch (failure) {
      if (generation === errorGeneration && auth === instance) lastError = failure && failure.message || 'The file inventory could not be loaded.';
      throw failure;
    } finally { preparing = Math.max(0, preparing - 1); }
  }

  root.DKRFiles = Object.freeze({
    load: load,
    list: function (filter) {
      syncAccount();
      filter = filter || {};
      return files.filter(function (record) { return (!filter.kind || record.kind === filter.kind) && (!filter.category || record.category === filter.category); });
    },
    clear: function () { syncAccount(); invalidate(); },
    get loaded() { syncAccount(); return loaded; },
    get loading() { return preparing > 0; },
    get error() { syncAccount(); return lastError; },
    get inventoryRevision() { syncAccount(); return inventoryRevision; },
    get note() { syncAccount(); return note; }
  });
  syncAccount();
}(window));
