import { DatabaseSync } from 'node:sqlite';

export function identityStorage(path = ':memory:') {
  const db = new DatabaseSync(path);
  db.exec('CREATE TABLE IF NOT EXISTS test_kv (key TEXT PRIMARY KEY, value TEXT NOT NULL)');
  const storage = {
    db, maxRows: 0, maxListLimit: 0,
    sql: { get databaseSize() {
      return db.prepare('PRAGMA page_count').get().page_count * db.prepare('PRAGMA page_size').get().page_size;
    }, exec(query, ...args) {
      const statement = db.prepare(query);
      const rows = /^\s*(SELECT|EXPLAIN|PRAGMA)/i.test(query) ? statement.all(...args) : (statement.run(...args), []);
      storage.maxRows = Math.max(storage.maxRows, rows.length);
      return { toArray: () => rows };
    } },
    transactionSync(fn) {
      db.exec('BEGIN');
      try { const result = fn(); db.exec('COMMIT'); return result; }
      catch (error) { db.exec('ROLLBACK'); throw error; }
    },
    async get(key) { const row = db.prepare('SELECT value FROM test_kv WHERE key = ?').get(key); return row ? JSON.parse(row.value) : undefined; },
    async put(key, value) { db.prepare('INSERT OR REPLACE INTO test_kv VALUES (?, ?)').run(key, JSON.stringify(value)); },
    async delete(key) { db.prepare('DELETE FROM test_kv WHERE key = ?').run(key); },
    async list({ prefix = '', limit = 1000, startAfter = '', end = null, reverse = false } = {}) {
      storage.maxListLimit = Math.max(storage.maxListLimit, limit);
      const rows = db.prepare(`SELECT key, value FROM test_kv WHERE key >= ? AND key < ? AND key > ?
        AND (? IS NULL OR key < ?) ORDER BY key ${reverse ? 'DESC' : 'ASC'} LIMIT ?`)
        .all(prefix, prefix + '\uffff', startAfter, end, end, limit);
      return new Map(rows.map(({ key, value }) => [key, JSON.parse(value)]));
    },
  };
  return storage;
}
