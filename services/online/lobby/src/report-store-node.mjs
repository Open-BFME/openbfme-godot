import {
  closeSync,
  existsSync,
  fstatSync,
  fsyncSync,
  ftruncateSync,
  mkdirSync,
  openSync,
  readFileSync,
  renameSync,
  unlinkSync,
  writeSync,
} from 'node:fs';
import { join } from 'node:path';
import { normalizeRoster, validReceipt, validateReportSource } from './reports.mjs';
import { repairStoredText } from './stored-text.mjs';

const COMPACT_AFTER_MS = 24 * 60 * 60 * 1000;
const KEY_PATTERNS = [
  /^report:R-[0-9a-f]{24}$/,
  /^evidence:R-[0-9a-f]{24}$/,
  /^index:\d{16}:R-[0-9a-f]{24}$/,
  /^attempts:[0-9a-f]{32}$/,
  /^receipt:[0-9a-f]{32}:[A-Za-z0-9_-]{8,64}$/,
  /^duplicate:[0-9a-f]{32}:[0-9a-f]{16}:[0-9a-f]{32}$/,
  /^roster:[0-9a-f]{16}$/,
];

function validKey(key) {
  return key === 'globalUTCday' || KEY_PATTERNS.some((pattern) => pattern.test(key));
}

function copy(value) {
  return JSON.parse(JSON.stringify(value));
}

function compare(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

function writeFully(fd, bytes) {
  for (let at = 0; at < bytes.length;) at += writeSync(fd, bytes, at, bytes.length - at);
}

function syncDirectory(directory) {
  const fd = openSync(directory, 'r');
  try {
    fsyncSync(fd);
  } finally {
    closeSync(fd);
  }
}

function validateRecord(record, lineNumber) {
  if (!record || typeof record !== 'object' || record.v !== 1 || (record.kind !== 'transaction' && record.kind !== 'snapshot')) {
    throw new Error(`report journal: malformed record at line ${lineNumber}`);
  }
  if (record.kind === 'transaction') {
    exactRecord(record, ['v', 'kind', 'ops'], 'transaction', lineNumber);
    if (!Array.isArray(record.ops) || !record.ops.length) throw new Error(`report journal: empty transaction at line ${lineNumber}`);
    for (const op of record.ops) {
      if (!Array.isArray(op) || (op[0] !== 'put' && op[0] !== 'delete') || typeof op[1] !== 'string' || !validKey(op[1])) {
        throw new Error(`report journal: malformed operation at line ${lineNumber}`);
      }
      if (op[0] === 'put' && op.length !== 3) throw new Error(`report journal: malformed put at line ${lineNumber}`);
      if (op[0] === 'delete' && op.length !== 2) throw new Error(`report journal: malformed delete at line ${lineNumber}`);
      if (op[0] === 'put') validateValue(op[1], op[2], lineNumber);
    }
  } else {
    exactRecord(record, ['v', 'kind', 'compacted_at', 'entries'], 'snapshot', lineNumber);
    if (!Array.isArray(record.entries) || !Number.isSafeInteger(record.compacted_at) || record.compacted_at < 0) {
      throw new Error(`report journal: malformed snapshot at line ${lineNumber}`);
    }
    const keys = new Set();
    for (const entry of record.entries) {
      if (!Array.isArray(entry) || entry.length !== 2 || typeof entry[0] !== 'string' || !validKey(entry[0]) || keys.has(entry[0])) {
        throw new Error(`report journal: malformed snapshot entry at line ${lineNumber}`);
      }
      keys.add(entry[0]);
      validateValue(entry[0], entry[1], lineNumber);
    }
  }
}

function exactRecord(value, fields, what, lineNumber) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...fields].sort().join(',')) {
    throw new Error(`report journal: malformed ${what} at line ${lineNumber}`);
  }
}

function validateValue(key, value, lineNumber) {
  try {
    if (key.startsWith('report:')) return validateReportSource(value, key.slice(7));
    if (key.startsWith('evidence:')) {
      exactRecord(value, ['expires_at', 'lines'], 'report evidence', lineNumber);
      if (!Number.isSafeInteger(value.expires_at) || value.expires_at < 0 ||
          !Array.isArray(value.lines) || value.lines.length > 50) throw new Error('evidence malformed');
      for (const line of value.lines) {
        exactRecord(line, ['game_id', 'nick', 'text', 'at'], 'report evidence line', lineNumber);
        if (typeof line.game_id !== 'string' || !/^[0-9a-f]{16}$/.test(line.game_id) ||
            typeof line.nick !== 'string' || !line.nick.length || Array.from(line.nick).length > 24 || !line.nick.isWellFormed() ||
            typeof line.text !== 'string' || Array.from(line.text).length > 200 || !line.text.isWellFormed() ||
            (line.at !== null && (!Number.isSafeInteger(line.at) || line.at < 0))) throw new Error('evidence line malformed');
      }
      return;
    }
    if (key.startsWith('roster:')) return normalizeRoster(value, key.slice(7));
    if (key.startsWith('receipt:')) return validReceipt(value);
    if (key.startsWith('attempts:')) {
      if (!Array.isArray(value) || value.length > 20 ||
          value.some((at) => !Number.isSafeInteger(at) || at < 0) ||
          value.some((at, index) => index > 0 && at < value[index - 1])) throw new Error('attempts malformed');
      return;
    }
    if (key === 'globalUTCday') {
      exactRecord(value, ['day', 'count'], 'global report counter', lineNumber);
      if (typeof value.day !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(value.day) ||
          new Date(`${value.day}T00:00:00Z`).toISOString().slice(0, 10) !== value.day ||
          !Number.isSafeInteger(value.count) || value.count < 0 || value.count > 1000) throw new Error('counter malformed');
      return;
    }
    if (key.startsWith('index:')) {
      const [, received, id] = /^index:(\d{16}):(R-[0-9a-f]{24})$/.exec(key);
      exactRecord(value, ['id', 'expires_at'], 'report index', lineNumber);
      if (value.id !== id || !Number.isSafeInteger(value.expires_at) || value.expires_at < 0 ||
          !Number.isSafeInteger(Number(received))) throw new Error('index malformed');
      return;
    }
    if (key.startsWith('duplicate:')) {
      exactRecord(value, ['expires_at'], 'duplicate guard', lineNumber);
      if (!Number.isSafeInteger(value.expires_at) || value.expires_at < 0) throw new Error('duplicate malformed');
      return;
    }
    throw new Error('unknown namespace');
  } catch (error) {
    throw new Error(`report journal: malformed value for ${key} at line ${lineNumber}: ${error.message}`);
  }
}

export class NodeReportStore {
  constructor(directory) {
    this.directory = directory;
    this.journalPath = join(directory, 'journal.ndjson');
    this.tempPath = join(directory, 'journal.ndjson.tmp');
    mkdirSync(directory, { recursive: true });
    if (existsSync(this.tempPath)) unlinkSync(this.tempPath);
    this.data = new Map();
    this.queue = Promise.resolve();
    this.failed = null;
    this.replay();
    this.lastCompaction = this.lastCompaction ?? 0;
  }

  replay() {
    if (!existsSync(this.journalPath)) {
      const fd = openSync(this.journalPath, 'a', 0o600);
      fsyncSync(fd);
      closeSync(fd);
      syncDirectory(this.directory);
      this.lastCompaction = 0;
      return;
    }
    let bytes = readFileSync(this.journalPath);
    if (!bytes.length) return;
    if (bytes.at(-1) !== 0x0a) {
      // DEVINPR-1 (review of WP-C): a final record without its newline is an append that was interrupted (a crash, a power loss) before its fsync
      // returned, so no request was ever answered for it: it is cut off, said in the log, and the journal before it is read as usual
      const keep = bytes.lastIndexOf(0x0a) + 1;
      const fd = openSync(this.journalPath, 'r+');
      try {
        ftruncateSync(fd, keep);
        fsyncSync(fd);
      } finally {
        closeSync(fd);
      }
      console.error(`report journal: cut off an incomplete final record (${bytes.length - keep} bytes) left by an interrupted write`);
      bytes = bytes.subarray(0, keep);
      if (!bytes.length) return;
    }
    let text;
    try {
      text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    } catch {
      throw new Error('report journal: invalid UTF-8');
    }
    const lines = text.split('\n');
    lines.pop();
    let snapshotSeen = false;
    for (const [index, line] of lines.entries()) {
      if (!line) throw new Error(`report journal: empty record at line ${index + 1}`);
      let record;
      try {
        record = repairStoredText(JSON.parse(line));
      } catch {
        throw new Error(`report journal: invalid JSON at line ${index + 1}`);
      }
      validateRecord(record, index + 1);
      if (record.kind === 'snapshot') {
        if (snapshotSeen || index !== 0) throw new Error(`report journal: snapshot is not first at line ${index + 1}`);
        snapshotSeen = true;
        this.data = new Map(record.entries.map(([key, value]) => [key, value]));
        this.lastCompaction = record.compacted_at;
      } else {
        for (const op of record.ops) {
          if (op[0] === 'put') this.data.set(op[1], op[2]);
          else this.data.delete(op[1]);
        }
      }
    }
  }

  async compact() {
    const compactedAt = Date.now();
    const record = {
      v: 1,
      kind: 'snapshot',
      compacted_at: compactedAt,
      entries: [...this.data.entries()].sort(([a], [b]) => compare(a, b)),
    };
    const bytes = new TextEncoder().encode(`${JSON.stringify(record)}\n`);
    const fd = openSync(this.tempPath, 'w', 0o600);
    try {
      writeFully(fd, bytes);
      fsyncSync(fd);
    } finally {
      closeSync(fd);
    }
    renameSync(this.tempPath, this.journalPath);
    syncDirectory(this.directory);
    this.lastCompaction = compactedAt;
  }

  reportTransaction(fn) {
    const run = this.queue.then(async () => {
      if (this.failed) throw this.failed;
      if (Date.now() - this.lastCompaction >= COMPACT_AFTER_MS) await this.compact();
      const overlay = new Map();
      const ops = [];
      const deleted = Symbol('deleted');
      const read = (key) => overlay.has(key) ? overlay.get(key) : this.data.has(key) ? this.data.get(key) : deleted;
      const keysFor = (prefix) => {
        const keys = new Set([...this.data.keys()].filter((key) => key.startsWith(prefix)));
        for (const [key, value] of overlay) {
          if (!key.startsWith(prefix)) continue;
          if (value === deleted) keys.delete(key);
          else keys.add(key);
        }
        return [...keys];
      };
      const tx = {
        get: async (key) => {
          if (!validKey(key)) throw new Error(`report journal: invalid key ${key}`);
          const value = read(key);
          return value === deleted ? null : copy(value);
        },
        put: async (key, value) => {
          if (!validKey(key)) throw new Error(`report journal: invalid key ${key}`);
          const stored = copy(value);
          validateValue(key, stored, 'transaction');
          ops.push(['put', key, stored]);
          overlay.set(key, stored);
        },
        delete: async (key) => {
          if (!validKey(key)) throw new Error(`report journal: invalid key ${key}`);
          ops.push(['delete', key]);
          overlay.set(key, deleted);
        },
        list: async (prefix, { after = null, limit = 100, reverse = false } = {}) => {
          if (typeof prefix !== 'string' || !Number.isInteger(limit) || limit < 1 || limit > 1000) throw new Error('report journal: invalid list');
          let keys = keysFor(prefix);
          keys.sort(compare);
          if (after !== null) keys = keys.filter((key) => reverse ? key < after : key > after);
          if (reverse) keys.reverse();
          return new Map(keys.slice(0, limit).map((key) => [key, copy(read(key))]));
        },
      };
      const result = await fn(tx);
      if (!ops.length) return result;
      const line = new TextEncoder().encode(`${JSON.stringify({ v: 1, kind: 'transaction', ops })}\n`);
      let fd;
      let size;
      try {
        fd = openSync(this.journalPath, 'a', 0o600);
        size = fstatSync(fd).size;
        writeFully(fd, line);
        fsyncSync(fd);
      } catch (error) {
        // DEVINPR-1 (review of WP-C): a failed append is cut back to the journal's size before it, so a passing disk error (full, I/O) fails this
        // transaction only; the store stays failed (every later transaction refused until a restart) only when that cut fails too
        let rolledBack = size === undefined;
        if (size !== undefined) {
          try {
            ftruncateSync(fd, size);
            fsyncSync(fd);
            rolledBack = true;
          } catch {
            rolledBack = false;
          }
        }
        if (!rolledBack) this.failed = error;
        throw error;
      } finally {
        if (fd !== undefined) closeSync(fd);
      }
      for (const [key, value] of overlay) {
        if (value === deleted) this.data.delete(key);
        else this.data.set(key, value);
      }
      return result;
    });
    this.queue = run.catch(() => {});
    return run;
  }
}

export function createNodeReportStore(directory) {
  return new NodeReportStore(directory);
}
