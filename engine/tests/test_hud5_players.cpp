// OpenBFME unit tests. GPL-3.0.
// Lane HUD-5 (the owner's report: the players screen showed every cell's text record name, "APT:_level3.OuterFrame.Pages.StatusPage.Table.0_field0"): the
// Status page of PlayerTribute.apt (RW 0x91741F -> the row list RW 0x9166AC -> the rows RW 0x915BF6), the command parameters (RW 0x81560E), the rows' texts
// (GUI/PlayerStatusInfo.h) and retail's text for an unknown label (fetchPtr: "MISSING: '<label>'"). The retail tests SKIP loudly without the installs.

#include "doctest.h"
#include "AptRetail.h"
#include "PlatformIconLayout.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GameText.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/PlatformIcons.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cmath>
#include <map>
#include <tuple>
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
		REQUIRE_MESSAGE(text.table.loadRetail(mount.fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
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
	REQUIRE_MESSAGE(text.table.loadRetail(mount.fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
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

TEST_CASE("platformicon1 retail: the players screen's Status page puts each network player's system icon left of its name field, none for a computer or an old peer")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	IniSkirmishSetupSource setup;
	std::string error;
	REQUIRE_MESSAGE(setup.load(mount.fs, &error), error);
	GameTextTableSource text;
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(text.table.loadRetail(mount.fs, &error), error); // data/lotr.str, or a non-English install's lotr.csf
	// the rows come from the game's slots: slot 0 a Linux human, slot 1 a macOS human, slot 2 a human that told nothing, slot 3 a computer (a platform
	// handed to a computer's slot is dropped: display data of humans only)
	SkirmishGameInfo game = ownersGame(setup);
	game.slots[1].state = SLOT_PLAYER;
	game.slots[1].name = u"Mac";
	game.slots[2].state = SLOT_PLAYER;
	game.slots[2].name = u"Old";
	PlayerStatusInput in;
	in.game = &game;
	in.localSlot = 0;
	in.network = true;
	in.platforms[0] = PlatformKind::Linux;
	in.platforms[1] = PlatformKind::MacOS;
	in.platforms[3] = PlatformKind::Windows;
	for (int i = 0; i < 4; ++i)
	{
		in.slots[i].hasPlayer = true;
	}
	PlayerStatusInfo info = makePlayerStatusInfo(in, setup, &text);
	REQUIRE(info.rows.size() == 4);
	CHECK(info.rows[0].platform == PlatformKind::Linux);
	CHECK(info.rows[1].platform == PlatformKind::MacOS);
	CHECK(info.rows[2].platform == PlatformKind::None);
	CHECK(info.rows[3].platform == PlatformKind::None);
	PlayersFx fx(mount);
	fx.environment.playerStatus = &info;
	fx.tick(2);
	fx.shell->push("PlayerTribute.apt");
	fx.tick(60);
	auto *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	REQUIRE(screen->statusRowsShown() == 4);
	CHECK(fx.wm->textBadges().size() == 2);
	// the render list's name fields, each with its icon: the right edge two stage pixels left of the field, inside the stage
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	GadgetDrawList icons;
	appendTextBadges(rl, fx.wm->textBadges(), icons);
	REQUIRE(icons.commands.size() == 2);
	// the render list's order is not the rows' (Table.1 comes before Table.0): each icon is found by the row its field belongs to
	std::map<int, const GadgetDrawCommand *> byRow;
	for (const GadgetDrawCommand &c : icons.commands)
	{
		MESSAGE("icon: " << c.x0 << "," << c.y0 << " - " << c.x1 << "," << c.y1 << " for " << c.window << " " << c.image);
		const std::size_t at = c.window.find(".Table.");
		REQUIRE(at != std::string::npos);
		byRow[std::atoi(c.window.c_str() + at + 7)] = &c;
		CHECK(c.kind == GadgetDrawCommand::Kind::Image);
		CHECK(c.x1 - c.x0 == kPlatformIconStage);
		CHECK(c.y1 - c.y0 == kPlatformIconStage);
	}
	REQUIRE(byRow.size() == 2);
	REQUIRE(byRow.count(0));
	REQUIRE(byRow.count(1));
	CHECK(byRow[0]->image == "OpenBFMEPlatformLinux");
	CHECK(byRow[1]->image == "OpenBFMEPlatformMacOS");
	// the rows' name fields start at stage x 299 (the icon's right edge two pixels before the table's border, 292: the gutter) and are 26 stage pixels apart (measured: the page's layout)
	CHECK(std::make_tuple(byRow[0]->x0, byRow[0]->y0, byRow[0]->x1, byRow[0]->y1) == std::make_tuple(274, 212, 290, 228));
	CHECK(std::make_tuple(byRow[1]->x0, byRow[1]->y0, byRow[1]->x1, byRow[1]->y1) == std::make_tuple(274, 238, 290, 254));
	icons.commands = { *byRow[0], *byRow[1] };
	// the icons overlap no part of the table: the page's frame (the table's border) is at stage x 291.9 and the icon's right edge is two pixels before it
	platformlayout::Rect table;
	REQUIRE(platformlayout::stageRect(platformlayout::clipAtPath(fx.wm->apt(), screen->level(), "OuterFrame.Pages.StatusPage"), table));
	CHECK(std::lround(table.x0) == 292);
	for (const auto &row : byRow)
	{
		const platformlayout::Rect icon{ (float)row.second->x0, (float)row.second->y0, (float)row.second->x1, (float)row.second->y1 };
		CHECK_FALSE(icon.overlaps(table));
		CHECK(row.second->x1 == std::lround(table.x0) - 2);
	}
	// the icons are drawn at different heights (one per row)
	CHECK(icons.commands[0].y0 != icons.commands[1].y0);
	bool noted = false;
	for (const WindowManagerNote &n : fx.wm->notes())
	{
		noted = noted || (n.kind == "platform-icon" && n.detail.find("[S-4850]") != std::string::npos);
	}
	CHECK(noted);
}

TEST_CASE("platformicon1 retail: the score screen puts each network player's system icon left of its name field and none for a computer or an old peer")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	PlayersFx fx(mount);
	ScoreScreenData data;
	data.valid = true;
	data.type = 3;
	const char *names[3] = { "Ann", "Bea", "Computer" };
	const PlatformKind kinds[3] = { PlatformKind::Windows, PlatformKind::None, PlatformKind::None };
	for (int i = 0; i < 3; ++i)
	{
		ScoreScreenData::Entry e;
		e.playerIndex = i + 1;
		e.name = i == 0 ? std::string(24, 'W') : names[i]; // the first player's name is the longest legal one (24 bytes), in the widest letter
		e.side = "Men";
		e.platform = i == 0 ? PlatformKind::Linux : kinds[i];
		e.local = i == 0;
		data.entries.push_back(e);
	}
	data.entries[1].platform = PlatformKind::MacOS;
	fx.environment.scoreScreen = &data;
	fx.tick(2);
	fx.shell->push("TimeLine.apt");
	fx.tick(90);
	REQUIRE(fx.shell->top());
	CHECK(fx.wm->textBadges().size() == 2);
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	GadgetDrawList icons;
	appendTextBadges(rl, fx.wm->textBadges(), icons);
	REQUIRE(icons.commands.size() == 2);
	std::map<std::string, const GadgetDrawCommand *> byName;
	for (const GadgetDrawCommand &c : icons.commands)
	{
		MESSAGE("icon: " << c.x0 << "," << c.y0 << " - " << c.x1 << "," << c.y1 << " " << c.image << " for " << c.window);
		byName[c.image] = &c;
	}
	REQUIRE(byName.count("OpenBFMEPlatformLinux"));
	REQUIRE(byName.count("OpenBFMEPlatformMacOS"));
	CHECK(byName["OpenBFMEPlatformLinux"]->y0 != byName["OpenBFMEPlatformMacOS"]->y0);
	// the standings rows: the faction icon (an unnamed child of the row's button) ends at stage x 753; the icon sits right of it, at the name field's left edge
	for (int row = 0; row < 2; ++row)
	{
		AptCharacterInst *button = platformlayout::clipAtPath(fx.wm->apt(), fx.shell->top()->level(), "Main.Button" + std::to_string(row));
		REQUIRE(button);
		std::vector<AptCharacterInst *> unnamed;
		for (AptCharacterInst *k : button->asSprite()->children())
		{
			unnamed.push_back(k->instName().empty() ? k : nullptr);
		}
		platformlayout::Rect faction;
		bool found = false;
		for (AptCharacterInst *k : unnamed)
		{
			platformlayout::Rect r;
			if (k && platformlayout::stageRect(k, r) && std::lround(r.x1) == 753)
			{
				faction = r;
				found = true;
			}
		}
		REQUIRE(found); // the faction icon's clip
		const GadgetDrawCommand *icon = row == 0 ? byName["OpenBFMEPlatformLinux"] : byName["OpenBFMEPlatformMacOS"];
		const platformlayout::Rect box{ (float)icon->x0, (float)icon->y0, (float)icon->x1, (float)icon->y1 };
		CHECK_FALSE(box.overlaps(faction));
		CHECK(icon->x0 == 759);
		CHECK(icon->x1 == 775);
	}
	// a name as long as the lobby allows (24 W's) never runs under the icon: the field's box is reserved at the icon, whatever the string's width (a wide
	// string is squeezed into the rest of the box, a narrow one centred in it)
	AptTextureStore textures(fx.source);
	AptTextRecordLookup lookup = [&fx](const std::string &name, std::string &utf8) { return fx.wm->aptTextShown(name, utf8); };
	AptCanvasInputs canvasIn;
	canvasIn.textures = &textures;
	canvasIn.text = &fx.text.table;
	canvasIn.textRecords = &lookup;
	canvasIn.textBadges = &fx.wm->textBadges();
	AptCanvasList canvas;
	BuildAptCanvas(rl, canvasIn, canvas);
	int checkedFields = 0;
	for (const AptCanvasOp &op : canvas.ops)
	{
		if (op.kind != AptCanvasOp::Kind::Text || op.path.find(".GUIPlayer") == std::string::npos || op.text.empty())
		{
			continue;
		}
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		op.matrix.apply(op.bounds[0], op.bounds[1], x0, y0);
		op.matrix.apply(op.bounds[2], op.bounds[3], x1, y1);
		const bool badged = op.text == std::string(24, 'W') || op.text == "Bea";
		for (const float width : { 20.0f, 150.0f, 400.0f, 1000.0f }) // a short, a fitting, the 24 W's (about 300 px) and an absurd string
		{
			const AptTextPlacement place = PlaceAptText(x0, y0, x1, y1, width, 14.0f, op.alignment, op.multiline, op.wordWrap);
			const platformlayout::Rect text{ place.x, 0.0f, place.x + width * place.squeezeX, 1.0f };
			const platformlayout::Rect iconBox{ 759.0f, 0.0f, 775.0f, 1.0f };
			if (badged)
			{
				CHECK_MESSAGE(!text.overlaps(iconBox), op.text << " at width " << width << " starts at " << place.x);
			}
		}
		++checkedFields;
	}
	CHECK(checkedFields == 3); // Ann (24 W's), Bea, Medium: the three rows' name fields
}
