// OpenBFME online lobby tests (lane ONLINE-2): node --test services/online/lobby/test
// The ticket text is pinned to the same value engine/tests/test_online_relay.cpp pins (computed with Python's hmac module): the lobby signs what the relay
// checks. The flow covers sign-in, create, list, join (profile check), slots, ready, chat, kick, START (one ticket per human slot) and the relay log upload,
// first on the core with a fake clock, then through the self-hosted HTTP server.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { MAX_WAIT_S, MAX_WAITERS_PER_GAME, MAX_HOLDS_PER_ACCOUNT, HOLD_BACKOFF_S, Lobby, route, signTicket, signRejoinTicket, signSpectatorTicket, base64UrlDecode, parseKeyRing, configFromEnv, LobbyError } from '../src/lobby.mjs';
import { startServer, nodeEnv } from '../src/server.mjs';
// node:sqlite (Node 22.5+) stands in for a Durable Object's SQLite storage; an older Node skips those tests loudly
const sqlite = await import('node:sqlite').catch(() => null);
const NO_SQLITE = !sqlite && 'SKIP LOUDLY: node:sqlite (Node 22.5+) is missing, so the Durable Object SQLite log store tests did not run';

const KEY_HEX = Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, '0')).join('');
const PROFILE = '9ecbb999bef4925c679a5570eacd6b99';
const ENV = { TICKET_KEYS: `# test\n1:${KEY_HEX}\n`, SESSION_KEY: 'aa'.repeat(32), UPLOAD_KEY: 'bb'.repeat(32), RELAYS: 'lab=192.0.2.10:7350', PUBLIC_URL: 'http://lobby.test' };

function fakeEnv() {
  let t = 1_700_000_000_000;
  let r = 0;
  const blobs = new Map();
  const env = nodeEnv({ putBlob: (k, b) => blobs.set(k, b), getBlob: (k) => blobs.get(k) ?? null });
  env.now = () => t;
  env.advance = (ms) => { t += ms; };
  env.randomBytes = (n) => Uint8Array.from({ length: n }, () => (r = (r * 1103515245 + 12345) & 0xff));
  env.blobs = blobs;
  return env;
}
const setup = {
  map: 'maps/map mp evendim/map mp evendim.map',
  startingCash: 1500,
  slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }, { state: 'hard', name: 'Computer', faction: 2, color: 2, startPos: 2, team: 1 }],
};
async function call(lobby, method, path, { token, body, ip = '198.51.100.1' } = {}) {
  const url = new URL(path, 'http://lobby');
  const out = await route(lobby, {
    method, path: url.pathname, query: url.searchParams, ip,
    headers: token ? { authorization: `Bearer ${token}` } : {},
    bytes: async () => (body instanceof Uint8Array ? body : new TextEncoder().encode(body === undefined ? '' : JSON.stringify(body))),
  });
  return { status: out.status, body: out.headers['content-type']?.startsWith('application/json') ? JSON.parse(out.body) : out.body };
}
// ONLINE-5: route's raw answer (a held poll is { deferred })
async function rawCall(lobby, method, path, token) {
  const url = new URL(path, 'http://lobby');
  return route(lobby, { method, path: url.pathname, query: url.searchParams, ip: '198.51.100.1', headers: { authorization: `Bearer ${token}` }, bytes: async () => new Uint8Array(0) });
}

test('the ticket is the 32 bytes the relay checks (pinned with engine/tests/test_online_relay.cpp)', async () => {
  const env = nodeEnv({});
  const keys = parseKeyRing(ENV.TICKET_KEYS);
  const t = await signTicket(env, 1, keys.get(1), '0123456789abcdef', 1, 0b0111, 2000000000);
  assert.equal(t, 'AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI');
  assert.equal(base64UrlDecode(t).length, 32);
  assert.throws(() => base64UrlDecode('AQJ'));
  assert.throws(() => parseKeyRing('1:00'));
});

test('a game from sign-in to START: the profile check, ready flags, chat, one ticket per human slot', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  assert.equal(a.account.kind, 'guest');
  assert.equal((await call(lobby, 'POST', '/v1/games', { body: { name: 'x' } })).status, 401);
  const created = await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'Ann\'s game', profile: PROFILE, setup } });
  assert.equal(created.status, 200, JSON.stringify(created.body));
  const id = created.body.id;
  assert.match(id, /^[0-9a-f]{16}$/);
  const list = (await call(lobby, 'GET', '/v1/games')).body;
  assert.equal(list.games.length, 1);
  assert.equal(list.games[0].host, 'Ann');
  assert.equal(list.games[0].openSlots, 1);
  // another profile is refused before anything else
  const wrong = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: 'f'.repeat(32) } });
  assert.equal(wrong.status, 409);
  assert.equal(wrong.body.error, 'profile');
  const joined = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  assert.equal(joined.body.slot, 1);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/slot`, { token: b.token, body: { faction: 1, color: 1, startPos: 1, team: 1 } })).status, 200);
  // a player cannot change another player's slot; the host can arrange an AI
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/slot`, { token: b.token, body: { slot: 2, state: 'easy' } })).status, 403);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/slot`, { token: a.token, body: { slot: 2, state: 'brutal', name: 'Sauron' } })).status, 200);
  // not ready: no start; ready: start
  const early = await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token });
  assert.equal(early.body.error, 'not_ready');
  await call(lobby, 'POST', `/v1/games/${id}/chat`, { token: b.token, body: { text: 'gl hf' } });
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: b.token, body: { ready: true } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/start`, { token: b.token })).body.error, 'not_host');
  const started = await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token });
  assert.equal(started.status, 200, JSON.stringify(started.body));
  const sa = started.body.start;
  assert.equal(sa.kind, 'openbfme-start');
  assert.equal(sa.slot, 0);
  assert.equal(sa.relay, '192.0.2.10:7350');
  assert.equal(sa.profile, PROFILE);
  assert.equal(sa.setup.slots.length, 3);
  assert.equal(sa.setup.slots[2].state, 'brutal');
  assert.equal(sa.setup.slots[2].name, 'Sauron');
  assert.equal(sa.setup.slots[1].name, 'Bea');
  // the joiner's poll gets its own START: same setup and seed, its slot, its ticket
  const polled = (await call(lobby, 'GET', `/v1/games/${id}?since=0`, { token: b.token })).body;
  assert.equal(polled.start.slot, 1);
  assert.equal(polled.start.setup.seed, sa.setup.seed);
  assert.notEqual(polled.start.ticket, sa.ticket);
  const ticket = base64UrlDecode(polled.start.ticket);
  assert.equal(ticket[10], 1);
  assert.equal(ticket[11], 0b011);
  // the ticket's MAC with the relay's key
  const mac = createHmac('sha256', Buffer.from(KEY_HEX, 'hex')).update(ticket.subarray(0, 16)).digest().subarray(0, 16);
  assert.deepEqual(Buffer.from(ticket.subarray(16)), mac);
  assert.ok(polled.chat.some((c) => c.text === 'gl hf' && c.from === 'Bea'));
  // the started game left the list; joining it now is refused
  assert.equal((await call(lobby, 'GET', '/v1/games')).body.games.length, 0);
  const c = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Cy' } })).body;
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: c.token, body: { profile: PROFILE } })).body.error, 'started');
  // the replay's START record is kept for the converter, and (ONLINE-2 r2, Sol: player takeover) it is no credential: no ticket, slot or relay
  const replayStart = JSON.parse(new TextDecoder().decode(env.blobs.get(`starts/${id}.json`)));
  assert.equal(replayStart.kind, 'openbfme-replay-start');
  assert.equal(replayStart.ticket, undefined);
  assert.equal(replayStart.slot, undefined);
  assert.equal(replayStart.relay, undefined);
  assert.equal(replayStart.setup.seed, sa.setup.seed);
  assert.ok(!JSON.stringify(replayStart).includes(sa.ticket) && !JSON.stringify(replayStart).includes(polled.start.ticket));
});

test('tampered sessions, kicks, a host leaving, rate limits and bad fields are refused', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const [body, mac] = a.token.split('.');
  const forged = `${Buffer.from(JSON.stringify({ id: 'discord:1', name: 'Admin', kind: 'discord', exp: 2e9 })).toString('base64url')}.${mac}`;
  assert.equal((await call(lobby, 'POST', '/v1/games', { token: forged, body: {} })).status, 401);
  assert.ok(body.length > 0);
  assert.equal((await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'bad\u0007' } })).status, 400);
  assert.equal((await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: 'nothex', setup } })).status, 400);
  const id = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: PROFILE, setup } })).body.id;
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/kick`, { token: a.token, body: { slot: 1 } })).status, 200);
  assert.equal((await call(lobby, 'GET', `/v1/games/${id}`, { token: b.token })).status, 403);
  // the host leaving closes the game
  await call(lobby, 'POST', `/v1/games/${id}/leave`, { token: a.token });
  assert.equal((await call(lobby, 'GET', `/v1/games/${id}`, { token: a.token })).status, 404);
  // chat flood
  const id2 = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g2', profile: PROFILE, setup } })).body.id;
  let limited = 0;
  for (let i = 0; i < 20; i++) limited += (await call(lobby, 'POST', `/v1/games/${id2}/chat`, { token: a.token, body: { text: `spam ${i}` } })).status === 429 ? 1 : 0;
  assert.ok(limited > 0);
  // sign-in flood from one address
  let refused = 0;
  for (let i = 0; i < 40; i++) refused += (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: `n${i}` }, ip: '203.0.113.9' })).status === 429 ? 1 : 0;
  assert.ok(refused > 0);
  // an expired session
  env.advance(31 * 24 * 3600 * 1000);
  assert.equal((await call(lobby, 'GET', `/v1/games/${id2}`, { token: a.token })).status, 401);
  // ONLINE-2 r2: Discord sign-in is gone until its return link to the game exists (Sol r1); guest sign-in stays
  assert.equal((await call(lobby, 'GET', '/v1/auth/discord')).status, 404);
  assert.equal((await call(lobby, 'GET', '/v1/auth/discord/callback?code=x&state=y')).status, 404);
});

test('the self-hosted server: a game over HTTP, the relay log upload (signed) and the replay files', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-lobby-'));
  const { server, port } = await startServer({ ...ENV, LOBBY_DATA: dir, LOBBY_BIND: '127.0.0.1:0' }, { log: () => {} });
  try {
    const base = `http://127.0.0.1:${port}`;
    const post = async (path, body, token, headers = {}) => {
      const r = await fetch(base + path, { method: 'POST', headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}), ...headers }, body: body instanceof Uint8Array ? body : JSON.stringify(body ?? {}) });
      return { status: r.status, body: r.headers.get('content-type')?.includes('json') ? await r.json() : await r.text() };
    };
    const a = (await post('/v1/auth/guest', { name: 'Ann' })).body;
    const b = (await post('/v1/auth/guest', { name: 'Bea' })).body;
    const id = (await post('/v1/games', { name: 'http', profile: PROFILE, setup }, a.token)).body.id;
    await post(`/v1/games/${id}/join`, { profile: PROFILE }, b.token);
    await post(`/v1/games/${id}/ready`, { ready: true }, b.token);
    const started = await post(`/v1/games/${id}/start`, {}, a.token);
    assert.equal(started.status, 200);
    const log = new Uint8Array(25);
    log.set(Buffer.from('OBFMERLG'));
    new DataView(log.buffer).setUint32(8, 1, true);
    new DataView(log.buffer).setBigUint64(12, BigInt(`0x${id}`), true);
    const sig = createHmac('sha256', Buffer.from(ENV.UPLOAD_KEY, 'hex')).update(log).digest('hex');
    assert.equal((await post(`/v1/relay/logs/${id}`, log, null, { 'x-openbfme-signature': '00'.repeat(32) })).status, 401);
    assert.equal((await post(`/v1/relay/logs/${id}`, log, null, { 'x-openbfme-signature': sig })).status, 200);
    const asB = { headers: { authorization: `Bearer ${b.token}` } };
    assert.equal((await fetch(`${base}/v1/replays/${id}/log`)).status, 401);
    const back = new Uint8Array(await (await fetch(`${base}/v1/replays/${id}/log`, asB)).arrayBuffer());
    assert.deepEqual(back, log);
    const startRec = await (await fetch(`${base}/v1/replays/${id}/start`, asB)).json();
    assert.equal(startRec.game, id);
    assert.equal(startRec.ticket, undefined); // ONLINE-2 r2: Sol's takeover started from this record
    assert.ok(!JSON.stringify(startRec).includes(started.body.start.ticket));
    const invite = await (await fetch(`${base}/j/${id}`)).text();
    assert.ok(invite.includes(`openbfme://join/${id}`));
    assert.equal((await (await fetch(`${base}/v1/health`)).json()).ok, true);
  } finally {
    server.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the configuration refuses a lobby without keys or with a bad relay address (an empty RELAYS is allowed: CFDEPLOY-1)', () => {
  assert.throws(() => configFromEnv({ ...ENV, TICKET_KEYS: '' }));
  assert.ok(new Lobby(nodeEnv({}), configFromEnv({ ...ENV, RELAYS: '' })));
  assert.throws(() => new Lobby(nodeEnv({}), configFromEnv({ ...ENV, RELAYS: 'x=relay.example:7350' })));
  assert.ok(LobbyError);
});

test('ONLINE-2 r2 (Sol): a join while START signs its tickets is refused; every player of the game gets a START', async () => {
  const env = fakeEnv();
  // the signing yields to other work, as WebCrypto does in a Worker
  const hmac = env.hmac;
  env.hmac = async (k, d) => {
    await new Promise((r) => setTimeout(r, 5));
    return hmac(k, d);
  };
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const c = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Cy' } })).body;
  const twoOpen = { ...setup, slots: [setup.slots[0], { state: 'open' }, { state: 'open' }] };
  const id = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'race', profile: PROFILE, setup: twoOpen } })).body.id;
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: b.token, body: { ready: true } });
  // Sol's interleaving: START is signing when Cy joins
  const starting = call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token });
  await new Promise((r) => setTimeout(r, 1));
  const join = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: c.token, body: { profile: PROFILE } });
  assert.equal(join.status, 409);
  assert.equal((await starting).status, 200);
  const g = lobby.game(id);
  for (const [i, s] of g.slots.entries()) if (s.state === 'player') assert.ok(g.starts[i], `slot ${i} has a START`);
  assert.equal(g.slots.filter((s) => s.state === 'player').length, 2);
});

test('ONLINE-2 r2 (Sol): the Durable Object runs one request at a time, even when they await', async () => {
  const { LobbyObject } = await import('../src/worker.mjs');
  const order = [];
  const stored = new Map();
  const state = {
    storage: {
      put: async (k, v) => { order.push(`put ${v.seq}`); await new Promise((r) => setTimeout(r, 10)); stored.set(k, v); },
      delete: async (k) => stored.delete(k),
      list: async () => new Map(),
    },
    blockConcurrencyWhile: (fn) => fn(),
  };
  const obj = new LobbyObject(state, { ...ENV, LOGS: { put: async () => {}, get: async () => null } });
  const req = (path, body, token) => new Request(`http://lobby${path}`, { method: 'POST', headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) }, body: JSON.stringify(body) });
  const a = await (await obj.fetch(req('/v1/auth/guest', { name: 'Ann' }))).json();
  const [r1, r2] = await Promise.all([
    obj.fetch(req('/v1/games', { name: 'one', profile: PROFILE, setup }, a.token)).then((r) => { order.push('done 1'); return r; }),
    obj.fetch(req('/v1/games', { name: 'two', profile: PROFILE, setup }, a.token)).then((r) => { order.push('done 2'); return r; }),
  ]);
  assert.equal(r1.status, 200);
  assert.equal(r2.status, 400); // the second sees the first game: "you are in game ... already"
  assert.deepEqual(order, ['put 1', 'done 1', 'done 2']);
});

test('ONLINE-2 r2 (Sol): a chunked JSON body over 16 KiB is refused by its real length', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-lobby-'));
  const { server, port } = await startServer({ ...ENV, LOBBY_DATA: dir, LOBBY_BIND: '127.0.0.1:0' }, { log: () => {} });
  try {
    const { request } = await import('node:http');
    const body = JSON.stringify({ name: 'review', padding: 'x'.repeat(20000) });
    const status = await new Promise((resolve, reject) => {
      const r = request({ host: '127.0.0.1', port, path: '/v1/auth/guest', method: 'POST', headers: { 'content-type': 'application/json' } }, (res) => { res.resume(); resolve(res.statusCode); });
      r.on('error', reject);
      for (let i = 0; i < body.length; i += 1000) r.write(body.slice(i, i + 1000)); // chunked transfer: no Content-Length
      r.end();
    });
    assert.equal(status, 413);
    // the core refuses it too (the Worker path: the bytes, not the header)
    const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
    assert.equal((await call(lobby, 'POST', '/v1/auth/guest', { body: new TextEncoder().encode(body) })).status, 413);
  } finally {
    server.closeAllConnections();
    server.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('ONLINE-2 r3 (Sol r2): the Durable Object reads a body with a byte limit and a timeout before its queue: an endless body blocks nothing', async () => {
  const { LobbyObject } = await import('../src/worker.mjs');
  const state = { storage: { put: async () => {}, delete: async () => {}, list: async () => new Map() }, blockConcurrencyWhile: (fn) => fn() };
  const obj = new LobbyObject(state, { ...ENV, BODY_TIMEOUT_MS: '300', LOGS: { put: async () => {}, get: async () => null } });
  // a body stream that sends `n` bytes and never ends
  const endless = (n) => new ReadableStream({ start(c) { for (let i = 0; i < n; i += 1000) c.enqueue(new TextEncoder().encode('x'.repeat(Math.min(1000, n - i)))); } });
  const post = (path, body) => new Request(`http://lobby${path}`, { method: 'POST', headers: { 'content-type': 'application/json' }, body, duplex: 'half' });
  const t0 = Date.now();
  // Sol's attack: over 20 KB of a body that never ends; round 2 waited for its end inside the queue
  const big = obj.fetch(post('/v1/auth/guest', endless(21000)));
  // a slow one under the limit: answered 408 after the read timeout, never queued
  const slow = obj.fetch(post('/v1/auth/guest', endless(10)));
  // meanwhile the health check and a sign-in go through
  const health = await obj.fetch(new Request('http://lobby/v1/health'));
  assert.equal(health.status, 200);
  const guest = await obj.fetch(post('/v1/auth/guest', JSON.stringify({ name: 'Ann' })));
  assert.equal(guest.status, 200);
  assert.ok(Date.now() - t0 < 250, 'the health check and the sign-in did not wait for the stuck bodies');
  assert.equal((await big).status, 413);
  assert.equal((await slow).status, 408);
});

test('ONLINE-2 r4 (Sol r3): the self-hosted body deadline covers the body only, and in total', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-lobby-'));
  const { server, port, lobby } = await startServer({ ...ENV, LOBBY_DATA: dir, LOBBY_BIND: '127.0.0.1:0', BODY_TIMEOUT_MS: '100' }, { log: () => {} });
  try {
    const { request } = await import('node:http');
    const send = (body, dripMs) => new Promise((resolve, reject) => {
      const r = request({ host: '127.0.0.1', port, path: '/v1/auth/guest', method: 'POST', headers: { 'content-type': 'application/json' } }, (res) => {
        let text = '';
        res.on('data', (c) => { text += c; });
        res.on('end', () => resolve({ status: res.statusCode, text }));
      });
      r.on('error', (e) => resolve({ status: 'reset', text: String(e) }));
      if (!dripMs) {
        r.end(body);
        return;
      }
      let i = 0;
      const step = () => {
        if (i >= body.length) return r.end();
        r.write(body[i++]);
        setTimeout(step, dripMs);
      };
      step();
    });
    // Sol's first case: a complete request whose handling takes longer than the deadline (180 ms vs 100 ms here) is answered, not reset
    const guest = lobby.guest.bind(lobby);
    lobby.guest = async (...a) => { await new Promise((r) => setTimeout(r, 180)); return guest(...a); };
    const slowHandler = await send(JSON.stringify({ name: 'Ann' }), 0);
    assert.equal(slowHandler.status, 200, slowHandler.text);
    lobby.guest = guest;
    // Sol's second case: a body that keeps dripping (one byte every 20 ms: never idle for 100 ms) is cut at the total deadline
    const drip = await send(JSON.stringify({ name: 'Bea', pad: 'x'.repeat(30) }), 20);
    assert.ok(drip.status === 408 || drip.status === 'reset', JSON.stringify(drip));
  } finally {
    server.closeAllConnections();
    server.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

// ---- CFDEPLOY-1: the Workers Free plan deployment (no relay yet, the relay logs in the Durable Object's SQLite storage) ---------------------------------

// A Durable Object's storage for the tests: `sql.exec` and `transactionSync` run on a real SQLite (node:sqlite) as the Workers runtime's do (BLOBs go in
// as ArrayBuffers), the key-value part (games) in a Map
function doStorage() {
  const db = new sqlite.DatabaseSync(':memory:');
  const kv = new Map();
  const arg = (v) => (v instanceof ArrayBuffer ? new Uint8Array(v) : v);
  return {
    db,
    sql: {
      exec: (q, ...args) => {
        const st = db.prepare(q);
        const rows = /^\s*SELECT/i.test(q) ? st.all(...args.map(arg)) : (st.run(...args.map(arg)), []);
        return { toArray: () => rows };
      },
    },
    transactionSync: (fn) => {
      db.exec('BEGIN');
      try {
        const r = fn();
        db.exec('COMMIT');
        return r;
      } catch (e) {
        db.exec('ROLLBACK');
        throw e;
      }
    },
    put: async (k, v) => { kv.set(k, v); },
    delete: async (k) => kv.delete(k),
    list: async ({ prefix }) => new Map([...kv].filter(([k]) => k.startsWith(prefix))),
  };
}
const uploadSig = (bytes) => createHmac('sha256', Buffer.from(ENV.UPLOAD_KEY, 'hex')).update(bytes).digest('hex');
// a relay log (services/online/relay/RelayCore.h "Log format"): "OBFMERLG", u32 version 1, u64 game id (little-endian), u8 slot mask, u32 start, records
function relayLog(size, seed, game = '0123456789abcdef') {
  const b = new Uint8Array(size);
  for (let i = 0; i < size; i++) b[i] = (i * 31 + seed) & 0xff;
  b.set(new TextEncoder().encode('OBFMERLG'));
  const v = new DataView(b.buffer);
  v.setUint32(8, 1, true);
  v.setBigUint64(12, BigInt(`0x${game}`), true);
  return b;
}
async function doObject(env) {
  const { LobbyObject } = await import('../src/worker.mjs');
  const storage = doStorage();
  const obj = new LobbyObject({ storage, blockConcurrencyWhile: (fn) => fn() }, env);
  const upload = (id, bytes, sig = uploadSig(bytes)) => obj.fetch(new Request(`http://lobby/v1/relay/logs/${id}`, { method: 'POST', headers: { 'x-openbfme-signature': sig }, body: bytes }));
  const auth = (token) => (token ? { authorization: `Bearer ${token}` } : {});
  const get = (path, token) => obj.fetch(new Request(`http://lobby${path}`, { headers: auth(token) }));
  const post = async (path, body, token) => (await obj.fetch(new Request(`http://lobby${path}`, { method: 'POST', headers: { 'content-type': 'application/json', ...auth(token) }, body: JSON.stringify(body ?? {}) }))).json();
  // a game played to START through the object: Ann hosts, Bea joins
  const startGame = async () => {
    const a = await post('/v1/auth/guest', { name: 'Ann' });
    const b = await post('/v1/auth/guest', { name: 'Bea' });
    const id = (await post('/v1/games', { name: 'g', profile: PROFILE, setup }, a.token)).id;
    await post(`/v1/games/${id}/join`, { profile: PROFILE }, b.token);
    await post(`/v1/games/${id}/ready`, { ready: true }, b.token);
    assert.ok((await post(`/v1/games/${id}/start`, {}, a.token)).start, 'the game started');
    return { id, a, b };
  };
  return { obj, storage, upload, get, post, startGame };
}

test('CFDEPLOY-1: the relay logs are kept in the Durable Object\'s SQLite storage, in parts under the 2 MB row limit', { skip: NO_SQLITE }, async () => {
  const { storage, upload, get, startGame } = await doObject({ ...ENV, LOG_STORE: 'durable-object' });
  assert.equal((await (await get('/v1/health')).json()).logStore, 'durable-object');
  const { id, a } = await startGame();
  const log = relayLog(2.5 * 1024 * 1024, 7, id);
  assert.equal((await upload(id, log, '00'.repeat(32))).status, 401);
  assert.equal(storage.db.prepare('SELECT COUNT(*) AS n FROM blob_parts WHERE key LIKE \'logs/%\'').get().n, 0, 'a refused upload stores nothing');
  const up = await upload(id, log);
  assert.equal(up.status, 200, await up.clone().text());
  // the rows in SQLite: three parts of at most 1 MiB, and the total
  const parts = storage.db.prepare('SELECT part, length(bytes) AS n FROM blob_parts WHERE key = ? ORDER BY part').all(`logs/${id}.obfmerlog`);
  assert.deepEqual(parts.map((p) => [p.part, p.n]), [[0, 1048576], [1, 1048576], [2, 524288]]);
  assert.equal(storage.db.prepare('SELECT log_bytes FROM blob_totals').get().log_bytes, log.length);
  // the replay download (a player's) gives the same bytes back
  const back = await get(`/v1/replays/${id}/log`, a.token);
  assert.equal(back.status, 200);
  assert.deepEqual(new Uint8Array(await back.arrayBuffer()), log);
  // the uploader's retry of the same bytes is accepted and changes nothing
  const again = await upload(id, log);
  assert.equal(again.status, 200);
  assert.equal((await again.json()).unchanged, true);
  assert.equal(storage.db.prepare('SELECT log_bytes FROM blob_totals').get().log_bytes, log.length);
});

test('CFDEPLOY-1 r2 (Sol r1): a log is stored only under its own game, a started one, and never replaced by other bytes', { skip: NO_SQLITE }, async () => {
  const { storage, upload, get, startGame } = await doObject({ ...ENV, LOG_STORE: 'durable-object' });
  const g1 = await startGame();
  const g2 = await startGame();
  const log1 = relayLog(5000, 1, g1.id);
  assert.equal((await upload(g1.id, log1)).status, 200);
  // Sol's case: the same signed log sent under another game id
  const moved = await upload(g2.id, log1);
  assert.equal(moved.status, 409);
  assert.equal((await moved.json()).error, 'log_game_mismatch');
  assert.equal((await (await get(`/v1/replays/${g2.id}/log`, g2.a.token)).json()).error, 'no_replay');
  // a different (validly signed) log for game 1 does not replace the stored one
  const other = await upload(g1.id, relayLog(5000, 2, g1.id));
  assert.equal(other.status, 409);
  assert.equal((await other.json()).error, 'log_exists');
  assert.deepEqual(new Uint8Array(await (await get(`/v1/replays/${g1.id}/log`, g1.b.token)).arrayBuffer()), log1);
  // a game this lobby never started, a log of an unknown version, a header cut short
  const stranger = await upload('00000000000000aa', relayLog(500, 3, '00000000000000aa'));
  assert.equal(stranger.status, 404);
  assert.equal((await stranger.json()).error, 'unknown_game');
  const v3 = relayLog(500, 4, g2.id);
  new DataView(v3.buffer).setUint32(8, 3, true);
  assert.equal((await (await upload(g2.id, v3)).json()).error, 'bad_log');
  assert.equal((await (await upload(g2.id, relayLog(500, 5, g2.id).slice(0, 20))).json()).error, 'bad_log');
  assert.equal(storage.db.prepare('SELECT COUNT(*) AS n FROM blobs WHERE key LIKE \'logs/%\'').get().n, 1, 'only game 1\'s log is stored');
  // DEPLOY-2: ONLINE-3's relay writes version 2 (REJOINED records); the lobby stores it and its players download it
  const v2 = relayLog(500, 6, g2.id);
  new DataView(v2.buffer).setUint32(8, 2, true);
  assert.equal((await upload(g2.id, v2)).status, 200);
  assert.deepEqual(new Uint8Array(await (await get(`/v1/replays/${g2.id}/log`, g2.a.token)).arrayBuffer()), v2);
});

test('CFDEPLOY-1 r2 (Sol r1): only the players of a game download its replay files', { skip: NO_SQLITE }, async () => {
  const { upload, get, post, startGame } = await doObject({ ...ENV, LOG_STORE: 'durable-object' });
  const { id, a, b } = await startGame();
  assert.equal((await upload(id, relayLog(800, 1, id))).status, 200);
  const cy = await post('/v1/auth/guest', { name: 'Cy' });
  for (const which of ['log', 'start']) {
    const anon = await get(`/v1/replays/${id}/${which}`);
    assert.equal(anon.status, 401);
    assert.equal((await anon.json()).error, 'signed_out');
    const outsider = await get(`/v1/replays/${id}/${which}`, cy.token);
    assert.equal(outsider.status, 403);
    assert.equal((await outsider.json()).error, 'not_a_player');
    for (const player of [a, b]) assert.equal((await get(`/v1/replays/${id}/${which}`, player.token)).status, 200);
  }
});

test('CFDEPLOY-1: a log over the store\'s 8 MiB is refused (413 too_big) and a full store refuses with log_store_full; nothing is evicted', { skip: NO_SQLITE }, async () => {
  const { SqliteLogStore, SQL_MAX_LOG_BYTES } = await import('../src/worker.mjs');
  const { storage, upload } = await doObject({ ...ENV, LOG_STORE: 'durable-object' });
  const big = await upload('0123456789abcdef', relayLog(SQL_MAX_LOG_BYTES + 1, 1));
  assert.equal(big.status, 413);
  assert.equal((await big.json()).error, 'too_big');
  assert.equal(storage.db.prepare('SELECT COUNT(*) AS n FROM blobs').get().n, 0);
  // the store's total cap (small here): a replacement fits, a new log past the cap is refused, the stored logs stay as they were
  const s2 = doStorage();
  const store = new SqliteLogStore(s2, { partBytes: 1000, maxLogBytes: 4000, maxStoredLogBytes: 3000 });
  store.putBlob('logs/a.obfmerlog', relayLog(2000, 1));
  store.putBlob('logs/a.obfmerlog', relayLog(2500, 2));
  assert.throws(() => store.putBlob('logs/b.obfmerlog', relayLog(1000, 3)), (e) => e instanceof LobbyError && e.status === 507 && e.code === 'log_store_full');
  assert.equal(store.getBlob('logs/b.obfmerlog'), null);
  assert.deepEqual(store.getBlob('logs/a.obfmerlog'), relayLog(2500, 2));
  assert.equal(store.storedLogBytes(), 2500);
  // START records are not logs: never capped
  store.putBlob('starts/b.json', new Uint8Array(900));
  assert.equal(store.storedLogBytes(), 2500);
  // a part missing from the table is an error, not a short log
  s2.db.prepare('DELETE FROM blob_parts WHERE key = ? AND part = 1').run('logs/a.obfmerlog');
  assert.throws(() => store.getBlob('logs/a.obfmerlog'), /log store: logs\/a\.obfmerlog has a bad part 2/);
});

test('CFDEPLOY-1: without a log store uploads and replay reads fail with no_log_store; a bad LOG_STORE is refused', { skip: NO_SQLITE }, async () => {
  const { upload, get, post } = await doObject({ ...ENV });
  assert.equal((await (await get('/v1/health')).json()).logStore, 'none');
  const up = await upload('0123456789abcdef', relayLog(100, 1));
  assert.equal(up.status, 501);
  assert.equal((await up.json()).error, 'no_log_store');
  const ann = await post('/v1/auth/guest', { name: 'Ann' });
  assert.equal((await (await get('/v1/replays/0123456789abcdef/log', ann.token)).json()).error, 'no_log_store');
  await assert.rejects(doObject({ ...ENV, LOG_STORE: 'r2' }), /LOGS/);
  await assert.rejects(doObject({ ...ENV, LOG_STORE: 'kv' }), /unknown LOG_STORE/);
  const { LobbyObject } = await import('../src/worker.mjs');
  assert.throws(() => new LobbyObject({ storage: { list: async () => new Map() }, blockConcurrencyWhile: (fn) => fn() }, { ...ENV, LOG_STORE: 'durable-object' }), /SQLite/);
});

test('CFDEPLOY-1: with RELAYS empty the health check says "no relay configured" and START is refused with no_relay', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv({ ...ENV, RELAYS: '' }));
  const health = (await call(lobby, 'GET', '/v1/health')).body;
  assert.deepEqual(health.relays, []);
  assert.equal(health.relay, 'no relay configured');
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const id = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'no relay', profile: PROFILE, setup } })).body.id;
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: b.token, body: { ready: true } })).status, 200);
  const start = await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token });
  assert.equal(start.status, 503);
  assert.equal(start.body.error, 'no_relay');
  // the game is still open, with no START for anyone and no replay record
  const g = lobby.game(id);
  assert.equal(g.state, 'open');
  assert.equal(g.starts, undefined);
  assert.equal(env.blobs.size, 0);
  assert.equal((await call(lobby, 'GET', `/v1/games/${id}`, { token: b.token })).body.start, null);
  // with a relay configured the same lobby state starts (the refusal is the relay's absence, nothing else)
  lobby.config.relays = [{ id: 'lab', address: '192.0.2.10:7350' }];
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token })).status, 200);
});

test('CFDEPLOY-1 r2 (Sol r1): a body of 20,000 empty chunks is refused (too_many_chunks), and the read deadline holds while chunks keep coming', async () => {
  const { readBodyLimited, LobbyObject, MAX_BODY_CHUNKS } = await import('../src/worker.mjs');
  const post = (body) => new Request('http://lobby/v1/auth/guest', { method: 'POST', body, duplex: 'half' });
  // Sol's body: 20,000 empty chunks, then the end
  const empties = (n) => {
    let left = n;
    return new ReadableStream({ pull(c) { if (left-- > 0) c.enqueue(new Uint8Array(0)); else c.close(); } });
  };
  const obj = new LobbyObject({ storage: { put: async () => {}, delete: async () => {}, list: async () => new Map() }, blockConcurrencyWhile: (fn) => fn() }, { ...ENV });
  const r = await obj.fetch(post(empties(20000)));
  assert.equal(r.status, 413);
  assert.equal((await r.json()).error, 'too_many_chunks');
  assert.ok((await obj.fetch(post(empties(MAX_BODY_CHUNKS)))).status !== 413, 'a body of exactly the chunk limit is read');
  // chunks that arrive without ever yielding to the timer (each one already queued, 1 ms of work apiece): round 1 read them all and succeeded past
  // its 50 ms timeout; the deadline is checked per chunk now
  let sent = 0;
  const busy = new ReadableStream({
    pull(c) {
      const until = Date.now() + 1;
      while (Date.now() < until);
      if (sent++ < 1000) c.enqueue(new Uint8Array(0));
      else c.close();
    },
  });
  const t0 = Date.now();
  const slow = await readBodyLimited(post(busy), 1000, 50, Infinity);
  assert.equal(slow.status, 408);
  assert.equal(slow.code, 'timeout');
  assert.ok(Date.now() - t0 < 500, `the deadline stopped the read (${Date.now() - t0} ms)`);
});

test('CFDEPLOY-1 r2 (Sol r1): at most two log uploads and 64 bodies are received at once; more are refused with 503 overloaded', async () => {
  const { LobbyObject, MAX_LOG_UPLOADS, MAX_BODIES } = await import('../src/worker.mjs');
  const obj = new LobbyObject({ storage: { put: async () => {}, delete: async () => {}, list: async () => new Map() }, blockConcurrencyWhile: (fn) => fn() },
    { ...ENV, BODY_TIMEOUT_MS: '300' });
  const endless = () => new ReadableStream({ start(c) { c.enqueue(new Uint8Array(10)); } });
  const upload = (body) => obj.fetch(new Request('http://lobby/v1/relay/logs/0123456789abcdef', { method: 'POST', headers: { 'x-openbfme-signature': '00' }, body, duplex: 'half' }));
  const held = Array.from({ length: MAX_LOG_UPLOADS }, () => upload(endless()));
  const third = await upload(endless());
  assert.equal(third.status, 503);
  assert.equal((await third.json()).error, 'overloaded');
  assert.equal(third.headers.get('retry-after'), '5');
  // the lobby still answers meanwhile
  assert.equal((await obj.fetch(new Request('http://lobby/v1/health'))).status, 200);
  // the held uploads time out (408) and free their places: the next upload is read again (and refused for its content, not for load)
  for (const h of await Promise.all(held)) assert.equal(h.status, 408);
  const next = await upload(new Uint8Array(30));
  assert.notEqual(next.status, 503);
  // any kind of body: MAX_BODIES held, one more refused
  const guest = (body) => obj.fetch(new Request('http://lobby/v1/auth/guest', { method: 'POST', body, duplex: 'half' }));
  const many = Array.from({ length: MAX_BODIES }, () => guest(endless()));
  const over = await guest(endless());
  assert.equal(over.status, 503);
  assert.equal((await over.json()).error, 'overloaded');
  for (const h of await Promise.all(many)) assert.equal(h.status, 408);
});

test('CFDEPLOY-1 r3 (Sol r2): capacity is held while a body waits in the queue: six 8 MiB uploads behind a stalled request are not all accepted', async () => {
  const { LobbyObject, MAX_LOG_UPLOADS } = await import('../src/worker.mjs');
  const storage = { put: async () => {}, delete: async () => {}, list: async () => new Map() };
  const env = { ...ENV, BODY_TIMEOUT_MS: '2000' };
  const obj = new LobbyObject({ storage, blockConcurrencyWhile: (fn) => fn() }, env);
  // the first request stalls in the lobby (a signing that does not return until released), as Sol's did in HMAC signing
  let release;
  const stall = new Promise((r) => { release = r; });
  const hmac = obj.lobby.env.hmac;
  obj.lobby.env.hmac = async (k, d) => { await stall; return hmac(k, d); };
  const stalled = obj.fetch(new Request('http://lobby/v1/auth/guest', { method: 'POST', body: JSON.stringify({ name: 'Ann' }) }));
  await new Promise((r) => setTimeout(r, 10));
  // six complete 8 MiB uploads, each already fully readable: all of them read their bodies at once, queue behind the stalled request
  const eight = new Uint8Array(8 * 1024 * 1024);
  const uploads = Array.from({ length: 6 }, () => obj.fetch(new Request('http://lobby/v1/relay/logs/0123456789abcdef', { method: 'POST', headers: { 'x-openbfme-signature': '00' }, body: eight })));
  await new Promise((r) => setTimeout(r, 200));
  assert.ok(obj.reading.logs <= MAX_LOG_UPLOADS, `${obj.reading.logs} log uploads held`);
  assert.equal(obj.reading.logs, MAX_LOG_UPLOADS, 'the two accepted uploads are still counted while they wait in the queue');
  assert.equal(obj.reading.all, MAX_LOG_UPLOADS + 1, 'and the stalled request too');
  release();
  const statuses = (await Promise.all(uploads)).map((r) => r.status);
  assert.equal(statuses.filter((s) => s === 503).length, 6 - MAX_LOG_UPLOADS, `the others were refused at once: ${statuses}`);
  assert.equal((await stalled).status, 200);
  // everything was released afterwards
  assert.deepEqual(obj.reading, { logs: 0, all: 0 });
});

test('ONLINE-4: /v1/motd gives the message of the day (MOTD) or null; a bad MOTD is refused at configuration', async () => {
  const none = new Lobby(fakeEnv(), configFromEnv(ENV));
  const a = await call(none, 'GET', '/v1/motd');
  assert.equal(a.status, 200);
  assert.deepEqual(a.body, { v: 1, motd: null });
  const some = new Lobby(fakeEnv(), configFromEnv({ ...ENV, MOTD: 'Welcome to the test lobby\\nSecond line' }));
  const b = await call(some, 'GET', '/v1/motd');
  assert.deepEqual(b.body, { v: 1, motd: 'Welcome to the test lobby\nSecond line' });
  assert.throws(() => configFromEnv({ ...ENV, MOTD: 'x'.repeat(2001) }), /over 2000 bytes/);
  assert.throws(() => configFromEnv({ ...ENV, MOTD: 'bell\u0007' }), /control characters/);
});

test('ONLINE-4: a game created with RotWK\'s unset money (-1: the InitialResources rule) keeps it; below -1 is refused', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const made = await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: 'ab'.repeat(16), setup: { ...setup, startingCash: -1 } } });
  assert.equal(made.status, 200);
  assert.equal(made.body.game.setup.startingCash, -1);
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bob' } })).body;
  const bad = await call(lobby, 'POST', '/v1/games', { token: b.token, body: { name: 'g', profile: 'ab'.repeat(16), setup: { ...setup, startingCash: -2 } } });
  assert.equal(bad.status, 400);
});

test('ONLINE-3: a player whose game restarted lists its running game and gets a rejoin START: a version 2 ticket of the next generation', async () => {
  // the rejoin ticket's bytes are the relay's (pinned with engine/tests: RelayProtocol::signRejoinTicket signs the same 18 bytes)
  const keys = parseKeyRing(ENV.TICKET_KEYS);
  const pinnedText = await signRejoinTicket(nodeEnv({}), 1, keys.get(1), '0123456789abcdef', 1, 0b0111, 2000000000, 3);
  assert.equal(pinnedText, 'AgHvzauJZ0UjAQEHAJQ1dwMAb4T6YFP3_RWwFnRvvO1_ow');
  const pinned = base64UrlDecode(pinnedText);
  assert.equal(pinned.length, 34);
  assert.equal(pinned[0], 2);
  assert.equal(pinned[16] | (pinned[17] << 8), 3);
  const pinnedMac = createHmac('sha256', Buffer.from(KEY_HEX, 'hex')).update(pinned.subarray(0, 18)).digest().subarray(0, 16);
  assert.deepEqual(Buffer.from(pinned.subarray(18)), pinnedMac);
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const c = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Cy' } })).body;
  const id = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: PROFILE, setup } })).body.id;
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  // nothing to rejoin before the start
  assert.equal((await call(lobby, 'GET', '/v1/mine', { token: b.token })).body.games.length, 0);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/rejoin`, { token: b.token })).body.error, 'not_started');
  await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: b.token, body: { ready: true } });
  const first = (await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token })).body.start;
  const mine = (await call(lobby, 'GET', '/v1/mine', { token: b.token })).body;
  assert.equal(mine.games.length, 1);
  assert.equal(mine.games[0].id, id);
  assert.equal(mine.games[0].slot, 1);
  assert.equal(mine.games[0].rejoin, `/v1/games/${id}/rejoin`);
  assert.equal((await call(lobby, 'GET', '/v1/mine')).status, 401);
  // the rejoin START: the same game, slot and setup; a new ticket of generation 1, then 2
  const r1 = (await call(lobby, 'POST', `/v1/games/${id}/rejoin`, { token: b.token })).body.start;
  assert.equal(r1.kind, 'openbfme-start');
  assert.equal(r1.slot, 1);
  assert.equal(r1.rejoin, 1);
  assert.equal(r1.setup.seed, first.setup.seed);
  const t1 = base64UrlDecode(r1.ticket);
  assert.equal(t1.length, 34);
  assert.equal(t1[0], 2);
  assert.equal(t1[10], 1);
  assert.equal(t1[11], 0b011);
  assert.equal(t1[16] | (t1[17] << 8), 1);
  const mac = createHmac('sha256', Buffer.from(KEY_HEX, 'hex')).update(t1.subarray(0, 18)).digest().subarray(0, 16);
  assert.deepEqual(Buffer.from(t1.subarray(18)), mac);
  const r2 = (await call(lobby, 'POST', `/v1/games/${id}/rejoin`, { token: b.token })).body.start;
  assert.equal(r2.rejoin, 2);
  // only the slot's own account: another player of the server gets nothing
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/rejoin`, { token: c.token })).body.error, 'not_in_game');
  assert.equal((await call(lobby, 'GET', '/v1/mine', { token: c.token })).body.games.length, 0);
  // the first START the game's poll returns is unchanged (an old process keeps its own ticket; the relay refuses it once a rejoin bound)
  assert.equal((await call(lobby, 'GET', `/v1/games/${id}?since=0`, { token: b.token })).body.start.ticket, (await call(lobby, 'GET', `/v1/games/${id}?since=0`, { token: b.token })).body.start.ticket);
});

test('ONLINE-3: a spectator (an account that is not a player) gets a spectator START once the game started: a version 3 ticket with the host\'s delay', async () => {
  const keys = parseKeyRing(ENV.TICKET_KEYS);
  const pinnedText = await signSpectatorTicket(nodeEnv({}), 1, keys.get(1), '0123456789abcdef', 2, 0b0111, 2000000000, 60);
  assert.equal(pinnedText, 'AwHvzauJZ0UjAQIHAJQ1dzwADqM2vSmJqqkoEsrChDi_PA');
  const pinned = base64UrlDecode(pinnedText);
  assert.equal(pinned.length, 34);
  assert.equal(pinned[0], 3);
  assert.equal(pinned[10], 2);
  assert.equal(pinned[16] | (pinned[17] << 8), 60);
  const mac = createHmac('sha256', Buffer.from(KEY_HEX, 'hex')).update(pinned.subarray(0, 18)).digest().subarray(0, 16);
  assert.deepEqual(Buffer.from(pinned.subarray(18)), mac);
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const w = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Wat' } })).body;
  const id = (await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: PROFILE, setup } })).body.id;
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  // the host sets the delay; out of range is refused
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/setup`, { token: a.token, body: { spectatorDelay: 601 } })).status, 400);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/setup`, { token: a.token, body: { spectatorDelay: 60 } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: w.token })).body.error, 'not_started');
  await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: b.token, body: { ready: true } });
  const first = (await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token })).body.start;
  // a player is no spectator
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: b.token })).body.error, 'player');
  const s0 = (await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: w.token })).body.start;
  assert.equal(s0.kind, 'openbfme-spectate');
  assert.equal(s0.spectator, 0);
  assert.equal(s0.delay, 60);
  assert.equal(s0.slot, undefined);
  assert.equal(s0.setup.seed, first.setup.seed);
  const t0 = base64UrlDecode(s0.ticket);
  assert.equal(t0[0], 3);
  assert.equal(t0[10], 0);
  assert.equal(t0[11], 0b011);
  assert.equal(t0[16] | (t0[17] << 8), 60);
  // asking again (a spectator whose game restarted) gives the next index
  const s1 = (await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: w.token })).body.start;
  assert.equal(s1.spectator, 1);
});

test('ONLINE-4: the server browser\'s list: open games only, or with ?running=1 also the started ones (to watch), open ones first', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const sign = async (n) => (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: n } })).body.token;
  const [a, b, c] = [await sign('A'), await sign('B'), await sign('C')];
  const g1 = (await call(lobby, 'POST', '/v1/games', { token: a, body: { name: 'started one', profile: 'ab'.repeat(16), setup } })).body.id;
  await call(lobby, 'POST', `/v1/games/${g1}/join`, { token: b, body: { profile: 'ab'.repeat(16) } });
  await call(lobby, 'POST', `/v1/games/${g1}/ready`, { token: b, body: { ready: true } });
  assert.equal((await call(lobby, 'POST', `/v1/games/${g1}/start`, { token: a })).status, 200);
  env.advance(1000);
  const g2 = (await call(lobby, 'POST', '/v1/games', { token: c, body: { name: 'open one', profile: 'ab'.repeat(16), setup } })).body.id;
  assert.deepEqual((await call(lobby, 'GET', '/v1/games')).body.games.map((g) => g.id), [g2]);
  const all = (await call(lobby, 'GET', '/v1/games?running=1')).body.games;
  assert.deepEqual(all.map((g) => [g.id, g.state]), [[g2, 'open'], [g1, 'started']]);
});

// INFRA-2: the admin backup on the Durable Object (services/online/deploy/lobby_backup.py is its client; tools/net/test_lobby_backup.py runs it
// against the self-hosted server)
const ADMIN_KEY = 'cc'.repeat(32);
function adminHeaders(method, path, body = new Uint8Array(0), key = ADMIN_KEY, at = Date.now()) {
  const url = new URL(path, 'http://lobby');
  const ts = String(Math.floor(at / 1000));
  const mac = createHmac('sha256', Buffer.from(key, 'hex')).update(`${method} ${url.pathname}?${url.searchParams}\n${ts}\n`).update(body).digest('hex');
  return { 'x-openbfme-admin': `${ts}.${mac}` };
}

test('INFRA-2: the Durable Object lists, exports and imports its blobs for a signed admin only; an import restores a player\'s replay', { skip: NO_SQLITE }, async () => {
  const env = { ...ENV, LOG_STORE: 'durable-object', ADMIN_KEY };
  const src = await doObject(env);
  const { id, a } = await src.startGame();
  const log = relayLog(2.5 * 1024 * 1024, 9, id); // over 16 KiB and over one 1 MiB part: the import takes the log's byte limit, not the JSON one
  assert.equal((await src.upload(id, log)).status, 200);
  const adminGet = (obj, path, key) => obj.fetch(new Request(`http://lobby${path}`, { headers: adminHeaders('GET', path, undefined, key) }));
  // unsigned, wrongly signed (the upload key) and stale requests are refused
  assert.equal((await src.get('/v1/admin/blobs')).status, 401);
  assert.equal((await (await adminGet(src.obj, '/v1/admin/blobs', ENV.UPLOAD_KEY)).json()).error, 'admin_signature');
  const stale = await src.obj.fetch(new Request('http://lobby/v1/admin/blobs', { headers: adminHeaders('GET', '/v1/admin/blobs', undefined, ADMIN_KEY, Date.now() - 400_000) }));
  assert.equal((await stale.json()).error, 'admin_clock');
  // the listing, one page of two and then the rest
  const page1 = await (await adminGet(src.obj, '/v1/admin/blobs?after=&limit=2')).json();
  assert.deepEqual(page1.blobs.map((b) => [b.key, b.size]), [[`logs/${id}.obfmerlog`, log.length], [`players/${id}.json`, page1.blobs[1].size]]);
  assert.equal(page1.next, `players/${id}.json`);
  const page2 = await (await adminGet(src.obj, `/v1/admin/blobs?${new URLSearchParams({ after: page1.next, limit: 2 })}`)).json();
  assert.deepEqual(page2.blobs.map((b) => b.key), [`starts/${id}.json`]);
  assert.equal(page2.next, null);
  // export every blob, import them into an empty object (a lost lobby redeployed), and the player reads the same replay there
  const dst = await doObject(env);
  for (const b of [...page1.blobs, ...page2.blobs]) {
    const bytes = new Uint8Array(await (await adminGet(src.obj, `/v1/admin/blobs/${b.key}`)).arrayBuffer());
    assert.equal(bytes.length, b.size);
    const path = `/v1/admin/blobs/${b.key}`;
    const r = await dst.obj.fetch(new Request(`http://lobby${path}`, { method: 'POST', headers: adminHeaders('POST', path, bytes), body: bytes }));
    assert.equal(r.status, 200, await r.clone().text());
  }
  assert.equal(dst.storage.db.prepare('SELECT log_bytes FROM blob_totals').get().log_bytes, log.length, 'the imported log counts against the store\'s cap');
  assert.deepEqual(new Uint8Array(await (await dst.get(`/v1/replays/${id}/log`, a.token)).arrayBuffer()), log);
  // an import never replaces a stored blob with other bytes
  const other = relayLog(1000, 1, id);
  const path = `/v1/admin/blobs/logs/${id}.obfmerlog`;
  const r = await dst.obj.fetch(new Request(`http://lobby${path}`, { method: 'POST', headers: adminHeaders('POST', path, other), body: other }));
  assert.equal(r.status, 409);
  assert.equal((await r.json()).error, 'blob_exists');
});

test('INFRA-2 r2: the relay uploader\'s status is stored in the Durable Object, survives its restart and is read by a signed admin only', { skip: NO_SQLITE }, async () => {
  const env = { ...ENV, LOG_STORE: 'durable-object', ADMIN_KEY };
  const src = await doObject(env);
  const body = new TextEncoder().encode(JSON.stringify({ v: 1, relay: '203.0.113.10:7350', at: Math.floor(Date.now() / 1000), pending: 1, partial: 0,
    oldestPendingS: 30, uploaded: 4, failed: 0, logErrors: 0 }));
  const post = (obj, bytes, sig = uploadSig(bytes)) => obj.fetch(new Request('http://lobby/v1/relay/status', { method: 'POST', headers: { 'x-openbfme-signature': sig }, body: bytes }));
  assert.equal((await post(src.obj, body, '00'.repeat(32))).status, 401);
  const r = await post(src.obj, body);
  assert.equal(r.status, 200, await r.clone().text());
  // a new object on the same storage (a redeploy restarts it) still has it; the blob listing (the backup) does not include it
  const { LobbyObject } = await import('../src/worker.mjs');
  const again = new LobbyObject({ storage: src.storage, blockConcurrencyWhile: (fn) => fn() }, env);
  await new Promise((res) => setTimeout(res, 0));
  const read = (path) => again.fetch(new Request(`http://lobby${path}`, { headers: adminHeaders('GET', path) }));
  const doc = await (await read('/v1/admin/relay-status')).json();
  assert.deepEqual(doc.relays.map((x) => [x.relay, x.pending, x.uploaded, x.logErrors]), [['203.0.113.10:7350', 1, 4, 0]]);
  assert.deepEqual((await (await read('/v1/admin/blobs')).json()).blobs, []);
  assert.equal((await again.fetch(new Request('http://lobby/v1/admin/relay-status'))).status, 401);
  // an old status is refused (it could hide a fault for longer than the window)
  const old = new TextEncoder().encode(JSON.stringify({ ...JSON.parse(new TextDecoder().decode(body)), at: 1000 }));
  assert.equal((await (await post(src.obj, old)).json()).error, 'status_clock');
});

// ---- lane ONLINE-5: the staging room as three players met it -----------------------------------------------------------------------------------------------
// a lobby with a fake clock whose timers fire when the clock passes them, and a game of Ann (host, slot 0), Bea (1) and Cy (2) with an AI in slot 3
async function room5() {
  const env = fakeEnv();
  const timers = new Set();
  env.setTimer = (fn, ms) => { const t = { fn, at: env.now() + ms }; timers.add(t); return t; };
  env.clearTimer = (t) => timers.delete(t);
  const advance = env.advance;
  env.advance = (ms) => {
    advance(ms);
    for (const t of [...timers]) if (t.at <= env.now() && timers.delete(t)) t.fn();
  };
  const lobby = new Lobby(env, configFromEnv(ENV));
  const guest = async (name) => (await call(lobby, 'POST', '/v1/auth/guest', { body: { name } })).body;
  const [a, b, c] = [await guest('Ann'), await guest('Bea'), await guest('Cy')];
  const slots = [{ state: 'player', faction: 0, color: 0, startPos: 0, team: -1 }, { state: 'open' }, { state: 'open' }, { state: 'hard', name: 'Computer', faction: 2, color: 3, startPos: 3, team: -1 }];
  const created = await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'three', profile: PROFILE, setup: { ...setup, slots } } });
  assert.equal(created.status, 200, JSON.stringify(created.body));
  const id = created.body.id;
  for (const p of [b, c]) assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: p.token, body: { profile: PROFILE } })).status, 200);
  const post = (who, what, body) => call(lobby, 'POST', `/v1/games/${id}/${what}`, { token: who.token, body });
  // the room as stored, without the clock's touch
  const snapshot = () => JSON.stringify({ ...lobby.games.get(id), touched: 0 });
  return { env, lobby, timers, a, b, c, id, post, snapshot };
}

test('ONLINE-5: a ready, spot, colour or slot claim for another player is refused by name and the room is unchanged', async () => {
  const { lobby, a, b, c, id, post, snapshot } = await room5();
  assert.equal((await post(b, 'slot', { faction: 1, color: 1, startPos: 1, team: 0 })).status, 200);
  assert.equal((await post(c, 'slot', { faction: 2, color: 2, startPos: 2, team: 1 })).status, 200);
  assert.equal((await post(b, 'ready', { ready: true })).status, 200);
  const before = snapshot();
  const refused = async (who, what, body, status, code, message) => {
    const r = await post(who, what, body);
    assert.equal(r.status, status, `${what} ${JSON.stringify(body)}: ${JSON.stringify(r.body)}`);
    assert.equal(r.body.error, code);
    if (message) assert.equal(r.body.message, message);
    assert.equal(snapshot(), before, `${what} ${JSON.stringify(body)} changed the room`);
  };
  // ready for someone else's slot (it must not ready or unready the sender either)
  await refused(c, 'ready', { ready: true, slot: 1 }, 403, 'not_your_slot', 'a player readies their own slot only');
  await refused(b, 'ready', { ready: false, slot: 2 }, 403, 'not_your_slot');
  await refused(a, 'ready', { ready: true, slot: 1 }, 403, 'not_your_slot');
  await refused(c, 'ready', { ready: 'yes' }, 400, 'bad_field');
  // a start position or a colour another player or an AI holds
  await refused(c, 'slot', { startPos: 1 }, 409, 'spot_taken', 'start position 2 is Bea\'s');
  await refused(c, 'slot', { startPos: 0 }, 409, 'spot_taken', 'start position 1 is Ann\'s');
  await refused(c, 'slot', { startPos: 3 }, 409, 'spot_taken', 'start position 4 is Computer\'s');
  await refused(c, 'slot', { color: 1 }, 409, 'color_taken', 'that colour is Bea\'s');
  await refused(a, 'slot', { slot: 3, color: 0, startPos: 2 }, 409, 'spot_taken'); // the host's AI on a player's spot
  await refused(a, 'slot', { slot: 3, color: 2 }, 409, 'color_taken');
  // another player's slot, by a player and by the host; a player cannot make its own slot something else
  await refused(c, 'slot', { slot: 1, faction: 3 }, 403, 'not_host');
  await refused(c, 'slot', { slot: 3, state: 'open' }, 403, 'not_host');
  await refused(a, 'slot', { slot: 1, faction: 3 }, 403, 'player_slot');
  await refused(c, 'slot', { state: 'closed' }, 400, 'bad_field');
  await refused(c, 'slot', { name: 'Ann' }, 400, 'bad_field');
  // one bad field refuses the whole change: the good faction before it is not kept (the old code kept it)
  await refused(c, 'slot', { faction: 5, color: 64 }, 400, 'bad_field');
  await refused(c, 'slot', { faction: 5, startPos: 8 }, 400, 'bad_field');
  await refused(a, 'slot', { slot: 3, state: 'easy', name: 'Bot', team: 9 }, 400, 'bad_field');
  await refused(a, 'slot', { slot: 8 }, 400, 'bad_field');
  await refused(c, 'kick', { slot: 1 }, 403, 'not_host');
  await refused(c, 'setup', { map: 'maps/x/x.map' }, 403, 'not_host');
  await refused(c, 'start', undefined, 403, 'not_host');
  // the same ready again changes nothing (no new seq: nobody's poll is woken for it)
  assert.equal((await post(b, 'ready', { ready: true })).status, 200);
  assert.equal(snapshot(), before);
  // the legal twins of the refused claims work: random is never taken, a free spot is
  assert.equal((await post(c, 'slot', { startPos: -1, color: -1 })).status, 200);
  assert.equal((await post(c, 'slot', { startPos: 4, color: 5 })).status, 200);
  assert.deepEqual(lobby.games.get(id).slots.slice(0, 4).map((s) => [s.startPos, s.color]), [[0, 0], [1, 1], [4, 5], [3, 3]]);
  // an AI slot closed frees its spot and colour; a new game cannot begin with two slots on one spot
  assert.equal((await post(a, 'slot', { slot: 3, state: 'closed' })).status, 200);
  assert.equal((await post(c, 'slot', { startPos: 3, color: 3 })).status, 200);
  const d = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Dee' } })).body;
  const twice = await call(lobby, 'POST', '/v1/games', { token: d.token, body: { name: 'dup', profile: PROFILE, setup: { ...setup, slots: [{ state: 'player', startPos: 1 }, { state: 'easy', startPos: 1 }] } } });
  assert.equal(twice.body.error, 'spot_taken');
  assert.equal(lobby.games.size, 1);
});

test('ONLINE-5: a change of the game unreadies everyone, a new map frees every start position, and a started game takes no ready', async () => {
  const { lobby, a, b, c, id, post } = await room5();
  await post(b, 'slot', { startPos: 1 });
  await post(c, 'slot', { startPos: 2 });
  const ready = () => lobby.games.get(id).slots.slice(0, 3).map((s) => s.ready);
  const both = async () => {
    for (const p of [b, c]) assert.equal((await post(p, 'ready', { ready: true })).status, 200);
    assert.deepEqual(ready(), [false, true, true]);
  };
  await both();
  assert.equal((await post(a, 'setup', { startingCash: 4000 })).status, 200);
  assert.deepEqual(ready(), [false, false, false]);
  assert.deepEqual(lobby.games.get(id).slots.map((s) => s.startPos), [0, 1, 2, 3, -1, -1, -1, -1]); // the same map: the spots stay
  await both();
  assert.equal((await post(a, 'slot', { faction: 4 })).status, 200); // the host's own army
  assert.deepEqual(ready(), [false, false, false]);
  await both();
  assert.equal((await post(a, 'setup', { map: 'maps/map mp other/map mp other.map' })).status, 200);
  assert.deepEqual(ready(), [false, false, false]);
  assert.deepEqual(lobby.games.get(id).slots.map((s) => s.startPos), [-1, -1, -1, -1, -1, -1, -1, -1]);
  // the start names who is not ready
  await post(b, 'ready', { ready: true });
  const early = await post(a, 'start');
  assert.equal(early.body.error, 'not_ready');
  assert.equal(early.body.message, 'not ready: Cy');
  await post(c, 'ready', { ready: true });
  assert.equal((await post(a, 'start')).status, 200);
  const late = await post(b, 'ready', { ready: false });
  assert.equal(late.status, 409);
  assert.equal(late.body.error, 'started');
  assert.deepEqual(ready(), [false, true, true]);
});

test('ONLINE-5: a poll that asks to wait is held until the game changes, its own chat lines are marked, and held polls are bounded', async () => {
  const { env, lobby, a, b, c, id, post } = await room5();
  const poll = (who, q) => rawCall(lobby, 'GET', `/v1/games/${id}${q}`, who.token);
  const seq = () => lobby.games.get(id).seq;
  // a poll without wait is answered at once, as every client before ONLINE-5 expects; it says the lobby can hold polls
  const plain = await poll(b, '?since=0');
  assert.equal(plain.deferred, undefined);
  assert.equal(JSON.parse(plain.body).wait, MAX_WAIT_S);
  assert.equal(JSON.parse(plain.body).game.seq, seq());
  // an old view (another seq) is answered at once too
  assert.equal((await poll(b, `?since=0&seq=${seq() - 1}&wait=20`)).deferred, undefined);
  // the current view: held; nothing comes until the game changes
  const held = await poll(b, `?since=${seq()}&seq=${seq()}&wait=20`);
  assert.ok(held.deferred);
  let answer = null;
  held.deferred.then((r) => { answer = r; });
  await new Promise((r) => setImmediate(r));
  assert.equal(answer, null);
  assert.equal(lobby.waiterCount, 1);
  env.advance(19000);
  await new Promise((r) => setImmediate(r));
  assert.equal(answer, null);
  const sinceBefore = seq();
  assert.equal((await post(c, 'chat', { text: 'hello' })).status, 200);
  await new Promise((r) => setImmediate(r));
  assert.ok(answer, 'the chat line woke the held poll');
  assert.equal(lobby.waiterCount, 0);
  assert.equal(lobby.waiters.size, 0);
  const woken = JSON.parse(answer.body);
  assert.deepEqual(woken.chat, [{ seq: sinceBefore + 1, from: 'Cy', text: 'hello' }]); // Bea reads Cy's line: not marked
  // the writer's own poll marks it
  const own = JSON.parse((await poll(c, `?since=${sinceBefore}`)).body);
  assert.deepEqual(own.chat, [{ seq: sinceBefore + 1, from: 'Cy', text: 'hello', you: true }]);
  // a held poll with nothing happening is answered when its time is up, with the unchanged view; the wait is capped at MAX_WAIT_S
  const idle = await poll(a, `?since=${seq()}&seq=${seq()}&wait=999`);
  let idleAnswer = null;
  idle.deferred.then((r) => { idleAnswer = r; });
  env.advance((MAX_WAIT_S - 1) * 1000);
  await new Promise((r) => setImmediate(r));
  assert.equal(idleAnswer, null);
  env.advance(1000);
  await new Promise((r) => setImmediate(r));
  assert.equal(JSON.parse(idleAnswer.body).game.seq, seq());
  assert.deepEqual(JSON.parse(idleAnswer.body).chat, []);
  assert.equal(lobby.waiterCount, 0);
  // anything that is not a whole number of seconds from 1, or not a view seq, is not held (answered at once, never an error or a timer)
  for (const q of ['&wait=0', '&wait=-5', '&wait=1e3', '&wait=abc', '&wait=20.5', '&wait=', '&wait=1000000']) {
    const r = await poll(b, `?since=0&seq=${seq()}${q}`);
    assert.equal(r.deferred, undefined, q);
    assert.equal(r.status, 200, q);
  }
  for (const q of [`?since=0&wait=20`, `?since=0&seq=abc&wait=20`, `?since=0&seq=-1&wait=20`, `?since=0&seq=99999999999&wait=20`]) assert.equal((await poll(b, q)).deferred, undefined, q);
  await new Promise((r) => setImmediate(r));
  assert.equal(lobby.waiterCount, 0);
  // someone who is not in the game is refused at once, never held
  const d = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Dee' } })).body;
  const stranger = await poll(d, `?since=0&seq=${seq()}&wait=20`);
  assert.equal(stranger.status, 403);
  // Sol r1: each player has MAX_HOLDS_PER_ACCOUNT holds at once: a third poll of Bea's is answered at once with the back-off, and takes nothing from Cy
  const many = [];
  for (let i = 0; i < 2 * MAX_HOLDS_PER_ACCOUNT; i++) many.push(await poll(i % 2 ? b : c, `?since=${seq()}&seq=${seq()}&wait=20`));
  assert.ok(many.every((r) => r.deferred));
  assert.equal(lobby.waiterCount, 2 * MAX_HOLDS_PER_ACCOUNT);
  const over = await poll(b, `?since=${seq()}&seq=${seq()}&wait=20`);
  assert.equal(over.deferred, undefined);
  assert.equal(over.status, 200);
  assert.equal(JSON.parse(over.body).backoff, HOLD_BACKOFF_S);
  assert.equal(lobby.waiterCount, 2 * MAX_HOLDS_PER_ACCOUNT);
  // a poll that is held, or answered at once for an old view, carries no back-off
  assert.equal(JSON.parse((await poll(b, `?since=0&seq=${seq() - 1}&wait=20`)).body).backoff, undefined);
  // a kick wakes them: the kicked player's held polls say so, the others' carry the new view; every hold is released (none left for anyone)
  assert.equal((await post(a, 'kick', { slot: 2 })).status, 200);
  const done = await Promise.all(many.map((r) => r.deferred));
  assert.equal(lobby.waiterCount, 0);
  assert.equal(lobby.waiters.size, 0);
  assert.equal(lobby.accountHolds.size, 0);
  done.forEach((r, i) => {
    assert.equal(r.status, i % 2 ? 200 : 403, JSON.stringify(r.body));
    if (!(i % 2)) assert.equal(JSON.parse(r.body).error, 'not_in_game');
    else assert.equal(JSON.parse(r.body).backoff, undefined);
  });
  // the host leaves: a held poll says the game is gone
  const last = await poll(b, `?since=${seq()}&seq=${seq()}&wait=20`);
  assert.ok(last.deferred);
  assert.equal((await post(a, 'leave')).status, 200);
  assert.equal(JSON.parse((await last.deferred).body).error, 'no_game');
  assert.equal(lobby.waiterCount, 0);
  // a started game's polls are not held (the START is in them; the client stops polling)
});

test('ONLINE-5: through the HTTP server a held poll does not block other requests, and a chat line reaches a waiting player in milliseconds', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-lobby5-'));
  const { server, port } = await startServer({ ...ENV, LOBBY_BIND: '127.0.0.1:0', LOBBY_DATA: dir }, { log: () => {} });
  try {
    const base = `http://127.0.0.1:${port}`;
    const req = async (method, path, token, body) => {
      const r = await fetch(base + path, { method, headers: token ? { authorization: `Bearer ${token}` } : {}, body: body === undefined ? undefined : JSON.stringify(body) });
      return { status: r.status, body: await r.json() };
    };
    const a = (await req('POST', '/v1/auth/guest', null, { name: 'Ann' })).body;
    const b = (await req('POST', '/v1/auth/guest', null, { name: 'Bea' })).body;
    const id = (await req('POST', '/v1/games', a.token, { name: 'g', profile: PROFILE, setup })).body.id;
    const joined = (await req('POST', `/v1/games/${id}/join`, b.token, { profile: PROFILE })).body;
    const seq = joined.game.seq;
    const t0 = Date.now();
    let heldDone = 0;
    const held = req('GET', `/v1/games/${id}?since=${seq}&seq=${seq}&wait=10`, b.token).then((r) => { heldDone = Date.now(); return r; });
    await new Promise((r) => setTimeout(r, 150));
    assert.equal(heldDone, 0, 'the poll is held');
    // while it is held: the list, the health check and the host's own plain poll are answered
    assert.equal((await req('GET', '/v1/health')).status, 200);
    assert.equal((await req('GET', '/v1/games')).body.games.length, 1);
    assert.equal((await req('GET', `/v1/games/${id}?since=0`, a.token)).status, 200);
    const sent = Date.now();
    assert.equal((await req('POST', `/v1/games/${id}/chat`, a.token, { text: 'now' })).status, 200);
    const r = await held;
    assert.deepEqual(r.body.chat.map((c) => c.text), ['now']);
    const delay = heldDone - sent;
    console.log(`ONLINE-5 chat delay through a held poll (local HTTP): ${delay} ms after the send; the poll was held ${sent - t0} ms`);
    assert.ok(delay < 500, `the held poll answered ${delay} ms after the chat line`); // tolerance: local HTTP round trips take a few ms; the old 1 s poll averaged 500 ms
  } finally {
    server.closeAllConnections?.();
    await new Promise((r) => server.close(r));
    rmSync(dir, { recursive: true, force: true });
  }
});

test('ONLINE-5: the Durable Object answers other requests while polls are held, and held polls take no body place', async () => {
  const { LobbyObject, MAX_BODIES } = await import('../src/worker.mjs');
  const obj = new LobbyObject({ storage: { put: async () => {}, delete: async () => {}, list: async () => new Map() }, blockConcurrencyWhile: (fn) => fn() }, { ...ENV });
  const req = async (method, path, token, body) => {
    const r = await obj.fetch(new Request(`http://lobby${path}`, { method, headers: token ? { authorization: `Bearer ${token}` } : {}, body: body === undefined ? undefined : JSON.stringify(body) }));
    return { status: r.status, body: await r.json() };
  };
  const a = (await req('POST', '/v1/auth/guest', null, { name: 'Ann' })).body;
  const b = (await req('POST', '/v1/auth/guest', null, { name: 'Bea' })).body;
  const id = (await req('POST', '/v1/games', a.token, { name: 'g', profile: PROFILE, setup })).body.id;
  const seq = (await req('POST', `/v1/games/${id}/join`, b.token, { profile: PROFILE })).body.game.seq;
  // each of the two players has MAX_HOLDS_PER_ACCOUNT polls held; their other polls are answered at once with the back-off (Sol r1)
  const held = [];
  for (let i = 0; i < 2 * MAX_HOLDS_PER_ACCOUNT; i++) held.push(req('GET', `/v1/games/${id}?since=${seq}&seq=${seq}&wait=20`, i % 2 ? a.token : b.token));
  await new Promise((r) => setTimeout(r, 50));
  assert.equal(obj.lobby.waiterCount, 2 * MAX_HOLDS_PER_ACCOUNT);
  const extra = await Promise.all(Array.from({ length: 12 }, (_, i) => req('GET', `/v1/games/${id}?since=${seq}&seq=${seq}&wait=20`, i % 2 ? a.token : b.token)));
  assert.ok(extra.every((r) => r.status === 200 && r.body.backoff === HOLD_BACKOFF_S && r.body.chat.length === 0));
  assert.equal(obj.lobby.waiterCount, 2 * MAX_HOLDS_PER_ACCOUNT);
  assert.deepEqual(obj.reading, { logs: 0, all: 0 }); // no place is held while they wait
  assert.ok(MAX_BODIES >= 16);
  assert.equal((await req('GET', '/v1/health')).status, 200);
  assert.equal((await req('POST', `/v1/games/${id}/chat`, b.token, { text: 'hi' })).status, 200);
  const answers = await Promise.all(held);
  assert.ok(answers.every((r) => r.status === 200 && r.body.chat.length === 1 && r.body.chat[0].text === 'hi'));
  assert.deepEqual(answers.map((r) => !!r.body.chat[0].you), Array.from({ length: 2 * MAX_HOLDS_PER_ACCOUNT }, (_, i) => !(i % 2)));
  assert.equal(obj.lobby.waiterCount, 0);
  assert.equal(obj.lobby.accountHolds.size, 0);
});

test('ONLINE-5 (Sol r1): held polls are shared fairly across rooms: a per-player allowance, the lobby-wide bound with the back-off, and every hold released', async () => {
  const env = fakeEnv();
  const timers = new Set();
  env.setTimer = (fn, ms) => { const t = { fn, at: env.now() + ms }; timers.add(t); return t; };
  env.clearTimer = (t) => timers.delete(t);
  const advance = (ms) => {
    env.advance(ms);
    for (const t of [...timers]) if (t.at <= env.now() && timers.delete(t)) t.fn();
  };
  const lobby = new Lobby(env, { ...configFromEnv(ENV), maxHolds: 5 });
  const guest = async (name) => (await call(lobby, 'POST', '/v1/auth/guest', { body: { name } })).body;
  const [ann, bea, cy, dee] = [await guest('Ann'), await guest('Bea'), await guest('Cy'), await guest('Dee')];
  const room = async (host, joiner) => {
    const id = (await call(lobby, 'POST', '/v1/games', { token: host.token, body: { name: 'r', profile: PROFILE, setup } })).body.id;
    assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: joiner.token, body: { profile: PROFILE } })).status, 200);
    return id;
  };
  const x = await room(ann, bea);
  const y = await room(cy, dee);
  const seqOf = (id) => lobby.games.get(id).seq;
  const poll = (who, id) => rawCall(lobby, 'GET', `/v1/games/${id}?since=${seqOf(id)}&seq=${seqOf(id)}&wait=20`, who.token);
  const backoff = (r) => (r.deferred ? null : JSON.parse(r.body).backoff);
  // room X: Ann 2 (her allowance; a third is told to back off), Bea 2
  const xs = [await poll(ann, x), await poll(ann, x), await poll(bea, x), await poll(bea, x)];
  assert.ok(xs.every((r) => r.deferred));
  assert.equal(backoff(await poll(ann, x)), HOLD_BACKOFF_S);
  // room Y: Cy gets the fifth hold, the lobby-wide bound; Dee, with none of her own, is told to back off
  const cyHeld = await poll(cy, y);
  assert.ok(cyHeld.deferred);
  assert.equal(lobby.waiterCount, 5);
  assert.equal(backoff(await poll(dee, y)), HOLD_BACKOFF_S);
  assert.deepEqual([...lobby.accountHolds.entries()].map(([, n]) => n).sort(), [1, 2, 2]);
  // room X changes: its four holds end; Dee is held now (another room's bookkeeping freed the lobby's places)
  assert.equal((await call(lobby, 'POST', `/v1/games/${x}/chat`, { token: bea.token, body: { text: 'hi' } })).status, 200);
  await Promise.all(xs.map((r) => r.deferred));
  assert.equal(lobby.waiterCount, 1);
  const deeHeld = await poll(dee, y);
  assert.ok(deeHeld.deferred);
  // the timers release the rest when nothing happens; nothing is left behind (counters, sets, timers)
  advance(MAX_WAIT_S * 1000);
  const [cyAnswer, deeAnswer] = await Promise.all([cyHeld.deferred, deeHeld.deferred]);
  assert.equal(cyAnswer.status, 200);
  assert.equal(deeAnswer.status, 200);
  assert.equal(lobby.waiterCount, 0);
  assert.equal(lobby.waiters.size, 0);
  assert.equal(lobby.accountHolds.size, 0);
  assert.equal(timers.size, 0);
  // a game removed with holds on it releases them (the sweep of an idle game)
  const z = await poll(ann, x);
  assert.ok(z.deferred);
  advance(11 * 60 * 1000); // past openGameIdleMs: the timer answered first; then the sweep removes the game
  lobby.sweep();
  await z.deferred;
  assert.equal(lobby.accountHolds.size, 0);
  assert.equal(lobby.waiterCount, 0);
});

test('ONLINE-5 (Sol r1): an emptied slot keeps no choices: a reopened AI slot\'s spot and colour do not come back with the player who joins it', async () => {
  const { lobby, a, b, c, id, post, snapshot } = await room5();
  const slotsOf = () => lobby.games.get(id).slots.slice(0, 4).map((s) => `${s.state}:${s.startPos}/${s.color}/${s.faction}/${s.team}`);
  // Sol's reproduction: the AI in slot 3 holds spot 4 and colour 3; the host reopens the slot; Bea takes spot 4 and colour 3; Dee joins slot 3
  assert.equal((await post(a, 'slot', { slot: 3, state: 'open' })).status, 200);
  assert.deepEqual(slotsOf()[3], 'open:-1/-1/-1/-1');
  assert.equal((await post(b, 'slot', { startPos: 3, color: 3 })).status, 200);
  const d = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Dee' } })).body;
  const joined = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: d.token, body: { profile: PROFILE } });
  assert.equal(joined.body.slot, 3);
  assert.deepEqual(slotsOf(), ['player:0/0/0/-1', 'player:3/3/-1/-1', 'player:-1/-1/-1/-1', 'player:-1/-1/-1/-1']);
  // no two occupied slots share a spot or a colour
  const held = lobby.games.get(id).slots.filter((s) => s.state === 'player' || s.state === 'hard');
  for (const k of ['startPos', 'color']) {
    const taken = held.map((s) => s[k]).filter((v) => v >= 0);
    assert.equal(new Set(taken).size, taken.length, k);
  }
  // Dee may not take Bea's spot; an AI made in an emptied slot starts with none either
  assert.equal((await post(d, 'slot', { startPos: 3 })).body.error, 'spot_taken');
  assert.equal((await post(d, 'leave')).status, 200);
  assert.deepEqual(slotsOf()[3], 'open:-1/-1/-1/-1');
  assert.equal((await post(a, 'slot', { slot: 3, state: 'easy' })).status, 200);
  assert.deepEqual(slotsOf()[3], 'easy:-1/-1/-1/-1');
  // closing an AI's slot clears its choices too; an open or closed slot takes no choices (-1, "none", is accepted)
  assert.equal((await post(a, 'slot', { slot: 3, startPos: 5, color: 6 })).status, 200);
  assert.equal((await post(a, 'slot', { slot: 3, state: 'closed' })).status, 200);
  assert.deepEqual(slotsOf()[3], 'closed:-1/-1/-1/-1');
  const before = snapshot();
  const refused = await post(a, 'slot', { slot: 3, startPos: 5 });
  assert.equal(refused.body.error, 'bad_field');
  assert.equal(refused.body.message, 'an open or closed slot has no faction, color, startPos or team');
  assert.equal((await post(a, 'slot', { slot: 4, state: 'open', startPos: 6 })).body.error, 'bad_field');
  assert.equal(snapshot(), before);
  assert.equal((await post(a, 'slot', { slot: 3, state: 'open', startPos: -1, color: -1, faction: -1, team: -1 })).status, 200);
  assert.deepEqual(slotsOf()[3], 'open:-1/-1/-1/-1');
  // a new game whose open slots carry choices keeps none of them
  const e = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Eve' } })).body;
  const made = await call(lobby, 'POST', '/v1/games', { token: e.token, body: { name: 'x', profile: PROFILE, setup: { ...setup, slots: [{ state: 'player', startPos: 0 }, { state: 'open', startPos: 1, color: 2 }] } } });
  assert.equal(made.status, 200, JSON.stringify(made.body));
  assert.deepEqual([lobby.games.get(made.body.id).slots[1].startPos, lobby.games.get(made.body.id).slots[1].color], [-1, -1]);
  void c;
});
