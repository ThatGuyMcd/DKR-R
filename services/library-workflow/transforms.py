"""Guarded transformations of the accepted portal baseline; no launcher edits."""

from pathlib import Path

AUTH = Path(__file__).resolve().parent


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Website anchor changed: " + old[:110])
    return text.replace(old, new)


def worker(text):
    text = once(
        text,
        "  return json({ files, inventoryRevision: HOSTED_FILES_REVISION, note: HOSTED_FILES_NOTE });",
        "  return json({ files: await __dkrLibrary.workflowUsage(env, files), inventoryRevision: HOSTED_FILES_REVISION, note: HOSTED_FILES_NOTE });",
    )
    text = once(
        text,
        "async function handleLibrary(request, env, ctx) {",
        (AUTH / "backend.inl").read_text(encoding="utf-8") + "\nasync function handleLibrary(request, env, ctx) {\n"
        "  const workflow = await handleWorkflow(request, env); if (workflow) return workflow;",
    )
    text = once(
        text,
        "return Object.freeze({handleLibrary,cleanupLibrary});",
        "return Object.freeze({handleLibrary,cleanupLibrary,workflowUsage});",
    )
    text = once(
        text,
        "  if (name === 'catalogue.save') {",
        "  if (name === 'catalogue.save' && input.operationId && !input.id) return (await workflowSaveDraft(db,env,user,input)).id;\n  if (name === 'catalogue.save') {",
    )
    text = once(
        text,
        "    launcherRevision: '20261018-beta8',",
        "    launcherRevision: '20261018-beta8',\n    workflowRevision: '20261023-2',",
    )
    text = once(
        text, "const limits = { 'request.submit':", "const limits = { 'workflow.entry': [60, 240], 'request.submit':"
    )
    return text


def hosted(text):
    text = once(
        text,
        "        url: url.href, source: 'ftp' });",
        "        url: url.href, source: 'ftp', inLibrary: typeof record.inLibrary === 'boolean' ? record.inLibrary : null });",
    )
    text = once(
        text,
        "return (!filter.kind || record.kind === filter.kind) && (!filter.category || record.category === filter.category);",
        "return (!filter.kind || record.kind === filter.kind) && (!filter.category || record.category === filter.category) && (filter.usage !== 'unused' || record.inLibrary === false);",
    )
    text = once(
        text,
        "    get loaded() { syncAccount(); return loaded; },",
        "    get usageReady() { syncAccount(); return loaded && files.every(file => typeof file.inLibrary === 'boolean'); },\n    get loaded() { syncAccount(); return loaded; },",
    )
    return text


def uploads(text):
    helper = """  function resetSelection(options) {
    syncAccount();
    if(active||mode==='checking'||mode==='cancelling')throw failure('Pause the current transfer before clearing its selection.','upload_busy');
    if(job&&['open','completing','cancel_pending'].includes(job.state)&&!(options&&options.detach))throw failure('Finish or cancel this transfer first.','upload_busy');
    selection++;selectedFile=null;selectedFingerprint='';clientId='';verifiedPrefix=0;job=null;kind='';category='';
    pauseRequested=false;stopping=false;mode='idle';message='';lastError='';notify();
  }
  async function loadJob(id) {
    syncAccount();const epoch=generation,expected=account;
    if(!/^up_[a-f0-9]{32}$/.test(id))throw failure('Invalid saved upload.','invalid_response');
    const data=await retryTransfer(()=>request('/v1/uploads/'+id,undefined,epoch,expected),epoch,expected);
    return validJob(data.upload);
  }
"""
    text = once(
        text, "  async function selectFile(file, options) {", helper + "  async function selectFile(file, options) {"
    )
    text = once(
        text,
        "    syncAccount();if(active)throw failure('Pause the current upload before choosing a file.','upload_busy');",
        "    syncAccount();if(active)throw failure('Pause the current upload before choosing a file.','upload_busy');\n"
        "    if(job&&['completed','cancelled','expired'].includes(job.state)&&!(options&&options.resume))resetSelection();",
    )
    text = once(
        text,
        "      }else{job=null;clientId=randomId();verifiedPrefix=0;}",
        "      }else{job=null;clientId=options&&/^[a-f0-9]{32}$/.test(options.clientId)?options.clientId:randomId();verifiedPrefix=0;}",
    )
    text = once(
        text,
        "root.DKRUploads=Object.freeze({loadSettings,loadJobs,selectFile,selectJob,start,pause,cancel,checkConnection,",
        "root.DKRUploads=Object.freeze({loadSettings,loadJobs,loadJob,resetSelection,selectFile,selectJob,start,pause,cancel,checkConnection,",
    )
    start = text.index("  function canRetry(e) {")
    end = text.index("  async function delay(", start)
    text = (
        text[:start]
        + """  function canRetry(e) {
    if(!e||/configuration|ftp_access|ftp_login_failed|ftp_tls_failed|ftp_session_reuse|ftp_resume_unavailable/.test(e.code||''))return false;
    return ['network_error','request_timeout','upload_busy','upload_transfer_failed','upload_ftp_timeout','upload_ftp_close_pending','upload_lease_lost'].includes(e.code) || e.status===429 || e.status>=500;
  }
  async function retryTransfer(work,epoch,expected) {
    for(let attempt=0;attempt<=5;attempt++){
      try{return await work();}
      catch(e){
        if(!canRetry(e)||pauseRequested||stopping)throw e;
        if(attempt===5)throw failure('Upload failed after 5 automatic retries. '+e.message,'upload_retries_exhausted',e.status);
        message='Upload interrupted. Automatic retry '+(attempt+1)+' of 5…';notify();
        await delay(1000*Math.pow(2,attempt),epoch,expected);
      }
    }
  }
"""
        + text[end:]
    )
    start = text.index("  async function sendChunk(")
    end = text.index("  async function transfer(", start)
    text = (
        text[:start]
        + """  async function sendChunk(bytes,offset,epoch,expected) {
    for(let attempt=0;attempt<=5;attempt++){
      if(pauseRequested||stopping)return false;
      try{
        if(attempt){
          message='Checking saved progress before automatic retry '+attempt+' of 5…';notify();
          await reconcile(epoch,expected);
          if(job.offset===offset+bytes.size)return true;
          if(job.offset!==offset||job.state!=='open')throw failure('The saved upload position changed. Resume to recheck it.','upload_offset_mismatch',409);
        }
        const data=await request('/v1/uploads/'+job.id+'/chunk?offset='+offset,bytes,epoch,expected,true);
        const checked=validJob(data.upload);
        if(checked.id!==job.id||checked.offset!==offset+bytes.size)throw failure('The service reported an unexpected upload position. Resume to recheck the file.','upload_offset_mismatch',409);
        useJob(checked);verifiedPrefix=job.offset;return true;
      }catch(e){
        if(!canRetry(e)||pauseRequested||stopping)throw e;
        if(attempt===5)throw failure('Upload failed after 5 automatic retries. '+e.message,'upload_retries_exhausted',e.status);
        message='Upload interrupted. Automatic retry '+(attempt+1)+' of 5…';notify();
        await delay(1000*Math.pow(2,attempt),epoch,expected);
      }
    }
    return false;
  }
"""
        + text[end:]
    )
    text = once(
        text,
        "const data=await request('/v1/uploads',{fileName:selectedFile.name,size:selectedFile.size,kind,category,fingerprint:selectedFingerprint,clientId},epoch,expected);",
        "const data=await retryTransfer(()=>request('/v1/uploads',{fileName:selectedFile.name,size:selectedFile.size,kind,category,fingerprint:selectedFingerprint,clientId},epoch,expected),epoch,expected);",
    )
    text = once(
        text,
        "const data=await request('/v1/uploads/'+job.id+'/complete',{},epoch,expected);useJob(data.upload);",
        "const data=await retryTransfer(()=>request('/v1/uploads/'+job.id+'/complete',{},epoch,expected),epoch,expected);useJob(data.upload);",
    )
    text = once(
        text,
        "await request('/v1/launcher/uploads/'+job.id+'/integrity',{sha256:full.hex(),bytes:end},epoch,expected);",
        "const checksum=full.hex();await retryTransfer(()=>request('/v1/launcher/uploads/'+job.id+'/integrity',{sha256:checksum,bytes:end},epoch,expected),epoch,expected);",
    )
    text = once(
        text,
        "const saved=await request('/v1/uploads/'+job.id,undefined,epoch,expected);",
        "const saved=await retryTransfer(()=>request('/v1/uploads/'+job.id,undefined,epoch,expected),epoch,expected);",
    )
    text = once(
        text,
        "}else{message='Checking saved progress…';notify();await reconcile(epoch,expected);}",
        "}else{message='Checking saved progress…';notify();await retryTransfer(()=>reconcile(epoch,expected),epoch,expected);}",
    )
    text = once(
        text,
        "if(job.fingerprint!==selectedFingerprint||job.size!==selectedFile.size)throw failure('The new upload does not match the selected file.','invalid_response');",
        "if(job.fingerprint!==selectedFingerprint||job.size!==selectedFile.size)throw failure('The new upload does not match the selected file.','invalid_response');\n"
        "        if(job.offset>0)await retryTransfer(()=>reconcile(epoch,expected),epoch,expected);",
    )
    return text


def api(text):
    text = once(
        text,
        "      return mutate('catalogue.save', { id: draft.id, draft: draft,",
        "      return mutate('catalogue.save', { id: draft.id, operationId: draft.operationId, draft: draft,",
    )
    return text


def app(text):
    text = once(
        text,
        "{id:'legacy',label:'Legacy Mods / Romhacks',short:'Legacy / Romhacks',icon:'archive',description:'Explore classic packs and ROM patch releases.'}",
        "{id:'legacy',label:'Vehicles',short:'Vehicles',icon:'car',description:'Find custom vehicles and new ways to race.'}",
    )
    text = once(text, "c.id==='legacy'?'Legacy Mods':c.short", "c.short")
    text = once(text, "legacy:'Romhacks & collections'", "legacy:'Custom vehicles & new rides'")
    text = once(
        text,
        "legacy:'Read the required source ROM revision and DKR-R compatibility notes before applying a patch or importing a legacy package.'",
        "legacy:'Read the vehicle creator’s compatibility notes, required DKR-R version and installation instructions before importing the package.'",
    )
    text = once(
        text,
        "filePickerKind:'',filePickerSearch:'',filePickerError:'',hostedSelections:",
        "filePickerKind:'',filePickerSearch:'',filePickerUsage:'all',filePickerError:'',hostedSelections:",
    )
    text = once(
        text,
        "  let s = blankUI();",
        "  let s = blankUI();\n  let keepNext = {}, nextDefaults = {};\n  const Batch = window.DKRBatch;",
    )
    text = once(
        text, "  function blankUI() {", (AUTH / "ui.inl").read_text(encoding="utf-8") + "\n  function blankUI() {"
    )
    text = once(
        text,
        "function clearPrivateState() { privateEpoch++;",
        "function clearPrivateState() { if(Batch)Batch.clear();keepNext={};nextDefaults={};privateEpoch++;",
    )
    text = once(
        text,
        "    const cat=source.category||'tracks';",
        "    const cat=source.category||nextDefaults.category||'tracks';",
    )
    text = once(
        text,
        "    return go('editor',{},false);",
        "    if(!saved&&!item&&!request)Object.assign(s.editorDraft,nextDefaults);\n    if(!s.editorDraft.id&&!s.editorDraft.operationId)s.editorDraft.operationId=workflowId();\n    return go('editor',{},false);",
    )
    text = once(
        text,
        "    uploadTarget=null;s.uploadScope=++uploadScopeSerial;",
        "    uploadTarget=null;uploadAppliedId='';\n    if(Uploads&&!Uploads.state.running&&(!Uploads.state.job||['completed','cancelled','expired'].includes(Uploads.state.job.state)))Uploads.resetSelection();\n    s.uploadScope=++uploadScopeSerial;",
    )
    text = once(
        text,
        "    const rows=all.filter(file=>!query||file.fileName.toLocaleLowerCase().includes(query));",
        "    if(s.filePickerUsage==='unused'&&!Files.usageReady)return banner('Deploy the website API update to enable the Not in Mod Library filter.','error');\n"
        "    const rows=all.filter(file=>(s.filePickerUsage!=='unused'||file.inLibrary===false)&&(!query||file.fileName.toLocaleLowerCase().includes(query)));",
    )
    text = once(
        text,
        "if(!rows.length)return '<p class=\"dkr-small dkr-muted\">'+esc(all.length?'No filenames match your search.':",
        "if(!rows.length)return '<p class=\"dkr-small dkr-muted\">'+esc(s.filePickerUsage==='unused'&&all.length?'No unlisted files match. Files used by saved drafts, archived entries and older releases are included in the library.':all.length?'No filenames match your search.':",
    )
    text = once(
        text,
        'else body=\'<label class="dkr-field"><span>Find a filename</span>',
        'else body=workflowFileFilters()+\'<label class="dkr-field"><span>Find a filename</span>',
    )
    text = once(
        text,
        "+'</p></section>'+uploadPanel(kind,d);",
        "+'</p>'+workflowSelectionControls(kind,d)+'</section>'+uploadPanel(kind,d);",
    )
    text = once(
        text,
        "await Uploads.selectFile(file,{kind,category:draft.category});",
        "await Uploads.selectFile(file,{kind,category:draft.category,resume:!!(Uploads.state.job&&Uploads.state.job.state==='completed'&&Uploads.state.status!=='completed')});",
    )
    text = once(
        text,
        "    if(state.status==='completed'&&state.job",
        "    if(Batch&&Batch.running)return;\n    if(state.status==='completed'&&state.job",
    )
    text = once(text, "const pages={store:storePage,", "const pages={batch:batchPage,store:storePage,")
    text = text.replace(
        "'publication-preview','admin'].includes(s.view)", "'publication-preview','admin','batch'].includes(s.view)"
    )
    text = once(
        text,
        "    if(pendingWrite)return;\n    try {",
        "    if(pendingWrite)return;\n    try {\n      if(await workflowAction(action,id,target))return;",
    )
    text = once(text, "if(action==='publish'){", "if(action==='publish'||action==='publish-another'){")
    text = once(
        text,
        "            if(!isAdmin())throw new Error('Administrator access is required.');",
        "            if(!isStaff())throw new Error('Moderator access is required.');",
    )
    text = once(
        text,
        "          s.pendingPublication=null;s.editorDraft=null;await go('detail',{itemId:item.id,detailTab:'overview'});if(epoch!==privateEpoch)return;",
        "          rememberNext(p.draft);s.pendingPublication=null;s.editorDraft=null;if(Files)Files.clear();resetFilePicker();\n"
        "          if(action==='publish-another')await prepareEditor('admin',null);else await go('detail',{itemId:item.id,detailTab:'overview'});if(epoch!==privateEpoch)return;",
    )
    text = once(
        text,
        "button('publish',icon('check')+'Publish to library','gold')",
        "button('publish',icon('check')+'Publish and view','gold')+button('publish-another',icon('plus')+'Publish and add another','quiet')",
    )
    text = once(
        text,
        "const draftId=await D.saveCatalogueDraft(p.draft);if(epoch!==privateEpoch)return;",
        "const draftId=await D.saveCatalogueDraft(p.draft);if(epoch!==privateEpoch)return;",
    )
    text = once(
        text,
        "await D.saveCatalogueDraft(draft);if(epoch!==privateEpoch)return;await go('admin'",
        "const savedId=await D.saveCatalogueDraft(draft);if(epoch!==privateEpoch)return;draft.id=savedId;const savedDraft=list(D.catalogueDrafts).find(row=>row.id===savedId);if(savedDraft)draft.revision=savedDraft.revision;if(Files)Files.clear();await go('admin'",
    )
    text = once(
        text,
        "button('add-listing',icon('plus')+'Add to library','gold')+'</div>'",
        "button('add-listing',icon('plus')+'Add to library','gold')+button('open-batch',icon('list-plus')+'Add multiple entries','quiet')+'</div>'",
    )
    text = once(
        text,
        '<h2 class="dkr-subtitle">Before it goes live</h2>',
        "<h2 class=\"dkr-subtitle\">Before it goes live</h2>'+workflowNextControls()+'",
    )
    text = once(
        text,
        "submit(icon('eye')+'Preview publication','preview','gold')+(!r?",
        "submit(icon('eye')+'Preview publication','preview','gold')+button('clear-listing','Clear form','quiet')+(!r?",
    )
    text = once(
        text,
        "const el=event.target;if(pendingWrite)return;\n    if(el.id==='dkr-remember-sign-in')",
        "const el=event.target;if(pendingWrite)return;\n    if(el.type==='file')return;\n    if(workflowInput(el))return;\n    if(el.id==='dkr-remember-sign-in')",
    )
    text = once(
        text,
        "const el=event.target;if(pendingWrite)return;\n    if(el.dataset&&el.dataset.uploadFile)",
        "const el=event.target;if(pendingWrite)return;\n    if(workflowInput(el))return;\n    if(el.dataset&&el.dataset.uploadFile)",
    )
    text = once(
        text,
        "      if(Uploads)Uploads.onChange(uploadsChanged);",
        "      if(Uploads)Uploads.onChange(uploadsChanged);\n      if(Batch)Batch.onChange(batchChanged);",
    )
    return text


def transform(name, text):
    return {"app.js": app, "uploads-client.js": uploads, "api-store.js": api}[name](text)
