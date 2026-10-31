// OpenBFME online relay. GPL-3.0.
// See RelayCore.h.

#include "RelayCore.h"
#include "RelayCanonicalAuthority.h"

#include "Common/Crc32.h"
#include "Common/Sha256.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace
{
// Transport.h, packet version 2
constexpr size_t kPacketHeader = 4 + 2 + 1 + 1 + 4 + 2;
constexpr size_t kRecordHeader = 4 + 2 + 2 + 4;
constexpr std::uint8_t kPacketVersion = 2;
// NetPacket.h: the commands whose header names their execution frame and that decide a frame's command list
bool isFrameCommand(std::uint8_t type)
{
	return type == 3 /*FRAMEINFO*/ || type == 4 /*GAMECOMMAND*/ || type == 10 /*PLAYERLEAVE*/ || type == 11 /*DESTROYPLAYER*/ || type == 0x82 /*RUNAHEAD*/
		|| type == 0x83 /*REJOIN, lane ONLINE-3*/ || type == 0x8A /*VIEW, lane ANTICHEAT-1: the player's view, logged like a frame command*/;
}
constexpr std::uint8_t kDisconnectPlayer = 26; // NetPacket.h NETCOMMANDTYPE_DISCONNECTPLAYER (lane ONLINE-3: logged once, the drop frame)
constexpr std::uint8_t kDisconnectPlayer16 = 0x87; // NetPacket.h: the same command with a u16 released mask
constexpr std::uint32_t kKeepFrames = 4000;
constexpr size_t kMaxFrontier = 4096;  ///< lane ONLINE-3: frontier entries per game (thinned beyond)
constexpr size_t kEarlyOverhead = 64; ///< ONLINE-2 r3: an early record's map node and vector header count against the caps, not only its bytes ///< per-frame counts older than this behind a stream's newest frame are forgotten (run-ahead is at most 25)

std::uint16_t get16(const std::uint8_t *p)
{
	return (std::uint16_t)(p[0] | (p[1] << 8));
}
std::uint32_t get32(const std::uint8_t *p)
{
	return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
void put8(std::vector<std::uint8_t> &b, std::uint8_t v)
{
	b.push_back(v);
}
void put32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		b.push_back((std::uint8_t)(v >> (8 * i)));
	}
}
void put64(std::vector<std::uint8_t> &b, std::uint64_t v)
{
	put32(b, (std::uint32_t)v);
	put32(b, (std::uint32_t)(v >> 32));
}
bool seqBefore(std::uint32_t a, std::uint32_t b)
{
	return (std::int32_t)(a - b) < 0;
}
std::string hex64(std::uint64_t v)
{
	char b[17];
	std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
	return b;
}
} // namespace

std::string RelayAddress::text() const
{
	char b[32];
	std::snprintf(b, sizeof(b), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
	return b;
}

RelayCore::RelayCore(const Options &options) : m_options(options) {}

// ROUTER-1: a canonical game's durable append — the record's bytes land in the game's OBFMERLG log (record kind 6,
// the OBRC wire encoding) before the authority fans them out. The game log is the commit journal.
bool RelayCore::canonicalAppend(Game &g, const RelayCommit::Record &record, std::string &error)
{
	if (!g.log)
	{
		error = "canonical game " + hex64(g.id) + " has no live log";
		return false;
	}
	std::vector<std::uint8_t> recordBytes;
	if (!RelayCommit::encode(record, recordBytes, error))
	{
		return false;
	}
	if (record.kind == RelayCommit::Kind::Frame)
	{
		g.maxFrame = std::max(g.maxFrame, record.stamp.frame); // the frontier + spectator delay count canonical frames
	}
	std::vector<std::uint8_t> r;
	r.reserve(5 + recordBytes.size());
	put8(r, 6);
	put32(r, (std::uint32_t)recordBytes.size());
	r.insert(r.end(), recordBytes.begin(), recordBytes.end());
	write(g, r);
	if (!g.log)
	{
		error = "the log of game " + hex64(g.id) + " stopped on the canonical append";
		return false;
	}
	++m_stats.canonicalRecords;
	return true;
}

void RelayCore::takePending(std::vector<Action> &out)
{
	while (!m_pending.empty())
	{
		Action a = std::move(m_pending.front());
		m_pending.pop_front();
		sealAction(a); // lane AUTH-1: pending emits authenticate like handle()'s own
		out.push_back(std::move(a));
	}
}

void RelayCore::emitCanonical(const Game &g, const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> &emissions,
	std::vector<Action> &out)
{
	for (const auto &[slot, packet] : emissions)
	{
		const Slot &s = g.slots[slot];
		if (!s.bound || s.bye)
		{
			continue;
		}
		std::vector<std::uint8_t> d;
		RelayProtocol::putHeader(d, RelayProtocol::DATA_FROM);
		d.push_back(slot); // the DATA_FROM source byte: the relay's own stream to this slot uses the slot's number
		d.insert(d.end(), packet.begin(), packet.end());
		out.push_back(reply(s.address, std::move(d)));
	}
}

RelayCore::~RelayCore()
{
	shutdown(0);
}

void RelayCore::error(const std::string &e)
{
	if (m_errors.size() < 100)
	{
		m_errors.push_back(e);
	}
}

RelayCore::Action RelayCore::reply(const RelayAddress &to, std::vector<std::uint8_t> &&bytes)
{
	Action a;
	a.kind = Action::REPLY;
	a.to = to;
	a.reply = std::move(bytes);
	return a;
}

RelayCore::Action RelayCore::handle(const RelayAddress &from, std::uint8_t *d, size_t size, std::uint64_t nowMs, std::uint32_t nowUnix)
{
	using namespace RelayProtocol;
	++m_stats.datagrams;
	m_stats.bytesIn += size;
	if (!isRelayDatagram(d, size))
	{
		++m_stats.badDatagrams;
		return Action();
	}
	// lane AUTH-1: envelope version 5 is the authenticated one (RelayAuth.h). The HELLO context it proves is kept
	// for this call; everything else needs a binding the slot already authenticated
	m_helloAuth = HelloAuth();
	m_inAuthed = false;
	m_inVersion = d[2];
	if (d[2] != kVersionAuth)
	{
		// once a binding speaks the authenticated envelope it never accepts anything else: a version-3/4 datagram is a
		// downgrade, any other version a forgery — dropped, never answered (ONLINE16-FIX, AUTH-1 review 4: an unsealed
		// REFUSE_VERSION made the victim's client give up; nothing unauthenticated may influence an authenticated slot)
		const auto b = m_bindings.find(from.key());
		if (b != m_bindings.end() && bindingAuth(b->second).authed)
		{
			++m_stats.authDropped;
			if (!b->second.spectator)
			{
				++m_games.at(b->second.game).slots[b->second.slot].inUnauthRefused; // lane ANTICHEAT-1: D7
			}
			return Action();
		}
	}
	if (d[2] == kVersionAuth)
	{
		const size_t n = unwrapAuth(from, d, size);
		if (n == 0)
		{
			m_helloAuth = HelloAuth();
			return Action(); // counted inside: a bad tag or a replay is dropped silently
		}
		if (n == (size_t)-1)
		{
			++m_stats.notBound;
			m_inVersion = kVersion;
			return reply(from, encodeEmpty(NOT_BOUND)); // unbound, like a v4 datagram from an unknown address
		}
		size = n;
		m_inVersion = kVersion;
	}
	else if (d[2] != kVersion && d[2] != kVersionLegacy)
	{
		++m_stats.badDatagrams;
		++m_stats.refused;
		m_inVersion = kVersion;
		return reply(from, encodeRefused(REFUSE_VERSION));
	}
	Action a;
	switch (d[3])
	{
	case HELLO: a = hello(from, d, size, nowMs, nowUnix); break;
	case DATA: a = data(from, d, size, nowMs); break;
	case BYE: bye(from, nowMs); break;
	case LOG_REQUEST: a = logRequest(from, d, size, nowMs); break; // lane ONLINE-3
	default: ++m_stats.badDatagrams; m_helloAuth = HelloAuth(); return Action();
	}
	sealAction(a);
	// AUTH-1 review 7: the HELLO's key context and the envelope version belong to this datagram only (takePending /
	// tick seal later actions by their binding)
	m_helloAuth = HelloAuth();
	m_inVersion = kVersion;
	return a;
}

RelayCore::PeerAuth &RelayCore::bindingAuth(const Binding &b)
{
	Game &g = m_games.at(b.game);
	return b.spectator ? g.spectators[b.slot].auth : g.slots[b.slot].auth;
}

std::uint64_t RelayCore::nextCounterBase()
{
	const std::uint64_t now = (std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	m_counterBase = std::max(m_counterBase + 1, now);
	return m_counterBase;
}

size_t RelayCore::unwrapAuth(const RelayAddress &from, std::uint8_t *d, size_t size)
{
	using namespace RelayProtocol;
	// ONLINE16-FIX: an empty kind (BYE) seals to exactly the envelope: header + counter + tag (the "+ 1" dropped every sealed BYE, so an
	// authenticated game only ever closed on silence); a HELLO's ticket length is checked against its body below
	if (size < kHeaderBytes + RelayAuth::kOverhead)
	{
		++m_stats.authDropped;
		return 0;
	}
	RelayAuth::Keys keys;
	RelayAuth::Window *window = nullptr;
	Slot *inSlot = nullptr; // lane ANTICHEAT-1: the bound player slot of a non-HELLO datagram (its INPUT counts)
	std::uint64_t counter = 0;
	for (int i = 0; i < 8; ++i)
	{
		counter |= (std::uint64_t)d[kHeaderBytes + i] << (8 * i);
	}
	if (d[3] == HELLO)
	{
		// ONLINE16-FIX (AUTH-1 review 1): a HELLO is verified with the keys derived from ITS OWN ticket, whoever sends it —
		// never with the sender's binding: a party bound with its own key (a spectator, another player) must not carry
		// another slot's ticket + session token (both visible on the wire) into that slot
		const std::uint8_t *payload = d + kHeaderBytes + RelayAuth::kCounterBytes;
		const size_t body = size - kHeaderBytes - RelayAuth::kOverhead;
		const size_t ticketBytes = body >= 1 ? ticketSize(payload[0]) : 0;
		Ticket fields;
		std::string why;
		// AUTH-1 review 6: the ticket's own length must fit the datagram before a byte of it is read
		const auto keyIt = ticketBytes >= 2 && ticketBytes <= body ? m_options.keys.find(payload[1]) : m_options.keys.end();
		if (keyIt == m_options.keys.end() || !readTicketFields(payload, ticketBytes, fields, &why))
		{
			++m_stats.authDropped;
			return 0;
		}
		keys = RelayAuth::packetKeys(
			RelayAuth::sessionKey(keyIt->second.data(), keyIt->second.size(), payload, ticketBytes), fields.gameId, fields.slot, fields.generation);
		// AUTH-1 review 2: the window of the slot (or spectator place) the ticket names, when it authenticated with these
		// very keys — a captured HELLO replayed from any address is spent there
		const auto g = m_games.find(fields.gameId);
		if (g != m_games.end() && fields.slot < (fields.spectator ? kMaxSpectators : kMaxSlots))
		{
			PeerAuth &named = fields.spectator ? g->second.spectators[fields.slot].auth : g->second.slots[fields.slot].auth;
			if (named.authed && named.keys.c2r == keys.c2r && named.keys.r2c == keys.r2c)
			{
				window = &named.recv;
			}
		}
	}
	else
	{
		const auto it = m_bindings.find(from.key());
		if (it == m_bindings.end())
		{
			// an authenticated datagram from an address the relay forgot (NAT rebinding, a restart): like a v4
			// one it learns it is not bound and re-HELLOs, carrying its own tag
			return (size_t)-1;
		}
		PeerAuth &auth = bindingAuth(it->second);
		if (!it->second.spectator)
		{
			inSlot = &m_games.at(it->second.game).slots[it->second.slot]; // lane ANTICHEAT-1: whose c2r datagram this is (D7)
		}
		if (!auth.authed)
		{
			++m_stats.authDropped; // a v5 datagram to a binding that never authenticated
			if (inSlot)
			{
				++inSlot->inUnauthRefused;
			}
			return 0;
		}
		keys = auth.keys;
		window = &auth.recv;
	}
	// the tag, then the window
	std::uint8_t want[RelayAuth::kTagBytes];
	RelayAuth::tag(keys.c2r, d, size - RelayAuth::kTagBytes, want);
	if (!RelayAuth::tagEqual(want, d + size - RelayAuth::kTagBytes))
	{
		++m_stats.authDropped;
		if (inSlot)
		{
			++inSlot->inBadTag; // lane ANTICHEAT-1: D7
		}
		return 0;
	}
	if (window && !window->accept(counter))
	{
		++m_stats.authDropped;
		if (inSlot)
		{
			++inSlot->inReplayed; // lane ANTICHEAT-1: D7
		}
		return 0;
	}
	m_inAuthed = true;
	if (d[3] == HELLO)
	{
		m_helloAuth.active = true; // the same keys seal this HELLO's answer and are the only ones hello() may install
		m_helloAuth.keys = keys;
		m_helloAuth.counter = counter;
		m_helloAuth.windowed = window != nullptr;
	}
	// compact to the version-4 view for the regular dispatch (the counter and the tag go)
	const size_t payload = size - kHeaderBytes - RelayAuth::kOverhead;
	std::memmove(d + kHeaderBytes, d + kHeaderBytes + RelayAuth::kCounterBytes, payload);
	d[2] = kVersion;
	return kHeaderBytes + payload;
}

void RelayCore::installAuth(PeerAuth &auth, const RelayAuth::Keys &keys, std::uint16_t generation, std::uint64_t seenCounter)
{
	// ONLINE16-FIX (AUTH-1 review 5): new keys — a rejoin's generation or another ticket of the slot — start a fresh window
	// and a fresh send counter; the same keys keep both (unwrapAuth already spent this HELLO's counter in that window)
	if (!auth.authed || auth.generation != generation || auth.keys.c2r != keys.c2r || auth.keys.r2c != keys.r2c)
	{
		auth = PeerAuth();
		auth.generation = generation;
		auth.authed = true;
		auth.keys = keys;
		auth.sendCounter = nextCounterBase();
		auth.recv.accept(seenCounter); // the HELLO that bound it is consumed: a replayed one never verifies again
	}
}

void RelayCore::sealAction(Action &a)
{
	using namespace RelayProtocol;
	if (a.kind != Action::REPLY)
	{
		return; // NONE; FORWARD is the in-place zero-copy path: data() stamped the (unauthenticated) target's envelope
	}
	const auto it = m_bindings.find(a.to.key());
	if (it != m_bindings.end())
	{
		Game &g = m_games.at(it->second.game);
		PeerAuth &auth = bindingAuth(it->second);
		const std::uint8_t envelope = it->second.spectator ? g.spectators[it->second.slot].envelope : g.slots[it->second.slot].envelope;
		if (auth.authed && (!m_helloAuth.active || (auth.keys.r2c == m_helloAuth.keys.r2c)))
		{
			RelayAuth::seal(a.reply, auth.keys.r2c, auth.sendCounter++);
			return;
		}
		if (!auth.authed && !m_helloAuth.active)
		{
			a.reply[2] = envelope; // ONLINE16-FIX: the previous release reads version 3 only
			return;
		}
	}
	if (m_helloAuth.active)
	{
		// the answer of a verified HELLO that bound nothing with its keys (a refusal): sealed with the HELLO's own keys and a
		// counter the client's window has not seen (AUTH-1 review 5: never a fixed 0)
		RelayAuth::seal(a.reply, m_helloAuth.keys.r2c, nextCounterBase());
		return;
	}
	a.reply[2] = m_inVersion == kVersionLegacy ? kVersionLegacy : kVersion;
}

RelayCore::Action RelayCore::hello(const RelayAddress &from, const std::uint8_t *d, size_t size, std::uint64_t nowMs, std::uint32_t nowUnix)
{
	using namespace RelayProtocol;
	++m_stats.hellos;
	Ticket t;
	RefuseReason why = REFUSE_BAD_TICKET;
	const size_t body = size >= kHeaderBytes ? size - kHeaderBytes : 0;
	const size_t ticketBytes = body >= 1 ? ticketSize(d[kHeaderBytes]) : 0;
	// lane NET-SYNC: the v4 HELLO ends with the client's capability bits; the optional session token precedes them.
	// ONLINE16-FIX: the previous release's v3 HELLO has no capability byte (it declares none)
	const size_t capsBytes = m_inVersion == kVersionLegacy ? 0 : 1;
	const size_t tail = body >= ticketBytes ? body - ticketBytes : (size_t)-1;
	if (ticketBytes == 0 || (tail != capsBytes && tail != kSessionTokenBytes + capsBytes)
		|| !verifyTicket(d + kHeaderBytes, ticketBytes, m_options.keys, nowUnix, t, nullptr, &why))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(why));
	}
	const std::uint8_t *token = tail == kSessionTokenBytes + capsBytes ? d + kHeaderBytes + ticketBytes : nullptr;
	const std::uint8_t caps = capsBytes ? d[kHeaderBytes + ticketBytes + (tail == 1 ? 0 : kSessionTokenBytes)] : 0;
	const std::uint8_t envelope = m_inVersion == kVersionLegacy ? kVersionLegacy : kVersion;
	// lane AUTH-1: a ticket whose signed mode requires the authenticated envelope is refused plain before the game
	// is even opened (REFUSE_AUTH_REQUIRED; the lobby only signs this when its START also carried the macKey)
	if (!m_inAuthed && (t.mode & TICKET_MODE_AUTH))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_AUTH_REQUIRED));
	}
	if (m_tombstones.count(t.gameId))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_GAME_CLOSED));
	}
	auto it = m_games.find(t.gameId);
	if (it == m_games.end())
	{
		if (t.spectator)
		{
			// online batch r3 (Sol's delta review): only a player's HELLO opens a game: its envelope decides the log's format (version 2 / 4) and
			// its ticket the mode. A spectator early (or re-HELLOing first after a relay restart) is NOT_BOUND, which every release's Transport
			// answers with another HELLO (at most one per helloMs; v0.4.11 / v0.4.12 Transport.cpp handleRelay) until a player has opened it
			++m_stats.notBound;
			return reply(from, encodeEmpty(NOT_BOUND));
		}
		if (!t.spectator && t.generation > 0)
		{
			// a rejoin ticket cannot start a game: nothing exists to resume (refused before a game, a log handle or a maxGames slot is taken)
			++m_stats.refused;
			return reply(from, encodeRefused(REFUSE_REJOIN_BEFORE_BIND));
		}
		if (m_games.size() >= m_options.maxGames)
		{
			++m_stats.refused;
			return reply(from, encodeRefused(REFUSE_FULL));
		}
		it = m_games.emplace(t.gameId, Game()).first;
		Game &g = it->second;
		if (m_options.metrics)
		{
			g.health = std::make_unique<RelayHealth>(); // lane NETDIAG-1: fixed-size, allocated once per game
		}
		g.id = t.gameId;
		g.mask = t.slotMask;
		g.openerSlot = t.slot; // a player ticket (spectators open no game): online batch r4's sidecar names it
		g.startMs = nowMs;
		g.lastHeard = nowMs;
		++m_stats.gamesOpened;
		if (!m_options.logDir.empty())
		{
			g.logPath = m_options.logDir + "/" + hex64(g.id) + ".obfmerlog";
			g.log = std::fopen((g.logPath + ".part").c_str(), "wb");
			if (!g.log)
			{
				++m_stats.logErrors;
				error("cannot write " + g.logPath + ".part: game " + hex64(g.id) + " has no log");
			}
			else
			{
				std::vector<std::uint8_t> h = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
				// lane ONLINE-3: version 2 (the REJOINED record); version 4 (ONLINE16-FIX + ANTICHEAT-1, the relay lead's and the anti-cheat lane's
				// agreement): the u16 slot mask and the INPUT records (kind 7), for every current game, eight slots or fewer too. Online batch (Sol,
				// blocking 2): a game the previous release opened (a player's version-3 HELLO; spectators open no game) keeps version 2, the log
				// its clients' rejoin catch-up and spectating read (v0.4.11 / v0.4.12 read 1 and 2) and the previous lobby's upload takes; its
				// clients hold no AUTH-1 key to count. Never a canonical ticket: the previous release is refused by capability, kind 6 needs 4
				g.legacyLog = m_inVersion == RelayProtocol::kVersionLegacy && g.mask <= 0xff && !(t.mode & RelayProtocol::TICKET_MODE_CANONICAL);
				put32(h, g.legacyLog ? 2 : 4);
				put64(h, g.id);
				put8(h, (std::uint8_t)g.mask);
				if (!g.legacyLog)
				{
					put8(h, (std::uint8_t)(g.mask >> 8));
				}
				put32(h, nowUnix);
				write(g, h);
				g.logOpened = true;
				g.headerBytes = g.logBytes;
			}
		}
	}
	Game &g = it->second;
	if (t.slotMask != g.mask)
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_BAD_TICKET)); // tickets of one game agree on its slots
	}
	// lane AUTH-1: a game whose signed mode requires authenticated datagrams refuses a plain HELLO — also the
	// rejoin and spectator tickets, which carry no mode field of their own and can only reach a declared game
	if (!m_inAuthed && (g.mode & TICKET_MODE_AUTH))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_AUTH_REQUIRED));
	}
	// lane NET-SYNC: the game's mode is a property of the game, fixed by its first player START ticket; a player ticket that
	// disagrees is refused, and every HELLO into a canonical game must declare the canonical capability (rejoin and spectator
	// tickets carry no mode: they join whatever the game already is)
	if (!t.spectator && t.generation == 0)
	{
		if (!g.modeDeclared)
		{
			g.mode = t.mode;
			g.modeDeclared = true;
			if (g.mode & TICKET_MODE_CANONICAL)
			{
				// ROUTER-1: the canonical game's authority — scheduler + builder + channels, durable through the game
				// log (the source branch's fsync journal is dropped: the game log already stores every frame command).
				// It needs logDir: without a log a canonical game cannot exist.
				if (m_options.logDir.empty())
				{
					++m_stats.refused;
					m_errors.push_back("canonical game " + hex64(g.id) + ": the relay keeps no game log (logDir unset)");
					m_games.erase(t.gameId);
					return reply(from, encodeRefused(REFUSE_VERSION));
				}
				RelayCanonicalAuthority::Options ao;
				ao.game = g.id;
				ao.epoch = 1; // the fencing generation; failover reserved
				ao.slotMask = g.mask;
				ao.policy = (g.mode & TICKET_MODE_EQUAL_DELAY) ? RelayFrameScheduler::Policy::EqualDelay
					: RelayFrameScheduler::Policy::NextFrame;
				ao.clockOriginMs = nowMs;
				ao.frameMs = t.frameMs
					? std::max<std::uint64_t>(RelayFrameClock::kMinFrameMs, std::min<std::uint64_t>(RelayFrameClock::kMaxFrameMs, t.frameMs))
					: RelayFrameClock::kFrameMs; // the game's own period from its ticket (ROUTER-1)
				ao.channel.gameId = g.id;
				ao.channel.epoch = ao.epoch;
				ao.append = [this, gameId = g.id](const RelayCommit::Record &record, std::string &error) -> bool {
					auto it = m_games.find(gameId);
					if (it == m_games.end())
					{
						error = "canonical game " + hex64(gameId) + " is gone";
						return false;
					}
					return canonicalAppend(it->second, record, error);
				};
				g.canonical = std::make_unique<RelayCanonicalAuthority>(ao);
				if (g.canonical->failed())
				{
					m_errors.push_back("canonical game " + hex64(g.id) + ": " + g.canonical->failure());
				}
			}
		}
		else if (t.mode != g.mode && t.generation == 0 && !t.spectator)
		{
			// ROUTER-1: rejoin (version 2/5) and spectator (version 3/6) tickets carry no mode field — they can only
			// reach an already-declared game (the lobby issues them for started games only; a rejoin needs an
			// ever-bound slot), so they cannot mix modes; the WELCOME's epoch tail tells the client the game's mode
			++m_stats.refused;
			return reply(from, encodeRefused(REFUSE_MODE_MIXED));
		}
	}
	if ((g.mode & TICKET_MODE_CANONICAL) && !(caps & CAP_CANONICAL))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_CAPABILITY));
	}
	if (t.spectator)
	{
		return spectatorHello(g, t, from, token, nowMs, envelope); // lane ONLINE-3
	}
	Slot &s = g.slots[t.slot];
	if (!s.everBound && t.generation > 0)
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_REJOIN_BEFORE_BIND));
	}
	// ONLINE16-FIX (AUTH-1 review 3): a slot that authenticated once is never bound by a plain HELLO again — not with its
	// session token (visible in the clear WELCOME), not with a rejoin ticket: its client holds a key and seals
	if (!m_inAuthed && s.auth.authed)
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_AUTH_REQUIRED));
	}
	// AUTH-1 review 2: a HELLO sealed with the slot's own keys whose counter the slot's window admitted as fresh proves
	// it comes from the key's holder — a captured one is spent in that window — so it moves the slot like the token does
	const bool proven = m_inAuthed && m_helloAuth.windowed;
	std::uint8_t event = 0;
	// lane ONLINE-3: a rejoin ticket of a newer generation: the slot's game restarted and its player got a fresh ticket from the lobby. The slot moves to
	// this address without the session token (the old process is gone with it) and starts as a new incarnation; an older generation is stale
	if (t.generation < s.generation)
	{
		++m_stats.refused;
		++m_stats.staleTickets;
		return reply(from, encodeRefused(REFUSE_STALE));
	}
	if (s.everBound && t.generation > s.generation)
	{
		if (s.bound)
		{
			eraseBinding(s.address, Binding{ g.id, t.slot, false });
		}
		releaseEndpoint(from, Binding{ g.id, t.slot, false });
		for (int o = 0; o < (int)RelayProtocol::kMaxSlots; ++o)
		{
			resetStream(g, t.slot, o);
			resetStream(g, o, t.slot);
		}
		clearSenderLogged(g, t.slot);
		g.senderStopped[t.slot] = false; // its new incarnation's streams start afresh
		newSessionToken(s.session);
		s.generation = t.generation;
		s.bound = true;
		s.bye = false;
		s.address = from;
		s.lastHeard = nowMs;
		s.envelope = envelope;
		m_bindings[from.key()] = Binding{ g.id, t.slot };
		if (m_inAuthed) // lane AUTH-1: the rejoin ticket's fresh generation carries its own k and counters
		{
			installAuth(s.auth, m_helloAuth.keys, t.generation, m_helloAuth.counter);
			noteAuthenticated(g);
		}
		++m_stats.rejoins;
		if (g.log)
		{
			std::vector<std::uint8_t> r;
			put8(r, 5);
			put8(r, t.slot);
			put8(r, (std::uint8_t)(t.generation & 0xFF));
			put8(r, (std::uint8_t)(t.generation >> 8));
			put32(r, (std::uint32_t)(nowMs - g.startMs));
			write(g, r);
		}
		g.lastHeard = nowMs;
		++m_stats.welcomes;
		if (g.canonical)
		{
			std::string bindError;
			if (!g.canonical->bind(t.slot, t.generation, /*rejoin*/ true, bindError)) m_errors.push_back("canonical bind: " + bindError);
		}
		return reply(from, encodeWelcome(g.id, t.slot, s.session, g.canonical ? 1 : 0));
	}
	if (!s.bound || !(s.address == from))
	{
		// ONLINE-2 r2 (Sol's takeover): once a slot was bound, the ticket alone never moves it: only its session token (sent to the first address in
		// WELCOME) proves the HELLO comes from the slot's own client, now behind a new address (a NAT rebinding)
		if (s.everBound && !proven && (!token || !ConstantTimeEqual(token, s.session.data(), kSessionTokenBytes)))
		{
			++m_stats.refused;
			++m_stats.takeoversRefused;
			return reply(from, encodeRefused(REFUSE_SLOT_TAKEN));
		}
		// the address leaves whatever it was bound to (a player slot or a spectator place, of this game or another); the slot leaves its old address
		releaseEndpoint(from, Binding{ g.id, t.slot, false });
		if (s.bound)
		{
			eraseBinding(s.address, Binding{ g.id, t.slot, false });
		}
		if (!s.everBound)
		{
			// the first bind: the session token, and the full burst budget. A rebind keeps the budget it has (Sol: rebinding refilled the burst)
			newSessionToken(s.session);
			s.budgetMilli = (std::uint64_t)m_options.burstBytes * 1000u;
			s.lastRefill = nowMs;
		}
		event = s.everBound ? 2 : 1;
		s.generation = t.generation;
		s.bound = true;
		s.everBound = true;
		s.bye = false;
		s.address = from;
		m_bindings[from.key()] = Binding{ g.id, t.slot };
		if (m_inAuthed) // lane AUTH-1
		{
			installAuth(s.auth, m_helloAuth.keys, t.generation, m_helloAuth.counter);
			noteAuthenticated(g);
		}
	}
	if (event && g.log)
	{
		std::vector<std::uint8_t> r;
		put8(r, 2);
		put8(r, t.slot);
		put8(r, event);
		put32(r, (std::uint32_t)(nowMs - g.startMs));
		write(g, r);
	}
	g.lastHeard = nowMs;
	s.lastHeard = nowMs; // lane ONLINE-3
	s.envelope = envelope;
	++m_stats.welcomes;
	if (g.canonical)
	{
		std::string bindError;
		if (!g.canonical->bind(t.slot, t.generation, /*rejoin*/ false, bindError))
		{
			m_errors.push_back("canonical bind: " + bindError);
		}
	}
	return reply(from, encodeWelcome(g.id, t.slot, s.session, g.canonical ? 1 : 0));
}

RelayCore::Action RelayCore::spectatorHello(Game &g, const RelayProtocol::Ticket &t, const RelayAddress &from, const std::uint8_t *token, std::uint64_t nowMs,
	std::uint8_t envelope)
{
	// lane ONLINE-3: a spectator binds like a slot (its session token moves it to a new address; the ticket alone does not take a bound index)
	using namespace RelayProtocol;
	Spectator &sp = g.spectators[t.slot];
	if (!m_inAuthed && sp.auth.authed)
	{
		++m_stats.refused; // ONLINE16-FIX (AUTH-1 review 3), as for a player slot
		return reply(from, encodeRefused(REFUSE_AUTH_REQUIRED));
	}
	const bool proven = m_inAuthed && m_helloAuth.windowed; // AUTH-1 review 2
	if (sp.bound && !(sp.address == from) && !proven && (!token || !ConstantTimeEqual(token, sp.session.data(), kSessionTokenBytes)))
	{
		++m_stats.refused;
		++m_stats.takeoversRefused;
		return reply(from, encodeRefused(REFUSE_SLOT_TAKEN));
	}
	if (!sp.bound || !(sp.address == from))
	{
		const auto old = m_bindings.find(from.key());
		if (old != m_bindings.end())
		{
			++m_stats.refused; // an address bound to a player slot or another spectator stays what it is
			return reply(from, encodeRefused(REFUSE_SLOT_TAKEN));
		}
		if (sp.bound)
		{
			eraseBinding(sp.address, Binding{ g.id, t.slot, true });
		}
		if (!sp.everBound)
		{
			newSessionToken(sp.session);
			sp.everBound = true;
		}
		sp.bound = true;
		sp.address = from;
		sp.delayFrames = (std::uint32_t)t.delaySeconds * 5u; // LOGICFRAMES_PER_SECOND (RW 0xD9F608)
		m_bindings[from.key()] = Binding{ g.id, t.slot, true };
		if (m_inAuthed) // lane AUTH-1: relay->spectator only; a spectator sends no commands
		{
			installAuth(sp.auth, m_helloAuth.keys, 0, m_helloAuth.counter);
		}
		++m_stats.spectatorsBound;
	}
	sp.lastHeard = nowMs;
	sp.envelope = envelope;
	++m_stats.welcomes;
	return reply(from, encodeWelcome(g.id, (std::uint8_t)(kSpectatorWelcome + t.slot), sp.session));
}

void RelayCore::unbindSpectator(Game &g, int index)
{
	Spectator &sp = g.spectators[(size_t)index];
	if (sp.bound)
	{
		eraseBinding(sp.address, Binding{ g.id, (std::uint8_t)index, true });
		sp.bound = false;
	}
}

void RelayCore::eraseBinding(const RelayAddress &address, const Binding &which)
{
	// lane ONLINE-3 (Sol r1 of ONLINE-3: a spectator's timeout erased the binding its address had since taken in another game): an address's binding is
	// removed only while it is still the one named
	const auto it = m_bindings.find(address.key());
	if (it != m_bindings.end() && it->second.game == which.game && it->second.slot == which.slot && it->second.spectator == which.spectator)
	{
		m_bindings.erase(it);
	}
}

void RelayCore::releaseEndpoint(const RelayAddress &from, const Binding &keep)
{
	// lane ONLINE-3: an address that binds to `keep` leaves its prior binding by kind; spectator identity is distinguished by Binding::spectator, not slot index
	const auto it = m_bindings.find(from.key());
	if (it == m_bindings.end() || (it->second.game == keep.game && it->second.slot == keep.slot && it->second.spectator == keep.spectator))
	{
		return;
	}
	const auto og = m_games.find(it->second.game);
	if (og != m_games.end())
	{
		if (it->second.spectator)
		{
			Spectator &sp = og->second.spectators[it->second.slot];
			if (sp.bound && sp.address == from)
			{
				sp.bound = false;
			}
		}
		else
		{
			Slot &old = og->second.slots[it->second.slot];
			if (old.bound && old.address == from)
			{
				old.bound = false;
			}
		}
	}
	m_bindings.erase(it);
}

// ADMINFIX-1 (Sol adminfix1-watch): a spectator of a finished game is done once it had its own delay plus the grace
bool RelayCore::spectatorExpired(const Game &g, const Spectator &sp, std::uint64_t nowMs) const
{
	return g.closed && nowMs - g.closedMs >= (std::uint64_t)sp.delayFrames * 200u + m_options.spectatorGraceMs;
}

std::uint64_t RelayCore::spectatorLimit(const Game &g, const Spectator &sp, std::uint64_t nowMs) const
{
	// lane ONLINE-3: the log up to the last record whose frames are all at least `delayFrames` behind the players' latest announced frame (after the game
	// ended, that frame goes on at 5 a second, so the end arrives after the delay too)
	if (sp.delayFrames == 0)
	{
		return g.logBytes;
	}
	std::uint64_t front = g.maxFrame;
	if (g.closed)
	{
		front += (nowMs - g.closedMs) / 200u;
	}
	if (front < sp.delayFrames)
	{
		return g.headerBytes;
	}
	const std::uint64_t cutoff = front - sp.delayFrames;
	if (cutoff >= g.maxFrame)
	{
		return g.logBytes;
	}
	// the frontier is ordered by both members: the last entry whose frame is at most the cut-off (Sol r1 of ONLINE-3: a lookup, not a scan per read)
	const auto it = std::upper_bound(g.frontier.begin(), g.frontier.end(), cutoff,
		[](std::uint64_t c, const std::pair<std::uint64_t, std::uint32_t> &e) { return c < e.second; });
	return it == g.frontier.begin() ? g.headerBytes : std::prev(it)->first;
}

void RelayCore::clearTargetCounts(Game &g, int sender, int target, Stream &st)
{
	SenderFrameCounts &counts = g.commandCounts[(size_t)sender][(size_t)target / kCountColumns];
	const size_t column = (size_t)target % kCountColumns;
	size_t released = 0;
	for (auto frame = counts.begin(); frame != counts.end();)
	{
		FrameCounts &fc = frame->second;
		const std::uint32_t distinct = fc.distinctByTarget[column];
		if (distinct != 0)
		{
			const size_t logical = (size_t)distinct * RelayCore::kMetaNodeBytes + RelayCore::kMetaNodeBytes;
			st.metaBytes = st.metaBytes >= logical ? st.metaBytes - logical : 0;
		}
		fc.distinctByTarget[column] = 0;
		for (auto command = fc.commands.begin(); command != fc.commands.end();)
		{
			command->second.carried[column] = 0;
			bool carried = false;
			for (std::uint32_t count : command->second.carried)
			{
				carried = carried || count != 0;
			}
			if (command->second.logged == 0 && !carried)
			{
				released += RelayCore::kCommandCountsNodeBytes;
				command = fc.commands.erase(command);
			}
			else
			{
				++command;
			}
		}
		if (fc.commands.empty())
		{
			released += RelayCore::kFrameCountsNodeBytes;
			frame = counts.erase(frame);
		}
		else
		{
			++frame;
		}
	}
	st.metaBytes = 0;
	if (released != 0)
	{
		accountMeta(g, sender, -(std::ptrdiff_t)released);
	}
}

void RelayCore::clearSenderLogged(Game &g, int sender)
{
	size_t released = 0;
	for (SenderFrameCounts &counts : g.commandCounts[(size_t)sender])
	{
		for (auto frame = counts.begin(); frame != counts.end();)
		{
			FrameCounts &fc = frame->second;
			for (auto command = fc.commands.begin(); command != fc.commands.end();)
			{
				command->second.logged = 0;
				bool carried = false;
				for (std::uint32_t count : command->second.carried)
				{
					carried = carried || count != 0;
				}
				if (!carried)
				{
					released += RelayCore::kCommandCountsNodeBytes;
					command = fc.commands.erase(command);
				}
				else
				{
					++command;
				}
			}
			if (fc.commands.empty())
			{
				released += RelayCore::kFrameCountsNodeBytes;
				frame = counts.erase(frame);
			}
			else
			{
				++frame;
			}
		}
	}
	if (released != 0)
	{
		accountMeta(g, sender, -(std::ptrdiff_t)released);
	}
}

void RelayCore::clearSenderCounts(Game &g, int sender)
{
	size_t released = 0;
	for (SenderFrameCounts &counts : g.commandCounts[(size_t)sender])
	{
		for (const auto &frame : counts)
		{
			released += RelayCore::kFrameCountsNodeBytes + frame.second.commands.size() * RelayCore::kCommandCountsNodeBytes;
		}
		counts.clear();
	}
	for (Stream &st : g.streams[(size_t)sender])
	{
		st.metaBytes = 0;
	}
	if (released != 0)
	{
		accountMeta(g, sender, -(std::ptrdiff_t)released);
	}
}

void RelayCore::trimSenderCounts(Game &g, int sender)
{
	auto &halves = g.commandCounts[(size_t)sender];
	size_t released = 0;
	// the halves trim as one table: the span over every frame value either holds decides when the earliest goes
	for (;;)
	{
		const bool lowEmpty = halves[0].empty(), highEmpty = halves[1].empty();
		if (lowEmpty && highEmpty)
		{
			break;
		}
		const std::uint32_t first = lowEmpty ? halves[1].begin()->first
			: highEmpty ? halves[0].begin()->first
			: std::min(halves[0].begin()->first, halves[1].begin()->first);
		const std::uint32_t last = lowEmpty ? halves[1].rbegin()->first
			: highEmpty ? halves[0].rbegin()->first
			: std::max(halves[0].rbegin()->first, halves[1].rbegin()->first);
		if (last - first <= kKeepFrames)
		{
			break;
		}
		for (size_t half = 0; half < halves.size(); ++half)
		{
			SenderFrameCounts &counts = halves[half];
			const auto frame = counts.find(first);
			if (frame == counts.end())
			{
				continue;
			}
			FrameCounts &fc = frame->second;
			for (size_t column = 0; column < kCountColumns; ++column)
			{
				const std::uint32_t distinct = fc.distinctByTarget[column];
				if (distinct != 0)
				{
					Stream &st = g.streams[(size_t)sender][half * kCountColumns + column];
					const size_t logical = (size_t)distinct * RelayCore::kMetaNodeBytes + RelayCore::kMetaNodeBytes;
					st.metaBytes = st.metaBytes >= logical ? st.metaBytes - logical : 0;
				}
			}
			released += RelayCore::kFrameCountsNodeBytes + fc.commands.size() * RelayCore::kCommandCountsNodeBytes;
			counts.erase(frame);
		}
	}
	if (released != 0)
	{
		accountMeta(g, sender, -(std::ptrdiff_t)released);
	}
}

void RelayCore::resetStream(Game &g, int sender, int target)
{
	// lane ONLINE-3: a new incarnation's stream (sequence numbers from 0): what the old one held for the log is released
	if (g.health)
	{
		g.health->streams[(size_t)sender][(size_t)target] = RelayHealth::Stream(); // lane NETDIAG-1: a rejoin resets the stream's timing history
	}
	Stream &st = g.streams[(size_t)sender][(size_t)target];
	clearTargetCounts(g, sender, target, st);
	const size_t was = held(st);
	st = Stream();
	account(g, sender, was, st);
}

RelayCore::Action RelayCore::logRequest(const RelayAddress &from, const std::uint8_t *d, size_t size, std::uint64_t nowMs)
{
	// lane ONLINE-3: one LOG_CHUNK of the game's log for a bound client of the game (a rejoining client re-simulates the game from it)
	using namespace RelayProtocol;
	const auto b = m_bindings.find(from.key());
	if (b == m_bindings.end())
	{
		++m_stats.notBound;
		return reply(from, encodeEmpty(NOT_BOUND));
	}
	if (size != kHeaderBytes + 4)
	{
		++m_stats.badDatagrams;
		return Action();
	}
	Game &g = m_games.at(b->second.game);
	Spectator *spectator = b->second.spectator ? &g.spectators[b->second.slot] : nullptr;
	if (spectator && spectatorExpired(g, *spectator, nowMs))
	{
		// ADMINFIX-1 (Sol adminfix1-watch): after the game ends each spectator has its own delay plus the grace, never the longest one's: another
		// spectator (the admin's silent watch, 120 s behind) must not keep a public spectator's binding alive (that would reveal the watch)
		unbindSpectator(g, b->second.slot);
		++m_stats.notBound;
		return reply(from, encodeEmpty(NOT_BOUND));
	}
	Slot dummy;
	Slot &s = spectator ? dummy : g.slots[b->second.slot];
	if (spectator)
	{
		spectator->lastHeard = nowMs; // lane ONLINE-3: a spectator's reads keep it bound (never the game: spectators do not keep a game open)
	}
	else
	{
		s.lastHeard = nowMs;
		g.lastHeard = nowMs;
	}
	if (!g.logOpened || (!g.log && !g.closed))
	{
		++m_stats.logRefused;
		return reply(from, encodeRefused(REFUSE_NO_LOG));
	}
	const std::uint32_t offset = get32(d + kHeaderBytes);
	const std::uint64_t limit = spectator ? spectatorLimit(g, *spectator, nowMs) : g.logBytes; // lane ONLINE-3: a spectator's delay
	const size_t want = offset < limit ? (size_t)std::min<std::uint64_t>(kLogChunkBytes, limit - offset) : 0;
	// the log budgets (token buckets in milli-bytes): the client's own, so a rejoin's catch-up does not eat the slot's game traffic budget, and (Sol r1
	// of ONLINE-3) the game's and the relay's, all checked before the disk is touched
	auto refill = [nowMs](std::uint64_t &budget, std::uint64_t &lastRefill, std::uint64_t rate, std::uint64_t burstBytes) {
		const std::uint64_t cap = burstBytes * 1000u;
		if (lastRefill == 0)
		{
			budget = cap;
			lastRefill = nowMs;
		}
		const std::uint64_t elapsed = nowMs > lastRefill ? nowMs - lastRefill : 0;
		const std::uint64_t add = rate == 0 ? 0 : elapsed >= cap / rate + 1 ? cap : elapsed * rate;
		budget = std::min<std::uint64_t>(cap, budget + std::min<std::uint64_t>(cap, add));
		lastRefill = nowMs;
	};
	std::uint64_t &budget = spectator ? spectator->logBudgetMilli : s.logBudgetMilli;
	refill(budget, spectator ? spectator->logRefill : s.logRefill, m_options.logBytesPerSecond, m_options.logBurstBytes);
	refill(g.logReadBudgetMilli, g.logReadRefill, m_options.logGameBytesPerSecond, m_options.logGameBurstBytes);
	refill(m_logReadBudgetMilli, m_logReadRefill, m_options.logTotalBytesPerSecond, m_options.logTotalBurstBytes);
	const std::uint64_t cost = (std::uint64_t)(want + kHeaderBytes + 8) * 1000u;
	if (cost > budget || cost > g.logReadBudgetMilli || cost > m_logReadBudgetMilli)
	{
		++m_stats.logRateLimited;
		return Action();
	}
	budget -= cost;
	g.logReadBudgetMilli -= cost;
	m_logReadBudgetMilli -= cost;
	if (g.log)
	{
		std::fflush(g.log);
	}
	if (!g.logReader)
	{
		g.logReader = std::fopen((g.logRenamed ? g.logPath : g.logPath + ".part").c_str(), "rb");
		if (!g.logReader)
		{
			++m_stats.logErrors;
			error("cannot read " + g.logPath + ".part back for a LOG_REQUEST");
			++m_stats.logRefused;
			return reply(from, encodeRefused(REFUSE_NO_LOG));
		}
	}
	std::uint8_t buffer[kLogChunkBytes];
	size_t n = 0;
	if (want > 0)
	{
		std::clearerr(g.logReader);
		if (std::fseek(g.logReader, (long)offset, SEEK_SET) == 0)
		{
			n = std::fread(buffer, 1, want, g.logReader);
		}
	}
	++m_stats.logChunksSent;
	m_stats.logBytesSent += n;
	if (spectator)
	{
		m_stats.spectatorLogBytes += n;
	}
	return reply(from, encodeLogChunk(offset, (std::uint32_t)limit, buffer, n));
}

void RelayCore::newSessionToken(std::array<std::uint8_t, RelayProtocol::kSessionTokenBytes> &out)
{
	for (size_t i = 0; i < out.size(); i += 4)
	{
		const std::uint32_t v = (std::uint32_t)m_random(); // std::random_device: the operating system's generator
		for (size_t k = 0; k < 4; ++k)
		{
			out[i + k] = (std::uint8_t)(v >> (8 * k));
		}
	}
}

RelayCore::Action RelayCore::data(const RelayAddress &from, std::uint8_t *d, size_t size, std::uint64_t nowMs)
{
	using namespace RelayProtocol;
	const auto b = m_bindings.find(from.key());
	if (b == m_bindings.end())
	{
		++m_stats.notBound;
		return reply(from, encodeEmpty(NOT_BOUND));
	}
	Game &g = m_games.at(b->second.game);
	if (b->second.spectator)
	{
		// lane ONLINE-3: a spectator is read-only: whatever it sends is no command of the game (never logged, never forwarded)
		++m_stats.spectatorDataRefused;
		g.spectators[b->second.slot].lastHeard = nowMs;
		return reply(from, encodeRefused(REFUSE_SPECTATOR));
	}
	const int sender = b->second.slot;
	Slot &s = g.slots[(size_t)sender];
	g.lastHeard = nowMs;
	s.lastHeard = nowMs; // lane ONLINE-3
	++(m_inAuthed ? s.inVerified : s.inPlain); // lane ANTICHEAT-1: the slot's DATA as AUTH-1 received it (D7)
	++s.datagramsIn;
	s.bytesIn += size;
	// the per-slot budget: a token bucket in milli-bytes, integers only (Sol r1); bytesPerSecond B/s over n ms adds n * bytesPerSecond milli-bytes
	{
		const std::uint64_t cap = (std::uint64_t)m_options.burstBytes * 1000u;
		const std::uint64_t elapsed = nowMs > s.lastRefill ? nowMs - s.lastRefill : 0;
		// ONLINE-2 r3 (Sol r2): rate 0 never refills (round 2 refilled a depleted bucket after burstBytes x 1000 ms); else elapsed * rate, capped
		// before it can overflow
		const std::uint64_t rate = m_options.bytesPerSecond;
		const std::uint64_t refill = rate == 0 ? 0 : elapsed >= cap / rate + 1 ? cap : elapsed * rate;
		s.budgetMilli = std::min<std::uint64_t>(cap, s.budgetMilli + std::min<std::uint64_t>(cap, refill));
		s.lastRefill = nowMs;
		const std::uint64_t cost = (std::uint64_t)size * 1000u;
		if (cost > s.budgetMilli)
		{
			noteDataDrop(g, s);
			++m_stats.rateLimited;
			return Action();
		}
		s.budgetMilli -= cost;
	}
	const size_t at = kHeaderBytes + 1;
	if (g.canonical && size > at + 1 && d[at] == 'N' && d[at + 1] == 'S')
	{
		// lane NET-SYNC: the canonical game's frame input is the slot's NS channel packet (its own envelope byte, not a
		// transport packet, never routed to other players). Emissions past the first queue for the caller.
		std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> emissions;
		g.canonical->receive(sender, d + at, size - at, nowMs, emissions);
		std::vector<Action> extra;
		emitCanonical(g, emissions, extra);
		Action first;
		if (!extra.empty())
		{
			first = std::move(extra.front());
			for (size_t i = 1; i < extra.size(); ++i) m_pending.push_back(std::move(extra[i]));
		}
		return first;
	}
	// lane CHAT-1: in a canonical game a DATA that does not start with the NS magic is a direct command on the legacy
	// stream (chat, the disconnect-path keepalives and screen messages): forwarded to the addressed slot like a
	// lockstep game's — validated, never logged into the canonical log, never put into a bundle.
	if (size < at + kPacketHeader)
	{
		noteDataDrop(g, s);
		++s.inMalformed; // lane ANTICHEAT-1
		++m_stats.badDatagrams;
		return Action();
	}
	const int target = d[kHeaderBytes];
	const std::uint8_t *packet = d + at;
	if (packet[7] != (std::uint8_t)sender)
	{
		noteDataDrop(g, s);
		++s.inSlotMismatch; // lane ANTICHEAT-1
		++m_stats.slotMismatches; // the slot check by session: the packet speaks for another slot than its ticket's
		return Action();
	}
	if (target == sender && target < RelayProtocol::kMaxSlots && !g.canonical)
	{
		// lane ONLINE-3: the sender's log-only stream (its frame commands that reach no other player: a 1v1 whose other player is held): logged, never
		// forwarded, acknowledged by the relay as the sender's own slot (an ack-only transport packet in DATA_FROM)
		if (!follow(g, sender, sender, packet, size - at, nowMs))
		{
			noteDataDrop(g, s);
			++s.inMalformed; // lane ANTICHEAT-1
			++m_stats.invalidPackets;
			return Action();
		}
		observePacket(g, sender, sender, packet, nowMs); // lane NETDIAG-1: the unwrapped packet (AUTH-1 opened it in handle)
		++m_stats.logStreamPackets;
		std::vector<std::uint8_t> ack(4, 0);
		ack.push_back(0x42);
		ack.push_back(0x46);
		ack.push_back(kPacketVersion);
		ack.push_back((std::uint8_t)sender);
		put32(ack, g.streams[(size_t)sender][(size_t)sender].expected);
		ack.push_back(0);
		ack.push_back(0);
		const std::uint32_t crc = crc32Bytes(ack.data() + 4, ack.size() - 4);
		for (int i = 0; i < 4; ++i)
		{
			ack[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
		}
		std::vector<std::uint8_t> out;
		putHeader(out, DATA_FROM);
		out.push_back((std::uint8_t)sender);
		out.insert(out.end(), ack.begin(), ack.end());
		return reply(from, std::move(out));
	}
	if (target >= RelayProtocol::kMaxSlots || target == sender || !(g.mask & (1u << target)))
	{
		noteDataDrop(g, s);
		++m_stats.badTargets;
		return Action();
	}
	const Slot &t = g.slots[(size_t)target];
	// ONLINE-2 r2 (Sol: a packet with trailing bytes entered the log though clients refuse it): a packet the receiving Transport would refuse
	// whole is neither logged nor forwarded, so the log and the receivers see the same packets. The command log follows the stream even when the
	// target is not there yet (its transport resends anyway)
	if (g.canonical)
	{
		// CHAT-1: the same structural check `follow` runs, but no stream model and no log — the receiving transport
		// enforces ordering and content on its own, the canonical log holds the OBRC stream only.
		if (!validatePacket(packet, size - at, m_records)) // NETDIAG-1: the reused record list (no allocation per packet; observePacket reads it)
		{
			noteDataDrop(g, s);
			++s.inMalformed; // lane ANTICHEAT-1
			++m_stats.invalidPackets;
			return Action();
		}
	}
	else if (!follow(g, sender, target, packet, size - at, nowMs))
	{
		noteDataDrop(g, s);
		++m_stats.invalidPackets;
		return Action();
	}
	observePacket(g, sender, target, packet, nowMs); // lane NETDIAG-1: validated (and followed), the unwrapped packet, forwarded or not
	if (!t.bound)
	{
		++m_stats.unboundTargets;
		return Action();
	}
	d[2] = t.envelope; // ONLINE16-FIX: in the target's envelope (a version-3 sender to a current client, and back)
	d[3] = DATA_FROM;
	d[kHeaderBytes] = (std::uint8_t)sender;
	++m_stats.forwarded;
	m_stats.bytesForwarded += size;
	if (t.auth.authed || m_inAuthed)
	{
		// lane AUTH-1: an authenticated destination's copy is a sealed fresh buffer (the zero-copy FORWARD cannot
		// grow the input in place); sealAction seals it. Online batch (Sol, blocking 1): an authenticated sender's datagram was opened in place,
		// `size` bytes of the received ones: the zero-copy FORWARD sends the received length (relay_main), its tag too, so it is a buffer of `size`
		return reply(t.address, std::vector<std::uint8_t>(d, d + size));
	}
	Action a;
	a.kind = Action::FORWARD;
	a.to = t.address;
	return a;
}

void RelayCore::bye(const RelayAddress &from, std::uint64_t nowMs)
{
	const auto b = m_bindings.find(from.key());
	if (b == m_bindings.end())
	{
		return;
	}
	Game &g = m_games.at(b->second.game);
	if (b->second.spectator)
	{
		unbindSpectator(g, b->second.slot); // lane ONLINE-3: a spectator that leaves
		return;
	}
	Slot &s = g.slots[b->second.slot];
	if (!s.bye && g.log)
	{
		std::vector<std::uint8_t> r;
		put8(r, 2);
		put8(r, b->second.slot);
		put8(r, 3);
		put32(r, (std::uint32_t)(nowMs - g.startMs));
		write(g, r);
	}
	s.bye = true;
	if (g.canonical)
	{
		g.canonical->depart(b->second.slot);
	}
	g.lastHeard = nowMs;
	for (const Slot &o : g.slots)
	{
		if (o.everBound && !o.bye)
		{
			return;
		}
	}
	close(g, 1, nowMs);
}

bool RelayCore::validatePacket(const std::uint8_t *p, size_t n, std::vector<Record> &records)
{
	// ---- the whole packet is validated first, as Transport::handlePacket does before it applies anything: the size, the CRC, the magic, the version,
	// every record's layout, the exact end (no trailing bytes) ----
	if (n < kPacketHeader || n > m_options.maxPacketBytes || p[4] != 0x42 || p[5] != 0x46 || p[6] != kPacketVersion || crc32Bytes(p + 4, n - 4) != get32(p))
	{
		return false;
	}
	records.clear();
	const size_t count = get16(p + 12);
	size_t at = kPacketHeader;
	for (size_t c = 0; c < count; ++c)
	{
		if (n - at < kRecordHeader)
		{
			return false;
		}
		Record r;
		r.seq = get32(p + at);
		r.index = get16(p + at + 4);
		r.frags = get16(p + at + 6);
		r.len = get32(p + at + 8);
		at += kRecordHeader;
		if (r.frags == 0 || r.index >= r.frags || r.len == 0 || n - at < r.len)
		{
			return false;
		}
		r.at = at;
		at += r.len;
		records.push_back(r);
	}
	if (at != n)
	{
		return false; // trailing bytes
	}
	return true;
}

bool RelayCore::follow(Game &g, int sender, int target, const std::uint8_t *p, size_t n, std::uint64_t nowMs)
{
	std::vector<Record> &records = m_records; // reused: no allocation per packet
	if (!validatePacket(p, n, records))
	{
		return false;
	}
	const size_t nrec = records.size();
	if (!g.log && sender != target)
	{
		return true; // no log: nothing more to keep consistent (lane ONLINE-3: the log-only stream is followed anyway, its acks need it)
	}
	Stream &st = g.streams[(size_t)sender][(size_t)target];
	const Stream &reverse = g.streams[(size_t)target][(size_t)sender];
	// ---- the stateful checks of the receiver, against what the relay has seen of both directions (the receiver has seen no more: it hears only
	// through the relay): every record inside the window; the cumulative ack at most the records the target sent this sender ----
	if (!st.broken)
	{
		for (size_t i = 0; i < nrec; ++i)
		{
			if (!seqBefore(records[i].seq, st.expected + m_options.streamWindow))
			{
				return false;
			}
		}
	}
	if (!reverse.broken && (std::uint64_t)(std::uint32_t)((std::uint32_t)reverse.highest - get32(p + 8)) > reverse.highest)
	{
		return false; // an ack beyond the records sent (Transport: "the ack not beyond the sequence numbers issued")
	}
	// ---- follow: the highest sequence number seen (the reverse ack check), then the records in order ----
	for (size_t i = 0; i < nrec; ++i)
	{
		if (!seqBefore(records[i].seq, (std::uint32_t)st.highest))
		{
			st.highest += (std::uint32_t)(records[i].seq + 1 - (std::uint32_t)st.highest);
		}
	}
	if (st.broken)
	{
		return true;
	}
	// lane ONLINE-3 r2 (Sol r2 of ONLINE-3: the caps were checked after the growth): every growth of the stream (an early record, the command being
	// joined, a command's count nodes) is admitted under the stream's, the sender's, the game's and the relay's caps BEFORE it is made, and accounted at
	// once; what does not fit stops the stream's logging (a GAP record), which frees what the stream holds
	size_t accounted = held(st);
	auto sync = [&] {
		account(g, sender, accounted, st);
		accounted = held(st);
	};
	auto stop = [&](std::uint8_t reason) {
		sync();
		gap(g, sender, target, st, reason, nowMs);
		return true;
	};
	for (size_t i = 0; i < nrec; ++i)
	{
		const Record &r = records[i];
		if (!seqBefore(r.seq, st.expected) && !st.early.count(r.seq))
		{
			if (!admit(g, sender, &st, r.len + kEarlyOverhead, 0, 0))
			{
				return stop(1);
			}
			st.early[r.seq] = { { r.index, r.frags }, std::vector<std::uint8_t>(p + r.at, p + r.at + r.len) };
			st.earlyBytes += r.len + kEarlyOverhead;
			sync();
		}
	}
	for (auto it = st.early.find(st.expected); it != st.early.end(); it = st.early.find(st.expected))
	{
		const std::uint16_t index = it->second.first.first, frags = it->second.first.second;
		std::vector<std::uint8_t> bytes = std::move(it->second.second);
		st.earlyBytes -= bytes.size() + kEarlyOverhead;
		st.early.erase(it);
		++st.expected;
		if (frags == 1)
		{
			std::vector<std::uint8_t>().swap(st.partial); // ONLINE-2 r3 (Sol r2): a discarded partial command releases its allocation
			st.partialNext = st.partialCount = 0;
			sync();
			const bool admitted = command(g, sender, target, st, std::move(bytes), nowMs);
			accounted = held(st); // command() charged its own changes (Sol r3 of ONLINE-3: a second charge here double-counted or double-released)
			if (!admitted)
			{
				return stop(1);
			}
			continue;
		}
		if (st.partialCount == 0 ? index != 0 : (frags != st.partialCount || index != st.partialNext))
		{
			return stop(2);
		}
		const size_t needed = st.partial.size() + bytes.size();
		if (needed > st.partial.capacity())
		{
			// the record leaves the early records (its bytes released above) and joins the partial command, allocated exactly
			sync();
			if (!admit(g, sender, &st, needed - st.partial.capacity(), 0, 0))
			{
				return stop(1);
			}
			st.partial.reserve(needed);
		}
		st.partialCount = frags;
		st.partial.insert(st.partial.end(), bytes.begin(), bytes.end());
		sync();
		if (++st.partialNext == st.partialCount)
		{
			std::vector<std::uint8_t> whole = std::move(st.partial);
			st.partial = std::vector<std::uint8_t>();
			st.partialNext = st.partialCount = 0;
			sync();
			const bool admitted = command(g, sender, target, st, std::move(whole), nowMs);
			accounted = held(st); // command() charged its own changes
			if (!admitted)
			{
				return stop(1);
			}
		}
	}
	sync();
	return true;
}

// ONLINE-2 r3 (Sol r2: partial.clear() kept 3,600 bytes allocated with nothing accounted): what a stream holds is its early records and the
// ALLOCATED size of the command being joined (capacity, not size); discarded partial commands are released by swap, so the two agree. The command-count
// metadata is sender-owned and is charged separately as conservative map-node/storage estimates.
size_t RelayCore::held(const Stream &st)
{
	return st.earlyBytes + st.partial.capacity();
}

void RelayCore::noteDataDrop(Game &g, Slot &s)
{
	++s.dropped;
	if (m_options.metrics)
	{
		++m_dataDrops;
		if (g.health)
		{
			++g.health->dataDrops;
		}
	}
}

// lane NETDIAG-1 (from a contributor's relay-metrics change, adapted to sixteen slots and AUTH-1): the health sample of one transport packet that
// passed validatePacket (and follow, on a logged stream), read from the UNWRAPPED bytes: handle() opened a version-5 envelope in place before data()
// ran, so `p` is never sealed bytes. Its records are m_records. Passive: it changes nothing the relay forwards, logs or answers.
//   * ACK turnaround: a packet whose cumulative ack (+8) covers a pending (timed, never retransmitted: Karn) packet of the reverse stream samples
//     nowMs minus the newest such packet's send time; jitter is the change between two successive samples of one directed stream
//   * resends: records the 64-record window saw already; lateFrames: a fresh first fragment of a frame command of the sender older than the
//     stream's highest frame so far
void RelayCore::observePacket(Game &g, int sender, int target, const std::uint8_t *p, std::uint64_t nowMs)
{
	if (!g.health)
	{
		return;
	}
	RelayHealth &h = *g.health;
	RelayHealth::Stream &st = h.streams[(size_t)sender][(size_t)target];
	RelayHealth::Stream &reverse = h.streams[(size_t)target][(size_t)sender];
	const std::uint32_t ack = get32(p + 8);
	bool sampled = false;
	std::uint64_t newest = 0;
	if (sender != target && reverse.seen && !seqBefore(reverse.highest + 1, ack))
	{
		for (RelayHealth::Pending &pending : reverse.pending)
		{
			if (pending.active && !seqBefore(ack, pending.end))
			{
				if (!pending.resent && nowMs >= pending.at && (!sampled || pending.at > newest))
				{
					newest = pending.at;
					sampled = true;
				}
				pending.active = false;
			}
		}
	}
	if (sampled)
	{
		const std::uint64_t rtt = nowMs - newest;
		h.rtt.add(rtt);
		if (reverse.hasRtt)
		{
			h.jitter.add(rtt > reverse.lastRtt ? rtt - reverse.lastRtt : reverse.lastRtt - rtt);
		}
		reverse.lastRtt = rtt;
		reverse.hasRtt = true;
	}
	bool first = true, retransmission = false;
	std::uint32_t firstSeq = 0, lastSeq = 0;
	for (const Record &r : m_records)
	{
		bool duplicate = false;
		const bool fresh = st.fresh(r.seq, duplicate);
		if (duplicate)
		{
			++h.resends;
			++m_resends;
		}
		for (RelayHealth::Pending &pending : st.pending)
		{
			if (pending.active && !seqBefore(r.seq, pending.first) && seqBefore(r.seq, pending.end))
			{
				pending.resent = true;
			}
		}
		retransmission = retransmission || !fresh;
		if (first || seqBefore(r.seq, firstSeq))
		{
			firstSeq = r.seq;
		}
		if (first || seqBefore(lastSeq, r.seq))
		{
			lastSeq = r.seq;
		}
		first = false;
		// the first fragment carries the command header (u8 type, u8 slot, u32 execution frame: NetPacket.h); no payload byte is kept
		if (fresh && r.index == 0 && r.len >= 6 && isFrameCommand(p[r.at]) && p[r.at + 1] == (std::uint8_t)sender)
		{
			const std::uint32_t frame = get32(p + r.at + 2);
			if (frame < st.frame)
			{
				++h.lateFrames;
				++m_lateFrames;
			}
			st.frame = std::max(st.frame, frame);
		}
	}
	if (!first && !retransmission && sender != target && g.slots[(size_t)target].bound)
	{
		st.pending[st.next] = { firstSeq, lastSeq + 1, nowMs, true, false };
		st.next = (st.next + 1) % st.pending.size();
	}
}

std::string RelayCore::healthJson(std::uint64_t uptimeMs, std::uint64_t residentBytes)
{
	if (!m_options.metrics)
	{
		return {};
	}
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(3);
	const std::uint64_t elapsed = uptimeMs > m_healthUptime ? uptimeMs - m_healthUptime : 0;
	out << "{\"v\":1,\"event\":\"relay_health\",\"uptimeMs\":" << uptimeMs << ",\"residentBytes\":";
	if (residentBytes)
	{
		out << residentBytes;
	}
	else
	{
		out << "null";
	}
	size_t metricsBytes = 0, games = 0, players = 0, spectators = 0;
	for (const auto &[id, g] : m_games)
	{
		(void)id;
		metricsBytes += g.health ? sizeof(RelayHealth) : 0;
		games += g.closed ? 0 : 1;
		for (const Slot &sl : g.slots)
		{
			players += sl.bound && !sl.bye ? 1 : 0;
		}
		for (const Spectator &sp : g.spectators)
		{
			spectators += sp.bound ? 1 : 0;
		}
	}
	out << ",\"bufferedBytes\":" << m_buffered << ",\"metricsBytes\":" << metricsBytes << ",\"activeGames\":" << games << ",\"players\":" << players
		<< ",\"spectators\":" << spectators << ",\"packetsIn\":" << m_stats.datagrams << ",\"packetsOut\":" << m_packetsOut << ",\"sampleMs\":" << elapsed
		<< ",\"packetsInPerSecond\":" << (elapsed ? (double)(m_stats.datagrams - m_healthIn) * 1000.0 / (double)elapsed : 0.0)
		<< ",\"packetsOutPerSecond\":" << (elapsed ? (double)(m_packetsOut - m_healthOut) * 1000.0 / (double)elapsed : 0.0)
		<< ",\"sendDrops\":" << m_sendDrops << ",\"invalidDatagrams\":" << m_stats.badDatagrams << ",\"dataDrops\":" << m_dataDrops
		<< ",\"resends\":" << m_resends << ",\"lateFrames\":" << m_lateFrames << ",\"games\":[";
	bool firstGame = true;
	for (const auto &[id, g] : m_games)
	{
		if (g.closed || !g.health)
		{
			continue;
		}
		out << (firstGame ? "" : ",");
		firstGame = false;
		const RelayHealth &h = *g.health;
		size_t count = 0;
		for (const Slot &sl : g.slots)
		{
			count += sl.bound && !sl.bye ? 1 : 0;
		}
		out << "{\"game\":\"" << hex64(id) << "\",\"players\":" << count << ",\"dataDrops\":" << h.dataDrops << ",\"resends\":" << h.resends
			<< ",\"lateFrames\":" << h.lateFrames << ",\"ackRtt\":";
		h.rtt.json(out);
		out << ",\"ackJitter\":";
		h.jitter.json(out);
		out << '}';
	}
	out << "]}";
	m_healthUptime = uptimeMs;
	m_healthIn = m_stats.datagrams;
	m_healthOut = m_packetsOut;
	return out.str();
}

void RelayCore::accountMeta(Game &g, int sender, std::ptrdiff_t delta)
{
	// lane ONLINE-3 r3 (Sol r3 of ONLINE-3: a double release wrapped heldBytes to 2^64 - 2688 and blocked another game): the one way every held-bytes
	// counter changes. A release larger than what a counter holds is a bookkeeping error: it is counted and reported (the tests require none) and the
	// counter stops at 0, never wrapping
	auto apply = [&](size_t &counter, const char *what) {
		if (delta < 0 && counter < (size_t)(-delta))
		{
			++m_stats.accountingErrors;
			error(std::string("held-bytes accounting: releasing ") + std::to_string(-delta) + " bytes from the " + what + "'s " + std::to_string(counter)
				+ " (game " + hex64(g.id) + ")");
			counter = 0;
			return;
		}
		counter = (size_t)((std::ptrdiff_t)counter + delta);
	};
	if (sender >= 0)
	{
		apply(g.slots[(size_t)sender].buffered, "sender");
	}
	apply(g.buffered, "game");
	apply(m_buffered, "relay");
	m_stats.peakBuffered = std::max<unsigned long long>(m_stats.peakBuffered, m_buffered);
}

void RelayCore::noteFrontier(Game &g)
{
	// lane ONLINE-3: (bytes of the log, the highest execution frame logged up to there), one entry per frame value. Its allocation is held by the game
	// (Sol r2 of ONLINE-3: admitted before it grows, by doubling from 16 entries up to kMaxFrontier); a full frontier at kMaxFrontier keeps every second
	// entry (the last stays); one that may not grow under the caps moves its last entry on instead. Either way a delayed spectator's cut-off falls on an
	// earlier entry, never a later one
	if (!g.frontier.empty() && g.frontier.back().second == g.maxFrame)
	{
		g.frontier.back().first = g.logBytes;
		return;
	}
	if (g.frontier.size() == g.frontier.capacity())
	{
		if (g.frontier.size() >= kMaxFrontier)
		{
			size_t k = 0;
			for (size_t i = 1; i < g.frontier.size(); i += 2)
			{
				g.frontier[k++] = g.frontier[i]; // the last one (an odd index: the size is even) stays
			}
			g.frontier.resize(k);
		}
		else
		{
			const size_t entry = sizeof(g.frontier[0]), cap = g.frontier.capacity(), want = cap == 0 ? 16 : cap * 2;
			if (!admit(g, -1, nullptr, 0, 0, (want - cap) * entry))
			{
				if (!g.frontier.empty())
				{
					g.frontier.back() = { g.logBytes, g.maxFrame };
				}
				return;
			}
			g.frontier.reserve(want);
			g.frontierBytes = g.frontier.capacity() * entry;
			accountMeta(g, -1, (std::ptrdiff_t)((g.frontier.capacity() - cap) * entry));
		}
	}
	g.frontier.push_back({ g.logBytes, g.maxFrame });
}

size_t RelayCore::retainedBytes() const
{
	size_t n = 0;
	for (const auto &kv : m_games)
	{
		// lane ONLINE-3: the count nodes and the frontier, walked (held() and accountMeta count them as they change)
		for (const auto &senderCounts : kv.second.commandCounts)
		{
			for (const SenderFrameCounts &counts : senderCounts)
			{
				for (const auto &frame : counts)
				{
					n += RelayCore::kFrameCountsNodeBytes + frame.second.commands.size() * RelayCore::kCommandCountsNodeBytes;
				}
			}
		}
		n += kv.second.frontier.capacity() * sizeof(kv.second.frontier[0]);
		for (const std::vector<std::uint8_t> &m : kv.second.membershipLogged)
		{
			n += RelayCore::kMetaNodeBytes + m.size();
		}
		for (const auto &row : kv.second.streams)
		{
			for (const Stream &st : row)
			{
				n += st.earlyBytes + st.partial.capacity();
			}
		}
	}
	return n;
}

void RelayCore::account(Game &g, int sender, size_t before, const Stream &st)
{
	accountMeta(g, sender, (std::ptrdiff_t)held(st) - (std::ptrdiff_t)before);
}

bool RelayCore::command(Game &g, int sender, int target, Stream &st, std::vector<std::uint8_t> &&bytes, std::uint64_t nowMs)
{
	if (!g.log)
	{
		return true; // lane ONLINE-3 r2: no log (none kept, or it stopped): nothing is counted
	}
	if (bytes.size() >= 6 && (bytes[0] == kDisconnectPlayer || bytes[0] == kDisconnectPlayer16) && bytes[1] == (std::uint8_t)sender)
	{
		// lane ONLINE-3: the decider's DISCONNECTPLAYER goes to every other slot: logged once (the identical copies on the other streams are skipped)
		for (const std::vector<std::uint8_t> &m : g.membershipLogged)
		{
			if (m == bytes)
			{
				return true;
			}
		}
		// ONLINE16-FIX: sixteen slots, each dropping each other one per incarnation, rejoining a few times: 256 distinct records. Beyond that the
		// log stops loudly — the old silent skip (at 64, sized for eight slots) left a drop out of the replay, which then ran it wrongly
		if (g.membershipLogged.size() >= (size_t)RelayProtocol::kMaxSlots * RelayProtocol::kMaxSlots)
		{
			stopLog(g, "game " + hex64(g.id) + " sent more than 256 distinct disconnect records: its log stops here");
			return true;
		}
		// held by the game (Sol r2 of ONLINE-3): admitted first; a drop the log cannot keep would make its replay wrong, so the log stops instead
		const size_t grow = RelayCore::kMetaNodeBytes + bytes.size();
		if (!admit(g, -1, nullptr, 0, 0, grow))
		{
			stopLog(g, "a disconnect record of game " + hex64(g.id) + " is over the relay's memory caps: its log stops here");
			return true;
		}
		bytes.shrink_to_fit();
		g.membershipLogged.push_back(bytes);
		g.membershipBytes += grow;
		accountMeta(g, -1, (std::ptrdiff_t)grow);
		std::vector<std::uint8_t> r;
		put8(r, 1);
		put8(r, (std::uint8_t)sender);
		put32(r, (std::uint32_t)(nowMs - g.startMs));
		put32(r, (std::uint32_t)bytes.size());
		r.insert(r.end(), bytes.begin(), bytes.end());
		write(g, r);
		if (g.log)
		{
			++m_stats.commandsLogged;
		}
		return true;
	}
	if (bytes.size() < 6 || !isFrameCommand(bytes[0]))
	{
		return true;
	}
	if (bytes[1] != (std::uint8_t)sender)
	{
		++m_stats.foreignFrameCommands; // the receiving Network refuses it too ("sent a command claiming slot ...")
		return true;
	}
	if (g.senderStopped[(size_t)sender])
	{
		return true; // lane ONLINE-3 r2: every stream of this sender stopped: its log ended at their GAP records
	}
	const std::uint32_t frame = get32(bytes.data() + 2);
	// lane ONLINE-3: identical commands are counted together (FNV-1a over the bytes): every stream of a sender carries its commands of a frame in order,
	// but a log-only stream carries only those after it started, so the n-th command of one stream need not be the n-th of another
	std::uint64_t hash = 0xCBF29CE484222325ull;
	for (std::uint8_t b : bytes)
	{
		hash = (hash ^ b) * 0x100000001B3ull;
	}
	// A sender owns one shared frame/hash table per half of the target space. The target columns preserve the old
	// per-stream occurrence counts, while the logged maximum is shared across those columns (and mirrored between the
	// halves, so a hash is never logged twice). The halves' map nodes are admitted under the caps before they are
	// made; the stream's metaBytes track its own logical counts for the maxStreamBuffer quota, never held bytes
	SenderFrameCounts &senderCounts = g.commandCounts[(size_t)sender][(size_t)target / kCountColumns];
	const size_t column = (size_t)target % kCountColumns;
	const auto frameIt = senderCounts.find(frame);
	const bool newFrame = frameIt == senderCounts.end();
	const FrameCounts *existingFrame = newFrame ? nullptr : &frameIt->second;
	std::uint32_t priorCarried = 0;
	std::uint32_t priorDistinct = 0;
	bool newCommand = true;
	if (existingFrame != nullptr)
	{
		priorDistinct = existingFrame->distinctByTarget[column];
		const auto commandIt = existingFrame->commands.find(hash);
		if (commandIt != existingFrame->commands.end())
		{
			newCommand = false;
			priorCarried = commandIt->second.carried[column];
		}
	}
	if (priorCarried == std::numeric_limits<std::uint32_t>::max()
		|| (priorCarried == 0 && priorDistinct == std::numeric_limits<std::uint32_t>::max()))
	{
		return false;
	}
	const size_t logicalGrow = (priorCarried == 0 ? RelayCore::kMetaNodeBytes : 0)
		+ (priorDistinct == 0 ? RelayCore::kMetaNodeBytes : 0);
	const size_t physicalGrow = (newFrame ? RelayCore::kFrameCountsNodeBytes : 0)
		+ (newCommand ? RelayCore::kCommandCountsNodeBytes : 0);
	if (!admit(g, sender, &st, 0, physicalGrow, 0, logicalGrow))
	{
		return false;
	}
	if (physicalGrow != 0)
	{
		accountMeta(g, sender, (std::ptrdiff_t)physicalGrow);
	}
	FrameCounts &frameCounts = newFrame ? senderCounts.emplace(frame, FrameCounts()).first->second : frameIt->second;
	auto commandIt = frameCounts.commands.find(hash);
	if (commandIt == frameCounts.commands.end())
	{
		commandIt = frameCounts.commands.emplace(hash, CommandCounts()).first;
		// the watermark is shared across the halves: a hash the other half already logged keeps its count
		const SenderFrameCounts &sibling = g.commandCounts[(size_t)sender][((size_t)target / kCountColumns) ^ 1];
		const auto siblingFrame = sibling.find(frame);
		if (siblingFrame != sibling.end())
		{
			const auto siblingCommand = siblingFrame->second.commands.find(hash);
			if (siblingCommand != siblingFrame->second.commands.end())
			{
				commandIt->second.logged = siblingCommand->second.logged;
			}
		}
	}
	CommandCounts &counts = commandIt->second;
	const std::uint32_t carried = counts.carried[column];
	++counts.carried[column];
	if (carried == 0)
	{
		++frameCounts.distinctByTarget[column];
		st.metaBytes += logicalGrow;
	}
	st.carried = true;
	const bool fresh = counts.carried[column] > counts.logged;
	if (fresh)
	{
		counts.logged = counts.carried[column];
		// mirror the watermark into the other half's node when it holds the same command
		SenderFrameCounts &sibling = g.commandCounts[(size_t)sender][((size_t)target / kCountColumns) ^ 1];
		const auto siblingFrame = sibling.find(frame);
		if (siblingFrame != sibling.end())
		{
			const auto siblingCommand = siblingFrame->second.commands.find(hash);
			if (siblingCommand != siblingFrame->second.commands.end())
			{
				siblingCommand->second.logged = counts.logged;
			}
		}
	}
	trimSenderCounts(g, sender);
	if (!fresh)
	{
		return true; // another target's stream already brought it
	}
	g.maxFrame = std::max(g.maxFrame, frame); // lane ONLINE-3: the players' latest announced frame (a spectator's delay counts from it)
	std::vector<std::uint8_t> r;
	r.reserve(10 + bytes.size());
	put8(r, 1);
	put8(r, (std::uint8_t)sender);
	put32(r, (std::uint32_t)(nowMs - g.startMs));
	put32(r, (std::uint32_t)bytes.size());
	r.insert(r.end(), bytes.begin(), bytes.end());
	write(g, r);
	if (g.log)
	{
		++m_stats.commandsLogged; // lane ONLINE-3: not when this record ended the log (its size cap, a failed write)
	}
	return true;
}

bool RelayCore::admit(const Game &g, int sender, const Stream *st, size_t streamGrow, size_t senderGrow, size_t gameGrow, size_t optionalMetaGrow) const
{
	// lane ONLINE-3 r2 (Sol r2 of ONLINE-3): may the relay hold these bytes more? The stream's growth counts against the stream, the stream's and the
	// sender's against the sender, and all of it against the game and the relay. The optional metadata growth is a
	// logical per-stream quota only; shared command nodes are accounted by senderGrow once.
	const auto add = [](size_t a, size_t b, size_t &out) {
		if (b > std::numeric_limits<size_t>::max() - a)
		{
			return false;
		}
		out = a + b;
		return true;
	};
	const auto within = [](size_t current, size_t growth, size_t limit) {
		return growth <= limit && current <= limit - growth;
	};
	size_t toSender = 0, all = 0, streamHeld = 0, streamQuota = 0;
	if (!add(streamGrow, senderGrow, toSender) || !add(toSender, gameGrow, all))
	{
		return false;
	}
	if (st != nullptr)
	{
		if (!add(held(*st), st->metaBytes, streamHeld)
			|| !add(streamHeld, streamGrow, streamQuota)
			|| !add(streamQuota, optionalMetaGrow, streamQuota)
			|| streamQuota > m_options.maxStreamBuffer)
		{
			return false;
		}
	}
	return (sender < 0 || within(g.slots[(size_t)sender].buffered, toSender, m_options.maxSlotBuffer))
		&& within(g.buffered, all, m_options.maxGameBuffer)
		&& within(m_buffered, all, m_options.maxTotalBuffer);
}

void RelayCore::stopLog(Game &g, const std::string &why)
{
	// lane ONLINE-3 r2: the log of a game ends here (its size cap, a failed write, a record the caps cannot hold): an error, and everything kept for it
	// (the command counts, the frontier, the disconnect records) is released, so nothing stays held for a log that takes no more
	++m_stats.logErrors;
	error(why);
	if (g.logReader)
	{
		std::fclose(g.logReader);
		g.logReader = nullptr;
	}
	if (g.log)
	{
		std::fclose(g.log);
		g.log = nullptr;
	}
	for (int sender = 0; sender < (int)RelayProtocol::kMaxSlots; ++sender)
	{
		clearSenderCounts(g, sender);
		for (Stream &st : g.streams[(size_t)sender])
		{
			const size_t was = held(st);
			st.metaBytes = 0;
			account(g, sender, was, st);
		}
	}
	accountMeta(g, -1, -(std::ptrdiff_t)(g.frontierBytes + g.membershipBytes));
	std::vector<std::pair<std::uint64_t, std::uint32_t>>().swap(g.frontier);
	g.frontierBytes = 0;
	std::vector<std::vector<std::uint8_t>>().swap(g.membershipLogged);
	g.membershipBytes = 0;
}

void RelayCore::gap(Game &g, int sender, int target, Stream &st, std::uint8_t reason, std::uint64_t nowMs)
{
	const size_t was = held(st);
	st.broken = true;
	st.early.clear();
	st.earlyBytes = 0;
	std::vector<std::uint8_t>().swap(st.partial);
	clearTargetCounts(g, sender, target, st); // lane ONLINE-3: a broken stream logs nothing more
	account(g, sender, was, st);
	// lane ONLINE-3 r2 (Sol r2 of ONLINE-3: a stopped stream left its sender's logged counts held): once no stream of the sender that carried its commands
	// is still followed, its log has ended (these GAP records say where): its counts are released and nothing more of it is logged
	bool following = false;
	for (const Stream &o : g.streams[(size_t)sender])
	{
		following = following || (o.carried && !o.broken);
	}
	if (!following)
	{
		clearSenderLogged(g, sender);
		g.senderStopped[(size_t)sender] = true;
	}
	++m_stats.logGaps;
	std::vector<std::uint8_t> r;
	put8(r, 3);
	put8(r, (std::uint8_t)sender);
	put8(r, (std::uint8_t)target);
	put32(r, (std::uint32_t)(nowMs - g.startMs));
	put8(r, reason);
	write(g, r);
}

void RelayCore::write(Game &g, const std::vector<std::uint8_t> &bytes)
{
	if (g.log && g.logBytes + bytes.size() > m_options.maxLogBytes)
	{
		// lane ONLINE-3 (Sol r1 of ONLINE-3): the log's size is capped; beyond it the log stops, as on a write failure
		stopLog(g, "the log of game " + hex64(g.id) + " reached " + std::to_string(m_options.maxLogBytes) + " bytes: it stops here");
		return;
	}
	if (g.log)
	{
		g.logBytes += bytes.size(); // lane ONLINE-3: LOG_CHUNK's size
		noteFrontier(g);
	}
	if (g.log && std::fwrite(bytes.data(), 1, bytes.size(), g.log) != bytes.size())
	{
		stopLog(g, "writing " + g.logPath + ".part failed: the log of game " + hex64(g.id) + " stops here");
	}
}

void RelayCore::noteAuthenticated(Game &g)
{
	// online batch r4 (Sol's second delta): a version-2 log means a previous-release HELLO opened the game; a player authenticating in it means a
	// current client is there too, which an honest game never is (one release per game): a participant can pin the version (and so keep its INPUT
	// records out of the log) by sending its own ticket in a version-3 HELLO first. Counted, and named in the sidecar for D7
	if (!g.legacyLog)
	{
		return;
	}
	g.legacyAuthAnomaly = true;
	++g.authenticatedHellos;
	++m_stats.legacyLogAuthenticated;
}

void RelayCore::writeInputSidecar(Game &g)
{
	std::vector<std::uint8_t> b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'I' };
	put32(b, 1);
	put64(b, g.id);
	put8(b, g.legacyAuthAnomaly ? 1 : 0);
	put8(b, (std::uint8_t)g.openerSlot);
	put32(b, g.authenticatedHellos);
	const std::string name = g.logPath.substr(g.logPath.find_last_of('/') + 1);
	put8(b, (std::uint8_t)(name.size() & 0xFF));
	put8(b, (std::uint8_t)(name.size() >> 8));
	b.insert(b.end(), name.begin(), name.end());
	std::vector<std::uint8_t> records;
	int count = 0;
	for (size_t i = 0; i < g.slots.size(); ++i)
	{
		const Slot &sl = g.slots[i];
		if (!sl.everBound)
		{
			continue;
		}
		put8(records, 7);
		put8(records, (std::uint8_t)i);
		put8(records, sl.auth.authed ? 1 : 0);
		for (unsigned long long v : { sl.inVerified, sl.inPlain, sl.inBadTag, sl.inReplayed, sl.inUnauthRefused, sl.inSlotMismatch, sl.inMalformed })
		{
			put64(records, v);
		}
		++count;
	}
	put8(b, (std::uint8_t)count);
	b.insert(b.end(), records.begin(), records.end());
	const std::string path = g.logPath + ".input";
	std::FILE *f = std::fopen(path.c_str(), "wb");
	const bool written = f && std::fwrite(b.data(), 1, b.size(), f) == b.size();
	const bool closed = f && std::fclose(f) == 0;
	if (!written || !closed)
	{
		++m_stats.logErrors;
		error("cannot write " + path + ": game " + hex64(g.id) + " has no INPUT sidecar");
	}
}

void RelayCore::close(Game &g, std::uint8_t reason, std::uint64_t nowMs)
{
	if (g.canonical && g.log)
	{
		// ROUTER-1: the committed End record closes the canonical game's input in the log, before the END record
		std::string finError;
		if (!g.canonical->finish(finError))
		{
			m_errors.push_back("canonical game " + hex64(g.id) + " end record: " + finError);
		}
	}
	if (g.log && !g.legacyLog)
	{
		// lane ANTICHEAT-1: every slot's input as AUTH-1 verified it (log version 4, kind 7 INPUT, the relay lead's and the anti-cheat lane's layout),
		// one per slot that ever bound, before END: the integrity check D7 reads it
		for (size_t i = 0; i < g.slots.size(); ++i)
		{
			const Slot &sl = g.slots[i];
			if (!sl.everBound)
			{
				continue;
			}
			std::vector<std::uint8_t> r;
			put8(r, 7);
			put8(r, (std::uint8_t)i);
			put8(r, sl.auth.authed ? 1 : 0);
			for (unsigned long long v : { sl.inVerified, sl.inPlain, sl.inBadTag, sl.inReplayed, sl.inUnauthRefused, sl.inSlotMismatch, sl.inMalformed })
			{
				put64(r, v);
			}
			write(g, r);
		}
	}
	if (g.log && g.legacyLog)
	{
		writeInputSidecar(g); // online batch r4: before the log is renamed into place (a finished log's sidecar is there)
	}
	if (g.log)
	{
		std::vector<std::uint8_t> r;
		put8(r, 4);
		put32(r, (std::uint32_t)(nowMs >= g.startMs ? nowMs - g.startMs : 0));
		put8(r, reason);
		const int wireSlots = g.mask <= 0xff ? 8 : (int)RelayProtocol::kMaxSlots;
		for (int i = 0; i < wireSlots; ++i)
		{
			const Slot &s = g.slots[(size_t)i];
			put64(r, s.datagramsIn);
			put64(r, s.bytesIn);
			put64(r, s.dropped);
		}
		write(g, r);
	}
	bool watched = false;
	for (const Spectator &sp : g.spectators)
	{
		watched = watched || sp.bound;
	}
	// Windows cannot rename a log with its reader open. LOG_REQUEST reopens the finished path for spectators.
	if (g.logReader)
	{
		std::fclose(g.logReader); // lane ONLINE-3
		g.logReader = nullptr;
	}
	if (g.log)
	{
		const bool ok = std::fclose(g.log) == 0;
		g.log = nullptr;
		if (!ok || std::rename((g.logPath + ".part").c_str(), g.logPath.c_str()) != 0)
		{
			++m_stats.logErrors;
			error("finishing " + g.logPath + " failed");
		}
		else
		{
			g.logRenamed = true;
			m_closedLogs.push_back(g.logPath);
			if (m_closedLogs.size() > 1000)
			{
				m_closedLogs.erase(m_closedLogs.begin());
			}
		}
	}
	for (size_t i = 0; i < g.slots.size(); ++i)
	{
		if (g.slots[i].bound)
		{
			eraseBinding(g.slots[i].address, Binding{ g.id, (std::uint8_t)i, false });
		}
	}
	accountMeta(g, -1, -(std::ptrdiff_t)g.buffered); // the game's held bytes go with it
	for (auto &row : g.streams)
	{
		for (Stream &st : row)
		{
			st = Stream();
		}
	}
	for (size_t i = 0; i < g.slots.size(); ++i)
	{
		g.slots[i].buffered = 0;
		for (SenderFrameCounts &counts : g.commandCounts[i])
		{
			counts.clear();
		}
	}
	std::vector<std::vector<std::uint8_t>>().swap(g.membershipLogged); // lane ONLINE-3 r2: released with the game's other held bytes
	g.membershipBytes = 0;
	if (!watched)
	{
		std::vector<std::pair<std::uint64_t, std::uint32_t>>().swap(g.frontier);
		g.frontierBytes = 0;
	}
	if (watched)
	{
		accountMeta(g, -1, (std::ptrdiff_t)g.frontierBytes); // lane ONLINE-3: the spectators' frontier stays with the finished game
	}
	const std::uint64_t id = g.id; // g dies with the erase
	m_tombstones[id] = nowMs;
	m_tombstoneOrder.push_back({ nowMs, id });
	++m_stats.gamesClosed;
	if (watched)
	{
		// lane ONLINE-3: the spectators keep reading the finished log (their delay runs out at 5 frames a second); erased by tick
		g.health.reset(); // lane NETDIAG-1: a closed game leaves the health list (its counters stay in the relay's totals)
		for (Slot &sl : g.slots)
		{
			sl.bound = false;
		}
		g.closed = true;
		g.closedMs = nowMs;
		return;
	}
	if (g.logReader)
	{
		std::fclose(g.logReader);
		g.logReader = nullptr;
	}
	m_games.erase(id);
}

void RelayCore::tick(std::uint64_t nowMs, std::vector<Action> *out)
{
	std::vector<std::uint64_t> silent, finished;
	for (auto &kv : m_games)
	{
		// lane ONLINE-3: spectators that stopped asking are dropped; a finished game goes once its spectators are gone. ADMINFIX-1 (Sol adminfix1-watch):
		// after the end each spectator is released at ITS OWN delay plus the grace (spectatorExpired), so one with a longer delay (the admin's silent
		// watch) never keeps another's binding, and so what that one sees, alive: a public spectator's view of the end is the same with or without it
		bool anySpectator = false;
		for (int i = 0; i < RelayProtocol::kMaxSpectators; ++i)
		{
			Spectator &sp = kv.second.spectators[(size_t)i];
			if (sp.bound && nowMs - sp.lastHeard >= m_options.spectatorIdleMs)
			{
				unbindSpectator(kv.second, i);
				++m_stats.spectatorsDropped;
			}
			else if (sp.bound && spectatorExpired(kv.second, sp, nowMs))
			{
				unbindSpectator(kv.second, i);
			}
			anySpectator = anySpectator || sp.bound;
		}
		if (kv.second.closed)
		{
			if (!anySpectator)
			{
				finished.push_back(kv.first);
			}
			continue;
		}
		if (nowMs - kv.second.lastHeard >= m_options.idleMs)
		{
			silent.push_back(kv.first);
			continue;
		}
		// lane ONLINE-3: the STATUS of every slot that ever bound, to every bound slot, while one is silent or has rejoined
		const Game &g = kv.second;
		if (g.canonical)
		{
			for (size_t i = 0; i < g.slots.size(); ++i)
			{
				const Slot &sl = g.slots[i];
				if (sl.bound && !sl.bye && nowMs - sl.lastHeard >= m_options.silentMs)
				{
					g.canonical->hold((std::uint8_t)i);
				}
			}
			if (out)
			{
				std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> emissions;
				g.canonical->service(nowMs, emissions);
				const size_t from = out->size();
				emitCanonical(g, emissions, *out);
				for (size_t i = from; i < out->size(); ++i)
				{
					sealAction((*out)[i]); // lane AUTH-1
				}
			}
			if (g.canonical->failed())
			{
				m_errors.push_back("canonical game " + hex64(kv.first) + ": " + g.canonical->failure());
			}
		}
		std::vector<RelayProtocol::SlotStatus> st;
		bool interesting = false;
		for (int i = 0; i < (int)RelayProtocol::kMaxSlots; ++i)
		{
			const Slot &sl = g.slots[(size_t)i];
			if (!sl.everBound)
			{
				continue;
			}
			RelayProtocol::SlotStatus e;
			e.slot = (std::uint8_t)i;
			e.bound = sl.bound;
			e.silent = !sl.bound || sl.bye || nowMs - sl.lastHeard >= m_options.silentMs;
			e.generation = sl.generation;
			interesting = interesting || e.silent || e.generation > 0;
			st.push_back(e);
		}
		if (!interesting || !out)
		{
			continue;
		}
		const std::vector<std::uint8_t> datagram = RelayProtocol::encodeStatus(st);
		for (const Slot &sl : g.slots)
		{
			if (sl.bound && !sl.bye)
			{
				Action a = reply(sl.address, std::vector<std::uint8_t>(datagram));
				sealAction(a); // lane AUTH-1
				out->push_back(std::move(a));
				++m_stats.statusesSent;
			}
		}
	}
	for (std::uint64_t id : silent)
	{
		close(m_games.at(id), 2, nowMs);
	}
	for (std::uint64_t id : finished)
	{
		Game &g = m_games.at(id);
		for (int i = 0; i < RelayProtocol::kMaxSpectators; ++i)
		{
			unbindSpectator(g, i);
		}
		if (g.logReader)
		{
			std::fclose(g.logReader);
		}
		accountMeta(g, -1, -(std::ptrdiff_t)g.buffered); // lane ONLINE-3: the finished game's frontier
		m_games.erase(id);
	}
	while (!m_tombstoneOrder.empty() && nowMs - m_tombstoneOrder.front().first >= m_options.tombstoneMs)
	{
		m_tombstones.erase(m_tombstoneOrder.front().second);
		m_tombstoneOrder.pop_front();
	}
}

void RelayCore::shutdown(std::uint64_t nowMs)
{
	while (!m_games.empty())
	{
		Game &g = m_games.begin()->second;
		for (int i = 0; i < RelayProtocol::kMaxSpectators; ++i)
		{
			unbindSpectator(g, i); // lane ONLINE-3: the relay stops: nobody reads on
		}
		if (g.closed)
		{
			if (g.logReader)
			{
				std::fclose(g.logReader);
			}
			accountMeta(g, -1, -(std::ptrdiff_t)g.buffered);
			m_games.erase(m_games.begin());
			continue;
		}
		close(g, 3, nowMs ? nowMs : g.lastHeard);
	}
}
