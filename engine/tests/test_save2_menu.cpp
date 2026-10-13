// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-2: SaveLoad.apt's saved-game pages through the real shell and the retail movie (GameClient/GUI/AptScreens/AptSaveLoad.h): the Load page lists
// the host's saves by type with populateSaveGameListbox's columns and colours (a save another build made grey, a retail save as a grey import entry), Load
// hands a loadable save to the host and shows the refusal of the others, a load over a game asks first (the movie's showMessageBox "Load"), Delete asks and
// then hands the file to the host and lists again; the Save page starts with GUI:NewSaveGame and the default name GUI:DefaultSaveFileName with the first free
// number, asks before overwriting, refuses file-name characters and shows the host's result. SKIP loudly without the retail installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/SaveLoadInfo.h"

#include <string>
#include <vector>

namespace
{
void saveLabels(starttest::MapGameText &t)
{
	// the retail texts (lotr.str) of the labels the pages fetch
	t.set("GUI:NewSaveGame", "**NEW SAVE FILE**");
	t.set("GUI:DefaultSaveFileName", "Saved Game %d");
	t.set("GUI:ErrorLoadingGame", "Error loading game '%s'");
	t.set("APT:SaveGameProgress", "Save Progress");
	t.set("GUI:GameSaved", "Game Saved");
}

SaveLoadInfo::Save save(const std::string &name, int kind, bool loadable, const std::string &refusal = "", bool retail = false)
{
	SaveLoadInfo::Save s;
	s.path = "/s/" + name + (retail ? (kind == 2 ? ".BfME2Skirmish" : ".BfME2Campaign") : ".obfmesave");
	s.fileNameUtf8 = name;
	s.mapUtf8 = retail ? "" : "Evendim";
	s.dateUtf8 = "2026-10-12";
	s.timeUtf8 = "10:00:00";
	s.gameKind = kind;
	s.loadable = loadable;
	s.refusalUtf8 = refusal;
	s.retailImport = retail;
	return s;
}

const char *kOtherBuild = "this game was saved by OpenBFME v0.2.9 (engine 111111111111); this is OpenBFME v0.3.0 (engine 222222222222). A save loads only in "
						  "the build that made it.";

struct Pages
{
	starttest::ShellFx fx;
	SaveLoadInfo info;
	AptSaveLoad *screen = nullptr;

	Pages(starttest::Shared &s, int mode, unsigned flags, bool inGame) : fx(s, 11)
	{
		saveLabels(fx.text);
		info.mode = mode;
		info.flags = flags;
		info.inGame = inGame;
		info.saves.push_back(save("Saved Game 1", 2, true));
		info.saves.push_back(save("old build", 2, false, kOtherBuild));
		info.saves.push_back(save("retail skirmish", 2, false, "", true));
		info.saves.push_back(save("Helm's Deep", 1, true));
		SaveLoadInfo::Save cached = save("Evendim 2", 2, true); // as list_saves gives it: the map's path and the cache's display label
		cached.mapPathUtf8 = "maps/map mp evendim/map mp evendim.map";
		cached.mapUtf8 = "$Map:MAPMPEvendim";
		info.saves.push_back(cached);
		fx.environment.saveLoad = &info;
		fx.environment.services = &fx.services;
		fx.shell->push("SaveLoad.apt");
		REQUIRE(fx.shell->errors().empty());
		screen = dynamic_cast<AptSaveLoad *>(fx.shell->top());
		REQUIRE(screen);
		for (int i = 0; i < 120 && screen->rows().empty(); ++i)
		{
			fx.tick(1);
		}
		fx.tick(10);
		if (screen->rows().empty())
		{
			for (const WindowManagerNote &n : fx.wm->notes())
			{
				MESSAGE("note " << n.kind << ": " << n.detail);
			}
			MESSAGE("game type " << screen->gameType() << ", lists " << (screen->gameList() != nullptr) << " " << (screen->autoSaveList() != nullptr)
								 << " entry " << (screen->fileNameEntry() != nullptr));
			for (const std::string &t : fx.wm->traces())
			{
				MESSAGE("trace " << t);
			}
		}
		REQUIRE_FALSE(screen->rows().empty());
	}
	void showSkirmish()
	{
		REQUIRE(fx.wm->setExtern("CurrentGameType", "Skirmish")); // the movie's Skirmish tab (SetCurrentGameType)
		fx.tick(2);
		REQUIRE(screen->gameType() == 2);
	}
	void select(int row)
	{
		GadgetListBoxSetSelected(screen->gameList(), row);
		fx.tick(2);
	}
	void command(const std::string &name)
	{
		fx.wm->fscommand("AptSaveLoad::" + name, "");
		fx.tick(2);
	}
};
} // namespace

TEST_CASE("save2 retail: SaveLoad.apt's Load page lists the saves by type, loads a loadable one, refuses the others by name and deletes after asking")
{
	OPENBFME_REQUIRE_START(s);
	Pages p(*s, 2, 0xB, false); // the main menu's load page (RW 0x91C360: Campaign | Skirmish | War of the Ring)
	std::string value;
	CHECK(p.fx.wm->getExtern("SaveLoadMode", value) == AptExternResult::Value);
	CHECK(value == "Load");
	CHECK(p.fx.wm->getExtern("GameTypes", value) == AptExternResult::Value);
	CHECK(value == "CampaignSkirmishWOTRSP");
	// the first type of the mask (RW 0x816692): the campaign saves, in (239, 204, 82)
	CHECK(p.screen->gameType() == 1);
	CHECK(p.screen->rows() == std::vector<std::string>{ "GameList|Evendim|Helm's Deep|10:00:00|2026-10-12|FFEFCC52" });
	p.showSkirmish();
	// column 0 of the last one is the map cache's base display name (getMapDisplayName): the test's text source has no map labels, so the map's file name
	// (SkirmishGameSetup::mapBaseDisplayName's last answer), not the saved label
	const std::vector<std::string> skirmish = { "GameList|Evendim|Saved Game 1|10:00:00|2026-10-12|FFFFFFFF",
		"GameList|Evendim|old build|10:00:00|2026-10-12|FF808080", "GameList||retail skirmish (import: coming later)|10:00:00|2026-10-12|FF808080",
		"GameList|map mp evendim|Evendim 2|10:00:00|2026-10-12|FFFFFFFF" };
	CHECK(p.screen->rows() == skirmish);
	CHECK(GadgetListBoxGetNumColumns(p.screen->gameList()) == 4);
	CHECK(GadgetListBoxGetNumEntries(p.screen->gameList()) == 4);
	CHECK((p.screen->autoSaveList() == nullptr || GadgetListBoxGetNumEntries(p.screen->autoSaveList()) == 0));
	// a save another build made: Load shows why, nothing loads
	p.select(1);
	p.command("Load");
	CHECK(p.fx.services.countRequests(ShellAction::LoadGameFile) == 0);
	CHECK(p.screen->boxTitle() == u"Error loading game 'old build'");
	// one clause per line in the box (its text field shrinks a long line)
	CHECK(loadScreenU16ToUtf8(p.screen->boxText()) == "this game was saved by OpenBFME v0.2.9 (engine 111111111111);\nthis is OpenBFME v0.3.0 (engine 222222222222).\nA save "
														"loads only in the build that made it.");
	// a retail save: an import entry, refused with the "coming later" note
	p.select(2);
	p.command("Load");
	CHECK(p.fx.services.countRequests(ShellAction::LoadGameFile) == 0);
	CHECK(loadScreenU16ToUtf8(p.screen->boxText()) == "This is a saved game of The Rise of the Witch-king itself.\nImporting retail saves is coming later.");
	// a loadable save, from the main menu (not over a game): no question, the file goes to the host (state 3)
	p.select(0);
	p.command("Load");
	REQUIRE(p.fx.services.countRequests(ShellAction::LoadGameFile) == 1);
	CHECK(p.fx.services.requests.back().argument == "/s/Saved Game 1.obfmesave");
	CHECK(p.screen->confirmations().empty());
	// Delete asks first (showMessageBox "Delete", state 20), Cancel keeps the file
	p.select(1);
	p.command("Delete");
	CHECK(p.screen->confirmations() == std::vector<std::string>{ "Delete" });
	CHECK(p.screen->state() == 20);
	p.command("ConfirmationCancel");
	CHECK(p.screen->state() == 0);
	CHECK(p.fx.services.countRequests(ShellAction::DeleteGameFile) == 0);
	p.command("Delete");
	p.command("ConfirmationOk");
	REQUIRE(p.fx.services.countRequests(ShellAction::DeleteGameFile) == 1);
	CHECK(p.fx.services.requests.back().argument == "/s/old build.obfmesave");
	// the host deletes it and gives the new list: the page fills its list again
	p.info.saves.erase(p.info.saves.begin() + 1);
	++p.info.generation;
	p.fx.tick(3);
	CHECK(p.screen->rows() == std::vector<std::string>{ skirmish[0], skirmish[2], skirmish[3] });
}

TEST_CASE("save2 retail: a load over a running game asks first (showMessageBox \"Load\"), as RW 0x816DBF's state 14 does")
{
	OPENBFME_REQUIRE_START(s);
	Pages p(*s, 2, 2, true); // the quit menu's LoadMenu in a skirmish
	CHECK(p.screen->gameType() == 2);
	p.select(0);
	p.command("Load");
	CHECK(p.screen->confirmations() == std::vector<std::string>{ "Load" });
	CHECK(p.screen->state() == 18);
	CHECK(p.fx.services.countRequests(ShellAction::LoadGameFile) == 0);
	p.command("ConfirmationOk");
	REQUIRE(p.fx.services.countRequests(ShellAction::LoadGameFile) == 1);
	CHECK(p.fx.services.requests.back().argument == "/s/Saved Game 1.obfmesave");
}

TEST_CASE("save2 retail: SaveLoad.apt's Save page offers a new save under the first free default name, asks before overwriting and shows the result")
{
	OPENBFME_REQUIRE_START(s);
	Pages p(*s, 3, 2, true); // the quit menu's SaveMenu in a skirmish
	GameWindow *entry = p.screen->fileNameEntry();
	REQUIRE(entry);
	SUBCASE("a new save")
	{
		std::string value;
		CHECK(p.fx.wm->getExtern("SaveLoadMode", value) == AptExternResult::Value);
		CHECK(value == "Save");
		// populateSaveGameListbox's new-save row first, in (200, 200, 200), selected
		REQUIRE(p.screen->rows().size() == 5u);
		CHECK(p.screen->rows()[0] == "GameList|**NEW SAVE FILE**||||FFC8C8C8");
		// RW 0x6DE38D: "Saved Game %d" with the first number whose file does not exist ("Saved Game 1" does)
		CHECK(GadgetTextEntryGetText(entry) == u"Saved Game 2");
		p.command("Save");
		REQUIRE(p.fx.services.countRequests(ShellAction::SaveGameFile) == 1);
		CHECK(p.fx.services.requests.back().argument == "Saved Game 2");
		CHECK(p.screen->confirmations().empty());
		// the host saved: APT:SaveGameProgress with GUI:GameSaved
		p.screen->saveFinished(true, "");
		CHECK(p.screen->boxTitle() == u"Save Progress");
		CHECK(p.screen->boxText() == u"Game Saved");
	}
	SUBCASE("an existing name: asked first, then saved under it; the host's failure shown as it is")
	{
		GadgetTextEntrySetText(entry, u"  Saved Game 1 ");
		p.command("Save");
		CHECK(p.screen->confirmations() == std::vector<std::string>{ "Save" });
		CHECK(p.screen->state() == 19);
		CHECK(p.fx.services.countRequests(ShellAction::SaveGameFile) == 0);
		p.command("ConfirmationOk");
		REQUIRE(p.fx.services.countRequests(ShellAction::SaveGameFile) == 1);
		CHECK(p.fx.services.requests.back().argument == "Saved Game 1"); // trimmed (RW 0x817B5F)
		p.screen->saveFinished(false, "writing the save failed (disk full?)");
		CHECK(p.screen->boxText() == u"writing the save failed (disk full?)");
	}
	SUBCASE("typing a name refreshes the movie's Save button (RW 0x8180A8's text-changed branch -> RW 0x816F24)")
	{
		GadgetTextEntrySetText(entry, u"");
		const size_t before = p.screen->buttonCalls().size();
		starttest::typeText(p.fx, entry, u"Ab");
		const std::vector<std::string> calls(p.screen->buttonCalls().begin() + (std::ptrdiff_t)before, p.screen->buttonCalls().end());
		CHECK(GadgetTextEntryGetText(entry) == u"Ab");
		CHECK(calls == std::vector<std::string>{ "SaveButtonEnable 1", "SaveButtonEnable 1" }); // one refresh per typed character
	}
	SUBCASE("a file-name character is refused by name; a selected save's name goes into the entry")
	{
		GadgetTextEntrySetText(entry, u"a/b");
		p.command("Save");
		CHECK(p.fx.services.countRequests(ShellAction::SaveGameFile) == 0);
		CHECK(p.screen->boxText() == u"A saved game's name cannot contain \\ / : * ? \" < > |");
		p.select(1);
		CHECK(GadgetTextEntryGetText(entry) == u"Saved Game 1");
	}
}
