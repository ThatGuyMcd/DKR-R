import assert from 'node:assert/strict';
import { createCipheriv, randomBytes } from 'node:crypto';
import { archiveName, probeHostedVideo, serveHostedVideo, VIDEO_CHUNK } from '../api/speedrun_video.mjs';

const bytes = randomBytes(VIDEO_CHUNK + 101);
bytes.write('ftyp',4);
const key = randomBytes(32);
const object = {
  id:'abcdefghijklmnopqrstuvwx',replaySha256:'a'.repeat(64),videoSha256:'b'.repeat(64),size:bytes.length,
};
const encrypted = Buffer.concat([bytes.subarray(0,VIDEO_CHUNK),bytes.subarray(VIDEO_CHUNK)].map((chunk,i) => {
  const iv = randomBytes(12);
  const cipher = createCipheriv('aes-256-gcm',key,iv);
  cipher.setAAD(Buffer.from(JSON.stringify([object.id,object.replaySha256,object.videoSha256,i,chunk.length])));
  return Buffer.concat([iv,cipher.update(chunk),cipher.final(),cipher.getAuthTag()]);
}));
const server = damaged => async (url, options) => {
  assert.ok(url.endsWith('/Replays/'+archiveName(object.id,object.videoSha256)));
  assert.equal(options.redirect,'manual');
  const [,start,end] = /^bytes=(\d+)-(\d+)$/.exec(options.headers.Range).map(Number);
  const reply = Buffer.from(encrypted.subarray(start,end+1));
  if (damaged) reply[reply.length-1] ^= 1;
  return new Response(reply,{status:206,headers:{'Content-Range':`bytes ${start}-${end}/${encrypted.length}`}});
};
const endpoint = 'https://example.test/video';
const read = (headers = {}, method = 'GET', damaged = false) => serveHostedVideo(
  new Request(endpoint,{headers,method}),key.toString('hex'),object,server(damaged));
assert.equal(await probeHostedVideo(key.toString('hex'),object,server(false)),true);
await assert.rejects(probeHostedVideo(key.toString('hex'),object,server(true)));
const entire = await read();
assert.equal(entire.status,200);
assert.deepEqual(Buffer.from(await entire.arrayBuffer()),bytes);
const aroundBoundary = await read({Range:`bytes=${VIDEO_CHUNK-12}-${VIDEO_CHUNK+23}`});
assert.equal(aroundBoundary.status,206);
assert.deepEqual(Buffer.from(await aroundBoundary.arrayBuffer()),bytes.subarray(VIDEO_CHUNK-12,VIDEO_CHUNK+24));
const tail = await read({Range:'bytes=-79'});
assert.deepEqual(Buffer.from(await tail.arrayBuffer()),bytes.subarray(bytes.length-79));
assert.equal((await read({Range:`bytes=${bytes.length}-`})).status,416);
assert.equal((await read({},'HEAD')).headers.get('Content-Length'),String(bytes.length));
await assert.rejects(async () => (await read({Range:'bytes=-79'},'GET',true)).arrayBuffer());
console.log('Encrypted hosted video ranges, seeks and chunk integrity passed.');
