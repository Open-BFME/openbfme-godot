// OpenBFME unit tests. GPL-3.0.
// Lane NET-SYNC: the canonical-mode negotiation. The mode is a property of the GAME, signed into the lobby's START ticket (a
// version-7 ticket: the version-4 layout plus a u8 mode byte). A v4-envelope HELLO carries the client's capability bits; the
// relay binds a game with the first player START ticket's mode, refuses a HELLO whose capabilities lack that mode
// (REFUSE_CAPABILITY) and a player ticket whose mode disagrees (REFUSE_MODE_MIXED). Rejoin and spectator tickets carry no
// mode: they join whatever the game already is. Flagless tickets take the old path unchanged.

#include "doctest.h"

#include "Common/Crc32.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "RelayCore.h"

#include <cstring>
#include <filesystem>
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
RelayProtocol::Ticket testTicket(std::uint8_t slot, std::uint8_t mode = 0, std::uint16_t generation = 0)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = 0x0077001100220033ull;
	t.slot = slot;
	t.slotMask = 0x0003;
	t.expires = 2000000000u;
	t.generation = generation;
	t.mode = mode;
	return t;
}
std::vector<std::uint8_t> startBytes(const RelayProtocol::Ticket &t)
{
	return RelayProtocol::signTicket(t, testKey());
}
std::vector<std::uint8_t> rejoinBytes(const RelayProtocol::Ticket &t)
{
	return RelayProtocol::signRejoinTicket(t, testKey());
}
std::vector<std::uint8_t> spectatorBytes(std::uint8_t index)
{
	RelayProtocol::Ticket t = testTicket(index);
	t.spectator = true;
	t.delaySeconds = 30;
	return RelayProtocol::signSpectatorTicket(t, testKey());
}
RelayAddress addr(std::uint16_t port)
{
	return RelayAddress{ 0x0A000001u, port };
}
} // namespace

TEST_CASE("canonical ticket: version 7 carries the game's mode byte")
{
	using namespace RelayProtocol;
	Ticket t = testTicket(0, TICKET_MODE_CANONICAL | TICKET_MODE_EQUAL_DELAY);
	const std::vector<std::uint8_t> bytes = startBytes(t);
	REQUIRE(bytes.size() == kTicketBytesFlags);
	CHECK(bytes[0] == 7);
	CHECK(bytes[13] == (TICKET_MODE_CANONICAL | TICKET_MODE_EQUAL_DELAY));

	Ticket back;
	std::string error;
	REQUIRE(readTicketFields(bytes.data(), bytes.size(), back, &error));
	CHECK(back.mode == (TICKET_MODE_CANONICAL | TICKET_MODE_EQUAL_DELAY));
	CHECK(back.slot == 0);
	CHECK(back.slotMask == 0x0003);
	CHECK(back.expires == 2000000000u);
	CHECK(back.generation == 0);
	CHECK_FALSE(back.spectator);

	Ticket verified;
	REQUIRE(verifyTicket(bytes.data(), bytes.size(), testRing(), 1700000000u, verified, nullptr, nullptr));
	CHECK(verified.mode == back.mode);

	// a tampered mode byte fails the MAC; mode 0 signs the plain ticket of the mask's width (the old path: version 1, 32 bytes
	// for a narrow mask)
	std::vector<std::uint8_t> tampered = bytes;
	tampered[13] = 0;
	CHECK_FALSE(verifyTicket(tampered.data(), tampered.size(), testRing(), 1700000000u, verified, nullptr, nullptr));
	const std::vector<std::uint8_t> plain = startBytes(testTicket(0));
	REQUIRE(plain.size() == kTicketBytes);
	CHECK(plain[0] == 1);

	// the same bytes the lobby signs (pinned with services/online/lobby/test/lobby.test.mjs)
	Ticket pin;
	pin.keyId = 1;
	pin.gameId = 0x0123456789abcdefull;
	pin.slot = 1;
	pin.slotMask = 0x0007;
	pin.expires = 2000000000u;
	pin.mode = TICKET_MODE_CANONICAL | TICKET_MODE_EQUAL_DELAY;
	static const std::uint8_t want[] = { 0x07, 0x01, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x01, 0x07, 0x00, 0x03,
		0x00, 0x94, 0x35, 0x77, 0x00, 0x00, 0xb5, 0x9c, 0x53, 0x14, 0x80, 0x88, 0xe6, 0xa4, 0x5c, 0x31, 0xd5, 0xd0, 0xb1,
		0x71, 0xb4, 0x54 };
	const std::vector<std::uint8_t> signedPin = signTicket(pin, testKey());
	REQUIRE(signedPin.size() == sizeof(want));
	CHECK(std::equal(signedPin.begin(), signedPin.end(), want));

	// ROUTER-1: the ticket's frame period round-trips signed (the relay's frame clock takes it)
	Ticket period = pin;
	period.frameMs = 100;
	const std::vector<std::uint8_t> periodBytes = signTicket(period, testKey());
	Ticket periodBack;
	REQUIRE(readTicketFields(periodBytes.data(), periodBytes.size(), periodBack, &error));
	CHECK(periodBack.frameMs == 100);
	CHECK(periodBack.mode == pin.mode);
	Ticket periodVerified;
	REQUIRE(verifyTicket(periodBytes.data(), periodBytes.size(), testRing(), 1700000000u, periodVerified, nullptr, nullptr));
	CHECK(periodVerified.frameMs == 100);
}

TEST_CASE("canonical game: the relay binds the mode and refuses missing capabilities and mixed modes")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.logDir = (std::filesystem::temp_directory_path()
		/ ("openbfme_canonical_ticket_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(options.logDir);
	RelayCore relay(options);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		return relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
	};

	// the first player binds the game's canonical mode with a capable HELLO
	RelayCore::Action a = send(addr(100), encodeHello(startBytes(testTicket(0, TICKET_MODE_CANONICAL)), {}, CAP_CANONICAL));
	CHECK(a.kind == RelayCore::Action::REPLY);
	REQUIRE(a.reply.size() > kHeaderBytes);
	CHECK(a.reply[3] == WELCOME);

	// a capable HELLO whose ticket lacks the mode: the ticket's mode disagrees with the game's
	CHECK(send(addr(101), encodeHello(startBytes(testTicket(1)), {}, CAP_CANONICAL)).reply == encodeRefused(REFUSE_MODE_MIXED));

	// the matching second ticket but no canonical capability: refused by name
	CHECK(send(addr(101), encodeHello(startBytes(testTicket(1, TICKET_MODE_CANONICAL)), {}, 0)).reply == encodeRefused(REFUSE_CAPABILITY));

	// mode and capability agree: welcomed
	a = send(addr(101), encodeHello(startBytes(testTicket(1, TICKET_MODE_CANONICAL)), {}, CAP_CANONICAL));
	REQUIRE(a.reply.size() > kHeaderBytes);
	CHECK(a.reply[3] == WELCOME);
}

TEST_CASE("classic game: flagless tickets keep the old path regardless of capabilities")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.logDir = (std::filesystem::temp_directory_path()
		/ ("openbfme_canonical_ticket_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(options.logDir);
	RelayCore relay(options);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		return relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
	};
	for (std::uint8_t slot = 0; slot < 2; ++slot)
	{
		RelayCore::Action a = send(addr((std::uint16_t)(100 + slot)), encodeHello(startBytes(testTicket(slot))));
		REQUIRE(a.reply.size() > kHeaderBytes);
		CHECK(a.reply[3] == WELCOME);
	}
	// a mode-flagged ticket into a classic game disagrees with the bound mode
	CHECK(send(addr(200), encodeHello(startBytes(testTicket(0, TICKET_MODE_CANONICAL)), {}, CAP_CANONICAL)).reply == encodeRefused(REFUSE_MODE_MIXED));
}

TEST_CASE("canonical game: spectator and rejoin tickets carry no mode but the capability still binds")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.logDir = (std::filesystem::temp_directory_path()
		/ ("openbfme_canonical_ticket_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(options.logDir);
	RelayCore relay(options);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		return relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
	};
	REQUIRE(send(addr(100), encodeHello(startBytes(testTicket(0, TICKET_MODE_CANONICAL)), {}, CAP_CANONICAL)).reply[3] == WELCOME);
	REQUIRE(send(addr(101), encodeHello(startBytes(testTicket(1, TICKET_MODE_CANONICAL)), {}, CAP_CANONICAL)).reply[3] == WELCOME);

	// a spectator without the capability is refused; with it, welcomed as a spectator
	CHECK(send(addr(300), encodeHello(spectatorBytes(0))).reply == encodeRefused(REFUSE_CAPABILITY));
	RelayCore::Action a = send(addr(300), encodeHello(spectatorBytes(0), {}, CAP_CANONICAL));
	REQUIRE(a.reply.size() > kHeaderBytes);
	CHECK(a.reply[3] == WELCOME);

	// a rejoin ticket (generation > 0, no mode byte) joins the canonical game only with the capability
	const std::vector<std::uint8_t> r = rejoinBytes(testTicket(1, 0, 1));
	CHECK(send(addr(400), encodeHello(r)).reply == encodeRefused(REFUSE_CAPABILITY));
	a = send(addr(400), encodeHello(r, {}, CAP_CANONICAL));
	REQUIRE(a.reply.size() > kHeaderBytes);
	CHECK(a.reply[3] == WELCOME);
}

// lane CHAT-1: a direct, non-frame command on the legacy stream (a chat line, a disconnect keepalive or screen
// message) is addressed at the envelope's target byte and forwarded like a lockstep game's — never logged into the
// canonical game's OBRC log, never put into a bundle.
TEST_CASE("canonical game: a direct command reaches only its addressed slot")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.logDir = (std::filesystem::temp_directory_path()
		/ ("openbfme_canonical_direct_" + std::to_string(NetMilliseconds()))).string();
	std::filesystem::create_directories(options.logDir);
	RelayCore relay(options);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		return relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
	};
	RelayProtocol::Ticket wide = testTicket(0, TICKET_MODE_CANONICAL); // slot mask covers slots 0..2
	wide.slotMask = 0x0007;
	REQUIRE(send(addr(100), encodeHello(startBytes(wide), {}, CAP_CANONICAL)).reply[3] == WELCOME);
	RelayProtocol::Ticket t1 = testTicket(1, TICKET_MODE_CANONICAL);
	t1.slotMask = 0x0007;
	REQUIRE(send(addr(101), encodeHello(startBytes(t1), {}, CAP_CANONICAL)).reply[3] == WELCOME);
	RelayProtocol::Ticket t2 = testTicket(2, TICKET_MODE_CANONICAL);
	t2.slotMask = 0x0007;
	REQUIRE(send(addr(102), encodeHello(startBytes(t2), {}, CAP_CANONICAL)).reply[3] == WELCOME);

	auto transportPacket = [](std::uint8_t sender, const std::vector<std::uint8_t> &command) {
		std::vector<std::uint8_t> p(14, 0);
		p[4] = 0x42;
		p[5] = 0x46;
		p[6] = 2;      // Transport packet version
		p[7] = sender; // the packet speaks for its bound slot only
		// ack field stays 0; one record, one fragment
		p[12] = 1;
		p.insert(p.end(), { 1, 0, 0, 0, 0, 0, 1, 0 }); // seq=1, index=0, frags=1
		const std::uint32_t len = (std::uint32_t)command.size();
		p.push_back((std::uint8_t)len);
		p.push_back((std::uint8_t)(len >> 8));
		p.push_back((std::uint8_t)(len >> 16));
		p.push_back((std::uint8_t)(len >> 24));
		p.insert(p.end(), command.begin(), command.end());
		const std::uint32_t crc = crc32Bytes(p.data() + 4, p.size() - 4);
		for (int i = 0; i < 4; ++i) p[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
		return p;
	};
	// a DISCONNECTKEEPALIVE-shaped command (u8 type, u8 slot, u32 frame): the relay forwards whole packets
	const std::vector<std::uint8_t> keepalive = { 25, 0, 1, 0, 0, 0 };
	const unsigned long long canonicalBefore = relay.stats().canonicalRecords;

	// addressed at slot 1 only
	RelayCore::Action a = send(addr(100), encodeData(1, transportPacket(0, keepalive)));
	CHECK(a.kind == RelayCore::Action::FORWARD);
	CHECK(a.to.ip == addr(101).ip);
	CHECK(a.to.port == addr(101).port);
	// slot 2 never sees it (one FORWARD action, to slot 1's bound address only)
	// and the canonical log gained no record for it
	CHECK(relay.stats().canonicalRecords == canonicalBefore);

	// addressed at slot 2: forwarded there
	a = send(addr(100), encodeData(2, transportPacket(0, keepalive)));
	CHECK(a.kind == RelayCore::Action::FORWARD);
	CHECK(a.to.port == addr(102).port);
	CHECK(relay.stats().canonicalRecords == canonicalBefore);

	// the same command must not go back to its own slot
	a = send(addr(100), encodeData(0, transportPacket(0, keepalive)));
	CHECK(a.kind == RelayCore::Action::NONE);
	CHECK(relay.stats().badTargets == 1);

	// a packet claiming another slot's sender byte is dropped, not forwarded
	a = send(addr(100), encodeData(1, transportPacket(1, keepalive)));
	CHECK(a.kind == RelayCore::Action::NONE);
	CHECK(relay.stats().slotMismatches == 1);

	// a corrupt packet (trailing garbage) is dropped, never forwarded nor logged
	std::vector<std::uint8_t> corrupt = transportPacket(0, keepalive);
	corrupt.push_back(0xEE);
	a = send(addr(100), encodeData(1, corrupt));
	CHECK(a.kind == RelayCore::Action::NONE);
	CHECK(relay.stats().invalidPackets == 1);
	CHECK(relay.stats().canonicalRecords == canonicalBefore);
}
