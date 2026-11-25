// Video is a derived artifact; ownership, publication and verification belong to the run.
export const videoKey = id => 'runs/' + id + '.mp4';
export const VIDEO_CHUNK = 1024 * 1024;
export const MAX_VIDEO_BYTES = 512 * VIDEO_CHUNK;
const mediaRoot = 'https://positron-portal.com/dkr-r/downloads/Replays/';
const hex = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const overhead = 28; // AES-GCM nonce and authentication tag.
export const videoArchiveEnabled = env => hex(env.SPEEDRUN_FTP_VIDEO_KEY);
export const archiveName = (id, sha) => 'dkr-video-' + id + '-' + sha + '.bin';
const encryptedSize = size => size + overhead * Math.ceil(size / VIDEO_CHUNK);

export function replayVideoDurationMatches(proof, ticks) {
  if (proof.videoBranding !== undefined && !['dkr-r-logo-v1','dkr-r-logo-v2'].includes(proof.videoBranding)) return false;
  const bookends = proof.videoBranding ? 4 : 0;
  return Number.isFinite(proof.videoDuration) && Math.abs(proof.videoDuration - ticks / 30 - bookends) < 0.05;
}

export function videoRacerCredit(name) {
  const clean = name.replace(/[\u0000-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069]/gu,' ')
    .replace(/\s+/gu,' ').trim();
  return Array.from(clean).slice(0,60).join('') || 'DKR-R player';
}

function videoCipher(secret) {
  if (!hex(secret)) throw new Error('Invalid video archive key.');
  return crypto.subtle.importKey('raw', Uint8Array.from(secret.match(/../g), part => parseInt(part,16)),
    'AES-GCM', false, ['decrypt']);
}

function decryptChunk(key, object, chunk, sealed) {
  const encoder = new TextEncoder();
  const aad = JSON.stringify([object.id,object.replaySha256,object.videoSha256,chunk,sealed.length-overhead]);
  return crypto.subtle.decrypt({name:'AES-GCM',iv:sealed.subarray(0,12),additionalData:encoder.encode(aad)},
    key,sealed.subarray(12));
}

async function archiveRange(object, start, end, fetchOrigin) {
  const url = mediaRoot + archiveName(object.id,object.videoSha256);
  const response = await fetchOrigin(url, {redirect:'manual',headers:{Range:`bytes=${start}-${end}`} });
  if (response.status !== 206 || response.headers.get('Content-Range') !== `bytes ${start}-${end}/${encryptedSize(object.size)}` ||
      !response.body) {
    await response.body?.cancel();
    return null;
  }
  return response;
}

export async function probeHostedVideo(secret, object, fetchOrigin = fetch) {
  const end = Math.min(VIDEO_CHUNK,object.size) + overhead - 1;
  const response = await archiveRange(object,0,end,fetchOrigin);
  if (!response) return false;
  const sealed = new Uint8Array(await response.arrayBuffer());
  if (sealed.length !== end+1) return false;
  const first = new Uint8Array(await decryptChunk(await videoCipher(secret),object,0,sealed));
  return new TextDecoder().decode(first.subarray(4,8)) === 'ftyp';
}

export async function runVideo(bucket, id, digest) {
  const object = await bucket?.head(videoKey(id));
  return object && object.customMetadata?.replaySha256 === digest && object.size > 0 ? object : null;
}

function videoResponse(request, object, get) {
  const size = object.size;
  const headers = new Headers({
    'Content-Type': 'video/mp4',
    'Cache-Control': 'private, no-store',
    'Accept-Ranges': 'bytes',
    'ETag': object.httpEtag,
  });
  const rangeHeader = request.headers.get('Range');
  let range;
  if (rangeHeader) {
    const match = /^bytes=(\d*)-(\d*)$/.exec(rangeHeader);
    const start = match?.[1] ? Number(match[1]) : null;
    const end = match?.[2] ? Number(match[2]) : null;
    if (match && start === null && Number.isSafeInteger(end) && end > 0) {
      range = { offset: Math.max(0, size - end), length: Math.min(size, end) };
    } else if (match && Number.isSafeInteger(start) && start < size &&
               (end === null || Number.isSafeInteger(end) && end >= start)) {
      range = { offset: start, length: Math.min(size - 1, end ?? size - 1) - start + 1 };
    }
    if (!range) {
      headers.set('Content-Range', 'bytes */' + size);
      return new Response(null, { status: 416, headers });
    }
    headers.set('Content-Range', 'bytes ' + range.offset + '-' + (range.offset + range.length - 1) + '/' + size);
  }
  headers.set('Content-Length', String(range?.length ?? size));
  const status = range ? 206 : 200;
  if (request.method === 'HEAD') return new Response(null, { status, headers });
  return get(range ?? {offset:0,length:size}, !!range).then(body =>
    body ? new Response(body, { status, headers }) : new Response(null, {status:404}));
}

export function serveRunVideo(request, bucket, object) {
  return videoResponse(request, object, async (range, partial) => {
    const video = await bucket.get(object.key, partial ? {range} : undefined);
    return video && video.etag === object.etag ? video.body : null;
  });
}

export async function serveHostedVideo(request, secret, object, fetchOrigin = fetch) {
  const {id, replaySha256, videoSha256, size} = object;
  if (!hex(replaySha256) || !hex(videoSha256) || !hex(secret) || !/^[A-Za-z0-9_-]{24}$/.test(id) ||
      !Number.isSafeInteger(size) || size < 1 || size > MAX_VIDEO_BYTES)
    return new Response(null, {status:404});
  return videoResponse(request, {...object, httpEtag:'"' + videoSha256 + '"'}, async range => {
    const stride = VIDEO_CHUNK + overhead;
    const first = Math.floor(range.offset / VIDEO_CHUNK);
    const last = Math.floor((range.offset + range.length - 1) / VIDEO_CHUNK);
    const total = encryptedSize(size);
    const start = first * stride;
    const end = Math.min(total, (last + 1) * stride) - 1;
    const encrypted = await archiveRange(object,start,end,fetchOrigin);
    if (!encrypted) return null;
    const reader = encrypted.body.getReader();
    const key = await videoCipher(secret);
    let chunk = first;
    let unused = new Uint8Array(0);
    return new ReadableStream({
      async pull(controller) {
        try {
          const offset = chunk * VIDEO_CHUNK;
          const length = Math.min(VIDEO_CHUNK,size-offset);
          const sealed = new Uint8Array(length + overhead);
          for (let written = 0; written < sealed.length;) {
            if (!unused.length) {
              const packet = await reader.read();
              if (packet.done) throw new Error('Truncated video archive.');
              unused = packet.value;
            }
            const copy = Math.min(unused.length, sealed.length-written);
            sealed.set(unused.subarray(0,copy),written);
            unused = unused.subarray(copy);
            written += copy;
          }
          const bytes = new Uint8Array(await decryptChunk(key,object,chunk,sealed));
          controller.enqueue(bytes.subarray(Math.max(0,range.offset-offset),Math.min(length,range.offset+range.length-offset)));
          if (chunk++ === last) {
            controller.close();
            await reader.cancel();
          }
        } catch (error) {
          controller.error(error);
          await reader.cancel().catch(() => {});
        }
      },
      cancel: () => reader.cancel(),
    });
  });
}
