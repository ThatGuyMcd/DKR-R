const assert = require('node:assert/strict');
const path = require('node:path');
const { makePilot, engine, rules } = require('../run_local_ranked.cjs');

async function main() {
  const { customPoints, buildRecordHistory, scoreStandings } = await import('../api/speedrun_rankings.mjs');
  assert.equal(customPoints(Array(31).fill(1_000_000)),250);
  assert.equal(customPoints([...Array(10).fill(1_000_000),800_000]),202);
  assert.equal(customPoints([...Array(30).fill(800_000),600_000]),200);
  const row = (at,ticks,account_id,id) => ({at,ticks,account_id,id,name:account_id});
  const coholders = buildRecordHistory([
    row(100,4200,'alice','1'),row(150,4200,'bob','2'),
    row(180,4000,'alice','3'),row(180,4000,'bob','4'),row(200,3900,'carol','5')
  ],0,250);
  assert.deepEqual(coholders.changes.map(event=>event.until),[180,180,200,null]);
  assert.deepEqual(coholders.changes[2].holders.map(holder=>[holder.name,holder.from,holder.until]),
    [['alice',100,200],['bob',150,200]]);
  const tied = scoreStandings([
    {board_id:'car',account_id:'alice',name:'Alice',race:4000,lap:1100,race_id:'a',lap_id:'a'},
    {board_id:'car',account_id:'bob',name:'Bob',race:4000,lap:1200,race_id:'b',lap_id:'b'}
  ],new Map([['car',{kind:'vanilla',id:'ancient-lake',title:'Ancient Lake',vehicle:'car'}]]));
  assert.deepEqual(tied.map(player=>[player.name,player.personalBests,player.records]),[['Alice',2,2],['Bob',2,1]]);

  const { mf, db, fetch, addBoard } = await makePilot(path.resolve(process.argv[2]));
  const read = async suffix => {
    const response = await fetch(suffix);
    assert.equal(response.status,200,await response.clone().text());
    return response.json();
  };
  try {
    assert.deepEqual((await read('/overall')).players,[]);
    await addBoard('ancient-lake-car','vanilla','Ancient Lake');
    await addBoard('ancient-lake-car-tt','vanilla','Ancient Lake');
    const add = async (id,account,board,race,lap,date,state='published',revision=engine) => {
      await db.prepare(`INSERT INTO speedrun_runs
        (id,account_id,board_id,sha256,attempt_id,manifest,notes,state,created_at,updated_at,verified_ticks,
          verified_best_ticks,verifier_digest) VALUES(?,?,?,?,?,?,'',?,?,?,?,?,?)`)
        .bind(id,account,board,id,id,JSON.stringify({ruleset:rules,engine:revision}),state,date,date,race,lap,'a'.repeat(64)).run();
      if (date && state==='published') await db.prepare(`INSERT INTO speedrun_moderation
        (run_id,moderator_id,decision,reason,decided_at) VALUES(?,?,'published','',?)`).bind(id,'reviewer',date).run();
    };
    await add('alice-first','alice','ancient-lake-car',3900,1200,100);
    await add('alice-faster','alice','ancient-lake-car-tt',3800,1190,150);
    await add('bob-ties','bob','ancient-lake-car-tt',3800,1170,200);
    await add('bob-wins','bob','ancient-lake-car',3700,1180,300);
    await add('unverified','reviewer','ancient-lake-car',100,30,350,'queued');
    await add('old-rules','reviewer','ancient-lake-car',100,30,360,'published','previous-engine');
    const standings = await read('/overall');
    assert.deepEqual(standings.players.map(player=>player.id),['bob','alice']);
    assert.deepEqual(standings.players.map(player=>[player.personalBests,player.records]),[[2,2],[2,0]]);
    assert.equal(standings.players[0].coverage,1);
    const bob = await read('/overall/bob');
    assert.equal(bob.player.personalBests,2);
    assert.equal(bob.player.records,2);
    assert.equal(bob.events.length,1);
    assert.equal(bob.events[0].race.runId,'bob-wins');
    assert.equal(bob.events[0].lap.runId,'bob-ties');
    assert.equal(bob.events[0].race.field,2);
    const route = kind => '/tracks/ancient-lake?kind=vanilla&vehicle=car&score='+kind;
    let race = (await read(route('race'))).history;
    assert.deepEqual(race.changes.map(event=>event.ticks),[3900,3800,3800,3700]);
    assert.equal(race.changes[2].holders[0].from,100);
    assert.equal(race.changes[2].holders[1].from,200);
    assert.deepEqual(race.changes.at(-1).holders.map(holder=>[holder.name,holder.from]),[['bob',200]]);
    const lap = (await read(route('lap'))).history;
    assert.deepEqual(lap.changes.map(event=>event.ticks),[1200,1190,1170]);
    assert.equal((await read(route('race')+'&offset=100')).history,null);
    await add('undated','bob','ancient-lake-car',3600,1100,0);
    assert.equal((await read(route('race'))).history.complete,false);
    await db.prepare("UPDATE accounts SET status='deleted' WHERE id='bob'").run();
    race = (await read(route('race'))).history;
    assert.equal(race.complete,true);
    assert.deepEqual(race.changes.map(event=>event.ticks),[3900,3800]);
    assert.equal(race.changes.at(-1).holders[0].until,null);
    assert.deepEqual((await read('/overall')).players.map(player=>player.id),['alice']);
    assert.equal((await fetch('/overall/bob')).status,404);
    console.log('Overall points, grouped racers, historical ties, publication times, missing dates, and removal passed.');
  } finally {
    await mf.dispose();
  }
}

main().catch(error=>{console.error(error);process.exitCode=1;});
