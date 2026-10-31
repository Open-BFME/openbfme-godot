import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { LobbyObject, durableSocialTransaction, SOCIAL_DATABASE_BUDGET_BYTES } from '../src/worker.mjs';

const ENV = {
  TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
  SESSION_KEY: 'c3'.repeat(32),
  ADMIN_KEY: 'a1'.repeat(32),
  MOD_KEY: 'b2'.repeat(32),
  RELAYS: '',
  PUBLIC_URL: 'http://lobby.test',
  LOG_STORE: '',
};

test('budget: SQLite admission preserves history and rolls back partial writes at capacity', () => {
  const { state, database } = durableState();
  const transact = durableSocialTransaction(state.storage);
  transact((tx) => tx.put('retained', { text: 'forever' }));
  let reads = 0;
  Object.defineProperty(state.storage.sql, 'databaseSize', {
    configurable: true,
    get: () => ++reads > 1 ? SOCIAL_DATABASE_BUDGET_BYTES : SOCIAL_DATABASE_BUDGET_BYTES - 4096,
  });
  assert.throws(() => transact((tx) => {
    tx.put('partial', true);
    tx.put('second', true);
  }), (error) => error.kind === 'full');
  assert.deepEqual(transact((tx) => tx.get('retained')), { text: 'forever' });
  assert.equal(transact((tx) => tx.get('partial')), null);
  assert.equal(transact((tx) => tx.get('second')), null);
  Object.defineProperty(state.storage.sql, 'databaseSize', { value: undefined });
  assert.throws(() => transact((tx) => tx.put('unknown-size', true)), (error) => error.kind === 'unavailable');
  database.close();
});

function durableState({ sqlite = true } = {}) {
  const database = sqlite ? new DatabaseSync(':memory:') : null;
  const metrics = { executions: 0, rows: 0 };
  const sql = sqlite ? {
    get databaseSize() {
      return database.prepare('PRAGMA page_count').get().page_count * database.prepare('PRAGMA page_size').get().page_size;
    },
    exec(statement, ...args) {
      metrics.executions += 1;
      if (/^\s*(CREATE|ALTER|DROP)\b/i.test(statement)) {
        database.exec(statement);
        return { toArray: () => [] };
      }
      const prepared = database.prepare(statement);
      if (/^\s*SELECT\b/i.test(statement)) {
        const result = prepared.all(...args);
        metrics.rows += result.length;
        return { toArray: () => result };
      }
      prepared.run(...args);
      return { toArray: () => [] };
    },
  } : null;
  const rows = {
    get size() {
      return sqlite ? database.prepare('SELECT COUNT(*) AS count FROM social_records').get().count : 0;
    },
    values() {
      return sqlite ? database.prepare('SELECT value FROM social_records').all().map((row) => row.value) : [];
    },
  };
  const persisted = new Map();
  const storage = {
    ...(sqlite ? { sql } : {}),
    async get(key) { return persisted.get(key) ?? null; },
    async put(key, value) { persisted.set(key, structuredClone(value)); },
    async delete(key) { persisted.delete(key); },
    async list({ prefix = '' } = {}) {
      return new Map([...persisted].filter(([key]) => key.startsWith(prefix)).map(([key, value]) => [key, structuredClone(value)]));
    },
    ...(sqlite ? { transactionSync(fn) {
      database.exec('BEGIN IMMEDIATE');
      try {
        const result = fn();
        database.exec('COMMIT');
        return result;
      } catch (error) {
        database.exec('ROLLBACK');
        throw error;
      }
    } } : {}),
  };
  const state = { storage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
  return { state, rows, database, metrics };
}

test('inbox cache: warm immediate and held polls avoid SQL, while restart and eviction hydrate a fresh epoch', async () => {
  const { state, database, metrics } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  const request = (path, token, method = 'GET', body) => object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {}),
  }));
  const guest = await request('/v1/auth/guest', null, 'POST', {
    name: 'Inbox Cache', install: '4'.repeat(32), client: { platform: 'linux', version: 'test' },
  });
  const { token, account } = await guest.json();
  const createdGame = await request('/v1/games', token, 'POST', {
    v: 1,
    name: 'warm poll room',
    profile: '9ecbb999bef4925c679a5570eacd6b99',
    setup: {
      map: 'maps/map mp evendim/map mp evendim.map',
      startingCash: 1500,
      slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }],
    },
  });
  assert.equal(createdGame.status, 200);
  const gameId = (await createdGame.json()).id;
  const cold = await request('/v1/inbox', token);
  const first = await cold.json();
  assert.equal(cold.status, 200);
  assert.ok(metrics.executions > 0 && metrics.rows > 0);

  metrics.executions = 0;
  metrics.rows = 0;
  const warm = await request(`/v1/inbox?since=${first.seq}&wait=0`, token);
  assert.equal(warm.status, 200);
  assert.deepEqual((await warm.json()).seq, first.seq);
  assert.deepEqual(metrics, { executions: 0, rows: 0 });

  const otherGuest = await request('/v1/auth/guest', null, 'POST', {
    name: 'Second Inbox', install: '9'.repeat(32), client: { platform: 'linux', version: 'test' },
  });
  const otherToken = (await otherGuest.json()).token;
  metrics.executions = 0;
  metrics.rows = 0;
  const statsRefresh = await request(`/v1/inbox?since=${first.seq}&wait=0`, token);
  assert.equal(statsRefresh.status, 200);
  const statsRefreshBody = await statsRefresh.json();
  assert.notEqual(statsRefreshBody.seq, first.seq);
  assert.equal(statsRefreshBody.stats.players, 2);
  assert.deepEqual(metrics, { executions: 0, rows: 0 });

  metrics.executions = 0;
  metrics.rows = 0;
  for (const path of ['/v1/stats', '/v1/motd', '/v1/games', `/v1/games/${gameId}`]) {
    assert.equal((await request(path, token)).status, 200);
  }
  assert.deepEqual(metrics, { executions: 0, rows: 0 });

  metrics.executions = 0;
  metrics.rows = 0;
  const held = await request(`/v1/inbox?since=${statsRefreshBody.seq}&wait=1`, token);
  assert.equal(held.status, 200);
  assert.deepEqual((await held.json()).seq, statsRefreshBody.seq);
  assert.deepEqual(metrics, { executions: 0, rows: 0 });

  const restarted = new LobbyObject(state, ENV);
  await state.ready;
  metrics.executions = 0;
  metrics.rows = 0;
  const afterRestart = await restarted.fetch(new Request('https://lobby.test/v1/inbox', {
    headers: { authorization: `Bearer ${token}` },
  }));
  assert.equal(afterRestart.status, 200);
  const restartedBody = await afterRestart.json();
  assert.notEqual(restartedBody.seq.split(':')[0], first.seq.split(':')[0]);
  assert.ok(metrics.executions > 0 && metrics.rows > 0);

  const social = restarted.lobby.social;
  social.maxInboxEntries = 1;
  await restarted.fetch(new Request('https://lobby.test/v1/inbox', { headers: { authorization: `Bearer ${otherToken}` } }));
  metrics.executions = 0;
  metrics.rows = 0;
  const afterEviction = await restarted.fetch(new Request(`https://lobby.test/v1/inbox?since=${restartedBody.seq}&wait=0`, {
    headers: { authorization: `Bearer ${token}` },
  }));
  assert.equal(afterEviction.status, 200);
  const evictedBody = await afterEviction.json();
  assert.notEqual(evictedBody.seq, restartedBody.seq);
  assert.equal(evictedBody.seq.split(':')[0], restartedBody.seq.split(':')[0]);
  assert.ok(metrics.executions > 0 && metrics.rows > 0);
  database.close();
});

test('inbox cache: a concurrent profile transaction invalidates an in-flight friends view before publication', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  const request = (path, token, method = 'GET', body) => object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {}),
  }));
  const makeGuest = async (name, install) => (await (await request('/v1/auth/guest', null, 'POST', {
    name, install: install.repeat(32), client: { platform: 'linux', version: 'test' },
  })).json());
  const alice = await makeGuest('Cache Alice', '5');
  const bob = await makeGuest('Cache Bob', '6');
  const me = await (await request('/v1/me', alice.token)).json();
  const ask = await (await request('/v1/friends/requests', bob.token, 'POST', { v: 1, code: me.code })).json();
  assert.equal((await request(`/v1/friends/requests/${ask.request.id}/accept`, alice.token, 'POST', { v: 1 })).status, 200);

  const social = object.lobby.social;
  social.invalidateInboxOwner(alice.account.pid);
  const original = social.buildInboxSnapshot.bind(social);
  let wrote = false;
  social.buildInboxSnapshot = async (...args) => {
    const snapshot = await original(...args);
    if (!wrote) {
      wrote = true;
      await social.transaction((tx) => {
        const key = `player:${bob.account.pid}`;
        tx.put(key, { ...tx.get(key), nick: 'Updated Bob' });
      });
    }
    return snapshot;
  };
  const response = await request('/v1/friends', alice.token);
  assert.equal(response.status, 200);
  assert.equal((await response.json()).friends[0].nick, 'Updated Bob');
  database.close();
});

test('inbox cache: hidden and expired-presence friends expose integer-zero since through Worker routes', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  let now = Date.now();
  object.lobby.env.now = () => now;
  const request = (path, token, method = 'GET', body) => object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {}),
  }));
  const guest = async (name, install) => (await (await request('/v1/auth/guest', null, 'POST', {
    name, install: install.repeat(32), client: { platform: 'linux', version: 'test' },
  })).json());
  const alice = await guest('Presence Alice', '7');
  const bob = await guest('Presence Bob', '8');
  const code = await (await request('/v1/me', alice.token)).json();
  const created = await (await request('/v1/friends/requests', bob.token, 'POST', { v: 1, code: code.code })).json();
  assert.equal((await request(`/v1/friends/requests/${created.request.id}/accept`, alice.token, 'POST', { v: 1 })).status, 200);

  const checkOffline = async () => {
    const friends = await (await request('/v1/friends', alice.token)).json();
    const inbox = await (await request('/v1/inbox', alice.token)).json();
    const friend = friends.friends.find(({ pid }) => pid === bob.account.pid)?.presence;
    const snapshot = inbox.presence.find(({ pid }) => pid === bob.account.pid);
    assert.equal(friend.since, 0);
    assert.equal(Number.isInteger(friend.since), true);
    assert.equal(snapshot.since, 0);
    assert.equal(Number.isInteger(snapshot.since), true);
  };
  assert.equal((await request('/v1/me/status', bob.token, 'POST', { v: 1, state: 'online', hidden: true })).status, 200);
  await checkOffline();
  assert.equal((await request('/v1/me/status', bob.token, 'POST', { v: 1, state: 'online', hidden: false })).status, 200);
  now += 151000;
  await checkOffline();
  database.close();
});

test('inbox cache: a generation change during session validation cannot publish revoked poll auth', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  const guest = await object.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ name: 'Auth Race', install: '1'.repeat(32), client: { platform: 'linux', version: 'test' } }),
  }));
  const { token, account } = await guest.json();
  const social = object.lobby.social;
  const assertSession = social.assertSession.bind(social);
  let readSession;
  let resumeSession;
  let paused = false;
  const sessionRead = new Promise((resolve) => { readSession = resolve; });
  const sessionGate = new Promise((resolve) => { resumeSession = resolve; });
  social.assertSession = async (candidate) => {
    const result = await assertSession(candidate);
    if (!paused) {
      paused = true;
      readSession();
      await sessionGate;
    }
    return result;
  };
  const polling = object.fetch(new Request('https://lobby.test/v1/inbox', {
    headers: { authorization: `Bearer ${token}` },
  }));
  await sessionRead;
  await social.transaction((tx) => {
    const player = tx.get(`player:${account.pid}`);
    tx.put(`player:${account.pid}`, { ...player, generation: player.generation + 1 });
  });
  resumeSession();
  const response = await polling;
  assert.equal(response.status, 401);
  assert.equal((await response.json()).error, 'session_revoked');
  assert.equal([...social.pollAuth.values()].some((entry) => entry.pid === account.pid), false);
  database.close();
});

test('inbox cache: concurrent same-token poll authorization keeps cache byte and index accounting exact', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  const install = '9'.repeat(32);
  const guest = await object.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ name: 'Concurrent Auth', install, client: { platform: 'linux', version: 'test' } }),
  }));
  const { token, account } = await guest.json();
  const social = object.lobby.social;
  const assertSession = social.assertSession.bind(social);
  let arrivals = 0;
  let release;
  let resolveArrivals;
  const arrived = new Promise((resolve) => { resolveArrivals = resolve; });
  const gate = new Promise((resolve) => { release = resolve; });
  social.assertSession = async (candidate) => {
    const result = await assertSession(candidate);
    arrivals += 1;
    if (arrivals === 2) resolveArrivals();
    await gate;
    return result;
  };

  const authorization = `Bearer ${token}`;
  const first = object.lobby.account(authorization, { pollCache: true });
  const second = object.lobby.account(authorization, { pollCache: true });
  await arrived;
  release();
  const [firstAccount, secondAccount] = await Promise.all([first, second]);
  const key = token.split('.')[0];
  assert.equal(firstAccount.pid, account.pid);
  assert.equal(secondAccount.pid, account.pid);
  assert.deepEqual([...social.pollAuth.keys()], [key]);
  assert.equal(social.pollAuthCacheBytes,
    [...social.pollAuth.values()].reduce((total, entry) => total + entry.bytes, 0));
  assert.deepEqual([...social.pollAuthByPid.get(account.pid)], [key]);
  assert.deepEqual([...social.pollAuthByPid.get(install)], [key]);
  assert.equal(social.pollAuthByPid.size, 2);
  for (const [identity, keys] of social.pollAuthByPid) {
    for (const indexedKey of keys) {
      const entry = social.pollAuth.get(indexedKey);
      assert.ok(entry);
      assert.ok(entry.pid === identity || entry.install === identity);
    }
  }
  database.close();
});

test('inbox cache: a concurrent enforcement write is rechecked before publishing a cold viewer snapshot', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  const install = '2'.repeat(32);
  const device = 'd'.repeat(64);
  const guest = await object.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ name: 'Viewer Ban', install, device, client: { platform: 'linux', version: 'test' } }),
  }));
  const { token } = await guest.json();
  const social = object.lobby.social;
  const build = social.buildInboxSnapshot.bind(social);
  let injected = false;
  social.buildInboxSnapshot = async (...args) => {
      const snapshot = await build(...args);
      if (!injected) {
        injected = true;
        await object.lobby.env.store.identity.saveEnforcement({
          install: '3'.repeat(32), v: 1, version: 1,
          block: { kind: 'suspend', until: Math.floor(object.lobby.env.now() / 1000) + 300, reason: 'shared device race' },
          mute_until: null, routing: '', notice: null, devices: [device],
        });
      }
    return snapshot;
  };
  const response = await object.fetch(new Request('https://lobby.test/v1/inbox', {
    headers: { authorization: `Bearer ${token}` },
  }));
  const error = await response.json();
  assert.equal(response.status, 403, JSON.stringify(error));
  assert.equal(error.error, 'suspended');
  database.close();
});

test('inbox cache: shared-device suspension expiry refreshes presence from memory without SQL', async () => {
  const { state, database, metrics } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  let now = Date.now();
  object.lobby.env.now = () => now;
  const request = (path, token, method = 'GET', body) => object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {}),
  }));
  const guest = async (name, install, device) => (await (await request('/v1/auth/guest', null, 'POST', {
    name, install: install.repeat(32), device, client: { platform: 'linux', version: 'test' },
  })).json());
  const alice = await guest('Device Alice', '3', 'd'.repeat(64));
  const bob = await guest('Device Bob', '4', 'e'.repeat(64));
  const profile = await (await request('/v1/me', alice.token)).json();
  const friendRequest = await (await request('/v1/friends/requests', bob.token, 'POST', { v: 1, code: profile.code })).json();
  assert.equal((await request(`/v1/friends/requests/${friendRequest.request.id}/accept`, alice.token, 'POST', { v: 1 })).status, 200);
  assert.equal((await request('/v1/me/status', bob.token, 'POST', { v: 1, state: 'online' })).status, 200);

  await object.lobby.env.store.identity.saveEnforcement({
    install: '5'.repeat(32), v: 1, version: 1,
    block: { kind: 'suspend', until: Math.floor(now / 1000) + 5, reason: 'shared device' },
    mute_until: null, routing: '', notice: null, devices: ['e'.repeat(64)],
  });
  const blockedView = await (await request('/v1/inbox', alice.token)).json();
  assert.equal(blockedView.presence.find(({ pid }) => pid === bob.account.pid).state, 'offline');

  now += 6000;
  metrics.executions = 0;
  metrics.rows = 0;
  const expiredView = await request(`/v1/inbox?since=${blockedView.seq}&wait=0`, alice.token);
  assert.equal(expiredView.status, 200);
  const expiredBody = await expiredView.json();
  assert.equal(expiredBody.presence.find(({ pid }) => pid === bob.account.pid).state, 'online');
  assert.notEqual(expiredBody.seq, blockedView.seq);
  assert.deepEqual(metrics, { executions: 0, rows: 0 });
  database.close();
});

test('inbox cache: request, chat, read, invite, room, block, and enforcement writes refresh only affected views', async () => {
  const { state, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;
  let now = Date.now();
  object.lobby.env.now = () => now;
  const request = (path, token, method = 'GET', body) => object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    ...(body ? { body: JSON.stringify(body) } : {}),
  }));
  const guest = async (name, install) => (await (await request('/v1/auth/guest', null, 'POST', {
    name, install: install.repeat(32), client: { platform: 'linux', version: 'test' },
  })).json());
  const alice = await guest('Writes Alice', 'a');
  const bob = await guest('Writes Bob', 'b');
  const aliceProfile = await (await request('/v1/me', alice.token)).json();
  const pending = await (await request('/v1/friends/requests', bob.token, 'POST', {
    v: 1, code: aliceProfile.code,
  })).json();
  const pendingInbox = await (await request('/v1/inbox', alice.token)).json();
  assert.equal(pendingInbox.requests.length, 1);
  assert.equal((await request(`/v1/friends/requests/${pending.request.id}/accept`, alice.token, 'POST', { v: 1 })).status, 200);
  assert.deepEqual((await (await request('/v1/inbox', alice.token)).json()).requests, []);

  const sent = await (await request(`/v1/chat/${bob.account.pid}/messages`, alice.token, 'POST', {
    v: 1, client_id: 'c'.repeat(32), text: 'cache invalidation',
  })).json();
  const withMessage = await (await request('/v1/inbox', alice.token)).json();
  assert.equal(withMessage.chat[0].last_id, sent.message.id);
  assert.equal((await request(`/v1/chat/${bob.account.pid}/read`, alice.token, 'POST', {
    v: 1, through: sent.message.id,
  })).status, 200);
  const afterRead = await (await request('/v1/inbox', alice.token)).json();
  assert.equal(afterRead.chat[0].read_id, sent.message.id);

  const createdGame = await request('/v1/games', alice.token, 'POST', {
    v: 1,
    name: 'cache room',
    profile: '9ecbb999bef4925c679a5570eacd6b99',
    setup: {
      map: 'maps/map mp evendim/map mp evendim.map',
      startingCash: 1500,
      slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }],
    },
  });
  assert.equal(createdGame.status, 200);
  const game = (await createdGame.json()).id;
  const invite = await request('/v1/invites', alice.token, 'POST', {
    v: 1, to: bob.account.pid, game, kind: 'invite',
  });
  assert.equal(invite.status, 200);
  const inviteBody = await invite.json();
  assert.equal((await (await request('/v1/inbox', alice.token)).json()).invites[0].id, inviteBody.invite.id);
  now += 301000;
  assert.deepEqual((await (await request('/v1/inbox', alice.token)).json()).invites, []);

  const secondInvite = await (await request('/v1/invites', alice.token, 'POST', {
    v: 1, to: bob.account.pid, game, kind: 'invite',
  })).json();
  const accepted = await request(`/v1/invites/${secondInvite.invite.id}/accept`, bob.token, 'POST', {
    v: 1, profile: '9ecbb999bef4925c679a5570eacd6b99', mode: 'normal', platform: 'linux',
  });
  assert.equal(accepted.status, 200);
  assert.equal((await (await request('/v1/inbox', alice.token)).json()).presence[0].state, 'in_room');
  assert.equal((await request('/v1/blocks', alice.token, 'POST', { v: 1, pid: bob.account.pid })).status, 200);
  assert.deepEqual((await (await request('/v1/friends', alice.token)).json()).friends, []);

  await object.lobby.env.store.identity.saveEnforcement({
    install: 'a'.repeat(32), v: 1, version: 1,
    block: { kind: 'ban', until: null, reason: 'cache test' }, mute_until: null,
    routing: '', notice: null, devices: [],
  });
  assert.equal((await request('/v1/inbox', alice.token)).status, 403);
  database.close();
});

test('LobbyObject preserves friendship, chat, and recovery across a new object over SQLite storage', async () => {
  const { state, rows, database } = durableState();
  const object = new LobbyObject(state, ENV);
  await state.ready;

  const guestResponse = await object.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      name: 'Worker Alice',
      install: '1'.repeat(32),
      client: { platform: 'linux', version: 'worker-test' },
    }),
  }));
  assert.equal(guestResponse.status, 200);
  const alice = await guestResponse.json();
  assert.match(alice.account.pid, /^[0-9a-f]{16}$/);

  const bobResponse = await object.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      name: 'Worker Bob',
      install: '2'.repeat(32),
      client: { platform: 'linux', version: 'worker-test' },
    }),
  }));
  assert.equal(bobResponse.status, 200);
  const bob = await bobResponse.json();
  const meResponse = await object.fetch(new Request('https://lobby.test/v1/me', {
    headers: { authorization: `Bearer ${alice.token}` },
  }));
  const me = await meResponse.json();
  assert.equal(me.pid, alice.account.pid);
  assert.match(me.code, /^[0-9A-HJKMNP-TV-Z]{10}$/);
  const requestResponse = await object.fetch(new Request('https://lobby.test/v1/friends/requests', {
    method: 'POST',
    headers: { 'content-type': 'application/json', authorization: `Bearer ${bob.token}` },
    body: JSON.stringify({ v: 1, code: me.code }),
  }));
  assert.equal(requestResponse.status, 200);
  const friendRequest = await requestResponse.json();
  const acceptedResponse = await object.fetch(new Request(
    `https://lobby.test/v1/friends/requests/${friendRequest.request.id}/accept`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', authorization: `Bearer ${alice.token}` },
      body: JSON.stringify({ v: 1 }),
    }));
  assert.equal(acceptedResponse.status, 200);
  const sentResponse = await object.fetch(new Request(
    `https://lobby.test/v1/chat/${bob.account.pid}/messages`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', authorization: `Bearer ${alice.token}` },
      body: JSON.stringify({ v: 1, client_id: 'a'.repeat(32), text: 'persistent worker chat' }),
    }));
  assert.equal(sentResponse.status, 200);
  const identityStore = object.lobby.env.store.identity;
  await identityStore.saveEnforcement({
    install: '1'.repeat(32), v: 1, version: 1, block: null, mute_until: null,
    routing: 'social-route', notice: null, devices: [],
  });
  await identityStore.saveEnforcement({
    install: '2'.repeat(32), v: 1, version: 1, block: null, mute_until: null,
    routing: 'other-route', notice: null, devices: [],
  });
  assert.equal(object.lobby.enforcement.size, 0);
  assert.equal((await object.fetch(new Request(
    `https://lobby.test/v1/chat/${bob.account.pid}/messages`, {
      headers: { authorization: `Bearer ${alice.token}` },
    }))).status, 404);
  await identityStore.saveEnforcement({
    install: '2'.repeat(32), v: 1, version: 2, block: null, mute_until: null,
    routing: 'social-route', notice: null, devices: [],
  });
  const routedHistory = await object.fetch(new Request(
    `https://lobby.test/v1/chat/${bob.account.pid}/messages`, {
      headers: { authorization: `Bearer ${alice.token}` },
    }));
  assert.equal(routedHistory.status, 200);
  await identityStore.saveEnforcement({
    install: '1'.repeat(32), v: 1, version: 2, block: null,
    mute_until: Math.floor(Date.now() / 1000) + 600, routing: 'social-route', notice: null, devices: [],
  });
  const mutedResponse = await object.fetch(new Request(
    `https://lobby.test/v1/chat/${bob.account.pid}/messages`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', authorization: `Bearer ${alice.token}` },
      body: JSON.stringify({ v: 1, client_id: 'b'.repeat(32), text: 'muted by indexed enforcement' }),
    }));
  assert.equal(mutedResponse.status, 403);
  assert.equal((await mutedResponse.json()).error, 'chat_muted');
  await identityStore.saveEnforcement({
    install: '2'.repeat(32), v: 1, version: 3, block: null, mute_until: null,
    routing: '', notice: null, devices: [],
  });
  const recoveryResponse = await object.fetch(new Request('https://lobby.test/v1/me/recovery', {
    method: 'POST',
    headers: { 'content-type': 'application/json', authorization: `Bearer ${alice.token}` },
    body: JSON.stringify({ v: 1, install: '1'.repeat(32) }),
  }));
  assert.equal(recoveryResponse.status, 200);
  const recovery = await recoveryResponse.json();
  assert.match(recovery.code, /^[0-9A-HJKMNP-TV-Z]{16}$/);

  const restarted = new LobbyObject(state, ENV);
  await state.ready;
  const newInstallResponse = await restarted.fetch(new Request('https://lobby.test/v1/auth/guest', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      name: 'Recovered Alice',
      install: '3'.repeat(32),
      recover: recovery.code,
      client: { platform: 'linux', version: 'worker-test' },
    }),
  }));
  assert.equal(newInstallResponse.status, 200);
  const recovered = await newInstallResponse.json();
  assert.equal(recovered.account.pid, alice.account.pid);
  assert.equal((await restarted.fetch(new Request('https://lobby.test/v1/me', {
    headers: { authorization: `Bearer ${alice.token}` },
  }))).status, 401);
  const messagesResponse = await restarted.fetch(new Request(
    `https://lobby.test/v1/chat/${bob.account.pid}/messages`, {
      headers: { authorization: `Bearer ${recovered.token}` },
    }));
  assert.equal(messagesResponse.status, 200);
  assert.equal((await messagesResponse.json()).messages[0].text, 'persistent worker chat');

  assert.equal(JSON.stringify(rows.values()).includes(recovery.code), false);
  assert.equal(rows.size > 0, true);
  assert.equal((await restarted.fetch(new Request('https://lobby.test/v1/social/me', {
    headers: { authorization: `Bearer ${recovered.token}` },
  }))).status, 404);
  database.close();
});

test('Worker refuses to initialize identity without Durable Object SQLite', async () => {
  const { state } = durableState({ sqlite: false });
  assert.throws(
    () => new LobbyObject(state, ENV),
    /identity storage needs a SQLite-backed Durable Object/,
  );
});
