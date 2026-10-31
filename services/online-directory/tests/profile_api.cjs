// Local exported Worker + D1 + SQLite Durable Object + native X25519 fixture.
// No deployed endpoint, real account, launcher, ROM or GPU is used.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {randomBytes,createHash}=require('node:crypto');
const {execFileSync}=require('node:child_process');
const {deflateSync}=require('node:zlib');
const root=path.resolve(process.argv[2]),work=path.resolve(process.argv[3]),helper=path.resolve(process.argv[5]);
const {Miniflare}=require(path.resolve(process.argv[4]));
const {schema}=require('../../library-workflow/tests/support.cjs');
let checks=0;
const check=(value,label)=>{assert.ok(value,label);++checks;};
const equal=(a,b,label)=>{assert.equal(a,b,label);++checks;};
const random=()=>randomBytes(32).toString('hex');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function native(name,server='0'.repeat(64)) {
  return JSON.parse(execFileSync(helper,['--fixture',path.join(work,'profile-proof-fixture-'+name),server],{encoding:'utf8'}));
}
function crc(bytes){let c=0xffffffff;for(const b of bytes){c^=b;for(let n=0;n<8;n++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;}
function chunk(type,bytes){const kind=Buffer.from(type),length=Buffer.alloc(4),checksum=Buffer.alloc(4);length.writeUInt32BE(bytes.length);checksum.writeUInt32BE(crc(Buffer.concat([kind,bytes])));return Buffer.concat([length,kind,bytes,checksum]);}
function png(width=128,height=128,inflateExtra=0,metadata=false){
  const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(width,0);ihdr.writeUInt32BE(height,4);ihdr[8]=8;ihdr[9]=6;
  const raw=Buffer.alloc((128*4+1)*128+inflateExtra,85);for(let y=0;y<128;y++)raw[y*(128*4+1)]=0;
  return Buffer.concat([Buffer.from('89504e470d0a1a0a','hex'),chunk('IHDR',ihdr),...(metadata?[chunk('tEXt',Buffer.from('private filename'))]:[]),chunk('IDAT',deflateSync(raw)),chunk('IEND',Buffer.alloc(0))]);
}
async function main(){
  fs.mkdirSync(work,{recursive:true});
  const mf=new Miniflare({workers:[
    {name:'api',modules:true,modulesRoot:root,scriptPath:path.join(root,'PRIVATE-API/worker-single-file.mjs'),d1Databases:['DB'],serviceBindings:{ONLINE_DIRECTORY:'online'},
      bindings:{ONLINE_ENABLED:'true',RUNNING_SPECTATORS_ENABLED:'false',FRONTEND_URL:'https://positron-portal.com/dkr-r/index.html',DOWNLOAD_ORIGINS:'["https://positron-portal.com/dkr-r/downloads/"]',IMAGE_ORIGINS:'["https://positron-portal.com/dkr-r/images/"]'}},
    {name:'online',modules:true,modulesRoot:root,scriptPath:path.join(root,'ONLINE-WORKER/directory.mjs'),compatibilityDate:'2026-07-30',
      durableObjects:{DIRECTORY:{className:'WorldwideDirectory',useSQLite:true}},bindings:{ONLINE_ENABLED:'true',RUNNING_SPECTATORS_ENABLED:'false'}}
  ]});
  const sockets=[];
  try {
    const db=await mf.getD1Database('DB','api');await db.exec(schema);
    const tokens={},now=Math.floor(Date.now()/1000);
    for(const account of ['host','guest','other']){
      const token=randomBytes(32).toString('base64url');tokens[account]=token;
      await db.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').bind(account,account,'Player '+account,'player','active',now,now).run();
      await db.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').bind(createHash('sha256').update(token).digest('base64url'),account,now,now,now+3600,now+86400).run();
    }
    async function request(route,account,body,headers={}){
      return mf.dispatchFetch('https://test.invalid'+route,{method:body===undefined?'GET':'POST',
        headers:{Origin:'https://positron-portal.com',...(account?{Authorization:'Bearer '+tokens[account]}:{}),...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},
        body:body===undefined?undefined:typeof body==='string'?body:JSON.stringify(body)});
    }
    const pendingProfileRoute='/v1/online/ticket?profile='+random();
    equal((await request(pendingProfileRoute,'host',{}, {'X-DKRR-Instance':random()})).status,200,'pending optional profile migrations preserve authenticated online tickets');
    equal((await request(pendingProfileRoute,null,{}, {'X-DKRR-Instance':random()})).status,401,'pending optional profile migrations never bypass ticket authentication');
    for(const name of ['001-launcher-discord-profiles.sql','002-launcher-profiles.sql'])await db.exec(fs.readFileSync(path.join(root,'PRIVATE-API',name),'utf8').replace(/\s+/g,' '));
    async function publish(account,fixture,source='keep',extra={}){
      const card=native(fixture);const current=await (await request('/v1/launcher/profile?key='+card.publicKey,account)).json();
      const body={publicKey:card.publicKey,identity:card.identity,friendCode:card.friendCode,source,sharing:true,...extra};
      if(current.publicKey!==card.publicKey){
        const response=await request('/v1/launcher/profile-challenge',account,{publicKey:card.publicKey});equal(response.status,200,'challenge issued');
        const challenge=await response.json();body.challenge=challenge.id;body.proof=native(fixture,challenge.publicKey).proof;
      }
      return request('/v1/launcher/profile',account,body);
    }
    equal((await request('/v1/launcher/profile')).status,401,'profile requires Discord sign-in');
    equal((await request('/v1/launcher/profile-challenge',null,{publicKey:random()})).status,401,'anonymous key registration refused');
    equal((await request('/v1/launcher/profile-challenge','host',{publicKey:'0'.repeat(64)})).status,400,'low-order zero key rejected');
    const cardA=native('host-a'),cardB=native('host-b'),guestCard=native('guest'),otherCard=native('other');
    const challenged=await request('/v1/launcher/profile-challenge','host',{publicKey:cardA.publicKey});
    equal(challenged.status,200,'native challenge issued: '+await challenged.clone().text());
    const challenge=await challenged.json();
    let response=await request('/v1/launcher/profile','host',{publicKey:cardA.publicKey,identity:cardA.identity,friendCode:cardA.friendCode,source:'initials',sharing:true,challenge:challenge.id,proof:'0'.repeat(64)});
    equal(response.status,403,'wrong private-key proof refused: '+await response.clone().text());
    response=await request('/v1/launcher/profile','host',{publicKey:cardA.publicKey,identity:cardA.identity,friendCode:cardA.friendCode,source:'initials',sharing:true,challenge:challenge.id,proof:native('host-a',challenge.publicKey).proof});
    equal(response.status,403,'consumed challenge cannot be replayed');
    response=await publish('host','host-a','initials');equal(response.status,200,'native Monocypher proof accepted by Worker Web Crypto: '+await response.clone().text());
    equal((await response.json()).publicKey,cardA.publicKey,'registered key belongs to the native fixture');
    response=await publish('host','host-b');equal(response.status,200,'second launcher for same account is registered independently');
    equal((await (await request('/v1/launcher/profile?key='+cardA.publicKey,'host')).json()).publicKey,cardA.publicKey,'second instance does not replace first native identity');
    equal((await (await request('/v1/launcher/profile?key='+cardA.publicKey,'guest')).json()).publicKey,'','another account cannot use a native profile binding');
    const image=png();
    response=await publish('host','host-a','custom',{picture:image.toString('base64url')});equal(response.status,200,'bounded prepared PNG accepted: '+await response.clone().text());
    let profile=await response.json();
    check(/^https:\/\/dkr-mod-library-api\.ross-macgregorbars\.workers\.dev\/v1\/launcher\/profile-picture\/[a-f0-9]{64}\.png$/.test(profile.avatarUrl),'only canonical picture address returned');
    const pictureRoute=new URL(profile.avatarUrl).pathname;
    response=await request(pictureRoute);equal(response.status,200,'shared picture is available without disclosing account metadata');
    equal(response.headers.get('content-type'),'image/png','picture is served with a fixed media type');
    equal(response.headers.get('x-content-type-options'),'nosniff','picture cannot become active content');
    check(Buffer.from(await response.arrayBuffer()).equals(image),'stored PNG bytes remain exact');
    await publish('host','host-a','keep',{sharing:false});
    equal((await request(pictureRoute)).status,401,'unshared image requires authentication');
    equal((await request(pictureRoute,'guest')).status,404,'unshared picture is private to its owner');
    response=await request(pictureRoute,'host');equal(response.status,200,'owner can still use an unshared image');
    equal(response.headers.get('cache-control'),'private, no-store','unshared picture is not publicly cached');
    await db.prepare('DELETE FROM launcher_profile_limits').run();
    for(const [bad,label] of [[png(256,128),'wrong dimensions'],[png(128,128,1),'excess decompressed data'],[png(128,128,0,true),'metadata chunks'],[Buffer.from('<svg/>'),'active format'],[Buffer.alloc(98305),'oversized file']]){
      response=await publish('host','host-a','custom',{picture:bad.toString('base64url')});equal(response.status,400,'reject '+label);
    }
    response=await request('/v1/launcher/profile','host','x'.repeat(192*1024+1));equal(response.status,413,'JSON input is bounded while streaming');
    response=await request('/v1/launcher/profile','host','{}',{'Content-Type':'text/plain'});equal(response.status,415,'wrong request content type rejected');
    response=await publish('guest','guest');equal(response.status,200,'guest native profile registered');
    response=await publish('other','other');equal(response.status,200,'other native profile registered');
    await db.prepare('INSERT INTO launcher_discord_profiles VALUES(?,?,?,?)').bind('guest','Discord Racer','https://cdn.discordapp.com/embed/avatars/2.png?size=128',now).run();
    response=await publish('guest','guest','discord');equal(response.status,200,'Discord avatar can be selected explicitly');
    equal((await response.json()).avatarUrl,'https://cdn.discordapp.com/embed/avatars/2.png?size=128','Discord source comes from private OAuth metadata');
    equal((await publish('other','other','discord')).status,409,'missing OAuth metadata gives a sign-in instruction');
    async function open(account,card){
      const instance=random(),ticketResponse=await request('/v1/online/ticket?profile='+card.publicKey,account,{}, {'X-DKRR-Instance':instance});
      equal(ticketResponse.status,200,'directory ticket issued');const {ticket}=await ticketResponse.json();
      const upgraded=await mf.dispatchFetch('https://test.invalid/v1/online/connect',{headers:{Upgrade:'websocket','Sec-WebSocket-Protocol':'dkrr-online-v1, dkrr-ticket-'+ticket}});
      equal(upgraded.status,101,'directory upgraded');const socket=upgraded.webSocket,events=[],waiters=new Map();let serial=0;
      socket.addEventListener('message',event=>{const data=JSON.parse(event.data);events.push(data);if(data.type==='result'&&waiters.has(data.request)){const done=waiters.get(data.request);waiters.delete(data.request);done(data);}});
      socket.accept();sockets.push(socket);
      return {socket,account,instance,
        call(input,repeat){const id=repeat || 'p'+(++serial);return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{waiters.delete(id);reject(Error('No directory reply: '+JSON.stringify(input)));},6000);waiters.set(id,data=>{clearTimeout(timer);resolve(data);});socket.send(JSON.stringify({...input,request:id}));});},
        async event(type){for(let n=0;n<200;n++){const at=events.findIndex(row=>row.type===type);if(at>=0)return events.splice(at,1)[0];await pause(20);}throw Error('Missing event '+type);}
      };
    }
    const host=await open('host',cardA),guest=await open('guest',guestCard),other=await open('other',otherCard),duplicate=await open('host',cardB);
    const state={title:'Profile fixture',epoch:'profile-room',compatibility:'test-network',visibility:'public',phase:'waiting',racers:1,maxRacers:4,spectators:0,maxSpectators:8,reservedRacers:0,reservedSpectators:0,roster:'1',code:'FRND1',bootstrap:'FRND1',socialHost:'101',socialRoster:['101']};
    let room=(await host.call({type:'room',revision:0,state})).data;
    equal((await duplicate.call({type:'social',code:state.code,sender:'101'})).error.code,'social_membership','same account in another instance is not a lobby member');
    async function admit(client,sender){
      const joined=await client.call({type:'join',room:room.id,role:'racer',key:random(),compatibility:'test-network'});check(joined.ok,'join request created');
      const intent=await host.event('intent');check((await host.call({type:'grant',id:intent.join.id,allowed:true,admission:random()})).ok,'host native grant recorded');
      check((await host.call({type:'consume',id:intent.join.id,sender})).ok,'native identity consumed');
    }
    await admit(guest,'202');
    equal((await guest.call({type:'social',code:state.code,sender:'202'})).error.code,'social_membership','pending native handshake does not grant profile access before roster admission');
    state.racers=2;state.roster='2';state.socialRoster.push('202');room=(await host.call({type:'room',revision:room.revision,state})).data;
    let members=(await guest.call({type:'social',code:state.code,sender:'202'})).data.members;
    equal(members.length,2,'admitted guest can see verified host and guest profiles');
    equal(members.find(member=>member.sender==='101').publicKey,cardA.publicKey,'host profile is bound to the correct launcher instance');
    equal(members.find(member=>member.sender==='101').avatar,'','picture opt-out is respected in lobby metadata');
    equal(members.find(member=>member.sender==='202').identity,guestCard.identity,'member uses native identity instead of display-name matching');
    equal((await other.call({type:'social',code:state.code,sender:'202'})).error.code,'social_membership','outsider cannot impersonate an admitted sender');
    await admit(other,'303');state.racers=3;state.roster='3';state.socialRoster.push('303');room=(await host.call({type:'room',revision:room.revision,state})).data;
    members=(await guest.call({type:'social',code:state.code,sender:'202'})).data.members;
    equal(members.find(member=>member.sender==='303').friendCode,otherCard.friendCode,'client can obtain another admitted client friend capability');
    equal((await host.call({type:'social',code:state.code,sender:'101'})).data.members.length,3,'host has the same friend-request capability');
    check((await other.call({type:'social',code:state.code,sender:'303'},'repeat-social')).ok,'member can read current profiles');
    state.racers=2;state.roster='4';state.socialRoster=state.socialRoster.filter(sender=>sender!=='303');room=(await host.call({type:'room',revision:room.revision,state})).data;
    equal((await other.call({type:'social',code:state.code,sender:'303'})).error.code,'social_membership','leaving the native roster retires access immediately');
    equal((await other.call({type:'social',code:state.code,sender:'303'},'repeat-social')).error.code,'social_membership','replaying an earlier request cannot restore access after leaving');
    check((await host.call({type:'close'})).ok,'host closes lobby');
    equal((await guest.call({type:'social',code:state.code,sender:'202'})).error.code,'social_membership','closed lobby does not disclose stale player metadata');
    response=await publish('host','host-a','initials');equal(response.status,200,'initials can replace a custom picture');
    equal((await request(pictureRoute)).status,404,'removed picture is no longer served');
    await db.prepare('DELETE FROM launcher_profile_limits').run();
    for(let n=0;n<24;n++)equal((await request('/v1/launcher/profile-challenge','host',{publicKey:cardA.publicKey})).status,200,'bounded challenge attempt');
    equal((await request('/v1/launcher/profile-challenge','host',{publicKey:cardA.publicKey})).status,429,'profile changes are rate limited per account');
    console.log(JSON.stringify({checks,nativeKeyProof:true,privatePictures:true,instanceScopedIdentity:true,verifiedLobbyRoster:true,realCredentialsOrGameplayUsed:false,liveDeployment:false}));
  } finally {for(const socket of sockets)try{socket.close(1000,'fixture complete');}catch{}await mf.dispose();}
}
main().catch(error=>{console.error(error.stack||error);process.exitCode=1;});
