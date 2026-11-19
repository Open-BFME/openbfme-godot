// OpenBFME unit tests, PORT-58: SpecialAbilityUpdate::withinStartAbilityRange() (+ 0xC5 ApproachUntilMembersInRange).
// RW 0x85213E .. 0x852171 (tier A to BFME2 1.06 SpecialAbilityUpdateIsWithinStartAbilityRange.cpp, retail 0x44F28E): when the caster is a HORDE
// (template + 0x115 & 0x20) and ApproachUntilMembersInRange (+ 0xC5) is true against a non-CHUNK_VENDOR object target, dist2 is computed in x87 PC24 as
// the squared 2D center distance (RW 0x66137C) plus the caster's own GeometryInfo::m_boundingCircleRadius (Object + 0xB8, un-squared, RW 0x852168 fadd ->
// 0x85216E fstp dword), NOT edge distance and NOT subtracting target radius. Synthetic data (no retail files). GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char kPort58Ini[] = R"ini(
SpecialPower Port58Power
 Enum = SPECIAL_AT_VISIBLE_OBJECT
 ReloadTime = 0
End

Weapon Port58Shot
 AttackRange = 600
 DelayBetweenShots = 200
 DamageNugget
  Damage = 25
  Radius = 0.0
  DelayTime = 0
  DamageType = PIERCE
  DeathType = NORMAL
 End
End

; BOX 20x60 -> m_boundingCircleRadius = hypot(20, 60) ~= 63.245553
Object Port58HordeYes
 KindOf = HORDE SELECTABLE CAN_ATTACK
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 60
 GeometryHeight = 20
 Body = ImmortalBody ModuleTag_Body
  MaxHealth = 1
 End
 Behavior = HordeAIUpdate ModuleTag_AI
 End
 Behavior = HordeContain ModuleTag_Contain
  Slots = 1
  RankInfo = RankNumber:1 UnitType:Archer Position:X:0 Y:0
  InitialPayload = Archer 1
 End
 LocomotorSet
  Locomotor = HordeLoco
  Condition = SET_NORMAL
  Speed = 50
 End
 Behavior = SpecialPowerModule ModuleTag_SP
  SpecialPowerTemplate = Port58Power
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  SpecialWeapon = Port58Shot
  StartAbilityRange = 400
  AbilityAbortRange = 400
  ApproachUntilMembersInRange = Yes
  IgnoreFacingCheck = Yes
  PackTime = 200
 End
End

Object Port58HordeNo
 KindOf = HORDE SELECTABLE CAN_ATTACK
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 60
 GeometryHeight = 20
 Body = ImmortalBody ModuleTag_Body
  MaxHealth = 1
 End
 Behavior = HordeAIUpdate ModuleTag_AI
 End
 Behavior = HordeContain ModuleTag_Contain
  Slots = 1
  RankInfo = RankNumber:1 UnitType:Archer Position:X:0 Y:0
  InitialPayload = Archer 1
 End
 LocomotorSet
  Locomotor = HordeLoco
  Condition = SET_NORMAL
  Speed = 50
 End
 Behavior = SpecialPowerModule ModuleTag_SP
  SpecialPowerTemplate = Port58Power
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  SpecialWeapon = Port58Shot
  StartAbilityRange = 400
  AbilityAbortRange = 400
  ApproachUntilMembersInRange = No
  IgnoreFacingCheck = Yes
  PackTime = 200
 End
End

; Small StartAbilityRange (50 -> rangeSq = 2500) with boundingCircleRadius ~= 63.25 (r^2 = 4000 > 2500): proves +r (not +r^2 and not (d+r)^2).
Object Port58HordeSmallRange
 KindOf = HORDE SELECTABLE CAN_ATTACK
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 60
 GeometryHeight = 20
 Body = ImmortalBody ModuleTag_Body
  MaxHealth = 1
 End
 Behavior = HordeAIUpdate ModuleTag_AI
 End
 Behavior = HordeContain ModuleTag_Contain
  Slots = 1
  RankInfo = RankNumber:1 UnitType:Archer Position:X:0 Y:0
  InitialPayload = Archer 1
 End
 LocomotorSet
  Locomotor = HordeLoco
  Condition = SET_NORMAL
  Speed = 50
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  StartAbilityRange = 50
  AbilityAbortRange = 50
  ApproachUntilMembersInRange = Yes
 End
End

; CYLINDER r=64, StartAbilityRange=10 (rangeSq=100): exact boundary at centerDist=6 (36 + 64 = 100) vs 7 (49 + 64 = 113).
Object Port58HordeBoundary
 KindOf = HORDE SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 64
 GeometryHeight = 20
 Body = ImmortalBody ModuleTag_Body
  MaxHealth = 1
 End
 Behavior = HordeAIUpdate ModuleTag_AI
 End
 Behavior = HordeContain ModuleTag_Contain
  Slots = 1
  RankInfo = RankNumber:1 UnitType:Archer Position:X:0 Y:0
  InitialPayload = Archer 1
 End
 LocomotorSet
  Locomotor = HordeLoco
  Condition = SET_NORMAL
  Speed = 50
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  StartAbilityRange = 10
  AbilityAbortRange = 10
  ApproachUntilMembersInRange = Yes
 End
End

; Non-HORDE caster with ApproachUntilMembersInRange=Yes: RW 0x852141 skips + 0xC5 and keeps edgeDistanceSquared2D.
Object Port58NonHorde
 KindOf = INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 64
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 200
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  StartAbilityRange = 10
  AbilityAbortRange = 10
  ApproachUntilMembersInRange = Yes
 End
End

; Horde with r=10 and StartAbilityRange=61 against a CHUNK_VENDOR box (major 30, minor 10) at dx=100: boxDist=60^2 <= 61^2, centerDist^2+10=10010 > 61^2.
Object Port58HordeVendorTest
 KindOf = HORDE SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 20
 Body = ImmortalBody ModuleTag_Body
  MaxHealth = 1
 End
 Behavior = HordeAIUpdate ModuleTag_AI
 End
 Behavior = HordeContain ModuleTag_Contain
  Slots = 1
  RankInfo = RankNumber:1 UnitType:Archer Position:X:0 Y:0
  InitialPayload = Archer 1
 End
 LocomotorSet
  Locomotor = HordeLoco
  Condition = SET_NORMAL
  Speed = 50
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = Port58Power
  StartAbilityRange = 61
  AbilityAbortRange = 61
  ApproachUntilMembersInRange = Yes
 End
End

Object Port58BigTarget
 KindOf = IMMOBILE
 Geometry = CYLINDER
 GeometryMajorRadius = 50
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End

Object Port58TinyTarget
 KindOf = IMMOBILE
 Geometry = CYLINDER
 GeometryMajorRadius = 1
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End

Object Port58ChunkVendor
 KindOf = CHUNK_VENDOR IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 30
 GeometryMinorRadius = 10
 GeometryHeight = 10
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
End
)ini";

struct Port58World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;

	Port58World() : combattest::CombatWorld("", 150)
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		combat().setAutoAcquireEnabled(false);
		const std::string err = w.load(kPort58Ini, INI_LOAD_OVERWRITE, "port58.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Port58World() { TheSpecialPowerStore = savedPowers; }
};

SpecialAbilityUpdate *abilityOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (auto *u = dynamic_cast<SpecialAbilityUpdate *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("PORT-58: HORDE with ApproachUntilMembersInRange=Yes adds boundingCircleRadius (un-squared) to centerDistSquared2D")
{
	Port58World f;
	Object *hordeYes = f.unit("Port58HordeYes", 'A', 200.0f, 300.0f);
	Object *hordeNo = f.unit("Port58HordeNo", 'A', 200.0f, 500.0f);
	Object *bigTarget = f.unit("Port58BigTarget", 'B', 650.0f, 300.0f);
	Object *bigForNo = f.unit("Port58BigTarget", 'B', 650.0f, 500.0f);
	Object *tinyTarget = f.unit("Port58TinyTarget", 'B', 600.0f, 300.0f);
	f.frames(2);

	const float r = CombatQueries::boundingCircleRadius(*hordeYes);
	// tolerance: float sqrt(20^2 + 60^2)
	CHECK(r == doctest::Approx(std::sqrt(20.0f * 20.0f + 60.0f * 60.0f)).epsilon(1e-5));

	SpecialAbilityUpdate *abYes = abilityOf(hordeYes);
	SpecialAbilityUpdate *abNo = abilityOf(hordeNo);
	REQUIRE(abYes);
	REQUIRE(abNo);
	const SpecialPowerTemplate *power = abYes->abilityData()->m_specialPowerTemplate;
	REQUIRE(power);

	// At dx = 450: edge distance = 450 - 63.2455 - 50 = 336.75 < 400 -> No is in range, Yes is out (450^2 + 63.25 = 202563.25 > 160000).
	abNo->initiateIntentToDoSpecialPower(power, bigForNo, nullptr, 0, 0);
	abYes->initiateIntentToDoSpecialPower(power, bigTarget, nullptr, 0, 0);
	CHECK(abNo->withinStartAbilityRangeForTest());
	CHECK_FALSE(abYes->withinStartAbilityRangeForTest());

	// At dx = 400: centerDist^2 == StartAbilityRange^2 (160000), + boundingCircleRadius (63.25) pushes it out of range regardless of target radius.
	const Coord3D pos400{ 600.0f, 300.0f, 0.0f };
	bigTarget->setPosition(&pos400);
	tinyTarget->setPosition(&pos400);
	abYes->initiateIntentToDoSpecialPower(power, bigTarget, nullptr, 0, 0);
	CHECK_FALSE(abYes->withinStartAbilityRangeForTest());
	abYes->initiateIntentToDoSpecialPower(power, tinyTarget, nullptr, 0, 0);
	CHECK_FALSE(abYes->withinStartAbilityRangeForTest());

	// At dx = 399: 399^2 + 63.25 = 159264.25 <= 160000 -> both big and tiny targets are in range.
	const Coord3D pos399{ 599.0f, 300.0f, 0.0f };
	bigTarget->setPosition(&pos399);
	tinyTarget->setPosition(&pos399);
	abYes->initiateIntentToDoSpecialPower(power, bigTarget, nullptr, 0, 0);
	CHECK(abYes->withinStartAbilityRangeForTest());
	abYes->initiateIntentToDoSpecialPower(power, tinyTarget, nullptr, 0, 0);
	CHECK(abYes->withinStartAbilityRangeForTest());

	// Proof that r is added un-squared (+r, NOT +r^2 and NOT (d+r)^2): StartAbilityRange=50 (rangeSq=2500), r=63.25 (r^2=4000 > 2500).
	// At dx=40: 40^2 + 63.25 = 1663.25 <= 2500 -> in range.
	Object *smallRange = f.unit("Port58HordeSmallRange", 'A', 200.0f, 800.0f);
	Object *smallTarget = f.unit("Port58TinyTarget", 'B', 240.0f, 800.0f);
	f.frames(2);
	SpecialAbilityUpdate *abSmall = abilityOf(smallRange);
	REQUIRE(abSmall);
	abSmall->initiateIntentToDoSpecialPower(power, smallTarget, nullptr, 0, 0);
	CHECK(abSmall->withinStartAbilityRangeForTest());

	// Exact boundary at r=64, StartAbilityRange=10 (rangeSq=100): dx=6 -> 36 + 64 = 100 <= 100 (in), dx=7 -> 49 + 64 = 113 > 100 (out, even though edgeDist == 0).
	Object *boundary = f.unit("Port58HordeBoundary", 'A', 200.0f, 1000.0f);
	Object *boundTarget = f.unit("Port58TinyTarget", 'B', 206.0f, 1000.0f);
	f.frames(2);
	SpecialAbilityUpdate *abBound = abilityOf(boundary);
	REQUIRE(abBound);
	abBound->initiateIntentToDoSpecialPower(power, boundTarget, nullptr, 0, 0);
	CHECK(abBound->withinStartAbilityRangeForTest());
	const Coord3D pos7{ 207.0f, 1000.0f, 0.0f };
	boundTarget->setPosition(&pos7);
	abBound->initiateIntentToDoSpecialPower(power, boundTarget, nullptr, 0, 0);
	CHECK_FALSE(abBound->withinStartAbilityRangeForTest());
}

TEST_CASE("PORT-58 gates: non-HORDE caster, CHUNK_VENDOR target, position target, and withinAbortRange keep retail paths")
{
	Port58World f;
	Object *nonHorde = f.unit("Port58NonHorde", 'A', 200.0f, 300.0f);
	Object *bigTarget = f.unit("Port58BigTarget", 'B', 320.0f, 300.0f);
	Object *hordeVendor = f.unit("Port58HordeVendorTest", 'A', 200.0f, 600.0f);
	Object *chunkVendor = f.unit("Port58ChunkVendor", 'B', 300.0f, 600.0f);
	Object *hordePos = f.unit("Port58HordeBoundary", 'A', 200.0f, 900.0f);
	Object *hordeAbort = f.unit("Port58HordeYes", 'A', 200.0f, 1200.0f);
	Object *abortTarget = f.unit("Port58BigTarget", 'B', 650.0f, 1200.0f);
	f.frames(2);

	SpecialAbilityUpdate *abNonHorde = abilityOf(nonHorde);
	SpecialAbilityUpdate *abVendor = abilityOf(hordeVendor);
	SpecialAbilityUpdate *abPos = abilityOf(hordePos);
	SpecialAbilityUpdate *abAbort = abilityOf(hordeAbort);
	REQUIRE(abNonHorde);
	REQUIRE(abVendor);
	REQUIRE(abPos);
	REQUIRE(abAbort);
	const SpecialPowerTemplate *power = abNonHorde->abilityData()->m_specialPowerTemplate;
	REQUIRE(power);

	// 1) Non-HORDE with ApproachUntilMembersInRange=Yes (r=64, StartAbilityRange=10) targeting r=50 at dx=120: edgeDist = 120 - 64 - 50 = 6 <= 10 -> in range.
	abNonHorde->initiateIntentToDoSpecialPower(power, bigTarget, nullptr, 0, 0);
	CHECK(abNonHorde->withinStartAbilityRangeForTest());

	// 2) HORDE + ApproachUntilMembersInRange=Yes targeting CHUNK_VENDOR (RW 0x85212D precedes RW 0x85213E): box distance (100 - 40)^2 = 3600 <= 61^2 = 3721 -> in range.
	abVendor->initiateIntentToDoSpecialPower(power, chunkVendor, nullptr, 0, 0);
	CHECK(abVendor->withinStartAbilityRangeForTest());

	// 3) HORDE + ApproachUntilMembersInRange=Yes with ground position target: CircleDistanceSquaredToPoint (70 - 64)^2 = 36 <= 10^2 -> in range.
	const Coord3D groundAt{ 270.0f, 900.0f, 0.0f };
	abPos->initiateIntentToDoSpecialPower(power, nullptr, &groundAt, 0, 0);
	CHECK(abPos->withinStartAbilityRangeForTest());

	// 4) withinAbortRange (RW 0x8522E3) unconditionally uses edgeDistanceSquared2D (RW 0x852347), never + 0xC5: at dx=450, start range is false, abort range is true.
	abAbort->initiateIntentToDoSpecialPower(power, abortTarget, nullptr, 0, 0);
	CHECK_FALSE(abAbort->withinStartAbilityRangeForTest());
	CHECK(abAbort->withinAbortRangeForTest());
}

TEST_CASE("PORT-58 approach and fire: WeaponFireSpecialAbilityUpdate on a HORDE approaches before firing when ApproachUntilMembersInRange=Yes")
{
	// Run ApproachUntilMembersInRange=No in its own world: at dx=450 (edge=336.75 < 400), it fires immediately without moving past x=200.
	{
		Port58World f;
		Object *horde = f.unit("Port58HordeNo", 'A', 200.0f, 400.0f);
		Object *target = f.unit("Port58BigTarget", 'B', 650.0f, 400.0f);
		f.frames(2);
		auto *wf = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(abilityOf(horde));
		REQUIRE(wf);
		const SpecialPowerTemplate *power = wf->abilityData()->m_specialPowerTemplate;
		REQUIRE(power);
		CHECK(horde->getPosition()->x == 200.0f);
		wf->initiateIntentToDoSpecialPower(power, target, nullptr, 0, 0);
		REQUIRE(wf->withinStartAbilityRangeForTest());
		const int firedAtFrame = f.runUntil([&]() { return wf->shotsFired() == 1; }, 20);
		// tolerance: bounded search until trigger frame
		CHECK(firedAtFrame < 20);
		CHECK(wf->shotsFired() == 1);
		CHECK(horde->getPosition()->x == 200.0f);
	}

	// Run ApproachUntilMembersInRange=Yes: at dx=450, it must walk closer until centerDist^2 + r <= 400^2 (dx <= ~399.92, i.e. x >= ~250.08) before firing.
	{
		Port58World f;
		Object *horde = f.unit("Port58HordeYes", 'A', 200.0f, 400.0f);
		Object *target = f.unit("Port58BigTarget", 'B', 650.0f, 400.0f);
		f.frames(2);
		auto *wf = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(abilityOf(horde));
		REQUIRE(wf);
		const SpecialPowerTemplate *power = wf->abilityData()->m_specialPowerTemplate;
		REQUIRE(power);
		wf->initiateIntentToDoSpecialPower(power, target, nullptr, 0, 0);
		CHECK_FALSE(wf->withinStartAbilityRangeForTest());
		const int firedAtFrame = f.runUntil([&]() { return wf->shotsFired() == 1; }, 40);
		// tolerance: bounded search while horde approaches and fires
		CHECK(firedAtFrame < 40);
		CHECK(wf->shotsFired() == 1);
		// Fired only after closing past the centerDist^2 + r <= 400^2 boundary (~250.079 along +X).
		// tolerance: approach stops at first logic frame inside sqrt(400^2 - r) (~250.08 <= x)
		CHECK(horde->getPosition()->x > 250.0f);
		CHECK(target->getBodyModule()->getHealth() == 975.0f);
	}
}

TEST_CASE("PORT-58 PC24 parity and S-860 narrowing: x87 wide sum + radius and STOPS.md record")
{
	Port58World f;
	Object *horde = f.unit("Port58HordeYes", 'A', 100.125f, 200.375f);
	Object *targetIn = f.unit("Port58TinyTarget", 'B', 499.5f, 220.625f);
	Object *targetOut = f.unit("Port58TinyTarget", 'B', 499.75f, 220.625f);
	f.frames(2);
	SpecialAbilityUpdate *ab = abilityOf(horde);
	REQUIRE(ab);
	const SpecialPowerTemplate *power = ab->abilityData()->m_specialPowerTemplate;
	REQUIRE(power);

	const Coord3D &op = *horde->getPosition();
	{
		const Coord3D &tp = *targetIn->getPosition();
		const double dx = SimMath::pc24SubW((double)tp.x, (double)op.x);
		const double dy = SimMath::pc24SubW((double)tp.y, (double)op.y);
		const float expectedDist2 = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)), (double)CombatQueries::boundingCircleRadius(*horde)));
		// tolerance: confirms synthetic coordinates lie just inside StartAbilityRange^2 (159973.7 <= 160000)
		CHECK(expectedDist2 <= SimMath::mulf32(400.0f, 400.0f));
		ab->initiateIntentToDoSpecialPower(power, targetIn, nullptr, 0, 0);
		CHECK(ab->withinStartAbilityRangeForTest());
	}
	{
		const Coord3D &tp = *targetOut->getPosition();
		const double dx = SimMath::pc24SubW((double)tp.x, (double)op.x);
		const double dy = SimMath::pc24SubW((double)tp.y, (double)op.y);
		const float expectedDist2 = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)), (double)CombatQueries::boundingCircleRadius(*horde)));
		// tolerance: confirms 0.25-unit step crosses StartAbilityRange^2 (160173.45 > 160000)
		CHECK(expectedDist2 > SimMath::mulf32(400.0f, 400.0f));
		ab->initiateIntentToDoSpecialPower(power, targetOut, nullptr, 0, 0);
		CHECK_FALSE(ab->withinStartAbilityRangeForTest());
	}
	CHECK(f.logic->report().errors.empty());
	CHECK(f.logic->report().unportedModules.empty());

	// S-860 stays present and explicitly records Port 58's ApproachUntilMembersInRange narrowing.
	bool foundStop = false;
	for (const std::string &l : SpecialAbilityModules::stopLines())
	{
		if (l.rfind("[S-860]", 0) == 0)
		{
			foundStop = true;
			CHECK(l.find("ApproachUntilMembersInRange") != std::string::npos);
			CHECK(l.find("PORT-58") != std::string::npos);
		}
	}
	CHECK(foundStop);

	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string docs = ss.str();
	const size_t pos = docs.find("| S-860 |");
	REQUIRE(pos != std::string::npos);
	const size_t end = docs.find('\n', pos);
	const std::string row = docs.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
	CHECK(row.find("ApproachUntilMembersInRange") != std::string::npos);
	CHECK(row.find("0x85213E") != std::string::npos);
}
