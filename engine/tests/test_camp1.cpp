// OpenBFME unit tests: lane CAMP-1, TheLinearCampaignManager (GameClient/LinearCampaign.h): the LinearCampaign block grammar (RW 0x5ECCB9 / 0x5EC9FE),
// the retail campaign list RotWK loads (the expansion legend's Data\INI\LinearCampaignExpansion1.ini) and the campaign progress. GPL-3.0.

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/LinearCampaign.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Module/AttachUpdate.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"

#include <string>
#include <vector>

namespace
{
// parses `text` into a fresh manager with the given load type; the audio hook knows the events in `audio`
struct Parsed
{
	LinearCampaignManager manager;
	std::string error;
	int code = -1;
};

void parse(Parsed &out, const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, std::vector<std::string> audio = { "R_BbEvil108" })
{
	INIEnvironment env;
	out.manager.registerBlock(env.blocks);
	LinearCampaignManager *previous = TheLinearCampaignManager;
	TheLinearCampaignManager = &out.manager;
	ContainParseHooks &hooks = TheContainParseHooks();
	const auto previousAudio = hooks.audioEventExists;
	hooks.audioEventExists = [audio](const std::string &n) {
		for (const std::string &a : audio)
		{
			if (a == n)
			{
				return true;
			}
		}
		return false;
	};
	try
	{
		INI ini(env);
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		ini.loadMemory("Data\\INI\\camp1.ini", bytes, type);
	}
	catch (const INIException &e)
	{
		out.error = e.what();
		out.code = e.code();
	}
	hooks.audioEventExists = previousAudio;
	TheLinearCampaignManager = previous;
}
} // namespace

TEST_CASE("camp1 LinearCampaign: the field tables RW 0xBF5354 / 0xBF5268, the mission defaults (RW 0x5EBA4A), MillisecondsAfterStartToStartFadeUp in frames")
{
	Parsed p;
	parse(p,
		"LinearCampaign CAMP1_TEST\n"
		"  CampaignDisplayNameLabel = CAMPAIGN:ANGMAR\n"
		"  CarryoverUnit = AngmarHwaldar\n"
		"  CarryoverUnit = AngmarKarsh\n"
		"  OverallCampaignIntroMovie = Angmar_Campaign_Intro\n"
		"  Mission One\n"
		"    Map = \"MAP ANG Angmar\"\n"
		"    IntroMovie = Angmar_Campaign_M1Open\n"
		"    LoadScreenImage = CampaignAngmar_Angmar_Loadscreen\n"
		"    LoadScreenMusicTrack = R_BbEvil108\n"
		"    MillisecondsAfterStartToStartFadeUp = 1400\n"
		"    DelayCarryoverSpawningOf = AngmarMorgramir\n"
		"    DelayCarryoverSpawningOf = AngmarRogash\n"
		"  End\n"
		"  Mission Two\n"
		"    Map = Second\n"
		"    LoadScreenMusicTrack = NoSound\n"
		"  End\n"
		"End\n");
	REQUIRE_MESSAGE(p.error.empty(), p.error);
	const LinearCampaign *c = p.manager.find("camp1_test"); // RW 0x5EB010: compareNoCase
	REQUIRE(c != nullptr);
	CHECK(c->displayNameLabel == "CAMPAIGN:ANGMAR");
	CHECK(c->overallIntroMovie == "Angmar_Campaign_Intro");
	CHECK(c->carryoverUnits == std::vector<std::string>{ "AngmarHwaldar", "AngmarKarsh" });
	REQUIRE(c->missions.size() == 2);
	const LinearCampaignMission &m = c->missions[0];
	CHECK(m.name == "One");
	CHECK(m.map == "MAP ANG Angmar");
	CHECK(m.introMovie == "Angmar_Campaign_M1Open");
	CHECK(m.loadScreenImage == "CampaignAngmar_Angmar_Loadscreen");
	CHECK(m.loadScreenMusicTrack == "R_BbEvil108");
	CHECK(m.fadeUpFrames == 7); // ceil(1400 * 0.005)
	CHECK(m.delayCarryover == std::vector<std::string>{ "AngmarMorgramir", "AngmarRogash" });
	CHECK(c->missions[1].fadeUpFrames == 7); // the ctor's default
	CHECK(c->missions[1].loadScreenMusicTrack.empty());
	CHECK(c->missions[1].introMovie.empty());
}

TEST_CASE("camp1 LinearCampaign: what the parser refuses (RW 0xBF5238, 0xBF53F0, 0xC2500C)")
{
	{
		Parsed p;
		parse(p, "LinearCampaign C\n  Mission M\n    IntroMovie = X\n  End\nEnd\n");
		CHECK(p.code == 3);
		CHECK(p.error.find("Campaign missions must have a Map. M does not") != std::string::npos);
	}
	{
		Parsed p;
		parse(p, "LinearCampaign C\nEnd\n", INI_LOAD_CREATE_OVERRIDES); // a map.ini
		CHECK(p.code == 8);
		CHECK(p.error.find("anywhere but the main INI files") != std::string::npos);
		CHECK(p.manager.campaigns().empty());
	}
	{
		Parsed p;
		parse(p, "LinearCampaign C\n  Mission M\n    Map = A\n    LoadScreenMusicTrack = CAMP1NoSuchTrack\n  End\nEnd\n");
		CHECK(p.code == 3);
		CHECK(p.error.find("Invalid Sound 'CAMP1NoSuchTrack'") != std::string::npos);
	}
	{
		Parsed p;
		parse(p, "LinearCampaign C\n  NoSuchField = 1\nEnd\n");
		CHECK(!p.error.empty());
	}
}

TEST_CASE("camp1 CampaignProgress: the difficulty character (RW 0x91C108), the next mission, the final mission (RW 0x5EAB6E), the sidecar text")
{
	CHECK(CampaignProgress::difficultyFromCommand("E") == 0);
	CHECK(CampaignProgress::difficultyFromCommand("H") == 2);
	CHECK(CampaignProgress::difficultyFromCommand("M") == 1);
	CHECK(CampaignProgress::difficultyFromCommand("") == 1);
	Parsed p;
	parse(p, "LinearCampaign C\n  Mission A\n    Map = MA\n  End\n  Mission B\n    Map = MB\n  End\nEnd\n");
	REQUIRE(p.error.empty());
	CampaignProgress g;
	g.campaign = "C";
	g.difficulty = 2;
	REQUIRE(g.current(p.manager) != nullptr);
	CHECK(g.current(p.manager)->map == "MA");
	CHECK_FALSE(g.isFinalMission(p.manager));
	g.victorious = true;
	CHECK(g.advance(p.manager));
	CHECK(g.current(p.manager)->map == "MB");
	CHECK_FALSE(g.victorious);
	CHECK(g.isFinalMission(p.manager));
	CHECK_FALSE(g.advance(p.manager));
	g.victorious = true;
	CampaignProgress back;
	std::string error;
	REQUIRE(CampaignProgress::parse(g.serialize(), back, &error));
	CHECK(back.campaign == "C");
	CHECK(back.mission == 1);
	CHECK(back.difficulty == 2);
	CHECK(back.victorious);
	CHECK_FALSE(CampaignProgress::parse("campaign=C\nmission=1\n", back, &error)); // a missing field
	CHECK_FALSE(CampaignProgress::parse("campaign=C\nmission=x\ndifficulty=1\nvictorious=0\n", back, &error));
	CHECK_FALSE(CampaignProgress::parse("campaign=C\nmission=1\ndifficulty=3\nvictorious=0\n", back, &error));
	CHECK(error.find("campaign progress") == 0);
}

TEST_CASE("camp1 retail: RotWK's TheLinearCampaignManager holds the expansion legend's three campaigns; ANGMAR_CAMPAIGN is the eight Angmar missions")
{
	OPENBFME_REQUIRE_START(s);
	const LinearCampaignManager &m = s->world->linearCampaigns();
	std::vector<std::string> names;
	for (const LinearCampaign &c : m.campaigns())
	{
		names.push_back(c.name);
	}
	// subsystemlegendexpansion1.ini: LinearCampaignManager InitFile = Data\INI\LinearCampaignExpansion1.ini (BFME2's GOOD / EVIL_CAMPAIGN are not loaded)
	CHECK(names == std::vector<std::string>{ "ANGMAR_CAMPAIGN", "ANGMAR_CAMPAIGN_DEMO", "ANGMAR_BONUS_CAMPAIGN" });
	const LinearCampaign *a = m.find("ANGMAR_CAMPAIGN");
	REQUIRE(a != nullptr);
	std::vector<std::string> maps;
	for (const LinearCampaignMission &mi : a->missions)
	{
		maps.push_back(mi.map);
	}
	CHECK(maps == std::vector<std::string>{ "MAP ANG Angmar", "MAP ANG Rhudaur", "MAP ANG Amon Sul", "MAP ANG Dark Eye", "MAP ANG Barrow Downs",
					  "MAP ANG Carn Dum", "MAP ANG Barrow Wights", "MAP ANG Fornost" });
	CHECK(a->overallIntroMovie == "Angmar_Campaign_Intro");
	CHECK(a->carryoverUnits.size() == 5);
	CHECK(a->missions[0].delayCarryover.size() == 5);
	CHECK(a->missions[1].fadeUpFrames == 10); // 2000 ms
	const LinearCampaign *bonus = m.find("ANGMAR_BONUS_CAMPAIGN");
	REQUIRE(bonus != nullptr);
	REQUIRE(bonus->missions.size() == 1);
	CHECK(bonus->missions[0].map == "MAP ANG Bonus");
}

TEST_CASE("camp1 stops: the lane's stops are reported with their ids (S-1360 .. S-1366)")
{
	std::vector<std::string> lines = LinearCampaignManager::stopLines();
	for (const std::string &l : AttachUpdate::stopLines())
	{
		lines.push_back(l);
	}
	for (const std::string &l : ScriptActions::campaignStopLines())
	{
		lines.push_back(l);
	}
	REQUIRE(lines.size() == 8);
	// lane CAMP-1H: S-1362 (ENEMY_SIGHTED's sight filter) is ported (Object::canSee RW 0x68FA3D); S-1711 and S-1712 joined
	const char *ids[] = { "[S-1360]", "[S-1364]", "[S-1711]", "[S-1361]", "[S-1363]", "[S-1365]", "[S-1366]", "[S-1712]" };
	for (size_t i = 0; i < 8; ++i)
	{
		CHECK(lines[i].rfind(ids[i], 0) == 0);
	}
	CHECK(lines[0].find("00000000.sav") != std::string::npos);
	CHECK(lines[1].find("S-1710") != std::string::npos);
	CHECK(lines[2].find("MilitaryCaptionDelayMS") != std::string::npos);
	CHECK(lines[3].find("HordeContain + 0x54") != std::string::npos);
	CHECK(lines[4].find("PLAYER_SET_MAX_SPELLPOINTS") != std::string::npos);
	CHECK(lines[5].find("RW 0x7A208C") != std::string::npos);
	CHECK(lines[6].find("GateOpenAndCloseBehavior") != std::string::npos);
	CHECK(lines[7].find("RW 0x6AD2B5") != std::string::npos);
	// lane CAMP-1H: the movies' stop (GameWorld.get_movie's report)
	REQUIRE(VideoPlayer::stopLines().size() == 1);
	CHECK(VideoPlayer::stopLines()[0].rfind("[S-1710]", 0) == 0);
}

TEST_CASE("camp1 fade: a finished CAMERA_FADE_* applies no fade any more (ZH ScriptEngine::updateFades -> FADE_NONE), even when its minimum is 1")
{
	ScriptCameraDirector d;
	ScriptClientRequest r;
	r.action = "CAMERA_FADE_SUBTRACT";
	for (int i = 0; i < 5; ++i)
	{
		ScriptParameter p;
		p.type = i < 2 ? 1 : 0;
		p.realValue = i == 0 ? 1.0f : 0.0f;
		r.params.push_back(p);
	}
	d.apply(r, Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f); // MAP ANG Amon Sul's intro: (1, 0, 0, 0, 0)
	d.update(200.0);
	CHECK(d.fade() == 0.0f);
	// a fade in progress: (0, 1, 5 frames up, 5 held, 5 down; 30 Hz client frames) is full while held, gone at the end
	r.params[0].realValue = 0.0f;
	r.params[1].realValue = 1.0f;
	r.params[2].intValue = 5;
	r.params[3].intValue = 5;
	r.params[4].intValue = 5;
	d.apply(r, Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f);
	d.update(250.0); // 7.5 frames: held
	CHECK(d.fade() == 1.0f);
	d.update(400.0);
	CHECK(d.fade() == 0.0f);
}
