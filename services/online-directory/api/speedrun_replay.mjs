import { MAX_REPLAY_BYTES } from '../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js';

export async function readReplayBody(request) {
  if (!request.body || request.headers.get('Content-Type') !== 'application/octet-stream' ||
      Number(request.headers.get('Content-Length')) > MAX_REPLAY_BYTES) return null;
  const reader = request.body.getReader();
  const bytes = new Uint8Array(MAX_REPLAY_BYTES);
  let length = 0;
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) return bytes.subarray(0, length);
      if (value.byteLength > bytes.length - length) {
        await reader.cancel();
        return null;
      }
      bytes.set(value, length);
      length += value.byteLength;
    }
  } finally {
    reader.releaseLock();
  }
}
