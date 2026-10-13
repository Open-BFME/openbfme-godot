// OpenBFME online lobby, self-hosted (lane ONLINE-2): the lobby (lobby.mjs) as a plain Node HTTP service, for a home lab or a VPS beside the relay. Node 18+
// standard library only (no packages). Put TLS in front of it (a reverse proxy) before exposing it beyond a LAN: the bearer tokens travel in it.
//
// Configuration (environment; secrets also from files: X_FILE=path reads the variable from that file):
//   LOBBY_BIND (default 127.0.0.1:8787), LOBBY_DATA (a directory: games, START records and relay logs), TICKET_KEYS (the relay's key ring),
//   SESSION_KEY, UPLOAD_KEY, ADMIN_KEY (hex; ADMIN_KEY: INFRA-2's backup export), RELAYS ("id=a.b.c.d:port,..."), PUBLIC_URL
// Usage: node server.mjs   (services/online/DEPLOY.md runs it under systemd)

import { createServer } from 'node:http';
import { createHmac, randomBytes } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync, renameSync, unlinkSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Lobby, route, configFromEnv, parseBlobKey, MAX_JSON_BYTES, MAX_LOG_BYTES } from './lobby.mjs';

export function readEnv(env = process.env) {
  const out = { ...env };
  for (const [k, v] of Object.entries(env)) {
    if (k.endsWith('_FILE') && v) out[k.slice(0, -5)] = readFileSync(v, 'utf8').trim() + (k === 'TICKET_KEYS_FILE' ? '\n' : '');
  }
  return out;
}

export function fileStore(dir) {
  for (const sub of ['games', 'starts', 'logs', 'players', 'meta']) mkdirSync(join(dir, sub), { recursive: true });
  const write = (path, data) => {
    writeFileSync(`${path}.tmp`, data);
    renameSync(`${path}.tmp`, path);
  };
  return {
    kind: 'files',
    saveGame: (g) => write(join(dir, 'games', `${g.id}.json`), JSON.stringify(g)),
    deleteGame: (id) => {
      const p = join(dir, 'games', `${id}.json`);
      if (existsSync(p)) unlinkSync(p);
    },
    // INFRA-2 r2: the relay uploader's status (one JSON per key; not a blob, so never in the backup's listing)
    saveMeta: (k, v) => write(join(dir, 'meta', `${encodeURIComponent(k)}.json`), JSON.stringify(v)),
    loadMeta: () => readdirSync(join(dir, 'meta')).filter((f) => f.endsWith('.json')).map((f) => JSON.parse(readFileSync(join(dir, 'meta', f), 'utf8'))),
    loadGames: () => readdirSync(join(dir, 'games')).filter((f) => f.endsWith('.json')).map((f) => JSON.parse(readFileSync(join(dir, 'games', f), 'utf8'))),
    putBlob: (key, bytes) => write(join(dir, key), bytes),
    getBlob: (key) => (existsSync(join(dir, key)) ? new Uint8Array(readFileSync(join(dir, key))) : null),
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
  lobby.load(store.loadGames(), store.loadMeta());
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
    req.on('data', (c) => {
      size += c.length;
      if (size > limit && !refused) {
        refused = true;
        clearTimeout(deadline);
        res.writeHead(413, { 'content-type': 'application/json; charset=utf-8', connection: 'close' });
        res.end(JSON.stringify({ v: 1, error: 'too_big', message: `request body over ${limit} bytes` }), () => req.destroy());
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
      chain = chain.then(() => route(lobby, r)).then((out) => {
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
