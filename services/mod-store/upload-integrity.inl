// Full-file digests are registered by the same trusted staff who publish files.
// This never grants ordinary downloaders permission to change integrity data.
async function launcherUploadIntegrity(request, env) {
  const match=/^\/v1\/launcher\/uploads\/(up_[0-9a-f]{32})\/integrity$/.exec(new URL(request.url).pathname);
  if(!match)return null;
  if(request.method!=='POST')throw new HttpError(405,'Use POST for upload checksums.');
  requireOrigin(request,env);
  const user=await requireUser(request,env);requireRole(user,'moderator');
  const data=await readJSON(request,1024),db=uploadDatabase(env);
  if(typeof data.sha256!=='string'||!/^[0-9a-f]{64}$/.test(data.sha256)||!Number.isSafeInteger(data.bytes))
    throw new HttpError(400,'Invalid full-file checksum.');
  await uploadRecheckUser(request,env,user.id);
  const row=await uploadRow(db,match[1],user);
  if(row.state!=='completed'||row.kind!=='download'||row.total_size!==data.bytes)
    throw new HttpError(409,'Complete this exact package upload before registering its checksum.');
  // Recheck all confirmed chunk receipts. Resume fingerprints are not hashes.
  await uploadResumeManifest(db,row);
  const asset=uploadAsset(row,env),stamp=now();
  await db.prepare(`INSERT INTO launcher_asset_integrity(file_url,sha256,bytes,verified_at)
    VALUES(?1,?2,?3,?4) ON CONFLICT(file_url) DO NOTHING`).bind(asset.url,data.sha256,data.bytes,stamp).run();
  const saved=await db.prepare('SELECT sha256,bytes FROM launcher_asset_integrity WHERE file_url=?1').bind(asset.url).first();
  if(saved.sha256!==data.sha256||saved.bytes!==data.bytes)
    throw new HttpError(409,'This uploaded file already has a different checksum. Upload a new file.');
  return json({ok:true});
}

async function requirePreparedUpload(db,env,address){
  const file=decodeURIComponent(new URL(address).pathname.split('/').pop()),match=/^dkr-([0-9a-f]{32})-/.exec(file);
  if(!match)return; // Older separately hosted files use the existing backfill path.
  const row=await uploadFirst(db,'SELECT * FROM library_upload_jobs WHERE id=?',['up_'+match[1]]);
  if(!row)return;
  if(row.file_name!==file||row.state!=='completed'||row.kind!=='download'||uploadAsset(row,env).url!==address)
    throw new HttpError(409,'Finish this package upload before publishing it.');
  const proof=await db.prepare('SELECT bytes FROM launcher_asset_integrity WHERE file_url=?1').bind(address).first();
  if(!proof||proof.bytes!==row.total_size)
    throw new HttpError(409,'This upload is still being prepared for download. Wait for preparation to finish, then publish.');
}
