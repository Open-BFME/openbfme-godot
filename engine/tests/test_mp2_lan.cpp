// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: the LAN lobby (GameNetwork/LANAPI.h) between three instances on localhost UDP sockets: the host's game appears in the others' lists, two
// players join and get slots, a player's own options go through the host (a colour another slot holds is refused), chat reaches the game, the host
// kicks and the kicked player rejoins, the start waits for every accept and gives every player the same game with its own slot and the host's game
// socket, another profile identity is refused at the join, and a player who stops answering is dropped.

#include "doctest.h"

#include "GameNetwork/LANAPI.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/ProfileChoice.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
ProfileIdentity profileFor(const std::string &feature)
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	return ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, feature.empty() ? std::vector<std::string>{} : std::vector<std::string>{ feature });
}

std::uint16_t testPortBase()
{
#ifdef _WIN32
	const int pid = 1234;
#else
	const int pid = (int)getpid();
#endif
	return (std::uint16_t)(40000 + (pid % 1000) * 16);
}

struct Lan
{
	std::vector<std::unique_ptr<LANAPI>> peers;
	std::uint64_t clock = 1000;
	std::vector<std::vector<LANAPI::Event>> events;
	Lan(const std::vector<std::pair<std::u16string, std::string>> &who)
	{
		LANAPI::Options o;
		o.playerTemplateCount = 16; // lane MP-2 (review): the ranges the host checks requests against
		o.colorCount = 10;
		o.lobbyPortBase = testPortBase();
		o.lobbyPorts = 8;
		o.broadcast = false;
		o.extraTargets = { 0x7F000001u };
		o.resendMs = 300;
		o.actionTimeoutMs = 2000;
		// lane HERO-2: the test's Create-a-Hero check (the game's is CreateAHeroSystem::validateHero): class 0 .. 5, a unique id other than "REFUSED"
		o.validateCreateAHero = [](const CreateAHeroHero &h, std::string *why) {
			if (h.classIndex <= 5 && h.uniqueID != "REFUSED")
			{
				return true;
			}
			*why = "refused by the test's validator";
			return false;
		};
		for (const auto &w : who)
		{
			peers.push_back(std::make_unique<LANAPI>(profileFor(w.second), w.first, o));
			std::string error;
			REQUIRE_MESSAGE(peers.back()->open(&error), error);
		}
		events.resize(peers.size());
	}
	void run(int ticks, const std::vector<bool> &active = {})
	{
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			for (size_t i = 0; i < peers.size(); ++i)
			{
				if (!active.empty() && !active[i])
				{
					continue;
				}
				peers[i]->update(clock);
				for (LANAPI::Event &e : peers[i]->takeEvents())
				{
					events[i].push_back(e);
				}
			}
			NetSleepMilliseconds(1);
		}
	}
	bool saw(size_t peer, LANAPI::Event::Kind k) const
	{
		for (const LANAPI::Event &e : events[peer])
		{
			if (e.kind == k)
			{
				return true;
			}
		}
		return false;
	}
	const LANAPI::Event *last(size_t peer, LANAPI::Event::Kind k) const
	{
		for (auto it = events[peer].rbegin(); it != events[peer].rend(); ++it)
		{
			if (it->kind == k)
			{
				return &*it;
			}
		}
		return nullptr;
	}
};

// lane HERO-2: a record built here (no retail bytes): every field set
CreateAHeroHero testHero(const std::string &uniqueID)
{
	CreateAHeroHero h;
	h.objectID = 0;
	h.name = u"Lanhero";
	h.classIndex = 3;
	h.subClassIndex = 1;
	h.primaryColor = 0xFF102030u;
	h.secondaryColor = 0xFF405060u;
	h.tertiaryColor = 0xFF708090u;
	h.powers[0] = { "Command_TestPowerA", 0, 1 };
	h.powers[1] = { "Command_TestPowerB", 3, 2 };
	h.powers[9] = { "Command_TestPowerA_Level2", 9, 1 };
	h.bling = { { "CreateAHero_Weapon", 1 }, { "CreateAHero_Helmet", 2 } };
	h.uniqueID = uniqueID;
	h.isSystemHero = false;
	CreateAHeroHero loaded; // the record as a .cah load gives it (the checksum, valid, the load flags)
	std::string error;
	REQUIRE_MESSAGE(loaded.load(h.save(), &error), error);
	return loaded;
}

SkirmishGameInfo hostedGame()
{
	SkirmishGameInfo g;
	g.mapName = "maps/map mp evendim/map mp evendim.map";
	g.startingCash = 1500;
	g.slots[1].state = SLOT_OPEN;
	g.slots[2].state = SLOT_OPEN;
	g.slots[3].state = SLOT_MED_AI;
	g.slots[3].name = u"Computer";
	return g;
}
} // namespace

TEST_CASE("mp2 lan: discovery identifies a process across addresses, not games by name")
{
	Lan lan({ { u"Observer", "" } });
	LANAPI &observer = *lan.peers[0];
	UDP first, alias;
	std::string error;
	REQUIRE_MESSAGE(first.bind(0x7F000001u, 0, &error), error);
#ifdef __APPLE__
	// DEVPORT-6: macOS's lo0 answers 127.0.0.1 only (a second loopback address needs an administrator's `ifconfig lo0 alias`); the
	// discovery logic is the same C++ on every platform and runs on Linux and Windows. Everywhere else a failed bind fails the test.
	if (!alias.bind(0x7F000002u, first.localAddress().port, &error))
	{
		MESSAGE("SKIP LOUDLY: mp2 lan alias discovery: this Mac has no 127.0.0.2 (" << error << ")");
		return;
	}
#else
	REQUIRE_MESSAGE(alias.bind(0x7F000002u, first.localAddress().port, &error), error);
#endif
	auto announce = [&](UDP &socket, std::uint32_t nonce, std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion); // PUBLICCI-1: was 2, left behind when OWNERFIX-2 moved the protocol to 3 (refused by version)
		w.u8(type);
		w.u32(nonce);
		w.wstr(u"Same name");
		if (type == LANMessageType::GAME_ANNOUNCE)
		{
			w.wstr(u"Same name");
			w.u8(0);
			w.str(profileFor("").digest);
			NetPacket::writeGameInfo(w, hostedGame());
		}
		REQUIRE(socket.sendTo(NetAddress{ 0x7F000001u, observer.lobbyPort() }, w.data().data(), w.data().size()));
		lan.run(10);
	};
	announce(first, 71, LANMessageType::GAME_ANNOUNCE);
	announce(alias, 71, LANMessageType::GAME_ANNOUNCE);
	REQUIRE(observer.games().size() == 1);
	CHECK(observer.games()[0].host == first.localAddress());
	announce(alias, 72, LANMessageType::GAME_ANNOUNCE);
	CHECK(observer.games().size() == 2);
	announce(first, 71, LANMessageType::LOBBY_ANNOUNCE);
	announce(alias, 71, LANMessageType::LOBBY_ANNOUNCE);
	CHECK(observer.lobbyPlayers().size() == 1);
	announce(alias, 71, LANMessageType::REQUEST_LOBBY_LEAVE);
	CHECK(observer.lobbyPlayers().empty());
}

#ifdef _WIN32
TEST_CASE("mp2 lan: Windows UDP port-unreachable errors do not hide a queued reply")
{
	UDP receiver, sender;
	std::string error;
	REQUIRE(receiver.bind(0x7F000001u, 0, &error));
	REQUIRE(sender.bind(0x7F000001u, 0, &error));
	const std::vector<std::uint8_t> payload{ 1, 2, 3 };
	for (int i = 0; i < 6; ++i)
	{
		NetAddress closed;
		{
			UDP reserve;
			REQUIRE(reserve.bind(0x7F000001u, 0, &error));
			closed = reserve.localAddress();
		}
		REQUIRE(receiver.sendTo(closed, payload.data(), payload.size()));
		NetSleepMilliseconds(20);
	}
	REQUIRE(sender.sendTo(receiver.localAddress(), payload.data(), payload.size()));
	NetSleepMilliseconds(100);
	NetAddress from;
	std::vector<std::uint8_t> received;
	REQUIRE(receiver.receiveFrom(from, received));
	CHECK(from == sender.localAddress());
	CHECK(received == payload);
	CHECK_FALSE(receiver.receiveFrom(from, received));
}
#endif

TEST_CASE("p16 net: LAN slot requests use the advertised map capacity")
{
	for (int capacity : { 8, 9, 16 })
	{
		CAPTURE(capacity);
		Lan lan({ { u"Host", "" }, { u"Alice", "" } });
		LANAPI &host = *lan.peers[0];
		LANAPI &alice = *lan.peers[1];
		SkirmishGameInfo game = hostedGame();
		game.mapName = "p16-generated-capacity";
		game.mapStartCount = capacity;
		host.requestGameCreate(game);
		alice.requestLocations();
		lan.run(10);
		REQUIRE(alice.games().size() == 1);
		CHECK(alice.games()[0].info.mapStartCount == capacity);
		alice.requestGameJoin(0);
		lan.run(10);
		REQUIRE(alice.currentGame());
		const int lastTeam = capacity > 8 ? 7 : 3;
		alice.requestSlotOptions(2, 3, lastTeam, capacity - 1);
		lan.run(10);
		CHECK(host.currentGame()->info.slots[1].teamNumber == lastTeam);
		CHECK(host.currentGame()->info.slots[1].startPos == capacity - 1);
		CHECK(alice.currentGame()->info.slots[1].teamNumber == lastTeam);
		CHECK(alice.currentGame()->info.slots[1].startPos == capacity - 1);
		REQUIRE(host.errors().empty());
		REQUIRE(alice.errors().empty());
		alice.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, lastTeam + 1, LANAPI::kKeep);
		lan.run(10);
		CHECK(host.errors().size() == 1);
		alice.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, capacity);
		lan.run(10);
		CHECK(host.errors().size() == 2);
		CHECK(host.currentGame()->info.slots[1].teamNumber == lastTeam);
		CHECK(host.currentGame()->info.slots[1].startPos == capacity - 1);
	}
}

TEST_CASE("p16 net: LAN refuses oversized START without entering the running state")
{
	for (int capacity : { 8, 16 })
	{
		CAPTURE(capacity);
		Lan lan({ { u"Host", "" } });
		LANAPI &host = *lan.peers[0];
		SkirmishGameInfo game;
		game.mapStartCount = capacity;
		game.mapName.assign(65500, 'x'); // legal wire string length; the full datagram exceeds IPv4's payload limit
		host.requestGameCreate(game);
		REQUIRE(host.currentGame());
		REQUIRE_FALSE(host.errors().empty());
		CHECK(host.errors().back().find("IPv4 UDP payload limit") != std::string::npos);
		std::string reason;
		CHECK_FALSE(host.hostStartGame(&reason));
		CHECK(reason.find("IPv4 UDP payload limit") != std::string::npos);
		CHECK_FALSE(host.currentGame()->inProgress);
		CHECK_FALSE(host.started());
		game.mapName = "p16-generated-capacity";
		REQUIRE(host.hostSetOptions(game));
		REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
		lan.run(2);
		CHECK(host.started());
	}
}

TEST_CASE("p16 net: sixteen LAN lobby peers receive the same START over real loopback UDP")
{
	LANAPI::Options options;
	options.playerTemplateCount = 16;
	options.colorCount = 16;
	options.lobbyPortBase = testPortBase();
	options.lobbyPorts = 16;
	options.broadcast = false;
	options.extraTargets = { 0x7F000001u };
	std::vector<std::unique_ptr<LANAPI>> peers;
	std::string error;
	for (int i = 0; i < 16; ++i)
	{
		const std::string name = "Player" + std::to_string(i);
		peers.push_back(std::make_unique<LANAPI>(profileFor(""), std::u16string(name.begin(), name.end()), options));
		REQUIRE_MESSAGE(peers.back()->open(&error), error);
	}
	std::uint64_t clock = 1000;
	auto advance = [&]() {
		for (int tick = 0; tick < 20; ++tick)
		{
			clock += 20;
			for (auto &peer : peers) peer->update(clock);
			NetSleepMilliseconds(1);
		}
	};
	SkirmishGameInfo game;
	game.mapName = "p16-generated-capacity";
	game.mapStartCount = 16;
	for (auto &slot : game.slots) slot.state = SLOT_OPEN;
	peers[0]->requestGameCreate(game);
	advance();
	for (int i = 1; i < 16; ++i)
	{
		REQUIRE(peers[(size_t)i]->games().size() == 1);
		CHECK(peers[(size_t)i]->games()[0].info.mapStartCount == 16);
		peers[(size_t)i]->requestGameJoin(0);
		advance();
		REQUIRE(peers[(size_t)i]->currentGame());
		CHECK(peers[(size_t)i]->localSlot() == i);
	}
	for (int i = 1; i < 16; ++i)
	{
		peers[(size_t)i]->requestSlotOptions(i, i, i % 8, i);
		advance();
	}
	for (int i = 1; i < 16; ++i) peers[(size_t)i]->requestAccept(true);
	advance();
	REQUIRE_MESSAGE(peers[0]->hostStartGame(&error), error);
	advance();
	NetByteWriter expected;
	NetPacket::writeNewGameMessage(expected, peers[0]->start().game);
	for (int i = 0; i < 16; ++i)
	{
		CAPTURE(i);
		const auto &peer = *peers[(size_t)i];
		CHECK(peer.errors().empty());
		REQUIRE(peer.started());
		CHECK(peer.start().localSlot == i);
		CHECK(peer.start().game.game.mapStartCount == 16);
		CHECK(peer.start().game.game.slots[15].startPos == 15);
		CHECK(peer.start().game.game.slots[15].color == 15);
		CHECK(std::all_of(peer.start().networkSlot.begin(), peer.start().networkSlot.end(), [](bool used) { return used; }));
		NetByteWriter received;
		NetPacket::writeNewGameMessage(received, peer.start().game);
		CHECK(received.data() == expected.data());
		for (int s = 0; s < 16; ++s)
		{
			CHECK(peer.start().addresses[(size_t)s].port == peers[(size_t)s]->gameSocket().localAddress().port);
		}
	}
}

TEST_CASE("mp2 lan: find, join, options through the host, chat, kick and rejoin, accept, start: every player gets the same game")
{
	Lan lan({ { u"Host", "" }, { u"Alice", "" }, { u"Bob", "" } });
	LANAPI &host = *lan.peers[0];
	LANAPI &alice = *lan.peers[1];
	LANAPI &bob = *lan.peers[2];
	CHECK(host.lobbyPort() == testPortBase()); // RW 0x84C881: the first free port of the range
	CHECK(alice.lobbyPort() == testPortBase() + 1);
	lan.run(10);
	CHECK(alice.lobbyPlayers().size() == 2); // the others' LOBBY_ANNOUNCE
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	lan.run(10);
	REQUIRE(alice.games().size() == 1);
	CHECK(alice.games()[0].name == u"Host");
	CHECK(alice.games()[0].info.slots[3].state == SLOT_MED_AI);
	REQUIRE(bob.games().size() == 1);
	// two joins
	alice.requestGameJoin(0);
	lan.run(10);
	bob.requestGameJoin(0);
	lan.run(15);
	REQUIRE(alice.currentGame());
	REQUIRE(bob.currentGame());
	CHECK(alice.localSlot() == 1);
	CHECK(bob.localSlot() == 2);
	CHECK(lan.last(1, LANAPI::Event::GameJoin)->ret == LANAPI::RET_OK);
	CHECK(host.currentGame()->info.slots[1].name == u"Alice");
	CHECK(bob.currentGame()->info.slots[1].name == u"Alice");
	CHECK(bob.currentGame()->info.slots[0].name == u"Host");
	// Alice's own options through the host; Bob asks for the colour Alice holds: refused
	alice.requestSlotOptions(2, 3, 1, 1);
	lan.run(10);
	CHECK(bob.currentGame()->info.slots[1].playerTemplate == 2);
	CHECK(bob.currentGame()->info.slots[1].color == 3);
	CHECK(bob.currentGame()->info.slots[1].teamNumber == 1);
	CHECK(bob.currentGame()->info.slots[1].startPos == 1);
	bob.requestSlotOptions(LANAPI::kKeep, 3, LANAPI::kKeep, 1);
	lan.run(10);
	CHECK(host.currentGame()->info.slots[2].color == -1);
	CHECK(host.currentGame()->info.slots[2].startPos == -1);
	// the host's own change of an AI slot reaches everybody
	SkirmishGameInfo info = host.currentGame()->info;
	info.slots[3].state = SLOT_HARD_AI;
	CHECK(host.hostSetOptions(info));
	lan.run(10);
	CHECK(alice.currentGame()->info.slots[3].state == SLOT_HARD_AI);
	// chat
	bob.requestChat(u"hello there");
	lan.run(10);
	const LANAPI::Event *chat = lan.last(1, LANAPI::Event::Chat);
	REQUIRE(chat);
	CHECK(chat->name == u"Bob");
	CHECK(chat->text == u"hello there");
	REQUIRE(lan.last(0, LANAPI::Event::Chat));
	// the host kicks Bob; Bob is back in the lobby and joins again
	CHECK(host.hostKick(2));
	lan.run(10);
	CHECK(lan.saw(2, LANAPI::Event::Kicked));
	CHECK(bob.inLobby());
	CHECK(alice.currentGame()->info.slots[2].state == SLOT_OPEN);
	bob.requestGameJoin(0);
	lan.run(15);
	REQUIRE(bob.currentGame());
	CHECK(bob.localSlot() == 2);
	// the start waits for every accept
	std::string reason;
	CHECK(!host.hostStartGame(&reason));
	CHECK(reason.find("has not accepted") != std::string::npos);
	alice.requestAccept(true);
	bob.requestAccept(true);
	lan.run(10);
	CHECK(host.currentGame()->accepted[1]);
	CHECK(host.currentGame()->accepted[2]);
	REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
	lan.run(20);
	REQUIRE(host.started());
	REQUIRE(alice.started());
	REQUIRE(bob.started());
	CHECK(lan.saw(0, LANAPI::Event::GameStart));
	CHECK(alice.start().game.game == host.start().game.game);
	CHECK(bob.start().game.game == host.start().game.game);
	CHECK(host.start().localSlot == 0);
	CHECK(alice.start().localSlot == 1);
	CHECK(bob.start().localSlot == 2);
	CHECK(alice.start().networkSlot[0]);
	CHECK(alice.start().networkSlot[2]);
	CHECK(!alice.start().networkSlot[3]); // the AI
	CHECK(alice.start().addresses[0].ip == 0x7F000001u); // the host's game socket, at the address the host was heard from
	CHECK(alice.start().addresses[0].port == host.gameSocket().localAddress().port);
	CHECK(alice.start().addresses[2].port == bob.gameSocket().localAddress().port);
	CHECK(host.start().game.game.seed != 0);
	CHECK(host.errors().empty());
	CHECK(alice.errors().empty());
	CHECK(bob.errors().empty());
}

TEST_CASE("mp2 lan: another profile identity is refused at the join; a player who stops answering is dropped by the host")
{
	Lan lan({ { u"Host", "" }, { u"Modder", "test-feature" }, { u"Carol", "" } });
	LANAPI &host = *lan.peers[0];
	host.requestGameCreate(hostedGame());
	lan.run(10);
	REQUIRE(lan.peers[1]->games().size() == 1);
	CHECK(lan.peers[1]->games()[0].profileDigest != profileFor("test-feature").digest); // the list shows another profile's game
	lan.peers[1]->requestGameJoin(0);
	lan.run(10);
	REQUIRE(lan.last(1, LANAPI::Event::GameJoin));
	CHECK(lan.last(1, LANAPI::Event::GameJoin)->ret == LANAPI::RET_CRC_MISMATCH);
	CHECK(lan.peers[1]->inLobby());
	lan.peers[2]->requestGameJoin(0);
	lan.run(10);
	REQUIRE(lan.peers[2]->currentGame());
	// Carol goes silent: after 8 resend periods (2.4 s) the host drops her (ZH LANAPI::update, "LAN:PlayerDropped")
	lan.run(150, { true, true, false });
	CHECK(host.currentGame()->info.slots[1].state == SLOT_OPEN);
	REQUIRE(lan.last(0, LANAPI::Event::PlayerLeave));
	CHECK(lan.last(0, LANAPI::Event::PlayerLeave)->name == u"Carol");
}

TEST_CASE("mp2 lan (review): a truncated or oversized lobby message changes nothing and is an error: the game leave, the accept, the slot request, the chat, "
		  "the join request")
{
	LANAPI::Options o;
	o.playerTemplateCount = 16; // lane MP-2 (review): the ranges the host checks requests against
	o.colorCount = 10;
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 8);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	const ProfileIdentity profile = profileFor("");
	LANAPI host(profile, u"Host", o);
	std::string error;
	REQUIRE_MESSAGE(host.open(&error), error);
	host.requestGameCreate(hostedGame());
	UDP attacker;
	REQUIRE_MESSAGE(attacker.bind(0, 0, &error), error);
	const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
	std::uint64_t clock = 1000;
	auto header = [](std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C); // the lobby's magic and version
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(77);
		w.wstr(u"Mallory");
		return w;
	};
	auto deliver = [&](const NetByteWriter &w) {
		attacker.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			host.update(++clock);
		}
	};
	// a well-formed join: the attacker holds slot 1
	NetByteWriter join = header(LANMessageType::REQUEST_JOIN);
	join.str(profile.digest);
	join.u16(attacker.localAddress().port);
	deliver(join);
	REQUIRE(host.currentGame());
	REQUIRE(host.currentGame()->info.slots[1].isHuman());
	const int humans = host.currentGame()->humanSlots();
	size_t errors = host.errors().size();
	auto expectRefused = [&](const NetByteWriter &w, const char *what) {
		deliver(w);
		INFO(what);
		CHECK(host.currentGame()->humanSlots() == humans);
		CHECK(host.currentGame()->info.slots[1].isHuman());
		CHECK(!host.currentGame()->accepted[1]);
		CHECK(host.currentGame()->info.slots[1].playerTemplate == -1);
		CHECK(host.errors().size() == errors + 1);
		errors = host.errors().size();
	};
	expectRefused(header(LANMessageType::REQUEST_GAME_LEAVE), "a leave without its flag");
	NetByteWriter leave = header(LANMessageType::REQUEST_GAME_LEAVE);
	leave.u8(0);
	leave.u8(123);
	expectRefused(leave, "a leave with a trailing byte");
	NetByteWriter leaveFlag = header(LANMessageType::REQUEST_GAME_LEAVE);
	leaveFlag.u8(7);
	expectRefused(leaveFlag, "a leave whose flag is not 0 / 1");
	NetByteWriter accept = header(LANMessageType::SET_ACCEPT);
	accept.u8(1);
	accept.u8(0);
	expectRefused(accept, "an accept with a trailing byte");
	expectRefused(header(LANMessageType::SET_ACCEPT), "an accept without its flag");
	NetByteWriter slot = header(LANMessageType::GAME_OPTIONS);
	slot.u8(1); // OPT_SLOT_REQUEST
	slot.u32(2);
	slot.u32((std::uint32_t)LANAPI::kKeep);
	slot.u32((std::uint32_t)LANAPI::kKeep);
	expectRefused(slot, "a slot request without its start position");
	NetByteWriter slotLong = header(LANMessageType::GAME_OPTIONS);
	slotLong.u8(1);
	for (int i = 0; i < 4; ++i)
	{
		slotLong.u32(i == 0 ? 2u : (std::uint32_t)LANAPI::kKeep);
	}
	slotLong.u8(9);
	expectRefused(slotLong, "a slot request with a trailing byte");
	NetByteWriter chat = header(LANMessageType::CHAT);
	chat.wstr(u"hi");
	expectRefused(chat, "a chat without its system flag");
	NetByteWriter join2 = header(LANMessageType::REQUEST_JOIN);
	join2.str(profile.digest);
	join2.u16(1);
	join2.u8(0);
	expectRefused(join2, "a join request with a trailing byte");
	// the well-formed leave still works
	NetByteWriter goodLeave = header(LANMessageType::REQUEST_GAME_LEAVE);
	goodLeave.u8(0);
	deliver(goodLeave);
	CHECK(host.currentGame()->info.slots[1].state == SLOT_OPEN);
	CHECK(host.errors().size() == errors);
}

TEST_CASE("mp2 lan (review): a flag byte other than 0 / 1 is an error in every message kind that carries one, and nothing is applied: the accept, the leave, "
		  "the chat, the game announcement, a nested GameInfo, the full game options, the START's network slots; a join deny of an unknown reason")
{
	LANAPI::Options o;
	o.playerTemplateCount = 16; // lane MP-2 (review): the ranges the host checks requests against
	o.colorCount = 10;
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 16 < 65000 ? testPortBase() + 16 : testPortBase() - 16);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	const ProfileIdentity profile = profileFor("");
	std::string error;
	auto header = [](std::uint8_t type, const std::u16string &name) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(77);
		w.wstr(name);
		return w;
	};
	std::uint64_t clock = 1000;
	UDP fake;
	REQUIRE_MESSAGE(fake.bind(0, 0, &error), error);
	const NetAddress fakeAddress{ 0x7F000001u, fake.localAddress().port };
	// ---- a host receiving a joined player's messages
	LANAPI host(profile, u"Host", o);
	REQUIRE_MESSAGE(host.open(&error), error);
	host.requestGameCreate(hostedGame());
	auto toHost = [&](const NetByteWriter &w) {
		const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
		fake.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			host.update(++clock);
		}
	};
	NetByteWriter join = header(LANMessageType::REQUEST_JOIN, u"Mallory");
	join.str(profile.digest);
	join.u16(fake.localAddress().port);
	toHost(join);
	REQUIRE(host.currentGame());
	REQUIRE(host.currentGame()->info.slots[1].isHuman());
	host.takeEvents();
	size_t errors = host.errors().size();
	auto refusedByHost = [&](const NetByteWriter &w, const char *what) {
		toHost(w);
		INFO(what);
		CHECK(host.errors().size() == errors + 1);
		errors = host.errors().size();
		CHECK(host.currentGame()->info.slots[1].isHuman());
		CHECK(!host.currentGame()->accepted[1]);
		for (const LANAPI::Event &e : host.takeEvents())
		{
			CHECK_MESSAGE(e.kind != LANAPI::Event::Chat, what);
			CHECK_MESSAGE(e.kind != LANAPI::Event::Accept, what);
			CHECK_MESSAGE(e.kind != LANAPI::Event::PlayerLeave, what);
		}
	};
	NetByteWriter accept = header(LANMessageType::SET_ACCEPT, u"Mallory");
	accept.u8(7);
	refusedByHost(accept, "accept flag 7");
	NetByteWriter leave = header(LANMessageType::REQUEST_GAME_LEAVE, u"Mallory");
	leave.u8(7);
	refusedByHost(leave, "leave flag 7");
	NetByteWriter chat = header(LANMessageType::CHAT, u"Mallory");
	chat.wstr(u"hello");
	chat.u8(7);
	refusedByHost(chat, "chat system flag 7");
	// ---- a lobby player receiving a host's messages
	LANAPI client(profile, u"Client", o);
	REQUIRE_MESSAGE(client.open(&error), error);
	auto toClient = [&](const NetByteWriter &w) {
		const NetAddress dst{ 0x7F000001u, client.lobbyPort() };
		fake.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			client.update(++clock);
		}
	};
	SkirmishGameInfo g = hostedGame();
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"Fake";
	g.slots[1].state = SLOT_PLAYER;
	g.slots[1].name = u"Client";
	size_t cerrors = client.errors().size();
	auto refusedByClient = [&](const NetByteWriter &w, const char *what) {
		toClient(w);
		INFO(what);
		CHECK(client.errors().size() == cerrors + 1);
		cerrors = client.errors().size();
	};
	NetByteWriter announce = header(LANMessageType::GAME_ANNOUNCE, u"Fake");
	announce.wstr(u"Fake");
	announce.u8(7); // in progress
	announce.str(profile.digest);
	NetPacket::writeGameInfo(announce, g);
	refusedByClient(announce, "announcement in-progress flag 7");
	CHECK(client.games().empty());
	// a GameInfo whose slot carries accepted = 7: write it, then patch the byte after slot 0's name
	NetByteWriter info;
	NetPacket::writeGameInfo(info, g);
	std::vector<std::uint8_t> patched = info.data();
	const size_t acceptedAt = 4 + 4 + 2 * g.slots[0].name.size(); // i32 state, u32 name length, UTF-16 units (NetByteWriter::wstr)
	REQUIRE(patched.size() > acceptedAt);
	patched[acceptedAt] = 7;
	NetByteWriter nested = header(LANMessageType::GAME_ANNOUNCE, u"Fake");
	nested.wstr(u"Fake");
	nested.u8(0);
	nested.str(profile.digest);
	for (std::uint8_t b : patched)
	{
		nested.u8(b);
	}
	refusedByClient(nested, "nested GameInfo accepted flag 7");
	CHECK(client.games().empty());
	// the full options of the joined game with an accepted flag 7
	client.requestGameJoinDirect(fakeAddress);
	NetByteWriter acceptJoin = header(LANMessageType::JOIN_ACCEPT, u"Fake");
	acceptJoin.u8(1);
	toClient(acceptJoin);
	REQUIRE(client.currentGame());
	NetByteWriter options = header(LANMessageType::GAME_OPTIONS, u"Fake");
	options.u8(0); // OPT_FULL
	options.wstr(u"Fake");
	NetPacket::writeGameInfo(options, g);
	for (int s = 0; s < g.serializedSlotCount(); ++s)
	{
		options.u32(fakeAddress.ip);
		options.u16(fakeAddress.port);
		options.u32(fakeAddress.ip);
		options.u16(fakeAddress.port);
		options.u8(s == 1 ? 7 : 0);
	}
	refusedByClient(options, "game options accepted flag 7");
	CHECK(!client.currentGame()->accepted[1]);
	// a join deny of an unknown reason (a fresh join attempt to the fake host)
	client.requestGameLeave();
	toClient(header(LANMessageType::REQUEST_LOCATIONS, u"Fake")); // let the leave settle
	cerrors = client.errors().size();
	client.requestGameJoinDirect(fakeAddress);
	NetByteWriter deny = header(LANMessageType::JOIN_DENY, u"Fake");
	deny.u8(200);
	refusedByClient(deny, "join deny reason 200");
	// ---- the START's network-slot flags
	LobbyStart start;
	start.game.game = g;
	start.runAhead = 2;
	start.crcInterval = 100;
	start.networkSlot[0] = true;
	start.networkSlot[1] = true;
	std::vector<std::uint8_t> bytes = LANLobby::encodeStart(start);
	LobbyStart out;
	REQUIRE_MESSAGE(LANLobby::decodeStart(bytes, out, &error), error);
	bytes[bytes.size() - (size_t)start.game.game.serializedSlotCount() * 7] = 7; // slot 0's network flag (u8 flag, u32 ip, u16 port per slot)
	CHECK(!LANLobby::decodeStart(bytes, out, &error));
	CHECK(error.find("not 0 / 1") != std::string::npos);
}

TEST_CASE("mp2 lan (review): every field of a slot request is kKeep, -1 or within its range, else the host refuses the request with an error and keeps "
		  "the slot; full game options with a field out of range are refused by the player")
{
	LANAPI::Options o;
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 24);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	o.playerTemplateCount = 12;
	o.colorCount = 10;
	const ProfileIdentity profile = profileFor("");
	std::string error;
	LANAPI host(profile, u"Host", o);
	REQUIRE_MESSAGE(host.open(&error), error);
	host.requestGameCreate(hostedGame());
	UDP fake;
	REQUIRE_MESSAGE(fake.bind(0, 0, &error), error);
	std::uint64_t clock = 1000;
	auto header = [](std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(77);
		w.wstr(u"Mallory");
		return w;
	};
	auto toHost = [&](const NetByteWriter &w) {
		const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
		fake.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			host.update(++clock);
		}
	};
	NetByteWriter join = header(LANMessageType::REQUEST_JOIN);
	join.str(profile.digest);
	join.u16(fake.localAddress().port);
	toHost(join);
	REQUIRE(host.currentGame());
	REQUIRE(host.currentGame()->info.slots[1].isHuman());
	const SkirmishGameSlot before = host.currentGame()->info.slots[1];
	struct Case
	{
		const char *what;
		int fields[4]; // faction, colour, team, start position
	};
	const int K = LANAPI::kKeep;
	const Case cases[] = { { "faction -2", { -2, K, K, K } }, { "faction 12 (12 factions)", { 12, K, K, K } }, { "colour -2", { K, -2, K, K } },
		{ "colour 10 (10 colours)", { K, 10, K, K } }, { "team -2", { K, K, -2, K } }, { "team 4", { K, K, 4, K } }, { "start position -2", { K, K, K, -2 } },
		{ "start position 8", { K, K, K, 8 } }, { "kKeep - 1", { K - 1, K, K, K } } };
	size_t errors = host.errors().size();
	for (const Case &c : cases)
	{
		NetByteWriter w = header(LANMessageType::GAME_OPTIONS);
		w.u8(1); // OPT_SLOT_REQUEST
		for (int v : c.fields)
		{
			w.u32((std::uint32_t)v);
		}
		w.u32((std::uint32_t)K); // lane OWNERFIX-2 (protocol 3): the handicap, kept
		toHost(w);
		INFO(c.what);
		CHECK(host.errors().size() == errors + 1);
		errors = host.errors().size();
		const SkirmishGameSlot &now = host.currentGame()->info.slots[1];
		CHECK(now.playerTemplate == before.playerTemplate);
		CHECK(now.color == before.color);
		CHECK(now.teamNumber == before.teamNumber);
		CHECK(now.startPos == before.startPos);
	}
	// a request within the ranges is applied
	NetByteWriter good = header(LANMessageType::GAME_OPTIONS);
	good.u8(1);
	for (int v : { 11, 9, 3, 7, K })
	{
		good.u32((std::uint32_t)v);
	}
	toHost(good);
	CHECK(host.errors().size() == errors);
	CHECK(host.currentGame()->info.slots[1].playerTemplate == 11);
	CHECK(host.currentGame()->info.slots[1].color == 9);
	CHECK(host.currentGame()->info.slots[1].teamNumber == 3);
	CHECK(host.currentGame()->info.slots[1].startPos == 7);
	// the player side: full options whose slot 2 has team 4
	LANAPI client(profile, u"Client", o);
	REQUIRE_MESSAGE(client.open(&error), error);
	const NetAddress fakeAddress{ 0x7F000001u, fake.localAddress().port };
	auto toClient = [&](const NetByteWriter &w) {
		const NetAddress dst{ 0x7F000001u, client.lobbyPort() };
		fake.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			client.update(++clock);
		}
	};
	client.requestGameJoinDirect(fakeAddress);
	NetByteWriter acceptJoin = header(LANMessageType::JOIN_ACCEPT);
	acceptJoin.u8(1);
	toClient(acceptJoin);
	REQUIRE(client.currentGame());
	SkirmishGameInfo g = hostedGame();
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"Mallory";
	g.slots[1].state = SLOT_PLAYER;
	g.slots[1].name = u"Client";
	g.slots[2].state = SLOT_EASY_AI;
	g.slots[2].teamNumber = 4;
	NetByteWriter options = header(LANMessageType::GAME_OPTIONS);
	options.u8(0); // OPT_FULL
	options.wstr(u"Mallory");
	NetPacket::writeGameInfo(options, g);
	for (int s = 0; s < g.serializedSlotCount(); ++s)
	{
		options.u32(fakeAddress.ip);
		options.u16(fakeAddress.port);
		options.u32(fakeAddress.ip);
		options.u16(fakeAddress.port);
		options.u8(0);
	}
	const size_t cerrors = client.errors().size();
	toClient(options);
	CHECK(client.errors().size() == cerrors + 1);
	CHECK(client.currentGame()->info.slots[2].teamNumber != 4);
}

TEST_CASE("mp2 lan (HERO-2): a player's Create-a-Hero goes whole through the host to every player and into the start; a refused or broken record "
		  "changes nothing and is an error")
{
	Lan lan({ { u"Host", "" }, { u"Alice", "" }, { u"Bob", "" } });
	LANAPI &host = *lan.peers[0];
	LANAPI &alice = *lan.peers[1];
	LANAPI &bob = *lan.peers[2];
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	lan.run(10);
	REQUIRE(alice.games().size() == 1);
	alice.requestGameJoin(0);
	lan.run(10);
	bob.requestGameJoin(0);
	lan.run(15);
	REQUIRE(alice.currentGame());
	REQUIRE(bob.currentGame());
	REQUIRE(alice.localSlot() == 1);

	// Alice's hero: through the host to Bob, every field
	const CreateAHeroHero hero = testHero("HERO2LANTEST0001");
	std::string error;
	REQUIRE_MESSAGE(alice.requestSlotCreateAHero(&hero, &error), error);
	lan.run(10);
	for (LANAPI *p : { &host, &alice, &bob })
	{
		const SkirmishGameSlot &s = p->currentGame()->info.slots[1];
		CHECK(s.hasCreateAHero);
		CHECK(s.createAHero == hero);
		CHECK_FALSE(p->currentGame()->info.slots[2].hasCreateAHero);
	}
	// the host's own options keep the players' heroes; an AI slot never has one
	SkirmishGameInfo info = host.currentGame()->info;
	info.slots[1].clearCreateAHero();
	info.slots[3].hasCreateAHero = true;
	info.slots[3].createAHero = hero;
	CHECK(host.hostSetOptions(info));
	lan.run(10);
	CHECK(bob.currentGame()->info.slots[1].createAHero == hero);
	CHECK_FALSE(bob.currentGame()->info.slots[3].hasCreateAHero);

	// a record the host's validator refuses: an error on the requester already, and nothing sent
	const CreateAHeroHero refused = testHero("REFUSED");
	CHECK_FALSE(alice.requestSlotCreateAHero(&refused, &error));
	CHECK(error.find("refused by the test's validator") != std::string::npos);
	// Alice clears her hero; then sets it again
	REQUIRE(alice.requestSlotCreateAHero(nullptr, &error));
	lan.run(10);
	CHECK_FALSE(bob.currentGame()->info.slots[1].hasCreateAHero);
	REQUIRE(alice.requestSlotCreateAHero(&hero, &error));
	lan.run(10);
	CHECK(bob.currentGame()->info.slots[1].createAHero == hero);

	// the start carries it to every player
	alice.requestAccept(true);
	bob.requestAccept(true);
	lan.run(10);
	std::string reason;
	REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
	lan.run(20);
	REQUIRE(alice.started());
	REQUIRE(bob.started());
	CHECK(bob.start().game.game.slots[1].hasCreateAHero);
	CHECK(bob.start().game.game.slots[1].createAHero == hero);
	CHECK(bob.start().game.game == host.start().game.game);
	CHECK(alice.start().game.game == host.start().game.game);
	CHECK(host.errors().empty());
	CHECK(alice.errors().empty());
	CHECK(bob.errors().empty());
}

TEST_CASE("mp2 lan (HERO-2): a GameInfo's slot Create-a-Hero is read whole: the record, its checksum, its wire form, a human slot, the size bound")
{
	SkirmishGameInfo g = hostedGame();
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"Host";
	const CreateAHeroHero hero = testHero("HERO2LANTEST0002");
	std::string error;
	REQUIRE(g.slots[0].setCreateAHero(hero, &error));
	NetByteWriter w;
	NetPacket::writeGameInfo(w, g);
	const std::vector<std::uint8_t> good = w.data();
	{
		NetByteReader r(good);
		SkirmishGameInfo back;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error), error);
		CHECK(r.atEnd());
		CHECK(back == g);
		CHECK(back.slots[0].createAHero == hero);
	}
	// where the record starts: after slot 0's fixed fields (state, the name, two flags, seven i32), the hero flag and the byte count
	const size_t at = 4 + 4 + 2 * g.slots[0].name.size() + 2 + 7 * 4 + 1 + 4;
	const std::vector<std::uint8_t> record = hero.save();
	REQUIRE(std::equal(record.begin(), record.end(), good.begin() + (std::ptrdiff_t)at));
	auto readFails = [&](std::vector<std::uint8_t> bytes, const std::string &what) {
		NetByteReader r(bytes);
		SkirmishGameInfo back;
		std::string e;
		CHECK_MESSAGE(!NetPacket::readGameInfo(r, back, &e), what);
		CHECK_MESSAGE(e.find(what) != std::string::npos, e);
	};
	{
		std::vector<std::uint8_t> b = good;
		b[at + record.size() - 1] ^= 1; // the checksum
		readFails(b, "a wrong checksum");
	}
	{
		std::vector<std::uint8_t> b = good;
		b[at + 17] ^= 1; // a byte of the record's ID: the stored checksum no longer matches
		readFails(b, "a wrong checksum");
	}
	{
		std::vector<std::uint8_t> b = good;
		b[at] = 0; // not an ALAE stream
		readFails(b, "not an ALAE");
	}
	{
		std::vector<std::uint8_t> b = good;
		b[at - 5] = 2; // the hero flag
		readFails(b, "flag that is not 0 / 1");
	}
	{
		SkirmishGameInfo ai = g;
		ai.slots[0].state = SLOT_MED_AI;
		NetByteWriter aw;
		NetPacket::writeGameInfo(aw, ai);
		readFails(aw.data(), "no human player");
	}
	{
		CreateAHeroHero big = hero;
		big.bling.clear();
		for (int i = 0; i < 40; ++i)
		{
			big.bling.emplace_back(std::string(200, (char)('A' + i % 26)) + std::to_string(i), 0u);
		}
		SkirmishGameSlot s;
		CHECK_FALSE(s.setCreateAHero(big, &error));
		CHECK(error.find("bytes") != std::string::npos);
	}
}

TEST_CASE("mp2 lan (HERO-2): the host reads a Create-a-Hero request whole and checks the record before it applies it: a broken record, a refused one, a "
		  "bad flag, trailing bytes, a lobby without a validator")
{
	LANAPI::Options o;
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 40);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	o.playerTemplateCount = 12;
	o.colorCount = 10;
	const ProfileIdentity profile = profileFor("");
	std::string error;
	LANAPI noValidator(profile, u"Host", o); // first: the same ports, before the validating host
	o.validateCreateAHero = [](const CreateAHeroHero &h, std::string *why) {
		if (h.uniqueID != "REFUSED")
		{
			return true;
		}
		*why = "refused by the test's validator";
		return false;
	};
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 56);
	LANAPI host(profile, u"Host", o);
	REQUIRE_MESSAGE(host.open(&error), error);
	host.requestGameCreate(hostedGame());
	UDP fake;
	REQUIRE_MESSAGE(fake.bind(0, 0, &error), error);
	std::uint64_t clock = 1000;
	auto header = [](std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(77);
		w.wstr(u"Mallory");
		return w;
	};
	auto toHost = [&](LANAPI &to, const NetByteWriter &w) {
		const NetAddress dst{ 0x7F000001u, to.lobbyPort() };
		fake.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			to.update(++clock);
		}
	};
	NetByteWriter join = header(LANMessageType::REQUEST_JOIN);
	join.str(profile.digest);
	join.u16(fake.localAddress().port);
	toHost(host, join);
	REQUIRE(host.currentGame());
	REQUIRE(host.currentGame()->info.slots[1].isHuman());
	auto request = [&](int flag, const std::vector<std::uint8_t> &record, bool trailing) {
		NetByteWriter w = header(LANMessageType::GAME_OPTIONS);
		w.u8(3); // OPT_SLOT_CREATE_A_HERO
		w.u8((std::uint8_t)flag);
		if (flag)
		{
			w.bytes(record);
		}
		if (trailing)
		{
			w.u8(0);
		}
		return w;
	};
	const std::vector<std::uint8_t> good = testHero("HERO2LANTEST0003").save();
	std::vector<std::uint8_t> broken = good;
	broken[broken.size() - 1] ^= 1;
	struct Case
	{
		const char *what;
		NetByteWriter w;
	};
	const Case cases[] = { { "a wrong checksum", request(1, broken, false) }, { "refused by the test's validator", request(1, testHero("REFUSED").save(), false) },
		{ "malformed", request(2, good, false) }, { "malformed", request(1, good, true) } };
	size_t errors = host.errors().size();
	for (const Case &c : cases)
	{
		toHost(host, c.w);
		INFO(c.what);
		REQUIRE(host.errors().size() == errors + 1);
		CHECK(host.errors().back().find(c.what) != std::string::npos);
		CHECK_FALSE(host.currentGame()->info.slots[1].hasCreateAHero);
		errors = host.errors().size();
	}
	toHost(host, request(1, good, false));
	CHECK(host.errors().size() == errors);
	REQUIRE(host.currentGame()->info.slots[1].hasCreateAHero);
	CHECK(host.currentGame()->info.slots[1].createAHero == testHero("HERO2LANTEST0003"));
	toHost(host, request(0, {}, false)); // cleared
	CHECK_FALSE(host.currentGame()->info.slots[1].hasCreateAHero);

	// a lobby without a validator refuses every record
	o.validateCreateAHero = nullptr;
	REQUIRE_MESSAGE(noValidator.open(&error), error);
	noValidator.requestGameCreate(hostedGame());
	toHost(noValidator, join);
	REQUIRE(noValidator.currentGame());
	toHost(noValidator, request(1, good, false));
	REQUIRE_FALSE(noValidator.errors().empty());
	CHECK(noValidator.errors().back().find("no Create-a-Hero validator") != std::string::npos);
	CHECK_FALSE(noValidator.currentGame()->info.slots[1].hasCreateAHero);
}

TEST_CASE("rules1 lan: AllowCustomHeroes off clears every player's Create-a-Hero in the host's setup, refuses new ones (on the player and on the host) and "
		  "the start carries none; a setup with a hero while the rule is off is refused by the reader")
{
	Lan lan({ { u"Host", "" }, { u"Alice", "" } });
	LANAPI &host = *lan.peers[0];
	LANAPI &alice = *lan.peers[1];
	SkirmishGameInfo game = hostedGame();
	game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	host.requestGameCreate(game);
	alice.requestLocations();
	lan.run(10);
	REQUIRE(alice.games().size() == 1);
	alice.requestGameJoin(0);
	lan.run(15);
	REQUIRE(alice.currentGame());
	const CreateAHeroHero hero = testHero("RULES1LANTEST001");
	std::string error;
	REQUIRE_MESSAGE(alice.requestSlotCreateAHero(&hero, &error), error);
	lan.run(10);
	REQUIRE(host.currentGame()->info.slots[1].hasCreateAHero);

	// the host turns the rule off: the authoritative setup drops Alice's hero and every player sees it
	SkirmishGameInfo info = host.currentGame()->info;
	info.rules[RULE_ALLOW_CUSTOM_HEROES] = 0;
	REQUIRE(host.hostSetOptions(info));
	CHECK_FALSE(host.currentGame()->info.slots[1].hasCreateAHero);
	lan.run(10);
	CHECK(alice.currentGame()->info.rules[RULE_ALLOW_CUSTOM_HEROES] == 0);
	CHECK_FALSE(alice.currentGame()->info.slots[1].hasCreateAHero);

	// Alice may not choose one now
	CHECK_FALSE(alice.requestSlotCreateAHero(&hero, &error));
	CHECK(error.find("AllowCustomHeroes is off") != std::string::npos);
	// a request that crossed the change (Alice still saw the rule on) is refused by the host
	info.rules[RULE_ALLOW_CUSTOM_HEROES] = 1;
	REQUIRE(host.hostSetOptions(info));
	lan.run(10);
	info.rules[RULE_ALLOW_CUSTOM_HEROES] = 0;
	REQUIRE(host.hostSetOptions(info)); // not yet heard by Alice
	REQUIRE(alice.requestSlotCreateAHero(&hero, &error));
	lan.run(10);
	CHECK_FALSE(host.currentGame()->info.slots[1].hasCreateAHero);
	CHECK_FALSE(alice.currentGame()->info.slots[1].hasCreateAHero);
	bool refused = false;
	for (const std::string &e : host.errors())
	{
		refused = refused || e.find("while AllowCustomHeroes is off: refused") != std::string::npos;
	}
	CHECK(refused);

	// the start: the same setup everywhere, no hero, the rule off
	alice.requestAccept(true);
	lan.run(10);
	std::string reason;
	REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
	lan.run(20);
	REQUIRE(alice.started());
	CHECK(alice.start().game.game == host.start().game.game);
	CHECK(alice.start().game.game.rules[RULE_ALLOW_CUSTOM_HEROES] == 0);
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		CHECK_FALSE(alice.start().game.game.slots[s].hasCreateAHero);
	}

	// the reader refuses a setup that carries a hero with the rule off (no host makes one)
	SkirmishGameInfo forged = alice.start().game.game;
	REQUIRE(forged.slots[1].setCreateAHero(hero, &error));
	NetByteWriter w;
	NetPacket::writeGameInfo(w, forged);
	NetByteReader r(w.data());
	SkirmishGameInfo back;
	CHECK_FALSE(NetPacket::readGameInfo(r, back, &error));
	CHECK(error.find("AllowCustomHeroes is off") != std::string::npos);
}

// lane UBERJOIN-1: the GameInfo's simulation profile is optional where it ends the packet (the game list's announcement: a peer that predates UBER-1 lists
// as Faithful) and mandatory everywhere else; a present value is checked as ever
TEST_CASE("uberjoin1 lan: an announcement without the simulation profile lists as Faithful; a malformed profile, or a missing one mid-packet, is refused")
{
	SkirmishGameInfo g = hostedGame();
	g.simulationProfile = SimulationProfile::Uber;
	NetByteWriter w;
	NetPacket::writeGameInfo(w, g);
	const std::vector<std::uint8_t> full = w.data();
	std::string error;
	{
		NetByteReader r(full);
		SkirmishGameInfo back;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error, true), error);
		CHECK(back.simulationProfile == SimulationProfile::Uber); // present: read as ever
		CHECK(r.atEnd());
	}
	// lane OWNERFIX-2: the slots' handicaps (8 i32) follow the profile; LANAPI protocol 3 always writes them, so an announcement with the profile and without
	// them is truncated (a peer that predates OWNERFIX-2 speaks protocol 2 and is refused by its header); a peer that predates UBER-1 has neither
	{
		std::vector<std::uint8_t> noHandicaps(full.begin(), full.end() - 4 - 4 * LEGACY_MAX_SLOTS); // lane CHEAT-1: the Cheat Mode i32 ends the GameInfo
		NetByteReader r(noHandicaps);
		SkirmishGameInfo back;
		std::string e;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &e, true));
		CHECK(e == "truncated GameInfo");
	}
	std::vector<std::uint8_t> absent(full.begin(), full.end() - 4 - 4 - 4 * LEGACY_MAX_SLOTS); // lane CHEAT-1: the profile, the handicaps and the Cheat Mode
	{
		NetByteReader r(absent);
		SkirmishGameInfo back;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error, true), error);
		CHECK(back.simulationProfile == SimulationProfile::Faithful);
		CHECK(r.atEnd());
	}
	{
		NetByteReader r(absent); // not the announcement: the field is mandatory
		SkirmishGameInfo back;
		std::string e;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &e));
		CHECK(e == "truncated GameInfo");
	}
	{
		std::vector<std::uint8_t> partial(full.begin(), full.end() - 2); // half the field: truncated, never "absent"
		NetByteReader r(partial);
		SkirmishGameInfo back;
		std::string e;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &e, true));
		CHECK(e == "truncated GameInfo");
	}
	for (std::int32_t bad : { 2, -1, 99 })
	{
		std::vector<std::uint8_t> b = absent;
		NetByteWriter tail;
		tail.i32(bad);
		for (int i = 0; i < LEGACY_MAX_SLOTS; ++i)
		{
			tail.i32(0); // lane OWNERFIX-2: the handicaps follow a present profile
		}
		tail.i32(-1); // lane CHEAT-1: then the Cheat Mode (off)
		b.insert(b.end(), tail.data().begin(), tail.data().end());
		NetByteReader r(b);
		SkirmishGameInfo back;
		std::string e;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &e, true));
		CHECK(e == "GameInfo simulation profile " + std::to_string(bad));
	}
}

// lane UBERJOIN-1: the player's profile and a lobby's switch for one game, through a real LAN lobby pair: the setter really switches, a switch is undone on
// every exit (a failed or cancelled join, the room left, the lobby closed, the game over and the room left), a held room pins the profile, and the setting
// is never written
namespace
{
ProfileIdentity identityOf(SimulationProfile p)
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	return ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {}, p);
}

struct ModePair
{
	std::unique_ptr<LANAPI> host, join;
	ProfileChoice hostChoice, joinChoice; ///< the host program's choice per player: the way GameWorld holds one
	std::uint64_t clock = 1000;
	ModePair(SimulationProfile hostMode, SimulationProfile joinOwn)
	{
		LANAPI::Options o;
		o.playerTemplateCount = 16;
		o.colorCount = 10;
		o.lobbyPortBase = testPortBase();
		o.lobbyPorts = 8;
		o.broadcast = false;
		o.extraTargets = { 0x7F000001u };
		o.resendMs = 300;
		o.actionTimeoutMs = 2000;
		host = std::make_unique<LANAPI>(identityOf(hostMode), u"Host", o);
		join = std::make_unique<LANAPI>(identityOf(joinOwn), u"Joiner", o);
		std::string error;
		REQUIRE_MESSAGE(host->open(&error), error);
		REQUIRE_MESSAGE(join->open(&error), error);
		hostChoice.own = hostChoice.current = hostMode;
		hostChoice.lan = host.get();
		hostChoice.identityFor = identityOf;
		joinChoice.own = joinChoice.current = joinOwn;
		joinChoice.lan = join.get();
		joinChoice.identityFor = identityOf;
	}
	void run(int ticks)
	{
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			host->update(clock);
			host->takeEvents();
			join->update(clock);
			join->takeEvents();
			NetSleepMilliseconds(1);
		}
	}
	SimulationProfile announced() const { return join->profile().simulationProfile(); }
};
} // namespace

TEST_CASE("uberjoin1 lan: the setting really switches (Normal -> Uber and back), inside and outside a room, and the lobby announces it")
{
	ModePair m(SimulationProfile::Faithful, SimulationProfile::Faithful);
	std::string why;
	// the reported bug: asking for Uber from Normal answered ok and stayed Normal
	REQUIRE(m.joinChoice.setOwn(SimulationProfile::Uber, &why));
	CHECK(m.joinChoice.current == SimulationProfile::Uber);
	CHECK(m.joinChoice.own == SimulationProfile::Uber);
	CHECK_FALSE(m.joinChoice.switched);
	CHECK(m.announced() == SimulationProfile::Uber);
	REQUIRE(m.joinChoice.setOwn(SimulationProfile::Faithful, &why));
	CHECK(m.joinChoice.current == SimulationProfile::Faithful);
	CHECK(m.announced() == SimulationProfile::Faithful);
	CHECK_FALSE(m.joinChoice.setOwn((SimulationProfile)7, &why));
	CHECK(why == "unknown simulation profile");
	CHECK(m.joinChoice.current == SimulationProfile::Faithful);
	// a lobby's switch leaves the setting alone and restore undoes it; a restore with nothing switched changes nothing
	REQUIRE(m.joinChoice.restore());
	REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
	CHECK(m.joinChoice.current == SimulationProfile::Uber);
	CHECK(m.joinChoice.own == SimulationProfile::Faithful);
	CHECK(m.joinChoice.switched);
	CHECK(m.announced() == SimulationProfile::Uber);
	REQUIRE(m.joinChoice.restore());
	CHECK(m.joinChoice.current == SimulationProfile::Faithful);
	CHECK_FALSE(m.joinChoice.switched);
	CHECK(m.announced() == SimulationProfile::Faithful);
	// a switch to the player's own mode is no switch
	REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Faithful, &why));
	CHECK_FALSE(m.joinChoice.switched);
	// the option changed while a switch holds: only the setting moves, and it is what comes back
	REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
	REQUIRE(m.joinChoice.setOwn(SimulationProfile::Faithful, &why)); // the player's setting is Normal already: nothing changes for the game
	CHECK(m.joinChoice.current == SimulationProfile::Uber);
	CHECK(m.joinChoice.own == SimulationProfile::Faithful);
	CHECK(m.joinChoice.switched);
	REQUIRE(m.joinChoice.setOwn(SimulationProfile::Uber, &why)); // now the setting equals the game's: the switch is over
	CHECK_FALSE(m.joinChoice.switched);
	CHECK(m.joinChoice.current == SimulationProfile::Uber);
}

TEST_CASE("uberjoin1 lan: a switched player's room lifecycle: a held room pins the profile; every exit puts the setting back: the room left, a failed join, the "
		  "lobby closed, the game over and the room left")
{
	std::string why;
	// ---- the join, then the room left by the player ----
	{
		ModePair m(SimulationProfile::Uber, SimulationProfile::Faithful);
		m.run(10);
		m.host->requestGameCreate(hostedGame());
		m.run(10);
		REQUIRE(m.join->games().size() == 1);
		CHECK(m.join->games()[0].info.simulationProfile == SimulationProfile::Uber);
		REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
		m.join->requestGameJoin(0);
		m.run(20);
		REQUIRE(m.join->currentGame());
		CHECK(m.join->localSlot() == 1);
		CHECK(m.joinChoice.roomHeld());
		// inside the room the profile is pinned: another switch, and the option's change while it is the game's, are refused with nothing changed
		CHECK_FALSE(m.joinChoice.switchForGame(SimulationProfile::Faithful, &why));
		CHECK(why == "the simulation profile cannot change inside a LAN game (leave it first)");
		CHECK(m.joinChoice.current == SimulationProfile::Uber);
		CHECK_FALSE(m.joinChoice.restore()); // the room is held: the switch stands
		CHECK(m.joinChoice.current == SimulationProfile::Uber);
		CHECK(m.joinChoice.switched);
		// the player's setting changes in the room (the option while the room is up): only the setting moves
		REQUIRE(m.joinChoice.setOwn(SimulationProfile::Faithful, &why));
		CHECK(m.joinChoice.current == SimulationProfile::Uber);
		// the room is left (the lobby screen's Cancel): the setting is back, announced
		m.join->requestGameLeave();
		m.run(10);
		CHECK_FALSE(m.join->currentGame());
		REQUIRE(m.joinChoice.restore());
		CHECK(m.joinChoice.current == SimulationProfile::Faithful);
		CHECK_FALSE(m.joinChoice.switched);
		CHECK(m.announced() == SimulationProfile::Faithful);
	}
	// ---- the game over: the room is kept (the players return to it): the switch stands until the room is left, and leaveRoomAndRestore does both ----
	{
		ModePair m(SimulationProfile::Uber, SimulationProfile::Faithful);
		m.run(10);
		m.host->requestGameCreate(hostedGame());
		m.run(10);
		REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
		m.join->requestGameJoin(0);
		m.run(20);
		REQUIRE(m.join->currentGame());
		m.join->requestAccept(true);
		m.run(10);
		// the game ended: the world asks for the restore: a held room stands (the bug: nothing ever left it)
		CHECK_FALSE(m.joinChoice.restore());
		CHECK(m.joinChoice.switched);
		CHECK(m.joinChoice.leaveRoomAndRestore()); // the lobby is gone for good (main menu): the room is left, then the setting is back
		m.run(10);
		CHECK_FALSE(m.join->currentGame());
		CHECK(m.joinChoice.current == SimulationProfile::Faithful);
		CHECK_FALSE(m.joinChoice.switched);
		CHECK(m.announced() == SimulationProfile::Faithful);
		// and a lobby that offers the Normal game again: the list shows the host's game with the host's mode only
		m.run(10);
	}
	// ---- a failed or cancelled join: no room was ever held ----
	{
		ModePair m(SimulationProfile::Uber, SimulationProfile::Faithful);
		REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
		CHECK_FALSE(m.joinChoice.roomHeld());
		REQUIRE(m.joinChoice.restore());
		CHECK(m.joinChoice.current == SimulationProfile::Faithful);
		CHECK(m.announced() == SimulationProfile::Faithful);
	}
	// ---- the lobby closed (lan_close): the pair of the choice is dropped, then the restore ----
	{
		ModePair m(SimulationProfile::Uber, SimulationProfile::Faithful);
		m.run(10);
		m.host->requestGameCreate(hostedGame());
		m.run(10);
		REQUIRE(m.joinChoice.switchForGame(SimulationProfile::Uber, &why));
		m.join->requestGameJoin(0);
		m.run(20);
		REQUIRE(m.join->currentGame());
		m.joinChoice.lan = nullptr;
		m.join.reset();
		REQUIRE(m.joinChoice.restore());
		CHECK(m.joinChoice.current == SimulationProfile::Faithful);
	}
	// ---- the setter inside a held room with nothing switched: refused by name, nothing changes ----
	{
		ModePair m(SimulationProfile::Faithful, SimulationProfile::Faithful);
		m.run(10);
		m.host->requestGameCreate(hostedGame());
		m.run(10);
		m.join->requestGameJoin(0);
		m.run(20);
		REQUIRE(m.join->currentGame());
		CHECK_FALSE(m.joinChoice.setOwn(SimulationProfile::Uber, &why));
		CHECK(why == "the simulation profile cannot change inside a LAN game (leave it first)");
		CHECK(m.joinChoice.current == SimulationProfile::Faithful);
		CHECK(m.joinChoice.own == SimulationProfile::Faithful);
		REQUIRE(m.joinChoice.setOwn(SimulationProfile::Faithful, &why)); // the same setting: fine
	}
}

// lane OWNERFIX-2 (Sol's review of 7fe9e8e1): a joining player's handicap goes through the host like its faction (the slot request's fifth field, LANAPI
// protocol 3): the host applies it to the sender's own slot only, clears every accept, re-broadcasts it, and the START carries it; a value the combo never
// offers, the previous request layout and a message of the previous protocol version are refused by name with the setup unchanged
TEST_CASE("ownerfix2 lan: a joiner's handicap reaches the host and every player through its slot request; out-of-set values and the previous layouts are refused")
{
	Lan lan({ { u"Host", "" }, { u"Alice", "" }, { u"Bob", "" } });
	LANAPI &host = *lan.peers[0];
	LANAPI &alice = *lan.peers[1];
	LANAPI &bob = *lan.peers[2];
	lan.run(10);
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	bob.requestLocations();
	lan.run(10);
	REQUIRE(alice.games().size() == 1);
	alice.requestGameJoin(0);
	lan.run(10);
	bob.requestGameJoin(0);
	lan.run(15);
	REQUIRE(alice.currentGame());
	REQUIRE(bob.currentGame());
	REQUIRE(alice.localSlot() == 1);
	REQUIRE(bob.localSlot() == 2);
	alice.requestAccept(true);
	bob.requestAccept(true);
	lan.run(10);
	REQUIRE(host.currentGame()->accepted[1]);
	REQUIRE(host.currentGame()->accepted[2]);
	// Alice's handicap alone (every other field kept): the host's setup, Bob's and Alice's own copy show it; every accept is cleared (the options changed)
	alice.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, -25);
	lan.run(10);
	CHECK(host.currentGame()->info.slots[1].handicap == -25);
	CHECK(bob.currentGame()->info.slots[1].handicap == -25);
	CHECK(alice.currentGame()->info.slots[1].handicap == -25);
	CHECK(host.currentGame()->info.slots[0].handicap == 0); // the sender's slot only
	CHECK(host.currentGame()->info.slots[2].handicap == 0);
	CHECK(host.currentGame()->info.slots[3].handicap == 0);
	CHECK_FALSE(host.currentGame()->accepted[1]);
	CHECK_FALSE(host.currentGame()->accepted[2]);
	// a later request that keeps the handicap (kKeep) leaves it; Bob's own is Bob's
	alice.requestSlotOptions(2, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep);
	bob.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, -95);
	lan.run(10);
	CHECK(host.currentGame()->info.slots[1].playerTemplate == 2);
	CHECK(host.currentGame()->info.slots[1].handicap == -25);
	CHECK(host.currentGame()->info.slots[2].handicap == -95);
	CHECK(alice.currentGame()->info.slots[2].handicap == -95);
	// values the combo never offers (RW 0x840B9D: 0, -5 .. -95): refused by the host by name, the slot unchanged
	for (int bad : { -7, 5, -100, -1, 1000 })
	{
		const size_t errors = host.errors().size();
		alice.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, bad);
		lan.run(10);
		INFO(bad);
		CHECK(host.currentGame()->info.slots[1].handicap == -25);
		REQUIRE(host.errors().size() == errors + 1);
		CHECK(host.errors().back().find("handicap " + std::to_string(bad) + " (0, -5 .. -95): refused") != std::string::npos);
	}
	// the host's own slot through its own setter
	host.requestSlotOptions(LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, LANAPI::kKeep, -50);
	lan.run(10);
	CHECK(alice.currentGame()->info.slots[0].handicap == -50);
	// the START carries every handicap to every player
	alice.requestAccept(true);
	bob.requestAccept(true);
	lan.run(10);
	std::string reason;
	REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
	lan.run(20);
	REQUIRE(alice.started());
	REQUIRE(bob.started());
	for (LANAPI *p : { &host, &alice, &bob })
	{
		CHECK(p->start().game.game.slots[0].handicap == -50);
		CHECK(p->start().game.game.slots[1].handicap == -25);
		CHECK(p->start().game.game.slots[2].handicap == -95);
		CHECK(p->start().game.game.slots[3].handicap == 0);
	}
}

TEST_CASE("ownerfix2 lan: the protocol versions moved with the handicap layouts (LANAPI 3, LANLobby 4); the previous versions and the four-field slot request are refused by name")
{
	CHECK(LANAPI::kProtocolVersion == 4); // lane CHEAT-1: 4 / 5 add the GameInfo's Cheat Mode (the handicap layouts' 3 / 4 are refused as before)
	CHECK(LANLobby::kVersion == 6); // lane CHEAT-1 (5 is P16's former extended START)
	// a START of the previous lobby version (3: the GameInfo without the handicaps) is refused by its version byte, before any GameInfo is read
	LobbyStart start;
	start.game.game = hostedGame();
	start.game.game.slots[0].state = SLOT_PLAYER;
	start.game.game.slots[0].name = u"Host";
	start.game.game.slots[0].handicap = -30;
	start.networkSlot[0] = true;
	std::vector<std::uint8_t> bytes = LANLobby::encodeStart(start);
	CHECK(bytes[3] == LANLobby::kVersion); // u16 magic, u8 kind, u8 version
	LobbyStart out;
	std::string error;
	REQUIRE_MESSAGE(LANLobby::decodeStart(bytes, out, &error), error);
	CHECK(out.game.game.slots[0].handicap == -30);
	bytes[3] = 3;
	CHECK_FALSE(LANLobby::decodeStart(bytes, out, &error));
	CHECK(error == "lobby START version 3 (this build speaks 6)");
	// a lobby message of protocol 2 and a protocol 3 slot request in the four-field layout of protocol 2: refused, the host's setup unchanged
	LANAPI::Options o;
	o.playerTemplateCount = 16;
	o.colorCount = 10;
	o.lobbyPortBase = (std::uint16_t)(testPortBase() + 8);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	const ProfileIdentity profile = profileFor("");
	LANAPI host(profile, u"Host", o);
	REQUIRE_MESSAGE(host.open(&error), error);
	host.requestGameCreate(hostedGame());
	UDP peer;
	REQUIRE_MESSAGE(peer.bind(0, 0, &error), error);
	const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
	std::uint64_t clock = 1000;
	auto header = [](std::uint8_t version, std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(version);
		w.u8(type);
		w.u32(78);
		w.wstr(u"Carol");
		return w;
	};
	auto deliver = [&](const NetByteWriter &w) {
		peer.sendTo(dst, w.data().data(), w.data().size());
		for (int i = 0; i < 20; ++i)
		{
			NetSleepMilliseconds(2);
			host.update(++clock);
		}
	};
	NetByteWriter oldJoin = header(2, LANMessageType::REQUEST_JOIN);
	oldJoin.str(profile.digest);
	oldJoin.u16(peer.localAddress().port);
	size_t errors = host.errors().size();
	deliver(oldJoin);
	REQUIRE(host.errors().size() == errors + 1);
	CHECK(host.errors().back().rfind("a LAN lobby message of version 2 (this build speaks 4) from ", 0) == 0);
	CHECK_FALSE(host.currentGame()->info.slots[1].isHuman());
	NetByteWriter join = header(LANAPI::kProtocolVersion, LANMessageType::REQUEST_JOIN);
	join.str(profile.digest);
	join.u16(peer.localAddress().port);
	deliver(join);
	REQUIRE(host.currentGame()->info.slots[1].isHuman());
	const SkirmishGameSlot before = host.currentGame()->info.slots[1];
	NetByteWriter fourFields = header(LANAPI::kProtocolVersion, LANMessageType::GAME_OPTIONS);
	fourFields.u8(1); // OPT_SLOT_REQUEST
	fourFields.i32(2);
	fourFields.i32(LANAPI::kKeep);
	fourFields.i32(LANAPI::kKeep);
	fourFields.i32(LANAPI::kKeep);
	errors = host.errors().size();
	deliver(fourFields);
	REQUIRE(host.errors().size() == errors + 1);
	CHECK(host.errors().back().rfind("a malformed slot request from ", 0) == 0);
	CHECK(host.currentGame()->info.slots[1] == before);
	NetByteWriter fiveFields = header(LANAPI::kProtocolVersion, LANMessageType::GAME_OPTIONS);
	fiveFields.u8(1);
	fiveFields.i32(2);
	fiveFields.i32(LANAPI::kKeep);
	fiveFields.i32(LANAPI::kKeep);
	fiveFields.i32(LANAPI::kKeep);
	fiveFields.i32(-40);
	errors = host.errors().size();
	deliver(fiveFields);
	CHECK(host.errors().size() == errors);
	CHECK(host.currentGame()->info.slots[1].playerTemplate == 2);
	CHECK(host.currentGame()->info.slots[1].handicap == -40);
}
