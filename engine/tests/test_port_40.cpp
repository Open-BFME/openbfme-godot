// OpenBFME unit tests: PORT-40 (issue #125) — AIData MaxRecruitRadius: how far the AI and scripts recruit units into teams. GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (GameLogicSettings.cpp, ScriptActionsOrders.cpp, Common/Team.cpp), never the
// engine's own output:
//   * the row RW 0xC1CCD0 (parseReal RW 0x42ED00, AIData + 0x5C; the constructor's 0.0, RW 0x6FFD04) and the mod data point
//     default\aidata.ini:32 (= 400.0, retail 2.01 and Ennorath alike);
//   * TEAM_RECRUIT_UNITS (397): the case RW 0x7CEAF2 -> RW 0x7C2BFB -> Team RW 0x7A491C, the radius a team's centre gathers from —
//     MaxRecruitRadius with a live member (RW 0x7C2C47 -> RW 0x7A0956(0), read RW 0x7C2C5B), the constant 1000000.0 without one (RW 0xBDCDC0,
//     read RW 0x7C2C60);
//   * the override writers RW 0x7C0C3E (TEAM_AVAILABLE_FOR_RECRUITMENT, 94) and RW 0x7C199C (TEAM_SET_AI_RECRUITABLE_FLAG, 566), team + 0x110 /
//     + 0x111, read by the recruit gate RW 0x7A4A13 .. 0x7A4A22.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Dict.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

using namespace scripttest;

namespace
{

void startEngine(GameLogic &logic)
{
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, nullptr, nullptr, false);
}

void run(ScriptEngine &e, const ScriptActionRec &a)
{
	ScriptActions::execute(e, a);
}

std::string qual(Team *t)
{
	// RW 0x604043's qualified team name: the owner's name and the PROTOTYPE's name (ScriptConditions::team -> TeamFactory::findTeamPrototype)
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getPrototype()->getName();
}

// the fixture's recruit world: Alice's destination prototype outranks the candidates' teams (teamProductionPriority, TeamTemplateInfo + 0xF0
// <-> prototype + 0x21C; the gate RW 0x7A49F1 .. 0x7A4A06 recruits only into a strictly higher one; no key = 0, RW 0x7A384B)
struct RecruitWorld
{
	combattest::CombatWorld w;
	Player *alice = nullptr;
	TeamFactory *teams = nullptr;
	Dict prio10, prio5;
	Team *dst = nullptr;

	RecruitWorld()
	{
		startEngine(*w.logic);
		w.logic->settings().maxRecruitRadius = 400.0f; // default\aidata.ini:32 (retail 2.01 and Ennorath)
		alice = w.playerOf('A');
		teams = &w.logic->players().teams();
		prio10.setInt("teamProductionPriority", 10);
		prio5.setInt("teamProductionPriority", 5);
		dst = teams->createTeam(teams->initTeam("Dst", alice, true, &prio10), true);
		Object *anchor = w.unit("Swordsman", 'A', 500.0f, 500.0f);
		anchor->setTeam(dst); // a live member: the radius branch (RW 0x7C2C5B), the centre at (500, 500) (RW 0x7A02E1)
	}
};

TEST_CASE("PORT-40 settings: MaxRecruitRadius (row RW 0xC1CCD0, parseReal RW 0x42ED00, AIData + 0x5C) parses; the block without it keeps the "
		  "constructor's 0.0")
{
	std::string err;
	GameLogicSettings g;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  MaxRecruitRadius = 400.0\nEnd\n", g, &err), err); // the mod's value
	CHECK(g.maxRecruitRadius == 400.0f);
	CHECK(g.unappliedFields.count("AIData.MaxRecruitRadius") == 0);
	GameLogicSettings fresh;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  EnableRepulsors = Yes\nEnd\n", fresh, &err), err);
	CHECK(fresh.maxRecruitRadius == 0.0f); // the AIData constructor's (RW 0x6FFD04: xmm0 zeroed at RW 0x6FFC01)
}

TEST_CASE("PORT-40 TEAM_RECRUIT_UNITS (RW 0x7C2BFB -> RW 0x7A491C): within MaxRecruitRadius of the team's live centre a unit joins the team, "
		  "beyond it it stays home")
{
	RecruitWorld f;
	Object *inRange = f.w.unit("Dummy", 'A', 500.0f + 399.0f, 500.0f);  // 399 < 400
	Object *outRange = f.w.unit("Dummy", 'A', 500.0f + 401.0f, 500.0f); // 401 > 400
	run(f.w.logic->scriptEngine(), act("TEAM_RECRUIT_UNITS", { sp(3, qual(f.dst)), ip(0, 5), sp(61, "Dummy") }));
	CHECK(inRange->getTeam() == f.dst);
	CHECK(outRange->getTeam() == f.alice->getDefaultTeam());
	// a second run with the same radius: the recruit restarts from the radius squared (RW 0x7A4A99: comiss + ja keeps a tie for the later one)
	f.w.logic->settings().maxRecruitRadius = 500.0f;
	run(f.w.logic->scriptEngine(), act("TEAM_RECRUIT_UNITS", { sp(3, qual(f.dst)), ip(0, 5), sp(61, "Dummy") }));
	CHECK(outRange->getTeam() == f.dst);
	CHECK(f.w.logic->scriptEngine().stats().unportedActions.count("TEAM_RECRUIT_UNITS") == 0);
}

TEST_CASE("PORT-40 TEAM_RECRUIT_UNITS: a team without a live member recruits with the constant 1000000.0 (RW 0x7C2C60, RW 0xBDCDC0)")
{
	RecruitWorld f;
	Team *far = f.teams->createTeam(f.teams->initTeam("Far", f.alice, true, &f.prio10), true); // no members at all
	Object *edge = f.w.unit("Dummy", 'A', 999.0f, 999.0f); // 1410 from (0, 0): way beyond 400, within 1e6
	run(f.w.logic->scriptEngine(), act("TEAM_RECRUIT_UNITS", { sp(3, qual(far)), ip(0, 1), sp(61, "Dummy") }));
	CHECK(edge->getTeam() == far);
}

TEST_CASE("PORT-40 TEAM_RECRUIT_UNITS gates (RW 0x7A491C): the other player's unit and a wrong type stay; prototype teamProductionPriority "
		  "gates the pull; the recruitable override (+ 0x110 / + 0x111) opens and shuts the gate")
{
	RecruitWorld f;
	Object *foreign = f.w.unit("Dummy", 'B', 510.0f, 500.0f);     // RW 0x7A497C: the destination's player's objects only
	Object *wrongType = f.w.unit("Swordsman", 'A', 510.0f, 500.0f); // not of the type (RW 0x73D5C2)
	Team *src = f.teams->createTeam(f.teams->initTeam("Src", f.alice, true, &f.prio5), true); // priority 5, no teamIsAIRecruitable
	REQUIRE(src != nullptr);
	REQUIRE(src->isActive()); // + 0x5D set (initTeam + createTeam activate)
	Object *cand = f.w.unit("Dummy", 'A', 510.0f, 500.0f);
	cand->setTeam(src); // not the default team, the prototype not recruitable
	ScriptEngine &e = f.w.logic->scriptEngine();
	run(e, act("TEAM_RECRUIT_UNITS", { sp(3, qual(f.dst)), ip(0, 9), sp(61, "Dummy") }));
	CHECK(foreign->getTeam() == f.w.playerOf('B')->getDefaultTeam());
	CHECK(wrongType->getTeam() == f.alice->getDefaultTeam());
	CHECK(cand->getTeam() == src); // the gate holds (RW 0x7A4A22)
	// RW 0x7C199C: the override (+ 0x110 = 1, + 0x111 = the flag) opens it
	run(e, act("TEAM_SET_AI_RECRUITABLE_FLAG", { sp(3, qual(src)), ip(8, 1) }));
	CHECK(src->hasAIRecruitableOverride());
	CHECK(src->aiRecruitableOverride());
	run(e, act("TEAM_RECRUIT_UNITS", { sp(3, qual(f.dst)), ip(0, 9), sp(61, "Dummy") }));
	CHECK(cand->getTeam() == f.dst);
	// priorities: dst 5 against a source prototype's 5 — jge skips (RW 0x7A4A00)
	Team *low = f.teams->createTeam(f.teams->initTeam("Low", f.alice, true, &f.prio5), true);
	Object *anchor = f.w.unit("Swordsman", 'A', 500.0f, 500.0f);
	anchor->setTeam(low);
	Object *held = f.w.unit("Dummy", 'A', 510.0f, 500.0f);
	held->setTeam(src); // same prototype as before, the override still true — but the priority now shuts
	run(e, act("TEAM_RECRUIT_UNITS", { sp(3, qual(low)), ip(0, 9), sp(61, "Dummy") }));
	CHECK(held->getTeam() == src);
	// RW 0x7C0C3E: TEAM_AVAILABLE_FOR_RECRUITMENT writes the same override; clearing it shuts the gate again
	run(e, act("TEAM_AVAILABLE_FOR_RECRUITMENT", { sp(3, qual(src)), ip(8, 0) }));
	CHECK(src->hasAIRecruitableOverride());
	CHECK_FALSE(src->aiRecruitableOverride());
	run(e, act("TEAM_RECRUIT_UNITS", { sp(3, qual(f.dst)), ip(0, 9), sp(61, "Dummy") }));
	CHECK(held->getTeam() == src); // still src's, the override clear
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("PORT-40: the AI-recruitable override (+ 0x110 / + 0x111) round-trips through a version 4 team block, and an older block loads with none")
{
	TeamFactory f;
	TeamPrototype *proto = f.initTeam("PORT-40 override", nullptr, false, nullptr); // RW 0x7A2C47's prototype of a team dict
	REQUIRE(proto);
	Team *saved = f.createTeam(proto);
	REQUIRE(saved);
	saved->setAIRecruitableOverride(true);
	XferSave save;
	saved->xfer(save);
	REQUIRE(!save.data().empty());
	CHECK(save.data()[0] == 4); // the block's version: 2 is the special-power registry, 3 the threat-order list, 4 this override
	Team *loaded = f.createTeam(proto);
	REQUIRE(loaded);
	CHECK_FALSE(loaded->hasAIRecruitableOverride());
	XferLoad load(save.data());
	loaded->xfer(load);
	CHECK(loaded->hasAIRecruitableOverride());
	CHECK(loaded->aiRecruitableOverride());
	// a team without an override writes version 1, and a version 1 load clears the override the target team already had
	Team *plain = f.createTeam(proto);
	REQUIRE(plain);
	XferSave save1;
	plain->xfer(save1);
	REQUIRE(!save1.data().empty());
	CHECK(save1.data()[0] == 1);
	Team *loaded1 = f.createTeam(proto);
	REQUIRE(loaded1);
	loaded1->setAIRecruitableOverride(true);
	XferLoad load1(save1.data());
	loaded1->xfer(load1);
	CHECK_FALSE(loaded1->hasAIRecruitableOverride());
	CHECK_FALSE(loaded1->aiRecruitableOverride());
}

} // namespace
