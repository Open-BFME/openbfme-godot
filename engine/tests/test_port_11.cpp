// OpenBFME unit tests. GPL-3.0.
// PORT-11: SlavedUpdate/StayOnSameLayerAsMaster. Synthetic INI only; no game data. Expectations are the RotWK update's own branch
// (RW 0x8A5E66 .. 0x8A5E88, getter 0x68BBE0 / setter 0x68BB9D) and the neighboring gates read in SlavedUpdate.h, not the port's implementation.

#include "doctest.h"
#include "MoveTestUtil.h"

#include "Common/Team.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Weapon.h"

using namespace movetest;

namespace
{
const char kPort11Objects[] =
	"Object P11Master\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P11SlaveYes\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SlavedUpdate ModuleTag_Slaved\n"
	"    StayOnSameLayerAsMaster = Yes\n"
	"    AttackRange = 300\n"
	"    DistToTargetToGrantRangeBonus = 50\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P11SlaveNo\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SlavedUpdate ModuleTag_Slaved\n"
	"    StayOnSameLayerAsMaster = No\n"
	"    AttackRange = 300\n"
	"    DistToTargetToGrantRangeBonus = 50\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P11SlaveNoAI\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = SlavedUpdate ModuleTag_Slaved\n"
	"    StayOnSameLayerAsMaster = Yes\n"
	"  End\n"
	"End\n";

struct Port11World : MoveWorld
{
	Port11World() : MoveWorld(20, 20, kPort11Objects) {} // no map surfaces: the stored layer is the state under test, not a terrain classification
};

SlavedUpdate *slavedOf(Object &o)
{
	auto *m = dynamic_cast<SlavedUpdate *>(o.findModule("SlavedUpdate"));
	REQUIRE_MESSAGE(m != nullptr, "the synthetic slave has a SlavedUpdate");
	return m;
}

void enslave(Object &master, Object &slave)
{
	slavedOf(slave)->onEnslave(&master);
	REQUIRE(slavedOf(slave)->getSlaverID() == master.getID());
}

int bonusBit(const char *name)
{
	const int bit = INI::scanIndexList(name, TheWeaponBonusConditionNames);
	REQUIRE_MESSAGE(bit != -1, name);
	return bit;
}

bool hasBonus(const Object &o, int bit)
{
	return (o.weaponBonusConditionMask() & (1u << bit)) == (1u << bit);
}

std::uint32_t bonusMaskWithoutDrone()
{
	return (1u << bonusBit("GARRISONED")) | (1u << bonusBit("HORDE")) | (1u << bonusBit("PLAYER_UPGRADE"));
}
} // namespace

TEST_CASE("PORT-11 layer: an enabled slave copies a non-ground master on the next update and follows later changes")
{
	Port11World w;
	Object *master = w.spawn("P11Master", 100, 100);
	Object *slave = w.spawn("P11SlaveYes", 100, 100);
	enslave(*master, *slave);
	// RW 0x68BBE0 returns Object + 0x428; the port stores that layer in ObjectPathfindAdapter. Use a wall, a ramp, a raised layer and ground.
	for (PathfindLayerEnum layer : { LAYER_WALL_FIRST, LAYER_RAMP, LAYER_TOP, LAYER_GROUND })
	{
		w.ai->adapterFor(*master).setLayer(layer);
		w.ai->adapterFor(*slave).setLayer(LAYER_GROUND);
		const Coord3D before = *slave->getPosition();
		w.frames(1); // RW 0x8A5E6E / 0x8A5E77 run in the scheduled update, after the live-master / ALLIES gate
		CHECK(w.ai->adapterFor(*slave).getLayer() == layer);
		CHECK(w.ai->adapterFor(*master).getLayer() == layer);
		CHECK(slave->getPosition()->x == before.x);
		CHECK(slave->getPosition()->y == before.y);
		CHECK(slave->getPosition()->z == before.z);
		CHECK(slavedOf(*slave)->getSlaverID() == master->getID());
	}
}

TEST_CASE("PORT-11 layer: a master without an AI module still supplies its Object layer")
{
	Port11World w;
	Object *master = w.spawn("Block", 100, 100); // no AIUpdateInterface; RW's getter is an Object method, not an AI method
	Object *slave = w.spawn("P11SlaveYes", 100, 100);
	enslave(*master, *slave);
	REQUIRE(master->getAIUpdateInterface() == nullptr);
	w.ai->adapterFor(*master).setLayer(LAYER_WALL_FIRST);
	w.frames(1);
	CHECK(w.ai->adapterFor(*slave).getLayer() == LAYER_WALL_FIRST);
}

TEST_CASE("PORT-11 bonus: both flag paths clear only the master's DRONE_SPOTTING bit")
{
	const int drone = bonusBit("DRONE_SPOTTING");
	CHECK(drone == 6); // RW name-pointer table 0xDA1740, entry 6 -> 0xC18008 "DRONE_SPOTTING"
	const std::uint32_t others = bonusMaskWithoutDrone();
	for (const char *name : { "P11SlaveYes", "P11SlaveNo" })
	{
		Port11World w;
		Object *master = w.spawn("P11Master", 100, 100);
		Object *slave = w.spawn(name, 100, 100);
		enslave(*master, *slave);
		const std::uint32_t withDrone = others | (1u << drone);
		for (int bit = 0; bit < WEAPONBONUS_CONDITION_COUNT; ++bit)
		{
			if (withDrone & (1u << bit))
			{
				slave->setWeaponBonusCondition(bit, true);
				master->setWeaponBonusCondition(bit, true);
			}
		}
		w.frames(1); // RW 0x8A5E82 .. 0x8A5E89: and [ebx + 0x39C], ~0x40 on BOTH paths; ebx is the MASTER (0x8A5E0B), before repair / attack / guard
		CHECK(master->weaponBonusConditionMask() == others);
		CHECK(slave->weaponBonusConditionMask() == withDrone);
		CHECK_FALSE(hasBonus(*master, drone));
		CHECK(hasBonus(*slave, drone));
	}
}

TEST_CASE("PORT-11 attack neighbor: the range bonus still marks the master and the attack move still runs")
{
	const int drone = bonusBit("DRONE_SPOTTING");
	// RW 0x8A5F06 -> 0x8A5319; 0x8A54BD .. 0x8A54CF sets the MASTER's bit when the edge-squared distance (0x6CA525, slave radius 8 subtracted)
	// is below 50^2. The update first clears the master's bit at 0x8A5E82, then the attack path may set it again. Each case starts from a fresh world so
	// the AI's own victim selection cannot carry a previous case's target into the next one.
	struct Case { const char *name; float victimX; bool bonus; };
	const Case cases[] = {
		{ "inside", 120.0f, true },
		{ "outside the bonus but inside AttackRange", 200.0f, false },
		// The boundary itself is outside: centre distance 58 = bonus 50 + the synthetic slave's bounding circle 8 (RW 0x6CA536 subtracts Object+0xB8),
		// so the compared edge distance is exactly 50. RW 0x8A54C9 comiss radius^2, distance; 0x8A54CD jbe skips the OR when radius^2 <= distance.
		{ "one unit inside the boundary", 157.0f, true },
		{ "exact boundary", 158.0f, false },
	};
	for (const Case &c : cases)
	{
		Port11World w;
		Object *master = w.spawn("P11Master", 100, 100);
		Object *slave = w.spawn("P11SlaveNo", 100, 100);
		Object *victim = w.spawn("P11Master", c.victimX, 100, 0.0f, w.teamOf("Bob"));
		enslave(*master, *slave);
		master->getAIUpdateInterface()->setCurrentVictim(victim);
		w.ai->adapterFor(*slave).setLayer(LAYER_RAMP);
		const auto attacks = SlavedUpdate::stats().attackMoves;
		slavedOf(*slave)->update();
		CHECK_MESSAGE(hasBonus(*master, drone) == c.bonus, std::string(c.name));
		CHECK_FALSE(hasBonus(*slave, drone));
		CHECK_MESSAGE(SlavedUpdate::stats().attackMoves == attacks + 1, std::string(c.name));
		CHECK(w.ai->adapterFor(*slave).getLayer() == LAYER_RAMP);
	}
}

TEST_CASE("PORT-11 snapshot: the copied layer uses the existing xfer and hash, and the next update follows the loaded master")
{
	Port11World w;
	Object *master = w.spawn("P11Master", 100, 100);
	Object *slave = w.spawn("P11SlaveYes", 100, 100);
	enslave(*master, *slave);
	const ObjectID masterID = master->getID(), slaveID = slave->getID();
	w.ai->adapterFor(*master).setLayer(LAYER_WALL_FIRST);
	w.frames(1);
	REQUIRE(w.ai->adapterFor(*slave).getLayer() == LAYER_WALL_FIRST);
	const auto savedHash = w.logic->computeStateHash();
	std::vector<std::uint8_t> saved;
	std::string error;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(saved, &error), error);
	w.ai->adapterFor(*master).setLayer(LAYER_RAMP);
	w.frames(1);
	REQUIRE(w.ai->adapterFor(*slave).getLayer() == LAYER_RAMP);
	REQUIRE_MESSAGE(w.logic->loadSnapshot(saved, &error), error);
	CHECK(w.logic->computeStateHash() == savedHash);
	master = w.logic->findObjectByID(masterID);
	slave = w.logic->findObjectByID(slaveID);
	REQUIRE(master);
	REQUIRE(slave);
	CHECK(w.ai->adapterFor(*slave).getLayer() == LAYER_WALL_FIRST);
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(again, &error), error);
	CHECK(again == saved);
	w.ai->adapterFor(*master).setLayer(LAYER_TOP);
	w.frames(1);
	CHECK(w.ai->adapterFor(*slave).getLayer() == LAYER_TOP);
}
