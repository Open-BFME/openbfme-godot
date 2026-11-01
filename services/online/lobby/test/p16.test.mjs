// OpenBFME lobby service tests. GPL-3.0.
// Lane ONLINE16-FIX (Sol r1 #8): sixteen-slot rooms (docs/ONLINE.md "Extended map capacity (P16)"): the capacity carried through creation, slot
// updates, views and the START; slots beyond the map's starts closed; older clients (no X-OpenBFME-Max-Slots header) keep their eight-slot rooms
// and are refused from sixteen-slot ones; the per-game tag requirement (AUTH-1 cut-over: off by default).
import test from 'node:test';
import assert from 'node:assert/strict';
import { Lobby, route, configFromEnv, base64UrlDecode } from '../src/lobby.mjs';
import { nodeEnv } from '../src/server.mjs';

const KEY_HEX = Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, '0')).join('');
const PROFILE = '9ecbb999bef4925c679a5570eacd6b99';
const ENV = { TICKET_KEYS: `1:${KEY_HEX}\n`, SESSION_KEY: 'aa'.repeat(32), UPLOAD_KEY: 'bb'.repeat(32), RELAYS: 'lab=192.0.2.10:7350', PUBLIC_URL: 'http://lobby.test' };
const MAP16 = 'maps/p16 grid/p16 grid.map';

function fakeEnv() {
  let t = 1_700_000_000_000;
  let r = 0;
  const env = nodeEnv({});
  env.now = () => t;
  env.advance = (ms) => { t += ms; };
  env.randomBytes = (n) => Uint8Array.from({ length: n }, () => (r = (r * 1103515245 + 12345) & 0xff));
  return env;
}
// `slots`: the X-OpenBFME-Max-Slots header (undefined: an older client, which sends none)
async function call(lobby, method, path, { token, body, slots } = {}) {
  const url = new URL(path, 'http://lobby');
  const headers = {};
  if (token) headers.authorization = `Bearer ${token}`;
  if (slots !== undefined) headers['x-openbfme-max-slots'] = String(slots);
  const out = await route(lobby, {
    method, path: url.pathname, query: url.searchParams, ip: '198.51.100.1', headers,
    bytes: async () => new TextEncoder().encode(body === undefined ? '' : JSON.stringify(body)),
  });
  return { status: out.status, body: JSON.parse(out.body) };
}
async function guest(lobby, name) {
  return (await call(lobby, 'POST', '/v1/auth/guest', { body: { name } })).body.token;
}
// a 16-slot setup: the host in slot 0, `open` more open places, the rest closed up to the map's starts
function wide(capacity, open = 1, extra = {}) {
  const slots = [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }];
  for (let i = 1; i <= open; i++) slots.push({ state: 'open' });
  while (slots.length < 16) slots.push({ state: 'closed' });
  return { map: MAP16, startingCash: 1500, mapStartCount: capacity, slots, ...extra };
}

test('p16 lobby: a 16-slot room is created, viewed and listed with its capacity; an older client never sees it', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const created = await call(lobby, 'POST', '/v1/games', { token: host, slots: 16, body: { name: 'sixteen', profile: PROFILE, setup: wide(16, 3) } });
  assert.equal(created.status, 200, JSON.stringify(created.body));
  assert.equal(created.body.game.setup.mapStartCount, 16);
  assert.equal(created.body.game.slots.length, 16);
  assert.deepEqual(created.body.game.slots.map((s) => s.state).slice(0, 5), ['player', 'open', 'open', 'open', 'closed']);
  // the listing: with the capacity for a sixteen-slot client, absent for an older one
  const listed = (await call(lobby, 'GET', '/v1/games', { slots: 16 })).body.games;
  assert.equal(listed.length, 1);
  assert.equal(listed[0].mapStartCount, 16);
  assert.equal(listed[0].openSlots, 3);
  assert.equal((await call(lobby, 'GET', '/v1/games')).body.games.length, 0);
  assert.equal((await call(lobby, 'GET', '/v1/games', { slots: 8 })).body.games.length, 0);
  assert.equal((await call(lobby, 'GET', '/v1/games?running=1')).body.games.length, 0);
});

test('p16 lobby: the header is exactly 8 or 16; an extended room needs it; a capacity of 9 .. 16 only', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  // a fresh host per attempt (creation is rate-limited per account)
  const create = async (setup, slots) => call(lobby, 'POST', '/v1/games', { token: await guest(lobby, 'Host'), slots, body: { name: 'g', profile: PROFILE, setup } });
  for (const bad of ['12', '0', 'sixteen', '16 ']) {
    const r = await call(lobby, 'GET', '/v1/games', { slots: bad });
    assert.equal(r.status, 400, bad);
    assert.equal(r.body.error, 'bad_field');
  }
  for (const slots of [undefined, 8]) {
    const r = await create(wide(16), slots);
    assert.equal(r.status, 400);
    assert.equal(r.body.error, 'bad_field');
    assert.match(r.body.message, /16-player support/);
  }
  for (const capacity of [7, 17, 8.5, '16']) {
    const r = await create(wide(capacity), 16);
    assert.equal(r.status, 400, String(capacity));
  }
  // a place beyond the map's starts must be closed
  const open13 = wide(12);
  open13.slots[13] = { state: 'open' };
  assert.equal((await create(open13, 16)).status, 400);
  const ai13 = wide(12);
  ai13.slots[12] = { state: 'hard', name: 'Computer', faction: 1, color: 1, startPos: 1, team: 1 };
  assert.equal((await create(ai13, 16)).status, 400);
  // the host's own place is inside the capacity; start positions stop at the capacity
  const host12 = wide(12);
  host12.slots[0].startPos = 12;
  assert.equal((await create(host12, 16)).status, 400);
  // more than eight places on a legacy map: refused as before
  const legacy9 = { map: 'maps/map mp evendim/map mp evendim.map', slots: wide(16).slots.slice(0, 9) };
  assert.equal((await create(legacy9, 16)).status, 400);
});

test('p16 lobby: a legacy room keeps the eight-slot shape for every client', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const setup = { map: 'maps/map mp evendim/map mp evendim.map', slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }] };
  const created = await call(lobby, 'POST', '/v1/games', { token: host, slots: 16, body: { name: 'eight', profile: PROFILE, setup } });
  assert.equal(created.status, 200);
  assert.equal(created.body.game.slots.length, 8);
  assert.equal('mapStartCount' in created.body.game.setup, false);
  // an explicit capacity of 8 is the legacy room
  const explicit = await call(lobby, 'POST', '/v1/games', { token: await guest(lobby, 'Other'), slots: 16, body: { name: 'eight', profile: PROFILE, setup: { ...setup, mapStartCount: 8 } } });
  assert.equal(explicit.status, 200);
  assert.equal(explicit.body.game.slots.length, 8);
  assert.equal('mapStartCount' in explicit.body.game.setup, false);
  const listed = (await call(lobby, 'GET', '/v1/games')).body.games;
  assert.equal(listed.length, 2);
  assert.ok(listed.every((g) => !('mapStartCount' in g)));
  // an older client joins it
  const old = await guest(lobby, 'Old');
  const joined = await call(lobby, 'POST', `/v1/games/${created.body.id}/join`, { token: old, body: { profile: PROFILE } });
  assert.equal(joined.status, 200);
  assert.equal(joined.body.game.slots.length, 8);
});

test('p16 lobby: an older client is refused from a 16-slot room with the clear message, after a private room\'s code', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const id = (await call(lobby, 'POST', '/v1/games', { token: host, slots: 16, body: { name: 'sixteen', profile: PROFILE, setup: wide(16, 2) } })).body.id;
  const old = await guest(lobby, 'Old');
  const r = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: old, body: { profile: PROFILE } });
  assert.equal(r.status, 409);
  assert.equal(r.body.error, 'capacity');
  assert.equal(r.body.message, 'this game needs 16-player support');
  const newer = await guest(lobby, 'New');
  const ok = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: newer, slots: 16, body: { profile: PROFILE } });
  assert.equal(ok.status, 200);
  assert.equal(ok.body.slot, 1);
  assert.equal(ok.body.game.slots.length, 16);
  // a private 16-slot room: the code is checked before the capacity (nothing about the room is told without it)
  const host2 = await guest(lobby, 'Host2');
  const pid = (await call(lobby, 'POST', '/v1/games', { token: host2, slots: 16, body: { name: 'p', profile: PROFILE, room_code: 'ROOM16x', setup: wide(16, 2) } })).body.id;
  const noCode = await call(lobby, 'POST', `/v1/games/${pid}/join`, { token: old, body: { profile: PROFILE } });
  assert.equal(noCode.body.error, 'room_code');
  const withCode = await call(lobby, 'POST', '/v1/rooms/ROOM16x/join', { token: old, body: { profile: PROFILE } });
  assert.equal(withCode.body.error, 'capacity');
});

test('p16 lobby: slot updates reach slots 8 .. capacity-1 and never beyond; a capacity change keeps the room consistent', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const id = (await call(lobby, 'POST', '/v1/games', { token: host, slots: 16, body: { name: 'g', profile: PROFILE, setup: wide(12, 1) } })).body.id;
  const slot = (body) => call(lobby, 'POST', `/v1/games/${id}/slot`, { token: host, slots: 16, body });
  let r = await slot({ slot: 11, state: 'hard', name: 'Computer', faction: 2, color: 11, startPos: 11, team: 15 });
  assert.equal(r.status, 200, JSON.stringify(r.body));
  assert.equal(r.body.game.slots[11].state, 'hard');
  assert.equal(r.body.game.slots[11].team, 15);
  assert.equal((await slot({ slot: 12, state: 'open' })).status, 400); // beyond the map's 12 starts
  assert.equal((await slot({ slot: 16, state: 'open' })).status, 400);
  assert.equal((await slot({ slot: 10, state: 'easy', name: 'Computer', startPos: 12 })).status, 400);
  assert.equal((await slot({ slot: 10, state: 'easy', name: 'Computer', team: 16 })).status, 400);
  r = await slot({ slot: 9, state: 'open' });
  assert.equal(r.status, 200);
  // a player in slot 9: the host cannot shrink the map below it
  const p = await guest(lobby, 'P');
  const joined = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: p, slots: 16, body: { profile: PROFILE } });
  assert.equal(joined.body.slot, 1);
  const setup = (body, token = host) => call(lobby, 'POST', `/v1/games/${id}/setup`, { token, slots: 16, body });
  r = await setup({ mapStartCount: 8, map: 'maps/map mp evendim/map mp evendim.map' });
  assert.equal(r.status, 409); // the AI in slot 11 is an occupied place beyond 8
  assert.equal(r.body.error, 'capacity');
  await slot({ slot: 11, state: 'closed' });
  r = await setup({ mapStartCount: 8, map: 'maps/map mp evendim/map mp evendim.map' });
  assert.equal(r.status, 200, JSON.stringify(r.body));
  assert.equal(r.body.game.slots.length, 8); // back to the legacy shape
  assert.equal('mapStartCount' in r.body.game.setup, false);
  // growing again needs every member to support 16 slots: an older client in the room blocks it
  const old = await guest(lobby, 'Old');
  await slot({ slot: 2, state: 'open' });
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: old, body: { profile: PROFILE } })).status, 200);
  r = await setup({ mapStartCount: 16, map: MAP16 });
  assert.equal(r.status, 409);
  assert.equal(r.body.error, 'capacity');
  assert.match(r.body.message, /16-player support/);
});

test('p16 lobby: the START of a 16-slot game carries its capacity, all sixteen slots and wide tickets with their session_keys', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const tokens = [await guest(lobby, 'Host')];
  const id = (await call(lobby, 'POST', '/v1/games', { token: tokens[0], slots: 16, body: { name: 'g', profile: PROFILE, setup: wide(16, 15) } })).body.id;
  for (let i = 1; i < 16; i++) {
    tokens.push(await guest(lobby, `P${i}`));
    const j = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: tokens[i], slots: 16, body: { profile: PROFILE } });
    assert.equal(j.body.slot, i);
    const s = await call(lobby, 'POST', `/v1/games/${id}/slot`, { token: tokens[i], slots: 16, body: { faction: i % 7, color: i, startPos: i, team: i % 8 } });
    assert.equal(s.status, 200, JSON.stringify(s.body));
  }
  for (let i = 1; i < 16; i++) {
    assert.equal((await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: tokens[i], slots: 16, body: { ready: true } })).status, 200);
  }
  const started = await call(lobby, 'POST', `/v1/games/${id}/start`, { token: tokens[0], slots: 16 });
  assert.equal(started.status, 200, JSON.stringify(started.body));
  const start = started.body.start;
  assert.equal(start.setup.mapStartCount, 16);
  assert.equal(start.setup.slots.length, 16);
  const t = base64UrlDecode(start.ticket);
  assert.equal(t[0], 4); // the wide START ticket (version 4, a u16 mask)
  assert.equal(t[11] | (t[12] << 8), 0xffff);
  assert.match(start.session_key, /^[A-Za-z0-9_-]{22}$/);
  const last = (await call(lobby, 'GET', `/v1/games/${id}`, { token: tokens[15], slots: 16 })).body.start;
  assert.equal(last.slot, 15);
  assert.equal(base64UrlDecode(last.ticket)[10], 15);
  // a running 16-slot game is offered to watch to a sixteen-slot client only; an older one is refused by the same rule
  assert.equal((await call(lobby, 'GET', '/v1/games?running=1', { slots: 16 })).body.games.length, 1);
  assert.equal((await call(lobby, 'GET', '/v1/games?running=1')).body.games.length, 0);
  const watcher = await guest(lobby, 'Watcher');
  const refused = await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: watcher });
  assert.equal(refused.body.error, 'capacity');
  const spec = await call(lobby, 'POST', `/v1/games/${id}/spectate`, { token: watcher, slots: 16 });
  assert.equal(spec.status, 200);
  assert.equal(base64UrlDecode(spec.body.start.ticket)[0], 6);
});

test('p16 lobby: a 12-start map: the START names 12 and closes slots 12 .. 15; a mask within eight slots keeps the version-1 ticket', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const setup = wide(12, 1);
  setup.slots[11] = { state: 'brutal', name: 'Computer', faction: 3, color: 11, startPos: 11, team: 7 };
  const id = (await call(lobby, 'POST', '/v1/games', { token: host, slots: 16, body: { name: 'g', profile: PROFILE, setup } })).body.id;
  const p = await guest(lobby, 'P');
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: p, slots: 16, body: { profile: PROFILE } });
  await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: p, slots: 16, body: { ready: true } });
  const start = (await call(lobby, 'POST', `/v1/games/${id}/start`, { token: host, slots: 16 })).body.start;
  assert.equal(start.setup.mapStartCount, 12);
  assert.equal(start.setup.slots.length, 16);
  assert.deepEqual(start.setup.slots.slice(12).map((s) => s.state), ['closed', 'closed', 'closed', 'closed']);
  assert.equal(start.setup.slots[11].state, 'brutal');
  assert.equal(base64UrlDecode(start.ticket)[0], 1);
});

test('AUTH-1 cut-over: a game requires tags only when its setup asks; then every ticket carries the auth mode bit and older clients are refused', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const host = await guest(lobby, 'Host');
  const setup = { map: 'maps/map mp evendim/map mp evendim.map', requireAuth: true, slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }, { state: 'open' }] };
  // an older client cannot host one (it would not send the field anyway)
  assert.equal((await call(lobby, 'POST', '/v1/games', { token: host, body: { name: 'g', profile: PROFILE, setup } })).status, 400);
  const id = (await call(lobby, 'POST', '/v1/games', { token: host, slots: 8, body: { name: 'g', profile: PROFILE, setup } })).body.id;
  const old = await guest(lobby, 'Old');
  const refused = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: old, body: { profile: PROFILE } });
  assert.equal(refused.status, 409);
  assert.equal(refused.body.error, 'update');
  const p = await guest(lobby, 'P');
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: p, slots: 8, body: { profile: PROFILE } });
  await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: p, slots: 8, body: { ready: true } });
  const start = (await call(lobby, 'POST', `/v1/games/${id}/start`, { token: host, slots: 8 })).body.start;
  const t = base64UrlDecode(start.ticket);
  assert.equal(t[0], 7); // the mode-flagged ticket
  assert.equal(t[13], 4); // TICKET_MODE_AUTH, no canonical bit: a classic lockstep game that requires tags
  assert.match(start.session_key, /^[A-Za-z0-9_-]{22}$/);
  // turning it on in an open room with an older client in it is refused; the default stays off (version-1 tickets)
  const host2 = await guest(lobby, 'Host2');
  const plainSetup = { ...setup, requireAuth: undefined };
  const id2 = (await call(lobby, 'POST', '/v1/games', { token: host2, slots: 8, body: { name: 'g2', profile: PROFILE, setup: plainSetup } })).body.id;
  assert.equal((await call(lobby, 'POST', `/v1/games/${id2}/join`, { token: old, body: { profile: PROFILE } })).status, 200);
  const on = await call(lobby, 'POST', `/v1/games/${id2}/setup`, { token: host2, slots: 8, body: { requireAuth: true } });
  assert.equal(on.status, 409);
  assert.equal(on.body.error, 'update');
  await call(lobby, 'POST', `/v1/games/${id2}/ready`, { token: old, body: { ready: true } });
  const plain = (await call(lobby, 'POST', `/v1/games/${id2}/start`, { token: host2, slots: 8 })).body.start;
  assert.equal(base64UrlDecode(plain.ticket)[0], 1);
});
