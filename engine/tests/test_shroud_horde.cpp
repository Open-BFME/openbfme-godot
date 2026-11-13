// OpenBFME unit tests. GPL-3.0.
// The horde's VisionSideOverride / VisionRearOverride on the object's look (RW 0x694A45): the object's contain, asked for its horde interface (RW 0x694B6A ->
// RW 0x68C866), answers interface vslots 0x25C / 0x258 (RW 0x872BAB / 0x872B9E, HordeContainModuleData + 0x278 / + 0x274) and those replace the template's
// VisionSide / VisionRear when they are above 0 (RW 0x694B82 `fldz; fcompi; jbe`, RW 0x694BB2 `ja`, the products RW 0x694BBD / 0x694BCA `mulss`).
// Synthetic INI only; the world is a horde built as the game builds one (HordeContain with an InitialPayload).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/System/ShroudManager.h"

#include <string>

using namespace combattest;

namespace
{
// a horde whose template vision is 50% / 25%, with the two overrides the INI rows set; 200 is the object's own ShroudClearingRange (its VisionRange)
std::string hordeObjects(const char *sideOverride, const char *rearOverride)
{
	std::string s =
		"Object VisionFootman\n"
		"  KindOf = INFANTRY SELECTABLE\n"
		"  VisionRange = 200\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n"
		"Object VisionHorde\n"
		"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
		"  VisionRange = 200\n"
		"  VisionSide = 50%\n"
		"  VisionRear = 25%\n"
		"  Geometry = BOX\n"
		"  GeometryMajorRadius = 30\n"
		"  GeometryMinorRadius = 45\n"
		"  GeometryHeight = 20\n"
		"  Body = ImmortalBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"  Behavior = HordeAIUpdate ModuleTag_AI\n"
		"  End\n"
		"  Behavior = HordeContain ModuleTag_Contain\n"
		"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
		"    Slots = 3\n"
		"    InitialPayload = VisionFootman 3\n"
		"    VisionSideOverride = ";
	s += sideOverride;
	s +=
		"\n"
		"    VisionRearOverride = ";
	s += rearOverride;
	s +=
		"\n"
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = HordeLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 50\n"
		"  End\n"
		"End\n";
	return s;
}

// the world of the shroud retail reads: the manager attached before any object exists, so the world entry hook sees the horde (RW 0x68E31F)
struct VisionWorld : CombatWorld
{
	ShroudManager shroud;
	explicit VisionWorld(const char *extraObjects)
		: CombatWorld(extraObjects)
		, shroud(*logic)
	{
		shroud.init(40.0f, 1);
		shroud.setExtent(0.0f, 0.0f, 1000.0f, 800.0f);
		shroud.attach();
	}
};

// the template values the look is given (ThingTemplate + 0x4C0 / + 0x4C4 / + 0x4C8 of this horde: 200, 50%, 25%)
ShroudManager::TemplateVision hordeVision()
{
	ShroudManager::TemplateVision tv;
	tv.shroudClearingRange = 200.0f;
	tv.visionSide = 0.5f;
	tv.visionRear = 0.25f;
	return tv;
}
} // namespace

TEST_CASE("shroud objects: a horde's VisionSideOverride / VisionRearOverride replace the template's factors when above 0 (RW 0x694B79 / 0x694B9B)")
{
	const ShroudManager::ObjectSource source = ShroudManager::retailObjectSource();
	const ShroudManager::TemplateVision tv = hordeVision();
	{
		// 100% / 80%: both overrides win over the template's 50% / 25%
		VisionWorld w(hordeObjects("100%", "80%").c_str());
		Object *h = w.unit("VisionHorde", 'A', 300, 300);
		HordeContain *contain = dynamic_cast<HordeContain *>(h->getContain());
		REQUIRE(contain != nullptr);
		REQUIRE(contain->getHordeContainInterface() != nullptr);
		// the parse reaches the interface straight through (RW 0x872BAB / 0x872B9E read module data + 0x278 / + 0x274)
		CHECK(contain->getVisionSideOverride() == 1.0f);
		// tolerance: 80% parses as 0.79999998 (the percent multiply at RW 0x42EB18 is an x87 extended-precision operation)
		CHECK(contain->getVisionRearOverride() == doctest::Approx(0.8f).epsilon(0.0001));
		ShroudManager::LookInputs in;
		source.look(*h, tv, in);
		CHECK(in.forward == 200.0f);
		CHECK(in.side == 200.0f); // 200 * 100%, not 200 * 50%
		// tolerance: 200 * 0.79999998 is 159.99999, one round binary32 product of the rounding 80%
		CHECK(in.rear == doctest::Approx(160.0f).epsilon(0.0001)); // 200 * 80%, not 200 * 25%
	}
	{
		// an explicit 0% is not "above 0" (RW 0x694B82 `jbe`, RW 0x694BB2 `ja`): the template's factors stay
		VisionWorld w(hordeObjects("0%", "0%").c_str());
		Object *h = w.unit("VisionHorde", 'A', 300, 300);
		HordeContain *contain = dynamic_cast<HordeContain *>(h->getContain());
		REQUIRE(contain != nullptr);
		CHECK(contain->getVisionSideOverride() == 0.0f);
		CHECK(contain->getVisionRearOverride() == 0.0f);
		ShroudManager::LookInputs in;
		source.look(*h, tv, in);
		CHECK(in.side == 100.0f); // 200 * 50%
		CHECK(in.rear == 50.0f);  // 200 * 25%
	}
	{
		// the side and the rear are decided one by one: a 0% side keeps the template's side while the 80% rear replaces the template's rear
		VisionWorld w(hordeObjects("0%", "80%").c_str());
		Object *h = w.unit("VisionHorde", 'A', 300, 300);
		ShroudManager::LookInputs in;
		source.look(*h, tv, in);
		CHECK(in.side == 100.0f);
		// tolerance: 200 * 0.79999998 is 159.99999 (the 80% row rounds in x87, RW 0x42EB18)
		CHECK(in.rear == doctest::Approx(160.0f).epsilon(0.0001));
	}
	{
		// an object without a horde contain keeps the template's factors: the object is asked for its contain (RW 0x694B6A) and a plain unit has none
		VisionWorld w(hordeObjects("100%", "80%").c_str());
		Object *foot = w.unit("VisionFootman", 'A', 300, 300);
		ShroudManager::LookInputs in;
		source.look(*foot, tv, in);
		CHECK(in.side == 100.0f);
		CHECK(in.rear == 50.0f);
	}
}
