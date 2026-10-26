export class SocialStorageError extends Error {
  constructor(kind, cause = undefined) {
    super(kind === 'full' ? 'social storage is full' : 'social storage is unavailable', { cause });
    this.kind = kind;
  }
}

export function socialSqlTransaction(sql, { beforePut = () => {} } = {}) {
  return (fn) => {
    const tx = {
      get(key) {
        const row = sql.exec('SELECT value FROM social_records WHERE key = ?', key).toArray()[0];
        return row ? JSON.parse(row.value) : null;
      },
      put(key, value) {
        beforePut();
        sql.exec('INSERT INTO social_records (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value', key, JSON.stringify(value));
      },
      delete(key) {
        sql.exec('DELETE FROM social_records WHERE key = ?', key);
      },
      list(prefix, { after = null, limit = 100, reverse = false } = {}) {
        if (typeof prefix !== 'string' || !Number.isInteger(limit) || limit < 1 || limit > 1000 ||
            (after !== null && (typeof after !== 'string' || !after.startsWith(prefix)))) {
          throw new TypeError('invalid social list bounds');
        }
        const order = reverse ? 'DESC' : 'ASC';
        const comparison = reverse ? '<' : '>';
        const rows = after === null
          ? sql.exec(`SELECT key, value FROM social_records WHERE key >= ? AND key < ? ORDER BY key ${order} LIMIT ?`, prefix, `${prefix}\uffff`, limit).toArray()
          : sql.exec(`SELECT key, value FROM social_records WHERE key >= ? AND key < ? AND key ${comparison} ? ORDER BY key ${order} LIMIT ?`, prefix, `${prefix}\uffff`, after, limit).toArray();
        return new Map(rows.map((row) => [row.key, JSON.parse(row.value)]));
      },
    };
    return fn(tx);
  };
}

export function socialStorageError(error) {
  if (error instanceof SocialStorageError) return error;
  if (Number.isInteger(error?.status) && typeof error?.code === 'string') return error;
  if (error?.errcode === 13 || /SQLITE_FULL|database or disk is full/i.test(String(error?.message ?? ''))) {
    return new SocialStorageError('full', error);
  }
  return new SocialStorageError('unavailable', error);
}
