import { customRules, legacyRules, vanillaRules } from './speedrun_rules.mjs';

const signature = 'DKRPGH01';
const headerBytes = 136;
const poseBytes = 20;
export const MAX_GHOST_BYTES = headerBytes + 54000 * poseBytes;
export const ghostKey = id => 'runs/' + id + '.dkrghost';

const utf8 = new TextDecoder();
export const ghostRace = manifest => [legacyRules, vanillaRules, ...customRules].includes(manifest.ruleset);

export async function validGhost(bytes, replayDigest, manifest) {
  const frames = manifest.finishTick ?? manifest.ticks;
  if (!ghostRace(manifest) || !Number.isSafeInteger(frames) || frames < 1 ||
      bytes.length > MAX_GHOST_BYTES || bytes.length !== headerBytes + frames * poseBytes ||
      utf8.decode(bytes.subarray(0, 72)) !== signature + replayDigest)
    return false;
  const payload = bytes.subarray(headerBytes);
  const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', payload));
  const digest = Array.from(hash, byte => byte.toString(16).padStart(2, '0')).join('');
  if (utf8.decode(bytes.subarray(72, headerBytes)) !== digest) return false;
  const positions = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  for (let i = 0; i < payload.length; i += poseBytes)
    if (![0, 4, 8].every(axis => Number.isFinite(positions.getFloat32(i + axis, true)))) return false;
  return true;
}
