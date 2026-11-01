import { repairStoredText } from './stored-text.mjs';

const VERSION = 1;
export const REPORT_TTL_S = 90 * 24 * 60 * 60;
export const REPORT_WINDOW_S = 24 * 60 * 60;
const MAX_ATTEMPTS_HOUR = 5;
const MAX_ATTEMPTS_DAY = 20;
const MAX_GLOBAL_REPORTS_DAY = 1000;
const MAX_REPORT_PARTICIPANTS = 256;
const MAX_CLEANUP_KEYS = 100;
const REPORT_ID = /^R-[0-9a-f]{24}$/;
const GAME_ID = /^[0-9a-f]{16}$/;
const INSTALL_ID = /^[0-9a-f]{32}$/;
const IDEMPOTENCY_KEY = /^[A-Za-z0-9_-]{8,64}$/;
const REPORT_CATEGORIES = new Set(['cheating', 'harassment', 'griefing', 'other']);
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

function validReportReason(reason) {
  if (typeof reason !== 'string' || /[\u0000-\u001f\u007f]/u.test(reason) || !isWellFormedUnicode(reason)) return false;
  const separator = reason.indexOf(':');
  const category = separator < 0 ? reason : reason.slice(0, separator);
  return REPORT_CATEGORIES.has(category) &&
    (separator < 0 || (reason.startsWith(`${category}: `) && !!reason.slice(separator + 2).trim()));
}

function isWellFormedUnicode(value) {
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code >= 0xd800 && code <= 0xdbff) {
      const next = value.charCodeAt(++i);
      if (!(next >= 0xdc00 && next <= 0xdfff)) return false;
    } else if (code >= 0xdc00 && code <= 0xdfff) return false;
  }
  return true;
}

export class ReportError extends Error {
  constructor(status, code, message, details = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

function fail(status, code, message, details = {}) {
  return new ReportError(status, code, message, details);
}

function exactFields(value, fields, name) {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw fail(400, 'bad_field', `${name} must be an object`);
  const unknown = Object.keys(value).find((key) => !fields.includes(key));
  if (unknown) throw fail(400, 'unknown_field', `${name} has unknown field ${unknown}`);
  const missing = fields.find((key) => !Object.hasOwn(value, key));
  if (missing) throw fail(400, 'missing_field', `${name} is missing required field ${missing}`);
}

function codePoints(value) {
  return Array.from(value).length;
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function validateParticipant(value) {
  exactFields(value, ['account', 'install', 'nick'], 'stored report participant');
  if (typeof value.account !== 'string' || !value.account.length || codePoints(value.account) > 128) throw new Error('stored report participant account is invalid');
  if (value.install !== null && (typeof value.install !== 'string' || !INSTALL_ID.test(value.install))) throw new Error('stored report participant install is invalid');
  if (typeof value.nick !== 'string' || !value.nick.length || codePoints(value.nick) > 24 || !isWellFormedUnicode(value.nick)) throw new Error('stored report participant nick is invalid');
  return { account: value.account, install: value.install, nick: value.nick };
}

export function reportRosterFromGame(game, endedAt = game.reportEndedAt ?? null) {
  const participants = game.reportParticipants ?? game.slots
    .filter((slot) => slot.state === 'player' && typeof slot.account === 'string')
    .map((slot) => ({ account: slot.account, install: slot.install ?? null, nick: slot.name }));
  return normalizeRoster({
    game_id: game.id,
    participants,
    active: game.slots.filter((slot) => slot.state === 'player' && typeof slot.account === 'string').map((slot) => slot.account),
    state: game.finished ? 'finished' : (game.state === 'starting' ? 'started' : game.state),
    ended_at: endedAt,
  }, game.id);
}

export function normalizeRoster(value, expectedGameId = null) {
  value = repairStoredText(value);
  exactFields(value, ['game_id', 'participants', 'active', 'state', 'ended_at'], 'stored report roster');
  if (typeof value.game_id !== 'string' || !GAME_ID.test(value.game_id) || (expectedGameId && value.game_id !== expectedGameId)) {
    throw new Error('stored report roster game id is invalid');
  }
  if (!Array.isArray(value.participants) || value.participants.length > MAX_REPORT_PARTICIPANTS) throw new Error('stored report roster participants are invalid');
  const participants = value.participants.map(validateParticipant);
  const accountIds = new Set(participants.map((participant) => participant.account));
  if (accountIds.size !== participants.length) throw new Error('stored report roster participants are not unique');
  if (!Array.isArray(value.active) || value.active.length > 8 || value.active.some((account) => typeof account !== 'string' || !accountIds.has(account))) {
    throw new Error('stored report roster active participants are invalid');
  }
  if (new Set(value.active).size !== value.active.length) throw new Error('stored report roster active participants are not unique');
  if (!['open', 'started', 'closed', 'finished'].includes(value.state)) throw new Error('stored report roster state is invalid');
  if (value.ended_at !== null && (!Number.isSafeInteger(value.ended_at) || value.ended_at < 0)) throw new Error('stored report roster end time is invalid');
  return {
    game_id: value.game_id,
    participants,
    active: [...value.active],
    state: value.state,
    ended_at: value.ended_at,
  };
}

export function mergeReportRosters(previous, incoming) {
  const next = normalizeRoster(incoming, previous?.game_id ?? null);
  if (!previous) return next;
  const old = normalizeRoster(previous, next.game_id);
  const participants = [...old.participants];
  const byAccount = new Map(participants.map((participant) => [participant.account, participant]));
  for (const participant of next.participants) {
    const existing = byAccount.get(participant.account);
    if (existing) {
      if (existing.install !== participant.install) throw new Error('report roster account identity changed');
    } else {
      if (participants.length >= MAX_REPORT_PARTICIPANTS) throw fail(409, 'roster_full', 'this game has reached its participant history limit');
      participants.push(participant);
      byAccount.set(participant.account, participant);
    }
  }
  return {
    game_id: next.game_id,
    participants,
    active: old.ended_at === null ? next.active : old.active,
    state: old.ended_at === null ? next.state : old.state,
    ended_at: old.ended_at ?? next.ended_at,
  };
}

// LOBBYSEC-1 (as the live lobby line): intake = new requests must be well-formed Unicode; stored receipts written before that rule still replay
function validateReportBody(raw, { intake = true, legacyReceipt = false } = {}) {
  let value;
  try {
    value = JSON.parse(decoder.decode(raw));
  } catch {
    throw fail(400, 'bad_json', 'the body must be JSON');
  }
  exactFields(value, ['v', 'game_id', 'target', 'reason'], 'report');
  if (value.v !== VERSION) throw fail(400, 'bad_version', `v must be ${VERSION}`);
  if (typeof value.game_id !== 'string' || !GAME_ID.test(value.game_id)) throw fail(400, 'bad_field', 'game_id must be 16 lowercase hexadecimal digits');
  if (typeof value.target !== 'string' || codePoints(value.target) < 1 || codePoints(value.target) > 128 || (intake && !value.target.isWellFormed())) {
    throw fail(400, 'bad_field', 'target must be a 1 .. 128 character player id');
  }
  if (typeof value.reason !== 'string' || codePoints(value.reason) < 1 || codePoints(value.reason) > 1000 || (intake && !value.reason.isWellFormed())) {
    throw fail(400, 'bad_field', 'reason must be 1 .. 1000 Unicode characters');
  }
  if (!legacyReceipt && !validReportReason(value.reason)) {
    throw fail(400, 'bad_reason', 'reason must use cheating, harassment, griefing, or other, optionally followed by a note');
  }
  const body = { v: VERSION, game_id: value.game_id, target: value.target, reason: value.reason };
  return { ...body, canonical: JSON.stringify(body) };
}

export function validReceipt(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== 'canonical,expires_at,received_at,report_id' ||
      typeof value.canonical !== 'string' || typeof value.report_id !== 'string' || !REPORT_ID.test(value.report_id) ||
      !Number.isSafeInteger(value.received_at) || value.received_at < 0 ||
      !Number.isSafeInteger(value.expires_at) || value.expires_at !== value.received_at + REPORT_WINDOW_S) {
    throw new Error('stored report receipt is malformed');
  }
  const body = validateReportBody(new TextEncoder().encode(value.canonical), { intake: false, legacyReceipt: true });
  if (body.canonical !== value.canonical) throw new Error('stored report receipt is malformed');
  return value;
}

export function validateReportSource(value, expectedId = null) {
  value = repairStoredText(value);
  exactFields(value, ['id', 'source', 'reporter', 'target', 'game_id', 'reason', 'received_at', 'expires_at'], 'stored report');
  if (typeof value.id !== 'string' || !REPORT_ID.test(value.id) || (expectedId && value.id !== expectedId) || value.source !== 'player') {
    throw new Error('stored report identity is invalid');
  }
  for (const who of ['reporter', 'target']) {
    exactFields(value[who], ['install', 'nick'], `stored report ${who}`);
    if (typeof value[who].install !== 'string' || !INSTALL_ID.test(value[who].install) ||
        typeof value[who].nick !== 'string' || codePoints(value[who].nick) > 24 || !isWellFormedUnicode(value[who].nick)) {
      throw new Error(`stored report ${who} is invalid`);
    }
  }
  if (typeof value.game_id !== 'string' || !GAME_ID.test(value.game_id) ||
      typeof value.reason !== 'string' || codePoints(value.reason) < 1 || codePoints(value.reason) > 1000 ||
      !Number.isSafeInteger(value.received_at) || value.received_at < 0 ||
      !Number.isSafeInteger(value.expires_at) || value.expires_at <= value.received_at ||
      value.expires_at - value.received_at !== REPORT_TTL_S) {
    throw new Error('stored report fields are invalid');
  }
  return value;
}

function validateAttempts(value) {
  if (!Array.isArray(value) || value.length > MAX_ATTEMPTS_DAY || value.some((at) => !Number.isSafeInteger(at) || at < 0)) {
    throw new Error('stored report attempts are malformed');
  }
  return value;
}

function attemptsStatus(attempts, now) {
  const recent = validateAttempts(attempts).filter((at) => at > now - 24 * 60 * 60 && at <= now);
  const hour = recent.filter((at) => at > now - 60 * 60);
  if (hour.length >= MAX_ATTEMPTS_HOUR) {
    return { allowed: false, retry: Math.max(1, Math.min(...hour) + 60 * 60 - now) };
  }
  if (recent.length >= MAX_ATTEMPTS_DAY) {
    return { allowed: false, retry: Math.max(1, Math.min(...recent) + 24 * 60 * 60 - now) };
  }
  return { allowed: true, recent: [...recent, now] };
}

function makeReportId(randomBytes) {
  const bytes = randomBytes(12);
  if (!(bytes instanceof Uint8Array) || bytes.length !== 12) throw new Error('report id generator must return 12 bytes');
  return `R-${Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('')}`;
}

export async function persistReportRoster(store, game, endedAt = game.reportEndedAt ?? null) {
  if (typeof store?.reportTransaction !== 'function') return null;
  const incoming = reportRosterFromGame(game, endedAt);
  return store.reportTransaction(async (tx) => {
    const key = `roster:${game.id}`;
    const previous = await tx.get(key);
    const next = mergeReportRosters(previous, incoming);
    if (JSON.stringify(previous ?? null) !== JSON.stringify(next)) await tx.put(key, next);
    return next;
  });
}

export async function endReportRoster(store, gameId, endedAt, game = null) {
  if (typeof store?.reportTransaction !== 'function') return null;
  return store.reportTransaction(async (tx) => {
    const key = `roster:${gameId}`;
    const previous = await tx.get(key);
    if (!previous && !game) return null;
    const initial = previous ? normalizeRoster(previous, gameId) : reportRosterFromGame(game, null);
    const incoming = game ? reportRosterFromGame(game, initial.ended_at ?? endedAt) : initial;
    const merged = mergeReportRosters(initial, incoming);
    const next = { ...merged, ended_at: initial.ended_at ?? endedAt, state: initial.ended_at === null ? (game?.finished ? 'finished' : 'closed') : initial.state };
    if (JSON.stringify(previous ?? null) !== JSON.stringify(next)) await tx.put(key, next);
    return next;
  });
}

function requestFailure(error) {
  return error instanceof ReportError ? error : fail(400, 'bad_field', 'the report request is invalid');
}

export async function submitPlayerReport({ store, now, randomBytes, account, rawBody, idempotencyKey, getLiveGame }) {
  if (!account?.install || !INSTALL_ID.test(account.install)) throw fail(401, 'install_required', 'sign in with an install identity before reporting');
  if (typeof store?.reportTransaction !== 'function') throw fail(501, 'no_report_store', 'report storage is not configured');
  let body = null;
  let bodyError = null;
  try {
    body = validateReportBody(rawBody, { legacyReceipt: true });
    if (!validReportReason(body.reason)) throw fail(400, 'bad_reason', 'reason must use cheating, harassment, griefing, or other, optionally followed by a note');
  } catch (error) {
    bodyError = requestFailure(error);
  }
  const keyValid = typeof idempotencyKey === 'string' && IDEMPOTENCY_KEY.test(idempotencyKey);
  const keyError = keyValid ? null : fail(400, idempotencyKey === undefined || idempotencyKey === null || idempotencyKey === ''
    ? 'missing_field' : 'bad_field', 'Idempotency-Key must be 8 .. 64 letters, digits, underscore or hyphen');
  const receivedAt = Math.floor(now() / 1000);
  const live = body ? getLiveGame(body.game_id) : null;
  const key = keyValid ? `receipt:${account.install}:${idempotencyKey}` : null;

  for (let collisionRetry = 0; collisionRetry < 4; collisionRetry++) {
    const candidateIds = body ? Array.from({ length: 4 }, () => makeReportId(randomBytes)) : [];
    const outcome = await store.reportTransaction(async (tx) => {
      if (body && keyValid) {
        const storedReceipt = await tx.get(key);
        if (storedReceipt) {
          const receipt = validReceipt(storedReceipt);
          if (receipt.expires_at > receivedAt) {
            if (receipt.canonical === body.canonical) return { receipt: { v: VERSION, report_id: receipt.report_id, received_at: receipt.received_at }, replay: true };
            var payloadConflict = true;
          }
        }
      }

      const attemptsKey = `attempts:${account.install}`;
      const status = attemptsStatus(await tx.get(attemptsKey) ?? [], receivedAt);
      if (!status.allowed) {
        return { error: fail(429, 'rate_limited', 'the report attempt limit has been reached', { retry_after: status.retry }) };
      }
      const charge = async (error) => {
        await tx.put(attemptsKey, status.recent);
        return { error };
      };
      if (keyError) return charge(keyError);
      if (bodyError) return charge(bodyError);
      if (payloadConflict) return charge(fail(409, 'conflict', 'Idempotency-Key was already used for a different report'));

      const rosterKey = `roster:${body.game_id}`;
      const persistedRoster = await tx.get(rosterKey);
      let roster = persistedRoster ? normalizeRoster(persistedRoster, body.game_id) : null;
      if (live) {
        const reconciled = mergeReportRosters(roster, reportRosterFromGame(live));
        if (JSON.stringify(roster) !== JSON.stringify(reconciled)) await tx.put(rosterKey, reconciled);
        roster = reconciled;
      }
      if (!roster) return charge(fail(404, 'no_game', 'the game is not available for reporting'));
      const active = live && live.reportEndedAt == null && !live.finished && ['open', 'starting', 'started'].includes(live.state);
      if (!active && roster.ended_at === null) return charge(fail(404, 'no_game', 'the game is not available for reporting'));
      if (roster.ended_at !== null && receivedAt > roster.ended_at + REPORT_WINDOW_S) {
        return charge(fail(404, 'report_expired', 'the report window for this game has ended'));
      }
      if (!roster.participants.some((participant) => participant.install === account.install)) {
        return charge(fail(403, 'not_in_game', 'the reporting install did not participate in this game'));
      }
      const target = roster.participants.find((participant) => participant.account === body.target);
      if (!target || !target.install) return charge(fail(404, 'no_player', 'the target is not a reportable participant in this game'));
      if (target.install === account.install) return charge(fail(403, 'self_report', 'players cannot report themselves'));
      const reporter = roster.participants.find((participant) => participant.install === account.install);

      const duplicateKey = `duplicate:${account.install}:${body.game_id}:${target.install}`;
      const duplicate = await tx.get(duplicateKey);
      if (duplicate && Number.isSafeInteger(duplicate.expires_at) && duplicate.expires_at > receivedAt) {
        return charge(fail(409, 'already_reported', 'this player was already reported in this game'));
      }

      const day = new Date(receivedAt * 1000).toISOString().slice(0, 10);
      const dayCounter = await tx.get('globalUTCday');
      const global = dayCounter?.day === day ? dayCounter.count : 0;
      if (!Number.isSafeInteger(global) || global < 0) throw new Error('stored global report counter is malformed');
      if (global >= MAX_GLOBAL_REPORTS_DAY) {
        const nextDay = Math.floor((Date.parse(`${day}T00:00:00Z`) + 24 * 60 * 60 * 1000) / 1000);
        return charge(fail(429, 'daily_limit', 'the lobby report limit has been reached for today', { retry_after: nextDay - receivedAt }));
      }

      let reportId = null;
      for (const candidate of candidateIds) {
        if (!(await tx.get(`report:${candidate}`))) {
          reportId = candidate;
          break;
        }
      }
      if (!reportId) return { retryId: true };

      const expiresAt = receivedAt + REPORT_TTL_S;
      const receiptExpiresAt = receivedAt + REPORT_WINDOW_S;
      const source = {
        id: reportId,
        source: 'player',
        reporter: { install: account.install, nick: reporter.nick },
        target: { install: target.install, nick: target.nick },
        game_id: body.game_id,
        reason: body.reason,
        received_at: receivedAt,
        expires_at: expiresAt,
      };
      const participants = new Map([[reporter.account, reporter], [target.account, target]]);
      const evidence = (live?.chat ?? []).filter((line) => participants.has(line.account)).slice(-50).map((line) => {
        const participant = participants.get(line.account);
        return {
          game_id: body.game_id,
          nick: participant.nick,
          text: line.text,
          at: Number.isSafeInteger(line.at) && line.at >= 0 ? Math.floor(line.at / 1000) : null,
        };
      });
      const receipt = { canonical: body.canonical, report_id: reportId, received_at: receivedAt, expires_at: receiptExpiresAt };
      const indexKey = `index:${String(receivedAt).padStart(16, '0')}:${reportId}`;
      await tx.put(attemptsKey, status.recent);
      await tx.put(`report:${reportId}`, source);
      if (evidence.length) await tx.put(`evidence:${reportId}`, { expires_at: expiresAt, lines: evidence });
      await tx.put(indexKey, { id: reportId, expires_at: expiresAt });
      await tx.put(key, receipt);
      await tx.put(duplicateKey, { expires_at: receiptExpiresAt });
      await tx.put('globalUTCday', { day, count: global + 1 });
      return { receipt: { v: VERSION, report_id: reportId, received_at: receivedAt }, replay: false };
    });
    if (outcome.retryId) continue;
    if (outcome.error) throw outcome.error;
    return { receipt: outcome.receipt, replay: outcome.replay };
  }
  throw new Error('could not allocate a unique report id');
}

function encodeBase64Url(text) {
  const bytes = encoder.encode(text);
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function decodeBase64Url(text) {
  if (!/^[A-Za-z0-9_-]+$/.test(text) || text.length % 4 === 1) throw new Error('invalid cursor encoding');
  const base64 = text.replace(/-/g, '+').replace(/_/g, '/') + '='.repeat((4 - text.length % 4) % 4);
  const binary = atob(base64);
  const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
  return decoder.decode(bytes);
}

function hex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

function equalMac(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

const REPORT_SORT = 'reports:received_at_desc:id_desc';
const INDEX_KEY = /^index:(\d{16}):(R-[0-9a-f]{24})$/;

async function makeCursor(key, hmac, modKey) {
  const payload = encodeBase64Url(JSON.stringify({ v: VERSION, sort: REPORT_SORT, key }));
  const mac = hex(await hmac(modKey, encoder.encode(`lobby-report-cursor\n${payload}`)));
  return `${payload}.${mac}`;
}

async function parseCursor(cursor, hmac, modKey) {
  if (typeof cursor !== 'string' || cursor.length > 512) throw fail(400, 'bad_field', 'after is not a valid report cursor');
  const match = /^([A-Za-z0-9_-]+)\.([0-9a-f]{64})$/.exec(cursor);
  if (!match) throw fail(400, 'bad_field', 'after is not a valid report cursor');
  const wanted = hex(await hmac(modKey, encoder.encode(`lobby-report-cursor\n${match[1]}`)));
  if (!equalMac(wanted, match[2])) throw fail(400, 'bad_field', 'after is not a valid report cursor');
  let payload;
  try {
    payload = JSON.parse(decodeBase64Url(match[1]));
  } catch {
    throw fail(400, 'bad_field', 'after is not a valid report cursor');
  }
  try {
    exactFields(payload, ['v', 'sort', 'key'], 'report cursor');
  } catch {
    throw fail(400, 'bad_field', 'after is not a valid report cursor');
  }
  if (payload.v !== VERSION || payload.sort !== REPORT_SORT || typeof payload.key !== 'string' || !INDEX_KEY.test(payload.key)) {
    throw fail(400, 'bad_field', 'after is not a valid report cursor');
  }
  return payload.key;
}

export async function readPlayerReports({ store, now, hmac, modKey, after = null, limit = null }) {
  if (typeof store?.reportTransaction !== 'function') throw fail(501, 'no_report_store', 'report storage is not configured');
  const pageSize = limit === null ? 50 : Number(limit);
  if (!Number.isInteger(pageSize) || pageSize < 1 || pageSize > 100) throw fail(400, 'bad_field', 'limit must be an integer in 1 .. 100');
  const afterKey = after === null || after === '' ? null : await parseCursor(after, hmac, modKey);
  const nowSeconds = Math.floor(now() / 1000);
  return store.reportTransaction(async (tx) => {
    const rows = await tx.list('index:', { after: afterKey, limit: pageSize, reverse: true });
    const items = [];
    for (const [key, value] of rows) {
      const match = INDEX_KEY.exec(key);
      if (!match || !value || typeof value !== 'object' || value.id !== match[2] || !Number.isSafeInteger(value.expires_at)) {
        console.warn('lobby: skipping malformed stored report index');
        continue;
      }
      if (value.expires_at <= nowSeconds) continue;
      const report = await tx.get(`report:${value.id}`);
      if (!report) continue;
      let source;
      try {
        source = validateReportSource(report, value.id);
      } catch {
        console.warn(`lobby: skipping malformed stored report ${value.id}`);
        continue;
      }
      if (source.expires_at > nowSeconds) items.push(source);
    }
    const lastKey = rows.size === pageSize ? [...rows.keys()].at(-1) : null;
    return { v: VERSION, items, next_cursor: lastKey ? await makeCursor(lastKey, hmac, modKey) : null };
  });
}

export async function readPlayerReport({ store, now, reportId }) {
  if (typeof store?.reportTransaction !== 'function') throw fail(501, 'no_report_store', 'report storage is not configured');
  if (typeof reportId !== 'string' || !REPORT_ID.test(reportId)) throw fail(404, 'no_report', 'no such report');
  const nowSeconds = Math.floor(now() / 1000);
  return store.reportTransaction(async (tx) => {
    const value = await tx.get(`report:${reportId}`);
    if (!value) throw fail(404, 'no_report', 'no such report');
    let source;
    try {
      source = validateReportSource(value, reportId);
    } catch {
      console.warn(`lobby: skipping malformed stored report ${reportId}`);
      throw fail(404, 'no_report', 'no such report');
    }
    if (source.expires_at <= nowSeconds) throw fail(404, 'no_report', 'no such report');
    return { v: VERSION, report: source };
  });
}

export async function cleanupReportData(store, nowSeconds, cursorState = { index: 0, after: null }) {
  if (typeof store?.reportTransaction !== 'function') return cursorState;
  const prefixes = ['index:', 'receipt:', 'duplicate:', 'attempts:', 'roster:'];
  const index = cursorState.index % prefixes.length;
  const prefix = prefixes[index];
  return store.reportTransaction(async (tx) => {
    const rows = await tx.list(prefix, { after: cursorState.after, limit: MAX_CLEANUP_KEYS, reverse: false });
    let last = null;
    let visited = 0;
    let deleted = 0;
    for (const [key, value] of rows) {
      let expired = false;
      let reportId = null;
      if (prefix === 'roster:') {
        const match = /^roster:([0-9a-f]{16})$/.exec(key);
        if (!match) throw new Error('stored report roster key is malformed');
        const roster = normalizeRoster(value, match[1]);
        expired = roster.ended_at !== null && nowSeconds > roster.ended_at + REPORT_WINDOW_S;
      } else if (prefix === 'attempts:') {
        if (!/^attempts:[0-9a-f]{32}$/.test(key) || !Array.isArray(value)) throw new Error('stored report attempts are malformed');
        expired = value.length === 0 || Math.max(...value, 0) <= nowSeconds - 24 * 60 * 60;
      } else {
        const hasExpiry = value && typeof value === 'object' && Number.isSafeInteger(value.expires_at);
        if (!hasExpiry) throw new Error(`stored ${prefix} record is malformed`);
        expired = value.expires_at <= nowSeconds;
        if (prefix === 'index:') {
          const match = INDEX_KEY.exec(key);
          if (!match || value.id !== match[2]) throw new Error('stored report index is malformed');
          reportId = value.id;
        }
      }
      const deleteCount = expired ? (reportId ? 3 : 1) : 0;
      if (deleted + deleteCount > MAX_CLEANUP_KEYS) break;
      last = key;
      visited += 1;
      if (expired) {
        await tx.delete(key);
        if (reportId) {
          await tx.delete(`report:${reportId}`);
          await tx.delete(`evidence:${reportId}`);
        }
        deleted += deleteCount;
      }
    }
    const completed = rows.size < MAX_CLEANUP_KEYS && visited === rows.size;
    return completed ? { index: (index + 1) % prefixes.length, after: null } : { index, after: last ?? cursorState.after };
  });
}

export async function chargeReportAttempt(store, install, now) {
  const receivedAt = Math.floor(now() / 1000);
  return store.reportTransaction(async (tx) => {
    const key = `attempts:${install}`;
    const status = attemptsStatus(await tx.get(key) ?? [], receivedAt);
    if (!status.allowed) return status.retry;
    await tx.put(key, status.recent);
    return null;
  });
}
