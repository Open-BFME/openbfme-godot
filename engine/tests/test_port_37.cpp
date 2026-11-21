// OpenBFME. PORT-37 (issue fork issue 122) tests: the shape of a DamageNugget's area damage, RotWK 2.01 RW 0x90DEF0 at RW 0x90DF35 .. 0x90DF77.
//
// Synthetic data only (combattest::CombatWorld's engine INI reader and field tables, no retail / retail / mod file, value or byte). Every test name
// begins PORT-37 so the lane's narrow filter is -tc="PORT-37*".
//
// What RW does (see PROCESS.md and issue fork issue 122 for the addresses):
//   * RW 0x90DF0C .. 0x90DF1F: the radius is max(1.0, Radius + 0x150) (RW 0xBD1908 is 1.0).
//   * RW 0x90DF35: CylinderAOE (+0x1C0). Set (RW 0x90DF44, edi = 1): the query asks the partition for distance type 1, RW 0xA3AE50 — the 2D bounding
//     circle, dx and dy of the centres, z ignored, so the area is a CYLINDER: a victim straight above or below the centre is inside it, and only the
//     height caps (DamageMaxHeight RW 0x90E1AC, DamageMaxHeightAboveTerrain RW 0x90E1DF) limit it in z.
//   * Clear (RW 0x90DF3C, edi = 3): distance type 3, RW 0xA3AEB0 — the 3D bounding sphere: sqrt(dx^2 + dy^2 + (dz + the geometry's centre height + 0x20)^2)
//     less the geometry's bounding sphere radius (+0x14). A victim above the sphere's reach is spared.
//   * Clear with max(1, Radius) >= 10.0 (RW 0x90DF4E against RW 0xBD83D8) and the source (RW 0x449681) not a BIG_MONSTER (template + 0x11D bit 3,
//     KindOf 171) that is also a TRANSPORT (+ 0x10A bit 5, KindOf 21) and has a contain (Object + 0x258): RW 0x90DF75 moves 4, RW 0xA3C7D0 — the
//     victim's OWN shapes against a static sphere of the radius at the centre, so the victim's bounding sphere no longer pads the reach.
//
// Victim geometry of the fixture below: Geometry CYLINDER, major 8, minor 8, height 20: the geometry's centre height is 10 (RW 0xB4E370, RW 0xAD2040's
// z extent) and its bounding sphere radius is sqrt(8^2 + 10^2) = 12.806248 (RW 0xAD2770), both computed by the port's own geometry code (S-320: ZH's
// formulas). The victim lies on one team, the source on the other, so the relationship mask of the nugget allows it.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"

#include <cstdio>

using namespace combattest;

namespace
{
// Port37Victim: the victim above (CYLINDER 8 / 8 / height 20, 100 health, no damage reduction: NoArmor is the fixture's DEFAULT 100%). Port37Beast: the
// exception of RW 0x90DF57 .. 0x90DF73 — KindOf BIG_MONSTER (171, + 0x11D bit 3) and TRANSPORT (21, + 0x10A bit 5) with a contain (+ 0x258).
// Port37Sphere / Port37Cylinder both have Radius 5 (below the 10 that turns RW to the geometry overlap); Port37Wide has Radius 10 and no CylinderAOE.
const char kPort37[] =
	"Object Port37Victim\n"
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
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port37Beast\n"
	"  KindOf = HORDE BIG_MONSTER TRANSPORT SELECTABLE CAN_ATTACK\n"
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
	"    RankInfo = RankNumber:1 UnitType:MemberOfHorde Position:X:50 Y:0\n"
	"    InitialPayload = MemberOfHorde 1\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	"Weapon Port37Sphere\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 5\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon Port37Cylinder\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 5\n"
	"    CylinderAOE = Yes\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon Port37Wide\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 10\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

const WeaponTemplate &weapon(const char *name)
{
	const WeaponTemplate *w = TheWeaponStore->findWeaponTemplate(name);
	REQUIRE_MESSAGE(w != nullptr, name);
	return *w;
}

float health(const Object &o)
{
	return o.getBodyModule()->getHealth();
}

// the victim's z: RW reads the position's z in every distance type (RW 0xA3A7D0 / RW 0xA3AEB0) and the victim's shapes in type 4
void lift(Object &o, float z)
{
	Coord3D p = *o.getPosition();
	p.z = z;
	o.setPosition(&p);
}

// one delivery of `weaponName` with `source` as the owner, at `center` (the position path: RW 0x90DEF0's slot 6)
void deliver(CombatWorld &w, const Object &source, const char *weaponName, const Coord3D &center, Object *victim)
{
	DeliverNuggets(*w.logic, source.getID(), weapon(weaponName), WeaponBonus{}, nullptr, &center, false, nullptr);
	std::printf("  info: %s at (%.1f, %.1f, %.1f): the victim at z %.1f has %.1f health\n", weaponName, center.x, center.y, center.z,
	            victim ? victim->getPosition()->z : 0.0f, victim ? health(*victim) : 0.0f);
}
} // namespace

// The cylinder branch (RW 0x90DF44 -> RW 0xA3AE50): a victim 50 above the centre is outside the 3D bounding sphere of a Radius 5 nugget (its bounding
// sphere is 12.806 and the geometry's centre height 10, so the distance is 60 - 12.806 = 47.19 against a radius of 5) but inside the cylinder, whose 2D
// distance is sqrt(2^2) = 2 against 5 + 8. Before the port read CylinderAOE both deliveries hit, because every nugget used the 2D circle.
TEST_CASE("PORT-37 cylinder: CylinderAOE reaches a victim 50 above the centre, the same nugget without it spares the victim (RW 0x90DF35 .. 0x90DF47)")
{
	for (const bool cylinder : { false, true })
	{
		CAPTURE(cylinder);
		CombatWorld w(kPort37);
		Object *source = w.unit("Dummy", 'A', 200.0f, 200.0f);
		Object *victim = w.unit("Port37Victim", 'B', 500.0f, 500.0f);
		REQUIRE(source != nullptr);
		REQUIRE(victim != nullptr);
		w.frames(2);
		lift(*victim, 50.0f);
		const Coord3D center{ 502.0f, 500.0f, 0.0f }; // 2 to the side of the victim: 2D distance 2, inside the cylinder's 13
		deliver(w, *source, cylinder ? "Port37Cylinder" : "Port37Sphere", center, victim);
		CHECK(health(*victim) == (cylinder ? 90.0f : 100.0f));
	}
}

// The 3D bounding sphere branch (RW 0x90DF3C -> RW 0xA3AEB0), the neighbouring behaviour the cylinder branch must not disturb: a victim at the centre's
// own height is inside the sphere of the same Radius 5 nugget with and without the flag (2D distance 2 is under 5 + 8; the 3D distance
// sqrt(2^2 + 10^2) = 10.198 less the 12.806 bounding sphere is negative), so the ordinary hit stays a hit
TEST_CASE("PORT-37 sphere: a victim at the centre's height is inside both shapes, with and without CylinderAOE (RW 0x90DF3C -> RW 0xA3AEB0)")
{
	for (const bool cylinder : { false, true })
	{
		CAPTURE(cylinder);
		CombatWorld w(kPort37);
		Object *source = w.unit("Dummy", 'A', 200.0f, 200.0f);
		Object *ground = w.unit("Port37Victim", 'B', 500.0f, 500.0f);
		REQUIRE(source != nullptr);
		REQUIRE(ground != nullptr);
		w.frames(2);
		const Coord3D center{ 502.0f, 500.0f, 0.0f };
		deliver(w, *source, cylinder ? "Port37Cylinder" : "Port37Sphere", center, ground);
		CHECK(health(*ground) == 90.0f);
	}
}

// The geometry overlap branch (RW 0x90DF4E .. RW 0x90DF75 -> RW 0xA3C7D0) at Radius 10, no CylinderAOE: the victim's own shapes (a cylinder from its z up to
// z + 20, radius 8) must touch a sphere of radius 10 at the centre. At z 11.5 the shape starts above that sphere and the victim is spared, although the 3D
// bounding sphere of RW 0xA3AEB0 would reach it (dz + 10 = 21.5, less 12.806 = 8.69 against 10); at z 6 the shape overlaps the sphere and the victim takes
// the hit, so the pair pins the shapes and not the bounding sphere (margin 1.5 and 4 on either side of the branch's z = 10 boundary)
TEST_CASE("PORT-37 geometry overlap: from Radius 10 a plain nugget tests the victim's own shapes, not its bounding sphere")
{
	struct Case
	{
		float z;
		float expected;
	};
	for (const Case &c : { Case{ 11.5f, 100.0f }, Case{ 6.0f, 90.0f } })
	{
		CAPTURE(c.z);
		CombatWorld w(kPort37);
		Object *source = w.unit("Dummy", 'A', 200.0f, 200.0f);
		Object *victim = w.unit("Port37Victim", 'B', 500.0f, 500.0f);
		REQUIRE(source != nullptr);
		REQUIRE(victim != nullptr);
		w.frames(2);
		lift(*victim, c.z);
		const Coord3D center{ 502.0f, 500.0f, 0.0f };
		deliver(w, *source, "Port37Wide", center, victim);
		CHECK(health(*victim) == c.expected);
	}
}

// The exception of RW 0x90DF57 .. 0x90DF73: a source that is a BIG_MONSTER and a TRANSPORT and has a contain keeps the 3D bounding sphere at Radius 10, so
// the victim at z 11.5 that the geometry overlap spares (the test above) is hit here. Without the three conditions read (every source taking the geometry
// overlap at Radius 10) this victim would be spared too
TEST_CASE("PORT-37 geometry overlap: a BIG_MONSTER TRANSPORT source with a contain keeps the 3D bounding sphere (RW 0x90DF57 .. 0x90DF73)")
{
	for (const bool transport : { false, true })
	{
		CAPTURE(transport);
		CombatWorld w(kPort37);
		Object *source = w.unit(transport ? "Port37Beast" : "Dummy", 'A', 200.0f, 200.0f);
		Object *victim = w.unit("Port37Victim", 'B', 500.0f, 500.0f);
		REQUIRE(source != nullptr);
		REQUIRE(victim != nullptr);
		w.frames(2);
		lift(*victim, 11.5f);
		const Coord3D center{ 502.0f, 500.0f, 0.0f };
		deliver(w, *source, "Port37Wide", center, victim);
		CHECK(health(*victim) == (transport ? 90.0f : 100.0f));
	}
}
