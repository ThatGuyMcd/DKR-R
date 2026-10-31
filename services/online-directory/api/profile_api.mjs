import { __dkrAuth, HttpError } from './library_api.mjs';
// DKRR_LAUNCHER_PROFILE_BEGIN
const launcherPictureOrigin = 'https://dkr-mod-library-api.ross-macgregorbars.workers.dev/v1/launcher/profile-picture/';
const launcherProfileHeaders = {'Cache-Control':'no-store','Access-Control-Allow-Origin':'https://positron-portal.com'};
const launcherHex = bytes => Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2,'0')).join('');
const launcherUnhex = value => Uint8Array.from(value.match(/../g) || [], pair => parseInt(pair,16));
const launcherSha = async bytes => launcherHex(await crypto.subtle.digest('SHA-256', bytes));
const launcherToken = () => btoa(String.fromCharCode(...crypto.getRandomValues(new Uint8Array(32)))).replaceAll('+','-').replaceAll('/','_').replace(/=+$/,'');
function launcherNeed(condition, status, code, message) { if (!condition) throw new HttpError(status,message,code); }
function launcherAvatarUrl(value) {
  return typeof value === 'string' && value.length <= 180 &&
    (/^https:\/\/cdn\.discordapp\.com\/(?:avatars\/\d{17,22}\/(?:a_)?[0-9a-f]{32}|embed\/avatars\/[0-5])\.png\?size=128$/.test(value) ||
     value.startsWith(launcherPictureOrigin) && /^[a-f0-9]{64}\.png$/.test(value.slice(launcherPictureOrigin.length)));
}
async function launcherProfileInput(request) {
  launcherNeed((request.headers.get('Content-Type') || '').split(';')[0].trim() === 'application/json',415,'profile_type','Use a JSON profile request.');
  const reader = request.body?.getReader(); let total = 0; const chunks = [];
  launcherNeed(reader,400,'profile_input','Profile details are missing.');
  try {
    for (;;) { const {done,value} = await reader.read(); if (done) break;
      total += value.byteLength; launcherNeed(total <= 192*1024,413,'profile_size','The profile picture is too large.'); chunks.push(value); }
  } finally { try { await reader.cancel(); } catch {} }
  const bytes = new Uint8Array(total); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk,offset); offset += chunk.byteLength; }
  let input; try { input = JSON.parse(new TextDecoder().decode(bytes)); } catch { throw new HttpError(400,'Invalid profile details.','profile_input'); }
  launcherNeed(input && typeof input === 'object' && !Array.isArray(input) && Object.keys(input).length <= 10,400,'profile_input','Invalid profile details.');
  return input;
}
async function launcherProfileLimit(env, account) {
  const now = Date.now();
  await env.DB.prepare(`INSERT INTO launcher_profile_limits VALUES(?1,?2,1) ON CONFLICT(account_id) DO UPDATE SET
    window=CASE WHEN window<=?3 THEN excluded.window ELSE window END,
    count=CASE WHEN window<=?3 THEN 1 ELSE count+1 END`).bind(account,now,now-60000).run();
  const row = await env.DB.prepare('SELECT count FROM launcher_profile_limits WHERE account_id=?1').bind(account).first();
  launcherNeed(row?.count <= 24,429,'profile_rate','Too many profile changes. Wait a minute before trying again.');
}
function launcherProfileMetadata(row) {
  return {publicKey:row?.public_key || '',avatarUrl:row?.avatar_url || '',source:row?.source || 'initials',sharing:row?.sharing !== 0};
}
async function launcherVerifiedSocial(env, account, publicKey) {
  if(typeof publicKey!=='string' || !/^[a-f0-9]{64}$/.test(publicKey))return null;
  try {
    const row = await env.DB.prepare('SELECT l.public_key,l.identity,l.friend_code,p.avatar_url,p.sharing FROM launcher_social_links l JOIN launcher_profiles p ON p.account_id=l.account_id WHERE l.account_id=?1 AND l.public_key=?2').bind(account,publicKey).first();
    if (!row) return null;
    return {publicKey:row.public_key,identity:row.identity,friendCode:row.friend_code,avatar:row.sharing && launcherAvatarUrl(row.avatar_url) ? row.avatar_url : ''};
  } catch { return null; } // A pending optional migration never prevents lobby admission.
}
function launcherCrc(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) { value ^= byte; for (let bit=0;bit<8;bit++) value = (value>>>1)^((value&1)?0xedb88320:0); }
  return (value^0xffffffff)>>>0;
}
async function launcherValidatePng(bytes) {
  const invalid = () => new HttpError(400,'The picture must be a prepared 128 x 128 PNG.','profile_picture');
  if (bytes.length < 57 || bytes.length > 96*1024 || launcherHex(bytes.slice(0,8)) !== '89504e470d0a1a0a') throw invalid();
  const view = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  let offset=8, count=0, header=false, ended=false, idatEnded=false, compressedLength=0; const compressed=[];
  while (offset < bytes.length) {
    if (++count > 32 || offset+12 > bytes.length) throw invalid();
    const length=view.getUint32(offset), end=offset+12+length;
    if (end > bytes.length) throw invalid();
    const kind=String.fromCharCode(...bytes.slice(offset+4,offset+8));
    if (launcherCrc(bytes.slice(offset+4,offset+8+length)) !== view.getUint32(offset+8+length)) throw invalid();
    if (kind === 'IHDR') {
      if (header || offset !== 8 || length !== 13 || view.getUint32(offset+8)!==128 || view.getUint32(offset+12)!==128 ||
          launcherHex(bytes.slice(offset+16,offset+21))!=='0806000000') throw invalid();
      header=true;
    } else if (kind === 'IDAT') {
      if (!header || idatEnded || ended) throw invalid();
      compressed.push(bytes.slice(offset+8,offset+8+length)); compressedLength+=length;
    } else if (kind === 'IEND') {
      if (!header || !compressedLength || length || ended || end !== bytes.length) throw invalid();
      ended=true; idatEnded=true;
    } else throw invalid(); // No metadata, animation or embedded ancillary content.
    offset=end;
  }
  if (!ended) throw invalid();
  const packed=new Uint8Array(compressedLength); let at=0;
  for(const chunk of compressed){packed.set(chunk,at);at+=chunk.length;}
  let reader;
  try {
    reader=new Blob([packed]).stream().pipeThrough(new DecompressionStream('deflate')).getReader();
    let produced=0; const expected=128*(128*4+1);
    for(;;){const {done,value}=await reader.read();if(done)break;
      if(produced+value.length>expected)throw invalid();
      for(let n=0;n<value.length;n++)if((produced+n)%(128*4+1)===0 && value[n]>4)throw invalid();
      produced+=value.length;
    }
    if(produced!==expected)throw invalid();
  } catch { throw invalid(); } finally { try{await reader?.cancel();}catch{} }
}
async function handleLauncherProfile(request, env) {
  const url=new URL(request.url), route=url.pathname;
  const picture=/^\/v1\/launcher\/profile-picture\/([a-f0-9]{64})\.png$/.exec(route);
  if (!picture && route !== '/v1/launcher/profile' && route !== '/v1/launcher/profile-challenge') return null;
  launcherNeed(url.protocol==='https:',400,'https','Use HTTPS.');
  if (picture) {
    if(request.method!=='GET')return new Response(null,{status:405,headers:{Allow:'GET'}});
    let row=await env.DB.prepare("SELECT p.account_id,p.picture,p.sharing FROM launcher_profiles p JOIN accounts a ON a.id=p.account_id WHERE p.picture_hash=?1 AND p.picture IS NOT NULL AND a.status='active' ORDER BY p.sharing DESC LIMIT 1").bind(picture[1]).first();
    launcherNeed(row,404,'picture_missing','This profile picture is no longer available.');
    if(!row.sharing){const user=await __dkrAuth.requireUser(request,env);row=await env.DB.prepare('SELECT account_id,picture,sharing FROM launcher_profiles WHERE account_id=?1 AND picture_hash=?2 AND picture IS NOT NULL').bind(user.id,picture[1]).first();launcherNeed(row,404,'picture_missing','This profile picture is no longer available.');}
    return new Response(new Uint8Array(row.picture),{headers:{'Content-Type':'image/png','X-Content-Type-Options':'nosniff',
      'Cache-Control':row.sharing?'public, max-age=300':'private, no-store','Access-Control-Allow-Origin':'https://positron-portal.com'}});
  }
  const user=await __dkrAuth.requireUser(request,env);
  if(request.method==='GET' && route==='/v1/launcher/profile') {
    const row=await env.DB.prepare('SELECT public_key,avatar_url,source,sharing FROM launcher_profiles WHERE account_id=?1').bind(user.id).first();
    const key=url.searchParams.get('key') || '';
    const link=/^[a-f0-9]{64}$/.test(key)?await env.DB.prepare('SELECT public_key FROM launcher_social_links WHERE account_id=?1 AND public_key=?2').bind(user.id,key).first():null;
    if(row)row.public_key=link?.public_key || '';
    return Response.json({accountId:user.id,...launcherProfileMetadata(row)},{headers:launcherProfileHeaders});
  }
  if(request.method!=='POST')return new Response(null,{status:405,headers:{Allow:route.endsWith('challenge')?'POST':'GET, POST'}});
  await launcherProfileLimit(env,user.id);
  const input=await launcherProfileInput(request);
  launcherNeed(typeof input.publicKey==='string' && /^[a-f0-9]{64}$/.test(input.publicKey) && !/^0+$/.test(input.publicKey),400,'profile_key','Invalid profile key.');
  if(route.endsWith('challenge')) {
    const peer=await crypto.subtle.importKey('raw',launcherUnhex(input.publicKey),'X25519',false,[]);
    const pair=await crypto.subtle.generateKey('X25519',true,['deriveBits']);
    if (!('privateKey' in pair) || !('publicKey' in pair)) throw new HttpError(503,'Profile verification is temporarily unavailable.','profile_crypto');
    const agreement={name:'X25519',public:peer};
    let shared; try { shared=await crypto.subtle.deriveBits(agreement,pair.privateKey,256); }
    catch { throw new HttpError(400,'Invalid profile key.','profile_key'); }
    const id=launcherToken(), expires=Date.now()+30000;
    await env.DB.prepare('DELETE FROM launcher_profile_challenges WHERE expires<=?1').bind(Date.now()).run();
    await env.DB.prepare('INSERT INTO launcher_profile_challenges VALUES(?1,?2,?3,?4,?5)').bind(id,user.id,input.publicKey,await launcherSha(shared),expires).run();
    return Response.json({id,publicKey:launcherHex(await crypto.subtle.exportKey('raw',pair.publicKey)),expiresAt:expires},{headers:launcherProfileHeaders});
  }
  const previous=await env.DB.prepare('SELECT * FROM launcher_profiles WHERE account_id=?1').bind(user.id).first();
  const linked=await env.DB.prepare('SELECT public_key FROM launcher_social_links WHERE account_id=?1 AND public_key=?2').bind(user.id,input.publicKey).first();
  if(!linked) {
    launcherNeed(typeof input.challenge==='string' && /^[A-Za-z0-9_-]{43}$/.test(input.challenge) &&
      typeof input.proof==='string' && /^[a-f0-9]{64}$/.test(input.proof),400,'profile_proof','Confirm this launcher profile before saving it.');
    const challenge=await env.DB.prepare('DELETE FROM launcher_profile_challenges WHERE id=?1 AND account_id=?2 RETURNING *').bind(input.challenge,user.id).first();
    launcherNeed(challenge && challenge.expires>Date.now() && challenge.public_key===input.publicKey &&
      challenge.proof_hash===await launcherSha(launcherUnhex(input.proof)),403,'profile_proof','The profile confirmation expired or was invalid. Try saving again.');
  }
  launcherNeed(typeof input.identity==='string' && /^[A-Z2-9]{13}$/.test(input.identity) &&
    typeof input.friendCode==='string' && input.friendCode.length>=60 && input.friendCode.length<=100 && /^DKRR-[A-Z2-9-]+$/.test(input.friendCode),400,'profile_friend','Invalid lobby friend details.');
  launcherNeed(typeof input.sharing==='boolean',400,'profile_sharing','Choose whether to share your profile picture.');
  let source=input.source, avatar=previous?.avatar_url || '', hash=previous?.picture_hash || '', png=previous?.picture || null;
  launcherNeed(['keep','custom','discord','initials'].includes(source),400,'profile_source','Choose a profile picture source.');
  if(source==='custom') {
    launcherNeed(typeof input.picture==='string' && input.picture.length<=131072 && /^[A-Za-z0-9_-]+$/.test(input.picture),400,'profile_picture','Prepare your picture before saving.');
    const encoded=input.picture.replaceAll('-','+').replaceAll('_','/');
    let bytes;try{bytes=Uint8Array.from(atob(encoded),c=>c.charCodeAt(0));}catch{throw new HttpError(400,'Invalid profile picture.','profile_picture');}
    await launcherValidatePng(bytes); png=bytes.buffer; hash=await launcherSha(bytes); avatar=launcherPictureOrigin+hash+'.png';
  } else if(source==='discord') {
    const discord=await env.DB.prepare('SELECT avatar_url FROM launcher_discord_profiles WHERE account_id=?1').bind(user.id).first();
    launcherNeed(discord && launcherAvatarUrl(discord.avatar_url),409,'discord_profile_missing','Sign out and sign in with Discord once after deploying the profile API update.');
    avatar=discord.avatar_url;hash='';png=null;
  } else if(source==='initials'){avatar='';hash='';png=null;}
  else source=previous?.source || 'initials';
  await env.DB.batch([env.DB.prepare(`INSERT INTO launcher_profiles VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)
    ON CONFLICT(account_id) DO UPDATE SET public_key=excluded.public_key,identity=excluded.identity,friend_code=excluded.friend_code,
    source=excluded.source,avatar_url=excluded.avatar_url,sharing=excluded.sharing,picture_hash=excluded.picture_hash,picture=excluded.picture,updated_at=excluded.updated_at`)
    .bind(user.id,input.publicKey,input.identity,input.friendCode,source,avatar,input.sharing?1:0,hash,png,Math.floor(Date.now()/1000)),
    env.DB.prepare(`INSERT INTO launcher_social_links VALUES(?1,?2,?3,?4,?5) ON CONFLICT(account_id,public_key) DO UPDATE SET
      identity=excluded.identity,friend_code=excluded.friend_code,updated_at=excluded.updated_at`)
      .bind(user.id,input.publicKey,input.identity,input.friendCode,Math.floor(Date.now()/1000))]);
  return Response.json({accountId:user.id,publicKey:input.publicKey,avatarUrl:avatar,source,sharing:input.sharing},{headers:launcherProfileHeaders});
}
// DKRR_LAUNCHER_PROFILE_END
export { handleLauncherProfile, launcherVerifiedSocial };
