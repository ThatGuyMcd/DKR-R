window.DKRLauncher = window.DKRLauncher || {};

(function () {
  const STORAGE_KEY = 'dkr-launcher-state';

  // Deterministic stand-in for the SHA-256 content and review identities the runtime shows.
  function mockDigest(seed) {
    let hash = 0x811c9dc5;
    let out = '';
    for (let round = 0; out.length < 64; round += 1) {
      for (const ch of seed + '#' + round) {
        hash ^= ch.charCodeAt(0);
        hash = Math.imul(hash, 0x01000193) >>> 0;
      }
      out += hash.toString(16).padStart(8, '0');
    }
    return out.slice(0, 64);
  }

  function mockMod(kind, name, source, extra) {
    return Object.assign({
      id: mockDigest(kind + ':' + name),
      name,
      enabled: false,
      hidden: false,
      revisions: 3,
      sources: [source],
      managedBytes: 0,
      importedAt: 0,
      details: '',
      vehicles: kind === 'Track' ? 7 : 0,
      review: mockDigest('review:' + source),
      group: mockDigest('group:' + source).slice(0, 16),
      storage: name.toLowerCase().replace(/[^a-z0-9]+/g, '-'),
    }, extra);
  }

  function mockImport(kind, name, source, warnings) {
    return { name, kind, warnings: warnings || [], review: mockDigest('review:' + source) };
  }

  DKRLauncher.mock = { digest: mockDigest, mod: mockMod, importItem: mockImport };

  const defaults = {
    // catalog: every ROM the launcher has accepted, so racers can flip between v 1.0 and v 1.1.
    rom: { path: '', ready: false, catalog: [] },
    // uiScale: 'auto' or a fixed step such as '2.5' (js/display.js).
    display: { uiScale: 'auto' },
    graphics: {
      resolution: '1920x1080',
      windowMode: 'borderless',
      renderer: 'rt64',
      vsync: true,
      hudSize: 100,
      fpsOverlay: false,
      maxVehicleDetail: true,
      keepHubScenery: true,
      keepTrackScenery: true,
      ultrawideGuard: false,
      crt: false,
      profile: 'accurate',
    },
    // Native defaults: every volume at 100%, a flat EQ (dB). Mockup-only: muted keeps each level so
    // unmuting brings it back; eqCustom is the last hand-made EQ curve, kept when a preset is picked.
    sound: {
      master: 100, music: 100, sfx: 100, vehicles: 100, nature: 100,
      muted: { master: false, music: false, sfx: false, vehicles: false, nature: false },
      bass: 0, mid: 0, treble: 0, eqCustom: null, restore34PlayerMusic: false,
    },
    controls: {
      gyro: false,
      invertX: false,
      invertY: false,
      quickRestart: true,
      backgroundInput: false,
      bindings: {
        Accelerate: 'A', Brake: 'B', Drift: 'R', 'Use item': 'Z',
        'Look behind': 'C-Down', Honk: 'D-Up', Pause: 'Start',
      },
    },
    saves: { activeTab: 'adventure' },
    textures: {
      packs: [
        { id: 'hd-characters', name: 'HD Characters (example)', enabled: true },
        { id: 'hd-tracks', name: 'HD Tracks (example)', enabled: false },
      ],
    },
    mods: {
      // Simulation switch: true behaves like an active online lobby (mods and Magic Codes locked).
      lobbyActive: false,
      magicCodes: [],
      library: {
        // revisions: 1 = Game Pak v 1.0, 2 = v 1.1, 3 = both. vehicles: 1 car, 2 hovercraft, 4 plane.
        // importedAt is Unix seconds; 0 reads as "Unknown (older import)".
        tracks: [
          mockMod('Track', 'Crystal Caverns', 'Frozen Isles Pack.zip', { enabled: true, managedBytes: 19293798, importedAt: 1785628800 }),
          mockMod('Track', 'Glacier Pass', 'Frozen Isles Pack.zip', { managedBytes: 17146470, importedAt: 1785628800, vehicles: 3, details: 'Weather effects use the stock Snowflake Mountain palette.' }),
          mockMod('Track', 'Lava Lagoon', 'lava_lagoon_v2.xdelta', { revisions: 1, managedBytes: 8830156, importedAt: 1786924800, vehicles: 1, details: 'Car-only course. Hovercraft and plane selections fall back to the car.' }),
          mockMod('Track', 'Sky Garden Speedway', 'Sky Garden.zip', { enabled: true, revisions: 2, managedBytes: 11520000, vehicles: 5 }),
          mockMod('Track', 'Old Mill Circuit', 'Old Mill Circuit.xdelta', { hidden: true, managedBytes: 6400000, importedAt: 1783036800, vehicles: 1 }),
        ],
        characters: [
          mockMod('Character', 'Captain Kiwi', 'Kiwi Racer.xdelta', { enabled: true, managedBytes: 2310144, importedAt: 1786320000 }),
          mockMod('Character', 'Robo Rex', 'Robo Rex Character.zip', { managedBytes: 3102720, importedAt: 1787011200, details: 'Uses the stock Krunch voice set.' }),
        ],
        imports: [
          mockImport('Track', 'Crystal Caverns', 'Frozen Isles Pack.zip'),
          mockImport('Track', 'Glacier Pass', 'Frozen Isles Pack.zip', ['Custom music is not supported; the stock course music is used.']),
          mockImport('Track', 'Lava Lagoon', 'lava_lagoon_v2.xdelta', ['Prepared for Game Pak v 1.0 only.']),
          mockImport('Track', 'Sky Garden Speedway', 'Sky Garden.zip'),
          mockImport('Track', 'Old Mill Circuit', 'Old Mill Circuit.xdelta'),
          mockImport('Character', 'Captain Kiwi', 'Kiwi Racer.xdelta'),
          mockImport('Character', 'Robo Rex', 'Robo Rex Character.zip', ['Menu portrait was not found; a generic portrait is used.']),
        ],
        // Removed cards stay here so PREPARE THIS IMPORT AGAIN can reinstall them.
        removed: [],
      },
      trackLab: {
        workingFolder: '',
        watched: [],
        autoBoot: false,
        armed: '',
        tracks: [
          {
            id: 'coral_coast', name: 'Coral Coast', author: 'TrackSmith', textures: 128, translucent: 12, animated: 4,
            hd: { installed: true, enabled: true, digestMatches: true, siblingMismatch: false, packId: 'coral_coast-hd', packName: 'coral_coast-hd', format: 'Native RT64', managedBytes: 51032064, images: 312, hidden: false },
          },
          { id: 'desert_loop_wip', name: 'Desert Loop WIP', author: '', textures: 16, translucent: 0, animated: 0, hd: null },
          {
            id: 'snow_test', name: 'Snow Test', author: 'TrackSmith', textures: 40, translucent: 3, animated: 0,
            hd: { installed: false, enabled: false, digestMatches: false, siblingMismatch: false },
          },
        ],
      },
    },
    online: {
      nickname: 'Player',
      friendCode: 'DKR-7K2Q-94XM',
      controlProfile: 'Controller 1',
      appearOffline: false,
      allowInvites: true,
      notifyFriendOnline: true,
      lobbyLocked: false,
      roomName: 'DKR-R Grand Prix',
      displayName: 'Player',
      identityLabel: 'RACER-7K2Q',
      localProfile: 'Player 1 profile',
      alertPosition: 'Top right',
      hostSettings: { menuOwnership: 'Host guides menus until character select', maximumRacers: 2,
        synchronization: 'Rollback', rollbackWindow: 10, automaticDelay: true, inputDelay: 2, recordReplay: true },
      saveSeed: 'Copy my single-player save',
      previousOnlineSave: false,
      overlays: { network: false, networkPosition: 'Top left', detail: 'Standard', singleRow: false,
        controller: false, controllerPosition: 'Bottom left' },
      friendCodeLifetime: 'Permanent',
      friendCodeMinutes: 60,
      generatedCodes: [],
      dismissedInvites: [],
      guideAcknowledged: false,
      friends: [
        { name: 'Banjo64', status: 'online' },
        { name: 'TipTupTina', status: 'online' },
        { name: 'TrackyMcTrackface', status: 'offline' },
        { name: 'DrumstickDan', status: 'offline' },
        { name: 'PitStopPete', status: 'pending' },
      ],
    },
    about: { diagnosticLogging: false, crashDumps: true },
  };

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function mergeDefaults(target, source) {
    for (const key of Object.keys(source)) {
      const value = source[key];
      if (value && typeof value === 'object' && !Array.isArray(value)) {
        target[key] = mergeDefaults(target[key] && typeof target[key] === 'object' ? target[key] : {}, value);
      } else if (!(key in target)) {
        target[key] = value;
      }
    }
    return target;
  }

  function load() {
    let saved = {};
    try {
      saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}') || {};
    } catch {
      saved = {};
    }
    const merged = mergeDefaults(saved, clone(defaults));
    // Saves from before the ROM list: the loaded ROM becomes its first entry.
    const rom = merged.rom;
    if (rom.ready && rom.path && !rom.catalog.some((entry) => entry.path === rom.path)) rom.catalog.push({ path: rom.path });
    return merged;
  }

  let state = load();
  const listeners = new Set();

  function persist() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch {
      /* private mode or storage disabled: state stays in-memory for this session */
    }
  }

  function get() {
    return state;
  }

  function update(section, patch) {
    state[section] = Object.assign({}, state[section], patch);
    persist();
    listeners.forEach((fn) => fn(state, section));
  }

  function subscribe(fn) {
    listeners.add(fn);
    return () => listeners.delete(fn);
  }

  function reset(section) {
    state[section] = clone(defaults[section]);
    persist();
    listeners.forEach((fn) => fn(state, section));
  }

  DKRLauncher.state = { get, update, subscribe, reset, defaults };
})();
