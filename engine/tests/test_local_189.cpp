// OpenBFME unit tests for Local 189 (fork issue 982): script engine binary-map ports.
// Synthetic INI only; every expectation comes from RotWK 2.01 game.dat disassembly (MD5 daed3668006cd90f01c34e5a7da1901f).

#include "doctest.h"
#include "CombatTestUtil.h"
#include "MoveTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Dict.h"
#include "Common/MapObject.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
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
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

MapObject makeWaypoint(int id, const char *name, float x, float y)
{
	MapObject a;
	a.m_objectName = "*Waypoints/Waypoint";
	a.m_location = { x, y, 0.0f };
	a.m_properties.setInt("waypointID", id);
	a.m_properties.setAsciiString("waypointName", name);
	a.m_isWaypoint = true;
	return a;
}

const char *kLocal189Powers =
	"SpecialPower SP_L189_Test\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS_TWO\n"
	"  ReloadTime = 10000\n"
	"End\n";

const char *kLocal189Objects =
	"Object L189_DualHero\n"
	"  Side = Alpha\n"
	"  KindOf = HERO INFANTRY SELECTABLE\n"
	"  Body = ActiveBody B\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AI\n"
	"  End\n"
	"  Behavior = DualWeaponBehavior DW\n"
	"    SwitchWeaponOnCloseRangeDistance = 40\n"
	"  End\n"
	"End\n"
	"Object L189_Caster\n"
	"  Side = Alpha\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Body = ActiveBody B\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = SpecialPowerModule SP\n"
	"    SpecialPowerTemplate = SP_L189_Test\n"
	"    StartsPaused = No\n"
	"  End\n"
	"End\n"
	"Object L189_Target\n"
	"  Side = Alpha\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody B\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"End\n";

struct Local189Fixture
{
	movetest::MoveWorld w;
	SpecialPowerStore spStore;
	SpecialPowerStore *prevSp = nullptr;
	Player *p1 = nullptr;
	Player *p2 = nullptr;

	Local189Fixture() : w(30, 30)
	{
		prevSp = TheSpecialPowerStore;
		TheSpecialPowerStore = &spStore;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		REQUIRE(w.w.load(kLocal189Powers, INI_LOAD_OVERWRITE, "l189_powers.ini").empty());
		REQUIRE(w.w.load(kLocal189Objects, INI_LOAD_OVERWRITE, "l189_objects.ini").empty());
		w.buildMap();

		p1 = w.players.findPlayerWithName("Player_1");
		p2 = w.players.findPlayerWithName("Player_2");
		if (!p1) p1 = w.players.getNthPlayer(1);
		if (!p2) p2 = w.players.getNthPlayer(2);
		REQUIRE(p1 != nullptr);
		REQUIRE(p2 != nullptr);
		startEngine(*w.logic);
	}

	~Local189Fixture()
	{
		TheSpecialPowerStore = prevSp;
	}
};

} // namespace

// RW 0x7C3FD1 (TEAM_TELEPORT_TO_WAYPOINT, case 503): teleports every member of the team via Object::teleportTo (RW 0x696E63).
// RW 0x7C28FC (PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS, case 275): calls getTeamNamed(p1, false) and discards result (no-op).
TEST_CASE("LOCAL-189 TEAM_TELEPORT_TO_WAYPOINT (RW 0x7C3FD1) and PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS (RW 0x7C28FC)")
{
	combattest::CombatWorld w;
	TerrainLogic terrain;
	std::vector<MapObject> objs;
	objs.push_back(makeWaypoint(1, "WP_L189_Dest", 320.0f, 440.0f));
	WorldHeightMap hm; hm.m_width = 64; hm.m_height = 64; hm.m_data.assign(64u * 64u, 0); MapChunks chunks; chunks.objects = objs; terrain.init(hm, chunks);
	w.logic->setTerrain(&terrain);
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();

	Object *u1 = w.unit("Dummy", 'A', 100.0f, 100.0f);
	Object *u2 = w.unit("Dummy", 'A', 140.0f, 120.0f);
	REQUIRE(u1);
	REQUIRE(u2);
	TeamFactory &teams = w.logic->players().teams();
	Team *t = teams.createTeam(teams.initTeam("TeleTeam", w.playerOf('A'), true), true);
	REQUIRE(t);
	u1->setTeam(t);
	u2->setTeam(t);

	// Missing waypoint: positions unchanged
	run(e, act("TEAM_TELEPORT_TO_WAYPOINT", { sp(3, qual(t)), sp(7, "WP_Missing") }));
	CHECK(u1->getPosition()->x == 100.0f);
	CHECK(u2->getPosition()->x == 140.0f);

	// Valid waypoint: both members teleported to waypoint location
	run(e, act("TEAM_TELEPORT_TO_WAYPOINT", { sp(3, qual(t)), sp(7, "WP_L189_Dest") }));
	CHECK(u1->getPosition()->x == 320.0f);
	CHECK(u1->getPosition()->y == 440.0f);
	CHECK(u2->getPosition()->x == 320.0f);
	CHECK(u2->getPosition()->y == 440.0f);

	// RW 0x7C28FC: no-op, leaves team membership intact
	run(e, act("PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS", { sp(11, "Alice"), sp(3, qual(t)) }));
	CHECK(u1->getTeam() == t);
	CHECK(u2->getTeam() == t);
}

// RW 0x7C10C0 (SET_STOPPING_DISTANCE, case 144): sets Locomotor +0x3C (m_closeEnoughDist) when dist >= 0.5f (RW 0xBD869C).
TEST_CASE("LOCAL-189 SET_STOPPING_DISTANCE (RW 0x7C10C0) updates Locomotor m_closeEnoughDist when >= 0.5f")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();

	Object *mover = w.unit("Swordsman", 'A', 200.0f, 200.0f);
	REQUIRE(mover);
	AIUpdateInterface *ai = mover->getAIUpdateInterface();
	REQUIRE(ai);
	Locomotor *loco = ai->curLocomotor();
	REQUIRE(loco);
	TeamFactory &teams = w.logic->players().teams();
	Team *t = teams.createTeam(teams.initTeam("StopDistTeam", w.playerOf('A'), true), true);
	REQUIRE(t);
	mover->setTeam(t);

	const float initial = loco->getCloseEnoughDist();
	// Below 0.5f threshold (RW 0xBD869C): ignored
	run(e, act("SET_STOPPING_DISTANCE", { sp(3, qual(t)), rp(0.4f) }));
	CHECK(loco->getCloseEnoughDist() == initial);

	// At/above 0.5f threshold: applied
	run(e, act("SET_STOPPING_DISTANCE", { sp(3, qual(t)), rp(12.5f) }));
	CHECK(loco->getCloseEnoughDist() == 12.5f);
}

// RW 0x7C3B6E (TEAM_TOGGLE_CLOSE_RANGE_WEAPON, case 469) & RW 0x7C3C0A (NAMED_TOGGLE_CLOSE_RANGE_WEAPON, case 470):
// sets DualWeaponBehavior +0x20 (m_closeRequest).
TEST_CASE("LOCAL-189 TEAM/NAMED_TOGGLE_CLOSE_RANGE_WEAPON (RW 0x7C3B6E / RW 0x7C3C0A) toggles DualWeaponBehavior m_closeRequest")
{
	Local189Fixture fx;
	ScriptEngine &e = fx.w.logic->scriptEngine();

	TeamFactory &teams = fx.w.logic->players().teams();
	Team *t = teams.createTeam(teams.initTeam("HeroTeam", fx.p1, true), true);
	REQUIRE(t);
	Object *hero = fx.w.spawn("L189_DualHero", 100.0f, 100.0f, 0.0f, t);
	REQUIRE(hero);
	hero->setTeam(t);
	hero->setName("MyDualHero");

	DualWeaponBehavior *dw = DualWeaponBehavior::first(*hero);
	REQUIRE(dw);
	CHECK(!dw->closeRequest());

	run(e, act("NAMED_TOGGLE_CLOSE_RANGE_WEAPON", { sp(14, "MyDualHero"), ip(8, 1) }));
	CHECK(dw->closeRequest());

	run(e, act("TEAM_TOGGLE_CLOSE_RANGE_WEAPON", { sp(3, qual(t)), ip(8, 0) }));
	CHECK(!dw->closeRequest());

	run(e, act("TEAM_TOGGLE_CLOSE_RANGE_WEAPON", { sp(3, qual(t)), ip(8, 1) }));
	CHECK(dw->closeRequest());
}

// RW 0x7C4BDB (FIRE_SPECIAL_POWER_ON_TEAM, case 447), RW 0x7C6170 (FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE, case 445),
// RW 0x7C6040 (FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE_BY_PLAYER, case 446) -> RW 0x7C4CA0 -> RW 0x7C112E.
TEST_CASE("LOCAL-189 FIRE_SPECIAL_POWER_ON_TEAM (RW 0x7C4BDB) and _ON_NEAREST_OBJECTTYPE* (RW 0x7C6170 / RW 0x7C6040 / RW 0x7C4CA0)")
{
	Local189Fixture fx;
	ScriptEngine &e = fx.w.logic->scriptEngine();

	const SpecialPowerTemplate *spTmpl = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate("SP_L189_Test") : nullptr;
	REQUIRE(spTmpl);

	Object *caster = fx.w.spawn("L189_Caster", 50.0f, 50.0f, 0.0f, fx.p1->getDefaultTeam());
	REQUIRE(caster);
	SpecialPowerModuleInterface *spm = SpecialPowerModules::findModule(*caster, spTmpl);
	REQUIRE(spm);

	TeamFactory &teams = fx.w.logic->players().teams();
	Team *targetTeam = teams.createTeam(teams.initTeam("TgtTeam", fx.p2, true), true);
	REQUIRE(targetTeam);
	Object *t1 = fx.w.spawn("L189_Target", 200.0f, 200.0f, 0.0f, targetTeam);
	REQUIRE(t1);
	t1->setTeam(targetTeam);
	fx.w.frames(2);

	const std::string p1Name = fx.p1->getPlayerName();
	const std::string p2Name = fx.p2->getPlayerName();

	// 1. FIRE_SPECIAL_POWER_ON_TEAM (RW 0x7C4BDB)
	CHECK(spm->isReady());
	run(e, act("FIRE_SPECIAL_POWER_ON_TEAM", { sp(11, p1Name), sp(29, "SP_L189_Test"), sp(3, qual(targetTeam)) }));
	CHECK(!spm->isReady());

	// Advance past reload (10000ms = 50 frames at 5fps)
	fx.w.frames(60);
	REQUIRE(spm->isReady());

	// 2. FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE_BY_PLAYER (RW 0x7C6040): no L189_Target owned by p1 -> does not fire
	run(e, act("FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE_BY_PLAYER",
		{ sp(11, p1Name), sp(29, "SP_L189_Test"), sp(61, "L189_Target"), sp(3, qual(targetTeam)), sp(11, p1Name) }));
	CHECK(spm->isReady());

	// Owned by p2 -> fires via RW 0x7C4CA0 -> RW 0x7C112E
	run(e, act("FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE_BY_PLAYER",
		{ sp(11, p1Name), sp(29, "SP_L189_Test"), sp(61, "L189_Target"), sp(3, qual(targetTeam)), sp(11, p2Name) }));
	CHECK(!spm->isReady());

	fx.w.frames(60);
	REQUIRE(spm->isReady());

	// 3. FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE (RW 0x7C6170): any owner -> fires
	run(e, act("FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE",
		{ sp(11, p1Name), sp(29, "SP_L189_Test"), sp(61, "L189_Target"), sp(3, qual(targetTeam)) }));
	CHECK(!spm->isReady());
}

// RW 0x7C2CF5 (TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF, case 391) & RW 0x7C2E34 (_OWNED_BY_PLAYER, case 392).
TEST_CASE("LOCAL-189 TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF* (RW 0x7C2CF5 / RW 0x7C2E34)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();

	Object *mover = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	Object *ownStruct = w.unit("Dummy", 'A', 560.0f, 500.0f);
	Object *enemyStruct = w.unit("Dummy", 'B', 500.0f, 700.0f);
	REQUIRE(mover);
	REQUIRE(ownStruct);
	REQUIRE(enemyStruct);

	TeamFactory &teams = w.logic->players().teams();
	Team *t = teams.createTeam(teams.initTeam("KindOfMovers", w.playerOf('A'), true), true);
	REQUIRE(t);
	mover->setTeam(t);
	w.frames(2);

	int dummyBit = -1;
	const auto &mask = w.logic->templateInfo(ownStruct->getTemplate()).kindOf;
	for (int b = 0; b < 224; ++b)
	{
		if (MaskTest(mask, (unsigned)b) && !MaskTest(w.logic->templateInfo(mover->getTemplate()).kindOf, (unsigned)b))
		{
			dummyBit = b;
			break;
		}
	}
	if (dummyBit < 0)
	{
		for (int b = 0; b < 224; ++b)
		{
			if (MaskTest(mask, (unsigned)b))
			{
				dummyBit = b;
				break;
			}
		}
	}
	REQUIRE(dummyBit != -1); // tolerance: valid KindOf bit index on Dummy

	// Owned by Bob -> moves toward (500, 700)
	run(e, act("TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF_OWNED_BY_PLAYER", { sp(3, qual(t)), ip(27, dummyBit), sp(11, "Bob") }));
	w.frames(150);
	const float toEnemy = std::fabs(mover->getPosition()->x - 500.0f) + std::fabs(mover->getPosition()->y - 700.0f);
	CHECK(toEnemy < 45.0f); // tolerance: move ends within arrival distance and unit radii

	// Any player -> moves toward nearest (ownStruct at 560, 500 is closer once we teleport mover to 540, 500)
	Coord3D nearOwn{ 540.0f, 500.0f, 0.0f };
	mover->teleportTo(&nearOwn, false);
	w.frames(2);
	run(e, act("TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF", { sp(3, qual(t)), ip(27, dummyBit) }));
	w.frames(60);
	const float toOwn = std::fabs(mover->getPosition()->x - 560.0f) + std::fabs(mover->getPosition()->y - 500.0f);
	CHECK(toOwn < 40.0f); // tolerance: move ends within arrival distance and unit radii
}
