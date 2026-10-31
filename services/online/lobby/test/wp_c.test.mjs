import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { mkdtempSync, rmSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Lobby, MAX_SIGHTINGS, configFromEnv, route } from '../src/lobby.mjs';
import { cleanupReportData, readPlayerReport, readPlayerReports, submitPlayerReport, REPORT_TTL_S } from '../src/reports.mjs';
import { fileStore, nodeEnv } from '../src/server.mjs';
import { durableReportTransaction, LobbyObject } from '../src/worker.mjs';
import { identityStorage } from './identity-storage.mjs';
import { SqliteIdentityStore } from '../src/identity-store.mjs';

const TICKET_KEY = '01'.repeat(32);
const ADMIN_KEY = 'a1'.repeat(32);
const MOD_KEY = 'b2'.repeat(32);
const SESSION_KEY = 'c3'.repeat(32);
const PROFILE = '9ecbb999bef4925c679a5570eacd6b99';
const ENV = {
  TICKET_KEYS: `1:${TICKET_KEY}\n`,
  SESSION_KEY,
  ADMIN_KEY,
  MOD_KEY,
  RELAYS: 'lab=192.0.2.10:7350',
  PUBLIC_URL: 'http://lobby.test',
};
const SETUP = {
  map: 'maps/map mp evendim/map mp evendim.map',
  startingCash: 1500,
  slots: [
    { state: 'player', faction: 0, color: 0, startPos: 0, team: 0 },
    { state: 'open' },
  ],
};
const encoder = new TextEncoder();

test('moderation report list and detail repair legacy report and closed-roster text', async () => {
  for (const text of ['\ud800x', 'é e\u0301 🌍']) {
    const reports = memoryReportStore();
    const env = fakeEnv({ reportTransaction: reports.reportTransaction.bind(reports) });
    const lobby = new Lobby(env, configFromEnv(ENV));
    const host = (await guest(lobby, 'Host', 'a'.repeat(32))).body;
    const other = (await guest(lobby, 'Other', 'b'.repeat(32))).body;
    const reporter = await lobby.account(`Bearer ${host.token}`);
    const target = await lobby.account(`Bearer ${other.token}`);
    const game = '0123456789abcdef';
    reports.data.set(`roster:${game}`, {
      game_id: game,
      participants: [reporter, target].map((account) => ({ account: account.id, install: account.install, nick: text })),
      active: [], state: 'finished', ended_at: Math.floor(env.now() / 1000),
    });
    const submitted = await call(lobby, 'POST', '/v1/reports', {
      token: host.token,
      body: { v: 1, game_id: game, target: target.id, reason: 'other: note' },
      headers: { 'idempotency-key': 'legacy-text-001' },
    });
    assert.equal(submitted.status, 201, JSON.stringify(submitted.body));
    const id = submitted.body.report_id;
    const source = reports.data.get(`report:${id}`);
    assert.equal(source.reporter.nick, text.toWellFormed());
    assert.equal(source.target.nick, text.toWellFormed());
    source.reporter.nick = text;
    source.target.nick = text;
    source.reason = `other: ${text}`;
    const before = JSON.stringify(source);
    const expected = { ...source, reporter: { ...source.reporter, nick: text.toWellFormed() },
      target: { ...source.target, nick: text.toWellFormed() }, reason: `other: ${text.toWellFormed()}` };
    const detail = await admin(lobby, 'GET', `/v1/admin/mod/reports/${id}`, undefined, env);
    assert.equal(detail.status, 200);
    assert.equal(detail.rawBody, JSON.stringify({ v: 1, report: expected }));
    const list = await admin(lobby, 'GET', '/v1/admin/mod/reports', undefined, env);
    assert.equal(list.status, 200);
    assert.equal(list.rawBody, JSON.stringify({ v: 1, items: [expected], next_cursor: null }));
    assert.equal(JSON.stringify(reports.data.get(`report:${id}`)), before, 'read repair does not mutate storage');
  }
});

test('a malformed legacy report is logged and skipped without losing the other reports', async () => {
  const reports = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reports.reportTransaction.bind(reports) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const now = Math.floor(env.now() / 1000);
  const id = `R-${'a'.repeat(24)}`;
  const source = { id, source: 'player', reporter: { install: 'a'.repeat(32), nick: '\ud800x' },
    target: { install: 'b'.repeat(32), nick: 'Good 🌍' }, game_id: '0123456789abcdef',
    reason: 'other: note', received_at: now, expires_at: now + REPORT_TTL_S };
  reports.data.set(`report:${id}`, source);
  reports.data.set(`index:${String(now).padStart(16, '0')}:${id}`, { id, expires_at: now + 100 });
  const broken = `R-${'b'.repeat(24)}`;
  reports.data.set(`report:${broken}`, { ...source, id: broken, reporter: null });
  reports.data.set(`index:${String(now).padStart(16, '0')}:${broken}`, { id: broken, expires_at: now + 100 });
  const warnings = [];
  const warn = console.warn;
  console.warn = (message) => warnings.push(message);
  try {
    const list = await admin(lobby, 'GET', '/v1/admin/mod/reports', undefined, env);
    assert.equal(list.status, 200);
    assert.deepEqual(list.body.items.map((item) => item.reporter.nick), ['�x']);
    assert.equal((await admin(lobby, 'GET', `/v1/admin/mod/reports/${broken}`, undefined, env)).status, 404);
    assert.match(warnings.join('\n'), /skipping malformed stored report/);
  } finally { console.warn = warn; }
});

function legacySighting(install, text, now) {
  return { install, nicks: [{ nick: text, first_seen: now, last_seen: now }], first_seen: now, last_seen: now,
    signins: 1, platform: 'linux', version: text,
    devices: [{ hash: 'd'.repeat(64), count: 1, first_seen: now, last_seen: now }] };
}

test('file sightings persist text-only repairs and leave well-formed JSON files byte-identical', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-text-'));
  try {
    const store = fileStore(dir);
    const env = fakeEnv(store);
    const now = Math.floor(env.now() / 1000);
    for (const [install, text] of [['a'.repeat(32), '\ud800x'], ['b'.repeat(32), 'é e\u0301 🌍']]) {
      const path = join(dir, 'sightings', `${install}.json`);
      const raw = JSON.stringify(legacySighting(install, text, now), null, 2) + '\n';
      writeFileSync(path, raw);
      const records = store.loadSightings();
      const lobby = new Lobby(env, configFromEnv(ENV));
      lobby.load([], [], [], records);
      const detail = await admin(lobby, 'GET', `/v1/admin/mod/players/${install}`, undefined, env);
      const list = await admin(lobby, 'GET', '/v1/admin/mod/players', undefined, env);
      assert.equal(detail.status, 200);
      assert.equal(list.status, 200);
      assert.equal(detail.body.nick, text.toWellFormed());
      assert.equal(detail.body.version, text.toWellFormed());
      assert.equal(list.body.players.find((player) => player.install === install).nick, text.toWellFormed());
      if (text.isWellFormed()) assert.equal(readFileSync(path, 'utf8'), raw);
      else {
        const repaired = JSON.parse(readFileSync(path, 'utf8'));
        assert.equal(repaired.platform, 'linux');
        assert.equal(repaired.version, '�x');
        assert.equal(repaired.nicks[0].nick, '�x');
      }
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('completed DO migrations rerun text repair, persist unchanged-platform sightings and rebuild nickname indexes', async () => {
  const storage = identityStorage();
  try {
    const index = new SqliteIdentityStore(storage);
    const now = Math.floor(Date.now() / 1000);
    const badInstall = 'a'.repeat(32);
    const goodInstall = 'b'.repeat(32);
    const kvInstall = 'c'.repeat(32);
    index.saveSighting(legacySighting(badInstall, '\ud800x', now));
    const good = legacySighting(goodInstall, 'é e\u0301 🌍', now);
    index.saveSighting(good);
    const before = storage.sql.exec('SELECT payload FROM identity_sightings WHERE install = ?', goodInstall).toArray()[0].payload;
    await storage.put(`sighting:${kvInstall}`, legacySighting(kvInstall, '\ud800x', now));
    for (const prefix of ['sighting:', 'enforcement:', 'platform-v1:kv', 'platform-v1:sql']) {
      storage.sql.exec('INSERT OR REPLACE INTO identity_migration VALUES (?, NULL, 1)', prefix);
    }
    const state = { storage, blockConcurrencyWhile(fn) { return fn(); } };
    const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
    await object.readyPromise;
    assert.equal(object.ready, true);
    const persisted = JSON.parse(storage.sql.exec('SELECT payload FROM identity_sightings WHERE install = ?', badInstall).toArray()[0].payload);
    assert.equal(persisted.platform, 'linux');
    assert.equal(persisted.version, '�x');
    assert.equal(persisted.nicks[0].nick, '�x');
    assert.equal((await storage.get(`sighting:${kvInstall}`)).nicks[0].nick, '�x');
    assert.equal(storage.sql.exec('SELECT nick FROM identity_nicks WHERE install = ?', badInstall).toArray()[0].nick, '�x');
    assert.equal(storage.sql.exec('SELECT payload FROM identity_sightings WHERE install = ?', goodInstall).toArray()[0].payload, before);
    const env = { now: () => Date.now() };
    const list = await admin(object.lobby, 'GET', `/v1/admin/mod/players?q=${encodeURIComponent('�x')}`, undefined, env);
    assert.equal(list.status, 200);
    assert.deepEqual(list.body.players.map((player) => player.install), [badInstall]);
    const detail = await admin(object.lobby, 'GET', `/v1/admin/mod/players/${badInstall}`, undefined, env);
    assert.equal(detail.status, 200);
    assert.equal(detail.body.nick, '�x');
    assert.equal(detail.body.version, '�x');
    assert.equal(detail.body.linked[0].nick, good.nicks[0].nick);
  } finally { storage.db.close(); }
});

for (const source of ['legacy-kv', 'platform-kv', 'platform-sql']) {
  test(`${source} sighting upgrade keeps international intake names and skips malformed records without blocking readiness`, async () => {
    const storage = identityStorage();
    const warnings = [];
    const warn = console.warn;
    console.warn = (message) => warnings.push(message);
    try {
      const env = fakeEnv();
      env.now = () => Date.now();
      const prior = new Lobby(env, configFromEnv(ENV));
      const index = new SqliteIdentityStore(storage);
      const names = ['玩家的名字真好听呀', '🧙‍♂️🌍', 'مرحبا بالعالم', '界'.repeat(24), '😀'.repeat(24), 'ا'.repeat(24), 'x'.repeat(24)];
      const before = new Map();
      for (const [i, name] of names.entries()) {
        const install = (i + 1).toString(16).padStart(32, '0');
        const signedIn = await call(prior, 'POST', '/v1/auth/guest', { body: {
          name, install, device: 'd'.repeat(64), client: { platform: 'linux', version: 'fixture' },
        } });
        assert.equal(signedIn.status, 200, name);
        const record = env.persisted.sightings.get(install);
        const payload = JSON.stringify(record, null, 2) + '\n';
        before.set(install, { name, payload });
        if (source === 'platform-sql') {
          index.saveSighting(record);
          storage.sql.exec('UPDATE identity_sightings SET payload = ? WHERE install = ?', payload, install);
        } else {
          storage.sql.exec('INSERT INTO test_kv VALUES (?, ?)', `sighting:${install}`, payload);
          if (source === 'platform-kv') index.saveSighting(record);
        }
      }
      const repairInstall = 'e'.repeat(32);
      const repair = { ...legacySighting(repairInstall, '\ud800'.repeat(24), Math.floor(Date.now() / 1000)), version: 'fixture' };
      if (source !== 'legacy-kv') index.saveSighting(repair);
      if (source !== 'platform-sql') await storage.put(`sighting:${repairInstall}`, repair);
      const brokenInstall = 'f'.repeat(32);
      const broken = legacySighting(brokenInstall, 'Broken', Math.floor(Date.now() / 1000));
      index.saveSighting(broken);
      broken.signins = 0;
      if (source === 'platform-sql') {
        storage.sql.exec('UPDATE identity_sightings SET payload = ? WHERE install = ?', JSON.stringify(broken), brokenInstall);
      } else {
        await storage.put(`sighting:${brokenInstall}`, broken);
      }
      if (source !== 'legacy-kv') for (const prefix of ['sighting:', 'enforcement:', 'platform-v1:kv', 'platform-v1:sql']) {
        storage.sql.exec('INSERT INTO identity_migration VALUES (?, NULL, 1)', prefix);
      }
      storage.sql.exec('DROP TABLE identity_migration_skips');
      const state = { storage, blockConcurrencyWhile: (fn) => fn() };
      const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
      await object.readyPromise;
      assert.equal(object.ready, true);
      assert.equal(object.migrationError, null);
      assert.equal((await object.fetch(new Request('https://lobby.test/v1/health'))).status, 200);
      for (const [install, { name, payload }] of before) {
        assert.deepEqual(Buffer.from(object.identity.getSighting(install).nicks[0].nick), Buffer.from(name));
        assert.equal(object.identity.players(name.toLocaleLowerCase('en-US'), null, 10, Math.floor(Date.now() / 1000))[0].install, install);
        const stored = source === 'platform-sql'
          ? storage.sql.exec('SELECT payload FROM identity_sightings WHERE install = ?', install).toArray()[0].payload
          : storage.sql.exec('SELECT value FROM test_kv WHERE key = ?', `sighting:${install}`).toArray()[0].value;
        assert.equal(stored, payload);
      }
      assert.equal(object.identity.getSighting(brokenInstall), undefined);
      assert.equal(object.identity.getSighting(repairInstall).nicks[0].nick, '�'.repeat(24));
      for (const table of ['identity_nicks', 'identity_devices']) {
        assert.equal(storage.sql.exec(`SELECT COUNT(*) AS n FROM ${table} WHERE install = ?`, brokenInstall).toArray()[0].n, 0);
      }
      const phase = source === 'platform-sql' ? 'text-v2:sql' : source === 'platform-kv' ? 'text-v2:kv' : 'sighting:';
      assert.equal(storage.sql.exec('SELECT count FROM identity_migration_skips WHERE prefix = ?', phase).toArray()[0].count, 1);
      assert.match(warnings.join('\n'), /skipping malformed stored sightings/);
      const counts = storage.sql.exec('SELECT * FROM identity_migration_skips ORDER BY prefix').toArray();
      const restarted = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
      await restarted.readyPromise;
      assert.equal(restarted.ready, true);
      assert.deepEqual(storage.sql.exec('SELECT * FROM identity_migration_skips ORDER BY prefix').toArray(), counts);
    } finally { console.warn = warn; storage.db.close(); }
  });
}

test('loaded enforcement and moderation game views repair legacy text without altering good values', async () => {
  for (const text of ['\ud800x', 'é e\u0301 🌍']) {
    const env = fakeEnv();
    const lobby = new Lobby(env, configFromEnv(ENV));
    const install = 'a'.repeat(32);
    const host = (await guest(lobby, 'Host', install)).body;
    const game = await createGame(lobby, host.token);
    const stored = structuredClone(lobby.games.get(game));
    stored.name = text;
    stored.slots[0].name = text;
    const enforcement = { install, ...effective(1, { notice: { id: text, text } }) };
    const restored = new Lobby(env, configFromEnv(ENV));
    restored.load([stored], [], [enforcement]);
    const games = await admin(restored, 'GET', '/v1/admin/mod/games', undefined, env);
    assert.equal(games.status, 200);
    assert.equal(games.body.games[0].name, text.toWellFormed());
    assert.equal(games.body.games[0].slots[0].name, text.toWellFormed());
    const states = await admin(restored, 'GET', '/v1/admin/mod/enforcement', undefined, env);
    assert.equal(states.status, 200);
    assert.equal(states.body.items[0].notice.text, text.toWellFormed());
    if (text.isWellFormed()) assert.equal(JSON.stringify(restored.games.get(game)), JSON.stringify(stored));
  }
});

test('file and DO game loaders persist repaired text and do not rewrite well-formed records', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-games-text-'));
  const storage = identityStorage();
  try {
    const files = fileStore(dir);
    const env = fakeEnv();
    const lobby = new Lobby(env, configFromEnv(ENV));
    const host = (await guest(lobby, 'Host', 'a'.repeat(32))).body;
    const id = await createGame(lobby, host.token);
    const original = structuredClone(lobby.games.get(id));
    original.created = original.touched = Date.now();
    for (const text of ['\ud800x', 'é e\u0301 🌍']) {
      const raw = { ...original, name: text, slots: original.slots.map((slot) => ({ ...slot, name: text })) };
      const path = join(dir, 'games', `${id}.json`);
      const bytes = JSON.stringify(raw, null, 2) + '\n';
      writeFileSync(path, bytes);
      const loaded = files.loadGames()[0];
      assert.equal(loaded.name, text.toWellFormed());
      if (text.isWellFormed()) assert.equal(readFileSync(path, 'utf8'), bytes);
      else assert.equal(JSON.parse(readFileSync(path, 'utf8')).name, '�x');
      await storage.put(`game:${id}`, raw);
      const object = new LobbyObject({ storage, blockConcurrencyWhile: (fn) => fn() }, { ...ENV, LOG_STORE: '' });
      await object.readyPromise;
      assert.equal(object.ready, true);
      const saved = await storage.get(`game:${id}`);
      assert.equal(saved.name, text.toWellFormed());
      if (text.isWellFormed()) assert.equal(JSON.stringify(saved), JSON.stringify(raw));
      const response = await admin(object.lobby, 'GET', '/v1/admin/mod/games', undefined, { now: () => Date.now() });
      assert.equal(response.status, 200);
      assert.equal(response.body.games[0].name, text.toWellFormed());
      assert.equal(response.body.games[0].slots[0].name, text.toWellFormed());
    }
  } finally { storage.db.close(); rmSync(dir, { recursive: true, force: true }); }
});

function fakeEnv(storeOverrides = {}) {
  let now = 1_700_000_000_000;
  let random = 0;
  const games = new Map();
  const enforcement = new Map();
  const sightings = new Map();
  const blobs = new Map();
  const moderationReceipts = new Map();
  const store = {
    saveGame: (game) => games.set(game.id, structuredClone(game)),
    deleteGame: (id) => games.delete(id),
    saveEnforcement: (record) => enforcement.set(record.install, structuredClone(record)),
    loadEnforcement: () => [...enforcement.values()].map((record) => structuredClone(record)),
    saveSighting: (record) => sightings.set(record.install, structuredClone(record)),
    deleteSighting: (install) => sightings.delete(install),
    loadSightings: () => [...sightings.values()].map((record) => structuredClone(record)),
    putBlob: (key, bytes) => blobs.set(key, bytes),
    getBlob: (key) => blobs.get(key) ?? null,
    claimModerationReceipt: (key, record) => {
      const old = moderationReceipts.get(key);
      if (old) return structuredClone(old);
      moderationReceipts.set(key, structuredClone(record));
      return null;
    },
    completeModerationReceipt: (key, record) => {
      const old = moderationReceipts.get(key);
      if (!old || old.status !== 'pending' || old.fingerprint !== record.fingerprint) throw new Error('claim missing');
      moderationReceipts.set(key, structuredClone(record));
    },
    ...storeOverrides,
  };
  const env = nodeEnv(store);
  env.now = () => now;
  env.advance = (seconds) => { now += seconds * 1000; };
  env.randomBytes = (n) => Uint8Array.from({ length: n }, () => (random = (random * 1103515245 + 12345) & 0xff));
  env.persisted = { games, enforcement, sightings, blobs, moderationReceipts };
  return env;
}

function memoryReportStore() {
  const data = new Map();
  let failed = false;
  let queue = Promise.resolve();
  const store = {
    data,
    failWrites(value) { failed = value; },
    reportTransaction(fn) {
      const run = queue.then(async () => {
        const draft = new Map([...data].map(([key, value]) => [key, structuredClone(value)]));
        let wrote = false;
        const tx = {
          get: async (key) => draft.has(key) ? structuredClone(draft.get(key)) : null,
          put: async (key, value) => { wrote = true; draft.set(key, structuredClone(value)); },
          delete: async (key) => { wrote = true; draft.delete(key); },
          list: async (prefix, { after = null, limit = 100, reverse = false } = {}) => {
            let keys = [...draft.keys()].filter((key) => key.startsWith(prefix));
            keys.sort();
            if (after !== null) keys = keys.filter((key) => reverse ? key < after : key > after);
            if (reverse) keys.reverse();
            return new Map(keys.slice(0, limit).map((key) => [key, structuredClone(draft.get(key))]));
          },
        };
        const result = await fn(tx);
        if (failed && wrote) throw new Error('simulated report storage failure');
        data.clear();
        for (const [key, value] of draft) data.set(key, value);
        return result;
      });
      queue = run.catch(() => {});
      return run;
    },
  };
  return store;
}

async function call(lobby, method, path, { token, body, headers = {}, ip = '198.51.100.1', raw, bodyRejection } = {}) {
  const url = new URL(path, 'http://lobby');
  const bytes = raw ?? (body === undefined ? new Uint8Array() : encoder.encode(JSON.stringify(body)));
  let out = await route(lobby, {
    method,
    path: url.pathname,
    query: url.searchParams,
    ip,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...headers },
    bytes: async () => bytes,
    ...(bodyRejection ? { bodyRejection } : {}),
  });
  if (out.deferred) out = await out.deferred;
  return {
    status: out.status,
    headers: out.headers,
    rawBody: out.body,
    body: out.headers['content-type']?.startsWith('application/json') ? JSON.parse(out.body) : out.body,
  };
}

function adminHeader(env, method, path, raw, key = MOD_KEY, time = Math.floor(env.now() / 1000)) {
  const url = new URL(path, 'http://lobby');
  const message = encoder.encode(`${method} ${url.pathname}?${url.searchParams.toString()}\n${time}\n`);
  const signed = Buffer.concat([Buffer.from(message), Buffer.from(raw)]);
  const mac = createHmac('sha256', Buffer.from(key, 'hex')).update(signed).digest('hex');
  return { 'x-openbfme-admin': `${time}.${mac}` };
}

async function admin(lobby, method, path, body, env, { key = MOD_KEY, time, raw, signedRaw } = {}) {
  const sent = raw ?? (body === undefined ? new Uint8Array() : encoder.encode(JSON.stringify(body)));
  const signed = signedRaw ?? sent;
  return call(lobby, method, path, {
    raw: sent,
    headers: adminHeader(env, method, path, signed, key, time),
  });
}

let commandNumber = 0;
function captureKick(lobby, id, slot, reason) {
  const game = lobby.games.get(id);
  const target = game.slots[slot];
  return {
    v: 1,
    command_id: `wp-c-command-${++commandNumber}`,
    expected_seq: game.seq,
    slot,
    expected_account: target.account,
    expected_install: target.install ?? null,
    reason,
  };
}

function captureClose(lobby, id, reason) {
  return { v: 1, command_id: `wp-c-command-${++commandNumber}`, expected_seq: lobby.games.get(id).seq, reason };
}

function effective(version, { block = null, mute_until = null, routing = '', notice = null, devices = [], block_devices } = {}) {
  return {
    v: 1, version, block, mute_until, routing, notice, devices,
    ...(block_devices === undefined ? {} : { block_devices }),
  };
}

async function update(lobby, env, install, state) {
  return admin(lobby, 'PUT', `/v1/admin/mod/enforcement/${install}`, state, env);
}

let ipSuffix = 0;
async function guest(lobby, name, install, device = null, extra = {}) {
  const body = {
    name,
    ...(install ? { install } : {}),
    ...(device ? { device } : {}),
    client: { platform: 'linux', version: 'test-1' },
    ...extra,
  };
  return call(lobby, 'POST', '/v1/auth/guest', { body, ip: `198.51.100.${++ipSuffix}` });
}

async function createGame(lobby, token, name = 'room') {
  const result = await call(lobby, 'POST', '/v1/games', {
    token,
    body: { name, profile: PROFILE, setup: SETUP },
  });
  assert.equal(result.status, 200, JSON.stringify(result.body));
  return result.body.id;
}

async function startedGame(lobby, host, other) {
  const id = await createGame(lobby, host.token, 'started');
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: other.token, body: { profile: PROFILE } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/ready`, { token: other.token, body: { ready: true } })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/start`, { token: host.token })).status, 200);
  return id;
}

test('all seven mod routes require MOD_KEY and return their admin-only interfaces', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const installA = '1'.repeat(32);
  const installB = '2'.repeat(32);
  const deviceA = 'a'.repeat(64);
  const a = (await guest(lobby, 'Alice', installA, deviceA)).body;
  const b = (await guest(lobby, 'Bea', installB, 'b'.repeat(64))).body;
  const id = await createGame(lobby, a.token);
  await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  assert.equal(lobby.games.get(id).slots[0].install, installA);
  assert.equal(lobby.games.get(id).slots[1].install, installB);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/leave`, { token: b.token })).status, 200);
  assert.equal(lobby.games.get(id).slots[1].install, null);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } })).status, 200);

  const unauthorized = [
    ['GET', '/v1/admin/mod/games', undefined],
    ['POST', `/v1/admin/mod/games/${id}/kick`, { v: 1, slot: 1, reason: 'remove player' }],
    ['POST', `/v1/admin/mod/games/${id}/close`, { v: 1, reason: 'close room' }],
    ['GET', '/v1/admin/mod/players?q=Alice&limit=50', undefined],
    ['GET', `/v1/admin/mod/players/${installA}`, undefined],
    ['PUT', `/v1/admin/mod/enforcement/${installA}`, effective(1)],
    ['GET', '/v1/admin/mod/enforcement', undefined],
  ];
  for (const [method, path, body] of unauthorized) {
    const result = await call(lobby, method, path, body === undefined ? {} : { body });
    assert.equal(result.status, 401, `${method} ${path}`);
    const outsider = await call(lobby, method, path, body === undefined ? { token: a.token } : { token: a.token, body });
    assert.equal(outsider.status, 401, `bearer token cannot authenticate ${method} ${path}`);
  }

  const games = await admin(lobby, 'GET', '/v1/admin/mod/games?state=open', undefined, env);
  assert.equal(games.status, 200);
  assert.equal(games.body.games[0].routing, '');
  assert.equal(games.body.games[0].map, SETUP.map);
  assert.equal(games.body.games[0].password, false);
  assert.equal(games.body.games[0].slots[0].install, installA);
  assert.equal(games.body.games[0].slots[1].install, installB);
  assert.equal((await admin(lobby, 'GET', '/v1/admin/mod/players?q=alice&limit=50', undefined, env)).body.players[0].install, installA);
  const detail = await admin(lobby, 'GET', `/v1/admin/mod/players/${installA}`, undefined, env);
  assert.equal(detail.status, 200);
  assert.equal(detail.body.devices, 1);
  assert.ok(!JSON.stringify(detail.body).includes(deviceA));

  const kickCommand = captureKick(lobby, id, 1, 'remove player');
  const kicked = await admin(lobby, 'POST', `/v1/admin/mod/games/${id}/kick`, kickCommand, env);
  assert.deepEqual({ status: kicked.status, kicked: kicked.body.kicked }, { status: 200, kicked: true });
  assert.equal(kicked.body.effect, 'removed_from_room');
  const kickedPoll = await call(lobby, 'GET', `/v1/games/${id}`, { token: b.token });
  assert.equal(kickedPoll.status, 403);
  assert.match(kickedPoll.body.message, /kicked or left/);
  // DEVINPR-1 (review of WP-C): the moderator's kick sticks: the player cannot join the room again, nor see it in the list
  const rejoined = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } });
  assert.equal(rejoined.status, 403);
  assert.equal(rejoined.body.error, 'not_in_game');
  assert.ok(!(await call(lobby, 'GET', '/v1/games', { token: b.token })).body.games.some((game) => game.id === id));
  assert.equal(lobby.games.get(id).slots[1].platform, undefined);
  const repeatedKick = await admin(lobby, 'POST', `/v1/admin/mod/games/${id}/kick`, kickCommand, env);
  assert.deepEqual(repeatedKick.body, kicked.body);
  const closeCommand = captureClose(lobby, id, 'close room');
  const closed = await admin(lobby, 'POST', `/v1/admin/mod/games/${id}/close`, closeCommand, env);
  assert.equal(closed.body.closed, true);

  const state = effective(1, { routing: 'alpha', devices: [deviceA] });
  assert.equal((await update(lobby, env, installA, state)).body.updated, true);
  const listed = await admin(lobby, 'GET', '/v1/admin/mod/enforcement', undefined, env);
  assert.equal(listed.status, 200);
  assert.equal(listed.body.items[0].install, installA);
  assert.deepEqual(listed.body.items[0].devices, [deviceA]);
  for (const key of [ADMIN_KEY, MOD_KEY]) {
    assert.equal((await admin(lobby, 'GET', '/v1/admin/relay-status', undefined, env, { key })).status, 200);
  }
});

test('signed-admin authentication separates keys and binds exact query, body, and clock', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = '3'.repeat(32);
  await guest(lobby, 'Casey', install);

  const path = '/v1/admin/mod/players?q=Casey&limit=10';
  const badMac = await call(lobby, 'GET', path, { headers: { 'x-openbfme-admin': `${Math.floor(env.now() / 1000)}.${'0'.repeat(64)}` } });
  assert.equal(badMac.body.error, 'admin_signature');
  const wrongKey = await admin(lobby, 'GET', path, undefined, env, { key: 'd4'.repeat(32) });
  assert.equal(wrongKey.status, 401);
  const backupKey = await admin(lobby, 'GET', path, undefined, env, { key: ADMIN_KEY });
  assert.equal(backupKey.status, 401);
  const future = await admin(lobby, 'GET', path, undefined, env, { time: Math.floor(env.now() / 1000) + 301 });
  assert.equal(future.body.error, 'admin_clock');
  const expired = await admin(lobby, 'GET', path, undefined, env, { time: Math.floor(env.now() / 1000) - 301 });
  assert.equal(expired.body.error, 'admin_clock');

  const signedQuery = '/v1/admin/mod/players?q=Casey&limit=10';
  const tamperedQuery = await call(lobby, 'GET', '/v1/admin/mod/players?q=Other&limit=10', {
    headers: adminHeader(env, 'GET', signedQuery, new Uint8Array()),
  });
  assert.equal(tamperedQuery.status, 401);
  const original = encoder.encode(JSON.stringify(effective(1)));
  const changed = encoder.encode(JSON.stringify(effective(2)));
  const tamperedBody = await call(lobby, 'PUT', `/v1/admin/mod/enforcement/${install}`, {
    raw: changed,
    headers: adminHeader(env, 'PUT', `/v1/admin/mod/enforcement/${install}`, original),
  });
  assert.equal(tamperedBody.status, 401);
  assert.equal(env.persisted.enforcement.size, 0);

  const missing = new Lobby(fakeEnv(), configFromEnv({ ...ENV, MOD_KEY: '' }));
  assert.equal((await admin(missing, 'GET', '/v1/admin/mod/games', undefined, fakeEnv())).status, 501);
  assert.throws(() => configFromEnv({ ...ENV, MOD_KEY: ADMIN_KEY }));
});

test('Lobby constructor enforces enforcement-check key length and isolation', () => {
  const config = configFromEnv(ENV);
  assert.throws(
    () => new Lobby(fakeEnv(), { ...config, enforcementCheckKey: config.adminKey }),
    /ENFORCEMENT_CHECK_KEY must be different/,
  );
  assert.throws(
    () => new Lobby(fakeEnv(), { ...config, enforcementCheckKey: new Uint8Array(31) }),
    /ENFORCEMENT_CHECK_KEY must be 32 bytes/,
  );
});

test('internal enforcement check requires its isolated key and returns routing and opt-in device decisions', async () => {
  const checkKey = 'e5'.repeat(32);
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv({ ...ENV, ENFORCEMENT_CHECK_KEY: checkKey }));
  const device = 'a'.repeat(64);
  const optOutDevice = 'b'.repeat(64);
  const target = '1'.repeat(32);
  const associated = '2'.repeat(32);
  const unblocked = '3'.repeat(32);
  const suspended = '4'.repeat(32);
  const direct = '5'.repeat(32);
  const directAssociated = '6'.repeat(32);
  const routingTag = '\t\n\u0085';
  await guest(lobby, 'Target', target, device);
  await guest(lobby, 'Associated', associated, device);
  await guest(lobby, 'Unblocked', unblocked);
  await guest(lobby, 'Suspended', suspended);
  await guest(lobby, 'Direct', direct, optOutDevice);
  await guest(lobby, 'Direct associated', directAssociated, optOutDevice);
  await update(lobby, env, unblocked, effective(1, { routing: routingTag }));
  await update(lobby, env, suspended, effective(1, {
    block: { kind: 'suspend', until: Math.floor(env.now() / 1000) + 5, reason: 'private suspension' },
  }));
  await update(lobby, env, direct, effective(1, {
    block: { kind: 'ban', until: null, reason: 'direct only' }, devices: [], block_devices: false,
  }));
  await update(lobby, env, target, effective(1, {
    routing: 'ranked',
    block: { kind: 'ban', until: null, reason: 'private reason' },
    block_devices: true,
  }));
  const body = { v: 1, nonce: 'f'.repeat(32), installs: [target, associated, unblocked, suspended, direct, directAssociated] };
  const path = '/v1/internal/enforcement/check';
  assert.equal((await admin(lobby, 'POST', path, { ...body, nonce: [body.nonce] }, env, { key: checkKey })).status, 400);
  for (const wrongKey of [ADMIN_KEY, MOD_KEY]) {
    const wrong = await admin(lobby, 'POST', path, body, env, { key: wrongKey });
    assert.equal(wrong.status, 401);
  }
  const stale = await admin(lobby, 'POST', path, body, env, { key: checkKey, time: Math.floor(env.now() / 1000) - 301 });
  assert.equal(stale.status, 401);
  const original = encoder.encode(JSON.stringify(body));
  const tampered = encoder.encode(JSON.stringify({ ...body, nonce: '0'.repeat(32) }));
  const changed = await call(lobby, 'POST', path, {
    raw: tampered,
    headers: adminHeader(env, 'POST', path, original, checkKey),
  });
  assert.equal(changed.status, 401);
  for (const [method, adminPath, adminBody] of [
    ['GET', '/v1/admin/relay-status', undefined],
    ['GET', '/v1/admin/mod/players', undefined],
    ['PUT', `/v1/admin/mod/enforcement/${direct}`, effective(2)],
  ]) {
    assert.equal((await admin(lobby, method, adminPath, adminBody, env, { key: checkKey })).status, 401);
  }
  const enforcementCount = env.persisted.enforcement.size;
  const sightingCount = env.persisted.sightings.size;
  const response = await admin(lobby, 'POST', path, body, env, { key: checkKey });
  assert.equal(response.status, 200);
  assert.equal(response.headers['cache-control'], 'no-store');
  assert.deepEqual(response.body.decisions, [
    { install: target, allowed: false, code: 'banned', routing: 'ranked' },
    { install: associated, allowed: false, code: 'banned', routing: '' },
    { install: unblocked, allowed: true, code: null, routing: routingTag },
    { install: suspended, allowed: false, code: 'suspended', routing: '' },
    { install: direct, allowed: false, code: 'banned', routing: '' },
    { install: directAssociated, allowed: true, code: null, routing: '' },
  ]);
  assert.equal(JSON.stringify(response.body).includes(device), false);
  assert.equal(JSON.stringify(response.body).includes('private reason'), false);
  assert.equal(env.persisted.enforcement.size, enforcementCount);
  assert.equal(env.persisted.sightings.size, sightingCount);
  env.advance(6);
  const afterExpiry = await admin(lobby, 'POST', path, body, env, { key: checkKey });
  assert.equal(afterExpiry.body.decisions.find((item) => item.install === suspended).allowed, true);
});

test('enforcement blocks current sessions, expires, mutes chat only, and carries notices', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = '4'.repeat(32);
  const device = 'c'.repeat(64);
  const first = (await guest(lobby, 'Dara', install, device)).body;
  const game = await createGame(lobby, first.token);

  const notice = { id: 'notice-1', text: 'A neutral service notice.' };
  assert.equal((await update(lobby, env, install, effective(1, { notice }))).status, 200);
  assert.deepEqual((await guest(lobby, 'Dara', install, device)).body.notice, notice);
  assert.equal((await update(lobby, env, install, effective(2, { block: { kind: 'suspend', until: Math.floor(env.now() / 1000) + 10, reason: 'scheduled maintenance' } }))).status, 200);
  const suspended = await call(lobby, 'GET', '/v1/stats', { token: first.token });
  assert.equal(suspended.status, 403);
  assert.equal(suspended.body.error, 'suspended');
  assert.equal(suspended.body.reason, 'scheduled maintenance');
  assert.equal('appeal' in suspended.body, false);
  assert.ok(suspended.body.message.includes('scheduled maintenance'));
  env.advance(11);
  assert.equal((await call(lobby, 'GET', '/v1/stats', { token: first.token })).status, 200);

  assert.equal((await update(lobby, env, install, effective(3, { mute_until: Math.floor(env.now() / 1000) + 10 }))).status, 200);
  const muted = await call(lobby, 'POST', `/v1/games/${game}/chat`, { token: first.token, body: { text: 'hello' } });
  assert.equal(muted.status, 403);
  assert.equal(muted.body.error, 'muted');
  assert.ok(Number.isSafeInteger(muted.body.until));
  assert.equal((await call(lobby, 'GET', '/v1/games', { token: first.token })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${game}/leave`, { token: first.token })).status, 200);
  const nextGame = await createGame(lobby, first.token, 'still allowed');
  env.advance(11);
  assert.equal((await call(lobby, 'POST', `/v1/games/${nextGame}/chat`, { token: first.token, body: { text: 'hello' } })).status, 200);
});

test('CHAT-1: a player the moderators muted is chat-muted in every START (by slot), and only while the mute lasts', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const hostInstall = '6'.repeat(32);
  const mutedInstall = '7'.repeat(32);
  const host = (await guest(lobby, 'Hal', hostInstall, 'e'.repeat(64))).body;
  const other = (await guest(lobby, 'Mo', mutedInstall, 'f'.repeat(64))).body;
  assert.equal((await update(lobby, env, mutedInstall, effective(1, { mute_until: Math.floor(env.now() / 1000) + 600 }))).status, 200);
  const id = await startedGame(lobby, host, other);
  const hostView = (await call(lobby, 'GET', `/v1/games/${id}?since=0`, { token: host.token })).body;
  const otherView = (await call(lobby, 'GET', `/v1/games/${id}?since=0`, { token: other.token })).body;
  assert.deepEqual(hostView.start.chatMuted.slice(0, 2), [false, true]);
  assert.deepEqual(otherView.start.chatMuted, hostView.start.chatMuted);
  assert.equal('chatMuted' in hostView.start.setup, false); // never in the replay's record
  // after the mute ended, the next game's START mutes nobody
  env.advance(601);
  const host2 = (await guest(lobby, 'Hal', hostInstall, 'e'.repeat(64))).body;
  const other2 = (await guest(lobby, 'Mo', mutedInstall, 'f'.repeat(64))).body;
  const id2 = await startedGame(lobby, host2, other2);
  const view2 = (await call(lobby, 'GET', `/v1/games/${id2}?since=0`, { token: host2.token })).body;
  assert.equal(view2.start.chatMuted.some((m) => m), false);
});

test('a ban applies to the next request and signed-in failures never become anonymous', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = '5'.repeat(32);
  const user = (await guest(lobby, 'Eli', install, 'd'.repeat(64))).body;
  const outsider = (await guest(lobby, 'Outsider', '0'.repeat(32))).body;
  const id = await createGame(lobby, user.token);
  const outsiderKick = await call(lobby, 'POST', `/v1/games/${id}/kick`, { token: outsider.token, body: { slot: 1 } });
  assert.equal(outsiderKick.status, 403);
  assert.equal(outsiderKick.body.error, 'not_host');
  const state = effective(1, { block: { kind: 'ban', until: null, reason: 'policy reason', appeal: 'legacy appeal text' } });
  assert.equal((await update(lobby, env, install, state)).status, 200);

  const calls = [
    ['GET', '/v1/health', undefined],
    ['GET', '/v1/motd', undefined],
    ['GET', `/v1/games/${id}`, undefined],
    ['GET', '/v1/games', undefined],
    ['GET', '/v1/mine', undefined],
    ['POST', '/v1/games', { name: 'blocked', profile: PROFILE, setup: SETUP }],
    ['POST', `/v1/games/${id}/join`, { profile: PROFILE }],
    ['POST', `/v1/games/${id}/slot`, { faction: 0, color: 0, startPos: 0, team: 0 }],
    ['POST', `/v1/games/${id}/setup`, { spectatorDelay: 0 }],
    ['POST', `/v1/games/${id}/ready`, { ready: true }],
    ['POST', `/v1/games/${id}/kick`, { slot: 1 }],
    ['POST', `/v1/games/${id}/start`, {}],
    ['POST', `/v1/games/${id}/spectate`, {}],
    ['POST', `/v1/games/${id}/rejoin`, {}],
    ['POST', `/v1/games/${id}/chat`, { text: 'hello' }],
    ['GET', `/v1/replays/${id}/log`, undefined],
    ['GET', '/v1/stats', undefined],
  ];
  for (const [method, path, body] of calls) {
    const unauthorized = await call(lobby, method, path, { token: 'not-a-valid-token', body });
    assert.equal(unauthorized.status, 401, `${method} ${path} with invalid token`);
    assert.equal(unauthorized.body.error, 'signed_out');
    const banned = await call(lobby, method, path, { token: user.token, body });
    assert.equal(banned.status, 403, `${method} ${path} with banned token`);
    assert.equal(banned.body.error, 'banned');
    assert.equal(banned.body.until, null);
    assert.equal(banned.body.reason, 'policy reason');
    assert.equal('appeal' in banned.body, false);
    if (path === '/v1/stats') assert.equal(banned.headers['cache-control'], 'no-store');
  }
  for (const path of ['/v1/health', '/v1/motd', '/v1/games', '/v1/stats']) {
    assert.equal((await call(lobby, 'GET', path)).status, 200, `${path} remains public without a token`);
    const unauthorized = await call(lobby, 'GET', path, { headers: { authorization: '' } });
    assert.equal(unauthorized.status, 401, `${path} rejects an explicitly empty credential`);
  }
  const deniedGuest = await guest(lobby, 'Eli', install, 'd'.repeat(64));
  assert.equal(deniedGuest.status, 403);
  assert.equal(deniedGuest.body.error, 'banned');
  const longReason = 'R'.repeat(1000);
  await update(lobby, env, install, effective(2, {
    block: { kind: 'ban', until: null, reason: longReason },
  }));
  const longRefusal = await call(lobby, 'GET', '/v1/stats', { token: user.token });
  assert.equal(longRefusal.body.reason, longReason);
  assert.ok(longRefusal.body.message.endsWith(longReason));
  assert.ok(encoder.encode(longRefusal.body.message).length <= 8192);
});

test('failed enforcement persistence leaves the active version and device index unchanged', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = 'a'.repeat(32);
  const originalDevice = 'b'.repeat(64);
  const replacementDevice = 'c'.repeat(64);
  assert.equal((await update(lobby, env, install, effective(1, { devices: [originalDevice] }))).status, 200);
  const saveEnforcement = env.store.saveEnforcement;
  env.store.saveEnforcement = async () => { throw new Error('simulated persistence rejection'); };
  const failed = await update(lobby, env, install, effective(2, { devices: [replacementDevice] }));
  assert.equal(failed.status, 500);
  assert.equal(failed.body.error, 'internal');
  assert.equal(lobby.enforcement.get(install).version, 1);
  assert.deepEqual(lobby.enforcement.get(install).devices, [originalDevice]);
  assert.deepEqual([...lobby.enforcementDeviceOwners.get(originalDevice)], [install]);
  assert.equal(lobby.enforcementDeviceOwners.has(replacementDevice), false);
  assert.equal(env.persisted.enforcement.get(install).version, 1);
  env.store.saveEnforcement = saveEnforcement;
});

test('enforcement wakes held polls and the deferred response rechecks current access', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = 'a'.repeat(32);
  const user = (await guest(lobby, 'Polling player', install)).body;
  const id = await createGame(lobby, user.token);
  const seq = lobby.games.get(id).seq;
  const pending = call(lobby, 'GET', `/v1/games/${id}?seq=${seq}&wait=30`, { token: user.token });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(lobby.waiterCount, 1);
  await update(lobby, env, install, effective(1, {
    block: { kind: 'ban', until: null, reason: 'newly blocked' },
  }));
  const response = await pending;
  assert.equal(response.status, 403);
  assert.equal(response.body.reason, 'newly blocked');
  assert.equal(lobby.waiterCount, 0);
  await update(lobby, env, install, effective(2));
  const routingPending = call(lobby, 'GET', `/v1/games/${id}?seq=${seq}&wait=30`, { token: user.token });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(lobby.waiterCount, 1);
  await update(lobby, env, install, effective(3, { routing: 'separate' }));
  const routingResponse = await routingPending;
  assert.equal(routingResponse.status, 404);
  assert.equal(routingResponse.body.error, 'no_game');
  assert.equal(lobby.waiterCount, 0);
});

test('routing isolates anonymous and authenticated game views and rejects cross-route access', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const installA = '6'.repeat(32);
  const installB = '7'.repeat(32);
  const installC = '8'.repeat(32);
  await update(lobby, env, installA, effective(1, { routing: 'route-a' }));
  await update(lobby, env, installB, effective(1, { routing: 'route-b' }));
  const a = (await guest(lobby, 'A', installA, 'e'.repeat(64))).body;
  const b = (await guest(lobby, 'B', installB, 'f'.repeat(64))).body;
  const c = (await guest(lobby, 'C', installC)).body;
  const gameA = await createGame(lobby, a.token, 'a room');
  const gameC = await createGame(lobby, c.token, 'public room');

  const anonymous = await call(lobby, 'GET', '/v1/games');
  assert.deepEqual(anonymous.body.games.map((game) => game.id), [gameC]);
  assert.deepEqual((await call(lobby, 'GET', '/v1/games', { token: b.token })).body.games, []);
  const accountGames = await call(lobby, 'GET', '/v1/games', { token: a.token });
  assert.deepEqual(accountGames.body.games.map((game) => game.id), [gameA]);
  assert.equal((await call(lobby, 'POST', `/v1/games/${gameA}/chat`, { token: a.token, body: { text: 'public room chat' } })).status, 200);
  const gameView = await call(lobby, 'GET', `/v1/games/${gameA}`, { token: a.token });
  assert.equal(gameView.body.chat.find((entry) => entry.text === 'public room chat').text, 'public room chat');
  const gameViewText = JSON.stringify(gameView.body);
  for (const privateValue of [installA, 'e'.repeat(64), 'route-a']) assert.ok(!gameViewText.includes(privateValue));
  for (const [method, path, body] of [
    ['GET', `/v1/games/${gameA}`, undefined],
    ['POST', `/v1/games/${gameA}/join`, { profile: PROFILE }],
    ['POST', `/v1/games/${gameA}/spectate`, {}],
    ['POST', `/v1/games/${gameA}/rejoin`, {}],
  ]) {
    const response = await call(lobby, method, path, { token: b.token, body });
    assert.equal(response.status, 404);
    assert.equal(response.body.error, 'no_game');
  }
  assert.deepEqual((await call(lobby, 'GET', '/v1/mine', { token: b.token })).body.games, []);
  const publicText = JSON.stringify(anonymous.body);
  for (const privateValue of [installA, 'e'.repeat(64), 'route-a']) assert.ok(!publicText.includes(privateValue));
  const accountText = JSON.stringify(accountGames.body);
  for (const privateValue of [installA, 'e'.repeat(64), 'route-a']) assert.ok(!accountText.includes(privateValue));
  const adminGames = await admin(lobby, 'GET', '/v1/admin/mod/games', undefined, env);
  assert.ok(JSON.stringify(adminGames.body).includes('route-a'));
  assert.ok(JSON.stringify(adminGames.body).includes(installA));
  assert.equal((await update(lobby, env, installA, effective(2, { routing: 'route-a-next' }))).status, 200);
  assert.equal((await call(lobby, 'GET', `/v1/games/${gameA}`, { token: a.token })).body.error, 'no_game');
  assert.deepEqual((await call(lobby, 'GET', '/v1/games', { token: a.token })).body.games, []);
});

test('effective versions, explicit device propagation, expiry, and owner lifting are independent', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const device = '9'.repeat(64);
  const a = 'a'.repeat(32);
  const b = 'b'.repeat(32);
  const c = 'c'.repeat(32);
  await guest(lobby, 'A', a, device);
  await guest(lobby, 'B', b, device);
  assert.equal((await guest(lobby, 'C', c, device)).status, 200, 'sighting alone does not block');

  const ban = effective(1, { block: { kind: 'ban', until: null, reason: 'owner A' }, devices: [device] });
  assert.equal((await update(lobby, env, a, ban)).body.updated, true);
  const suspend = effective(1, { block: { kind: 'suspend', until: Math.floor(env.now() / 1000) + 100, reason: 'owner B' }, devices: [device] });
  await update(lobby, env, b, suspend);
  assert.equal((await guest(lobby, 'C', c, device)).body.error, 'banned');

  assert.deepEqual((await update(lobby, env, a, ban)).body, { v: 1, unchanged: true });
  const newer = effective(2, { block: { kind: 'suspend', until: Math.floor(env.now() / 1000) + 100, reason: 'new owner state' }, devices: [device] });
  assert.equal((await update(lobby, env, a, newer)).body.updated, true);
  assert.equal((await update(lobby, env, a, ban)).body.stale, true);
  assert.equal((await update(lobby, env, a, effective(2, { routing: 'different' }))).status, 409);
  assert.equal((await update(lobby, env, a, effective(3, { devices: [] }))).body.updated, true);
  assert.equal((await guest(lobby, 'C', c, device)).body.error, 'suspended', 'lifting A does not clear B');
  assert.equal((await update(lobby, env, b, effective(2))).body.updated, true);
  assert.equal((await guest(lobby, 'C', c, device)).status, 200);
});

test('effective state is exact and bounded before anything is persisted', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const install = 'f'.repeat(32);
  const invalid = [
    { ...effective(1), v: 2 },
    { ...effective(1), extra: true },
    { ...effective(1), routing: 'r'.repeat(65) },
    effective(1, { block: { kind: 'ban', until: null, reason: 'r'.repeat(1001) } }),
    effective(1, { notice: { id: 'n'.repeat(129), text: 'notice' } }),
    effective(1, { notice: { id: 'notice', text: 'n'.repeat(4001) } }),
    effective(1, { devices: ['A'.repeat(64)] }),
    effective(1, { devices: ['a'.repeat(64), 'a'.repeat(64)] }),
  ];
  for (const state of invalid) assert.equal((await update(lobby, env, install, state)).status, 400);
  assert.equal(env.persisted.enforcement.size, 0);

  const boundary = effective(1, { notice: { id: 'n'.repeat(128), text: 't'.repeat(4000) } });
  assert.equal((await update(lobby, env, install, boundary)).body.updated, true);
  assert.equal(env.persisted.enforcement.get(install).notice.id.length, 128);
  assert.equal(env.persisted.enforcement.get(install).notice.text.length, 4000);
  assert.equal((await update(lobby, env, install, effective(2, { notice: { id: 'n', text: 't'.repeat(4001) } }))).status, 400);
  assert.equal(env.persisted.enforcement.get(install).version, 1);
});

test('open host kick closes; started kick preserves slots and denies poll, rejoin and spectate', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const a = (await guest(lobby, 'Host', 'd'.repeat(32))).body;
  const b = (await guest(lobby, 'Joiner', 'e'.repeat(32))).body;
  const open = await createGame(lobby, a.token, 'open');
  const closeHost = await admin(lobby, 'POST', `/v1/admin/mod/games/${open}/kick`, captureKick(lobby, open, 0, 'close host room'), env);
  assert.equal(closeHost.body.closed, true);
  assert.equal(closeHost.body.effect, 'removed_from_room');

  const started = await startedGame(lobby, a, b);
  const startedKick = captureKick(lobby, started, 1, 'remove from running game');
  const kicked = await admin(lobby, 'POST', `/v1/admin/mod/games/${started}/kick`, startedKick, env);
  assert.equal(kicked.body.kicked, true);
  assert.equal(kicked.body.effect, 'rejoin_refused');
  assert.equal(lobby.games.get(started).slots[1].state, 'player');
  assert.equal((await call(lobby, 'GET', `/v1/games/${started}`, { token: b.token })).status, 403);
  assert.equal((await call(lobby, 'POST', `/v1/games/${started}/rejoin`, { token: b.token })).status, 403);
  assert.equal((await call(lobby, 'POST', `/v1/games/${started}/spectate`, { token: b.token })).status, 403);
  const repeatedStartedKick = await admin(lobby, 'POST', `/v1/admin/mod/games/${started}/kick`, startedKick, env);
  assert.deepEqual(repeatedStartedKick.body, kicked.body);

  const closeOnlyOpen = await admin(lobby, 'POST', `/v1/admin/mod/games/${started}/close`, captureClose(lobby, started, 'cannot close started'), env);
  assert.equal(closeOnlyOpen.status, 409);
});

test('REQUIRE_INSTALL rejects missing guest and legacy no-install tokens', async () => {
  const env = fakeEnv();
  const legacy = new Lobby(env, configFromEnv(ENV));
  const token = (await guest(legacy, 'Legacy', null)).body.token;
  const required = new Lobby(env, configFromEnv({ ...ENV, REQUIRE_INSTALL: '1' }));
  assert.equal((await call(required, 'GET', '/v1/stats', { token })).body.error, 'signed_out');
  const missing = await call(required, 'POST', '/v1/auth/guest', { body: { name: 'No identity' } });
  assert.equal(missing.status, 400);
  assert.equal(missing.body.error, 'install_required');
  const malformed = await call(legacy, 'POST', '/v1/auth/guest', { body: { name: 'Bad', install: 'A'.repeat(32) } });
  assert.equal(malformed.status, 400);
});

test('sightings preserve the latest ten nicknames and five device hashes', async () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const install = '9'.repeat(32);
  for (let index = 0; index < 12; index++) {
    const device = index.toString(16).padStart(64, '0');
    const result = await guest(lobby, `Nickname${index}`, install, device, {
      client: { platform: 'linux', version: `build-${index}` },
    });
    assert.equal(result.status, 200, JSON.stringify(result.body));
  }
  const record = lobby.sightings.get(install);
  assert.equal(record.signins, 12);
  assert.equal(record.nicks.length, 10);
  assert.equal(record.nicks[0].nick, 'Nickname11');
  assert.ok(!record.nicks.some(({ nick }) => nick === 'Nickname0'));
  assert.equal(record.devices.length, 5);
  assert.equal(record.devices[0].hash, '0'.repeat(63) + 'b');
  assert.ok(!record.devices.some(({ hash }) => hash === '0'.repeat(64)));
  assert.equal(record.platform, 'linux');
  assert.equal(record.version, 'build-11');
  const repeated = await guest(lobby, 'Nickname11', install, '0'.repeat(63) + 'b', { client: undefined });
  assert.equal(repeated.status, 200);
  const updated = lobby.sightings.get(install);
  assert.equal(updated.signins, 13);
  assert.equal(updated.devices[0].count, 2);
  assert.equal(updated.platform, 'linux');
  assert.equal(updated.version, 'build-11');
});

// DEVINPR-1 (review of WP-C): a full sightings table (one IP can fill it with random installs over a few days) forgets the install seen longest ago;
// a new player's sign-in is never refused for it, and an enforcement record is not touched
// DEVINPR-1 (review of WP-C): a player banned while in a room can still leave it (their slot opens for the others); nothing else
test('a banned player in an open room can leave it, and only that', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'Host', '6'.repeat(32))).body;
  const install = '7'.repeat(32);
  const joiner = (await guest(lobby, 'Joiner', install)).body;
  const id = await createGame(lobby, host.token);
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/join`, { token: joiner.token, body: { profile: PROFILE } })).status, 200);
  const ban = effective(1, { block: { kind: 'ban', until: null, reason: 'policy reason' } });
  assert.equal((await update(lobby, env, install, ban)).status, 200);
  const poll = await call(lobby, 'GET', `/v1/games/${id}`, { token: joiner.token });
  assert.equal(poll.status, 403);
  assert.equal(poll.body.error, 'banned');
  const chat = await call(lobby, 'POST', `/v1/games/${id}/chat`, { token: joiner.token, body: { text: 'hi' } });
  assert.equal(chat.status, 403);
  const left = await call(lobby, 'POST', `/v1/games/${id}/leave`, { token: joiner.token });
  assert.equal(left.status, 200, JSON.stringify(left.body));
  assert.equal(left.body.left, true);
  assert.equal(lobby.games.get(id).slots[1].state, 'open');
  assert.equal(lobby.games.get(id).slots[1].install, null);
  const rejoin = await call(lobby, 'POST', `/v1/games/${id}/join`, { token: joiner.token, body: { profile: PROFILE } });
  assert.equal(rejoin.status, 403);
  assert.equal(rejoin.body.error, 'banned');
  assert.equal((await call(lobby, 'POST', `/v1/games/${id}/leave`, { token: 'not-a-valid-token' })).status, 401);
});

test('a full sightings table forgets the install seen longest ago instead of refusing new players', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const now = Math.floor(env.now() / 1000);
  const rows = Array.from({ length: MAX_SIGHTINGS }, (_, index) => ({
    install: (index + 1).toString(16).padStart(32, '0'),
    nicks: [{ nick: 'Filler', first_seen: now - 300, last_seen: index === 777 ? now - 200 : now - 100 }],
    first_seen: now - 300,
    last_seen: index === 777 ? now - 200 : now - 100,
    signins: 1,
    platform: null,
    version: null,
    devices: [],
  }));
  lobby.load([], [], [], rows);
  for (const row of rows) env.persisted.sightings.set(row.install, row);
  const oldest = (778).toString(16).padStart(32, '0');
  const answer = await guest(lobby, 'Newcomer', 'f'.repeat(32));
  assert.equal(answer.status, 200);
  assert.equal(lobby.sightings.size, MAX_SIGHTINGS);
  assert.ok(lobby.sightings.has('f'.repeat(32)));
  assert.ok(!lobby.sightings.has(oldest));
  assert.ok(!env.persisted.sightings.has(oldest));
  const next = await guest(lobby, 'Second', 'e'.repeat(32));
  assert.equal(next.status, 200);
  assert.equal(lobby.sightings.size, MAX_SIGHTINGS);
  assert.ok(!lobby.sightings.has((1).toString(16).padStart(32, '0'))); // then the lowest install of the oldest time
});

test('stored state validation is explicit; expired sightings hide immediately and clean up in bounded batches', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  assert.throws(() => lobby.load([], [], [{ install: 'bad' }], []), /stored enforcement is malformed/);
  assert.throws(() => lobby.load([], [], [], [{ install: 'bad' }]), /stored sighting is malformed/);
  const now = Math.floor(env.now() / 1000);
  const freshIndexes = new Set([20, 50, 150, 240]);
  const sharedDevice = 'a'.repeat(64);
  const rows = Array.from({ length: 250 }, (_, index) => {
    const fresh = freshIndexes.has(index);
    const lastSeen = fresh ? now : now - 366 * 86400;
    const devices = index === 30 || index === 20
      ? [{ hash: sharedDevice, count: 1, first_seen: lastSeen, last_seen: lastSeen }]
      : [];
    return {
      install: index.toString(16).padStart(32, '0'),
      nicks: [{ nick: fresh ? 'Fresh' : 'Old', first_seen: lastSeen, last_seen: lastSeen }],
      first_seen: lastSeen,
      last_seen: lastSeen,
      signins: 1,
      platform: null,
      version: null,
      devices,
    };
  });
  lobby.load([], [], [], rows);
  for (const row of rows) env.persisted.sightings.set(row.install, structuredClone(row));
  const freshInstalls = [...freshIndexes].map((index) => index.toString(16).padStart(32, '0')).sort();
  assert.deepEqual(lobby.adminPlayers('', null, 50).players.map(({ install }) => install), freshInstalls);
  assert.throws(() => lobby.adminPlayer('1e'.padStart(32, '0')), (error) => error.code === 'no_player');
  assert.deepEqual(lobby.adminPlayer('14'.padStart(32, '0')).linked, []);

  await lobby.cleanupSightingsIfDue(now);
  assert.equal(lobby.sightings.size, 152);
  assert.equal(lobby.sightingCleanupCursor, '63'.padStart(32, '0'));
  env.advance(59);
  await lobby.cleanupSightingsIfDue(now + 59);
  assert.equal(lobby.sightings.size, 152);
  env.advance(1);
  await lobby.cleanupSightingsIfDue(now + 60);
  assert.equal(lobby.sightings.size, 53);
  assert.equal(lobby.sightingCleanupCursor, 'c7'.padStart(32, '0'));
  env.advance(60);
  await lobby.cleanupSightingsIfDue(now + 120);
  assert.equal(lobby.sightings.size, freshIndexes.size);
  assert.equal(env.persisted.sightings.size, freshIndexes.size);
  assert.deepEqual(lobby.adminPlayers('', null, 50).players.map(({ install }) => install), freshInstalls);
  assert.equal(lobby.adminPlayer('14'.padStart(32, '0')).linked.length, 0);
});

test('file and Durable Object adapters persist sightings and enforcement independently of relay meta', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-wpc-'));
  let sqlite;
  try {
    const store = fileStore(dir);
    const env = nodeEnv(store);
    let random = 0;
    env.randomBytes = (n) => Uint8Array.from({ length: n }, () => (random = (random * 1103515245 + 12345) & 0xff));
    const first = new Lobby(env, configFromEnv(ENV));
    first.load(store.loadGames(), store.loadMeta(), store.loadEnforcement(), store.loadSightings());
    const a = (await guest(first, 'File player', 'f'.repeat(32), '1'.repeat(64))).body;
    const b = (await guest(first, 'Other player', '0'.repeat(32))).body;
    const id = await createGame(first, a.token, 'persistent');
    assert.equal((await call(first, 'POST', `/v1/games/${id}/join`, { token: b.token, body: { profile: PROFILE } })).status, 200);
    await update(first, env, 'f'.repeat(32), effective(1, {
      routing: 'kept',
      block: { kind: 'ban', until: null, reason: 'persisted opt-in' },
      block_devices: true,
    }));
    const offInstall = '4'.repeat(32);
    const falseInstall = '5'.repeat(32);
    const unbanInstall = '6'.repeat(32);
    assert.equal((await update(first, env, offInstall, effective(1))).status, 200);
    assert.equal((await update(first, env, falseInstall, effective(1, { block_devices: false }))).status, 200);
    assert.equal((await update(first, env, unbanInstall, effective(1, {
      block: { kind: 'ban', until: null, reason: 'temporary ban' },
      block_devices: false,
    }))).status, 200);
    assert.equal((await update(first, env, unbanInstall, effective(2, { block_devices: false }))).status, 200);
    await admin(first, 'POST', `/v1/admin/mod/games/${id}/kick`, captureKick(first, id, 1, 'persist kick'), env);

    const restarted = new Lobby(env, configFromEnv(ENV));
    restarted.load(store.loadGames(), store.loadMeta(), store.loadEnforcement(), store.loadSightings());
    assert.equal(restarted.sightings.get('f'.repeat(32)).signins, 1);
    assert.equal(restarted.enforcement.get('f'.repeat(32)).routing, 'kept');
    assert.equal(restarted.enforcement.get('f'.repeat(32)).block_devices, true);
    assert.deepEqual(restarted.enforcement.get('f'.repeat(32)).devices, ['1'.repeat(64)]);
    for (const install of [offInstall, falseInstall, unbanInstall]) {
      assert.equal(restarted.enforcement.get(install).block_devices, undefined);
      assert.deepEqual(restarted.enforcement.get(install).devices, []);
    }
    assert.equal(restarted.enforcement.get(unbanInstall).block, null);
    assert.equal(restarted.games.get(id).slots[1].install, null);
    assert.equal((await call(restarted, 'GET', `/v1/games/${id}`, { token: b.token })).status, 403);
    assert.equal(restarted.relayStatus.size, 0);

    const fakeStorage = identityStorage();
    const state = { storage: fakeStorage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
    const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
    await object.readyPromise;
    const doLobby = object.lobby;
    const doA = (await guest(doLobby, 'DO player', '2'.repeat(32), '2'.repeat(64))).body;
    const doB = (await guest(doLobby, 'DO joiner', '3'.repeat(32))).body;
    const doGame = await createGame(doLobby, doA.token, 'durable object room');
    assert.equal((await call(doLobby, 'POST', `/v1/games/${doGame}/join`, { token: doB.token, body: { profile: PROFILE } })).status, 200);
    await update(doLobby, object.lobby.env, '2'.repeat(32), effective(1, {
      routing: 'do',
      block: { kind: 'ban', until: null, reason: 'DO persisted opt-in' },
      block_devices: true,
    }));
    const doOffInstall = '7'.repeat(32);
    const doFalseInstall = '8'.repeat(32);
    const doUnbanInstall = '9'.repeat(32);
    assert.equal((await update(doLobby, object.lobby.env, doOffInstall, effective(1))).status, 200);
    assert.equal((await update(doLobby, object.lobby.env, doFalseInstall, effective(1, { block_devices: false }))).status, 200);
    assert.equal((await update(doLobby, object.lobby.env, doUnbanInstall, effective(1, {
      block: { kind: 'ban', until: null, reason: 'temporary DO ban' },
      block_devices: false,
    }))).status, 200);
    assert.equal((await update(doLobby, object.lobby.env, doUnbanInstall, effective(2, { block_devices: false }))).status, 200);
    assert.equal((await admin(doLobby, 'POST', `/v1/admin/mod/games/${doGame}/kick`, captureKick(doLobby, doGame, 1, 'persist in DO'), object.lobby.env)).body.kicked, true);

    const secondState = { storage: fakeStorage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
    const restartedObject = new LobbyObject(secondState, { ...ENV, LOG_STORE: '' });
    await restartedObject.readyPromise;
    assert.equal(restartedObject.identity.getSighting('2'.repeat(32)).signins, 1);
    assert.equal(restartedObject.identity.getEnforcement('2'.repeat(32)).routing, 'do');
    assert.equal(restartedObject.identity.getEnforcement('2'.repeat(32)).block_devices, true);
    assert.deepEqual(restartedObject.identity.getEnforcement('2'.repeat(32)).devices, ['2'.repeat(64)]);
    for (const install of [doOffInstall, doFalseInstall, doUnbanInstall]) {
      assert.equal(restartedObject.identity.getEnforcement(install).block_devices, undefined);
      assert.deepEqual(restartedObject.identity.getEnforcement(install).devices, []);
    }
    assert.equal(restartedObject.identity.getEnforcement(doUnbanInstall).block, null);
    assert.equal(restartedObject.lobby.games.get(doGame).slots[1].install, null);
    assert.equal((await call(restartedObject.lobby, 'GET', `/v1/games/${doGame}`, { token: doB.token })).status, 403);
    assert.deepEqual(await fakeStorage.get(`sighting:${'2'.repeat(32)}`), undefined);
    assert.deepEqual(await fakeStorage.get(`enforcement:${'2'.repeat(32)}`), undefined);
  } finally {
    sqlite?.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('Durable Object identity migration stays indexed, bounded, and fails closed on invalid legacy rows', async () => {
  const storage = identityStorage();
  const install = 'd'.repeat(32);
  const record = effective(1, { routing: 'migrated' });
  await storage.put(`enforcement:${install}`, { install, ...record });
  const state = { storage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
  const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
  await object.readyPromise;
  assert.equal(object.ready, true);
  assert.equal(object.lobby.enforcement.size, 0);
  assert.equal(object.lobby.sightings.size, 0);
  assert.equal(object.identity.getEnforcement(install).routing, 'migrated');

  const brokenStorage = identityStorage();
  await brokenStorage.put(`enforcement:${install}`, { install, version: 'broken' });
  const brokenState = { storage: brokenStorage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
  const broken = new LobbyObject(brokenState, { ...ENV, LOG_STORE: '' });
  await broken.readyPromise;
  assert.equal(broken.ready, false);
  const refused = await broken.fetch(new Request('https://lobby.test/v1/health'));
  assert.equal(refused.status, 503);
  assert.equal((await refused.json()).error, 'identity_migrating');
});

test('Durable Object migrates 350 legacy rows in bounded alarms and restarts from SQLite', async () => {
  const storage = identityStorage();
  const alarms = [];
  storage.setAlarm = async (time) => alarms.push(time);
  const identityPageLimits = [];
  const originalList = storage.list.bind(storage);
  storage.list = async (options) => {
    if (options.prefix === 'enforcement:' || options.prefix === 'sighting:') identityPageLimits.push(options.limit);
    return originalList(options);
  };
  const firstInstall = '0'.repeat(32);
  for (let i = 0; i < 350; i++) {
    const install = i.toString(16).padStart(32, '0');
    await storage.put(`enforcement:${install}`, { install, ...effective(1, { routing: `route-${i}` }) });
  }
  const state = { storage, blockConcurrencyWhile(fn) { this.initialization = fn(); return this.initialization; } };
  const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
  await object.readyPromise;
  assert.equal(object.ready, false);
  assert.equal(alarms.length, 1);
  const refused = await object.fetch(new Request('https://lobby.test/v1/health'));
  assert.equal(refused.status, 503);
  assert.equal((await refused.json()).error, 'identity_migrating');

  await object.alarm();
  assert.equal(object.ready, true);
  assert.ok(identityPageLimits.length > 3);
  assert.ok(identityPageLimits.every((limit) => limit === 100));
  for (const map of [
    object.lobby.enforcement,
    object.lobby.enforcementDeviceOwners,
    object.lobby.sightings,
    object.lobby.sightingDeviceOwners,
  ]) assert.equal(map.size, 0);
  assert.equal(object.identity.getEnforcement(firstInstall).routing, 'route-0');
  assert.equal((await object.fetch(new Request('https://lobby.test/v1/health'))).status, 200);

  const restartedState = { storage, blockConcurrencyWhile(fn) { this.initialization = fn(); return this.initialization; } };
  const restarted = new LobbyObject(restartedState, { ...ENV, LOG_STORE: '' });
  await restarted.readyPromise;
  assert.equal(restarted.ready, true);
  assert.equal(restarted.identity.getEnforcement('0000000000000000000000000000015d').routing, 'route-349');
  for (const map of [
    restarted.lobby.enforcement,
    restarted.lobby.enforcementDeviceOwners,
    restarted.lobby.sightings,
    restarted.lobby.sightingDeviceOwners,
  ]) assert.equal(map.size, 0);
});

test('Durable Object retries transient identity and game-list failures without opening requests', async () => {
  const identityStorageWithFailure = identityStorage();
  const identityAlarms = [];
  identityStorageWithFailure.setAlarm = async (time) => identityAlarms.push(time);
  const originalIdentityList = identityStorageWithFailure.list.bind(identityStorageWithFailure);
  let identityFailure = true;
  identityStorageWithFailure.list = async (options) => {
    if (options.prefix === 'enforcement:' && identityFailure) {
      identityFailure = false;
      throw new Error('legacy list unavailable');
    }
    return originalIdentityList(options);
  };
  const identityState = {
    storage: identityStorageWithFailure,
    blockConcurrencyWhile(fn) { this.initialization = fn(); return this.initialization; },
  };
  const identityObject = new LobbyObject(identityState, { ...ENV, LOG_STORE: '' });
  await identityObject.readyPromise;
  assert.equal(identityObject.ready, false);
  assert.equal(identityAlarms.length, 1);
  assert.equal((await identityObject.fetch(new Request('https://lobby.test/v1/games'))).status, 503);
  identityStorageWithFailure.list = originalIdentityList;
  await identityObject.alarm();
  assert.equal(identityObject.ready, true);

  for (const prefix of ['game:', 'relay-status:']) {
    const storage = identityStorage();
    const alarms = [];
    storage.setAlarm = async (time) => alarms.push(time);
    const originalList = storage.list.bind(storage);
    let failures = 2;
    storage.list = async (options) => {
      if (options.prefix === prefix && failures > 0) {
        failures -= 1;
        throw new Error('game state list unavailable');
      }
      return originalList(options);
    };
    const state = { storage, blockConcurrencyWhile(fn) { this.initialization = fn(); return this.initialization; } };
    const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
    await object.readyPromise;
    assert.equal(object.ready, false, `${prefix} startup failure must keep requests closed`);
    assert.equal(alarms.length, 1);
    assert.equal((await object.fetch(new Request('https://lobby.test/v1/games'))).status, 503);
    await object.alarm();
    assert.equal(object.ready, false, `${prefix} alarm failure must not open requests`);
    assert.equal(alarms.length, 2);
    assert.equal((await object.fetch(new Request('https://lobby.test/v1/games'))).status, 503);
    storage.list = originalList;
    await object.alarm();
    assert.equal(object.ready, true, `${prefix} should recover after repair`);
  }
});

test('Durable Object alarm waits for initialization', async () => {
  const storage = identityStorage();
  let release;
  const initializationGate = new Promise((resolve) => { release = resolve; });
  const state = {
    storage,
    blockConcurrencyWhile(fn) {
      const initialization = fn();
      return initializationGate.then(() => initialization);
    },
  };
  const object = new LobbyObject(state, { ...ENV, LOG_STORE: '' });
  let alarmFinished = false;
  const alarm = object.alarm().then(() => { alarmFinished = true; });
  await Promise.resolve();
  assert.equal(alarmFinished, false);
  release();
  await alarm;
  assert.equal(object.ready, true);
});

test('50k bounded sightings search is deterministic for ties and returns opaque cursors', () => {
  const lobby = new Lobby(fakeEnv(), configFromEnv(ENV));
  const rows = Array.from({ length: 50000 }, (_, index) => ({
    install: index.toString(16).padStart(32, '0'),
    nicks: [{ nick: index === 49999 ? 'Alex' : `Nick${index}`, first_seen: 1_700_000_000, last_seen: 1_700_000_000 }],
    first_seen: 1_700_000_000,
    last_seen: 1_700_000_000,
    signins: 1,
    platform: null,
    version: null,
    devices: [],
  }));
  lobby.load([], [], [], rows);
  const start = performance.now();
  const page = lobby.adminPlayers('', null, 50);
  const elapsed = performance.now() - start;
  assert.ok(elapsed < 200, `search took ${elapsed.toFixed(1)} ms`);
  assert.ok(page.next);
  assert.match(page.next, /^[A-Za-z0-9_-]+$/);
  const second = lobby.adminPlayers('', page.next, 50);
  assert.notEqual(page.players.at(-1).install, second.players[0].install);
  assert.equal(lobby.adminPlayers('alex', null, 50).players[0].nick, 'Alex');
});

test('ONLINEMENU-1 (owner 2026-10-20): a ban blocks the hardware hash by default; block_devices: false skips it; a suspension does not', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const device = '7'.repeat(64);
  await guest(lobby, 'Cheater', 'a'.repeat(32), device);
  await guest(lobby, 'Spared', 'b'.repeat(32), '8'.repeat(64));
  await guest(lobby, 'Suspended', 'c'.repeat(32), '9'.repeat(64));
  const ban = (version, extra = {}) => ({ v: 1, version, block: { kind: 'ban', until: null, reason: 'cheating' }, mute_until: null, routing: '', notice: null, devices: [], ...extra });
  assert.equal((await update(lobby, env, 'a'.repeat(32), ban(1))).status, 200);
  assert.equal(lobby.getEnforcement('a'.repeat(32)).block_devices, true);
  assert.deepEqual(lobby.getEnforcement('a'.repeat(32)).devices, [device]);
  // a new install on the same machine is refused
  assert.equal((await guest(lobby, 'Fresh install', 'd'.repeat(32), device)).status, 403);
  // the console's checkbox off: the install only
  assert.equal((await update(lobby, env, 'b'.repeat(32), ban(1, { block_devices: false }))).status, 200);
  assert.deepEqual(lobby.getEnforcement('b'.repeat(32)).devices, []);
  assert.equal((await guest(lobby, 'Same machine', 'e'.repeat(32), '8'.repeat(64))).status, 200);
  // a suspension never blocks devices
  const suspend = { ...ban(1), block: { kind: 'suspend', until: null, reason: 'cool down' } };
  assert.equal((await update(lobby, env, 'c'.repeat(32), suspend)).status, 200);
  assert.equal(lobby.getEnforcement('c'.repeat(32)).block_devices, undefined);
  assert.equal((await guest(lobby, 'Other install', 'f'.repeat(32), '9'.repeat(64))).status, 200);
});

test('block_devices snapshots only the target install, stays frozen, and restores from persistence', async () => {
  const env = fakeEnv();
  const lobby = new Lobby(env, configFromEnv(ENV));
  const installA = 'a'.repeat(32);
  const installB = 'b'.repeat(32);
  const deviceA = '1'.repeat(64);
  const deviceB = '2'.repeat(64);
  const deviceC = '3'.repeat(64);
  await guest(lobby, 'A', installA, deviceA);
  await guest(lobby, 'B', installB, deviceB);
  assert.equal((await guest(lobby, 'Before', 'c'.repeat(32), deviceA)).status, 200, 'sighting alone does not block');

  const optIn = {
    v: 1,
    version: 1,
    block: { kind: 'ban', until: null, reason: 'targeted ban' },
    mute_until: null,
    routing: '',
    notice: null,
    block_devices: true,
  };
  assert.equal((await update(lobby, env, installA, optIn)).body.updated, true);
  assert.deepEqual(env.persisted.enforcement.get(installA).devices, [deviceA]);
  assert.equal(env.persisted.enforcement.get(installA).block_devices, true);
  assert.deepEqual((await update(lobby, env, installA, { ...optIn, devices: [] })).body, { v: 1, unchanged: true });
  for (const invalidDevices of [null, 'not-an-array', {}, 42]) {
    assert.equal((await update(lobby, env, installA, { ...optIn, devices: invalidDevices })).status, 400);
  }
  assert.equal((await guest(lobby, 'Unrelated', 'd'.repeat(32), deviceB)).status, 200);
  assert.equal((await guest(lobby, 'Targeted', 'e'.repeat(32), deviceA)).body.error, 'banned');

  const changedSighting = structuredClone(lobby.sightings.get(installA));
  changedSighting.devices = [{
    hash: deviceC,
    first_seen: changedSighting.last_seen,
    last_seen: changedSighting.last_seen,
    count: 1,
  }];
  lobby.sightings.set(installA, changedSighting);
  assert.deepEqual((await update(lobby, env, installA, optIn)).body, { v: 1, unchanged: true });
  assert.deepEqual(lobby.enforcement.get(installA).devices, [deviceA], 'same-version retry reuses its frozen snapshot');
  assert.equal((await update(lobby, env, installA, {
    ...optIn, block: { kind: 'ban', until: null, reason: 'changed' },
  })).status, 409);
  for (const invalid of [
    effective(2, { block: { kind: 'suspend', until: null, reason: 'bad' }, block_devices: true }),
    effective(2, { block: { kind: 'ban', until: null, reason: 'bad' }, block_devices: 'true' }),
    effective(2, { block: { kind: 'ban', until: null, reason: 'bad' }, block_devices: true, devices: [deviceB] }),
    { ...effective(2), resolved_devices: [deviceA] },
  ]) assert.equal((await update(lobby, env, installA, invalid)).status, 400);

  const saveEnforcement = env.store.saveEnforcement;
  env.store.saveEnforcement = async () => { throw new Error('simulated enforcement persistence failure'); };
  assert.equal((await update(lobby, env, installA, effective(2, { devices: [deviceB] }))).status, 500);
  assert.equal(lobby.enforcement.get(installA).version, 1);
  assert.deepEqual(lobby.enforcement.get(installA).devices, [deviceA]);
  assert.ok(lobby.enforcementDeviceOwners.get(deviceA).has(installA));
  assert.ok(!lobby.enforcementDeviceOwners.get(deviceB)?.has(installA));
  env.store.saveEnforcement = saveEnforcement;

  const restarted = new Lobby(env, configFromEnv(ENV));
  restarted.load(
    [...env.persisted.games.values()],
    [],
    [...env.persisted.enforcement.values()],
    [...env.persisted.sightings.values()],
  );
  assert.equal(restarted.enforcement.get(installA).block_devices, true);
  assert.deepEqual(restarted.enforcement.get(installA).devices, [deviceA]);

  assert.equal((await update(lobby, env, installA, effective(2, {
    block: { kind: 'ban', until: null, reason: 'account only' },
    devices: [],
    block_devices: false,
  }))).body.updated, true);
  assert.equal(lobby.enforcement.get(installA).block_devices, undefined);
  assert.deepEqual(lobby.enforcement.get(installA).devices, []);
  assert.equal((await guest(lobby, 'Signal only', 'f'.repeat(32), deviceA)).status, 200);
});

test('player report routes require identity, emit exact receipts, and keep moderator data private', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'Reporter', '6'.repeat(32))).body;
  const target = (await guest(lobby, 'Target', '7'.repeat(32))).body;
  const outsider = (await guest(lobby, 'Outsider', '8'.repeat(32))).body;
  const game = await createGame(lobby, host.token);
  assert.equal((await call(lobby, 'POST', `/v1/games/${game}/join`, {
    token: target.token, body: { profile: PROFILE },
  })).status, 200);
  const hostAccount = await lobby.account(`Bearer ${host.token}`);
  const targetAccount = await lobby.account(`Bearer ${target.token}`);
  const report = { v: 1, game_id: game, target: targetAccount.id, reason: 'other: A sufficiently detailed report.' };
  const postPath = '/v1/reports';
  const anonymous = await call(lobby, 'POST', postPath, {
    body: report, headers: { 'idempotency-key': 'route-key-001' },
  });
  assert.equal(anonymous.status, 401);
  assert.equal(anonymous.body.error.code, 'signed_out');
  assert.match(anonymous.body.error.request_id, /^[0-9a-f]{24}$/);
  assert.equal(reportStore.data.has(`attempts:${'6'.repeat(32)}`), false, 'unauthenticated attempts are not charged');

  const legacy = (await guest(lobby, 'Legacy', null)).body;
  const noInstall = await call(lobby, 'POST', postPath, {
    token: legacy.token, body: report, headers: { 'idempotency-key': 'legacy-key-001' },
  });
  assert.equal(noInstall.status, 401);
  assert.equal(noInstall.body.error.code, 'install_required');
  assert.equal(reportStore.data.has(`attempts:${'6'.repeat(32)}`), false, 'legacy no-install tokens are not charged');
  const oversizedLegacy = await call(lobby, 'POST', postPath, {
    token: legacy.token, bodyRejection: { status: 413, code: 'too_big', message: 'request body too large' },
  });
  assert.equal(oversizedLegacy.status, 401);
  assert.equal(oversizedLegacy.body.error.code, 'install_required');
  assert.equal(reportStore.data.has('attempts:undefined'), false);

  const accepted = await call(lobby, 'POST', postPath, {
    token: host.token, body: report, headers: { 'idempotency-key': 'route-key-001' },
  });
  assert.equal(accepted.status, 201, JSON.stringify(accepted.body));
  assert.deepEqual(Object.keys(accepted.body).sort(), ['received_at', 'report_id', 'v']);
  const replay = await call(lobby, 'POST', postPath, {
    token: host.token, body: report, headers: { 'idempotency-key': 'route-key-001' },
  });
  assert.deepEqual(replay.body, accepted.body);
  assert.equal(replay.headers['idempotent-replay'], 'true');
  assert.equal(replay.headers['idempotency-replay'], undefined);

  const validCases = [
    [{ ...report, unknown: true }, 'unknown_field'],
    [{ ...report, target: hostAccount.id }, 'self_report'],
    [{ ...report, target: 'guest:unknown' }, 'no_player'],
  ];
  for (const [body, code] of validCases) {
    env.advance(3601);
    const result = await call(lobby, 'POST', postPath, {
      token: host.token, body, headers: { 'idempotency-key': `route-${code}-001` },
    });
    assert.equal(result.body.error.code, code);
    assert.equal(result.body.v, 1);
    assert.ok(result.body.error.request_id);
    assert.notEqual(result.body.error.request_id, anonymous.body.error.request_id);
  }
  env.advance(3601);
  const malformed = await call(lobby, 'POST', postPath, {
    token: host.token, raw: encoder.encode('{'), headers: { 'idempotency-key': 'malformed-key-01' },
  });
  assert.equal(malformed.status, 400);
  assert.equal(malformed.body.error.code, 'bad_json');
  env.advance(3601);
  const missingKey = await call(lobby, 'POST', postPath, { token: host.token, body: report });
  assert.equal(missingKey.status, 400);
  assert.equal(missingKey.body.error.code, 'missing_field');
  env.advance(3601);
  const badKey = await call(lobby, 'POST', postPath, {
    token: host.token, body: report, headers: { 'idempotency-key': 'bad key' },
  });
  assert.equal(badKey.status, 400);
  assert.equal(badKey.body.error.code, 'bad_field');

  const outsiderResponse = await call(lobby, 'POST', postPath, {
    token: outsider.token, body: report, headers: { 'idempotency-key': 'outsider-key-001' },
  });
  assert.equal(outsiderResponse.status, 403);
  assert.equal(outsiderResponse.body.error.code, 'not_in_game');

  for (let index = 0; index < 5; index++) {
    const quotaAttempt = await call(lobby, 'POST', postPath, {
      token: target.token,
      body: { ...report, target: hostAccount.id, extra: true },
      headers: { 'idempotency-key': `quota-key-${String(index).padStart(3, '0')}` },
    });
    assert.equal(quotaAttempt.status, 400);
  }
  const limited = await call(lobby, 'POST', postPath, {
    token: target.token,
    body: { ...report, target: hostAccount.id, extra: true },
    headers: { 'idempotency-key': 'quota-key-005' },
  });
  assert.equal(limited.status, 429);
  assert.equal(limited.body.error.code, 'rate_limited');
  assert.ok(Number(limited.headers['retry-after']) > 0);

  const beforeOversized = reportStore.data.get(`attempts:${hostAccount.install}`).length;
  env.advance(3601);
  const oversized = await call(lobby, 'POST', postPath, {
    token: host.token,
    bodyRejection: { status: 413, code: 'too_big', message: 'request body too large' },
  });
  assert.equal(oversized.status, 413);
  assert.equal(reportStore.data.get(`attempts:${hostAccount.install}`).length, beforeOversized + 1);

  const listPath = '/v1/admin/mod/reports';
  const unauthenticatedList = await call(lobby, 'GET', listPath);
  assert.equal(unauthenticatedList.status, 401);
  assert.ok(unauthenticatedList.body.error.request_id);
  assert.equal((await call(lobby, 'GET', listPath, { token: host.token })).status, 401);
  assert.equal((await admin(lobby, 'GET', listPath, undefined, env, { key: ADMIN_KEY })).status, 401);
  assert.equal((await admin(lobby, 'GET', listPath, undefined, env, {
    time: Math.floor(env.now() / 1000) - 301,
  })).status, 401);
  const queryPath = `${listPath}?limit=1`;
  const tamperedQuery = await call(lobby, 'GET', `${listPath}?limit=2`, {
    headers: adminHeader(env, 'GET', queryPath, new Uint8Array()),
  });
  assert.equal(tamperedQuery.status, 401);
  const badMac = adminHeader(env, 'GET', listPath, new Uint8Array());
  const [time, mac] = badMac['x-openbfme-admin'].split('.');
  badMac['x-openbfme-admin'] = `${time}.${mac[0] === '0' ? '1' : '0'}${mac.slice(1)}`;
  assert.equal((await call(lobby, 'GET', listPath, { headers: badMac })).status, 401);
  const listed = await admin(lobby, 'GET', listPath, undefined, env);
  assert.equal(listed.status, 200, JSON.stringify(listed.body));
  assert.equal(listed.body.items.length, 1);
  assert.equal(listed.body.items[0].reporter.install, hostAccount.install);
  assert.equal(listed.body.items[0].target.install, targetAccount.install);
  const detailPath = `${listPath}/${accepted.body.report_id}`;
  const unauthenticatedDetail = await call(lobby, 'GET', detailPath);
  assert.equal(unauthenticatedDetail.status, 401);
  assert.ok(unauthenticatedDetail.body.error.request_id);
  assert.equal((await admin(lobby, 'GET', detailPath, undefined, env)).status, 200);
  const missing = await admin(lobby, 'GET', `${listPath}/R-${'0'.repeat(24)}`, undefined, env);
  assert.equal(missing.status, 404);
  assert.equal(missing.body.error.code, 'no_report');

  const noModEnv = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const noModLobby = new Lobby(noModEnv, configFromEnv({ ...ENV, MOD_KEY: '' }));
  const missingModKey = await call(noModLobby, 'GET', listPath, {
    headers: adminHeader(env, 'GET', listPath, new Uint8Array()),
  });
  assert.equal(missingModKey.status, 501);
  assert.ok(missingModKey.body.error.request_id);

  assert.equal((await update(lobby, env, hostAccount.install,
    effective(1, { block: { kind: 'ban', until: null, reason: 'account restricted' } }))).status, 200);
  const banned = await call(lobby, 'POST', postPath, {
    token: host.token, body: { ...report, reason: 'other: after restriction' },
    headers: { 'idempotency-key': 'banned-route-key' },
  });
  assert.equal(banned.status, 403);
  assert.equal(banned.body.error.code, 'banned');
  assert.match(banned.body.error.message, /account restricted/);
  assert.equal('appeal' in banned.body, false);
});

test('failed report-roster writes roll back join and game-end state', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'Lifecycle host', '9'.repeat(32))).body;
  const joiner = (await guest(lobby, 'Lifecycle joiner', '0'.repeat(32))).body;
  const game = await createGame(lobby, host.token);
  const before = structuredClone(lobby.games.get(game));
  reportStore.failWrites(true);
  const join = await call(lobby, 'POST', `/v1/games/${game}/join`, {
    token: joiner.token, body: { profile: PROFILE },
  });
  assert.equal(join.status, 503);
  assert.deepEqual(lobby.games.get(game).slots, before.slots);
  assert.deepEqual(lobby.games.get(game).reportParticipants, before.reportParticipants);
  assert.deepEqual(reportStore.data.get(`roster:${game}`), {
    game_id: game,
    participants: before.reportParticipants,
    active: [before.slots[before.hostSlot].account],
    state: 'open',
    ended_at: null,
  });

  const gameBeforeEnd = structuredClone(lobby.games.get(game));
  await assert.rejects(lobby.markFinished(game), /game end could not be recorded|report end|report_store/i);
  assert.equal(lobby.games.get(game).finished, gameBeforeEnd.finished);
  assert.equal(lobby.games.get(game).reportEndedAt, gameBeforeEnd.reportEndedAt);
  assert.equal(reportStore.data.get(`roster:${game}`).ended_at, null);
  await assert.rejects(lobby.adminCloseGame(game, 'cannot persist close', lobby.games.get(game).seq), /game end could not be recorded|report end|report_store/i);
  assert.equal(lobby.games.get(game).state, gameBeforeEnd.state);
  assert.equal(lobby.games.get(game).reportEndedAt, gameBeforeEnd.reportEndedAt);
  assert.equal(reportStore.data.get(`roster:${game}`).ended_at, null);
});

test('failed report-roster writes do not expose joins or resolve held polls', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'Staged join host', '1'.repeat(32))).body;
  const joiner = (await guest(lobby, 'Staged joiner', '2'.repeat(32))).body;
  const hostAccount = await lobby.account(`Bearer ${host.token}`);
  const joinerAccount = await lobby.account(`Bearer ${joiner.token}`);
  const game = await createGame(lobby, host.token);
  const seq = lobby.games.get(game).seq;
  const held = await route(lobby, {
    method: 'GET',
    path: `/v1/games/${game}`,
    query: new URLSearchParams(`since=${seq}&seq=${seq}&wait=20`),
    headers: { authorization: `Bearer ${host.token}` },
    bytes: async () => new Uint8Array(),
  });
  assert.ok(held.deferred);
  let answer = null;
  held.deferred.then((result) => { answer = JSON.parse(result.body); });

  let transactionStarted;
  const started = new Promise((resolve) => { transactionStarted = resolve; });
  let releaseFailure;
  const blocked = new Promise((resolve) => { releaseFailure = resolve; });
  const originalTransaction = env.store.reportTransaction;
  env.store.reportTransaction = (fn) => originalTransaction(async (tx) => {
    const result = await fn(tx);
    transactionStarted();
    await blocked;
    throw new Error('simulated report storage failure');
  });
  try {
    const joining = lobby.join(joinerAccount, game, { profile: PROFILE });
    await started;
    const during = lobby.poll(hostAccount, game, seq);
    assert.equal(during.game.slots[1].state, 'open');
    assert.ok(!during.chat.some((line) => line.text === 'Staged joiner joined'));
    releaseFailure();
    await assert.rejects(joining, /the game roster could not be saved/);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(answer, null);
    assert.equal(lobby.waiterCount, 1);
    env.store.reportTransaction = originalTransaction;
    lobby.wake(game);
    const result = await held.deferred;
    const body = JSON.parse(result.body);
    assert.equal(body.game.slots[1].state, 'open');
    assert.ok(!body.chat.some((line) => line.text === 'Staged joiner joined'));
  } finally {
    releaseFailure();
    env.store.reportTransaction = originalTransaction;
    lobby.wake(game);
  }
});

test('failed report-roster start leaves the room open without activated tickets or sequence changes', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'Staged start host', '3'.repeat(32))).body;
  const joiner = (await guest(lobby, 'Staged start joiner', '4'.repeat(32))).body;
  const game = await createGame(lobby, host.token);
  assert.equal((await call(lobby, 'POST', `/v1/games/${game}/join`, {
    token: joiner.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await call(lobby, 'POST', `/v1/games/${game}/ready`, {
    token: joiner.token, body: { ready: true },
  })).status, 200);
  const before = structuredClone(lobby.games.get(game));
  reportStore.failWrites(true);
  await assert.rejects(lobby.start(await lobby.account(`Bearer ${host.token}`), game), /the game roster could not be saved/);
  const after = lobby.games.get(game);
  assert.equal(after.state, 'open');
  assert.equal(after.starts, undefined);
  assert.equal(after.mask, undefined);
  assert.equal(after.startedAt, undefined);
  assert.equal(after.seq, before.seq);
  assert.ok(!after.chat.some((line) => line.text === 'the game started'));
  assert.ok(!env.persisted.blobs.has(`starts/${game}.json`));
});

test('Durable Object report storage survives restart and pages descending with sparse expired rows', async () => {
  const values = new Map();
  let queue = Promise.resolve();
  let failCommit = false;
  const listCalls = [];
  const storage = {
    transaction(fn) {
      const run = queue.then(async () => {
        const draft = new Map([...values].map(([key, value]) => [key, structuredClone(value)]));
        const transaction = {
          get: async (key) => draft.has(key) ? structuredClone(draft.get(key)) : undefined,
          put: async (key, value) => { draft.set(key, structuredClone(value)); },
          delete: async (key) => { draft.delete(key); },
          list: async (options) => {
            listCalls.push(structuredClone(options));
            let keys = [...draft.keys()].filter((key) => key.startsWith(options.prefix ?? ''));
            if (options.start !== undefined) keys = keys.filter((key) => key >= options.start);
            if (options.startAfter !== undefined) keys = keys.filter((key) => key > options.startAfter);
            if (options.end !== undefined) keys = keys.filter((key) => key < options.end);
            keys.sort();
            if (options.reverse) keys.reverse();
            return new Map(keys.slice(0, options.limit).map((key) => [key, structuredClone(draft.get(key))]));
          },
        };
        const result = await fn(transaction);
        if (failCommit) throw new Error('simulated Durable Object transaction failure');
        values.clear();
        for (const [key, value] of draft) values.set(key, value);
        return result;
      });
      queue = run.catch(() => {});
      return run;
    },
  };
  const nowSeconds = Math.floor(envTime());
  let random = 0;
  const randomBytes = (length) => new Uint8Array(length).fill(++random);
  const participants = [
    { account: 'guest:do-a', install: 'a'.repeat(32), nick: 'A' },
    { account: 'guest:do-b', install: 'b'.repeat(32), nick: 'B' },
    { account: 'guest:do-c', install: 'c'.repeat(32), nick: 'C' },
    { account: 'guest:do-d', install: 'd'.repeat(32), nick: 'D' },
  ];
  const store = { reportTransaction: durableReportTransaction(storage) };
  await store.reportTransaction((tx) => tx.put('roster:0123456789abcdef', {
    game_id: '0123456789abcdef',
    participants,
    active: [],
    state: 'finished',
    ended_at: nowSeconds - 60,
  }));
  const submit = (reporter, target, key) => submitPlayerReport({
    store,
    now: () => nowSeconds * 1000,
    randomBytes,
    account: { ...reporter, },
    rawBody: encoder.encode(JSON.stringify({
      v: 1, game_id: '0123456789abcdef', target: target.account, reason: 'other: durable report',
    })),
    idempotencyKey: key,
    getLiveGame: () => null,
  });
  const first = await submit({ id: 'guest:do-a', install: 'a'.repeat(32), name: 'A' }, participants[1], 'do-report-a');
  const second = await submit({ id: 'guest:do-c', install: 'c'.repeat(32), name: 'C' }, participants[3], 'do-report-c');
  failCommit = true;
  await assert.rejects(
    submit({ id: 'guest:do-b', install: 'b'.repeat(32), name: 'B' }, participants[0], 'do-report-b'),
    /simulated Durable Object transaction failure/,
  );
  failCommit = false;
  assert.equal(values.has(`attempts:${'b'.repeat(32)}`), false);
  assert.equal([...values.keys()].filter((key) => key.startsWith('report:')).length, 2);
  const restartedStore = { reportTransaction: durableReportTransaction(storage) };
  const replay = await submitPlayerReport({
    store: restartedStore,
    now: () => nowSeconds * 1000,
    randomBytes,
    account: { id: 'guest:do-a', install: 'a'.repeat(32), name: 'A' },
    rawBody: encoder.encode(JSON.stringify({
      v: 1, game_id: '0123456789abcdef', target: participants[1].account, reason: 'other: durable report',
    })),
    idempotencyKey: 'do-report-a',
    getLiveGame: () => null,
  });
  assert.equal(replay.replay, true);
  assert.deepEqual(replay.receipt, first.receipt);
  assert.equal((await restartedStore.reportTransaction((tx) => tx.get('attempts:' + 'a'.repeat(32))).then((value) => value.length)), 1);
  assert.ok(values.has('roster:0123456789abcdef'));
  assert.ok((await readPlayerReport({ store: restartedStore, now: () => nowSeconds * 1000, reportId: second.receipt.report_id })).report);

  const env = fakeEnv();
  const modKey = Buffer.from(MOD_KEY, 'hex');
  const hmac = async (key, data) => new Uint8Array(createHmac('sha256', key).update(data).digest());
  await restartedStore.reportTransaction(async (tx) => {
    for (const [index, id] of ['f', 'e'].map((char, index) => [index, `R-${char.repeat(24)}`])) {
      await tx.put(`index:${String(nowSeconds).padStart(16, '0')}:${id}`, { id, expires_at: nowSeconds - 1 - index });
    }
  });
  const sparse = await readPlayerReports({
    store: restartedStore, now: () => nowSeconds * 1000, hmac, modKey, limit: 2,
  });
  assert.deepEqual(sparse.items, []);
  assert.ok(sparse.next_cursor);
  const continued = await readPlayerReports({
    store: restartedStore, now: () => nowSeconds * 1000, hmac, modKey, after: sparse.next_cursor, limit: 2,
  });
  assert.deepEqual(continued.items.map((item) => item.id), [second.receipt.report_id, first.receipt.report_id].sort().reverse());
  const reverseCall = listCalls.find((options) => options.reverse && options.end);
  assert.ok(reverseCall, 'reverse cursor uses documented end-exclusive range');
  assert.equal(reverseCall.startAfter, undefined);
  await assert.rejects(readPlayerReports({
    store: restartedStore, now: () => nowSeconds * 1000, hmac, modKey,
    after: `${sparse.next_cursor.slice(0, -1)}${sparse.next_cursor.endsWith('0') ? '1' : '0'}`, limit: 2,
  }), (error) => error.status === 400);
  const cleanup = await cleanupReportData(restartedStore, nowSeconds, { index: 0, after: null });
  assert.equal(cleanup.index, 1);
  assert.equal(values.has(`index:${String(nowSeconds).padStart(16, '0')}:R-${'f'.repeat(24)}`), false);
  assert.equal(values.has(`index:${String(nowSeconds).padStart(16, '0')}:R-${'e'.repeat(24)}`), false);
});

test('closed, idle, and signed-finished games retain departed report targets for 24 hours', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const pair = async (label, suffix) => {
    const host = (await guest(lobby, `${label} host`, suffix.repeat(32))).body;
    const target = (await guest(lobby, `${label} target`, (Number.parseInt(suffix, 16) + 1).toString(16).repeat(32))).body;
    const game = await createGame(lobby, host.token, label);
    assert.equal((await call(lobby, 'POST', `/v1/games/${game}/join`, {
      token: target.token, body: { profile: PROFILE },
    })).status, 200);
    return { host, target, game };
  };
  const report = async ({ host, target, game }, key) => call(lobby, 'POST', '/v1/reports', {
    token: host.token,
    body: {
      v: 1,
      game_id: game,
      target: (await lobby.account(`Bearer ${target.token}`)).id,
      reason: 'other: The other participant left before the report.',
    },
    headers: { 'idempotency-key': key },
  });

  const closed = await pair('closed', '1');
  const targetAccount = await lobby.account(`Bearer ${closed.target.token}`);
  assert.equal((await call(lobby, 'POST', `/v1/games/${closed.game}/leave`, { token: closed.target.token })).body.left, true);
  assert.equal((await admin(lobby, 'POST', `/v1/admin/mod/games/${closed.game}/close`, captureClose(lobby, closed.game, 'room closed'), env)).body.closed, true);
  const afterClose = await report(closed, 'closed-report-001');
  assert.equal(afterClose.status, 201, JSON.stringify(afterClose.body));
  const closedSource = reportStore.data.get(`report:${afterClose.body.report_id}`);
  assert.deepEqual(closedSource.target, { install: targetAccount.install, nick: targetAccount.name });
  env.advance(24 * 60 * 60 + 1);
  const expired = await report(closed, 'closed-report-002');
  assert.equal(expired.status, 404);
  assert.equal(expired.body.error.code, 'report_expired');

  const idle = await pair('idle', '3');
  env.advance(Math.ceil(lobby.config.openGameIdleMs / 1000) + 1);
  const afterIdle = await report(idle, 'idle-report-001');
  assert.equal(afterIdle.status, 201, JSON.stringify(afterIdle.body));
  assert.ok(reportStore.data.get(`roster:${idle.game}`).ended_at);

  const finished = await pair('finished', '5');
  await lobby.markFinished(finished.game);
  assert.equal(reportStore.data.get(`roster:${finished.game}`).state, 'finished');
  const afterFinished = await report(finished, 'finished-report-001');
  assert.equal(afterFinished.status, 201, JSON.stringify(afterFinished.body));
});

test('participant history refuses a 257th identity without mutating the open slot', async () => {
  const reportStore = memoryReportStore();
  const env = fakeEnv({ reportTransaction: reportStore.reportTransaction.bind(reportStore) });
  const lobby = new Lobby(env, configFromEnv(ENV));
  const host = (await guest(lobby, 'History host', 'a'.repeat(32))).body;
  const joiner = (await guest(lobby, 'History joiner', 'b'.repeat(32))).body;
  const game = await createGame(lobby, host.token);
  const record = lobby.games.get(game);
  record.reportParticipants = [
    ...record.reportParticipants,
    ...Array.from({ length: 255 }, (_, index) => ({
      account: `guest:departed:${index}`,
      install: null,
      nick: `Departed${index}`,
    })),
  ];
  const beforeSlots = structuredClone(record.slots);
  const overflow = await call(lobby, 'POST', `/v1/games/${game}/join`, {
    token: joiner.token, body: { profile: PROFILE },
  });
  assert.equal(overflow.status, 409);
  assert.equal(overflow.body.error, 'roster_full');
  assert.deepEqual(lobby.games.get(game).slots, beforeSlots);
  assert.equal(lobby.games.get(game).reportParticipants.length, 256);
});

function envTime() {
  return 1_700_000_000;
}
