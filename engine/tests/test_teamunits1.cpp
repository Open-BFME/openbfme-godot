// OpenBFME. TEAMUNITS-1 tests: the script condition TEAM_HAS_UNITS (RotWK RW 0x7E9F44) and the action TEAM_CREATE_RADAR_EVENT (RW 0x7C0B61) count a
// member as retail's Team::hasAnyUnits does (RW 0x7A07D8; donor Open-BFME-2 Team.cpp (Team::hasAnyUnits), BFME2 0x79DEC4, same shape: both editions): not
// dead, not DESTROYED, not a STRUCTURE, PROJECTILE or IGNORE_FOR_VICTORY. Synthetic data, no retail files.
#include "doctest.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

using namespace scripttest;

namespace
{
const char kTeamUnitsIni[] = "Object TUInfantry\n"
							 "  KindOf = SELECTABLE INFANTRY\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
							 "End\n"
							 "Object TUTower\n"
							 "  KindOf = SELECTABLE STRUCTURE\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n  End\n"
							 "End\n"
							 "Object TUArrow\n"
							 "  KindOf = PROJECTILE\n"
							 "End\n"
							 "Object TUProp\n"
							 "  KindOf = SELECTABLE INFANTRY IGNORE_FOR_VICTORY\n"
							 "End\n";

struct TeamUnitsFx
{
	ScriptGame g;
	TeamUnitsFx()
	{
		LogicModules::registerAll(g.lw.w.modules);
		g.lw.logic->settings().bodyThresholdsLoaded = true; // ActiveBody's default thresholds (GameData)
		g.lw.logic->settings().unitDamagedThreshold = 0.7f;
		g.lw.logic->settings().unitReallyDamagedThreshold = 0.35f;
		const std::string err = g.lw.w.load(kTeamUnitsIni, INI_LOAD_OVERWRITE, "teamunits1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		g.start();
	}
	Object *add(const char *name) { return g.lw.make(name, g.lw.teamOf("Alice")); }
	bool hasUnits() { return ScriptConditions::evaluate(g.engine(), cond("TEAM_HAS_UNITS", { sp(3, "Alice/teamAlice") })); }
	size_t radarEvents()
	{
		ScriptActions::execute(g.engine(), act("TEAM_CREATE_RADAR_EVENT", { sp(3, "Alice/teamAlice"), ip(30, 2) }));
		return g.lw.logic->radarEventNotices().size();
	}
};
} // namespace

TEST_CASE("teamunits1: TEAM_HAS_UNITS is false for a team of only a STRUCTURE, a PROJECTILE, an IGNORE_FOR_VICTORY object or a DESTROYED unit")
{
	SUBCASE("structure") { TeamUnitsFx f; REQUIRE(f.add("TUTower")); CHECK_FALSE(f.hasUnits()); CHECK(f.radarEvents() == 0); }
	SUBCASE("projectile") { TeamUnitsFx f; REQUIRE(f.add("TUArrow")); CHECK_FALSE(f.hasUnits()); CHECK(f.radarEvents() == 0); }
	SUBCASE("ignore for victory") { TeamUnitsFx f; REQUIRE(f.add("TUProp")); CHECK_FALSE(f.hasUnits()); CHECK(f.radarEvents() == 0); }
	SUBCASE("destroyed")
	{
		TeamUnitsFx f;
		Object *u = f.add("TUInfantry");
		REQUIRE(u);
		u->setStatus(OBJECT_STATUS_DESTROYED, true);
		REQUIRE_FALSE(u->isEffectivelyDead());
		CHECK_FALSE(f.hasUnits());
		CHECK(f.radarEvents() == 0);
	}
}

TEST_CASE("teamunits1: one live infantry makes TEAM_HAS_UNITS true, next to a tower too")
{
	TeamUnitsFx f;
	REQUIRE(f.add("TUTower"));
	CHECK_FALSE(f.hasUnits());
	REQUIRE(f.add("TUInfantry"));
	CHECK(f.hasUnits());
	CHECK(f.radarEvents() == 1);
}
