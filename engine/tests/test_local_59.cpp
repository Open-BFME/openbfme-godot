// OpenBFME unit tests. GPL-3.0.
//
// Narrow tests for lane LOCAL-59 (fork issue 418):
// WeaponFireSpecialAbilityUpdate creates and loads its special weapon in the constructor as RotWK 2.01 (RW 0x895D98: RW 0x895DE1 .. 0x895E22,
// DONOR BFME2 0x00492708) rather than lazily at first call; xfer uses version 2 (refusing version 1) and returns on XFER_CRC after SpecialAbilityUpdate::xfer
// (RW 0x895C9F .. 0x895CA4). Both BFME2 1.06 and RotWK 2.01 behave identically here.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/RandomValue.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <string>
#include <vector>

namespace
{
struct WeaponFireWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;

	WeaponFireWorld()
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		combat().setAutoAcquireEnabled(false);

		const char *ini = R"ini(
SpecialPower TestSpecialBoltPower
 Enum = SPECIAL_WORD_OF_POWER
 ReloadTime = 5000
End

Weapon TestBoltWeapon
 AttackRange = 180
 DelayBetweenShots = 600
 ClipSize = 3
 ClipReloadTime = 3000
 AutoReloadsClip = Yes
 RadiusDamageAffects = ENEMIES
 DamageNugget
  Damage = 40
  Radius = 40
  DamageType = MAGIC
  DeathType = NORMAL
 End
End

Object TestWeaponedCaster
 KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
 End
 Body = ActiveBody BodyTag
  MaxHealth = 200
 End
 Behavior = AIUpdateInterface AiTag
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = TestSpecialBoltPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = TestSpecialBoltPower
  SpecialWeapon = TestBoltWeapon
  WhichSpecialWeapon = 1
  StartAbilityRange = 160
  UnpackTime = 200
  PackTime = 200
  IgnoreFacingCheck = Yes
  BusyForDuration = 600
 End
End

Object TestWeaponlessBanner
 KindOf = STRUCTURE IMMOBILE
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 Body = ActiveBody BodyTag
  MaxHealth = 200
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = TestSpecialBoltPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = TestSpecialBoltPower
  SpecialWeapon = TestBoltWeapon
  WhichSpecialWeapon = 1
  Instant = Yes
  BusyForDuration = 600
 End
End

Object TestMissingWeaponCaster
 KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
 End
 Body = ActiveBody BodyTag
  MaxHealth = 200
 End
 Behavior = AIUpdateInterface AiTag
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = TestSpecialBoltPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = TestSpecialBoltPower
  SpecialWeapon = NoSuchSpecialWeapon_Local59
  WhichSpecialWeapon = 1
  StartAbilityRange = 160
  UnpackTime = 200
  PackTime = 200
  IgnoreFacingCheck = Yes
 End
End

Object TestEmptyWeaponCaster
 KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
 End
 Body = ActiveBody BodyTag
  MaxHealth = 200
 End
 Behavior = AIUpdateInterface AiTag
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = TestSpecialBoltPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = TestSpecialBoltPower
  WhichSpecialWeapon = 1
  StartAbilityRange = 160
  UnpackTime = 200
  PackTime = 200
  IgnoreFacingCheck = Yes
 End
End
)ini";
		const std::string err = w.load(ini);
		REQUIRE_MESSAGE(err.empty(), err);
	}

	~WeaponFireWorld() { TheSpecialPowerStore = savedPowers; }
};

void checkLoadedInCtor(Object &obj, WeaponFireSpecialAbilityUpdate &wf, const std::string &expectedWeapon, unsigned expectedClip, std::uint32_t expectedFrame)
{
	// Hash BEFORE calling ownedWeapon(): with constructor-time creation, ownedWeapon() is purely an observer and cannot alter module state
	StateHasher before;
	wf.crc(before);

	Weapon *w = wf.ownedWeapon();

	StateHasher after;
	wf.crc(after);
	CHECK(before.value() == after.value());

	REQUIRE(w != nullptr);
	CHECK(w->getOwnerID() == 0); // RW 0x895E16
	REQUIRE(w->getTemplate() != nullptr);
	CHECK(w->getTemplate()->getName() == expectedWeapon);
	CHECK(w->ammoInClip() == expectedClip);
	CHECK(w->storedStatus() == WEAPON_RELOADING_CLIP);
	CHECK(w->whenWeCanFireAgain() == expectedFrame);
	if (ObjectWeapons *ow = obj.getWeapons())
	{
		CHECK(ow->extraWeaponStatus(*w) == WEAPON_READY_TO_FIRE);
	}
}
} // namespace

TEST_CASE("LOCAL-59: WeaponFireSpecialAbilityUpdate makes and loads its special weapon in the constructor (RW 0x895DE1 .. 0x895E22)")
{
	WeaponFireWorld f;
	f.frames(3);
	const std::uint32_t createFrame = f.logic->getFrame();
	REQUIRE(createFrame == 3);

	Object *caster = f.unit("TestWeaponedCaster", 'A', 100, 100);
	REQUIRE(caster);
	auto *wfCaster = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(caster->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(wfCaster);
	checkLoadedInCtor(*caster, *wfCaster, "TestBoltWeapon", 3, createFrame);
	REQUIRE(caster->getWeapons() != nullptr);
	REQUIRE(caster->getWeapons()->weaponInSlot(PRIMARY_WEAPON) != nullptr);
	CHECK(caster->getWeapons()->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "SwordWeapon");

	// Weaponless object (mirrors deployed mine / banner): ConstructorWeaponHost loads ammo without building a premature ObjectWeapons host
	const std::uint32_t bannerCreateFrame = f.logic->getFrame();
	Object *banner = f.unit("TestWeaponlessBanner", 'A', 120, 100);
	REQUIRE(banner);
	CHECK(banner->getWeapons() == nullptr);
	auto *wfBanner = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(banner->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(wfBanner);
	checkLoadedInCtor(*banner, *wfBanner, "TestBoltWeapon", 3, bannerCreateFrame);
	CHECK(banner->getWeapons() == nullptr);

	// Missing or empty SpecialWeapon leaves m_weapon null without an error and fires cleanly without crashing
	Object *missing = f.unit("TestMissingWeaponCaster", 'A', 140, 100);
	Object *empty = f.unit("TestEmptyWeaponCaster", 'A', 160, 100);
	Object *target = f.unit("TestWeaponedCaster", 'B', 130, 100);
	REQUIRE(missing);
	REQUIRE(empty);
	REQUIRE(target);
	auto *wfMissing = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(missing->findModule("WeaponFireSpecialAbilityUpdate"));
	auto *wfEmpty = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(empty->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(wfMissing);
	REQUIRE(wfEmpty);
	CHECK(wfMissing->ownedWeapon() == nullptr);
	CHECK(wfEmpty->ownedWeapon() == nullptr);

	const SpecialPowerTemplate *sp = f.powers.findSpecialPowerTemplate("TestSpecialBoltPower");
	REQUIRE(sp != nullptr);
	wfMissing->initiateIntentToDoSpecialPower(sp, target, nullptr, 0, 0);
	wfEmpty->initiateIntentToDoSpecialPower(sp, target, nullptr, 0, 0);
	f.frames(6);
	CHECK(wfMissing->abilitiesTriggered() == 1);
	CHECK(wfMissing->shotsFired() == 0);
	CHECK(wfEmpty->abilitiesTriggered() == 1);
	CHECK(wfEmpty->shotsFired() == 0);

	// Weaponless object can still acquire its own host at fire time and damage the target
	const float hpBefore = target->getBodyModule()->getHealth();
	wfBanner->initiateIntentToDoSpecialPower(sp, nullptr, nullptr, 0, 0);
	f.frames(2);
	CHECK(wfBanner->abilitiesTriggered() == 1);
	CHECK(wfBanner->shotsFired() == 1);
	CHECK(wfBanner->ownedWeapon()->getOwnerID() == banner->getID());
	CHECK(wfBanner->ownedWeapon()->ammoInClip() == 2);
	CHECK(target->getBodyModule()->getHealth() == hpBefore - 40.0f);
}

TEST_CASE("LOCAL-59: WeaponFireSpecialAbilityUpdate xfer uses version 2, skips weapon on XferCRC (RW 0x895C9F), and refuses version 1")
{
	WeaponFireWorld f1;
	Object *caster1 = f1.unit("TestWeaponedCaster", 'A', 100, 100);
	Object *enemy1 = f1.unit("TestWeaponedCaster", 'B', 140, 100);
	Object *missing1 = f1.unit("TestMissingWeaponCaster", 'A', 180, 100);
	REQUIRE(caster1);
	REQUIRE(enemy1);
	REQUIRE(missing1);

	auto *wf1 = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(caster1->findModule("WeaponFireSpecialAbilityUpdate"));
	auto *wfMissing1 = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(missing1->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(wf1);
	REQUIRE(wfMissing1);

	// RW 0x895C9F: XferCRC returns right after SpecialAbilityUpdate::xfer (weapon mutation does not change XferCRC, while StateHasher does)
	XferCRC crcBefore;
	wf1->xfer(crcBefore);
	const unsigned savedAmmo = wf1->ownedWeapon()->ammoInClip();
	wf1->ownedWeapon()->setAmmoInClip(0);
	XferCRC crcAfter;
	wf1->xfer(crcAfter);
	CHECK(crcBefore.getCRC() == crcAfter.getCRC());
	wf1->ownedWeapon()->setAmmoInClip(savedAmmo);

	// Save before any ability fire: version byte is 2, version 1 is explicitly refused
	XferSave savePreFire;
	wf1->xfer(savePreFire);
	REQUIRE(!savePreFire.data().empty());
	CHECK(savePreFire.data()[0] == 2);

	std::vector<std::uint8_t> badV1 = savePreFire.data();
	badV1[0] = 1;
	XferLoad loadV1(badV1);
	CHECK_THROWS_WITH_AS(wf1->xfer(loadV1), doctest::Contains("Found version 1 but the earliest supported is 2"), XferException);

	// Fire once, verify state change, and round-trip into a second world
	const SpecialPowerTemplate *sp = f1.powers.findSpecialPowerTemplate("TestSpecialBoltPower");
	REQUIRE(sp != nullptr);
	wf1->initiateIntentToDoSpecialPower(sp, enemy1, nullptr, 0, 0);
	f1.frames(6);
	REQUIRE(wf1->shotsFired() == 1);
	REQUIRE(wf1->ownedWeapon()->ammoInClip() == 2);
	REQUIRE(wf1->ownedWeapon()->getOwnerID() == caster1->getID());
	const std::uint32_t expectedFireFrame = 2; // initiate at frame 0 -> unpack starts frame 1, UnpackTime 200 ms (1 frame) -> fires on frame 2
	REQUIRE(wf1->ownedWeapon()->lastFireFrame() == expectedFireFrame);

	XferSave saveFired;
	wf1->xfer(saveFired);

	XferSave saveMissing;
	wfMissing1->xfer(saveMissing);

	WeaponFireWorld f2;
	Object *caster2 = f2.unit("TestWeaponedCaster", 'A', 100, 100);
	Object *missing2 = f2.unit("TestMissingWeaponCaster", 'A', 180, 100);
	REQUIRE(caster2);
	REQUIRE(missing2);
	auto *wf2 = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(caster2->findModule("WeaponFireSpecialAbilityUpdate"));
	auto *wfMissing2 = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(missing2->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(wf2);
	REQUIRE(wfMissing2);

	XferLoad loadFired(saveFired.data());
	wf2->xfer(loadFired);
	CHECK(wf2->shotsFired() == 1);
	REQUIRE(wf2->ownedWeapon() != nullptr);
	CHECK(wf2->ownedWeapon()->ammoInClip() == 2);
	CHECK(wf2->ownedWeapon()->getOwnerID() == caster2->getID());
	CHECK(wf2->ownedWeapon()->lastFireFrame() == wf1->ownedWeapon()->lastFireFrame());

	StateHasher h1, h2;
	wf1->crc(h1);
	wf2->crc(h2);
	CHECK(h1.value() == h2.value());

	// Missing weapon round-trips as null; loading a snapshot with weapon=true onto missing-weapon module throws
	XferLoad loadMissing(saveMissing.data());
	wfMissing2->xfer(loadMissing);
	CHECK(wfMissing2->ownedWeapon() == nullptr);

	XferLoad loadMismatch(saveFired.data());
	CHECK_THROWS_AS(wfMissing2->xfer(loadMismatch), XferException);
}

