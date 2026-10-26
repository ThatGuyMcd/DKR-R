// Additive launcher metadata endpoints. Existing browser endpoints, sessions,
// publication transactions and the supplied FTPS implementation are preserved.
let launcherStorageReady;
async function launcherStorage(env){
  if(!launcherStorageReady)launcherStorageReady=env.DB.batch([
    env.DB.prepare(`CREATE TABLE IF NOT EXISTS launcher_flows(flow_hash TEXT PRIMARY KEY,client_challenge TEXT NOT NULL,client_state TEXT NOT NULL UNIQUE,created_at INTEGER NOT NULL,expires_at INTEGER NOT NULL,last_poll INTEGER NOT NULL DEFAULT 0,grant_cipher TEXT,error TEXT NOT NULL DEFAULT '',consumed INTEGER NOT NULL DEFAULT 0 CHECK(consumed IN(0,1)))`),
    env.DB.prepare('CREATE INDEX IF NOT EXISTS launcher_flows_expiry ON launcher_flows(expires_at)'),
    env.DB.prepare(`CREATE TABLE IF NOT EXISTS launcher_release_integrity(release_id TEXT PRIMARY KEY REFERENCES library_releases(id) ON DELETE CASCADE,sha256 TEXT NOT NULL CHECK(length(sha256)=64),bytes INTEGER NOT NULL CHECK(bytes>0 AND bytes<=2147483648),verified_at INTEGER NOT NULL)`),
    env.DB.prepare(`CREATE TABLE IF NOT EXISTS launcher_asset_integrity(file_url TEXT PRIMARY KEY,sha256 TEXT NOT NULL CHECK(length(sha256)=64),bytes INTEGER NOT NULL CHECK(bytes>0 AND bytes<=2147483648),verified_at INTEGER NOT NULL)`)
  ]).then(async()=>{
    if(launcherPinnedBackfill.length)await env.DB.batch(launcherPinnedBackfill.map(row=>env.DB.prepare(`INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at) SELECT id,?1,?2,?3 FROM library_releases WHERE id=?4 AND file_url=?5 AND version=?6 ON CONFLICT(release_id) DO NOTHING`).bind(row.sha256,row.bytes,row.verifiedAt,row.id,row.url,row.version)));
    await env.DB.prepare(`INSERT INTO launcher_asset_integrity(file_url,sha256,bytes,verified_at) SELECT r.file_url,v.sha256,v.bytes,v.verified_at FROM launcher_release_integrity v JOIN library_releases r ON r.id=v.release_id WHERE 1 ON CONFLICT(file_url) DO NOTHING`).run();
  }).catch(error=>{launcherStorageReady=null;throw error;});
  await launcherStorageReady;
}
async function handleLauncher(request, env) {
  const url=new URL(request.url);
  if(url.pathname==='/v1/launcher/capabilities'&&request.method==='GET')
    return __dkrCommon.json({schema:1,launcher:true,auth:'pkce-pairing',integrity:true,publishingIntegrity:true,revision:'20261018-beta8-polish'});
  if(url.pathname==='/v1/catalogue-tools'&&request.method==='GET'){
    const user=await __dkrAuth.requireUser(request,env);__dkrCommon.requireRole(user,'moderator');
    const cursor=Number(url.searchParams.get('cursor')||0);
    if(!Number.isSafeInteger(cursor)||cursor<0||cursor>10000)throw new __dkrCommon.HttpError(400,'Invalid page cursor.');
    const rows=(await env.DB.prepare('SELECT * FROM library_catalogue_drafts ORDER BY updated_at DESC,id DESC LIMIT 41 OFFSET ?1').bind(cursor).all()).results;
    const drafts=rows.slice(0,40).map(row=>({...JSON.parse(row.content_json),id:row.id,revision:row.revision,status:row.status}));
    return __dkrCommon.json({accounts:[],drafts,audit:[],items:[],nextCursor:rows.length>40?String(cursor+40):null});
  }
  const match=/^\/v1\/launcher\/releases\/(release_[A-Za-z0-9_-]{1,100})$/.exec(url.pathname);
  if(match&&request.method==='POST'){
    const user=await __dkrAuth.requireUser(request,env);__dkrCommon.requireRole(user,'moderator');
    const data=await __dkrCommon.readJSON(request,2048);
    if(typeof data.sha256!=='string'||!/^[a-f0-9]{64}$/.test(data.sha256)||!Number.isSafeInteger(data.bytes)||data.bytes<1||data.bytes>2147483648||!Number.isSafeInteger(data.expectedRevision))throw new __dkrCommon.HttpError(400,'Invalid release verification.');
    const row=await env.DB.prepare('SELECT r.id,r.file_url,i.id AS item_id,i.revision FROM library_releases r JOIN library_items i ON i.id=r.item_id WHERE r.id=?1 AND i.archived=0').bind(match[1]).first();
    if(!row)throw new __dkrCommon.HttpError(404,'Release not found.');
    if(row.revision!==data.expectedRevision)throw new __dkrCommon.HttpError(409,'This listing changed. Refresh before publishing its download verification.','stale_record');
    await env.DB.batch([
      env.DB.prepare(`INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at)
        VALUES(?1,?2,?3,?4) ON CONFLICT(release_id) DO NOTHING`).bind(row.id,data.sha256,data.bytes,__dkrCommon.now()),
      env.DB.prepare(`INSERT INTO launcher_asset_integrity(file_url,sha256,bytes,verified_at) SELECT ?1,?2,?3,?4 WHERE EXISTS(SELECT 1 FROM launcher_release_integrity WHERE release_id=?5 AND sha256=?2 AND bytes=?3) ON CONFLICT(file_url) DO NOTHING`).bind(row.file_url,data.sha256,data.bytes,__dkrCommon.now(),row.id),
      env.DB.prepare('INSERT INTO library_audit(id,actor_id,actor_name,action,target_id,message,created_at) VALUES(?1,?2,?3,?4,?5,?6,?7)').bind('audit_'+__dkrCommon.randomToken(18),user.id,user.displayName,'Release download verified',row.id,'Full-file SHA-256 registered by the publisher.',__dkrCommon.now())
    ]);
    const saved=await env.DB.prepare('SELECT sha256,bytes FROM launcher_release_integrity WHERE release_id=?1').bind(row.id).first();
    if(saved.sha256!==data.sha256||saved.bytes!==data.bytes)throw new __dkrCommon.HttpError(409,'This release already has different file bytes. Publish a new release instead of replacing it.','release_immutable');
    return __dkrCommon.json({ok:true});
  }
  if(match&&request.method==='GET'){
    const row=await env.DB.prepare(`SELECT r.id,r.file_url,r.file_name,r.format,i.category,
      v.sha256,v.bytes FROM library_releases r JOIN library_items i ON i.id=r.item_id
      LEFT JOIN launcher_release_integrity v ON v.release_id=r.id
      WHERE r.id=?1 AND i.archived=0`).bind(match[1]).first();
    if(!row)throw new __dkrCommon.HttpError(404,'This release is no longer available.','not_found');
    if(!row.sha256||!Number.isSafeInteger(row.bytes)||row.bytes<1)
      throw new __dkrCommon.HttpError(409,'This download is not ready yet. The library team needs to check its hosted file.','release_unverified');
    return __dkrCommon.json({id:row.id,url:row.file_url,fileName:row.file_name,format:row.format,
      category:row.category,sha256:row.sha256,bytes:row.bytes});
  }
  return null;
}
