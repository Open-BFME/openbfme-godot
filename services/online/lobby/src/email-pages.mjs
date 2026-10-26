// OpenBFME online lobby: the e-mail verification pages (L1). GPL-3.0.
//
// ONLINEMENU-1 (owner identity decisions 2026-10-20, docs/IDENTITY.md "E-mail"). The lobby serves these at https://play.openbfme.com/email/<ticket>
// (asks for the address, with Cloudflare Turnstile) and /email/verify/<token> (one Confirm button). Each page is one self-contained HTML string:
// inline CSS, one inline script carrying the CSP nonce, and no outside resource except Turnstile's script on the request page.
//
// Rendering a page never changes anything: the verify page only POSTs when the player presses Confirm, so a mail scanner that opens the link
// consumes nothing. Every value put in the HTML is escaped, and the ticket and token are format-checked first (a bad one gets the error page).

import { escapeHtml } from './email.mjs';

const TURNSTILE_ORIGIN = 'https://challenges.cloudflare.com';
const TURNSTILE_SCRIPT = `${TURNSTILE_ORIGIN}/turnstile/v0/api.js`;
const TICKET = /^[0-9a-f]{48}$/;
const TOKEN = /^[A-Za-z0-9_-]{43}$/;
const NONCE = /^[A-Za-z0-9+/_-]{16,128}={0,2}$/;
const SITE_KEY = /^[A-Za-z0-9_-]{1,128}$/;
const PAGE_KINDS = Object.freeze(['request', 'verify', 'error']);

function checkNonce(nonce) {
  if (typeof nonce !== 'string' || !NONCE.test(nonce)) throw new Error('the page nonce must be 16+ base64 characters');
  return nonce;
}

// The security headers for a page. Only the request page may load Turnstile (its script and its frame); the error page runs no script at all.
// Scripts run only by nonce (no 'self': the lobby serves no script files, so none is allowed from it).
export function pageHeaders(kind, nonce) {
  if (!PAGE_KINDS.includes(kind)) throw new Error('unknown e-mail page kind');
  const script = kind === 'error' ? "'none'" : `'nonce-${checkNonce(nonce)}'${kind === 'request' ? ` ${TURNSTILE_ORIGIN}` : ''}`;
  const frame = kind === 'request' ? TURNSTILE_ORIGIN : "'none'";
  const csp = [
    "default-src 'none'",
    `script-src ${script}`,
    `frame-src ${frame}`,
    "connect-src 'self'",
    "style-src 'unsafe-inline'",
    "form-action 'self'",
    "base-uri 'none'",
    "frame-ancestors 'none'",
  ].join('; ');
  return {
    'content-type': 'text/html; charset=utf-8',
    'content-security-policy': csp,
    'referrer-policy': 'no-referrer',
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
  };
}

const STYLE = `
  :root { color-scheme: dark; }
  body { margin: 0; min-height: 100vh; display: flex; align-items: center; justify-content: center;
         background: radial-gradient(circle at 50% 20%, #2b2417 0%, #12100c 70%); color: #e8dcc0;
         font-family: Georgia, "Times New Roman", serif; }
  main { width: min(92vw, 30rem); padding: 2rem 2.2rem; background: rgba(20, 17, 12, 0.92);
         border: 1px solid #8a6d2f; border-radius: 6px; box-shadow: 0 0 2.5rem rgba(0, 0, 0, 0.7); }
  h1 { margin: 0 0 1rem; font-size: 1.5rem; color: #e3c27a; letter-spacing: 0.04em; }
  p { line-height: 1.5; }
  label { display: block; margin: 1rem 0 0.4rem; }
  input[type=email] { box-sizing: border-box; width: 100%; padding: 0.6rem; font-size: 1rem; color: #f3ead6;
         background: #0d0b08; border: 1px solid #6b5527; border-radius: 4px; }
  button { margin-top: 1.2rem; padding: 0.6rem 1.6rem; font-size: 1rem; font-family: inherit; color: #1a1408;
         background: linear-gradient(#e8c97f, #b8913f); border: 1px solid #5c4615; border-radius: 4px; cursor: pointer; }
  button:disabled { opacity: 0.5; cursor: default; }
  .cf-turnstile { margin-top: 1rem; min-height: 65px; }
  #result { min-height: 1.5em; margin-top: 1rem; }
  #result.bad { color: #f08a6c; }
  #result.good { color: #a8d88a; }
  a { color: #e3c27a; }
  small { display: block; margin-top: 1.5rem; color: #a99a7a; }
`;

function page(title, body, head = '') {
  return [
    '<!doctype html>',
    '<html lang="en">',
    '<head>',
    '<meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width, initial-scale=1">',
    '<meta name="referrer" content="no-referrer">',
    '<meta name="robots" content="noindex, nofollow">',
    `<title>${escapeHtml(title)}</title>`,
    `<style>${STYLE}</style>`,
    head,
    '</head>',
    '<body>',
    '<main>',
    body,
    '</main>',
    '</body>',
    '</html>',
    '',
  ].join('\n');
}

// A page that only says what went wrong (no script).
export function emailErrorPage(message) {
  return page('OpenBFME e-mail', [
    '<h1>OpenBFME e-mail</h1>',
    `<p>${escapeHtml(message)}</p>`,
    '<p>Go back to the launcher and start again.</p>',
  ].join('\n'));
}

// The shared part of both scripts: post JSON to the lobby and show its answer as text (never as HTML).
const POST_SCRIPT = `
  const result = document.getElementById('result');
  function show(text, good) {
    result.textContent = text;
    result.className = good ? 'good' : 'bad';
  }
  async function post(path, body) {
    let response;
    try {
      response = await fetch(path, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(body),
        credentials: 'omit',
        cache: 'no-store',
        redirect: 'error',
      });
    } catch {
      return { ok: false, message: 'The lobby could not be reached. Check your connection and try again.' };
    }
    let answer = null;
    try { answer = await response.json(); } catch {}
    if (response.ok) return { ok: true, answer };
    const message = answer && typeof answer.message === 'string' && answer.message
      ? answer.message
      : 'Something went wrong (' + response.status + '). Try again in a minute.';
    return { ok: false, message };
  }
`;

// The address request page: the address, the Turnstile widget and a link to what we keep.
export function emailRequestPage({ ticket, siteKey, nonce, privacyUrl }) {
  if (typeof ticket !== 'string' || !TICKET.test(ticket)) return emailErrorPage('This e-mail link is not valid.');
  checkNonce(nonce);
  if (typeof siteKey !== 'string' || !SITE_KEY.test(siteKey)) throw new Error('TURNSTILE_SITE_KEY is not a site key');
  if (typeof privacyUrl !== 'string' || !privacyUrl.startsWith('https://') || !/^[\x21-\x7e]{9,2048}$/.test(privacyUrl)) {
    throw new Error('the privacy page must be an https URL');
  }
  const body = [
    '<h1>Confirm an e-mail address</h1>',
    '<p>Enter the e-mail address for your OpenBFME online player. We send one link to it; the address itself is not kept.</p>',
    `<form id="email-form" data-ticket="${escapeHtml(ticket)}" novalidate>`,
    '<label for="email">E-mail address</label>',
    '<input id="email" name="email" type="email" autocomplete="email" maxlength="254" required>',
    `<div class="cf-turnstile" data-sitekey="${escapeHtml(siteKey)}" data-action="email" data-theme="dark"></div>`,
    '<button id="send" type="submit">Send the link</button>',
    '</form>',
    '<p id="result" role="status" aria-live="polite"></p>',
    '<noscript><p>This page needs JavaScript for its anti-bot check.</p></noscript>',
    `<small><a href="${escapeHtml(privacyUrl)}" rel="noreferrer noopener" target="_blank">What we keep</a></small>`,
    `<script nonce="${escapeHtml(nonce)}">${POST_SCRIPT}
  const form = document.getElementById('email-form');
  const button = document.getElementById('send');
  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    const email = document.getElementById('email').value.trim();
    const field = form.querySelector('[name="cf-turnstile-response"]');
    const turnstile = field ? field.value : '';
    if (!email) { show('Enter your e-mail address.', false); return; }
    if (!turnstile) { show('Wait for the anti-bot check to finish, then try again.', false); return; }
    button.disabled = true;
    show('Sending...', true);
    const outcome = await post('/v1/auth/email/request', { v: 1, ticket: form.dataset.ticket, email, turnstile });
    if (outcome.ok) {
      show('Check your inbox: we sent a link to confirm the address. It works once, for 15 minutes.', true);
      return;
    }
    show(outcome.message, false);
    if (window.turnstile) window.turnstile.reset();
    button.disabled = false;
  });
</script>`,
  ].join('\n');
  const head = `<script src="${TURNSTILE_SCRIPT}" nonce="${escapeHtml(nonce)}" async defer></script>`;
  return page('Confirm your e-mail for OpenBFME', body, head);
}

// The verify page: one Confirm button. Nothing runs until it is pressed.
export function emailVerifyPage({ token, nonce }) {
  if (typeof token !== 'string' || !TOKEN.test(token)) return emailErrorPage('This confirmation link is not valid.');
  checkNonce(nonce);
  const body = [
    '<h1>Confirm your e-mail</h1>',
    '<p>Press Confirm to use this e-mail address for your OpenBFME online player.</p>',
    `<button id="confirm" type="button" data-token="${escapeHtml(token)}">Confirm</button>`,
    '<p id="result" role="status" aria-live="polite"></p>',
    '<noscript><p>This page needs JavaScript to confirm.</p></noscript>',
    `<script nonce="${escapeHtml(nonce)}">${POST_SCRIPT}
  const button = document.getElementById('confirm');
  button.addEventListener('click', async () => {
    button.disabled = true;
    show('Confirming...', true);
    const outcome = await post('/v1/auth/email/verify', { v: 1, token: button.dataset.token });
    if (outcome.ok) {
      show('Confirmed. You can close this page and go back to the launcher.', true);
      return;
    }
    show(outcome.message, false);
    button.disabled = false;
  });
</script>`,
  ].join('\n');
  return page('Confirm your e-mail for OpenBFME', body);
}
