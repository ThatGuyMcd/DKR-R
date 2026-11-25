// Private service-binding worker. No public workers.dev or HTTP route is enabled.
// The existing library API authenticates every ingress and supplies actor headers.
import { DurableObject } from 'cloudflare:workers';

const PROTOCOL = 1;
const LEASE = 60_000, AUTH_LEASE = 125_000, RESERVATION = 30_000;
const HOUR = 3_600_000, DAY = 24 * HOUR, MAX_HISTORY_HOURS = 30 * 24;
const MAX_BODY = 16_384, MAX_ROOMS = 1000, MAX_SESSIONS = 5000;
const phases = new Set(['waiting', 'testing', 'starting', 'loading', 'racing', 'returning']);
const identifier = /^[A-Za-z0-9_-]{1,160}$/;
const instanceID = /^[A-Za-z0-9_-]{32,80}$/;
const opaque = () => crypto.randomUUID().replaceAll('-', '');
const digest = async value => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value))), b => b.toString(16).padStart(2, '0')).join('');
const json = (data, status = 200) => Response.json(data, { status, headers: { 'Cache-Control': 'no-store' } });
class Fault extends Error {
  close = false;
  /** @type {{ id: string, epoch: string, revision: number, visibility: string } | undefined} */
  current;
  constructor(status, code, message) { super(message); this.status = status; this.code = code; }
}
const need = (condition, status, code, message) => { if (!condition) throw new Fault(status, code, message); };
function text(value, max, label) {
  need(typeof value === 'string' && value.length <= max && !/[\x00-\x1f\x7f]/.test(value), 400, 'invalid', `Invalid ${label}.`);
  return value.trim();
}
function integer(value, max) { need(Number.isSafeInteger(value) && value >= 0 && value <= max, 400, 'invalid', 'Invalid occupancy.'); return value; }
const socialSender = value => typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) && BigInt(value) <= 18446744073709551615n;
function socialProfile(value) {
  if(!value || typeof value !== 'object' || !/^[a-f0-9]{64}$/.test(value.publicKey || '') ||
     !/^[A-Z2-9]{13}$/.test(value.identity || '') || typeof value.friendCode !== 'string' || value.friendCode.length>100 ||
     value.friendCode.length<60 || !/^DKRR-[A-Z2-9-]+$/.test(value.friendCode)) return null;
  const avatar = typeof value.avatar === 'string' && value.avatar.length<=180 &&
    (/^https:\/\/cdn\.discordapp\.com\/(?:avatars\/\d{17,22}\/(?:a_)?[a-f0-9]{32}|embed\/avatars\/[0-5])\.png\?size=128$/.test(value.avatar) ||
     /^https:\/\/dkr-mod-library-api\.ross-macgregorbars\.workers\.dev\/v1\/launcher\/profile-picture\/[a-f0-9]{64}\.png$/.test(value.avatar)) ? value.avatar : '';
  return {publicKey:value.publicKey,identity:value.identity,friendCode:value.friendCode,avatar};
}
function actor(request) {
  const account = request.headers.get('X-DKRR-Account') || '';
  const session = request.headers.get('X-DKRR-Session') || '';
  const instance = request.headers.get('X-DKRR-Instance') || '';
  need(identifier.test(account) && /^[a-f0-9]{64}$/.test(session) && instanceID.test(instance), 401, 'sign_in', 'Sign in with Discord to connect.');
  let social=null;
  try { const raw=request.headers.get('X-DKRR-Social') || ''; if(raw.length<=4096)social=socialProfile(JSON.parse(decodeURIComponent(raw))); } catch {}
  return { account, session, instance, social, name: text(decodeURIComponent(request.headers.get('X-DKRR-Name') || 'Player'), 100, 'display name') };
}
function roomState(input, runningSpectators) {
  need(input && typeof input === 'object' && !Array.isArray(input), 400, 'invalid', 'Invalid lobby state.');
  const state = {
    title: text(input.title, 80, 'lobby name') || 'DKR-R lobby',
    epoch: text(input.epoch, 80, 'room epoch'),
    compatibility: text(input.compatibility, 160, 'compatibility'),
    visibility: input.visibility, phase: input.phase,
    racers: integer(input.racers, 4), maxRacers: integer(input.maxRacers, 4),
    spectators: integer(input.spectators, 8), maxSpectators: integer(input.maxSpectators, 8),
    reservedRacers: integer(input.reservedRacers || 0, 16),
    reservedSpectators: integer(input.reservedSpectators || 0, 16),
    locked: input.locked === true, hidden: input.hidden === true,
    canSpectateRunning: runningSpectators && input.canSpectateRunning === true,
    roster: text(input.roster, 32, 'roster generation'),
    code: text(input.code || '', 5, 'lobby code'), bootstrap: text(input.bootstrap || '', 4096, 'bootstrap'),
    socialHost: socialSender(input.socialHost) ? input.socialHost : '',
    socialRoster: Array.isArray(input.socialRoster) && input.socialRoster.length<=12 && input.socialRoster.every(socialSender)
      ? [...new Set(input.socialRoster)] : []
  };
  need(state.epoch && state.compatibility && state.maxRacers >= 1 && state.racers <= state.maxRacers &&
       state.spectators <= state.maxSpectators && phases.has(state.phase) &&
       ['public', 'private'].includes(state.visibility) && /^[A-Z0-9]{5}$/.test(state.code) && state.bootstrap,
       400, 'invalid', 'Invalid hosted lobby.');
  need(!(state.hidden && state.visibility === 'public'), 409, 'hidden', 'Become visible before publishing a public lobby.');
  return state;
}
const publicRoom = (room, runningSpectators = false) => ({ id: room.id, title: room.title, host: room.hostName,
  compatibility: room.compatibility, phase: room.phase, racers: room.racers, maxRacers: room.maxRacers,
  spectators: room.spectators, maxSpectators: room.maxSpectators, locked: room.locked,
  canJoin: !room.locked && room.phase === 'waiting' && room.racers + room.reservedRacers < room.maxRacers,
  canSpectate: !room.locked && (room.phase === 'waiting' || runningSpectators && room.phase === 'racing' && room.canSpectateRunning) && room.spectators + room.reservedSpectators < room.maxSpectators,
  revision: room.revision });

export class WorldwideDirectory extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env); this.ctx = ctx; this.env = env; this.sessions = new Map(); this.dirty = false;
    this.sql = ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS rooms(id TEXT PRIMARY KEY, owner TEXT NOT NULL, instance TEXT NOT NULL, data TEXT NOT NULL);
      CREATE UNIQUE INDEX IF NOT EXISTS room_owner_instance ON rooms(owner,instance);
      CREATE TABLE IF NOT EXISTS joins(id TEXT PRIMARY KEY, account TEXT NOT NULL, instance TEXT NOT NULL, room TEXT NOT NULL, expires INTEGER NOT NULL, data TEXT NOT NULL);
      CREATE INDEX IF NOT EXISTS join_room ON joins(room,expires);
      CREATE TABLE IF NOT EXISTS social_members(room TEXT NOT NULL,instance TEXT NOT NULL,account TEXT NOT NULL,sender TEXT NOT NULL,PRIMARY KEY(room,instance),UNIQUE(room,sender));
      CREATE TABLE IF NOT EXISTS host_transfers(room TEXT PRIMARY KEY,expires INTEGER NOT NULL,data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS replies(key TEXT PRIMARY KEY, expires INTEGER NOT NULL, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS tickets(hash TEXT PRIMARY KEY, expires INTEGER NOT NULL, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS limits(key TEXT PRIMARY KEY, window INTEGER NOT NULL, count INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS changes(id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS audit(id TEXT PRIMARY KEY, expires INTEGER NOT NULL, account TEXT NOT NULL, event TEXT NOT NULL, room TEXT NOT NULL);
      CREATE INDEX IF NOT EXISTS audit_expiry ON audit(expires);
      CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
      INSERT OR IGNORE INTO meta VALUES('revision',1);
      INSERT OR IGNORE INTO meta VALUES('published',0);
      INSERT OR IGNORE INTO meta VALUES('connection',0);
      CREATE TABLE IF NOT EXISTS online_hourly_peaks(hour INTEGER PRIMARY KEY, players INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS online_monthly_peaks(month INTEGER PRIMARY KEY, players INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS online_yearly_peaks(year INTEGER PRIMARY KEY, players INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS online_peak(singleton INTEGER PRIMARY KEY CHECK(singleton=1), players INTEGER NOT NULL, observed_at INTEGER NOT NULL);`);
    this.onlineHour = -1;
    this.hourPeak = 0;
    for (const socket of ctx.getWebSockets()) {
      const data = socket.deserializeAttachment();
      if (data?.instance && data?.account && (!this.sessions.has(data.instance) || (data.connection || 0) > (this.sessions.get(data.instance).data.connection || 0)))
        this.sessions.set(data.instance, { socket, data });
    }
  }
  rows(query, ...args) { return this.sql.exec(query, ...args).toArray(); }
  revision() { return this.rows("SELECT value FROM meta WHERE key='revision'")[0].value; }
  changed() {
    this.sql.exec("UPDATE meta SET value=value+1 WHERE key='revision'"); this.dirty = true;
    this.schedule(Date.now() + 2000);
  }
  schedule(at) {
    const previous = this.rows("SELECT value FROM meta WHERE key='alarm'")[0]?.value || 0;
    if (!previous || at < previous) {
      this.sql.exec("INSERT OR REPLACE INTO meta VALUES('alarm',?)", at);
      this.ctx.storage.setAlarm(at);
    }
  }
  live(data, now = Date.now()) { return data.lease > now && data.authorized > now; }
  room(id) { const row = this.rows('SELECT data FROM rooms WHERE id=?', id)[0]; return row ? JSON.parse(row.data) : null; }
  roomLive(room) {
    const session = this.sessions.get(room.instance)?.data;
    return !!session && session.account === room.owner && session.room === room.id && this.live(session);
  }
  saveRoom(room) { this.sql.exec('INSERT OR REPLACE INTO rooms VALUES(?,?,?,?)', room.id, room.owner, room.instance, JSON.stringify(room)); }
  publicChange(room, wasPublic) {
    if (room.visibility === 'public') this.sql.exec('INSERT OR REPLACE INTO changes VALUES(?,?)', room.id, JSON.stringify(publicRoom(room, this.env.RUNNING_SPECTATORS_ENABLED === 'true')));
    else if (wasPublic) this.sql.exec('INSERT OR REPLACE INTO changes VALUES(?,?)', room.id, 'null');
  }
  record(account, event, room) {
    this.sql.exec('INSERT INTO audit VALUES(?,?,?,?,?)', opaque(), Date.now() + 86400000, account, event, room);
    this.sql.exec('DELETE FROM audit WHERE id IN (SELECT id FROM audit ORDER BY expires DESC LIMIT -1 OFFSET 10000)');
  }
  send(session, message) {
    try { need(typeof session.socket.bufferedAmount !== 'number' || session.socket.bufferedAmount < 262144, 503, 'slow_client', 'Reconnect to refresh the lobby list.'); session.socket.send(JSON.stringify(message)); }
    catch { try { session.socket.close(1013, 'Reconnect to refresh'); } catch {} }
  }
  count() { return new Set([...this.sessions.values()].filter(s => this.live(s.data)).map(s => s.data.account)).size; }
  observeOnline(players, time = Date.now()) {
    if (players === 0) return;
    try {
      const hour = Math.floor(time / HOUR);
      if (hour !== this.onlineHour) {
        this.hourPeak = this.rows('SELECT players FROM online_hourly_peaks WHERE hour=?', hour)[0]?.players ?? 0;
        this.sql.exec('DELETE FROM online_hourly_peaks WHERE hour<?', hour - MAX_HISTORY_HOURS + 1);
        this.onlineHour = hour;
      }
      if (players <= this.hourPeak) return;
      const date = new Date(time);
      const month = date.getUTCFullYear() * 12 + date.getUTCMonth();
      const day = Math.floor(time / DAY);
      this.ctx.storage.transactionSync(() => {
        this.sql.exec(`INSERT INTO online_hourly_peaks VALUES(?,?)
          ON CONFLICT(hour) DO UPDATE SET players=MAX(players,excluded.players)`, hour, players);
        this.sql.exec(`INSERT INTO online_peak VALUES(1,?,?)
          ON CONFLICT(singleton) DO UPDATE SET players=excluded.players,observed_at=excluded.observed_at
          WHERE excluded.players>online_peak.players`, players, time);
        this.sql.exec(`INSERT INTO online_monthly_peaks VALUES(?,?)
          ON CONFLICT(month) DO UPDATE SET players=MAX(players,excluded.players)`, month, players);
        this.sql.exec('DELETE FROM online_monthly_peaks WHERE month<?', month - 11);
        this.sql.exec(`INSERT INTO online_yearly_peaks VALUES(?,?)
          ON CONFLICT(year) DO UPDATE SET players=MAX(players,excluded.players)`, date.getUTCFullYear(), players);
        const lastDay = this.rows("SELECT value FROM meta WHERE key='online_stats_last_day'")[0]?.value;
        if (lastDay === undefined || day > lastDay) {
          this.sql.exec("INSERT OR IGNORE INTO meta VALUES('online_stats_started',?)", time);
          this.sql.exec("INSERT OR REPLACE INTO meta VALUES('online_stats_last_day',?)", day);
          this.sql.exec(`INSERT INTO meta VALUES('online_stats_active_days',1)
            ON CONFLICT(key) DO UPDATE SET value=value+1`);
        }
      });
      this.hourPeak = players;
    } catch {
      console.error('Failed to record directory activity.');
    }
  }
  onlineHistory(time) {
    const hour = Math.floor(time / HOUR);
    const day = Math.floor(time / DAY);
    const peak = this.rows('SELECT players,observed_at AS observedAt FROM online_peak WHERE singleton=1')[0] ?? null;
    const hourly = this.rows('SELECT hour,players FROM online_hourly_peaks WHERE hour>=? ORDER BY hour',
      (day - 29) * 24);
    const range = days => {
      const cutoff = (day - days + 1) * 24;
      let busiest = null;
      const daily = new Map();
      for (const row of hourly) {
        if (row.hour < cutoff) continue;
        const dayStart = Math.floor(row.hour / 24) * DAY;
        daily.set(dayStart, Math.max(row.players, daily.get(dayStart) ?? 0));
        if (!busiest || row.players > busiest.players) busiest = row;
      }
      return { peak: busiest?.players ?? null,
        busiestHour: busiest ? { startsAt: busiest.hour * HOUR, players: busiest.players } : null,
        activeDays: daily.size, history: [...daily].map(([startsAt, players]) => ({ startsAt, players })) };
    };
    const date = new Date(time);
    const month = date.getUTCFullYear() * 12 + date.getUTCMonth();
    const metadata = Object.fromEntries(this.rows("SELECT key,value FROM meta WHERE key IN ('online_stats_started','online_stats_active_days')").map(row => [row.key,row.value]));
    const oldestMonth = Date.UTC(Math.floor((month - 11) / 12), (month - 11) % 12, 1);
    const yearly = metadata.online_stats_started < oldestMonth;
    const lifetime = yearly
      ? this.rows('SELECT year,players FROM online_yearly_peaks ORDER BY year')
        .map(row => ({ startsAt: Date.UTC(row.year, 0, 1), players: row.players }))
      : this.rows('SELECT month,players FROM online_monthly_peaks WHERE month>=? ORDER BY month', month - 11)
        .map(row => ({ startsAt: Date.UTC(Math.floor(row.month / 12), row.month % 12, 1), players: row.players }));
    return { peak, trackedSince: metadata.online_stats_started ?? null, periods: {
      '7': range(7), '30': range(30), all: {
        peak: peak?.players ?? null,
        busiestHour: peak ? { startsAt: Math.floor(peak.observedAt / HOUR) * HOUR, players: peak.players } : null,
        activeDays: metadata.online_stats_active_days ?? 0, interval: yearly ? 'year' : 'month', history: lifetime
      }
    } };
  }
  list(cursor = '', web = false) {
    need(typeof cursor === 'string' && (!cursor || identifier.test(cursor)), 400, 'cursor', 'Invalid list cursor.');
    const active = this.rows('SELECT data FROM rooms WHERE id>? ORDER BY id', cursor)
      .map(row => JSON.parse(row.data)).filter(room => room.visibility === 'public' && this.roomLive(room));
    const rooms = active.slice(0, 50).map(room => publicRoom(room, this.env.RUNNING_SPECTATORS_ENABLED === 'true'));
    return { rooms, nextCursor: active.length > 50 ? active[49].id : '',
      online: this.count(), revision: this.revision(), serverTime: Date.now(), ...(web && !cursor ? {
        publicLobbies: active.length,
        publicRacers: active.reduce((total, room) => total + room.racers, 0),
        publicSpectators: active.reduce((total, room) => total + room.spectators, 0)
      } : {}) };
  }
  publicSnapshot(cursor) {
    const { rooms, online, nextCursor, serverTime, publicLobbies, publicRacers, publicSpectators } = this.list(cursor, true);
    const snapshot = { online, nextCursor, serverTime,
      rooms: rooms.map(({ id, title, phase, racers, maxRacers, spectators, maxSpectators, canJoin, canSpectate }) => ({
        id, title, phase, racers, maxRacers, spectators, maxSpectators, canJoin, canSpectate
      }))
    };
    if (!cursor) {
      snapshot.stats = { publicLobbies, publicRacers, publicSpectators };
      try {
        Object.assign(snapshot.stats, this.onlineHistory(serverTime));
      } catch { console.error('Failed to read directory activity history.'); }
    }
    return snapshot;
  }
  cleanup() {
    const now = Date.now(); let changed = false;
    for (const [id, session] of this.sessions) if (!this.live(session.data, now)) {
      this.sessions.delete(id); try { session.socket.close(1008, 'Online session expired'); } catch {} changed = true;
    }
    for (const { data } of this.rows('SELECT data FROM rooms')) {
      const room = JSON.parse(data);
      if (!this.roomLive(room)) { this.closeRoom(room, 'The host disconnected.'); changed = true; }
    }
    for (const row of this.rows('SELECT data FROM joins WHERE expires<=?', now)) this.cancelJoin(JSON.parse(row.data), 'The join request expired.');
    this.sql.exec('DELETE FROM replies WHERE expires<=?', now);
    this.sql.exec('DELETE FROM tickets WHERE expires<=?', now);
    this.sql.exec('DELETE FROM host_transfers WHERE expires<=?', now);
    this.sql.exec('DELETE FROM limits WHERE window<?', now - 120000);
    this.sql.exec('DELETE FROM audit WHERE expires<=?', now);
    if (changed) this.changed();
  }
  closeRoom(room, reason) {
    for (const row of this.rows('SELECT data FROM joins WHERE room=?', room.id)) this.cancelJoin(JSON.parse(row.data), reason);
    this.sql.exec('DELETE FROM rooms WHERE id=?', room.id);
    this.sql.exec('DELETE FROM social_members WHERE room=?', room.id);
    this.record(room.owner, 'room_closed', room.id);
    if (room.visibility === 'public') this.sql.exec('INSERT OR REPLACE INTO changes VALUES(?,?)', room.id, 'null');
  }
  cancelJoin(join, message) {
    this.sql.exec('DELETE FROM joins WHERE id=?', join.id);
    const client = this.sessions.get(join.instance), host = this.sessions.get(join.hostInstance);
    if (client?.data.account === join.account) this.send(client, { type: 'join_cancelled', id: join.id, message });
    if (host) this.send(host, { type: 'revoke', id: join.id, room: join.room });
  }
  rate(data, operation) {
    const now = Date.now();
    if (!data.window || now - data.window > 60000) { data.window = now; data.messages = data.codes = 0; }
    if (++data.messages > 180) {
      const error = new Fault(429, 'rate_limit', 'Too many online requests. Try again shortly.'); error.close = true; throw error;
    }
    if (operation === 'resolve') this.accountLimit(data.account, 'codes', 8);
  }
  accountLimit(account, bucket, limit) {
    const key = account + ':' + bucket, now = Date.now();
    this.sql.exec(`INSERT INTO limits VALUES(?,?,1) ON CONFLICT(key) DO UPDATE SET
      window=CASE WHEN window<=? THEN excluded.window ELSE window END,
      count=CASE WHEN window<=? THEN 1 ELSE count+1 END`, key, now, now - 60000, now - 60000);
    need(this.rows('SELECT count FROM limits WHERE key=?', key)[0].count <= limit,
      429, 'rate_limit', 'Too many attempts. Wait a minute before trying again.');
  }
  async fetch(request) {
    try {
      const url = new URL(request.url);
      // This route is only reachable through the private service binding.
      if (url.pathname === '/v1/online/public' && request.method === 'GET')
        return json(this.publicSnapshot(url.searchParams.get('cursor') || ''));
      let who;
      if (url.pathname === '/v1/online/connect') {
        const protocols = (request.headers.get('Sec-WebSocket-Protocol') || '').split(',').map(s => s.trim());
        const ticket = protocols.find(p => /^dkrr-ticket-[a-f0-9]{64}$/.test(p))?.slice(12);
        need(ticket && protocols.includes('dkrr-online-v1'), 401, 'ticket', 'Sign in to DKR-R Online again.');
        const hash = await digest(ticket), row = this.rows('SELECT data FROM tickets WHERE hash=? AND expires>?', hash, Date.now())[0];
        need(row, 401, 'ticket', 'This connection ticket expired or was already used.');
        this.sql.exec('DELETE FROM tickets WHERE hash=?', hash); who = JSON.parse(row.data);
      } else who = actor(request);
      this.cleanup();
      if (url.pathname === '/v1/online/ticket' && request.method === 'POST') {
        this.accountLimit(who.account, 'tickets', 20);
        need(this.rows('SELECT COUNT(*) AS n FROM tickets WHERE json_extract(data,\'$.account\')=?', who.account)[0].n < 8,
          429, 'tickets', 'Too many connection attempts. Wait a moment.');
        const ticket = opaque() + opaque(), hash = await digest(ticket);
        this.sql.exec('INSERT INTO tickets VALUES(?,?,?)', hash, Date.now() + 15000, JSON.stringify(who));
        this.schedule(Date.now() + 15000);
        return json({ ticket, protocol: PROTOCOL, expiresIn: 15 });
      }
      if (url.pathname === '/v1/online/connect') {
        need(request.headers.get('Upgrade')?.toLowerCase() === 'websocket', 426, 'upgrade', 'A WebSocket connection is required.');
        need(this.sessions.size < MAX_SESSIONS, 503, 'busy', 'The online directory is busy. Try again shortly.');
        const others = [...this.sessions.values()].filter(s => s.data.account === who.account && s.data.instance !== who.instance);
        need(others.length < 4, 429, 'instances', 'Disconnect another DKR-R instance before connecting.');
        const old = this.sessions.get(who.instance);
        need(!old || old.data.account === who.account, 409, 'instance', 'This launcher instance is already connected.');
        for (const row of this.rows('SELECT data FROM joins WHERE instance=? OR json_extract(data,\'$.hostInstance\')=?', who.instance, who.instance))
          this.cancelJoin(JSON.parse(row.data), 'A launcher reconnected. Please request to join again.');
        const pair = new WebSocketPair(), client = pair[0], socket = pair[1], now = Date.now();
        this.sql.exec("UPDATE meta SET value=value+1 WHERE key='connection'");
        const connection = this.rows("SELECT value FROM meta WHERE key='connection'")[0].value;
        const data = { ...who, socketId: opaque(), connection, lease: now + LEASE, authorized: now + AUTH_LEASE, room: old?.data.room || '' };
        this.ctx.acceptWebSocket(socket); socket.serializeAttachment(data);
        this.sessions.set(who.instance, { socket, data });
        if (old) { try { old.socket.close(1000, 'Reconnected'); } catch {} }
        this.changed();
        const snapshot = this.list();
        this.observeOnline(snapshot.online, now);
        this.send({ socket, data }, { type: 'snapshot', ...snapshot });
        return new Response(null, { status: 101, webSocket: client, headers: { 'Sec-WebSocket-Protocol': 'dkrr-online-v1' } });
      }
      const current = this.sessions.get(who.instance);
      need(current && current.data.account === who.account && current.data.session === who.session, 409, 'connect', 'Connect the launcher to DKR-R Online first.');
      if (url.pathname === '/v1/online/session' && request.method === 'POST') {
        current.data.authorized = Date.now() + AUTH_LEASE; current.data.social=who.social; current.socket.serializeAttachment(current.data);
        return json({ ok: true, protocol: PROTOCOL, authorizedFor: AUTH_LEASE });
      }
      if (url.pathname === '/v1/online/disconnect' && request.method === 'POST') {
        current.data.lease = 0; current.socket.serializeAttachment(current.data); this.cleanup(); return json({ ok: true });
      }
      if (url.pathname === '/v1/online/lobbies' && request.method === 'GET') return json(this.list(url.searchParams.get('cursor') || ''));
      throw new Fault(404, 'not_found', 'This online endpoint does not exist.');
    } catch (error) { return json({ error: { code: error.code || 'service_error', message: error instanceof Fault ? error.message : 'The online directory is unavailable. Try again shortly.' } }, error.status || 500); }
  }
  async webSocketMessage(socket, message) {
    let request = '';
    try {
      need(this.env.ONLINE_ENABLED === 'true', 503, 'disabled', 'Public lobbies have been disabled.');
      const data = socket.deserializeAttachment(), current = this.sessions.get(data?.instance);
      need(current?.data.socketId === data?.socketId && this.live(data), 401, 'expired', 'Reconnect to DKR-R Online.');
      this.rate(data, null); current.data = data; socket.serializeAttachment(data);
      need(typeof message === 'string' && message.length <= MAX_BODY, 400, 'size', 'Invalid online message.');
      const input = JSON.parse(message);
      need(input && typeof input === 'object' && !Array.isArray(input), 400, 'invalid', 'Invalid online message.');
      request = text(input.request || '', 80, 'request identifier');
      need(identifier.test(request), 400, 'request', 'Invalid request identifier.');
      if (input.type === 'resolve') this.accountLimit(data.account, 'codes', 8);
      const key = data.account + ':' + data.instance + ':' + request;
      const transient = ['heartbeat', 'list', 'social'].includes(input.type);
      const prior = transient ? null : this.rows('SELECT data FROM replies WHERE key=? AND expires>?', key, Date.now())[0];
      if (prior) { this.send(current, JSON.parse(prior.data)); return; }
      if (!transient)
        need(this.rows('SELECT COUNT(*) AS n FROM replies WHERE key>=? AND key<?', data.account + ':', data.account + ';')[0].n < 512, 429, 'busy', 'Wait for earlier requests to finish.');
      const result = this.operation(current, input);
      const reply = { type: 'result', request, ok: true, data: result };
      if (!transient) {
        this.sql.exec('INSERT OR REPLACE INTO replies VALUES(?,?,?)', key, Date.now() + 120000, JSON.stringify(reply));
      }
      this.send(current, reply);
    } catch (error) {
      const data = socket.deserializeAttachment();
      this.send({ socket, data }, { type: 'result', request, ok: false, error: { code: error.code || 'invalid', message: error instanceof Fault ? error.message : 'Invalid online request.', current: error.current } });
      if (error.status === 401) { try { socket.close(1008, 'Reconnect to sign in'); } catch {} }
      if (error.code === 'disabled') { try { socket.close(1012, 'Online service disabled'); } catch {} }
      if (error.close) { try { socket.close(1008, 'Too many online messages'); } catch {} }
    }
  }
  operation(current, input) {
    const who = current.data;
    if (input.type === 'heartbeat') {
      who.lease = Date.now() + LEASE; current.socket.serializeAttachment(who);
      return { online: this.count(), revision: this.revision(), serverTime: Date.now() };
    }
    if (input.type === 'list') return this.list(input.cursor || '');
    if (input.type === 'transfer_prepare' || input.type === 'transfer_accept' || input.type === 'transfer_commit') {
      const serial=text(input.serial,20,'transfer serial'),epoch=text(input.epoch,80,'room epoch');
      const hash=text(input.digest,64,'prepared session digest'),code=text(input.code,5,'lobby code');
      const nextCode=text(input.nextCode,5,'successor code'),sender=text(input.sender,20,'successor identity');
      need(/^[1-9][0-9]{0,19}$/.test(serial)&&/^[a-f0-9]{64}$/.test(hash)&&socialSender(sender)&&
        /^[A-Z0-9]{5}$/.test(code)&&/^[A-Z0-9]{5}$/.test(nextCode),400,'transfer','Invalid prepared host transfer.');
      this.sql.exec('DELETE FROM host_transfers WHERE expires<=?',Date.now());
      const prior=this.rows('SELECT data FROM host_transfers').map(row=>JSON.parse(row.data)).find(t=>
        t.epoch===epoch&&t.serial===serial&&t.digest===hash&&t.oldAccount===who.account&&t.oldInstance===who.instance&&t.completed);
      if(input.type==='transfer_commit'&&prior)return prior.result;
      const room=this.rows('SELECT data FROM rooms').map(row=>JSON.parse(row.data)).find(r=>r.code===code&&r.epoch===epoch);
      need(room&&this.roomLive(room),409,'transfer_room','The original lobby is no longer available.');
      need(['waiting','testing'].includes(room.phase),409,'transfer_phase','Return to the lobby before transferring host.');
      if(input.type==='transfer_prepare') {
        need(room.owner===who.account&&room.instance===who.instance,403,'owner','Only the current host can prepare this transfer.');
        const member=this.rows('SELECT account,instance FROM social_members WHERE room=? AND sender=?',room.id,sender)[0];
        need(member&&room.socialRoster.includes(sender)&&member.instance!==room.instance,
          403,'transfer_member','The proposed host is not an admitted lobby member.');
        const candidate=this.sessions.get(member.instance);
        need(candidate&&this.live(candidate.data)&&candidate.data.account===member.account,409,'transfer_left','The proposed host disconnected.');
        const previous=this.rows('SELECT data FROM host_transfers WHERE room=?',room.id)[0];
        const pending=previous&&JSON.parse(previous.data);
        if(pending&&!pending.completed&&pending.serial===serial&&pending.digest===hash&&pending.sender===sender)return {prepared:true,serial,digest:hash};
        const transfer={room:room.id,epoch,serial,digest:hash,code,nextCode,sender,account:member.account,instance:member.instance,
          oldAccount:room.owner,oldInstance:room.instance,completed:false,accepted:false};
        this.sql.exec('INSERT OR REPLACE INTO host_transfers VALUES(?,?,?)',room.id,Date.now()+120000,JSON.stringify(transfer));
        return {prepared:true,serial,digest:hash};
      }
      if(input.type==='transfer_accept') {
        const row=this.rows('SELECT data FROM host_transfers WHERE room=?',room.id)[0],pending=row&&JSON.parse(row.data);
        need(pending&&!pending.completed&&pending.instance===who.instance&&pending.account===who.account&&pending.serial===serial&&
          pending.digest===hash&&pending.sender===sender&&pending.nextCode===nextCode,
          403,'transfer_member','Only the admitted successor can accept the host prepared session.');
        pending.accepted=true;this.sql.exec('UPDATE host_transfers SET data=?,expires=? WHERE room=?',JSON.stringify(pending),Date.now()+120000,room.id);
        return {accepted:true,serial,digest:hash};
      }
      need(room.owner===who.account&&room.instance===who.instance,403,'owner','Only the current host can commit this transfer.');
      const row=this.rows('SELECT data FROM host_transfers WHERE room=?',room.id)[0],transfer=row&&JSON.parse(row.data);
      need(transfer&&!transfer.completed&&transfer.accepted&&transfer.serial===serial&&transfer.digest===hash&&transfer.nextCode===nextCode&&transfer.sender===sender,
        409,'transfer_consent','Waiting for the admitted successor to confirm the same session.');
      const candidate=this.sessions.get(transfer.instance),member=this.rows('SELECT account,sender FROM social_members WHERE room=? AND instance=?',room.id,transfer.instance)[0];
      need(candidate&&this.live(candidate.data)&&candidate.data.account===transfer.account&&member?.account===transfer.account&&member.sender===sender&&
        room.socialRoster.includes(sender),409,'transfer_left','The proposed host disconnected.');
      need(!this.rows('SELECT id FROM rooms WHERE owner=? AND instance=?',transfer.account,transfer.instance).length,
        409,'transfer_busy','The proposed host already owns another lobby.');
      for(const join of this.rows('SELECT data FROM joins WHERE room=?',room.id))this.cancelJoin(JSON.parse(join.data),'The host transferred the lobby. Please request to join again.');
      const oldHost=room.socialHost,wasPublic=room.visibility==='public';
      const next={...room,owner:transfer.account,instance:transfer.instance,hostName:candidate.data.name,socialHost:sender,
        code:nextCode,bootstrap:nextCode,revision:room.revision+1,generation:room.generation+1};
      const result={id:next.id,epoch:next.epoch,revision:next.revision,visibility:next.visibility,serial,digest:hash};
      transfer.completed=true;transfer.result=result;
      this.ctx.storage.transactionSync(()=>{
        this.saveRoom(next);
        this.sql.exec('DELETE FROM social_members WHERE room=? AND instance=?',room.id,transfer.instance);
        if(socialSender(oldHost))this.sql.exec('INSERT OR REPLACE INTO social_members VALUES(?,?,?,?)',room.id,who.instance,who.account,oldHost);
        this.sql.exec('UPDATE host_transfers SET expires=?,data=? WHERE room=?',Date.now()+120000,JSON.stringify(transfer),room.id);
      });
      who.room='';current.socket.serializeAttachment(who);candidate.data.room=room.id;candidate.socket.serializeAttachment(candidate.data);
      this.send(candidate,{type:'host_transferred',...result});this.publicChange(next,wasPublic);this.changed();this.record(who.account,'host_transferred',room.id);
      return result;
    }
    if (input.type === 'social') {
      const member=this.rows('SELECT room,sender FROM social_members WHERE instance=? AND account=?',who.instance,who.account)
        .map(row=>({row,room:this.room(row.room)})).find(entry=>entry.room?.code===input.code && entry.room.socialRoster?.includes(entry.row.sender));
      const owned=this.room(who.room);
      const host=owned?.owner===who.account && owned?.instance===who.instance && owned?.code===input.code &&
        owned.socialHost===input.sender && owned.socialRoster?.includes(input.sender);
      const room=host?owned:member?.room;
      need(room && this.roomLive(room) && (host || member.row.sender===input.sender),403,'social_membership','Join this lobby before viewing its player profiles.');
      const rows=this.rows('SELECT account,instance,sender FROM social_members WHERE room=?',room.id);
      rows.push({account:room.owner,instance:room.instance,sender:room.socialHost});
      const members=[];
      for(const row of rows){
        const peer=this.sessions.get(row.instance);
        if(!room.socialRoster.includes(row.sender) || !peer || !this.live(peer.data) || peer.data.account!==row.account)continue;
        const profile=socialProfile(peer.data.social);
        if(profile && !members.some(value=>value.sender===row.sender))members.push({sender:row.sender,...profile});
      }
      return {code:room.code,sender:input.sender,members};
    }
    if (input.type === 'room') {
      const state = roomState(input.state, this.env.RUNNING_SPECTATORS_ENABLED === 'true'), existing = this.rows('SELECT data FROM rooms WHERE owner=? AND instance=?', who.account, who.instance)[0];
      let room = existing && JSON.parse(existing.data);
      if (room && room.epoch !== state.epoch) { this.closeRoom(room, 'The host created a new lobby.'); room = null; }
      need(room || this.rows('SELECT COUNT(*) AS n FROM rooms')[0].n < MAX_ROOMS, 503, 'busy', 'The lobby directory is full.');
      if (room && integer(input.revision, Number.MAX_SAFE_INTEGER) !== room.revision) {
        const error = new Fault(409, 'revision', 'Refreshing the hosted lobby after reconnecting.');
        error.current = { id: room.id, epoch: room.epoch, revision: room.revision, visibility: room.visibility };
        throw error;
      }
      const invalidate = room && (room.visibility !== state.visibility || room.locked !== state.locked || room.phase !== state.phase || room.roster !== state.roster || room.code !== state.code || room.compatibility !== state.compatibility || room.maxRacers !== state.maxRacers || room.maxSpectators !== state.maxSpectators);
      if (invalidate) for (const row of this.rows('SELECT data FROM joins WHERE room=?', room.id)) this.cancelJoin(JSON.parse(row.data), 'The host changed the lobby. Please try again.');
      const wasPublic = room?.visibility === 'public';
      const changedVisibility = !room || room.visibility !== state.visibility;
      room = { ...state, id: room?.id || opaque(), owner: who.account, instance: who.instance, hostName: who.name,
        revision: (room?.revision || 0) + 1, generation: (room?.generation || 0) + (invalidate || !room ? 1 : 0) };
      this.saveRoom(room); this.publicChange(room, wasPublic); who.room = room.id; current.socket.serializeAttachment(who); this.changed();
      if (changedVisibility) this.record(who.account, state.visibility === 'public' ? 'room_public' : 'room_private', room.id);
      return { id: room.id, epoch: room.epoch, revision: room.revision, visibility: room.visibility };
    }
    if (input.type === 'close') {
      const room = this.room(who.room);
      if (room && room.owner === who.account && room.instance === who.instance) this.closeRoom(room, 'The host closed the lobby.');
      who.room = ''; current.socket.serializeAttachment(who); this.changed(); return { closed: true };
    }
    if (input.type === 'join' || input.type === 'resolve') {
      const role = input.role; need(role === 'racer' || role === 'spectator', 400, 'role', 'Choose Racer or Spectator.');
      const key = text(input.key, 80, 'handshake key'); need(/^[a-f0-9]{64}$/.test(key), 400, 'key', 'Invalid handshake key.');
      let room;
      if (input.type === 'resolve') {
        const code = text(input.code, 5, 'lobby code').toUpperCase();
        need(/^[A-Z0-9]{5}$/.test(code), 400, 'code', 'Enter the five-character lobby code.');
        room = this.rows('SELECT data FROM rooms').map(r => JSON.parse(r.data)).find(r => r.code === code && this.roomLive(r));
      } else {
        room = this.room(text(input.room, 80, 'lobby identifier'));
        if (room?.visibility !== 'public') room = null;
      }
      need(room && this.roomLive(room), 404, 'unavailable', 'This lobby is no longer available.');
      need(room.instance !== who.instance, 409, 'own_room', 'You are already hosting this lobby.');
      const running = role === 'spectator' && room.phase === 'racing' && room.canSpectateRunning && this.env.RUNNING_SPECTATORS_ENABLED === 'true';
      need(!room.locked && (room.phase === 'waiting' || running), 409, 'phase', 'The host is not accepting joins right now.');
      need(text(input.compatibility, 160, 'compatibility') === room.compatibility, 409, 'incompatible', 'This lobby requires a different DKR-R network build.');
      const pending = this.rows('SELECT data FROM joins WHERE expires>?', Date.now()).map(r => JSON.parse(r.data));
      need(!pending.some(j => j.instance === who.instance), 409, 'pending', 'Cancel your current join before joining another lobby.');
      const reserved = pending.filter(j => j.room === room.id && j.role === role).length;
      const full = role === 'racer' ? room.racers + room.reservedRacers + reserved >= room.maxRacers : room.spectators + room.reservedSpectators + reserved >= room.maxSpectators;
      need(!full && pending.filter(j => j.room === room.id).length < 16, 409, 'full', 'There are no unreserved places for that role.');
      const join = { id: opaque(), room: room.id, epoch: room.epoch, revision: room.revision, generation: room.generation,
        public: input.type === 'join', running,
        account: who.account, instance: who.instance, hostInstance: room.instance, role, key,
        name: who.name, expires: Date.now() + RESERVATION, granted: false,
        friendAdmission: text(input.friendAdmission || '', 80, 'friend admission') };
      this.sql.exec('INSERT INTO joins VALUES(?,?,?,?,?,?)', join.id, who.account, who.instance, room.id, join.expires, JSON.stringify(join));
      this.send(this.sessions.get(room.instance), { type: 'intent', join });
      this.schedule(join.expires);
      return { id: join.id, expiresAt: join.expires, status: 'contacting_host' };
    }
    if (input.type === 'grant') {
      const row = this.rows('SELECT data FROM joins WHERE id=? AND expires>?', text(input.id, 80, 'join identifier'), Date.now())[0];
      need(row, 409, 'expired', 'The join request expired.');
      const join = JSON.parse(row.data), room = this.room(join.room);
      need(room && room.owner === who.account && room.instance === who.instance && room.generation === join.generation && room.epoch === join.epoch && this.roomLive(room), 403, 'owner', 'This join no longer belongs to your lobby.');
      if (!input.allowed) { this.cancelJoin(join, 'The host declined this join request.'); return { declined: true }; }
      need(!join.granted && !room.locked && (room.phase === 'waiting' || join.running && join.role === 'spectator' && room.phase === 'racing' && room.canSpectateRunning && this.env.RUNNING_SPECTATORS_ENABLED === 'true'), 409, 'changed', 'The lobby is not accepting this request.');
      const admission = text(input.admission, 80, 'admission'); need(/^[a-f0-9]{64}$/.test(admission), 400, 'admission', 'Invalid admission capability.');
      const target = this.sessions.get(join.instance); need(target?.data.account === join.account && this.live(target.data), 409, 'left', 'The joining player disconnected.');
      join.granted = true;
      this.sql.exec('UPDATE joins SET data=? WHERE id=?', JSON.stringify(join), join.id);
      this.send(target, { type: 'bootstrap', id: join.id, room: join.room, epoch: join.epoch, role: join.role,
        key: join.key, code: room.code, bootstrap: room.bootstrap, admission, expiresAt: join.expires });
      this.record(join.account, 'join_bootstrap', room.id);
      return { granted: true };
    }
    if (input.type === 'consume' || input.type === 'cancel') {
      const row = this.rows('SELECT data FROM joins WHERE id=?', text(input.id, 80, 'join identifier'))[0];
      if (!row) return { done: true };
      const join = JSON.parse(row.data), room = this.room(join.room);
      const host = room?.owner === who.account && room.instance === who.instance;
      need(host || input.type === 'cancel' && join.account === who.account && join.instance === who.instance, 403, 'owner', 'This request belongs to another player.');
      if (input.type === 'consume') {
        need(join.granted,409,'social_admission','The native join has not been authorized.');
        if(socialSender(input.sender) && this.rows('SELECT COUNT(*) AS n FROM social_members WHERE room=?',room.id)[0].n<64)
          this.sql.exec('INSERT OR REPLACE INTO social_members VALUES(?,?,?,?)',room.id,join.instance,join.account,input.sender);
        this.sql.exec('DELETE FROM joins WHERE id=?', join.id);
        const client = this.sessions.get(join.instance);
        if (client?.data.account === join.account) this.send(client, { type: 'join_consumed', id: join.id });
      }
      else this.cancelJoin(join, 'The join was cancelled.');
      return { done: true };
    }
    throw new Fault(400, 'operation', 'This online operation is not supported.');
  }
  async webSocketClose(socket) {
    const data = socket.deserializeAttachment(), current = this.sessions.get(data?.instance);
    if (current?.data.socketId === data?.socketId) {
      for (const row of this.rows('SELECT data FROM joins WHERE account=? AND instance=?', data.account, data.instance))
        this.cancelJoin(JSON.parse(row.data), 'The joining launcher disconnected.');
      this.sessions.delete(data.instance); this.cleanup(); this.changed();
    }
    try { socket.close(1000, 'Disconnected'); } catch {}
  }
  async webSocketError(socket) { await this.webSocketClose(socket); }
  async alarm() {
    this.sql.exec("INSERT OR REPLACE INTO meta VALUES('alarm',0)");
    this.cleanup();
    const online = this.count();
    this.observeOnline(online);
    const revision = this.revision();
    if (revision > this.rows("SELECT value FROM meta WHERE key='published'")[0].value) {
      const changes = this.rows('SELECT id,data FROM changes');
      const batches = Math.max(1, Math.ceil(changes.length / 50));
      for (let index = 0; index < batches; index++) {
        const batch = changes.slice(index * 50, (index + 1) * 50);
        const delta = { type: 'delta', revision, online,
          upsert: batch.filter(row => row.data !== 'null').map(row => JSON.parse(row.data)),
          remove: batch.filter(row => row.data === 'null').map(row => row.id) };
        for (const session of this.sessions.values()) if (this.live(session.data)) this.send(session, delta);
      }
      this.sql.exec('DELETE FROM changes');
      this.sql.exec("UPDATE meta SET value=? WHERE key='published'", revision);
      this.dirty = false;
    }
    const deadlines = [...this.sessions.values()].map(s => Math.min(s.data.lease, s.data.authorized));
    const joinExpiry = this.rows('SELECT MIN(expires) AS value FROM joins')[0]?.value;
    const ticketExpiry = this.rows('SELECT MIN(expires) AS value FROM tickets')[0]?.value;
    if (joinExpiry) deadlines.push(joinExpiry);
    if (ticketExpiry) deadlines.push(ticketExpiry);
    if (deadlines.length) this.schedule(Math.max(Date.now() + 100, Math.min(...deadlines)));
  }
}

export default {
  async fetch(request, env) {
    if (env.ONLINE_ENABLED !== 'true') return json({ error: { code: 'disabled', message: 'Public lobbies are not enabled yet.' } }, 503);
    return env.DIRECTORY.get(env.DIRECTORY.idFromName('dkrr-worldwide-v1')).fetch(request);
  }
};
