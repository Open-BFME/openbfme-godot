// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE16-FIX (Sol r1 of lane/online16 + the AUTH-1 code review): the previous release's version-3 envelope keeps
// playing legacy games through the current relay (and the current client through a version-3-only relay), and the
// authenticated envelope's HELLO / counter / downgrade rules hold against a party that is bound with a key of its own.

#include "doctest.h"

#include "Common/Crc32.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/RelayMembership.h"
#include "GameNetwork/RelayAuth.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "RelayCore.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr std::uint64_t kGame = 0x0123456789abcdefull;
constexpr std::uint8_t kV3 = 3; // the previous release's envelope version (RelayProtocol::kVersionLegacy; spelled out so this file also builds on the base)
std::vector<std::uint8_t> testKey()
{
	std::vector<std::uint8_t> k;
	for (int i = 0; i < 32; ++i)
	{
		k.push_back((std::uint8_t)(0x40 + i));
	}
	return k;
}
RelayProtocol::KeyRing testRing()
{
	return { { 1, testKey() } };
}
RelayProtocol::Ticket testTicket(std::uint8_t slot, std::uint16_t mask)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = kGame;
	t.slot = slot;
	t.slotMask = mask;
	t.expires = 2000000000u;
	return t;
}
std::vector<std::uint8_t> playerTicket(std::uint8_t slot, std::uint16_t mask = 0x07)
{
	return RelayProtocol::signTicket(testTicket(slot, mask), testKey());
}
std::vector<std::uint8_t> rejoinTicket(std::uint8_t slot, std::uint16_t generation, std::uint16_t mask = 0x07)
{
	RelayProtocol::Ticket t = testTicket(slot, mask);
	t.generation = generation;
	return RelayProtocol::signRejoinTicket(t, testKey());
}
std::vector<std::uint8_t> spectatorTicket(std::uint8_t index, std::uint16_t mask = 0x07)
{
	RelayProtocol::Ticket t = testTicket(index, mask);
	t.spectator = true;
	t.delaySeconds = 0;
	return RelayProtocol::signSpectatorTicket(t, testKey());
}
RelayAddress addr(std::uint16_t port)
{
	return RelayAddress{ 0x0A000002u, port };
}
// the previous release's (v0.4.11) envelope: version 3, a HELLO without the capability byte
std::vector<std::uint8_t> v3(std::vector<std::uint8_t> d)
{
	d[2] = kV3;
	return d;
}
std::vector<std::uint8_t> hello3(const std::vector<std::uint8_t> &ticket, const std::vector<std::uint8_t> &token = {})
{
	std::vector<std::uint8_t> d = { 'B', 'R', 3, RelayProtocol::HELLO };
	d.insert(d.end(), ticket.begin(), ticket.end());
	d.insert(d.end(), token.begin(), token.end());
	return d;
}
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
std::vector<std::uint8_t> frameInfo(std::uint8_t slot, std::uint32_t frame)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = slot;
	c.executionFrame = frame;
	c.commandCount = 0;
	return NetPacket::encodeCommand(c, nullptr);
}
std::vector<RelayCore::Action> send(RelayCore &relay, const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now = 1000)
{
	std::vector<RelayCore::Action> out;
	RelayCore::Action a = relay.handle(from, d.data(), d.size(), now, 1700000000u);
	if (a.kind == RelayCore::Action::FORWARD)
	{
		a.reply = d;
	}
	if (a.kind != RelayCore::Action::NONE)
	{
		out.push_back(std::move(a));
	}
	relay.takePending(out);
	return out;
}
// an authenticating party: the k the lobby computed for ITS ticket, and its direction keys
struct Party
{
	RelayAddress from;
	std::vector<std::uint8_t> ticket;
	RelayAuth::Keys keys;
	std::uint64_t sent = 0;
	RelayAuth::Window recv;
	std::vector<std::uint8_t> token;
};
Party party(const std::vector<std::uint8_t> &ticket, std::uint16_t port)
{
	Party p;
	p.from = addr(port);
	p.ticket = ticket;
	RelayProtocol::Ticket f;
	REQUIRE(RelayProtocol::readTicketFields(ticket.data(), ticket.size(), f, nullptr));
	p.keys = RelayAuth::packetKeys(RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size()), f.gameId, f.slot,
		f.generation); // as Transport and the relay derive them
	return p;
}
std::vector<std::uint8_t> sealed(Party &p, std::vector<std::uint8_t> d)
{
	RelayAuth::seal(d, p.keys.c2r, p.sent++);
	return d;
}
std::vector<std::uint8_t> helloOf(Party &p)
{
	return sealed(p, RelayProtocol::encodeHello(p.ticket, p.token, RelayProtocol::CAP_AUTH));
}
// opens a sealed answer with the party's r2c key and window: the version-4 view, or empty
std::vector<std::uint8_t> open(Party &p, const RelayCore::Action &a)
{
	std::vector<std::uint8_t> d = a.reply;
	if (d.size() < 4 || d[2] != RelayProtocol::kVersionAuth)
	{
		return {};
	}
	const size_t n = RelayAuth::open(d.data(), d.size(), p.keys.r2c, &p.recv);
	d.resize(n);
	return d;
}
// binds a party over the authenticated envelope and keeps its session token
void bind(RelayCore &relay, Party &p)
{
	std::vector<RelayCore::Action> a = send(relay, p.from, helloOf(p));
	REQUIRE(a.size() == 1);
	const std::vector<std::uint8_t> w = open(p, a[0]);
	REQUIRE(w.size() == RelayProtocol::kHeaderBytes + 9 + RelayProtocol::kSessionTokenBytes);
	REQUIRE(w[3] == RelayProtocol::WELCOME);
	p.token.assign(w.end() - (long)RelayProtocol::kSessionTokenBytes, w.end());
}
// where DATA from `sender` to `target` goes now (the bound address of the target slot)
RelayAddress routeOf(RelayCore &relay, Party &sender, int target, std::uint32_t seq)
{
	std::vector<RelayCore::Action> a = send(relay, sender.from, sealed(sender, RelayProtocol::encodeData((std::uint8_t)target,
		transportPacket(sender.ticket[10], seq, frameInfo(sender.ticket[10], seq)))));
	return a.size() == 1 ? a[0].to : RelayAddress{};
}
} // namespace

// ---- finding 1 (Sol #7): the previous release's version-3 envelope through the current relay -----------------------------------------------

TEST_CASE("online16-fix: a previous-release client (v3 HELLO, v1 ticket) plays a legacy game beside a current one; every answer is in its envelope")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme_o16fix_v3_" + std::to_string(NetMilliseconds()));
	std::filesystem::create_directories(dir);
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	// slot 0: the previous release (version 3, no capability byte); slot 1: this build (version 4)
	std::vector<RelayCore::Action> a = send(relay, addr(5000), hello3(playerTicket(0)));
	REQUIRE(a.size() == 1);
	REQUIRE(a[0].reply.size() == kHeaderBytes + 9 + kSessionTokenBytes);
	CHECK(a[0].reply[2] == kV3);
	CHECK(a[0].reply[3] == WELCOME);
	const std::vector<std::uint8_t> token0(a[0].reply.end() - (long)kSessionTokenBytes, a[0].reply.end());
	a = send(relay, addr(5001), encodeHello(playerTicket(1), {}, CAP_CANONICAL));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply[2] == kVersion);
	CHECK(a[0].reply[3] == WELCOME);
	// v3 -> v4: forwarded in the target's envelope; v4 -> v3: in the previous release's
	a = send(relay, addr(5000), v3(encodeData(1, transportPacket(0, 0, frameInfo(0, 1)))));
	REQUIRE(a.size() == 1);
	CHECK(a[0].to == addr(5001));
	CHECK(a[0].reply[2] == kVersion);
	CHECK(a[0].reply[3] == DATA_FROM);
	a = send(relay, addr(5001), encodeData(0, transportPacket(1, 0, frameInfo(1, 1))));
	REQUIRE(a.size() == 1);
	CHECK(a[0].to == addr(5000));
	CHECK(a[0].reply[2] == kV3);
	CHECK(a[0].reply[3] == DATA_FROM);
	// its log-only stream's ack, its LOG_CHUNK, its rebind with the session token and its NOT_BOUND are all version 3
	a = send(relay, addr(5000), v3(encodeData(0, transportPacket(0, 0, frameInfo(0, 2)))));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply[2] == kV3);
	a = send(relay, addr(5000), v3(encodeLogRequest(0)));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply[2] == kV3);
	CHECK(a[0].reply[3] == LOG_CHUNK);
	a = send(relay, addr(5099), v3(encodeData(1, transportPacket(0, 1, frameInfo(0, 3)))));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == v3(encodeEmpty(NOT_BOUND)));
	a = send(relay, addr(5002), hello3(playerTicket(0), token0));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply[2] == kV3);
	CHECK(a[0].reply[3] == WELCOME);
	// the STATUS once slot 1 goes silent: version 3 to the previous release, version 4 to this build
	std::vector<RelayCore::Action> st;
	relay.tick(1000 + o.silentMs + 1, &st);
	REQUIRE(st.size() == 2);
	for (const RelayCore::Action &s : st)
	{
		CHECK(s.reply[3] == STATUS);
		CHECK(s.reply[2] == (s.to == addr(5002) ? kV3 : kVersion));
	}
	// a refusal of a version-3 HELLO is version 3 too (the previous release reads nothing else); an unknown version is still refused
	std::vector<std::uint8_t> bad = playerTicket(2);
	bad.back() ^= 1;
	a = send(relay, addr(5003), hello3(bad));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == v3(encodeRefused(REFUSE_BAD_TICKET)));
	std::vector<std::uint8_t> v2 = encodeHello(playerTicket(2));
	v2[2] = 2;
	a = send(relay, addr(5004), v2);
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == encodeRefused(REFUSE_VERSION));
	// the previous release cannot join a canonical game: refused by capability, in its own envelope
	RelayProtocol::Ticket c = testTicket(0, 0x03);
	c.gameId = kGame + 1;
	c.mode = TICKET_MODE_CANONICAL;
	a = send(relay, addr(5005), encodeHello(signTicket(c, testKey()), {}, CAP_CANONICAL));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply[3] == WELCOME);
	c.slot = 1;
	a = send(relay, addr(5006), hello3(signTicket(c, testKey())));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == v3(encodeRefused(REFUSE_CAPABILITY)));
	CHECK(relay.stats().badDatagrams == 1);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online16-fix: a version-3 DATA or HELLO from an authenticated binding is a downgrade: dropped, never refused (AUTH-1 review 4)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 6000), p1 = party(playerTicket(1), 6001);
	bind(relay, p0);
	bind(relay, p1);
	// a spoofed datagram in the previous release's envelope, from the victim's own address
	CHECK(send(relay, p0.from, v3(encodeData(1, transportPacket(0, 9, frameInfo(0, 9))))).empty());
	CHECK(send(relay, p0.from, hello3(p0.ticket, p0.token)).empty());
	// and an unknown version: no REFUSE_VERSION reaches an authenticated client
	std::vector<std::uint8_t> v9 = encodeData(1, transportPacket(0, 9, frameInfo(0, 9)));
	v9[2] = 9;
	CHECK(send(relay, p0.from, v9).empty());
	CHECK(relay.stats().authDropped == 3);
	CHECK(relay.stats().refused == 0);
	CHECK(routeOf(relay, p1, 0, 0) == p0.from); // the victim keeps its slot
}

#ifdef OPENBFME_RELAY_EXE
#include "TestChild.h"
#include <csignal>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
namespace
{
struct RealRelay
{
	std::filesystem::path dir;
	std::uint16_t port = 0;
	std::unique_ptr<TestChild> child;
	explicit RealRelay(const std::string &name)
	{
		dir = std::filesystem::temp_directory_path() / ("openbfme_o16fix_" + name + "_" + std::to_string(NetMilliseconds()));
		std::filesystem::create_directories(dir);
		{
			std::ofstream f(dir / "keys.txt");
			f << "1:";
			for (std::uint8_t b : testKey())
			{
				char h[3];
				snprintf(h, 3, "%02x", b);
				f << h;
			}
			f << "\n";
		}
		{
			UDP tmp;
			std::string error;
			REQUIRE(tmp.bind(0x7F000001u, 0, &error));
			port = tmp.localAddress().port;
		}
		child = std::make_unique<TestChild>(OPENBFME_RELAY_EXE,
			std::vector<std::string>{ "--bind", "127.0.0.1:" + std::to_string(port), "--keys", (dir / "keys.txt").string(), "--log-dir", dir.string(),
				"--status", (dir / "status.json").string(), "--stats-s", "0" },
			(dir / "relay.out").string());
		REQUIRE(child->started());
	}
	NetAddress address() const { return NetAddress{ 0x7F000001u, port }; }
	std::string stopAndStatus()
	{
		child->stop();
		std::ifstream f(dir / "status.json");
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}
	~RealRelay()
	{
		child.reset(); // the relay stops (and stops writing its log) before its directory goes
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
};
} // namespace

TEST_CASE("online16-fix: two previous-release clients and a current Transport play one legacy game through the real openbfme_relay")
{
	using namespace RelayProtocol;
	RealRelay relay("v3game");
	std::string error;
	// slots 0 and 1: the previous release's wire (v3 HELLO without capabilities, v1 ticket); slot 2: this build's Transport
	UDP old0, old1, cur;
	REQUIRE(old0.bind(0x7F000001u, 0, &error));
	REQUIRE(old1.bind(0x7F000001u, 0, &error));
	REQUIRE(cur.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relay.address();
	o.relay.ticket = playerTicket(2);
	o.relay.helloMs = 20;
	o.resendMs = 20;
	Transport t(cur, 2, o);
	t.setPeer(0, NetAddress{});
	t.setPeer(1, NetAddress{});
	UDP *old[2] = { &old0, &old1 };
	bool welcomed[2] = {};
	std::vector<std::vector<std::uint8_t>> heard[2]; // DATA_FROM datagrams each old client received
	const std::uint64_t deadline = NetMilliseconds() + 10000;
	auto pumpOld = [&] {
		for (int i = 0; i < 2; ++i)
		{
			NetAddress from;
			std::vector<std::uint8_t> d;
			while (old[i]->receiveFrom(from, d))
			{
				REQUIRE(d.size() >= kHeaderBytes); // tolerance: any relay datagram; its kind decides the rest
				CHECK(d[2] == kV3); // the previous release refuses every other version byte (its Transport.cpp:339)
				welcomed[i] = welcomed[i] || d[3] == WELCOME;
				if (d[3] == DATA_FROM)
				{
					heard[i].push_back(d);
				}
			}
		}
	};
	while ((!welcomed[0] || !welcomed[1] || !t.relayBound()) && NetMilliseconds() < deadline)
	{
		for (int i = 0; i < 2; ++i)
		{
			if (!welcomed[i])
			{
				const std::vector<std::uint8_t> h = hello3(playerTicket((std::uint8_t)i));
				old[i]->sendTo(relay.address(), h.data(), h.size());
			}
		}
		t.service();
		NetSleepMilliseconds(5);
		pumpOld();
	}
	REQUIRE(welcomed[0]);
	REQUIRE(welcomed[1]);
	REQUIRE(t.relayBound());
	// the current client's frame reaches both previous-release clients; theirs reach it and each other
	REQUIRE(t.sendTo(0, frameInfo(2, 7), &error));
	REQUIRE(t.sendTo(1, frameInfo(2, 7), &error));
	for (int i = 0; i < 2; ++i)
	{
		const std::vector<std::uint8_t> toCur = v3(encodeData(2, transportPacket((std::uint8_t)i, 0, frameInfo((std::uint8_t)i, 7))));
		old[i]->sendTo(relay.address(), toCur.data(), toCur.size());
	}
	const std::vector<std::uint8_t> oldToOld = v3(encodeData(1, transportPacket(0, 0, frameInfo(0, 7))));
	old0.sendTo(relay.address(), oldToOld.data(), oldToOld.size());
	bool fromOld[2] = {};
	while ((!fromOld[0] || !fromOld[1] || heard[0].empty() || heard[1].size() < 2) && NetMilliseconds() < deadline)
	{
		t.service();
		int from = -1;
		std::vector<std::uint8_t> b;
		while (t.receive(from, b))
		{
			NetCommandMsg c;
			REQUIRE(NetPacket::decodeCommand(b, c, &error));
			CHECK(c.executionFrame == 7u);
			if (from == 0 || from == 1)
			{
				fromOld[from] = true;
			}
		}
		NetSleepMilliseconds(5);
		pumpOld();
	}
	CHECK(fromOld[0]);
	CHECK(fromOld[1]);
	REQUIRE(!heard[0].empty());
	CHECK(heard[0][0][kHeaderBytes] == 2); // from the current client
	bool oldHeardOld = false;
	for (const std::vector<std::uint8_t> &d : heard[1])
	{
		oldHeardOld = oldHeardOld || d[kHeaderBytes] == 0;
	}
	CHECK(oldHeardOld);
	CHECK(t.relayRefusal() == 0);
	CHECK(t.stats().badPackets == 0);
	const std::string status = relay.stopAndStatus();
	CHECK(status.find("\"gamesOpened\":1") != std::string::npos);
	CHECK(status.find("\"authDropped\":0") != std::string::npos);
}
#endif

// ---- the reverse: the current client with a version-3-only relay (the previous release's openbfme_relay) ---------------------------------------

namespace
{
// a fake relay socket that answers like the previous release's RelayCore: anything not version 3 gets REFUSED(VERSION) in version 3
struct OldRelay
{
	UDP socket;
	std::vector<std::vector<std::uint8_t>> got;
	OldRelay()
	{
		std::string error;
		REQUIRE(socket.bind(0x7F000001u, 0, &error));
	}
	void pump(const NetAddress &client)
	{
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (socket.receiveFrom(from, d))
		{
			got.push_back(d);
			if (d.size() >= 4 && d[2] != kV3)
			{
				const std::vector<std::uint8_t> r = v3(RelayProtocol::encodeRefused(RelayProtocol::REFUSE_VERSION));
				socket.sendTo(client, r.data(), r.size());
			}
			else if (d.size() >= 4 && d[3] == RelayProtocol::HELLO)
			{
				const std::array<std::uint8_t, RelayProtocol::kSessionTokenBytes> token{ 7 };
				const std::vector<std::uint8_t> w = v3(RelayProtocol::encodeWelcome(kGame, 0, token));
				socket.sendTo(client, w.data(), w.size());
			}
		}
	}
};
} // namespace

TEST_CASE("online16-fix: a current client without a macKey plays through a version-3-only relay in the previous release's envelope")
{
	using namespace RelayProtocol;
	OldRelay relay;
	UDP client;
	std::string error;
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relay.socket.localAddress();
	o.relay.ticket = playerTicket(0);
	o.relay.helloMs = 10;
	Transport t(client, 0, o);
	t.setPeer(1, NetAddress{});
	for (int i = 0; i < 400 && !t.relayBound(); ++i)
	{
		t.service();
		NetSleepMilliseconds(2);
		relay.pump(client.localAddress());
	}
	REQUIRE(t.relayBound());
	CHECK(t.relayRefusal() == 0);
	// its HELLO after the refusal is the previous release's byte for byte (no capability byte)
	bool sawOldHello = false;
	for (const std::vector<std::uint8_t> &d : relay.got)
	{
		sawOldHello = sawOldHello || d == hello3(o.relay.ticket);
	}
	CHECK(sawOldHello);
	// DATA leaves in version 3, and a version-3 DATA_FROM is read
	relay.got.clear();
	REQUIRE(t.sendTo(1, frameInfo(0, 3), &error));
	for (int i = 0; i < 50 && relay.got.empty(); ++i)
	{
		t.service();
		NetSleepMilliseconds(2);
		relay.pump(client.localAddress());
	}
	REQUIRE(!relay.got.empty());
	CHECK(relay.got[0][2] == kV3);
	CHECK(relay.got[0][3] == DATA);
	const std::vector<std::uint8_t> in = v3(encodeData(1, transportPacket(1, 0, frameInfo(1, 3))));
	std::vector<std::uint8_t> dataFrom = in;
	dataFrom[3] = DATA_FROM;
	relay.socket.sendTo(client.localAddress(), dataFrom.data(), dataFrom.size());
	bool got = false;
	for (int i = 0; i < 100 && !got; ++i)
	{
		t.service();
		int from = -1;
		std::vector<std::uint8_t> b;
		got = t.receive(from, b) && from == 1;
		NetSleepMilliseconds(2);
	}
	CHECK(got);
	CHECK(t.stats().badPackets == 0);
}

TEST_CASE("online16-fix: a client holding a macKey (a current lobby) never drops to the version-3 envelope: the old relay is a clear refusal")
{
	using namespace RelayProtocol;
	OldRelay relay;
	UDP client;
	std::string error;
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relay.socket.localAddress();
	o.relay.ticket = playerTicket(0);
	o.relay.helloMs = 10;
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), o.relay.ticket.data(), o.relay.ticket.size());
	o.relay.macKey.assign(k.begin(), k.end());
	Transport t(client, 0, o);
	for (int i = 0; i < 100 && t.relayRefusal() == 0; ++i)
	{
		t.service();
		NetSleepMilliseconds(2);
		relay.pump(client.localAddress());
	}
	CHECK(t.relayRefusal() == REFUSE_VERSION);
	CHECK_FALSE(t.relayBound());
	for (const std::vector<std::uint8_t> &d : relay.got)
	{
		CHECK(d[2] == kVersionAuth); // nothing unauthenticated ever left
	}
	// a wide (P16) ticket cannot be read by a version-3 relay either: refused, no version-3 HELLO
	OldRelay relay2;
	Transport::Options o2;
	o2.relay.enabled = true;
	o2.relay.address = relay2.socket.localAddress();
	o2.relay.ticket = playerTicket(9, 0x0301);
	o2.relay.helloMs = 10;
	UDP client2;
	REQUIRE(client2.bind(0x7F000001u, 0, &error));
	Transport t2(client2, 9, o2);
	for (int i = 0; i < 100 && t2.relayRefusal() == 0; ++i)
	{
		t2.service();
		NetSleepMilliseconds(2);
		relay2.pump(client2.localAddress());
	}
	CHECK(t2.relayRefusal() == REFUSE_VERSION);
	for (const std::vector<std::uint8_t> &d : relay2.got)
	{
		CHECK(d[2] == kVersion); // a wide ticket never goes out in version 3
	}
}

// ---- the AUTH-1 code review: HELLO keys, HELLO replay, plain HELLO, counters, bounds ---------------------------------------------------------

TEST_CASE("online16-fix auth: a spectator bound with its own key cannot take a player slot with the player's ticket and session token (review 1)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = (std::filesystem::temp_directory_path() / ("openbfme_o16fix_spec_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(o.logDir);
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 7000), p1 = party(playerTicket(1), 7001), spec = party(spectatorTicket(0), 7100);
	bind(relay, p0);
	bind(relay, p1);
	std::vector<RelayCore::Action> a = send(relay, spec.from, helloOf(spec));
	REQUIRE(a.size() == 1);
	REQUIRE(open(spec, a[0]).size() == kHeaderBytes + 9 + kSessionTokenBytes);
	// both are visible on the wire: the player's ticket (its HELLO) and session token (its WELCOME is sealed, not encrypted)
	const std::uint64_t dropped = relay.stats().authDropped;
	a = send(relay, spec.from, sealed(spec, encodeHello(p1.ticket, p1.token, CAP_AUTH)));
	CHECK(a.empty()); // the tag is checked with the key of the HELLO's own ticket: the spectator's does not open it
	CHECK(relay.stats().authDropped == dropped + 1);
	CHECK(routeOf(relay, p0, 1, 0) == p1.from);
	// a player bound with its key cannot take another player's slot either
	a = send(relay, p0.from, sealed(p0, encodeHello(p1.ticket, p1.token, CAP_AUTH)));
	CHECK(a.empty());
	CHECK(relay.stats().authDropped == dropped + 2);
	CHECK(routeOf(relay, p0, 1, 1) == p1.from);
	std::filesystem::remove_all(o.logDir);
}

TEST_CASE("online16-fix auth: a captured token HELLO replayed from another address is dropped by the slot's window (review 2)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 7200), p1 = party(playerTicket(1), 7201);
	bind(relay, p0);
	bind(relay, p1);
	// the slot moves (a NAT rebinding): its token HELLO from the new address, captured on the way
	p0.from = addr(7210);
	const std::vector<std::uint8_t> captured = helloOf(p0);
	std::vector<RelayCore::Action> a = send(relay, p0.from, captured);
	REQUIRE(a.size() == 1);
	CHECK(open(p0, a[0])[3] == WELCOME);
	CHECK(routeOf(relay, p1, 0, 0) == p0.from);
	// and the very first HELLO, replayed
	const std::uint64_t dropped = relay.stats().authDropped;
	CHECK(send(relay, addr(7299), captured).empty());
	CHECK(relay.stats().authDropped == dropped + 1);
	CHECK(routeOf(relay, p1, 0, 1) == p0.from);
	// a fresh HELLO of the slot's own key proves ownership even from an address the relay never saw
	p0.from = addr(7220);
	a = send(relay, p0.from, helloOf(p0));
	REQUIRE(a.size() == 1);
	CHECK(open(p0, a[0])[3] == WELCOME);
	CHECK(routeOf(relay, p1, 0, 2) == p0.from);
}

TEST_CASE("online16-fix auth: a plain HELLO, token and all, is refused for a slot that authenticated (review 3)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 7300), p1 = party(playerTicket(1), 7301);
	bind(relay, p0);
	bind(relay, p1);
	std::vector<RelayCore::Action> a = send(relay, addr(7399), encodeHello(p0.ticket, p0.token, CAP_CANONICAL));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == encodeRefused(REFUSE_AUTH_REQUIRED));
	a = send(relay, addr(7398), hello3(p0.ticket, p0.token));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == v3(encodeRefused(REFUSE_AUTH_REQUIRED)));
	CHECK(routeOf(relay, p1, 0, 0) == p0.from);
	// the victim's later authenticated datagrams still count
	CHECK(routeOf(relay, p0, 1, 0) == p1.from);
	CHECK(relay.stats().authDropped == 0);
}

TEST_CASE("online16-fix auth: authenticated rebind, rejoin and spectator flows; STATUS and LOG_CHUNK are sealed")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = (std::filesystem::temp_directory_path() / ("openbfme_o16fix_flows_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(o.logDir);
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 7400), p1 = party(playerTicket(1), 7401);
	bind(relay, p0);
	bind(relay, p1);
	// a rebind (the address changes) with the token: sealed WELCOME, the slot's traffic follows
	p0.from = addr(7410);
	std::vector<RelayCore::Action> a = send(relay, p0.from, helloOf(p0));
	REQUIRE(a.size() == 1);
	CHECK(open(p0, a[0])[3] == WELCOME);
	CHECK(routeOf(relay, p1, 0, 0) == p0.from);
	// a rejoin: the generation-1 ticket's own keys, from yet another address; the old keys stop working
	Party r0 = party(rejoinTicket(0, 1), 7420);
	a = send(relay, r0.from, helloOf(r0));
	REQUIRE(a.size() == 1);
	CHECK(open(r0, a[0])[3] == WELCOME);
	CHECK(open(p0, a[0]).empty());
	CHECK(routeOf(relay, p1, 0, 1) == r0.from);
	CHECK(routeOf(relay, r0, 1, 0) == p1.from);
	// a spectator: its LOG_CHUNK is sealed with its own key
	Party spec = party(spectatorTicket(3), 7430);
	bind(relay, spec);
	a = send(relay, spec.from, sealed(spec, encodeLogRequest(0)));
	REQUIRE(a.size() == 1);
	const std::vector<std::uint8_t> chunk = open(spec, a[0]);
	REQUIRE(chunk.size() > kHeaderBytes + 8); // tolerance: the log's length so far (its header and the binds)
	CHECK(chunk[3] == LOG_CHUNK);
	// STATUS (slot 0 has rejoined, so it is sent) opens for each player with its own key
	std::vector<RelayCore::Action> st;
	relay.tick(1001, &st);
	REQUIRE(st.size() == 2);
	for (const RelayCore::Action &s : st)
	{
		Party &who = s.to == r0.from ? r0 : p1;
		const std::vector<std::uint8_t> d = open(who, s);
		REQUIRE(d.size() > kHeaderBytes); // tolerance: a STATUS of the slots that ever bound
		CHECK(d[3] == STATUS);
	}
	std::filesystem::remove_all(o.logDir);
}

TEST_CASE("online16-fix auth: a restarted relay's answers pass the client's window; refusals are never sealed with a fixed counter (review 5)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	Party p0 = party(playerTicket(0), 7500), p1 = party(playerTicket(1), 7501);
	{
		RelayCore before(o);
		bind(before, p0);
		bind(before, p1);
		// a game's worth of traffic to slot 0 moves its window far from where a fresh relay would start counting
		for (std::uint32_t i = 0; i < 300; ++i)
		{
			std::vector<RelayCore::Action> a = send(before, p1.from, sealed(p1, encodeData(0, transportPacket(1, i, frameInfo(1, i)))));
			REQUIRE(a.size() == 1);
			REQUIRE(open(p0, a[0])[3] == DATA_FROM);
		}
	}
	RelayCore after(o);
	// the client's HELLO after the restart: the new relay's sealed WELCOME opens in the client's window
	std::vector<RelayCore::Action> a = send(after, p0.from, helloOf(p0));
	REQUIRE(a.size() == 1);
	const std::vector<std::uint8_t> w = open(p0, a[0]);
	REQUIRE(w.size() > kHeaderBytes); // tolerance: opened at all (the kind is checked next)
	CHECK(w[3] == WELCOME);
	// two refusals of the same key carry different counters (the client's window accepts each once)
	RelayProtocol::Ticket expired = testTicket(2, 0x07);
	expired.expires = 1;
	Party old = party(RelayProtocol::signTicket(expired, testKey()), 7503);
	a = send(after, old.from, helloOf(old));
	REQUIRE(a.size() == 1);
	CHECK(open(old, a[0]) == encodeRefused(REFUSE_EXPIRED));
	a = send(after, old.from, helloOf(old));
	REQUIRE(a.size() == 1);
	CHECK(open(old, a[0]) == encodeRefused(REFUSE_EXPIRED));
}

TEST_CASE("online16-fix auth: a short authenticated HELLO whose ticket byte names a longer ticket is dropped within its own bytes (review 6)")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	for (std::uint8_t version = 1; version <= 8; ++version)
	{
		// 'B' 'R' 5 HELLO | counter | a 1-byte "ticket" naming `version` | tag: 29 bytes, in an exactly sized heap buffer
		std::vector<std::uint8_t> d = { 'B', 'R', 4, HELLO, version };
		RelayAuth::Key junk{};
		RelayAuth::seal(d, junk, 1);
		REQUIRE(d.size() == 29);
		std::unique_ptr<std::uint8_t[]> exact(new std::uint8_t[d.size()]);
		std::memcpy(exact.get(), d.data(), d.size());
		CHECK(relay.handle(addr(7600), exact.get(), d.size(), 1000, 1700000000u).kind == RelayCore::Action::NONE);
	}
	CHECK(relay.stats().authDropped == 8);
}

// ---- the client side of review 4: untagged answers once the relay has been verified --------------------------------------------------------

TEST_CASE("online16-fix auth: an authenticated client ignores an untagged REFUSE_VERSION after it verified the relay, and an untagged NOT_BOUND only re-HELLOs")
{
	using namespace RelayProtocol;
	UDP relaySocket, client;
	std::string error;
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relaySocket.localAddress();
	o.relay.ticket = playerTicket(0);
	o.relay.helloMs = 100;
	Party me = party(o.relay.ticket, 0);
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), o.relay.ticket.data(), o.relay.ticket.size());
	o.relay.macKey.assign(k.begin(), k.end());
	std::uint64_t now = 10000;
	Transport t(client, 0, o);
	t.setClock([&] { return now; });
	t.setPeer(1, NetAddress{});
	auto hellos = [&] {
		int n = 0;
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (relaySocket.receiveFrom(from, d))
		{
			n += d.size() > 4 && d[2] == kVersionAuth && d[3] == HELLO;
		}
		return n;
	};
	auto deliver = [&](const std::vector<std::uint8_t> &d) {
		relaySocket.sendTo(client.localAddress(), d.data(), d.size());
		NetSleepMilliseconds(5);
		t.service();
	};
	t.service();
	REQUIRE(hellos() == 1);
	// the relay's sealed WELCOME
	std::array<std::uint8_t, kSessionTokenBytes> token{ 1, 2, 3 };
	std::vector<std::uint8_t> w = encodeWelcome(kGame, 0, token);
	RelayAuth::seal(w, me.keys.r2c, 5000);
	deliver(w);
	REQUIRE(t.relayBound());
	CHECK(t.localLinkAlive(1000));
	// a spoofed REFUSE_VERSION: ignored (no latch, still bound)
	deliver(encodeRefused(REFUSE_VERSION));
	CHECK(t.relayRefusal() == 0);
	CHECK(t.relayBound());
	// untagged NOT_BOUNDs: one HELLO per helloMs, the binding and the link timer untouched
	now += 5000;
	CHECK_FALSE(t.localLinkAlive(1000));
	hellos();
	deliver(encodeEmpty(NOT_BOUND));
	deliver(encodeEmpty(NOT_BOUND));
	deliver(encodeEmpty(NOT_BOUND));
	CHECK(hellos() == 1);
	CHECK(t.relayBound());
	CHECK_FALSE(t.localLinkAlive(1000));
	now += 100;
	deliver(encodeEmpty(NOT_BOUND));
	CHECK(hellos() == 1);
	CHECK(t.relayRefusal() == 0);
}

#ifdef OPENBFME_RELAY_EXE
TEST_CASE("online16-fix auth: an authenticated Transport recovers after the real relay restarts")
{
	using namespace RelayProtocol;
	std::string error;
	UDP s0, s1;
	REQUIRE(s0.bind(0x7F000001u, 0, &error));
	REQUIRE(s1.bind(0x7F000001u, 0, &error));
	auto relay = std::make_unique<RealRelay>("restart");
	const NetAddress at = relay->address();
	std::vector<std::unique_ptr<Transport>> t;
	UDP *sockets[2] = { &s0, &s1 };
	for (int i = 0; i < 2; ++i)
	{
		Transport::Options o;
		o.relay.enabled = true;
		o.relay.address = at;
		o.relay.ticket = playerTicket((std::uint8_t)i);
		o.relay.helloMs = 20;
		o.resendMs = 20;
		const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), o.relay.ticket.data(), o.relay.ticket.size());
		o.relay.macKey.assign(k.begin(), k.end());
		t.push_back(std::make_unique<Transport>(*sockets[i], i, o));
		t.back()->setPeer(1 - i, NetAddress{});
	}
	auto pump = [&](std::uint64_t ms, const std::function<bool()> &until) {
		const std::uint64_t end = NetMilliseconds() + ms;
		while (NetMilliseconds() < end && !until())
		{
			for (auto &tr : t)
			{
				tr->service();
			}
			NetSleepMilliseconds(2);
		}
		return until();
	};
	REQUIRE(pump(10000, [&] { return t[0]->relayBound() && t[1]->relayBound(); }));
	int got = 0;
	auto receiveAll = [&] {
		int from = -1;
		std::vector<std::uint8_t> b;
		while (t[0]->receive(from, b))
		{
			++got;
		}
		return got;
	};
	// one datagram each: the old relay's counter towards slot 0 climbs past what a relay starting at a fixed 0 would reach in a few HELLOs
	for (std::uint32_t f = 0; f < 200; ++f)
	{
		REQUIRE(t[1]->sendTo(0, frameInfo(1, f), &error));
		REQUIRE(pump(5000, [&] { return receiveAll() == (int)f + 1; }));
	}
	// the relay process restarts on the same port
	relay->child->stop();
	relay->child = std::make_unique<TestChild>(OPENBFME_RELAY_EXE,
		std::vector<std::string>{ "--bind", "127.0.0.1:" + std::to_string(relay->port), "--keys", (relay->dir / "keys.txt").string(), "--log-dir",
			relay->dir.string(), "--status", (relay->dir / "status.json").string(), "--stats-s", "0" },
		(relay->dir / "relay.out").string());
	REQUIRE(relay->child->started());
	const unsigned long long welcomes = t[0]->stats().relayWelcomes;
	for (std::uint32_t f = 200; f < 210; ++f)
	{
		REQUIRE(t[1]->sendTo(0, frameInfo(1, f), &error));
	}
	// NOT_BOUND -> HELLO -> the new relay's WELCOME, sealed with a counter beyond everything the old one sent (a counter from 0 would be
	// dropped by the client's window until about 200 HELLOs, 4 s at helloMs 20, had passed)
	CHECK(pump(2000, [&] { return t[0]->stats().relayWelcomes > welcomes; })); // tolerance: a wall-clock recovery bound, not a value
	CHECK(pump(5000, [&] { return receiveAll() == 210; }));
	CHECK(t[0]->relayRefusal() == 0);
	CHECK(t[1]->relayRefusal() == 0);
}
TEST_CASE("online batch: an authenticated sender's DATA reaches an unauthenticated client of the same game at its own length through the real relay")
{
	// Sol (blocking 1): the relay opened the sealed DATA in place (24 bytes shorter) and relay_main forwarded the received length: the untagged
	// recipient read the tag as transport bytes and dropped the packet (CRC / length)
	using namespace RelayProtocol;
	std::string error;
	UDP s0, s1;
	REQUIRE(s0.bind(0x7F000001u, 0, &error));
	REQUIRE(s1.bind(0x7F000001u, 0, &error));
	RealRelay relay("mixedauth");
	std::vector<std::unique_ptr<Transport>> t;
	UDP *sockets[2] = { &s0, &s1 };
	for (int i = 0; i < 2; ++i)
	{
		Transport::Options o;
		o.relay.enabled = true;
		o.relay.address = relay.address();
		o.relay.ticket = playerTicket((std::uint8_t)i);
		o.relay.helloMs = 20;
		o.resendMs = 20;
		if (i == 0)
		{
			// slot 0 holds the START's session key (tagged); slot 1 an older lobby's START without one (untagged)
			const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), o.relay.ticket.data(), o.relay.ticket.size());
			o.relay.macKey.assign(k.begin(), k.end());
		}
		t.push_back(std::make_unique<Transport>(*sockets[i], i, o));
		t.back()->setPeer(1 - i, NetAddress{});
	}
	const std::uint64_t deadline = NetMilliseconds() + 10000;
	int got[2] = {};
	bool sent = false;
	while ((!t[0]->relayBound() || !t[1]->relayBound() || got[0] < 20 || got[1] < 20) && NetMilliseconds() < deadline)
	{
		for (int i = 0; i < 2; ++i)
		{
			t[(size_t)i]->service();
			int from = -1;
			std::vector<std::uint8_t> b;
			while (t[(size_t)i]->receive(from, b))
			{
				NetCommandMsg c;
				REQUIRE(NetPacket::decodeCommand(b, c, &error));
				CHECK(from == 1 - i);
				CHECK(c.executionFrame < 20u); // tolerance: one of the 20 frames sent (the transport may deliver them in any order)
				++got[i];
			}
		}
		if (!sent && t[0]->relayBound() && t[1]->relayBound())
		{
			sent = true;
			for (std::uint32_t f = 0; f < 20; ++f)
			{
				REQUIRE(t[0]->sendTo(1, frameInfo(0, f), &error));
				REQUIRE(t[1]->sendTo(0, frameInfo(1, f), &error));
			}
		}
		NetSleepMilliseconds(2);
	}
	CHECK(got[1] == 20); // tagged -> untagged
	CHECK(got[0] == 20); // untagged -> tagged
	CHECK(t[1]->stats().badPackets == 0);
	CHECK(t[0]->stats().badPackets == 0);
	CHECK(t[0]->stats().relayAuthDropped == 0);
	const std::string status = relay.stopAndStatus();
	CHECK(status.find("\"authDropped\":0") != std::string::npos);
}
#endif

// ---- finding 3 (Sol #6): slots 8 .. 15 in the relay log's replay, catch-up and spectating paths -----------------------------------------------

namespace
{
// a relay log header: version 4 (a u16 slot mask) for a wide mask, as RelayCore writes it
std::vector<std::uint8_t> logHeader(std::uint16_t mask)
{
	std::vector<std::uint8_t> b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	auto put32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			b.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	put32(4);
	put32((std::uint32_t)kGame);
	put32((std::uint32_t)(kGame >> 32));
	b.push_back((std::uint8_t)mask);
	b.push_back((std::uint8_t)(mask >> 8));
	put32(1700000000u);
	return b;
}
void put32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		b.push_back((std::uint8_t)(v >> (8 * i)));
	}
}
std::vector<std::uint8_t> encodedOrder(GameMessageType type, int arg)
{
	GameMessage m(type, 99);
	m.appendIntegerArgument(arg);
	if (type == MSG_LOGIC_CRC)
	{
		m.appendBooleanArgument(false);
	}
	NetByteWriter w;
	std::string err;
	REQUIRE(NetPacket::writeGameMessage(w, m, &err));
	return w.data();
}
NetworkConfig sixteenPlayers()
{
	NetworkConfig config;
	for (int s = 0; s < 16; ++s)
	{
		config.slotPlayerIndex[(size_t)s] = 2 + s; // Player_<s+1>: the engine's index of each slot's player
	}
	config.crcInterval = 2;
	return config;
}
} // namespace

TEST_CASE("online16-fix: a canonical sixteen-slot log assembles slots 8 .. 15's orders as their own players' (RelayLog.cpp, Sol #6)")
{
	const std::uint16_t mask = 0x8401; // slots 0, 10 and 15
	std::vector<std::uint8_t> b = logHeader(mask);
	std::uint64_t seq = 0;
	auto record = [&](RelayCommit::Kind kind, std::uint32_t frame, const std::vector<std::uint8_t> &payload) {
		RelayCommit::Record rec;
		rec.stamp.game = kGame;
		rec.stamp.epoch = 1;
		rec.stamp.sequence = ++seq;
		rec.stamp.frame = frame;
		rec.kind = kind;
		rec.payload = payload;
		std::vector<std::uint8_t> enc;
		std::string err;
		REQUIRE(RelayCommit::encode(rec, enc, err));
		b.push_back(6);
		put32(b, (std::uint32_t)enc.size());
		b.insert(b.end(), enc.begin(), enc.end());
	};
	RelayMembership::Snapshot snap;
	snap.slotMask = snap.active = snap.loaded = mask;
	std::vector<std::uint8_t> p;
	std::string err;
	REQUIRE(RelayMembership::encode(snap, p, err));
	record(RelayCommit::Kind::Membership, 0, p);
	RelayFrameBundle::Frame f;
	f.orders.push_back({ 0, 1, encodedOrder(MSG_LOGIC_CRC, 111) });
	f.orders.push_back({ 10, 1, encodedOrder(MSG_DO_ATTACK_OBJECT, 7) });
	f.orders.push_back({ 15, 1, encodedOrder(MSG_LOGIC_CRC, 111) });
	p.clear();
	REQUIRE(RelayFrameBundle::encode(f, p, err));
	record(RelayCommit::Kind::Frame, 1, p);
	record(RelayCommit::Kind::End, 1, {});
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(b, log, &error), error);
	RelayLog::Assembled a;
	REQUIRE_MESSAGE(RelayLog::assemble(log, sixteenPlayers(), a, &error), error);
	REQUIRE(a.frames.count(1));
	const std::vector<GameMessage> &f1 = a.frames.at(1).messages();
	REQUIRE(f1.size() == 3);
	CHECK(f1[0].getPlayerIndex() == 2);
	CHECK(f1[1].getPlayerIndex() == 12); // slot 10's player, not -1
	CHECK(f1[2].getPlayerIndex() == 17); // slot 15's
	CHECK(a.crcDisagreements == 0);
	// a network slot without a player in the loaded game is an error, never an order of nobody
	NetworkConfig missing = sixteenPlayers();
	missing.slotPlayerIndex[10] = -1;
	CHECK_FALSE(RelayLog::assemble(log, missing, a, &error));
	CHECK(error.find("network slot 10") != std::string::npos);
}

TEST_CASE("online16-fix: a legacy (lockstep) sixteen-slot log assembles slot 15's frames as its player's")
{
	std::vector<std::uint8_t> b = logHeader(0x8001);
	auto cmd = [&](int sender, const std::vector<std::uint8_t> &c) {
		b.push_back(1);
		b.push_back((std::uint8_t)sender);
		put32(b, 0);
		put32(b, (std::uint32_t)c.size());
		b.insert(b.end(), c.begin(), c.end());
	};
	auto frame = [](std::uint8_t slot, std::uint32_t f, std::uint16_t count) {
		NetCommandMsg c;
		c.type = NETCOMMANDTYPE_FRAMEINFO;
		c.slot = slot;
		c.executionFrame = f;
		c.commandCount = count;
		return NetPacket::encodeCommand(c, nullptr);
	};
	auto order = [](std::uint8_t slot, std::uint32_t f, int arg) {
		NetCommandMsg c;
		c.type = NETCOMMANDTYPE_GAMECOMMAND;
		c.slot = slot;
		c.executionFrame = f;
		auto m = std::make_shared<GameMessage>(MSG_LOGIC_CRC, 99);
		m->appendIntegerArgument(arg);
		m->appendBooleanArgument(false);
		c.message = m;
		return NetPacket::encodeCommand(c, nullptr);
	};
	cmd(15, frame(15, 0, 0));
	cmd(15, order(15, 1, 111));
	cmd(15, frame(15, 1, 1));
	cmd(0, frame(0, 0, 0));
	cmd(0, order(0, 1, 111));
	cmd(0, frame(0, 1, 1));
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(b, log, &error), error);
	RelayLog::Assembled a;
	REQUIRE_MESSAGE(RelayLog::assemble(log, sixteenPlayers(), a, &error), error);
	REQUIRE(a.frames.count(1));
	const std::vector<GameMessage> &f1 = a.frames.at(1).messages();
	REQUIRE(f1.size() == 2);
	CHECK(f1[0].getPlayerIndex() == 2);
	CHECK(f1[1].getPlayerIndex() == 17);
	CHECK(a.hashes.at(2) == 111u);
	CHECK(a.crcDisagreements == 0);
}

TEST_CASE("online16-fix: a spectator's score results see the drops of slots 8 .. 15 (disconnectedSlotsAt)")
{
	using E = Network::MembershipEvent;
	RelayLog::Assembled log;
	log.membership = { { E::DROP, 10, 5 }, { E::DROP, 15, 6 }, { E::REJOIN, 15, 8, 1 }, { E::DROP, 3, 7 } };
	CHECK(log.disconnectedSlotsAt(5) == 0x0400u); // slot 10
	CHECK(log.disconnectedSlotsAt(7) == 0x8408u); // slots 3, 10, 15
	CHECK(log.disconnectedSlotsAt(9) == 0x0408u); // slot 15 rejoined
}

// ---- finding 5: a slot value 9 .. 15 in an eight-slot game is still refused at the game and network layers -------------------------------------
// (the P16 edits moved the out-of-range probes from 9 to 16: the wire now carries 9 .. 15, so the game itself must refuse them)

namespace
{
struct Inbox : NetTransport
{
	std::deque<std::pair<int, std::vector<std::uint8_t>>> in;
	std::vector<std::pair<int, std::vector<std::uint8_t>>> out;
	bool sendTo(int slot, const std::vector<std::uint8_t> &bytes, std::string *) override
	{
		out.push_back({ slot, bytes });
		return true;
	}
	bool receive(int &from, std::vector<std::uint8_t> &bytes) override
	{
		if (in.empty())
		{
			return false;
		}
		from = in.front().first;
		bytes = in.front().second;
		in.pop_front();
		return true;
	}
	std::uint64_t lastHeardFrom(int) const override { return 1000; }
};
NetworkConfig eightSlotGame()
{
	NetworkConfig c;
	c.localSlot = 0;
	for (int i = 0; i < 3; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = 2 + i; // slots 0 .. 2 play; the game has eight places
		c.slotNames[(size_t)i] = u"P";
	}
	c.runAhead = 2;
	c.crcInterval = 0;
	return c;
}
bool anyError(const Network &n, const std::string &part)
{
	return std::any_of(n.errors().begin(), n.errors().end(), [&](const std::string &e) { return e.find(part) != std::string::npos; });
}
} // namespace

TEST_CASE("online16-fix: an eight-slot game refuses a sender, a vote, a resend and a destroy naming slots 9 .. 15")
{
	for (const int slot : { 8, 9, 12, 15 })
	{
		CAPTURE(slot);
		Inbox t;
		Network net(eightSlotGame(), t);
		net.setClock([] { return (std::uint64_t)1000; });
		// a frame "from" slot 9: no remote network player of this game
		NetCommandMsg f;
		f.type = NETCOMMANDTYPE_FRAMEINFO;
		f.slot = (std::uint8_t)slot;
		f.executionFrame = 3;
		t.in.push_back({ slot, NetPacket::encodeCommand(f, nullptr) });
		// a vote about slot 9 (the wide wire form decodes it): not a player of the game
		NetCommandMsg v;
		v.type = NETCOMMANDTYPE_DISCONNECTVOTE;
		v.slot = 1;
		v.targetSlot = (std::uint8_t)slot;
		v.frame = 3;
		const std::vector<std::uint8_t> vote = NetPacket::encodeCommand(v, nullptr);
		REQUIRE(!vote.empty());
		t.in.push_back({ 1, vote });
		// a resend of slot 9's frame and a DESTROYPLAYER of slot 9: nobody's frame runs, no player is destroyed
		NetCommandMsg r;
		r.type = NETCOMMANDTYPE_FRAMERESEND;
		r.slot = 1;
		r.targetSlot = (std::uint8_t)slot;
		r.frame = 3;
		t.in.push_back({ 1, NetPacket::encodeCommand(r, nullptr) });
		NetCommandMsg d;
		d.type = NETCOMMANDTYPE_DESTROYPLAYER;
		d.slot = 1;
		d.targetSlot = (std::uint8_t)slot;
		d.executionFrame = 3;
		t.in.push_back({ 1, NetPacket::encodeCommand(d, nullptr) });
		CommandList none;
		net.update(0, none);
		CHECK(anyError(net, "a net command from slot " + std::to_string(slot) + ", which is not a remote network player"));
		CHECK(anyError(net, "DISCONNECTVOTE of slot 1 for slot " + std::to_string(slot) + ", which is not a player of the game: refused"));
		CHECK(anyError(net, "DESTROYPLAYER of slot " + std::to_string(slot) + ", which has no player"));
		CHECK_FALSE(net.isGameSlot(slot));
	}
}

TEST_CASE("online16-fix: an eight-slot relay game refuses tickets and targets of slots 9 .. 15")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	REQUIRE(send(relay, addr(8000), encodeHello(playerTicket(0, 0x07), {}, CAP_CANONICAL))[0].reply[3] == WELCOME);
	REQUIRE(send(relay, addr(8001), encodeHello(playerTicket(1, 0x07), {}, CAP_CANONICAL))[0].reply[3] == WELCOME);
	for (const std::uint8_t slot : { 9, 12, 15 })
	{
		CAPTURE((int)slot);
		// a version-1 ticket has an eight-bit mask: slot 9 is outside it whatever the signer wrote
		RelayProtocol::Ticket t = testTicket(slot, 0x07);
		std::vector<std::uint8_t> bytes = signTicket(t, testKey());
		Ticket out;
		std::string why;
		CHECK_FALSE(verifyTicket(bytes.data(), bytes.size(), testRing(), 1700000000u, out, &why));
		// DATA addressed to slot 9 of the three-slot game: not a target
		const unsigned long long before = relay.stats().badTargets;
		CHECK(send(relay, addr(8000), encodeData(slot, transportPacket(0, slot, frameInfo(0, slot)))).empty());
		CHECK(relay.stats().badTargets == before + 1);
	}
}

TEST_CASE("online16-fix auth: a sealed BYE (no payload) is read and closes the game; it is not an auth drop")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = (std::filesystem::temp_directory_path() / ("openbfme_o16fix_bye_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(o.logDir);
	RelayCore relay(o);
	Party p0 = party(playerTicket(0), 7700), p1 = party(playerTicket(1), 7701);
	bind(relay, p0);
	bind(relay, p1);
	REQUIRE(relay.openGames() == 1);
	const std::vector<std::uint8_t> bye0 = sealed(p0, encodeEmpty(BYE));
	REQUIRE(bye0.size() == kHeaderBytes + RelayAuth::kOverhead); // 28 bytes: the envelope of an empty kind
	CHECK(send(relay, p0.from, bye0).empty());
	CHECK(send(relay, p1.from, sealed(p1, encodeEmpty(BYE))).empty());
	CHECK(relay.stats().authDropped == 0);
	CHECK(relay.openGames() == 0); // every bound slot said BYE: the log closed
	CHECK(relay.closedLogs().size() == 1);
	std::filesystem::remove_all(o.logDir);
}

// ---- online batch (Sol, blocking 2): a previous-release game's relay log is one the previous release reads (live rejoin and spectating) ---------

namespace
{
// the previous release's relay-log reader (RelayLog.cpp of v0.4.11 and of v0.4.12, b7c72f71c: the same file): versions 1 and 2, record kinds 1 .. 5
// (5 from version 2), an END of eight slots last. Returns "" when it reads the log, else its refusal
std::string previousReleaseRefusal(const std::vector<std::uint8_t> &b)
{
	if (b.size() < 8 + 4 + 8 + 1 + 4 || std::string(b.begin(), b.begin() + 8) != "OBFMERLG")
	{
		return "not a relay log";
	}
	const std::uint32_t version = (std::uint32_t)b[8] | ((std::uint32_t)b[9] << 8) | ((std::uint32_t)b[10] << 16) | ((std::uint32_t)b[11] << 24);
	if (version != 1 && version != 2)
	{
		return "log version " + std::to_string(version) + " (this build reads 1 and 2)";
	}
	size_t at = 8 + 4 + 8 + 1 + 4;
	while (at < b.size())
	{
		const std::uint8_t kind = b[at];
		const size_t fixed = kind == 1 ? 10 : kind == 2 ? 7 : kind == 3 ? 8 : kind == 4 ? 6 + 8 * 24 : kind == 5 ? 8 : 0;
		if (fixed == 0 || (kind == 5 && version < 2))
		{
			return "unknown record kind " + std::to_string(kind);
		}
		if (at + fixed > b.size())
		{
			return "truncated record";
		}
		if (kind == 1 && b[at + 1] >= 8)
		{
			return "truncated or malformed COMMAND record";
		}
		const size_t len = kind == 1 ? ((size_t)b[at + 6] | ((size_t)b[at + 7] << 8) | ((size_t)b[at + 8] << 16) | ((size_t)b[at + 9] << 24)) : 0;
		at += fixed + len;
		if (kind == 4 && at != b.size())
		{
			return "bytes after the END record";
		}
	}
	return at == b.size() ? "" : "truncated record";
}
std::vector<std::uint8_t> readAll(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// a whole game in one envelope: slots 0 and 1 bind, exchange a frame, slot 0 rejoins (generation 1), a spectator reads the log, both say BYE
std::vector<std::uint8_t> playedLog(bool previousRelease, std::vector<std::uint8_t> *spectated)
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = (std::filesystem::temp_directory_path() / ("openbfme_batch_log_" + std::to_string(previousRelease) + "_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(o.logDir);
	RelayCore relay(o);
	auto wire = [&](std::vector<std::uint8_t> d) { return previousRelease ? v3(std::move(d)) : d; };
	auto hello = [&](const std::vector<std::uint8_t> &ticket) { return previousRelease ? hello3(ticket) : encodeHello(ticket, {}, CAP_CANONICAL); };
	for (std::uint8_t s = 0; s < 2; ++s)
	{
		std::vector<RelayCore::Action> a = send(relay, addr((std::uint16_t)(8100 + s)), hello(playerTicket(s, 0x03)));
		REQUIRE(a.size() == 1);
		REQUIRE(a[0].reply[3] == WELCOME);
	}
	for (std::uint8_t s = 0; s < 2; ++s)
	{
		std::vector<RelayCore::Action> a = send(relay, addr((std::uint16_t)(8100 + s)), wire(encodeData((std::uint8_t)(1 - s), transportPacket(s, 0,
			frameInfo(s, 1)))));
		REQUIRE(a.size() == 1);
	}
	std::vector<RelayCore::Action> a = send(relay, addr(8110), hello(rejoinTicket(0, 1, 0x03)));
	REQUIRE(a.size() == 1);
	REQUIRE(a[0].reply[3] == WELCOME);
	a = send(relay, addr(8111), hello(spectatorTicket(0, 0x03)));
	REQUIRE(a.size() == 1);
	REQUIRE(a[0].reply[3] == WELCOME);
	a = send(relay, addr(8111), wire(encodeLogRequest(0)));
	REQUIRE(a.size() == 1);
	REQUIRE(a[0].reply[3] == LOG_CHUNK);
	REQUIRE(a[0].reply.size() > kHeaderBytes + 8); // tolerance: the LOG_CHUNK's offset and the log bytes so far
	spectated->assign(a[0].reply.begin() + (long)kHeaderBytes + 8, a[0].reply.end());
	send(relay, addr(8110), wire(encodeEmpty(BYE)));
	send(relay, addr(8101), wire(encodeEmpty(BYE)));
	REQUIRE(relay.closedLogs().size() == 1);
	const std::vector<std::uint8_t> log = readAll(relay.closedLogs()[0]);
	std::filesystem::remove_all(o.logDir);
	return log;
}
} // namespace

TEST_CASE("online batch: a previous-release game (version-3 envelope) gets a version-2 relay log the previous release reads; a current game keeps version 4")
{
	std::vector<std::uint8_t> spectated;
	const std::vector<std::uint8_t> old = playedLog(true, &spectated);
	REQUIRE(old.size() > 25); // tolerance: a header and records
	// what its rejoining players and its spectators read through LOG_CHUNK, mid-game and whole
	CHECK(previousReleaseRefusal(spectated) == "");
	CHECK(previousReleaseRefusal(old) == "");
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(old, f, &error), error);
	CHECK(f.version == 2);
	CHECK(f.slotMask == 0x03);
	CHECK(f.inputs.empty()); // no INPUT record (kind 7): a previous-release client has no AUTH-1 key anyway
	CHECK(f.rejoins.size() == 1);
	CHECK(f.complete);
	// a current game: version 4, the INPUT records of both slots (ANTICHEAT-1); the previous release refuses it by its version
	const std::vector<std::uint8_t> cur = playedLog(false, &spectated);
	RelayLogFile g;
	REQUIRE_MESSAGE(RelayLogFile::parse(cur, g, &error), error);
	CHECK(g.version == 4);
	CHECK(g.inputs.size() == 2);
	CHECK(g.complete);
	CHECK(previousReleaseRefusal(cur) == "log version 4 (this build reads 1 and 2)");
}

// ---- online batch r3 (Sol's delta review, blocking): only a player's HELLO opens a game and so decides its log's format --------------------------

namespace
{
// a game's relay log when a spectator HELLOs first (spectatorEnvelope 3: the previous release's, 4: this build's; it must be NOT_BOUND, nothing
// opened) and, with `restart`, again after the relay restarts mid-game (a new RelayCore: the spectator re-HELLOs before the players). The players:
// the previous release's (version 3, plain) or this build's, authenticated (version 5 with the START's session key)
std::vector<std::uint8_t> spectatorFirstLog(bool previousRelease, std::uint8_t spectatorEnvelope, bool restart)
{
	using namespace RelayProtocol;
	const std::string base = (std::filesystem::temp_directory_path() / ("openbfme_batch_specfirst_" + std::to_string(previousRelease) + "_"
		+ std::to_string(spectatorEnvelope) + "_" + std::to_string(restart) + "_" + std::to_string(NetMilliseconds()))).string();
	Party p[2] = { party(playerTicket(0, 0x03), 8300), party(playerTicket(1, 0x03), 8301) };
	std::vector<std::uint8_t> log;
	const int runs = restart ? 2 : 1;
	std::uint32_t seq = 0;
	for (int run = 0; run < runs; ++run)
	{
		RelayCore::Options o;
		o.keys = testRing();
		o.logDir = base + "_" + std::to_string(run);
		std::filesystem::create_directories(o.logDir);
		RelayCore relay(o);
		const RelayAddress spec = addr((std::uint16_t)(8310 + run));
		const std::vector<std::uint8_t> sticket = spectatorTicket(0, 0x03);
		std::vector<RelayCore::Action> a = send(relay, spec, spectatorEnvelope == kV3 ? hello3(sticket) : encodeHello(sticket, {}, CAP_CANONICAL));
		REQUIRE(a.size() == 1);
		CHECK(a[0].reply == (spectatorEnvelope == kV3 ? v3(encodeEmpty(NOT_BOUND)) : encodeEmpty(NOT_BOUND))); // the client HELLOs again
		CHECK(relay.openGames() == 0);
		CHECK(relay.stats().gamesOpened == 0);
		// the players (after a restart: again, as their transports do on NOT_BOUND)
		for (int s = 0; s < 2; ++s)
		{
			if (previousRelease)
			{
				a = send(relay, p[s].from, hello3(p[s].ticket));
				REQUIRE(a.size() == 1);
				REQUIRE(a[0].reply[3] == WELCOME);
			}
			else
			{
				p[s].token.clear();
				p[s].recv = RelayAuth::Window();
				bind(relay, p[s]);
			}
		}
		// now the spectator binds
		a = send(relay, spec, spectatorEnvelope == kV3 ? hello3(sticket) : encodeHello(sticket, {}, CAP_CANONICAL));
		REQUIRE(a.size() == 1);
		CHECK(a[0].reply[3] == WELCOME);
		for (int s = 0; s < 2; ++s)
		{
			const std::vector<std::uint8_t> d = encodeData((std::uint8_t)(1 - s), transportPacket((std::uint8_t)s, seq, frameInfo((std::uint8_t)s, seq + 1)));
			a = send(relay, p[s].from, previousRelease ? v3(d) : sealed(p[s], d));
			REQUIRE(a.size() == 1);
		}
		++seq;
		if (run + 1 < runs)
		{
			continue; // the relay process ends here (restart)
		}
		for (int s = 0; s < 2; ++s)
		{
			send(relay, p[s].from, previousRelease ? v3(encodeEmpty(BYE)) : sealed(p[s], encodeEmpty(BYE)));
		}
		REQUIRE(relay.closedLogs().size() == 1);
		log = readAll(relay.closedLogs()[0]);
	}
	for (int run = 0; run < runs; ++run)
	{
		std::filesystem::remove_all(base + "_" + std::to_string(run));
	}
	return log;
}
void checkCurrentLog(const std::vector<std::uint8_t> &log)
{
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(log, f, &error), error);
	CHECK(f.version == 4);
	REQUIRE(f.inputs.size() == 2);
	for (const RelayLogFile::Input &in : f.inputs)
	{
		CHECK(in.hadKey);
		CHECK(in.signedIn >= 1); // tolerance: the frames DATA of this incarnation (one), whatever else the slot sent verified
	}
}
void checkPreviousReleaseLog(const std::vector<std::uint8_t> &log)
{
	CHECK(previousReleaseRefusal(log) == "");
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(log, f, &error), error);
	CHECK(f.version == 2);
	CHECK(f.inputs.empty());
}
} // namespace

TEST_CASE("online batch r3: a previous-release spectator HELLO first into a current authenticated game is NOT_BOUND; the game's log is version 4 with its INPUT records")
{
	checkCurrentLog(spectatorFirstLog(false, kV3, false));
}

TEST_CASE("online batch r3: a current spectator HELLO first into a previous-release game is NOT_BOUND; the game's log is version 2")
{
	checkPreviousReleaseLog(spectatorFirstLog(true, RelayProtocol::kVersion, false));
}

TEST_CASE("online batch r3: after a relay restart a spectator re-HELLOing before the players is NOT_BOUND; the players' HELLOs decide the format again")
{
	checkCurrentLog(spectatorFirstLog(false, kV3, true));
	checkPreviousReleaseLog(spectatorFirstLog(true, RelayProtocol::kVersion, true));
}

// ---- online batch r4 (Sol's second delta): a version-2 log keeps its INPUT records in a sidecar; authenticated players under it are an anomaly D7 reports

namespace
{
enum class R4 { Current, AllOld, PinnedByV3Hello };
struct R4Game
{
	std::string dir, logPath;
	std::vector<std::uint8_t> log;
	bool sidecar = false;
	unsigned long long legacyLogAuthenticated = 0;
	~R4Game()
	{
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
};
// two players (slots 0 and 1) play one frame each and say BYE. PinnedByV3Hello: slot 0, a current participant, first sends its own ticket in a
// version-3 HELLO (the game opens with a version-2 log), then both authenticate as usual (slot 0 rebinds with the session token it got)
void playR4(R4 kind, R4Game &out)
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	out.dir = (std::filesystem::temp_directory_path() / ("openbfme_batch_r4_" + std::to_string((int)kind) + "_" + std::to_string(NetMilliseconds()))).string();
	o.logDir = out.dir;
	std::filesystem::create_directories(o.logDir);
	RelayCore relay(o);
	Party p[2] = { party(playerTicket(0, 0x03), 8500), party(playerTicket(1, 0x03), 8501) };
	const bool old = kind == R4::AllOld;
	if (kind == R4::PinnedByV3Hello)
	{
		std::vector<RelayCore::Action> a = send(relay, addr(8509), hello3(p[0].ticket));
		REQUIRE(a.size() == 1);
		REQUIRE(a[0].reply[2] == kV3);
		REQUIRE(a[0].reply[3] == WELCOME);
		p[0].token.assign(a[0].reply.end() - (long)kSessionTokenBytes, a[0].reply.end());
	}
	for (int s = 0; s < 2; ++s)
	{
		if (old)
		{
			std::vector<RelayCore::Action> a = send(relay, p[s].from, hello3(p[s].ticket));
			REQUIRE(a.size() == 1);
			REQUIRE(a[0].reply[3] == WELCOME);
		}
		else
		{
			bind(relay, p[s]);
		}
	}
	for (int s = 0; s < 2; ++s)
	{
		const std::vector<std::uint8_t> d = encodeData((std::uint8_t)(1 - s), transportPacket((std::uint8_t)s, 0, frameInfo((std::uint8_t)s, 1)));
		REQUIRE(send(relay, p[s].from, old ? v3(d) : sealed(p[s], d)).size() == 1);
	}
	for (int s = 0; s < 2; ++s)
	{
		send(relay, p[s].from, old ? v3(encodeEmpty(BYE)) : sealed(p[s], encodeEmpty(BYE)));
	}
	REQUIRE(relay.closedLogs().size() == 1);
	out.logPath = relay.closedLogs()[0];
	out.log = readAll(out.logPath);
	out.sidecar = std::filesystem::exists(out.logPath + ".input");
	out.legacyLogAuthenticated = relay.stats().legacyLogAuthenticated;
}
std::string d7(const RelayLogFile &f)
{
	NetworkConfig config;
	config.slotPlayerIndex[0] = 3;
	config.slotPlayerIndex[1] = 4;
	RelayLog::Assembled a;
	return RelayLog::packetIntegrityReport(f, config, a, "0123456789abcdef");
}
} // namespace

TEST_CASE("online batch r4: a current participant's version-3 HELLO first pins a version-2 log: the old reader reads it, the sidecar holds the authenticated "
		  "players' INPUT records and the anomaly, and D7 reports a finding")
{
	R4Game g;
	playR4(R4::PinnedByV3Hello, g);
	CHECK(previousReleaseRefusal(g.log) == ""); // old clients' rejoin, spectating and the previous lobby's upload: unchanged
	REQUIRE(g.sidecar);
	CHECK(g.legacyLogAuthenticated == 2); // both players' authenticated HELLOs (slot 0's rebind, slot 1's bind): the relay's status counter
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(g.logPath, f, &error), error);
	CHECK(f.version == 2);
	CHECK(f.inputSidecar);
	CHECK(f.legacyAuthAnomaly);
	CHECK(f.openerSlot == 0);
	CHECK(f.authenticatedHellos == 2);
	REQUIRE(f.inputs.size() == 2);
	for (const RelayLogFile::Input &in : f.inputs)
	{
		CHECK(in.hadKey);
		CHECK(in.signedIn == 1);
	}
	const std::string r = d7(f);
	CHECK(r.find("\"status\":\"ok\"") != std::string::npos); // a finding, not an abstention
	CHECK(r.find("\"anomaly\":\"") != std::string::npos);
	CHECK(r.find("\"slot\":0,\"name\":\"\",\"band\":\"review\"") != std::string::npos); // the slot whose HELLO chose version 2
	CHECK(r.find("\"slot\":1,\"name\":\"\",\"band\":\"none\"") != std::string::npos);
	// the sidecar names its log: another log's (or a damaged) sidecar is an error, never read as this game's
	std::vector<std::uint8_t> side = readAll(g.logPath + ".input");
	RelayLogFile other;
	CHECK_FALSE(RelayLogFile::parseInputSidecar(side, "fedcba9876543210.obfmerlog", other, &error));
	side.pop_back();
	CHECK_FALSE(RelayLogFile::parseInputSidecar(side, std::filesystem::path(g.logPath).filename().string(), other, &error));
}

TEST_CASE("online batch r4: an all-previous-release game: a version-2 log and a sidecar with no authenticated slot and no anomaly")
{
	R4Game g;
	playR4(R4::AllOld, g);
	CHECK(previousReleaseRefusal(g.log) == "");
	REQUIRE(g.sidecar);
	CHECK(g.legacyLogAuthenticated == 0);
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(g.logPath, f, &error), error);
	CHECK(f.version == 2);
	CHECK(f.inputSidecar);
	CHECK_FALSE(f.legacyAuthAnomaly);
	CHECK(f.authenticatedHellos == 0);
	REQUIRE(f.inputs.size() == 2);
	for (const RelayLogFile::Input &in : f.inputs)
	{
		CHECK_FALSE(in.hadKey);
		CHECK(in.plainIn == 1);
	}
	const std::string r = d7(f);
	CHECK(r.find("\"status\":\"ok\"") != std::string::npos);
	CHECK(r.find("\"anomaly\":null") != std::string::npos);
	CHECK(r.find("\"band\":\"review\"") == std::string::npos);
}

TEST_CASE("online batch r4: a current game: version 4 with its INPUT records, no sidecar")
{
	R4Game g;
	playR4(R4::Current, g);
	CHECK_FALSE(g.sidecar);
	CHECK(g.legacyLogAuthenticated == 0);
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(g.logPath, f, &error), error);
	CHECK(f.version == 4);
	CHECK_FALSE(f.inputSidecar);
	REQUIRE(f.inputs.size() == 2);
	CHECK(d7(f).find("\"status\":\"ok\"") != std::string::npos);
}

TEST_CASE("online batch r6: a version-2 log without its sidecar: D7 abstains saying no INPUT sidecar; a sidecar over 2048 bytes is refused unread")
{
	R4Game g;
	playR4(R4::AllOld, g);
	REQUIRE(g.sidecar);
	const std::string side = g.logPath + ".input";
	// too large: refused before it is buffered (a valid one is at most 527 bytes)
	{
		std::ofstream f(side, std::ios::binary | std::ios::trunc);
		f << std::string(2049, 'x');
	}
	RelayLogFile big;
	std::string error;
	CHECK_FALSE(RelayLogFile::load(g.logPath, big, &error));
	CHECK(error.find("2049 bytes (at most 2048)") != std::string::npos);
	// none (a relay before the sidecar): the log reads, D7 abstains and says why
	std::filesystem::remove(side);
	RelayLogFile f;
	REQUIRE_MESSAGE(RelayLogFile::load(g.logPath, f, &error), error);
	CHECK_FALSE(f.inputSidecar);
	const std::string r = d7(f);
	CHECK(r.find("\"status\":\"abstained\"") != std::string::npos);
	CHECK(r.find("\"abstain_reason\":\"the relay log (version 2) has no INPUT records and no INPUT sidecar\"") != std::string::npos);
}

// ---- the full suite's crash: AptSkirmish::startGame fills MAX_SLOTS (16) honors slots into an eight-entry array (P16 left it eight wide) -----

TEST_CASE("online16-fix: the skirmish honors game holds every game slot; a 16-slot game's opponents beyond slot 7 count, a legacy game is unchanged")
{
	SkirmishHonorsGame g;
	CHECK(g.slots.size() == (size_t)MAX_SLOTS);
	g.valid = true;
	g.localSlot = 0;
	g.slots[0] = { 6, 0 };
	g.slots[12] = { 4, 1 }; // a Hard AI on another team, beyond the old eight
	CHECK_FALSE(g.isSandbox());
	CHECK(g.isAI(12));
	CHECK_FALSE(g.isAlly(12));
	g.slots[12] = { 4, 0 }; // the same AI as an ally: a sandbox again
	CHECK(g.isSandbox());
	CHECK(g.isAlly(12));
	// eight-slot games: slots 8 .. 15 stay closed, nothing there counts
	SkirmishHonorsGame legacy;
	legacy.slots[0] = { 6, 0 };
	legacy.slots[3] = { 5, 1 };
	legacy.localSlot = 0;
	CHECK_FALSE(legacy.isSandbox());
	for (int i = 8; i < MAX_SLOTS; ++i)
	{
		CHECK_FALSE(legacy.occupied(i));
	}
}

TEST_CASE("online16-fix: the session key is ANTICHEAT-1's OBFME-SESSION-1 derivation (the lobby's START session_key), pinned")
{
	// key bytes 0 .. 31, ticket version 1: key id 1, game 0123456789abcdef, slot 0, mask 7, expiry 2000000000 (lane/anticheat1's pin)
	std::vector<std::uint8_t> key;
	for (int i = 0; i < 32; ++i)
	{
		key.push_back((std::uint8_t)i);
	}
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = kGame;
	t.slot = 0;
	t.slotMask = 0x07;
	t.expires = 2000000000u;
	const std::vector<std::uint8_t> ticket = RelayProtocol::signTicket(t, key);
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(key.data(), key.size(), ticket.data(), ticket.size());
	CHECK(RelayProtocol::base64UrlEncode(k.data(), k.size()) == "cHLqM4pf8wcINff_naIhJg");
}
