// OpenBFME unit tests for PORT-244 (fork issue 568): SpecialAbilityUpdate PreTriggerUnstealthTime in handlePackingProcessing
// (WeaponFireSpecialAbilityUpdate and ModelConditionSpecialAbilityUpdate share SpecialAbilityUpdateModuleData + 0x8C).
//
// TARGET FACTS (RotWK 2.01 game.dat, MD5 daed3668006cd90f01c34e5a7da1901f, caveat S-001), read linearly with tools/mod/rw.py:
//   * SpecialAbilityUpdateModuleData table RW 0xC55F40 row RW 0xC55FC0 parses PreTriggerUnstealthTime (RW 0x73A429,
//     INI::parseDurationUnsignedInt) into data + 0x8C (default 0); LoseStealthOnTrigger is data + 0xB0.
//   * SpecialAbilityUpdate::handlePackingProcessing (RW 0x851E9F; DONOR BFME2 0x0044EFEC, SpecialAbilityUpdate.cpp:1410)
//     decrements m_packFramesLeft (+ 0x28) at RW 0x851EB8. When the decremented count is non-zero (RW 0x851EBC -> RW 0x852030),
//     it tests data + 0xB0 (RW 0x852030: cmp byte ptr [edi + 0xb0], 0; je 0x852073) and unsigned m_packFramesLeft < data + 0x8C
//     (RW 0x852039: cmp eax, dword ptr [edi + 0x8c]; jae 0x852073). When both hold, it resolves the object's StealthUpdate
//     (RW 0x852044 -> RW 0x68FBD3), calls StealthUpdate::markAsDetected(0, 1, null, true) (RW 0x852057 -> RW 0x7767A9) and
//     InvisibilityManager::markDetected(object, null, 0, 1) (RW 0x85206E -> RW 0x81C32C), then returns true (RW 0x852073).
//   * Neither WeaponFireSpecialAbilityUpdate (ctor RW 0x8963DE, update RW 0x896370 -> base RW 0x854DF7) nor
//     ModelConditionSpecialAbilityUpdate (dtor RW 0x894402) overrides handlePackingProcessing (vtable slot 0x50).
//
// Synthetic INI only: no RotWK or retail archive is mounted. At 5 logic frames/s (200 ms/frame):
//   - 1000 ms = 5 frames, 300 ms = 2 frames, 2000 ms = 10 frames, 2100 ms = 11 frames.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <string>

namespace
{

const char kPort244Ini[] =
	"Weapon P244PanWeapon\n"
	"  AttackRange = 25.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  DamageNugget\n"
	"    Damage = 40.0\n"
	"    Radius = 0.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"SpecialPower SpecialPowerP244WeaponFire\n"
	"  Enum = SPECIAL_INFANTRY_CAPTURE_BUILDING\n"
	"  ReloadTime = 0\n"
	"End\n"
	"SpecialPower SpecialPowerP244ModelCond\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
	"  ReloadTime = 0\n"
	"End\n"
	// WeaponFireSpecialAbilityUpdate with UnpackTime = 1000 ms (5 frames), PreTriggerUnstealthTime = 300 ms (2 frames),
	// LoseStealthOnTrigger = Yes (mirrors synthetic timing of a melee special-weapon strike).
	"Object P244StealthStriker\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 400\n"
	"    InnateStealth = Yes\n"
	"    StartsActive = Yes\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerP244WeaponFire\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_WF\n"
	"    SpecialPowerTemplate = SpecialPowerP244WeaponFire\n"
	"    StartAbilityRange = 20.0\n"
	"    UnpackTime = 1000\n"
	"    PreparationTime = 200\n"
	"    PackTime = 1000\n"
	"    SpecialWeapon = P244PanWeapon\n"
	"    LoseStealthOnTrigger = Yes\n"
	"    PreTriggerUnstealthTime = 300\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	// ModelConditionSpecialAbilityUpdate with UnpackTime = 2000 ms (10 frames), PreTriggerUnstealthTime = 2100 ms (11 frames > UnpackTime),
	// LoseStealthOnTrigger = Yes, carrying both StealthUpdate and InvisibilityUpdate (STEALTH nugget).
	"Object P244CloakedCaster\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 400\n"
	"    InnateStealth = Yes\n"
	"    StartsActive = Yes\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P244PanWeapon\n"
	"  End\n"
	"  Behavior = InvisibilityUpdate ModuleTag_Invis\n"
	"    InvisibilityNugget\n"
	"      InvisibilityType = STEALTH\n"
	"      ForbiddenWeaponConditions = SPECIAL_ONE\n"
	"    End\n"
	"    StartsActive = Yes\n"
	"    UpdatePeriod = 200\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ModelConditionSpecialAbilityUpdate ModuleTag_MC\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UnpackTime = 2000\n"
	"    PreparationTime = 200\n"
	"    PersistentPrepTime = 600\n"
	"    PackTime = 1000\n"
	"    WhichSpecialPower = 1\n"
	"    LoseStealthOnTrigger = Yes\n"
	"    PreTriggerUnstealthTime = 2100\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	// Neighbor 1: LoseStealthOnTrigger = No with PreTriggerUnstealthTime = 2100 ms (RW 0x852030 skips unstealth).
	"Object P244KeepStealthCaster\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 400\n"
	"    InnateStealth = Yes\n"
	"    StartsActive = Yes\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ModelConditionSpecialAbilityUpdate ModuleTag_MC\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UnpackTime = 1000\n"
	"    PreparationTime = 200\n"
	"    PackTime = 600\n"
	"    WhichSpecialPower = 1\n"
	"    LoseStealthOnTrigger = No\n"
	"    PreTriggerUnstealthTime = 2100\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	// Neighbor 2: LoseStealthOnTrigger = Yes with default PreTriggerUnstealthTime = 0 (unstealths at trigger RW 0x85488A, never during unpack).
	"Object P244TriggerOnlyCaster\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 400\n"
	"    InnateStealth = Yes\n"
	"    StartsActive = Yes\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ModelConditionSpecialAbilityUpdate ModuleTag_MC\n"
	"    SpecialPowerTemplate = SpecialPowerP244ModelCond\n"
	"    UnpackTime = 1000\n"
	"    PreparationTime = 200\n"
	"    PackTime = 600\n"
	"    WhichSpecialPower = 1\n"
	"    LoseStealthOnTrigger = Yes\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n"
	"Object P244TargetDummy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"End\n";

struct Port244World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = nullptr;

	Port244World()
	{
		savedPowers = TheSpecialPowerStore;
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		InvisibilityModules::registerAll(w.modules);
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		logic->settings().invisibilityRulesLoaded = true;
		logic->settings().reinvisibilityDelay = 20; // 4000 ms at 5 logic frames/s
		const std::string err = w.load(kPort244Ini, INI_LOAD_OVERWRITE, "port244.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}

	~Port244World()
	{
		TheSpecialPowerStore = savedPowers;
	}

	Object *make(const std::string &tmpl, char side, float x, float y)
	{
		Object *o = unit(tmpl, side, x, y);
		REQUIRE(o != nullptr);
		o->friend_onBuildComplete();
		return o;
	}

	static bool isStealthedStatus(const Object &o)
	{
		return o.testStatus((unsigned)CombatNames::status("STEALTHED"));
	}

	static bool isDetectedStatus(const Object &o)
	{
		return o.testStatus((unsigned)CombatNames::status("DETECTED"));
	}

	static SpecialAbilityUpdate *ability(Object *o, const char *cls)
	{
		return dynamic_cast<SpecialAbilityUpdate *>(o->findModule(cls));
	}
};

} // namespace

TEST_CASE("PORT-244 WeaponFireSpecialAbilityUpdate: PreTriggerUnstealthTime reveals a stealthed striker once remaining unpack frames drop strictly below the threshold (RW 0x851E9F / RW 0x852030..0x85206E)")
{
	Port244World f;
	Object *striker = f.make("P244StealthStriker", 'A', 300.0f, 300.0f);
	Object *dummy = f.make("P244TargetDummy", 'B', 312.0f, 300.0f);

	// StealthDelay = 400 ms (2 frames): let the striker enter innate stealth before initiating the ability.
	f.frames(5);
	REQUIRE(Port244World::isStealthedStatus(*striker));
	REQUIRE_FALSE(Port244World::isDetectedStatus(*striker));
	REQUIRE(f.health(dummy) == 200.0f);

	SpecialAbilityUpdate *wf = Port244World::ability(striker, "WeaponFireSpecialAbilityUpdate");
	REQUIRE(wf != nullptr);
	const SpecialPowerTemplate *spt = wf->abilityData()->m_specialPowerTemplate;
	REQUIRE(spt != nullptr);
	REQUIRE(wf->abilityData()->m_unpackFrames == 5u);
	REQUIRE(wf->abilityData()->m_preTriggerUnstealthFrames == 2u);

	wf->initiateIntentToDoSpecialPower(spt, dummy, nullptr, 0, 0);
	REQUIRE(wf->isActive());

	// Frame 1: in StartAbilityRange (distance 12 <= 20), startUnpacking sets m_packFramesLeft = 5; handlePackingProcessing ran before startUnpacking.
	f.frames(1);
	CHECK_FALSE(Port244World::isDetectedStatus(*striker));

	// Frames 2..4: handlePackingProcessing decrements m_packFramesLeft 5 -> 4, 4 -> 3, 3 -> 2.
	// At 2, RW 0x852039 `cmp eax, [edi + 0x8c]; jae 0x852073` (2 >= 2) still keeps stealth.
	for (int step = 0; step < 3; ++step)
	{
		f.frames(1);
		CHECK_FALSE(Port244World::isDetectedStatus(*striker));
		CHECK_FALSE(f.logic->invisibility().isDetected(*striker));
		CHECK(f.health(dummy) == 200.0f);
	}

	// Frame 5: m_packFramesLeft decrements 2 -> 1 (< 2): RW 0x852041..0x852057 marks StealthUpdate detected and clears STEALTHED
	// BEFORE unpacking finishes and BEFORE SpecialWeapon fires.
	f.frames(1);
	CHECK(Port244World::isDetectedStatus(*striker));
	CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*striker, dummy->getControllingPlayer()));
	CHECK(f.health(dummy) == 200.0f); // weapon has not fired yet (1 unpack frame + PreparationTime still ahead)

	// Advance past unpack end + preparation: SpecialWeapon fires and damages the target.
	f.frames(4);
	CHECK(f.health(dummy) == 160.0f);
}

TEST_CASE("PORT-244 ModelConditionSpecialAbilityUpdate: PreTriggerUnstealthTime above UnpackTime reveals both StealthUpdate and InvisibilityManager on the first unpacking step (RW 0x851E9F / RW 0x852030..0x85206E)")
{
	Port244World f;
	Object *caster = f.make("P244CloakedCaster", 'A', 300.0f, 300.0f);

	// Advance to frame 6 (InvisibilityManager::update just ran at frame 5 -> m_nextUpdate = 10):
	// both StealthUpdate and InvisibilityManager have the caster in stealth.
	f.frames(6);
	REQUIRE(Port244World::isStealthedStatus(*caster));
	REQUIRE_FALSE(Port244World::isDetectedStatus(*caster));
	REQUIRE(f.logic->invisibility().invisibilityType(*caster) == InvisibilityNugget::STEALTH);
	REQUIRE_FALSE(f.logic->invisibility().isDetected(*caster));

	// Flip SPECIAL_ONE weapon-set flag between InvisibilityManager's 5-frame ticks (StealthUpdate has no RevealWeaponSets, while
	// InvisibilityManager stays stale STEALTH during frames 7..9 unless RW 0x85206E's markDetected(caster, null, 0, 1) evaluates now).
	REQUIRE(caster->getWeapons() != nullptr);
	caster->getWeapons()->setWeaponSetFlag(CombatNames::weaponSetBit("SPECIAL_ONE"), true);

	SpecialAbilityUpdate *mc = Port244World::ability(caster, "ModelConditionSpecialAbilityUpdate");
	REQUIRE(mc != nullptr);
	const SpecialPowerTemplate *spt = mc->abilityData()->m_specialPowerTemplate;
	REQUIRE(spt != nullptr);
	REQUIRE(mc->abilityData()->m_unpackFrames == 10u);
	REQUIRE(mc->abilityData()->m_preTriggerUnstealthFrames == 11u);

	mc->initiateIntentToDoSpecialPower(spt, nullptr, nullptr, 0, 0);
	REQUIRE(mc->isActive());

	// Frame 7: startUnpacking sets m_packFramesLeft = 10; InvisibilityManager has not ticked yet (7 < 10).
	f.frames(1);
	CHECK_FALSE(Port244World::isDetectedStatus(*caster));
	CHECK(f.logic->invisibility().invisibilityType(*caster) == InvisibilityNugget::STEALTH);
	CHECK_FALSE(f.logic->invisibility().isDetected(*caster));

	// Frame 8 (first unpacking step, still < frame 10): m_packFramesLeft decrements 10 -> 9 (< 11); RW 0x852041..0x85206E immediately
	// reveals both StealthUpdate (RW 0x7767A9) and InvisibilityManager (RW 0x81C32C), 9 frames before unpacking ends.
	f.frames(1);
	CHECK(Port244World::isDetectedStatus(*caster));
	CHECK(f.logic->invisibility().invisibilityType(*caster) == InvisibilityNugget::NONE);
	CHECK(f.logic->invisibility().isDetected(*caster));
}

TEST_CASE("PORT-244 SpecialAbilityUpdate neighbor gates: LoseStealthOnTrigger=No never reveals, and PreTriggerUnstealthTime=0 reveals only at trigger (RW 0x852030, RW 0x852039, RW 0x85488A)")
{
	Port244World f;
	Object *keepStealth = f.make("P244KeepStealthCaster", 'A', 200.0f, 200.0f);
	Object *triggerOnly = f.make("P244TriggerOnlyCaster", 'A', 500.0f, 500.0f);

	f.frames(5);
	REQUIRE(Port244World::isStealthedStatus(*keepStealth));
	REQUIRE(Port244World::isStealthedStatus(*triggerOnly));

	SpecialAbilityUpdate *mcKeep = Port244World::ability(keepStealth, "ModelConditionSpecialAbilityUpdate");
	SpecialAbilityUpdate *mcTrig = Port244World::ability(triggerOnly, "ModelConditionSpecialAbilityUpdate");
	REQUIRE(mcKeep != nullptr);
	REQUIRE(mcTrig != nullptr);

	mcKeep->initiateIntentToDoSpecialPower(mcKeep->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	mcTrig->initiateIntentToDoSpecialPower(mcTrig->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);

	// Frame 1 starts unpacking (m_packFramesLeft = 5); frames 2..5 decrement 5 -> 4 -> 3 -> 2 -> 1.
	// Neither unit may unstealth during unpacking: keepStealth has LoseStealthOnTrigger = No (RW 0x852030),
	// triggerOnly has PreTriggerUnstealthTime = 0 (RW 0x852039: unsigned eax < 0 is false).
	for (int step = 0; step < 5; ++step)
	{
		f.frames(1);
		CHECK_FALSE(Port244World::isDetectedStatus(*keepStealth));
		CHECK_FALSE(Port244World::isDetectedStatus(*triggerOnly));
	}

	// Step 2 frames (unpack completion + 1-frame PreparationTime) to land on triggerAbilityEffect (RW 0x85488A):
	// triggerOnly is now detected; keepStealth remains undetected, and stays undetected through packing.
	f.frames(2);
	CHECK_FALSE(Port244World::isDetectedStatus(*keepStealth));
	CHECK(Port244World::isDetectedStatus(*triggerOnly));
	f.frames(4);
	CHECK_FALSE(Port244World::isDetectedStatus(*keepStealth));
}
