import { handleNotifications, cleanupNotifications } from './notification_api.mjs';
import { handleSpeedrun, cleanupSpeedruns } from './speedrun_api.mjs';
import library, { HttpError } from './library_api.mjs';
import { handleLauncherProfile, launcherVerifiedSocial } from './profile_api.mjs';
import { handleCloudSaves } from './cloud_saves_api.mjs';
import { handleCommunity, communityCatalogue, cleanupCommunity } from './community_api.mjs';

// Account totals change slowly. Coalesce concurrent reads and keep one
// aggregate per database binding for at most a minute.
let accountTotals;
async function registeredPlayers(db) {
  if (!db) return null;
  const stamp = Date.now();
  if (accountTotals?.db === db && accountTotals.until > stamp) return accountTotals.result;
  const today = Math.floor(stamp / 86_400_000) * 86400;
  const result = Promise.resolve().then(() => db.prepare(`SELECT COUNT(*) AS total,
    COUNT(CASE WHEN created_at>=? THEN 1 END) AS new7,
    COUNT(CASE WHEN created_at>=? THEN 1 END) AS new30 FROM accounts WHERE status='active'`)
    .bind(today - 6 * 86400, today - 29 * 86400).first()).catch(() => null);
  accountTotals = { db, result, until: stamp + 60_000 };
  return result;
}

// The website uses a read-only projection. Never forward browser credentials
// or actor headers to the directory.
async function publicOnline(request, env, url) {
  const headers = { 'Cache-Control': 'no-store', 'Access-Control-Allow-Origin': '*' };
  const error = (status, code) => Response.json({ error: { code } }, { status, headers });
  if (request.method !== 'GET') return error(405, 'method');
  if (url.protocol !== 'https:') return error(400, 'https');
  if (env.ONLINE_ENABLED !== 'true' || !env.ONLINE_DIRECTORY) return error(503, 'disabled');

  const cursor = url.searchParams.get('cursor') || '';
  if (cursor && !/^[a-f0-9]{32}$/.test(cursor)) return error(400, 'cursor');
  const forwarded = new URL('/v1/online/public', url.origin);
  if (cursor) forwarded.searchParams.set('cursor', cursor);
  try {
    const response = await env.ONLINE_DIRECTORY.fetch(new Request(forwarded));
    if (!response.ok) return error(503, 'unavailable');
    headers['Cache-Control'] = 'public, max-age=15, s-maxage=15';
    const snapshot = await response.json();
    if (!cursor) {
      const accounts = await registeredPlayers(env.DB);
      snapshot.totalSignedUp = accounts?.total ?? null;
      snapshot.newSignups = { '7': accounts?.new7 ?? null, '30': accounts?.new30 ?? null };
    }
    return Response.json(snapshot, { headers });
  } catch {
    return error(503, 'unavailable');
  }
}

export default {
  scheduled(event,env,ctx) {
    ctx.waitUntil(cleanupCommunity(env).catch(() => {}));
    ctx.waitUntil(cleanupNotifications(env).catch(() => { console.error('Notification cleanup failed.'); }));
    ctx.waitUntil(cleanupSpeedruns(env).catch(() => { console.error('Speedrun cleanup failed.'); }));
    return library.scheduled(event,env,ctx);
  },
  async fetch(request, env, ctx) {
    try {
      const notifications = await handleNotifications(request, env);
      if (notifications) return notifications;
      const community = await handleCommunity(request, env, ctx);
      if (community) return community;
      const speedrun = await handleSpeedrun(request, env);
      if (speedrun) return speedrun;
      const cloudSaves = await handleCloudSaves(request, env);
      if (cloudSaves) return cloudSaves;
      const profile = await handleLauncherProfile(request, env);
      if(profile)return profile;
      const url=new URL(request.url);
      if (url.pathname === '/v1/online/public') return publicOnline(request, env, url);
      if(url.pathname==='/v1/online/capabilities' && request.method==='GET') {
        const response=await library.fetch(request,env,ctx);
        if(!response.ok)return response;
        return Response.json({...await response.json(),socialProfiles:true},{status:response.status,headers:response.headers});
      }
      if(env.ONLINE_DIRECTORY && url.pathname.startsWith('/v1/online/')) {
        // Authentication and ticket handling stay with the existing API.
        // Only the authenticated service-binding ingress receives social metadata.
        const binding=env.ONLINE_DIRECTORY;
        const directory={async fetch(input) {
          const forwarded=input instanceof Request?input:new Request(input);
          const headers=new Headers(forwarded.headers);
          headers.delete('X-DKRR-Social');
          const account=headers.get('X-DKRR-Account');
          if(account) {
            const profile=await launcherVerifiedSocial(env,account,new URL(forwarded.url).searchParams.get('profile'));
            if(profile)headers.set('X-DKRR-Social',encodeURIComponent(JSON.stringify(profile)));
          }
          return binding.fetch(new Request(forwarded,{headers}));
        }};
        return library.fetch(request,{...env,ONLINE_DIRECTORY:directory},ctx);
      }
      return communityCatalogue(await library.fetch(request,env,ctx),request,env);
    } catch(error) {
      return Response.json({error:{code:error instanceof HttpError?error.code:'profile_service',
        message:error instanceof HttpError?error.message:'The profile service could not complete this request. Please try again.'}},
        {status:error instanceof HttpError?error.status:500,headers:{'Cache-Control':'no-store','Access-Control-Allow-Origin':'https://positron-portal.com'}});
    }
  }
};
