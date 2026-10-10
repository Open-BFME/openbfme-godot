// OpenBFME unit tests. GPL-3.0.
// Lane HUD-5 (the owner's report: the players screen showed every cell's text record name, "APT:_level3.OuterFrame.Pages.StatusPage.Table.0_field0"): the
// Status page of PlayerTribute.apt (RW 0x91741F -> the row list RW 0x9166AC -> the rows RW 0x915BF6), the command parameters (RW 0x81560E), the rows' texts
// (GUI/PlayerStatusInfo.h) and retail's text for an unknown label (fetchPtr: "MISSING: '<label>'"). The retail tests SKIP loudly without the installs.

#include "doctest.h"
#include "AptRetail.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GameText.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <memory>
#include <string>

namespace
{
struct PlayersFx
{
	AptArchiveFileSource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	GameTextTableSource text;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;

	explicit PlayersFx(AptRetail &mount) : source(mount.fs)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		environment.gameText = &text;
		registerAptScreenFactories(factories);
		REQUIRE_MESSAGE(loadGadgetSkinData(mount.fs, skins, &error), error);
		wm = std::make_unique<WindowManager>(source, services);
		layer = std::make_unique<AptGadgetLayer>(*wm, source, skins);
		layer->registerComponents();
		shell = std::make_unique<Shell>(*wm, factories, services, environment);
		wm->setShell(shell.get());
		wm->init();
	}
	~PlayersFx()
	{
		shell.reset();
		layer.reset();
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			wm->update(33);
		}
	}
	// the resolved texts of the movie's text fields now (the canvas rule: ResolveAptText with the window manager's records)
	std::vector<std::pair<std::string, std::string>> shownTexts()
	{
		AptRenderList rl;
		wm->apt().buildRenderList(rl);
		const AptTextRecordLookup records = [this](const std::string &name, std::string &utf8) { return wm->aptTextShown(name, utf8); };
		std::vector<std::pair<std::string, std::string>> out;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Text)
			{
				out.emplace_back(c.path, ResolveAptText(c.text, text.table, &records).text);
			}
		}
		return out;
	}
};

// a 4-player skirmish of the owner's report: Helm's Deep, the human at slot 0 (Mordor), three Easy computers
SkirmishGameInfo ownersGame(const IniSkirmishSetupSource &setup)
{
	SkirmishGameInfo g;
	auto faction = [&](const char *name) {
		for (size_t i = 0; i < setup.factions().size(); ++i)
		{
			if (setup.factions()[i].templateName == name)
			{
				return (int)i;
			}
		}
		return -1;
	};
	const char *factions[4] = { "FactionMordor", "FactionMen", "FactionElves", "FactionIsengard" };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &s = g.slots[i];
		s.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		s.name = i == 0 ? u"hey" : u"Easy";
		s.playerTemplate = faction(factions[i]);
		s.color = i;
		s.startPos = i;
		s.teamNumber = -1;
		s.origPlayerTemplate = i == 3 ? PLAYERTEMPLATE_RANDOM : s.playerTemplate; // slot 3 chose Random in the lobby
		s.origColor = s.color;
	}
	return g;
}
} // namespace

TEST_CASE("hud5 players: the command parameters as RW 0x81560E reads them; the level helpers RW 0x8155D9 / 0x815563")
{
	std::string v;
	CHECK(AptPlayerTribute::commandParam("index=3&name=_level4.OuterFrame.Row3", "index", v));
	CHECK(v == "3");
	CHECK(AptPlayerTribute::commandParam("index=3&name=_level4.OuterFrame.Row3", "name", v));
	CHECK(v == "_level4.OuterFrame.Row3");
	CHECK(AptPlayerTribute::commandParam(" name = x & type=StatusPage", "type", v)); // spaces skipped before the key and after '='
	CHECK(v == "StatusPage");
	CHECK(AptPlayerTribute::commandParam("a=&b=2", "a", v));
	CHECK(v.empty());
	CHECK_FALSE(AptPlayerTribute::commandParam("index=3", "name", v));
	CHECK_FALSE(AptPlayerTribute::commandParam("", "name", v));
	CHECK(AptPlayerTribute::levelIndexFromTarget("_level3.OuterFrame") == 3);
	CHECK(AptPlayerTribute::levelIndexFromTarget("OuterFrame") == -1);
	CHECK(AptPlayerTribute::skipLevelN("_level3.OuterFrame.Pages.StatusPage") == "OuterFrame.Pages.StatusPage");
	CHECK(AptPlayerTribute::skipLevelN("_level12/Clip") == "Clip");
	CHECK(AptPlayerTribute::skipLevelN("Clip.Sub") == "Clip.Sub");
}

TEST_CASE("hud5 players: an Apt label with no text and no record shows retail's \"MISSING: '<label>'\", never the bare record name")
{
	GameTextTable t; // empty: no label has a text
	const AptTextResolution r = ResolveAptText("$_level3.OuterFrame.Pages.StatusPage.Table.0_field0", t);
	CHECK_FALSE(r.found);
	CHECK(r.label == "APT:_level3.OuterFrame.Pages.StatusPage.Table.0_field0");
	CHECK(r.text == "MISSING: 'APT:_level3.OuterFrame.Pages.StatusPage.Table.0_field0'");
}

TEST_CASE("hud5 players retail: the Status rows of the owner's game: names, armies (an enemy's Random), teams, statuses, colours")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	IniSkirmishSetupSource setup;
	std::string error;
	REQUIRE_MESSAGE(setup.load(mount.fs, &error), error);
	GameTextTableSource text;
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(mount.fs.readFile("data/lotr.str", bytes, &error), error);
	REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
	const SkirmishGameInfo game = ownersGame(setup);
	PlayerStatusInput in;
	in.game = &game;
	in.localSlot = 0;
	for (int i = 0; i < 4; ++i)
	{
		in.slots[i].hasPlayer = true;
	}
	in.slots[2].defeated = true;
	const PlayerStatusInfo info = makePlayerStatusInfo(in, setup, &text);
	REQUIRE(info.rows.size() == 4);
	CHECK(info.inSkirmish);
	auto label = [&](const char *l) {
		std::u16string u;
		REQUIRE_MESSAGE(text.fetch(l, u), l);
		return loadScreenU16ToUtf8(u);
	};
	CHECK(info.rows[0].fields[0] == "hey");
	CHECK(info.rows[0].fields[1] == label("INI:FactionMordor"));
	CHECK(info.rows[1].fields[1] == label("INI:FactionMen"));
	CHECK(info.rows[3].fields[1] == label("GUI:Random")); // an enemy's random faction stays hidden (ShowRandomPlayerTemplate)
	CHECK(info.rows[1].fields[2] == "CPU");             // Team:AI: a computer without a team
	CHECK(info.rows[0].fields[2] == "-");               // Team:0: a human without a team
	CHECK(info.rows[0].fields[3] == label("GUI:PlayerAlive"));
	CHECK(info.rows[2].fields[3] == label("GUI:PlayerDead"));
	CHECK(info.rows[0].color == (std::int32_t)(0xFF000000u | setup.colors()[0].rgb));
	for (const PlayerStatusRow &row : info.rows)
	{
		for (const std::string &f : row.fields)
		{
			CHECK(f.find("MISSING") == std::string::npos);
			CHECK(f.find("APT:") == std::string::npos);
		}
	}
	// teams: "Team:<team + 1>"
	SkirmishGameInfo teams = game;
	teams.slots[0].teamNumber = 1;
	in.game = &teams;
	CHECK(makePlayerStatusInfo(in, setup, &text).rows[0].fields[2] == label("Team:2"));
}

TEST_CASE("hud5 players retail: PlayerTribute.apt loads its Status page, shows a row per player with the engine's texts, no record names, and blank unused rows")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PlayersFx fx(mount);
	PlayerStatusInfo info;
	info.inSkirmish = true;
	const char *rowNames[4] = { "hey", "Easy", "Easy", "Easy" };
	for (int i = 0; i < 4; ++i)
	{
		PlayerStatusRow r;
		r.slot = i;
		r.fields[0] = rowNames[i];
		r.fields[1] = "Army" + std::to_string(i);
		r.fields[2] = "-";
		r.fields[3] = "Alive";
		r.color = (std::int32_t)0xFF00FF00u;
		info.rows.push_back(r);
	}
	fx.environment.playerStatus = &info;
	fx.tick(2);
	fx.shell->push("PlayerTribute.apt");
	fx.tick(60);
	auto *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		MESSAGE("note " << n.kind << ": " << n.detail);
	}
	CHECK(screen->statusPageLoaded());
	CHECK(screen->statusRowsShown() == 4);
	int names = 0, recordNames = 0;
	for (const auto &t : fx.shownTexts())
	{
		if (t.second.find("APT:") != std::string::npos || t.second.find("MISSING") != std::string::npos)
		{
			++recordNames;
			MESSAGE("record name shown: " << t.first << " = " << t.second);
		}
		names += t.second == "hey" ? 1 : 0;
	}
	CHECK(recordNames == 0);
	CHECK(names == 1);
	// the records the rows wrote (RW 0x915159: "APT:_level%u.%s_field%d")
	int written = 0;
	for (int i = 0; i < 4; ++i)
	{
		for (const auto &t : fx.shownTexts())
		{
			written += t.second == "Army" + std::to_string(i) ? 1 : 0;
		}
	}
	CHECK(written == 4);
}
