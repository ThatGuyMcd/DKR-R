import { __dkrAuth, HttpError } from './library_api.mjs';

const cloudSaveRoute = '/v1/launcher/cloud-saves';
const cloudSaveRecordsLimit = 20 + 8192 * 76;
const cloudSavePakSize = 32 * 1024;
const cloudSaveLimits = Object.freeze({
  'offline-adventure': [512, 512],
  'modded-adventure': [512, 512],
  'offline-pak-1': [cloudSavePakSize, cloudSavePakSize],
  'offline-pak-2': [cloudSavePakSize, cloudSavePakSize],
  'offline-pak-3': [cloudSavePakSize, cloudSavePakSize],
  'offline-pak-4': [cloudSavePakSize, cloudSavePakSize],
  'modded-records': [20, cloudSaveRecordsLimit],
  'online-adventure': [512, 512],
  'online-records': [20, cloudSaveRecordsLimit],
  'online-paks': [4 * cloudSavePakSize, 4 * cloudSavePakSize],
});
const cloudSaveHeaders = {'Cache-Control': 'private, no-store', 'X-Content-Type-Options': 'nosniff'};

function cloudSaveRequire(condition, status, code, message) {
  if (!condition) throw new HttpError(status, message, code);
}

async function readCloudSaveImage(request, [min, max]) {
  cloudSaveRequire(request.headers.get('Content-Type') === 'application/octet-stream',
    415, 'cloud_save_type', 'Upload a DKR-R save image.');
  const reader = request.body?.getReader();
  cloudSaveRequire(reader, 400, 'cloud_save_empty', 'The save image is missing.');
  const buffer = new Uint8Array(max);
  let offset = 0;
  try {
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      cloudSaveRequire(offset + value.length <= max, 413, 'cloud_save_size', 'That save image is too large.');
      buffer.set(value, offset);
      offset += value.length;
    }
  } finally {
    try { await reader.cancel(); } catch {}
  }
  cloudSaveRequire(offset >= min, 400, 'cloud_save_size', 'That save image is incomplete.');
  return buffer.slice(0, offset);
}

export async function handleCloudSaves(request, env) {
  const url = new URL(request.url);
  if (url.pathname !== cloudSaveRoute && !url.pathname.startsWith(cloudSaveRoute + '/')) return null;
  cloudSaveRequire(url.protocol === 'https:', 400, 'cloud_save_https', 'Use HTTPS for Cloud Saves.');
  const mode = env.CLOUD_SAVES_ENABLED;
  cloudSaveRequire(mode === 'true' || mode === 'pilot', 503, 'cloud_saves_disabled', 'Cloud Saves are currently unavailable.');
  const {id} = await __dkrAuth.requireUser(request, env);
  const pilot = mode === 'pilot' ? 1 : 0;
  cloudSaveRequire(!pilot || id === env.CLOUD_SAVES_PILOT_ACCOUNT, 403, 'cloud_saves_private',
    'Cloud Saves are currently in private testing.');
  const db = env.DB;
  if (url.pathname === cloudSaveRoute) {
    cloudSaveRequire(request.method === 'GET', 405, 'cloud_save_method', 'Use GET to check your Cloud Saves.');
    const includeModdedAdventure = url.searchParams.get('format') === '2';
    const inventory = `SELECT save_key AS key, revision, digest, length(image) AS bytes,
      updated_at AS updatedAt FROM launcher_cloud_saves WHERE account_id=?1 AND pilot=?2`;
    // Older launchers cap their inventory at nine save types.
    const modded = includeModdedAdventure ? ` UNION ALL SELECT save_key, revision, digest, length(image),
      updated_at FROM launcher_modded_adventure WHERE account_id=?1 AND pilot=?2` : '';
    const {results} = await db.prepare(inventory + modded + ' ORDER BY key').bind(id, pilot).all();
    return Response.json({
      accountId: id, saves: results, ...(includeModdedAdventure && {moddedAdventure: true}),
    }, {headers: cloudSaveHeaders});
  }

  const key = url.pathname.slice(cloudSaveRoute.length + 1);
  cloudSaveRequire(Object.hasOwn(cloudSaveLimits, key), 404, 'cloud_save_key', 'That save type is unavailable.');
  const table = key === 'modded-adventure' ? 'launcher_modded_adventure' : 'launcher_cloud_saves';
  const rawRevision = url.searchParams.get('revision');
  const revision = rawRevision !== null && /^\d{1,15}$/.test(rawRevision) ? Number(rawRevision) : -1;
  cloudSaveRequire(Number.isSafeInteger(revision) && revision >= 0, 400, 'cloud_save_revision', 'Refresh your save revisions.');
  if (request.method === 'GET') {
    const row = await db.prepare(`SELECT image FROM ${table} WHERE account_id=?1 AND save_key=?2 AND revision=?3 AND pilot=?4`)
      .bind(id, key, revision, pilot).first();
    cloudSaveRequire(row, 409, 'cloud_save_conflict', 'Your cloud save changed on another device. Sync again before replacing it.');
    return new Response(new Uint8Array(row.image), {headers:{...cloudSaveHeaders, 'Content-Type':'application/octet-stream'}});
  }
  cloudSaveRequire(request.method === 'PUT', 405, 'cloud_save_method', 'Use PUT to sync a save image.');
  const image = await readCloudSaveImage(request, cloudSaveLimits[key]);
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', image)),
    byte => byte.toString(16).padStart(2, '0')).join('');
  const row = revision === 0
    ? await db.prepare(`INSERT INTO ${table} (account_id,save_key,revision,digest,image,updated_at,pilot)
        VALUES (?1,?2,1,?3,?4,unixepoch(),?5) ON CONFLICT(account_id,pilot,save_key) DO NOTHING RETURNING revision`)
        .bind(id,key,digest,image.buffer,pilot).first()
    : await db.prepare(`UPDATE ${table} SET revision=revision+1,digest=?3,image=?4,updated_at=unixepoch()
        WHERE account_id=?1 AND save_key=?2 AND revision=?5 AND pilot=?6 RETURNING revision`)
        .bind(id,key,digest,image.buffer,revision,pilot).first();
  cloudSaveRequire(row, 409, 'cloud_save_conflict', 'Your cloud save changed on another device. Sync again before replacing it.');
  return Response.json({revision: row.revision, digest}, {headers: cloudSaveHeaders});
}
