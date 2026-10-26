// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-2 (docs/ONLINE.md phase O1): SHA-256 / HMAC-SHA256 against the published vectors, the relay ticket (pinned bytes computed independently with
// Python's hmac module; services/online/lobby signs the same bytes, its own test pins the same text), the relay envelope, the online START, the relay's
// core (binding, forwarding, the slot check, the budget, the command log), real Transports talking through the relay with loss, and the relay log
// assembled into frames as the Network relays them.

#include "doctest.h"
#include "TestChild.h"
#include "OnlineTestWait.h"

#include "Common/Crc32.h"
#include "Common/MiniJson.h"
#include "Common/Sha256.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayMembership.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"
#include "RelayCore.h"
#include "RelayLogProbe.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// relaylogprobe::classicDigest(600) with lane NETDIAG-1's RelayLog::assemble (0d044941d, compiled against these headers)
constexpr std::uint64_t kNetdiag1ClassicDigest = 0xcd9078d1c4bf1210ull;

#if defined(__linux__) || defined(__APPLE__)
#include "DatagramBatch.h"
#include <unistd.h>

namespace
{
struct DatagramPair
{
	int fd[2]{ -1, -1 };
	~DatagramPair()
	{
		for (int socket : fd)
		{
			if (socket != -1)
			{
				close(socket);
			}
		}
	}
};
} // namespace

TEST_CASE("online2 batch: datagram order, empty datagrams and truncation survive a partial receive batch")
{
	DatagramPair sockets;
	REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets.fd) == 0);
	char data[] = "abcdef";
	iovec sendBuffers[3]{ { data, 3 }, { data, 0 }, { data, 6 } };
	relayio::Message outgoing[3]{};
	for (int i = 0; i < 3; ++i)
	{
		outgoing[i].msg_hdr.msg_iov = &sendBuffers[i];
		outgoing[i].msg_hdr.msg_iovlen = 1;
	}
	REQUIRE(relayio::sendBatch(sockets.fd[0], outgoing, 3) == 3);
	CHECK(outgoing[0].msg_len == 3);
	CHECK(outgoing[1].msg_len == 0);
	CHECK(outgoing[2].msg_len == 6);
	char dataIn[4][4]{};
	iovec receiveBuffers[4]{};
	relayio::Message incoming[4]{};
	for (int i = 0; i < 4; ++i)
	{
		receiveBuffers[i] = { dataIn[i], sizeof dataIn[i] };
		incoming[i].msg_hdr.msg_iov = &receiveBuffers[i];
		incoming[i].msg_hdr.msg_iovlen = 1;
	}
	REQUIRE(relayio::receiveBatch(sockets.fd[1], incoming, 4) == 3);
	CHECK(incoming[0].msg_len == 3);
	CHECK(std::string(dataIn[0], 3) == "abc");
	CHECK((incoming[0].msg_hdr.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) == 0);
	CHECK(incoming[1].msg_len == 0);
	CHECK((incoming[1].msg_hdr.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) == 0);
	CHECK(incoming[2].msg_len == 4);
	CHECK(std::string(dataIn[2], 4) == "abcd");
	CHECK((incoming[2].msg_hdr.msg_flags & MSG_TRUNC) == MSG_TRUNC);
	CHECK(relayio::receiveBatch(sockets.fd[1], incoming, 4) == -1);
	CHECK((errno == EAGAIN || errno == EWOULDBLOCK));
}

TEST_CASE("online2 batch: a send error returns the completed prefix, or minus one when nothing was sent")
{
	DatagramPair sockets;
	REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets.fd) == 0);
	char data = 'x';
	iovec buffer{ &data, 1 };
	relayio::Message outgoing[2]{};
	outgoing[0].msg_hdr.msg_iov = &buffer;
	outgoing[0].msg_hdr.msg_iovlen = 1;
	outgoing[1].msg_hdr.msg_iovlen = 1; // null iovec: the second message must fail
	CHECK(relayio::sendBatch(sockets.fd[0], outgoing, 2) == 1);
	CHECK(outgoing[0].msg_len == 1);
	CHECK(relayio::sendBatch(-1, outgoing, 1) == -1);
	CHECK(errno == EBADF);
	CHECK(relayio::receiveBatch(-1, outgoing, 1) == -1);
	CHECK(errno == EBADF);
}
#endif

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

namespace
{
// lane NETDIAG-1: an ack-only transport packet (no records) from `sender` acknowledging records below `ack`
std::vector<std::uint8_t> ackPacket(std::uint8_t sender, std::uint32_t ack)
{
	std::vector<std::uint8_t> p = transportPacket(sender, 0, {});
	p.resize(14);
	p[12] = p[13] = 0;
	for (int i = 0; i < 4; ++i)
	{
		p[(size_t)i + 8] = (std::uint8_t)(ack >> (8 * i));
	}
	const std::uint32_t crc = crc32Bytes(p.data() + 4, p.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		p[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return p;
}
} // namespace

// lane NETDIAG-1 (relay health, from a contributor's change): the same scripted exchange on every DATA path that forwards a transport packet —
// a plain lockstep game without and with the command log (follow), and a canonical game's direct commands (validatePacket only)
// lane NETDIAG-1 (from a contributor's parser hardening): a GAP reason other than 1 / 2 and an END reason other than 1..3 are refused (RelayCore
// writes no other); the valid ones parse in an eight-slot version-2 log and a sixteen-slot version-4 one. (The contributor's `slot >= 8` checks are
// not taken: RelayLogFile already checks every slot against the log's wire slots and its slot mask.)
TEST_CASE("online2: relay log GAP and END reasons outside what the relay writes are refused; the valid ones parse in v2 and v4 logs")
{
	auto header = [](std::uint32_t version, std::uint16_t mask) {
		std::vector<std::uint8_t> log{ 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
		NetByteWriter w;
		w.u32(version);
		w.u32(0x89abcdefu);
		w.u32(0x01234567u);
		w.u8((std::uint8_t)mask);
		if (version == 4)
		{
			w.u8((std::uint8_t)(mask >> 8));
		}
		w.u32(1700000000u);
		log.insert(log.end(), w.data().begin(), w.data().end());
		return log;
	};
	auto gap = [](std::uint8_t sender, std::uint8_t target, std::uint8_t reason) { return std::vector<std::uint8_t>{ 3, sender, target, 0, 0, 0, 0, reason }; };
	auto end = [](std::uint8_t reason, size_t wireSlots) {
		std::vector<std::uint8_t> r{ 4, 0, 0, 0, 0, reason };
		r.insert(r.end(), wireSlots * 24, 0);
		return r;
	};
	auto parse = [](std::vector<std::uint8_t> log, const std::vector<std::vector<std::uint8_t>> &records, std::string &error) {
		for (const std::vector<std::uint8_t> &r : records)
		{
			log.insert(log.end(), r.begin(), r.end());
		}
		RelayLogFile parsed;
		error.clear();
		return RelayLogFile::parse(log, parsed, &error) ? (int)parsed.gaps.size() * 10 + parsed.endReason : -1;
	};
	std::string error;
	const std::vector<std::uint8_t> v2 = header(2, 0x03), v4 = header(4, 0x8001);
	CHECK(parse(v2, { gap(0, 1, 0) }, error) == -1);
	CHECK(error == "relay log: invalid GAP reason 0");
	CHECK(parse(v4, { gap(15, 0, 3) }, error) == -1);
	CHECK(error == "relay log: invalid GAP reason 3");
	CHECK(parse(v2, { end(0, 8) }, error) == -1);
	CHECK(error == "relay log: invalid END reason 0");
	CHECK(parse(v4, { end(4, 16) }, error) == -1);
	CHECK(error == "relay log: invalid END reason 4");
	CHECK(parse(v2, { gap(0, 1, 1), gap(1, 0, 2), end(1, 8) }, error) == 21);
	CHECK(parse(v2, { end(2, 8) }, error) == 2);
	CHECK(parse(v4, { gap(15, 0, 1), gap(0, 15, 2), end(3, 16) }, error) == 23);
	// the slot checks that were already there: slot 8 of an eight-slot log, slot 14 outside a sixteen-slot log's mask
	CHECK(parse(v2, { gap(8, 0, 1) }, error) == -1);
	CHECK(error == "relay log: invalid GAP slot");
	CHECK(parse(v4, { gap(14, 0, 1) }, error) == -1);
	CHECK(error == "relay log: invalid GAP slot");
}

// lane NETDIAG-1 (found by the relay_log fuzzer, 26 s on one 289-byte log): the assembly walked every frame up to the highest execution frame any
// command named while a slot was held for a REJOIN, though no slot ran a frame any more; a client chooses that frame number (0xFFFFFFFE here:
// four billion empty frames; 0xFFFFFFFF never ended). It now ends at the first frame with no member, with the same result.
// the classic rooms' assembly (2 or more network slots) is lane NETDIAG-1's (0d044941d): the hash of 600 generated logs' assembled results
// (tests/RelayLogProbe.h), measured with 0d044941d's RelayLog::assemble
TEST_CASE("online2: a classic room's relay log assembles exactly as lane NETDIAG-1's assembler did (600 generated logs)")
{
	const std::uint64_t digest = relaylogprobe::classicDigest(600, [](const RelayLogFile &l, const NetworkConfig &c, RelayLog::Assembled &a, std::string *e) {
		return RelayLog::assemble(l, c, a, e);
	});
	CHECK(digest == kNetdiag1ClassicDigest);
}

// lane SOLO-1 x NETDIAG-1: a one-human room's walk jumps to its held slot's own REJOINs; a crafted log of tens of thousands of incarnations,
// each one's REJOIN far ahead, and as many drops, assembles in linear time (each frame, incarnation and drop is looked at a bounded number of times)
TEST_CASE("online2: a crafted one-human room's relay log of far-ahead REJOINs and many drops assembles in well under a second")
{
	RelayLogFile log;
	log.version = 2;
	log.slotMask = 0x01;
	auto add = [&](const NetCommandMsg &m) {
		RelayLogFile::Command c;
		c.sender = 0;
		c.msg = m;
		log.commands.push_back(c);
	};
	for (std::uint32_t f = 0; f < 3; ++f)
	{
		add(relaylogprobe::frameInfo(0, f, 0));
	}
	const int kIncarnations = 60000;
	for (int k = 1; k <= kIncarnations; ++k)
	{
		RelayLogFile::Rejoined j;
		j.slot = 0;
		j.generation = k;
		j.firstCommand = log.commands.size();
		log.rejoins.push_back(j);
		const std::uint32_t at = 10u + (std::uint32_t)k * 1000u; // the slot's own REJOIN, far ahead of the last frame it ran
		NetCommandMsg r;
		r.type = NETCOMMANDTYPE_REJOIN;
		r.executionFrame = at;
		r.targetSlot = 0;
		add(r);
		add(relaylogprobe::frameInfo(0, at, 1));
		add(relaylogprobe::frameInfo(0, at + 1, 0));
	}
	for (int d = 0; d < 20000; ++d)
	{
		NetCommandMsg m;
		m.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
		m.targetSlot = 0;
		m.frame = 100000000u + (std::uint32_t)d * 7u; // after every incarnation's frames: each joined incarnation takes the first one
		add(m);
	}
	NetworkConfig config;
	config.slotPlayerIndex.fill(-1);
	config.slotPlayerIndex[0] = 0;
	RelayLog::Assembled a;
	std::string error;
	const auto t0 = std::chrono::steady_clock::now();
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, a, &error), error);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	MESSAGE("crafted one-human relay log of ", log.commands.size(), " records assembled in ", ms, " ms");
	CHECK(ms < 1000.0); // tolerance: a linear walk takes tens of milliseconds; the quadratic one took minutes
	// every incarnation's own REJOIN ran: it was admitted, ran its two frames and ended where its successor began
	size_t rejoins = 0;
	for (const auto &m : a.membership)
	{
		rejoins += m.kind == Network::MembershipEvent::REJOIN ? 1 : 0;
	}
	CHECK(rejoins == (size_t)kIncarnations);
	CHECK(a.joinFrame[0] == 10u + (std::uint32_t)kIncarnations * 1000u + 1u);
}

TEST_CASE("online2: the relay log assembly ends when no slot runs a frame, whatever frame a held slot's command names")
{
	auto assembleWith = [](std::uint32_t namedFrame, RelayLog::Assembled &a) {
		RelayLogFile log;
		log.version = 2;
		log.slotMask = 0x03;
		auto add = [&](int sender, NetCommandMsg m) {
			RelayLogFile::Command c;
			c.sender = sender;
			c.msg = m;
			log.commands.push_back(c);
		};
		auto info = [](int slot, std::uint32_t frame) {
			NetCommandMsg m;
			m.type = NETCOMMANDTYPE_FRAMEINFO;
			m.slot = (std::uint8_t)slot;
			m.executionFrame = frame;
			m.commandCount = 0;
			return m;
		};
		auto leave = [](int slot, std::uint32_t frame) {
			NetCommandMsg m;
			m.type = NETCOMMANDTYPE_PLAYERLEAVE;
			m.slot = (std::uint8_t)slot;
			m.executionFrame = frame;
			return m;
		};
		// slot 0 runs frames 0..2 and leaves at 3; slot 1 runs frame 0, leaves at 1 and comes back (REJOINED) but no REJOIN of it ever runs
		for (std::uint32_t f = 0; f < 3; ++f)
		{
			add(0, info(0, f));
		}
		add(0, leave(0, 3));
		add(1, info(1, 0));
		add(1, leave(1, 1));
		RelayLogFile::Rejoined j;
		j.slot = 1;
		j.generation = 1;
		j.firstCommand = log.commands.size();
		log.rejoins.push_back(j);
		add(1, info(1, namedFrame)); // the held incarnation's frame info
		NetworkConfig config;
		config.slotPlayerIndex = { { 0, 1, -1, -1, -1, -1, -1, -1 } };
		std::string error;
		const bool ok = RelayLog::assemble(log, config, a, &error);
		CHECK_MESSAGE(ok, error);
		return ok;
	};
	RelayLog::Assembled small, huge;
	REQUIRE(assembleWith(40, small));
	REQUIRE(assembleWith(0xFFFFFFFEu, huge));
	// what the walk to frame 40 gave (each empty frame up to the named one counted complete): the shortcut gives the same
	CHECK(small.anyComplete);
	CHECK(small.lastCompleteFrame == 40);
	CHECK(small.frames.empty());
	CHECK(huge.lastCompleteFrame == 0xFFFFFFFEu);
	CHECK(huge.frames.empty());
	CHECK(huge.membership.size() == small.membership.size());
	CHECK(huge.problems == small.problems);
}

TEST_CASE("online health: passive game metrics have exact counts, bounded samples and no endpoint or payload identity")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	std::filesystem::path dir;
	std::uint8_t mode = 0, caps = 0;
	SUBCASE("without command logging") {}
	SUBCASE("with command logging")
	{
		dir = tempDir("health");
		options.logDir = dir.string();
	}
	SUBCASE("canonical game: direct commands")
	{
		mode = TICKET_MODE_CANONICAL;
		caps = CAP_CANONICAL;
		dir = tempDir("health_canonical"); // a canonical game needs its log (RelayCore::hello)
		options.logDir = dir.string();
	}
	RelayCore relay(options);
	auto ticket = [&](std::uint8_t slot) {
		Ticket t = testTicket(slot);
		t.mode = mode;
		return ticketBytes(t);
	};
	auto send = [&](int slot, std::vector<std::uint8_t> bytes, std::uint64_t at, bool fail = false) {
		const RelayCore::Action action = relay.handle(addr((std::uint16_t)(100 + slot)), bytes.data(), bytes.size(), at, 1700000000u);
		std::vector<RelayCore::Action> extra;
		relay.takePending(extra); // a canonical game's own emissions are not part of the script
		if (action.kind != RelayCore::Action::NONE)
		{
			relay.noteSend(fail ? 0 : 1, fail ? 1 : 0);
		}
		return action.kind;
	};
	auto ack = [&](std::uint32_t end, std::uint64_t at) { CHECK(send(1, encodeData(0, ackPacket(1, end)), at) == RelayCore::Action::FORWARD); };
	CHECK(send(0, encodeHello(ticket(0), {}, caps), 0) == RelayCore::Action::REPLY);
	CHECK(send(1, encodeHello(ticket(1), {}, caps), 0) == RelayCore::Action::REPLY);
	CHECK(send(0, encodeData(1, transportPacket(0, 0, frameInfo(0, 10, 0))), 100) == RelayCore::Action::FORWARD);
	ack(1, 120);
	CHECK(send(0, encodeData(1, transportPacket(0, 1, frameInfo(0, 12, 0))), 200) == RelayCore::Action::FORWARD);
	ack(2, 250);
	const std::vector<std::uint8_t> resend = encodeData(1, transportPacket(0, 2, frameInfo(0, 11, 0)));
	CHECK(send(0, resend, 300) == RelayCore::Action::FORWARD);
	CHECK(send(0, resend, 310) == RelayCore::Action::FORWARD);
	ack(3, 330); // Karn: the retransmitted record contributes no RTT/jitter sample
	const std::string identity = "player@example.org";
	CHECK(send(0, encodeData(1, transportPacket(0, 3, { identity.begin(), identity.end() })), 400, true) == RelayCore::Action::FORWARD);
	CHECK(send(0, encodeData(9, transportPacket(0, 4, frameInfo(0, 14, 0))), 450) == RelayCore::Action::NONE); // slot 9: not in the mask
	std::vector<std::uint8_t> malformed = transportPacket(0, 4, frameInfo(0, 14, 0));
	malformed[0] ^= 1;
	CHECK(send(0, encodeData(1, malformed), 460) == RelayCore::Action::NONE);
	CHECK(send(0, { 1, 2, 3 }, 470) == RelayCore::Action::NONE);
	relay.noteTruncated();
	JsonValue health;
	std::string error;
	const std::string json = relay.healthJson(1000, 4096);
	REQUIRE_MESSAGE(JsonValue::parse(json, health, &error), error);
	CHECK(health.get("event")->string == "relay_health");
	CHECK(health.get("activeGames")->number == 1);
	CHECK(health.get("players")->number == 2);
	CHECK(health.get("spectators")->number == 0);
	CHECK(health.get("packetsIn")->number == 14);
	CHECK(health.get("packetsOut")->number == 9);
	CHECK(health.get("packetsInPerSecond")->number == 14);
	CHECK(health.get("packetsOutPerSecond")->number == 9);
	CHECK(health.get("sampleMs")->number == 1000);
	CHECK(health.get("uptimeMs")->number == 1000);
	CHECK(health.get("residentBytes")->number == 4096);
	CHECK(health.get("metricsBytes")->number == sizeof(RelayHealth));
	CHECK(health.get("bufferedBytes")->number == relay.heldBytes());
	CHECK(health.get("dataDrops")->number == 2); // the bad target and the bad CRC
	CHECK(health.get("invalidDatagrams")->number == 2); // the 3-byte datagram and the truncated one
	CHECK(health.get("sendDrops")->number == 1);
	CHECK(health.get("resends")->number == 1);
	CHECK(health.get("lateFrames")->number == 1);
	REQUIRE(health.get("games")->array.size() == 1);
	const JsonValue &game = health.get("games")->array[0];
	CHECK(game.get("game")->string == "0123456789abcdef");
	CHECK(game.get("players")->number == 2);
	CHECK(game.get("dataDrops")->number == 2);
	CHECK(game.get("resends")->number == 1);
	CHECK(game.get("lateFrames")->number == 1);
	CHECK(game.get("ackRtt")->get("samples")->number == 2);
	CHECK(game.get("ackRtt")->get("p50Ms")->number == 20);
	CHECK(game.get("ackRtt")->get("p95Ms")->number == 50);
	CHECK(game.get("ackRtt")->get("p99Ms")->number == 50);
	CHECK(game.get("ackJitter")->get("samples")->number == 1);
	CHECK(game.get("ackJitter")->get("p50Ms")->number == 30);
	CHECK(game.get("ackJitter")->get("p95Ms")->number == 30);
	CHECK(game.get("ackJitter")->get("p99Ms")->number == 30);
	// privacy: game ids and counters only (no address, e-mail, IPv6, ticket or session material)
	CHECK_FALSE(std::regex_search(json, std::regex(R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}|(?:[A-Fa-f0-9]{1,4}:){2,})")));
	CHECK(json.find(addr(100).text()) == std::string::npos);
	CHECK(json.find(identity) == std::string::npos);
	CHECK(json.find("ticket") == std::string::npos);
	CHECK(json.find("session") == std::string::npos);
	REQUIRE(JsonValue::parse(relay.healthJson(2000), health, &error));
	CHECK(health.get("residentBytes")->type == JsonValue::NUL);
	CHECK(health.get("packetsInPerSecond")->number == 0);
	CHECK(health.get("packetsOutPerSecond")->number == 0);
	CHECK(health.get("packetsIn")->number == 14);
	relay.shutdown(2100);
	REQUIRE(JsonValue::parse(relay.healthJson(3000), health, &error));
	CHECK(health.get("activeGames")->number == 0);
	CHECK(health.get("players")->number == 0);
	CHECK(health.get("metricsBytes")->number == 0);
	CHECK(health.get("resends")->number == 1);
	CHECK(health.get("games")->array.empty());
	if (!dir.empty())
	{
		std::filesystem::remove_all(dir);
	}
}

TEST_CASE("online health: collection can be disabled without changing relay forwarding")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.metrics = false;
	RelayCore relay(options);
	for (int slot : { 0, 1 })
	{
		std::vector<std::uint8_t> hello = encodeHello(ticketBytes(testTicket((std::uint8_t)slot)));
		CHECK(relay.handle(addr((std::uint16_t)(100 + slot)), hello.data(), hello.size(), 0, 1700000000u).kind == RelayCore::Action::REPLY);
	}
	std::vector<std::uint8_t> data = encodeData(1, transportPacket(0, 0, frameInfo(0, 10, 0)));
	CHECK(relay.handle(addr(100), data.data(), data.size(), 100, 1700000000u).kind == RelayCore::Action::FORWARD);
	relay.noteSend(3, 1);
	relay.noteTruncated(); // the legacy counters are unchanged with metrics off
	CHECK(relay.healthJson(1000).empty());
	CHECK(relay.stats().forwarded == 1);
	CHECK(relay.stats().datagrams == 3);
	CHECK(relay.stats().badDatagrams == 0);
	CHECK(relay.heldBytes() == 0);
}

TEST_CASE("online health: pending ACK coverage is bounded, invalid ACKs are ignored and closed games disappear")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	options.idleMs = 1000;
	options.silentMs = 1000;
	RelayCore relay(options);
	auto send = [&](int slot, std::vector<std::uint8_t> bytes, std::uint64_t at) {
		return relay.handle(addr((std::uint16_t)(100 + slot)), bytes.data(), bytes.size(), at, 1700000000u).kind;
	};
	for (int slot : { 0, 1 })
	{
		CHECK(send(slot, encodeHello(ticketBytes(testTicket((std::uint8_t)slot))), 0) == RelayCore::Action::REPLY);
	}
	CHECK(send(2, encodeHello(ticketBytes(testTicket(0, 1, 0xfedcba9876543210ull))), 0) == RelayCore::Action::REPLY);
	JsonValue health;
	std::string error;
	REQUIRE(JsonValue::parse(relay.healthJson(1), health, &error));
	CHECK(health.get("activeGames")->number == 2);
	CHECK(health.get("players")->number == 3);
	CHECK(health.get("metricsBytes")->number == 2 * sizeof(RelayHealth));
	CHECK(health.get("games")->array[0].get("ackRtt")->get("p95Ms")->type == JsonValue::NUL);
	for (unsigned seq = 0; seq < 6; ++seq)
	{
		CHECK(send(0, encodeData(1, transportPacket(0, seq, frameInfo(0, 10 + seq, 0))), 100 + seq * 10) == RelayCore::Action::FORWARD);
	}
	auto ack = [&](std::uint32_t end, std::uint64_t at) { CHECK(send(1, encodeData(0, ackPacket(1, end)), at) == RelayCore::Action::FORWARD); };
	ack(1000, 200); // beyond the observed sequence range: no diagnostic sample
	ack(2, 210);    // these two packets fell out of the four pending timestamps
	REQUIRE(JsonValue::parse(relay.healthJson(220), health, &error));
	CHECK(health.get("games")->array[0].get("ackRtt")->get("samples")->number == 0);
	ack(6, 250); // one cumulative ACK samples only the newest eligible packet (sent at 150)
	ack(6, 260); // a repeated ACK: never another sample
	REQUIRE(JsonValue::parse(relay.healthJson(300), health, &error));
	CHECK(health.get("games")->array[0].get("ackRtt")->get("samples")->number == 1);
	CHECK(health.get("games")->array[0].get("ackRtt")->get("p95Ms")->number == 100);
	CHECK(health.get("games")->array[0].get("ackJitter")->get("samples")->number == 0);
	relay.tick(1500);
	REQUIRE(JsonValue::parse(relay.healthJson(1500), health, &error));
	CHECK(health.get("activeGames")->number == 0);
	CHECK(health.get("players")->number == 0);
	CHECK(health.get("metricsBytes")->number == 0);
}

// lane NETDIAG-1: dataDrops are the DATA the relay refused (the slot's `dropped` of the log's END record), not every DATA that went nowhere: a
// target that has not bound yet (its transport resends) is unboundTargets only. The contributor's version counted every DATA answered with nothing.
TEST_CASE("online health: dataDrops count refused DATA only, not DATA to a target that has not bound yet")
{
	using namespace RelayProtocol;
	const std::vector<std::uint8_t> toUnbound = encodeData(2, transportPacket(0, 0, frameInfo(0, 10, 0))); // slot 2: in the mask, not bound
	RelayCore::Options options;
	options.keys = testRing();
	options.bytesPerSecond = 0; // the burst only, two datagrams' worth: the third is over the budget
	options.burstBytes = (std::uint32_t)(2 * toUnbound.size());
	RelayCore relay(options);
	auto send = [&](int slot, std::vector<std::uint8_t> bytes, std::uint64_t at) {
		return relay.handle(addr((std::uint16_t)(100 + slot)), bytes.data(), bytes.size(), at, 1700000000u).kind;
	};
	CHECK(send(0, encodeHello(ticketBytes(testTicket(0))), 0) == RelayCore::Action::REPLY);
	CHECK(send(1, encodeHello(ticketBytes(testTicket(1))), 0) == RelayCore::Action::REPLY);
	CHECK(send(0, toUnbound, 100) == RelayCore::Action::NONE);
	CHECK(send(0, encodeData(1, transportPacket(0, 0, frameInfo(0, 10, 0))), 110) == RelayCore::Action::FORWARD);
	CHECK(send(0, encodeData(1, transportPacket(0, 1, frameInfo(0, 11, 0))), 120) == RelayCore::Action::NONE); // over the burst
	CHECK(relay.stats().unboundTargets == 1);
	CHECK(relay.stats().rateLimited == 1);
	JsonValue health;
	std::string error;
	REQUIRE(JsonValue::parse(relay.healthJson(1000), health, &error));
	CHECK(health.get("dataDrops")->number == 1);
	CHECK(health.get("games")->array[0].get("dataDrops")->number == 1);
}

// lane NETDIAG-1: the log-only stream (DATA addressed to the sender's own slot, answered by the relay) is observed too: its resends and late frames
// count; it times nothing (no receiver acknowledges it)
TEST_CASE("online health: the log-only stream counts resends and late frames and takes no ACK sample")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	RelayCore relay(options);
	auto send = [&](int slot, std::vector<std::uint8_t> bytes, std::uint64_t at) {
		return relay.handle(addr((std::uint16_t)(100 + slot)), bytes.data(), bytes.size(), at, 1700000000u).kind;
	};
	CHECK(send(0, encodeHello(ticketBytes(testTicket(0))), 0) == RelayCore::Action::REPLY);
	CHECK(send(0, encodeData(0, transportPacket(0, 0, frameInfo(0, 20, 0))), 100) == RelayCore::Action::REPLY);
	CHECK(send(0, encodeData(0, transportPacket(0, 1, frameInfo(0, 19, 0))), 110) == RelayCore::Action::REPLY);
	CHECK(send(0, encodeData(0, transportPacket(0, 1, frameInfo(0, 19, 0))), 120) == RelayCore::Action::REPLY);
	CHECK(send(0, encodeData(0, ackPacket(0, 2)), 130) == RelayCore::Action::REPLY);
	CHECK(relay.stats().logStreamPackets == 4);
	JsonValue health;
	std::string error;
	REQUIRE(JsonValue::parse(relay.healthJson(1000), health, &error));
	const JsonValue &game = health.get("games")->array[0];
	CHECK(game.get("resends")->number == 1);
	CHECK(game.get("lateFrames")->number == 1);
	CHECK(game.get("dataDrops")->number == 0);
	CHECK(game.get("ackRtt")->get("samples")->number == 0);
}

TEST_CASE("online health: percentile storage wraps at 128 samples and duplicate history is fixed-size")
{
	RelayHealth::Samples samples;
	for (unsigned n = 1; n <= 256; ++n)
	{
		samples.add(n);
	}
	std::ostringstream out;
	samples.json(out);
	CHECK(out.str() == "{\"samples\":128,\"p50Ms\":192,\"p95Ms\":250,\"p99Ms\":255}");
	RelayHealth::Samples empty;
	std::ostringstream missing;
	empty.json(missing);
	CHECK(missing.str() == "{\"samples\":0,\"p50Ms\":null,\"p95Ms\":null,\"p99Ms\":null}");
	RelayHealth::Stream stream;
	bool duplicate = false;
	CHECK(stream.fresh(100, duplicate));
	CHECK_FALSE(stream.fresh(100, duplicate));
	CHECK(duplicate);
	CHECK(stream.fresh(99, duplicate));
	CHECK_FALSE(stream.fresh(1, duplicate));
	CHECK_FALSE(duplicate); // beyond the 64-record window is unmeasured, not a claimed retransmission
	CHECK(stream.fresh(200, duplicate));
	CHECK_FALSE(stream.fresh(100, duplicate));
	CHECK_FALSE(duplicate);
	RelayHealth::Stream wrapping;
	CHECK(wrapping.fresh(0xffffffffu, duplicate));
	CHECK(wrapping.fresh(0, duplicate));
	CHECK_FALSE(wrapping.fresh(0xffffffffu, duplicate));
	CHECK(duplicate);
	// every relay slot has its streams (sixteen: RelayProtocol::kMaxSlots), so slots 8..15 index inside the table
	CHECK(RelayHealth().streams.size() == 16);
	CHECK(RelayHealth().streams[15].size() == 16);
}

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

TEST_CASE("p16 net: wide relay tickets preserve slot 15 and authenticate every byte")
{
	using namespace RelayProtocol;
	Ticket t = testTicket(15);
	t.slotMask = 0x8001;
	t.generation = 7;
	t.delaySeconds = 60;
	const std::vector<std::vector<std::uint8_t>> tickets = { signTicket(t, testKey()), signRejoinTicket(t, testKey()), signSpectatorTicket(t, testKey()) };
	// Independent Python struct.pack('<BBQBHI', ...) + hmac/sha256, key bytes 0..31.
	const char *pins[] = {
		"0401efcdab89674523010f0180009435774e6bb4312fe030524161d836cb3e692c",
		"0501efcdab89674523010f018000943577070034015cecde49f2aa031c8ddb1a0e27f2",
		"0601efcdab89674523010f0180009435773c00d513e3a3d58c88c5cc0798624a466942"
	};
	for (size_t kind = 0; kind < tickets.size(); ++kind)
	{
		const auto &bytes = tickets[kind];
		CHECK(HexBytes(bytes.data(), bytes.size()) == pins[kind]);
		CHECK(bytes.size() == ticketSize((std::uint8_t)(4 + kind)));
		Ticket out;
		std::string error;
		REQUIRE(verifyTicket(bytes.data(), bytes.size(), testRing(), 1700000000u, out, &error));
		CHECK(out.gameId == t.gameId);
		CHECK(out.slot == 15);
		CHECK(out.slotMask == 0x8001);
		CHECK(out.generation == (kind == 1 ? 7 : 0));
		CHECK(out.spectator == (kind == 2));
		CHECK(out.delaySeconds == (kind == 2 ? 60 : 0));
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			auto bad = bytes;
			bad[i] ^= 1;
			CHECK_FALSE(verifyTicket(bad.data(), bad.size(), testRing(), 1700000000u, out, &error));
		}
		CHECK_FALSE(verifyTicket(bytes.data(), bytes.size() - 1, testRing(), 1700000000u, out, &error));
		CHECK_FALSE(verifyTicket(bytes.data(), bytes.size(), testRing(), 2000000001u, out, &error));
	}
	Ticket out;
	std::string error;
	CHECK_FALSE(readTicketFields(nullptr, 0, out, &error));
	t.slot = 16;
	auto bad = signTicket(t, testKey());
	CHECK_FALSE(verifyTicket(bad.data(), bad.size(), testRing(), 1700000000u, out, &error));
	t.slot = 14;
	bad = signTicket(t, testKey());
	CHECK_FALSE(verifyTicket(bad.data(), bad.size(), testRing(), 1700000000u, out, &error));
	t.slot = 15;
	t.generation = 0;
	bad = signRejoinTicket(t, testKey());
	CHECK_FALSE(verifyTicket(bad.data(), bad.size(), testRing(), 1700000000u, out, &error));
}

TEST_CASE("p16 net: Transport accepts all six versioned ticket widths and rejects malformed lengths")
{
	using namespace RelayProtocol;
	UDP socket;
	for (bool extended : { false, true })
	{
		Ticket ticket = testTicket(extended ? 15 : 1);
		ticket.slotMask = extended ? 0xffff : 0x03;
		ticket.generation = 1;
		ticket.delaySeconds = 60;
		const std::vector<std::vector<std::uint8_t>> tickets = {
			signTicket(ticket, testKey()), signRejoinTicket(ticket, testKey()), signSpectatorTicket(ticket, testKey())
		};
		for (const auto &bytes : tickets)
		{
			Transport::Options options;
			options.relay.enabled = true;
			options.relay.ticket = bytes;
			Transport valid(socket, ticket.slot, options);
			CHECK(valid.errors().empty());
			options.relay.ticket.pop_back();
			Transport truncated(socket, ticket.slot, options);
			CHECK_FALSE(truncated.errors().empty());
			options.relay.ticket = bytes;
			options.relay.ticket.push_back(0);
			Transport trailing(socket, ticket.slot, options);
			CHECK_FALSE(trailing.errors().empty());
			options.relay.ticket = bytes;
			options.relay.ticket[0] = 127;
			Transport unknown(socket, ticket.slot, options);
			CHECK_FALSE(unknown.errors().empty());
		}
	}
	Transport::Options options;
	options.relay.enabled = true;
	Transport missing(socket, 0, options);
	CHECK_FALSE(missing.errors().empty());
}

TEST_CASE("p16 net: sixteen relay slots bind, forward, log-only, rejoin and reject slot 16")
{
	using namespace RelayProtocol;
	RelayCore::Options options;
	options.keys = testRing();
	RelayCore relay(options);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) {
		return relay.handle(from, d.data(), d.size(), 1000, 1700000000u);
	};
	for (std::uint8_t slot = 0; slot < 16; ++slot)
	{
		Ticket player = testTicket(slot);
		player.slotMask = 0xffff;
		const RelayCore::Action a = send(addr((std::uint16_t)(100 + slot)), encodeHello(ticketBytes(player)));
		CHECK(a.kind == RelayCore::Action::REPLY);
		CHECK(welcomeToken(a.reply, slot).size() == kSessionTokenBytes);
	}
	CHECK(relay.openGames() == 1);
	const RelayCore::Action forwarded = send(addr(115), encodeData(0, transportPacket(15, 0, { 1, 2, 3 })));
	CHECK(forwarded.kind == RelayCore::Action::FORWARD);
	CHECK(forwarded.to == addr(100));
	const RelayCore::Action logOnly = send(addr(115), encodeData(15, transportPacket(15, 1, { 4 })));
	CHECK(logOnly.kind == RelayCore::Action::REPLY);
	CHECK(logOnly.reply[4] == 15);
	Ticket spectator = testTicket(0);
	spectator.slotMask = 0xffff;
	spectator.spectator = true;
	const std::vector<std::uint8_t> spectatorTicket = signSpectatorTicket(spectator, testKey());
	const RelayCore::Action spectatorWelcome = send(addr(300), encodeHello(spectatorTicket));
	CHECK(spectatorWelcome.reply[12] == (std::uint8_t)(kSpectatorWelcome));
	CHECK(send(addr(300), encodeData(0, transportPacket(0, 0, { 5 }))).reply == encodeRefused(REFUSE_SPECTATOR));
	Ticket bad = testTicket(16);
	bad.slotMask = 0xffff;
	CHECK(send(addr(200), encodeHello(ticketBytes(bad))).reply == encodeRefused(REFUSE_BAD_TICKET));
	Ticket rejoin = testTicket(15);
	rejoin.slotMask = 0xffff;
	rejoin.generation = 1;
	const std::vector<std::uint8_t> rejoinBytes = signRejoinTicket(rejoin, testKey());
	const RelayCore::Action rebound = send(addr(215), encodeHello(rejoinBytes));
	CHECK(rebound.kind == RelayCore::Action::REPLY);
	std::vector<RelayCore::Action> statuses;
	relay.tick(2000, &statuses);
	bool sawWideStatus = false;
	for (const RelayCore::Action &action : statuses)
	{
		std::vector<SlotStatus> decoded;
		if (decodeStatus(action.reply.data(), action.reply.size(), decoded) && decoded.size() == 16)
		{
			for (const SlotStatus &slot : decoded)
			{
				sawWideStatus = sawWideStatus || (slot.slot == 15 && slot.generation == 1);
			}
		}
	}
	CHECK(sawWideStatus);
}

TEST_CASE("p16 net: version 4 relay logs parse sixteen stats and reject truncation or unknown slots")
{
	std::vector<std::uint8_t> b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	auto put32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(v >> (8 * i)));
	};
	auto put64 = [&](std::uint64_t v) {
		put32((std::uint32_t)v);
		put32((std::uint32_t)(v >> 32));
	};
	put32(4);
	put64(0x0123456789abcdefull);
	b.push_back(0xff);
	b.push_back(0xff);
	put32(1700000000u);
	const std::vector<std::uint8_t> command = gameCommand(15, 0, MSG_LOGIC_CRC, 7);
	b.push_back(1);
	b.push_back(15);
	put32(0);
	put32((std::uint32_t)command.size());
	b.insert(b.end(), command.begin(), command.end());
	b.insert(b.end(), { 4, 0, 0, 0, 0, 1 });
	for (int i = 0; i < 16; ++i)
	{
		put64((std::uint64_t)i);
		put64((std::uint64_t)i + 100);
		put64((std::uint64_t)i + 200);
	}
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(b, log, &error), error);
	CHECK(log.version == 4);
	CHECK(log.slotMask == 0xffff);
	REQUIRE(log.commands.size() == 1);
	CHECK(log.commands[0].sender == 15);
	CHECK(log.complete);
	CHECK(log.datagramsIn[15] == 15);
	CHECK(log.dropped[15] == 215);
	std::vector<std::uint8_t> cut(b.begin(), b.end() - 1);
	CHECK_FALSE(RelayLogFile::parse(cut, log, &error));
	std::vector<std::uint8_t> unknown = b;
	unknown[20] = 1;
	unknown[21] = 0;
	CHECK_FALSE(RelayLogFile::parse(unknown, log, &error));
}

TEST_CASE("p16 net: online START carries wide player rejoin and spectator tickets with explicit capacity")
{
	using namespace RelayProtocol;
	const ProfileIdentity own = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 8;
	limits.colorCount = 16;
	for (int capacity : { 9, 16 })
	{
		CAPTURE(capacity);
		std::string slots;
		for (int i = 0; i < 16; ++i)
		{
			if (i) slots += ",";
			slots += i < capacity ? "{\"state\":\"player\",\"name\":\"Player\",\"faction\":0,\"color\":" + std::to_string(i)
				+ ",\"startPos\":" + std::to_string(i) + ",\"team\":-1}" : "{\"state\":\"closed\"}";
		}
		const auto json = [&](const Ticket &ticket, OnlineStart::Kind kind, int count, int rejoin = 7, int delay = 60) {
			const bool replay = kind == OnlineStart::Kind::ReplayStart;
			const bool spectator = kind == OnlineStart::Kind::Spectate;
			const auto bytes = spectator ? signSpectatorTicket(ticket, testKey()) : ticket.generation ? signRejoinTicket(ticket, testKey()) : signTicket(ticket, testKey());
			std::string s = "{\"v\":1,\"kind\":\"" + std::string(replay ? "openbfme-replay-start" : spectator ? "openbfme-spectate" : "openbfme-start")
				+ "\",\"game\":\"0123456789abcdef\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + own.digest + "\"";
			if (!replay)
			{
				s += ",\"relay\":\"127.0.0.1:7350\",\"ticket\":\"" + base64UrlEncode(bytes.data(), bytes.size()) + "\"";
				s += spectator ? ",\"spectator\":3,\"delay\":" + std::to_string(delay) : ",\"slot\":" + std::to_string(capacity - 1);
				if (ticket.generation && !spectator) s += ",\"rejoin\":" + std::to_string(rejoin);
			}
			s += ",\"setup\":{\"map\":\"maps/p16_fixture/p16_fixture.map\",\"seed\":1,\"startingCash\":1000,\"superweaponRestriction\":0,\"rules\":null,\"legendaryHeroes\":1";
			if (count) s += ",\"mapStartCount\":" + std::to_string(count);
			return s + ",\"slots\":[" + slots + "]}}";
		};
		Ticket ticket = testTicket((std::uint8_t)(capacity - 1));
		ticket.slotMask = (std::uint16_t)((1u << capacity) - 1);
		LobbyStart parsed;
		std::string error;
		REQUIRE_MESSAGE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, capacity), own, limits, parsed, &error), error);
		CHECK(parsed.ticket[0] == 4);
		CHECK(parsed.localSlot == capacity - 1);
		CHECK(parsed.game.game.mapStartCount == capacity);
		CHECK(parsed.networkSlot[(size_t)capacity - 1]);
		CHECK(parsed.game.game.slots[capacity - 1].startPos == capacity - 1);
		CHECK_FALSE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, 0), own, limits, parsed, &error));
		CHECK_FALSE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, capacity - 1), own, limits, parsed, &error));
		CHECK_FALSE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, 17), own, limits, parsed, &error));
		Ticket badMask = ticket;
		badMask.slotMask ^= 1;
		CHECK_FALSE(OnlineStart::parse(json(badMask, OnlineStart::Kind::Start, capacity), own, limits, parsed, &error));
		CHECK(error.find("slot mask") != std::string::npos);
		ticket.generation = 7;
		REQUIRE_MESSAGE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, capacity), own, limits, parsed, &error), error);
		CHECK(parsed.ticket[0] == 5);
		CHECK(parsed.rejoinGeneration == 7);
		CHECK_FALSE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Start, capacity, 8), own, limits, parsed, &error));
		ticket.generation = 0;
		ticket.slot = 3;
		ticket.delaySeconds = 60;
		REQUIRE_MESSAGE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Spectate, capacity), own, limits, parsed, &error, OnlineStart::Kind::Spectate), error);
		CHECK(parsed.ticket[0] == 6);
		CHECK(parsed.spectatorIndex == 3);
		CHECK(parsed.spectatorDelay == 60);
		CHECK(parsed.localSlot == -1);
		CHECK_FALSE(OnlineStart::parse(json(ticket, OnlineStart::Kind::Spectate, capacity, 7, 59), own, limits, parsed, &error, OnlineStart::Kind::Spectate));
		REQUIRE_MESSAGE(OnlineStart::parse(json(ticket, OnlineStart::Kind::ReplayStart, capacity), own, limits, parsed, &error, OnlineStart::Kind::ReplayStart), error);
		CHECK(parsed.ticket.empty());
		CHECK(parsed.networkSlot[(size_t)capacity - 1]);
		CHECK(parsed.game.game.mapStartCount == capacity);
	}
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
	CHECK(error.find("different OpenBFME version or different game files") != std::string::npos); // lane JOINVER-1: plain words, no ids
	CHECK(error.find("00000000000000000000000000000000") == std::string::npos);
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
		return onlinetest::waitUntil([&] {
			t.service();
			return relaySocket.receiveFrom(from, d) && d.size() >= kHeaderBytes && d[3] == HELLO;
		});
	};
	std::vector<std::uint8_t> d;
	REQUIRE(next(d));
	CHECK(d == encodeHello(o.relay.ticket, {}, CAP_CANONICAL)); // the first HELLO: the ticket and the capability bits
	std::array<std::uint8_t, kSessionTokenBytes> token{};
	for (size_t i = 0; i < token.size(); ++i)
	{
		token[i] = (std::uint8_t)(0xA0 + i);
	}
	const std::vector<std::uint8_t> welcome = encodeWelcome(0x0123456789abcdefull, 0, token), notBound = encodeEmpty(NOT_BOUND);
	relaySocket.sendTo(client.localAddress(), welcome.data(), welcome.size());
	REQUIRE(onlinetest::waitUntil([&] {
		t.service();
		return t.relayBound();
	}));
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
	CHECK(d == encodeHello(o.relay.ticket, std::vector<std::uint8_t>(token.begin(), token.end()), CAP_CANONICAL));
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
	REQUIRE(onlinetest::waitUntil([&] {
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
		// Delivery may finish before a keep-alive or resend reaches a seeded loss draw. Observe loss on every endpoint too.
		return done() && std::all_of(t.begin(), t.end(), [](const auto &tr) { return tr->stats().dropped > 0; });
	}));
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
	REQUIRE(onlinetest::waitUntil([&] {
		for (auto &tr : t)
		{
			tr->service();
		}
		pumpRelay();
		for (auto &tr : t)
		{
			for (int peer = 0; peer < 3; ++peer)
			{
				if (!tr->allAcked(peer))
				{
					return false;
				}
			}
		}
		return true;
	}));
	t.clear();
	REQUIRE(onlinetest::waitUntil([&] {
		pumpRelay();
		return relay.openGames() == 0;
	}));
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

TEST_CASE("ROUTER-1: a canonical game's log assembles the committed frames: orders as GameMessages, CRC hashes, a rejoin")
{
	// a synthetic canonical log: records of game 0x0123456789abcdef, mask 0b101, crc interval 2
	std::vector<std::uint8_t> b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	auto put32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			b.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	put32(2);
	put32(0x89abcdefu);
	put32(0x01234567u);
	b.push_back(0x05);
	put32(1700000000u);
	const std::uint64_t game = 0x0123456789abcdefull;
	std::uint64_t seq = 0;
	auto record = [&](RelayCommit::Kind kind, std::uint32_t frame, const std::vector<std::uint8_t> &payload) {
		RelayCommit::Record rec;
		rec.stamp.game = game;
		rec.stamp.epoch = 1;
		rec.stamp.sequence = ++seq;
		rec.stamp.frame = frame;
		rec.kind = kind;
		rec.payload = payload;
		std::vector<std::uint8_t> enc;
		std::string err;
		REQUIRE(RelayCommit::encode(rec, enc, err));
		b.push_back(6);
		put32((std::uint32_t)enc.size());
		b.insert(b.end(), enc.begin(), enc.end());
	};
	auto order = [&](GameMessageType type, int arg) {
		GameMessage m(type, 99); // the sender's player field is bound to its slot on assembly, like the relay binds it
		m.appendIntegerArgument(arg);
		if (type == MSG_LOGIC_CRC)
		{
			m.appendBooleanArgument(false);
		}
		NetByteWriter w;
		std::string err;
		REQUIRE(NetPacket::writeGameMessage(w, m, &err));
		return w.data();
	};
	RelayMembership::Snapshot snap;
	snap.slotMask = 0x05;
	snap.active = 0x05;
	snap.loaded = 0x05;
	{
		std::vector<std::uint8_t> p;
		std::string err;
		REQUIRE(RelayMembership::encode(snap, p, err));
		record(RelayCommit::Kind::Membership, 0, p);
	}
	{
		RelayFrameBundle::Frame f;
		f.orders.push_back({ 0, 1, order(MSG_LOGIC_CRC, 111) });
		f.orders.push_back({ 2, 1, order(MSG_DO_ATTACK_OBJECT, 7) });
		std::vector<std::uint8_t> p;
		std::string err;
		REQUIRE(RelayFrameBundle::encode(f, p, err));
		record(RelayCommit::Kind::Frame, 1, p);
	}
	snap.ticketGenerations[2] = 1; // slot 2 rejoined with a bumped generation
	{
		std::vector<std::uint8_t> p;
		std::string err;
		REQUIRE(RelayMembership::encode(snap, p, err));
		record(RelayCommit::Kind::Membership, 2, p);
	}
	{
		RelayFrameBundle::Frame f;
		f.orders.push_back({ 2, 9, order(MSG_LOGIC_CRC, 111) });
		std::vector<std::uint8_t> p;
		std::string err;
		REQUIRE(RelayFrameBundle::encode(f, p, err));
		record(RelayCommit::Kind::Frame, 2, p);
	}
	record(RelayCommit::Kind::End, 2, {});
	RelayLogFile log;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(b, log, &error), error);
	REQUIRE(log.canonical.size() == 5);
	NetworkConfig config;
	config.slotPlayerIndex = { { 3, -1, 5, -1, -1, -1, -1, -1 } };
	config.crcInterval = 2;
	RelayLog::Assembled a;
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, a, &error), error);
	REQUIRE(a.frames.count(1));
	const std::vector<GameMessage> &f1 = a.frames.at(1).messages();
	REQUIRE(f1.size() == 2);
	CHECK(f1[0].getPlayerIndex() == 3);
	CHECK(f1[1].getPlayerIndex() == 5);
	CHECK(a.frames.at(2).messages().size() == 1);
	CHECK(a.lastCompleteFrame == 2);
	CHECK(a.hashes.at(2) == 111u);
	CHECK(a.crcDisagreements == 0);
	CHECK(a.incarnations[2] == 2);
	bool rejoinLogged = false, dropLogged = false;
	for (const Network::MembershipEvent &e : a.membership)
	{
		rejoinLogged = rejoinLogged || (e.kind == Network::MembershipEvent::REJOIN && e.slot == 2);
		dropLogged = dropLogged || (e.kind == Network::MembershipEvent::DROP);
	}
	CHECK(rejoinLogged);
	CHECK_FALSE(dropLogged);
	// a frame record that does not decode is an error, never dropped
	b.clear();
	b = { 'O', 'B', 'F', 'M', 'E', 'R', 'L', 'G' };
	put32(2);
	put32(0x89abcdefu);
	put32(0x01234567u);
	b.push_back(0x05);
	put32(1700000000u);
	b.push_back(6);
	put32(3);
	b.insert(b.end(), { 1, 2, 3 });
	CHECK_FALSE(RelayLogFile::parse(b, log, &error));
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
	// lane DEVPORT-7: TestChild.h stops the relay with this scope, kills it with the test and keeps it off the test's output
	TestChild relay(OPENBFME_RELAY_EXE, { "--bind", bind, "--keys", keys, "--log-dir", logs, "--status", status, "--stats-s", "0" }, (dir / "relay.out").string());
	REQUIRE(relay.started());
	const std::vector<std::uint8_t> hello = RelayProtocol::encodeHello(ticketBytes(testTicket(0)));
	bool welcomed = false;
	REQUIRE(onlinetest::waitUntil([&] {
		probe.sendTo(NetAddress{ 0x7F000001u, port }, hello.data(), hello.size());
		NetAddress from;
		std::vector<std::uint8_t> d;
		while (probe.receiveFrom(from, d))
		{
			welcomed = welcomed || welcomeToken(d, 0).size() == RelayProtocol::kSessionTokenBytes;
		}
		return welcomed;
	}));
	CHECK(welcomed);
	const int status_ = relay.stop();
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

TEST_CASE("anticheat1 views: a VIEW on the log-only stream is logged by the relay, never forwarded, assembled apart from the frames and kept in the replay")
{
	using namespace RelayProtocol;
	NetCommandMsg v;
	v.type = NETCOMMANDTYPE_VIEW;
	v.slot = 0;
	v.executionFrame = 42;
	v.view = { 100.5f, 200.25f, 640.0f, 480.0f };
	v.viewFlags = 1;
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(v, &error);
	REQUIRE_MESSAGE(!bytes.empty(), error);
	NetCommandMsg back;
	REQUIRE(NetPacket::decodeCommand(bytes, back, &error));
	CHECK(back.type == NETCOMMANDTYPE_VIEW);
	CHECK(back.executionFrame == 42);
	CHECK(back.view[1] == 200.25f);
	CHECK(back.viewFlags == 1);
	// through the relay: slot 0's log-only stream (DATA to its own slot) carries a frame command, then the VIEW
	const std::filesystem::path dir = tempDir("views");
	RelayCore::Options o;
	o.keys = testRing();
	o.logDir = dir.string();
	RelayCore relay(o);
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
	send(addr(1), encodeHello(ticketBytes(testTicket(0, 0x03))));
	send(addr(2), encodeHello(ticketBytes(testTicket(1, 0x03))));
	RelayCore::Action a = send(addr(1), encodeData(0, transportPacket(0, 0, frameInfo(0, 41, 0))));
	CHECK(a.kind == RelayCore::Action::REPLY); // the relay's ack as slot 0
	a = send(addr(1), encodeData(0, transportPacket(0, 1, bytes)));
	CHECK(a.kind == RelayCore::Action::REPLY);
	CHECK(a.to == addr(1)); // acknowledged to the sender, sent to nobody else
	send(addr(1), encodeEmpty(BYE));
	send(addr(2), encodeEmpty(BYE));
	REQUIRE(relay.closedLogs().size() == 1);
	RelayLogFile log;
	REQUIRE_MESSAGE(RelayLogFile::load(relay.closedLogs().front(), log, &error), error);
	REQUIRE(log.commands.size() == 2);
	CHECK(log.commands[1].msg.type == NETCOMMANDTYPE_VIEW);
	NetworkConfig config;
	config.slotPlayerIndex = { { 3, 4, -1, -1, -1, -1, -1, -1 } };
	config.crcInterval = 0;
	RelayLog::Assembled as;
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, as, &error), error);
	REQUIRE(as.views.size() == 1);
	CHECK(as.views[0].frame == 42);
	CHECK(as.views[0].slot == 0);
	CHECK(as.views[0].cursorX == 640.0f);
	CHECK_FALSE(as.frames.count(42)); // never a frame entry
	// the replay keeps it (version 8 VIEW record)
	const std::string path = (dir / "views.replay").string();
	ReplayHeader h;
	h.profile = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	REQUIRE_MESSAGE(RelayLog::writeReplay(path, h, as, &error), error);
	ReplayFile rf;
	REQUIRE_MESSAGE(ReplayFile::load(path, rf, &error), error);
	REQUIRE(rf.views.size() == 1);
	CHECK(rf.views[0].frame == 42);
	CHECK(rf.views[0].cameraX == 100.5f);
	CHECK(rf.views[0].cursorY == 480.0f);
	CHECK(rf.views[0].flags == 1);
	std::filesystem::remove_all(dir);
}

TEST_CASE("onlinemenu1: the online START carries each slot's handicap (0, -5 .. -95; absent from an older lobby: 0); anything else refuses the START")
{
	const ProfileIdentity own = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	const std::string ticket = "AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI"; // slot 1, mask 0x07
	auto start = [&](const std::string &h0, const std::string &h1) {
		return "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0123456789abcdef\",\"slot\":1,\"relay\":\"192.0.2.10:7350\",\"ticket\":\"" + ticket +
			"\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + own.digest +
			"\",\"setup\":{\"map\":\"maps/map mp evendim/map mp evendim.map\",\"seed\":4242,\"startingCash\":2000,\"superweaponRestriction\":0,\"rules\":null,"
			"\"legendaryHeroes\":1,\"slots\":[{\"state\":\"player\",\"name\":\"Ann\",\"faction\":0,\"color\":0,\"startPos\":0,\"team\":0" + h0 + "},"
			"{\"state\":\"player\",\"name\":\"Bea\",\"faction\":1,\"color\":1,\"startPos\":1,\"team\":1" + h1 + "},"
			"{\"state\":\"player\",\"name\":\"Cy\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1},"
			"{\"state\":\"hard\",\"name\":\"AI\",\"faction\":2,\"color\":3,\"startPos\":3,\"team\":1,\"handicap\":-95}]}}";
	};
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 8;
	limits.colorCount = 8;
	LobbyStart s;
	std::string error;
	REQUIRE_MESSAGE(OnlineStart::parse(start("", ",\"handicap\":-15"), own, limits, s, &error), error);
	CHECK(s.game.game.slots[0].handicap == 0); // absent
	CHECK(s.game.game.slots[1].handicap == -15);
	CHECK(s.game.game.slots[2].handicap == 0);
	CHECK(s.game.game.slots[3].handicap == -95);
	REQUIRE_MESSAGE(OnlineStart::parse(start(",\"handicap\":0", ",\"handicap\":-5"), own, limits, s, &error), error);
	CHECK(s.game.game.slots[1].handicap == -5);
	for (const char *bad : { ",\"handicap\":-7", ",\"handicap\":5", ",\"handicap\":-100", ",\"handicap\":\"-10\"", ",\"handicap\":-10.5", ",\"handicap\":null" })
	{
		CHECK_MESSAGE(!OnlineStart::parse(start("", bad), own, limits, s, &error), bad);
		CHECK_MESSAGE(error.find("handicap") != std::string::npos, bad << ": " << error);
	}
}
