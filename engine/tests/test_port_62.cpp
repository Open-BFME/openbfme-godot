// OpenBFME. PORT-62 tests: a TransportContain's GrabWeapon fire and FireGrabWeaponOnVictim, RotWK 2.01 RW 0x86AA4B .. 0x86AAA2 inside
// TransportContain::onContaining (RW 0x86A943, reached from OpenContain::addToContain RW 0x8674C2 at RW 0x867530).
//
// Synthetic data only (combattest::CombatWorld's engine INI reader and field tables, no retail or mod file, value or byte). Every test name begins
// PORT-62 so the lane's narrow filter is -tc="PORT-62*".
//
// What RW does:
//   * RW 0x86AA4B .. 0x86AA51: GrabWeapon (data + 0x144, a WeaponTemplate pointer, null when the INI names none or a name the store does not know) absent ->
//     the whole block is skipped.
//   * RW 0x86AA54 .. 0x86AA60: Object::getRelationship (RW 0x68D7AB) of the container and the rider; ALLIES (2) -> skipped.
//   * RW 0x86AA62 .. 0x86AA71: FireGrabWeaponOnVictim (data + 0x148) false -> Object::setStatus (RW 0x62684D) gives the rider UNATTACKABLE (status 0x3C).
//   * RW 0x86AA76 .. 0x86AA8A: the fire itself, in BOTH branches: TheWeaponStore (RW 0xDE4A1C) -> createAndFireTempWeapon (RW 0x6CF530) from the container at
//     the container's own position (Object + 0x38). The flag does not gate it.
//   * RW 0x86AA8F .. 0x86AA9E: FireGrabWeaponOnVictim false -> the same status comes off the rider again, right after that one fire.
// The bracket is not inert: one of the image's five readers of status 0x3C sits on the fire's own path, in the body's damage acceptance (RW 0x8C3FFB ..
// 0x8C400C: after the indestructible test RW 0x8C3FDA and INVULNERABLE RW 0x8C3FE7, an UNATTACKABLE victim is refused unless the damage type is 8,
// UNRESISTABLE) — the port's own ActiveBody::attemptDamage. The other four (RW 0x6C920B / 0x6C93E4 in WeaponSet::getAbleToAttackSpecificObject, RW 0x75120E,
// RW 0x76DF7C) are not on it, and RW's area query excludes the KindOf bit UNATTACKABLE (RW 0x90DF78 pushes 0x36), not the status, so the shielded rider is
// still a candidate and is refused only at the acceptance. Net: with the flag off everything in the grab's area takes the damage except the rider just grabbed;
// with it on (the default) the grabbed victim takes its own grab's splash like any other enemy. No new simulation state is needed for any of it.
//
// Fixture: the grabber is a STRUCTURE IMMOBILE container (no BIG_MONSTER and no TRANSPORT KindOf, so RW 0x90DF5A .. 0x90DF73's exception does not apply and
// the nugget's area stays the 3D bounding sphere of RW 0x90DF3C at Radius 60); the rider and the bystander are INFANTRY with
// NoArmor (DEFAULT 100%), so the 40 SLASH of the nugget is exactly 40 off their 100 health. ObjectStatusOfContained is UNSELECTABLE only, so the rider stays
// in the world (RW 0x865D3D takes an object out of the world only for an ENCLOSED container) and is a candidate of the area query like any other object.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"

#include <functional>
#include <string>

using namespace combattest;

namespace
{
// GrabWeapon is a name the contain parser resolves through the parse hook (S-083): a field whose lookup is not installed is a loud INIException(8), never a
// silently accepted name. Retail resolves it at parse time against TheWeaponStore (RW 0x73AE79 -> RW 0x6CC5DF: the template pointer, null for an unknown name,
// no error), and the fixture installs that store itself (CombatWorld's WeaponStores::install), so the hook asks the store and nothing else.
struct WeaponLookup
{
	std::function<bool(const std::string &)> saved = TheContainParseHooks().weaponTemplateExists;
	WeaponLookup()
	{
		TheContainParseHooks().weaponTemplateExists =
		    [](const std::string &n) { return TheWeaponStore != nullptr && TheWeaponStore->findWeaponTemplate(n) != nullptr; };
	}
	~WeaponLookup() { TheContainParseHooks().weaponTemplateExists = saved; }
};
// Port62Grab stands for the mod's grab weapon shape (an area nugget that affects enemies and not the source's own kind), reduced to what the fire needs: no
// MetaImpactNugget, whose shockwave is a client effect here. `grabLines` is the container's GrabWeapon / FireGrabWeaponOnVictim pair, so a test can leave it
// empty (RW's data + 0x144 stays null) or name a weapon the store does not know (the parser leaves the pointer null there too).
const char *const kGrab = "    GrabWeapon = Port62Grab\n";
const char *const kFireNo = "    FireGrabWeaponOnVictim = No\n";
const char *const kFireYes = "    FireGrabWeaponOnVictim = Yes\n";
const char *const kGrabWeaponBlock = "Weapon Port62Grab\n"
                                     "  RadiusDamageAffects = ENEMIES NOT_SIMILAR\n"
                                     "  DamageNugget\n"
                                     "    Damage = 40\n"
                                     "    Radius = 60\n"
                                     "    DelayTime = 0\n"
                                     "    DamageType = SLASH\n"
                                     "    DeathType = NORMAL\n"
                                     "  End\n"
                                     "End\n";
// `weaponAfterObjects` emits the Weapon block after every object block: the grabber's module data is parsed while TheWeaponStore does not know the name yet, so
// INI::parseWeaponTemplate (RW 0x6CBADE) leaves data + 0x144 null even though the name exists by the time a rider enters. Retail does not rescue that pointer, and
// neither does the port (`StoreReference::resolved`), which is what the late-definition test below pins.
std::string objects(const std::string &grabLines, bool weaponAfterObjects = false)
{
	const std::string weapon = kGrabWeaponBlock;
	return (weaponAfterObjects ? std::string() : weapon)
	       + "Object Port62Grabber\n"
	                   "  KindOf = STRUCTURE IMMOBILE\n"
	                   "  Geometry = CYLINDER\n"
	                   "  GeometryMajorRadius = 20\n"
	                   "  GeometryMinorRadius = 20\n"
	                   "  GeometryHeight = 30\n"
	                   "  Body = ActiveBody ModuleTag_Body\n"
	                   "    MaxHealth = 1000\n"
	                   "  End\n"
	                   "  Behavior = TransportContain ModuleTag_Contain\n"
	                   "    Slots = 1\n"
	                   "    AllowEnemiesInside = Yes\n"
	                   "    ObjectStatusOfContained = UNSELECTABLE\n"
	       + grabLines + "  End\n"
	       + "End\n"
	       + "Object Port62Rider\n"
	       + "  KindOf = INFANTRY SELECTABLE\n"
	       + "  TransportSlotCount = 1\n"
	       + "  Geometry = CYLINDER\n"
	       + "  GeometryMajorRadius = 8\n"
	       + "  GeometryMinorRadius = 8\n"
	       + "  GeometryHeight = 20\n"
	       + "  ArmorSet\n"
	       + "    Conditions = None\n"
	       + "    Armor = NoArmor\n"
	       + "  End\n"
	       + "  Body = ActiveBody ModuleTag_Body\n"
	       + "    MaxHealth = 100\n"
	       + "  End\n"
	       + "End\n"
	       + "Object Port62Bystander\n"
	       + "  KindOf = INFANTRY SELECTABLE\n"
	       + "  Geometry = CYLINDER\n"
	       + "  GeometryMajorRadius = 8\n"
	       + "  GeometryMinorRadius = 8\n"
	       + "  GeometryHeight = 20\n"
	       + "  ArmorSet\n"
	       + "    Conditions = None\n"
	       + "    Armor = NoArmor\n"
	       + "  End\n"
	       + "  Body = ActiveBody ModuleTag_Body\n"
	       + "    MaxHealth = 100\n"
	       + "  End\n"
	       + "End\n"
	       + (weaponAfterObjects ? weapon : std::string());
}

struct Grabbed
{
	float bystander = 0.0f;
	float rider = 0.0f;
	bool riderUnattackable = false;
	bool contained = false;
};

// a rider of `riderSide` enters the grabber; the bystander (Bob) stands 30 away, inside the nugget's radius
Grabbed grab(const std::string &grabLines, char riderSide)
{
	const WeaponLookup lookup;
	CombatWorld w(objects(grabLines).c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *container = w.unit("Port62Grabber", 'A', 200.0f, 200.0f);
	Object *rider = w.unit("Port62Rider", riderSide, 210.0f, 200.0f);
	Object *bystander = w.unit("Port62Bystander", 'B', 230.0f, 200.0f);
	REQUIRE(container != nullptr);
	REQUIRE(rider != nullptr);
	REQUIRE(bystander != nullptr);
	TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
	REQUIRE(transport != nullptr);
	REQUIRE(TheWeaponStore != nullptr); // CombatWorld's WeaponStores::install
	REQUIRE(TheWeaponStore->findWeaponTemplate("Port62Grab") != nullptr); // the template RW keeps at data + 0x144
	REQUIRE(w.health(bystander) == 100.0f);
	REQUIRE(w.health(rider) == 100.0f);
	Grabbed g;
	g.contained = transport->addToContain(rider); // OpenContain RW 0x8674C2 -> onContaining RW 0x867530 -> the grab block RW 0x86AA4B
	REQUIRE(g.contained);
	g.bystander = w.health(bystander);
	g.rider = w.health(rider);
	g.riderUnattackable = rider->testStatus((unsigned)CombatNames::status("UNATTACKABLE"));
	std::printf("  info: grab lines '%s', rider side %c: bystander %.1f, rider %.1f, rider UNATTACKABLE %d\n", grabLines.c_str(), riderSide, (double)g.bystander,
	            (double)g.rider, (int)g.riderUnattackable);
	return g;
}
} // namespace

// The fire (RW 0x86AA76 .. 0x86AA8A) and FireGrabWeaponOnVictim's bracket around it (RW 0x86AA62 .. 0x86AA71 / RW 0x86AA8F .. 0x86AA9E). All three spellings of
// the field fire: the flag is read only at the two setStatus calls, and neither branch skips RW 0x86AA8A, so the bystander next to the grabber loses its 40
// health in every case. What the flag decides is the rider: with `No` the body's damage acceptance (RW 0x8C3FFB .. 0x8C400C) refuses the SLASH of a victim that
// is UNATTACKABLE for that one fire, so the grabbed rider keeps its 100 health; with `Yes` and with the field absent (the parser's default is true) it takes the
// same 40 as the bystander. The rider is not UNATTACKABLE afterwards because RW 0x86AA9E takes the status off again before onContaining returns. Without the
// ported block nothing fires at all: both keep their 100 health, which is the failure this lane fixes.
TEST_CASE("PORT-62 grab fire: a non-allied rider is fired at, with FireGrabWeaponOnVictim on and off (RW 0x86AA4B .. 0x86AAA2)")
{
	struct Case
	{
		std::string lines;
		float rider;
	};
	for (const Case &c : { Case{ std::string(kGrab) + kFireNo, 100.0f }, Case{ std::string(kGrab) + kFireYes, 60.0f }, Case{ std::string(kGrab), 60.0f } })
	{
		CAPTURE(c.lines);
		const Grabbed g = grab(c.lines, 'B'); // Bob against Alice's grabber: ENEMIES, so RW 0x86AA5D does not skip
		CHECK(g.bystander == 60.0f);          // 40 SLASH through NoArmor's DEFAULT 100%: the fire ran
		CHECK(g.rider == c.rider);            // RW 0x8C3FFB: the bracketed rider is refused, the unbracketed one is not
		CHECK_FALSE(g.riderUnattackable); // RW 0x86AA98 .. 0x86AA9E: the status is for that one fire only
	}
}

// The relationship gate (RW 0x86AA54 .. 0x86AA60): a rider that is an ALLIES of the container leaves the block before the fire, so the bystander next to the
// grabber is untouched and no bracket is counted. This is the neighbouring behaviour the fire must not disturb.
TEST_CASE("PORT-62 allies: an ALLIES rider is not fired at (RW 0x86AA54 .. 0x86AA60)")
{
	const Grabbed g = grab(std::string(kGrab) + kFireNo, 'A'); // Alice's rider in Alice's grabber: ALLIES = 2
	CHECK(g.bystander == 100.0f);
	CHECK(g.rider == 100.0f);
	CHECK_FALSE(g.riderUnattackable);
}

// The template gate (RW 0x86AA4B .. 0x86AA51): data + 0x144 is a pointer that INI's weapon parser leaves null when the object names no GrabWeapon or a name
// TheWeaponStore does not know, and the block is skipped in both cases. Neighbouring behaviour: a transport without a grab weapon grabs silently, as before.
TEST_CASE("PORT-62 no grab weapon: without a GrabWeapon template nothing fires (RW 0x86AA4B .. 0x86AA51)")
{
	for (const std::string &lines : { std::string(kFireNo), std::string("    GrabWeapon = Port62NoSuchWeapon\n") + kFireNo })
	{
		CAPTURE(lines);
		const WeaponLookup lookup;
		CombatWorld w(objects(lines).c_str());
		w.combat().setAutoAcquireEnabled(false);
		Object *container = w.unit("Port62Grabber", 'A', 200.0f, 200.0f);
		Object *rider = w.unit("Port62Rider", 'B', 210.0f, 200.0f);
		Object *bystander = w.unit("Port62Bystander", 'B', 230.0f, 200.0f);
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		REQUIRE(bystander != nullptr);
		TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
		REQUIRE(transport != nullptr);
		REQUIRE(transport->addToContain(rider));
		CHECK(w.health(bystander) == 100.0f);
		CHECK(w.health(rider) == 100.0f);
	}
}

// A Weapon block that comes AFTER the transport that names it (review on PR #287): TheWeaponStore knows "Port62Grab" by the time a rider enters, because the
// store is filled by the same INI load, but retail's data + 0x144 is the pointer INI::parseWeaponTemplate (RW 0x6CBADE) produced while the object's module data
// was parsed — and that pointer was null. Nothing resurrects it later, so the grab block at RW 0x86AA4B is skipped. A lookup made at fire time instead would
// find the late definition and fire a weapon retail never fires; this case pins the parse-time answer (StoreReference::resolved) against that.
TEST_CASE("PORT-62 late weapon definition: a GrabWeapon defined after its transport does not fire (RW 0x86AA4B .. 0x86AA51)")
{
	const WeaponLookup lookup;
	CombatWorld w(objects(kGrab, /*weaponAfterObjects=*/true).c_str()); // the Weapon block follows every object block
	w.combat().setAutoAcquireEnabled(false);
	REQUIRE(TheWeaponStore != nullptr);
	REQUIRE(TheWeaponStore->findWeaponTemplate("Port62Grab") != nullptr); // the name the container holds IS in the store now
	Object *container = w.unit("Port62Grabber", 'A', 200.0f, 200.0f);
	Object *rider = w.unit("Port62Rider", 'B', 210.0f, 200.0f);
	Object *bystander = w.unit("Port62Bystander", 'B', 230.0f, 200.0f);
	REQUIRE(container != nullptr);
	REQUIRE(rider != nullptr);
	REQUIRE(bystander != nullptr);
	TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
	REQUIRE(transport != nullptr);
	REQUIRE(transport->addToContain(rider));
	CHECK(w.health(bystander) == 100.0f); // RW 0x86AA4B reads the null pointer: no fire
	CHECK(w.health(rider) == 100.0f);
	CHECK_FALSE(rider->testStatus((unsigned)CombatNames::status("UNATTACKABLE"))); // and RW 0x86AA6B was never reached
}

// An unknown GrabWeapon name (review on PR #287): retail's +0x144 is null and the block is skipped silently, which the port keeps — but the port also reports
// the name once per world (GameLogic::reportError dedupes its text), like an unknown warhead (WeaponDelivery.cpp) or an unknown armour (ActiveBody.cpp), so a
// misspelt or forward-named GrabWeapon reaches the report instead of looking like "this transport has no grab at all".
TEST_CASE("PORT-62 unknown grab weapon: nothing fires and the name reaches the report once (RW 0x86AA4B .. 0x86AA51)")
{
	const WeaponLookup lookup;
	CombatWorld w(objects(std::string("    GrabWeapon = Port62NoSuchWeapon\n") + kFireNo).c_str());
	w.combat().setAutoAcquireEnabled(false);
	for (int i = 0; i < 2; ++i) // two grabbers of the same template, one rider each: the report text dedupes to one line per name
	{
		CAPTURE(i);
		Object *container = w.unit("Port62Grabber", 'A', 200.0f + 40.0f * i, 200.0f);
		Object *rider = w.unit("Port62Rider", 'B', 210.0f + 40.0f * i, 200.0f);
		Object *bystander = w.unit("Port62Bystander", 'B', 230.0f + 40.0f * i, 200.0f);
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		REQUIRE(bystander != nullptr);
		TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
		REQUIRE(transport != nullptr);
		REQUIRE(transport->addToContain(rider));
		CHECK(w.health(bystander) == 100.0f);
		CHECK(w.health(rider) == 100.0f);
	}
	int reported = 0;
	for (const std::string &e : w.logic->report().errors)
	{
		if (e.find("Port62NoSuchWeapon") == std::string::npos)
		{
			continue;
		}
		++reported;
		CHECK(e.find("GrabWeapon") != std::string::npos);
	}
	CHECK(reported == 1); // once per name, not once per rider or per container
}

namespace
{
// a lethal variant of the fixture: the grab does 400 SLASH, rider and bystander leave the game when they die (DestroyDie), and with `killsSelf` the grab's
// area takes in the grabber too (RadiusDamageAffects SELF, with ALLIES because the source is its own ally for the relationship mask and without NOT_SIMILAR,
// which skips an ally of the source's own template; RW 0x90D77C), whose 300 health and DestroyDie make the same fire kill it
std::string lethalObjects(const std::string &grabLines, bool killsSelf)
{
	auto swap = [](std::string s, const std::string &from, const std::string &to, bool all) {
		for (size_t at = s.find(from); at != std::string::npos; at = all ? s.find(from, at + to.size()) : std::string::npos)
		{
			s.replace(at, from.size(), to);
		}
		return s;
	};
	std::string s = objects(grabLines);
	s = swap(s, "    Damage = 40\n", "    Damage = 400\n", false);
	s = swap(s, "    MaxHealth = 100\n  End\n", "    MaxHealth = 100\n  End\n  Behavior = DestroyDie ModuleTag_Die\n  End\n", true);
	if (killsSelf)
	{
		s = swap(s, "  RadiusDamageAffects = ENEMIES NOT_SIMILAR\n", "  RadiusDamageAffects = ALLIES ENEMIES SELF\n", false);
		s = swap(s, "    MaxHealth = 1000\n  End\n", "    MaxHealth = 300\n  End\n  Behavior = DestroyDie ModuleTag_Die\n  End\n", false);
	}
	return s;
}
} // namespace

// onContaining can kill (the grab fire RW 0x86AA8A runs inside OpenContain::addToContain RW 0x8674C2 at RW 0x867530, before the rider is linked at RW 0x867543).
// GameLogic::destroyObject (RW 0x62BBAB) runs Object::onDestroy (RW 0x6902FA) at once, whose leave-the-container step reads + 0x27C, still null then, so the
// destroyed rider would stay in the list and the next frames (TransportContain::update -> letRidersUpgradeWeaponSet) would read a freed object. The port
// finishes the entry and runs the exit: the rider is not in the list, and the frames after the destroy list freed it run clean.
TEST_CASE("PORT-62 lethal grab: a rider the grab kills does not stay in the transport (RW 0x86AA8A inside RW 0x8674C2)")
{
	const WeaponLookup lookup;
	CombatWorld w(lethalObjects(std::string(kGrab) + kFireYes, false).c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *container = w.unit("Port62Grabber", 'A', 200.0f, 200.0f);
	Object *rider = w.unit("Port62Rider", 'B', 210.0f, 200.0f);
	REQUIRE(container != nullptr);
	REQUIRE(rider != nullptr);
	TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
	REQUIRE(transport != nullptr);
	const ObjectID riderId = rider->getID();
	CHECK(transport->addToContain(rider)); // accepted: the grab kills it inside the add
	CHECK(rider->isDestroyed());
	CHECK(rider->getContainedBy() == nullptr);
	CHECK(transport->getContainCount() == 0);
	w.frames(3); // the destroy list frees the rider; TransportContain::update walks its riders every frame
	CHECK(w.byId(riderId) == nullptr);
	CHECK(transport->getContainCount() == 0);
	CHECK_FALSE(container->isEffectivelyDead());
}

// The same fire killing the grabber (RadiusDamageAffects SELF): with EjectPassengersOnDeath the container's own death (OpenContain::onDie RW 0x867120 ->
// removeAllContained) takes the rider out inside onContaining, so the rider must not be linked to the dead container afterwards. With
// FireGrabWeaponOnVictim = Yes the grab kills the rider too; with No the rider is shielded from the fire (RW 0x86AA6B), and the container's death kills it
// instead (TransportContain::killRidersWhoAreNotFreeToExit RW 0x86B05F -> kill(8, 0) RW 0x86B0CB: the fixture's rider has no AI, so it is not free to exit)
// before removeAllContained takes it out. Either way the rider leaves the list once and is never linked to the dead grabber.
TEST_CASE("PORT-62 lethal grab: a grab that kills the transport leaves no rider pointing at it (RW 0x86AA8A, RW 0x867120)")
{
	for (const bool riderDies : { false, true })
	{
		CAPTURE(riderDies);
		const WeaponLookup lookup;
		CombatWorld w(lethalObjects(std::string(kGrab) + (riderDies ? kFireYes : kFireNo) + "    EjectPassengersOnDeath = Yes\n", true).c_str());
		w.combat().setAutoAcquireEnabled(false);
		Object *container = w.unit("Port62Grabber", 'A', 200.0f, 200.0f);
		Object *rider = w.unit("Port62Rider", 'B', 210.0f, 200.0f);
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		TransportContain *transport = dynamic_cast<TransportContain *>(container->getContain());
		REQUIRE(transport != nullptr);
		const ObjectID riderId = rider->getID();
		CHECK(transport->addToContain(rider));
		CHECK(container->isDestroyed());
		CHECK(rider->getContainedBy() == nullptr);
		CHECK(transport->getContainCount() == 0);
		CHECK(rider->isDestroyed());
		w.frames(3); // the destroy list frees both
		CHECK(w.byId(riderId) == nullptr);
	}
}
