import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Lobby, configFromEnv, route } from '../src/lobby.mjs';
import { SocialError } from '../src/social.mjs';
import { SocialStorageError } from '../src/social-store.mjs';
import { createNodeSocialStore } from '../src/social-store-node.mjs';
import { nodeEnv } from '../src/server.mjs';

const PROFILE = '9ecbb999bef4925c679a5570eacd6b99';
const ENV = {
  TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
  SESSION_KEY: 'c3'.repeat(32),
  ADMIN_KEY: 'a1'.repeat(32),
  MOD_KEY: 'b2'.repeat(32),
  RELAYS: '',
  PUBLIC_URL: 'http://lobby.test',
};
const SETUP = {
  map: 'maps/map mp evendim/map mp evendim.map',
  startingCash: 1500,
  slots: [
    { state: 'player', faction: 0, color: 0, startPos: 0, team: 0 },
    { state: 'open' },
  ],
};

test('social JSON reads repair legacy profiles, messages, receipts, recent players and block notes', async () => {
  for (const text of ['\ud800x', 'é e\u0301 🌍']) {
    const { lobby, store, env } = setup();
    const alice = (await guest(lobby, 'Alice', 'a'.repeat(32))).body;
    const bob = (await guest(lobby, 'Bob', 'b'.repeat(32))).body;
    await befriend(lobby, alice, bob);
    const clientId = 'c'.repeat(32);
    const path = `/v1/chat/${bob.account.pid}/messages`;
    const sent = await jsonCall(lobby, 'POST', path, {
      token: alice.token, body: { v: 1, client_id: clientId, text: 'original' },
    });
    assert.equal(sent.status, 200);
    const [first, second] = [alice.account.pid, bob.account.pid].sort();
    const messageKey = `message:${first}:${second}:0000000000000001`;
    const receiptKey = `receipt:${alice.account.pid}:${first}:${second}:${clientId}`;
    store.data.get(`player:${bob.account.pid}`).nick = text;
    store.data.get(messageKey).text = text;
    store.data.get(receiptKey).text = text;
    store.data.get(receiptKey).message.text = text;
    const now = Math.floor(env.now() / 1000);
    const game = '0123456789abcdef';
    const recent = { at: now, game, players: [{ pid: bob.account.pid,
      code: store.data.get(`player:${bob.account.pid}`).code, nick: text, target: text }] };
    store.data.set(`recent:${alice.account.pid}:${String(now).padStart(16, '0')}:${game}`, recent);
    const stored = JSON.stringify([...store.data]);
    lobby.social.resetCaches();
    const me = await call(lobby, 'GET', '/v1/me', { token: bob.token });
    assert.equal(me.status, 200);
    assert.equal(JSON.parse(me.body).nick, text.toWellFormed());
    const player = store.data.get(`player:${bob.account.pid}`);
    assert.equal(me.body, JSON.stringify({ v: 1, pid: player.pid, code: player.code, nick: text.toWellFormed(),
      privacy: player.privacy, hidden: player.hidden }));
    const friends = await call(lobby, 'GET', '/v1/friends', { token: alice.token });
    assert.equal(friends.status, 200);
    assert.equal(JSON.parse(friends.body).friends[0].nick, text.toWellFormed());
    const history = await call(lobby, 'GET', path, { token: alice.token });
    assert.equal(history.status, 200);
    assert.equal(JSON.parse(history.body).messages[0].text, text.toWellFormed());
    assert.equal(history.body, JSON.stringify({ v: 1, messages: [{ ...store.data.get(messageKey), text: text.toWellFormed() }],
      next: null, last_id: 1, read_id: 0 }));
    const games = await call(lobby, 'GET', '/v1/recent', { token: alice.token });
    assert.equal(games.status, 200);
    assert.equal(JSON.parse(games.body).games[0].players[0].nick, text.toWellFormed());
    assert.equal(JSON.parse(games.body).games[0].players[0].target, text.toWellFormed());
    assert.equal(JSON.stringify([...store.data]), stored, 'read repair does not rewrite social records');
    if (text.isWellFormed()) {
      assert.equal(games.body, JSON.stringify({ v: 1, games: [recent] }));
    }
    const replay = await jsonCall(lobby, 'POST', path, {
      token: alice.token, body: { v: 1, client_id: clientId, text: text.toWellFormed() },
    });
    assert.equal(replay.status, 200, JSON.stringify(replay.body));
    assert.equal(replay.body.message.text, text.toWellFormed());
    const blocked = await jsonCall(lobby, 'POST', '/v1/blocks', {
      token: alice.token, body: { v: 1, pid: bob.account.pid, note: 'original' },
    });
    assert.equal(blocked.status, 200);
    store.data.get(`block:${alice.account.pid}:${bob.account.pid}`).note = text;
    const blocks = await call(lobby, 'GET', '/v1/blocks', { token: alice.token });
    assert.equal(blocks.status, 200);
    assert.equal(JSON.parse(blocks.body).blocks[0].note, text.toWellFormed());
    assert.equal(JSON.parse(blocks.body).blocks[0].nick, text.toWellFormed());
  }
});

test('budget: recipient invite cap frees space at five-minute expiry', async () => {
  const { lobby, env } = setup();
  const alice = (await guest(lobby, 'Alice', 'a'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'b'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const game = await createGame(lobby, alice.token);
  for (let i = 1; i <= 50; i++) {
    const id = i.toString(16).padStart(16, '0');
    lobby.social.pendingInvites.set(id, {
      id, from: 'f'.repeat(16), to: bob.account.pid, game,
      expires: Math.floor(env.now() / 1000) + 300,
    });
  }
  const send = () => jsonCall(lobby, 'POST', '/v1/invites', {
    token: alice.token, body: { v: 1, to: bob.account.pid, game, kind: 'invite' },
  });
  const capped = await send();
  assert.equal(capped.status, 409, JSON.stringify(capped.body));
  assert.equal(capped.body.error, 'invite_limit');
  assert.equal(lobby.social.pendingInvites.size, 50);
  env.advance(300);
  assert.equal((await send()).status, 200);
  assert.equal(lobby.social.pendingInvites.size, 1);
});

test('budget: decline suppression expires without poisoning social transactions', async () => {
  const { lobby, env, store } = setup();
  const alice = (await guest(lobby, 'Alice', 'a'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'b'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  let game = await createGame(lobby, alice.token);
  const send = () => jsonCall(lobby, 'POST', '/v1/invites', {
    token: alice.token, body: { v: 1, to: bob.account.pid, game, kind: 'invite' },
  });
  for (let i = 0; i < 3; i++) {
    const sent = await send();
    assert.equal(sent.status, 200, JSON.stringify(sent.body));
    const declined = await jsonCall(lobby, 'POST', `/v1/invites/${sent.body.invite.id}/decline`, {
      token: bob.token, body: { v: 1 },
    });
    assert.equal(declined.status, 200, JSON.stringify(declined.body));
    env.advance(61);
  }
  assert.equal((await send()).body.error, 'cooldown');
  assert.equal((await jsonCall(lobby, 'GET', '/v1/me', { token: alice.token })).status, 200);
  env.advance(86400);
  await lobby.social.transaction(() => {});
  assert.equal([...store.data.keys()].some((key) => key.startsWith('invite-suppress:') ||
    key.startsWith('quota-expire:') && key.includes('invite-suppress')), false);
  game = await createGame(lobby, alice.token);
  const afterExpiry = await send();
  assert.equal(afterExpiry.status, 200, JSON.stringify(afterExpiry.body));
  assert.deepEqual(
    { version: afterExpiry.body.v, recipient: afterExpiry.body.invite.to.pid, game: afterExpiry.body.invite.game },
    { version: 1, recipient: bob.account.pid, game },
  );
});

test('inbox cache: hidden and offline friends use since zero in friends and inbox responses', async () => {
  const { lobby, env } = setup();
  const alice = (await guest(lobby, 'Alice', 'a'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'b'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const setStatus = (hidden) => jsonCall(lobby, 'POST', '/v1/me/status', {
    token: bob.token, body: { v: 1, state: 'online', hidden },
  });
  const assertOfflineViews = async () => {
    const friends = (await jsonCall(lobby, 'GET', '/v1/friends', { token: alice.token })).body.friends;
    const inbox = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token })).body.presence;
    const friendPresence = friends.find(({ pid }) => pid === bob.account.pid)?.presence;
    const inboxPresence = inbox.find(({ pid }) => pid === bob.account.pid);
    const expected = { pid: bob.account.pid, state: 'offline', game: null, joinable: false, since: 0, map: null, mode: null };
    assert.deepEqual(friendPresence, expected);
    assert.deepEqual(inboxPresence, expected);
    assert.equal(Number.isInteger(friendPresence.since), true);
    assert.equal(Number.isInteger(inboxPresence.since), true);
  };

  assert.equal((await setStatus(true)).status, 200);
  await assertOfflineViews();
  assert.equal((await setStatus(false)).status, 200);
  env.advance(151);
  await assertOfflineViews();
});

function memorySocialStore() {
  const data = new Map();
  let queue = Promise.resolve();
  let failWrites = false;
  let failureKind = 'unavailable';
  return {
    data,
    failWrites(value, kind = 'unavailable') { failWrites = value; failureKind = kind; },
    socialTransaction(fn) {
      const run = queue.then(async () => {
        const draft = new Map([...data].map(([key, value]) => [key, structuredClone(value)]));
        const tx = {
          get: (key) => draft.has(key) ? structuredClone(draft.get(key)) : null,
          put: (key, value) => draft.set(key, structuredClone(value)),
          delete: (key) => draft.delete(key),
          list: (prefix, { after = null, limit = 100, reverse = false } = {}) => {
            if (!Number.isInteger(limit) || limit < 1 || limit > 1000) throw new TypeError('invalid social list bounds');
            let keys = [...draft.keys()].filter((key) => key.startsWith(prefix));
            keys.sort();
            if (after !== null) keys = keys.filter((key) => reverse ? key < after : key > after);
            if (reverse) keys.reverse();
            return new Map(keys.slice(0, limit).map((key) => [key, structuredClone(draft.get(key))]));
          },
        };
        const result = await fn(tx);
        const changed = data.size !== draft.size || [...draft].some(([key, value]) =>
          JSON.stringify(data.get(key)) !== JSON.stringify(value));
        if (failWrites && changed) throw new SocialStorageError(failureKind);
        data.clear();
        for (const [key, value] of draft) data.set(key, value);
        return result;
      });
      queue = run.catch(() => {});
      return run;
    },
  };
}

function setup({ store = memorySocialStore(), config = ENV } = {}) {
  const env = nodeEnv({
    saveGame() {},
    deleteGame() {},
    saveSighting() {},
    deleteSighting() {},
    loadSightings: () => [],
    reportTransaction: async (fn) => fn({ get: () => null, put() {}, delete() {}, list: () => new Map() }),
    socialTransaction: (fn) => store.socialTransaction(fn),
  });
  let now = 1_700_000_000_000;
  let random = 0;
  let timerId = 0;
  const timers = new Map();
  env.now = () => now;
  env.setTimer = (callback, delay) => {
    const id = ++timerId;
    timers.set(id, { callback, due: now + delay, delay });
    return id;
  };
  env.clearTimer = (id) => timers.delete(id);
  env.pendingTimers = () => [...timers.values()];
  env.advance = (seconds) => {
    now += seconds * 1000;
    while (true) {
      const due = [...timers.entries()].filter(([, timer]) => timer.due <= now).sort((a, b) => a[1].due - b[1].due)[0];
      if (!due) break;
      timers.delete(due[0]);
      due[1].callback();
    }
  };
  env.randomBytes = (n) => {
    const call = random++;
    return Uint8Array.from({ length: n }, (_, i) => (call * 73 + i * 53) & 0xff);
  };
  const lobby = new Lobby(env, configFromEnv(config));
  return { env, lobby, store };
}

async function call(lobby, method, path, { token, body, raw, ip = '198.51.100.1', signal } = {}) {
  const url = new URL(path, 'http://lobby.test');
  const bytes = raw !== undefined ? new TextEncoder().encode(raw) :
    body === undefined ? new Uint8Array() : new TextEncoder().encode(JSON.stringify(body));
  const result = await route(lobby, {
    method,
    path: url.pathname,
    query: url.searchParams,
    ip,
    headers: token ? { authorization: `Bearer ${token}` } : {},
    signal,
    bytes: async () => bytes,
  });
  return result.deferred ?? result;
}

async function jsonCall(lobby, method, path, options) {
  const result = await call(lobby, method, path, options);
  return { status: result.status, body: JSON.parse(result.body) };
}

async function flushEvents(turns = 1) {
  for (let index = 0; index < turns; index++) await new Promise((resolve) => setImmediate(resolve));
}

async function waitForHolds(lobby, count) {
  for (let turn = 0; turn < 100 && lobby.waiterCount < count; turn++) await flushEvents();
  assert.equal(lobby.waiterCount, count);
}

async function guest(lobby, name, install) {
  const result = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name, install, client: { platform: 'linux', version: 'social-test' } },
  });
  assert.equal(result.status, 200, JSON.stringify(result.body));
  return result;
}

async function createGame(lobby, token, { password, roomCode, mode, setup = SETUP, name = 'room' } = {}) {
  const result = await jsonCall(lobby, 'POST', '/v1/games', {
    token,
    body: { name, profile: PROFILE, setup, ...(password ? { password } : {}),
      ...(roomCode ? { room_code: roomCode } : {}), ...(mode ? { mode } : {}) },
  });
  assert.equal(result.status, 200, JSON.stringify(result.body));
  return result.body.id;
}

async function befriend(lobby, from, to) {
  const target = await jsonCall(lobby, 'GET', '/v1/me', { token: to.token });
  assert.equal(target.status, 200, JSON.stringify(target.body));
  const request = await jsonCall(lobby, 'POST', '/v1/friends/requests', {
    token: from.token,
    body: { v: 1, code: target.body.code },
  });
  assert.equal(request.status, 200, JSON.stringify(request.body));
  const accepted = await jsonCall(lobby, 'POST', `/v1/friends/requests/${request.body.request.id}/accept`, {
    token: to.token,
    body: { v: 1 },
  });
  assert.equal(accepted.status, 200, JSON.stringify(accepted.body));
}

function seededCode(number) {
  const alphabet = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
  let value = BigInt(number);
  let result = '';
  for (let index = 0; index < 10; index++) {
    result = alphabet[Number(value & 31n)] + result;
    value >>= 5n;
  }
  return result;
}

function seedPlayer(tx, number) {
  const pid = (0x8000000000000000n + BigInt(number)).toString(16);
  const install = (0x80000000000000000000000000000000n + BigInt(number)).toString(16);
  const code = seededCode(number);
  tx.put(`player:${pid}`, {
    pid, install, device: null, nick: `Seed ${number}`, code, generation: 1,
    privacy: { invites: 'friends_or_room', challenges: 'friends_or_room', requests: 'anyone', presence: 'friends' },
    hidden: false, created_at: 1, updated_at: 1,
  });
  tx.put(`install:${install}`, pid);
  tx.put(`code:${code}`, pid);
  return pid;
}

test('room-peer challenge handoff survives explicit leave, recipient leave, and failed profile acceptance', async () => {
  const { lobby } = setup();
  const sender = (await guest(lobby, 'Sender', '1'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '2'.repeat(32))).body;
  const source = await createGame(lobby, recipient.token, { name: 'public room' });
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/join`, {
    token: sender.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: sender.token })).body.left, true);

  const challengeRoom = await createGame(lobby, sender.token, { password: 'private', name: 'challenge room' });
  const sent = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: sender.token,
    body: { v: 1, to: recipient.account.pid, game: challengeRoom, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(sent.status, 200, JSON.stringify(sent.body));
  const inviteId = sent.body.invite.id;
  const before = structuredClone(lobby.games.get(challengeRoom).slots);
  const incompatible = await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: '0'.repeat(32) },
  });
  assert.equal(incompatible.status, 409);
  assert.equal(lobby.social.pendingInvites.has(inviteId), true);
  assert.deepEqual(lobby.games.get(challengeRoom).slots, before);

  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: recipient.token })).body.closed, true);
  const accepted = await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  });
  assert.equal(accepted.status, 200, JSON.stringify(accepted.body));
  assert.ok(accepted.body.game.slots.some((slot) => slot.pid === recipient.account.pid));
  assert.equal(lobby.social.pendingInvites.has(inviteId), false);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
});

test('private code and password rooms accept only recipient-bound five-minute grants', async () => {
  const { env, lobby } = setup();
  const host = (await guest(lobby, 'Host', 'c'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', 'd'.repeat(32))).body;
  const outsider = (await guest(lobby, 'Outsider', 'e'.repeat(32))).body;
  await befriend(lobby, host, recipient);
  await befriend(lobby, host, outsider);
  const assertPrivateJoinHidesProfile = async (game, secretError, mode = 'normal') => {
    const refused = await jsonCall(lobby, 'POST', `/v1/games/${game}/join`, {
      token: outsider.token,
      body: {
        profile: '0'.repeat(32),
        mode,
        grant: { v: 1, pid: outsider.account.pid, game, expires: env.now() / 1000 + 300, used: false },
      },
    });
    assert.equal(refused.status, 403);
    assert.equal(refused.body.error, secretError);
    assert.ok(!JSON.stringify(refused.body).includes(PROFILE));
  };

  const codeRoom = await createGame(lobby, host.token, { roomCode: 'CODE1234' });
  await assertPrivateJoinHidesProfile(codeRoom, 'room_code');
  assert.equal(lobby.social.roomJoinable({ pid: recipient.account.pid }, lobby.games.get(codeRoom)), false);
  const createdAt = Math.floor(env.now() / 1000);
  const codeInvite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token,
    body: { v: 1, to: recipient.account.pid, game: codeRoom, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(codeInvite.status, 200, JSON.stringify(codeInvite.body));
  const codeInviteId = codeInvite.body.invite.id;
  assert.equal(codeInvite.body.invite.expires, createdAt + 300);
  assert.equal('room_code' in codeInvite.body.invite, false);
  assert.equal(lobby.social.roomJoinable({ pid: recipient.account.pid }, lobby.games.get(codeRoom)), true);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${codeInviteId}/accept`, {
    token: outsider.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${codeRoom}/join`, {
    token: recipient.token, body: { profile: PROFILE },
  })).status, 403);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${codeInviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: '0'.repeat(32), mode: 'normal' },
  })).status, 409);
  const wrongMode = await jsonCall(lobby, 'POST', `/v1/invites/${codeInviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE, mode: 'uber' },
  });
  assert.equal(wrongMode.status, 409);
  assert.equal(wrongMode.body.error, 'mode');
  assert.equal(lobby.social.pendingInvites.has(codeInviteId), true);
  const codeAccepted = await jsonCall(lobby, 'POST', `/v1/invites/${codeInviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  });
  assert.equal(codeAccepted.status, 200, JSON.stringify(codeAccepted.body));
  assert.equal(codeAccepted.body.game.slots.find((slot) => slot.pid === recipient.account.pid).platform, null);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${codeInviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${codeRoom}/leave`, { token: recipient.token })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${codeRoom}/leave`, { token: host.token })).body.closed, true);

  const passwordRoom = await createGame(lobby, host.token, { password: 'private', mode: 'uber' });
  await assertPrivateJoinHidesProfile(passwordRoom, 'password', 'uber');
  assert.equal(lobby.social.roomJoinable({ pid: outsider.account.pid }, lobby.games.get(passwordRoom)), false);
  const passwordInvite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token,
    body: { v: 1, to: recipient.account.pid, game: passwordRoom, kind: 'invite' },
  });
  assert.equal(passwordInvite.status, 200, JSON.stringify(passwordInvite.body));
  const missingMode = await jsonCall(lobby, 'POST', `/v1/invites/${passwordInvite.body.invite.id}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE, platform: 'linux' },
  });
  assert.equal(missingMode.status, 409);
  assert.equal(missingMode.body.error, 'mode');
  assert.equal(lobby.social.pendingInvites.has(passwordInvite.body.invite.id), true);
  const passwordAccepted = await jsonCall(lobby, 'POST', `/v1/invites/${passwordInvite.body.invite.id}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE, mode: 'uber', platform: 'linux' },
  });
  assert.equal(passwordAccepted.status, 200, JSON.stringify(passwordAccepted.body));
  assert.equal(passwordAccepted.body.game.slots.find((slot) => slot.pid === recipient.account.pid).platform, 'linux');
  assert.equal(lobby.social.roomJoinable({ pid: outsider.account.pid }, lobby.games.get(passwordRoom)), false);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${passwordRoom}/leave`, { token: host.token })).body.closed, true);

  const expiryRoom = await createGame(lobby, host.token, { roomCode: 'EXPIRY12' });
  const expiring = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token, body: { v: 1, to: outsider.account.pid, game: expiryRoom, kind: 'invite' },
  });
  assert.equal(expiring.status, 200, JSON.stringify(expiring.body));
  env.advance(299);
  assert.equal(lobby.social.pendingInvites.has(expiring.body.invite.id), true);
  env.advance(1);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${expiring.body.invite.id}/accept`, {
    token: outsider.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
});

test('kicking an invitation sender revokes its pending invite', async () => {
  const { lobby } = setup();
  const host = (await guest(lobby, 'Host', 'f'.repeat(32))).body;
  const sender = (await guest(lobby, 'Sender', '0'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '1'.repeat(32))).body;
  await befriend(lobby, sender, recipient);
  const roomSetup = structuredClone(SETUP);
  roomSetup.slots.push({ state: 'open' });
  const room = await createGame(lobby, host.token, { setup: roomSetup });
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${room}/join`, {
    token: sender.token, body: { profile: PROFILE },
  })).status, 200);
  const sent = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: sender.token, body: { v: 1, to: recipient.account.pid, game: room, kind: 'invite' },
  });
  assert.equal(sent.status, 200, JSON.stringify(sent.body));
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${room}/kick`, {
    token: host.token, body: { slot: 1 },
  })).status, 200);
  assert.equal(lobby.social.pendingInvites.has(sent.body.invite.id), false);
});

test('challenge handoffs cannot cross a sign-in or a later room creation', async () => {
  const { lobby } = setup();
  const sender = (await guest(lobby, 'Sender', '3'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '4'.repeat(32))).body;
  const source = await createGame(lobby, recipient.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/join`, {
    token: sender.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: sender.token })).body.left, true);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: recipient.token })).body.closed, true);
  const room = await createGame(lobby, sender.token, { password: 'private' });
  const signedInAgain = (await guest(lobby, 'Sender', '3'.repeat(32))).body;
  const wrongSession = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: signedInAgain.token,
    body: { v: 1, to: recipient.account.pid, game: room, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(wrongSession.status, 404);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${room}/leave`, { token: sender.token })).body.closed, true);

  const source2 = await createGame(lobby, recipient.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source2}/join`, {
    token: signedInAgain.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source2}/leave`, { token: signedInAgain.token })).body.left, true);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source2}/leave`, { token: recipient.token })).body.closed, true);
  const first = await createGame(lobby, signedInAgain.token, { password: 'one' });
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${first}/join`, {
    token: recipient.token, body: { profile: PROFILE, password: 'one' },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${first}/kick`, {
    token: signedInAgain.token, body: { slot: 1 },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${first}/leave`, { token: signedInAgain.token })).body.closed, true);
  const second = await createGame(lobby, signedInAgain.token, { password: 'two' });
  const invalidated = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: signedInAgain.token,
    body: { v: 1, to: recipient.account.pid, game: second, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(invalidated.status, 404);
});

test('a host leaving and closing the source room captures its seated social peers', async () => {
  const { lobby } = setup();
  const sender = (await guest(lobby, 'Sender', '5'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '6'.repeat(32))).body;
  const source = await createGame(lobby, sender.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/join`, {
    token: recipient.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: sender.token })).body.closed, true);
  const room = await createGame(lobby, sender.token, { password: 'private' });
  const sent = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: sender.token,
    body: { v: 1, to: recipient.account.pid, game: room, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(sent.status, 200, JSON.stringify(sent.body));
});

test('friends-only challenge privacy cancels a room-peer invite and wakes both sides', async () => {
  const { lobby } = setup();
  const sender = (await guest(lobby, 'Sender', 'a'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', 'b'.repeat(32))).body;
  const source = await createGame(lobby, recipient.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/join`, {
    token: sender.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, {
    token: sender.token,
  })).body.left, true);
  const room = await createGame(lobby, sender.token, { password: 'challenge' });
  const invite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: sender.token,
    body: { v: 1, to: recipient.account.pid, game: room, kind: 'challenge', mode: 'normal' },
  });
  assert.equal(invite.status, 200, JSON.stringify(invite.body));
  assert.equal(lobby.social.pendingInvites.has(invite.body.invite.id), true);
  const privacy = await jsonCall(lobby, 'PUT', '/v1/me/privacy', {
    token: recipient.token,
    body: { v: 1, privacy: { invites: 'friends_or_room', challenges: 'friends', requests: 'anyone', presence: 'friends' } },
  });
  assert.equal(privacy.status, 200);
  assert.equal(lobby.social.pendingInvites.has(invite.body.invite.id), false);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${invite.body.invite.id}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
});

test('friend invites seat only the intended compatible recipient, enforce decline cooldown, and notify expiry', async () => {
  const { env, lobby } = setup();
  const host = (await guest(lobby, 'Host', '4'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '5'.repeat(32))).body;
  const outsider = (await guest(lobby, 'Outsider', '6'.repeat(32))).body;
  const decliner = (await guest(lobby, 'Decliner', '7'.repeat(32))).body;
  const expiring = (await guest(lobby, 'Expiring', '8'.repeat(32))).body;
  const cancelling = (await guest(lobby, 'Cancelling', '9'.repeat(32))).body;
  const cooldownTarget = (await guest(lobby, 'Cooldown', 'a'.repeat(32))).body;
  for (const friend of [recipient, decliner, expiring, cancelling, cooldownTarget]) await befriend(lobby, host, friend);

  const gameId = await createGame(lobby, host.token);
  const sent = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token, body: { v: 1, to: recipient.account.pid, game: gameId, kind: 'invite' },
  });
  assert.equal(sent.status, 200, JSON.stringify(sent.body));
  const inviteId = sent.body.invite.id;
  assert.equal('grant' in sent.body.invite, false);
  assert.equal('password' in sent.body.invite, false);
  const before = structuredClone(lobby.games.get(gameId).slots);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: outsider.token, body: { v: 1, profile: PROFILE },
  })).status, 404);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: '0'.repeat(32) },
  })).status, 409);
  assert.deepEqual(lobby.games.get(gameId).slots, before);
  assert.equal(lobby.social.pendingInvites.has(inviteId), true);
  const fullInvite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token, body: { v: 1, to: decliner.account.pid, game: gameId, kind: 'invite' },
  });
  assert.equal(fullInvite.status, 200, JSON.stringify(fullInvite.body));
  const hostSnapshot = await jsonCall(lobby, 'GET', '/v1/inbox', { token: host.token });
  const senderHold = call(lobby, 'GET', `/v1/inbox?since=${hostSnapshot.body.seq}&wait=25`, { token: host.token });
  await waitForHolds(lobby, 1);
  const accepted = await jsonCall(lobby, 'POST', `/v1/invites/${inviteId}/accept`, {
    token: recipient.token, body: { v: 1, profile: PROFILE },
  });
  assert.equal(accepted.status, 200, JSON.stringify(accepted.body));
  assert.ok(accepted.body.game.slots.some((slot) => slot.pid === recipient.account.pid));
  assert.equal(lobby.social.pendingInvites.has(inviteId), false);
  const senderWoke = await senderHold;
  const senderWokeBody = JSON.parse(senderWoke.body);
  assert.notEqual(senderWokeBody.seq, hostSnapshot.body.seq);
  assert.equal(senderWokeBody.invites.some(({ id }) => id === inviteId), false);

  const fullBefore = structuredClone(lobby.games.get(gameId).slots);
  const fullAccept = await jsonCall(lobby, 'POST', `/v1/invites/${fullInvite.body.invite.id}/accept`, {
    token: decliner.token, body: { v: 1, profile: PROFILE },
  });
  assert.equal(fullAccept.status, 409);
  assert.equal(fullAccept.body.error, 'full');
  assert.deepEqual(lobby.games.get(gameId).slots, fullBefore);
  assert.equal(lobby.social.pendingInvites.has(fullInvite.body.invite.id), true);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/leave`, { token: recipient.token })).status, 200);

  for (let decline = 0; decline < 3; decline++) {
    const invite = await jsonCall(lobby, 'POST', '/v1/invites', {
      token: host.token, body: { v: 1, to: cooldownTarget.account.pid, game: gameId, kind: 'invite' },
    });
    assert.equal(invite.status, 200, JSON.stringify(invite.body));
    assert.equal((await jsonCall(lobby, 'POST', `/v1/invites/${invite.body.invite.id}/decline`, {
      token: cooldownTarget.token, body: { v: 1 },
    })).status, 200);
    if (decline < 2) env.advance(61);
  }
  const suppressed = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token, body: { v: 1, to: cooldownTarget.account.pid, game: gameId, kind: 'invite' },
  });
  assert.equal(suppressed.status, 409);
  assert.equal(suppressed.body.error, 'cooldown');

  const expiringInvite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token, body: { v: 1, to: expiring.account.pid, game: gameId, kind: 'invite' },
  });
  assert.equal(expiringInvite.status, 200, JSON.stringify(expiringInvite.body));
  env.advance(301);
  const hostInbox = await jsonCall(lobby, 'GET', '/v1/inbox', { token: host.token });
  assert.ok(hostInbox.body.notices.some(({ kind, invite }) =>
    kind === 'expired' && invite === expiringInvite.body.invite.id));
  assert.equal(lobby.social.pendingInvites.has(expiringInvite.body.invite.id), false);

  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/leave`, { token: host.token })).body.closed, true);
  const cancellationRoom = await createGame(lobby, host.token, { name: 'cancellation room' });
  const cancellation = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token,
    body: { v: 1, to: cancelling.account.pid, game: cancellationRoom, kind: 'invite' },
  });
  assert.equal(cancellation.status, 200, JSON.stringify(cancellation.body));
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${cancellationRoom}/leave`, {
    token: host.token,
  })).body.closed, true);
  assert.equal(lobby.social.pendingInvites.has(cancellation.body.invite.id), false);
});

test('an expired challenge handoff cannot authorize an invite', async () => {
  const { env, lobby } = setup();
  const sender = (await guest(lobby, 'Sender', '7'.repeat(32))).body;
  const recipient = (await guest(lobby, 'Recipient', '8'.repeat(32))).body;
  const source = await createGame(lobby, recipient.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/join`, {
    token: sender.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${source}/leave`, { token: sender.token })).body.left, true);
  const room = await createGame(lobby, sender.token, { password: 'private' });
  env.advance(121);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/invites', {
    token: sender.token,
    body: { v: 1, to: recipient.account.pid, game: room, kind: 'challenge', mode: 'normal' },
  })).status, 404);
});

test('Node social SQLite transactions preserve indexed rows across a real restart', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-social-'));
  const path = join(dir, 'social.sqlite');
  try {
    const first = createNodeSocialStore(path);
    await first.socialTransaction((tx) => {
      tx.put('player:0000000000000001', { pid: '0000000000000001' });
      tx.put('friend:0000000000000001:0000000000000002', { since: 1 });
      tx.put('friend:0000000000000001:0000000000000003', { since: 2 });
      return true;
    });
    first.close();
    const restarted = createNodeSocialStore(path);
    try {
      assert.deepEqual(await restarted.socialTransaction((tx) => tx.get('player:0000000000000001')),
        { pid: '0000000000000001' });
      assert.deepEqual([...await restarted.socialTransaction((tx) =>
        tx.list('friend:0000000000000001:', { after: 'friend:0000000000000001:0000000000000002', limit: 2 }))],
      [['friend:0000000000000001:0000000000000003', { since: 2 }]]);
    } finally {
      restarted.close();
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('canonical social routes create symmetric friendships, remove them, and do not echo attacker names', async () => {
  const { lobby } = setup();
  const alice = (await guest(lobby, 'Alice', '9'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'a'.repeat(32))).body;
  const me = await jsonCall(lobby, 'GET', '/v1/me', { token: alice.token });
  assert.equal(me.status, 200);
  assert.equal(me.body.pid, alice.account.pid);
  assert.equal(Object.hasOwn(me.body, 'install'), false);
  assert.equal((await jsonCall(lobby, 'PUT', '/v1/me/privacy', {
    token: alice.token,
    body: { v: 1, privacy: { invites: 'friends', challenges: 'friends', requests: 'nobody', presence: 'nobody' } },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: alice.token, body: { v: 1, state: 'matching' },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/me/recovery', {
    token: alice.token, body: { v: 1, install: '9'.repeat(32) },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'GET', '/v1/social/me', { token: alice.token })).status, 404);
  assert.equal((await jsonCall(lobby, 'PUT', '/v1/privacy', {
    token: alice.token, body: { v: 1, privacy: {} },
  })).status, 404);
  const unknownField = await jsonCall(lobby, 'POST', '/v1/friends/requests', {
    token: alice.token,
    body: { v: 1, code: '0000000000', 'attacker-secret': 'do-not-echo' },
  });
  assert.equal(unknownField.status, 400);
  assert.equal(JSON.stringify(unknownField.body).includes('attacker-secret'), false);
  const unknownQuery = await jsonCall(lobby, 'GET', '/v1/friends?query-secret=do-not-echo', { token: alice.token });
  assert.equal(unknownQuery.status, 400);
  assert.equal(JSON.stringify(unknownQuery.body).includes('query-secret'), false);

  await befriend(lobby, alice, bob);
  const aliceFriends = await jsonCall(lobby, 'GET', '/v1/friends', { token: alice.token });
  const bobFriends = await jsonCall(lobby, 'GET', '/v1/friends', { token: bob.token });
  assert.deepEqual(aliceFriends.body.friends.map((friend) => friend.pid), [bob.account.pid]);
  assert.deepEqual(bobFriends.body.friends.map((friend) => friend.pid), [alice.account.pid]);
  const removed = await jsonCall(lobby, 'DELETE', `/v1/friends/${bob.account.pid}`, { token: alice.token });
  assert.equal(removed.status, 200, JSON.stringify(removed.body));
  assert.deepEqual((await jsonCall(lobby, 'GET', '/v1/friends', { token: alice.token })).body.friends, []);
});

test('only the addressed recipient can accept a request and missing indexed players fail closed', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', '1'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', '2'.repeat(32))).body;
  const outsider = (await guest(lobby, 'Outsider', '3'.repeat(32))).body;
  const bobMe = (await jsonCall(lobby, 'GET', '/v1/me', { token: bob.token })).body;
  const request = await jsonCall(lobby, 'POST', '/v1/friends/requests', {
    token: alice.token, body: { v: 1, code: bobMe.code },
  });
  assert.equal(request.status, 200);
  const id = request.body.request.id;
  const stored = `request:${id}`;
  const wrongRecipient = await jsonCall(lobby, 'POST', `/v1/friends/requests/${id}/accept`, {
    token: outsider.token, body: { v: 1 },
  });
  assert.equal(wrongRecipient.status, 404, JSON.stringify(wrongRecipient.body));
  assert.ok(store.data.has(stored));
  assert.deepEqual((await jsonCall(lobby, 'GET', '/v1/inbox', { token: bob.token })).body.requests.map(({ id: value }) => value), [id]);

  store.data.delete(`player:${alice.account.pid}`);
  const corrupt = await jsonCall(lobby, 'POST', `/v1/friends/requests/${id}/accept`, {
    token: bob.token, body: { v: 1 },
  });
  assert.equal(corrupt.status, 503, JSON.stringify(corrupt.body));
  assert.ok(store.data.has(stored));
  assert.equal(store.data.has(`friend:${alice.account.pid}:${bob.account.pid}`), false);
});

test('recovery is single-use, rebinds generation, and revokes the old install and token', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', '4'.repeat(32))).body;
  const occupied = (await guest(lobby, 'Occupied', '5'.repeat(32))).body;
  const recovery = await jsonCall(lobby, 'POST', '/v1/me/recovery', {
    token: alice.token, body: { v: 1, install: '4'.repeat(32) },
  });
  assert.equal(recovery.status, 200);
  const code = recovery.body.code;
  const invalid = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name: 'Invalid recovery', install: '6'.repeat(32), recover: '0'.repeat(16) },
  });
  assert.equal(invalid.status, 401);
  assert.equal(invalid.body.error, 'bad_recovery');
  const conflict = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name: 'Occupied recovery', install: '5'.repeat(32), recover: code },
  });
  assert.equal(conflict.status, 409);
  assert.equal(conflict.body.error, 'install_in_use');

  const migrated = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name: 'Alice moved', install: '7'.repeat(32), recover: code },
  });
  assert.equal(migrated.status, 200, JSON.stringify(migrated.body));
  assert.equal(migrated.body.account.pid, alice.account.pid);
  assert.equal(store.data.get(`player:${alice.account.pid}`).recovery_hash, undefined);
  assert.equal((await jsonCall(lobby, 'GET', '/v1/me', { token: alice.token })).status, 401);
  const oldInstall = (await guest(lobby, 'Old install', '4'.repeat(32))).body;
  assert.notEqual(oldInstall.account.pid, alice.account.pid);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name: 'Reuse', install: '8'.repeat(32), recover: code },
  })).body.error, 'bad_recovery');
  assert.equal(JSON.stringify([...store.data.values()]).includes(code), false);
});

test('incoming request eviction removes paired indexes and acceptance enforces the 200-friend cap', async () => {
  const { env, lobby, store } = setup();
  const target = (await guest(lobby, 'Target', '9'.repeat(32))).body;
  const requester = (await guest(lobby, 'Requester', 'a'.repeat(32))).body;
  const targetCode = (await jsonCall(lobby, 'GET', '/v1/me', { token: target.token })).body.code;
  const now = Math.floor(env.now() / 1000);
  const seededIds = [];
  let oldestSender;
  await store.socialTransaction((tx) => {
    for (let index = 0; index < 50; index++) {
      const from = seedPlayer(tx, 1000 + index);
      if (index === 0) oldestSender = from;
      const id = (index + 1).toString(16).padStart(16, '0');
      const [first, second] = [from, target.account.pid].sort();
      tx.put(`request:${id}`, { id, from, to: target.account.pid, at: now - 50 + index });
      tx.put(`request-in:${target.account.pid}:${id}`, true);
      tx.put(`request-out:${from}:${id}`, true);
      tx.put(`request-pair:${first}:${second}:${id}`, true);
      seededIds.push(id);
    }
  });
  const added = await jsonCall(lobby, 'POST', '/v1/friends/requests', {
    token: requester.token, body: { v: 1, code: targetCode },
  });
  assert.equal(added.status, 200, JSON.stringify(added.body));
  assert.equal(store.data.has(`request:${seededIds[0]}`), false);
  assert.equal(store.data.has(`request-in:${target.account.pid}:${seededIds[0]}`), false);
  assert.equal(store.data.has(`request-out:${oldestSender}:${seededIds[0]}`), false);
  const [oldFirst, oldSecond] = [oldestSender, target.account.pid].sort();
  assert.equal(store.data.has(`request-pair:${oldFirst}:${oldSecond}:${seededIds[0]}`), false);
  const newRequest = added.body.request.id;
  assert.equal(store.data.has(`request-in:${target.account.pid}:${newRequest}`), true);
  assert.equal([...store.data.keys()].filter((key) => key.startsWith(`request-in:${target.account.pid}:`)).length, 50);
  assert.equal(lobby.social.notices.has(oldestSender), false);
  assert.equal(lobby.social.notices.has(target.account.pid), false);

  const friendOwner = (await guest(lobby, 'Friend-cap owner', 'b'.repeat(32))).body;
  const friendRequester = (await guest(lobby, 'Friend-cap requester', 'c'.repeat(32))).body;
  const requestId = 'f'.repeat(16);
  await store.socialTransaction((tx) => {
    for (let index = 0; index < 200; index++) {
      const peer = seedPlayer(tx, 3000 + index);
      const edge = `friend:${friendOwner.account.pid}:${peer}`;
      tx.put(edge, { since: index + 1 });
      tx.put(`friend:${peer}:${friendOwner.account.pid}`, { since: index + 1 });
    }
    const [first, second] = [friendRequester.account.pid, friendOwner.account.pid].sort();
    tx.put(`request:${requestId}`, {
      id: requestId, from: friendRequester.account.pid, to: friendOwner.account.pid, at: now,
    });
    tx.put(`request-in:${friendOwner.account.pid}:${requestId}`, true);
    tx.put(`request-out:${friendRequester.account.pid}:${requestId}`, true);
    tx.put(`request-pair:${first}:${second}:${requestId}`, true);
  });
  const capped = await jsonCall(lobby, 'POST', `/v1/friends/requests/${requestId}/accept`, {
    token: friendOwner.token, body: { v: 1 },
  });
  assert.equal(capped.status, 409);
  assert.equal(capped.body.error, 'friend_limit');
  assert.equal(store.data.has(`request:${requestId}`), true);
  assert.equal(store.data.has(`friend:${friendOwner.account.pid}:${friendRequester.account.pid}`), false);
});

test('empty read markers stay empty and history reads survive block and unfriend', async () => {
  const { lobby } = setup();
  const alice = (await guest(lobby, 'Alice', '4'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', '5'.repeat(32))).body;
  await befriend(lobby, alice, bob);

  const emptyRead = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/read`, {
    token: alice.token, body: { v: 1, through: 0 },
  });
  assert.equal(emptyRead.status, 200, JSON.stringify(emptyRead.body));
  assert.equal(emptyRead.body.read_id, 0);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: '4'.repeat(32), text: 'first' },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/blocks', {
    token: alice.token, body: { v: 1, pid: bob.account.pid },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'DELETE', `/v1/friends/${bob.account.pid}`, { token: alice.token })).status, 200);
  const readAfterRemoval = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/read`, {
    token: alice.token, body: { v: 1, through: 1 },
  });
  assert.equal(readAfterRemoval.status, 200, JSON.stringify(readAfterRemoval.body));
  assert.equal(readAfterRemoval.body.read_id, 1);
  const historyAfterRemoval = await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token,
  });
  assert.equal(historyAfterRemoval.status, 200, JSON.stringify(historyAfterRemoval.body));
  assert.equal(historyAfterRemoval.body.messages[0].text, 'first');
});

test('chat replay validates the stored receipt and message reference before returning it', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', '6'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', '7'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const clientId = '8'.repeat(32);
  const sent = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: clientId, text: 'stored' },
  });
  assert.equal(sent.status, 200);
  const [first, second] = [alice.account.pid, bob.account.pid].sort();
  const receiptKey = `receipt:${alice.account.pid}:${first}:${second}:${clientId}`;
  const receipt = store.data.get(receiptKey);
  store.data.set(receiptKey, { ...receipt, message: { ...receipt.message, to: alice.account.pid } });
  const replay = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: clientId, text: 'stored' },
  });
  assert.equal(replay.status, 503, JSON.stringify(replay.body));
  assert.equal(JSON.stringify(replay.body).includes('client_id'), false);
});

test('corrupt social player, read, and message rows return named storage failures', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', '9'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'a'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const badNick = store.data.get(`player:${alice.account.pid}`);
  store.data.set(`player:${alice.account.pid}`, { ...badNick, privacy: { presence: 'nobody' } });
  const me = await jsonCall(lobby, 'GET', '/v1/me', { token: alice.token });
  assert.equal(me.status, 503, JSON.stringify(me.body));
  assert.equal(me.body.error, 'social_unavailable');
  assert.equal(JSON.stringify(me.body).includes('cause'), false);

  store.data.set(`player:${alice.account.pid}`, badNick);
  const sent = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'b'.repeat(32), text: 'history' },
  });
  assert.equal(sent.status, 200);
  const [first, second] = [alice.account.pid, bob.account.pid].sort();
  store.data.set(`message:${first}:${second}:0000000000000001`, { ...sent.body.message, id: -1 });
  const history = await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages`, { token: alice.token });
  assert.equal(history.status, 503, JSON.stringify(history.body));
  assert.equal(history.body.error, 'social_unavailable');
});

test('guest sign-in refuses nicknames that cannot be stored as valid social players', async () => {
  const { lobby, store } = setup();
  const invalidNick = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    body: { name: 'bad\ud800name', install: 'c'.repeat(32) },
  });
  assert.equal(invalidNick.status, 400, JSON.stringify(invalidNick.body));
  assert.equal([...store.data.keys()].some((key) => key.startsWith('player:')), false);
});

test('raw malformed JSON and unknown query sends charge authenticated chat attempts once', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', 'c'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'd'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const path = `/v1/chat/${bob.account.pid}/messages`;
  const malformed = await call(lobby, 'POST', path, { token: alice.token, raw: '{"v":1,' });
  assert.equal(malformed.status, 400);
  assert.equal(JSON.parse(malformed.body).error, 'bad_json');
  const afterMalformed = store.data.get(`quota:chat-minute-pid:${alice.account.pid}`);
  assert.equal(afterMalformed.attempts.length, 1);
  assert.equal([...store.data.keys()].filter((key) => key.startsWith('message:')).length, 0);

  const unknownQuery = await call(lobby, 'POST', `${path}?attacker=secret`, {
    token: alice.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'not sent' },
  });
  assert.equal(unknownQuery.status, 400);
  assert.equal(JSON.parse(unknownQuery.body).error, 'bad_query');
  assert.equal(store.data.get(`quota:chat-minute-pid:${alice.account.pid}`).attempts.length, 2);
  assert.equal(unknownQuery.body.includes('secret'), false);
});

test('malformed friend, invite, and recovery mutations consume their attempt windows', async () => {
  const { lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', 'a'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'b'.repeat(32))).body;
  const bobCode = (await jsonCall(lobby, 'GET', '/v1/me', { token: bob.token })).body.code;

  const badRequest = await jsonCall(lobby, 'POST', '/v1/friends/requests?unknown=1', {
    token: alice.token, body: { v: 1, code: bobCode },
  });
  assert.equal(badRequest.status, 400);
  assert.equal(badRequest.body.error, 'bad_query');
  const badInvite = await jsonCall(lobby, 'POST', '/v1/invites?unknown=1', {
    token: alice.token, body: { v: 1 },
  });
  assert.equal(badInvite.status, 400);
  assert.equal(badInvite.body.error, 'bad_query');
  const badRecovery = await call(lobby, 'POST', '/v1/me/recovery', {
    token: alice.token, raw: '{"v":',
  });
  assert.equal(badRecovery.status, 400);
  assert.equal(JSON.parse(badRecovery.body).error, 'bad_json');
  const malformedGuestRecovery = await jsonCall(lobby, 'POST', '/v1/auth/guest', {
    ip: '198.51.100.2',
    body: { name: 'Malformed recovery', install: 'c'.repeat(32), recover: 'not-a-code' },
  });
  assert.equal(malformedGuestRecovery.status, 400);
  assert.equal(malformedGuestRecovery.body.error, 'bad_field');
  const ipHash = await lobby.social.hash('ip', '198.51.100.1');
  for (const key of [
    `quota:request-code-pid:${alice.account.pid}`,
    `quota:request-ip-hour:${ipHash}`,
    `quota:invite-pid:${alice.account.pid}`,
    `quota:invite-ip:${ipHash}`,
    `quota:recovery-pid:${alice.account.pid}`,
    `quota:recovery-ip:${ipHash}`,
  ]) {
    assert.equal((await store.socialTransaction((tx) => tx.get(key)))?.attempts.length, 1, key);
  }
  const malformedRecoveryHash = await lobby.social.hash('ip', '198.51.100.2');
  assert.equal((await store.socialTransaction((tx) =>
    tx.get(`quota:recovery-ip:${malformedRecoveryHash}`)))?.attempts.length, 1);
  assert.equal([...store.data.keys()].some((key) => key.startsWith('request:')), false);
  assert.equal(lobby.social.pendingInvites.size, 0);
  assert.equal([...store.data.keys()].some((key) => key.startsWith('recovery:')), false);
});

test('authenticated malformed request sends hit the fixed cap and return Retry-After', async () => {
  const { lobby } = setup();
  const alice = (await guest(lobby, 'Alice', 'c'.repeat(32))).body;
  const path = '/v1/friends/requests?unknown=1';
  for (let index = 0; index < 10; index++) {
    const response = await call(lobby, 'POST', path, {
      token: alice.token, body: { v: 1 },
    });
    assert.equal(response.status, 400);
    assert.equal(JSON.parse(response.body).error, 'bad_query');
  }
  const limited = await call(lobby, 'POST', path, {
    token: alice.token, body: { v: 1 },
  });
  assert.equal(limited.status, 429);
  assert.equal(JSON.parse(limited.body).error, 'rate_limited');
  assert.equal(limited.headers['retry-after'], '3600');
});

test('privacy, shadow routing, and chat mute hide presence and restrict only chat surfaces', async () => {
  const { lobby } = setup();
  const aliceInstall = 'd'.repeat(32);
  const bobInstall = 'e'.repeat(32);
  const alice = (await guest(lobby, 'Alice', aliceInstall)).body;
  const bob = (await guest(lobby, 'Bob', bobInstall)).body;
  await befriend(lobby, alice, bob);
  const room = await createGame(lobby, alice.token);
  const now = Math.floor(lobby.env.now() / 1000);
  lobby.env.store.saveEnforcement = async () => {};
  const setEnforcement = async (install, routing, muteUntil = null) => {
    const previous = lobby.getEnforcement(install);
    await lobby.setEnforcement(install, {
      v: 1, version: (previous?.version ?? 0) + 1, block: null,
      mute_until: muteUntil, routing, notice: null, devices: [],
    });
  };
  await setEnforcement(aliceInstall, '', now + 3600);

  const mutedMessage = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'd'.repeat(32), text: 'muted' },
  });
  assert.equal(mutedMessage.status, 403);
  assert.equal(mutedMessage.body.error, 'chat_muted');
  const notedInvite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: alice.token,
    body: { v: 1, to: bob.account.pid, game: room, kind: 'invite', note: 'muted note' },
  });
  assert.equal(notedInvite.status, 403);
  assert.equal(notedInvite.body.error, 'chat_muted');
  const invite = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: alice.token, body: { v: 1, to: bob.account.pid, game: room, kind: 'invite' },
  });
  assert.equal(invite.status, 200, JSON.stringify(invite.body));
  const accepted = await jsonCall(lobby, 'POST', `/v1/invites/${invite.body.invite.id}/accept`, {
    token: bob.token, body: { v: 1, profile: PROFILE },
  });
  assert.equal(accepted.status, 200, JSON.stringify(accepted.body));

  assert.equal((await jsonCall(lobby, 'PUT', '/v1/me/privacy', {
    token: bob.token,
    body: { v: 1, privacy: { invites: 'friends_or_room', challenges: 'friends_or_room', requests: 'anyone', presence: 'nobody' } },
  })).status, 200);
  let visible = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token })).body.presence
    .find(({ pid }) => pid === bob.account.pid);
  assert.deepEqual(visible, { pid: bob.account.pid, state: 'offline', game: null, joinable: false, since: 0, map: null, mode: null });
  await jsonCall(lobby, 'PUT', '/v1/me/privacy', {
    token: bob.token,
    body: { v: 1, privacy: { invites: 'friends_or_room', challenges: 'friends_or_room', requests: 'anyone', presence: 'friends' } },
  });
  assert.equal((await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: bob.token, body: { v: 1, state: 'online', hidden: true },
  })).status, 200);
  visible = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token })).body.presence
    .find(({ pid }) => pid === bob.account.pid);
  assert.equal(visible.state, 'offline');
  await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: bob.token, body: { v: 1, state: 'online', hidden: false },
  });

  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${room}/leave`, { token: alice.token })).body.closed, true);
  await setEnforcement(bobInstall, 'shadow');
  visible = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token })).body.presence
    .find(({ pid }) => pid === bob.account.pid);
  assert.equal(visible.state, 'offline');
  await setEnforcement(aliceInstall, 'shadow');
  const shadowRoom = await createGame(lobby, alice.token, { name: 'shadow room' });
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${shadowRoom}/join`, {
    token: bob.token, body: { profile: PROFILE },
  })).status, 200);
  visible = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token })).body.presence
    .find(({ pid }) => pid === bob.account.pid);
  assert.equal(visible.state, 'in_room');
  assert.equal(visible.game, shadowRoom);

  await setEnforcement(aliceInstall, '');
  const crossRoute = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'e'.repeat(32), text: 'isolated' },
  });
  assert.equal(crossRoute.status, 404);
  await setEnforcement(aliceInstall, 'shadow');
  const allowed = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'f'.repeat(32), text: 'same shadow route' },
  });
  assert.equal(allowed.status, 200, JSON.stringify(allowed.body));
});

test('START storage failure leaves room and pending invite unchanged', async () => {
  const store = memorySocialStore();
  const { lobby } = setup({ store, config: { ...ENV, RELAYS: 'lab=192.0.2.10:7350' } });
  const host = (await guest(lobby, 'Host', 'e'.repeat(32))).body;
  const player = (await guest(lobby, 'Player', 'f'.repeat(32))).body;
  const invitee = (await guest(lobby, 'Invitee', '0'.repeat(32))).body;
  await befriend(lobby, host, player);
  await befriend(lobby, host, invitee);
  const gameId = await createGame(lobby, host.token);
  const sent = await jsonCall(lobby, 'POST', '/v1/invites', {
    token: host.token,
    body: { v: 1, to: invitee.account.pid, game: gameId, kind: 'invite' },
  });
  assert.equal(sent.status, 200, JSON.stringify(sent.body));
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/join`, {
    token: player.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/ready`, {
    token: player.token, body: { ready: true },
  })).status, 200);
  const before = structuredClone(lobby.games.get(gameId));

  store.failWrites(true);
  const started = await jsonCall(lobby, 'POST', `/v1/games/${gameId}/start`, { token: host.token });
  assert.equal(started.status, 503);
  assert.equal(started.body.error, 'social_unavailable');
  assert.deepEqual(lobby.games.get(gameId), before);
  assert.equal(lobby.social.pendingInvites.has(sent.body.invite.id), true);
  store.failWrites(false);
  const successfulStart = await jsonCall(lobby, 'POST', `/v1/games/${gameId}/start`, { token: host.token });
  assert.equal(successfulStart.status, 200, JSON.stringify(successfulStart.body));
  assert.equal(lobby.social.pendingInvites.has(sent.body.invite.id), false);
});

test('chat admission rolls back on transaction failure and reports full storage as 507', async () => {
  const store = memorySocialStore();
  const { lobby } = setup({ store });
  const alice = (await guest(lobby, 'Alice', '2'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', '3'.repeat(32))).body;
  await befriend(lobby, alice, bob);

  store.failWrites(true);
  const unavailable = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: '2'.repeat(32), text: 'must roll back' },
  });
  assert.equal(unavailable.status, 503);
  assert.equal(unavailable.body.error, 'social_unavailable');
  assert.equal([...store.data.keys()].some((key) =>
    key.startsWith('message:') || key.startsWith('receipt:') || key.startsWith('conversation:')), false);

  store.failWrites(true, 'full');
  const full = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: '3'.repeat(32), text: 'full store' },
  });
  assert.equal(full.status, 507);
  assert.equal(full.body.error, 'social_store_full');
  assert.equal([...store.data.keys()].some((key) =>
    key.startsWith('message:') || key.startsWith('receipt:') || key.startsWith('conversation:')), false);
});

test('Node SQLite rolls back partial chat writes and preserves attempts across reopen', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'openbfme-social-rollback-'));
  const databasePath = join(directory, 'social.sqlite');
  let disk = createNodeSocialStore(databasePath);
  const store = { socialTransaction: (fn) => disk.socialTransaction(fn) };
  try {
    const { lobby } = setup({ store });
    const alice = (await guest(lobby, 'Alice', '2'.repeat(32))).body;
    const bob = (await guest(lobby, 'Bob', '3'.repeat(32))).body;
    await befriend(lobby, alice, bob);
    const pair = [alice.account.pid, bob.account.pid].sort();
    const messagePrefix = `message:${pair[0]}:${pair[1]}:`;
    const receiptPrefix = `receipt:${alice.account.pid}:${pair[0]}:${pair[1]}:`;
    const quotaKey = `quota:chat-minute-pid:${alice.account.pid}`;
    const dailyQuotaKey = `quota:chat-day-pid:${alice.account.pid}`;
    const originalUpsert = disk.upsert;
    let failure = 'SQLITE_FULL';
    let messageWritten = false;
    disk.upsert = {
      run(key, value) {
        if (key.startsWith(messagePrefix)) messageWritten = true;
        if (key.startsWith(receiptPrefix) && failure) {
          assert.equal(messageWritten, true);
          if (failure === 'SQLITE_FULL') throw Object.assign(new Error('SQLITE_FULL at receipt write'), { code: 'SQLITE_FULL' });
          throw new SocialError(409, 'injected_domain', 'injected domain failure');
        }
        return originalUpsert.run(key, value);
      },
    };

    async function assertNoPartialWrite() {
      const rows = await store.socialTransaction((tx) => ({
        messages: tx.list(messagePrefix, { limit: 100 }).size,
        receipts: tx.list(receiptPrefix, { limit: 100 }).size,
        conversation: tx.get(`conversation:${pair[0]}:${pair[1]}`),
        aliceThread: tx.get(`thread:${alice.account.pid}:${bob.account.pid}`),
        bobThread: tx.get(`thread:${bob.account.pid}:${alice.account.pid}`),
        globalCount: tx.get('quota:chat-global-current'),
      }));
      assert.deepEqual(rows, {
        messages: 0, receipts: 0, conversation: null,
        aliceThread: null, bobThread: null, globalCount: null,
      });
    }

    const full = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'retry after full' },
    });
    assert.equal(full.status, 507, JSON.stringify(full.body));
    assert.equal(full.body.error, 'social_store_full');
    assert.equal(messageWritten, true);
    await assertNoPartialWrite();
    assert.equal((await store.socialTransaction((tx) => tx.get(quotaKey))).attempts.length, 1);
    assert.equal((await store.socialTransaction((tx) => tx.get(dailyQuotaKey))).attempts.length, 1);

    failure = 'domain';
    messageWritten = false;
    const domain = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token, body: { v: 1, client_id: 'b'.repeat(32), text: 'rollback domain error' },
    });
    assert.equal(domain.status, 409, JSON.stringify(domain.body));
    assert.equal(domain.body.error, 'injected_domain');
    assert.equal(messageWritten, true);
    await assertNoPartialWrite();
    assert.equal((await store.socialTransaction((tx) => tx.get(quotaKey))).attempts.length, 2);
    assert.equal((await store.socialTransaction((tx) => tx.get(dailyQuotaKey))).attempts.length, 2);

    failure = null;
    disk.upsert = originalUpsert;
    const retried = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'retry after full' },
    });
    assert.equal(retried.status, 200, JSON.stringify(retried.body));
    assert.equal(retried.body.message.id, 1);
    assert.equal((await store.socialTransaction((tx) => tx.get('quota:chat-global-current'))).count, 1);
    assert.equal((await store.socialTransaction((tx) => tx.get(quotaKey))).attempts.length, 3);

    disk.close();
    disk = createNodeSocialStore(databasePath);
    const reopened = setup({ store: { socialTransaction: (fn) => disk.socialTransaction(fn) } }).lobby;
    const signedBackIn = (await guest(reopened, 'Alice', '2'.repeat(32))).body;
    assert.equal(signedBackIn.account.pid, alice.account.pid);
    const history = await jsonCall(reopened, 'GET', `/v1/chat/${bob.account.pid}/messages`, { token: signedBackIn.token });
    assert.deepEqual(history.body.messages.map(({ text }) => text), ['retry after full']);
    const replay = await jsonCall(reopened, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: signedBackIn.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'retry after full' },
    });
    assert.equal(replay.status, 200, JSON.stringify(replay.body));
    assert.equal(replay.body.message.id, 1);
    assert.equal((await store.socialTransaction((tx) => tx.get(quotaKey))).attempts.length, 3);
  } finally {
    disk.close();
    rmSync(directory, { recursive: true, force: true });
  }
});

test('global daily chat cap permits exact replay and resets at UTC midnight', async () => {
  const { env, lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', '2'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', '3'.repeat(32))).body;
  await befriend(lobby, alice, bob);

  const now = Math.floor(env.now() / 1000);
  const date = new Date(now * 1000);
  const today = date.toISOString().slice(0, 10);
  const nextDay = new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate() + 1));
  const retryAfter = Math.floor(nextDay.getTime() / 1000) - now;
  await store.socialTransaction((tx) => tx.put('quota:chat-global-current', { day: today, count: 9999 }));

  const admitted = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'last global slot' },
  });
  assert.equal(admitted.status, 200, JSON.stringify(admitted.body));
  assert.equal((await store.socialTransaction((tx) => tx.get('quota:chat-global-current'))).count, 10000);

  const replay = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'a'.repeat(32), text: 'last global slot' },
  });
  assert.equal(replay.status, 200, JSON.stringify(replay.body));
  assert.deepEqual(replay.body.message, admitted.body.message);
  assert.deepEqual(await store.socialTransaction((tx) => tx.get('quota:chat-global-current')), { day: today, count: 10000 });

  const limited = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'b'.repeat(32), text: 'wait for tomorrow' },
  });
  assert.equal(limited.status, 429);
  assert.equal(limited.body.error, 'rate_limited');
  assert.equal(limited.body.retry_after, retryAfter);

  env.advance(retryAfter);
  const nextDaySend = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token, body: { v: 1, client_id: 'b'.repeat(32), text: 'new UTC day' },
  });
  assert.equal(nextDaySend.status, 200, JSON.stringify(nextDaySend.body));
  assert.deepEqual(await store.socialTransaction((tx) => tx.get('quota:chat-global-current')), {
    day: nextDay.toISOString().slice(0, 10), count: 1,
  });
});

test('closed game tombstones stay closed when lobby state is loaded', async () => {
  const { env, lobby } = setup();
  const host = (await guest(lobby, 'Host', '9'.repeat(32))).body;
  const gameId = await createGame(lobby, host.token);
  const closed = structuredClone(lobby.games.get(gameId));
  closed.state = 'closed';
  const restarted = new Lobby(env, configFromEnv(ENV));
  restarted.load([closed]);
  assert.equal(restarted.games.has(gameId), false);
});

test('started-game leave goes offline, prefers a new open room, and the left game is not rejoined (ONLINEUX-1)', async () => {
  const { env, lobby } = setup({ config: { ...ENV, RELAYS: 'lab=192.0.2.10:7350' } });
  const host = (await guest(lobby, 'Host', '1'.repeat(32))).body;
  const target = (await guest(lobby, 'Target', '2'.repeat(32))).body;
  const watcher = (await guest(lobby, 'Watcher', '3'.repeat(32))).body;
  await befriend(lobby, watcher, target);
  const gameId = await createGame(lobby, host.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/join`, {
    token: target.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/ready`, {
    token: target.token, body: { ready: true },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/start`, { token: host.token })).status, 200);

  env.advance(151);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: target.token, body: { v: 1, state: 'online' },
  })).status, 200);
  assert.equal(lobby.social.active.has(target.account.pid), true);
  const staleActivePresence = await jsonCall(lobby, 'GET', '/v1/inbox', { token: watcher.token });
  assert.equal(staleActivePresence.body.presence.find(({ pid }) => pid === target.account.pid).state, 'playing');
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${gameId}/leave`, { token: target.token })).status, 200);
  await jsonCall(lobby, 'POST', '/v1/me/status', { token: target.token, body: { v: 1, state: 'online' } });
  assert.equal((await jsonCall(lobby, 'GET', '/v1/inbox', { token: watcher.token }))
    .body.presence.find(({ pid }) => pid === target.account.pid).state, 'online');

  const currentRoom = await createGame(lobby, target.token, { name: 'current room' });
  let presence = (await jsonCall(lobby, 'GET', '/v1/inbox', { token: watcher.token }))
    .body.presence.find(({ pid }) => pid === target.account.pid);
  assert.equal(presence.state, 'in_room');
  assert.equal(presence.game, currentRoom);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${currentRoom}/leave`, { token: target.token })).body.closed, true);

  // ONLINEMENU-1 (merge with ONLINEUX-1): a player who left a started game is done with it: its rejoin is refused by name and presence stays as it was
  const rejoin = await jsonCall(lobby, 'POST', `/v1/games/${gameId}/rejoin`, { token: target.token });
  assert.equal(rejoin.status, 409);
  assert.equal(rejoin.body.error, 'left');
  assert.notEqual((await jsonCall(lobby, 'GET', '/v1/inbox', { token: watcher.token }))
    .body.presence.find(({ pid }) => pid === target.account.pid).state, 'playing');
});

test('SQLite-backed chat preserves read state, receipts, and history across restart and unfriend', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'openbfme-social-lifecycle-'));
  const databasePath = join(dir, 'social.sqlite');
  let disk = createNodeSocialStore(databasePath);
  const store = { socialTransaction: (fn) => disk.socialTransaction(fn) };
  try {
    const { env, lobby } = setup({ store });
    const alice = (await guest(lobby, 'Alice', 'b'.repeat(32))).body;
    const bob = (await guest(lobby, 'Bob', 'c'.repeat(32))).body;
    const charlie = (await guest(lobby, 'Charlie', 'd'.repeat(32))).body;
    await befriend(lobby, alice, bob);
    await befriend(lobby, alice, charlie);
    const first = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: '1'.repeat(32), text: 'first 🐎' },
    });
    assert.equal(first.status, 200, JSON.stringify(first.body));
    assert.equal((await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/read`, {
      token: alice.token, body: { v: 1, through: 1 },
    })).body.read_id, 1);
    const second = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: '2'.repeat(32), text: 'second' },
    });
    assert.equal(second.status, 200);
    const charlieMessage = await jsonCall(lobby, 'POST', `/v1/chat/${charlie.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: '3'.repeat(32), text: 'new friend thread' },
    });
    assert.equal(charlieMessage.status, 200);
    const inbox = await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token });
    assert.deepEqual(inbox.body.chat, [
      { pid: bob.account.pid, last_id: 2, read_id: 1 },
      { pid: charlie.account.pid, last_id: 1, read_id: 0 },
    ].sort((a, b) => a.pid.localeCompare(b.pid)));
    const replay = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: '2'.repeat(32), text: 'second' },
    });
    assert.equal(replay.status, 200);
    assert.deepEqual(replay.body.message, second.body.message);
    const mismatch = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: '2'.repeat(32), text: 'changed' },
    });
    assert.equal(mismatch.body.error, 'idempotency_conflict');

    const now = Math.floor(env.now() / 1000);
    const liveHeads = await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token });
    assert.deepEqual(liveHeads.body.chat, [
      { pid: bob.account.pid, last_id: 2, read_id: 1 },
      { pid: charlie.account.pid, last_id: 1, read_id: 0 },
    ].sort((a, b) => a.pid.localeCompare(b.pid)));
    const heads = await jsonCall(lobby, 'GET', '/v1/chat', { token: alice.token });
    assert.deepEqual(heads.body.threads.map(({ pid }) => pid), [bob.account.pid, charlie.account.pid].sort());
    assert.equal(heads.body.threads.find(({ pid }) => pid === bob.account.pid).read_id, 1);
    const firstPage = await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages?limit=1`, { token: alice.token });
    assert.deepEqual(firstPage.body.messages.map(({ id }) => id), [1]);
    assert.equal(firstPage.body.next, 1);
    const secondPage = await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages?after=1&limit=1`, { token: alice.token });
    assert.deepEqual(secondPage.body.messages.map(({ id }) => id), [2]);
    assert.equal((await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/read`, {
      token: alice.token, body: { v: 1, through: 0 },
    })).body.read_id, 1);
    assert.equal((await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages`, { token: charlie.token })).status, 404);
    assert.equal((await jsonCall(lobby, 'POST', '/v1/blocks', {
      token: alice.token, body: { v: 1, pid: bob.account.pid },
    })).status, 200);
    assert.equal((await jsonCall(lobby, 'GET', `/v1/chat/${bob.account.pid}/messages`, { token: alice.token })).status, 200);

    const oldConversation = `/v1/chat/${bob.account.pid}/messages`;
    assert.equal((await jsonCall(lobby, 'DELETE', `/v1/friends/${bob.account.pid}`, { token: alice.token })).status, 200);
    env.advance(91 * 24 * 60 * 60);
    disk.close();
    disk = createNodeSocialStore(databasePath);
    const restarted = new Lobby(env, configFromEnv(ENV));
    const signedBackIn = (await guest(restarted, 'Alice', 'b'.repeat(32))).body;
    const history = await jsonCall(restarted, 'GET', oldConversation, { token: signedBackIn.token });
    assert.equal(history.status, 200, JSON.stringify(history.body));
    assert.deepEqual(history.body.messages.map((message) => message.text), ['first 🐎', 'second']);
    assert.equal((await jsonCall(restarted, 'DELETE', `/v1/blocks/${bob.account.pid}`, { token: signedBackIn.token })).status, 200);
    const bobAfterRestart = (await guest(restarted, 'Bob', 'c'.repeat(32))).body;
    await befriend(restarted, signedBackIn, bobAfterRestart);
    const replayAfterRestart = await jsonCall(restarted, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: signedBackIn.token,
      body: { v: 1, client_id: '2'.repeat(32), text: 'second' },
    });
    assert.equal(replayAfterRestart.status, 200);
    assert.equal(replayAfterRestart.body.message.id, 2);
    assert.equal((await jsonCall(restarted, 'GET', oldConversation, { token: signedBackIn.token })).body.messages.length, 2);
  } finally {
    disk.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test('quota cleanup retains staggered active attempts and malformed chat sends consume limits', async () => {
  const { env, lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', 'b'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'c'.repeat(32))).body;
  await befriend(lobby, alice, bob);
  const now = Math.floor(env.now() / 1000);
  const quotaKey = `quota:chat-minute-pid:${alice.account.pid}`;
  const oldIndex = `quota-expire:${String(now - 1).padStart(16, '0')}:${encodeURIComponent(quotaKey)}`;
  await store.socialTransaction((tx) => {
    tx.put(quotaKey, { attempts: [now - 61, now - 20], index: oldIndex });
    tx.put(oldIndex, { key: quotaKey, expires: now - 1 });
  });
  const charged = await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
    token: alice.token,
    body: { v: 1, client_id: 'not-a-client-id', text: 'charged despite malformed id' },
  });
  assert.equal(charged.status, 400);
  const retained = await store.socialTransaction((tx) => tx.get(quotaKey));
  assert.deepEqual(retained?.attempts, [now - 20, now]);

  const malformed = [];
  for (let i = 0; i < 31; i++) {
    malformed.push(await jsonCall(lobby, 'POST', `/v1/chat/${bob.account.pid}/messages`, {
      token: alice.token,
      body: { v: 1, client_id: 'not-a-client-id', text: 'charged despite malformed id' },
    }));
  }
  assert.equal(malformed[0].status, 400);
  assert.equal(malformed.at(-1).status, 429);
});

test('block cap uses the production list bound and blocked hosts do not invalidate seated joins', async () => {
  const { lobby, store } = setup();
  const host = (await guest(lobby, 'Host', '1'.repeat(32))).body;
  const seated = (await guest(lobby, 'Seated', '2'.repeat(32))).body;
  const outsider = (await guest(lobby, 'Outsider', '3'.repeat(32))).body;
  await befriend(lobby, host, seated);
  await befriend(lobby, host, outsider);
  const game = await createGame(lobby, host.token);
  assert.equal((await jsonCall(lobby, 'POST', `/v1/games/${game}/join`, {
    token: seated.token, body: { profile: PROFILE },
  })).status, 200);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/blocks', {
    token: host.token, body: { v: 1, pid: seated.account.pid },
  })).status, 200);
  const idempotent = await jsonCall(lobby, 'POST', `/v1/games/${game}/join`, {
    token: seated.token, body: { profile: PROFILE },
  });
  assert.equal(idempotent.status, 200, JSON.stringify(idempotent.body));
  assert.equal(idempotent.body.slot, 1);
  assert.equal((await jsonCall(lobby, 'POST', '/v1/blocks', {
    token: host.token, body: { v: 1, pid: outsider.account.pid },
  })).status, 200);
  assert.equal(await lobby.social.isBlocked(host.account.pid, outsider.account.pid), true);
  assert.equal(lobby.games.get(game).slots[lobby.games.get(game).hostSlot].pid, host.account.pid);
  const outsiderAccount = await lobby.account(`Bearer ${outsider.token}`);
  assert.equal(outsiderAccount.pid, outsider.account.pid);
  assert.equal(lobby.slotOf(lobby.games.get(game), outsiderAccount), -1,
    JSON.stringify({ outsider: outsiderAccount.id, slots: lobby.games.get(game).slots.map((slot) => slot.account) }));
  await assert.rejects(lobby.social.assertJoinAllowed(outsiderAccount, lobby.games.get(game)), { status: 404 });
  const blockedJoin = await jsonCall(lobby, 'POST', `/v1/games/${game}/join`, {
    token: outsider.token, body: { profile: PROFILE },
  });
  assert.equal(blockedJoin.status, 404, JSON.stringify(blockedJoin.body));

  const eligibleTarget = (await guest(lobby, 'Eligible target', 'f'.repeat(32))).body;
  const targetPid = eligibleTarget.account.pid;
  const fillerPids = Array.from({ length: 1000 }, (_, i) => (i + 1).toString(16).padStart(16, '0'));
  await store.socialTransaction((tx) => {
    for (const other of fillerPids) tx.put(`block:${host.account.pid}:${other}`, { note: '', at: 1 });
    const requestId = '1'.repeat(16);
    const [first, second] = [host.account.pid, targetPid].sort();
    tx.put(`request-pair:${first}:${second}:${requestId}`, true);
    tx.put(`request-out:${host.account.pid}:${requestId}`, { at: 1 });
    tx.put(`request-in:${targetPid}:${requestId}`, { at: 1 });
    tx.put(`request:${requestId}`, { id: requestId, from: host.account.pid, to: targetPid, at: 1 });
  });
  const capped = await jsonCall(lobby, 'POST', '/v1/blocks', {
    token: host.token, body: { v: 1, pid: targetPid },
  });
  assert.equal(capped.status, 409, JSON.stringify(capped.body));
  assert.equal(capped.body.error, 'block_limit');
});

test('active-player memory enforces its bound and prunes stale identities', async () => {
  const { env, lobby } = setup();
  const player = (await guest(lobby, 'Bounded player', 'a'.repeat(32))).body;
  const now = Math.floor(env.now() / 1000);
  let index = 0;
  while (lobby.social.active.size < 10_000) {
    const pid = (0xe000000000000000n + BigInt(index++)).toString(16);
    if (pid === player.account.pid) continue;
    lobby.social.active.set(pid, { state: 'online', since: now, lastActivity: now, lastRequest: now });
  }
  const full = await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: player.token, body: { v: 1, state: 'online' },
  });
  assert.equal(full.status, 503);
  assert.equal(full.body.error, 'social_overloaded');
  assert.equal(lobby.social.active.size, 10_000);
  env.advance(151);
  const recovered = await jsonCall(lobby, 'POST', '/v1/me/status', {
    token: player.token, body: { v: 1, state: 'online' },
  });
  assert.equal(recovered.status, 200, JSON.stringify(recovered.body));
  assert.equal(lobby.social.active.size, 1);
  assert.equal(lobby.social.active.has(player.account.pid), true);
});

test('unchanged inbox holds rearm to 25 seconds and permit 144 timeout polls per hour', async () => {
  const { env, lobby } = setup();
  const player = (await guest(lobby, 'Idle player', 'e'.repeat(32))).body;
  const initial = await jsonCall(lobby, 'GET', '/v1/inbox', { token: player.token });
  for (let i = 0; i < 144; i++) {
    const pending = jsonCall(lobby, 'GET', `/v1/inbox?since=${initial.body.seq}&wait=25`, { token: player.token });
    await waitForHolds(lobby, 1);
    assert.equal(env.pendingTimers()[0]?.delay, 25_000);
    env.advance(25);
    const result = await pending;
    assert.equal(result.status, 200);
    assert.equal(result.body.seq, initial.body.seq);
  }
  assert.equal(lobby.waiterCount, 0);
});

test('real inbox holds preserve deadlines, enforce bounds, abort, reset epochs, and revalidate storage', async () => {
  const { env, lobby, store } = setup();
  const alice = (await guest(lobby, 'Alice', 'd'.repeat(32))).body;
  const bob = (await guest(lobby, 'Bob', 'e'.repeat(32))).body;
  const flush = () => flushEvents(20);
  const initial = await jsonCall(lobby, 'GET', '/v1/inbox', { token: alice.token });
  const path = `/v1/inbox?since=${initial.body.seq}&wait=25`;

  const held = call(lobby, 'GET', path, { token: alice.token });
  await flush();
  assert.equal(lobby.waiterCount, 1);
  assert.equal(lobby.accountHolds.get(alice.account.id), 1);
  assert.equal(lobby.inboxPidHolds.get(alice.account.pid), 1);
  env.advance(10);
  lobby.wake(`inbox:${alice.account.pid}`);
  await flush();
  assert.equal(lobby.waiterCount, 1);
  assert.equal(env.pendingTimers()[0]?.delay, 15_000);
  env.advance(14.9);
  assert.equal(lobby.waiterCount, 1);
  env.advance(0.1);
  assert.equal((await held).status, 200);
  assert.equal(lobby.waiterCount, 0);

  const first = call(lobby, 'GET', path, { token: alice.token });
  const second = call(lobby, 'GET', path, { token: alice.token });
  await flush();
  assert.equal(lobby.waiterCount, 2);
  assert.equal(lobby.accountHolds.get(alice.account.id), 2);
  assert.equal(lobby.inboxPidHolds.get(alice.account.pid), 2);
  const third = await call(lobby, 'GET', path, { token: alice.token });
  assert.equal(JSON.parse(third.body).backoff, 10);
  lobby.wake(`inbox:${alice.account.pid}`);
  await flush();
  assert.equal(lobby.waiterCount, 2);
  assert.equal(env.pendingTimers()[0]?.delay, 25_000);
  env.advance(25);
  await Promise.all([first, second]);
  assert.equal(lobby.waiterCount, 0);

  lobby.config.maxHolds = 1;
  const globalFirst = call(lobby, 'GET', path, { token: alice.token });
  await flush();
  const globalSecond = await call(lobby, 'GET', path, { token: bob.token });
  assert.equal(JSON.parse(globalSecond.body).backoff, 10);
  assert.equal(lobby.waiterCount, 1);
  lobby.wake(`inbox:${alice.account.pid}`);
  await flush();
  assert.equal(lobby.waiterCount, 1);
  env.advance(25);
  await globalFirst;
  lobby.config.maxHolds = 1000;

  const controller = new AbortController();
  const aborted = call(lobby, 'GET', path, { token: alice.token, signal: controller.signal });
  await flush();
  controller.abort();
  const abortResponse = await aborted;
  assert.equal(JSON.parse(abortResponse.body).backoff, 10);
  assert.equal(lobby.waiterCount, 0);

  const resetPoll = call(lobby, 'GET', path, { token: alice.token });
  await flush();
  lobby.social.rotateInboxEpoch();
  lobby.wake(`inbox:${alice.account.pid}`);
  const resetResponse = await resetPoll;
  assert.equal(resetResponse.body.includes('"reset":true'), true);

  const resetBody = JSON.parse(resetResponse.body);
  const resetPath = `/v1/inbox?since=${resetBody.seq}&wait=25`;
  const corruptPoll = call(lobby, 'GET', resetPath, { token: alice.token });
  await flush();
  const player = store.data.get(`player:${alice.account.pid}`);
  store.data.set(`player:${alice.account.pid}`, { ...player, privacy: { presence: 'broken' } });
  lobby.social.invalidateInboxOwner(alice.account.pid);
  lobby.wake(`inbox:${alice.account.pid}`);
  const corruptResponse = await corruptPoll;
  assert.equal(corruptResponse.status, 503);
  assert.equal(JSON.parse(corruptResponse.body).error, 'social_unavailable');
});
