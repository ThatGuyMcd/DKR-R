/* Website-only queue. File bytes and session tokens stay in memory; drafts and
 * upload references are saved to the authenticated API. One FTP upload at a time. */
(function (root) {
  'use strict';
  const listeners = new Set(), files = new Map(), images = new Map(), errors = new Map();
  let batch = null, generation = 0, authority = null, running = false, paused = false, mode = '';
  let active = null, progress = null, reviewed = false, saved = '', saveError = '', loading = false;
  let saveTail = Promise.resolve(), saveTimer = null, pendingSave = null;
  const controllers = new Set();
  const packageRE = /\.(zip|rtz|dkrmap|xdelta|nrm|dkrcode|bps|ips|7z)$/i;
  const imageRE = /\.(png|jpe?g|webp)$/i;
  const clone = value => JSON.parse(JSON.stringify(value));
  const uid = () => Array.from(root.crypto.getRandomValues(new Uint8Array(16)), n => n.toString(16).padStart(2, '0')).join('');
  function key() { const u = root.DKRAuth && root.DKRAuth.getUser(); return u ? JSON.stringify([u.id, u.role, u.status]) : ''; }
  function assert(epoch) { if (epoch !== generation || authority !== key()) throw new Error('The signed-in account changed. Reopen the batch.'); }
  function defaults() { return { target:'new', targetId:'', category:'tracks', title:'', author:'', summary:'', version:'', format:'', fileUrl:'', fileName:'', image:'pattern', requirements:'', install:'', tags:[], related:[], featured:false }; }
  function content() { return batch ? { title:batch.title, defaults:batch.defaults, rows:batch.rows } : null; }
  function row(id) { const found = batch && batch.rows.find(r => r.id === id); if (!found) throw new Error('This entry is no longer in the batch.'); return found; }
  function pendingFile(r, kind) { return !!r[kind] && !(kind === 'image' ? r.draft.image && r.draft.image !== 'pattern' : r.draft.fileUrl); }
  function missing(r) {
    const fields = ['title','author','summary','version','install'];
    const names = { title:'title',author:'creator credit',summary:'description',version:'release version',install:'installation instructions' };
    const result = fields.filter(k => !String(r.draft[k] || '').trim()).map(k => names[k]);
    if (!r.draft.fileUrl) result.push('uploaded or hosted package');
    if (pendingFile(r,'image')) result.push('uploaded cover');
    return result;
  }
  function status(r) {
    if (r.publishedItemId) return 'Published';
    if (active && active.rowId === r.id) return progress && progress.status === 'completing' ? 'Verifying' : progress && ['paused','pausing'].includes(progress.status) ? 'Paused' : 'Uploading';
    if (errors.has(r.id)) return 'Needs attention';
    if (['image','download'].some(k => pendingFile(r,k) && !files.has(r.id+':'+k))) return 'Reselect original file';
    if (['image','download'].some(k => pendingFile(r,k))) return 'Waiting to upload';
    return missing(r).length ? 'Needs details' : 'Ready to publish';
  }
  function state() {
    return { batch:batch ? { ...clone(batch), rows:batch.rows.map(r => ({ ...clone(r), status:status(r), error:errors.get(r.id)||'', missing:missing(r) })) } : null,
      running, mode, paused, active:active&&{...active}, progress, reviewed, loading, saveError,
      dirty:!!batch && JSON.stringify(content()) !== saved, images:Array.from(images, ([id,file])=>({id,name:file.name})) };
  }
  function notify() { const s = state(); for (const fn of listeners) { try { fn(s); } catch (_) {} } }
  function clear() {
    generation++; root.clearTimeout(saveTimer); controllers.forEach(c=>c.abort());controllers.clear();
    files.clear();images.clear();errors.clear();batch=null;running=false;mode='';paused=false;active=null;progress=null;reviewed=false;saved='';saveError='';loading=false;saveTail=Promise.resolve();pendingSave=null;authority=key();notify();
  }
  function ensure() {
    const next = key(); if (authority !== next) clear();
    if (!batch) { authority=next;batch={id:uid(),revision:0,title:'New batch',defaults:defaults(),rows:[]};saved=JSON.stringify(content()); }
    return batch;
  }
  async function request(path, body, epoch = generation) {
    const auth = root.DKRAuth; assert(epoch);
    await auth.ready; if(auth.ensureSession) await auth.ensureSession(); assert(epoch);
    const u=auth.getUser(), token=auth.getToken();
    if(!u||!token||u.status!=='active'||!['admin','moderator'].includes(u.role))throw new Error('Sign in with moderator access to save this batch.');
    const base = new URL((root.DKR_CONFIG||{}).apiBase);
    if(base.protocol!=='https:'||base.username||base.password||base.search||base.hash)throw new Error('Invalid library service configuration.');
    const controller=new AbortController();controllers.add(controller);
    const timer=root.setTimeout(()=>controller.abort(),45000);
    try {
      const response=await root.fetch(base.href.replace(/\/+$/,'')+path,{method:body===undefined?'GET':'POST',
        headers:{Accept:'application/json',Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},
        body:body===undefined?undefined:JSON.stringify(body),credentials:'omit',cache:'no-store',redirect:'error',signal:controller.signal});
      let data;try{data=await response.json();}catch(_){throw new Error('The service returned an invalid response. Retry to check the saved result.');}
      assert(epoch);
      if(!response.ok){const e=new Error(data.error&&data.error.message||'The workflow request failed.');e.status=response.status;e.code=data.error&&data.error.code;throw e;}
      return data;
    } catch(e) { assert(epoch);if(e.name==='AbortError')throw new Error('The service took too long to respond. Retry to check the saved result.');throw e; }
    finally {root.clearTimeout(timer);controllers.delete(controller);}
  }
  function changed(silent) { reviewed=false;saveError='';schedule();if(!silent)notify(); }
  function schedule() {
    root.clearTimeout(saveTimer);
    if(batch&&batch.rows.length)saveTimer=root.setTimeout(()=>{save().catch(()=>{});},1400);
  }
  function save() {
    ensure();const epoch=generation;root.clearTimeout(saveTimer);
    const operation=saveTail.catch(()=>{}).then(async()=>{
      assert(epoch);const id=batch.id;
      async function submit(snapshot){
        const result=await request('/v1/workflow/batches/'+id,{batch:snapshot.input,expectedRevision:snapshot.revision},epoch);
        assert(epoch);
        if(!result.batch||result.batch.id!==id||!Number.isSafeInteger(result.batch.revision))throw new Error('The saved batch response is invalid.');
        batch.revision=result.batch.revision;saved=snapshot.fingerprint;pendingSave=null;
      }
      try {
        // Reconcile the exact previous snapshot first if its response was lost.
        // New edits stay in memory and are saved against the reconciled revision.
        if(pendingSave)await submit(pendingSave);
        const input=clone(content()),fingerprint=JSON.stringify(input);
        if(saved!==fingerprint||!batch.revision){pendingSave={input,fingerprint,revision:batch.revision};await submit(pendingSave);}
        saveError='';notify();return batch;
      }catch(e){if(epoch===generation){if(e.status&&e.status<500)pendingSave=null;saveError=e.message;notify();}throw e;}
    });
    saveTail=operation;return operation;
  }
  async function listBatches() {
    ensure();const epoch=generation;let cursor='',rows=[];
    do{const page=await request('/v1/workflow/batches'+(cursor?'?cursor='+encodeURIComponent(cursor):''),undefined,epoch);if(!Array.isArray(page.batches))throw new Error('Invalid saved batch list.');rows.push(...page.batches);cursor=page.nextCursor||'';if(rows.length>10000)throw new Error('Too many saved batches.');}while(cursor);
    return rows;
  }
  async function load(id) {
    if(running)throw new Error('Pause the queue before opening another batch.');
    if(!/^[a-f0-9]{32}$/.test(id))throw new Error('Invalid saved batch.');
    await saveIfDirty();const epoch=generation;loading=true;notify();
    try {
      const data=await request('/v1/workflow/batches/'+id,undefined,epoch), value=data.batch;
      if(!value||value.id!==id||!Array.isArray(value.rows)||value.rows.length>50||!Number.isSafeInteger(value.revision)||value.rows.some(r=>!/^[a-f0-9]{32}$/.test(r.id)||!r.draft))throw new Error('Invalid saved batch response.');
      assert(epoch);files.clear();images.clear();errors.clear();batch=value;reviewed=false;active=null;progress=null;saved=JSON.stringify(content());saveError='';
    } finally {if(epoch===generation){loading=false;notify();}}
  }
  async function saveIfDirty() {if(batch&&batch.rows.length&&JSON.stringify(content())!==saved)await save();}
  async function newBatch() {if(running)throw new Error('Pause the queue first.');await saveIfDirty();clear();ensure();notify();}
  function newRow(draft) {if(batch.rows.length>=50)throw new Error('A batch supports up to 50 entries. Start another batch for the remaining files.');const id=uid();const r={id,draft:{...defaults(),...clone(batch.defaults),...draft,operationId:id},image:null,download:null,selected:true,publishedItemId:''};batch.rows.push(r);return r;}
  function title(name) {return name.replace(/\.[^.]+$/,'').replace(/[_-]+/g,' ').replace(/\s+/g,' ').trim().slice(0,100);}
  function stem(name) {return name.replace(/\.[^.]+$/,'').toLowerCase().replace(/(?:[ _-](?:cover|image|artwork))$/,'').replace(/[ _-]+/g,'');}
  function descriptor(file) {return {name:file.name,size:file.size,jobId:'',clientId:uid()};}
  function attach(r,kind,file) {
    if(r.publishedItemId)throw new Error('Published entries cannot be replaced in this batch.');
    const old=r[kind];
    if(old&&old.name===file.name&&old.size===file.size){files.set(r.id+':'+kind,file);return;}
    if(old&&old.jobId&&!((kind==='image'?r.draft.image!=='pattern':!!r.draft.fileUrl)))throw new Error('Resume or cancel the existing upload before replacing this file.');
    r[kind]=descriptor(file);files.set(r.id+':'+kind,file);
    if(kind==='image')r.draft.image='pattern';else{r.draft.fileUrl='';r.draft.fileName=file.name;r.draft.format='.'+file.name.split('.').pop().toLowerCase();}
    errors.delete(r.id);
  }
  function matchImages() {
    for(const r of batch.rows){if(r.image||r.draft.image!=='pattern'||r.publishedItemId)continue;
      const base=stem(r.download?r.download.name:r.draft.fileName||'');
      const matches=Array.from(images.values()).filter(f=>stem(f.name)===base);
      const rows=batch.rows.filter(other=>stem(other.download?other.download.name:other.draft.fileName||'')===base);
      if(base&&matches.length===1&&rows.length===1)attach(r,'image',matches[0]);
    }
  }
  function addFiles(selected) {
    ensure();if(running)throw new Error('Pause the queue before adding files.');
    const incoming=Array.from(selected), unsupported=incoming.filter(f=>!imageRE.test(f.name)&&!packageRE.test(f.name));
    if(unsupported.length)throw new Error('Unsupported files: '+unsupported.map(f=>f.name).join(', '));
    const packages=incoming.filter(f=>packageRE.test(f.name));
    const additions=packages.filter(f=>!batch.rows.some(r=>r.download&&r.download.name===f.name&&r.download.size===f.size)).length;
    if(batch.rows.length+additions>50)throw new Error('A batch supports up to 50 entries. Select fewer packages.');
    for(const file of incoming)if(file.size<1||file.size>(imageRE.test(file.name)?20971520:2147483648))throw new Error(file.name+': images must be at most 20 MiB and packages at most 2 GiB, and files must not be empty.');
    for(const file of incoming){
      if(imageRE.test(file.name)){
        let found=false;for(const r of batch.rows)if(r.image&&r.image.name===file.name&&r.image.size===file.size){files.set(r.id+':image',file);found=true;}
        if(!found)images.set(uid(),file);
      }else{
        const r=batch.rows.find(r=>r.download&&r.download.name===file.name&&r.download.size===file.size)||newRow({title:title(file.name)});attach(r,'download',file);
      }
    }
    matchImages();changed();
  }
  function addHosted(records) {
    ensure();if(running)throw new Error('Pause the queue before adding entries.');
    const additions=records.filter(f=>f.kind==='download'&&!batch.rows.some(r=>r.draft.fileUrl===f.url));
    if(batch.rows.length+additions.length>50)throw new Error('A batch supports up to 50 entries.');
    for(const f of additions)newRow({category:f.category,title:title(f.fileName),fileUrl:f.url,fileName:f.fileName,format:f.format});
    changed();
  }
  function update(id, patch, silent=true) {
    const r=row(id);if(r.publishedItemId)throw new Error('This entry is already published.');
    if(patch.category!==undefined&&patch.category!==r.draft.category&&(r.download||r.image||r.draft.fileUrl))throw new Error('Clear the selected files before changing this entry’s category.');
    Object.assign(r.draft,patch);errors.delete(id);changed(silent);
  }
  function assignImage(id, localId) {const file=images.get(localId);if(!file)throw new Error('Choose an image again.');attach(row(id),'image',file);changed();}
  function attachFile(id,kind,file) {
    if(running)throw new Error('Pause the queue before changing a file.');
    if(!(kind==='image'?imageRE:packageRE).test(file.name))throw new Error('Choose a supported '+kind+' file.');
    attach(row(id),kind,file);changed();
  }
  function useImage(id,asset) {const r=row(id);if(r.publishedItemId)throw new Error('This entry is already published.');if(r.image&&r.image.jobId&&pendingFile(r,'image'))throw new Error('Cancel or finish this cover upload before replacing it.');if(asset.category!==r.draft.category)throw new Error('Choose a cover in this entry’s category.');r.draft.image=asset.url;r.image=null;files.delete(id+':image');changed();}
  async function clearFile(id,kind) {
    if(running)throw new Error('Pause the queue before removing a file.');const r=row(id), ref=r[kind];
    if(ref&&ref.jobId&&pendingFile(r,kind)){
      const up=root.DKRUploads;if(up.state.running)throw new Error('Pause the current upload first.');
      up.resetSelection({detach:true});up.selectJob(await up.loadJob(ref.jobId));
      await up.cancel();if(up.state.status==='cancel_pending')throw new Error('Cleanup is pending. Retry removing this file when the connection is available.');up.resetSelection();
    }
    r[kind]=null;files.delete(id+':'+kind);if(kind==='image')r.draft.image='pattern';else{r.draft.fileUrl='';r.draft.fileName='';r.draft.format='';}changed();
  }
  async function remove(id) {const r=row(id);if(running)throw new Error('Pause the queue first.');for(const k of ['image','download'])if(pendingFile(r,k))await clearFile(id,k);batch.rows=batch.rows.filter(v=>v.id!==id);files.delete(id+':image');files.delete(id+':download');errors.delete(id);changed();}
  function updateDefaults(patch) {ensure();Object.assign(batch.defaults,patch);changed(true);}
  function applyDefaults(fields) {for(const r of batch.rows)if(r.selected&&!r.publishedItemId)for(const field of fields)r.draft[field]=batch.defaults[field];changed();}
  function select(id,value) {row(id).selected=!!value;changed();}
  function setTitle(value) {ensure();batch.title=value.slice(0,100);changed(true);}
  function review() {ensure();reviewed=true;notify();}
  function pause() {paused=true;if(root.DKRUploads.state.running)root.DKRUploads.pause();notify();}
  async function upload(r,kind,epoch) {
    const ref=r[kind],file=files.get(r.id+':'+kind), up=root.DKRUploads;
    if(!file)throw new Error('Reselect '+ref.name+' to resume this entry.');
    if(up.state.running)throw new Error('Finish or pause the current form’s upload first.');
    up.resetSelection({detach:true});active={rowId:r.id,kind};progress=null;
    if(ref.jobId){const job=await up.loadJob(ref.jobId);assert(epoch);up.selectJob(job);}
    await up.selectFile(file,{kind,category:r.draft.category,resume:!!ref.jobId,clientId:ref.clientId});assert(epoch);
    if(paused)return;
    await up.start();assert(epoch);
    const value=up.state;
    if(value.status!=='completed'||!value.asset){if(paused)return;throw new Error(value.error||'The file has not finished uploading.');}
    if(kind==='image')r.draft.image=value.asset.url;else Object.assign(r.draft,{fileUrl:value.asset.url,fileName:value.asset.fileName,format:value.asset.format});
    ref.jobId=value.job.id;files.delete(r.id+':'+kind);if(root.DKRFiles)root.DKRFiles.clear();up.resetSelection();active=null;progress=null;
    await save();
  }
  async function process(operationMode,onlyId) {
    ensure();if(running)throw new Error('The queue is already running.');
    mode=operationMode;
    if(mode==='publish'&&!reviewed)throw new Error('Review the selected entries before publishing them.');
    if(root.DKRUploads.state.running)throw new Error('Pause the upload in the listing editor before starting the queue.');
    const current=root.DKRUploads.state.job;
    if(current&&['open','completing','cancel_pending'].includes(current.state)&&!batch.rows.some(r=>['image','download'].some(k=>r[k]&&r[k].jobId===current.id)))throw new Error('Finish or cancel the separate listing editor’s upload before starting this queue.');
    const epoch=generation;running=true;paused=false;notify();
    try {
      await save();assert(epoch);
      for(const r of batch.rows.filter(r=>(onlyId?r.id===onlyId:r.selected)&&!r.publishedItemId)){
        if(paused)break;errors.delete(r.id);
        try {
          if(mode==='upload')for(const kind of ['download','image']){if(paused)break;if(pendingFile(r,kind))await upload(r,kind,epoch);}
          if(paused)break;
          if(mode==='publish'&&missing(r).length)throw new Error('Complete: '+missing(r).join(', ')+'.');
          if(mode==='save'||mode==='publish'){
            const result=await request('/v1/workflow/entry',{operationId:r.id,draft:r.draft,expectedRevision:r.draft.revision,publish:mode==='publish'},epoch);
            assert(epoch);if(!result.draft||!result.draft.id)throw new Error('The saved entry response is invalid. Retry to reconcile it.');
            Object.assign(r.draft,{id:result.draft.id,revision:result.draft.revision});if(result.item)r.publishedItemId=result.item.id;
            if(root.DKRFiles)root.DKRFiles.clear();await save();
          }
        } catch(e) {assert(epoch);errors.set(r.id,e.message);}
        active=null;progress=null;notify();
      }
      await save();
    }finally{if(epoch===generation){running=false;active=null;progress=null;notify();}}
  }
  root.DKRBatch=Object.freeze({ensure,clear,newBatch,listBatches,load,save,addFiles,addHosted,update,updateDefaults,applyDefaults,select,setTitle,assignImage,attachFile,useImage,clearFile,remove,review,pause,process,
    get running(){return running;},get state(){return state();},onChange(fn){listeners.add(fn);return()=>listeners.delete(fn);}});
  authority=key();
  if(root.DKRAuth&&root.DKRAuth.onChange)root.DKRAuth.onChange(()=>{if(authority!==key())clear();});
  if(root.DKRUploads)root.DKRUploads.onChange(value=>{
    if(!running||!active||!batch)return;
    progress={status:value.status,offset:value.offset,size:value.size,message:value.message,error:value.error};
    const r=batch.rows.find(r=>r.id===active.rowId),ref=r&&r[active.kind];
    if(ref&&value.job&&value.kind===active.kind&&value.job.id!==ref.jobId){ref.jobId=value.job.id;schedule();}
    notify();
  });
  root.addEventListener('beforeunload',event=>{if(running||batch&&JSON.stringify(content())!==saved){event.preventDefault();event.returnValue='';}});
}(window));
