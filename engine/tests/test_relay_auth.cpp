// OpenBFME unit tests. GPL-3.0.
// Lane AUTH-1 (docs/ONLINE.md "per-packet authentication"): the per-slot k derivation (the lobby's answer equals the
// relay's recomputation — the secret never crosses UDP), the version-5 envelope (counter + tag), the replay window,
// and the relay's behaviour on forged / tampered / replayed / cross-slot / downgrade / on-path datagrams, ending in
// a 16-slot authenticated game through the real openbfme_relay binary.

#include "doctest.h"

#include "Common/Crc32.h"
#include "Common/Sha256.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/RelayAuth.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "RelayCore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> testKey()
{
	std::vector<std::uint8_t> k;
	for (int i = 0; i < 32; ++i)
	{
		k.push_back((std::uint8_t)i);
	}
	return k;
}
RelayProtocol::KeyRing testRing()
{
	return { { 1, testKey() } };
}
RelayProtocol::Ticket testTicket(std::uint8_t slot, std::uint16_t mask = 0xFFFFu, std::uint64_t game = 0x0123456789abcdefull)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = game;
	t.slot = slot;
	t.slotMask = mask;
	t.expires = 2000000000u;
	return t;
}
std::vector<std::uint8_t> ticketBytes(const RelayProtocol::Ticket &t)
{
	const auto b = RelayProtocol::signTicket(t, testKey());
	return std::vector<std::uint8_t>(b.begin(), b.end());
}
RelayAddress addr(std::uint16_t port)
{
	return RelayAddress{ 0x0A000001u, port };
}
// the client's side of a binding: the k the lobby computed (delivered in the START) and its direction keys
struct Client
{
	RelayAddress from;
	RelayAuth::Keys keys;
	std::uint64_t sent = 0;
	RelayAuth::Window recv;
};
Client bindClient(int slot, std::uint16_t port)
{
	Client c;
	c.from = addr((std::uint16_t)port);
	const std::vector<std::uint8_t> ticket = ticketBytes(testTicket((std::uint8_t)slot));
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	c.keys = RelayAuth::packetKeys(k, 0x0123456789abcdefull, (std::uint8_t)slot, 0);
	return c;
}
std::vector<std::uint8_t> hello5(Client &c, const std::vector<std::uint8_t> &ticket)
{
	std::vector<std::uint8_t> d = RelayProtocol::encodeHello(ticket);
	RelayAuth::seal(d, c.keys.c2r, c.sent++);
	return d;
}
std::vector<std::uint8_t> hello5(Client &c, int slot)
{
	return hello5(c, ticketBytes(testTicket((std::uint8_t)slot)));
}
std::vector<std::uint8_t> data5(Client &c, int target, const std::vector<std::uint8_t> &packet)
{
	std::vector<std::uint8_t> d = RelayProtocol::encodeData((std::uint8_t)target, packet);
	RelayAuth::seal(d, c.keys.c2r, c.sent++);
	return d;
}
// a well-formed transport packet (version 2) from `sender` with one record
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
// drives the core: handle() and the pending drain; a FORWARD's outgoing bytes are the rewritten input
std::vector<RelayCore::Action> send(RelayCore &relay, const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now = 1000,
	std::uint32_t unix = 1700000000u)
{
	std::vector<RelayCore::Action> out;
	RelayCore::Action a = relay.handle(from, d.data(), d.size(), now, unix);
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
// unwraps one answer as the client does (the sealed envelope becomes the v4 datagram in place)
std::vector<std::uint8_t> openReply(Client &c, const RelayCore::Action &a)
{
	std::vector<std::uint8_t> d = a.reply;
	if (d.size() < 4 || d[2] != RelayProtocol::kVersionAuth)
	{
		return {};
	}
	const size_t n = RelayAuth::open(d.data(), d.size(), c.keys.r2c, &c.recv);
	if (!n)
	{
		return {};
	}
	d.resize(n);
	return d;
}
} // namespace

TEST_CASE("auth: the slot's k and its direction keys derive as specified, and never from wire-visible material")
{
	const std::vector<std::uint8_t> ticket = ticketBytes(testTicket(3));
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	// deterministic and ticket-bound: another slot's ticket, another key, or one changed byte each give another k
	CHECK(k == RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size()));
	const std::vector<std::uint8_t> other = ticketBytes(testTicket(4));
	CHECK_FALSE(k == RelayAuth::sessionKey(testKey().data(), testKey().size(), other.data(), other.size()));
	std::vector<std::uint8_t> wrong = testKey();
	wrong[0] ^= 1;
	CHECK_FALSE(k == RelayAuth::sessionKey(wrong.data(), wrong.size(), ticket.data(), ticket.size()));
	std::vector<std::uint8_t> tampered = ticket;
	tampered[10] ^= 1;
	CHECK_FALSE(k == RelayAuth::sessionKey(testKey().data(), testKey().size(), tampered.data(), tampered.size()));
	const RelayAuth::Keys keys = RelayAuth::packetKeys(k, 0x0123456789abcdefull, 3, 0);
	CHECK(keys.c2r != keys.r2c); // the directions differ
	// a rejoin ticket's fresh generation carries its own direction keys from its own k
	const RelayAuth::SessionKey k2 = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	CHECK_FALSE(RelayAuth::packetKeys(k2, 0x0123456789abcdefull, 3, 1).c2r == keys.c2r);
}

TEST_CASE("auth: the sealed envelope round-trips; a tampered byte, a replay and a stale counter fail")
{
	RelayAuth::Window w;
	const std::vector<std::uint8_t> ticket = ticketBytes(testTicket(0));
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	const RelayAuth::Keys keys = RelayAuth::packetKeys(k, 0x0123456789abcdefull, 0, 0);
	std::vector<std::uint8_t> d = RelayProtocol::encodeHello(ticket);
	const size_t plain = d.size();
	RelayAuth::seal(d, keys.c2r, 0);
	CHECK(d.size() == plain + RelayAuth::kOverhead);
	CHECK(d[2] == RelayProtocol::kVersionAuth);
	std::vector<std::uint8_t> copy = d;
	const size_t n = RelayAuth::open(copy.data(), copy.size(), keys.c2r, &w);
	REQUIRE(n == plain);
	CHECK(copy[2] == RelayProtocol::kVersion);
	const std::vector<std::uint8_t> want = RelayProtocol::encodeHello(ticket);
	CHECK(std::equal(copy.begin(), copy.begin() + (long)n, want.begin(), want.end()));
	// replayed counter: dropped
	std::vector<std::uint8_t> again = d;
	CHECK(RelayAuth::open(again.data(), again.size(), keys.c2r, &w) == 0);
	// tampered byte: dropped
	std::vector<std::uint8_t> bad = d;
	bad[20] ^= 1;
	RelayAuth::Window w2;
	CHECK(RelayAuth::open(bad.data(), bad.size(), keys.c2r, &w2) == 0);
	// wrong key: dropped
	CHECK(RelayAuth::open(d.data(), d.size(), keys.r2c, &w2) == 0);
	// the window: fresh counters accept, reordering within it accepted, older than it dropped
	RelayAuth::Window win;
	CHECK(win.accept(10));
	CHECK(win.accept(12)); // ahead: slides
	CHECK(win.accept(11)); // inside, unseen: accepted (reordered)
	CHECK_FALSE(win.accept(11));
	CHECK_FALSE(win.accept(10));
	CHECK_FALSE(win.accept(12));
	CHECK(win.accept(75)); // slides past 11..74
	CHECK_FALSE(win.accept(10)); // now outside the window
	CHECK(win.accept(74));
}

TEST_CASE("auth: the relay authenticates every datagram — forged, tampered, replayed, cross-slot and downgraded ones drop silently")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Client c0 = bindClient(0, 1001), c1 = bindClient(1, 1002);
	// both bind over the authenticated envelope: the WELCOME comes back sealed and verifies
	std::vector<RelayCore::Action> a = send(relay, c0.from, hello5(c0, 0));
	REQUIRE(a.size() == 1);
	CHECK(a[0].kind == RelayCore::Action::REPLY);
	const std::vector<std::uint8_t> w0 = openReply(c0, a[0]);
	REQUIRE(w0.size() == kHeaderBytes + 9 + kSessionTokenBytes);
	CHECK(w0[3] == WELCOME);
	a = send(relay, c1.from, hello5(c1, 1));
	const std::vector<std::uint8_t> w1 = openReply(c1, a[0]);
	REQUIRE(w1[3] == WELCOME);
	CHECK(relay.stats().authDropped == 0);
	const std::vector<std::uint8_t> packet = transportPacket(0, 0, { 1, 2, 3 });
	// a good DATA forwards to the authed target as a sealed REPLY (a FORWARD cannot grow in place)
	a = send(relay, c0.from, data5(c0, 1, packet));
	REQUIRE(a.size() == 1);
	CHECK(a[0].to == c1.from);
	CHECK(a[0].kind == RelayCore::Action::REPLY); // sealed, not the in-place FORWARD
	const std::vector<std::uint8_t> got = openReply(c1, a[0]);
	REQUIRE(got.size() == packet.size() + kHeaderBytes + 1); // DATA_FROM adds the sender byte to the packet
	CHECK(got[3] == DATA_FROM);
	CHECK(got[4] == 0);
	CHECK(std::equal(got.begin() + 5, got.end(), packet.begin(), packet.end()));
	// a tampered byte: dropped
	{
		std::vector<std::uint8_t> bad = data5(c0, 1, packet);
		bad[20] ^= 1;
		CHECK(send(relay, c0.from, bad).empty());
		CHECK(relay.stats().authDropped == 1);
	}
	// a replayed datagram: dropped
	{
		std::vector<std::uint8_t> replay;
		Client attacker = c0;
		replay = data5(attacker, 1, packet); // reuses c0's keys but a counter already spent below
		// build it with the OLD counter: what an on-path replay looks like
		std::vector<std::uint8_t> again = RelayProtocol::encodeData(1, packet);
		RelayAuth::seal(again, c0.keys.c2r, 0); // counter 0 was the HELLO's — already consumed
		CHECK(send(relay, c0.from, again).empty());
		CHECK(relay.stats().authDropped == 2);
	}
	// MACed with another slot's key: dropped
	{
		std::vector<std::uint8_t> wrong = RelayProtocol::encodeData(1, packet);
		RelayAuth::seal(wrong, c1.keys.c2r, 99);
		CHECK(send(relay, c0.from, wrong).empty());
		CHECK(relay.stats().authDropped == 3);
	}
	// a forged source address (the attacker's UDP endpoint, the bound slot's bytes): dropped — the ticket-derived
	// key is checked against the ADDRESS's binding, and the slot the bytes name has no other address
	{
		Client evil = bindClient(0, 9999); // the same ticket from elsewhere: its HELLO verifies but cannot take the slot
		(void)evil;
		std::vector<std::uint8_t> forged = data5(c0, 1, packet);
		std::vector<RelayCore::Action> fa = send(relay, addr(9999), forged); // the slot is bound to another address:
		REQUIRE(fa.size() == 1); // the forged source is treated as unbound — answered NOT_BOUND, unsigned
		CHECK(fa[0].reply == encodeEmpty(NOT_BOUND));
		CHECK(relay.stats().authDropped == 3); // unbound is not an auth failure
		CHECK(relay.stats().notBound == 1);
	}
	// a reordered-but-fresh datagram is accepted
	{
		std::vector<std::uint8_t> fresh = RelayProtocol::encodeData(1, packet);
		RelayAuth::seal(fresh, c0.keys.c2r, c0.sent + 4); // ahead of the next one, inside the window
		a = send(relay, c0.from, fresh);
		REQUIRE(a.size() == 1);
		CHECK(openReply(c1, a[0])[3] == DATA_FROM);
		c0.sent += 5;
	}
	// a v4 datagram from an authenticated slot is a downgrade: dropped
	{
		CHECK(send(relay, c0.from, RelayProtocol::encodeData(1, packet)).empty());
		CHECK(relay.stats().authDropped == 4);
	}
	// a forged DATA_FROM to the client is dropped by its own window/key check — openReply() proves it
	{
		std::vector<std::uint8_t> forged = RelayProtocol::encodeData(0, transportPacket(9, 0, { 1 }));
		forged[3] = DATA_FROM;
		forged[4] = 1;
		const std::vector<std::uint8_t> otherTicket = ticketBytes(testTicket(5));
		const RelayAuth::Key other = RelayAuth::packetKeys(
			RelayAuth::sessionKey(testKey().data(), testKey().size(), otherTicket.data(), otherTicket.size()), 0x0123456789abcdefull, 5, 0).r2c;
		RelayCore::Action fa;
		fa.kind = RelayCore::Action::REPLY;
		RelayAuth::seal(forged, other, 0);
		fa.reply = forged;
		CHECK(openReply(c1, fa).empty());
	}
	CHECK(relay.stats().forwarded == 2);
}

TEST_CASE("auth: an on-path reader of the clear HELLO + WELCOME still cannot forge for the slot, nor speak as the relay")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Client c0 = bindClient(0, 2001), c1 = bindClient(1, 2002);
	// the attacker sees both datagrams in clear: the HELLO (ticket + any session token) and the sealed WELCOME
	const std::vector<std::uint8_t> seenHello = hello5(c0, 0);
	std::vector<RelayCore::Action> a = send(relay, c0.from, seenHello);
	const std::vector<std::uint8_t> seenWelcome = a[0].reply;
	send(relay, c1.from, hello5(c1, 1));
	// knowing the ticket and the session token buys no tag: the attacker does not hold the ticket key, so the k it
	// can reach is not the slot's — a DATA "from" the slot, fresh counter and all, verifies only under the real k
	const std::uint8_t *seenTicket = seenHello.data() + kHeaderBytes + RelayAuth::kCounterBytes;
	const size_t seenTicketBytes = ticketSize(seenTicket[0]);
	// the honest relay recomputes the same k from the HELLO's ticket — the check that the protocol accepts valid MACs
	const RelayAuth::Keys real = RelayAuth::packetKeys(
		RelayAuth::sessionKey(testKey().data(), testKey().size(), seenTicket, seenTicketBytes), 0x0123456789abcdefull, 0, 0);
	std::vector<std::uint8_t> valid = encodeData(1, transportPacket(0, 1, { 9 }));
	RelayAuth::seal(valid, real.c2r, c0.sent + 1);
	CHECK(send(relay, c0.from, valid).size() == 1);
	// what the attacker cannot do is reach k: a tag computed from the session token instead fails
	std::vector<std::uint8_t> guess = encodeData(1, transportPacket(0, 2, { 9 }));
	RelayAuth::Key weak;
	std::memcpy(weak.data(), seenWelcome.data() + kHeaderBytes + 9, kSessionTokenBytes); // the token, the old idea
	RelayAuth::seal(guess, weak, c0.sent + 2);
	CHECK(send(relay, c0.from, guess).empty());
	CHECK(relay.stats().authDropped == 1);
	// nor can it speak as the relay: a forged WELCOME-shaped datagram under any ticket-derived key fails the
	// client's r2c check — only the real k opens its window
	std::vector<std::uint8_t> fakeWelcome = encodeWelcome(0x0123456789abcdefull, 0, {});
	RelayAuth::SessionKey weakSession{}; // the session token taken as a session key (the 16 bytes an on-path reader has)
	std::memcpy(weakSession.data(), weak.data(), weakSession.size());
	RelayAuth::seal(fakeWelcome, RelayAuth::packetKeys(weakSession, 0x0123456789abcdefull, 0, 0).r2c, 0);
	CHECK(RelayAuth::open(fakeWelcome.data(), fakeWelcome.size(), c0.keys.r2c, &c0.recv) == 0);
}

TEST_CASE("auth: a mode-flagged ticket requires the authenticated envelope — a plain HELLO is refused")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	RelayProtocol::Ticket t = testTicket(0);
	t.mode = TICKET_MODE_AUTH;
	// the v4 (unauthenticated) HELLO carrying the auth-required ticket: refused, counted, and the game never opens
	std::vector<RelayCore::Action> a = send(relay, addr(3001), encodeHello(ticketBytes(t)));
	REQUIRE(a.size() == 1);
	CHECK(a[0].reply == encodeRefused(REFUSE_AUTH_REQUIRED));
	CHECK(relay.openGames() == 0);
	// the same ticket over v5 binds (the HELLO carries the mode-flagged ticket, sealed with its k)
	Client c = bindClient(0, 3001);
	const std::vector<std::uint8_t> authTicket = ticketBytes(t);
	c.keys = RelayAuth::packetKeys(RelayAuth::sessionKey(testKey().data(), testKey().size(), authTicket.data(), authTicket.size()),
		t.gameId, 0, 0);
	a = send(relay, c.from, hello5(c, authTicket));
	CHECK(openReply(c, a[0])[3] == WELCOME);
	CHECK(relay.openGames() == 1);
	// and a rejoin- or spectator-style HELLO with no mode of its own is refused plain too once the mode is bound
	RelayProtocol::Ticket r = testTicket(1);
	r.generation = 0; // a plain player ticket of another slot into the AUTH game
	a = send(relay, addr(3002), encodeHello(ticketBytes(r)));
	CHECK(a[0].reply == encodeRefused(REFUSE_AUTH_REQUIRED));
}

TEST_CASE("auth: an old unauthenticated client still plays with the new relay in a legacy game")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	// v4 HELLOs of plain tickets: welcomed and forwarded exactly as before
	std::vector<RelayCore::Action> a = send(relay, addr(4001), encodeHello(ticketBytes(testTicket(0))));
	CHECK(a[0].reply.size() == kHeaderBytes + 9 + kSessionTokenBytes);
	CHECK(a[0].reply[3] == WELCOME);
	a = send(relay, addr(4002), encodeHello(ticketBytes(testTicket(1))));
	CHECK(a[0].reply[3] == WELCOME);
	const std::vector<std::uint8_t> packet = transportPacket(0, 0, { 1 });
	a = send(relay, addr(4001), encodeData(1, packet));
	REQUIRE(a.size() == 1);
	CHECK(a[0].kind == RelayCore::Action::FORWARD);
	CHECK(a[0].to == addr(4002));
	CHECK(relay.stats().authDropped == 0);
	CHECK(relay.stats().forwarded == 1);
}

TEST_CASE("auth: the START's absent macKey keeps the game on the legacy envelope (an old lobby's START)")
{
	// Transport with relay + ticket but no macKey: m_auth stays false — it speaks version 4, never a silent weak key
	UDP socket;
	std::string error;
	REQUIRE(socket.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = NetAddress{ 0x7F000001u, 50000 }; // nothing listens: the HELLOs simply go out
	o.relay.ticket = ticketBytes(testTicket(0));
	Transport t(socket, 0, o);
	CHECK(t.errors().empty());
	// and with a macKey of the wrong size it stays legacy too (and complains)
	o.relay.macKey.assign(8, 1);
	Transport t2(socket, 0, o);
	bool complained = false;
	for (const std::string &e : t2.errors())
	{
		complained = complained || e.find("macKey") != std::string::npos;
	}
	CHECK(complained);
	// a well-formed macKey authenticates: its HELLO leaves in the v5 envelope
	o.relay.macKey = std::vector<std::uint8_t>(RelayAuth::kSessionKeyBytes, 0);
	const std::vector<std::uint8_t> ticket = ticketBytes(testTicket(0));
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	std::memcpy((void *)o.relay.macKey.data(), k.data(), k.size());
	UDP probe;
	REQUIRE(probe.bind(0x7F000001u, 50000, &error));
	{
		Transport t3(socket, 0, o);
		t3.setClock([] { return (std::uint64_t)1000; });
		t3.service(); // sends its HELLO
		NetAddress from;
		std::vector<std::uint8_t> d;
		// the datagram is version 5 and opens under the slot's own c2r key
		for (int i = 0; i < 20 && !probe.receiveFrom(from, d); ++i)
		{
			NetSleepMilliseconds(10);
		}
		REQUIRE(!d.empty());
		CHECK(d[2] == RelayProtocol::kVersionAuth);
		const size_t n = RelayAuth::open(d.data(), d.size(),
			RelayAuth::packetKeys(k, 0x0123456789abcdefull, 0, 0).c2r, nullptr);
		REQUIRE(n); // a zero return is the drop
		CHECK(d[3] == RelayProtocol::HELLO);
	}
}

#include "Common/MiniJson.h"

namespace
{
// lane NETDIAG-1: a transport packet (version 2) from `sender` with a chosen cumulative ack and (seq, bytes) records
std::vector<std::uint8_t> ackedPacket(std::uint8_t sender, std::uint32_t ack, const std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> &records)
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
	put32(ack);
	put16((std::uint16_t)records.size());
	for (const auto &[seq, bytes] : records)
	{
		put32(seq);
		put16(0);
		put16(1);
		put32((std::uint32_t)bytes.size());
		d.insert(d.end(), bytes.begin(), bytes.end());
	}
	const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		d[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return d;
}
std::vector<std::uint8_t> frameInfoCommand(std::uint8_t slot, std::uint32_t frame)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = slot;
	c.executionFrame = frame;
	return NetPacket::encodeCommand(c, nullptr);
}
// one game's entry of the relay's health snapshot
JsonValue healthGame(RelayCore &relay, std::uint64_t uptimeMs, JsonValue &health)
{
	std::string error;
	REQUIRE_MESSAGE(JsonValue::parse(relay.healthJson(uptimeMs), health, &error), error);
	REQUIRE(health.get("games")->array.size() == 1);
	return health.get("games")->array[0];
}
} // namespace

// lane NETDIAG-1: the relay health reads the transport packet AUTH-1's envelope carried, opened (handle() compacts it to the version-4 view before
// data() runs): the ack at +8 and the frame headers are the client's, never the counter / tag bytes of the sealed datagram
TEST_CASE("netdiag1: relay health of an authenticated two-slot game reads the opened packets")
{
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	Client c0 = bindClient(0, 1001), c1 = bindClient(1, 1002);
	REQUIRE(send(relay, c0.from, hello5(c0, 0)).size() == 1);
	REQUIRE(send(relay, c1.from, hello5(c1, 1)).size() == 1);
	auto forwarded = [&](Client &from, int target, const std::vector<std::uint8_t> &packet, std::uint64_t now) {
		const std::vector<RelayCore::Action> a = send(relay, from.from, data5(from, target, packet), now);
		return a.size() == 1 && a[0].kind == RelayCore::Action::REPLY; // an authenticated target's copy is sealed
	};
	CHECK(forwarded(c0, 1, ackedPacket(0, 0, { { 0, frameInfoCommand(0, 10) } }), 100));
	CHECK(forwarded(c1, 0, ackedPacket(1, 1, {}), 130)); // samples 30 ms
	CHECK(forwarded(c0, 1, ackedPacket(0, 0, { { 1, frameInfoCommand(0, 12) } }), 200));
	CHECK(forwarded(c0, 1, ackedPacket(0, 0, { { 2, frameInfoCommand(0, 11) } }), 210)); // a late frame
	CHECK(forwarded(c1, 0, ackedPacket(1, 3, {}), 260)); // covers both: the newest (210) samples 50 ms, jitter 20
	CHECK(forwarded(c0, 1, ackedPacket(0, 0, { { 1, frameInfoCommand(0, 12) } }), 270)); // a transport resend under a fresh envelope counter
	CHECK(relay.stats().authDropped == 0);
	JsonValue health;
	const JsonValue game = healthGame(relay, 1000, health);
	CHECK(health.get("players")->number == 2);
	CHECK(game.get("players")->number == 2);
	CHECK(game.get("resends")->number == 1);
	CHECK(game.get("lateFrames")->number == 1);
	CHECK(game.get("dataDrops")->number == 0);
	CHECK(game.get("ackRtt")->get("samples")->number == 2);
	CHECK(game.get("ackRtt")->get("p50Ms")->number == 30);
	CHECK(game.get("ackRtt")->get("p99Ms")->number == 50);
	CHECK(game.get("ackJitter")->get("samples")->number == 1);
	CHECK(game.get("ackJitter")->get("p50Ms")->number == 20);
}

// lane NETDIAG-1 (the contributor's table was [8][8]: slot 8 and up indexed past it): a sixteen-slot authenticated game, traffic among slots 8..15
TEST_CASE("netdiag1: relay health of a sixteen-slot authenticated game covers slots 8..15")
{
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	std::vector<Client> c;
	for (int slot = 0; slot < 16; ++slot)
	{
		c.push_back(bindClient(slot, (std::uint16_t)(2000 + slot)));
		REQUIRE(send(relay, c.back().from, hello5(c.back(), slot)).size() == 1);
	}
	auto forwarded = [&](int from, int target, const std::vector<std::uint8_t> &packet, std::uint64_t now) {
		const std::vector<RelayCore::Action> a = send(relay, c[(size_t)from].from, data5(c[(size_t)from], target, packet), now);
		return a.size() == 1 && a[0].kind == RelayCore::Action::REPLY && a[0].to == c[(size_t)target].from;
	};
	// pairs 8-15, 9-14, 10-13, 11-12: each sends a frame, its partner acknowledges it 200 ms later
	for (int s = 8; s < 16; ++s)
	{
		CHECK(forwarded(s, 23 - s, ackedPacket((std::uint8_t)s, 0, { { 0, frameInfoCommand((std::uint8_t)s, 10) } }), 100 + (std::uint64_t)s));
	}
	for (int s = 8; s < 16; ++s)
	{
		CHECK(forwarded(23 - s, s, ackedPacket((std::uint8_t)(23 - s), 1, {}), 300 + (std::uint64_t)s));
	}
	CHECK(forwarded(15, 8, ackedPacket(15, 1, { { 1, frameInfoCommand(15, 20) } }), 400));
	CHECK(forwarded(15, 8, ackedPacket(15, 1, { { 2, frameInfoCommand(15, 15) } }), 410)); // late
	CHECK(forwarded(15, 8, ackedPacket(15, 1, { { 2, frameInfoCommand(15, 15) } }), 420)); // resent
	// a target beyond the slot space: refused before the health table is touched (a data drop)
	CHECK(send(relay, c[12].from, data5(c[12], 16, ackedPacket(12, 0, { { 1, frameInfoCommand(12, 11) } })), 430).empty());
	CHECK(relay.stats().authDropped == 0);
	CHECK(relay.stats().badTargets == 1);
	JsonValue health;
	const JsonValue game = healthGame(relay, 1000, health);
	CHECK(health.get("players")->number == 16);
	CHECK(health.get("dataDrops")->number == 1);
	CHECK(health.get("metricsBytes")->number == sizeof(RelayHealth));
	CHECK(game.get("players")->number == 16);
	CHECK(game.get("dataDrops")->number == 1);
	CHECK(game.get("resends")->number == 1);
	CHECK(game.get("lateFrames")->number == 1);
	CHECK(game.get("ackRtt")->get("samples")->number == 8);
	CHECK(game.get("ackRtt")->get("p50Ms")->number == 200);
	CHECK(game.get("ackRtt")->get("p99Ms")->number == 200);
	CHECK(game.get("ackJitter")->get("samples")->number == 0);
}

TEST_CASE("auth: the per-packet MAC cost — keyed BLAKE2b-16 (chosen) vs HMAC-SHA256")
{
	const std::vector<std::uint8_t> ticket = ticketBytes(testTicket(0));
	const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
	const RelayAuth::Keys keys = RelayAuth::packetKeys(k, 0x0123456789abcdefull, 0, 0);
	for (const size_t size : { (size_t)64, (size_t)256, (size_t)1200 })
	{
		std::vector<std::uint8_t> d(size, 0x42);
		std::uint8_t out[RelayAuth::kTagBytes];
		const int rounds = 20000;
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < rounds; ++i)
		{
			RelayAuth::tag(keys.c2r, d.data(), d.size(), out);
		}
		const auto t1 = std::chrono::steady_clock::now();
		for (int i = 0; i < rounds; ++i)
		{
			const std::array<std::uint8_t, 32> h = HmacSha256(keys.c2r.data(), keys.c2r.size(), d.data(), d.size());
			std::memcpy(out, h.data(), sizeof out);
		}
		const auto t2 = std::chrono::steady_clock::now();
		MESSAGE("auth MAC cost, " << size << " B datagram: keyed BLAKE2b-16 "
			<< std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / rounds << " ns; HMAC-SHA256 "
			<< std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / rounds << " ns");
	}
	CHECK(true);
}

#ifdef OPENBFME_RELAY_EXE
#include "TestChild.h"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
TEST_CASE("auth: a 16-slot authenticated game through the real openbfme_relay binary")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme_auth_" + std::to_string(NetMilliseconds()));
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
	std::uint16_t port = 0;
	{
		UDP tmp;
		std::string error;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		port = tmp.localAddress().port;
	}
	const std::string bind = "127.0.0.1:" + std::to_string(port), keys = (dir / "keys.txt").string(), logs = dir.string(),
					  status = (dir / "status.json").string();
	TestChild relay(OPENBFME_RELAY_EXE, { "--bind", bind, "--keys", keys, "--log-dir", logs, "--status", status, "--stats-s", "0" }, (dir / "relay.out").string());
	REQUIRE(relay.started());
	// sixteen clients, each on its own socket, each with its slot's k
	std::vector<std::unique_ptr<UDP>> sockets;
	std::vector<Client> clients;
	std::string error;
	for (int i = 0; i < 16; ++i)
	{
		auto u = std::make_unique<UDP>();
		REQUIRE(u->bind(0x7F000001u, 0, &error));
		Client c;
		c.from = addr(0);
		const std::vector<std::uint8_t> ticket = ticketBytes(testTicket((std::uint8_t)i));
		const RelayAuth::SessionKey k = RelayAuth::sessionKey(testKey().data(), testKey().size(), ticket.data(), ticket.size());
		c.keys = RelayAuth::packetKeys(k, 0x0123456789abcdefull, (std::uint8_t)i, 0);
		sockets.push_back(std::move(u));
		clients.push_back(std::move(c));
	}
	const NetAddress relayAddr{ 0x7F000001u, port };
	const std::uint64_t deadline = NetMilliseconds() + 10000;
	// every client reads a sealed WELCOME and opens it with its r2c key; HELLOs resend until it does (the relay
	// may still be coming up, and a single lost datagram must not stall the test)
	bool welcomed[16] = {};
	while (!std::all_of(welcomed, welcomed + 16, [](bool b) { return b; }) && NetMilliseconds() < deadline)
	{
		for (int i = 0; i < 16; ++i)
		{
			if (!welcomed[i])
			{
				const std::vector<std::uint8_t> hello = hello5(clients[i], i);
				sockets[i]->sendTo(relayAddr, hello.data(), hello.size());
			}
			NetAddress from;
			std::vector<std::uint8_t> d;
			while (sockets[i]->receiveFrom(from, d))
			{
				const size_t n = RelayAuth::open(d.data(), d.size(), clients[i].keys.r2c, &clients[i].recv);
				welcomed[i] = welcomed[i] || (n > 0 && d[3] == WELCOME);
			}
		}
		NetSleepMilliseconds(20);
	}
	CHECK(std::all_of(welcomed, welcomed + 16, [](bool b) { return b; }));
	// slot 0 sends a sealed DATA to slot 15; it arrives sealed and opens
	const std::vector<std::uint8_t> packet = transportPacket(0, 0, { 1, 2, 3 });
	const std::vector<std::uint8_t> data = data5(clients[0], 15, packet);
	sockets[0]->sendTo(relayAddr, data.data(), data.size());
	bool got = false;
	while (!got && NetMilliseconds() < deadline)
	{
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (sockets[15]->receiveFrom(from, d))
		{
			const size_t n = RelayAuth::open(d.data(), d.size(), clients[15].keys.r2c, &clients[15].recv);
			got = n > 0 && d[3] == DATA_FROM;
		}
		NetSleepMilliseconds(10);
	}
	CHECK(got);
	// a forged v5 datagram (the right shape, a garbage key) is dropped: status.json counts it
	std::vector<std::uint8_t> forged = RelayProtocol::encodeData(15, packet);
	RelayAuth::Key junk{};
	junk[0] = 0xFF;
	RelayAuth::seal(forged, junk, 7);
	sockets[0]->sendTo(relayAddr, forged.data(), forged.size());
	// ONLINE16-FIX (AUTH-1 review): forgeries from ANOTHER bound client's own socket. Slot 1 seals slot 0's ticket and
	// session-less HELLO with its own key (dropped: a HELLO verifies only under its own ticket's key); it sends a DATA
	// that speaks for slot 0 under its own valid key (the slot check refuses it: a slot mismatch, not an auth drop); and
	// it downgrades to a plain version-4 DATA (dropped)
	{
		std::vector<std::uint8_t> takeover = RelayProtocol::encodeHello(ticketBytes(testTicket(0)), {}, CAP_AUTH);
		RelayAuth::seal(takeover, clients[1].keys.c2r, clients[1].sent++);
		sockets[1]->sendTo(relayAddr, takeover.data(), takeover.size());
		const std::vector<std::uint8_t> speaksFor0 = data5(clients[1], 15, transportPacket(0, 1, { 4 }));
		sockets[1]->sendTo(relayAddr, speaksFor0.data(), speaksFor0.size());
		const std::vector<std::uint8_t> plain = RelayProtocol::encodeData(15, transportPacket(1, 0, { 5 }));
		sockets[1]->sendTo(relayAddr, plain.data(), plain.size());
	}
	NetSleepMilliseconds(100);
	const int st = relay.stop();
	CHECK(WIFEXITED(st));
	const std::vector<std::uint8_t> sb = [](const std::filesystem::path &p) {
		std::ifstream f(p, std::ios::binary);
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}(status);
	const std::string text(sb.begin(), sb.end());
	CHECK(text.find("\"gamesOpened\":1") != std::string::npos);
	CHECK(text.find("\"authDropped\":") != std::string::npos);
	CHECK(text.find("\"authDropped\":3}") != std::string::npos); // the junk-key DATA, slot 1's takeover HELLO, its plain DATA
	CHECK(text.find("\"slotMismatches\":1,") != std::string::npos); // slot 1 speaking for slot 0 under its own key
	// lane NETDIAG-1: the status file nests the health snapshot (before authDropped); the slot mismatch is its one data drop, the auth drops are not
	CHECK(text.find("\"health\":{\"v\":1,\"event\":\"relay_health\",") != std::string::npos);
	CHECK(text.find("\"dataDrops\":1,\"resends\":") != std::string::npos);
	CHECK(text.find("\"games\":[]},\"authDropped\":3}") != std::string::npos);
	std::filesystem::remove_all(dir);
}
#endif

#include "GameNetwork/RelayLog.h"

// lane ANTICHEAT-1 (with the relay lead, 2026-10-21): the relay log's INPUT records (version 4, kind 7) count what each slot SENT as AUTH-1 verified it,
// for the integrity check D7 (openbfme_peer --relay-log --packet-check)
TEST_CASE("auth: the relay log's INPUT records count each slot's verified, plain, tampered, replayed and downgraded datagrams (D7)")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("openbfme_auth_input_" + std::to_string(std::chrono::steady_clock::now()
		.time_since_epoch().count()));
	std::filesystem::create_directories(dir);
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	Client c0 = bindClient(0, 5001), c1 = bindClient(1, 5002);
	REQUIRE(send(relay, c0.from, hello5(c0, 0)).size() == 1);
	REQUIRE(send(relay, c1.from, hello5(c1, 1)).size() == 1);
	// slot 2: an older client, unauthenticated (a legacy HELLO of a plain ticket)
	REQUIRE(send(relay, addr(5003), encodeHello(ticketBytes(testTicket(2)))).size() == 1);
	const std::vector<std::uint8_t> packet = transportPacket(0, 0, { 1, 2, 3 });
	CHECK(send(relay, c0.from, data5(c0, 1, packet)).size() == 1);                          // verified
	CHECK(send(relay, c0.from, data5(c0, 1, transportPacket(0, 1, { 4 }))).size() == 1);  // verified
	std::vector<std::uint8_t> tampered = data5(c0, 1, packet);
	tampered[20] ^= 1;
	CHECK(send(relay, c0.from, tampered).empty());                                         // bad tag
	std::vector<std::uint8_t> again = RelayProtocol::encodeData(1, packet);
	RelayAuth::seal(again, c0.keys.c2r, 0);
	CHECK(send(relay, c0.from, again).empty());                                            // replayed (the HELLO's counter)
	CHECK(send(relay, c0.from, encodeData(1, packet)).empty());                            // a downgrade: unauthenticated to an authed binding
	CHECK(send(relay, c0.from, data5(c0, 1, transportPacket(1, 2, { 5 }))).empty());      // verified, but speaks for slot 1
	CHECK(send(relay, c1.from, data5(c1, 0, transportPacket(1, 0, { 6 }))).size() == 1);  // verified
	CHECK(send(relay, addr(5003), encodeData(0, transportPacket(2, 0, { 7 }))).size() == 1); // plain (legacy)
	// every bound slot says BYE (sealed for the authenticated ones): the game closes, its log is written
	std::vector<std::uint8_t> bye0 = encodeEmpty(BYE), bye1 = encodeEmpty(BYE);
	RelayAuth::seal(bye0, c0.keys.c2r, c0.sent++);
	RelayAuth::seal(bye1, c1.keys.c2r, c1.sent++);
	send(relay, c0.from, bye0);
	send(relay, c1.from, bye1);
	send(relay, addr(5003), encodeEmpty(BYE));
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	CHECK(log.version == 4);
	REQUIRE(log.inputs.size() == 3);
	const RelayLogFile::Input &i0 = log.inputs[0], &i1 = log.inputs[1], &i2 = log.inputs[2];
	CHECK(i0.slot == 0);
	CHECK(i0.hadKey);
	CHECK(i0.signedIn == 3);
	CHECK(i0.plainIn == 0);
	CHECK(i0.badTag == 1);
	CHECK(i0.replayed == 1);
	CHECK(i0.unsignedRefused == 1);
	CHECK(i0.slotMismatch == 1);
	CHECK(i0.malformed == 0);
	CHECK(i1.slot == 1);
	CHECK(i1.hadKey);
	CHECK(i1.signedIn == 1);
	CHECK(i1.badTag + i1.replayed + i1.unsignedRefused + i1.slotMismatch == 0);
	CHECK(i2.slot == 2);
	CHECK_FALSE(i2.hadKey);
	CHECK(i2.signedIn == 0);
	CHECK(i2.plainIn == 1);
	std::filesystem::remove_all(dir);
}
