const assert = require('node:assert/strict');
const { createCipheriv, createHash, randomBytes } = require('node:crypto');

const chunkSize = 1024 * 1024;
const root = 'https://positron-portal.com/dkr-r/downloads/Replays/';

function videoArchiveFixture(secret) {
  const files = new Map();
  return {
    store(id, replaySha256, movie) {
      const videoSha256 = createHash('sha256').update(movie).digest('hex');
      const chunks = [];
      for (let offset = 0; offset < movie.length; offset += chunkSize) {
        const chunk = movie.subarray(offset, offset + chunkSize);
        const iv = randomBytes(12);
        const cipher = createCipheriv('aes-256-gcm', Buffer.from(secret, 'hex'), iv);
        cipher.setAAD(Buffer.from(JSON.stringify([id,replaySha256,videoSha256,chunks.length,chunk.length])));
        chunks.push(Buffer.concat([iv,cipher.update(chunk),cipher.final(),cipher.getAuthTag()]));
      }
      files.set(root + 'dkr-video-' + id + '-' + videoSha256 + '.bin', Buffer.concat(chunks));
      return videoSha256;
    },
    fetch(request) {
      assert.ok(request.url.startsWith(root), 'only the test archive may receive requests');
      assert.equal(request.method, 'GET');
      const bytes = files.get(request.url);
      if (!bytes) return new Response(null, {status:404});
      const range = /^bytes=(\d+)-(\d+)$/.exec(request.headers.get('Range') || '');
      assert.ok(range);
      const start = Number(range[1]), end = Number(range[2]);
      assert.ok(start <= end && end < bytes.length);
      return new Response(bytes.subarray(start,end+1), {status:206,headers:{
        'Content-Range':`bytes ${start}-${end}/${bytes.length}`,
      }});
    },
  };
}

module.exports = { videoArchiveFixture };
