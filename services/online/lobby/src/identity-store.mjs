// Durable identities stay in SQLite; only bounded result pages enter the isolate.
import { repairStoredText } from './stored-text.mjs';

export const IDENTITY_PAGE = 100;
export const SIGHTING_RETENTION_S = 365 * 24 * 60 * 60;

export class SqliteIdentityStore {
  constructor(storage) {
    if (!storage.sql || !storage.transactionSync) throw new Error('identity storage needs a SQLite-backed Durable Object');
    this.storage = storage;
    this.sql = storage.sql;
    for (const statement of [
      'CREATE TABLE IF NOT EXISTS identity_enforcement (install TEXT PRIMARY KEY, payload TEXT NOT NULL)',
      'CREATE TABLE IF NOT EXISTS identity_blocks (device TEXT NOT NULL, install TEXT NOT NULL, rank INTEGER NOT NULL, permanent INTEGER NOT NULL, expires INTEGER NOT NULL, payload TEXT NOT NULL, PRIMARY KEY (device, install))',
      'CREATE INDEX IF NOT EXISTS identity_block_order ON identity_blocks (device, rank DESC, permanent DESC, expires DESC, install)',
      'CREATE INDEX IF NOT EXISTS identity_block_owner ON identity_blocks (install)',
      'CREATE TABLE IF NOT EXISTS identity_sightings (install TEXT PRIMARY KEY, last_seen INTEGER NOT NULL, payload TEXT NOT NULL)',
      'CREATE INDEX IF NOT EXISTS identity_seen ON identity_sightings (last_seen DESC, install)',
      'CREATE TABLE IF NOT EXISTS identity_devices (device TEXT NOT NULL, install TEXT NOT NULL, PRIMARY KEY (device, install))',
      'CREATE INDEX IF NOT EXISTS identity_device_owner ON identity_devices (install)',
      'CREATE TABLE IF NOT EXISTS identity_nicks (nick TEXT NOT NULL, install TEXT NOT NULL, PRIMARY KEY (nick, install))',
      'CREATE INDEX IF NOT EXISTS identity_nick_owner ON identity_nicks (install)',
      'CREATE TABLE IF NOT EXISTS identity_migration (prefix TEXT PRIMARY KEY, cursor TEXT, done INTEGER NOT NULL)',
      'CREATE TABLE IF NOT EXISTS identity_migration_skips (prefix TEXT PRIMARY KEY, count INTEGER NOT NULL)',
    ]) this.sql.exec(statement);
  }

  getEnforcement(install) {
    const row = this.sql.exec('SELECT payload FROM identity_enforcement WHERE install = ?', install).toArray()[0];
    if (!row) return undefined;
    const { install: unused, ...state } = repairStoredText(JSON.parse(row.payload));
    return state;
  }

  saveEnforcement(record) {
    this.storage.transactionSync(() => this.writeEnforcement(record));
    this.onEnforcementChange?.(record.install);
  }

  writeEnforcement(record) {
    const { install, block, devices } = record;
    this.onEnforcementChange?.(install);
    this.sql.exec('INSERT OR REPLACE INTO identity_enforcement VALUES (?, ?)', install, JSON.stringify(record));
    this.sql.exec('DELETE FROM identity_blocks WHERE install = ?', install);
    if (block) for (const device of devices) {
      this.sql.exec('INSERT INTO identity_blocks VALUES (?, ?, ?, ?, ?, ?)', device, install,
        block.kind === 'ban' ? 2 : 1, block.until === null ? 1 : 0, block.until ?? 0, JSON.stringify(block));
    }
  }

  deviceBlock(device, now) {
    if (!device) return null;
    const row = this.sql.exec(`SELECT payload FROM identity_blocks
      WHERE device = ? AND (permanent = 1 OR expires > ?)
      ORDER BY rank DESC, permanent DESC, expires DESC, install LIMIT 1`, device, now).toArray()[0];
    return row ? repairStoredText(JSON.parse(row.payload)) : null;
  }

  enforcementPage(after, limit) {
    if (!Number.isInteger(limit) || limit < 1 || limit > IDENTITY_PAGE + 1) throw new Error('invalid identity page size');
    return this.sql.exec('SELECT payload FROM identity_enforcement WHERE install > ? ORDER BY install LIMIT ?', after || '', limit)
      .toArray().map(({ payload }) => { const { install, ...state } = repairStoredText(JSON.parse(payload)); return [install, state]; });
  }

  getSighting(install) {
    const row = this.sql.exec('SELECT payload FROM identity_sightings WHERE install = ?', install).toArray()[0];
    return row ? repairStoredText(JSON.parse(row.payload)) : undefined;
  }

  saveSighting(record) {
    this.storage.transactionSync(() => this.writeSighting(record));
  }

  deleteSighting(install) {
    this.storage.transactionSync(() => this.deleteSightingRows(install));
  }

  // DEVINPR-1 (review of WP-C): a sign-in rewrites its sighting row and only the nick / device rows that changed (before: every row of the install
  // deleted and inserted again, about 32 row writes a sign-in, so one address could spend the Free plan's daily write budget alone)
  writeSighting(record) {
    const { install } = record;
    this.sql.exec('INSERT OR REPLACE INTO identity_sightings VALUES (?, ?, ?)', install, record.last_seen, JSON.stringify(record));
    const devices = new Set(record.devices.map(({ hash }) => hash));
    for (const { device } of this.sql.exec('SELECT device FROM identity_devices WHERE install = ?', install).toArray()) {
      if (!devices.delete(device)) this.sql.exec('DELETE FROM identity_devices WHERE device = ? AND install = ?', device, install);
    }
    for (const device of devices) this.sql.exec('INSERT INTO identity_devices VALUES (?, ?)', device, install);
    const nicks = new Set(record.nicks.map(({ nick }) => nick.toLocaleLowerCase('en-US')));
    for (const { nick } of this.sql.exec('SELECT nick FROM identity_nicks WHERE install = ?', install).toArray()) {
      if (!nicks.delete(nick)) this.sql.exec('DELETE FROM identity_nicks WHERE nick = ? AND install = ?', nick, install);
    }
    for (const nick of nicks) this.sql.exec('INSERT INTO identity_nicks VALUES (?, ?)', nick, install);
  }

  deleteSightingRows(install) {
    this.sql.exec('DELETE FROM identity_devices WHERE install = ?', install);
    this.sql.exec('DELETE FROM identity_nicks WHERE install = ?', install);
    this.sql.exec('DELETE FROM identity_sightings WHERE install = ?', install);
  }

  cleanupSightings(now) {
    const rows = this.sql.exec('SELECT install FROM identity_sightings WHERE last_seen < ? ORDER BY last_seen LIMIT ?',
      now - SIGHTING_RETENTION_S, IDENTITY_PAGE).toArray();
    this.storage.transactionSync(() => { for (const { install } of rows) this.deleteSightingRows(install); });
    return rows.length;
  }

  players(query, cursor, limit, now) {
    if (!Number.isInteger(limit) || limit < 1 || limit > IDENTITY_PAGE) throw new Error('invalid identity page size');
    const upper = prefixEnd(query);
    const rows = this.sql.exec(`SELECT s.payload FROM identity_sightings s
      WHERE s.last_seen >= ? AND (? IS NULL OR s.last_seen < ? OR (s.last_seen = ? AND s.install > ?))
      AND (? = '' OR (s.install >= ? AND (? IS NULL OR s.install < ?)) OR EXISTS
        (SELECT 1 FROM identity_nicks n WHERE n.install = s.install AND n.nick >= ? AND (? IS NULL OR n.nick < ?)))
      ORDER BY s.last_seen DESC, s.install LIMIT ?`, now - SIGHTING_RETENTION_S,
    cursor?.last_seen ?? null, cursor?.last_seen ?? null, cursor?.last_seen ?? null, cursor?.install ?? '',
    query, query, upper, upper, query, upper, upper, limit).toArray();
    return rows.map(({ payload }) => repairStoredText(JSON.parse(payload)));
  }

  linked(install, now) {
    return this.sql.exec(`SELECT s.payload, COUNT(*) AS shared_devices FROM identity_devices own
      JOIN identity_devices other ON other.device = own.device
      JOIN identity_sightings s ON s.install = other.install
      WHERE own.install = ? AND other.install != ? AND s.last_seen >= ?
      GROUP BY s.install ORDER BY shared_devices DESC, s.last_seen DESC, s.install LIMIT ?`,
    install, install, now - SIGHTING_RETENTION_S, IDENTITY_PAGE).toArray().map(({ payload, shared_devices }) => {
      const row = repairStoredText(JSON.parse(payload));
      return { install: row.install, nick: row.nicks[0]?.nick ?? '', shared_devices,
        signins: row.signins, last_seen: row.last_seen };
    });
  }

  // One legacy page per call. Cursor and index writes commit together; legacy KV is retained for rollback.
  async migratePage(validateEnforcement, validateSighting) {
    for (const [prefix, validate, write] of [
      ['enforcement:', validateEnforcement, (row) => this.writeEnforcement(row)],
      ['sighting:', validateSighting, (row) => this.writeSighting(row)],
    ]) {
      const state = this.sql.exec('SELECT cursor, done FROM identity_migration WHERE prefix = ?', prefix).toArray()[0];
      if (state?.done) continue;
      const rows = await this.storage.list({ prefix, limit: IDENTITY_PAGE, ...(state?.cursor ? { startAfter: state.cursor } : {}) });
      const skipped = [];
      this.storage.transactionSync(() => {
        for (const [key, raw] of rows) {
          if (prefix === 'sighting:') {
            const install = key.slice(prefix.length);
            const value = sightingForMigration(() => raw, install, validate);
            if (!value) { skipped.push(install); continue; }
            write(value.clean);
            continue;
          }
          const record = validate(raw);
          if (key !== `${prefix}${record.install}`) throw new Error('stored identity key does not match install');
          write(record);
        }
        this.finishMigrationPage(prefix, rows.size ? [...rows.keys()].at(-1) : state?.cursor ?? null,
          rows.size < IDENTITY_PAGE, skipped);
      });
      warnSightingSkips(prefix, skipped);
      return false;
    }
    return this.sanitizePlatformsPage(validateSighting);
  }

  async sanitizePlatformsPage(validateSighting) {
    const legacyKey = 'text-v2:kv';
    const legacy = this.sql.exec('SELECT cursor, done FROM identity_migration WHERE prefix = ?', legacyKey).toArray()[0];
    if (!legacy?.done) {
      const rows = await this.storage.list({ prefix: 'sighting:', limit: IDENTITY_PAGE,
        ...(legacy?.cursor ? { startAfter: legacy.cursor } : {}) });
      const skipped = [];
      for (const [key, raw] of rows) {
        const install = key.slice('sighting:'.length);
        const value = sightingForMigration(() => raw, install, validateSighting);
        if (!value) { skipped.push(install); continue; }
        const { clean } = value;
        if (repairStoredText(raw) !== raw || clean.platform !== raw.platform) await this.storage.put(key, clean);
      }
      this.storage.transactionSync(() => this.finishMigrationPage(legacyKey,
        rows.size ? [...rows.keys()].at(-1) : legacy?.cursor ?? null, rows.size < IDENTITY_PAGE, skipped));
      warnSightingSkips(legacyKey, skipped);
      return false;
    }
    const indexKey = 'text-v2:sql';
    const indexed = this.sql.exec('SELECT cursor, done FROM identity_migration WHERE prefix = ?', indexKey).toArray()[0];
    if (!indexed?.done) {
      const rows = this.sql.exec('SELECT install, payload FROM identity_sightings WHERE install > ? ORDER BY install LIMIT ?',
        indexed?.cursor ?? '', IDENTITY_PAGE).toArray();
      const skipped = [];
      this.storage.transactionSync(() => {
        for (const { install, payload } of rows) {
          const value = sightingForMigration(() => JSON.parse(payload), install, validateSighting);
          if (!value) { skipped.push(install); continue; }
          const { raw, clean } = value;
          if (repairStoredText(raw) !== raw || clean.platform !== raw.platform) this.writeSighting(clean);
        }
        this.finishMigrationPage(indexKey, rows.length ? rows.at(-1).install : indexed?.cursor ?? null,
          rows.length < IDENTITY_PAGE, skipped);
      });
      warnSightingSkips(indexKey, skipped);
      return false;
    }
    return true;
  }

  finishMigrationPage(prefix, cursor, done, skipped) {
    for (const install of skipped) this.deleteSightingRows(install);
    if (skipped.length) {
      this.sql.exec(`INSERT INTO identity_migration_skips VALUES (?, ?)
        ON CONFLICT(prefix) DO UPDATE SET count = count + excluded.count`, prefix, skipped.length);
    }
    this.sql.exec('INSERT OR REPLACE INTO identity_migration VALUES (?, ?, ?)', prefix, cursor, done ? 1 : 0);
  }
}

function warnSightingSkips(prefix, skipped) {
  if (skipped.length) console.warn(`lobby: skipping malformed stored sightings during ${prefix}: ${skipped.length}`);
}

function sightingForMigration(read, install, validate) {
  try {
    const raw = read();
    const clean = validate(raw);
    return clean.install === install ? { raw, clean } : null;
  } catch {
    return null;
  }
}

function prefixEnd(prefix) {
  const chars = Array.from(prefix);
  while (chars.length) {
    const code = chars.pop().codePointAt(0);
    if (code < 0x10ffff) return chars.join('') + String.fromCodePoint(code === 0xd7ff ? 0xe000 : code + 1);
  }
  return null;
}
