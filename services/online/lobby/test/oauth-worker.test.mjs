import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { LobbyObject } from '../src/worker.mjs';

function sqliteState() {
  const db = new DatabaseSync(':memory:');
  const persisted = new Map();
  const sql = {
    // the lobby budget (lobby-budget-1) refuses a write when the Durable Object cannot report its database size
    get databaseSize() {
      return db.prepare('PRAGMA page_count').get().page_count * db.prepare('PRAGMA page_size').get().page_size;
    },
    exec(statement, ...args) {
      if (/^\s*(CREATE|ALTER|DROP)\b/i.test(statement)) {
        db.exec(statement);
        return { toArray: () => [] };
      }
      const query = db.prepare(statement);
      if (/^\s*SELECT\b/i.test(statement)) return { toArray: () => query.all(...args) };
      query.run(...args);
      return { toArray: () => [] };
    },
  };
  const storage = {
    sql,
    async get(key) { return persisted.get(key) ?? null; },
    async put(key, value) { persisted.set(key, structuredClone(value)); },
    async delete(key) { persisted.delete(key); },
    async list({ prefix = '', startAfter = null, limit = 1000 } = {}) {
      return new Map([...persisted].filter(([key]) => key.startsWith(prefix) && (startAfter === null || key > startAfter))
        .sort(([a], [b]) => a.localeCompare(b)).slice(0, limit).map(([key, value]) => [key, structuredClone(value)]));
    },
    transactionSync(fn) {
      db.exec('BEGIN IMMEDIATE');
      try { const value = fn(); db.exec('COMMIT'); return value; }
      catch (error) { db.exec('ROLLBACK'); throw error; }
    },
  };
  const state = { storage, blockConcurrencyWhile(fn) { this.ready = fn(); } };
  return { state, db };
}

const ENV = {
  TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
  SESSION_KEY: 'c3'.repeat(32),
  ADMIN_KEY: 'a1'.repeat(32),
  MOD_KEY: 'b2'.repeat(32),
  RELAYS: '',
  PUBLIC_URL: 'http://lobby.test',
  LOG_STORE: '',
  OAUTH_DISCORD_CLIENT_ID: 'discord-test-client',
  OAUTH_DISCORD_CLIENT_SECRET: 'discord-test-secret',
  oauthFetch: async (url, options) => options.method === 'POST'
    ? new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'worker-mock-token' }), { status: 200 })
    : new Response(JSON.stringify({ id: '1234567890' }), { status: 200 }),
};

async function request(object, path, body, token, method = 'POST') {
  const headers = { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) };
  const response = await object.fetch(new Request(`https://lobby.test${path}`, {
    method,
    headers,
    ...(method === 'GET' ? {} : { body: JSON.stringify(body) }),
  }));
  return { status: response.status, body: await response.json() };
}

test('OAuth restore survives a Worker Durable Object restart and revokes the old token', async (t) => {
  const { state, db } = sqliteState();
  t.after(() => db.close());
  let object = new LobbyObject(state, ENV);
  await state.ready;
  const guest = await request(object, '/v1/auth/guest', { name: 'Worker Alice', install: '1'.repeat(32) });
  assert.equal(guest.status, 200);
  const me = await object.fetch(new Request('https://lobby.test/v1/me', { headers: { authorization: `Bearer ${guest.body.token}` } }));
  const before = await me.json();
  const friend = await request(object, '/v1/auth/guest', { name: 'Worker Bob', install: '7'.repeat(32) });
  assert.equal(friend.status, 200);
  const friendMe = await request(object, '/v1/me', null, friend.body.token, 'GET');
  const requestFriend = await request(object, '/v1/friends/requests', { v: 1, code: friendMe.body.code }, guest.body.token);
  assert.equal(requestFriend.status, 200);
  const accepted = await request(
    object,
    `/v1/friends/requests/${requestFriend.body.request.id}/accept`,
    { v: 1 },
    friend.body.token,
  );
  assert.equal(accepted.status, 200);
  const sent = await request(object, `/v1/chat/${friend.body.account.pid}/messages`, {
    v: 1, client_id: '8'.repeat(32), text: 'worker permanent history',
  }, guest.body.token);
  assert.equal(sent.status, 200);

  const verifier = 'w'.repeat(43);
  const challenge = btoa(String.fromCharCode(...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(verifier)))))
    .replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
  const stateValue = 'a'.repeat(64);
  const link = await request(object, '/v1/auth/oauth/start', {
    v: 1, provider: 'discord', mode: 'link', state: stateValue, code_challenge: challenge, install: '1'.repeat(32),
  }, guest.body.token);
  assert.equal(link.status, 200);
  const linked = await request(object, '/v1/auth/oauth/finish', {
    v: 1, transaction: link.body.transaction, state: stateValue, code: 'worker-code', code_verifier: verifier,
  }, guest.body.token);
  assert.equal(linked.status, 200);
  assert.equal(linked.body.account.id, guest.body.account.id);
  assert.equal(linked.body.account.pid, guest.body.account.pid);

  object = new LobbyObject(state, ENV);
  await state.ready;
  const restoreState = 'b'.repeat(64);
  const restore = await request(object, '/v1/auth/oauth/start', {
    v: 1, provider: 'discord', mode: 'restore', state: restoreState, code_challenge: challenge,
    name: 'Worker restored', install: '2'.repeat(32),
  });
  assert.equal(restore.status, 200);
  assert.equal(restore.body.pid, null);
  const result = await request(object, '/v1/auth/oauth/finish', {
    v: 1, transaction: restore.body.transaction, state: restoreState, code: 'worker-code', code_verifier: verifier,
  });
  assert.equal(result.status, 200);
  assert.equal(result.body.account.pid, guest.body.account.pid);
  assert.equal(result.body.account.kind, 'guest');
  const friends = await request(object, '/v1/friends', null, result.body.token, 'GET');
  assert.deepEqual(friends.body.friends.map((entry) => entry.pid), [friend.body.account.pid]);
  const history = await request(object, `/v1/chat/${friend.body.account.pid}/messages`, null, result.body.token, 'GET');
  assert.equal(history.body.messages[0].text, 'worker permanent history');

  object = new LobbyObject(state, ENV);
  await state.ready;
  const oldSession = await object.fetch(new Request('https://lobby.test/v1/me', {
    headers: { authorization: `Bearer ${guest.body.token}` },
  }));
  assert.equal(oldSession.status, 401);
  const current = await object.fetch(new Request('https://lobby.test/v1/me', {
    headers: { authorization: `Bearer ${result.body.token}` },
  }));
  assert.equal(current.status, 200);
  assert.equal((await current.json()).code, before.code);
  const persistedFriends = await request(object, '/v1/friends', null, result.body.token, 'GET');
  assert.deepEqual(persistedFriends.body.friends.map((entry) => entry.pid), [friend.body.account.pid]);
  const persistedHistory = await request(object, `/v1/chat/${friend.body.account.pid}/messages`, null, result.body.token, 'GET');
  assert.equal(persistedHistory.body.messages[0].text, 'worker permanent history');
});
