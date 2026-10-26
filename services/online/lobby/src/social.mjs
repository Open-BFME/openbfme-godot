import { SocialStorageError } from './social-store.mjs';
import { repairStoredText } from './stored-text.mjs';
import {
  authorizationUrl,
  exchangeProvider,
  OAUTH_PROVIDERS,
  OAuthProviderFailure,
  pkceChallenge,
  providerConfig,
} from './oauth.mjs';
import { EmailError, EmailProviderFailure, isDisposable, magicLinkMessage, normalizeEmail, sendMail, verifyTurnstile } from './email.mjs';
import { DISPOSABLE_DOMAINS } from './disposable-domains.mjs';

const VERSION = 1;
const PID = /^[0-9a-f]{16}$/;
const INSTALL = /^[0-9a-f]{32}$/;
const DEVICE = /^[0-9a-f]{64}$/;
const CLIENT_ID = /^[0-9a-f]{32}$/;
const REQUEST_ID = /^[0-9a-f]{16}$/;
const INVITE_ID = /^[0-9a-f]{16}$/;
const GAME_ID = /^[0-9a-f]{16}$/;
const PROFILE = /^[0-9a-f]{32}$/;
const CROCKFORD = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const JOIN_GRANT = Symbol('socialJoinGrant');
const MAX_CHALLENGE_HANDOFFS = 1024;
const MAX_ACTIVE_PLAYERS = 10000;
const MAX_INBOX_CACHES = 10000;
const MAX_INBOX_CACHE_BYTES = 16 * 1024 * 1024;
const MAX_POLL_AUTH_CACHES = 10000;
const MAX_POLL_AUTH_CACHE_BYTES = 4 * 1024 * 1024;
const MAX_PENDING_INVITES = 10000;
const MAX_NOTICE_PLAYERS = 10000;
const CACHE_INVALIDATED = Symbol('cacheInvalidated');
const DEFAULT_PRIVACY = Object.freeze({
  invites: 'friends_or_room',
  challenges: 'friends_or_room',
  requests: 'anyone',
  presence: 'friends',
});
const encoder = new TextEncoder();
const OAUTH_HEX = /^[0-9a-f]{64}$/;
const OAUTH_TRANSACTION = /^[0-9a-f]{48}$/;
const OAUTH_STATE = /^[0-9a-f]{64}$/;
const OAUTH_CHALLENGE = /^[A-Za-z0-9_-]{43}$/;
const OAUTH_VERIFIER = /^[A-Za-z0-9._~-]{43,128}$/;
const OAUTH_CODE = /^[\x21-\x7e]{1,2048}$/;
const OAUTH_PENDING_SECONDS = 5 * 60;
const OAUTH_QUOTA = Object.freeze({
  start: { window: 600, global: 10000, ip: 30, install: 10, pid: 20 },
  finish: { window: 600, global: 20000, ip: 30, install: 20, pid: 40 },
  unlink: { window: 86400, global: 10000, ip: 20, install: 10, pid: 20 },
  // ONLINEMENU-1: e-mail sends (the request page): per address (ip), per ticket's install and player, and all of them
  email: { window: 600, global: 200, ip: 10, install: 5, pid: 5 },
});
const OAUTH_MAX_EXPIRY_CLEANUP = 100;
// ONLINEMENU-1 (owner 2026-10-20): the identities a player can link, each of which can also move the player to a new PC: Discord sign-in and a
// confirmed e-mail address (stored as a fingerprint). Optional; everyone plays without them
export const IDENTITY_KINDS = Object.freeze([...OAUTH_PROVIDERS, 'email']);
const MOVED_NOTICE_SECONDS = 90 * 24 * 3600; // ONLINEMENU-1: how long an install's "your player was moved" notice waits for it
const OAUTH_NET_FACTOR = 4; // ONLINEMENU-1: an IPv6 /48's OAuth budget, in /64 budgets
// ONLINEMENU-1 (Sol r2 on PR #8: the Free plan's 100,000 row writes a day are shared with every lobby operation): OAuth's admitted operations per
// UTC day, per global bucket. A restore start or finish writes about 9 rows and cleans them up later, a link about 12: these caps keep OAuth near
// 25,000 row writes a day at worst. The address budgets are kept in memory (no writes at all for a refused or malformed request)
const OAUTH_DAILY = Object.freeze({
  start: { link: 600, restore: 400, all: 600 },
  finish: { link: 600, restore: 400, all: 600 },
  unlink: { all: 200, link: 200, restore: 200 },
  email: { all: 300, link: 300, restore: 300 }, // the SES sandbox sends 200 a day until production access
});
const OAUTH_DAY_SECONDS = 86400;
const OAUTH_ADDRESS_ENTRIES = 100000;

export class SocialError extends Error {
  constructor(status, code, message, details = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

const fail = (status, code, message, details) => { throw new SocialError(status, code, message, details); };
const invalid = (message = 'the social request is invalid') => fail(400, 'bad_field', message);
const byPid = (a, b) => a.pid < b.pid ? -1 : a.pid > b.pid ? 1 : 0;
const sortedPair = (a, b) => a < b ? [a, b] : [b, a];
const pairKey = (a, b) => sortedPair(a, b).join(':');
const time = (lobby) => Math.floor(lobby.env.now() / 1000);
const hex = (bytes) => Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');

function exact(value, fields, optional = []) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) invalid('the body must be an object');
  for (const key of Object.keys(value)) if (!fields.includes(key) && !optional.includes(key)) invalid('the body contains an unknown field');
  for (const key of fields) if (!Object.hasOwn(value, key)) invalid(`missing field ${key}`);
  if (value.v !== VERSION) fail(400, 'bad_version', 'v must be 1');
}

function text(value, what, max, { empty = false } = {}) {
  if (typeof value !== 'string' || (!empty && !value.length) || Array.from(value).length > max ||
      /[\u0000-\u001f\u007f]/u.test(value) || value !== value.trim() || (!empty && !value.trim())) {
    invalid(`${what} must be ${empty ? 'at most' : '1 to'} ${max} characters without surrounding whitespace or controls`);
  }
  if (Array.from(value).some((c) => {
    const cp = c.codePointAt(0);
    return cp >= 0xd800 && cp <= 0xdfff;
  })) invalid(`${what} contains invalid Unicode`);
  return value;
}

function integer(value, what, min, max) {
  if (!Number.isSafeInteger(value) || value < min || value > max) invalid(`${what} must be an integer from ${min} through ${max}`);
  return value;
}

function pid(value, what = 'pid') {
  if (typeof value !== 'string' || !PID.test(value)) invalid(`${what} must be 16 lowercase hexadecimal characters`);
  return value;
}

function codeInput(value) {
  if (typeof value !== 'string') invalid('code must be a friend code');
  const normalized = value.includes('-')
    ? (/^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{2}$/.test(value) ? value.replaceAll('-', '') : '')
    : value;
  if (!/^[0-9A-HJKMNP-TV-Z]{10}$/.test(normalized)) invalid('code must be ten uppercase Crockford characters');
  return normalized;
}

function newPid(randomBytes) {
  return hex(randomBytes(8));
}

function newCode(randomBytes) {
  const bytes = randomBytes(7);
  if (!(bytes instanceof Uint8Array) || bytes.length !== 7) throw new Error('social random source returned an invalid code buffer');
  let value = 0n;
  for (const byte of bytes) value = (value << 8n) | BigInt(byte);
  value &= (1n << 50n) - 1n;
  let out = '';
  for (let i = 0; i < 10; i++) {
    out = CROCKFORD[Number(value & 31n)] + out;
    value >>= 5n;
  }
  return out;
}

function base64Url(bytes) {
  let text = '';
  for (const byte of bytes) text += String.fromCharCode(byte);
  return btoa(text).replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
}

function serial(value) {
  return JSON.stringify(value);
}

function normalizeSocialFailure(error) {
  if (error instanceof SocialError) return error;
  if (error instanceof SocialStorageError) {
    return new SocialError(error.kind === 'full' ? 507 : 503,
      error.kind === 'full' ? 'social_store_full' : 'social_unavailable',
      error.kind === 'full' ? 'social storage is full' : 'social storage is unavailable');
  }
  return new SocialError(503, 'social_unavailable', 'social storage is unavailable');
}

function oauthUnavailable() {
  throw new SocialError(503, 'oauth_provider_unavailable', 'the OAuth provider is unavailable');
}

function oauthProvider(value) {
  if (typeof value !== 'string' || !OAUTH_PROVIDERS.includes(value)) {
    fail(400, 'unknown_provider', 'the OAuth provider is not supported');
  }
  return value;
}

function oauthState(value) {
  if (typeof value !== 'string' || !OAUTH_STATE.test(value)) {
    fail(400, 'oauth_state', 'the OAuth state is invalid');
  }
  return value;
}

function oauthChallenge(value) {
  if (typeof value !== 'string' || !OAUTH_CHALLENGE.test(value)) {
    fail(400, 'oauth_state', 'the OAuth challenge is invalid');
  }
  return value;
}

function oauthTransaction(value) {
  if (typeof value !== 'string' || !OAUTH_TRANSACTION.test(value)) {
    fail(400, 'oauth_expired', 'the OAuth transaction is unavailable');
  }
  return value;
}

function oauthCode(value) {
  if (typeof value !== 'string' || !OAUTH_CODE.test(value) || /\s/u.test(value)) {
    fail(400, 'bad_field', 'the OAuth code is invalid');
  }
  return value;
}

function oauthVerifier(value) {
  if (typeof value !== 'string' || !OAUTH_VERIFIER.test(value)) {
    fail(400, 'oauth_verifier', 'the OAuth verifier is invalid');
  }
  return value;
}

export function validPlayerName(value) {
  return typeof value === 'string' && Array.from(value).length >= 1 && Array.from(value).length <= 24 &&
    !/[\u0000-\u001f\u007f]/u.test(value) && value === value.trim() && value.isWellFormed();
}

function validOAuthClient(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'platform,version' ||
      !['windows', 'linux', 'macos'].includes(value.platform) ||
      typeof value.version !== 'string' || !value.version.length ||
      encoder.encode(value.version).length > 64 || /[\u0000-\u0008\u000b-\u001f\u007f]/.test(value.version)) return false;
  return true;
}

function oauthClient(value) {
  if (!validOAuthClient(value)) invalid('client is invalid');
  return { platform: value.platform, version: value.version };
}

function oauthName(value) {
  if (!validPlayerName(value)) invalid('name must be 1 to 24 Unicode characters without controls or surrounding whitespace');
  return value;
}

function oauthIdentityFields(body) {
  if (typeof body.install !== 'string' || !INSTALL.test(body.install)) invalid('install must be 32 lowercase hex digits');
  if (body.device !== undefined && (typeof body.device !== 'string' || !DEVICE.test(body.device))) {
    invalid('device must be 64 lowercase hex digits');
  }
  const client = body.client === undefined ? undefined : oauthClient(body.client);
  return {
    name: oauthName(body.name),
    install: body.install,
    device: body.device ?? null,
    ...(client === undefined ? {} : { client }),
  };
}

function oauthStorageObject(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new SocialStorageError('unavailable');
  return value;
}

function oauthSafe(value, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) throw new SocialStorageError('unavailable');
  return value;
}

function oauthPlayer(tx, pidValue) {
  const player = validatePlayer(tx.get(`player:${pidValue}`), pidValue);
  if (tx.get(`install:${player.install}`) !== pidValue || tx.get(`code:${player.code}`) !== pidValue) {
    throw new SocialStorageError('unavailable');
  }
  return player;
}

function oauthSessionPlayer(tx, account) {
  const player = oauthPlayer(tx, account.pid);
  if (player.generation !== account.generation || player.install !== account.install) {
    fail(401, 'session_revoked', 'this session has been revoked');
  }
  return player;
}

function oauthExpiryKey(expires, suffix) {
  return `oauth2:expiry:${String(expires).padStart(16, '0')}:${suffix}`;
}

function oauthQuotaKey(operation, scope, identity, bucket) {
  return `oauth2:quota:${operation}:${scope}:${identity}:${bucket}`;
}

function oauthQuotaValue(value) {
  oauthStorageObject(value);
  oauthSafe(value.count, Number.MAX_SAFE_INTEGER);
  oauthSafe(value.expires, Number.MAX_SAFE_INTEGER);
  if (Object.keys(value).sort().join(',') !== 'count,expires') throw new SocialStorageError('unavailable');
  return value;
}

function oauthDeletePending(tx, pending) {
  const pendingKey = `oauth2:pending:${pending.transaction}`;
  if (JSON.stringify(tx.get(pendingKey)) !== JSON.stringify(pending) ||
      tx.get(`oauth2:pending-install:${pending.install}`) !== pending.transaction ||
      tx.get(oauthExpiryKey(pending.expires, `pending:${pending.transaction}`)) !== pendingKey) {
    throw new SocialStorageError('unavailable');
  }
  tx.delete(pendingKey);
  tx.delete(`oauth2:pending-install:${pending.install}`);
  tx.delete(oauthExpiryKey(pending.expires, `pending:${pending.transaction}`));
}

function oauthPending(value, clock) {
  oauthStorageObject(value);
  if (value.v !== 1 || !OAUTH_TRANSACTION.test(value.transaction) ||
      !OAUTH_STATE.test(value.state) || !OAUTH_CHALLENGE.test(value.challenge) ||
      !OAUTH_PROVIDERS.includes(value.provider) || !['link', 'restore', 'confirm'].includes(value.mode) ||
      !Number.isSafeInteger(value.expires) || value.expires < 1 ||
      typeof value.install !== 'string' || !INSTALL.test(value.install) ||
      !(value.device === null || (typeof value.device === 'string' && DEVICE.test(value.device)))) {
    throw new SocialStorageError('unavailable');
  }
  if (value.mode === 'link' || value.mode === 'confirm') {
    const expected = 'accountId,challenge,device,expires,generation,install,mode,pid,provider,revision,state,transaction,v,version';
    if (Object.keys(value).sort().join(',') !== expected ||
        typeof value.accountId !== 'string' || !value.accountId.length || value.accountId.length > 128 ||
        typeof value.pid !== 'string' || !PID.test(value.pid) ||
        !Number.isSafeInteger(value.generation) || value.generation < 1 ||
        !Number.isSafeInteger(value.version) || value.version < 0 || value.version > clock ||
        !Number.isSafeInteger(value.revision) || value.revision < value.version || value.revision > clock) {
      throw new SocialStorageError('unavailable');
    }
  } else {
    const keys = Object.keys(value).sort().join(',');
    const expected = 'challenge,device,expires,install,mode,name,pid,provider,state,transaction,v,watermark';
    const expectedWithClient = 'challenge,client,device,expires,install,mode,name,pid,provider,state,transaction,v,watermark';
    if ((keys !== expected && keys !== expectedWithClient) || value.pid !== null ||
        !Number.isSafeInteger(value.watermark) || value.watermark < 0 || value.watermark > clock ||
        !validPlayerName(value.name) || (Object.hasOwn(value, 'client') && !validOAuthClient(value.client))) {
      throw new SocialStorageError('unavailable');
    }
  }
  return value;
}

function oauthMeta(value, clock) {
  oauthStorageObject(value);
  if (Object.keys(value).sort().join(',') !== 'providers,revision,version' ||
      !Number.isSafeInteger(value.version) || value.version < 0 || value.version > clock ||
      !Number.isSafeInteger(value.revision) || value.revision < 0 || value.revision > clock ||
      value.version > value.revision ||
      !value.providers || typeof value.providers !== 'object' || Array.isArray(value.providers)) {
    throw new SocialStorageError('unavailable');
  }
  const keys = Object.keys(value.providers);
  if (keys.some((provider) => !IDENTITY_KINDS.includes(provider)) ||
      keys.some((provider) => typeof value.providers[provider] !== 'string' || !OAUTH_HEX.test(value.providers[provider]))) {
    throw new SocialStorageError('unavailable');
  }
  return value;
}

function oauthClock(tx, initialize = false) {
  const clock = tx.get('oauth2:clock');
  const schema = tx.get('oauth2:schema');
  if (clock === null && schema === null) {
    const records = tx.list('oauth2:', { limit: 1 });
    if (records.size) throw new SocialStorageError('unavailable');
    if (initialize) {
      tx.put('oauth2:clock', 0);
      tx.put('oauth2:schema', 1);
    }
    return 0;
  }
  if (clock === null || schema !== 1) throw new SocialStorageError('unavailable');
  return oauthSafe(clock);
}

function oauthMetaFor(tx, pidValue, clock, initialize = false) {
  const player = oauthPlayer(tx, pidValue);
  const meta = tx.get(`oauth2:player:${pidValue}`);
  const marked = player.oauth2;
  if (marked === undefined && meta === null) {
    if (!initialize) return null;
    tx.put(`player:${pidValue}`, { ...player, oauth2: 1 });
    const next = { providers: {}, revision: clock, version: 0 };
    tx.put(`oauth2:player:${pidValue}`, next);
    return next;
  }
  if (marked !== 1 || meta === null) throw new SocialStorageError('unavailable');
  const validated = oauthMeta(meta, clock);
  for (const [provider, hash] of Object.entries(validated.providers)) {
    if (tx.get(`oauth2:subject:${provider}:${hash}`) !== pidValue) throw new SocialStorageError('unavailable');
  }
  return validated;
}

function oauthSubjectPid(tx, provider, subjectHash, clock) {
  const pidValue = tx.get(`oauth2:subject:${provider}:${subjectHash}`);
  if (pidValue === null) return null;
  if (typeof pidValue !== 'string' || !PID.test(pidValue)) throw new SocialStorageError('unavailable');
  const meta = oauthMetaFor(tx, pidValue, clock);
  if (!meta || meta.providers[provider] !== subjectHash) throw new SocialStorageError('unavailable');
  return pidValue;
}

function oauthNextClock(tx, clock) {
  if (clock >= Number.MAX_SAFE_INTEGER) throw new SocialStorageError('unavailable');
  const next = clock + 1;
  tx.put('oauth2:clock', next);
  return next;
}

// ONLINEMENU-1 (PR #8 fix, finding 1): a quota is checked (oauthQuotaCheck, no write) before any is charged (oauthQuotaTake), so a refused request
// writes nothing and charges no other scope
function oauthQuotaCheck(tx, operation, scope, identity, now, limit, window) {
  const bucket = Math.floor(now / window);
  const expires = (bucket + 1) * window;
  oauthSafe(bucket);
  oauthSafe(expires);
  const key = oauthQuotaKey(operation, scope, identity, bucket);
  const previous = tx.get(key);
  const value = previous === null ? { count: 0, expires } : oauthQuotaValue(previous);
  if (value.expires !== expires) throw new SocialStorageError('unavailable');
  return { retry: value.count >= limit ? Math.max(1, expires - now) : 0, key, value, expires, bucket };
}

function oauthQuotaTake(tx, operation, scope, identity, checked) {
  checked.value.count += 1;
  tx.put(checked.key, checked.value);
  tx.put(oauthExpiryKey(checked.expires, `quota:${operation}:${scope}:${identity}:${checked.bucket}`), checked.key);
}

function oauthQuotaRetry(tx, operation, scope, identity, now, limit, window) {
  const checked = oauthQuotaCheck(tx, operation, scope, identity, now, limit, window);
  if (checked.retry) return checked.retry;
  oauthQuotaTake(tx, operation, scope, identity, checked);
  return 0;
}

// ONLINEMENU-1 (PR #8 fix, finding 1): the address buckets of an OAuth request: an IPv4 address, or an IPv6 /64 (one subscriber's usual
// allocation, which one machine can rotate through freely) and the /48 around it (one site's usual allocation) with a wider limit. Anything else
// (an empty or unparsable address) is its own bucket as given
export function oauthAddressBuckets(ip) {
  const text = String(ip ?? '').trim().toLowerCase().replace(/%.*$/, '').replace(/^\[|\]$/g, '');
  const mapped = /^(?:0{0,4}:){0,5}(?:ffff:)(\d{1,3}(?:\.\d{1,3}){3})$/.exec(text) ?? /^::ffff:(\d{1,3}(?:\.\d{1,3}){3})$/.exec(text);
  const v4 = mapped ? mapped[1] : text;
  if (/^\d{1,3}(?:\.\d{1,3}){3}$/.test(v4) && v4.split('.').every((part) => Number(part) <= 255)) return { ip: v4.split('.').map(Number).join('.'), net: null };
  const groups = oauthIpv6Groups(text);
  if (!groups) return { ip: text, net: null };
  // Sol r2: an IPv4-mapped address in hex form (::ffff:c000:201) is that IPv4 address
  if (groups.slice(0, 5).every((group) => group === '0') && groups[5] === 'ffff') {
    const high = parseInt(groups[6], 16);
    const low = parseInt(groups[7], 16);
    return { ip: [high >> 8, high & 255, low >> 8, low & 255].join('.'), net: null };
  }
  return { ip: `v6/64:${groups.slice(0, 4).join(':')}`, net: `v6/48:${groups.slice(0, 3).join(':')}` };
}

function oauthIpv6Groups(text) {
  if (!/^[0-9a-f:.]+$/.test(text) || text.split('::').length > 2) return null;
  let tail = text;
  let v4 = [];
  const dotted = /^(.*:)(\d{1,3}(?:\.\d{1,3}){3})$/.exec(text);
  if (dotted) {
    const parts = dotted[2].split('.').map(Number);
    if (parts.some((part) => part > 255)) return null;
    v4 = [((parts[0] << 8) | parts[1]).toString(16), ((parts[2] << 8) | parts[3]).toString(16)];
    tail = dotted[1].endsWith('::') ? dotted[1] : dotted[1].slice(0, -1);
  }
  const [head, rest] = tail.includes('::') ? tail.split('::') : [tail, null];
  const left = head ? head.split(':') : [];
  const right = rest ? rest.split(':') : [];
  const fixed = left.length + right.length + v4.length;
  if (rest === null ? fixed !== 8 : fixed > 7) return null;
  const groups = [...left, ...Array(8 - fixed).fill('0'), ...right, ...v4];
  if (!groups.every((group) => /^[0-9a-f]{1,4}$/.test(group))) return null;
  return groups.map((group) => parseInt(group, 16).toString(16));
}

// ONLINEMENU-1 (owner 2026-10-20): an e-mail flow (docs/IDENTITY.md "E-mail"): oauth2:email:<ticket>, one per install
// (oauth2:email-install:<install>), and once a link was sent, oauth2:email-token:<sha256 of the link token> -> the ticket
const EMAIL_FLOW_SECONDS = 30 * 60;
const EMAIL_LINK_SECONDS = 15 * 60;
const EMAIL_SENDS_PER_FLOW = 3;
const EMAIL_TOKEN = /^[A-Za-z0-9_-]{43}$/;
const EMAIL_MODES = ['link', 'restore', 'confirm'];
function emailFlowValue(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.v !== 1 || !OAUTH_TRANSACTION.test(value.ticket ?? '') ||
      !EMAIL_MODES.includes(value.mode) || !OAUTH_HEX.test(value.poll_hash ?? '') || !Number.isSafeInteger(value.expires) ||
      !INSTALL.test(value.install ?? '') || !['waiting', 'sent', 'done'].includes(value.state) || !Number.isSafeInteger(value.sends) ||
      (value.token_hash !== undefined && !OAUTH_HEX.test(value.token_hash)) ||
      (value.mode !== 'restore' && (!PID.test(value.pid ?? '') || !Number.isSafeInteger(value.generation) || !Number.isSafeInteger(value.version)))) {
    throw new SocialStorageError('unavailable');
  }
  return value;
}

function emailDeleteFlow(tx, flow) {
  tx.delete(`oauth2:email:${flow.ticket}`);
  if (tx.get(`oauth2:email-install:${flow.install}`) === flow.ticket) tx.delete(`oauth2:email-install:${flow.install}`);
  if (flow.token_hash) tx.delete(`oauth2:email-token:${flow.token_hash}`);
  tx.delete(oauthExpiryKey(flow.expires, `email:${flow.ticket}`));
}

function oauthCleanExpiry(tx, now) {
  oauthClock(tx);
  oauthSafe(now);
  const rows = tx.list('oauth2:expiry:', { limit: OAUTH_MAX_EXPIRY_CLEANUP });
  for (const [index, value] of rows) {
    const match = /^oauth2:expiry:(\d{16}):(pending|quota|email):(.+)$/.exec(index);
    if (!match) throw new SocialStorageError('unavailable');
    const expires = Number(match[1]);
    oauthSafe(expires);
    if (String(expires).padStart(16, '0') !== match[1] || typeof value !== 'string') throw new SocialStorageError('unavailable');
    if (match[2] === 'email') {
      // ONLINEMENU-1: an e-mail flow (oauth2:email:<ticket>) and its install and token indexes
      if (!OAUTH_TRANSACTION.test(match[3]) || value !== `oauth2:email:${match[3]}`) throw new SocialStorageError('unavailable');
      const flow = emailFlowValue(tx.get(value));
      if (flow.ticket !== match[3] || flow.expires !== expires) throw new SocialStorageError('unavailable');
      if (expires <= now) emailDeleteFlow(tx, flow);
      continue;
    }
    if (match[2] === 'pending') {
      if (!OAUTH_TRANSACTION.test(match[3])) throw new SocialStorageError('unavailable');
      const pendingKey = `oauth2:pending:${match[3]}`;
      if (value !== pendingKey) throw new SocialStorageError('unavailable');
      const raw = tx.get(pendingKey);
      if (raw === null) throw new SocialStorageError('unavailable');
      const clock = oauthClock(tx);
      const pending = oauthPending(raw, clock);
      if (pending.transaction !== match[3] || pending.expires !== expires ||
          tx.get(`oauth2:pending-install:${pending.install}`) !== pending.transaction) {
        throw new SocialStorageError('unavailable');
      }
      if (expires <= now) oauthDeletePending(tx, pending);
      continue;
    }
    const quota = /^((?:start|finish|unlink|email)):(global|day|ip|net|install|pid):([a-z0-9]+):(\d+)$/.exec(match[3]);
    if (!quota) throw new SocialStorageError('unavailable');
    const [, operation, scope, identity, bucketText] = quota;
    if (((scope === 'global' || scope === 'day') && !['all', 'link', 'restore'].includes(identity)) ||
        ((scope === 'ip' || scope === 'net') && !OAUTH_HEX.test(identity)) ||
        (scope === 'install' && !OAUTH_HEX.test(identity)) ||
        (scope === 'pid' && !PID.test(identity))) throw new SocialStorageError('unavailable');
    const bucket = Number(bucketText);
    oauthSafe(bucket);
    if (String(bucket) !== bucketText) throw new SocialStorageError('unavailable');
    const window = scope === 'day' ? OAUTH_DAY_SECONDS : OAUTH_QUOTA[operation].window;
    if ((bucket + 1) * window !== expires) throw new SocialStorageError('unavailable');
    const key = oauthQuotaKey(operation, scope, identity, bucket);
    if (value !== key) throw new SocialStorageError('unavailable');
    const stored = tx.get(key);
    if (stored === null || oauthQuotaValue(stored).expires !== expires) throw new SocialStorageError('unavailable');
    if (expires <= now) {
      tx.delete(key);
      tx.delete(index);
    }
  }
}

export class SocialService {
  constructor(lobby) {
    this.lobby = lobby;
    this.active = new Map();
    this.pendingInvites = new Map();
    this.challengeHandoffs = new Map();
    this.notices = new Map();
    this.inboxRevisions = new Map();
    this.inboxEpochOverrides = new Map();
    this.inboxEpoch = hex(lobby.env.randomBytes(8));
    this.inboxSnapshots = new Map();
    this.inboxProfileDependents = new Map();
    this.inboxCacheBytes = 0;
    this.inboxBuildGeneration = 0;
    this.maxInboxEntries = MAX_INBOX_CACHES;
    this.maxInboxBytes = MAX_INBOX_CACHE_BYTES;
    this.pollAuth = new Map();
    this.pollAuthByPid = new Map();
    this.pollAuthGeneration = 0;
    this.pollAuthBuildGeneration = 0;
    this.pollAuthCacheBytes = 0;
    this.blockView = new Map();
    this.freshStartedGames = new Set();
    this.inboxPidHolds = new Map();
    this.loadingUntil = new Map();
    this.leftStartedGames = new Set();
    this.oauthAddressCounts = new Map(); // ONLINEMENU-1 (Sol r2): OAuth's address budgets, in memory (no storage writes)
  }

  // ONLINEMENU-1 (owner 2026-10-20: bans apply to the player id, its linked e-mail / Discord fingerprints and the hardware hash): a block records
  // each linked identity (oauth2:ban:<kind>:<hash> -> the banned install); whether it still holds is read from that install's enforcement when it
  // matters, so a lifted or expired ban ends it without another write. A banned identity cannot be linked to any player or move one
  identityBanned(tx, kind, hash) {
    const value = tx.get(`oauth2:ban:${kind}:${hash}`);
    if (value === null) return false;
    if (!value || typeof value !== 'object' || !INSTALL.test(value.install ?? '')) throw new SocialStorageError('unavailable');
    return this.lobby.installBlocked(value.install);
  }

  async syncIdentityBans(install) {
    if (!this.configured) return;
    await this.transaction((tx) => {
      const pidValue = tx.get(`install:${install}`);
      if (pidValue === null || !PID.test(pidValue)) return;
      const meta = oauthMetaFor(tx, pidValue, oauthClock(tx, true), false);
      if (!meta) return;
      for (const [kind, hash] of Object.entries(meta.providers)) tx.put(`oauth2:ban:${kind}:${hash}`, { install });
    });
  }

  get configured() {
    return typeof this.lobby.env.store?.socialTransaction === 'function';
  }

  async transaction(fn) {
    if (!this.configured) fail(503, 'social_unavailable', 'social storage is not configured');
    const mutations = new Map();
    const invalidatedOwners = new Set();
    const invalidatedProfiles = new Set();
    const invalidatedAuth = new Set();
    let invalidatedAll = false;
    let cacheMutationObserved = false;
    const observe = (key, previous, next) => {
      const impact = this.socialMutationImpact(key, previous, next);
      if (impact.unknown) {
        if (!invalidatedAll) {
          invalidatedAll = true;
          if (!cacheMutationObserved) this.inboxBuildGeneration += 1;
          cacheMutationObserved = true;
          this.invalidateAllCaches();
        }
        return;
      }
      if (!impact.owners.size && !impact.profiles.size && !impact.auth.size && !impact.blocks.length) return;
      if (!cacheMutationObserved) this.inboxBuildGeneration += 1;
      cacheMutationObserved = true;
      for (const owner of impact.owners) if (!invalidatedOwners.has(owner)) {
        invalidatedOwners.add(owner);
        this.invalidateInboxOwner(owner);
      }
      for (const profile of impact.profiles) if (!invalidatedProfiles.has(profile)) {
        invalidatedProfiles.add(profile);
        this.invalidateInboxProfile(profile, invalidatedOwners);
      }
      for (const account of impact.auth) if (!invalidatedAuth.has(account)) {
        invalidatedAuth.add(account);
        this.invalidatePollAuth(account);
      }
      if (impact.blocks.length) mutations.set(key, impact.blocks.at(-1));
    };
    const needsPrevious = (key) => /^(?:install|code|recovery|request):/.test(key);
    try {
      const result = await this.lobby.env.store.socialTransaction((tx) => {
        const observed = {
          get: (key) => repairStoredText(tx.get(key)),
          list: (prefix, options) => new Map([...tx.list(prefix, options)].map(([key, value]) => [key, repairStoredText(value)])),
          put: (key, value) => {
            const previous = needsPrevious(key) ? tx.get(key) : null;
            observe(key, previous, value);
            return tx.put(key, value);
          },
          delete: (key) => {
            const previous = needsPrevious(key) ? tx.get(key) : null;
            observe(key, previous, null);
            return tx.delete(key);
          },
        };
        const now = time(this.lobby);
        cleanExpiredQuotas(observed, now);
        oauthCleanExpiry(observed, now);
        cleanExpiredStartedGames(observed, now);
        return fn(observed);
      });
      if (cacheMutationObserved) {
        this.inboxBuildGeneration += 1;
        if (invalidatedAll) this.invalidateAllCaches();
        else {
          for (const account of invalidatedAuth) this.invalidatePollAuth(account);
          for (const owner of invalidatedOwners) this.invalidateInboxOwner(owner);
          for (const profile of invalidatedProfiles) this.invalidateInboxProfile(profile, invalidatedOwners);
        }
        for (const [key, mutation] of mutations) this.applyBlockMutation(key, mutation);
      }
      return result;
    } catch (error) {
      if (error instanceof SocialStorageError) throw normalizeSocialFailure(error);
      throw error;
    }
  }

  socialMutationImpact(key, previous, next) {
    const parts = key.split(':');
    const family = parts[0];
    const result = { owners: new Set(), profiles: new Set(), auth: new Set(), blocks: [], unknown: false };
    const addPair = (a, b, set = result.owners) => {
      if (PID.test(a ?? '') && PID.test(b ?? '')) { set.add(a); set.add(b); }
    };
    const addProfile = (value) => {
      if (PID.test(value ?? '')) {
        result.profiles.add(value);
        result.auth.add(value);
      }
    };
    if (family === 'player') addProfile(parts[1]);
    else if (family === 'install' || family === 'code' || family === 'recovery') {
      addProfile(typeof previous === 'string' ? previous : null);
      addProfile(typeof next === 'string' ? next : null);
    } else if (family === 'friend' || family === 'block') {
      addPair(parts[1], parts[2]);
      if (family === 'block') result.blocks.push({ owner: parts[1], target: parts[2], value: next });
    } else if (family === 'request-in' || family === 'request-out') {
      if (PID.test(parts[1] ?? '')) result.owners.add(parts[1]);
    } else if (family === 'request-pair') addPair(parts[1], parts[2]);
    else if (family === 'request') {
      for (const request of [previous, next]) if (request && typeof request === 'object') addPair(request.from, request.to);
    } else if (family === 'thread' || family === 'conversation' || family === 'read' || family === 'message') {
      addPair(parts[1], parts[2]);
    } else if (family === 'receipt') {
      addPair(parts[2], parts[3]);
      if (PID.test(parts[1] ?? '')) result.owners.add(parts[1]);
    }
    else if (family === 'quota' || family === 'quota-expire' || family === 'invite-suppress' ||
        family === 'recent' || family === 'recent-started' || family === 'recent-started-expire' || family === 'moved') return result;
    else result.unknown = true;
    return result;
  }

  invalidateInboxOwner(pidValue) {
    if (!PID.test(pidValue ?? '')) return;
    this.advanceInboxRevision(pidValue);
    this.deleteInboxSnapshot(pidValue);
    this.wakeInbox(pidValue);
  }

  invalidateInboxProfile(pidValue, already = new Set()) {
    if (!already.has(pidValue)) {
      already.add(pidValue);
      this.invalidateInboxOwner(pidValue);
    }
    for (const owner of [...(this.inboxProfileDependents.get(pidValue) ?? [])]) {
      if (already.has(owner)) continue;
      already.add(owner);
      this.invalidateInboxOwner(owner);
    }
  }

  advanceInboxRevision(pidValue) {
    if (!this.inboxRevisions.has(pidValue) && this.inboxRevisions.size >= MAX_INBOX_CACHES) {
      this.rotateInboxEpoch();
    }
    const next = (this.inboxRevisions.get(pidValue) ?? 0) + 1;
    if (!Number.isSafeInteger(next)) this.rotateInboxEpoch();
    this.inboxRevisions.set(pidValue, (this.inboxRevisions.get(pidValue) ?? 0) + 1);
  }

  deleteInboxSnapshot(pidValue, { evicted = false } = {}) {
    const entry = this.inboxSnapshots.get(pidValue);
    if (!entry) return;
    this.inboxSnapshots.delete(pidValue);
    this.inboxCacheBytes -= entry.bytes;
    for (const dependency of entry.profileDeps) {
      const owners = this.inboxProfileDependents.get(dependency);
      owners?.delete(pidValue);
      if (owners && !owners.size) this.inboxProfileDependents.delete(dependency);
    }
    if (evicted) {
      this.advanceInboxRevision(pidValue);
      this.wakeInbox(pidValue);
    }
  }

  rotateInboxEpoch() {
    const owners = [...this.inboxSnapshots.keys()];
    for (const pidValue of owners) this.lobby.wake(`inbox:${pidValue}`);
    this.inboxEpoch = hex(this.lobby.env.randomBytes(8));
    this.inboxSnapshots.clear();
    this.inboxProfileDependents.clear();
    this.inboxRevisions.clear();
    this.inboxEpochOverrides.clear();
    this.inboxCacheBytes = 0;
    this.inboxBuildGeneration += 1;
  }

  inboxSeq(pidValue, epoch, revision) {
    return `${this.inboxEpochOverrides.get(pidValue) ?? epoch}:${revision}`;
  }

  invalidateAllCaches() {
    this.pollAuthGeneration += 1;
    this.pollAuthBuildGeneration += 1;
    this.pollAuth.clear();
    this.pollAuthByPid.clear();
    this.pollAuthCacheBytes = 0;
    this.blockView.clear();
    for (const pidValue of new Set([...this.inboxSnapshots.keys(), ...this.inboxPidHolds.keys()])) this.invalidateInboxOwner(pidValue);
    this.inboxBuildGeneration += 1;
  }

  securityStateChanged() {
    this.invalidateAllCaches();
  }

  invalidatePollAuth(pidValue) {
    this.pollAuthBuildGeneration += 1;
    const keys = this.pollAuthByPid.get(pidValue);
    if (!keys) return;
    for (const key of [...keys]) this.removePollAuth(key);
  }

  clearPollAuth() {
    this.pollAuthGeneration += 1;
    this.pollAuthBuildGeneration += 1;
    this.pollAuth.clear();
    this.pollAuthByPid.clear();
    this.pollAuthCacheBytes = 0;
  }

  async authorizePoll(account, key) {
    for (let attempt = 0; attempt < 2; attempt++) {
      const generation = this.pollAuthGeneration;
      const buildGeneration = this.pollAuthBuildGeneration;
      let cached = this.pollAuth.get(key);
      if (cached && cached.expiresAt > this.lobby.env.now() && cached.generation === generation) {
        this.pollAuth.delete(key);
        this.pollAuth.set(key, cached);
        const current = this.lobby.checkAccount(account, { security: cached.security });
        Object.defineProperty(current, 'pollSecurity', { value: cached.security });
        return current;
      }
      if (cached) this.removePollAuth(key, cached);
      await this.assertSession(account);
      if (generation !== this.pollAuthGeneration || buildGeneration !== this.pollAuthBuildGeneration) continue;
      const security = this.lobby.accountSecurity(account);
      const current = this.lobby.checkAccount(account, { security });
      if (generation !== this.pollAuthGeneration || buildGeneration !== this.pollAuthBuildGeneration) continue;
      const bytes = encoder.encode(serial([key, security])).length;
      if (bytes <= MAX_POLL_AUTH_CACHE_BYTES) {
        const entry = { generation, expiresAt: account.exp * 1000, security, pid: account.pid, install: account.install, bytes };
        this.removePollAuth(key);
        this.pollAuth.set(key, entry);
        this.pollAuthCacheBytes += bytes;
        for (const identity of [account.pid, account.install]) {
          if (!identity) continue;
          const keys = this.pollAuthByPid.get(identity) ?? new Set();
          keys.add(key);
          this.pollAuthByPid.set(identity, keys);
        }
        while (this.pollAuth.size > MAX_POLL_AUTH_CACHES || this.pollAuthCacheBytes > MAX_POLL_AUTH_CACHE_BYTES) {
          const oldest = this.pollAuth.keys().next().value;
          this.removePollAuth(oldest);
        }
      }
      Object.defineProperty(current, 'pollSecurity', { value: security });
      return current;
    }
    fail(503, 'social_overloaded', 'the lobby is refreshing social data; retry shortly');
  }

  removePollAuth(key) {
    const entry = this.pollAuth.get(key);
    this.pollAuth.delete(key);
    if (!entry) return;
    this.pollAuthCacheBytes -= entry.bytes;
    for (const identity of [entry.pid, entry.install]) {
      const keys = this.pollAuthByPid.get(identity);
      keys?.delete(key);
      if (keys && !keys.size) this.pollAuthByPid.delete(identity);
    }
  }

  applyBlockMutation(key, mutation) {
    if (!PID.test(mutation.owner ?? '') || !PID.test(mutation.target ?? '')) return;
    const blocked = mutation.value !== null;
    const view = this.blockView.get(mutation.owner);
    if (!view) return;
    if (blocked) view.add(mutation.target);
    else view.delete(mutation.target);
  }

  async hash(domain, value) {
    return hex(await this.lobby.env.hmac(this.lobby.config.sessionKey, encoder.encode(`openbfme-social-v1:${domain}:${value}`)));
  }

  async signIn(account, { recover = null, ip = '' } = {}) {
    if (!account.install) {
      if (recover !== null) fail(401, 'install_required', 'recovery requires an install identity');
      return null;
    }
    if (!this.configured) {
      if (recover !== null || this.lobby.env.socialStorageRequired) fail(503, 'social_unavailable', 'social storage is not configured');
      return null;
    }
    const now = time(this.lobby);
    const ipKey = await this.hash('ip', ip);
    const recoveryHash = recover === null ? null : await this.hash('recovery', recover);
    const pids = Array.from({ length: 8 }, () => newPid(this.lobby.env.randomBytes));
    const codes = Array.from({ length: 8 }, () => newCode(this.lobby.env.randomBytes));
    const movedRecord = `moved:${account.install}`; // the store keys installs as they are (install:<id>)
    const outcome = await this.transaction((tx) => {
      const installPid = tx.get(`install:${account.install}`);
      if (installPid !== null && !PID.test(installPid)) throw new SocialStorageError('unavailable');
      // ONLINEMENU-1 (finding 2): this install's player was moved to another install (restore): it is told once, here
      const movedValue = tx.get(movedRecord);
      const answer = (value) => {
        if (!movedValue) return value;
        if (movedValue.index) tx.delete(movedValue.index);
        tx.delete(movedRecord);
        return Number.isSafeInteger(movedValue.at) && movedValue.until > now ? { ...value, moved: { at: movedValue.at, via: movedValue.via } } : value;
      };
      if (recoveryHash) {
        const ipAttempt = takeAttempts(tx, `quota:recovery-ip:${ipKey}`, now, 5, 3600);
        if (ipAttempt.retry) return { error: new SocialError(429, 'rate_limited', 'recovery attempts are limited', { retry_after: ipAttempt.retry }) };
        const sourcePid = tx.get(`recovery:${recoveryHash}`);
        if (!sourcePid) return { error: new SocialError(401, 'bad_recovery', 'the recovery code is invalid or has been used') };
        const player = tx.get(`player:${sourcePid}`);
        if (!PID.test(sourcePid) || !player) throw new SocialStorageError('unavailable');
        validatePlayer(player, sourcePid);
        const pidAttempt = takeAttempts(tx, `quota:recovery-pid:${sourcePid}`, now, 5, 3600);
        if (pidAttempt.retry) return { error: new SocialError(429, 'rate_limited', 'recovery attempts are limited', { retry_after: pidAttempt.retry }) };
        if (player.recovery_hash !== recoveryHash || tx.get(`install:${player.install}`) !== sourcePid ||
            tx.get(`code:${player.code}`) !== sourcePid) throw new SocialStorageError('unavailable');
        if (installPid && installPid !== sourcePid && this.releaseMovedPlaceholder(tx, account.install, sourcePid) !== null) {
          return { error: new SocialError(409, 'install_in_use', 'this install identity is already in use') };
        }
        const rebound = this.rebindPlayer(tx, sourcePid, {
          name: account.name,
          install: account.install,
          device: account.device ?? null,
        }, { generation: player.generation }, now, { via: 'recovery' });
        return answer({ pid: sourcePid, generation: rebound.generation, recovered: true });
      }
      if (installPid) {
        const existing = tx.get(`player:${installPid}`);
        if (!existing) throw new Error('social install index is malformed');
        validatePlayer(existing, installPid);
        if (existing.install !== account.install || tx.get(`install:${existing.install}`) !== installPid ||
            tx.get(`code:${existing.code}`) !== installPid) throw new SocialStorageError('unavailable');
        const next = { ...existing, nick: account.name, device: account.device ?? null, updated_at: now };
        tx.put(`player:${installPid}`, next);
        return answer({ pid: installPid, generation: next.generation, recovered: false });
      }
      let selected = null;
      for (let i = 0; i < pids.length; i++) {
        if (tx.get(`player:${pids[i]}`) || tx.get(`code:${codes[i]}`)) continue;
        selected = { pid: pids[i], code: codes[i] };
        break;
      }
      if (!selected) throw new Error('social identity collision retry limit reached');
      const player = {
        pid: selected.pid,
        install: account.install,
        device: account.device ?? null,
        nick: account.name,
        code: selected.code,
        generation: 1,
        privacy: { ...DEFAULT_PRIVACY },
        hidden: false,
        created_at: now,
        updated_at: now,
        ...(movedValue ? { after_move: now } : {}),
      };
      tx.put(`player:${selected.pid}`, player);
      tx.put(`install:${account.install}`, selected.pid);
      tx.put(`code:${selected.code}`, selected.pid);
      return answer({ pid: selected.pid, generation: 1, recovered: false });
    });
    if (outcome.error) throw outcome.error;
    for (const [accountId, handoff] of this.challengeHandoffs) {
      if (handoff.pid === outcome.pid) this.challengeHandoffs.delete(accountId);
    }
    if (outcome.recovered) {
      this.finishRebind(outcome.pid);
    }
    return { pid: outcome.pid, generation: outcome.generation, ...(outcome.moved ? { moved: outcome.moved } : {}) };
  }

  async assertSession(account) {
    if (!account.pid) return account;
    if (!this.configured) fail(503, 'social_unavailable', 'social storage is not configured');
    const records = await this.transaction((tx) => ({
      player: tx.get(`player:${account.pid}`),
      install: tx.get(`install:${account.install}`),
      code: tx.get(`code:${tx.get(`player:${account.pid}`)?.code ?? ''}`),
    }));
    const player = records.player;
    if (!player || player.install !== account.install || player.generation !== account.generation) {
      fail(401, 'session_revoked', 'this session has been revoked');
    }
    validatePlayer(player, account.pid);
    if (records.install !== account.pid || records.code !== account.pid) throw new SocialStorageError('unavailable');
    await this.refreshBlockView(account.pid);
    return account;
  }

  resetCaches() {
    this.clearPollAuth();
    this.inboxBuildGeneration += 1;
    this.inboxEpoch = hex(this.lobby.env.randomBytes(8));
    this.inboxRevisions.clear();
    this.inboxEpochOverrides.clear();
    this.inboxSnapshots.clear();
    this.inboxProfileDependents.clear();
    this.inboxCacheBytes = 0;
    this.blockView.clear();
    this.lobby.wakeAll();
  }

  async player(pidValue) {
    if (!PID.test(pidValue)) throw new SocialStorageError('unavailable');
    const records = await this.transaction((tx) => {
      const player = tx.get(`player:${pidValue}`);
      return player ? { player, install: tx.get(`install:${player.install}`), code: tx.get(`code:${player.code}`) } : null;
    });
    if (!records) return null;
    const player = validatePlayer(records.player, pidValue);
    if (records.install !== pidValue || records.code !== pidValue) throw new SocialStorageError('unavailable');
    return player;
  }

  async me(account) {
    const player = await this.current(account);
    return { v: VERSION, pid: player.pid, code: player.code, nick: player.nick, privacy: { ...player.privacy }, hidden: player.hidden };
  }

  async current(account) {
    if (!account?.pid) fail(401, 'install_required', 'sign in with an install identity to use social features');
    await this.assertSession(account);
    const player = await this.player(account.pid);
    if (!player) fail(401, 'install_required', 'sign in with an install identity to use social features');
    return validatePlayer(player, account.pid);
  }

  async chargeMalformedRecovery(ip, install = null) {
    if (!this.configured) {
      if (this.lobby.env.socialStorageRequired) fail(503, 'social_unavailable', 'social storage is not configured');
      return;
    }
    const ipKey = await this.hash('ip', ip);
    const now = time(this.lobby);
    const retry = await this.transaction((tx) => {
      const attempts = [takeAttempts(tx, `quota:recovery-ip:${ipKey}`, now, 5, 3600)];
      if (install !== null) {
        const pidValue = tx.get(`install:${install}`);
        if (pidValue !== null) {
          if (!PID.test(pidValue)) throw new SocialStorageError('unavailable');
          const player = tx.get(`player:${pidValue}`);
          if (!player) throw new SocialStorageError('unavailable');
          validatePlayer(player, pidValue);
          attempts.push(takeAttempts(tx, `quota:recovery-pid:${pidValue}`, now, 5, 3600));
        }
      }
      return attempts.find((entry) => entry.retry)?.retry ?? 0;
    });
    if (retry) fail(429, 'rate_limited', 'recovery attempts are limited', { retry_after: retry });
  }

  async chargeRecoveryAttempt(account, ip) {
    const player = await this.current(account);
    const ipKey = await this.hash('ip', ip);
    const result = await this.transaction((tx) => {
      const attempts = [
        takeAttempts(tx, `quota:recovery-ip:${ipKey}`, time(this.lobby), 5, 3600),
        takeAttempts(tx, `quota:recovery-pid:${player.pid}`, time(this.lobby), 5, 3600),
      ];
      return attempts.find((entry) => entry.retry)?.retry ?? 0;
    });
    if (result) fail(429, 'rate_limited', 'recovery attempts are limited', { retry_after: result });
  }

  async chargeRejectedMutation(account, path, ip) {
    if (!account?.pid) {
      await this.current(account);
      return;
    }
    const route = path.slice(1);
    const now = time(this.lobby);
    const ipHash = await this.hash('ip', ip);
    const attempts = route[0] === 'friends' && route[1] === 'requests'
      ? [
        [`quota:request-code-pid:${account.pid}`, 20, 60],
        [`quota:request-code-ip:${ipHash}`, 20, 60],
        [`quota:request-pid:${account.pid}`, 10, 3600],
        [`quota:request-ip-hour:${ipHash}`, 10, 3600],
        [`quota:request-pid-day:${account.pid}`, 30, 86400],
        [`quota:request-ip-day:${ipHash}`, 30, 86400],
      ]
      : route[0] === 'invites' && route.length === 1
        ? [[`quota:invite-pid:${account.pid}`, 6, 60], [`quota:invite-ip:${ipHash}`, 6, 60]]
        : route[0] === 'chat' && route[2] === 'messages' && path.length === 4
          ? [
            [`quota:chat-minute-pid:${account.pid}`, 30, 60],
            [`quota:chat-minute-ip:${ipHash}`, 30, 60],
            [`quota:chat-day-pid:${account.pid}`, 500, 86400],
            [`quota:chat-day-ip:${ipHash}`, 500, 86400],
          ]
          : route[0] === 'me' && route[1] === 'recovery'
            ? [[`quota:recovery-pid:${account.pid}`, 5, 3600], [`quota:recovery-ip:${ipHash}`, 5, 3600]]
            : [];
    if (attempts.length === 0) return;
    const retry = await this.transaction((tx) =>
      attempts.map(([key, limit, window]) => takeAttempts(tx, key, now, limit, window))
        .find((entry) => entry.retry)?.retry ?? 0);
    if (retry) fail(429, 'rate_limited', 'social mutation attempts are limited', { retry_after: retry });
  }

  // ONLINEMENU-1 (PR #8 fix, finding 1: one address could exhaust the global budget and turn OAuth off for everyone). Every narrow scope (the
  // address, the install, the player) is checked first; the global budgets are charged only by a request that passed all of them, and only once
  // the request is known to be well formed (the route charges the address alone, before it reads the body). Nothing is charged when any scope
  // refuses. `global` names the global bucket ('all', or the start / finish mode, so a restore flood cannot use up linking's budget); a global
  // charge also counts against the bucket's daily cap (OAUTH_DAILY). Sol r2: the address budgets live in memory, so an address-only check (a
  // malformed or refused request) writes nothing to storage
  async chargeOAuth(kind, ip, account = null, { global = 'all' } = {}) {
    const limits = OAUTH_QUOTA[kind];
    if (!limits) throw new SocialStorageError('unavailable');
    const now = time(this.lobby);
    oauthSafe(now);
    const address = ip === null ? null : oauthAddressBuckets(ip);
    // Sol r3: every narrow scope (address, /48, install, player) is counted in memory, so only a request admitted by the global and daily budgets
    // writes anything; the daily caps then bound all of OAuth's storage work (a freshly registered guest's malformed link start writes nothing)
    const memory = [];
    if (address !== null) memory.push([`${kind}:ip:${address.ip}`, limits.ip]);
    if (address?.net) memory.push([`${kind}:net:${address.net}`, limits.ip * OAUTH_NET_FACTOR]);
    if (account?.install) memory.push([`${kind}:install:${account.install}`, limits.install]);
    if (account?.pid) memory.push([`${kind}:pid:${account.pid}`, limits.pid]);
    const bucket = Math.floor(now / limits.window);
    for (const [key, limit] of memory) {
      const entry = this.oauthAddressCounts.get(key);
      if (entry && entry.bucket === bucket && entry.count >= limit) fail(429, 'rate_limited', 'OAuth attempts are limited', { retry_after: Math.max(1, (bucket + 1) * limits.window - now) });
    }
    const scopes = [];
    if (global) {
      const name = global === true ? 'all' : global;
      scopes.push(['global', name, limits.global, limits.window]);
      scopes.push(['day', name, OAUTH_DAILY[kind][name], OAUTH_DAY_SECONDS]);
    }
    if (scopes.length) {
      const retry = await this.transaction((tx) => {
        oauthClock(tx, true);
        const checked = [];
        for (const [scope, identity, limit, window] of scopes) {
          const check = oauthQuotaCheck(tx, kind, scope, identity, now, limit, window);
          if (check.retry) return check.retry;
          checked.push([scope, identity, check]);
        }
        for (const [scope, identity, check] of checked) oauthQuotaTake(tx, kind, scope, identity, check);
        return 0;
      });
      if (retry) fail(429, 'rate_limited', 'OAuth attempts are limited', { retry_after: retry });
    }
    if (this.oauthAddressCounts.size > OAUTH_ADDRESS_ENTRIES) {
      for (const [key, entry] of this.oauthAddressCounts) if (entry.bucket < bucket) this.oauthAddressCounts.delete(key);
      if (this.oauthAddressCounts.size > OAUTH_ADDRESS_ENTRIES) this.oauthAddressCounts.clear();
    }
    for (const [key] of memory) {
      const entry = this.oauthAddressCounts.get(key);
      this.oauthAddressCounts.set(key, entry && entry.bucket === bucket ? { bucket, count: entry.count + 1 } : { bucket, count: 1 });
    }
  }

  async oauthStart(body, { authorization = undefined, hasAuthorization = false, ip = '', charged = false } = {}) {
    if (!charged) await this.chargeOAuth('start', ip, null, { global: null });
    if (!body || typeof body !== 'object' || Array.isArray(body)) invalid('the body must be an object');
    let authenticated = null;
    const signedIn = body.mode === 'link' || body.mode === 'confirm';
    if (signedIn && hasAuthorization) {
      authenticated = await this.lobby.account(authorization);
      await this.current(authenticated);
      await this.chargeOAuth('start', null, authenticated, { global: null });
    }
    if (body.mode === 'link') exact(body, ['v', 'provider', 'mode', 'state', 'code_challenge', 'install'], ['recovery']);
    else if (body.mode === 'confirm') exact(body, ['v', 'provider', 'mode', 'state', 'code_challenge', 'install']);
    else if (body.mode === 'restore') exact(body, ['v', 'provider', 'mode', 'state', 'code_challenge', 'name', 'install'], ['device', 'client']);
    else invalid('mode must be link, confirm or restore');
    const provider = oauthProvider(body.provider);
    const state = oauthState(body.state);
    const challenge = oauthChallenge(body.code_challenge);
    const config = providerConfig(this.lobby.config.oauth, provider);
    if (!config) throw new SocialError(503, 'oauth_provider_unavailable', 'the OAuth provider is unavailable');

    let identity;
    let account = null;
    if (signedIn) {
      if (!hasAuthorization) fail(401, 'signed_out', 'sign in first');
      account = authenticated;
      if (!account) fail(401, 'signed_out', 'sign in first');
      if (!account.install) fail(401, 'install_required', 'an install identity is required');
      this.lobby.proveInstall(account, body.install); // ONLINEMENU-1 (finding 2)
      identity = { accountId: account.id, pid: account.pid, generation: account.generation, install: account.install, device: account.device ?? null };
    } else {
      if (hasAuthorization) fail(401, 'signed_out', 'restore does not accept an authorization header');
      identity = oauthIdentityFields(body);
      this.lobby.checkAccount(identity);
      await this.chargeOAuth('start', null, identity, { global: 'restore' });
    }
    const now = time(this.lobby);
    oauthSafe(now);
    const expires = now + OAUTH_PENDING_SECONDS;
    oauthSafe(expires);
    const recoveryHash = account && body.recovery !== undefined ? await this.recoveryProofHash(body.recovery) : null;
    if (recoveryHash !== null) await this.proveRecovery(account.pid, recoveryHash);
    if (account) await this.chargeOAuth('start', null, null, { global: 'link' }); // Sol r2: the global budget after validation and the proofs
    const result = await this.transaction((tx) => {
      const clock = oauthClock(tx, true);
      let version = null;
      let revision = null;
      if (account) {
        const player = oauthSessionPlayer(tx, account);
        const meta = oauthMetaFor(tx, account.pid, clock, true);
        if (body.mode === 'confirm') {
          // ONLINEMENU-1 (Sol r2): confirming with a linked provider (a recovery code for a player whose providers are linked) needs that link
          if (!Object.hasOwn(meta.providers, provider)) fail(404, 'oauth_not_linked', 'that provider account is not linked');
        } else {
          // ONLINEMENU-1 (finding 2): a linked provider is never replaced, and a second one needs the recovery code (a session alone could add
          // its holder's account next to the owner's and restore with it). Finish re-checks the version, so nothing is linked in between
          if (Object.hasOwn(meta.providers, provider)) fail(409, 'oauth_provider_exists', 'that provider is already linked: unlink it first');
          if (Object.keys(meta.providers).length) this.requireRecoveryProof(player, recoveryHash);
        }
        version = meta.version;
        revision = meta.revision;
      }
      const previous = tx.get(`oauth2:pending-install:${identity.install}`);
      if (previous !== null) {
        if (typeof previous !== 'string' || !OAUTH_TRANSACTION.test(previous)) throw new SocialStorageError('unavailable');
        const old = tx.get(`oauth2:pending:${previous}`);
        if (old === null) throw new SocialStorageError('unavailable');
        oauthDeletePending(tx, oauthPending(old, clock));
      }
      let transaction = null;
      for (let attempt = 0; attempt < 8; attempt++) {
        const candidate = hex(this.lobby.env.randomBytes(24));
        if (OAUTH_TRANSACTION.test(candidate) && tx.get(`oauth2:pending:${candidate}`) === null) {
          transaction = candidate;
          break;
        }
      }
      if (!transaction) throw new SocialStorageError('unavailable');
      const pending = {
        v: 1, transaction, provider, mode: body.mode, state, challenge, expires,
        install: identity.install, device: identity.device,
        ...(account ? { pid: account.pid, accountId: account.id, generation: account.generation, version, revision }
          : { pid: null, name: identity.name, watermark: clock, ...(identity.client ? { client: identity.client } : {}) }),
      };
      tx.put(`oauth2:pending:${transaction}`, pending);
      tx.put(`oauth2:pending-install:${identity.install}`, transaction);
      tx.put(oauthExpiryKey(expires, `pending:${transaction}`), `oauth2:pending:${transaction}`);
      return { transaction, pid: account?.pid ?? null };
    });
    return { v: VERSION, ...result, authorization_url: authorizationUrl(provider, config.id, state, challenge) };
  }

  // ---- e-mail sign-in (ONLINEMENU-1, owner 2026-10-20; docs/IDENTITY.md "E-mail") ---------------------------------------------------------------------
  // The launcher starts a flow (link: the session and its install; restore: the new install; confirm: a linked player's new recovery code) and keeps
  // a poll secret (it sends its SHA-256); the player types the address on the lobby's page (Turnstile), gets a one-time link (15 minutes), confirms it
  // with a button (a POST: mail scanners opening links consume nothing), and the launcher's poll collects the result. The address is used for that one
  // send; only HMAC fingerprints are kept. Budgets as OAuth's: refused or malformed requests write nothing; admitted ones count against daily caps
  emailConfig() {
    const config = this.lobby.config.email;
    if (!config) throw new SocialError(503, 'email_unavailable', 'e-mail sign-in is not configured');
    if (config.error) throw new SocialError(503, 'email_unavailable', config.error);
    return config;
  }

  disposableDomains() {
    this.disposableSet ??= new Set(DISPOSABLE_DOMAINS);
    return this.disposableSet;
  }

  memoryLimit(key, limit, window, now, message) {
    const bucket = Math.floor(now / window);
    const entry = this.oauthAddressCounts.get(key);
    if (entry && entry.bucket === bucket && entry.count >= limit) fail(429, 'rate_limited', message, { retry_after: Math.max(1, (bucket + 1) * window - now) });
    this.oauthAddressCounts.set(key, entry && entry.bucket === bucket ? { bucket, count: entry.count + 1 } : { bucket, count: 1 });
  }

  async emailStart(body, { authorization, hasAuthorization = false, ip = '' } = {}) {
    await this.chargeOAuth('start', ip, null, { global: null });
    if (!body || typeof body !== 'object' || Array.isArray(body)) invalid('the body must be an object');
    this.emailConfig();
    const mode = body.mode;
    let account = null;
    let identity;
    if (mode === 'link' || mode === 'confirm') {
      if (!hasAuthorization) fail(401, 'signed_out', 'sign in first');
      account = await this.lobby.account(authorization);
      await this.current(account);
      await this.chargeOAuth('start', null, account, { global: null });
      exact(body, ['v', 'mode', 'install', 'poll_hash'], mode === 'link' ? ['recovery'] : []);
      if (!account.install) fail(401, 'install_required', 'an install identity is required');
      this.lobby.proveInstall(account, body.install);
      identity = { install: account.install, device: account.device ?? null };
    } else if (mode === 'restore') {
      if (hasAuthorization) fail(401, 'signed_out', 'restore does not accept an authorization header');
      exact(body, ['v', 'mode', 'install', 'poll_hash', 'name'], ['device', 'client']);
      identity = oauthIdentityFields(body);
      this.lobby.checkAccount(identity);
      await this.chargeOAuth('start', null, identity, { global: null });
    } else {
      invalid('mode must be link, confirm or restore');
    }
    if (typeof body.poll_hash !== 'string' || !OAUTH_HEX.test(body.poll_hash)) invalid('poll_hash must be 64 lowercase hex digits');
    const recoveryHash = account && body.recovery !== undefined ? await this.recoveryProofHash(body.recovery) : null;
    if (recoveryHash !== null) await this.proveRecovery(account.pid, recoveryHash);
    await this.chargeOAuth('start', null, null, { global: mode === 'restore' ? 'restore' : 'link' });
    const now = time(this.lobby);
    const expires = now + EMAIL_FLOW_SECONDS;
    const ticket = await this.transaction((tx) => {
      const clock = oauthClock(tx, true);
      let fields;
      if (account) {
        const player = oauthSessionPlayer(tx, account);
        const meta = oauthMetaFor(tx, account.pid, clock, true);
        if (mode === 'confirm') {
          if (!Object.hasOwn(meta.providers, 'email')) fail(404, 'oauth_not_linked', 'no e-mail address is linked to this player');
        } else {
          if (Object.hasOwn(meta.providers, 'email')) fail(409, 'oauth_provider_exists', 'an e-mail address is linked already: unlink it first');
          if (Object.keys(meta.providers).length) this.requireRecoveryProof(player, recoveryHash); // a second identity needs the recovery code
        }
        fields = { pid: account.pid, accountId: account.id, generation: account.generation, version: meta.version, revision: meta.revision };
      } else {
        fields = { name: identity.name, watermark: clock, ...(identity.client ? { client: identity.client } : {}) };
      }
      const previous = tx.get(`oauth2:email-install:${identity.install}`);
      if (previous !== null) {
        const old = tx.get(`oauth2:email:${previous}`);
        if (old !== null) emailDeleteFlow(tx, emailFlowValue(old));
        else tx.delete(`oauth2:email-install:${identity.install}`);
      }
      let made = null;
      for (let attempt = 0; attempt < 8 && made === null; attempt++) {
        const candidate = hex(this.lobby.env.randomBytes(24));
        if (OAUTH_TRANSACTION.test(candidate) && tx.get(`oauth2:email:${candidate}`) === null) made = candidate;
      }
      if (made === null) throw new SocialStorageError('unavailable');
      tx.put(`oauth2:email:${made}`, {
        v: 1, ticket: made, mode, poll_hash: body.poll_hash, expires, install: identity.install, device: identity.device, state: 'waiting', sends: 0, ...fields,
      });
      tx.put(`oauth2:email-install:${identity.install}`, made);
      tx.put(oauthExpiryKey(expires, `email:${made}`), `oauth2:email:${made}`);
      return made;
    });
    return { v: VERSION, ticket, url: `${this.lobby.config.publicUrl}/email/${ticket}`, expires };
  }

  async emailFlow(ticket) {
    return this.transaction((tx) => {
      const raw = tx.get(`oauth2:email:${ticket}`);
      return raw === null ? null : emailFlowValue(raw);
    });
  }

  async emailRequest(body, { ip = '' } = {}) {
    await this.chargeOAuth('email', ip, null, { global: null });
    exact(body, ['v', 'ticket', 'email', 'turnstile']);
    const config = this.emailConfig();
    if (typeof body.ticket !== 'string' || !OAUTH_TRANSACTION.test(body.ticket)) invalid('ticket must be 48 lowercase hex digits');
    if (typeof body.email !== 'string' || body.email.length > 320) fail(400, 'bad_email', 'that e-mail address is not valid');
    if (typeof body.turnstile !== 'string' || !body.turnstile.length || body.turnstile.length > 2048) invalid('turnstile is malformed');
    let address;
    try {
      address = normalizeEmail(body.email);
    } catch (error) {
      if (error instanceof EmailError) fail(400, 'bad_email', 'that e-mail address is not valid');
      throw error;
    }
    if (isDisposable(address.domain, this.disposableDomains())) fail(400, 'disposable_email', 'use a permanent e-mail address, not a disposable one');
    const now = time(this.lobby);
    const flow = await this.emailFlow(body.ticket);
    if (!flow || flow.expires <= now || flow.state === 'done') fail(400, 'email_expired', 'this page has expired: start again in the launcher');
    if (flow.sends >= EMAIL_SENDS_PER_FLOW) fail(429, 'rate_limited', 'this page has sent its e-mails: start again in the launcher');
    let human;
    try {
      human = await verifyTurnstile(this.lobby.env, config.turnstileSecret, body.turnstile, ip, { action: 'email', hostname: config.hostname });
    } catch (error) {
      if (error instanceof EmailProviderFailure) fail(502, 'email_unavailable', 'the check could not be done: try again');
      throw error;
    }
    if (!human) fail(403, 'turnstile', 'the check failed: reload the page and try again');
    const fingerprint = await this.hash('email', address.address.toLowerCase());
    this.memoryLimit(`email-address:${fingerprint}`, 3, 3600, now, 'e-mails to this address are limited: try again later');
    // Sol (identity review): one daily send cap for every flow mode (link and restore had 300 each)
    await this.chargeOAuth('email', null, flow.mode === 'restore' ? { install: flow.install } : { install: flow.install, pid: flow.pid }, { global: 'all' });
    const token = base64Url(this.lobby.env.randomBytes(32));
    const tokenHash = await this.hash('email-token', token);
    await this.transaction((tx) => {
      const raw = tx.get(`oauth2:email:${flow.ticket}`);
      if (raw === null) fail(400, 'email_expired', 'this page has expired: start again in the launcher');
      const current = emailFlowValue(raw);
      if (current.state === 'done' || current.sends >= EMAIL_SENDS_PER_FLOW) fail(400, 'email_expired', 'this page has expired: start again in the launcher');
      if (current.token_hash) tx.delete(`oauth2:email-token:${current.token_hash}`);
      tx.put(`oauth2:email:${flow.ticket}`, { ...current, state: 'sent', sends: current.sends + 1, token_hash: tokenHash, token_expires: now + EMAIL_LINK_SECONDS, fingerprint });
      tx.put(`oauth2:email-token:${tokenHash}`, flow.ticket);
    });
    const message = magicLinkMessage({ link: `${this.lobby.config.publicUrl}/email/verify/${token}`, minutes: EMAIL_LINK_SECONDS / 60 });
    let sent = false;
    try {
      sent = await sendMail(this.lobby.env, { url: config.mailerUrl, key: config.mailerKey }, { to: address.address, ...message });
    } catch (error) {
      if (!(error instanceof EmailProviderFailure)) throw error;
    }
    if (!sent) fail(502, 'email_send_failed', 'the e-mail could not be sent: try again in a few minutes');
    return { v: VERSION, sent: true };
  }

  async emailVerify(body, { ip = '' } = {}) {
    await this.chargeOAuth('finish', ip, null, { global: null });
    exact(body, ['v', 'token']);
    this.emailConfig();
    if (typeof body.token !== 'string' || !EMAIL_TOKEN.test(body.token)) invalid('token is malformed');
    const tokenHash = await this.hash('email-token', body.token);
    const now = time(this.lobby);
    const expired = () => fail(400, 'email_expired', 'this link has expired or was used: start again in the launcher');
    const flow = await this.transaction((tx) => {
      const ticket = tx.get(`oauth2:email-token:${tokenHash}`);
      if (ticket === null) return null;
      const raw = tx.get(`oauth2:email:${ticket}`);
      return raw === null ? null : emailFlowValue(raw);
    });
    if (!flow || flow.state !== 'sent' || flow.token_hash !== tokenHash || !(flow.token_expires > now) || flow.expires <= now) expired();
    await this.chargeOAuth('finish', null, null, { global: flow.mode === 'restore' ? 'restore' : 'link' });
    const accountId = `guest:${hex(this.lobby.env.randomBytes(8))}`;
    const result = await this.transaction((tx) => {
      const clock = oauthClock(tx, true);
      const raw = tx.get(`oauth2:email:${flow.ticket}`);
      if (raw === null) expired();
      const current = emailFlowValue(raw);
      if (current.state !== 'sent' || current.token_hash !== tokenHash || !(current.token_expires > now)) expired();
      const fingerprint = current.fingerprint;
      if (typeof fingerprint !== 'string' || !OAUTH_HEX.test(fingerprint)) throw new SocialStorageError('unavailable');
      const linkedPid = oauthSubjectPid(tx, 'email', fingerprint, clock);
      if (current.mode !== 'confirm' && this.identityBanned(tx, 'email', fingerprint)) fail(403, 'identity_banned', 'this e-mail address belongs to a banned player');
      let outcome;
      if (current.mode === 'restore') {
        if (!linkedPid) fail(404, 'oauth_not_linked', 'no player uses this e-mail address');
        const targetMeta = oauthMetaFor(tx, linkedPid, clock);
        if (!targetMeta) throw new SocialStorageError('unavailable');
        if (targetMeta.revision > current.watermark) fail(409, 'oauth_stale', 'something changed meanwhile: start again in the launcher');
        const rebound = this.rebindPlayer(tx, linkedPid, { name: current.name, install: current.install, device: current.device },
          { revision: current.watermark }, now, { keepRecovery: true, via: 'email' });
        outcome = { pid: linkedPid, generation: rebound.generation, accountId };
      } else {
        const player = oauthPlayer(tx, current.pid);
        if (player.generation !== current.generation || player.install !== current.install) fail(401, 'session_revoked', 'this session has been revoked');
        const meta = oauthMetaFor(tx, current.pid, clock);
        if (!meta || meta.version !== current.version) fail(409, 'oauth_stale', 'something changed meanwhile: start again in the launcher');
        if (current.mode === 'confirm') {
          if (meta.providers.email !== fingerprint || linkedPid !== current.pid) fail(403, 'oauth_not_this_player', 'that is not the e-mail address linked to this player');
        } else {
          if (linkedPid && linkedPid !== current.pid) fail(409, 'email_in_use', 'this e-mail address belongs to another player');
          if (Object.hasOwn(meta.providers, 'email')) fail(409, 'oauth_provider_exists', 'an e-mail address is linked already');
          this.lobby.checkAccount({ install: player.install, device: player.device });
          const providers = { ...meta.providers, email: fingerprint };
          const revision = oauthNextClock(tx, clock);
          tx.put(`oauth2:subject:email:${fingerprint}`, current.pid);
          tx.put(`oauth2:player:${current.pid}`, { providers, version: meta.version + 1, revision });
        }
        outcome = { pid: current.pid };
      }
      tx.delete(`oauth2:email-token:${tokenHash}`);
      const { token_hash: _used, ...rest } = current;
      tx.put(`oauth2:email:${current.ticket}`, { ...rest, state: 'done', result: outcome });
      return { mode: current.mode, ...outcome, install: current.install, name: current.name, device: current.device, client: current.client };
    });
    if (result.mode === 'restore') {
      this.finishRebind(result.pid);
      await this.lobby.recordSighting(result.install, result.name, result.device ?? null, result.client ?? null);
    }
    return { v: VERSION, done: true };
  }

  async emailPoll(body, { ip = '' } = {}) {
    // Sol (identity review): the per-address token bucket comes first (bounded by the lobby's own cleanup); a flow's own budget is kept only for a
    // flow that exists and whose poll secret matched, so made-up tickets leave nothing behind
    this.lobby.limit(`email-poll:${ip}`, 40, 20);
    exact(body, ['v', 'ticket', 'poll']);
    if (typeof body.ticket !== 'string' || !OAUTH_TRANSACTION.test(body.ticket)) invalid('ticket must be 48 lowercase hex digits');
    if (typeof body.poll !== 'string' || !OAUTH_HEX.test(body.poll)) invalid('poll must be 64 lowercase hex digits');
    const now = time(this.lobby);
    const pollHash = hex(await this.lobby.env.sha256(encoder.encode(body.poll)));
    const flow = await this.emailFlow(body.ticket);
    if (!flow || flow.expires <= now) fail(404, 'email_expired', 'this sign-in has expired: start again');
    let diff = 0;
    for (let i = 0; i < 64; i++) diff |= pollHash.charCodeAt(i) ^ flow.poll_hash.charCodeAt(i);
    if (diff) fail(404, 'email_expired', 'this sign-in has expired: start again');
    this.memoryLimit(`email-poll:${body.ticket}`, 40, 60, now, 'poll at most every few seconds');
    if (flow.state !== 'done') return { v: VERSION, done: false, state: flow.state };
    const candidates = flow.mode === 'confirm' ? Array.from({ length: 8 }, () => randomRecovery(this.lobby.env.randomBytes)) : [];
    const hashes = await Promise.all(candidates.map((code) => this.hash('recovery', code)));
    const collected = await this.transaction((tx) => {
      const raw = tx.get(`oauth2:email:${flow.ticket}`);
      if (raw === null) fail(404, 'email_expired', 'this sign-in has expired: start again');
      const current = emailFlowValue(raw);
      if (current.state !== 'done' || current.poll_hash !== flow.poll_hash) fail(404, 'email_expired', 'this sign-in has expired: start again');
      emailDeleteFlow(tx, current);
      const clock = oauthClock(tx, true);
      const meta = oauthMetaFor(tx, current.result.pid, clock);
      const providers = meta ? Object.keys(meta.providers).sort() : [];
      if (current.mode === 'confirm') {
        const player = oauthPlayer(tx, current.result.pid);
        if (player.generation !== current.generation) fail(401, 'session_revoked', 'this session has been revoked');
        return { mode: 'confirm', pid: current.result.pid, providers, code: this.storeRecoveryCode(tx, player, candidates, hashes, now) };
      }
      return { mode: current.mode, providers, current };
    });
    if (collected.mode === 'confirm') return { v: VERSION, done: true, pid: collected.pid, providers: collected.providers, code: collected.code };
    if (collected.mode === 'link') return { v: VERSION, done: true, pid: collected.current.pid, providers: collected.providers };
    const current = collected.current;
    const account = {
      id: current.result.accountId, name: current.name, kind: 'guest', install: current.install,
      ...(current.device === null ? {} : { device: current.device }), ...(current.client ? { client: current.client } : {}),
      pid: current.result.pid, generation: current.result.generation,
    };
    this.lobby.seen(account.id);
    const answer = {
      v: VERSION, done: true, token: await this.lobby.makeToken(account),
      account: { id: account.id, name: account.name, kind: 'guest', pid: account.pid }, providers: collected.providers,
    };
    const notice = this.lobby.getEnforcement(current.install)?.notice;
    if (notice) answer.notice = { ...notice };
    return answer;
  }

  async oauthAccounts(authorization) {
    const account = await this.lobby.account(authorization);
    await this.current(account);
    this.lobby.limit(`oauth-accounts:${account.id}`, 60, 60);
    const providers = await this.transaction((tx) => {
      oauthSessionPlayer(tx, account);
      // Sol r4: reading never initializes the metadata (that was two writes per fresh guest, outside OAuth's daily caps)
      const meta = oauthMetaFor(tx, account.pid, oauthClock(tx), false);
      return meta ? Object.keys(meta.providers).sort() : [];
    });
    return { v: VERSION, pid: account.pid, providers };
  }

  async oauthUnlink(body, { authorization, hasAuthorization = false, ip = '', charged = false } = {}) {
    if (!charged) await this.chargeOAuth('unlink', ip, null, { global: null });
    if (!hasAuthorization) fail(401, 'signed_out', 'sign in first');
    const account = await this.lobby.account(authorization);
    await this.current(account);
    await this.chargeOAuth('unlink', null, account, { global: null });
    exact(body, ['v', 'provider', 'install', 'recovery']);
    if (typeof body.provider !== 'string' || !IDENTITY_KINDS.includes(body.provider)) fail(400, 'unknown_provider', 'that account type is not supported');
    const provider = body.provider; // ONLINEMENU-1: Discord or the e-mail address
    // ONLINEMENU-1 (finding 2): unlinking needs the install and the recovery code (with a session alone, its holder could unlink the owner's
    // account, link their own and restore the player with it)
    this.lobby.proveInstall(account, body.install);
    const recoveryHash = await this.recoveryProofHash(body.recovery);
    await this.proveRecovery(account.pid, recoveryHash);
    await this.chargeOAuth('unlink', null, null); // Sol r2: the global budget after validation and the proofs
    const providers = await this.transaction((tx) => {
      const clock = oauthClock(tx, true);
      const player = oauthSessionPlayer(tx, account);
      this.requireRecoveryProof(player, recoveryHash);
      const meta = oauthMetaFor(tx, account.pid, clock);
      if (!meta || !Object.hasOwn(meta.providers, provider)) fail(404, 'oauth_not_linked', 'that OAuth provider is not linked');
      const hash = meta.providers[provider];
      if (tx.get(`oauth2:subject:${provider}:${hash}`) !== account.pid) throw new SocialStorageError('unavailable');
      tx.delete(`oauth2:subject:${provider}:${hash}`);
      const linked = { ...meta.providers };
      delete linked[provider];
      const revision = oauthNextClock(tx, clock);
      tx.put(`oauth2:player:${account.pid}`, { providers: linked, version: meta.version + 1, revision });
      return Object.keys(linked).sort();
    });
    return { v: VERSION, pid: account.pid, providers };
  }

  async oauthFinish(body, { authorization, hasAuthorization = false, ip = '', signal = null, charged = false } = {}) {
    if (!charged) await this.chargeOAuth('finish', ip, null, { global: null });
    exact(body, ['v', 'transaction', 'state', 'code', 'code_verifier']);
    const transaction = oauthTransaction(body.transaction);
    const state = oauthState(body.state);
    const code = oauthCode(body.code);
    const verifier = oauthVerifier(body.code_verifier);
    const pending = await this.transaction((tx) => {
      const raw = tx.get(`oauth2:pending:${transaction}`);
      if (raw === null) return null;
      const value = oauthPending(raw, oauthClock(tx));
      if (tx.get(`oauth2:pending-install:${value.install}`) !== value.transaction ||
          tx.get(oauthExpiryKey(value.expires, `pending:${value.transaction}`)) !== `oauth2:pending:${value.transaction}`) {
        throw new SocialStorageError('unavailable');
      }
      const now = time(this.lobby);
      oauthSafe(now);
      if (value.expires <= now) {
        oauthDeletePending(tx, value);
        return null;
      }
      if (value.state !== state) fail(400, 'oauth_state', 'the OAuth state is invalid');
      return value;
    });
    if (!pending) fail(400, 'oauth_expired', 'the OAuth transaction is unavailable');
    const challenge = await pkceChallenge(this.lobby.env, verifier);
    if (challenge !== pending.challenge) fail(400, 'oauth_verifier', 'the OAuth verifier is invalid');
    let account = null;
    if (pending.mode === 'restore') {
      if (hasAuthorization) fail(401, 'signed_out', 'restore does not accept an authorization header');
    } else {
      if (!hasAuthorization) fail(401, 'signed_out', 'sign in first');
      account = await this.lobby.account(authorization);
      await this.current(account);
      if (account.id !== pending.accountId || account.pid !== pending.pid ||
          account.generation !== pending.generation || account.install !== pending.install) {
        fail(401, 'session_revoked', 'this session has been revoked');
      }
    }
    await this.chargeOAuth('finish', null, account ?? pending, { global: pending.mode === 'restore' ? 'restore' : 'link' });
    const consumed = await this.transaction((tx) => {
      const raw = tx.get(`oauth2:pending:${transaction}`);
      if (raw === null) return null;
      const value = oauthPending(raw, oauthClock(tx));
      if (tx.get(`oauth2:pending-install:${value.install}`) !== value.transaction ||
          tx.get(oauthExpiryKey(value.expires, `pending:${value.transaction}`)) !== `oauth2:pending:${value.transaction}`) {
        throw new SocialStorageError('unavailable');
      }
      const now = time(this.lobby);
      oauthSafe(now);
      if (value.expires <= now || value.state !== state) return null;
      if (value.mode !== 'restore' &&
          (!account || account.id !== value.accountId || account.pid !== value.pid ||
           account.generation !== value.generation || account.install !== value.install)) {
        fail(401, 'session_revoked', 'this session has been revoked');
      }
      if (value.mode !== 'restore') oauthSessionPlayer(tx, account);
      oauthDeletePending(tx, value);
      return value;
    });
    if (!consumed) fail(400, 'oauth_expired', 'the OAuth transaction is unavailable');
    return { deferred: this.oauthFinishProvider(consumed, code, verifier, account, signal) };
  }

  async oauthFinishProvider(pending, code, verifier, account, signal) {
    let subject;
    try {
      subject = await exchangeProvider(this.lobby.env, this.lobby.config.oauth, pending.provider, code, verifier, signal);
    } catch (error) {
      if (error instanceof OAuthProviderFailure) throw new SocialError(502, 'oauth_provider_unavailable', 'the OAuth provider is unavailable');
      throw error;
    }
    const subjectHash = await this.hash(`oauth2:${pending.provider}`, subject);
    const candidates = pending.mode === 'confirm' ? Array.from({ length: 8 }, () => randomRecovery(this.lobby.env.randomBytes)) : [];
    const candidateHashes = await Promise.all(candidates.map((code) => this.hash('recovery', code)));
    const result = await this.transaction((tx) => {
      const clock = oauthClock(tx);
      const now = time(this.lobby);
      oauthSafe(now);
      if (pending.expires <= now) fail(400, 'oauth_expired', 'the OAuth transaction is unavailable');
      const linkedPid = oauthSubjectPid(tx, pending.provider, subjectHash, clock);
      if (pending.mode !== 'confirm' && this.identityBanned(tx, pending.provider, subjectHash)) {
        fail(403, 'identity_banned', 'that account belongs to a banned player');
      }
      if (pending.mode === 'confirm') {
        // ONLINEMENU-1 (Sol r2): the player's own linked account, signed in again just now: a new recovery code (the old one, if any, ends)
        const player = oauthSessionPlayer(tx, account);
        if (account.id !== pending.accountId || account.pid !== pending.pid ||
            player.generation !== pending.generation || player.install !== pending.install) {
          fail(401, 'session_revoked', 'this session has been revoked');
        }
        const meta = oauthMetaFor(tx, pending.pid, clock);
        if (!meta || meta.version !== pending.version || meta.revision !== pending.revision) fail(409, 'oauth_stale', 'the OAuth operation is stale');
        if (linkedPid !== pending.pid || meta.providers[pending.provider] !== subjectHash) {
          fail(403, 'oauth_not_this_player', 'that account is not the one linked to this player');
        }
        this.lobby.checkAccount(account);
        const code = this.storeRecoveryCode(tx, player, candidates, candidateHashes, now);
        return { mode: 'confirm', pid: pending.pid, account: { ...account }, providers: Object.keys(meta.providers).sort(), code };
      }
      if (pending.mode === 'link') {
        if (linkedPid && linkedPid !== pending.pid) fail(409, 'oauth_conflict', 'that provider account is linked to another player');
        const player = oauthSessionPlayer(tx, account);
        if (account.id !== pending.accountId || account.pid !== pending.pid ||
            player.generation !== pending.generation || player.install !== pending.install) {
          fail(401, 'session_revoked', 'this session has been revoked');
        }
        const meta = oauthMetaFor(tx, pending.pid, clock);
        if (!meta || meta.version !== pending.version || meta.revision !== pending.revision) fail(409, 'oauth_stale', 'the OAuth operation is stale');
        this.lobby.checkAccount(account);
        this.lobby.checkAccount({ install: player.install, device: player.device });
        const old = meta.providers[pending.provider];
        if (old && old !== subjectHash) fail(409, 'oauth_provider_exists', 'a provider is already linked');
        const providers = { ...meta.providers, [pending.provider]: subjectHash };
        const revision = oauthNextClock(tx, clock);
        tx.put(`oauth2:subject:${pending.provider}:${subjectHash}`, pending.pid);
        if (old && old !== subjectHash) tx.delete(`oauth2:subject:${pending.provider}:${old}`);
        tx.put(`oauth2:player:${pending.pid}`, { providers, version: meta.version + 1, revision });
        return { mode: 'link', pid: pending.pid, account: { ...account }, providers: Object.keys(providers).sort() };
      }
      if (!linkedPid) fail(404, 'oauth_not_linked', 'that provider account is not linked');
      const target = tx.get(`player:${linkedPid}`);
      if (!target) throw new SocialStorageError('unavailable');
      validatePlayer(target, linkedPid);
      const targetMeta = oauthMetaFor(tx, linkedPid, clock);
      if (!targetMeta) throw new SocialStorageError('unavailable');
      if (targetMeta.revision > pending.watermark) fail(409, 'oauth_stale', 'the OAuth operation is stale');
      const limits = OAUTH_QUOTA.finish;
      const retry = oauthQuotaRetry(tx, 'finish', 'pid', linkedPid, now, limits.pid, limits.window);
      if (retry) fail(429, 'rate_limited', 'OAuth attempts are limited', { retry_after: retry });
      const rebound = this.rebindPlayer(tx, linkedPid, {
        name: pending.name,
        install: pending.install,
        device: pending.device,
      }, { revision: pending.watermark }, now, { keepRecovery: true, via: pending.provider });
      const reboundMeta = oauthMetaFor(tx, linkedPid, oauthClock(tx));
      const newAccount = {
        id: `guest:${hex(this.lobby.env.randomBytes(8))}`,
        name: pending.name,
        kind: 'guest',
        install: pending.install,
        ...(pending.device === null ? {} : { device: pending.device }),
        ...(pending.client ? { client: pending.client } : {}),
        pid: linkedPid,
        generation: rebound.generation,
      };
      return { mode: 'restore', pid: linkedPid, account: newAccount, providers: Object.keys(reboundMeta.providers).sort() };
    });
    if (result.mode === 'restore') {
      this.finishRebind(result.pid);
      await this.lobby.recordSighting(result.account.install, result.account.name, result.account.device ?? null, result.account.client ?? null);
    }
    this.lobby.seen(result.account.id);
    if (result.mode === 'confirm') return { v: VERSION, pid: result.pid, providers: result.providers, code: result.code };
    const answer = {
      v: VERSION,
      token: await this.lobby.makeToken(result.account),
      account: { id: result.account.id, name: result.account.name, kind: 'guest', pid: result.pid },
      providers: result.providers,
    };
    const notice = result.mode === 'restore' ? this.lobby.enforcement.get(result.account.install)?.notice : null;
    if (notice) answer.notice = { ...notice };
    return answer;
  }

  // ONLINEMENU-1 (finding 2): the recovery code as proof (unlinking, a second provider): its hash, made before the transaction
  async recoveryProofHash(code) {
    if (code === undefined) return null;
    if (typeof code !== 'string' || !/^[0-9A-HJKMNP-TV-Z]{16}$/.test(code)) invalid('recovery must be 16 uppercase Crockford characters');
    return this.hash('recovery', code);
  }

  // the recovery code as proof: the player must have a code and this must be it; 10 tries an hour per player, counted in memory (Sol r2: a wrong
  // try must count; Sol r4: and must not write, or wrong tries become storage work outside OAuth's daily caps). Read-only
  async proveRecovery(pidValue, recoveryHash) {
    const now = time(this.lobby);
    const key = `recovery-proof:${pidValue}`;
    const hour = Math.floor(now / 3600);
    const tries = this.oauthAddressCounts.get(key);
    if (tries && tries.bucket === hour && tries.count >= 10) fail(429, 'rate_limited', 'recovery code attempts are limited', { retry_after: Math.max(1, (hour + 1) * 3600 - now) });
    const outcome = await this.transaction((tx) => {
      const player = tx.get(`player:${pidValue}`);
      if (!player) throw new SocialStorageError('unavailable');
      if (!player.recovery_hash) return { error: new SocialError(403, 'recovery_required', 'make a recovery code first: this change needs it') };
      if (recoveryHash === null) return { error: new SocialError(403, 'recovery_required', 'this change needs the player\'s recovery code') };
      if (player.recovery_hash !== recoveryHash || tx.get(`recovery:${recoveryHash}`) !== pidValue) {
        return { error: new SocialError(403, 'bad_recovery', 'the recovery code is not this player\'s'), counted: true };
      }
      return { counted: true };
    });
    if (outcome.counted) this.oauthAddressCounts.set(key, tries && tries.bucket === hour ? { bucket: hour, count: tries.count + 1 } : { bucket: hour, count: 1 });
    if (outcome.error) throw outcome.error;
  }

  // inside the caller's transaction, after proveRecovery: the code is still the player's (nothing is charged here)
  requireRecoveryProof(player, recoveryHash) {
    if (!player.recovery_hash || recoveryHash === null) fail(403, 'recovery_required', 'this change needs the player\'s recovery code');
    if (player.recovery_hash !== recoveryHash) fail(403, 'bad_recovery', 'the recovery code is not this player\'s');
  }

  // a new recovery code for the player (the old one, if any, ends), inside the caller's transaction
  storeRecoveryCode(tx, stored, candidates, hashes, now) {
    let selected = -1;
    for (let i = 0; i < hashes.length; i++) if (!tx.get(`recovery:${hashes[i]}`)) { selected = i; break; }
    if (selected < 0) throw new Error('social recovery collision retry limit reached');
    if (stored.recovery_hash) tx.delete(`recovery:${stored.recovery_hash}`);
    tx.put(`recovery:${hashes[selected]}`, stored.pid);
    tx.put(`player:${stored.pid}`, { ...stored, recovery_hash: hashes[selected], updated_at: now });
    return candidates[selected];
  }

  // ONLINEMENU-1 (Sol r2, r3): the player a "moved" sign-in made on an install whose player was restored elsewhere (`after_move`) gives the
  // install up to a restore of the original player there, while it has no code, friends, requests, chats or linked accounts. It is retired in
  // place, not deleted (other players' blocks and recent games may name it): bound to a fresh random install no one holds. Returns the pid still
  // bound to the install (null when free)
  releaseMovedPlaceholder(tx, install, keepPid) {
    const pidValue = tx.get(`install:${install}`);
    if (pidValue === null || pidValue === keepPid) return pidValue;
    const player = tx.get(`player:${pidValue}`);
    if (!player?.after_move || player.recovery_hash) return pidValue;
    for (const prefix of [`friend:${pidValue}:`, `request-in:${pidValue}:`, `request-out:${pidValue}:`, `thread:${pidValue}:`]) {
      if (tx.list(prefix, { limit: 1 }).size) return pidValue;
    }
    const meta = tx.get(`oauth2:player:${pidValue}`);
    if (meta && Object.keys(meta.providers ?? {}).length) return pidValue;
    let retired = null;
    for (let attempt = 0; attempt < 8 && retired === null; attempt++) {
      const candidate = hex(this.lobby.env.randomBytes(16));
      if (INSTALL.test(candidate) && tx.get(`install:${candidate}`) === null) retired = candidate;
    }
    if (retired === null) throw new SocialStorageError('unavailable');
    tx.delete(`install:${install}`);
    tx.put(`install:${retired}`, pidValue);
    tx.put(`player:${pidValue}`, { ...player, install: retired, device: null, generation: player.generation + 1, hidden: true, retired: true, updated_at: time(this.lobby) });
    return null;
  }

  // ONLINEMENU-1 (finding 2): `keepRecovery` (an OAuth restore) keeps the player's recovery code, so the owner can still take the player back
  // with it; a recovery-code restore spends the code as before. Either way the install the player leaves is told when it next signs in
  // (`moved:<install>`, signIn's `moved`), so a move the owner did not make does not pass unseen
  rebindPlayer(tx, pidValue, target, expected = {}, now = time(this.lobby), { keepRecovery = false, via } = {}) {
    const player = oauthPlayer(tx, pidValue);
    if (typeof target?.name !== 'string' || !target.name.length || Array.from(target.name).length > 24 ||
        /[\u0000-\u001f\u007f]/u.test(target.name) || !target.name.isWellFormed() || target.name !== target.name.trim() ||
        typeof target.install !== 'string' || !INSTALL.test(target.install) ||
        (target.device !== null && (typeof target.device !== 'string' || !DEVICE.test(target.device)))) throw new SocialStorageError('unavailable');
    if (!Number.isSafeInteger(now) || now < player.created_at) throw new SocialStorageError('unavailable');
    if (expected.generation !== undefined && player.generation !== expected.generation) fail(409, 'oauth_stale', 'the identity operation is stale');
    const installPid = this.releaseMovedPlaceholder(tx, target.install, pidValue);
    if (installPid !== null && (typeof installPid !== 'string' || !PID.test(installPid))) throw new SocialStorageError('unavailable');
    if (installPid !== null && installPid !== pidValue) fail(409, 'install_in_use', 'this install identity is already in use');
    this.lobby.checkAccount({ install: player.install, device: player.device });
    this.lobby.checkAccount({ install: target.install, device: target.device });
    const generation = player.generation + 1;
    if (!Number.isSafeInteger(generation)) throw new SocialStorageError('unavailable');
    if (player.recovery_hash && tx.get(`recovery:${player.recovery_hash}`) !== pidValue) throw new SocialStorageError('unavailable');
    const clock = oauthClock(tx, true);
    const meta = oauthMetaFor(tx, pidValue, clock, true);
    if ((expected.version !== undefined && meta.version !== expected.version) ||
        (expected.revision !== undefined && meta.revision > expected.revision)) fail(409, 'oauth_stale', 'the identity operation is stale');
    for (const [provider, hash] of Object.entries(meta.providers)) {
      if (tx.get(`oauth2:subject:${provider}:${hash}`) !== pidValue) throw new SocialStorageError('unavailable');
    }
    const revision = oauthNextClock(tx, clock);
    const next = { ...player, oauth2: 1, install: target.install, device: target.device, nick: target.name, generation, updated_at: now };
    if (!keepRecovery) {
      delete next.recovery_hash;
      if (player.recovery_hash) tx.delete(`recovery:${player.recovery_hash}`);
    }
    if (player.install !== target.install) {
      tx.delete(`install:${player.install}`);
      if (via !== 'recovery' && !IDENTITY_KINDS.includes(via)) throw new SocialStorageError('unavailable');
      setExpiring(tx, `moved:${player.install}`, { at: now, via }, now + MOVED_NOTICE_SECONDS);
    }
    tx.put(`install:${target.install}`, pidValue);
    tx.put(`player:${pidValue}`, next);
    tx.put(`oauth2:player:${pidValue}`, { ...meta, revision });
    return next;
  }

  finishRebind(pidValue) {
    for (const [accountId, handoff] of this.challengeHandoffs) {
      if (handoff.pid === pidValue) this.challengeHandoffs.delete(accountId);
    }
    this.active.delete(pidValue);
    // the restored player's inbox starts over under its own epoch (a client holding an older seq resets)
    if (!this.inboxEpochOverrides.has(pidValue) && this.inboxEpochOverrides.size >= MAX_INBOX_CACHES) this.rotateInboxEpoch();
    this.inboxEpochOverrides.set(pidValue, hex(this.lobby.env.randomBytes(8)));
    this.invalidateInboxOwner(pidValue);
    for (const [id, invite] of this.pendingInvites) {
      if (invite.from === pidValue || invite.to === pidValue) {
        this.pendingInvites.delete(id);
        this.wakeInbox(invite.from);
        this.wakeInbox(invite.to);
      }
    }
    for (const game of this.lobby.games.values()) {
      if (game.state === 'started' && game.slots.some((slot) => slot.pid === pidValue && slot.state === 'player')) this.leftStartedGames.add(`${game.id}:${pidValue}`);
    }
    addNotice(this, pidValue, { kind: 'recovered', invite: null, at: time(this.lobby) });
    this.wakeInbox(pidValue);
    void this.wakeFriends(pidValue);
  }

  async dispatch(account, method, path, query, body, ip, options) {
    const tail = path.slice(2);
    if (path[1] === 'me' && tail.length === 1 && tail[0] === 'privacy' && method === 'PUT') {
      readQuery(query, []);
      return this.privacy(account, body);
    }
    if (path[1] === 'me' && tail.length === 1 && tail[0] === 'status' && method === 'POST') {
      readQuery(query, []);
      return this.status(account, body);
    }
    if (path[1] === 'me' && tail.length === 1 && tail[0] === 'recovery' && method === 'POST') {
      if (query.size) fail(400, 'bad_query', 'this route does not accept query parameters');
      if (!body || typeof body !== 'object' || Array.isArray(body) ||
          ![2, 3].includes(Object.keys(body).length) || body.v !== VERSION || typeof body.install !== 'string') {
        await this.chargeRecoveryAttempt(account, ip);
      }
      exact(body, ['v', 'install'], ['recovery']);
      this.lobby.proveInstall(account, body.install); // ONLINEMENU-1 (finding 2): a session alone cannot make a recovery code
      return this.recovery(account, ip, await this.recoveryProofHash(body.recovery));
    }
    if (path[1] === 'me' && tail.length === 0 && method === 'GET') {
      readQuery(query, []);
      return this.me(account);
    }
    if (tail.length === 0 && path[1] === 'friends' && method === 'GET') {
      readQuery(query, []);
      return this.friends(account);
    }
    if (path[1] === 'friends' && tail.length === 1 && method === 'DELETE') {
      readQuery(query, []);
      return this.removeFriend(account, tail[0]);
    }
    if (path[1] === 'friends' && tail.length === 1 && tail[0] === 'requests' && method === 'POST') {
      readQuery(query, []);
      return this.request(account, body, ip);
    }
    if (path[1] === 'friends' && tail.length === 3 && tail[0] === 'requests' && method === 'POST') {
      readQuery(query, []);
      if (tail[2] === 'accept') {
        exact(body, ['v']);
        return this.acceptRequest(account, tail[1]);
      }
      if (tail[2] === 'decline') {
        exact(body, ['v']);
        return this.declineRequest(account, tail[1]);
      }
    }
    if (path[1] === 'blocks' && tail.length === 0 && method === 'GET') {
      const q = readQuery(query, ['after', 'limit']);
      return this.blocks(account, q.after ?? null, q.limit ?? null);
    }
    if (path[1] === 'blocks' && tail.length === 0 && method === 'POST') {
      readQuery(query, []);
      return this.block(account, body);
    }
    if (path[1] === 'blocks' && tail.length === 1 && method === 'DELETE') {
      readQuery(query, []);
      return this.unblock(account, tail[0]);
    }
    if (path[1] === 'recent' && tail.length === 0 && method === 'GET') {
      readQuery(query, []);
      return this.recent(account);
    }
    if (path[1] === 'invites' && tail.length === 0 && method === 'POST') {
      readQuery(query, []);
      return this.createInvite(account, body, ip);
    }
    if (path[1] === 'invites' && tail.length === 2 && method === 'POST') {
      readQuery(query, []);
      if (tail[1] === 'accept') return this.acceptInvite(account, tail[0], body);
      if (tail[1] === 'decline') return this.declineInvite(account, tail[0], body);
    }
    if (path[1] === 'inbox' && tail.length === 0 && method === 'GET') {
      const q = readQuery(query, ['since', 'wait']);
      return this.inbox(account, q.since ?? null, q.wait ?? null, options);
    }
    if (path[1] === 'chat' && tail.length === 0 && method === 'GET') {
      const q = readQuery(query, ['after', 'limit']);
      return this.threads(account, q.after ?? null, q.limit ?? null);
    }
    if (path[1] === 'chat' && tail.length === 2 && tail[1] === 'messages' && method === 'POST') {
      readQuery(query, []);
      return this.sendMessage(account, tail[0], body, ip);
    }
    if (path[1] === 'chat' && tail.length === 2 && tail[1] === 'messages' && method === 'GET') {
      const q = readQuery(query, ['after', 'limit']);
      return this.messages(account, tail[0], q.after ?? null, q.limit ?? null);
    }
    if (path[1] === 'chat' && tail.length === 2 && tail[1] === 'read' && method === 'POST') {
      readQuery(query, []);
      return this.markRead(account, tail[0], body);
    }
    fail(404, 'not_found', 'unknown social path');
  }

  async inbox(account, since, waitText, options) {
    if (since !== null && (typeof since !== 'string' || since.length > 80 || !/^[0-9a-f]{16}:\d{1,16}$/.test(since) ||
        !Number.isSafeInteger(Number(since.split(':')[1])))) {
      fail(400, 'bad_query', 'since is an invalid inbox sequence');
    }
    const wait = waitText === null ? 0 : parseBoundedLimit(waitText, 0, 25, 'wait');
    let snapshot = await this.inboxSnapshot(account);
    const matches = since === snapshot.seq;
    if (wait === 0 || !matches) return { ...snapshot, reset: since === null || since.split(':')[0] !== snapshot.seq.split(':')[0] || (Number(since?.split(':')[1]) > Number(snapshot.seq.split(':')[1])) };
    return {
      deferred: (async () => {
        const deadline = this.lobby.env.now() + wait * 1000;
        while (true) {
          const remaining = deadline - this.lobby.env.now();
          if (remaining <= 0) return { ...snapshot, reset: false };
          this.inboxPidHolds.set(account.pid, (this.inboxPidHolds.get(account.pid) ?? 0) + 1);
          const held = this.lobby.holdPoll(`inbox:${account.pid}`, remaining, account.id, account.pid, options?.signal);
          if (!held) {
            this.releaseInboxHold(account.pid);
            return { ...snapshot, reset: false, backoff: 10 };
          }
          if (this.inboxSnapshots.get(account.pid)?.snapshot.seq !== snapshot.seq) this.wakeInbox(account.pid);
          try {
            await held;
          } finally {
            this.releaseInboxHold(account.pid);
          }
          const freshAccount = await this.lobby.account(options.authorization, { activity: false, pollCache: true });
          const next = await this.inboxSnapshot(freshAccount);
          if (next.seq !== snapshot.seq) return { ...next, reset: next.seq.split(':')[0] !== snapshot.seq.split(':')[0] };
          snapshot = next;
        }
      })(),
    };
  }

  releaseInboxHold(pidValue) {
    const count = this.inboxPidHolds.get(pidValue) ?? 0;
    if (count <= 1) this.inboxPidHolds.delete(pidValue);
    else this.inboxPidHolds.set(pidValue, count - 1);
  }

  async inboxSnapshot(account) {
    for (let attempt = 0; attempt < 3; attempt++) {
      this.ensureInboxRevision(account.pid);
      const cached = this.inboxSnapshots.get(account.pid);
      if (cached) {
        if (!this.refreshInboxSnapshot(cached)) this.invalidateInboxOwner(account.pid);
        else return structuredClone(cached.snapshot);
      }
      const stamp = {
        generation: this.inboxBuildGeneration,
        revision: this.inboxRevisions.get(account.pid) ?? 0,
        epoch: this.inboxEpoch,
      };
      try {
        const entry = await this.buildInboxSnapshot(account, stamp);
        if (!this.inboxBuildIsCurrent(account.pid, stamp.generation, stamp.revision, stamp.epoch)) continue;
        entry.snapshot.seq = this.inboxSeq(account.pid, stamp.epoch, stamp.revision);
        if (!this.storeInboxSnapshot(account.pid, entry, stamp.epoch)) continue;
        return structuredClone(entry.snapshot);
      } catch (error) {
        if (error === CACHE_INVALIDATED) continue;
        throw error;
      }
    }
    fail(503, 'social_overloaded', 'the lobby is refreshing social data; retry shortly');
  }

  async buildInboxSnapshot(account, stamp) {
    const assertCurrent = () => {
      if (!this.inboxBuildIsCurrent(account.pid, stamp.generation, stamp.revision, stamp.epoch)) throw CACHE_INVALIDATED;
    };
    const self = await this.current(account);
    assertCurrent();
    const viewerSecurity = this.lobby.accountSecurity(account);
    this.lobby.checkAccount(account, { security: viewerSecurity });
    assertCurrent();
    const data = await this.transaction((tx) => {
      const friendRows = tx.list(`friend:${self.pid}:`, { limit: 201 });
      if (friendRows.size > 200) throw new SocialStorageError('unavailable');
      for (const [key, edge] of friendRows) {
        const peer = key.slice(`friend:${self.pid}:`.length);
        if (!PID.test(peer)) throw new SocialStorageError('unavailable');
        validateFriendEdge(edge);
        const reverse = tx.get(`friend:${peer}:${self.pid}`);
        if (!reverse || validateFriendEdge(reverse).since !== edge.since) throw new SocialStorageError('unavailable');
      }
      const incoming = tx.list(`request-in:${self.pid}:`, { limit: 51 });
      const outgoing = tx.list(`request-out:${self.pid}:`, { limit: 201 });
      if (incoming.size > 50 || outgoing.size > 200) throw new SocialStorageError('unavailable');
      const requestKeys = [
        ...incoming.keys(),
        ...outgoing.keys(),
      ];
      const requestIds = [...new Set(requestKeys.map((key) => key.slice(key.lastIndexOf(':') + 1)))];
      const requests = requestIds.map((id) => {
        const request = validateRequest(tx.get(`request:${id}`), id);
        if ((request.to === self.pid) !== incoming.has(`request-in:${self.pid}:${id}`) ||
            (request.from === self.pid) !== outgoing.has(`request-out:${self.pid}:${id}`) ||
            !tx.get(`request-pair:${pairKey(request.from, request.to)}:${id}`)) throw new SocialStorageError('unavailable');
        return request;
      });
      const friends = [...friendRows].map(([key, edge]) => ({
        pid: key.slice(`friend:${self.pid}:`.length),
        since: validateFriendEdge(edge).since,
      }));
      const threads = friends.flatMap(({ pid: peerPid }) => {
        const value = tx.get(`thread:${self.pid}:${peerPid}`);
        if (!value) return [];
        const thread = validateThread(value);
        const pair = sortedPair(self.pid, peerPid);
        const conversation = tx.get(`conversation:${pair.join(':')}`);
        if (!conversation || validateConversation(tx, pair, conversation) !== thread.last_id ||
            validateReadState(tx, self.pid, peerPid, thread.last_id, value) !== thread.read_id) {
          throw new SocialStorageError('unavailable');
        }
        return [[`thread:${self.pid}:${peerPid}`, thread]];
      });
      return { friends, requests, threads };
    });
    assertCurrent();
    const profiles = new Map([[self.pid, self]]);
    const getProfile = async (pidValue) => {
      if (!profiles.has(pidValue)) {
        const player = await this.player(pidValue);
        assertCurrent();
        if (!player) throw new SocialError(503, 'social_unavailable', 'social storage is unavailable');
        profiles.set(pidValue, validatePlayer(player, pidValue));
      }
      return profiles.get(pidValue);
    };
    const friends = [];
    const presenceInfo = new Map();
    for (const friend of data.friends) {
      const profile = await getProfile(friend.pid);
      const info = await this.presenceDetails(self, profile, viewerSecurity);
      assertCurrent();
      presenceInfo.set(friend.pid, info);
      friends.push({ pid: friend.pid, nick: profile.nick, code: profile.code, since: friend.since, presence: info.presence });
    }
    friends.sort((a, b) => a.pid.localeCompare(b.pid));
    const requests = [];
    for (const request of data.requests) {
      const from = await getProfile(request.from);
      const to = await getProfile(request.to);
      requests.push({ id: request.id, from: this.publicPlayer(from), to: this.publicPlayer(to), at: request.at });
    }
    requests.sort((a, b) => a.at - b.at || a.id.localeCompare(b.id));
    const inviteRecords = [];
    for (const invite of this.pendingInvites.values()) {
      if (invite.from !== self.pid && invite.to !== self.pid) continue;
      if (invite.expires <= time(this.lobby)) continue;
      inviteRecords.push(invite);
    }
    const inviteSnapshots = new Map();
    const invites = [];
    for (const invite of inviteRecords) {
      const from = await getProfile(invite.from);
      const to = await getProfile(invite.to);
      const value = { id: invite.id, from: this.publicPlayer(from), to: this.publicPlayer(to), game: invite.game,
        kind: invite.kind, mode: invite.mode, note: invite.note, expires: invite.expires };
      inviteSnapshots.set(invite.id, value);
      invites.push(value);
    }
    invites.sort((a, b) => a.expires - b.expires || a.id.localeCompare(b.id));
    const chat = data.threads.map(([key, value]) => ({
      pid: key.slice(`thread:${self.pid}:`.length),
      last_id: value.last_id,
      read_id: value.read_id,
    })).sort((a, b) => a.pid.localeCompare(b.pid));
    const notices = (this.notices.get(self.pid) ?? []).map((notice) => ({ ...notice }));
    const stats = this.lobby.stats({ fresh: true });
    assertCurrent();
    const snapshot = { v: VERSION, seq: this.inboxSeq(self.pid, stamp.epoch, stamp.revision),
      presence: friends.map((friend) => friend.presence), requests, invites, notices, chat, stats, wait: 25 };
    const entry = {
      owner: self.pid,
      snapshot,
      friends,
      profiles,
      presenceInfo,
      inviteSnapshots,
      profileDeps: new Set(profiles.keys()),
      digest: this.inboxDigest(snapshot),
    };
    entry.bytes = this.inboxEntryBytes(entry);
    return entry;
  }

  inboxBuildIsCurrent(pidValue, generation, revision, epoch) {
    return this.inboxBuildGeneration === generation && this.inboxEpoch === epoch &&
      (this.inboxRevisions.get(pidValue) ?? 0) === revision;
  }

  ensureInboxRevision(pidValue) {
    if (!PID.test(pidValue ?? '')) fail(401, 'install_required', 'sign in with an install identity to use social features');
    if (!this.inboxRevisions.has(pidValue) && this.inboxRevisions.size >= MAX_INBOX_CACHES) this.rotateInboxEpoch();
  }

  inboxDigest(snapshot) {
    return serial({
      presence: snapshot.presence,
      requests: snapshot.requests,
      invites: snapshot.invites,
      notices: snapshot.notices,
      chat: snapshot.chat,
      stats: { v: snapshot.stats.v, players: snapshot.stats.players,
        games_open: snapshot.stats.games_open, games_running: snapshot.stats.games_running },
    });
  }

  inboxEntryBytes(entry) {
    return encoder.encode(serial({
      snapshot: entry.snapshot,
      friends: entry.friends,
      profiles: [...entry.profiles],
      presenceInfo: [...entry.presenceInfo],
      inviteSnapshots: [...entry.inviteSnapshots],
    })).length;
  }

  refreshInboxSnapshot(entry) {
    const now = time(this.lobby);
    const presence = [];
    const friends = [];
    for (const friend of entry.friends) {
      const info = entry.presenceInfo.get(friend.pid);
      const profile = entry.profiles.get(friend.pid);
      const viewer = entry.profiles.get(entry.owner);
      if (!info || !profile || !viewer) return false;
      const allowed = this.presenceAllowedFromMemory(viewer, profile, info);
      const value = this.presenceFromMemory(entry.owner, friend.pid, allowed);
      friends.push({ ...friend, presence: value });
      presence.push(value);
    }
    const invites = [];
    for (const invite of this.pendingInvites.values()) {
      if (invite.from !== entry.owner && invite.to !== entry.owner) continue;
      if (invite.expires <= now) continue;
      const value = entry.inviteSnapshots.get(invite.id);
      if (!value) return false;
      invites.push(value);
    }
    invites.sort((a, b) => a.expires - b.expires || a.id.localeCompare(b.id));
    const stats = this.lobby.stats({ fresh: true });
    const next = { ...entry.snapshot, presence, invites,
      notices: (this.notices.get(entry.owner) ?? []).map((notice) => ({ ...notice })),
      stats: { ...stats } };
    const digest = this.inboxDigest(next);
    const previousBytes = entry.bytes;
    entry.snapshot = next;
    entry.friends = friends;
    if (digest !== entry.digest) {
      this.advanceInboxRevision(entry.owner);
      next.seq = this.inboxSeq(entry.owner, this.inboxEpoch, this.inboxRevisions.get(entry.owner));
      entry.digest = digest;
      this.wakeInbox(entry.owner);
    }
    entry.bytes = this.inboxEntryBytes(entry);
    this.inboxCacheBytes += entry.bytes - previousBytes;
    if (entry.bytes > this.maxInboxBytes) return false;
    this.inboxSnapshots.delete(entry.owner);
    this.inboxSnapshots.set(entry.owner, entry);
    while (this.inboxCacheBytes > this.maxInboxBytes) {
      const oldest = this.inboxSnapshots.keys().next().value;
      if (oldest === undefined) break;
      this.deleteInboxSnapshot(oldest, { evicted: true });
    }
    return this.inboxSnapshots.has(entry.owner);
  }

  storeInboxSnapshot(pidValue, entry, epoch) {
    this.ensureInboxRevision(pidValue);
    if (this.inboxEpoch !== epoch) return false;
    entry.owner = pidValue;
    entry.bytes = this.inboxEntryBytes(entry);
    if (entry.bytes > this.maxInboxBytes) {
      this.advanceInboxRevision(pidValue);
      entry.snapshot.seq = this.inboxSeq(pidValue, this.inboxEpoch, this.inboxRevisions.get(pidValue));
      return true;
    }
    this.deleteInboxSnapshot(pidValue);
    while (this.inboxSnapshots.size >= this.maxInboxEntries ||
        this.inboxCacheBytes + entry.bytes > this.maxInboxBytes) {
      const oldest = this.inboxSnapshots.keys().next().value;
      if (oldest === undefined) break;
      this.deleteInboxSnapshot(oldest, { evicted: true });
    }
    this.inboxSnapshots.set(pidValue, entry);
    this.inboxCacheBytes += entry.bytes;
    for (const dependency of entry.profileDeps) {
      const owners = this.inboxProfileDependents.get(dependency) ?? new Set();
      owners.add(pidValue);
      this.inboxProfileDependents.set(dependency, owners);
    }
    return true;
  }

  async createInvite(account, body, ip) {
    const self = await this.current(account);
    this.expireInvites();
    const now = time(this.lobby);
    const ipHash = await this.hash('ip', ip);
    const limit = await this.transaction((tx) => {
      const perPid = takeAttempts(tx, `quota:invite-pid:${self.pid}`, now, 6, 60);
      const perIp = takeAttempts(tx, `quota:invite-ip:${ipHash}`, now, 6, 60);
      return perPid.retry || perIp.retry || 0;
    });
    if (limit) fail(429, 'rate_limited', 'invite sends are limited', { retry_after: limit });
    exact(body, ['v', 'to', 'game', 'kind'], ['mode', 'note']);
    const toPid = pid(body.to, 'to');
    if (!GAME_ID.test(body.game)) invalid('game must be a 16-character game id');
    if (!['invite', 'challenge'].includes(body.kind)) invalid('kind must be invite or challenge');
    if (body.kind === 'challenge') {
      if (!['normal', 'uber'].includes(body.mode)) invalid('challenge mode must be normal or uber');
    } else if (body.mode !== undefined) invalid('normal invites do not accept mode');
    const note = body.note === undefined ? '' : text(body.note, 'note', 200, { empty: true });
    const target = await this.targetPlayer(toPid);
    this.requireRouted(self, target);
    if (await this.isBlocked(self.pid, toPid)) fail(404, 'unavailable', 'the player is unavailable');
    const friend = await this.transaction((tx) => Boolean(tx.get(`friend:${self.pid}:${toPid}`)));
    const handoffPeer = body.kind === 'challenge' && this.hasChallengeHandoff(account, body.game, toPid);
    const roomPeer = this.sameRoom(self.pid, toPid);
    if (!friend && (target.privacy[body.kind === 'challenge' ? 'challenges' : 'invites'] !== 'friends_or_room' ||
        (!roomPeer && !handoffPeer))) {
      fail(404, 'unavailable', 'the player is unavailable');
    }
    const game = this.lobby.gameFor(account, body.game);
    if (game.state !== 'open' || !game.slots.some((slot) => slot.state === 'player' && slot.pid === self.pid) ||
        !game.slots.some((slot) => slot.state === 'open')) fail(409, 'full', 'the game is not available');
    if (body.kind === 'challenge' && (game.slots[game.hostSlot]?.pid !== self.pid || (!game.password && !game.roomCode))) {
      fail(403, 'not_host', 'only the host of a private room can challenge');
    }
    if (note && this.isMuted(self)) fail(403, 'chat_muted', 'chat is muted');
    const ids = Array.from({ length: 8 }, () => hex(this.lobby.env.randomBytes(8)));
    const quota = await this.transaction((tx) => {
      const pair = pairKey(self.pid, toPid);
      const declined = tx.get(`quota:invite-decline:${pair}`);
      const recent = (declined?.attempts ?? []).filter((at) => at > now - 3600 && at <= now);
      const suppressed = tx.get(`invite-suppress:${pair}`);
      const retry = suppressed?.until > now ? suppressed.until - now :
        (recent.length && recent.at(-1) + 60 > now ? recent.at(-1) + 60 - now : 0);
      if (retry) return { code: 'cooldown', retry };
      return 0;
    });
    if (quota && typeof quota === 'object') fail(quota.code === 'cooldown' ? 409 : 429, quota.code, 'invite sends are limited', { retry_after: quota.retry });
    const pendingFromSender = [...this.pendingInvites.values()].filter((invite) => invite.from === self.pid && invite.expires > now);
    if (this.pendingInvites.size >= MAX_PENDING_INVITES) fail(503, 'social_overloaded', 'pending invite capacity is temporarily full');
    if (pendingFromSender.length >= 3) fail(409, 'invite_limit', 'the pending invite limit has been reached');
    if ([...this.pendingInvites.values()].filter((invite) => invite.to === toPid).length >= 50) {
      fail(409, 'invite_limit', 'the recipient pending invite limit has been reached');
    }
    if (pendingFromSender.some((invite) => invite.to === toPid)) fail(409, 'invite_limit', 'an invite to this player is already pending');
    let id = ids.find((candidate) => !this.pendingInvites.has(candidate));
    if (!id) throw new Error('social invite collision retry limit reached');
    const invite = {
      id,
      from: self.pid,
      to: toPid,
      game: body.game,
      kind: body.kind,
      mode: body.kind === 'challenge' ? body.mode : null,
      note,
      expires: now + 300,
      senderInstall: self.install,
      eligibility: friend ? 'friend' : 'room-peer',
    };
    const result = { v: VERSION, invite: await this.publicInvite(invite) };
    this.pendingInvites.set(id, invite);
    if (handoffPeer) this.consumeChallengePeer(account, toPid);
    this.wakeInbox(self.pid);
    this.wakeInbox(toPid);
    return result;
  }

  async acceptInvite(account, id, body) {
    const self = await this.current(account);
    if (!INVITE_ID.test(id)) invalid('invite id must be 16 lowercase hexadecimal characters');
    exact(body, ['v', 'profile'], ['mode', 'platform', 'build']); // lane JOINVER-1: the joiner's build (lobby.mjs checkBuild)
    if (typeof body.profile !== 'string' || !PROFILE.test(body.profile)) invalid('profile must be 32 lowercase hexadecimal characters');
    const mode = body.mode === undefined ? 'normal' : body.mode;
    const invite = this.pendingInvites.get(id);
    if (!invite || invite.to !== self.pid || invite.expires <= time(this.lobby)) fail(404, 'unavailable', 'the invite is unavailable');
    if (await this.isBlocked(self.pid, invite.from)) fail(404, 'unavailable', 'the invite is unavailable');
    const sender = await this.targetPlayer(invite.from);
    const target = await this.targetPlayer(self.pid);
    this.requireRouted(self, sender);
    this.requireRouted(sender, target);
    try { this.lobby.checkAccount({ install: sender.install, device: sender.device }); } catch { fail(404, 'unavailable', 'the invite is unavailable'); }
    const friend = await this.transaction((tx) => Boolean(tx.get(`friend:${self.pid}:${invite.from}`)));
    const setting = target.privacy[invite.kind === 'challenge' ? 'challenges' : 'invites'];
    if (invite.eligibility === 'friend' ? !friend : setting !== 'friends_or_room') {
      fail(404, 'unavailable', 'the invite is unavailable');
    }
    const game = this.lobby.gameFor(account, invite.game);
    if (game.state !== 'open' || !game.slots.some((slot) => slot.state === 'player' && slot.pid === invite.from) ||
        !game.slots.some((slot) => slot.state === 'open')) fail(409, 'full', 'the game is not available');
    if (invite.kind === 'challenge' && (game.slots[game.hostSlot]?.pid !== invite.from || (!game.password && !game.roomCode))) {
      fail(404, 'unavailable', 'the invite is unavailable');
    }
    if (game.profile !== body.profile) throw this.lobby.profileRefusal(game, account, body); // lane JOINVER-1: both builds in plain words
    if ([...this.lobby.games.values()].some((other) => other.id !== game.id && other.state === 'open' && this.lobby.slotOf(other, account) >= 0)) {
      fail(409, 'in_game', 'leave the other open room before accepting an invite');
    }
    const grant = { [JOIN_GRANT]: true, pid: self.pid, game: game.id, expires: invite.expires, used: false };
    const joined = await this.lobby.join(account, game.id, { profile: body.profile, mode, platform: body.platform, build: body.build }, false, grant);
    this.pendingInvites.delete(id);
    this.wakeInbox(invite.from);
    return { v: VERSION, id: game.id, slot: joined.slot, game: joined.game };
  }

  async declineInvite(account, id, body) {
    const self = await this.current(account);
    if (!INVITE_ID.test(id)) invalid('invite id must be 16 lowercase hexadecimal characters');
    exact(body, ['v'], ['busy']);
    if (body.busy !== undefined && typeof body.busy !== 'boolean') invalid('busy must be a boolean');
    const invite = this.pendingInvites.get(id);
    if (!invite || invite.to !== self.pid || invite.expires <= time(this.lobby) ||
        await this.isBlocked(self.pid, invite.from)) fail(404, 'unavailable', 'the invite is unavailable');
    const pair = pairKey(invite.from, invite.to);
    const now = time(this.lobby);
    await this.transaction((tx) => {
      const key = `quota:invite-decline:${pair}`;
      const status = takeAttempts(tx, key, now, 3, 3600);
      if (status.retry) return;
      const recent = tx.get(key)?.attempts ?? [];
      if (recent.length >= 3) setExpiring(tx, `invite-suppress:${pair}`, { until: now + 86400 }, now + 86400);
    });
    this.pendingInvites.delete(id);
    addNotice(this, invite.from, { kind: body.busy ? 'busy' : 'declined', invite: id, at: time(this.lobby) });
    this.wakeInbox(invite.from);
    this.wakeInbox(self.pid);
    return { v: VERSION, ok: true };
  }

  async publicInvite(invite) {
    const [from, to] = await Promise.all([this.player(invite.from), this.player(invite.to)]);
    if (!from || !to) fail(404, 'unavailable', 'the invite is unavailable');
    return { id: invite.id, from: this.publicPlayer(from), to: this.publicPlayer(to), game: invite.game,
      kind: invite.kind, mode: invite.mode, note: invite.note, expires: invite.expires };
  }

  expireInvites() {
    const now = time(this.lobby);
    for (const [id, invite] of [...this.pendingInvites]) {
      if (invite.expires > now) continue;
      this.pendingInvites.delete(id);
      addNotice(this, invite.from, { kind: 'expired', invite: id, at: now });
      this.wakeInbox(invite.to);
      this.wakeInbox(invite.from);
    }
  }

  async cancelPlayerInvites(pidValue) {
    const invites = [...this.pendingInvites.values()].filter((invite) => invite.from === pidValue || invite.to === pidValue);
    for (const invite of invites) {
      try {
        const friend = await this.transaction((tx) => Boolean(tx.get(`friend:${invite.from}:${invite.to}`)));
        const recipient = await this.player(invite.to);
        const setting = recipient?.privacy[invite.kind === 'challenge' ? 'challenges' : 'invites'];
        if ((invite.eligibility === 'friend' && !friend) ||
            (invite.eligibility === 'room-peer' && setting !== 'friends_or_room')) {
          this.pendingInvites.delete(invite.id);
          this.wakeInbox(invite.from);
          this.wakeInbox(invite.to);
        }
      } catch {
        continue;
      }
    }
  }

  captureChallengePeers(game, account) {
    if (game.state !== 'open' || !account.pid || !account.id) return null;
    const peers = [...new Set(game.slots
      .filter((slot) => slot.state === 'player' && slot.pid && slot.account !== account.id)
      .map((slot) => slot.pid))].slice(0, 7);
    return { accountId: account.id, pid: account.pid, generation: account.generation, install: account.install, peers };
  }

  rememberChallengeHandoff(account, snapshot) {
    if (!snapshot || snapshot.accountId !== account.id || snapshot.pid !== account.pid) return;
    const now = time(this.lobby);
    this.pruneChallengeHandoffs(now);
    if (!this.challengeHandoffs.has(account.id) && this.challengeHandoffs.size >= MAX_CHALLENGE_HANDOFFS) {
      const oldest = [...this.challengeHandoffs.entries()].sort((a, b) => a[1].expires - b[1].expires)[0]?.[0];
      if (oldest) this.challengeHandoffs.delete(oldest);
    }
    this.challengeHandoffs.set(account.id, { ...snapshot, peers: new Set(snapshot.peers), room: null, expires: now + 120 });
  }

  bindChallengeRoom(account, gameId) {
    const now = time(this.lobby);
    this.pruneChallengeHandoffs(now);
    const handoff = this.challengeHandoffs.get(account.id);
    if (!handoff) return;
    if (handoff.pid !== account.pid || handoff.install !== account.install || handoff.generation !== account.generation ||
        handoff.room !== null) {
      this.challengeHandoffs.delete(account.id);
      return;
    }
    handoff.room = gameId;
  }

  hasChallengeHandoff(account, gameId, peerPid) {
    const handoff = this.challengeHandoffs.get(account.id);
    return !!handoff && handoff.pid === account.pid && handoff.install === account.install &&
      handoff.generation === account.generation && handoff.room === gameId &&
      handoff.expires > time(this.lobby) && handoff.peers.has(peerPid);
  }

  consumeChallengePeer(account, peerPid) {
    const handoff = this.challengeHandoffs.get(account.id);
    if (!handoff || !handoff.peers.delete(peerPid)) return;
    if (!handoff.peers.size) this.challengeHandoffs.delete(account.id);
  }

  pruneChallengeHandoffs(now = time(this.lobby)) {
    for (const [accountId, handoff] of this.challengeHandoffs) {
      if (handoff.expires <= now) this.challengeHandoffs.delete(accountId);
    }
  }

  blockedInView(viewerPid, targetPid) {
    return this.blockView?.get(viewerPid)?.has(targetPid) ?? false;
  }

  async refreshBlockView(viewerPid) {
    const rows = await this.transaction((tx) => tx.list(`block:${viewerPid}:`, { limit: 1000 }));
    this.blockView ??= new Map();
    if (!this.blockView.has(viewerPid) && this.blockView.size >= MAX_ACTIVE_PLAYERS) {
      this.blockView.delete(this.blockView.keys().next().value);
    }
    this.blockView.set(viewerPid, new Set([...rows.keys()].map((key) => key.slice(`block:${viewerPid}:`.length))));
  }

  async assertJoinAllowed(account, game) {
    if (!account.pid) return;
    const host = game.slots[game.hostSlot]?.pid;
    if (host && await this.isBlocked(host, account.pid)) fail(404, 'unavailable', 'the game is unavailable');
  }

  validJoinGrant(grant, account, game) {
    return !!grant && grant[JOIN_GRANT] === true && grant.pid === account.pid &&
      grant.game === game.id && grant.expires > time(this.lobby) && !grant.used &&
      (grant.used = true);
  }

  async roomChanged(game) {
    for (const slot of game.slots) if (slot.pid) void this.wakeFriends(slot.pid).catch(() => {});
    const live = new Set(this.lobby.games.keys());
    for (const id of this.freshStartedGames) if (!live.has(id)) this.freshStartedGames.delete(id);
    for (const id of this.loadingUntil.keys()) if (!live.has(id)) this.loadingUntil.delete(id);
    for (const id of this.leftStartedGames) {
      const gameId = id.slice(0, 16);
      if (!live.has(gameId)) this.leftStartedGames.delete(id);
    }
  }

  leaveStarted(game, account) {
    if (game.state === 'started' && account.pid) this.leftStartedGames.add(`${game.id}:${account.pid}`);
  }

  rejoinStarted(game, account) {
    if (account.pid) {
      this.leftStartedGames.delete(`${game.id}:${account.pid}`);
      void this.wakeFriends(account.pid);
    }
  }

  cancelGameInvites(gameId) {
    for (const [id, invite] of this.pendingInvites) if (invite.game === gameId) {
      this.pendingInvites.delete(id);
      this.wakeInbox(invite.from);
      this.wakeInbox(invite.to);
    }
  }

  cancelSenderInvites(pidValue, gameId) {
    if (!pidValue) return;
    for (const [id, invite] of this.pendingInvites) if (invite.from === pidValue && (!gameId || invite.game === gameId)) {
      this.pendingInvites.delete(id);
      this.wakeInbox(invite.from);
      this.wakeInbox(invite.to);
    }
  }

  // ONLINEMENU-1 (Sol r2 on PR #8): once a provider is linked, a new recovery code needs the current one (`recoveryHash`) or a confirmation with
  // the linked account (oauthStart mode "confirm"): with the session and the install alone, a new code could unlink the owner's account
  async recovery(account, ip, recoveryHash = null) {
    const player = await this.current(account);
    const now = time(this.lobby);
    const ipKey = await this.hash('ip', ip);
    const candidates = Array.from({ length: 8 }, () => randomRecovery(this.lobby.env.randomBytes));
    const hashes = await Promise.all(candidates.map((code) => this.hash('recovery', code)));
    if (recoveryHash !== null) await this.proveRecovery(player.pid, recoveryHash);
    const outcome = await this.transaction((tx) => {
      const current = tx.get(`player:${player.pid}`);
      if (!current || current.generation !== account.generation) return { error: new SocialError(401, 'session_revoked', 'this session has been revoked') };
      const meta = oauthMetaFor(tx, player.pid, oauthClock(tx, true), true);
      const stored = tx.get(`player:${player.pid}`); // read again: the metadata's first use marks the player record
      if (Object.keys(meta.providers).length) {
        if (!stored.recovery_hash) {
          return { error: new SocialError(403, 'provider_confirm_required', 'An account is linked to this player: make the recovery code in the launcher (Multiplayer), by confirming with the linked account.') };
        }
        if (recoveryHash === null) return { error: new SocialError(403, 'recovery_required', 'An account is linked to this player: make a new recovery code in the launcher (Multiplayer), with your current code or the linked account.') };
        if (stored.recovery_hash !== recoveryHash) return { error: new SocialError(403, 'bad_recovery', 'the recovery code is not this player\'s') };
      }
      const ipAttempt = takeAttempts(tx, `quota:recovery-ip:${ipKey}`, now, 5, 3600);
      if (ipAttempt.retry) return { error: new SocialError(429, 'rate_limited', 'recovery attempts are limited', { retry_after: ipAttempt.retry }) };
      const pidAttempt = takeAttempts(tx, `quota:recovery-pid:${player.pid}`, now, 5, 3600);
      if (pidAttempt.retry) return { error: new SocialError(429, 'rate_limited', 'recovery attempts are limited', { retry_after: pidAttempt.retry }) };
      return { code: this.storeRecoveryCode(tx, stored, candidates, hashes, now) };
    });
    if (outcome.error) throw outcome.error;
    return { v: VERSION, code: outcome.code };
  }

  async privacy(account, body) {
    exact(body, ['v', 'privacy']);
    const p = body.privacy;
    exact({ v: VERSION, ...p }, ['v', 'invites', 'challenges', 'requests', 'presence']);
    const next = {
      invites: p.invites,
      challenges: p.challenges,
      requests: p.requests,
      presence: p.presence,
    };
    if (!['friends_or_room', 'friends'].includes(next.invites) ||
        !['friends_or_room', 'friends'].includes(next.challenges) ||
        !['anyone', 'nobody'].includes(next.requests) ||
        !['friends', 'nobody'].includes(next.presence)) invalid('privacy contains an unsupported setting');
    const player = await this.current(account);
    const changed = serial(player.privacy) !== serial(next);
    await this.transaction((tx) => tx.put(`player:${player.pid}`, { ...player, privacy: next, updated_at: time(this.lobby) }));
    if (changed) {
      await this.cancelPlayerInvites(player.pid);
      this.wakeFriends(player.pid);
    }
    return { v: VERSION, pid: player.pid, code: player.code, nick: player.nick, privacy: next, hidden: player.hidden };
  }

  async status(account, body) {
    exact(body, ['v', 'state'], ['hidden']);
    if (!['online', 'away', 'matching'].includes(body.state) || (body.hidden !== undefined && typeof body.hidden !== 'boolean')) invalid('state or hidden is invalid');
    const player = await this.current(account);
    const now = time(this.lobby);
    const next = { ...player, hidden: body.hidden ?? player.hidden, updated_at: now };
    this.ensureActiveCapacity(player.pid);
    await this.transaction((tx) => tx.put(`player:${player.pid}`, next));
    const previous = this.active.get(player.pid);
    const active = { state: body.state, lastActivity: now, lastRequest: now, since: previous?.since ?? now };
    const changed = visibleActivityState(previous, now) !== visibleActivityState(active, now) || next.hidden !== player.hidden;
    this.rememberActive(player.pid, active);
    if (next.hidden !== player.hidden) await this.cancelPlayerInvites(player.pid);
    if (changed) this.wakeFriends(player.pid);
    return { v: VERSION, ok: true };
  }

  activity(account, { idle = false } = {}) {
    if (!account?.pid) return;
    this.expireInvites();
    const now = time(this.lobby);
    const previous = this.active.get(account.pid) ?? { state: 'online', since: now, lastActivity: now, lastRequest: now };
    const was = visibleActivityState(this.active.get(account.pid), now);
    const next = { ...previous, lastRequest: now };
    if (!idle) next.lastActivity = now;
    const becomes = visibleActivityState(next, now);
    this.rememberActive(account.pid, next);
    if (was !== becomes) this.wakeFriends(account.pid);
  }

  ensureActiveCapacity(pidValue) {
    const now = time(this.lobby);
    for (const [key, value] of this.active) {
      if (now - value.lastRequest > 150 && !this.hasLiveStartedMembership(key)) this.active.delete(key);
    }
    if (!this.active.has(pidValue) && this.active.size >= MAX_ACTIVE_PLAYERS) {
      throw new SocialError(503, 'social_overloaded', 'active player capacity is temporarily full');
    }
  }

  hasLiveStartedMembership(pidValue) {
    return [...this.lobby.games.values()].some((game) => game.state === 'started' && !game.finished &&
      !this.leftStartedGames.has(`${game.id}:${pidValue}`) &&
      game.slots.some((slot) => slot.pid === pidValue && slot.state === 'player'));
  }

  rememberActive(pidValue, state) {
    this.ensureActiveCapacity(pidValue);
    this.active.set(pidValue, state);
  }

  async friends(account) {
    const cached = this.inboxSnapshots.get(account.pid);
    if (cached && this.refreshInboxSnapshot(cached)) return { v: VERSION, friends: structuredClone(cached.friends) };
    await this.inboxSnapshot(account);
    const refreshed = this.inboxSnapshots.get(account.pid);
    if (refreshed) return { v: VERSION, friends: structuredClone(refreshed.friends) };
    const self = await this.current(account);
    const rows = await this.transaction((tx) => {
      const page = tx.list(`friend:${self.pid}:`, { limit: 201 });
      if (page.size > 200) throw new SocialStorageError('unavailable');
      for (const [key, edge] of page) {
        const peer = key.slice(`friend:${self.pid}:`.length);
        if (!PID.test(peer) || validateFriendEdge(edge).since < 0) throw new SocialStorageError('unavailable');
        const reverse = tx.get(`friend:${peer}:${self.pid}`);
        if (!reverse || validateFriendEdge(reverse).since !== edge.since) throw new SocialStorageError('unavailable');
      }
      return page;
    });
    const friends = [];
    for (const [key, edge] of rows) {
      const otherPid = key.slice(`friend:${self.pid}:`.length);
      const other = await this.player(otherPid);
      if (!other) throw new SocialError(503, 'social_unavailable', 'social storage is unavailable');
      const valid = validatePlayer(other, otherPid);
      friends.push({ pid: valid.pid, nick: valid.nick, code: valid.code, since: validateFriendEdge(edge).since, presence: await this.presence(self, valid) });
    }
    friends.sort((a, b) => a.pid.localeCompare(b.pid));
    return { v: VERSION, friends };
  }

  async request(account, body, ip) {
    const self = await this.current(account);
    const now = time(this.lobby);
    const ipHash = await this.hash('ip', ip);
    const lookup = !!body && typeof body === 'object' && Object.hasOwn(body, 'code');
    const limited = await this.transaction((tx) => {
      const attempts = [
        ...(lookup ? [
          takeAttempts(tx, `quota:request-code-pid:${self.pid}`, now, 20, 60),
          takeAttempts(tx, `quota:request-code-ip:${ipHash}`, now, 20, 60),
        ] : []),
        takeAttempts(tx, `quota:request-pid:${self.pid}`, now, 10, 3600),
        takeAttempts(tx, `quota:request-ip-hour:${ipHash}`, now, 10, 3600),
        takeAttempts(tx, `quota:request-pid-day:${self.pid}`, now, 30, 86400),
        takeAttempts(tx, `quota:request-ip-day:${ipHash}`, now, 30, 86400),
      ];
      return attempts.find((entry) => entry.retry)?.retry ?? 0;
    });
    if (limited) fail(429, 'rate_limited', 'friend requests are limited', { retry_after: limited });
    exact(body, ['v'], ['code', 'pid']);
    if (Object.hasOwn(body, 'code') === Object.hasOwn(body, 'pid')) invalid('provide exactly one of code or pid');
    const targetPid = body.code !== undefined
      ? await this.lookupCode(codeInput(body.code))
      : pid(body.pid, 'pid');
    if (!targetPid) fail(404, 'unavailable', 'the player is unavailable');
    if (targetPid === self.pid) fail(400, 'self_target', 'you cannot add yourself');
    if (body.pid !== undefined && (await this.targetPlayer(targetPid)).privacy.requests === 'nobody') {
      fail(404, 'unavailable', 'the player is unavailable');
    }
    const target = await this.targetPlayer(targetPid);
    this.requireRouted(self, target);
    if (body.pid !== undefined && !await this.knownPeer(self.pid, targetPid)) fail(404, 'unavailable', 'the player is unavailable');
    if (await this.isBlocked(self.pid, targetPid)) fail(404, 'unavailable', 'the player is unavailable');
    const rids = Array.from({ length: 8 }, () => hex(this.lobby.env.randomBytes(8)));
    let evicted = null;
    const outcome = await this.transaction((tx) => {
      if (tx.get(`friend:${self.pid}:${targetPid}`)) return { error: new SocialError(409, 'already_friends', 'you are already friends') };
      const pending = [...tx.list(`request-pair:${pairKey(self.pid, targetPid)}:`, { limit: 2 })];
      const outgoing = tx.list(`request-out:${self.pid}:`, { limit: 201 });
      if (outgoing.size > 200) throw new SocialStorageError('unavailable');
      for (const [key, marker] of outgoing) {
        const requestId = key.slice(`request-out:${self.pid}:`.length);
        const request = validateRequest(tx.get(`request:${requestId}`), requestId);
        if (marker !== true || request.from !== self.pid ||
            !tx.get(`request-in:${request.to}:${requestId}`) ||
            !tx.get(`request-pair:${pairKey(request.from, request.to)}:${requestId}`)) throw new SocialStorageError('unavailable');
      }
      if (pending.length) {
        for (const [key, marker] of pending) {
          const requestId = key.slice(key.lastIndexOf(':') + 1);
          const request = validateRequest(tx.get(`request:${requestId}`), requestId);
          if (marker !== true || pairKey(request.from, request.to) !== pairKey(self.pid, targetPid) ||
              !tx.get(`request-in:${request.to}:${requestId}`) ||
              !tx.get(`request-out:${request.from}:${requestId}`)) throw new SocialStorageError('unavailable');
        }
        return { error: new SocialError(409, 'already_requested', 'a friend request is already pending') };
      }
      if (outgoing.size >= 200) return { error: new SocialError(409, 'request_limit', 'the outgoing request limit has been reached') };
      const incoming = tx.list(`request-in:${targetPid}:`, { limit: 51 });
      if (incoming.size > 50) throw new SocialStorageError('unavailable');
      for (const [key, marker] of incoming) {
        const requestId = key.slice(`request-in:${targetPid}:`.length);
        const request = validateRequest(tx.get(`request:${requestId}`), requestId);
        if (marker !== true || request.to !== targetPid ||
            !tx.get(`request-out:${request.from}:${requestId}`) ||
            !tx.get(`request-pair:${pairKey(request.from, request.to)}:${requestId}`)) throw new SocialStorageError('unavailable');
      }
      let id = null;
      for (const candidate of rids) if (!tx.get(`request:${candidate}`)) { id = candidate; break; }
      if (!id) throw new Error('social request collision retry limit reached');
      if (incoming.size >= 50) {
        const oldestId = [...incoming.keys()].map((key) => key.slice(`request-in:${targetPid}:`.length)).sort((a, b) => {
          const ra = validateRequest(tx.get(`request:${a}`), a);
          const rb = validateRequest(tx.get(`request:${b}`), b);
          return ra.at - rb.at || a.localeCompare(b);
        })[0];
        const oldest = validateRequest(tx.get(`request:${oldestId}`), oldestId);
        evicted = oldest;
        tx.delete(`request:${oldestId}`);
        tx.delete(`request-in:${oldest.to}:${oldestId}`);
        tx.delete(`request-out:${oldest.from}:${oldestId}`);
        tx.delete(`request-pair:${pairKey(oldest.from, oldest.to)}:${oldestId}`);
      }
      const request = { id, from: self.pid, to: targetPid, at: now };
      tx.put(`request:${id}`, request);
      tx.put(`request-in:${targetPid}:${id}`, true);
      tx.put(`request-out:${self.pid}:${id}`, true);
      tx.put(`request-pair:${pairKey(self.pid, targetPid)}:${id}`, true);
      return { request };
    });
    if (outcome.error) throw outcome.error;
    if (evicted) {
      this.wakeInbox(evicted.from);
      this.wakeInbox(evicted.to);
    }
    this.wakeInbox(targetPid);
    this.wakeInbox(self.pid);
    return { v: VERSION, request: await this.publicRequest(outcome.request) };
  }

  async lookupCode(code) {
    const found = await this.transaction((tx) => tx.get(`code:${code}`));
    return found ?? null;
  }

  async publicRequest(request) {
    validateRequest(request);
    const [from, to] = await Promise.all([this.player(request.from), this.player(request.to)]);
    if (!from || !to) throw new SocialStorageError('unavailable');
    validatePlayer(from, request.from);
    validatePlayer(to, request.to);
    return { id: request.id, from: this.publicPlayer(from), to: this.publicPlayer(to), at: request.at };
  }

  publicPlayer(player) {
    return { pid: player.pid, nick: player.nick, code: player.code };
  }

  async targetPlayer(targetPid) {
    const target = await this.player(targetPid);
    if (!target) fail(404, 'unavailable', 'the player is unavailable');
    return validatePlayer(target, targetPid);
  }

  async knownPeer(a, b) {
    if (this.sameRoom(a, b)) return true;
    return this.transaction((tx) => {
      const rows = tx.list(`recent:${a}:`, { limit: 20, reverse: true });
      for (const [key, value] of rows) validateRecent(key, a, value);
      return [...rows.values()].some((game) => game.players.some((player) => player.pid === b));
    });
  }

  sameRoom(a, b) {
    const now = time(this.lobby);
    return [...this.lobby.games.values()].some((game) => game.state === 'open' &&
      [a, b].every((target) => game.slots.some((slot) => slot.pid === target && slot.state === 'player') &&
        now - (this.active.get(target)?.lastRequest ?? 0) <= 150));
  }

  requireRouted(viewer, target) {
    try {
      const source = this.lobby.checkAccount({ install: viewer.install, device: viewer.device });
      const effective = this.lobby.checkAccount({ install: target.install, device: target.device });
      if ((effective.routing ?? '') !== (source.routing ?? '')) fail(404, 'unavailable', 'the player is unavailable');
    } catch (error) {
      if (error instanceof SocialError) throw error;
      fail(404, 'unavailable', 'the player is unavailable');
    }
  }

  requireHistoryRouted(viewer, target) {
    const source = this.lobby.checkAccount({ install: viewer.install, device: viewer.device });
    const effective = this.lobby.getEnforcement(target.install)?.routing ?? '';
    if ((source.routing ?? '') !== effective) fail(404, 'unavailable', 'the player is unavailable');
  }

  async isBlocked(a, b) {
    return this.transaction((tx) => Boolean(tx.get(`block:${a}:${b}`) || tx.get(`block:${b}:${a}`)));
  }

  async acceptRequest(account, id) {
    const self = await this.current(account);
    if (!REQUEST_ID.test(id)) invalid('request id must be 16 lowercase hexadecimal characters');
    const now = time(this.lobby);
    const outcome = await this.transaction((tx) => {
      const stored = tx.get(`request:${id}`);
      if (!stored) return { error: new SocialError(404, 'unavailable', 'the request is unavailable') };
      const request = validateRequest(stored, id);
      if (request.to !== self.pid) return { error: new SocialError(404, 'unavailable', 'the request is unavailable') };
      if (tx.get(`request-in:${self.pid}:${id}`) !== true ||
          tx.get(`request-out:${request.from}:${id}`) !== true ||
          tx.get(`request-pair:${pairKey(request.from, request.to)}:${id}`) !== true) {
        throw new SocialStorageError('unavailable');
      }
      if (tx.get(`block:${request.from}:${request.to}`) || tx.get(`block:${request.to}:${request.from}`)) {
        return { error: new SocialError(404, 'unavailable', 'the request is unavailable') };
      }
      const a = tx.get(`player:${request.from}`);
      const b = tx.get(`player:${request.to}`);
      if (!a || !b) throw new SocialStorageError('unavailable');
      validatePlayer(a, request.from);
      validatePlayer(b, request.to);
      if (tx.get(`install:${a.install}`) !== a.pid || tx.get(`code:${a.code}`) !== a.pid ||
          tx.get(`install:${b.install}`) !== b.pid || tx.get(`code:${b.code}`) !== b.pid) {
        throw new SocialStorageError('unavailable');
      }
      try { this.lobby.checkAccount({ install: a.install, device: a.device }); } catch (error) { return { error }; }
      try { this.lobby.checkAccount({ install: b.install, device: b.device }); } catch (error) { return { error }; }
      try { this.requireRouted(self, a); this.requireRouted(self, b); } catch (error) { return { error }; }
      const aFriends = tx.list(`friend:${a.pid}:`, { limit: 201 });
      const bFriends = tx.list(`friend:${b.pid}:`, { limit: 201 });
      if (aFriends.size > 200 || bFriends.size > 200) throw new SocialStorageError('unavailable');
      if (aFriends.size >= 200 || bFriends.size >= 200) {
        return { error: new SocialError(409, 'friend_limit', 'a friend limit has been reached') };
      }
      for (const [key, edge] of aFriends) {
        const peer = key.slice(`friend:${a.pid}:`.length);
        if (!PID.test(peer) || !tx.get(`friend:${peer}:${a.pid}`) ||
            validateFriendEdge(tx.get(`friend:${peer}:${a.pid}`)).since !== validateFriendEdge(edge).since) {
          throw new SocialStorageError('unavailable');
        }
      }
      tx.put(`friend:${a.pid}:${b.pid}`, { since: now });
      tx.put(`friend:${b.pid}:${a.pid}`, { since: now });
      removeRequest(tx, request);
      return { ok: true, a: a.pid, b: b.pid };
    });
    if (outcome.error) throw outcome.error;
    this.wakeInbox(outcome.a);
    this.wakeInbox(outcome.b);
    return { v: VERSION, ok: true };
  }

  async declineRequest(account, id) {
    const self = await this.current(account);
    if (!REQUEST_ID.test(id)) invalid('request id must be 16 lowercase hexadecimal characters');
    const request = await this.transaction((tx) => {
      const value = tx.get(`request:${id}`);
      if (!value) return null;
      const valid = validateRequest(value, id);
      if (valid.to !== self.pid) return null;
      if (tx.get(`request-in:${self.pid}:${id}`) !== true ||
          tx.get(`request-out:${valid.from}:${id}`) !== true ||
          tx.get(`request-pair:${pairKey(valid.from, valid.to)}:${id}`) !== true) throw new SocialStorageError('unavailable');
      removeRequest(tx, valid);
      return valid;
    });
    if (!request) fail(404, 'unavailable', 'the request is unavailable');
    this.wakeInbox(request.from);
    this.wakeInbox(request.to);
    return { v: VERSION, ok: true };
  }

  async removeFriend(account, targetPid) {
    const self = await this.current(account);
    pid(targetPid);
    await this.transaction((tx) => {
      tx.delete(`friend:${self.pid}:${targetPid}`);
      tx.delete(`friend:${targetPid}:${self.pid}`);
      cancelRequests(tx, self.pid, targetPid);
    });
    this.cancelInvites(self.pid, targetPid);
    this.wakeInbox(self.pid);
    this.wakeInbox(targetPid);
    return { v: VERSION, ok: true };
  }

  async blocks(account, after, limit) {
    const self = await this.current(account);
    const nextLimit = limit === null ? 50 : parseBoundedLimit(limit, 1, 100, 'limit');
    if (after !== null && !PID.test(after)) fail(400, 'bad_query', 'after must be a pid');
    const rows = await this.transaction((tx) => {
      const prefix = `block:${self.pid}:`;
      const page = tx.list(prefix, { after: after === null ? null : `${prefix}${after}`, limit: nextLimit + 1 });
      return [...page.entries()];
    });
    const page = rows.slice(0, nextLimit);
    const blocks = [];
    for (const [key, value] of page) {
      const targetPid = key.slice(`block:${self.pid}:`.length);
      if (!PID.test(targetPid)) throw new SocialStorageError('unavailable');
      const edge = validateBlockEdge(value);
      const target = await this.player(targetPid);
      if (!target) throw new SocialStorageError('unavailable');
      const valid = validatePlayer(target, target.pid);
      blocks.push({ pid: valid.pid, nick: valid.nick, code: valid.code, note: edge.note, at: edge.at });
    }
    return { v: VERSION, blocks, next: rows.length > nextLimit ? page.at(-1)[0].slice(`block:${self.pid}:`.length) : null };
  }

  async block(account, body) {
    const self = await this.current(account);
    exact(body, ['v', 'pid'], ['note']);
    const targetPid = pid(body.pid);
    const note = body.note === undefined ? '' : text(body.note, 'note', 200, { empty: true });
    if (targetPid === self.pid) fail(400, 'self_target', 'you cannot block yourself');
    const known = await this.knownPeer(self.pid, targetPid) ||
      await this.transaction((tx) => Boolean(tx.get(`block:${self.pid}:${targetPid}`) ||
        tx.get(`friend:${self.pid}:${targetPid}`) || tx.list(`request-pair:${pairKey(self.pid, targetPid)}:`, { limit: 1 }).size));
    const invited = [...this.pendingInvites.values()].some((invite) => pairKey(invite.from, invite.to) === pairKey(self.pid, targetPid));
    if (!known && !invited) fail(404, 'unavailable', 'the player is unavailable');
    const target = await this.targetPlayer(targetPid);
    this.requireRouted(self, target);
    const now = time(this.lobby);
    const outcome = await this.transaction((tx) => {
      const key = `block:${self.pid}:${targetPid}`;
      const existing = tx.get(key);
      if (!existing && tx.list(`block:${self.pid}:`, { limit: 1000 }).size >= 1000) {
        return { error: new SocialError(409, 'block_limit', 'the block limit has been reached') };
      }
      tx.put(key, { note, at: now });
      tx.delete(`friend:${self.pid}:${targetPid}`);
      tx.delete(`friend:${targetPid}:${self.pid}`);
      cancelRequests(tx, self.pid, targetPid);
      return { ok: true };
    });
    if (outcome.error) throw outcome.error;
    this.cancelInvites(self.pid, targetPid);
    this.blockView.get(self.pid)?.add(targetPid);
    this.wakeInbox(self.pid);
    this.wakeInbox(targetPid);
    this.wakeFriends(self.pid);
    this.wakeFriends(targetPid);
    return { v: VERSION, ok: true };
  }

  async unblock(account, targetPid) {
    const self = await this.current(account);
    pid(targetPid);
    await this.transaction((tx) => tx.delete(`block:${self.pid}:${targetPid}`));
    this.blockView.get(self.pid)?.delete(targetPid);
    this.wakeInbox(self.pid);
    this.wakeInbox(targetPid);
    return { v: VERSION, ok: true };
  }

  async recent(account) {
    const self = await this.current(account);
    const rows = await this.transaction((tx) => {
      const page = tx.list(`recent:${self.pid}:`, { limit: 20, reverse: true });
      for (const [key, value] of page) validateRecent(key, self.pid, value);
      return page;
    });
    return { v: VERSION, games: [...rows.values()] };
  }

  async recordGameStart(game) {
    if (!this.configured || game.socialRecentRecorded) return null;
    const at = time(this.lobby);
    const roster = game.slots.filter((slot) => slot.state === 'player' && PID.test(slot.pid ?? ''));
    const gameId = game.id;
    const outcome = await this.transaction((tx) => {
      cleanExpiredStartedGames(tx, at);
      const marker = tx.get(`recent-started:${gameId}`);
      if (marker && (!Number.isSafeInteger(marker.at) || marker.at < 0 ||
          (marker.expires !== undefined && (!Number.isSafeInteger(marker.expires) || marker.expires < marker.at)) ||
          (marker.index !== undefined && marker.index !== `recent-started-expire:${String(marker.expires).padStart(16, '0')}:${gameId}`))) {
        throw new SocialStorageError('unavailable');
      }
      if (marker && marker.expires > at) return null;
      if (marker) {
        if (marker.index) tx.delete(marker.index);
        tx.delete(`recent-started:${gameId}`);
      }
      const records = [];
      const removed = [];
      for (const subject of roster) {
        const players = roster.filter((other) => other.pid !== subject.pid).slice(0, 7).map((other) => {
          const stored = tx.get(`player:${other.pid}`);
          if (!stored) throw new SocialStorageError('unavailable');
          validatePlayer(stored, other.pid);
          const target = game.reportParticipants?.find((entry) => entry.account === other.account)?.account;
          return {
            ...this.publicPlayer({ pid: other.pid, nick: other.name, code: stored.code }),
            target: target ?? other.account,
          };
        });
        records.push([subject.pid, { game: gameId, at, players }]);
      }
      const markerKey = `recent-started-expire:${String(at + 10800).padStart(16, '0')}:${gameId}`;
      tx.put(`recent-started:${gameId}`, { at, expires: at + 10800, index: markerKey });
      tx.put(markerKey, { game: gameId, expires: at + 10800 });
      for (const [owner, value] of records) {
        const key = `recent:${owner}:${String(at).padStart(16, '0')}:${gameId}`;
        tx.put(key, value);
        const all = [...tx.list(`recent:${owner}:`, { limit: 21 })];
        for (const [recentKey, recentValue] of all) validateRecent(recentKey, owner, recentValue);
        if (all.length > 20) for (const [oldKey, oldValue] of all.slice(0, all.length - 20)) {
          removed.push([oldKey, oldValue]);
          tx.delete(oldKey);
        }
      }
      return {
        gameId,
        at,
        markerKey,
        added: records.map(([owner, value]) => [`recent:${owner}:${String(at).padStart(16, '0')}:${gameId}`, value]),
        removed,
      };
    });
    if (outcome) game.socialRecentRecorded = true;
    return outcome;
  }

  async rollbackGameStart(undo) {
    if (!undo) return;
    await this.transaction((tx) => {
      const marker = tx.get(`recent-started:${undo.gameId}`);
      if (!marker || marker.at !== undo.at) return;
      for (const [key] of undo.added) tx.delete(key);
      for (const [key, value] of undo.removed) tx.put(key, value);
      tx.delete(undo.markerKey);
      tx.delete(`recent-started:${undo.gameId}`);
    });
  }

  async sendMessage(account, targetPid, body, ip) {
    const self = await this.current(account);
    const now = time(this.lobby);
    const ipHash = await this.hash('ip', ip);
    if (PID.test(targetPid ?? '') && body && typeof body === 'object' && !Array.isArray(body) &&
        body.v === VERSION && CLIENT_ID.test(body.client_id ?? '') &&
        typeof body.text === 'string' && Object.keys(body).length === 3) {
      const pair = sortedPair(self.pid, targetPid);
      const key = `receipt:${self.pid}:${pair[0]}:${pair[1]}:${body.client_id}`;
      const existing = await this.transaction((tx) => {
        const receipt = tx.get(key);
        return receipt === null ? null : validateStoredReceipt(tx, receipt, self.pid, targetPid, body.client_id, pair);
      });
      if (existing && existing.text === body.text) {
        const peer = await this.targetPlayer(targetPid);
        this.requireRouted(self, peer);
        const allowed = await this.transaction((tx) => Boolean(
          tx.get(`friend:${self.pid}:${targetPid}`) &&
          !tx.get(`block:${self.pid}:${targetPid}`) &&
          !tx.get(`block:${targetPid}:${self.pid}`),
        ));
        if (!allowed) fail(404, 'unavailable', 'the player is unavailable');
        if (this.isMuted(self)) fail(403, 'chat_muted', 'chat is muted');
        return { v: VERSION, message: existing };
      }
    }
    const retry = await this.transaction((tx) => {
      const attempts = [
        takeAttempts(tx, `quota:chat-minute-pid:${self.pid}`, now, 30, 60),
        takeAttempts(tx, `quota:chat-minute-ip:${ipHash}`, now, 30, 60),
        takeAttempts(tx, `quota:chat-day-pid:${self.pid}`, now, 500, 86400),
        takeAttempts(tx, `quota:chat-day-ip:${ipHash}`, now, 500, 86400),
      ];
      return attempts.find((entry) => entry.retry)?.retry ?? 0;
    });
    if (retry) fail(429, 'rate_limited', 'chat sends are limited', { retry_after: retry });
    pid(targetPid);
    exact(body, ['v', 'client_id', 'text']);
    if (typeof body.client_id !== 'string' || !CLIENT_ID.test(body.client_id)) invalid('client_id must be 32 lowercase hexadecimal characters');
    const messageText = text(body.text, 'text', 1000);
    if (encoder.encode(messageText).length > 4000) invalid('text exceeds 4000 UTF-8 bytes');
    if (self.pid === targetPid) fail(400, 'self_target', 'you cannot message yourself');
    const peer = await this.targetPlayer(targetPid);
    this.requireRouted(self, peer);
    const pair = sortedPair(self.pid, targetPid);
    const conv = `conversation:${pair[0]}:${pair[1]}`;
    const receiptKey = `receipt:${self.pid}:${pair[0]}:${pair[1]}:${body.client_id}`;
    const outcome = await this.transaction((tx) => {
      const existing = tx.get(receiptKey);
      if (existing) {
        const message = validateStoredReceipt(tx, existing, self.pid, targetPid, body.client_id, pair);
        if (message.text !== messageText) return { error: new SocialError(409, 'idempotency_conflict', 'client_id was already used for different text') };
        if (!tx.get(`friend:${self.pid}:${targetPid}`) || tx.get(`block:${self.pid}:${targetPid}`) || tx.get(`block:${targetPid}:${self.pid}`)) {
          return { error: new SocialError(404, 'unavailable', 'the player is unavailable') };
        }
        return { message, replay: true };
      }
      if (!tx.get(`friend:${self.pid}:${targetPid}`) || tx.get(`block:${self.pid}:${targetPid}`) || tx.get(`block:${targetPid}:${self.pid}`)) {
        return { error: new SocialError(404, 'unavailable', 'the player is unavailable') };
      }
      if (this.isMuted(self)) return { error: new SocialError(403, 'chat_muted', 'chat is muted') };
      const day = new Date(now * 1000).toISOString().slice(0, 10);
      const dailyKey = 'quota:chat-global-current';
      const counter = tx.get(dailyKey) ?? { day, count: 0 };
      const parsedCounterDay = typeof counter?.day === 'string' ? Date.parse(`${counter.day}T00:00:00.000Z`) : NaN;
      if (!counter || typeof counter !== 'object' || typeof counter.day !== 'string' ||
          !/^\d{4}-\d{2}-\d{2}$/.test(counter.day) || !Number.isSafeInteger(counter.count) ||
          !Number.isFinite(parsedCounterDay) || new Date(parsedCounterDay).toISOString().slice(0, 10) !== counter.day ||
          counter.count < 0 || counter.count > 10000) throw new SocialStorageError('unavailable');
      const total = counter.day === day ? counter.count : 0;
      if (total >= 10000) return { error: new SocialError(429, 'rate_limited', 'the daily chat limit has been reached', { retry_after: secondsToUtcDay(now) }) };
      const oldThread = tx.get(conv);
      const lastId = oldThread === null ? 0 : validateConversation(tx, pair, oldThread);
      const selfThread = tx.get(`thread:${self.pid}:${targetPid}`);
      const targetThread = tx.get(`thread:${targetPid}:${self.pid}`);
      if (!oldThread && (selfThread || targetThread || tx.get(`read:${self.pid}:${targetPid}`) !== null ||
          tx.get(`read:${targetPid}:${self.pid}`) !== null)) throw new SocialStorageError('unavailable');
      const selfRead = validateReadState(tx, self.pid, targetPid, lastId, selfThread);
      const targetRead = validateReadState(tx, targetPid, self.pid, lastId, targetThread);
      const nextId = lastId + 1;
      if (!Number.isSafeInteger(nextId)) return { error: new SocialError(507, 'social_store_full', 'conversation capacity has been reached') };
      const message = { id: nextId, from: self.pid, to: targetPid, text: messageText, at: now, client_id: body.client_id };
      tx.put(`message:${pair[0]}:${pair[1]}:${String(nextId).padStart(16, '0')}`, message);
      tx.put(receiptKey, { text: messageText, message });
      tx.put(conv, { last_id: nextId });
      tx.put(`thread:${self.pid}:${targetPid}`, { last_id: nextId, read_id: selfRead });
      tx.put(`thread:${targetPid}:${self.pid}`, { last_id: nextId, read_id: targetRead });
      tx.put(dailyKey, { day, count: total + 1 });
      return { message, replay: false };
    });
    if (outcome.error) throw outcome.error;
    this.wakeInbox(targetPid);
    this.wakeInbox(self.pid);
    return { v: VERSION, message: outcome.message };
  }

  isMuted(player) {
    const record = player.install ? this.lobby.getEnforcement(player.install) : null;
    return record?.mute_until !== null && record?.mute_until > time(this.lobby);
  }

  async messages(account, targetPid, after, limit) {
    const self = await this.current(account);
    pid(targetPid);
    const afterId = after === null ? 0 : parseBoundedInteger(after, 0, Number.MAX_SAFE_INTEGER, 'after');
    const pageLimit = limit === null ? 50 : parseBoundedLimit(limit, 1, 100, 'limit');
    const peer = await this.targetPlayer(targetPid);
    this.requireHistoryRouted(self, peer);
    const pair = sortedPair(self.pid, targetPid);
    const result = await this.transaction((tx) => {
      const conv = tx.get(`conversation:${pair[0]}:${pair[1]}`);
      const friend = tx.get(`friend:${self.pid}:${targetPid}`);
      if (!conv && !friend) return null;
      const lastId = conv ? validateConversation(tx, pair, conv) : 0;
      const readId = validateReadState(tx, self.pid, targetPid, lastId, tx.get(`thread:${self.pid}:${targetPid}`));
      const prefix = `message:${pair[0]}:${pair[1]}:`;
      const rows = tx.list(prefix, { after: `${prefix}${String(afterId).padStart(16, '0')}`, limit: pageLimit + 1 });
      const messages = [...rows.values()].map((message) =>
        validateMessage(message, String(message?.id ?? '').padStart(16, '0'), pair));
      if (messages.some((message, index) => message.id !== afterId + index + 1 ||
          (index > 0 && message.id <= messages[index - 1].id) || message.id > lastId)) {
        throw new SocialStorageError('unavailable');
      }
      for (const message of messages) {
        const receipt = tx.get(`receipt:${message.from}:${pair[0]}:${pair[1]}:${message.client_id}`);
        if (!receipt || receipt.text !== message.text || serial(receipt.message) !== serial(message)) {
          throw new SocialStorageError('unavailable');
        }
      }
      const hasMore = messages.length > pageLimit;
      if (hasMore) messages.pop();
      return {
        messages,
        next: hasMore ? messages.at(-1).id : null,
        last_id: lastId,
        read_id: readId,
      };
    });
    if (!result) fail(404, 'unavailable', 'the player is unavailable');
    return { v: VERSION, ...result };
  }

  async markRead(account, targetPid, body) {
    const self = await this.current(account);
    pid(targetPid);
    exact(body, ['v', 'through']);
    const through = integer(body.through, 'through', 0, Number.MAX_SAFE_INTEGER);
    const peer = await this.targetPlayer(targetPid);
    this.requireHistoryRouted(self, peer);
    const pair = sortedPair(self.pid, targetPid);
    const result = await this.transaction((tx) => {
      const conv = tx.get(`conversation:${pair[0]}:${pair[1]}`);
      const friend = tx.get(`friend:${self.pid}:${targetPid}`);
      if (!conv && !friend) return { unavailable: true };
      const lastId = conv ? validateConversation(tx, pair, conv) : 0;
      const oldThread = tx.get(`thread:${self.pid}:${targetPid}`);
      if (!conv && (oldThread || tx.get(`read:${self.pid}:${targetPid}`) !== null)) {
        throw new SocialStorageError('unavailable');
      }
      if (through > lastId) return { future: true };
      if (!conv && through === 0) return { read_id: 0 };
      const currentRead = validateReadState(tx, self.pid, targetPid, lastId, oldThread);
      const readId = Math.max(currentRead, through);
      tx.put(`read:${self.pid}:${targetPid}`, readId);
      tx.put(`thread:${self.pid}:${targetPid}`, { last_id: lastId, read_id: readId });
      return { read_id: readId };
    });
    if (result.unavailable) fail(404, 'unavailable', 'the player is unavailable');
    if (result.future) fail(400, 'bad_field', 'through cannot be greater than last_id');
    this.wakeInbox(self.pid);
    return { v: VERSION, read_id: result.read_id };
  }

  async threads(account, after, limit) {
    const self = await this.current(account);
    const afterPid = after === null ? null : pid(after, 'after');
    const pageLimit = limit === null ? 50 : parseBoundedLimit(limit, 1, 100, 'limit');
    const prefix = `thread:${self.pid}:`;
    const rows = await this.transaction((tx) => {
      const entries = tx.list(prefix, {
        after: afterPid === null ? null : `${prefix}${afterPid}`,
        limit: pageLimit + 1,
      });
      for (const [key, value] of entries) {
        const peerPid = key.slice(prefix.length);
        if (!PID.test(peerPid)) throw new SocialStorageError('unavailable');
        const head = validateThread(value);
        const pair = sortedPair(self.pid, peerPid);
        const conversation = tx.get(`conversation:${pair[0]}:${pair[1]}`);
        if (!conversation || validateConversation(tx, pair, conversation) !== head.last_id ||
            validateReadState(tx, self.pid, peerPid, head.last_id, value) !== head.read_id) {
          throw new SocialStorageError('unavailable');
        }
      }
      return entries;
    });
    const entries = [...rows.entries()];
    const threads = [];
    for (const [key, value] of entries.slice(0, pageLimit)) {
      const peerPid = key.slice(prefix.length);
      const peer = await this.player(peerPid);
      if (!peer) throw new SocialStorageError('unavailable');
      const valid = validatePlayer(peer, peerPid);
      threads.push({ pid: peerPid, nick: valid.nick, code: valid.code, last_id: value.last_id, read_id: value.read_id });
    }
    return { v: VERSION, threads, next: entries.length > pageLimit ? entries[pageLimit - 1][0].slice(prefix.length) : null };
  }

  async presence(viewer, target) {
    return (await this.presenceDetails(viewer, target)).presence;
  }

  async presenceDetails(viewer, target, viewerSecurity = null) {
    const blocked = await this.isBlocked(viewer.pid, target.pid);
    const info = {
      blocked,
      viewerSecurity: viewerSecurity ?? this.lobby.accountSecurity(viewer),
      targetSecurity: this.lobby.accountSecurity(target),
    };
    const allowed = this.presenceAllowedFromMemory(viewer, target, info);
    return { ...info, presence: this.presenceFromMemory(viewer.pid, target.pid, allowed) };
  }

  presenceAllowedFromMemory(viewer, target, info) {
    if (target.hidden || target.privacy.presence === 'nobody' || info.blocked) return false;
    try {
      this.lobby.checkAccount(viewer, { security: info.viewerSecurity });
      this.lobby.checkAccount(target, { security: info.targetSecurity });
      return (info.viewerSecurity.effective?.routing ?? '') === (info.targetSecurity.effective?.routing ?? '');
    } catch {
      return false;
    }
  }

  presenceFromMemory(viewerPid, targetPid, allowed) {
    if (!allowed) return offlinePresence(targetPid);
    const active = this.active.get(targetPid);
    const now = time(this.lobby);
    const memberships = [...this.lobby.games.values()].filter((candidate) =>
      candidate.slots.some((slot) => slot.pid === targetPid && slot.state === 'player'));
    const game = memberships.find((candidate) => candidate.state === 'open' && active &&
      now - active.lastRequest <= 150) ?? memberships.find((candidate) => candidate.state === 'started' &&
      !candidate.finished && !this.leftStartedGames.has(`${candidate.id}:${targetPid}`));
    if (game?.state === 'started' && !game.finished) {
      if (!active && !this.freshStartedGames.has(game.id)) return offlinePresence(targetPid);
      const loading = this.loadingUntil.get(game.id) ?? 0;
      const state = now < loading ? 'loading' : 'playing';
      return { pid: targetPid, state, game: game.id, joinable: false, since: active?.since ?? Math.floor(game.startedAt / 1000), map: game.setup.map, mode: game.mode ?? null };
    }
    if (!active || now - (active.lastRequest ?? 0) > 150) return offlinePresence(targetPid);
    if (game?.state === 'open') {
      return { pid: targetPid, state: 'in_room', game: game.id, joinable: this.roomJoinable({ pid: viewerPid }, game), since: active.since, map: game.setup.map, mode: game.mode ?? null };
    }
    const age = now - active.lastActivity;
    const state = age >= 600 ? 'away' : active.state;
    return { pid: targetPid, state, game: null, joinable: false, since: active.since, map: null, mode: null };
  }

  roomJoinable(viewer, game) {
    if (game.state !== 'open' || !game.slots.some((slot) => slot.state === 'open')) return false;
    const now = time(this.lobby);
    const invite = [...this.pendingInvites.values()].some((entry) => entry.to === viewer.pid && entry.game === game.id &&
      entry.expires > now && game.slots.some((slot) => slot.state === 'player' && slot.pid === entry.from));
    return !(game.password || game.roomCode) || invite;
  }

  async wakeFriends(pidValue) {
    this.inboxBuildGeneration += 1;
    for (const owner of [...(this.inboxProfileDependents.get(pidValue) ?? [])]) this.wakeInbox(owner);
    this.wakeInbox(pidValue);
  }

  statsChanged() {
    this.inboxBuildGeneration += 1;
    for (const pidValue of this.inboxSnapshots.keys()) this.wakeInbox(pidValue);
  }

  wakeInbox(pidValue) {
    this.inboxBuildGeneration += 1;
    if (this.inboxSnapshots.has(pidValue) || this.inboxPidHolds.has(pidValue)) {
      this.lobby.wake(`inbox:${pidValue}`, { invalidateStats: false });
    }
  }

  cancelInvites(a, b) {
    const pair = pairKey(a, b);
    for (const [id, invite] of this.pendingInvites) if (pairKey(invite.from, invite.to) === pair) this.pendingInvites.delete(id);
  }
}

function randomRecovery(randomBytes) {
  const bytes = randomBytes(10);
  if (!(bytes instanceof Uint8Array) || bytes.length !== 10) throw new Error('social random source returned an invalid recovery buffer');
  let value = 0n;
  for (const byte of bytes) value = (value << 8n) | BigInt(byte);
  let out = '';
  for (let i = 0; i < 16; i++) {
    out = CROCKFORD[Number(value & 31n)] + out;
    value >>= 5n;
  }
  return out;
}

function takeAttempts(tx, key, now, limit, windowSeconds) {
  const previous = tx.get(key);
  if (previous !== null && (!previous || typeof previous !== 'object' || Array.isArray(previous) ||
      (previous.attempts !== undefined && (!Array.isArray(previous.attempts) ||
        previous.attempts.some((at, index) => !Number.isSafeInteger(at) || at < 0 || at > now ||
          (index > 0 && at < previous.attempts[index - 1])))))) throw new SocialStorageError('unavailable');
  const recent = (previous?.attempts ?? [])
    .filter((at) => at > now - windowSeconds);
  if (recent.length >= limit) return { retry: Math.max(1, recent[0] + windowSeconds - now) };
  recent.push(now);
  const expiry = recent.at(-1) + windowSeconds;
  const index = `quota-expire:${String(expiry).padStart(16, '0')}:${encodeURIComponent(key)}`;
  if (previous?.index) tx.delete(previous.index);
  tx.put(key, { attempts: recent, window_seconds: windowSeconds, index });
  tx.put(index, { key, expires: expiry });
  return { retry: 0 };
}

function setExpiring(tx, key, value, expiry) {
  const previous = tx.get(key);
  if (previous?.index) tx.delete(previous.index);
  const index = `quota-expire:${String(expiry).padStart(16, '0')}:${encodeURIComponent(key)}`;
  tx.put(key, { ...value, until: expiry, index });
  tx.put(index, { key, expires: expiry });
}

function cleanExpiredQuotas(tx, now) {
  for (const [index, entry] of tx.list('quota-expire:', { limit: 32 })) {
    if (!entry || typeof entry.key !== 'string' ||
        !(entry.key.startsWith('quota:') || /^invite-suppress:[0-9a-f]{16}:[0-9a-f]{16}$/.test(entry.key) || /^moved:[0-9a-f]{32}$/.test(entry.key)) ||
        !Number.isSafeInteger(entry.expires) || entry.expires < 0) throw new SocialStorageError('unavailable');
    if (entry.expires > now) break;
    const current = tx.get(entry.key);
    if (current?.index === index) {
      if (Number.isSafeInteger(current.until) && current.until > now) {
        const expiry = current.until;
        const nextIndex = `quota-expire:${String(expiry).padStart(16, '0')}:${encodeURIComponent(entry.key)}`;
        tx.put(entry.key, { ...current, index: nextIndex });
        tx.put(nextIndex, { key: entry.key, expires: expiry });
      } else if (Array.isArray(current.attempts)) {
        const windowSeconds = Number.isSafeInteger(current.window_seconds)
          ? current.window_seconds
          : quotaWindow(entry.key);
        const attempts = current.attempts.filter((at) => Number.isSafeInteger(at) && at + windowSeconds > now);
        if (!attempts.length) {
          tx.delete(entry.key);
        } else {
          const expiry = attempts.at(-1) + windowSeconds;
          const nextIndex = `quota-expire:${String(expiry).padStart(16, '0')}:${encodeURIComponent(entry.key)}`;
          tx.put(entry.key, { ...current, attempts, window_seconds: windowSeconds, index: nextIndex });
          tx.put(nextIndex, { key: entry.key, expires: expiry });
        }
      } else {
        tx.delete(entry.key);
      }
    }
    tx.delete(index);
  }
}

function cleanExpiredStartedGames(tx, now) {
  for (const [index, entry] of tx.list('recent-started-expire:', { limit: 32 })) {
    if (!entry || !GAME_ID.test(entry.game ?? '') || !Number.isSafeInteger(entry.expires) || entry.expires < 0) {
      throw new SocialStorageError('unavailable');
    }
    if (entry.expires > now) break;
    const marker = tx.get(`recent-started:${entry.game}`);
    if (marker?.index === index && marker.expires === entry.expires) tx.delete(`recent-started:${entry.game}`);
    tx.delete(index);
  }
}

function quotaWindow(key) {
  if (key.includes(':minute:') || key.startsWith('quota:invite-pid:') || key.startsWith('quota:invite-ip:') ||
      key.startsWith('quota:request-code-')) return 60;
  if (key.startsWith('quota:request-ip-hour:') || key.startsWith('quota:request-pid:') ||
      key.startsWith('quota:recovery-') || key.startsWith('quota:invite-decline:')) return 3600;
  return 86400;
}

function readQuery(query, allowed) {
  const result = {};
  for (const key of query.keys()) {
    if (!allowed.includes(key)) fail(400, 'bad_query', 'the query contains an unknown parameter');
    const values = query.getAll(key);
    if (values.length !== 1) fail(400, 'bad_query', 'a query parameter must appear once');
    result[key] = values[0];
  }
  return result;
}

function removeRequest(tx, request) {
  tx.delete(`request:${request.id}`);
  tx.delete(`request-in:${request.to}:${request.id}`);
  tx.delete(`request-out:${request.from}:${request.id}`);
  tx.delete(`request-pair:${pairKey(request.from, request.to)}:${request.id}`);
}

function cancelRequests(tx, a, b) {
  for (const [key] of tx.list(`request-pair:${pairKey(a, b)}:`, { limit: 10 })) {
    const id = key.slice(key.lastIndexOf(':') + 1);
    const request = tx.get(`request:${id}`);
    if (request) removeRequest(tx, request);
  }
}

function addNotice(service, pidValue, notice) {
  if (!service.notices.has(pidValue) && service.notices.size >= MAX_NOTICE_PLAYERS) {
    service.notices.delete(service.notices.keys().next().value);
  }
  const notices = service.notices.get(pidValue) ?? [];
  notices.push({ id: hex(service.lobby.env.randomBytes(8)), ...notice });
  if (notices.length > 50) notices.splice(0, notices.length - 50);
  service.notices.set(pidValue, notices);
}

function offlinePresence(pidValue) {
  return { pid: pidValue, state: 'offline', game: null, joinable: false, since: 0, map: null, mode: null };
}

function visibleActivityState(active, now) {
  if (!active || now - active.lastRequest > 150) return 'offline';
  if (now - active.lastActivity >= 600) return 'away';
  return active.state;
}

function validatePlayer(value, expectedPid) {
  if (!value || typeof value !== 'object' || value.pid !== expectedPid || !PID.test(value.pid) ||
      !INSTALL.test(value.install ?? '') || (value.device !== null && !DEVICE.test(value.device ?? '')) ||
      typeof value.nick !== 'string' || !value.nick.length || Array.from(value.nick).length > 24 ||
      /[\u0000-\u001f\u007f]/u.test(value.nick) || !value.nick.isWellFormed() ||
      typeof value.code !== 'string' || !/^[0-9A-HJKMNP-TV-Z]{10}$/.test(value.code) ||
      !Number.isSafeInteger(value.generation) || value.generation < 1 || typeof value.hidden !== 'boolean' ||
      (value.recovery_hash !== undefined && !/^[0-9a-f]{64}$/.test(value.recovery_hash)) ||
      !Number.isSafeInteger(value.created_at) || value.created_at < 0 ||
      !Number.isSafeInteger(value.updated_at) || value.updated_at < value.created_at ||
      !value.privacy || typeof value.privacy !== 'object' || Array.isArray(value.privacy) ||
      Object.keys(value.privacy).sort().join(',') !== 'challenges,invites,presence,requests' ||
      !['friends_or_room', 'friends'].includes(value.privacy.invites) ||
      !['friends_or_room', 'friends'].includes(value.privacy.challenges) ||
      !['anyone', 'nobody'].includes(value.privacy.requests) ||
      !['friends', 'nobody'].includes(value.privacy.presence)) {
    throw new SocialStorageError('unavailable');
  }
  return value;
}

function validateCounter(value, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) throw new SocialStorageError('unavailable');
  return value;
}

function validateFriendEdge(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'since' || !Number.isSafeInteger(value.since) || value.since < 0) {
    throw new SocialStorageError('unavailable');
  }
  return value;
}

function validateBlockEdge(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'at,note' ||
      typeof value.note !== 'string' || Array.from(value.note).length > 200 ||
      /[\u0000-\u001f\u007f]/u.test(value.note) || !value.note.isWellFormed() ||
      !Number.isSafeInteger(value.at) || value.at < 0) throw new SocialStorageError('unavailable');
  return value;
}

function validateRequest(value, expectedId = null) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'at,from,id,to' ||
      !REQUEST_ID.test(value.id) || (expectedId !== null && value.id !== expectedId) ||
      !PID.test(value.from) || !PID.test(value.to) || value.from === value.to ||
      !Number.isSafeInteger(value.at) || value.at < 0) throw new SocialStorageError('unavailable');
  return value;
}

function validateThread(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'last_id,read_id') throw new SocialStorageError('unavailable');
  const lastId = validateCounter(value.last_id);
  const readId = validateCounter(value.read_id, lastId);
  return { last_id: lastId, read_id: readId };
}

function validateConversation(tx, pair, value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).join(',') !== 'last_id') throw new SocialStorageError('unavailable');
  const lastId = validateCounter(value.last_id);
  if (lastId > 0) {
    const keyId = String(lastId).padStart(16, '0');
    const message = tx.get(`message:${pair[0]}:${pair[1]}:${keyId}`);
    validateMessage(message, keyId, pair);
  }
  return lastId;
}

function validateReadState(tx, owner, peer, lastId, threadValue) {
  const thread = threadValue === null ? null : validateThread(threadValue);
  if ((thread && thread.last_id !== lastId) || (!thread && lastId > 0)) throw new SocialStorageError('unavailable');
  const stored = tx.get(`read:${owner}:${peer}`);
  const readId = stored === null ? (thread?.read_id ?? 0) : validateCounter(stored, lastId);
  if (thread && thread.read_id !== readId) throw new SocialStorageError('unavailable');
  return readId;
}

function validateRecent(key, owner, value) {
  if (typeof key !== 'string' || !key.startsWith(`recent:${owner}:`) ||
      !value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'at,game,players' ||
      !GAME_ID.test(value.game ?? '') || !Number.isSafeInteger(value.at) || value.at < 0 ||
      !key.endsWith(`:${String(value.at).padStart(16, '0')}:${value.game}`) ||
      !Array.isArray(value.players) || value.players.length > 7) throw new SocialStorageError('unavailable');
  for (const player of value.players) {
    if (!player || typeof player !== 'object' || Array.isArray(player) ||
        Object.keys(player).sort().join(',') !== 'code,nick,pid,target' ||
        !PID.test(player.pid ?? '') || player.pid === owner ||
        typeof player.nick !== 'string' || !player.nick.length || Array.from(player.nick).length > 24 ||
        /[\u0000-\u001f\u007f]/u.test(player.nick) || !player.nick.isWellFormed() ||
        typeof player.code !== 'string' || !/^[0-9A-HJKMNP-TV-Z]{10}$/.test(player.code) ||
        typeof player.target !== 'string' || !player.target.length || Array.from(player.target).length > 128 ||
        !player.target.isWellFormed()) throw new SocialStorageError('unavailable');
  }
  return value;
}

function validateMessage(value, keyId, pair) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      !Number.isSafeInteger(value.id) || value.id < 1 ||
      String(value.id).padStart(16, '0') !== keyId ||
      !pair.includes(value.from) || !pair.includes(value.to) || value.from === value.to ||
      typeof value.text !== 'string' || !value.text.length || Array.from(value.text).length > 1000 ||
      encoder.encode(value.text).length > 4000 || /[\u0000-\u001f\u007f]/u.test(value.text) ||
      !value.text.isWellFormed() || !Number.isSafeInteger(value.at) || value.at < 0 ||
      typeof value.client_id !== 'string' || !CLIENT_ID.test(value.client_id)) {
    throw new SocialStorageError('unavailable');
  }
  return value;
}

function validateStoredReceipt(tx, value, sender, receiver, clientId, pair) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'message,text' || typeof value.text !== 'string') {
    throw new SocialStorageError('unavailable');
  }
  const keyId = String(value.message?.id ?? '').padStart(16, '0');
  const message = validateMessage(value.message, keyId, pair);
  const conversation = tx.get(`conversation:${pair[0]}:${pair[1]}`);
  if (message.from !== sender || message.to !== receiver || message.client_id !== clientId ||
      message.text !== value.text || !conversation ||
      validateConversation(tx, pair, conversation) < message.id) {
    throw new SocialStorageError('unavailable');
  }
  const storedMessage = tx.get(`message:${pair[0]}:${pair[1]}:${keyId}`);
  if (!storedMessage || serial(storedMessage) !== serial(message)) throw new SocialStorageError('unavailable');
  return message;
}

function parseBoundedLimit(value, min, max, name) {
  if (typeof value !== 'string' || !/^\d{1,3}$/.test(value)) fail(400, 'bad_query', `${name} must be an integer from ${min} through ${max}`);
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < min || parsed > max) fail(400, 'bad_query', `${name} must be an integer from ${min} through ${max}`);
  return parsed;
}

function parseBoundedInteger(value, min, max, name) {
  if (typeof value !== 'string' || !/^\d{1,16}$/.test(value)) fail(400, 'bad_query', `${name} is invalid`);
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < min || parsed > max) fail(400, 'bad_query', `${name} is invalid`);
  return parsed;
}

function secondsToUtcDay(now) {
  return Math.max(1, Math.floor((Date.parse(`${new Date(now * 1000).toISOString().slice(0, 10)}T00:00:00Z`) + 86400000) / 1000) - now);
}
