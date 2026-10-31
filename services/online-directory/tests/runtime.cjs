// Full exported library Worker -> service binding -> real SQLite Durable Object.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {randomBytes,createHash}=require('node:crypto');
const {Miniflare}=require(path.resolve(process.argv[3]&&!process.argv[3].startsWith('--')?process.argv[3]:'build/library-workflow-d1-runtime/node_modules/miniflare'));
const {schema}=require('../../library-workflow/tests/support.cjs');
const root=path.resolve(process.argv[2]);
const runningSpectators=process.argv.includes('--running');
let checks=0;
const check=(value,label)=>{assert.ok(value,label);checks++;};
const equal=(a,b,label)=>{assert.equal(a,b,label);checks++;};
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const key=()=>randomBytes(32).toString('hex');
async function main(){
 const options={workers:[
  {name:'api',modules:true,modulesRoot:root,scriptPath:path.join(root,'PRIVATE-API/worker-single-file.mjs'),d1Databases:['DB'],
   serviceBindings:{ONLINE_DIRECTORY:'online'},bindings:{ONLINE_ENABLED:'true',RUNNING_SPECTATORS_ENABLED:runningSpectators?'true':'false',FRONTEND_URL:'https://positron-portal.com/dkr-r/index.html',DOWNLOAD_ORIGINS:'["https://positron-portal.com/dkr-r/downloads/"]',IMAGE_ORIGINS:'["https://positron-portal.com/dkr-r/images/"]'}},
  {name:'online',modules:true,modulesRoot:root,scriptPath:path.join(root,'ONLINE-WORKER/directory.mjs'),compatibilityDate:'2026-07-30',
   durableObjects:{DIRECTORY:{className:'WorldwideDirectory',useSQLite:true}},bindings:{ONLINE_ENABLED:'true',RUNNING_SPECTATORS_ENABLED:runningSpectators?'true':'false'}}
 ]};
 const mf=new Miniflare(options);
 const sockets=[];
 try{
  const db=await mf.getD1Database('DB','api');await db.exec(schema);
  if(process.argv.includes('--profile'))await db.exec(fs.readFileSync(path.join(root,'PRIVATE-API/001-launcher-discord-profiles.sql'),'utf8').replace(/\s+/g,' '));
  const now=Math.floor(Date.now()/1000),tokens={};
  for(const name of ['host','guest','other','intruder','closer']){
   const token=randomBytes(32).toString('base64url');tokens[name]=token;
   await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(name,name,'Player '+name,'player','active',now,now).run();
   await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(token).digest('base64url'),name,now,now,now+3600,now+86400).run();
  }
  const api={fetch:(...args)=>mf.dispatchFetch(...args)};
  if(process.argv.includes('--profile')){
   const worker=fs.readFileSync(path.join(root,'PRIVATE-API/worker-single-file.mjs'),'utf8');
   const source=worker.slice(worker.indexOf('function discordProfileAvatar('),worker.indexOf('async function handleLauncherDiscordProfile('));
   const functions=require('node:vm').runInNewContext(source+';({captureDiscordProfile,discordProfileAvatar})',{URL,Response});
   const identity={id:'123456789012345678',avatar:'0123456789abcdef0123456789abcdef',discriminator:'0'};
   await functions.captureDiscordProfile({DB:db},{id:'host'},identity,'Discord Racer');
   const get=token=>api.fetch('https://test.invalid/v1/launcher/discord-profile',{headers:{Origin:'https://positron-portal.com',...(token?{Authorization:'Bearer '+token}:{})}});
   equal((await get()).status,401,'profile metadata requires sign-in');
   let response=await get(tokens.host);equal(response.status,200,'own profile available');
   let data=await response.json();equal(data.displayName,'Discord Racer','Discord display name retained independently');
   equal(data.avatarUrl,'https://cdn.discordapp.com/avatars/'+identity.id+'/'+identity.avatar+'.png?size=128','bounded avatar URL');
   equal(data.accountId,'host','profile is scoped to authenticated account');
   equal(response.headers.get('Cache-Control'),'no-store','private metadata is not cached');
   data=await (await get(tokens.guest)).json();equal(data.displayName,'','another account cannot read the host profile');
   check(!JSON.stringify(data).includes(identity.id),'another account cannot read Discord identity');
   await functions.captureDiscordProfile({DB:db},{id:'host'},{...identity,avatar:'https://evil.invalid/x'},'Updated');
   data=await (await get(tokens.host)).json();equal(data.displayName,'Updated','repeated sign-in updates own metadata');
   check(/^https:\/\/cdn\.discordapp\.com\/embed\/avatars\/[0-5]\.png\?size=128$/.test(data.avatarUrl),'invalid provider image uses a safe default');
   equal(functions.discordProfileAvatar({...identity,id:'bad'}),'','malformed provider identity refused');
   equal((await api.fetch('https://test.invalid/v1/launcher/discord-profile',{method:'POST'})).status,405,'profile metadata is read-only');
  }
  async function http(route,account,instance,method='GET'){
   const headers={Origin:'https://positron-portal.com'};
   if(account)headers.Authorization='Bearer '+tokens[account];
   if(instance)headers['X-DKRR-Instance']=instance;
   return api.fetch('https://test.invalid'+route,{method,headers,body:method==='POST'?'{}':undefined});
  }
  const capabilities=await http('/v1/online/capabilities');equal(capabilities.status,200,'public capabilities: '+await capabilities.clone().text());equal((await capabilities.json()).protocol,1,'protocol negotiation');
  equal((await api.fetch('https://test.invalid/v1/online/connect')).status,426,'ordinary HTTP cannot allocate a WebSocket session');
  const anonymous=await http('/v1/online/ticket',null,key(),'POST');equal(anonymous.status,401,'anonymous ticket refused: '+await anonymous.text());
  async function open(account,instance=key()){
   const ticketResponse=await http('/v1/online/ticket',account,instance,'POST');
   equal(ticketResponse.status,200,'authenticated ticket issued');const {ticket}=await ticketResponse.json();
   const response=await api.fetch('https://test.invalid/v1/online/connect',{headers:{Upgrade:'websocket','Sec-WebSocket-Protocol':'dkrr-online-v1, dkrr-ticket-'+ticket}});
   equal(response.status,101,'upgrade preserved through exported API and service binding');
   const socket=response.webSocket;check(socket,'upgrade has WebSocket');
   const events=[],waiters=new Map(),prefix=key().slice(0,8);let seq=0;
   socket.addEventListener('message',event=>{const message=JSON.parse(event.data);events.push(message);if(message.type==='result'&&waiters.has(message.request)){const done=waiters.get(message.request);waiters.delete(message.request);done(message);}});
   socket.accept();sockets.push(socket);
   const client={account,instance,socket,ticket,events,
    call(input,id){const request=id||prefix+'t'+(++seq);return new Promise((resolve,reject)=>{
     const timeout=setTimeout(()=>{waiters.delete(request);reject(new Error('No reply: '+JSON.stringify(input)));},6000);
     waiters.set(request,value=>{clearTimeout(timeout);resolve(value);});socket.send(JSON.stringify({...input,request}));
    });},
    async event(type,predicate=()=>true){for(let n=0;n<200;n++){const index=events.findIndex(e=>e.type===type&&predicate(e));if(index>=0)return events.splice(index,1)[0];await pause(30);}throw Error('Missing event '+type);}
   };
   await client.event('snapshot');return client;
  }
  let host=await open('host');const guest=await open('guest'),duplicate=await open('guest'),other=await open('other');
  equal((await guest.call({type:'heartbeat'})).data.online,3,'online count deduplicates accounts across instances');
  equal((await api.fetch('https://test.invalid/v1/online/session',{method:'POST',headers:{Origin:'https://positron-portal.com',Authorization:'bearer '+tokens.other,'X-DKRR-Instance':other.instance},body:'{}'})).status,200,'authorization scheme casing does not change session binding');
  const replay=await api.fetch('https://test.invalid/v1/online/connect',{headers:{Upgrade:'websocket','Sec-WebSocket-Protocol':'dkrr-online-v1, dkrr-ticket-'+host.ticket}});
  equal(replay.status,401,'connection ticket cannot be replayed');
  const state={title:'Private test',epoch:'epoch-one',compatibility:'test-network',visibility:'private',phase:'waiting',racers:1,maxRacers:2,spectators:0,maxSpectators:8,reservedRacers:0,reservedSpectators:0,roster:'1',code:'ABCD1',bootstrap:'ABCD1',canSpectateRunning:true};
  let room=(await host.call({type:'room',revision:0,state})).data;
  check(room.id,'private room created');
  equal((await guest.call({type:'list'})).data.rooms.length,0,'private room omitted from public payload');
  equal((await guest.call({type:'join',room:room.id,role:'racer',key:key(),compatibility:'test-network'})).error.code,'unavailable','private room identifier grants no public access');
  equal((await guest.call({type:'resolve',code:'WRONG',role:'racer',key:key(),compatibility:'test-network'})).error.code,'unavailable','unknown private code is not searchable');
  const privateJoin=await guest.call({type:'resolve',code:'ABCD1',role:'spectator',key:key(),compatibility:'test-network'});
  check(privateJoin.ok,'private code creates a host intent');
  await host.event('intent');await guest.call({type:'cancel',id:privateJoin.data.id});await guest.event('join_cancelled');
  state.visibility='public';state.title='Public test';room=(await host.call({type:'room',revision:room.revision,state})).data;
  const published=await other.event('delta',message=>message.upsert?.some(row=>row.id===room.id));
  check(published.upsert.some(row=>row.id===room.id),'coalesced public room updates reach other launchers');
  const list=(await guest.call({type:'list'})).data;
  equal(list.rooms.length,1,'public room visible globally');
  const wire=JSON.stringify(list);
  check(!wire.includes('ABCD1')&&!wire.includes('admission')&&!wire.includes('owner')&&!wire.includes('account'),'public payload excludes connection secrets and account identifiers');
  equal(list.rooms[0].host,'Player host','verified display name preserved');
  equal((await other.call({type:'room',revision:room.revision,state:{...state,epoch:'bad',visibility:'invalid'}})).error.code,'invalid','invalid host state rejected');
  const firstKey=key();
  const intent=await guest.call({type:'join',room:room.id,role:'racer',key:firstKey,compatibility:'test-network'},'idempotent');
  check(intent.ok,'public join reserved');
  const repeated=await guest.call({type:'join',room:room.id,role:'racer',key:firstKey,compatibility:'test-network'},'idempotent');
  equal(repeated.data.id,intent.data.id,'lost acknowledgement does not duplicate reservation');
  const full=await other.call({type:'join',room:room.id,role:'racer',key:key(),compatibility:'test-network'});
  equal(full.error.code,'full','last-seat reservation is authoritative');
  const hostIntent=await host.event('intent');equal(hostIntent.join.key,firstKey,'host receives reserved handshake key');equal(hostIntent.join.account,'guest','host receives verified account');
  equal((await other.call({type:'grant',id:intent.data.id,allowed:true,admission:key()})).error.code,'owner','another account cannot authorize the host room');
  const admission=key();check((await host.call({type:'grant',id:intent.data.id,allowed:true,admission})).ok,'host grant accepted');
  const bootstrap=await guest.event('bootstrap');equal(bootstrap.admission,admission,'bootstrap is delivered privately');equal(bootstrap.key,firstKey,'bootstrap retains joining key binding');
  check(!duplicate.events.some(e=>e.type==='bootstrap'),'same-account other instance receives no bootstrap');
  state.visibility='private';room=(await host.call({type:'room',revision:room.revision,state})).data;
  const cancelled=await guest.event('join_cancelled');equal(cancelled.id,intent.data.id,'privacy transition revokes pending public admission');
  equal((await guest.call({type:'list'})).data.rooms.length,0,'private transition removes global listing');
  const removed=await other.event('delta',message=>message.remove?.includes(room.id));
  check(removed.remove.includes(room.id),'private transition sends a public removal delta');
  state.visibility='public';state.phase='starting';room=(await host.call({type:'room',revision:room.revision,state})).data;
  equal((await guest.call({type:'join',room:room.id,role:'racer',key:key(),compatibility:'test-network'})).error.code,'phase','countdown/preparation admission refused');
  state.phase='testing';room=(await host.call({type:'room',revision:room.revision,state})).data;
  equal((await guest.call({type:'list'})).data.rooms[0].canJoin,false,'Scoreboard test pauses discovery joins');
  state.phase='racing';room=(await host.call({type:'room',revision:room.revision,state})).data;
  equal((await guest.call({type:'list'})).data.rooms[0].canSpectate,runningSpectators,'running spectator advertisement follows the pilot gate');
  if(runningSpectators){
   equal((await guest.call({type:'join',room:room.id,role:'racer',key:key(),compatibility:'test-network'})).error.code,'phase','running spectator gate never opens a racing port');
   const watch=await guest.call({type:'join',room:room.id,role:'spectator',key:key(),compatibility:'test-network'});
   check(watch.ok,'staging running spectator request allowed');
   const intent=await host.event('intent');check(intent.join.running&&intent.join.role==='spectator','host receives a spectator-only running scope');
   check((await host.call({type:'grant',id:watch.data.id,allowed:true,admission:key()})).ok,'host may grant only the current running-spectator scope');
   equal((await guest.event('bootstrap')).role,'spectator','running bootstrap preserves role');
   await host.call({type:'consume',id:watch.data.id});equal((await guest.event('join_consumed')).id,watch.data.id,'native authentication releases the provisional reservation');
  }
  state.phase='returning';room=(await host.call({type:'room',revision:room.revision,state})).data;
  equal((await guest.call({type:'list'})).data.rooms[0].canJoin,false,'safe return is not prematurely joinable');
  state.phase='waiting';state.roster='2';room=(await host.call({type:'room',revision:room.revision,state})).data;
  equal((await guest.call({type:'list'})).data.rooms[0].id,room.id,'return to lobby retains room identity');
  equal((await guest.call({type:'join',room:room.id,role:'racer',key:key(),compatibility:'old-build'})).error.code,'incompatible','old network build refused');
  equal((await host.call({type:'room',revision:1,state})).error.code,'revision','stale host revision cannot overwrite newer state');
  state.hidden=true;
  equal((await host.call({type:'room',revision:room.revision,state})).error.code,'hidden','appear-offline host cannot publish publicly');state.hidden=false;
  check((await http('/v1/online/session','guest',guest.instance,'POST')).status===200,'account authorization can renew independently of presence');
  await db.prepare('UPDATE sessions SET revoked_at=? WHERE user_id=?').bind(now,'guest').run();
  equal((await http('/v1/online/session','guest',guest.instance,'POST')).status,401,'revoked account session cannot renew socket authority');
  const refreshedToken=randomBytes(32).toString('base64url');
  await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(refreshedToken).digest('base64url'),'host',now,now,now+3600,now+86400).run();tokens.host=refreshedToken;
  host=await open('host',host.instance);
  const conflict=await host.call({type:'room',revision:0,state});
  check(conflict.error,'reconnect should reconcile an existing room: '+JSON.stringify(conflict));
  equal(conflict.error.current.revision,room.revision,'reconnecting owner receives its current revision privately');
  const recovered=await host.call({type:'room',revision:conflict.error.current.revision,state});
  equal(recovered.data.id,room.id,'reconnect reconciles the same hosted room');room=recovered.data;
  const sameAccount=await open('other');
  for(let index=0;index<8;index++){
   const peer=index%2?other:sameAccount;
   const denied=await peer.call({type:'resolve',code:'WRONG',role:'racer',key:key(),compatibility:'test-network'});
   equal(denied.error.code,'unavailable','bounded incorrect code lookup');
  }
  equal((await other.call({type:'resolve',code:'WRONG',role:'racer',key:key(),compatibility:'test-network'})).error.code,'rate_limit','private code rate limit survives duplicate instances');
  sameAccount.socket.close(1000,'done');
  const leaving=await open('intruder');
  const abandoned=await leaving.call({type:'join',room:room.id,role:'spectator',key:key(),compatibility:'test-network'});
  check(abandoned.ok,'disconnect cancellation fixture reserved');
  await host.event('intent',message=>message.join.id===abandoned.data.id);leaving.socket.close(1000,'cancel join');
  equal((await host.event('revoke',message=>message.id===abandoned.data.id)).id,abandoned.data.id,'clean client disconnect revokes its unused native admission');
  const flood=await open('intruder');
  const floodClosed=new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>reject(new Error('Malformed-message flood was not closed')),6000);
   flood.socket.addEventListener('close',event=>{clearTimeout(timer);resolve(event.code);},{once:true});
  });
  for(let index=0;index<185;index++)try{flood.socket.send('{');}catch{break;}
  equal(await floodClosed,1008,'malformed messages cannot bypass the socket rate limit');
  const closingPeer=await open('closer');
  const closingJoin=await closingPeer.call({type:'join',room:room.id,role:'spectator',key:key(),compatibility:'test-network'});
  check(closingJoin.ok,'close fixture has a pending admission');
  await host.event('intent',message=>message.join.id===closingJoin.data.id);
  const closedRoom=room.id,closedCode=state.code;
  const countBeforeClose=(await other.call({type:'heartbeat'})).data.online;
  await host.call({type:'close'});
  equal((await other.call({type:'list'})).data.rooms.length,0,'explicit host close removes room');
  await other.event('delta',message=>message.remove?.includes(closedRoom));
  equal((await closingPeer.event('join_cancelled')).id,closingJoin.data.id,'close cancels pending admissions');
  equal((await closingPeer.call({type:'resolve',code:closedCode,role:'racer',key:key(),compatibility:'test-network'})).error.code,'unavailable','closed code is invalid while the host remains online');
  equal((await other.call({type:'heartbeat'})).data.online,countBeforeClose,'closing a room preserves online presence');
  state.epoch='epoch-after-close';state.code=state.bootstrap='EFGH2';
  room=(await host.call({type:'room',revision:0,state})).data;
  check(room.id!==closedRoom,'same connected launcher can create a fresh room immediately');
  equal((await other.call({type:'list'})).data.rooms[0].id,room.id,'replacement public room is listed');
  await host.call({type:'close'});
  state.visibility='private';state.epoch='private-after-close';state.code=state.bootstrap='JKLM3';
  room=(await host.call({type:'room',revision:0,state})).data;
  const privateAfterClose=await closingPeer.call({type:'resolve',code:state.code,role:'spectator',key:key(),compatibility:'test-network'});
  check(privateAfterClose.ok,'same launcher can recreate a private code lobby');
  await host.call({type:'close'});
  equal((await closingPeer.event('join_cancelled',message=>message.id===privateAfterClose.data.id)).id,privateAfterClose.data.id,'private lobby close also cancels pending admissions');
  equal((await closingPeer.call({type:'resolve',code:state.code,role:'racer',key:key(),compatibility:'test-network'})).error.code,'unavailable','private code stops resolving without restarting the launcher');
  closingPeer.socket.close(1000,'close lifecycle checks complete');await pause(50);
  state.visibility='public';
  duplicate.socket.close(1000,'test done');await pause(50);
  equal((await other.call({type:'heartbeat'})).data.online,3,'closing one duplicate leaves the account counted while another remains');
  if(process.argv.includes('--lease')){
   await host.call({type:'room',revision:0,state});
   const timer=setInterval(()=>other.call({type:'heartbeat'}).catch(()=>{}),15000);
   try{console.log('Checking real 60-second crash/sleep expiry...');await pause(62000);
    const expired=await other.call({type:'list'});equal(expired.data.rooms.length,0,'stale host disappears without a clean disconnect');
    equal(expired.data.online,1,'unresponsive accounts expire while live peer remains');
   }finally{clearInterval(timer);}
  }
  if(process.argv.includes('--load')){
   for(const socket of sockets)try{socket.close(1000,'start load scenario');}catch{}
   await pause(150);
   const loadStart=Date.now(),clients=[];
   for(let start=0;start<1000;start+=100){
    const statements=[];
    for(let index=start;index<start+100;index++){
     const account='load_'+index,token=randomBytes(32).toString('base64url');tokens[account]=token;
     statements.push(db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(account,account,'Load '+index,'player','active',now,now));
     statements.push(db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(token).digest('base64url'),account,now,now,now+3600,now+86400));
    }
    await db.batch(statements);
   }
   const heartbeatTimer=setInterval(()=>{for(const client of clients)client.call({type:'heartbeat'}).catch(()=>{});},15000);
   try{
    for(let start=0;start<1000;start+=25){clients.push(...await Promise.all(Array.from({length:25},(_,offset)=>open('load_'+(start+offset)))));}
    equal((await clients[999].call({type:'heartbeat'})).data.online,1000,'load: 1000 distinct live accounts');
    const rooms=[];
    for(let start=0;start<100;start+=20){
     rooms.push(...(await Promise.all(Array.from({length:20},(_,offset)=>{
      const index=start+offset;
      return clients[index].call({type:'room',revision:0,state:{...state,epoch:'load-room-'+index,title:'Load room '+index,code:String(index).padStart(5,'0'),bootstrap:String(index).padStart(5,'0')}});
     }))).map(reply=>reply.data));
    }
    check(rooms.every(room=>room?.id),'load: 100 hosted rooms');
    const first=(await clients[999].call({type:'list'})).data;
    equal(first.rooms.length,50,'load: public snapshot paginated');check(first.nextCursor,'load: pagination cursor provided');
    const second=(await clients[999].call({type:'list',cursor:first.nextCursor})).data;
    equal(new Set([...first.rooms,...second.rooms].map(room=>room.id)).size,100,'load: global catalogue contains all 100 rooms');
    const contenders=await Promise.all(clients.slice(100,200).map(client=>client.call({type:'join',room:rooms[0].id,role:'racer',key:key(),compatibility:'test-network'})));
    equal(contenders.filter(result=>result.ok).length,1,'load: 100 simultaneous joins reserve exactly one last place');
    equal(contenders.filter(result=>result.error?.code==='full').length,99,'load: other last-place contenders receive full');
    console.log(JSON.stringify({loadSessions:1000,hostedRooms:100,simultaneousJoins:100,seconds:(Date.now()-loadStart)/1000,scope:'Local Cloudflare runtime; no geographical or gameplay claim.'}));
   }finally{clearInterval(heartbeatTimer);}
  }
  options.workers[0].bindings.ONLINE_ENABLED='false';options.workers[1].bindings.ONLINE_ENABLED='false';
  await mf.setOptions(options);
  equal((await (await http('/v1/online/capabilities')).json()).enabled,false,'feature disable is advertised');
  equal((await http('/v1/online/ticket','host',host.instance,'POST')).status,503,'disabled service refuses new admission channels');
  console.log(JSON.stringify({checks,fullWorkerUpgrade:true,sqliteDurableObject:true,serviceBinding:true,runningSpectators,featureDisable:true,liveDeployment:false}));
 }finally{for(const socket of sockets)try{socket.close(1000,'test complete');}catch{}await mf.dispose();}
}
main().catch(error=>{console.error(error.stack||error);process.exitCode=1;});
