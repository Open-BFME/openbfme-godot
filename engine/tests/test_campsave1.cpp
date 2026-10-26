// OpenBFME unit tests. GPL-3.0.
// Lane CAMPSAVE-1: the campaign between missions as RotWK runs it (GameClient/LinearCampaign.h CampaignProgress: the flags + 0xD0 / + 0xD2 / + 0xD3, the end of
// the score screen RW 0x927EC6, the menu's mission RW 0x5EADDE, Next / Replay Mission RW 0x5EAE37 / 0x5EC5D1), the campaign menu save point of the save file
// (header version 2, Common/System/SaveGame/SaveGameFile.h), CampaignMenu.apt on the retail movie (GUI/AptScreens/AptCampaignMenu.h), the campaign's
// auto-save on SaveLoad.apt's AutoSaveList and the main menu's campaign providers. The retail cases SKIP loudly without the installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/Sha256.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/EndGame.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptCampaignMenu.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/LinearCampaign.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
// three missions of a campaign parsed from INI text (the LinearCampaign block, RW 0x5ECCB9)
struct ThreeMissions
{
	LinearCampaignManager manager;
	ThreeMissions()
	{
		INIEnvironment env;
		manager.registerBlock(env.blocks);
		LinearCampaignManager *previous = TheLinearCampaignManager;
		TheLinearCampaignManager = &manager;
		const std::string text = "LinearCampaign CS1\n"
								 "  Mission One\n    Map = \"MAP CS1 One\"\n    LoadScreenImage = ImageOne\n  End\n"
								 "  Mission Two\n    Map = \"MAP CS1 Two\"\n    LoadScreenImage = ImageTwo\n  End\n"
								 "  Mission Three\n    Map = \"MAP CS1 Three\"\n    LoadScreenImage = ImageThree\n  End\n"
								 "End\n";
		INI ini(env);
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		ini.loadMemory("Data\\INI\\campsave1.ini", bytes, INI_LOAD_OVERWRITE);
		TheLinearCampaignManager = previous;
	}
};

CampaignProgress at(int mission)
{
	CampaignProgress p;
	p.campaign = "CS1";
	p.mission = mission;
	p.difficulty = 2;
	p.missionStarted();
	return p;
}

ProfileIdentity identity()
{
	return ProfileIdentity::fromText(std::string("openbfme-profile 1\nprofile enhanced\nengine ") + ProfileIdentity::buildId() +
		"\nrng ZH_CarryChain\nmods none\narchive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 100\n");
}

// a campaign menu save as GameWorld::save_campaign_menu writes it
SaveGameFile menuSave()
{
	SaveGameFile f;
	SaveGameHeader &h = f.header;
	h.buildVersion = "test";
	h.engineId = ProfileIdentity::buildId();
	h.profile = identity();
	h.executionProfile = "enhanced";
	h.gameKind = 1;
	h.savePoint = 1;
	h.description = "__AUTO#SAVE__";
	h.mapPath = "maps/map ang rhudaur/map ang rhudaur.map";
	h.mapDisplay = "Rhudaur";
	h.date = "2026-10-14";
	h.time = "23:00:00";
	h.campaign = "ANGMAR_CAMPAIGN";
	h.campaignMission = 1;
	h.campaignDifficulty = 1;
	h.campaignVictorious = true;
	h.campaignReady = true;
	h.campaignAdvanced = true;
	h.autoSave = true;
	return f;
}

std::string parseError(const std::vector<std::uint8_t> &bytes)
{
	SaveGameFile out;
	std::string error;
	CHECK_FALSE(SaveGameFile::parse(bytes, out, &error));
	return error;
}

// the HEAD part's payload replaced (its length and digest recomputed): bytes 12 .. are tag, u32 length, 32-byte digest, payload
std::vector<std::uint8_t> withHead(const std::vector<std::uint8_t> &bytes, const std::vector<std::uint8_t> &payload)
{
	std::uint32_t oldLength = 0;
	std::memcpy(&oldLength, bytes.data() + 16, 4);
	std::vector<std::uint8_t> out(bytes.begin(), bytes.begin() + 12);
	out.insert(out.end(), bytes.begin() + 12, bytes.begin() + 16);
	const std::uint32_t length = (std::uint32_t)payload.size();
	out.insert(out.end(), (const std::uint8_t *)&length, (const std::uint8_t *)&length + 4);
	const std::array<std::uint8_t, 32> d = Sha256::hash(payload.data(), payload.size());
	out.insert(out.end(), d.begin(), d.end());
	out.insert(out.end(), payload.begin(), payload.end());
	out.insert(out.end(), bytes.begin() + 52 + oldLength, bytes.end());
	return out;
}

std::vector<std::uint8_t> headOf(const std::vector<std::uint8_t> &bytes)
{
	std::uint32_t length = 0;
	std::memcpy(&length, bytes.data() + 16, 4);
	return std::vector<std::uint8_t>(bytes.begin() + 52, bytes.begin() + 52 + length);
}

// the retail string table (lotr.str) as the shell's game text
struct RetailText
{
	GameTextTableSource text;
	explicit RetailText(starttest::Shared &s)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
	}
	std::string get(const std::string &label)
	{
		std::string out;
		return text.table.lookup(label, out) ? out : std::string("<missing ") + label + ">";
	}
};

// CampaignMenu.apt pushed on the real shell with `content` (the host's CampaignMenuContext), its intro played
struct Menu
{
	starttest::ShellFx fx;
	RetailText text;
	CampaignMenuContext content;
	AptCampaignMenu *screen = nullptr;
	Menu(starttest::Shared &s, const CampaignMenuContext &c) : fx(s, 7), text(s), content(c)
	{
		fx.environment.gameText = &text.text;
		fx.environment.services = &fx.services;
		fx.environment.campaignMenu = &content;
		fx.shell->push("CampaignMenu.apt");
		REQUIRE(fx.shell->errors().empty());
		screen = dynamic_cast<AptCampaignMenu *>(fx.shell->top());
		REQUIRE(screen);
		fx.tick(90); // the _open animation
	}
	AptCharacterInst *button(const std::string &name) { return fx.at(screen->level(), "Buttons." + name); }
};
} // namespace

TEST_CASE("campsave1 CampaignProgress: a mission's flags (RW 0x5EAB8F / 0x5EBAEE), the game's end (RW 0x5EAB0A), the score screen's end (RW 0x927EC6), "
		  "the menu's mission (RW 0x5EADDE), Next Mission (RW 0x5EAE37) and Replay Mission (RW 0x5EC5D1)")
{
	ThreeMissions t;
	const LinearCampaignManager &m = t.manager;
	REQUIRE(m.find("CS1") != nullptr);
	REQUIRE(m.find("CS1")->missions.size() == 3);
	SUBCASE("a won mission that is not the last: the index moves on once, the auto-save and the menu name the next mission, Next Mission plays it with its IntroMovie")
	{
		CampaignProgress p = at(0);
		CHECK_FALSE(p.victorious);
		CHECK_FALSE(p.ready);
		CHECK_FALSE(p.advanced);
		CHECK(p.menuMissionIndex(m) == 0);
		bool intro = false;
		CHECK_FALSE(p.nextMission(&intro)); // RW 0x5EAE37: nothing before + 0xD2
		CHECK(p.mission == 0);
		p.victorious = true; // RW 0x5EBAEE
		CHECK(p.menuMissionIndex(m) == 1);   // won, not advanced: mission + 1
		p.gameOver();                        // RW 0x5EAB0A
		CHECK(p.ready);
		CHECK(p.advanced);
		CHECK(p.mission == 1);
		CHECK(p.menuMissionIndex(m) == 1);   // advanced: the index itself
		p.gameOver();                        // a second call does not move it again (+ 0xD3)
		CHECK(p.mission == 1);
		CHECK(p.afterScore(m) == CampaignProgress::AfterScore::AutoSaveMenu);
		CHECK(p.nextMission(&intro));
		CHECK(intro);
		CHECK(p.mission == 1); // already advanced
		p.missionStarted();
		CHECK_FALSE(p.victorious);
		CHECK_FALSE(p.ready);
		CHECK_FALSE(p.advanced);
	}
	SUBCASE("a lost mission: no auto-save, the menu names the same mission, Replay Mission plays it again without its IntroMovie, Next Mission moves on")
	{
		CampaignProgress p = at(1);
		p.gameOver();
		CHECK(p.ready);
		CHECK_FALSE(p.advanced);
		CHECK(p.mission == 1);
		CHECK(p.afterScore(m) == CampaignProgress::AfterScore::Menu);
		CHECK(p.menuMissionIndex(m) == 1);
		CampaignProgress replay = p;
		bool intro = true;
		replay.lastMission(&intro);
		CHECK_FALSE(intro);
		CHECK(replay.mission == 1);
		CampaignProgress next = p;
		CHECK(next.nextMission(&intro)); // retail's movie hides Next Mission after a defeat; the command itself moves on (RW 0x5EAE37)
		CHECK(intro);
		CHECK(next.mission == 2);
	}
	SUBCASE("Replay Mission after a won one (RW 0x5EC5D1 -> RW 0x5EADC3): the next mission, with its IntroMovie")
	{
		CampaignProgress p = at(0);
		p.victorious = true;
		bool intro = false;
		p.lastMission(&intro);
		CHECK(intro);
		CHECK(p.mission == 1);
		CHECK(p.advanced);
	}
	SUBCASE("the last mission won: the bonus unlock, no menu (RW 0x5EAE75 -> RW 0x5EAB6E)")
	{
		CampaignProgress p = at(2);
		CHECK(p.isFinalMission(m));
		p.victorious = true;
		p.gameOver(); // the index moves past the list
		CHECK(p.mission == 3);
		CHECK(p.isFinalMission(m)); // advanced: the index itself is past the list
		CHECK(p.afterScore(m) == CampaignProgress::AfterScore::BonusUnlock);
		CHECK(p.menuMissionIndex(m) == -1);
		CampaignProgress lost = at(2);
		lost.gameOver();
		CHECK(lost.afterScore(m) == CampaignProgress::AfterScore::Menu); // the last mission lost: the menu, Replay Mission
		CampaignProgress none;
		none.campaign = "NO_SUCH_CAMPAIGN";
		CHECK(none.isFinalMission(m)); // RW 0x5EAE75: no campaign answers true
		CHECK(none.menuMissionIndex(m) == -1);
	}
}

TEST_CASE("campsave1 the score screen of a campaign game is type 1 (RW 0x927898: the single player modes 0 / 6, RW 0x6253BF), which RW 0x926E0A tests")
{
	CHECK(ScoreScreenData::typeFor(0, false) == 1);
	CHECK(ScoreScreenData::typeFor(6, false) == 1);
	CHECK(ScoreScreenData::typeFor(2, false) == 2);
	CHECK(ScoreScreenData::typeFor(0, true) == 3);
}

TEST_CASE("campsave1 save file: the campaign menu save point (header version 2) reads back; its closed sets and its 'no game' rule are refused by name; "
		  "a version 1 header still loads")
{
	const SaveGameFile f = menuSave();
	const std::vector<std::uint8_t> bytes = f.encode();
	SaveGameFile back;
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::parse(bytes, back, &error), error);
	CHECK(back.header.savePoint == 1);
	CHECK(back.header.campaign == "ANGMAR_CAMPAIGN");
	CHECK(back.header.campaignMission == 1);
	CHECK(back.header.campaignVictorious);
	CHECK(back.header.campaignReady);
	CHECK(back.header.campaignAdvanced);
	CHECK(back.header.autoSave);
	CHECK(back.log.empty());
	CHECK(back.snapshot.empty());
	SaveGameFile headerOnly;
	REQUIRE_MESSAGE(SaveGameFile::parse(bytes, headerOnly, &error, true), error);
	CHECK(headerOnly.header.autoSave);

	const std::vector<std::uint8_t> head = headOf(bytes);
	REQUIRE(head.size() > 12); // tolerance: guard before indexing the save point and flags preceding the empty build date
	const size_t point = head.size() - 12; // u32 save point, four flags, then the empty build-date string
	{
		std::vector<std::uint8_t> h = head;
		h[point] = 2;
		CHECK(parseError(withHead(bytes, h)) == "the save's save point 2 is not one this build saves (0 a game, 1 the campaign menu)");
	}
	for (int i = 0; i < 4; ++i)
	{
		std::vector<std::uint8_t> h = head;
		h[point + 4 + (size_t)i] = 7;
		CHECK(parseError(withHead(bytes, h)) == "the save's campaign flag " + std::to_string(i) + " is 7 (0 or 1)");
	}
	{
		std::vector<std::uint8_t> h(head.begin(), head.end() - 3); // the header ends inside the flags
		CHECK(parseError(withHead(bytes, h)) == "the save is damaged (its header part ends early)");
	}
	SaveGameFile withLog = menuSave();
	withLog.log = { 1, 2, 3 };
	CHECK(parseError(withLog.encode()) == "the campaign menu save carries a game (a frame, commands, a log or a snapshot)");
	SaveGameFile withFrame = menuSave();
	withFrame.header.frame = 9;
	CHECK(parseError(withFrame.encode()) == "the campaign menu save carries a game (a frame, commands, a log or a snapshot)");
	SaveGameFile withSnap = menuSave();
	withSnap.snapshot = { 1 };
	CHECK(parseError(withSnap.encode()) == "the campaign menu save carries a game (a frame, commands, a log or a snapshot)");
	SaveGameFile range = menuSave();
	range.header.campaignDifficulty = 3;
	CHECK(parseError(range.encode()) == "the campaign menu save's mission 1 / difficulty 3 is out of range");
	range.header.campaignDifficulty = 1;
	range.header.campaignMission = -1;
	CHECK(parseError(range.encode()) == "the campaign menu save's mission -1 / difficulty 1 is out of range");
	SaveGameFile game = menuSave();
	game.header.savePoint = 0; // a game at its frame with the menu's flags
	game.log = { 1 };
	CHECK(parseError(game.encode()) == "the save is a game at its frame with the campaign menu's flags");
	SaveGameFile skirmishMenu = menuSave();
	skirmishMenu.header.gameKind = 2;
	skirmishMenu.header.campaign.clear();
	CHECK(parseError(skirmishMenu.encode()) == "the save is a campaign menu save without its campaign");

	// a version 1 header (lanes SAVE-2 .. SAVE-4): no save point, no flags; it reads as a game at its frame
	SaveGameFile v1 = menuSave();
	v1.header.savePoint = 0;
	v1.header.campaignVictorious = v1.header.campaignReady = v1.header.campaignAdvanced = v1.header.autoSave = false;
	v1.header.frame = 120;
	v1.log = { 9, 9 };
	const std::vector<std::uint8_t> v2bytes = v1.encode();
	std::vector<std::uint8_t> h1 = headOf(v2bytes);
	h1.resize(h1.size() - 12);
	h1[0] = 1; // the header version word
	SaveGameFile old;
	REQUIRE_MESSAGE(SaveGameFile::parse(withHead(v2bytes, h1), old, &error), error);
	CHECK(old.header.savePoint == 0);
	CHECK(old.header.frame == 120);
	CHECK_FALSE(old.header.autoSave);
	CHECK(old.log == std::vector<std::uint8_t>{ 9, 9 });
	CHECK(old.header.buildDate.empty());
	auto h2 = headOf(v2bytes);
	h2.resize(h2.size() - 4);
	h2[0] = 2;
	REQUIRE_MESSAGE(SaveGameFile::parse(withHead(v2bytes, h2), old, &error), error);
	CHECK(old.header.buildDate.empty());
}

TEST_CASE("campsave1 retail: ANGMAR_CAMPAIGN through RotWK's end-of-game logic: each won mission auto-saves and names the next one on the menu "
		  "(its CampaignName: label in lotr.str, its LoadScreenImage), the eighth unlocks the bonus campaign; ANGMAR_BONUS_CAMPAIGN's one mission unlocks it too")
{
	OPENBFME_REQUIRE_START(s);
	RetailText text(*s);
	const LinearCampaignManager &m = s->world->linearCampaigns();
	const LinearCampaign *c = m.find("ANGMAR_CAMPAIGN");
	REQUIRE(c != nullptr);
	REQUIRE(c->missions.size() == 8);
	CampaignProgress p;
	p.campaign = c->name;
	p.mission = 0;
	std::vector<std::string> shown;
	int autoSaves = 0;
	for (int i = 0; i < 8; ++i)
	{
		INFO("mission " << i << " " << c->missions[(size_t)i].name);
		p.missionStarted();
		CHECK(p.mission == i);
		CHECK(text.get("CampaignName:" + c->missions[(size_t)i].name).find("<missing") == std::string::npos);
		p.victorious = true;
		p.gameOver();
		const CampaignProgress::AfterScore a = p.afterScore(m);
		if (i < 7)
		{
			REQUIRE(a == CampaignProgress::AfterScore::AutoSaveMenu);
			++autoSaves;
			const int next = p.menuMissionIndex(m);
			REQUIRE(next == i + 1);
			CHECK_FALSE(c->missions[(size_t)next].loadScreenImage.empty());
			shown.push_back(text.get("CampaignName:" + c->missions[(size_t)next].name));
			bool intro = false;
			REQUIRE(p.nextMission(&intro));
			CHECK(intro);
		}
		else
		{
			CHECK(a == CampaignProgress::AfterScore::BonusUnlock);
		}
	}
	CHECK(autoSaves == 7);
	// the names the menu shows after missions 1 .. 7 (lotr.str)
	CHECK(shown == std::vector<std::string>{ "Conquest of Rhudaur", "Amon Sul", "Dark Lord's Eye", "Barrow-downs", "Carn Dum", "Plague Bearer", "Fornost" });
	const LinearCampaign *bonus = m.find("ANGMAR_BONUS_CAMPAIGN");
	REQUIRE(bonus != nullptr);
	CampaignProgress b;
	b.campaign = bonus->name;
	b.missionStarted();
	b.victorious = true;
	b.gameOver();
	CHECK(b.afterScore(m) == CampaignProgress::AfterScore::BonusUnlock);
	CHECK(text.get("CampaignName:" + bonus->missions[0].name) == "Fall of Angmar");
}

TEST_CASE("campsave1 retail: CampaignMenu.apt after a won mission: the five commands and CampaignMenu:Victorious registered (RW 0x927B9F), the next "
		  "mission's name and picture, Next Mission / Save / Load / Main Menu shown, Replay Mission hidden; each button posts its request")
{
	OPENBFME_REQUIRE_START(s);
	CampaignMenuContext won;
	won.haveMission = true;
	won.missionName = "Rhudaur";
	won.loadScreenImage = "CampaignAngmar_Rhudaur_Loadscreen";
	won.victorious = true;
	Menu menu(*s, won);
	for (const std::string &name : AptCampaignMenu::retailNames())
	{
		if (name == "CampaignMenu:Victorious")
		{
			CHECK(menu.fx.wm->hasProvider(name));
		}
		else
		{
			CHECK_MESSAGE(menu.fx.wm->hasCommand(name), name);
		}
	}
	std::string value;
	CHECK(menu.fx.wm->getExtern("CampaignMenu:Victorious", value) == AptExternResult::Number);
	CHECK(value == "1");
	CHECK(menu.screen->missionText() == "Conquest of Rhudaur");
	std::string record;
	CHECK(menu.fx.wm->aptTextShown("APT:CampaignMissionName", record));
	CHECK(record == "Conquest of Rhudaur");
	const std::string *image = menu.fx.wm->aptImage("AptCampaign::LoadImage");
	REQUIRE(image != nullptr);
	CHECK(*image == "CampaignAngmar_Rhudaur_Loadscreen");
	// the movie's own logic (sprite 21 / 16): won -> LastMission hidden, "Next Mission:" over the name
	for (const char *b : { "NextMission", "MainMenu", "SaveGame", "LoadGame" })
	{
		AptCharacterInst *c = menu.button(b);
		REQUIRE_MESSAGE(c, b);
		CHECK_MESSAGE(c->visible, b);
	}
	REQUIRE(menu.button("LastMission"));
	CHECK_FALSE(menu.button("LastMission")->visible);
	// the movie's OnInitialized has no handler in RotWK either (RW 0x927B9F registers the five buttons only): the one unhandled command
	CHECK(menu.fx.wm->noteCount("command-unhandled") == 1);
	for (const WindowManagerNote &n : menu.fx.wm->notes())
	{
		if (n.kind == "command-unhandled")
		{
			CHECK(n.detail.find("AptCampaignMenu::OnInitialized") != std::string::npos);
		}
	}
	CHECK(menu.fx.wm->noteCount("extern-read-unhandled") == 0);
	// a real click on Next Mission: the movie disables the others, runs OnBttnNextMission and plays _close
	AptButtonInst *next = starttest::firstButtonIn(menu.button("NextMission"));
	REQUIRE(next);
	starttest::clickButton(menu.fx, *next);
	menu.fx.tick(60); // the button's release animation runs to its frame 58 before it calls OnButtonClick (MenuExport.apt sprite 281)
	if (menu.fx.services.requests.size() != 1)
	{
		for (const WindowManagerNote &n : menu.fx.wm->notes())
		{
			MESSAGE("note " << n.kind << ": " << n.detail);
		}
		for (const std::string &t : menu.fx.wm->traces())
		{
			MESSAGE("trace " << t);
		}
	}
	REQUIRE(menu.fx.services.requests.size() == 1);
	CHECK(menu.fx.services.requests[0].action == ShellAction::CampaignMenuNextMission);
	CHECK(menu.screen->commandCalls("AptCampaignMenu::OnBttnNextMission") == 1);
	// the other commands as the movie calls them
	for (const auto &c : std::vector<std::pair<std::string, ShellAction>>{ { "OnBttnSaveGame", ShellAction::CampaignMenuSave },
			 { "OnBttnLoadGame", ShellAction::CampaignMenuLoad }, { "OnBttnLastMission", ShellAction::CampaignMenuLastMission },
			 { "OnBttnMainMenu", ShellAction::CampaignMenuMainMenu } })
	{
		menu.fx.services.requests.clear();
		menu.fx.wm->fscommand("AptCampaignMenu::" + c.first, "");
		REQUIRE(menu.fx.services.requests.size() == 1);
		CHECK(menu.fx.services.requests[0].action == c.second);
	}
}

TEST_CASE("campsave1 retail: CampaignMenu.apt after a defeat: \"Failed Mission:\", Replay Mission / Load / Main Menu shown, Next Mission and Save hidden; "
		  "without the host's content the screen says so")
{
	OPENBFME_REQUIRE_START(s);
	CampaignMenuContext lost;
	lost.haveMission = true;
	lost.missionName = "Amonsul";
	lost.loadScreenImage = "CampaignAngmar_AmonSul_LoadScreen";
	lost.victorious = false;
	Menu menu(*s, lost);
	std::string value;
	CHECK(menu.fx.wm->getExtern("CampaignMenu:Victorious", value) == AptExternResult::Number);
	CHECK(value == "0");
	CHECK(menu.screen->missionText() == "Amon Sul");
	for (const char *b : { "LastMission", "MainMenu", "LoadGame" })
	{
		REQUIRE_MESSAGE(menu.button(b), b);
		CHECK_MESSAGE(menu.button(b)->visible, b);
	}
	for (const char *b : { "NextMission", "SaveGame" })
	{
		REQUIRE_MESSAGE(menu.button(b), b);
		CHECK_FALSE_MESSAGE(menu.button(b)->visible, b);
	}
	AptButtonInst *replay = starttest::firstButtonIn(menu.button("LastMission"));
	REQUIRE(replay);
	starttest::clickButton(menu.fx, *replay);
	menu.fx.tick(60);
	REQUIRE(menu.fx.services.requests.size() == 1);
	CHECK(menu.fx.services.requests[0].action == ShellAction::CampaignMenuLastMission);

	starttest::ShellFx bare(*s, 8);
	bare.environment.services = &bare.services;
	bare.shell->push("CampaignMenu.apt");
	CHECK(bare.wm->noteCount("campaign-menu-unwired") == 1);
}

TEST_CASE("campsave1 retail: SaveLoad.apt's campaign page lists the campaign's auto-save in the AutoSaveList (populateSaveGameListbox RW 0x6E04AD) and "
		  "Load takes it from there")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 12);
	fx.text.set("GUI:NewSaveGame", "**NEW SAVE FILE**");
	fx.text.set("GUI:DefaultSaveFileName", "Saved Game %d");
	SaveLoadInfo info;
	info.mode = 2;
	info.flags = 1; // the campaign menu's Load (RW 0x927AA4: 2, 1, 0)
	info.inGame = false;
	SaveLoadInfo::Save automatic;
	automatic.path = "/s/Auto Save for Rhudaur.obfmesave";
	automatic.fileNameUtf8 = "Auto Save for Rhudaur";
	automatic.mapUtf8 = "Rhudaur";
	automatic.dateUtf8 = "2026-10-14";
	automatic.timeUtf8 = "23:00:00";
	automatic.gameKind = 1;
	automatic.loadable = true;
	automatic.autoSave = true;
	info.saves.push_back(automatic);
	fx.environment.saveLoad = &info;
	fx.environment.services = &fx.services;
	fx.shell->push("SaveLoad.apt");
	AptSaveLoad *screen = dynamic_cast<AptSaveLoad *>(fx.shell->top());
	REQUIRE(screen);
	for (int i = 0; i < 120 && screen->rows().empty(); ++i)
	{
		fx.tick(1);
	}
	fx.tick(10);
	REQUIRE(screen->autoSaveList() != nullptr);
	REQUIRE(screen->rows().size() == 1);
	CHECK(screen->rows()[0] == "AutoSaveList|Rhudaur|Auto Save for Rhudaur|23:00:00|2026-10-14|FFEFCC52");
	CHECK(GadgetListBoxGetNumEntries(screen->gameList()) == 0);
	int row = -1;
	GadgetListBoxGetSelected(screen->autoSaveList(), &row);
	CHECK(row == 0); // the GameList is empty: the AutoSaveList's first row is selected
	fx.wm->fscommand("AptSaveLoad::Load", "");
	fx.tick(3);
	REQUIRE(fx.services.requests.size() == 1);
	CHECK(fx.services.requests[0].action == ShellAction::LoadGameFile);
	CHECK(fx.services.requests[0].argument == automatic.path);
}

TEST_CASE("campsave1 retail: the main menu's MainMenuUnlockBonusCampaign is TheGlobalData + 0xAF5 (RW 0x91BBAD) and MainMenuContinueCampaign whether the "
		  "Save folder holds 00000000.sav (RW 0x91BBBB)")
{
	OPENBFME_REQUIRE_START(s);
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openbfme-campsave1-save";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	starttest::ShellFx fx(*s, 13);
	fx.environment.services = &fx.services;
	fx.environment.saveFolder = dir.u8string();
	fx.shell->push("MainMenu.apt");
	fx.tick(3);
	std::string value;
	CHECK(fx.wm->getExtern("MainMenuUnlockBonusCampaign", value) == AptExternResult::Number);
	CHECK(value == "0");
	CHECK(fx.wm->getExtern("MainMenuContinueCampaign", value) == AptExternResult::Number);
	CHECK(value == "0");
	fx.environment.bonusCampaignUnlocked = true;
	{
		std::ofstream(dir / "00000000.sav") << "x";
	}
	CHECK(fx.wm->getExtern("MainMenuUnlockBonusCampaign", value) == AptExternResult::Number);
	CHECK(value == "1");
	CHECK(fx.wm->getExtern("MainMenuContinueCampaign", value) == AptExternResult::Number);
	CHECK(value == "1");
	CHECK(fx.wm->noteCount("provider-unwired") == 0);
	CHECK_FALSE(AptMainMenu::continueCampaignSaveExists(""));
	std::filesystem::remove_all(dir);
}

TEST_CASE("campsave1 menu save compatibility (review): one verdict for the listing and the load: another build's save loads, other data or a mission that does not "
		  "exist is refused by name")
{
	ThreeMissions t;
	const LinearCampaignManager &m = t.manager;
	SaveGameHeader h = menuSave().header;
	h.campaign = "CS1";
	h.campaignMission = 0;
	h.campaignVictorious = false;
	h.campaignReady = true;
	h.campaignAdvanced = false;
	auto verdict = [&](const SaveGameHeader &x, const ProfileIdentity &running) {
		CampaignProgress p;
		p.campaign = x.campaign;
		p.mission = x.campaignMission;
		p.difficulty = x.campaignDifficulty;
		p.victorious = x.campaignVictorious;
		p.ready = x.campaignReady;
		p.advanced = x.campaignAdvanced;
		return SaveGameCompatibility::checkMenuSave(x, running, p.menuSaveRefusal(m));
	};
	const ProfileIdentity running = identity();
	CHECK(verdict(h, running).loadable);
	SaveGameHeader older = h; // another build (its engine id and execution profile differ): a menu save has no log, so it loads
	older.profile = ProfileIdentity::fromText(std::string("openbfme-profile 1\nprofile uber\nengine 111111111111\nrng ZH_CarryChain\nmods none\n"
														  "archive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 100\n"));
	older.engineId = "111111111111";
	CHECK(verdict(older, running).loadable);
	SaveGameHeader other = h; // other game data: an archive with another checksum
	other.profile = ProfileIdentity::fromText(std::string("openbfme-profile 1\nprofile enhanced\nengine ") + ProfileIdentity::buildId() +
		"\nrng ZH_CarryChain\nmods none\narchive 0 rotwk:INI.big ffffffffffffffffffffffffffffffff 100\n");
	const SaveGameCompatibility changed = verdict(other, running);
	CHECK_FALSE(changed.loadable);
	CHECK(changed.refusal.find("from the launcher.") != std::string::npos); // the sample's "test" build is no release: "the build that made it"
	SaveGameHeader mods = h;
	mods.profile = ProfileIdentity::fromText(std::string("openbfme-profile 1\nprofile enhanced\nengine ") + ProfileIdentity::buildId() +
		"\nrng ZH_CarryChain\nmods BigMod\narchive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 100\n");
	CHECK_FALSE(verdict(mods, running).loadable);
	SaveGameHeader far = h; // mission 9999: listed refused, named
	far.campaignMission = 9999;
	CHECK(verdict(far, running).refusal == "the campaign CS1 has no mission 9999 (it has 3)");
	SaveGameHeader past = h; // a won mission 2 whose next mission does not exist
	past.campaignMission = 2;
	past.campaignVictorious = true;
	CHECK(verdict(past, running).refusal == "the campaign CS1 has no mission 3 for its menu to name (it has 3)");
	SaveGameHeader unknown = h;
	unknown.campaign = "NO_SUCH";
	CHECK(verdict(unknown, running).refusal == "the game data has no campaign 'NO_SUCH'");
	SaveGameHeader hard = h;
	hard.campaignDifficulty = 3;
	CHECK(verdict(hard, running).refusal == "the save's difficulty 3 is not 0 .. 2");
	SaveGameHeader early = h; // advanced before the game was over
	early.campaignReady = false;
	early.campaignAdvanced = true;
	CHECK(verdict(early, running).refusal == "the save's campaign state is not one a game reaches (the mission index moved on before the game was over)");
	// the same state: the first mission of the campaign, nothing played, is a legal menu (Replay Mission after an early quit)
	SaveGameHeader fresh = h;
	fresh.campaignReady = true;
	CHECK(verdict(fresh, running).loadable);
}

TEST_CASE("campsave1 stops: the lane's stops are reported with their ids (S-1360, S-4900 .. S-4903)")
{
	const std::vector<std::string> lines = LinearCampaignManager::stopLines();
	for (const char *id : { "[S-1360]", "[S-4900]", "[S-4901]", "[S-4902]", "[S-4903]" })
	{
		int n = 0;
		for (const std::string &l : lines)
		{
			n += l.rfind(id, 0) == 0 ? 1 : 0;
		}
		CHECK_MESSAGE(n == 1, id);
	}
}
