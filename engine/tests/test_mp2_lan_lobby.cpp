// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: LanLobby.apt through the real shell (GameClient/GUI/AptScreens/AptLanLobby.h) on two LAN lobbies (GameNetwork/LANAPI.h, localhost sockets):
// the host's Create Game puts its game in the joiner's game list, the joiner's Join (a selected row) seats it in MpGameSetup, the joiner's faction combo
// reaches the host and the host's slot is not the joiner's to change, chat, the host's Kick sends the joiner back to the list and it joins again, Play Game
// is refused until the joiner accepted, then both screens hand the start to the host program (ShellAction::LanGameStart). SKIP loudly without the retail
// installs.

#include "doctest.h"

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/MenuNavigator.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/ProfileSwitch.h"
#include "GameNetwork/LANAPI.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
std::uint16_t lobbyTestPortBase()
{
#ifdef _WIN32
	const int pid = 4321;
#else
	const int pid = (int)getpid();
#endif
	return (std::uint16_t)(44000 + (pid % 1000) * 16);
}

std::unique_ptr<LANAPI> testLan(const ProfileIdentity &profile, const std::u16string &name, int factions, int colors)
{
	LANAPI::Options o;
	o.playerTemplateCount = factions; // lane MP-2 (review): the ranges the host checks requests against
	o.colorCount = colors;
	o.lobbyPortBase = lobbyTestPortBase();
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	o.resendMs = 300;
	o.actionTimeoutMs = 2000;
	auto lan = std::make_unique<LANAPI>(profile, name, o);
	std::string error;
	REQUIRE_MESSAGE(lan->open(&error), error);
	return lan;
}

bool hasRequest(const RecordingShellServices &s, ShellAction a)
{
	return std::any_of(s.requests.begin(), s.requests.end(), [a](const ShellRequest &r) { return r.action == a; });
}

bool hasNote(WindowManager &wm, const std::string &kind)
{
	return std::any_of(wm.notes().begin(), wm.notes().end(), [&](const WindowManagerNote &n) { return n.kind == kind; });
}
} // namespace


namespace
{
// lane UI-1: the setup page's CHAT tab (TabButtons.ChatTab, shown while AptMpGameRules::ShowChat is "1") pressed through its own DoButtonCallBack
AptCharacterInst *findClip(AptCharacterInst *c, const std::string &parent, const std::string &name)
{
	if (!c)
	{
		return nullptr;
	}
	AptSpriteInst *sp = c->asSprite();
	if (!sp)
	{
		return nullptr;
	}
	for (AptCharacterInst *child : sp->children())
	{
		if (child && child->instName() == name && c->instName() == parent)
		{
			return child;
		}
		if (AptCharacterInst *found = findClip(child, parent, name))
		{
			return found;
		}
	}
	return nullptr;
}
bool pressChatTab(WindowManager &wm, int level)
{
	AptCharacterInst *tab = findClip(wm.apt().level(level), "TabButtons", "ChatTab");
	std::string error;
	return tab && wm.apt().invoke(tab, "DoButtonCallBack", {}, nullptr, &error);
}
} // namespace

TEST_CASE("shellfb1: the LAN browser is not a host and retains preview controls while host setup has the map picker")
{
	OPENBFME_REQUIRE_START(s);
	const ProfileIdentity profile = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {});
	auto lan = testLan(profile, u"Host", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	starttest::ShellFx fx(*s, 11);
	fx.environment.lan = lan.get();
	fx.shell->push("LanLobby.apt");
	REQUIRE(fx.shell->errors().empty());
	auto *screen = dynamic_cast<AptLanLobby *>(fx.shell->top());
	REQUIRE(screen);
	auto run = [&](int state) {
		for (int i = 0; i < 300; ++i)
		{
			fx.tick(1);
			if (screen->state() == state)
			{
				fx.tick(60);
				return true;
			}
		}
		return false;
	};
	auto hostMode = [&] {
		std::string value;
		CHECK(fx.wm->getExtern("MpGameSetup::HostMode", value) == AptExternResult::Value);
		return value;
	};
	REQUIRE(run(1));
	CHECK(hostMode() == "0");
	CHECK(fx.wm->setExtern("MpGameSetup::HostMode", "1"));
	CHECK(hostMode() == "0");
	REQUIRE(screen->mapListWindow());
	CHECK(screen->mapListWindow()->winIsHidden());
	CHECK(screen->mapInfoWindow() != nullptr);
	REQUIRE(fx.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	REQUIRE(run(4));
	CHECK(hostMode() == "1");
	CHECK(fx.wm->setExtern("MpGameSetup::HostMode", "false"));
	CHECK(hostMode() == "1");
	REQUIRE(screen->mapListWindow());
	CHECK_FALSE(screen->mapListWindow()->winIsHidden());
	REQUIRE(fx.wm->invokeCallback("AptLanLobby::OnCancelBttn", ""));
	REQUIRE(run(1));
	CHECK(hostMode() == "0");
	REQUIRE(screen->mapListWindow());
	CHECK(screen->mapListWindow()->winIsHidden());
	CHECK(screen->mapInfoWindow() != nullptr);
}

TEST_CASE("mp2 retail: LanLobby.apt hosts and joins a LAN game: the list, Join, a player's own options through the host, chat, Kick and rejoin, Accept, "
		  "Play Game hands every player the start")
{
	OPENBFME_REQUIRE_START(s);
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity profile = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	std::unique_ptr<LANAPI> hostLan = testLan(profile, u"Host", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	std::unique_ptr<LANAPI> joinLan = testLan(profile, u"Joiner", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	starttest::ShellFx host(*s, 11);
	starttest::ShellFx join(*s, 12);
	host.environment.lan = hostLan.get();
	host.environment.services = &host.services;
	join.environment.lan = joinLan.get();
	join.environment.services = &join.services;
	host.shell->push("LanLobby.apt");
	join.shell->push("LanLobby.apt");
	REQUIRE(host.shell->errors().empty());
	REQUIRE(join.shell->errors().empty());
	AptLanLobby *hs = dynamic_cast<AptLanLobby *>(host.shell->top());
	AptLanLobby *js = dynamic_cast<AptLanLobby *>(join.shell->top());
	REQUIRE(hs);
	REQUIRE(js);
	// both screens tick (the screens pump their lobbies, RW 0x8490FB) until `done`
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
	// the movies are ready: StartLobby, state 1 (the game list)
	REQUIRE(run([&] { return hs->state() == 1 && js->state() == 1; }));
	// Create Game (RW 0x847B85): the host's game; the setup movie is up (state 4)
	CHECK(host.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	REQUIRE(run([&] { return hs->state() == 4; }));
	CHECK(hostLan->amIHost());
	host.tick(60);
	MenuNavigator controller(*host.wm);
	controller.setMenu(hs->level(), hs);
	REQUIRE(controller.supportsControllerSections());
	controller.setControllerSections(true);
	for (int section : { 2, 0, 1 })
	{
		controller.switchControllerSection(1);
		INFO("controller section ", section);
		CHECK(controller.controllerSection() == section);
		REQUIRE_FALSE(controller.targets().empty());
		bool representative = false;
		for (const auto &target : controller.targets())
			representative |= target.id.find(section == 2 ? ".MapList" : section == 0 ? ".PlayGame." : ".PlayerList.") != std::string::npos;
		CHECK(representative);
	}
	// RB cycles Map -> Rules -> Chat -> Map; LB wraps back through Chat.
	const char *activeTabs[] = { "RulesTab", "ChatTab", "MapTab", "ChatTab", "RulesTab", "MapTab" };
	int tabStep = 0;
	for (MenuNavKey key : { MenuNavKey::PageNext, MenuNavKey::PageNext, MenuNavKey::PageNext,
		MenuNavKey::PagePrevious, MenuNavKey::PagePrevious, MenuNavKey::PagePrevious })
	{
		REQUIRE(controller.key(key));
		host.tick(60);
		CHECK(controller.controllerSection() == 2);
		for (const char *name : { "MapTab", "RulesTab", "ChatTab" })
		{
			bool available = false;
			for (const auto &target : controller.targets()) available |= target.id.find(std::string(".TabButtons.") + name + ".") != std::string::npos;
			CHECK(available == (std::string(name) != activeTabs[tabStep]));
		}
		++tabStep;
		if (std::string(activeTabs[tabStep - 1]) == "ChatTab")
		{
			REQUIRE(hs->chatEntryWindow());
			bool chatInSection = false;
			for (const auto &target : controller.targets()) chatInSection |= target.window == hs->chatEntryWindow();
			CHECK(chatInSection);
		}
	}
	controller.setControllerSections(false);

	// the joiner's list shows it (columns GameName, Players, Map, Ping, Status)
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	CHECK(js->gameRows()[0].rfind("Host|1/8|", 0) == 0);
	// Join of the selected row seats the joiner
	REQUIRE(js->gamesList());
	join.tick(40);
	REQUIRE_FALSE(join.wm->componentInputBlocked());
	starttest::wheelAndClickRow(join, js->gamesList(), 0);
	int clickedRow = -1;
	GadgetListBoxGetSelected(js->gamesList(), &clickedRow);
	REQUIRE(clickedRow == 0);
	AptValue joinEnabled;
	REQUIRE(join.wm->apt().level(js->level())->getMember("vJoinGameEnabled", joinEnabled));
	CHECK(joinEnabled.toBoolean(7));
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	CHECK(joinLan->localSlot() == 1);
	REQUIRE(run([&] { return hs->setup()->info().slots[1].name == u"Joiner"; }));
	CHECK(js->setup()->info().slots[0].name == u"Host");
	join.tick(60);
	MenuNavigator guestController(*join.wm);
	guestController.setMenu(js->level(), js);
	CHECK(guestController.supportsControllerSections());

	// the joiner's own faction through its combo: a request the host applies and re-broadcasts
	// (DoJoinGame plays the lobby out and the setup in: its gadgets come up a few frames later)
	REQUIRE(run([&] {
		GameWindow *f = js->slotGadget(1, "PlayerTemplate");
		return f && GadgetComboBoxGetLength(f) > 2 && hs->slotGadget(1, "PlayerTemplate");
	}));
	GameWindow *faction = js->slotGadget(1, "PlayerTemplate");
	GadgetComboBoxSetSelectedPos(faction, 2, false);
	const int wanted = js->setup()->info().slots[1].playerTemplate;
	CHECK(wanted >= 0);
	REQUIRE(run([&] { return hostLan->currentGame()->info.slots[1].playerTemplate == wanted; }));
	CHECK(run([&] { return hs->setup()->info().slots[1].playerTemplate == wanted; }));
	// the host's slot is not the joiner's to change; the joiner's is not the host's
	REQUIRE(js->slotGadget(0, "PlayerTemplate"));
	auto enabled = [](GameWindow *w) { return (w->winGetStatus() & WIN_STATUS_ENABLED) != 0; };
	CHECK(!enabled(js->slotGadget(0, "PlayerTemplate")));
	CHECK(enabled(js->slotGadget(1, "PlayerTemplate")));
	CHECK(!enabled(hs->slotGadget(1, "PlayerTemplate")));
	// chat (lane UI-1: RotWK's Send reads the ChatEntry gadget, RW 0x9BEDE0, and a line reads "[name] text", RW 0x649E5C)
	REQUIRE(run([&] { return pressChatTab(*join.wm, js->level()) && pressChatTab(*host.wm, hs->level()); }));
	// GitHub #23: one REQUIRE per side, so a failure says which window is missing
	REQUIRE(run([&] { return js->chatEntryWindow() != nullptr; }));
	REQUIRE(run([&] { return hs->chatListWindow() != nullptr; }));
	GadgetTextEntrySetText(js->chatEntryWindow(), u"  hello host ");
	CHECK(join.wm->invokeCallback("AptMpChat::Send", ""));
	CHECK(GadgetTextEntryGetText(js->chatEntryWindow()).empty());
	REQUIRE(run([&] { return !hs->chatLines().empty(); }));
	CHECK(hs->chatLines().back() == "[Joiner] hello host");
	REQUIRE(hs->chatListWindow());
	CHECK(GadgetListBoxGetText(hs->chatListWindow(), GadgetListBoxGetNumEntries(hs->chatListWindow()) - 1, 0) == u"[Joiner] hello host");
	// Play Game before the joiner accepted: refused, the movie's EnablePlayGame
	CHECK(host.wm->invokeCallback("AptLanLobby::OnStartGameBttn", ""));
	run([&] { return hs->state() == 4 && hasNote(*host.wm, "lan-start-refused"); }, 30);
	CHECK(hasNote(*host.wm, "lan-start-refused"));
	CHECK(!hostLan->started());
	// the host kicks the joiner (MpGameSetup::OnKickPlayer): its screen goes back to the list (state 9 -> 0 -> 1), and it joins again
	CHECK(host.wm->invokeCallback("MpGameSetup::OnKickPlayer", "1"));
	REQUIRE(run([&] { return js->state() == 1 && joinLan->inLobby(); }));
	CHECK(hostLan->currentGame()->info.slots[1].state == SLOT_OPEN);
	REQUIRE(js->messageBox());
	REQUIRE(js->messageBox()->type() == AptMessageBox::TYPE_OK);
	js->messageBox()->hide(true);
	join.tick(10);
	CHECK(js->messageBox()->type() == AptMessageBox::TYPE_NONE);
	// back on the list (the movie's CancelGame: LanOpenPlay seeks back to "_lobby"): the list refills, keeps a selection across the announcements'
	// refills, and Join seats the joiner again
	REQUIRE(run([&] { return js->gameRows().size() == 1 && js->gamesList(); }));
	run([] { return false; }, 40);
	REQUIRE(js->gamesList());
	GadgetListBoxSetSelected(js->gamesList(), 0);
	run([] { return false; }, 20);
	int selected = -1;
	REQUIRE(js->gamesList());
	GadgetListBoxGetSelected(js->gamesList(), &selected);
	CHECK(selected == 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	CHECK(joinLan->localSlot() == 1);
	// lane OWNERFIX-2 (Sol's review): the rejoined joiner's own Handicap combo (RW 0x840B9D, item data the value) reaches the host through its slot request,
	// the host's own slot keeps its handicap, and the START carries it below
	REQUIRE(run([&] {
		GameWindow *h = js->slotGadget(1, "Handicap");
		return h && GadgetComboBoxGetLength(h) == 20;
	}));
	GadgetComboBoxSetSelectedPos(js->slotGadget(1, "Handicap"), 6, false); // -30%
	REQUIRE(js->setup()->info().slots[1].handicap == -30);
	REQUIRE(run([&] { return hostLan->currentGame()->info.slots[1].handicap == -30; }));
	CHECK(run([&] { return hs->setup()->info().slots[1].handicap == -30; }));
	CHECK(hostLan->currentGame()->info.slots[0].handicap == 0);
	// lane ONLINE-5: the Ready column (the LAN panel's flag 0x20, RW 0x848C6B): each screen told its movie the boxes' states (RW 0x84211D): the host's
	// slot ticked, the joiner's own box the only one that takes a press
	REQUIRE(run([&] { return hs->readyIcon(1) == "_disabledUnchecked" && js->readyIcon(1) == "_enabledUnchecked"; }));
	CHECK(hs->readyIcon(0) == "_disabledChecked");
	CHECK(js->readyIcon(0) == "_disabledChecked");
	CHECK(hs->readyIcon(2) == "_none");
	// The local Ready box must draw and accept through mouse input, not an invoked callback.
	join.tick(60);
	AptCharacterInst *ready = join.wm->apt().resolvePath(join.wm->apt().level(js->level()),
		"LanOpenPlay.JoinMain.instance1.PlayerList.~1.Ready");
	REQUIRE(ready);
	REQUIRE(ready->globallyVisible());
	AptButtonInst *readyButton = starttest::firstButtonIn(ready);
	REQUIRE(readyButton);
	AptRenderList readyDraw;
	join.wm->apt().buildRenderList(readyDraw);
	const std::string readyPath = ready->targetPath() + ".";
	CHECK(std::any_of(readyDraw.commands.begin(), readyDraw.commands.end(), [&](const AptRenderCommand &cmd) {
		return cmd.path.rfind(readyPath, 0) == 0 && cmd.kind == AptRenderCommand::Kind::Shape
			&& cmd.geometry && !cmd.fills.empty() && cmd.color.mul[3] > 0;
	}));
	starttest::clickButton(join, *readyButton);
	REQUIRE(run([&] { return hostLan->currentGame()->accepted[1]; }));
	REQUIRE(run([&] { return hs->readyIcon(1) == "_disabledChecked" && js->readyIcon(1) == "_enabledChecked"; }));
	CHECK(!enabled(js->slotGadget(1, "PlayerTemplate"))); // RW 0x843F6E: a joiner who accepted cannot change its row
	CHECK(host.wm->invokeCallback("AptLanLobby::OnStartGameBttn", ""));
	// lane UI-1: the start counts down first (RW 0x843840 / 0x84392C: six seconds of LAN:GameStartTimer lines to every player, then five seconds of the
	// QM:STARTINGGAME box)
	REQUIRE(run([&] { return hs->countingDown(); }, 100));
	REQUIRE(run([&] { return hs->startingBoxShown(); }, 20000));
	REQUIRE(hs->messageBox() != nullptr);
	CHECK(hs->messageBox()->type() == AptMessageBox::TYPE_NON_INTERACTIVE);
	CHECK(js->chatLines().size() >= 5); // the five countdown lines reached the joiner
	REQUIRE(run([&] { return hasRequest(host.services, ShellAction::LanGameStart) && hasRequest(join.services, ShellAction::LanGameStart); }, 20000));
	CHECK(hostLan->started());
	CHECK(joinLan->started());
	CHECK(joinLan->start().game.game == hostLan->start().game.game);
	CHECK(joinLan->start().game.game.slots[1].name == u"Joiner");
	CHECK(joinLan->start().localSlot == 1);
	CHECK(joinLan->start().game.game.slots[1].handicap == -30); // lane OWNERFIX-2
	CHECK(hostLan->start().game.game.slots[0].handicap == 0);
	CHECK(hostLan->errors().empty());
	CHECK(joinLan->errors().empty());
}

// lane UBERJOIN-1 (S-4860): the LAN game list tags an Uber game; Join from Normal asks Yes / No: No stays in the list, Yes switches the profile for the
// game and joins it, leaving the room puts the player's own mode back. The mode is the announced GameInfo's simulation profile (UBER-1's wire field)
TEST_CASE("uberjoin1 retail: the LAN list tags an Uber game; Join from Normal asks Yes / No: No stays, Yes switches and joins, leaving restores")
{
	OPENBFME_REQUIRE_START(s);
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity normal = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful);
	const ProfileIdentity uber = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Uber);
	std::unique_ptr<LANAPI> hostLan = testLan(uber, u"Host", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	std::unique_ptr<LANAPI> joinLan = testLan(normal, u"Joiner", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	starttest::ShellFx host(*s, 21);
	starttest::ShellFx join(*s, 22);
	// the host program's side of the switch (GodotGameWorld::switch_profile_for_game / restore_simulation_profile): the LAN lobby follows the identity
	int requests = 0, restores = 0;
	SimulationProfile current = SimulationProfile::Faithful;
	std::string refuse;
	ProfileSwitch ps;
	ps.current = [&]() { return current; };
	ps.digestOf = [&](SimulationProfile p) { return (p == SimulationProfile::Uber ? uber : normal).digest; };
	ps.request = [&](SimulationProfile p, std::string *why) {
		++requests;
		if (!refuse.empty())
		{
			*why = refuse;
			return false;
		}
		if (!joinLan->setProfile(p == SimulationProfile::Uber ? uber : normal))
		{
			*why = "in a game";
			return false;
		}
		current = p;
		return true;
	};
	ps.restore = [&]() {
		++restores;
		current = SimulationProfile::Faithful;
		joinLan->setProfile(normal);
	};
	host.environment.lan = hostLan.get();
	host.environment.services = &host.services;
	join.environment.lan = joinLan.get();
	join.environment.services = &join.services;
	join.environment.profileSwitch = &ps;
	host.shell->push("LanLobby.apt");
	join.shell->push("LanLobby.apt");
	REQUIRE(host.shell->errors().empty());
	REQUIRE(join.shell->errors().empty());
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
	CHECK(std::find(hs->chatLines().begin(), hs->chatLines().end(), "This room uses Uber performance mode.") != hs->chatLines().end());
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	const std::string row = js->gameRows()[0];
	CHECK(row.rfind("|uber") == row.size() - 5);
	{
		// the header cells are the movie's own: the four sort buttons of the list's frame (Status, Game Name, Map, Players), read from the APT; the movie
		// has no Ping header (the fifth column's cell must still start right of the Players header's left edge and inside the list)
		std::map<std::string, std::pair<float, float>> headers; // name -> stage x of the cell's left and right edge
		std::function<void(AptCharacterInst *)> walk = [&](AptCharacterInst *c) {
			if (!c)
			{
				return;
			}
			if (c->instName().rfind("OnSort", 0) == 0)
			{
				float x0, y0, x1, y1, a, b, cx, d;
				REQUIRE(c->contentBounds(x0, y0, x1, y1));
				c->globalMatrix().apply(x0, y0, a, b);
				c->globalMatrix().apply(x1, y1, cx, d);
				headers[c->instName()] = { a, cx };
			}
			if (AptSpriteInst *sp = c->asSprite())
			{
				for (AptCharacterInst *k : sp->children())
				{
					walk(k);
				}
			}
		};
		walk(join.wm->apt().level(js->level()));
		REQUIRE(headers.size() == 4); // exactly Status, Game Name, Map, Players: a movie without one of them fails here, not silently
		REQUIRE(headers.count("OnSortStatus"));
		REQUIRE(headers.count("OnSortName"));
		REQUIRE(headers.count("OnSortMap"));
		REQUIRE(headers.count("OnSortPlayers"));
		int listX = 0, listY = 0, listW = 0, listH = 0;
		js->gamesList()->winGetScreenPosition(&listX, &listY);
		js->gamesList()->winGetSize(&listW, &listH);
		float cellLeft[5];
		float x = (float)listX;
		for (int c = 0; c < 5; ++c)
		{
			cellLeft[c] = x;
			x += (float)GadgetListBoxGetColumnWidth(js->gamesList(), c);
		}
		const char *order[4] = { "OnSortStatus", "OnSortName", "OnSortMap", "OnSortPlayers" };
		for (int c = 0; c < 4; ++c)
		{
			const auto &h = headers[order[c]];
			// the windows' integer coordinates may sit up to one pixel left of a header cell's fractional edge
			CHECK_MESSAGE(cellLeft[c] >= std::floor(h.first), "column " << c << " starts at " << cellLeft[c] << ", its header " << order[c] << " at " << h.first); // tolerance: containment in the header cell read from the APT (a cell's left edge is a range, not one value); the window coordinates are integers
			CHECK_MESSAGE(cellLeft[c] < h.second, "column " << c << " starts at " << cellLeft[c] << ", past its header " << order[c] << "'s right edge " << h.second); // tolerance: containment in the header cell read from the APT (a cell's left edge is a range, not one value); the window coordinates are integers
		}
		CHECK_MESSAGE(cellLeft[4] >= std::floor(headers["OnSortPlayers"].first), "the Ping column starts at " << cellLeft[4]); // tolerance: containment in the header cell read from the APT (a cell's left edge is a range, not one value); the window coordinates are integers
		CHECK_MESSAGE(cellLeft[4] < (float)(listX + listW), "the Ping column starts inside the list"); // tolerance: containment in the header cell read from the APT (a cell's left edge is a range, not one value); the window coordinates are integers
	}
	// the cells sit under the movie's headers Status, Game Name, Map, Players, Ping (they once stood one column off: the name under Status); an empty cell holds a space
	REQUIRE(js->gamesList());
	CHECK(GadgetListBoxGetText(js->gamesList(), 0, 0) == u" ");
	CHECK(GadgetListBoxGetText(js->gamesList(), 0, 1) == u"Host  - UBER");
	CHECK(GadgetListBoxGetText(js->gamesList(), 0, 2) == u"map mp adorn river");
	CHECK(GadgetListBoxGetText(js->gamesList(), 0, 3) == u"1/8");
	CHECK(GadgetListBoxGetText(js->gamesList(), 0, 4) == u" ");
	const std::string prompt = "mode-switch|This game uses Uber performance mode. Switch to Uber mode and join? (Your setting changes back after the game.)";
	// Join: the box asks; nothing switched, nothing sent
	REQUIRE(js->gamesList());
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->boxesShown().size() == 1; }));
	CHECK(js->boxesShown()[0] == prompt);
	CHECK(requests == 0);
	CHECK(js->messageBox()->type() == AptMessageBox::TYPE_YES_NO);
	run([] { return false; }, 20);
	CHECK(!joinLan->currentGame());
	// No: stays in the list, unswitched, no error box
	join.wm->fscommand("_level" + std::to_string(js->messageBox()->level()) + ".MessageBox_OnButtonNo", "");
	run([] { return false; }, 30);
	CHECK(js->state() == 1);
	CHECK(requests == 0);
	CHECK(joinLan->profile().simulationProfile() == SimulationProfile::Faithful);
	CHECK(!joinLan->currentGame());
	CHECK(js->boxesShown().size() == 1);
	// Yes: switched, joined
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->boxesShown().size() == 2; }));
	join.wm->fscommand("_level" + std::to_string(js->messageBox()->level()) + ".MessageBox_OnButtonYes", "");
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	CHECK(requests == 1);
	CHECK(current == SimulationProfile::Uber);
	CHECK(joinLan->profile().simulationProfile() == SimulationProfile::Uber);
	CHECK(js->boxesShown().size() == 2);
	CHECK(std::find(js->chatLines().begin(), js->chatLines().end(), "This room uses Uber performance mode.") != js->chatLines().end());
	// leaving: the player's own mode again
	const int restoresBefore = restores;
	CHECK(join.wm->invokeCallback("AptLanLobby::OnCancelBttn", ""));
	REQUIRE(run([&] { return js->state() == 1; }));
	CHECK(restores == restoresBefore + 1);
	CHECK(current == SimulationProfile::Faithful);
	CHECK(joinLan->profile().simulationProfile() == SimulationProfile::Faithful);
	// a refused switch: a named box, still in the list
	refuse = "the simulation profile cannot change inside a LAN game (leave it first)";
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->boxesShown().size() == 3; }));
	join.wm->fscommand("_level" + std::to_string(js->messageBox()->level()) + ".MessageBox_OnButtonYes", "");
	REQUIRE(run([&] { return js->boxesShown().size() == 4; }));
	CHECK(js->boxesShown()[3] == "mode-switch-failed|The performance mode could not be switched: " + refuse);
	CHECK(!joinLan->currentGame());
	CHECK(joinLan->profile().simulationProfile() == SimulationProfile::Faithful);
}

namespace
{
void collectButtons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
{
	if (!c)
	{
		return;
	}
	if (AptButtonInst *b = c->asButton())
	{
		if (b->globallyVisible())
		{
			out.push_back(b);
		}
		return;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			collectButtons(k, out);
		}
	}
}

// presses the visible button whose path holds `part` when the pointer at its centre reaches it (a player's click); false: none reachable
bool pressReachable(starttest::ShellFx &fx, AptLanLobby &screen, const std::string &part)
{
	std::vector<AptButtonInst *> all;
	collectButtons(fx.wm->apt().level(screen.level()), all);
	for (AptButtonInst *b : all)
	{
		float x0, y0, x1, y1, x, y;
		if (b->targetPath().find(part) == std::string::npos || !b->contentBounds(x0, y0, x1, y1))
		{
			continue;
		}
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		AptButtonInst *hit = fx.wm->apt().input().hitTestButtons(x, y);
		const std::string clip = b->targetPath().substr(0, b->targetPath().rfind('.'));
		if (hit && (hit == b || hit->targetPath().compare(0, clip.size(), clip) == 0))
		{
			starttest::clickAt(fx, x, y);
			return true;
		}
	}
	return false;
}

// the shown rule rows (the movie's RuleComboBox / RuleCheckBox placeholders) that are not the screen's filed rows or reach into the player list
std::string lanRulesProblems(starttest::ShellFx &fx, AptLanLobby &screen)
{
	std::set<const GameWindow *> filed;
	for (int i = 0; i < 16; ++i)
	{
		filed.insert(screen.ruleComboWindow(i));
		filed.insert(screen.ruleCheckWindow(i));
	}
	GameWindow *firstRow = screen.slotGadget(0, "Player");
	if (!firstRow)
	{
		return "no player list";
	}
	int lx = 0, listTop = 0;
	firstRow->winGetScreenPosition(&lx, &listTop);
	std::ostringstream out;
	int shown = 0;
	for (GameWindow *w : fx.wm->gadgetLayer()->gadgets().allWindows())
	{
		const std::string &n = w->aptInstanceName();
		bool visible = true;
		for (GameWindow *p = w; p; p = p->winGetParent())
		{
			visible = visible && !p->winIsHidden();
		}
		if ((n.rfind("RuleComboBox", 0) != 0 && n.rfind("RuleCheckBox", 0) != 0) || !visible)
		{
			continue;
		}
		++shown;
		int x = 0, y = 0, ww = 0, hh = 0;
		w->winGetScreenPosition(&x, &y);
		w->winGetSize(&ww, &hh);
		if (!filed.count(w))
		{
			out << "an unfiled " << n << " is shown at (" << x << "," << y << "); ";
		}
		if (y + hh > listTop)
		{
			out << n << " at (" << x << "," << y << ") is over the player list (its top " << listTop << "); ";
		}
	}
	return shown == 0 ? std::string("no rule row is shown") : out.str();
}
} // namespace

TEST_CASE("rulesov1 retail: the LAN game's RULES tab shows one set of rule rows, above the player list, after the LAN list's own RULES tab")
{
	OPENBFME_REQUIRE_START(s);
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity profile = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	std::unique_ptr<LANAPI> hostLan = testLan(profile, u"Host", s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	starttest::ShellFx host(*s, 21);
	host.environment.lan = hostLan.get();
	host.environment.services = &host.services;
	host.shell->push("LanLobby.apt");
	REQUIRE(host.shell->errors().empty());
	AptLanLobby *hs = dynamic_cast<AptLanLobby *>(host.shell->top());
	REQUIRE(hs);
	auto run = [&](const std::function<bool()> &done, int limit = 300) {
		for (int i = 0; i < limit; ++i)
		{
			host.tick(1);
			NetSleepMilliseconds(2);
			if (done())
			{
				return true;
			}
		}
		return false;
	};
	REQUIRE(run([&] { return hs->state() == 1; }));
	// the LAN list's own MAP / RULES panel: its RULES tab when the page has one
	const bool listRules = run([&] { return pressReachable(host, *hs, "TabButtons.RulesTab"); }, 120);
	MESSAGE("the LAN list's RULES tab " << (listRules ? "pressed" : "not on the page"));
	host.tick(120);
	CHECK(host.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	REQUIRE(run([&] { return hs->state() == 4; }));
	host.tick(60);
	REQUIRE(run([&] { return pressReachable(host, *hs, "TabButtons.RulesTab"); }));
	REQUIRE(run([&] { return hs->rulesPopulated() && hs->slotGadget(0, "Player") != nullptr; }));
	host.tick(120);
	CHECK_MESSAGE(lanRulesProblems(host, *hs).empty(), lanRulesProblems(host, *hs));
	for (const char *tab : { "TabButtons.MapTab", "TabButtons.ChatTab", "TabButtons.RulesTab" })
	{
		REQUIRE_MESSAGE(run([&] { return pressReachable(host, *hs, tab); }), tab);
		host.tick(60);
	}
	REQUIRE(run([&] { return hs->rulesPopulated(); }));
	host.tick(60);
	CHECK_MESSAGE(lanRulesProblems(host, *hs).empty(), lanRulesProblems(host, *hs));
}
