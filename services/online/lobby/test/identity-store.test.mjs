import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { performance } from 'node:perf_hooks';
import { SqliteIdentityStore, SIGHTING_RETENTION_S } from '../src/identity-store.mjs';
import { identityStorage } from './identity-storage.mjs';
import { validateSighting } from '../src/lobby.mjs';

const NOW = 1_700_000_000;
const install = (i) => i.toString(16).padStart(32, '0');
const device = (i) => i.toString(16).padStart(64, '0');

test('text repairs merge legacy nickname collisions and rebuild a single searchable index entry', async () => {
  const storage = identityStorage();
  try {
    const index = new SqliteIdentityStore(storage);
    const raw = sighting(1);
    raw.nicks = [
      { nick: '\ud800x', first_seen: NOW - 10, last_seen: NOW - 2 },
      { nick: '\udc00x', first_seen: NOW - 5, last_seen: NOW },
      { nick: '�x', first_seen: NOW - 4, last_seen: NOW - 1 },
    ];
    storage.sql.exec('INSERT INTO identity_sightings VALUES (?, ?, ?)', raw.install, raw.last_seen, JSON.stringify(raw));
    while (!await index.migratePage((record) => record, validateSighting)) {}
    assert.deepEqual(index.getSighting(raw.install).nicks, [{ nick: '�x', first_seen: NOW - 10, last_seen: NOW }]);
    assert.equal(index.players('�x', null, 51, NOW).length, 1);
    assert.deepEqual(storage.sql.exec('SELECT nick FROM identity_nicks WHERE install = ?', raw.install).toArray().map((row) => row.nick), ['�x']);
    assert.equal(validateSighting({ ...raw, nicks: [raw.nicks[0], raw.nicks[0]] }).nicks.length, 1);
    assert.throws(() => validateSighting({ ...raw, nicks: [raw.nicks[2], raw.nicks[2]] }), /nicks must be unique/);
  } finally { storage.db.close(); }
});
function effective(i, version = 1) {
  return { install: install(i), v: 1, version, block: { kind: 'ban', until: null, reason: 'Capacity fixture' },
    mute_until: null, routing: '', notice: null, devices: [device(0), ...Array.from({ length: 4 }, (_, j) => device(i * 4 + j + 1))] };
}
function sighting(i) {
  return { install: install(i), first_seen: NOW - 10, last_seen: NOW, signins: 10, platform: 'linux', version: 'fixture',
    nicks: Array.from({ length: 10 }, (_, j) => ({ nick: `Player${i}-${j}`, first_seen: NOW - 10, last_seen: NOW })),
    devices: effective(i).devices.map((hash) => ({ hash, count: 2, first_seen: NOW - 10, last_seen: NOW })) };
}

test('identity indexes replace atomically, expire blocks, and keep bounded pages across reopen', () => {
  const storage = identityStorage();
  const index = new SqliteIdentityStore(storage);
  index.saveSighting(sighting(1));
  index.saveSighting(sighting(2));
  index.saveEnforcement(effective(1));
  const before = index.getEnforcement(install(1));
  const exec = storage.sql.exec;
  storage.sql.exec = (query, ...args) => {
    if (query.startsWith('INSERT INTO identity_blocks')) throw new Error('injected write failure');
    return exec(query, ...args);
  };
  assert.throws(() => index.saveEnforcement(effective(1, 2)), /injected/);
  storage.sql.exec = exec;
  assert.deepEqual(index.getEnforcement(install(1)), before);
  assert.equal(index.deviceBlock(device(0), NOW).kind, 'ban');
  index.saveEnforcement({ ...effective(1, 2), block: { kind: 'suspend', until: NOW + 1, reason: 'short' } });
  assert.equal(index.deviceBlock(device(0), NOW).kind, 'suspend');
  assert.equal(index.deviceBlock(device(0), NOW + 1), null);
  assert.equal(index.players('player1', null, 51, NOW).length, 1);
  assert.equal(index.linked(install(1), NOW)[0].shared_devices, 1);
  index.saveEnforcement({ ...effective(1, 3), block: null, devices: [] });
  const reopened = new SqliteIdentityStore(storage);
  assert.equal(reopened.getEnforcement(install(1)).version, 3);
  assert.equal(reopened.deviceBlock(device(0), NOW), null);
  assert.equal(reopened.cleanupSightings(NOW + SIGHTING_RETENTION_S + 1), 2);
  assert.equal(reopened.getSighting(install(1)), undefined);
  assert.equal(storage.db.prepare('SELECT COUNT(*) AS n FROM identity_devices').get().n, 0);
  storage.db.close();
});

// DEVINPR-1 (review of WP-C): a repeated sign-in writes its sighting row and nothing else; a changed nick or device touches only its own rows
test('a repeated sign-in rewrites only the rows that changed', () => {
  const storage = identityStorage();
  const index = new SqliteIdentityStore(storage);
  index.saveSighting(sighting(1));
  index.saveSighting(sighting(2));
  const exec = storage.sql.exec;
  let writes = [];
  storage.sql.exec = (query, ...args) => {
    if (!/^\s*SELECT/i.test(query)) writes.push(query.split(' ').slice(0, 3).join(' '));
    return exec(query, ...args);
  };
  index.saveSighting({ ...sighting(1), last_seen: NOW + 60, signins: 11 });
  assert.deepEqual(writes, ['INSERT OR REPLACE']);
  writes = [];
  const changed = sighting(1);
  changed.nicks = [{ nick: 'Renamed', first_seen: NOW, last_seen: NOW + 120 }, ...changed.nicks.slice(0, 9)];
  changed.devices = [{ hash: device(99), count: 1, first_seen: NOW, last_seen: NOW + 120 }, ...changed.devices.slice(0, 4)];
  index.saveSighting({ ...changed, last_seen: NOW + 120 });
  assert.deepEqual(writes, ['INSERT OR REPLACE', 'DELETE FROM identity_devices', 'INSERT INTO identity_devices', 'DELETE FROM identity_nicks', 'INSERT INTO identity_nicks']);
  storage.sql.exec = exec;
  const nicks = storage.db.prepare('SELECT nick FROM identity_nicks WHERE install = ? ORDER BY nick').all(install(1)).map(({ nick }) => nick);
  assert.deepEqual(nicks, [...changed.nicks.map(({ nick }) => nick.toLowerCase())].sort());
  const devices = storage.db.prepare('SELECT device FROM identity_devices WHERE install = ? ORDER BY device').all(install(1)).map(({ device: d }) => d);
  assert.deepEqual(devices, changed.devices.map(({ hash }) => hash).sort());
  assert.equal(index.players('renamed', null, 10, NOW + 120).length, 1);
  assert.equal(index.players('player1-9', null, 10, NOW + 120).length, 0);
  assert.equal(storage.db.prepare('SELECT COUNT(*) AS n FROM identity_nicks WHERE install = ?').get(install(2)).n, 10);
  storage.db.close();
});

test('legacy migration resumes by bounded durable cursor without partial index publication', async () => {
  const storage = identityStorage();
  for (let i = 1; i <= 205; i++) await storage.put(`enforcement:${install(i)}`, effective(i));
  const index = new SqliteIdentityStore(storage);
  assert.equal(await index.migratePage((x) => x, (x) => x), false);
  assert.equal(storage.maxListLimit, 100);
  assert.equal(index.getEnforcement(install(101)), undefined);
  const reopened = new SqliteIdentityStore(storage);
  while (!await reopened.migratePage((x) => x, (x) => x)) { /* bounded pages */ }
  assert.equal(reopened.getEnforcement(install(205)).version, 1);
  assert.equal(reopened.enforcementPage(null, 101).length, 101);
  assert.equal(reopened.enforcementPage(install(200), 101).length, 5);
  assert.ok(storage.maxRows <= 101);
  storage.db.close();
});

test('sighting migration skips invalid JSON and mismatched rows with durable counts, cursors and atomic index cleanup', async () => {
  const storage = identityStorage();
  const warn = console.warn;
  const warnings = [];
  console.warn = (message) => warnings.push(message);
  try {
    const index = new SqliteIdentityStore(storage);
    for (const prefix of ['sighting:', 'enforcement:', 'text-v2:kv']) {
      storage.sql.exec('INSERT INTO identity_migration VALUES (?, NULL, 1)', prefix);
    }
    for (let i = 1; i <= 102; i++) index.saveSighting(sighting(i));
    storage.sql.exec('UPDATE identity_sightings SET payload = ? WHERE install = ?', '{', install(1));
    storage.sql.exec('UPDATE identity_sightings SET payload = ? WHERE install = ?', JSON.stringify(sighting(102)), install(2));
    storage.sql.exec('UPDATE identity_sightings SET payload = ? WHERE install = ?', JSON.stringify({ ...sighting(3), signins: 0 }), install(3));
    const exec = storage.sql.exec;
    storage.sql.exec = (query, ...args) => {
      if (query.startsWith('INSERT OR REPLACE INTO identity_migration')) throw new Error('migration write failed');
      return exec(query, ...args);
    };
    await assert.rejects(index.migratePage((x) => x, validateSighting), /migration write failed/);
    storage.sql.exec = exec;
    assert.equal(storage.sql.exec('SELECT COUNT(*) AS n FROM identity_sightings').toArray()[0].n, 102);
    assert.deepEqual(storage.sql.exec('SELECT * FROM identity_migration_skips').toArray(), []);
    assert.deepEqual(warnings, []);
    assert.equal(await index.migratePage((x) => x, validateSighting), false);
    assert.deepEqual(storage.sql.exec('SELECT cursor, done FROM identity_migration WHERE prefix = ?', 'text-v2:sql').toArray().map((row) => ({ ...row })),
      [{ cursor: install(100), done: 0 }]);
    const reopened = new SqliteIdentityStore(storage);
    assert.equal(await reopened.migratePage((x) => x, validateSighting), false);
    assert.equal(await reopened.migratePage((x) => x, validateSighting), true);
    assert.equal(storage.sql.exec('SELECT count FROM identity_migration_skips WHERE prefix = ?', 'text-v2:sql').toArray()[0].count, 3);
    for (let i = 1; i <= 3; i++) {
      assert.equal(reopened.getSighting(install(i)), undefined);
      for (const table of ['identity_nicks', 'identity_devices']) {
        assert.equal(storage.sql.exec(`SELECT COUNT(*) AS n FROM ${table} WHERE install = ?`, install(i)).toArray()[0].n, 0);
      }
    }
    assert.equal(reopened.getSighting(install(102)).install, install(102));
    assert.match(warnings.join('\n'), /skipping malformed stored sightings during text-v2:sql: 3/);
    assert.equal(warnings.length, 1);
  } finally { console.warn = warn; storage.db.close(); }
});

test('nickname prefix upper bound skips the Unicode surrogate range', () => {
  const storage = identityStorage();
  const index = new SqliteIdentityStore(storage);
  for (const [i, nick] of [[1, '\ud7ff-one'], [2, '\ue000-two']]) {
    const record = sighting(i);
    record.nicks = [{ nick, first_seen: NOW - 10, last_seen: NOW }];
    index.saveSighting(record);
  }
  assert.deepEqual(index.players('\ud7ff', null, 51, NOW).map((row) => row.install), [install(1)]);
  storage.db.close();
});

test('100k identity capacity: file-backed storage, bounded isolate results and retained heap', () => {
  const dir = mkdtempSync(join(tmpdir(), 'identity-capacity-'));
  const path = join(dir, 'identities.sqlite');
  const storage = identityStorage(path);
  try {
    const index = new SqliteIdentityStore(storage);
    global.gc?.();
    const baseline = process.memoryUsage().heapUsed;
    const started = performance.now();
    for (let batch = 0; batch < 1000; batch++) storage.transactionSync(() => {
      for (let j = 1; j <= 100; j++) {
        const i = batch * 100 + j;
        index.writeEnforcement(effective(i));
        index.writeSighting(sighting(i));
      }
    });
    const seededMs = performance.now() - started;
    const lookupStart = performance.now();
    for (let i = 1; i <= 1000; i++) {
      assert.equal(index.getEnforcement(install(i * 100)).version, 1);
      assert.equal(index.deviceBlock(device(i * 400 + 1), NOW).kind, 'ban');
    }
    const lookupMs = performance.now() - lookupStart;
    assert.equal(index.deviceBlock(device(0), NOW).kind, 'ban'); // 100k owners on one device
    assert.equal(index.getEnforcement(install(100001)), undefined);
    assert.equal(index.enforcementPage(install(99900), 101).length, 100);
    assert.equal(index.players('player100000-', null, 51, NOW).length, 1);
    assert.equal(index.linked(install(100000), NOW).length, 100);
    const reopened = new SqliteIdentityStore(storage);
    assert.equal(reopened.getSighting(install(100000)).nicks.length, 10);
    assert.equal(reopened.deviceBlock(device(0), NOW).kind, 'ban');
    const counts = {};
    for (const table of ['identity_enforcement', 'identity_sightings', 'identity_blocks', 'identity_devices', 'identity_nicks']) {
      counts[table] = storage.db.prepare(`SELECT COUNT(*) AS n FROM ${table}`).get().n;
    }
    assert.deepEqual(Object.values(counts), [100000, 100000, 500000, 500000, 1000000]);
    assert.ok(storage.maxRows <= 101);
    global.gc?.();
    const retainedHeapBytes = process.memoryUsage().heapUsed - baseline;
    if (global.gc) assert.ok(retainedHeapBytes < 16 * 1024 * 1024, `retained identity heap ${retainedHeapBytes}`);
    console.log(JSON.stringify({ capacity: 100000, counts, maxMaterializedRows: storage.maxRows,
      retainedHeapBytes: global.gc ? retainedHeapBytes : null, gcEnabled: Boolean(global.gc),
      seededMs, lookupMs, databaseBytes: statSync(path).size, rssBytes: process.memoryUsage().rss }));
  } finally { storage.db.close(); rmSync(dir, { recursive: true, force: true }); }
});
