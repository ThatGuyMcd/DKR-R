DKRLauncher.pages = DKRLauncher.pages || {};

(function () {
  const { ui, state, rom: roms } = DKRLauncher;
  const { h } = ui;

  // Quick Join codes skip look-alike characters (0/O, 1/I) so they can be read aloud.
  const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  const CODE_LENGTH = 5;
  const CODE_LIFETIME_MS = 6 * 60 * 60 * 1000;
  const MAX_PLAYERS = 4;
  const COUNTDOWN_SECONDS = 5;
  const PROFILES = ['Player 1 profile', 'Player 2 profile', 'Player 3 profile', 'Player 4 profile'];
  const POSITIONS = ['Top left', 'Top right', 'Bottom left', 'Bottom right'];
  // Typing this code shows the "no lobby found" state.
  const MISSING_CODE = 'XXXXX';

  // The session is a simulation held in memory only; it never survives a reload.
  let session = { phase: 'idle' };
  let joinCode = '';
  let joinError = '';
  let root = null;
  let regions = null;
  let shownPhase = null;
  const renderedSwitches = new Map();
  let ownsModLock = false;
  const timers = new Set();
  let section = 'lobbies';
  let friendSearch = '';
  let friendFilter = 'All friends';
  let friendSort = 'Online first';
  let joinHost = 'Banjo64';
  let guideShown = false;
  // revision: the Game Pak the host races on (1 = v 1.0, 2 = v 1.1). Everyone in a lobby must match.
  const demoLobbies = [
    { name: 'Banjo64', code: 'BANJ4', players: 1, max: 4, ping: 42, invited: true, compatible: true, revision: 2 },
    { name: 'TipTupTina', code: 'TINA7', players: 1, max: 2, ping: 68, compatible: true, revision: 1 },
  ];

  const online = () => state.get().online;
  const romRevision = () => (state.get().rom.ready ? roms.revisionOf(state.get().rom.path) : 0);
  const versionName = (revision) => (revision === 2 ? 'v 1.1' : 'v 1.0');
  const romFor = (revision) => roms.entries().find((entry) => roms.revisionOf(entry.path) === revision);

  // Switching ROMs never leaves Online: the drop-down, lobby cards and checklist all switch in place.
  function romChanged(path, added) {
    ui.notify((added ? 'Added and selected ' : 'Now racing on ') + roms.label(path) + '.');
  }

  function openRomBrowser() {
    roms.openBrowser({ onPicked: romChanged });
  }

  // focusAfter: the control that replaces the switch button (the lobby's join button), so the
  // D-pad carries on from where it was.
  function switchTo(revision, focusAfter) {
    const entry = romFor(revision);
    const land = () => root?.querySelector(`[data-fk="${CSS.escape(focusAfter)}"]`)?.focus();
    if (!entry) {
      roms.openBrowser({ onPicked: (path, added) => { romChanged(path, added); if (roms.revisionOf(path) === revision) land(); } });
      return;
    }
    roms.select(entry.path);
    romChanged(entry.path, false);
    land();
  }

  function randomCode() {
    let code = '';
    for (let i = 0; i < CODE_LENGTH; i += 1) code += CODE_CHARS[Math.floor(Math.random() * CODE_CHARS.length)];
    return code === MISSING_CODE ? randomCode() : code;
  }

  function later(ms, fn) {
    const id = setTimeout(() => { timers.delete(id); fn(); render(); }, ms);
    timers.add(id);
  }

  function clearTimers() {
    timers.forEach(clearTimeout);
    timers.clear();
  }

  // An open lobby locks mod changes, exactly like the Mods page expects.
  function setModLock(active) {
    ownsModLock = active;
    if (!!state.get().mods.lobbyActive !== active) state.update('mods', { lobbyActive: active });
  }
  window.addEventListener('pagehide', () => { if (ownsModLock) setModLock(false); });

  async function copy(text, message) {
    try { await navigator.clipboard.writeText(text); ui.notify(message); }
    catch { ui.notify('Clipboard unavailable. Select and copy the code shown on screen.'); }
  }

  // ------------------------------------------------------------ session actions

  function me(ready = false) {
    return { id: 'me', name: online().nickname || 'Player', you: true, ready, ping: 0, saveReady: true };
  }

  function inLobby() {
    return session.phase === 'lobby';
  }

  function hasSpace() {
    return inLobby() && !session.locked && !session.testing && !session.countdown && !session.launched &&
      session.players.length < (session.rules?.maximumRacers || MAX_PLAYERS);
  }

  function addPlayer(name, ready = false) {
    const player = { id: 'p-' + name, name, ready, ping: 28 + Math.floor(Math.random() * 90), saveReady: false };
    session.players.push(player);
    later(900, () => { if (inLobby() && session.players.includes(player)) player.saveReady = true; });
    if (!ready) later(1600 + Math.random() * 1400, () => setReady(player.id, true));
  }

  function setReady(id, ready) {
    if (!inLobby() || session.countdown || session.launched) return;
    if (session.testing) {
      if (id !== 'me') later(500, () => setReady(id, ready));
      return;
    }
    const player = session.players.find((p) => p.id === id);
    if (player) player.ready = ready;
    // A guest's host starts once everyone is ready.
    if (session.role === 'guest' && allReady() && !session.countdown) later(1200, () => { if (allReady()) startRace(); });
  }

  function allReady() {
    return inLobby() && !session.testing && !session.launched && session.players.length >= 2 && session.players.every((p) => p.ready && p.saveReady);
  }

  function createLobby() {
    if (!state.get().rom.ready || (online().saveSeed === 'Continue with previous session' && !online().previousOnlineSave)) return;
    clearTimers();
    section = 'lobby';
    session = {
      phase: 'lobby', role: 'host', code: randomCode(), expiresAt: Date.now() + CODE_LIFETIME_MS,
      locked: false, players: [me()], requests: [], invites: {}, countdown: 0,
      name: online().roomName, rules: { ...online().hostSettings }, saveSeed: online().saveSeed, blocked: [],
    };
    setModLock(true);
    later(2600, () => requestFrom('KrunchKart'));
    render();
  }

  function requestFrom(name) {
    if (!inLobby() || session.locked || !hasSpace() || session.blocked.includes(name)) return;
    if (session.requests.some((r) => r.name === name) || session.players.some((p) => p.name === name)) return;
    session.requests.push({ id: 'r-' + name, name, compatible: true });
  }

  function answerRequest(id, approve) {
    if (busy()) return;
    const request = session.requests.find((r) => r.id === id);
    session.requests = session.requests.filter((r) => r.id !== id);
    if (approve && request && request.compatible && hasSpace()) {
      addPlayer(request.name);
      ui.notify(`${request.name} joined your lobby.`);
    }
    render();
  }

  function removePlayer(id) {
    if (session.testing || session.countdown || session.launched) return;
    const player = session.players.find((p) => p.id === id);
    session.players = session.players.filter((p) => p.id !== id);
    if (player) delete session.invites[player.name];
    render();
  }

  function invite(friend) {
    if (!hasSpace() || friend.blocked || session.role !== 'host') return;
    session.invites[friend.name] = 'sent';
    later(600, () => { if (inLobby() && session.invites[friend.name] === 'sent') session.invites[friend.name] = 'delivered'; });
    later(2200, () => {
      if (!inLobby() || !['sent', 'delivered'].includes(session.invites[friend.name])) return;
      if (!hasSpace()) { delete session.invites[friend.name]; return; }
      session.invites[friend.name] = 'joined';
      addPlayer(friend.name);
      ui.notify(`${friend.name} accepted your invite.`);
    });
    render();
  }

  function newCode() {
    if (session.testing || session.countdown || session.launched) return;
    session.code = randomCode();
    session.expiresAt = Date.now() + CODE_LIFETIME_MS;
    session.requests = [];
    Object.keys(session.invites).forEach((name) => { if (session.invites[name] !== 'joined') session.invites[name] = 'cancelled'; });
    ui.notify('New code ready. The old code no longer works; racers already here stay.');
    render();
  }

  function closeLobby() {
    ui.openModal({
      heading: 'Close this lobby?',
      body: h('p', {}, 'Everyone in the lobby will be disconnected and the code will stop working.'),
      actions: [
        ui.raceButton({ label: 'KEEP LOBBY', onClick: () => ui.closeModal() }),
        ui.raceButton({ label: 'CLOSE LOBBY', variant: 'red', onClick: () => { ui.closeModal(); leave(); } }),
      ],
    });
  }

  function leave() {
    clearTimers();
    session = { phase: 'idle' };
    section = 'lobbies';
    setModLock(false);
    render();
  }

  function startRace() {
    if (!allReady() || session.countdown) return;
    session.countdown = COUNTDOWN_SECONDS;
    const tick = () => {
      if (!inLobby()) return;
      session.countdown -= 1;
      if (session.countdown > 0) later(1000, tick);
      else {
        session.launched = true;
        if (session.role === 'host') state.update('online', { previousOnlineSave: true });
        ui.notify('Race launched in the preview. Return to the lobby to try another setup.', 4200);
      }
    };
    later(1000, tick);
    render();
  }

  function requestJoin() {
    if (!state.get().rom.ready || session.phase !== 'idle') return;
    if (joinCode.length < CODE_LENGTH) {
      joinError = `Enter all ${CODE_LENGTH} characters of the code.`;
      render();
      return;
    }
    const lobby = demoLobbies.find((l) => l.code === joinCode);
    if (lobby) joinHost = lobby.name;
    if (lobby && lobby.revision !== romRevision()) {
      joinError = `That lobby races on ${versionName(lobby.revision)} and you have ${versionName(romRevision())} selected. Switch your Game ROM at the top of this page, then try again.`;
      render();
      return;
    }
    clearTimers();
    joinError = '';
    session = { phase: 'joining', code: joinCode };
    section = 'lobby';
    later(2200, () => {
      if (session.phase !== 'joining') return;
      if (session.code === MISSING_CODE) {
        session = { phase: 'idle' };
        section = 'play';
        joinError = 'No open lobby uses that code. Check it with your host; codes stop working when a lobby closes.';
        return;
      }
      joinCode = '';
      session = {
        phase: 'lobby', role: 'guest', code: session.code, host: joinHost,
        players: [{ id: 'p-host', name: joinHost, host: true, ready: true, ping: 42, saveReady: true }, me()],
        requests: [], invites: {}, countdown: 0,
        rules: { ...state.defaults.online.hostSettings, maximumRacers: 4 }, blocked: [],
      };
      setModLock(true);
      ui.notify(`${joinHost} let you in. Welcome to the lobby!`);
    });
    render();
  }

  function cancelJoin() {
    clearTimers();
    session = { phase: 'idle' };
    section = 'play';
    render();
  }

  // ------------------------------------------------------------ small pieces

  const updateOnline = (patch) => state.update('online', patch);
  const friendLabel = (friend) => friend.nickname || friend.name;
  const busy = () => !!(session.testing || session.countdown || session.launched);

  function card(title, description, ...children) {
    return h('section', { class: 'ol-card ol-feature' }, h('h2', {}, title),
      description ? h('p', { class: 'ol-soft' }, description) : null, ...children);
  }

  function selectField(label, value, choices, onChange, disabled = false) {
    const input = h('select', { 'data-fk': label, 'aria-label': label, disabled },
      ...choices.map((choice) => h('option', { value: String(choice) }, String(choice))));
    input.value = String(value);
    input.addEventListener('change', () => onChange(input.value));
    return h('label', { class: 'ol-field' }, h('span', {}, label), input);
  }

  // Text boxes are buttons that open the on-screen keyboard, named like the native text-entry
  // targets: "ENTER LOBBY NAME" ... "USE LOBBY NAME".
  function textField(label, value, onChange, maxLength = 48) {
    const caps = label.toUpperCase();
    return h('div', { class: 'ol-field' }, h('span', {}, label),
      ui.fieldButton({
        label, value, fk: label,
        onOpen: (typed) => ui.keyboard({
          heading: 'ENTER ' + caps, hint: caps, accept: 'USE ' + caps, value, typed, maxLength,
          onAccept: (text) => onChange(text.trim()),
        }),
      }));
  }

  function rangeField(label, value, min, max, onChange, disabled = false) {
    const output = h('output', {}, value);
    const input = h('input', { type: 'range', min, max, value, disabled, 'data-fk': label, 'aria-label': label });
    input.addEventListener('input', () => { output.value = input.value; });
    input.addEventListener('change', () => onChange(Number(input.value)));
    return h('label', { class: 'ol-field' }, h('span', {}, label, ' ', output), input);
  }

  function confirmAction(heading, description, label, accept) {
    ui.openModal({ heading, body: h('p', {}, description), actions: [
      ui.raceButton({ label: 'CANCEL', onClick: ui.closeModal }),
      ui.raceButton({ label, variant: 'red', onClick: () => { ui.closeModal(); accept(); } }),
    ] });
  }

  function editFriend(friend, patch) {
    updateOnline({ friends: online().friends.map((f) => f.name === friend.name ? { ...f, ...patch } : f) });
  }

  function manageFriend(friend) {
    // SET FRIEND NICKNAME saves as soon as it is accepted, like the native keyboard.
    const nicknameField = () => ui.fieldButton({
      label: 'Nickname', value: friend.nickname || '', placeholder: 'No nickname', className: 'is-dialog', fk: 'friend-nickname',
      onOpen: (typed) => ui.keyboard({
        heading: 'SET FRIEND NICKNAME', hint: 'FRIEND NICKNAME', accept: 'SAVE NICKNAME', allowEmpty: true,
        value: friend.nickname || '', typed, maxLength: 24,
        onAccept: (text) => {
          friend = { ...friend, nickname: text.trim() };
          editFriend(friend, { nickname: friend.nickname });
          const modal = document.getElementById('modal');
          modal.querySelector('#modal-heading').textContent = friendLabel(friend);
          const next = nicknameField();
          modal.querySelector('.field-button.is-dialog').replaceWith(next);
          next.focus();
          ui.notify(friend.nickname ? 'Friend nickname saved.' : 'Friend nickname cleared.');
        },
      }),
    });
    ui.openModal({ heading: friendLabel(friend), body: [h('p', {}, 'Racer: ' + friend.name),
      h('div', { class: 'ol-field' }, h('span', {}, 'Nickname (only visible to you)'), nicknameField())], actions: [
      ui.raceButton({ label: friend.blocked ? 'UNBLOCK' : 'BLOCK', onClick: () => {
        ui.closeModal();
        if (friend.blocked) editFriend(friend, { blocked: false });
        else confirmAction('Block racer?', 'Their presence, invites and hosted lobbies will be hidden.', 'BLOCK', () => editFriend(friend, { blocked: true }));
      } }),
      ui.raceButton({ label: 'REMOVE', variant: 'red', onClick: () => {
        ui.closeModal(); confirmAction('Remove friend?', 'Exchange another Friend Code to add this racer again.', 'REMOVE',
          () => updateOnline({ friends: online().friends.filter((f) => f.name !== friend.name) }));
      } }),
      ui.raceButton({ label: 'CLOSE', onClick: ui.closeModal }),
    ] });
  }

  function hostSettingsView() {
    const rules = online().hostSettings;
    const change = (patch) => updateOnline({ hostSettings: { ...online().hostSettings, ...patch } });
    return card('Host settings', 'These settings are fixed when you create a lobby.',
      selectField('Menu ownership', rules.menuOwnership, ['Host guides menus until character select', 'Host controls shared menus', 'Every assigned port'], (v) => change({ menuOwnership: v })),
      selectField('Maximum racers', rules.maximumRacers, [2, 3, 4], (v) => change({ maximumRacers: Number(v) })),
      h('p', { class: 'ol-soft' }, 'Races and minigames support 2–4 racers. Adventure supports 2.'),
      selectField('Synchronization', rules.synchronization, ['Rollback', 'Lockstep'], (v) => change({ synchronization: v })),
      rules.synchronization === 'Rollback'
        ? rangeField('Rollback window (frames)', rules.rollbackWindow, 2, 20, (v) => change({ rollbackWindow: v }))
        : h('p', { class: 'ol-error' }, 'Lockstep suits very stable, low-latency connections. Network variation can cause stalls.'),
      switchRow('Automatic input delay', 'Measures the slowest racer before launch. Delay stays fixed during the race.', rules.automaticDelay,
        (v) => change({ automaticDelay: v }), 'automatic-delay'),
      rangeField('Input delay (frames)', rules.inputDelay, 0, 9, (v) => change({ inputDelay: v }), rules.automaticDelay),
      !rules.automaticDelay ? button('Use automatic input delay', () => change({ automaticDelay: true })) : null,
      switchRow('Record deterministic replay', 'Keep a replay of the shared race.', rules.recordReplay, (v) => change({ recordReplay: v }), 'record-replay'));
  }

  function overlaysView() {
    const o = online().overlays;
    const change = (patch) => updateOnline({ overlays: { ...online().overlays, ...patch } });
    const preview = h('div', { class: 'ol-overlay-preview', 'aria-label': 'Overlay preview' },
      h('span', { class: 'ol-preview-label' }, 'In-game preview'),
      o.network ? h('div', { class: 'ol-overlay-sample at-' + o.networkPosition.toLowerCase().replace(' ', '-') },
        h('strong', {}, 'NETWORK · 42 ms'), o.detail !== 'Compact' ? h(o.singleRow ? 'span' : 'div', {}, '  Jitter 3 ms · Delay 2f') : null,
        o.detail === 'Detailed' ? h(o.singleRow ? 'span' : 'div', {}, '  Loss 0% · Rollback 0 · Lead 1.2f') : null) : null,
      o.controller ? h('div', { class: 'ol-overlay-sample is-input at-' + o.controllerPosition.toLowerCase().replace(' ', '-') },
        h('strong', {}, 'CONTROLLER'), h('div', {}, 'Local: A + →'), h('div', {}, 'Player 1 committed: A + →')) : null);
    return card('Online overlays', 'Presentation settings stay available during a session.',
      switchRow('Show networking overlay', 'See connection quality while racing.', o.network, (v) => change({ network: v }), 'network-overlay'),
      ...(o.network ? [selectField('Network overlay position', o.networkPosition, POSITIONS, (v) => change({ networkPosition: v })),
        selectField('Network overlay detail', o.detail, ['Compact', 'Standard', 'Detailed'], (v) => change({ detail: v })),
        switchRow('Network details on one row', 'Use a compact horizontal display.', o.singleRow, (v) => change({ singleRow: v }), 'network-single-row')] : []),
      switchRow('Show controller input overlay', 'Compare your local input with the input committed by Player 1.', o.controller, (v) => change({ controller: v }), 'controller-overlay'),
      o.controller ? selectField('Controller overlay position', o.controllerPosition, POSITIONS, (v) => change({ controllerPosition: v })) : null,
      preview);
  }

  function openLobbiesView() {
    const available = demoLobbies.filter((lobby) => online().friends.some((f) => f.name === lobby.name && f.status === 'online' && !f.blocked));
    const join = (lobby) => { joinCode = lobby.code; joinHost = lobby.name; requestJoin(); };
    const ready = state.get().rom.ready;
    return card('Open lobbies', 'Find rooms hosted by friends. Invitations appear first.',
      ...available.map((lobby) => {
        const invited = lobby.invited && online().allowInvites && !online().dismissedInvites.includes(lobby.code);
        const matches = romRevision() === lobby.revision;
        const needed = versionName(lobby.revision);
        return h('article', { class: 'ol-room' + (invited ? ' is-invited' : '') },
          h('span', { class: 'ol-soft-tag' }, invited ? 'Lobby invitation' : 'Friend lobby'),
          h('h3', {}, friendLabel(online().friends.find((f) => f.name === lobby.name))),
          h('p', {}, `${lobby.players} / ${lobby.max} racers · ${lobby.ping} ms · Rollback · ROM ${needed}`),
          ready && !matches ? h('p', { class: 'ol-rom-mismatch' },
            `This lobby races on ${needed}. You have ${versionName(romRevision())} selected.`) : null,
          h('div', { class: 'ol-actions' },
            ready && !matches
              ? button(romFor(lobby.revision) ? `Switch to ${needed}` : `Add a ${needed} ROM…`, () => switchTo(lobby.revision, 'join-lobby-' + lobby.name), { tone: 'is-strong', fk: 'switch-rom-' + lobby.name })
              : button(invited ? 'Accept and join' : 'Request to join', () => join(lobby), { tone: 'is-go', disabled: !ready, fk: 'join-lobby-' + lobby.name, reason: 'Choose a Game ROM first, at the top of this page.' }),
            invited ? button('Decline', () => updateOnline({ dismissedInvites: [...online().dismissedInvites, lobby.code] })) : null));
      }),
      !available.length ? h('p', {}, 'No friends are hosting an open lobby right now.') : null,
      !ready ? button('Choose a ROM before joining…', openRomBrowser, { tone: 'is-strong', fk: 'lobbies-choose-rom' }) : null);
  }

  function friendCodesView() {
    const o = online();
    const generate = () => {
      const code = 'DKR-' + (randomCode() + randomCode()).slice(0, 8);
      updateOnline({ generatedCodes: [...o.generatedCodes, { code, lifetime: o.friendCodeLifetime,
        expiresAt: o.friendCodeLifetime === 'Timed' ? Date.now() + o.friendCodeMinutes * 60000 : 0 }] });
    };
    return card('Share a Friend Code', 'Friend Codes add trusted racers. Quick Join codes open a lobby.',
      selectField('Code lifetime', o.friendCodeLifetime, ['Permanent', 'One use', 'Timed'], (v) => updateOnline({ friendCodeLifetime: v })),
      o.friendCodeLifetime === 'Timed' ? rangeField('Active minutes', o.friendCodeMinutes, 1, 43200, (v) => updateOnline({ friendCodeMinutes: v })) : null,
      button('Generate Friend Code', generate, { tone: 'is-strong', fk: 'generate-friend-code' }),
      ...o.generatedCodes.map((entry) => h('div', { class: 'ol-room' },
        h('strong', { class: 'ol-mono' }, entry.code), h('p', { class: 'ol-soft' }, entry.lifetime +
          (entry.expiresAt ? entry.expiresAt <= Date.now() ? ' · Expired' : ' · ' + formatExpiry(entry.expiresAt - Date.now()) + ' remaining' : '')),
        h('div', { class: 'ol-actions' },
          button('Copy code', () => copy(entry.code, 'Friend Code copied.'), { disabled: !!entry.expiresAt && entry.expiresAt <= Date.now(), reason: 'This code has expired. Generate a new one.' }),
          button('Revoke', () => updateOnline({ generatedCodes: online().generatedCodes.filter((c) => c.code !== entry.code) }), { tone: 'is-danger' })))),
      button('Copy friend connection diagnostics', () => copy('DKR-R mockup: presence available; transport simulated; no network measurements.', 'Diagnostics copied. No names, codes or addresses included.')));
  }

  function connectionTest() {
    if (!inLobby() || session.role !== 'host' || busy()) return;
    session.testing = true; session.testRemaining = 7; session.testResults = null;
    const tick = () => {
      if (!inLobby()) return;
      session.testRemaining -= 1;
      if (session.testRemaining) later(1000, tick);
      else {
        session.testing = false;
        session.testResults = session.players.map((p) => ({ name: p.name, ping: p.ping, score: p.you ? 10 : p.ping < 80 ? 9 : 7 }));
        showTestResults();
      }
    };
    later(1000, tick); render();
  }

  function showTestResults() {
    const results = session.testResults || [];
    const score = Math.min(...results.map((p) => p.score));
    ui.openModal({ heading: 'Session pre-flight results', body: [
      h('h3', {}, `Overall connection: ${score} / 10`),
      h('p', {}, score > 7 ? 'Excellent — best online experience' : 'Average — playable, but hitches may occur'),
      ...results.map((p) => h('p', {}, `${p.name}: ${p.score} / 10 · P95 ${p.ping} ms · Jitter ${p.ping ? 3 : 0} ms · Loss 0% · Late 0% · Queues drained`)),
      h('p', {}, 'Preview results are simulated. The launcher tests session traffic for seven seconds; this is not a CPU or GPU benchmark.'),
    ], actions: [ui.raceButton({ label: 'CLOSE', onClick: ui.closeModal })] });
  }

  function connectionView() {
    const rules = session.rules;
    return card('Connection status', 'Quick Join · Host is Player 1. Preview measurements are simulated.',
      h('div', { class: 'ol-metrics' }, ...[
        ['Synchronization', rules.synchronization], ['Input delay', (rules.automaticDelay ? 2 : rules.inputDelay) + ' frames'],
        ['App RTT', '42 ms'], ['Jitter', '3 ms'], ['Expired probes', '0%'], ['Input stalls', '0'],
      ].map(([label, value]) => h('div', {}, h('small', {}, label), h('strong', {}, value)))),
      h('p', { class: 'ol-soft' }, session.launched ? 'Determinism verified · Prediction lead 1.2 frames · Corrections 0' : 'Waiting for the race to start.'),
      rules.recordReplay ? h('p', {}, 'Deterministic replay recording enabled.') : null,
      session.role === 'host' ? button('Test session connection', connectionTest, { disabled: busy(), reason: 'Not while the connection test or countdown is running.' }) : null,
      session.testing ? h('p', { role: 'status' }, `Testing real session load… ${session.testRemaining} seconds`) : null,
      session.testResults ? button('View test results', showTestResults) : null);
  }

  function showGuide() {
    const remember = h('input', { type: 'checkbox' }); remember.checked = true;
    ui.openModal({ heading: 'Welcome to DKR-R Online', body: [
      h('p', {}, 'Host a private room, share its five-character code, approve requests and ready up. Player 1 starts the race for everyone.'),
      h('p', {}, 'Open lobbies lists rooms hosted by friends. Friend Codes add racers to your list; lobby invitations grant one admission and expire after five minutes.'),
      h('p', {}, 'Host Settings controls racer limits, shared menus, synchronization and input delay. Rollback and automatic delay are the defaults.'),
      h('p', {}, 'Every racer needs the same ROM revision and gameplay settings. The host supplies a separate online Adventure save; single-player progress is kept.'),
      h('p', {}, 'Online is in beta. This mockup simulates lobbies, friends and connection results locally.'),
      h('label', {}, remember, ' Do not show this guide again'),
    ], actions: [ui.raceButton({ label: 'GOT IT', onClick: () => { updateOnline({ guideAcknowledged: remember.checked }); ui.closeModal(); } })] });
  }

  async function pasteCode() {
    try { joinCode = (await navigator.clipboard.readText()).toUpperCase().replace(/[^A-Z2-9]/g, '').slice(0, CODE_LENGTH); render(); }
    catch { ui.notify('Clipboard unavailable. Enter the code with the on-screen keyboard.'); }
  }

  const codeFilter = (text) => [...text.toUpperCase()].filter((ch) => CODE_CHARS.includes(ch)).join('');

  // ENTER QUICK JOIN CODE: the code alphabet only, no SPACE. USE CODE fills the tiles and hands
  // focus to REQUEST TO JOIN.
  function codeKeyboard(typed = '') {
    ui.keyboard({
      heading: 'ENTER QUICK JOIN CODE', hint: 'FIVE CHARACTERS',
      help: 'Use the D-pad to choose each character and press A to enter it. Quick Join avoids I, O, 1 and 0 so codes are easy to read.',
      keys: CODE_CHARS, value: joinCode, typed, maxLength: CODE_LENGTH, tiles: CODE_LENGTH, filter: codeFilter,
      space: false, letterCase: false, paste: true, accept: 'USE CODE',
      validate: (text) => (text.length < CODE_LENGTH ? `Enter all ${CODE_LENGTH} characters of the code.` : ''),
      onAccept: (code) => {
        joinCode = code;
        joinError = '';
        render();
        root.querySelector('[data-fk="request-join"]:not(:disabled)')?.focus();
      },
    });
  }

  // ADD A DKR-R FRIEND: "DKR-" stays put; SEND REQUEST sends straight from the keyboard.
  function friendCodeKeyboard(typed = '') {
    ui.keyboard({
      heading: 'ADD A DKR-R FRIEND', hint: 'DKR-XXXXXXXX',
      help: 'Paste the secure Friend Code, or enter its eight characters with the D-pad and A button. The request is delivered when both racers are online.',
      keys: CODE_CHARS, value: 'DKR-', typed, prefix: 'DKR-', maxLength: 12, mono: true,
      filter: (text) => 'DKR-' + text.toUpperCase().replace(/^DKR-?/, '').replace(/[^A-Z0-9]/g, '').slice(0, 8),
      space: false, letterCase: false, paste: true, accept: 'SEND REQUEST',
      validate: friendCodeProblem,
      onAccept: sendFriendRequest,
    });
  }

  function friendCodeProblem(code) {
    const o = online();
    const normalized = code.replaceAll('-', '');
    if (!/^DKR-[A-Z0-9]{8}$/.test(code)) return 'Enter all eight characters, such as DKR-7K2Q94XM.';
    if ([o.friendCode, ...o.generatedCodes.map((c) => c.code)].some((c) => c.replaceAll('-', '') === normalized)) return 'That is your own friend code.';
    if (o.friends.some((f) => f.code?.replaceAll('-', '') === normalized)) return 'That racer is already in your friends or requests.';
    return '';
  }

  function sendFriendRequest(code) {
    state.update('online', { friends: [...online().friends, { name: 'Racer ' + code.slice(-4), code, status: 'sent', delivery: 'Waiting for the racer to be reachable' }] });
    ui.notify('Friend request sent.');
  }

  // reason: shown when the D-pad rests on the button while it is disabled (controller.js).
  function button(label, onClick, { tone = '', disabled = false, fk, ariaLabel, title, reason } = {}) {
    return h('button', {
      type: 'button', class: 'ol-btn' + (tone ? ' ' + tone : ''), disabled, 'data-fk': fk,
      'aria-label': ariaLabel, title, onclick: onClick, 'data-disabled-reason': disabled ? reason : undefined,
    }, label);
  }

  function bigButton(label, onClick, { variant, disabled = false, fk, reason } = {}) {
    const btn = ui.raceButton({ label, onClick, variant, disabled });
    btn.classList.add('ol-cta');
    if (fk) btn.dataset.fk = fk;
    if (disabled && reason) btn.dataset.disabledReason = reason;
    return btn;
  }

  function avatar(name, cls = '') {
    return h('span', { class: 'ol-avatar ' + cls, 'aria-hidden': 'true', style: `--hue:${hue(name)}` }, (name || '?')[0].toUpperCase());
  }

  function hue(name) {
    let value = 0;
    for (const ch of name || '') value = (value * 31 + ch.charCodeAt(0)) % 360;
    return value;
  }

  function switchRow(title, description, checked, onChange, fk, disabled = false) {
    const input = h('input', { type: 'checkbox', role: 'switch', 'data-fk': fk, disabled });
    input.checked = !!checked;
    input.addEventListener('change', () => onChange(input.checked));
    return h('label', { class: 'ol-switch' }, input,
      h('span', {}, h('strong', {}, title), h('small', {}, description)));
  }

  function codeTiles(code, { live = false } = {}) {
    return h('span', { class: 'ol-code-tiles', 'aria-hidden': 'true' },
      Array.from({ length: CODE_LENGTH }, (_, i) => h('span', {
        class: 'ol-code-tile' + (live && i === Math.min(code.length, CODE_LENGTH - 1) ? ' is-caret' : '') + (code[i] ? ' is-filled' : ''),
      }, code[i] || '')));
  }

  function formatExpiry(ms) {
    const minutes = Math.max(0, Math.ceil(ms / 60000));
    const hours = Math.floor(minutes / 60);
    return hours ? `${hours} h ${String(minutes % 60).padStart(2, '0')} min` : `${minutes} min`;
  }

  function pingBars(ping) {
    const level = ping < 60 ? 3 : ping < 120 ? 2 : 1;
    return h('span', { class: 'ol-ping', title: `${ping} ms round trip` },
      h('span', { class: 'ol-bars', 'data-level': level, 'aria-hidden': 'true' }, h('i'), h('i'), h('i')),
      `${ping} ms`);
  }

  // ------------------------------------------------------------ session views

  function idleView() {
    // The tiles are a button: A opens ENTER QUICK JOIN CODE. A real keyboard can still type
    // straight into them, and Enter asks to join once all five are there.
    const tiles = codeTiles(joinCode, { live: true });
    const error = h('p', { class: 'ol-error', id: 'ol-join-error', role: 'alert' }, joinError);
    const codeLabel = () => `Lobby code: ${joinCode ? joinCode.split('').join(' ') : 'empty'}. Opens the on-screen keyboard.`;
    const codeButton = h('button', {
      type: 'button', class: 'ol-code-field' + (joinError ? ' has-error' : ''), 'data-fk': 'join-code',
      'aria-haspopup': 'dialog', 'aria-label': codeLabel(), 'aria-describedby': 'ol-join-help ol-join-error',
    }, tiles);
    const typeCode = (next) => {
      joinCode = next;
      tiles.replaceChildren(...codeTiles(joinCode, { live: true }).childNodes);
      codeButton.setAttribute('aria-label', codeLabel());
      if (joinError) { joinError = ''; error.textContent = ''; codeButton.classList.remove('has-error'); }
    };
    codeButton.addEventListener('click', () => codeKeyboard());
    codeButton.addEventListener('keydown', (event) => {
      if (event.ctrlKey || event.metaKey || event.altKey) return;
      if (event.key === 'Backspace') { event.preventDefault(); typeCode(joinCode.slice(0, -1)); return; }
      if (event.key === 'Enter' && joinCode.length === CODE_LENGTH) { event.preventDefault(); requestJoin(); return; }
      if (event.key.length === 1 && event.key !== ' ') {
        event.preventDefault();
        typeCode(codeFilter(joinCode + event.key).slice(0, CODE_LENGTH));
      }
    });

    return [
      h('div', { class: 'ol-choices' },
        h('section', { class: 'ol-choice is-host', 'aria-labelledby': 'ol-host-title' },
          h('span', { class: 'ol-choice-tag' }, 'Host'),
          h('h2', { id: 'ol-host-title' }, 'Start a lobby'),
          h('p', {}, `Share a 5-character code with up to ${online().hostSettings.maximumRacers - 1} ${online().hostSettings.maximumRacers === 2 ? 'friend' : 'friends'}.`),
          textField('Lobby name', online().roomName, (v) => updateOnline({ roomName: v || 'DKR-R Grand Prix' })),
          selectField('Online Adventure save', online().saveSeed, ['Copy my single-player save', 'Start with a fresh save', 'Continue with previous session'], (v) => updateOnline({ saveSeed: v })),
          h('p', { class: 'ol-soft' }, online().saveSeed === 'Continue with previous session'
            ? online().previousOnlineSave ? 'Continue the last host-owned online Adventure save.' : 'No previous online session save is available yet.'
            : online().saveSeed === 'Start with a fresh save' ? 'Start a new online Adventure. Your single-player save is kept.' : 'Copy your progress into a separate online save. Your single-player file is kept.'),
          button('Host settings', () => { section = 'settings'; render(); }, { fk: 'host-settings' }),
          bigButton('CREATE LOBBY', createLobby, { variant: 'green', fk: 'create-lobby', disabled: !state.get().rom.ready || (online().saveSeed === 'Continue with previous session' && !online().previousOnlineSave),
            reason: state.get().rom.ready ? 'There is no previous online session save yet. Pick another Online Adventure save.' : 'Choose a Game ROM first, at the top of this page.' })),
        h('div', { class: 'ol-or', 'aria-hidden': 'true' }, h('span', { class: 'ol-or-sign' }, 'or')),
        h('section', { class: 'ol-choice is-join', 'aria-labelledby': 'ol-join-title' },
          h('span', { class: 'ol-choice-tag' }, 'Join'),
          h('h2', { id: 'ol-join-title' }, 'Join a friend'),
          h('p', { id: 'ol-join-help' }, 'Type the code your host sent you.'),
          codeButton,
          error,
          h('div', { class: 'ol-actions' }, button('Paste code', pasteCode, { fk: 'paste-code' })),
          bigButton('REQUEST TO JOIN', () => { joinHost = 'Banjo64'; requestJoin(); }, { fk: 'request-join', disabled: !state.get().rom.ready, reason: 'Choose a Game ROM first, at the top of this page.' }))),
      h('ol', { class: 'ol-steps', 'aria-label': 'How online races work' },
        ...[['Open', 'The host creates a lobby'], ['Share', 'Friends type the code'], ['Approve', 'The host lets them in'], ['Race', 'Everyone readies up']]
          .map(([title, text], i) => h('li', {}, h('span', { class: 'ol-step-num' }, i + 1), h('span', {}, h('strong', {}, title), text)))),
    ];
  }

  function joiningView() {
    return [h('section', { class: 'ol-waiting', role: 'status' },
      h('div', { class: 'ol-waiting-lights', 'aria-hidden': 'true' }, h('i'), h('i'), h('i')),
      h('h2', {}, 'Knocking on the door…'),
      h('p', {}, 'Waiting for the host to let you in with code ', h('strong', { class: 'ol-mono' }, session.code), '.'),
      h('p', { class: 'ol-soft' }, 'The host has to approve every request. This usually takes a few seconds.'),
      button('Cancel request', cancelJoin, { fk: 'cancel-join' }))];
  }

  function lobbyView() {
    const host = session.role === 'host';
    const you = session.players.find((p) => p.you);
    const readyCount = session.players.filter((p) => p.ready).length;
    const total = session.players.length;
    const maximum = session.rules.maximumRacers;

    const top = h('div', { class: 'ol-lobby-top' },
      h('div', { class: 'ol-lobby-title' },
        h('span', { class: 'ol-live' }, host ? 'Hosting' : 'Joined'),
        h('h2', {}, host ? session.name : `${session.host}'s lobby`),
        h('p', {}, host
          ? `${total} of ${maximum} racers. ${session.locked ? 'Locked: nobody new can ask to join.' : 'Friends can join with the code.'}`
          : 'The host starts the race when everyone is ready.')),
      host ? h('div', { class: 'ol-plate' },
        h('span', { class: 'ol-plate-label' }, 'Lobby code'),
        h('span', { class: 'ol-plate-code' },
          codeTiles(session.code), h('span', { class: 'ol-visually-hidden' }, session.code.split('').join(' '))),
        h('div', { class: 'ol-plate-actions' },
          button('Copy code', () => copy(session.code, `Code ${session.code} copied. Share it only with friends.`), { tone: 'is-strong', fk: 'copy-code' }),
          button('New code', newCode, { fk: 'new-code', disabled: busy(), title: 'Stops the old code working. Racers already here stay.', reason: 'Not while the connection test or countdown is running.' })),
        h('span', { class: 'ol-expiry' }, 'Expires in ', h('span', { class: 'ol-expiry-time' }, formatExpiry(session.expiresAt - Date.now()))))
        : h('div', { class: 'ol-plate is-guest' },
          h('span', { class: 'ol-plate-label' }, 'Lobby code'),
          h('span', { class: 'ol-plate-code' },
            codeTiles(session.code), h('span', { class: 'ol-visually-hidden' }, session.code.split('').join(' ')))));

    const gate = host && session.requests.length ? h('section', { class: 'ol-gate', 'aria-label': 'Join requests' },
      h('h3', {}, 'Wants to join'),
      ...session.requests.map((r) => h('div', { class: 'ol-gate-row' },
        avatar(r.name),
        h('span', { class: 'ol-gate-name' }, h('strong', {}, r.name), h('small', {}, hasSpace() ? 'Used your lobby code' : 'Lobby is full')),
        button('Let in', () => answerRequest(r.id, true), { tone: 'is-go', disabled: !hasSpace() || !r.compatible, fk: 'approve-' + r.id, reason: r.compatible ? 'The lobby is full or locked.' : 'Their game does not match yours.' }),
        button('Decline', () => answerRequest(r.id, false), { disabled: busy(), fk: 'decline-' + r.id, reason: 'Not while the connection test or countdown is running.' }),
        button('Block for session', () => { session.blocked.push(r.name); answerRequest(r.id, false); }, { disabled: busy(), fk: 'block-' + r.id, reason: 'Not while the connection test or countdown is running.' })))) : null;

    const grid = h('ol', { class: 'ol-grid', 'aria-label': 'Starting grid' },
      ...Array.from({ length: maximum }, (_, i) => {
        const p = session.players[i];
        if (!p) {
          return h('li', { class: 'ol-slot is-empty' },
            h('span', { class: 'ol-slot-num' }, 'P' + (i + 1)),
            h('span', { class: 'ol-slot-body' }, h('strong', {}, 'Open slot'),
              h('small', {}, host ? (session.locked ? 'Lobby locked' : 'Waiting for a friend') : 'Waiting for racers')));
        }
        const isHost = host ? p.you : p.host;
        return h('li', { class: 'ol-slot' + (p.ready ? ' is-ready' : '') + (p.you ? ' is-you' : '') },
          h('span', { class: 'ol-slot-num' }, 'P' + (i + 1)),
          avatar(p.name),
          h('span', { class: 'ol-slot-body' },
            h('strong', {}, p.name, isHost ? h('span', { class: 'ol-tag' }, 'Host') : null, p.you ? h('span', { class: 'ol-tag is-you' }, 'You') : null),
            h('span', { class: 'ol-slot-meta' },
              h('span', { class: 'ol-ready-text' }, session.launched ? 'Loaded' : p.ready ? 'Ready' : 'Not ready yet'),
              h('span', { class: 'ol-soft' }, p.saveReady ? 'Online save verified' : 'Verifying online save…'),
              p.you ? h('span', { class: 'ol-ping' }, 'This PC') : pingBars(p.ping))),
          host && !p.you ? button('×', () => removePlayer(p.id), { tone: 'is-icon', disabled: busy(), ariaLabel: `Remove ${p.name}`, title: `Remove ${p.name}`, fk: 'remove-' + p.id, reason: 'Not while the connection test or countdown is running.' }) : null);
      }));

    let hint;
    if (total < 2) hint = host ? 'You need at least one friend to start.' : 'Waiting for more racers.';
    else if (readyCount < total) hint = `${readyCount} of ${total} racers ready`;
    else hint = host ? 'Everyone is ready. Start when you like!' : 'Everyone is ready. The host is starting…';

    const lights = h('div', { class: 'ol-lights', 'aria-hidden': 'true' },
      ...Array.from({ length: maximum }, (_, i) => {
        const p = session.players[i];
        return h('i', { class: !p ? 'is-off' : p.ready ? 'is-go' : 'is-wait' });
      }));

    const readyButton = bigButton(you.ready ? 'NOT READY' : 'READY TO RACE',
      () => { setReady('me', !you.ready); render(); }, { variant: you.ready ? '' : 'green', fk: 'ready', disabled: busy() || !you.saveReady,
        reason: busy() ? 'Not while the connection test or countdown is running.' : 'Still checking your online save. This takes a moment.' });
    readyButton.setAttribute('aria-pressed', String(you.ready));
    const startBar = h('div', { class: 'ol-start' },
      lights,
      h('p', { class: 'ol-start-hint', role: 'status' }, hint),
      h('div', { class: 'ol-start-actions' },
        readyButton,
        host ? bigButton('START RACE', startRace, { variant: 'red', disabled: !allReady() || busy(), fk: 'start',
          reason: busy() ? 'Not while the connection test or countdown is running.' : total < 2 ? 'You need at least one friend in the lobby.' : 'Every racer has to be ready first.' }) : null));

    const footer = h('div', { class: 'ol-lobby-footer' },
      host ? switchRow('Lock lobby', 'Nobody new can ask to join. Racers already here stay.', session.locked,
        (value) => { session.locked = value; if (value) session.requests = []; render(); }, 'lock', busy()) : h('span'),
      button(host ? 'Close lobby' : 'Leave lobby', host ? closeLobby : leave, { tone: 'is-danger', fk: 'leave' }));

    const panel = h('section', { class: 'ol-lobby', 'aria-labelledby': 'ol-lobby-heading' }, top,
      h('p', { class: 'ol-soft' }, `${session.rules.synchronization} · ${session.rules.automaticDelay ? 'Automatic input delay' : session.rules.inputDelay + ' frame delay'} · ${session.rules.menuOwnership}`),
      gate, grid,
      host ? h('div', { class: 'ol-actions' }, button('Test session connection', connectionTest, { disabled: busy(), fk: 'test-connection', reason: 'Not while the connection test or countdown is running.' }),
        button('Invite friends', () => { section = 'friends'; render(); }, { disabled: !hasSpace(), fk: 'invite-friends', reason: 'The lobby is full or locked.' })) : null,
      session.testing ? h('p', { class: 'ol-test-status', role: 'status' }, `Testing session traffic… ${session.testRemaining} seconds. Ready, Start and lobby changes are paused.`) : null,
      session.testResults ? button('View test results', showTestResults) : null,
      session.launched ? h('div', { class: 'ol-room' }, h('h3', {}, 'Race running · Preview'),
        h('p', {}, 'All racers loaded. This preview does not start the game.'),
        button('Return to lobby', () => {
          session.launched = false;
          session.players.forEach((p) => { p.ready = false; if (!p.you) later(1600, () => setReady(p.id, true)); });
          render();
        })) : startBar,
      footer);
    panel.querySelector('h2').id = 'ol-lobby-heading';
    if (session.countdown > 0) {
      panel.append(h('div', { class: 'ol-countdown', role: 'alert' },
        h('span', { class: 'ol-countdown-label' }, 'DKR-R Online starting in'),
        h('span', { class: 'ol-countdown-num' }, session.countdown),
        h('div', { class: 'ol-lights is-big', 'aria-hidden': 'true' },
          ...Array.from({ length: COUNTDOWN_SECONDS }, (_, i) => h('i', { class: i < COUNTDOWN_SECONDS - session.countdown ? 'is-on' : 'is-off' })))));
    }
    return [panel];
  }

  function checklistView() {
    const rom = state.get().rom;
    const codes = state.get().mods.magicCodes || [];
    const o = online();
    const profile = h('select', { 'data-fk': 'profile', id: 'ol-profile' },
      ...PROFILES.map((name) => h('option', { value: name }, name)));
    profile.value = o.localProfile;
    profile.disabled = session.phase !== 'idle';
    profile.addEventListener('change', () => state.update('online', { localProfile: profile.value }));
    const locked = session.phase !== 'idle';
    return h('section', { class: 'ol-check', 'aria-labelledby': 'ol-check-title' },
      h('div', { class: 'ol-check-head' },
        h('h2', { id: 'ol-check-title' }, 'Before you race'),
        h('p', {}, 'Everyone in a lobby must match on these. DKR-R checks it for you when someone joins.')),
      h('ul', { class: 'ol-check-list' },
        h('li', { class: rom.ready ? 'is-ok' : 'is-bad' },
          h('span', { class: 'ol-check-icon', 'aria-hidden': 'true' }),
          h('span', {}, h('strong', {}, 'Game ROM'), rom.ready
            ? `${versionName(romRevision())} selected. Friends need ${versionName(romRevision())} too.`
            : 'Not loaded yet'),
          rom.ready
            ? (locked ? h('span', { class: 'ol-soft-tag' }, 'Locked in lobby') : h('span', { class: 'ol-soft-tag' }, 'Switch at the top of this page'))
            : button('Choose ROM…', openRomBrowser, { tone: 'is-strong', fk: 'check-choose-rom' })),
        h('li', { class: 'is-info' },
          h('span', { class: 'ol-check-icon', 'aria-hidden': 'true' }),
          h('span', {}, h('strong', {}, 'Magic Codes'), codes.length ? `${codes.length} on. Friends need the same ones.` : 'None on. Friends need none on too.'),
          locked ? h('span', { class: 'ol-soft-tag' }, 'Locked in lobby') : h('a', { class: 'ol-link', href: '#/mods' }, 'Change')),
        h('li', { class: 'is-info' },
          h('span', { class: 'ol-check-icon', 'aria-hidden': 'true' }),
          h('span', {}, h('strong', {}, 'Adventure save'), 'The host supplies a separate online save. Your single-player progress is kept.'))),
      h('label', { class: 'ol-profile', for: 'ol-profile' },
        h('span', {}, h('strong', {}, 'You race with'), 'Your controls stay yours, whatever player number you get.'),
        profile));
  }

  // ------------------------------------------------------------ side column

  function profileCard() {
    const o = online();
    const name = ui.fieldButton({
      label: 'Racer name', value: o.nickname, fk: 'nickname', disabled: session.phase !== 'idle',
      reason: 'Your racer name is fixed while you are in a lobby.',
      onOpen: (typed) => ui.keyboard({
        heading: 'ENTER RACER NAME', hint: 'RACER NAME', accept: 'USE RACER NAME', value: o.nickname, typed, maxLength: 24,
        onAccept: (text) => state.update('online', { nickname: text.trim().slice(0, 24) || 'Player' }),
      }),
    });
    return h('section', { class: 'ol-card ol-me', 'aria-label': 'Your online profile' },
      h('div', { class: 'ol-me-head' },
        avatar(o.nickname, 'is-large'),
        h('div', { class: 'ol-me-name' },
          h('span', { class: 'ol-me-label' }, 'Racer name'), name)),
      h('div', { class: 'ol-presence' + (o.appearOffline ? ' is-hidden' : '') },
        h('span', { class: 'ol-dot', 'aria-hidden': 'true' }),
        o.appearOffline ? 'Friends see you as offline' : inLobby() ? 'Online · in a lobby' : 'Online'),
      h('div', { class: 'ol-friend-code' },
        h('span', {}, h('small', {}, 'Your friend code'), h('strong', { class: 'ol-mono' }, o.friendCode)),
        button('Copy', () => copy(o.friendCode, 'Friend code copied.'), { fk: 'copy-friend-code' })),
      h('p', { class: 'ol-soft' }, 'Device identity: ' + o.identityLabel),
      section === 'profile' ? textField('Display name', o.displayName, (v) => updateOnline({ displayName: v || 'Player' }), 24) : null,
      section === 'profile' ? button('Save online profile', () => { updateOnline({ nickname: online().displayName }); ui.notify('Online profile saved.'); }) : null);
  }

  function friendsCard() {
    const o = online();
    const friends = o.friends || [];
    const setFriends = (list) => state.update('online', { friends: list });
    // Both boxes open keyboards: SEARCH DKR-R FRIENDS applies on SEARCH, ADD A DKR-R FRIEND sends
    // the request from the keyboard itself.
    const add = ui.fieldButton({ label: 'Add a friend', value: '', placeholder: 'DKR-XXXXXXXX', className: 'is-mono', fk: 'add-friend', onOpen: friendCodeKeyboard });
    const search = h('div', { class: 'ol-search' },
      ui.fieldButton({
        label: 'Search friends', value: friendSearch, placeholder: 'Search names or nicknames', fk: 'friend-search',
        onOpen: (typed) => ui.keyboard({
          heading: 'SEARCH DKR-R FRIENDS', hint: 'FRIEND NAME', accept: 'SEARCH', allowEmpty: true,
          help: 'Use the D-pad and A button to enter a racer’s name or nickname. Every letter and number is available; spaces and common name characters are included too.',
          value: friendSearch, typed, maxLength: 48,
          onAccept: (text) => { friendSearch = text.trim(); render(); },
        }),
      }),
      friendSearch ? button('×', () => { friendSearch = ''; render(); root.querySelector('[data-fk="friend-search"]')?.focus(); },
        { tone: 'is-icon', ariaLabel: 'Clear friend search', title: 'Clear search', fk: 'friend-search-clear' }) : null);
    const filtered = friends.filter((f) => !['pending', 'sent'].includes(f.status) &&
      (friendLabel(f) + ' ' + f.name).toLowerCase().includes(friendSearch.toLowerCase()) &&
      (friendFilter === 'All friends' || friendFilter === 'Blocked' && f.blocked ||
        !f.blocked && (friendFilter === 'Online' && f.status === 'online' || friendFilter === 'Offline' && f.status === 'offline')));
    filtered.sort((a, b) => {
      if (friendSort === 'Recently seen') return (b.lastSeen || 0) - (a.lastSeen || 0);
      if (friendSort === 'Online first') {
        const rank = (f) => f.blocked ? 2 : f.status === 'online' ? 0 : 1;
        if (rank(a) !== rank(b)) return rank(a) - rank(b);
      }
      return friendLabel(a).localeCompare(friendLabel(b)) * (friendSort === 'Name Z-A' ? -1 : 1);
    });
    const groups = [
      ['Friend requests', friends.filter((f) => f.status === 'pending')],
      ['Outgoing requests', friends.filter((f) => f.status === 'sent')],
      ['Your friends', filtered],
    ];
    const onlineCount = friends.filter((f) => f.status === 'online' && !f.blocked).length;
    const canInvite = inLobby() && session.role === 'host';

    function row(f) {
      let status, actions = [];
      if (f.status === 'pending') {
        status = 'Wants to be friends';
        actions = [
          button('Accept', () => setFriends(friends.map((x) => (x === f ? { ...x, status: 'online' } : x))), { tone: 'is-go', fk: 'accept-' + f.name }),
          button('×', () => setFriends(friends.filter((x) => x !== f)), { tone: 'is-icon', ariaLabel: `Decline ${f.name}`, title: 'Decline', fk: 'decline-' + f.name }),
          button('Block', () => editFriend(f, { blocked: true, status: 'offline' })),
        ];
      } else if (f.blocked) {
        status = 'Blocked';
        actions = [button('Unblock', () => editFriend(f, { blocked: false }))];
      } else if (f.status === 'online') {
        const inviteState = canInvite ? session.invites[f.name] : null;
        status = inviteState === 'joined' ? 'In your lobby' : inviteState ? 'Invitation ' + inviteState : 'Online';
        if (canInvite && (!inviteState || ['cancelled', 'expired', 'declined'].includes(inviteState))) actions = [button('Invite', () => invite(f), { tone: 'is-strong', disabled: !hasSpace(), fk: 'invite-' + f.name, reason: 'The lobby is full or locked.' })];
        if (['sent', 'delivered'].includes(inviteState)) actions = [button('Cancel invite', () => { session.invites[f.name] = 'cancelled'; render(); }, { disabled: busy(), fk: 'cancel-invite-' + f.name, reason: 'Not while the connection test or countdown is running.' })];
      } else {
        status = f.status === 'sent' ? f.delivery || 'Request sent' : 'Offline';
        if (f.status === 'sent') actions = [button('Retry now', () => {
          editFriend(f, { delivery: 'Retrying…' });
          later(1200, () => editFriend(f, { delivery: 'Waiting for the racer to be reachable' }));
        }, { disabled: f.delivery === 'Retrying…' }), button('Cancel request', () => setFriends(friends.filter((x) => x !== f)))];
      }
      if (!['pending', 'sent'].includes(f.status)) actions.push(button('Manage', () => manageFriend(f), { fk: 'manage-friend-' + f.name }));
      return h('li', { class: 'ol-friend is-' + f.status },
        h('span', { class: 'ol-avatar-wrap' }, avatar(f.name), h('span', { class: 'ol-dot', 'aria-hidden': 'true' })),
        h('span', { class: 'ol-friend-text' }, h('strong', {}, friendLabel(f)), h('small', {}, status)),
        ...actions);
    }

    return h('section', { class: 'ol-card', 'aria-labelledby': 'ol-friends-title' },
      h('div', { class: 'ol-card-head' },
        h('h2', { id: 'ol-friends-title' }, 'Friends'),
        h('span', { class: 'ol-count' }, `${onlineCount} online`)),
      canInvite ? null : h('p', { class: 'ol-soft ol-invite-hint' }, 'Create a lobby to invite friends with one click.'),
      canInvite ? h('p', { class: 'ol-soft' }, 'Invites grant one admission and expire after five minutes. Locking the lobby prevents new invitations.') : null,
      h('div', { class: 'ol-friend-toolbar' }, search,
        selectField('Friend filter', friendFilter, ['All friends', 'Online', 'Offline', 'Blocked'], (v) => { friendFilter = v; render(); }),
        selectField('Sort friends', friendSort, ['Online first', 'Name A-Z', 'Name Z-A', 'Recently seen'], (v) => { friendSort = v; render(); })),
      ...groups.filter(([, list]) => list.length).map(([title, list]) => h('div', { class: 'ol-friend-group' },
        h('h3', {}, title), h('ul', { class: 'ol-friend-list' }, ...list.map(row)))),
      filtered.length ? null : h('p', { class: 'ol-soft' }, 'No friends match this search or filter.'),
      h('div', { class: 'ol-add' },
        h('span', { class: 'ol-add-label' }, 'Add a friend'),
        add));
  }

  // Always in the header, whichever Online section is open. A session pins the ROM: everyone in
  // the lobby has to race on the same revision.
  function romBar() {
    const locked = session.phase !== 'idle';
    const known = roms.entries().length > 0;
    return h('div', { class: 'ol-rom' + (state.get().rom.ready ? '' : ' is-missing') },
      h('span', { class: 'ol-rom-label', id: 'ol-rom-label' }, 'Game ROM'),
      known
        ? roms.romSelect({ fk: 'online-rom', compact: true, disabled: locked, reason: 'Locked while you are in a lobby. Everyone races on the same revision.', onChange: romChanged })
        : button('Choose a ROM…', openRomBrowser, { tone: 'is-strong', fk: 'online-rom' }),
      h('small', {}, locked ? 'Locked while you are in a lobby.' : known ? 'Everyone in a lobby needs the same revision.' : 'You need a ROM to host or join.'));
  }

  function privacyCard() {
    const o = online();
    return h('section', { class: 'ol-card', 'aria-labelledby': 'ol-privacy-title' },
      h('div', { class: 'ol-card-head' }, h('h2', { id: 'ol-privacy-title' }, 'Privacy')),
      switchRow('Appear offline', 'Friends see you as offline. You can still join with a code.',
        o.appearOffline, (v) => state.update('online', { appearOffline: v }), 'appear-offline'),
      switchRow('Allow lobby invites', 'Friends can invite you to their lobby with one click.',
        o.allowInvites, (v) => state.update('online', { allowInvites: v }), 'allow-invites'),
      switchRow('Friend online alerts', 'Show a notice when a friend comes online.',
        o.notifyFriendOnline, (v) => state.update('online', { notifyFriendOnline: v }), 'friend-alerts'),
      selectField('Online alert position', o.alertPosition, POSITIONS, (v) => updateOnline({ alertPosition: v })),
      h('p', { class: 'ol-soft ol-privacy-note' }, 'Races connect PC to PC. Players in the same lobby can see each other’s IP address, so only share codes with people you trust.'));
  }

  // ------------------------------------------------------------ render

  function render() {
    if (!root || !root.isConnected) return;
    const focusKey = document.activeElement?.dataset?.fk;
    const selection = document.activeElement?.matches?.('input[type=text], .ol-code-input')
      ? [document.activeElement.selectionStart, document.activeElement.selectionEnd] : null;

    const tabs = session.phase === 'idle'
      ? [['lobbies', 'Open lobbies'], ['play', 'Host / Quick Join'], ['settings', 'Host settings'], ['profile', 'Online profile'], ['friends', 'Friends'], ['overlays', 'Overlays']]
      : [['lobby', 'Lobby'], ...(inLobby() ? [...(session.role === 'host' ? [['friends', 'Invite friends']] : []), ['connection', 'Connection'], ['overlays', 'Overlays']] : [])];
    if (!tabs.some(([id]) => id === section)) section = tabs[0][0];
    const phaseKey = session.phase + (session.role || '') + section;
    regions.rom.replaceChildren(romBar());
    regions.nav.replaceChildren(...tabs.map(([id, title]) => {
      const tab = button(title, () => { section = id; render(); }, { fk: 'section-' + id });
      if (section === id) tab.setAttribute('aria-current', 'page');
      return tab;
    }));
    const views = {
      lobbies: () => [openLobbiesView()], play: idleView, settings: () => [hostSettingsView()],
      profile: () => [profileCard(), privacyCard(), friendCodesView(), checklistView()],
      friends: () => [friendsCard()], overlays: () => [overlaysView()],
      connection: () => [connectionView()], lobby: session.phase === 'joining' ? joiningView : lobbyView,
    };
    regions.session.replaceChildren(...views[section]());
    regions.check.replaceChildren(...(session.phase === 'idle' && ['play', 'lobbies'].includes(section) ? [checklistView()] : []));
    regions.side.replaceChildren(...(section !== 'profile' ? [profileCard()] : []),
      card('Online guide', 'Two to four racers. One private lobby.',
        h('p', { class: 'ol-soft' }, session.phase === 'idle' ? 'Host settings apply to the next lobby you create.' : 'Gameplay settings are fixed until you leave this session.'),
        button('Read the online guide', showGuide, { fk: 'online-guide' }),
        h('p', { class: 'ol-preview-note' }, 'Interactive preview · Friends, rooms and measurements are simulated.')));
    // Switches are rebuilt too; replay the flip on any whose rendered value changed.
    for (const el of root.querySelectorAll('.ol-switch input[data-fk]')) {
      const before = renderedSwitches.get(el.dataset.fk);
      if (before !== undefined && before !== el.checked) el.classList.add('is-flipped');
      renderedSwitches.set(el.dataset.fk, el.checked);
    }

    if (shownPhase !== null && shownPhase !== phaseKey) {
      [...regions.session.querySelectorAll(':scope > *, .ol-lobby > :not(.ol-countdown), .ol-choices > *')]
        .filter((el) => !el.matches('.ol-lobby, .ol-choices'))
        .forEach((el, i) => { el.style.setProperty('--enter-i', Math.min(i, 6)); el.classList.add('is-entering'); });
    }
    shownPhase = phaseKey;

    const target = focusKey && root.querySelector(`[data-fk="${CSS.escape(focusKey)}"]`);
    // A controller player may be resting on a disabled button (it stays reachable to explain why).
    if (target && target.disabled && DKRLauncher.controller?.mode !== 'pointer') DKRLauncher.controller.focus(target);
    else if (target && !target.disabled) {
      target.focus({ preventScroll: true });
      if (selection && target.setSelectionRange) target.setSelectionRange(...selection);
    }
  }

  state.subscribe((_, section) => {
    if (section === 'online' || section === 'rom' || section === 'mods') render();
  });

  // Keep the expiry line current without rebuilding anything.
  setInterval(() => {
    if (!root?.isConnected || !inLobby() || session.role !== 'host') return;
    const el = root.querySelector('.ol-expiry-time');
    if (el) el.textContent = formatExpiry(session.expiresAt - Date.now());
  }, 1000);

  DKRLauncher.pages.online = function (container) {
    regions = {
      rom: h('div', { class: 'ol-rom-slot' }),
      nav: h('nav', { class: 'ol-section-nav', 'aria-label': 'Online sections', 'data-tabs': '' }),
      session: h('div', { class: 'ol-session' }),
      check: h('div', { class: 'ol-check-wrap' }),
      side: h('aside', { class: 'ol-side', 'aria-label': 'Profile, friends and privacy' }),
    };
    root = h('div', { class: 'online-page' },
      h('header', { class: 'ol-header' },
        h('div', { class: 'ol-header-text' },
          h('h1', { id: 'page-heading' }, 'Online'),
          h('p', { class: 'ol-lede' }, 'Race friends over the internet. No account, no port forwarding: just share a code.')),
        regions.rom),
      regions.nav,
      h('div', { class: 'ol-layout' },
        h('div', { class: 'ol-main' }, regions.session, regions.check),
        regions.side));
    container.append(root);
    shownPhase = null;
    render();
    if (!online().guideAcknowledged && !guideShown) { guideShown = true; showGuide(); }
  };
})();
