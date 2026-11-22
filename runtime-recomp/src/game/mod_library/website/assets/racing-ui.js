(function () {
  'use strict';
  const replayModule = new URL('replay-format.js?v=1', document.currentScript?.src || new URL('assets/racing-ui.js', document.baseURI).href);
  const sections = [['vanilla','Vanilla Tracks'],['custom','Custom Tracks'],['full-game','Full Game']];
  const phases = {waiting:'In lobby',testing:'Testing connection',starting:'Starting',loading:'Loading',racing:'Racing',returning:'Returning to lobby'};
  const states = {uploading:'Upload incomplete',queued:'Waiting for verification',verified:'Waiting for moderator review',published:'Published',rejected:'Rejected'};
  const clock = ticks => {
    if (!Number.isSafeInteger(ticks)) return '—';
    const ms = Math.floor(ticks * 1000 / 60);
    return Math.floor(ms / 60000) + ':' + String(Math.floor(ms / 1000) % 60).padStart(2, '0') +
      '.' + String(ms % 1000).padStart(3, '0');
  };

  window.DKRRacingUI = {create({ root, config, Auth, view, user, isStaff, esc, icon, button, pagehead, requireSignedIn, go, renderIcons }) {
    const linkedRoom = /^#lobby=([a-f0-9]{32})$/.exec(location.hash)?.[1];
    let online, directoryError, directoryBusy = false, directoryTimer;
    let catalogue, board, selectedBoard = '', section = 'vanilla', listingTab = 'boards';
    let privateRows = [];
    let leaderboardBusy = false, leaderboardError = '', leaderSequence = 0;
    let file, manifest, postMessage = '', posting = false, fileSequence = 0, postError = false;
    let currentPage = '', onlineNotice = '', privateIdentity = '';

    async function request(path, { fresh = false, method = 'GET', body, privateAccess = false, type = 'application/json', download = false } = {}) {
      const endpoint = new URL(config.apiBase);
      if (endpoint.protocol !== 'https:' || endpoint.username || endpoint.password || endpoint.hash || endpoint.search)
        throw new Error('The library address is invalid.');
      const headers = { Accept: download ? 'application/octet-stream' : 'application/json' };
      if (privateAccess) {
        await Auth.ensureSession();
        const token = Auth.getToken();
        if (!token) throw new Error('Sign in again to continue.');
        headers.Authorization = 'Bearer ' + token;
      }
      if (body !== undefined) headers['Content-Type'] = type;
      const reply = await fetch(new URL('/v1/' + path, endpoint), { method, body, headers, redirect:'error',
        credentials:'omit', cache:fresh ? 'reload' : 'default', signal:AbortSignal.timeout(20000) });
      if (download && reply.ok) return reply.blob();
      const data = await reply.json().catch(() => ({}));
      if (reply.ok) return data;
      const error = new Error(data.error?.message || 'The service is temporarily unavailable.');
      error.code = data.error?.code;
      throw error;
    }

    function refreshRegion(selector, template, focusAction) {
      const region = root.querySelector(selector);
      if (!region) return;
      const focused = region.contains(document.activeElement) ? document.activeElement : null;
      const action = focusAction || focused?.dataset.action;
      const id = focusAction ? null : focused?.dataset.id;
      region.innerHTML = template();
      renderIcons();
      if (action) [...region.querySelectorAll('[data-action]')]
        .find(node => node.dataset.action === action && (id == null || node.dataset.id === id))?.focus();
    }

    function onlinePage() {
      return pagehead('DKR-R Online', 'See who’s online and find a public lobby.') +
        '<div class="dkr-stack" data-online-region>' + onlineContent() + '</div>';
    }
    function onlineContent() {
      const age = online && Date.now() - online.serverTime;
      const live = online && age < 90000 && !directoryError;
      const count = live ? online.online : '—';
      const status = directoryError === 'disabled' ? 'The public lobby browser is currently disabled.' :
        online && !live ? 'Last known rooms. Open DKR-R Online to check availability.' :
        directoryError ? 'The lobby list could not be loaded.' :
        online ? 'Updates every 30 seconds.' : 'Loading the public lobby list…';
      const rooms = online ? [...online.rooms] : [];
      if (linkedRoom) rooms.sort((a, b) => Number(b.id === linkedRoom) - Number(a.id === linkedRoom));
      return '<section class="dkr-panel dkr-row dkr-between"><div><strong class="dkr-racing-count">' + count +
        '</strong> players online<p class="dkr-small dkr-muted" role="status">' + esc(status) + '</p></div>' +
        button('online-refresh',icon('refresh-cw') + 'Refresh','quiet small') + '</section>' +
        '<p class="dkr-small dkr-muted">Copy a link and use Find lobby from website in the launcher’s Public lobbies panel. The host approves requests to race or spectate.</p>' +
        (onlineNotice ? '<p role="status">' + esc(onlineNotice) + '</p>' : '') +
        (rooms.length ? '<div class="dkr-racing-grid">' + rooms.map(room =>
          '<article class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><h2 class="dkr-subtitle">' + esc(room.title) +
          '</h2><span class="dkr-status">' + esc(phases[room.phase] || 'Updating') + '</span></div><span>' +
          esc(room.racers) + ' / ' + esc(room.maxRacers) + ' racers · ' + esc(room.spectators) + ' / ' +
          esc(room.maxSpectators) + ' spectators</span><p class="dkr-small dkr-muted">' +
          (live ? (room.canJoin && room.canSpectate ? 'Racing and spectator spots available' :
            room.canJoin ? 'Racing spots available' : room.canSpectate ? 'Spectator spots available' : 'No places available right now') :
            'Check in the launcher for open spots') + '</p>' +
          (live ? '<div>' + button('online-copy','Copy lobby link','quiet small',room.id) + '</div>' : '') + '</article>').join('') + '</div>' :
          live ? '<div class="dkr-panel"><p>No public lobbies are open right now.</p></div>' : '') +
        (live && online.nextCursor ? button('online-more','More lobbies','quiet') : '');
    }
    async function loadOnline({ more = false, fresh = false } = {}) {
      if (directoryBusy) return;
      directoryBusy = true;
      try {
        const suffix = more && online?.nextCursor ? '?cursor=' + encodeURIComponent(online.nextCursor) : '';
        const data = await request('online/public' + suffix, { fresh });
        if (!Array.isArray(data.rooms) || !Number.isSafeInteger(data.online) || !Number.isSafeInteger(data.serverTime))
          throw new Error('Invalid lobby list.');
        if (more && online) {
          const rooms = new Map(online.rooms.map(room => [room.id, room]));
          data.rooms.forEach(room => rooms.set(room.id, room));
          online = {...data, rooms:[...rooms.values()]};
        } else online = data;
        directoryError = '';
      } catch (error) {
        directoryError = error.code || 'unavailable';
        if (directoryError === 'disabled') online = null;
      } finally {
        directoryBusy = false;
        if (view() === 'online') refreshRegion('[data-online-region]', onlineContent, more ? 'online-refresh' : undefined);
      }
    }

    const postButton = () => button('post-run','Post a run','gold');
    function clearPrivateIfChanged() {
      const account = user();
      const identity = account ? account.id + ':' + account.role : '';
      if (identity === privateIdentity) return;
      privateIdentity = identity;
      if (listingTab !== 'boards') {
        ++leaderSequence;
        listingTab = 'boards';
        leaderboardError = '';
        leaderboardBusy = false;
        privateRows = [];
      }
      ++fileSequence;
      file = manifest = undefined;
      postMessage = '';
      postError = false;
    }
    function speedrunsPage() {
      clearPrivateIfChanged();
      return pagehead('Leaderboards','Race for a personal best and watch how other players set theirs.') +
        '<div class="dkr-stack" data-leaderboard-region>' + speedrunContent() + '</div>';
    }
    function speedrunContent() {
      if (leaderboardError) return '<div class="dkr-panel" role="status">' + esc(leaderboardError) +
        '<div class="dkr-spaced">' + button('run-refresh','Try again','quiet') + '</div></div>';
      if (listingTab !== 'boards') return runListContent();
      const nav = '<nav class="dkr-localnav" aria-label="Leaderboard categories">' + sections.map(([id,label]) =>
        '<button type="button" data-action="run-category" data-id="' + id + '" aria-pressed="' + (section === id) +
        '">' + label + '</button>').join('') + '</nav>';
      const tools = '<div class="dkr-row">' + postButton() +
        (user() ? button('run-list','My runs','quiet','mine') : '') +
        (isStaff() ? button('run-list','Review runs','quiet','review') : '') + '</div>';
      if (!catalogue) return nav + '<div class="dkr-panel" role="status">Loading leaderboards…</div>';
      const matching = catalogue.boards.filter(value => value.kind === section);
      return nav + tools + (board ? rankingContent() : matching.length ? '<div class="dkr-racing-grid">' +
        matching.map(track => '<article class="dkr-panel dkr-stack"><h2 class="dkr-subtitle">' + esc(track.title) +
          '</h2><p class="dkr-muted">' + esc(track.category) + ' · ' + esc(track.vehicle) +
          '</p>' + button('run-board','View times','quiet',track.id) + '</article>').join('') + '</div>' :
        '<section class="dkr-panel"><p>' + (section === 'full-game' ? 'Full-game categories planned for replay qualification include Any%, 100% and All Trophy Races.' :
          'Times will appear here once the first tracks pass replay qualification.') + '</p></section>') +
        (section === 'custom' && !board ? customContent() : '');
    }

    function customContent() {
      return '<section class="dkr-stack"><h2 class="dkr-subtitle">Tracks in the Mod Library</h2><div class="dkr-racing-list">' +
        catalogue.customTracks.map(track => '<article class="dkr-panel dkr-row dkr-between"><div><strong>' +
          esc(track.title) + '</strong><p class="dkr-small dkr-muted">Version ' + esc(track.version) + '</p></div><span class="dkr-status">' +
          (track.qualified ? 'Ranked' : 'Awaiting qualification') + '</span></article>').join('') +
        (catalogue.customTracks.length ? '' : '<p class="dkr-muted">No custom tracks are listed yet.</p>') +
        '</div>' + (catalogue.nextCursor ? button('run-more','More tracks','quiet') : '') + '</section>';
    }
    function rankingContent() {
      return '<section class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><h2 class="dkr-subtitle">' +
        esc(board.board.title) + '</h2>' + button('run-back','All tracks','quiet small') + '</div><p class="dkr-muted">' +
        esc(board.board.category) + ' · ' + esc(board.board.vehicle) + '</p>' +
        (board.entries.length ? '<table class="dkr-run-table"><thead><tr><th>Rank</th><th>Racer</th><th>Time</th><th>Replay</th></tr></thead><tbody>' +
        board.entries.map(entry => '<tr><td>' + esc(entry.place) + '</td><td>' + esc(entry.name) + '</td><td>' +
          clock(entry.ticks) + '</td><td>' + button('run-download','Download','quiet small',entry.id) +
          '</td></tr>').join('') + '</tbody></table>' : '<p>No verified runs have been published yet.</p>') + '</section>';
    }
    function runListContent() {
      return '<div class="dkr-row dkr-between"><h2 class="dkr-subtitle">' + (listingTab === 'review' ? 'Review runs' : 'My runs') +
        '</h2>' + button('run-back','Back to leaderboards','quiet') + '</div>' +
        (leaderboardBusy ? '<p role="status">Loading runs…</p>' : privateRows.length ?
          '<div class="dkr-racing-list">' + privateRows.map(run => '<article class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><h3 class="dkr-subtitle">' +
            esc(run.title) + '</h3><span class="dkr-status">' + esc(states[run.state] || run.state) +
            '</span></div><p>' + esc(run.category) + ' · ' + esc(run.vehicle) + ' · ' + clock(run.ticks ?? run.claimedTicks) +
            (run.ticks == null ? ' (recorded)' : '') + '</p><p class="dkr-muted">' + esc(run.name) + ' · ' +
            esc(run.platform) + ' ' + esc(run.arch) + ' · US ROM ' + esc(run.rom) + '</p>' +
            (run.notes ? '<p>' + esc(run.notes) + '</p>' : '') + (run.reason ? '<p>' + esc(run.reason) + '</p>' : '') +
            '<div class="dkr-row">' + (run.state === 'uploading' ? '' : button('run-download','Download replay','quiet',run.id)) + '</div>' +
            (listingTab === 'review' ? '<div class="dkr-stack"><label>Review note <input data-review-note class="dkr-input" maxlength="600"></label><div class="dkr-row">' +
              button('run-publish','Publish','gold', run.id) + button('run-reject','Reject','red', run.id) + '</div></div>' : '') +
            '</article>').join('') + '</div>' : '<div class="dkr-panel"><p>No runs to show yet.</p></div>');
    }

    async function loadLeaderboards({ more = false, focus } = {}) {
      const seq = ++leaderSequence;
      leaderboardBusy = true;
      leaderboardError = '';
      try {
        if (listingTab !== 'boards') {
          const data = await request('speedruns/' + listingTab, { privateAccess:true });
          if (seq === leaderSequence) privateRows = data.runs;
        } else if (selectedBoard) {
          const data = await request('speedruns/boards/' + encodeURIComponent(selectedBoard));
          if (seq === leaderSequence) board = data;
        } else {
          const data = await request('speedruns' + (more && catalogue?.nextCursor ? '?cursor=' + encodeURIComponent(catalogue.nextCursor) : ''));
          if (seq === leaderSequence) catalogue = more && catalogue ? {...catalogue, nextCursor:data.nextCursor,
            customTracks:[...catalogue.customTracks, ...data.customTracks]} : data;
        }
      } catch (error) {
        if (seq === leaderSequence) leaderboardError = error.message;
      } finally {
        if (seq === leaderSequence) {
          leaderboardBusy = false;
          if (view() === 'speedruns') refreshRegion('[data-leaderboard-region]',speedrunContent,focus);
          if (view() === 'post-run') refreshRegion('[data-post-region]',postContent);
        }
      }
    }

    function postPage() {
      clearPrivateIfChanged();
      return pagehead('Post a run','Attach a replay from DKR-R to send it for verification and review.','speedruns') +
        '<div class="dkr-stack" data-post-region>' + postContent() + '</div>';
    }
    function postContent() {
      const qualified = catalogue?.boards?.find(entry => entry.id === manifest?.board);
      return '<section class="dkr-panel dkr-stack">' +
        (!user() ? '<p>Sign in with the same library account you use to post mods and comments.</p>' +
          button('run-sign-in',icon('user-round') + 'Sign in','gold') :
          '<label><strong>' + (file ? 'Replace replay file' : 'Replay file') + '</strong><input class="dkr-input dkr-racing-file" type="file" accept=".dkrreplay" data-run-file></label>' +
          (manifest ? '<div class="dkr-panel"><p><strong>' + esc(qualified?.title || manifest.board) + '</strong></p><p class="dkr-small dkr-muted">' +
            esc(file.name) + '</p><p>' +
            esc(qualified?.category || 'Unknown category') + ' · ' + clock(manifest.clockTicks) +
            ' recorded · US ROM ' + esc(manifest.rom) + ' · ' + esc(manifest.platform) + '</p></div>' : '') +
          (!catalogue?.canSubmit ? '<p class="dkr-muted">Posting opens once replay verification is ready.</p>' : '') +
          '<div>' + (catalogue?.canSubmit && qualified && !posting && file ? button('run-upload','Post run','gold') : '') + '</div>') +
        '<p role="' + (postError ? 'alert' : 'status') + '">' + esc(postMessage) + '</p></section>';
    }
    root.addEventListener('change', async event => {
      if (!event.target.matches('[data-run-file]')) return;
      const sequence = ++fileSequence;
      const next = event.target.files[0];
      file = manifest = undefined;
      postMessage = '';
      postError = false;
      if (next) {
        try {
          const { MAX_REPLAY_BYTES, parseReplay } = await import(replayModule.href);
          if (next.size > MAX_REPLAY_BYTES) throw new Error();
          const parsed = parseReplay(new Uint8Array(await next.arrayBuffer()));
          if (sequence !== fileSequence || view() !== 'post-run') return;
          file = next;
          manifest = parsed;
        } catch {
          if (sequence !== fileSequence || view() !== 'post-run') return;
          postMessage = 'Choose a valid .dkrreplay file under 1 MiB.';
          postError = true;
        }
      }
      refreshRegion('[data-post-region]',postContent, file && catalogue?.canSubmit ? 'run-upload' : undefined);
    });

    async function action(action, id, target) {
      if (action === 'online' || action === 'speedruns' || action === 'post-run') { await go(action); return true; }
      if (action === 'online-refresh' || action === 'online-more') {
        await loadOnline({ fresh:action === 'online-refresh', more:action === 'online-more' }); return true;
      }
      if (action === 'online-copy') {
        if (!/^[a-f0-9]{32}$/.test(id)) return true;
        const url = 'https://positron-portal.com/dkr-r/#lobby=' + id;
        try {
          await navigator.clipboard.writeText(url);
          onlineNotice = 'Link copied. In DKR-R Online, choose Find lobby from website.';
        } catch {
          onlineNotice = 'Copy this link into the clipboard to find the lobby in DKR-R Online. ' + url;
        }
        refreshRegion('[data-online-region]',onlineContent);
        return true;
      }
      if (action === 'run-category' || action === 'run-back') {
        if (action === 'run-category') section = id;
        ++leaderSequence;
        selectedBoard = ''; board = undefined; listingTab = 'boards'; privateRows = []; leaderboardError = '';
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        if (!catalogue) await loadLeaderboards();
        return true;
      }
      if (action === 'run-board' || action === 'run-list' || action === 'run-more' || action === 'run-refresh') {
        if (action === 'run-board') selectedBoard = id;
        if (action === 'run-list') {
          if (!requireSignedIn({view:'speedruns'},id === 'review')) return true;
          listingTab = id; privateRows = [];
        }
        await loadLeaderboards({more:action === 'run-more', focus:action === 'run-board' ? 'run-back' : undefined});
        return true;
      }
      if (action === 'run-sign-in') { requireSignedIn({view:'post-run'}); return true; }
      if (action === 'run-upload') {
        if (posting || !file || !user()) return true;
        const owner = user().id;
        posting = true;
        postError = false;
        postMessage = 'Posting your run…';
        refreshRegion('[data-post-region]',postContent);
        try {
          const data = await request('speedruns/runs', { method:'POST', privateAccess:true, type:'application/octet-stream',body:file });
          if (view() === 'post-run' && user()?.id === owner) {
            file = manifest = undefined;
            postMessage = 'Run saved. ' + (states[data.run.state] || 'Check My runs for its status') + '.';
          }
        } catch (error) {
          if (view() === 'post-run' && user()?.id === owner) { postError = true; postMessage = error.message; }
        } finally {
          posting = false;
          refreshRegion('[data-post-region]',postContent);
        }
        return true;
      }
      if (action === 'run-download') {
        const blob = await request('speedruns/runs/' + encodeURIComponent(id) + '/replay',
          { download:true, privateAccess:listingTab !== 'boards' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url; link.download = 'DKR-R-' + id + '.dkrreplay';
        link.click();
        setTimeout(() => URL.revokeObjectURL(url),1000);
        return true;
      }
      if (action === 'run-publish' || action === 'run-reject') {
        const reason = target.closest('article').querySelector('[data-review-note]').value;
        target.disabled = true;
        try {
          await request('speedruns/runs/' + encodeURIComponent(id) + '/review', { method:'POST', privateAccess:true,
            body:JSON.stringify({decision:action === 'run-publish' ? 'published' : 'rejected',reason}) });
          await loadLeaderboards();
        } finally { target.disabled = false; }
        return true;
      }
      return false;
    }

    function mounted() {
      const next = view();
      if (!root.querySelector('[data-online-region],[data-leaderboard-region],[data-post-region]') &&
          ['online','speedruns','post-run'].includes(next)) return;
      if (next === currentPage) return;
      currentPage = next;
      clearInterval(directoryTimer);
      if (next !== 'post-run') { ++fileSequence; file = manifest = undefined; postMessage = ''; postError = false; }
      if (next !== 'speedruns') { ++leaderSequence; listingTab = 'boards'; selectedBoard = ''; board = undefined; privateRows = []; leaderboardError = ''; }
      if (next === 'online' && root.querySelector('[data-online-region]')) {
        loadOnline();
        directoryTimer = setInterval(() => { if (!document.hidden) loadOnline(); },30000);
      }
      if ((next === 'speedruns' || next === 'post-run') && !catalogue) loadLeaderboards();
    }
    return { onlinePage, speedrunsPage, postPage, action, mounted };
  }};
}());
