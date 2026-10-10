// OpenBFME unit tests. GPL-3.0.
// Lane MP-1: the lockstep network without retail data: the GameMessage / net command encoding, the frame model (run-ahead, frame infos, the readiness rule,
// the slot-then-issue order of a frame's commands, the sender's player), the CRC exchange and the desync report, a leaving player, the replay file, and
// the UDP transport over localhost with loss. The expectations follow the ZH functions named in GameNetwork/Network.h.

#include "doctest.h"

#include "Common/Recorder.h"
#include "Common/Crc32.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/Transport.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace
{
// An in-memory network of peers: every datagram is delivered after a per-link delay in "ticks" (reliable, in order, like Transport)
class Hub
{
public:
	struct Endpoint : NetTransport
	{
		Hub *hub = nullptr;
		int slot = 0;
		std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
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
		std::set<int> retired;
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
	void setDelay(int from, int to, int ticks) { m_delay[{ from, to }] = ticks; }
	void queue(int from, int to, const std::vector<std::uint8_t> &b)
	{
		const int d = m_delay.count({ from, to }) ? m_delay[{ from, to }] : 0;
		m_inFlight.push_back(Flight{ m_now + d, from, to, b });
	}
	void tick()
	{
		++m_now;
		std::deque<Flight> keep;
		for (Flight &f : m_inFlight)
		{
			if (f.at <= m_now)
			{
				m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			}
			else
			{
				keep.push_back(f);
			}
		}
		m_inFlight.swap(keep);
	}

private:
	struct Flight
	{
		int at, from, to;
		std::vector<std::uint8_t> bytes;
	};
	std::vector<std::unique_ptr<Endpoint>> m_endpoints;
	std::map<std::pair<int, int>, int> m_delay;
	std::deque<Flight> m_inFlight;
	int m_now = 0;
};

NetworkConfig configFor(int slot, int peers, int runAhead, int crcInterval)
{
	NetworkConfig c;
	c.localSlot = slot;
	for (int i = 0; i < peers; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = 10 + i; // player index != slot on purpose
	}
	c.runAhead = runAhead;
	c.crcInterval = crcInterval;
	return c;
}

GameMessage moveMessage(int player, float x)
{
	GameMessage m(MSG_DO_MOVETO, player);
	m.appendLocationArgument(Coord3D{ x, 2.5f, -1.0f });
	return m;
}
} // namespace

TEST_CASE("net core: a GameMessage and every net command survive the encoding bit for bit; malformed input is rejected, never truncated")
{
	GameMessage m(MSG_QUEUE_UNIT_CREATE, 3);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument(-123456);
	m.appendRealArgument(-0.0f);
	m.appendObjectIDArgument(0xDEADBEEFu);
	m.appendLocationArgument(Coord3D{ 1.0e-38f, 3.25f, -7.5f });
	NetByteWriter w;
	std::string error;
	REQUIRE(NetPacket::writeGameMessage(w, m, &error));
	NetByteReader r(w.data());
	std::unique_ptr<GameMessage> back = NetPacket::readGameMessage(r, &error);
	REQUIRE_MESSAGE(back, error);
	CHECK(r.atEnd());
	CHECK(back->getType() == MSG_QUEUE_UNIT_CREATE);
	CHECK(back->getPlayerIndex() == 3);
	REQUIRE(back->getArgumentCount() == 5);
	CHECK(back->getArgument(1)->integer == -123456);
	CHECK(std::signbit(back->getArgument(2)->real)); // -0.0f stays -0.0f
	CHECK(back->getArgument(3)->objectID == 0xDEADBEEFu);
	CHECK(back->getArgument(4)->location.x == 1.0e-38f);
	CHECK(back->getArgument(4)->location.z == -7.5f);

	// every truncation of the bytes fails
	for (size_t n = 0; n < w.data().size(); ++n)
	{
		NetByteReader t(w.data().data(), n);
		CHECK(NetPacket::readGameMessage(t, &error) == nullptr);
	}
	// a non-logic type is refused both ways
	GameMessage bad(999, 0);
	NetByteWriter w2;
	CHECK_FALSE(NetPacket::writeGameMessage(w2, bad, &error));

	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = 5;
	c.executionFrame = 77;
	c.commandCount = 3;
	std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
	NetCommandMsg d;
	REQUIRE(NetPacket::decodeCommand(bytes, d, &error));
	CHECK(d.type == NETCOMMANDTYPE_FRAMEINFO);
	CHECK(d.slot == 5);
	CHECK(d.executionFrame == 77);
	CHECK(d.commandCount == 3);
	bytes.push_back(0);
	CHECK_FALSE(NetPacket::decodeCommand(bytes, d, &error)); // trailing bytes
	bytes[0] = 99;
	CHECK_FALSE(NetPacket::decodeCommand(bytes, d, &error)); // unknown type
	// RotWK's NetCommandType numbering (RW 0x989449)
	CHECK(NETCOMMANDTYPE_FRAMEINFO == 3);
	CHECK(NETCOMMANDTYPE_GAMECOMMAND == 4);
	CHECK(NETCOMMANDTYPE_PLAYERLEAVE == 10);
	CHECK(NETCOMMANDTYPE_KEEPALIVE == 12);
	CHECK(NETCOMMANDTYPE_LOADCOMPLETE == 16);

	// the lobby's START carries the whole game
	LobbyStart s;
	s.game.game.mapName = "maps/x/x.map";
	s.game.game.seed = 99;
	s.game.game.slots[2].state = SLOT_PLAYER;
	s.game.game.slots[2].name = u"Grün";
	s.game.game.slots[2].playerTemplate = 4;
	s.runAhead = 3;
	s.crcInterval = 50;
	s.networkSlot[2] = true;
	s.addresses[2] = NetAddress{ 0x7F000001u, 4321 };
	LobbyStart s2;
	REQUIRE(LANLobby::decodeStart(LANLobby::encodeStart(s), s2, &error));
	CHECK(s2.game.game == s.game.game);
	CHECK(s2.runAhead == 3);
	CHECK(s2.crcInterval == 50);
	CHECK(s2.networkSlot[2]);
	CHECK(s2.addresses[2] == s.addresses[2]);
}

TEST_CASE("net core: lockstep: a command issued on frame G runs on G + run-ahead on every peer, slot by slot in issue order, as its sender's player")
{
	const int kPeers = 3, kRunAhead = 3;
	Hub hub(kPeers);
	hub.setDelay(0, 1, 2); // uneven links: slot 1 hears slot 0 two ticks late
	hub.setDelay(2, 0, 1);
	std::vector<std::unique_ptr<Network>> nets;
	std::vector<CommandList> pending(kPeers);
	std::vector<UnsignedInt> frame(kPeers, 0);
	std::vector<std::vector<std::string>> executed(kPeers);
	std::vector<long long> issuedOn(kPeers, -1); // one issue per frame (a stalled peer sees the same frame on several ticks)
	for (int i = 0; i < kPeers; ++i)
	{
		nets.push_back(std::make_unique<Network>(configFor(i, kPeers, kRunAhead, 0), hub.endpoint(i)));
	}
	unsigned long long stalls = 0;
	for (int tick = 0; tick < 400; ++tick)
	{
		for (int p = 0; p < kPeers; ++p)
		{
			// every peer issues a command on some frames (slot 1 two of them)
			if (frame[p] % 4 == (UnsignedInt)p && frame[p] < 40 && issuedOn[(size_t)p] != (long long)frame[p])
			{
				issuedOn[(size_t)p] = (long long)frame[p];
				pending[p].append(moveMessage(99, (float)(frame[p] * 10 + p)));
				if (p == 1)
				{
					pending[p].append(GameMessage(MSG_DO_STOP, 99));
				}
			}
			nets[(size_t)p]->update(frame[p], pending[p]);
			if (frame[p] < 60 && nets[(size_t)p]->isFrameReady(frame[p]))
			{
				CommandList out;
				nets[(size_t)p]->relayCommands(frame[p], out);
				for (const GameMessage &m : out.messages())
				{
					executed[(size_t)p].push_back(std::to_string(frame[p]) + ":" + std::to_string(m.getPlayerIndex()) + ":" + std::to_string(m.getType()) + ":"
						+ (m.getArgumentCount() ? std::to_string((int)m.getArgument(0)->location.x) : std::string("-")));
				}
				++frame[p];
			}
			else if (frame[p] < 60)
			{
				++stalls;
			}
		}
		hub.tick();
	}
	for (int p = 0; p < kPeers; ++p)
	{
		CHECK(frame[p] == 60);
		CHECK(nets[(size_t)p]->errors().empty());
	}
	REQUIRE(!executed[0].empty());
	CHECK(executed[1] == executed[0]);
	CHECK(executed[2] == executed[0]);
	// the first command: slot 0 issued it on frame 0 -> it runs on frame 3 as player 10 (the slot's player, not the 99 it was issued with)
	CHECK(executed[0][0] == "3:10:1071:0");
	// frame 4: slot 1's two commands (issued on frame 1) in issue order
	CHECK(executed[0][1] == "4:11:1071:11");
	CHECK(executed[0][2] == "4:11:1077:-");
	CHECK(stalls > 0); // the delayed links made peers wait (and nothing ran early)
}

TEST_CASE("net core: the CRC exchange: equal hashes pass; a differing one is a desync report naming the frame, both hashes and the differing subsystem")
{
	Hub hub(2);
	Network a(configFor(0, 2, 2, 10), hub.endpoint(0));
	Network b(configFor(1, 2, 2, 10), hub.endpoint(1));
	// frameCompleted needs a GameLogic; the exchange itself is driven here with the halves Network keeps, so emulate it with MSG_LOGIC_CRC messages:
	// a peer's CRC for frame F is a MSG_LOGIC_CRC sent on F (executes on F + run-ahead on both)
	CommandList pa, pb;
	UnsignedInt f = 0, issued = 0;
	for (int tick = 0; tick < 100 && f < 30; ++tick)
	{
		if ((f == 10 || f == 20) && issued != f)
		{
			issued = f;
			GameMessage ca(MSG_LOGIC_CRC, 0), cb(MSG_LOGIC_CRC, 1);
			ca.appendIntegerArgument((int)(0x1000 + f));
			ca.appendBooleanArgument(false);
			cb.appendIntegerArgument((int)(f == 20 ? 0x2000 : 0x1000 + f)); // frame 20 differs
			cb.appendBooleanArgument(false);
			pa.append(ca);
			pb.append(cb);
		}
		a.update(f, pa);
		b.update(f, pb);
		hub.tick();
		a.update(f, pa);
		b.update(f, pb);
		if (a.isFrameReady(f) && b.isFrameReady(f))
		{
			CommandList oa, ob;
			a.relayCommands(f, oa);
			b.relayCommands(f, ob);
			++f;
		}
	}
	CHECK(f == 30);
	CHECK(a.crcChecksPassed() == 1);
	REQUIRE(a.desyncs().size() == 1);
	REQUIRE(b.desyncs().size() == 1);
	const DesyncReport &r = a.desyncs()[0];
	CHECK(r.checkedOnFrame == 22);
	CHECK(r.crcs.at(0) == 0x1014u);
	CHECK(r.crcs.at(1) == 0x2000u);

	// the report text from two halves (as exchanged after the mismatch)
	DesyncReport rep;
	rep.frame = 200;
	rep.checkedOnFrame = 202;
	rep.crcs = { { 0, 0x11111111u }, { 1, 0x22222222u } };
	DesyncHalf h0, h1;
	h0.slot = 0;
	h0.frame = 200;
	h0.sections = { { "frame, object ids, RNG", 1 }, { "objects", 2 }, { "players and teams", 3 } };
	h0.objects = { { 7, "GondorFighter", 5 }, { 8, "MordorOrc", 6 } };
	h1 = h0;
	h1.slot = 1;
	h1.sections[2].value = 4;
	h1.objects[1].value = 9;
	DesyncHalf h1back;
	REQUIRE(DesyncHalf::decode(h1.encode(), h1back, nullptr));
	CHECK(h1back.sections[2].value == 4);
	CHECK(h1back.objects[1].templateName == "MordorOrc");
	rep.halves[0] = h0;
	rep.halves[1] = h1back;
	REQUIRE(rep.differingSections() == std::vector<std::string>{ "players and teams" });
	const std::string text = rep.text();
	MESSAGE(text);
	CHECK(text.find("logic frame 200 differs") != std::string::npos);
	CHECK(text.find("0x11111111") != std::string::npos);
	CHECK(text.find("players and teams  0x00000003  0x00000004  <== DIFFERS") != std::string::npos);
	CHECK(text.find("object 8 (MordorOrc)") != std::string::npos);
	CHECK(text.find("object 7") == std::string::npos);
}

TEST_CASE("net core: a leaving player is not waited for after its leave frame; the others keep running in lockstep")
{
	Hub hub(2);
	Network a(configFor(0, 2, 2, 0), hub.endpoint(0));
	Network b(configFor(1, 2, 2, 0), hub.endpoint(1));
	CommandList pa, pb;
	UnsignedInt fa = 0, fb = 0;
	for (int tick = 0; tick < 200; ++tick)
	{
		a.update(fa, pa);
		if (fb == 10)
		{
			b.leave(fb); // b stops after frame 10 + run-ahead
			fb = 1000;
		}
		if (fb < 1000)
		{
			b.update(fb, pb);
		}
		hub.tick();
		if (a.isFrameReady(fa) && fa < 50)
		{
			CommandList o;
			a.relayCommands(fa, o);
			++fa;
		}
		if (fb < 1000 && b.isFrameReady(fb))
		{
			CommandList o;
			b.relayCommands(fb, o);
			++fb;
		}
	}
	CHECK(fa == 50);
	CHECK(a.hasLeft(1));
	CHECK(a.errors().empty());
}

TEST_CASE("net core: a replay file keeps the game, every frame's commands and hashes; truncation and a bad magic are errors")
{
	const std::string path = "net_core_test.replay";
	ReplayHeader h;
	h.game.game.mapName = "maps/a/a.map";
	h.game.game.seed = 1234;
	h.recordingSlot = 1;
	h.network = configFor(1, 2, 2, 100);
	{
		ReplayWriter w;
		REQUIRE(w.open(path, h, nullptr));
		CommandList l;
		l.append(moveMessage(10, 5.0f));
		w.recordFrame(3, l);
		w.recordFrame(4, CommandList()); // an empty frame writes nothing
		w.recordHash(4, 0xABCDu);
		w.close(5, 0x1234u);
	}
	ReplayFile f;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, f, &error), error);
	CHECK(f.complete);
	CHECK(f.header.game.game.seed == 1234);
	CHECK(f.header.recordingSlot == 1);
	CHECK(f.header.network.slotPlayerIndex[1] == 11);
	CHECK(f.header.network.crcInterval == 100);
	CHECK(f.frames.size() == 1);
	CHECK(f.frames.at(3).size() == 1);
	CHECK(f.hashes.at(4) == 0xABCDu);
	CHECK(f.finalFrame == 5);
	CHECK(f.finalHash == 0x1234u);
	std::ifstream in(path, std::ios::binary);
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	std::remove(path.c_str());
	std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 3);
	CHECK_FALSE(ReplayFile::parse(cut, f, &error));
	bytes[0] = 'X';
	CHECK_FALSE(ReplayFile::parse(bytes, f, &error));
}

TEST_CASE("net core: the UDP transport delivers every command once and in order over localhost with 30 % of the datagrams dropped")
{
	UDP sa, sb;
	std::string error;
	REQUIRE_MESSAGE(sa.bind(0x7F000001u, 0, &error), error);
	REQUIRE_MESSAGE(sb.bind(0x7F000001u, 0, &error), error);
	Transport::Options o;
	o.impairment.all.lossPerMille = 300;
	o.resendMs = 5;
	Transport ta(sa, 0, o), tb(sb, 1, o);
	ta.setPeer(1, sb.localAddress());
	tb.setPeer(0, sa.localAddress());
	const int kCommands = 300;
	for (int i = 0; i < kCommands; ++i)
	{
		std::vector<std::uint8_t> b(1 + (size_t)(i % 50), (std::uint8_t)i);
		b[0] = (std::uint8_t)(i & 0xFF);
		ta.sendTo(1, b, nullptr);
	}
	std::vector<std::uint8_t> big(20000, 7); // a desync report sized command travels as fragments
	ta.sendTo(1, big, nullptr);
	int got = 0;
	bool inOrder = true, bigArrived = false;
	const std::uint64_t start = NetMilliseconds();
	while ((got < kCommands || !bigArrived) && NetMilliseconds() - start < 10000)
	{
		ta.service();
		tb.service();
		int from;
		std::vector<std::uint8_t> b;
		while (tb.receive(from, b))
		{
			CHECK(from == 0);
			if (got < kCommands)
			{
				inOrder = inOrder && b[0] == (std::uint8_t)(got & 0xFF) && b.size() == 1 + (size_t)(got % 50);
				++got;
			}
			else
			{
				bigArrived = b == big;
			}
		}
		NetSleepMilliseconds(1);
	}
	CHECK(got == kCommands);
	CHECK(inOrder);
	CHECK(bigArrived);
	CHECK(ta.stats().dropped > 0);
	CHECK(ta.stats().commandsResent > 0);
	CHECK(tb.stats().badPackets == 0);
	// everything gets acked
	const std::uint64_t t2 = NetMilliseconds();
	while (!ta.allAcked(1) && NetMilliseconds() - t2 < 5000)
	{
		ta.service();
		tb.service();
		NetSleepMilliseconds(1);
	}
	CHECK(ta.allAcked(1));
	// a corrupted datagram is counted and dropped
	std::vector<std::uint8_t> junk = { 0, 0, 0, 0, 0x42, 0x46, 1, 0, 0, 0, 0, 0, 0, 0 };
	sa.sendTo(sb.localAddress(), junk.data(), junk.size());
	NetSleepMilliseconds(20);
	tb.service();
	CHECK(tb.stats().badPackets == 1);
}

TEST_CASE("net core: the network layer's acceptance stops are reported (S-720 .. S-725)")
{
	const std::vector<std::string> lines = Network::stopLines();
	REQUIRE(lines.size() == 6);
	const char *ids[] = { "[S-720]", "[S-721]", "[S-722]", "[S-723]", "[S-724]", "[S-725]" };
	for (size_t i = 0; i < lines.size(); ++i)
	{
		CHECK(lines[i].rfind(ids[i], 0) == 0);
	}
}

// ---- MP-1 review r1 regressions ---------------------------------------------------------------------------------------------------------------------
namespace
{
GameMessage bigSelection(int player, int objects)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (int i = 0; i < objects; ++i)
	{
		sel.appendObjectIDArgument((ObjectID)(1000 + i));
	}
	return sel;
}

// a CRC-valid transport packet (version 2) from `slot` with the given ack and raw record bytes
std::vector<std::uint8_t> craftPacket(int slot, std::uint32_t ack, std::uint16_t count, const std::vector<std::uint8_t> &records)
{
	NetByteWriter w;
	w.u32(0);
	w.u16(0x4642);
	w.u8(2);
	w.u8((std::uint8_t)slot);
	w.u32(ack);
	w.u16(count);
	std::vector<std::uint8_t> d = w.take();
	d.insert(d.end(), records.begin(), records.end());
	const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		d[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return d;
}

std::vector<std::uint8_t> record(std::uint32_t seq, std::uint16_t index, std::uint16_t count, const std::vector<std::uint8_t> &bytes, std::int64_t lenOverride = -1)
{
	NetByteWriter w;
	w.u32(seq);
	w.u16(index);
	w.u16(count);
	w.u32(lenOverride >= 0 ? (std::uint32_t)lenOverride : (std::uint32_t)bytes.size());
	std::vector<std::uint8_t> d = w.take();
	d.insert(d.end(), bytes.begin(), bytes.end());
	return d;
}
} // namespace

TEST_CASE("net core r1 (fix 1): a selection of 255+ objects, an order and another command in the same frame reach every peer and the replay")
{
	// the encoding: 256 and 300 arguments round-trip (format 2: u32 counts); a count the bytes cannot hold is refused before allocating
	for (int n : { 255, 299, 5000 })
	{
		const GameMessage sel = bigSelection(4, n);
		NetByteWriter w;
		std::string error;
		REQUIRE_MESSAGE(NetPacket::writeGameMessage(w, sel, &error), error);
		NetByteReader r(w.data());
		std::unique_ptr<GameMessage> back = NetPacket::readGameMessage(r, &error);
		REQUIRE_MESSAGE(back, error);
		CHECK(back->getArgumentCount() == (size_t)n + 1);
		CHECK(back->getArgument((size_t)n)->objectID == (ObjectID)(1000 + n - 1));
	}
	{
		NetByteWriter w;
		w.u16(MSG_DO_STOP);
		w.u8(0);
		w.u32(0xFFFFFFFFu); // a hostile count
		NetByteReader r(w.data());
		std::string error;
		CHECK(NetPacket::readGameMessage(r, &error) == nullptr);
		CHECK(error.find("argument count") != std::string::npos);
	}

	Hub hub(2);
	Network a(configFor(0, 2, 2, 0), hub.endpoint(0));
	Network b(configFor(1, 2, 2, 0), hub.endpoint(1));
	CommandList pa, pb;
	pa.append(bigSelection(0, 300));
	pa.append(moveMessage(0, 42.0f));
	pa.append(GameMessage(MSG_DO_STOP, 0));
	std::vector<std::string> ea, eb;
	UnsignedInt f = 0;
	for (int tick = 0; tick < 40 && f < 6; ++tick)
	{
		a.update(f, pa);
		b.update(f, pb);
		hub.tick();
		a.update(f, pa);
		b.update(f, pb);
		if (a.isFrameReady(f) && b.isFrameReady(f))
		{
			CommandList oa, ob;
			a.relayCommands(f, oa);
			b.relayCommands(f, ob);
			for (const GameMessage &m : oa.messages())
			{
				ea.push_back(std::to_string(f) + ":" + std::to_string(m.getType()) + ":" + std::to_string(m.getArgumentCount()));
			}
			for (const GameMessage &m : ob.messages())
			{
				eb.push_back(std::to_string(f) + ":" + std::to_string(m.getType()) + ":" + std::to_string(m.getArgumentCount()));
			}
			++f;
		}
	}
	CHECK(a.errors().empty());
	CHECK(b.errors().empty());
	CHECK(ea == std::vector<std::string>{ "2:1001:301", "2:1071:1", "2:1077:0" });
	CHECK(eb == ea);

	// a message beyond the documented limit is an error the network reports (never a silent loss), and the replay records it as lost
	CommandList over;
	over.append(bigSelection(0, (int)NetPacket::kMaxGameMessageArguments)); // + the boolean: one beyond the limit
	a.update(f, over);
	REQUIRE(!a.errors().empty());
	CHECK(a.errors().back().find("arguments (at most") != std::string::npos);

	const std::string path = "net_core_r1_selection.replay";
	ReplayHeader h;
	h.profile = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	{
		ReplayWriter w;
		REQUIRE(w.open(path, h, nullptr));
		CommandList frame;
		frame.append(bigSelection(10, 300));
		frame.append(bigSelection(10, (int)NetPacket::kMaxGameMessageArguments));
		frame.append(moveMessage(10, 7.0f));
		frame.append(GameMessage(MSG_DO_STOP, 10));
		w.recordFrame(5, frame);
		REQUIRE(w.errors().size() == 1);
		CHECK(w.errors()[0].find("frame 5 command 1") != std::string::npos);
		w.close(6, 0);
	}
	ReplayFile rf;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, rf, &error), error);
	std::remove(path.c_str());
	REQUIRE(rf.frames.at(5).size() == 3); // the other commands of the frame are kept
	CHECK(rf.frames.at(5)[0].getArgumentCount() == 301);
	CHECK(rf.frames.at(5)[1].getType() == MSG_DO_MOVETO);
	CHECK(rf.frames.at(5)[2].getType() == MSG_DO_STOP);
	REQUIRE(rf.lost.size() == 1);
	CHECK(rf.lost[0].find("frame 5 command 1") != std::string::npos);
}

TEST_CASE("net core r1 (fix 2): a 104,035-byte command and a small one after it arrive whole and in order under loss and reordering; datagrams stay "
		  "within the packet size; an oversized command is refused before it gets a sequence number")
{
	UDP sa, sb, spy;
	std::string error;
	REQUIRE(sa.bind(0x7F000001u, 0, &error));
	REQUIRE(sb.bind(0x7F000001u, 0, &error));
	REQUIRE(spy.bind(0x7F000001u, 0, &error)); // forwards both ways and measures every datagram
	Transport::Options o;
	o.impairment.all.lossPerMille = 200;
	o.impairment.all.latencyMs = 8;
	o.impairment.all.jitterMs = 7; // reorders
	o.resendMs = 10;
	o.maxCommandBytes = 1u << 20;
	Transport ta(sa, 0, o), tb(sb, 1, o);
	ta.setPeer(1, spy.localAddress());
	tb.setPeer(0, spy.localAddress());
	std::vector<std::uint8_t> report(104035);
	for (size_t i = 0; i < report.size(); ++i)
	{
		report[i] = (std::uint8_t)(i * 31u + 7u);
	}
	std::vector<std::uint8_t> tooBig((1u << 20) + 1, 1);
	CHECK_FALSE(ta.sendTo(1, tooBig, &error)); // refused first: it never takes a sequence number
	CHECK(error.find("bytes") != std::string::npos);
	REQUIRE(ta.sendTo(1, report, &error));
	REQUIRE(ta.sendTo(1, std::vector<std::uint8_t>{ 1, 2, 3 }, &error));
	size_t biggest = 0;
	std::vector<std::vector<std::uint8_t>> got;
	const std::uint64_t start = NetMilliseconds();
	while (got.size() < 2 && NetMilliseconds() - start < 20000)
	{
		ta.service();
		tb.service();
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (spy.receiveFrom(from, d))
		{
			biggest = std::max(biggest, d.size());
			spy.sendTo(from == sa.localAddress() ? sb.localAddress() : sa.localAddress(), d.data(), d.size());
		}
		int slot;
		std::vector<std::uint8_t> b;
		while (tb.receive(slot, b))
		{
			got.push_back(b);
		}
		NetSleepMilliseconds(1);
	}
	REQUIRE(got.size() == 2);
	CHECK(got[0] == report);
	CHECK(got[1] == std::vector<std::uint8_t>{ 1, 2, 3 });
	CHECK(biggest <= o.packetBytes);
	CHECK(ta.stats().fragmentedCommands == 1);
	CHECK(ta.stats().dropped > 0);
	CHECK(tb.errors().empty());
	MESSAGE("fragments sent " << ta.stats().recordsSent << ", resent " << ta.stats().commandsResent << ", dropped " << ta.stats().dropped << ", biggest datagram "
							  << biggest);
}

TEST_CASE("net core r1 (fix 4): a malformed packet changes no delivery state: truncated records, trailing bytes, a bad later record, an ack beyond the "
		  "sequence numbers issued")
{
	UDP sa, sb;
	std::string error;
	REQUIRE(sa.bind(0x7F000001u, 0, &error));
	REQUIRE(sb.bind(0x7F000001u, 0, &error));
	Transport tb(sb, 1, Transport::Options());
	tb.setPeer(0, sa.localAddress());
	REQUIRE(tb.sendTo(0, std::vector<std::uint8_t>{ 9, 9 }, &error)); // one record outstanding (sequence 0)
	tb.service();
	REQUIRE_FALSE(tb.allAcked(0));
	auto deliverRaw = [&](const std::vector<std::uint8_t> &d) {
		sa.sendTo(sb.localAddress(), d.data(), d.size());
		NetSleepMilliseconds(15);
		tb.service();
	};
	const std::vector<std::uint8_t> good = record(0, 0, 1, { 5, 6, 7 });
	const std::vector<std::vector<std::uint8_t>> bad = {
		craftPacket(0, 1, 1, std::vector<std::uint8_t>(good.begin(), good.begin() + 6)),         // a truncated record header, with a valid ack
		craftPacket(0, 1, 1, record(0, 0, 1, { 5, 6, 7 }, 10)),                                // a record longer than the packet
		[&] { auto r = good; r.push_back(0xEE); return craftPacket(0, 1, 1, r); }(),             // trailing bytes
		[&] { auto r = good; auto x = record(1, 0, 1, {}, 0); r.insert(r.end(), x.begin(), x.end()); return craftPacket(0, 1, 2, r); }(), // a bad later record
		craftPacket(0, 2, 1, good),                                                          // ack 2: only sequence 0 was issued
		craftPacket(0, 1, 1, record(0, 2, 2, { 1 })),                                        // fragment index >= count
		craftPacket(0, 1, 1, record(1u << 30, 0, 1, { 1 })),                                 // beyond the receive window
	};
	for (size_t i = 0; i < bad.size(); ++i)
	{
		const unsigned long long badBefore = tb.stats().badPackets;
		deliverRaw(bad[i]);
		CHECK_MESSAGE(tb.stats().badPackets == badBefore + 1, "case " << i);
		CHECK_MESSAGE(!tb.allAcked(0), "case " << i << " acked the outstanding record");
		CHECK_MESSAGE(tb.stats().commandsDelivered == 0, "case " << i << " delivered a record");
		CHECK_MESSAGE(tb.lastHeardFrom(0) == 0, "case " << i << " counted as heard");
	}
	// the same records in a well-formed packet: acked and delivered
	deliverRaw(craftPacket(0, 1, 1, good));
	CHECK(tb.allAcked(0));
	int slot;
	std::vector<std::uint8_t> b;
	REQUIRE(tb.receive(slot, b));
	CHECK(b == std::vector<std::uint8_t>{ 5, 6, 7 });
}

TEST_CASE("net core r1 (fix 5): after a peer's graceful leave its connection is retired: survivors keep running and their outgoing queues stay bounded")
{
	const int kPeers = 3;
	std::vector<std::unique_ptr<UDP>> sockets;
	std::vector<std::unique_ptr<Transport>> transports;
	std::vector<std::unique_ptr<Network>> nets;
	std::string error;
	Transport::Options o;
	o.resendMs = 10;
	for (int i = 0; i < kPeers; ++i)
	{
		sockets.push_back(std::make_unique<UDP>());
		REQUIRE(sockets.back()->bind(0x7F000001u, 0, &error));
	}
	for (int i = 0; i < kPeers; ++i)
	{
		transports.push_back(std::make_unique<Transport>(*sockets[(size_t)i], i, o));
		for (int j = 0; j < kPeers; ++j)
		{
			if (j != i)
			{
				transports.back()->setPeer(j, sockets[(size_t)j]->localAddress());
			}
		}
		nets.push_back(std::make_unique<Network>(configFor(i, kPeers, 2, 0), *transports.back()));
	}
	std::vector<UnsignedInt> frame(kPeers, 0);
	std::vector<CommandList> pending(kPeers);
	bool hostGone = false;
	size_t maxUnackedLate = 0;
	const std::uint64_t start = NetMilliseconds();
	while ((frame[1] < 600 || frame[2] < 600) && NetMilliseconds() - start < 30000)
	{
		for (int p = 0; p < kPeers; ++p)
		{
			if (p == 0 && hostGone)
			{
				transports[0]->service(); // the leaver only drains its acks
				continue;
			}
			if (p == 0 && frame[0] == 101)
			{
				nets[0]->leave(frame[0]);
				nets[0]->update(frame[0], pending[0]);
				hostGone = true;
				continue;
			}
			if (frame[(size_t)p] % 5 == 0)
			{
				pending[(size_t)p].append(moveMessage(0, (float)frame[(size_t)p]));
			}
			nets[(size_t)p]->update(frame[(size_t)p], pending[(size_t)p]);
			if (frame[(size_t)p] < 600 && nets[(size_t)p]->isFrameReady(frame[(size_t)p]))
			{
				CommandList out;
				nets[(size_t)p]->relayCommands(frame[(size_t)p], out);
				++frame[(size_t)p];
			}
		}
		if (frame[1] > 300)
		{
			maxUnackedLate = std::max(maxUnackedLate, transports[1]->unackedRecords() + transports[2]->unackedRecords());
		}
		NetSleepMilliseconds(1);
	}
	CHECK(frame[1] == 600);
	CHECK(frame[2] == 600);
	CHECK(nets[1]->hasLeft(0));
	CHECK(transports[1]->stats().peersRetired == 1);
	CHECK(transports[2]->stats().peersRetired == 1);
	CHECK_FALSE(transports[1]->isPeer(0));
	CHECK(transports[0]->allAcked(1)); // the leaver's last commands were acked before the survivors forgot it
	CHECK(transports[0]->allAcked(2));
	CHECK(maxUnackedLate < 200); // nothing accumulates for the departed peer
	CHECK(nets[1]->errors().empty());
	CHECK(nets[2]->errors().empty());
	MESSAGE("survivor resends " << transports[1]->stats().commandsResent << " / " << transports[2]->stats().commandsResent << ", max unacked after frame 300 "
								<< maxUnackedLate);
	CHECK(transports[1]->stats().commandsResent < 2000);
}

TEST_CASE("net core r1 (fix 3): the lobby refuses another profile identity before anything loads; a replay knows its identity")
{
	const ProfileIdentity ours = ProfileIdentity::compute({ MountedArchive{ "rotwk", "INI.big", "/x/INI.big", 10, "00ff" } }, RandomAlgorithm::ZH_CarryChain);
	const ProfileIdentity theirs = ProfileIdentity::compute({ MountedArchive{ "rotwk", "INI.big", "/x/INI.big", 10, "00ff" } }, RandomAlgorithm::ZH_CarryChain, { "test-feature" });
	const ProfileIdentity otherData = ProfileIdentity::compute({ MountedArchive{ "rotwk", "INI.big", "/x/INI.big", 10, "11ee" } }, RandomAlgorithm::ZH_CarryChain);
	const ProfileIdentity otherOrder = ProfileIdentity::compute(
		{ MountedArchive{ "bfme2", "INI.big", "/y/INI.big", 10, "00ff" }, MountedArchive{ "rotwk", "INI.big", "/x/INI.big", 10, "00ff" } }, RandomAlgorithm::ZH_CarryChain);
	CHECK(ours != theirs);
	CHECK(ours != otherData);
	CHECK(ours != otherOrder);
	CHECK(ProfileIdentity::compute({ MountedArchive{ "rotwk", "INI.big", "/elsewhere/INI.big", 10, "00ff" } }, RandomAlgorithm::ZH_CarryChain) == ours); // the disk path is not identity
	CHECK(ours.text.find(std::string("engine ") + ProfileIdentity::buildId()) != std::string::npos);
	CHECK(ProfileIdentity::difference(ours, otherData).find("11ee") != std::string::npos);

	auto runLobby = [&](const ProfileIdentity &hostProfile, const ProfileIdentity &joinProfile, std::string &clientError, std::vector<std::string> &hostLog) {
		UDP hs, cs;
		std::string error;
		REQUIRE(hs.bind(0x7F000001u, 0, &error));
		REQUIRE(cs.bind(0x7F000001u, 0, &error));
		NewGameMessage m;
		m.game.slots[0].state = SLOT_PLAYER;
		m.game.slots[1].state = SLOT_PLAYER;
		LANLobbyHost host(hs, m, 0, 2, 100, hostProfile);
		LANLobbyClient client(cs, hs.localAddress(), u"J", joinProfile);
		bool hostDone = false, clientDone = false;
		const std::uint64_t t0 = NetMilliseconds();
		while (NetMilliseconds() - t0 < 2000 && !(hostDone && clientDone) && client.error().empty())
		{
			hostDone = hostDone || host.poll();
			clientDone = clientDone || client.poll();
			NetSleepMilliseconds(1);
		}
		clientError = client.error();
		hostLog = host.log();
		return hostDone && clientDone;
	};
	std::string clientError;
	std::vector<std::string> hostLog;
	CHECK(runLobby(ours, ours, clientError, hostLog));
	CHECK(clientError.empty());
	CHECK_FALSE(runLobby(ours, theirs, clientError, hostLog));
	CHECK(clientError.find("another profile") != std::string::npos);
	CHECK(clientError.find("test-feature") != std::string::npos);
	REQUIRE(!hostLog.empty());
	CHECK(hostLog.back().find("refused") != std::string::npos);

	// a START of another identity (a host that did not check) is refused by the player
	LobbyStart s;
	s.profile = theirs;
	s.game.game.slots[0].state = SLOT_PLAYER; // a decodable START has a network player (lane MP-2's START validation)
	s.networkSlot[0] = true;
	LobbyStart back;
	REQUIRE(LANLobby::decodeStart(LANLobby::encodeStart(s), back, nullptr));
	CHECK(back.profile == theirs);

	ReplayFile rf;
	rf.header.profile = theirs;
	CHECK(rf.profileMismatch(ours).find("test-feature") != std::string::npos);
	rf.header.profile = ours;
	CHECK(rf.profileMismatch(ours).empty());
}

// ---- MP-1 review r2 regressions: receive caps and the ack range ---------------------------------------------------------------------------------------
namespace
{
// a Transport `tb` (slot 1) with peer 0 = `sa`'s address, for crafted packets from slot 0
struct CraftRig
{
	UDP sa, sb;
	std::unique_ptr<Transport> tb;
	explicit CraftRig(const Transport::Options &o, std::uint32_t sendStart = 0, std::uint32_t receiveStart = 0)
	{
		std::string error;
		REQUIRE(sa.bind(0x7F000001u, 0, &error));
		REQUIRE(sb.bind(0x7F000001u, 0, &error));
		tb = std::make_unique<Transport>(sb, 1, o);
		tb->setPeer(0, sa.localAddress(), sendStart, receiveStart);
	}
	void send(const std::vector<std::uint8_t> &d)
	{
		sa.sendTo(sb.localAddress(), d.data(), d.size());
		NetSleepMilliseconds(15);
		tb->service();
	}
};
} // namespace

TEST_CASE("net core r2 (fix 2): the receive caps hold before any state changes: an oversized datagram, an oversized whole command, the aggregate budget")
{
	Transport::Options o;
	o.packetBytes = 64;       // the receive ceiling is 64 as well
	o.maxCommandBytes = 32;
	CraftRig rig(o);
	std::string error;
	REQUIRE(rig.tb->sendTo(0, std::vector<std::uint8_t>{ 9 }, &error)); // one outstanding record
	auto expectRefused = [&](const std::vector<std::uint8_t> &d, const char *what) {
		const unsigned long long before = rig.tb->stats().badPackets;
		rig.send(d);
		CHECK_MESSAGE(rig.tb->stats().badPackets == before + 1, what);
		CHECK_MESSAGE(!rig.tb->allAcked(0), what << ": acked the outstanding record");
		CHECK_MESSAGE(rig.tb->stats().commandsDelivered == 0, what << ": delivered");
		CHECK_MESSAGE(rig.tb->lastHeardFrom(0) == 0, what << ": counted as heard");
	};
	// the review's probe: a CRC-valid 1,026-byte packet carrying a 1,000-byte command with a valid ack
	expectRefused(craftPacket(0, 1, 1, record(0, 0, 1, std::vector<std::uint8_t>(1000, 7))), "oversized datagram");
	// a whole command of 33 bytes in a packet within the receive ceiling is beyond maxCommandBytes (32)
	Transport::Options o2;
	o2.packetBytes = 200;
	o2.maxCommandBytes = 32;
	CraftRig rig2(o2);
	REQUIRE(rig2.tb->sendTo(0, std::vector<std::uint8_t>{ 9 }, &error));
	rig2.send(craftPacket(0, 1, 1, record(0, 0, 1, std::vector<std::uint8_t>(33, 7))));
	CHECK(rig2.tb->stats().badPackets == 1);
	CHECK_FALSE(rig2.tb->allAcked(0));
	CHECK(rig2.tb->stats().commandsDelivered == 0);
	CHECK(rig2.tb->lastHeardFrom(0) == 0);
	// the budget invariant (review r3): maxBufferedBytes is raised to maxCommandBytes + (window - 1) * (maxPacketBytes - 26)
	Transport::Options o3;
	o3.packetBytes = 64;
	o3.maxCommandBytes = 100;
	o3.maxBufferedBytes = 100;
	o3.window = 16;
	{
		CraftRig rig3(o3);
		CHECK(rig3.tb->options().maxBufferedBytes == 100u + 15u * 38u);
		// maximal valid fragmented traffic: a 100-byte command split 38 / 38 / 24 (sequences 0, 1, 2) and whole 38-byte commands in every other slot of the
		// window (sequences 3 .. 15); fragments 1 and 2 are lost first, so the receiver holds the 38-byte partial command plus 13 * 38 early bytes
		const std::vector<std::uint8_t> chunk(38, 3), tail(24, 4);
		rig3.send(craftPacket(0, 0, 1, record(0, 0, 3, chunk)));
		for (std::uint32_t seq = 3; seq <= 15; ++seq)
		{
			rig3.send(craftPacket(0, 0, 1, record(seq, 0, 1, chunk)));
		}
		CHECK(rig3.tb->stats().badPackets == 0);
		CHECK(rig3.tb->stats().commandsDelivered == 0);
		// the retries of the missing fragments are accepted (never refused for the budget) and everything drains in order
		rig3.send(craftPacket(0, 0, 1, record(1, 1, 3, chunk)));
		rig3.send(craftPacket(0, 0, 1, record(2, 2, 3, tail)));
		CHECK(rig3.tb->stats().badPackets == 0);
		CHECK(rig3.tb->stats().commandsDelivered == 14); // the 100-byte command and 13 whole ones
		int slot;
		std::vector<std::uint8_t> first;
		REQUIRE(rig3.tb->receive(slot, first));
		CHECK(first.size() == 100);
	}
	// the review's scenario: budget = command cap = 100, whole commands of 38 and 24 bytes after the fragmented one: the same progress
	{
		Transport::Options o5 = o3;
		o5.window = 16;
		CraftRig rig5(o5);
		const std::vector<std::uint8_t> chunk(38, 3), tail(24, 4), w24(24, 5);
		rig5.send(craftPacket(0, 0, 1, record(0, 0, 3, chunk)));
		rig5.send(craftPacket(0, 0, 1, record(3, 0, 1, chunk)));
		rig5.send(craftPacket(0, 0, 1, record(4, 0, 1, w24)));
		rig5.send(craftPacket(0, 0, 1, record(1, 1, 3, chunk)));
		rig5.send(craftPacket(0, 0, 1, record(2, 2, 3, tail)));
		CHECK(rig5.tb->stats().badPackets == 0);
		CHECK(rig5.tb->stats().commandsDelivered == 3);
	}
	// an overflow-checked window: a window beyond the serial half-range is clamped and reported, never wrapped
	{
		Transport::Options big;
		big.window = 0xFFFFFFFFu;
		big.maxPacketBytes = 65507;
		big.packetBytes = 65507;
		UDP u;
		std::string e;
		REQUIRE(u.bind(0x7F000001u, 0, &e));
		Transport t(u, 0, big);
		CHECK(t.options().window == 0x7FFFFFFFu);
		CHECK_FALSE(t.errors().empty());
	}
	// the datagram ceiling is not applied to the lobby's datagrams: a big foreign datagram is handed over
	std::vector<std::uint8_t> lobbyish(500, 0xAB);
	lobbyish[0] = 0x42;
	lobbyish[1] = 0x4C;
	rig.sa.sendTo(rig.sb.localAddress(), lobbyish.data(), lobbyish.size());
	NetSleepMilliseconds(15);
	rig.tb->service();
	CHECK(rig.tb->foreignDatagrams().size() == 1);
}

TEST_CASE("net core r2 (fix 3): an ack is checked against the records issued, across the 32-bit wrap: 0x80000002 after one record is refused atomically; stale "
		  "and boundary acks are accepted")
{
	// the review's probe: one record issued, ack 0x80000002 with a valid three-byte record
	{
		CraftRig rig{ Transport::Options() };
		std::string error;
		REQUIRE(rig.tb->sendTo(0, std::vector<std::uint8_t>{ 9 }, &error));
		rig.send(craftPacket(0, 0x80000002u, 1, record(0, 0, 1, { 5, 6, 7 })));
		CHECK(rig.tb->stats().badPackets == 1);
		CHECK_FALSE(rig.tb->allAcked(0));
		CHECK(rig.tb->stats().commandsDelivered == 0);
		CHECK(rig.tb->lastHeardFrom(0) == 0);
		for (std::uint32_t ack : { 2u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u })
		{
			rig.send(craftPacket(0, ack, 0, {}));
			CHECK_MESSAGE(rig.tb->stats().badPackets >= 1, "ack " << ack);
			CHECK_MESSAGE(!rig.tb->allAcked(0), "ack " << ack);
			CHECK_MESSAGE(rig.tb->lastHeardFrom(0) == 0, "ack " << ack);
		}
		// the boundary: ack 1 (everything issued) is valid and acks it; a stale ack 0 afterwards is valid and removes nothing
		rig.send(craftPacket(0, 1, 0, {}));
		CHECK(rig.tb->allAcked(0));
		CHECK(rig.tb->lastHeardFrom(0) != 0);
		const unsigned long long bad = rig.tb->stats().badPackets;
		rig.send(craftPacket(0, 0, 1, record(0, 0, 1, { 5, 6, 7 })));
		CHECK(rig.tb->stats().badPackets == bad);
		CHECK(rig.tb->stats().commandsDelivered == 1);
	}
	// across the wrap: both directions start 2 records before 2^32; three records are issued (sequence numbers FFFFFFFE, FFFFFFFF, 0, 1: four)
	{
		UDP sa, sb;
		std::string error;
		REQUIRE(sa.bind(0x7F000001u, 0, &error));
		REQUIRE(sb.bind(0x7F000001u, 0, &error));
		Transport ta(sa, 0, Transport::Options()), tb(sb, 1, Transport::Options());
		ta.setPeer(1, sb.localAddress(), 0xFFFFFFFEu, 0xFFFFFFFEu);
		tb.setPeer(0, sa.localAddress(), 0xFFFFFFFEu, 0xFFFFFFFEu);
		for (int i = 0; i < 4; ++i)
		{
			REQUIRE(ta.sendTo(1, std::vector<std::uint8_t>{ (std::uint8_t)i }, &error));
		}
		std::vector<std::uint8_t> got;
		const std::uint64_t t0 = NetMilliseconds();
		while ((got.size() < 4 || !ta.allAcked(1)) && NetMilliseconds() - t0 < 5000)
		{
			ta.service();
			tb.service();
			int from;
			std::vector<std::uint8_t> b;
			while (tb.receive(from, b))
			{
				got.push_back(b[0]);
			}
			NetSleepMilliseconds(1);
		}
		CHECK(got == std::vector<std::uint8_t>{ 0, 1, 2, 3 });
		CHECK(ta.allAcked(1));
		CHECK(ta.stats().badPackets == 0);
		CHECK(tb.stats().badPackets == 0);
		// after the wrap, ack 0x80000002 is still never-issued (4 records issued from 0xFFFFFFFE: next sequence number 2)
		const std::vector<std::uint8_t> pk = craftPacket(1, 0x80000002u, 0, {});
		sb.sendTo(sa.localAddress(), pk.data(), pk.size());
		NetSleepMilliseconds(15);
		ta.service();
		CHECK(ta.stats().badPackets == 1);
	}
}

TEST_CASE("net core r3 (fix 2): an ancient ack after more than 2^31 records issued removes nothing; acks are monotonic")
{
	CraftRig rig{ Transport::Options() };
	std::string error;
	// as if 0x80000001 records had been sent and acknowledged; then one record (sequence 0x80000001) is outstanding: issued 0x80000002
	rig.tb->fastForwardSend(0, 0x80000001ull);
	REQUIRE(rig.tb->sendTo(0, std::vector<std::uint8_t>{ 9 }, &error));
	REQUIRE_FALSE(rig.tb->allAcked(0));
	// the review's case: a duplicate ancient ack 0 passes the range check ((nextSeq - 0) == issued) but is stale: it must not remove the current record
	rig.send(craftPacket(0, 0, 0, {}));
	CHECK(rig.tb->stats().badPackets == 0);
	CHECK_FALSE(rig.tb->allAcked(0));
	// other historical values, including one a signed comparison would call "after" the outstanding record
	for (std::uint32_t ack : { 1u, 0x7FFFFFFFu, 0x80000000u })
	{
		rig.send(craftPacket(0, ack, 0, {}));
		CHECK_MESSAGE(!rig.tb->allAcked(0), "ack " << ack);
	}
	// the real ack (everything issued) acks it; a later stale ack changes nothing and a never-issued one is still refused
	rig.send(craftPacket(0, 0x80000002u, 0, {}));
	CHECK(rig.tb->allAcked(0));
	REQUIRE(rig.tb->sendTo(0, std::vector<std::uint8_t>{ 8 }, &error));
	rig.send(craftPacket(0, 0x80000002u, 0, {})); // a duplicate of the old ack: stale
	CHECK_FALSE(rig.tb->allAcked(0));
	const unsigned long long bad = rig.tb->stats().badPackets;
	rig.send(craftPacket(0, 0x80000004u, 0, {})); // beyond the 0x80000003 issued
	CHECK(rig.tb->stats().badPackets == bad + 1);
	CHECK_FALSE(rig.tb->allAcked(0));
	rig.send(craftPacket(0, 0x80000003u, 0, {}));
	CHECK(rig.tb->allAcked(0));
}
