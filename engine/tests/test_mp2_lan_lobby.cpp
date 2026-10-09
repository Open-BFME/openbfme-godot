// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: LanLobby.apt through the real shell (GameClient/GUI/AptScreens/AptLanLobby.h) on two LAN lobbies (GameNetwork/LANAPI.h, localhost sockets):
// the host's Create Game puts its game in the joiner's game list, the joiner's Join (a selected row) seats it in MpGameSetup, the joiner's faction combo
// reaches the host and the host's slot is not the joiner's to change, chat, the host's Kick sends the joiner back to the list and it joins again, Play Game
// is refused until the joiner accepted, then both screens hand the start to the host program (ShellAction::LanGameStart). SKIP loudly without the retail
// installs.

#include "doctest.h"

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameNetwork/LANAPI.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

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
	// the joiner's list shows it (columns GameName, Players, Map, Ping, Status)
	REQUIRE(run([&] { return js->gameRows().size() == 1; }));
	CHECK(js->gameRows()[0].rfind("Host|1/8|", 0) == 0);
	// Join of the selected row seats the joiner
	REQUIRE(js->gamesList());
	GadgetListBoxSetSelected(js->gamesList(), 0);
	CHECK(join.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", ""));
	REQUIRE(run([&] { return js->state() == 4 && hostLan->currentGame()->info.slots[1].isHuman(); }));
	CHECK(joinLan->localSlot() == 1);
	REQUIRE(run([&] { return hs->setup()->info().slots[1].name == u"Joiner"; }));
	CHECK(js->setup()->info().slots[0].name == u"Host");
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
	REQUIRE(run([&] { return js->chatEntryWindow() != nullptr && hs->chatListWindow() != nullptr; }));
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
	// Accept (MpGameSetup::OnReadyPress), then Play Game: every player gets the start, both screens hand it to the host program
	CHECK(join.wm->invokeCallback("MpGameSetup::OnReadyPress", "1"));
	REQUIRE(run([&] { return hostLan->currentGame()->accepted[1]; }));
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
	CHECK(hostLan->errors().empty());
	CHECK(joinLan->errors().empty());
}
