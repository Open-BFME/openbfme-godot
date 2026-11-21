// OpenBFME unit tests. GPL-3.0.
// Port 95 (issue fork issue 243): KeepObjectDie/StayOnRadar — the remains of a destroyed building stay on the minimap.
//
// RW 0x889FA7 KeepObjectDie::onDie, read with tools/mod/rw.py (RotWK 2.01 game.dat, caveat S-001):
//   * RW 0x889FAB .. 0x889FD8: an object whose controlling player (RW 0x68B678) has Player + 0x754 set (Player::isPlayerActive, RW 0x6AAC4B) is destroyed at once
//     (TheGameLogic's destroyObject RW 0x62BBAB) and the onDie ends;
//   * RW 0x889FDB .. 0x889FE8: setSpecialModelConditionState (RW 0x68B581) with model condition 0x14F (COLLAPSING, TheModelConditionNames[335]) and data + 0x38
//     (CollapsingTime) frames — the object's SMCHelper holds the timer (lane PROD-1);
//   * RW 0x889FED: isDieApplicable (RW 0x85FED5) — the DieMux rows RW 0xC76BD8 decide whether this death reaches the module;
//   * RW 0x889FFD .. 0x88A004: StayOnRadar (data + 0x3C, the row RW 0x42E558 of the table RW 0xC06C2C) 0: nothing more;
//   * RW 0x88A006 .. 0x88A011: no object, or the radar record (Object + 0x268) already set: nothing more;
//   * RW 0x88A013 .. 0x88A01A: TheRadar (RW 0xDE4AB8) addObject (RW 0x6D9042): the remains are on the radar.
// The data ctor RW 0x6538DB: vtable 0xC07A30, data + 0x38 = 0x19 (RW 0x6538E9), data + 0x3C = 0 (RW 0x6538F0); the proc RW 0x6538F8 pushes the DieMux table
// RW 0xC76BD8 and the field table RW 0xC06C2C.
// Object::onDie RW 0x698F06 at RW 0x698F70 .. 0x698F80 takes the record off when it is set (TheRadar's removeObject RW 0x6D876A), BEFORE the die modules
// (the module walk is RW 0x698F86 .. 0x698FA1), so the add in the KeepObjectDie's onDie is what keeps a dead object on the radar.
// The port's radar rebuilds its list of blips from the objects (Radar::captureObject, lane RADAR-1), so the two tests here drive that entry point with the
// object's state after the death; Radar::renderObjects (the game's blip pass) keeps the same list.
#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/Module.h"
#include "GameClient/Radar.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponStores.h"

using namespace prodtest;

namespace
{
// the radar's own gates (RW 0x6D9042): a listed blip needs a visible RadarPriority, and RW 0x68EBE9 keeps INVALID only for a garrisonable / CAPTURABLE
// template, so the tests' structures carry an explicit STRUCTURE priority as the mod's do (the port stores the field's value: STRUCTURE = 2)
const char *kRadarPriority = "  RadarPriority = STRUCTURE\n";

std::string structureHead(const char *name)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n" +
		kRadarPriority +
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 30\n"
		"  GeometryHeight = 60\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n";
}

// a keeper: a KeepObjectDie with StayOnRadar on or off and CollapsingTime when the caller names it
std::string keeperIni(const char *name, const char *stayOnRadar, const char *collapsingTime)
{
	std::string text = structureHead(name) + "  Behavior = KeepObjectDie ModuleTag_Keep\n";
	if (stayOnRadar)
	{
		text += std::string("    StayOnRadar = ") + stayOnRadar + "\n";
	}
	if (collapsingTime)
	{
		text += std::string("    CollapsingTime = ") + collapsingTime + "\n";
	}
	return text + "  End\nEnd\n";
}

// the mod's case (neutral/inn.ini:196 StayOnRadar = Yes, 10 objects), its neighbour (the object is kept, the blip is not) and a template with no KeepObjectDie
const char kPort95Objects[] =
	"Object P95Inn\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  RadarPriority = STRUCTURE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = KeepObjectDie ModuleTag_Keep\n"
	"    StayOnRadar = Yes\n"
	"  End\n"
	"End\n"
	"Object P95Shed\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  RadarPriority = STRUCTURE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = KeepObjectDie ModuleTag_Keep\n"
	"    StayOnRadar = No\n"
	"  End\n"
	"End\n"
	"Object P95Post\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  RadarPriority = STRUCTURE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct Port95World : ProdWorld
{
	WeaponStores stores; // the damage pipeline's armour store (ActiveBody refuses a hit without one)
	Port95World()
	{
		stores.install();
		load(kPort95Objects);
		logic->settings().bodyThresholdsLoaded = true; // logic test convention (MoveTestUtil): the bodies' damage states need no GameData
		local = alice();                              // the radar's local player (Radar::captureObject's `local`)
	}
	Player *local = nullptr;

	// the radar's entry of the object (Radar::captureObject: RADAR-1's live list; Radar::renderObjects draws these)
	ObjectSnapshot::RadarEntry capture(Object &o)
	{
		ObjectSnapshot::RadarEntry e;
		Radar::captureObject(*logic, o, local, e);
		return e;
	}
	// a real damage death: the body calls Object::friend_onDie, which runs every die module (ActiveBody.cpp)
	void kill(Object &o)
	{
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_deathType = DEATH_NORMAL;
		info.m_input.m_amount = 1000.0f;
		o.attemptDamage(info);
	}
};
} // namespace

TEST_CASE("PORT-95 KeepObjectDie/StayOnRadar: the remains of a destroyed structure stay on the radar, without it they leave")
{
	Port95World w;
	Object *inn = w.make("P95Inn", w.teamOf("Alice"));
	Object *shed = w.make("P95Shed", w.teamOf("Alice"));
	REQUIRE(inn != nullptr);
	REQUIRE(shed != nullptr);
	Coord3D a{ 700.0f, 700.0f, 0.0f };
	Coord3D b{ 900.0f, 700.0f, 0.0f };
	inn->setPosition(&a);
	shed->setPosition(&b);
	// alive: the radar lists both (a SELECTABLE structure of the local player with a visible priority, RW 0x6D9042's add)
	REQUIRE(w.capture(*inn).listed);
	REQUIRE(w.capture(*shed).listed);

	w.kill(*inn);
	w.kill(*shed);
	REQUIRE(inn->isEffectivelyDead()); // the ActiveBody's death ran the die modules (Object::friend_onDie RW 0x698F06)
	REQUIRE(shed->isEffectivelyDead());
	REQUIRE_FALSE(inn->isDestroyed()); // KeepObjectDie keeps the object in the world (no DestroyDie on the template)

	// RW 0x889FFD: StayOnRadar = Yes -> the object is on the radar again (RW 0x88A01A); StayOnRadar = No -> it is not (onDie RW 0x698F80 removed the record)
	CHECK(inn->stayOnRadarAfterDeath());
	CHECK_FALSE(shed->stayOnRadarAfterDeath());
	CHECK(w.capture(*inn).listed);        // the blip of the remains
	CHECK_FALSE(w.capture(*shed).listed); // the same object kind without StayOnRadar: no blip
	// the kept object's blip is the structure's (the add RW 0x6D9042 asks a dead and a live object the same questions)
	CHECK(w.capture(*inn).priority == Radar::RADAR_PRIORITY_STRUCTURE);
	CHECK(w.capture(*inn).local);
	// RW 0x889FE8: the death sets COLLAPSING (0x14F) for CollapsingTime (25 frames here: the data ctor's default, RW 0x6538E9) - the SMCHelper timer, PROD-1
	CHECK(inn->testModelCondition(CombatNames::modelCondition("COLLAPSING")));
}

TEST_CASE("PORT-95 neighbour: an object without KeepObjectDie leaves the radar at once, and a save keeps the decision")
{
	Port95World w;
	Object *post = w.make("P95Post", w.teamOf("Alice"));
	Object *inn = w.make("P95Inn", w.teamOf("Alice"));
	REQUIRE(post != nullptr);
	REQUIRE(inn != nullptr);
	Coord3D a{ 700.0f, 700.0f, 0.0f };
	Coord3D b{ 900.0f, 700.0f, 0.0f };
	post->setPosition(&a);
	inn->setPosition(&b);
	REQUIRE(w.capture(*post).listed);

	w.kill(*post);
	w.kill(*inn);
	// a template with no KeepObjectDie: the body's death removes the object and the radar drops it (RW 0x698F80)
	CHECK(post->isEffectivelyDead());
	CHECK_FALSE(w.capture(*post).listed);
	CHECK_FALSE(post->stayOnRadarAfterDeath());
	CHECK(w.capture(*inn).listed);

	// lane SAVE-4b: the decision is simulation state and travels in the object's snapshot (Object::xfer)
	const std::uint32_t before = w.logic->computeStateHash();
	inn->setStayOnRadarAfterDeath(false);
	CHECK(w.logic->computeStateHash() != before);
	inn->setStayOnRadarAfterDeath(true);
	CHECK(w.logic->computeStateHash() == before);
}

TEST_CASE("PORT-95 construction: CollapsingTime keeps the data ctor's 0x19 unless the INI names it, and StayOnRadar parses to the module data")
{
	// the data ctor RW 0x6538DB writes 0x19 to + 0x38 (RW 0x6538E9) and 0 to + 0x3C (RW 0x6538F0): a template that names only StayOnRadar keeps 25 frames;
	// CollapsingTime is a duration (RW 0x73A429): its INI milliseconds become frames, 1000 ms = 5 frames (ceil(ms * 0.005))
	const std::string text = keeperIni("P95Defaults", "Yes", nullptr) + keeperIni("P95Timed", "Yes", "1000");
	Port95World w;
	CHECK(w.w.load(text).empty());
	for (const char *name : { "P95Defaults", "P95Timed" })
	{
		Object *o = w.make(name, w.teamOf("Alice"));
		REQUIRE(o != nullptr);
		const KeepObjectDie *keep = dynamic_cast<const KeepObjectDie *>(o->findModule("KeepObjectDie"));
		REQUIRE(keep != nullptr);
		const KeepObjectDieModuleData *data = dynamic_cast<const KeepObjectDieModuleData *>(keep->getModuleData());
		REQUIRE(data != nullptr);
		CHECK(data->m_stayOnRadar);
		CHECK(data->m_collapsingTime == (std::string(name) == "P95Timed" ? 5u : 0x19u));
	}
}
