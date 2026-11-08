// Routed before the existing JSON response wrapper so WebSocket upgrades survive.
async function handleOnlineDirectory(request, env) {
  const url = new URL(request.url);
  if (!url.pathname.startsWith('/v1/online/')) return null;
  const headers = { 'Cache-Control': 'no-store', 'Access-Control-Allow-Origin': '*' };
  if (url.pathname === '/v1/online/capabilities' && request.method === 'GET') {
    return Response.json({ protocol: 1, enabled: env.ONLINE_ENABLED === 'true' && !!env.ONLINE_DIRECTORY,
      runningSpectators: env.RUNNING_SPECTATORS_ENABLED === 'true', heartbeatSeconds: 20, leaseSeconds: 60 }, { headers });
  }
  if (env.ONLINE_ENABLED !== 'true' || !env.ONLINE_DIRECTORY)
    return Response.json({ error: { code: 'disabled', message: 'Public lobbies are not enabled yet.' } }, { status: 503, headers });
  if (url.protocol !== 'https:') return Response.json({ error: { code: 'https', message: 'Use HTTPS.' } }, { status: 400, headers });
  if (url.pathname === '/v1/online/connect' && request.method === 'GET') {
    if (request.headers.get('Upgrade')?.toLowerCase() !== 'websocket')
      return Response.json({ error: { code: 'upgrade', message: 'A WebSocket connection is required.' } }, { status: 426, headers });
    const forwarded = new Headers({ 'Upgrade': 'websocket', 'Sec-WebSocket-Protocol': request.headers.get('Sec-WebSocket-Protocol') || '' });
    return env.ONLINE_DIRECTORY.fetch(new Request(url, { headers: forwarded }));
  }
  const user = await __dkrAuth.requireUser(request, env);
  const instance = request.headers.get('X-DKRR-Instance') || '';
  if (!/^[A-Za-z0-9_-]{32,80}$/.test(instance)) throw new HttpError(400, 'Reconnect the launcher to DKR-R Online.', 'instance');
  const credential = (request.headers.get('Authorization') || '').replace(/^Bearer\s+/i, '');
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(credential));
  const session = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
  const forwarded = new Headers();
  forwarded.set('X-DKRR-Account', user.id); forwarded.set('X-DKRR-Name', encodeURIComponent(user.displayName || 'Player'));
  forwarded.set('X-DKRR-Session', session); forwarded.set('X-DKRR-Instance', instance);
  if (request.headers.get('Upgrade')?.toLowerCase() === 'websocket') forwarded.set('Upgrade', 'websocket');
  return env.ONLINE_DIRECTORY.fetch(new Request(url, { method: request.method, headers: forwarded,
    body: request.method === 'POST' ? '{}' : undefined }));
}
