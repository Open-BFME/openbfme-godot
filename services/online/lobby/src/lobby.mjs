// OpenBFME online lobby. GPL-3.0.
//
// The lobby's logic (lane ONLINE-2, docs/ONLINE.md phase O1), without a platform: the Cloudflare Worker + Durable Object (worker.mjs) and the self-hosted
// Node server (server.mjs) both wrap it. It holds the game list, each game's slots, ready flags and chat, builds the START (engine/src/GameNetwork/
// OnlineStart.h is its reader), signs the relay tickets (engine/src/GameNetwork/RelayProtocol.h: the same 32 bytes) and keeps the relay's command logs.
//
// Wire: JSON over HTTP, every body carries "v": 1. Sign-in gives a bearer token (a signed guest identity; ONLINE-2 r2 dropped Discord sign-in until its
// return link to the game is built: Sol r1). Clients
// poll GET /v1/games/<id>?since=<seq> about once a second (WebSocket push is the next step, docs/ONLINE.md).
//
// Platform hooks (`env`): hmac(keyBytes, dataBytes) -> Promise<Uint8Array> (HMAC-SHA256), randomBytes(n) -> Uint8Array, now() -> ms, store (games and
// blobs: saveGame / deleteGame / putBlob / getBlob), config (below). No secret is in this file: keys come from the environment / files.

export const VERSION = 1;
export const MAX_JSON_BYTES = 16384;          // a JSON request body (ONLINE-2 r2: counted as received)
export const MAX_LOG_BYTES = 64 * 1024 * 1024; // a relay log upload
const LOG_HEADER_BYTES = 25; // RelayCore.h "Log format": magic 8, version 4, game id 8, slot mask 1, start 4
const MAX_SLOTS = 8;
const SLOT_STATES = ['open', 'closed', 'player', 'easy', 'medium', 'hard', 'brutal'];
const AI_STATES = new Set(['easy', 'medium', 'hard', 'brutal']);
// INFRA-2: the admin backup's signature window, page size and the blob keys it moves (the three kinds the lobby stores per started game)
export const ADMIN_WINDOW_S = 300;
export const ADMIN_PAGE = 1000;
const BLOB_KEY = /^(?:logs\/[0-9a-f]{16}\.obfmerlog|(?:starts|players)\/[0-9a-f]{16}\.json)$/;
export function parseBlobKey(key) {
  return BLOB_KEY.test(key) ? { kind: key.split('/')[0], id: key.slice(key.indexOf('/') + 1, key.indexOf('/') + 17) } : null;
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
export async function signTicket(env, keyId, key, gameIdHex, slot, mask, expires) {
  const b = new Uint8Array(32);
  b[0] = 1;
  b[1] = keyId;
  const id = hexToBytes(gameIdHex); // big-endian text -> little-endian u64
  for (let i = 0; i < 8; i++) b[2 + i] = id[7 - i];
  b[10] = slot;
  b[11] = mask;
  new DataView(b.buffer).setUint32(12, expires >>> 0, true);
  const mac = await env.hmac(key, b.subarray(0, 16));
  b.set(mac.subarray(0, 16), 16);
  return base64(b, true);
}

// lane ONLINE-3: the rejoin ticket (RelayProtocol.h version 2, 34 bytes): u8 2, the same fields, u16 generation (>= 1), then 16 bytes of HMAC-SHA256
// over the 18 bytes before. A HELLO with a newer generation binds the slot's new incarnation at the relay without the old session token
export async function signRejoinTicket(env, keyId, key, gameIdHex, slot, mask, expires, generation) {
  const b = new Uint8Array(34);
  b[0] = 2;
  b[1] = keyId;
  const id = hexToBytes(gameIdHex);
  for (let i = 0; i < 8; i++) b[2 + i] = id[7 - i];
  b[10] = slot;
  b[11] = mask;
  const v = new DataView(b.buffer);
  v.setUint32(12, expires >>> 0, true);
  v.setUint16(16, generation, true);
  const mac = await env.hmac(key, b.subarray(0, 18));
  b.set(mac.subarray(0, 16), 18);
  return base64(b, true);
}

// lane ONLINE-3: the spectator ticket (RelayProtocol.h version 3, 34 bytes): u8 3, the same fields with the spectator index as the slot byte, u16 the
// spectator delay in seconds, then 16 bytes of HMAC-SHA256 over the 18 bytes before
export async function signSpectatorTicket(env, keyId, key, gameIdHex, index, mask, expires, delaySeconds) {
  const b = base64UrlDecode(await signRejoinTicket(env, keyId, key, gameIdHex, index, mask, expires, delaySeconds));
  b[0] = 3;
  const mac = await env.hmac(key, b.subarray(0, 18));
  b.set(mac.subarray(0, 16), 18);
  return base64(b, true);
}

// ---- errors ----------------------------------------------------------------------------------------------------------------------------------------------
export class LobbyError extends Error {
  constructor(status, code, message) {
    super(message);
    this.status = status;
    this.code = code;
  }
}
const bad = (code, msg) => new LobbyError(400, code, msg);

function intIn(v, lo, hi, what) {
  if (!Number.isInteger(v) || v < lo || v > hi) throw bad('bad_field', `${what} must be an integer in ${lo} .. ${hi}`);
  return v;
}
function str(v, maxLen, what, { empty = false } = {}) {
  if (typeof v !== 'string' || (!empty && !v.length) || enc.encode(v).length > maxLen) throw bad('bad_field', `${what} must be a string of ${empty ? 0 : 1} .. ${maxLen} bytes`);
  // no control characters (names and chat are shown in the game's UI)
  if (/[\u0000-\u001f\u007f]/.test(v)) throw bad('bad_field', `${what} has control characters`);
  return v;
}

// the game id a relay log names in its header (services/online/relay/RelayCore.h, "Log format": "OBFMERLG", u32 version, u64 game id, ...)
// DEPLOY-2: versions 1 and 2 (ONLINE-3's relay writes 2: the same header, plus REJOINED records after it, which the lobby does not read)
function logGameOf(bytes) {
  if (bytes.length < LOG_HEADER_BYTES || dec.decode(bytes.subarray(0, 8)) !== 'OBFMERLG') throw bad('bad_log', 'not a relay log');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  const version = view.getUint32(8, true);
  if (version !== 1 && version !== 2) throw bad('bad_log', `relay log version ${version} (this lobby reads versions 1 and 2)`);
  return view.getBigUint64(12, true).toString(16).padStart(16, '0');
}

// ---- the lobby ---------------------------------------------------------------------------------------------------------------------------------------------
// config: { ticketKeys: Map, sessionKey: Uint8Array, uploadKey: Uint8Array|null, relays: [{ id, address }], runAhead, crcInterval, ticketSeconds,
//           publicUrl, limits }
export class Lobby {
  constructor(env, config) {
    this.env = env;
    this.config = {
      runAhead: 2,
      crcInterval: 100,
      ticketSeconds: 6 * 3600,
      sessionSeconds: 30 * 24 * 3600,
      openGameIdleMs: 10 * 60 * 1000,
      startedGameKeepMs: 3 * 3600 * 1000,
      maxGames: 2000,
      ...config,
    };
    if (!(this.config.ticketKeys instanceof Map) || !this.config.ticketKeys.size) throw new Error('lobby: no ticket keys (TICKET_KEYS)');
    if (!this.config.sessionKey || this.config.sessionKey.length < 16) throw new Error('lobby: no session key (SESSION_KEY, 16+ bytes of hex)');
    // CFDEPLOY-1: an empty RELAYS is allowed (the lobby can run before its relay exists): /v1/health says so and START is refused ('no_relay')
    if (!Array.isArray(this.config.relays)) throw new Error('lobby: RELAYS is not a list');
    for (const r of this.config.relays) {
      if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5}$/.test(r.address)) throw new Error(`lobby: relay address ${r.address} must be a.b.c.d:port`);
    }
    this.games = new Map(); // id -> game
    this.buckets = new Map(); // rate limit key -> { tokens, at }
    this.relayStatus = new Map(); // INFRA-2 r2: relay address -> its uploader's last status (relayStatus())
  }

  load(games, relayStatus = []) {
    for (const g of games) this.games.set(g.id, g);
    for (const r of relayStatus) this.relayStatus.set(r.relay, r);
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
  async makeToken(account) {
    const body = enc.encode(JSON.stringify({ ...account, exp: Math.floor(this.env.now() / 1000) + this.config.sessionSeconds }));
    const mac = await this.env.hmac(this.config.sessionKey, body);
    return `${base64(body, true)}.${base64(mac, true)}`;
  }
  async account(authorization) {
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
    const a = JSON.parse(dec.decode(body));
    if (!(a.exp > this.env.now() / 1000)) throw new LobbyError(401, 'signed_out', 'the session expired: sign in again');
    return a;
  }

  async guest(body, ip) {
    this.limit(`signin:${ip}`, 10, 20);
    const name = str(body?.name, 24, 'name').trim();
    if (!name) throw bad('bad_field', 'name must not be blank');
    const id = `guest:${bytesToHex(this.env.randomBytes(8))}`;
    const account = { id, name, kind: 'guest' };
    return { v: VERSION, token: await this.makeToken(account), account };
  }

  // ---- games ----
  publicGame(g) {
    return {
      id: g.id,
      name: g.name,
      host: g.slots[g.hostSlot].name,
      map: g.setup.map,
      players: g.slots.filter((s) => s.state === 'player').length,
      openSlots: g.slots.filter((s) => s.state === 'open').length,
      state: g.state,
      password: !!g.password,
    };
  }
  gameView(g, account) {
    return {
      id: g.id,
      name: g.name,
      state: g.state,
      hostSlot: g.hostSlot,
      profile: g.profile,
      setup: { ...g.setup, slots: undefined },
      slots: g.slots.map((s, i) => ({
        slot: i,
        state: s.state,
        name: s.name ?? null,
        faction: s.faction,
        color: s.color,
        startPos: s.startPos,
        team: s.team,
        ready: !!s.ready,
        you: !!account && s.account === account.id,
      })),
      seq: g.seq,
    };
  }
  sweep() {
    const now = this.env.now();
    for (const [id, g] of this.games) {
      const keep = g.state === 'started' ? this.config.startedGameKeepMs : this.config.openGameIdleMs;
      if (now - g.touched > keep) {
        this.games.delete(id);
        this.env.store.deleteGame?.(id);
      }
    }
  }
  game(id) {
    const g = this.games.get(id);
    if (!g) throw new LobbyError(404, 'no_game', `no game ${id}`);
    return g;
  }
  slotOf(g, account) {
    return g.slots.findIndex((s) => s.account === account.id);
  }
  async save(g) {
    g.seq += 1;
    g.touched = this.env.now();
    await this.env.store.saveGame?.(g);
  }
  event(g, text, from = null) {
    g.chat.push({ seq: g.seq + 1, from, text, at: this.env.now() });
    if (g.chat.length > 200) g.chat.splice(0, g.chat.length - 200);
  }

  checkSlotChoice(s, body) {
    if (body.faction !== undefined) s.faction = intIn(body.faction, -1, 63, 'faction');
    if (body.color !== undefined) s.color = intIn(body.color, -1, 63, 'color');
    if (body.startPos !== undefined) s.startPos = intIn(body.startPos, -1, MAX_SLOTS - 1, 'startPos');
    if (body.team !== undefined) s.team = intIn(body.team, -1, MAX_SLOTS - 1, 'team');
  }
  checkSetup(setup, slotsRequired) {
    if (typeof setup !== 'object' || setup === null) throw bad('bad_field', 'setup must be an object');
    const out = {
      map: str(setup.map, 200, 'setup.map'),
      // ONLINE-4: -1 is RotWK's unset money (GameInfo::reset, RW 0x801AAF): the game's InitialResources rule gives it (what the online screens send)
      startingCash: intIn(setup.startingCash ?? 1500, -1, 1000000000, 'setup.startingCash'),
      superweaponRestriction: intIn(setup.superweaponRestriction ?? 0, 0, 1, 'setup.superweaponRestriction'),
      legendaryHeroes: intIn(setup.legendaryHeroes ?? 1, 1, 4, 'setup.legendaryHeroes'),
      // lane ONLINE-3: how far behind the players the spectators watch (seconds of game time; the relay holds the log back that far)
      spectatorDelay: intIn(setup.spectatorDelay ?? 0, 0, 600, 'setup.spectatorDelay'),
      rules: setup.rules ?? null,
    };
    if (out.rules !== null) {
      if (!Array.isArray(out.rules) || out.rules.length !== 10) throw bad('bad_field', 'setup.rules must be null or 10 integers');
      out.rules.forEach((r, i) => intIn(r, -2147483648, 2147483647, `setup.rules[${i}]`));
    }
    if (slotsRequired) {
      if (!Array.isArray(setup.slots) || setup.slots.length < 2 || setup.slots.length > MAX_SLOTS) throw bad('bad_field', 'setup.slots must be 2 .. 8 slots');
    }
    return out;
  }

  async create(account, body, ip) {
    this.limit(`create:${account.id}`, 6, 6);
    this.sweep();
    if (this.games.size >= this.config.maxGames) throw new LobbyError(503, 'full', 'the lobby holds its maximum number of games');
    for (const g of this.games.values()) {
      if (g.state === 'open' && this.slotOf(g, account) >= 0) throw bad('in_game', `you are in game ${g.id} already: leave it first`);
    }
    const name = str(body?.name, 48, 'name');
    const profile = str(body?.profile, 32, 'profile');
    if (!/^[0-9a-f]{32}$/.test(profile)) throw bad('bad_field', 'profile must be the profile identity\'s MD5 (32 hex digits)');
    const password = body.password === undefined || body.password === '' ? null : str(body.password, 32, 'password');
    const setup = this.checkSetup(body.setup, true);
    const slots = body.setup.slots.map((s, i) => {
      if (!SLOT_STATES.includes(s?.state)) throw bad('bad_field', `setup.slots[${i}].state must be one of ${SLOT_STATES.join(', ')}`);
      const slot = { state: s.state, faction: -1, color: -1, startPos: -1, team: -1, ready: false };
      if (AI_STATES.has(s.state)) slot.name = str(s.name ?? 'Computer', 24, `setup.slots[${i}].name`);
      this.checkSlotChoice(slot, s);
      return slot;
    });
    while (slots.length < MAX_SLOTS) slots.push({ state: 'closed', faction: -1, color: -1, startPos: -1, team: -1, ready: false });
    const hostSlot = intIn(body.hostSlot ?? 0, 0, MAX_SLOTS - 1, 'hostSlot');
    if (slots[hostSlot].state !== 'player') throw bad('bad_field', `the host's slot ${hostSlot} must be "player"`);
    for (const [i, s] of slots.entries()) {
      if (s.state === 'player' && i !== hostSlot) throw bad('bad_field', 'other human slots start "open" (players join them)');
    }
    slots[hostSlot].account = account.id;
    slots[hostSlot].name = account.name;
    const id = bytesToHex(this.env.randomBytes(8));
    const g = { id, name, password, profile, setup, slots, hostSlot, state: 'open', chat: [], seq: 0, created: this.env.now(), touched: this.env.now(), ip };
    this.event(g, `${account.name} created the game`);
    this.games.set(id, g);
    await this.save(g);
    return { v: VERSION, id, game: this.gameView(g, account) };
  }

  // ONLINE-4: `running` also lists the started games (the server browser offers them to watch: POST .../spectate), after the open ones
  list(running = false) {
    this.sweep();
    const games = [...this.games.values()].filter((g) => g.state === 'open' || (running && g.state === 'started'))
      .sort((a, b) => (b.state === 'open') - (a.state === 'open') || b.created - a.created).slice(0, 200).map((g) => this.publicGame(g));
    return { v: VERSION, games };
  }

  async join(account, id, body) {
    this.limit(`join:${account.id}`, 20, 10);
    const g = this.game(id);
    const existing = this.slotOf(g, account);
    if (existing >= 0) return { v: VERSION, slot: existing, game: this.gameView(g, account) };
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (g.password && body?.password !== g.password) throw new LobbyError(403, 'password', 'wrong password');
    if (body?.profile !== g.profile) {
      throw new LobbyError(409, 'profile', `your game files or build differ from the host's (profile ${body?.profile} vs ${g.profile}): both need the same OpenBFME version and RotWK 2.01 files`);
    }
    for (const other of this.games.values()) {
      if (other !== g && other.state === 'open' && this.slotOf(other, account) >= 0) throw bad('in_game', `you are in game ${other.id} already: leave it first`);
    }
    const slot = g.slots.findIndex((s) => s.state === 'open');
    if (slot < 0) throw new LobbyError(409, 'full', 'the game is full');
    Object.assign(g.slots[slot], { state: 'player', account: account.id, name: account.name, ready: false });
    this.unreadyAll(g);
    this.event(g, `${account.name} joined`);
    await this.save(g);
    return { v: VERSION, slot, game: this.gameView(g, account) };
  }

  unreadyAll(g) {
    for (const s of g.slots) s.ready = false;
  }

  async leave(account, id) {
    const g = this.game(id);
    const slot = this.slotOf(g, account);
    if (slot < 0) return { v: VERSION, left: false };
    if (g.state === 'open' && slot === g.hostSlot) {
      g.state = 'closed';
      this.event(g, 'the host closed the game');
      await this.save(g);
      this.games.delete(id);
      await this.env.store.deleteGame?.(id);
      return { v: VERSION, left: true, closed: true };
    }
    if (g.state === 'open') {
      Object.assign(g.slots[slot], { state: 'open', account: undefined, name: undefined, ready: false, faction: -1, color: -1, startPos: -1, team: -1 });
      this.unreadyAll(g);
    }
    this.event(g, `${account.name} left`);
    await this.save(g);
    return { v: VERSION, left: true };
  }

  async setSlot(account, id, body) {
    this.limit(`slot:${account.id}`, 120, 20);
    const g = this.game(id);
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    const own = this.slotOf(g, account);
    if (own < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    const target = body.slot === undefined ? own : intIn(body.slot, 0, MAX_SLOTS - 1, 'slot');
    const s = g.slots[target];
    if (target !== own) {
      // the host arranges the slots that are not players: open / closed / an AI and its options
      if (own !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host changes other slots');
      if (s.state === 'player') throw new LobbyError(403, 'player_slot', 'a player chooses their own options');
      if (body.state !== undefined) {
        if (!SLOT_STATES.includes(body.state) || body.state === 'player') throw bad('bad_field', 'state must be open, closed or an AI level');
        s.state = body.state;
        s.name = AI_STATES.has(body.state) ? str(body.name ?? 'Computer', 24, 'name') : undefined;
      }
    }
    this.checkSlotChoice(s, body);
    this.unreadyAll(g);
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async setSetup(account, id, body) {
    const g = this.game(id);
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host changes the game options');
    g.setup = this.checkSetup({ ...g.setup, ...body }, false);
    this.unreadyAll(g);
    this.event(g, 'the host changed the game options');
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async ready(account, id, body) {
    const g = this.game(id);
    const slot = this.slotOf(g, account);
    if (slot < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    if (typeof body?.ready !== 'boolean') throw bad('bad_field', 'ready must be true or false');
    g.slots[slot].ready = body.ready;
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  async chat(account, id, body) {
    this.limit(`chat:${account.id}`, 30, 8);
    const g = this.game(id);
    if (this.slotOf(g, account) < 0) throw new LobbyError(403, 'not_in_game', 'join the game first');
    const text = str(body?.text, 200, 'text');
    this.event(g, text, account.name);
    await this.save(g);
    return { v: VERSION, seq: g.seq };
  }

  async kick(account, id, body) {
    const g = this.game(id);
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host kicks');
    const slot = intIn(body?.slot, 0, MAX_SLOTS - 1, 'slot');
    if (slot === g.hostSlot || g.slots[slot].state !== 'player') throw bad('bad_field', 'kick another player\'s slot');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    const name = g.slots[slot].name;
    Object.assign(g.slots[slot], { state: 'open', account: undefined, name: undefined, ready: false, faction: -1, color: -1, startPos: -1, team: -1 });
    (g.kicked ??= []).push(slot);
    this.unreadyAll(g);
    this.event(g, `${name} was kicked`);
    await this.save(g);
    return { v: VERSION, game: this.gameView(g, account) };
  }

  // The START (OnlineStart.h): the service decides the seed, the run-ahead, the CRC interval and the relay, and signs one ticket per human slot
  async start(account, id) {
    const g = this.game(id);
    if (this.slotOf(g, account) !== g.hostSlot) throw new LobbyError(403, 'not_host', 'only the host starts the game');
    if (g.state !== 'open') throw new LobbyError(409, 'started', 'the game has started');
    if (!this.config.relays.length) throw new LobbyError(503, 'no_relay', 'no relay configured: online games cannot start yet');
    const humans = g.slots.map((s, i) => [s, i]).filter(([s]) => s.state === 'player');
    if (humans.length < 2) throw new LobbyError(409, 'players', 'an online game needs at least two players');
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
    const mask = humans.reduce((m, [, i]) => m | (1 << i), 0);
    const seedBytes = this.env.randomBytes(4);
    const seed = new DataView(seedBytes.buffer, seedBytes.byteOffset, 4).getUint32(0, true);
    const relay = this.config.relays[0];
    const keyId = Math.max(...this.config.ticketKeys.keys());
    const key = this.config.ticketKeys.get(keyId);
    const expires = Math.floor(this.env.now() / 1000) + this.config.ticketSeconds;
    let last = g.slots.length;
    while (last > 0 && g.slots[last - 1].state === 'closed') last -= 1;
    const setup = Object.freeze({
      map: g.setup.map,
      seed,
      startingCash: g.setup.startingCash,
      superweaponRestriction: g.setup.superweaponRestriction,
      rules: g.setup.rules,
      legendaryHeroes: g.setup.legendaryHeroes,
      slots: g.slots.slice(0, last).map((s) => (s.state === 'player' || AI_STATES.has(s.state)
        ? { state: s.state, name: s.name, faction: s.faction, color: s.color, startPos: s.startPos, team: s.team }
        : { state: s.state })),
    });
    const starts = {};
    for (const [, i] of humans) {
      starts[i] = {
        v: VERSION,
        kind: 'openbfme-start',
        game: g.id,
        slot: i,
        relay: relay.address,
        ticket: await signTicket(this.env, keyId, key, g.id, i, mask, expires),
        runAhead: this.config.runAhead,
        crcInterval: this.config.crcInterval,
        profile: g.profile,
        setup,
      };
    }
    g.starts = starts;
    g.mask = mask; // lane ONLINE-3: a rejoin ticket names the same slots
    g.state = 'started';
    g.startedAt = this.env.now();
    this.event(g, 'the game started');
    await this.save(g);
    // ONLINE-2 r2 (Sol: /v1/replays/<id>/start handed anyone a live ticket): the published record of the game names no slot, no relay and no
    // ticket ("openbfme-replay-start", GameNetwork/OnlineStart.h): enough to turn the relay's log into a replay, never a credential
    const replayStart = { v: VERSION, kind: 'openbfme-replay-start', game: g.id, runAhead: this.config.runAhead, crcInterval: this.config.crcInterval, profile: g.profile, setup };
    // (no log store: no replay record either; /v1/replays then answers 'no_log_store')
    await this.env.store.putBlob?.(`starts/${g.id}.json`, enc.encode(JSON.stringify(replayStart)));
    // CFDEPLOY-1 r2 (Sol r1: replay downloads were open to anyone): the accounts that played; only they read the game's replay files. docs/ONLINE.md
    // promises no public replays ("view online" is the player's own link), so a public listing would be a new decision
    await this.env.store.putBlob?.(`players/${g.id}.json`, enc.encode(JSON.stringify(humans.map(([s]) => s.account))));
    return { v: VERSION, start: g.starts[this.slotOf(g, account)] };
  }

  async poll(account, id, since) {
    const g = this.game(id);
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
      chat: g.chat.filter((c) => c.seq > s).map(({ seq, from, text }) => ({ seq, from, text })),
      start: g.state === 'started' ? g.starts[slot] : null,
    };
  }

  // ---- lane ONLINE-3: rejoin ----
  // the signed-in player's running games: a client whose game restarted (a crash, a lost connection) offers "rejoin" for each
  mine(account) {
    const games = [...this.games.values()]
      .filter((g) => g.state === 'started' && this.slotOf(g, account) >= 0)
      .map((g) => ({ ...this.publicGame(g), slot: this.slotOf(g, account), rejoin: `/v1/games/${g.id}/rejoin` }));
    return { v: VERSION, games };
  }
  // a fresh START for the player's slot of a running game: its ticket is a rejoin ticket of the next generation (the relay binds the slot's new
  // incarnation with it and refuses every older ticket of the slot); "rejoin" names the generation. Only the slot's own account gets one
  async rejoin(account, id) {
    this.limit(`rejoin:${account.id}`, 6, 6);
    const g = this.game(id);
    const slot = this.slotOf(g, account);
    if (slot < 0) throw new LobbyError(403, 'not_in_game', 'you have no slot in this game');
    if (g.state !== 'started' || !g.starts?.[slot]) throw new LobbyError(409, 'not_started', 'the game has not started');
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
    return { v: VERSION, start: { ...g.starts[slot], ticket, rejoin: generation } };
  }

  // lane ONLINE-3: a spectator START for a signed-in account that is not a player of the started game: a spectator ticket (a fresh index each time, so a
  // spectator whose game restarted asks again) and the game's setup; the relay serves it the game's log, `delay` seconds behind the players
  async spectate(account, id) {
    this.limit(`spectate:${account.id}`, 12, 12);
    const g = this.game(id);
    if (this.slotOf(g, account) >= 0) throw new LobbyError(409, 'player', 'players of the game play it; spectators are other accounts');
    if (g.state !== 'started' || !g.starts) throw new LobbyError(409, 'not_started', 'the game has not started yet: ask again once it has');
    const index = g.spectatorsIssued ?? 0;
    if (index >= 16) throw new LobbyError(409, 'spectators_full', 'the game has had its 16 spectator places');
    g.spectatorsIssued = index + 1;
    const keyId = Math.max(...this.config.ticketKeys.keys());
    const key = this.config.ticketKeys.get(keyId);
    const expires = Math.floor(this.env.now() / 1000) + this.config.ticketSeconds;
    const mask = g.mask ?? g.slots.reduce((m, s, i) => (s.state === 'player' ? m | (1 << i) : m), 0);
    const delay = g.setup.spectatorDelay ?? 0;
    const any = Object.values(g.starts)[0];
    const ticket = await signSpectatorTicket(this.env, keyId, key, g.id, index, mask, expires, delay);
    (g.spectators ??= []).push({ account: account.id, name: account.name, index });
    this.event(g, `${account.name} is watching`);
    await this.save(g);
    return {
      v: VERSION,
      start: { v: VERSION, kind: 'openbfme-spectate', game: g.id, spectator: index, relay: any.relay, ticket, delay, runAhead: any.runAhead, crcInterval: any.crcInterval, profile: g.profile, setup: any.setup },
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
      if (old.length === bytes.length && old.every((x, i) => x === bytes[i])) return { v: VERSION, stored: key, unchanged: true };
      throw new LobbyError(409, 'log_exists', `game ${id} has a different log stored already`);
    }
    await store.putBlob(key, bytes);
    return { v: VERSION, stored: key };
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
    return b;
  }

  // ---- the admin backup (INFRA-2): list, export and import the stored blobs (relay logs, replay START records, player lists) ----
  // Every admin request carries X-OpenBFME-Admin: <unix seconds>.<hex HMAC-SHA256(ADMIN_KEY, "<METHOD> <path>?<query>\n<seconds>\n" + body)>, within
  // ADMIN_WINDOW_S of the lobby's clock. A replayed request inside the window can only repeat a read or an identical import (blobs are never replaced).
  // The admin key is its own secret (admin.key), not the relays' upload key: a relay host can store its logs but never read anyone's.
  async adminCheck(req, bytes) {
    if (!this.config.adminKey) throw new LobbyError(501, 'not_configured', 'the admin backup is not configured (ADMIN_KEY)');
    const m = /^(\d{1,12})\.([0-9a-f]{64})$/.exec(String(req.headers['x-openbfme-admin'] ?? ''));
    if (!m) throw new LobbyError(401, 'admin_signature', 'an admin request needs X-OpenBFME-Admin: <unix seconds>.<hex HMAC>');
    if (Math.abs(Number(m[1]) - this.env.now() / 1000) > ADMIN_WINDOW_S) throw new LobbyError(401, 'admin_clock', `the admin signature's time is more than ${ADMIN_WINDOW_S} s from the lobby's`);
    const q = req.query.toString();
    const head = enc.encode(`${req.method} ${req.path}?${q}\n${m[1]}\n`);
    const signed = new Uint8Array(head.length + bytes.length);
    signed.set(head);
    signed.set(bytes, head.length);
    const want = bytesToHex(await this.env.hmac(this.config.adminKey, signed));
    let diff = 0;
    for (let i = 0; i < 64; i++) diff |= want.charCodeAt(i) ^ m[2].charCodeAt(i);
    if (diff) throw new LobbyError(401, 'admin_signature', 'the admin signature does not match');
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
    if (!parseBlobKey(key)) throw bad('bad_field', 'not a blob key (logs/<id>.obfmerlog, starts/<id>.json, players/<id>.json)');
    const b = await this.logStore().getBlob(key);
    if (!b) throw new LobbyError(404, 'no_blob', `no blob ${key}`);
    return b;
  }
  // the import a restore runs: a blob is written only where none is; the same bytes again answer 'unchanged', other bytes 409 'blob_exists'
  async adminPut(key, bytes) {
    const store = this.logStore();
    const k = parseBlobKey(key);
    if (!k) throw bad('bad_field', 'not a blob key (logs/<id>.obfmerlog, starts/<id>.json, players/<id>.json)');
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
    }
    const old = await store.getBlob(key);
    if (old) {
      if (old.length === bytes.length && old.every((x, i) => x === bytes[i])) return { v: VERSION, stored: key, unchanged: true };
      throw new LobbyError(409, 'blob_exists', `${key} holds different bytes already`);
    }
    await store.putBlob(key, bytes);
    return { v: VERSION, stored: key };
  }

  health() {
    const relays = this.config.relays.map((r) => ({ id: r.id, address: r.address }));
    return {
      v: VERSION,
      ok: true,
      games: this.games.size,
      relays,
      ...(relays.length ? {} : { relay: 'no relay configured' }),
      logStore: this.env.store.kind ?? (this.env.store.putBlob ? 'yes' : 'none'),
    };
  }
}

// ---- routing: one function both platforms call -------------------------------------------------------------------------------------------------------------
// req: { method, path, query (URLSearchParams), headers (lower-case keys), json() -> Promise, bytes() -> Promise<Uint8Array>, ip }
// returns { status, headers, body (string | Uint8Array) }
export async function route(lobby, req) {
  const json = (status, obj) => ({ status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' }, body: JSON.stringify(obj) });
  try {
    const p = req.path.split('/').filter(Boolean);
    const m = req.method;
    const body = async () => {
      // ONLINE-2 r2 (Sol: a chunked 20 KB body passed the Content-Length check): the bytes actually received count, whatever the headers say
      if (Number(req.headers['content-length'] ?? 0) > MAX_JSON_BYTES) throw new LobbyError(413, 'too_big', `request body over ${MAX_JSON_BYTES} bytes`);
      const raw = await req.bytes();
      if (raw.length > MAX_JSON_BYTES) throw new LobbyError(413, 'too_big', `request body over ${MAX_JSON_BYTES} bytes`);
      try {
        return raw.length ? (JSON.parse(dec.decode(raw)) ?? {}) : {};
      } catch {
        throw bad('bad_json', 'the body must be JSON');
      }
    };
    const v1 = p[0] === 'v1';
    if (m === 'GET' && p[0] === 'j' && p.length === 2 && /^[0-9a-f]{16}$/.test(p[1])) {
      // the invite page: the openbfme:// link (the launcher's handler) and the id for the debug join
      const link = `openbfme://join/${p[1]}`;
      return { status: 200, headers: { 'content-type': 'text/html; charset=utf-8' }, body: `<!doctype html><meta charset="utf-8"><title>OpenBFME game invite</title><h1>OpenBFME online game</h1><p><a href="${link}">Join game ${p[1]}</a> (opens the OpenBFME launcher).</p><p>Or start OpenBFME with <code>--online-join ${p[1]}</code>. Online play is a test feature.</p>` };
    }
    if (!v1) return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
    if (m === 'GET' && p[1] === 'health') return json(200, lobby.health());
    // ONLINE-4: the message of the day the game's online home page shows (OnlineHome.apt); null when the lobby has none
    if (m === 'GET' && p[1] === 'motd' && p.length === 2) return json(200, { v: VERSION, motd: lobby.config.motd || null });
    if (m === 'POST' && p[1] === 'auth' && p[2] === 'guest') return json(200, await lobby.guest(await body(), req.ip));
    if (m === 'POST' && p[1] === 'relay' && p[2] === 'logs' && p.length === 4) {
      return json(200, await lobby.uploadLog(p[3], await req.bytes(), String(req.headers['x-openbfme-signature'] ?? '')));
    }
    if (m === 'POST' && p[1] === 'relay' && p[2] === 'status' && p.length === 3) {
      return json(200, await lobby.relayStatusPost(await req.bytes(), String(req.headers['x-openbfme-signature'] ?? '')));
    }
    if (p[1] === 'admin' && p[2] === 'relay-status' && p.length === 3) {
      await lobby.adminCheck(req, await req.bytes());
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
    if (p[1] !== 'games') return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
    if (m === 'GET' && p.length === 2) return json(200, lobby.list(req.query.get('running') === '1'));
    const account = await lobby.account(req.headers.authorization);
    if (m === 'POST' && p.length === 2) return json(200, await lobby.create(account, await body(), req.ip));
    const id = p[2];
    if (!/^[0-9a-f]{16}$/.test(id ?? '')) return json(404, { v: VERSION, error: 'no_game', message: 'a game id is 16 hex digits' });
    if (m === 'GET' && p.length === 3) {
      const since = req.query.get('since');
      return json(200, await lobby.poll(account, id, since === null ? 0 : Number(since)));
    }
    const actions = { join: 'join', leave: 'leave', slot: 'setSlot', setup: 'setSetup', ready: 'ready', chat: 'chat', kick: 'kick', start: 'start', rejoin: 'rejoin', spectate: 'spectate' };
    if (m === 'POST' && p.length === 4 && actions[p[3]]) {
      const b = p[3] === 'leave' || p[3] === 'start' || p[3] === 'rejoin' || p[3] === 'spectate' ? {} : await body();
      return json(200, await lobby[actions[p[3]]](account, id, b));
    }
    return json(404, { v: VERSION, error: 'not_found', message: 'unknown path' });
  } catch (e) {
    if (e instanceof LobbyError) return json(e.status, { v: VERSION, error: e.code, message: e.message });
    return json(500, { v: VERSION, error: 'internal', message: 'internal error' });
  }
}

// ---- configuration from environment-style strings (both platforms) -----------------------------------------------------------------------------------------
// TICKET_KEYS: the key ring text; SESSION_KEY / UPLOAD_KEY / ADMIN_KEY (INFRA-2: the backup export): hex; RELAYS: "id=a.b.c.d:port,..."; PUBLIC_URL;
// RUN_AHEAD; CRC_INTERVAL; MOTD (ONLINE-4: the message of the day, up to 2000 bytes, lines separated by \n)
function motdFromEnv(text) {
  if (text === undefined || text === null || text === '') return null;
  const m = String(text).replace(/\\n/g, '\n');
  if (enc.encode(m).length > 2000) throw new Error('MOTD: over 2000 bytes');
  if (/[\u0000-\u0009\u000b-\u001f\u007f]/.test(m)) throw new Error('MOTD: control characters other than line breaks');
  return m;
}

export function configFromEnv(e) {
  const relays = String(e.RELAYS ?? '').split(',').map((x) => x.trim()).filter(Boolean).map((x) => {
    const [id, address] = x.includes('=') ? x.split('=') : ['default', x];
    return { id, address };
  });
  return {
    ticketKeys: parseKeyRing(String(e.TICKET_KEYS ?? '')),
    sessionKey: hexToBytes(String(e.SESSION_KEY ?? '')),
    uploadKey: e.UPLOAD_KEY ? hexToBytes(String(e.UPLOAD_KEY)) : null,
    adminKey: e.ADMIN_KEY ? hexToBytes(String(e.ADMIN_KEY)) : null,
    relays,
    publicUrl: String(e.PUBLIC_URL ?? '').replace(/\/$/, ''),
    motd: motdFromEnv(e.MOTD),
    ...(e.RUN_AHEAD ? { runAhead: Number(e.RUN_AHEAD) } : {}),
    ...(e.CRC_INTERVAL ? { crcInterval: Number(e.CRC_INTERVAL) } : {}),
  };
}
