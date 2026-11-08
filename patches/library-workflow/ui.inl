  function workflowId() { return Array.from(window.crypto.getRandomValues(new Uint8Array(16)),n=>n.toString(16).padStart(2,'0')).join(''); }
  function workflowFileFilters() {
    return '<div class="dkr-workflow-filters" role="group" aria-label="Files in the library">'+[['all','All'],['unused','Not in Mod Library']].map(([id,label])=>button('workflow-file-filter',label,'quiet small',id,'aria-pressed="'+(s.filePickerUsage===id)+'"')).join('')+'</div>';
  }
  function workflowSelectionControls(kind,d) {
    const selected=kind==='image'&&!['pattern','pattern-two',''].includes(d.image)?d.image:kind==='download'?d.fileUrl:'';
    return selected?'<div class="dkr-row dkr-spaced">'+button('workflow-replace-file','Replace '+(kind==='image'?'image':'package'),'quiet small',kind)+button('workflow-remove-file','Remove selection','quiet small',kind)+'</div>':'';
  }
  function workflowNextControls() {
    return '<fieldset class="dkr-fieldset dkr-spaced"><legend>Keep for the next entry</legend>'+[['category','Category'],['author','Creator credit'],['version','Version'],['requirements','Requirements'],['install','Installation instructions']].map(([key,label])=>'<label class="dkr-check"><input type="checkbox" data-workflow-keep="'+key+'"'+(keepNext[key]?' checked':'')+'><span>'+label+'</span></label>').join('')+'</fieldset>';
  }
  function rememberNext(draft) {nextDefaults={};for(const key of Object.keys(keepNext))if(keepNext[key])nextDefaults[key]=draft[key];}
  async function workflowRemove(kind) {
    const draft=captureListingForm();if(!draft)return;
    if(Uploads&&uploadMatchesContext(uploadTarget,false)&&Uploads.state.kind===kind){
      uploadTarget={...uploadTarget,cancelledIntent:true};
      if(Uploads.state.running||Uploads.state.job&&['open','completing','cancel_pending'].includes(Uploads.state.job.state)){
        await Uploads.cancel();if(Uploads.state.status==='cancel_pending')throw new Error('Partial-file cleanup is pending. Retry Remove selection when the connection is available.');
      }
      Uploads.resetSelection();uploadTarget=null;uploadAppliedId='';
    }
    if(kind==='image'){draft.image='pattern';delete draft.imageChoice;delete draft.imageUrl;}
    else{draft.fileUrl='';draft.fileName='';draft.format='';}
    s.hostedSelections[kind]=null;s.filePickerKind='';s.uploadOpenKind='';s.uploadError='';render();flash((kind==='image'?'Image':'Package')+' selection removed.');
  }
  function batchField(label,name,value,attrs,large) {
    return '<label class="dkr-field"><span>'+label+'</span>'+(large?'<textarea':'<input type="text"')+' name="'+esc(name)+'" data-batch-field'+(attrs?' '+attrs:'')+(large?'>'+esc(value)+'</textarea>':' value="'+esc(value)+'">')+'</label>';
  }
  function batchRow(r,state) {
    const d=r.draft,locked=r.publishedItemId||state.running&&state.mode!=='upload'?' disabled':'',pendingDownload=r.download&&!d.fileUrl,pendingImage=r.image&&d.image==='pattern';
    const cover=['pattern','pattern-two'].includes(d.image)?d.image:d.image||'pattern';
    const active=state.active&&state.active.rowId===r.id, p=active&&state.progress;
    const packageText=r.download?r.download.name:d.fileName||'Choose a package';
    let fileControls='<div class="dkr-workflow-files"><section><h3 class="dkr-subtitle">Package</h3><p class="dkr-safe-wrap">'+esc(packageText)+'</p>';
    if(!r.publishedItemId)fileControls+='<label class="dkr-button quiet small">'+(pendingDownload?'Reselect original package':'Choose / replace package')+'<input type="file" class="dkr-workflow-file-input" data-batch-file="download" accept=".zip,.rtz,.dkrmap,.xdelta,.nrm,.dkrcode,.bps,.ips,.7z"'+(state.running?' disabled':'')+'></label> '+button('batch-clear-file','Remove package','quiet small',r.id,'data-kind="download"'+(state.running?' disabled':''));
    fileControls+='</section><section><h3 class="dkr-subtitle">Cover image</h3>'+image(cover,d.title,'dkr-workflow-cover')+'<p class="dkr-small dkr-safe-wrap">'+esc(r.image?r.image.name:['pattern','pattern-two'].includes(cover)?'Launcher pattern':'Hosted image')+'</p>';
    if(!r.publishedItemId){
      fileControls+='<div class="dkr-row"><label class="dkr-button quiet small">'+(pendingImage?'Reselect original image':'Choose / replace image')+'<input type="file" class="dkr-workflow-file-input" data-batch-file="image" accept=".png,.jpg,.jpeg,.webp"'+(state.running?' disabled':'')+'></label>'+button('batch-pick-cover','Choose from FTP','quiet small',r.id,(state.running?'disabled':''))+button('batch-clear-file','Remove image','quiet small',r.id,'data-kind="image"'+(state.running?' disabled':''))+'</div>';
      if(state.images.length)fileControls+='<label class="dkr-field dkr-spaced"><span>Match an image from this batch</span><select data-batch-local-image'+(state.running?' disabled':'')+'>'+options([['','Choose an image…'],...state.images.map(f=>[f.id,f.name])],'')+'</select></label>';
    }
    fileControls+='</section></div>';
    return '<article class="dkr-panel dkr-workflow-entry" data-batch-row="'+esc(r.id)+'"><header class="dkr-row dkr-between"><label class="dkr-check"><input type="checkbox" data-batch-selected'+(r.selected?' checked':'')+(state.running||r.publishedItemId?' disabled':'')+'><span>'+esc(d.title||packageText||'Untitled entry')+'</span></label><span class="dkr-status" data-batch-status>'+esc(r.status)+'</span></header>'+
      '<div data-batch-progress>'+(p?batchProgress(p):'')+'</div>'+(r.error?banner(esc(r.error),'error'):'')+
      (r.publishedItemId?'<p class="dkr-spaced">Published successfully. '+button('detail','View listing','quiet small',r.publishedItemId)+'</p>':'')+
      '<details'+(!r.publishedItemId?' open':'')+'><summary>Listing details and files</summary><div class="dkr-stack dkr-spaced">'+fileControls+
      '<div class="dkr-pair">'+batchField('Public title','title',d.title,'maxlength="100"'+locked)+batchField('Creator credit','author',d.author,'maxlength="100"'+locked)+'</div>'+
      '<div class="dkr-pair">'+'<label class="dkr-field"><span>Category</span><select name="category" data-batch-field'+(r.download||d.fileUrl||r.image?' disabled':locked)+'>'+categoryOptions(d.category)+'</select></label>'+batchField('Release version','version',d.version,'maxlength="80"'+locked)+'</div>'+
      batchField('Description','summary',d.summary,'maxlength="4000"'+locked,true)+
      '<div class="dkr-pair">'+batchField('Public HTTPS package URL','fileUrl',d.fileUrl,'maxlength="1500"'+(pendingDownload?' disabled':locked))+batchField('Package format','format',d.format,'maxlength="40"'+locked)+'</div>'+
      batchField('Public HTTPS image URL (optional)','image',['pattern','pattern-two'].includes(d.image)?'':d.image,'maxlength="1500"'+(pendingImage?' disabled':locked))+
      batchField('Requirements','requirements',d.requirements,'maxlength="2000"'+locked,true)+batchField('Installation instructions','install',d.install,'maxlength="6000"'+locked,true)+
      batchField('Tags, separated by commas','tags',list(d.tags).join(', '),'maxlength="502"'+locked)+
      '<label class="dkr-check"><input type="checkbox" name="featured" data-batch-field'+(d.featured?' checked':'')+locked+'><span>Feature this listing</span></label></div></details>'+
      '<div class="dkr-row dkr-spaced">'+(!r.publishedItemId?button('batch-upload-row','Upload / retry this entry','quiet small',r.id,state.running?'disabled':''):'')+button('batch-remove','Remove from batch','quiet small',r.id,state.running?'disabled':'')+'</div></article>';
  }
  function batchProgress(p) {return (p.size?'<progress max="'+p.size+'" value="'+p.offset+'" aria-label="Confirmed upload progress"></progress><span class="dkr-small"> '+esc(byteText(p.offset)||'0 B')+' / '+esc(byteText(p.size))+'</span>':'')+'<p class="dkr-small dkr-muted" role="status">'+esc(p.error||p.message||'Preparing transfer…')+'</p>';}
  function batchPicker(state) {
    if(!s.batchPicker)return '';
    const cover=s.batchPicker!=='download',target=cover&&state.batch.rows.find(r=>r.id===s.batchPicker);
    if(cover&&!target)return '';
    const cat=target?target.draft.category:(s.batchCategory||state.batch.defaults.category||'tracks');
    const kind=cover?'image':'download';
    let body='';
    if(!Files||Files.loading)body=progress('Loading FTP files');
    else if(Files.error)body=banner(esc(Files.error),'error')+button('batch-refresh-files','Retry','quiet');
    else if(!Files.loaded)body=button('batch-refresh-files','Load FTP files','quiet');
    else if(s.batchUsage==='unused'&&!Files.usageReady)body=banner('The website API update is required for this filter.','error');
    else{
      const query=(s.batchSearch||'').trim().toLocaleLowerCase();
      const matches=Files.list({kind,category:cat,usage:s.batchUsage||'unused'}).filter(f=>!query||f.fileName.toLocaleLowerCase().includes(query));
      const rows=matches.slice(0,s.batchFileLimit||100);
      body='<p class="dkr-small">'+matches.length+' matching files</p><div class="dkr-workflow-inventory">'+(rows.length?rows.map(f=>cover?'<div class="dkr-file-option">'+image(f.url,'','dkr-file-thumb')+'<span>'+esc(f.fileName)+'</span>'+button('batch-use-cover','Use image','quiet small',f.id,'data-row="'+esc(target.id)+'"')+'</div>':'<label class="dkr-check"><input type="checkbox" data-batch-hosted="'+esc(f.id)+'"'+(s.batchHosted&&s.batchHosted.has(f.id)?' checked':'')+'><span>'+esc(f.fileName)+' <small>'+esc(byteText(f.size))+'</small></span></label>').join(''):'<p>No matching files. Files used by saved drafts, archived entries and older releases count as already in the library.</p>')+'</div>'+(matches.length>rows.length?button('batch-more-files','Show more files','quiet small'):'')+(!cover?'<div class="dkr-spaced">'+button('batch-add-hosted','Add selected packages to batch','gold')+'</div>':'');
    }
    return '<section class="dkr-panel dkr-stack dkr-spaced"><div class="dkr-row dkr-between"><h2 class="dkr-title">'+(cover?'Choose a cover from FTP':'Choose packages from FTP')+'</h2>'+button('batch-close-picker','Close picker','quiet small')+'</div>'+
      '<div class="dkr-workflow-filters" role="group" aria-label="FTP file usage">'+[['all','All'],['unused','Not in Mod Library']].map(([id,label])=>button('batch-file-filter',label,'quiet small',id,'aria-pressed="'+((s.batchUsage||'unused')===id)+'"')).join('')+'</div>'+
      (!cover?'<label class="dkr-field"><span>Category</span><select data-batch-category>'+categoryOptions(cat)+'</select></label>':'')+
      '<label class="dkr-field"><span>Find a filename</span><input type="search" data-batch-search value="'+esc(s.batchSearch||'')+'"></label>'+body+'</section>';
  }
  function batchPage() {
    if(!isStaff())return deniedPage();if(!Batch)return banner('The bulk-entry script could not load. Reload this page.','error');
    Batch.ensure();const state=Batch.state,b=state.batch,d=b.defaults;
    const disabled=state.running?' disabled':'';
    const ready=b.rows.filter(r=>r.selected&&r.status==='Ready to publish').length;
    let content=pagehead('Add multiple entries','Upload new files or choose packages already on FTP. Each package becomes a separate library listing.','admin')+
      '<div class="dkr-row dkr-between dkr-spaced">'+button('batch-new','New batch','quiet',null,disabled)+button('batch-load-list','Open saved batch','quiet',null,disabled)+'</div>'+
      (s.batchError?banner(esc(s.batchError),'error'):'')+(state.saveError?banner('Batch recovery could not be saved: '+esc(state.saveError)+'. Keep this page open and retry Save batch.','error'):'')+
      (s.batchSavedList?'<section class="dkr-panel dkr-spaced"><h2 class="dkr-subtitle">Saved batches</h2>'+s.batchSavedList.map(x=>'<div class="dkr-listitem"><span>'+esc(x.title)+' · '+dateText(x.updatedAt)+'</span>'+button('batch-load','Open','quiet small',x.id)+'</div>').join('')+'</section>':'')+
      '<section class="dkr-panel dkr-stack dkr-spaced"><label class="dkr-field"><span>Batch name</span><input data-batch-name value="'+esc(b.title)+'" maxlength="100"></label><p class="dkr-small dkr-muted" data-batch-save-status>'+(state.dirty?'Unsaved changes':'Saved for this account')+'</p>'+
      '<div class="dkr-workflow-drop" data-batch-drop tabindex="0" aria-label="Drop mod packages and cover images here"><strong>Drop packages and cover images here</strong><p>Up to 50 entries per batch. PNG, JPEG and WebP covers. Packages up to 2 GiB.</p><label class="dkr-button gold">Choose files<input type="file" class="dkr-workflow-file-input" data-batch-files multiple accept=".zip,.rtz,.dkrmap,.xdelta,.nrm,.dkrcode,.bps,.ips,.7z,.png,.jpg,.jpeg,.webp"'+disabled+'></label> '+button('batch-open-packages','Choose existing FTP packages','quiet',null,disabled)+'</div>'+
      '<details><summary>Shared details for new and selected entries</summary><div class="dkr-stack dkr-spaced"><div class="dkr-pair"><label class="dkr-field"><span>Category for new entries</span><select data-batch-default="category">'+categoryOptions(d.category)+'</select></label>'+batchDefaultField('Creator credit','author',d.author,100)+'</div>'+batchDefaultField('Release version','version',d.version,80)+batchDefaultField('Description','summary',d.summary,4000,true)+batchDefaultField('Requirements','requirements',d.requirements,2000,true)+batchDefaultField('Installation instructions','install',d.install,6000,true)+'<fieldset class="dkr-fieldset"><legend>Apply these shared details</legend>'+[['author','Creator credit'],['version','Version'],['summary','Description'],['requirements','Requirements'],['install','Installation instructions']].map(([id,label])=>'<label class="dkr-check"><input type="checkbox" data-batch-apply="'+id+'"><span>'+label+'</span></label>').join('')+'</fieldset>'+button('batch-apply-defaults','Apply to selected entries','quiet')+'</div></details></section>'+
      batchPicker(state)+'<div class="dkr-workflow-toolbar dkr-panel dkr-spaced"><strong>'+b.rows.length+' entries · '+ready+' ready to publish</strong><div class="dkr-row">'+button('batch-upload','Upload selected files','gold',null,disabled)+button('batch-save-drafts','Save selected as drafts','quiet',null,disabled)+button('batch-save','Save batch','quiet',null,disabled)+(state.running?button('batch-pause','Pause queue','quiet'):button('batch-review','Review selected entries','quiet'))+'</div></div>';
    if(state.reviewed&&b.rows.some(r=>r.selected&&!r.publishedItemId)){const selected=b.rows.filter(r=>r.selected&&!r.publishedItemId);content+='<section class="dkr-panel dkr-spaced"><h2 class="dkr-title">Review publication</h2><p>Check the artwork, credits, version and download for each entry. Entries missing required details will remain in the batch.</p><div class="dkr-workflow-review">'+selected.map(r=>'<article>'+image(r.draft.image,r.draft.title,'dkr-workflow-cover')+'<h3>'+esc(r.draft.title||'Untitled')+'</h3><p>'+esc(category(r.draft.category).label)+' · '+esc(r.draft.author)+' · '+esc(r.draft.version)+'</p><p class="dkr-safe-wrap">'+esc(r.draft.summary)+'</p>'+sourceLink(r.draft.fileUrl)+'<p>'+esc(r.missing.length?'Missing: '+r.missing.join(', '):'Ready to publish')+'</p></article>').join('')+'</div>'+button('batch-publish','Publish ready entries','gold',null,disabled)+'</section>';}
    return content+'<div class="dkr-stack dkr-spaced">'+(b.rows.length?b.rows.map(r=>batchRow(r,state)).join(''):empty('Build your batch','Choose packages from your computer or the FTP file picker. Matching cover filenames are suggested automatically.'))+'</div>';
  }
  function batchDefaultField(label,name,value,max,large) {return '<label class="dkr-field"><span>'+label+'</span>'+(large?'<textarea':'<input type="text"')+' data-batch-default="'+name+'" maxlength="'+max+'"'+(large?'>'+esc(value)+'</textarea>':' value="'+esc(value)+'">')+'</label>';}
  function batchChanged() {
    if(s.view!=='batch')return;
    const focused=document.activeElement;
    if(Batch.state.running||focused&&root.contains(focused)&&focused.matches('input,textarea,select')){
      const state=Batch.state;
      for(const r of state.batch?state.batch.rows:[]){const node=$('[data-batch-row="'+r.id+'"]');if(!node)continue;node.querySelector('[data-batch-status]').textContent=r.status;node.querySelector('[data-batch-progress]').innerHTML=state.active&&state.active.rowId===r.id&&state.progress?batchProgress(state.progress):'';}
      const statusNode=$('[data-batch-save-status]');if(statusNode)statusNode.textContent=state.saveError?'Recovery save failed: '+state.saveError:state.dirty?'Saving changes…':'Saved for this account';
    }else render();
  }
  function workflowInput(el) {
    if(el.dataset.workflowKeep){keepNext[el.dataset.workflowKeep]=el.checked;return true;}
    if(el.id==='dkr-workflow-file-filter'){s.filePickerUsage=el.value;render();return true;}
    if(s.view!=='batch'||!Batch)return false;
    if(el.dataset.batchFiles!==undefined){if(el.files&&el.files.length){try{Batch.addFiles(el.files);}catch(e){s.batchError=errorMessage(e);render();}}return true;}
    const node=el.closest('[data-batch-row]'),id=node&&node.dataset.batchRow;
    try{
      if(el.dataset.batchName!==undefined){Batch.setTitle(el.value);return true;}
      if(el.dataset.batchDefault){Batch.updateDefaults({[el.dataset.batchDefault]:el.value});return true;}
      if(el.dataset.batchApply!==undefined)return true;
      if(el.dataset.batchField!==undefined&&id){Batch.update(id,{[el.name]:el.name==='tags'?el.value.split(',').map(t=>t.trim()).filter(Boolean):el.name==='featured'?el.checked:el.name==='image'&&!el.value?'pattern':el.value});return true;}
      if(el.dataset.batchSelected!==undefined&&id){Batch.select(id,el.checked);return true;}
      if(el.dataset.batchFile&&id){if(el.files&&el.files[0])Batch.attachFile(id,el.dataset.batchFile,el.files[0]);return true;}
      if(el.dataset.batchLocalImage!==undefined&&id){if(el.value)Batch.assignImage(id,el.value);return true;}
      if(el.dataset.batchHosted){if(!s.batchHosted)s.batchHosted=new Set();if(el.checked)s.batchHosted.add(el.dataset.batchHosted);else s.batchHosted.delete(el.dataset.batchHosted);return true;}
      if(el.dataset.batchCategory!==undefined){s.batchCategory=el.value;s.batchHosted=new Set();render();return true;}
      if(el.dataset.batchSearch!==undefined){s.batchSearch=el.value;const start=el.selectionStart;render();const input=$('[data-batch-search]');if(input){input.focus();input.setSelectionRange(start,start);}return true;}
    }catch(e){s.batchError=errorMessage(e);render();return true;}
    return false;
  }
  async function workflowAction(action,id,target) {
    if(Batch&&Batch.running&&['add-listing','edit-listing','open-draft','edit-details','open-upload','start-upload'].includes(action))throw new Error('Pause the bulk-entry queue before opening another uploader.');
    if(action==='workflow-file-filter'){captureListingForm();s.filePickerUsage=id;render();return true;}
    if(action==='workflow-remove-file'){await workflowRemove(id);return true;}
    if(action==='workflow-replace-file'){await openUpload(id);return true;}
    if(action==='clear-listing'){await workflowRemove('image');if(s.view==='editor')await workflowRemove('download');nextDefaults={};await prepareEditor(s.editorMode,s.editorRequestId);return true;}
    if(action!=='open-batch'&&!action.startsWith('batch-'))return false;
    if(!isStaff())throw new Error('Moderator access is required.');if(!Batch)throw new Error('Reload the page to load the bulk-entry tools.');
    s.batchError='';
    try{
      if(action==='open-batch'){captureListingForm();uploadTarget=null;Batch.ensure();await go('batch',{},false);return true;}
      if(action==='batch-new'){await Batch.newBatch();s.batchSavedList=null;s.batchPicker='';render();}
      if(action==='batch-load-list'){s.batchSavedList=await Batch.listBatches();render();}
      if(action==='batch-load'){await Batch.load(id);s.batchSavedList=null;s.batchPicker='';render();}
      if(action==='batch-save'){await Batch.save();render();flash('Batch saved. Original files may need to be reselected after a reload.');}
      if(action==='batch-open-packages'||action==='batch-pick-cover'){s.batchPicker=action==='batch-open-packages'?'download':id;s.batchUsage='unused';s.batchSearch='';s.batchHosted=new Set();s.batchFileLimit=100;const loading=Files.load({force:true});render();await loading;render();}
      if(action==='batch-close-picker'){s.batchPicker='';render();}
      if(action==='batch-file-filter'){s.batchUsage=id;render();}
      if(action==='batch-more-files'){s.batchFileLimit=(s.batchFileLimit||100)+100;render();}
      if(action==='batch-refresh-files'){await Files.load({force:true});render();}
      if(action==='batch-add-hosted'){Batch.addHosted(Files.list({kind:'download',category:s.batchCategory||Batch.state.batch.defaults.category}).filter(f=>s.batchHosted&&s.batchHosted.has(f.id)));s.batchHosted=new Set();s.batchPicker='';render();}
      if(action==='batch-use-cover'){const file=Files.list({kind:'image'}).find(f=>f.id===id);if(!file)throw new Error('Refresh the file picker.');Batch.useImage(target.dataset.row,file);s.batchPicker='';render();}
      if(action==='batch-apply-defaults'){Batch.applyDefaults(Array.from(root.querySelectorAll('[data-batch-apply]:checked'),el=>el.dataset.batchApply));render();}
      if(action==='batch-clear-file'){await Batch.clearFile(id,target.dataset.kind);render();}
      if(action==='batch-remove'){await Batch.remove(id);render();}
      if(action==='batch-review'){Batch.review();render();}
      if(action==='batch-pause'){Batch.pause();render();}
      if(['batch-upload','batch-upload-row','batch-save-drafts','batch-publish'].includes(action)){
        const mode=action.startsWith('batch-upload')?'upload':action==='batch-publish'?'publish':'save';
        const operation=Batch.process(mode,action==='batch-upload-row'?id:undefined);render();await operation;
        if(mode!=='upload')await D.refresh();if(Files)Files.clear();render();
      }
    }catch(e){s.batchError=errorMessage(e);render();}
    return true;
  }
  root.addEventListener('dragover',event=>{if(s.view==='batch'&&event.target.closest('[data-batch-drop]')){event.preventDefault();event.dataTransfer.dropEffect='copy';}});
  root.addEventListener('drop',event=>{if(s.view!=='batch'||!event.target.closest('[data-batch-drop]'))return;event.preventDefault();try{Batch.addFiles(event.dataTransfer.files);}catch(e){s.batchError=errorMessage(e);render();}});
