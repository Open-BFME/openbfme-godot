// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: the LAN lobby (GameNetwork/LANAPI.h) between three instances on localhost UDP sockets: the host's game appears in the others' lists, two
// players join and get slots, a player's own options go through the host (a colour another slot holds is refused), chat reaches the game, the host
// kicks and the kicked player rejoins, the start waits for every accept and gives every player the same game with its own slot and the host's game
// socket, another profile identity is refused at the join, and a player who stops answering is dropped.

#include "doctest.h"

#include "GameNetwork/LANAPI.h"
#include "GameNetwork/NetPacket.h"
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
		w.u8(2);
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
		w.u8(2);
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
	for (int s = 0; s < MAX_SLOTS; ++s)
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
	bytes[bytes.size() - (size_t)MAX_SLOTS * 7] = 7; // slot 0's network flag (u8 flag, u32 ip, u16 port per slot)
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
		w.u8(2);
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
	for (int v : { 11, 9, 3, 7 })
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
	for (int s = 0; s < MAX_SLOTS; ++s)
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
		w.u8(2);
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
