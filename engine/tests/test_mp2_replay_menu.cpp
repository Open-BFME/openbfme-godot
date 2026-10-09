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
	// a row chosen in the GameList (the list's own selection message), then Load
	GadgetListBoxSetSelected(games, 0);
	fx.tick(2);
	fx.wm->fscommand("AptSaveLoad::Load", "");
	REQUIRE(fx.services.countRequests(ShellAction::LoadReplayFile) == 1);
	CHECK(fx.services.requests.back().argument == "/r/duel.replay");
	CHECK(screen->loaded() == "/r/duel.replay");
}
