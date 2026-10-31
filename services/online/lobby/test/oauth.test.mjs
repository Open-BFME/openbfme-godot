import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash, createHmac } from 'node:crypto';
import { Lobby, configFromEnv, route } from '../src/lobby.mjs';
import { createNodeSocialStore } from '../src/social-store-node.mjs';
import { nodeEnv } from '../src/server.mjs';
import { exchangeProvider, OAuthProviderFailure, pkceChallenge } from '../src/oauth.mjs';
import { oauthAddressBuckets } from '../src/social.mjs';

const CONFIG = {
  TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
  SESSION_KEY: 'c3'.repeat(32),
  ADMIN_KEY: 'a1'.repeat(32),
  MOD_KEY: 'b2'.repeat(32),
  RELAYS: '',
  PUBLIC_URL: 'http://lobby.test',
  OAUTH_DISCORD_CLIENT_ID: 'discord-test-client',
  OAUTH_DISCORD_CLIENT_SECRET: 'discord-test-secret',
};

function harness(path, provider, subject) {
  const state = { store: createNodeSocialStore(path) };
  const enforcement = new Map();
  const store = {
    socialTransaction: (fn) => state.store.socialTransaction(fn),
    saveEnforcement: (record) => enforcement.set(record.install, structuredClone(record)),
    loadEnforcement: () => [...enforcement.values()].map((record) => structuredClone(record)),
  };
  const env = nodeEnv({
    ...store,
    saveGame() {},
    deleteGame() {},
    saveSighting() {},
    deleteSighting() {},
    loadSightings: () => [],
    reportTransaction: async (fn) => fn({ get: () => null, put() {}, delete() {}, list: () => new Map() }),
  });
  env.now = () => 1_700_000_000_000;
  env.sha256 = async (bytes) => new Uint8Array(createHash('sha256').update(bytes).digest());
  env.oauthFetch = async (url, options) => {
    assert.equal(options.redirect, 'error');
    assert.ok(url.startsWith('https://'));
    if (options.method === 'POST') {
      return new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'mock-access-token' }), { status: 200 });
    }
    return new Response(JSON.stringify(provider === 'discord' ? { id: subject } : { sub: subject }), { status: 200 });
  };
  const config = configFromEnv(CONFIG);
  const makeLobby = () => new Lobby(env, config);
  return { state, env, makeLobby, enforcement };
}

async function jsonCall(lobby, method, path, { token, body } = {}) {
  const url = new URL(path, 'http://lobby.test');
  const bytes = body === undefined ? new Uint8Array() : new TextEncoder().encode(JSON.stringify(body));
  const result = await route(lobby, {
    method,
    path: url.pathname,
    query: url.searchParams,
    headers: token ? { authorization: `Bearer ${token}` } : {},
    ip: '198.51.100.6',
    bytes: async () => bytes,
  });
  const response = result.deferred ? await result.deferred : result;
  return { status: response.status, body: JSON.parse(response.body) };
}

async function befriend(lobby, alice, bob) {
  const target = await jsonCall(lobby, 'GET', '/v1/me', { token: bob.token });
  assert.equal(target.status, 200);
  const request = await jsonCall(lobby, 'POST', '/v1/friends/requests', {
    token: alice.token, body: { v: 1, code: target.body.code },
  });
  assert.equal(request.status, 200);
  const accepted = await jsonCall(lobby, 'POST', `/v1/friends/requests/${request.body.request.id}/accept`, {
    token: bob.token, body: { v: 1 },
  });
  assert.equal(accepted.status, 200);
}

// ONLINEMENU-1 (PR #8 finding 2): link starts and unlinks carry the install the session was issued to (the token no longer shows it)
async function installOf(lobby, token) {
  return (await lobby.account(`Bearer ${token}`)).install;
}

async function complete(lobby, env, provider, mode, { token, install, name = 'Restored', client, recovery } = {}) {
  const suffix = provider === 'discord' ? '1' : '2';
  const state = suffix.repeat(64);
  const verifier = `v${suffix}`.repeat(22).slice(0, 44);
  const challenge = await pkceChallenge(env, verifier);
  const body = mode === 'link' || mode === 'confirm'
    ? { v: 1, provider, mode, state, code_challenge: challenge, install: await installOf(lobby, token), ...(recovery ? { recovery } : {}) }
    : { v: 1, provider, mode, state, code_challenge: challenge, name, install, ...(client ? { client } : {}) };
  const started = await lobby.social.oauthStart(body, {
    ...(token ? { authorization: `Bearer ${token}`, hasAuthorization: true } : {}),
    ip: '198.51.100.4',
  });
  const authorization = new URL(started.authorization_url);
  assert.equal(authorization.searchParams.get('prompt'), provider === 'discord' ? 'consent' : 'select_account');
  const finished = await lobby.social.oauthFinish({
    v: 1, transaction: started.transaction, state, code: 'mock-authorization-code', code_verifier: verifier,
  }, {
    ...(token ? { authorization: `Bearer ${token}`, hasAuthorization: true } : {}),
    ip: '198.51.100.4',
  });
  return finished.deferred;
}

async function oauthQuotaIdentity(scope, index, lobby, ip) {
  if (scope === 'global') return 'all';
  if (scope === 'ip') return lobby.social.hash('oauth2:ip', ip);
  if (scope === 'install') return lobby.social.hash('oauth2:install', index.toString(16).padStart(32, '0'));
  return index.toString(16).padStart(16, '0');
}

function oauthQuotaRecordKey(operation, scope, identity, bucket) {
  return `oauth2:quota:${operation}:${scope}:${identity}:${bucket}`;
}

function oauthQuotaExpiryKey(expires, operation, scope, identity, bucket) {
  return `oauth2:expiry:${String(expires).padStart(16, '0')}:quota:${operation}:${scope}:${identity}:${bucket}`;
}

async function routeJson(lobby, method, path, body, ip = '198.51.100.80') {
  const url = new URL(path, 'http://lobby.test');
  const bytes = new TextEncoder().encode(JSON.stringify(body));
  const response = await route(lobby, {
    method,
    path: url.pathname,
    query: url.searchParams,
    headers: {},
    ip,
    bytes: async () => bytes,
  });
  return { ...response, body: JSON.parse(response.body) };
}

async function identitySnapshot(state, pids, installs) {
  return state.store.socialTransaction((tx) => {
    const players = pids.map((pid) => tx.get(`player:${pid}`));
    return {
      players,
      metadata: pids.map((pid) => tx.get(`oauth2:player:${pid}`)),
      subjects: [...tx.list('oauth2:subject:', { limit: 100 })],
      installs: installs.map((install) => tx.get(`install:${install}`)),
      recoveries: players.map((player) => player?.recovery_hash
        ? tx.get(`recovery:${player.recovery_hash}`)
        : null),
    };
  });
}

async function startRestore(lobby, env, provider, stateValue, verifier, install, options = {}) {
  return lobby.social.oauthStart({
    v: 1,
    provider,
    mode: 'restore',
    state: stateValue,
    code_challenge: await pkceChallenge(env, verifier),
    name: options.name ?? 'Restored',
    install,
    ...(options.device ? { device: options.device } : {}),
  }, { ip: options.ip ?? '198.51.100.81' });
}

async function finishRestore(lobby, started, stateValue, verifier, code, ip = '198.51.100.81') {
  return lobby.social.oauthFinish({
    v: 1,
    transaction: started.transaction,
    state: stateValue,
    code,
    code_verifier: verifier,
  }, { ip });
}

function gatedOAuthFetch(env, fetcher) {
  const gates = new Map();
  env.oauthFetch = async (url, options) => {
    if (options.method === 'POST') {
      const code = new URLSearchParams(options.body).get('code');
      const gate = gates.get(code);
      if (gate) {
        gate.enter();
        await gate.wait;
      }
    }
    return fetcher(url, options);
  };
  return (code) => {
    let enter;
    let release;
    const entered = new Promise((resolve) => { enter = resolve; });
    const wait = new Promise((resolve) => { release = resolve; });
    gates.set(code, { entered: false, enter: () => { if (!gates.get(code).entered) { gates.get(code).entered = true; enter(); } }, wait });
    return { entered, release };
  };
}

async function signedAdminUpdate(lobby, install, body) {
  const path = `/v1/admin/mod/enforcement/${install}`;
  const bytes = new TextEncoder().encode(JSON.stringify(body));
  const seconds = String(Math.floor(lobby.env.now() / 1000));
  const prefix = new TextEncoder().encode(`PUT ${path}?\n${seconds}\n`);
  const signed = Buffer.concat([Buffer.from(prefix), Buffer.from(bytes)]);
  const signature = createHmac('sha256', Buffer.from(CONFIG.MOD_KEY, 'hex')).update(signed).digest('hex');
  return route(lobby, {
    method: 'PUT',
    path,
    query: new URLSearchParams(),
    headers: { 'x-openbfme-admin': `${seconds}.${signature}` },
    ip: '198.51.100.82',
    bytes: async () => bytes,
  });
}

async function beginGatedRestore(lobby, env, gate, stateValue, verifier, install, code, options = {}) {
  const started = await startRestore(lobby, env, 'discord', stateValue, verifier, install, options);
  const hold = gate(code);
  const finishing = await finishRestore(lobby, started, stateValue, verifier, code, options.ip);
  await hold.entered;
  return { started, finishing, hold, stateValue, verifier, code, ip: options.ip };
}

async function assertRestoreConsumed(lobby, flow) {
  await assert.rejects(
    finishRestore(lobby, flow.started, flow.stateValue, flow.verifier, flow.code, flow.ip),
    (error) => error.code === 'oauth_expired',
  );
}

for (const [provider, subject] of [['discord', '1234567890']]) {
  test(`OAuth ${provider} links and restores the same Node SQLite player`, async (t) => {
    const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-'));
    const file = join(dir, 'social.sqlite');
    const { state, env, makeLobby } = harness(file, provider, subject);
    t.after(() => {
      try { state.store.close(); } catch {}
      rmSync(dir, { recursive: true, force: true });
    });
    let lobby = makeLobby();
    const guest = await lobby.guest({ name: 'Alice', install: '1'.repeat(32) }, '198.51.100.4');
    const friend = await lobby.guest({ name: 'Bob', install: '7'.repeat(32) }, '198.51.100.4');
    await befriend(lobby, guest, friend);
    const sent = await jsonCall(lobby, 'POST', `/v1/chat/${friend.account.pid}/messages`, {
      token: guest.token, body: { v: 1, client_id: '8'.repeat(32), text: 'permanent history' },
    });
    assert.equal(sent.status, 200);
    const before = await lobby.social.me(await lobby.account(`Bearer ${guest.token}`));
    const linked = await complete(lobby, env, provider, 'link', { token: guest.token });
    assert.equal(linked.account.id, guest.account.id);
    assert.equal(linked.account.pid, guest.account.pid);
    for (const response of [linked]) {
      const serialized = JSON.stringify(response);
      for (const secret of [subject, 'mock-authorization-code', 'mock-access-token']) {
        assert.equal(serialized.includes(secret), false);
      }
    }
    assert.deepEqual(linked.providers, [provider]);
    assert.equal((await lobby.social.oauthAccounts(`Bearer ${guest.token}`)).pid, guest.account.pid);

    const newInstall = '2'.repeat(32);
    const startState = '3'.repeat(64);
    const verifier = 'x'.repeat(43);
    const challenge = await pkceChallenge(env, verifier);
    const playerCount = await state.store.socialTransaction((tx) => tx.list('player:', { limit: 10 }).size);
    const started = await lobby.social.oauthStart({
      v: 1, provider, mode: 'restore', state: startState, code_challenge: challenge,
      name: 'Alice restored', install: newInstall, client: { platform: 'linux', version: 'test' },
    }, { ip: '198.51.100.4' });
    assert.equal(started.pid, null);
    assert.equal((await state.store.socialTransaction((tx) => tx.list('player:', { limit: 10 }).size)), playerCount);
    assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${newInstall}`)), null);

    state.store.close();
    state.store = createNodeSocialStore(file);
    lobby = makeLobby();
    const restored = await (await lobby.social.oauthFinish({
      v: 1, transaction: started.transaction, state: startState,
      code: 'mock-authorization-code', code_verifier: verifier,
    }, { ip: '198.51.100.4' })).deferred;
    assert.equal(restored.account.pid, guest.account.pid);
    assert.equal(restored.account.kind, 'guest');
    assert.equal(JSON.stringify(restored).includes(subject), false);
    const restoredSession = await lobby.account(`Bearer ${restored.token}`);
    assert.equal(Object.hasOwn(restoredSession, 'device'), false);
    assert.equal((await lobby.social.me(restoredSession)).code, before.code);
    const friends = await jsonCall(lobby, 'GET', '/v1/friends', { token: restored.token });
    assert.deepEqual(friends.body.friends.map((entry) => entry.pid), [friend.account.pid]);
    const history = await jsonCall(lobby, 'GET', `/v1/chat/${friend.account.pid}/messages`, { token: restored.token });
    assert.equal(history.body.messages[0].text, 'permanent history');
    await assert.rejects(lobby.account(`Bearer ${guest.token}`), (error) => error.code === 'session_revoked');

    // ONLINEMENU-1 (Sol r2): a linked player's recovery code comes from a confirmation with the linked account
    const unlinkCode = await complete(lobby, env, provider, 'confirm', { token: restored.token });
    assert.deepEqual(Object.keys(unlinkCode).sort(), ['code', 'pid', 'providers', 'v']);
    await assert.rejects(lobby.social.recovery(restoredSession, '198.51.100.4'), (error) => error.code === 'recovery_required');
    const unlinked = await lobby.social.oauthUnlink({ v: 1, provider, install: newInstall, recovery: unlinkCode.code }, {
      authorization: `Bearer ${restored.token}`, hasAuthorization: true, ip: '198.51.100.4',
    });
    assert.deepEqual(unlinked.providers, []);
    assert.equal((await lobby.account(`Bearer ${restored.token}`)).id, restored.account.id);

    const stored = await state.store.socialTransaction((tx) => [...tx.list('oauth2:', { limit: 1000 }).values()]);
    const serialized = JSON.stringify(stored);
    for (const secret of [subject, 'mock-authorization-code', 'mock-access-token', verifier]) {
      assert.equal(serialized.includes(secret), false);
    }
    // the player's budgets are counted in memory (Sol r3), so they start again with this lobby object (made after the restart above): the
    // confirmation that made the unlink's recovery code, and the unlink
    const counts = lobby.social.oauthAddressCounts;
    assert.equal(counts.get(`finish:pid:${guest.account.pid}`).count, 1);
    assert.equal(counts.get(`start:pid:${guest.account.pid}`).count, 1);
    assert.equal(counts.get(`unlink:pid:${guest.account.pid}`).count, 1);
    assert.equal(await state.store.socialTransaction((tx) => tx.list('oauth2:quota:finish:pid:', { limit: 10 }).size), 1); // the restore target's
  });
}

test('OAuth quota boundaries honor every operation/scope limit and reject malformed route floods', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-quotas-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  await state.store.socialTransaction((tx) => {
    tx.put('oauth2:schema', 1);
    tx.put('oauth2:clock', 0);
  });
  let now = 1_700_000_000;
  let caseIndex = 0;
  const limits = {
    start: { window: 600, global: 10000, ip: 30, install: 10, pid: 20 },
    finish: { window: 600, global: 20000, ip: 30, install: 20, pid: 40 },
    unlink: { window: 86400, global: 10000, ip: 20, install: 10, pid: 20 },
  };
  for (const [operation, scopes] of Object.entries(limits)) {
    for (const scope of ['global', 'ip', 'install', 'pid']) {
      const limit = scopes[scope];
      const window = scopes.window;
      env.now = () => now * 1000;
      const ip = `198.51.100.${20 + caseIndex}`;
      const account = scope === 'install'
        ? { install: caseIndex.toString(16).padStart(32, '0') }
        : scope === 'pid'
          ? { pid: caseIndex.toString(16).padStart(16, '0') }
          : null;
      const identity = await oauthQuotaIdentity(scope, caseIndex, lobby, ip);
      const bucket = Math.floor(now / window);
      const expires = (bucket + 1) * window;
      const key = oauthQuotaRecordKey(operation, scope, identity, bucket);
      const expiry = oauthQuotaExpiryKey(expires, operation, scope, identity, bucket);
      const attempt = () => lobby.social.chargeOAuth(
        operation,
        scope === 'ip' ? ip : null,
        account,
        { global: scope === 'global' },
      );
      if (scope !== 'global') {
        // ONLINEMENU-1 (Sol r2, r3): the address, install and player budgets are kept in memory: no storage row at all
        for (let i = 0; i < limit; i++) await attempt();
        await assert.rejects(attempt(), (error) =>
          error.code === 'rate_limited' && error.details.retry_after === expires - now);
        assert.equal(await state.store.socialTransaction((tx) => tx.list(`oauth2:quota:${operation}:${scope}:`, { limit: 10 }).size), 0);
        now = expires;
        caseIndex += 1;
        continue;
      }
      await state.store.socialTransaction((tx) => {
        tx.put(key, { count: limit - 1, expires });
        tx.put(expiry, key);
      });
      await attempt();
      assert.equal(await state.store.socialTransaction((tx) => tx.get(key).count), limit);
      await assert.rejects(attempt(), (error) =>
        error.code === 'rate_limited' && error.details.retry_after === expires - now);
      assert.equal(await state.store.socialTransaction((tx) => tx.get(key).count), limit);
      now = expires;
      caseIndex += 1;
    }
  }

  env.now = () => now * 1000;
  const floodIp = '198.51.100.90';
  const quotaRows = () => state.store.socialTransaction((tx) => JSON.stringify([...tx.list('oauth2:', { limit: 1000 })]));
  const beforeFlood = await quotaRows();
  for (let attempt = 0; attempt < 30; attempt++) {
    const response = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', { v: 1 }, floodIp);
    assert.equal(response.status, 400);
  }
  const bucket = Math.floor(now / 600);
  const expires = (bucket + 1) * 600;
  // ONLINEMENU-1 (Sol r2): the flood wrote nothing (address budgets in memory; no global, daily or address rows)
  assert.equal(await quotaRows(), beforeFlood);
  // ONLINEMENU-1 (PR #8 fix, finding 1): malformed requests charge their address only, never a global budget
  for (const name of ['all', 'link', 'restore']) {
    assert.equal(await state.store.socialTransaction((tx) => tx.get(oauthQuotaRecordKey('start', 'global', name, bucket))), null);
  }
  const limited = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', { v: 1 }, floodIp);
  assert.equal(limited.status, 429);
  assert.equal(limited.body.retry_after, expires - now);
  assert.equal(limited.headers['retry-after'], String(expires - now));
  assert.deepEqual(await state.store.socialTransaction((tx) => ({
    players: tx.list('player:', { limit: 10 }).size,
    pending: tx.list('oauth2:pending:', { limit: 10 }).size,
    installs: tx.list('install:', { limit: 10 }).size,
  })), { players: 0, pending: 0, installs: 0 });

  now = expires;
  env.now = () => now * 1000;
  const globalBucket = Math.floor(now / 600);
  const globalExpires = (globalBucket + 1) * 600;
  const globalKey = oauthQuotaRecordKey('start', 'global', 'restore', globalBucket);
  await state.store.socialTransaction((tx) => {
    tx.put(globalKey, { count: 10000, expires: globalExpires });
    tx.put(oauthQuotaExpiryKey(globalExpires, 'start', 'global', 'restore', globalBucket), globalKey);
  });
  const globallyLimited = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {
    v: 1, provider: 'discord', mode: 'restore', state: '4'.repeat(64), code_challenge: await pkceChallenge(env, 'q'.repeat(43)),
    name: 'Late', install: 'e'.repeat(32),
  }, '198.51.100.91');
  assert.equal(globallyLimited.status, 429);
  assert.equal(globallyLimited.body.retry_after, globalExpires - now);
  assert.equal(globallyLimited.headers['retry-after'], String(globalExpires - now));
  assert.equal(await state.store.socialTransaction((tx) => tx.list('oauth2:pending:', { limit: 10 }).size), 0);
});

test('ONLINEMENU-1 (PR #8 finding 1): one address cannot turn OAuth off for everyone; IPv6 shares a /64 and a /48 budget', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-lockout-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const now = 1_700_000_000;
  env.now = () => now * 1000;
  const bucket = Math.floor(now / 600);
  const restoreBody = async (n) => ({
    v: 1, provider: 'discord', mode: 'restore', state: n.toString(16).padStart(64, '0'),
    code_challenge: await pkceChallenge(env, 'r'.repeat(43)), name: 'Someone', install: n.toString(16).padStart(32, '0'),
  });
  // the attack of the finding: empty and malformed starts from one address, far past its limit
  const attacker = '203.0.113.66';
  let refused = 0;
  for (let i = 0; i < 200; i++) {
    const response = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', i % 2 ? {} : { v: 1 }, attacker);
    if (response.status === 429) refused += 1;
    else assert.equal(response.status, 400);
  }
  assert.equal(refused, 200 - 30);
  // well-formed restore starts from the same address: refused by its own budget, and they charge nothing global
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', await restoreBody(1), attacker)).status, 429);
  const globals = await state.store.socialTransaction((tx) =>
    ['all', 'link', 'restore'].map((name) => tx.get(oauthQuotaRecordKey('start', 'global', name, bucket))));
  assert.deepEqual(globals, [null, null, null]);
  // everyone else: a restore start and a link start from other addresses still work
  const other = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', await restoreBody(2), '198.51.100.30');
  assert.equal(other.status, 200, JSON.stringify(other.body));
  const alice = await lobby.guest({ name: 'Alice', install: 'a'.repeat(32) }, '198.51.100.31');
  const link = await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: '5'.repeat(64), code_challenge: await pkceChallenge(env, 's'.repeat(43)), install: 'a'.repeat(32),
  }, { authorization: `Bearer ${alice.token}`, hasAuthorization: true, ip: '198.51.100.31' });
  assert.match(link.transaction, /^[0-9a-f]{48}$/);
  // a restore flood that fills the restore budget leaves linking's budget alone
  await state.store.socialTransaction((tx) => {
    const key = oauthQuotaRecordKey('start', 'global', 'restore', bucket);
    tx.put(key, { count: 10000, expires: (bucket + 1) * 600 });
    tx.put(oauthQuotaExpiryKey((bucket + 1) * 600, 'start', 'global', 'restore', bucket), key);
  });
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', await restoreBody(3), '198.51.100.32')).status, 429);
  const bob = await lobby.guest({ name: 'Bob', install: 'b'.repeat(32) }, '198.51.100.33');
  await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: '6'.repeat(64), code_challenge: await pkceChallenge(env, 't'.repeat(43)), install: 'b'.repeat(32),
  }, { authorization: `Bearer ${bob.token}`, hasAuthorization: true, ip: '198.51.100.33' });
  // IPv6: every address of one /64 is one budget (a host rotates through its /64 freely)
  for (let i = 0; i < 30; i++) {
    assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {}, `2001:db8:7:1::${(i + 1).toString(16)}`)).status, 400);
  }
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {}, '2001:db8:7:1:ffff:1:2:3')).status, 429);
  // ... and the /64s of one /48 share a wider budget (4 x 30)
  for (let net = 2; net <= 4; net++) {
    for (let i = 0; i < 30; i++) {
      assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {}, `2001:db8:7:${net}::${(i + 1).toString(16)}`)).status, 400);
    }
  }
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {}, '2001:db8:7:5::1')).status, 429);
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', {}, '2001:db8:8:5::1')).status, 400);
});

test('ONLINEMENU-1 (Sol r2 on PR #8): OAuth\'s storage work is capped per day, and refused or malformed requests write nothing', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-daily-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const now = 1_700_000_000;
  env.now = () => now * 1000;
  // count every storage write (puts and deletes are both Free-plan row writes)
  let writes = 0;
  const transact = state.store.socialTransaction.bind(state.store);
  state.store.socialTransaction = (fn) => transact((tx) => fn(new Proxy(tx, {
    get(target, property) {
      if (property === 'put' || property === 'delete') return (...args) => { writes += 1; return target[property](...args); };
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
  })));
  // the review's attack: malformed starts and finishes from four /64s of one /48, and from IPv4 addresses
  writes = 0;
  for (let net = 1; net <= 4; net++) {
    for (let i = 0; i < 40; i++) {
      for (const path of ['/v1/auth/oauth/start', '/v1/auth/oauth/finish']) {
        const response = await routeJson(lobby, 'POST', path, {}, `2001:db8:9:${net}::${(i + 1).toString(16)}`);
        assert.ok([400, 429].includes(response.status));
      }
    }
  }
  for (let i = 0; i < 100; i++) await routeJson(lobby, 'POST', '/v1/auth/oauth/start', { v: 1 }, `203.0.113.${i % 3}`);
  assert.equal(writes, 0);
  // well-formed restore starts are admitted up to the day's cap, then refused until the next UTC day; the cap holds across addresses
  const bucket = Math.floor(now / 86400);
  const dayKey = oauthQuotaRecordKey('start', 'day', 'restore', bucket);
  await state.store.socialTransaction((tx) => {
    tx.put('oauth2:schema', 1);
    tx.put('oauth2:clock', 0);
    tx.put(dayKey, { count: 399, expires: (bucket + 1) * 86400 });
    tx.put(oauthQuotaExpiryKey((bucket + 1) * 86400, 'start', 'day', 'restore', bucket), dayKey);
  });
  const restoreBody = async (n) => ({
    v: 1, provider: 'discord', mode: 'restore', state: n.toString(16).padStart(64, '0'),
    code_challenge: await pkceChallenge(env, 'r'.repeat(43)), name: 'Someone', install: n.toString(16).padStart(32, '0'),
  });
  assert.equal((await routeJson(lobby, 'POST', '/v1/auth/oauth/start', await restoreBody(1), '198.51.100.50')).status, 200);
  writes = 0;
  const refused = await routeJson(lobby, 'POST', '/v1/auth/oauth/start', await restoreBody(2), '198.51.100.51');
  assert.equal(refused.status, 429);
  assert.equal(refused.body.retry_after, (bucket + 1) * 86400 - now);
  assert.equal(writes, 0);
  // linking has its own daily budget
  const alice = await lobby.guest({ name: 'Alice', install: 'a'.repeat(32) }, '198.51.100.52');
  await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: '5'.repeat(64), code_challenge: await pkceChallenge(env, 's'.repeat(43)), install: 'a'.repeat(32),
  }, { authorization: `Bearer ${alice.token}`, hasAuthorization: true, ip: '198.51.100.52' });
});

test('ONLINEMENU-1 (Sol r3 on PR #8): a fresh guest\'s malformed or refused link starts write nothing to storage', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-guestflood-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const now = 1_700_000_000;
  env.now = () => now * 1000;
  let writes = 0;
  const transact = state.store.socialTransaction.bind(state.store);
  state.store.socialTransaction = (fn) => transact((tx) => fn(new Proxy(tx, {
    get(target, property) {
      if (property === 'put' || property === 'delete') return (...args) => { writes += 1; return target[property](...args); };
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
  })));
  const bucket = Math.floor(now / 86400);
  await transact((tx) => {
    tx.put('oauth2:schema', 1);
    tx.put('oauth2:clock', 0);
    const key = oauthQuotaRecordKey('start', 'day', 'link', bucket);
    tx.put(key, { count: 600, expires: (bucket + 1) * 86400 });
    tx.put(oauthQuotaExpiryKey((bucket + 1) * 86400, 'start', 'day', 'link', bucket), key);
  });
  for (let i = 0; i < 5; i++) {
    const install = (i + 1).toString(16).padStart(32, '0');
    const guest = await lobby.guest({ name: `G${i}`, install }, `203.0.113.${i}`);
    const auth = { authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: `203.0.113.${i}` };
    writes = 0;
    await assert.rejects(lobby.social.oauthStart({ v: 1, mode: 'link' }, auth), (error) => error.code === 'bad_field');
    // a well-formed start past the day's link cap: refused, nothing written
    await assert.rejects(lobby.social.oauthStart({
      v: 1, provider: 'discord', mode: 'link', state: '5'.repeat(64), code_challenge: await pkceChallenge(env, 's'.repeat(43)), install,
    }, auth), (error) => error.code === 'rate_limited');
    await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord' }, auth), (error) => error.code === 'bad_field');
    assert.equal(writes, 0);
  }
});

test('ONLINEMENU-1 (Sol r4 on PR #8): wrong recovery-code proofs and reading the linked accounts write nothing', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-proofwrites-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const install = 'a'.repeat(32);
  const guest = await lobby.guest({ name: 'Alice', install }, '198.51.100.90');
  await lobby.social.recovery(await lobby.account(`Bearer ${guest.token}`), '198.51.100.90');
  await complete(lobby, env, 'discord', 'link', { token: guest.token });
  const fresh = await lobby.guest({ name: 'Fresh', install: 'b'.repeat(32) }, '198.51.100.91');
  let writes = 0;
  const transact = state.store.socialTransaction.bind(state.store);
  state.store.socialTransaction = (fn) => transact((tx) => fn(new Proxy(tx, {
    get(target, property) {
      if (property === 'put' || property === 'delete') return (...args) => { writes += 1; return target[property](...args); };
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
  })));
  const auth = { authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: '198.51.100.90' };
  for (let i = 0; i < 10; i++) {
    await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install, recovery: 'Z'.repeat(16) }, auth), (error) => error.code === 'bad_recovery');
  }
  assert.equal(lobby.social.oauthAddressCounts.get(`recovery-proof:${guest.account.pid}`).count, 10);
  await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install, recovery: 'Z'.repeat(16) }, auth), (error) => error.code === 'rate_limited');
  assert.deepEqual((await lobby.social.oauthAccounts(`Bearer ${fresh.token}`)).providers, []);
  assert.equal(writes, 0);
});

test('ONLINEMENU-1 (owner 2026-10-20): a ban covers the linked Discord account: it cannot be linked to another player or move one', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-identityban-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const bannedInstall = 'a'.repeat(32);
  const banned = await lobby.guest({ name: 'Banned', install: bannedInstall }, '198.51.100.92');
  await complete(lobby, env, 'discord', 'link', { token: banned.token });
  const ban = await signedAdminUpdate(lobby, bannedInstall, {
    v: 1, version: 1, block: { kind: 'ban', until: null, reason: 'cheating' }, mute_until: null, routing: '', notice: null, devices: [], block_devices: false,
  });
  assert.equal(ban.status, 200, ban.body);
  assert.ok(await state.store.socialTransaction((tx) => tx.list('oauth2:ban:discord:', { limit: 10 }).size) === 1);
  // a new player on a new PC links the same Discord account
  const fresh = await lobby.guest({ name: 'Fresh start', install: 'b'.repeat(32) }, '198.51.100.93');
  await assert.rejects(complete(lobby, env, 'discord', 'link', { token: fresh.token }), (error) => error.code === 'identity_banned');
  await assert.rejects(complete(lobby, env, 'discord', 'restore', { install: 'c'.repeat(32), name: 'Moved' }), (error) => error.code === 'identity_banned');
  // lifting the ban ends it for the identity too (no write needed)
  const lift = await signedAdminUpdate(lobby, bannedInstall, {
    v: 1, version: 2, block: null, mute_until: null, routing: '', notice: null, devices: [],
  });
  assert.equal(lift.status, 200, lift.body);
  await assert.rejects(complete(lobby, env, 'discord', 'link', { token: fresh.token }), (error) => error.code === 'oauth_conflict');
});

test('ONLINEMENU-1 (PR #8 finding 2): a stolen session token cannot take a player over', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-takeover-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '5550001112');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const ip = '198.51.100.40';
  const victimInstall = 'c'.repeat(32);
  const victim = await lobby.guest({ name: 'Victim', install: victimInstall }, ip);
  const pid = victim.account.pid;
  // the token does not show the install (the credential that signs in as the player): sealed, with a public tag
  const payload = JSON.parse(Buffer.from(victim.token.split('.')[0], 'base64url').toString());
  assert.equal(Object.hasOwn(payload, 'install'), false);
  assert.equal(victim.token.includes(victimInstall), false);
  assert.equal(JSON.stringify(payload).includes(victimInstall), false);
  assert.match(payload.ix, /^[A-Za-z0-9_-]{38}$/);
  assert.match(payload.ih, /^[0-9a-f]{32}$/);
  assert.ok(Number.isSafeInteger(payload.iat));
  assert.equal((await lobby.account(`Bearer ${victim.token}`)).install, victimInstall);
  // a token from before (the install in the clear, MAC valid) is refused: the client signs in again
  const legacyBody = Buffer.from(JSON.stringify({ ...victim.account, kind: 'guest', install: victimInstall, generation: 1, exp: 1_800_000_000 }));
  const legacyMac = createHmac('sha256', Buffer.from(CONFIG.SESSION_KEY, 'hex')).update(legacyBody).digest();
  await assert.rejects(lobby.account(`Bearer ${legacyBody.toString('base64url')}.${legacyMac.toString('base64url')}`),
    (error) => error.code === 'signed_out');

  // the attack of the finding, with the token alone: a recovery code, linking the attacker's account, unlinking the owner's
  const stolen = { authorization: `Bearer ${victim.token}`, hasAuthorization: true, ip: '203.0.113.9' };
  const guessed = 'd'.repeat(32);
  const recoveryTry = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: victim.token, body: { v: 1, install: guessed } });
  assert.equal(recoveryTry.status, 403);
  assert.equal(recoveryTry.body.error, 'install_proof');
  const recoveryBare = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: victim.token, body: { v: 1 } });
  assert.equal(recoveryBare.status, 400);
  const linkBody = { v: 1, provider: 'discord', mode: 'link', state: '7'.repeat(64), code_challenge: await pkceChallenge(env, 'u'.repeat(43)) };
  await assert.rejects(lobby.social.oauthStart(linkBody, stolen), (error) => error.code === 'bad_field');
  await assert.rejects(lobby.social.oauthStart({ ...linkBody, install: guessed }, stolen), (error) => error.code === 'install_proof');
  assert.equal(await state.store.socialTransaction((tx) => tx.list('oauth2:pending:', { limit: 10 }).size), 0);

  // the owner links Discord and keeps a recovery code; even with the install, an unlink needs that code (a second identity too: e-mail test)
  const code = (await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: victim.token, body: { v: 1, install: victimInstall } })).body.code;
  assert.match(code, /^[0-9A-HJKMNP-TV-Z]{16}$/);
  await complete(lobby, env, 'discord', 'link', { token: victim.token });
  const withInstall = { ...stolen };
  await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install: victimInstall }, withInstall), (error) => error.code === 'bad_field');
  await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install: guessed, recovery: code }, withInstall),
    (error) => error.code === 'install_proof');
  await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install: victimInstall, recovery: 'Z'.repeat(16) }, withInstall),
    (error) => error.code === 'bad_recovery');
  // a linked provider is never replaced, even with every proof
  await assert.rejects(lobby.social.oauthStart({ ...linkBody, provider: 'discord', install: victimInstall, recovery: code }, withInstall),
    (error) => error.code === 'oauth_provider_exists');
  assert.deepEqual((await lobby.social.oauthAccounts(`Bearer ${victim.token}`)).providers, ['discord']);

  // a restore with the linked account (here the owner's own, on a new PC) keeps the recovery code and tells the PC it left
  const restored = await complete(lobby, env, 'discord', 'restore', { install: 'e'.repeat(32), name: 'Victim new PC' });
  assert.equal(restored.account.pid, pid);
  await assert.rejects(lobby.account(`Bearer ${victim.token}`), (error) => error.code === 'session_revoked');
  const player = await state.store.socialTransaction((tx) => tx.get(`player:${pid}`));
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`recovery:${player.recovery_hash}`)), pid);
  const oldPc = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Victim', install: victimInstall } });
  assert.equal(oldPc.status, 200);
  assert.notEqual(oldPc.body.account.pid, pid);
  assert.match(oldPc.body.notice.id, /^moved:\d+$/);
  assert.match(oldPc.body.notice.text, /moved to another PC on .* UTC with a linked Discord account/);
  assert.equal(JSON.stringify(oldPc.body.notice).includes('e'.repeat(32)), false);
  const again = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Victim', install: victimInstall } });
  assert.equal(again.body.notice, undefined);
  // the owner takes the player back with the recovery code it kept
  const back = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Victim', install: 'f'.repeat(32), recover: code } });
  assert.equal(back.status, 200);
  assert.equal(back.body.account.pid, pid);
});

test('ONLINEMENU-1 (Sol r2 on PR #8): the session and the install cannot remake the recovery code of a linked player', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-regenerate-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '5550001112');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const ownerInstall = 'c'.repeat(32);
  const owner = await lobby.guest({ name: 'Owner', install: ownerInstall }, '198.51.100.60');
  const pid = owner.account.pid;
  // the review's chain, with the session and the install: a new code, then unlink, link one's own, restore elsewhere
  const code = (await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: owner.token, body: { v: 1, install: ownerInstall } })).body.code;
  await complete(lobby, env, 'discord', 'link', { token: owner.token });
  const remake = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: owner.token, body: { v: 1, install: ownerInstall } });
  assert.equal(remake.status, 403);
  assert.equal(remake.body.error, 'recovery_required');
  const wrong = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: owner.token, body: { v: 1, install: ownerInstall, recovery: 'Z'.repeat(16) } });
  assert.equal(wrong.status, 403);
  assert.equal(wrong.body.error, 'bad_recovery');
  // a wrong code's try is counted (Sol r2), in memory (Sol r4: no storage write)
  assert.equal(lobby.social.oauthAddressCounts.get(`recovery-proof:${pid}`).count, 1);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`quota:recovery-proof:${pid}`)), null);
  const player = await state.store.socialTransaction((tx) => tx.get(`player:${pid}`));
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`recovery:${player.recovery_hash}`)), pid);
  // the owner, with the current code, gets a new one
  const rotated = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: owner.token, body: { v: 1, install: ownerInstall, recovery: code } });
  assert.equal(rotated.status, 200);
  assert.notEqual(rotated.body.code, code);

  // a linked player without a code: only a confirmation with the linked account makes one, and only with that account
  const linkedOnly = await lobby.guest({ name: 'Linked', install: 'd'.repeat(32) }, '198.51.100.61');
  env.oauthFetch = async (url, options) => options.method === 'POST'
    ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'mock-access-token' }), { status: 200 })
    : new Response(JSON.stringify({ id: '7770001112' }), { status: 200 });
  await complete(lobby, env, 'discord', 'link', { token: linkedOnly.token });
  const none = await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: linkedOnly.token, body: { v: 1, install: 'd'.repeat(32) } });
  assert.equal(none.status, 403);
  assert.equal(none.body.error, 'provider_confirm_required');
  env.oauthFetch = async (url, options) => options.method === 'POST'
    ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'mock-access-token' }), { status: 200 })
    : new Response(JSON.stringify({ id: '9990001112' }), { status: 200 }); // someone else's Discord account
  await assert.rejects(complete(lobby, env, 'discord', 'confirm', { token: linkedOnly.token }), (error) => error.code === 'oauth_not_this_player');
  env.oauthFetch = async (url, options) => options.method === 'POST'
    ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'mock-access-token' }), { status: 200 })
    : new Response(JSON.stringify({ id: '7770001112' }), { status: 200 });
  const confirmed = await complete(lobby, env, 'discord', 'confirm', { token: linkedOnly.token });
  assert.match(confirmed.code, /^[0-9A-HJKMNP-TV-Z]{16}$/);
  assert.equal(confirmed.pid, linkedOnly.account.pid);
  assert.equal(JSON.stringify(confirmed).includes('7770001112'), false);
  // a confirmation is only for a provider that is linked
  const unlinked = await lobby.guest({ name: 'Unlinked', install: '9'.repeat(32) }, '198.51.100.62');
  const confirmBody = { v: 1, provider: 'discord', mode: 'confirm', state: '8'.repeat(64), code_challenge: await pkceChallenge(env, 'g'.repeat(43)), install: '9'.repeat(32) };
  await assert.rejects(lobby.social.oauthStart(confirmBody, { authorization: `Bearer ${unlinked.token}`, hasAuthorization: true, ip: '198.51.100.62' }),
    (error) => error.code === 'oauth_not_linked');
});

test('ONLINEMENU-1 (Sol r2 on PR #8): the PC a player was moved from can take it back with the recovery code, and notices are not lost', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-takeback-'));
  const { state, env, makeLobby, enforcement } = harness(join(dir, 'social.sqlite'), 'discord', '5550001112');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const oldPc = 'c'.repeat(32);
  const owner = await lobby.guest({ name: 'Owner', install: oldPc }, '198.51.100.70');
  const pid = owner.account.pid;
  const code = (await jsonCall(lobby, 'POST', '/v1/me/recovery', { token: owner.token, body: { v: 1, install: oldPc } })).body.code;
  await complete(lobby, env, 'discord', 'link', { token: owner.token });
  await complete(lobby, env, 'discord', 'restore', { install: 'e'.repeat(32), name: 'Elsewhere' });
  // the old PC signs in: the notice, and a new empty player there
  const told = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Owner', install: oldPc } });
  assert.match(told.body.notice.id, /^moved:/);
  assert.notEqual(told.body.account.pid, pid);
  // ... which gives the install up to the original player's recovery
  const back = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Owner', install: oldPc, recover: code } });
  assert.equal(back.status, 200, JSON.stringify(back.body));
  assert.equal(back.body.account.pid, pid);
  // the placeholder is retired in place (Sol r3: others' blocks and recent games may name it): hidden, on an install no one holds
  const retired = await state.store.socialTransaction((tx) => tx.get(`player:${told.body.account.pid}`));
  assert.equal(retired.retired, true);
  assert.equal(retired.hidden, true);
  assert.notEqual(retired.install, oldPc);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${retired.install}`)), told.body.account.pid);
  // a placeholder that made a friend is a real player now: it keeps its install
  const later = await complete(lobby, env, 'discord', 'restore', { install: 'f'.repeat(32), name: 'Again' });
  assert.equal(later.account.pid, pid);
  const second = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Owner', install: oldPc } });
  const friend = await lobby.guest({ name: 'Friend', install: '7'.repeat(32) }, '198.51.100.71');
  await befriend(lobby, second.body, friend);
  const busy = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Owner', install: oldPc, recover: 'Z'.repeat(16) } });
  assert.equal(busy.status, 401); // a wrong code first: nothing changes
  assert.equal((await state.store.socialTransaction((tx) => tx.get(`install:${oldPc}`))), second.body.account.pid);
  // a moderation notice and the moved notice are shown together (the moved one is used up by this sign-in)
  await complete(lobby, env, 'discord', 'restore', { install: '8'.repeat(32), name: 'Third' });
  const getEnforcement = lobby.getEnforcement.bind(lobby);
  lobby.getEnforcement = (install) => install === 'f'.repeat(32)
    ? { install, v: 1, version: 1, block: null, mute_until: null, routing: '', notice: { id: 'N-1', text: 'Be kind in chat.' }, devices: [] }
    : getEnforcement(install);
  const both = await jsonCall(lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Again', install: 'f'.repeat(32) } });
  assert.equal(both.status, 200, JSON.stringify(both.body));
  assert.match(both.body.notice.id, /^N-1\+moved:\d+$/);
  assert.ok(both.body.notice.text.includes('Be kind in chat.') && both.body.notice.text.includes('moved to another PC'));
  assert.ok(Array.from(both.body.notice.text).length <= 2000 && both.body.notice.id.length <= 128);
  void enforcement;
});

test('ONLINEMENU-1: OAuth address buckets (IPv4, IPv4-mapped in both forms, IPv6 /64 and /48)', () => {
  assert.deepEqual(oauthAddressBuckets('192.0.2.1'), { ip: '192.0.2.1', net: null });
  assert.deepEqual(oauthAddressBuckets('::ffff:192.0.2.1'), { ip: '192.0.2.1', net: null });
  assert.deepEqual(oauthAddressBuckets('::ffff:c000:201'), { ip: '192.0.2.1', net: null }); // Sol r2: the hex form was one bucket for all
  assert.notDeepEqual(oauthAddressBuckets('::ffff:c633:6402'), oauthAddressBuckets('::ffff:c000:201'));
  assert.deepEqual(oauthAddressBuckets('[2001:DB8:1:2::9%eth0]'), { ip: 'v6/64:2001:db8:1:2', net: 'v6/48:2001:db8:1' });
  assert.deepEqual(oauthAddressBuckets('2001:db8:1:2:3:4:5:6'), oauthAddressBuckets('2001:db8:1:2::ffff'));
});

test('OAuth expiry cleanup is limited to bounded pages and preserves active quotas', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-cleanup-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const now = 1_700_000_000;
  env.now = () => now * 1000;
  const bucketNow = Math.floor(now / 600);
  const activeExpires = (bucketNow + 1) * 600;
  const activeKey = oauthQuotaRecordKey('finish', 'global', 'all', bucketNow);
  await state.store.socialTransaction((tx) => {
    tx.put('oauth2:schema', 1);
    tx.put('oauth2:clock', 0);
    tx.put(activeKey, { count: 7, expires: activeExpires });
    tx.put(oauthQuotaExpiryKey(activeExpires, 'finish', 'global', 'all', bucketNow), activeKey);
    for (let i = 0; i < 205; i++) {
      const bucket = bucketNow - 500 - i;
      const expires = (bucket + 1) * 600;
      const identity = i.toString(16).padStart(64, '0');
      const key = oauthQuotaRecordKey('start', 'ip', identity, bucket);
      tx.put(key, { count: 1, expires });
      tx.put(oauthQuotaExpiryKey(expires, 'start', 'ip', identity, bucket), key);
    }
  });

  const calls = [];
  const transact = state.store.socialTransaction.bind(state.store);
  state.store.socialTransaction = (fn) => transact((tx) => fn(new Proxy(tx, {
    get(target, property) {
      if (property === 'list') return (prefix, options) => {
        calls.push({ prefix, limit: options?.limit });
        return target.list(prefix, options);
      };
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
  })));
  await lobby.social.chargeOAuth('start', null);
  let remaining = await transact((tx) => tx.list('oauth2:expiry:', { limit: 1000 }).size);
  // 105 old rows left of 205, the active finish row, and this charge's global and daily start rows (ONLINEMENU-1, Sol r2: the daily cap)
  assert.equal(remaining, 108);
  await lobby.social.chargeOAuth('start', null);
  remaining = await transact((tx) => tx.list('oauth2:expiry:', { limit: 1000 }).size);
  assert.equal(remaining, 8);
  assert.ok(calls.length >= 2);
  assert.ok(calls.some(({ prefix, limit }) => prefix === 'oauth2:expiry:' && limit === 100));
  assert.ok(calls.every(({ limit }) => Number.isInteger(limit) && limit <= 100));
  assert.deepEqual(await transact((tx) => tx.get(activeKey)), { count: 7, expires: activeExpires });
});

test('OAuth conflicts and occupied restore installs preserve identity and consume each flow', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-conflicts-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const alice = await lobby.guest({ name: 'Alice', install: 'a'.repeat(32) }, '198.51.100.83');
  const bob = await lobby.guest({ name: 'Bob', install: 'b'.repeat(32) }, '198.51.100.83');
  await lobby.social.recovery(await lobby.account(`Bearer ${alice.token}`), '198.51.100.83');
  await lobby.social.recovery(await lobby.account(`Bearer ${bob.token}`), '198.51.100.83');
  const originalFetcher = env.oauthFetch;
  await complete(lobby, env, 'discord', 'link', { token: alice.token });

  const linkFlow = async (guest, stateValue, verifier) => {
    const started = await lobby.social.oauthStart({
      v: 1, provider: 'discord', mode: 'link', state: stateValue,
      code_challenge: await pkceChallenge(env, verifier), install: await installOf(lobby, guest.token),
    }, { authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: '198.51.100.83' });
    return {
      started,
      body: { v: 1, transaction: started.transaction, state: stateValue, code: `code-${stateValue[0]}`, code_verifier: verifier },
      options: { authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: '198.51.100.83' },
    };
  };
  const rejectConsumed = async (flow, expected) => {
    const finish = await lobby.social.oauthFinish(flow.body, flow.options);
    await assert.rejects(finish.deferred, (error) => error.code === expected);
    await assert.rejects(lobby.social.oauthFinish(flow.body, flow.options), (error) => error.code === 'oauth_expired');
  };

  let flow = await linkFlow(bob, 'c'.repeat(64), 'q'.repeat(43));
  let before = await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]);
  await rejectConsumed(flow, 'oauth_conflict');
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]), before);

  env.oauthFetch = async (url, options) => options.method === 'POST'
    ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'new-subject-token' }), { status: 200 })
    : new Response(JSON.stringify({ id: '9876543210' }), { status: 200 });
  // ONLINEMENU-1 (finding 2): a linked provider is never replaced; the start is refused and nothing is pending
  before = await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]);
  await assert.rejects(linkFlow(alice, 'd'.repeat(64), 'r'.repeat(43)), (error) => error.code === 'oauth_provider_exists');
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]), before);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`oauth2:pending-install:${'a'.repeat(32)}`)), null);

  env.oauthFetch = originalFetcher;
  const verifier = 's'.repeat(43);
  const restoreState = 'e'.repeat(64);
  const occupied = await startRestore(lobby, env, 'discord', restoreState, verifier, 'b'.repeat(32));
  const restoreFlow = {
    started: occupied,
    body: { v: 1, transaction: occupied.transaction, state: restoreState, code: 'occupied-install-code', code_verifier: verifier },
    options: { ip: '198.51.100.83' },
  };
  before = await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]);
  const finish = await lobby.social.oauthFinish(restoreFlow.body, restoreFlow.options);
  await assert.rejects(finish.deferred, (error) => error.code === 'install_in_use');
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['a'.repeat(32), 'b'.repeat(32)]), before);
  await assert.rejects(lobby.social.oauthFinish(restoreFlow.body, restoreFlow.options), (error) => error.code === 'oauth_expired');
});

test('restore intake rejects unsupported platforms and even an empty Authorization header', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-intake-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const verifier = 'n'.repeat(43);
  const base = {
    v: 1, provider: 'discord', mode: 'restore', state: 'c'.repeat(64),
    code_challenge: await pkceChallenge(env, verifier), name: 'Alice', install: '3'.repeat(32),
  };
  await assert.rejects(
    lobby.social.oauthStart({ ...base, client: { platform: 'handheld', version: 'test' } }, { ip: '198.51.100.8' }),
    (error) => error.code === 'bad_field',
  );
  await assert.rejects(
    lobby.social.oauthStart(base, { authorization: '', hasAuthorization: true, ip: '198.51.100.8' }),
    (error) => error.code === 'signed_out',
  );
  const response = await route(lobby, {
    method: 'POST',
    path: '/v1/auth/oauth/start',
    query: new URLSearchParams(),
    headers: {},
    ip: '198.51.100.9',
    bytes: async () => new TextEncoder().encode(JSON.stringify(base)),
  });
  assert.equal(response.status, 200);
  assert.equal(response.headers['cache-control'], 'no-store');
  const started = JSON.parse(response.body);
  const denied = await route(lobby, {
    method: 'POST',
    path: '/v1/auth/oauth/finish',
    query: new URLSearchParams(),
    headers: { authorization: '' },
    ip: '198.51.100.9',
    bytes: async () => new TextEncoder().encode(JSON.stringify({
      v: 1, transaction: started.transaction, state: base.state, code: 'mock-code', code_verifier: verifier,
    })),
  });
  assert.equal(denied.status, 401);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`oauth2:pending:${started.transaction}`)) !== null, true);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${base.install}`)), null);
});

test('provider responses are bounded and redirect failures are sanitized', async () => {
  const config = configFromEnv(CONFIG).oauth;
  const oversize = { oauthFetch: async () => new Response('x'.repeat(64 * 1024 + 1), { status: 200 }) };
  await assert.rejects(exchangeProvider(oversize, config, 'discord', 'code', 'v'.repeat(43)), OAuthProviderFailure);
  const redirect = { oauthFetch: async (_url, options) => {
    assert.equal(options.redirect, 'error');
    return new Response('', { status: 302 });
  } };
  await assert.rejects(exchangeProvider(redirect, config, 'discord', 'code', 'v'.repeat(43)), OAuthProviderFailure);
});

test('provider timeout covers a hanging response body and cancels its reader', async () => {
  let cancelled = false;
  const stalled = {
    oauthFetch: async () => new Response(new ReadableStream({
      start(controller) { controller.enqueue(new TextEncoder().encode('{"token_type":"Bearer"')); },
      cancel() { cancelled = true; },
    }), { status: 200 }),
  };
  await assert.rejects(exchangeProvider(stalled, configFromEnv(CONFIG).oauth, 'discord', 'code', 'v'.repeat(43)), OAuthProviderFailure);
  assert.equal(cancelled, true);
});

test('state and PKCE failures keep a flow usable, while provider failures consume it', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-flow-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const guest = await lobby.guest({ name: 'Alice', install: '9'.repeat(32) }, '198.51.100.10');
  const auth = { authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: '198.51.100.10' };
  const stateValue = 'a'.repeat(64);
  const verifier = 'v'.repeat(43);
  const challenge = await pkceChallenge(env, verifier);
  const started = await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: stateValue, code_challenge: challenge, install: '9'.repeat(32),
  }, auth);
  const body = {
    v: 1, transaction: started.transaction, state: stateValue, code: 'valid-code', code_verifier: verifier,
  };
  await assert.rejects(
    lobby.social.oauthFinish({ ...body, state: 'b'.repeat(64) }, auth),
    (error) => error.code === 'oauth_state',
  );
  await assert.rejects(
    lobby.social.oauthFinish({ ...body, code_verifier: 'x'.repeat(43) }, auth),
    (error) => error.code === 'oauth_verifier',
  );
  assert.notEqual(await state.store.socialTransaction((tx) => tx.get(`oauth2:pending:${started.transaction}`)), null);

  const beforeFailure = await state.store.socialTransaction((tx) => tx.get(`oauth2:player:${guest.account.pid}`));
  let providerCalls = 0;
  const fetcher = env.oauthFetch;
  env.oauthFetch = async () => { providerCalls += 1; throw new Error('synthetic provider failure'); };
  const failedFlow = await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: 'c'.repeat(64),
    code_challenge: await pkceChallenge(env, 'w'.repeat(43)), install: '9'.repeat(32),
  }, auth);
  const failedBody = {
    v: 1, transaction: failedFlow.transaction, state: 'c'.repeat(64),
    code: 'failed-code', code_verifier: 'w'.repeat(43),
  };
  const failed = await lobby.social.oauthFinish(failedBody, auth);
  await assert.rejects(failed.deferred, (error) => error.code === 'oauth_provider_unavailable');
  assert.equal(providerCalls, 1);
  assert.deepEqual(await state.store.socialTransaction((tx) => tx.get(`oauth2:player:${guest.account.pid}`)), beforeFailure);
  await assert.rejects(lobby.social.oauthFinish(failedBody, auth), (error) => error.code === 'oauth_expired');

  env.oauthFetch = fetcher;
  const expiring = await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: 'd'.repeat(64),
    code_challenge: await pkceChallenge(env, 'z'.repeat(43)), install: '9'.repeat(32),
  }, auth);
  env.now = () => 1_700_000_301_000;
  await assert.rejects(lobby.social.oauthFinish({
    v: 1, transaction: expiring.transaction, state: 'd'.repeat(64),
    code: 'expired-code', code_verifier: 'z'.repeat(43),
  }, auth), (error) => error.code === 'oauth_expired');
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`oauth2:pending:${expiring.transaction}`)), null);
});

test('OAuth mutation revisions isolate unrelated players and reject same-player races', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-race-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const alice = await lobby.guest({ name: 'Alice', install: '1'.repeat(32) }, '198.51.100.11');
  const bob = await lobby.guest({ name: 'Bob', install: '2'.repeat(32) }, '198.51.100.11');
  const aliceRecovery = await lobby.social.recovery(await lobby.account(`Bearer ${alice.token}`), '198.51.100.11');
  const recovery = await lobby.social.recovery(await lobby.account(`Bearer ${bob.token}`), '198.51.100.11');
  const auth = { authorization: `Bearer ${alice.token}`, hasAuthorization: true, ip: '198.51.100.11' };
  const fetcher = async (url, options) => {
    assert.equal(options.redirect, 'error');
    assert.ok(url.startsWith('https://'));
    return options.method === 'POST'
      ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'race-access-token' }), { status: 200 })
      : new Response(JSON.stringify({ id: '4440001112' }), { status: 200 });
  };
  env.oauthFetch = fetcher;
  const startLink = async (stateValue, verifier) => lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'link', state: stateValue, code_challenge: await pkceChallenge(env, verifier), install: '1'.repeat(32),
  }, auth);
  const beginGatedFinish = async (stateValue, verifier, code) => {
    const started = await startLink(stateValue, verifier);
    let enteredResolve;
    let release;
    const entered = new Promise((resolve) => { enteredResolve = resolve; });
    const gate = new Promise((resolve) => { release = resolve; });
    env.oauthFetch = async (url, options) => {
      if (options.method === 'POST' && new URLSearchParams(options.body).get('code') === code) {
        enteredResolve();
        await gate;
      }
      return fetcher(url, options);
    };
    const result = await lobby.social.oauthFinish({
      v: 1, transaction: started.transaction, state: stateValue, code, code_verifier: verifier,
    }, auth);
    await entered;
    return { deferred: result.deferred, release };
  };

  // an unrelated player's change during the exchange does not make it stale
  const first = await beginGatedFinish('e'.repeat(64), 'a'.repeat(43), 'unrelated-race-code');
  const rebound = await lobby.guest({
    name: 'Bob recovered', install: '3'.repeat(32), recover: recovery.code,
  }, '198.51.100.11');
  assert.equal(rebound.account.pid, bob.account.pid);
  first.release();
  const linked = await first.deferred;
  assert.equal(linked.account.pid, alice.account.pid);
  env.oauthFetch = fetcher;

  // the same player's change during the exchange does: a link held at the provider while the player unlinks and links again
  const proof = { install: '1'.repeat(32), recovery: aliceRecovery.code };
  assert.deepEqual((await lobby.social.oauthUnlink({ v: 1, provider: 'discord', ...proof }, auth)).providers, []);
  const second = await beginGatedFinish('f'.repeat(64), 'b'.repeat(43), 'same-player-race-code');
  const other = await startLink('9'.repeat(64), 'c'.repeat(43));
  const done = await (await lobby.social.oauthFinish({
    v: 1, transaction: other.transaction, state: '9'.repeat(64), code: 'other-code', code_verifier: 'c'.repeat(43),
  }, auth)).deferred;
  assert.deepEqual(done.providers, ['discord']);
  second.release();
  await assert.rejects(second.deferred, (error) => error.code === 'oauth_stale');
  env.oauthFetch = fetcher;
  assert.deepEqual((await lobby.social.oauthAccounts(`Bearer ${alice.token}`)).providers, ['discord']);
});

test('gated restore exchanges reject target rebinds, install conflicts, and new enforcement', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-restore-races-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const ip = '198.51.100.84';
  const oldInstall = '1'.repeat(32);
  const alice = await lobby.guest({ name: 'Alice', install: oldInstall, device: 'a'.repeat(64) }, ip);
  await complete(lobby, env, 'discord', 'link', { token: alice.token });
  const baseFetcher = env.oauthFetch;
  const gate = gatedOAuthFetch(env, baseFetcher);

  const bob = await lobby.guest({ name: 'Bob', install: '6'.repeat(32) }, ip);
  const bobRecovery = await lobby.social.recovery(await lobby.account(`Bearer ${bob.token}`), ip);
  let flow = await beginGatedRestore(lobby, env, gate, 'b'.repeat(64), 'b'.repeat(43), '8'.repeat(32), 'unrelated-player-restore-code', { ip });
  const bobRecovered = await lobby.guest({
    name: 'Bob recovered', install: '7'.repeat(32), recover: bobRecovery.code,
  }, ip);
  assert.equal(bobRecovered.account.pid, bob.account.pid);
  assert.equal((await state.store.socialTransaction((tx) => tx.get(`player:${bob.account.pid}`))).install, '7'.repeat(32));
  flow.hold.release();
  const unrelatedRestore = await flow.finishing.deferred;
  assert.equal(unrelatedRestore.account.pid, alice.account.pid);
  assert.equal((await state.store.socialTransaction((tx) => tx.get(`player:${alice.account.pid}`))).install, '8'.repeat(32));

  const recoveredCode = await complete(lobby, env, 'discord', 'confirm', { token: unrelatedRestore.token });
  flow = await beginGatedRestore(lobby, env, gate, 'c'.repeat(64), 'c'.repeat(43), '2'.repeat(32), 'recovery-race-code', { ip });
  const recovered = await lobby.guest({
    name: 'Alice recovered', install: '3'.repeat(32), recover: recoveredCode.code,
  }, ip);
  assert.equal(recovered.account.pid, alice.account.pid);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.code === 'oauth_stale');
  let player = await state.store.socialTransaction((tx) => tx.get(`player:${alice.account.pid}`));
  assert.equal(player.install, '3'.repeat(32));
  assert.equal(player.generation, 3);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${'2'.repeat(32)}`)), null);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${'3'.repeat(32)}`)), alice.account.pid);
  await assertRestoreConsumed(lobby, flow);

  const recoveredAuth = `Bearer ${recovered.token}`;
  flow = await beginGatedRestore(lobby, env, gate, 'd'.repeat(64), 'd'.repeat(43), '4'.repeat(32), 'unlink-relink-race-code', { ip });
  const unlinkCode = await complete(lobby, env, 'discord', 'confirm', { token: recovered.token });
  await lobby.social.oauthUnlink({ v: 1, provider: 'discord', install: '3'.repeat(32), recovery: unlinkCode.code }, {
    authorization: recoveredAuth, hasAuthorization: true, ip,
  });
  await complete(lobby, env, 'discord', 'link', { token: recovered.token });
  const beforeStaleFinish = await identitySnapshot(state, [alice.account.pid], ['3'.repeat(32), '4'.repeat(32)]);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.code === 'oauth_stale');
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid], ['3'.repeat(32), '4'.repeat(32)]), beforeStaleFinish);
  await assertRestoreConsumed(lobby, flow);

  const flowA = await beginGatedRestore(lobby, env, gate, 'e'.repeat(64), 'e'.repeat(43), '5'.repeat(32), 'first-restore-race-code', { ip });
  const flowB = await beginGatedRestore(lobby, env, gate, 'f'.repeat(64), 'f'.repeat(43), '6'.repeat(32), 'second-restore-race-code', { ip });
  flowA.hold.release();
  const won = await flowA.finishing.deferred;
  assert.equal(won.account.pid, alice.account.pid);
  flowB.hold.release();
  await assert.rejects(flowB.finishing.deferred, (error) => error.code === 'oauth_stale');
  player = await state.store.socialTransaction((tx) => tx.get(`player:${alice.account.pid}`));
  assert.equal(player.install, '5'.repeat(32));
  assert.equal(player.generation, 4);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${'5'.repeat(32)}`)), alice.account.pid);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`install:${'6'.repeat(32)}`)), null);
  await assertRestoreConsumed(lobby, flowB);

  flow = await beginGatedRestore(lobby, env, gate, '8'.repeat(64), 'h'.repeat(43), '7'.repeat(32), 'occupied-race-code', { ip });
  const beforeOccupied = await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['5'.repeat(32), '7'.repeat(32)]);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.code === 'install_in_use');
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid, bob.account.pid], ['5'.repeat(32), '7'.repeat(32)]), beforeOccupied);
  await assertRestoreConsumed(lobby, flow);

  const enforcementState = (version, { ban = true, blockDevices = false } = {}) => ({
    v: 1,
    version,
    block: ban ? { kind: 'ban', until: null, reason: 'test restriction' } : null,
    mute_until: null,
    routing: '',
    notice: null,
    devices: [],
    ...(blockDevices ? { block_devices: true } : {}),
  });
  flow = await beginGatedRestore(lobby, env, gate, '9'.repeat(64), 'i'.repeat(43), '9'.repeat(32), 'old-install-ban-code', { ip });
  let beforeEnforcement = await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), '9'.repeat(32)]);
  assert.equal((await signedAdminUpdate(lobby, '5'.repeat(32), enforcementState(1))).status, 200);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.status === 403);
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), '9'.repeat(32)]), beforeEnforcement);
  await assertRestoreConsumed(lobby, flow);
  assert.equal((await signedAdminUpdate(lobby, '5'.repeat(32), enforcementState(2, { ban: false }))).status, 200);

  flow = await beginGatedRestore(lobby, env, gate, 'a'.repeat(64), 'j'.repeat(43), 'a'.repeat(32), 'new-install-ban-code', { ip });
  beforeEnforcement = await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), 'a'.repeat(32)]);
  assert.equal((await signedAdminUpdate(lobby, 'a'.repeat(32), enforcementState(1))).status, 200);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.status === 403);
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), 'a'.repeat(32)]), beforeEnforcement);
  await assertRestoreConsumed(lobby, flow);

  const blockedDevice = 'b'.repeat(64);
  const enforcementInstall = 'c'.repeat(32);
  await lobby.recordSighting(enforcementInstall, 'Device owner', blockedDevice, null);
  flow = await beginGatedRestore(lobby, env, gate, 'b'.repeat(64), 'k'.repeat(43), 'd'.repeat(32), 'device-ban-code', { ip, device: blockedDevice });
  beforeEnforcement = await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), 'd'.repeat(32)]);
  assert.equal((await signedAdminUpdate(lobby, enforcementInstall, enforcementState(1, { blockDevices: true }))).status, 200);
  flow.hold.release();
  await assert.rejects(flow.finishing.deferred, (error) => error.status === 403);
  assert.deepEqual(await identitySnapshot(state, [alice.account.pid], ['5'.repeat(32), 'd'.repeat(32)]), beforeEnforcement);
  await assertRestoreConsumed(lobby, flow);
});

test('legacy metadata initializes transactionally and corruption or clock overflow fails closed', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-metadata-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); });
  const lobby = makeLobby();
  const guest = await lobby.guest({ name: 'Legacy', install: '4'.repeat(32) }, '198.51.100.12');
  const pid = guest.account.pid;
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`player:${pid}`).oauth2), undefined);
  assert.deepEqual((await lobby.social.oauthAccounts(`Bearer ${guest.token}`)).providers, []);
  // Sol r4: reading the accounts initializes nothing; the first identity write does (here a recovery code's metadata check)
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`player:${pid}`).oauth2), undefined);
  assert.equal(await state.store.socialTransaction((tx) => tx.get(`oauth2:player:${pid}`)), null);
  await lobby.social.recovery(await lobby.account(`Bearer ${guest.token}`), '198.51.100.12');
  let records = await state.store.socialTransaction((tx) => ({
    player: tx.get(`player:${pid}`),
    meta: tx.get(`oauth2:player:${pid}`),
    schema: tx.get('oauth2:schema'),
    clock: tx.get('oauth2:clock'),
  }));
  assert.equal(records.player.oauth2, 1);
  assert.deepEqual(records.meta, { providers: {}, revision: records.clock, version: 0 });
  assert.equal(records.schema, 1);

  const legacyCode = await lobby.social.recovery(await lobby.account(`Bearer ${guest.token}`), '198.51.100.12');
  await complete(lobby, env, 'discord', 'link', { token: guest.token });
  records = await state.store.socialTransaction((tx) => ({
    meta: tx.get(`oauth2:player:${pid}`),
  }));
  await state.store.socialTransaction((tx) => tx.put('oauth2:clock', Number.MAX_SAFE_INTEGER));
  await assert.rejects(lobby.social.oauthUnlink({ v: 1, provider: 'discord', install: '4'.repeat(32), recovery: legacyCode.code }, {
    authorization: `Bearer ${guest.token}`, hasAuthorization: true, ip: '198.51.100.12',
  }), (error) => error.code === 'social_unavailable');
  assert.deepEqual(await state.store.socialTransaction((tx) => tx.get(`oauth2:player:${pid}`)), records.meta);

  await state.store.socialTransaction((tx) => {
    tx.put('oauth2:clock', records.meta.revision);
    tx.put(`oauth2:player:${pid}`, { ...records.meta, providers: { discord: 'a'.repeat(64) } });
  });
  await assert.rejects(lobby.social.oauthAccounts(`Bearer ${guest.token}`), (error) => error.code === 'social_unavailable');
});

test('SQLite rolls back OAuth and recovery rebinds when a later player write fails', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-oauth-rollback-'));
  const { state, env, makeLobby } = harness(join(dir, 'social.sqlite'), 'discord', '1234567890');
  t.after(() => {
    try { state.store.db.exec('DROP TRIGGER IF EXISTS fail_player_rebind'); } catch {}
    try { state.store.close(); } catch {}
    rmSync(dir, { recursive: true, force: true });
  });
  const lobby = makeLobby();
  const guest = await lobby.guest({ name: 'Alice', install: '4'.repeat(32) }, '198.51.100.7');
  const recovery = await lobby.social.recovery(await lobby.account(`Bearer ${guest.token}`), '198.51.100.7');
  await complete(lobby, env, 'discord', 'link', { token: guest.token });
  const startState = 'e'.repeat(64);
  const verifier = 'r'.repeat(43);
  const challenge = await pkceChallenge(env, verifier);
  const started = await lobby.social.oauthStart({
    v: 1, provider: 'discord', mode: 'restore', state: startState, code_challenge: challenge,
    name: 'Alice rebound', install: '5'.repeat(32),
  }, { ip: '198.51.100.7' });
  const pid = guest.account.pid;
  const original = await state.store.socialTransaction((tx) => ({
    player: tx.get(`player:${pid}`),
    metadata: tx.get(`oauth2:player:${pid}`),
    subjects: [...tx.list('oauth2:subject:', { limit: 10 })],
    oldInstall: tx.get(`install:${'4'.repeat(32)}`),
    newInstall: tx.get(`install:${'5'.repeat(32)}`),
  }));
  state.store.db.exec(`CREATE TRIGGER fail_player_rebind BEFORE UPDATE ON social_records WHEN NEW.key = 'player:${pid}' BEGIN SELECT RAISE(ABORT, 'injected rebind failure'); END;`);
  const finish = await lobby.social.oauthFinish({
    v: 1, transaction: started.transaction, state: startState, code: 'rollback-code', code_verifier: verifier,
  }, { ip: '198.51.100.7' });
  await assert.rejects(finish.deferred, (error) => error.code === 'social_unavailable');
  const afterRestoreFailure = await state.store.socialTransaction((tx) => ({
    player: tx.get(`player:${pid}`),
    metadata: tx.get(`oauth2:player:${pid}`),
    subjects: [...tx.list('oauth2:subject:', { limit: 10 })],
    oldInstall: tx.get(`install:${'4'.repeat(32)}`),
    newInstall: tx.get(`install:${'5'.repeat(32)}`),
  }));
  assert.deepEqual(afterRestoreFailure, original);

  await assert.rejects(
    lobby.guest({ name: 'Alice recovered', install: '6'.repeat(32), recover: recovery.code }, '198.51.100.7'),
    (error) => error.code === 'social_unavailable',
  );
  const afterRecoveryFailure = await state.store.socialTransaction((tx) => ({
    player: tx.get(`player:${pid}`),
    recovery: tx.get(`recovery:${tx.get(`player:${pid}`).recovery_hash}`),
    oldInstall: tx.get(`install:${'4'.repeat(32)}`),
    attemptedInstall: tx.get(`install:${'6'.repeat(32)}`),
  }));
  assert.deepEqual(afterRecoveryFailure.player, original.player);
  assert.equal(afterRecoveryFailure.recovery, pid);
  assert.equal(afterRecoveryFailure.oldInstall, pid);
  assert.equal(afterRecoveryFailure.attemptedInstall, null);
  assert.equal((await lobby.account(`Bearer ${guest.token}`)).pid, pid);
});
