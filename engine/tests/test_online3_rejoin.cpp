// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-3 (docs/ONLINE.md "Phase O3: reconnect"): the disconnect path through the relay (its STATUS, the ping test), the hold of a dropped online
// player and the hand-over when the rejoin window ends, the rejoin (the relay's new incarnation of a slot, its log read back in LOG_CHUNKs, the packet
// router's REJOIN, the switch to lockstep with the CRC exchange), the multiplayer pause, and the catch-up "START + command log -> state at frame F" on the
// retail game (SKIPs loudly without ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"

#include "StartTestUtil.h"
#include "OnlineTestWait.h"

#include "Common/Crc32.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/NetGameSession.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/Rejoin.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "RelayCore.h"

#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> testKey()
{
	std::vector<std::uint8_t> k;
	for (int i = 0; i < 32; ++i)
	{
		k.push_back((std::uint8_t)(i * 3 + 1));
	}
	return k;
}
RelayProtocol::KeyRing testRing()
{
	return { { 1, testKey() } };
}
constexpr std::uint64_t kGame = 0x0011223344556677ull;
RelayProtocol::Ticket testTicket(std::uint8_t slot, std::uint8_t mask = 0x07, std::uint16_t generation = 0)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = kGame;
	t.slot = slot;
	t.slotMask = mask;
	t.expires = 2000000000u;
	t.generation = generation;
	return t;
}
std::vector<std::uint8_t> ticketBytes(const RelayProtocol::Ticket &t)
{
	if (t.generation > 0)
	{
		return RelayProtocol::signRejoinTicket(t, testKey());
	}
	const auto b = RelayProtocol::signTicket(t, testKey());
	return std::vector<std::uint8_t>(b.begin(), b.end());
}
RelayAddress addr(std::uint16_t port)
{
	return RelayAddress{ 0x0A000001u, port };
}
std::filesystem::path tempDir(const std::string &name)
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / ("openbfme_online3_" + name + "_" + std::to_string(NetMilliseconds()));
	std::filesystem::create_directories(p);
	return p;
}
// a transport packet (Transport.h version 2) from `sender` with one record and a cumulative ack of 0
std::vector<std::uint8_t> transportPacket(std::uint8_t sender, std::uint32_t seq, const std::vector<std::uint8_t> &record)
{
	std::vector<std::uint8_t> d(4, 0);
	auto put16 = [&](std::uint16_t v) {
		d.push_back((std::uint8_t)v);
		d.push_back((std::uint8_t)(v >> 8));
	};
	auto put32 = [&](std::uint32_t v) {
		put16((std::uint16_t)v);
		put16((std::uint16_t)(v >> 16));
	};
	put16(0x4642);
	d.push_back(2);
	d.push_back(sender);
	put32(0);
	put16(1);
	put32(seq);
	put16(0);
	put16(1);
	put32((std::uint32_t)record.size());
	d.insert(d.end(), record.begin(), record.end());
	const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		d[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return d;
}
std::vector<std::uint8_t> sessionTokenOf(const std::vector<std::uint8_t> &welcome)
{
	if (welcome.size() != RelayProtocol::kHeaderBytes + 9 + RelayProtocol::kSessionTokenBytes || welcome[3] != RelayProtocol::WELCOME)
	{
		return {};
	}
	return std::vector<std::uint8_t>(welcome.end() - (long)RelayProtocol::kSessionTokenBytes, welcome.end());
}
// the whole log of the game, read through LOG_REQUEST / LOG_CHUNK as a client does
std::vector<std::uint8_t> fetchLog(RelayCore &relay, const RelayAddress &client, std::uint64_t now)
{
	std::vector<std::uint8_t> out;
	for (int guard = 0; guard < 100000; ++guard)
	{
		std::vector<std::uint8_t> req = RelayProtocol::encodeLogRequest((std::uint32_t)out.size());
		RelayCore::Action a = relay.handle(client, req.data(), req.size(), now, 1700000000u);
		REQUIRE(a.kind == RelayCore::Action::REPLY);
		// tolerance: a precondition: a LOG_CHUNK is at least its header (its data length varies)
		REQUIRE(a.reply.size() >= RelayProtocol::kHeaderBytes + 8);
		REQUIRE(a.reply[3] == RelayProtocol::LOG_CHUNK);
		const size_t n = a.reply.size() - RelayProtocol::kHeaderBytes - 8;
		if (n == 0)
		{
			break;
		}
		out.insert(out.end(), a.reply.begin() + (long)(RelayProtocol::kHeaderBytes + 8), a.reply.end());
		now += 1000; // the log budget refills
	}
	return out;
}

// ---- an in-memory network (as test_mp2_disconnect's), with the relay's view of each slot (generations, the local link) and a tap that feeds every
// delivered datagram through a real RelayCore (so its command log is the game's) ----
class Hub
{
public:
	struct Endpoint : NetTransport
	{
		Hub *hub = nullptr;
		int slot = 0;
		std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
		std::set<int> retired;
		std::map<int, std::uint64_t> heard;
		bool linkAlive = true;
		std::vector<int> resets;
		bool sendTo(int to, const std::vector<std::uint8_t> &bytes, std::string *) override
		{
			if (retired.count(to))
			{
				return false;
			}
			hub->queue(slot, to, bytes);
			return true;
		}
		void retire(int to) override { retired.insert(to); }
		bool receive(int &from, std::vector<std::uint8_t> &bytes) override
		{
			if (inbox.empty())
			{
				return false;
			}
			from = inbox.front().first;
			bytes = inbox.front().second;
			inbox.pop_front();
			return true;
		}
		std::uint64_t lastHeardFrom(int s) const override
		{
			const auto it = heard.find(s);
			return it == heard.end() ? 0 : it->second;
		}
		// the log-only stream (Transport::sendToLog): the relay logs it (DATA to the sender's own slot)
		bool sendToLog(const std::vector<std::uint8_t> &bytes) override
		{
			++logOnly;
			if (!hub->relay)
			{
				return false;
			}
			std::vector<std::uint8_t> d = RelayProtocol::encodeData((std::uint8_t)slot, transportPacket((std::uint8_t)slot, hub->m_seq[{ slot, slot }]++, bytes));
			hub->relay->handle(hub->address[(size_t)slot], d.data(), d.size(), hub->clock, 1700000000u);
			return true;
		}
		unsigned long long logOnly = 0;
		int peerGeneration(int s) const override { return hub->generation[(size_t)s]; }
		bool silentAtRelay(int s) const override { return hub->silentAtRelay[(size_t)s]; }
		bool localLinkAlive(std::uint32_t) const override { return linkAlive; }
		void resetPeer(int s) override
		{
			// the transport streams to and from the slot start afresh: from now on its new incarnation and this peer hear each other
			retired.erase(s);
			resets.push_back(s);
			hub->m_blocked.erase({ slot, s });
			hub->m_blocked.erase({ s, slot });
		}
	};
	explicit Hub(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			auto e = std::make_unique<Endpoint>();
			e->hub = this;
			e->slot = i;
			m_endpoints.push_back(std::move(e));
		}
	}
	Endpoint &endpoint(int slot) { return *m_endpoints[(size_t)slot]; }
	void cut(int from, int to, bool c) { m_cut[{ from, to }] = c; }
	void silence(int slot, bool s)
	{
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			cut(slot, i, s);
			cut(i, slot, s);
		}
		if (s)
		{
			m_silenced.insert(slot);
		}
		else
		{
			m_silenced.erase(slot);
		}
	}
	// a restarted process: nothing of the old one is in flight any more, its inbox is empty; until a peer starts its streams with the slot afresh
	// (resetPeer), what they send each other is dropped (a real Transport refuses the other side's sequence numbers)
	void restart(int slot)
	{
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			if (i != slot)
			{
				m_blocked.insert({ slot, i });
				m_blocked.insert({ i, slot });
			}
		}
		std::deque<Flight> keep;
		for (Flight &f : m_inFlight)
		{
			if (f.from != slot && f.to != slot)
			{
				keep.push_back(std::move(f));
			}
		}
		m_inFlight.swap(keep);
		endpoint(slot).inbox.clear();
		endpoint(slot).retired.clear();
		silence(slot, false);
		for (int i = 0; i < (int)m_endpoints.size(); ++i)
		{
			m_seq[{ slot, i }] = 0;
			m_seq[{ i, slot }] = 0;
		}
	}
	void queue(int from, int to, const std::vector<std::uint8_t> &b) { m_inFlight.push_back(Flight{ m_now + 1, from, to, b }); }
	void tick(std::uint64_t ms)
	{
		++m_now;
		clock += ms;
		std::deque<Flight> keep;
		std::set<std::pair<int, int>> held;
		for (Flight &f : m_inFlight)
		{
			// the relay logs what reaches it from a sender, whether or not the target is there (its process may be dead): unless the sender itself is cut
			// off, or the stream is the stale one of a restarted slot (the relay started that stream afresh: the old sequence numbers are not logged)
			if (f.at <= m_now && relay && !f.tapped && !m_silenced.count(f.from) && !m_blocked.count({ f.from, f.to }))
			{
				f.tapped = true;
				std::vector<std::uint8_t> d = RelayProtocol::encodeData((std::uint8_t)f.to, transportPacket((std::uint8_t)f.from, m_seq[{ f.from, f.to }]++, f.bytes));
				relay->handle(address[(size_t)f.from], d.data(), d.size(), clock, 1700000000u);
			}
			if (f.at <= m_now && m_blocked.count({ f.from, f.to }))
			{
				continue; // dropped: the streams of the slot's old and new incarnation do not match
			}
			if (f.at <= m_now && !m_cut[{ f.from, f.to }] && !held.count({ f.from, f.to }))
			{
				m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			}
			else if (f.at <= m_now)
			{
				held.insert({ f.from, f.to });
				keep.push_back(f);
			}
			else
			{
				keep.push_back(f);
			}
		}
		m_inFlight.swap(keep);
		for (int a = 0; a < (int)m_endpoints.size(); ++a)
		{
			for (int b = 0; b < (int)m_endpoints.size(); ++b)
			{
				if (a != b && !m_cut[{ b, a }])
				{
					m_endpoints[(size_t)a]->heard[b] = clock;
				}
			}
		}
	}
	std::uint64_t clock = 1000;
	std::array<int, MAX_SLOTS> generation{};
	std::array<bool, MAX_SLOTS> silentAtRelay{};
	RelayCore *relay = nullptr;
	std::array<RelayAddress, MAX_SLOTS> address{};
	std::set<std::pair<int, int>> m_blocked;
	std::set<int> m_silenced;
	std::map<std::pair<int, int>, std::uint32_t> m_seq; ///< the relay tap's sequence numbers per (sender, target)

private:
	struct Flight
	{
		int at, from, to;
		std::vector<std::uint8_t> bytes;
		bool tapped = false;
	};
	std::vector<std::unique_ptr<Endpoint>> m_endpoints;
	std::map<std::pair<int, int>, bool> m_cut;
	std::deque<Flight> m_inFlight;
	int m_now = 0;
};

// lane NETLAG-1: the lobby's run-ahead the helpers below play with (ONLINE-3's cases keep 2; the netlag1 cases run them with RotWK's 1)
int g_runAhead = 2;

NetworkConfig configFor(int slot, int peers, bool online, int crcInterval = 0)
{
	NetworkConfig c;
	c.localSlot = slot;
	for (int i = 0; i < peers; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = 10 + i;
		c.slotNames[(size_t)i] = u"Player" + std::u16string(1, (char16_t)(u'0' + i));
	}
	c.runAhead = g_runAhead;
	c.crcInterval = crcInterval;
	c.settings.disconnectTime = 1000;
	c.settings.playerTimeoutTime = 6000;
	c.holdForRejoin = online;
	c.rejoinWindowMs = 20000;
	return c;
}

// a deterministic stand-in for the state hash: every peer that ran the same frames with the same commands has the same value
std::uint32_t fakeHash(std::uint32_t previous, UnsignedInt frame, const std::vector<std::string> &commands)
{
	std::uint32_t h = previous * 16777619u ^ frame;
	for (const std::string &c : commands)
	{
		for (char ch : c)
		{
			h = (h ^ (std::uint8_t)ch) * 16777619u;
		}
	}
	return h;
}

struct Peer
{
	std::unique_ptr<Network> net;
	UnsignedInt frame = 0;
	bool running = true;
	bool commands = false; ///< the player issues a command every 7 frames
	CommandList pending;
	std::map<UnsignedInt, std::vector<std::string>> relayed; ///< frame -> its commands ("type player arg")
	std::vector<UnsignedInt> selfDestructFrames;
	std::uint32_t hash = 0x811C9DC5u;
	std::map<UnsignedInt, std::uint32_t> hashes; ///< logic frame after the frame -> fakeHash
	std::function<void(UnsignedInt, const CommandList &)> onRan; ///< lane ONLINE-3 r1: each frame's commands to a real game too (the ownership test)
	static std::vector<std::string> describe(const CommandList &out)
	{
		std::vector<std::string> cmds;
		for (const GameMessage &m : out.messages())
		{
			std::string d = std::to_string(m.getType()) + " " + std::to_string(m.getPlayerIndex());
			if (m.getArgumentCount() > 0 && m.getArgument(0)->type == ARGUMENTDATATYPE_INTEGER)
			{
				d += " " + std::to_string(m.getArgument(0)->integer);
			}
			cmds.push_back(d);
		}
		return cmds;
	}
	void ran(UnsignedInt f, const CommandList &out)
	{
		const std::vector<std::string> cmds = describe(out);
		for (const GameMessage &m : out.messages())
		{
			if (m.getType() == MSG_SELF_DESTRUCT)
			{
				selfDestructFrames.push_back(f);
			}
		}
		relayed[f] = cmds;
		hash = fakeHash(hash, f, cmds);
		hashes[f + 1] = hash;
		if (onRan)
		{
			onRan(f, out);
		}
	}
	void step(UnsignedInt stopAt)
	{
		if (!running)
		{
			return;
		}
		net->update(frame, pending);
		if (frame < stopAt && net->isFrameReady(frame) && net->allowedToContinue(frame))
		{
			CommandList out;
			net->relayCommands(frame, out);
			ran(frame, out);
			++frame;
			if (net->config().crcInterval > 0)
			{
				FrameCompletion c;
				c.frame = frame;
				c.batchFrame = frame - 1;
				c.hashed = true;
				c.hash = hash;
				c.breakdown = true;
				net->frameCompleted(c, pending);
			}
			if (commands && frame % 7 == 3)
			{
				GameMessage m(MSG_DO_STOP, net->config().slotPlayerIndex[(size_t)net->config().localSlot]);
				m.appendIntegerArgument((int)frame);
				pending.append(m);
			}
		}
	}
};

struct Game;
Game *g_game = nullptr; ///< the game runRejoinGame plays (its step helper)

struct Game
{
	Hub hub;
	std::vector<Peer> peers;
	Game(int n, bool online, int crcInterval = 0)
		: hub(n)
		, peers((size_t)n)
	{
		for (int i = 0; i < n; ++i)
		{
			peers[(size_t)i].net = std::make_unique<Network>(configFor(i, n, online, crcInterval), hub.endpoint(i));
			Hub *h = &hub;
			peers[(size_t)i].net->setClock([h] { return h->clock; });
		}
		for (Peer &p : peers)
		{
			p.net->sendLoadComplete();
		}
	}
	void tick(UnsignedInt stopAt, std::uint64_t ms = 50)
	{
		for (Peer &p : peers)
		{
			p.step(stopAt);
		}
		hub.tick(ms);
	}
};
} // namespace

TEST_CASE("online3: the new net commands, the rejoin ticket and the STATUS survive the encoding; a rejoin START names the ticket's generation")
{
	std::string error;
	for (NetCommandType t : { NETCOMMANDTYPE_REJOIN, NETCOMMANDTYPE_REJOINREQUEST, NETCOMMANDTYPE_PAUSE, NETCOMMANDTYPE_RESUME })
	{
		NetCommandMsg c;
		c.type = t;
		c.slot = 2;
		c.executionFrame = 1234;
		c.targetSlot = 1;
		c.frame = 777;
		const std::vector<std::uint8_t> b = NetPacket::encodeCommand(c, &error);
		REQUIRE_MESSAGE(!b.empty(), error);
		NetCommandMsg d;
		REQUIRE_MESSAGE(NetPacket::decodeCommand(b, d, &error), error);
		CHECK(d.type == t);
		CHECK(d.slot == 2);
		CHECK(d.executionFrame == 1234);
		if (t == NETCOMMANDTYPE_REJOIN)
		{
			CHECK(d.targetSlot == 1);
		}
		else
		{
			CHECK(d.frame == 777);
		}
		CHECK(std::string(NetCommandTypeName(t)).rfind("NETCOMMANDTYPE_", 0) == 0);
	}
	NetCommandMsg r;
	r.type = NETCOMMANDTYPE_FRAMERESEND;
	r.targetSlot = 0;
	r.frame = 50;
	r.rejoinMask = 0x04;
	std::vector<std::uint8_t> b = NetPacket::encodeCommand(r, &error);
	NetCommandMsg rr;
	REQUIRE(NetPacket::decodeCommand(b, rr, &error));
	CHECK(rr.rejoinMask == 0x04);
	// the rejoin ticket: version 2, 34 bytes, its generation signed with the rest
	RelayProtocol::Ticket t = testTicket(1, 0x07, 3);
	std::vector<std::uint8_t> tb = ticketBytes(t);
	REQUIRE(tb.size() == RelayProtocol::kTicketBytesV2);
	CHECK(tb[0] == 2);
	RelayProtocol::Ticket out;
	REQUIRE(RelayProtocol::verifyTicket(tb.data(), tb.size(), testRing(), 1700000000u, out, &error));
	CHECK(out.generation == 3);
	CHECK(out.slot == 1);
	tb[16] ^= 1; // another generation under the same MAC
	CHECK_FALSE(RelayProtocol::verifyTicket(tb.data(), tb.size(), testRing(), 1700000000u, out, &error));
	// the pinned bytes the lobby signs (services/online/lobby/test: the same text): key 00 01 .. 1f, game 0123456789abcdef, slot 1, mask 7, generation 3
	{
		RelayProtocol::Ticket p;
		p.keyId = 1;
		p.gameId = 0x0123456789abcdefull;
		p.slot = 1;
		p.slotMask = 0x07;
		p.expires = 2000000000u;
		p.generation = 3;
		std::vector<std::uint8_t> key;
		for (int i = 0; i < 32; ++i)
		{
			key.push_back((std::uint8_t)i);
		}
		const std::vector<std::uint8_t> pb = RelayProtocol::signRejoinTicket(p, key);
		CHECK(RelayProtocol::base64UrlEncode(pb.data(), pb.size()) == "AgHvzauJZ0UjAQEHAJQ1dwMAb4T6YFP3_RWwFnRvvO1_ow");
	}
	t.generation = 0;
	std::vector<std::uint8_t> zero = RelayProtocol::signRejoinTicket(t, testKey());
	CHECK_FALSE(RelayProtocol::verifyTicket(zero.data(), zero.size(), testRing(), 1700000000u, out, &error)); // a rejoin ticket of generation 0
	// STATUS
	std::vector<RelayProtocol::SlotStatus> st(2);
	st[0].slot = 0;
	st[0].bound = true;
	st[1].slot = 2;
	st[1].silent = true;
	st[1].generation = 5;
	const std::vector<std::uint8_t> sd = RelayProtocol::encodeStatus(st);
	std::vector<RelayProtocol::SlotStatus> back;
	REQUIRE(RelayProtocol::decodeStatus(sd.data(), sd.size(), back));
	REQUIRE(back.size() == 2);
	CHECK(back[1].slot == 2);
	CHECK(back[1].silent);
	CHECK_FALSE(back[1].bound);
	CHECK(back[1].generation == 5);
	std::vector<std::uint8_t> bad = sd;
	bad.push_back(0);
	CHECK_FALSE(RelayProtocol::decodeStatus(bad.data(), bad.size(), back));
	// a rejoin START: "rejoin" must name the version 2 ticket's generation; a first START has neither
	const ProfileIdentity profile = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 20;
	limits.colorCount = 10;
	auto startJson = [&](const std::vector<std::uint8_t> &ticket, const std::string &rejoin) {
		return std::string("{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0011223344556677\",\"slot\":1,\"relay\":\"10.0.0.1:7350\",\"ticket\":\"")
			+ RelayProtocol::base64UrlEncode(ticket.data(), ticket.size()) + "\"" + rejoin + ",\"runAhead\":2,\"crcInterval\":100,\"profile\":\"" + profile.digest
			+ "\",\"setup\":{\"map\":\"maps/x/x.map\",\"seed\":1,\"startingCash\":1000,\"superweaponRestriction\":0,\"rules\":null,\"legendaryHeroes\":1,"
			  "\"slots\":[{\"state\":\"player\",\"name\":\"A\",\"faction\":1,\"color\":0,\"startPos\":0,\"team\":-1},"
			  "{\"state\":\"player\",\"name\":\"B\",\"faction\":2,\"color\":1,\"startPos\":1,\"team\":-1},"
			  "{\"state\":\"player\",\"name\":\"C\",\"faction\":2,\"color\":2,\"startPos\":2,\"team\":-1}]}}";
	};
	LobbyStart ls;
	const std::vector<std::uint8_t> rejoinTicket = ticketBytes(testTicket(1, 0x07, 2));
	REQUIRE_MESSAGE(OnlineStart::parse(startJson(rejoinTicket, ",\"rejoin\":2"), profile, limits, ls, &error), error);
	CHECK(ls.rejoinGeneration == 2);
	CHECK(ls.ticket == rejoinTicket);
	CHECK_FALSE(OnlineStart::parse(startJson(rejoinTicket, ",\"rejoin\":1"), profile, limits, ls, &error));
	CHECK(error.find("generation") != std::string::npos);
	CHECK_FALSE(OnlineStart::parse(startJson(rejoinTicket, ""), profile, limits, ls, &error));
	REQUIRE_MESSAGE(OnlineStart::parse(startJson(ticketBytes(testTicket(1)), ""), profile, limits, ls, &error), error);
	CHECK(ls.rejoinGeneration == 0);
	CHECK_FALSE(OnlineStart::parse(startJson(ticketBytes(testTicket(1)), ",\"rejoin\":1"), profile, limits, ls, &error));
}

TEST_CASE("online3: the relay reports silent slots (STATUS), binds a newer rejoin ticket without the session token, refuses an older one and a "
		  "takeover, and logs REJOINED")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("status");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	o.silentMs = 3000;
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		REQUIRE(sessionTokenOf(send(addr((std::uint16_t)(10 + s)), encodeHello(ticketBytes(testTicket(s))), 1000).reply).size() == kSessionTokenBytes);
	}
	// nothing silent, nobody rejoined: no STATUS
	std::vector<RelayCore::Action> out;
	relay.tick(2000, &out);
	CHECK(out.empty());
	// slots 0 and 1 keep sending, slot 2 stops: after 3 s the others (and slot 2's address too) hear that slot 2 is silent
	for (std::uint64_t t = 1500; t <= 4500; t += 500)
	{
		send(addr(10), encodeData(1, transportPacket(0, 0, { 1 })), t);
		send(addr(11), encodeData(0, transportPacket(1, 0, { 1 })), t);
	}
	relay.tick(4500, &out);
	REQUIRE(out.size() == 3);
	std::vector<SlotStatus> st;
	REQUIRE(decodeStatus(out[0].reply.data(), out[0].reply.size(), st));
	REQUIRE(st.size() == 3);
	CHECK_FALSE(st[0].silent);
	CHECK_FALSE(st[1].silent);
	CHECK(st[2].silent);
	CHECK(st[2].bound);
	CHECK(st[2].generation == 0);
	// slot 2's game restarted: its new process has no session token; the START's ticket from a new address is a takeover and refused
	CHECK(send(addr(20), encodeHello(ticketBytes(testTicket(2))), 5000).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	// the lobby's rejoin ticket (generation 1) binds the slot to the new address with a new session token
	const std::vector<std::uint8_t> token = sessionTokenOf(send(addr(20), encodeHello(ticketBytes(testTicket(2, 0x07, 1))), 5000).reply);
	REQUIRE(token.size() == kSessionTokenBytes);
	CHECK(relay.stats().rejoins == 1);
	// the old address is no longer the slot's; the new one forwards
	CHECK(send(addr(12), encodeData(0, transportPacket(2, 0, { 1 })), 5000).reply == encodeEmpty(NOT_BOUND));
	CHECK(send(addr(20), encodeData(0, transportPacket(2, 0, { 1 })), 5000).kind == RelayCore::Action::FORWARD);
	// the same generation again from a third address, without the token: a takeover, refused; a stale ticket (generation 0) is refused as stale
	CHECK(send(addr(30), encodeHello(ticketBytes(testTicket(2, 0x07, 1))), 5100).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	CHECK(send(addr(30), encodeHello(ticketBytes(testTicket(2))), 5100).reply == encodeRefused(REFUSE_STALE));
	CHECK(relay.stats().staleTickets == 1);
	// with its token the new incarnation rebinds (a NAT rebinding of the new process)
	CHECK(sessionTokenOf(send(addr(31), encodeHello(ticketBytes(testTicket(2, 0x07, 1)), token), 5200).reply) == token);
	// the STATUS names generation 1 (sent while any slot has rejoined, silent or not)
	out.clear();
	relay.tick(5300, &out);
	REQUIRE(!out.empty());
	REQUIRE(decodeStatus(out[0].reply.data(), out[0].reply.size(), st));
	CHECK(st[2].generation == 1);
	CHECK_FALSE(st[2].silent);
	// the log: version 2 with the REJOINED record
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		send(s == 2 ? addr(31) : addr((std::uint16_t)(10 + s)), encodeEmpty(BYE), 6000);
	}
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	CHECK(log.version == 4); // lane ANTICHEAT-1 with ONLINE16-FIX: v4 for every game (u16 mask), kind-7 INPUT records before END (RelayCore.h)
	REQUIRE(log.rejoins.size() == 1);
	CHECK(log.rejoins[0].slot == 2);
	CHECK(log.rejoins[0].generation == 1);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3: rejoin before first bind is refused without claiming the slot or losing a later valid rejoin")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("before_bind");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	auto hello = [&](std::uint16_t port, std::uint8_t slot, std::uint16_t generation) {
		auto d = encodeHello(ticketBytes(testTicket(slot, 0x03, generation)));
		return relay.handle(addr(port), d.data(), d.size(), 1000, 1700000000u).reply;
	};
	REQUIRE(sessionTokenOf(hello(10, 0, 0)).size() == kSessionTokenBytes);
	// The original joiner dies before sending HELLO; only its replacement's ticket reaches the relay.
	CHECK(hello(20, 1, 1) == encodeRefused(REFUSE_REJOIN_BEFORE_BIND));
	CHECK(hello(21, 1, 2) == encodeRefused(REFUSE_REJOIN_BEFORE_BIND));
	CHECK(relay.stats().refused == 2);
	CHECK(relay.stats().rejoins == 0);
	CHECK(std::string(refuseReasonName(REFUSE_REJOIN_BEFORE_BIND)) == "cannot rejoin: the original player never connected to the relay");
	REQUIRE(sessionTokenOf(hello(11, 1, 0)).size() == kSessionTokenBytes);
	REQUIRE(sessionTokenOf(hello(20, 1, 1)).size() == kSessionTokenBytes);
	CHECK(relay.stats().rejoins == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(fetchLog(relay, addr(10), 2000), log, &error, true), error);
	REQUIRE(log.rejoins.size() == 1);
	CHECK(log.rejoins[0].slot == 1);
	CHECK(log.rejoins[0].generation == 1);
	auto bye = encodeEmpty(BYE);
	relay.handle(addr(10), bye.data(), bye.size(), 3000, 1700000000u);
	relay.handle(addr(20), bye.data(), bye.size(), 3000, 1700000000u);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3: the relay serves its log in LOG_CHUNKs to a bound client of the game only, refuses without a log, logs the decider's "
		  "DISCONNECTPLAYER once, and logs and acknowledges a log-only stream")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("chunks");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now = 1000) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		send(addr((std::uint16_t)(10 + s)), encodeHello(ticketBytes(testTicket(s))));
	}
	// slot 0 drops slot 2: the same DISCONNECTPLAYER to slot 1 and to slot 2 (each copy forwarded), logged once
	NetCommandMsg drop;
	drop.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
	drop.slot = 0;
	drop.targetSlot = 2;
	drop.frame = 40;
	const std::vector<std::uint8_t> dropBytes = NetPacket::encodeCommand(drop, nullptr);
	CHECK(send(addr(10), encodeData(1, transportPacket(0, 0, dropBytes))).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(10), encodeData(2, transportPacket(0, 0, dropBytes))).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 1);
	// many frame infos: a log bigger than one chunk
	for (std::uint32_t f = 0; f < 200; ++f)
	{
		NetCommandMsg fi;
		fi.type = NETCOMMANDTYPE_FRAMEINFO;
		fi.slot = 1;
		fi.executionFrame = f;
		send(addr(11), encodeData(0, transportPacket(1, f, NetPacket::encodeCommand(fi, nullptr))));
	}
	// an unbound address gets NOT_BOUND, never log bytes
	const std::vector<std::uint8_t> req = encodeLogRequest(0);
	CHECK(send(addr(99), req).reply == encodeEmpty(NOT_BOUND));
	const std::vector<std::uint8_t> bytes = fetchLog(relay, addr(11), 2000);
	// tolerance: a precondition: the log must span more than one chunk for the chunking to be shown (its size depends on the encoding)
	REQUIRE(bytes.size() > kLogChunkBytes);
	CHECK(relay.stats().logChunksSent == (bytes.size() + kLogChunkBytes - 1) / kLogChunkBytes + 1); // every chunk, then the empty one at the end
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(bytes, log, &error), error);
	REQUIRE(log.commands.size() == 201);
	CHECK(log.commands[0].msg.type == NETCOMMANDTYPE_DISCONNECTPLAYER);
	CHECK(log.commands[0].msg.frame == 40);
	CHECK(log.commands[200].msg.executionFrame == 199);
	// a cut at any point parses as a still-growing log (partialTail) and never as a whole one
	std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + (long)(bytes.size() - 5));
	CHECK_FALSE(RelayLogFile::parse(cut, log, &error));
	REQUIRE_MESSAGE(RelayLogFile::parse(cut, log, &error, true), error);
	CHECK(log.commands.size() == 200);
	// the log-only stream: DATA to the sender's own slot is logged once, forwarded nowhere, and acknowledged as that slot (the ack names the next record)
	{
		NetCommandMsg fi;
		fi.type = NETCOMMANDTYPE_FRAMEINFO;
		fi.slot = 1;
		fi.executionFrame = 500;
		const unsigned long long logged = relay.stats().commandsLogged;
		RelayCore::Action a = send(addr(11), encodeData(1, transportPacket(1, 0, NetPacket::encodeCommand(fi, nullptr))));
		REQUIRE(a.kind == RelayCore::Action::REPLY);
		CHECK(a.to == addr(11));
		REQUIRE(a.reply.size() == kHeaderBytes + 1 + 14);
		CHECK(a.reply[3] == DATA_FROM);
		CHECK(a.reply[4] == 1);
		CHECK(a.reply[5 + 7] == 1); // the packet's sender byte: the slot itself
		CHECK(a.reply[5 + 8] == 1); // ack: record 0 arrived, 1 is next
		CHECK(crc32Bytes(a.reply.data() + 9, a.reply.size() - 9) == ((std::uint32_t)a.reply[5] | ((std::uint32_t)a.reply[6] << 8) | ((std::uint32_t)a.reply[7] << 16) | ((std::uint32_t)a.reply[8] << 24)));
		CHECK(relay.stats().commandsLogged == logged + 1);
		CHECK(relay.stats().logStreamPackets == 1);
		// the same record again (a resend): acknowledged, not logged twice
		a = send(addr(11), encodeData(1, transportPacket(1, 0, NetPacket::encodeCommand(fi, nullptr))));
		CHECK(a.kind == RelayCore::Action::REPLY);
		CHECK(relay.stats().commandsLogged == logged + 1);
	}
	// a relay without a log refuses the request (REFUSE_NO_LOG), the binding stays
	RelayCore::Options n;
	n.keys = testRing();
	RelayCore bare(n);
	std::vector<std::uint8_t> hello = encodeHello(ticketBytes(testTicket(0)));
	bare.handle(addr(10), hello.data(), hello.size(), 1000, 1700000000u);
	std::vector<std::uint8_t> r2 = req;
	CHECK(bare.handle(addr(10), r2.data(), r2.size(), 1000, 1700000000u).reply == encodeRefused(REFUSE_NO_LOG));
	std::vector<std::uint8_t> data = encodeData(1, transportPacket(0, 0, { 1 }));
	CHECK(bare.handle(addr(10), data.data(), data.size(), 1000, 1700000000u).reply != encodeEmpty(NOT_BOUND));
	CHECK(relay.openLogHandles() == 2);
	relay.shutdown(2000);
	CHECK(relay.openLogHandles() == 0);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3: a real Transport keeps the relay's STATUS, its ping test follows the relay, resetPeer restarts both streams, its log-only stream "
		  "is acknowledged by the relay, and a log refusal keeps the binding")
{
	using namespace RelayProtocol;
	UDP relaySocket, client;
	std::string error;
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relaySocket.localAddress();
	o.relay.ticket = ticketBytes(testTicket(0, 0x07, 1)); // a rejoin ticket is accepted by the transport
	o.relay.helloMs = 10;
	Transport t(client, 0, o);
	CHECK(t.errors().empty());
	t.setPeer(1, NetAddress{});
	t.setPeer(2, NetAddress{});
	CHECK_FALSE(t.localLinkAlive(1000)); // nothing from the relay yet: the ping test fails
	auto deliver = [&](const std::vector<std::uint8_t> &d) {
		const auto received = t.stats().bytesReceived;
		REQUIRE(relaySocket.sendTo(client.localAddress(), d.data(), d.size()));
		REQUIRE(onlinetest::waitUntil([&] {
			t.service();
			return t.stats().bytesReceived == received + d.size();
		}));
	};
	std::array<std::uint8_t, kSessionTokenBytes> token{};
	deliver(encodeWelcome(kGame, 0, token));
	REQUIRE(t.relayBound());
	CHECK(t.localLinkAlive(1000));
	std::vector<SlotStatus> st(2);
	st[0].slot = 1;
	st[0].bound = true;
	st[1].slot = 2;
	st[1].silent = true;
	st[1].generation = 3;
	deliver(encodeStatus(st));
	CHECK(t.peerGeneration(2) == 3);
	CHECK(t.silentAtRelay(2));
	CHECK_FALSE(t.silentAtRelay(1));
	CHECK(t.relayStatuses() == 1);
	// a generation never goes back (a late STATUS)
	st[1].generation = 2;
	deliver(encodeStatus(st));
	CHECK(t.peerGeneration(2) == 3);
	// resetPeer: the next record to slot 2 is sequence number 0 again
	REQUIRE(t.sendTo(2, { 1, 2, 3 }, &error));
	REQUIRE(t.sendTo(2, { 4 }, &error));
	t.resetPeer(2);
	REQUIRE(t.sendTo(2, { 5 }, &error));
	{
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
		}
		bool seen = false;
		REQUIRE(onlinetest::waitUntil([&] {
			t.service();
			while (relaySocket.receiveFrom(from, d))
			{
				// DATA to slot 2 with one record: its sequence number (after the 5-byte envelope and the 14-byte packet header)
				if (d.size() >= 5 + 14 + 12 && d[3] == DATA && d[4] == 2 && d[5 + 12] == 1)
				{
					const std::uint32_t seq = (std::uint32_t)d[5 + 14] | ((std::uint32_t)d[5 + 15] << 8);
					CHECK(seq == 0);
					CHECK(d.back() == 5); // the record queued before the reset is gone
					seen = true;
				}
			}
			return seen;
		}));
		CHECK(seen);
	}
	// the log-only stream: a record to the client's own slot (slot 0) goes out as DATA naming slot 0; the relay's ack (DATA_FROM 0) clears it
	REQUIRE(t.sendToLog({ 7, 7 }));
	CHECK_FALSE(t.allAcked(0));
	{
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
		}
		bool seen = false;
		REQUIRE(onlinetest::waitUntil([&] {
			t.service();
			while (relaySocket.receiveFrom(from, d))
			{
				seen = seen || (d.size() >= 5 + 14 + 12 && d[3] == DATA && d[4] == 0 && d[5 + 7] == 0 && d[5 + 12] == 1 && d.back() == 7);
			}
			return seen;
		}));
		CHECK(seen);
		std::vector<std::uint8_t> ack(4, 0);
		for (std::uint8_t b : { (std::uint8_t)0x42, (std::uint8_t)0x46, (std::uint8_t)2, (std::uint8_t)0, (std::uint8_t)1, (std::uint8_t)0, (std::uint8_t)0, (std::uint8_t)0, (std::uint8_t)0, (std::uint8_t)0 })
		{
			ack.push_back(b);
		}
		const std::uint32_t crc = crc32Bytes(ack.data() + 4, ack.size() - 4);
		for (int i = 0; i < 4; ++i)
		{
			ack[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
		}
		std::vector<std::uint8_t> fromRelay;
		putHeader(fromRelay, DATA_FROM);
		fromRelay.push_back(0);
		fromRelay.insert(fromRelay.end(), ack.begin(), ack.end());
		deliver(fromRelay);
		CHECK(t.allAcked(0));
	}
	// the relay keeps no log: the refusal is the log's, the binding stays
	deliver(encodeRefused(REFUSE_NO_LOG));
	CHECK(t.relayLogRefused());
	CHECK(t.relayBound());
	CHECK(t.relayRefusal() == 0);
	// log chunks are kept by offset
	const std::vector<std::uint8_t> piece = { 9, 8, 7 };
	deliver(encodeLogChunk(1024, 4096, piece.data(), piece.size()));
	CHECK(t.relayLogSize() == 4096);
	const auto chunks = t.takeRelayLogChunks();
	REQUIRE(chunks.count(1024));
	CHECK(chunks.at(1024) == piece);
}

TEST_CASE("online3: an online drop holds the player (no hand-over) until the rejoin window ends; LAN and a vote hand over at once")
{
	SUBCASE("online: held, then handed over by the packet router when the window ends, on one frame everywhere")
	{
		Game g(3, true);
		for (int t = 0; t < 60; ++t)
		{
			g.tick(100000, 200);
		}
		g.hub.silence(2, true);
		g.peers[2].running = false;
		std::uint64_t heldAt = 0;
		for (int t = 0; t < 400 && heldAt == 0; ++t)
		{
			g.tick(100000, 200);
			if (g.peers[0].net->isHeld(2) && g.peers[1].net->isHeld(2))
			{
				heldAt = g.hub.clock;
			}
		}
		if (heldAt == 0)
		{
			for (const std::string &l : g.peers[0].net->disconnectManager().log())
			{
				MESSAGE("peer 0: " << l);
			}
			for (const std::string &l : g.peers[0].net->rejoinLog())
			{
				MESSAGE("peer 0 rejoin: " << l);
			}
			for (const std::string &l : g.peers[0].net->errors())
			{
				MESSAGE("peer 0 error: " << l);
			}
			MESSAGE("selfdestructs " << g.peers[0].selfDestructFrames.size() << " frame " << g.peers[0].frame);
		}
		// tolerance: 0 is the sentinel for never held
		REQUIRE(heldAt != 0);
		const UnsignedInt heldFrame = g.peers[0].frame;
		// the game goes on, the player's army stays its own: no MSG_SELF_DESTRUCT inside the window (20 s here)
		while (g.hub.clock < heldAt + 15000)
		{
			g.tick(100000, 200);
		}
		// tolerance: the claim is that the game went on while the player was held; how many frames run in the window depends on the run-ahead pacing
		CHECK(g.peers[0].frame > heldFrame + 20);
		CHECK(g.peers[0].selfDestructFrames.empty());
		CHECK(g.peers[1].selfDestructFrames.empty());
		CHECK(g.peers[0].net->isDisconnected(2));
		// the window ends: one hand-over, the same frame on both
		while (g.hub.clock < heldAt + 30000)
		{
			g.tick(100000, 200);
		}
		REQUIRE(g.peers[0].selfDestructFrames.size() == 1);
		CHECK(g.peers[0].selfDestructFrames == g.peers[1].selfDestructFrames);
		CHECK(g.peers[0].net->isHandedOver(2));
		CHECK(g.peers[1].net->isHandedOver(2));
		CHECK_FALSE(g.peers[1].net->isHeld(2));
		CHECK(g.peers[0].net->errors().empty());
		CHECK(g.peers[1].net->errors().empty());
	}
	SUBCASE("LAN: retail's hand-over at the drop")
	{
		Game g(3, false);
		for (int t = 0; t < 60; ++t)
		{
			g.tick(100000, 200);
		}
		g.hub.silence(2, true);
		g.peers[2].running = false;
		for (int t = 0; t < 400 && g.peers[0].selfDestructFrames.empty(); ++t)
		{
			g.tick(100000, 200);
		}
		CHECK(g.peers[0].selfDestructFrames.size() == 1);
		CHECK_FALSE(g.peers[0].net->isHeld(2));
	}
	SUBCASE("online: a vote drops and hands over at once")
	{
		Game g(3, true);
		for (int t = 0; t < 60; ++t)
		{
			g.tick(100000, 200);
		}
		g.hub.silence(2, true);
		g.peers[2].running = false;
		bool voted = false;
		for (int t = 0; t < 400 && g.peers[0].selfDestructFrames.empty(); ++t)
		{
			g.tick(100000, 200);
			if (!voted && g.peers[0].net->disconnectManager().screen().visible && g.peers[1].net->disconnectManager().screen().visible)
			{
				g.peers[0].net->voteForPlayerDisconnect(2); // Kick on both screens before the timeout
				g.peers[1].net->voteForPlayerDisconnect(2);
				voted = true;
			}
		}
		REQUIRE(voted);
		CHECK(g.peers[0].selfDestructFrames.size() == 1);
		CHECK(g.peers[0].net->isHandedOver(2));
		// tolerance: the claim is a deadline: the hand-over came before the player timeout would have run out
		CHECK(g.hub.clock < 1000 + 60 * 200 + 6000); // before the player timeout would have run out
	}
}

TEST_CASE("online3: the ping test through the relay: a peer whose own link failed blames itself, the others blame the silent slot")
{
	Game g(3, true);
	for (int t = 0; t < 40; ++t)
	{
		g.tick(100000, 200);
	}
	// slot 0's link to the relay fails: it hears nobody and nothing from the relay (its ping test fails); the others still hear the relay
	g.hub.silence(0, true);
	g.hub.endpoint(0).linkAlive = false;
	for (int t = 0; t < 15; ++t)
	{
		g.tick(100000, 200);
	}
	const DisconnectManager &cut = g.peers[0].net->disconnectManager();
	CHECK(cut.silentEpisodes(0) == 2); // one spell per silent slot (1 and 2), each the local link's
	CHECK(cut.silentEpisodes(1) == 0);
	CHECK(cut.silentEpisodes(2) == 0);
	const DisconnectManager &other = g.peers[1].net->disconnectManager();
	CHECK(other.silentEpisodes(0) == 1); // slot 0's own spell
	CHECK(other.silentEpisodes(1) == 0);
	CHECK(g.peers[1].net->pingTest());
	CHECK_FALSE(g.peers[0].net->pingTest());
}

namespace
{
void runPauseGame()
{
	Game g(3, true);
	for (Peer &p : g.peers)
	{
		p.commands = true;
	}
	for (int t = 0; t < 40; ++t)
	{
		g.tick(100000, 100);
	}
	g.peers[1].net->requestPause();
	const UnsignedInt pauseFrame = g.peers[1].net->pauseFrame();
	// Peer::frame is the next frame to run; with the run-ahead 1 the pause frame is the requester's next frame (lane NETLAG-1)
	// tolerance: an ordering claim: no peer had run the pause frame when it was requested
	CHECK(pauseFrame >= std::max(g.peers[0].frame, std::max(g.peers[1].frame, g.peers[2].frame)));
	for (int t = 0; t < 100; ++t)
	{
		g.tick(100000, 100);
	}
	for (int i = 0; i < 3; ++i)
	{
		INFO("peer " << i);
		CHECK(g.peers[(size_t)i].frame == pauseFrame); // frames before it ran, it did not
		CHECK(g.peers[(size_t)i].net->isPaused());
		CHECK(g.peers[(size_t)i].net->pausedBy() == 1);
		CHECK_FALSE(g.peers[(size_t)i].net->disconnectManager().screen().visible); // a pause is not a disconnect
	}
	g.peers[2].net->requestResume(); // any player resumes
	for (int t = 0; t < 60; ++t)
	{
		g.tick(100000, 100);
	}
	for (int i = 0; i < 3; ++i)
	{
		CHECK_FALSE(g.peers[(size_t)i].net->isPaused());
		// tolerance: the claim is that every peer ran on after the resume; the count depends on the pacing
		CHECK(g.peers[(size_t)i].frame > pauseFrame + 20);
	}
	// a RESUME that overtakes its PAUSE: slot 1's link to slot 0 is slow; slot 2 got the pause and resumed at once
	g.hub.cut(1, 0, true);
	g.peers[1].net->requestPause();
	const UnsignedInt second = g.peers[1].net->pauseFrame();
	for (int t = 0; t < 4; ++t)
	{
		g.tick(100000, 100);
	}
	REQUIRE(g.peers[2].net->isPaused());
	g.peers[2].net->requestResume();
	for (int t = 0; t < 4; ++t)
	{
		g.tick(100000, 100);
	}
	g.hub.cut(1, 0, false);
	for (int t = 0; t < 80; ++t)
	{
		g.tick(100000, 100);
	}
	for (int i = 0; i < 3; ++i)
	{
		INFO("peer " << i);
		CHECK_FALSE(g.peers[(size_t)i].net->isPaused());
		// tolerance: the claim is that every peer ran on after the second resume; the count depends on the pacing
		CHECK(g.peers[(size_t)i].frame > second + 10);
		CHECK(g.peers[(size_t)i].net->errors().empty());
	}
	CHECK(g.peers[0].net->pausesSeen() == 1); // the second pause arrived after its resume: never applied
	const UnsignedInt common = std::min(g.peers[0].frame, std::min(g.peers[1].frame, g.peers[2].frame));
	for (UnsignedInt f = 0; f < common; ++f)
	{
		CHECK(g.peers[0].relayed[f] == g.peers[1].relayed[f]);
		CHECK(g.peers[0].relayed[f] == g.peers[2].relayed[f]);
	}
}
} // namespace

TEST_CASE("online3: pause and resume: every peer stops before the same frame and runs the same frames after; a RESUME that overtakes its PAUSE "
		  "cancels it")
{
	runPauseGame();
}

namespace
{
// n players (slots 0 .. n - 1); the last one's game restarts: its old process died (and the relay reported it silent), or (oldAlive) still runs when
// the new one binds (a hung game the player started again): the others' last records to the old process are then in flight while the relay starts the
// slot's streams afresh, and only their re-send to the log-only stream keeps them in the log. Lane ONLINE-3 r1 (Sol r1 of ONLINE-3): `restarts` names
// the slots whose games restart in turn (default: the last one once); each rejoins before the next restarts, and a later one rebuilds the earlier
// rejoins from the log (its transport and the generations as a peer that saw them live)
void runRejoinGame(int n, bool oldAlive = false, std::vector<int> restarts = {})
{
	if (restarts.empty())
	{
		restarts.push_back(n - 1);
	}
	const std::uint8_t mask = (std::uint8_t)((1u << n) - 1u);
	std::array<int, MAX_SLOTS> generations{};
	int X = 0;
	std::vector<int> others;
	auto stepOthers = [&](int ticks) {
		for (int t = 0; t < ticks; ++t)
		{
			for (int s : others)
			{
				g_game->peers[(size_t)s].step(100000);
			}
			g_game->hub.tick(100);
		}
	};
	const std::filesystem::path dir = tempDir("rejoin");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	RelayCore relay(ro);
	const int kCrc = 10;
	Game g(n, true, kCrc);
	g_game = &g;
	g.hub.relay = &relay;
	for (int s = 0; s < n; ++s)
	{
		g.hub.address[(size_t)s] = addr((std::uint16_t)(10 + s));
		std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(ticketBytes(testTicket((std::uint8_t)s, mask)));
		REQUIRE(relay.handle(g.hub.address[(size_t)s], hello.data(), hello.size(), g.hub.clock, 1700000000u).kind == RelayCore::Action::REPLY);
	}
	for (Peer &p : g.peers)
	{
		p.commands = true;
	}
	for (int t = 0; t < 120; ++t)
	{
		g.tick(100000, 100);
	}
	// tolerance: a precondition: the game is under way (beyond its first CRC frames) before the restart
	REQUIRE(g.peers[0].frame > 50);
	// tolerance: a precondition: several CRC exchanges passed before the restart; their number depends on the pacing
	CHECK(g.peers[0].net->crcChecksPassed() > 3);
	const NetworkConfig xc = configFor(n - 1, n, true, kCrc);
	UnsignedInt lastJoin = 0;
	for (size_t round = 0; round < restarts.size(); ++round)
	{
	X = restarts[round];
	// tolerance: slot 0 must not be the restarted slot (the harness reads the packet router's state)
	REQUIRE(X != 0); // slot 0 is the packet router the checks below read
	const int gen = ++generations[(size_t)X];
	INFO("round " << round << ": slot " << X << " restarts (generation " << gen << ")");
	others.clear();
	for (int s = 0; s < n; ++s)
	{
		if (s != X)
		{
			others.push_back(s);
		}
	}
	const unsigned long long logOnlyBefore = g.hub.endpoint(0).logOnly, logStreamBefore = relay.stats().logStreamPackets;
	std::array<size_t, MAX_SLOTS> logBefore{};
	for (int s = 0; s < n; ++s)
	{
		logBefore[(size_t)s] = g.peers[(size_t)s].net->disconnectManager().log().size();
	}
	if (!oldAlive)
	{
		// the last slot's process dies; after 3 s the relay reports it silent (STATUS)
		g.hub.silence(X, true);
		g.peers[(size_t)X].running = false;
		for (int t = 0; t < 15; ++t)
		{
			g.tick(100000, 100);
		}
		g.hub.silentAtRelay[(size_t)X] = true;
		for (int t = 0; t < 5; ++t)
		{
			g.tick(100000, 100);
		}
		REQUIRE(g.peers[0].net->disconnectManager().screen().visible);
	}
	else
	{
		// the old process stops being the slot's the moment the new one binds (the relay moved the slot): its datagrams then go nowhere
		g.peers[(size_t)X].running = false;
	}
	// its game restarted: the new process's datagrams reach the others' transports (heard, though refused) a moment before the relay's next STATUS
	// names its generation: the screen of the old incarnation stays on (no screen off and on again at one frame, where the drop could never be decided)
	g.hub.restart(X);
	if (oldAlive)
	{
		g.hub.generation[(size_t)X] = gen; // the relay's STATUS names the new process at once
	}
	stepOthers(8);
	for (int s : others)
	{
		CHECK((oldAlive || g.peers[(size_t)s].net->disconnectManager().screen().visible));
	}
	// the rejoin ticket binds the slot's new incarnation at the relay, which tells the others (STATUS: generation 1, no longer silent)
	g.hub.address[(size_t)X] = addr((std::uint16_t)(102 + round));
	{
		std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(ticketBytes(testTicket((std::uint8_t)X, mask, (std::uint16_t)gen)));
		RelayCore::Action a = relay.handle(g.hub.address[(size_t)X], hello.data(), hello.size(), g.hub.clock, 1700000000u);
		REQUIRE(sessionTokenOf(a.reply).size() == RelayProtocol::kSessionTokenBytes);
	}
	g.hub.generation[(size_t)X] = gen;
	g.hub.silentAtRelay[(size_t)X] = false;
	Peer &x = g.peers[(size_t)X];
	x.pending.reset(); // a process restart discards its unsent commands with the old Network
	x.net = std::make_unique<Network>(configFor(X, n, true, kCrc), g.hub.endpoint(X));
	{
		Hub *h = &g.hub;
		x.net->setClock([h] { return h->clock; });
	}
	x.net->setRejoining(gen);
	x.relayed.clear();
	x.hashes.clear();
	x.hash = 0x811C9DC5u;
	x.frame = 0;
	// lane NETLAG-1: a restarted process has no input of its old one (the old incarnation's CRC of its last completed frame, appended but never sent,
	// went out from the new one with the run-ahead 1: an extra CRC on a frame no peer had one for)
	x.pending.reset();
	// the old incarnation is gone: the others drop it at once (no player timeout) and hold it; nobody hands it over
	std::uint64_t restartedAt = g.hub.clock;
	auto allHeld = [&] {
		bool all = true;
		for (int s : others)
		{
			all = all && g.peers[(size_t)s].net->isHeld(X);
		}
		return all;
	};
	for (int t = 0; t < 200 && !allHeld(); ++t)
	{
		stepOthers(1);
	}
	if (!g.peers[0].net->isHeld(X))
	{
		for (const std::string &l : g.peers[0].net->disconnectManager().log())
		{
			MESSAGE("peer 0: " << l);
		}
		for (const std::string &l : g.peers[0].net->rejoinLog())
		{
			MESSAGE("peer 0 rejoin: " << l);
		}
		for (const std::string &l : g.peers[0].net->errors())
		{
			MESSAGE("peer 0 error: " << l);
		}
	}
	REQUIRE(allHeld());
	// tolerance: the claim is a deadline: held on the restart report, well before NetworkPlayerTimeoutTime (6 s)
	CHECK(g.hub.clock - restartedAt < 2000); // at once: the restart, not NetworkPlayerTimeoutTime (6 s here)
	for (int i : others)
	{
		int offs = 0;
		const std::vector<std::string> &dl = g.peers[(size_t)i].net->disconnectManager().log();
		for (size_t k = logBefore[(size_t)i]; k < dl.size(); ++k)
		{
			offs += dl[k].rfind("disconnect screen off", 0) == 0 ? 1 : 0;
		}
		// tolerance: zero or one screen-off are both correct (the drop's own); two would be the flap this guards against
		CHECK(offs <= 1); // this round: at most the screen off after the drop, never one while the slot was gone
	}
	CHECK(g.peers[0].selfDestructFrames.empty());

	// the new incarnation: reads the log, re-runs it (here: the frame lists and the stand-in hash), asks, waits for the REJOIN
	RelayLog::Assembled a;
	std::string error;
	auto readLog = [&] {
		RelayLogFile log;
		const std::vector<std::uint8_t> bytes = fetchLog(relay, g.hub.address[(size_t)X], g.hub.clock);
		REQUIRE_MESSAGE(RelayLogFile::parse(bytes, log, &error, true), error);
		REQUIRE_MESSAGE(RelayLog::assemble(log, xc, a, &error, true), error);
	};
	std::map<UnsignedInt, Network::LiveStart::Crc> crcs;
	auto catchUp = [&](UnsignedInt upTo) {
		while (x.frame <= upTo && x.frame <= a.lastCompleteFrame)
		{
			CommandList out;
			const auto it = a.frames.find(x.frame);
			if (it != a.frames.end())
			{
				out = it->second;
			}
			x.ran(x.frame, out);
			++x.frame;
			const auto h = a.hashes.find(x.frame);
			if (h != a.hashes.end())
			{
				CHECK(h->second == x.hash); // the players' CRC of the frame
			}
			if (x.frame % kCrc == 0)
			{
				Network::LiveStart::Crc c;
				c.frame = x.frame;
				c.hash = x.hash;
				c.half.frame = x.frame;
				c.half.hash = x.hash;
				c.half.slot = X;
				crcs[x.frame] = c;
			}
		}
	};
	readLog();
	catchUp(0xFFFFFFFFu);
	x.net->setRejoinReady();
	UnsignedInt join = 0xFFFFFFFFu;
	for (int t = 0; t < 400 && join == 0xFFFFFFFFu; ++t)
	{
		x.net->update(x.frame, x.pending);
		stepOthers(1);
		if (t % 5 == 0)
		{
			readLog();
			if (a.incarnations[(size_t)X] == gen + 1 && a.joinFrame[(size_t)X] != 0xFFFFFFFFu)
			{
				join = a.joinFrame[(size_t)X];
			}
		}
	}
	if (join == 0xFFFFFFFFu)
	{
		for (const std::string &l : g.peers[0].net->disconnectManager().log())
		{
			MESSAGE("peer 0: " << l);
		}
		for (const std::string &l : g.peers[0].net->rejoinLog())
		{
			MESSAGE("peer 0 rejoin: " << l);
		}
		for (const std::string &l : g.peers[0].net->errors())
		{
			MESSAGE("peer 0 error: " << l);
		}
		MESSAGE("log: incarnations " << a.incarnations[(size_t)X] << " last complete " << a.lastCompleteFrame << " peer 0 frame " << g.peers[0].frame
										  << " log-only " << g.hub.endpoint(0).logOnly << " relay log-stream packets " << relay.stats().logStreamPackets);
		std::string have;
		for (const auto &kv : a.latestFrames[0])
		{
			if (kv.first >= 115)
			{
				have += " " + std::to_string(kv.first);
			}
		}
		MESSAGE("slot 0 complete frames in the log from 115:" << have);
		RelayLogFile raw;
		const std::vector<std::uint8_t> bytes = fetchLog(relay, g.hub.address[(size_t)X], g.hub.clock);
		RelayLogFile::parse(bytes, raw, &error, true);
		std::string cmds;
		for (const auto &c : raw.commands)
		{
			if (c.msg.executionFrame >= 118 || c.msg.type == NETCOMMANDTYPE_DISCONNECTPLAYER)
			{
				cmds += " [" + std::to_string(c.sender) + " " + NetCommandTypeName(c.msg.type) + " " + std::to_string(c.msg.executionFrame) + " n" + std::to_string(c.msg.commandCount) + "]";
			}
		}
		MESSAGE("log commands:" << cmds);
	}
	REQUIRE(join != 0xFFFFFFFFu);
	CHECK(std::count(g.hub.endpoint(0).resets.begin(), g.hub.endpoint(0).resets.end(), X) == gen); // its streams started afresh for each incarnation
	if (n == 2)
	{
		// tolerance: the claim is that the log-only stream was used; how many records went there depends on how long the player was alone
		CHECK(g.hub.endpoint(0).logOnly > logOnlyBefore); // alone while slot 1 was held: its frames reached the relay through the log-only stream
		CHECK(relay.stats().logStreamPackets - logStreamBefore == g.hub.endpoint(0).logOnly - logOnlyBefore);
	}
	else
	{
		CHECK(g.hub.endpoint(0).logOnly == logOnlyBefore); // a live player received every frame command
	}
	for (int s : others)
	{
		CHECK(g.peers[(size_t)s].net->rejoinFrame(X) == join);
		CHECK(g.peers[(size_t)s].frame == join); // the others wait for the rejoined slot's frames from `join` on
	}
	catchUp(join - 1);
	REQUIRE(x.frame == join);
	// the switch: the membership from the log, this slot's CRCs that execute from `join` on, the others' frames after the switch from the log
	Network::LiveStart ls;
	ls.frame = join;
	ls.runAhead = a.runAheadAfter(join - 1, g_runAhead);
	ls.membership = a.membership;
	for (const auto &kv : crcs)
	{
		const UnsignedInt exec = kv.first + (UnsignedInt)a.runAheadAfter(kv.first - 1, g_runAhead);
		if (kv.first <= join && exec >= join)
		{
			Network::LiveStart::Crc c = kv.second;
			c.executionFrame = exec;
			ls.crcs.push_back(c);
		}
	}
	bool sawDrop = false;
	for (const Network::MembershipEvent &e : a.membership)
	{
		sawDrop = sawDrop || (e.kind == Network::MembershipEvent::DROP && e.slot == X);
	}
	CHECK(sawDrop);
	x.net->beginLive(ls);
	for (int s : others)
	{
		for (const auto &kv : a.latestFrames[(size_t)s])
		{
			if (kv.first >= join)
			{
				x.net->fillFromLog(s, kv.first, kv.second.commands, kv.second.runAheadChange, kv.second.rejoins);
			}
		}
	}
	x.running = true;
	const unsigned long long passedBefore = g.peers[0].net->crcChecksPassed();
	for (int t = 0; t < 300; ++t)
	{
		g.tick(100000, 100);
	}
	// tolerance: the claim is that the rejoined peer runs on after its switch; the count depends on the pacing
	CHECK(x.frame > join + 40);
	// tolerance: the claim is that CRC exchanges passed after the switch; their number depends on the pacing
	CHECK(g.peers[0].net->crcChecksPassed() > passedBefore + 3);
	for (int i = 0; i < n; ++i)
	{
		INFO("peer " << i);
		const Network &net = *g.peers[(size_t)i].net;
		CHECK(net.errors().empty());
		for (const std::string &e : net.errors())
		{
			MESSAGE(e);
		}
		CHECK_FALSE(net.sawCRCMismatch());
		CHECK_FALSE(net.isHeld(X));
		// tolerance: the claim is that this peer took part in a passed CRC exchange
		CHECK(net.crcChecksPassed() > 0);
	}
	// tolerance: the claim is that the rejoined peer passed CRC exchanges; their number depends on the pacing
	CHECK(x.net->crcChecksPassed() > 3);
	for (int s : others)
	{
		// lane ONLINE-3 r1: an earlier rejoined slot is a member of the rejoined peer's game, not one it takes for restarted (its generation)
		CHECK_FALSE(x.net->slotRestarted(s));
		CHECK(x.net->isPlayerConnected(s));
		CHECK_FALSE(g.hub.endpoint(X).retired.count(s));
	}
	lastJoin = join;
	} // the rounds
	// every frame the rejoined slots ran equals the others'
	UnsignedInt common = 0xFFFFFFFFu;
	for (const Peer &p : g.peers)
	{
		common = std::min(common, p.frame);
	}
	for (UnsignedInt f = 0; f < common; ++f)
	{
		INFO("frame " << f);
		for (int s = 1; s < n; ++s)
		{
			CHECK(g.peers[(size_t)s].relayed[f] == g.peers[0].relayed[f]);
		}
	}
	// the relay's log of the whole game (every incarnation) assembles into the frames every peer ran, with the CRCs agreed
	std::string error;
	RelayLogFile final;
	REQUIRE_MESSAGE(RelayLogFile::parse(fetchLog(relay, g.hub.address[0], g.hub.clock + 100000), final, &error, true), error);
	RelayLog::Assembled whole;
	REQUIRE_MESSAGE(RelayLog::assemble(final, xc, whole, &error, true), error);
	CHECK(whole.crcDisagreements == 0);
	// every frame all peers ran (0 .. common - 1) is complete in the log: a peer runs a frame only once every member's FRAMEINFO for it reached it
	// through the hub, whose relay tap logs each flight as it is delivered; the log may hold more (the run-ahead frames announced but not yet run)
	// tolerance: a lower bound on the log's end, derived above; the frames below are compared exactly
	CHECK(whole.lastCompleteFrame + 1 >= common);
	for (UnsignedInt f = 0; f <= whole.lastCompleteFrame && f < common; ++f)
	{
		INFO("frame " << f);
		const auto it = whole.frames.find(f);
		const std::vector<std::string> fromLog = it == whole.frames.end() ? std::vector<std::string>() : Peer::describe(it->second);
		CHECK(fromLog == g.peers[0].relayed[f]);
	}
	size_t lateHashes = 0;
	for (const auto &kv : whole.hashes)
	{
		CHECK(kv.second == g.peers[0].hashes[kv.first]);
		lateHashes += kv.first > lastJoin ? 1 : 0;
	}
	// tolerance: the claim is that CRCs after the rejoin are in the log; their number depends on the pacing
	CHECK(lateHashes > 3);
	relay.shutdown(0);
	CHECK(relay.openLogHandles() == 0);
	std::filesystem::remove_all(dir);
	g_game = nullptr;
}
} // namespace

TEST_CASE("solo2: the only network player logs one self-rejoin and resumes from the assembled join frame")
{
	struct Sink : NetTransport
	{
		std::vector<std::vector<std::uint8_t>> records;
		bool sendTo(int, const std::vector<std::uint8_t> &, std::string *) override { return true; }
		bool receive(int &, std::vector<std::uint8_t> &) override { return false; }
		bool sendToLog(const std::vector<std::uint8_t> &bytes) override
		{
			records.push_back(bytes);
			return true;
		}
	} sink;
	NetworkConfig config;
	config.localSlot = 0;
	config.slotPlayerIndex[0] = 10;
	config.disconnectPath = false;
	config.adaptiveRunAhead = false;
	config.earlyOrders = false;
	config.runAhead = 2;
	Network net(config, sink);
	std::uint64_t clock = 1000;
	net.setClock([&] { return clock; });
	net.setRejoining(1);
	CommandList pending;
	net.update(5, pending);
	CHECK(sink.records.empty());
	net.setRejoinReady();
	net.update(5, pending);
	clock += 1000;
	net.update(5, pending);
	REQUIRE(sink.records.size() == 1);
	NetCommandMsg rejoin;
	std::string error;
	REQUIRE_MESSAGE(NetPacket::decodeCommand(sink.records[0], rejoin, &error), error);
	CHECK(rejoin.type == NETCOMMANDTYPE_REJOIN);
	CHECK(rejoin.slot == 0);
	CHECK(rejoin.targetSlot == 0);
	CHECK(rejoin.executionFrame == 7);

	RelayLogFile log;
	log.slotMask = 1;
	for (UnsignedInt frame = 0; frame < 5; ++frame)
	{
		NetCommandMsg info;
		info.type = NETCOMMANDTYPE_FRAMEINFO;
		info.executionFrame = frame;
		info.commandCount = 0;
		log.commands.push_back({ 0, 0, info });
	}
	log.rejoins.push_back({ 0, 1, log.commands.size() });
	RelayLog::Assembled assembled;
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, assembled, &error, true), error);
	CHECK(assembled.lastCompleteFrame == 4);
	CHECK(assembled.joinFrame[0] == 0xFFFFFFFFu);
	CHECK(assembled.problems.empty());
	log.commands.push_back({ 0, 0, rejoin });
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, assembled, &error, true), error);
	CHECK(assembled.lastCompleteFrame == 7);
	CHECK(assembled.joinFrame[0] == 8);
	CHECK(assembled.problems.empty());
	REQUIRE(assembled.membership.size() == 1);
	CHECK(assembled.membership[0].kind == Network::MembershipEvent::REJOIN);
	CHECK(assembled.membership[0].generation == 1);
	Network::LiveStart live;
	live.frame = assembled.joinFrame[0];
	live.runAhead = config.runAhead;
	live.membership = assembled.membership;
	net.beginLive(live);
	net.update(live.frame, pending);
	CHECK(net.isFrameReady(live.frame));
	CHECK(net.errors().empty());
	for (UnsignedInt frame = 8; frame < 11; ++frame)
	{
		NetCommandMsg info;
		info.type = NETCOMMANDTYPE_FRAMEINFO;
		info.executionFrame = frame;
		info.commandCount = 0;
		log.commands.push_back({ 0, 0, info });
	}
	log.rejoins.push_back({ 0, 2, log.commands.size() });
	rejoin.executionFrame = 13;
	log.commands.push_back({ 0, 0, rejoin });
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, assembled, &error, true), error);
	CHECK(assembled.lastCompleteFrame == 13);
	CHECK(assembled.joinFrame[0] == 14);
	CHECK(assembled.problems.empty());
	REQUIRE(assembled.membership.size() == 2);
	CHECK(assembled.membership[0].frame == 7);
	CHECK(assembled.membership[0].generation == 1);
	CHECK(assembled.membership[1].frame == 13);
	CHECK(assembled.membership[1].generation == 2);

	Sink multiplayerSink;
	config.slotPlayerIndex[1] = 11;
	Network multiplayer(config, multiplayerSink);
	multiplayer.setRejoining(1);
	multiplayer.setRejoinReady();
	multiplayer.update(5, pending);
	CHECK(multiplayerSink.records.empty());
}

TEST_CASE("solo2: solo CRCs continue across rejoin; multiplayer still requires a first-incarnation CRC anchor")
{
	NetworkConfig config;
	config.slotPlayerIndex[0] = 10;
	config.crcInterval = 5;
	RelayLogFile log;
	log.slotMask = 1;
	auto info = [&](int slot, UnsignedInt frame, int count) {
		NetCommandMsg msg;
		msg.type = NETCOMMANDTYPE_FRAMEINFO;
		msg.executionFrame = frame;
		msg.commandCount = count;
		log.commands.push_back({ slot, 0, msg });
	};
	auto crc = [&](UnsignedInt frame, int value) {
		NetCommandMsg msg;
		msg.type = NETCOMMANDTYPE_GAMECOMMAND;
		msg.executionFrame = frame;
		msg.message = std::make_shared<GameMessage>(MSG_LOGIC_CRC, 10);
		msg.message->appendIntegerArgument(value);
		log.commands.push_back({ 0, 0, msg });
	};
	for (UnsignedInt frame = 0; frame < 5; ++frame)
	{
		info(0, frame, frame == 4 ? 1 : 0);
	}
	crc(4, 100);
	log.rejoins.push_back({ 0, 1, log.commands.size() });
	NetCommandMsg rejoin;
	rejoin.type = NETCOMMANDTYPE_REJOIN;
	rejoin.targetSlot = 0;
	rejoin.executionFrame = 7;
	log.commands.push_back({ 0, 0, rejoin });
	for (UnsignedInt frame = 8; frame <= 10; ++frame)
	{
		info(0, frame, frame == 10 ? 1 : 0);
	}
	crc(10, 200);
	RelayLog::Assembled assembled;
	std::string error;
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, assembled, &error, true), error);
	CHECK(assembled.problems.empty());
	REQUIRE(assembled.hashes.size() == 2);
	CHECK(assembled.hashes.at(5) == 100);
	CHECK(assembled.hashes.at(10) == 200);

	log.slotMask = 3;
	config.slotPlayerIndex[1] = 11;
	for (UnsignedInt frame = 0; frame <= 10; ++frame)
	{
		info(1, frame, frame == 7 ? 1 : 0);
	}
	// a multiplayer game logs the decider's DROP of the restarted slot (a classic room's old incarnation is not ended by its successor, lane NETDIAG-1)
	NetCommandMsg drop;
	drop.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
	drop.targetSlot = 0;
	drop.frame = 5;
	log.commands.push_back({ 1, 0, drop });
	log.commands.push_back({ 1, 0, rejoin });
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, assembled, &error, true), error);
	REQUIRE(assembled.problems.size() == 1);
	CHECK(assembled.problems[0].find("has no CRC of a first incarnation beside it") != std::string::npos);
	CHECK(assembled.hashes.count(10) == 0);
}

TEST_CASE("online3: rejoin: a restarted slot is dropped and held, its new incarnation reads the relay's log, asks, is taken back by the packet router at "
		  "one frame, and its CRCs complete the exchange; the log replays the whole game")
{
	runRejoinGame(3);
}

TEST_CASE("online3: rejoin in a 1v1: the player left alone sends its frames to the relay's log-only stream, so the rejoining player finds them and the "
		  "REJOIN in the log")
{
	runRejoinGame(2);
}

TEST_CASE("online3: rejoin in a 1v1 while the old process still runs: the frame commands in flight to it when the relay restarts its streams are sent "
		  "to the log-only stream again, so the log has no hole")
{
	runRejoinGame(2, true);
}

TEST_CASE("online3 r1 (Sol: log order): a peer that finds itself alone sends its history to the relay's log-only stream before its next command, and "
		  "no command twice, alone again or not: the relay's log has the sender's commands once each, in its order")
{
	// Sol's probe: command 1 went to the other player (a stream the relay then started afresh), command 2 was the first sent alone: round 1 sent 2 to
	// the log, then the history 1, 2, and the relay logged 2, 1, 2
	struct Sink : NetTransport
	{
		bool silent = false;
		std::vector<std::vector<std::uint8_t>> logStream;
		bool sendTo(int, const std::vector<std::uint8_t> &, std::string *) override { return true; }
		bool receive(int &, std::vector<std::uint8_t> &) override { return false; }
		bool silentAtRelay(int s) const override { return s == 1 && silent; }
		bool sendToLog(const std::vector<std::uint8_t> &bytes) override
		{
			logStream.push_back(bytes);
			return true;
		}
	} sink;
	NetworkConfig c;
	c.localSlot = 0;
	c.slotPlayerIndex[0] = 10;
	c.slotPlayerIndex[1] = 11;
	c.disconnectPath = false;
	c.adaptiveRunAhead = false;
	c.runAhead = 2;
	Network net(c, sink);
	auto issue = [&](int value, UnsignedInt frame) {
		CommandList pending;
		GameMessage m(MSG_DO_STOP, 10);
		m.appendIntegerArgument(value);
		pending.append(m);
		net.update(frame, pending);
	};
	auto commandsIn = [](const std::vector<std::vector<std::uint8_t>> &records) {
		std::vector<int> out;
		for (const std::vector<std::uint8_t> &r : records)
		{
			NetCommandMsg x;
			std::string error;
			REQUIRE_MESSAGE(NetPacket::decodeCommand(r, x, &error), error);
			if (x.type == NETCOMMANDTYPE_GAMECOMMAND)
			{
				out.push_back(x.message->getArgument(0)->integer);
			}
		}
		return out;
	};
	issue(1, 5);
	CHECK(sink.logStream.empty()); // the other player receives it
	sink.silent = true;
	issue(2, 5); // the first command alone
	CHECK(commandsIn(sink.logStream) == std::vector<int>{ 1, 2 });
	sink.silent = false;
	issue(3, 6); // live again
	CHECK(commandsIn(sink.logStream) == std::vector<int>{ 1, 2 });
	sink.silent = true;
	issue(4, 7); // alone again: 3 (never on the log-only stream) first, then 4; 1 and 2 not again
	CHECK(commandsIn(sink.logStream) == std::vector<int>{ 1, 2, 3, 4 });
	// the real relay's log of that stream
	const std::filesystem::path dir = tempDir("r1order");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	RelayCore relay(ro);
	std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(ticketBytes(testTicket(0, 0x03)));
	REQUIRE(relay.handle(addr(10), hello.data(), hello.size(), 1000, 1700000000u).kind == RelayCore::Action::REPLY);
	std::uint32_t seq = 0;
	for (const std::vector<std::uint8_t> &r : sink.logStream)
	{
		std::vector<std::uint8_t> d = RelayProtocol::encodeData(0, transportPacket(0, seq++, r));
		relay.handle(addr(10), d.data(), d.size(), 1000, 1700000000u);
	}
	relay.shutdown(2000);
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	std::vector<int> logged;
	for (const auto &cmd : log.commands)
	{
		if (cmd.msg.type == NETCOMMANDTYPE_GAMECOMMAND)
		{
			logged.push_back(cmd.msg.message->getArgument(0)->integer);
		}
	}
	CHECK(logged == std::vector<int>{ 1, 2, 3, 4 });
	std::filesystem::remove_all(dir);
}

namespace
{
// lane ONLINE-3 r2 (Sol r2 of ONLINE-3): the rejoins of runRejoinGame over REAL Transports and UDP through a real RelayCore on the loopback (the
// mocks restart no sequence numbers and drop no queues). Three players; the slots in `restarts` crash in turn (their process stops answering) and
// start again with a rejoin ticket: catch-up from the relay's log, REJOINREQUEST and frames on the new process's streams before the switch, then
// beginLive with the log's membership (a later one rebuilds the earlier rejoins) and lockstep with CRC exchanges after it
void runRealRejoins(const std::vector<int> &restarts, bool crashWithPendingCrc)
{
	const int n = 3, kCrc = 10;
	const std::uint8_t mask = 0x07;
	const std::filesystem::path dir = tempDir("realrejoin");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	ro.silentMs = 1000;
	RelayCore relay(ro);
	UDP relaySocket;
	std::string error;
	REQUIRE_MESSAGE(relaySocket.bind(0x7F000001u, 0, &error), error);
	struct Process
	{
		std::unique_ptr<UDP> socket;
		std::unique_ptr<Transport> transport;
	};
	std::vector<Process> live((size_t)n), dead; // dead: crashed processes, kept unserviced (a crash says no BYE)
	std::vector<Peer> peers((size_t)n);
	std::array<int, MAX_SLOTS> generations{};
	auto start = [&](int slot, int generation) {
		Process pr;
		pr.socket = std::make_unique<UDP>();
		REQUIRE_MESSAGE(pr.socket->bind(0x7F000001u, 0, &error), error);
		Transport::Options o;
		o.resendMs = 5;
		o.relay.enabled = true;
		o.relay.address = relaySocket.localAddress();
		o.relay.ticket = ticketBytes(testTicket((std::uint8_t)slot, mask, (std::uint16_t)generation));
		o.relay.helloMs = 20;
		pr.transport = std::make_unique<Transport>(*pr.socket, slot, o);
		for (int p = 0; p < n; ++p)
		{
			if (p != slot)
			{
				pr.transport->setPeer(p, NetAddress{ 0x7F000001u, 1 }); // every peer is the relay
			}
		}
		Peer &peer = peers[(size_t)slot];
		peer.pending.reset(); // a process restart discards its unsent commands with the old Network
		peer.net = std::make_unique<Network>(configFor(slot, n, true, kCrc), *pr.transport);
		peer.frame = 0;
		peer.running = true;
		peer.commands = true;
		peer.relayed.clear();
		peer.hashes.clear();
		peer.hash = 0x811C9DC5u;
		peer.pending.reset(); // lane NETLAG-1: a restarted process has no input of its old one
		live[(size_t)slot] = std::move(pr);
	};
	std::uint64_t lastTick = 0;
	auto pumpRelay = [&] {
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
			RelayCore::Action a = relay.handle(RelayAddress{ from.ip, from.port }, d.data(), d.size(), NetMilliseconds(), 1700000000u);
			const std::vector<std::uint8_t> &out = a.kind == RelayCore::Action::FORWARD ? d : a.reply;
			if (a.kind != RelayCore::Action::NONE)
			{
				relaySocket.sendTo(NetAddress{ a.to.ip, a.to.port }, out.data(), out.size());
			}
		}
		if (NetMilliseconds() - lastTick >= 250)
		{
			lastTick = NetMilliseconds();
			std::vector<RelayCore::Action> status;
			relay.tick(lastTick, &status);
			for (const RelayCore::Action &a : status)
			{
				relaySocket.sendTo(NetAddress{ a.to.ip, a.to.port }, a.reply.data(), a.reply.size());
			}
		}
	};
	auto step = [&](int skip) {
		for (int s = 0; s < n; ++s)
		{
			if (s != skip)
			{
				peers[(size_t)s].step(100000);
			}
		}
		pumpRelay();
		NetSleepMilliseconds(2);
	};
	for (int s = 0; s < n; ++s)
	{
		start(s, 0);
	}
	for (int t = 0; t < 200; ++t)
	{
		step(-1);
	}
	for (Peer &p : peers)
	{
		p.net->sendLoadComplete();
	}
	for (int t = 0; t < 400; ++t)
	{
		step(-1);
	}
	// tolerance: a precondition: the game is under way (beyond its first CRC frames) before the first crash
	REQUIRE(peers[0].frame > 50);
	const NetworkConfig xc = configFor(n - 1, n, true, kCrc);
	for (size_t round = 0; round < restarts.size(); ++round)
	{
		const int X = restarts[round];
		// tolerance: slot 0 must not be the restarted slot (the harness reads the packet router's state)
		REQUIRE(X != 0);
		const int gen = ++generations[(size_t)X];
		INFO("round " << round << ": slot " << X << " restarts (generation " << gen << ")");
		if (crashWithPendingCrc)
		{
			auto hasPendingCrc = [&] {
				const auto &messages = peers[(size_t)X].pending.messages();
				return std::any_of(messages.begin(), messages.end(), [](const GameMessage &m) { return m.getType() == MSG_LOGIC_CRC; });
			};
			const std::uint64_t waitingSince = NetMilliseconds();
			while (!hasPendingCrc() && NetMilliseconds() - waitingSince < 30000)
			{
				step(-1);
			}
			REQUIRE(hasPendingCrc());
		}
		// the crash: the process stops (no BYE); after a few seconds a new one binds with the rejoin ticket
		peers[(size_t)X].running = false;
		dead.push_back(std::move(live[(size_t)X]));
		const std::uint64_t crashed = NetMilliseconds();
		while (NetMilliseconds() - crashed < 2000)
		{
			step(X);
		}
		start(X, gen);
		Peer &x = peers[(size_t)X];
		x.net->setRejoining(gen);
		x.running = false; // stepped by hand until the switch
		const RelayAddress xAddress{ 0x7F000001u, live[(size_t)X].socket->localAddress().port };
		auto held = [&] {
			bool all = true;
			for (int s = 0; s < n; ++s)
			{
				all = all && (s == X || peers[(size_t)s].net->isHeld(X));
			}
			return all;
		};
		const std::uint64_t restartedAt = NetMilliseconds();
		while ((!held() || !live[(size_t)X].transport->relayBound()) && NetMilliseconds() - restartedAt < 20000)
		{
			x.net->update(0, x.pending);
			step(X);
		}
		REQUIRE(held());
		RelayLog::Assembled a;
		auto readLog = [&] {
			std::vector<std::uint8_t> bytes;
			for (int guard = 0; guard < 10000; ++guard)
			{
				std::vector<std::uint8_t> req = RelayProtocol::encodeLogRequest((std::uint32_t)bytes.size());
				RelayCore::Action r = relay.handle(xAddress, req.data(), req.size(), NetMilliseconds(), 1700000000u);
				REQUIRE(r.kind == RelayCore::Action::REPLY);
				REQUIRE(r.reply[3] == RelayProtocol::LOG_CHUNK);
				const size_t got = r.reply.size() - RelayProtocol::kHeaderBytes - 8;
				if (got == 0)
				{
					break;
				}
				bytes.insert(bytes.end(), r.reply.begin() + (long)(RelayProtocol::kHeaderBytes + 8), r.reply.end());
			}
			RelayLogFile log;
			REQUIRE_MESSAGE(RelayLogFile::parse(bytes, log, &error, true), error);
			REQUIRE_MESSAGE(RelayLog::assemble(log, xc, a, &error, true), error);
		};
		std::map<UnsignedInt, Network::LiveStart::Crc> crcs;
		auto catchUp = [&](UnsignedInt upTo) {
			while (x.frame <= upTo && x.frame <= a.lastCompleteFrame)
			{
				CommandList out;
				const auto it = a.frames.find(x.frame);
				if (it != a.frames.end())
				{
					out = it->second;
				}
				x.ran(x.frame, out);
				++x.frame;
				const auto h = a.hashes.find(x.frame);
				if (h != a.hashes.end())
				{
					CHECK(h->second == x.hash);
				}
				if (x.frame % kCrc == 0)
				{
					Network::LiveStart::Crc c;
					c.frame = x.frame;
					c.hash = x.hash;
					c.half.frame = x.frame;
					c.half.hash = x.hash;
					c.half.slot = X;
					crcs[x.frame] = c;
				}
			}
		};
		readLog();
		catchUp(0xFFFFFFFFu);
		x.net->setRejoinReady();
		UnsignedInt join = 0xFFFFFFFFu;
		const std::uint64_t askedAt = NetMilliseconds();
		std::uint64_t lastRead = 0;
		while (join == 0xFFFFFFFFu && NetMilliseconds() - askedAt < 20000)
		{
			x.net->update(x.frame, x.pending); // the REJOINREQUEST and the others' frames: traffic on the new streams before the switch
			step(X);
			if (NetMilliseconds() - lastRead >= 200)
			{
				lastRead = NetMilliseconds();
				readLog();
				if (a.incarnations[(size_t)X] == gen + 1 && a.joinFrame[(size_t)X] != 0xFFFFFFFFu)
				{
					join = a.joinFrame[(size_t)X];
				}
			}
		}
		REQUIRE(join != 0xFFFFFFFFu);
		catchUp(join - 1);
		REQUIRE(x.frame == join);
		Network::LiveStart ls;
		ls.frame = join;
		ls.runAhead = a.runAheadAfter(join - 1, g_runAhead);
		ls.membership = a.membership;
		for (const auto &kv : crcs)
		{
			const UnsignedInt exec = kv.first + (UnsignedInt)a.runAheadAfter(kv.first - 1, g_runAhead);
			if (kv.first <= join && exec >= join)
			{
				Network::LiveStart::Crc c = kv.second;
				c.executionFrame = exec;
				ls.crcs.push_back(c);
			}
		}
		x.net->beginLive(ls);
		for (int s = 0; s < n; ++s)
		{
			if (s == X)
			{
				continue;
			}
			for (const auto &kv : a.latestFrames[(size_t)s])
			{
				if (kv.first >= join)
				{
					x.net->fillFromLog(s, kv.first, kv.second.commands, kv.second.runAheadChange, kv.second.rejoins);
				}
			}
		}
		x.running = true;
		std::array<unsigned long long, MAX_SLOTS> passedBefore{};
		for (int s = 0; s < n; ++s)
		{
			passedBefore[(size_t)s] = peers[(size_t)s].net->crcChecksPassed();
		}
		const std::uint64_t switched = NetMilliseconds();
		while (x.frame < join + 60 && NetMilliseconds() - switched < 30000)
		{
			step(-1);
		}
		// tolerance: real time over UDP: the claim is that the rejoined peer runs on after its switch (60 frames, so CRC exchanges every 10 frames follow)
		CHECK(x.frame >= join + 60);
		for (int s = 0; s < n; ++s)
		{
			INFO("peer " << s);
			const Network &net = *peers[(size_t)s].net;
			// tolerance: real time over UDP: the claim is that a CRC exchange passed after the switch; how many depends on the timing
			CHECK(net.crcChecksPassed() > passedBefore[(size_t)s]); // a CRC exchange after the switch, with the rejoined slot in it
			CHECK_FALSE(net.sawCRCMismatch());
			CHECK(net.errors().empty());
			for (const std::string &e : net.errors())
			{
				MESSAGE(e);
			}
			for (int o = 0; o < n; ++o)
			{
				if (o != s)
				{
					CHECK_FALSE(net.isHeld(o));
					CHECK_FALSE(net.slotRestarted(o));
				}
			}
			CHECK(live[(size_t)s].transport->errors().empty());
		}
	}
	// every frame each peer ran (the rejoined ones re-ran theirs from the log) is the same
	UnsignedInt common = 0xFFFFFFFFu;
	for (const Peer &p : peers)
	{
		common = std::min(common, p.frame);
	}
	for (UnsignedInt f = 0; f < common; ++f)
	{
		INFO("frame " << f);
		for (int s = 1; s < n; ++s)
		{
			CHECK(peers[(size_t)s].relayed[f] == peers[0].relayed[f]);
		}
	}
	for (Peer &p : peers)
	{
		p.net.reset();
	}
	live.clear();
	dead.clear();
	relay.shutdown(0);
	CHECK(relay.openLogHandles() == 0);
	std::filesystem::remove_all(dir);
}
} // namespace

TEST_CASE("online3 r2 (Sol r2: rejoins over real UDP): slot 1 rejoins, then slot 2, whose switch rebuilds slot 1's drop and rejoin from the log "
		  "without touching its current streams; every peer's CRC exchange after each switch passes, and the frames match")
{
	SUBCASE("crash at an arbitrary frame")
	{
		runRealRejoins({ 1, 2 }, false);
	}
	SUBCASE("crash with an unsent CRC")
	{
		runRealRejoins({ 1, 2 }, true);
	}
}

TEST_CASE("online3 r1 (Sol: repeated rejoins): the same player rejoins three times; each rejoin's frames, CRCs and log match the others'")
{
	runRejoinGame(3, false, { 2, 2, 2 });
}

TEST_CASE("online3 r1 (Sol: repeated rejoins of different slots): slot 1 rejoins, then slot 2, whose catch-up rebuilds slot 1's drop and rejoin from the "
		  "log: its transport to slot 1 is live again and slot 1's generation is the member's, so slot 1 is not taken for restarted")
{
	runRejoinGame(3, false, { 1, 2 });
}

// lane NETLAG-1: the lobby's run-ahead is 1 now (RotWK's frame + 1, RW 0x8D4301): ONLINE-3's pause, rejoin and real-UDP rejoin cases with it
TEST_CASE("netlag1: with the lobby's run-ahead 1, pause and resume, a rejoin (in a 1v1 and of 3 players, the same player three times) and rejoins over "
		  "real UDP keep every peer's frames and CRCs equal")
{
	struct Restore
	{
		~Restore() { g_runAhead = 2; }
	} restore;
	g_runAhead = 1;
	SUBCASE("pause") { runPauseGame(); }
	SUBCASE("rejoin of 3") { runRejoinGame(3); }
	SUBCASE("rejoin in a 1v1") { runRejoinGame(2); }
	SUBCASE("repeated rejoins") { runRejoinGame(3, false, { 2, 2, 2 }); }
	SUBCASE("rejoins over real UDP") { runRealRejoins({ 1, 2 }, false); }
}

TEST_CASE("online3: the acceptance stops of the lane are reported (S-3900, S-3901, S-3902)")
{
	const std::vector<std::string> stops = Network::onlineStopLines();
	REQUIRE(stops.size() == 3);
	CHECK(stops[0].rfind("[S-3900] reconnect:", 0) == 0);
	CHECK(stops[1].rfind("[S-3901] multiplayer pause:", 0) == 0);
	CHECK(stops[2].rfind("[S-3902] spectators:", 0) == 0);
}

// ---- the catch-up on the retail game ----------------------------------------------------------------------------------------------------------------
namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

struct RetailGame
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void loadRetail(starttest::Shared &s, const NewGameMessage &m, RetailGame &out)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

// the first human's mobile objects move around every 25 frames (the commands a log carries)
void moveCommands(LiveGame &game, CommandList &out)
{
	const UnsignedInt f = game.frame();
	if (f % 25 != 0)
	{
		return;
	}
	GameLogic &logic = game.logic();
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p->getPlayerType() != PLAYER_HUMAN || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, p->getPlayerIndex());
		sel.appendBooleanArgument(true);
		Coord3D at{ 0, 0, 0 };
		int n = 0;
		for (Object *o = logic.getFirstObject(); o && n < 8; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == p && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
			{
				sel.appendObjectIDArgument(o->getID());
				at = *o->getPosition();
				++n;
			}
		}
		if (n == 0)
		{
			return;
		}
		out.append(sel);
		GameMessage mv(MSG_DO_MOVETO, p->getPlayerIndex());
		at.x += (f / 25) % 2 ? 100.0f : -100.0f;
		at.z = logic.getGroundHeight(at.x, at.y);
		mv.appendLocationArgument(at);
		out.append(mv);
		return;
	}
}
} // namespace

TEST_CASE("online3: a rejoin ticket of an unknown game is refused before a game or a log is allocated (maxGames 1 stays free)")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("unknown_game_rejoin");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	o.maxGames = 1;
	RelayCore relay(o);
	auto helloOf = [&](std::uint64_t game, std::uint16_t port, std::uint16_t generation) {
		Ticket t = testTicket(0, 0x03, generation);
		t.gameId = game;
		auto d = encodeHello(ticketBytes(t));
		return relay.handle(addr(port), d.data(), d.size(), 1000, 1700000000u).reply;
	};
	auto filesIn = [&] {
		size_t n = 0;
		for (const auto &e : std::filesystem::directory_iterator(dir))
		{
			(void)e;
			++n;
		}
		return n;
	};
	for (int round = 0; round < 3; ++round) // repeated refusals must not accumulate anything
	{
		CHECK(helloOf(kGame + 1, (std::uint16_t)(30 + round), 1) == encodeRefused(REFUSE_REJOIN_BEFORE_BIND));
	}
	CHECK(relay.stats().gamesOpened == 0);
	CHECK(relay.stats().refused == 3);
	CHECK(filesIn() == 0);
	// the single game slot is still free: an unrelated first join is welcomed, and then a second game is the full one
	REQUIRE(sessionTokenOf(helloOf(kGame, 10, 0)).size() == kSessionTokenBytes);
	CHECK(relay.stats().gamesOpened == 1);
	CHECK(helloOf(kGame + 2, 11, 0) == encodeRefused(REFUSE_FULL));
	auto bye = encodeEmpty(BYE);
	relay.handle(addr(10), bye.data(), bye.size(), 3000, 1700000000u);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 retail: rejoin before first bind fails explicitly with a refusal or a legacy log missing REJOINED")
{
	using namespace RelayProtocol;
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 9876u;
	m.game.startingCash = 1500;
	auto &human = m.game.slots[0];
	human.state = SLOT_PLAYER;
	human.name = u"Human";
	human.playerTemplate = factionIndex(s->world->playerTemplates(), "FactionMen");
	human.startPos = 0;
	human.color = 0;
	human.teamNumber = 0;
	auto &ai = m.game.slots[1];
	ai.state = SLOT_MED_AI;
	ai.name = u"Computer";
	ai.playerTemplate = factionIndex(s->world->playerTemplates(), "FactionMordor");
	ai.startPos = 1;
	ai.color = 1;
	ai.teamNumber = 1;
	RetailGame g;
	loadRetail(*s, m, g);
	bool legacy = false;
	SUBCASE("named relay refusal") {}
	SUBCASE("legacy relay welcomes a first incarnation") { legacy = true; }
	UDP socket, relaySocket;
	std::string error;
	REQUIRE(socket.bind(0x7F000001u, 0, &error));
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	const auto dir = tempDir("session_before_bind");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	RelayCore relay(ro);
	auto initial = encodeHello(ticketBytes(testTicket(0, 0x01)));
	relay.handle(addr(10), initial.data(), initial.size(), 1000, 1700000000u);
	const auto log = fetchLog(relay, addr(10), 2000);
	auto bye = encodeEmpty(BYE);
	relay.handle(addr(10), bye.data(), bye.size(), 3000, 1700000000u);
	LobbyStart start;
	start.game = m;
	start.localSlot = 0;
	start.networkSlot[0] = true;
	start.online = true;
	start.onlineGameId = kGame;
	start.relay = relaySocket.localAddress();
	start.rejoinGeneration = 1;
	start.ticket = ticketBytes(testTicket(0, 0x01, 1));
	NetGameSession session(socket, start, NetGameSession::Options{});
	REQUIRE_MESSAGE(session.beginRejoin(*g.game, &error), error);
	auto state = NetGameSession::RejoinState::Binding;
	for (int step = 0; step < 100 && state != NetGameSession::RejoinState::Failed; ++step)
	{
		state = session.rejoinStep(*g.game, 0);
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
			REQUIRE(isRelayDatagram(d));
			std::vector<std::uint8_t> reply;
			if (d[3] == HELLO)
			{
				reply = legacy ? encodeWelcome(kGame, 0, {}) : encodeRefused(REFUSE_REJOIN_BEFORE_BIND);
			}
			else if (d[3] == LOG_REQUEST)
			{
				reply = encodeLogChunk(0, (std::uint32_t)log.size(), log.data(), log.size());
			}
			if (!reply.empty())
			{
				relaySocket.sendTo(from, reply.data(), reply.size());
			}
		}
		NetSleepMilliseconds(1);
	}
	CHECK(state == NetGameSession::RejoinState::Failed);
	CHECK(session.rejoinReport().framesCaughtUp == 0);
	CHECK(session.rejoinReport().failure == (legacy ? "cannot rejoin: the relay log has no REJOINED record for this player"
		: "the relay refused the rejoin ticket: cannot rejoin: the original player never connected to the relay"));
	CHECK(session.rejoinStep(*g.game, 0) == NetGameSession::RejoinState::Failed);
	std::filesystem::remove_all(dir);
}

TEST_CASE("onlineux1 r2 (Sol 1) retail: a rejoin that fails before it is live leaves nothing behind: its finish sends the relay no frame counts and no "
		  "PLAYERLEAVE (it never was a member), so the player keeps the slot")
{
	using namespace RelayProtocol;
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 9876u;
	m.game.startingCash = 1500;
	for (int i = 0; i < 2; ++i)
	{
		auto &h = m.game.slots[(size_t)i];
		h.state = SLOT_PLAYER;
		h.name = i == 0 ? u"Rejoiner" : u"Other";
		h.playerTemplate = factionIndex(s->world->playerTemplates(), i == 0 ? "FactionMen" : "FactionMordor");
		h.startPos = i;
		h.color = i;
		h.teamNumber = i;
	}
	RetailGame g;
	loadRetail(*s, m, g);
	UDP socket, relaySocket;
	std::string error;
	REQUIRE(socket.bind(0x7F000001u, 0, &error));
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	// the relay's log of the game: the slot's first incarnation only (no REJOINED of this client: the rejoin fails once the log is read)
	const auto dir = tempDir("ux1_unjoined");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	RelayCore relay(ro);
	auto initial = encodeHello(ticketBytes(testTicket(0, 0x03)));
	relay.handle(addr(10), initial.data(), initial.size(), 1000, 1700000000u);
	const auto log = fetchLog(relay, addr(10), 2000);
	LobbyStart start;
	start.game = m;
	start.localSlot = 0;
	start.networkSlot[0] = true;
	start.networkSlot[1] = true;
	start.addresses[1] = relaySocket.localAddress();
	start.online = true;
	start.onlineGameId = kGame;
	start.relay = relaySocket.localAddress();
	start.rejoinGeneration = 1;
	start.ticket = ticketBytes(testTicket(0, 0x03, 1));
	NetGameSession session(socket, start, NetGameSession::Options{});
	REQUIRE_MESSAGE(session.beginRejoin(*g.game, &error), error);
	auto state = NetGameSession::RejoinState::Binding;
	int dataDuringRejoin = 0;
	// the lockstep commands of a leave in a DATA datagram (the relay envelope, then the transport packet: Transport.h "Packet (version 2)"): frame counts
	// (FRAMEINFO) and PLAYERLEAVE; acks, keep-alives and resends of other records are not counted (a loaded machine times those differently)
	auto leaveCommands = [](const std::vector<std::uint8_t> &d) {
		int found = 0;
		auto u16 = [&](size_t at) { return (unsigned)d[at] | ((unsigned)d[at + 1] << 8); };
		auto u32 = [&](size_t at) { return (std::uint32_t)(u16(at) | (u16(at + 2) << 16)); };
		size_t q = kHeaderBytes + 1 + 12; // the envelope (header, target slot); CRC, magic, version, sender, ack
		if (d.size() < q + 2)
		{
			return found;
		}
		const unsigned count = u16(q);
		q += 2;
		for (unsigned i = 0; i < count && q + 12 <= d.size(); ++i)
		{
			const unsigned fragments = u16(q + 6);
			const std::uint32_t len = u32(q + 8);
			q += 12;
			if (q + len > d.size())
			{
				break;
			}
			NetCommandMsg c;
			if (fragments == 1 && NetPacket::decodeCommand(std::vector<std::uint8_t>(d.begin() + (long)q, d.begin() + (long)(q + len)), c, nullptr) &&
				(c.type == NETCOMMANDTYPE_FRAMEINFO || c.type == NETCOMMANDTYPE_PLAYERLEAVE))
			{
				++found;
			}
			q += len;
		}
		return found;
	};
	auto relayTurn = [&]() {
		int data = 0;
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
			REQUIRE(isRelayDatagram(d));
			std::vector<std::uint8_t> reply;
			if (d[3] == HELLO)
			{
				reply = encodeWelcome(kGame, 0, {});
			}
			else if (d[3] == LOG_REQUEST)
			{
				reply = encodeLogChunk(0, (std::uint32_t)log.size(), log.data(), log.size());
			}
			else if (d[3] == DATA)
			{
				data += leaveCommands(d);
			}
			if (!reply.empty())
			{
				relaySocket.sendTo(from, reply.data(), reply.size());
			}
		}
		return data;
	};
	for (int step = 0; step < 200 && state != NetGameSession::RejoinState::Failed; ++step)
	{
		state = session.rejoinStep(*g.game, 0);
		dataDuringRejoin += relayTurn();
		NetSleepMilliseconds(1);
	}
	REQUIRE(state == NetGameSession::RejoinState::Failed);
	CHECK(session.rejoinReport().failure == "cannot rejoin: the relay log has no REJOINED record for this player");
	CHECK(session.rejoinUnjoined());
	NetSleepMilliseconds(5);
	relayTurn();
	// the host's teardown (GameWorld::net_finish): finish, then the transport serviced while it would wait for acks
	session.finish(*g.game);
	int dataAfterFinish = 0;
	for (int i = 0; i < 20; ++i)
	{
		session.service();
		NetSleepMilliseconds(2);
		dataAfterFinish += relayTurn();
	}
	// before r2: Network::leave announced frames 0 .. the catch-up's execution frame and a PLAYERLEAVE, all as DATA to the relay
	CHECK_MESSAGE(dataAfterFinish == 0, "frame counts / PLAYERLEAVE sent by the failed rejoin's finish: " << dataAfterFinish << " (during the rejoin: " << dataDuringRejoin << ")");
	CHECK(dataDuringRejoin == 0); // a rejoining incarnation announces no frames
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 retail: the catch-up runs a game's frames from its command log and reaches the same state, the CRCs compared on the way; a wrong "
		  "CRC is reported")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 9876u;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = factionIndex(s->world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_MED_AI;
	c.name = u"Computer";
	c.playerTemplate = factionIndex(s->world->playerTemplates(), "FactionMordor");
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	const int kCrc = 50, kFrames = 320;
	// the played game: its frame lists (as a relay log assembles them) and its state hash on every frame
	auto log = std::make_shared<RelayLog::Assembled>();
	std::map<UnsignedInt, std::uint32_t> played;
	UnsignedInt first = 0;
	{
		RetailGame g;
		loadRetail(*s, m, g);
		first = g.game->frame();
		struct Recorder : LiveGameFrameDriver
		{
			RelayLog::Assembled *log = nullptr;
			std::map<UnsignedInt, std::uint32_t> *played = nullptr;
			CommandList local;
			Capture capture() const override
			{
				Capture c;
				c.hashEveryFrame = true;
				return c;
			}
			void pump(UnsignedInt, CommandList &pending) override
			{
				for (const GameMessage &x : pending.messages())
				{
					local.append(x);
				}
				pending.reset();
			}
			bool acquire(UnsignedInt frame, FrameBatch &out) override
			{
				out.frame = frame;
				out.commands = local.messages();
				if (!out.commands.empty())
				{
					log->frames[frame] = local;
				}
				local.reset();
				return true;
			}
			void completed(const FrameCompletion &c) override { (*played)[c.frame] = c.hash; }
		} rec;
		rec.log = log.get();
		rec.played = &played;
		g.game->setFrameDriver(&rec);
		for (int i = 0; i < kFrames; ++i)
		{
			moveCommands(*g.game, g.game->commands());
			REQUIRE(g.game->advance(0.2) == 1);
		}
		g.game->setFrameDriver(nullptr);
	}
	log->anyComplete = true;
	log->lastCompleteFrame = first + kFrames - 1;
	for (const auto &kv : played)
	{
		if (kv.first % kCrc == 0)
		{
			log->hashes[kv.first] = kv.second; // the players' CRCs: the state hash every CRC interval
		}
	}
	// tolerance: a precondition: the recorded game carries commands on several frames
	REQUIRE(log->frames.size() > 8);
	// tolerance: a precondition: the recorded game has several CRC frames to compare
	REQUIRE(log->hashes.size() >= 5);
	// the catch-up: a fresh game, the log's frames up to a target frame at full speed
	const UnsignedInt target = first + 250;
	{
		RetailGame g;
		loadRetail(*s, m, g);
		REQUIRE(g.game->frame() == first);
		CommandLogCatchUp catchUp(kCrc);
		catchUp.setLog(log);
		catchUp.setTarget(target);
		g.game->setFrameDriver(&catchUp);
		int ran = 0;
		while (g.game->advance(0.2) == 1 && ran < 1000)
		{
			moveCommands(*g.game, g.game->commands()); // local input during a catch-up is ignored
			++ran;
		}
		g.game->drainFrames();
		CHECK(g.game->frame() == target + 1);
		CHECK(catchUp.reached() == target + 1);
		CHECK(g.game->logic().computeStateHash() == played.at(target + 1));
		unsigned long long expected = 0;
		for (const auto &kv : log->hashes)
		{
			expected += kv.first > first && kv.first <= target + 1 ? 1 : 0;
		}
		CHECK(catchUp.crcsCompared() == expected);
		CHECK(catchUp.crcMismatches() == 0);
		// tolerance: the claim is that local input during the catch-up was dropped; how much the test issued is incidental
		CHECK(catchUp.localInputIgnored() > 0);
		CHECK(!catchUp.crcs().empty());
		CHECK(catchUp.crcs().rbegin()->second.hash == played.at(catchUp.crcs().rbegin()->first));
		g.game->setFrameDriver(nullptr);
	}
	// a log whose CRC does not match the re-simulated state is reported with its frame
	{
		auto wrong = std::make_shared<RelayLog::Assembled>(*log);
		const UnsignedInt bad = wrong->hashes.begin()->first;
		wrong->hashes[bad] ^= 1u;
		RetailGame g;
		loadRetail(*s, m, g);
		CommandLogCatchUp catchUp(kCrc);
		catchUp.setLog(wrong);
		catchUp.setTarget(bad + 5);
		g.game->setFrameDriver(&catchUp);
		while (g.game->advance(0.2) == 1)
		{
		}
		g.game->drainFrames();
		CHECK(catchUp.crcMismatches() == 1);
		CHECK(catchUp.firstMismatch().find("frame " + std::to_string(bad)) == 0);
		g.game->setFrameDriver(nullptr);
	}
}

TEST_CASE("online3 r1 retail (Sol: ownership): who owns a dropped player's army at each step: its own while held, the ally's when the rejoin window ends "
		  "or a vote decides, the ally's at the drop on a LAN")
{
	// the network game of the tests above (three peers, slot 2's process dies) drives a real RotWK game with peer 0's frames: what the packet router
	// decides reaches the logic as retail's MSG_SELF_DESTRUCT { TRUE } (RW 0x77CA3D: transferAssetsFromThat, RW 0x6AF5B4), and the objects say who
	// owns them. Slots 0 and 2 are allies (team 0), slot 1 the enemy
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = 4321u;
	m.game.startingCash = 1500;
	const char *const factions[] = { "FactionMen", "FactionMordor", "FactionElves" };
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &h = m.game.slots[(size_t)i];
		h.state = SLOT_PLAYER;
		h.name = std::u16string(u"Player") + (char16_t)(u'0' + i);
		h.playerTemplate = factionIndex(s->world->playerTemplates(), factions[i]);
		// tolerance: -1 is the sentinel for a faction not found
		REQUIRE(h.playerTemplate >= 0);
		h.startPos = i;
		h.color = i;
		h.teamNumber = i == 1 ? 1 : 0;
	}
	enum class Way
	{
		Window,
		Vote,
		Lan
	};
	for (Way way : { Way::Window, Way::Vote, Way::Lan })
	{
		INFO("way " << (way == Way::Window ? "window" : way == Way::Vote ? "vote" : "LAN"));
		RetailGame rg;
		loadRetail(*s, m, rg);
		LiveGame &game = *rg.game;
		const LiveGame::Report rep = game.report();
		// tolerance: a precondition: the report names the three start slots' players (it may list more slots)
		REQUIRE(rep.startSlotPlayers.size() >= 3);
		Player *slotPlayer[3] = {};
		for (int i = 0; i < 3; ++i)
		{
			slotPlayer[i] = game.players().findPlayerWithName(rep.startSlotPlayers[(size_t)i]);
			REQUIRE(slotPlayer[i]);
		}
		struct Feed : LiveGameFrameDriver
		{
			std::deque<CommandList> queue;
			Capture capture() const override { return Capture(); }
			void pump(UnsignedInt, CommandList &pending) override { pending.reset(); }
			bool acquire(UnsignedInt frame, FrameBatch &out) override
			{
				if (queue.empty())
				{
					return false;
				}
				out.frame = frame;
				out.commands = queue.front().messages();
				queue.pop_front();
				return true;
			}
			void completed(const FrameCompletion &) override {}
		} feed;
		game.setFrameDriver(&feed);
		Game g(3, way != Way::Lan);
		// peer 0's frames, its players' indices (10 + slot) as the game's
		g.peers[0].onRan = [&](UnsignedInt, const CommandList &out) {
			CommandList mapped;
			for (const GameMessage &msg : out.messages())
			{
				GameMessage copy(msg);
				const int slot = msg.getPlayerIndex() - 10;
				// tolerance: a range check of the slot a fake player index maps to
				REQUIRE(slot >= 0);
				// tolerance: a range check of the slot a fake player index maps to
				REQUIRE(slot < 3);
				copy.friend_setPlayerIndex(slotPlayer[slot]->getPlayerIndex());
				mapped.append(copy);
			}
			feed.queue.push_back(mapped);
		};
		auto run = [&] {
			while (!feed.queue.empty())
			{
				REQUIRE(game.advance(0.2) == 1);
			}
		};
		auto tick = [&] {
			g.tick(100000, 200);
			run();
		};
		// slot 2's army: what transferAssetsFromThat hands over (not INERT, MOVE_ONLY or IGNORED_IN_GUI)
		auto armyOf = [&](const Player *p) {
			std::set<ObjectID> ids;
			for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == p && !o->isEffectivelyDead() && !o->isKindOfName("INERT") && !o->isKindOfName("MOVE_ONLY")
					&& !o->isKindOfName("IGNORED_IN_GUI"))
				{
					ids.insert(o->getID());
				}
			}
			return ids;
		};
		auto ownedBy = [&](const std::set<ObjectID> &ids, const Player *p) {
			size_t n = 0;
			for (ObjectID id : ids)
			{
				const Object *o = game.logic().findObjectByID(id);
				n += o && o->getControllingPlayer() == p ? 1 : 0;
			}
			return n;
		};
		for (int t = 0; t < 60; ++t)
		{
			tick();
		}
		const std::set<ObjectID> army = armyOf(slotPlayer[2]);
		// tolerance: a precondition: slot 2 has an army to hand over (its exact set is what the checks follow, by id)
		REQUIRE(army.size() > 0);
		const std::set<ObjectID> allyBefore = armyOf(slotPlayer[0]);
		g.hub.silence(2, true);
		g.peers[2].running = false;
		if (way == Way::Lan)
		{
			for (int t = 0; t < 400 && g.peers[0].selfDestructFrames.empty(); ++t)
			{
				CHECK(ownedBy(army, slotPlayer[2]) == army.size()); // its own until the drop
				tick();
			}
			REQUIRE(g.peers[0].selfDestructFrames.size() == 1);
		}
		else if (way == Way::Vote)
		{
			bool voted = false;
			for (int t = 0; t < 400 && g.peers[0].selfDestructFrames.empty(); ++t)
			{
				CHECK(ownedBy(army, slotPlayer[2]) == army.size());
				tick();
				if (!voted && g.peers[0].net->disconnectManager().screen().visible && g.peers[1].net->disconnectManager().screen().visible)
				{
					g.peers[0].net->voteForPlayerDisconnect(2);
					g.peers[1].net->voteForPlayerDisconnect(2);
					voted = true;
				}
			}
			REQUIRE(voted);
			REQUIRE(g.peers[0].selfDestructFrames.size() == 1);
		}
		else
		{
			std::uint64_t heldAt = 0;
			for (int t = 0; t < 400 && heldAt == 0; ++t)
			{
				tick();
				heldAt = g.peers[0].net->isHeld(2) ? g.hub.clock : 0;
			}
			// tolerance: 0 is the sentinel for never held
			REQUIRE(heldAt != 0);
			// held: the game goes on and the army stays slot 2's for the whole window (20 s here)
			while (g.hub.clock < heldAt + 19000)
			{
				tick();
				REQUIRE(g.peers[0].selfDestructFrames.empty());
				CHECK(ownedBy(army, slotPlayer[2]) == army.size());
			}
			while (g.hub.clock < heldAt + 30000 && g.peers[0].selfDestructFrames.empty())
			{
				tick();
			}
			REQUIRE(g.peers[0].selfDestructFrames.size() == 1);
		}
		// the hand-over ran: every object of the army that still exists is the ally's, none is slot 2's, the enemy got nothing
		for (int t = 0; t < 3; ++t)
		{
			tick();
		}
		// nobody fought: the whole army is still there, and all of it is the ally's (beside what the ally had)
		CHECK(ownedBy(army, slotPlayer[0]) == army.size());
		CHECK(ownedBy(army, slotPlayer[2]) == 0);
		CHECK(ownedBy(army, slotPlayer[1]) == 0);
		const std::set<ObjectID> allyNow = armyOf(slotPlayer[0]);
		CHECK(std::includes(allyNow.begin(), allyNow.end(), allyBefore.begin(), allyBefore.end()));
		CHECK(std::includes(allyNow.begin(), allyNow.end(), army.begin(), army.end()));
		CHECK(g.peers[0].selfDestructFrames == g.peers[1].selfDestructFrames);
		game.setFrameDriver(nullptr);
	}
}
