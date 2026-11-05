// OpenBFME unit tests, lane STARTRANGE-1 (decompfind-20261104 F-11): SpecialAbilityUpdate::isWithinStartAbilityRange (RW 0x85207A) and
// isWithinAbilityAbortRange (RW 0x8522E3). A CHUNK_VENDOR target answers with its box distance (RW 0x852137 -> 0x8513FD -> 0x68F430), and a
// SPECIAL_GRAB_PASSENGER (0x27) position target takes 400.0 (RW 0xC55874) off the squared distance. Synthetic data (no retail files). GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

namespace
{
const char kPowers[] = R"ini(
SpecialPower RangeTestGrab
 Enum = SPECIAL_GRAB_PASSENGER
 ReloadTime = 0
End
SpecialPower RangeTestKnife
 Enum = SPECIAL_KNIFE_ATTACK
 ReloadTime = 0
End
)ini";

std::string caster(const std::string &name, const std::string &power, const std::string &range)
{
	return "Object " + name + R"ini(
 KindOf = MONSTER SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 Behavior = PhysicsBehavior ModuleTag_Physics
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = )ini" + power + "\n  StartAbilityRange = " + range + "\n  AbilityAbortRange = " + range + R"ini(
  UnpackTime = 300
  PackTime = 300
 End
End
)ini";
}

const char kRocks[] = R"ini(
Object RangeTestChunk
 KindOf = CHUNK_VENDOR IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 30
 GeometryMinorRadius = 10
 GeometryHeight = 10
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
End
Object RangeTestBlock
 KindOf = IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 30
 GeometryMinorRadius = 10
 GeometryHeight = 10
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
End
Object RangeTestOffsetChunk
 KindOf = CHUNK_VENDOR IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 30
 GeometryMinorRadius = 10
 GeometryHeight = 10
 GeometryOffset = X:20 Y:0 Z:0
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
End
)ini";

struct RangeWorld
{
	combattest::CombatWorld w;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	RangeWorld()
	{
		TheSpecialPowerStore = &powers;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		std::string err = w.w.load(kPowers, INI_LOAD_OVERWRITE, "rangepowers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		// 59: between the bounding circle's edge distance (~58.4) and the box distance (60) of the chunk 100 away
		err = w.w.load(caster("RangeTroll", "RangeTestGrab", "59") + caster("RangeKnife", "RangeTestKnife", "8") + caster("RangeGrabber", "RangeTestGrab", "8") +
				std::string(kRocks),
			INI_LOAD_OVERWRITE, "rangecasters.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		w.combat().setAutoAcquireEnabled(false);
	}
	~RangeWorld() { TheSpecialPowerStore = savedPowers; }
};

SpecialAbilityUpdate *abilityOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (SpecialAbilityUpdate *x = dynamic_cast<SpecialAbilityUpdate *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("STARTRANGE-1: RW 0x68F430, the box distance: inside 0, off the ends (|X . d| - major), off the long sides (|Y . d| - minor), less the other's radius")
{
	RangeWorld f;
	Object *chunk = f.w.unit("RangeTestChunk", 'B', 300.0f, 300.0f);
	REQUIRE(chunk);
	// inside the box: 0 (RW 0x68F52C)
	Object *in = f.w.unit("RangeKnife", 'A', 320.0f, 305.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *in) == 0.0f);
	// off an end: 100 along X, extent 30 (major), radius 10: (100 - 40)^2
	Object *end = f.w.unit("RangeKnife", 'A', 400.0f, 300.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *end) == 3600.0f);
	// off a long side: 50 along Y, extent 10 (minor), radius 10: (50 - 20)^2
	Object *side = f.w.unit("RangeKnife", 'A', 310.0f, 250.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *side) == 900.0f);
	// touching (the distance is not above 0): 0
	Object *touch = f.w.unit("RangeKnife", 'A', 340.0f, 300.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *touch) == 0.0f);
	// off a corner (100, 100): the extent is the box projected on the normalised direction, |30 nx| + |10 ny| = 40 / sqrt 2;
	// (sqrt(20000) - (40 / sqrt 2 + 10))^2 = 10637.3...
	Object *corner = f.w.unit("RangeKnife", 'A', 400.0f, 400.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *corner) == doctest::Approx(10637.26).epsilon(1e-5)); // tolerance: x87 PC24 / Inv_Sqrt rounding of the closed form
	// shape 0's offset (X 20) moves the box's centre (RW 0xAD14F0) to 620, and the half size + 0x24 (RW 0xAD2860) is the larger of the local box's
	// hi.x (20 + 30) and -lo.x (10): 50. 100 along X: (100 - (50 + 10))^2
	Object *offset = f.w.unit("RangeTestOffsetChunk", 'B', 600.0f, 300.0f);
	Object *offEnd = f.w.unit("RangeKnife", 'A', 720.0f, 300.0f);
	CHECK(CombatQueries::boxDistanceSquared2D(*offset, *offEnd) == 1600.0f);
	// turned a quarter: the long axis lies along world Y, so 100 along world X is off a long side: (100 - 20)^2
	chunk->setOrientation(1.5707964f);
	CHECK(CombatQueries::boxDistanceSquared2D(*chunk, *end) == doctest::Approx(6400.0f).epsilon(1e-6)); // tolerance: the turned basis' cos(pi/2) is not exactly 0
}

TEST_CASE("STARTRANGE-1: RW 0x85212D, a CHUNK_VENDOR target is in start range by the box distance (RW 0x8513FD), any other by the edge distance (RW 0x66352C)")
{
	RangeWorld f;
	Object *troll = f.w.unit("RangeTroll", 'A', 200.0f, 200.0f);
	Object *chunk = f.w.unit("RangeTestChunk", 'B', 300.0f, 200.0f);
	Object *block = f.w.unit("RangeTestBlock", 'B', 200.0f, 300.0f);
	block->setOrientation(1.5707964f); // its long axis toward the troll, as the chunk's
	f.w.frames(1);
	SpecialAbilityUpdate *ability = abilityOf(troll);
	REQUIRE(ability);
	const SpecialPowerTemplate *power = ability->abilityData()->m_specialPowerTemplate;
	REQUIRE(power);
	// the chunk: box distance (100 - 40)^2 = 3600 > 59^2 = 3481: not in range (the bounding circle's edge distance, ~58.4^2, would be)
	CHECK(CombatQueries::edgeDistanceSquared2D(*troll, *troll->getPosition(), *chunk, *chunk->getPosition()) < 3481.0f); // tolerance: the old reading is in range
	ability->initiateIntentToDoSpecialPower(power, chunk, nullptr, 0, 0);
	CHECK_FALSE(ability->withinStartAbilityRangeForTest());
	// the same box without CHUNK_VENDOR keeps the edge distance: in range
	ability->initiateIntentToDoSpecialPower(power, block, nullptr, 0, 0);
	CHECK(ability->withinStartAbilityRangeForTest());
}

TEST_CASE("STARTRANGE-1: RW 0x85221D / 0x85240B, a SPECIAL_GRAB_PASSENGER (0x27) position target takes 400 off the squared distance; other powers do not")
{
	RangeWorld f;
	Object *grabber = f.w.unit("RangeGrabber", 'A', 200.0f, 200.0f);
	Object *knife = f.w.unit("RangeKnife", 'A', 400.0f, 200.0f);
	f.w.frames(1);
	SpecialAbilityUpdate *grab = abilityOf(grabber);
	SpecialAbilityUpdate *other = abilityOf(knife);
	REQUIRE(grab);
	REQUIRE(other);
	// 30 from the centre, radius 10: (30 - 10)^2 = 400; less 400 = 0 <= 8^2 for the grab, 400 > 64 for the knife
	const Coord3D grabAt{ 230.0f, 200.0f, 0.0f };
	const Coord3D knifeAt{ 430.0f, 200.0f, 0.0f };
	grab->initiateIntentToDoSpecialPower(grab->abilityData()->m_specialPowerTemplate, nullptr, &grabAt, 0, 0);
	other->initiateIntentToDoSpecialPower(other->abilityData()->m_specialPowerTemplate, nullptr, &knifeAt, 0, 0);
	CHECK(grab->withinStartAbilityRangeForTest());
	CHECK(grab->withinAbortRangeForTest());
	CHECK_FALSE(other->withinStartAbilityRangeForTest());
	CHECK_FALSE(other->withinAbortRangeForTest());
	// 32 away: (22)^2 - 400 = 84 > 64: out
	const Coord3D farther{ 232.0f, 200.0f, 0.0f };
	grab->initiateIntentToDoSpecialPower(grab->abilityData()->m_specialPowerTemplate, nullptr, &farther, 0, 0);
	CHECK_FALSE(grab->withinStartAbilityRangeForTest());
	CHECK_FALSE(grab->withinAbortRangeForTest());
}
