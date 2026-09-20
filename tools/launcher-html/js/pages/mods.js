DKRLauncher.pages = DKRLauncher.pages || {};

// MODS / HACKS - an interactive UI study using the native launcher's policies.
// Native sources: runtime_ui.cpp (DrawModsHacks, DrawMagicCodes, DrawTrackLabSection,
// DrawTrackLabControls, DrawLegacyModImportModal, DrawTextEntryKeyboard,
// DrawTexturePackManagementModal), runtime_mod_library_ui.inl (card browser and its modals),
// magic_code_policy.hpp and legacy_mod_browser.hpp. Every worker is simulated with timers.
(function () {
  const { ui, state, mock } = DKRLauncher;
  const { h } = ui;

  const MAX_ACTIVE_STAGE_CHARACTERS = 2;
  const CATALOGUE_STAGE = 'Preparing custom track catalogue';
  const MODERN_NOTICE = 'Switched to Modern - custom tracks and their HD textures need it. Change back in Graphics.';

  // kind: persistent | oneshot | diagnostic
  const MAGIC_CODES = [
    [4, 'ARNOLD', 'Large racers', 'ALL RACE MODES', 'persistent'],
    [5, 'TEENYWEENIES', 'Small racers', 'ALL RACE MODES', 'persistent'],
    [6, 'JUKEBOX', 'Unlock the Music Test', 'OPTIONS MENU', 'persistent'],
    [7, 'FREEFRUIT', 'Start races with ten bananas', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [8, 'BLABBERMOUTH', 'Character voices replace vehicle horns', 'ALL RACE MODES', 'persistent'],
    [10, 'WHODIDTHIS', 'Open Options > Magic Codes, then go Back to show the credits', 'ONE SHOT - OFFLINE ONLY; KEPT QUEUED ONLINE', 'oneshot'],
    [11, 'BYEBYEBALLOONS', 'Disable Weapon Balloons', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [12, 'NOYELLOWSTUFF', 'Disable bananas', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [13, 'BOGUSBANANAS', 'Bananas reduce speed', 'TRACKS MODE - NOT TIME TRIAL', 'persistent'],
    [14, 'VITAMINB', 'Remove the banana limit', 'TRACKS MODE - NOT TIME TRIAL', 'persistent'],
    [15, 'BOMBSAWAY', 'All Weapon Balloons are red', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [16, 'TOXICOFFENDER', 'All Weapon Balloons are green', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [17, 'ROCKETFUEL', 'All Weapon Balloons are blue', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [18, 'BODYARMOR', 'All Weapon Balloons are yellow', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [19, 'OPPOSITESATTRACT', 'All Weapon Balloons are rainbow', 'TRACKS MODE - NOT CHALLENGES OR TIME TRIAL', 'persistent'],
    [20, 'FREEFORALL', 'Weapon Balloons begin fully powered', 'TRACKS MODE - NOT TIME TRIAL', 'persistent'],
    [21, 'ZAPTHEZIPPERS', 'Disable zippers', 'TRACKS MODE - NOT TIME TRIAL', 'persistent'],
    [22, 'DOUBLEVISION', 'Allow duplicate racers', 'CHARACTER SELECT', 'persistent'],
    [23, 'OFFROAD', 'Enable four-wheel drive', 'TRACKS MODE - NOT TIME TRIAL', 'persistent'],
    [24, 'JOINTVENTURE', 'Enable two-player Adventure', 'CHARACTER SELECT AND ADVENTURE', 'persistent'],
    [25, 'TIMETOLOSE', 'Enable Ultimate AI', 'ALL RACE MODES', 'persistent'],
    [26, 'EOLAOBFENRLONE', 'Grant one Golden Balloon to the selected Adventure save', 'ONE SHOT - ADVENTURE FILE SELECT', 'oneshot'],
    [27, 'EPC', 'Enable the EPC lock-up diagnostic', 'DIAGNOSTIC - ONLY VISIBLE DURING A FAULT', 'diagnostic'],
    [28, 'DODGYROMMER', 'Display the ROM checksum', 'DIAGNOSTIC - IN-GAME MAGIC CODES SCREEN', 'diagnostic'],
  ].map(([index, phrase, effect, availability, kind]) => ({ index, phrase, effect, availability, kind }));

  const SORT_CHOICES = ['Name A-Z', 'Name Z-A', 'Largest first', 'Smallest first', 'Newest first', 'Oldest first'];
  const STATE_CHOICES = ['All', 'Active', 'Inactive'];
  const COMPATIBILITY_CHOICES = ['All', 'Compatible', 'Needs attention'];
  const VISIBILITY_CHOICES = ['Visible', 'All', 'Hidden'];

  function newBrowser() {
    return { search: '', source: '', sort: 0, state: 0, compatibility: 0, visibility: 0, format: 0 };
  }

  // Navigation and filters are local to this visit; installed mods remain in state.
  const view = {
    section: null,
    category: 'tracks',
    filtersOpen: false,
    detailsOpen: new Set(),
    importDetailsOpen: false,
    browsers: [newBrowser(), newBrowser()],
    magicStatus: '',
    legacyImportStatus: '',
    trackImportStatus: '',
    modernNotice: '',
    pickerOpen: false,
    modal: null,
  };

  // ModLibraryView: native startup runs a scan that ends in "Mod libraries refreshed."
  const lib = {
    busy: false, modal: false, succeeded: true, result: 'Mod libraries refreshed.',
    stage: 'Mod library ready', completed: 0, total: 0, timer: 0, cancel: null,
  };

  let pageRoot = null;
  let renderQueued = false;
  // What the last render showed; enter motion only plays when this changes, not on every re-render.
  let shownView = null;
  let pendingFocusKey;

  // ---------------------------------------------------------------- policies

  function enableMagicCode(current, index) {
    const selected = new Set(current);
    selected.add(index);
    const drop = (list) => list.forEach((i) => selected.delete(i));
    const bananaModifiers = [7, 13, 14];
    const weaponModifiers = [15, 16, 17, 18, 19, 20];
    const balloonColours = [15, 16, 17, 18, 19];
    if (index === 4) drop([5]);
    if (index === 5) drop([4]);
    if (index === 12) drop(bananaModifiers);
    if (bananaModifiers.includes(index)) drop([12]);
    if (index === 11) drop(weaponModifiers);
    if (weaponModifiers.includes(index)) drop([11]);
    if (balloonColours.includes(index)) drop(balloonColours.filter((i) => i !== index));
    return MAGIC_CODES.map((code) => code.index).filter((i) => selected.has(i));
  }

  function gamePakRevision() {
    const rom = state.get().rom;
    if (!rom.ready) return 0;
    return /v\s*1\.1|rev\s*a|v80/i.test(rom.path) ? 2 : 1;
  }

  const compatible = (card, revision) => card.native || (revision !== 0 && (card.revisions & revision) !== 0);
  const activeCount = (cards) => cards.filter((card) => card.enabled).length;

  function canActivate(card, characters, active, revision, locked) {
    return !locked && !card.hidden && compatible(card, revision) &&
      (!characters || active < MAX_ACTIVE_STAGE_CHARACTERS);
  }

  function selectCards(all, filters, revision) {
    const query = filters.search.toLowerCase();
    const shown = all.filter((card) => {
      const searchable = [card.name, card.author || '', card.native ? 'DKR' : 'Legacy', ...card.sources].join(' ').toLowerCase();
      if (query && !searchable.includes(query)) return false;
      if ((filters.format === 1 && card.native) || (filters.format === 2 && !card.native)) return false;
      if (filters.source && !card.sources.includes(filters.source)) return false;
      if ((filters.state === 1 && !card.enabled) || (filters.state === 2 && card.enabled)) return false;
      if ((filters.visibility === 0 && card.hidden) || (filters.visibility === 2 && !card.hidden)) return false;
      if ((filters.compatibility === 1 && !compatible(card, revision)) ||
          (filters.compatibility === 2 && compatible(card, revision))) return false;
      return true;
    });
    return shown.sort((a, b) => {
      if (filters.sort === 2 && a.managedBytes !== b.managedBytes) return b.managedBytes - a.managedBytes;
      if (filters.sort === 3 && a.managedBytes !== b.managedBytes) return a.managedBytes - b.managedBytes;
      if ((filters.sort === 4 || filters.sort === 5) && a.importedAt !== b.importedAt) {
        // Unknown historical dates sort last in either direction.
        if (!a.importedAt || !b.importedAt) return a.importedAt ? -1 : 1;
        return filters.sort === 4 ? b.importedAt - a.importedAt : a.importedAt - b.importedAt;
      }
      const left = a.name.toLowerCase();
      const right = b.name.toLowerCase();
      if (left !== right) return (filters.sort === 1 ? left < right : left > right) ? 1 : -1;
      return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
    });
  }

  function formatSize(bytes) {
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit + 1 < units.length) { value /= 1024; unit += 1; }
    return unit === 0 ? `${Math.round(value)} ${units[unit]}` : `${value.toFixed(1)} ${units[unit]}`;
  }

  function formatDate(seconds) {
    return new Date(seconds * 1000).toISOString().slice(0, 10);
  }

  // ----------------------------------------------------------------- widgets

  function text(content, tone, extraClass) {
    return h('div', { class: ['im', 'im-text', tone && 'im-' + tone, extraClass].filter(Boolean).join(' ') }, content);
  }

  function dummy(height) {
    return h('div', { class: 'im', style: `height:${height}px`, 'aria-hidden': 'true' });
  }

  function button(label, { height = 42, full = true, width, variant, disabled, onClick, fk, cls } = {}) {
    const el = ui.raceButton({ label, variant, disabled, onClick });
    el.classList.add('im');
    if (full) el.classList.add('im-full');
    if (cls) el.classList.add(...cls.split(' '));
    if (height) el.style.height = height + 'px';
    if (width) el.style.width = width;
    if (fk) el.dataset.fk = fk;
    return el;
  }

  // ModWrappedButton: full width, wrapped label, height grows with the label.
  function wrappedButton(label, { minHeight = 42, control, disabled, onClick, fk, variant } = {}) {
    const el = button(label, { height: 0, disabled, onClick, fk, variant, cls: 'im-wrapped' + (control ? ' im-control' : '') });
    el.style.minHeight = minHeight + 'px';
    return el;
  }

  function checkbox(label, checked, { disabled, onChange, fk, ariaLabel } = {}) {
    const input = h('input', { type: 'checkbox', disabled, 'aria-label': ariaLabel, 'data-fk': fk });
    input.checked = !!checked;
    input.addEventListener('change', () => onChange(input.checked));
    return h('label', { class: 'im im-check' + (disabled ? ' is-disabled' : '') },
      input, label ? h('span', {}, label) : null);
  }

  function separatorText(label, dim) {
    return h('h3', { class: 'im-septext' + (dim ? ' im-dim' : '') }, label);
  }

  function separator() {
    return h('div', { class: 'im-separator', role: 'separator' });
  }

  function helpDisclosure(label, key, ...children) {
    return h('details', {
      class: 'mods-help-disclosure', open: view.detailsOpen.has(key),
      ontoggle: (event) => {
        if (!event.target.isConnected) return;
        if (event.target.open) view.detailsOpen.add(key);
        else view.detailsOpen.delete(key);
      },
    }, h('summary', { 'data-fk': 'help-disclosure-' + key }, label), ...children);
  }

  function spinner(large) {
    const radius = large ? 9 : 8;
    const centre = large ? 12 : 10;
    const el = h('span', { class: 'im-spinner' + (large ? ' large' : ''), 'aria-hidden': 'true' });
    el.innerHTML = `<svg width="${centre * 2}" height="${centre * 2}" viewBox="0 0 ${centre * 2} ${centre * 2}">` +
      `<circle cx="${centre}" cy="${centre}" r="${radius}" fill="none" stroke="#ffab14" stroke-width="${large ? 3 : 2}" ` +
      `stroke-dasharray="${(4.5 * radius).toFixed(2)} ${(2 * Math.PI * radius).toFixed(2)}"/></svg>`;
    return el;
  }

  // ------------------------------------------------------------ state helpers

  const mods = () => state.get().mods;
  const libraryKey = (characters) => (characters ? 'characters' : 'tracks');

  function updateLibrary(mutate) {
    const current = mods().library;
    const next = {
      tracks: current.tracks.map((card) => ({ ...card })),
      characters: current.characters.map((card) => ({ ...card })),
      imports: current.imports.slice(),
      removed: (current.removed || []).map((entry) => ({ ...entry })),
    };
    mutate(next);
    state.update('mods', { library: next });
  }

  function updateLab(patch) {
    state.update('mods', { trackLab: { ...mods().trackLab, ...patch } });
  }

  function labTracks(lab) {
    const installed = lab.tracks || [];
    const ids = new Set(installed.map((track) => track.id));
    return installed.concat((lab.watched || []).filter((track) => !ids.has(track.id)));
  }

  // Installed DKR courses appear in Track Select; Track Lab selection is only for testing.
  function trackLibrary() {
    const lab = mods().trackLab;
    return mods().library.tracks.concat(labTracks(lab).map((track) => ({
      ...track, native: true, trackId: track.id, id: 'dkr-' + track.id,
      sources: [track.source || track.id + '.dkrmap'],
      enabled: true, hidden: false,
      managedBytes: track.managedBytes || 0, importedAt: track.importedAt || 0,
    })));
  }

  function showImportedTrack(track) {
    view.section = 'library';
    view.category = 'tracks';
    Object.assign(view.browsers[0], newBrowser());
    view.trackImportStatus = `${track.name} installed. Find it in the in-game track menu.`;
    scheduleRender();
  }

  function updateLabTrack(id, mutate) {
    const lab = mods().trackLab;
    const hdPacks = { ...(lab.hdPacks || {}) };
    const edit = (list) => (list || []).map((track) => {
      if (track.id !== id) return track;
      const copy = { ...track, hd: track.hd ? { ...track.hd } : null };
      mutate(copy);
      if (copy.hd?.installed) hdPacks[id] = { ...copy.hd };
      else delete hdPacks[id];
      return copy;
    });
    updateLab({ tracks: edit(lab.tracks), watched: edit(lab.watched), hdPacks });
  }

  function ensureModernForTracks() {
    if (state.get().graphics.profile === 'modern') return false;
    view.modernNotice = MODERN_NOTICE;
    state.update('graphics', { profile: 'modern' });
    return true;
  }

  // --------------------------------------------------------- simulated jobs

  function startJob({ modal, steps, finish, cancelled }) {
    if (lib.busy) return false;
    let index = 0;
    const complete = (success, result, showModal) => {
      clearTimeout(lib.timer);
      Object.assign(lib, { busy: false, succeeded: success, result, stage: 'Mod library ready', completed: 0, total: 0, cancel: null });
      if (showModal) rememberReturnFocus();
      if (showModal) lib.modal = true;
      scheduleRender();
    };
    const step = () => {
      if (index >= steps.length) {
        const outcome = finish();
        complete(outcome.success, outcome.result, outcome.modal);
        return;
      }
      const current = steps[index];
      index += 1;
      Object.assign(lib, { stage: current.stage, completed: current.completed || 0, total: current.total || 0 });
      scheduleRender();
      lib.timer = setTimeout(step, current.ms);
    };
    if (modal) rememberReturnFocus();
    Object.assign(lib, { busy: true, modal, succeeded: false, result: '' });
    lib.cancel = () => complete(false, cancelled ? cancelled(index - 1) : 'Custom content operation stopped safely.');
    step();
    return true;
  }

  function refreshLibraries() {
    startJob({
      modal: false,
      steps: [{ stage: 'Reading mod libraries', ms: 800 }],
      finish: () => ({ success: true, result: 'Mod libraries refreshed.' }),
    });
  }

  // A failed toggle opens the library modal (ModLibrary::finish).
  function setEnabled(characters, id, enabled) {
    startJob({
      modal: false,
      steps: [{ stage: CATALOGUE_STAGE, ms: 350 }],
      cancelled: () => 'Activation change cancelled.',
      finish: () => {
        const list = mods().library[libraryKey(characters)];
        const card = list.find((entry) => entry.id === id);
        if (!card) return { success: false, modal: true, result: 'Only a prepared custom course can change activation state.' };
        if (enabled && card.hidden) return { success: false, modal: true, result: 'Restore this mod to the library before activating it.' };
        const nextActive = list.filter((entry) => entry.enabled && entry.id !== id).length + (enabled ? 1 : 0);
        if (characters && nextActive > MAX_ACTIVE_STAGE_CHARACTERS) {
          return { success: false, modal: true, result: 'This beta supports two extra active characters on the original stage. Disable one before enabling another; installed characters remain in your library.' };
        }
        updateLibrary((library) => { library[libraryKey(characters)].find((entry) => entry.id === id).enabled = enabled; });
        return {
          success: true,
          result: enabled ? 'Custom content enabled for the next game session.' : 'Custom content disabled. Its files and records were retained.',
        };
      },
    });
  }

  function setHidden(characters, id, hidden) {
    startJob({
      modal: false,
      steps: [{ stage: CATALOGUE_STAGE, ms: 350 }],
      cancelled: () => 'Library change cancelled.',
      finish: () => {
        if (!mods().library[libraryKey(characters)].some((entry) => entry.id === id)) {
          return { success: false, modal: true, result: 'Only installed content can be managed.' };
        }
        updateLibrary((library) => {
          const card = library[libraryKey(characters)].find((entry) => entry.id === id);
          card.hidden = hidden;
          if (hidden) card.enabled = false;
        });
        return {
          success: true,
          result: hidden ? 'Mod deactivated and hidden. Files and saves retained.' : 'Mod restored to the library. It remains inactive.',
        };
      },
    });
  }

  function removeMod(characters, id) {
    startJob({
      modal: true,
      steps: [
        { stage: CATALOGUE_STAGE, ms: 300 },
        { stage: 'Removing selected mod; preserving other content and saves', ms: 1100 },
      ],
      cancelled: () => 'Library change cancelled.',
      finish: () => {
        updateLibrary((library) => {
          const key = libraryKey(characters);
          const card = library[key].find((entry) => entry.id === id);
          if (!card) return;
          library[key] = library[key].filter((entry) => entry.id !== id);
          library.removed.push({ kind: key, card: { ...card, enabled: false } });
        });
        return {
          success: true,
          result: 'Selected mod and its prepared revision variants removed. Other mods, original files, retained import material and all saves remain untouched.',
        };
      },
    });
  }

  function uninstallTrack(id) {
    if (mods().lobbyActive || lib.busy || view.pickerOpen) return;
    startJob({
      modal: true,
      steps: [{ stage: 'Uninstalling track', ms: 500 }],
      cancelled: () => 'Track uninstall cancelled.',
      finish: () => {
        if (mods().lobbyActive) return { success: false, modal: true, result: 'Leave the lobby to change tracks.' };
        const lab = mods().trackLab;
        const track = (lab.tracks || []).find((entry) => entry.id === id);
        if (!track) return { success: false, modal: true, result: 'This track is no longer installed.' };
        const hdPacks = { ...(lab.hdPacks || {}) };
        if (track.hd?.installed) hdPacks[id] = { ...track.hd };
        updateLab({
          tracks: lab.tracks.filter((entry) => entry.id !== id), hdPacks,
          ...(lab.armed === id ? { armed: '', autoBoot: false } : {}),
        });
        view.trackImportStatus = 'Track uninstalled. Source files, saves and HD texture packs were kept.';
        return { success: true, result: view.trackImportStatus };
      },
    });
  }

  function disableAll(characters) {
    startJob({
      modal: false,
      steps: [{ stage: CATALOGUE_STAGE, ms: 500 }],
      finish: () => {
        updateLibrary((library) => { library[libraryKey(characters)].forEach((card) => { card.enabled = false; }); });
        return { success: true, result: 'All content in this library disabled. Installed files and saves were retained.' };
      },
    });
  }

  // Prepares a saved review for the current ROM and reinstalls removed entries.
  function prepareVariants(library, review, kind, revision) {
    const key = kind === 'Character' ? 'characters' : 'tracks';
    const names = library.imports.filter((item) => item.review === review && item.kind === kind).map((item) => item.name);
    const kept = [];
    for (const entry of library.removed) {
      if (entry.kind === key && entry.card.review === review) library[key].push(entry.card);
      else kept.push(entry);
    }
    library.removed = kept;
    for (const name of names) {
      const id = mock.digest(kind + ':' + name);
      const card = library[key].find((entry) => entry.id === id);
      if (card) card.revisions |= revision;
    }
    return names.length;
  }

  function preparedLine(kind, count) {
    return kind === 'Character'
      ? `Characters: ${count} character variant(s) prepared. Enable them in Custom Characters.`
      : `Tracks: ${count} course variant(s) prepared. Enable them in Custom Tracks.`;
  }

  function prepareReview(review) {
    const items = mods().library.imports.filter((item) => item.review === review);
    const kinds = ['Track', 'Character'].filter((kind) => items.some((item) => item.kind === kind));
    if (!kinds.length) return;
    const revision = gamePakRevision();
    const steps = [];
    for (const kind of kinds) {
      steps.push({ stage: CATALOGUE_STAGE, ms: 400 });
      if (!revision) continue;
      const total = items.filter((item) => item.kind === kind).length;
      for (let n = 1; n <= total; n += 1) {
        steps.push({ stage: kind === 'Character' ? 'Preparing additive character assets' : 'Preparing scoped course assets', completed: n, total, ms: 650 });
      }
    }
    startJob({
      modal: true,
      steps,
      cancelled: () => 'Tracks: Preparation cancelled. Enabled tracks and saves were not changed.\nCancelled. Completed imports remain installed but were not enabled.',
      finish: () => {
        const prefix = (kind) => (kind === 'Character' ? 'Characters: ' : 'Tracks: ');
        if (!revision) {
          return {
            success: false,
            result: kinds.map((kind) => prefix(kind) + 'Select a saved review and its imported ROM first.').join('\n') +
              '\nChoose which prepared mods to enable in the libraries below.',
          };
        }
        const lines = [];
        updateLibrary((library) => {
          for (const kind of kinds) lines.push(preparedLine(kind, prepareVariants(library, review, kind, revision)));
        });
        return { success: true, result: lines.join('\n') + '\nChoose which prepared mods to enable in the libraries below.' };
      },
    });
  }

  function titleFromFile(stem) {
    return stem.replace(/[_-]+/g, ' ').replace(/\s+/g, ' ').trim()
      .replace(/\b\w/g, (letter) => letter.toUpperCase()) || 'Imported Mod';
  }

  function startImport(fileName, fileSize) {
    view.legacyImportStatus = '';
    view.trackImportStatus = '';
    const revision = gamePakRevision();
    if (!revision) {
      startJob({
        modal: true,
        steps: [{ stage: 'Preparing legacy import', ms: 600 }],
        finish: () => ({ success: false, result: 'Import an original ROM in the Play tab first (maximum eight source files).' }),
      });
      return;
    }
    const stem = fileName.replace(/\.(xdelta|zip)$/i, '');
    const kind = /char|racer|driver/i.test(stem) ? 'Character' : 'Track';
    const name = titleFromFile(stem);
    const reviewStep = 3;
    const steps = [
      { stage: 'Preparing legacy import', ms: 400 },
      { stage: 'Reading and checking archive', ms: 700 },
      { stage: 'Decoding patch against verified ROMs', completed: 1, total: 1, ms: 1000 },
      { stage: 'Preparing import review', ms: 600 },
      { stage: CATALOGUE_STAGE, ms: 400 },
      { stage: kind === 'Character' ? 'Preparing additive character assets' : 'Preparing scoped course assets', completed: 1, total: 1, ms: 900 },
    ];
    const review = mock.digest('review:' + fileName);
    const saveReview = (library) => {
      if (!library.imports.some((item) => item.review === review && item.name === name)) {
        library.imports.push(mock.importItem(kind, name, fileName));
      }
    };
    startJob({
      modal: true,
      steps,
      cancelled: (stepIndex) => {
        if (stepIndex <= reviewStep) return 'Import cancelled. Stock tracks, ROMs and saves were not changed.';
        updateLibrary(saveReview);
        return `${kind === 'Character' ? 'Characters' : 'Tracks'}: Preparation cancelled. Enabled tracks and saves were not changed.\nCancelled. Completed imports remain installed but were not enabled.`;
      },
      finish: () => {
        updateLibrary((library) => {
          saveReview(library);
          const key = kind === 'Character' ? 'characters' : 'tracks';
          const id = mock.digest(kind + ':' + name);
          if (!library[key].some((card) => card.id === id) &&
              !library.removed.some((entry) => entry.card.id === id)) {
            library[key].push(mock.mod(kind, name, fileName, {
              revisions: kind === 'Character' ? 3 : revision,
              managedBytes: Math.max(1, fileSize) * 6,
              importedAt: Math.floor(Date.now() / 1000),
            }));
          }
          prepareVariants(library, review, kind, revision);
        });
        view.section = 'library';
        view.category = kind === 'Character' ? 'characters' : 'tracks';
        Object.assign(view.browsers[kind === 'Character' ? 1 : 0], newBrowser());
        return { success: true, result: preparedLine(kind, 1) + '\nChoose which prepared mods to enable in the libraries below.' };
      },
    });
  }

  function importMods() {
    if (lib.busy || mods().lobbyActive) return;
    openModal({ type: 'choose-import' });
  }

  function importLegacyFile() {
    if (lib.busy || mods().lobbyActive || !gamePakRevision()) return;
    closeModal();
    const input = h('input', { type: 'file', accept: '.xdelta,.zip', hidden: true });
    input.addEventListener('change', () => {
      const file = input.files[0];
      input.remove();
      if (file) startImport(file.name, file.size);
    });
    input.addEventListener('cancel', () => input.remove());
    document.body.append(input);
    input.click();
  }

  // ------------------------------------------------------ Track Lab pickers

  function pickFolder(onPicked) {
    const input = h('input', { type: 'file', webkitdirectory: true, multiple: true, hidden: true });
    view.pickerOpen = true;
    const done = (files) => {
      view.pickerOpen = false;
      input.remove();
      onPicked(files);
    };
    input.addEventListener('change', () => done([...input.files]));
    input.addEventListener('cancel', () => done(null));
    document.body.append(input);
    input.click();
  }

  const pathParts = (file) => (file.webkitRelativePath || file.name).split('/');

  async function describeTrack(folder, entries) {
    const stem = folder.replace(/\.dkrmap$/i, '');
    let manifest = {};
    const manifestEntry = entries.find((entry) => entry.rest.toLowerCase() === 'manifest.json');
    if (manifestEntry) {
      try { manifest = JSON.parse(await manifestEntry.file.text()) || {}; } catch { manifest = {}; }
    }
    const id = typeof manifest.id === 'string' && manifest.id ? manifest.id : stem;
    return {
      id,
      name: typeof manifest.name === 'string' && manifest.name ? manifest.name : id,
      author: typeof manifest.author === 'string' ? manifest.author : '',
      textures: entries.filter((entry) => /\.(png|bmp|tga|dds)$/i.test(entry.rest)).length,
      translucent: 0,
      animated: 0,
      hd: null,
      source: folder,
      managedBytes: entries.reduce((total, entry) => total + entry.file.size, 0),
      importedAt: Math.floor(Date.now() / 1000),
    };
  }

  // .dkrmap folders directly inside the picked folder, or the picked folder itself.
  async function findDkrmaps(files) {
    const groups = new Map();
    for (const file of files) {
      const parts = pathParts(file);
      const at = /\.dkrmap$/i.test(parts[0]) ? 0 : (parts.length > 2 && /\.dkrmap$/i.test(parts[1]) ? 1 : -1);
      if (at < 0) continue;
      const key = parts.slice(0, at + 1).join('/');
      if (!groups.has(key)) groups.set(key, { folder: parts[at], entries: [] });
      groups.get(key).entries.push({ file, rest: parts.slice(at + 1).join('/') });
    }
    const hdZips = new Map(files.filter((file) => pathParts(file).length === 2 && /-hd\.zip$/i.test(file.name))
      .map((file) => [file.name.toLowerCase(), file]));
    const tracks = [];
    for (const group of groups.values()) {
      const track = await describeTrack(group.folder, group.entries);
      const hdZip = hdZips.get(`${track.id}-hd.zip`.toLowerCase());
      if (hdZip) {
        track.hd = { installed: false, enabled: false, digestMatches: false, siblingMismatch: false,
          sourceBytes: hdZip.size, sourceName: hdZip.name, sourceStamp: `${hdZip.size}:${hdZip.lastModified}` };
      }
      tracks.push(track);
    }
    return tracks;
  }

  function chooseWorkingFolder(rescan = false) {
    if (lib.busy || mods().lobbyActive || view.pickerOpen) return;
    // Directory-input fallback: reselect the same folder to obtain a fresh file snapshot.
    // No track files are copied into managed storage by this workflow.
    view.trackImportStatus = rescan === true ? 'Choose your working folder to rescan its tracks.' : 'Choose your working folder.';
    pickFolder(async (files) => {
      if (!files) { view.trackImportStatus = ''; scheduleRender(); return; }
      const root = files.length ? pathParts(files[0])[0] : mods().trackLab.workingFolder || 'the selected folder';
      const tracks = await findDkrmaps(files);
      const previous = mods().trackLab;
      const hdPacks = { ...(previous.hdPacks || {}) };
      (previous.watched || []).forEach((track) => {
        if (track.hd?.installed) hdPacks[track.id] = { ...track.hd };
      });
      const sameFolder = root === previous.workingFolder;
      for (const track of tracks) {
        const old = (sameFolder && (previous.watched || []).find((entry) => entry.id === track.id)) ||
          (hdPacks[track.id] && { hd: hdPacks[track.id] });
        if (!old) continue;
        track.importedAt = old.importedAt || track.importedAt;
        if (old.hd?.installed) {
          const source = track.hd;
          // Keep the installed AppData pack and its user's enabled/hidden choices.
          track.hd = { ...old.hd };
          if (source) Object.assign(track.hd, {
            sourceBytes: source.sourceBytes, sourceName: source.sourceName, sourceStamp: source.sourceStamp,
            siblingMismatch: old.hd.siblingMismatch || !!(old.hd.sourceStamp && old.hd.sourceStamp !== source.sourceStamp),
          });
        }
      }
      const keepTest = sameFolder && tracks.some((track) => track.id === previous.armed);
      updateLab({ workingFolder: root, watched: tracks, hdPacks, ...(!keepTest ? { armed: '', autoBoot: false } : {}) });
      view.trackImportStatus = tracks.length === 0 ? 'No .dkrmap folders found here.' : `${tracks.length} ${tracks.length === 1 ? 'track' : 'tracks'} found in ${root}.`;
      scheduleRender();
    });
    scheduleRender();
  }

  function installWorkingHD(trackId) {
    const track = findLabTrack(trackId);
    if (!track?.hd?.sourceName || lib.busy || mods().lobbyActive) return;
    startJob({ modal: false, steps: [{ stage: 'Installing HD textures', ms: 650 }], finish: () => {
      updateLabTrack(trackId, (entry) => {
        entry.hd = { ...entry.hd, installed: true, enabled: true, digestMatches: true, siblingMismatch: false,
          packId: `${trackId}-hd`, packName: `${entry.name} HD`, format: 'Native RT64',
          managedBytes: entry.hd.sourceBytes, images: Math.max(entry.textures, 1), hidden: false };
      });
      view.trackImportStatus = `HD textures installed for ${track.name}. The track stays in your working folder.`;
      return { success: true, result: view.trackImportStatus };
    }});
  }

  function importTrackCopy() {
    if (lib.busy || mods().lobbyActive || view.pickerOpen) return;
    closeModal();
    view.section = 'library';
    view.category = 'tracks';
    view.trackImportStatus = 'Choose the track in the folder picker...';
    pickFolder(async (files) => {
      if (!files) { view.trackImportStatus = ''; scheduleRender(); return; }
      const direct = files.filter((file) => pathParts(file).length === 2);
      const root = files.length ? pathParts(files[0])[0] : '';
      let track = null;
      if (/\.dkrmap$/i.test(root) || direct.some((file) => file.name.toLowerCase() === 'manifest.json')) {
        track = await describeTrack(root, files.map((file) => ({ file, rest: pathParts(file).slice(1).join('/') })));
      } else {
        const found = await findDkrmaps(files);
        const zips = direct.filter((file) => /\.zip$/i.test(file.name) && !/-hd\.zip$/i.test(file.name));
        if (found.length === 1) track = found[0];
        else if (found.length === 0 && zips.length) track = await describeTrack(zips[0].name.replace(/\.zip$/i, ''), []);
      }
      if (!track) {
        view.trackImportStatus = "Pick the track's .dkrmap, its folder, or the folder that holds both it and its -hd.zip.";
        scheduleRender();
        return;
      }
      const hdZip = direct.find((file) => file.name.toLowerCase() === `${track.id}-hd.zip`.toLowerCase());
      if (hdZip) {
        track.hd = {
          installed: true, enabled: true, digestMatches: true, siblingMismatch: false,
          packId: `${track.id}-hd`, packName: `${track.id}-hd`, format: 'Native RT64',
          managedBytes: hdZip.size, images: Math.max(track.textures, 1), hidden: false,
        };
      }
      const lab = mods().trackLab;
      updateLab({ tracks: (lab.tracks || []).filter((entry) => entry.id !== track.id).concat(track) });
      showImportedTrack(track);
    });
    scheduleRender();
  }

  // Like legacy imports, archive preparation is simulated in this UI study.
  function importDKRArchive() {
    if (lib.busy || mods().lobbyActive) return;
    closeModal();
    const input = h('input', { type: 'file', accept: '.zip', hidden: true });
    input.addEventListener('change', () => {
      const file = input.files[0];
      input.remove();
      if (!file) return;
      const stem = file.name.replace(/\.zip$/i, '').replace(/\.dkrmap$/i, '');
      const track = { id: mock.digest('dkr:' + stem), name: titleFromFile(stem), author: '',
        source: file.name, managedBytes: file.size, importedAt: Math.floor(Date.now() / 1000),
        textures: 0, translucent: 0, animated: 0, hd: null };
      startJob({ modal: true, steps: [{ stage: 'Importing DKR track', ms: 700 }], finish: () => {
        const lab = mods().trackLab;
        updateLab({ tracks: (lab.tracks || []).filter((entry) => entry.id !== track.id).concat(track) });
        showImportedTrack(track);
        return { success: true, result: `${track.name} installed. Find it in the in-game track menu.` };
      }});
    });
    input.addEventListener('cancel', () => input.remove());
    document.body.append(input);
    input.click();
  }

  function armTrack(track, needsRelaunch) {
    const patch = { armed: track.id };
    // From the launcher there is no process to relaunch, so skipping the menus is how HD textures load.
    if (needsRelaunch) patch.autoBoot = true;
    updateLab(patch);
    ensureModernForTracks();
  }

  // ----------------------------------------------------------------- page

  function magicCodesSection(lobbyActive) {
    const out = [];
    const selected = mods().magicCodes || [];
    out.push(h('div', { class: 'mods-inline-note' }, h('strong', {}, `${selected.length} ${selected.length === 1 ? 'code' : 'codes'} selected`),
      h('span', {}, 'Conflicting codes are turned off automatically.')),
      helpDisclosure('When and where do codes apply?', 'code-rules',
        h('p', {}, 'Changes apply on the next launch. Each code lists its supported modes. Tracks-only codes do not affect Adventure.'),
        h('p', {}, 'One-time actions stay queued until used. After granting a Golden Balloon, let the game save before quitting.')));
    if (lobbyActive) {
      out.push(text('Leave the online lobby before changing Magic Codes. This session uses the codes agreed with the host. Credits remain queued for offline play.'));
    }
    const dim = lobbyActive ? 'im-dim' : '';
    const group = (diagnostics) => {
      const table = h('div', { class: 'im-table' });
      for (const code of MAGIC_CODES) {
        if ((code.kind === 'diagnostic') !== diagnostics) continue;
        const tone = diagnostics ? 'red' : 'muted';
        table.append(h('div', { class: 'im-cell im-magic-cell' },
          checkbox(code.phrase, selected.includes(code.index), {
            disabled: lobbyActive,
            fk: 'magic-' + code.index,
            onChange: (enabled) => {
              view.magicStatus = enabled
                ? code.phrase + (code.kind === 'oneshot' ? ' queued until its native action runs.' : ' will be active on launch.')
                : code.phrase + ' disabled.';
              state.update('mods', {
                magicCodes: enabled ? enableMagicCode(selected, code.index) : selected.filter((i) => i !== code.index),
              });
            },
          }),
          h('div', { class: 'im-indent' },
            text(code.effect, tone, dim),
            text(code.availability, tone, dim))));
      }
      return table;
    };
    out.push(
      dummy(8),
      separatorText('RACE MODIFIERS', lobbyActive),
      group(false),
      helpDisclosure('Advanced: diagnostic codes', 'diagnostics', group(true)),
      dummy(6),
      button('CLEAR ALL MAGIC CODES', {
        disabled: lobbyActive,
        fk: 'magic-clear',
        onClick: () => {
          view.magicStatus = 'All launch Magic Codes cleared.';
          state.update('mods', { magicCodes: [] });
        },
      }));
    if (view.magicStatus) out.push(h('p', { class: 'mods-feedback', role: 'status' }, view.magicStatus));
    return out;
  }

  function plainButton(label, onClick, { cls = '', fk, disabled = false } = {}) {
    return h('button', { type: 'button', class: 'mods-button ' + cls, onclick: onClick, disabled, 'data-fk': fk }, label);
  }

  function filterSelect(label, options, value, onChange, fk) {
    const select = h('select', { 'data-fk': fk });
    options.forEach((option, i) => select.append(h('option', { value: i }, option)));
    select.value = value;
    select.addEventListener('change', () => { onChange(Number(select.value)); scheduleRender(); });
    return h('label', { class: 'mods-field' }, h('span', {}, label), select);
  }

  function cardBrowser(characters, modsLocked) {
    const all = characters ? mods().library.characters : trackLibrary();
    const browser = view.browsers[characters ? 1 : 0];
    const locked = modsLocked || lib.busy || view.pickerOpen;
    const revision = gamePakRevision();
    const key = libraryKey(characters);
    const active = activeCount(all);
    const out = [];
    const filtered = !!(browser.search || browser.source || browser.state || browser.compatibility || browser.visibility || browser.format);
    const advancedCount = [browser.source, browser.compatibility, browser.visibility, browser.format].filter(Boolean).length;
    // The search box opens the on-screen keyboard (SEARCH CUSTOM TRACKS / CHARACTERS) and applies on APPLY SEARCH.
    const search = ui.fieldButton({
      label: 'Search ' + key, value: browser.search, placeholder: 'Search by name or source pack…', className: 'mods-search', fk: key + '-search',
      onOpen: (typed) => ui.keyboard({
        heading: characters ? 'SEARCH CUSTOM CHARACTERS' : 'SEARCH CUSTOM TRACKS',
        hint: characters ? 'CHARACTER OR PACK NAME' : 'TRACK OR PACK NAME', accept: 'APPLY SEARCH', allowEmpty: true,
        value: browser.search, typed, maxLength: 120,
        onAccept: (text) => { browser.search = text.trim(); scheduleRender(); },
      }),
    });
    const filterButton = plainButton('Filters' + (advancedCount ? ` (${advancedCount})` : ''),
      () => { view.filtersOpen = !view.filtersOpen; scheduleRender(); }, { fk: key + '-filters', cls: view.filtersOpen ? 'is-selected' : '' });
    filterButton.setAttribute('aria-expanded', String(view.filtersOpen));
    filterButton.setAttribute('aria-controls', 'mods-filters');
    out.push(h('div', { class: 'mods-toolbar' }, search,
      filterSelect('Status', STATE_CHOICES, browser.state, (i) => { browser.state = i; }, key + '-state'),
      filterSelect('Sort by', SORT_CHOICES, browser.sort, (i) => { browser.sort = i; }, key + '-sort'), filterButton));
    if (view.filtersOpen) {
      const sources = [...new Set(all.flatMap((card) => card.sources))].sort();
      if (browser.source && !sources.includes(browser.source)) browser.source = '';
      out.push(h('div', { class: 'mods-filter-panel', id: 'mods-filters' },
        !characters ? filterSelect('Track format', ['All tracks', 'Legacy', 'DKR'], browser.format, (i) => { browser.format = i; }, key + '-format') : null,
        filterSelect('Compatibility', COMPATIBILITY_CHOICES, browser.compatibility, (i) => { browser.compatibility = i; }, key + '-compatibility'),
        filterSelect('Source pack', ['All packs', ...sources], Math.max(0, sources.indexOf(browser.source) + 1), (i) => { browser.source = sources[i - 1] || ''; }, key + '-source'),
        filterSelect('Visibility', VISIBILITY_CHOICES, browser.visibility, (i) => { browser.visibility = i; }, key + '-visibility')));
    }
    const shown = selectCards(all, browser, revision);
    out.push(h('div', { class: 'mods-results' },
      h('span', { role: 'status' }, `${shown.length} of ${all.length} ${key}`),
      filtered ? plainButton('Clear filters', () => { Object.assign(browser, newBrowser()); scheduleRender(); }, { fk: key + '-reset', cls: 'mods-link' }) : null));
    if (characters) out.push(h('div', { class: 'mods-inline-note' },
      h('strong', {}, `${active} / ${MAX_ACTIVE_STAGE_CHARACTERS} character slots selected`),
      h('span', {}, active >= MAX_ACTIVE_STAGE_CHARACTERS ? 'Turn off a character before choosing another.' : 'Up to two custom racers can join the original character selection.')));
    if (!shown.length) {
      out.push(h('div', { class: 'mods-empty' }, h('h3', {}, all.length ? 'No matching mods' : 'Your library starts here'),
        h('p', {}, all.length ? 'Try a different search or clear your filters to see more mods.' : 'Import a legacy patch or a DKR track to add it here.'),
        all.length ? plainButton('Clear filters', () => { Object.assign(browser, newBrowser()); scheduleRender(); })
          : plainButton('Import mods', importMods, { cls: 'mods-primary', disabled: locked })));
      return out;
    }
    const grid = h('div', { class: 'mods-library-grid' });
    for (const card of shown) {
      if (card.native) { grid.append(dkrTrackCard(card)); continue; }
      const ready = compatible(card, revision);
      const canEnable = canActivate(card, characters, active, revision, locked);
      const status = card.hidden ? 'Hidden' : revision && !ready ? 'Different ROM version' : card.enabled ? 'On for next launch' : 'Off';
      const warning = revision && !ready;
      const info = characters ? 'Custom racer' : [card.vehicles & 1 ? 'Car' : '', card.vehicles & 2 ? 'Hovercraft' : '', card.vehicles & 4 ? 'Plane' : ''].filter(Boolean).join(' · ');
      const toggle = checkbox(card.enabled ? 'On' : 'Off', card.enabled, {
        disabled: locked || (!card.enabled && !canEnable),
        ariaLabel: `Enable ${card.name} for next launch`, fk: `${key}-enabled-${card.id}`,
        onChange: (enabled) => setEnabled(characters, card.id, enabled),
      });
      toggle.classList.add('mods-toggle');
      toggle.querySelector('input').setAttribute('role', 'switch');
      const noteId = `note-${key}-${card.id}`;
      toggle.querySelector('input').setAttribute('aria-describedby', !revision ? 'mods-rom-status' : noteId);
      const note = card.hidden ? 'Restore this mod in Details to enable it.'
        : revision && !ready ? 'Prepare this legacy mod for the ROM version you have loaded.' : characters && !card.enabled && active >= MAX_ACTIVE_STAGE_CHARACTERS
          ? 'Both character slots are in use. Turn one off first.' : card.details || (characters ? 'Appears in character selection when enabled.' : 'Appears below the original worlds in Track Select.');
      const prepare = !card.hidden && revision && !ready && card.review
        ? plainButton('Prepare mod', () => prepareReview(card.review), { disabled: locked, fk: `${key}-prepare-${card.id}`, cls: 'mods-link' })
        : null;
      grid.append(h('article', { class: 'mods-library-card' + (card.enabled && ready ? ' is-enabled' : '') },
        h('div', { class: 'mods-card-top' }, h('span', { class: 'mods-format-tag is-legacy', title: 'Imported from an original-ROM patch' }, 'Legacy'), h('span', { class: 'mods-badge' + (warning ? ' is-warning' : card.enabled && !card.hidden ? ' is-on' : '') }, status)),
        h('div', { class: 'mods-card-title' }, h('h3', {}, card.name),
          h('div', { class: 'mods-source', title: card.sources.join(', ') }, card.sources.join(', '))),
        h('div', { class: 'mods-vehicles' }, info, h('span', { class: 'mods-size' }, formatSize(card.managedBytes))),
        h('p', { class: 'mods-card-note', id: noteId }, note),
        prepare,
        h('div', { class: 'mods-card-footer' }, toggle,
          plainButton('Details', () => openModal({ type: 'manage', characters, id: card.id }), { fk: `${key}-manage-${card.id}`, cls: 'mods-link' }))));
    }
    out.push(grid);
    return out;
  }

  function dkrTrackCard(card) {
    const track = findLabTrack(card.trackId);
    const hd = track.hd?.installed && track.hd.enabled;
    const note = 'Installed. Available automatically in the in-game track menu.';
    return h('article', { class: 'mods-library-card is-dkr' },
      h('div', { class: 'mods-card-top' }, h('span', { class: 'mods-format-tag is-dkr', title: 'Native .dkrmap track made for DKR-R' }, 'DKR'),
        h('span', { class: 'mods-badge is-on' }, 'Installed')),
      h('div', { class: 'mods-card-title' }, h('h3', {}, card.name),
        h('div', { class: 'mods-source' }, card.author ? `By ${card.author}` : card.sources[0])),
      h('div', { class: 'mods-vehicles' }, hd ? 'HD textures · Modern graphics' : 'Modern graphics',
        card.managedBytes ? h('span', { class: 'mods-size' }, formatSize(card.managedBytes)) : null),
      h('p', { class: 'mods-card-note' }, note),
      h('div', { class: 'mods-card-footer' },
        plainButton('Details', () => openModal({ type: 'dkr-details', trackId: track.id }), { fk: 'dkr-details-' + track.id, cls: 'mods-link' })));
  }

  function trackRow(track, armed) {
    const hd = track.hd;
    const declaresPack = !!hd;
    const packReady = declaresPack && hd.installed && hd.enabled;
    // The launcher has not published the 3D texture table yet; only a running game has.
    const published = false;
    const needsRelaunch = packReady && !published;
    const isArmed = armed === track.id;

    const info = [
      text(track.name),
      text(`${track.author || 'unknown author'}  -  level assigned at launch`, 'muted'),
    ];
    if (track.textures > 0) {
      let line = `${track.textures} ${track.textures === 1 ? 'texture' : 'textures'}`;
      if (track.translucent > 0) line += `, ${track.translucent} see-through`;
      if (track.animated > 0) line += `, ${track.animated} animated`;
      info.push(text(line, 'muted'));
    }
    const action = isArmed
      ? button('STOP TESTING', {
        height: 30, full: false, cls: 'im-track-action stop', fk: 'lab-arm-' + track.id,
        onClick: () => updateLab({ armed: '' }),
      })
      : button(declaresPack && packReady ? 'PLAY IN HD' : 'RACE THIS', {
        height: 30, full: false, cls: 'im-track-action', fk: 'lab-arm-' + track.id,
        onClick: () => armTrack(track, needsRelaunch),
      });
    const rows = [h('div', { class: 'im im-track-row' }, h('div', { class: 'im-group' }, ...info), action)];

    if (declaresPack) {
      let line = 'HD textures: pack not installed';
      let tone = 'muted';
      if (packReady && published) { line = 'HD textures: ready'; tone = 'accent'; }
      else if (packReady) { line = 'HD textures: restart to load'; tone = 'warm'; }
      else if (hd.siblingMismatch || (hd.installed && !hd.digestMatches)) { line = 'HD textures: pack does not match this export'; tone = 'warm'; }
      else if (hd.installed) line = 'HD textures: pack disabled';
      const row = h('div', { class: 'im im-hd-line' }, h('div', { class: `im-text im-${tone}` }, line));
      if (hd.installed) {
        row.append(button('Manage', {
          height: 22, full: false, fk: 'lab-manage-' + track.id,
          onClick: () => openModal({ type: 'pack', trackId: track.id }),
        }));
      }
      if (hd.sourceName && (!hd.installed || hd.siblingMismatch)) row.append(button(hd.installed ? 'Update HD textures' : 'Install HD textures', {
        full: false, fk: 'lab-install-hd-' + track.id, onClick: () => installWorkingHD(track.id),
      }));
      rows.push(row);
    }
    // RESTART & PLAY IN HD only exists in the in-game overlay.
    rows.push(dummy(8));
    return rows;
  }

  function trackLabSection() {
    const out = [];
    const lab = mods().trackLab;
    const installed = lab.watched || [];
    const armed = lab.armed || '';
    const locked = !!mods().lobbyActive || lib.busy || view.pickerOpen;
    if (state.get().graphics.profile !== 'modern') out.push(h('p', { class: 'mods-alert' },
      'Testing a track uses the Modern graphics profile. Selecting a test track switches to it automatically.'));
    if (view.modernNotice) out.push(h('p', { class: 'mods-feedback', role: 'status' }, view.modernNotice));
    out.push(h('div', { class: 'mods-task-grid mods-workspace' },
      h('div', { class: 'mods-task-card' }, h('h3', {}, 'Work on a track'),
        h('p', {}, 'Keep your .dkrmap tracks in one working folder. Re-export in place, then rescan to find updates and new tracks. Remove a track from this folder to remove it from the list.'),
        h('p', {}, 'Track files stay in this folder. HD texture packs are installed separately in AppData.'),
        lab.workingFolder ? h('p', { class: 'mods-feedback' }, 'Watching: ' + lab.workingFolder) : null,
        h('div', { class: 'mods-action-row' },
          plainButton(lab.workingFolder ? 'Change folder' : 'Choose working folder', chooseWorkingFolder, { disabled: locked, fk: 'lab-folder' }),
          lab.workingFolder ? plainButton('Stop watching', () => updateLab({ workingFolder: '', watched: [], armed: '', autoBoot: false }), { disabled: locked, fk: 'lab-stop', cls: 'mods-link' }) : null))));
    if (view.trackImportStatus) out.push(h('p', { class: 'mods-feedback', role: 'status' }, view.trackImportStatus));
    out.push(h('div', { class: 'mods-library-heading' }, h('h3', {}, `Test tracks (${installed.length})`),
      plainButton('Rescan tracks', () => chooseWorkingFolder(true), { disabled: locked || !lab.workingFolder, fk: 'lab-rescan', cls: 'mods-link' })));
    if (!installed.length) out.push(h('div', { class: 'mods-empty' }, h('h3', {}, 'No test tracks yet'), h('p', {}, 'Choose the folder containing your .dkrmap tracks to start testing.')));
    else {
      out.push(h('p', { class: 'mods-tool-intro' }, 'Select a track, then launch the game. Races load that track until you stop testing or choose a course in Track Select.'));
      const tracks = h('div', { class: 'mods-lab-tracks' });
      installed.forEach((track) => {
        const entry = h('article', { class: 'mods-task-card' + (armed === track.id ? ' is-testing' : '') }, ...trackRow(track, armed).filter((node) => node.getAttribute('aria-hidden') !== 'true'));
        entry.querySelectorAll('button').forEach((btn) => { btn.disabled = locked; });
        tracks.append(entry);
      });
      out.push(tracks);
    }
    if (installed.length || lab.autoBoot || armed) out.push(h('div', { class: 'mods-launch-option' },
      checkbox('Skip the menus on every launch', lab.autoBoot, { disabled: locked, fk: 'lab-autoboot', onChange: (value) => updateLab({ autoBoot: value }) }),
      h('p', {}, 'Start directly in the selected test track as Diddy, in single player. Stays on until you turn it off. Quit a race to reach the menus; L+Z reloads the track.'),
      armed ? h('p', { class: 'mods-feedback' }, `Selected for testing: ${installed.find((track) => track.id === armed)?.name || armed}`) : null));
    return out;
  }

  function modHelpSection(modsLocked) {
    const locked = modsLocked || lib.busy;
    const out = [h('div', { class: 'mods-task-grid' },
      h('div', { class: 'mods-task-card' }, h('h3', {}, 'From import to race'),
        h('ol', {}, h('li', {}, 'Use Import mods to choose a legacy patch or a DKR track.'),
          h('li', {}, 'Load your original ROM in Play. Legacy patches need it for preparation; DKR tracks can be imported before this step.'),
          h('li', {}, 'Enable legacy tracks for Track Select. Installed DKR tracks appear automatically in the in-game track menu.'))),
      h('div', { class: 'mods-task-card' }, h('h3', {}, 'Your progress stays safe'),
        h('p', {}, 'Each enabled legacy mod set uses separate Adventure saves and Controller Paks.'),
        h('p', {}, 'Hiding a legacy mod turns it off and keeps its files. Removing it keeps your saves and original files.'))),
      helpDisclosure('ROM versions and compatibility', 'versions',
        h('p', {}, "Legacy patches need their exact source ROM. Tracks are prepared for your imported US v1.0 or v1.1 ROMs; characters are validated for either version. Native DKR tracks do not need patch preparation.")),
      h('div', { class: 'mods-action-row' },
        plainButton('Refresh library', refreshLibraries, { disabled: locked, fk: 'help-refresh' }),
        plainButton('Turn off legacy tracks', () => disableAll(false), { disabled: locked, fk: 'help-off-tracks' }),
        plainButton('Turn off all characters', () => disableAll(true), { disabled: locked, fk: 'help-off-characters' }))];
    if (lib.result) out.push(h('p', { class: 'mods-feedback', role: 'status' }, lib.result));
    out.push(h('h3', { class: 'mods-import-heading' }, 'Legacy import history'),
      h('p', { class: 'mods-tool-intro' }, 'Preparing an import again adds support for your current ROM and reinstalls removed entries. Hidden mods stay hidden.'));
    const groups = new Map();
    mods().library.imports.forEach((item, index) => {
      if (!groups.has(item.review)) groups.set(item.review, { index, items: [] });
      groups.get(item.review).items.push(item);
    });
    if (!groups.size) out.push(h('p', {}, 'No imports yet. Start with Import mods.'));
    for (const [review, group] of groups) {
      out.push(helpDisclosure(group.items.map((item) => item.name).join(', '), 'import-' + review,
        ...group.items.map((item) => h('div', { class: 'mods-import-entry' }, h('strong', {}, `${item.name} / ${item.kind}`),
          ...item.warnings.map((warning) => h('p', { class: 'mods-feedback' }, warning.replace(/Game Pak/g, 'ROM'))))),
        plainButton('Prepare this import again', () => prepareReview(review), { disabled: locked || !gamePakRevision(), fk: 'help-prepare-' + group.index })));
    }
    return out;
  }

  function buildPage() {
    const modsLocked = !!mods().lobbyActive;
    const library = mods().library;
    const revision = gamePakRevision();
    const selected = [...library.tracks, ...library.characters].filter((card) => card.enabled && !card.hidden);
    const nextTrack = findLabTrack(mods().trackLab.armed);
    const out = [h('header', { class: 'mods-page-header' },
      h('div', {}, h('h1', { id: 'page-heading' }, 'MODS / HACKS'),
        h('p', {}, 'Your tracks, racers and race modifiers.')),
      plainButton([h('span', { class: 'mods-import-symbol', 'aria-hidden': 'true' }, '+'),
        h('span', {}, 'Import mods')], importMods,
        { cls: 'mods-import-button', fk: 'import-mods', disabled: modsLocked || lib.busy || view.pickerOpen }))];
    const statusLine = h('div', { class: 'mods-rom-line', id: 'mods-rom-status' },
      h('p', {}, revision
        ? `${selected.length} legacy mods enabled. Changes apply on the next launch.`
        : 'No ROM loaded. Choose your original Diddy Kong Racing ROM to play or prepare legacy mods.'),
      h('a', { href: '#/play', class: 'mods-text-link' }, revision ? 'Go to Play' : 'Choose ROM'));
    out.push(statusLine);
    if (nextTrack) out.push(h('div', { class: 'mods-next-track' },
      h('span', {}, 'Track Lab test: ', h('strong', {}, nextTrack.name)),
      plainButton('Stop testing', () => updateLab({ armed: '', autoBoot: false }), { cls: 'mods-link', fk: 'clear-dkr-selection', disabled: modsLocked || lib.busy })));
    if (revision && selected.some((card) => !compatible(card, revision))) out.push(h('p', { class: 'mods-alert' },
      'Some legacy mods were prepared for a different ROM version. Use Prepare mod on their cards.'));
    if (modsLocked) out.push(h('p', { class: 'mods-alert', role: 'status' }, 'You are in an online lobby. Leave the lobby to change mods or Magic Codes.'));
    if (lib.busy) out.push(h('div', { class: 'mods-job', role: 'status' }, spinner(false), lib.stage + '…'));
    if (!lib.busy && !lib.succeeded && lib.result) out.push(h('p', { class: 'mods-alert', role: 'status' }, lib.result));
    const sections = [['library', 'My mods'], ['magic', 'Magic Codes'], ['trackLab', 'Track Lab'], ['details', 'Help & imports']];
    const nav = h('nav', { class: 'mods-section-nav', 'aria-label': 'Mod sections', 'data-tabs': '' });
    sections.forEach(([id, label]) => {
      const btn = plainButton(label, () => { view.section = view.section === id ? null : id; scheduleRender(); }, { fk: 'section-' + id });
      btn.id = 'mods-section-' + id;
      btn.setAttribute('aria-expanded', String(view.section === id));
      btn.setAttribute('aria-controls', 'mods-section-content');
      nav.append(btn);
    });
    out.push(nav);
    const panel = h('section', {
      id: 'mods-section-content', hidden: !view.section,
      'aria-labelledby': view.section ? 'mods-section-' + view.section : undefined,
    });
    out.push(panel);
    if (!view.section) return out;
    if (view.section === 'library') {
      const categories = h('div', { class: 'mods-categories', 'aria-label': 'Mod type', role: 'group' });
      [['tracks', 'Tracks'], ['characters', 'Characters']].forEach(([key, label]) => {
        const btn = plainButton([label, h('span', { class: 'mods-count' }, key === 'tracks' ? trackLibrary().length : library[key].length)], () => { view.category = key; scheduleRender(); }, { fk: 'category-' + key });
        btn.setAttribute('aria-pressed', String(view.category === key));
        categories.append(btn);
      });
      panel.append(h('div', { class: 'mods-library-heading' }, categories,
        plainButton('Refresh library', refreshLibraries, { cls: 'mods-link', fk: 'library-refresh', disabled: modsLocked || lib.busy })));
      if (view.category === 'tracks') panel.append(h('p', { class: 'mods-format-guide' },
        h('span', { class: 'mods-format-tag is-legacy' }, 'Legacy'), ' Original-ROM patches',
        h('span', { class: 'mods-format-tag is-dkr' }, 'DKR'), ' Native .dkrmap tracks'));
      if (view.trackImportStatus) panel.append(h('p', { class: 'mods-feedback', role: 'status' }, view.trackImportStatus));
      panel.append(...cardBrowser(view.category === 'characters', modsLocked));
      panel.append(h('p', { class: 'mods-library-help' }, 'Installed DKR tracks appear automatically in the in-game track menu. Enable legacy mods to include them. Track Lab is for development and testing.'));
    } else {
      const titles = { magic: ['Magic Codes', 'Choose a race modifier. Enabled codes apply on your next launch.'],
        trackLab: ['Track Lab', 'Create and test your own .dkrmap tracks. These tools are for track creators.'],
        details: ['Help & imports', 'Manage your library, review imports and prepare mods for a different ROM.'] };
      const [title, description] = titles[view.section];
      const content = view.section === 'magic' ? magicCodesSection(modsLocked) : view.section === 'trackLab' ? trackLabSection() : modHelpSection(modsLocked);
      panel.append(h('section', { class: 'mods-tool-section' }, h('h2', {}, title), h('p', { class: 'mods-tool-intro' }, description), ...content));
    }
    return out;
  }

  // ---------------------------------------------------------------- modals

  const dialog = document.getElementById('modal');
  const IMPORT_MODAL = { type: 'import' };
  let renderedModal = null;
  let returnFocusKey;
  let internalCloses = 0;

  function activeFocusKey() {
    const el = document.activeElement;
    return el && el.dataset ? el.dataset.fk : undefined;
  }

  // Only the first modal of a chain remembers where focus goes back to.
  function rememberReturnFocus() {
    if (!renderedModal && !lib.modal && !view.modal) returnFocusKey = activeFocusKey();
  }

  dialog.addEventListener('cancel', (event) => {
    if (!dialog.classList.contains('imgui-modal')) return;
    event.preventDefault();
    if (lib.busy) return;
    lib.modal = false;
    closeModal();
  });
  dialog.addEventListener('close', () => {
    if (internalCloses > 0) internalCloses -= 1;
    else if (dialog.classList.contains('imgui-modal')) {
      // The browser forced it shut (repeated Escape). A running import reopens on the next render.
      view.modal = null;
      if (!lib.busy) lib.modal = false;
      renderedModal = null;
    }
    if (dialog.open) return;
    if (dialog.classList.contains('imgui-modal')) {
      dialog.className = '';
      dialog.removeAttribute('style');
      dialog.setAttribute('aria-labelledby', 'modal-heading');
    }
    if (lib.modal || view.modal) scheduleRender();
  });

  function openModal(spec) {
    rememberReturnFocus();
    view.modal = spec;
    scheduleRender();
  }

  function closeModal() {
    view.modal = null;
    scheduleRender();
  }

  function findCard(characters, id) {
    return mods().library[libraryKey(characters)].find((card) => card.id === id) || null;
  }

  function importModal() {
    const body = [text(lib.busy ? 'PREPARING MODS' : 'MOD LIBRARY', null, 'im-nowrap'), dummy(8)];
    if (lib.busy) {
      body.push(h('div', { class: 'im' }, spinner(true)), text(lib.stage));
      if (lib.total) body.push(text(`Patch ${lib.completed} of ${lib.total}`, null, 'im-nowrap'));
      body.push(
        text('Library work runs in the background. Original ROMs and saves are not modified. Cancellation stops before commit; committed changes finish safely.'),
        dummy(10),
        button('CANCEL OPERATION', { fk: 'library-cancel', onClick: () => lib.cancel && lib.cancel() }));
    } else {
      body.push(text(lib.result), dummy(10), button('DONE', {
        fk: 'library-done',
        onClick: () => { lib.modal = false; scheduleRender(); },
      }));
    }
    return { title: 'Import and manage mods', width: 600, top: true, body };
  }

  function manageModal({ characters, id }) {
    const card = findCard(characters, id);
    const locked = !!mods().lobbyActive || lib.busy;
    const body = [];
    if (!card) {
      body.push(text('This mod is no longer in the installed library.'));
    } else {
      const all = mods().library[libraryKey(characters)];
      const revision = gamePakRevision();
      body.push(
        text(characters ? 'MANAGE CUSTOM CHARACTER' : 'MANAGE CUSTOM TRACK'),
        separator(),
        text(card.name),
        text(card.hidden ? 'HIDDEN' : card.enabled ? 'ACTIVE' : 'INACTIVE', card.enabled ? 'accent' : 'warm'));
      card.sources.forEach((source) => body.push(text('SOURCE PACK: ' + source)));
      body.push(
        text('PREPARED FOR: ' + (card.revisions === 3 ? 'ROM v 1.0 / v 1.1' : card.revisions === 1 ? 'ROM v 1.0' : 'ROM v 1.1')),
        text(`MANAGED SIZE: ${formatSize(card.managedBytes)} (prepared variants; retained import material excluded)`),
        card.importedAt ? text('IMPORTED: ' + formatDate(card.importedAt), null, 'im-nowrap') : text('IMPORTED: Unknown (older import)'));
      if (card.details) body.push(text(card.details, 'warm'));
      if (!characters) {
        const vehicles = (card.vehicles & 1 ? 'Car ' : '') + (card.vehicles & 2 ? 'Hovercraft ' : '') + (card.vehicles & 4 ? 'Plane' : '');
        body.push(text('VEHICLES: ' + vehicles));
      }
      body.push(dummy(8));
      if (locked) body.push(text('Return to the launcher and leave the lobby to change mods. Busy operations must finish first.', 'warm'));
      const active = activeCount(all);
      if (characters && !card.enabled && active >= MAX_ACTIVE_STAGE_CHARACTERS) {
        body.push(text('Two custom characters are already active. Deactivate one before enabling another.'));
      }
      const canEnable = canActivate(card, characters, active, revision, locked);
      body.push(
        wrappedButton(card.enabled ? 'DEACTIVATE MOD' : 'ACTIVATE MOD', {
          disabled: locked || (!card.enabled && !canEnable), fk: 'manage-activate',
          onClick: () => { closeModal(); setEnabled(characters, id, !card.enabled); },
        }),
        wrappedButton(card.hidden ? 'RESTORE TO LIBRARY' : 'HIDE FROM LIBRARY', {
          disabled: locked, fk: 'manage-hide',
          onClick: () => {
            if (card.hidden) { closeModal(); setHidden(characters, id, false); }
            else openModal({ type: 'confirm', remove: false, characters, id });
          },
        }),
        wrappedButton('REMOVE MOD...', {
          disabled: locked, fk: 'manage-remove',
          onClick: () => openModal({ type: 'confirm', remove: true, characters, id }),
        }),
        wrappedButton('OPEN MANAGED LOCATION', {
          fk: 'manage-open',
          onClick: () => ui.notify(`Opens %APPDATA%\\DKR-R\\mods\\legacy\\${characters ? 'prepared-characters' : 'prepared'}\\${card.group}\\${card.storage} (simulated).`),
        }));
      const header = h('button', {
        type: 'button', class: 'im-collapsing', 'aria-expanded': String(view.importDetailsOpen), 'data-fk': 'manage-details',
        onclick: () => { view.importDetailsOpen = !view.importDetailsOpen; scheduleRender(); },
      }, 'IMPORT DETAILS');
      body.push(header);
      if (view.importDetailsOpen) {
        body.push(
          text('CONTENT ID: ' + card.id),
          text('Source imports are retained for re-preparation. This mod uses isolated saves; hiding or removing it never deletes saves.'),
          wrappedButton('PREPARE SOURCE IMPORT AGAIN', {
            disabled: locked || !card.review, fk: 'manage-prepare',
            onClick: () => { closeModal(); prepareReview(card.review); },
          }),
          text('This explicitly restores removed entries from this source import. Existing hidden entries remain hidden.'));
      }
    }
    body.push(wrappedButton('CLOSE', { fk: 'manage-close', onClick: closeModal }));
    return { title: 'Manage custom mod', width: 540, body };
  }

  function confirmModal({ remove, characters, id }) {
    const card = findCard(characters, id);
    const locked = !!mods().lobbyActive || lib.busy;
    const body = [];
    if (card) {
      body.push(
        text(card.name),
        separator(),
        text(remove
          ? 'Remove this mod and all its prepared ROM variants? Other tracks and characters from the same pack, source files, retained import material and all saves are kept.'
          : 'Deactivate and hide this mod? Its files and saves are kept. Use Visibility: Hidden to restore the card. Restoring does not activate it.'));
      if (remove) body.push(text('Prepared content is deleted. Reimport its source patch to install it again.', 'warm'));
      body.push(wrappedButton(remove ? 'REMOVE THIS MOD' : 'DEACTIVATE AND HIDE', {
        minHeight: 44, disabled: locked, fk: 'confirm-accept',
        onClick: () => {
          closeModal();
          if (remove) removeMod(characters, id);
          else setHidden(characters, id, true);
        },
      }));
    } else {
      body.push(text('This mod is no longer installed.'));
    }
    body.push(wrappedButton('CANCEL', { minHeight: 44, fk: 'confirm-cancel', onClick: closeModal }));
    return { title: remove ? 'Remove custom mod?' : 'Hide custom mod?', width: 540, body };
  }

  function findLabTrack(id) {
    return labTracks(mods().trackLab).find((track) => track.id === id) || null;
  }

  function packModal({ trackId }) {
    const track = findLabTrack(trackId);
    const pack = track && track.hd && track.hd.installed ? track.hd : null;
    const body = [text('MANAGE TEXTURE PACK', null, 'im-nowrap'), separator()];
    if (!pack) {
      body.push(
        text("This texture pack is no longer present in DKR-R's managed library."),
        button('CLOSE', { full: false, width: '150px', fk: 'pack-close', onClick: closeModal }));
      return { title: 'Manage texture pack', width: 540, body };
    }
    const isCompatible = true;
    const status = pack.hidden ? 'HIDDEN' : pack.enabled ? 'ACTIVE' : 'INACTIVE';
    const tone = isCompatible && !pack.hidden ? 'im-accent' : 'im-warm';
    const rows = [
      ['TYPE', pack.format || 'Native RT64'],
      ['MANAGED SIZE', formatSize(pack.managedBytes || 0)],
      ['TEXTURES', String(pack.images || 0)],
      ['VISIBILITY', pack.hidden ? 'Hidden' : 'Visible'],
    ];
    const labelWidth = Math.max(...rows.map((row) => row[0].length));
    const valueWidth = Math.max(...rows.map((row) => row[1].length));
    body.push(
      text(pack.packName),
      dummy(5),
      h('div', { class: `im im-sameline ${tone}` },
        h('span', { class: 'im-text im-nowrap' }, status),
        h('span', { class: 'im-text im-nowrap' }, '  -  '),
        h('span', { class: 'im-text im-nowrap' }, isCompatible ? 'COMPATIBLE' : 'INCOMPATIBLE')),
      h('div', { class: 'im-detail-table', style: `grid-template-columns:${labelWidth}fr ${valueWidth}fr` },
        ...rows.map(([label, value]) => h('div', {},
          h('span', { class: 'im-muted' }, label),
          h('span', {}, value)))),
      text('MANAGED LOCATION', 'muted', 'im-nowrap'),
      text(`%APPDATA%\\DKR-R\\texture-packs\\${pack.packId}`),
      dummy(8));
    const locked = pack.hidden || !isCompatible;
    body.push(h('div', { class: 'im-actions', style: '--cols:2' },
      button(pack.enabled ? 'DEACTIVATE PACK' : 'ACTIVATE PACK', {
        disabled: locked, fk: 'pack-activate',
        onClick: () => updateLabTrack(trackId, (entry) => { entry.hd.enabled = !entry.hd.enabled; }),
      }),
      button(pack.hidden ? 'RESTORE TO LIBRARY' : 'HIDE FROM LIBRARY', {
        fk: 'pack-hide',
        onClick: () => updateLabTrack(trackId, (entry) => {
          entry.hd.hidden = !entry.hd.hidden;
          if (entry.hd.hidden) entry.hd.enabled = false;
        }),
      }),
      button('OPEN MANAGED LOCATION', {
        fk: 'pack-open',
        onClick: () => ui.notify('Opened the managed texture-pack location. (simulated)'),
      }),
      button('REFRESH PACK DETAILS', {
        fk: 'pack-refresh',
        onClick: () => ui.notify('Texture-pack details refreshed.'),
      }),
      button('REMOVE PACK...', {
        variant: 'dark-red', fk: 'pack-remove',
        onClick: () => openModal({ type: 'pack-remove', trackId }),
      }),
      button('CLOSE', { fk: 'pack-close', onClick: closeModal })));
    body[body.length - 1].querySelectorAll('.race-button').forEach((el) => el.classList.remove('im'));
    return { title: 'Manage texture pack', width: 540, body };
  }

  function packRemoveModal({ trackId }) {
    const track = findLabTrack(trackId);
    const name = track && track.hd ? track.hd.packName : '';
    const actions = h('div', { class: 'im-actions', style: '--cols:1' },
      button('CANCEL', { height: 44, fk: 'pack-remove-cancel', onClick: closeModal }),
      button('HIDE FROM LIST', {
        height: 44, fk: 'pack-remove-hide',
        onClick: () => {
          updateLabTrack(trackId, (entry) => { if (entry.hd) { entry.hd.hidden = true; entry.hd.enabled = false; } });
          closeModal();
        },
      }),
      button('DELETE COMPLETELY', {
        height: 44, variant: 'dark-red', fk: 'pack-remove-delete',
        onClick: () => {
          updateLabTrack(trackId, (entry) => {
            if (entry.hd) entry.hd = { installed: false, enabled: false, digestMatches: false, siblingMismatch: false };
          });
          closeModal();
        },
      }));
    actions.querySelectorAll('.race-button').forEach((el) => el.classList.remove('im'));
    return {
      title: 'Remove texture pack?',
      width: 480,
      body: [
        text('REMOVE TEXTURE PACK?', null, 'im-nowrap'),
        separator(),
        text(name),
        dummy(6),
        text("HIDE FROM LIST keeps DKR-R's managed copy on disk. Choose Hidden from the Visibility filter to restore it later."),
        dummy(8),
        text('WARNING - PERMANENT DELETION CANNOT BE UNDONE', 'red'),
        text("DELETE COMPLETELY removes DKR-R's managed archive or converted texture cache. The original source archive outside DKR-R is never touched."),
        dummy(12),
        actions,
      ],
    };
  }

  function chooseImportModal() {
    const locked = !!mods().lobbyActive || lib.busy || view.pickerOpen;
    return { title: 'Import mods', width: 680, body: [
      h('p', {}, 'Choose the format you downloaded. Both types appear in My mods.'),
      h('div', { class: 'mods-import-options' },
        h('section', { class: 'mods-import-option' },
          h('span', { class: 'mods-format-tag is-legacy' }, 'Legacy'), h('h3', {}, 'ROM patches'),
          h('p', {}, '.xdelta files or ZIP packs containing legacy tracks and characters.'),
          !gamePakRevision() ? h('p', {}, 'Load the original ROM first so the patch can be prepared.', ' ',
            h('a', { href: '#/play', class: 'mods-text-link', onclick: closeModal }, 'Choose ROM')) : null,
          plainButton('Choose patch file', importLegacyFile, { disabled: locked || !gamePakRevision(), fk: 'import-legacy' })),
        h('section', { class: 'mods-import-option' },
          h('span', { class: 'mods-format-tag is-dkr' }, 'DKR'), h('h3', {}, 'DKR-R tracks'),
          h('p', {}, 'A .dkrmap folder or a ZIP of a native track. You can import it before loading a ROM.'),
          h('div', { class: 'mods-action-row' },
            plainButton('Choose .dkrmap folder', importTrackCopy, { disabled: locked, fk: 'import-dkr-folder' }),
            plainButton('Choose track ZIP', importDKRArchive, { disabled: locked, fk: 'import-dkr-zip' })))),
      plainButton('Cancel', closeModal, { fk: 'import-choice-cancel', cls: 'mods-link' }),
    ]};
  }

  function dkrDetailsModal({ trackId }) {
    const track = findLabTrack(trackId);
    if (!track) return { title: 'DKR track', width: 540, body: [text('This track is no longer in the library.'), plainButton('Close', closeModal)] };
    const installed = (mods().trackLab.tracks || []).some((entry) => entry.id === trackId);
    const locked = !!mods().lobbyActive || lib.busy || view.pickerOpen;
    const body = [h('span', { class: 'mods-format-tag is-dkr' }, 'DKR'),
      h('h3', { class: 'mods-native-title' }, track.name),
      text(track.author ? 'By ' + track.author : 'Native .dkrmap track'),
      text(installed ? 'Installed. This course appears automatically in the in-game track menu.'
        : 'Read from your working folder. Source files are managed in Track Lab.'),
      text('Use Track Lab to test work in progress from your working folder. Native tracks use the Modern graphics profile.')];
    if (!state.get().rom.ready) body.push(text('Load your original Diddy Kong Racing ROM in Play before racing.'));
    if (track.hd?.installed) body.push(plainButton('Manage HD textures', () => openModal({ type: 'pack', trackId }), { fk: 'dkr-hd-' + trackId }));
    if (installed) body.push(button('UNINSTALL TRACK', {
      height: 44, variant: 'dark-red', disabled: locked, fk: 'dkr-uninstall-' + trackId,
      onClick: () => openModal({ type: 'dkr-uninstall', trackId }),
    }));
    else body.push(text('To remove this track, move its .dkrmap out of your working folder and rescan, or stop watching that folder in Track Lab.'));
    if (locked) body.push(text('Leave the lobby to change tracks. Wait for any current import to finish.', 'warm'));
    body.push(plainButton('Close', closeModal, { fk: 'dkr-detail-close', cls: 'mods-link' }));
    return { title: 'Track details', width: 570, body };
  }

  function dkrUninstallModal({ trackId }) {
    const track = (mods().trackLab.tracks || []).find((entry) => entry.id === trackId);
    const locked = !!mods().lobbyActive || lib.busy || view.pickerOpen;
    return { title: 'Uninstall track?', width: 540, body: [
      text(track ? track.name : 'This track is no longer installed.'),
      ...(track ? [text('Delete the installed .dkrmap copy from DKR-R? Original source files, saves and HD texture packs will be kept. You can import the track again later.'),
        button('UNINSTALL TRACK', { height: 44, variant: 'dark-red', disabled: locked, fk: 'dkr-uninstall-confirm',
          onClick: () => { closeModal(); uninstallTrack(trackId); } })] : []),
      button('CANCEL', { height: 44, fk: 'dkr-uninstall-cancel', onClick: () => openModal({ type: 'dkr-details', trackId }) }),
    ] };
  }

  function buildModal(spec) {
    switch (spec.type) {
      case 'choose-import': return chooseImportModal();
      case 'dkr-details': return dkrDetailsModal(spec);
      case 'dkr-uninstall': return dkrUninstallModal(spec);
      case 'import': return importModal();
      case 'manage': return manageModal(spec);
      case 'confirm': return confirmModal(spec);
      case 'pack': return packModal(spec);
      case 'pack-remove': return packRemoveModal(spec);
      default: return null;
    }
  }

  // Returns the focus key to restore when a modal chain has just closed.
  function renderModal() {
    // Another page's confirmation owns the shared dialog; render again once it closes.
    if (dialog.open && !dialog.classList.contains('imgui-modal')) return undefined;
    const spec = lib.modal ? IMPORT_MODAL : view.modal;
    if (!spec) {
      if (!renderedModal) return undefined;
      renderedModal = null;
      if (dialog.open) {
        internalCloses += 1;
        dialog.close();
      }
      const key = returnFocusKey;
      returnFocusKey = undefined;
      return key;
    }
    const built = buildModal(spec);
    const previousBody = dialog.querySelector('.im-modal-body');
    const scrollTop = renderedModal && renderedModal.type === spec.type && previousBody ? previousBody.scrollTop : 0;
    dialog.className = ['imgui-modal', built.top && 'im-top', built.height && 'fixed-height'].filter(Boolean).join(' ');
    dialog.style.setProperty('--im-modal-width', built.width + 'px');
    if (built.height) dialog.style.setProperty('--im-modal-height', built.height + 'px');
    else dialog.style.removeProperty('--im-modal-height');
    dialog.setAttribute('aria-labelledby', 'im-modal-title');
    const body = h('div', { class: 'im-modal-body' }, ...built.body);
    dialog.replaceChildren(h('div', { class: 'im-titlebar', id: 'im-modal-title' }, built.title), body);
    if (!dialog.open) dialog.showModal();
    body.scrollTop = scrollTop;
    if (renderedModal !== spec) {
      const first = body.querySelector('input:not([type="checkbox"]), button:not(:disabled)');
      if (first) first.focus({ preventScroll: true });
    }
    renderedModal = spec;
    return undefined;
  }

  // ---------------------------------------------------------------- render

  // The page is rebuilt on every change, so CSS transitions never run. Mark the nodes whose
  // visible state just changed and let one-shot keyframes play from the previous state.
  function markChanges(toggleStates) {
    for (const input of pageRoot.querySelectorAll('.mods-toggle input[data-fk]')) {
      const before = toggleStates.get(input.dataset.fk);
      if (before !== undefined && before !== input.checked) input.classList.add('is-flipped');
    }
    const now = { section: view.section, category: view.category, filtersOpen: view.filtersOpen };
    const before = shownView;
    shownView = now;
    if (!before) return;
    if (now.section !== before.section) {
      for (const id of [before.section, now.section]) {
        if (id) pageRoot.querySelector('#mods-section-' + id)?.classList.add('is-toggled');
      }
    }
    const panel = pageRoot.querySelector('#mods-section-content');
    const switched = now.section !== before.section || now.category !== before.category;
    if (now.section && switched) {
      const chunks = panel.querySelectorAll(':scope > :not(.mods-tool-section, .mods-library-grid), ' +
        ':scope > .mods-tool-section > *, .mods-library-grid > *');
      chunks.forEach((chunk, i) => {
        chunk.style.setProperty('--enter-i', Math.min(i, 7));
        chunk.classList.add('is-entering');
      });
    } else if (now.filtersOpen && !before.filtersOpen) {
      pageRoot.querySelector('#mods-filters')?.classList.add('is-entering');
    }
  }

  function render() {
    renderQueued = false;
    const focusKey = activeFocusKey() || (document.activeElement === document.body ? pendingFocusKey : undefined);
    pendingFocusKey = undefined;
    const focusedInput = document.activeElement;
    const selection = focusedInput?.matches('input[type=search], input[type=text]')
      ? [focusedInput.selectionStart, focusedInput.selectionEnd] : null;
    if (pageRoot && pageRoot.isConnected) {
      const toggleStates = new Map([...pageRoot.querySelectorAll('.mods-toggle input[data-fk]')]
        .map((input) => [input.dataset.fk, input.checked]));
      pageRoot.replaceChildren(...buildPage());
      markChanges(toggleStates);
    } else {
      pageRoot = null;
    }
    const restoreKey = renderModal() || focusKey;
    if (restoreKey) {
      const target = document.querySelector(`[data-fk="${CSS.escape(restoreKey)}"]`) ||
        (!dialog.open && pageRoot?.querySelector('.mods-section-nav [aria-expanded="true"], .mods-section-nav button'));
      if (target?.disabled && lib.busy) pendingFocusKey = restoreKey;
      if (target && document.activeElement !== target && !target.disabled) target.focus({ preventScroll: true });
      if (target && selection && target.setSelectionRange) target.setSelectionRange(...selection);
    }
  }

  function scheduleRender() {
    if (renderQueued) return;
    renderQueued = true;
    queueMicrotask(render);
  }

  state.subscribe((_, section) => {
    if (section === 'mods' || section === 'rom' || section === 'graphics') scheduleRender();
  });

  // The open section is kept between visits, so LT / RT back to Mods lands where you left it.
  DKRLauncher.pages.mods = function (container) {
    view.filtersOpen = false;
    view.detailsOpen.clear();
    view.importDetailsOpen = false;
    shownView = null;
    pageRoot = h('div', { class: 'mods-page' });
    container.append(pageRoot);
    render();
  };
})();
