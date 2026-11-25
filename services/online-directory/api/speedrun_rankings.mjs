import { adventureBoard, boardColumns, customBoard, customPackageJoin, vanillaBoard } from './speedrun_rules.mjs';

const query = async (db, sql, ...params) => (await db.prepare(sql).bind(...params).all()).results;
const scale = 1_000_000;
const currentRuns = `FROM speedrun_runs r JOIN accounts a ON a.id=r.account_id
  JOIN speedrun_boards b ON b.id=r.board_id
  WHERE r.state='published' AND a.status='active' AND r.verified_ticks>0
    AND length(r.verifier_digest)=64 AND json_valid(r.manifest)
    AND json_extract(r.manifest,'$.ruleset')=b.ruleset AND json_extract(r.manifest,'$.engine')=b.engine`;
const compare = (a,b) => a < b ? -1 : a > b ? 1 : 0;
const tickScore = (time, fastest, place, count) => {
  const placement = count === 1 ? 1 : 1 - (place-1)/(count-1);
  const field = .35 + .65*(Math.min(count,8)-1)/7;
  return Math.round(scale*field*(.70*placement + .30*(fastest/time)**2));
};

export function customPoints(grades) {
  return [...grades].sort((a,b)=>b-a).slice(0,30)
    .reduce((sum,grade,index)=>sum + grade*(index<10 ? 20 : 2.5)/scale,0);
}

const boardInfo = row => {
  const vanilla = vanillaBoard(row);
  if (vanilla) return {kind:'vanilla', id:vanilla.course, title:row.title, vehicle:row.vehicle};
  if (customBoard(row) && row.item_id) return {kind:'custom', id:row.item_id, title:row.item_title, vehicle:row.vehicle};
  if (adventureBoard(row)) return {kind:'full-game', id:row.category, title:row.title, vehicle:'mixed'};
  return null;
};

// Collapse racers before ranking each metric. A player contributes one finish
// and one best lap per course and vehicle, even if those came from different runs.
function bestResults(rows, boards) {
  const events = new Map();
  for (const row of rows) {
    const board = boards.get(row.board_id);
    if (!board) continue;
    const key = JSON.stringify([board.kind,board.id,board.vehicle]);
    if (!events.has(key)) events.set(key,{...board,players:new Map()});
    const players = events.get(key).players;
    if (!players.has(row.account_id)) players.set(row.account_id,{name:row.name});
    const entry = players.get(row.account_id);
    for (const metric of ['race','lap']) {
      if (!row[metric] || (metric === 'lap' && board.kind === 'full-game')) continue;
      const value = {ticks:row[metric],runId:row[metric+'_id'],hasVideo:!!row[metric+'_video']};
      const previous = entry[metric];
      if (!previous || value.ticks < previous.ticks || (value.ticks === previous.ticks && value.runId < previous.runId))
        entry[metric] = value;
    }
  }
  return events;
}

export function scoreStandings(rows, boards) {
  const events = bestResults(rows,boards);
  const players = new Map(), trackSlots = new Map(), categories = new Set();
  for (const event of events.values()) {
    const {kind,id,title,vehicle} = event;
    const trackKey = kind+'|'+id;
    if (!trackSlots.has(trackKey)) trackSlots.set(trackKey,new Set());
    trackSlots.get(trackKey).add(vehicle);
    if (kind === 'full-game') categories.add(id);
    for (const metric of ['race','lap']) {
      const field = [...event.players].filter(([,result])=>result[metric]).sort((a,b)=>a[1][metric].ticks-b[1][metric].ticks);
      let place = 0, previous = 0;
      for (const [i,[account,result]] of field.entries()) {
        const value = result[metric];
        if (value.ticks !== previous) place = i+1;
        previous = value.ticks;
        value.place = place;
        value.field = field.length;
        value.grade = tickScore(value.ticks,field[0][1][metric].ticks,place,field.length);
        if (!players.has(account)) players.set(account,{id:account,name:result.name,personalBests:0,records:0,
          podiums:0,contestedFirsts:0,events:[]});
        const player = players.get(account);
        player.personalBests++;
        if (place === 1) {
          player.records++;
          if (field.length > 1) player.contestedFirsts++;
        }
        if (place <= 3) player.podiums++;
      }
    }
    for (const [account,result] of event.players) {
      const grade = kind === 'full-game' ? result.race.grade : Math.round(result.race.grade*.75 + (result.lap?.grade ?? 0)*.25);
      players.get(account).events.push({kind,id,title,vehicle,grade,race:result.race,...(result.lap ? {lap:result.lap} : {})});
    }
  }
  const vanillaVehicles = new Set([...events.values()].filter(e=>e.kind==='vanilla').map(e=>e.vehicle));
  const vanillaCounts = new Map([...vanillaVehicles].map(vehicle=>[vehicle,[...events.values()]
    .filter(e=>e.kind==='vanilla' && e.vehicle===vehicle).length]));
  const field = [...players.values()].map(player=>{
    const vanilla = new Map(), custom = new Map();
    let fullGame = 0;
    for (const event of player.events) {
      if (event.kind === 'full-game') fullGame += event.grade;
      else if (event.kind === 'vanilla') vanilla.set(event.vehicle,(vanilla.get(event.vehicle) ?? 0)+event.grade);
      else {
        const track = custom.get(event.id) ?? {id:event.id,title:event.title,grade:0};
        track.grade += event.grade;
        custom.set(event.id,track);
      }
    }
    const customTracks = [...custom.values()].map(track=>({...track,grade:Math.round(track.grade/trackSlots.get('custom|'+track.id).size)}))
      .sort((a,b)=>b.grade-a.grade || compare(a.id,b.id));
    const vanillaPoints = !vanillaCounts.size ? 0 : 500/scale/vanillaCounts.size *
      [...vanillaCounts].reduce((sum,[vehicle,count])=>sum + (vanilla.get(vehicle) ?? 0)/count,0);
    const points = {
      vanilla:Math.round(vanillaPoints*1000),
      custom:Math.round(customPoints(customTracks.map(track=>track.grade))*1000),
      fullGame:categories.size ? Math.round(250_000*fullGame/scale/categories.size) : 0
    };
    const balanced = Math.min(...[
      vanillaCounts.size ? points.vanilla : null, [...events.values()].some(e=>e.kind==='custom') ? 2*points.custom : null,
      categories.size ? 2*points.fullGame : null
    ].filter(value=>value!==null));
    return {...player,points,balanced,total:points.vanilla+points.custom+points.fullGame,
      coverage:player.events.length,customTracks:customTracks.map((track,i)=>({...track,slot:i+1,
        points:i<30 ? Math.round(track.grade*(i<10 ? 20_000 : 2500)/scale) : 0}))};
  });
  const standingsOrder = (a,b) => b.total-a.total || b.balanced-a.balanced ||
    b.contestedFirsts-a.contestedFirsts || b.coverage-a.coverage;
  field.sort((a,b)=>standingsOrder(a,b)||compare(a.id,b.id));
  for (const [index,player] of field.entries())
    player.place = index && standingsOrder(field[index-1],player)===0 ? field[index-1].place : index+1;
  return field;
}

export async function overallRankings(db, id, offset = 0) {
  const candidates = await query(db,`SELECT ${boardColumns},l.item_id,l.item_title FROM speedrun_boards b
    ${customPackageJoin}
    LEFT JOIN (SELECT id AS item_id,title AS item_title,current_release_id FROM library_items
      WHERE archived=0 AND category='tracks') l ON l.current_release_id=b.release_id
    WHERE EXISTS (SELECT 1 FROM speedrun_runs r WHERE r.board_id=b.id AND r.state='published')`);
  const boards = new Map(candidates.flatMap(row=> {
    const info = boardInfo(row);
    return info ? [[row.id,info]] : [];
  }));
  const rows = await query(db,`WITH results AS (
    SELECT r.account_id,r.board_id,r.id,a.display_name AS name,r.verified_ticks AS race,r.verified_best_ticks AS lap,
      EXISTS (SELECT 1 FROM speedrun_video_assets v WHERE v.run_id=r.id AND v.replay_sha256=r.sha256) AS video,
      ROW_NUMBER() OVER (PARTITION BY r.account_id,r.board_id ORDER BY r.verified_ticks,r.created_at,r.id) AS finish_order,
      ROW_NUMBER() OVER (PARTITION BY r.account_id,r.board_id
        ORDER BY r.verified_best_ticks IS NULL,r.verified_best_ticks,r.created_at,r.id) AS lap_order
    ${currentRuns})
    SELECT account_id,board_id,name,MAX(CASE WHEN finish_order=1 THEN race END) AS race,
      MAX(CASE WHEN finish_order=1 THEN id END) AS race_id,MAX(CASE WHEN finish_order=1 THEN video END) AS race_video,
      MAX(CASE WHEN lap_order=1 THEN lap END) AS lap,MAX(CASE WHEN lap_order=1 THEN id END) AS lap_id,
      MAX(CASE WHEN lap_order=1 THEN video END) AS lap_video
    FROM results WHERE finish_order=1 OR lap_order=1 GROUP BY account_id,board_id,name`);
  const field = scoreStandings(rows,boards);
  const summary = ({events,customTracks,balanced,...player}) => player;
  if (!id) return {scoringVersion:1,players:field.slice(0,10).map(summary),totalPlayers:field.length,
    tiedCutoff:field.length>10 && field[9].place===field[10].place};
  const player = field.find(entry=>entry.id===id);
  if (!player) return null;
  const events = player.events.sort((a,b)=>compare(a.kind,b.kind)||compare(a.title,b.title)||compare(a.vehicle,b.vehicle));
  return {scoringVersion:1,player:summary(player),customTracks:player.customTracks.slice(0,30),
    events:events.slice(offset,offset+50),nextOffset:offset+50<events.length ? offset+50 : null};
}

export function buildRecordHistory(rows, undated, stamp) {
  let best = Infinity, holders = new Map();
  const changes = [];
  for (let i=0;i<rows.length;) {
    const {at,ticks} = rows[i];
    const batch = new Map();
    for (;i<rows.length && rows[i].at===at;i++) {
      const row = rows[i];
      if (row.ticks===ticks && !batch.has(row.account_id)) batch.set(row.account_id,row);
    }
    if (ticks>best) continue;
    const improved = ticks<best;
    if (!improved && [...batch.keys()].every(account=>holders.has(account))) continue;
    if (improved) {
      for (const [account,reign] of holders) {
        if (batch.has(account)) continue;
        reign.until = at;
        holders.delete(account);
      }
    }
    best = ticks;
    for (const [account,row] of batch) {
      if (!holders.has(account)) holders.set(account,{from:at,until:null});
      Object.assign(holders.get(account),{name:row.name,lastRun:row.id});
    }
    changes.push({at,ticks,holders:[...holders.values()].map(reign=>({reign,name:reign.name,runId:reign.lastRun}))});
  }
  let nextFaster = null;
  for (let i=changes.length-1;i>=0;i--) {
    if (i+1<changes.length && changes[i+1].ticks<changes[i].ticks) nextFaster = changes[i+1].at;
    changes[i].until = nextFaster;
  }
  return {asOf:stamp,complete:undated===0,earlierChanges:Math.max(0,changes.length-120),
    changes:changes.slice(-120).map(event=>({at:event.at,ticks:event.ticks,until:event.until,
      holders:event.holders.map(({reign,name,runId})=>({name,runId,from:reign.from,until:reign.until}))}))};
}

export async function recordHistory(db, boards, score, stamp) {
  if (!boards.length) return buildRecordHistory([],0,stamp);
  const time = score==='lap' ? 'verified_best_ticks' : 'verified_ticks';
  const ids = JSON.stringify(boards.map(row=>row.id));
  const results = await query(db,`WITH eligible AS (
    SELECT r.id,r.account_id,r.${time} AS ticks,
      (SELECT m.decided_at FROM speedrun_moderation m WHERE m.run_id=r.id AND m.decision='published' AND m.decided_at>0) AS at,
      COALESCE((SELECT c.credited_name FROM speedrun_capture_credits c WHERE c.run_id=r.id),a.display_name) AS name
    ${currentRuns} AND r.board_id IN (SELECT value FROM json_each(?)) AND r.${time}>0),
    firsts AS (
      SELECT *, ROW_NUMBER() OVER (PARTITION BY account_id,ticks ORDER BY at,id) AS duplicate
      FROM eligible WHERE at IS NOT NULL),
    record_times AS (
      SELECT *, MIN(ticks) OVER (ORDER BY at RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS record
      FROM firsts WHERE duplicate=1)
    SELECT id,account_id,ticks,at,name FROM record_times WHERE ticks=record
    UNION ALL SELECT NULL,NULL,COUNT(*),NULL,NULL FROM eligible WHERE at IS NULL
    ORDER BY at,ticks,id`,ids);
  const [missing,...rows] = results;
  return buildRecordHistory(rows,missing?.ticks ?? 0,stamp);
}
