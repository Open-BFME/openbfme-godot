// OpenBFME unit tests (lane MODCOMPAT-MOD1): GrabPassengerSpecialPower (data RW 0x8C98AA / table RW 0xC74534, doSpecialPowerAtObject RW 0x8C94D7,
// doSpecialPower RW 0x8C9629). Synthetic data (no retail files); the expected values follow the RotWK functions cited at each check. GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "HudTestUtil.h"
#include "StealthArena.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GrabPassengerSpecialPower.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char kPowers[] = R"ini(
SpecialPower GrabTestPower
 Enum = SPECIAL_GRAB_PASSENGER
 ReloadTime = 0
End
SpecialPower GrabTestKnife
 Enum = SPECIAL_KNIFE_ATTACK
 ReloadTime = 0
End
)ini";

std::string grabber(const std::string &name, const std::string &power, const std::string &grabFields)
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
 Behavior = GrabPassengerSpecialPower ModuleTag_Grab
  SpecialPowerTemplate = )ini" + power + "\n  UpdateModuleStartsAttack = Yes\n" + grabFields + R"ini( End
 Behavior = SpecialAbilityUpdate ModuleTag_GrabUpdate
  SpecialPowerTemplate = )ini" + power + R"ini(
  StartAbilityRange = 8
  UnpackTime = 300
  PackTime = 300
 End
End
)ini";
}

struct GrabWorld
{
	combattest::CombatWorld w;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	GrabWorld()
	{
		TheSpecialPowerStore = &powers;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		std::string err = w.w.load(kPowers, INI_LOAD_OVERWRITE, "grabpowers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.w.load(grabber("GrabTroll", "GrabTestPower", "  GrabRadius = 200\n") + grabber("GrabDefaults", "GrabTestPower", "  AllowTree = No\n") +
				grabber("GrabKnifeTroll", "GrabTestKnife", "  GrabRadius = 200\n"),
			INI_LOAD_OVERWRITE, "grabbers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		w.combat().setAutoAcquireEnabled(false);
	}
	~GrabWorld() { TheSpecialPowerStore = savedPowers; }
};

template <class T>
T *moduleOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (T *x = dynamic_cast<T *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

bool tagged(const Object &o)
{
	static const int kTagged = ObjectTemplateInfoBuilder::objectStatusIndex("TAGGED");
	return kTagged >= 0 && o.testStatus((unsigned)kTagged);
}
} // namespace

TEST_CASE("grabpassenger: the data is SpecialPowerModuleData then RW 0xC74534 (GrabRadius 0.0, AllowTree true: RW 0x8C98AA)")
{
	GrabWorld f;
	Object *a = f.w.unit("GrabDefaults", 'A', 100.0f, 100.0f);
	Object *b = f.w.unit("GrabTroll", 'A', 200.0f, 100.0f);
	GrabPassengerSpecialPower *ga = moduleOf<GrabPassengerSpecialPower>(a);
	GrabPassengerSpecialPower *gb = moduleOf<GrabPassengerSpecialPower>(b);
	REQUIRE(ga);
	REQUIRE(gb);
	CHECK_FALSE(ga->isUnported());
	CHECK(ga->grabData()->m_grabRadius == 0.0f);
	CHECK(ga->grabData()->m_allowTree == false);
	CHECK(gb->grabData()->m_grabRadius == 200.0f);
	CHECK(gb->grabData()->m_allowTree == true);
	CHECK(gb->grabData()->m_updateModuleStartsAttack); // the base table RW 0xC64DB0 parses first
	REQUIRE(gb->getSpecialPowerTemplate());
	CHECK(gb->getSpecialPowerTemplate()->getSpecialPowerType() == 0x27);
}

TEST_CASE("grabpassenger: doSpecialPowerAtObject (RW 0x8C94D7): a 0x27 power asks RW 0x82D925 first (an object without a contain fails the grab ability's room "
		  "test RW 0x851BA2 and the grab object test RW 0x82DB6A); a HORDE "
		  "target gives its first untagged member, TAGGED, to the initiate")
{
	GrabWorld f;
	Object *troll = f.w.unit("GrabTroll", 'A', 200.0f, 200.0f);
	Object *prey = f.w.unit("Swordsman", 'B', 240.0f, 200.0f);
	f.w.frames(2);
	GrabPassengerSpecialPower *grab = moduleOf<GrabPassengerSpecialPower>(troll);
	REQUIRE(grab);
	// RW 0x82D925 with the source requirements (last argument 1): the 0x27 module's SpecialAbilityUpdate (slot 0x20, RW 0x851B6E) answers first: no contain,
	// no room (RW 0x851BA2)
	SpecialAbilityUpdate *trollAbility = moduleOf<SpecialAbilityUpdate>(troll);
	REQUIRE(trollAbility);
	CHECK_FALSE(trollAbility->allowsSpecialPowerUse());
	grab->doSpecialPowerAtObject(prey, 0);
	CHECK(grab->grabsStarted() == 0);
	// without the source requirements the 0x27 object test itself (RW 0x82DB6A) refuses an object without a contain; it is ported (not counted)
	const unsigned long long before27 = ActionManager::stats().unportedObjectTypeTests[0x27];
	bool unported = false;
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*troll, prey, CMD_FROM_AI, grab->getSpecialPowerTemplate(), 0, false, &unported));
	CHECK_FALSE(unported);
	CHECK(ActionManager::stats().unportedObjectTypeTests[0x27] == before27);
	// a module whose power is not 0x27 skips the check: the horde's first untagged member is the target
	Object *knifer = f.w.unit("GrabKnifeTroll", 'A', 300.0f, 300.0f);
	Object *horde = f.w.unit("ArcherHorde", 'B', 360.0f, 300.0f);
	f.w.frames(3);
	GrabPassengerSpecialPower *kgrab = moduleOf<GrabPassengerSpecialPower>(knifer);
	SpecialAbilityUpdate *ability = moduleOf<SpecialAbilityUpdate>(knifer);
	REQUIRE(kgrab);
	REQUIRE(ability);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->getContainCount() > 1); // tolerance: any horde of two or more members serves (the fixture sets its size)
	const Object *first = horde->getContain()->getContainedItemsList()->front();
	kgrab->doSpecialPowerAtObject(horde, 0);
	CHECK(kgrab->grabsStarted() == 1);
	CHECK(tagged(*first)); // RW 0x62684D(0x3F, 1)
	CHECK(ability->isActive());
	// a disabled or paused module does nothing without option 0x40000
	knifer->setDisabled(DISABLED_HELD, 0x3FFFFFFFu);
	kgrab->doSpecialPowerAtObject(horde, 0);
	CHECK(kgrab->grabsStarted() == 1);
}

TEST_CASE("grabpassenger: doSpecialPower (RW 0x8C9629): the closest enemy INFANTRY that is not UNSELECTABLE within GrabRadius goes to the object's 0x27 "
		  "module at once (slot 0x2C, options 0x2000); option 0x2000 looks for a ROCK_VENDOR only")
{
	GrabWorld f;
	Object *troll = f.w.unit("GrabTroll", 'A', 200.0f, 200.0f);
	f.w.unit("Swordsman", 'A', 220.0f, 200.0f);   // a friend: not ENEMIES
	f.w.unit("Swordsman", 'B', 500.0f, 200.0f);   // beyond GrabRadius 200
	Object *near = f.w.unit("Swordsman", 'B', 300.0f, 200.0f);
	(void)near;
	f.w.frames(2);
	GrabPassengerSpecialPower *grab = moduleOf<GrabPassengerSpecialPower>(troll);
	REQUIRE(grab);
	grab->doSpecialPower(0);
	CHECK(grab->autoGrabsAtObject() == 1);
	CHECK(grab->grabsStarted() == 0); // the 0x27 module's room test refuses it (no contain)
	grab->doSpecialPower(0x2000);
	CHECK(grab->autoGrabsAtObject() == 1); // no ROCK_VENDOR around
	CHECK(grab->autoGrabsAtTree() == 0);
}

TEST_CASE("grabpassenger: the snapshot (SpecialPowerModule's, then the counters) round-trips")
{
	GrabWorld f;
	Object *knifer = f.w.unit("GrabKnifeTroll", 'A', 300.0f, 300.0f);
	Object *horde = f.w.unit("ArcherHorde", 'B', 360.0f, 300.0f);
	f.w.frames(3);
	GrabPassengerSpecialPower *grab = moduleOf<GrabPassengerSpecialPower>(knifer);
	REQUIRE(grab);
	grab->doSpecialPowerAtObject(horde, 0);
	REQUIRE(grab->grabsStarted() == 1);
	StateHasher before;
	grab->crc(before);
	XferSave saved;
	grab->xfer(saved);
	grab->doSpecialPowerAtObject(horde, 0x40000u);
	REQUIRE(grab->grabsStarted() == 2);
	{
		XferLoad load(saved.data());
		grab->xfer(load);
	}
	StateHasher after;
	grab->crc(after);
	CHECK(after.value() == before.value());
	CHECK(grab->grabsStarted() == 1);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(grab->xfer(lt), XferException);
}


// ---- retail data ---------------------------------------------------------------------------------------------------------------------------------------

namespace
{
SpecialAbilityUpdate *grabAbilityOf(Object &o)
{
	for (const auto &m : o.modules())
	{
		SpecialAbilityUpdate *u = dynamic_cast<SpecialAbilityUpdate *>(m.get());
		if (u && u->abilityData()->m_specialPowerTemplate &&
			u->abilityData()->m_specialPowerTemplate->getSpecialPowerType() == 0x27)
		{
			return u;
		}
	}
	return nullptr;
}
} // namespace

// The Watcher (WatcherHead, retail watcher.ini: GrabPassengerSpecialPower; SpecialAbilityUpdate StartAbilityRange 240, UnpackTime 3000, PackTime 3400,
// GrabPassengerAnimAndDuration EATING 4000 / TriggerTime 200; TransportContain Slots 1, PassengerFilter ANY +INFANTRY -HERO -HORDE) grabs a Gondor
// soldier with the eat option 0x4000 (the option the AutoPickUpUpdate's EatObjectEntry passes: test_autopickup_modules.cpp): RW 0x82D925 allows it
// (room RW 0x851BA2, object test RW 0x82DB6A), the unpack runs, the 0x27 enum prepares 0 frames (RW 0x85241F) and the first trigger contains the soldier
// (RW 0x854353 .. 0x854397: held, UNSELECTABLE); one trigger only (RW 0x852465), the pack, then finishAbility's eat (RW 0x8532C3): the contain's countdown
// (RW 0x865853, TriggerTime 1 frame) takes the soldier out, scores the kill and destroys it (RW 0x8669B1); the Watcher shows EATING and its AI is busy
TEST_CASE("grabpassenger retail: the Watcher grabs a soldier, holds it, then eats it")
{
	if (!hudtest::haveWorld("grabpassenger retail"))
	{
		return;
	}
	stealthtest::Arena a(hudtest::shared());
	Object *watcher = a.place("WatcherHead", 1, 600.0f, 600.0f);
	// the Watcher's own player's soldier (an enemy would be killed by the Watcher's attacks before the eat; the PassengerFilter and AllowAlliesInside take it)
	Object *soldier = a.place("GondorFighter", 1, 700.0f, 600.0f);
	a.run(5);
	REQUIRE_FALSE(soldier->isEffectivelyDead());
	GrabPassengerSpecialPower *grab = moduleOf<GrabPassengerSpecialPower>(watcher);
	SpecialAbilityUpdate *ability = grabAbilityOf(*watcher);
	REQUIRE(grab);
	REQUIRE(ability);
	OpenContain *contain = dynamic_cast<OpenContain *>(watcher->getContain());
	REQUIRE(contain);
	CHECK(contain->getContainMax() == 1);
	CHECK(ability->allowsSpecialPowerUse());
	const ObjectID soldierId = soldier->getID();
	grab->doSpecialPowerAtObject(soldier, 0x4000u);
	REQUIRE(grab->grabsStarted() == 1);
	CHECK(ability->isActive());
	int heldAt = -1;
	for (int i = 0; i < 60 && heldAt < 0; ++i)
	{
		a.run(1);
		if (soldier->getContainedBy() == watcher)
		{
			heldAt = i;
		}
	}
	// measured: held 17 frames after the order (UnpackTime 3000 ms = 15 frames, then the preparation and the trigger)
	REQUIRE_MESSAGE(heldAt == 16, "held after " << heldAt + 1 << " frames (0: never)");
	CHECK(soldier->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE"))); // ObjectStatusOfContained
	CHECK(ability->triggerCount() == 1);
	CHECK_FALSE(ability->allowsSpecialPowerUse()); // the one slot is full (RW 0x851BA2)
	// a second grab is refused while it holds one
	Object *other = a.place("GondorFighter", 1, 650.0f, 650.0f);
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*watcher, other, CMD_FROM_AI, grab->getSpecialPowerTemplate(), 0x4000u, true));
	int eatenAt = -1;
	for (int i = 0; i < 80 && eatenAt < 0; ++i)
	{
		a.run(1);
		if (!a.logic.findObjectByID(soldierId))
		{
			eatenAt = i;
		}
	}
	// measured: eaten 18 frames after the hold (PackTime 3400 ms = 17 frames, then the eat countdown: TriggerTime 200 ms = 1 frame)
	REQUIRE_MESSAGE(eatenAt == 17, "eaten " << eatenAt + 1 << " frames after the hold (0: never)");
	CHECK(contain->getContainCount() == 0);
	CHECK(contain->eatVictim() == soldierId);
	CHECK(contain->eatFramesLeft() == 0);
	CHECK(ability->allowsSpecialPowerUse()); // room again
	static const int kEating = CombatNames::modelCondition("EATING");
	REQUIRE(kEating >= 0); // tolerance: only that the binary names EATING (its index is the name table's)
	CHECK(watcher->testModelCondition(kEating)); // AnimTime 4000 ms
}

TEST_CASE("grabpassenger retail: the eat countdown (RW 0x865853, contain interface + 0xC4 / 0xC8 / 0xCC) is in the contain's snapshot and hash")
{
	if (!hudtest::haveWorld("grabpassenger retail"))
	{
		return;
	}
	stealthtest::Arena a(hudtest::shared());
	Object *watcher = a.place("WatcherHead", 1, 600.0f, 600.0f);
	Object *soldier = a.place("GondorFighter", 0, 700.0f, 600.0f);
	a.run(2);
	OpenContain *contain = dynamic_cast<OpenContain *>(watcher->getContain());
	REQUIRE(contain);
	contain->eatPassengerAfter(*soldier, 0, *watcher);
	CHECK(contain->eatFramesLeft() == 1); // max(frames, 1)
	contain->eatPassengerAfter(*soldier, 7, *watcher);
	StateHasher before;
	contain->crc(before);
	XferSave saved;
	contain->xfer(saved);
	contain->eatPassengerAfter(*watcher, 3, *soldier);
	{
		XferLoad load(saved.data());
		contain->xfer(load);
	}
	StateHasher after;
	contain->crc(after);
	CHECK(after.value() == before.value());
	CHECK(contain->eatVictim() == soldier->getID());
	CHECK(contain->eatFramesLeft() == 7);
}

// players' saves made before the eat (OpenContain snapshot version 1) keep loading: they load as not eating (no victim, no killer, no frames left)
TEST_CASE("grabpassenger retail: a version 1 OpenContain snapshot (before the eat) loads as not eating")
{
	if (!hudtest::haveWorld("grabpassenger retail"))
	{
		return;
	}
	stealthtest::Arena a(hudtest::shared());
	Object *watcher = a.place("WatcherHead", 1, 600.0f, 600.0f);
	Object *soldier = a.place("GondorFighter", 1, 700.0f, 600.0f);
	a.run(2);
	OpenContain *contain = dynamic_cast<OpenContain *>(watcher->getContain());
	REQUIRE(contain);
	XferSave fresh; // version 2, not eating
	contain->xfer(fresh);
	StateHasher notEating;
	contain->crc(notEating);
	contain->eatPassengerAfter(*soldier, 7, *watcher);
	XferSave eating;
	contain->xfer(eating);
	// the version 1 layout: the eating snapshot without the three fields (victim, killer, frames: little-endian 32-bit words), version byte 1. The
	// TransportContain's own version byte comes first, OpenContain's second
	std::vector<std::uint8_t> bytes = eating.data();
	auto word = [](std::uint32_t v) { return std::vector<std::uint8_t>{ (std::uint8_t)v, (std::uint8_t)(v >> 8), (std::uint8_t)(v >> 16), (std::uint8_t)(v >> 24) }; };
	std::vector<std::uint8_t> eat = word(soldier->getID());
	const std::vector<std::uint8_t> killer = word(watcher->getID()), frames = word(7);
	eat.insert(eat.end(), killer.begin(), killer.end());
	eat.insert(eat.end(), frames.begin(), frames.end());
	auto at = std::search(bytes.begin(), bytes.end(), eat.begin(), eat.end());
	REQUIRE(at != bytes.end());
	CHECK(std::search(at + 1, bytes.end(), eat.begin(), eat.end()) == bytes.end()); // the fields are found once
	bytes.erase(at, at + (std::ptrdiff_t)eat.size());
	REQUIRE(bytes.size() == fresh.data().size() - 12);
	CHECK(bytes[1] == 2);
	bytes[1] = 1;
	{
		XferLoad load(bytes);
		contain->xfer(load);
	}
	CHECK(contain->eatVictim() == INVALID_ID);
	CHECK(contain->eatFramesLeft() == 0);
	StateHasher loaded;
	contain->crc(loaded);
	CHECK(loaded.value() == notEating.value());
	// a version above the current one is refused
	bytes[1] = 3;
	XferLoad bad(bytes);
	CHECK_THROWS_AS(contain->xfer(bad), XferException);
}
