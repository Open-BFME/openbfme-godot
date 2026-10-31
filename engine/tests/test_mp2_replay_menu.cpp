// OpenBFME unit tests. GPL-3.0.
// Lane MP-2: SaveLoad.apt's replay page through the real shell (GameClient/GUI/AptScreens/AptSaveLoad.h): the retail movie gets its providers (RW 0x816164),
// its two list boxes get RW 0x818D41's four columns and one row per replay (the GUI:LastReplay file in the AutoSaveList, a replay of another profile grey),
// and Load on a selected row hands the file to the host. SKIP loudly without the retail installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/SaveLoadInfo.h"

#include <string>
#include <vector>

namespace
{
// every visible button below `c` (a message box's Ok)
void visibleButtons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
{
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp)
	{
		return;
	}
	for (AptCharacterInst *child : sp->children())
	{
		if (child && child->asButton() && child->globallyVisible())
		{
			out.push_back(child->asButton());
		}
		visibleButtons(child, out);
	}
}
} // namespace

TEST_CASE("mp2 retail: Load Replay shows SaveLoad.apt's replay page: the files in the two lists with RW 0x818D41's columns, Load hands the chosen file to "
		  "the host")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 7);
	SaveLoadInfo info;
	info.mode = 2;
	info.flags = 4;
	info.lastReplayNameUtf8 = "Last Replay";
	info.replays.push_back({ "/r/Last Replay.replay", "Last Replay", "map mp evendim", "10/06/2026", "12:00", true });
	info.replays.push_back({ "/r/duel.replay", "duel", "map mp fall back 4p", "10/05/2026", "09:30", true });
	info.replays.push_back({ "/r/old.replay", "old", "map mp harlindon", "09/01/2026", "18:45", false });
	fx.environment.saveLoad = &info;
	fx.environment.services = &fx.services;
	fx.shell->push("SaveLoad.apt");
	REQUIRE(fx.shell->errors().empty());
	AptSaveLoad *screen = dynamic_cast<AptSaveLoad *>(fx.shell->top());
	REQUIRE(screen);
	for (int i = 0; i < 120 && screen->rows().empty(); ++i)
	{
		fx.tick(1);
	}
	fx.tick(10);
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		MESSAGE("note " << n.kind << ": " << n.detail);
	}
	if (screen->rows().empty())
	{
		AptSpriteInst *root = fx.wm->apt().level(screen->level());
		for (const char *m : { "SaveLoadMode", "GameTypes", "CurrentGameType", "Initialized" })
		{
			AptValue v;
			const bool found = root && root->getMember(m, v);
			MESSAGE("root." << m << " = " << (found ? v.toString() : std::string("(none)")));
		}
		AptCharacterInst *ui = fx.at(screen->level(), "UI");
		MESSAGE("UI frame " << (ui && ui->asSprite() ? ui->asSprite()->frame : -2) << " root frame " << (root ? root->frame : -2));
		MESSAGE("gadget windows: " << fx.layer->gadgets().allWindows().size());
		for (const std::string &e : fx.layer->errors())
		{
			MESSAGE("layer error " << e);
		}
		for (const std::string &e : fx.layer->notes())
		{
			MESSAGE("layer note " << e);
		}
		for (const std::string &t : fx.wm->traces())
		{
			MESSAGE("trace " << t);
		}
	}
	// the providers (RW 0x816164)
	std::string value;
	CHECK(fx.wm->getExtern("SaveLoadMode", value) == AptExternResult::Value);
	CHECK(value == "Load");
	CHECK(fx.wm->getExtern("GameTypes", value) == AptExternResult::Value);
	CHECK(value == "Replay");
	// the rows (list|map|name|time|date|colour): the last replay in the AutoSaveList, the others in the GameList, the other profile's grey
	const std::vector<std::string> expected = { "AutoSaveList|map mp evendim|Last Replay|12:00|10/06/2026|FFFFFFFF",
		"GameList|map mp fall back 4p|duel|09:30|10/05/2026|FFFFFFFF", "GameList|map mp harlindon|old|18:45|09/01/2026|FF808080" };
	CHECK(screen->rows() == expected);
	GameWindow *games = screen->gameList();
	GameWindow *autos = screen->autoSaveList();
	REQUIRE(games);
	REQUIRE(autos);
	CHECK(GadgetListBoxGetNumColumns(games) == 4);
	int gw = 0, gh = 0;
	games->winGetSize(&gw, &gh);
	MESSAGE("GameList " << gw << "x" << gh << " columns " << GadgetListBoxGetColumnWidth(games, 0) << " " << GadgetListBoxGetColumnWidth(games, 1) << " "
						<< GadgetListBoxGetColumnWidth(games, 2) << " " << GadgetListBoxGetColumnWidth(games, 3));
	CHECK(GadgetListBoxGetColumnWidth(games, 1) > 0);
	CHECK(GadgetListBoxGetNumEntries(games) == 2);
	CHECK(GadgetListBoxGetNumEntries(autos) == 1);
	CHECK(GadgetListBoxGetText(games, 0, 1) == u"duel");
	CHECK(GadgetListBoxGetText(games, 1, 0) == u"map mp harlindon");
	// nothing selected: Load does nothing (noted)
	fx.wm->fscommand("AptSaveLoad::Load", "");
	CHECK(fx.services.countRequests(ShellAction::LoadReplayFile) == 0);
	GadgetDrawList drawn;
	fx.layer->buildDrawList(drawn);
	const ListboxData &data = *static_cast<ListboxData *>(games->winGetUserData());
	CHECK(data.displayPos == 0);
	for (const std::u16string &name : { u"Last Replay", u"duel", u"old" })
	{
		bool visible = false;
		for (const GadgetDrawCommand &command : drawn.commands)
		{
			visible = visible || (command.kind == GadgetDrawCommand::Kind::Text && command.text == name);
		}
		CHECK_MESSAGE(visible, "replay filename is drawn: " << std::string(name.begin(), name.end()));
	}
	GameWindow *chosen = games;
	GameWindow *other = autos;
	int row = 0;
	std::string path = "/r/duel.replay";
	SUBCASE("named replay") {}
	SUBCASE("last replay")
	{
		chosen = autos;
		other = games;
		path = "/r/Last Replay.replay";
	}
	SUBCASE("incompatible replay still reaches the host's validation")
	{
		row = 1;
		path = "/r/old.replay";
	}
	starttest::wheelAndClickRow(fx, other, 0);
	starttest::wheelAndClickRow(fx, chosen, row);
	int selected = -1;
	GadgetListBoxGetSelected(chosen, &selected);
	REQUIRE(selected == row);
	GadgetListBoxGetSelected(other, &selected);
	CHECK(selected == -1);
	fx.tick(10);
	AptButtonInst *load = starttest::firstButtonIn(fx.at(screen->level(), "MainButtons.Load"));
	REQUIRE(load);
	starttest::clickButton(fx, *load);
	fx.tick(60);
	REQUIRE(fx.services.countRequests(ShellAction::LoadReplayFile) == 1);
	CHECK(fx.services.requests.back().argument == path);
	CHECK(screen->loaded() == path);
	// The host answers a Load by starting the replay (the screen goes) or by refusing it with a box: SaveLoad.apt keeps the pressed Load clip on its
	// press frame until a box's Ok reveals it (the movie's RevealButtonList after showMessageBox; AptSaveLoad::revealButton after OpenBFME's own
	// boxes), and LoadButtonEnable("1") on a new selection does not (its vLoadButtonedEnabled is still 1), so retail never takes a second Load
	// without one of the two in between. KNOWN-3: the refusal is the host's answer to the incompatible replay's Load (this case pressed a second Load
	// right after a load the host had accepted: the movie rightly ignored it).
	if (path != "/r/old.replay")
	{
		return;
	}
	// The host's profile refusal uses the same modal as a refused saved game, without leaving the replay list (DEVPORT-7); its Ok reveals Load, and
	// the next row chosen and Load pressed through the real list and button load that file.
	screen->loadRefused("old", "Replay profile: Uber. Current profile: Faithful.");
	CHECK(screen->boxText() == u"Replay profile: Uber.\nCurrent profile: Faithful.");
	CHECK_FALSE(games->winGetEnabled());
	fx.tick(20);
	const int boxLevel = fx.wm->findAptMovieIndex("GuiFX.apt");
	// tolerance: the refusal modal must have a live movie level to receive its dismissal callback
	REQUIRE(boxLevel >= 0);
	// the player's Ok: the box's own button (its press plays the box's exit and sends "_level<n>.MessageBox_OnButtonOk")
	std::vector<AptButtonInst *> boxButtons;
	visibleButtons(fx.wm->apt().level(boxLevel), boxButtons);
	AptButtonInst *ok = nullptr;
	for (AptButtonInst *b : boxButtons)
	{
		ok = !ok && b->targetPath().find("Ok") != std::string::npos ? b : ok;
	}
	REQUIRE(ok);
	starttest::clickButton(fx, *ok);
	fx.tick(30);
	CHECK(fx.shell->top() == screen);
	CHECK(screen->rows() == expected);
	CHECK(games->winGetEnabled());
	starttest::wheelAndClickRow(fx, autos, 0);
	starttest::wheelAndClickRow(fx, games, 0);
	fx.tick(10);
	starttest::clickButton(fx, *load);
	fx.tick(60);
	CHECK(fx.services.countRequests(ShellAction::LoadReplayFile) == 2);
	CHECK(fx.services.requests.back().argument == "/r/duel.replay");
}
