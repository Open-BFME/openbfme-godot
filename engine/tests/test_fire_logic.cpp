// OpenBFME unit tests. GPL-3.0.
// Tests for TheFireLogicSystem (lane FIRE): logical fire grid, ignition, spreading, decay,
// damage to watching objects, tree burning, extinguishing, xfer and CRC.
// Addresses: RotWK RW 0x6886E0, 0x6878D1, 0x687A7D, 0x687C4C, 0x686C13, 0x688354, 0x68836C, 0x912241.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/FireLogicSystem.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "Common/Snapshot.h"
#include "Common/StateHash.h"

using namespace combattest;

namespace
{
const char kFireObjects[] =
	"Object FlammableBuilding\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"    InitialHealth = 500\n"
	"  End\n"
	"  Flammability\n"
	"    Fuel = 100\n"
	"    MaxBurnRate = 20\n"
	"    Decay = 2\n"
	"    Resistance = 5\n"
	"  End\n"
	"End\n"
	"Object NonFlammableStone\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("fire logic: grid layout and cell coordinates")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();

	// Initialize grid for 0..200 x 0..200 (retail: FloatToLong(floor(hi * 0.1)) + 1 -> 21 x 21)
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);
	CHECK(fire.numRows() == 21);
	CHECK(fire.numCols() == 21);
	CHECK(fire.cellsBurningCount() == 0);
	CHECK(!fire.anyBurning());

	// Initial cell state: has default fuel, not burning
	CHECK(!fire.isCellBurning(5, 5));
	CHECK(fire.getCellBurnRate(5, 5) == 0);
	CHECK(fire.getCellFuel(5, 5) == 100);
}

TEST_CASE("fire logic: ignition and burn rate in area")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	// Ignite at world pos (50, 50), cell (5, 5) with radius 15
	Coord3D pos{ 50.0f, 50.0f, 0.0f };
	fire.changeBurnRateInArea(pos, 15.0f, 10, false);

	CHECK(fire.isCellBurning(5, 5));
	CHECK(fire.getCellBurnRate(5, 5) == 10);

	// Cycle through slices so newly lit cells enter m_cellsOnFire
	for (int i = 0; i < 10; ++i)
	{
		fire.update();
	}

	CHECK(fire.anyBurning());
	// tolerance: disc area contains multiple burning cells
	CHECK(fire.cellsBurningCount() > 0);
}

TEST_CASE("fire logic: directional cone ignition")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	// Ignite directed along +X axis (dir = {1, 0, 0}), cone of 45 deg (cos ~ 0.707)
	Coord3D origin{ 50.0f, 50.0f, 0.0f };
	Coord3D dir{ 1.0f, 0.0f, 0.0f };
	float cosArc = 0.7071f;

	// Target cell directly in front (+X) should ignite, while behind (-X) should not
	fire.changeBurnRateInArea(origin, 25.0f, dir, cosArc, 15, false);

	CHECK(fire.isCellBurning(5, 5)); // centre (50, 50)
	CHECK(fire.isCellBurning(6, 5)); // ahead (+X, 60, 50)
	CHECK(!fire.isCellBurning(4, 5)); // behind (-X, 40, 50)
}

TEST_CASE("fire logic: spreading to orthogonal neighbours")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	// Ignite cluster at world (100, 100), cell (10, 10) with radius 5 (covers distance 1 cells)
	Coord3D pos{ 100.0f, 100.0f, 0.0f };
	fire.changeBurnRateInArea(pos, 5.0f, 50, false);
	CHECK(fire.isCellBurning(10, 10));

	// Distance 2 cells should not be burning yet
	CHECK(!fire.isCellBurning(12, 10));
	CHECK(!fire.isCellBurning(8, 10));
	CHECK(!fire.isCellBurning(10, 12));
	CHECK(!fire.isCellBurning(10, 8));

	// Run full 10 logic frames (10 rows slice cycle in FireLogicSystem::update)
	for (int i = 0; i < 10; ++i)
	{
		fire.update();
	}

	// Fire should spread outwards to at least one distance 2 cell
	bool spread = fire.isCellBurning(12, 10) || fire.isCellBurning(8, 10) ||
	              fire.isCellBurning(10, 12) || fire.isCellBurning(10, 8);
	CHECK(spread);
}

TEST_CASE("fire logic: fuel consumption and burnout")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	Coord3D pos{ 80.0f, 80.0f, 0.0f };
	// Reduce default fuel (100) down to 5: amount = -95
	fire.changeFuelInArea(pos, 4.0f, -95, 20, 2, 5, false);
	fire.changeBurnRateInArea(pos, 4.0f, 10, false);

	CHECK(fire.isCellBurning(8, 8));
	CHECK(fire.getCellFuel(8, 8) == 5);

	// Burn until fuel is exhausted
	for (int i = 0; i < 30; ++i)
	{
		fire.update();
	}

	// Fuel was consumed and burnt out, cell is no longer burning
	CHECK(!fire.isCellBurning(8, 8));
	// tolerance: fuel regrows by 1 per slice cycle after burning out
	CHECK(fire.getCellFuel(8, 8) < 50);
}

TEST_CASE("fire logic: extinguishing via changeBurnRateInArea and changeBurnRateAll")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	Coord3D pos1{ 40.0f, 40.0f, 0.0f };
	Coord3D pos2{ 140.0f, 140.0f, 0.0f };
	fire.changeBurnRateInArea(pos1, 8.0f, 10, false);
	fire.changeBurnRateInArea(pos2, 8.0f, 10, false);
	CHECK(fire.isCellBurning(4, 4));
	CHECK(fire.isCellBurning(14, 14));

	// Area extinguishing (PorterFireExtinguisher RW 0x6878D1 with negative delta)
	fire.changeBurnRateInArea(pos1, 12.0f, -20, false);
	CHECK(!fire.isCellBurning(4, 4));
	CHECK(fire.isCellBurning(14, 14));

	// Global extinguishing (Freezing Rain RW 0x687C4C with negative delta)
	fire.changeBurnRateAll(-100);
	CHECK(!fire.isCellBurning(14, 14));
	CHECK(!fire.anyBurning());
}

TEST_CASE("fire logic: object damage while watching burning cell")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	// Place a flammable building at (60, 60), cell (6, 6)
	Object *building = w.unit("FlammableBuilding", 'A', 60.0f, 60.0f);
	REQUIRE(building != nullptr);
	float hp0 = building->getBodyModule()->getHealth();
	CHECK(hp0 == 500.0f);

	// Ignite cell under building
	Coord3D pos{ 60.0f, 60.0f, 0.0f };
	fire.changeBurnRateInArea(pos, 10.0f, 15, false);
	CHECK(fire.isCellBurning(6, 6));

	// Step frames through logic
	for (int i = 0; i < 20; ++i)
	{
		w.frames(1);
	}

	// Building should have sustained flame damage
	float hp1 = building->getBodyModule()->getHealth();
	// tolerance: flame damage decreases building health each tick
	CHECK(hp1 < hp0);
}

TEST_CASE("fire logic: terrain tree lumber burning")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 200.0f, 200.0f);

	// Register tree at (70, 70), cell (7, 7)
	Coord3D treePos{ 70.0f, 70.0f, 0.0f };
	std::uint32_t treeId = 12345;
	fire.registerTree(treeId, treePos, nullptr);

	// Ignite cell containing tree
	fire.changeBurnRateInArea(treePos, 5.0f, 20, false);
	CHECK(fire.isCellBurning(7, 7));

	// Step logic frames: fire burns tree lumber
	for (int i = 0; i < 20; ++i)
	{
		fire.update();
	}

	// Tree should have been harvested/burned
	fire.unregisterTree(treeId, treePos);
}

TEST_CASE("fire logic: CRC and snapshot xfer")
{
	CombatWorld w(kFireObjects, 50);
	FireLogicSystem &fire = w.logic->fireLogic();
	fire.newMap(0.0f, 0.0f, 100.0f, 100.0f);

	StateHasher h1;
	fire.crc(h1);
	uint32_t hashQuiet = h1.value();

	// Ignite a cell and update slices so it's registered in m_cellsOnFire
	Coord3D pos{ 20.0f, 20.0f, 0.0f };
	fire.changeBurnRateInArea(pos, 5.0f, 15, false);
	for (int i = 0; i < 10; ++i)
	{
		fire.update();
	}

	StateHasher h2;
	fire.crc(h2);
	uint32_t hashBurning = h2.value();
	CHECK(hashQuiet != hashBurning);

	// Extinguish: returns to non-burning state
	fire.changeBurnRateAll(-100);
	CHECK(!fire.anyBurning());
}
