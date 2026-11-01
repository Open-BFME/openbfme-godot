// OpenBFME unit tests: lane CAMPUI-1, the mission objectives (TheMissionObjectiveTracker, GameLogic/ScriptEngine/MissionObjectiveTracker.h), the military
// caption's settings (MilitaryCaptionSettings) and the script camera director's start point. GPL-3.0.

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/ScriptEngine/MissionObjectiveTracker.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace
{
std::vector<MissionObjective> parse(const std::string &text, INILoadType type, bool *threw = nullptr)
{
	std::vector<MissionObjective> list;
	bool parsed = false;
	INIEnvironment env;
	env.blocks.registerBlock("MissionObjectiveList", [&](INI *ini) { MissionObjectiveTracker::parseList(ini, list, parsed); });
	INI ini(env);
	try
	{
		ini.loadMemory("map.ini", std::vector<std::uint8_t>(text.begin(), text.end()), type);
	}
	catch (const INIException &)
	{
		if (threw)
		{
			*threw = true;
		}
	}
	return list;
}

std::vector<int> shownIndices(const MissionObjectiveTracker &t)
{
	std::vector<int> out;
	for (size_t i = 0; i < t.objectives().size(); ++i)
	{
		if (t.objectives()[i].shown)
		{
			out.push_back((int)i + 1); // the scripts' 1-based index
		}
	}
	return out;
}
} // namespace

TEST_CASE("campui1 objectives: MissionObjectiveList (RW 0x836497): every token a record, the bonus tags marked, map.ini only, the first list wins")
{
	const std::string text = "MissionObjectiveList\n"
							 "  MissionObjectiveTag = SCRIPT:A1 ; the first\n"
							 "  MissionObjectiveTag = SCRIPT:A2 SCRIPT:A3\n"
							 "  BonusMissionObjectiveTag = SCRIPT:B1\n"
							 "END\n"
							 "MissionObjectiveList\n"
							 "  MissionObjectiveTag = SCRIPT:SECOND\n"
							 "END\n";
	const std::vector<MissionObjective> list = parse(text, INI_LOAD_CREATE_OVERRIDES);
	REQUIRE(list.size() == 4);
	CHECK(list[0].label == "SCRIPT:A1");
	CHECK(list[1].label == "SCRIPT:A2");
	CHECK(list[2].label == "SCRIPT:A3"); // RW 0x836792: each token of the line
	CHECK(list[3].label == "SCRIPT:B1");
	CHECK_FALSE(list[2].bonus);
	CHECK(list[3].bonus); // RW 0x8367EB
	for (const MissionObjective &o : list)
	{
		CHECK(o.shown); // RW 0x835F4F: shown by default
		CHECK_FALSE(o.completed);
	}
	bool threw = false;
	parse(text, INI_LOAD_OVERWRITE, &threw);
	CHECK(threw); // "A MissionObjectiveList may only appear in a map.ini file."
}

TEST_CASE("campui1 objectives: the tracker's actions (1-based in the scripts), hide then complete keeps it hidden, the sounds' transitions, the score's term")
{
	MissionObjectiveTracker t;
	CHECK_FALSE(t.hasList());
	CHECK_FALSE(t.setShown(0, true)); // no list: nothing
	std::vector<MissionObjective> list(4);
	list[2].bonus = list[3].bonus = true;
	t.installList(list);
	std::vector<MissionObjective> other(1);
	t.installList(other); // the tracker keeps its first list
	REQUIRE(t.objectives().size() == 4);
	CHECK_FALSE(t.setShown(1, true));  // already shown: no false -> true change (no sound)
	CHECK_FALSE(t.setShown(1, false)); // hidden
	CHECK(t.setCompleted(1, true));    // completed while hidden...
	CHECK_FALSE(t.objectives()[1].shown); // ...stays hidden (RW 0x836097 does not touch + 5)
	CHECK(t.setShown(1, true));        // shown again: the added sound
	CHECK_FALSE(t.setShown(9, true));  // out of range (RW 0x835E24): nothing
	CHECK_FALSE(t.setShown(-1, true));
	CHECK(t.setCompleted(3, true));
	CHECK_FALSE(t.setCompleted(3, true));
	int total = 0;
	CHECK(t.countShownBonus(total) == 1); // RW 0x79DE90: the shown bonus objectives (2 and 3), one completed
	CHECK(total == 2);
	t.setShown(2, false);
	CHECK(t.countShownBonus(total) == 1);
	CHECK(total == 1);
	REQUIRE(t.nthShown(2) != nullptr);
	CHECK(t.nthShown(2) == &t.objectives()[3]); // RW 0x8E8A8A: the third SHOWN one
	CHECK(t.nthShown(3) == nullptr);
	t.setCompleted(3, false); // MARK_MISSION_OBJECTIVE_NOT_COMPLETED
	CHECK_FALSE(t.objectives()[3].completed);
	t.reset();
	CHECK_FALSE(t.hasList());
	CHECK(t.objectives().empty());
}

TEST_CASE("campui1 objectives: CHUNK_MissionObjectives / CHUNK_ObjectivesMenu round trip in retail's layout (RW 0x8363F2 / 0x836835 / 0x835E3F, RW 0x8227E9)")
{
	MissionObjectiveTracker t;
	std::vector<MissionObjective> list(2);
	list[0].label = "SCRIPT:X";
	list[1].label = "SCRIPT:Y";
	list[1].bonus = true;
	t.installList(list);
	t.setShown(0, false);
	t.setCompleted(1, true);
	t.setScreenEnabled(false);
	XferSave save;
	t.xfer(save);
	t.xferMenu(save);
	const std::vector<std::uint8_t> bytes = save.data();
	MissionObjectiveTracker u;
	XferLoad load(bytes);
	u.xfer(load);
	u.xferMenu(load);
	CHECK(load.atEnd());
	REQUIRE(u.objectives().size() == 2);
	CHECK(u.hasList());
	CHECK(u.objectives()[0].label == "SCRIPT:X");
	CHECK_FALSE(u.objectives()[0].shown);
	CHECK(u.objectives()[1].bonus);
	CHECK(u.objectives()[1].completed);
	CHECK_FALSE(u.screenEnabled());
}

TEST_CASE("campui1 retail: MAP ANG Angmar's objectives through its intro are the retail objectives screen's rows (Witch-king survives, then the bonus "
		  "Level 3 objectives; 'Help the mysterious warrior Troll' after the intro)")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang angmar");
	const MissionObjectiveTracker &t = m.logic().missionObjectives();
	// map.ini: 9 MissionObjectiveTag then 3 BonusMissionObjectiveTag (retail data, read by the tracker's block)
	REQUIRE(t.hasList());
	REQUIRE(t.objectives().size() == 12);
	CHECK(t.objectives()[0].label == "SCRIPT:ANGAngmarObjectiveText_01");
	CHECK(t.objectives()[2].label == "SCRIPT:ANGAngmarObjectiveText_03");
	CHECK(t.objectives()[9].label == "SCRIPT:ANGAngmarBonusObjectiveText_01");
	for (size_t i = 0; i < 12; ++i)
	{
		const bool bonus = i >= 9; // the three BonusMissionObjectiveTag rows follow the nine MissionObjectiveTag rows
		CHECK(t.objectives()[i].bonus == bonus);
	}
	m.step(2);
	// 'Map Setup' (frame 0): HIDE 2..9 and 12 -> the screen's rows: 1 (the Witch-king must survive), 10, 11 (the Level 3 bonus ones). Its
	// MARK_MISSION_OBJECTIVE_COMPLETED(2) is a DISABLED action of the map (its v3 tail 0: RW 0x7CAFB7 runs nothing), so objective 2 stays open
	CHECK(shownIndices(t) == std::vector<int>{ 1, 10, 11 });
	CHECK_FALSE(t.objectives()[1].completed);
	CHECK_FALSE(t.objectives()[0].completed);
	REQUIRE(t.nthShown(1) != nullptr);
	CHECK(t.nthShown(1)->label == "SCRIPT:ANGAngmarBonusObjectiveText_01");
	// no sound for the setup frame's marks (RW 0x836097: frame > 3)
	CHECK_FALSE(m.requested("MISSION_OBJECTIVE_SOUND"));
	// the intro ends (Intro Cine Part 5: SHOW_MISSION_OBJECTIVE(3), the NewObjective box): the rows 1, 3, 10, 11, as the retail screenshot after the intro
	REQUIRE(m.until([&] { return t.objectives()[2].shown; }, 400));
	MESSAGE("objective 3 shown at frame " << m.frame());
	CHECK(shownIndices(t) == std::vector<int>{ 1, 3, 10, 11 });
	CHECK(m.requested("MISSION_OBJECTIVE_SOUND")); // the local player's ObjectiveAddedSound
	CHECK(m.requested("DISPLAY_NOTIFICATION_BOX", "NewObjective"));
	// retail's screenshot after the intro shows the bonus objectives open (the Witch-king at level 1). NOT CHECKED here: in the port the counterattack
	// wolf riders the map places below its edge (y about -80, PlyrBNEast/West 'Counterattack Wave 1') are spawned inside the map (their horde members
	// on the pathfinder's cells, whose grid starts at 0: ZH sizes it by the terrain's maximum pathfind extent), so they are not OFF_MAP and fight the
	// heroes during the intro (reported to the coordinator from lane CAMPUI-1)
	MESSAGE("bonus objective 10 (the Witch-king reaches level 3) completed after the intro: " << t.objectives()[9].completed);
	if (Object *wk = m.unit("Witch King"))
	{
		if (const ExperienceTracker *xp = wk->getExperienceTracker())
		{
			MESSAGE("the Witch-king's level after the intro: " << xp->getLevelName());
		}
	}
}

TEST_CASE("campui1 retail: the military caption's settings are InGameUI.ini's MilitaryCaption* with language.ini's caption font (the retail serif)")
{
	retailtest::Mount *mt = retailtest::pureMount();
	if (!mt)
	{
		retailtest::printSkip("campui1 caption settings");
		return;
	}
	REQUIRE_MESSAGE(mt->fs != nullptr, mt->error);
	MilitaryCaptionSettings c;
	std::string error;
	REQUIRE_MESSAGE(MilitaryCaptionSettings::load(*mt->fs, c, &error), error);
	CHECK(c.color.red == 255);
	CHECK(c.color.green == 204);
	CHECK(c.color.blue == 0);
	CHECK(c.color.alpha == 255);
	CHECK(c.position.x == 0.5f);
	CHECK(c.position.y == 0.01f);
	CHECK(c.centered);
	CHECK(c.delayMs == 16);
	CHECK(c.text.name == "Albertus MT"); // language.ini's MilitaryCaptionFont replaces InGameUI's Arial
	CHECK(c.text.pointSize == 16);
	CHECK_FALSE(c.text.bold);
}

TEST_CASE("campui1 director: a camera move starts where the camera looks now, not where an earlier finished move left the director")
{
	ScriptCameraDirector d;
	ScriptClientRequest r;
	r.action = "MOVE_CAMERA_TO";
	r.hasPosition = true;
	r.position = Coord3D{ 100.0f, 100.0f, 0.0f };
	ScriptParameter p;
	p.type = 51;
	r.params = { p, ScriptParameter(), ScriptParameter(), ScriptParameter(), ScriptParameter() };
	r.params[1].type = 1;
	r.params[1].realValue = 1.0f;
	d.apply(r, Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f);
	d.update(2000.0);
	CHECK_FALSE(d.moving());
	CHECK(d.target().x == 100.0f);
	// the player scrolled to (500, 500); the next move starts there
	r.position = Coord3D{ 600.0f, 600.0f, 0.0f };
	d.apply(r, Coord3D{ 500.0f, 500.0f, 0.0f }, 0.0f);
	CHECK(d.moving());
	CHECK(d.target().x == 500.0f); // startMove: the target is the move's start until the first update
	// a caption: its serial and duration (the client types it, RW 0x69FF85)
	ScriptClientRequest c;
	c.action = "SHOW_MILITARY_CAPTION";
	ScriptParameter label, seconds;
	label.type = 25;
	label.stringValue = "SCRIPT:X";
	seconds.type = 1;
	seconds.realValue = 10.0f;
	c.params = { label, seconds };
	const unsigned before = d.captionSerial();
	d.apply(c, Coord3D{}, 0.0f);
	CHECK(d.captionSerial() == before + 1);
	CHECK(d.captionDurationMs() == 10000.0);
}

TEST_CASE("campui1 retail: OFF_MAP (RW + 0x458 bit 3) follows the map's active boundary on a position change (RW 0x69355D), and an off-map unit is no "
		  "candidate of the closest-enemy search nor a scanner (RW 0x6FF7A4, RW 0x66845E)")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang angmar");
	Object *wk = m.unit("Witch King");
	REQUIRE(wk != nullptr);
	CHECK_FALSE(wk->isOffMap());
	// MAP ANG Angmar's boundary 0: (350, 351) cells -> the extent (0, 0) .. (3500, 3510); strictly inside is on the map
	Coord3D p = *wk->getPosition();
	p.y = -80.0f;
	wk->setPosition(&p);
	CHECK(wk->isOffMap());
	p.y = 0.0f;
	wk->setPosition(&p);
	CHECK(wk->isOffMap()); // y <= lo.y
	p.y = 0.5f;
	wk->setPosition(&p);
	CHECK_FALSE(wk->isOffMap());
	p.x = 3500.0f;
	wk->setPosition(&p);
	CHECK(wk->isOffMap()); // x >= hi.x
	p.x = 2039.0f;
	p.y = 225.0f;
	wk->setPosition(&p);
	CHECK_FALSE(wk->isOffMap());
}
