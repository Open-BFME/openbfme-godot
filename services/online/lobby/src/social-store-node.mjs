import { DatabaseSync } from 'node:sqlite';
import { SocialStorageError, socialStorageError } from './social-store.mjs';

export class NodeSocialStore {
  constructor(path) {
    try {
      this.db = new DatabaseSync(path);
      this.db.exec('PRAGMA journal_mode = WAL; PRAGMA synchronous = FULL; CREATE TABLE IF NOT EXISTS social_records (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;');
    } catch (error) {
      throw socialStorageError(error);
    }
    this.queue = Promise.resolve();
    this.select = this.db.prepare('SELECT value FROM social_records WHERE key = ?');
    this.upsert = this.db.prepare('INSERT INTO social_records (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value');
    this.remove = this.db.prepare('DELETE FROM social_records WHERE key = ?');
    this.listPrefix = this.db.prepare('SELECT key, value FROM social_records WHERE key >= ? AND key < ? ORDER BY key ASC LIMIT ?');
    this.listAfter = this.db.prepare('SELECT key, value FROM social_records WHERE key >= ? AND key < ? AND key > ? ORDER BY key ASC LIMIT ?');
    this.listPrefixReverse = this.db.prepare('SELECT key, value FROM social_records WHERE key >= ? AND key < ? ORDER BY key DESC LIMIT ?');
    this.listAfterReverse = this.db.prepare('SELECT key, value FROM social_records WHERE key >= ? AND key < ? AND key < ? ORDER BY key DESC LIMIT ?');
  }

  socialTransaction(fn) {
    const run = this.queue.then(() => {
      if (this.db.isTransaction) throw new SocialStorageError('unavailable');
      try {
        this.db.exec('BEGIN IMMEDIATE');
        const tx = {
          get: (key) => {
            const row = this.select.get(key);
            return row ? JSON.parse(row.value) : null;
          },
          put: (key, value) => this.upsert.run(key, JSON.stringify(value)),
          delete: (key) => this.remove.run(key),
          list: (prefix, { after = null, limit = 100, reverse = false } = {}) => {
            if (typeof prefix !== 'string' || !Number.isInteger(limit) || limit < 1 || limit > 1000 ||
                (after !== null && (typeof after !== 'string' || !after.startsWith(prefix)))) {
              throw new TypeError('invalid social list bounds');
            }
            const end = `${prefix}\uffff`;
            const rows = after === null
              ? (reverse ? this.listPrefixReverse : this.listPrefix).all(prefix, end, limit)
              : (reverse ? this.listAfterReverse : this.listAfter).all(prefix, end, after, limit);
            return new Map(rows.map((row) => [row.key, JSON.parse(row.value)]));
          },
        };
        const result = fn(tx);
        if (result && typeof result.then === 'function') throw new TypeError('social transaction callback must be synchronous');
        this.db.exec('COMMIT');
        return result;
      } catch (error) {
        try {
          if (this.db.isTransaction) this.db.exec('ROLLBACK');
        } catch {}
        if (Number.isInteger(error?.status) && typeof error?.code === 'string') throw error;
        throw socialStorageError(error);
      }
    });
    this.queue = run.catch(() => {});
    return run;
  }

  close() {
    this.db.close();
  }
}

export function createNodeSocialStore(path) {
  return new NodeSocialStore(path);
}
