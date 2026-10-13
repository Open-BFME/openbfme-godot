// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-2 (docs/ONLINE.md phase O1): SHA-256 / HMAC-SHA256 against the published vectors, the relay ticket (pinned bytes computed independently with
// Python's hmac module; services/online/lobby signs the same bytes, its own test pins the same text), the relay envelope, the online START, the relay's
// core (binding, forwarding, the slot check, the budget, the command log), real Transports talking through the relay with loss, and the relay log
// assembled into frames as the Network relays them.

#include "doctest.h"

#include "Common/Crc32.h"
#include "Common/Sha256.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "RelayCore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#ifdef OPENBFME_RELAY_EXE
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
RelayProtocol::Ticket testTicket(std::uint8_t slot, std::uint8_t mask = 0x07, std::uint64_t game = 0x0123456789abcdefull)
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
std::string hex(const std::array<std::uint8_t, 32> &h)
{
	return HexBytes(h.data(), h.size());
}
std::filesystem::path tempDir(const std::string &name)
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / ("openbfme_online2_" + name + "_" + std::to_string(NetMilliseconds()));
	std::filesystem::create_directories(p);
	return p;
}
RelayAddress addr(std::uint16_t port)
{
	return RelayAddress{ 0x0A000001u, port };
}
// a well-formed transport packet (Transport.h version 2) from `sender` with one record
std::vector<std::uint8_t> transportPacket(std::uint8_t sender, std::uint32_t seq, const std::vector<std::uint8_t> &record, std::uint16_t fragIndex = 0,
	std::uint16_t fragCount = 1)
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
	put16(fragIndex);
	put16(fragCount);
	put32((std::uint32_t)record.size());
	d.insert(d.end(), record.begin(), record.end());
	const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		d[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return d;
}
std::vector<std::uint8_t> frameInfo(std::uint8_t slot, std::uint32_t frame, std::uint16_t count)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = slot;
	c.executionFrame = frame;
	c.commandCount = count;
	return NetPacket::encodeCommand(c, nullptr);
}
std::vector<std::uint8_t> gameCommand(std::uint8_t slot, std::uint32_t frame, GameMessageType type, int arg)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_GAMECOMMAND;
	c.slot = slot;
	c.executionFrame = frame;
	auto m = std::make_shared<GameMessage>(type, 99); // the player a sender writes is never trusted (Network::file sets the sender's)
	m->appendIntegerArgument(arg);
	if (type == MSG_LOGIC_CRC)
	{
		m->appendBooleanArgument(false);
	}
	c.message = m;
	return NetPacket::encodeCommand(c, nullptr);
}
// a WELCOME for the slot of the test game: its session token (empty when `reply` is not one)
std::vector<std::uint8_t> welcomeToken(const std::vector<std::uint8_t> &reply, std::uint8_t slot, std::uint64_t game = 0x0123456789abcdefull)
{
	using namespace RelayProtocol;
	std::array<std::uint8_t, kSessionTokenBytes> token{};
	if (reply.size() != kHeaderBytes + 9 + kSessionTokenBytes)
	{
		return {};
	}
	std::copy(reply.end() - (long)kSessionTokenBytes, reply.end(), token.begin());
	if (reply != encodeWelcome(game, slot, token))
	{
		return {};
	}
	return std::vector<std::uint8_t>(token.begin(), token.end());
}
std::vector<std::uint8_t> readFile(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("online2: SHA-256 (FIPS 180-4 examples) and HMAC-SHA256 (RFC 4231 cases 1, 2 and 6)")
{
	CHECK(hex(Sha256::hash("", 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(hex(Sha256::hash("abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	CHECK(hex(Sha256::hash(two.data(), two.size())) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	// a million 'a', fed in uneven pieces
	Sha256 s;
	const std::string chunk(997, 'a');
	size_t left = 1000000;
	while (left)
	{
		const size_t n = std::min(left, chunk.size());
		s.update(chunk.data(), n);
		left -= n;
	}
	CHECK(hex(s.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	const std::vector<std::uint8_t> k1(20, 0x0b);
	CHECK(hex(HmacSha256(k1.data(), k1.size(), (const std::uint8_t *)"Hi There", 8)) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	const std::string jefe = "Jefe", what = "what do ya want for nothing?";
	CHECK(hex(HmacSha256((const std::uint8_t *)jefe.data(), jefe.size(), (const std::uint8_t *)what.data(), what.size()))
		== "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	const std::vector<std::uint8_t> k6(131, 0xaa);
	const std::string m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
	CHECK(hex(HmacSha256(k6.data(), k6.size(), (const std::uint8_t *)m6.data(), m6.size())) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE("online2: the ticket is the pinned 32 bytes, verifies, and every tampered, expired, unknown-key or out-of-mask ticket is refused")
{
	using namespace RelayProtocol;
	const std::vector<std::uint8_t> t = ticketBytes(testTicket(1));
	// pinned: Python hmac / hashlib over the same 16 bytes (services/online/lobby/test pins the same text for the lobby's signer)
	CHECK(base64UrlEncode(t.data(), t.size()) == "AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI");
	Ticket out;
	std::string error;
	RefuseReason reason{};
	REQUIRE(verifyTicket(t.data(), t.size(), testRing(), 1700000000u, out, &error, &reason));
	CHECK(out.gameId == 0x0123456789abcdefull);
	CHECK(out.slot == 1);
	CHECK(out.slotMask == 0x07);
	for (size_t i = 0; i < t.size(); ++i)
	{
		std::vector<std::uint8_t> bad = t;
		bad[i] ^= 0x01;
		CHECK_FALSE(verifyTicket(bad.data(), bad.size(), testRing(), 1700000000u, out, &error, &reason));
	}
	CHECK_FALSE(verifyTicket(t.data(), t.size(), testRing(), 2000000001u, out, &error, &reason));
	CHECK(reason == REFUSE_EXPIRED);
	CHECK_FALSE(verifyTicket(t.data(), t.size(), KeyRing{ { 2, testKey() } }, 1700000000u, out, &error, &reason));
	CHECK(reason == REFUSE_BAD_TICKET);
	CHECK_FALSE(verifyTicket(t.data(), t.size() - 1, testRing(), 1700000000u, out, &error, &reason));
	const std::vector<std::uint8_t> outOfMask = ticketBytes(testTicket(3, 0x07));
	CHECK_FALSE(verifyTicket(outOfMask.data(), outOfMask.size(), testRing(), 1700000000u, out, &error, &reason));
	CHECK(error.find("not one of the game's network slots") != std::string::npos);
}

TEST_CASE("online2: base64url is strict, key rings parse and refuse malformed lines")
{
	using namespace RelayProtocol;
	std::vector<std::uint8_t> b;
	CHECK(base64UrlDecode("AQID", b));
	CHECK(b == std::vector<std::uint8_t>{ 1, 2, 3 });
	CHECK_FALSE(base64UrlDecode("AQI+", b)); // the standard alphabet's '+'
	CHECK_FALSE(base64UrlDecode("AQID=", b));
	CHECK_FALSE(base64UrlDecode("AQJ", b)); // non-zero spare bits: not the canonical text of any bytes
	CHECK(base64UrlDecode("AQI", b));
	CHECK(base64Decode("AQID", b));
	CHECK(base64Decode("AQ==", b));
	CHECK(b == std::vector<std::uint8_t>{ 1 });
	CHECK(base64Encode(b.data(), b.size()) == "AQ==");
	KeyRing ring;
	std::string error;
	CHECK(parseKeyRing("# lobby + relay keys\n1:000102030405060708090a0b0c0d0e0f\n\n2:FFEEDDCCBBAA99887766554433221100ff\n", ring, &error));
	CHECK(ring.size() == 2);
	CHECK_FALSE(parseKeyRing("1:0001\n", ring, &error)); // too short
	CHECK_FALSE(parseKeyRing("1:000102030405060708090a0b0c0d0e0f\n1:000102030405060708090a0b0c0d0e0f\n", ring, &error));
	CHECK_FALSE(parseKeyRing("x:000102030405060708090a0b0c0d0e0f\n", ring, &error));
	CHECK_FALSE(parseKeyRing("# nothing\n", ring, &error));
}

TEST_CASE("online2: the online START becomes a LobbyStart; another profile, a mismatched ticket and malformed fields are errors")
{
	const ProfileIdentity own = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	const std::string ticket = "AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI"; // slot 1, mask 0x07
	auto start = [&](const std::string &profile, int slot, const std::string &slotsJson, const std::string &extra = "") {
		return "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0123456789abcdef\",\"slot\":" + std::to_string(slot) + ",\"relay\":\"192.0.2.10:7350\",\"ticket\":\""
			+ ticket + "\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + profile + "\"" + extra
			+ ",\"setup\":{\"map\":\"maps/map mp evendim/map mp evendim.map\",\"seed\":4242,\"startingCash\":2000,\"superweaponRestriction\":0,\"rules\":null,"
			  "\"legendaryHeroes\":1,\"slots\":"
			+ slotsJson + "}}";
	};
	const std::string slots3 = "[{\"state\":\"player\",\"name\":\"Ann\",\"faction\":0,\"color\":0,\"startPos\":0,\"team\":0},"
							   "{\"state\":\"player\",\"name\":\"B\\u00e9a\",\"faction\":1,\"color\":1,\"startPos\":1,\"team\":1},"
							   "{\"state\":\"player\",\"name\":\"Cy\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1},"
							   "{\"state\":\"hard\",\"name\":\"AI\",\"faction\":2,\"color\":3,\"startPos\":3,\"team\":1}]";
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 8;
	limits.colorCount = 8;
	LobbyStart s;
	std::string error;
	REQUIRE_MESSAGE(OnlineStart::parse(start(own.digest, 1, slots3), own, limits, s, &error), error);
	CHECK(s.online);
	CHECK(s.onlineGameId == 0x0123456789abcdefull);
	CHECK(s.relay.text() == "192.0.2.10:7350");
	CHECK(s.localSlot == 1);
	CHECK(s.runAhead == 3);
	CHECK(s.crcInterval == 100);
	CHECK(s.ticket.size() == 32);
	CHECK(s.game.game.seed == 4242);
	CHECK(s.game.game.startingCash == 2000);
	CHECK(s.game.game.slots[1].name == u"Béa");
	CHECK(s.game.game.slots[3].state == SLOT_HARD_AI);
	CHECK(s.game.game.slots[4].state == SLOT_CLOSED); // missing slots are closed
	CHECK(s.networkSlot[0]);
	CHECK(s.networkSlot[2]);
	CHECK_FALSE(s.networkSlot[3]);
	CHECK(s.profile == own);
	// the other profile, the slot the ticket is not for, a mask that differs from the human slots, bad fields
	CHECK_FALSE(OnlineStart::parse(start("00000000000000000000000000000000", 1, slots3), own, limits, s, &error));
	CHECK(error.find("profile identity") != std::string::npos);
	CHECK_FALSE(OnlineStart::parse(start(own.digest, 0, slots3), own, limits, s, &error));
	CHECK(error.find("ticket is for slot 1") != std::string::npos);
	const std::string slots2 = "[{\"state\":\"player\",\"name\":\"Ann\",\"faction\":0,\"color\":0,\"startPos\":0,\"team\":0},"
							   "{\"state\":\"player\",\"name\":\"Bea\",\"faction\":1,\"color\":1,\"startPos\":1,\"team\":1}]";
	CHECK_FALSE(OnlineStart::parse(start(own.digest, 1, slots2), own, limits, s, &error));
	CHECK(error.find("slot mask") != std::string::npos);
	CHECK_FALSE(OnlineStart::parse(start(own.digest, 1, "[{\"state\":\"pirate\"}]"), own, limits, s, &error));
	CHECK_FALSE(OnlineStart::parse(start(own.digest, 1, "[{\"state\":\"player\",\"name\":\"A\",\"faction\":9,\"color\":0,\"startPos\":0,\"team\":0}]"), own, limits, s,
		&error));
	CHECK(error.find("\"faction\"") != std::string::npos);
	CHECK_FALSE(OnlineStart::parse("{\"v\":2}", own, limits, s, &error));
	CHECK_FALSE(OnlineStart::parse("not json", own, limits, s, &error));
	// ONLINE-2 r2 (Sol: player takeover): the published replay START carries no credential; each kind is refused as the other
	const std::string setupJson = ",\"setup\":{\"map\":\"maps/map mp evendim/map mp evendim.map\",\"seed\":4242,\"startingCash\":2000,\"superweaponRestriction\":0,"
								  "\"rules\":null,\"legendaryHeroes\":1,\"slots\":" + slots3 + "}}";
	const std::string replayStart = "{\"v\":1,\"kind\":\"openbfme-replay-start\",\"game\":\"0123456789abcdef\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\""
		+ own.digest + "\"" + setupJson;
	REQUIRE_MESSAGE(OnlineStart::parse(replayStart, own, limits, s, &error, OnlineStart::Kind::ReplayStart), error);
	CHECK(s.localSlot == 0);
	CHECK(s.ticket.empty());
	CHECK(s.game.game.seed == 4242);
	CHECK_FALSE(OnlineStart::parse(replayStart, own, limits, s, &error)); // never playable: no ticket
	CHECK_FALSE(OnlineStart::parse(start(own.digest, 1, slots3), own, limits, s, &error, OnlineStart::Kind::ReplayStart));
	const std::string withTicket = "{\"v\":1,\"kind\":\"openbfme-replay-start\",\"game\":\"0123456789abcdef\",\"ticket\":\"" + ticket
		+ "\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + own.digest + "\"" + setupJson;
	CHECK_FALSE(OnlineStart::parse(withTicket, own, limits, s, &error, OnlineStart::Kind::ReplayStart));
	CHECK(error.find("no ticket") != std::string::npos);
	std::u16string u;
	CHECK(OnlineStart::utf8ToUtf16("\xF0\x9F\x99\x82", u));
	CHECK(u.size() == 2);
	CHECK_FALSE(OnlineStart::utf8ToUtf16("\xC0\x80", u)); // overlong
	CHECK_FALSE(OnlineStart::utf8ToUtf16("\xED\xA0\x80", u)); // a surrogate
}

TEST_CASE("online2: the relay binds by ticket, forwards in place, enforces the slot check, the targets, the budget, and answers NOT_BOUND / REFUSED")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.bytesPerSecond = 1000;
	o.burstBytes = 2000;
	RelayCore relay(o);
	const std::uint32_t unix = 1700000000u;
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now = 1000) {
		RelayCore::Action a = relay.handle(from, d.data(), d.size(), now, unix);
		if (a.kind == RelayCore::Action::FORWARD)
		{
			a.reply = d; // what goes out: the rewritten input
		}
		return a;
	};
	// DATA before HELLO: NOT_BOUND
	RelayCore::Action a = send(addr(1), encodeData(1, transportPacket(0, 0, { 1 })));
	CHECK(a.kind == RelayCore::Action::REPLY);
	CHECK(a.reply == encodeEmpty(NOT_BOUND));
	// a bad ticket: REFUSED
	std::vector<std::uint8_t> bad = ticketBytes(testTicket(0));
	bad[20] ^= 1;
	a = send(addr(1), encodeHello(bad));
	CHECK(a.reply == encodeRefused(REFUSE_BAD_TICKET));
	// slot 0 and slot 1 bind
	a = send(addr(1), encodeHello(ticketBytes(testTicket(0))));
	CHECK(welcomeToken(a.reply, 0).size() == kSessionTokenBytes);
	a = send(addr(2), encodeHello(ticketBytes(testTicket(1))));
	const std::vector<std::uint8_t> token1 = welcomeToken(a.reply, 1);
	CHECK(token1.size() == kSessionTokenBytes);
	CHECK(relay.openGames() == 1);
	// slot 0 -> slot 1: forwarded as DATA_FROM 0, the same bytes after the envelope
	const std::vector<std::uint8_t> packet = transportPacket(0, 0, { 1, 2, 3 });
	a = send(addr(1), encodeData(1, packet));
	REQUIRE(a.kind == RelayCore::Action::FORWARD);
	CHECK(a.to == addr(2));
	CHECK(a.reply[3] == DATA_FROM);
	CHECK(a.reply[4] == 0);
	CHECK(std::vector<std::uint8_t>(a.reply.begin() + 5, a.reply.end()) == packet);
	// slot 0 speaking as slot 1: dropped (the slot check by session)
	a = send(addr(1), encodeData(1, transportPacket(1, 0, { 1 })));
	CHECK(a.kind == RelayCore::Action::NONE);
	CHECK(relay.stats().slotMismatches == 1);
	// a target outside the game, a slot that has not bound (2 is in the mask but never said HELLO); lane ONLINE-3: itself is the log-only stream (logged and
	// acknowledged, forwarded nowhere: tests/test_online3_rejoin.cpp)
	CHECK(send(addr(1), encodeData(5, packet)).kind == RelayCore::Action::NONE);
	CHECK(send(addr(1), encodeData(0, packet)).kind == RelayCore::Action::REPLY);
	CHECK(send(addr(1), encodeData(2, packet)).kind == RelayCore::Action::NONE);
	CHECK(relay.stats().badTargets == 1);
	CHECK(relay.stats().unboundTargets == 1);
	// a NAT rebinding: slot 1 says HELLO from a new address with its session token; the old one is NOT_BOUND now and data goes to the new one
	a = send(addr(3), encodeHello(ticketBytes(testTicket(1)), token1));
	CHECK(welcomeToken(a.reply, 1) == token1);
	CHECK(send(addr(2), encodeData(0, transportPacket(1, 0, { 1 }))).reply == encodeEmpty(NOT_BOUND));
	a = send(addr(1), encodeData(1, packet));
	CHECK(a.to == addr(3));
	// the budget: 2,000 bytes of burst at 1,000 B/s; a flood at one instant is cut, a second later there is room again
	int forwarded = 0;
	for (int i = 0; i < 40; ++i)
	{
		forwarded += send(addr(1), encodeData(1, transportPacket(0, 1, std::vector<std::uint8_t>(100, 9))), 1000).kind == RelayCore::Action::FORWARD ? 1 : 0;
	}
	CHECK(forwarded < 20);
	CHECK(relay.stats().rateLimited > 0);
	CHECK(send(addr(1), encodeData(1, packet), 3000).kind == RelayCore::Action::FORWARD);
	// another envelope version is refused
	std::vector<std::uint8_t> v1 = encodeHello(ticketBytes(testTicket(0)));
	v1[2] = 1; // round 1's envelope (no session token)
	CHECK(send(addr(9), v1).reply == encodeRefused(REFUSE_VERSION));
	// junk is counted, never answered
	CHECK(send(addr(9), { 1, 2, 3, 4, 5 }).kind == RelayCore::Action::NONE);
	// silence closes the game; its HELLOs are then refused
	relay.tick(1000 + o.idleMs + 5000);
	CHECK(relay.openGames() == 0);
	CHECK(send(addr(1), encodeHello(ticketBytes(testTicket(0))), 70000).reply == encodeRefused(REFUSE_GAME_CLOSED));
}

TEST_CASE("online2: the relay logs every sender's frame commands once, in order, across its streams to every target; BYE closes the log")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("log");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	const std::uint32_t unix = 1700000000u;
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, unix); };
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		send(addr((std::uint16_t)(10 + s)), encodeHello(ticketBytes(testTicket(s))));
	}
	// slot 0 sends the same three commands to slot 1 and to slot 2; to slot 2 out of order, with a duplicate; one command is fragmented
	const std::vector<std::uint8_t> c0 = frameInfo(0, 5, 1), c1 = gameCommand(0, 5, MSG_LOGIC_CRC, 77), c2 = frameInfo(0, 6, 0);
	send(addr(10), encodeData(1, transportPacket(0, 0, c0)));
	send(addr(10), encodeData(1, transportPacket(0, 1, c1)));
	send(addr(10), encodeData(2, transportPacket(0, 1, c1)));
	send(addr(10), encodeData(2, transportPacket(0, 0, c0)));
	send(addr(10), encodeData(2, transportPacket(0, 0, c0))); // a resend
	const size_t half = c2.size() / 2;
	send(addr(10), encodeData(2, transportPacket(0, 2, std::vector<std::uint8_t>(c2.begin(), c2.begin() + (long)half), 0, 2)));
	send(addr(10), encodeData(2, transportPacket(0, 3, std::vector<std::uint8_t>(c2.begin() + (long)half, c2.end()), 1, 2)));
	send(addr(10), encodeData(1, transportPacket(0, 2, c2))); // slot 1's stream catches up: already logged
	// a frame command naming another slot is not logged
	send(addr(11), encodeData(0, transportPacket(1, 0, frameInfo(2, 5, 0))));
	CHECK(relay.stats().foreignFrameCommands == 1);
	CHECK(relay.stats().commandsLogged == 3);
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		send(addr((std::uint16_t)(10 + s)), encodeEmpty(BYE));
	}
	CHECK(relay.openGames() == 0);
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	CHECK(log.gameId == 0x0123456789abcdefull);
	CHECK(log.slotMask == 0x07);
	CHECK(log.complete);
	CHECK(log.endReason == 1);
	REQUIRE(log.commands.size() == 3);
	CHECK(log.commands[0].msg.type == NETCOMMANDTYPE_FRAMEINFO);
	CHECK(log.commands[1].msg.type == NETCOMMANDTYPE_GAMECOMMAND);
	CHECK(log.commands[2].msg.executionFrame == 6);
	CHECK(log.events.size() == 6); // three bound, three bye
	CHECK(log.datagramsIn[0] == 8);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online2 r2 (Sol: player takeover): a leaked ticket from another address cannot take a bound slot; only the slot's session token rebinds it")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		RelayCore::Action a = relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
		if (a.kind == RelayCore::Action::FORWARD)
		{
			a.reply = d;
		}
		return a;
	};
	send(addr(1), encodeHello(ticketBytes(testTicket(0))));
	const std::vector<std::uint8_t> token1 = welcomeToken(send(addr(2), encodeHello(ticketBytes(testTicket(1)))).reply, 1);
	REQUIRE(token1.size() == kSessionTokenBytes);
	// Sol's attack: the attacker has slot 1's ticket (round 1 published it in /v1/replays/<id>/start) and says HELLO from its own address
	CHECK(send(addr(66), encodeHello(ticketBytes(testTicket(1)))).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	// ... with a guessed token too
	std::vector<std::uint8_t> guess = token1;
	guess[0] ^= 1;
	CHECK(send(addr(66), encodeHello(ticketBytes(testTicket(1)), guess)).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	CHECK(relay.stats().takeoversRefused == 2);
	// the attacker gets nothing of slot 0's traffic, and cannot speak as slot 1
	const RelayCore::Action fwd = send(addr(1), encodeData(1, transportPacket(0, 0, { 7 })));
	CHECK(fwd.kind == RelayCore::Action::FORWARD);
	CHECK(fwd.to == addr(2));
	CHECK(send(addr(66), encodeData(0, transportPacket(1, 0, { 7 }))).reply == encodeEmpty(NOT_BOUND));
	// the same address saying HELLO again (a lost WELCOME) gets the same token back; a malformed token length is a bad ticket
	CHECK(welcomeToken(send(addr(2), encodeHello(ticketBytes(testTicket(1)))).reply, 1) == token1);
	std::vector<std::uint8_t> shortToken(token1.begin(), token1.begin() + 8);
	CHECK(send(addr(67), encodeHello(ticketBytes(testTicket(1)), shortToken)).reply == encodeRefused(REFUSE_BAD_TICKET));
	// the owner behind a new NAT mapping proves itself with the token: rebound
	CHECK(welcomeToken(send(addr(3), encodeHello(ticketBytes(testTicket(1)), token1)).reply, 1) == token1);
	CHECK(send(addr(1), encodeData(1, transportPacket(0, 1, { 8 }))).to == addr(3));
	// two slots' tokens differ (random per slot)
	CHECK(welcomeToken(send(addr(1), encodeHello(ticketBytes(testTicket(0)))).reply, 0) != token1);
}

TEST_CASE("online2 r2 (Sol: relay DoS): rebinding does not refill the burst budget; the bucket is integer milli-bytes")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.bytesPerSecond = 1000; // 1 byte per ms
	const std::vector<std::uint8_t> datagram = encodeData(1, transportPacket(0, 0, std::vector<std::uint8_t>(31, 1))); // 5 + 14 + 12 + 31 = 62 bytes
	REQUIRE(datagram.size() == 62);
	o.burstBytes = 124; // two datagrams
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	const std::vector<std::uint8_t> token0 = welcomeToken(send(addr(1), encodeHello(ticketBytes(testTicket(0))), 1000).reply, 0);
	send(addr(2), encodeHello(ticketBytes(testTicket(1))), 1000);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::NONE); // the burst is spent
	// Sol's attack: rebind at the same instant (round 1 refilled the burst here) and flood again
	REQUIRE(welcomeToken(send(addr(9), encodeHello(ticketBytes(testTicket(0)), token0), 1000).reply, 0) == token0);
	CHECK(send(addr(9), datagram, 1000).kind == RelayCore::Action::NONE);
	// exactly 62 ms later 62 bytes have come back (integer arithmetic: not one ms early)
	CHECK(send(addr(9), datagram, 1061).kind == RelayCore::Action::NONE);
	CHECK(send(addr(9), datagram, 1062).kind == RelayCore::Action::FORWARD);
	// a long silence refills to the burst, never beyond (no overflow)
	CHECK(send(addr(9), datagram, 1000 + 4000000000000ull).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(9), datagram, 1000 + 4000000000000ull).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(9), datagram, 1000 + 4000000000000ull).kind == RelayCore::Action::NONE);
}

TEST_CASE("online2 r2 (Sol: log integrity): packets the receiving Transport refuses are neither logged nor forwarded")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r2valid");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	send(addr(1), encodeHello(ticketBytes(testTicket(0))));
	send(addr(2), encodeHello(ticketBytes(testTicket(1))));
	auto recrc = [](std::vector<std::uint8_t> &pk) {
		const std::uint32_t crc = crc32Bytes(pk.data() + 4, pk.size() - 4);
		for (int i = 0; i < 4; ++i)
		{
			pk[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
		}
	};
	// Sol's attack: a valid frame command followed by one trailing byte (the CRC covers it): Transport::handlePacket refuses the packet whole
	std::vector<std::uint8_t> trailing = transportPacket(0, 0, frameInfo(0, 5, 0));
	trailing.push_back(0);
	recrc(trailing);
	CHECK(send(addr(1), encodeData(1, trailing)).kind == RelayCore::Action::NONE);
	// an ack beyond what slot 1 ever sent slot 0 (Transport: "the ack not beyond the sequence numbers issued")
	std::vector<std::uint8_t> badAck = transportPacket(0, 0, frameInfo(0, 5, 0));
	badAck[8] = 5;
	recrc(badAck);
	CHECK(send(addr(1), encodeData(1, badAck)).kind == RelayCore::Action::NONE);
	// a record beyond the receive window
	CHECK(send(addr(1), encodeData(1, transportPacket(0, 20000, frameInfo(0, 5, 0)))).kind == RelayCore::Action::NONE);
	// a packet beyond the clients' receive ceiling (1,200 bytes)
	CHECK(send(addr(1), encodeData(1, transportPacket(0, 0, std::vector<std::uint8_t>(1200, 3)))).kind == RelayCore::Action::NONE);
	CHECK(relay.stats().invalidPackets == 4);
	CHECK(relay.stats().commandsLogged == 0);
	// the valid packet is logged and forwarded; once slot 1 sent slot 0 three records, an ack of 3 is valid
	CHECK(send(addr(1), encodeData(1, transportPacket(0, 0, frameInfo(0, 5, 0)))).kind == RelayCore::Action::FORWARD);
	for (std::uint32_t q = 0; q < 3; ++q)
	{
		CHECK(send(addr(2), encodeData(0, transportPacket(1, q, frameInfo(1, q, 0)))).kind == RelayCore::Action::FORWARD);
	}
	std::vector<std::uint8_t> goodAck = transportPacket(0, 1, frameInfo(0, 6, 0));
	goodAck[8] = 3;
	recrc(goodAck);
	CHECK(send(addr(1), encodeData(1, goodAck)).kind == RelayCore::Action::FORWARD);
	CHECK(relay.stats().commandsLogged == 5);
	relay.shutdown(2000);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online2 r2 (Sol: relay memory): a command being joined from fragments counts against the stream, the sender, the game and the relay")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r2frag");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	o.maxStreamBuffer = 64; // Sol's probe: a stream limit of 64 bytes, 1,024-byte fragments
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	send(addr(1), encodeHello(ticketBytes(testTicket(0))));
	send(addr(2), encodeHello(ticketBytes(testTicket(1))));
	// fragments of one huge command, in order: round 1 joined them without any limit (gaps 0)
	for (std::uint32_t i = 0; i < 40; ++i)
	{
		send(addr(1), encodeData(1, transportPacket(0, i, std::vector<std::uint8_t>(1024, 4), (std::uint16_t)i, 60000)));
	}
	CHECK(relay.stats().logGaps == 1);
	CHECK(relay.stats().peakBuffered <= 64 + 1024); // at most one record beyond the cap before the stream stops
	relay.shutdown(2000);
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	REQUIRE(log.gaps.size() == 1);
	CHECK(log.gaps[0].find("reorder buffer") != std::string::npos);
	// the sender cap: two streams of one sender, each under its own limit, together over the sender's
	RelayCore::Options o2 = o;
	o2.maxStreamBuffer = 4096;
	o2.maxSlotBuffer = 3000;
	RelayCore relay2(o2);
	auto send2 = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay2.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	send2(addr(1), encodeHello(ticketBytes(testTicket(0))));
	send2(addr(2), encodeHello(ticketBytes(testTicket(1))));
	send2(addr(3), encodeHello(ticketBytes(testTicket(2))));
	for (std::uint32_t i = 0; i < 2; ++i)
	{
		send2(addr(1), encodeData(1, transportPacket(0, i, std::vector<std::uint8_t>(1000, 4), (std::uint16_t)i, 100)));
		send2(addr(1), encodeData(2, transportPacket(0, i, std::vector<std::uint8_t>(1000, 4), (std::uint16_t)i, 100)));
	}
	CHECK(relay2.stats().logGaps == 1);
	// lane ONLINE-3 r3 (Sol r3 of ONLINE-3: every growth is admitted before it is made, so the old one-record allowance is gone): the peak, exactly, is the
	// second fragment of target 2 arriving as an early record (1,000 bytes + its 64-byte record overhead) beside target 2's joined first fragment
	// (1,000), target 1's stream having stopped at its second fragment (3,064 > the 3,000-byte sender cap), plus the game's frontier (16 entries)
	CHECK(relay2.stats().peakBuffered == 1000 + 1000 + 64 + 16 * sizeof(std::pair<std::uint64_t, std::uint32_t>));
	relay2.shutdown(2000);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online2 r3 (Sol r2): a finished or discarded partial command releases its allocation; what the streams keep is what the caps count")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r3mem");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	o.maxTotalBuffer = 2000 + 400; // Sol's probe: a 2,000-byte relay cap (lane ONLINE-3: + what the command counts and the log's frontier hold)
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	for (std::uint8_t s = 0; s < 3; ++s)
	{
		send(addr((std::uint16_t)(100 + s)), encodeHello(ticketBytes(testTicket(s))));
	}
	// Sol's sequence on both streams of slot 0: two 900-byte fragments of a 10-fragment command, then a whole command (which drops the partial one);
	// round 2 kept 2 x 1,800 bytes allocated with nothing accounted and no GAP
	for (std::uint8_t target = 1; target <= 2; ++target)
	{
		send(addr(100), encodeData(target, transportPacket(0, 0, std::vector<std::uint8_t>(900, 5), 0, 10)));
		CHECK(relay.retainedBytes() == relay.heldBytes());
		send(addr(100), encodeData(target, transportPacket(0, 1, std::vector<std::uint8_t>(900, 5), 1, 10)));
		CHECK(relay.retainedBytes() == relay.heldBytes());
		send(addr(100), encodeData(target, transportPacket(0, 2, frameInfo(0, 3, 0))));
		CHECK(relay.retainedBytes() == relay.heldBytes());
		CHECK(relay.heldBytes() <= o.maxTotalBuffer);
	}
	// both partial commands were dropped: what stays is (lane ONLINE-3, Sol r1 of ONLINE-3) the command counts of the FRAMEINFOs and the log's frontier,
	// exactly what a control relay that received only the FRAMEINFOs holds
	{
		RelayCore control(o);
		for (std::uint8_t s = 0; s < 3; ++s)
		{
			std::vector<std::uint8_t> h = encodeHello(ticketBytes(testTicket(s)));
			control.handle(addr((std::uint16_t)(100 + s)), h.data(), h.size(), 1000, 1700000000u);
		}
		for (std::uint8_t target = 1; target <= 2; ++target)
		{
			std::vector<std::uint8_t> d = encodeData(target, transportPacket(0, 0, frameInfo(0, 3, 0)));
			control.handle(addr(100), d.data(), d.size(), 1000, 1700000000u);
		}
		CHECK(relay.heldBytes() == control.heldBytes());
		control.shutdown(2000);
	}
	CHECK(relay.retainedBytes() == relay.heldBytes());
	// the cap counts what is allocated: two streams each joining 1,800 bytes go over the cap together, and one of them stops (a GAP)
	for (std::uint8_t target = 1; target <= 2; ++target)
	{
		send(addr(100), encodeData(target, transportPacket(0, 3, std::vector<std::uint8_t>(900, 5), 0, 10)));
		send(addr(100), encodeData(target, transportPacket(0, 4, std::vector<std::uint8_t>(900, 5), 1, 10)));
	}
	CHECK(relay.stats().logGaps == 1);
	CHECK(relay.heldBytes() <= o.maxTotalBuffer);
	CHECK(relay.retainedBytes() == relay.heldBytes());
	// many tiny early records count their overhead too: 300 one-byte records ahead of a missing one exceed a 16 KiB stream cap
	RelayCore::Options o2 = o;
	o2.maxTotalBuffer = 256u << 20;
	o2.maxStreamBuffer = 16384;
	RelayCore relay2(o2);
	auto send2 = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay2.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	send2(addr(100), encodeHello(ticketBytes(testTicket(0))));
	send2(addr(101), encodeHello(ticketBytes(testTicket(1))));
	for (std::uint32_t q = 1; q <= 300; ++q)
	{
		send2(addr(100), encodeData(1, transportPacket(0, q, { 1 })));
	}
	CHECK(relay2.stats().logGaps == 1);
	CHECK(relay2.retainedBytes() == relay2.heldBytes());
	relay.shutdown(2000);
	relay2.shutdown(2000);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online2 r3 (Sol r2): a relay rate of 0 never refills a spent bucket")
{
	using namespace RelayProtocol;
	RelayCore::Options o;
	o.keys = testRing();
	o.bytesPerSecond = 0;
	o.burstBytes = 124;
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d, std::uint64_t now) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	const std::vector<std::uint8_t> datagram = encodeData(1, transportPacket(0, 0, std::vector<std::uint8_t>(31, 1))); // 62 bytes
	send(addr(1), encodeHello(ticketBytes(testTicket(0))), 1000);
	send(addr(2), encodeHello(ticketBytes(testTicket(1))), 1000);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::FORWARD);
	CHECK(send(addr(1), datagram, 1000).kind == RelayCore::Action::NONE);
	CHECK(send(addr(1), datagram, 41000).kind == RelayCore::Action::NONE); // Sol: round 2 forwarded again after 40 s
	CHECK(send(addr(1), datagram, 1000 + 4000000000000ull).kind == RelayCore::Action::NONE);
}

TEST_CASE("online2 r2: the Transport keeps the WELCOME's session token and proves its slot with it in the HELLO after a NOT_BOUND")
{
	using namespace RelayProtocol;
	UDP relaySocket, client;
	std::string error;
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relaySocket.localAddress();
	o.relay.ticket = ticketBytes(testTicket(0));
	o.relay.helloMs = 10;
	Transport t(client, 0, o);
	t.setPeer(1, NetAddress{});
	auto next = [&](std::vector<std::uint8_t> &d) {
		NetAddress from;
		const std::uint64_t start = NetMilliseconds();
		while (NetMilliseconds() - start < 2000)
		{
			t.service();
			if (relaySocket.receiveFrom(from, d) && d.size() >= kHeaderBytes && d[3] == HELLO)
			{
				return true;
			}
			NetSleepMilliseconds(1);
		}
		return false;
	};
	std::vector<std::uint8_t> d;
	REQUIRE(next(d));
	CHECK(d == encodeHello(o.relay.ticket)); // the first HELLO: the ticket alone
	std::array<std::uint8_t, kSessionTokenBytes> token{};
	for (size_t i = 0; i < token.size(); ++i)
	{
		token[i] = (std::uint8_t)(0xA0 + i);
	}
	const std::vector<std::uint8_t> welcome = encodeWelcome(0x0123456789abcdefull, 0, token), notBound = encodeEmpty(NOT_BOUND);
	relaySocket.sendTo(client.localAddress(), welcome.data(), welcome.size());
	for (int i = 0; i < 50 && !t.relayBound(); ++i)
	{
		t.service();
		NetSleepMilliseconds(1);
	}
	REQUIRE(t.relayBound());
	{
		NetAddress from;
		while (relaySocket.receiveFrom(from, d))
		{
			// the HELLOs sent before the WELCOME arrived
		}
	}
	relaySocket.sendTo(client.localAddress(), notBound.data(), notBound.size());
	REQUIRE(next(d));
	CHECK(d == encodeHello(o.relay.ticket, std::vector<std::uint8_t>(token.begin(), token.end())));
}

TEST_CASE("online2: three real Transports play through the relay with 20 % loss: every command arrives once and in order, the log holds each once")
{
	const std::filesystem::path dir = tempDir("transport");
	RelayCore::Options ro;
	ro.keys = testRing();
	ro.logDir = dir.string();
	RelayCore relay(ro);
	UDP relaySocket, s0, s1, s2;
	std::string error;
	REQUIRE_MESSAGE(relaySocket.bind(0x7F000001u, 0, &error), error);
	UDP *sockets[3] = { &s0, &s1, &s2 };
	std::vector<std::unique_ptr<Transport>> t;
	for (int i = 0; i < 3; ++i)
	{
		REQUIRE_MESSAGE(sockets[i]->bind(0x7F000001u, 0, &error), error);
		Transport::Options o;
		o.resendMs = 5;
		o.impairment.all.lossPerMille = 200;
		o.impairment.seed = 7u + (std::uint32_t)i;
		o.relay.enabled = true;
		o.relay.address = relaySocket.localAddress();
		o.relay.ticket = ticketBytes(testTicket((std::uint8_t)i));
		o.relay.helloMs = 20;
		t.push_back(std::make_unique<Transport>(*sockets[i], i, o));
		for (int p = 0; p < 3; ++p)
		{
			if (p != i)
			{
				t.back()->setPeer(p, NetAddress{ 0x7F000001u, 1 }); // ignored: every peer is the relay in relay mode
			}
		}
	}
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
	};
	// every slot sends 150 frame commands (a FRAMEINFO per frame) to both others; a big fragmented command in the middle
	const int kFrames = 150;
	for (int i = 0; i < 3; ++i)
	{
		for (int f = 0; f < kFrames; ++f)
		{
			for (int p = 0; p < 3; ++p)
			{
				if (p != i)
				{
					REQUIRE(t[(size_t)i]->sendTo(p, frameInfo((std::uint8_t)i, (std::uint32_t)f, 0), &error));
					if (f == 75)
					{
						REQUIRE(t[(size_t)i]->sendTo(p, gameCommand((std::uint8_t)i, (std::uint32_t)f, MSG_LOGIC_CRC, 5000 + i), &error));
					}
				}
			}
		}
	}
	std::map<std::pair<int, int>, int> got; // (receiver, sender) -> commands in order
	bool inOrder = true;
	const std::uint64_t start = NetMilliseconds();
	auto done = [&] {
		for (int i = 0; i < 3; ++i)
		{
			for (int p = 0; p < 3; ++p)
			{
				if (p != i && got[{ i, p }] < kFrames + 1)
				{
					return false;
				}
			}
		}
		return true;
	};
	while (!done() && NetMilliseconds() - start < 20000)
	{
		for (auto &tr : t)
		{
			tr->service();
		}
		pumpRelay();
		for (int i = 0; i < 3; ++i)
		{
			int from;
			std::vector<std::uint8_t> b;
			while (t[(size_t)i]->receive(from, b))
			{
				NetCommandMsg c;
				REQUIRE(NetPacket::decodeCommand(b, c, &error));
				int &n = got[{ i, from }];
				const int expectFrame = n <= 75 ? n : n - 1;
				inOrder = inOrder && c.slot == from && (int)c.executionFrame == expectFrame;
				++n;
			}
		}
		NetSleepMilliseconds(1);
	}
	CHECK(done());
	CHECK(inOrder);
	for (int i = 0; i < 3; ++i)
	{
		CHECK(t[(size_t)i]->relayBound());
		CHECK(t[(size_t)i]->stats().relaySlotMismatches == 0);
		CHECK(t[(size_t)i]->stats().badPackets == 0);
		CHECK(t[(size_t)i]->stats().dropped > 0);
	}
	CHECK(relay.stats().slotMismatches == 0);
	// let the acks settle, then the transports say BYE as they go
	const std::uint64_t t2 = NetMilliseconds();
	while (NetMilliseconds() - t2 < 300)
	{
		for (auto &tr : t)
		{
			tr->service();
		}
		pumpRelay();
		NetSleepMilliseconds(1);
	}
	t.clear();
	const std::uint64_t t3 = NetMilliseconds();
	while (relay.openGames() > 0 && NetMilliseconds() - t3 < 2000)
	{
		pumpRelay();
		NetSleepMilliseconds(1);
	}
	CHECK(relay.openGames() == 0);
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	CHECK(log.complete);
	CHECK(log.gaps.empty());
	std::map<int, int> perSender;
	for (const RelayLogFile::Command &c : log.commands)
	{
		++perSender[c.sender];
	}
	for (int i = 0; i < 3; ++i)
	{
		CHECK(perSender[i] == kFrames + 1); // each command once, though it went to two targets and was resent
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("online2: the relay log assembles into the frames the Network runs: slot order, the sender's player, CRCs as hashes, a leave")
{
	// a synthetic log: slots 0 and 2 of mask 0b101, crc interval 2
	std::vector<std::uint8_t> b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	auto put32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			b.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	put32(1);
	put32(0x89abcdefu);
	put32(0x01234567u);
	b.push_back(0x05);
	put32(1700000000u);
	auto cmd = [&](int sender, const std::vector<std::uint8_t> &c) {
		b.push_back(1);
		b.push_back((std::uint8_t)sender);
		put32(0);
		put32((std::uint32_t)c.size());
		b.insert(b.end(), c.begin(), c.end());
	};
	// slot 2's commands come first in the log: the frame still lists slot 0 first
	cmd(2, frameInfo(2, 0, 0));
	cmd(2, frameInfo(2, 1, 1));
	cmd(2, gameCommand(2, 1, MSG_LOGIC_CRC, 111)); // its first CRC: the hash after frame 2
	cmd(0, frameInfo(0, 0, 0));
	cmd(0, gameCommand(0, 1, MSG_LOGIC_CRC, 111));
	cmd(0, frameInfo(0, 1, 1));
	cmd(2, frameInfo(2, 2, 0));
	NetCommandMsg runAhead; // the packet router's RUNAHEAD: one of the frame's entries, no message (Network.h FrameData::entries)
	runAhead.type = NETCOMMANDTYPE_RUNAHEAD;
	runAhead.slot = 0;
	runAhead.executionFrame = 2;
	runAhead.frame = 4;
	cmd(0, NetPacket::encodeCommand(runAhead, nullptr));
	cmd(0, frameInfo(0, 2, 1));
	NetCommandMsg leave;
	leave.type = NETCOMMANDTYPE_PLAYERLEAVE;
	leave.slot = 2;
	leave.executionFrame = 3;
	cmd(2, NetPacket::encodeCommand(leave, nullptr));
	cmd(2, frameInfo(2, 3, 0)); // after its leave: discarded
	cmd(0, frameInfo(0, 3, 1));
	cmd(0, gameCommand(0, 3, MSG_LOGIC_CRC, 222));
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(b, log, &error), error);
	CHECK_FALSE(log.complete);
	NetworkConfig config;
	config.slotPlayerIndex = { { 3, -1, 5, -1, -1, -1, -1, -1 } };
	config.crcInterval = 2;
	RelayLog::Assembled a;
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, a, &error), error);
	REQUIRE(a.frames.count(1));
	const std::vector<GameMessage> &f1 = a.frames.at(1).messages();
	REQUIRE(f1.size() == 2);
	CHECK(f1[0].getPlayerIndex() == 3); // slot 0's player, whatever the sender wrote
	CHECK(f1[1].getPlayerIndex() == 5);
	CHECK(a.frames.at(3).messages().size() == 1);
	CHECK_FALSE(a.frames.count(2)); // the RUNAHEAD is no message
	CHECK(a.leaveFrame[2] == 3);
	CHECK(a.lastCompleteFrame == 3);
	CHECK(a.hashes.at(2) == 111u);
	CHECK(a.hashes.at(4) == 222u); // slot 0's second CRC (slot 2 had left)
	CHECK(a.crcDisagreements == 0);
	CHECK(a.problems.empty());
	// a disagreeing CRC is a desync the log shows
	RelayLogFile log2 = log;
	GameMessage changed(MSG_LOGIC_CRC, 99);
	changed.appendIntegerArgument(112);
	changed.appendBooleanArgument(false);
	log2.commands[2].msg.message = std::make_shared<GameMessage>(changed);
	REQUIRE(log2.commands[2].msg.type == NETCOMMANDTYPE_GAMECOMMAND);
	REQUIRE(RelayLog::assemble(log2, config, a, &error));
	CHECK(a.crcDisagreements == 1);
	CHECK_FALSE(a.hashes.count(2));
	// the replay file carries the frames and the hashes
	const std::filesystem::path dir = tempDir("replay");
	ReplayHeader h;
	h.profile = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	h.network = config;
	REQUIRE(RelayLog::assemble(log, config, a, &error));
	const std::string path = (dir / "game.obfmereplay").string();
	REQUIRE_MESSAGE(RelayLog::writeReplay(path, h, a, &error), error);
	ReplayFile rf;
	REQUIRE_MESSAGE(ReplayFile::load(path, rf, &error), error);
	CHECK(rf.frames.size() == a.frames.size());
	CHECK(rf.hashes.size() == 2);
	CHECK(rf.finalFrame == 4); // an interrupted recording plays to its last hashed frame
	std::filesystem::remove_all(dir);
	// a truncated log is an error
	std::vector<std::uint8_t> cut(b.begin(), b.end() - 3);
	CHECK_FALSE(RelayLogFile::parse(cut, log, &error));
}

TEST_CASE("online2: the lane's acceptance stops are reported (S-3710, S-3711)")
{
	const std::vector<std::string> stops = RelayLog::stopLines();
	REQUIRE(stops.size() == 2);
	CHECK(stops[0].rfind("[S-3710] relay log replay:", 0) == 0);
	CHECK(stops[1].rfind("[S-3711] relay trust", 0) == 0);
}

#ifdef OPENBFME_RELAY_EXE
TEST_CASE("online2: the openbfme_relay process welcomes a ticket over UDP, writes its status file and stops cleanly on SIGTERM")
{
	const std::filesystem::path dir = tempDir("exe");
	{
		std::ofstream k(dir / "keys.txt");
		k << "# test key\n1:" << HexBytes(testKey().data(), testKey().size()) << "\n";
	}
	UDP probe;
	std::string error;
	REQUIRE(probe.bind(0x7F000001u, 0, &error));
	// a free port: bind one and let it go
	std::uint16_t port = 0;
	{
		UDP tmp;
		REQUIRE(tmp.bind(0x7F000001u, 0, &error));
		port = tmp.localAddress().port;
	}
	const std::string bind = "127.0.0.1:" + std::to_string(port), keys = (dir / "keys.txt").string(), logs = dir.string(),
					  status = (dir / "status.json").string();
	const pid_t pid = fork();
	REQUIRE(pid >= 0);
	if (pid == 0)
	{
		execl(OPENBFME_RELAY_EXE, OPENBFME_RELAY_EXE, "--bind", bind.c_str(), "--keys", keys.c_str(), "--log-dir", logs.c_str(), "--status", status.c_str(),
			"--stats-s", "0", (char *)nullptr);
		_exit(127);
	}
	const std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(ticketBytes(testTicket(0)));
	bool welcomed = false;
	const std::uint64_t start = NetMilliseconds();
	while (!welcomed && NetMilliseconds() - start < 5000)
	{
		probe.sendTo(NetAddress{ 0x7F000001u, port }, hello.data(), hello.size());
		NetSleepMilliseconds(20);
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (probe.receiveFrom(from, d))
		{
			welcomed = welcomed || welcomeToken(d, 0).size() == RelayProtocol::kSessionTokenBytes;
		}
	}
	CHECK(welcomed);
	kill(pid, SIGTERM);
	int status_ = 0;
	waitpid(pid, &status_, 0);
	CHECK(WIFEXITED(status_));
	CHECK(WEXITSTATUS(status_) == 0);
	// the shutdown closed the game: a finished log, and the status file says so
	CHECK(std::filesystem::exists(dir / "0123456789abcdef.obfmerlog"));
	const std::vector<std::uint8_t> st = readFile(status);
	const std::string text(st.begin(), st.end());
	CHECK(text.find("\"gamesClosed\":1") != std::string::npos);
	CHECK(text.find("\"takeoversRefused\":0") != std::string::npos); // ONLINE-2 r2: the security counters are in the health check
	CHECK(text.find("\"invalidPackets\":0") != std::string::npos);
	std::filesystem::remove_all(dir);
}
#endif
