// ONLINEMENU-1 (owner 2026-10-20, docs/IDENTITY.md "E-mail"): the e-mail sign-in end to end through the lobby's routes: link, restore (moving a
// player to a new PC), confirm (a linked player's new recovery code), the pages, and every refusal; Turnstile and the mail function are mocked.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash, createHmac, randomBytes } from 'node:crypto';
import { Lobby, configFromEnv, route } from '../src/lobby.mjs';
import { createNodeSocialStore } from '../src/social-store-node.mjs';
import { nodeEnv } from '../src/server.mjs';
import { pkceChallenge } from '../src/oauth.mjs';
import { DISPOSABLE_DOMAINS } from '../src/disposable-domains.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const CONFIG = {
  TICKET_KEYS: `1:${'01'.repeat(32)}\n`,
  SESSION_KEY: 'c3'.repeat(32),
  ADMIN_KEY: 'a1'.repeat(32),
  MOD_KEY: 'b2'.repeat(32),
  RELAYS: '',
  PUBLIC_URL: 'https://lobby.test',
  OAUTH_DISCORD_CLIENT_ID: 'discord-test-client',
  OAUTH_DISCORD_CLIENT_SECRET: 'discord-test-secret',
  TURNSTILE_SITE_KEY: '0x4AAAAAAA-test-site',
  TURNSTILE_SECRET: '0x4AAAAAAA-test-secret',
  MAILER_URL: 'https://mailer.test/send',
  MAILER_KEY: 'd4'.repeat(32),
};

function harness() {
  const dir = mkdtempSync(join(tmpdir(), 'lobby-email-'));
  const state = { store: createNodeSocialStore(join(dir, 'social.sqlite')) };
  const enforcement = new Map();
  const env = nodeEnv({
    socialTransaction: (fn) => state.store.socialTransaction(fn),
    saveEnforcement: (record) => enforcement.set(record.install, structuredClone(record)),
    loadEnforcement: () => [...enforcement.values()].map((record) => structuredClone(record)),
    saveGame() {}, deleteGame() {}, saveSighting() {}, deleteSighting() {}, loadSightings: () => [],
    reportTransaction: async (fn) => fn({ get: () => null, put() {}, delete() {}, list: () => new Map() }),
  });
  let now = 1_700_000_000;
  env.now = () => now * 1000;
  env.sha256 = async (bytes) => new Uint8Array(createHash('sha256').update(bytes).digest());
  const mails = [];
  const net = { human: true, mailOk: true, turnstileCalls: 0 };
  env.oauthFetch = async (url, options) => {
    assert.equal(options.redirect, 'error');
    if (url.startsWith('https://challenges.cloudflare.com/')) {
      net.turnstileCalls += 1;
      return new Response(JSON.stringify({ success: net.human, action: 'email', hostname: 'lobby.test' }), { status: 200 });
    }
    if (url === CONFIG.MAILER_URL) {
      const time = options.headers['x-openbfme-time'];
      const signature = createHmac('sha256', Buffer.from(CONFIG.MAILER_KEY, 'hex')).update(`${time}\n${options.body}`).digest('hex');
      assert.equal(options.headers['x-openbfme-sig'], signature);
      mails.push(JSON.parse(options.body));
      return new Response(JSON.stringify(net.mailOk ? { ok: true } : { ok: false, error: 'send_failed' }), { status: net.mailOk ? 200 : 502 });
    }
    if (options.method === 'POST') return new Response(JSON.stringify({ token_type: 'Bearer', access_token: 'mock-access-token' }), { status: 200 });
    return new Response(JSON.stringify({ id: '1234567890' }), { status: 200 });
  };
  const lobby = new Lobby(env, configFromEnv(CONFIG));
  return {
    lobby, env, state, mails, net, enforcement,
    advance: (seconds) => { now += seconds; },
    close: () => { try { state.store.close(); } catch {} rmSync(dir, { recursive: true, force: true }); },
  };
}

async function call(lobby, method, path, { token, body, ip = '198.51.100.20' } = {}) {
  const url = new URL(path, 'https://lobby.test');
  const bytes = body === undefined ? new Uint8Array() : new TextEncoder().encode(JSON.stringify(body));
  const response = await route(lobby, {
    method, path: url.pathname, query: url.searchParams, headers: token ? { authorization: `Bearer ${token}` } : {}, ip, bytes: async () => bytes,
  });
  const result = response.deferred ? await response.deferred : response;
  const type = result.headers?.['content-type'] ?? '';
  return { status: result.status, headers: result.headers, body: type.startsWith('application/json') ? JSON.parse(result.body) : result.body };
}

function pollSecret() {
  const poll = randomBytes(32).toString('hex');
  return { poll, poll_hash: createHash('sha256').update(poll).digest('hex') };
}

// one full e-mail flow up to the confirmed link; returns the poll result
async function emailFlow(h, mode, { token, install, name = 'Mover', address = 'player@example.org', recovery, ip } = {}) {
  const secret = pollSecret();
  const start = await call(h.lobby, 'POST', '/v1/auth/email/start', {
    token, ip, body: { v: 1, mode, install, poll_hash: secret.poll_hash, ...(mode === 'restore' ? { name } : {}), ...(recovery ? { recovery } : {}) },
  });
  assert.equal(start.status, 200, JSON.stringify(start.body));
  assert.equal(start.body.url, `https://lobby.test/email/${start.body.ticket}`);
  const page = await call(h.lobby, 'GET', `/email/${start.body.ticket}`);
  assert.equal(page.status, 200);
  assert.ok(page.body.includes(CONFIG.TURNSTILE_SITE_KEY));
  assert.ok(page.headers['content-security-policy'].includes("frame-ancestors 'none'"));
  const before = h.mails.length;
  const request = await call(h.lobby, 'POST', '/v1/auth/email/request', { ip, body: { v: 1, ticket: start.body.ticket, email: address, turnstile: 'token-from-widget' } });
  assert.equal(request.status, 200, JSON.stringify(request.body));
  assert.equal(h.mails.length, before + 1);
  const mail = h.mails.at(-1);
  assert.equal(mail.to.toLowerCase(), address.toLowerCase()); // the domain is lowercased, the local part kept as typed
  const link = /https:\/\/lobby\.test\/email\/verify\/([A-Za-z0-9_-]{43})/.exec(mail.text)[1];
  // opening the link renders a button and changes nothing; the poll still waits
  const verifyPage = await call(h.lobby, 'GET', `/email/verify/${link}`);
  assert.equal(verifyPage.status, 200);
  assert.equal((await call(h.lobby, 'POST', '/v1/auth/email/poll', { body: { v: 1, ticket: start.body.ticket, poll: secret.poll } })).body.done, false);
  const verify = await call(h.lobby, 'POST', '/v1/auth/email/verify', { ip, body: { v: 1, token: link } });
  return { start, link, secret, verify, poll: verify.status === 200
    ? await call(h.lobby, 'POST', '/v1/auth/email/poll', { body: { v: 1, ticket: start.body.ticket, poll: secret.poll } }) : null };
}

async function storeText(state) {
  return state.store.socialTransaction((tx) => JSON.stringify([...tx.list('', { limit: 1000 })]));
}

test('the generated disposable-domain module matches data/disposable-domains.txt', () => {
  const text = readFileSync(join(HERE, '..', 'data', 'disposable-domains.txt'), 'utf8');
  const domains = text.split('\n').map((line) => line.trim()).filter((line) => line && !line.startsWith('#'));
  assert.deepEqual([...DISPOSABLE_DOMAINS], domains);
});

test('e-mail link, then moving the player to a new PC with it; the address is never stored', async (t) => {
  const h = harness();
  t.after(h.close);
  const alice = await h.lobby.guest({ name: 'Alice', install: 'a'.repeat(32) }, '198.51.100.20');
  const linked = await emailFlow(h, 'link', { token: alice.token, install: 'a'.repeat(32), address: 'Alice.Player@Example.org' });
  assert.equal(linked.verify.status, 200, JSON.stringify(linked.verify.body));
  assert.deepEqual(linked.poll.body, { v: 1, done: true, pid: alice.account.pid, providers: ['email'] });
  // the link was used: a second confirm is refused, the flow is gone after the poll
  assert.equal((await call(h.lobby, 'POST', '/v1/auth/email/verify', { body: { v: 1, token: linked.link } })).body.error, 'email_expired');
  const dump = await storeText(h.state);
  assert.equal(dump.toLowerCase().includes('alice.player'), false);
  assert.equal(dump.toLowerCase().includes('example.org'), false);
  // a new PC: restore with the same address (case-insensitive) moves the same player there
  const moved = await emailFlow(h, 'restore', { install: 'b'.repeat(32), address: 'alice.player@example.org', name: 'Alice laptop' });
  assert.equal(moved.verify.status, 200, JSON.stringify(moved.verify.body));
  assert.equal(moved.poll.body.account.pid, alice.account.pid);
  assert.deepEqual(moved.poll.body.providers, ['email']);
  assert.equal((await h.lobby.account(`Bearer ${moved.poll.body.token}`)).install, 'b'.repeat(32));
  await assert.rejects(h.lobby.account(`Bearer ${alice.token}`), (error) => error.code === 'session_revoked');
  const oldPc = await call(h.lobby, 'POST', '/v1/auth/guest', { body: { v: 1, name: 'Alice', install: 'a'.repeat(32) } });
  assert.match(oldPc.body.notice.text, /with its confirmed e-mail address/);
});

test('e-mail restore of an address no player uses, and a confirm that is not the linked address, change nothing', async (t) => {
  const h = harness();
  t.after(h.close);
  const nobody = await emailFlow(h, 'restore', { install: 'c'.repeat(32), address: 'stranger@example.org' });
  assert.equal(nobody.verify.status, 404);
  assert.equal(nobody.verify.body.error, 'oauth_not_linked');
  const bob = await h.lobby.guest({ name: 'Bob', install: 'd'.repeat(32) }, '198.51.100.21');
  assert.equal((await emailFlow(h, 'link', { token: bob.token, install: 'd'.repeat(32), address: 'bob@example.org' })).verify.status, 200);
  // a linked player without a code makes one by confirming the linked address
  const noCode = await call(h.lobby, 'POST', '/v1/me/recovery', { token: bob.token, body: { v: 1, install: 'd'.repeat(32) } });
  assert.equal(noCode.body.error, 'provider_confirm_required');
  const wrong = await emailFlow(h, 'confirm', { token: bob.token, install: 'd'.repeat(32), address: 'not-bob@example.org' });
  assert.equal(wrong.verify.body.error, 'oauth_not_this_player');
  const right = await emailFlow(h, 'confirm', { token: bob.token, install: 'd'.repeat(32), address: 'BOB@example.org' });
  assert.equal(right.verify.status, 200);
  assert.match(right.poll.body.code, /^[0-9A-HJKMNP-TV-Z]{16}$/);
  // and with it, the e-mail can be unlinked
  const unlinked = await call(h.lobby, 'POST', '/v1/auth/oauth/unlink', { token: bob.token, body: { v: 1, provider: 'email', install: 'd'.repeat(32), recovery: right.poll.body.code } });
  assert.deepEqual(unlinked.body.providers, []);
});

test('a second identity needs the recovery code; an address can belong to one player only; a banned address cannot be used', async (t) => {
  const h = harness();
  t.after(h.close);
  const carol = await h.lobby.guest({ name: 'Carol', install: 'e'.repeat(32) }, '198.51.100.22');
  const code = (await call(h.lobby, 'POST', '/v1/me/recovery', { token: carol.token, body: { v: 1, install: 'e'.repeat(32) } })).body.code;
  // Discord first (through the OAuth routes)
  const state = '1'.repeat(64);
  const verifier = 'v1'.repeat(22).slice(0, 44);
  const started = await h.lobby.social.oauthStart({ v: 1, provider: 'discord', mode: 'link', state, code_challenge: await pkceChallenge(h.env, verifier), install: 'e'.repeat(32) },
    { authorization: `Bearer ${carol.token}`, hasAuthorization: true, ip: '198.51.100.22' });
  await (await h.lobby.social.oauthFinish({ v: 1, transaction: started.transaction, state, code: 'c', code_verifier: verifier },
    { authorization: `Bearer ${carol.token}`, hasAuthorization: true, ip: '198.51.100.22' })).deferred;
  const secret = pollSecret();
  const noProof = await call(h.lobby, 'POST', '/v1/auth/email/start', { token: carol.token, body: { v: 1, mode: 'link', install: 'e'.repeat(32), poll_hash: secret.poll_hash } });
  assert.equal(noProof.body.error, 'recovery_required');
  const withProof = await emailFlow(h, 'link', { token: carol.token, install: 'e'.repeat(32), address: 'carol@example.org', recovery: code });
  assert.deepEqual(withProof.poll.body.providers, ['discord', 'email']);
  // the same address for another player: refused at the confirm (the page never says whether an address is in use)
  const dave = await h.lobby.guest({ name: 'Dave', install: 'f'.repeat(32) }, '198.51.100.23');
  const taken = await emailFlow(h, 'link', { token: dave.token, install: 'f'.repeat(32), address: 'carol@example.org' });
  assert.equal(taken.verify.body.error, 'email_in_use');
  // a ban on Carol covers her address: nobody can link it or move a player with it
  const bytes = new TextEncoder().encode(JSON.stringify({ v: 1, version: 1, block: { kind: 'ban', until: null, reason: 'cheating' }, mute_until: null, routing: '', notice: null, devices: [], block_devices: false }));
  const seconds = String(Math.floor(h.lobby.env.now() / 1000));
  const path = `/v1/admin/mod/enforcement/${'e'.repeat(32)}`;
  const signature = createHmac('sha256', Buffer.from(CONFIG.MOD_KEY, 'hex')).update(Buffer.concat([Buffer.from(`PUT ${path}?\n${seconds}\n`), Buffer.from(bytes)])).digest('hex');
  const ban = await route(h.lobby, { method: 'PUT', path, query: new URLSearchParams(), headers: { 'x-openbfme-admin': `${seconds}.${signature}` }, ip: '198.51.100.99', bytes: async () => bytes });
  assert.equal(ban.status, 200, ban.body);
  const banned = await emailFlow(h, 'restore', { install: '9'.repeat(32), address: 'carol@example.org' });
  assert.equal(banned.verify.body.error, 'identity_banned');
});

test('e-mail request refusals: disposable domains, a failed Turnstile check, bad addresses, expired pages and links, the poll secret, send limits', async (t) => {
  const h = harness();
  t.after(h.close);
  const erin = await h.lobby.guest({ name: 'Erin', install: '1'.repeat(32) }, '198.51.100.24');
  const secret = pollSecret();
  const start = await call(h.lobby, 'POST', '/v1/auth/email/start', { token: erin.token, body: { v: 1, mode: 'link', install: '1'.repeat(32), poll_hash: secret.poll_hash } });
  const ticket = start.body.ticket;
  const ask = (email, ip = '198.51.100.24') => call(h.lobby, 'POST', '/v1/auth/email/request', { ip, body: { v: 1, ticket, email, turnstile: 'widget-token' } });
  assert.equal((await ask('someone@mailinator.com')).body.error, 'disposable_email');
  assert.equal((await ask('someone@sub.mailinator.com')).body.error, 'disposable_email');
  assert.equal((await ask('not an address')).body.error, 'bad_email');
  assert.equal(h.net.turnstileCalls, 0); // refused before the check
  h.net.human = false;
  assert.equal((await ask('erin@example.org')).body.error, 'turnstile');
  h.net.human = true;
  assert.equal(h.mails.length, 0);
  // a send that fails at the mail function is told, and counts
  h.net.mailOk = false;
  assert.equal((await ask('erin@example.org')).body.error, 'email_send_failed');
  h.net.mailOk = true;
  assert.equal((await ask('erin@example.org')).status, 200);
  assert.equal((await ask('erin@example.org')).status, 200);
  // three sends per page, and the third to one address in an hour is the last
  assert.equal((await ask('erin@example.org')).body.error, 'rate_limited');
  // the wrong poll secret learns nothing
  const wrongPoll = await call(h.lobby, 'POST', '/v1/auth/email/poll', { body: { v: 1, ticket, poll: 'e'.repeat(64) } });
  assert.equal(wrongPoll.status, 404);
  // an expired link: refused, and the page then too
  const link = /verify\/([A-Za-z0-9_-]{43})/.exec(h.mails.at(-1).text)[1];
  h.advance(16 * 60);
  assert.equal((await call(h.lobby, 'POST', '/v1/auth/email/verify', { body: { v: 1, token: link } })).body.error, 'email_expired');
  h.advance(15 * 60);
  assert.equal((await ask('erin@example.org', '198.51.100.25')).body.error, 'email_expired');
  // pages with a malformed ticket or token render the error page
  assert.ok((await call(h.lobby, 'GET', '/email/%3Cscript%3E')).body.includes('not valid'));
  assert.equal((await call(h.lobby, 'GET', `/email/${'z'.repeat(48)}`)).headers['content-security-policy'].includes("script-src 'none'"), true);
});

test('refused and malformed e-mail requests write nothing; only admitted flows write', async (t) => {
  const h = harness();
  t.after(h.close);
  const frank = await h.lobby.guest({ name: 'Frank', install: '2'.repeat(32) }, '198.51.100.26');
  const secret = pollSecret();
  const start = await call(h.lobby, 'POST', '/v1/auth/email/start', { token: frank.token, body: { v: 1, mode: 'link', install: '2'.repeat(32), poll_hash: secret.poll_hash } });
  let writes = 0;
  const transact = h.state.store.socialTransaction.bind(h.state.store);
  h.state.store.socialTransaction = (fn) => transact((tx) => fn(new Proxy(tx, {
    get(target, property) {
      if (property === 'put' || property === 'delete') return (...args) => { writes += 1; return target[property](...args); };
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
  })));
  for (let i = 0; i < 5; i++) {
    await call(h.lobby, 'POST', '/v1/auth/email/start', { body: { v: 1 }, ip: `203.0.113.${i}` });
    await call(h.lobby, 'POST', '/v1/auth/email/request', { body: { v: 1, ticket: start.body.ticket, email: 'x@mailinator.com', turnstile: 't' }, ip: `203.0.113.${i}` });
    await call(h.lobby, 'POST', '/v1/auth/email/verify', { body: { v: 1, token: 'A'.repeat(43) }, ip: `203.0.113.${i}` });
    await call(h.lobby, 'POST', '/v1/auth/email/poll', { body: { v: 1, ticket: start.body.ticket, poll: secret.poll } });
  }
  h.net.human = false;
  await call(h.lobby, 'POST', '/v1/auth/email/request', { body: { v: 1, ticket: start.body.ticket, email: 'frank@example.org', turnstile: 't' } });
  assert.equal(writes, 0);
});

test('e-mail sign-in is off (503) until all four secrets are configured', async () => {
  // a partial set does not stop the lobby: e-mail sign-in answers 503 with the reason, and the health check names it
  const partial = new Lobby(nodeEnv({ socialTransaction: () => { throw new Error('unused'); } }), configFromEnv({ ...CONFIG, MAILER_KEY: '', MAILER_URL: '' }));
  assert.match(partial.health().emailConfig, /missing MAILER_URL, MAILER_KEY/);
  // ONLINEMENU-1 (BATCH-P3): the features a client turns on: social always, e-mail only once configured
  assert.ok(partial.health().features.includes('social'));
  assert.ok(!partial.health().features.includes('email'));
  assert.ok(new Lobby(nodeEnv({ socialTransaction: () => { throw new Error('unused'); } }), configFromEnv(CONFIG)).health().features.includes('email'));
  const refused = await call(partial, 'POST', '/v1/auth/email/start', { body: { v: 1, mode: 'restore' } });
  assert.equal(refused.status, 503);
  assert.match(refused.body.message, /missing MAILER_URL/);
  const { TURNSTILE_SITE_KEY, TURNSTILE_SECRET, MAILER_URL, MAILER_KEY, ...plain } = CONFIG;
  void TURNSTILE_SITE_KEY; void TURNSTILE_SECRET; void MAILER_URL; void MAILER_KEY;
  const lobby = new Lobby(nodeEnv({ socialTransaction: () => { throw new Error('unused'); } }), configFromEnv(plain));
  const page = await call(lobby, 'GET', `/email/${'a'.repeat(48)}`);
  assert.ok(page.body.includes('not available'));
});

test('Sol (identity review): one daily send cap for every mode, and made-up poll tickets leave nothing in memory', async (t) => {
  const h = harness();
  t.after(h.close);
  const greta = await h.lobby.guest({ name: 'Greta', install: '3'.repeat(32) }, '198.51.100.27');
  assert.equal((await emailFlow(h, 'link', { token: greta.token, install: '3'.repeat(32), address: 'greta@example.org' })).verify.status, 200);
  await emailFlow(h, 'restore', { install: '4'.repeat(32), address: 'greta@example.org' });
  const day = Math.floor(1_700_000_000 / 86400);
  const rows = await h.state.store.socialTransaction((tx) => [...tx.list('oauth2:quota:email:day:', { limit: 10 }).keys()]);
  assert.deepEqual(rows, [`oauth2:quota:email:day:all:${day}`]);
  assert.equal((await h.state.store.socialTransaction((tx) => tx.get(rows[0]))).count, 2);
  const before = h.lobby.social.oauthAddressCounts.size;
  for (let i = 0; i < 30; i++) {
    await call(h.lobby, 'POST', '/v1/auth/email/poll', { body: { v: 1, ticket: i.toString(16).padStart(48, '0'), poll: 'a'.repeat(64) } });
  }
  assert.equal(h.lobby.social.oauthAddressCounts.size, before);
});
