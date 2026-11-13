// OpenBFME unit tests. PORT-22 (issue #78): Pathfinder::adjustMeleeOffset (RW 0x6EA0EC) — the offset walk of the melee engagement and the siege deploy's
// approach point RW 0x8C9D06, which calls it at RW 0x8C9E7A (stop S-4941 is closed by it).
//
// What RotWK does (read from the supplied game.dat, caveat S-001), not from the ported code:
//   * RW 0x8C9E64: the approach point tests SkipAdjustPosition (module data + 0x22, passed RW 0x8C9DFE's squared length above 1.0 at RW 0x8C9E38); clear,
//     RW 0x8C9E6A .. 0x8C9E7F calls RW 0x6EA0EC with the deploying object (module + 0x08, RW 0x8C9E77), the dock object (the first argument, EDI) and the
//     offset scaled by 0.1 (RW 0xBD83D4), then writes the approach point as the dock's position + the bent offset (RW 0x8C9E7F .. 0x8C9EAD). Set, the jump at
//     RW 0x8C9E68 keeps the offset and the dock's own point (RW 0x8C9EB2);
//   * RW 0x6EA0EC: i = 0 .. 19 (RW 0x6EA1E7 compares with 0x14); p = the target's position (RW 0x68C9AC) + v * i (RW 0x6EA147 .. 0x6EA183) and its cell
//     (RW 0x5E2EF2 = floor(x / 10), floor(y / 10), RW 0x6E8CE6) on the target's layer (RW 0x68BBE0); no cell: `v` stays; a cell whose type (the low nibble of
//     cell + 0x0C, RW 0x6EA1E0) is not 4 (OBSTACLE) is the one that leaves the obstacle: `v` is scaled by i (RW 0x6EA1B1 .. 0x6EA1D4) and the walk ends. i = 0
//     counts, so a target standing on a free cell zeroes `v`; twenty obstacle cells leave `v` alone. The contact-point step before it (RW 0x6EA10B .. 0x6EA125,
//     RW 0x667C76 through the unit's AI) is the melee lane's inference `false` (stop S-787) and is not exercised here.
//
// Synthetic data only: engine/tests/PathfindTestUtil.h's terrain and objects (no retail files) for the pathfinder, engine/tests/MoveTestUtil.h's logic world
// (its Walker template carries AIUpdateInterface and a LocomotorSet; the wall and ladder templates below are written here) for the siege module. The dock
// points are the wall's SIEGELADDER bone, read by SiegeDockingBehavior::initializeBones (RW 0x85CE65) through the client's bone provider: the tests install a
// ProjectileLaunchOffsets that answers one bone, the same seam a drawable uses in the game (no client, no bone).
//
// TEST_CASE names start with PORT-22 (the run filter -tc="PORT-22*").
#include "doctest.h"

#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Object/Object.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace pathtest;
using namespace movetest;

namespace
{

// ---- the reach of a melee weapon at `reach` around `victim`: RW 0x6CC07C's two questions (from a point to the victim, and from a point to an explicit
// victim position, which is how RW 0x6F37B3 asks the reach of a candidate point)
class ReachRange : public PathfindAttackRange
{
public:
	ReachRange(Coord3D victim, float reach) : m_victim(victim), m_reach(reach) {}
	bool inRangeFrom(const Coord3D &from, float extra) const override { return within(from, m_victim, extra); }
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override { return within(from, victimPos, extra); }
	float attackDistance() const override { return m_reach; }

private:
	bool within(const Coord3D &a, const Coord3D &b, float extra) const
	{
		const float dx = a.x - b.x, dy = a.y - b.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}
	Coord3D m_victim;
	float m_reach;
};

// a structure on the grid: its footprint's cells become obstacles (RW 0x936B7D through the pathfinder's map build)
TestObject *addStructure(TestWorld &world, std::vector<std::unique_ptr<TestObject>> &owned, Pathfinder &pf, PathfindObjectID id, float x, float y, float major,
	float minor)
{
	owned.emplace_back(new TestObject());
	TestObject *o = owned.back().get();
	o->id = id;
	o->pos = Coord3D{ x, y, 0.0f };
	o->structure = true;
	o->team = 2;
	o->geometry.type = PATHFIND_GEOMETRY_BOX;
	o->geometry.majorRadius = major;
	o->geometry.minorRadius = minor;
	world.objects[id] = o;
	pf.addObjectToPathfindMap(*o);
	return o;
}

} // namespace


// ---------------------------------------------------------------------------------------------------------------------------------------------------
// RW 0x6EA0EC on its own: the step count of the walk out of the obstacle
// ---------------------------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("PORT-22 pathfind: adjustMeleeOffset scales the offset by the first of i = 0 .. 19 steps whose cell is not an obstacle (RW 0x6EA12B .. 0x6EA1F0)")
{
	SyntheticTerrain terrain(60, 60); // the walls of this case reach cell 43
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	std::vector<std::unique_ptr<TestObject>> owned;

	// a target on a free cell: i = 0 is already free, so the offset is scaled by 0 (RW 0x6EA1B1 with EDI = 0)
	TestObject free;
	free.id = 5;
	free.pos = Coord3D{ 255.0f, 155.0f, 0.0f };
	free.mobile = true;
	free.team = 1;
	world.objects[free.id] = &free;
	REQUIRE(pf.getCell(LAYER_GROUND, 25, 15)->getType() != PathfindCell::CELL_OBSTACLE);
	Coord3D v{ 10.0f, -4.0f, 2.0f };
	pf.adjustMeleeOffset(unit, free, v);
	CHECK(v.x == 0.0f);
	CHECK(v.y == 0.0f);
	CHECK(v.z == 0.0f);

	// a box 240 x 30 at (300, 305): the box rasteriser writes x cells 18 .. 41 and y cells 29 .. 32 (RW 0x936EE8: ceil(radius * 0.4) steps of 5 from the
	// top-left corner, (x + 0.5) / 10 floored), so the map has to be wider than 42 cells. Twenty steps of 1 along +x from the target's position stay inside
	// it, so `v` is unchanged (RW 0x6EA1E7)
	TestObject *longWall = addStructure(world, owned, pf, 7, 300.0f, 305.0f, 120.0f, 15.0f);
	REQUIRE(pf.getCell(LAYER_GROUND, 30, 30)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 31, 30)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 35, 30)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 42, 30)->getType() != PathfindCell::CELL_OBSTACLE);
	Coord3D inside{ 1.0f, 0.0f, 0.0f };
	pf.adjustMeleeOffset(unit, *longWall, inside);
	CHECK(inside.x == 1.0f);
	CHECK(inside.y == 0.0f);
	CHECK(inside.z == 0.0f);

	// a second box 30 x 30 at (425, 305): its cells are x 41 .. 43 x y 29 .. 32. From the target's cell 42 the steps of 10 land on 43 (i = 1) and 44
	// (i = 2), so the walk leaves the obstacle at i = 2 and the offset is scaled by 2 (RW 0x6EA1B1)
	TestObject *shortWall = addStructure(world, owned, pf, 8, 425.0f, 305.0f, 15.0f, 15.0f);
	REQUIRE(pf.getCell(LAYER_GROUND, 42, 30)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 43, 30)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 44, 30)->getType() != PathfindCell::CELL_OBSTACLE);
	Coord3D two{ 10.0f, 0.0f, 0.0f };
	pf.adjustMeleeOffset(unit, *shortWall, two);
	CHECK(two.x == 20.0f);
	CHECK(two.y == 0.0f);
	CHECK(two.z == 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the melee caller: RW 0x6F3905 still runs RW 0x6EA0EC on its v (behaviour unchanged by the extraction)
// ---------------------------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("PORT-22 melee engagement: a structure target's step toward the unit is bent out of the target's footprint (RW 0x6F3905 -> RW 0x6EA0EC)")
{
	SyntheticTerrain terrain(40, 40);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 155.0f, 155.0f, 0.0f }; // facing +x (angle 0)
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	std::vector<std::unique_ptr<TestObject>> owned;
	// the target: a 30 x 30 box at (205, 155), its cells x 19 .. 21 x y 14 .. 16
	TestObject *target = addStructure(world, owned, pf, 5, 205.0f, 155.0f, 15.0f, 15.0f);
	REQUIRE(pf.getCell(LAYER_GROUND, 20, 15)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 19, 15)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 18, 15)->getType() != PathfindCell::CELL_OBSTACLE);

	// the reach 40: the target is 50 away from the unit, so the engagement search runs. v = normalize(ahead - dest) * 10 = (-10, 0, 0) with dest = the
	// target's position; from (205, 155) the steps -10 * i land on cells 20 (i = 0), 19 (i = 1) and 18 (i = 2): the first free one scales v to (-20, 0, 0)
	// (RW 0x6EA1B1), so the engagement destination (205 - 20, 155) = (185, 155) is a free cell 30 from the unit that the reach 40 covers (RW 0x6F37B3's
	// direct check: the cell's footprint RW 0x6EBE89, crowd 0, RW 0x6CC07C from the unit's position)
	Coord3D dest = target->pos;
	const ReachRange reach(target->pos, 40.0f);
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), *target, reach, &dest));
	CHECK(dest.x == 185.0f);
	CHECK(dest.y == 155.0f);
	CHECK(dest.z == 0.0f);
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the siege deploy: RW 0x8C9D06's approach point
// ---------------------------------------------------------------------------------------------------------------------------------------------------
namespace
{

// the mod's ladder and wall as the tests need them: the layer's Walker template (AIUpdateInterface + WalkerLoco) with the siege deploy and the SIEGE_LADDER
// KindOf, a 30 x 30 structure that carries SiegeDockingBehavior. SkipAdjustPosition is the field the two halves of the scene differ in.
const char kPort22Objects[] =
	"Object Port22Ladder\n"
	"  KindOf = INFANTRY SELECTABLE SIEGE_LADDER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SiegeDeploySpecialPower ModuleTag_Deploy\n"
	"    SkipAdjustPosition = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port22LadderSkip\n"
	"  KindOf = INFANTRY SELECTABLE SIEGE_LADDER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SiegeDeploySpecialPower ModuleTag_Deploy\n"
	"    SkipAdjustPosition = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port22Wall\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  Behavior = SiegeDockingBehavior ModuleTag_Dock\n"
	"  End\n"
	"End\n";

// the client's bone provider (ProjectileLaunchOffsets, WeaponDelivery.h): one SIEGELADDER bone 40 along the wall's local X, its X axis +x (so the dock
// direction, the negated X axis, is -x). SiegeDockingBehavior::initializeBones takes it through Object::getMultiLogicalBonePosition (RW 0x68C650)
class WallBones : public ProjectileLaunchOffsets
{
public:
	bool launchOffset(const Object &, int, int, float[12]) override { return false; } // no weapon launch bone is asked here
	int multiLogicalBones(const Object &, const std::string &prefix, const std::array<std::uint32_t, 19> &, int maxBones, float (*out)[12]) override
	{
		if (prefix != "SIEGELADDER" || maxBones < 1)
		{
			return 0;
		}
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				out[0][r * 4 + c] = r == c ? 1.0f : 0.0f;
			}
		}
		out[0][3] = 40.0f; // the bone's translation: 40 along the wall's X axis
		return 1;
	}
};

// the scene: the wall at the centre of cell (50, 50) (505, 505) whose footprint the box rasteriser writes to x 49 .. 51 x y 49 .. 52 (RW 0x936EE8), the
// ladder 300 away on the same y, one logic frame so the dock reads its bone, then the deploy order at the wall. The approach point is the wall's position +
// the bent offset: v = (545 - 505, 0, 0) * 0.1 = (4, 0, 0), whose steps i = 0 .. 3 land on cells 50, 50, 51, 51 (obstacle) and i = 4 on cell 52 (free),
// so v becomes (16, 0, 0) and the approach point (521, 505) — or, with SkipAdjustPosition, the dock point (545, 505).
struct SiegeScene
{
	MoveWorld mw{ 100, 100, kPort22Objects };
	WallBones bones;
	Object *wall = nullptr;
	Object *ladder = nullptr;
	SiegeDeploySpecialPower *deploy = nullptr;
	SiegeDockingBehavior *dock = nullptr;

	explicit SiegeScene(const char *ladderTemplate)
	{
		wall = mw.spawn("Port22Wall", 505.0f, 505.0f);
		mw.buildMap(); // the footprint of the wall is on the grid now
		ladder = mw.spawn(ladderTemplate, 505.0f, 205.0f);
		mw.logic->combat().setLaunchOffsets(&bones);
		mw.frames(2); // the dock's first update (RW 0x85D216) reads the bones
		mw.logic->combat().setLaunchOffsets(nullptr);
		dock = SiegeDockingBehavior::of(*wall);
		deploy = SiegeDeploySpecialPower::of(*ladder);
	}
	~SiegeScene() { mw.logic->combat().setLaunchOffsets(nullptr); }
};

} // namespace


TEST_CASE("PORT-22 siege deploy: SkipAdjustPosition No bends the approach offset out of the dock's footprint (RW 0x8C9E7A -> RW 0x6EA0EC)")
{
	SiegeScene s("Port22Ladder");
	REQUIRE(s.mw.logic->aiWorld() != nullptr);
	Pathfinder &pf = s.mw.logic->aiWorld()->pathfinder();
	// the grid the expectation above is written on: the wall's own cell and the two to its right are obstacles, cell 52 is not
	REQUIRE(pf.getCell(LAYER_GROUND, 50, 50)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 51, 50)->getType() == PathfindCell::CELL_OBSTACLE);
	REQUIRE(pf.getCell(LAYER_GROUND, 52, 50)->getType() != PathfindCell::CELL_OBSTACLE);
	REQUIRE(s.dock != nullptr);
	REQUIRE(s.dock->entries().size() == 1u);
	REQUIRE(s.dock->entries()[0].type == 1); // a SIEGELADDER bone: a ladder takes it (RW 0x85CCB8)
	CHECK(s.dock->entries()[0].position.x == 545.0f);
	CHECK(s.dock->entries()[0].position.y == 505.0f);
	REQUIRE(s.deploy != nullptr);
	REQUIRE_FALSE(s.deploy->data().m_skipAdjustPosition);

	s.deploy->initiateIntentToDoSpecialPower(nullptr, s.wall, nullptr, 0, 0); // RW 0x8CA5C2
	REQUIRE(s.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(s.deploy->dockId() == s.wall->getID());
	CHECK(s.deploy->approachPoint().x == 521.0f); // 505 + 16: i = 4 leaves the obstacle (RW 0x6EA1B1)
	CHECK(s.deploy->approachPoint().y == 505.0f);
	CHECK(s.deploy->approachPoint().z == 0.0f);
	// the dock point itself is 545: the offset is what changed, not the entry (RW 0x8C9D06 leaves the entry alone)
	CHECK(s.dock->dockPosition(0).x == 545.0f);
}

TEST_CASE("PORT-22 siege deploy: SkipAdjustPosition Yes keeps the dock point (RW 0x8C9E64 -> RW 0x8C9E68 -> RW 0x8C9EB2)")
{
	SiegeScene s("Port22LadderSkip");
	REQUIRE(s.dock != nullptr);
	REQUIRE(s.dock->entries().size() == 1u);
	REQUIRE(s.deploy != nullptr);
	REQUIRE(s.deploy->data().m_skipAdjustPosition);

	s.deploy->initiateIntentToDoSpecialPower(nullptr, s.wall, nullptr, 0, 0); // RW 0x8CA5C2
	REQUIRE(s.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(s.deploy->approachPoint().x == 545.0f);
	CHECK(s.deploy->approachPoint().y == 505.0f);
	CHECK(s.deploy->approachPoint().z == 0.0f);
}
