// DKR-R Mod Library 1.0.0-rc.1 — generated dashboard bundle.
// Deploy this file as a Cloudflare module Worker. Configure bindings and secrets in Cloudflare.
// Never upload this file or the private backend directory to the public FTP folder.

const __dkrCommon = (() => {
// Shared validation. This file belongs in the Worker project, never the FTP folder.
class HttpError extends Error {
  constructor(status, message, code = 'request_failed') {
    super(message); this.name = 'HttpError'; this.status = status; this.code = code;
  }
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {status, headers: {
    'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store'
  }});
}

async function readJSON(request, maxBytes = 65536) {
  if ((request.headers.get('Content-Type') || '').toLowerCase().split(';')[0].trim() !== 'application/json') {
    throw new HttpError(415, 'Send this request as JSON.', 'json_required');
  }
  const declared = Number(request.headers.get('Content-Length') || 0);
  if (declared > maxBytes) throw new HttpError(413, 'This request is too large.', 'request_too_large');
  const reader = request.body?.getReader();
  if (!reader) throw new HttpError(400, 'A request body is required.', 'body_required');
  const chunks = []; let total = 0;
  while (true) {
    const {done, value} = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > maxBytes) { await reader.cancel(); throw new HttpError(413, 'This request is too large.', 'request_too_large'); }
    chunks.push(value);
  }
  const buffer = new Uint8Array(total); let offset = 0;
  for (const chunk of chunks) { buffer.set(chunk, offset); offset += chunk.byteLength; }
  let value;
  try {
    // Supply both options for generated Workers types; keep strict UTF-8 decoding.
    value = JSON.parse(new TextDecoder('utf-8', {fatal:true, ignoreBOM:false}).decode(buffer));
  }
  catch (_) { throw new HttpError(400, 'This request contains invalid JSON.', 'invalid_json'); }
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new HttpError(400, 'Send a JSON object.', 'object_required');
  return value;
}

const now = () => Math.floor(Date.now() / 1000);
function base64url(bytes) {
  let binary = ''; for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}
function randomToken(bytes = 32) {
  return base64url(crypto.getRandomValues(new Uint8Array(bytes)));
}
async function sha256(text) {
  return base64url(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))));
}

function safeText(value, max, label, required = false) {
  if (value == null) value = '';
  if (typeof value !== 'string') throw new HttpError(400, `${label} must be text.`, 'invalid_text');
  const cleaned = value.trim().replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g, '');
  if (required && !cleaned) throw new HttpError(400, `${label} is required.`, 'required_field');
  if (cleaned.length > max) throw new HttpError(400, `${label} must be ${max} characters or fewer.`, 'field_too_long');
  return cleaned;
}

function requireRole(user, role) {
  const levels = {player:1, moderator:2, admin:3};
  if (!user || user.status !== 'active' || !levels[role] || (levels[user.role] || 0) < levels[role]) {
    throw new HttpError(403, role === 'admin' ? 'Administrator access is required.' : 'Moderator access is required.', 'forbidden');
  }
  return user;
}

function baseURL(env) {
  let url;
  try { url = new URL(env.FRONTEND_URL); }
  catch (_) { throw new HttpError(503, 'The library service is awaiting its website address.', 'configuration_required'); }
  if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash) {
    throw new HttpError(503, 'The library service needs a valid HTTPS website address.', 'configuration_required');
  }
  return url;
}

function requireOrigin(request, env) {
  if (request.headers.get('Origin') !== baseURL(env).origin) {
    throw new HttpError(403, 'This action must come from the configured library website.', 'origin_not_allowed');
  }
}

function publicUser(row) {
  if (!row) return null;
  return {
    id: row.id,
    displayName: row.display_name ?? row.displayName,
    role: row.role,
    status: row.status,
    createdAt: row.created_at ?? row.createdAt,
    discordLinked: true
  };
}

function allowedRoots(env, kind) {
  const value = kind === 'image' ? (env.IMAGE_ORIGINS || env.DOWNLOAD_ORIGINS) : env.DOWNLOAD_ORIGINS;
  let roots;
  try { roots = typeof value === 'string' ? JSON.parse(value) : value; }
  catch (_) { throw new HttpError(503, 'The library file locations need to be configured.', 'configuration_required'); }
  if (!Array.isArray(roots) || roots.some(x => typeof x !== 'string')) {
    throw new HttpError(503, 'The library file locations need to be configured.', 'configuration_required');
  }
  if (kind === 'image') roots = roots.concat(new URL('./', baseURL(env)).href);
  return roots.map(root => {
    let url;
    try { url = new URL(root); } catch (_) { throw new HttpError(503, 'A configured file location is invalid.', 'configuration_required'); }
    if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash) throw new HttpError(503, 'A configured file location is invalid.', 'configuration_required');
    return url;
  });
}

function validatePublicURL(value, env, kind = 'download') {
  const text = safeText(value, 1500, kind === 'image' ? 'Image address' : 'Download address', true);
  let url;
  try { url = new URL(text); } catch (_) { throw new HttpError(400, 'Enter a complete public HTTPS file address.', 'invalid_url'); }
  if (url.protocol !== 'https:' || url.username || url.password || url.hash || url.search || /[\\\r\n]/.test(text)) {
    throw new HttpError(400, 'Use a permanent HTTPS file address without credentials, a query or a fragment.', 'invalid_url');
  }
  if (/%(?:2f|5c|00|25)/i.test(url.pathname)) throw new HttpError(400, 'The file address contains an unsupported encoded path.', 'invalid_url');
  let decodedPath;
  try { decodedPath = decodeURIComponent(url.pathname); } catch (_) { throw new HttpError(400, 'The file address contains an invalid path.', 'invalid_url'); }
  if (decodedPath.split('/').some(x => x === '.' || x === '..')) throw new HttpError(400, 'Use the final file address without relative path segments.', 'invalid_url');
  const allowed = allowedRoots(env, kind).some(root => {
    const prefix = root.pathname.endsWith('/') ? root.pathname : root.pathname + '/';
    return url.origin === root.origin && url.pathname.startsWith(prefix);
  });
  if (!allowed) throw new HttpError(400, 'Use a file inside one of the owner-approved download locations.', 'file_location_not_allowed');
  if (kind === 'image' && !/\.(?:png|jpe?g|webp)$/i.test(decodedPath)) throw new HttpError(400, 'Use a PNG, JPEG or WebP image.', 'invalid_image_format');
  if (kind === 'download' && !/\.(?:zip|rtz|dkrmap|xdelta|nrm|dkrcode|bps|ips|7z)$/i.test(decodedPath)) {
    throw new HttpError(400, 'Use a supported mod package: ZIP, RTZ, DKRMAP, XDELTA, NRM, DKRCODE, BPS, IPS or 7Z.', 'invalid_package_format');
  }
  return url.href;
}

return Object.freeze({HttpError,json,readJSON,now,randomToken,sha256,safeText,requireRole,baseURL,requireOrigin,publicUser,validatePublicURL});
})();

const __dkrAuth = (() => {
/**
 * Discord-only authentication for a static frontend on a separate HTTPS origin.
 *
 * GET  /auth/discord/start?challenge=<S256>&client_state=<random nonce>
 * GET  /auth/discord/callback?code=...&state=...
 * POST /auth/exchange { grant, verifier, clientState }
 *      -> { token, user, expiresAt, absoluteExpiresAt }
 * POST /auth/refresh -> restore the current browser session or remembered device
 * POST /auth/persistence -> confirm that this browser accepted the auth cookies
 * GET  /auth/me -> { user }
 * POST /auth/logout -> { ok: true }
 *
 * Provider tokens never leave this module and are not retained. The frontend
 * receives only a one-use, verifier-bound grant in a fragment at FRONTEND_URL.
 * A session is opaque, revocable, and checked against current account roles.
 */
const {
  HttpError, json, readJSON, now, randomToken, sha256,
  safeText, requireOrigin, baseURL, publicUser
} = __dkrCommon;
const FLOW_SECONDS = 10 * 60;
const GRANT_SECONDS = 60;
const IDLE_SECONDS = 30 * 60;
const SESSION_TOUCH_SECONDS = 60;
const ABSOLUTE_SECONDS = 8 * 60 * 60;
const REMEMBER_SECONDS = 30 * 24 * 60 * 60;
const SESSION_COOKIE = '__Host-dkr_session';
const REMEMBER_COOKIE = '__Host-dkr_remember';
const CORRELATION_COOKIE = '__Host-dkr_oauth';
const TOKEN_PATTERN = /^[A-Za-z0-9_-]{43}$/;
const STATE_PATTERN = /^[A-Za-z0-9_-]{32,128}$/;
const VERIFIER_PATTERN = /^[A-Za-z0-9._~-]{43,128}$/;
const encoder = new TextEncoder();
const AUTH_REVISION = '20261018-4';
// Discord requires an identifying User-Agent for HTTP API clients.
// https://docs.discord.com/developers/reference#user-agent
const DISCORD_USER_AGENT = 'DiscordBot (https://github.com/ThatGuyMcd/DKR-R, 1.0.0-rc.1)';

class DiscordRequestError extends Error {
  constructor(code) {
    super('The Discord request could not be completed.');
    this.code = code;
  }
}

/**
 * Optional Workers Rate Limiting binding. Configure AUTH_RATE_LIMITER with a
 * shared 20 requests / 60 seconds budget for sign-in starts and grant exchanges.
 * Only a keyed, context-separated digest is passed to the ephemeral limiter;
 * neither the visitor IP nor its digest is written to D1 or application logs.
 * Cloudflare supplies CF-Connecting-IP at its public edge. A missing edge IP
 * shares one fallback bucket, which also keeps local/misconfigured calls bounded.
 */
async function limitAuth(request, env) {
  if (!env.AUTH_RATE_LIMITER) return;
  const secret = String(env.AUTH_ENCRYPTION_KEY || env.DISCORD_CLIENT_SECRET || '');
  if (secret.length < 16) throw new HttpError(503, 'Discord sign-in is not configured yet.', 'AUTH_CONFIGURATION');
  const keyBytes = await crypto.subtle.digest('SHA-256', encoder.encode('DKR-R auth-rate-key v1\0' + secret));
  const key = await crypto.subtle.importKey('raw', keyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const fingerprint = await crypto.subtle.sign('HMAC', key,
    encoder.encode(request.headers.get('CF-Connecting-IP') || 'unavailable'));
  let outcome;
  try { outcome = await env.AUTH_RATE_LIMITER.limit({ key: 'auth:v1:' + b64url(fingerprint) }); }
  catch (_) { throw new HttpError(503, 'Sign-in is temporarily unavailable. Please try again shortly.', 'AUTH_RATE_UNAVAILABLE'); }
  if (!outcome || outcome.success !== true) {
    throw new HttpError(429, 'Too many sign-in attempts. Please wait a minute and try again.', 'AUTH_RATE_LIMITED');
  }
}

/**
 * Always-on, fixed UTC-hour quota for new anonymous OAuth flows. The conditional
 * upsert does not write after the bucket is saturated. This bounds new flow and
 * quota-row writes, but requests can still incur database reads/Worker costs;
 * it is not complete denial-of-service protection. No visitor identity is stored.
 */
async function reserveAuthStart(env, timestamp) {
  const configured = env.AUTH_STARTS_PER_HOUR;
  const limit = configured === undefined || configured === null || configured === '' ? 300 : Number(configured);
  if (!Number.isInteger(limit) || limit < 1 || limit > 5000) {
    throw new HttpError(503, 'The sign-in capacity has not been configured correctly.', 'AUTH_CONFIGURATION');
  }
  const windowStart = Math.floor(timestamp / 3600) * 3600;
  const reserved = await env.DB.prepare(`INSERT INTO auth_start_limits (bucket,window_start,count)
    VALUES ('discord',?1,1)
    ON CONFLICT(bucket) DO UPDATE SET
      window_start=excluded.window_start,
      count=CASE WHEN auth_start_limits.window_start<excluded.window_start THEN 1 ELSE auth_start_limits.count+1 END
    WHERE auth_start_limits.window_start<excluded.window_start
      OR (auth_start_limits.window_start=excluded.window_start AND auth_start_limits.count<?2)
    RETURNING count`).bind(windowStart, limit).first();
  if (!reserved) {
    throw new HttpError(429, 'The library has reached its sign-in capacity for this hour. Please try again after the next hour begins.', 'AUTH_START_CAPACITY');
  }
}

function configuredFrontend(env) {
  const value = baseURL(env);
  const url = new URL(value instanceof URL ? value.href : String(value));
  if (url.protocol !== 'https:' || url.username || url.password || url.hash || url.search) {
    throw new HttpError(503, 'The sign-in callback has not been configured correctly.', 'AUTH_CONFIGURATION');
  }
  return url;
}

function configuration(env) {
  const frontend = configuredFrontend(env);
  const clientId = String(env.DISCORD_CLIENT_ID || '').trim();
  const clientSecret = String(env.DISCORD_CLIENT_SECRET || '').trim();
  const ownerId = String(env.OWNER_DISCORD_ID || '').trim();
  if (!/^\d{17,22}$/.test(clientId) || clientSecret.length < 16 || !/^\d{17,22}$/.test(ownerId)) {
    throw new HttpError(503, 'Discord sign-in is not configured yet.', 'AUTH_CONFIGURATION');
  }
  if (!env.DB) throw new HttpError(503, 'The account service is not configured yet.', 'AUTH_CONFIGURATION');
  return { frontend, clientId, clientSecret, ownerId };
}

function validate(value, pattern, message) {
  if (typeof value !== 'string' || !pattern.test(value)) {
    throw new HttpError(400, message, 'INVALID_AUTH_REQUEST');
  }
  return value;
}

function b64url(bytes) {
  let binary = '';
  for (const value of new Uint8Array(bytes)) binary += String.fromCharCode(value);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function fromB64url(value) {
  if (!/^[A-Za-z0-9_-]+$/.test(value)) throw new Error('Invalid encrypted value');
  const base = value.replace(/-/g, '+').replace(/_/g, '/');
  const binary = atob(base + '='.repeat((4 - base.length % 4) % 4));
  return Uint8Array.from(binary, character => character.charCodeAt(0));
}

async function encryptionKey(env) {
  const configured = String(env.AUTH_ENCRYPTION_KEY || '');
  if (configured && configured.length < 32) {
    throw new HttpError(503, 'The authentication encryption key is not configured correctly.', 'AUTH_CONFIGURATION');
  }
  const secret = configured || String(env.DISCORD_CLIENT_SECRET || '');
  if (secret.length < 16) throw new HttpError(503, 'Discord sign-in is not configured yet.', 'AUTH_CONFIGURATION');
  // Context separation ensures this derived key is not the raw provider secret.
  const bytes = await crypto.subtle.digest('SHA-256', encoder.encode('DKR-R oauth-verifier AES-GCM v1\0' + secret));
  return crypto.subtle.importKey('raw', bytes, 'AES-GCM', false, ['encrypt', 'decrypt']);
}

async function protectVerifier(value, stateHash, env) {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ciphertext = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: encoder.encode(stateHash) },
    await encryptionKey(env), encoder.encode(value)
  );
  return 'v1.' + b64url(iv) + '.' + b64url(ciphertext);
}

async function revealVerifier(value, stateHash, env) {
  const [version, ivText, ciphertextText, extra] = String(value).split('.');
  if (version !== 'v1' || extra !== undefined) throw new Error('Invalid encrypted verifier');
  const iv = fromB64url(ivText);
  if (iv.length !== 12) throw new Error('Invalid encrypted verifier');
  const plaintext = await crypto.subtle.decrypt(
    { name: 'AES-GCM', iv, additionalData: encoder.encode(stateHash) },
    await encryptionKey(env), fromB64url(ciphertextText)
  );
  return new TextDecoder().decode(plaintext);
}

function cookieValue(request) {
  const cookies = (request.headers.get('Cookie') || '').split(';');
  const matches = cookies.map(value => value.trim()).filter(value => value.startsWith(CORRELATION_COOKIE + '='));
  if (matches.length !== 1) return null;
  const nonce = matches[0].slice(CORRELATION_COOKIE.length + 1);
  return TOKEN_PATTERN.test(nonce) ? nonce : null;
}

function correlationCookie(value, seconds) {
  return CORRELATION_COOKIE + '=' + value + '; Path=/; Max-Age=' + seconds + '; Secure; HttpOnly; SameSite=Lax';
}

function redirect(location, cookie) {
  const headers = new Headers({
    Location: location,
    'Cache-Control': 'no-store',
    'Referrer-Policy': 'no-referrer',
    'X-Content-Type-Options': 'nosniff'
  });
  if (cookie) headers.set('Set-Cookie', cookie);
  return new Response(null, { status: 303, headers });
}

async function completeAtFrontend(frontend, clientState, values, env) {
  if (clientState.startsWith("dkrnative_")) return launcherComplete(frontend, clientState, values, env);
  const destination = new URL(frontend.href);
  const fragment = new URLSearchParams({ dkr_state: clientState });
  if (values.grant) fragment.set('dkr_grant', values.grant);
  if (values.error) fragment.set('dkr_error', values.error);
  destination.hash = fragment.toString();
  return redirect(destination.href, correlationCookie('', 0));
}

function callbackFailure(message) {
  // No provider URL, secret, state, token or private account data is reflected.
  const body = '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Sign-in could not be completed</title><body><h1>Sign-in could not be completed</h1><p>' + message +
    '</p><p>Close this window and choose Continue with Discord again in the library.</p></body></html>';
  return new Response(body, { status: 400, headers: {
    'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
    'Content-Security-Policy': "default-src 'none'; base-uri 'none'; frame-ancestors 'none'",
    'X-Content-Type-Options': 'nosniff', 'Set-Cookie': correlationCookie('', 0)
  } });
}

async function providerFetch(url, options, stage) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 12000);
  const failure = stage === 'token' ? 'discord_token_failed' : 'discord_identity_failed';
  try {
    const headers = { Accept: 'application/json', ...options.headers, 'User-Agent': DISCORD_USER_AGENT };
    // workerd rejects redirect:'error' during Request construction. Manual
    // mode is supported and returns redirects to this code, where !ok below
    // rejects them without forwarding credentials or following Location.
    const response = await fetch(url, { ...options, headers, signal: controller.signal, redirect: 'manual' });
    if (!response.ok) {
      if (response.status === 429) throw new DiscordRequestError('discord_rate_limited');
      let code = failure;
      if (stage === 'token') {
        if (response.status === 403) code = 'discord_token_blocked';
        if (response.status === 401) code = 'discord_token_unauthorized';
        // Only known OAuth error labels affect the public diagnosis. Never
        // forward descriptions, arbitrary body text, tokens or provider details.
        if (/^application\/json\b/i.test(response.headers.get('Content-Type') || '')) {
          try {
            const detail = await response.json();
            if (detail && (detail.error === 'invalid_client' || detail.error === 'unauthorized_client')) code = 'discord_credentials_invalid';
            else if (detail && detail.error === 'invalid_grant') code = 'discord_authorization_invalid';
          } catch (_) { /* Keep the fixed HTTP-stage diagnosis. */ }
        }
      }
      throw new DiscordRequestError(code);
    }
    try { return await response.json(); }
    catch (_) { throw new DiscordRequestError(stage === 'token' ? 'discord_token_invalid' : failure); }
  } catch (error) {
    if (error instanceof DiscordRequestError) throw error;
    throw new DiscordRequestError('discord_unreachable');
  } finally { clearTimeout(timer); }
}

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

async function startDiscord(request, env) {
  const config = configuration(env);
  const url = new URL(request.url);
  if (url.protocol !== 'https:') throw new HttpError(400, 'Sign-in requires HTTPS.', 'HTTPS_REQUIRED');
  const challenge = validate(url.searchParams.get('challenge'), TOKEN_PATTERN, 'The sign-in challenge is invalid.');
  const clientState = validate(url.searchParams.get('client_state'), STATE_PATTERN, 'The sign-in request is invalid.');
  const issued = now();
  await reserveAuthStart(env, issued);
  const state = randomToken(32), nonce = randomToken(32), providerVerifier = randomToken(32);
  const stateHash = await sha256(state);
  const encrypted = await protectVerifier(providerVerifier, stateHash, env);
  await env.DB.prepare(`INSERT INTO oauth_flows
    (state_hash,browser_nonce_hash,provider_verifier_ciphertext,client_challenge,client_state,frontend_url,created_at,expires_at)
    VALUES (?1,?2,?3,?4,?5,?6,?7,?8)`)
    .bind(stateHash, await sha256(nonce), encrypted, challenge, clientState, config.frontend.href, issued, issued + FLOW_SECONDS).run();
  const authorization = new URL('https://discord.com/oauth2/authorize');
  authorization.search = new URLSearchParams({
    client_id: config.clientId,
    response_type: 'code',
    scope: 'identify',
    redirect_uri: url.origin + '/auth/discord/callback',
    state,
    code_challenge: await sha256(providerVerifier),
    code_challenge_method: 'S256'
  }).toString();
  return redirect(authorization.href, correlationCookie(nonce, FLOW_SECONDS));
}

async function discordCallback(request, env) {
  const config = configuration(env);
  const url = new URL(request.url);
  const state = url.searchParams.get('state');
  const nonce = cookieValue(request);
  if (!state || !TOKEN_PATTERN.test(state) || !nonce) {
    return callbackFailure('This sign-in request could not be verified.');
  }
  const stateHash = await sha256(state);
  const flow = await env.DB.prepare(`DELETE FROM oauth_flows
    WHERE state_hash=?1 AND browser_nonce_hash=?2 AND expires_at>?3
    RETURNING provider_verifier_ciphertext,client_challenge,client_state,frontend_url`)
    .bind(stateHash, await sha256(nonce), now()).first();
  if (!flow || flow.frontend_url !== config.frontend.href) {
    return callbackFailure('This sign-in request has expired or was already used.');
  }
  const fail = error => completeAtFrontend(config.frontend, flow.client_state, { error }, env);
  const issuer = url.searchParams.get('iss');
  if (issuer !== null && issuer !== 'https://discord.com') return fail('invalid_response');
  if (url.searchParams.has('error')) return fail('access_denied');
  const code = url.searchParams.get('code');
  if (!code || code.length > 2048) return fail('invalid_response');

  let stage = 'login_verification_failed';
  try {
    const verifier = await revealVerifier(flow.provider_verifier_ciphertext, stateHash, env);
    stage = 'discord_token_failed';
    const tokens = await providerFetch('https://discord.com/api/oauth2/token', {
      method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        client_id: config.clientId, client_secret: config.clientSecret,
        grant_type: 'authorization_code', code,
        redirect_uri: url.origin + '/auth/discord/callback', code_verifier: verifier
      }).toString()
    }, 'token');
    if (!tokens || typeof tokens.access_token !== 'string' || !tokens.access_token ||
        String(tokens.token_type || '').toLowerCase() !== 'bearer' ||
        !String(tokens.scope || '').split(/\s+/).includes('identify')) {
      return fail('discord_token_invalid');
    }
    stage = 'discord_identity_failed';
    const identity = await providerFetch('https://discord.com/api/v10/users/@me', {
      headers: { Authorization: 'Bearer ' + tokens.access_token, Accept: 'application/json' }
    }, 'identity');
    if (!identity || typeof identity !== 'object') return fail('discord_identity_failed');
    // Only these identity fields are retained. Email, avatar and provider tokens are discarded.
    const discordId = typeof identity.id === 'string' ? identity.id : '';
    if (!/^\d{17,22}$/.test(discordId) || identity.bot === true) return fail('invalid_response');
    const nameValue = typeof identity.global_name === 'string' && identity.global_name.trim()
      ? identity.global_name : typeof identity.username === 'string' ? identity.username : 'Racer';
    const displayName = safeText(nameValue.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 80) || 'Racer', 80, 'display name', true);
    const issued = now();
    stage = 'account_write_failed';
    // UNIQUE discord_id handles simultaneous first sign-ins. Existing website roles
    // and display names are intentionally preserved on subsequent Discord logins.
    await env.DB.prepare(`INSERT INTO accounts
      (id,discord_id,display_name,role,status,created_at,updated_at)
      VALUES (?1,?2,?3,?4,'active',?5,?5) ON CONFLICT(discord_id) DO NOTHING`)
      .bind('usr_' + randomToken(18), discordId, displayName, discordId === config.ownerId ? 'admin' : 'player', issued).run();
    stage = 'account_read_failed';
    const user = await env.DB.prepare(`SELECT id,display_name,role,status,created_at,updated_at
      FROM accounts WHERE discord_id=?1`).bind(discordId).first();
    if (!user || user.status !== 'active') return fail('account_disabled');
    stage = 'login_grant_failed';
    const grant = randomToken(32);
    await env.DB.prepare(`INSERT INTO login_grants
      (grant_hash,user_id,client_challenge,client_state,frontend_origin,created_at,expires_at)
      VALUES (?1,?2,?3,?4,?5,?6,?7)`)
      .bind(await sha256(grant), user.id, flow.client_challenge, flow.client_state, config.frontend.origin, issued, issued + GRANT_SECONDS).run();
    return completeAtFrontend(config.frontend, flow.client_state, { grant }, env);
  } catch (error) {
    // Do not expose/log upstream response bodies, provider tokens, SQL or secrets.
    return fail(error instanceof DiscordRequestError ? error.code : stage);
  }
}

async function exchange(request, env) {
  requireOrigin(request, env);
  const frontend = configuredFrontend(env);
  const data = await readJSON(request, 8192);
  const grant = validate(data.grant, TOKEN_PATTERN, 'The sign-in grant is invalid.');
  const verifier = validate(data.verifier, VERIFIER_PATTERN, 'The sign-in verifier is invalid.');
  const clientState = validate(data.clientState, STATE_PATTERN, 'The sign-in state is invalid.');
  if (data.remember !== undefined && typeof data.remember !== 'boolean') {
    throw new HttpError(400, 'Choose whether to stay signed in on this device.', 'INVALID_REMEMBER_CHOICE');
  }
  const issued = now();
  const record = await env.DB.prepare(`DELETE FROM login_grants
    WHERE grant_hash=?1 AND client_challenge=?2 AND client_state=?3 AND frontend_origin=?4 AND expires_at>?5
    RETURNING user_id`)
    .bind(await sha256(grant), await sha256(verifier), clientState, frontend.origin, issued).first();
  if (!record) throw new HttpError(401, 'This sign-in request expired or was already used. Please sign in again.', 'GRANT_INVALID');
  const row = await env.DB.prepare(`SELECT id,display_name,role,status,created_at,updated_at
    FROM accounts WHERE id=?1 AND status='active'`).bind(record.user_id).first();
  if (!row) throw new HttpError(403, 'This account is unavailable.', 'ACCOUNT_DISABLED');
  // Replacing an ordinary browser sign-in also revokes its previous short
  // session, so an older tab cannot keep that replaced credential alive.
  const previousSession = browserCookieValue(request, SESSION_COOKIE);
  if (previousSession) {
    await env.DB.prepare('DELETE FROM sessions WHERE token_hash=?1').bind(await sha256(previousSession)).run();
  }
  const token = randomToken(32);
  await env.DB.prepare(`INSERT INTO sessions
    (token_hash,user_id,created_at,last_seen_at,idle_expires_at,absolute_expires_at,revoked_at)
    VALUES (?1,?2,?3,?3,?4,?5,NULL)`)
    .bind(await sha256(token), row.id, issued, issued + IDLE_SECONDS, issued + ABSOLUTE_SECONDS).run();
  const previousDevice = browserCookieValue(request, REMEMBER_COOKIE);
  let rememberedToken = null, rememberedUntil = null, warning = '';
  try {
    if (previousDevice) await revokeRememberedDevice(env, await sha256(previousDevice));
    if (data.remember === true) {
      rememberedToken = randomToken(32);
      rememberedUntil = issued + REMEMBER_SECONDS;
      await env.DB.prepare(`INSERT INTO auth_remembered_devices
        (token_hash,user_id,created_at,last_used_at,expires_at,session_hashes_json)
        VALUES (?1,?2,?3,?3,?4,?5)`)
        .bind(await sha256(rememberedToken), row.id, issued, rememberedUntil, JSON.stringify([await sha256(token)])).run();
    }
  } catch (_) {
    // Remembering is optional. A missing additive migration must not prevent
    // the already verified Discord identity from receiving a normal session.
    rememberedToken = null; rememberedUntil = null;
    warning = 'Stay signed in is unavailable. You are signed in for this browser session; the site owner may need to apply the sign-in update.';
  }
  const response = json({ token, user: publicUser(row), expiresAt: issued + IDLE_SECONDS,
    absoluteExpiresAt: issued + ABSOLUTE_SECONDS, remembered: !!rememberedToken, rememberedUntil,
    persistenceCheck: true, persistenceWarning: warning });
  setBrowserCookie(response, SESSION_COOKIE, token);
  setBrowserCookie(response, REMEMBER_COOKIE, rememberedToken, rememberedToken ? REMEMBER_SECONDS : 0);
  return response;
}

function browserCookieValue(request, name) {
  const values = (request.headers.get('Cookie') || '').split(';').map(value => value.trim())
    .filter(value => value.startsWith(name + '='));
  if (values.length !== 1) return null;
  const value = values[0].slice(name.length + 1);
  return TOKEN_PATTERN.test(value) ? value : null;
}

function setBrowserCookie(response, name, value, maxAge) {
  // Set only from a credentialed frontend-origin request. The OAuth popup has
  // a different top-level partition and must never issue these cookies.
  response.headers.append('Set-Cookie', name + '=' + (value || '') + '; Path=/' +
    (maxAge === undefined ? '' : '; Max-Age=' + maxAge) + '; Secure; HttpOnly; SameSite=None; Partitioned');
}

function missingRememberedTable(error) {
  return /no such table[^\n]*auth_remembered_devices/i.test(String(error));
}

async function rememberedDevice(request, env, timestamp = now()) {
  const token = browserCookieValue(request, REMEMBER_COOKIE);
  if (!token) return null;
  try {
    return await env.DB.prepare(`SELECT d.token_hash,d.user_id,d.expires_at
      FROM auth_remembered_devices d JOIN accounts a ON a.id=d.user_id
      WHERE d.token_hash=?1 AND d.expires_at>?2 AND a.status='active'`)
      .bind(await sha256(token), timestamp).first();
  } catch (error) {
    if (missingRememberedTable(error)) return null;
    throw error;
  }
}

async function revokeRememberedDevice(env, tokenHash) {
  try {
    await env.DB.batch([
      env.DB.prepare(`DELETE FROM sessions WHERE token_hash IN
        (SELECT value FROM auth_remembered_devices d,json_each(d.session_hashes_json) WHERE d.token_hash=?1)`).bind(tokenHash),
      env.DB.prepare('DELETE FROM auth_remembered_devices WHERE token_hash=?1').bind(tokenHash)
    ]);
  } catch (error) {
    if (!missingRememberedTable(error)) throw error;
  }
}

function bearer(request, optional = false) {
  const header = request.headers.get('Authorization') || '';
  if (!header && optional) return null;
  const match = /^Bearer ([A-Za-z0-9_-]{43})$/i.exec(header);
  if (!match) throw new HttpError(401, 'Please sign in to continue.', 'SIGN_IN_REQUIRED');
  return match[1];
}

/**
 * Current roles/status and expiry are read from the primary database for every
 * request. A transaction touches activity at most once per minute per session;
 * this can end an idle session less than one minute before 30 minutes of silence.
 */
async function sessionRecord(token, env, issued = now()) {
  if (!token) return null;
  const tokenHash = await sha256(token);
  const result = await env.DB.batch([
    env.DB.prepare(`UPDATE sessions
      SET last_seen_at=?1, idle_expires_at=MIN(?2,absolute_expires_at)
      WHERE token_hash=?3 AND revoked_at IS NULL AND idle_expires_at>?1 AND absolute_expires_at>?1
        AND last_seen_at<=?4
        AND EXISTS (SELECT 1 FROM accounts WHERE accounts.id=sessions.user_id AND accounts.status='active')`)
      .bind(issued, issued + IDLE_SECONDS, tokenHash, issued - SESSION_TOUCH_SECONDS),
    env.DB.prepare(`SELECT accounts.id,accounts.display_name,accounts.role,accounts.status,accounts.created_at,accounts.updated_at,
        sessions.idle_expires_at AS session_idle_expires_at,sessions.absolute_expires_at AS session_absolute_expires_at
      FROM sessions JOIN accounts ON accounts.id=sessions.user_id
      WHERE sessions.token_hash=?1 AND sessions.revoked_at IS NULL
        AND sessions.idle_expires_at>?2 AND sessions.absolute_expires_at>?2 AND accounts.status='active'`)
      .bind(tokenHash, issued)
  ]);
  return result[1].results[0] || null;
}

async function requireUser(request, env) {
  requireOrigin(request, env);
  const row = await sessionRecord(bearer(request), env);
  if (!row) throw new HttpError(401, 'Your session expired. Sign in again to continue.', 'SESSION_EXPIRED');
  return publicUser(row);
}

function restoredSession(token, row, device) {
  const sameDevice = device && device.user_id === row.id;
  return { token, user: publicUser(row), expiresAt: row.session_idle_expires_at,
    absoluteExpiresAt: row.session_absolute_expires_at, remembered: !!sameDevice,
    rememberedUntil: sameDevice ? device.expires_at : null, persistenceCheck: true };
}

async function refreshSession(request, env) {
  requireOrigin(request, env);
  await readJSON(request, 1024);
  const timestamp = now();
  const supplied = bearer(request, true), cookie = browserCookieValue(request, SESSION_COOKIE);
  let token = supplied, row = await sessionRecord(supplied, env, timestamp);
  if (!row && cookie && cookie !== supplied) { token = cookie; row = await sessionRecord(cookie, env, timestamp); }
  const device = await rememberedDevice(request, env, timestamp);
  if (row) {
    const response = json(restoredSession(token, row, device));
    setBrowserCookie(response, SESSION_COOKIE, token);
    return response;
  }
  if (!device) {
    const response = json({token: null, user: null, remembered: false, rememberedUntil: null});
    setBrowserCookie(response, SESSION_COOKIE, null, 0);
    if (browserCookieValue(request, REMEMBER_COOKIE)) setBrowserCookie(response, REMEMBER_COOKIE, null, 0);
    return response;
  }
  token = randomToken(32);
  const tokenHash = await sha256(token);
  const absoluteExpiry = Math.min(timestamp + ABSOLUTE_SECONDS, device.expires_at);
  // One browser cookie normally reuses its current short session. Keep a bounded
  // list of live hashes for simultaneous tabs so logout revokes every session
  // minted for this remembered device. No raw credential is persisted in D1.
  const result = await env.DB.batch([
    env.DB.prepare(`UPDATE auth_remembered_devices SET session_hashes_json=COALESCE(
      (SELECT json_group_array(value) FROM json_each(session_hashes_json) WHERE value IN
        (SELECT token_hash FROM sessions WHERE revoked_at IS NULL AND idle_expires_at>?2 AND absolute_expires_at>?2)), '[]')
      WHERE token_hash=?1 AND expires_at>?2`).bind(device.token_hash, timestamp),
    env.DB.prepare(`INSERT INTO sessions
      (token_hash,user_id,created_at,last_seen_at,idle_expires_at,absolute_expires_at,revoked_at)
      SELECT ?1,?2,?3,?3,?4,?5,NULL WHERE EXISTS
        (SELECT 1 FROM auth_remembered_devices d JOIN accounts a ON a.id=d.user_id
         WHERE d.token_hash=?6 AND d.user_id=?2 AND d.expires_at>?3 AND a.status='active'
           AND json_array_length(d.session_hashes_json)<32)`)
      .bind(tokenHash, device.user_id, timestamp, Math.min(timestamp + IDLE_SECONDS, absoluteExpiry), absoluteExpiry, device.token_hash),
    env.DB.prepare(`UPDATE auth_remembered_devices
      SET last_used_at=?2,session_hashes_json=json_insert(session_hashes_json,'$[#]',?3)
      WHERE token_hash=?1 AND EXISTS(SELECT 1 FROM sessions WHERE token_hash=?3)`)
      .bind(device.token_hash, timestamp, tokenHash)
  ]);
  if (!(result[1].meta?.changes || result[1].changes)) {
    throw new HttpError(401, 'This remembered sign-in is no longer available. Sign in with Discord again.', 'REMEMBERED_SESSION_UNAVAILABLE');
  }
  row = await sessionRecord(token, env, timestamp);
  if (!row) throw new HttpError(401, 'Your session is no longer available. Sign in again.', 'SESSION_EXPIRED');
  const response = json(restoredSession(token, row, device));
  setBrowserCookie(response, SESSION_COOKIE, token);
  return response;
}

async function persistenceStatus(request, env) {
  requireOrigin(request, env);
  await readJSON(request, 1024);
  const user = await requireUser(request, env);
  const cookieRow = await sessionRecord(browserCookieValue(request, SESSION_COOKIE), env);
  const device = await rememberedDevice(request, env);
  return json({session: !!cookieRow && cookieRow.id === user.id,
    remembered: !!device && device.user_id === user.id,
    rememberedUntil: device && device.user_id === user.id ? device.expires_at : null});
}

async function logout(request, env) {
  requireOrigin(request, env);
  const data = request.body === null ? {} : await readJSON(request, 1024);
  const token = bearer(request, true), cookie = browserCookieValue(request, SESSION_COOKIE);
  const device = browserCookieValue(request, REMEMBER_COOKIE);
  if (data.discard === true) {
    // Cleanup for a cancelled exchange is scoped to its returned token. A late
    // cancellation must never revoke a different browser account or device.
    if (!token) throw new HttpError(401, 'The discarded session is missing.', 'SIGN_IN_REQUIRED');
    const hash = await sha256(token);
    let matches = [];
    try {
      matches = (await env.DB.prepare(`SELECT token_hash FROM auth_remembered_devices
        WHERE EXISTS (SELECT 1 FROM json_each(session_hashes_json) WHERE value=?1)`).bind(hash).all()).results;
    } catch (error) { if (!missingRememberedTable(error)) throw error; }
    for (const match of matches) await revokeRememberedDevice(env, match.token_hash);
    await env.DB.prepare('DELETE FROM sessions WHERE token_hash=?1').bind(hash).run();
    const response = json({ok: true});
    if (cookie === token) setBrowserCookie(response, SESSION_COOKIE, null, 0);
    if (device && matches.map(match => match.token_hash).includes(await sha256(device))) {
      setBrowserCookie(response, REMEMBER_COOKIE, null, 0);
    }
    return response;
  }
  if (device) await revokeRememberedDevice(env, await sha256(device));
  for (const value of new Set([token, cookie].filter(Boolean))) {
    await env.DB.prepare('DELETE FROM sessions WHERE token_hash=?1').bind(await sha256(value)).run();
  }
  const response = json({ ok: true });
  setBrowserCookie(response, SESSION_COOKIE, null, 0);
  setBrowserCookie(response, REMEMBER_COOKIE, null, 0);
  return response;
}

async function handleAuth(request, env, ctx) {
  const path = new URL(request.url).pathname;
  const routes = {
    '/auth/launcher/start': ['POST', launcherStart],
    '/auth/launcher/authorize': ['GET', launcherAuthorize],
    '/auth/launcher/refresh': ['POST', launcherRefresh],
    '/auth/launcher/logout': ['POST', launcherLogout],
    '/auth/launcher/poll': ['POST', launcherPoll],
    '/auth/launcher/cancel': ['POST', launcherCancel],
    '/auth/discord/start': ['GET', startDiscord],
    '/auth/discord/callback': ['GET', discordCallback],
    '/auth/exchange': ['POST', exchange],
    '/auth/refresh': ['POST', refreshSession],
    '/auth/persistence': ['POST', persistenceStatus],
    '/auth/logout': ['POST', logout],
    '/auth/me': ['GET', async (req, bindings) => json({ user: await requireUser(req, bindings) })]
  };
  const route = routes[path];
  if (!route) return null;
  if (request.method !== route[0]) throw new HttpError(405, 'This method is not supported.', 'METHOD_NOT_ALLOWED');
  if (path === '/auth/discord/start' || path === '/auth/exchange') await limitAuth(request, env);
  return route[1](request, env, ctx);
}

/** Run from the Worker's scheduled handler; authentication checks do not rely on cleanup. */
async function cleanupAuth(env) {
  const timestamp = now();
  await env.DB.batch([
    env.DB.prepare('DELETE FROM oauth_flows WHERE expires_at<=?1').bind(timestamp),
    env.DB.prepare('DELETE FROM login_grants WHERE expires_at<=?1').bind(timestamp),
    env.DB.prepare('DELETE FROM sessions WHERE idle_expires_at<=?1 OR absolute_expires_at<=?1 OR revoked_at IS NOT NULL').bind(timestamp)
  ]);
  try {
    await env.DB.prepare('DELETE FROM auth_remembered_devices WHERE expires_at<=?1').bind(timestamp).run();
  } catch (error) { if (!missingRememberedTable(error)) throw error; }
}

return Object.freeze({AUTH_REVISION,requireUser,handleAuth,cleanupAuth});
})();

const __dkrFtps = (() => {
/*
 * Explicit FTPS transport for the owner's confirmed Fasthosts directories.
 * Credentials are supplied only by private Worker configuration. This module
 * never logs commands, credentials, server replies, or socket exceptions.
 *
 * The caller must hold its database upload lease for the entire withSession
 * callback. A transport success is not a publication decision. No arbitrary
 * host, directory, existing filename, or cleartext fallback is supported.
 *
 * Protocol references: RFC 959 (replies/APPE), RFC 2428 (EPSV), RFC 3659
 * (SIZE/REST), RFC 4217 (AUTH TLS/PBSZ/PROT and data-channel TLS).
 */

const FTPS_REVISION = '20261018-3';
const FTPS_TARGET = Object.freeze({ hostname: 'ftp.fasthosts.co.uk', port: 21 });
const FTPS_ROOTS = Object.freeze({
  download: '/positron-portal.com>htdocs>dkr-r>downloads',
  image: '/positron-portal.com>htdocs>dkr-r>images'
});
const FTPS_MAX_TRANSFER_BYTES = 8 * 1024 * 1024;

const FTPS_CATEGORIES = new Set(['Tracks', 'Characters', 'Legacy', 'MagicCodes', 'TexturePacks']);
const FTPS_DOWNLOAD_EXTENSIONS = new Set(['zip', 'rtz', 'dkrmap', 'xdelta', 'nrm', 'dkrcode', 'bps', 'ips', '7z']);
const FTPS_IMAGE_EXTENSIONS = new Set(['png', 'jpg', 'jpeg', 'webp']);
const FTPS_MAX_LINE_BYTES = 4096;
const FTPS_MAX_REPLY_BYTES = 32768;
const FTPS_MAX_REPLY_LINES = 64;
const FTPS_MAX_BUFFER_BYTES = 65536;
const FTPS_MAX_LIST_BYTES = 256 * 1024;
const FTPS_ENCODER = new TextEncoder();
const FTPS_ACCESS_FALLBACK = 'The FTP account could not access the required upload file or directory. It may be missing, inaccessible to this account, or addressed from a different FTP login root. Check the exact Remote site path using the same restricted FTP account.';

class FtpsError extends Error {
  /**
   * @param {string} code
   * @param {string} message
   * @param {{ftpCode?: number, retryable?: boolean, ftpCommand?: string, ftpPath?: string}} [options]
   */
  constructor(code, message, { ftpCode, retryable = false, ftpCommand, ftpPath } = {}) {
    super(message);
    this.name = 'FtpsError';
    this.code = code;
    this.retryable = Boolean(retryable);
    if (Number.isInteger(ftpCode)) this.ftpCode = ftpCode;
    const context = ftpsDiagnosticContext(ftpCommand, ftpPath);
    if (context) {
      this.ftpCommand = context.ftpCommand;
      this.ftpPath = context.ftpPath;
    }
  }
}

function ftpsFailure(code, message, options) {
  return new FtpsError(code, message, options);
}

function ftpsSafeCredential(value, label) {
  const limit = label === 'username' ? 256 : 1024;
  if (typeof value !== 'string' || value.length < 1 || value.length > limit || /[^\x20-\x7e]/.test(value)) {
    throw ftpsFailure('ftp_not_configured', 'The private FTP account settings are missing or contain unsupported characters.');
  }
  return value;
}

function ftpsDirectory(path) {
  if (typeof path !== 'string') throw ftpsFailure('ftp_path_invalid', 'The upload directory is not permitted.');
  for (const [kind, root] of Object.entries(FTPS_ROOTS)) {
    if (path === root) return { kind, root, category: null };
    if (path.startsWith(root + '/') && FTPS_CATEGORIES.has(path.slice(root.length + 1))) {
      return { kind, root, category: path.slice(root.length + 1) };
    }
  }
  throw ftpsFailure('ftp_path_invalid', 'The upload directory is not permitted.');
}

function ftpsFile(path, temporaryOnly = false) {
  if (typeof path !== 'string' || path.length > 256) {
    throw ftpsFailure('ftp_path_invalid', 'The upload filename is not permitted.');
  }
  const slash = path.lastIndexOf('/');
  const directory = ftpsDirectory(path.slice(0, slash));
  const name = path.slice(slash + 1);
  const match = /^dkr-([0-9a-f]{32})-([a-z0-9][a-z0-9_-]{0,69})\.([a-z0-9]+)(\.part)?$/.exec(name);
  const extensions = directory.kind === 'download' ? FTPS_DOWNLOAD_EXTENSIONS : FTPS_IMAGE_EXTENSIONS;
  if (!directory.category || !match || !extensions.has(match[3]) || (temporaryOnly && !match[4])) {
    throw ftpsFailure('ftp_path_invalid', 'Only generated upload filenames in the approved category folders are permitted.');
  }
  return { ...directory, name, temporary: Boolean(match[4]) };
}

/**
 * Revalidate diagnostics independently of the command sender. Only the fixed
 * owner-approved paths can leave this module as error metadata. No server
 * reply, credential command, arbitrary filename or alternate root is accepted.
 * @param {unknown} verb
 * @param {unknown} path
 * @returns {{ftpCommand: string, ftpPath: string} | null}
 */
function ftpsDiagnosticContext(verb, path) {
  if (typeof verb !== 'string' || typeof path !== 'string') return null;
  try {
    if (verb === 'CWD' || verb === 'NLST') ftpsDirectory(path);
    else if (verb === 'SIZE' || verb === 'RETR') ftpsFile(path);
    else if (verb === 'APPE' || verb === 'RNFR' || verb === 'DELE') ftpsFile(path, true);
    else if (verb === 'RNTO') { if (ftpsFile(path).temporary) return null; }
    else return null;
    return { ftpCommand: verb, ftpPath: path };
  } catch (_) { return null; }
}

/** @returns {{ftpCommand?: string, ftpPath?: string}} */
function ftpsCommandContext(command) {
  if (typeof command !== 'string') return {};
  const match = /^(CWD|NLST|SIZE|APPE|RETR|RNFR|RNTO|DELE) (.+)$/.exec(command);
  return match ? ftpsDiagnosticContext(match[1], match[2]) || {} : {};
}

/**
 * Format a safe access diagnostic for the website. Do not trust error.message
 * or expose server replies. Revalidation also covers forged/mutated metadata.
 * @param {unknown} error
 * @returns {string}
 */
function ftpsAccessMessage(error) {
  try {
    if (!(error instanceof FtpsError) || error.code !== 'ftp_access') {
      return FTPS_ACCESS_FALLBACK;
    }
    const ftpCode = error.ftpCode;
    if (typeof ftpCode !== 'number' || ![550, 553, 532].includes(ftpCode)) return FTPS_ACCESS_FALLBACK;
    const context = ftpsDiagnosticContext(error.ftpCommand, error.ftpPath);
    if (!context) return FTPS_ACCESS_FALLBACK;
    /** @type {Record<string, string>} */
    const actions = {
      CWD: 'It could not enter that directory. Check that the folder exists and the account can open it.',
      NLST: 'It could not list that directory. Check that the folder exists and the account can list its contents.',
      SIZE: 'It could not inspect that file size. Check that the file exists and the account can read it.',
      APPE: 'It could not create or append to that temporary file. Check the parent folder and the account\'s create/append access.',
      RETR: 'It could not read back that file. Check that the file exists and the account can download it.',
      RNFR: 'It could not select that temporary file for renaming. Check that it exists and the account permits renaming it.',
      RNTO: 'It could not rename the upload to that final path. Check the destination folder and the account\'s rename access.',
      DELE: 'It could not remove that temporary file. Check whether the file still exists and the account permits deletion.'
    };
    const reason = ftpCode === 553
      ? 'FTP 553 can indicate a filename or destination policy; this reply does not prove a permissions problem.'
      : ftpCode === 532
        ? 'FTP 532 can mean that the hosting service requires additional account authorization for storage.'
        : 'FTP 550 can mean a missing path or denied access; this reply alone does not distinguish them.';
    return 'Fasthosts returned FTP ' + ftpCode + ' for ' + context.ftpCommand + ' ' + context.ftpPath + '. ' +
      actions[context.ftpCommand] + ' ' + reason + ' Compare the exact Remote site path in FileZilla using the same restricted FTP account; its login root may differ.';
  } catch (_) { return FTPS_ACCESS_FALLBACK; }
}

function ftpsInteger(value, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw ftpsFailure('ftp_range_invalid', 'The requested file offset or transfer size is invalid.');
  }
  return value;
}

function ftpsCloseSocket(socket) {
  if (!socket) return Promise.resolve();
  try { return Promise.resolve(socket.close()).catch(() => {}); }
  catch (_) { return Promise.resolve(); }
}

class FtpsScope {
  constructor(timeoutMs, signal) {
    this.sockets = new Set();
    this.closing = new Map();
    this.error = null;
    this.disposed = false;
    this.signal = signal;
    this.failed = new Promise((_, reject) => { this.reject = reject; });
    this.failed.catch(() => {});
    this.timer = setTimeout(() => this.abort(ftpsFailure('ftp_timeout', 'The secure file transfer timed out. Retry to check its saved progress.', { retryable: true })), timeoutMs);
    this.onAbort = () => this.abort(ftpsFailure('ftp_cancelled', 'The secure file transfer was cancelled.', { retryable: true }));
    if (signal) {
      signal.addEventListener('abort', this.onAbort, { once: true });
      if (signal.aborted) this.onAbort();
    }
  }
  assertOpen() {
    if (this.error) throw this.error;
    if (this.disposed) throw ftpsFailure('ftp_closed', 'The secure file connection has closed.');
  }
  async wait(value) {
    this.assertOpen();
    return Promise.race([Promise.resolve(value), this.failed]);
  }
  track(socket) {
    if (!socket || !socket.readable || !socket.writable || typeof socket.close !== 'function') {
      throw ftpsFailure('ftp_network', 'The secure file service could not open a connection.', { retryable: true });
    }
    if (socket.closed && typeof socket.closed.catch === 'function') socket.closed.catch(() => {});
    if (socket.opened && typeof socket.opened.catch === 'function') socket.opened.catch(() => {});
    this.sockets.add(socket);
    if (this.error || this.disposed) this.closeOne(socket);
    return socket;
  }
  replace(previous, next) {
    this.track(next);
    this.sockets.delete(previous); // startTls() invalidates the old socket.
    return next;
  }
  closeOne(socket) {
    this.sockets.delete(socket);
    if (!this.closing.has(socket)) this.closing.set(socket, ftpsCloseSocket(socket));
    return this.closing.get(socket);
  }
  abort(error) {
    if (this.error || this.disposed) return;
    this.error = error;
    this.reject(error);
    for (const socket of [...this.sockets]) this.closeOne(socket);
  }
  async dispose() {
    clearTimeout(this.timer);
    if (this.signal) this.signal.removeEventListener('abort', this.onAbort);
    this.disposed = true;
    this.reject(this.error || ftpsFailure('ftp_closed', 'The secure file connection has closed.'));
    for (const socket of [...this.sockets]) this.closeOne(socket);
    this.sockets.clear();
    let timer;
    try {
      await Promise.race([
        Promise.all(this.closing.values()),
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(ftpsFailure('ftp_close_pending', 'The file connection is still closing. Wait for the upload lock to expire before retrying.', { retryable: true })), 2000);
        })
      ]);
    } finally { clearTimeout(timer); }
  }
}

class FtpsReplyReader {
  constructor(socket, scope) {
    this.reader = socket.readable.getReader();
    this.scope = scope;
    this.pending = '';
  }
  release() {
    if (this.pending.length) throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent unexpected data while starting encryption.');
    this.reader.releaseLock();
  }
  async line() {
    while (true) {
      const end = this.pending.indexOf('\n');
      if (end >= 0) {
        if (end === 0 || this.pending[end - 1] !== '\r' || end - 1 > FTPS_MAX_LINE_BYTES) {
          throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an invalid or oversized reply.');
        }
        const value = this.pending.slice(0, end - 1);
        this.pending = this.pending.slice(end + 1);
        if (value.includes('\r')) throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an invalid reply.');
        return value;
      }
      if (this.pending.length > FTPS_MAX_LINE_BYTES + 1) {
        throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an oversized reply.');
      }
      const { value, done } = await this.scope.wait(this.reader.read());
      if (done) throw ftpsFailure('ftp_connection_closed', 'The FTP server closed the secure connection before confirming the operation.', { retryable: true });
      if (!(value instanceof Uint8Array) || this.pending.length + value.byteLength > FTPS_MAX_BUFFER_BYTES) {
        throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an oversized reply.');
      }
      // FTP replies are ASCII here. Reject Telnet escapes and other control
      // bytes instead of accidentally interpreting an injected second reply.
      let text = '';
      for (const byte of value) {
        if ((byte < 32 && byte !== 9 && byte !== 10 && byte !== 13) || byte > 126) {
          throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent unsupported control-channel data.');
        }
        text += String.fromCharCode(byte);
      }
      this.pending += text;
    }
  }
  async reply() {
    const first = await this.line();
    const match = /^([1-5][0-9]{2})([ -])/.exec(first);
    if (!match) throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an invalid reply code.');
    const code = Number(match[1]);
    const lines = [first];
    let bytes = first.length + 2;
    if (match[2] === '-') {
      while (true) {
        const next = await this.line();
        bytes += next.length + 2;
        lines.push(next);
        if (bytes > FTPS_MAX_REPLY_BYTES || lines.length > FTPS_MAX_REPLY_LINES) {
          throw ftpsFailure('ftp_reply_invalid', 'The FTP server sent an oversized multiline reply.');
        }
        if (next.startsWith(match[1] + ' ')) break;
      }
    }
    return { code, line: lines[lines.length - 1], text: lines.join('\n') };
  }
}

function ftpsReplyError(reply, stage, command = '') {
  const options = { ftpCode: reply.code, retryable: reply.code >= 400 && reply.code < 500, ...ftpsCommandContext(command) };
  if (reply.code === 530 || stage === 'login') {
    return ftpsFailure('ftp_auth', 'Fasthosts did not accept the private FTP account. Check its username, password and access settings.', options);
  }
  if (/session.{0,32}(reus|resum|cach)|(reus|resum).{0,32}session/i.test(reply.text)) {
    return ftpsFailure('ftp_session_reuse', 'Fasthosts requires TLS session reuse for file transfers. This Worker transport cannot provide that requirement; a compatible secure transfer service is needed.', options);
  }
  if (reply.code === 522 || reply.code === 521 || reply.code === 536 || stage === 'data_tls') {
    return ftpsFailure('ftp_data_tls', 'Fasthosts refused the encrypted data connection. Its TLS or session-reuse requirements need to be checked; unencrypted transfer is disabled.', options);
  }
  if (stage === 'control_tls') {
    return ftpsFailure('ftp_tls', 'Fasthosts did not accept the required encrypted FTP connection. No account credentials were sent.', options);
  }
  if (stage === 'passive') {
    return ftpsFailure('ftp_passive', 'Fasthosts could not open a passive file-transfer connection.', options);
  }
  if (stage === 'restart') {
    return ftpsFailure('ftp_resume_unavailable', 'The FTP server could not read back the saved upload tail. Resumable upload verification is unavailable.', options);
  }
  if (reply.code === 550 || reply.code === 553 || reply.code === 532) {
    return ftpsFailure('ftp_access', FTPS_ACCESS_FALLBACK, options);
  }
  return ftpsFailure('ftp_transfer', 'The FTP server did not confirm the requested file operation. Retry to check its saved progress.', options);
}

async function ftpsDefaultConnect(address, options) {
  // Lazy built-in import keeps this source importable by Node-based tests.
  // The Worker bundle preserves this runtime import unchanged.
  const sockets = await import('cloudflare:sockets');
  return sockets.connect(address, options);
}

class FtpsSession {
  constructor(scope, connect, username, password) {
    this.scope = scope;
    this.connect = connect;
    this.username = username;
    this.password = password;
    this.control = null;
    this.replies = null;
    this.writer = null;
    this.busy = false;
  }
  async operation(callback) {
    this.scope.assertOpen();
    if (this.busy) throw ftpsFailure('ftp_busy', 'File operations on one FTP connection must run in order.');
    this.busy = true;
    try {
      return await this.scope.wait(callback());
    } catch (error) {
      const safe = error instanceof FtpsError ? error : ftpsFailure('ftp_network', 'The secure file connection failed before the operation was confirmed. Retry to check its saved progress.', { retryable: true });
      this.scope.abort(safe);
      throw safe;
    } finally {
      this.busy = false;
    }
  }
  async socket(port) {
    const pending = Promise.resolve().then(() => {
      this.scope.assertOpen();
      return this.connect({ hostname: FTPS_TARGET.hostname, port }, { secureTransport: 'starttls', allowHalfOpen: false });
    }).then(socket => this.scope.track(socket));
    // Track even a connector that resolves after a deadline so it is closed.
    const socket = await this.scope.wait(pending);
    await this.scope.wait(socket.opened);
    return socket;
  }
  upgrade(socket, stage) {
    try {
      if (typeof socket.startTls !== 'function') throw new Error('TLS API unavailable');
      return this.scope.replace(socket, socket.startTls());
    } catch (_) {
      throw ftpsFailure(stage === 'control' ? 'ftp_tls' : 'ftp_data_tls', stage === 'control'
        ? 'The secure FTP handshake could not be started. No account credentials were sent.'
        : 'The encrypted file connection could not be started. Unencrypted transfer is disabled.');
    }
  }
  async write(command) {
    this.scope.assertOpen();
    // All commands are assembled internally. This guard additionally prevents
    // accidental future command injection when extending this adapter.
    if (typeof command !== 'string' || command.length > 1400 || /[^\x20-\x7e]/.test(command)) {
      throw ftpsFailure('ftp_command_invalid', 'The secure file command is invalid.');
    }
    await this.scope.wait(this.writer.write(FTPS_ENCODER.encode(command + '\r\n')));
  }
  async command(command, accepted, stage = 'command') {
    await this.write(command);
    const reply = await this.replies.reply();
    if (!accepted.includes(reply.code)) throw ftpsReplyError(reply, stage, command);
    return reply;
  }
  async open() {
    return this.operation(async () => {
      this.control = await this.socket(FTPS_TARGET.port);
      this.replies = new FtpsReplyReader(this.control, this.scope);
      this.writer = this.control.writable.getWriter();
      let greeting = await this.replies.reply();
      if (greeting.code === 120) greeting = await this.replies.reply();
      if (greeting.code !== 220) throw ftpsReplyError(greeting, 'greeting');
      await this.command('AUTH TLS', [234], 'control_tls');
      this.replies.release();
      this.writer.releaseLock();
      this.control = this.upgrade(this.control, 'control');
      try { await this.scope.wait(this.control.opened); }
      catch (error) {
        if (error instanceof FtpsError) throw error;
        throw ftpsFailure('ftp_tls', 'Fasthosts could not establish a trusted encrypted connection. No account credentials were sent.');
      }
      this.replies = new FtpsReplyReader(this.control, this.scope);
      this.writer = this.control.writable.getWriter();
      const user = await this.command('USER ' + this.username, [230, 331], 'login');
      if (user.code === 331) await this.command('PASS ' + this.password, [230], 'login');
      await this.command('PBSZ 0', [200], 'data_tls');
      await this.command('PROT P', [200], 'data_tls');
      await this.command('TYPE I', [200]);
      // Authentication values are no longer needed by the connection object.
      this.username = null;
      this.password = null;
    });
  }
  async passiveSocket() {
    await this.write('EPSV');
    const epsv = await this.replies.reply();
    let port;
    if (epsv.code === 229) {
      const tuple = /\(([\x21-\x7e])\1\1([0-9]{1,5})\1\)/.exec(epsv.line);
      if (!tuple) throw ftpsFailure('ftp_passive_invalid', 'The FTP server returned an invalid passive connection address.');
      port = Number(tuple[2]);
    } else {
      if (![500, 501, 502, 504, 522].includes(epsv.code)) throw ftpsReplyError(epsv, 'passive');
      const pasv = await this.command('PASV', [227], 'passive');
      const tuple = /\(([0-9]{1,3}),([0-9]{1,3}),([0-9]{1,3}),([0-9]{1,3}),([0-9]{1,3}),([0-9]{1,3})\)/.exec(pasv.line);
      if (!tuple || tuple.slice(1).some(value => Number(value) > 255)) {
        throw ftpsFailure('ftp_passive_invalid', 'The FTP server returned an invalid passive connection address.');
      }
      port = Number(tuple[5]) * 256 + Number(tuple[6]);
      // Deliberately ignore the PASV-supplied IP. Both control and data use
      // the fixed, certificate-validated Fasthosts hostname.
    }
    if (!Number.isInteger(port) || port < 1024 || port > 65535) {
      throw ftpsFailure('ftp_passive_invalid', 'The FTP server returned an unsafe passive connection port.');
    }
    return this.socket(port);
  }
  async transfer(command, action, restart = null) {
    let data = await this.passiveSocket();
    try {
      if (restart !== null) await this.command('REST ' + restart, [350], 'restart');
      // Send the transfer command before waiting for the data TLS handshake.
      // Some servers start that handshake only after this command arrives.
      await this.write(command);
      data = this.upgrade(data, 'data');
      const preliminary = await this.replies.reply();
      if (![125, 150].includes(preliminary.code)) throw ftpsReplyError(preliminary, 'transfer', command);
      const completion = this.replies.reply().then(reply => {
        if (![226, 250].includes(reply.code)) throw ftpsReplyError(reply, 'transfer', command);
        return reply;
      });
      completion.catch(() => {});
      const earlyFailure = completion.then(() => new Promise(() => {}));
      earlyFailure.catch(() => {});
      try {
        await this.scope.wait(Promise.race([data.opened, earlyFailure]));
      } catch (error) {
        if (error instanceof FtpsError) throw error;
        // The encrypted control channel may provide a more specific 522
        // session-reuse reason immediately after a failed TLS handshake.
        let timer;
        const diagnostic = await this.scope.wait(Promise.race([
          completion.then(() => null, failure => failure),
          new Promise(resolve => { timer = setTimeout(() => resolve(null), 250); })
        ])).finally(() => clearTimeout(timer));
        if (diagnostic instanceof FtpsError) throw diagnostic;
        throw ftpsFailure('ftp_data_tls', 'The encrypted file connection failed. Fasthosts may require TLS session reuse that this Worker transport cannot provide.', { retryable: false });
      }
      const result = await this.scope.wait(Promise.race([action(data), earlyFailure]));
      await this.scope.wait(completion);
      return result;
    } finally {
      this.scope.closeOne(data);
    }
  }
  async internalSize(path) {
    const response = await this.command('SIZE ' + path, [213, 550]);
    // FTP 550 is intentionally not described as proof that a path is absent:
    // it can also mean that this account cannot inspect it. Only unique,
    // backend-owned generated filenames may reach a write operation.
    if (response.code === 550) return null;
    const match = /^213 ([0-9]+)\s*$/.exec(response.line);
    const size = match ? Number(match[1]) : NaN;
    if (!Number.isSafeInteger(size) || size < 0) throw ftpsFailure('ftp_size_invalid', 'The FTP server returned an invalid file size.');
    return size;
  }
  size(path) {
    ftpsFile(path);
    return this.operation(() => this.internalSize(path));
  }
  append(path, expectedOffset, source, expectedBytes) {
    ftpsFile(path, true);
    ftpsInteger(expectedOffset);
    ftpsInteger(expectedBytes, FTPS_MAX_TRANSFER_BYTES);
    if (expectedBytes < 1 || !Number.isSafeInteger(expectedOffset + expectedBytes)) {
      throw ftpsFailure('ftp_range_invalid', 'The upload chunk size is invalid.');
    }
    if (source instanceof Uint8Array) {
      if (source.byteLength !== expectedBytes) throw ftpsFailure('ftp_length_mismatch', 'The upload chunk length does not match its declared size.');
      const bytes = source;
      source = new ReadableStream({ start(controller) { controller.enqueue(bytes); controller.close(); } });
    }
    if (!source || typeof source.getReader !== 'function') throw ftpsFailure('ftp_chunk_invalid', 'The upload chunk must be binary data.');
    return this.operation(async () => {
      const size = await this.internalSize(path);
      if ((size === null && expectedOffset !== 0) || (size !== null && size !== expectedOffset)) {
        throw ftpsFailure('ftp_offset_mismatch', 'The saved file size differs from this upload chunk. Refresh the upload status before retrying.', { retryable: true });
      }
      await this.transfer('APPE ' + path, async socket => {
        const reader = source.getReader();
        const writer = socket.writable.getWriter();
        const remoteReader = socket.readable.getReader();
        let written = 0;
        let complete = false;
        try {
          while (true) {
            const next = await this.scope.wait(reader.read());
            if (next.done) break;
            if (!(next.value instanceof Uint8Array) || written + next.value.byteLength > expectedBytes) {
              throw ftpsFailure('ftp_length_mismatch', 'The upload chunk contains more data than its declared size.');
            }
            if (next.value.byteLength) {
              await this.scope.wait(writer.write(next.value));
              written += next.value.byteLength;
            }
          }
          if (written !== expectedBytes) throw ftpsFailure('ftp_length_mismatch', 'The upload chunk ended before its declared size was received.', { retryable: true });
          await this.scope.wait(writer.close());
          while (true) {
            const next = await this.scope.wait(remoteReader.read());
            if (next.done) break;
            if (!(next.value instanceof Uint8Array) || next.value.byteLength) {
              throw ftpsFailure('ftp_reply_invalid', 'The upload data connection returned unexpected content.');
            }
          }
          complete = true;
        } finally {
          if (!complete) { try { Promise.resolve(reader.cancel()).catch(() => {}); } catch (_) {} }
          try { reader.releaseLock(); } catch (_) {}
          try { writer.releaseLock(); } catch (_) {}
          try { remoteReader.releaseLock(); } catch (_) {}
        }
      });
      const finalSize = await this.internalSize(path);
      if (finalSize !== expectedOffset + expectedBytes) {
        throw ftpsFailure('ftp_length_mismatch', 'Fasthosts did not confirm the complete upload chunk. Retry to check its saved progress.', { retryable: true });
      }
      return { size: finalSize, bytesWritten: expectedBytes };
    });
  }
  readRange(path, offset, length) {
    ftpsFile(path);
    ftpsInteger(offset);
    ftpsInteger(length, FTPS_MAX_TRANSFER_BYTES);
    if (!Number.isSafeInteger(offset + length)) throw ftpsFailure('ftp_range_invalid', 'The verification range is invalid.');
    return this.operation(async () => {
      const size = await this.internalSize(path);
      if (size === null || size !== offset + length) {
        throw ftpsFailure('ftp_offset_mismatch', 'The saved upload tail changed before it could be verified.', { retryable: true });
      }
      if (length === 0) return new Uint8Array(0);
      const bytes = new Uint8Array(length);
      const received = await this.transfer('RETR ' + path, async socket => {
        const reader = socket.readable.getReader();
        let used = 0;
        try {
          while (true) {
            const next = await this.scope.wait(reader.read());
            if (next.done) break;
            if (!(next.value instanceof Uint8Array) || used + next.value.byteLength > length) {
              throw ftpsFailure('ftp_length_mismatch', 'The FTP server sent more verification data than requested.');
            }
            bytes.set(next.value, used);
            used += next.value.byteLength;
          }
          return used;
        } finally { try { reader.releaseLock(); } catch (_) {} }
      }, offset > 0 ? offset : null);
      if (received !== length) throw ftpsFailure('ftp_length_mismatch', 'The upload verification ended before the expected data was received.', { retryable: true });
      return bytes;
    });
  }
  rename(from, to) {
    ftpsFile(from, true);
    ftpsFile(to);
    if (from !== to + '.part') throw ftpsFailure('ftp_path_invalid', 'An upload can only be renamed to its own final filename.');
    return this.operation(async () => {
      const size = await this.internalSize(from);
      if (size === null) throw ftpsFailure('ftp_file_unavailable', 'The temporary upload could not be found or inspected.');
      if (await this.internalSize(to) !== null) throw ftpsFailure('ftp_rename_conflict', 'A file already exists at this upload destination. Refresh the upload status before retrying.', { retryable: true });
      await this.command('RNFR ' + from, [350]);
      await this.command('RNTO ' + to, [250]);
      if (await this.internalSize(to) !== size) throw ftpsFailure('ftp_length_mismatch', 'The final upload size could not be confirmed. Refresh the upload status before retrying.', { retryable: true });
      return { size };
    });
  }
  remove(path) {
    ftpsFile(path, true);
    return this.operation(async () => {
      const response = await this.command('DELE ' + path, [250, 550]);
      // A 550 refusal is not reported as a successful deletion.
      if (response.code === 550) throw ftpsReplyError(response, 'remove', 'DELE ' + path);
      return true;
    });
  }
  checkDirectory(path) {
    ftpsDirectory(path);
    return this.operation(async () => {
      await this.command('CWD ' + path, [250]);
      // CWD confirms access without downloading or modifying any files.
      return true;
    });
  }
  list(path) {
    ftpsDirectory(path);
    return this.operation(async () => {
      const chunks = [];
      let total = 0;
      await this.transfer('NLST ' + path, async socket => {
        const reader = socket.readable.getReader();
        try {
          while (true) {
            const next = await this.scope.wait(reader.read());
            if (next.done) break;
            if (!(next.value instanceof Uint8Array) || total + next.value.byteLength > FTPS_MAX_LIST_BYTES) {
              throw ftpsFailure('ftp_listing_too_large', 'The FTP directory listing exceeded its safe inspection limit.');
            }
            chunks.push(next.value);
            total += next.value.byteLength;
          }
        } finally { try { reader.releaseLock(); } catch (_) {} }
      });
      const bytes = new Uint8Array(total);
      let offset = 0;
      for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
      const text = new TextDecoder('utf-8').decode(bytes);
      if (/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(text)) throw ftpsFailure('ftp_listing_invalid', 'The FTP server returned an invalid directory listing.');
      const names = text.split(/\r?\n/).filter(Boolean);
      if (names.length > 2048 || names.some(name => name.length > 1024)) throw ftpsFailure('ftp_listing_too_large', 'The FTP directory listing exceeded its safe inspection limit.');
      return names;
    });
  }
  api() {
    return Object.freeze({
      size: this.size.bind(this), append: this.append.bind(this),
      readRange: this.readRange.bind(this), rename: this.rename.bind(this),
      remove: this.remove.bind(this), checkDirectory: this.checkDirectory.bind(this),
      list: this.list.bind(this)
    });
  }
}

/**
 * @param {{username?: string, password?: string,
 * connect?: (...args: Parameters<typeof ftpsDefaultConnect>) => ReturnType<typeof ftpsDefaultConnect> | Awaited<ReturnType<typeof ftpsDefaultConnect>>,
 * timeoutMs?: number, signal?: AbortSignal}} [options]
 */
function createFtpsClient({ username, password, connect = ftpsDefaultConnect, timeoutMs = 45000, signal } = {}) {
  const privateUsername = ftpsSafeCredential(username, 'username');
  const privatePassword = ftpsSafeCredential(password, 'password');
  if (typeof connect !== 'function' || !Number.isInteger(timeoutMs) || timeoutMs < 10 || timeoutMs > 60000 ||
      (signal && (typeof signal.addEventListener !== 'function' || typeof signal.removeEventListener !== 'function'))) {
    throw ftpsFailure('ftp_not_configured', 'The private secure transfer configuration is invalid.');
  }
  return Object.freeze({
    async withSession(callback) {
      if (typeof callback !== 'function') throw ftpsFailure('ftp_operation_invalid', 'A secure file operation is required.');
      const scope = new FtpsScope(timeoutMs, signal);
      const session = new FtpsSession(scope, connect, privateUsername, privatePassword);
      try {
        await session.open();
        return await scope.wait(callback(session.api()));
      } finally {
        session.username = null;
        session.password = null;
        await scope.dispose();
      }
    }
  });
}

return Object.freeze({FTPS_REVISION,FTPS_TARGET,FTPS_ROOTS,FTPS_MAX_TRANSFER_BYTES,FtpsError,ftpsAccessMessage,createFtpsClient});
})();

const __dkrUploads = (() => {
// Private, resumable staff uploads to the confirmed Fasthosts directories.
// Every transport session has a deadline shorter than its D1 fencing lease.
// Chunk intent is durable BEFORE APPE; remote bytes are read back before acknowledgement.
const { HttpError, json, readJSON, now, requireRole, requireOrigin, baseURL, validatePublicURL } = __dkrCommon;
const { requireUser } = __dkrAuth;
const { createFtpsClient, ftpsAccessMessage, FTPS_ROOTS } = __dkrFtps;
const UPLOADS_REVISION = '20261018-ftps-3';
const UPLOAD_CHUNK_BYTES = 4 * 1024 * 1024;
const UPLOAD_LIMITS = Object.freeze({ download: 2 * 1024 * 1024 * 1024, image: 20 * 1024 * 1024 });
const UPLOAD_FORMATS = Object.freeze({
  download: Object.freeze(['.zip', '.rtz', '.dkrmap', '.xdelta', '.nrm', '.dkrcode', '.bps', '.ips', '.7z']),
  image: Object.freeze(['.png', '.jpg', '.jpeg', '.webp'])
});
const UPLOAD_FOLDERS = Object.freeze({ tracks: 'Tracks', characters: 'Characters', legacy: 'Legacy', magic: 'MagicCodes', textures: 'TexturePacks' });
// Use the adapter's exact approved roots for transfer, cleanup and diagnostics.
const UPLOAD_ROOTS = FTPS_ROOTS;
const UPLOAD_TTL = 72 * 60 * 60, UPLOAD_LEASE = 120, UPLOAD_TRANSPORT_MS = 45000, UPLOAD_OPEN_LIMIT = 4;
const UPLOAD_ACTIVE = new Set(['open', 'completing', 'cancel_pending']);
const uploadChanges = result => Number(result?.meta?.changes ?? result?.changes ?? 0);
const uploadHex = bytes => Array.from(bytes, n => n.toString(16).padStart(2, '0')).join('');
const uploadNonce = () => uploadHex(crypto.getRandomValues(new Uint8Array(16)));
const uploadHash = async bytes => uploadHex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));
const uploadStatement = (db, sql, args = []) => db.prepare(sql).bind(...args);
const uploadFirst = (db, sql, args = []) => uploadStatement(db, sql, args).first();
const uploadAll = async (db, sql, args = []) => (await uploadStatement(db, sql, args).all()).results || [];
const uploadFail = (status, message, code) => { throw new HttpError(status, message, code); };
const uploadMissingTables = error => /no such table\s*:\s*(?:main\.)?library_upload_(?:jobs|chunks)/i.test(String(error?.message || error));

function uploadDatabase(env) {
  if (!env.DB) uploadFail(503, 'The upload database has not been connected.', 'upload_storage_unavailable');
  return env.DB.withSession ? env.DB.withSession('first-primary') : env.DB;
}

function uploadPrivateConfig(env) {
  if (env.FTP_UPLOADS_ENABLED !== 'true') uploadFail(503, 'File uploads are not enabled yet. Existing hosted files can still be selected.', 'uploads_disabled');
  if (typeof env.FTP_USERNAME !== 'string' || !env.FTP_USERNAME || env.FTP_USERNAME.length > 256 ||
      /[^\x20-\x7e]/.test(env.FTP_USERNAME) || typeof env.FTP_PASSWORD !== 'string' || !env.FTP_PASSWORD ||
      env.FTP_PASSWORD.length > 1024 || /[^\x20-\x7e]/.test(env.FTP_PASSWORD)) {
    uploadFail(503, 'The private FTP username and password need to be configured in Cloudflare using printable ASCII characters.', 'upload_configuration_required');
  }
}

function uploadRelative(kind, category, fileName) {
  return (kind === 'image' ? 'images' : 'downloads') + '/' + UPLOAD_FOLDERS[category] + '/' + fileName;
}

function uploadPublicURL(env, relative, kind) {
  const url = new URL(relative, new URL('./', baseURL(env))).href;
  try { return validatePublicURL(url, env, kind); }
  catch (_) { uploadFail(503, 'The generated upload address must be included in the library\'s approved download and image locations.', 'upload_configuration_required'); }
}

function uploadAssertRecord(row) {
  const suffix = String(row.id || '').match(/^up_([0-9a-f]{32})$/)?.[1];
  if (!suffix || !Object.hasOwn(UPLOAD_FOLDERS, row.category) || !Object.hasOwn(UPLOAD_FORMATS, row.kind) ||
      !UPLOAD_FORMATS[row.kind].includes(row.format) ||
      !new RegExp('^dkr-' + suffix + '-[a-z0-9][a-z0-9-]{0,69}\\' + row.format + '$').test(row.file_name) ||
      row.relative_path !== uploadRelative(row.kind, row.category, row.file_name) ||
      !Number.isSafeInteger(row.total_size) || row.total_size < 1 || row.total_size > UPLOAD_LIMITS[row.kind] ||
      !Number.isSafeInteger(row.byte_offset) || row.byte_offset < 0 || row.byte_offset > row.total_size) {
    uploadFail(503, 'This upload contains inconsistent private metadata. Ask an administrator to inspect it.', 'upload_metadata_invalid');
  }
  return row;
}

function uploadRemote(row, temporary = false) {
  uploadAssertRecord(row);
  return UPLOAD_ROOTS[row.kind] + '/' + UPLOAD_FOLDERS[row.category] + '/' + row.file_name + (temporary ? '.part' : '');
}

function uploadAsset(row, env) {
  uploadAssertRecord(row);
  return { id: 'ftp_' + row.id.slice(3, 27), kind: row.kind, category: row.category,
    path: row.relative_path, fileName: row.file_name, format: row.format,
    size: row.total_size, source: 'ftp', url: uploadPublicURL(env, row.relative_path, row.kind) };
}

function uploadView(row, env) {
  uploadAssertRecord(row);
  return { id: row.id, kind: row.kind, category: row.category, originalName: row.original_name,
    fileName: row.file_name, size: row.total_size, offset: row.byte_offset,
    fingerprint: row.fingerprint, chunkBytes: UPLOAD_CHUNK_BYTES, state: row.state,
    createdAt: row.created_at, updatedAt: row.updated_at, expiresAt: row.expires_at,
    errorCode: row.error_code || '', asset: row.state === 'completed' ? uploadAsset(row, env) : null };
}

async function uploadTablesReady(db) {
  try {
    await uploadFirst(db, 'SELECT id,lock_token,byte_offset FROM library_upload_jobs LIMIT 1');
    await uploadFirst(db, 'SELECT job_id,digest,verified FROM library_upload_chunks LIMIT 1');
    return true;
  } catch (_) { return false; }
}

async function uploadSettings(env, db) {
  const enabled = env.FTP_UPLOADS_ENABLED === 'true';
  let configured = true, message = 'Uploads are configured. Check the encrypted connection before the first file transfer.';
  try {
    uploadPrivateConfig(env);
    for (const kind of ['download', 'image']) {
      const name = 'dkr-' + '0'.repeat(32) + '-check' + UPLOAD_FORMATS[kind][0];
      uploadPublicURL(env, uploadRelative(kind, 'tracks', name), kind);
    }
    if (!await uploadTablesReady(db)) uploadFail(503, 'Run the upload database migration before uploading files.', 'upload_storage_unavailable');
  } catch (error) { configured = false; message = error instanceof HttpError ? error.message : 'The upload service needs to be configured.'; }
  return { enabled, configured, revision: UPLOADS_REVISION, chunkBytes: UPLOAD_CHUNK_BYTES,
    limits: UPLOAD_LIMITS, expiresAfterSeconds: UPLOAD_TTL, maxOpenJobs: UPLOAD_OPEN_LIMIT,
    formats: UPLOAD_FORMATS, categories: Object.keys(UPLOAD_FOLDERS), message };
}

function uploadObjectKeys(input, allowed) {
  if (Object.keys(input).some(key => !allowed.includes(key))) uploadFail(400, 'This upload request contains unsupported fields.', 'upload_invalid_input');
}

function uploadCreateInput(input) {
  uploadObjectKeys(input, ['fileName', 'size', 'kind', 'category', 'fingerprint', 'clientId']);
  if (!Object.hasOwn(UPLOAD_FORMATS, input.kind) || !Object.hasOwn(UPLOAD_FOLDERS, input.category)) {
    uploadFail(400, 'Choose a supported file type and library category.', 'upload_invalid_input');
  }
  if (typeof input.fileName !== 'string' || !input.fileName.trim() || input.fileName.length > 180 ||
      /[\\/\u0000-\u001f\u007f]/.test(input.fileName)) uploadFail(400, 'Choose a file with a valid filename of 180 characters or fewer.', 'upload_invalid_name');
  const originalName = input.fileName.trim(), extension = '.' + originalName.split('.').pop().toLowerCase();
  if (!UPLOAD_FORMATS[input.kind].includes(extension)) uploadFail(400, 'That file extension is not supported for this upload.', 'upload_invalid_format');
  if (!Number.isSafeInteger(input.size) || input.size < 1 || input.size > UPLOAD_LIMITS[input.kind]) {
    uploadFail(413, input.kind === 'image' ? 'Cover images must be between 1 byte and 20 MiB.' : 'Mod packages must be between 1 byte and 2 GiB.', 'upload_size_limit');
  }
  if (typeof input.fingerprint !== 'string' || !/^[0-9a-f]{64}$/.test(input.fingerprint)) uploadFail(400, 'Select the file again so its resume fingerprint can be checked.', 'upload_fingerprint_required');
  const clientId = input.clientId === undefined ? uploadNonce() : input.clientId;
  if (typeof clientId !== 'string' || !/^[0-9a-f]{32}$/.test(clientId)) uploadFail(400, 'The upload request identity is invalid.', 'upload_invalid_input');
  const base = originalName.slice(0, -extension.length).normalize('NFKD').replace(/[\u0300-\u036f]/g, '')
    .toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 70).replace(/-+$/g, '') || 'file';
  return { originalName, extension, slug: base, clientId, size: input.size, kind: input.kind, category: input.category, fingerprint: input.fingerprint };
}

function uploadSameCreation(row, input) {
  if (row.original_name !== input.originalName || row.total_size !== input.size || row.kind !== input.kind ||
      row.category !== input.category || row.fingerprint !== input.fingerprint) {
    uploadFail(409, 'This upload request identity belongs to a different file. Start a new upload.', 'upload_creation_conflict');
  }
}

async function uploadRecheckUser(request, env, expectedId, role = 'moderator') {
  // Reading a browser request body may take much longer than the initial
  // authorization. Check the same session's current account again immediately
  // before reserving a write, so a delayed body cannot retain revoked powers.
  const user = await requireUser(request, env);
  requireRole(user, role);
  if (expectedId && user.id !== expectedId) uploadFail(403, 'The upload account changed. Sign in and select the upload again.', 'upload_account_changed');
  return user;
}

async function uploadCreate(request, env, db, user) {
  const input = uploadCreateInput(await readJSON(request, 4096));
  await uploadRecheckUser(request, env, user.id);
  let row = await uploadFirst(db, 'SELECT * FROM library_upload_jobs WHERE owner_id=? AND client_id=?', [user.id, input.clientId]);
  if (row) { uploadSameCreation(row, input); return json({ upload: uploadView(row, env) }); }
  const nonce = uploadNonce(), id = 'up_' + nonce, fileName = 'dkr-' + nonce + '-' + input.slug + input.extension;
  const relative = uploadRelative(input.kind, input.category, fileName), timestamp = now();
  uploadPublicURL(env, relative, input.kind);
  await uploadStatement(db, `INSERT OR IGNORE INTO library_upload_jobs
    (id,owner_id,client_id,fingerprint,kind,category,original_name,file_name,format,relative_path,total_size,created_at,updated_at,expires_at)
    SELECT ?,?,?,?,?,?,?,?,?,?,?,?,?,? WHERE
      (SELECT COUNT(*) FROM library_upload_jobs WHERE owner_id=? AND state IN ('open','completing','cancel_pending')) < ?`,
    [id, user.id, input.clientId, input.fingerprint, input.kind, input.category, input.originalName, fileName,
      input.extension, relative, input.size, timestamp, timestamp, timestamp + UPLOAD_TTL, user.id, UPLOAD_OPEN_LIMIT]).run();
  row = await uploadFirst(db, 'SELECT * FROM library_upload_jobs WHERE owner_id=? AND client_id=?', [user.id, input.clientId]);
  if (!row) uploadFail(429, 'You already have four unfinished uploads. Resume or cancel one before starting another.', 'upload_open_limit');
  uploadSameCreation(row, input);
  return json({ upload: uploadView(row, env) }, 201);
}

async function uploadRow(db, id, user, allowAdminCancel = false) {
  const row = await uploadFirst(db, 'SELECT * FROM library_upload_jobs WHERE id=?', [id]);
  if (!row || row.owner_id !== user.id && !(allowAdminCancel && user.role === 'admin')) uploadFail(404, 'That upload could not be found for this account.', 'upload_not_found');
  return uploadAssertRecord(row);
}

async function uploadFresh(db, id) {
  const row = await uploadFirst(db, 'SELECT * FROM library_upload_jobs WHERE id=?', [id]);
  if (!row) uploadFail(404, 'That upload no longer exists.', 'upload_not_found');
  return uploadAssertRecord(row);
}

function uploadRequireOpen(row) {
  if (row.state === 'expired' || UPLOAD_ACTIVE.has(row.state) && row.expires_at <= now()) uploadFail(410, 'This unfinished upload expired after 72 hours. Cancel it to clean up, then start again.', 'upload_expired');
  if (row.state !== 'open') uploadFail(409, 'This upload is no longer accepting file chunks.', 'upload_not_open');
}

async function uploadAcquire(db, row) {
  const token = uploadNonce(), timestamp = now(), until = timestamp + UPLOAD_LEASE;
  const result = await uploadStatement(db, `UPDATE library_upload_jobs SET lock_token=?,lock_until=?
    WHERE id=? AND lock_until<=? AND state IN ('open','completing','cancel_pending')`, [token, until, row.id, timestamp]).run();
  if (!uploadChanges(result)) uploadFail(409, 'Another upload operation is still running. Wait a moment and retry.', 'upload_busy');
  const fresh = await uploadFresh(db, row.id);
  return { row: fresh, token, until };
}

async function uploadRelease(db, lease) {
  // A timed-out socket close must keep the fencing lease until its natural
  // expiry. Releasing early could let a retry append beside that old writer.
  if (lease.retain) return;
  await uploadStatement(db, 'UPDATE library_upload_jobs SET lock_token=\'\',lock_until=0 WHERE id=? AND lock_token=?', [lease.row.id, lease.token]).run();
}

async function uploadFence(db, lease) {
  const row = await uploadFirst(db, 'SELECT id FROM library_upload_jobs WHERE id=? AND lock_token=? AND lock_until>?', [lease.row.id, lease.token, now()]);
  if (!row) uploadFail(409, 'This upload operation lost its reservation. Check its progress before retrying.', 'upload_lease_lost');
}

async function uploadTransport(env, options, lease, work, signal) {
  const remaining = lease ? (lease.until - now() - 10) * 1000 : UPLOAD_TRANSPORT_MS;
  if (remaining < 1000) uploadFail(409, 'This upload operation took too long to start. Check its progress before retrying.', 'upload_lease_lost');
  const factory = options?.createClient || createFtpsClient;
  try {
    const client = factory({ username: env.FTP_USERNAME, password: env.FTP_PASSWORD,
      timeoutMs: Math.min(UPLOAD_TRANSPORT_MS, remaining), signal });
    return await client.withSession(work);
  } catch (error) {
    if (error instanceof HttpError) throw error;
    const code = typeof error?.code === 'string' ? error.code : '';
    if (lease && code === 'ftp_close_pending') lease.retain = true;
    if (code === 'ftp_not_configured') uploadFail(503, 'The private FTP settings are missing or unsupported. An administrator needs to check the Cloudflare FTP secrets.', 'upload_configuration_required');
    if (code === 'ftp_session_reuse') uploadFail(502, 'Fasthosts requires TLS session reuse for encrypted file transfers. This direct Worker connection cannot satisfy that requirement. A compatible transfer service is needed before website uploads can work.', 'upload_ftp_session_reuse');
    if (code === 'ftp_resume_unavailable') uploadFail(502, 'The FTP server cannot read back an uploaded file from the required offset. Verified resumable uploads are unavailable on this connection.', 'upload_ftp_resume_unavailable');
    if (code === 'ftp_access') uploadFail(502, ftpsAccessMessage(error), 'upload_ftp_access');
    if (code === 'ftp_passive' || code === 'ftp_passive_invalid') uploadFail(502, 'The FTP server could not provide a supported passive file connection. Check the hosting account\'s encrypted passive FTP settings.', 'upload_ftp_passive');
    if (code === 'ftp_close_pending') uploadFail(502, 'The previous file connection is still closing. This upload remains locked for up to two minutes to prevent overlapping writes; check its progress before retrying.', 'upload_ftp_close_pending');
    if (code === 'ftp_timeout') uploadFail(502, 'The encrypted file transfer timed out. Its saved progress is retained; retry to verify any transferred bytes and resume.', 'upload_ftp_timeout');
    if (code === 'ftp_offset_mismatch') uploadFail(409, 'The temporary file offset changed during this operation. Refresh upload progress before retrying.', 'upload_remote_conflict');
    if (code === 'ftp_rename_conflict') uploadFail(409, 'A file already exists at the final upload address. Check this upload\'s status before completing it again.', 'upload_destination_conflict');
    if (/AUTH|LOGIN|CREDENTIAL/i.test(code)) uploadFail(502, 'The encrypted FTP service rejected its private login. An administrator needs to check the Cloudflare FTP secrets.', 'upload_ftp_login_failed');
    if (/TLS|CERT|SECURE/i.test(code)) uploadFail(502, 'The FTP service could not establish the required encrypted connection. Check the FTPS compatibility report.', 'upload_ftp_tls_failed');
    uploadFail(502, 'The encrypted file transfer did not finish. Your recorded progress is retained; retry to resume or cancel to request cleanup.', 'upload_transfer_failed');
  }
}

async function uploadReadChunk(request, expectedBytes) {
  if ((request.headers.get('Content-Type') || '').toLowerCase().split(';')[0].trim() !== 'application/octet-stream') {
    uploadFail(415, 'Send file chunks as application/octet-stream.', 'upload_binary_required');
  }
  const lengthHeader = request.headers.get('Content-Length');
  if (lengthHeader !== null && (!/^\d+$/.test(lengthHeader) || Number(lengthHeader) !== expectedBytes)) uploadFail(400, 'This file chunk has the wrong length.', 'upload_chunk_length');
  const reader = request.body?.getReader();
  if (!reader) uploadFail(400, 'A file chunk is required.', 'upload_chunk_length');
  const buffer = new Uint8Array(expectedBytes); let offset = 0;
  try {
    while (true) {
      const result = await reader.read(); if (result.done) break;
      if (offset + result.value.byteLength > expectedBytes) { await reader.cancel(); uploadFail(413, 'This file chunk exceeds its expected length.', 'upload_chunk_length'); }
      buffer.set(result.value, offset); offset += result.value.byteLength;
    }
  } finally { reader.releaseLock(); }
  if (offset !== expectedBytes) uploadFail(400, 'This file chunk ended before its expected length.', 'upload_chunk_length');
  return buffer;
}

function uploadCheckImage(row, bytes) {
  if (row.kind !== 'image') return;
  const png = bytes.length >= 8 && [137,80,78,71,13,10,26,10].every((n, i) => bytes[i] === n);
  const jpeg = bytes.length >= 3 && bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255;
  const webp = bytes.length >= 12 && [82,73,70,70].every((n, i) => bytes[i] === n) && [87,69,66,80].every((n, i) => bytes[i + 8] === n);
  if (!(row.format === '.png' ? png : row.format === '.webp' ? webp : jpeg)) uploadFail(400, 'The selected image contents do not match its filename extension.', 'upload_image_mismatch');
}

async function uploadChunk(request, env, db, original, options, url) {
  const offsetText = url.searchParams.get('offset');
  if (url.searchParams.size !== 1 || offsetText === null || !/^(?:0|[1-9]\d*)$/.test(offsetText)) uploadFail(400, 'A valid file-chunk offset is required.', 'upload_invalid_offset');
  const offset = Number(offsetText);
  if (!Number.isSafeInteger(offset) || offset < 0 || offset >= original.total_size || offset % UPLOAD_CHUNK_BYTES !== 0) uploadFail(400, 'This file-chunk offset is invalid.', 'upload_invalid_offset');
  uploadRequireOpen(original);
  const expected = Math.min(UPLOAD_CHUNK_BYTES, original.total_size - offset), bytes = await uploadReadChunk(request, expected);
  if (offset === 0) uploadCheckImage(original, bytes);
  const digest = await uploadHash(bytes);
  await uploadRecheckUser(request, env, original.owner_id);
  const lease = await uploadAcquire(db, original);
  try {
    const row = lease.row; uploadRequireOpen(row);
    if (offset > row.byte_offset) uploadFail(409, 'An earlier file chunk is still missing. Refresh upload progress and resume from its recorded offset.', 'upload_offset_mismatch');
    let receipt = await uploadFirst(db, 'SELECT * FROM library_upload_chunks WHERE job_id=? AND byte_offset=?', [row.id, offset]);
    if (receipt && (receipt.digest !== digest || receipt.byte_length !== expected)) uploadFail(409, 'This retry contains different bytes from the original chunk. Select the original file or cancel this upload.', 'upload_chunk_conflict');
    if (offset < row.byte_offset) {
      if (!receipt?.verified || offset + expected > row.byte_offset) uploadFail(409, 'The recorded chunk progress is inconsistent.', 'upload_offset_mismatch');
      return json({ upload: uploadView(row, env) });
    }
    await uploadStatement(db, `INSERT OR IGNORE INTO library_upload_chunks(job_id,byte_offset,byte_length,digest,created_at,updated_at)
      SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM library_upload_jobs WHERE id=? AND lock_token=? AND lock_until>? AND byte_offset=? AND state='open')`,
      [row.id, offset, expected, digest, now(), now(), row.id, lease.token, now(), offset]).run();
    await uploadFence(db, lease);
    receipt = await uploadFirst(db, 'SELECT * FROM library_upload_chunks WHERE job_id=? AND byte_offset=?', [row.id, offset]);
    if (!receipt || receipt.digest !== digest || receipt.byte_length !== expected) uploadFail(409, 'This file chunk could not reserve its retry record.', 'upload_chunk_conflict');
    await uploadTransport(env, options, lease, async ftp => {
      const path = uploadRemote(row, true), remote = await ftp.size(path), size = remote === null ? 0 : remote;
      if (!Number.isSafeInteger(size) || size < offset || size > offset + expected) uploadFail(409, 'The temporary file size no longer matches this upload. Cancel it and start a new upload.', 'upload_remote_conflict');
      if (size > offset) {
        const tail = await ftp.readRange(path, offset, size - offset);
        if (tail.byteLength !== size - offset || await uploadHash(tail) !== await uploadHash(bytes.subarray(0, size - offset))) {
          uploadFail(409, 'The temporary file contains different bytes. Cancel this upload before trying again.', 'upload_remote_conflict');
        }
      }
      await uploadFence(db, lease);
      if (size < offset + expected) await ftp.append(path, size, bytes.subarray(size - offset), offset + expected - size);
      const readback = await ftp.readRange(path, offset, expected);
      if (readback.byteLength !== expected || await uploadHash(readback) !== digest) uploadFail(409, 'The uploaded bytes did not pass their read-back check. The chunk has not been accepted.', 'upload_remote_conflict');
    }, request.signal);
    await uploadFence(db, lease);
    const timestamp = now();
    const results = await db.batch([
      uploadStatement(db, `UPDATE library_upload_chunks SET verified=1,updated_at=? WHERE job_id=? AND byte_offset=? AND digest=? AND byte_length=?
        AND EXISTS(SELECT 1 FROM library_upload_jobs WHERE id=? AND lock_token=? AND lock_until>? AND byte_offset=? AND state='open')`,
        [timestamp, row.id, offset, digest, expected, row.id, lease.token, timestamp, offset]),
      uploadStatement(db, `UPDATE library_upload_jobs SET byte_offset=?,updated_at=?,error_code='' WHERE id=? AND lock_token=? AND lock_until>? AND byte_offset=? AND state='open'
        AND EXISTS(SELECT 1 FROM library_upload_chunks WHERE job_id=? AND byte_offset=? AND byte_length=? AND digest=? AND verified=1)`,
        [offset + expected, timestamp, row.id, lease.token, timestamp, offset, row.id, offset, expected, digest])
    ]);
    if (!uploadChanges(results[1])) uploadFail(409, 'The transfer finished but its reservation changed. Refresh its progress before retrying.', 'upload_lease_lost');
    return json({ upload: uploadView(await uploadFresh(db, row.id), env) });
  } finally { await uploadRelease(db, lease); }
}

async function uploadVerifiedTail(db, row) {
  if (row.byte_offset !== row.total_size) uploadFail(409, 'Finish sending every file chunk before completing the upload.', 'upload_incomplete');
  const receipts = await uploadAll(db, 'SELECT byte_offset,byte_length,digest,verified FROM library_upload_chunks WHERE job_id=? ORDER BY byte_offset', [row.id]);
  let expected = 0;
  for (const receipt of receipts) {
    if (receipt.byte_offset !== expected || receipt.byte_length !== Math.min(UPLOAD_CHUNK_BYTES, row.total_size - expected) || !receipt.verified) uploadFail(409, 'The upload is missing a verified file chunk.', 'upload_incomplete');
    expected += receipt.byte_length;
  }
  if (expected !== row.total_size || !receipts.length) uploadFail(409, 'The upload is missing a verified file chunk.', 'upload_incomplete');
  return receipts[receipts.length - 1];
}

async function uploadResumeManifest(db, row) {
  // Sample fingerprints are only a quick selection check. The browser hashes
  // these already-accepted chunks from a reselected file before resuming, so
  // edits in an unsampled middle cannot splice two different files together.
  const receipts = await uploadAll(db, `SELECT byte_offset,byte_length,digest FROM library_upload_chunks
    WHERE job_id=? AND verified=1 ORDER BY byte_offset LIMIT 513`, [row.id]);
  if (receipts.length > 512) uploadFail(503, 'The upload resume record exceeds its permitted size.', 'upload_metadata_invalid');
  let expected = 0;
  const chunks = [];
  for (const receipt of receipts) {
    const length = Math.min(UPLOAD_CHUNK_BYTES, row.total_size - expected);
    if (receipt.byte_offset !== expected || receipt.byte_length !== length || length < 1 ||
        typeof receipt.digest !== 'string' || !/^[0-9a-f]{64}$/.test(receipt.digest) ||
        expected + length > row.byte_offset) uploadFail(503, 'The upload resume record is inconsistent. Check this upload before continuing.', 'upload_metadata_invalid');
    chunks.push({ offset: expected, size: length, sha256: receipt.digest }); expected += length;
  }
  if (expected !== row.byte_offset) uploadFail(503, 'The upload resume record is missing accepted chunks.', 'upload_metadata_invalid');
  return chunks;
}

async function uploadVerifyFinal(ftp, row, tail, path) {
  if (await ftp.size(path) !== row.total_size) uploadFail(409, 'The uploaded file size does not match the recorded file.', 'upload_remote_conflict');
  const bytes = await ftp.readRange(path, tail.byte_offset, tail.byte_length);
  if (bytes.byteLength !== tail.byte_length || await uploadHash(bytes) !== tail.digest) uploadFail(409, 'The uploaded file no longer matches its verified final chunk.', 'upload_remote_conflict');
}

async function uploadMarkComplete(db, lease) {
  const timestamp = now();
  const result = await uploadStatement(db, `UPDATE library_upload_jobs SET state='completed',completed_at=COALESCE(completed_at,?),updated_at=?,error_code=''
    WHERE id=? AND lock_token=? AND lock_until>? AND byte_offset=total_size AND state IN ('open','completing','cancel_pending')`,
    [timestamp, timestamp, lease.row.id, lease.token, timestamp]).run();
  if (!uploadChanges(result)) uploadFail(409, 'The file transfer completed but progress needs to be reconciled. Check this upload again.', 'upload_lease_lost');
  return uploadFresh(db, lease.row.id);
}

async function uploadComplete(request, env, db, original, options) {
  uploadObjectKeys(await readJSON(request, 1024), []);
  await uploadRecheckUser(request, env, original.owner_id);
  if (original.state === 'completed') return json({ upload: uploadView(original, env), asset: uploadAsset(original, env) });
  if (!['open', 'completing'].includes(original.state)) uploadFail(409, 'This upload cannot be completed in its current state.', 'upload_not_open');
  if (original.expires_at <= now()) uploadFail(410, 'This unfinished upload expired. Cancel it to clean up, then start again.', 'upload_expired');
  const lease = await uploadAcquire(db, original);
  try {
    const row = lease.row;
    if (!['open', 'completing'].includes(row.state)) uploadFail(409, 'This upload changed while completion was starting.', 'upload_not_open');
    const tail = await uploadVerifiedTail(db, row);
    await uploadStatement(db, `UPDATE library_upload_jobs SET state='completing',updated_at=? WHERE id=? AND lock_token=? AND lock_until>?`, [now(), row.id, lease.token, now()]).run();
    await uploadFence(db, lease);
    await uploadTransport(env, options, lease, async ftp => {
      const part = uploadRemote(row, true), final = uploadRemote(row);
      if (await ftp.size(final) !== null) {
        if (await ftp.size(part) !== null) uploadFail(409, 'Both the temporary and final upload paths exist. An administrator must inspect this upload.', 'upload_destination_conflict');
        await uploadVerifyFinal(ftp, row, tail, final);
      } else {
        await uploadVerifyFinal(ftp, row, tail, part);
        await uploadFence(db, lease);
        await ftp.rename(part, final);
        if (await ftp.size(final) !== row.total_size) uploadFail(409, 'The final upload size could not be confirmed.', 'upload_remote_conflict');
      }
    }, request.signal);
    const completed = await uploadMarkComplete(db, lease);
    return json({ upload: uploadView(completed, env), asset: uploadAsset(completed, env) });
  } finally { await uploadRelease(db, lease); }
}

async function uploadCleanupLease(env, db, lease, options, signal) {
  const row = lease.row;
  let recovered = false;
  await uploadFence(db, lease);
  await uploadTransport(env, options, lease, async ftp => {
    const final = uploadRemote(row), part = uploadRemote(row, true);
    if (await ftp.size(final) !== null) {
      // RNTO may have succeeded just before a response or database commit was lost.
      // Reconcile that successful upload; never delete an already-final asset.
      const tail = await uploadVerifiedTail(db, row);
      await uploadVerifyFinal(ftp, row, tail, final);
      if (await ftp.size(part) !== null) uploadFail(409, 'A final asset and temporary file both exist. Cleanup needs an administrator to inspect them.', 'upload_destination_conflict');
      recovered = true;
      return;
    }
    await uploadFence(db, lease);
    const directory = UPLOAD_ROOTS[row.kind] + '/' + UPLOAD_FOLDERS[row.category];
    // FTP 550 can mean either missing or unavailable, and DELE of a missing
    // file can close the transport. List first so a never-created temporary
    // file, or a deletion whose database acknowledgement was lost, is safe.
    const names = await uploadListingNames(ftp, directory);
    if (names.has(row.file_name)) uploadFail(409, 'The final upload exists but cannot be inspected. An administrator must check it before cleanup.', 'upload_remote_conflict');
    if (!names.has(row.file_name + '.part')) return;
    await ftp.remove(part);
    if ((await uploadListingNames(ftp, directory)).has(row.file_name + '.part')) {
      uploadFail(502, 'The temporary file has not been confirmed removed. Cleanup will be retried.', 'upload_cleanup_pending');
    }
  }, signal);
  if (recovered) return uploadMarkComplete(db, lease);
  const timestamp = now();
  const result = await uploadStatement(db, `UPDATE library_upload_jobs SET state=cleanup_state,updated_at=?,error_code=''
    WHERE id=? AND lock_token=? AND lock_until>? AND state='cancel_pending'`, [timestamp, row.id, lease.token, timestamp]).run();
  if (!uploadChanges(result)) uploadFail(409, 'Cleanup finished but its reservation changed. Check the upload again.', 'upload_lease_lost');
  return uploadFresh(db, row.id);
}

async function uploadListingNames(ftp, directory) {
  const entries = await ftp.list(directory);
  if (!Array.isArray(entries)) uploadFail(502, 'The temporary file directory could not be inspected. Cleanup will be retried.', 'upload_cleanup_pending');
  const names = new Set();
  for (const entry of entries) {
    const value = typeof entry === 'string' ? entry : entry?.name;
    if (typeof value !== 'string' || /[\u0000-\u001f\u007f]/.test(value)) uploadFail(502, 'The temporary file directory returned invalid names. Cleanup will be retried.', 'upload_cleanup_pending');
    // Servers may return absolute paths, relative paths, or bare names in NLST.
    names.add(value.replace(/\\/g, '/').split('/').pop());
  }
  return names;
}

async function uploadRequestCleanup(env, db, original, options, reason, signal) {
  if (original.state === 'completed') return { row: original, pending: false };
  if (['cancelled', 'expired'].includes(original.state)) return { row: original, pending: false };
  const lease = await uploadAcquire(db, original);
  try {
    await uploadStatement(db, `UPDATE library_upload_jobs SET state='cancel_pending',cleanup_state=?,updated_at=? WHERE id=? AND lock_token=? AND lock_until>?`,
      [reason, now(), original.id, lease.token, now()]).run();
    lease.row = await uploadFresh(db, original.id);
    try { return { row: await uploadCleanupLease(env, db, lease, options, signal), pending: false }; }
    catch (error) {
      if (error instanceof HttpError && error.code === 'upload_lease_lost') throw error;
      const code = error instanceof HttpError ? error.code : 'upload_transfer_failed';
      await uploadStatement(db, `UPDATE library_upload_jobs SET error_code=?,updated_at=? WHERE id=? AND lock_token=? AND lock_until>? AND state='cancel_pending'`,
        [code, now(), original.id, lease.token, now()]).run();
      return { row: await uploadFresh(db, original.id), pending: true };
    }
  } finally { await uploadRelease(db, lease); }
}

async function uploadCheckConnection(request, env, options, user) {
  uploadObjectKeys(await readJSON(request, 1024), []);
  await uploadRecheckUser(request, env, user.id, 'admin');
  await uploadTransport(env, options, null, async ftp => {
    for (const root of Object.values(UPLOAD_ROOTS)) {
      await ftp.checkDirectory(root);
      await ftp.list(root);
      for (const folder of Object.values(UPLOAD_FOLDERS)) await ftp.checkDirectory(root + '/' + folder);
    }
  }, request.signal);
  return json({ ok: true, ready: true, protocol: 'explicit-ftps', host: 'ftp.fasthosts.co.uk', port: 21, roots: UPLOAD_ROOTS,
    message: 'Encrypted connection access succeeded.',
    checks: { controlTls: 'ready', dataTls: 'ready', downloadRoot: 'ready', imageRoot: 'ready', categoryDirectories: 'ready' } });
}

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

async function handleUploads(request, env, ctx, options = {}) {
  const url = new URL(request.url);
  if (url.pathname !== '/v1/uploads' && !url.pathname.startsWith('/v1/uploads/')) return null;
  const allowed = url.pathname === '/v1/uploads' ? ['GET', 'POST'] :
    url.pathname === '/v1/uploads/settings' || /^\/v1\/uploads\/up_[0-9a-f]{32}$/.test(url.pathname) ? ['GET'] :
    url.pathname === '/v1/uploads/check' || /^\/v1\/uploads\/up_[0-9a-f]{32}\/(?:chunk|complete|cancel)$/.test(url.pathname) ? ['POST'] : [];
  if (!allowed.length) uploadFail(404, 'That upload action could not be found.', 'upload_not_found');
  if (!allowed.includes(request.method)) uploadFail(405, 'This method is not supported for uploads.', 'method_not_allowed');
  if (request.method === 'POST') requireOrigin(request, env);
  const user = await requireUser(request, env); requireRole(user, 'moderator');
  const db = uploadDatabase(env);
  try {
    if (url.pathname === '/v1/uploads/settings') return json(await uploadSettings(env, db));
    uploadPrivateConfig(env);
    if (!await uploadTablesReady(db)) uploadFail(503, 'Run the upload database migration before uploading files.', 'upload_storage_unavailable');
    if (url.pathname === '/v1/uploads/check') { requireRole(user, 'admin'); return await uploadCheckConnection(request, env, options, user); }
    if (url.pathname === '/v1/uploads') {
      if (request.method === 'POST') return await uploadCreate(request, env, db, user);
      const rows = await uploadAll(db, `SELECT * FROM library_upload_jobs WHERE owner_id=?
        ORDER BY CASE WHEN state IN ('open','completing','cancel_pending') THEN 0 ELSE 1 END,updated_at DESC,id LIMIT 24`, [user.id]);
      return json({ uploads: rows.map(row => uploadView(row, env)) });
    }
    const match = url.pathname.match(/^\/v1\/uploads\/(up_[0-9a-f]{32})(?:\/(chunk|complete|cancel))?$/);
    const action = match[2], row = await uploadRow(db, match[1], user, action === 'cancel');
    if (!action) return json({ upload: uploadView(row, env), resumeChunks: await uploadResumeManifest(db, row) });
    if (action === 'chunk') return await uploadChunk(request, env, db, row, options, url);
    if (action === 'complete') return await uploadComplete(request, env, db, row, options);
    uploadObjectKeys(await readJSON(request, 1024), []);
    const currentUser = await uploadRecheckUser(request, env, user.id);
    const currentRow = await uploadRow(db, row.id, currentUser, true);
    const result = await uploadRequestCleanup(env, db, currentRow, options, 'cancelled', request.signal);
    return json({ upload: uploadView(result.row, env), cleanupPending: result.pending }, result.pending ? 202 : 200);
  } catch (error) {
    if (error instanceof HttpError) throw error;
    if (uploadMissingTables(error)) uploadFail(503, 'Run the upload database migration before uploading files.', 'upload_storage_unavailable');
    throw error;
  }
}

// Completed files only. Missing optional upload tables do not break the existing
// catalogue or snapshot picker. The root merges/deduplicates this bounded list.
async function listUploadedAssets(env, limit = 950) {
  const bounded = Math.max(1, Math.min(950, Number.isSafeInteger(limit) ? limit : 950));
  try {
    const rows = await uploadAll(uploadDatabase(env), `SELECT * FROM library_upload_jobs WHERE state='completed' ORDER BY completed_at DESC,id LIMIT ?`, [bounded]);
    return rows.map(row => uploadAsset(row, env));
  } catch (error) {
    if (uploadMissingTables(error)) return [];
    throw error;
  }
}

// Call from the existing scheduled handler. Bounded sequential cleanup touches
// generated .part names only; final assets are reconciled, never deleted.
async function cleanupUploads(env, options = {}) {
  if (env.FTP_UPLOADS_ENABLED !== 'true' || !env.DB) return { examined: 0, cleaned: 0, pending: 0 };
  try { uploadPrivateConfig(env); } catch (_) { return { examined: 0, cleaned: 0, pending: 0 }; }
  const db = uploadDatabase(env), summary = { examined: 0, cleaned: 0, pending: 0 };
  try {
    if (!await uploadTablesReady(db)) return summary;
    const rows = await uploadAll(db, `SELECT * FROM library_upload_jobs WHERE lock_until<=? AND
      (state='cancel_pending' OR (state IN ('open','completing') AND expires_at<=?))
      ORDER BY updated_at,id LIMIT 3`, [now(), now()]);
    for (const row of rows) {
      summary.examined++;
      try {
        const result = await uploadRequestCleanup(env, db, uploadAssertRecord(row), options,
          row.state === 'cancel_pending' ? row.cleanup_state : 'expired');
        if (result.pending) summary.pending++; else summary.cleaned++;
      } catch (_) { summary.pending++; }
    }
    await uploadStatement(db, `DELETE FROM library_upload_jobs WHERE state IN ('cancelled','expired') AND updated_at<? AND lock_until<=?`, [now() - 30 * 86400, now()]).run();
    return summary;
  } catch (error) {
    if (uploadMissingTables(error)) return summary;
    throw error;
  }
}

return Object.freeze({UPLOADS_REVISION,UPLOAD_CHUNK_BYTES,handleUploads,listUploadedAssets,cleanupUploads,launcherUploadIntegrity,requirePreparedUpload});
})();

const __dkrHostedFiles = (() => {
// Private inventory generated from dkr-r.zip directory metadata only.
// No archive contents were extracted, no hosted files were fetched, and no
// approval/publication status is implied. Update this snapshot for later FTP files.
const { HttpError, json, baseURL, requireRole } = __dkrCommon;
const { requireUser } = __dkrAuth;
const { listUploadedAssets } = __dkrUploads;
const HOSTED_FILES_REVISION = '20261018-ftp-snapshot-1';
const HOSTED_FILES_NOTE = 'Includes the owner-supplied FTP inventory and up to 950 recent completed website uploads. Files added separately over FTP require an inventory update.';
const HOSTED_CATEGORY_FOLDERS = Object.freeze({
  tracks: 'Tracks', characters: 'Characters', legacy: 'Legacy',
  magic: 'MagicCodes', textures: 'TexturePacks'
});
const HOSTED_FORMATS = Object.freeze({
  download: ['.zip', '.rtz', '.dkrmap', '.xdelta', '.nrm', '.dkrcode', '.bps', '.ips', '.7z'],
  image: ['.png', '.jpg', '.jpeg', '.webp']
});
const HOSTED_FILES = Object.freeze(
[
  {
    "id": "ftp_66d48e6e87adc594df4fa81b",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/bomberman_-_syeo_(recomp).xdelta",
    "fileName": "bomberman_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 614107,
    "source": "ftp"
  },
  {
    "id": "ftp_29ac2ba5132047734fcfff8a",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/bottles_-_syeo.xdelta",
    "fileName": "bottles_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1168716,
    "source": "ftp"
  },
  {
    "id": "ftp_ae814d50502fe1afa7918b8e",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/bowser_-_syeo.xdelta",
    "fileName": "bowser_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1171161,
    "source": "ftp"
  },
  {
    "id": "ftp_7ab884a67041c91ff7566d07",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/Bubsy_(Recomp).xdelta",
    "fileName": "Bubsy_(Recomp).xdelta",
    "format": ".xdelta",
    "size": 1117879,
    "source": "ftp"
  },
  {
    "id": "ftp_8144dc2d7c20c6a2a8c1bc49",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/chunky_kong_-_syeo.xdelta",
    "fileName": "chunky_kong_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 446253,
    "source": "ftp"
  },
  {
    "id": "ftp_425222388b66ee7daa3fc254",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/dixie_kong.xdelta",
    "fileName": "dixie_kong.xdelta",
    "format": ".xdelta",
    "size": 903872,
    "source": "ftp"
  },
  {
    "id": "ftp_3cb9c906749cb5a8794c9932",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/dkjr_-_syeo_(recomp).xdelta",
    "fileName": "dkjr_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 457707,
    "source": "ftp"
  },
  {
    "id": "ftp_54d9130086c39de8047b384c",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/DKR-Rexy64.xdelta",
    "fileName": "DKR-Rexy64.xdelta",
    "format": ".xdelta",
    "size": 113952,
    "source": "ftp"
  },
  {
    "id": "ftp_7f13a5dd3b6dfedf379df19b",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/donkey_kong_-_syeo.xdelta",
    "fileName": "donkey_kong_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 982640,
    "source": "ftp"
  },
  {
    "id": "ftp_dd7632c840e30aa7c3f410d3",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/fungus.xdelta",
    "fileName": "fungus.xdelta",
    "format": ".xdelta",
    "size": 1529939,
    "source": "ftp"
  },
  {
    "id": "ftp_8ec6989b6a172cd8cde53361",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/funky_kong_-_syeo.xdelta",
    "fileName": "funky_kong_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 955920,
    "source": "ftp"
  },
  {
    "id": "ftp_dc356fbaff6a620563764e99",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/gregg_-_syeo.xdelta",
    "fileName": "gregg_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1220353,
    "source": "ftp"
  },
  {
    "id": "ftp_f0b98a86bb7b92ea37cc8162",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/grunty_-_syeo_(recomp).xdelta",
    "fileName": "grunty_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 730291,
    "source": "ftp"
  },
  {
    "id": "ftp_b25987ddf39f32513848fdcb",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/jinjo_(blue)_-_syeo.xdelta",
    "fileName": "jinjo_(blue)_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 973632,
    "source": "ftp"
  },
  {
    "id": "ftp_166c197732f83c36c34b2c12",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/kazooie_-_syeo.xdelta",
    "fileName": "kazooie_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 911299,
    "source": "ftp"
  },
  {
    "id": "ftp_2c7d97bc529bc0c9561056c2",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/kkrool.xdelta",
    "fileName": "kkrool.xdelta",
    "format": ".xdelta",
    "size": 1510481,
    "source": "ftp"
  },
  {
    "id": "ftp_2d33389efb3d194274ff1dab",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/koopa_kid_-_syeo.xdelta",
    "fileName": "koopa_kid_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 880806,
    "source": "ftp"
  },
  {
    "id": "ftp_7e28f48da5d0feda6749893c",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/kuro_bomberman_-_syeo_recomp.xdelta",
    "fileName": "kuro_bomberman_-_syeo_recomp.xdelta",
    "format": ".xdelta",
    "size": 600967,
    "source": "ftp"
  },
  {
    "id": "ftp_0c6ddfd3984bd0687e63e530",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/laylee_-_syeo.xdelta",
    "fileName": "laylee_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 768518,
    "source": "ftp"
  },
  {
    "id": "ftp_aa5a57f52640ce755fd07587",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/luigi_-_syeo.xdelta",
    "fileName": "luigi_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1630348,
    "source": "ftp"
  },
  {
    "id": "ftp_20877976c8472e666fed3a74",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/mario.xdelta",
    "fileName": "mario.xdelta",
    "format": ".xdelta",
    "size": 1629752,
    "source": "ftp"
  },
  {
    "id": "ftp_d03a623732c073df7d15c44e",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/moggy_-_syeo_(recomp).xdelta",
    "fileName": "moggy_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 510510,
    "source": "ftp"
  },
  {
    "id": "ftp_229260a93b1b6218e92658e1",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/mr_dog_-_syeo.xdelta",
    "fileName": "mr_dog_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 787721,
    "source": "ftp"
  },
  {
    "id": "ftp_38a59811e87edc2da38d3452",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/mumbo_jumbo_-_syeo_(recomp).xdelta",
    "fileName": "mumbo_jumbo_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 645945,
    "source": "ftp"
  },
  {
    "id": "ftp_50812a58e105a19055b16e33",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/RAYMAN_DKRRecomp.xdelta",
    "fileName": "RAYMAN_DKRRecomp.xdelta",
    "format": ".xdelta",
    "size": 771951,
    "source": "ftp"
  },
  {
    "id": "ftp_bda3374275143af6bf765cf6",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/sabreman_-_syeo.xdelta",
    "fileName": "sabreman_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1241082,
    "source": "ftp"
  },
  {
    "id": "ftp_c3250ccdbbce42622e7e2c2e",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/Sexy_Grunty.xdelta",
    "fileName": "Sexy_Grunty.xdelta",
    "format": ".xdelta",
    "size": 11003297,
    "source": "ftp"
  },
  {
    "id": "ftp_0f031b3c193546f9a7ce1857",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/shovelknight.xdelta",
    "fileName": "shovelknight.xdelta",
    "format": ".xdelta",
    "size": 919765,
    "source": "ftp"
  },
  {
    "id": "ftp_530494d46a284bf698916a3f",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/slash_-_syeo_recomp.xdelta",
    "fileName": "slash_-_syeo_recomp.xdelta",
    "format": ".xdelta",
    "size": 865554,
    "source": "ftp"
  },
  {
    "id": "ftp_a2eaa129f2a78aff10f1d608",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/taj.xdelta",
    "fileName": "taj.xdelta",
    "format": ".xdelta",
    "size": 1141571,
    "source": "ftp"
  },
  {
    "id": "ftp_eae735952f71d4310a5ab3f3",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/tiny_kong_-_syeo.xdelta",
    "fileName": "tiny_kong_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1317177,
    "source": "ftp"
  },
  {
    "id": "ftp_2104e87523cfb5e8f11b6e45",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/toon_link_-_syeo_recomp.xdelta",
    "fileName": "toon_link_-_syeo_recomp.xdelta",
    "format": ".xdelta",
    "size": 568327,
    "source": "ftp"
  },
  {
    "id": "ftp_44ad51b67f31dbca2546e019",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/waluigi_-_syeo.xdelta",
    "fileName": "waluigi_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1543596,
    "source": "ftp"
  },
  {
    "id": "ftp_1f382ad7d035a8d1eb9d9aef",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/wario_-_syeo_(recomp).xdelta",
    "fileName": "wario_-_syeo_(recomp).xdelta",
    "format": ".xdelta",
    "size": 1583450,
    "source": "ftp"
  },
  {
    "id": "ftp_25684c2eacc4b9cecff5b127",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/wizpig.xdelta",
    "fileName": "wizpig.xdelta",
    "format": ".xdelta",
    "size": 1547270,
    "source": "ftp"
  },
  {
    "id": "ftp_f29a9f2643d2afc7754cf1d5",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/yooka.xdelta",
    "fileName": "yooka.xdelta",
    "format": ".xdelta",
    "size": 1000782,
    "source": "ftp"
  },
  {
    "id": "ftp_34e10c792a2736d8bb143c6c",
    "kind": "download",
    "category": "characters",
    "path": "downloads/Characters/yoshi_-_syeo.xdelta",
    "fileName": "yoshi_-_syeo.xdelta",
    "format": ".xdelta",
    "size": 1235571,
    "source": "ftp"
  },
  {
    "id": "ftp_1f7e250c5d97f7a6e5bfa64d",
    "kind": "download",
    "category": "magic",
    "path": "downloads/MagicCodes/dkr_cc_speed-0.1.0.nrm",
    "fileName": "dkr_cc_speed-0.1.0.nrm",
    "format": ".nrm",
    "size": 3654,
    "source": "ftp"
  },
  {
    "id": "ftp_f8382f8749686a5d0f88eea4",
    "kind": "download",
    "category": "magic",
    "path": "downloads/MagicCodes/dkr_rainbow_rumble-0.1.3.nrm",
    "fileName": "dkr_rainbow_rumble-0.1.3.nrm",
    "format": ".nrm",
    "size": 23806,
    "source": "ftp"
  },
  {
    "id": "ftp_77ad2726da8f5664da910c07",
    "kind": "download",
    "category": "magic",
    "path": "downloads/MagicCodes/dkr_skip_caution-1.0.0.nrm",
    "fileName": "dkr_skip_caution-1.0.0.nrm",
    "format": ".nrm",
    "size": 1719,
    "source": "ftp"
  },
  {
    "id": "ftp_545dbe4784a04b06ebd39935",
    "kind": "download",
    "category": "magic",
    "path": "downloads/MagicCodes/dkr_terrain_scale-0.1.0.nrm",
    "fileName": "dkr_terrain_scale-0.1.0.nrm",
    "format": ".nrm",
    "size": 13760,
    "source": "ftp"
  },
  {
    "id": "ftp_fa6e2e7c02c6fd1fb0622020",
    "kind": "download",
    "category": "textures",
    "path": "downloads/TexturePacks/DKR_REMASTERED_V0.2.8_(SR.GU's).zip",
    "fileName": "DKR_REMASTERED_V0.2.8_(SR.GU's).zip",
    "format": ".zip",
    "size": 376611390,
    "source": "ftp"
  },
  {
    "id": "ftp_58cb539e310626eed1b7c9d3",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/DS_Mario_Circuit.zip",
    "fileName": "DS_Mario_Circuit.zip",
    "format": ".zip",
    "size": 4441415,
    "source": "ftp"
  },
  {
    "id": "ftp_e004f2a66d58b650c33d7513",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Kalimari_Desert.zip",
    "fileName": "Kalimari_Desert.zip",
    "format": ".zip",
    "size": 5012445,
    "source": "ftp"
  },
  {
    "id": "ftp_33f4a917a2be28f4e406d245",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Koopa_Troopa_Beach.zip",
    "fileName": "Koopa_Troopa_Beach.zip",
    "format": ".zip",
    "size": 4740361,
    "source": "ftp"
  },
  {
    "id": "ftp_f39f4ae84196112433ff8e3b",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Luigi_Raceway.zip",
    "fileName": "Luigi_Raceway.zip",
    "format": ".zip",
    "size": 987520,
    "source": "ftp"
  },
  {
    "id": "ftp_0820e1d14199d20b0a50da03",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Moo_Moo_Farm.zip",
    "fileName": "Moo_Moo_Farm.zip",
    "format": ".zip",
    "size": 4498506,
    "source": "ftp"
  },
  {
    "id": "ftp_10ba8096bd84a1d49bd9c0e5",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Strangled_Shrine.zip",
    "fileName": "Strangled_Shrine.zip",
    "format": ".zip",
    "size": 10302901,
    "source": "ftp"
  },
  {
    "id": "ftp_07f401fda91e87c081e66a08",
    "kind": "download",
    "category": "tracks",
    "path": "downloads/Tracks/Toads_Turnpike.zip",
    "fileName": "Toads_Turnpike.zip",
    "format": ".zip",
    "size": 5676405,
    "source": "ftp"
  },
  {
    "id": "ftp_67fd8f085a423e4949f8c862",
    "kind": "image",
    "category": "textures",
    "path": "images/TexturePacks/V0.2.8.png",
    "fileName": "V0.2.8.png",
    "format": ".png",
    "size": 2255757,
    "source": "ftp"
  }
]
.map(record => Object.freeze(record)));

// The build can call this assertion before packaging the private Worker.
// It also runs at module initialization so an edited snapshot fails closed.
function validateHostedInventory(records = HOSTED_FILES) {
  if (!Array.isArray(records) || records.length < 1 || records.length > 1000 ||
      JSON.stringify(records).length > 1024 * 1024) {
    throw new Error('The hosted-file snapshot exceeds its metadata limits.');
  }
  const paths = new Set(), ids = new Set();
  for (const record of records) {
    if (!record || typeof record !== 'object' ||
        typeof record.id !== 'string' || !/^ftp_[a-f0-9]{24}$/.test(record.id) ||
        !Object.hasOwn(HOSTED_FORMATS, record.kind) ||
        !Object.hasOwn(HOSTED_CATEGORY_FOLDERS, record.category) ||
        record.source !== 'ftp' ||
        typeof record.path !== 'string' || record.path.length > 1000 ||
        /[\\\\%?#\u0000-\u001f\u007f]/.test(record.path) ||
        typeof record.fileName !== 'string' || !record.fileName || record.fileName.length > 180 ||
        !Number.isSafeInteger(record.size) || record.size < 1) {
      throw new Error('The hosted-file snapshot contains invalid metadata.');
    }
    const parts = record.path.split('/');
    const root = record.kind === 'image' ? 'images' : 'downloads';
    const extension = '.' + record.fileName.split('.').pop().toLowerCase();
    if (parts.length !== 3 || parts[0] !== root ||
        parts[1] !== HOSTED_CATEGORY_FOLDERS[record.category] ||
        parts[2] !== record.fileName || parts.some(part => !part || part === '.' || part === '..') ||
        record.format !== extension || !HOSTED_FORMATS[record.kind].includes(extension) ||
        paths.has(record.path) || ids.has(record.id)) {
      throw new Error('The hosted-file snapshot contains an invalid or duplicate file path.');
    }
    paths.add(record.path); ids.add(record.id);
  }
  return true;
}
validateHostedInventory();

function hostedPathSegment(value) {
  // Encode each original segment once, including apostrophes and parentheses.
  return encodeURIComponent(value).replace(/[!'()*]/g, character =>
    '%' + character.charCodeAt(0).toString(16).toUpperCase());
}

async function handleHostedFiles(request, env) {
  const route = new URL(request.url);
  if (route.pathname !== '/v1/assets') return null;
  if (request.method !== 'GET') {
    throw new HttpError(405, 'This file inventory supports GET requests only.', 'method_not_allowed');
  }
  const user = await requireUser(request, env);
  requireRole(user, 'moderator');
  const directory = new URL('./', baseURL(env));
  const files = HOSTED_FILES.map(record => ({
    ...record,
    url: new URL(record.path.split('/').map(hostedPathSegment).join('/'), directory).href
  }));
  const uploaded = await listUploadedAssets(env);
  const paths = new Set(files.map(file => file.path));
  const ids = new Set(files.map(file => file.id));
  for (const file of uploaded) {
    if (files.length >= 1000) break;
    if (paths.has(file.path) || ids.has(file.id)) continue;
    files.push(file); paths.add(file.path); ids.add(file.id);
  }
  return json({ files, inventoryRevision: HOSTED_FILES_REVISION, note: HOSTED_FILES_NOTE });
}


return Object.freeze({HOSTED_FILES_REVISION,validateHostedInventory,handleHostedFiles});
})();

const __dkrLibrary = (() => {
// Private Cloudflare Worker module. No database binding or credentials belong in FTP assets.
const { requireUser } = __dkrAuth;
const { HttpError, json, readJSON, now, randomToken, safeText, requireRole,
  requireOrigin, publicUser, validatePublicURL } = __dkrCommon;
const CATEGORIES = ['tracks', 'characters', 'legacy', 'magic', 'textures'];
const ACTIONS = new Set(['review.save', 'review.report', 'report.resolve', 'request.submit', 'request.resubmit',
  'request.start', 'request.note', 'request.decide', 'request.publish', 'catalogue.save', 'catalogue.publish',
  'catalogue.archive', 'catalogue.update', 'admin.role', 'account.profile', 'account.delete']);
const PAGE_SIZE = 100;
const MAX_PAGE = 100000;
const iso = value => value ? new Date(Number(value) * 1000).toISOString() : '';
const actorName = user => user.display_name ?? user.displayName ?? 'Member';
const uid = prefix => prefix + '_' + randomToken(18);
const fail = (message, status = 400, code = 'invalid_input') => { throw new HttpError(status, message, code); };
const parse = (value, fallback) => { try { return JSON.parse(value); } catch (_) { return fallback; } };
const changes = result => Number(result?.meta?.changes ?? result?.changes ?? 0);
const rowsOf = result => result?.results || [];
function primary(env) { return env.DB.withSession ? env.DB.withSession('first-primary') : env.DB; }
function statement(db, sql, args = []) { return db.prepare(sql).bind(...args); }
async function all(db, sql, args = []) { return rowsOf(await statement(db, sql, args).all()); }
async function first(db, sql, args = []) { return statement(db, sql, args).first(); }
function ident(value, label = 'Record') {
  const id = safeText(value, 96, label, true);
  if (!/^[A-Za-z0-9_-]+$/.test(id)) fail(`${label} is invalid.`);
  return id;
}
function category(value) { if (!CATEGORIES.includes(value)) fail('Choose a library category.'); return value; }
function revision(value) {
  if (!Number.isSafeInteger(value) || value < 1) fail('Reload this record before saving it.', 409, 'revision_required');
  return value;
}
function checkRevision(row, expected) {
  if (Number(row.revision) !== revision(expected)) fail('This record changed. Reload it before saving.', 409, 'stale_record');
}
function object(value) { if (!value || typeof value !== 'object' || Array.isArray(value)) fail('Send a valid input object.'); return value; }
function safeHTTPS(value, label, required = false) {
  const text = safeText(value, 1500, label, required);
  if (!text) return '';
  let url; try { url = new URL(text); } catch (_) { fail(`Enter a complete HTTPS ${label.toLowerCase()}.`); }
  if (url.protocol !== 'https:' || url.username || url.password || /[\\\r\n]/.test(text)) fail(`Use an HTTPS ${label.toLowerCase()} without credentials.`);
  return url.href;
}
function pagination(url) {
  const n = url.searchParams.get('cursor');
  const offset = n == null ? 0 : Number(n);
  if (!Number.isSafeInteger(offset) || offset < 0 || offset > MAX_PAGE) fail('Invalid page cursor.');
  return { offset, limit: PAGE_SIZE };
}
function nextPage(offset, groups) { return groups.some(group => group.length > PAGE_SIZE) ? String(offset + PAGE_SIZE) : null; }
function auditStatement(db, user, action, target, message = '', gateSQL = '1', gateArgs = []) {
  return statement(db, `INSERT INTO library_audit(id,actor_id,actor_name,action,target_id,message,created_at)
    SELECT ?,?,?,?,?,?,? WHERE ${gateSQL}`, [uid('audit'), user.id, actorName(user), action, target, message, now(), ...gateArgs]);
}
function eventStatement(db, user, requestId, rev, label, message, visibility, mutation) {
  return statement(db, `INSERT INTO library_request_events(id,request_id,revision,label,message,visibility,actor_id,actor_name,created_at)
    SELECT ?,?,?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM library_requests WHERE id=? AND last_mutation=?)`,
  [uid('event'), requestId, rev, label, message, visibility, user.id, actorName(user), now(), requestId, mutation]);
}
async function requestRow(db, id) {
  const row = await first(db, 'SELECT r.*,a.display_name AS owner_name FROM library_requests r JOIN accounts a ON a.id=r.owner_id WHERE r.id=?', [ident(id)]);
  if (!row) fail('That request could not be found.', 404, 'not_found');
  return row;
}
async function itemRow(db, id, allowArchived = false) {
  const row = await first(db, 'SELECT * FROM library_items WHERE id=?' + (allowArchived ? '' : ' AND archived=0 AND current_release_id IS NOT NULL'), [ident(id)]);
  if (!row) fail('That library item could not be found.', 404, 'not_found');
  return row;
}
function reviewDTO(row, privateFields = false) {
  const value = { id: row.id, itemId: row.item_id, displayName: row.display_name,
    authorName: row.display_name, score: row.score, text: row.body, version: row.tested_version,
    date: iso(row.updated_at), createdAt: iso(row.created_at), updatedAt: iso(row.updated_at) };
  if (privateFields) Object.assign(value, { userId: row.account_id, hidden: !!row.hidden,
    status: row.hidden ? 'hidden' : 'visible', hiddenReason: row.hidden_reason, revision: row.revision });
  return value;
}
async function ratingFor(db, itemId) {
  const counts = await all(db, 'SELECT score,COUNT(*) AS count FROM library_reviews WHERE item_id=? AND hidden=0 GROUP BY score', [itemId]);
  const distribution = [0, 0, 0, 0, 0]; let total = 0, count = 0;
  for (const row of counts) { distribution[row.score - 1] = row.count; total += row.score * row.count; count += row.count; }
  return { average: count ? total / count : 0, count, distribution };
}
async function hydrateItems(db, rows) {
  if (!rows.length) return [];
  const ids = rows.map(row => row.id), slots = ids.map(() => '?').join(',');
  const batch = await db.batch([
    statement(db, `SELECT * FROM library_releases WHERE item_id IN (${slots}) ORDER BY created_at DESC,id DESC`, ids),
    statement(db, `SELECT item_id,score,COUNT(*) AS count FROM library_reviews WHERE hidden=0 AND item_id IN (${slots}) GROUP BY item_id,score`, ids)
  ]);
  const releases = rowsOf(batch[0]), ratings = rowsOf(batch[1]);
  const integrity = await all(db, `SELECT v.* FROM launcher_release_integrity v JOIN library_releases r ON r.id=v.release_id WHERE r.item_id IN (${slots})`, ids);
  return rows.map(row => {
    const versions = releases.filter(release => release.item_id === row.id).map(release => ({ id: release.id,
      version: release.version, fileName: release.file_name, fileUrl: release.file_url, format: release.format,
      requirements: release.requirements, install: release.install, createdAt: iso(release.created_at),
      ...((verified) => verified ? {sha256:verified.sha256,size:verified.bytes} : {})(integrity.find(value => value.release_id === release.id)) }));
    const current = versions.find(release => release.id === row.current_release_id) || {};
    const r = { average: 0, count: 0, distribution: [0, 0, 0, 0, 0] }; let total = 0;
    for (const score of ratings.filter(x => x.item_id === row.id)) { r.distribution[score.score - 1] = score.count; r.count += score.count; total += score.count * score.score; }
    r.average = r.count ? total / r.count : 0;
    return { id: row.id, title: row.title, author: row.author, category: row.category, summary: row.summary,
      description: row.summary, image: row.image, gallery: [], tags: parse(row.tags_json, []), featured: !!row.featured,
      archived: !!row.archived, revision: row.revision, createdAt: iso(row.created_at), updatedAt: iso(row.updated_at),
      currentReleaseId: row.current_release_id || null, releases: versions, version: current.version || '', fileName: current.fileName || '', fileUrl: current.fileUrl || '',
      format: current.format || '', requirements: current.requirements || '', install: current.install || '',
      compatibility: current.requirements || 'Requirements not supplied', rating: r, related: parse(row.related_json, []) };
  });
}
async function fullItem(db, id, allowArchived = false) { return (await hydrateItems(db, [await itemRow(db, id, allowArchived)]))[0]; }
async function hydrateRequests(db, rows, staff) {
  if (!rows.length) return [];
  const slots = rows.map(() => '?').join(',');
  const events = await all(db, `SELECT * FROM library_request_events WHERE request_id IN (${slots})${staff ? '' : " AND visibility='requester'"} ORDER BY revision,created_at,id`, rows.map(row => row.id));
  const reviewerIds = staff ? [...new Set(rows.map(row => row.reviewed_by).filter(Boolean))] : [];
  const reviewers = reviewerIds.length ? await all(db, `SELECT id,display_name FROM accounts WHERE id IN (${reviewerIds.map(() => '?').join(',')})`, reviewerIds) : [];
  return rows.map(row => {
    const history = events.filter(event => event.request_id === row.id).map(event => ({ id: event.id,
      label: event.label, message: event.message, text: event.message, visibility: event.visibility,
      actor: event.actor_name, at: iso(event.created_at), date: iso(event.created_at), revision: event.revision }));
    return { id: row.id, ownerId: row.owner_id, ownerName: row.owner_name, title: row.title, author: row.author,
      category: row.category, sourceUrl: row.source_url, downloadUrl: row.download_url, screenshotUrl: row.screenshot_url,
      summary: row.summary, requirements: row.requirements, testedVersion: row.tested_version,
      relation: row.relation, targetId: row.target_id, kind: row.kind, image: row.image, status: row.status,
      checks: parse(row.checks_json, {}), decisionMessage: row.decision_message, listingId: row.listing_id,
      releaseId: row.release_id, revision: row.revision, createdAt: iso(row.created_at), updatedAt: iso(row.updated_at),
      events: history, requesterMessages: history.filter(event => event.visibility === 'requester'),
      notes: staff ? history.filter(event => event.visibility === 'staff') : [],
      ...(staff ? { reviewedBy: row.reviewed_by, reviewerName: reviewers.find(reviewer => reviewer.id === row.reviewed_by)?.display_name || '' } : {}) };
  });
}
async function fullRequest(db, id, staff) { return (await hydrateRequests(db, [await requestRow(db, id)], staff))[0]; }
function reportDTO(row) {
  return { id: row.id, reviewId: row.review_id, itemId: row.item_id, reporterId: row.reporter_id,
    reporterName: row.reporter_name, reason: row.reason, status: row.status, resolved: row.status === 'resolved',
    action: row.action, resolution: row.resolution, resolvedBy: row.resolved_by, revision: row.revision,
    createdAt: iso(row.created_at), resolvedAt: iso(row.resolved_at) };
}
function draftDTO(row) {
  return { ...parse(row.content_json, {}), id: row.id, ownerId: row.owner_id, status: row.status,
    revision: row.revision, listingId: row.listing_id, releaseId: row.release_id,
    createdAt: iso(row.created_at), updatedAt: iso(row.updated_at) };
}
async function throttle(db, user, action) {
  const limits = { 'request.submit': [3600, 8], 'review.report': [3600, 20], 'review.save': [60, 12], 'account.profile': [60, 6] };
  const [seconds, maximum] = limits[action] || [60, 60];
  const stamp = now(), window = Math.floor(stamp / seconds) * seconds;
  const row = await statement(db, `INSERT INTO library_action_limits(account_id,bucket,window_start,count) VALUES(?,?,?,1)
    ON CONFLICT(account_id,bucket) DO UPDATE SET count=CASE WHEN window_start=excluded.window_start THEN count+1 ELSE 1 END,
    window_start=excluded.window_start WHERE library_action_limits.window_start<>excluded.window_start OR library_action_limits.count<?
    RETURNING count`, [user.id, action, window, maximum]).first();
  if (!row || row.count > maximum) fail('Please wait before trying this action again.', 429, 'rate_limited');
}
async function casRequest(db, user, row, expected, assignments, args, label, message, visibility = 'requester') {
  checkRevision(row, expected);
  const mutation = uid('change');
  const results = await db.batch([
    statement(db, `UPDATE library_requests SET ${assignments},revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND revision=?`,
      [...args, now(), mutation, row.id, expected]),
    eventStatement(db, user, row.id, expected + 1, label, message, visibility, mutation),
    auditStatement(db, user, label, row.id, visibility === 'staff' ? 'Private staff note added.' : message,
      'EXISTS(SELECT 1 FROM library_requests WHERE id=? AND last_mutation=?)', [row.id, mutation])
  ]);
  if (!changes(results[0])) fail('This request changed. Reload it before saving.', 409, 'stale_record');
  return fullRequest(db, row.id, user.role !== 'player');
}
function cleanMetadata(input, env, complete = false) {
  object(input);
  let image = safeText(input.image, 1500, 'Cover artwork') || 'pattern';
  if (complete && !['pattern', 'pattern-two'].includes(image)) image = validatePublicURL(image, env, 'image');
  let tags = input.tags ?? [];
  if (!Array.isArray(tags) || tags.length > 12) fail('Use no more than 12 tags.');
  tags = [...new Set(tags.map(tag => safeText(tag, 40, 'Tag', true)))];
  let related = input.related ?? [];
  if (!Array.isArray(related) || related.length > 10) fail('Choose no more than 10 related listings.');
  related = [...new Set(related.map(id => ident(id, 'Related listing')))];
  if (input.featured != null && typeof input.featured !== 'boolean') fail('Featured must be true or false.');
  return { title: safeText(input.title, 100, 'Title', complete), author: safeText(input.author, 100, 'Creator credit', complete),
    category: category(input.category), summary: safeText(input.summary, 4000, 'Description', complete), image,
    tags, related, featured: input.featured === true };
}
function cleanDraft(input, env, complete = false) {
  object(input);
  const metadata = cleanMetadata(input, env, complete);
  const target = input.target || 'new';
  if (!['new', 'existing'].includes(target)) fail('Choose a new or existing listing.');
  const fileUrlText = safeText(input.fileUrl, 1500, 'Download address', complete);
  const fileUrl = fileUrlText && complete ? validatePublicURL(fileUrlText, env) : fileUrlText;
  let fileName = safeText(input.fileName, 180, 'Release filename');
  if (!fileName && fileUrl) { try { fileName = decodeURIComponent(new URL(fileUrl).pathname.split('/').pop()); } catch (_) { if (complete) fail('Use a valid release filename.'); } }
  if (fileName && (/[\\/\r\n\u0000]/.test(fileName) || fileName.length > 180 || fileName === '.' || fileName === '..')) fail('Use a filename without folders.');
  if (complete && !fileName) fail('A release filename is required.');
  const result = { ...metadata, target, targetId: target === 'existing' ? ident(input.targetId, 'Existing listing') : null,
    targetRevision: target === 'existing' ? revision(input.targetRevision) : null,
    version: safeText(input.version, 80, 'Release version', complete), fileUrl, fileName,
    format: safeText(input.format, 40, 'Package format') || (fileName.includes('.') ? '.' + fileName.split('.').pop().toLowerCase() : ''),
    requirements: safeText(input.requirements, 2000, 'Requirements'), install: safeText(input.install, 6000, 'Installation instructions', complete) };
  return result;
}
async function validateRelated(db, relatedIds, selfId) {
  if (!relatedIds.length) return;
  if (relatedIds.includes(selfId)) fail('A listing cannot be its own related download.');
  const related = await all(db, `SELECT id FROM library_items WHERE archived=0 AND current_release_id IS NOT NULL AND id IN (${relatedIds.map(() => '?').join(',')})`, relatedIds);
  if (related.length !== relatedIds.length) fail('Choose published, available related downloads.');
}
async function validateTarget(db, draft) {
  if (draft.target !== 'existing') return null;
  const row = await itemRow(db, draft.targetId, true);
  if (row.category !== draft.category) fail('The release category must match its existing listing.');
  checkRevision(row, draft.targetRevision);
  return row;
}
async function publish(db, env, user, sourceType, source, input) {
  const sourceTable = sourceType === 'request' ? 'library_requests' : 'library_catalogue_drafts';
  // Retrying an already committed publication returns its original result.
  if (source.status === 'published' && source.listing_id) return fullItem(db, source.listing_id, true);
  if (source.status !== (sourceType === 'request' ? 'approved' : 'draft')) fail('This record is not ready for publication.', 409, 'invalid_transition');
  checkRevision(source, input.expectedRevision);
  const draft = cleanDraft(input.draft || parse(source.content_json, {}), env, true);
  await __dkrUploads.requirePreparedUpload(db,env,draft.fileUrl);
  if (sourceType === 'request') {
    if (source.category !== draft.category) fail('The category must match the approved request.');
    if (source.relation === 'update' && (draft.target !== 'existing' || draft.targetId !== source.target_id)) fail('Publish this update to its originally approved listing.');
    const checked = parse(source.checks_json, {});
    if (!['source', 'files', 'requirements', 'credits'].every(key => checked[key] === true)) fail('This request has not passed all review checks.', 409);
  }
  const targetRow = await validateTarget(db, draft);
  if (sourceType === 'request' && targetRow?.archived) fail('An administrator must restore this archived listing before an update can be published.', 409, 'target_archived');
  await validateRelated(db, draft.related, draft.targetId);
  const claim = uid('publication'), listingId = draft.target === 'existing' ? draft.targetId : uid('mod'), releaseId = uid('release');
  const stamp = now();
  const targetCondition = draft.target === 'existing' ? ` AND EXISTS(SELECT 1 FROM library_items WHERE id=? AND revision=? AND category=?${sourceType === 'request' ? ' AND archived=0' : ''})` : '';
  const targetArgs = draft.target === 'existing' ? [draft.targetId, draft.targetRevision, draft.category] : [];
  const claimGate = 'EXISTS(SELECT 1 FROM library_publications WHERE id=?)';
  const queries = [statement(db, `INSERT INTO library_publications(id,source_type,source_id,item_id,release_id,created_at)
    SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM ${sourceTable} WHERE id=? AND revision=? AND status=?)${targetCondition}
    ON CONFLICT(source_type,source_id) DO NOTHING`,
    [claim, sourceType, source.id, listingId, releaseId, stamp, source.id, input.expectedRevision, sourceType === 'request' ? 'approved' : 'draft', ...targetArgs])];
  if (draft.target === 'new') {
    queries.push(statement(db, `INSERT INTO library_items(id,title,author,category,summary,image,tags_json,related_json,featured,current_release_id,created_at,updated_at,last_mutation)
      SELECT ?,?,?,?,?,?,?,?,?,?,?,?,? WHERE ${claimGate}`,
    [listingId, draft.title, draft.author, draft.category, draft.summary, draft.image, JSON.stringify(draft.tags), JSON.stringify(draft.related), draft.featured ? 1 : 0, null, stamp, stamp, claim, claim]));
  } else {
    queries.push(statement(db, `UPDATE library_items SET title=?,author=?,summary=?,image=?,tags_json=?,related_json=?,featured=?,archived=0,revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND ${claimGate}`,
    [draft.title, draft.author, draft.summary, draft.image, JSON.stringify(draft.tags), JSON.stringify(draft.related), draft.featured ? 1 : 0, stamp, claim, listingId, claim]));
  }
  queries.push(statement(db, `INSERT INTO library_releases(id,item_id,version,file_name,file_url,format,requirements,install,created_at)
    SELECT ?,?,?,?,?,?,?,?,? WHERE ${claimGate}`, [releaseId, listingId, draft.version, draft.fileName, draft.fileUrl, draft.format, draft.requirements, draft.install, stamp, claim]));
  queries.push(statement(db, `INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at) SELECT ?,a.sha256,a.bytes,? FROM launcher_asset_integrity a WHERE a.file_url=? AND ${claimGate} ON CONFLICT(release_id) DO NOTHING`, [releaseId,stamp,draft.fileUrl,claim]));
  queries.push(statement(db, `UPDATE library_items SET current_release_id=? WHERE id=? AND ${claimGate}`, [releaseId, listingId, claim]));
  queries.push(statement(db, `UPDATE ${sourceTable} SET status='published',listing_id=?,release_id=?,revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND ${claimGate}`,
    [listingId, releaseId, stamp, claim, source.id, claim]));
  if (sourceType === 'request') queries.push(eventStatement(db, user, source.id, source.revision + 1, 'Published in the library', `Your request is now part of ${draft.title}.`, 'requester', claim));
  queries.push(auditStatement(db, user, 'Catalogue publication', source.id, listingId, claimGate, [claim]));
  let results;
  try { results = await db.batch(queries); }
  catch (error) {
    if (/library_releases.item_id|library_releases.version|UNIQUE constraint failed.*library_releases/i.test(String(error))) fail('This listing already has that version. Choose a new release version.', 409, 'duplicate_release');
    throw error;
  }
  if (!changes(results[0])) {
    const current = await first(db, `SELECT * FROM ${sourceTable} WHERE id=?`, [source.id]);
    if (current?.status === 'published' && current.listing_id) return fullItem(db, current.listing_id, true);
    fail('The request or target listing changed. Reload before publishing.', 409, 'stale_record');
  }
  return fullItem(db, listingId, true);
}

async function action(db, request, env, user, name, input) {
  object(input);
  if (!ACTIONS.has(name)) fail('That action does not exist.', 404, 'not_found');
  if (name.startsWith('admin.') || name === 'catalogue.archive') requireRole(user, 'admin');
  if (name.startsWith('catalogue.') && name !== 'catalogue.archive') requireRole(user, 'moderator');
  if (['request.start', 'request.note', 'request.decide', 'request.publish', 'report.resolve'].includes(name)) requireRole(user, 'moderator');
  await throttle(db, user, name);
  if (name === 'review.save') {
    const item = await itemRow(db, input.itemId);
    const score = Number(input.score);
    if (!Number.isInteger(score) || score < 1 || score > 5) fail('Choose a rating from 1 to 5 stars.');
    const text = safeText(input.text, 4000, 'Review'), version = safeText(input.version, 80, 'Tested version') || 'Unknown';
    const stamp = now();
    await db.batch([
      statement(db, `INSERT INTO library_reviews(id,item_id,account_id,score,body,tested_version,created_at,updated_at)
        VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(account_id,item_id) DO UPDATE SET score=excluded.score,body=excluded.body,
        tested_version=excluded.tested_version,updated_at=excluded.updated_at,revision=revision+1`, [uid('review'), item.id, user.id, score, text, version, stamp, stamp]),
      auditStatement(db, user, 'Review saved', item.id)
    ]);
    return reviewDTO(await first(db, 'SELECT r.*,a.display_name FROM library_reviews r JOIN accounts a ON a.id=r.account_id WHERE r.item_id=? AND r.account_id=?', [item.id, user.id]), true);
  }
  if (name === 'review.report') {
    const review = await first(db, `SELECT r.* FROM library_reviews r JOIN library_items i ON i.id=r.item_id WHERE r.id=? AND r.hidden=0 AND i.archived=0`, [ident(input.reviewId)]);
    if (!review) fail('That review could not be found.', 404, 'not_found');
    if (review.account_id === user.id) fail('You cannot report your own review.');
    const id = uid('report'), reason = safeText(input.reason, 1600, 'Report reason', true);
    try { await db.batch([
      statement(db, 'INSERT INTO library_review_reports(id,review_id,reporter_id,reason,created_at) VALUES(?,?,?,?,?)', [id, review.id, user.id, reason, now()]),
      auditStatement(db, user, 'Review reported', id)
    ]); } catch (error) { if (/UNIQUE constraint failed/i.test(String(error))) fail('You have already reported this review.', 409, 'already_reported'); throw error; }
    return { id, reviewId: review.id, status: 'open' };
  }
  if (name === 'report.resolve') {
    const report = await first(db, 'SELECT * FROM library_review_reports WHERE id=?', [ident(input.reportId)]);
    if (!report) fail('That report could not be found.', 404, 'not_found');
    checkRevision(report, input.expectedRevision);
    const reviewed = await first(db, 'SELECT * FROM library_reviews WHERE id=?', [report.review_id]);
    if (!reviewed) fail('That review could not be found.', 404, 'not_found');
    checkRevision(reviewed, input.expectedReviewRevision);
    if (!['hide', 'keep', 'restore'].includes(input.action)) fail('Choose a moderation action.');
    if (report.status === 'resolved' && input.action !== 'restore') fail('This report has already been resolved.', 409, 'invalid_transition');
    const reason = safeText(input.reason, 1600, 'Moderation reason', input.action === 'hide') || (input.action === 'keep' ? 'Report dismissed; review kept.' : 'Review restored.');
    const token = uid('change');
    const queries = [statement(db, `UPDATE library_review_reports SET status='resolved',action=?,resolution=?,resolved_by=?,resolved_at=?,revision=revision+1,last_mutation=? WHERE id=? AND revision=? AND EXISTS(SELECT 1 FROM library_reviews WHERE id=? AND revision=?)`,
      [input.action, reason, user.id, now(), token, report.id, input.expectedRevision, report.review_id, input.expectedReviewRevision])];
    if (input.action !== 'keep') queries.push(statement(db, `UPDATE library_reviews SET hidden=?,hidden_reason=?,revision=revision+1 WHERE id=? AND EXISTS(SELECT 1 FROM library_review_reports WHERE id=? AND last_mutation=?)`,
      [input.action === 'hide' ? 1 : 0, input.action === 'hide' ? reason : '', report.review_id, report.id, token]));
    queries.push(auditStatement(db, user, 'Review ' + input.action, report.review_id, reason,
      'EXISTS(SELECT 1 FROM library_review_reports WHERE id=? AND last_mutation=?)', [report.id, token]));
    const result = await db.batch(queries);
    if (!changes(result[0])) fail('This report changed. Reload before deciding.', 409, 'stale_record');
    return { id: report.id, status: 'resolved', action: input.action, resolution: reason, revision: report.revision + 1 };
  }
  if (name === 'request.submit') {
    const cat = category(input.category), relation = input.relation || 'new', kind = input.kind || 'suggestion';
    if (!['new', 'update'].includes(relation) || !['creator', 'suggestion'].includes(kind)) fail('Choose a valid request type.');
    const target = relation === 'update' ? await itemRow(db, input.targetId) : null;
    if (target && target.category !== cat) fail('Use the existing listing’s category for this update.');
    const id = uid('request'), token = uid('change'), stamp = now();
    const values = [id, user.id, safeText(input.title, 100, 'Mod title', true), safeText(input.author, 100, 'Creator name', true), cat,
      safeHTTPS(input.sourceUrl, 'Source link', true), safeHTTPS(input.downloadUrl, 'Download link'), safeText(input.summary, 4000, 'Description', true),
      safeText(input.requirements, 2000, 'Requirements'), safeText(input.testedVersion, 80, 'Tested version'), safeHTTPS(input.screenshotUrl, 'Screenshot link'),
      relation, target?.id || null, kind, target?.image || 'pattern', stamp, stamp, token];
    await db.batch([
      statement(db, `INSERT INTO library_requests(id,owner_id,title,author,category,source_url,download_url,summary,requirements,tested_version,screenshot_url,relation,target_id,kind,image,created_at,updated_at,last_mutation)
        VALUES(${values.map(() => '?').join(',')})`, values),
      eventStatement(db, user, id, 1, 'Request submitted', 'Your request is waiting for moderator review.', 'requester', token),
      auditStatement(db, user, 'Request submitted', id)
    ]);
    return fullRequest(db, id, false);
  }
  if (name === 'request.resubmit') {
    const row = await requestRow(db, input.id);
    if (row.owner_id !== user.id) fail('Only the requester can revise this request.', 403, 'forbidden');
    if (row.status !== 'changes') fail('This request is not waiting for changes.', 409, 'invalid_transition');
    const message = safeText(input.message, 2400, 'Reply describing your changes', true);
    const source = input.sourceUrl === undefined ? row.source_url : safeHTTPS(input.sourceUrl, 'Source link', true);
    const download = input.downloadUrl === undefined ? row.download_url : safeHTTPS(input.downloadUrl, 'Download link');
    const screenshot = input.screenshotUrl === undefined ? row.screenshot_url : safeHTTPS(input.screenshotUrl, 'Screenshot link');
    const requirements = input.requirements === undefined ? row.requirements : safeText(input.requirements, 2000, 'Requirements');
    const tested = input.testedVersion === undefined ? row.tested_version : safeText(input.testedVersion, 80, 'Tested version');
    return casRequest(db, user, row, input.expectedRevision, "source_url=?,download_url=?,screenshot_url=?,requirements=?,tested_version=?,status='pending',checks_json='{}',decision_message='',reviewed_by=NULL",
      [source, download, screenshot, requirements, tested], 'Changes resubmitted', message);
  }
  if (name === 'request.start') {
    const row = await requestRow(db, input.id);
    if (row.status !== 'pending') fail('Only pending requests can enter review.', 409, 'invalid_transition');
    return casRequest(db, user, row, input.expectedRevision, "status='review',reviewed_by=?", [user.id], 'Under review', 'A moderator is checking this request.');
  }
  if (name === 'request.note') {
    const row = await requestRow(db, input.id);
    return casRequest(db, user, row, input.expectedRevision, 'reviewed_by=reviewed_by', [], 'Staff note', safeText(input.value, 3000, 'Staff note', true), 'staff');
  }
  if (name === 'request.decide') {
    const row = await requestRow(db, input.id);
    if (row.status !== 'review') fail('Start reviewing this request before deciding.', 409, 'invalid_transition');
    if (!['approve', 'changes', 'reject'].includes(input.decision)) fail('Choose an approval decision.');
    const checksInput = input.checks || {}, checks = {};
    for (const key of ['source', 'files', 'requirements', 'credits']) checks[key] = checksInput[key] === true;
    if (input.decision === 'approve' && !Object.values(checks).every(Boolean)) fail('Complete all four checks before approving.');
    const message = safeText(input.message, 2400, 'Message to requester', input.decision !== 'approve') || 'The request passed review and is waiting for publication.';
    const status = { approve: 'approved', changes: 'changes', reject: 'rejected' }[input.decision];
    return casRequest(db, user, row, input.expectedRevision, 'status=?,checks_json=?,decision_message=?,reviewed_by=?', [status, JSON.stringify(checks), message, user.id],
      { approve: 'Approved for publication', changes: 'Changes requested', reject: 'Request declined' }[input.decision], message);
  }
  if (name === 'request.publish') return publish(db, env, user, 'request', await requestRow(db, input.id), input);
  if (name === 'catalogue.save') {
    const draft = cleanDraft(input.draft || input, env, false);
    const id = input.id || input.draft?.id;
    if (!id) {
      const created = uid('draft'), stamp = now();
      await db.batch([
        statement(db, 'INSERT INTO library_catalogue_drafts(id,owner_id,content_json,created_at,updated_at) VALUES(?,?,?,?,?)', [created, user.id, JSON.stringify(draft), stamp, stamp]),
        auditStatement(db, user, 'Catalogue draft saved', created, draft.title)
      ]);
      return created;
    }
    const row = await first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [ident(id)]);
    if (!row) fail('That draft could not be found.', 404, 'not_found');
    if (row.status !== 'draft') fail('This draft has already been published.', 409, 'invalid_transition');
    checkRevision(row, input.expectedRevision);
    const token = uid('change');
    const result = await db.batch([
      statement(db, 'UPDATE library_catalogue_drafts SET content_json=?,revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND revision=? AND status=\'draft\'',
        [JSON.stringify(draft), now(), token, row.id, input.expectedRevision]),
      auditStatement(db, user, 'Catalogue draft saved', row.id, draft.title, 'EXISTS(SELECT 1 FROM library_catalogue_drafts WHERE id=? AND last_mutation=?)', [row.id, token])
    ]);
    if (!changes(result[0])) fail('This draft changed. Reload it before saving.', 409, 'stale_record');
    return row.id;
  }
  if (name === 'catalogue.publish') {
    const row = await first(db, 'SELECT * FROM library_catalogue_drafts WHERE id=?', [ident(input.id)]);
    if (!row) fail('That draft could not be found.', 404, 'not_found');
    return publish(db, env, user, 'draft', row, input);
  }
  if (name === 'catalogue.archive') {
    const row = await itemRow(db, input.id, true); checkRevision(row, input.expectedRevision);
    if (typeof input.archived !== 'boolean') fail('Choose archive or restore.');
    const token = uid('change');
    const result = await db.batch([
      statement(db, 'UPDATE library_items SET archived=?,revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND revision=?', [input.archived ? 1 : 0, now(), token, row.id, input.expectedRevision]),
      auditStatement(db, user, input.archived ? 'Listing archived' : 'Listing restored', row.id, '', 'EXISTS(SELECT 1 FROM library_items WHERE id=? AND last_mutation=?)', [row.id, token])
    ]);
    if (!changes(result[0])) fail('This listing changed. Reload it first.', 409, 'stale_record');
    return fullItem(db, row.id, true);
  }
  if (name === 'catalogue.update') {
    const row = await itemRow(db, input.id, true); checkRevision(row, input.expectedRevision);
    const metadata = cleanMetadata({ ...input, category: input.category ?? row.category }, env, true);
    if (metadata.category !== row.category) fail('An existing listing must keep its category.');
    await validateRelated(db, metadata.related, row.id);
    const token = uid('change');
    const result = await db.batch([
      statement(db, `UPDATE library_items SET title=?,author=?,summary=?,image=?,tags_json=?,related_json=?,featured=?,revision=revision+1,updated_at=?,last_mutation=? WHERE id=? AND revision=?`,
        [metadata.title, metadata.author, metadata.summary, metadata.image, JSON.stringify(metadata.tags), JSON.stringify(metadata.related), metadata.featured ? 1 : 0, now(), token, row.id, input.expectedRevision]),
      auditStatement(db, user, 'Listing details updated', row.id, metadata.title,
        'EXISTS(SELECT 1 FROM library_items WHERE id=? AND last_mutation=?)', [row.id, token])
    ]);
    if (!changes(result[0])) fail('This listing changed. Reload it before saving.', 409, 'stale_record');
    return fullItem(db, row.id, true);
  }
  if (name === 'admin.role') {
    const accountId = ident(input.accountId, 'Account');
    if (!['player', 'moderator', 'admin'].includes(input.role)) fail('Choose a valid account role.');
    const selected = await first(db, 'SELECT id,role,status FROM accounts WHERE id=?', [accountId]);
    if (!selected || selected.status !== 'active') fail('That active account could not be found.', 404, 'not_found');
    try { await db.batch([
      statement(db, 'UPDATE accounts SET role=?,updated_at=? WHERE id=?', [input.role, now(), accountId]),
      auditStatement(db, user, 'Account role changed', accountId, selected.role + ' → ' + input.role)
    ]); } catch (error) { if (/last_active_admin/i.test(String(error))) fail('Keep at least one active administrator.', 409, 'last_admin'); throw error; }
    return publicUser(await first(db, 'SELECT id,display_name,role,status,created_at FROM accounts WHERE id=?', [accountId]));
  }
  if (name === 'account.profile') {
    const displayName = safeText(input.displayName, 60, 'Display name', true);
    await statement(db, 'UPDATE accounts SET display_name=?,updated_at=? WHERE id=?', [displayName, now(), user.id]).run();
    return publicUser(await first(db, 'SELECT id,display_name,role,status,created_at FROM accounts WHERE id=?', [user.id]));
  }
  if (name === 'account.delete') {
    if (input.confirmation !== 'DELETE') fail('Type DELETE to confirm removal of your account.');
    try { await db.batch([
      statement(db, "UPDATE library_audit SET actor_name='Deleted account' WHERE actor_id=?", [user.id]),
      statement(db, "UPDATE library_request_events SET actor_name='Deleted account' WHERE actor_id=?", [user.id]),
      statement(db, 'DELETE FROM accounts WHERE id=?', [user.id])
    ]); } catch (error) { if (/last_active_admin/i.test(String(error))) fail('Assign another active administrator before deleting this account.', 409, 'last_admin'); throw error; }
    return { deleted: true };
  }
  fail('That action does not exist.', 404, 'not_found');
}

async function handleLibrary(request, env, ctx) {
  const url = new URL(request.url), path = url.pathname;
  if (request.method === 'GET' && path === '/v1/catalogue') {
    const db = primary(env), page = pagination(url);
    const rows = await all(db, 'SELECT * FROM library_items WHERE archived=0 AND current_release_id IS NOT NULL ORDER BY updated_at DESC,id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset]);
    return json({ items: await hydrateItems(db, rows.slice(0, PAGE_SIZE)), nextCursor: nextPage(page.offset, [rows]) });
  }
  const reviewsPath = path.match(/^\/v1\/mods\/([A-Za-z0-9_-]{1,96})\/reviews$/);
  if (request.method === 'GET' && reviewsPath) {
    const db = primary(env), itemId = reviewsPath[1]; await itemRow(db, itemId);
    const cursorText = url.searchParams.get('cursor'); let cursor = null;
    if (cursorText) {
      if (cursorText.length > 200) fail('Invalid review cursor.');
      try { cursor = JSON.parse(atob(cursorText.replace(/-/g, '+').replace(/_/g, '/'))); } catch (_) { fail('Invalid review cursor.'); }
      if (!cursor || !Number.isSafeInteger(cursor.at) || cursor.at < 0) fail('Invalid review cursor.');
      ident(cursor.id);
    }
    const limit = 20;
    const rows = await all(db, `SELECT r.*,a.display_name FROM library_reviews r JOIN accounts a ON a.id=r.account_id
      WHERE r.item_id=? AND r.hidden=0${cursor ? ' AND (r.created_at<? OR (r.created_at=? AND r.id<?))' : ''}
      ORDER BY r.created_at DESC,r.id DESC LIMIT ?`, [itemId, ...(cursor ? [cursor.at, cursor.at, cursor.id] : []), limit + 1]);
    const visible = rows.slice(0, limit), last = visible.at(-1);
    const nextCursor = rows.length > limit ? btoa(JSON.stringify({ at: last.created_at, id: last.id })).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '') : null;
    return json({ reviews: visible.map(row => reviewDTO(row)), rating: await ratingFor(db, itemId), nextCursor });
  }
  if (request.method === 'GET' && ['/v1/me', '/v1/staff', '/v1/admin'].includes(path)) {
    const user = await requireUser(request, env), db = primary(env), page = pagination(url);
    if (path === '/v1/me') {
      const batch = await db.batch([
        statement(db, 'SELECT r.*,a.display_name AS owner_name FROM library_requests r JOIN accounts a ON a.id=r.owner_id WHERE owner_id=? ORDER BY r.updated_at DESC,r.id DESC LIMIT ? OFFSET ?', [user.id, PAGE_SIZE + 1, page.offset]),
        statement(db, 'SELECT r.*,a.display_name FROM library_reviews r JOIN accounts a ON a.id=r.account_id WHERE account_id=? ORDER BY r.updated_at DESC,r.id DESC LIMIT ? OFFSET ?', [user.id, PAGE_SIZE + 1, page.offset])
      ]);
      const requests = rowsOf(batch[0]), reviews = rowsOf(batch[1]);
      return json({ user: publicUser(user), requests: await hydrateRequests(db, requests.slice(0, PAGE_SIZE), false), reviews: reviews.slice(0, PAGE_SIZE).map(row => reviewDTO(row, true)), nextCursor: nextPage(page.offset, [requests, reviews]) });
    }
    if (path === '/v1/staff') {
      requireRole(user, 'moderator');
      const batch = await db.batch([
        statement(db, 'SELECT r.*,a.display_name AS owner_name FROM library_requests r JOIN accounts a ON a.id=r.owner_id ORDER BY r.updated_at DESC,r.id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset]),
        statement(db, `SELECT p.*,r.item_id,a.display_name AS reporter_name FROM library_review_reports p JOIN library_reviews r ON r.id=p.review_id JOIN accounts a ON a.id=p.reporter_id ORDER BY p.created_at DESC,p.id DESC LIMIT ? OFFSET ?`, [PAGE_SIZE + 1, page.offset])
      ]);
      const requests = rowsOf(batch[0]), reports = rowsOf(batch[1]), pageReports = reports.slice(0, PAGE_SIZE);
      const ids = [...new Set(pageReports.map(report => report.review_id))];
      const reviews = ids.length ? await all(db, `SELECT r.*,a.display_name FROM library_reviews r JOIN accounts a ON a.id=r.account_id WHERE r.id IN (${ids.map(() => '?').join(',')})`, ids) : [];
      return json({ requests: await hydrateRequests(db, requests.slice(0, PAGE_SIZE), true), reports: pageReports.map(reportDTO), reviews: reviews.map(row => reviewDTO(row, true)), nextCursor: nextPage(page.offset, [requests, reports]) });
    }
    requireRole(user, 'admin');
    const batch = await db.batch([
      statement(db, 'SELECT id,display_name,role,status,created_at FROM accounts ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset]),
      statement(db, 'SELECT * FROM library_catalogue_drafts ORDER BY updated_at DESC,id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset]),
      statement(db, 'SELECT * FROM library_audit ORDER BY created_at DESC,id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset]),
      statement(db, 'SELECT * FROM library_items ORDER BY updated_at DESC,id DESC LIMIT ? OFFSET ?', [PAGE_SIZE + 1, page.offset])
    ]);
    const groups = batch.map(rowsOf);
    return json({ accounts: groups[0].slice(0, PAGE_SIZE).map(publicUser), drafts: groups[1].slice(0, PAGE_SIZE).map(draftDTO),
      audit: groups[2].slice(0, PAGE_SIZE).map(row => ({ id: row.id, action: row.action, targetId: row.target_id, message: row.message, actor: row.actor_name, at: iso(row.created_at), date: iso(row.created_at) })),
      items: await hydrateItems(db, groups[3].slice(0, PAGE_SIZE)), nextCursor: nextPage(page.offset, groups) });
  }
  const actionPath = path.match(/^\/v1\/actions\/([a-z]+\.[a-z]+)$/);
  if (request.method === 'POST' && actionPath) {
    requireOrigin(request, env);
    const user = await requireUser(request, env), body = await readJSON(request, 65536);
    return json({ result: await action(primary(env), request, env, user, actionPath[1], body.input || {}) });
  }
  return null;
}

async function cleanupLibrary(env) {
  const db = primary(env);
  const cutoff = now() - 180 * 86400;
  await db.batch([
    statement(db, 'DELETE FROM library_action_limits WHERE window_start<?', [now() - 172800]),
    statement(db, 'DELETE FROM library_audit WHERE created_at<?', [cutoff]),
    statement(db, "DELETE FROM library_requests WHERE status IN ('published','rejected') AND updated_at<?", [cutoff]),
    statement(db, "DELETE FROM library_catalogue_drafts WHERE status='published' AND updated_at<?", [cutoff])
  ]);
}

return Object.freeze({handleLibrary,cleanupLibrary});
})();

const {HttpError, json, baseURL} = __dkrCommon;
const {handleAuth, cleanupAuth, AUTH_REVISION} = __dkrAuth;
const {handleHostedFiles, HOSTED_FILES_REVISION} = __dkrHostedFiles;
const {handleUploads, cleanupUploads, UPLOADS_REVISION} = __dkrUploads;
const {handleLibrary, cleanupLibrary} = __dkrLibrary;
async function healthStatus(env) {
  // Public setup diagnostics are fixed status labels only. Never return env
  // values, database results, SQL, exception messages, or account information.
  // Discord checks preserve the original presence-only health semantics; an
  // actual sign-in is still needed to validate the provider configuration.
  const checks = {
    frontend_url: env.FRONTEND_URL ? 'invalid' : 'missing',
    database_binding: !env.DB ? 'missing' : typeof env.DB.prepare === 'function' ? 'ready' : 'invalid',
    discord_client_id: env.DISCORD_CLIENT_ID ? 'ready' : 'missing',
    discord_client_secret: env.DISCORD_CLIENT_SECRET ? 'ready' : 'missing',
    owner_discord_id: env.OWNER_DISCORD_ID ? 'ready' : 'missing',
    account_storage: 'not_checked',
    catalogue_storage: 'not_checked'
  };
  try { baseURL(env); checks.frontend_url = 'ready'; }
  catch (_) { /* Keep the fixed missing/invalid label; do not expose details. */ }

  if (checks.database_binding === 'ready') {
    // LIMIT 0 validates the same table/column references as the original
    // health check without fetching any account or catalogue records.
    for (const [name, sql] of [
      ['account_storage', 'SELECT id FROM accounts LIMIT 0'],
      ['catalogue_storage', 'SELECT 1 FROM library_items LIMIT 0']
    ]) {
      try { await env.DB.prepare(sql).first(); checks[name] = 'ready'; }
      catch (_) { checks[name] = 'unavailable'; }
    }
  }
  return {
    service: 'DKR-R Mod Library',
    version: '1.0.0-rc.1',
    launcherRevision: '20261018-beta8',
    healthRevision: '20261017-1',
    authRevision: AUTH_REVISION,
    hostedFilesRevision: HOSTED_FILES_REVISION,
    uploadRevision: UPLOADS_REVISION,
    configured: Object.values(checks).every(status => status === 'ready'),
    checks
  };
}

function finish(response, request, env) {
  const headers = new Headers(response.headers);
  headers.set('Cache-Control', 'no-store');
  headers.set('X-Content-Type-Options', 'nosniff');
  headers.set('Referrer-Policy', 'no-referrer');
  headers.set('Strict-Transport-Security', 'max-age=31536000');
  headers.set('Content-Security-Policy', "default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
  headers.set('Permissions-Policy', 'camera=(), microphone=(), geolocation=()');
  headers.set('Vary', 'Origin');
  try {
    if (request.headers.get('Origin') === baseURL(env).origin) {
      headers.set('Access-Control-Allow-Origin', baseURL(env).origin);
      // Ordinary library requests still use a bearer token. Only auth endpoints
      // opt into the protected browser-session/remembered-device cookies.
      if (['/auth/exchange','/auth/refresh','/auth/persistence','/auth/logout'].includes(new URL(request.url).pathname)) {
        headers.set('Access-Control-Allow-Credentials', 'true');
      }
      headers.set('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      headers.set('Access-Control-Allow-Headers', 'Authorization, Content-Type');
      headers.set('Access-Control-Max-Age', '600');
    }
  } catch (_) { /* An unconfigured service must still give a safe error. */ }
  return new Response(response.body, {status:response.status, statusText:response.statusText, headers});
}

const launcherPinnedBackfill=[{"id":"release_sUGKqwoCWoBGyQpe_5cRhhLG","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Toads_Turnpike.zip","sha256":"70482f6ef1e969e8bda1d2570eec389e061fe4cef73760977c7f6ea0d2d8aa5c","bytes":5676405,"version":"1.0.0","verifiedAt":1792352384},{"id":"release_IvehbY3CnmEPYcTxPhnPHwE3","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Moo_Moo_Farm.zip","sha256":"fe411f426e6db876b388accec23070fb302bf3ba56fd29927e64c5b70d3e7242","bytes":4498506,"version":"1.0.0","verifiedAt":1792352384},{"id":"release_IpOqD0EN0_Xum_JXGUrLqxRt","url":"https://positron-portal.com/dkr-r/downloads/Tracks/DS_Mario_Circuit.zip","sha256":"a6fb258bd41522407cfb3bdb448b649b0304a4b1da8606e851eae2ee4d43631b","bytes":4441415,"version":"1.0.0","verifiedAt":1792352385},{"id":"release_kxXFUbgZNaY0Giyk3dqXxmwR","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Strangled_Shrine.zip","sha256":"af5f75bd8fbd8e74546d988262a98500e3744eaead64c3cc84989b83260f2084","bytes":10302901,"version":"1.0.0","verifiedAt":1792352386},{"id":"release_r_ARKvV9aBncgxfl6HhQ4PRY","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Kalimari_Desert.zip","sha256":"6430c527d8d8344cc49e40b0f9981af596e19065a6a9629fb11ccb59736a5522","bytes":5012445,"version":"1.0.0","verifiedAt":1792352386},{"id":"release_KnXeonWodgB-MEKNRN1kzOTD","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Koopa_Troopa_Beach.zip","sha256":"170e293ed962ca4eb18488ba69c6b10cc197aa54aadbcb2aab4475b6186a3655","bytes":4740361,"version":"1.0.0","verifiedAt":1792352386},{"id":"release_lamb_ctAajkyjOSHNAoHvPJd","url":"https://positron-portal.com/dkr-r/downloads/Tracks/Luigi_Raceway.zip","sha256":"9a63bc7e292db4bd5a0f9ea9206ce9593be7b69609ce2af20e799e69cf64e857","bytes":987520,"version":"1.0.0","verifiedAt":1792352387},{"id":"release_DGF6FzIJqafpnaOQs7JhMEMl","url":"https://positron-portal.com/dkr-r/downloads/TexturePacks/DKR_REMASTERED_V0.2.8_(SR.GU's).zip","sha256":"2f350c0971892ef6804d71366985c4232186daa8587a5be1f80dedc1efc14419","bytes":376611390,"version":"0.2.8","verifiedAt":1792352394}];
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

export default {
  async fetch(request, env, ctx) {
    let response;
    try {
      const url = new URL(request.url);
      if (url.protocol !== 'https:') throw new HttpError(400, 'Use the HTTPS library service address.', 'https_required');
      if (!['GET','POST','OPTIONS'].includes(request.method)) throw new HttpError(405, 'This method is not supported.', 'method_not_allowed');
      if (request.method === 'OPTIONS') {
        if (request.headers.get('Origin') !== baseURL(env).origin) throw new HttpError(403, 'Origin not allowed.', 'origin_not_allowed');
        response = new Response(null, {status:204});
      } else if (url.pathname === '/health' && request.method === 'GET') {
        response = json(await healthStatus(env));
      } else {
        if (!env.DB) throw new HttpError(503, 'The private library database is not connected yet.', 'configuration_required');
        await launcherStorage(env);
        response = await handleLauncher(request, env) || await __dkrUploads.launcherUploadIntegrity(request, env) || await handleAuth(request, env, ctx) || await handleUploads(request, env) || await handleHostedFiles(request, env) || await handleLibrary(request, env, ctx);
        if (!response) throw new HttpError(404, 'This library endpoint does not exist.', 'not_found');
      }
    } catch (error) {
      const known = error instanceof HttpError;
      // Never echo exception details, SQL, OAuth responses or credentials.
      response = json({error:{code:known ? error.code : 'service_error',message:known ? error.message : 'The library service could not complete this request. Please try again.'}},known ? error.status : 500);
    }
    return finish(response, request, env);
  },
  async scheduled(_controller, env, ctx) {
    const cleanup = async () => {
      await cleanupAuth(env);
      await cleanupLibrary(env);
      await cleanupUploads(env);
    };
    ctx.waitUntil(cleanup());
  }
};
