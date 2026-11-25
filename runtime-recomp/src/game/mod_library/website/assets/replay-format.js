// DKRREPL2 stores one applied controller sample per 30 Hz update. The race
// result ends at finishTick, and optional neutral input extends playback.
// The file is untrusted evidence. Only native re-simulation can verify its claims.
export const MAX_REPLAY_BYTES = 3 * 1024 * 1024;
export const ADVENTURE_RULES = 'adventure-us80-7';
const originalRouteRules = 'adventure-us80-6';
const milestoneRules = 'adventure-us80-5';
const oldAdventureRules = [originalRouteRules,milestoneRules,'adventure-us80-4','adventure-us80-3','adventure-us80-2','adventure-us80-1'];
const tracksCategories = ['all-trophy-races-dino-first','all-trophy-races-ffl-first','car-percent','hover-percent','plane-percent'];
export const adventureCategories = ['any-percent','100-percent','all-trophy-races',
  'all-bosses','all-minigames','tt-amulet-no-wrong-warp','any-percent-glitchless',...tracksCategories];
const adventureKeys = new Set(['board','engine','ruleset','releaseHash','rom','platform','arch','ticks',
  'finishTick','clockTicks','startSeed','checkpoints','notes','ticket','practice','category','splits']);
export const replayHash = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
export const replayId = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,96}$/.test(value);
const decoder = new TextDecoder('utf-8', { fatal: true });
const manifestKeys = new Set(['board', 'engine', 'ruleset', 'releaseHash', 'rom', 'platform', 'arch',
  'ticks', 'clockTicks', 'startSeed', 'checkpoints', 'splits', 'notes']);
const rankedKeys = new Set([...manifestKeys, 'ticket', 'practice', 'racer', 'finishTick', 'course', 'vehicle', 'trackId', 'trackHash', 'laps']);
export const CUSTOM_RULES = 'custom-time-trial-us80-1';
export const CUSTOM_VARIABLE_LAPS_RULES = 'custom-time-trial-us80-2';
export const ancientLakeRacers = [
  ['ancient-lake-car-krunch', 'Krunch'], ['ancient-lake-car-bumper', 'Bumper'],
  ['ancient-lake-car-tiptup', 'Tiptup'], ['ancient-lake-car-conker', 'Conker'],
  ['ancient-lake-car-timber', 'Timber'], ['ancient-lake-car-banjo', 'Banjo'],
  ['ancient-lake-car-drumstick', 'Drumstick'], ['ancient-lake-car-pipsy', 'Pipsy'],
  ['ancient-lake-car-tt', 'T.T.'], ['ancient-lake-car', 'Diddy']
];
export const rankedTicket = value => typeof value === 'string' && /^[A-Za-z0-9_-]{24}\.[0-9]{1,12}\.[a-f0-9]{64}$/.test(value);
const tick = (value, maximum) => Number.isInteger(value) && value > 0 && value <= maximum;

export function parseReplay(bytes) {
  const invalid = () => { throw new Error('Choose a valid DKR-R replay made with a supported build.'); };
  if (bytes.byteLength < 16 || bytes.byteLength > MAX_REPLAY_BYTES ||
      !['DKRREPL1', 'DKRREPL2'].includes(decoder.decode(bytes.subarray(0, 8)))) invalid();
  const native = bytes[7] === 50;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const size = view.getUint32(8, true);
  if (size < 2 || size > 65536 || size > bytes.byteLength - 16) invalid();
  const manifest = JSON.parse(decoder.decode(bytes.subarray(12, 12 + size)));
  if (native && [ADVENTURE_RULES, ...oldAdventureRules].includes(manifest?.ruleset)) {
    if (!validAdventure(manifest, bytes, size)) invalid();
    return manifest;
  }
  if (size > 16384 || bytes.byteLength > 1024 * 1024) invalid();
  if (native && manifest?.practice === true) {
    if (!validNative(manifest, bytes, size)) invalid();
    return manifest;
  }
  if (!manifest || Array.isArray(manifest) || typeof manifest !== 'object' ||
      Object.keys(manifest).some(key => !(native ? rankedKeys : manifestKeys).has(key)) ||
      (native && (manifest.practice !== false || !rankedTicket(manifest.ticket) ||
        !validNative(manifest, bytes, size))) ||
      !replayId(manifest.board) || !replayHash(manifest.engine) || !replayId(manifest.ruleset) ||
      (manifest.releaseHash !== null && !replayHash(manifest.releaseHash)) ||
      ![77, 80].includes(manifest.rom) || !['windows', 'linux'].includes(manifest.platform) ||
      !['x86_64', 'aarch64'].includes(manifest.arch) ||
      !tick(manifest.ticks, 216000) || !tick(manifest.clockTicks, manifest.ticks * (native ? 2 : 1)) ||
      !Number.isInteger(manifest.startSeed) || manifest.startSeed < 0 || manifest.startSeed > 0xffffffff ||
      bytes.byteLength !== 12 + size + 4 * manifest.ticks ||
      (manifest.notes !== undefined && (typeof manifest.notes !== 'string' || manifest.notes.length > 600))) invalid();
  const { ticks, clockTicks, checkpoints, splits } = manifest;
  const finish = native ? manifest.finishTick ?? ticks : ticks;
  if (!Array.isArray(checkpoints) || checkpoints.length !== Math.ceil(finish / 600)) invalid();
  if (checkpoints.some((entry, index) => !Array.isArray(entry) || entry.length !== 2 ||
      entry[0] !== Math.min((index + 1) * 600, finish) || !replayHash(entry[1]))) invalid();
  if (!Array.isArray(splits) || splits.length < 1 || splits.length > 32 ||
      splits.at(-1) !== clockTicks ||
      splits.some((value, index) => !tick(value, clockTicks) || (index && value <= splits[index - 1]))) invalid();
  return manifest;
}

function validAdventure(manifest, bytes, size) {
  const { ticks, clockTicks, checkpoints, category } = manifest;
  const splits = manifest.splits;
  const routeSplits = [ADVENTURE_RULES, originalRouteRules].includes(manifest.ruleset);
  const nativeSplits = routeSplits || manifest.ruleset === milestoneRules;
  const categories = routeSplits ? adventureCategories : adventureCategories.slice(0, nativeSplits ? 6 : 3);
  const tracks = routeSplits && (tracksCategories.includes(category) ||
    (manifest.ruleset === ADVENTURE_RULES && ['all-trophy-races','all-minigames'].includes(category)));
  const start = tracks ? 'Start' : 'Overworld';
  const uniqueLabels = new Set();
  const validSplits = nativeSplits ?
    Array.isArray(splits) && splits.length >= 2 && splits.length <= 64 &&
    splits.every((entry, i) => {
      if (!Array.isArray(entry) || entry.length !== 2 || typeof entry[0] !== 'string' ||
          !/^[A-Za-z0-9 .%-]{1,64}$/.test(entry[0]) || uniqueLabels.has(entry[0]) ||
          !tick(entry[1], clockTicks) || entry[1] % 2 || (i && entry[1] < splits[i-1][1])) return false;
      uniqueLabels.add(entry[0]);
      return true;
    }) && splits[0][0] === start && splits[0][1] === 2 && splits.at(-1)[0] === 'Finish' && splits.at(-1)[1] === clockTicks :
    splits === undefined;
  return Object.keys(manifest).every(key => adventureKeys.has(key)) &&
    validSplits && categories.includes(category) &&
    (manifest.rom === 77 || manifest.rom === 80) && manifest.startSeed === 0x444b5252 &&
    tick(ticks, 648000) && ticks === manifest.finishTick && tick(clockTicks, 2 * ticks) &&
    clockTicks % 2 === 0 && bytes.byteLength === 12 + size + 4 * ticks &&
    Array.isArray(checkpoints) && checkpoints.length === Math.ceil(ticks / 1800) &&
    checkpoints.every((entry, i) => Array.isArray(entry) && entry.length === 2 &&
      entry[0] === Math.min((i + 1) * 1800, ticks) && replayHash(entry[1])) &&
    (manifest.notes === undefined || (typeof manifest.notes === 'string' && manifest.notes.length <= 600)) &&
    (manifest.practice === true ||
      (manifest.practice === false && [ADVENTURE_RULES, ...oldAdventureRules.slice(0,4)].includes(manifest.ruleset) &&
        manifest.releaseHash === null &&
        manifest.board === 'adventure-' + category && replayHash(manifest.engine) && rankedTicket(manifest.ticket) &&
        ['windows','linux'].includes(manifest.platform) && manifest.arch === 'x86_64'));
}

function validNative(manifest, bytes, size) {
  const { ticks, clockTicks, checkpoints, splits } = manifest;
  const finish = manifest.finishTick ?? ticks;
  const racer = manifest.racer ?? 9;
  const legacy = manifest.ruleset === 'ancient-lake-us80-practice-2';
  const variable = manifest.ruleset === CUSTOM_VARIABLE_LAPS_RULES;
  const custom = variable || manifest.ruleset === CUSTOM_RULES;
  const laps = variable ? manifest.laps : 3;
  if (variable ? (!tick(laps, 5) || laps === 3) : manifest.laps !== undefined) return false;
  const vehicle = ['car','hovercraft','plane'].includes(manifest.vehicle);
  if (!custom && (manifest.trackId !== undefined || manifest.trackHash !== undefined)) return false;
  const target = custom ? manifest.course === undefined && replayId(manifest.trackId) &&
    replayHash(manifest.trackHash) && vehicle && (manifest.practice || replayHash(manifest.releaseHash)) :
    legacy ? manifest.course === undefined && manifest.vehicle === undefined :
    manifest.ruleset === 'vanilla-time-trial-us80-1' &&
    typeof manifest.course === 'string' && /^[a-z][a-z0-9-]{1,47}$/.test(manifest.course) &&
    vehicle &&
    (manifest.course !== 'ancient-lake' || manifest.vehicle !== 'car');
  return (manifest.rom === 77 || manifest.rom === 80) && manifest.startSeed === 0x444b5252 && target &&
    Number.isInteger(racer) && racer >= 0 && racer < ancientLakeRacers.length &&
    tick(finish, 54000) && tick(ticks, finish + 210) && ticks >= finish &&
    tick(clockTicks, 2 * finish) && clockTicks % 2 === 0 &&
    bytes.byteLength === 12 + size + 4 * ticks &&
    bytes.subarray(12 + size + 4 * finish).every(value => value === 0) &&
    Array.isArray(checkpoints) && checkpoints.length === Math.ceil(finish / 600) &&
    checkpoints.every((entry, i) => Array.isArray(entry) && entry.length === 2 &&
      entry[0] === Math.min((i + 1) * 600, finish) && replayHash(entry[1])) &&
    Array.isArray(splits) && splits.length === laps && splits.at(-1) === clockTicks &&
    splits.every((value, i) => tick(value, clockTicks) && value % 2 === 0 && (!i || value > splits[i - 1]));
}
