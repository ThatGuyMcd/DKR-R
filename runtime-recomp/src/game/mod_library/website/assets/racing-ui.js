(function () {
  'use strict';
  const activityPeriods = [['7','7 days'],['30','30 days'],['all','All time']];
  const sections = [['overall','Top Players'],['vanilla','Vanilla Tracks'],['custom','Custom Tracks'],['full-game','Full Game']];
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
    let activityPeriod = 'all';
    let online, directoryError, directoryBusy = false, directoryTimer;
    let catalogue, ranking, overall, profile, section = 'overall', listingTab = 'boards', runId = '';
    let playerId = '', playerPanel = 'score', profileError = '';
    let fromProfile = false, explainPoints = false, selectedRecord = null;
    let privateRuns, quotaTimer, score = 'race';
    const quotaDate = new Intl.DateTimeFormat(undefined, { dateStyle:'medium', timeStyle:'medium' });
    let query = '', world = '', vehicle = '', availability = '', course = '', courseVehicle = '', racer = '';
    const vehicleNames = {car:'Car',hovercraft:'Hovercraft',plane:'Plane',mixed:'Mixed'};
    const matches = value => value.toLocaleLowerCase().includes(query.trim().toLocaleLowerCase());
    let leaderboardBusy = false, leaderboardError = '', leaderSequence = 0;
    let currentPage = '', onlineNotice = '', privateIdentity = '';

    async function request(path, { fresh = false, method = 'GET', body, privateAccess = false, type = 'application/json' } = {}) {
      const headers = { Accept:'application/json' };
      if (privateAccess) {
        await Auth.ensureSession();
        const token = Auth.getToken();
        if (!token) throw new Error('Sign in again to continue.');
        headers.Authorization = 'Bearer ' + token;
      }
      if (body !== undefined) headers['Content-Type'] = type;
      const reply = await fetch(apiURL(path), { method, body, headers, redirect:'error',
        credentials:'omit', cache:fresh ? 'reload' : 'default', signal:AbortSignal.timeout(20000) });
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
      if (region.hasAttribute('data-leaderboard-region')) updateSubmissionQuota();
      renderIcons();
      if (action) [...region.querySelectorAll('[data-action]')]
        .find(node => node.dataset.action === action && (id == null || node.dataset.id === id))?.focus();
    }

    function onlinePage() {
      return pagehead('DKR-R Online', 'See who’s online and find a public lobby.') +
        '<div class="dkr-stack" data-online-region>' + onlineContent() + '</div>';
    }
    const players = value => Number.isSafeInteger(value) && value >= 0 ? value.toLocaleString() : '—';
    function activityTime(value) {
      return Number.isSafeInteger(value) && value > 0 ? new Intl.DateTimeFormat(undefined, {
        weekday:'short', month:'short', day:'numeric', hour:'numeric', minute:'2-digit', timeZoneName:'short'
      }).format(value) : '';
    }
    function stat(label, count, detail = '') {
      return '<div><dt>' + label + '</dt><dd>' + players(count) +
        '</dd>' + (detail ? '<dd class="dkr-small dkr-muted">' + esc(detail) + '</dd>' : '') + '</div>';
    }
    function activityHistory(history, interval) {
      if (!Array.isArray(history) || !history.length) return '';
      const valid = history.filter(point => Number.isSafeInteger(point.startsAt) &&
        Number.isSafeInteger(point.players) && point.players >= 0);
      if (!valid.length) return '';
      const maximum = Math.max(1, ...valid.map(point => point.players));
      const date = new Intl.DateTimeFormat(undefined, interval === 'year' ? {year:'numeric',timeZone:'UTC'} :
        interval === 'month' ? {month:'short',year:'numeric',timeZone:'UTC'} : {month:'short',day:'numeric',timeZone:'UTC'});
      const label = interval === 'year' ? 'Yearly peaks' :
        interval === 'month' ? 'Monthly peaks' : 'Daily peaks · days with recorded activity (UTC)';
      return '<div class="dkr-online-history"><h3 class="dkr-small">' + label + '</h3>' +
        '<ol class="dkr-online-bars" aria-label="' + label + '">' + valid.map(point => {
          const tip = date.format(point.startsAt) + ' · ' + players(point.players) + ' online';
          return '<li style="--peak:' + Math.round(100 * point.players / maximum) + '%" title="' + esc(tip) +
            '" aria-label="' + esc(tip) + '"></li>';
        }).join('') + '</ol><p class="dkr-row dkr-between dkr-small dkr-muted"><span>' +
        esc(date.format(valid[0].startsAt)) + '</span>' +
        (valid.length > 1 ? '<span>' + esc(date.format(valid.at(-1).startsAt)) + '</span>' : '') + '</p></div>';
    }
    function onlineStats(live) {
      const stats = online?.stats;
      const period = stats?.periods?.[activityPeriod];
      const lifetime = activityPeriod === 'all';
      const peakTime = activityTime(stats?.peak?.observedAt);
      const busiest = period?.busiestHour;
      const busiestTime = activityTime(busiest?.startsAt);
      return '<dl class="dkr-online-stats">' + stat('Public lobbies',live ? stats?.publicLobbies : undefined) +
        stat('Racers in public lobbies',live ? stats?.publicRacers : undefined) +
        stat('Spectators in public lobbies',live ? stats?.publicSpectators : undefined) + '</dl>' +
        '<div class="dkr-row dkr-between dkr-online-periods"><h2 class="dkr-subtitle">Player activity</h2>' +
        '<div class="dkr-row" role="group" aria-label="Activity period">' +
        activityPeriods.map(([id,label]) => '<button type="button" class="dkr-button quiet small" data-action="online-period" data-id="' +
          id + '" aria-pressed="' + (activityPeriod === id) + '">' + label + '</button>').join('') + '</div></div>' +
        '<dl class="dkr-online-stats dkr-online-history-stats">' +
        stat(lifetime ? 'All-time concurrent peak' : 'Peak concurrent players', period?.peak,
          lifetime ? peakTime : '') +
        stat('Days with activity',period?.activeDays) +
        (lifetime ? '<div><dt>Activity recorded since</dt><dd class="dkr-small">' +
          esc(stats?.trackedSince ? new Intl.DateTimeFormat(undefined,{dateStyle:'medium'}).format(stats.trackedSince) : 'Collecting activity') + '</dd></div>' :
          '<div><dt>Busiest hour</dt><dd class="dkr-small">' + esc(busiestTime || 'Collecting activity') + '</dd>' +
          (busiestTime ? '<dd class="dkr-small dkr-muted">' + players(busiest.players) + ' at peak</dd>' : '') + '</div>' +
          stat('New sign-ups',online?.newSignups?.[activityPeriod])) + '</dl>' + activityHistory(period?.history,lifetime ? period?.interval || 'month' : 'day');
    }
    function onlineContent() {
      const age = online && Date.now() - online.serverTime;
      const live = online && age < 90000 && !directoryError;
      const count = players(live ? online.online : undefined);
      const registered = players(online?.totalSignedUp);
      const status = directoryError === 'disabled' ? 'The public lobby browser is currently disabled.' :
        online && !live ? 'Last known rooms. Open DKR-R Online to check availability.' :
        directoryError ? 'The lobby list could not be loaded.' :
        online ? 'Updates every 30 seconds.' : 'Loading the public lobby list…';
      const rooms = online ? [...online.rooms] : [];
      if (linkedRoom) rooms.sort((a, b) => Number(b.id === linkedRoom) - Number(a.id === linkedRoom));
      return '<section class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><div><strong class="dkr-racing-count">' +
        count + ' / ' + registered + '</strong><p class="dkr-small">Total online / Total signed up</p>' +
        '<p class="dkr-small dkr-muted" role="status">' + esc(status) + '</p></div>' +
        button('online-refresh',icon('refresh-cw') + 'Refresh','quiet small') + '</div>' + onlineStats(live) + '</section>' +
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
          online = {...online, ...data, rooms:[...rooms.values()]};
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
        privateRuns = undefined;
      }
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
      const tools = '<div class="dkr-row">' +
        (user() ? button('run-list','My runs','quiet','mine') : '') +
        (isStaff() ? button('run-list','Review runs','quiet','review') : '') + '</div>';
      if (!catalogue) return nav + '<div class="dkr-panel" role="status">Loading leaderboards…</div>';
      if (course) return nav + tools + rankingContent();
      if (section === 'overall') return nav + tools + overallContent();
      if (section === 'vanilla') return nav + tools + vanillaContent();
      if (section === 'custom') return nav + tools + customContent();
      return nav + fullGameContent();
    }

    const points = milli => Number.isSafeInteger(milli) ? (milli/1000).toLocaleString(undefined,{maximumFractionDigits:3}) : '—';
    function overallContent() {
      const explanation = '<details class="dkr-points"' + (explainPoints ? ' open' : '') +
        ' data-points-explainer><summary>How points work</summary><div class="dkr-stack">' +
        '<p>Verified results earn points for placing higher (70%) and getting closer to the record (30%). Solo boards count for 35%. Board weight grows to 100% at eight players.</p>' +
        '<p>Track scores are 75% Full Race and 25% Best Lap. Each player contributes their best time in each, across all racers.</p>' +
        '<p>Posted PBs count each current verified, published best time on a scored board. A Time Trial can have a race PB and a lap PB. World Records are the PBs holding or sharing first place.</p>' +
        '<dl><dt>Vanilla Tracks · 500</dt><dd>Scores across populated boards, with equal weight per vehicle type.</dd>' +
        '<dt>Custom Tracks · 250</dt><dd>Best 10 tracks for up to 200 points, plus the next 20 for up to 50. A new track can earn more points without reducing existing scores.</dd>' +
        '<dt>Full Game · 250</dt><dd>Finish scores across populated Full Game categories.</dd></dl></div></details>';
      if (!overall) return explanation + '<p role="status">Loading top players…</p>';
      const players = overall.players;
      const selected = players.find(player=>player.id===playerId) || players[0];
      const rows = players.map(player => {
        const active = player.id===selected.id;
        return '<tr><td>' + esc(player.place) + '</td><td>' +
          button('overall-player',esc(player.name),'quiet small',player.id,'aria-pressed="'+active+'"') +
          '</td><td>' + esc(personalBestCount(player)) + '</td><td>' + esc(player.records) +
          '</td><td><strong>' + points(player.total) + '</strong></td></tr>';
      }).join('');
      return explanation + '<div class="dkr-overall-layout"><section class="dkr-panel dkr-stack" aria-labelledby="dkr-overall-heading">' +
        '<h2 class="dkr-subtitle" id="dkr-overall-heading">Top Players</h2>' + (players.length ?
        '<table class="dkr-run-table dkr-overall-table"><thead><tr><th scope="col">Rank</th><th scope="col">Player</th>' +
        '<th scope="col">Posted PBs</th><th scope="col">World Records</th><th scope="col">Points</th></tr></thead><tbody>' +
        rows + '</tbody></table>' : '<p>The first published run will appear here.</p>') +
        (overall.tiedCutoff ? '<p class="dkr-muted">More players share the final rank shown.</p>' : '') + '</section>' +
        (selected ? profileContent(selected) : '') + '</div>';
    }

    const personalBestCount = player => Number.isSafeInteger(player.personalBests) ? player.personalBests : '—';

    function profileContent(player) {
      const tabs = [['score','Score breakdown'],['runs','Ranked runs']].map(([id,label]) =>
        button('overall-panel',label,'quiet small',id,'aria-pressed="'+(playerPanel===id)+'"')).join('');
      const stats = '<dl class="dkr-overall-counts"><div><dt>Posted PBs</dt><dd>' + esc(personalBestCount(player)) +
        '</dd></div><div><dt>World Records</dt><dd>' + esc(player.records) + '</dd></div></dl>';
      const scoreBreakdown = '<div class="dkr-profile-points">' +
        [['Vanilla Tracks','vanilla',500],['Custom Tracks','custom',250],['Full Game','fullGame',250]].map(([name,id,cap]) => {
          const value = player.points[id];
          return '<div><div class="dkr-row dkr-between"><strong>' + name + '</strong><span>' + points(value) +
            ' / ' + cap + ' pts</span></div><progress aria-label="' + name + ' points" max="' + cap +
            '" value="' + (value/1000) + '"></progress></div>';
        }).join('') + '</div><p class="dkr-muted">' + esc(player.podiums) + ' podiums · ' +
        esc(player.coverage) + ' ranked events</p>';
      return '<section class="dkr-panel dkr-stack dkr-overall-profile" aria-labelledby="dkr-overall-player-heading">' +
        '<div class="dkr-row dkr-between"><div><p class="dkr-small dkr-muted">#' + esc(player.place) +
        ' overall</p><h2 class="dkr-subtitle" id="dkr-overall-player-heading">' + esc(player.name) +
        '</h2></div><strong class="dkr-overall-score">' + points(player.total) + ' pts</strong></div>' +
        '<nav class="dkr-row dkr-overall-profile-tabs" aria-label="Player statistics">' + tabs + '</nav>' + stats +
        (playerPanel==='runs' ? profileRunsContent(player.id) : scoreBreakdown) + '</section>';
    }

    function profileRunsContent(id) {
      if (profileError) return '<p role="status">' + esc(profileError) + '</p>' + button('run-refresh','Try again','quiet small');
      if (profile?.player.id!==id) return '<p role="status">Loading ranked runs…</p>';
      const {events,customTracks} = profile;
      const custom = new Map(customTracks.map(track=>[track.id,track]));
      return '<div class="dkr-racing-list">' + events.map(event => {
          const customScore = event.kind==='custom' && custom.get(event.id);
          const record = (label,result) => '<div class="dkr-row"><span>' + label + ' · #' + esc(result.place) + ' · ' +
            clock(result.ticks) + '</span>' + (result.place===1 ? '<strong class="dkr-overall-record">World Record</strong>' : '') +
            (result.hasVideo ? button('run-video','Watch Video','quiet small',result.runId) : '') + '</div>';
          return '<article class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><strong>' + esc(event.title) + '</strong>' +
            '<button type="button" class="dkr-button quiet small" data-action="overall-track" data-id="' + esc(event.id) + '" data-kind="' +
            esc(event.kind) + '" data-vehicle="' + esc(event.vehicle) + '">View Times</button></div><p class="dkr-muted">' +
            esc(sections.find(([id]) => id===event.kind)?.[1]) + (event.kind==='full-game' ? '' : ' · ' + esc(vehicleNames[event.vehicle])) +
            (customScore ? ' · Scoring track #' + esc(customScore.slot) + ' (' + points(customScore.points) + ' pts)' : '') + '</p>' +
            record(event.kind==='full-game' ? 'Finish' : 'Full Race',event.race) +
            (event.lap ? record('Best Lap',event.lap) : '') + '</article>';
        }).join('') + '</div>' + (profile.nextOffset!=null ? button('run-more-profile','More results','quiet') : '');
    }

    const recordDay = stamp => new Date(stamp*1000).toLocaleDateString(undefined,{day:'numeric',month:'short',year:'numeric',timeZone:'UTC'});
    function reignDuration(seconds) {
      for (const [unit,size] of [['day',86400],['hour',3600],['minute',60]]) {
        const count = Math.floor(seconds/size);
        if (count>0) return count + ' ' + unit + (count===1 ? '' : 's');
      }
      return Math.max(0,seconds) + ' sec';
    }
    function recordHistoryContent(history) {
      if (!Array.isArray(history?.changes)) return '';
      const events = history.changes;
      const incomplete = '<p class="dkr-small dkr-muted">Some publication dates are unavailable.</p>';
      if (!events.length) return '<section class="dkr-record-history"><h3>World record history</h3>' +
        (history.complete ? '<p class="dkr-muted">History appears after the first published run.</p>' : incomplete) + '</section>';
      const first = events[0], last = events.at(-1);
      const choice = events.findIndex(event=>event.at===selectedRecord);
      const index = choice<0 ? events.length-1 : choice;
      const chosen = events[index];
      const end = history.complete ? Math.max(last.at,history.asOf) : last.at;
      const x = stamp => 12 + 976*(stamp-first.at)/Math.max(end-first.at,1);
      const y = time => 9 + 78*(first.ticks-time)/Math.max(first.ticks-last.ticks,1);
      const line = events.map((event,i)=>i ? 'H '+x(event.at)+' V '+y(event.ticks) : 'M '+x(event.at)+' '+y(event.ticks)).join(' ')+' H '+x(end);
      const duration = chosen.until || history.complete ? ' · ' + (chosen.until ? 'Stood for ' : 'Unbeaten for ') +
        reignDuration((chosen.until || end)-chosen.at) : '';
      return '<section class="dkr-record-history dkr-stack"><div class="dkr-row dkr-between"><h3>World record history</h3>' +
        '<strong>' + clock(last.ticks) + '</strong></div><div class="dkr-record-plot" aria-label="Record time by publication date">' +
        '<div class="dkr-record-axis"><span>' + clock(first.ticks) + '</span>' +
        (last.ticks!==first.ticks ? '<span>'+clock(last.ticks)+'</span>' : '') +
        '</div><div><div class="dkr-record-line"><svg viewBox="0 0 1000 100" preserveAspectRatio="none" aria-hidden="true">' +
        '<path class="dkr-record-grid" d="M 0 9 H 1000 M 0 48 H 1000 M 0 87 H 1000"/><path class="dkr-record-step" d="' + line +
        '"/></svg>' + events.map((event,i) => '<button type="button" class="dkr-record-mark" tabindex="-1" data-action="record-point" data-id="' +
          event.at + '" aria-label="' + esc(recordDay(event.at) + ' · ' + clock(event.ticks)) + '" aria-pressed="' + (index===i) +
          '" style="left:' + x(event.at)/10 + '%;top:' + y(event.ticks) + '%"></button>').join('') +
        '</div><div class="dkr-record-dates"><span>' + recordDay(first.at) + '</span>' + (end!==first.at ? '<span>'+recordDay(end)+'</span>' : '') +
        '</div></div></div><label class="dkr-ranked-select"><span>Record changes</span><select class="dkr-select" data-board-filter="record">' +
        events.map(event=>option(String(event.at),recordDay(event.at)+' · '+event.holders.map(holder=>holder.name).join(', ')+' · '+clock(event.ticks),String(chosen.at))).join('') +
        '</select></label><div aria-live="polite"><strong>' + clock(chosen.ticks) + '</strong><span class="dkr-muted"> · ' +
        recordDay(chosen.at) + esc(duration) + '</span>' + chosen.holders.map(holder => '<p>' + esc(holder.name) +
          (holder.until || history.complete ? ' · ' + (holder.until ? 'Held for ' : 'Holding for ') +
            reignDuration((holder.until || end)-holder.from) : '') + '</p>').join('') +
        '</div>' + (!history.complete ? incomplete : '') +
        (history.earlierChanges ? '<p class="dkr-muted">Showing the latest record changes.</p>' : '') + '</section>';
    }

    function option(value, label, selected) {
      return '<option value="' + esc(value) + '"' + (value === selected ? ' selected' : '') + '>' + esc(label) + '</option>';
    }

    function filterSelect(label, key, first, options, value) {
      return '<label class="dkr-ranked-select"><span>' + label + '</span><span class="dkr-ranked-select-control"><select class="dkr-select" data-board-filter="' +
        key + '" aria-label="' + label + '">' + (first ? option('', first, value) : '') +
        options.map(([id,name]) => option(id,name,value)).join('') + '</select>' + icon('chevron-down') + '</span></label>';
    }

    function trackFilters(trackWorlds = []) {
      const search = '<label class="dkr-ranked-select"><span>Search tracks</span><span class="dkr-search">' + icon('search') +
        '<input type="search" aria-label="Search tracks" data-board-filter="query" placeholder="Track name" value="' + esc(query) + '"></span></label>';
      const kind = section === 'custom' ? '' :
        filterSelect('World', 'world', 'All worlds', trackWorlds.map(name => [name,name]), world) +
        filterSelect('Vehicle', 'vehicle', 'All vehicles', Object.entries(vehicleNames).slice(0,3), vehicle);
      return '<div class="dkr-board-filters">' + search + kind + filterSelect('Status', 'availability', 'All tracks',
        [['ranked','Ranked'],['pending','Awaiting qualification']],availability) + '</div>';
    }

    function statusMatches(qualified) {
      return !availability || qualified === (availability === 'ranked');
    }

    function trackScore(label, score) {
      return '<div class="dkr-track-score"><span class="dkr-muted">' + label + '</span>' +
        (Number.isSafeInteger(score?.ticks) && score.ticks > 0 ?
          '<span class="dkr-track-keeper">' + esc(score.name) + '</span><span class="dkr-track-time"><strong>' +
          clock(score.ticks) + '</strong><span class="dkr-first-place" aria-label="First place">' + icon('crown') +
          '<span aria-hidden="true">1st</span></span></span>' : '<span aria-label="No time">—</span>') + '</div>';
    }

    function trackRow(track, details, qualified, fullGame = false) {
      const vehicles = track.vehicles || [];
      const leaders = track.leaders || {};
      const records = fullGame ? trackScore('Finish time', leaders.mixed?.race) :
        vehicles.map(id => '<section class="dkr-track-vehicle" aria-label="' + esc(vehicleNames[id]) + '"><h3>' +
          esc(vehicleNames[id]) + '</h3>' + trackScore('Full race', leaders[id]?.race) +
          trackScore('Best lap', leaders[id]?.lap) + '</section>').join('');
      const route = fullGame && track.stages.length ? '<details class="dkr-track-splits"><summary>Route splits</summary>' +
        '<ol class="dkr-adventure-splits">' + track.stages.map(stage => '<li>' + esc(stage) + '</li>').join('') +
        '</ol></details>' : '';
      return '<article class="dkr-panel dkr-track-row"><div class="dkr-track-name"><h2>' +
        esc(track.title) + '</h2><p class="dkr-small dkr-muted">' + esc(details) +
        '</p><div class="dkr-track-actions">' +
        (qualified ? button('run-course','View Times','quiet',track.id) : '<span class="dkr-status">Awaiting qualification</span>') +
        route + '</div></div>' +
        (records ? '<div class="dkr-track-records">' + records + '</div>' : '') + '</article>';
    }

    function vanillaContent() {
      const boards = catalogue.boards.filter(value => value.kind === 'vanilla');
      const tracks = catalogue.vanillaTracks || [];
      const isRanked = track => boards.some(value => value.title === track.title && value.ranked && (!vehicle || value.vehicle === vehicle));
      const worlds = [...new Set(tracks.map(track => track.world))].filter(Boolean);
      const matching = tracks.filter(track => matches(track.title) && (!world || track.world === world) &&
        (!vehicle || track.vehicles.includes(vehicle)) && statusMatches(isRanked(track)));
      return trackFilters(worlds) + '<div data-track-results><p class="dkr-resultmeta">' + matching.length + ' of ' + tracks.length + ' tracks</p>' +
        (matching.length ? '<div class="dkr-racing-list">' + matching.map(track =>
          trackRow(track, [track.world,...track.vehicles.map(id => vehicleNames[id])].join(' · '), isRanked(track))
        ).join('') + '</div>' : '<p>No tracks match these filters.</p>') + '</div>';
    }

    function customContent() {
      const matching = catalogue.customTracks.filter(track => matches(track.title) && statusMatches(track.qualified));
      return trackFilters() + '<section class="dkr-stack" data-track-results><h2 class="dkr-subtitle">Tracks in the Mod Library</h2><div class="dkr-racing-list">' +
        matching.map(track => trackRow(track, ['Version ' + track.version,...(track.vehicles || []).map(id => vehicleNames[id])].join(' · '),
          track.qualified)).join('') +
        (matching.length ? '' : '<p class="dkr-muted">No loaded tracks match these filters.</p>') +
        '</div>' + (catalogue.nextCursor ? button('run-more','More tracks','quiet') : '') + '</section>';
    }

    function rankingContent() {
      if (!ranking) return '<div class="dkr-panel" role="status">Loading times…</div>';
      const videoEnabled = catalogue?.videoEnabled !== false;
      const videoNotice = videoEnabled && catalogue?.hostedVideos ?
        '<p class="dkr-muted">The leaderboard host prepares videos for review. You can close DKR-R once your upload finishes.</p>' : '';
      const filters = section === 'full-game' ? '' :
        filterSelect('Racer','ranking-racer','All racers',ranking.racers.map(item => [item.id,item.name]),ranking.racer) +
        (ranking.vehicles.length ? filterSelect('Vehicle','ranking-vehicle','',ranking.vehicles.map(id => [id,vehicleNames[id]]),ranking.vehicle) : '');
      return '<section class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><div><h2 class="dkr-subtitle">' +
        esc(ranking.track.title) + '</h2><p class="dkr-muted">' + esc(ranking.track.world || ranking.track.version || '') +
        '</p></div>' + button('run-back',fromProfile ? 'Back to player stats' : section==='full-game' ? 'All categories' : 'All tracks','quiet small') + '</div>' +
        '<div class="dkr-ranking-filters">' + filters + '</div>' +
        (section === 'full-game' ? '' : '<nav class="dkr-localnav" aria-label="Times">' + [['race','Full race'],['lap','Best lap']].map(([id,label]) =>
          '<button type="button" data-action="run-score" data-id="' + id + '" aria-pressed="' + (score === id) +
          '">' + label + '</button>').join('') + '</nav>') +
        videoNotice +
        (ranking.entries.length ? '<table class="dkr-run-table"><thead><tr><th>Rank</th><th>Player</th><th>Racer</th><th>Time</th>' +
          (videoEnabled ? '<th>Video</th>' : '') + '</tr></thead><tbody>' +
        ranking.entries.map(entry => '<tr><td>' + esc(entry.place) + '</td><td>' + esc(entry.name) + '</td><td>' +
          esc(entry.racer || '—') + '</td><td>' + clock(entry.ticks) + '</td>' +
          (videoEnabled ? '<td>' + (catalogue?.hostedVideos && !entry.hasVideo ? 'Video pending' :
            button('run-video','Watch Video','quiet small',entry.id)) + '</td>' : '') +
          '</tr>').join('') + '</tbody></table>' :
          '<p>' + (ranking.ranked ? 'No verified runs have been published for this selection.' : 'This track and vehicle are awaiting replay qualification.') +
          '</p>') + (ranking.nextOffset != null ? button('run-more-times','More times','quiet') : '') +
        recordHistoryContent(ranking.history) + '</section>';
    }

    function fullGameContent() {
      const categories = catalogue.adventureCatalogue || [];
      return '<section class="dkr-stack"><div class="dkr-racing-list">' + categories.map(category => {
          const ranked = catalogue.boards.some(board => board.id === 'adventure-' + category.id && board.ranked);
          return trackRow(category, category.summary, ranked, true);
        }).join('') +
        '</div><details class="dkr-panel dkr-full-category"><summary><strong>Shared rules and verification</strong>' + icon('chevron-down') +
        '</summary><p>The ranked launcher supports US v1.0 and US v1.1. Tracks routes get one completed save slot to unlock the courses. All Bosses uses that slot in Adventure. The other Adventure routes start from a new save. Each run uses its own save and blocks gameplay mods and Magic Codes.</p>' +
        '<p>The clock starts in the Overworld for Adventure routes and in the first race for Tracks routes. It keeps running across menus, travel and races. The verifier checks the finish event and split times before staff review.</p></details></section>';
    }
    function updateSubmissionQuota() {
      clearTimeout(quotaTimer);
      const panel = view() === 'speedruns' && root.querySelector('[data-submission-quota]');
      const quota = privateRuns?.submissionQuota;
      if (!panel || !quota) return;
      const now = quota.observedAt * 1000 + (performance.now() - privateRuns.receivedAt);
      const active = quota.releasesAt.filter(stamp => stamp * 1000 > now);
      panel.querySelector('strong').textContent = (quota.maxRuns - active.length) + ' of ' + quota.maxRuns + ' submissions left';
      const time = panel.querySelector('time');
      time.parentElement.hidden = active.length === 0;
      if (active.length) {
        const next = Math.min(...active) * 1000;
        const date = new Date(next);
        time.dateTime = date.toISOString();
        time.textContent = quotaDate.format(date);
        quotaTimer = setTimeout(updateSubmissionQuota, Math.ceil(next - now));
      }
    }

    function runListContent() {
      const review = listingTab === 'review';
      const rows = privateRuns?.runs ?? [];
      const quota = !review && !runId && privateRuns?.submissionQuota ?
        '<section class="dkr-panel dkr-stack" role="status" data-submission-quota><p><strong></strong></p>' +
        '<p class="dkr-muted" hidden>Next slot opens <time></time> (local time).</p></section>' : '';
      const videoStatus = {
        queued:'Waiting for the video renderer',
        rendering:'Preparing video…',
        retrying:'Video render will retry automatically',
        unavailable:'Video rendering is unavailable',
      };
      return '<div class="dkr-row dkr-between"><h2 class="dkr-subtitle">' + (listingTab === 'review' ? 'Review runs' : 'My runs') +
        '</h2><div class="dkr-row">' + (runId ? button('run-list','Show all runs','quiet',listingTab) : '') +
        button('run-refresh','Refresh','quiet') + button('run-back','Back to leaderboards','quiet') + '</div></div>' + quota +
        (leaderboardBusy ? '<p role="status">Loading runs…</p>' : rows.length ?
          '<div class="dkr-racing-list">' + rows.map(run => '<article class="dkr-panel dkr-stack"' + (review ? ' data-review-run' : '') +
            '><div class="dkr-row dkr-between"><h3 class="dkr-subtitle">' +
            esc(run.title) + '</h3><span class="dkr-status">' + esc(states[run.state] || run.state) +
            '</span></div><p>' + esc(run.category) + ' · ' + esc(run.vehicle) + ' · ' + clock(run.ticks ?? run.claimedTicks) +
            (run.ticks == null ? ' (recorded)' : '') + '</p><p class="dkr-muted">' + esc(run.name) + ' · ' +
            (run.linkedLocal ? 'Linked local run' : esc(run.platform) + ' ' + esc(run.arch)) + (run.rom === 77 ? ' · US v1.0' : ' · US v1.1') + '</p>' +
            (run.notes ? '<p>' + esc(run.notes) + '</p>' : '') + (run.reason ? '<p>' + esc(run.reason) + '</p>' : '') +
            (run.state === 'published' || review && run.videoState === 'ready' ?
              '<div class="dkr-row">' + button('run-video','Watch Video',review ? 'gold' : 'quiet',run.id) + '</div>' :
              run.state === 'verified' && videoStatus[run.videoState] ?
                '<p class="dkr-muted">' + videoStatus[run.videoState] + '</p>' : '') +
            (review ? '<div class="dkr-stack"><label>Review note <input data-review-note class="dkr-input" maxlength="600"></label><div class="dkr-row">' +
              (run.videoRequired && run.videoState !== 'ready' ?
                '<button type="button" class="dkr-btn gold" disabled>Publish</button>' : button('run-publish','Publish','gold',run.id)) +
              button('run-reject','Reject','red',run.id) + '</div></div>' : '') +
            '</article>').join('') + '</div>' : '<div class="dkr-panel"><p>' +
            (runId ? 'This run is no longer available in this list.' : 'No runs to show yet.') + '</p></div>');
    }

    async function loadLeaderboards({ more = false, focus } = {}) {
      const seq = ++leaderSequence;
      const playerOnly = listingTab==='boards' && !course && section==='overall' && playerPanel==='runs' && !!playerId;
      leaderboardBusy = true;
      leaderboardError = '';
      if (playerOnly) profileError = '';
      try {
        if (listingTab !== 'boards') {
          const endpoint = 'speedruns/' + listingTab + (runId ? '?run=' + encodeURIComponent(runId) : '');
          const data = await request(endpoint, { privateAccess:true });
          if (seq === leaderSequence) privateRuns = { ...data, receivedAt:performance.now() };
        } else if (course) {
          const params = new URLSearchParams({score,kind:section});
          if (courseVehicle) params.set('vehicle',courseVehicle);
          if (racer) params.set('racer',racer);
          if (more && ranking?.nextOffset != null) params.set('offset',ranking.nextOffset);
          const data = await request('speedruns/tracks/' + encodeURIComponent(course) + '?' + params);
          if (seq === leaderSequence) ranking = more && ranking ?
            {...data,history:ranking.history,entries:[...ranking.entries,...data.entries]} : data;
        } else {
          if (section==='overall') {
            const path = playerOnly ? 'speedruns/overall/'+encodeURIComponent(playerId) +
              (more && profile?.nextOffset!=null ? '?offset='+profile.nextOffset : '') : 'speedruns/overall';
            const [base,data] = await Promise.all([catalogue || request('speedruns'),request(path,{fresh:true})]);
            if (seq === leaderSequence) {
              catalogue = base;
              if (playerOnly) profile = more && profile ? {...data,events:[...profile.events,...data.events]} : data;
              else {
                overall = data;
                if (!overall.players.some(player=>player.id===playerId)) playerId = overall.players[0]?.id || '';
              }
            }
          } else {
            const data = await request('speedruns' + (more && catalogue?.nextCursor ? '?cursor=' + encodeURIComponent(catalogue.nextCursor) : ''));
            if (seq === leaderSequence) catalogue = more && catalogue ? {...catalogue, nextCursor:data.nextCursor,
              customTracks:[...catalogue.customTracks, ...data.customTracks]} : data;
          }
        }
      } catch (error) {
        if (seq === leaderSequence) {
          if (playerOnly) profileError = error.message;
          else leaderboardError = error.message;
        }
      } finally {
        if (seq === leaderSequence) {
          leaderboardBusy = false;
          if (view() === 'speedruns') refreshRegion('[data-leaderboard-region]',speedrunContent,focus);
        }
      }
    }

    function postPage() {
      return pagehead('Post a run', 'Choose Post to Leaderboard in the Launcher’s Replays tab. Once the upload finishes you can close DKR-R. The host prepares the video and Time Trial ghost for review. Track the submission in My runs.', 'speedruns');
    }

    function apiURL(path) {
      const endpoint = new URL(config.apiBase, location.href);
      const local = endpoint.origin === location.origin && ['127.0.0.1','localhost'].includes(endpoint.hostname);
      if ((!local && endpoint.protocol !== 'https:') || endpoint.username || endpoint.password || endpoint.hash || endpoint.search)
        throw new Error('The library address is invalid.');
      return new URL('/v1/' + path, endpoint);
    }

    async function openRunVideo(id, target) {
      if (!/^[A-Za-z0-9_-]{1,96}$/.test(id)) return;
      const parent = target.closest('[data-leaderboard-region]');
      if (!parent) return;
      const reviewCard = target.closest('[data-review-run]');
      const previous = parent.querySelector('[data-run-player]');
      const resumeAt = previous?.dataset.runPlayer === id ? previous.querySelector('video')?.currentTime : 0;
      if (previous) previous.remove();
      const player = document.createElement('section');
      player.className = 'dkr-panel dkr-stack dkr-run-player';
      player.dataset.runPlayer = id;
      const label = document.createElement('h2');
      label.className = 'dkr-subtitle';
      label.textContent = reviewCard ? 'Review video' : 'Verifier-rendered replay';
      const status = document.createElement('p');
      status.setAttribute('role', 'status');
      status.textContent = 'Checking for video…';
      const close = document.createElement('button');
      close.type = 'button';
      close.className = 'dkr-btn quiet';
      close.textContent = 'Close video';
      close.addEventListener('click', () => { player.remove(); target.focus(); });
      player.append(label, status, close);
      if (reviewCard) target.parentElement.after(player);
      else parent.append(player);
      player.scrollIntoView({ block:'nearest' });
      close.focus({ preventScroll:true });
      try {
        const path = 'speedruns/runs/' + encodeURIComponent(id) + '/video';
        const url = apiURL(path);
        if (reviewCard) {
          const { grant } = await request(path, {method:'POST', privateAccess:true});
          if (!player.isConnected) return;
          url.searchParams.set('review',grant);
        }
        const ready = await fetch(url, { method:'HEAD', signal:AbortSignal.timeout(15000), redirect:'error' });
        if (!player.isConnected) return;
        if (ready.status === 404) {
          status.textContent = 'This run’s video is preparing or has not been rendered yet.';
          return;
        }
        if (!ready.ok) throw new Error();
        const video = document.createElement('video');
        video.controls = true;
        video.preload = 'metadata';
        video.playsInline = true;
        if (reviewCard) video.crossOrigin = 'anonymous';
        video.setAttribute('aria-label', 'Verifier-rendered gameplay');
        if (resumeAt > 0) video.addEventListener('loadedmetadata', () => { video.currentTime = resumeAt; }, {once:true});
        video.addEventListener('error', () => {
          status.hidden = false;
          status.textContent = 'The video could not be played. Open Watch Video again to retry.';
        });
        video.src = url.href;
        status.hidden = true;
        player.insertBefore(video, close);
      } catch {
        status.textContent = 'The run video is temporarily unavailable.';
      }
    }

    async function openRunList(tab, id = '') {
      if (!requireSignedIn({view:'speedruns'}, tab === 'review')) return;
      if (view() !== 'speedruns') await go('speedruns', {}, false);
      listingTab = tab;
      runId = id;
      privateRuns = undefined;
      refreshRegion('[data-leaderboard-region]', speedrunContent);
      await loadLeaderboards({focus:'run-back'});
    }

    function updateTrackFilter(target) {
      const filter = target.dataset.boardFilter;
      if (!filter || !catalogue) return;
      if (filter === 'query') query = target.value;
      if (filter === 'world') world = target.value;
      if (filter === 'vehicle') vehicle = target.value;
      if (filter === 'availability') availability = target.value;
      const template = document.createElement('template');
      template.innerHTML = section === 'custom' ? customContent() : vanillaContent();
      root.querySelector('[data-track-results]')?.replaceWith(template.content.querySelector('[data-track-results]'));
      renderIcons();
    }
    root.addEventListener('input', event => {
      if (event.target.dataset.boardFilter === 'query') updateTrackFilter(event.target);
    });
    root.addEventListener('change', async event => {
      const filter = event.target.dataset.boardFilter;
      if (course && filter==='record') {
        selectedRecord = Number(event.target.value);
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        root.querySelector('[data-board-filter="record"]')?.focus({preventScroll:true});
        return true;
      }
      if (course && (filter === 'ranking-racer' || filter === 'ranking-vehicle')) {
        if (filter === 'ranking-vehicle') {
          courseVehicle = event.target.value;
          racer = '';
        } else racer = event.target.value;
        selectedRecord = null;
        await loadLeaderboards();
        root.querySelector('[data-board-filter="' + filter + '"]')?.focus({preventScroll:true});
      } else if (filter !== 'query') updateTrackFilter(event.target);
    });

    async function action(action, id, target) {
      if (action === 'online' || action === 'speedruns' || action === 'post-run') { await go(action); return true; }
      if (action === 'online-period') {
        if (activityPeriods.some(([value]) => value === id)) {
          activityPeriod = id;
          refreshRegion('[data-online-region]',onlineContent);
        }
        return true;
      }
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
      if (action === 'overall-player') {
        if (playerId===id) return true;
        ++leaderSequence;
        leaderboardBusy = false;
        playerId = id;
        playerPanel = 'score';
        profile = undefined;
        profileError = '';
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        return true;
      }
      if (action === 'overall-panel' && (id==='score' || id==='runs')) {
        playerId = playerId || overall?.players[0]?.id || '';
        playerPanel = id;
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        if (id==='runs' && profile?.player.id!==playerId) await loadLeaderboards();
        return true;
      }
      if (action === 'record-point') {
        selectedRecord = Number(id);
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        return true;
      }
      if (action === 'run-course' || action === 'overall-track') {
        fromProfile = action==='overall-track';
        if (fromProfile) section = target.dataset.kind;
        course = id;
        courseVehicle = fromProfile && section!=='full-game' ? target.dataset.vehicle : section === 'vanilla' ? vehicle : '';
        selectedRecord = null;
        ranking = undefined;
        racer = '';
        score = 'race';
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        await loadLeaderboards({focus:'run-back'});
        return true;
      }
      if (action === 'run-category' || action === 'run-back') {
        if (action === 'run-category') {
          section = id;
          fromProfile = false;
          playerId = '';
          playerPanel = 'score';
          profile = undefined;
          profileError = '';
          query = world = vehicle = availability = '';
        } else if (fromProfile) {
          section = 'overall';
          fromProfile = false;
        }
        ++leaderSequence;
        const previous = course;
        course = runId = '';
        ranking = undefined;
        listingTab = 'boards';
        privateRuns = undefined;
        leaderboardError = '';
        refreshRegion('[data-leaderboard-region]',speedrunContent);
        if (previous) [...root.querySelectorAll('[data-action="run-course"]')]
          .find(button => button.dataset.id === previous)?.focus({preventScroll:true});
        if (previous && section==='overall') [...root.querySelectorAll('[data-action="overall-track"]')]
          .find(button=>button.dataset.id===previous)?.focus({preventScroll:true});
        if (!catalogue || section==='overall' && !overall) await loadLeaderboards();
        return true;
      }
      if (action === 'run-list') { await openRunList(id); return true; }
      if (action === 'run-more' || action === 'run-more-times' || action === 'run-more-profile' || action === 'run-refresh') {
        await loadLeaderboards({more:action !== 'run-refresh'});
        return true;
      }
      if (action === 'run-score' && (id === 'race' || id === 'lap')) {
        score = id;
        selectedRecord = null;
        await loadLeaderboards({focus:'run-score'});
        return true;
      }
      if (action === 'run-video') { await openRunVideo(id, target); return true; }
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
      updateSubmissionQuota();
      if (next === currentPage) return;
      currentPage = next;
      clearInterval(directoryTimer);
      if (next !== 'speedruns') {
        ++leaderSequence;
        section = 'overall';
        listingTab = 'boards';
        course = runId = playerId = '';
        playerPanel = 'score';
        ranking = profile = undefined;
        profileError = '';
        fromProfile = false;
        privateRuns = undefined;
        leaderboardError = '';
      }
      if (next === 'online' && root.querySelector('[data-online-region]')) {
        loadOnline();
        directoryTimer = setInterval(() => { if (!document.hidden) loadOnline(); },30000);
      }
      if (next === 'speedruns' && !course && (!catalogue || !overall)) loadLeaderboards();
    }
    root.addEventListener('toggle',event => {
      if (event.target.matches('[data-points-explainer]')) explainPoints = event.target.open;
    },true);
    return { onlinePage, speedrunsPage, postPage, action, mounted, openRunList };
  }};
}());
