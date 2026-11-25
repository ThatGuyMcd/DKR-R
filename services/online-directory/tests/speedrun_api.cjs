// Local Workers, SQLite-backed D1, and private R2 integration test.
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const path = require('node:path');
const { makePilot, engine, rules } = require('../run_local_ranked.cjs');
const { videoArchiveFixture } = require('./video_archive_fixture.cjs');

const origin = 'https://positron-portal.com';
const releaseHash = 'b'.repeat(64);
const verifierDigest = 'c'.repeat(64);
const runtimeHash = '3'.repeat(64);
const verifierSecret = '4'.repeat(64);
const videoSecret = '7'.repeat(64);

function preparedGhost(replayDigest, frames = 60) {
  const data = Buffer.alloc(frames * 20);
  return Buffer.concat([Buffer.from('DKRPGH01' + replayDigest + createHash('sha256').update(data).digest('hex')), data]);
}

function replay(overrides = {}, samples = Buffer.alloc(240)) {
  const manifest = Buffer.from(JSON.stringify({
    board:'ancient-lake-car', engine, ruleset:rules, releaseHash:null, rom:80, practice:false,
    platform:'linux', arch:'x86_64', ticks:60, clockTicks:114, startSeed:0x444b5252,
    checkpoints:[[60,'0'.repeat(64)]], splits:[36,74,114], ...overrides,
  }));
  const prefix = Buffer.alloc(12);
  prefix.write('DKRREPL2');
  prefix.writeUInt32LE(manifest.length, 8);
  return Buffer.concat([prefix, manifest, samples]);
}

async function checkSubmissionLimits(root) {
  const { mf, db, fetch, addBoard } = await makePilot(root);
  try {
    await addBoard('ancient-lake-car', 'vanilla', 'Ancient Lake');
    const { submissionLimit } = await (await fetch('')).json();
    assert.deepEqual(submissionLimit, {maxRuns:25, windowHours:24});
    assert.equal((await fetch('/mine')).status,401);
    const quota = async user => {
      const response = await fetch('/mine',{user});
      assert.equal(response.status,200,await response.clone().text());
      const { submissionQuota } = await response.json();
      assert.equal(submissionQuota.maxRuns,25);
      assert.ok(submissionQuota.releasesAt.every(stamp => stamp > submissionQuota.observedAt));
      return submissionQuota;
    };
    assert.deepEqual((await quota('alice')).releasesAt,[]);
    const review = await (await fetch('/review',{user:'reviewer'})).json();
    assert.equal(review.submissionQuota,undefined);

    const newRun = async user => {
      const attempt = await fetch('/attempts', {method:'POST', user, headers:{'Content-Type':'application/json'},
        body:JSON.stringify({board:'ancient-lake-car'})});
      assert.equal(attempt.status,200,await attempt.clone().text());
      return replay({ticket:(await attempt.json()).ticket});
    };
    const post = (user, body) => fetch('/runs', {method:'POST', user, body,
      headers:{'Content-Type':'application/octet-stream'}});

    await db.prepare(`WITH RECURSIVE sample(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM sample WHERE i<24)
      INSERT INTO speedrun_runs(id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at)
      SELECT 'recent-'||i,'alice','ancient-lake-car',printf('%064x',i),'recent-attempt-'||i,'{}','',
        CASE WHEN i%2=0 THEN 'published' ELSE 'rejected' END,unixepoch()-7200,unixepoch()-7200 FROM sample`).run();
    const submissions = await Promise.all([newRun('alice'),newRun('alice')]);
    const results = await Promise.all(submissions.map(body => post('alice',body)));
    assert.deepEqual(results.map(result => result.status).sort(),[202,429]);
    const blockedIndex = results.findIndex(result => result.status === 429);
    const blocked = await results[blockedIndex].json();
    assert.equal(blocked.error.code,'submission_limit');
    assert.match(blocked.error.message,/25 runs in the last 24 hours.*opens in about/);
    const accepted = await results[1-blockedIndex].json();
    const filled = await quota('alice');
    assert.equal(filled.releasesAt.length,25);
    const seed = await db.prepare("SELECT created_at FROM speedrun_runs WHERE id='recent-1'").first();
    assert.equal(Math.min(...filled.releasesAt),seed.created_at + 86400);
    const ownRun = await (await fetch('/mine?run='+accepted.run.id,{user:'alice'})).json();
    assert.equal(ownRun.submissionQuota,undefined);
    const retry = await post('alice',submissions[1-blockedIndex]);
    assert.deepEqual(await retry.json(),accepted,'retrying the same upload needs no additional slot');
    assert.equal((await post('bob',await newRun('bob'))).status,202,'another account can still submit');
    assert.equal((await quota('bob')).releasesAt.length,1);

    await db.prepare("UPDATE speedrun_runs SET created_at=unixepoch()-86400 WHERE id='recent-1'").run();
    assert.equal((await quota('alice')).releasesAt.length,24);
    assert.equal((await post('alice',submissions[blockedIndex])).status,202,'the oldest slot opens after 24 hours');

    await db.prepare(`WITH RECURSIVE sample(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM sample WHERE i<997)
      INSERT INTO speedrun_runs(id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at)
      SELECT 'pending-'||i,'alice','ancient-lake-car',printf('%064x',i+1000),'pending-attempt-'||i,'{}','',
        'queued',unixepoch()-172800,unixepoch()-172800 FROM sample`).run();
    const queue = await post('bob',await newRun('bob'));
    assert.equal(queue.status,429);
    assert.equal((await queue.json()).error.code,'queue_limit','the shared backlog has a separate cap');
    console.log('Submission limit passed: 25th and 26th concurrent uploads, safe retry, separate accounts, rolling expiry, and shared backlog.');
  } finally {
    await mf.dispose();
  }
}

async function checkLocalClaims(root) {
  const disabled = await makePilot(root);
  try {
    assert.equal((await (await disabled.fetch('')).json()).canClaimLocal,false);
    assert.equal((await disabled.fetch('/runs/claim',{method:'POST',user:'alice'})).status,503);
  } finally {
    await disabled.mf.dispose();
  }

  const { mf, db, bucket, fetch, addBoard } = await makePilot(root,{
    SPEEDRUN_LOCAL_CLAIMS_ENABLED:'true',SPEEDRUN_CUSTOM_UPLOADS_ENABLED:'true',
    SPEEDRUN_ADVENTURE_UPLOADS_ENABLED:'true',SPEEDRUN_VERIFIER_RUNTIME:runtimeHash,
    SPEEDRUN_VERIFIER_SECRET:verifierSecret
  });
  try {
    await addBoard('ancient-lake-car','vanilla','Ancient Lake');
    const local = replay({practice:true,board:undefined,engine:undefined,releaseHash:undefined,
      platform:undefined,arch:undefined});
    const submit = (user, body = local) => fetch('/runs/claim', {method:'POST',user,body,
      headers:{'Content-Type':'application/octet-stream'}});
    assert.equal((await (await fetch('')).json()).canClaimLocal,true);
    assert.equal((await submit(undefined)).status,401);
    assert.equal((await submit('alice',replay())).status,400);
    assert.equal((await submit('alice',replay({practice:true,board:undefined,engine:undefined,releaseHash:undefined,
      racer:8}))).status,409);

    const responses = await Promise.all([submit('alice'),submit('bob')]);
    assert.deepEqual(responses.map(response => response.status).sort(),[202,409]);
    const [owner, other] = responses[0].status === 202 ? ['alice','bob'] : ['bob','alice'];
    const {run} = await responses.find(response => response.status === 202).json();
    assert.equal(run.state,'queued');
    assert.deepEqual(Buffer.from(await (await bucket.get('runs/' + run.id + '.dkrreplay')).arrayBuffer()),local);
    assert.equal((await (await submit(owner)).json()).run.id,run.id);
    const changed = replay({practice:true,board:undefined,engine:undefined,releaseHash:undefined,
      platform:undefined,arch:undefined,notes:'different metadata'});
    assert.equal((await (await submit(other,changed)).json()).error.code,'local_already_linked');
    const mine = await (await fetch('/mine',{user:owner})).json();
    assert.equal(mine.runs[0].linkedLocal,true);
    assert.equal(mine.runs[0].name,owner);
    assert.equal(mine.submissionQuota.releasesAt.length,1);
    assert.equal((await fetch('/review',{user:owner})).status,403);
    assert.deepEqual((await (await fetch('/review',{user:'reviewer'})).json()).runs,[]);
    await db.prepare('DELETE FROM speedrun_runs WHERE id=?').bind(run.id).run();
    assert.equal((await (await submit(other)).json()).error.code,'local_already_linked');

    const trackHash = '9'.repeat(64);
    const custom = replay({practice:true,board:undefined,engine:undefined,releaseHash:undefined,
      platform:undefined,arch:undefined,ruleset:'custom-time-trial-us80-1',
      vehicle:'car',trackId:'custom-course',trackHash});
    assert.equal((await submit('alice',custom)).status,409);
    await db.prepare(`INSERT INTO library_items(id,title,category,archived,current_release_id)
      VALUES('custom','Custom course','tracks',0,'release1')`).run();
    await db.prepare(`INSERT INTO library_releases(id,item_id,version) VALUES('release1','custom','1.0')`).run();
    await db.prepare("INSERT INTO launcher_release_integrity VALUES('release1',?,128,unixepoch())")
      .bind(releaseHash).run();
    await db.prepare(`INSERT INTO speedrun_custom_packages(release_id,release_sha256,track_id,track_sha256)
      VALUES('release1',?,'custom-course',?)`).bind(releaseHash,trackHash).run();
    await addBoard('track-release1-car','custom','Custom course','release1',releaseHash);
    await db.prepare("UPDATE speedrun_boards SET ruleset='custom-time-trial-us80-1' WHERE id='track-release1-car'").run();
    const uploadedCustom = await submit('alice',custom);
    assert.equal(uploadedCustom.status,202,await uploadedCustom.clone().text());
    const customId = (await uploadedCustom.json()).run.id;
    const customRow = await db.prepare('SELECT board_id,manifest FROM speedrun_runs WHERE id=?').bind(customId).first();
    assert.equal(customRow.board_id,'track-release1-car');
    assert.equal(JSON.parse(customRow.manifest).releaseHash,releaseHash);

    await db.prepare(`INSERT INTO speedrun_boards(id,kind,title,category,vehicle,ruleset,engine)
      VALUES('adventure-any-percent','full-game','Any%','any-percent','mixed','adventure-us80-7',?)`).bind(engine).run();
    const adventure = replay({practice:true,board:undefined,engine:undefined,releaseHash:undefined,
      platform:undefined,arch:undefined,ruleset:'adventure-us80-7',category:'any-percent',
      finishTick:60,splits:[['Overworld',2],['Finish',114]]});
    assert.equal((await submit('alice',adventure)).status,409);
    await db.prepare('INSERT INTO speedrun_adventure_verifier_health(runtime,seen) VALUES(?,unixepoch())')
      .bind(runtimeHash).run();
    const uploadedAdventure = await submit('alice',adventure);
    assert.equal(uploadedAdventure.status,202,await uploadedAdventure.clone().text());
    const adventureId = (await uploadedAdventure.json()).run.id;
    const adventureRow = await db.prepare('SELECT board_id FROM speedrun_runs WHERE id=?').bind(adventureId).first();
    assert.equal(adventureRow.board_id,'adventure-any-percent');
  } finally {
    await mf.dispose();
  }
}

async function main() {
  const { CUSTOM_VARIABLE_LAPS_RULES, parseReplay } =
    await import('../../../runtime-recomp/src/game/mod_library/website/assets/replay-format.js');
  const variableRace = {ruleset:CUSTOM_VARIABLE_LAPS_RULES,practice:true,vehicle:'car',
    trackId:'custom-course',trackHash:'9'.repeat(64)};
  for (const [laps,splits] of [[1,[114]],[5,[20,40,60,84,114]]]) {
    assert.equal(parseReplay(replay({...variableRace,laps,splits})).laps,laps);
    assert.throws(() => parseReplay(replay({...variableRace,laps:4,splits})));
  }
  const root = path.resolve(process.argv[2]);
  const archive = videoArchiveFixture(videoSecret);
  const { mf, db, tokens, fetch: send, addBoard } = await makePilot(root, {SPEEDRUN_VERIFIER_RUNTIME:runtimeHash,
    SPEEDRUN_VERIFIER_SECRET:verifierSecret,SPEEDRUN_FTP_VIDEO_KEY:videoSecret},archive.fetch);
  try {
    const stamp = Math.floor(Date.now() / 1000);
    const fetch = (route, options = {}) => send(route, {
      ...options, headers: { Origin: origin, ...options.headers }
    });
    const ticket = async (user, board = 'ancient-lake-car') => {
      const response = await fetch('/attempts', { method:'POST', user,
        body: JSON.stringify({ board }), headers:{'Content-Type':'application/json'} });
      assert.equal(response.status,200,await response.clone().text());
      return (await response.json()).ticket;
    };
    const post = (id, body, from = origin) => fetch('/runs',{method:'POST',user:id,body,
      headers:{Origin:from,'Content-Type':'application/octet-stream'}});
    const review = (id, user, decision, reason = '') => fetch('/runs/' + id + '/review',{method:'POST',user,
      headers:{'Content-Type':'application/json'},body:JSON.stringify({decision,reason})});
    const replayReview = (id, user, headers = {}) => fetch('/runs/' + id + '/review', {
      method:'POST',user,headers:{'Content-Type':'application/json',...headers},
      body:JSON.stringify({decision:'published',evidence:'replay'})
    });
    const submit = async (id, body) => {
      const response = await post(id,body);
      assert.equal(response.status,202,await response.clone().text());
      return (await response.json()).run;
    };
    const catalogue = async () => (await fetch('')).json();
    const deleteAccount = async id => {
      const response = await mf.dispatchFetch('https://review.invalid/v1/actions/account.delete', {
        method:'POST', headers:{Origin:origin, Authorization:'Bearer ' + tokens[id], 'Content-Type':'application/json'},
        body:JSON.stringify({input:{confirmation:'DELETE'}})
      });
      assert.equal(response.status,200,await response.clone().text());
    };
    assert.equal((await catalogue()).canSubmit,false);
    await db.prepare("INSERT INTO library_items(id,title,category,archived,current_release_id) VALUES('custom','Custom course','tracks',0,'release1')").run();
    await db.prepare("INSERT INTO library_releases(id,item_id,version) VALUES('release1','custom','1.0')").run();
    await db.prepare("INSERT INTO library_releases(id,item_id,version) VALUES('release2','custom','1.1')").run();
    await addBoard('ancient-lake-car','vanilla','Ancient Lake');
    await addBoard('custom-car','custom','Custom course','release1',releaseHash);
    let state = await catalogue();
    assert.equal(state.canSubmit,true);
    assert.equal(state.vanillaTracks.length,20);
    assert.equal(state.vanillaTracks.reduce((sum,track) => sum+track.vehicles.length,0),47);
    assert.deepEqual(state.vanillaTracks.find(track => track.id === 'whale-bay').vehicles,['hovercraft']);
    assert.equal(state.boards.length,1);
    assert.equal(state.customTracks[0].qualified,false);
    await db.prepare("INSERT INTO launcher_release_integrity VALUES('release1',?,?,?)").bind(releaseHash,128,stamp).run();
    state = await catalogue();
    assert.equal(state.boards.length,2);
    assert.equal(state.customTracks[0].qualified,false,'an archive digest alone does not qualify the track');
    await db.prepare("INSERT INTO speedrun_custom_packages(release_id,release_sha256,track_id,track_sha256) VALUES('release1',?,'custom-course',?)")
      .bind(releaseHash,'9'.repeat(64)).run();
    await db.prepare("UPDATE speedrun_boards SET id='track-release1-car',ruleset='custom-time-trial-us80-1' WHERE id='custom-car'").run();
    assert.equal((await catalogue()).customTracks[0].qualified,true);
    await db.prepare("UPDATE speedrun_custom_packages SET lap_count=5 WHERE release_id='release1'").run();
    assert.equal((await catalogue()).customTracks[0].qualified,false,'old rules cannot qualify a five-lap board');
    await db.prepare("UPDATE speedrun_boards SET ruleset=? WHERE id='track-release1-car'").bind(CUSTOM_VARIABLE_LAPS_RULES).run();
    assert.equal((await catalogue()).customTracks[0].qualified,true,'the current lap count is bound to the verified package');
    assert.equal((await fetch('/attempts', { method:'POST', user:'alice', headers:{'Content-Type':'application/json'},
      body:JSON.stringify({board:'track-release1-car'}) })).status,404,'custom intake requires its own opt-in');
    await db.prepare("UPDATE library_items SET current_release_id='release2' WHERE id='custom'").run();
    assert.equal((await catalogue()).customTracks[0].qualified,false,'new version has separate qualification');

    const pipsy = 'ancient-lake-car-pipsy';
    await addBoard(pipsy, 'vanilla', 'Ancient Lake');
    const extended = { board: pipsy, ticket: await ticket('bob', pipsy), racer: 7, ticks: 270, finishTick: 60 };
    const inputs = Buffer.alloc(1080);
    assert.equal((await post('bob', replay({ ...extended, racer: 9 }, inputs))).status, 409);
    assert.equal((await post('bob', replay({ ...extended, finishTick: 59 }, inputs))).status, 400);
    const nonNeutral = Buffer.from(inputs);
    nonNeutral[240] = 1;
    assert.equal((await post('bob', replay(extended, nonNeutral))).status, 400);
    const pipsyRun = await submit('bob', replay(extended, inputs));
    assert.equal(pipsyRun.state, 'queued');
    const racerBoard = await (await fetch('/boards/' + pipsy)).json();
    assert.equal(racerBoard.board.racer, 'Pipsy');

    const verifier = async (op, data = {}, token = verifierSecret, caller = {}) =>
      mf.dispatchFetch('https://ranked-test.invalid/v1/speedruns/verifier/' + op, {
        method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token,...caller},
        body:JSON.stringify({runtime:runtimeHash,...data}),
      });
    assert.equal((await verifier('poll',{},tokens.bob)).status,404);
    assert.equal((await verifier('poll',{},verifierSecret,{Origin:origin})).status,404);
    assert.equal((await verifier('poll',{runtime:'e'.repeat(64)})).status,409);
    assert.equal((await verifier('probe')).status,200);
    const claimed = (await (await verifier('poll')).json()).job;
    assert.equal(claimed.id,pipsyRun.id);
    assert.deepEqual((await (await verifier('poll')).json()).job,null);
    assert.equal((await verifier('renew',{...claimed,claim:'a'.repeat(24)})).status,409);
    await db.prepare('UPDATE speedrun_verifier_health SET seen=0').run();
    await db.prepare('UPDATE speedrun_verifier_claims SET until=unixepoch()+10 WHERE run_id=?').bind(claimed.id).run();
    assert.equal((await verifier('renew',claimed)).status,200);
    const lease = await db.prepare('SELECT until-unixepoch() AS remaining FROM speedrun_verifier_claims WHERE run_id=?')
      .bind(claimed.id).first();
    assert.ok(lease.remaining >= 350);
    assert.ok((await db.prepare('SELECT seen FROM speedrun_verifier_health WHERE id=1').first()).seen > 0);
    await db.prepare('UPDATE speedrun_verifier_claims SET until=0 WHERE run_id=?').bind(claimed.id).run();
    assert.equal((await verifier('renew',claimed)).status,409);
    const replacement = (await (await verifier('poll')).json()).job;
    assert.equal(replacement.id,claimed.id);
    assert.notEqual(replacement.claim,claimed.claim);
    assert.equal((await verifier('renew',claimed)).status,409);
    claimed.claim = replacement.claim;
    assert.equal((await verifier('evidence',{...claimed,claim:'a'.repeat(24)})).status,409);
    const retrieved = await verifier('evidence',claimed);
    assert.deepEqual(Buffer.from(await retrieved.arrayBuffer()),replay(extended,inputs));
    assert.equal((await verifier('finish',{...claimed,proof:{clockTicks:1}})).status,409);
    assert.equal((await review(pipsyRun.id,'reviewer','published')).status,409);
    const proofResult = {runtimeSha256:runtimeHash,replaySha256:retrieved.headers.get('X-DKRR-Replay-SHA256'),
      laps:3, clockTicks:114,bestLapTicks:36,updates:60,
      splits:[36,74,114],checkpoints:[[60,'0'.repeat(64)]],finalCheckpoint:'0'.repeat(64)};
    const prepared = preparedGhost(proofResult.replaySha256);
    const foreignGhost = preparedGhost('f'.repeat(64));
    assert.equal((await verifier('finish',{...claimed,proof:proofResult})).status,409,'a hosted Time Trial needs its verified ghost');
    assert.equal((await verifier('finish',{...claimed,proof:proofResult,ghost:foreignGhost.toString('base64')})).status,409);
    assert.equal((await verifier('finish',{...claimed,proof:proofResult,ghost:prepared.toString('base64')})).status,200);
    const reviewVideo = '/runs/' + pipsyRun.id + '/video';
    const pendingPublish = await review(pipsyRun.id,'reviewer','published');
    assert.equal(pendingPublish.status,409);
    assert.equal((await pendingPublish.json()).error.code,'video_pending');
    assert.equal((await (await fetch('/review',{user:'reviewer'})).json()).runs[0].videoState,'queued');
    const movieJob = (await (await verifier('video-claim')).json()).job;
    assert.equal(movieJob.id,pipsyRun.id,'verified runs start rendering before publication');
    assert.equal(movieJob.claim.length,32);
    assert.equal(movieJob.racerCredit,'bob');
    assert.equal((await (await fetch('/review',{user:'reviewer'})).json()).runs[0].videoState,'rendering');
    const movieClaim = {id:movieJob.id,claim:movieJob.claim};
    assert.equal((await verifier('video-evidence',movieClaim,tokens.bob)).status,404);
    assert.equal((await verifier('video-evidence',{...movieClaim,claim:'a'.repeat(32)})).status,409);
    const movieEvidence = await verifier('video-evidence',movieClaim);
    assert.equal(movieEvidence.headers.get('X-DKRR-Replay-SHA256'),proofResult.replaySha256);
    assert.deepEqual(Buffer.from(await movieEvidence.arrayBuffer()),replay(extended,inputs));
    const reviewMovie = Buffer.from('....ftyp review video before publication');
    const videoSha256 = archive.store(pipsyRun.id,proofResult.replaySha256,reviewMovie);
    const movieProof = {...proofResult,videoSha256,videoInputOverlay:'n64-v1',videoBranding:'dkr-r-logo-v2',videoRacerCredit:'bob',
      videoDuration:extended.ticks/30+4};
    const finishMovie = proof => verifier('video-complete',{...movieClaim,bytes:reviewMovie.length,proof});
    assert.equal((await finishMovie({...movieProof,videoDuration:extended.ticks/30})).status,409);
    assert.equal((await finishMovie({...movieProof,videoRacerCredit:'reviewer'})).status,409);
    assert.deepEqual(await (await finishMovie(movieProof)).json(),{ready:true});
    assert.equal((await (await fetch('/review',{user:'reviewer'})).json()).runs[0].videoState,'ready');
    assert.equal((await fetch(reviewVideo)).status,404);
    assert.equal((await fetch(reviewVideo,{method:'HEAD'})).status,404);
    assert.equal((await fetch(reviewVideo,{method:'POST',user:'bob'})).status,403);
    const videoGrant = await (await fetch(reviewVideo,{method:'POST',user:'reviewer'})).json();
    assert.ok(videoGrant.expires > stamp);
    const privateVideo = reviewVideo+'?review='+encodeURIComponent(videoGrant.grant);
    const preview = await fetch(privateVideo,{headers:{Range:'bytes=5-18'}});
    assert.equal(preview.status,206);
    assert.equal(preview.headers.get('Cache-Control'),'no-store');
    assert.deepEqual(Buffer.from(await preview.arrayBuffer()),reviewMovie.subarray(5,19));
    assert.equal((await fetch(privateVideo,{method:'HEAD'})).headers.get('Content-Length'),String(reviewMovie.length));
    assert.equal((await fetch(privateVideo,{headers:{Origin:'https://other.invalid'}})).status,404);
    assert.equal((await fetch(privateVideo+'x')).status,404);
    await db.prepare("UPDATE accounts SET role='player' WHERE id='reviewer'").run();
    assert.equal((await fetch(privateVideo)).status,404,'revoking moderator access closes pending videos');
    await db.prepare("UPDATE accounts SET role='moderator' WHERE id='reviewer'").run();
    assert.equal((await fetch('/runs/' + pipsyRun.id + '/ghost')).status,404,'verified ghosts require publication');
    assert.equal((await verifier('finish',{...claimed,proof:proofResult})).status,409);
    assert.equal((await fetch('/review',{user:'bob'})).status,403);
    assert.equal((await review(pipsyRun.id,'reviewer','rejected','fixture')).status,200);
    assert.equal((await fetch(privateVideo)).status,404,'rejected video stays private');

    const aliceTicket = await ticket('alice');
    const proof = replay({ ticket: aliceTicket });
    assert.equal((await post(null,proof)).status,401);
    assert.equal((await post('alice',proof,'https://foreign.invalid')).status,403);
    assert.equal((await post('bob',proof)).status,403);
    for (const bad of [Buffer.alloc(4), Buffer.alloc(1024*1024+1), replay({ticket:aliceTicket,ticks:61}),
      replay({ticket:aliceTicket,rom:1}), replay({ticket:aliceTicket,checkpoints:[[59,'0'.repeat(64)]]}),
      replay({ticket:aliceTicket,verified:true}), replay({ticket:aliceTicket,splits:[60,60,114]}),
      replay({ticket:aliceTicket,practice:true}), replay()])
      assert.equal((await post('alice',bad)).status,400);
    assert.equal((await post('alice',replay({ticket:aliceTicket,engine:'f'.repeat(64)}))).status,409);
    assert.equal((await post('alice',replay({ticket:aliceTicket,board:'custom-car'}))).status,409);
    const alice = await submit('alice',proof);
    assert.equal(alice.state,'queued');
    assert.equal((await submit('alice',proof)).id,alice.id,'resubmission is idempotent');
    assert.equal((await post('alice',replay({ticket:aliceTicket,clockTicks:112,splits:[36,74,112]}))).status,409);
    const secondProof = replay({ ticket: await ticket('alice'), clockTicks:112, splits:[36,74,112] });
    const alice2 = await submit('alice',secondProof);
    const bobInputs = Buffer.alloc(240);
    bobInputs[11] = 3;
    const bob = await submit('bob',replay({ ticket: await ticket('bob') }, bobInputs));
    assert.notEqual(alice.id,bob.id,'private replay identity is account scoped');
    assert.equal((await fetch('/runs/'+alice.id+'/replay')).status,404);
    assert.equal((await fetch('/runs/'+alice.id+'/replay',{user:'bob'})).status,404);
    assert.deepEqual(Buffer.from(await (await fetch('/runs/'+alice.id+'/replay',{user:'alice'})).arrayBuffer()),proof);
    assert.equal((await review(alice.id,'alice','published')).status,403);
    assert.equal((await review(alice.id,'reviewer','published')).status,409,'a moderator cannot bypass verification');
    assert.equal((await replayReview(alice2.id,'reviewer')).status,409,'native moderation still requires verification');
    assert.equal((await fetch('/review',{user:'alice'})).status,403);
    assert.equal((await (await fetch('/mine',{user:'alice'})).json()).runs.length,2);
    assert.equal((await (await fetch('/review',{user:'reviewer'})).json()).runs.length,0);
    const entries = async () => (await (await fetch('/boards/ancient-lake-car')).json()).entries;
    assert.deepEqual(await entries(),[]);
    // Seed only the trust boundary result. This fixture does not attest engine determinism.
    for (const [id,ticks] of [[alice.id,65],[alice2.id,64],[bob.id,64]]) {
      await db.prepare("UPDATE speedrun_runs SET state='verified',verified_ticks=?,verified_best_ticks=?,verifier_digest=? WHERE id=?")
        .bind(ticks,34,verifierDigest,id).run();
      if (id === alice2.id) {
        assert.equal((await replayReview(id,'alice')).status,403);
        const browser = await replayReview(id,'reviewer',{'Sec-Fetch-Mode':'cors','Sec-Fetch-Site':'same-site'});
        assert.equal(browser.status,409);
        assert.equal((await browser.json()).error.code,'video_pending');
        const approved = await replayReview(id,'reviewer');
        assert.equal(approved.status,200,await approved.clone().text());
        const video = '/runs/' + id + '/video';
        assert.equal((await fetch(video)).status,404);
        assert.deepEqual(Buffer.from(await (await fetch('/runs/'+id+'/replay')).arrayBuffer()),secondProof);
        const laterJob = (await (await verifier('video-claim')).json()).job;
        assert.equal(laterJob.id,id,'published runs are still queued for video');
        const laterMovie = Buffer.from('....ftyp video prepared after native review');
        const laterSha = archive.store(id,laterJob.sha256,laterMovie);
        const result = await verifier('video-complete',{id,claim:laterJob.claim,bytes:laterMovie.length,
          proof:{...movieProof,replaySha256:laterJob.sha256,clockTicks:ticks,bestLapTicks:34,updates:60,
            splits:[36,74,112],videoSha256:laterSha,videoDuration:6,videoRacerCredit:'alice'}});
        assert.equal(result.status,200,await result.clone().text());
        assert.deepEqual(Buffer.from(await (await fetch(video)).arrayBuffer()),laterMovie);
        continue;
      }
      await db.prepare(`INSERT INTO speedrun_video_assets(run_id,replay_sha256,video_sha256,bytes)
        SELECT id,sha256,?,24 FROM speedrun_runs WHERE id=?`).bind(verifierDigest,id).run();
      assert.equal((await review(id,'reviewer','published')).status,200);
    }
    const duplicate = replay({ ticket: await ticket('bob'), ticks: 61, finishTick: 60 }, Buffer.alloc(244));
    assert.equal((await post('bob',duplicate)).status,409,'a longer neutral tail cannot re-credit someone else’s inputs');
    const credits = await db.prepare('SELECT discord_id,credited_name FROM speedrun_capture_credits WHERE run_id=?')
      .bind(alice.id).first();
    assert.deepEqual(credits,{discord_id:'alice',credited_name:'alice'});
    const videos = await mf.getR2Bucket('SPEEDRUN_VIDEOS');
    const movie = Buffer.from('local video range fixture');
    const videoRoute = '/runs/' + alice.id + '/video';
    await db.prepare('DELETE FROM speedrun_video_assets WHERE run_id=?').bind(alice.id).run();
    assert.equal((await fetch(videoRoute)).status,404);
    await videos.put('runs/' + alice.id + '.mp4',movie,{customMetadata:{replaySha256:'f'.repeat(64)}});
    assert.equal((await fetch(videoRoute)).status,404,'a movie must be tied to this replay');
    await videos.put('runs/' + alice.id + '.mp4',movie,{
      customMetadata:{replaySha256:createHash('sha256').update(proof).digest('hex')},
    });
    const suffix = await fetch(videoRoute,{headers:{Range:'bytes=-7'}});
    assert.equal(suffix.status,206);
    assert.deepEqual(Buffer.from(await suffix.arrayBuffer()),movie.subarray(-7));
    assert.equal((await fetch(videoRoute,{headers:{Range:'bytes=999-'}})).status,416);
    await db.prepare("UPDATE accounts SET role='admin' WHERE id='reviewer'").run();
    assert.equal((await fetch('/review',{user:'reviewer'})).status,200,'admin can access the review queue');
    await db.prepare("UPDATE accounts SET role='moderator' WHERE id='reviewer'").run();
    await db.prepare("UPDATE speedrun_runs SET state='verified',verified_ticks=63,verified_best_ticks=33,verifier_digest=? WHERE id=?").bind(verifierDigest,pipsyRun.id).run();
    await db.prepare("DELETE FROM speedrun_moderation WHERE run_id=?").bind(pipsyRun.id).run();
    assert.equal((await review(pipsyRun.id,'reviewer','published')).status,200);
    assert.deepEqual(Buffer.from(await (await fetch(reviewVideo)).arrayBuffer()),reviewMovie);
    const publishedTimes = async user => (await fetch('/mine/published', { user })).json();
    assert.deepEqual((await publishedTimes('alice')).scores, [64,65].map(ticks => ({board:'ancient-lake-car',ticks})));
    assert.ok((await publishedTimes('bob')).scores.some(score => score.board === pipsy && score.ticks === 63));
    assert.ok(!(await publishedTimes('alice')).scores.some(score => score.board === pipsy), 'other racers and owners stay separate');
    assert.deepEqual(Buffer.from(await (await fetch('/runs/' + pipsyRun.id + '/ghost')).arrayBuffer()), prepared);
    const track = await (await fetch('/tracks/ancient-lake')).json();
    assert.equal(track.entries.length,3,'all racers include one PB per player and racer');
    assert.equal(track.entries[0].racer,'Pipsy');
    assert.deepEqual(track.entries.map(entry=>entry.place),[1,2,2]);
    const lake = (await catalogue()).vanillaTracks.find(row => row.id === 'ancient-lake');
    assert.deepEqual(lake.leaders,{car:{race:{name:'bob',ticks:63},lap:{name:'bob',ticks:33}}});
    const finish = await (await fetch('/attempts', {method:'POST',user:'alice',
      headers:{'Content-Type':'application/json'},body:JSON.stringify({board:'ancient-lake-car'})})).json();
    assert.deepEqual(finish.finishTargets,{title:'Ancient Lake',personal:64,rivals:[63,64]});
    const selected = await (await fetch('/tracks/ancient-lake?racer='+pipsy+'&score=lap')).json();
    assert.deepEqual(selected.entries.map(entry=>[entry.racer,entry.ticks,entry.place]),[['Pipsy',33,1]]);
    assert.equal((await (await fetch('/tracks/ancient-lake?vehicle=plane')).json()).ranked,false);
    assert.equal((await fetch('/tracks/ancient-lake?vehicle=mixed')).status,400);
    assert.equal((await fetch('/tracks/ancient-lake?racer=nope')).status,400);
    assert.equal((await fetch('/mine?run='+pipsyRun.id,{user:'alice'})).status,200);
    assert.equal((await (await fetch('/mine?run='+pipsyRun.id,{user:'alice'})).json()).runs.length,0);
    const leaderboard = await entries();
    assert.equal(leaderboard.length,2,'one PB per account');
    assert.equal(leaderboard[0].ticks,64,'time comes from verifier result, not upload');
    assert.equal(leaderboard[1].place,1,'equal times share rank');
    assert.deepEqual(Buffer.from(await (await fetch('/runs/'+alice2.id+'/replay')).arrayBuffer()),secondProof);
    assert.equal((await review(alice2.id,'reviewer','published')).status,409);
    await mf.getR2Bucket('SPEEDRUN_REPLAYS').then(store => store.put('runs/' + alice.id + '.dkrghost', prepared));
    await deleteAccount('alice');
    assert.equal((await entries()).length,1);
    assert.equal((await fetch('/runs/'+alice2.id+'/replay')).status,404);
    const deletes = (await db.prepare("SELECT run_id AS id FROM speedrun_replay_deletions ORDER BY run_id").all()).results;
    assert.deepEqual(deletes.map(row => row.id), [alice.id, alice2.id].sort());
    const bucket = await mf.getR2Bucket('SPEEDRUN_REPLAYS');
    assert.ok(await bucket.head('runs/' + alice.id + '.dkrreplay'), 'object remains in the private retry queue');
    await deleteAccount('reviewer');
    assert.equal((await entries()).length,1,'reviewer deletion preserves another racer’s result');
    assert.equal((await db.prepare("SELECT moderator_id FROM speedrun_moderation WHERE run_id=?")
      .bind(bob.id).first()).moderator_id,null);
    const { cleanupSpeedruns } = await import('../api/speedrun_api.mjs');
    await db.prepare("UPDATE speedrun_replay_deletions SET removed_at=?").bind(stamp - 7200).run();
    await assert.rejects(cleanupSpeedruns({DB:db,SPEEDRUN_REPLAYS:{
      delete() { throw new Error('Private storage temporarily unavailable'); }
    }}), /storage temporarily unavailable/);
    assert.equal((await db.prepare("SELECT COUNT(*) AS n FROM speedrun_replay_deletions").first()).n,2);
    await cleanupSpeedruns({DB:db,SPEEDRUN_REPLAYS:bucket});
    assert.equal(await bucket.head('runs/' + alice.id + '.dkrreplay'),null);
    assert.equal(await bucket.head('runs/' + alice.id + '.dkrghost'),null);
    assert.ok(await bucket.head('runs/' + bob.id + '.dkrreplay'));
    assert.equal((await db.prepare("SELECT COUNT(*) AS n FROM speedrun_replay_deletions").first()).n,0);
    const newRules = 'vanilla-time-trial-us80-1';
    await db.prepare("INSERT INTO speedrun_boards VALUES('fossil-canyon-plane','vanilla','Fossil Canyon','Time Trial','plane',?,?,NULL,NULL)")
      .bind(newRules,engine).run();
    const expansion = { board:'fossil-canyon-plane', ruleset:newRules, course:'fossil-canyon', vehicle:'plane', rom:77,
      ticket:await ticket('bob','fossil-canyon-plane') };
    assert.equal((await post('bob',replay({...expansion,vehicle:'car'}))).status,409);
    assert.equal((await post('bob',replay({...expansion,course:'jungle-falls'}))).status,409);
    assert.equal((await post('bob',replay({...expansion,racer:2}))).status,409);
    const expandedRun = await submit('bob',replay(expansion));
    const expandedBoard = await (await fetch('/tracks/fossil-canyon?vehicle=plane')).json();
    assert.equal(expandedRun.state,'queued');
    assert.equal(expandedBoard.racers[0].name,'Diddy');
    assert.deepEqual(expandedBoard.entries,[]);
    const expandedJob = (await (await verifier('poll')).json()).job;
    assert.equal(expandedJob.id,expandedRun.id);
    const expandedBytes = await verifier('evidence',expandedJob);
    assert.deepEqual(Buffer.from(await expandedBytes.arrayBuffer()),replay(expansion));
    const expandedProof = {...proofResult,replaySha256:expandedBytes.headers.get('X-DKRR-Replay-SHA256'),rom:77,
      course:'fossil-canyon',vehicle:'plane'};
    assert.equal((await verifier('finish',{...expandedJob,proof:{...expandedProof,vehicle:'hovercraft'}})).status,409);
    assert.equal((await verifier('finish',{...expandedJob,proof:{...expandedProof,rom:80}})).status,409);
    const ghost = preparedGhost(expandedProof.replaySha256);
    const encoded = ghost.toString('base64');
    assert.equal((await verifier('finish',{...expandedJob,proof:expandedProof,ghost:encoded})).status,200);
    await bucket.delete('runs/' + expandedRun.id + '.dkrghost');
    assert.equal((await fetch('/runs/' + expandedRun.id + '/ghost')).status,404);
    for (const state of ['verified', 'published']) {
      // The review fixture account was deleted earlier.
      if (state === 'published')
        await db.prepare("UPDATE speedrun_runs SET state='published' WHERE id=?").bind(expandedRun.id).run();
      assert.equal((await publishedTimes('bob')).scores.some(score => score.board === expansion.board),state === 'published');
      const legacyJob = (await (await verifier('poll',{backfill:expandedRun.id})).json()).job;
      const backfill = {...legacyJob, backfill:true, proof:expandedProof};
      assert.equal((await verifier('renew',backfill)).status,200);
      assert.equal((await verifier('evidence',backfill)).status,200);
      assert.equal((await verifier('finish',backfill)).status,409);
      assert.equal((await verifier('finish',{...backfill,rejected:true})).status,409);
      assert.equal((await verifier('finish',{...backfill,ghost:encoded})).status,200);
      assert.equal((await db.prepare('SELECT state FROM speedrun_runs WHERE id=?').bind(expandedRun.id).first()).state,state);
      const response = await fetch('/runs/' + expandedRun.id + '/ghost');
      if (state === 'verified') assert.equal(response.status,404);
      else assert.deepEqual(Buffer.from(await response.arrayBuffer()),ghost);
    }
    const scoresRoute = '/mine/published';
    const first = await fetch(scoresRoute, {user:'bob'});
    assert.equal(first.status,200);
    assert.ok((await publishedTimes('bob')).scores.some(score => score.board === expansion.board && score.ticks === 114));
    assert.equal((await fetch(scoresRoute + '?afterTicks=1', {user:'bob'})).status,400);
    await db.prepare(`WITH RECURSIVE sample(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM sample WHERE i<260)
      INSERT INTO speedrun_runs(id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at,
        verified_ticks,verifier_digest)
      SELECT 'old-time-'||i,'bob',?,printf('%064x',i),'old-attempt-'||i,'{}','','published',?,?,1000+i,?
      FROM sample`).bind(expansion.board,stamp-7200,stamp-7200,verifierDigest).run();
    const page = await publishedTimes('bob');
    assert.equal(page.scores.length,256);
    assert.equal(page.more,true);
    const last = page.scores.at(-1);
    const rest = await (await fetch(scoresRoute + '?afterBoard=' + last.board + '&afterTicks=' + last.ticks,
      {user:'bob'})).json();
    assert.equal(rest.more,false);
    assert.ok(rest.scores.every(score => score.board === expansion.board && score.ticks > last.ticks));
    assert.deepEqual(rest.scores.at(-1),{board:expansion.board,ticks:1260});
    console.log('Ranked API passed: catalogue, version gate, bounded replay upload, retry, privacy, verification gate, moderation, PB ranking and account deletion.');
  } finally { await mf.dispose(); }
  await checkSubmissionLimits(root);
  await checkLocalClaims(root);
  console.log('Local replay claims passed: feature gate, authentication, one-time credit, retries, review gate and quota.');
}
main().catch(error => { console.error(error); process.exitCode=1; });
