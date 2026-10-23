// OpenBFME online lobby on Cloudflare (lane ONLINE-2, docs/ONLINE.md option (d)): a Worker that hands every /v1 request to ONE Durable Object (the lobby:
// it keeps the games in its storage). Bindings: LOBBY (the Durable Object namespace), LOGS (an R2 bucket, only with LOG_STORE = "r2"); secrets:
// TICKET_KEYS, SESSION_KEY, UPLOAD_KEY, ADMIN_KEY, MOD_KEY; vars: RELAYS (may be empty: no relay yet), PUBLIC_URL, LOG_STORE, REQUIRE_INSTALL.
// CFDEPLOY-1: LOG_STORE picks where the relay's command logs go: "durable-object" (the object's own SQLite storage, on the Workers Free plan),
// "r2" (the LOGS bucket), or unset (no store: uploads and replay reads fail with 'no_log_store').
// ONLINE-2 r2 (Sol r1): a Durable Object runs another request whenever one awaits (storage, crypto, R2), so the object queues the requests itself: one
// request through the lobby at a time, as the self-hosted server does.

import { Lobby, LobbyError, route, configFromEnv, MAX_JSON_BYTES, validateEnforcementRecord, validateSighting, withoutGameIp } from './lobby.mjs';
import { SqliteIdentityStore } from './identity-store.mjs';
import { SocialStorageError, socialSqlTransaction, socialStorageError } from './social-store.mjs';

const subtle = crypto.subtle;

// CFDEPLOY-1: the relay's command logs (and the replay START records) in the LobbyObject's own SQLite storage, for the Workers Free plan (no R2).
// Free-plan limits (developers.cloudflare.com durable-objects/platform/limits and pricing, read 2026-10-12): a string, BLOB or row is at most 2 MB; 5 GB of
// SQL storage per account; 100,000 rows written and 5 M rows read a day (then operations fail; no charge on Free). So:
// - a blob is cut into 1 MiB parts, one row each (under the 2 MB row limit with its key);
// - a log is at most 8 MiB (413 'too_big', checked against the body as it arrives); ONLINE-2 measured about 90 KB per game, so one part is typical;
// - logs together are at most 1 GiB (blob_totals keeps the sum, so no upload scans the table); past it an upload is refused with 507
//   'log_store_full'. Nothing is evicted: old logs stay until a cleanup or the R2 store comes (the deploy notes). START records are tiny and not capped.
export const SQL_PART_BYTES = 1024 * 1024;
export const SQL_MAX_LOG_BYTES = 8 * 1024 * 1024;
export const SQL_MAX_STORED_LOG_BYTES = 1024 * 1024 * 1024;

export class SqliteLogStore {
  constructor(storage, { partBytes = SQL_PART_BYTES, maxLogBytes = SQL_MAX_LOG_BYTES, maxStoredLogBytes = SQL_MAX_STORED_LOG_BYTES } = {}) {
    if (!storage.sql) throw new Error('LOG_STORE = "durable-object" needs a SQLite-backed Durable Object (the new_sqlite_classes migration)');
    this.kind = 'durable-object';
    this.storage = storage;
    this.sql = storage.sql;
    this.partBytes = partBytes;
    this.maxLogBytes = maxLogBytes;
    this.maxStoredLogBytes = maxStoredLogBytes;
    this.sql.exec('CREATE TABLE IF NOT EXISTS blobs (key TEXT PRIMARY KEY, size INTEGER NOT NULL, parts INTEGER NOT NULL, stored_at INTEGER NOT NULL)');
    this.sql.exec('CREATE TABLE IF NOT EXISTS blob_parts (key TEXT NOT NULL, part INTEGER NOT NULL, bytes BLOB NOT NULL, PRIMARY KEY (key, part))');
    this.sql.exec('CREATE TABLE IF NOT EXISTS blob_totals (id INTEGER PRIMARY KEY CHECK (id = 0), log_bytes INTEGER NOT NULL)');
    this.sql.exec('INSERT OR IGNORE INTO blob_totals (id, log_bytes) VALUES (0, 0)');
  }
  storedLogBytes() {
    return Number(this.sql.exec('SELECT log_bytes FROM blob_totals WHERE id = 0').toArray()[0].log_bytes);
  }
  putBlob(key, bytes) {
    const isLog = key.startsWith('logs/');
    if (isLog && bytes.length > this.maxLogBytes) throw new LobbyError(413, 'too_big', `a relay log of more than ${this.maxLogBytes} bytes (the log store's limit)`);
    const old = this.sql.exec('SELECT size FROM blobs WHERE key = ?', key).toArray()[0];
    const oldSize = old ? Number(old.size) : 0;
    const total = this.storedLogBytes() + (isLog ? bytes.length - oldSize : 0);
    if (isLog && total > this.maxStoredLogBytes) {
      throw new LobbyError(507, 'log_store_full', `the log store holds ${this.storedLogBytes()} of ${this.maxStoredLogBytes} bytes: no room for ${bytes.length} more`);
    }
    this.storage.transactionSync(() => {
      this.sql.exec('DELETE FROM blob_parts WHERE key = ?', key);
      let parts = 0;
      for (let at = 0; at < bytes.length || parts === 0; at += this.partBytes, parts++) {
        const part = bytes.slice(at, at + this.partBytes); // a copy: its own ArrayBuffer, exactly the part
        this.sql.exec('INSERT INTO blob_parts (key, part, bytes) VALUES (?, ?, ?)', key, parts, part.buffer);
      }
      this.sql.exec('INSERT OR REPLACE INTO blobs (key, size, parts, stored_at) VALUES (?, ?, ?, ?)', key, bytes.length, parts, Date.now());
      if (isLog) this.sql.exec('UPDATE blob_totals SET log_bytes = ? WHERE id = 0', total);
    });
  }
  // INFRA-2: the admin backup's listing, in key order after `after`
  listBlobs(after, limit) {
    return this.sql.exec('SELECT key, size, stored_at FROM blobs WHERE key > ? ORDER BY key LIMIT ?', after, limit).toArray()
      .map((r) => ({ key: r.key, size: Number(r.size), storedAt: Number(r.stored_at) }));
  }
  // LOBBYSTATS-1: when the START records and relay logs stored since `sinceMs` were written (ms), for /v1/stats' games per day. One read of the
  // blobs table, run once per object lifetime (the lobby keeps the times in memory after it): the range on the key reads only starts/ and logs/
  // rows, but every one of them (stored_at has no index: an index would cost a row write per blob), so a restarted object pays one row read per
  // stored game; nothing is written
  gameTimes(sinceMs) {
    const rows = this.sql.exec("SELECT substr(key, 1, 6) AS kind, stored_at FROM blobs WHERE ((key >= 'logs/' AND key < 'logs0') OR (key >= 'starts/' AND key < 'starts0')) AND stored_at >= ?", sinceMs).toArray();
    const out = { started: [], finished: [] };
    for (const r of rows) (r.kind === 'starts' ? out.started : out.finished).push(Number(r.stored_at));
    return out;
  }
  getBlob(key) {
    const meta = this.sql.exec('SELECT size, parts FROM blobs WHERE key = ?', key).toArray()[0];
    if (!meta) return null;
    const rows = this.sql.exec('SELECT part, bytes FROM blob_parts WHERE key = ? ORDER BY part', key).toArray();
    const size = Number(meta.size);
    const out = new Uint8Array(size);
    let at = 0;
    for (const [i, r] of rows.entries()) {
      const b = new Uint8Array(r.bytes);
      if (Number(r.part) !== i || at + b.length > size) throw new Error(`log store: ${key} has a bad part ${r.part}`);
      out.set(b, at);
      at += b.length;
    }
    if (rows.length !== Number(meta.parts) || at !== size) throw new Error(`log store: ${key} has ${rows.length} of ${meta.parts} parts, ${at} of ${size} bytes`);
    return out;
  }
}

// CFDEPLOY-1: the log store LOG_STORE names; a misnamed store or a missing binding is an error, never a silent default
export function logStoreFor(state, env) {
  const kind = String(env.LOG_STORE ?? '');
  if (kind === 'durable-object') return new SqliteLogStore(state.storage);
  if (kind === 'r2') {
    if (!env.LOGS) throw new Error('LOG_STORE = "r2" needs the LOGS (R2 bucket) binding');
    return {
      kind: 'r2',
      putBlob: (key, bytes) => env.LOGS.put(key, bytes),
      getBlob: async (key) => {
        const o = await env.LOGS.get(key);
        return o ? new Uint8Array(await o.arrayBuffer()) : null;
      },
      listBlobs: async (after, limit) => {
        const r = await env.LOGS.list({ startAfter: after, limit });
        return r.objects.map((o) => ({ key: o.key, size: o.size, storedAt: o.uploaded.getTime() }));
      },
    };
  }
  if (kind === '') return { kind: 'none' };
  throw new Error(`unknown LOG_STORE "${kind}" (durable-object, r2, or unset)`);
}

export function durableReportTransaction(storage) {
  if (typeof storage.transaction !== 'function') return null;
  return (fn) => storage.transaction(async (transaction) => fn({
    get: async (key) => (await transaction.get(key)) ?? null,
    put: (key, value) => transaction.put(key, value),
    delete: (key) => transaction.delete(key),
    list: async (prefix, { after = null, limit = 100, reverse = false } = {}) => {
      const rows = await transaction.list({
        prefix,
        ...(after === null ? {} : (reverse ? { end: after } : { startAfter: after })),
        limit: Math.min(1000, limit),
        reverse,
      });
      return new Map(rows);
    },
  }));
}

export const SOCIAL_DATABASE_BUDGET_BYTES = 900_000_000;

export function durableSocialTransaction(storage) {
  if (!storage.sql || typeof storage.transactionSync !== 'function') return null;
  storage.sql.exec('CREATE TABLE IF NOT EXISTS social_records (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID');
  const transaction = socialSqlTransaction(storage.sql, { beforePut() {
    const size = storage.sql.databaseSize;
    if (!Number.isSafeInteger(size) || size < 0) throw new SocialStorageError('unavailable');
    if (size >= SOCIAL_DATABASE_BUDGET_BYTES) throw new SocialStorageError('full');
  } });
  return (fn) => {
    try {
      return storage.transactionSync(() => transaction(fn));
    } catch (error) {
      if (Number.isInteger(error?.status) && typeof error?.code === 'string') throw error;
      throw socialStorageError(error);
    }
  };
}

function workerEnv(state, env) {
  const keys = new Map();
  const logs = logStoreFor(state, env);
  const identity = new SqliteIdentityStore(state.storage);
  const reportTransaction = durableReportTransaction(state.storage);
  const claimModerationReceipt = (commandHash, record) => state.storage.transactionSync(() => {
    const sql = state.storage.sql;
    sql.exec('CREATE TABLE IF NOT EXISTS moderation_receipts (command_hash TEXT PRIMARY KEY, payload TEXT NOT NULL)');
    const row = sql.exec('SELECT payload FROM moderation_receipts WHERE command_hash = ?', commandHash).toArray()[0];
    if (row) return JSON.parse(row.payload);
    sql.exec('INSERT INTO moderation_receipts VALUES (?, ?)', commandHash, JSON.stringify(record));
    return null;
  });
  const completeModerationReceipt = (commandHash, record) => state.storage.transactionSync(() => {
    const sql = state.storage.sql;
    sql.exec('CREATE TABLE IF NOT EXISTS moderation_receipts (command_hash TEXT PRIMARY KEY, payload TEXT NOT NULL)');
    const row = sql.exec('SELECT payload FROM moderation_receipts WHERE command_hash = ?', commandHash).toArray()[0];
    if (!row) throw new Error('moderation command claim is missing');
    const existing = JSON.parse(row.payload);
    if (existing.status !== 'pending' || existing.fingerprint !== record.fingerprint) {
      throw new Error('moderation command claim is not pending');
    }
    sql.exec('UPDATE moderation_receipts SET payload = ? WHERE command_hash = ?', JSON.stringify(record), commandHash);
  });
  const socialTransaction = durableSocialTransaction(state.storage);
  return {
    socialStorageRequired: true,
    hmac: async (key, data) => {
      const id = Array.from(key).join(',');
      let k = keys.get(id);
      if (!k) {
        k = await subtle.importKey('raw', key, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
        keys.set(id, k);
      }
      return new Uint8Array(await subtle.sign('HMAC', k, data));
    },
    sha256: async (data) => new Uint8Array(await subtle.digest('SHA-256', data)),
    randomBytes: (n) => crypto.getRandomValues(new Uint8Array(n)),
    now: () => Date.now(),
    ...(typeof env.oauthFetch === 'function' ? { oauthFetch: env.oauthFetch } : {}),
    store: {
      saveGame: (g) => state.storage.put(`game:${g.id}`, withoutGameIp(g)),
      saveMeta: (k, v) => state.storage.put(k, v), // INFRA-2 r2: the relay uploader's status (relay-status:<address>)
      deleteGame: (id) => state.storage.delete(`game:${id}`),
      claimModerationReceipt,
      completeModerationReceipt,
      identity,
      saveEnforcement: (record) => identity.saveEnforcement(record),
      loadEnforcement: () => [],
      saveSighting: (record) => identity.saveSighting(record),
      deleteSighting: (install) => identity.deleteSighting(install),
      loadSightings: () => [],
      kind: logs.kind,
      ...(reportTransaction ? { reportTransaction } : {}),
      ...(socialTransaction ? { socialTransaction } : {}),
      maxLogBytes: logs.maxLogBytes,
      putBlob: logs.putBlob?.bind(logs),
      getBlob: logs.getBlob?.bind(logs),
      gameTimes: logs.gameTimes?.bind(logs),
      listBlobs: logs.listBlobs?.bind(logs),
    },
  };
}

// ONLINE-2 r3 (Sol r2: the body was read inside the queue, after full buffering, so a stream with no end held every later request, the health
// check included): the body is read BEFORE the request joins the queue, chunk by chunk against the route's byte limit (413 at once beyond it) and a
// read timeout (408). {bytes} or {status, code, error}
// CFDEPLOY-1 r2 (Sol r1: 20,000 empty chunks ran past the timer and still succeeded, because Promise.race picks the read that has already settled over
// the timer): the deadline is checked on every chunk, and a body of more than `maxChunks` chunks is refused (413 too_many_chunks)
export const MAX_BODY_CHUNKS = 4096; // an 8 MiB log in 2 KiB chunks; a 16 KiB JSON body in 4-byte chunks
export async function readBodyLimited(request, limit, timeoutMs, maxChunks = MAX_BODY_CHUNKS) {
  if (Number(request.headers.get('content-length') ?? 0) > limit) return { status: 413, code: 'too_big', error: `request body over ${limit} bytes` };
  if (!request.body) return { bytes: new Uint8Array(0) };
  const reader = request.body.getReader();
  const deadline = Date.now() + timeoutMs;
  const chunks = [];
  let size = 0;
  let count = 0;
  let timer;
  const timeout = new Promise((resolve) => {
    timer = setTimeout(() => resolve('timeout'), timeoutMs);
  });
  const stop = (status, code, error) => {
    reader.cancel().catch(() => {});
    return { status, code, error };
  };
  try {
    for (;;) {
      const r = await Promise.race([reader.read(), timeout]);
      if (r === 'timeout' || Date.now() > deadline) return stop(408, 'timeout', `the request body did not arrive within ${timeoutMs} ms`);
      if (r.done) break;
      count += 1;
      if (count > maxChunks) return stop(413, 'too_many_chunks', `request body in more than ${maxChunks} chunks`);
      size += r.value.length;
      if (size > limit) return stop(413, 'too_big', `request body over ${limit} bytes`);
      chunks.push(r.value);
    }
  } finally {
    clearTimeout(timer);
  }
  const bytes = new Uint8Array(size);
  let at = 0;
  for (const c of chunks) {
    bytes.set(c, at);
    at += c.length;
  }
  return { bytes };
}

// CFDEPLOY-1 r2 (Sol r1): bodies are buffered before the queue, so their number is bounded too: at most MAX_LOG_UPLOADS relay logs (up to 8 MiB each)
// and MAX_BODIES bodies of any kind are held at once (a Worker has 128 MB); more are refused at once with 503 'overloaded' (the uploader retries).
// r3 (Sol r2): "held" means until the response is built, queue wait included
export const MAX_LOG_UPLOADS = 2;
export const MAX_BODIES = 64;

export class LobbyObject {
  constructor(state, env) {
    this.lobby = new Lobby(workerEnv(state, env), configFromEnv(env));
    this.queue = Promise.resolve();
    this.state = state;
    this.identity = this.lobby.env.store.identity;
    this.identity.onEnforcementChange = () => this.lobby.social.securityStateChanged();
    this.ready = false;
    this.migrationError = null;
    this.bodyTimeoutMs = Number(env.BODY_TIMEOUT_MS ?? 10000);
    this.reading = { logs: 0, all: 0 };
    let initialization;
    const initialize = () => {
      initialization = this.initialize(5);
      return initialization;
    };
    try {
      const blocked = state.blockConcurrencyWhile(initialize);
      this.readyPromise = Promise.resolve(blocked ?? initialization);
    } catch {
      this.migrationError = 'initialization_failed';
      this.readyPromise = this.scheduleMigration().catch(() => {
        this.migrationError = 'initialization_failed';
        this.ready = false;
      });
    }
  }
  async initialize(maxPages) {
    this.ready = false;
    try {
      this.lobby.games.clear();
      this.lobby.relayStatus.clear();
      const gamePageLimit = 1000;
      let startAfter = '';
      for (;;) {
        const games = await this.state.storage.list({ prefix: 'game:', limit: gamePageLimit, startAfter });
        if (!(games instanceof Map) || games.size > gamePageLimit) throw new Error('stored game page is malformed');
        let lastKey = startAfter;
        const page = [];
        for (const [key, game] of games) {
          if (typeof key !== 'string' || !key.startsWith('game:') || key <= lastKey) {
            throw new Error('stored game page is not strictly ordered');
          }
          const id = key.slice('game:'.length);
          if (!/^[0-9a-f]{16}$/.test(id) || !game || typeof game !== 'object' || Array.isArray(game) || game.id !== id) {
            throw new Error('stored game key does not match its id');
          }
          lastKey = key;
          const clean = withoutGameIp(game);
          if (Object.hasOwn(game, 'ip')) await this.state.storage.put(key, clean);
          page.push(clean);
        }
        this.lobby.loadGames(page);
        if (games.size < gamePageLimit) break;
        if (lastKey <= startAfter) throw new Error('stored game pagination made no progress');
        startAfter = lastKey;
      }
      const relays = await this.state.storage.list({ prefix: 'relay-status:' });
      this.lobby.load([], [...relays.values()], [], []);
      for (let i = 0; i < maxPages && !this.ready; i++) {
        this.ready = await this.identity.migratePage(validateEnforcementRecord, validateSighting);
      }
      this.migrationError = null;
    } catch {
      this.migrationError = 'initialization_failed';
      this.ready = false;
    }
    if (!this.ready) {
      try {
        await this.scheduleMigration();
      } catch {
        this.migrationError = 'initialization_failed';
      }
    } else if (typeof this.state.storage.setAlarm === 'function') {
      await this.state.storage.setAlarm(Date.now() + 60000);
    }
  }
  async scheduleMigration() {
    if (typeof this.state.storage.setAlarm === 'function') await this.state.storage.setAlarm(Date.now() + 1000);
  }
  async processMigration() {
    if (this.ready) return;
    await this.initialize(10);
  }
  async alarm() {
    await this.readyPromise;
    const run = this.queue.then(async () => {
      await this.processMigration();
      if (!this.ready) return;
      try {
        await this.lobby.prepareReportExpiries?.();
      } finally {
        if (typeof this.state.storage.setAlarm === 'function') await this.state.storage.setAlarm(Date.now() + 60000);
      }
    });
    this.queue = run.catch(() => {});
    await run;
  }
  async fetch(request) {
    await this.readyPromise;
    const path = new URL(request.url).pathname;
    // INFRA-2: an admin import (POST /v1/admin/blobs/...) carries a log too: the same byte limit and upload places as the relay's uploads
    const isLog = path.startsWith('/v1/relay/logs/') || path.startsWith('/v1/admin/blobs/');
    const refuse = (status, code, message, extra = {}) => new Response(JSON.stringify({ v: 1, error: code, message }),
      { status, headers: { 'content-type': 'application/json; charset=utf-8', connection: 'close', ...extra } });
    if (!this.ready) {
      const health = request.method === 'GET' && path === '/v1/health';
      return refuse(503, 'identity_migrating', health ? 'the lobby identity store is not ready' : 'the lobby is not ready', { 'cache-control': 'no-store' });
    }
    if (request.body && (this.reading.all >= MAX_BODIES || (isLog && this.reading.logs >= MAX_LOG_UPLOADS))) {
      request.body.cancel().catch(() => {});
      return refuse(503, 'overloaded', isLog ? `${MAX_LOG_UPLOADS} log uploads are being received: retry later` : 'too many requests in flight: retry later',
        { 'retry-after': '5' });
    }
    // CFDEPLOY-1 r3 (Sol r2: capacity was released when the body was read, so six 8 MiB uploads queued behind a stalled request while the counters
    // showed zero): a place is held from here until the request's response is built, i.e. while its body waits in the queue too
    this.reading.all += 1;
    if (isLog) this.reading.logs += 1;
    let out;
    try {
      const body = await readBodyLimited(request, isLog ? this.lobby.maxLogBytes() : MAX_JSON_BYTES, this.bodyTimeoutMs);
      const reportPost = request.method === 'POST' && path === '/v1/reports';
      if (body.status && !(reportPost && body.status === 413)) return refuse(body.status, body.code, body.error);
      const run = this.queue.then(() => this.handle(request, body.bytes ?? new Uint8Array(), body.status && reportPost
        ? { status: body.status, code: body.code, message: body.error }
        : null));
      this.queue = run.catch(() => {});
      out = await run;
    } finally {
      this.reading.all -= 1;
      if (isLog) this.reading.logs -= 1;
    }
    // ONLINE-5: a held poll (route's `deferred`) waits outside the queue and holds no body place (it has no body); the lobby bounds the held polls
    if (out.deferred) out = await out.deferred;
    return new Response(out.body, { status: out.status, headers: out.headers });
  }
  async handle(request, bytes, bodyRejection = null) {
    const url = new URL(request.url);
    const headers = {};
    for (const [k, v] of request.headers) headers[k.toLowerCase()] = v;
    const out = await route(this.lobby, {
      method: request.method,
      path: url.pathname,
      query: url.searchParams,
      headers,
      ip: request.headers.get('cf-connecting-ip') ?? '',
      bytes: async () => bytes,
      signal: request.signal,
      ...(bodyRejection ? { bodyRejection } : {}),
    });
    return out;
  }
}

// ONLINECOUNT-1: GET /v1/stats without a token (the main menu's and the launcher's line) is answered from the data centre's Cache API for STATS_CACHE_S
// seconds, so those requests do not reach the Durable Object (its own 100,000-a-day free allowance); they still count as Worker requests (the Worker runs
// before any cache). A request with a token (the online menu's, which marks the player present) always goes to the object.
// ONLINECOUNT-1 r2 (Sol: a cache hit skipped the limiter, 100 x 200 from one address): the order is admission -> cache -> the object (whose lobby limiter
// is the second line). Admission is a token bucket per address kept per Worker isolate ( separate or recycled isolates and the eviction of old buckets reset it, so it is no aggregate cap and no protection for the Worker
// request budget; it only stops one address hammering one isolate, and costs no Durable Object request). A refused request is a
// 429 with Retry-After and is never cached.
export const STATS_ADMIT_PER_MINUTE = 60;
export const STATS_ADMIT_BURST = 20;
export const MAX_ADMIT_ADDRESSES = 5000;

export class StatsAdmission {
  constructor(now = () => Date.now()) {
    this.now = now;
    this.buckets = new Map();
  }
  // true when the request may go on
  admit(address) {
    const t = this.now();
    const b = this.buckets.get(address) ?? { tokens: STATS_ADMIT_BURST, at: t };
    b.tokens = Math.min(STATS_ADMIT_BURST, b.tokens + ((t - b.at) * STATS_ADMIT_PER_MINUTE) / 60000);
    b.at = t;
    const ok = b.tokens >= 1;
    if (ok) b.tokens -= 1;
    this.buckets.delete(address); // re-inserted last: the map's order is the least recently used first
    this.buckets.set(address, b);
    while (this.buckets.size > MAX_ADMIT_ADDRESSES) this.buckets.delete(this.buckets.keys().next().value);
    return ok;
  }
}
export const statsAdmission = new StatsAdmission();

export async function statsFromCache(request, ctx, cache, ask) {
  const url = new URL(request.url);
  const key = new Request(`${url.origin}/v1/stats`, { method: 'GET' });
  const hit = await cache.match(key);
  if (hit) return hit;
  const fresh = await ask();
  if (fresh.status === 200 && ctx) ctx.waitUntil(cache.put(key, fresh.clone()));
  return fresh;
}

export default {
  async fetch(request, env, ctx) {
    // one lobby object for now (one region); docs/ONLINE.md: one per region later
    const stub = env.LOBBY.get(env.LOBBY.idFromName('lobby'));
    const url = new URL(request.url);
    if (request.method === 'GET' && url.pathname === '/v1/stats') {
      // every stats request is admitted first, signed or not, cached or not
      if (!statsAdmission.admit(request.headers.get('cf-connecting-ip') ?? '')) {
        return new Response(JSON.stringify({ v: 1, error: 'rate_limited', message: 'too many requests: wait a moment' }),
          { status: 429, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', 'retry-after': '10' } });
      }
      if (!request.headers.has('authorization') && typeof caches !== 'undefined') return statsFromCache(request, ctx, caches.default, () => stub.fetch(request));
    }
    return stub.fetch(request);
  },
};
