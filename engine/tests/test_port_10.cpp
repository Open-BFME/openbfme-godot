// OpenBFME unit tests. GPL-3.0.
// PORT-10 (issue #65) tests: FlammableUpdate / RunToWater — a burning object whose burning anim ended
// searches the water nearest to it and runs there; with no water within reach the burning goes on unchanged.
//
// RotWK 2.01 (addresses of the RunToWater search in FlammableUpdate::update RW 0x890738):
//   * the anim end reached with RunToWater set (RW 0x890779): the search RW 0x890782 starts; the object's AI is required
//     (RW 0x890782 .. 0x8907A7, without one: return 1, no move and no statuses);
//   * the candidate grid: the current point starts at trunc(position - radius) and steps trunc(current + increment),
//     both truncated with cvttss2si (RW 0x8907CB / 0x8907F8 at the init, RW 0x890DE2 / 0x890915 in the steps); the loops stop at
//     position + radius (untruncated float add, RW 0x8907D3 / 0x8907D7 for x, RW 0x890800 / 0x890804 for y); the x loop is outer;
//   * a candidate counts only when the depth (waterZ - terrainZ) is above RunToWaterDepth (slot 0x64 RW 0x67DB7E, an x87
//     double compare RW 0x89083A; the port's compare is subf32, as AIPathfind.cpp:457, at most 1 ulp at the border);
//   * the candidate keeps only when it is nearer ((posY-y)² + (posX-x)², the y part first, RW 0x89089E .. 0x8908A6) than the
//     best point so far, strictly (RW 0x8908AA, jbe keeps the old); best starts at (1e7f, 1e7f, 1e7f) (RW 0xBDE694 = 10000000.0f);
//   * a point wins only when the quick path test says so: RW 0x6F5BB0, quickDoesPathExist with no locomotor set of its own and a
//     nullptr 4th arg (RW 0x8908AF .. 0x8908C6);
//   * the point found must be under water (TerrainLogic slot 0x4C, RW 0x89096A): yes -> the PANIC locomotor (RW vslot 0x238(4),
//     only with PanicLocomotorWhileAflame, RW 0x890993), the move to the point with source 1 (CMD_FROM_SCRIPT, RW 0x66C4CA), the
//     run flag (module + 0x40), the deselection for every player (RW 0x625759), statuses UNSELECTABLE (0x3) and NO_ATTACK (0x5)
//     (RW 0x62684D) and the ent's EntEnragedUpdate told to stay calm (RW 0x8B64B8) instead of enraged; no -> the rage is forced
//     (RW 0x8B649F, RW 0x890A15), the burning goes on unchanged (the object does not move). EntEnragedUpdate is not ported in this
//     tree (the module's stop line names it), so its flag is not read here: what a test sees is the move, the statuses, the
//     locomotor set and the burn.
//   * under water with an AI that is not moving (RW 0x890A22 .. 0x890A88; the RW 0x664485 test = AIMover::isMoving, ported as
//     AIUpdateInterface::isMoving): the aflame end is shortened to now + 3 * LOGICFRAMES_PER_SECOND (3 * 5 frames, RW 0xD9F608 = 5,
//     an unsigned min over the aflame end, RW 0x890A76 .. 0x890A84) and the run flag is cleared (RW 0x890A86). A SHIP is never asked
//     (template + 0x11F bit 7).
//   * stopBurning (RW 0x88FF6F) ends the panic locomotor (RW 0x8900F0 .. 0x890119): only with PanicLocomotorWhileAflame and the
//     module's + 0x4C flag and an AI: RW vslot 0x238(0), LOCOMOTORSET_NORMAL, and the flag is cleared; the AI + 0x3C9 byte RW writes
//     is write-only in RotWK 2.01 (three writes, no readers) and is not carried by the port.
//
// Synthetic data (movetest::MoveWorld, pathtest::SyntheticTerrain: push the water BEFORE buildMap()); no retail or Ennorath files.
// The water z = 3 keeps the depth at 3, below the pathfinder's wade depth (5.0), so the water cells stay walkable — the ent
// wades in, as it does in the mod (its RunToWaterDepth is 0 and its panic locomotor's Surfaces are GROUND; the depth test, not a
// cell class, is what the search asks about).
// Mod shape this models: an Ennorath ent (RohanEntBase, PanicLocomotorWhileAflame = Yes) or a Flinkbaum (RunToWaterDepth = 0,
// RunToWaterSearchRadius = 10000, RunToWaterSearchIncrement = 50, PanicLocomotorWhileAflame = Yes); the test uses a radius of 200
// and a step of 5 (the same 1/50 ratio) so a frame stays cheap.
// The object burns forever (AflameDuration = 0, as the mod's ents): the only end of the burning in these tests is the water.
#include "doctest.h"
#include "MoveTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Object/Object.h"

#include <cstdio>
#include <string>

using namespace movetest;

namespace
{
const char kPort10Locomotors[] =
	"Locomotor EntLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 1500\n"
	"  TurnTimeDamaged = 1500\n"
	"  Acceleration = 300\n"
	"  Braking = 300\n"
	"  MinTurnSpeed = 12%\n"
	"  TurnThreshold = 15\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = HUGE_TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	"Locomotor EntPanicLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 1500\n"
	"  TurnTimeDamaged = 1500\n"
	"  Acceleration = 300\n"
	"  Braking = 300\n"
	"  MinTurnSpeed = 12%\n"
	"  TurnThreshold = 15\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = HUGE_TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n";

// BurningEnt: the mod's ents' shape (RunToWater = Yes, PanicLocomotorWhileAflame = Yes).
// BurningEntPanicNo: the same with PanicLocomotorWhileAflame = No.
// BurningEntNoRun: the same without RunToWater (the neighbor: the burning touches nothing).
const char kPort10Objects[] =
	"Object BurningEnt\n"
	"  KindOf = MONSTER SELECTABLE CAN_ATTACK PATH_THROUGH_INFANTRY HEAVY_MELEE_HITTER\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 24\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    AflameDamageDelay = 500\n"
	"    AflameDamageAmount = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    SetBurnedStatus = Yes\n"
	"    RunToWater = Yes\n"
	"    RunToWaterDepth = 0\n"
	"    RunToWaterSearchRadius = 200\n"
	"    RunToWaterSearchIncrement = 5\n"
	"    PanicLocomotorWhileAflame = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = EntLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 22\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = EntPanicLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 80\n"
	"  End\n"
	"End\n"
	"Object BurningEntPanicNo\n"
	"  KindOf = MONSTER SELECTABLE CAN_ATTACK PATH_THROUGH_INFANTRY HEAVY_MELEE_HITTER\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 24\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    AflameDamageDelay = 500\n"
	"    AflameDamageAmount = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    SetBurnedStatus = Yes\n"
	"    RunToWater = Yes\n"
	"    RunToWaterDepth = 0\n"
	"    RunToWaterSearchRadius = 200\n"
	"    RunToWaterSearchIncrement = 5\n"
	"    PanicLocomotorWhileAflame = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = EntLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 22\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = EntPanicLoco\n"
	"    Condition = SET_PANIC\n"
	"    Speed = 80\n"
	"  End\n"
	"End\n"
	"Object BurningEntNoRun\n"
	"  KindOf = MONSTER SELECTABLE CAN_ATTACK PATH_THROUGH_INFANTRY HEAVY_MELEE_HITTER\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 24\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    AflameDamageDelay = 500\n"
	"    AflameDamageAmount = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    SetBurnedStatus = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = EntLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 22\n"
	"  End\n"
	"End\n";

const int kStatusAflame = CombatNames::status("AFLAME");
const int kStatusUnselectable = CombatNames::status("UNSELECTABLE");
const int kStatusNoAttack = CombatNames::status("NO_ATTACK");

FlammableUpdate *flameOf(Object *o)
{
	return dynamic_cast<FlammableUpdate *>(o->findModule("FlammableUpdate"));
}

} // namespace

TEST_CASE("PORT-10 burning RunToWater object with water within reach runs to it: unselected, statuses UNSELECTABLE and NO_ATTACK, the panic locomotor (RW 0x89098C .. 0x890A1A)")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 300.0f, 100.0f, 500.0f, 300.0f, 3.0f }); // a river, depth 3
	mw.buildMap();
	Object *ent = mw.spawn("BurningEnt", 190.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	REQUIRE(ent->testStatus((unsigned)kStatusAflame));
	mw.frames(3);
	const AIUpdateInterface *ai = mw.aiOf(ent);
	REQUIRE(ai);
	CHECK(ent->testStatus((unsigned)kStatusUnselectable)); // RW 0x8909ED: status 3 (TheObjectStatusNames[3] UNSELECTABLE)
	CHECK(ent->testStatus((unsigned)kStatusNoAttack));     // RW 0x8909F7: status 5 (TheObjectStatusNames[5] NO_ATTACK)
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_PANIC);    // RW 0x890A7: AI vslot 0x238(4)
	// RW 0x890A04 tells the ent's EntEnragedUpdate to stay calm; the module is not ported in this tree, so there is no flag to read here
	// the search happened: the report carries no "not ported" line about the RunToWater search
	bool runToWaterStop = false;
	for (const std::string &line : mw.logic->report().stops)
	{
		runToWaterStop = runToWaterStop || line.find("RunToWater") != std::string::npos;
	}
	CHECK_FALSE(runToWaterStop);
}

TEST_CASE("PORT-10 the burning ent reaches the water and the burning ends there (RW 0x890A22 .. 0x890A88): the aflame end is shortened once it is under water and still")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 300.0f, 100.0f, 500.0f, 300.0f, 3.0f });
	mw.buildMap();
	Object *ent = mw.spawn("BurningEnt", 190.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	const unsigned aflame = (unsigned)kStatusAflame;
	int entered = -1;
	int quenched = -1;
	for (int f = 0; f < 400 && quenched < 0; ++f)
	{
		mw.frames(1);
		const Coord3D *p = ent->getPosition();
		if (entered < 0 && mw.terrain.isUnderwater(p->x, p->y, nullptr, nullptr))
		{
			entered = f;
		}
		if (!ent->testStatus(aflame))
		{
			quenched = f;
		}
	}
	std::printf("port10 quench: entered water at frame %d, aflame cleared at frame %d\n", entered, quenched);
	CHECK(entered >= 0); // tolerance: the frame the walk wades in is data- and turn-time-dependent (movement, not logic)
	CHECK(quenched >= 0); // tolerance: the frame the burning ends depends on the walk and the 15-frame shortening (RW 0x890A62 .. 0x890A84); AflameDuration = 0, so without the shortening it never ends
	if (entered >= 0 && quenched >= 0)
	{
		CHECK(quenched - entered <= 60); // tolerance: 3 * LOGICFRAMES_PER_SECOND = 15 frames (RW 0x890A62 .. 0x890A84) plus the arrival and the scheduler's frames
	}
	const AIUpdateInterface *ai = mw.aiOf(ent);
	REQUIRE(ai);
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);       // RW 0x8900F0 .. 0x890119: the panic locomotor ended in stopBurning
	CHECK_FALSE(ent->testStatus((unsigned)kStatusUnselectable)); // RW 0x88FFB3: stopBurning cleared the run-to-water statuses
	CHECK_FALSE(ent->testStatus((unsigned)kStatusNoAttack));
	CHECK(mw.terrain.isUnderwater(ent->getPosition()->x, ent->getPosition()->y, nullptr, nullptr)); // it is still standing in the water
}

TEST_CASE("PORT-10 water only beyond RunToWaterSearchRadius: no water found (RW 0x890A15), the object does not move")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 700.0f, 700.0f, 800.0f, 800.0f, 3.0f }); // 500+ units away, the search radius is 200
	mw.buildMap();
	Object *ent = mw.spawn("BurningEnt", 190.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	mw.frames(10);
	// RW 0x890A15 forces the ent's rage; EntEnragedUpdate is not ported in this tree, so the no-water branch shows only as the object staying put
	CHECK_FALSE(ent->testStatus((unsigned)kStatusUnselectable));
	CHECK_FALSE(ent->testStatus((unsigned)kStatusNoAttack));
	CHECK(mw.aiOf(ent)->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	const Coord3D *p = ent->getPosition();
	CHECK(p->x == 190.0f); // it never ran anywhere
	CHECK(p->y == 190.0f);
	CHECK(ent->testStatus((unsigned)kStatusAflame)); // the burning goes on unchanged (AflameDuration = 0)
}

TEST_CASE("PORT-10 water at or below RunToWaterDepth: no water found (RW 0x89083A .. 0x89083E), the object does not move")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 300.0f, 100.0f, 500.0f, 300.0f, 0.0f }); // z = 0: the depth is 0, not above RunToWaterDepth = 0
	mw.buildMap();
	Object *ent = mw.spawn("BurningEnt", 190.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	mw.frames(10);
	// RW 0x890A15 forces the ent's rage; EntEnragedUpdate is not ported in this tree
	CHECK_FALSE(ent->testStatus((unsigned)kStatusUnselectable));
	CHECK_FALSE(ent->testStatus((unsigned)kStatusNoAttack));
	CHECK(mw.aiOf(ent)->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	const Coord3D *p = ent->getPosition();
	CHECK(p->x == 190.0f);
	CHECK(p->y == 190.0f);
	CHECK(ent->testStatus((unsigned)kStatusAflame));
}

TEST_CASE("PORT-10 PanicLocomotorWhileAflame = No: the object still runs to the water but keeps the NORMAL locomotor set (RW 0x890993)")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 300.0f, 100.0f, 500.0f, 300.0f, 3.0f });
	mw.buildMap();
	Object *ent = mw.spawn("BurningEntPanicNo", 250.0f, 190.0f, 0.0f); // 50 units from the river: the NORMAL locomotor (Speed 22) is slower than the PANIC one
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	mw.frames(3);
	const AIUpdateInterface *ai = mw.aiOf(ent);
	REQUIRE(ai);
	CHECK(ent->testStatus((unsigned)kStatusUnselectable)); // RW 0x8909ED: the statuses are set regardless of the panic flag
	CHECK(ent->testStatus((unsigned)kStatusNoAttack));     // RW 0x8909F7
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);   // RW 0x890993: the PANIC locomotor is chosen only with the flag (data + 0x48)
	// RW 0x890A04 suppresses the ent either way; EntEnragedUpdate is not ported in this tree
	// and the run ends in the water the same way (the under-water branch RW 0x890A22 does not ask about the panic flag)
	int quenched = -1;
	for (int f = 0; f < 700 && quenched < 0; ++f)
	{
		mw.frames(1);
		if (!ent->testStatus((unsigned)kStatusAflame))
		{
			quenched = f;
		}
	}
	if (quenched < 0)
	{
		std::printf("port10 panic-no: not quenched; position (%.1f, %.1f), locomotor set %d\n", ent->getPosition()->x, ent->getPosition()->y,
			(int)ai->curLocomotorSet());
	}
	CHECK(quenched >= 0); // tolerance: the walk to the water and the 15-frame shortening are data- and turn-time-dependent
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL); // RW 0x8900F0: the panic end is skipped without the flag — and it never switched away
	CHECK_FALSE(ent->testStatus((unsigned)kStatusUnselectable));
	CHECK_FALSE(ent->testStatus((unsigned)kStatusNoAttack));
}

TEST_CASE("PORT-10 RunToWater = No (the neighbor of the RunToWater path): the burning touches nothing, no move, no statuses")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 300.0f, 100.0f, 500.0f, 300.0f, 3.0f });
	mw.buildMap();
	Object *ent = mw.spawn("BurningEntNoRun", 190.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	mw.frames(10);
	// RW 0x89077C: the RunToWater clear jumps to return 1, the ent is never told (and has no EntEnragedUpdate in this tree)
	CHECK_FALSE(ent->testStatus((unsigned)kStatusUnselectable));
	CHECK_FALSE(ent->testStatus((unsigned)kStatusNoAttack));
	CHECK(mw.aiOf(ent)->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	const Coord3D *p = ent->getPosition();
	CHECK(p->x == 190.0f);
	CHECK(p->y == 190.0f);
	CHECK(ent->testStatus((unsigned)kStatusAflame)); // RW 0x890A1A's neighbor: the burning goes on forever (AflameDuration = 0)
}

TEST_CASE("PORT-10 the nearest reachable water point is chosen (RW 0x8908AA: the strictly nearer candidate wins over the first the scan sees)")
{
	MoveWorld mw(100, 100);
	const std::string err = mw.w.load(std::string(kPort10Locomotors) + kPort10Objects, INI_LOAD_OVERWRITE, "port10.ini"); // one file: locomotors and objects together
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.water.push_back({ 510.0f, 150.0f, 550.0f, 230.0f, 3.0f }); // ahead, 60 units away (facing +x)
	mw.terrain.water.push_back({ 300.0f, 150.0f, 340.0f, 230.0f, 3.0f }); // behind, 110 units away; the scan sees it first (x grows from trunc(x - 200))
	mw.buildMap();
	Object *ent = mw.spawn("BurningEnt", 450.0f, 190.0f, 0.0f);
	FlammableUpdate *fl = flameOf(ent);
	REQUIRE(fl);
	mw.frames(1);
	fl->tryToIgnite(); // ignite after frame 0: the custom-anim end is `now`, and the update's anim-end branch (RW 0x89076E) is skipped when it is 0
	mw.frames(40);
	const float x = ent->getPosition()->x;
	std::printf("port10 nearest: at frame 40 the object is at x = %.1f\n", x);
	CHECK(x > 450.0f); // tolerance: the object accelerates from rest toward the nearer water; after 40 frames it is strictly past its spawn x
}
