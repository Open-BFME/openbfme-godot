// OpenBFME online relay. GPL-3.0.
// See RelayCore.h.

#include "RelayCore.h"

#include "Common/Crc32.h"
#include "Common/Sha256.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <cstring>

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
		|| type == 0x83 /*REJOIN, lane ONLINE-3*/;
}
constexpr std::uint8_t kDisconnectPlayer = 26; // NetPacket.h NETCOMMANDTYPE_DISCONNECTPLAYER (lane ONLINE-3: logged once, the drop frame)
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
	if (d[2] != kVersion)
	{
		++m_stats.badDatagrams;
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_VERSION));
	}
	switch (d[3])
	{
	case HELLO: return hello(from, d, size, nowMs, nowUnix);
	case DATA: return data(from, d, size, nowMs);
	case BYE: bye(from, nowMs); return Action();
	case LOG_REQUEST: return logRequest(from, d, size, nowMs); // lane ONLINE-3
	default: ++m_stats.badDatagrams; return Action();
	}
}

RelayCore::Action RelayCore::hello(const RelayAddress &from, const std::uint8_t *d, size_t size, std::uint64_t nowMs, std::uint32_t nowUnix)
{
	using namespace RelayProtocol;
	++m_stats.hellos;
	Ticket t;
	RefuseReason why = REFUSE_BAD_TICKET;
	const size_t body = size - kHeaderBytes;
	// lane ONLINE-3: a version 1 ticket (32 bytes) or a rejoin ticket (34), each with or without the session token
	const size_t ticketBytes = body == kTicketBytesV2 || body == kTicketBytesV2 + kSessionTokenBytes ? kTicketBytesV2 : kTicketBytes;
	if ((body != ticketBytes && body != ticketBytes + kSessionTokenBytes)
		|| !verifyTicket(d + kHeaderBytes, ticketBytes, m_options.keys, nowUnix, t, nullptr, &why))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(why));
	}
	const std::uint8_t *token = body == ticketBytes + kSessionTokenBytes ? d + kHeaderBytes + ticketBytes : nullptr;
	if (m_tombstones.count(t.gameId))
	{
		++m_stats.refused;
		return reply(from, encodeRefused(REFUSE_GAME_CLOSED));
	}
	auto it = m_games.find(t.gameId);
	if (it == m_games.end())
	{
		if (m_games.size() >= m_options.maxGames)
		{
			++m_stats.refused;
			return reply(from, encodeRefused(REFUSE_FULL));
		}
		it = m_games.emplace(t.gameId, Game()).first;
		Game &g = it->second;
		g.id = t.gameId;
		g.mask = t.slotMask;
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
				put32(h, 2); // lane ONLINE-3: version 2 (the REJOINED record)
				put64(h, g.id);
				put8(h, g.mask);
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
	if (t.spectator)
	{
		return spectatorHello(g, t, from, token, nowMs); // lane ONLINE-3
	}
	Slot &s = g.slots[t.slot];
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
		for (int o = 0; o < 8; ++o)
		{
			resetStream(g, t.slot, o);
			resetStream(g, o, t.slot);
		}
		accountMeta(g, t.slot, -(std::ptrdiff_t)g.loggedBytes[t.slot]);
		g.logged[t.slot].clear();
		g.loggedBytes[t.slot] = 0;
		g.senderStopped[t.slot] = false; // its new incarnation's streams start afresh
		newSessionToken(s.session);
		s.generation = t.generation;
		s.bound = true;
		s.bye = false;
		s.address = from;
		s.lastHeard = nowMs;
		m_bindings[from.key()] = Binding{ g.id, t.slot };
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
		return reply(from, encodeWelcome(g.id, t.slot, s.session));
	}
	if (!s.bound || !(s.address == from))
	{
		// ONLINE-2 r2 (Sol's takeover): once a slot was bound, the ticket alone never moves it: only its session token (sent to the first address in
		// WELCOME) proves the HELLO comes from the slot's own client, now behind a new address (a NAT rebinding)
		if (s.everBound && (!token || !ConstantTimeEqual(token, s.session.data(), kSessionTokenBytes)))
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
		s.generation = t.generation; // lane ONLINE-3: a first bind may already be a rejoin ticket's (equal generations rebind with the token only)
		s.bound = true;
		s.everBound = true;
		s.bye = false;
		s.address = from;
		m_bindings[from.key()] = Binding{ g.id, t.slot };
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
	++m_stats.welcomes;
	return reply(from, encodeWelcome(g.id, t.slot, s.session));
}

RelayCore::Action RelayCore::spectatorHello(Game &g, const RelayProtocol::Ticket &t, const RelayAddress &from, const std::uint8_t *token, std::uint64_t nowMs)
{
	// lane ONLINE-3: a spectator binds like a slot (its session token moves it to a new address; the ticket alone does not take a bound index)
	using namespace RelayProtocol;
	Spectator &sp = g.spectators[t.slot];
	if (sp.bound && !(sp.address == from) && (!token || !ConstantTimeEqual(token, sp.session.data(), kSessionTokenBytes)))
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
		++m_stats.spectatorsBound;
	}
	sp.lastHeard = nowMs;
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
	// lane ONLINE-3 (Sol r1 of ONLINE-3): an address that binds to `keep` leaves what it was bound to before, by the binding's kind: a spectator place
	// is a spectator's (its index is no player slot: indices 8..15 are beyond the slots), a player slot a player's, each only while the address is its own
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

void RelayCore::resetStream(Game &g, int sender, int target)
{
	// lane ONLINE-3: a new incarnation's stream (sequence numbers from 0): what the old one held for the log is released
	Stream &st = g.streams[(size_t)sender][(size_t)target];
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
		g.logReader = std::fopen((g.closed ? g.logPath : g.logPath + ".part").c_str(), "rb");
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
			++s.dropped;
			++m_stats.rateLimited;
			return Action();
		}
		s.budgetMilli -= cost;
	}
	const size_t at = kHeaderBytes + 1;
	if (size < at + kPacketHeader)
	{
		++s.dropped;
		++m_stats.badDatagrams;
		return Action();
	}
	const int target = d[kHeaderBytes];
	const std::uint8_t *packet = d + at;
	if (packet[7] != (std::uint8_t)sender)
	{
		++s.dropped;
		++m_stats.slotMismatches; // the slot check by session: the packet speaks for another slot than its ticket's
		return Action();
	}
	if (target == sender && target < 8)
	{
		// lane ONLINE-3: the sender's log-only stream (its frame commands that reach no other player: a 1v1 whose other player is held): logged, never
		// forwarded, acknowledged by the relay as the sender's own slot (an ack-only transport packet in DATA_FROM)
		if (!follow(g, sender, sender, packet, size - at, nowMs))
		{
			++s.dropped;
			++m_stats.invalidPackets;
			return Action();
		}
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
	if (target >= 8 || target == sender || !(g.mask & (1u << target)))
	{
		++s.dropped;
		++m_stats.badTargets;
		return Action();
	}
	const Slot &t = g.slots[(size_t)target];
	// ONLINE-2 r2 (Sol: a packet with trailing bytes entered the log though clients refuse it): a packet the receiving Transport would refuse
	// whole is neither logged nor forwarded, so the log and the receivers see the same packets. The command log follows the stream even when the
	// target is not there yet (its transport resends anyway)
	if (!follow(g, sender, target, packet, size - at, nowMs))
	{
		++s.dropped;
		++m_stats.invalidPackets;
		return Action();
	}
	if (!t.bound)
	{
		++m_stats.unboundTargets;
		return Action();
	}
	d[3] = DATA_FROM;
	d[kHeaderBytes] = (std::uint8_t)sender;
	++m_stats.forwarded;
	m_stats.bytesForwarded += size;
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

bool RelayCore::follow(Game &g, int sender, int target, const std::uint8_t *p, size_t n, std::uint64_t nowMs)
{
	// ---- the whole packet is validated first, as Transport::handlePacket does before it applies anything: the size, the CRC, the magic, the version,
	// every record's layout, the exact end (no trailing bytes) ----
	if (n < kPacketHeader || n > m_options.maxPacketBytes || p[4] != 0x42 || p[5] != 0x46 || p[6] != kPacketVersion || crc32Bytes(p + 4, n - 4) != get32(p))
	{
		return false;
	}
	std::vector<Record> &records = m_records; // reused: no allocation per packet
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
	const size_t nrec = records.size();
	if (at != n)
	{
		return false; // trailing bytes
	}
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
			const bool admitted = command(g, sender, st, std::move(bytes), nowMs);
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
			const bool admitted = command(g, sender, st, std::move(whole), nowMs);
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
// ALLOCATED size of the command being joined (capacity, not size); discarded partial commands are released by swap, so the two agree
size_t RelayCore::held(const Stream &st)
{
	return st.earlyBytes + st.partial.capacity() + st.metaBytes;
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
		auto nodes = [](const std::map<std::uint32_t, std::map<std::uint64_t, std::uint32_t>> &m) {
			size_t c = m.size();
			for (const auto &f : m)
			{
				c += f.second.size();
			}
			return c * RelayCore::kMetaNodeBytes;
		};
		for (const auto &l : kv.second.logged)
		{
			n += nodes(l);
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
				n += st.earlyBytes + st.partial.capacity() + nodes(st.perFrame);
			}
		}
	}
	return n;
}

void RelayCore::account(Game &g, int sender, size_t before, const Stream &st)
{
	accountMeta(g, sender, (std::ptrdiff_t)held(st) - (std::ptrdiff_t)before);
}

bool RelayCore::command(Game &g, int sender, Stream &st, std::vector<std::uint8_t> &&bytes, std::uint64_t nowMs)
{
	if (!g.log)
	{
		return true; // lane ONLINE-3 r2: no log (none kept, or it stopped): nothing is counted
	}
	if (bytes.size() >= 6 && bytes[0] == kDisconnectPlayer && bytes[1] == (std::uint8_t)sender)
	{
		// lane ONLINE-3: the decider's DISCONNECTPLAYER goes to every other slot: logged once (the identical copies on the other streams are skipped)
		for (const std::vector<std::uint8_t> &m : g.membershipLogged)
		{
			if (m == bytes)
			{
				return true;
			}
		}
		if (g.membershipLogged.size() >= 64)
		{
			return true; // a game has at most 7 drops per slot incarnation; beyond this the sender misbehaves (its receivers judge it)
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
	// lane ONLINE-3 (Sol r1 of ONLINE-3: 5,000 distinct commands of one frame made 10,000 nodes that no cap saw): every count node is held, the
	// stream's by the stream, the logged ones by the sender; (Sol r2) the new nodes are admitted under the caps before they are made: a command whose
	// counts do not fit stops the stream (false)
	using Counts = std::map<std::uint32_t, std::map<std::uint64_t, std::uint32_t>>;
	auto newNodes = [frame, hash](const Counts &m) -> size_t {
		const auto fit = m.find(frame);
		return fit == m.end() ? 2 : fit->second.count(hash) ? 0 : 1;
	};
	Counts &logged = g.logged[(size_t)sender];
	if (!admit(g, sender, &st, newNodes(st.perFrame) * RelayCore::kMetaNodeBytes, newNodes(logged) * RelayCore::kMetaNodeBytes, 0))
	{
		return false;
	}
	auto bump = [frame, hash](Counts &m, size_t &bytes) -> std::uint32_t & {
		auto fit = m.find(frame);
		if (fit == m.end())
		{
			fit = m.emplace(frame, std::map<std::uint64_t, std::uint32_t>()).first;
			bytes += RelayCore::kMetaNodeBytes;
		}
		auto hit = fit->second.find(hash);
		if (hit == fit->second.end())
		{
			hit = fit->second.emplace(hash, 0u).first;
			bytes += RelayCore::kMetaNodeBytes;
		}
		return hit->second;
	};
	auto trim = [](Counts &m, size_t &bytes) {
		// counts far behind the newest frame are never needed again
		while (!m.empty() && m.rbegin()->first - m.begin()->first > kKeepFrames)
		{
			bytes -= (m.begin()->second.size() + 1) * RelayCore::kMetaNodeBytes;
			m.erase(m.begin());
		}
	};
	st.carried = true;
	// lane ONLINE-3 r3 (Sol r3 of ONLINE-3): the stream's count nodes are charged here, before the log write below (whose frontier growth is admitted
	// against these totals, and which may stop the log and release them); follow() takes held(st) afresh after a command and charges nothing for it
	const size_t streamBefore = held(st);
	const std::uint32_t carried = ++bump(st.perFrame, st.metaBytes);
	trim(st.perFrame, st.metaBytes);
	account(g, sender, streamBefore, st);
	const size_t loggedBefore = g.loggedBytes[(size_t)sender];
	std::uint32_t &done = bump(logged, g.loggedBytes[(size_t)sender]);
	const bool fresh = carried > done;
	if (fresh)
	{
		done = carried;
	}
	trim(logged, g.loggedBytes[(size_t)sender]);
	accountMeta(g, sender, (std::ptrdiff_t)g.loggedBytes[(size_t)sender] - (std::ptrdiff_t)loggedBefore);
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

bool RelayCore::admit(const Game &g, int sender, const Stream *st, size_t streamGrow, size_t senderGrow, size_t gameGrow) const
{
	// lane ONLINE-3 r2 (Sol r2 of ONLINE-3): may the relay hold these bytes more? The stream's growth counts against the stream, the stream's and the
	// sender's against the sender, and all of it against the game and the relay
	const size_t toSender = streamGrow + senderGrow, all = toSender + gameGrow;
	return (!st || held(*st) + streamGrow <= m_options.maxStreamBuffer)
		&& (sender < 0 || g.slots[(size_t)sender].buffered + toSender <= m_options.maxSlotBuffer) && g.buffered + all <= m_options.maxGameBuffer
		&& m_buffered + all <= m_options.maxTotalBuffer;
}

void RelayCore::stopLog(Game &g, const std::string &why)
{
	// lane ONLINE-3 r2: the log of a game ends here (its size cap, a failed write, a record the caps cannot hold): an error, and everything kept for it
	// (the command counts, the frontier, the disconnect records) is released, so nothing stays held for a log that takes no more
	++m_stats.logErrors;
	error(why);
	if (g.log)
	{
		std::fclose(g.log);
		g.log = nullptr;
	}
	for (int sender = 0; sender < 8; ++sender)
	{
		for (Stream &st : g.streams[(size_t)sender])
		{
			const size_t was = held(st);
			st.perFrame.clear();
			st.metaBytes = 0;
			account(g, sender, was, st);
		}
		accountMeta(g, sender, -(std::ptrdiff_t)g.loggedBytes[(size_t)sender]);
		g.logged[(size_t)sender].clear();
		g.loggedBytes[(size_t)sender] = 0;
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
	st.perFrame.clear(); // lane ONLINE-3: a broken stream logs nothing more
	st.metaBytes = 0;
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
		accountMeta(g, sender, -(std::ptrdiff_t)g.loggedBytes[(size_t)sender]);
		g.logged[(size_t)sender].clear();
		g.loggedBytes[(size_t)sender] = 0;
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

void RelayCore::close(Game &g, std::uint8_t reason, std::uint64_t nowMs)
{
	if (g.log)
	{
		std::vector<std::uint8_t> r;
		put8(r, 4);
		put32(r, (std::uint32_t)(nowMs >= g.startMs ? nowMs - g.startMs : 0));
		put8(r, reason);
		for (const Slot &s : g.slots)
		{
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
	if (g.logReader && !watched)
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
		g.logged[i].clear();
		g.loggedBytes[i] = 0;
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
		// lane ONLINE-3: spectators that stopped asking are dropped; a finished game goes once its spectators are gone or had their delay and the grace
		std::uint32_t longestDelay = 0;
		bool anySpectator = false;
		for (int i = 0; i < RelayProtocol::kMaxSpectators; ++i)
		{
			Spectator &sp = kv.second.spectators[(size_t)i];
			if (sp.bound && nowMs - sp.lastHeard >= m_options.spectatorIdleMs)
			{
				unbindSpectator(kv.second, i);
				++m_stats.spectatorsDropped;
			}
			anySpectator = anySpectator || sp.bound;
			longestDelay = std::max(longestDelay, sp.bound ? sp.delayFrames : 0u);
		}
		if (kv.second.closed)
		{
			if (!anySpectator || nowMs - kv.second.closedMs >= (std::uint64_t)longestDelay * 200u + m_options.spectatorGraceMs)
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
		std::vector<RelayProtocol::SlotStatus> st;
		bool interesting = false;
		for (int i = 0; i < 8; ++i)
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
				out->push_back(reply(sl.address, std::vector<std::uint8_t>(datagram)));
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
