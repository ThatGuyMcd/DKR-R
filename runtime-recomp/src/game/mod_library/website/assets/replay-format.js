// DKRREPL1 stores an UTF-8 manifest and one applied controller sample per 60 Hz tick.
// The file is untrusted evidence. Only native re-simulation can verify its claims.
export const MAX_REPLAY_BYTES = 1024 * 1024;
export const replayHash = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
export const replayId = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,96}$/.test(value);
const decoder = new TextDecoder('utf-8', { fatal: true });
const manifestKeys = new Set(['board', 'engine', 'ruleset', 'releaseHash', 'rom', 'platform', 'arch',
  'ticks', 'clockTicks', 'startSeed', 'checkpoints', 'splits', 'notes']);
const tick = (value, maximum) => Number.isInteger(value) && value > 0 && value <= maximum;

export function parseReplay(bytes) {
  const invalid = () => { throw new Error('Choose a valid DKR-R replay made with a supported build.'); };
  if (bytes.byteLength < 16 || bytes.byteLength > MAX_REPLAY_BYTES ||
      decoder.decode(bytes.subarray(0, 8)) !== 'DKRREPL1') invalid();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const size = view.getUint32(8, true);
  if (size < 2 || size > 16384 || size > bytes.byteLength - 16) invalid();
  const manifest = JSON.parse(decoder.decode(bytes.subarray(12, 12 + size)));
  if (!manifest || Array.isArray(manifest) || typeof manifest !== 'object' ||
      Object.keys(manifest).some(key => !manifestKeys.has(key)) ||
      !replayId(manifest.board) || !replayHash(manifest.engine) || !replayId(manifest.ruleset) ||
      (manifest.releaseHash !== null && !replayHash(manifest.releaseHash)) ||
      ![77, 80].includes(manifest.rom) || !['windows', 'linux'].includes(manifest.platform) ||
      !['x86_64', 'aarch64'].includes(manifest.arch) ||
      !tick(manifest.ticks, 216000) || !tick(manifest.clockTicks, manifest.ticks) ||
      !Number.isInteger(manifest.startSeed) || manifest.startSeed < 0 || manifest.startSeed > 0xffffffff ||
      bytes.byteLength !== 12 + size + 4 * manifest.ticks ||
      (manifest.notes !== undefined && (typeof manifest.notes !== 'string' || manifest.notes.length > 600))) invalid();
  const { ticks, clockTicks, checkpoints, splits } = manifest;
  if (!Array.isArray(checkpoints) || checkpoints.length !== Math.ceil(ticks / 600)) invalid();
  if (checkpoints.some((entry, index) => !Array.isArray(entry) || entry.length !== 2 ||
      entry[0] !== Math.min((index + 1) * 600, ticks) || !replayHash(entry[1]))) invalid();
  if (!Array.isArray(splits) || splits.length < 1 || splits.length > 32 ||
      splits.at(-1) !== clockTicks ||
      splits.some((value, index) => !tick(value, clockTicks) || (index && value <= splits[index - 1]))) invalid();
  return manifest;
}
