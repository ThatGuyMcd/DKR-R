import { __dkrAuth, __dkrCommon, HttpError, finish } from './library_api.mjs';

const { json, readJSON, requireOrigin, now } = __dkrCommon;
const path = '/v1/notifications';
const fail = (status, code, message) => { throw new HttpError(status, message, code); };

function eventScope(user) {
  const staff = user.role === 'moderator' || user.role === 'admin';
  const scope = staff ? "(e.owner_id=? OR e.audience='staff')" : "e.owner_id=?";
  return scope + " AND (e.audience='staff' OR e.created_at>?)";
}

async function inbox(db, user, cursor) {
  const scope = eventScope(user);
  const cutoff = now() - 90 * 86400;
  const stat = db.prepare(`SELECT COUNT(*) AS unread, COALESCE(MAX(e.id),0) AS latest
    FROM library_notification_events e WHERE `+scope+
    ` AND e.id>COALESCE((SELECT CASE e.audience WHEN 'staff' THEN staff_through ELSE owner_through END FROM library_notification_watermarks WHERE account_id=?),0)
      AND NOT EXISTS (SELECT 1 FROM library_notification_reads r WHERE r.account_id=? AND r.event_id=e.id)`)
    .bind(user.id,cutoff,user.id,user.id);
  const list = db.prepare(`SELECT e.id,e.request_id,e.run_id,e.audience,e.status,e.created_at,
    COALESCE(m.title,b.title) AS title,
    (e.id<=COALESCE(CASE e.audience WHEN 'staff' THEN w.staff_through ELSE w.owner_through END,0) OR r.event_id IS NOT NULL) AS read
    FROM library_notification_events e
    LEFT JOIN library_requests m ON m.id=e.request_id
    LEFT JOIN speedrun_runs s ON s.id=e.run_id
    LEFT JOIN speedrun_boards b ON b.id=s.board_id
    LEFT JOIN library_notification_watermarks w ON w.account_id=?
    LEFT JOIN library_notification_reads r ON r.event_id=e.id AND r.account_id=?
    WHERE `+scope+` AND (? IS NULL OR e.id<?) ORDER BY e.id DESC LIMIT 21`)
    .bind(user.id,user.id,user.id,cutoff,cursor,cursor);
  const [counts, {results}] = await db.batch([stat,list]);
  const [totals] = counts.results;
  return json({ unread:totals.unread, through:totals.latest, nextCursor:results.length>20 ? results[19].id : null,
    events:results.slice(0,20).map(row => ({
      id:row.id, itemId:row.request_id || row.run_id, type:row.request_id ? 'mod' : 'run',
      staff:row.audience === 'staff', status:row.status, title:row.title,
      createdAt:row.created_at, read:!!row.read,
    })),
  });
}

function eventId(value) {
  if (!Number.isSafeInteger(value) || value < 1) fail(400,'invalid_notification','Invalid notification.');
  return value;
}

export async function handleNotifications(request, env) {
  const url = new URL(request.url);
  if (![path,path+'/read',path+'/read-all'].includes(url.pathname)) return null;
  let response;
  try {
    if (url.protocol !== 'https:') fail(400,'https','Use HTTPS.');
    requireOrigin(request,env);
    if (request.method === 'OPTIONS') return finish(new Response(null,{status:204}),request,env);
    const user = await __dkrAuth.requireUser(request,env);
    const db = env.DB?.withSession ? env.DB.withSession('first-primary') : env.DB;
    if (!db) fail(503,'unavailable','Notifications are temporarily unavailable.');
    if (request.method === 'GET' && url.pathname === path) {
      const cursor = url.searchParams.has('before') ? eventId(Number(url.searchParams.get('before'))) : null;
      response = await inbox(db,user,cursor);
    } else if (request.method === 'POST' && url.pathname !== path) {
      const body = await readJSON(request,1024);
      const cutoff = now() - 90 * 86400;
      if (url.pathname === path+'/read') {
        const id = eventId(body.id);
        const result = await db.prepare(`INSERT OR IGNORE INTO library_notification_reads(account_id,event_id)
          SELECT ?,id FROM library_notification_events e WHERE e.id=? AND `+eventScope(user))
          .bind(user.id,id,user.id,cutoff).run();
        if (!result.meta.changes) {
          const accessible = await db.prepare('SELECT id FROM library_notification_events e WHERE e.id=? AND '+eventScope(user))
            .bind(id,user.id,cutoff).first();
          if (!accessible) fail(404,'notification_missing','This notification is no longer available.');
        }
      } else {
        // Use the inbox snapshot limit so arrivals after the click stay unread.
        const through = eventId(body.through);
        await db.prepare(`INSERT INTO library_notification_watermarks(account_id,owner_through,staff_through)
          SELECT ?,COALESCE(MAX(CASE WHEN e.audience='owner' THEN e.id END),0),
            COALESCE(MAX(CASE WHEN e.audience='staff' THEN e.id END),0)
          FROM library_notification_events e WHERE e.id<=? AND `+eventScope(user)+
          ` HAVING COUNT(*)>0 ON CONFLICT(account_id) DO UPDATE SET
            owner_through=MAX(owner_through,excluded.owner_through),
            staff_through=MAX(staff_through,excluded.staff_through)`)
          .bind(user.id,through,user.id,cutoff).run();
      }
      response = await inbox(db,user,null);
    } else fail(405,'method_not_allowed','This notification route does not support that method.');
  } catch (error) {
    const known = error instanceof HttpError;
    response = json({ error:{code:known ? error.code : 'unavailable',
      message:known ? error.message : 'Notifications are temporarily unavailable.'} },known ? error.status : 503);
  }
  return finish(response,request,env);
}

export async function cleanupNotifications(env) {
  if (!env.DB) return;
  await env.DB.prepare(`DELETE FROM library_notification_events WHERE id IN (
    SELECT id FROM library_notification_events WHERE audience='owner' AND created_at<=?
    ORDER BY id LIMIT 200)`).bind(now()-90*86400).run();
}

