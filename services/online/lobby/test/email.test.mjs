// ONLINEMENU-1 (owner identity decisions 2026-10-20): e-mail verification helpers and pages (src/email.mjs, src/email-pages.mjs).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHmac } from 'node:crypto';
import { nodeEnv } from '../src/server.mjs';
import {
  EmailError,
  EmailProviderFailure,
  emailFingerprint,
  isDisposable,
  loadDisposableDomains,
  magicLinkMessage,
  normalizeEmail,
  sendMail,
  signMailRequest,
  verifyTurnstile,
} from '../src/email.mjs';
import { emailErrorPage, emailRequestPage, emailVerifyPage, pageHeaders } from '../src/email-pages.mjs';

const KEY = 'd4'.repeat(32);
const MAILER = { url: 'https://mailer.example.lambda-url.us-east-2.on.aws/', key: 'e5'.repeat(32) };
const ADDRESS = 'Private.Person@example.org';
const TICKET = '0123456789abcdef'.repeat(3);
const TOKEN = 'A'.repeat(21) + '_-' + 'z'.repeat(20);
const NONCE = 'bm9uY2Utbm9uY2Utbm9uY2U';

function envWith(oauthFetch, now = 1_760_000_000_000) {
  return { ...nodeEnv({}), now: () => now, oauthFetch };
}

function json(value, status = 200) {
  return new Response(JSON.stringify(value), { status, headers: { 'content-type': 'application/json' } });
}

// ---- normalization ----------------------------------------------------------------------------------------------------------------------------------------

test('normalizeEmail accepts plain addresses, lowercases the domain and keeps the local part', () => {
  assert.deepEqual(normalizeEmail('  Alice.Smith@Example.COM \n'), { address: 'Alice.Smith@example.com', domain: 'example.com' });
  assert.deepEqual(normalizeEmail('a+tag@mail.example.co.uk'), { address: 'a+tag@mail.example.co.uk', domain: 'mail.example.co.uk' });
  assert.deepEqual(normalizeEmail("o'brien_1@xn--bcher-kva.example"), { address: "o'brien_1@xn--bcher-kva.example", domain: 'xn--bcher-kva.example' });
  assert.equal(normalizeEmail(`${'x'.repeat(64)}@example.com`).address.length, 76);
  // No plus-address or dot folding.
  assert.notEqual(normalizeEmail('a.b+c@example.com').address, normalizeEmail('ab@example.com').address);
});

test('normalizeEmail refuses malformed addresses with EmailError, never echoing the input', () => {
  const refused = [
    '', '   ', 'no-at-sign', 'two@@example.com', 'a@b@example.com', '@example.com', 'alice@',
    `${'x'.repeat(65)}@example.com`, 'has space@example.com', '"quoted"@example.com', 'a(comment)@example.com',
    '.lead@example.com', 'trail.@example.com', 'dou..ble@example.com', 'ctl\u0001@example.com', 'tab\t@example.com',
    'alice@localhost', 'alice@[192.0.2.1]', 'alice@192.0.2.1', 'alice@-bad.com', 'alice@bad-.com', 'alice@ex_ample.com',
    'alice@example.c0m', 'alice@example.c', 'alice@exa..mple.com', 'alice@bücher.example', 'ålice@example.com',
    `alice@${'a'.repeat(64)}.com`, `alice@${('a'.repeat(60) + '.').repeat(5)}com`, `${'x'.repeat(64)}@${'d'.repeat(63)}.${'e'.repeat(63)}.${'f'.repeat(63)}.com`,
    'x'.repeat(2000), 42, null, undefined,
  ];
  for (const input of refused) {
    assert.throws(() => normalizeEmail(input), (error) => {
      assert.ok(error instanceof EmailError, String(input));
      assert.equal(error.code, 'email_invalid');
      if (typeof input === 'string' && input.trim().length > 3) assert.equal(error.message.includes(input.trim()), false);
      return true;
    });
  }
});

// ---- disposable domains -----------------------------------------------------------------------------------------------------------------------------------

test('the bundled disposable list loads and matches parent domains', () => {
  const set = loadDisposableDomains(readFileSync(new URL('../data/disposable-domains.txt', import.meta.url), 'utf8'));
  assert.ok(set.size > 1000);
  assert.equal(isDisposable('mailinator.com', set), true);
  assert.equal(isDisposable('sub.mailinator.com', set), true);
  assert.equal(isDisposable('a.b.mailinator.com', set), true);
  assert.equal(isDisposable('mailinator.com.example', set), false);
  for (const big of ['gmail.com', 'outlook.com', 'proton.me', 'yahoo.com', 'icloud.com']) assert.equal(isDisposable(big, set), false, big);
  assert.equal(isDisposable('example.org', set), false);
  assert.equal(isDisposable('com', set), false);
});

test('loadDisposableDomains ignores comments and blank lines (EMAIL_BLOCKLIST shape)', () => {
  const set = loadDisposableDomains('# header\n\nBad.Example\r\n  spam.test  # trailing note\n#ignored.test\n');
  assert.deepEqual([...set].sort(), ['bad.example', 'spam.test']);
  assert.equal(isDisposable('x.bad.example', set), true);
});

// ---- fingerprint ------------------------------------------------------------------------------------------------------------------------------------------

test('the e-mail fingerprint is HMAC-SHA256 over the label and the lowercased address, stable and case-insensitive', async () => {
  const env = nodeEnv({});
  const a = await emailFingerprint(env, KEY, normalizeEmail('Alice@Example.com'));
  const b = await emailFingerprint(env, KEY, normalizeEmail('alice@EXAMPLE.COM'));
  const c = await emailFingerprint(env, KEY, 'ALICE@example.com');
  assert.equal(a, b);
  assert.equal(a, c);
  assert.match(a, /^[0-9a-f]{64}$/);
  const want = createHmac('sha256', Buffer.from(KEY, 'hex')).update('openbfme-email-v1:alice@example.com').digest('hex');
  assert.equal(a, want);
  assert.notEqual(await emailFingerprint(env, 'f6'.repeat(32), normalizeEmail('alice@example.com')), a);
  assert.notEqual(await emailFingerprint(env, KEY, normalizeEmail('alice+x@example.com')), a);
  await assert.rejects(emailFingerprint(env, 'short', normalizeEmail('alice@example.com')));
});

// ---- Turnstile --------------------------------------------------------------------------------------------------------------------------------------------

test('verifyTurnstile posts the form to siteverify and is true only for 200 + success true', async () => {
  const calls = [];
  const env = envWith(async (url, options) => {
    calls.push({ url, options });
    return json({ success: true, action: 'email', hostname: 'play.openbfme.com' });
  });
  assert.equal(await verifyTurnstile(env, 'turnstile-secret', 'token-1', '198.51.100.7'), true);
  assert.equal(calls[0].url, 'https://challenges.cloudflare.com/turnstile/v0/siteverify');
  assert.equal(calls[0].options.method, 'POST');
  assert.equal(calls[0].options.redirect, 'error');
  assert.equal(calls[0].options.headers['content-type'], 'application/x-www-form-urlencoded');
  const form = new URLSearchParams(calls[0].options.body);
  assert.equal(form.get('secret'), 'turnstile-secret');
  assert.equal(form.get('response'), 'token-1');
  assert.equal(form.get('remoteip'), '198.51.100.7');
  assert.equal(await verifyTurnstile(env, 'turnstile-secret', 'token-1', null, { action: 'email', hostname: 'play.openbfme.com' }), true);
  assert.equal(await verifyTurnstile(env, 'turnstile-secret', 'token-1', null, { action: 'login' }), false);
  assert.equal(await verifyTurnstile(env, 'turnstile-secret', 'token-1', null, { hostname: 'evil.example' }), false);
  assert.equal(new URLSearchParams(calls.at(-1).options.body).has('remoteip'), false);
});

test('verifyTurnstile is false for a refused token and refuses a malformed token without a request', async () => {
  let requests = 0;
  const no = envWith(async () => { requests += 1; return json({ success: false, 'error-codes': ['invalid-input-response'] }); });
  assert.equal(await verifyTurnstile(no, 's', 'tok', undefined), false);
  const truthy = envWith(async () => json({ success: 'true' }));
  assert.equal(await verifyTurnstile(truthy, 's', 'tok', undefined), false);
  requests = 0;
  for (const token of ['', 'x'.repeat(2049), 'bad\ntoken', 42, null]) assert.equal(await verifyTurnstile(no, 's', token, undefined), false);
  assert.equal(requests, 0);
  assert.equal(await verifyTurnstile(no, 's', 'x'.repeat(2048), undefined), false);
  assert.equal(requests, 1);
  await assert.rejects(verifyTurnstile(no, '', 'tok', undefined), /TURNSTILE_SECRET/);
});

test('verifyTurnstile throws EmailProviderFailure on malformed answers, HTTP errors, network errors, redirects and oversize bodies', async () => {
  const cases = [
    async () => new Response('not json', { status: 200 }),
    async () => json([true]),
    async () => json({ success: true }, 500),
    async () => { throw new TypeError('network down'); },
    async (_url, options) => { assert.equal(options.redirect, 'error'); throw new TypeError('redirect mode is set to error'); },
    async () => new Response('x'.repeat(64 * 1024 + 1), { status: 200 }),
    async () => new Response('{}', { status: 200, headers: { 'content-length': String(1 << 20) } }),
  ];
  for (const fetcher of cases) await assert.rejects(verifyTurnstile(envWith(fetcher), 's', 'tok', undefined), EmailProviderFailure);
});

test('verifyTurnstile times out on a hanging body and cancels its reader', async () => {
  let cancelled = false;
  const env = envWith(async () => new Response(new ReadableStream({
    start(controller) { controller.enqueue(new TextEncoder().encode('{"success":')); },
    cancel() { cancelled = true; },
  }), { status: 200 }));
  await assert.rejects(verifyTurnstile(env, 's', 'tok', undefined), EmailProviderFailure);
  assert.equal(cancelled, true);
});

// ---- the mail function ------------------------------------------------------------------------------------------------------------------------------------

test('signMailRequest is hex HMAC-SHA256 over "<time>\\n<body>"', async () => {
  const env = nodeEnv({});
  const sig = await signMailRequest(env, MAILER.key, 1760000000, '{"v":1}');
  assert.equal(sig, createHmac('sha256', Buffer.from(MAILER.key, 'hex')).update('1760000000\n{"v":1}').digest('hex'));
  await assert.rejects(signMailRequest(env, MAILER.key, 1.5, '{}'));
  await assert.rejects(signMailRequest(env, 'abc', 1, '{}'));
});

test('sendMail posts the signed JSON message and is true only for 200 {ok: true}', async () => {
  const now = 1_760_000_123_456;
  const message = magicLinkMessage({ link: `https://play.openbfme.com/email/verify/${TOKEN}`, minutes: 15 });
  let seen;
  const env = envWith(async (url, options) => { seen = { url, options }; return json({ ok: true }); }, now);
  assert.equal(await sendMail(env, MAILER, { to: ADDRESS, ...message }), true);
  assert.equal(seen.url, MAILER.url);
  assert.equal(seen.options.method, 'POST');
  assert.equal(seen.options.redirect, 'error');
  assert.equal(seen.options.headers['content-type'], 'application/json');
  assert.equal(seen.options.headers['x-openbfme-time'], '1760000123');
  const want = createHmac('sha256', Buffer.from(MAILER.key, 'hex')).update(`1760000123\n${seen.options.body}`).digest('hex');
  assert.equal(seen.options.headers['x-openbfme-sig'], want);
  assert.deepEqual(JSON.parse(seen.options.body), { v: 1, to: ADDRESS, ...message });

  const answers = [json({ ok: false, error: 'bad_signature' }, 401), json({ ok: 'yes' }), json({ ok: true }, 202), json({ ok: false }, 200)];
  for (const answer of answers) assert.equal(await sendMail(envWith(async () => answer.clone()), MAILER, { to: ADDRESS, ...message }), false);
});

test('sendMail failures never carry the address', async () => {
  const message = magicLinkMessage({ link: `https://play.openbfme.com/email/verify/${TOKEN}` });
  const leaks = (error) => {
    const text = `${error?.name} ${error?.message} ${error?.stack} ${JSON.stringify(error)} ${String(error?.cause ?? '')}`;
    assert.equal(text.toLowerCase().includes('private.person'), false, text);
    return true;
  };
  const failing = [
    async () => { throw new Error(`could not reach mailer for ${ADDRESS}`); },
    async () => new Response(`echo ${ADDRESS}`, { status: 500 }),
    async () => new Response('x'.repeat(70 * 1024), { status: 200 }),
  ];
  for (const fetcher of failing) {
    await assert.rejects(sendMail(envWith(fetcher), MAILER, { to: ADDRESS, ...message }), (error) => error instanceof EmailProviderFailure && leaks(error));
  }
  const ok = envWith(async () => json({ ok: true }));
  const malformed = [
    { to: `${ADDRESS}\r\nBcc: x@example.com`, ...message },
    { to: `${'p'.repeat(250)}${ADDRESS}`, ...message },
    { to: ADDRESS, ...message, subject: 'two\nlines' },
    { to: ADDRESS, ...message, subject: 's'.repeat(201) },
    { to: ADDRESS, ...message, text: 't'.repeat(32 * 1024 + 1) },
    { to: ADDRESS, ...message, html: '' },
  ];
  for (const body of malformed) await assert.rejects(sendMail(ok, MAILER, body), leaks);
  await assert.rejects(sendMail(ok, { ...MAILER, url: 'http://mailer.example/' }, { to: ADDRESS, ...message }), /https/);
  await assert.rejects(sendMail(ok, { ...MAILER, key: 'short' }, { to: ADDRESS, ...message }), /MAILER_KEY/);
});

test('sendMail times out when the mail function never answers', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  let aborted = false;
  const env = envWith((_url, options) => new Promise(() => { options.signal.addEventListener('abort', () => { aborted = true; }); }));
  const message = magicLinkMessage({ link: `https://play.openbfme.com/email/verify/${TOKEN}` });
  const pending = sendMail(env, MAILER, { to: ADDRESS, ...message });
  for (let i = 0; i < 10; i++) await Promise.resolve();
  t.mock.timers.tick(5000);
  await assert.rejects(pending, EmailProviderFailure);
  assert.equal(aborted, true);
});

test('the magic-link message says what it does, escapes the link and has no images or tracking', () => {
  const link = `https://play.openbfme.com/email/verify/${TOKEN}`;
  const message = magicLinkMessage({ link, minutes: 15 });
  assert.equal(message.subject, 'Confirm your e-mail for OpenBFME');
  assert.ok(message.text.includes(`Click to confirm this e-mail address for your OpenBFME online player: ${link}`));
  assert.ok(message.text.includes("It works once, for 15 minutes. If you didn't ask for this, ignore this message: nothing changes."));
  assert.ok(message.html.includes(`<a href="${link}">${link}</a>`));
  assert.equal(/<img|<script|src=|style=["'][^"']*url\(/i.test(message.html), false);
  const odd = magicLinkMessage({ link: 'https://x.example/?a=1&b="<i>"' }).html;
  assert.ok(odd.includes('a=1&amp;b=&quot;&lt;i&gt;&quot;'));
  assert.equal(odd.includes('<i>'), false);
  assert.throws(() => magicLinkMessage({ link: 'javascript:alert(1)' }));
  assert.throws(() => magicLinkMessage({ link: 'https://x.example/ space' }));
});

// ---- pages ------------------------------------------------------------------------------------------------------------------------------------------------

test('page headers carry the CSP and the privacy headers', () => {
  const request = pageHeaders('request', NONCE);
  const csp = request['content-security-policy'];
  for (const part of [
    "default-src 'none'", `script-src 'nonce-${NONCE}' https://challenges.cloudflare.com`, 'frame-src https://challenges.cloudflare.com',
    "connect-src 'self'", "style-src 'unsafe-inline'", "form-action 'self'", "base-uri 'none'", "frame-ancestors 'none'",
  ]) assert.ok(csp.includes(part), part);
  assert.equal(request['referrer-policy'], 'no-referrer');
  assert.equal(request['cache-control'], 'no-store');
  assert.equal(request['x-content-type-options'], 'nosniff');
  const verify = pageHeaders('verify', NONCE)['content-security-policy'];
  assert.ok(verify.includes(`script-src 'nonce-${NONCE}';`));
  assert.ok(verify.includes("frame-src 'none'"));
  assert.equal(verify.includes('challenges.cloudflare.com'), false);
  assert.ok(pageHeaders('error')['content-security-policy'].includes("script-src 'none'"));
  assert.throws(() => pageHeaders('request', "x' 'unsafe-inline"));
  assert.throws(() => pageHeaders('other', NONCE));
});

test('the request page has the Turnstile widget, the site key, the nonce and the privacy link', () => {
  const html = emailRequestPage({ ticket: TICKET, siteKey: '1x00000000000000000000AA', nonce: NONCE, privacyUrl: 'https://openbfme.com/privacy-signin' });
  assert.ok(html.includes('data-sitekey="1x00000000000000000000AA"'));
  assert.ok(html.includes('data-action="email"'));
  assert.ok(html.includes(`data-ticket="${TICKET}"`));
  assert.ok(html.includes('<script src="https://challenges.cloudflare.com/turnstile/v0/api.js"'));
  assert.ok(html.includes(`<script nonce="${NONCE}">`));
  assert.ok(html.includes("'/v1/auth/email/request'"));
  assert.ok(html.includes('href="https://openbfme.com/privacy-signin"'));
  assert.ok(html.includes('What we keep'));
  assert.ok(html.includes('Check your inbox'));
  // Only Turnstile is loaded from outside.
  const sources = [...html.matchAll(/(?:src|href)="([^"]+)"/g)].map((m) => m[1]);
  assert.deepEqual(sources, ['https://challenges.cloudflare.com/turnstile/v0/api.js', 'https://openbfme.com/privacy-signin']);
  // Every script tag carries the nonce.
  assert.equal((html.match(/<script\b/g) ?? []).length, (html.match(/<script\b[^>]*nonce="/g) ?? []).length);
});

test('a bad ticket or token renders the error page, and inputs are escaped', () => {
  const evil = '<script>alert(1)</script>';
  for (const html of [
    emailRequestPage({ ticket: evil, siteKey: 'k', nonce: NONCE, privacyUrl: 'https://openbfme.com/p' }),
    emailRequestPage({ ticket: TICKET.toUpperCase(), siteKey: 'k', nonce: NONCE, privacyUrl: 'https://openbfme.com/p' }),
    emailRequestPage({ ticket: `${TICKET}0`, siteKey: 'k', nonce: NONCE, privacyUrl: 'https://openbfme.com/p' }),
    emailVerifyPage({ token: evil, nonce: NONCE }),
    emailVerifyPage({ token: `${TOKEN}=`, nonce: NONCE }),
    emailVerifyPage({ token: undefined, nonce: NONCE }),
  ]) {
    assert.equal(html.includes('<script'), false);
    assert.equal(html.includes('alert(1)'), false);
    assert.ok(html.includes('not valid'));
  }
  const error = emailErrorPage(evil);
  assert.ok(error.includes('&lt;script&gt;alert(1)&lt;/script&gt;'));
  assert.equal(error.includes('<script'), false);
  assert.throws(() => emailRequestPage({ ticket: TICKET, siteKey: '"><script>', nonce: NONCE, privacyUrl: 'https://openbfme.com/p' }));
  assert.throws(() => emailRequestPage({ ticket: TICKET, siteKey: 'k', nonce: NONCE, privacyUrl: 'javascript:alert(1)' }));
  assert.throws(() => emailRequestPage({ ticket: TICKET, siteKey: 'k', nonce: '"><b>', privacyUrl: 'https://openbfme.com/p' }));
  assert.throws(() => emailVerifyPage({ token: TOKEN, nonce: 'short' }));
});

test('the verify page posts only when Confirm is pressed', () => {
  const html = emailVerifyPage({ token: TOKEN, nonce: NONCE });
  assert.ok(html.includes(`data-token="${TOKEN}"`));
  assert.ok(html.includes('<button id="confirm" type="button"'));
  assert.ok(html.includes("'/v1/auth/email/verify'"));
  assert.ok(html.includes('Confirmed. You can close this page and go back to the launcher.'));
  assert.equal(html.includes('<form'), false);
  assert.equal(html.includes('challenges.cloudflare.com'), false);
  assert.equal(/DOMContentLoaded|addEventListener\(['"]load|onload|\.submit\(|\.click\(\)|http-equiv|autofocus/i.test(html), false);
  // The one network call sits inside the click handler.
  const script = html.slice(html.indexOf('<script'), html.indexOf('</script>'));
  const handler = script.indexOf("button.addEventListener('click'");
  assert.ok(handler > 0);
  assert.ok(script.indexOf("post('/v1/auth/email/verify'") > handler);
  assert.equal((script.match(/fetch\(/g) ?? []).length, 1);
});
