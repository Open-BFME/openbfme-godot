// OpenBFME online lobby. GPL-3.0.
//
// The lobby's logic (lane ONLINE-2, docs/ONLINE.md phase O1), without a platform: the Cloudflare Worker + Durable Object (worker.mjs) and the self-hosted
// Node server (server.mjs) both wrap it. It holds the game list, each game's slots, ready flags and chat, builds the START (engine/src/GameNetwork/
// OnlineStart.h is its reader), signs the relay tickets (engine/src/GameNetwork/RelayProtocol.h: the same 32 bytes) and keeps the relay's command logs.
//
// Wire: JSON over HTTP, every body carries "v": 1. Sign-in gives a bearer token (a signed guest identity; ONLINE-2 r2 dropped Discord sign-in until its
// return link to the game is built: Sol r1). Clients
// poll GET /v1/games/<id>?since=<seq> about once a second (WebSocket push is the next step, docs/ONLINE.md).
// ONLINE-5: a poll may wait for a change (long polling): GET /v1/games/<id>?since=<chat seq>&seq=<view seq>&wait=<seconds> is answered at once when the
// game's seq is not <view seq>, else when the game next changes or after min(wait, MAX_WAIT_S) seconds. Every poll answer carries "wait": MAX_WAIT_S, by
// which a client knows the lobby holds polls (an older lobby has no such field and is polled once a second, as before; an older client sends no wait and
// is answered at once, as before; Sol r1: a poll not held because a bound is reached carries "backoff": seconds to wait). A poll's chat lines carry "you": true for the reader's own lines (a client that showed its line when it was typed
// does not show it twice).
//
// Platform hooks (`env`): hmac(keyBytes, dataBytes) -> Promise<Uint8Array> (HMAC-SHA256), randomBytes(n) -> Uint8Array, now() -> ms, store (games and
// blobs: saveGame / deleteGame / putBlob / getBlob), config (below). No secret is in this file: keys come from the environment / files.

import {
  chargeReportAttempt,
  cleanupReportData,
  endReportRoster,
  persistReportRoster,
  ReportError,
  readPlayerReport,
  readPlayerReports,
  submitPlayerReport,
} from './reports.mjs';
import { SocialError, SocialService, validPlayerName } from './social.mjs';
import { repairStoredText } from './stored-text.mjs';
import { SocialStorageError } from './social-store.mjs';
import { oauthConfigFromEnv } from './oauth.mjs';
import { emailErrorPage, emailRequestPage, emailVerifyPage, pageHeaders } from './email-pages.mjs';

export const VERSION = 1;
// UBERJOIN-1: the simulation profile a room runs (docs/ONLINE.md): "normal" (Faithful) or "uber" (the Uber performance mode). A room made by a client that
// predates the field has none and is "normal"; any other value is refused by name
export const MODES = ['normal', 'uber'];
// lane NET-SYNC: the game's network mode, signed into the START ticket (RelayProtocol.h TicketMode): 'classic' is today's
// client-scheduled path (the default, the fallback); 'canonical' = the relay keeps the beat, late-order policy B; 'canonical-equal'
// = the relay keeps the beat with policy A (equal delay for everyone, for the comparison runs)
export const NET_MODES = ['classic', 'canonical', 'canonical-equal'];
const NET_MODE_TICKET = { 'classic': 0, 'canonical': 1, 'canonical-equal': 3 }; // TICKET_MODE_CANONICAL | TICKET_MODE_EQUAL_DELAY
const TICKET_MODE_AUTH = 4; // lane AUTH-1 (RelayProtocol.h): the relay refuses this game's plain HELLOs
function modeOf(value, what) {
  if (value === undefined) return 'normal';
  if (typeof value !== 'string' || !MODES.includes(value)) throw bad('bad_field', `${what} must be one of ${MODES.join(', ')}`);
  return value;
}
// ROUTER-1: the canonical game's frame period (ms), signed into the ticket: 50 .. 200 ms (20 .. 5 logic Hz)
function frameMsOf(value, what) {
  if (value === undefined) return 200;
  return intIn(value, 50, 200, what);
}
function netModeOf(value, what) {
  if (value === undefined) return 'classic';
  if (typeof value !== 'string' || !NET_MODES.includes(value)) throw bad('bad_field', `${what} must be one of ${NET_MODES.join(', ')}`);
  return value;
}
export const MAX_JSON_BYTES = 16384;          // a JSON request body (ONLINE-2 r2: counted as received)
export const MAX_LOG_BYTES = 64 * 1024 * 1024; // a relay log upload
// lane PROFILE-2 (owner decision 10-26 "A"): a player's archive list (POST /v1/profile-archives): its profile identity's archive lines, named by their
// SHA-256 in a create / join ("archives") and in the replay START a checker reads. A pure 2.01 install is 213 lines, 15.4 KB as JSON; a mod adds its own
export const MAX_ARCHIVES_BYTES = 65536;
export const MAX_ARCHIVE_LINES = 1024;
// Sol review P2 (storage): an uploaded list waits in memory, the oldest dropped past this many bytes; it is stored only when a game naming it starts,
// so the store grows with started games (bounded like their logs), never with sign-ins
export const MAX_PENDING_ARCHIVE_BYTES = 16 * 1024 * 1024;
const ARCHIVE_LINE = /^[a-z0-9_]{1,16}:[^\u0000-\u001f]{1,200} [0-9a-f]{32} [0-9]{1,12}$/;
const ARCHIVES_ID = /^[0-9a-f]{64}$/;
// the request body bound of a path (the platforms read the body before the route runs)
export function bodyLimitFor(path) {
  if (path.startsWith('/v1/relay/logs/') || path.startsWith('/v1/admin/blobs/')) return MAX_LOG_BYTES;
  if (path === '/v1/profile-archives') return MAX_ARCHIVES_BYTES;
  return MAX_JSON_BYTES;
}
// lane PROFILE-2: a create / join body's optional "archives" (the id of a list the client uploaded); absent or null: none told (an older client)
function archivesOf(body) {
  const v = body?.archives;
  if (v === undefined || v === null) return null;
  if (typeof v !== 'string' || !ARCHIVES_ID.test(v)) throw bad('bad_field', 'archives must be the SHA-256 of the uploaded archive list (64 hex digits)');
  return v;
}
const LOG_HEADER_BYTES = 25; // RelayCore.h "Log format": magic 8, version 4, game id 8, slot mask 1, start 4
const MAX_SLOTS = 8; // a legacy (eight-start) room's places, the shape every client before P16 reads
// lane ONLINE16-FIX (P16, docs/ONLINE.md "Extended map capacity"): a map with 9 .. 16 starts makes a sixteen-place room (`setup.mapStartCount`,
// places beyond it closed). A client tells its width in the X-OpenBFME-Max-Slots header (exactly 8 or 16; absent: an older, eight-slot client);
// the same header marks a client of the AUTH-1 release line (both shipped together), which is what `setup.requireAuth` needs
const WIDE_SLOTS = 16;
const capacityOf = (g) => g.setup.mapStartCount ?? MAX_SLOTS;
const placesOf = (capacity) => (capacity > MAX_SLOTS ? WIDE_SLOTS : MAX_SLOTS);
const NEEDS_WIDE = 'this game needs 16-player support';
const NEEDS_AUTH = 'this game requires authenticated relay traffic: update OpenBFME to join it';
// the header's value: 8 or 16, anything else is refused by name
// BATCH-P4 (ONLINE16-FIX x ONLINEMENU-1): the client's width rides with its account for one request. A copy keeps every property, the
// non-enumerable ones too (the social poll cache's pollSecurity: a plain spread dropped it and a cached poll went back to SQL)
function withMaxSlots(account, maxSlots) {
  return Object.defineProperties(Object.create(Object.getPrototypeOf(account)), {
    ...Object.getOwnPropertyDescriptors(account),
    maxSlots: { value: maxSlots, enumerable: true, writable: true, configurable: true },
  });
}
export function maxSlotsOf(headers) {
  const v = headers?.['x-openbfme-max-slots'];
  if (v === undefined || v === null) return MAX_SLOTS;
  if (v === '8') return MAX_SLOTS;
  if (v === '16') return WIDE_SLOTS;
  throw bad('bad_field', 'X-OpenBFME-Max-Slots must be 8 or 16');
}
const MINE_LEFT_MAX = 32; // ONLINEUX-1 r2: the game client's cap of a /v1/mine list (OnlineLobbyClient::kMaxMine)
// ONLINE-5: long polling. A held poll is one open request and one timer: bounded per game (8 players, each one poll, and a second one while the first
// is being replaced) and in total; beyond the bounds a poll is answered at once (the client's next poll waits)
export const MAX_WAIT_S = 25;
export const MAX_WAITERS_PER_GAME = 16;
export const MAX_WAITERS = 4000;
// Sol r1 (ONLINE-5): the holds are shared fairly: one account (a player, any number of windows) has at most MAX_HOLDS_PER_ACCOUNT at once (its poll,
// and the next one while the last is being replaced), so it cannot take a room's holds from the other players. A poll that is not held because a
// bound is reached is answered at once with "backoff": HOLD_BACKOFF_S, the seconds the client waits before its next poll (an explicit back-off: without
// it a client that is never held would poll back to back, about 3,600 requests an hour)
export const MAX_HOLDS_PER_ACCOUNT = 2;
export const HOLD_BACKOFF_S = 10;
// ONLINECOUNT-1: GET /v1/stats. A player counts while the lobby has seen a request with their valid session token in the last PRESENCE_S seconds
// (the online menu's own stats poll every 90 s keeps them present; a staging room's polls do too), or while they hold a human slot of a game that has started and has no relay log yet
// (the lobby hears of a game's end only through the relay's log upload, so a game whose log never arrives counts for startedGameKeepMs). Nothing is
// stored: presence lives in memory and a restarted lobby counts from zero again. The answer is computed at most every STATS_CACHE_S seconds.
export const STATS_STOP = '[S-4830] players online = accounts whose token was seen in the last 150 s plus the humans of started games whose relay log has not been uploaded (3 h at most): an estimate, not a connection count';
// LOBBYSTATS-1: games per day. A game counts as started when its replay START record (starts/<id>.json) is stored and as finished when the relay's log
// of it (logs/<id>.obfmerlog) arrives, both at the time the store wrote the record: counts only, from records the lobby keeps anyway (no write of its
// own). The times are read from the store once (store.gameTimes) and then kept in memory as the lobby writes those records; a store without
// gameTimes (R2, none) answers null. A restore (the admin import) writes its records at the time of the import, so they count on that day
export const GAME_DAYS = 7;
const DAY_MS = 24 * 3600 * 1000;
export const PRESENCE_S = 150;
export const STATS_CACHE_S = 30;
export const MAX_PRESENT = 50000; // the presence table's bound (sign-ins are rate-limited per address; past it a new session is not remembered)
export const MAX_SIGHTINGS = 50000;
export const MAX_SIGHTING_CLEANUP = 100;
const SIGHTING_TTL_S = 365 * 24 * 60 * 60;
const PLAYER_PAGE = 50;
const ENFORCEMENT_PAGE = 50;
const MAX_ROUTING_BYTES = 64;
const SIGHTING_CLEANUP_INTERVAL_S = 60;
const MAX_REASON_CHARS = 1000;
const MAX_REASON_BYTES = 4000;
const MAX_MOD_MESSAGE_BYTES = 8192;
const MAX_NOTICE_TEXT_BYTES = 4000;
const MAX_NOTICE_ID_BYTES = 128;
const SLOT_STATES = ['open', 'closed', 'player', 'easy', 'medium', 'hard', 'brutal'];
const AI_STATES = new Set(['easy', 'medium', 'hard', 'brutal']);
// ONLINE-5 (Sol r1): the choices of a slot no one holds: an open or closed slot keeps none (they would come back with the next occupant)
// ONLINEHUB-1: whole-string, constant-time comparison of a presented secret (a non-string never matches)
function sameSecret(presented, secret) {
  if (typeof presented !== 'string' || presented.length !== secret.length) return false;
  let diff = 0;
  for (let i = 0; i < secret.length; i++) diff |= presented.charCodeAt(i) ^ secret.charCodeAt(i);
  return diff === 0;
}
const ROOM_CODE = /^[A-Za-z0-9_-]{6,32}$/; // ONLINEHUB-1: a private room's code (docs/PLATFORM_API.md)
// PLATFORMICON-1: the one short value a client says about its system, shown beside its name (display only: nothing else about the machine). Absent
// (an older client) or null is no icon; anything else is refused by name
export const PLATFORMS = Object.freeze(['windows', 'linux', 'macos']);
const NO_CHOICES = Object.freeze({ faction: -1, color: -1, startPos: -1, team: -1, handicap: 0, cah: null, link: null });
// ONLINEMENU-1: a player's Create-a-Hero record (the .cah wire form, GameNetwork/GameInfo.h kMaxCreateAHeroBytes), standard base64 with padding
const MAX_CAH_BYTES = 4096;
// INFRA-2: the admin backup's signature window, page size and the blob keys it moves (the three kinds the lobby stores per started game)
export const ADMIN_WINDOW_S = 300;
export const ADMIN_PAGE = 1000;
const BLOB_KEY = /^(?:logs\/[0-9a-f]{16}\.obfmerlog|(?:starts|players)\/[0-9a-f]{16}\.json|archives\/[0-9a-f]{64}\.json)$/; // lane PROFILE-2: archives
export function parseBlobKey(key) {
  return BLOB_KEY.test(key) ? { kind: key.split('/')[0], id: key.slice(key.indexOf('/') + 1, key.lastIndexOf('.')) } : null; // a game id, or (archives) a SHA-256
}

// ---- bytes and encodings ---------------------------------------------------------------------------------------------------------------------------------
export function hexToBytes(hex) {
  if (typeof hex !== 'string' || hex.length % 2 !== 0 || /[^0-9a-fA-F]/.test(hex)) throw new Error('not hex');
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(2 * i, 2), 16);
  return out;
}
export function bytesToHex(b) {
  return Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
}
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
export function base64(bytes, url = false) {
  let s = '';
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    s += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + B64[v & 63];
  }
  if (bytes.length - i === 1) {
    const v = bytes[i] << 16;
    s += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + (url ? '' : '==');
  } else if (bytes.length - i === 2) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    s += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + (url ? '' : '=');
  }
  return url ? s.replace(/\+/g, '-').replace(/\//g, '_') : s;
}
export function base64UrlDecode(text) {
  if (typeof text !== 'string' || /[^A-Za-z0-9_-]/.test(text) || text.length % 4 === 1) throw new Error('not base64url');
  const out = [];
  let acc = 0;
  let bits = 0;
  for (const c of text.replace(/-/g, '+').replace(/_/g, '/')) {
    acc = (acc << 6) | B64.indexOf(c);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push((acc >> bits) & 0xff);
    }
    acc &= (1 << bits) - 1;
  }
  if (acc !== 0) throw new Error('not canonical base64url');
  return new Uint8Array(out);
}
const enc = new TextEncoder();
const dec = new TextDecoder();

// "keyId:hex" lines, as RelayProtocol::parseKeyRing reads them (the relay gets the same file)
export function parseKeyRing(text) {
  const ring = new Map();
  for (const [n, raw] of text.split('\n').entries()) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const m = /^(\d{1,3}):([0-9a-fA-F]+)$/.exec(line);
    if (!m || Number(m[1]) > 255 || m[2].length % 2 || m[2].length < 32) throw new Error(`key line ${n + 1}: wants keyId:hex (16+ bytes)`);
    if (ring.has(Number(m[1]))) throw new Error(`key id ${m[1]} twice`);
    ring.set(Number(m[1]), hexToBytes(m[2]));
  }
  if (!ring.size) throw new Error('the key ring is empty');
  return ring;
}

// ---- the relay ticket (RelayProtocol.h): u8 1, u8 key id, u64 game id, u8 slot, u8 slot mask, u32 expiry, 16 bytes of HMAC-SHA256 ------------------------
export async function signTicket(env, keyId, key, gameIdHex, slot, mask, expires, mode = 0, frameMs = 0) {
  // lane NET-SYNC: a mode-flagged START ticket is version 7 (the version-4 layout plus u8 mode + u16 frameMs: 36 bytes)
  if (mode) {
    const b = new Uint8Array(36);
    b[0] = 7;
    b[1] = keyId;
    const id7 = hexToBytes(gameIdHex);
    for (let i = 0; i < 8; i++) b[2 + i] = id7[7 - i];
    b[10] = slot;
    b[11] = mask;
    b[12] = mask >>> 8;
    b[13] = mode;
    new DataView(b.buffer).setUint32(14, expires >>> 0, true);
    new DataView(b.buffer).setUint16(18, frameMs >>> 0, true); // ROUTER-1
    const mac = await env.hmac(key, b.subarray(0, b.length - 16));
    b.set(mac.subarray(0, 16), b.length - 16);
    return base64(b, true);
  }
  const wide = mask > 0xff;
  const b = new Uint8Array(wide ? 33 : 32);
  b[0] = wide ? 4 : 1;
  b[1] = keyId;
  const id = hexToBytes(gameIdHex); // big-endian text -> little-endian u64
  for (let i = 0; i < 8; i++) b[2 + i] = id[7 - i];
  b[10] = slot;
  b[11] = mask;
  const extra = wide ? 1 : 0;
  if (wide) b[12] = mask >>> 8;
  new DataView(b.buffer).setUint32(12 + extra, expires >>> 0, true);
  const mac = await env.hmac(key, b.subarray(0, b.length - 16));
  b.set(mac.subarray(0, 16), b.length - 16);
  return base64(b, true);
}

// lane ONLINE-3: the rejoin ticket (RelayProtocol.h version 2, 34 bytes): u8 2, the same fields, u16 generation (>= 1), then 16 bytes of HMAC-SHA256
// over the 18 bytes before. A HELLO with a newer generation binds the slot's new incarnation at the relay without the old session token
export async function signRejoinTicket(env, keyId, key, gameIdHex, slot, mask, expires, generation) {
  const wide = mask > 0xff;
  const b = new Uint8Array(wide ? 35 : 34);
  b[0] = wide ? 5 : 2;
  b[1] = keyId;
  const id = hexToBytes(gameIdHex);
  for (let i = 0; i < 8; i++) b[2 + i] = id[7 - i];
  b[10] = slot;
  b[11] = mask;
  const extra = wide ? 1 : 0;
  if (wide) b[12] = mask >>> 8;
  const v = new DataView(b.buffer);
  v.setUint32(12 + extra, expires >>> 0, true);
  v.setUint16(16 + extra, generation, true);
  const mac = await env.hmac(key, b.subarray(0, b.length - 16));
  b.set(mac.subarray(0, 16), b.length - 16);
  return base64(b, true);
}

// lane ONLINE-3: the spectator ticket (RelayProtocol.h version 3, 34 bytes): u8 3, the same fields with the spectator index as the slot byte, u16 the
// spectator delay in seconds, then 16 bytes of HMAC-SHA256 over the 18 bytes before
export async function signSpectatorTicket(env, keyId, key, gameIdHex, index, mask, expires, delaySeconds) {
  const b = base64UrlDecode(await signRejoinTicket(env, keyId, key, gameIdHex, index, mask, expires, delaySeconds));
  b[0] = mask > 0xff ? 6 : 3;
  const mac = await env.hmac(key, b.subarray(0, b.length - 16));
  b.set(mac.subarray(0, 16), b.length - 16);
  return base64(b, true);
}

// lane ANTICHEAT-1: the session key of a ticket (RelayProtocol.h "Signed input"): the first 16 bytes of HMAC-SHA256(the ticket's key,
// "OBFME-SESSION-1" || the ticket's bytes), base64url. ONLINE16-FIX (one packet-authentication scheme): the START's session_key; the client and the
// relay derive AUTH-1's direction keys from it (RelayAuth::sessionKey); it never crosses UDP. Old clients ignore the field
export async function sessionKey(env, key, ticketText) {
  const label = new TextEncoder().encode('OBFME-SESSION-1');
  const ticket = base64UrlDecode(ticketText);
  const m = new Uint8Array(label.length + ticket.length);
  m.set(label, 0);
  m.set(ticket, label.length);
  return base64((await env.hmac(key, m)).subarray(0, 16), true);
}

// ---- errors ----------------------------------------------------------------------------------------------------------------------------------------------
export class LobbyError extends Error {
  constructor(status, code, message, details = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details;
  }
}
const bad = (code, msg) => new LobbyError(400, code, msg);
function restoreGame(target, snapshot) {
  for (const key of Object.keys(target)) delete target[key];
  Object.assign(target, snapshot);
}

// ONLINEMENU-1: null (no hero) or the canonical standard base64 of 1 .. MAX_CAH_BYTES bytes. The record's fields are checked by every client (OnlineStart
// reads it with SkirmishGameSlot::setCreateAHeroBytes, the game start with CreateAHeroSystem::validateHero): the lobby only bounds and carries it
function checkCah(v) {
  if (v === null) return null;
  if (typeof v !== 'string' || v.length < 4 || v.length > 4 * Math.ceil(MAX_CAH_BYTES / 3) || v.length % 4 !== 0 || !/^[A-Za-z0-9+/]+={0,2}$/.test(v)) {
    throw bad('bad_field', `cah must be null or the base64 of a Create-a-Hero record of 1 .. ${MAX_CAH_BYTES} bytes`);
  }
  let bytes;
  try {
    bytes = base64UrlDecode(v.replace(/=+$/, '').replace(/\+/g, '-').replace(/\//g, '_'));
  } catch {
    throw bad('bad_field', 'cah is not base64');
  }
  if (bytes.length < 1 || bytes.length > MAX_CAH_BYTES || base64(bytes) !== v) throw bad('bad_field', `cah must be the canonical base64 of 1 .. ${MAX_CAH_BYTES} bytes`);
  return v;
}
function intIn(v, lo, hi, what) {
  if (!Number.isInteger(v) || v < lo || v > hi) throw bad('bad_field', `${what} must be an integer in ${lo} .. ${hi}`);
  return v;
}
function boolOf(v, what) {
  if (typeof v !== 'boolean') throw bad('bad_field', `${what} must be true or false`);
  return v;
}
export function checkPlatform(v) {
  if (v === undefined || v === null) return undefined;
  if (typeof v !== 'string' || !PLATFORMS.includes(v)) throw bad('bad_field', `platform must be one of ${PLATFORMS.join(', ')} or absent`);
  return v;
}
// lane JOINVER-1 (the owner, 2026-10-23): the build a client tells at create / join: its release tag, build day, whether it is an official release
// (a packaged build with the release key: the client's claim, shown to people, never trusted for anything) and its system. A game keeps its host's;
// the list shows it and a join refused for a different build (409 profile) names both sides in plain words. Older clients tell none: the sign-in's
// client (version, platform) stands in.
export function checkBuild(v) {
  if (v === undefined || v === null) return null;
  if (typeof v !== 'object' || Array.isArray(v)) throw bad('bad_field', 'build must be an object');
  allowFields(v, ['version', 'date', 'official', 'platform'], 'build');
  const version = str(v.version, 64, 'build.version');
  if (!/^[0-9A-Za-z._+-]+$/.test(version)) throw bad('bad_field', 'build.version must be a version tag');
  const date = v.date === undefined || v.date === '' ? '' : str(v.date, 10, 'build.date');
  if (date && !/^\d{4}-\d{2}-\d{2}$/.test(date)) throw bad('bad_field', 'build.date must be YYYY-MM-DD');
  if (v.official !== undefined && typeof v.official !== 'boolean') throw bad('bad_field', 'build.official must be true or false');
  return { version, date, official: v.official ?? null, platform: checkPlatform(v.platform) ?? null };
}
function accountBuild(account) {
  const c = account?.client;
  return c && typeof c.version === 'string' ? { version: c.version, date: '', official: null, platform: knownPlatform(c.platform) } : null;
}
// "Nightly 0.4.12 (2026-10-22)", "Alpha 0.4.11": the names players read (engine BuildVersion::displayVersion, launcher semver.gd display())
export function buildName(b) {
  if (!b) return 'an unknown version';
  let m = /^v(\d+\.\d+\.\d+)-nightly\.(\d{4})(\d{2})(\d{2})$/.exec(b.version);
  let name = b.version;
  if (m) name = `Nightly ${m[1]} (${m[2]}-${m[3]}-${m[4]})`;
  else if ((m = /^v(\d+\.\d+\.\d+)$/.exec(b.version))) name = `Alpha ${m[1]}`;
  else if ((m = /^v(\d+\.\d+\.\d+)-alpha\.(\d+)$/.exec(b.version))) name = `v${m[1]} Alpha ${m[2]}`;
  else name = `a development build (${b.version})`;
  const system = { windows: 'Windows', linux: 'Linux', macos: 'macOS' }[b.platform] ?? '';
  return name + (b.official === true ? ', official' : b.official === false ? ', unofficial' : '') + (system ? `, ${system}` : '');
}
function str(v, maxLen, what, { empty = false } = {}) {
  if (typeof v !== 'string' || (!empty && !v.length) || enc.encode(v).length > maxLen) throw bad('bad_field', `${what} must be a string of ${empty ? 0 : 1} .. ${maxLen} bytes`);
  // no control characters (names and chat are shown in the game's UI)
  if (/[\u0000-\u001f\u007f]/.test(v)) throw bad('bad_field', `${what} has control characters`);
  // LOBBYSEC-1 (as ADMINFIX-1's live lobby a4953ead8): an unpaired UTF-16 surrogate is legal in a JSON escape but is not text (no UTF-8 form);
  // echoed by /v1/games it made the client's JSON parser refuse the whole list
  if (!v.isWellFormed()) throw bad('bad_field', `${what} is not well-formed Unicode`);
  return v;
}
function exactFields(value, fields, what) {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw bad('bad_field', `${what} must be an object`);
  const unknown = Object.keys(value).filter((key) => !fields.includes(key));
  if (unknown.length) throw bad('unknown_field', `${what} has unknown field ${unknown[0]}`);
  const missing = fields.filter((key) => !Object.hasOwn(value, key));
  if (missing.length) throw bad('missing_field', `${what} is missing required field ${missing[0]}`);
}
function allowFields(value, fields, what) {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw bad('bad_field', `${what} must be an object`);
  const unknown = Object.keys(value).filter((key) => !fields.includes(key));
  if (unknown.length) throw bad('unknown_field', `${what} has unknown field ${unknown[0]}`);
}
function boundedText(value, maxBytes, what, { empty = false } = {}) {
  if (typeof value !== 'string' || (!empty && !value.length) || enc.encode(value).length > maxBytes) {
    throw bad('bad_field', `${what} must be a string of ${empty ? 0 : 1} .. ${maxBytes} bytes`);
  }
  if (/[\u0000-\u0008\u000b-\u001f\u007f]/.test(value)) throw bad('bad_field', `${what} has control characters`);
  if (!value.isWellFormed()) throw bad('bad_field', `${what} is not well-formed Unicode`);
  return value;
}
function modReason(value, what = 'reason') {
  const text = boundedText(value, MAX_REASON_BYTES, what);
  if (Array.from(text).length > MAX_REASON_CHARS) throw bad('bad_field', `${what} must be at most ${MAX_REASON_CHARS} characters`);
  return text;
}
function storedModerationReceipt(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      !validSha256(value.fingerprint) || value.v !== VERSION) {
    throw new LobbyError(503, 'command_store_unavailable', 'moderation command receipts are unavailable');
  }
  if (value.status === 'pending' && Object.keys(value).sort().join(',') === 'fingerprint,status,v') return value;
  if (value.status === 'complete' && Object.keys(value).sort().join(',') === 'fingerprint,response,status,v' &&
      value.response && typeof value.response === 'object' && !Array.isArray(value.response) &&
      Object.keys(value.response).sort().join(',') === 'body,status' &&
      Number.isSafeInteger(value.response.status) && value.response.status >= 200 && value.response.status <= 599 &&
      value.response.body && typeof value.response.body === 'object' && !Array.isArray(value.response.body)) return value;
  throw new LobbyError(503, 'command_store_unavailable', 'moderation command receipts are unavailable');
}
function epochOrNull(value, what) {
  if (value === null) return null;
  if (!Number.isSafeInteger(value) || value < 0) throw bad('bad_field', `${what} must be null or non-negative unix seconds`);
  return value;
}
const validInstall = (value) => typeof value === 'string' && /^[0-9a-f]{32}$/.test(value);
const TOKEN_INSTALL_LABEL = 'openbfme-session-install-v1:';
const INSTALL_TAG_KEY = enc.encode('openbfme-install-tag-v1');
// ONLINEMENU-1: a session's public install tag (the token's `ih`): the first 16 bytes of HMAC-SHA256 keyed with the public INSTALL_TAG_KEY over the
// install id, in hex. It names the install without revealing it (OnlineLobbyClient::sessionMatchesInstall computes the same)
// ONLINEMENU-1 (PR #8 review, finding 2): the sign-in notice of an install whose player was restored on another install (social.mjs rebindPlayer),
// shown once by the game and the launcher like a moderation notice
// text cut to at most `chars` code points and `bytes` UTF-8 bytes; when cut, it ends with `tail` (which counts toward both limits)
function fitText(text, chars, bytes, tail) {
  const size = (value) => enc.encode(value).length;
  if (Array.from(text).length <= chars && size(text) <= bytes) return text;
  const points = Array.from(text);
  let keep = Math.min(points.length, chars - Array.from(tail).length);
  while (keep > 0 && size(points.slice(0, keep).join('') + tail) > bytes) keep -= 1;
  return points.slice(0, keep).join('') + tail;
}

export function movedNotice({ at, via }) {
  const how = { recovery: 'its recovery code', discord: 'a linked Discord account', email: 'its confirmed e-mail address' }[via] ?? 'a linked account';
  const when = new Date(at * 1000).toISOString().slice(0, 16).replace('T', ' ');
  return {
    id: `moved:${at}`,
    text: `Your online player was moved to another PC on ${when} UTC with ${how}, so this PC has a new player now. If that was not you, ` +
      (via === 'recovery' ? 'your friends list is on that PC now; report it to the OpenBFME team.'
        : 'sign in with your recovery code to take your player back, then unlink that account.'),
  };
}

export async function installTag(env, install) {
  return bytesToHex((await env.hmac(INSTALL_TAG_KEY, enc.encode(install))).subarray(0, 16));
}
const validDevice = (value) => typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
const validCommandId = (value) => typeof value === 'string' && /^[A-Za-z0-9_-]{8,64}$/.test(value);
const validSha256 = (value) => typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
const compareText = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
function validateEffectiveState(value, { stored = false } = {}) {
  const fields = ['v', 'version', 'block', 'mute_until', 'routing', 'notice', 'devices', 'block_devices'];
  allowFields(value, fields, 'enforcement');
  for (const field of ['v', 'version', 'block', 'mute_until', 'routing', 'notice']) {
    if (!Object.hasOwn(value, field)) throw bad('missing_field', `enforcement is missing required field ${field}`);
  }
  if (Object.hasOwn(value, 'block_devices') && typeof value.block_devices !== 'boolean') {
    throw bad('bad_field', 'block_devices must be a boolean');
  }
  // ONLINEMENU-1 (owner 2026-10-20: bans apply to the player, its linked identities and the hardware hash): a ban blocks the install's recent device
  // hashes by default; the console skips that with block_devices: false (its checkbox), and explicit devices name them instead
  const banDefault = !stored && value.block_devices === undefined && value.block?.kind === 'ban' &&
    (value.devices === undefined || (Array.isArray(value.devices) && value.devices.length === 0));
  const blockDevices = value.block_devices === true || banDefault;
  if (value.devices === undefined && !blockDevices) throw bad('missing_field', 'enforcement is missing required field devices');
  if (value.v !== VERSION) throw bad('bad_version', `v must be ${VERSION}`);
  if (!Number.isSafeInteger(value.version) || value.version < 1) throw bad('bad_field', 'version must be a positive safe integer');
  let block = null;
  if (value.block !== null) {
    allowFields(value.block, ['kind', 'until', 'reason', 'appeal'], 'block');
    for (const field of ['kind', 'until', 'reason']) {
      if (!Object.hasOwn(value.block, field)) throw bad('missing_field', `block is missing required field ${field}`);
    }
    if (value.block.kind !== 'ban' && value.block.kind !== 'suspend') throw bad('bad_field', 'block.kind must be ban or suspend');
    block = {
      kind: value.block.kind,
      until: epochOrNull(value.block.until, 'block.until'),
      reason: modReason(value.block.reason, 'block.reason'),
      appeal: value.block.appeal === undefined ? '' : boundedText(value.block.appeal, MAX_REASON_BYTES, 'block.appeal', { empty: true }),
    };
    const message = `${block.kind === 'ban' ? 'Access is unavailable' : 'Access is temporarily unavailable'}: ${block.reason}`;
    if (enc.encode(message).length > MAX_MOD_MESSAGE_BYTES) throw bad('bad_field', `the access message must be at most ${MAX_MOD_MESSAGE_BYTES} bytes`);
  }
  const muteUntil = epochOrNull(value.mute_until, 'mute_until');
  const routing = boundedText(value.routing, MAX_ROUTING_BYTES, 'routing', { empty: true });
  let notice = null;
  if (value.notice !== null) {
    exactFields(value.notice, ['id', 'text'], 'notice');
    notice = {
      id: boundedText(value.notice.id, MAX_NOTICE_ID_BYTES, 'notice.id'),
      text: boundedText(value.notice.text, MAX_NOTICE_TEXT_BYTES, 'notice.text'),
    };
  }
  const requestedDevices = value.devices === undefined ? [] : value.devices;
  if (!Array.isArray(requestedDevices) || requestedDevices.length > 5 || requestedDevices.some((device) => !validDevice(device))) {
    throw bad('bad_field', 'devices must contain at most five lowercase 64-digit hashes');
  }
  const devices = [...new Set(requestedDevices)].sort();
  if (devices.length !== requestedDevices.length) throw bad('bad_field', 'devices must not contain duplicates');
  if (blockDevices && block?.kind !== 'ban') throw bad('bad_field', 'block_devices requires a ban');
  if (blockDevices && devices.length && !stored) throw bad('bad_field', 'block_devices cannot be combined with explicit devices');
  if (stored && blockDevices && devices.length > 5) throw bad('bad_field', 'stored block_devices snapshot is invalid');
  return {
    v: VERSION, version: value.version, block, mute_until: muteUntil, routing, notice, devices,
    ...(blockDevices ? { block_devices: true } : {}),
  };
}
export function validateEnforcementRecord(value) {
  value = repairStoredText(value);
  try {
    allowFields(value, ['install', 'v', 'version', 'block', 'mute_until', 'routing', 'notice', 'devices', 'block_devices'], 'stored enforcement');
    if (!validInstall(value.install)) throw new Error('install must be 32 lowercase hex digits');
    const { install, ...state } = value;
    return { install, ...validateEffectiveState(state, { stored: true }) };
  } catch (error) {
    const install = validInstall(value?.install) ? ` for ${value.install}` : '';
    throw new Error(`lobby: stored enforcement${install} is malformed: ${error.message}`);
  }
}
export function knownPlatform(value) {
  return ['windows', 'linux', 'macos'].includes(value) ? value : null;
}

export function withoutGameIp(game) {
  const record = { ...game };
  delete record.ip;
  return record;
}

export function validateSighting(value) {
  const original = value;
  value = repairStoredText(value);
  const fields = ['install', 'nicks', 'first_seen', 'last_seen', 'signins', 'platform', 'version', 'devices'];
  try {
    exactFields(value, fields, 'stored sighting');
    if (!validInstall(value.install)) throw bad('bad_field', 'install must be 32 lowercase hex digits');
    if (!Number.isSafeInteger(value.first_seen) || value.first_seen < 0 || !Number.isSafeInteger(value.last_seen) ||
        value.last_seen < value.first_seen || !Number.isSafeInteger(value.signins) || value.signins < 1) {
      throw bad('bad_field', 'stored sighting timestamps and signins are invalid');
    }
    // LOBBYSEC-1: records stored before intake refused unpaired surrogates are repaired (U+FFFD), never a load failure
    if (value.platform !== null) boundedText(String(value.platform).toWellFormed(), 32, 'stored sighting platform');
    const version = value.version === null ? null : boundedText(String(value.version).toWellFormed(), 64, 'stored sighting version');
    if (!Array.isArray(value.nicks) || value.nicks.length < 1 || value.nicks.length > 10) throw bad('bad_field', 'stored sighting nicks are invalid');
    const repaired = new Set(original.nicks.filter((entry) => typeof entry?.nick === 'string' && !entry.nick.isWellFormed())
      .map((entry) => entry.nick.toWellFormed()));
    const nicks = value.nicks.map((entry) => {
      exactFields(entry, ['nick', 'first_seen', 'last_seen'], 'stored nickname');
      const nick = entry.nick;
      if (!validPlayerName(nick)) throw bad('bad_field', 'stored nickname must be 1 to 24 Unicode characters without controls or surrounding whitespace');
      if (!Number.isSafeInteger(entry.first_seen) || entry.first_seen < value.first_seen ||
          !Number.isSafeInteger(entry.last_seen) || entry.last_seen < entry.first_seen || entry.last_seen > value.last_seen) {
        throw bad('bad_field', 'stored nickname timestamps are invalid');
      }
      return { nick, first_seen: entry.first_seen, last_seen: entry.last_seen };
    });
    const mergedNicks = new Map();
    for (const entry of nicks) {
      const previous = mergedNicks.get(entry.nick);
      if (!previous) mergedNicks.set(entry.nick, entry);
      else {
        if (!repaired.has(entry.nick)) throw bad('bad_field', 'stored sighting nicks must be unique');
        previous.first_seen = Math.min(previous.first_seen, entry.first_seen);
        previous.last_seen = Math.max(previous.last_seen, entry.last_seen);
      }
    }
    if (!Array.isArray(value.devices) || value.devices.length > 5) throw bad('bad_field', 'stored sighting devices are invalid');
    const devices = value.devices.map((entry) => {
      exactFields(entry, ['hash', 'count', 'first_seen', 'last_seen'], 'stored device');
      if (!validDevice(entry.hash) || !Number.isSafeInteger(entry.count) || entry.count < 1 ||
          !Number.isSafeInteger(entry.first_seen) || entry.first_seen < value.first_seen ||
          !Number.isSafeInteger(entry.last_seen) || entry.last_seen < entry.first_seen || entry.last_seen > value.last_seen) {
        throw bad('bad_field', 'stored device fields are invalid');
      }
      return { hash: entry.hash, count: entry.count, first_seen: entry.first_seen, last_seen: entry.last_seen };
    });
    if (new Set(devices.map((entry) => entry.hash)).size !== devices.length) throw bad('bad_field', 'stored sighting devices must be unique');
    return {
      install: value.install,
      nicks: [...mergedNicks.values()],
      first_seen: value.first_seen,
      last_seen: value.last_seen,
      signins: value.signins,
      platform: knownPlatform(value.platform),
      version, // LOBBYSEC-1: the repaired value (Sol follow-up: it validated the repair but returned the original)
      devices,
    };
  } catch (error) {
    throw new Error(`lobby: stored sighting is malformed: ${error.message}`);
  }
}
function isActiveBlock(block, now) {
  return block && (block.until === null || block.until > now) ? block : null;
}
function sightingExpired(record, now) {
  return now - record.last_seen > SIGHTING_TTL_S;
}
function compareBlocks(a, b) {
  if (!a) return -1;
  if (!b) return 1;
  const rank = (block) => block.kind === 'ban' ? 2 : 1;
  if (rank(a) !== rank(b)) return rank(a) - rank(b);
  if (a.until === null && b.until !== null) return 1;
  if (a.until !== null && b.until === null) return -1;
  return (a.until ?? 0) - (b.until ?? 0);
}

// the game id a relay log names in its header (services/online/relay/RelayCore.h, "Log format": "OBFMERLG", u32 version, u64 game id, ...)
// DEPLOY-2: versions 1 and 2 (ONLINE-3's relay writes 2 with REJOINED records); version 4 adds the high slot-mask byte
function logGameOf(bytes) {
  if (bytes.length < LOG_HEADER_BYTES || dec.decode(bytes.subarray(0, 8)) !== 'OBFMERLG') throw bad('bad_log', 'not a relay log');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  const version = view.getUint32(8, true);
  if (version !== 1 && version !== 2 && version !== 4) throw bad('bad_log', `relay log version ${version} (this lobby reads versions 1, 2 and 4)`);
  if (version === 4 && bytes.length < 26) throw bad('bad_log', 'relay log header is truncated');
  return view.getBigUint64(12, true).toString(16).padStart(16, '0');
}

// ---- the lobby ---------------------------------------------------------------------------------------------------------------------------------------------
// config: { ticketKeys: Map, sessionKey: Uint8Array, uploadKey: Uint8Array|null, relays: [{ id, address }], runAhead, crcInterval, ticketSeconds,
//           publicUrl, limits }
export class Lobby {
  constructor(env, config) {
    this.env = env;
    this.config = {
      runAhead: 1, // lane NETLAG-1: RotWK's command delay, the current frame + 1 (was 2); the peers raise it on a slow link
      crcInterval: 100,
      ticketSeconds: 6 * 3600,
      sessionSeconds: 30 * 24 * 3600,
      requireInstall: false,
      openGameIdleMs: 10 * 60 * 1000,
      startedGameKeepMs: 3 * 3600 * 1000,
      maxGames: 2000,
      ...config,
    };
    if (!(this.config.ticketKeys instanceof Map) || !this.config.ticketKeys.size) throw new Error('lobby: no ticket keys (TICKET_KEYS)');
    if (!this.config.sessionKey || this.config.sessionKey.length < 16) throw new Error('lobby: no session key (SESSION_KEY, 16+ bytes of hex)');
    if (this.config.adminKey && this.config.modKey && bytesToHex(this.config.adminKey) === bytesToHex(this.config.modKey)) {
      throw new Error('lobby: ADMIN_KEY and MOD_KEY must be different');
    }
    if (this.config.enforcementCheckKey && this.config.enforcementCheckKey.length !== 32) {
      throw new Error('lobby: ENFORCEMENT_CHECK_KEY must be 32 bytes');
    }
    if (this.config.enforcementCheckKey && (
      (this.config.adminKey && bytesToHex(this.config.enforcementCheckKey) === bytesToHex(this.config.adminKey)) ||
      (this.config.modKey && bytesToHex(this.config.enforcementCheckKey) === bytesToHex(this.config.modKey))
    )) {
      throw new Error('lobby: ENFORCEMENT_CHECK_KEY must be different from ADMIN_KEY and MOD_KEY');
    }
    // CFDEPLOY-1: an empty RELAYS is allowed (the lobby can run before its relay exists): /v1/health says so and START is refused ('no_relay')
    if (!Array.isArray(this.config.relays)) throw new Error('lobby: RELAYS is not a list');
    for (const r of this.config.relays) {
      if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5}$/.test(r.address)) throw new Error(`lobby: relay address ${r.address} must be a.b.c.d:port`);
    }
    this.games = new Map(); // id -> game
    this.buckets = new Map(); // rate limit key -> { tokens, at }
    this.relayStatus = new Map(); // INFRA-2 r2: relay address -> its uploader's last status (relayStatus())
    this.waiters = new Map(); // ONLINE-5: game id -> Set of wake functions (held polls)
    this.waiterCount = 0;
    this.accountHolds = new Map(); // Sol r1: account id -> its held polls
    this.inboxPidHolds = new Map();
    this.present = new Map(); // ONLINECOUNT-1: account id -> the last time (ms) the lobby saw its token
    this.statsCache = null; // { at (ms), answer }
    this.gameTimes = null; // LOBBYSTATS-1: { started: [ms], finished: [ms] } since the first of the GAME_DAYS days, once read from the store
    this.enforcement = new Map();
    this.enforcementDeviceOwners = new Map();
    this.sightings = new Map();
    this.sightingDeviceOwners = new Map();
    this.lastSightingCleanupAt = null;
    this.sightingCleanupCursor = null;
    this.reportCleanupAt = null;
    this.reportCleanupCursor = { index: 0, after: null };
    this.reportLifecycleError = null;
    this.social = new SocialService(this);
  }

  identityStore() {
    return this.env.store.identity ?? null;
  }

  identityIndexed() {
    return this.identityStore() !== null;
  }

  getEnforcement(install) {
    const identity = this.identityStore();
    return identity ? identity.getEnforcement(install) : this.enforcement.get(install);
  }

  getSighting(install) {
    const identity = this.identityStore();
    return identity ? identity.getSighting(install) : this.sightings.get(install);
  }

  // ---- ONLINECOUNT-1: who is online ----
  seen(accountId) {
    const now = this.env.now();
    const previous = this.present.get(accountId);
    const wasCurrent = previous !== undefined && now - previous <= PRESENCE_S * 1000;
    if (!this.present.has(accountId) && this.present.size >= MAX_PRESENT) {
      for (const [k, t] of this.present) if (now - t > PRESENCE_S * 1000) this.present.delete(k);
      if (this.present.size >= MAX_PRESENT) return;
    }
    this.present.set(accountId, now);
    if (!wasCurrent) {
      this.social.statsChanged();
    }
  }
  // counts only: no names, no addresses, no ids in the answer
  stats({ fresh = false } = {}) {
    const now = this.env.now();
    if (!fresh && this.statsCache && now - this.statsCache.at >= 0 && now - this.statsCache.at < STATS_CACHE_S * 1000) return this.statsCache.answer;
    this.sweep();
    const online = new Set();
    for (const [id, t] of this.present) {
      if (now - t <= PRESENCE_S * 1000) online.add(id);
      else this.present.delete(id);
    }
    let open = 0;
    let running = 0;
    for (const g of this.games.values()) {
      if (g.state === 'open') open += 1;
      else if (g.state === 'started' && !g.finished) {
        running += 1;
        for (const s of g.slots) if (s.state === 'player' && s.account) online.add(s.account);
      }
    }
    const answer = { v: VERSION, players: online.size, games_open: open, games_running: running, ...this.gamesPerDay(now), as_of: Math.floor(now / 1000) };
    if (!fresh) this.statsCache = { at: now, answer };
    return answer;
  }
  // LOBBYSTATS-1: the last 24 hours, and the games started on each of the last GAME_DAYS UTC days (oldest first, today last, today so far)
  gamesPerDay(now) {
    const first = Math.floor(now / DAY_MS) * DAY_MS - (GAME_DAYS - 1) * DAY_MS;
    if (!this.gameTimes) {
      if (typeof this.env.store.gameTimes !== 'function') return { games_started_24h: null, games_finished_24h: null, games_started_by_day: null };
      const t = this.env.store.gameTimes(first);
      this.gameTimes = { started: [...t.started], finished: [...t.finished] };
    }
    const keep = (list) => list.filter((at) => at >= first);
    this.gameTimes.started = keep(this.gameTimes.started);
    this.gameTimes.finished = keep(this.gameTimes.finished);
    const since = now - DAY_MS;
    const byDay = Array.from({ length: GAME_DAYS }, (_, i) => ({ day: new Date(first + i * DAY_MS).toISOString().slice(0, 10), games: 0 }));
    for (const at of this.gameTimes.started) {
      const i = Math.floor((at - first) / DAY_MS);
      if (i < GAME_DAYS) byDay[i].games += 1;
    }
    return {
      games_started_24h: this.gameTimes.started.filter((at) => at > since && at <= now).length,
      games_finished_24h: this.gameTimes.finished.filter((at) => at > since && at <= now).length,
      games_started_by_day: byDay,
    };
  }
  // a START record or relay log the lobby has just stored (the store's own time is the same clock); after a restore the times are read again
  noteGameRecord(kind) {
    if (this.gameTimes) this.gameTimes[kind].push(this.env.now());
  }

  // ---- ONLINE-5: held polls ----
  // resolves when game `id` changes (save), goes away, or after `ms`; null when a bound is reached (the caller answers at once, with the back-off).
  // Every hold is released exactly once, whichever comes first: the game's change, the game's removal or the timer
  holdPoll(id, ms, accountId, pid = null, signal = null) {
    const set = this.waiters.get(id) ?? new Set();
    const mine = this.accountHolds.get(accountId) ?? 0;
    const perPid = pid === null ? 0 : (this.inboxPidHolds.get(pid) ?? 0);
    if (signal?.aborted || this.waiterCount >= (this.config.maxHolds ?? MAX_WAITERS) ||
        set.size >= MAX_WAITERS_PER_GAME || mine >= MAX_HOLDS_PER_ACCOUNT || (pid !== null && perPid >= 2)) return null;
    this.waiters.set(id, set);
    this.waiterCount += 1;
    this.accountHolds.set(accountId, mine + 1);
    if (pid !== null) this.inboxPidHolds.set(pid, perPid + 1);
    return new Promise((resolve) => {
      let timer = null;
      const wake = () => {
        if (!set.delete(wake)) return; // woken once
        this.waiterCount -= 1;
        const left = (this.accountHolds.get(accountId) ?? 1) - 1;
        if (left > 0) this.accountHolds.set(accountId, left);
        else this.accountHolds.delete(accountId);
        if (pid !== null) {
          const remaining = (this.inboxPidHolds.get(pid) ?? 1) - 1;
          if (remaining > 0) this.inboxPidHolds.set(pid, remaining);
          else this.inboxPidHolds.delete(pid);
        }
        if (!set.size && this.waiters.get(id) === set) this.waiters.delete(id);
        (this.env.clearTimer ?? clearTimeout)(timer);
        signal?.removeEventListener('abort', wake);
        resolve();
      };
      set.add(wake);
      timer = (this.env.setTimer ?? setTimeout)(wake, ms);
      signal?.addEventListener('abort', wake, { once: true });
      if (signal?.aborted) wake();
    });
  }
  wake(id, { invalidateStats = true } = {}) {
    if (invalidateStats) this.statsCache = null;
    const set = this.waiters.get(id);
    if (set) for (const w of [...set]) w();
  }

  wakeAll() {
    for (const id of [...this.waiters.keys()]) this.wake(id);
  }

  load(games, relayStatus = [], enforcementRecords = [], sightings = []) {
    if (!Array.isArray(games) || !Array.isArray(relayStatus) || !Array.isArray(enforcementRecords) || !Array.isArray(sightings)) {
      throw new Error('lobby: persisted state collections must be arrays');
    }
    this.social.resetCaches();
    this.loadGames(games);
    for (const r of relayStatus) this.relayStatus.set(r.relay, repairStoredText(r));
    if (this.identityIndexed()) return;
    for (const raw of enforcementRecords) {
      const record = validateEnforcementRecord(raw);
      if (this.enforcement.has(record.install)) throw new Error(`lobby: duplicate stored enforcement for ${record.install}`);
      const { install, ...state } = record;
      this.enforcement.set(install, state);
      for (const device of state.devices) {
        const owners = this.enforcementDeviceOwners.get(device) ?? new Set();
        owners.add(install);
        this.enforcementDeviceOwners.set(device, owners);
      }
    }
    for (const raw of sightings) {
      const record = validateSighting(raw);
      if (this.sightings.has(record.install)) throw new Error(`lobby: duplicate stored sighting for ${record.install}`);
      if (this.sightings.size >= MAX_SIGHTINGS) throw new Error(`lobby: stored sightings exceed ${MAX_SIGHTINGS}`);
      this.sightings.set(record.install, record);
      for (const { hash } of record.devices) {
        const installs = this.sightingDeviceOwners.get(hash) ?? new Set();
        installs.add(record.install);
        this.sightingDeviceOwners.set(hash, installs);
      }
    }
  }

  loadGames(games) {
    if (!Array.isArray(games)) throw new Error('lobby: persisted games must be an array');
    for (const raw of games) {
      const g = withoutGameIp(repairStoredText(raw));
      if (!g || typeof g.id !== 'string' || !/^[0-9a-f]{16}$/.test(g.id) || !Array.isArray(g.slots) || g.slots.length !== MAX_SLOTS) {
        throw new Error('lobby: stored game is malformed');
      }
      if (this.games.has(g.id)) throw new Error(`lobby: duplicate stored game ${g.id}`);
      if (g.routing === undefined) g.routing = '';
      if (typeof g.routing !== 'string' || enc.encode(g.routing).length > MAX_ROUTING_BYTES) throw new Error(`lobby: stored game ${g.id} has invalid routing`);
      if (g.reportParticipants === undefined) {
        g.reportParticipants = g.slots.filter((slot) => slot.state === 'player' && typeof slot.account === 'string')
          .map((slot) => ({ account: slot.account, install: slot.install ?? null, nick: slot.name }));
      }
      if (!Array.isArray(g.reportParticipants) || g.reportParticipants.length > 256 ||
          new Set(g.reportParticipants.map((participant) => participant?.account)).size !== g.reportParticipants.length) {
        throw new Error(`lobby: stored game ${g.id} has invalid report participants`);
      }
      for (const slot of g.slots) {
        if (slot.install === undefined) slot.install = null;
        if (slot.install !== null && !validInstall(slot.install)) throw new Error(`lobby: stored game ${g.id} has an invalid slot install`);
      }
      if (g.state === 'closed') continue; // SOCIAL-1: a host's closed room is saved once before it is deleted
      this.games.set(g.id, g);
    }
  }

  // ---- rate limits (token buckets: per account and per address) ----
  limit(key, perMinute, burst) {
    const now = this.env.now();
    const b = this.buckets.get(key) ?? { tokens: burst, at: now };
    b.tokens = Math.min(burst, b.tokens + ((now - b.at) * perMinute) / 60000);
    b.at = now;
    if (b.tokens < 1) {
      this.buckets.set(key, b);
      throw new LobbyError(429, 'rate_limited', 'too many requests: wait a moment');
    }
    b.tokens -= 1;
    this.buckets.set(key, b);
    if (this.buckets.size > 50000) {
      for (const [k, v] of this.buckets) if (now - v.at > 600000) this.buckets.delete(k);
    }
  }

  // ---- sessions: base64url(JSON).base64url(HMAC) ----
  // ONLINEMENU-1 (PR #8 review, finding 2: a stolen session token was a permanent takeover). The install id is the credential that signs in as a
  // player, and the token showed it: anyone holding a token could sign in again forever, link their own Discord / Google account or make a recovery
  // code. The token now carries it sealed (`ix`: base64url of a 12-byte nonce and the install's 16 bytes XORed with HMAC-SHA256(session key,
  // TOKEN_INSTALL_LABEL | nonce); the token's MAC covers it) and a public tag of it (`ih`: installTag, the game's check that a kept session is its
  // own install's). A token is usable until it expires and can be renewed only with the install itself; identity changes (linking, unlinking,
  // a recovery code) need the install in the request (Lobby.proveInstall). `iat` is the issue time
  async makeToken(account) {
    const { install, ix: _ix, ih: _ih, iat: _iat, exp: _exp, ...rest } = account;
    const now = Math.floor(this.env.now() / 1000);
    const sealed = install === undefined ? {} : { ix: await this.sealInstall(install), ih: await installTag(this.env, install) };
    const body = enc.encode(JSON.stringify({ ...rest, ...sealed, iat: now, exp: now + this.config.sessionSeconds }));
    const mac = await this.env.hmac(this.config.sessionKey, body);
    return `${base64(body, true)}.${base64(mac, true)}`;
  }

  async installMask(nonce) {
    const label = enc.encode(TOKEN_INSTALL_LABEL);
    const input = new Uint8Array(label.length + nonce.length);
    input.set(label);
    input.set(nonce, label.length);
    return this.env.hmac(this.config.sessionKey, input);
  }

  async sealInstall(install) {
    if (!validInstall(install)) throw new Error('a session install must be 32 lowercase hex digits');
    const nonce = this.env.randomBytes(12);
    const mask = await this.installMask(nonce);
    const out = new Uint8Array(28);
    out.set(nonce);
    for (let i = 0; i < 16; i++) out[12 + i] = parseInt(install.slice(i * 2, i * 2 + 2), 16) ^ mask[i];
    return base64(out, true);
  }

  async openInstall(ix) {
    let bytes;
    try {
      bytes = typeof ix === 'string' && /^[A-Za-z0-9_-]{38}$/.test(ix) ? base64UrlDecode(ix) : null;
    } catch {
      bytes = null;
    }
    if (!bytes || bytes.length !== 28) throw new LobbyError(401, 'signed_out', 'the session identity is malformed');
    const mask = await this.installMask(bytes.subarray(0, 12));
    return bytesToHex(bytes.subarray(12).map((byte, i) => byte ^ mask[i]));
  }

  // ONLINEMENU-1 (finding 2): an identity change (linking or unlinking a provider, a new recovery code) must come with the install the session
  // was issued to; a session token alone (which no longer shows it) is not enough
  proveInstall(account, install) {
    if (typeof install !== 'string' || !validInstall(install) || !account.install || install.length !== account.install.length) {
      throw new LobbyError(403, 'install_proof', 'this change must come from the game install that signed in');
    }
    let diff = 0;
    for (let i = 0; i < install.length; i++) diff |= install.charCodeAt(i) ^ account.install.charCodeAt(i);
    if (diff) throw new LobbyError(403, 'install_proof', 'this change must come from the game install that signed in');
  }
  // `leaving` (DEVINPR-1, review of WP-C): a banned or suspended player may still leave the room they are in (POST .../leave), so their slot does not
  // stay taken until the host kicks them; nothing else is allowed and the token itself is checked as always. `activity` (SOCIAL-1): false for a request
  // that must not count as the player being active (presence)
  async account(authorization, { leaving = false, activity = true, pollCache = false } = {}) {
    const m = /^Bearer ([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+)$/.exec(authorization ?? '');
    if (!m) throw new LobbyError(401, 'signed_out', 'sign in first');
    let body;
    let mac;
    try {
      body = base64UrlDecode(m[1]);
      mac = base64UrlDecode(m[2]);
    } catch {
      throw new LobbyError(401, 'signed_out', 'a malformed session token');
    }
    const want = await this.env.hmac(this.config.sessionKey, body);
    let diff = want.length ^ mac.length;
    for (let i = 0; i < Math.min(want.length, mac.length); i++) diff |= want[i] ^ mac[i];
    if (diff) throw new LobbyError(401, 'signed_out', 'the session token is not this lobby\'s');
    let a;
    try {
      a = JSON.parse(dec.decode(body));
    } catch {
      throw new LobbyError(401, 'signed_out', 'a malformed session token');
    }
    if (typeof a !== 'object' || a === null || Array.isArray(a) || typeof a.id !== 'string' || typeof a.name !== 'string') {
      throw new LobbyError(401, 'signed_out', 'a malformed session token');
    }
    // LOBBYSEC-1: a token signed before intake refused unpaired surrogates may carry one in its name, and the name is echoed in every room it
    // joins: sign in again (the new sign-in checks the name)
    if (!a.name.isWellFormed()) throw new LobbyError(401, 'signed_out', 'sign in again: the session name is not well-formed text');
    if (!(a.exp > this.env.now() / 1000)) throw new LobbyError(401, 'signed_out', 'the session expired: sign in again');
    // ONLINEMENU-1 (finding 2): a token that shows its install is from before the install was sealed: it is refused (the game and the launcher
    // sign in again); a sealed one is opened here, so everything after this sees `install` as before
    if (Object.hasOwn(a, 'install')) throw new LobbyError(401, 'signed_out', 'the session is from an older lobby: sign in again');
    if (Object.hasOwn(a, 'ix')) {
      a.install = await this.openInstall(a.ix);
      delete a.ix;
      delete a.ih;
    } else if (Object.hasOwn(a, 'ih')) throw new LobbyError(401, 'signed_out', 'the session identity is malformed');
    if ((a.install !== undefined && !validInstall(a.install)) || (a.device !== undefined && !validDevice(a.device))) {
      throw new LobbyError(401, 'signed_out', 'the session identity is malformed');
    }
    if (this.config.requireInstall && !a.install) throw new LobbyError(401, 'signed_out', 'sign in again (an OpenBFME version older than this lobby: update OpenBFME if this repeats)');
    let current;
    if (pollCache && !leaving) current = await this.social.authorizePoll(a, m[1]);
    else {
      if (a.pid !== undefined) await this.social.assertSession(a);
      current = leaving ? { ...a, routing: (a.install ? this.getEnforcement(a.install)?.routing : null) ?? '' } : this.checkAccount(a);
    }
    this.seen(a.id); // ONLINECOUNT-1
    this.statsCache = null;
    if (current.pid !== undefined) this.social.activity(current, { idle: !activity });
    return current;
  }

  async guest(body, ip) {
    this.limit(`signin:${ip}`, 10, 20);
    allowFields(body, ['v', 'name', 'install', 'device', 'client', 'recover'], 'guest');
    if (body.v !== undefined && body.v !== VERSION) throw bad('bad_version', `v must be ${VERSION}`);
    const name = body?.name;
    if (!validPlayerName(name)) {
      throw bad('bad_field', 'name must be 1 to 24 Unicode characters without controls or surrounding whitespace');
    }
    if (body.install !== undefined && !validInstall(body.install)) throw bad('bad_field', 'install must be 32 lowercase hex digits');
    if (body.device !== undefined && !validDevice(body.device)) throw bad('bad_field', 'device must be 64 lowercase hex digits');
    if (body.recover !== undefined && (typeof body.recover !== 'string' || !/^[0-9A-HJKMNP-TV-Z]{16}$/.test(body.recover))) {
      if (body.install) await this.social.chargeMalformedRecovery(ip, body.install);
      throw bad('bad_field', 'recover must be 16 uppercase Crockford characters');
    }
    if (this.config.requireInstall && !body.install) throw new LobbyError(400, 'install_required', 'this OpenBFME version is too old for online play: update OpenBFME (the launcher updates it) and sign in again');
    let client;
    if (body.client !== undefined) {
      exactFields(body.client, ['platform', 'version'], 'client');
      const platform = boundedText(body.client.platform, 32, 'client.platform');
      if (knownPlatform(platform) === null) throw bad('bad_field', 'client.platform must be windows, linux or macos');
      client = { platform, version: boundedText(body.client.version, 64, 'client.version') };
    }
    const id = `guest:${bytesToHex(this.env.randomBytes(8))}`;
    const account = {
      id,
      name,
      kind: 'guest',
      ...(body.install !== undefined ? { install: body.install } : {}),
      ...(body.device !== undefined ? { device: body.device } : {}),
      ...(client ? { client } : {}),
    };
    const current = this.checkAccount(account);
    const identity = await this.social.signIn(account, { recover: body.recover ?? null, ip });
    if (identity) Object.assign(account, identity);
    if (body.install) await this.recordSighting(body.install, name, body.device ?? null, client ?? null);
    this.seen(id); // ONLINECOUNT-1
    const response = { v: VERSION, token: await this.makeToken(account), account: { id, name, kind: 'guest', ...(identity ? { pid: identity.pid } : {}) } };
    const notice = body.install ? this.getEnforcement(body.install)?.notice : null;
    // Sol r2: a moderation notice and a "moved" notice together are shown as one (the moved one is used up by this sign-in)
    const moved = identity?.moved ? movedNotice(identity.moved) : null;
    // Sol r3, r4: the joined notice keeps the clients' limits: id 128 bytes (the game), text 2,000 characters (the launcher) and 4,000 bytes (the game)
    if (notice && moved) {
      const id = fitText(`${notice.id}+${moved.id}`, 128, 128, `+${moved.id}`);
      const text = fitText(`${moved.text}\n\n${notice.text}`, 2000, 4000, '…');
      response.notice = { ...notice, id, text };
    }
    else if (notice) response.notice = { ...notice };
    else if (moved) response.notice = moved;
    return response;
  }

  deviceBlock(device) {
    if (!device) return null;
    const now = Math.floor(this.env.now() / 1000);
    if (this.identityIndexed()) return this.identityStore().deviceBlock(device, now);
    let selected = null;
    let owner = null;
    for (const install of [...(this.enforcementDeviceOwners.get(device) ?? [])].sort(compareText)) {
      const candidate = isActiveBlock(this.enforcement.get(install)?.block, now);
      const order = compareBlocks(candidate, selected);
      if (candidate && (order > 0 || (order === 0 && (owner === null || install < owner)))) {
        selected = candidate;
        owner = install;
      }
    }
    return selected;
  }

  accountSecurity(account) {
    return {
      effective: account.install ? this.getEnforcement(account.install) : null,
      deviceBlock: this.deviceBlock(account.device),
    };
  }
  checkAccount(account, { chat = false, security = null } = {}) {
    if (this.config.requireInstall && !account.install) throw new LobbyError(401, 'signed_out', 'sign in again (an OpenBFME version older than this lobby: update OpenBFME if this repeats)');
    const now = Math.floor(this.env.now() / 1000);
    const state = security ?? this.accountSecurity(account);
    const effective = state.effective;
    const directBlock = isActiveBlock(effective?.block, now);
    const deviceBlock = isActiveBlock(state.deviceBlock, now);
    const block = compareBlocks(deviceBlock, directBlock) > 0 ? deviceBlock : directBlock;
    if (block) {
      const until = block.until === null ? '' : ` until ${new Date(block.until * 1000).toISOString().slice(0, 16).replace('T', ' ')} UTC`; // DEVINPR-1
      const message = `${block.kind === 'ban' ? 'Access is unavailable' : 'Access is temporarily unavailable'}${until}: ${block.reason}`;
      if (enc.encode(message).length > MAX_MOD_MESSAGE_BYTES) throw new Error('lobby: stored access message exceeds its limit');
      throw new LobbyError(403, block.kind === 'ban' ? 'banned' : 'suspended', message, {
        until: block.until,
        reason: block.reason,
      });
    }
    if (chat && effective?.mute_until !== null && effective?.mute_until > now) {
      // DEVINPR-1 (review of WP-C): a time the player can read (UTC), not a Unix time; `until` keeps the number for clients
      const message = `Chat is unavailable until ${new Date(effective.mute_until * 1000).toISOString().slice(0, 16).replace('T', ' ')} UTC.`;
      if (enc.encode(message).length > MAX_MOD_MESSAGE_BYTES) throw new Error('lobby: stored mute message exceeds its limit');
      throw new LobbyError(403, 'muted', message, { until: effective.mute_until });
    }
    return { ...account, routing: effective?.routing ?? '' };
  }

  async recordSighting(install, nick, device, client) {
    const now = Math.floor(this.env.now() / 1000);
    await this.cleanupSightingsIfDue(now);
    const previous = this.getSighting(install);
    if (!this.identityIndexed() && !previous && this.sightings.size >= MAX_SIGHTINGS) {
      // DEVINPR-1 (review of WP-C): a full table forgets the install seen longest ago (a moderation signal only: bans and suspensions are the
      // enforcement records, never evicted) instead of refusing every new player's sign-in until a year of sightings expires
      let oldest = null;
      for (const [key, value] of this.sightings) {
        if (oldest === null || value.last_seen < oldest[1].last_seen || (value.last_seen === oldest[1].last_seen && key < oldest[0])) oldest = [key, value];
      }
      await this.forgetSighting(oldest[0], oldest[1]);
    }
    const record = previous
      ? {
        ...previous,
        nicks: previous.nicks.map((entry) => ({ ...entry })),
        devices: previous.devices.map((entry) => ({ ...entry })),
        last_seen: now,
        signins: previous.signins,
        platform: client?.platform ?? previous.platform,
        version: client?.version ?? previous.version,
      }
      : {
        install,
        nicks: [],
        first_seen: now,
        last_seen: now,
        signins: 0,
        platform: client?.platform ?? null,
        version: client?.version ?? null,
        devices: [],
      };
    record.signins += 1;
    const nickIndex = record.nicks.findIndex((entry) => entry.nick === nick);
    if (nickIndex >= 0) {
      const [nickRecord] = record.nicks.splice(nickIndex, 1);
      nickRecord.last_seen = now;
      record.nicks.unshift(nickRecord);
    } else {
      record.nicks.unshift({ nick, first_seen: now, last_seen: now });
    }
    if (record.nicks.length > 10) record.nicks.length = 10;
    if (device) {
      const deviceIndex = record.devices.findIndex((entry) => entry.hash === device);
      if (deviceIndex >= 0) {
        const [deviceRecord] = record.devices.splice(deviceIndex, 1);
        deviceRecord.last_seen = now;
        deviceRecord.count += 1;
        record.devices.unshift(deviceRecord);
      } else {
        record.devices.unshift({ hash: device, count: 1, first_seen: now, last_seen: now });
      }
      if (record.devices.length > 5) record.devices.length = 5;
    }
    if (typeof this.env.store.saveSighting !== 'function') throw new LobbyError(501, 'no_sighting_store', 'sighting storage is not configured');
    await this.env.store.saveSighting(record);
    if (previous && !this.identityIndexed()) {
      for (const { hash } of previous.devices) {
        const installs = this.sightingDeviceOwners.get(hash);
        installs?.delete(install);
        if (installs && !installs.size) this.sightingDeviceOwners.delete(hash);
      }
    }
    if (!this.identityIndexed()) {
      this.sightings.set(install, record);
      for (const { hash } of record.devices) {
        const installs = this.sightingDeviceOwners.get(hash) ?? new Set();
        installs.add(install);
        this.sightingDeviceOwners.set(hash, installs);
      }
    }
  }

  async forgetSighting(install, record) {
    if (typeof this.env.store.deleteSighting !== 'function') throw new LobbyError(501, 'no_sighting_store', 'sighting storage is not configured');
    await this.env.store.deleteSighting(install);
    this.sightings.delete(install);
    for (const { hash } of record.devices) {
      const installs = this.sightingDeviceOwners.get(hash);
      installs?.delete(install);
      if (installs && !installs.size) this.sightingDeviceOwners.delete(hash);
    }
  }

  async cleanupSightingsIfDue(now = Math.floor(this.env.now() / 1000)) {
    if (this.lastSightingCleanupAt !== null && now - this.lastSightingCleanupAt < SIGHTING_CLEANUP_INTERVAL_S) return;
    if (this.identityIndexed()) {
      this.identityStore().cleanupSightings(now);
      this.lastSightingCleanupAt = now;
      return;
    }
    if (!this.sightings.size) return;
    if (typeof this.env.store.deleteSighting !== 'function') throw new LobbyError(501, 'no_sighting_store', 'sighting storage is not configured');
    const keys = [...this.sightings.keys()].sort();
    let start = this.sightingCleanupCursor === null ? 0 : keys.findIndex((key) => key > this.sightingCleanupCursor);
    if (start < 0) start = 0;
    const visits = Math.min(MAX_SIGHTING_CLEANUP, keys.length);
    for (let i = 0; i < visits; i++) {
      const install = keys[(start + i) % keys.length];
      const record = this.sightings.get(install);
      if (record && sightingExpired(record, now)) await this.forgetSighting(install, record);
    }
    this.sightingCleanupCursor = keys[(start + visits - 1) % keys.length] ?? null;
    this.lastSightingCleanupAt = now;
  }

  // ---- games ----
  // lane JOINVER-1: a join refused for a different build, said in plain words (an older client shows this message as it is); both builds and profile
  // ids travel as fields for the client's own text and its DETAILS. The join and an invite's accept (social.mjs) refuse with it
  profileRefusal(g, account, body) {
    const yours = checkBuild(body?.build) ?? accountBuild(account);
    const host = g.hostBuild ?? null;
    return new LobbyError(409, 'profile', `You can't join this game: it runs a different OpenBFME version (host: ${buildName(host)}; you: ${buildName(yours)}). Both players need the same version: update through the launcher.`, {
      host_build: host, your_build: yours, host_profile: g.profile, your_profile: typeof body?.profile === 'string' ? body.profile.slice(0, 32) : null,
    });
  }
  publicGame(g) {
    return {
      id: g.id,
      name: g.name,
      host: g.slots[g.hostSlot].name,
      hostPlatform: g.slots[g.hostSlot].platform ?? null, // PLATFORMICON-1: the host's system, for the icon in the server browser
      map: g.setup.map,
      players: g.slots.filter((s) => s.state === 'player').length,
      openSlots: g.slots.filter((s) => s.state === 'open').length,
      state: g.state,
      password: !!g.password,
      mode: g.mode ?? 'normal',
      cheatMode: !!g.setup.cheatMode, // CHEAT-1: tagged in the server browser before anyone joins
      // lane JOINVER-1: the host's build and the game's profile id, so a client greys out a game it cannot join before the player clicks it
      profile: g.profile,
      build: g.hostBuild ?? null,
      ...(capacityOf(g) > MAX_SLOTS ? { mapStartCount: capacityOf(g) } : {}), // ONLINE16-FIX: absent on a legacy room (its old shape)
    };
  }
  gameView(g, account) {
    return {
      id: g.id,
      name: g.name,
      state: g.state,
      hostSlot: g.hostSlot,
      profile: g.profile,
      mode: g.mode ?? 'normal',
      setup: { ...g.setup, slots: undefined, mapStartCount: capacityOf(g) > MAX_SLOTS ? capacityOf(g) : undefined, requireAuth: g.setup.requireAuth || undefined },
      slots: g.slots.map((s, i) => ({
        slot: i,
        state: s.state,
        name: s.name ?? null,
        player_id: s.state === 'player' ? (s.account ?? null) : null,
        pid: s.state === 'player' ? (s.pid ?? null) : null,
        blocked: !!account?.pid && !!s.pid && this.social.blockedInView(account.pid, s.pid),
        faction: s.faction,
        color: s.color,
        startPos: s.startPos,
        team: s.team,
        handicap: s.handicap ?? 0,
        cah: s.state === 'player' ? s.cah ?? null : null,
        ready: !!s.ready,
        platform: s.state === 'player' ? s.platform ?? null : null,
        link: s.state === 'player' ? s.link ?? null : null,
        you: !!account && s.account === account.id,
      })),
      seq: g.seq,
      private: !!g.roomCode,
      // ONLINEMENU-1: the relay this room's START will name (the first of RELAYS; /v1/health lists them for anyone): the players probe it for the
      // Connections screen before the game starts. null: no relay, the room cannot start (no_relay)
      relay: this.config.relays[0]?.address ?? null,
    };
  }
  sweep() {
    const now = this.env.now();
    for (const [id, g] of this.games) {
      const keep = g.state === 'started' ? this.config.startedGameKeepMs : this.config.openGameIdleMs;
      if (now - g.touched > keep) {
        if (typeof this.env.store.reportTransaction === 'function' && g.reportEndedAt == null) continue;
        this.games.delete(id);
        this.env.store.deleteGame?.(id);
        this.wake(id);
      }
    }
  }
  async prepareReportExpiries({ cleanup = true } = {}) {
    if (typeof this.env.store.reportTransaction !== 'function') return;
    const now = this.env.now();
    for (const g of this.games.values()) {
      const keep = g.state === 'started' ? this.config.startedGameKeepMs : this.config.openGameIdleMs;
      if (now - g.touched <= keep || g.reportEndedAt != null) continue;
      const endedAt = Math.floor((g.touched + keep) / 1000);
      try {
        await endReportRoster(this.env.store, g.id, endedAt, g);
        g.reportEndedAt = endedAt;
      } catch (error) {
        this.reportLifecycleError = error;
        throw new LobbyError(503, 'report_store', 'the game end could not be recorded');
      }
    }
    if (!cleanup) return;
    const nowSeconds = Math.floor(now / 1000);
    if (this.reportCleanupAt === null || nowSeconds - this.reportCleanupAt >= 60) {
      try {
        this.reportCleanupCursor = await cleanupReportData(this.env.store, nowSeconds, this.reportCleanupCursor);
        this.reportCleanupAt = nowSeconds;
      } catch (error) {
        this.reportLifecycleError = error;
        throw new LobbyError(503, 'report_store', 'report retention cleanup could not be completed');
      }
    }
  }
  addReportParticipant(g, account) {
    g.reportParticipants ??= [];
    if (g.reportParticipants.some((participant) => participant.account === account.id)) return false;
    if (g.reportParticipants.length >= 256) throw new LobbyError(409, 'roster_full', 'this game has reached its participant history limit');
    g.reportParticipants.push({ account: account.id, install: account.install ?? null, nick: account.name });
    return true;
  }
  async syncReportRoster(g) {
    if (typeof this.env.store.reportTransaction !== 'function') return;
    try {
      await persistReportRoster(this.env.store, g, g.reportEndedAt ?? null);
    } catch (error) {
      this.reportLifecycleError = error;
      throw new LobbyError(503, 'report_store', 'the game roster could not be saved');
    }
  }
  async finishReportRoster(id, g = null) {
    if (typeof this.env.store.reportTransaction !== 'function') return;
    try {
      const result = await endReportRoster(this.env.store, id, Math.floor(this.env.now() / 1000), g);
      if (g && result) g.reportEndedAt = result.ended_at;
    } catch (error) {
      this.reportLifecycleError = error;
      throw new LobbyError(503, 'report_store', 'the game end could not be recorded');
    }
  }
  game(id) {
    const g = this.games.get(id);
    if (!g) throw new LobbyError(404, 'no_game', `no game ${id}`);
    return g;
  }
  deniedFromGame(g, account) {
    return (g.denied ?? []).some((entry) =>
      (entry.account && entry.account === account.id) || (entry.install && account.install && entry.install === account.install));
  }
  gameFor(account, id) {
    const g = this.game(id);
    if ((g.routing ?? '') !== (account.routing ?? '')) throw new LobbyError(404, 'no_game', `no game ${id}`);
    if (this.deniedFromGame(g, account)) throw new LobbyError(403, 'not_in_game', 'you are not in this game (kicked or left)');
    return g;
  }
  gameForInstall(install) {
    for (const g of this.games.values()) {
      if (g.slots.some((slot) => slot.state === 'player' && slot.install === install)) return g.id;
    }
    return null;
  }
  slotOf(g, account) {
    return g.slots.findIndex((s) => s.account === account.id);
  }
  async save(g, { wake = true } = {}) {
    g.seq += 1;
    g.touched = this.env.now();
    await this.env.store.saveGame?.(g);
    if (wake) this.wake(g.id); // ONLINE-5: the held polls of the game answer now
  }
  // `account`: the id of the player who wrote the line (ONLINE-5: a poll marks the reader's own lines)
  event(g, text, from = null, account = undefined) {
    g.chat.push({ seq: g.seq + 1, from, text, at: this.env.now(), ...(account ? { account } : {}) });
    if (g.chat.length > 200) g.chat.splice(0, g.chat.length - 200);
  }

  // ONLINE-5: the choices of a slot request, checked whole before anything is changed (a refused request leaves the slot as it was: the old code set
  // the fields one by one, so a good faction before a bad colour stayed). `slots` / `index`: the game's slots and the slot the choices are for; a
  // start position or a colour another occupied slot holds is refused by name (random, -1, is never taken)
  // ONLINE16-FIX: `capacity`: the map's starts (start positions 0 .. capacity-1; a sixteen-place room has teams 0 .. 15 as OnlineStart reads them)
  checkSlotChoice(s, body, slots = null, index = -1, capacity = MAX_SLOTS) {
    const out = {};
    if (body.faction !== undefined) out.faction = intIn(body.faction, -1, 63, 'faction');
    if (body.color !== undefined) out.color = intIn(body.color, -1, 63, 'color');
    if (body.startPos !== undefined) out.startPos = intIn(body.startPos, -1, capacity - 1, 'startPos');
    if (body.team !== undefined) out.team = intIn(body.team, -1, placesOf(capacity) - 1, 'team');
    // ONLINEMENU-1: the slot's handicap (RotWK GameSlot + 0x20: 0 or -5 .. -95 in steps of 5, the Handicap combo's item data, RW 0x840B9D)
    if (body.handicap !== undefined) {
      out.handicap = intIn(body.handicap, -95, 0, 'handicap');
      if (out.handicap % 5 !== 0) throw bad('bad_field', 'handicap must be 0 or -5 .. -95 in steps of 5');
    }
    // ONLINEMENU-1: a human player's own Create-a-Hero (null: none); an AI's or an empty slot's is refused (RotWK's lobby hands a hero to a player's slot:
    // the owner's applySlotHero, BFME2 MpGameSetup owner vslot 6)
    if (body.cah !== undefined) {
      if (s.state !== 'player') throw bad('bad_field', 'only a player\'s own slot takes a Create-a-Hero (cah)');
      out.cah = checkCah(body.cah);
    }
    if (slots) {
      const occupied = (o) => o.state === 'player' || AI_STATES.has(o.state);
      const holder = (field) => slots.findIndex((o, i) => i !== index && occupied(o) && out[field] !== undefined && out[field] >= 0 && o[field] === out[field]);
      const spot = holder('startPos');
      if (spot >= 0) throw new LobbyError(409, 'spot_taken', `start position ${out.startPos + 1} is ${slots[spot].name ?? `slot ${spot + 1}`}'s`);
      const color = holder('color');
      if (color >= 0) throw new LobbyError(409, 'color_taken', `that colour is ${slots[color].name ?? `slot ${color + 1}`}'s`);
    }
    return out;
  }
  checkSetup(setup, slotsRequired) {
    if (typeof setup !== 'object' || setup === null) throw bad('bad_field', 'setup must be an object');
    const out = {
      map: str(setup.map, 200, 'setup.map'),
      // ONLINE-4: -1 is RotWK's unset money (GameInfo::reset, RW 0x801AAF): the game's InitialResources rule gives it (what the online screens send)
      startingCash: intIn(setup.startingCash ?? 1500, -1, 1000000000, 'setup.startingCash'),
      superweaponRestriction: intIn(setup.superweaponRestriction ?? 0, 0, 1, 'setup.superweaponRestriction'),
      legendaryHeroes: intIn(setup.legendaryHeroes ?? 1, 1, 4, 'setup.legendaryHeroes'),
      // CHEAT-1: the host's open Cheat Mode (every player sees it in the list before joining; the START names the host's slot as the cheat seat)
      cheatMode: setup.cheatMode ?? false,
      // lane ONLINE-3: how far behind the players the spectators watch (seconds of game time; the relay holds the log back that far)
      spectatorDelay: intIn(setup.spectatorDelay ?? 120, 0, 600, 'setup.spectatorDelay'),
      rules: setup.rules ?? null,
      // lane NET-SYNC: the game's network mode; absent is 'classic' (a version-1 ticket, today's path)
      netMode: netModeOf(setup.netMode, 'setup.netMode'),
      // ROUTER-1: the game's frame period in ms (200 today; the parallel lane sets 5/10/15/20 Hz -> 200/100/67/50)
      frameMs: frameMsOf(setup.frameMs, 'setup.frameMs'),
      // ONLINE16-FIX: the map's starts; absent or 8 is the legacy room (stored absent: its old shape everywhere)
      mapStartCount: setup.mapStartCount === undefined || setup.mapStartCount === MAX_SLOTS ? undefined
        : intIn(setup.mapStartCount, MAX_SLOTS + 1, WIDE_SLOTS, 'setup.mapStartCount (8, or 9 .. 16 for an extended map)'),
      // ONLINE16-FIX (the AUTH-1 cut-over): the game's tickets carry TICKET_MODE_AUTH, so the relay refuses unauthenticated datagrams; off unless asked
      requireAuth: setup.requireAuth === undefined || setup.requireAuth === null ? undefined : boolOf(setup.requireAuth, 'setup.requireAuth') || undefined,
    };
    if (typeof out.cheatMode !== 'boolean') throw bad('bad_field', 'setup.cheatMode must be true or false');
    if (out.rules !== null) {
      if (!Array.isArray(out.rules) || out.rules.length !== 10) throw bad('bad_field', 'setup.rules must be null or 10 integers');
      out.rules.forEach((r, i) => intIn(r, -2147483648, 2147483647, `setup.rules[${i}]`));
    }
    if (slotsRequired) {
      const places = placesOf(out.mapStartCount ?? MAX_SLOTS);
      if (!Array.isArray(setup.slots) || setup.slots.length < 2 || setup.slots.length > places) throw bad('bad_field', `setup.slots must be 2 .. ${places} slots`);
    }
    return out;
  }

  // lane PROFILE-2: stores a client's archive list under its SHA-256 (content-addressed: the same install uploads the same blob; never overwritten)
  async putArchives(account, body, ip) {
    this.limit(`archives:${account.id}`, 10, 10);
    this.limit(`archives-ip:${ip}`, 6, 12); // Sol review P2: guest accounts are free; the address is not
    if (!this.env.store.putBlob || typeof this.env.sha256 !== 'function') throw new LobbyError(501, 'no_log_store', 'no blob store is configured (LOG_STORE)');
    allowFields(body, ['id', 'lines'], 'body');
    const id = body?.id;
    if (typeof id !== 'string' || !ARCHIVES_ID.test(id)) throw bad('bad_field', 'id must be the SHA-256 of the lines (64 hex digits)');
    const lines = body?.lines;
    if (!Array.isArray(lines) || lines.length < 1 || lines.length > MAX_ARCHIVE_LINES) throw bad('bad_field', `lines must hold 1 .. ${MAX_ARCHIVE_LINES} archive lines`);
    for (const [i, l] of lines.entries()) {
      if (typeof l !== 'string' || !ARCHIVE_LINE.test(l)) throw bad('bad_field', `lines[${i}] must be "<install>:<path> <md5> <size>"`);
    }
    const bytes = enc.encode(lines.map((l) => `${l}\n`).join(''));
    if (bytesToHex(await this.env.sha256(bytes)) !== id) throw bad('bad_field', 'id is not the SHA-256 of the lines (each followed by a newline)');
    const pending = (this.pendingArchives ??= new Map());
    const blob = enc.encode(JSON.stringify({ v: VERSION, lines }));
    if (pending.has(id)) {
      this.pendingArchiveBytes -= pending.get(id).length;
      pending.delete(id); // re-added below: the newest
    }
    if (!(await this.env.store.getBlob?.(`archives/${id}.json`))) {
      pending.set(id, blob);
      this.pendingArchiveBytes = (this.pendingArchiveBytes ?? 0) + blob.length;
      for (const [old, b] of pending) {
        if (this.pendingArchiveBytes <= MAX_PENDING_ARCHIVE_BYTES) break;
        pending.delete(old);
        this.pendingArchiveBytes -= b.length;
      }
    }
    return { v: VERSION, id };
  }

  // lane PROFILE-2: stores the lists a starting game names (each once: content-addressed); a list no longer held (dropped, or the lobby restarted
  // since the upload) stays a name the checker reports as missing
  async storeGameArchives(ids) {
    for (const id of new Set(ids.filter(Boolean))) {
      const blob = this.pendingArchives?.get(id);
      if (blob && !(await this.env.store.getBlob?.(`archives/${id}.json`))) await this.env.store.putBlob(`archives/${id}.json`, blob);
    }
  }

  async create(account, body) {
    this.limit(`create:${account.id}`, 6, 6);
    this.sweep();
    if (this.games.size >= this.config.maxGames) throw new LobbyError(503, 'full', 'the lobby holds its maximum number of games');
    for (const g of this.games.values()) {
      if (g.state === 'open' && this.slotOf(g, account) >= 0) throw bad('in_game', `you are in game ${g.id} already: leave it first`);
    }
    const name = str(body?.name, 48, 'name');
    const profile = str(body?.profile, 32, 'profile');
    if (!/^[0-9a-f]{32}$/.test(profile)) throw bad('bad_field', 'profile must be the profile identity\'s MD5 (32 hex digits)');
    const mode = modeOf(body.mode, 'mode');
    const password = body.password === undefined || body.password === '' ? null : str(body.password, 32, 'password');
    // ONLINEHUB-1: a private room named by a code (a Quick Match or a tournament match: the platform hands every player the same code); not listed,
    // joined with POST /v1/rooms/<code>/join; no password beside it
    const roomCode = body.room_code === undefined || body.room_code === null ? null : str(body.room_code, 32, 'room_code');
    if (roomCode !== null) {
      if (!ROOM_CODE.test(roomCode)) throw bad('bad_field', 'room_code must be 6 .. 32 characters of A-Z a-z 0-9 _ -');
      if (password !== null) throw bad('bad_field', 'a room with a room_code has no password');
      for (const g of this.games.values()) {
        if (g.state === 'open' && g.roomCode === roomCode) throw new LobbyError(409, 'code_taken', 'a room with this code is open already');
      }
    }
    const platform = checkPlatform(body.platform);
    const setup = this.checkSetup(body.setup, true);
    // CHEAT-1: a Quick Match / tournament room is never a Cheat Mode game
    if (roomCode !== null && setup.cheatMode) throw bad('bad_field', 'a room with a room_code has no Cheat Mode');
    // ONLINE16-FIX: a sixteen-place room needs a client that reads one; so does a game that requires tags (the AUTH-1 release line)
    const capacity = setup.mapStartCount ?? MAX_SLOTS;
    if (capacity > MAX_SLOTS && account.maxSlots !== WIDE_SLOTS) throw bad('bad_field', `${NEEDS_WIDE} (X-OpenBFME-Max-Slots: 16)`);
    if (setup.requireAuth && account.maxSlots === undefined) throw bad('bad_field', NEEDS_AUTH);
    const slots = body.setup.slots.map((s, i) => {
      if (!SLOT_STATES.includes(s?.state)) throw bad('bad_field', `setup.slots[${i}].state must be one of ${SLOT_STATES.join(', ')}`);
      if (i >= capacity && s.state !== 'closed') throw bad('bad_field', `setup.slots[${i}] is beyond the map's ${capacity} start positions: it must be "closed"`);
      const slot = { state: s.state, faction: -1, color: -1, startPos: -1, team: -1, handicap: 0, cah: null, ready: false };
      if (AI_STATES.has(s.state)) slot.name = str(s.name ?? 'Computer', 24, `setup.slots[${i}].name`);
      const choices = this.checkSlotChoice(slot, s, null, -1, capacity);
      if (s.state === 'player' || AI_STATES.has(s.state)) Object.assign(slot, choices); // Sol r1: an open or closed slot keeps none
      return slot;
    });
    // ONLINE-5: two occupied slots of a new game never share a start position or a colour either
    for (const [i, sl] of slots.entries()) this.checkSlotChoice(sl, sl.state === 'player' || AI_STATES.has(sl.state) ? { startPos: sl.startPos, color: sl.color } : {}, slots, i, capacity);
    while (slots.length < placesOf(capacity)) slots.push({ state: 'closed', faction: -1, color: -1, startPos: -1, team: -1, handicap: 0, cah: null, ready: false });
    const hostSlot = intIn(body.hostSlot ?? 0, 0, capacity - 1, 'hostSlot');
    if (slots[hostSlot].state !== 'player') throw bad('bad_field', `the host's slot ${hostSlot} must be "player"`);
    for (const [i, s] of slots.entries()) {
      if (s.state === 'player' && i !== hostSlot) throw bad('bad_field', 'other human slots start "open" (players join them)');
    }
    slots[hostSlot].account = account.id;
    slots[hostSlot].install = account.install ?? null;
    slots[hostSlot].pid = account.pid ?? null;
    slots[hostSlot].name = account.name;
    slots[hostSlot].platform = platform;
    const hostBuild = checkBuild(body.build) ?? accountBuild(account); // lane JOINVER-1
    const hostArchives = archivesOf(body); // lane PROFILE-2
    slots[hostSlot].maxSlots = account.maxSlots; // ONLINE16-FIX: what the member's client reads (never shown in a view)
    const id = bytesToHex(this.env.randomBytes(8));
    const g = {
      id, name, password, profile, hostBuild, mode, setup, slots, hostSlot, state: 'open', routing: account.routing ?? '',
      chat: [], seq: 0, created: this.env.now(), touched: this.env.now(),
      reportParticipants: [{ account: account.id, install: account.install ?? null, nick: account.name }],
    };
    if (roomCode !== null) g.roomCode = roomCode;
    if (hostArchives !== null) g.archives = { [account.id]: hostArchives }; // lane PROFILE-2: per account (a slot can change hands)
    this.event(g, `${account.name} created the game`);
    await this.syncReportRoster(g);
    await this.save(g);
    this.games.set(id, g);
    this.social.bindChallengeRoom(account, id);
    await this.social.roomChanged(g);
    return { v: VERSION, id, game: this.gameView(g, account) };
  }

  // ONLINE-4: `running` also lists the started games (the server browser offers them to watch: POST .../spectate), after the open ones
  // ONLINE16-FIX: `maxSlots`: the asking client's width; an eight-slot client is not shown sixteen-place rooms
  list(running = false, account = null, maxSlots = MAX_SLOTS) {
    this.sweep();
    const routing = account?.routing ?? '';
    const games = [...this.games.values()].filter((g) => (g.routing ?? '') === routing && !this.deniedFromGame(g, account ?? {})
      && !g.roomCode && (g.state === 'open' || (running && g.state === 'started'))
      && (capacityOf(g) <= MAX_SLOTS || maxSlots === WIDE_SLOTS))
      .sort((a, b) => (b.state === 'open') - (a.state === 'open') || b.created - a.created).slice(0, 200).map((g) => this.publicGame(g));
    return { v: VERSION, games };
  }

  // `byCode`: the caller found the room by its code; game-id joins otherwise need the code or an eligible server-only recipient grant
  async join(account, id, body, byCode = false, grant = null) {
    this.limit(`join:${account.id}`, 20, 10);
    const g = this.gameFor(account, id);
    // UBERJOIN-1: the mode is checked first, before the member / started / password branches (an existing member's repeated join too): a client that sends
    // none is "normal"; a malformed one is bad_field, the other mode is the 409 "mode" refusal
    const joinerMode = modeOf(body?.mode, 'mode');
    if (joinerMode !== (g.mode ?? 'normal')) {
      throw new LobbyError(409, 'mode', `this game uses ${g.mode ?? 'normal'} performance mode and you are in ${joinerMode} mode: switch modes to join it`);
    }
    const existing = this.slotOf(g, account);
    if (existing >= 0) return { v: VERSION, slot: existing, game: this.gameView(g, account) };
    await this.social.assertJoinAllowed(account, g);
    const validGrant = this.social.validJoinGrant(grant, account, g);
    if (g.roomCode && !byCode && !sameSecret(body?.room_code, g.roomCode) && !validGrant) {
      throw new LobbyError(403, 'room_code', 'this room is private: join it with its code');
    }
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (g.password && body?.password !== g.password && !validGrant) throw new LobbyError(403, 'password', 'wrong password');
    // ONLINE16-FIX: an authorized joiner whose client cannot read the room is told so (never seated in a room its client refuses)
    if (capacityOf(g) > MAX_SLOTS && account.maxSlots !== WIDE_SLOTS) throw new LobbyError(409, 'capacity', NEEDS_WIDE);
    if (g.setup.requireAuth && account.maxSlots === undefined) throw new LobbyError(409, 'update', NEEDS_AUTH);
    const platform = checkPlatform(body?.platform);
    checkBuild(body?.build); // lane JOINVER-1: a malformed build is refused whatever the profile
    const archives = archivesOf(body); // lane PROFILE-2
    if (body?.profile !== g.profile) throw this.profileRefusal(g, account, body);
    for (const other of this.games.values()) {
      if (other !== g && other.state === 'open' && this.slotOf(other, account) >= 0) throw bad('in_game', `you are in game ${other.id} already: leave it first`);
    }
    const slot = g.slots.findIndex((s) => s.state === 'open');
    if (slot < 0) throw new LobbyError(409, 'full', 'the game is full');
    const candidate = structuredClone(g);
    this.addReportParticipant(candidate, account);
    // Sol r1 (ONLINE-5): a joiner takes the slot with no choices of its own: nothing an earlier AI of the slot held (a start position, a colour others
    // may have claimed since) carries over to the player
    Object.assign(candidate.slots[slot], { state: 'player', account: account.id, install: account.install ?? null, pid: account.pid ?? null, name: account.name, ready: false, platform, maxSlots: account.maxSlots, ...NO_CHOICES });
    if (archives !== null) (candidate.archives ??= {})[account.id] = archives; // lane PROFILE-2
    else if (candidate.archives) delete candidate.archives[account.id];
    this.unreadyAll(candidate);
    await this.syncReportRoster(candidate);
    this.event(candidate, `${account.name} joined`);
    await this.save(candidate, { wake: false });
    restoreGame(g, candidate);
    this.wake(g.id);
    await this.social.roomChanged(g);
    return { v: VERSION, slot, game: this.gameView(g, account) };
  }

  // ONLINEHUB-1: the open room of a code (rate-limited per account before the lookup: codes are not guessed by trying)
  async joinByCode(account, code, body) {
    this.limit(`code:${account.id}`, 20, 10);
    if (!ROOM_CODE.test(code ?? '')) throw bad('bad_field', 'a room code is 6 .. 32 characters of A-Z a-z 0-9 _ -');
    this.sweep();
    for (const g of this.games.values()) {
      if (g.roomCode === code && g.state === 'open') return this.join(account, g.id, { profile: body?.profile, mode: body?.mode, platform: body?.platform, archives: body?.archives }, true);
    }
    throw new LobbyError(404, 'no_room', 'no open room has this code (the host has not made it yet, or it has started)');
  }

  unreadyAll(g) {
    for (const s of g.slots) s.ready = false;
  }

  async leave(account, id) {
    const g = this.gameFor(account, id);
    const slot = this.slotOf(g, account);
    if (slot < 0) return { v: VERSION, left: false };
    const challengeHandoff = this.social.captureChallengePeers(g, account);
    if (g.state === 'open' && slot === g.hostSlot) {
      const candidate = structuredClone(g);
      candidate.state = 'closed';
      await this.finishReportRoster(id, candidate);
      this.event(candidate, 'the host closed the game');
      await this.save(candidate, { wake: false });
      restoreGame(g, candidate);
      this.games.delete(id);
      try {
        await this.env.store.deleteGame?.(id);
      } catch {}
      this.wake(id);
      this.social.rememberChallengeHandoff(account, challengeHandoff);
      this.social.cancelGameInvites(id);
      await this.social.roomChanged(g);
      return { v: VERSION, left: true, closed: true };
    }
    const candidate = structuredClone(g);
    if (g.state === 'open') {
      Object.assign(candidate.slots[slot], { state: 'open', account: undefined, install: null, pid: null, name: undefined, platform: undefined, maxSlots: undefined, ready: false, ...NO_CHOICES });
      this.unreadyAll(candidate);
      await this.syncReportRoster(candidate);
    }
    // ONLINEUX-1: a player who leaves a started game is done with it (the game ended for them, they forfeited or quit): its slot stays theirs (the
    // START, the replay), but it is no longer offered in /v1/mine and a rejoin is refused. Before, a forfeited game was offered for a rejoin at the
    // next sign-in. A crash sends no leave: the game is still offered then
    if (g.state === 'started') candidate.left = { ...(g.left ?? {}), [slot]: true };
    this.event(candidate, `${account.name} left`);
    await this.save(candidate, { wake: false });
    restoreGame(g, candidate);
    this.wake(g.id);
    this.social.leaveStarted(g, account);
    this.social.rememberChallengeHandoff(account, challengeHandoff);
    this.social.cancelSenderInvites(account.pid, id);
    await this.social.roomChanged(g);
    return { v: VERSION, left: true };
  }

  async setSlot(account, id, body) {
    this.limit(`slot:${account.id}`, 120, 20);
    const g = this.gameFor(account, id);
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    const own = this.slotOf(g, account);
    if (own < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    const capacity = capacityOf(g);
    const target = body.slot === undefined ? own : intIn(body.slot, 0, g.slots.length - 1, 'slot');
    // ONLINE16-FIX: a place beyond the map's starts stays closed (the START's reader refuses anything else there)
    if (target >= capacity) throw bad('bad_field', `slot ${target} is beyond the map's ${capacity} start positions: it stays closed`);
    const s = g.slots[target];
    // ONLINE-5: everything is checked before anything changes (a refusal leaves the room as it was)
    const change = {};
    if (target !== own) {
      // the host arranges the slots that are not players: open / closed / an AI and its options
      if (own !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host changes other slots');
      if (s.state === 'player') throw new LobbyError(403, 'player_slot', 'a player chooses their own options');
      if (body.state !== undefined) {
        if (!SLOT_STATES.includes(body.state) || body.state === 'player') throw bad('bad_field', 'state must be open, closed or an AI level');
        change.state = body.state;
        change.name = AI_STATES.has(body.state) ? str(body.name ?? 'Computer', 24, 'name') : undefined;
      }
    } else if (body.state !== undefined || body.name !== undefined) {
      throw bad('bad_field', 'a player\'s own slot takes faction, color, startPos, team, handicap and cah');
    }
    // the choices are checked for the slot as it will be. Sol r1: an open or closed slot keeps no choices at all (a spot or a colour it kept came back
    // with the slot's next AI or player, beside another slot that had claimed it since), and none can be given to it
    const after = { ...s, ...change };
    const willHold = after.state === 'player' || AI_STATES.has(after.state);
    if (!willHold) {
      // (-1, "none", is what a client that sends the whole row says for them)
      if (['faction', 'color', 'startPos', 'team'].some((k) => body[k] !== undefined && body[k] !== -1) || (body.handicap !== undefined && body.handicap !== 0) || (body.cah !== undefined && body.cah !== null)) {
        throw bad('bad_field', 'an open or closed slot has no faction, color, startPos, team or handicap');
      }
      Object.assign(change, NO_CHOICES);
    } else {
      Object.assign(change, this.checkSlotChoice(after, body, g.slots, target, capacity));
      // what the slot keeps (an AI level changed, a choice left as it was) is checked against the others as well
      this.checkSlotChoice(after, { startPos: change.startPos ?? s.startPos, color: change.color ?? s.color }, g.slots, target, capacity);
    }
    Object.assign(s, change);
    this.unreadyAll(g);
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async setSetup(account, id, body) {
    const g = this.gameFor(account, id);
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host changes the game options');
    const setup = this.checkSetup({ ...g.setup, ...body }, false);
    if (g.roomCode && setup.cheatMode) throw bad('bad_field', 'a room with a room_code has no Cheat Mode'); // CHEAT-1
    // ONLINE16-FIX: the capacity follows the map. Growing needs every member to read sixteen places; shrinking needs the places beyond the new
    // map's starts empty (an open one closes; a player or an AI there is moved or removed by the host first). Requiring tags needs every member
    // to be of the AUTH-1 release line. Everything is checked before anything changes
    const capacity = setup.mapStartCount ?? MAX_SLOTS;
    const members = g.slots.filter((s) => s.state === 'player');
    if (capacity > MAX_SLOTS && members.some((s) => s.maxSlots !== WIDE_SLOTS)) {
      throw new LobbyError(409, 'capacity', `${NEEDS_WIDE}: a player in this room lacks it`);
    }
    if (setup.requireAuth && members.some((s) => s.maxSlots === undefined)) throw new LobbyError(409, 'update', `${NEEDS_AUTH} (a player in this room)`);
    const beyond = g.slots.findIndex((s, i) => i >= capacity && (s.state === 'player' || AI_STATES.has(s.state)));
    if (beyond >= 0) throw new LobbyError(409, 'capacity', `slot ${beyond + 1} is beyond the new map's ${capacity} start positions: free it first`);
    for (let i = capacity; i < g.slots.length; i++) Object.assign(g.slots[i], { state: 'closed', ready: false, ...NO_CHOICES });
    g.slots.length = Math.min(g.slots.length, placesOf(capacity));
    while (g.slots.length < placesOf(capacity)) g.slots.push({ state: 'closed', faction: -1, color: -1, startPos: -1, team: -1, ready: false });
    // ONLINE-5: another map has other start positions: every slot is back to a random one (ZH GameInfo::setMap resets the start spots)
    if (setup.map !== g.setup.map) for (const s of g.slots) s.startPos = -1;
    g.setup = setup;
    this.unreadyAll(g);
    this.event(g, 'the host changed the game options');
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async ready(account, id, body) {
    const g = this.gameFor(account, id);
    const slot = this.slotOf(g, account);
    if (slot < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    if (typeof body?.ready !== 'boolean') throw bad('bad_field', 'ready must be true or false');
    // ONLINE-5: a player readies their own slot only (a body naming another slot is refused by name, never applied to the sender's), in an open game
    if (body.slot !== undefined && body.slot !== slot) throw new LobbyError(403, 'not_your_slot', 'a player readies their own slot only');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (g.slots[slot].ready === body.ready) return { v: VERSION, game: this.gameView(g, account) }; // nothing changed: no new seq
    g.slots[slot].ready = body.ready;
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  // ONLINEMENU-1: a player's link to the room's relay, measured by the player's own client (UDP probes) for the staging room's Connections screen
  // (RotWK's Connections.apt grid: every player only reaches the relay, so the screen shows each player's link). { rtt: 1 .. 10000 ms, loss: 0 .. 100 % }
  // or { rtt: null } (the relay did not answer at all). Display data only: it never unreadies anyone and is not part of the START; an unchanged report
  // takes no new seq (nobody's poll is woken for it). Clients report it on joining, on Refresh and when its band changes
  async link(account, id, body) {
    this.limit(`link:${account.id}`, 30, 10);
    const g = this.gameFor(account, id);
    const slot = this.slotOf(g, account);
    if (slot < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    if (body?.slot !== undefined && body.slot !== slot) throw new LobbyError(403, 'not_your_slot', 'a player reports their own link only');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    allowFields(body, ['v', 'slot', 'rtt', 'loss'], 'link');
    let link;
    if (body.rtt === null) {
      if (body.loss !== undefined && body.loss !== 100) throw bad('bad_field', 'a link the relay never answered has loss 100 or none');
      link = { rtt: null, loss: 100 };
    } else {
      link = { rtt: intIn(body.rtt, 1, 10000, 'rtt'), loss: intIn(body.loss, 0, 100, 'loss') };
    }
    const was = g.slots[slot].link ?? null;
    if (was && was.rtt === link.rtt && was.loss === link.loss) return { v: VERSION, game: this.gameView(g, account) };
    g.slots[slot].link = link;
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async chat(account, id, body) {
    this.limit(`chat:${account.id}`, 30, 8);
    const g = this.gameFor(account, id);
    if (this.slotOf(g, account) < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    this.checkAccount(account, { chat: true });
    const text = str(body?.text, 200, 'text');
    this.event(g, text, account.name, account.id);
    await this.save(g);
    return { v: VERSION, seq: g.seq };
  }

  async kick(account, id, body) {
    const g = this.gameFor(account, id);
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host kicks');
    const slot = intIn(body?.slot, 0, g.slots.length - 1, 'slot');
    if (slot === g.hostSlot || g.slots[slot].state !== 'player') throw bad('bad_field', 'kick another player\'s slot');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    const name = g.slots[slot].name;
    const kickedPid = g.slots[slot].pid;
    Object.assign(g.slots[slot], { state: 'open', account: undefined, install: null, pid: null, name: undefined, platform: undefined, maxSlots: undefined, ready: false, ...NO_CHOICES });
    (g.kicked ??= []).push(slot);
    this.unreadyAll(g);
    this.event(g, `${name} was kicked`);
    await this.save(g);
    this.social.cancelSenderInvites(kickedPid, id);
    await this.social.roomChanged(g);
    if (kickedPid) await this.social.cancelPlayerInvites(kickedPid);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  // The START (OnlineStart.h): the service decides the seed, the run-ahead, the CRC interval and the relay, and signs one ticket per human slot
  async start(account, id) {
    const g = this.gameFor(account, id);
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host starts the game');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (!this.config.relays.length) throw new LobbyError(503, 'no_relay', 'no relay configured: online games cannot start yet');
    const humans = g.slots.map((s, i) => [s, i]).filter(([s]) => s.state === 'player');
    // SOLO-1: a custom game may start with a single human (alone or against AI); a room-code room is the platform's
    // (a Quick Match or tournament match, ONLINEHUB-1) and keeps the two-human minimum
    if (humans.length < (g.roomCode ? 2 : 1)) throw new LobbyError(409, 'players', 'an online game needs at least two players');
    const notReady = humans.filter(([s, i]) => i !== g.hostSlot && !s.ready).map(([s]) => s.name);
    if (notReady.length) throw new LobbyError(409, 'not_ready', `not ready: ${notReady.join(', ')}`);
    // ONLINE-2 r2 (Sol: a player joined while the tickets were being signed, and got no START): from here the game takes no join, slot or setup
    // change ('starting' is not 'open'); the slots were read above, before the first await
    g.state = 'starting';
    try {
      return await this.signAndStart(g, account, humans);
    } catch (e) {
      if (g.state === 'starting') g.state = 'open';
      throw e;
    }
  }

  async signAndStart(g, account, humans) {
    const candidate = structuredClone(g);
    const mask = humans.reduce((m, [, i]) => m | (1 << i), 0);
    const seedBytes = this.env.randomBytes(4);
    const seed = new DataView(seedBytes.buffer, seedBytes.byteOffset, 4).getUint32(0, true);
    const relay = this.config.relays[0];
    const keyId = Math.max(...this.config.ticketKeys.keys());
    const key = this.config.ticketKeys.get(keyId);
    const expires = Math.floor(this.env.now() / 1000) + this.config.ticketSeconds;
    let last = candidate.slots.length;
    while (last > 0 && candidate.slots[last - 1].state === 'closed') last -= 1;
    // ONLINE16-FIX: an extended game's START carries the capacity and all sixteen places (OnlineStart.cpp reads exactly that); a legacy one is unchanged
    const capacity = capacityOf(candidate);
    if (capacity > MAX_SLOTS) last = WIDE_SLOTS;
    const setup = Object.freeze({
      map: candidate.setup.map,
      ...(capacity > MAX_SLOTS ? { mapStartCount: capacity } : {}),
      seed,
      startingCash: candidate.setup.startingCash,
      superweaponRestriction: candidate.setup.superweaponRestriction,
      rules: candidate.setup.rules,
      legendaryHeroes: candidate.setup.legendaryHeroes,
      // CHEAT-1: the cheat seat is the host's slot (only when the host turned Cheat Mode on: a game without it has no field, as before CHEAT-1). The START is
      // the lobby's game record (starts/<id>.json): stats and any ranking read the field and leave the game out
      ...(candidate.setup.cheatMode ? { cheatMode: candidate.hostSlot } : {}),
      slots: candidate.slots.slice(0, last).map((s) => (s.state === 'player' || AI_STATES.has(s.state)
        ? { state: s.state, name: s.name, faction: s.faction, color: s.color, startPos: s.startPos, team: s.team, handicap: s.handicap ?? 0, ...(s.state === 'player' && s.cah ? { cah: s.cah } : {}) }
        : { state: s.state })),
    });
    // PLATFORMICON-1: each player's system, by slot (null: not told, an AI, no one): display data for the running game's player list. It rides in each
    // player's own START only, never in `setup` (the replay's record)
    const platforms = candidate.slots.map((s) => (s.state === 'player' ? s.platform ?? null : null));
    // CHAT-1: the players the moderators muted (enforcement mute_until in the future), by slot: every client drops their in-game chat lines and their own
    // client refuses to send (GameNetwork/Network.h setChatLobbyMuted). Like `platforms` it rides in each player's own START only, never in `setup`
    const nowS = Math.floor(this.env.now() / 1000);
    const chatMuted = candidate.slots.map((s) => {
      if (s.state !== 'player' || !s.install) return false;
      const until = this.getEnforcement(s.install)?.mute_until;
      return until !== null && until !== undefined && until > nowS;
    });
    const starts = {};
    for (const [, i] of humans) {
      const ticket = await signTicket(this.env, keyId, key, g.id, i, mask, expires,
        NET_MODE_TICKET[g.setup.netMode] | (g.setup.requireAuth ? TICKET_MODE_AUTH : 0), g.setup.frameMs);
      starts[i] = {
        v: VERSION,
        kind: 'openbfme-start',
        game: candidate.id,
        slot: i,
        relay: relay.address,
        ticket,
        session_key: await sessionKey(this.env, key, ticket), // lane AUTH-1
        netMode: g.setup.netMode, // lane NET-SYNC: display data; the ticket itself carries the binding mode bits
        frameMs: g.setup.frameMs, // ROUTER-1: the game's frame period; also signed into the ticket
        runAhead: this.config.runAhead,
        crcInterval: this.config.crcInterval,
        profile: candidate.profile,
        platforms,
        chatMuted,
        setup,
      };
    }
    candidate.starts = starts;
    candidate.mask = mask; // lane ONLINE-3: a rejoin ticket names the same slots
    candidate.state = 'started';
    candidate.startedAt = this.env.now();
    // Replay-start is a credential-free snapshot; live per-slot tickets never enter replay metadata.
    const replayStart = { v: VERSION, kind: 'openbfme-replay-start', game: g.id, runAhead: this.config.runAhead, crcInterval: this.config.crcInterval, profile: g.profile, setup };
    // lane PROFILE-2: each human slot's archive list id (null: its client told none). An added field, not a new START version: every checker
    // release reads version 1 and ignores fields it does not know (OnlineStart.cpp), so the checkers of older releases keep reading new games
    replayStart.archives = Object.fromEntries(humans.map(([s, i]) => [String(i), candidate.archives?.[s.account] ?? null]));
    this.event(candidate, 'the game started');
    const socialUndo = await this.social.recordGameStart(candidate);
    try {
      await this.syncReportRoster(candidate);
      // (no log store: no replay record either; /v1/replays then answers 'no_log_store')
      if (this.env.store.putBlob) await this.storeGameArchives(Object.values(replayStart.archives)); // lane PROFILE-2
      await this.env.store.putBlob?.(`starts/${g.id}.json`, enc.encode(JSON.stringify(replayStart)));
      // CFDEPLOY-1 r2 (Sol r1: replay downloads were open to anyone): the accounts that played; only they read the game's replay files. docs/ONLINE.md
      // promises no public replays ("view online" is the player's own link), so a public listing would be a new decision
      await this.env.store.putBlob?.(`players/${g.id}.json`, enc.encode(JSON.stringify(humans.map(([s]) => s.account))));
      await this.save(candidate, { wake: false });
    } catch (error) {
      await this.social.rollbackGameStart(socialUndo);
      throw error;
    }
    restoreGame(g, candidate);
    if (this.env.store.putBlob) this.noteGameRecord('started'); // LOBBYSTATS-1 (merged: counted once the start is saved, never for a rolled-back one)
    this.wake(g.id);
    this.social.loadingUntil.set(g.id, Math.floor(g.startedAt / 1000) + 10);
    this.social.freshStartedGames.add(g.id);
    this.social.cancelGameInvites(g.id);
    if (this.social.loadingUntil.size > this.config.maxGames * 2) this.social.loadingUntil.delete(this.social.loadingUntil.keys().next().value);
    if (this.social.freshStartedGames.size > this.config.maxGames * 2) this.social.freshStartedGames.delete(this.social.freshStartedGames.values().next().value);
    await this.social.roomChanged(g);
    return { v: VERSION, start: g.starts[this.slotOf(g, account)] };
  }

  // ONLINE-5: no awaits inside (a held poll is answered in one step when it wakes)
  poll(account, id, since) {
    account = this.checkAccount(account, { security: account.pollSecurity ?? null });
    const g = this.gameFor(account, id);
    // the host closed the game (leave): it is being removed; a poll held until then says what the next one would
    if (g.state === 'closed') throw new LobbyError(404, 'no_game', `no game ${id}`);
    const slot = this.slotOf(g, account);
    if (slot < 0) {
      if (g.kicked?.length) throw new LobbyError(403, 'not_in_game', 'you are not in this game (kicked or left)');
      throw new LobbyError(403, 'not_in_game', 'join the game first');
    }
    g.touched = this.env.now();
    const s = Number.isInteger(since) ? since : 0;
    return {
      v: VERSION,
      game: this.gameView(g, account),
      chat: g.chat.filter((c) => c.seq > s).map(({ seq, from, text, account: by }) => ({ seq, from, text, ...(by && by === account.id ? { you: true } : {}) })),
      start: g.state === 'started' ? g.starts[slot] : null,
      wait: MAX_WAIT_S, // ONLINE-5: this lobby holds polls (and marks the reader's own chat lines)
    };
  }
  // ONLINE-5: should this poll be held? Only an open game the caller sits in, whose seq is the one the caller has, with a whole number of seconds
  // 1 .. MAX_WAIT_S (anything else is answered at once, as before): { held: Promise }; { backoff: s } when it would be held but a bound (the account's,
  // the game's, the lobby's) is reached; null when it is not a poll to hold
  pollHold(account, id, seqText, waitText) {
    if (seqText === null || waitText === null || !/^\d{1,10}$/.test(seqText) || !/^\d{1,3}$/.test(waitText)) return null;
    const wait = Math.min(Number(waitText), MAX_WAIT_S);
    const g = this.gameFor(account, id);
    if (!g || wait < 1 || g.state !== 'open' || g.seq !== Number(seqText) || this.slotOf(g, account) < 0) return null;
    g.touched = this.env.now();
    const held = this.holdPoll(id, wait * 1000, account.id);
    return held ? { held } : { backoff: HOLD_BACKOFF_S };
  }

  // ---- lane ONLINE-3: rejoin ----
  // the signed-in player's running games: a client whose game restarted (a crash, a lost connection) offers "rejoin" for each
  // ONLINEUX-1 r2 (Sol 4): `left` names the started games the account left (newest first, at most MINE_LEFT_MAX): they stay in the running list for
  // spectators, and a client started after the leave knows them (it offers neither a rejoin nor a WATCH the lobby refuses to a player of the game)
  mine(account) {
    account = this.checkAccount(account);
    const started = [...this.games.values()]
      .filter((g) => (g.routing ?? '') === account.routing && !this.deniedFromGame(g, account) && g.state === 'started' && this.slotOf(g, account) >= 0);
    const games = started
      .filter((g) => !g.left?.[this.slotOf(g, account)])
      .map((g) => ({ ...this.publicGame(g), slot: this.slotOf(g, account), rejoin: `/v1/games/${g.id}/rejoin` }));
    const left = started
      .filter((g) => g.left?.[this.slotOf(g, account)])
      .sort((a, b) => (b.startedAt ?? 0) - (a.startedAt ?? 0))
      .slice(0, MINE_LEFT_MAX)
      .map((g) => g.id);
    return { v: VERSION, games, left };
  }
  // a fresh START for the player's slot of a running game: its ticket is a rejoin ticket of the next generation (the relay binds the slot's new
  // incarnation with it and refuses every older ticket of the slot); "rejoin" names the generation. Only the slot's own account gets one
  async rejoin(account, id) {
    this.limit(`rejoin:${account.id}`, 6, 6);
    const g = this.gameFor(account, id);
    const slot = this.slotOf(g, account);
    if (slot < 0) throw new LobbyError(403, 'not_in_game', 'you have no slot in this game');
    if (g.state !== 'started' || !g.starts?.[slot]) throw new LobbyError(409, 'not_started', 'the game has not started');
    if (g.left?.[slot]) throw new LobbyError(409, 'left', 'you left this game: it can not be rejoined'); // ONLINEUX-1
    g.rejoins ??= {};
    const generation = (g.rejoins[slot] ?? 0) + 1;
    if (generation > 0xffff) throw new LobbyError(409, 'rejoins', 'this slot rejoined too often');
    const keyId = Math.max(...this.config.ticketKeys.keys());
    const key = this.config.ticketKeys.get(keyId);
    const expires = Math.floor(this.env.now() / 1000) + this.config.ticketSeconds;
    const mask = g.mask ?? g.slots.reduce((m, s, i) => (s.state === 'player' ? m | (1 << i) : m), 0);
    const ticket = await signRejoinTicket(this.env, keyId, key, g.id, slot, mask, expires, generation);
    g.rejoins[slot] = generation;
    this.event(g, `${g.slots[slot].name} rejoins the game`);
    await this.save(g);
    this.social.rejoinStarted(g, account);
    return { v: VERSION, game: this.gameView(g, account), start: { ...g.starts[slot], ticket, rejoin: generation, session_key: await sessionKey(this.env, key, ticket) } }; // the fresh generation's own key
  }

  // lane ONLINE-3: a spectator START for a signed-in account that is not a player of the started game: a spectator ticket (a fresh index each time, so a
  // spectator whose game restarted asks again) and the game's setup; the relay serves it the game's log, `delay` seconds behind the players
  async spectate(account, id) {
    this.limit(`spectate:${account.id}`, 12, 12);
    const g = this.gameFor(account, id);
    await this.social.assertJoinAllowed(account, g);
    if (this.slotOf(g, account) >= 0) throw new LobbyError(409, 'player', 'players of the game play it; spectators are other accounts');
    if (g.roomCode) throw new LobbyError(403, 'private', 'a private room is not open to spectators'); // ONLINEHUB-1 (Sol)
    if (capacityOf(g) > MAX_SLOTS && account.maxSlots !== WIDE_SLOTS) throw new LobbyError(409, 'capacity', NEEDS_WIDE); // ONLINE16-FIX
    if (g.setup.requireAuth && account.maxSlots === undefined) throw new LobbyError(409, 'update', NEEDS_AUTH);
    if (g.state !== 'started' || !g.starts) throw new LobbyError(409, 'not_started', 'the game has not started yet: ask again once it has');
    const index = g.spectatorsIssued ?? 0;
    if (index >= 16) throw new LobbyError(409, 'spectators_full', 'the game has had its 16 spectator places');
    g.spectatorsIssued = index + 1;
    const keyId = Math.max(...this.config.ticketKeys.keys());
    const key = this.config.ticketKeys.get(keyId);
    const expires = Math.floor(this.env.now() / 1000) + this.config.ticketSeconds;
    const mask = g.mask ?? g.slots.reduce((m, s, i) => (s.state === 'player' ? m | (1 << i) : m), 0);
    const delay = g.setup.spectatorDelay ?? 120;
    const any = Object.values(g.starts)[0];
    const ticket = await signSpectatorTicket(this.env, keyId, key, g.id, index, mask, expires, delay);
    (g.spectators ??= []).push({ account: account.id, name: account.name, index });
    this.event(g, `${account.name} is watching`);
    await this.save(g);
    return {
      v: VERSION,
      start: { v: VERSION, kind: 'openbfme-spectate', game: g.id, spectator: index, relay: any.relay, ticket, session_key: await sessionKey(this.env, key, ticket), delay, runAhead: any.runAhead, crcInterval: any.crcInterval, profile: g.profile, setup: any.setup }, // lane AUTH-1: relay->spectator only
    };
  }

  // ---- the relay's command logs (upload_logs.py signs the body with the upload key) ----
  // CFDEPLOY-1: a platform without a log store (store.putBlob absent) refuses uploads and replay reads with 'no_log_store'; a store may cap a log
  // below MAX_LOG_BYTES (store.maxLogBytes: the Durable Object's SQLite store, worker.mjs)
  logStore() {
    if (!this.env.store.putBlob || !this.env.store.getBlob) throw new LobbyError(501, 'no_log_store', 'no relay log store is configured (LOG_STORE)');
    return this.env.store;
  }
  maxLogBytes() {
    return Math.min(MAX_LOG_BYTES, this.env.store.maxLogBytes ?? MAX_LOG_BYTES);
  }
  // ---- the relay's uploader status (INFRA-2 r2, Sol r1: the monitor could not see whether the relay's logs reach the lobby) ----
  // upload_logs.py posts after every run (once a minute) a JSON body signed like a log upload (X-OpenBFME-Signature: hex HMAC-SHA256(upload key, body)):
  // { v: 1, relay: "a.b.c.d:port", at: unix s (within ADMIN_WINDOW_S: an old status cannot be replayed to hide a fault), pending, partial,
  //   oldestPendingS, uploaded, failed, logErrors (the relay's own counter from its status file; null when the file could not be read) }.
  // The latest status per relay is kept (store.saveMeta) and read by the monitor through GET /v1/admin/relay-status.
  async relayStatusPost(bytes, signatureHex) {
    if (!this.config.uploadKey) throw new LobbyError(501, 'not_configured', 'log upload is not configured (UPLOAD_KEY)');
    const want = bytesToHex(await this.env.hmac(this.config.uploadKey, bytes));
    if (want !== signatureHex) throw new LobbyError(401, 'signature', 'the status signature does not match');
    let b;
    try {
      b = JSON.parse(dec.decode(bytes));
    } catch {
      throw bad('bad_json', 'the body must be JSON');
    }
    if (b?.v !== VERSION || typeof b.relay !== 'string' || !/^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5}$/.test(b.relay)) throw bad('bad_field', 'relay must be a.b.c.d:port');
    if (!Number.isInteger(b.at) || Math.abs(b.at - this.env.now() / 1000) > ADMIN_WINDOW_S) throw new LobbyError(401, 'status_clock', `the status's time is more than ${ADMIN_WINDOW_S} s from the lobby's`);
    const n = (k) => intIn(b[k], 0, 1e12, k);
    const rec = { relay: b.relay, at: b.at, receivedAt: Math.floor(this.env.now() / 1000), pending: n('pending'), partial: n('partial'),
      oldestPendingS: n('oldestPendingS'), uploaded: n('uploaded'), failed: n('failed'), logErrors: b.logErrors === null ? null : n('logErrors') };
    if (!this.relayStatus.has(rec.relay) && this.relayStatus.size >= 64) throw new LobbyError(507, 'too_many_relays', '64 relays reported a status already');
    this.relayStatus.set(rec.relay, rec);
    await this.env.store.saveMeta?.(`relay-status:${rec.relay}`, rec);
    return { v: VERSION, stored: rec.relay };
  }
  relayStatusList() {
    return { v: VERSION, now: Math.floor(this.env.now() / 1000), relays: [...this.relayStatus.values()].sort((x, y) => (x.relay < y.relay ? -1 : 1)) };
  }

  async uploadLog(id, bytes, signatureHex) {
    if (!this.config.uploadKey) throw new LobbyError(501, 'not_configured', 'log upload is not configured (UPLOAD_KEY)');
    const store = this.logStore();
    if (!/^[0-9a-f]{16}$/.test(id)) throw bad('bad_field', 'a game id is 16 hex digits');
    const max = this.maxLogBytes();
    if (bytes.length > max) throw new LobbyError(413, 'too_big', `a relay log of more than ${max} bytes (the log store's limit)`);
    const want = bytesToHex(await this.env.hmac(this.config.uploadKey, bytes));
    if (want !== signatureHex) throw new LobbyError(401, 'signature', 'the upload signature does not match');
    // CFDEPLOY-1 r2 (Sol r1: one signed log was accepted under two game ids): the log's own header names its game (services/online/relay/RelayCore.h,
    // "Log format": "OBFMERLG", u32 version (1 or 2), u64 game id, u8 slot mask, u32 start; the relay names the file %016llx of that id), the game must be one
    // this lobby started, and a stored log is never replaced by different bytes (the uploader's retry of the same bytes is accepted)
    const logGame = logGameOf(bytes);
    if (logGame !== id) throw new LobbyError(409, 'log_game_mismatch', `the log is game ${logGame}'s, not game ${id}'s`);
    if (!(await store.getBlob(`starts/${id}.json`))) throw new LobbyError(404, 'unknown_game', `game ${id} was not started by this lobby`);
    const key = `logs/${id}.obfmerlog`;
    const old = await store.getBlob(key);
    if (old) {
      if (old.length === bytes.length && old.every((x, i) => x === bytes[i])) {
        await this.markFinished(id);
        return { v: VERSION, stored: key, unchanged: true };
      }
      throw new LobbyError(409, 'log_exists', `game ${id} has a different log stored already`);
    }
    await store.putBlob(key, bytes);
    this.noteGameRecord('finished');
    await this.markFinished(id); // ONLINECOUNT-1: the relay's log is in, the game is over
    return { v: VERSION, stored: key };
  }
  // ONLINECOUNT-1 r2 (Sol: a restart brought finished games back as running): the game's record keeps `finished`, so a reload knows (one save per game end;
  // the seq and the idle clock are not touched)
  async markFinished(id) {
    const g = this.games.get(id);
    const candidate = g ? structuredClone(g) : null;
    if (candidate) candidate.finished = true;
    await this.finishReportRoster(id, candidate);
    if (!g) return;
    g.finished = true;
    if (candidate.reportEndedAt !== undefined) g.reportEndedAt = candidate.reportEndedAt;
    this.statsCache = null;
    await this.env.store.saveGame?.(g);
  }
  async report(account, rawBody, idempotencyKey) {
    account = this.checkAccount(account);
    const result = await submitPlayerReport({
      store: this.env.store,
      now: () => this.env.now(),
      randomBytes: this.env.randomBytes,
      account,
      rawBody,
      idempotencyKey,
      getLiveGame: (id) => this.games.get(id) ?? null,
    });
    return result;
  }
  async chargeReportAttempt(install) {
    return chargeReportAttempt(this.env.store, install, () => this.env.now());
  }
  async adminReports(after, limit) {
    return readPlayerReports({
      store: this.env.store,
      now: () => this.env.now(),
      hmac: this.env.hmac,
      modKey: this.config.modKey,
      after,
      limit,
    });
  }
  async adminReport(id) {
    return readPlayerReport({ store: this.env.store, now: () => this.env.now(), reportId: id });
  }
  async replayFile(account, id, which) {
    const store = this.logStore();
    if (!/^[0-9a-f]{16}$/.test(id)) throw bad('bad_field', 'a game id is 16 hex digits');
    const players = await store.getBlob(`players/${id}.json`);
    if (!players) throw new LobbyError(404, 'no_replay', `no replay for game ${id}`);
    if (!JSON.parse(dec.decode(players)).includes(account.id)) throw new LobbyError(403, 'not_a_player', `only the players of game ${id} read its replay`);
    const key = which === 'log' ? `logs/${id}.obfmerlog` : `starts/${id}.json`;
    const b = await store.getBlob(key);
    if (!b) throw new LobbyError(404, 'no_replay', `no ${which} for game ${id}`);
    if (which === 'start') {
      let start;
      try {
        start = JSON.parse(dec.decode(b));
      } catch {
        console.warn(`lobby: skipping malformed replay START ${id}`);
        throw new LobbyError(404, 'no_replay', `no start for game ${id}`);
      }
      const clean = repairStoredText(start);
      return clean === start ? b : enc.encode(JSON.stringify(clean));
    }
    return b;
  }

  // ---- the admin backup (INFRA-2): list, export and import the stored blobs (relay logs, replay START records, player lists) ----
  // Every admin request carries X-OpenBFME-Admin: <unix seconds>.<hex HMAC-SHA256(ADMIN_KEY, "<METHOD> <path>?<query>\n<seconds>\n" + body)>, within
  // ADMIN_WINDOW_S of the lobby's clock. A replayed request inside the window can only repeat a read or an identical import (blobs are never replaced).
  // The admin key is its own secret (admin.key), not the relays' upload key: a relay host can store its logs but never read anyone's.
  async adminCheck(req, bytes, keyKinds = ['admin']) {
    const keys = keyKinds.map((kind) => kind === 'admin' ? this.config.adminKey :
      kind === 'mod' ? this.config.modKey : this.config.enforcementCheckKey).filter(Boolean);
    if (!keys.length) throw new LobbyError(501, 'not_configured', 'the requested admin key is not configured');
    const m = /^(\d{1,12})\.([0-9a-f]{64})$/.exec(String(req.headers['x-openbfme-admin'] ?? ''));
    if (!m) throw new LobbyError(401, 'admin_signature', 'an admin request needs X-OpenBFME-Admin: <unix seconds>.<hex HMAC>');
    if (Math.abs(Number(m[1]) - this.env.now() / 1000) > ADMIN_WINDOW_S) throw new LobbyError(401, 'admin_clock', `the admin signature's time is more than ${ADMIN_WINDOW_S} s from the lobby's`);
    const q = req.query.toString();
    const head = enc.encode(`${req.method} ${req.path}?${q}\n${m[1]}\n`);
    const signed = new Uint8Array(head.length + bytes.length);
    signed.set(head);
    signed.set(bytes, head.length);
    let matched = 0;
    for (const key of keys) {
      const want = bytesToHex(await this.env.hmac(key, signed));
      let diff = 0;
      for (let i = 0; i < 64; i++) diff |= want.charCodeAt(i) ^ m[2].charCodeAt(i);
      matched |= Number(diff === 0);
    }
    if (!matched) throw new LobbyError(401, 'admin_signature', 'the admin signature does not match');
  }

  async enforcementCheck(req, raw) {
    await this.adminCheck(req, raw, ['check']);
    if (req.query.toString() !== '') throw bad('bad_query', 'the enforcement check query must be empty');
    let body;
    try {
      body = JSON.parse(dec.decode(raw));
    } catch {
      throw bad('bad_json', 'the body must be JSON');
    }
    exactFields(body, ['v', 'nonce', 'installs'], 'enforcement check');
    if (body.v !== VERSION || typeof body.nonce !== 'string' || !/^[0-9a-f]{32}$/.test(body.nonce) ||
        !Array.isArray(body.installs) || body.installs.length < 1 || body.installs.length > 128 ||
        body.installs.some((install) => !validInstall(install)) ||
        new Set(body.installs).size !== body.installs.length) {
      throw bad('bad_field', 'enforcement check requires a unique nonce and 1 .. 128 installs');
    }
    const now = Math.floor(this.env.now() / 1000);
    const decisions = body.installs.map((install) => {
      const state = this.getEnforcement(install);
      const direct = isActiveBlock(state?.block, now);
      const sighting = this.getSighting(install);
      let device = null;
      if (sighting && !sightingExpired(sighting, now)) {
        for (const { hash } of sighting.devices) {
          const candidate = this.deviceBlock(hash);
          if (compareBlocks(candidate, device) > 0) device = candidate;
        }
      }
      const block = compareBlocks(device, direct) > 0 ? device : direct;
      return {
        install,
        allowed: !block,
        code: block ? (block.kind === 'ban' ? 'banned' : 'suspended') : null,
        routing: state?.routing ?? '',
      };
    });
    return { v: VERSION, nonce: body.nonce, checked_at: now, decisions };
  }
  // one page of the stored blobs in key order after `after`: { blobs: [{ key, size, storedAt (ms) }], next (the `after` of the next page, or null) }
  async adminList(after, limit) {
    const store = this.logStore();
    if (!store.listBlobs) throw new LobbyError(501, 'no_listing', `the ${store.kind} log store cannot list its blobs`);
    const n = limit === null ? ADMIN_PAGE : Number(limit);
    if (!Number.isInteger(n) || n < 1 || n > ADMIN_PAGE) throw bad('bad_field', `limit must be an integer in 1 .. ${ADMIN_PAGE}`);
    const blobs = await store.listBlobs(after ?? '', n);
    return { v: VERSION, blobs, next: blobs.length === n ? blobs[n - 1].key : null };
  }
  async adminGet(key) {
    if (!parseBlobKey(key)) throw bad('bad_field', 'not a blob key (logs/<id>.obfmerlog, starts/<id>.json, players/<id>.json, archives/<sha256>.json)');
    const b = await this.logStore().getBlob(key);
    if (!b) throw new LobbyError(404, 'no_blob', `no blob ${key}`);
    return b;
  }
  // the import a restore runs: a blob is written only where none is; the same bytes again answer 'unchanged', other bytes 409 'blob_exists'
  async adminPut(key, bytes) {
    const store = this.logStore();
    const k = parseBlobKey(key);
    if (!k) throw bad('bad_field', 'not a blob key (logs/<id>.obfmerlog, starts/<id>.json, players/<id>.json, archives/<sha256>.json)');
    if (k.kind === 'logs') {
      if (bytes.length > this.maxLogBytes()) throw new LobbyError(413, 'too_big', `a relay log of more than ${this.maxLogBytes()} bytes (the log store's limit)`);
      const logGame = logGameOf(bytes);
      if (logGame !== k.id) throw new LobbyError(409, 'log_game_mismatch', `the log is game ${logGame}'s, not game ${k.id}'s`);
    } else {
      let doc;
      try {
        doc = JSON.parse(dec.decode(bytes));
      } catch {
        throw bad('bad_json', `${key} must be JSON`);
      }
      if (k.kind === 'starts' && (doc?.kind !== 'openbfme-replay-start' || doc.game !== k.id)) throw bad('bad_blob', `${key} is not game ${k.id}'s replay START record`);
      if (k.kind === 'players' && !(Array.isArray(doc) && doc.every((a) => typeof a === 'string'))) throw bad('bad_blob', `${key} is not a list of account ids`);
      if (k.kind === 'archives') { // lane PROFILE-2: a restore brings back exactly the list its key names
        const lines = doc?.lines;
        if (doc?.v !== VERSION || !Array.isArray(lines) || lines.length < 1 || lines.length > MAX_ARCHIVE_LINES || !lines.every((l) => typeof l === 'string' && ARCHIVE_LINE.test(l))
          || typeof this.env.sha256 !== 'function' || bytesToHex(await this.env.sha256(enc.encode(lines.map((l) => `${l}\n`).join('')))) !== k.id) {
          throw bad('bad_blob', `${key} is not the archive list its SHA-256 names`);
        }
      }
    }
    const old = await store.getBlob(key);
    if (old) {
      if (old.length === bytes.length && old.every((x, i) => x === bytes[i])) return { v: VERSION, stored: key, unchanged: true };
      throw new LobbyError(409, 'blob_exists', `${key} holds different bytes already`);
    }
    await store.putBlob(key, bytes);
    if (k.kind !== 'players') {
      this.gameTimes = null;
      this.statsCache = null;
    }
    return { v: VERSION, stored: key };
  }

  adminGames(state = null) {
    if (state !== null && state !== '' && state !== 'open' && state !== 'started') throw bad('bad_field', 'state must be open or started');
    const games = [...this.games.values()]
      .filter((g) => (g.state === 'open' || g.state === 'started') && (!state || g.state === state))
      .sort((a, b) => b.created - a.created || compareText(a.id, b.id))
      .map((g) => ({
        id: g.id,
        seq: g.seq,
        name: g.name,
        state: g.state,
        map: g.setup.map,
        password: !!g.password,
        routing: g.routing ?? '',
        created: Math.floor(g.created / 1000),
        started: g.startedAt ? Math.floor(g.startedAt / 1000) : null,
        spectators: g.spectators?.length ?? g.spectatorsIssued ?? 0,
        slots: g.slots.map((slot, index) => ({
          slot: index,
          account: slot.account ?? null,
          name: slot.name ?? null,
          install: slot.install ?? null,
          faction: slot.faction,
          team: slot.team,
          state: slot.state,
        })),
      }));
    return { v: VERSION, games };
  }

  async moderateGame(req, raw, operation, id, input) {
    const store = this.env.store;
    if (typeof store.claimModerationReceipt !== 'function' ||
        typeof store.completeModerationReceipt !== 'function' ||
        typeof this.env.sha256 !== 'function') {
      throw new LobbyError(503, 'command_store_unavailable', 'durable moderation command receipts are not configured');
    }
    const digest = async (bytes) => {
      const result = await this.env.sha256(bytes);
      if (!(result instanceof Uint8Array) || result.length !== 32) {
        throw new LobbyError(503, 'command_store_unavailable', 'durable moderation command receipts are not configured');
      }
      return bytesToHex(result);
    };
    const parts = [
      enc.encode(operation),
      enc.encode(req.method),
      enc.encode(req.path),
      enc.encode(req.query.toString()),
      raw,
    ];
    const packed = new Uint8Array(parts.reduce((size, part) => size + 4 + part.length, 0));
    const view = new DataView(packed.buffer);
    let offset = 0;
    for (const part of parts) {
      view.setUint32(offset, part.length);
      offset += 4;
      packed.set(part, offset);
      offset += part.length;
    }
    let commandHash;
    let fingerprint;
    try {
      commandHash = await digest(enc.encode(`openbfme-moderation-command-v1\0${input.command_id}`));
      fingerprint = await digest(packed);
    } catch (error) {
      if (error instanceof LobbyError) throw error;
      throw new LobbyError(503, 'command_store_unavailable', 'durable moderation command receipts are unavailable');
    }

    let previous;
    try {
      previous = await store.claimModerationReceipt(commandHash, { v: VERSION, fingerprint, status: 'pending' });
    } catch {
      throw new LobbyError(503, 'command_store_unavailable', 'durable moderation command receipts are unavailable');
    }
    if (previous !== null) {
      const receipt = storedModerationReceipt(previous);
      if (receipt.fingerprint !== fingerprint) {
        throw new LobbyError(409, 'command_conflict', 'command_id was already used for a different request');
      }
      if (receipt.status === 'pending') {
        throw new LobbyError(409, 'command_pending', 'the command result is pending operator reconciliation');
      }
      return receipt.response;
    }

    const complete = async (response) => {
      try {
        await store.completeModerationReceipt(commandHash, { v: VERSION, fingerprint, status: 'complete', response });
      } catch {
        throw new LobbyError(503, 'command_store_unavailable', 'moderation command completion could not be recorded');
      }
      return response;
    };
    try {
      const body = operation === 'kick'
        ? await this.adminKickGame(id, input.slot, input.reason, input.expected_seq, input.expected_account, input.expected_install)
        : await this.adminCloseGame(id, input.reason, input.expected_seq);
      return await complete({ status: 200, body });
    } catch (error) {
      if (!(error instanceof LobbyError) || error.status >= 500) throw error;
      return await complete({
        status: error.status,
        body: { v: VERSION, error: error.code, message: error.message, ...error.details },
      });
    }
  }

  assertModerationSequence(game, expected) {
    if (!Number.isSafeInteger(game.seq) || game.seq < 0) {
      throw new LobbyError(503, 'game_unavailable', 'the game revision is unavailable');
    }
    if (game.seq !== expected) throw new LobbyError(409, 'stale_game', 'the game changed after the moderator selected it');
  }

  async adminKickGame(id, slotIndex, reason, expectedSeq, expectedAccount, expectedInstall) {
    const g = this.game(id);
    this.assertModerationSequence(g, expectedSeq);
    const slot = intIn(slotIndex, 0, MAX_SLOTS - 1, 'slot');
    const why = modReason(reason);
    const target = g.slots[slot];
    if (target.account !== expectedAccount || (target.install ?? null) !== expectedInstall) {
      throw new LobbyError(409, 'stale_target', 'the selected slot occupant changed');
    }
    const targetName = target.name;
    const effect = g.state === 'started' ? 'rejoin_refused' : 'removed_from_room';
    if (g.state === 'open' && slot === g.hostSlot) {
      const candidate = structuredClone(g);
      candidate.state = 'closed';
      candidate.closeReason = why;
      await this.finishReportRoster(id, candidate);
      restoreGame(g, candidate);
      this.event(g, 'the room was closed');
      await this.save(g);
      this.games.delete(id);
      await this.env.store.deleteGame?.(id);
      return { v: VERSION, id, closed: true, effect };
    }
    if (target.state !== 'player') throw bad('bad_field', 'kick a player slot');
    if (g.state === 'open') {
      const candidate = structuredClone(g);
      const candidateTarget = candidate.slots[slot];
      if (slot === g.hostSlot) throw bad('bad_field', 'kick a non-host player slot');
      (candidate.adminKicks ??= []).push({ slot, account: candidateTarget.account, install: candidateTarget.install ?? null, reason: why });
      // DEVINPR-1 (review of WP-C): a moderator's kick keeps the player out of this room (before: they could join it again at once), as for a started game
      (candidate.denied ??= []).push({ slot, account: candidateTarget.account, install: candidateTarget.install ?? null });
      Object.assign(candidateTarget, { state: 'open', account: undefined, install: null, name: undefined, platform: undefined, ready: false, ...NO_CHOICES });
      (candidate.kicked ??= []).push(slot);
      this.unreadyAll(candidate);
      await this.syncReportRoster(candidate);
      restoreGame(g, candidate);
    } else if (g.state === 'started') {
      g.denied ??= [];
      const denied = g.denied.find((entry) => entry.account === target.account && entry.install === (target.install ?? null));
      if (!denied) g.denied.push({ slot, account: target.account, install: target.install ?? null });
      (g.adminKicks ??= []).push({ slot, account: target.account, install: target.install ?? null, reason: why });
    } else {
      throw new LobbyError(409, 'not_open', 'the game is not open');
    }
    this.event(g, `${targetName} was kicked`);
    await this.save(g);
    return { v: VERSION, id, slot, kicked: true, effect };
  }

  async adminCloseGame(id, reason, expectedSeq) {
    const g = this.game(id);
    this.assertModerationSequence(g, expectedSeq);
    if (g.state !== 'open') throw new LobbyError(409, 'not_open', 'only an open game can be closed');
    const candidate = structuredClone(g);
    candidate.closeReason = modReason(reason);
    candidate.state = 'closed';
    await this.finishReportRoster(id, candidate);
    restoreGame(g, candidate);
    this.event(g, 'the room was closed');
    await this.save(g);
    this.games.delete(id);
    await this.env.store.deleteGame?.(id);
    return { v: VERSION, id, closed: true };
  }

  playerSummary(record) {
    return {
      install: record.install,
      nick: record.nicks[0]?.nick ?? '',
      nick_history: record.nicks.map((entry) => ({ ...entry })),
      first_seen: record.first_seen,
      last_seen: record.last_seen,
      signins: record.signins,
      platform: knownPlatform(record.platform),
      version: record.version,
      devices: record.devices.length,
      in_game: this.gameForInstall(record.install),
    };
  }

  adminPlayers(query = '', after = null, limit = null) {
    const q = str(query, 32, 'q', { empty: true }).toLocaleLowerCase('en-US');
    const now = Math.floor(this.env.now() / 1000);
    const pageSize = limit === null ? PLAYER_PAGE : Number(limit);
    if (!Number.isInteger(pageSize) || pageSize < 1 || pageSize > PLAYER_PAGE) throw bad('bad_field', `limit must be an integer in 1 .. ${PLAYER_PAGE}`);
    let cursor = null;
    if (after !== null && after !== '') {
      try {
        if (after.length > 512) throw new Error('long cursor');
        cursor = JSON.parse(dec.decode(base64UrlDecode(after)));
        exactFields(cursor, ['q', 'last_seen', 'install'], 'after');
        if (cursor.q !== q || !validInstall(cursor.install) || !Number.isSafeInteger(cursor.last_seen) || cursor.last_seen < 0) throw new Error('invalid cursor');
      } catch {
        throw bad('bad_field', 'after is not a valid cursor for this query');
      }
    }
    if (this.identityIndexed()) {
      const rows = this.identityStore().players(q, cursor, pageSize + 1, now);
      const selected = rows.slice(0, pageSize);
      const next = rows.length > pageSize && selected.length
        ? base64(enc.encode(JSON.stringify({ q, last_seen: selected.at(-1).last_seen, install: selected.at(-1).install })), true)
        : null;
      return { v: VERSION, players: selected.map((record) => this.playerSummary(record)), next };
    }
    const rows = [...this.sightings.values()].filter((record) => !sightingExpired(record, now) && (!q ||
      record.install.startsWith(q) || record.nicks.some((entry) => entry.nick.toLocaleLowerCase('en-US').startsWith(q))))
      .sort((a, b) => b.last_seen - a.last_seen || compareText(a.install, b.install))
      .filter((record) => !cursor || record.last_seen < cursor.last_seen ||
        (record.last_seen === cursor.last_seen && record.install > cursor.install));
    const selected = rows.slice(0, pageSize);
    const next = rows.length > pageSize && selected.length
      ? base64(enc.encode(JSON.stringify({ q, last_seen: selected.at(-1).last_seen, install: selected.at(-1).install })), true)
      : null;
    return { v: VERSION, players: selected.map((record) => this.playerSummary(record)), next };
  }

  adminPlayer(install) {
    if (!validInstall(install)) throw bad('bad_field', 'install must be 32 lowercase hex digits');
    const now = Math.floor(this.env.now() / 1000);
    const record = this.getSighting(install);
    if (!record || sightingExpired(record, now)) throw new LobbyError(404, 'no_player', `no player sighting for ${install}`);
    const deviceSet = new Set(record.devices.map((entry) => entry.hash));
    const linked = [];
    if (this.identityIndexed()) {
      return { v: VERSION, ...this.playerSummary(record), linked: this.identityStore().linked(install, now) };
    }
    for (const [hash, installs] of this.sightingDeviceOwners) {
      if (!deviceSet.has(hash)) continue;
      for (const other of installs) {
        const item = this.sightings.get(other);
        if (other !== install && item && !sightingExpired(item, now)) linked.push(other);
      }
    }
    const counts = new Map();
    for (const other of linked) counts.set(other, (counts.get(other) ?? 0) + 1);
    const players = [...counts].map(([other, shared_devices]) => {
      const item = this.sightings.get(other);
      return { install: other, nick: item.nicks[0]?.nick ?? '', shared_devices, signins: item.signins, last_seen: item.last_seen };
    }).sort((a, b) => b.shared_devices - a.shared_devices || b.last_seen - a.last_seen || compareText(a.install, b.install));
    return { v: VERSION, ...this.playerSummary(record), linked: players };
  }

  adminEnforcement(after = null) {
    if (after !== null && after !== '' && !validInstall(after)) throw bad('bad_field', 'after must be a 32-digit install id');
    if (this.identityIndexed()) {
      const rows = this.identityStore().enforcementPage(after, ENFORCEMENT_PAGE + 1);
      const selected = rows.slice(0, ENFORCEMENT_PAGE);
      return {
        v: VERSION,
        items: selected.map(([install, state]) => ({ install, ...state, devices: [...state.devices] })),
        next: rows.length > ENFORCEMENT_PAGE ? selected.at(-1)[0] : null,
      };
    }
    const rows = [...this.enforcement].filter(([install]) => !after || install > after).sort(([a], [b]) => compareText(a, b));
    const selected = rows.slice(0, ENFORCEMENT_PAGE);
    return {
      v: VERSION,
      items: selected.map(([install, state]) => ({ install, ...state, devices: [...state.devices] })),
      next: rows.length > ENFORCEMENT_PAGE ? selected.at(-1)[0] : null,
    };
  }

  // whether an install is under an active block now (identity bans follow it)
  installBlocked(install) {
    return !!isActiveBlock(this.getEnforcement(install)?.block, Math.floor(this.env.now() / 1000));
  }

  async setEnforcement(install, body) {
    if (!validInstall(install)) throw bad('bad_field', 'install must be 32 lowercase hex digits');
    const previous = this.getEnforcement(install);
    const next = validateEffectiveState(body);
    if (previous?.block_devices === true && next.block_devices === true && previous.version === next.version) {
      next.devices = [...previous.devices];
    }
    if (previous && next.version < previous.version) return { v: VERSION, stale: true };
    if (previous && next.version === previous.version) {
      if (JSON.stringify(previous) === JSON.stringify(next)) return { v: VERSION, unchanged: true };
      throw new LobbyError(409, 'version_conflict', 'the same version has different effective state');
    }
    if (next.block_devices) {
      const sighting = this.getSighting(install);
      next.devices = previous?.block_devices === true && previous.version === next.version
        ? [...previous.devices]
        : sighting && !sightingExpired(sighting, Math.floor(this.env.now() / 1000))
          ? sighting.devices.map((device) => device.hash).slice(0, 5).sort()
          : [];
    }
    if (typeof this.env.store.saveEnforcement !== 'function') throw new LobbyError(501, 'no_enforcement_store', 'enforcement storage is not configured');
    await this.env.store.saveEnforcement({ install, ...next });
    this.social.securityStateChanged();
    if (isActiveBlock(next.block, Math.floor(this.env.now() / 1000))) await this.social.syncIdentityBans(install); // ONLINEMENU-1
    if (previous && !this.identityIndexed()) {
      for (const device of previous.devices) {
        const owners = this.enforcementDeviceOwners.get(device);
        owners?.delete(install);
        if (owners && !owners.size) this.enforcementDeviceOwners.delete(device);
      }
    }
    if (!this.identityIndexed()) {
      this.enforcement.set(install, next);
      for (const device of next.devices) {
        const owners = this.enforcementDeviceOwners.get(device) ?? new Set();
        owners.add(install);
        this.enforcementDeviceOwners.set(device, owners);
      }
    }
    this.statsCache = null;
    this.wakeAll();
    return { v: VERSION, version: next.version, updated: true };
  }

  health() {
    const relays = this.config.relays.map((r) => ({ id: r.id, address: r.address }));
    return {
      v: VERSION,
      ok: true,
      games: this.games.size,
      relays,
      ...(relays.length ? {} : { relay: 'no relay configured' }),
      stats: STATS_STOP,
      logStore: this.env.store.kind ?? (this.env.store.putBlob ? 'yes' : 'none'),
      emailConfig: this.config.email === null || this.config.email === undefined ? 'off' : this.config.email.error ?? 'ready', // ONLINEMENU-1
      // ONLINEMENU-1 (BATCH-P3): what this lobby serves beyond the games, so a client turns each part on only where it exists (an older lobby
      // lists nothing: the game's Messenger and the launcher's sign-in then say "not available on this server yet")
      features: [
        'social',
        'build', // lane JOINVER-1: list rows carry the host's build and profile; a 409 profile names both builds
        ...(this.env.store.putBlob && typeof this.env.sha256 === 'function' ? ['archives'] : []), // lane PROFILE-2: POST /v1/profile-archives
        ...(this.config.oauth?.discord ? ['oauth'] : []),
        ...(this.config.email && !this.config.email.error ? ['email'] : []),
      ],
    };
  }
}

// ---- routing: one function both platforms call -------------------------------------------------------------------------------------------------------------
// req: { method, path, query (URLSearchParams), headers (lower-case keys), json() -> Promise, bytes() -> Promise<Uint8Array>, ip }
// returns { status, headers, body (string | Uint8Array) }
export async function route(lobby, req) {
  const json = (status, obj) => ({
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store',
      ...(status === 429 && Number.isSafeInteger(obj?.retry_after) ? { 'retry-after': String(Math.max(1, obj.retry_after)) } : {}),
    },
    body: JSON.stringify(repairStoredText(obj)),
  });
  const failure = (error) => ({ v: VERSION, error: error.code, message: error.message, ...error.details });
  let reportRequest = false;
  try {
    const p = req.path.split('/').filter(Boolean);
    const m = req.method;
    reportRequest = p[0] === 'v1' && (
      (p[1] === 'reports' && p.length === 2) ||
      (p[1] === 'admin' && p[2] === 'mod' && p[3] === 'reports' && (p.length === 4 || p.length === 5))
    );
    const rawBody = async () => {
      if (req.bodyRejection) {
        throw new LobbyError(req.bodyRejection.status, req.bodyRejection.code, req.bodyRejection.message);
      }
      // ONLINE-2 r2 (Sol: a chunked 20 KB body passed the Content-Length check): the bytes actually received count, whatever the headers say
      const limit = req.path === '/v1/profile-archives' ? MAX_ARCHIVES_BYTES : MAX_JSON_BYTES; // lane PROFILE-2: a JSON body's bound (bodyLimitFor)
      if (Number(req.headers['content-length'] ?? 0) > limit) throw new LobbyError(413, 'too_big', `request body over ${limit} bytes`);
      const raw = await req.bytes();
      if (raw.length > limit) throw new LobbyError(413, 'too_big', `request body over ${limit} bytes`);
      return raw;
    };
    const parseBody = (raw) => {
      try {
        return raw.length ? (JSON.parse(dec.decode(raw)) ?? {}) : {};
      } catch {
        throw bad('bad_json', 'the body must be JSON');
      }
    };
    const body = async () => parseBody(await rawBody());
    const v1 = p[0] === 'v1';
    const hasAuthorization = Object.hasOwn(req.headers, 'authorization');
    const pollRoute = m === 'GET' && (p[1] === 'stats' || p[1] === 'motd' || p[1] === 'games' ||
      p[1] === 'inbox' || p[1] === 'friends');
    if (v1) await lobby.prepareReportExpiries?.({ cleanup: !pollRoute });
    if (m === 'GET' && p[0] === 'j' && p.length === 2 && /^[0-9a-f]{16}$/.test(p[1])) {
      // the invite page: the openbfme:// link (the launcher's handler) and the id for the debug join
      const link = `openbfme://join/${p[1]}`;
      return { status: 200, headers: { 'content-type': 'text/html; charset=utf-8' }, body: `<!doctype html><meta charset="utf-8"><title>OpenBFME game invite</title><h1>OpenBFME online game</h1><p><a href="${link}">Join game ${p[1]}</a> (opens the OpenBFME launcher).</p><p>Or start OpenBFME with <code>--online-join ${p[1]}</code>. Online play is a test feature.</p>` };
    }
    // ONLINEMENU-1 (owner 2026-10-20): the e-mail sign-in pages (docs/IDENTITY.md "E-mail"): the request page (the address and Turnstile) and the
    // confirm page (one button; opening it consumes nothing)
    if (m === 'GET' && p[0] === 'email' && (p.length === 2 || (p.length === 3 && p[1] === 'verify'))) {
      const nonce = base64(lobby.env.randomBytes(16), true);
      const page = (kind, html) => ({ status: 200, headers: { 'content-type': 'text/html; charset=utf-8', ...pageHeaders(kind, kind === 'error' ? undefined : nonce) }, body: html });
      const config = lobby.config.email;
      if (!config || config.error) return page('error', emailErrorPage('E-mail sign-in is not available on this lobby.'));
      lobby.limit(`email-page:${req.ip}`, 30, 30);
      if (p.length === 2) {
        return /^[0-9a-f]{48}$/.test(p[1])
          ? page('request', emailRequestPage({ ticket: p[1], siteKey: config.turnstileSiteKey, nonce, privacyUrl: config.privacyUrl }))
          : page('error', emailErrorPage('This page address is not valid: start again in the launcher.'));
      }
      return /^[A-Za-z0-9_-]{43}$/.test(p[2])
        ? page('verify', emailVerifyPage({ token: p[2], nonce }))
        : page('error', emailErrorPage('This link is not valid: start again in the launcher.'));
    }
    if (!v1) return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
    if (m === 'GET' && p[1] === 'health') {
      if (hasAuthorization) await lobby.account(req.headers.authorization);
      return json(200, lobby.health());
    }
    // ONLINE-4: the message of the day the game's online home page shows (OnlineHome.apt); null when the lobby has none
    // ONLINECOUNT-1: the counts the menus and the launcher show. No sign-in needed; a valid token on the request also marks its player present
    if (m === 'GET' && p[1] === 'stats' && p.length === 2) {
      lobby.limit(`stats:${req.ip}`, 60, 20);
      if (hasAuthorization) {
        await lobby.account(req.headers.authorization, { pollCache: true });
        return { status: 200, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' }, body: JSON.stringify(lobby.stats()) };
      }
      return { status: 200, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': `public, max-age=${STATS_CACHE_S}` }, body: JSON.stringify(lobby.stats()) };
    }
    if (m === 'GET' && p[1] === 'motd' && p.length === 2) {
      if (hasAuthorization) await lobby.account(req.headers.authorization, { pollCache: true });
      return json(200, { v: VERSION, motd: lobby.config.motd || null });
    }
    if (m === 'POST' && p[1] === 'auth' && p[2] === 'guest') return json(200, await lobby.guest(await body(), req.ip));
    if (p[1] === 'auth' && p[2] === 'email') {
      // ONLINEMENU-1: the e-mail sign-in's API (SocialService.emailStart / emailRequest / emailVerify / emailPoll)
      const target = p[3];
      if (m !== 'POST' || p.length !== 4 || !['start', 'request', 'verify', 'poll'].includes(target) || req.path !== `/v1/auth/email/${target}`) {
        return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
      }
      if (req.query.size) throw bad('bad_query', 'this route does not accept query parameters');
      const input = await body();
      const options = { authorization: req.headers.authorization, hasAuthorization, ip: req.ip };
      const result = target === 'start' ? await lobby.social.emailStart(input, options)
        : target === 'request' ? await lobby.social.emailRequest(input, options)
          : target === 'verify' ? await lobby.social.emailVerify(input, options)
            : await lobby.social.emailPoll(input, options);
      return json(200, result);
    }
    if (p[1] === 'auth' && p[2] === 'oauth') {
      const target = p[3];
      const canonical = req.path === `/v1/auth/oauth/${target}`;
      const route = canonical && p.length === 4 && (
        (m === 'POST' && ['start', 'finish', 'unlink'].includes(target)) ||
        (m === 'GET' && target === 'accounts')
      );
      if (!route) return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
      if (req.query.size) {
        const error = bad('bad_query', 'this route does not accept query parameters');
        if (m === 'POST') await lobby.social.chargeOAuth(target === 'start' ? 'start' : target === 'finish' ? 'finish' : 'unlink', req.ip, null, { global: null });
        throw error;
      }
      if (m === 'GET') return json(200, await lobby.social.oauthAccounts(req.headers.authorization));
      const kind = target === 'start' ? 'start' : target === 'finish' ? 'finish' : 'unlink';
      // ONLINEMENU-1 (PR #8 fix, finding 1): the address alone before the body is read; the global budget is charged by the operation, after the
      // address, install and player scopes passed and the body is well formed
      await lobby.social.chargeOAuth(kind, req.ip, null, { global: null });
      const input = await body();
      const options = {
        authorization: req.headers.authorization,
        hasAuthorization,
        ip: req.ip,
        charged: true,
        signal: req.signal,
      };
      const result = target === 'start'
        ? await lobby.social.oauthStart(input, options)
        : target === 'finish'
          ? await lobby.social.oauthFinish(input, options)
          : await lobby.social.oauthUnlink(input, options);
      if (result?.deferred) {
        return {
          deferred: result.deferred.then((answer) => json(200, answer)).catch((error) => {
            if (error instanceof LobbyError || error instanceof SocialError) return json(error.status, failure(error));
            if (error instanceof SocialStorageError) {
              const full = error.kind === 'full';
              const normalized = {
                status: full ? 507 : 503,
                code: full ? 'social_store_full' : 'social_unavailable',
                message: full ? 'social storage is full' : 'social storage is unavailable',
              };
              return json(normalized.status, failure(normalized));
            }
            return json(500, { v: VERSION, error: 'internal', message: 'internal error' });
          }),
        };
      }
      return json(200, result);
    }
    if (['me', 'friends', 'blocks', 'recent', 'invites', 'inbox', 'chat'].includes(p[1])) {
      const socialPath = p;
      const inboxPoll = m === 'GET' && socialPath[1] === 'inbox';
      const friendsPoll = m === 'GET' && socialPath[1] === 'friends';
      const account = await lobby.account(req.headers.authorization, {
        activity: !inboxPoll,
        pollCache: inboxPoll || friendsPoll,
      });
      let input = null;
      if (m === 'POST' || m === 'PUT') {
        try {
          input = await body();
        } catch (error) {
          if (m === 'POST') await lobby.social.chargeRejectedMutation(account, socialPath, req.ip);
          throw error;
        }
      }
      let result;
      try {
        result = await lobby.social.dispatch(account, m, socialPath, req.query, input, req.ip, {
          signal: req.signal,
          authorization: req.headers.authorization,
        });
      } catch (error) {
        if (m === 'POST' && error instanceof SocialError && error.code === 'bad_query') {
          await lobby.social.chargeRejectedMutation(account, socialPath, req.ip);
        }
        throw error;
      }
      if (result?.deferred) {
        return {
          deferred: result.deferred.then((answer) => json(200, answer)).catch((error) => {
            if (error instanceof LobbyError || error instanceof SocialError) return json(error.status, failure(error));
            if (error instanceof SocialStorageError) {
              const full = error.kind === 'full';
              const normalized = { status: full ? 507 : 503, code: full ? 'social_store_full' : 'social_unavailable',
                message: full ? 'social storage is full' : 'social storage is unavailable' };
              return json(normalized.status, failure(normalized));
            }
            return json(500, { v: VERSION, error: 'internal', message: 'internal error' });
          }),
        };
      }
      return json(200, result);
    }
    if (m === 'POST' && p[1] === 'reports' && p.length === 2) {
      const account = await lobby.account(req.headers.authorization);
      if (!account.install) throw new ReportError(401, 'install_required', 'sign in with an install identity before reporting');
      if (req.bodyRejection) {
        const retry = await lobby.chargeReportAttempt(account.install);
        if (retry !== null) throw new ReportError(429, 'rate_limited', 'the report attempt limit has been reached', { retry_after: retry });
        throw new LobbyError(req.bodyRejection.status, req.bodyRejection.code, req.bodyRejection.message);
      }
      let raw;
      try {
        raw = await rawBody();
      } catch (error) {
        if (error instanceof LobbyError && error.status === 413) {
          if (!account.install) throw new ReportError(401, 'install_required', 'sign in with an install identity before reporting');
          const retry = await lobby.chargeReportAttempt(account.install);
          if (retry !== null) throw new ReportError(429, 'rate_limited', 'the report attempt limit has been reached', { retry_after: retry });
        }
        throw error;
      }
      const result = await lobby.report(account, raw, req.headers['idempotency-key']);
      const response = json(201, result.receipt);
      if (result.replay) response.headers['idempotent-replay'] = 'true';
      return response;
    }
    if (m === 'POST' && p[1] === 'internal' && p[2] === 'enforcement' && p[3] === 'check' && p.length === 4) {
      const raw = await rawBody();
      return json(200, await lobby.enforcementCheck(req, raw));
    }
    if (m === 'POST' && p[1] === 'relay' && p[2] === 'logs' && p.length === 4) {
      return json(200, await lobby.uploadLog(p[3], await req.bytes(), String(req.headers['x-openbfme-signature'] ?? '')));
    }
    if (m === 'POST' && p[1] === 'relay' && p[2] === 'status' && p.length === 3) {
      return json(200, await lobby.relayStatusPost(await req.bytes(), String(req.headers['x-openbfme-signature'] ?? '')));
    }
    if (p[1] === 'admin' && p[2] === 'mod') {
      const raw = await rawBody();
      await lobby.adminCheck(req, raw, ['mod']);
      if (m === 'GET' && p[3] === 'reports' && p.length === 4) {
        const limitText = req.query.get('limit');
        return json(200, await lobby.adminReports(req.query.get('after'), limitText === null ? null : Number(limitText)));
      }
      if (m === 'GET' && p[3] === 'reports' && p.length === 5) return json(200, await lobby.adminReport(p[4]));
      if (m === 'GET' && p[3] === 'players') await lobby.cleanupSightingsIfDue();
      if (m === 'GET' && p[3] === 'games' && p.length === 4) return json(200, lobby.adminGames(req.query.get('state')));
      if (m === 'GET' && p[3] === 'players' && p.length === 4) {
        const limitText = req.query.get('limit');
        return json(200, lobby.adminPlayers(req.query.get('q') ?? '', req.query.get('after'), limitText === null ? null : Number(limitText)));
      }
      if (m === 'GET' && p[3] === 'players' && p.length === 5) return json(200, lobby.adminPlayer(p[4]));
      if (m === 'GET' && p[3] === 'enforcement' && p.length === 4) return json(200, lobby.adminEnforcement(req.query.get('after')));
      if ((m === 'POST' && p[3] === 'games' && p.length === 6 && p[5] === 'kick') ||
          (m === 'POST' && p[3] === 'games' && p.length === 6 && p[5] === 'close')) {
        const input = parseBody(raw);
        if (input?.v !== VERSION) throw bad('bad_version', `v must be ${VERSION}`);
        if (!/^[0-9a-f]{16}$/.test(p[4])) throw bad('bad_field', 'game id must be 16 lowercase hex digits');
        if (p[5] === 'kick') {
          exactFields(input, ['v', 'command_id', 'expected_seq', 'slot', 'expected_account', 'expected_install', 'reason'], 'kick');
          if (!validCommandId(input.command_id)) throw bad('bad_field', 'command_id must be 8 to 64 URL-safe characters');
          if (!Number.isSafeInteger(input.expected_seq) || input.expected_seq < 0) throw bad('bad_field', 'expected_seq must be a non-negative safe integer');
          intIn(input.slot, 0, MAX_SLOTS - 1, 'slot');
          str(input.expected_account, 128, 'expected_account');
          if (input.expected_install !== null && !validInstall(input.expected_install)) {
            throw bad('bad_field', 'expected_install must be null or 32 lowercase hex digits');
          }
          modReason(input.reason);
          const response = await lobby.moderateGame(req, raw, 'kick', p[4], input);
          return json(response.status, response.body);
        }
        exactFields(input, ['v', 'command_id', 'expected_seq', 'reason'], 'close');
        if (!validCommandId(input.command_id)) throw bad('bad_field', 'command_id must be 8 to 64 URL-safe characters');
        if (!Number.isSafeInteger(input.expected_seq) || input.expected_seq < 0) throw bad('bad_field', 'expected_seq must be a non-negative safe integer');
        modReason(input.reason);
        const response = await lobby.moderateGame(req, raw, 'close', p[4], input);
        return json(response.status, response.body);
      }
      if (m === 'PUT' && p[3] === 'enforcement' && p.length === 5) return json(200, await lobby.setEnforcement(p[4], parseBody(raw)));
      return json(404, { v: VERSION, error: 'not_found', message: 'unknown admin path' });
    }
    if (p[1] === 'admin' && p[2] === 'relay-status' && p.length === 3) {
      await lobby.adminCheck(req, await req.bytes(), ['admin', 'mod']);
      if (m !== 'GET') return json(404, { v: VERSION, error: 'not_found', message: 'unknown admin path' });
      return json(200, lobby.relayStatusList());
    }
    if (p[1] === 'admin' && p[2] === 'blobs') {
      const raw = await req.bytes();
      await lobby.adminCheck(req, raw);
      const key = p.slice(3).join('/');
      if (m === 'GET' && p.length === 3) return json(200, await lobby.adminList(req.query.get('after'), req.query.get('limit')));
      if (m === 'GET' && p.length === 5) return { status: 200, headers: { 'content-type': 'application/octet-stream', 'cache-control': 'no-store' }, body: await lobby.adminGet(key) };
      if (m === 'POST' && p.length === 5) return json(200, await lobby.adminPut(key, raw));
      return json(404, { v: VERSION, error: 'not_found', message: 'unknown admin path' });
    }
    if (m === 'GET' && p[1] === 'replays' && p.length === 4 && (p[3] === 'start' || p[3] === 'log')) {
      const b = await lobby.replayFile(await lobby.account(req.headers.authorization), p[2], p[3]);
      return { status: 200, headers: { 'content-type': p[3] === 'log' ? 'application/octet-stream' : 'application/json' }, body: b };
    }
    if (m === 'GET' && p[1] === 'mine' && p.length === 2) return json(200, lobby.mine(await lobby.account(req.headers.authorization))); // ONLINE-3
    if (m === 'POST' && p[1] === 'profile-archives' && p.length === 2) { // lane PROFILE-2
      return json(200, await lobby.putArchives(await lobby.account(req.headers.authorization), await body(), req.ip));
    }
    if (m === 'POST' && p[1] === 'rooms' && p.length === 4 && p[3] === 'join') { // ONLINEHUB-1: join a private room by its code
      const slotsHeader = req.headers['x-openbfme-max-slots'];
      const account = withMaxSlots(await lobby.account(req.headers.authorization), slotsHeader === undefined ? undefined : maxSlotsOf(req.headers));
      return json(200, await lobby.joinByCode(account, p[2], await body()));
    }
    if (p[1] !== 'games') return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
    const maxSlots = maxSlotsOf(req.headers); // ONLINE16-FIX
    if (m === 'GET' && p.length === 2) {
      const account = hasAuthorization ? await lobby.account(req.headers.authorization, { pollCache: true }) : null;
      return json(200, lobby.list(req.query.get('running') === '1', account, maxSlots));
    }
    // ONLINE16-FIX: the client's width rides with its account for this request (absent header: undefined, an older client)
    const account = withMaxSlots(await lobby.account(req.headers.authorization, { leaving: m === 'POST' && p.length === 4 && p[3] === 'leave', pollCache: m === 'GET' }),
      req.headers['x-openbfme-max-slots'] === undefined ? undefined : maxSlots);
    if (m === 'POST' && p.length === 2) return json(200, await lobby.create(account, await body()));
    const id = p[2];
    if (!/^[0-9a-f]{16}$/.test(id ?? '')) return json(404, { v: VERSION, error: 'no_game', message: 'a game id is 16 hex digits' });
    if (m === 'GET' && p.length === 3) {
      const since = req.query.get('since');
      const answer = async (backoff, reauthorize = false) => {
        try {
          const current = reauthorize
            ? await lobby.account(req.headers.authorization, { activity: false, pollCache: true })
            : account;
          const out = lobby.poll(current, id, since === null ? 0 : Number(since));
          return json(200, backoff ? { ...out, backoff } : out);
        } catch (e) {
          if (e instanceof LobbyError) return json(e.status, failure(e));
          return json(500, { v: VERSION, error: 'internal', message: 'internal error' });
        }
      };
      // ONLINE-5: a held poll: the platform sends `deferred`'s answer when it resolves, and lets other requests through meanwhile
      const hold = lobby.pollHold(account, id, req.query.get('seq'), req.query.get('wait'));
      if (hold?.held) return { deferred: hold.held.then(() => answer(undefined, true)) };
      return answer(hold?.backoff); // Sol r1: not held for a bound: the client is told how long to wait

    }
    const actions = { join: 'join', leave: 'leave', slot: 'setSlot', setup: 'setSetup', ready: 'ready', chat: 'chat', kick: 'kick', start: 'start', rejoin: 'rejoin', spectate: 'spectate', link: 'link' };
    if (m === 'POST' && p.length === 4 && actions[p[3]]) {
      const b = p[3] === 'leave' || p[3] === 'start' || p[3] === 'rejoin' || p[3] === 'spectate' ? {} : await body();
      return json(200, await lobby[actions[p[3]]](account, id, b));
    }
    return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
  } catch (e) {
    if (reportRequest) {
      const known = e instanceof ReportError || e instanceof LobbyError;
      const status = known ? e.status : 500;
      const code = known ? e.code : 'internal';
      const message = known ? e.message : 'internal error';
      const requestId = bytesToHex(lobby.env.randomBytes(12));
      const response = json(status, {
        v: VERSION,
        error: { code, message, request_id: requestId },
        ...(known ? e.details : {}),
      });
      if (known && Number.isSafeInteger(e.details?.retry_after)) {
        response.headers['retry-after'] = String(e.details.retry_after);
      }
      return response;
    }
    if (e instanceof LobbyError || e instanceof SocialError) return json(e.status, { v: VERSION, ...failure(e) });
    if (e instanceof SocialStorageError) {
      const full = e.kind === 'full';
      const normalized = { status: full ? 507 : 503, code: full ? 'social_store_full' : 'social_unavailable',
        message: full ? 'social storage is full' : 'social storage is unavailable' };
      return json(normalized.status, failure(normalized));
    }
    return json(500, { v: VERSION, error: 'internal', message: 'internal error' });
  }
}

// ---- configuration from environment-style strings (both platforms) -----------------------------------------------------------------------------------------
// TICKET_KEYS: the key ring text; SESSION_KEY / UPLOAD_KEY / ADMIN_KEY / MOD_KEY: hex; RELAYS: "id=a.b.c.d:port,..."; PUBLIC_URL; REQUIRE_INSTALL: 0 or 1;
// RUN_AHEAD; CRC_INTERVAL; MOTD (ONLINE-4: the message of the day, up to 2000 bytes, lines separated by \n)
function motdFromEnv(text) {
  if (text === undefined || text === null || text === '') return null;
  const m = String(text).replace(/\\n/g, '\n');
  if (enc.encode(m).length > 2000) throw new Error('MOTD: over 2000 bytes');
  if (/[\u0000-\u0009\u000b-\u001f\u007f]/.test(m)) throw new Error('MOTD: control characters other than line breaks');
  return m;
}

// ONLINEMENU-1 (owner 2026-10-20): the e-mail sign-in's configuration: the Turnstile widget (site key, secret), the mail function (URL, key) and the
// privacy page. None of them: e-mail sign-in is off (503 email_unavailable). A partial or malformed set does not stop the lobby (Discord and play go
// on while the secrets are being added one by one): e-mail sign-in answers 503 with the reason, and /v1/health names it (emailConfig)
export function emailConfigFromEnv(e) {
  const names = ['TURNSTILE_SITE_KEY', 'TURNSTILE_SECRET', 'MAILER_URL', 'MAILER_KEY'];
  const given = names.filter((name) => e[name] !== undefined && e[name] !== '');
  if (!given.length) return null;
  const missing = names.filter((name) => !given.includes(name));
  const publicUrl = String(e.PUBLIC_URL ?? '').replace(/\/$/, '');
  const problem = missing.length ? `missing ${missing.join(', ')}`
    : !/^https?:\/\/[a-z0-9.-]+(:\d+)?$/.test(publicUrl) ? 'PUBLIC_URL is needed (the links point there)'
      : !/^https:\/\//.test(String(e.MAILER_URL)) ? 'MAILER_URL must be an https URL'
        : !/^[0-9a-f]{64,}$/.test(String(e.MAILER_KEY)) ? 'MAILER_KEY must be 32+ bytes of hex' : null;
  if (problem) return { error: `e-mail sign-in is not configured: ${problem}` };
  return {
    turnstileSiteKey: String(e.TURNSTILE_SITE_KEY),
    turnstileSecret: String(e.TURNSTILE_SECRET),
    mailerUrl: String(e.MAILER_URL),
    mailerKey: String(e.MAILER_KEY),
    hostname: new URL(publicUrl).hostname,
    privacyUrl: String(e.PRIVACY_URL ?? 'https://openbfme.com/privacy'),
  };
}

export function configFromEnv(e) {
  const relays = String(e.RELAYS ?? '').split(',').map((x) => x.trim()).filter(Boolean).map((x) => {
    const [id, address] = x.includes('=') ? x.split('=') : ['default', x];
    return { id, address };
  });
  const requireInstallText = String(e.REQUIRE_INSTALL ?? '0');
  if (requireInstallText !== '0' && requireInstallText !== '1') throw new Error('REQUIRE_INSTALL must be 0 or 1');
  const adminKey = e.ADMIN_KEY ? hexToBytes(String(e.ADMIN_KEY)) : null;
  const modKey = e.MOD_KEY ? hexToBytes(String(e.MOD_KEY)) : null;
  const enforcementCheckKey = e.ENFORCEMENT_CHECK_KEY ? hexToBytes(String(e.ENFORCEMENT_CHECK_KEY)) : null;
  if (enforcementCheckKey && enforcementCheckKey.length !== 32) throw new Error('ENFORCEMENT_CHECK_KEY must be 32 bytes of hex');
  if (adminKey && modKey && bytesToHex(adminKey) === bytesToHex(modKey)) throw new Error('ADMIN_KEY and MOD_KEY must be different');
  if (enforcementCheckKey && ((adminKey && bytesToHex(enforcementCheckKey) === bytesToHex(adminKey)) ||
      (modKey && bytesToHex(enforcementCheckKey) === bytesToHex(modKey)))) {
    throw new Error('ENFORCEMENT_CHECK_KEY must be different from ADMIN_KEY and MOD_KEY');
  }
  return {
    ticketKeys: parseKeyRing(String(e.TICKET_KEYS ?? '')),
    sessionKey: hexToBytes(String(e.SESSION_KEY ?? '')),
    uploadKey: e.UPLOAD_KEY ? hexToBytes(String(e.UPLOAD_KEY)) : null,
    adminKey,
    modKey,
    enforcementCheckKey,
    requireInstall: requireInstallText === '1',
    relays,
    publicUrl: String(e.PUBLIC_URL ?? '').replace(/\/$/, ''),
    oauth: oauthConfigFromEnv(e),
    email: emailConfigFromEnv(e),
    motd: motdFromEnv(e.MOTD),
    ...(e.RUN_AHEAD ? { runAhead: Number(e.RUN_AHEAD) } : {}),
    ...(e.CRC_INTERVAL ? { crcInterval: Number(e.CRC_INTERVAL) } : {}),
  };
}
