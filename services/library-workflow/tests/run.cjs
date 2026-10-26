const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const {webcrypto,createHash}=require('node:crypto');
const {fixture,draft}=require('./support.cjs');
const stage=path.resolve(process.argv[2]||'build/library-workflow-20261022-r1/stage');
let checks=0;
function check(value,label){assert.ok(value,label);checks++;}
function equal(a,b,label){assert.equal(a,b,label);checks++;}
const read=name=>fs.readFileSync(path.join(stage,'FTP-PATCH/assets',name),'utf8');
async function backendTests(){
  const f=fixture(stage),{call,sqlite}=f;
  let r=await call('/v1/workflow/batches',undefined,null);equal(r.status,401,'anonymous access denied');
  r=await call('/v1/workflow/batches',undefined,'player');equal(r.status,403,'player access denied');
  const op='1'.repeat(32);
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft()});equal(r.status,200,JSON.stringify(r));
  const firstId=r.data.draft.id;equal(r.data.draft.revision,1,'new draft revision');
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft()});equal(r.data.draft.id,firstId,'lost save response reconciles same ID');equal(r.data.draft.revision,1,'retry does not increment unchanged revision');
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft({title:'Edited'}),expectedRevision:1});equal(r.data.draft.revision,2,'draft CAS update');
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft({title:'Stale edit'}),expectedRevision:1});equal(r.status,409,'stale revision denied');
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft({title:'Edited'}),expectedRevision:2,publish:true});equal(r.status,200,JSON.stringify(r));const itemId=r.data.item.id;
  r=await call('/v1/workflow/entry',{operationId:op,draft:draft(),publish:true});equal(r.data.item.id,itemId,'lost publication response returns original item');
  equal(sqlite.prepare('SELECT count(*) n FROM library_items').get().n,1,'one item after repeated publication');
  equal(sqlite.prepare('SELECT count(*) n FROM library_releases').get().n,1,'one release after repeated publication');
  const missing=await call('/v1/workflow/entry',{operationId:'2'.repeat(32),draft:draft({author:''}),publish:true});equal(missing.status,400,'required publication details validated');
  r=await call('/v1/workflow/entry',{operationId:'3'.repeat(32),draft:draft({fileUrl:'https://unapproved.invalid/new.zip'}),publish:true});equal(r.status,400,'approved hosting roots retained');
  const batchId='a'.repeat(32),rowId='b'.repeat(32),content={title:'Batch one',defaults:draft({fileUrl:'',fileName:''}),rows:[{id:rowId,draft:draft({fileUrl:'https://positron-portal.com/dkr-r/downloads/Tracks/batch.zip'}),selected:true,image:null,download:null}]};
  r=await call('/v1/workflow/batches/'+batchId,{batch:content,expectedRevision:0});equal(r.status,200,JSON.stringify(r));equal(r.data.batch.revision,1,'batch created');
  r=await call('/v1/workflow/batches/'+batchId,{batch:content,expectedRevision:0});equal(r.data.batch.revision,1,'batch create retry is idempotent');
  const savedEntryId='c'.repeat(32),savedEntryBatch={...content,rows:[{...content.rows[0],draft:{...content.rows[0].draft,id:firstId,revision:2}}]};
  r=await call('/v1/workflow/batches/'+savedEntryId,{batch:savedEntryBatch,expectedRevision:0});equal(r.status,200,'saved entry metadata accepted');equal(r.data.batch.rows[0].draft.id,firstId,'saved entry identifier preserved');equal(r.data.batch.rows[0].draft.revision,2,'saved entry revision preserved');
  r=await call('/v1/workflow/batches/'+savedEntryId,{batch:{...savedEntryBatch,rows:[{...savedEntryBatch.rows[0],draft:{...savedEntryBatch.rows[0].draft,revision:0}}]},expectedRevision:1});equal(r.status,409,'invalid saved entry revision still rejected');
  r=await call('/v1/workflow/batches/'+savedEntryId,{batch:{...savedEntryBatch,rows:[{...savedEntryBatch.rows[0],draft:{...savedEntryBatch.rows[0].draft,id:'invalid/id'}}]},expectedRevision:1});equal(r.status,400,'invalid saved entry identifier still rejected');
  r=await call('/v1/workflow/batches/'+batchId,undefined,'admin');equal(r.status,404,'other staff cannot read private batch');
  r=await call('/v1/workflow/batches/'+batchId,{batch:{...content,title:'Second'},expectedRevision:1});equal(r.data.batch.revision,2,'batch edit');
  r=await call('/v1/workflow/batches/'+batchId,{batch:content,expectedRevision:1});equal(r.status,409,'batch stale write denied');
  r=await call('/v1/workflow/batches/'+batchId,{batch:{...content,rows:Array(51).fill(content.rows[0])},expectedRevision:2});equal(r.status,400,'batch bounds enforced');
  r=await call('/v1/workflow/batches/'+batchId,{batch:{...content,rows:[...content.rows,content.rows[0]]},expectedRevision:2});equal(r.status,400,'duplicate row IDs denied');
  sqlite.prepare("INSERT INTO library_upload_jobs(id,owner_id,kind,total_size,original_name,state) VALUES('up_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','test_admin','image',42,'cover.png','open')").run();
  r=await call('/v1/workflow/batches/'+batchId,{batch:{...content,rows:[{...content.rows[0],image:{name:'cover.png',size:42,jobId:'up_'+'a'.repeat(32)}}]},expectedRevision:2});equal(r.status,409,'foreign upload references denied');
  const origin='https://positron-portal.com/dkr-r/';
  sqlite.prepare("UPDATE library_items SET archived=1,image=? WHERE id=?").run(origin+'images/Tracks/used.png',itemId);
  sqlite.prepare('INSERT INTO library_releases(id,item_id,version,file_url) VALUES(?,?,?,?)').run('old_release',itemId,'0.1',origin+'downloads/Tracks/old.zip');
  await call('/v1/workflow/entry',{operationId:'4'.repeat(32),draft:draft({fileUrl:origin+'downloads/Tracks/draft.zip',image:origin+'images/Tracks/same%20name.png'})});
  const urls=['downloads/Tracks/old.zip','downloads/Tracks/draft.zip','downloads/Tracks/batch.zip','images/Tracks/used.png','images/Tracks/same%20name.png','downloads/Characters/old.zip','downloads/Tracks/unused.zip'];
  const usage=await f.context.modules.library.workflowUsage(f.env,urls.map(url=>({url:origin+url})));
  equal(JSON.stringify(usage.map(v=>v.inLibrary)),JSON.stringify([true,true,true,true,true,false,false]),'usage covers history, archived, saved drafts and batches, preserves folder identity');
  r=await call('/v1/workflow/entry',{operationId:'5'.repeat(32),draft:draft({category:'legacy',title:'Custom vehicle'}),publish:true});equal(r.status,200,'Vehicles retain legacy category compatibility');equal(r.data.item.category,'legacy','API category unchanged');
  return f;
}
function clientRoot(fetcher,fast=false){
  const changes=new Set();let user={id:'test_moderator',role:'moderator',status:'active'};
  const timers=new Set();const backoffs=[];
  const root={DKR_CONFIG:{apiBase:'https://test.invalid'},crypto:webcrypto,fetch:fetcher,addEventListener(){},
    setTimeout(fn,ms){if(fast&&ms<=16000){backoffs.push(ms);return setTimeout(fn,0);}const timer=setTimeout(fn,ms);timers.add(timer);return timer;},clearTimeout(timer){timers.delete(timer);clearTimeout(timer);},
    DKRAuth:{ready:Promise.resolve(),getUser:()=>user,getToken:()=> 'mtest'.padEnd(43,'a'),ensureSession:async()=>{},onChange(fn){changes.add(fn);return()=>changes.delete(fn);}}};
  const context={window:root,URL,AbortController,TextEncoder,Uint8Array,Uint32Array,DataView,ReadableStream,Blob,console};
  const changeUser=next=>{user=next;for(const fn of changes)fn();};
  return {root,context,backoffs,changeUser,dispose(){for(const t of timers)clearTimeout(t);}};
}
function uploadFixture(options={}){
  let job=null,chunks=0,creates=0,completes=0,checks=0;const stored=[],manifest=[];
  const settings={enabled:true,configured:true,chunkBytes:4194304,limits:{download:2147483648,image:20971520},formats:{download:['.zip'],image:['.png','.jpg','.jpeg','.webp']}};
  const result=(data,status=200)=>({ok:status<400,status,json:async()=>structuredClone(data)});
  const fetcher=async(url,request)=>{
    const u=new URL(url),p=u.pathname;
    if(p.endsWith('/settings'))return result(settings);
    if(p==='/v1/uploads'&&request.method==='POST'){
      creates++;const input=JSON.parse(request.body);
      if(!job){job={id:'up_'+'a'.repeat(32),kind:input.kind,category:input.category,originalName:input.fileName,fileName:input.fileName,size:input.size,offset:0,chunkBytes:4194304,state:'open',fingerprint:input.fingerprint};if(options.badSavedPrefix){const bytes=Buffer.alloc(input.size,0);stored.push(bytes);manifest.push({offset:0,size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});job.offset=input.size;}}
      if(creates<=(options.createFailures||0))throw new TypeError('Connection lost');
      return result({upload:job});
    }
    if(p.endsWith('/chunk')){
      chunks++;
      if(chunks<=(options.chunkFailures||0))return result({error:{code:options.failureCode||'upload_ftp_timeout',message:'Simulated transfer error'}},options.failureStatus||502);
      const bytes=Buffer.from(await request.body.arrayBuffer()),offset=Number(u.searchParams.get('offset'));
      stored.push(bytes);manifest.push({offset,size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});job={...job,offset:offset+bytes.length};
      if(options.lostChunkResponse&&chunks===1)throw new TypeError('Response lost after commit');
      return result({upload:job});
    }
    if(p.endsWith('/complete')){
      completes++;
      job={...job,state:'completed',asset:{kind:job.kind,category:job.category,fileName:job.fileName,size:job.size,format:'.'+job.fileName.split('.').pop(),url:'https://positron-portal.com/dkr-r/'+(job.kind==='image'?'images':'downloads')+'/Tracks/'+job.fileName}};
      if(completes<=(options.completeFailures||0))throw new TypeError('Response lost');return result({upload:job});
    }
    if(p.endsWith('/integrity')){checks++;if(checks<=(options.integrityFailures||0))return result({error:{code:'request_failed',message:'temporary'}},503);const input=JSON.parse(request.body);equal(input.sha256,createHash('sha256').update(Buffer.concat(stored)).digest('hex'),'full-file integrity retained');return result({ok:true});}
    if(p.includes('/v1/uploads/up_'))return result({upload:job,resumeChunks:manifest});
    throw new Error('Unexpected test route: '+p);
  };
  const c=clientRoot(fetcher,true);vm.runInNewContext(read('uploads-client.js'),c.context);
  return {...c,get counts(){return {chunks,creates,completes,checks};}};
}
const png=(name='cover.png',tail='A')=>Object.assign(new Blob([new Uint8Array([137,80,78,71,13,10,26,10]),tail]),{name});
async function uploadTests(){
  let f=uploadFixture(),up=f.root.DKRUploads;
  await up.selectFile(png(),{kind:'image',category:'tracks'});await up.start();equal(up.state.status,'completed','first image completes');
  await up.selectFile(png('next.png','BBBB'),{kind:'image',category:'tracks'});equal(up.state.status,'ready','second image accepted without page reload');equal(up.state.job,null,'old completed job detached');f.dispose();
  f=uploadFixture({chunkFailures:5});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await up.start();equal(f.counts.chunks,6,'five retries then sixth attempt succeeds');equal(JSON.stringify(f.backoffs),JSON.stringify([1000,2000,4000,8000,16000]),'bounded exponential backoff');f.dispose();
  f=uploadFixture({chunkFailures:6});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await assert.rejects(up.start());equal(f.counts.chunks,6,'stops after five retries');equal(up.state.status,'paused','confirmed progress retained for manual retry');f.dispose();
  f=uploadFixture({lostChunkResponse:true});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await up.start();equal(f.counts.chunks,1,'lost acknowledgement reconciles instead of appending duplicate bytes');f.dispose();
  f=uploadFixture({createFailures:5,completeFailures:5});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await up.start();equal(f.counts.creates,6,'opening transfer retries with same creation identity');equal(f.counts.completes,6,'completion response retries safely');f.dispose();
  f=uploadFixture({chunkFailures:6,failureCode:'forbidden',failureStatus:403});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await assert.rejects(up.start());equal(f.counts.chunks,1,'access denial is not retried');f.dispose();
  f=uploadFixture({integrityFailures:5});up=f.root.DKRUploads;await up.selectFile(Object.assign(new Blob(['test package']),{name:'test.zip'}),{kind:'download',category:'tracks'});await up.start();equal(f.counts.checks,6,'checksum registration retries five times');equal(up.state.status,'completed','package completes with checksum');f.dispose();
  f=uploadFixture({badSavedPrefix:true});up=f.root.DKRUploads;await up.selectFile(png(),{kind:'image',category:'tracks'});await assert.rejects(up.start(),{code:'resume_prefix_mismatch'});checks++;equal(f.counts.chunks,0,'recovered creation checks saved prefix before sending bytes');equal(f.counts.completes,0,'mismatched prefix cannot complete');f.dispose();
}
async function batchTests(f){
  const calls=[];let loseSaveResponse=false,losePublishResponse=false;
  const c=clientRoot(async(url,options)=>{const route=new URL(url).pathname;calls.push(route);const input=options.body===undefined?undefined:JSON.parse(options.body);const r=await f.call(route,input);if(loseSaveResponse&&route.includes('/batches/')&&input){loseSaveResponse=false;throw new TypeError('Lost save response after commit');}if(losePublishResponse&&route.endsWith('/entry')&&input.publish&&r.status===200){losePublishResponse=false;throw new TypeError('Lost publication response after commit');}return {ok:r.status<400,status:r.status,json:async()=>r.data};});
  c.root.DKRUploads={state:{running:false,job:null},onChange(){},pause(){}};
  vm.runInNewContext(read('batch-client.js'),c.context);const B=c.root.DKRBatch;B.ensure();
  B.updateDefaults({author:'Bulk author',version:'1.0',summary:'Batch description',install:'Import package'});
  B.addHosted([{kind:'download',category:'tracks',fileName:'Bulk_One.zip',format:'.zip',url:'https://positron-portal.com/dkr-r/downloads/Tracks/bulk-one.zip'},{kind:'download',category:'tracks',fileName:'Bulk_Two.zip',format:'.zip',url:'https://positron-portal.com/dkr-r/downloads/Tracks/bulk-two.zip'}]);
  equal(B.state.batch.rows.length,2,'hosted packages produce two entries');const id=B.state.batch.id;
  await B.save();check(B.state.batch.revision>0,'batch saved');
  await B.process('save');check(B.state.batch.rows.every(r=>r.draft.id),'saved catalogue drafts per entry');
  B.review();await B.process('publish');check(B.state.batch.rows.every(r=>r.publishedItemId),'bulk publication');
  const count=f.sqlite.prepare('SELECT count(*) n FROM library_items').get().n;
  B.review();await B.process('publish');equal(f.sqlite.prepare('SELECT count(*) n FROM library_items').get().n,count,'repeated publish does not duplicate');
  await B.load(id);check(B.state.batch.rows.every(r=>r.publishedItemId),'batch publication status survives reload');
  await B.newBatch();B.updateDefaults({author:'Author',version:'1',summary:'Description',install:'Import'});B.addFiles([Object.assign(new Blob(['zip']),{name:'new_track.zip'}),png('new_track.png')]);
  equal(B.state.batch.rows.length,1,'local package queued');check(B.state.batch.rows[0].image,'cover matched by filename');
  const localId=B.state.batch.id;await B.save();await B.load(localId);equal(B.state.batch.rows[0].status,'Reselect original file','recovery requires browser file reselection');
  B.addFiles([Object.assign(new Blob(['zip']),{name:'new_track.zip'}),png('new_track.png')]);equal(B.state.batch.rows.length,1,'reselecting original files resumes existing row');equal(B.state.batch.rows[0].status,'Waiting to upload','reselected bytes available');
  B.setTitle('Lost save response test');loseSaveResponse=true;await assert.rejects(B.save());
  B.setTitle('New edits after response loss');await B.save();equal(B.state.batch.title,'New edits after response loss','new edits preserved after lost batch save response');
  await B.load(localId);equal(B.state.batch.title,'New edits after response loss','reconciled batch revision persists new edits');
  await B.newBatch();B.updateDefaults({author:'Author',version:'1',summary:'Description',install:'Import'});
  B.addHosted([{kind:'download',category:'tracks',fileName:'lost-reply.zip',format:'.zip',url:'https://positron-portal.com/dkr-r/downloads/Tracks/lost-reply.zip'},{kind:'download',category:'tracks',fileName:'other.zip',format:'.zip',url:'https://positron-portal.com/dkr-r/downloads/Tracks/other.zip'}]);
  losePublishResponse=true;B.review();await B.process('publish');equal(B.state.batch.rows[0].status,'Needs attention','unknown publication outcome retained for reconciliation');equal(B.state.batch.rows[1].status,'Published','one failed response does not stop other rows');
  const afterLoss=f.sqlite.prepare('SELECT count(*) n FROM library_items').get().n;
  B.review();await B.process('publish');equal(B.state.batch.rows[0].status,'Published','retry resolves original publication');equal(f.sqlite.prepare('SELECT count(*) n FROM library_items').get().n,afterLoss,'retry after lost response creates no duplicate');
  c.changeUser(null);equal(B.state.batch,null,'sign-out clears all private batch data');c.dispose();
}
function boundaries(){
  const baseline=fs.readFileSync(path.join(__dirname,'../baseline/PRIVATE-API/worker-single-file.mjs'),'utf8');
  const next=fs.readFileSync(path.join(stage,'PRIVATE-API/worker-single-file.mjs'),'utf8');
  const cut=s=>s.slice(s.indexOf('const __dkrFtps'),s.indexOf('const __dkrUploads'));
  check(cut(baseline).length>10000,'FTPS source boundary located');equal(cut(next),cut(baseline),'FTPS transport byte-for-byte unchanged');
  const ui=read('app.js');check(ui.includes("label:'Vehicles'")&&!ui.includes("?'Legacy Mods'"),'Vehicles navigation renamed');check(ui.includes("id:'legacy'"),'category key preserved');
  check(fs.readFileSync(path.join(stage,'FTP-PATCH/index.html'),'utf8').includes('vehicles, magic codes'),'page description updated');
}
(async()=>{boundaries();const f=await backendTests();await uploadTests();await batchTests(f);console.log('PASS:',checks,'workflow, recovery, retry, permission, publication and transport checks.');f.sqlite.close();})().catch(error=>{console.error(error);process.exitCode=1;});
