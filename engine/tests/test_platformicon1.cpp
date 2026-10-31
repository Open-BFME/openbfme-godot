// OpenBFME unit tests. GPL-3.0.
// Lane PLATFORMICON-1: each player's system (Windows / Linux / macOS) is display data next to the name. Here: the LAN lobby's PLATFORM_INFO message (a new
// type: an old build ignores it; a malformed one changes nothing and is named in errors()) between instances on localhost sockets, and the online lobby
// client's field (the create / join body, the room's slots: absent, null, a bad value). Nothing of it is part of a slot's equality or the options.

#include "doctest.h"

#include "GameNetwork/LANAPI.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/PlatformKind.h"
#include "GameNetwork/Transport.h"

#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
ProfileIdentity profile()
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	return ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
}

std::uint16_t portBase(int offset)
{
#ifdef _WIN32
	const int pid = 1234;
#else
	const int pid = (int)getpid();
#endif
	return (std::uint16_t)(48000 + (pid % 800) * 16 + offset * 8);
}

LANAPI::Options lanOptions(int offset, PlatformKind platform)
{
	LANAPI::Options o;
	o.playerTemplateCount = 16;
	o.colorCount = 10;
	o.lobbyPortBase = portBase(offset);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	o.resendMs = 300;
	o.actionTimeoutMs = 2000;
	o.platform = platform;
	return o;
}

struct Peers
{
	std::vector<std::unique_ptr<LANAPI>> p;
	std::uint64_t clock = 1000;
	Peers(int offset, const std::vector<std::pair<std::u16string, PlatformKind>> &who)
	{
		for (const auto &w : who)
		{
			p.push_back(std::make_unique<LANAPI>(profile(), w.first, lanOptions(offset, w.second)));
			std::string error;
			REQUIRE_MESSAGE(p.back()->open(&error), error);
		}
	}
	void run(int ticks)
	{
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			for (auto &peer : p)
			{
				peer->update(clock);
				peer->takeEvents();
			}
			NetSleepMilliseconds(1);
		}
	}
};

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

std::string platforms(const LANAPI &lan)
{
	std::string out;
	for (int s = 0; s < MAX_SLOTS; ++s) // P16: the current game's slots are the 16-slot table (the helper ran on the legacy 8)
	{
		out += std::to_string((int)lan.currentGame()->info.slots[s].platform);
	}
	return out;
}
} // namespace

TEST_CASE("platformicon1: the closed set of values (names, bytes, images, help text)")
{
	PlatformKind k = PlatformKind::None;
	CHECK((platformFromName("windows", k) && k == PlatformKind::Windows));
	CHECK((platformFromName("linux", k) && k == PlatformKind::Linux));
	CHECK((platformFromName("macos", k) && k == PlatformKind::MacOS));
	for (const char *bad : { "", "Windows", "LINUX", "mac", "macOS", "ios", "windows ", "linux\n", "beos" })
	{
		k = PlatformKind::Linux;
		CHECK_MESSAGE(!platformFromName(bad, k), bad);
		CHECK(k == PlatformKind::Linux); // untouched on a refusal
	}
	for (int b = 0; b < 4; ++b)
	{
		CHECK(platformFromByte((std::uint8_t)b, k));
		CHECK((int)k == b);
	}
	for (int b : { 4, 5, 127, 255 })
	{
		k = PlatformKind::Windows;
		CHECK_MESSAGE(!platformFromByte((std::uint8_t)b, k), b);
		CHECK(k == PlatformKind::Windows);
	}
	CHECK(std::string(platformHelpText(PlatformKind::Windows)) == "Playing on Windows");
	CHECK(std::string(platformHelpText(PlatformKind::Linux)) == "Playing on Linux");
	CHECK(std::string(platformHelpText(PlatformKind::MacOS)) == "Playing on macOS");
	CHECK(std::string(platformHelpText(PlatformKind::None)).empty());
	CHECK(std::string(platformImageName(PlatformKind::None)).empty());
	CHECK(localPlatform() != PlatformKind::None); // this build is one of the three
}

TEST_CASE("platformicon1 lan: each player sees the others' systems, an old peer (tells nothing) shows none, the AI and open slots show none, and a "
		  "slot's next player shows its own")
{
	Peers lan(0, { { u"Host", PlatformKind::Linux }, { u"Alice", PlatformKind::Windows }, { u"Bob", PlatformKind::None }, { u"Cara", PlatformKind::MacOS } });
	LANAPI &host = *lan.p[0], &alice = *lan.p[1], &bob = *lan.p[2], &cara = *lan.p[3];
	lan.run(10);
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	lan.run(10);
	REQUIRE(alice.games().size() == 1);
	CHECK(alice.games()[0].info.slots[0].platform == PlatformKind::None); // the list's announce carries none
	alice.requestGameJoin(0);
	lan.run(10);
	bob.requestGameJoin(0);
	lan.run(30);
	REQUIRE(alice.currentGame());
	REQUIRE(bob.currentGame());
	// slots: 0 host (linux = 2), 1 Alice (windows = 1), 2 Bob (an old peer: 0), 3 the AI (0)
	CHECK(platforms(host) == "2100000000000000");
	CHECK(platforms(alice) == "2100000000000000");
	CHECK(platforms(bob) == "2100000000000000");
	// the host's own setup change does not drop them
	SkirmishGameInfo info = host.currentGame()->info;
	info.slots[3].state = SLOT_HARD_AI;
	REQUIRE(host.hostSetOptions(info));
	lan.run(10);
	CHECK(platforms(host) == "2100000000000000");
	CHECK(platforms(alice) == "2100000000000000");
	CHECK(host.currentGame()->info.slots[3].state == SLOT_HARD_AI);
	// it is not part of the slot's equality (display data): the same slots with and without are equal
	SkirmishGameSlot a, b;
	a.platform = PlatformKind::Linux;
	CHECK(a == b);
	// Bob leaves, Cara (macOS) takes the slot: the others show macOS there, not Bob's none / a stale value
	bob.requestGameLeave();
	lan.run(10);
	CHECK(platforms(host) == "2100000000000000");
	cara.requestLocations();
	lan.run(10);
	cara.requestGameJoin(0);
	lan.run(40);
	REQUIRE(cara.currentGame());
	CHECK(cara.localSlot() == 2);
	CHECK(platforms(host) == "2130000000000000");
	CHECK(platforms(alice) == "2130000000000000");
	CHECK(platforms(cara) == "2130000000000000");
	// Alice leaves; the host's row of the open slot shows none, and nothing was reported as an error
	alice.requestGameLeave();
	lan.run(20);
	CHECK(platforms(host) == "2030000000000000");
	for (auto &peer : lan.p)
	{
		CHECK_MESSAGE(peer->errors().empty(), (peer->errors().empty() ? "" : peer->errors()[0]));
	}
}

TEST_CASE("platformicon1 lan: a malformed platform message (a value outside 0 .. 3, no byte, trailing bytes, from a stranger) changes nothing and is named")
{
	Peers lan(1, { { u"Host", PlatformKind::Linux }, { u"Alice", PlatformKind::Windows } });
	LANAPI &host = *lan.p[0], &alice = *lan.p[1];
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	lan.run(10);
	alice.requestGameJoin(0);
	lan.run(30);
	REQUIRE(host.currentGame());
	REQUIRE(platforms(host) == "2100000000000000");
	// the attacker is a player of the game (slot 2) that sends bad platform messages
	UDP attacker;
	std::string error;
	REQUIRE_MESSAGE(attacker.bind(0, 0, &error), error);
	const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
	auto header = [](std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(99);
		w.wstr(u"Mallory");
		return w;
	};
	auto pump = [&](const NetByteWriter &w) {
		attacker.sendTo(dst, w.data().data(), w.data().size());
		lan.run(8);
	};
	NetByteWriter join = header(LANMessageType::REQUEST_JOIN);
	join.str(profile().digest);
	join.u16(attacker.localAddress().port);
	pump(join);
	REQUIRE(host.currentGame()->info.slots[2].isHuman());
	CHECK(platforms(host) == "2100000000000000"); // a joiner that told nothing yet
	size_t errors = host.errors().size();
	NetByteWriter bad = header(LANMessageType::PLATFORM_INFO);
	bad.u8(9);
	pump(bad);
	CHECK(host.errors().size() == errors + 1);
	CHECK(host.errors().back().find("platform 9 (0 .. 3 are known)") != std::string::npos);
	CHECK(platforms(host) == "2100000000000000");
	NetByteWriter cut = header(LANMessageType::PLATFORM_INFO);
	pump(cut);
	CHECK(host.errors().size() == errors + 2);
	CHECK(host.errors().back().find("malformed platform message") != std::string::npos);
	NetByteWriter extra = header(LANMessageType::PLATFORM_INFO);
	extra.u8(2);
	extra.u8(2);
	pump(extra);
	CHECK(host.errors().size() == errors + 3);
	CHECK(platforms(host) == "2100000000000000");
	// the good one is applied, to the sender's slot only (no field of the setup shifted)
	const SkirmishGameInfo before = host.currentGame()->info;
	NetByteWriter good = header(LANMessageType::PLATFORM_INFO);
	good.u8(3);
	pump(good);
	CHECK(host.errors().size() == errors + 3);
	CHECK(platforms(host) == "2130000000000000");
	CHECK(host.currentGame()->info == before);
	// a stranger (not in the game) cannot set anyone's: its message is ignored
	UDP stranger;
	REQUIRE_MESSAGE(stranger.bind(0, 0, &error), error);
	NetByteWriter fromStranger = header(LANMessageType::PLATFORM_INFO);
	fromStranger.u8(1);
	stranger.sendTo(dst, fromStranger.data().data(), fromStranger.data().size());
	lan.run(8);
	CHECK(platforms(host) == "2130000000000000");
	CHECK(host.errors().size() == errors + 3);
}

TEST_CASE("platformicon1 lan: a player that leaves takes its system with it: the same endpoint joining again without telling shows no icon (host and peer)")
{
	Peers lan(5, { { u"Host", PlatformKind::Linux }, { u"Alice", PlatformKind::Windows } });
	LANAPI &host = *lan.p[0], &alice = *lan.p[1];
	host.requestGameCreate(hostedGame());
	alice.requestLocations();
	lan.run(10);
	alice.requestGameJoin(0);
	lan.run(30);
	REQUIRE(platforms(host) == "2100000000000000");
	UDP guest; // one endpoint for both visits
	std::string error;
	REQUIRE_MESSAGE(guest.bind(0, 0, &error), error);
	const NetAddress dst{ 0x7F000001u, host.lobbyPort() };
	auto header = [](std::uint8_t type) {
		NetByteWriter w;
		w.u16(0x4E4C);
		w.u8(LANAPI::kProtocolVersion);
		w.u8(type);
		w.u32(123);
		w.wstr(u"Guest");
		return w;
	};
	auto send = [&](const NetByteWriter &w) {
		guest.sendTo(dst, w.data().data(), w.data().size());
		lan.run(10);
	};
	auto join = [&] {
		NetByteWriter j = header(LANMessageType::REQUEST_JOIN);
		j.str(profile().digest);
		j.u16(guest.localAddress().port);
		send(j);
	};
	join();
	NetByteWriter told = header(LANMessageType::PLATFORM_INFO);
	told.u8(3);
	send(told);
	CHECK(platforms(host) == "2130000000000000"); // the host shows macOS in slot 2 (the guest told the host only: Alice shows none)
	CHECK(platforms(alice) == "2100000000000000");
	NetByteWriter leave = header(LANMessageType::REQUEST_GAME_LEAVE);
	leave.u8(0);
	send(leave);
	CHECK(platforms(host) == "2100000000000000");
	CHECK(platforms(alice) == "2100000000000000");
	join(); // the same endpoint, and this time it tells nothing (an older build)
	REQUIRE(host.currentGame()->info.slots[2].isHuman());
	CHECK(platforms(host) == "2100000000000000");
	CHECK(platforms(alice) == "2100000000000000");
	CHECK(host.errors().empty());
}

TEST_CASE("platformicon1 online client: the create / join body carries platform only when told; the room's slots read it; absent, null and bad values show none")
{
	OnlineLobbyClient::Options o;
	o.profileDigest = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = 16;
	o.colorCount = 16;
	auto signedIn = [&](OnlineLobbyClient &c) {
		c.signInGuest(u"x");
		auto rs = c.takeRequests();
		c.deliver(rs[0].id, 200, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"x\",\"kind\":\"guest\"}}", "");
		c.takeEvents();
	};
	// an old build / a client told nothing: the body is the one it always was
	{
		OnlineLobbyClient c(o);
		signedIn(c);
		c.joinGame("0123456789abcdef", "");
		CHECK(c.takeRequests()[0].body == "{\"profile\":\"0123456789abcdef0123456789abcdef\",\"mode\":\"normal\"}");
	}
	for (const auto &want : std::vector<std::pair<PlatformKind, const char *>>{ { PlatformKind::Windows, "windows" }, { PlatformKind::Linux, "linux" }, { PlatformKind::MacOS, "macos" } })
	{
		OnlineLobbyClient::Options t = o;
		t.platform = want.first;
		OnlineLobbyClient c(t);
		signedIn(c);
		c.joinGame("0123456789abcdef", "pw");
		CHECK(c.takeRequests()[0].body == std::string("{\"profile\":\"0123456789abcdef0123456789abcdef\",\"mode\":\"normal\",\"platform\":\"") + want.second + "\",\"password\":\"pw\"}");
	}
	// the room's slots
	auto view = [&](const std::string &, const std::vector<std::string> &perSlot) {
		std::string slots;
		for (int i = 0; i < 8; ++i)
		{
			const bool player = i < 3;
			slots += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i) + ",\"state\":\"" + (player ? "player" : (i == 3 ? "hard" : "open")) +
				"\",\"name\":\"p" + std::to_string(i) + "\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
				(i == 1 ? "true" : "false") + (perSlot[(size_t)i].empty() ? "" : ",\"platform\":" + perSlot[(size_t)i]) + "}";
		}
		return "{\"v\":1,\"slot\":1,\"game\":{\"id\":\"0123456789abcdef\",\"name\":\"g\",\"state\":\"open\",\"private\":false,\"hostSlot\":0,\"profile\":\"x\",\"setup\":{\"map\":\"m\","
			   "\"startingCash\":1500,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},\"slots\":[" + slots + "],\"seq\":3}}";
	};
	auto joined = [&](const std::vector<std::string> &perSlot) {
		auto c = std::make_unique<OnlineLobbyClient>(o);
		signedIn(*c);
		c->joinGame("0123456789abcdef", "");
		auto rs = c->takeRequests();
		c->deliver(rs[0].id, 200, view("", perSlot), "");
		c->takeEvents();
		return c;
	};
	// slot 0 linux, slot 1 (us) absent (an older lobby / client), slot 2 a bad value, slot 3 an AI claiming one, the rest null
	auto c = joined({ "\"linux\"", "", "\"beos\"", "\"windows\"", "null", "null", "null", "null" });
	REQUIRE(c->inGame());
	CHECK(c->gameInfo().slots[0].platform == PlatformKind::Linux);
	CHECK(c->gameInfo().slots[1].platform == PlatformKind::None);
	CHECK(c->gameInfo().slots[2].platform == PlatformKind::None);
	CHECK(c->gameInfo().slots[3].platform == PlatformKind::None); // an AI never shows one
	REQUIRE(c->platformNotes().size() == 2);
	CHECK(c->platformNotes()[0] == "slot 2: platform \"beos\" is not windows, linux or macos");
	CHECK(c->platformNotes()[1] == "slot 3: platform \"windows\" on a slot that is not a player's");
	// wrong types (a number, an array, an object, the empty string, other case) are named and show none; the view is still taken
	c = joined({ "\"macos\"", "7", "[\"linux\"]", "{\"os\":1}", "\"\"", "\"Linux\"", "\"windows\"", "" });
	REQUIRE(c->inGame());
	CHECK(c->gameInfo().slots[0].platform == PlatformKind::MacOS);
	for (int i = 1; i < 8; ++i)
	{
		CHECK_MESSAGE(c->gameInfo().slots[(size_t)i].platform == PlatformKind::None, i);
	}
	CHECK(c->platformNotes().size() == 6); // slots 1, 2, 3, 4, 5 and 6 (an open slot's value too)
	CHECK(c->platformNotes()[0] == "slot 1: platform (not a string) is not windows, linux or macos");
}

// ---- the staging room (LanLobby.apt's MpGameSetup), retail data ----
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/PlatformIcons.h"
#include "GameClient/GUI/WindowManager.h"

#include "PlatformIconLayout.h"
#include "StartShellFx.h"

#include <functional>

TEST_CASE("platformicon1 retail: the LAN staging room draws each human's system as an icon left of the name, and nothing for the AI, an open slot or an old peer")
{
	OPENBFME_REQUIRE_START(s);
	const int factions = s->world->playerTemplates().getPlayerTemplateCount();
	const int colors = (int)s->settings.multiplayerColors.size();
	auto makeLan = [&](const std::u16string &name, PlatformKind platform) {
		LANAPI::Options o = lanOptions(3, platform);
		o.playerTemplateCount = factions;
		o.colorCount = colors;
		auto lan = std::make_unique<LANAPI>(profile(), name, o);
		std::string error;
		REQUIRE_MESSAGE(lan->open(&error), error);
		return lan;
	};
	std::unique_ptr<LANAPI> hostLan = makeLan(u"Host", PlatformKind::Linux);
	std::unique_ptr<LANAPI> joinLan = makeLan(u"Joiner", PlatformKind::MacOS);
	starttest::ShellFx host(*s, 21);
	starttest::ShellFx join(*s, 22);
	host.environment.lan = hostLan.get();
	host.environment.services = &host.services;
	join.environment.lan = joinLan.get();
	join.environment.services = &join.services;
	host.shell->push("LanLobby.apt");
	join.shell->push("LanLobby.apt");
	AptLanLobby *hs = dynamic_cast<AptLanLobby *>(host.shell->top());
	AptLanLobby *js = dynamic_cast<AptLanLobby *>(join.shell->top());
	REQUIRE(hs);
	REQUIRE(js);
	auto run = [&](const std::function<bool()> &done, int limit = 300) {
		for (int i = 0; i < limit; ++i)
		{
			host.tick(1);
			join.tick(1);
			NetSleepMilliseconds(2);
			if (done())
			{
				return true;
			}
		}
		return false;
	};
	REQUIRE(run([&] { return hs->state() == 1 && js->state() == 1; }));
	CHECK(host.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	REQUIRE(run([&] { return hs->state() == 4; }));
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	REQUIRE(js->gamesList());
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	auto badge = [](AptLanLobby *screen, int slot) -> std::string {
		GameWindow *w = screen->slotGadget(slot, "Player");
		return w ? w->winBadgeImage() : std::string("(no window)");
	};
	REQUIRE(run([&] { return badge(hs, 0) == "OpenBFMEPlatformLinux" && badge(hs, 1) == "OpenBFMEPlatformMacOS" && badge(js, 0) == "OpenBFMEPlatformLinux" && badge(js, 1) == "OpenBFMEPlatformMacOS"; }));
	run([] { return false; }, 120); // the setup page plays in: its rows come to rest
	for (AptLanLobby *screen : { hs, js })
	{
		CHECK(badge(screen, 2).empty());
		CHECK(badge(screen, 3).empty());
		GameWindow *name = screen->slotGadget(1, "Player");
		REQUIRE(name);
		CHECK(name->winBadgeHelp() == "Playing on macOS");
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		REQUIRE(name->winBadgeRect(&x0, &y0, &x1, &y1));
		INFO("badge " << x0 << "," << y0 << " - " << x1 << "," << y1);
		// the icon is left of the name's window, on the stage, the size the screens ask for, and overlaps no gadget of any row
		int nx = 0, ny = 0, nw = 0, nh = 0;
		name->winGetScreenPosition(&nx, &ny);
		name->winGetSize(&nw, &nh);
		MESSAGE("name window " << nx << "," << ny << " " << nw << "x" << nh << " badge " << x0 << "," << y0 << "-" << x1 << "," << y1);
		CHECK(x1 == nx - 2);                          // two stage pixels of air before the name's window
		CHECK(x0 == nx - 2 - kPlatformIconStage);
		CHECK(y0 == ny + (nh - kPlatformIconStage) / 2); // centred on the window's height
		CHECK(y1 - y0 == kPlatformIconStage);
		// the movie's own parts: no Ready box (by name), no button (Kick ...) and no text of the row is under any slot's icon
		Apt &apt = (screen == hs ? host.wm : join.wm)->apt();
		std::vector<AptCharacterInst *> movieParts;
		platformlayout::collectNamed(apt.level(screen->level()), "Ready", movieParts);
		const std::size_t readyCount = movieParts.size();
		platformlayout::collectButtons(apt.level(screen->level()), movieParts);
		CHECK(readyCount == 9); // one Ready box per slot and the column's header
		for (int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			GameWindow *nameWin = screen->slotGadget(slot, "Player");
			int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
			if (!nameWin || nameWin->winBadgeImage().empty() || !nameWin->winBadgeRect(&ix0, &iy0, &ix1, &iy1))
			{
				continue;
			}
			const platformlayout::Rect icon{ (float)ix0, (float)iy0, (float)ix1, (float)iy1 };
			for (AptCharacterInst *part : movieParts)
			{
				platformlayout::Rect r;
				if (platformlayout::stageRect(part, r))
				{
					CHECK_MESSAGE(!icon.overlaps(r), "slot " << slot << "'s icon is over " << part->targetPath());
				}
			}
		}
		for (int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			for (const char *leaf : { "Player", "PlayerTemplate", "Team", "Color", "Hero", "Handicap" })
			{
				GameWindow *other = screen->slotGadget(slot, leaf);
				if (!other)
				{
					continue;
				}
				int ox = 0, oy = 0, ow = 0, oh = 0;
				other->winGetScreenPosition(&ox, &oy);
				other->winGetSize(&ow, &oh);
				const bool overlap = x0 < ox + ow && ox < x1 && y0 < oy + oh && oy < y1;
				CHECK_MESSAGE(!overlap, "slot " << slot << " " << leaf << " at " << ox << "," << oy << " " << ow << "x" << oh);
			}
		}
	}
	// the platform is display data: the games' slots are equal with and without it, and the start the host would send carries none
	CHECK(hostLan->currentGame()->info.slots[1].platform == PlatformKind::MacOS);
	CHECK(hostLan->errors().empty());
	CHECK(joinLan->errors().empty());
}
