// Native pairing uses the existing Discord OAuth/opaque session authority.
// No provider tokens, credentials, private roles or grant appear in a URL.
async function launcherStart(request, env) {
  requireOrigin(request, env);
  await limitAuth(request, env);
  const config = configuration(env), data = await readJSON(request, 2048);
  const challenge = validate(data.challenge, TOKEN_PATTERN, 'Invalid launcher proof.');
  const state = validate(data.clientState, /^dkrnative_[A-Za-z0-9_-]{43}$/, 'Invalid launcher state.');
  const flow = randomToken(32), stamp = now();
  await reserveAuthStart(env,stamp);
  await env.DB.prepare('DELETE FROM launcher_flows WHERE expires_at<=?1').bind(stamp).run();
  await env.DB.prepare(`INSERT INTO launcher_flows
    (flow_hash,client_challenge,client_state,created_at,expires_at,last_poll)
    VALUES (?1,?2,?3,?4,?5,0)`).bind(await sha256(flow),challenge,state,stamp,stamp+FLOW_SECONDS).run();
  const target = new URL('/auth/launcher/authorize', request.url);
  target.search = new URLSearchParams({flow}).toString();
  return json({flow,browserUrl:target.href,verificationCode:state.slice(-8),expiresAt:stamp+FLOW_SECONDS,interval:4});
}

async function launcherAuthorize(request,env){
  const url=new URL(request.url),flow=validate(url.searchParams.get('flow'),TOKEN_PATTERN,'Invalid launcher sign-in.');
  const row=await env.DB.prepare('SELECT client_state,client_challenge FROM launcher_flows WHERE flow_hash=?1 AND expires_at>?2 AND consumed=0').bind(await sha256(flow),now()).first();
  if(!row)return callbackFailure('This launcher sign-in expired.');
  const target=new URL('/auth/discord/start',request.url);target.search=new URLSearchParams({challenge:row.client_challenge,client_state:row.client_state}).toString();
  // Only fixed/validated token characters enter the page. The code matches the
  // app, so following an unsolicited sign-in link is not silently encouraged.
  const link=target.href.replace(/&/g,'&amp;').replace(/"/g,'&quot;');
  return new Response('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sign in to DKR-R</title><body><h1>Sign in to DKR-R</h1><p>Only continue if you started this sign-in in DKR-R.</p><p>Check that the launcher shows this code: <strong>'+row.client_state.slice(-8)+'</strong></p><p><a href="'+link+'">Continue with Discord</a></p></body></html>',{headers:{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store'}});
}

async function launcherSession(response,device){
  const data=await response.json();
  const cookies=typeof response.headers.getSetCookie==='function'?response.headers.getSetCookie():
    typeof response.headers.getAll==='function'?response.headers.getAll('Set-Cookie'):[response.headers.get('Set-Cookie')||''];
  const match=/(?:^|[,;]\s*)__Host-dkr_remember=([A-Za-z0-9_-]{43})(?:;|$)/.exec(cookies.join(','));
  if(data.remembered===true&&TOKEN_PATTERN.test(match?.[1]||device||''))data.device=match?.[1]||device;
  return json(data,response.status);
}

async function launcherRefresh(request,env){
  requireOrigin(request,env);await limitAuth(request,env);
  const data=await readJSON(request,1024),device=validate(data.device,TOKEN_PATTERN,'Sign in again to continue.');
  const response=await refreshSession(new Request(new URL('/auth/refresh',request.url),{method:'POST',headers:{Origin:baseURL(env).origin,'Content-Type':'application/json',Cookie:REMEMBER_COOKIE+'='+device},body:'{}'}),env);
  return launcherSession(response,device);
}

async function launcherLogout(request,env){
  requireOrigin(request,env);const data=await readJSON(request,1024);
  const headers=new Headers({Origin:baseURL(env).origin,'Content-Type':'application/json'});
  if(data.device)headers.set('Cookie',REMEMBER_COOKIE+'='+validate(data.device,TOKEN_PATTERN,'Invalid remembered session.'));
  const supplied=request.headers.get('Authorization');if(supplied)headers.set('Authorization',supplied);
  return logout(new Request(new URL('/auth/logout',request.url),{method:'POST',headers,body:'{}'}),env);
}

async function launcherComplete(frontend, state, values, env) {
  if (!env) return callbackFailure('This launcher sign-in is unavailable.');
  const row = await env.DB.prepare('SELECT flow_hash FROM launcher_flows WHERE client_state=?1 AND expires_at>?2 AND consumed=0')
    .bind(state, now()).first();
  if (!row) return callbackFailure('This launcher sign-in expired. Return to DKR-R and try again.');
  const cipher = values.grant ? await protectVerifier(values.grant,row.flow_hash,env) : null;
  const failure = values.error ? 'Discord sign-in did not complete. Return to DKR-R and try again.' : '';
  await env.DB.prepare('UPDATE launcher_flows SET grant_cipher=?1,error=?2 WHERE flow_hash=?3 AND consumed=0 AND expires_at>?4')
    .bind(cipher,failure,row.flow_hash,now()).run();
  return new Response('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DKR-R sign-in</title><body><h1>'+
    (failure?'Sign-in not completed':'Signed in to DKR-R')+'</h1><p>Return to DKR-R. You can close this window.</p></body></html>',
    {headers:{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store','Set-Cookie':correlationCookie('',0)}});
}

async function launcherProof(request, env) {
  requireOrigin(request, env);
  const data = await readJSON(request, 2048);
  const flow = validate(data.flow,TOKEN_PATTERN,'Invalid launcher sign-in.');
  const verifier = validate(data.verifier,VERIFIER_PATTERN,'Invalid launcher proof.');
  const state = validate(data.clientState,/^dkrnative_[A-Za-z0-9_-]{43}$/,'Invalid launcher state.');
  const hash = await sha256(flow), stamp=now();
  const row=await env.DB.prepare(`SELECT * FROM launcher_flows WHERE flow_hash=?1
    AND client_challenge=?2 AND client_state=?3 AND expires_at>?4 AND consumed=0`)
    .bind(hash,await sha256(verifier),state,stamp).first();
  if(!row)throw new HttpError(401,'This launcher sign-in expired. Sign in again.','launcher_expired');
  return {data,hash,row,stamp};
}

async function launcherPoll(request, env) {
  const {data,hash,row,stamp}=await launcherProof(request,env);
  const claimed=await env.DB.prepare(`UPDATE launcher_flows SET last_poll=?1 WHERE flow_hash=?2
    AND last_poll<=?3 AND consumed=0 RETURNING flow_hash`).bind(stamp,hash,stamp-3).first();
  if(!claimed)throw new HttpError(429,'Wait before checking sign-in again.','launcher_slow_down');
  if(row.error)throw new HttpError(401,row.error,'launcher_cancelled');
  if(!row.grant_cipher)return json({pending:true,interval:4});
  const grant=await revealVerifier(row.grant_cipher,hash,env);
  const won=await env.DB.prepare('UPDATE launcher_flows SET consumed=1,grant_cipher=NULL WHERE flow_hash=?1 AND consumed=0 RETURNING flow_hash').bind(hash).first();
  if(!won)throw new HttpError(401,'This launcher sign-in was already used.','launcher_expired');
  // Exchange performs the existing atomic, PKCE-bound grant consumption and
  // checks the current account. Browser cookies are not retained by DKR-R.
  return launcherSession(await exchange(new Request(new URL('/auth/exchange',request.url),{
    method:'POST',headers:{Origin:baseURL(env).origin,'Content-Type':'application/json'},
    body:JSON.stringify({grant,verifier:data.verifier,clientState:data.clientState,remember:data.remember===true})}),env));
}

async function launcherCancel(request, env) {
  const {hash}=await launcherProof(request,env);
  await env.DB.prepare('DELETE FROM launcher_flows WHERE flow_hash=?1').bind(hash).run();
  return json({ok:true});
}
