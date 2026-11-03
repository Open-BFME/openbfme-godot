import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Lobby, configFromEnv, route } from '../src/lobby.mjs';
import { fileStore, nodeEnv } from '../src/server.mjs';
import { LobbyObject } from '../src/worker.mjs';
import { SqliteIdentityStore } from '../src/identity-store.mjs';
import { identityStorage } from './identity-storage.mjs';

const ENV = { TICKET_KEYS: `1:${'01'.repeat(32)}\n`, SESSION_KEY: '02'.repeat(32),
  MOD_KEY: '03'.repeat(32), RELAYS: 'lab=192.0.2.10:7350' };
const INSTALL = 'a'.repeat(32);
const RAW_IP = '198.51.100.27';

test('WOTR-9 Node file store creates and exports the war phase index', () => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-war-'));
  try {
    const store = fileStore(dir);
    const key = 'wars/0123456789abcdef.json';
    const record = new TextEncoder().encode('{"v":2,"phases":[]}');
    store.putBlob(key, record);
    assert.deepEqual(store.getBlob(key), record);
    assert.deepEqual(store.listBlobs('', 10).map((entry) => entry.key), [key]);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

async function call(lobby, method, path, body, token) {
  const result = await route(lobby, { method, path, query: new URLSearchParams(),
    headers: token ? { authorization: `Bearer ${token}` } : {}, ip: RAW_IP,
    bytes: async () => new TextEncoder().encode(JSON.stringify(body)) });
  return { status: result.status, body: JSON.parse(result.body) };
}

function fixtureLobby() {
  return new Lobby(nodeEnv({ saveSighting() {}, deleteSighting() {} }), configFromEnv(ENV));
}

async function createGame(lobby) {
  const guest = await call(lobby, 'POST', '/v1/auth/guest', { v: 1, name: 'Host', install: INSTALL,
    client: { platform: 'linux', version: 'fixture' } });
  assert.equal(guest.status, 200);
  const created = await call(lobby, 'POST', '/v1/games', { name: 'Room', profile: 'f'.repeat(32),
    setup: { map: 'synthetic.map', slots: [{ state: 'player' }, { state: 'open' }] } }, guest.body.token);
  assert.equal(created.status, 200);
  return lobby.game(created.body.id);
}

test('platform intake rejects unsupported values without poisoning sightings', async () => {
  const lobby = fixtureLobby();
  const poison = await call(lobby, 'POST', '/v1/auth/guest', { v: 1, name: 'Poison', install: INSTALL,
    client: { platform: 'attacker', version: 'fixture' } });
  assert.equal(poison.status, 400);
  assert.equal(lobby.getSighting(INSTALL), undefined);
  for (const platform of ['windows', 'linux', 'macos']) {
    const accepted = await call(lobby, 'POST', '/v1/auth/guest', { v: 1, name: 'Valid', install: INSTALL,
      client: { platform, version: 'fixture' } });
    assert.equal(accepted.status, 200);
    assert.equal(lobby.adminPlayer(INSTALL).platform, platform);
  }
});

test('Node migration rewrites legacy raw IP and platform before restart reads', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-privacy-'));
  try {
    const store = fileStore(dir);
    const lobby = new Lobby(nodeEnv(store), configFromEnv(ENV));
    const game = await createGame(lobby);
    const gamePath = join(dir, 'games', `${game.id}.json`);
    assert.equal(Object.hasOwn(game, 'ip'), false);
    assert.equal(readFileSync(gamePath, 'utf8').includes(RAW_IP), false);
    writeFileSync(gamePath, JSON.stringify({ ...game, ip: RAW_IP }));
    const sightingPath = join(dir, 'sightings', `${INSTALL}.json`);
    writeFileSync(sightingPath, JSON.stringify({ ...lobby.getSighting(INSTALL), platform: 'attacker' }));
    const reopened = fileStore(dir);
    const restored = new Lobby(nodeEnv(reopened), configFromEnv(ENV));
    restored.load(reopened.loadGames(), [], [], reopened.loadSightings());
    assert.equal(Object.hasOwn(restored.game(game.id), 'ip'), false);
    assert.equal(readFileSync(gamePath, 'utf8').includes(RAW_IP), false);
    assert.equal(JSON.parse(readFileSync(sightingPath, 'utf8')).platform, null);
    assert.equal(restored.adminPlayer(INSTALL).platform, null);
    assert.equal(restored.adminPlayers('Host').players[0].platform, null);
    await restored.setEnforcement(INSTALL, { v: 1, version: 1, block: { kind: 'ban', until: null, reason: 'Fixture' },
      mute_until: null, routing: '', notice: null, devices: [] });
    assert.equal((await call(restored, 'POST', '/v1/auth/guest', { name: 'Host', install: INSTALL })).status, 403);
    reopened.saveGame({ ...game, ip: RAW_IP });
    assert.equal(readFileSync(gamePath, 'utf8').includes(RAW_IP), false);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('Node startup removes only recognized abandoned game temp files', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-game-temp-'));
  try {
    const store = fileStore(dir);
    const lobby = new Lobby(nodeEnv(store), configFromEnv(ENV));
    const game = await createGame(lobby);
    const gameDir = join(dir, 'games');
    const canonical = join(gameDir, `${game.id}.json`);
    const canonicalBytes = readFileSync(canonical);
    const oldTemp = join(gameDir, `${game.id}.json.tmp`);
    const newTemp = join(gameDir, `${game.id}.json.${'a'.repeat(24)}.tmp`);
    const unknown = join(gameDir, 'notes.json.tmp');
    writeFileSync(oldTemp, JSON.stringify({ ...game, ip: RAW_IP }));
    writeFileSync(newTemp, JSON.stringify({ ...game, ip: RAW_IP }));
    writeFileSync(unknown, 'leave this file alone');

    const reopened = fileStore(dir);
    assert.equal(existsSync(oldTemp), false);
    assert.equal(existsSync(newTemp), false);
    assert.equal(readFileSync(unknown, 'utf8'), 'leave this file alone');
    assert.deepEqual(readFileSync(canonical), canonicalBytes);
    const restored = new Lobby(nodeEnv(reopened), configFromEnv(ENV));
    restored.load(reopened.loadGames(), [], [], reopened.loadSightings());
    assert.equal(restored.games.has(game.id), true);
    assert.equal(Object.hasOwn(JSON.parse(readFileSync(canonical, 'utf8')), 'ip'), false);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('Worker migration purges game IP and both legacy/indexed platforms before serving', async () => {
  const fixture = fixtureLobby();
  const game = await createGame(fixture);
  const storage = identityStorage();
  try {
    await storage.put(`game:${game.id}`, { ...game, ip: RAW_IP });
    const poison = { ...fixture.getSighting(INSTALL), platform: 'attacker' };
    await storage.put(`sighting:${INSTALL}`, poison);
    const index = new SqliteIdentityStore(storage);
    index.saveSighting(poison);
    // Simulate a deployment whose old identity migration already completed.
    for (const prefix of ['enforcement:', 'sighting:']) storage.sql.exec(
      'INSERT OR REPLACE INTO identity_migration VALUES (?, ?, ?)', prefix, null, 1);
    const state = { storage, blockConcurrencyWhile: (fn) => fn() };
    const object = new LobbyObject(state, ENV);
    await object.readyPromise;
    for (let i = 0; i < 10 && !object.ready; i++) await object.processMigration();
    assert.equal(object.ready, true);
    assert.equal(Object.hasOwn(await storage.get(`game:${game.id}`), 'ip'), false);
    assert.equal((await storage.get(`sighting:${INSTALL}`)).platform, null);
    assert.equal(object.identity.getSighting(INSTALL).platform, null);
    assert.equal(object.lobby.adminPlayer(INSTALL).platform, null);
    await object.lobby.env.store.saveGame({ ...game, ip: RAW_IP });
    assert.equal(Object.hasOwn(await storage.get(`game:${game.id}`), 'ip'), false);
    const restarted = new LobbyObject(state, ENV);
    await restarted.readyPromise;
    assert.equal(restarted.ready, true);
    assert.equal(Object.hasOwn(restarted.lobby.game(game.id), 'ip'), false);
    assert.equal(restarted.lobby.adminPlayer(INSTALL).platform, null);
  } finally { storage.db.close(); }
});

test('Worker startup paginates and sanitizes every legacy game before ready and restart', async () => {
  const fixture = fixtureLobby();
  const template = await createGame(fixture);
  const storage = identityStorage();
  const count = 1001;
  const boundaryBefore = (999).toString(16).padStart(16, '0');
  const boundaryAfter = (1000).toString(16).padStart(16, '0');
  const lastId = (count - 1).toString(16).padStart(16, '0');
  try {
    for (let index = 0; index < count; index++) {
      const id = index.toString(16).padStart(16, '0');
      const game = { ...structuredClone(template), id };
      if (id === boundaryBefore || id === boundaryAfter) game.ip = RAW_IP;
      await storage.put(`game:${id}`, game);
    }
    const state = { storage, blockConcurrencyWhile: (fn) => fn() };
    const object = new LobbyObject(state, ENV);
    await object.readyPromise;
    const boundaryRecords = await Promise.all([storage.get(`game:${boundaryBefore}`), storage.get(`game:${boundaryAfter}`)]);
    assert.deepEqual({
      ready: object.ready,
      count: object.lobby.games.size,
      lastLoaded: object.lobby.games.has(lastId),
      beforeHasIp: Object.hasOwn(boundaryRecords[0], 'ip'),
      afterHasIp: Object.hasOwn(boundaryRecords[1], 'ip'),
    }, {
      ready: true,
      count,
      lastLoaded: true,
      beforeHasIp: false,
      afterHasIp: false,
    });

    const restarted = new LobbyObject(state, ENV);
    await restarted.readyPromise;
    assert.equal(restarted.ready, true);
    assert.equal(restarted.lobby.games.size, count);
    assert.equal(restarted.lobby.games.has(lastId), true);
    assert.equal(Object.hasOwn(await storage.get(`game:${lastId}`), 'ip'), false);
  } finally { storage.db.close(); }
});
