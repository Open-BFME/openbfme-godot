// OpenBFME online lobby, self-hosted (lane ONLINE-2): the lobby (lobby.mjs) as a plain Node HTTP service, for a home lab or a VPS beside the relay. Node 18+
// standard library only (no packages). Put TLS in front of it (a reverse proxy) before exposing it beyond a LAN: the bearer tokens travel in it.
//
// Configuration (environment; secrets also from files: X_FILE=path reads the variable from that file):
//   LOBBY_BIND (default 127.0.0.1:8787), LOBBY_DATA (a directory: games, START records, relay logs, enforcement and sightings), TICKET_KEYS (the relay's key ring),
//   SESSION_KEY, UPLOAD_KEY, ADMIN_KEY, MOD_KEY (hex; ADMIN_KEY: INFRA-2's backup export), REQUIRE_INSTALL (0 or 1), RELAYS ("id=a.b.c.d:port,..."), PUBLIC_URL
// Usage: node server.mjs   (the deploy notes run it under systemd)

import { createServer } from 'node:http';
import { createHash, createHmac, randomBytes } from 'node:crypto';
import { mkdirSync, readFileSync, renameSync, unlinkSync, readdirSync, existsSync, statSync,
  openSync, writeSync, fsyncSync, closeSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Lobby, route, configFromEnv, parseBlobKey, MAX_JSON_BYTES, MAX_LOG_BYTES, validateSighting, withoutGameIp } from './lobby.mjs';
import { createNodeReportStore } from './report-store-node.mjs';
import { createNodeSocialStore } from './social-store-node.mjs';
import { repairStoredText } from './stored-text.mjs';

export function readEnv(env = process.env) {
  const out = { ...env };
  for (const [k, v] of Object.entries(env)) {
    if (k.endsWith('_FILE') && v) out[k.slice(0, -5)] = readFileSync(v, 'utf8').trim() + (k === 'TICKET_KEYS_FILE' ? '\n' : '');
  }
  return out;
}

export function fileStore(dir) {
  for (const sub of ['games', 'starts', 'logs', 'players', 'meta', 'enforcement', 'sightings', 'reports', 'moderation-receipts']) mkdirSync(join(dir, sub), { recursive: true });
  const reports = createNodeReportStore(join(dir, 'reports'));
  const gamesDir = join(dir, 'games');
  const syncDirectory = (path) => {
    const fd = openSync(path, 'r');
    try { fsyncSync(fd); } finally { closeSync(fd); }
  };
  syncDirectory(dirname(dir));
  syncDirectory(dir);
  let removedGameTemp = false;
  for (const name of readdirSync(gamesDir)) {
    if (!/^[0-9a-f]{16}\.json(?:\.tmp|\.[0-9a-f]{24}\.tmp)$/.test(name)) continue;
    unlinkSync(join(gamesDir, name));
    removedGameTemp = true;
  }
  if (removedGameTemp) syncDirectory(gamesDir);
  const writeFully = (fd, bytes) => {
    let offset = 0;
    while (offset < bytes.length) offset += writeSync(fd, bytes, offset, bytes.length - offset);
  };
  const writeDurably = (path, data) => {
    const temporary = `${path}.${randomBytes(12).toString('hex')}.tmp`;
    let fd = null;
    let created = false;
    try {
      fd = openSync(temporary, 'wx', 0o600);
      created = true;
      writeFully(fd, typeof data === 'string' ? Buffer.from(data) : Buffer.from(data));
      fsyncSync(fd);
      closeSync(fd);
      fd = null;
      renameSync(temporary, path);
      syncDirectory(dirname(path));
    } catch (error) {
      if (fd !== null) {
        try { closeSync(fd); } catch {}
      }
      if (created && existsSync(temporary)) unlinkSync(temporary);
      throw error;
    }
  };
  const readModerationReceipt = (commandHash) => {
    if (!/^[0-9a-f]{64}$/.test(commandHash)) throw new Error('invalid moderation command hash');
    const path = join(dir, 'moderation-receipts', `${commandHash}.json`);
    if (!existsSync(path)) return null;
    return JSON.parse(readFileSync(path, 'utf8'));
  };
  const claimModerationReceipt = (commandHash, record) => {
    if (!/^[0-9a-f]{64}$/.test(commandHash)) throw new Error('invalid moderation command hash');
    const path = join(dir, 'moderation-receipts', `${commandHash}.json`);
    let fd = null;
    try {
      fd = openSync(path, 'wx', 0o600);
      writeFully(fd, Buffer.from(JSON.stringify(record)));
      fsyncSync(fd);
      closeSync(fd);
      fd = null;
      syncDirectory(dirname(path));
      return null;
    } catch (error) {
      if (fd !== null) closeSync(fd);
      if (error.code === 'EEXIST') return readModerationReceipt(commandHash);
      throw error;
    }
  };
  const completeModerationReceipt = (commandHash, record) => {
    const path = join(dir, 'moderation-receipts', `${commandHash}.json`);
    const existing = readModerationReceipt(commandHash);
    if (!existing || existing.status !== 'pending' || existing.fingerprint !== record.fingerprint) {
      throw new Error('moderation command claim is not pending');
    }
    writeDurably(path, JSON.stringify(record));
  };
  const social = createNodeSocialStore(join(dir, 'social.sqlite'));
  const write = (path, data) => {
    writeDurably(path, data);
  };
  const loadRecords = (sub, label) => readdirSync(join(dir, sub)).filter((f) => f.endsWith('.json')).map((f) => {
    let record;
    try {
      record = JSON.parse(readFileSync(join(dir, sub, f), 'utf8'));
    } catch (error) {
      throw new Error(`lobby: stored ${label} ${f} is malformed: ${error.message}`);
    }
    if (record?.install !== f.slice(0, -5)) throw new Error(`lobby: stored ${label} ${f} has a mismatched install key`);
    if (label === 'sighting') {
      const clean = validateSighting(record);
      if (repairStoredText(record) !== record || clean.platform !== record.platform) write(join(dir, sub, f), JSON.stringify(clean));
      record = clean;
    }
    return record;
  });
  return {
    kind: 'files',
    reportTransaction: reports.reportTransaction.bind(reports),
    socialTransaction: social.socialTransaction.bind(social),
    saveGame: (g) => write(join(dir, 'games', `${g.id}.json`), JSON.stringify(withoutGameIp(g))),
    deleteGame: (id) => {
      const p = join(dir, 'games', `${id}.json`);
      if (existsSync(p)) {
        unlinkSync(p);
        syncDirectory(dirname(p));
      }
    },
    claimModerationReceipt,
    completeModerationReceipt,
    // INFRA-2 r2: the relay uploader's status (one JSON per key; not a blob, so never in the backup's listing)
    saveMeta: (k, v) => write(join(dir, 'meta', `${encodeURIComponent(k)}.json`), JSON.stringify(v)),
    loadMeta: () => readdirSync(join(dir, 'meta')).filter((f) => f.endsWith('.json')).map((f) => JSON.parse(readFileSync(join(dir, 'meta', f), 'utf8'))),
    saveEnforcement: (record) => write(join(dir, 'enforcement', `${record.install}.json`), JSON.stringify(record)),
    loadEnforcement: () => loadRecords('enforcement', 'enforcement'),
    saveSighting: (record) => write(join(dir, 'sightings', `${record.install}.json`), JSON.stringify(record)),
    deleteSighting: (install) => {
      const p = join(dir, 'sightings', `${install}.json`);
      if (existsSync(p)) unlinkSync(p);
    },
    loadSightings: () => loadRecords('sightings', 'sighting'),
    loadGames: () => readdirSync(join(dir, 'games')).filter((f) => f.endsWith('.json')).map((f) => {
      const path = join(dir, 'games', f);
      const game = JSON.parse(readFileSync(path, 'utf8'));
      if (!game || game.id !== f.slice(0, -5)) throw new Error(`lobby: stored game ${f} has a mismatched key`);
      const repaired = repairStoredText(game);
      const clean = withoutGameIp(repaired);
      if (repaired !== game || Object.hasOwn(game, 'ip')) write(path, JSON.stringify(clean));
      return clean;
    }),
    putBlob: (key, bytes) => write(join(dir, key), bytes),
    getBlob: (key) => (existsSync(join(dir, key)) ? new Uint8Array(readFileSync(join(dir, key))) : null),
    // LOBBYSTATS-1: when the START records and relay logs written since `sinceMs` were stored (their files' mtimes, ms), for /v1/stats' games per day
    gameTimes: (sinceMs) => {
      const times = (sub) => readdirSync(join(dir, sub)).filter((f) => parseBlobKey(`${sub}/${f}`))
        .map((f) => Math.floor(statSync(join(dir, sub, f)).mtimeMs)).filter((t) => t >= sinceMs);
      return { started: times('starts'), finished: times('logs') };
    },
    // INFRA-2: the admin backup's listing, in key order after `after` (the keys lobby.mjs's parseBlobKey accepts)
    listBlobs: (after, limit) => ['logs', 'players', 'starts'].flatMap((sub) => readdirSync(join(dir, sub)).map((f) => `${sub}/${f}`))
      .filter((k) => parseBlobKey(k) && k > after).sort().slice(0, limit)
      .map((key) => {
        const st = statSync(join(dir, key));
        return { key, size: st.size, storedAt: Math.floor(st.mtimeMs) };
      }),
  };
}

export function nodeEnv(store) {
  return {
    hmac: async (key, data) => new Uint8Array(createHmac('sha256', key).update(data).digest()),
    sha256: async (data) => new Uint8Array(createHash('sha256').update(data).digest()),
    randomBytes: (n) => new Uint8Array(randomBytes(n)),
    now: () => Date.now(),
    store,
  };
}

export function startServer(env = readEnv(), { log = console.log } = {}) {
  const dataDir = env.LOBBY_DATA;
  if (!dataDir) throw new Error('LOBBY_DATA (a directory) is required');
  const store = fileStore(dataDir);
  const lobby = new Lobby(nodeEnv(store), configFromEnv(env));
  lobby.load(store.loadGames(), store.loadMeta(), store.loadEnforcement(), store.loadSightings());
  const [host, port] = String(env.LOBBY_BIND ?? '127.0.0.1:8787').split(':');
  // one request at a time through the lobby (as a Durable Object serialises them)
  let chain = Promise.resolve();
  const server = createServer((req, res) => {
    // ONLINE-2 r2 (Sol: a chunked 20 KB JSON body passed the 16 KiB check): the bytes are counted as they arrive; beyond the route's limit the
    // request is answered 413 at once and the rest is not read
    const limit = req.url.startsWith('/v1/relay/logs/') || req.url.startsWith('/v1/admin/blobs/') ? MAX_LOG_BYTES : MAX_JSON_BYTES;
    // ONLINE-2 r4 (Sol r3): a body-only TOTAL deadline (not an inactivity timer: a dripping body must not outlast it), started with the request
    // and cleared once the body is complete or refused, so a complete request is never cut while the lobby handles it
    let refused = false;
    const bodyMs = Number(env.BODY_TIMEOUT_MS ?? 10000);
    const deadline = setTimeout(() => {
      refused = true;
      if (!res.headersSent) {
        res.writeHead(408, { 'content-type': 'application/json; charset=utf-8', connection: 'close' });
        res.end(JSON.stringify({ v: 1, error: 'timeout', message: `the request body did not arrive within ${bodyMs} ms` }), () => req.destroy());
      } else {
        req.destroy();
      }
    }, bodyMs);
    const chunks = [];
    let size = 0;
    const rejectedReport = () => {
      if (refused || req.method !== 'POST' || req.url.split('?')[0] !== '/v1/reports') return false;
      refused = true;
      clearTimeout(deadline);
      const url = new URL(req.url, 'http://lobby');
      const routed = chain.then(() => route(lobby, {
        method: req.method,
        path: url.pathname,
        query: url.searchParams,
        headers: req.headers,
        ip: req.socket.remoteAddress ?? '',
        bytes: async () => new Uint8Array(),
        bodyRejection: { status: 413, code: 'too_big', message: `request body over ${limit} bytes` },
      }));
      chain = routed.catch(() => {});
      routed.then((out) => out.deferred ?? out).then((out) => {
        res.writeHead(out.status, out.headers);
        res.end(typeof out.body === 'string' ? out.body : Buffer.from(out.body), () => req.destroy());
      }, (error) => {
        res.writeHead(500);
        res.end();
        log(`lobby: ${error.stack ?? error}`);
      });
      req.pause();
      return true;
    };
    if (Number(req.headers['content-length'] ?? 0) > limit) rejectedReport();
    req.on('data', (c) => {
      size += c.length;
      if (size > limit && !refused) {
        if (!rejectedReport()) {
          refused = true;
          clearTimeout(deadline);
          res.writeHead(413, { 'content-type': 'application/json; charset=utf-8', connection: 'close' });
          res.end(JSON.stringify({ v: 1, error: 'too_big', message: `request body over ${limit} bytes` }), () => req.destroy());
        }
      } else if (!refused) {
        chunks.push(c);
      }
    });
    req.on('close', () => clearTimeout(deadline));
    req.on('end', () => {
      clearTimeout(deadline);
      if (refused) return;
      const raw = Buffer.concat(chunks);
      const url = new URL(req.url, 'http://lobby');
      const r = {
        method: req.method,
        path: url.pathname,
        query: url.searchParams,
        headers: req.headers,
        ip: req.socket.remoteAddress ?? '',
        bytes: async () => new Uint8Array(raw),
      };
      const controller = new AbortController();
      r.signal = controller.signal;
      req.once('aborted', () => controller.abort());
      res.once('close', () => { if (!res.writableEnded) controller.abort(); });
      // ONLINE-5: a held poll (route's `deferred`) is answered when it resolves; the chain goes on with the next request meanwhile
      const routed = chain.then(() => route(lobby, r));
      chain = routed.catch(() => {});
      routed.then((out) => out.deferred ?? out).then((out) => {
        res.writeHead(out.status, out.headers);
        res.end(typeof out.body === 'string' ? out.body : Buffer.from(out.body));
        if (out.status >= 500) log(`lobby: ${req.method} ${url.pathname} -> ${out.status}`);
      }, (e) => {
        res.writeHead(500);
        res.end();
        log(`lobby: ${e.stack ?? e}`);
      });
    });
  });
  return new Promise((resolve) => server.listen(Number(port), host, () => {
    log(`openbfme lobby: listening on ${host}:${server.address().port}, data ${dataDir}, relays ${env.RELAYS}`);
    resolve({ server, lobby, port: server.address().port });
  }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  startServer().catch((e) => {
    console.error(`openbfme lobby: ${e.message}`);
    process.exit(1);
  });
  for (const s of ['SIGTERM', 'SIGINT']) process.on(s, () => process.exit(0));
}
