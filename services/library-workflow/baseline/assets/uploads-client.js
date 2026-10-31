/* Staff file uploads. File bytes, job details and access tokens stay in memory.
 * Every call checks the current Discord session. Only an identical upload chunk
 * may be retried automatically, after reading the server's confirmed offset.
 * DKRUploads: loadSettings, loadJobs, selectFile, selectJob, start, pause, cancel,
 * checkConnection, clear, onChange; state/settings/jobs are read-only snapshots.
 */
(function (root) {
  'use strict';
  const MAX_CHUNK = 4194304;
  const LIMITS = { download: 2147483648, image: 20971520 };
  const FORMATS = { download: ['.zip','.rtz','.dkrmap','.xdelta','.nrm','.dkrcode','.bps','.ips','.7z'], image: ['.png','.jpg','.jpeg','.webp'] };
  const CATEGORIES = ['tracks','characters','legacy','magic','textures'];
  const JOB_STATES = ['open','completing','completed','cancel_pending','cancelled','expired'];
  let auth = null, account = null, unsubscribe = null, generation = 0, selection = 0;
  let settings = null, jobs = [], selectedFile = null, selectedFingerprint = '', clientId = '';
  let verifiedPrefix = 0;
  let job = null, mode = 'idle', message = '', lastError = '', pauseRequested = false, stopping = false;
  let kind = '', category = '', active = null, preparing = 0, checking = false;
  const controllers = new Set(), listeners = new Set();

  function failure(text, code, status) { const e = new Error(text); e.code = code || 'upload_failed'; if (status) e.status = status; return e; }
  function stale() { return failure('The signed-in account or its permissions changed. Open the uploader again.', 'account_changed'); }
  function accountKey(instance) {
    const u = instance && instance.getUser && instance.getUser();
    return u && typeof u.id === 'string' ? JSON.stringify([u.id,u.role,u.status]) : null;
  }
  function snapshot() {
    return Object.freeze({status:mode, message, error:lastError, job, file:selectedFile ? Object.freeze({name:selectedFile.name,size:selectedFile.size}) : null,
      kind, category, offset:job ? job.offset : 0, size:job ? job.size : selectedFile ? selectedFile.size : 0,
      asset:job && job.asset || null, running:!!active, loading:preparing>0, checking, needsFile:!!job&&!selectedFile&&['open','completing'].includes(job.state)});
  }
  function notify() { const state = snapshot(); listeners.forEach(fn => { try { fn(state); } catch (_) { /* A view cannot break a transfer. */ } }); }
  function invalidate() {
    generation++; selection++; pauseRequested=true; stopping=true;
    controllers.forEach(controller=>controller.abort()); controllers.clear();
    settings=null;jobs=[];selectedFile=null;selectedFingerprint='';clientId='';verifiedPrefix=0;job=null;kind='';category='';
    mode='idle';message='';lastError='';active=null;checking=false;preparing=0;
    notify();
  }
  function syncAccount() {
    const next=root.DKRAuth||null;
    if(next!==auth){if(unsubscribe)unsubscribe();unsubscribe=null;auth=next;account=accountKey(auth);invalidate();if(auth&&auth.onChange)unsubscribe=auth.onChange(syncAccount);}
    const key=accountKey(auth);
    if(key!==account){account=key;invalidate();}
    return auth;
  }
  function assertCurrent(epoch, expected) { syncAccount(); if(epoch!==generation||expected!==account)throw stale(); }
  function apiBase() {
    let url;try{url=new URL((root.DKR_CONFIG||{}).apiBase);}catch(_){/* Report a fixed message. */}
    if(!url||url.protocol!=='https:'||url.username||url.password||url.search||url.hash)throw failure('The library service address needs to be configured.','configuration_required');
    return url.href.replace(/\/+$/,'');
  }
  async function request(path, body, epoch, expected, binary) {
    const instance=syncAccount();assertCurrent(epoch,expected);
    if(!instance||!instance.getUser||!instance.getToken)throw failure('Reload the page to restore the Discord sign-in connection.','auth_unavailable');
    await instance.ready;
    if(instance.ensureSession)await instance.ensureSession();
    assertCurrent(epoch,expected);
    const u=instance.getUser(),token=instance.getToken();
    if(!u||!token)throw failure('Sign in again, then resume the upload.','SIGN_IN_REQUIRED',401);
    if(u.status!=='active'||!['admin','moderator'].includes(u.role))throw failure('Moderator access is required to upload files.','forbidden',403);
    const controller=new AbortController();controllers.add(controller);
    let timedOut=false;
    const timer=root.setTimeout(()=>{timedOut=true;controller.abort();},binary?120000:90000);
    try {
      const headers={Accept:'application/json',Authorization:'Bearer '+token};
      if(body!==undefined)headers['Content-Type']=binary?'application/octet-stream':'application/json';
      const response=await root.fetch(apiBase()+path,{method:body===undefined?'GET':'POST',headers,
        body:body===undefined?undefined:binary?body:JSON.stringify(body),credentials:'omit',cache:'no-store',redirect:'error',signal:controller.signal});
      let data;try{data=await response.json();}catch(_){throw failure('The upload service returned an invalid response. Resume to check its saved progress.','invalid_response');}
      assertCurrent(epoch,expected);
      if(!response.ok){
        const detail=data&&data.error;
        const e=failure(detail&&typeof detail.message==='string'&&detail.message.length<=1000?detail.message:'The upload request could not be completed.',detail&&detail.code,response.status);
        if((response.status===401||response.status===403)&&instance.ensureSession){try{await instance.ensureSession({force:true});}catch(_){/* Preserve the request failure. */}assertCurrent(epoch,expected);}
        throw e;
      }
      if(!data||typeof data!=='object'||Array.isArray(data))throw failure('The upload service returned an invalid response.','invalid_response');
      return data;
    } catch(e) {
      assertCurrent(epoch,expected);
      if(e&&e.name==='AbortError')throw failure(timedOut?'The upload connection timed out. Resume to check its saved progress.':'The current transfer was interrupted.','request_timeout');
      if(e&&e.name==='TypeError'&&!e.code)throw failure('The connection was interrupted. Resume to check the saved upload progress.','network_error');
      throw e;
    } finally { root.clearTimeout(timer);controllers.delete(controller); }
  }
  function validSettings(value) {
    if(!value||typeof value.enabled!=='boolean'||typeof value.configured!=='boolean'||!Number.isSafeInteger(value.chunkBytes)||value.chunkBytes<1||value.chunkBytes>MAX_CHUNK||
      !value.limits||!Object.keys(LIMITS).every(k=>Number.isSafeInteger(value.limits[k])&&value.limits[k]>0&&value.limits[k]<=LIMITS[k])||
      !value.formats||!Object.keys(FORMATS).every(k=>Array.isArray(value.formats[k])&&value.formats[k].length>0&&value.formats[k].every(x=>FORMATS[k].includes(x))))throw failure('The upload settings returned by the service are invalid.','invalid_response');
    return Object.freeze({...value,limits:Object.freeze({...value.limits}),formats:Object.freeze({download:Object.freeze(value.formats.download.slice()),image:Object.freeze(value.formats.image.slice())})});
  }
  function validAsset(asset, record) {
    let url;try{url=new URL(asset&&asset.url);}catch(_){/* Fixed failure below. */}
    const extension='.'+String(record.fileName).split('.').pop().toLowerCase();
    if(!asset||asset.kind!==record.kind||asset.category!==record.category||asset.fileName!==record.fileName||asset.size!==record.size||asset.format!==extension||
      !url||url.protocol!=='https:'||url.username||url.password||url.search||url.hash)throw failure('The completed upload returned invalid file details.','invalid_response');
    return Object.freeze({...asset,url:url.href});
  }
  function validJob(record) {
    if(!record||typeof record.id!=='string'||!/^up_[a-f0-9]{32}$/.test(record.id)||!FORMATS[record.kind]||!CATEGORIES.includes(record.category)||
      typeof record.originalName!=='string'||!record.originalName||record.originalName.length>180||typeof record.fileName!=='string'||!record.fileName||record.fileName.length>180||
      !Number.isSafeInteger(record.size)||record.size<1||record.size>LIMITS[record.kind]||!Number.isSafeInteger(record.offset)||record.offset<0||record.offset>record.size||
      !Number.isSafeInteger(record.chunkBytes)||record.chunkBytes<1||record.chunkBytes>MAX_CHUNK||!JOB_STATES.includes(record.state)||!/^[a-f0-9]{64}$/.test(record.fingerprint))throw failure('The upload service returned invalid transfer details.','invalid_response');
    const result={...record,asset:record.asset?validAsset(record.asset,record):null};
    if(result.state==='completed'&&!result.asset)throw failure('The completed file address is missing. Refresh the upload list.','invalid_response');
    return Object.freeze(result);
  }
  function useJob(record) {
    job=validJob(record);kind=job.kind;category=job.category;
    jobs=jobs.filter(row=>row.id!==job.id);jobs.unshift(job);jobs=jobs.slice(0,100);
    return job;
  }
  async function loadSettings(options) {
    syncAccount();const epoch=generation,expected=account;
    if(settings&&!(options&&options.force))return settings;
    preparing++;notify();
    try{settings=validSettings(await request('/v1/uploads/settings',undefined,epoch,expected));lastError='';return settings;}
    catch(e){if(epoch===generation){lastError=e.message;}throw e;}
    finally{if(epoch===generation){preparing=Math.max(0,preparing-1);notify();}}
  }
  async function loadJobs() {
    syncAccount();const epoch=generation,expected=account;preparing++;notify();
    try{
      const response=await request('/v1/uploads',undefined,epoch,expected);
      if(!Array.isArray(response.uploads)||response.uploads.length>100)throw failure('The saved upload list is invalid.','invalid_response');
      jobs=response.uploads.map(validJob);lastError='';return jobs.slice();
    }catch(e){if(epoch===generation)lastError=e.message;throw e;}
    finally{if(epoch===generation){preparing=Math.max(0,preparing-1);notify();}}
  }
  function validateFile(file, type, cat, config) {
    if(!FORMATS[type]||!CATEGORIES.includes(cat))throw failure('Choose a valid upload category.','invalid_category');
    if(!file||typeof file.name!=='string'||typeof file.slice!=='function'||!Number.isSafeInteger(file.size)||file.size<1)throw failure('Choose a non-empty file from your computer.','invalid_file');
    if(file.name.length>180||/[\u0000-\u001f\u007f/\\]/.test(file.name))throw failure('Use a filename of at most 180 characters without control characters or slashes.','invalid_file_name');
    const ext='.'+file.name.split('.').pop().toLowerCase();
    if(!config.formats[type].includes(ext))throw failure(type==='image'?'Choose a PNG, JPEG or WebP cover image.':'This file type is not supported. Choose a ZIP, RTZ, DKRMAP, XDELTA, NRM, DKRCODE, BPS, IPS or 7Z package.','unsupported_file');
    if(file.size>config.limits[type])throw failure(type==='image'?'Cover images must be 20 MiB or smaller.':'Mod packages must be 2 GiB or smaller.','file_too_large');
    return ext;
  }
  async function fingerprint(file) {
    if(!root.crypto||!root.crypto.subtle)throw failure('This browser needs a secure HTTPS connection to prepare uploads.','crypto_unavailable');
    const prefix=new TextEncoder().encode('dkr-upload-v1\n'+file.size+'\n');
    const first=new Uint8Array(await file.slice(0,65536).arrayBuffer());
    const last=new Uint8Array(await file.slice(Math.max(0,file.size-65536)).arrayBuffer());
    const bytes=new Uint8Array(prefix.length+first.length+last.length);bytes.set(prefix);bytes.set(first,prefix.length);bytes.set(last,prefix.length+first.length);
    return Array.from(new Uint8Array(await root.crypto.subtle.digest('SHA-256',bytes)),n=>n.toString(16).padStart(2,'0')).join('');
  }
  // Bounded-memory SHA-256 for full packages, not the resume sample. File bytes
  // are checked against the server's confirmed chunk digests in the same pass.
  class PackageSha256 {
    constructor(){this.h=new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);this.buffer=new Uint8Array(64);this.used=0;this.length=0;this.w=new Uint32Array(64);}
    block(bytes,at){
      const k=PackageSha256.k,w=this.w,ror=(x,n)=>(x>>>n)|(x<<(32-n));
      for(let i=0;i<16;i++){const p=at+i*4;w[i]=(bytes[p]<<24)|(bytes[p+1]<<16)|(bytes[p+2]<<8)|bytes[p+3];}
      for(let i=16;i<64;i++){const a=w[i-15],b=w[i-2];w[i]=(w[i-16]+(ror(a,7)^ror(a,18)^(a>>>3))+w[i-7]+(ror(b,17)^ror(b,19)^(b>>>10)))>>>0;}
      let [a,b,c,d,e,f,g,h]=this.h;
      for(let i=0;i<64;i++){const t1=(h+(ror(e,6)^ror(e,11)^ror(e,25))+((e&f)^(~e&g))+k[i]+w[i])>>>0,t2=((ror(a,2)^ror(a,13)^ror(a,22))+((a&b)^(a&c)^(b&c)))>>>0;h=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;}
      [a,b,c,d,e,f,g,h].forEach((x,i)=>{this.h[i]=(this.h[i]+x)>>>0;});
    }
    update(bytes){
      this.length+=bytes.length;let at=0;
      if(this.used){const n=Math.min(64-this.used,bytes.length);this.buffer.set(bytes.subarray(0,n),this.used);this.used+=n;at=n;if(this.used===64){this.block(this.buffer,0);this.used=0;}}
      while(at+64<=bytes.length){this.block(bytes,at);at+=64;}
      if(at<bytes.length){this.buffer.set(bytes.subarray(at),0);this.used=bytes.length-at;}
    }
    hex(){const bits=this.length*8,tail=new Uint8Array(this.used<56?64-this.used:128-this.used);tail[0]=128;const view=new DataView(tail.buffer);view.setUint32(tail.length-8,Math.floor(bits/4294967296));view.setUint32(tail.length-4,bits>>>0);this.update(tail);return Array.from(this.h,n=>n.toString(16).padStart(8,'0')).join('');}
  }
  PackageSha256.k=new Uint32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
  async function preparePackageDownload(file,epoch,expected,run){
    const saved=await request('/v1/uploads/'+job.id,undefined,epoch,expected);
    assertSelection(epoch,expected,run);
    const current=validJob(saved.upload),parts=saved.resumeChunks;
    if(current.id!==job.id||current.state!=='completed'||current.size!==file.size||current.offset!==file.size||!Array.isArray(parts)||parts.length>512)
      throw failure('The completed package could not be checked. Retry this upload.','invalid_response');
    const full=new PackageSha256();let end=0;
    for(const part of parts){
      assertSelection(epoch,expected,run);
      if(!part||part.offset!==end||!Number.isSafeInteger(part.size)||part.size!==Math.min(current.chunkBytes,file.size-end)||part.size<1||!/^[a-f0-9]{64}$/.test(part.sha256))throw failure('The completed upload has invalid chunk records.','invalid_response');
      const bytes=new Uint8Array(await file.slice(end,end+part.size).arrayBuffer());
      assertSelection(epoch,expected,run);
      const digest=Array.from(new Uint8Array(await root.crypto.subtle.digest('SHA-256',bytes)),n=>n.toString(16).padStart(2,'0')).join('');
      if(bytes.length!==part.size||digest!==part.sha256)throw failure('The selected file no longer matches its uploaded bytes. Do not publish this file.','upload_file_changed');
      // Yield between 256 KiB blocks so controls remain responsive while hashing.
      for(let at=0;at<bytes.length;at+=262144){full.update(bytes.subarray(at,at+262144));await new Promise(resolve=>root.setTimeout(resolve,0));assertSelection(epoch,expected,run);}
      end+=bytes.length;message='Preparing the download: '+Math.round(end/file.size*100)+'%';notify();
    }
    if(end!==file.size)throw failure('The package checksum is incomplete.','invalid_response');
    await request('/v1/launcher/uploads/'+job.id+'/integrity',{sha256:full.hex(),bytes:end},epoch,expected);
    assertSelection(epoch,expected,run);
  }

  async function checkImage(file, extension) {
    const bytes=new Uint8Array(await file.slice(0,12).arrayBuffer());
    const png=bytes.length>=8&&[137,80,78,71,13,10,26,10].every((n,i)=>bytes[i]===n);
    const jpeg=bytes.length>=3&&bytes[0]===255&&bytes[1]===216&&bytes[2]===255;
    const webp=bytes.length>=12&&String.fromCharCode(...bytes.slice(0,4))==='RIFF'&&String.fromCharCode(...bytes.slice(8,12))==='WEBP';
    if(!(extension==='.png'?png:extension==='.webp'?webp:jpeg))throw failure('The cover contents do not match its image extension. Export it as PNG, JPEG or WebP and try again.','invalid_image');
  }
  function randomId() { const b=new Uint8Array(16);root.crypto.getRandomValues(b);return Array.from(b,n=>n.toString(16).padStart(2,'0')).join(''); }
  function assertSelection(epoch,expected,run) { assertCurrent(epoch,expected);if(run!==selection)throw failure('File preparation was interrupted. Choose a file again when ready.','selection_changed'); }
  async function verifyResumePrefix(record,manifest,file,epoch,expected,run,knownPrefix) {
    if(!Array.isArray(manifest)||manifest.length>512)throw failure('The saved upload verification details are missing. Update the upload service before resuming.','invalid_response');
    let end=0;
    for(const part of manifest){
      if(!part||!Number.isSafeInteger(part.offset)||part.offset!==end||!Number.isSafeInteger(part.size)||part.size<1||part.size!==Math.min(record.chunkBytes,record.size-part.offset)||
        !/^[a-f0-9]{64}$/.test(part.sha256)||part.offset+part.size>record.offset)throw failure('The saved upload verification details are inconsistent.','invalid_response');
      end+=part.size;
    }
    if(end!==record.offset)throw failure('The saved upload verification details are incomplete.','invalid_response');
    for(const part of manifest){
      assertSelection(epoch,expected,run);
      if(part.offset+part.size<=knownPrefix)continue;
      const bytes=await file.slice(part.offset,part.offset+part.size).arrayBuffer();assertSelection(epoch,expected,run);
      const digest=Array.from(new Uint8Array(await root.crypto.subtle.digest('SHA-256',bytes)),n=>n.toString(16).padStart(2,'0')).join('');assertSelection(epoch,expected,run);
      if(digest!==part.sha256)throw failure('The selected file differs from the part already uploaded. Choose the original unchanged file, or cancel and start a new upload.','resume_prefix_mismatch');
      message='Checking the saved part of the selected file: '+Math.round(100*(part.offset+part.size)/record.offset)+'%…';notify();
    }
    return record.offset;
  }
  async function selectFile(file, options) {
    syncAccount();if(active)throw failure('Pause the current upload before choosing a file.','upload_busy');
    const epoch=generation,expected=account,run=++selection;
    const type=options&&options.kind,cat=options&&options.category;
    if(job&&['open','completing','cancel_pending'].includes(job.state)&&(job.kind!==type||job.category!==cat))throw failure('Finish or cancel the current upload before choosing a different destination.','upload_busy');
    selectedFile=null;selectedFingerprint='';verifiedPrefix=0;
    mode='checking';message='Checking the selected file…';lastError='';notify();
    try{
      const config=await loadSettings();assertCurrent(epoch,expected);
      if(!config.enabled||!config.configured)throw failure(config.message||'The site owner needs to configure the upload connection.','upload_configuration_required');
      const extension=validateFile(file,type,cat,config);
      if(type==='image')await checkImage(file,extension);
      const hash=await fingerprint(file);assertSelection(epoch,expected,run);
      if(job&&['open','completing','completed','cancel_pending'].includes(job.state)){
        if(job.state==='cancel_pending')throw failure('Finish cancelling this upload before selecting another file.','cleanup_pending');
        if(job.size!==file.size||job.fingerprint!==hash)throw failure('This is not the file used for that upload. Choose the original unchanged file to resume.','resume_file_mismatch');
        const data=await request('/v1/uploads/'+job.id,undefined,epoch,expected);assertSelection(epoch,expected,run);
        const saved=validJob(data.upload);
        if(saved.id!==job.id||saved.fingerprint!==hash||saved.size!==file.size)throw failure('The saved upload no longer matches this file.','resume_file_mismatch');
        verifiedPrefix=await verifyResumePrefix(saved,data.resumeChunks,file,epoch,expected,run,0);assertSelection(epoch,expected,run);useJob(saved);
      }else{job=null;clientId=randomId();verifiedPrefix=0;}
      selectedFile=file;selectedFingerprint=hash;kind=type;category=cat;mode=job?'paused':'ready';message=job?'The saved portion matches the selected file. Ready to resume.':'Ready to upload.';lastError='';notify();return snapshot();
    }catch(e){if(epoch===generation&&run===selection){mode=job?'paused':'error';lastError=e.message;message='';notify();}throw e;}
  }
  function selectJob(record) {
    syncAccount();if(active)throw failure('Pause the current upload first.','upload_busy');
    const candidate=validJob(record);
    if(job&&job.id!==candidate.id&&['open','completing','cancel_pending'].includes(job.state))throw failure('Finish or cancel the current upload before selecting another one.','upload_busy');
    selection++;selectedFile=null;selectedFingerprint='';clientId='';verifiedPrefix=0;useJob(candidate);lastError='';
    mode=job.state==='completed'?'completed':job.state==='cancel_pending'?'cancel_pending':job.state==='cancelled'?'cancelled':job.state==='expired'?'error':'paused';
    message=job.state==='completed'?'The uploaded file is ready to use.':job.state==='cancel_pending'?'Cancellation needs to finish removing the partial file.':'Choose the original unchanged file to resume.';notify();return snapshot();
  }
  function canRetry(e) { return !!e&&['network_error','request_timeout','upload_busy','upload_transfer_failed','upload_ftp_timeout'].includes(e.code); }
  async function delay(ms,epoch,expected) { await new Promise(resolve=>root.setTimeout(resolve,ms));assertCurrent(epoch,expected); }
  async function reconcile(epoch,expected) {
    const data=await request('/v1/uploads/'+job.id,undefined,epoch,expected);
    const checked=validJob(data.upload);
    if(checked.id!==job.id||checked.fingerprint!==selectedFingerprint||checked.size!==selectedFile.size)throw failure('The saved transfer does not match the selected file.','resume_file_mismatch');
    verifiedPrefix=await verifyResumePrefix(checked,data.resumeChunks,selectedFile,epoch,expected,selection,verifiedPrefix);
    useJob(checked);notify();return checked;
  }
  async function sendChunk(bytes,offset,epoch,expected) {
    for(let attempt=0;attempt<3;attempt++){
      if(pauseRequested||stopping)return false;
      try{
        const data=await request('/v1/uploads/'+job.id+'/chunk?offset='+offset,bytes,epoch,expected,true);
        const checked=validJob(data.upload);
        if(checked.id!==job.id||checked.offset!==offset+bytes.size)throw failure('The service reported an unexpected upload position. Resume to recheck the file.','upload_offset_mismatch',409);
        useJob(checked);verifiedPrefix=job.offset;return true;
      }catch(e){
        if(!canRetry(e)||attempt===2||pauseRequested||stopping)throw e;
        message='Connection interrupted. Checking the last saved chunk…';notify();
        await delay(350*(attempt+1),epoch,expected);
        await reconcile(epoch,expected);
        if(job.offset===offset+bytes.size)return true;
        if(job.offset!==offset||job.state!=='open')throw failure('The saved upload position changed. Resume to check it before sending more data.','upload_offset_mismatch',409);
        message='Retrying this chunk ('+(attempt+1)+' of 2)…';notify();
      }
    }
    return false;
  }
  async function transfer(epoch,expected) {
    try{
      if(!job){
        mode='uploading';message='Opening the encrypted transfer…';notify();
        const data=await request('/v1/uploads',{fileName:selectedFile.name,size:selectedFile.size,kind,category,fingerprint:selectedFingerprint,clientId},epoch,expected);
        useJob(data.upload);
        if(job.fingerprint!==selectedFingerprint||job.size!==selectedFile.size)throw failure('The new upload does not match the selected file.','invalid_response');
      }else{message='Checking saved progress…';notify();await reconcile(epoch,expected);}
      if(job.state==='cancel_pending')throw failure('Finish cancelling this upload before starting another transfer.','cleanup_pending');
      if(['cancelled','expired'].includes(job.state))throw failure('This upload has ended. Choose the file again to start a new upload.','upload_expired',410);
      while(job.offset<job.size&&!pauseRequested&&!stopping){
        mode='uploading';message='Uploading securely. Progress shows bytes confirmed by the file service.';notify();
        const offset=job.offset,bytes=selectedFile.slice(offset,Math.min(job.size,offset+job.chunkBytes));
        await sendChunk(bytes,offset,epoch,expected);assertCurrent(epoch,expected);notify();
      }
      if(stopping)return snapshot();
      if(pauseRequested){mode='paused';message='Upload paused. Keep this page open, or reselect the same file after returning.';notify();return snapshot();}
      if(job.state!=='completed'){
        mode='completing';message='Verifying the completed file and its final address…';notify();
        const data=await request('/v1/uploads/'+job.id+'/complete',{},epoch,expected);useJob(data.upload);
        if(job.state!=='completed'||!job.asset)throw failure('The file is still being checked. Resume to check whether completion succeeded.','upload_incomplete');
      }
      if(kind==='download'){mode='completing';message='Preparing the download checksum…';notify();await preparePackageDownload(selectedFile,epoch,expected,selection);}
      mode='completed';message='Upload complete. Ready to publish and download.';selectedFile=null;lastError='';notify();return snapshot();
    }catch(e){
      if(epoch!==generation)throw e;
      if(stopping)return snapshot();
      mode=job&&job.state==='cancel_pending'?'cancel_pending':'paused';lastError=e.message;message='The upload has stopped. Its confirmed progress is retained for a deliberate retry.';notify();throw e;
    }
  }
  function start() {
    syncAccount();if(active)return active;
    if(!selectedFile)return Promise.reject(failure('Choose the original file from your computer before starting or resuming.','file_required'));
    if(!settings||!settings.enabled||!settings.configured)return Promise.reject(failure('The site owner needs to configure the upload connection.','upload_configuration_required'));
    const epoch=generation,expected=account;pauseRequested=false;stopping=false;lastError='';
    active=Promise.resolve().then(()=>transfer(epoch,expected)).finally(()=>{if(epoch===generation){active=null;notify();}});notify();return active;
  }
  function pause() { if(!active)return;pauseRequested=true;mode='pausing';message='Pausing after the current chunk has been confirmed…';notify(); }
  async function cancel() {
    syncAccount();const epoch=generation,expected=account;
    selection++;
    stopping=true;pauseRequested=true;controllers.forEach(controller=>controller.abort());
    mode='cancelling';message='Stopping the transfer and removing its partial file…';lastError='';notify();
    if(active){try{await active;}catch(_){/* Revoke the server job below. */}}
    assertCurrent(epoch,expected);
    if(!job){selectedFile=null;selectedFingerprint='';clientId='';mode='cancelled';message='The selected file was cleared. If an opening request was interrupted, refresh saved uploads to check for its server job.';stopping=false;notify();return snapshot();}
    if(job.state==='completed'){mode='completed';message='The completed file remains on the server. Cancellation only removes partial uploads.';stopping=false;notify();return snapshot();}
    try{
      const data=await request('/v1/uploads/'+job.id+'/cancel',{},epoch,expected);useJob(data.upload);
      if(job.state==='completed'){selectedFile=null;mode='completed';message='The transfer completed before cancellation. Its completed file remains on the server.';notify();return snapshot();}
      const pending=!!data.cleanupPending||job.state==='cancel_pending';
      if(!pending&&job.state!=='cancelled'&&job.state!=='expired')throw failure('Cancellation has not been confirmed. Retry cleanup.','cleanup_pending');
      selectedFile=null;selectedFingerprint='';mode=pending?'cancel_pending':'cancelled';message=pending?'Upload stopped. The partial file still needs cleanup; retry when the connection is available.':'Upload cancelled. The partial file has been removed.';notify();return snapshot();
    }catch(e){if(epoch===generation){mode='cancel_pending';lastError=e.message;message='Cancellation could not be confirmed. Retry cleanup to remove the partial file.';notify();}throw e;}
    finally{if(epoch===generation){stopping=false;notify();}}
  }
  async function checkConnection() {
    syncAccount();const epoch=generation,expected=account;
    if(active)throw failure('Pause the upload before checking the connection.','upload_busy');
    checking=true;lastError='';notify();
    try{
      const u=auth&&auth.getUser();if(!u||u.role!=='admin')throw failure('Administrator access is required to check the connection.','forbidden',403);
      const result=await request('/v1/uploads/check',{},epoch,expected);
      if(result.ready!==true)throw failure('The file-service connection is not ready.','upload_configuration_required');
      return Object.freeze(result);
    }catch(e){if(epoch===generation)lastError=e.message;throw e;}
    finally{if(epoch===generation){checking=false;notify();}}
  }
  root.DKRUploads=Object.freeze({loadSettings,loadJobs,selectFile,selectJob,start,pause,cancel,checkConnection,
    clear(){syncAccount();invalidate();},onChange(fn){listeners.add(fn);return()=>listeners.delete(fn);},
    get state(){syncAccount();return snapshot();},get settings(){syncAccount();return settings;},get jobs(){syncAccount();return jobs.slice();}});
  syncAccount();
}(window));
