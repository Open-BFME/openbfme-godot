#!/usr/bin/env node
/*
 * Real-backend fixture for the launcher OAuth regression.
 *
 * It starts the production HTTP lobby and a separate loopback control server.
 * The control API is intentionally tiny:
 *   GET  /health
 *   GET  /stats
 *   GET  /authorize?url=<authorization URL>
 *   POST /fault/next-exchange
 *   POST /callback
 *
 * /authorize is the only browser emulation surface. It validates the exact
 * provider URL produced by oauth.mjs, creates one single-use code, and returns
 * the fixed loopback callback URL. /fault/next-exchange makes the next token
 * request fail so the launcher can exercise its provider-error cleanup path.
 */

import { createServer } from 'node:http';
import { createHash, randomBytes } from 'node:crypto';
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { startServer } from '../src/server.mjs';

const REDIRECT_URI = 'http://127.0.0.1:43827/oauth/callback';
const PROVIDERS = {
  discord: {
    authorization: 'https://discord.com/oauth2/authorize',
    token: 'https://discord.com/api/oauth2/token',
    profile: 'https://discord.com/api/users/@me',
    scope: 'identify',
    prompt: 'consent',
    client: 'launcher-fixture-discord-client',
    secret: 'launcher-fixture-discord-secret',
    subject: '900000000000001',
  },
};

const args = new Map();
for (let i = 2; i < process.argv.length; i += 1) {
  const value = process.argv[i];
  if (value.startsWith('--data-dir=')) args.set('data', value.slice(11));
  else if (value.startsWith('--data-dir')) args.set('data', process.argv[++i]);
}

const dataDir = args.get('data') || join(process.cwd(), 'workspace', 'launcher-oauth-fixture-data');
mkdirSync(dataDir, { recursive: true });
const stats = {
  guest_calls: 0,
  provider_calls: 0,
  provider_failures: 0,
  unexpected_provider_calls: 0,
  inbox_started: 0,
  inbox_finished: 0,
};
const codes = new Map();
const bearers = new Map();
let failNextExchange = false;
let lobbyServer;
let controlServer;

function json(res, status, value) {
  const body = JSON.stringify(value);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
    'content-length': Buffer.byteLength(body),
  });
  res.end(body);
}

function safeFailure(res, status, error) {
  json(res, status, { v: 1, ok: false, error });
}

function challenge(verifier) {
  return createHash('sha256').update(verifier).digest('base64url');
}

function providerForAuthorization(url) {
  for (const [provider, config] of Object.entries(PROVIDERS)) {
    if (url.origin + url.pathname !== config.authorization) continue;
    const expected = new Set([
      'client_id', 'response_type', 'redirect_uri', 'scope', 'state',
      'code_challenge', 'code_challenge_method', 'prompt',
    ]);
    if (url.searchParams.size !== expected.size ||
        [...url.searchParams.keys()].some((key) => !expected.has(key)) ||
        [...expected].some((key) => !url.searchParams.has(key))) {
      return null;
    }
    if (url.searchParams.get('client_id') !== config.client ||
        url.searchParams.get('response_type') !== 'code' ||
        url.searchParams.get('redirect_uri') !== REDIRECT_URI ||
        url.searchParams.get('scope') !== config.scope ||
        url.searchParams.get('code_challenge_method') !== 'S256' ||
        url.searchParams.get('prompt') !== config.prompt ||
        !/^[A-Za-z0-9_-]{43}$/.test(url.searchParams.get('code_challenge') || '') ||
        !/^[0-9a-f]{64}$/.test(url.searchParams.get('state') || '')) {
      return null;
    }
    return { provider, config, state: url.searchParams.get('state'), codeChallenge: url.searchParams.get('code_challenge') };
  }
  return null;
}

function issueAuthorization(raw) {
  let parsed;
  try {
    parsed = new URL(raw);
  } catch {
    return null;
  }
  const selected = providerForAuthorization(parsed);
  if (!selected || codes.size >= 16) return null;
  const code = `fixture-${randomBytes(18).toString('base64url')}`;
  codes.set(code, {
    provider: selected.provider,
    client: selected.config.client,
    secret: selected.config.secret,
    codeChallenge: selected.codeChallenge,
    redirectUri: REDIRECT_URI,
  });
  return `${REDIRECT_URI}?code=${encodeURIComponent(code)}&state=${encodeURIComponent(selected.state)}`;
}

function providerResponse(url, options = {}) {
  let parsed;
  try {
    parsed = new URL(url);
  } catch {
    stats.unexpected_provider_calls += 1;
    throw new Error('unexpected_provider_url');
  }
  if (options.redirect !== 'error') {
    stats.unexpected_provider_calls += 1;
    throw new Error('unexpected_provider_redirect_policy');
  }
  const entry = Object.entries(PROVIDERS).find(([, config]) => url === config.token || url === config.profile);
  if (!entry) {
    stats.unexpected_provider_calls += 1;
    throw new Error('unexpected_provider_url');
  }
  const [provider, config] = entry;
  stats.provider_calls += 1;
  if (url === config.token) {
    if (options.method !== 'POST') {
      stats.unexpected_provider_calls += 1;
      throw new Error('unexpected_provider_method');
    }
    const form = new URLSearchParams(options.body || '');
    const code = form.get('code');
    const record = codes.get(code);
    codes.delete(code);
    if (!record || record.provider !== provider ||
        record.client !== config.client ||
        record.secret !== config.secret ||
        record.redirectUri !== REDIRECT_URI ||
        form.get('client_id') !== config.client ||
        form.get('client_secret') !== config.secret ||
        form.get('redirect_uri') !== REDIRECT_URI ||
        form.get('grant_type') !== 'authorization_code' ||
        challenge(form.get('code_verifier') || '') !== record.codeChallenge) {
      return new Response(JSON.stringify({ error: 'invalid_grant' }), { status: 400 });
    }
    if (failNextExchange) {
      failNextExchange = false;
      stats.provider_failures += 1;
      return new Response(JSON.stringify({ error: 'provider_failure' }), { status: 503 });
    }
    const accessToken = `fixture-access-${randomBytes(18).toString('base64url')}`;
    bearers.set(accessToken, { provider, subject: config.subject });
    return new Response(JSON.stringify({ token_type: 'Bearer', access_token: accessToken }), { status: 200 });
  }
  if (options.method !== 'GET' || !String(options.headers?.authorization || '').startsWith('Bearer ')) {
    return new Response(JSON.stringify({ error: 'invalid_request' }), { status: 400 });
  }
  const bearer = String(options.headers.authorization).slice('Bearer '.length);
  const identity = bearers.get(bearer);
  bearers.delete(bearer);
  if (!identity || identity.provider !== provider) {
    return new Response(JSON.stringify({ error: 'invalid_token' }), { status: 401 });
  }
  return new Response(JSON.stringify(provider === 'discord'
    ? { id: identity.subject }
    : { sub: identity.subject }), { status: 200 });
}

function controlHandler(req, res) {
  let url;
  try {
    url = new URL(req.url, 'http://127.0.0.1');
  } catch {
    return safeFailure(res, 400, 'bad_url');
  }
  if (req.method === 'GET' && url.pathname === '/health') {
    return json(res, 200, { v: 1, ok: true });
  }
  if (req.method === 'GET' && url.pathname === '/stats') {
    return json(res, 200, {
      v: 1,
      stats: {
        ...stats,
        pending_codes: codes.size,
        lobby_pending: { inbox: Math.max(0, stats.inbox_started - stats.inbox_finished) },
      },
    });
  }
  if (req.method === 'POST' && url.pathname === '/callback') {
    const chunks = [];
    let bodyBytes = 0;
    let oversized = false;
    req.on('data', (part) => {
      bodyBytes += part.length;
      if (bodyBytes > 16 * 1024) {
        oversized = true;
        req.destroy();
        return;
      }
      chunks.push(part);
    });
    req.on('end', async () => {
      if (oversized) return;
      try {
        const payload = JSON.parse(Buffer.concat(chunks).toString('utf8'));
        if (!payload || typeof payload !== 'object' || Array.isArray(payload) ||
            Object.keys(payload).length !== 1 || typeof payload.url !== 'string') throw new Error('bad_callback');
        const callback = new URL(payload.url);
        if (callback.protocol !== 'http:' || callback.origin + callback.pathname !== REDIRECT_URI ||
            callback.username || callback.password || callback.hash) throw new Error('bad_callback');
        const result = await fetch(callback, { redirect: 'error', signal: AbortSignal.timeout(4000) });
        await result.text();
        json(res, result.status, { v: 1, ok: result.ok });
      } catch { safeFailure(res, 500, 'callback_failed'); }
    });
    return;
  }
  if (req.method === 'GET' && url.pathname === '/authorize') {
    const callback = issueAuthorization(url.searchParams.get('url') || '');
    if (!callback) return safeFailure(res, 400, 'authorization_rejected');
    return json(res, 200, { v: 1, callback_url: callback });
  }
  if (req.method === 'POST' && url.pathname === '/fault/next-exchange') {
    failNextExchange = true;
    return json(res, 200, { v: 1, ok: true });
  }
  return safeFailure(res, 404, 'not_found');
}

async function main() {
  const env = {
    LOBBY_BIND: '127.0.0.1:0',
    LOBBY_DATA: dataDir,
    TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
    SESSION_KEY: 'ab'.repeat(32),
    ADMIN_KEY: 'cd'.repeat(32),
    MOD_KEY: 'ef'.repeat(32),
    RELAYS: '',
    PUBLIC_URL: 'http://127.0.0.1',
    OAUTH_DISCORD_CLIENT_ID: PROVIDERS.discord.client,
    OAUTH_DISCORD_CLIENT_SECRET: PROVIDERS.discord.secret,
  };
  const result = await startServer(env, { log: () => {} });
  lobbyServer = result.server;
  lobbyServer.on('request', (req, res) => {
    const path = new URL(req.url, 'http://127.0.0.1').pathname;
    if (req.method === 'GET' && path === '/v1/inbox') {
      stats.inbox_started += 1;
      let settled = false;
      const finish = () => {
        if (settled) return;
        settled = true;
        stats.inbox_finished += 1;
      };
      res.once('finish', finish);
      res.once('close', finish);
    }
  });
  const originalGuest = result.lobby.guest.bind(result.lobby);
  result.lobby.guest = (...values) => {
    stats.guest_calls += 1;
    return originalGuest(...values);
  };
  result.lobby.env.oauthFetch = providerResponse;
  controlServer = createServer(controlHandler);
  await new Promise((resolve) => controlServer.listen(0, '127.0.0.1', resolve));
  const controlPort = controlServer.address().port;
  process.stdout.write(`${JSON.stringify({
    v: 1,
    lobby_origin: `http://127.0.0.1:${result.port}`,
    control_origin: `http://127.0.0.1:${controlPort}`,
  })}\n`);
}

function close() {
  controlServer?.close();
  lobbyServer?.close();
}
process.on('SIGINT', () => { close(); process.exit(0); });
process.on('SIGTERM', () => { close(); process.exit(0); });
main().catch(() => {
  close();
  process.exit(1);
});
