(function () {
  'use strict';

  const root = document.getElementById('dkr-mod-library');
  if (!root) return;
  const config = window.DKR_CONFIG || {};
  const Auth = window.DKRAuth;
  const Files = window.DKRFiles;
  const Uploads = window.DKRUploads;
  const A = {logo:'assets/logo.png',icon:'assets/icon.png',sky:'assets/sky.png',pattern:'assets/pattern.png','pattern-two':'assets/pattern-two.png'};
  const C = {items:[],categories:[
    {id:'all',label:'All downloads',short:'Browse all',icon:'compass',description:'Tracks, racers and new ways to play.'},
    {id:'tracks',label:'Custom Tracks',short:'Tracks',icon:'flag',description:'Find your next favourite place to race.'},
    {id:'characters',label:'Custom Characters',short:'Characters',icon:'users',description:'Meet the next additions to your starting grid.'},
    {id:'legacy',label:'Legacy Mods / Romhacks',short:'Legacy / Romhacks',icon:'archive',description:'Explore classic packs and ROM patch releases.'},
    {id:'magic',label:'Custom Magic Codes',short:'Magic Codes',icon:'wand-sparkles',description:'Download new rules, options and ways to play.'},
    {id:'textures',label:'Texture Packs',short:'Texture Packs',icon:'layers',description:'A fresh look for your game, tracks and racers.'}
  ]};
  const REPO = 'https://github.com/ThatGuyMcd/DKR-R';
  let D = null;
  let started = false;
  let bootLoading = true;
  let bootError = '';
  let pendingWrite = false;
  let authPending = false;
  let authIdentity = null;
  let authAuthority = null;
  let serverRole = '';
  let privateEpoch = 0;
  let navigation = 0;
  let loginDestination = null;
  let uploadScopeSerial = 0;
  let uploadTarget = null;
  let uploadAppliedId = '';

  function blankUI() {
    return {view:'store',category:'all',search:'',sort:'featured',tag:'all',limit:6,itemId:null,detailTab:'overview',
      accountTab:'requests',loginMode:'player',rememberSignIn:false,returnTo:null,queueTab:'requests',queueFilter:'all',requestId:null,reportId:null,
      editorMode:'admin',editorRequestId:null,editorDraft:null,pendingPublication:null,adminTab:'catalogue',adminLimit:12,
      filePickerKind:'',filePickerSearch:'',filePickerError:'',hostedSelections:{download:null,image:null},
      uploadOpenKind:'',uploadScope:0,uploadJobsShown:false,uploadError:'',uploadConnection:'',
      requestDraft:{},reviewDrafts:{},decisionDrafts:{},resubmitDrafts:{},noteDrafts:{},metadataDraft:null,pageLoading:false,pageError:'',reviewError:'',
      deleteOpen:false,archiveTarget:null};
  }
  let s = blankUI();
  const $ = selector => root.querySelector(selector);
  const esc = value => String(value == null ? '' : value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const list = value => Array.isArray(value) ? value : [];
  const user = () => D && typeof D.user === 'function' ? D.user() : null;
  const isStaff = () => !!D && D.isStaff();
  const isAdmin = () => !!D && D.isAdmin();
  const identity = value => value && value.id != null ? String(value.id) : null;
  const roleName = role => ({player:'Player',moderator:'Moderator',admin:'Admin'}[role] || 'Player');
  const category = id => C.categories.find(c=>c.id===id) || C.categories[0];
  const icon = name => '<span class="dkr-icon"><i data-lucide="'+esc(name)+'" aria-hidden="true"></i></span>';
  const button = (action,label,style,id,extra) => '<button type="button" class="dkr-button '+(style||'quiet')+'" data-action="'+esc(action)+'"'+(id!=null?' data-id="'+esc(id)+'"':'')+(extra?' '+extra:'')+'>'+label+'</button>';
  const textButton = (action,label,id) => '<button type="button" class="dkr-text-button" data-action="'+esc(action)+'"'+(id!=null?' data-id="'+esc(id)+'"':'')+'>'+label+'</button>';
  const submit = (label,value,style) => '<button type="submit" value="'+esc(value||'save')+'" class="dkr-button '+(style||'gold')+'">'+label+'</button>';
  const field = (label,name,value,type,attrs,help) => '<label class="dkr-field"><span>'+label+'</span><input name="'+esc(name)+'" type="'+(type||'text')+'" value="'+esc(value)+'"'+(attrs?' '+attrs:'')+'>'+(help?'<small>'+help+'</small>':'')+'</label>';
  const area = (label,name,value,attrs,help) => '<label class="dkr-field"><span>'+label+'</span><textarea name="'+esc(name)+'"'+(attrs?' '+attrs:'')+'>'+esc(value)+'</textarea>'+(help?'<small>'+help+'</small>':'')+'</label>';
  const selectField = (label,name,html,attrs,help) => '<label class="dkr-field"><span>'+label+'</span><select name="'+esc(name)+'"'+(attrs?' '+attrs:'')+'>'+html+'</select>'+(help?'<small>'+help+'</small>':'')+'</label>';
  const options = (rows,value) => rows.map(r=>'<option value="'+esc(r[0])+'"'+(String(value)===String(r[0])?' selected':'')+'>'+esc(r[1])+'</option>').join('');
  const categoryOptions = value => options(C.categories.filter(c=>c.id!=='all').map(c=>[c.id,c.label]),value);
  const statusLabels = {pending:'Pending review',review:'In review',changes:'Changes requested',approved:'Approved · Awaiting publication',published:'Published',rejected:'Rejected'};
  const status = value => '<span class="dkr-status '+esc(value)+'">'+esc(statusLabels[value]||value)+'</span>';
  const banner = (html,kind) => '<div class="dkr-banner '+(kind||'')+'">'+icon(kind==='error'?'circle-alert':'info')+'<div>'+html+'</div></div>';
  const empty = (title,description,action) => '<div class="dkr-empty"><h2 class="dkr-subtitle">'+esc(title)+'</h2><p>'+esc(description)+'</p>'+(action?'<div class="dkr-spaced">'+action+'</div>':'')+'</div>';
  const progress = label => '<div class="dkr-progress" role="status"><span class="dkr-spinner" aria-hidden="true"></span><h1 class="dkr-title">'+esc(label||'Loading')+'</h1><p>Please wait a moment.</p></div>';

  function safeURL(value,httpsOnly) {
    try { const u=new URL(value); return (u.protocol==='https:' || (!httpsOnly&&u.protocol==='http:')) && !u.username && !u.password ? u.href : ''; }
    catch (_) { return ''; }
  }
  function validateFileAddress(value,kind) {
    const label=kind==='image'?'Cover image':'Download file';
    let url;
    try { url=new URL(String(value).trim()); }
    catch (_) { throw new Error(label+': enter a complete HTTPS file address.'); }
    if(url.protocol!=='https:'||url.username||url.password||url.search||url.hash||/[\\\r\n]/.test(String(value)))throw new Error(label+': use a permanent HTTPS address without a login, query string (?) or fragment (#).');
    let path;
    try { path=decodeURIComponent(url.pathname); }
    catch (_) { throw new Error(label+': the address contains an invalid encoded filename.'); }
    if(/%(?:2f|5c|00|25)/i.test(url.pathname))throw new Error(label+': the address contains an unsupported encoded path. Choose the file from the inventory or use its direct link.');
    if(kind==='image'&&!/\.(?:png|jpe?g|webp)$/i.test(path))throw new Error('Cover image: choose a PNG, JPEG or WebP file.');
    if(kind!=='image'&&!/\.(?:zip|rtz|dkrmap|xdelta|nrm|dkrcode|bps|ips|7z)$/i.test(path))throw new Error('Download file: choose a supported mod package, such as ZIP, XDELTA or NRM.');
    return url.href;
  }
  const INVITE = safeURL(config.discordInvite,true) || 'https://discord.com/invite/AMWfXdBjNP';
  function externalLink(href,label,style) {
    const url=safeURL(href,false);
    return url?'<a class="dkr-button '+(style||'quiet')+'" href="'+esc(url)+'" target="_blank" rel="noopener noreferrer">'+label+'</a>':'';
  }
  function sourceLink(value) {
    const url=safeURL(value,false);
    return url?'<a href="'+esc(url)+'" target="_blank" rel="noopener noreferrer">'+esc(value)+'</a>':'<span class="dkr-muted">Not supplied</span>';
  }
  function image(value,alt,css) {
    const src=A[value]||safeURL(value,true)||A.pattern;
    return '<img src="'+esc(src)+'" alt="'+esc(alt||'')+'"'+(css?' class="'+esc(css)+'"':'')+' loading="lazy" referrerpolicy="no-referrer">';
  }
  function timeValue(value) {
    if(value===undefined||value===null||value==='')return 0;
    let time=typeof value==='number'?value:/^\d+(\.\d+)?$/.test(String(value))?Number(value):Date.parse(value);
    if(!Number.isFinite(time))return 0;
    if(time>0&&time<100000000000)time*=1000;
    return time;
  }
  function dateText(value) {
    const time=timeValue(value);if(!time)return 'Date unavailable';
    const date=new Date(time);
    return Number.isNaN(date.getTime())?'Date unavailable':new Intl.DateTimeFormat('en-GB',{day:'numeric',month:'short',year:'numeric',hour:'2-digit',minute:'2-digit'}).format(date);
  }
  function byteText(value) {
    const n=Number(value);if(!Number.isFinite(n)||n<=0)return '';
    if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KiB';if(n>=1073741824)return (n/1073741824).toFixed(1)+' GiB';return (n/1048576).toFixed(1)+' MiB';
  }
  function authorName(review) { return review.authorName||review.displayName||'Community member'; }
  function staffName(id) { const account=list(D&&D.accounts).find(a=>String(a.id)===String(id));return account?account.displayName:'Library staff'; }
  function rating(id) { return D?D.rating(id):{average:0,count:0,distribution:[0,0,0,0,0]}; }
  function stars(score) { const value=Math.max(0,Math.min(5,Number(score)||0));return '<span class="dkr-stars" aria-label="'+value.toFixed(1)+' out of 5 stars">'+'★'.repeat(Math.round(value))+'☆'.repeat(5-Math.round(value))+'</span>'; }
  function ratings(id) { const r=rating(id);return '<button type="button" class="dkr-rating" data-action="reviews" data-id="'+esc(id)+'" aria-label="View player reviews">'+(r.count?icon('star')+'<strong>'+Number(r.average).toFixed(1)+'</strong><span class="dkr-muted">('+r.count+')</span>':'<span class="dkr-muted">No ratings yet</span>')+'</button>'; }
  function releases(item) { return list(item.releases).slice().sort((a,b)=>timeValue(b.createdAt)-timeValue(a.createdAt)); }
  function latestRelease(item) { const rows=releases(item);return (safeURL(item.fileUrl,true)?rows.find(r=>r.fileUrl===item.fileUrl)||item:null)||rows.find(r=>safeURL(r.fileUrl,true))||null; }
  function downloadAnchor(release,label,style) {
    const url=release&&safeURL(release.fileUrl,true);
    return url?'<a class="dkr-button '+(style||'gold')+'" href="'+esc(url)+'" download'+(release.fileName?'="'+esc(release.fileName)+'"':'')+' rel="noopener noreferrer">'+icon('download')+(label||'Download')+'</a>':'<span class="dkr-small dkr-muted">No downloadable release</span>';
  }
  function managedItems() { return isStaff()&&Array.isArray(D.managedItems)&&D.managedItems.length?D.managedItems:list(D&&D.items); }
  function listingOptions(value) { return options([['','Choose an existing listing']].concat(managedItems().filter(m=>!m.archived).map(m=>[m.id,m.title+' · '+category(m.category).short])),value); }
  function pagehead(title,subtitle,backAction,id) {
    const back=backAction||'store';
    const label={moderation:'Back to moderation',account:'Back to my account',admin:'Back to admin','review-request':'Back to request'}[back]||'Back to library';
    const refresh=['account','request-status','moderation','review-request','admin','detail','editor','metadata-editor','publication-preview'].includes(s.view)?button('refresh-page',icon('refresh-cw')+'Refresh','quiet small'):'';
    return '<div class="dkr-pagehead"><div class="dkr-row dkr-between">'+button(back,icon('arrow-left')+label,'quiet small',id,'data-page-focus')+refresh+'</div><h1 class="dkr-heading">'+esc(title)+'</h1>'+(subtitle?'<p>'+subtitle+'</p>':'')+'</div>';
  }
  function renderIcons() { if(window.lucide&&window.lucide.createIcons)window.lucide.createIcons({attrs:{width:16,height:16}}); }
  function focusPage() { queueMicrotask(()=>{const node=$('[data-page-focus]')||$('#dkr-community-main');if(node)node.focus();}); }
  function restoreFocus(action,id) { queueMicrotask(()=>{const node=Array.from(root.querySelectorAll('[data-action="'+action+'"]')).find(el=>id==null||String(el.dataset.id)===String(id));if(node)node.focus({preventScroll:true});}); }
  function clearFlash() { const el=$('#dkr-community-flash');el.textContent='';el.className='';el.removeAttribute('role'); }
  function flash(message,error) { const el=$('#dkr-community-flash');el.className='dkr-flash'+(error?' error':'');el.setAttribute('role',error?'alert':'status');el.textContent=message;if(error){el.tabIndex=-1;el.focus();} }
  function formError(form,error) {
    const message=errorMessage(error);
    if(!form||!form.isConnected){flash(message,true);return;}
    form.querySelectorAll('[data-form-error]').forEach(el=>el.remove());
    form.insertAdjacentHTML('beforeend','<div class="dkr-banner error dkr-inline-error" role="alert" tabindex="-1" data-form-error>'+esc(message)+'</div>');
    form.querySelector('[data-form-error]').focus();
  }
  function errorMessage(error) {
    if(error&&(error.status===401||error.statusCode===401))return 'Your sign-in has expired. Sign in again, then retry this action. It will not be sent automatically.';
    return error&&error.message?error.message:typeof error==='string'?error:'Something went wrong. Please try again.';
  }
  function applyBusy() {
    root.setAttribute('aria-busy',pendingWrite?'true':'false');
    root.querySelectorAll('button,input,select,textarea').forEach(el=>{
      if(pendingWrite){if(!el.disabled){el.dataset.busyDisabled='true';el.disabled=true;}}
      else if(el.dataset.busyDisabled){el.disabled=false;delete el.dataset.busyDisabled;}
    });
  }
  async function perform(form,action) {
    if(pendingWrite)return;
    const epoch=privateEpoch;
    pendingWrite=true;if(form)form.setAttribute('aria-busy','true');applyBusy();
    try { await action(epoch);if(epoch===privateEpoch&&D&&typeof D.error==='string'&&D.error.startsWith('Your change was saved'))flash(D.error,true); }
    catch(error) { if(epoch===privateEpoch)formError(form,error); }
    finally { pendingWrite=false;if(form)form.removeAttribute('aria-busy');applyBusy(); }
  }
  function clearPrivateState() { privateEpoch++;navigation++;uploadTarget=null;uploadAppliedId='';s=blankUI();if(Uploads)Uploads.clear();clearFlash(); }
  function clearDecisionDrafts(id) { Object.keys(s.decisionDrafts).filter(k=>k.endsWith(':'+id)).forEach(k=>delete s.decisionDrafts[k]); }
  function formData(form) { const values={};new FormData(form).forEach((v,k)=>{if(typeof v==='string')values[k]=v;});return values; }
  function listingFormData(form,base) {
    const data=formData(form);
    const draft=Object.assign({},base,data,{featured:!!form.elements.featured.checked,
      tags:Array.from(new Set(String(data.tags||'').split(',').map(tag=>tag.trim()).filter(Boolean))),
      related:new FormData(form).getAll('related')});
    draft.image=data.imageChoice==='custom'?data.imageUrl:data.imageChoice;
    delete draft.imageChoice;delete draft.imageUrl;
    return draft;
  }
  function validateListingChoices(draft,complete) {
    if(draft.tags.length>12||draft.tags.some(tag=>tag.length>40))throw new Error('Choose up to 12 tags, with at most 40 characters each.');
    if(draft.related.length>10)throw new Error('Choose no more than 10 companion downloads.');
    if(complete!==false&&!['pattern','pattern-two'].includes(draft.image))draft.image=validateFileAddress(draft.image,'image');
  }
  function requireSignedIn(destination,staff) {
    if(user()||Auth&&Auth.getUser()&&Auth.getToken())return true;
    s.returnTo=destination||{view:s.view};s.loginMode=staff?'staff':'player';s.view='login';s.pageLoading=false;s.pageError='';clearFlash();render(true);return false;
  }

  function header() {
    const u=user();
    return '<button type="button" class="dkr-brand" data-action="store" aria-label="DKR-R Mod Library home">'+image('logo','Diddy Kong Racing Recompiled','dkr-logo')+'<span class="dkr-brand-copy"><strong>Mod Library</strong><span>Made by the community</span></span></button><nav class="dkr-header-nav" aria-label="Main navigation"><button type="button" class="dkr-navlink" data-action="store"'+(s.view==='store'?' aria-current="page"':'')+'>Library</button><button type="button" class="dkr-navlink" data-action="request">Request a mod</button>'+externalLink(INVITE,icon('messages-square')+'Join Discord','quiet')+(isStaff()?button('moderation',icon('shield-check')+'Moderation','quiet'):'')+(u?button('account',icon('user-round')+esc(u.displayName||'My account'),'gold'):button('login',icon('user-round')+'Sign in','gold'))+'</nav>';
  }
  function footer() { return '<span>DKR-R Mod Library <span class="dkr-muted">· Community creations, one starting grid.</span></span><div class="dkr-row"><a href="'+REPO+'" target="_blank" rel="noopener noreferrer">DKR-R project</a>'+textButton('help','Install help')+textButton('staff-login','Moderator login')+'<a href="privacy.html">Privacy</a></div>'; }
  function modcard(item) {
    return '<article class="dkr-modcard"><button type="button" class="dkr-coverbutton" data-action="detail" data-id="'+esc(item.id)+'" aria-label="View '+esc(item.title)+'">'+image(item.image||item.imageUrl,item.title,'dkr-cover')+'<span class="dkr-coverbadge">'+esc(category(item.category).short)+'</span></button><div class="dkr-rule" aria-hidden="true"></div><div class="dkr-cardbody"><button type="button" class="dkr-cardtitle" data-action="detail" data-id="'+esc(item.id)+'">'+esc(item.title)+'</button><div class="dkr-cardauthor">by '+esc(item.author)+'</div><div class="dkr-tags">'+list(item.tags).slice(0,2).map(t=>'<span>'+esc(t)+'</span>').join('')+'</div></div><div class="dkr-cardfoot">'+ratings(item.id)+downloadAnchor(latestRelease(item),'Download','gold small')+'</div></article>';
  }
  function filteredItems() {
    const query=s.search.trim().toLowerCase();
    const rows=list(D&&D.items).filter(m=>!m.archived&&(s.category==='all'||m.category===s.category)&&(s.tag==='all'||list(m.tags).includes(s.tag))&&(!query||(m.title+' '+m.author+' '+m.summary+' '+list(m.tags).join(' ')).toLowerCase().includes(query)));
    if(s.sort==='name')rows.sort((a,b)=>a.title.localeCompare(b.title));
    else if(s.sort==='rating')rows.sort((a,b)=>{const ra=rating(a.id),rb=rating(b.id);return rb.average-ra.average||rb.count-ra.count||a.title.localeCompare(b.title);});
    else rows.sort((a,b)=>Number(!!b.featured)-Number(!!a.featured)||timeValue(b.updatedAt)-timeValue(a.updatedAt));
    return rows;
  }
  function results() {
    if(bootError)return banner('<strong>Downloads are unavailable right now.</strong> '+esc(bootError)+'<div class="dkr-spaced">'+button('retry',icon('refresh-cw')+'Try again','quiet')+'</div>','error');
    const rows=filteredItems();
    if(!list(D&&D.items).length)return empty('The starting grid is open','The first community downloads will appear here when they are published.',isStaff()?button('add-listing',icon('plus')+'Add the first mod','gold'):externalLink(INVITE,icon('messages-square')+'Join the community','quiet'));
    return '<div class="dkr-resultmeta"><span role="status">'+rows.length+' '+(rows.length===1?'listing':'listings')+(s.search?' matching “'+esc(s.search)+'”':'')+'</span><span>Created by the DKR-R community</span></div><div class="dkr-grid">'+(rows.length?rows.slice(0,s.limit).map(modcard).join(''):empty('No mods found','Try another search, category or tag.',button('clear-filters','Clear filters','quiet')))+'</div>'+(rows.length>s.limit?'<div class="dkr-more">'+button('more','Show more mods '+icon('chevron-down'),'quiet')+'</div>':'');
  }
  function storePage() {
    const rows=list(D&&D.items).filter(m=>!m.archived);
    const featured=rows.find(m=>m.featured)||rows[0];
    const cat=category(s.category);
    const tags=Array.from(new Set(rows.filter(m=>s.category==='all'||m.category===s.category).flatMap(m=>list(m.tags))));
    const hero='<section class="dkr-hero" aria-label="'+(featured?'Featured mod':'Welcome to the mod library')+'"><div class="dkr-hero-img">'+image(featured?(featured.image||featured.imageUrl):'pattern',featured?featured.title:'','dkr-hero-img')+'</div><div class="dkr-hero-shade" aria-hidden="true"></div><div class="dkr-hero-copy"><span class="dkr-eyebrow">More tracks. More racers. More DKR.</span><h1 class="dkr-heading">Your next race<br>starts here.</h1><p>Discover community creations, find a fresh favourite and make DKR-R your own.</p><div class="dkr-row">'+(featured?button('detail','Explore the featured mod '+icon('arrow-right'),'gold',featured.id):externalLink(INVITE,icon('messages-square')+'Join the community','gold'))+'</div></div>'+(featured?'<div class="dkr-hero-label"><strong>'+esc(featured.title)+'</strong><br>by '+esc(featured.author)+'</div>':'')+'</section>';
    return hero+'<nav class="dkr-categories" aria-label="Mod categories">'+C.categories.filter(c=>c.id!=='all').map(c=>'<button type="button" class="dkr-category" data-action="category" data-id="'+c.id+'" aria-pressed="'+(s.category===c.id)+'">'+icon(c.icon)+'<strong>'+esc(c.id==='legacy'?'Legacy Mods':c.short)+'</strong><span class="dkr-small">'+({legacy:'Romhacks & collections',textures:'New looks & HD packs',magic:'New ways to play',characters:'Expand the starting grid',tracks:'Explore a new course'}[c.id])+'</span></button>').join('')+'</nav><section aria-label="Browse mods"><div class="dkr-sectionhead"><div><h2 class="dkr-title">'+esc(s.category==='all'?'Find your next favourite':cat.label)+'</h2><p>'+esc(cat.description)+'</p></div>'+(s.category!=='all'?button('category','Browse all','quiet small','all'):'')+'</div><div class="dkr-tools"><label class="dkr-search">'+icon('search')+'<input id="dkr-mod-search" name="search" type="search" value="'+esc(s.search)+'" placeholder="Search mods, creators or tags…" aria-label="Search mods"></label><label class="dkr-small dkr-muted">Sort <select id="dkr-mod-sort" class="dkr-select" aria-label="Sort mods">'+options([['featured','Featured & recent'],['rating','Highest rated'],['name','Name A–Z']],s.sort)+'</select></label></div><div class="dkr-filterrow" aria-label="Tag filters">'+['all'].concat(tags).map(t=>'<button type="button" class="dkr-chip" data-action="tag" data-id="'+esc(t)+'" aria-pressed="'+(s.tag===t)+'">'+esc(t==='all'?'All tags':t)+'</button>').join('')+'</div><div id="dkr-catalogue-results">'+results()+'</div></section><aside class="dkr-community-band"><div><h2 class="dkr-subtitle">Got something for the library?</h2><p>Share your own creation or suggest a mod you love. Our moderators review every request before it goes live.</p></div>'+button('request',icon('plus')+'Request a mod','gold')+'</aside>';
  }
  function installation(item) {
    if(item.install)return '<div class="dkr-prose"><p>'+esc(item.install)+'</p></div>';
    return '<div class="dkr-prose"><p>Installation instructions have not been added to this listing. Check the creator’s release notes and choose the launcher workflow that matches the file format.</p><p><a href="'+REPO+'" target="_blank" rel="noopener noreferrer">Open the DKR-R project documentation</a></p></div>';
  }
  function detailPage() {
    const m=D&&D.item(s.itemId);
    if(!m||m.archived&&!isAdmin())return empty('Listing unavailable','This download is not in the public library.',button('store','Back to library','gold'));
    const r=rating(m.id);
    let content='';
    if(s.detailTab==='reviews')content=reviewsPanel(m);
    else if(s.detailTab==='install')content='<h2 class="dkr-title">Install this mod</h2><div class="dkr-spaced">'+installation(m)+'</div>';
    else if(s.detailTab==='releases')content=releasePanel(m);
    else content='<div class="dkr-prose"><h2 class="dkr-title">About this mod</h2><p class="dkr-spaced">'+esc(m.summary)+'</p><h3>Compatibility & requirements</h3><p>'+esc(m.requirements||m.compatibility||'Compatibility details have not been supplied. Check the creator’s release notes before using this mod.')+'</p>'+(list(m.related).length?'<h3>Companion downloads</h3><div class="dkr-row">'+m.related.map(id=>{const rel=D.item(id);return rel&&!rel.archived?button('detail',icon('layers')+esc(rel.title),'quiet',id):'';}).join('')+'</div>':'')+'</div>';
    return pagehead(m.title,'by '+esc(m.author)+' · '+esc(category(m.category).label))+(m.archived?banner('This listing is archived. It is visible here to administrators only.','gold'):'')+'<div class="dkr-detailgrid"><figure class="dkr-gallery">'+image(m.image||m.imageUrl,m.title)+'<figcaption>'+esc(m.imageCaption||m.title)+'</figcaption></figure><aside class="dkr-panel dkr-detailinfo"><span class="dkr-eyebrow">'+esc(category(m.category).label)+'</span><h2 class="dkr-title">'+esc(m.title)+'</h2><div class="dkr-row">'+(r.count?stars(r.average):'')+ratings(m.id)+'</div><dl class="dkr-details"><div><dt>Creator</dt><dd>'+esc(m.author)+'</dd></div><div><dt>Release</dt><dd>'+esc(m.version||'Not supplied')+'</dd></div><div><dt>Package</dt><dd>'+esc(m.format||'See release files')+'</dd></div><div><dt>Updated</dt><dd>'+dateText(m.updatedAt||m.createdAt)+'</dd></div></dl>'+downloadAnchor(latestRelease(m),'Download mod','gold full')+button('install',icon('book-open')+'Installation guide','quiet full',m.id)+'<p class="dkr-small dkr-muted">No account is needed to browse or download.</p></aside></div><nav class="dkr-localnav" aria-label="Mod page sections">'+[['overview','Overview'],['install','Installation'],['reviews','Reviews ('+r.count+')'],['releases','Releases & files']].map(t=>'<button type="button" data-action="detail-tab" data-id="'+t[0]+'" aria-pressed="'+(s.detailTab===t[0])+'">'+t[1]+'</button>').join('')+'</nav><section>'+content+'</section>';
  }
  function releasePanel(m) {
    const rows=releases(m);
    return '<h2 class="dkr-title">Releases & files</h2><div class="dkr-list dkr-spaced">'+(rows.length?rows.map(release=>'<article class="dkr-listitem"><div><strong>'+esc(release.version||'Release')+'</strong><p>'+esc(release.fileName||'Download package')+' · '+esc(release.format||m.format||'')+'</p><div class="dkr-download-meta"><span>'+dateText(release.createdAt)+(byteText(release.size||release.fileSize)?' · '+byteText(release.size||release.fileSize):'')+'</span>'+(release.sha256?'<code>SHA-256: '+esc(release.sha256)+'</code>':'')+'</div>'+(release.requirements?'<p>'+esc(release.requirements)+'</p>':'')+(release.install?'<details class="dkr-release-guide"><summary>Installation for this release</summary><p>'+esc(release.install)+'</p></details>':'')+'</div>'+downloadAnchor(release,'Download '+esc(release.version||'release'),'gold small')+'</article>').join(''):empty('No release files','A release file has not been published for this listing.'))+'</div><p class="dkr-plain-note">Choose the version you need, then follow its installation instructions. Downloading does not install the mod automatically.</p>';
  }
  function reviewsPanel(m) {
    if(s.reviewError)return banner('Player reviews could not be loaded. '+esc(s.reviewError)+'<div class="dkr-spaced">'+button('reload-reviews','Try again','quiet',m.id)+'</div>','error');
    const r=rating(m.id),u=user(),own=u?D.ownReview(m.id):null;
    const draft=s.reviewDrafts[(u?u.id:'guest')+':'+m.id]||own;
    const versionNames=Array.from(new Set(['Unknown / not specified'].concat(releases(m).map(v=>v.version),draft&&draft.version?[draft.version]:[])));
    const distribution='<div class="dkr-distribution">'+[5,4,3,2,1].map(n=>{const count=list(r.distribution)[n-1]||0;return '<div><span>'+n+' ★</span><meter min="0" max="'+Math.max(1,r.count)+'" value="'+count+'" aria-label="'+n+' stars: '+count+' of '+r.count+' ratings"></meter><span>'+count+'</span></div>';}).join('')+'</div>';
    const form=u?'<form class="dkr-form dkr-reviewform" data-form="review" data-item-id="'+esc(m.id)+'"><h3 class="dkr-subtitle">'+(own?'Edit your review':'Share your experience')+'</h3><p class="dkr-small dkr-muted">Posting as '+esc(u.displayName)+(own&&own.hidden?' · Your review is hidden by moderation. Editing does not restore it.':'')+'</p><fieldset><legend>Your rating</legend><div class="dkr-scorechoices">'+[1,2,3,4,5].map(n=>'<label><input type="radio" name="score" value="'+n+'" required'+(draft&&Number(draft.score)===n?' checked':'')+'>'+n+' <span class="dkr-stars" aria-hidden="true">★</span></label>').join('')+'</div></fieldset>'+selectField('Version you played','version',options(versionNames.map(v=>[v,v]),draft?draft.version:'Unknown / not specified'))+area('Your review <span class="dkr-muted">(optional)</span>','text',draft?draft.text:'','maxlength="1000" placeholder="What worked well? What could be improved?"','One editable rating per account, per mod. Constructive criticism is welcome.')+'<div>'+submit(own?'Update review':'Post review')+'</div></form>':banner('Sign in to rate this mod or write a review. '+textButton('login-for-review','Sign in to review',m.id));
    const reviews=list(D.visibleReviews(m.id));
    return '<div class="dkr-sectionhead"><div><h2 class="dkr-title">Player reviews</h2><p>Experiences from the DKR-R community.</p></div></div><div class="dkr-ratinglayout"><aside class="dkr-panel"><div><div class="dkr-ratingbig">'+(r.count?Number(r.average).toFixed(1):'—')+'</div>'+(r.count?stars(r.average):'')+'<p class="dkr-small dkr-muted">'+r.count+' '+(r.count===1?'rating':'ratings')+'</p></div>'+distribution+'</aside><div>'+form+'<div class="dkr-list">'+(reviews.length?reviews.map(v=>{const mine=own&&String(own.id)===String(v.id);return '<article class="dkr-review"><div class="dkr-reviewhead"><strong>'+esc(authorName(v))+'</strong>'+stars(v.score)+'</div><div class="dkr-small dkr-muted">Version: '+esc(v.version||'Unknown / not specified')+' · '+dateText(v.updatedAt||v.createdAt)+'</div><p>'+esc(v.text||'Rating only.')+'</p>'+(mine?'<span class="dkr-small dkr-muted">Your review · Use the form above to edit.</span>':textButton('report-review','Report review',v.id))+(s.reportId===v.id?'<form class="dkr-form dkr-panel" data-form="report" data-review-id="'+esc(v.id)+'">'+selectField('Reason for report','reason',options([['Spam or advertising','Spam or advertising'],['Harassment or abusive language','Harassment or abusive language'],['Off-topic or misleading content','Off-topic or misleading content']],''))+'<div class="dkr-row">'+submit('Submit report','save','quiet small')+button('cancel-report','Cancel','quiet small')+'</div><p class="dkr-small dkr-muted">A report asks staff to assess the review. It does not hide it automatically.</p></form>':'')+'</article>';}).join(''):empty(r.count?'Reviews unavailable':'No reviews yet',r.count?'Review text could not be displayed. Try refreshing this page.':'Be the first to share your experience.'))+'</div>'+(D.reviewCursors&&D.reviewCursors[m.id]?'<div class="dkr-more">'+button('more-reviews','Load more reviews','quiet',m.id)+'</div>':'')+'</div></div>';
  }

  function loginPage() {
    const staff=s.loginMode==='staff';
    const setupMissing=!D||!D.configured;
    const setupMessage=!D?(bootError||'The library could not load its connection scripts. Reload this page or contact the site owner.'):(D.error||'The website connection settings are missing or invalid. Reload this page or contact the site owner.');
    return pagehead(staff?'Moderator access':'Your DKR-R account','')+'<div class="dkr-loginlayout"><section class="dkr-loginpitch">'+image('logo','Diddy Kong Racing Recompiled','dkr-loginart')+'<h2 class="dkr-heading">'+(staff?'Help the library grow.':'Welcome back, racer.')+'</h2><p class="dkr-muted">'+(staff?'Review incoming mods, help creators prepare their releases and keep player feedback useful.':'Rate your favourite mods, share useful feedback and suggest the next additions to the library.')+'</p><div class="dkr-bulletlist">'+(staff?[['clipboard-check','Review submissions and their release details.'],['messages-square','Send clear feedback to the requester.'],['shield-check','Prepare approved listings and assess reported reviews.']]:[['star','One account for ratings and reviews.'],['package-plus','Suggest a mod and follow its progress.'],['user-round','Choose the public name shown on your contributions.']]).map(row=>'<div>'+icon(row[0])+'<span>'+row[1]+'</span></div>').join('')+'</div><div>'+externalLink(INVITE,icon('messages-square')+'Join the DKR-R Discord','quiet')+'<p class="dkr-plain-note">Joining the Discord community is separate from signing in to this website.</p></div></section><section class="dkr-panel dkr-loginpanel"><h2 class="dkr-title">Sign in with Discord</h2><p class="dkr-muted dkr-spaced">'+(staff?'Use the Discord identity that the site administrator has granted staff access.':'Discord confirms your identity. Your public library name can be changed in your account settings.')+'</p><div class="dkr-spaced">'+(setupMissing?banner(esc(setupMessage),'error'):authPending?'<div class="dkr-login-status" role="status"><span class="dkr-busy-label"><span class="dkr-spinner" aria-hidden="true"></span>Complete sign-in in the Discord window.</span><p>If no window appeared, allow pop-ups for this site and try again.</p></div><div class="dkr-spaced">'+button('cancel-login','Cancel sign-in','quiet')+'</div>':(user()?'':rememberSignInChoice())+button('discord-login',icon('log-in')+(user()?'Continue with your account':'Continue with Discord'),'discord full'))+'</div>'+(!authPending&&Auth&&Auth.lastError?'<div class="dkr-spaced">'+banner(esc(Auth.lastError),'error')+'</div>':'')+'<p class="dkr-plain-note">This website does not ask for your Discord password or email address. Staff permissions are assigned by the site administrator.</p><p class="dkr-plain-note"><a href="privacy.html">Read how account information is used.</a></p></section></div>';
  }
  function deniedPage(adminOnly) { return pagehead(adminOnly?'Administrator access required':'Staff access required','Your current account does not have permission to open this workspace.')+banner('Signing in with Discord does not grant staff permissions. An administrator must assign the appropriate role.','gold')+'<div class="dkr-row">'+button('account','My account','quiet')+button('store','Browse the library','gold')+'</div>'; }
  function requestPage() {
    const d=s.requestDraft;
    return pagehead('Request a mod','Made something new, or found a mod that belongs here? Send it to the library team.')+banner('Every request goes to a private moderation queue. It appears in the library only after review and publication.')+'<div class="dkr-formgrid"><form class="dkr-form dkr-panel" data-form="request"><h2 class="dkr-subtitle">Tell us about the mod</h2><fieldset><legend>What would you like to share?</legend><div class="dkr-pair"><label class="dkr-choice"><input type="radio" name="kind" value="suggestion"'+(d.kind!=='creator'?' checked':'')+'>Suggest an existing mod</label><label class="dkr-choice"><input type="radio" name="kind" value="creator"'+(d.kind==='creator'?' checked':'')+'>I made this mod</label></div></fieldset>'+field('Mod title','title',d.title,'text','required maxlength="100" placeholder="Name of the mod"')+'<div class="dkr-pair">'+selectField('Category','category',categoryOptions(d.category||'tracks'))+field('Original creator','author',d.author,'text','required maxlength="100" placeholder="Creator or team name"')+'</div>'+field('Original release or source link','sourceUrl',d.sourceUrl,'url','required maxlength="1500" pattern="https://.+" placeholder="https://…"','Link to the creator’s page or original announcement.')+area('What does it change?','summary',d.summary,'required maxlength="1600" placeholder="Describe the mod and why it belongs in the library."')+'<div class="dkr-pair">'+selectField('Request type','relation',options([['new','New library listing'],['update','Update an existing listing']],d.relation||'new'))+selectField('Existing library listing','targetId',listingOptions(d.targetId),'','Choose a listing when requesting an update.')+'</div>'+field('Download link <span class="dkr-muted">(optional)</span>','downloadUrl',d.downloadUrl,'url','maxlength="1500" pattern="https://.+" placeholder="https://…"','This proposed download link stays private while staff review it.')+'<div class="dkr-pair">'+field('Tested DKR-R version <span class="dkr-muted">(if known)</span>','testedVersion',d.testedVersion,'text','maxlength="80" placeholder="Version tested, or unknown"')+field('Screenshot link <span class="dkr-muted">(optional)</span>','screenshotUrl',d.screenshotUrl,'url','maxlength="1500" pattern="https://.+" placeholder="https://…"','Use an HTTPS link. The screenshot stays in the private request until staff choose public artwork.')+'</div>'+area('Requirements or known limitations <span class="dkr-muted">(optional)</span>','requirements',d.requirements,'maxlength="1600" placeholder="Dependencies, source ROM revision, texture companions, install steps…"')+'<label class="dkr-check"><input type="checkbox" name="credit" required'+(d.credit?' checked':'')+'><span>I have named the original creator and provided the source as accurately as I can.</span></label><div class="dkr-row">'+submit(icon('send')+'Send for review')+button('account','My requests','quiet')+'</div><p class="dkr-small dkr-muted">Your request is visible to you and the moderation team.</p></form><aside class="dkr-stack"><section class="dkr-panel"><h2 class="dkr-subtitle">What happens next?</h2><ol class="dkr-timeline"><li><strong>Send your request</strong><p>Keep track of it in My requests.</p></li><li><strong>A moderator checks it</strong><p>We may ask for more information.</p></li><li><strong>Prepare the listing</strong><p>Approved content gets checked files, a description and installation instructions.</p></li><li><strong>Publish to the library</strong><p>Your request links to the finished listing.</p></li></ol></section><section class="dkr-panel"><h2 class="dkr-subtitle">All five categories</h2><div class="dkr-bulletlist">'+C.categories.filter(c=>c.id!=='all').map(c=>'<div>'+icon(c.icon)+'<span>'+esc(c.label)+'</span></div>').join('')+'</div></section></aside></div>';
  }
  function requestRow(r,staff) { return '<article class="dkr-listitem"><div><strong>'+esc(r.title)+'</strong><p>'+esc(category(r.category).label)+(staff?' · '+esc(r.ownerName||'Community member'):'')+'</p><p>'+dateText(r.updatedAt||r.createdAt)+'</p><div class="dkr-spaced">'+status(r.status)+'</div></div>'+button(staff?'review-request':'request-status',staff?'Open request '+icon('arrow-right'):'View status '+icon('arrow-right'),'quiet small',r.id)+'</article>'; }
  function accountPage() {
    const u=user();if(!u)return loginPage();
    const mine=list(D.reviews).filter(r=>String(r.userId)===String(u.id));
    const requests=list(D.requests).filter(r=>String(r.ownerId)===String(u.id));
    let content='';
    if(s.accountTab==='reviews')content='<div class="dkr-sectionhead"><h2 class="dkr-title">My reviews</h2><span class="dkr-small dkr-muted">'+mine.length+' '+(mine.length===1?'review':'reviews')+'</span></div><div class="dkr-list">'+(mine.length?mine.map(r=>{const m=D.item(r.itemId);return '<div class="dkr-listitem"><div><strong>'+esc(m?m.title:r.itemTitle||'Library mod')+'</strong><p>'+stars(r.score)+' · '+esc(r.version)+'</p><p>'+esc(r.text||'Rating only')+'</p>'+(r.hidden?'<p>Hidden by a moderator · '+esc(r.hiddenReason||'Contact the library team if you need more information.')+'</p>':'')+'</div>'+(m?button('reviews','Edit review','quiet small',r.itemId):'')+'</div>';}).join(''):empty('Your reviews start here','Choose a mod and share your experience.',button('store','Browse mods','gold')))+'</div>';
    else if(s.accountTab==='settings')content=accountSettings(u);
    else content='<div class="dkr-sectionhead"><div><h2 class="dkr-title">My requests</h2><p>Follow your suggestions from review to the public library.</p></div>'+button('request',icon('plus')+'Request a mod','gold')+'</div><div class="dkr-list">'+(requests.length?requests.map(r=>requestRow(r,false)).join(''):empty('No requests yet','Suggest a favourite mod or share your own creation.'))+'</div>';
    const initials=(u.displayName||'Racer').trim().split(/\s+/).slice(0,2).map(w=>w[0]).join('').toUpperCase();
    return '<div class="dkr-accounthead"><div class="dkr-avatar" aria-hidden="true">'+esc(initials)+'</div><div><span class="dkr-eyebrow">My account · '+roleName(u.role)+'</span><h1 class="dkr-title">'+esc(u.displayName)+'</h1><p class="dkr-small dkr-muted">Your place in the DKR-R community</p></div>'+button('refresh-page',icon('refresh-cw')+'Refresh','quiet small')+'</div><nav class="dkr-localnav" aria-label="My account sections">'+[['requests','My requests'],['reviews','My reviews'],['settings','Account settings']].map(t=>'<button type="button" data-action="account-tab" data-id="'+t[0]+'" aria-pressed="'+(s.accountTab===t[0])+'">'+t[1]+'</button>').join('')+'</nav>'+content;
  }
  function accountSettings(u) {
    return '<div class="dkr-stack"><section class="dkr-panel"><form class="dkr-form" data-form="profile"><h2 class="dkr-title">Your public name</h2>'+field('Public display name','displayName',u.displayName,'text','required maxlength="60" autocomplete="nickname"','Shown beside your reviews and requests. You can use a community alias.')+'<div>'+submit('Save public name')+'</div></form></section><section class="dkr-panel dkr-stack"><h2 class="dkr-subtitle">Sign-in</h2><p class="dkr-muted">You are signed in with Discord. This website does not store your Discord password or email address.</p>'+accountPersistence()+'<div class="dkr-row">'+externalLink(INVITE,icon('messages-square')+'Join Discord','quiet')+button('signout',icon('log-out')+'Sign out','quiet')+'</div><a href="privacy.html">Privacy and account information</a></section><section class="dkr-panel'+(s.deleteOpen?' dkr-delete-confirm':'')+'"><h2 class="dkr-subtitle">Delete your account</h2>'+(s.deleteOpen?'<form class="dkr-form" data-form="delete-account"><p class="dkr-small">Deleting your account permanently removes your library profile, sign-in sessions, mod requests, reviews, ratings and reports you submitted. Published mods, release files and creator credits remain in the library. Your name on retained staff activity becomes ‘Deleted account’. This cannot be undone.</p>'+field('Type DELETE to confirm','confirmation','','text','required pattern="DELETE" autocomplete="off" spellcheck="false"')+'<label class="dkr-check"><input type="checkbox" name="understood" required><span>I want to permanently delete this library account.</span></label><div class="dkr-row">'+submit('Permanently delete account','delete','red')+button('cancel-delete','Keep my account','quiet')+'</div></form>':'<p class="dkr-small dkr-muted">Remove your account and sign-in from this library.</p><div class="dkr-spaced">'+button('show-delete','Delete account…','red')+'</div>')+'</section></div>';
  }
  function timeline(r,staff) { return '<ol class="dkr-timeline">'+list(r.events).filter(e=>staff||e.visibility!=='staff').map(e=>'<li><strong>'+esc(e.label)+'</strong><p>'+esc(e.message||'')+'</p><p>'+esc(e.actorName||e.actor||'Library team')+' · '+dateText(e.createdAt||e.at||e.date)+(e.visibility==='staff'?' · Staff only':'')+'</p></li>').join('')+'</ol>'; }
  function requestStatusPage() {
    const r=D.request(s.requestId),u=user();
    if(!r||!u||String(r.ownerId)!==String(u.id))return empty('Request unavailable','Only the requester can open this account page.',button('account','My account','gold'));
    const draft=s.resubmitDrafts[r.id]||r;
    return pagehead(r.title,'Your library request · '+esc(category(r.category).label),'account')+'<div class="dkr-sectionhead">'+status(r.status)+(r.status==='published'?button('detail','Open published listing '+icon('arrow-right'),'gold',r.listingId):'')+'</div><div class="dkr-formgrid"><section class="dkr-panel"><h2 class="dkr-title">Request progress</h2>'+timeline(r,false)+(r.status==='approved'?'<p class="dkr-spaced dkr-muted">Your request is accepted. It will appear publicly after staff finish preparing and publishing the listing.</p>':'')+(r.status==='changes'?'<form class="dkr-form dkr-spaced" data-form="resubmit" data-request-id="'+esc(r.id)+'" data-revision="'+esc(r.revision)+'">'+area('Reply to the moderator','message',draft.message||'','required maxlength="1600" placeholder="Tell us what you have added or corrected."')+field('Updated source link','sourceUrl',draft.sourceUrl,'url','required maxlength="1500" pattern="https://.+"')+field('Updated download link <span class="dkr-muted">(optional)</span>','downloadUrl',draft.downloadUrl,'url','maxlength="1500" pattern="https://.+"')+field('Screenshot link <span class="dkr-muted">(optional)</span>','screenshotUrl',draft.screenshotUrl,'url','maxlength="1500" pattern="https://.+"')+field('Tested DKR-R version','testedVersion',draft.testedVersion,'text','maxlength="80"')+area('Requirements and install information','requirements',draft.requirements,'maxlength="2000"')+'<div>'+submit('Resubmit for review')+'</div></form>':'')+'</section><aside class="dkr-panel dkr-datapairs"><h2 class="dkr-subtitle">What you submitted</h2><div><strong>Original creator</strong><p>'+esc(r.author)+'</p></div><div><strong>Source</strong>'+sourceLink(r.sourceUrl)+'</div><div><strong>Download link</strong>'+sourceLink(r.downloadUrl)+'</div><div><strong>Tested DKR-R version</strong><p>'+esc(r.testedVersion||'Not supplied')+'</p></div><div><strong>Description</strong><p>'+esc(r.summary)+'</p></div><div><strong>Requirements</strong><p>'+esc(r.requirements||'Not supplied')+'</p></div><div><strong>Request type</strong><p>'+(r.relation==='update'?'Update an existing listing':'New library listing')+'</p></div><div><strong>Screenshot link</strong>'+sourceLink(r.screenshotUrl)+'</div></aside></div>';
  }
  function moderationPage() {
    if(!isStaff())return deniedPage(false);
    let content='';
    if(s.queueTab==='reports')content=reportsPage();
    else {const rows=list(D.requests).filter(r=>s.queueFilter==='all'||r.status===s.queueFilter);content='<div class="dkr-queuefilters"><label class="dkr-small dkr-muted">Request status <select id="dkr-queue-filter" class="dkr-select">'+options([['all','All requests']].concat(Object.keys(statusLabels).map(k=>[k,statusLabels[k]])),s.queueFilter)+'</select></label><span class="dkr-small dkr-muted">'+rows.length+' '+(rows.length===1?'request':'requests')+'</span></div><div class="dkr-list">'+(rows.length?rows.map(r=>requestRow(r,true)).join(''):empty('Nothing in this queue','Choose another status to see more requests.'))+'</div>';}
    return pagehead('Moderation','Review incoming mods, prepare approved listings and keep community reviews useful.')+'<div class="dkr-row dkr-between"><span class="dkr-small dkr-muted">Signed in as '+esc(user().displayName)+' · '+roleName(user().role)+'</span>'+(isStaff()?button('admin','Library manager','quiet small'):'')+'</div><nav class="dkr-localnav" aria-label="Moderation sections">'+[['requests','Mod requests'],['reports','Review reports ('+list(D.reports).filter(r=>r.status==='open').length+')']].map(t=>'<button type="button" data-action="queue-tab" data-id="'+t[0]+'" aria-pressed="'+(s.queueTab===t[0])+'">'+t[1]+'</button>').join('')+'</nav>'+content;
  }
  function reviewRequestPage() {
    if(!isStaff())return deniedPage(false);
    const r=D.request(s.requestId);if(!r)return empty('Request unavailable','Choose another request.',button('moderation','Back to moderation','gold'));
    const key=user().id+':'+r.id;
    const cached=s.decisionDrafts[key];
    const decision=cached&&Number(cached.revision)===Number(r.revision)?cached:{message:'',checks:r.checks||{},revision:r.revision};
    let controls='';
    if(r.status==='pending')controls='<h2 class="dkr-subtitle">Ready to review</h2><p class="dkr-small dkr-muted">Start the review before recording a decision.</p>'+button('start-review',icon('clipboard-check')+'Start review','gold full',r.id,'data-revision="'+esc(r.revision)+'"');
    else if(r.status==='review')controls='<form class="dkr-form" data-form="decision" data-request-id="'+esc(r.id)+'" data-revision="'+esc(r.revision)+'"><h2 class="dkr-subtitle">Review checklist</h2><fieldset class="dkr-fieldset"><legend>Complete these checks before approval</legend>'+[['source','Creator and original source checked'],['files','Package and contents reviewed'],['requirements','Compatibility and install steps checked'],['credits','Screenshots and credits checked']].map(c=>'<label class="dkr-check"><input type="checkbox" name="'+c[0]+'"'+(decision.checks[c[0]]?' checked':'')+'><span>'+c[1]+'</span></label>').join('')+'</fieldset>'+area('Message to requester','message',decision.message,'maxlength="1600" placeholder="Explain your decision or what is missing."','A reason is required for changes or rejection. This message is visible to the requester.')+submit(icon('check')+'Approve request','approve','gold full')+'<div class="dkr-row">'+submit('Request changes','changes','quiet')+submit('Reject','reject','red')+'</div><p class="dkr-small dkr-muted">Approval sends the request to listing preparation. It does not publish a download.</p></form>';
    else if(r.status==='approved')controls='<h2 class="dkr-subtitle">Prepare for publication</h2><p class="dkr-small dkr-muted">Choose the release file and check the public details before publishing.</p>'+button('prepare-request',icon('package-check')+'Prepare listing','gold full',r.id);
    else if(r.status==='published')controls='<h2 class="dkr-subtitle">Published to the library</h2>'+button('detail','View listing '+icon('arrow-right'),'gold full',r.listingId);
    else controls='<h2 class="dkr-subtitle">'+esc(statusLabels[r.status])+'</h2><p class="dkr-small dkr-muted">'+(r.status==='changes'?'The requester can reply and resubmit from My requests.':'The decision and reason are visible to the requester.')+'</p>';
    return pagehead(r.title,'Submitted by '+esc(r.ownerName||'Community member')+' · '+esc(category(r.category).label),'moderation')+'<div class="dkr-sectionhead">'+status(r.status)+'<span class="dkr-small dkr-muted">Private request · '+esc(r.reviewerName||r.reviewedByName||(r.reviewedBy?staffName(r.reviewedBy):'Unassigned'))+'</span></div><div class="dkr-requestlayout"><div class="dkr-stack"><section class="dkr-panel dkr-datapairs"><h2 class="dkr-title">Submitted material</h2><div><strong>Request type</strong><p>'+(r.kind==='creator'?'Creator submission':'Suggestion of an existing mod')+' · '+(r.relation==='update'?'New release for an existing listing':'New library listing')+'</p></div><div><strong>Creator</strong><p>'+esc(r.author)+'</p></div><div><strong>Original source</strong>'+sourceLink(r.sourceUrl)+'</div><div><strong>Proposed download</strong>'+sourceLink(r.downloadUrl)+'</div><div><strong>Description</strong><p>'+esc(r.summary)+'</p></div><div><strong>Tested version</strong><p>'+esc(r.testedVersion||'Not supplied')+'</p></div><div><strong>Requirements and limitations</strong><p>'+esc(r.requirements||'Not supplied')+'</p></div><div><strong>Screenshot link</strong>'+sourceLink(r.screenshotUrl)+'</div></section><section class="dkr-panel"><h2 class="dkr-subtitle">Request history</h2>'+timeline(r,true)+'</section></div><aside class="dkr-stack"><section class="dkr-panel dkr-stack">'+controls+'</section><section class="dkr-panel"><form class="dkr-form" data-form="staff-note" data-request-id="'+esc(r.id)+'" data-revision="'+esc(r.revision)+'"><h2 class="dkr-subtitle">Private staff notes</h2><p class="dkr-small dkr-muted">Visible to moderators and admins only. Never copied into public listing text.</p>'+area('Add a note','note',s.noteDrafts[r.id]||'','required maxlength="1600" placeholder="Testing notes, duplicate checks, follow-up…"')+'<div>'+submit('Save staff note','save','quiet small')+'</div></form><div class="dkr-list dkr-spaced">'+list(r.notes).map(n=>'<div class="dkr-review"><strong class="dkr-small">'+esc(n.actorName||n.actor||'Library staff')+'</strong><p>'+esc(n.text||n.message)+'</p><p>'+dateText(n.createdAt||n.at||n.date)+'</p></div>').join('')+'</div></section></aside></div>';
  }
  function reportsPage() {
    return banner('Reports stay visible until a moderator acts. Negative feedback alone is not a reason to hide a review.')+'<div class="dkr-list">'+(list(D.reports).length?D.reports.map(report=>{const review=report.review||list(D.reviews).find(v=>v.id===report.reviewId);if(!review)return '<article class="dkr-panel"><h2 class="dkr-subtitle">Reported review unavailable</h2><p class="dkr-muted">Refresh the moderation queue to load this report’s content.</p></article>';const m=D.item(review.itemId);return '<article class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><h2 class="dkr-subtitle">'+esc(m?m.title:report.itemTitle||'Mod review')+'</h2><span class="dkr-status '+(report.status==='open'?'pending':'published')+'">'+(report.status==='open'?'Open report':'Resolved')+'</span></div><div class="dkr-row">'+stars(review.score)+'<span class="dkr-small dkr-muted">by '+esc(authorName(review))+' · '+esc(review.version||'Version not specified')+'</span></div><p class="dkr-safe-wrap">'+esc(review.text||'Rating only')+'</p><p class="dkr-small dkr-muted"><strong>Report reason:</strong> '+esc(report.reason)+'</p>'+(report.status==='open'?'<form class="dkr-form" data-form="report-decision" data-report-id="'+esc(report.id)+'" data-revision="'+esc(report.revision)+'" data-review-revision="'+esc(review.revision)+'">'+area('Moderation reason','reason','','maxlength="1000" placeholder="Required if hiding the review."')+'<div class="dkr-row">'+submit('Dismiss report','keep','quiet')+submit('Hide review','hide','red')+'</div></form>':'<p class="dkr-small dkr-muted">Decision: '+esc(report.action||'Resolved')+' · '+esc(report.resolution||'')+'</p>'+(review.hidden?button('restore-review','Restore review','quiet small',report.id,'data-revision="'+esc(report.revision)+'" data-review-revision="'+esc(review.revision)+'"'):'<span class="dkr-small dkr-muted">Review is visible.</span>'))+(m?button('reviews','View mod reviews','quiet small',review.itemId):'')+'</article>';}).join(''):empty('No reported reviews','Reported content will appear here for staff to assess.'))+'</div>';
  }

  function prepareEditor(mode,id,saved) {
    const request=mode==='request'?D.request(id):null;
    const item=mode==='admin'&&id?D.item(id):request&&request.targetId?D.item(request.targetId):null;
    const source=item||request||{};
    const cat=source.category||'tracks';
    s.editorMode=mode;s.editorRequestId=request?request.id:null;
    resetFilePicker();
    s.editorDraft=saved?Object.assign({},saved):{target:item?'existing':'new',targetId:item?item.id:'',targetRevision:item?item.revision:undefined,requestRevision:request?request.revision:undefined,title:source.title||'',author:source.author||'',category:cat,summary:source.summary||'',version:'',fileName:'',fileUrl:request?request.downloadUrl||'':'',format:{tracks:'.dkrmap',characters:'.xdelta',legacy:'.xdelta',magic:'.dkrcode',textures:'.zip'}[cat],requirements:request?request.requirements||'':source.requirements||'',install:'',image:source.image||'pattern',featured:!!source.featured,tags:list(source.tags).slice(),related:list(source.related).slice()};
    return go('editor',{},false);
  }
  function resetFilePicker() {
    s.filePickerKind='';s.filePickerSearch='';s.filePickerError='';s.hostedSelections={download:null,image:null};
    resetUploadContext();
  }
  function resetUploadContext() {
    if(Uploads&&Uploads.state.running)Uploads.pause();
    uploadTarget=null;s.uploadScope=++uploadScopeSerial;s.uploadOpenKind='';s.uploadJobsShown=false;s.uploadError='';s.uploadConnection='';
  }
  function currentListingForm() {
    return s.view==='editor'?$('[data-form="publication"]'):s.view==='metadata-editor'?$('[data-form="metadata"]'):null;
  }
  function captureListingForm() {
    const form=currentListingForm();if(!form)return null;
    const key=s.view==='editor'?'editorDraft':'metadataDraft';
    // perform() disables controls after its caller snapshots the form. Reading
    // FormData during that window would omit artwork, tags and other controls.
    // Navigation or a concurrent upload completion must keep that snapshot.
    if(pendingWrite)return s[key]||null;
    s[key]=listingFormData(form,s[key]);return s[key];
  }
  function filePickerRows(kind,d) {
    if(!Files||!Files.loaded)return '';
    const all=Files.list({kind,category:d.category});
    const query=s.filePickerSearch.trim().toLocaleLowerCase();
    const rows=all.filter(file=>!query||file.fileName.toLocaleLowerCase().includes(query));
    if(!rows.length)return '<p class="dkr-small dkr-muted">'+esc(all.length?'No filenames match your search.':(kind==='image'?'No cover images':'No packages')+' are available for '+category(d.category).label+'. Upload a file or enter a public HTTPS address below.')+'</p>';
    return '<p class="dkr-small dkr-muted">'+rows.length+' '+(rows.length===1?'file':'files')+'</p><div class="dkr-file-list">'+rows.map(file=>'<button type="button" class="dkr-file-option" data-action="select-hosted-file" data-id="'+esc(file.id)+'" data-kind="'+esc(kind)+'">'+(kind==='image'?image(file.url,'','dkr-file-thumb'):icon('package'))+'<span><strong>'+esc(file.fileName)+'</strong><small>'+esc(byteText(file.size))+' · '+esc(file.format)+' · '+esc(category(file.category).short)+'</small></span>'+icon('arrow-right')+'</button>').join('')+'</div>';
  }
  function hostedFilePicker(kind,d) {
    if(!isStaff())return '';
    const open=s.filePickerKind===kind,selected=s.hostedSelections[kind];
    const matches=selected&&(kind==='download'?d.fileUrl===selected.url:(d.imageUrl||d.image)===selected.url);
    let body='';
    if(open){
      if(s.filePickerError||Files&&Files.error)body=banner(esc(s.filePickerError||Files.error),'error')+button('open-hosted-files','Try again','quiet small',kind);
      else if(!Files||Files.loading||!Files.loaded)body='<p class="dkr-busy-label dkr-small" role="status"><span class="dkr-spinner" aria-hidden="true"></span>Loading the file inventory…</p>';
      else body='<label class="dkr-field"><span>Find a filename</span><input type="search" id="dkr-hosted-search" value="'+esc(s.filePickerSearch)+'" placeholder="Search '+esc(category(d.category).short.toLowerCase())+' files" autocomplete="off"></label><div id="dkr-hosted-results" aria-live="polite">'+filePickerRows(kind,d)+'</div>';
    }
    return '<section class="dkr-hosted-files"><div class="dkr-row dkr-between">'+button(open?'close-hosted-files':'open-hosted-files',icon(kind==='image'?'images':'folder-open')+(open?'Close file picker':kind==='image'?'Choose an existing cover image':'Choose an existing package'),'quiet',kind,'aria-expanded="'+open+'" aria-controls="dkr-hosted-'+kind+'"')+(matches?'<span class="dkr-small dkr-file-selected">'+icon('check')+esc(byteText(selected.size))+' selected</span>':'')+'</div><div id="dkr-hosted-'+kind+'"'+(!open?' hidden':'')+' class="dkr-file-picker-body">'+body+'</div><p class="dkr-plain-note">'+esc(open&&Files&&Files.note?Files.note:'Completed website uploads appear here; files added separately over FTP require an inventory update.')+'</p></section>'+uploadPanel(kind,d);
  }
  async function openHostedFilePicker(kind) {
    if(!isStaff()||!['download','image'].includes(kind))throw new Error('Moderator access is required.');
    if(kind==='download'&&s.view!=='editor')return;
    if(!captureListingForm())return;
    const epoch=privateEpoch,seq=navigation,view=s.view;
    s.filePickerKind=kind;s.filePickerSearch='';s.filePickerError='';
    if(!Files){s.filePickerError='The file-picker script could not load. Reload the page or enter the HTTPS address below.';render();return;}
    const loading=Files.load({force:!!Files.error});render();
    try { await loading; }
    catch(error){if(epoch===privateEpoch&&seq===navigation&&view===s.view)s.filePickerError=errorMessage(error);}
    if(epoch!==privateEpoch||seq!==navigation||view!==s.view||s.filePickerKind!==kind)return;
    captureListingForm();render();
    const search=$('#dkr-hosted-search');if(search)search.focus({preventScroll:true});
  }
  function selectHostedFile(id,kind) {
    if(!isStaff()||!Files||s.filePickerKind!==kind)throw new Error('Open the file picker again before choosing a file.');
    if(unfinishedUploadForCurrentForm())throw new Error('Finish or cancel this form’s upload before choosing another hosted file.');
    const draft=captureListingForm();if(!draft)return;
    const file=Files.list({kind,category:draft.category}).find(row=>row.id===id);
    if(!file)throw new Error('Choose a file from the current category.');
    if(kind==='download'){
      if(s.view!=='editor')return;
      Object.assign(draft,{fileUrl:file.url,fileName:file.fileName,format:file.format});
    } else if(kind==='image'){
      draft.image=file.url;delete draft.imageChoice;delete draft.imageUrl;
    } else return;
    s.hostedSelections[kind]=file;s.filePickerKind='';s.filePickerSearch='';render();
    const input=$('[name="'+(kind==='download'?'fileUrl':'imageUrl')+'"]');if(input)input.focus({preventScroll:true});
    flash(file.fileName+' selected. Check the file and release details before publishing.');
  }
  function clearMismatchedHostedFiles(draft) {
    const download=s.hostedSelections.download,cover=s.hostedSelections.image;
    if(download&&download.category!==draft.category){
      if(draft.fileUrl===download.url){draft.fileUrl='';draft.fileName='';draft.format='';}
      s.hostedSelections.download=null;
    }
    if(cover&&cover.category!==draft.category){
      if(draft.image===cover.url){draft.image='pattern';delete draft.imageChoice;delete draft.imageUrl;}
      s.hostedSelections.image=null;
    }
    s.filePickerSearch='';s.filePickerError='';
  }
  function uploadScopeKey() {
    const draft=s.view==='editor'?s.editorDraft:s.view==='metadata-editor'?s.metadataDraft:null;
    return draft?JSON.stringify([privateEpoch,s.uploadScope,s.view,s.editorMode,s.editorRequestId,draft.itemId||'',draft.targetId||'',draft.category]):'';
  }
  function uploadContext(kind) { return {key:uploadScopeKey(),navigation,epoch:privateEpoch,kind}; }
  function uploadMatchesContext(context,includeNavigation) {
    return !!context&&context.epoch===privateEpoch&&context.key===uploadScopeKey()&&(!includeNavigation||context.navigation===navigation);
  }
  function unfinishedUploadForCurrentForm() {
    if(!Uploads||!uploadMatchesContext(uploadTarget,false))return false;
    const state=Uploads.state;
    return !!(state.file||state.job)&&!['completed','cancelled','idle'].includes(state.status)&&!(state.job&&state.job.state==='expired');
  }
  function uploadButton(action,label,kind,disabled,style) {
    return button(action,label,style||'quiet small',kind,disabled?'disabled':'');
  }
  function uploadPanel(kind,draft) {
    if(!isStaff())return '';
    const open=s.uploadOpenKind===kind;
    return '<section class="dkr-upload-panel"><div class="dkr-row dkr-between">'+button(open?'close-upload':'open-upload',icon('upload')+(open?'Close uploader':kind==='image'?'Upload cover':'Upload package'),'quiet',kind,'aria-expanded="'+open+'" aria-controls="dkr-upload-'+kind+'"')+'<span class="dkr-small dkr-muted">From your computer</span></div><div id="dkr-upload-'+kind+'" data-upload-body="'+kind+'" class="dkr-upload-body"'+(!open?' hidden':'')+'>'+(open?uploadPanelBody(kind,draft):'')+'</div></section>';
  }
  function uploadPanelBody(kind,draft) {
    if(!Uploads)return banner('The upload script could not load. Reload the page or contact the site owner.','error');
    const state=Uploads.state,settings=Uploads.settings,record=state.job;
    const same=state.kind===kind&&state.category===draft.category;
    const pending=!!record&&['open','completing','cancel_pending'].includes(record.state);
    const otherPending=pending&&!same;
    const disabled=state.running||state.loading||state.checking||state.status==='checking'||state.status==='cancelling';
    const configured=!!settings&&settings.enabled&&settings.configured;
    let html='<p class="dkr-small dkr-muted">Upload to <strong>'+esc(category(draft.category).label)+'</strong>. For mod packages and artwork intended for public download. The completed address fills in below. Publishing is a separate step.</p>';
    if(state.loading&&!settings)html+='<p class="dkr-busy-label dkr-small" role="status"><span class="dkr-spinner" aria-hidden="true"></span>Checking upload availability…</p>';
    if(settings&&!configured)html+=banner(esc(settings.message||'The site owner needs to configure the secure upload connection before files can be sent.'),'gold');
    if(!settings&&!state.loading)html+=uploadButton('reload-upload-settings','Retry upload settings',kind,disabled);
    if(s.uploadError||state.error)html+='<div class="dkr-upload-error" role="alert">'+esc(s.uploadError||state.error)+'</div>';
    if(configured){
      const accepted=settings.formats[kind].join(',');
      const fileNeeded=same&&state.needsFile;
      html+='<label class="dkr-upload-choose'+(disabled||otherPending?' disabled':'')+'"><span>'+icon(kind==='image'?'image-plus':'file-up')+'<strong>'+(fileNeeded?'Choose the original file to resume':kind==='image'?'Choose a cover image':'Choose a mod package')+'</strong></span><input type="file" data-upload-file="'+kind+'" accept="'+esc(accepted)+'"'+(disabled||otherPending?' disabled':'')+'><small>'+(kind==='image'?'PNG, JPEG or WebP · Up to '+esc(byteText(settings.limits.image)):'Supported mod packages · Up to '+esc(byteText(settings.limits.download)))+'</small></label>';
    }
    if((same&&(state.file||record||state.status==='checking'))||otherPending){
      const label=record?record.originalName:state.file?state.file.name:'Selected file';
      const percent=state.size?(state.offset>=state.size?100:Math.min(99.9,100*state.offset/state.size)):0;
      html+='<div class="dkr-upload-transfer"><div class="dkr-row dkr-between"><strong class="dkr-upload-filename">'+esc(label)+'</strong><span class="dkr-status '+(state.status==='completed'?'published':'review')+'">'+esc({idle:'Ready',checking:'Checking file',ready:'Ready',uploading:'Uploading',pausing:'Pausing',paused:'Paused',completing:'Verifying',completed:'Complete',cancelling:'Cancelling',cancel_pending:'Cleanup pending',cancelled:'Cancelled',error:'Needs attention'}[state.status]||'Paused')+'</span></div>';
      if(otherPending)html+='<p class="dkr-small dkr-pending-note">This transfer belongs to '+esc(category(state.category).label)+' '+(state.kind==='image'?'artwork':'packages')+'. Finish or cancel it before choosing a different destination.</p>';
      if(state.size)html+='<div class="dkr-upload-progress"><progress max="'+state.size+'" value="'+state.offset+'" aria-label="Confirmed upload progress"></progress><span>'+esc(byteText(state.offset)||'0 B')+' / '+esc(byteText(state.size))+' · '+percent.toFixed(1)+'%</span></div>';
      if(state.message)html+='<p class="dkr-small dkr-muted" role="status">'+esc(state.message)+'</p>';
      if(record&&record.expiresAt&&['open','completing'].includes(record.state))html+='<p class="dkr-plain-note">Resume before '+esc(dateText(record.expiresAt))+'. After a page reload, choose the same unchanged file again.</p>';
      if(state.status==='completed'&&record&&record.asset&&same){
        if(kind==='image')html+=image(record.asset.url,'Uploaded cover preview','dkr-upload-cover-preview');
        html+='<p class="dkr-small dkr-muted">Saved as <strong class="dkr-safe-wrap">'+esc(record.fileName)+'</strong></p><div class="dkr-row">'+uploadButton('use-uploaded-file',icon('check')+'Use this file',kind,false,'quiet small')+'</div>';
      }else if(state.status!=='cancelled'){
        let actions='';
        if(state.running)actions+=uploadButton('pause-upload',icon('pause')+(state.status==='pausing'?'Pausing…':'Pause'),kind,state.status==='pausing'||state.status==='completing');
        else if(same&&state.file&&!['cancel_pending','cancelling','checking'].includes(state.status))actions+=uploadButton('start-upload',icon('upload')+(record?'Resume upload':'Start upload'),kind,disabled||!configured,'gold small');
        actions+=uploadButton('cancel-upload',icon('x')+(state.status==='cancel_pending'?'Retry cleanup':'Cancel upload'),kind,state.status==='cancelling','quiet small');
        html+='<div class="dkr-row">'+actions+'</div>';
      }
      html+='</div>';
    }
    html+='<div class="dkr-row dkr-upload-tools">'+uploadButton('load-upload-jobs',icon('history')+(s.uploadJobsShown?'Refresh saved uploads':'Find saved uploads'),kind,disabled)+(isAdmin()?uploadButton('check-upload-connection',icon('plug-zap')+(state.checking?'Checking…':'Check connection'),kind,disabled||!configured):'')+'</div>';
    if(s.uploadConnection)html+='<p class="dkr-upload-connection" role="status">'+esc(s.uploadConnection)+'</p>';
    if(s.uploadJobsShown){
      const rows=Uploads.jobs.filter(row=>row.kind===kind&&row.category===draft.category&&!['cancelled','expired'].includes(row.state));
      html+='<div class="dkr-upload-jobs"><h3 class="dkr-small">Your saved uploads</h3><p class="dkr-plain-note">Only transfers owned by your library account appear here.</p>'+(rows.length?rows.map(row=>'<div class="dkr-upload-job"><div><strong>'+esc(row.originalName)+'</strong><small>'+esc(byteText(row.offset)||'0 B')+' of '+esc(byteText(row.size))+' · '+(row.state==='completed'?'Complete':row.state==='cancel_pending'?'Cleanup pending':'Ready to resume')+'</small></div>'+button('select-upload-job',row.state==='completed'?'Use file':row.state==='cancel_pending'?'Finish cleanup':'Resume','quiet small',row.id,disabled?'disabled':'')+'</div>').join(''):'<p class="dkr-small dkr-muted">No saved uploads were found for this category.</p>')+'</div>';
    }
    html+='<p class="dkr-plain-note">Keep this page open during a transfer. Pause or resume when needed; confirmed progress is saved on the service.</p>';
    return html;
  }
  function redrawUploadPanels() {
    if(!isStaff()||!['editor','metadata-editor'].includes(s.view))return;
    const draft=s.view==='editor'?s.editorDraft:s.metadataDraft;if(!draft)return;
    root.querySelectorAll('[data-upload-body]').forEach(node=>{if(node.dataset.uploadBody===s.uploadOpenKind){node.innerHTML=uploadPanelBody(node.dataset.uploadBody,draft);}});
    renderIcons();applyBusy();
  }
  async function openUpload(kind,force) {
    if(!isStaff()||!['download','image'].includes(kind)||kind==='download'&&s.view!=='editor')return;
    if(!captureListingForm())return;
    s.uploadOpenKind=kind;s.uploadError='';s.uploadConnection='';
    const context=uploadContext(kind);render();
    if(!Uploads)return;
    try{await Uploads.loadSettings({force:!!force});}
    catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
    if(uploadMatchesContext(context,true))redrawUploadPanels();
  }
  async function chooseUploadFile(file,kind) {
    if(!Uploads||!isStaff())return;
    const draft=captureListingForm();if(!draft)return;
    const context=uploadContext(kind);s.uploadError='';uploadTarget=context;uploadAppliedId='';
    try{await Uploads.selectFile(file,{kind,category:draft.category});}
    catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
    if(uploadMatchesContext(context,true))redrawUploadPanels();
  }
  function applyUploadedAsset(asset) {
    if(!isStaff()||!asset)throw new Error('Moderator access and a completed upload are required.');
    const draft=captureListingForm();if(!draft)return;
    if(asset.category!==draft.category)throw new Error('Choose an uploaded file from this listing’s category.');
    if(asset.kind==='download'){
      if(s.view!=='editor')throw new Error('Package uploads belong in the release editor.');
      Object.assign(draft,{fileUrl:validateFileAddress(asset.url,'download'),fileName:asset.fileName,format:asset.format});
    }else if(asset.kind==='image'){
      draft.image=validateFileAddress(asset.url,'image');delete draft.imageChoice;delete draft.imageUrl;
    }else throw new Error('The completed file type is invalid.');
    s.hostedSelections[asset.kind]=asset;s.filePickerKind='';s.uploadError='';
    if(Files)Files.clear();
    render();flash(asset.fileName+' uploaded and selected. Review the listing before publishing.');
  }
  function uploadsChanged(state) {
    if(state.status==='completed'&&state.job&&state.asset&&state.job.id!==uploadAppliedId&&uploadMatchesContext(uploadTarget,true)&&!uploadTarget.cancelledIntent&&state.kind===uploadTarget.kind){
      uploadAppliedId=state.job.id;
      try{applyUploadedAsset(state.asset);}catch(error){s.uploadError=errorMessage(error);}
    }
    redrawUploadPanels();
  }
  function rememberSignInChoice() {
    return '<label class="dkr-check dkr-remember-choice"><input type="checkbox" id="dkr-remember-sign-in"'+(s.rememberSignIn?' checked':'')+'><span>Stay signed in on this device for up to 30 days<small>Use this on a device you trust. You can end it with Sign out.</small></span></label>';
  }
  function accountPersistence() {
    const mode=Auth&&Auth.persistenceMode;
    const message=mode==='remembered'?'Stay signed in is enabled on this device'+(Auth.rememberedUntil?' until '+dateText(Auth.rememberedUntil):' for up to 30 days')+'. Signing out removes it.':mode==='session'?'Your sign-in can survive a page refresh in this browser session. Choose Stay signed in next time you sign in to return for up to 30 days.':'Your sign-in is available in this open page. Choose Stay signed in next time you sign in to return for up to 30 days.';
    return '<p class="dkr-small dkr-muted">'+esc(message)+'</p>'+(Auth&&Auth.persistenceWarning?banner(esc(Auth.persistenceWarning),'gold'):'');
  }
  function publicationFormOptions(d) {
    const imageValue=d.imageChoice||(['pattern','pattern-two'].includes(d.image)?d.image:'custom');
    const imageURL=d.imageUrl!==undefined?d.imageUrl:imageValue==='custom'?d.image:'';
    return selectField('Cover artwork','imageChoice',options([['pattern','Launcher pattern'],['pattern-two','Alternate launcher pattern'],['custom','An image hosted on the web']],imageValue))+field('Public image URL','imageUrl',imageURL,'url',(imageValue==='custom'?'required ':'disabled ')+'maxlength="1500" pattern="https://.+" placeholder="https://your-site.example/images/mod-cover.png"','For custom artwork, use a permanent HTTPS image address. The launcher patterns do not need a URL.')+hostedFilePicker('image',d);
  }
  function companionChoices(d) {
    const rows=list(D.items).filter(m=>!m.archived&&m.id!==d.targetId&&m.id!==d.itemId);
    return '<fieldset class="dkr-fieldset"><legend>Companion downloads</legend><p class="dkr-small dkr-muted">Choose up to 10 published mods that work with this release, such as a matching texture pack.</p>'+(rows.length?'<div class="dkr-companion-list">'+rows.map(m=>'<label class="dkr-check"><input type="checkbox" name="related" value="'+esc(m.id)+'"'+(list(d.related).includes(m.id)?' checked':'')+'><span>'+esc(m.title)+' <small class="dkr-muted">· '+esc(category(m.category).short)+'</small></span></label>').join('')+'</div>':'<p class="dkr-small dkr-muted">Published downloads will be available to select here.</p>')+'</fieldset>';
  }
  function metadataPage() {
    if(!isStaff())return deniedPage();
    const d=s.metadataDraft;
    if(!d)return empty('Choose a listing to edit','Open a published listing from the library manager.',button('admin','Library manager','quiet'));
    const tags=Array.isArray(d.tags)?d.tags.join(', '):d.tags||'';
    return pagehead('Edit listing details','Update the public title, description, artwork and companion downloads.','admin')+'<div class="dkr-formgrid"><form class="dkr-form dkr-panel" data-form="metadata" data-item-id="'+esc(d.itemId)+'" data-revision="'+esc(d.expectedRevision)+'"><h2 class="dkr-subtitle">Public listing details</h2><p class="dkr-small dkr-muted">Category: '+esc(category(d.category).label)+'</p>'+field('Public title','title',d.title,'text','required maxlength="100"')+field('Creator credit','author',d.author,'text','required maxlength="100"')+area('Public description','summary',d.summary,'required maxlength="4000"')+publicationFormOptions(d)+field('Tags','tags',tags,'text','maxlength="502" placeholder="HD textures, Multiplayer"','Separate tags with commas. Choose up to 12 tags, with at most 40 characters each.')+companionChoices(d)+'<label class="dkr-check"><input type="checkbox" name="featured"'+(d.featured?' checked':'')+'><span>Feature this listing on the library homepage.</span></label><div class="dkr-row">'+submit('Save public details','save','gold')+button('admin','Cancel','quiet')+'</div></form><aside class="dkr-panel dkr-stack"><h2 class="dkr-subtitle">Keep the listing up to date</h2><p class="dkr-muted">A clear description and matching companion downloads help players choose the right mod.</p><p class="dkr-small dkr-muted">Use Add release in the library manager when a new package, version or installation guide is ready.</p></aside></div>';
  }
  function editorPage() {
    if(s.editorMode==='request'&&!isStaff()||s.editorMode==='admin'&&!isStaff())return deniedPage(s.editorMode==='admin');
    const d=s.editorDraft||{},r=s.editorRequestId?D.request(s.editorRequestId):null;
    if(r&&r.status!=='approved')return empty('Request is not ready','Only approved requests can move to publication.',button('review-request','Back to request','quiet',r.id));
    const tags=Array.isArray(d.tags)?d.tags.join(', '):d.tags||'';
    return pagehead(r?'Prepare the approved mod':d.target==='existing'?'Edit listing & add a release':'Add to the library',r?'Prepare the public release for “'+esc(r.title)+'”.':'Save a draft, check the preview, then publish the listing and its release.',r?'review-request':'admin',r?r.id:null)+'<div class="dkr-formgrid"><form class="dkr-form dkr-panel" data-form="publication"><h2 class="dkr-subtitle">Public listing details</h2><div class="dkr-pair">'+selectField('Publish as','target',options([['new','A new library listing'],['existing','A release for an existing listing']],d.target))+selectField('Existing listing','targetId',listingOptions(d.targetId),'',r&&r.relation==='update'?'This approved update must stay linked to its original listing.':'Select the existing listing to keep its reviews and release history.')+'</div>'+field('Public title','title',d.title,'text','required maxlength="100"')+'<div class="dkr-pair">'+field('Creator credit','author',d.author,'text','required maxlength="100"')+selectField('Category','category',categoryOptions(d.category))+'</div>'+area('Public description','summary',d.summary,'required maxlength="4000"','This is separate from private staff notes and messages to the requester.')+'<div class="dkr-pair">'+field('Release version','version',d.version,'text','required maxlength="80" placeholder="1.0.0"')+field('Package format','format',d.format,'text','required maxlength="40" placeholder=".zip"')+'</div><fieldset class="dkr-fieldset"><legend>Release file</legend>'+hostedFilePicker('download',d)+field('Public HTTPS download URL','fileUrl',d.fileUrl,'url','required maxlength="1500" pattern="https://.+" placeholder="https://your-site.example/downloads/my-mod-v1.zip"','Upload a package, choose an existing one above, or enter a permanent HTTPS file address from an approved host.')+field('Download filename <span class="dkr-muted">(optional)</span>','fileName',d.fileName,'text','maxlength="180" placeholder="my-mod-v1.zip"','Leave blank to use the filename from the download URL.')+'</fieldset>'+publicationFormOptions(d)+area('Requirements and compatibility','requirements',d.requirements,'maxlength="2000"')+area('Installation instructions','install',d.install,'required maxlength="6000" placeholder="The exact steps for this release."')+field('Tags','tags',tags,'text','maxlength="502" placeholder="HD textures, Multiplayer"','Separate tags with commas. Choose up to 12 tags, with at most 40 characters each.')+companionChoices(d)+'<label class="dkr-check"><input type="checkbox" name="featured"'+(d.featured?' checked':'')+'><span>Feature this listing on the library homepage.</span></label><div class="dkr-row">'+submit(icon('eye')+'Preview publication','preview','gold')+(!r?'<button type="submit" value="draft" formnovalidate class="dkr-button quiet">Save draft</button>':'')+'</div></form><aside class="dkr-stack"><section class="dkr-panel"><h2 class="dkr-subtitle">Before it goes live</h2><div class="dkr-bulletlist"><div>'+icon('image')+'<span>Check public artwork and creator credit.</span></div><div>'+icon('file-check')+'<span>Test the download URL and confirm the release version.</span></div><div>'+icon('book-open')+'<span>Include requirements and installation steps.</span></div><div>'+icon('eye')+'<span>Review the final page, then publish.</span></div></div></section>'+(r?'<section class="dkr-panel dkr-datapairs"><h2 class="dkr-subtitle">Approved request</h2><div><strong>Source</strong>'+sourceLink(r.sourceUrl)+'</div><div><strong>Submitted description</strong><p>'+esc(r.summary)+'</p></div><div><strong>Requirements</strong><p>'+esc(r.requirements||'Not supplied')+'</p></div><div><strong>Tested DKR-R version</strong><p>'+esc(r.testedVersion||'Not supplied')+'</p></div><div><strong>Screenshot</strong>'+sourceLink(r.screenshotUrl)+'</div></section>':'')+'</aside></div>';
  }
  function publicationPreviewPage() {
    const p=s.pendingPublication;
    if(!p)return empty('No publication prepared','Prepare a listing first.',button(isAdmin()?'admin':'moderation','Back','quiet'));
    if(p.mode==='request'&&!isStaff()||p.mode==='admin'&&!isStaff())return deniedPage(p.mode==='admin');
    const d=p.draft,m=d.target==='existing'?D.item(d.targetId):null;
    return pagehead('Ready for the library?','Review the public listing and release before you publish.',p.mode==='request'?'review-request':'admin',p.requestId)+banner((d.target==='existing'?'This adds a release to <strong>'+esc(m?m.title:'the selected listing')+'</strong>.':'This creates a new public library listing.')+' The request and private staff notes stay separate.')+'<div class="dkr-detailgrid"><figure class="dkr-gallery">'+image(d.image,d.title)+'<figcaption>Public listing artwork</figcaption></figure><section class="dkr-panel dkr-stack"><span class="dkr-eyebrow">'+esc(category(d.category).label)+'</span><h2 class="dkr-title">'+esc(d.title)+'</h2><p class="dkr-small dkr-muted">by '+esc(d.author)+'</p><p class="dkr-safe-wrap">'+esc(d.summary)+'</p><dl class="dkr-details"><div><dt>Release</dt><dd>'+esc(d.version)+'</dd></div><div><dt>File</dt><dd>'+esc(d.fileName||'From download URL')+'</dd></div><div><dt>Format</dt><dd>'+esc(d.format)+'</dd></div></dl><div class="dkr-datapairs"><div><strong>Download URL</strong>'+sourceLink(d.fileUrl)+'</div></div>'+externalLink(d.fileUrl,icon('external-link')+'Open release file to check','quiet')+'</section></div><div class="dkr-pair dkr-spaced"><section class="dkr-panel"><h2 class="dkr-subtitle">Requirements</h2><p class="dkr-small dkr-safe-wrap">'+esc(d.requirements||'None supplied')+'</p></section><section class="dkr-panel"><h2 class="dkr-subtitle">Installation</h2><p class="dkr-small dkr-safe-wrap">'+esc(d.install)+'</p></section></div><div class="dkr-row dkr-spaced">'+button('publish',icon('check')+'Publish to library','gold')+button('back-editor','Back to edit','quiet')+'</div><p class="dkr-plain-note">Publish makes this listing and its approved release available to everyone.</p>';
  }
  function adminPage() {
    if(!isStaff())return deniedPage();
    if(!isAdmin()&&s.adminTab!=='catalogue')s.adminTab='catalogue';
    let content='';
    if(s.adminTab==='staff')content=banner('Assign website roles to existing accounts. Discord sign-in identifies a person; it never chooses their permissions.')+'<div class="dkr-list">'+list(D.accounts).map(a=>'<form class="dkr-listitem" data-form="staff-role" data-account-id="'+esc(a.id)+'"><div><strong>'+esc(a.displayName)+'</strong><p>'+roleName(a.role)+'</p></div><div class="dkr-row"><label class="dkr-small">Role <select name="role" class="dkr-select" aria-label="Role for '+esc(a.displayName)+'">'+options([['player','Player'],['moderator','Moderator'],['admin','Admin']],a.role)+'</select></label>'+submit('Save role','save','quiet small')+'</div></form>').join('')+'</div>';
    else if(s.adminTab==='audit')content='<div class="dkr-panel"><h2 class="dkr-title">Recent library activity</h2>'+(list(D.audit).length?'<ol class="dkr-timeline">'+D.audit.slice().sort((a,b)=>(timeValue(b.createdAt||b.at))-(timeValue(a.createdAt||a.at))).map(a=>'<li><strong>'+esc(a.action)+'</strong><p>'+esc(a.message)+'</p><p>'+esc(a.actorName||a.actor||'Library team')+' · '+dateText(a.createdAt||a.at||a.date)+'</p></li>').join('')+'</ol>':'<p class="dkr-muted dkr-spaced">Publication, moderation and staff changes will appear here.</p>')+'</div>';
    else {
      const drafts=list(D.catalogueDrafts).filter(d=>d.status==='draft');const items=managedItems();
      content='<div class="dkr-sectionhead"><div><h2 class="dkr-title">Library manager</h2><p>Keep descriptions, artwork, release files and installation steps together.</p></div>'+button('add-listing',icon('plus')+'Add to library','gold')+'</div>'+(drafts.length?'<div class="dkr-panel dkr-spaced"><h3 class="dkr-subtitle">Unpublished drafts</h3><div class="dkr-list">'+drafts.map(d=>'<div class="dkr-listitem"><div><strong>'+esc(d.title||'Untitled draft')+'</strong><p>Draft · '+esc(d.version||'No version yet')+'</p></div>'+button('open-draft','Continue editing','quiet small',d.id)+'</div>').join('')+'</div></div>':'')+'<div class="dkr-list dkr-spaced">'+(items.length?items.slice(0,s.adminLimit).map(m=>'<div class="dkr-listitem"><div class="dkr-adminrow">'+image(m.image||m.imageUrl,'')+'<div><strong>'+esc(m.title)+'</strong><p>'+esc(category(m.category).label)+' · '+esc(m.version||'Version not supplied')+(m.archived?' · Archived':'')+'</p></div><div class="dkr-row">'+button('detail','View','quiet small',m.id)+(!m.archived?button('edit-details','Edit details','quiet small',m.id)+button('edit-listing','Add release','quiet small',m.id):'')+button(m.archived?'restore-listing':'archive-listing',m.archived?'Restore':'Archive','quiet small',m.id,'data-revision="'+esc(m.revision)+'"')+'</div></div></div>').join(''):empty('Your library starts here','Publish your first mod to open the catalogue.'))+'</div>'+(items.length>s.adminLimit?'<div class="dkr-more">'+button('admin-more','Show more listings','quiet')+'</div>':'');
    }
    return pagehead('Admin workspace','Manage the library and the people who look after it.')+'<nav class="dkr-localnav" aria-label="Admin sections">'+[['catalogue','Catalogue'],...(isAdmin()?[['staff','Staff access'],['audit','Activity log']]:[])].map(t=>'<button type="button" data-action="admin-tab" data-id="'+t[0]+'" aria-pressed="'+(s.adminTab===t[0])+'">'+t[1]+'</button>').join('')+'</nav>'+content;
  }
  function helpPage() {
    const rows={tracks:'Use the importer appropriate to the track release. Keep any matching HD companion with the exact track version it was made for.',characters:'Check the supported source ROM, character slot and the creator’s import instructions. Character packages may use different formats.',legacy:'Read the required source ROM revision and DKR-R compatibility notes before applying a patch or importing a legacy package.',magic:'Use the Custom Magic Codes workflow documented for your DKR-R build. Check the code’s requirements and any online-play limitations.',textures:'Use the texture-pack workflow appropriate to your DKR-R version. Read the required rendering preset, pack priority and companion requirements.'};
    return pagehead('Download. Import. Race.','Choose a release, download its package and follow the creator’s installation instructions.')+'<div class="dkr-stack">'+C.categories.filter(c=>c.id!=='all').map(c=>'<section class="dkr-panel"><h2 class="dkr-subtitle">'+esc(c.label)+'</h2><p class="dkr-muted">'+rows[c.id]+'</p></section>').join('')+'<section class="dkr-panel dkr-stack"><h2 class="dkr-subtitle">Installation is handled in DKR-R</h2><p class="dkr-muted">Downloading from this website does not install a mod automatically. The listing’s instructions and the project documentation explain the correct steps for each format.</p>'+externalLink(REPO,icon('book-open')+'Read the DKR-R documentation','quiet')+'</section></div>';
  }
  function completionPage() { return '<div class="dkr-progress"><h1 class="dkr-title">Discord sign-in</h1><p>'+esc(Auth.completionMessage||Auth.lastError||'Sign-in is complete. You can return to the library window.')+'</p>'+button('close-window','Close this window','quiet')+'</div>'; }
  function render(focus) {
    // The sky URL is stylesheet-relative and is defined in app.css.
    $('#dkr-community-header').innerHTML=header();$('#dkr-community-footer').innerHTML=footer();
    const main=$('#dkr-community-main');
    if(Auth&&Auth.completionWindow)main.innerHTML=completionPage();
    else if(bootLoading)main.innerHTML=progress('Opening the library');
    else if(s.pageLoading)main.innerHTML=progress('Loading your page');
    else if(s.pageError)main.innerHTML=pagehead('This page could not be loaded','')+banner(esc(s.pageError)+'<div class="dkr-spaced">'+button('retry-page','Try again','quiet')+'</div>','error');
    else {
      const needsUser=['account','request','request-status','moderation','review-request','editor','metadata-editor','publication-preview','admin'].includes(s.view);
      if(needsUser&&!user()&&Auth&&Auth.getUser()){
        main.innerHTML=D&&D.loading?progress('Loading your account'):pagehead('Your account could not be loaded','')+banner(esc(D&&D.error||'The connection was interrupted. Refresh this page to reconnect to your account.')+'<div class="dkr-spaced">'+button('refresh-page','Reconnect','quiet')+'</div>','error');
      } else {
        if(needsUser&&!user()){s.returnTo={view:s.view,requestId:s.requestId};s.loginMode=['moderation','review-request','editor','metadata-editor','publication-preview','admin'].includes(s.view)?'staff':'player';s.view='login';}
        const pages={store:storePage,detail:detailPage,login:loginPage,denied:()=>deniedPage(false),request:requestPage,account:accountPage,'request-status':requestStatusPage,moderation:moderationPage,'review-request':reviewRequestPage,editor:editorPage,'metadata-editor':metadataPage,'publication-preview':publicationPreviewPage,admin:adminPage,help:helpPage};
        main.innerHTML=(pages[s.view]||storePage)();
      }
    }
    if(Auth&&Auth.signoutPending&&!Auth.completionWindow){main.insertAdjacentHTML('afterbegin',banner(esc(Auth.signoutWarning||'Sign-out could not be confirmed. Reconnect and finish signing out to remove remembered access from this device.')+'<div class="dkr-spaced">'+button('retry-signout','Finish signing out','quiet')+'</div>','error'));}
    renderIcons();applyBusy();if(focus)focusPage();
  }
  async function go(view,data,refresh) {
    if(view!==s.view){captureListingForm();if(Uploads&&Uploads.state.running&&uploadMatchesContext(uploadTarget,false))Uploads.pause();s.uploadOpenKind='';s.uploadJobsShown=false;s.uploadError='';s.uploadConnection='';}
    const seq=++navigation;
    if(view!==s.view){s.filePickerKind='';s.filePickerSearch='';s.filePickerError='';}
    Object.assign(s,{view,pageError:'',pageLoading:false,reportId:null},data||{});clearFlash();
    const privatePage=['account','request','request-status','moderation','review-request','admin'].includes(view);
    if(privatePage&&!requireSignedIn({view,requestId:s.requestId},['moderation','review-request','admin'].includes(view)))return;
    if(!D){render(true);return;}
    if(view==='detail'||privatePage&&refresh!==false){
      s.pageLoading=true;render(true);
      try {
        if(view==='detail'){s.reviewError='';try{await D.loadReviews(s.itemId);}catch(error){if(seq===navigation)s.reviewError=errorMessage(error);}}
        else await D.refresh();
      } catch(error){if(seq===navigation)s.pageError=errorMessage(error);}
      finally {if(seq===navigation){s.pageLoading=false;render(true);}}
    } else render(true);
  }
  async function waitForStore() {
    if(!D||!D.loading)return;
    await new Promise((resolve,reject)=>{
      let unsubscribe=()=>{};
      const timeout=setTimeout(()=>{unsubscribe();reject(new Error('Your sign-in finished, but the library account could not be loaded. Please try opening My account again.'));},30000);
      unsubscribe=D.onChange(()=>{if(!D.loading){clearTimeout(timeout);unsubscribe();resolve();}});
      if(!D.loading){clearTimeout(timeout);unsubscribe();resolve();}
    });
  }
  async function signIn() {
    if(authPending||!Auth)return;
    if(user()){await go(s.loginMode==='staff'?'moderation':'account');return;}
    const dest=s.returnTo||(s.loginMode==='staff'?{view:'moderation'}:{view:'account'});
    loginDestination=dest;authPending=true;render();
    try {
      await Auth.login({remember:s.rememberSignIn});await waitForStore();
      authPending=false;loginDestination=null;
      if(!user())throw new Error(D&&D.error?errorMessage(D.error):'Discord sign-in finished, but the library account is unavailable. Try again.');
      await go(dest.view,Object.assign({},dest),false);
      if(Auth.persistenceWarning)flash(Auth.persistenceWarning,true);
    } catch(error){authPending=false;loginDestination=null;s.view='login';s.pageLoading=false;render();flash(errorMessage(error),true);}
  }

  root.addEventListener('error',event=>{const img=event.target;if(img&&img.tagName==='IMG'&&!img.dataset.fallback){img.dataset.fallback='true';img.src=A.pattern;}},true);
  root.addEventListener('keydown',event=>{if(event.target.id==='dkr-hosted-search'&&event.key==='Enter')event.preventDefault();});
  root.addEventListener('click',async event=>{
    const target=event.target.closest('[data-action]');if(!target||!root.contains(target))return;
    const action=target.dataset.action,id=target.dataset.id;
    if(action==='cancel-login'){if(Auth)Auth.cancelLogin();return;}
    if(action==='close-window'){window.close();return;}
    if(pendingWrite)return;
    try {
      if(action==='store'){await go('store');return;}
      if(action==='category'){s.category=id;s.tag='all';s.limit=6;s.view='store';clearFlash();render();restoreFocus(action,id);return;}
      if(action==='tag'){s.tag=id;s.limit=6;clearFlash();render();restoreFocus(action,id);return;}
      if(action==='clear-filters'){s.category='all';s.tag='all';s.search='';s.limit=6;render(true);return;}
      if(action==='more'){const first=filteredItems()[s.limit];s.limit+=6;$('#dkr-catalogue-results').innerHTML=results();renderIcons();if(first)restoreFocus('detail',first.id);return;}
      if(action==='detail'||action==='reviews'||action==='install'){await go('detail',{itemId:id||s.itemId,detailTab:action==='reviews'?'reviews':action==='install'?'install':'overview'});return;}
      if(action==='detail-tab'){s.detailTab=id;clearFlash();render();restoreFocus(action,id);return;}
      if(action==='reload-reviews'||action==='more-reviews'){await perform(null,async epoch=>{const itemId=id||s.itemId;await D.loadReviews(itemId,action==='more-reviews'?D.reviewCursors[itemId]:undefined);if(epoch!==privateEpoch)return;s.reviewError='';render();});return;}
      if(action==='help'){await go('help');return;}
      if(action==='login'||action==='staff-login'){await go('login',{loginMode:action==='staff-login'?'staff':'player',returnTo:action==='staff-login'?{view:'moderation'}:null});return;}
      if(action==='discord-login'){await signIn();return;}
      if(action==='open-hosted-files'){await openHostedFilePicker(id);return;}
      if(action==='close-hosted-files'){captureListingForm();s.filePickerKind='';s.filePickerSearch='';render();restoreFocus('open-hosted-files',id);return;}
      if(action==='select-hosted-file'){selectHostedFile(id,target.dataset.kind);return;}
      if(action==='open-upload'||action==='reload-upload-settings'){await openUpload(id,action==='reload-upload-settings');return;}
      if(action==='close-upload'){captureListingForm();s.uploadOpenKind='';render();restoreFocus('open-upload',id);return;}
      if(action==='start-upload'){
        if(!Uploads||!isStaff()||!captureListingForm())return;
        const context=uploadContext(id);uploadTarget=context;s.uploadError='';
        try{await Uploads.start();}catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
        if(uploadMatchesContext(context,true))redrawUploadPanels();return;
      }
      if(action==='pause-upload'){if(Uploads){captureListingForm();Uploads.pause();}return;}
      if(action==='cancel-upload'){
        if(!Uploads||!isStaff())return;
        const context=uploadContext(id);captureListingForm();s.uploadError='';uploadTarget=Object.assign({},context,{cancelledIntent:true});
        try{await Uploads.cancel();}catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
        if(uploadMatchesContext(context,true))redrawUploadPanels();return;
      }
      if(action==='load-upload-jobs'){
        if(!Uploads||!isStaff()||!captureListingForm())return;
        const context=uploadContext(id);s.uploadError='';s.uploadJobsShown=true;
        try{await Uploads.loadJobs();}catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
        if(uploadMatchesContext(context,true))redrawUploadPanels();return;
      }
      if(action==='select-upload-job'){
        if(!Uploads||!isStaff())return;
        const draft=captureListingForm();if(!draft)return;
        const record=Uploads.jobs.find(row=>row.id===id&&row.category===draft.category&&row.kind===s.uploadOpenKind);
        if(!record)throw new Error('Refresh the saved upload list and choose a file in this category.');
        uploadTarget=uploadContext(record.kind);s.uploadError='';uploadAppliedId=record.state==='completed'?record.id:'';
        Uploads.selectJob(record);
        if(record.state==='completed')applyUploadedAsset(record.asset);
        else if(record.state==='cancel_pending'){try{await Uploads.cancel();}catch(error){if(uploadMatchesContext(uploadTarget,true))s.uploadError=errorMessage(error);}}
        redrawUploadPanels();return;
      }
      if(action==='use-uploaded-file'){
        if(!Uploads)return;
        const state=Uploads.state;if(!state.asset||!state.job)throw new Error('Wait for the upload to finish.');
        uploadTarget=uploadContext(id);uploadAppliedId=state.job.id;applyUploadedAsset(state.asset);return;
      }
      if(action==='check-upload-connection'){
        if(!Uploads||!isAdmin())throw new Error('Administrator access is required to check the upload connection.');
        const context=uploadContext(id);captureListingForm();s.uploadError='';s.uploadConnection='';
        try{const result=await Uploads.checkConnection();if(uploadMatchesContext(context,true))s.uploadConnection=result.message||'The encrypted connection and directory listing succeeded. Upload a file to verify write and resume permissions.';}
        catch(error){if(uploadMatchesContext(context,true))s.uploadError=errorMessage(error);}
        if(uploadMatchesContext(context,true))redrawUploadPanels();return;
      }
      if(action==='login-for-review'){await go('login',{loginMode:'player',returnTo:{view:'detail',itemId:id,detailTab:'reviews'}});return;}
      if(action==='request'){await go('request');return;}
      if(action==='account'){await go('account');return;}
      if(action==='account-tab'){s.accountTab=id;s.deleteOpen=false;clearFlash();render();restoreFocus(action,id);return;}
      if(action==='show-delete'){s.deleteOpen=true;render();const input=$('[data-form="delete-account"] input');if(input)input.focus();return;}
      if(action==='cancel-delete'){s.deleteOpen=false;render();restoreFocus('show-delete');return;}
      if(action==='signout'||action==='retry-signout'){await perform(null,async()=>{let failure=null;try{await Auth.logout();}catch(error){failure=error;}finally{clearPrivateState();render(true);}flash(failure?errorMessage(failure):'You have signed out.',!!failure);});return;}
      if(action==='request-status'){await go('request-status',{requestId:id});return;}
      if(action==='report-review'){if(!requireSignedIn({view:'detail',itemId:s.itemId,detailTab:'reviews',reportId:id}))return;s.reportId=id;render();const field=$('[data-form="report"] select');if(field)field.focus();return;}
      if(action==='cancel-report'){s.reportId=null;render();return;}
      if(action==='moderation'){await go('moderation');return;}
      if(action==='queue-tab'){s.queueTab=id;clearFlash();render();restoreFocus(action,id);return;}
      if(action==='review-request'){await go('review-request',{requestId:id});return;}
      if(action==='start-review'){await perform(null,async epoch=>{await D.startReview(id,Number(target.dataset.revision));if(epoch!==privateEpoch)return;clearDecisionDrafts(id);render();flash('Request is now in review. Complete the checks before approving it.');});return;}
      if(action==='prepare-request'){if(!isStaff())throw new Error('Moderator access is required.');await prepareEditor('request',id);return;}
      if(action==='restore-review'){await perform(null,async epoch=>{await D.resolveReport({reportId:id,action:'restore',reason:'Restored after moderator review.',expectedRevision:Number(target.dataset.revision),expectedReviewRevision:Number(target.dataset.reviewRevision)});if(epoch!==privateEpoch)return;render();flash('Review restored. It is included in the public rating again.');});return;}
      if(action==='admin'){await go('admin');return;}
      if(action==='admin-tab'){if(!isStaff()||(id!=='catalogue'&&!isAdmin()))throw new Error('Administrator access is required for this section.');s.adminTab=id;clearFlash();render();restoreFocus(action,id);return;}
      if(action==='admin-more'){s.adminLimit+=12;render();return;}
      if(action==='edit-details'){if(!isStaff())throw new Error('Moderator access is required.');const item=D.item(id);if(!item)throw new Error('This listing is unavailable. Refresh the library manager.');resetFilePicker();s.metadataDraft={itemId:item.id,targetId:item.id,expectedRevision:item.revision,title:item.title,author:item.author,category:item.category,summary:item.summary,image:item.image||'pattern',tags:list(item.tags).slice(),related:list(item.related).slice(),featured:!!item.featured};await go('metadata-editor',{},false);return;}
      if(action==='add-listing'||action==='edit-listing'){if(!isStaff())throw new Error('Moderator access is required.');await prepareEditor('admin',id);return;}
      if(action==='open-draft'){if(!isStaff())throw new Error('Moderator access is required.');const draft=list(D.catalogueDrafts).find(d=>d.id===id&&d.status==='draft');if(!draft)throw new Error('This draft is no longer available. Refresh the library manager.');await prepareEditor('admin',null,draft);return;}
      if(action==='back-editor'){await go('editor',{},false);return;}
      if(action==='archive-listing'||action==='restore-listing'){await perform(null,async epoch=>{await D.archiveCatalogueItem(id,action==='archive-listing',Number(target.dataset.revision));if(epoch!==privateEpoch)return;render();flash(action==='archive-listing'?'Listing archived. You can restore it from the library manager.':'Listing restored to the public library.');});return;}
      if(action==='publish'){
        const p=s.pendingPublication;if(!p)throw new Error('Prepare a publication first.');
        await perform(null,async epoch=>{
          let item;
          if(p.mode==='request')item=await D.publishRequest(p.requestId,p.draft);
          else {
            if(!isAdmin())throw new Error('Administrator access is required.');
            if(!p.savedDraftId){
              const originalRevision=Number(p.draft.revision)||0;
              const draftId=await D.saveCatalogueDraft(p.draft);if(epoch!==privateEpoch)return;
              const saved=list(D.catalogueDrafts).find(d=>d.id===draftId);
              p.savedDraftId=draftId;p.savedRevision=saved?saved.revision:originalRevision+1;
              p.draft=Object.assign({},p.draft,{id:draftId,revision:p.savedRevision});s.editorDraft=Object.assign({},p.draft);
              if(D.error)throw new Error(D.error+' Use Refresh to reconnect before publishing.');
            }
            item=await D.publishCatalogueDraft(p.savedDraftId,p.savedRevision);
          }
          if(epoch!==privateEpoch)return;
          s.pendingPublication=null;s.editorDraft=null;await go('detail',{itemId:item.id,detailTab:'overview'});if(epoch!==privateEpoch)return;
          flash('Published. This release is now available in the library.');
        });return;
      }
      if(action==='retry'){await perform(null,async epoch=>{if(!D)throw new Error('The library scripts could not be loaded. Reload this page.');await D.refresh();if(epoch!==privateEpoch)return;if(!D.configured||D.error)throw new Error(D.error||'The library connection has not been configured.');bootError='';render();});return;}
      if(action==='refresh-page'){await perform(null,async epoch=>{const view=s.view,itemId=s.itemId;await D.refresh();if(epoch!==privateEpoch)return;if(view==='detail'&&itemId){await D.loadReviews(itemId);if(epoch!==privateEpoch)return;s.reviewError='';}if(D.error)throw new Error(D.error);bootError='';s.pageError='';render();flash('Up to date.');});return;}
      if(action==='retry-page'){await go(s.view,{},true);return;}
    } catch(error){flash(errorMessage(error),true);}
  });
  root.addEventListener('input',event=>{
    const el=event.target;if(pendingWrite)return;
    if(el.id==='dkr-remember-sign-in'){s.rememberSignIn=el.checked;return;}
    if(el.id==='dkr-hosted-search'){s.filePickerSearch=el.value;const results=$('#dkr-hosted-results'),draft=s.view==='editor'?s.editorDraft:s.metadataDraft;if(results&&draft){results.innerHTML=filePickerRows(s.filePickerKind,draft);renderIcons();}return;}
    if(el.id==='dkr-mod-search'){s.search=el.value;s.limit=6;const resultsNode=$('#dkr-catalogue-results');if(resultsNode){resultsNode.innerHTML=results();renderIcons();}return;}
    const form=el.closest('form[data-form]');if(!form||!el.name)return;
    const value=el.type==='checkbox'?el.checked:el.value;
    if(el.type==='radio'&&!el.checked)return;
    if(form.dataset.form==='request')s.requestDraft[el.name]=value;
    if(['publication','metadata'].includes(form.dataset.form)){const draft=form.dataset.form==='publication'?s.editorDraft:s.metadataDraft;if(draft){if(el.name==='related')draft.related=Array.from(form.querySelectorAll('[name="related"]:checked')).map(node=>node.value);else draft[el.name]=value;}}
    if(form.dataset.form==='review'&&user()){const id=form.dataset.itemId,key=user().id+':'+id,base=D.ownReview(id)||{score:null,text:'',version:'Unknown / not specified'};const draft=s.reviewDrafts[key]||Object.assign({},base);draft[el.name]=el.name==='score'?Number(value):value;s.reviewDrafts[key]=draft;}
    if(form.dataset.form==='decision'&&user()){const id=form.dataset.requestId,key=user().id+':'+id;const draft=s.decisionDrafts[key]||{message:'',checks:Object.assign({},D.request(id).checks),revision:form.dataset.revision};if(el.type==='checkbox')draft.checks[el.name]=el.checked;else draft[el.name]=value;s.decisionDrafts[key]=draft;}
    if(form.dataset.form==='resubmit'){const id=form.dataset.requestId;const draft=s.resubmitDrafts[id]||Object.assign({},D.request(id));draft[el.name]=value;s.resubmitDrafts[id]=draft;}
    if(form.dataset.form==='staff-note')s.noteDrafts[form.dataset.requestId]=String(value);
  });
  root.addEventListener('change',event=>{
    const el=event.target;if(pendingWrite)return;
    if(el.dataset&&el.dataset.uploadFile){const file=el.files&&el.files[0];if(file)chooseUploadFile(file,el.dataset.uploadFile);return;}
    if(el.id==='dkr-remember-sign-in'){s.rememberSignIn=el.checked;return;}
    if(el.id==='dkr-mod-sort'){s.sort=el.value;$('#dkr-catalogue-results').innerHTML=results();renderIcons();return;}
    if(el.id==='dkr-queue-filter'){s.queueFilter=el.value;render();queueMicrotask(()=>{const node=$('#dkr-queue-filter');if(node)node.focus({preventScroll:true});});return;}
    const form=el.closest('form[data-form]');if(!form)return;
    if(form.dataset.form==='publication'&&el.name==='category'){const draft=captureListingForm();if(draft)clearMismatchedHostedFiles(draft);resetUploadContext();render();const input=$('[name="category"]');if(input)input.focus({preventScroll:true});return;}
    if(form.dataset.form==='request'&&el.name==='targetId'&&el.value){const m=D.item(el.value);if(m){s.requestDraft=Object.assign({},s.requestDraft,formData(form),{relation:'update',targetId:m.id,category:m.category});s.requestDraft.credit=!!form.elements.credit.checked;render();}}
    if(form.dataset.form==='publication'&&el.name==='targetId'&&el.value){const m=D.item(el.value);if(m){s.editorDraft=Object.assign({},listingFormData(form,s.editorDraft),{target:'existing',targetId:m.id,targetRevision:m.revision,title:m.title,author:m.author,category:m.category,summary:m.summary,image:m.image||'pattern',tags:list(m.tags).slice(),related:list(m.related).slice(),featured:!!m.featured});clearMismatchedHostedFiles(s.editorDraft);s.filePickerKind='';s.hostedSelections.image=null;resetUploadContext();render();}}
    if(form.dataset.form==='publication'&&el.name==='target'){captureListingForm();resetUploadContext();render();return;}
    if(['publication','metadata'].includes(form.dataset.form)&&el.name==='imageChoice'){const input=form.querySelector('[name="imageUrl"]');if(input){input.required=el.value==='custom';input.disabled=el.value!=='custom';}}
  });
  root.addEventListener('submit',async event=>{
    const form=event.target.closest('form[data-form]');if(!form||!root.contains(form))return;
    event.preventDefault();if(pendingWrite)return;
    const kind=form.dataset.form,data=formData(form),choice=event.submitter?event.submitter.value:'save';
    if(['publication','metadata'].includes(kind)&&choice!=='draft'&&unfinishedUploadForCurrentForm()){formError(form,new Error('Finish or cancel this form’s upload before previewing or saving the public listing.'));return;}
    if(!(event.submitter&&event.submitter.formNoValidate)&&!form.reportValidity())return;
    const current=user();if(!current){requireSignedIn({view:s.view,itemId:s.itemId,requestId:s.requestId});return;}
    const currentId=String(current.id);
    // Snapshot successful controls before the pending state disables the form.
    const listingDraft=['publication','metadata'].includes(kind)?listingFormData(form,kind==='publication'?s.editorDraft:s.metadataDraft):null;
    await perform(form,async epoch=>{
      if(kind==='review'){const id=form.dataset.itemId,existing=D.ownReview(id);await D.saveReview({itemId:id,score:Number(data.score),text:data.text,version:data.version});if(epoch!==privateEpoch)return;delete s.reviewDrafts[currentId+':'+id];render();flash(existing?'Your review was updated. It replaces your previous rating.':'Your review was added. Thank you for sharing your experience.');return;}
      if(kind==='report'){await D.reportReview({reviewId:form.dataset.reviewId,reason:data.reason});if(epoch!==privateEpoch)return;s.reportId=null;render();flash('Your report was sent to the moderation team. The review remains visible while it is assessed.');return;}
      if(kind==='request'){data.credit=!!form.elements.credit.checked;const request=await D.submitRequest(data);if(epoch!==privateEpoch)return;s.requestDraft={};await go('request-status',{requestId:request.id},false);if(epoch!==privateEpoch)return;flash('Your request was sent for review. It has not been added to the public library.');return;}
      if(kind==='resubmit'){const id=form.dataset.requestId;data.expectedRevision=Number(form.dataset.revision);await D.resubmitRequest(id,data);if(epoch!==privateEpoch)return;delete s.resubmitDrafts[id];clearDecisionDrafts(id);render();flash('Your revised request is back in the moderation queue.');return;}
      if(kind==='staff-note'){const id=form.dataset.requestId;await D.addStaffNote(id,data.note,Number(form.dataset.revision));if(epoch!==privateEpoch)return;delete s.noteDrafts[id];const draft=s.decisionDrafts[currentId+':'+id];if(draft&&D.request(id))draft.revision=D.request(id).revision;render();flash('Private staff note saved.');return;}
      if(kind==='decision'){const id=form.dataset.requestId;const request=await D.decideRequest(id,{decision:choice,message:data.message,expectedRevision:Number(form.dataset.revision),checks:{source:data.source==='on',files:data.files==='on',requirements:data.requirements==='on',credits:data.credits==='on'}});if(epoch!==privateEpoch)return;clearDecisionDrafts(id);render();flash(request.status==='approved'?'Approved and awaiting publication. Prepare the public listing when ready.':request.status==='changes'?'The requester can now see your questions and resubmit.':'Request rejected. The requester can see the reason.');return;}
      if(kind==='report-decision'){await D.resolveReport({reportId:form.dataset.reportId,action:choice,reason:data.reason,expectedRevision:Number(form.dataset.revision),expectedReviewRevision:Number(form.dataset.reviewRevision)});if(epoch!==privateEpoch)return;render();flash(choice==='hide'?'Review hidden with a recorded reason. The public rating has been recalculated.':'Report dismissed. The review’s visibility is unchanged.');return;}
      if(kind==='staff-role'){await D.setStaffRole(form.dataset.accountId,data.role);if(epoch!==privateEpoch)return;await waitForStore();if(epoch!==privateEpoch)return;render();flash('Website role updated.');return;}
      if(kind==='profile'){await D.updateProfile({displayName:data.displayName});if(epoch!==privateEpoch)return;await waitForStore();if(epoch!==privateEpoch)return;render();flash('Your public name was updated.');return;}
      if(kind==='delete-account'){if(data.confirmation!=='DELETE'||!form.elements.understood.checked)throw new Error('Type DELETE and confirm that you want to remove your account.');await D.deleteAccount({confirmation:'DELETE'});if(Auth.getUser())return;clearPrivateState();render(true);flash('Your library account has been deleted.');return;}
      if(kind==='metadata'){if(!isStaff())throw new Error('Moderator access is required.');const draft=listingDraft;s.metadataDraft=draft;validateListingChoices(draft);const item=await D.updateCatalogueItem(form.dataset.itemId,{expectedRevision:Number(form.dataset.revision),title:draft.title,author:draft.author,category:draft.category,summary:draft.summary,image:draft.image,tags:draft.tags,related:draft.related,featured:draft.featured});if(epoch!==privateEpoch)return;s.metadataDraft=null;await go('detail',{itemId:item.id,detailTab:'overview'});if(epoch!==privateEpoch)return;flash('Public listing details updated.');return;}
      if(kind==='publication'){
        const draft=listingDraft;
        s.editorDraft=draft;
        validateListingChoices(draft,choice!=='draft');
        if(choice==='draft'){if(!isStaff())throw new Error('Moderator access is required.');await D.saveCatalogueDraft(draft);if(epoch!==privateEpoch)return;await go('admin',{adminTab:'catalogue'},false);if(epoch!==privateEpoch)return;flash('Draft saved. The public catalogue is unchanged.');return;}
        draft.fileUrl=validateFileAddress(draft.fileUrl,'download');
        if(draft.target==='existing'){const item=D.item(draft.targetId);if(!item)throw new Error('Choose the existing listing for this release.');if(item.category!==draft.category)throw new Error('An updated release must use the existing listing’s category.');}
        if(s.editorMode==='request'){const request=D.request(s.editorRequestId);if(!isStaff()||!request||request.status!=='approved')throw new Error('An approved request and moderator access are required.');if(request.category!==draft.category)throw new Error('Use the category approved for this request.');if(request.relation==='update'&&(draft.target!=='existing'||draft.targetId!==request.targetId))throw new Error('This update must target its original listing.');draft.expectedRevision=draft.requestRevision;}
        else if(!isStaff())throw new Error('Moderator access is required.');
        s.pendingPublication={mode:s.editorMode,requestId:s.editorRequestId,draft:Object.assign({},draft)};
        await go('publication-preview',{},false);return;
      }
    });
  });

  async function initialise() {
    try {
      if(!Auth)throw new Error('The Discord sign-in script could not be loaded. Reload this page or contact the site owner.');
      if(typeof window.createLibraryAPI!=='function')throw new Error('The library connection script could not be loaded. Reload this page or contact the site owner.');
      await Auth.ready;
      authIdentity=identity(Auth.getUser());
      {const u=Auth.getUser();authAuthority=u?JSON.stringify([u.id,u.role,u.status]):null;}
      if(Auth.completionWindow){bootLoading=false;render();return;}
      D=window.createLibraryAPI(C);
      D.onChange(()=>{
        if(!started)return;
        const role=user()?user().role:'';
        if(!D.loading&&D.configured&&!D.error&&bootError){bootError='';if(!pendingWrite&&!authPending)render();}
        if(role!==serverRole){if(!D.loading&&role&&serverRole)clearPrivateState();serverRole=role;if(!pendingWrite&&!authPending)render();}
        if(!D.loading&&!pendingWrite&&!authPending&&s.view==='login'&&user())render();
      });
      Auth.onChange(nextUser=>{
        const next=identity(nextUser);
        const authority=nextUser?JSON.stringify([nextUser.id,nextUser.role,nextUser.status]):null;
        if(next!==authIdentity||authority!==authAuthority){const ended=authIdentity!==null&&next===null;authIdentity=next;authAuthority=authority;const destination=loginDestination;clearPrivateState();if(authPending){s.view='login';s.returnTo=destination;}if(started)render();if(ended)flash('Your sign-in has ended. Sign in again before sending another change.');}
      });
      if(Uploads)Uploads.onChange(uploadsChanged);
      await D.ready;
      bootError=D.configured?(D.error?errorMessage(D.error):''):'The site owner needs to finish connecting this website to its library service.';
      serverRole=user()?user().role:'';
    } catch(error){bootError=errorMessage(error);}
    finally {started=true;bootLoading=false;render();}
  }
  initialise();
}());
