// The default case checks the API contract. A supplied practice replay requires native proof.
const assert = require('node:assert/strict');
const { execFile } = require('node:child_process');
const { randomBytes } = require('node:crypto');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { promisify } = require('node:util');
const { makePilot, engine } = require('../run_local_ranked.cjs');
const { videoArchiveFixture } = require('./video_archive_fixture.cjs');
const { digest } = require('../verify_ranked.cjs');

const execute = promisify(execFile);
const category = 'any-percent';

function replay(metadata, inputs) {
  const manifest = Buffer.from(JSON.stringify(metadata));
  const header = Buffer.alloc(12);
  header.write('DKRREPL2');
  header.writeUInt32LE(manifest.length,8);
  return Buffer.concat([header, manifest, inputs]);
}

async function nativeProof(runtime, rom, evidence, ticks) {
  const windows = process.platform === 'win32';
  const temp = await fs.realpath(os.tmpdir());
  const dir = await fs.mkdtemp(path.join(temp, 'dkr-adventure-proof-'));
  assert.equal(path.dirname(await fs.realpath(dir)), temp);
  try {
    const file = path.join(dir, 'evidence.dkrreplay');
    await fs.writeFile(file, evidence, { flag:'wx', mode:0o600 });
    const seconds = Math.max(480, Math.ceil(ticks / 14) + 180);
    const args = [path.resolve(__dirname, '../../../scripts/verify_time_trial.py'), '--runtime', runtime,
      '--rom', rom, '--replay', file, '--output', path.join(dir, 'native'), '--timeout', String(seconds)];
    if (!windows) args.push('--isolate-network', '--xvfb');
    const env = { ...process.env, APPIMAGE_EXTRACT_AND_RUN:'1' };
    const { stdout } = await execute(windows ? 'python' : 'python3', args, {
      env, timeout:(seconds + 60) * 1000, windowsHide:true,
    });
    return JSON.parse(stdout);
  } finally {
    await fs.rm(dir, { recursive:true, force:true });
  }
}

async function main() {
  const [bundle, runtimePath, rom, fixture] = process.argv.slice(2);
  assert.ok(bundle && ((!runtimePath && !rom && !fixture) || (runtimePath && rom && fixture)),
    'Usage: adventure_ranked_api.cjs BUNDLE [RUNTIME ROM COMPLETED_PRACTICE_REPLAY]');
  const { ADVENTURE_RULES, adventureCategories, parseReplay } =
    await import('../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js');
  const source = fixture ? await fs.readFile(fixture) : null;
  const metadata = source ? parseReplay(source) : {
    ruleset:ADVENTURE_RULES, category, rom:80, startSeed:0x444b5252, practice:true,
    ticks:2000, finishTick:2000, clockTicks:3000, checkpoints:[[1800,'a'.repeat(64)],[2000,'b'.repeat(64)]],
    splits:[['Overworld',2],['Wizpig 2',3000],['Finish',3000]],
  };
  assert.equal(metadata.ruleset, ADVENTURE_RULES);
  assert.equal(metadata.practice, true);
  const { ticks, checkpoints } = metadata;
  const inputs = source ? source.subarray(12 + source.readUInt32LE(8)) : Buffer.alloc(ticks * 4);
  if (!source) {
    const tracks = new Set(['all-trophy-races','all-trophy-races-dino-first','all-trophy-races-ffl-first',
      'all-minigames','car-percent','hover-percent','plane-percent']);
    for (const route of adventureCategories) {
      const start = tracks.has(route) ? 'Start' : 'Overworld';
      const run = { ...metadata, category:route, splits:[[start,2],['Finish',metadata.clockTicks]] };
      assert.equal(parseReplay(replay(run,inputs)).category,route);
      run.splits[0][0] = start === 'Start' ? 'Overworld' : 'Start';
      assert.throws(() => parseReplay(replay(run,inputs)),/valid DKR-R replay/);
    }
  }
  const board = 'adventure-' + metadata.category;
  let runtime = '7'.repeat(64);
  if (runtimePath) {
    const inner = path.join(path.dirname(runtimePath), 'libexec', 'dkr-r', 'DKR-R-Runtime.exe');
    const binary = path.basename(runtimePath) === 'DKR-R.exe' && await fs.stat(inner).then(
      value => value.isFile(), () => false) ? inner : runtimePath;
    runtime = digest(await fs.readFile(binary));
  }
  const secret = randomBytes(32).toString('hex');
  const videoSecret = '6'.repeat(64);
  const archive = videoArchiveFixture(videoSecret);
  const pilot = await makePilot(path.resolve(bundle), {
    SPEEDRUN_VERIFIER_SECRET:secret, SPEEDRUN_VERIFIER_RUNTIME:runtime,
    SPEEDRUN_ADVENTURE_UPLOADS_ENABLED:'true', SPEEDRUN_FTP_VIDEO_KEY:videoSecret,
  },archive.fetch);
  const { db, mf, fetch } = pilot;
  try {
    await db.prepare("INSERT INTO speedrun_boards VALUES(?,'full-game',?,?,'mixed',?,?,NULL,NULL)")
      .bind(board,metadata.category,metadata.category,ADVENTURE_RULES,engine).run();
    const post = (url, payload, user = 'alice', type = 'application/json') =>
      fetch(url, {method:'POST',user,body:payload,headers:{'Content-Type':type}});
    const verify = (op,data) => mf.dispatchFetch('https://local-ranked.invalid/v1/speedruns/verifier/' + op, {
      method:'POST',headers:{Authorization:'Bearer '+secret,'Content-Type':'application/json'},
      body:JSON.stringify({runtime, ...data}),
    });
    const attempt = () => post('/attempts', JSON.stringify({board}));
    assert.equal((await attempt()).status,404);
    assert.equal((await (await verify('poll',{queue:'adventure'})).json()).job,null);
    const ticket = await attempt();
    assert.equal(ticket.status,200);
    const issued = await ticket.json();
    assert.deepEqual(issued.finishTargets,{title:metadata.category,personal:0,rivals:[]});
    const evidence = replay({ ...metadata, board:issued.board, engine:issued.engine, ticket:issued.ticket,
      practice:false, releaseHash:null,
      platform:metadata.platform || (process.platform === 'win32' ? 'windows' : 'linux'), arch:'x86_64' }, inputs);
    const upload = await post('/runs',evidence,'alice','application/octet-stream');
    assert.equal(upload.status,202, await upload.clone().text());
    const { run } = await upload.json();
    assert.equal((await (await verify('poll',{queue:'race'})).json()).job,null,'a long run may not occupy the race queue');
    const { job } = await (await verify('poll',{queue:'adventure'})).json();
    assert.equal(job.id,run.id);
    const stored = Buffer.from(await (await verify('evidence',job)).arrayBuffer());
    assert.deepEqual(stored,evidence);
    let proof = {
      runtimeSha256:runtime,replaySha256:digest(evidence),updates:ticks,
      category:metadata.category,clockTicks:metadata.clockTicks,checkpoints,finalCheckpoint:checkpoints.at(-1)[1],
      splits:metadata.splits,
    };
    if (fixture) {
      let renewal = Promise.resolve();
      let leaseError;
      const heartbeat = setInterval(() => {
        renewal = renewal.then(async () => {
          assert.equal((await verify('renew',job)).status,200);
        }).catch(error => { leaseError = error; });
      }, 60000);
      try {
        proof = await nativeProof(runtimePath, rom, stored, ticks);
      } finally {
        clearInterval(heartbeat);
        await renewal;
      }
      if (leaseError) throw leaseError;
    }
    const otherCategory = metadata.category === category ? '100-percent' : category;
    assert.equal((await verify('finish',{...job,proof:{...proof,category:otherCategory}})).status,409);
    assert.equal((await verify('finish',{...job,proof:{...proof,splits:[['Finish',metadata.clockTicks]]}})).status,409);
    const wrongCheckpoint = '0'.repeat(64) === proof.finalCheckpoint ? 'c'.repeat(64) : '0'.repeat(64);
    assert.equal((await verify('finish',{...job,proof:{...proof,finalCheckpoint:wrongCheckpoint}})).status,409);
    assert.equal((await verify('finish',{...job,proof})).status,200);
    const claimMovie = async queue => (await (await verify('video-claim',{queue})).json()).job;
    assert.equal((await post('/runs/'+run.id+'/review','{"decision":"published"}')).status,403);
    const publish = () => post('/runs/'+run.id+'/review','{"decision":"published"}','reviewer');
    assert.equal((await publish()).status,409);
    assert.equal(await claimMovie('race'),null,'a Full Game movie must use the separate queue');
    const movie = await claimMovie('adventure');
    assert.equal(movie.id,run.id);
    assert.equal(movie.ticks,ticks);
    assert.equal(movie.racerCredit,'alice');
    assert.equal(await claimMovie('adventure'),null,'the assigned movie has one active lease');
    const claim = {id:movie.id,claim:movie.claim};
    assert.equal((await verify('video-renew',claim)).status,200);
    const videoEvidence = await verify('video-evidence',claim);
    assert.deepEqual(Buffer.from(await videoEvidence.arrayBuffer()),evidence);
    const clip = Buffer.from('....ftyp full-game video');
    const videoSha256 = archive.store(run.id,proof.replaySha256,clip);
    const videoProof = {...proof,videoSha256,videoDuration:ticks/30+4,videoBranding:'dkr-r-logo-v2',
      videoInputOverlay:'n64-v1',videoRacerCredit:'alice'};
    assert.equal((await verify('video-complete',{...claim,bytes:clip.length,proof:videoProof})).status,409,
      'a Full Game MP4 without the live splits cannot be accepted');
    assert.deepEqual(await (await verify('video-complete',{...claim,bytes:clip.length,proof:{
      ...videoProof,videoSplitsOverlay:'adventure-live-v1',
    }})).json(),{ready:true});
    const videoRoute = '/runs/'+run.id+'/video';
    assert.equal((await fetch(videoRoute)).status,404);
    const { grant } = await (await fetch(videoRoute,{method:'POST',user:'reviewer'})).json();
    assert.deepEqual(Buffer.from(await (await fetch(videoRoute+'?review='+grant)).arrayBuffer()),clip);
    assert.equal((await publish()).status,200);
    const listing = await (await fetch('/tracks/'+metadata.category+'?kind=full-game&score=lap')).json();
    assert.equal(listing.score,'race');
    assert.equal(listing.entries[0].ticks,metadata.clockTicks);
    assert.deepEqual(listing.entries[0].splits,metadata.splits);
    assert.equal(listing.history.complete,true);
    assert.equal(listing.history.changes.length,1);
    assert.equal(listing.history.changes[0].ticks,metadata.clockTicks);
    assert.equal(listing.history.changes[0].holders[0].runId,run.id);
    assert.equal(listing.history.changes[0].holders[0].from,listing.history.changes[0].at);
    const catalogue = await (await fetch('')).json();
    const categoryRow = catalogue.adventureCatalogue.find(row => row.id === metadata.category);
    assert.deepEqual(categoryRow.leaders.mixed.race,{name:listing.entries[0].name,ticks:metadata.clockTicks});
    assert.equal(categoryRow.leaders.mixed.lap,undefined);
    const download = await fetch('/runs/'+run.id+'/replay');
    assert.equal(download.status,200);
    assert.deepEqual(Buffer.from(await download.arrayBuffer()),evidence);
    console.log(fixture ? JSON.stringify(proof) :
      'Adventure staging queue, proof matching and moderation passed with a synthetic API proof.');
  } finally {
    await mf.dispose();
  }
}

main().catch(error => {
  console.error(error);
  if (error.stderr) console.error(error.stderr);
  process.exitCode = 1;
});
