// lane PROFILE-2 (owner decision 10-26 "A"): a client's archive list (POST /v1/profile-archives), named by its SHA-256 in a create / join, reaches
// the replay START a checker reads; an older client (no "archives") still creates, joins and starts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { Lobby, route, configFromEnv, bodyLimitFor, MAX_ARCHIVES_BYTES, MAX_JSON_BYTES, MAX_PENDING_ARCHIVE_BYTES, parseBlobKey } from '../src/lobby.mjs';
import { nodeEnv } from '../src/server.mjs';

const KEY_HEX = Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, '0')).join('');
const PROFILE = '9ecbb999bef4925c679a5570eacd6b99';
const ENV = { TICKET_KEYS: `# test\n1:${KEY_HEX}\n`, SESSION_KEY: 'aa'.repeat(32), UPLOAD_KEY: 'bb'.repeat(32), RELAYS: 'lab=192.0.2.10:7350', PUBLIC_URL: 'http://lobby.test' };
const setup = {
  map: 'maps/map mp evendim/map mp evendim.map',
  startingCash: 1500,
  slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }, { state: 'open' }],
};

function fakeEnv() {
  let t = 1_700_000_000_000;
  let r = 0;
  const blobs = new Map();
  const env = nodeEnv({ putBlob: (k, b) => blobs.set(k, b), getBlob: (k) => blobs.get(k) ?? null });
  env.now = () => t;
  env.randomBytes = (n) => Uint8Array.from({ length: n }, () => (r = (r * 1103515245 + 12345) & 0xff));
  env.blobs = blobs;
  return env;
}
async function call(lobby, method, path, { token, body } = {}) {
  const url = new URL(path, 'http://lobby');
  const out = await route(lobby, {
    method, path: url.pathname, query: url.searchParams, ip: '198.51.100.1',
    headers: token ? { authorization: `Bearer ${token}` } : {},
    bytes: async () => (body instanceof Uint8Array ? body : new TextEncoder().encode(body === undefined ? '' : JSON.stringify(body))),
  });
  return { status: out.status, body: out.headers['content-type']?.startsWith('application/json') ? JSON.parse(out.body) : out.body };
}
const idOf = (lines) => createHash('sha256').update(lines.map((l) => `${l}\n`).join('')).digest('hex');
const DISC = ['rotwk:INI.big 0123456789abcdef0123456789abcdef 100', 'rotwk:_patch201.big 11111111111111111111111111111111 200'];
const AIO = ['rotwk:INI.big 0123456789abcdef0123456789abcdef 100', 'rotwk:lang/EnglishAudio.big 22222222222222222222222222222222 300'];

test('PROFILE-2: the lobby names the archives feature and takes a list under its SHA-256, checked line by line; nothing is stored yet', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  assert.ok(lobby.health().features.includes('archives'));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  assert.equal((await call(lobby, 'POST', '/v1/profile-archives', { body: { id: idOf(DISC), lines: DISC } })).status, 401);
  const put = await call(lobby, 'POST', '/v1/profile-archives', { token: a.token, body: { id: idOf(DISC), lines: DISC } });
  assert.equal(put.status, 200, JSON.stringify(put.body));
  assert.equal(put.body.id, idOf(DISC));
  // Sol review P2 (storage): held until a game naming it starts, never stored by the upload alone
  assert.equal(env.blobs.has(`archives/${idOf(DISC)}.json`), false);
  assert.equal(lobby.pendingArchives.size, 1);
  assert.equal((await call(lobby, 'POST', '/v1/profile-archives', { token: a.token, body: { id: idOf(DISC), lines: DISC } })).status, 200);
  assert.equal(lobby.pendingArchives.size, 1); // the same list again: one entry
  // refused: an id that is not the lines', a malformed line, an extra field, no lines
  const bad = async (body) => (await call(lobby, 'POST', '/v1/profile-archives', { token: a.token, body })).body.error;
  assert.equal(await bad({ id: idOf(AIO), lines: DISC }), 'bad_field');
  assert.equal(await bad({ id: idOf(['x']), lines: ['x'] }), 'bad_field');
  assert.equal(await bad({ id: idOf(['rotwk:a.big 0123 1']), lines: ['rotwk:a.big 0123 1'] }), 'bad_field');
  assert.equal(await bad({ id: idOf(DISC), lines: DISC, more: 1 }), 'unknown_field');
  assert.equal(await bad({ id: idOf([]), lines: [] }), 'bad_field');
});

test('PROFILE-2: the archive upload has its own body bound; every other JSON body keeps 16 KiB', async () => {
  assert.equal(bodyLimitFor('/v1/profile-archives'), MAX_ARCHIVES_BYTES);
  assert.equal(bodyLimitFor('/v1/games'), MAX_JSON_BYTES);
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  // 300 archive lines (a pure install is 213; a mod adds its own): over 16 KiB, under the upload's bound
  const lines = Array.from({ length: 300 }, (_, i) => `rotwk:mods/some long folder name/archive number ${String(i).padStart(4, '0')}.big ${'0'.repeat(32)} 123456`);
  const body = { id: idOf(lines), lines };
  assert.ok(JSON.stringify(body).length > MAX_JSON_BYTES);
  assert.equal((await call(lobby, 'POST', '/v1/profile-archives', { token: a.token, body })).status, 200);
  const big = { name: 'g', profile: PROFILE, setup, pad: 'x'.repeat(MAX_JSON_BYTES) };
  assert.equal((await call(lobby, 'POST', '/v1/games', { token: a.token, body: big })).status, 413);
});

test('PROFILE-2: each human slot\'s archives id reaches the replay START (an added field, START version 1); an older client\'s is null', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const b = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Bea' } })).body;
  const c = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Cid' } })).body;
  assert.equal((await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: PROFILE, setup, archives: 'nothex' } })).body.error, 'bad_field');
  assert.equal((await call(lobby, 'POST', '/v1/profile-archives', { token: a.token, body: { id: idOf(DISC), lines: DISC } })).status, 200);
  const created = await call(lobby, 'POST', '/v1/games', { token: a.token, body: { name: 'g', profile: PROFILE, setup, archives: idOf(DISC) } });
  assert.equal(created.status, 200, JSON.stringify(created.body));
  const id = created.body.id;
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE, archives: idOf(AIO) } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: c.token, body: { profile: PROFILE } })).status, 200); // an older client
  for (const [t, i] of [[b, 1], [c, 2]]) {
    assert.equal((await call(lobby, 'POST', `/v1/games/${id}/slot`, { token: t.token, body: { faction: i, color: i, startPos: i, team: i } })).status, 200);
  }
  for (const t of [b, c]) assert.equal((await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: t.token, body: { ready: true } })).status, 200);
  const started = await call(lobby, 'POST', `/v1/games/${id}/start`, { token: a.token });
  assert.equal(started.status, 200, JSON.stringify(started.body));
  assert.equal(started.body.start.archives, undefined); // the players' STARTs are unchanged
  const replayStart = JSON.parse(new TextDecoder().decode(env.blobs.get(`starts/${id}.json`)));
  assert.equal(replayStart.v, 1);
  assert.deepEqual(replayStart.archives, { 0: idOf(DISC), 1: idOf(AIO), 2: null });
  // the lists the game names are stored now (uploaded beforehand); AIO was never uploaded: named, not stored (the checker says "missing")
  assert.deepEqual(JSON.parse(new TextDecoder().decode(env.blobs.get(`archives/${idOf(DISC)}.json`))), { v: 1, lines: DISC });
  assert.equal(env.blobs.has(`archives/${idOf(AIO)}.json`), false);
});

test('PROFILE-2 (Sol review P2): uploads are rate-limited per address and held in a bounded pool, the oldest dropped first', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await call(lobby, 'POST', '/v1/auth/guest', { body: { name: 'Ann' } })).body;
  const account = await lobby.account(`Bearer ${a.token}`);
  const list = (n) => Array.from({ length: 600 }, (_, i) => `rotwk:mods/m${n}/archive number ${String(i).padStart(4, '0')}.big ${'0'.repeat(32)} 1`);
  const ids = [];
  for (let n = 0; n < 400; n++) {
    const lines = list(n);
    ids.push(idOf(lines));
    await lobby.putArchives({ ...account, id: `guest:${n}` }, { id: idOf(lines), lines }, `203.0.113.${n % 250}/${n}`);
  }
  assert.ok(lobby.pendingArchiveBytes <= MAX_PENDING_ARCHIVE_BYTES); // tolerance: the pool's bound
  assert.equal(lobby.pendingArchives.has(ids[0]), false); // the oldest dropped
  assert.equal(lobby.pendingArchives.has(ids[399]), true);
  // one address: 12 at once, then refused
  let refused = 0;
  for (let n = 0; n < 14; n++) {
    const lines = list(1000 + n);
    try { await lobby.putArchives({ ...account, id: `guest:x${n}` }, { id: idOf(lines), lines }, '198.51.100.9'); } catch (e) { if (e.code === 'rate_limited') refused++; }
  }
  assert.equal(refused, 2);
});

test('PROFILE-2 (Sol review P5): an archive list is a backup blob; a restore takes back only the list its SHA-256 names', async () => {
  const key = `archives/${idOf(DISC)}.json`;
  assert.deepEqual(parseBlobKey(key), { kind: 'archives', id: idOf(DISC) });
  assert.deepEqual(parseBlobKey('starts/0123456789abcdef.json'), { kind: 'starts', id: '0123456789abcdef' });
  assert.equal(parseBlobKey(`archives/${'0'.repeat(63)}.json`), null);
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const bytes = new TextEncoder().encode(JSON.stringify({ v: 1, lines: DISC }));
  await lobby.adminPut(key, bytes);
  assert.deepEqual(env.blobs.get(key), bytes);
  await assert.rejects(lobby.adminPut(`archives/${idOf(AIO)}.json`, bytes), (e) => e.code === 'bad_blob');
  await assert.rejects(lobby.adminPut(`archives/${idOf(DISC)}.json`, new TextEncoder().encode(JSON.stringify({ v: 1, lines: ['x'] }))), (e) => e.code === 'bad_blob');
});
