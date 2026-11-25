import { __dkrFtps } from './library_api.mjs';
import { MAX_REPLAY_BYTES } from '../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js';
import { MAX_GHOST_BYTES } from './speedrun_ghost.mjs';

const directory = __dkrFtps.FTPS_ROOTS.download + '/Replays';
const encoder = new TextEncoder();

export const ftpReplayEnabled = env => env.SPEEDRUN_FTP === 'true';
export const secretValid = secret => typeof secret === 'string' && /^[a-f0-9]{64}$/.test(secret);

function name(key) {
  const match = /^runs\/([A-Za-z0-9_-]{24})\.(dkrreplay|dkrghost)$/.exec(key);
  if (!match) throw new Error('Invalid replay storage key.');
  return (match[2] === 'dkrghost' ? 'dkr-ghost-' : 'dkr-run-') + match[1] + '.bin';
}

async function cipher(secret) {
  const raw = Uint8Array.from(secret.match(/../g), value => Number.parseInt(value, 16));
  return crypto.subtle.importKey('raw', raw, 'AES-GCM', false, ['encrypt', 'decrypt']);
}

export function replayStorage(env, { createClient = __dkrFtps.createFtpsClient } = {}) {
  if (!ftpReplayEnabled(env)) return env.SPEEDRUN_REPLAYS;
  if (!secretValid(env.SPEEDRUN_FTP_KEY) || !env.FTP_USERNAME || !env.FTP_PASSWORD) return null;

  const session = work => createClient({
    username:env.FTP_USERNAME, password:env.FTP_PASSWORD, timeoutMs:20000,
  }).withSession(work);

  const sizeLimit = file => file.startsWith('dkr-ghost-') ? MAX_GHOST_BYTES : MAX_REPLAY_BYTES;

  async function readArchive(ftp, filename) {
    const path = directory + '/' + filename;
    const size = await ftp.size(path);
    if (size === null) return null;
    if (size < 29 || size > sizeLimit(filename) + 28) throw new Error('Invalid replay archive size.');
    const encrypted = await ftp.readRange(path, 0, size);
    const bytes = await crypto.subtle.decrypt({
      name:'AES-GCM', iv:encrypted.subarray(0,12), additionalData:encoder.encode(filename),
    }, await cipher(env.SPEEDRUN_FTP_KEY), encrypted.subarray(12));
    return {size:bytes.byteLength, body:bytes, arrayBuffer:async () => bytes};
  }

  return {
    async get(key) {
      const filename = name(key);
      return session(ftp => readArchive(ftp, filename));
    },
    async put(key, bytes) {
      const filename = name(key);
      if (bytes.byteLength < 1 || bytes.byteLength > sizeLimit(filename)) throw new Error('Invalid replay length.');
      const path = directory + '/' + filename;
      const temp = path + '.part';
      const iv = crypto.getRandomValues(new Uint8Array(12));
      const ciphertext = await crypto.subtle.encrypt({
        name:'AES-GCM', iv, additionalData:encoder.encode(filename),
      }, await cipher(env.SPEEDRUN_FTP_KEY), bytes);
      const encrypted = new Uint8Array(iv.byteLength + ciphertext.byteLength);
      encrypted.set(iv);
      encrypted.set(new Uint8Array(ciphertext),iv.byteLength);
      await session(async ftp => {
        if (await ftp.size(path) === null) {
          if (await ftp.size(temp) !== null) await ftp.remove(temp);
          await ftp.append(temp, 0, encrypted, encrypted.length);
          await ftp.rename(temp, path);
        }
        const archive = await readArchive(ftp, filename);
        const stored = archive && new Uint8Array(await archive.arrayBuffer());
        if (!stored || stored.length !== bytes.length || !stored.every((byte,index) => byte === bytes[index]))
          throw new Error('Stored replay did not match the uploaded bytes.');
      });
    },
    async delete(keys) {
      await session(async ftp => {
        for (const key of keys) {
          const path = directory + '/' + name(key);
          for (const candidate of [path, path + '.part']) {
            if (await ftp.size(candidate) !== null) await ftp.remove(candidate);
          }
        }
      });
    },
  };
}


