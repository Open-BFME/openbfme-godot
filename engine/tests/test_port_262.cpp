// OpenBFME unit tests (lane MODULES-1 / fork issue 610, PORT-262): burning. FlammableUpdate's BurnContained (RW 0x8905A2 .. 0x890654) and the
// neighbouring behaviour of the two classes the nightly function map reported as uncited (FlammableUpdate, FireWeaponWhenDamagedBehavior).
// Synthetic INI only: no retail file is read and no ROTWK_INSTALL is needed. Every expectation names the RotWK address it comes from.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/FireWeaponWhenDamagedBehavior.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Object/Object.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
// three carriers of the same FlammableUpdate data: one with BurnContained, one without, and one whose structure holds the module the map's
// second class (FireWeaponWhenDamagedBehavior) rides on. AflameDamageAmount 10 keeps the halving of RW 0x8905C4 visible (10 / 2 = 5).
const char kBurnObjects[] =
	"Object BurnCarrier\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Burn\n"
	"    AflameDuration = 20000\n"
	"    AflameDamageDelay = 2000\n"
	"    AflameDamageAmount = 10\n"
	"    FlameDamageLimit = 20\n"
	"    FlameDamageExpiration = 5000\n"
	"    BurnContained = Yes\n"
	"  End\n"
	"End\n"
	"Object BurnCarrierPlain\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Burn\n"
	"    AflameDuration = 20000\n"
	"    AflameDamageDelay = 2000\n"
	"    AflameDamageAmount = 10\n"
	"    FlameDamageLimit = 20\n"
	"    FlameDamageExpiration = 5000\n"
	"  End\n"
	"End\n"
	"Object BurnRider\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryMinorRadius = 6\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object FwwdHolder\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = FireWeaponWhenDamagedBehavior ModuleTag_Fwwd\n"
	"    StartsActive = Yes\n"
	"    DamageTypes = NONE +FLAME\n"
	"    DamageAmount = 1\n"
	"  End\n"
	"End\n";

FlammableUpdate *flammableOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (FlammableUpdate *f = dynamic_cast<FlammableUpdate *>(m.get()))
		{
			return f;
		}
	}
	return nullptr;
}

FireWeaponWhenDamagedBehavior *fwwdOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (FireWeaponWhenDamagedBehavior *f = dynamic_cast<FireWeaponWhenDamagedBehavior *>(m.get()))
		{
			return f;
		}
	}
	return nullptr;
}

float healthOf(Object *o)
{
	auto *body = dynamic_cast<ActiveBody *>(o->getBodyModule());
	REQUIRE(body);
	return body->getHealth();
}

// a hit that empties a FlameDamageLimit of 20 (RW 0x890587) and so ignites the object (RW 0x8901AB)
DamageInfo flameHit(float amount)
{
	DamageInfo hit;
	hit.m_input.m_damageType = DAMAGE_FLAME; // RW 0x890560: the data's DamageType 6 = FLAME, the module only hears its own type
	hit.m_input.m_amount = amount;
	return hit;
}
} // namespace

// RW 0x8905A2 (data + 0x39 BurnContained) / RW 0x8905AC (Object + 0x258, the contain): after the ignition every passenger takes
// max(AflameDamageAmount / 2, 1) FLAME damage (RW 0x890608 D + 0x10 = 6) with the death type BURNED (RW 0x890606 D + 0x1C = 3) from the burning
// object itself (RW 0x8905E3 D + 0x08), the list being the contain's slot 0x118 (RW 0x89060F) walked at RW 0x89062E .. 0x890646 and each entry hit
// through RW 0x698E7D (RW 0x89063E).
TEST_CASE("PORT-262 flammable: BurnContained gives every passenger max(AflameDamageAmount / 2, 1) FLAME damage at the ignition (RW 0x8905A2)")
{
	CombatWorld w(kBurnObjects);
	Object *carrier = w.unit("BurnCarrier", 'A', 300, 300);
	Object *rider = w.unit("BurnRider", 'A', 320, 300);
	REQUIRE(carrier->getContain());
	REQUIRE(carrier->getContain()->addToContain(rider));
	CHECK(carrier->getContain()->getContainCount() == 1u);
	FlammableUpdate *f = flammableOf(carrier);
	REQUIRE(f);
	REQUIRE(f->status() == FlammableUpdate::FS_NORMAL);
	const float riderBefore = healthOf(rider);
	const float carrierBefore = healthOf(carrier);

	DamageInfo ignition = flameHit(100.0f);
	carrier->attemptDamage(ignition);

	// the ignition itself: RW 0x8901AB turned the status and the AFLAME model condition on
	REQUIRE(f->status() == FlammableUpdate::FS_AFLAME);
	CHECK(carrier->testStatus(10)); // ObjectStatus AFLAME (RW 0x8901CE)
	// the passenger: 10 / 2 = 5 (RW 0x8905C4 .. 0x8905DF), from the carrier, death type BURNED
	CHECK(riderBefore == 100.0f); // the rider's MaxHealth
	CHECK(healthOf(rider) == 95.0f);
	// the carrier keeps what the ignition hit did to it and nothing more at the ignition (the aflame ticks come later, RW 0x88FDFA)
	CHECK(carrierBefore == 500.0f); // the carrier's MaxHealth
	CHECK(healthOf(carrier) == 400.0f);
}

// the neighbour of the branch above: without BurnContained the contain is not touched (the ignition asks its slot 0x88(2) instead, RW 0x890656,
// which the port reports) and the object's own aflame ticks still take AflameDamageAmount after every AflameDamageDelay frames (RW 0x88FDFA).
TEST_CASE("PORT-262 flammable: without BurnContained the passengers are left alone; the aflame ticks still damage the burning object (RW 0x88FDFA)")
{
	CombatWorld w(kBurnObjects);
	Object *carrier = w.unit("BurnCarrierPlain", 'A', 300, 300);
	Object *rider = w.unit("BurnRider", 'A', 320, 300);
	REQUIRE(carrier->getContain());
	REQUIRE(carrier->getContain()->addToContain(rider));
	FlammableUpdate *f = flammableOf(carrier);
	REQUIRE(f);
	const float riderBefore = healthOf(rider);

	DamageInfo ignition = flameHit(100.0f);
	carrier->attemptDamage(ignition);
	REQUIRE(f->status() == FlammableUpdate::FS_AFLAME);
	CHECK(riderBefore == 100.0f);
	CHECK(healthOf(rider) == 100.0f); // BurnContained off: no passenger damage
	CHECK(healthOf(carrier) == 400.0f);
	for (int i = 0; i < 12 && f->aflameDamageTicks() == 0; ++i) // AflameDamageDelay 10
	{
		w.frames(1);
	}
	CHECK(f->status() == FlammableUpdate::FS_AFLAME);
	REQUIRE(f->aflameDamageTicks() == 1u);
	// AflameDamageAmount 10 for the one tick (RW 0x88FDFA -> RW 0x698E7D with the module's own data)
	CHECK(healthOf(carrier) == 390.0f);
	CHECK(healthOf(rider) == 100.0f);
}

// the map's second class: its six uncited functions are lifetime, name and interface plumbing (see FireWeaponWhenDamagedBehavior.h), so what is
// held here is the class's own behaviour around them: the mux is not permanent (RW 0x9188EB, mux slot 7), its snapshot survives a save and load
// (RW 0x88588B: UpdateModule RW 0x850C60, the mux RW 0x8D27E8, then the weapons), and a hit whose type the data's DamageTypes does not hold never
// reaches a weapon (RW 0x885CF4: `bt` on 1 << (type - 1)).
TEST_CASE("PORT-262 fwwd: the module's snapshot round-trips and a hit of an unlisted damage type fires nothing (RW 0x88588B, RW 0x885CD5)")
{
	CombatWorld w(kBurnObjects);
	Object *o = w.unit("FwwdHolder", 'A', 300, 300);
	FireWeaponWhenDamagedBehavior *m = fwwdOf(o);
	REQUIRE(m);
	CHECK_FALSE(m->isPermanent());

	StateHasher before;
	m->crc(before);
	const unsigned long long shots = m->reactionShots() + m->attackerShots() + m->continuousShots();
	XferSave saved;
	m->xfer(saved);
	w.frames(3);
	{
		XferLoad load(saved.data());
		m->xfer(load);
	}
	StateHasher after;
	m->crc(after);
	CHECK(after.value() == before.value());
	CHECK(m->reactionShots() + m->attackerShots() + m->continuousShots() == shots);

	DamageInfo other; // DamageTypes = FLAME: every other type is refused before a state is picked
	other.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	other.m_input.m_amount = 50.0f;
	o->attemptDamage(other);
	CHECK(m->reactionShots() == 0u);
	CHECK(m->attackerShots() == 0u);

	XferSave cut;
	m->xfer(cut);
	std::vector<std::uint8_t> truncated = cut.data();
	truncated.pop_back();
	XferLoad bad(truncated);
	CHECK_THROWS_AS(m->xfer(bad), XferException);
}
