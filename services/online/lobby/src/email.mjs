// OpenBFME online lobby: e-mail verification helpers (L1). GPL-3.0.
//
// ONLINEMENU-1 (owner identity decisions 2026-10-20, docs/IDENTITY.md "E-mail" and "Mail function"). This module holds no state: it normalizes an
// address, checks it against the disposable-domain list, makes its fingerprint (the only thing the lobby keeps), checks a Turnstile token and asks
// the mail function (services/online/mailer) to send one message. The network is reached only through env.oauthFetch (globalThis.fetch when unset),
// the same way as oauth.mjs: redirects are errors, answers are capped at 64 KiB and a 5 s timeout covers the body too.
//
// Privacy: an address is used in memory for one send. It is never logged and never put in an error message; errors carry fixed text only.

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const MAX_PROVIDER_BYTES = 64 * 1024;
const PROVIDER_TIMEOUT_MS = 5000;
const MAX_INPUT = 1024;
const MAX_ADDRESS = 254;
const MAX_TOKEN = 2048;
const MAX_SUBJECT = 200;
const MAX_PART_BYTES = 32 * 1024;
const FINGERPRINT_LABEL = 'openbfme-email-v1:';
const TURNSTILE_URL = 'https://challenges.cloudflare.com/turnstile/v0/siteverify';
// RFC 5322 dot-atom text: letters, digits and these marks. No quoted local parts, no comments, no spaces.
const LOCAL = /^[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+)*$/;
const LABEL = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
const TLD = /^[a-z]{2,63}$/;
const ASCII = /^[\x20-\x7e]+$/;
const IP = /^[0-9A-Fa-f:.]{2,45}$/;
const HEX_KEY = /^(?:[0-9a-fA-F]{2}){32,}$/;

// A refused address. `code` is what the lobby answers; the message never contains the address.
export class EmailError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'EmailError';
    this.code = code;
  }
}

// Turnstile or the mail function could not be asked (network, timeout, size, format). Fixed message only.
export class EmailProviderFailure extends Error {
  constructor() {
    super('the e-mail provider is unavailable');
    this.name = 'EmailProviderFailure';
  }
}

function bytesToHex(bytes) {
  return Array.from(bytes, (x) => x.toString(16).padStart(2, '0')).join('');
}

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(2 * i, 2), 16);
  return out;
}

// A key is bytes (at least 16) or a hex string of at least 32 bytes (how Worker secrets are written).
function keyBytes(key, name) {
  if (key instanceof Uint8Array && key.length >= 16) return key;
  if (typeof key === 'string' && HEX_KEY.test(key)) return hexToBytes(key);
  throw new Error(`${name} must be at least 16 key bytes or 32 hex-encoded bytes`);
}

// ---- addresses --------------------------------------------------------------------------------------------------------------------------------------------

// Trim, split at the one '@', check the local part (case kept as typed) and the domain (lowercased, ASCII only: a non-ASCII domain is refused,
// not converted). Owner: keep it simple, so there is no plus-address or dot folding: a+b@x and ab@x are different addresses.
export function normalizeEmail(text) {
  if (typeof text !== 'string' || text.length > MAX_INPUT) throw new EmailError('email_invalid', 'enter an e-mail address');
  const value = text.trim();
  if (value.length === 0) throw new EmailError('email_invalid', 'enter an e-mail address');
  const at = value.indexOf('@');
  if (at < 0 || value.indexOf('@', at + 1) >= 0) throw new EmailError('email_invalid', 'an e-mail address has exactly one @');
  const local = value.slice(0, at);
  const domain = value.slice(at + 1).toLowerCase();
  if (local.length < 1 || local.length > 64 || !LOCAL.test(local)) {
    throw new EmailError('email_invalid', 'the part before the @ is not a valid e-mail name');
  }
  if (!ASCII.test(domain)) throw new EmailError('email_invalid', 'the domain must be written in plain ASCII (punycode for international names)');
  if (domain.length < 1 || domain.length > 253) throw new EmailError('email_invalid', 'the domain is not valid');
  const labels = domain.split('.');
  if (labels.length < 2 || !labels.every((label) => LABEL.test(label)) || !TLD.test(labels[labels.length - 1])) {
    throw new EmailError('email_invalid', 'the domain is not valid');
  }
  const address = `${local}@${domain}`;
  // SMTP's path limit; the mail function refuses longer addresses too.
  if (address.length > MAX_ADDRESS) throw new EmailError('email_invalid', 'the e-mail address is too long');
  return { address, domain };
}

// The bundled list (data/disposable-domains.txt) or EMAIL_BLOCKLIST: one domain per line, '#' comments and blank lines ignored.
export function loadDisposableDomains(text) {
  const set = new Set();
  for (const raw of String(text).split(/\r?\n/)) {
    const hash = raw.indexOf('#');
    const line = (hash >= 0 ? raw.slice(0, hash) : raw).trim().toLowerCase();
    if (line) set.add(line);
  }
  return set;
}

// True when the domain or any parent of it is listed (sub.example.com is caught by example.com).
export function isDisposable(domain, set) {
  if (typeof domain !== 'string' || !(set instanceof Set)) throw new Error('isDisposable needs a domain and a set');
  const labels = domain.toLowerCase().split('.');
  for (let i = 0; i < labels.length; i++) {
    if (set.has(labels.slice(i).join('.'))) return true;
  }
  return false;
}

// The e-mail fingerprint: hex HMAC-SHA256 over the label and the lowercased address. The local part's case does not change it.
export async function emailFingerprint(env, key, normalized) {
  const address = typeof normalized === 'string' ? normalizeEmail(normalized).address : normalized?.address;
  if (typeof address !== 'string' || address.length === 0) throw new Error('emailFingerprint needs a normalized address');
  const mac = await env.hmac(keyBytes(key, 'the e-mail fingerprint key'), encoder.encode(FINGERPRINT_LABEL + address.toLowerCase()));
  return bytesToHex(mac);
}

// ---- network (the same bounds as oauth.mjs) --------------------------------------------------------------------------------------------------------------

function abortSignal() {
  const controller = new AbortController();
  let cancel = () => {};
  const abort = () => controller.abort();
  let timeoutResolve;
  const timeout = new Promise((_, reject) => { timeoutResolve = () => reject(new EmailProviderFailure()); });
  const timer = setTimeout(() => {
    abort();
    void cancel();
    timeoutResolve();
  }, PROVIDER_TIMEOUT_MS);
  return {
    signal: controller.signal,
    timeout,
    setCancel(fn) { cancel = fn; if (controller.signal.aborted) void cancel(); },
    abort() { abort(); void cancel(); },
    close() { clearTimeout(timer); },
  };
}

async function bytes(response, request) {
  const contentLength = Number(response?.headers?.get?.('content-length') ?? 0);
  if (Number.isFinite(contentLength) && contentLength > MAX_PROVIDER_BYTES) throw new EmailProviderFailure();
  if (response?.body?.getReader) {
    const reader = response.body.getReader();
    request.setCancel(() => reader.cancel().catch(() => {}));
    const chunks = [];
    let total = 0;
    try {
      while (true) {
        const part = await reader.read();
        if (part.done) break;
        total += part.value.byteLength;
        if (total > MAX_PROVIDER_BYTES) {
          await reader.cancel().catch(() => {});
          throw new EmailProviderFailure();
        }
        chunks.push(part.value);
      }
    } finally {
      request.setCancel(() => {});
    }
    const out = new Uint8Array(total);
    let at = 0;
    for (const chunk of chunks) {
      out.set(chunk, at);
      at += chunk.byteLength;
    }
    return out;
  }
  request.setCancel(() => response?.body?.cancel?.().catch(() => {}));
  try {
    const value = new Uint8Array(await response.arrayBuffer());
    if (value.length > MAX_PROVIDER_BYTES) throw new EmailProviderFailure();
    return value;
  } finally {
    request.setCancel(() => {});
  }
}

// One request: { status, value } with value a parsed JSON object. Any trouble (network, redirect, timeout, size, not a JSON object) throws
// EmailProviderFailure with its fixed message, so nothing from the request (the address) can leak through an error.
async function requestJson(env, url, options) {
  const fetcher = env.oauthFetch ?? globalThis.fetch;
  if (typeof fetcher !== 'function') throw new EmailProviderFailure();
  const request = abortSignal();
  try {
    const operation = (async () => {
      const response = await fetcher(url, { ...options, redirect: 'error', signal: request.signal });
      if (!response || !Number.isInteger(response.status)) throw new EmailProviderFailure();
      const raw = await bytes(response, request);
      let value;
      try {
        value = JSON.parse(decoder.decode(raw));
      } catch {
        throw new EmailProviderFailure();
      }
      if (!value || typeof value !== 'object' || Array.isArray(value)) throw new EmailProviderFailure();
      return { status: response.status, value };
    })();
    return await Promise.race([operation, request.timeout]);
  } catch {
    request.abort();
    throw new EmailProviderFailure();
  } finally {
    request.close();
  }
}

// ---- Turnstile ---------------------------------------------------------------------------------------------------------------------------------------------

// Ask Cloudflare's siteverify about a widget token. True only for a 200 answer with success === true (and, when given, the expected action and
// hostname). A malformed token is refused here without a request. Network or format trouble throws EmailProviderFailure.
export async function verifyTurnstile(env, secret, token, remoteIp, { action, hostname } = {}) {
  if (typeof secret !== 'string' || secret.length < 1 || secret.length > 512 || !ASCII.test(secret)) {
    throw new Error('TURNSTILE_SECRET must be printable ASCII');
  }
  if (typeof token !== 'string' || token.length < 1 || token.length > MAX_TOKEN || !ASCII.test(token)) return false;
  const form = new URLSearchParams({ secret, response: token });
  if (typeof remoteIp === 'string' && IP.test(remoteIp)) form.set('remoteip', remoteIp);
  const { status, value } = await requestJson(env, TURNSTILE_URL, {
    method: 'POST',
    headers: { accept: 'application/json', 'content-type': 'application/x-www-form-urlencoded' },
    body: form.toString(),
  });
  if (status !== 200) throw new EmailProviderFailure();
  if (value.success !== true) return false;
  if (action !== undefined && value.action !== action) return false;
  if (hostname !== undefined && value.hostname !== hostname) return false;
  return true;
}

// ---- the mail function -------------------------------------------------------------------------------------------------------------------------------------

// The mail function's signature: hex HMAC-SHA256 over "<unix seconds>\n<body>" (services/online/mailer/handler.py checks the same).
export async function signMailRequest(env, key, timestampSeconds, bodyText) {
  if (!Number.isSafeInteger(timestampSeconds) || timestampSeconds < 0) throw new Error('the mail timestamp must be whole seconds');
  if (typeof bodyText !== 'string') throw new Error('the mail body must be text');
  const mac = await env.hmac(keyBytes(key, 'MAILER_KEY'), encoder.encode(`${timestampSeconds}\n${bodyText}`));
  return bytesToHex(mac);
}

function oneLine(value, maximum) {
  return typeof value === 'string' && value.length >= 1 && value.length <= maximum && !/[\x00-\x1f\x7f]/.test(value);
}

function part(value) {
  return typeof value === 'string' && value.length >= 1 && encoder.encode(value).length <= MAX_PART_BYTES;
}

// Send one message through the mail function. True only for a 200 answer {ok: true}; any other answer is false, transport trouble throws
// EmailProviderFailure. Errors never contain the address.
export async function sendMail(env, { url, key }, { to, subject, text, html }) {
  if (typeof url !== 'string' || !url.startsWith('https://') || url.length > 2048 || !ASCII.test(url)) throw new Error('MAILER_URL must be an https URL');
  const secretKey = keyBytes(key, 'MAILER_KEY');
  if (!oneLine(to, MAX_ADDRESS) || !oneLine(subject, MAX_SUBJECT) || !part(text) || !part(html)) throw new Error('the mail request is malformed');
  const body = JSON.stringify({ v: 1, to, subject, text, html });
  const time = Math.floor(env.now() / 1000);
  const signature = await signMailRequest(env, secretKey, time, body);
  const { status, value } = await requestJson(env, url, {
    method: 'POST',
    headers: {
      accept: 'application/json',
      'content-type': 'application/json',
      'x-openbfme-time': String(time),
      'x-openbfme-sig': signature,
    },
    body,
  });
  return status === 200 && value.ok === true;
}

// ---- the message -------------------------------------------------------------------------------------------------------------------------------------------

export function escapeHtml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

// The magic-link e-mail: plain text and simple HTML, no images, no tracking.
export function magicLinkMessage({ link, minutes = 15 }) {
  if (typeof link !== 'string' || !link.startsWith('https://') || link.length > 2048 || !/^[\x21-\x7e]+$/.test(link)) {
    throw new Error('the magic link must be an https URL');
  }
  if (!Number.isSafeInteger(minutes) || minutes < 1 || minutes > 1440) throw new Error('the link lifetime must be whole minutes');
  const subject = 'Confirm your e-mail for OpenBFME';
  const text = [
    `Click to confirm this e-mail address for your OpenBFME online player: ${link}`,
    '',
    `It works once, for ${minutes} minutes. If you didn't ask for this, ignore this message: nothing changes.`,
    '',
  ].join('\n');
  const safe = escapeHtml(link);
  const html = [
    '<!doctype html>',
    '<html lang="en"><head><meta charset="utf-8"><title>' + escapeHtml(subject) + '</title></head>',
    '<body style="font-family:Georgia,serif;color:#222;background:#fff;">',
    `<p>Click to confirm this e-mail address for your OpenBFME online player: <a href="${safe}">${safe}</a></p>`,
    `<p>It works once, for ${escapeHtml(minutes)} minutes. If you didn${escapeHtml("'")}t ask for this, ignore this message: nothing changes.</p>`,
    '</body></html>',
    '',
  ].join('\n');
  return { subject, text, html };
}
