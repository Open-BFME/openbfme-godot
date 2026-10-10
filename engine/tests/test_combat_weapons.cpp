// OpenBFME. COMBAT-1 tests: the weapons of an object (ObjectWeapons): the weapon set and its conditions, range, the pre-attack / firing / reload timing driven through
// WEAPON-1's Weapon state machine, the damage a shot deals, clips, locks, the stand-in projectile and the hook for PROJ-1. Synthetic data, no retail files.
#include "doctest.h"

#include <cmath>
#include "CombatTestUtil.h"
#include "GameLogic/Module/ProjectileModules.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/System/DOTManager.h"
#include "Common/StateHash.h"

using namespace combattest;

namespace
{
const char kUpgradable[] =
	"Object Upgradable\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = PLAYER_UPGRADE\n"
	"    Weapon = PRIMARY SlowSword\n"
	"    Weapon = SECONDARY BowWeapon\n"
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
	// lane DECOMP-1: two swords that both hurt, the stronger one SECONDARY; a victim whose armour stops every SLASH
	"Object TwoSwords\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"    Weapon = SECONDARY SlowSword\n"
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
	"Weapon SlashSplash\n"
	"  AttackRange = 11.5\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 50.0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Armor SlashProofArmor\n"
	"  Armor = DEFAULT 100%\n"
	"  Armor = SLASH 0%\n"
	"End\n"
	"Object SlashProof\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = SlashProofArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct Duel
{
	CombatWorld w;
	Object *a, *b;
	ObjectWeapons *ow;
	explicit Duel(const char *attacker = "Swordsman", const char *victim = "Dummy", float gap = 12.0f)
		: w(kUpgradable)
	{
		w.combat().setAutoAcquireEnabled(false); // the units are driven by hand
		a = w.unit(attacker, 'A', 300, 300);
		b = w.unit(victim, 'B', 300 + gap, 300);
		w.frames(2);
		ow = a->getWeapons();
		REQUIRE(ow != nullptr);
	}
	unsigned frame() { return w.logic->getFrame(); }
};
} // namespace

TEST_CASE("combat weapons: an object gets a weapon per slot of the best WeaponSet for its conditions; a condition change re-selects the set")
{
	Duel d("Upgradable", "Dummy", 12.0f);
	REQUIRE(d.ow->weaponInSlot(PRIMARY_WEAPON) != nullptr);
	CHECK(d.ow->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "SwordWeapon");
	CHECK(d.ow->weaponInSlot(SECONDARY_WEAPON) == nullptr);
	CHECK(d.ow->hasAnyWeapon());
	d.ow->setWeaponSetFlag(CombatNames::weaponSetBit("PLAYER_UPGRADE"), true);
	REQUIRE(d.ow->weaponInSlot(PRIMARY_WEAPON) != nullptr);
	CHECK(d.ow->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "SlowSword");
	REQUIRE(d.ow->weaponInSlot(SECONDARY_WEAPON) != nullptr);
	CHECK(d.ow->weaponInSlot(SECONDARY_WEAPON)->getTemplate()->getName() == "BowWeapon");
	d.ow->setWeaponSetFlag(CombatNames::weaponSetBit("PLAYER_UPGRADE"), false);
	CHECK(d.ow->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "SwordWeapon");
	CHECK(d.ow->weaponInSlot(SECONDARY_WEAPON) == nullptr);
}

TEST_CASE("combat weapons: an object that cannot have a weapon has no ObjectWeapons")
{
	Duel d;
	CHECK(d.b->getWeapons() == nullptr);
	CHECK_FALSE(d.b->hasAnyWeapon());
	CHECK(d.a->hasAnyWeapon());
}

TEST_CASE("combat weapons: melee timing - pre-attack 3 frames, the swing deals 10 SLASH through the 50% armour, the next shot is 5 frames after")
{
	Duel d;
	REQUIRE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	CHECK(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	CHECK(d.ow->isWithinAttackRange(*d.b)); // reach 11.5 + both radii, 12 apart
	const unsigned start = d.frame();
	d.ow->preFireCurrentWeapon(d.b, nullptr);
	CHECK(d.ow->currentStatus() == WEAPON_PRE_ATTACK);
	CHECK(d.ow->currentWeapon()->whenPreAttackFinished() == start + 3); // PreAttackDelay 600 ms = 3 frames
	int waited = 0;
	while (d.ow->currentStatus() == WEAPON_PRE_ATTACK && waited < 10)
	{
		d.w.frames(1);
		++waited;
	}
	CHECK(waited == 3);
	CHECK(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	const unsigned fired = d.frame();
	d.ow->fireCurrentWeapon(d.b, nullptr);
	CHECK(d.w.health(d.b) == 95.0f);                       // 10 * PlainArmor SLASH 50%
	CHECK(d.ow->currentWeapon()->lastFireFrame() == fired);
	CHECK(d.ow->currentWeapon()->whenWeCanFireAgain() == fired + 5); // DelayBetweenShots 1000 ms
	CHECK(d.ow->stats().shotsFired == 1);
	CHECK(d.ow->stats().shotsAtVictim == 1);
	CHECK(d.ow->currentStatus() != WEAPON_READY_TO_FIRE);
	int until = 0;
	while (d.ow->currentStatus() != WEAPON_READY_TO_FIRE && until < 20)
	{
		d.w.frames(1);
		++until;
	}
	CHECK(until == 5);
}

TEST_CASE("combat weapons: the range test - the reach of the weapon plus the bounding spheres of both units")
{
	Duel d("Swordsman", "Dummy", 40.0f);
	REQUIRE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	// SwordWeapon AttackRange 11.5; the AI's approach distance adds the two spheres of the 8 x 20 cylinders. Lane PATH-2: a cylinder's bounding sphere is
	// RW 0xAD2770's sqrt(8^2 + 10^2) (d + sqrt(r^2 + h^2) with no offset), not ZH's max(8, 10): each sphere is sqrt(164) - 10 larger than the first pin
	// (11.5 + 17.5) assumed
	const float sphereGrowth = 2.0f * ((float)std::sqrt(164.0) - 10.0f);
	CHECK(d.ow->attackDistance(*d.b) == doctest::Approx(11.5f + 17.5f + sphereGrowth));
	// the range test itself flips between 25 and 26 units centre to centre
	for (float gap = 10.0f; gap <= 25.0f; gap += 1.0f)
	{
		Coord3D p{ 300.0f + gap, 300.0f, 0.0f };
		d.b->setPosition(&p);
		CHECK_MESSAGE(d.ow->isWithinAttackRange(*d.b), "gap " << gap);
	}
	for (float gap = 26.0f; gap <= 40.0f; gap += 2.0f)
	{
		Coord3D p{ 300.0f + gap, 300.0f, 0.0f };
		d.b->setPosition(&p);
		CHECK_MESSAGE(!d.ow->isWithinAttackRange(*d.b), "gap " << gap);
	}
	CHECK_FALSE(d.ow->isTooClose(*d.b));
	// the bow (AttackRange 200): the approach distance is 217.5 plus the sphere growth above, the test passes up to 210 and fails from 225 on
	Object *archer = d.w.unit("Archer", 'A', 100, 100);
	Object *target = d.w.unit("Dummy", 'B', 300, 100);
	d.w.frames(2);
	ObjectWeapons *bow = archer->getWeapons();
	REQUIRE(bow->chooseBestWeaponForTarget(target, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	CHECK(bow->attackDistance(*target) == doctest::Approx(217.5f + sphereGrowth));
	Coord3D in{ 310.0f, 100.0f, 0.0f };
	target->setPosition(&in);
	CHECK(bow->isWithinAttackRange(*target));
	Coord3D out{ 325.0f, 100.0f, 0.0f };
	target->setPosition(&out);
	CHECK_FALSE(bow->isWithinAttackRange(*target));
}

TEST_CASE("combat weapons: a delayed damage nugget lands DelayTime later")
{
	Duel d("Slasher", "Dummy", 12.0f);
	REQUIRE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	d.ow->preFireCurrentWeapon(d.b, nullptr);
	d.w.frames(2); // PreAttackDelay 200 ms = 1 frame
	REQUIRE(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	d.ow->fireCurrentWeapon(d.b, nullptr);
	CHECK(d.w.health(d.b) == 100.0f);       // DelayTime 800 ms = 4 frames
	CHECK(d.b->pendingDamageCount() == 1);
	d.w.frames(5);
	CHECK(d.b->pendingDamageCount() == 0);
	CHECK(d.w.health(d.b) == 85.0f);        // 30 SLASH through the 50% armour of the target
}

TEST_CASE("combat weapons: a clip of two arrows - the arrow is a projectile object that flies its Bezier path and detonates one update after the last step, the clip empties and reloads")
{
	Duel d("Archer", "Dummy", 100.0f);
	REQUIRE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	CHECK(d.w.combat().usesRealLauncher());
	// a new clip weapon starts in its reload (ClipReloadTime 2000 ms = 10 frames from creation): it is ready on the ninth frame
	CHECK(d.ow->currentStatus() == WEAPON_RELOADING_CLIP);
	while (d.ow->currentStatus() != WEAPON_READY_TO_FIRE && d.frame() < 30)
	{
		d.w.frames(1);
	}
	REQUIRE(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	d.ow->preFireCurrentWeapon(d.b, nullptr);
	int waited = 0;
	while (d.ow->currentStatus() == WEAPON_PRE_ATTACK && waited < 10)
	{
		d.w.frames(1);
		++waited;
	}
	CHECK(waited == 2); // PreAttackDelay 400 ms
	d.ow->fireCurrentWeapon(d.b, nullptr);
	CHECK(d.ow->stats().projectilesLaunched == 1);
	CHECK(d.w.combat().inFlight() == 1);
	CHECK(d.w.health(d.b) == 100.0f); // still in the air
	const unsigned launched = d.frame();
	BezierProjectileBehavior *arrow = nullptr;
	for (Object *o = d.w.logic->getFirstObject(); o && !arrow; o = o->getNextObject())
	{
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if ((arrow = dynamic_cast<BezierProjectileBehavior *>(m.get())) != nullptr)
			{
				break;
			}
		}
	}
	REQUIRE(arrow != nullptr);
	// WeaponSpeed 100 per second = 20 per frame; the arrow is about 100 away plus its arc: segments = ceil(length / 20)
	const int segments = arrow->segments();
	CHECK(segments >= 5);
	CHECK(segments <= 7);
	CHECK(arrow->flightSpeed() == doctest::Approx(20.0f));
	int flight = 0;
	while (d.w.combat().inFlight() != 0 && flight < 30)
	{
		d.w.frames(1);
		++flight;
	}
	// the launch takes path points 0 and 1 at once (RW 0x85EF34, lane PROJ-2), then one update per further point, then the update that finds the path used up detonates
	CHECK(flight == segments - 1);
	CHECK(d.frame() == launched + (unsigned)flight);
	CHECK(d.w.health(d.b) == doctest::Approx(100.0f - 15.0f * 0.8f)); // BowWarhead 15 PIERCE through PIERCE 80%
	CHECK(d.w.combat().counters().projectilesLaunched == 1);
	CHECK(d.w.combat().counters().projectilesDetonated == 1);
	// the second arrow is due DelayBetweenShots (800 ms = 4 frames) after the first; the clip is then empty and reloads for ClipReloadTime (2000 ms = 10 frames)
	while (d.ow->currentStatus() != WEAPON_READY_TO_FIRE && d.frame() < launched + 40)
	{
		d.w.frames(1);
	}
	CHECK(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	d.ow->preFireCurrentWeapon(d.b, nullptr);
	d.w.frames(2);
	d.ow->fireCurrentWeapon(d.b, nullptr);
	CHECK(d.ow->currentStatus() == WEAPON_RELOADING_CLIP); // the clip is refilled at once, the weapon waits ClipReloadTime
	int reload = 0;
	const unsigned emptied = d.frame();
	while (d.ow->currentStatus() == WEAPON_RELOADING_CLIP && reload < 40)
	{
		d.w.frames(1);
		++reload;
	}
	CHECK(d.ow->currentWeapon()->whenWeCanFireAgain() >= emptied + 9u); // ClipReloadTime 2000 ms = 10 frames (WEAPON-1 counts from the frame in progress)
	CHECK(d.ow->currentWeapon()->whenWeCanFireAgain() <= emptied + 10u);
	CHECK(d.frame() >= emptied + 9u);
	CHECK(d.frame() <= emptied + 10u);
	CHECK(d.ow->currentWeapon()->ammoInClip() == 2);
}

TEST_CASE("combat weapons: a custom launcher replaces the real one and receives the shot (the hook stays clear)")
{
	struct Capture : ProjectileLauncher
	{
		std::vector<ProjectileShot> shots;
		void launch(GameLogic &, const ProjectileShot &s) override { shots.push_back(s); }
	};
	Duel d("Archer", "Dummy", 100.0f);
	Capture cap;
	d.w.combat().setProjectileLauncher(&cap);
	CHECK_FALSE(d.w.combat().usesRealLauncher());
	REQUIRE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	d.w.frames(10); // the new clip reloads first
	REQUIRE(d.ow->currentStatus() == WEAPON_READY_TO_FIRE);
	d.ow->preFireCurrentWeapon(d.b, nullptr);
	d.w.frames(2);
	d.ow->fireCurrentWeapon(d.b, nullptr);
	REQUIRE(cap.shots.size() == 1);
	CHECK(cap.shots[0].source == d.a->getID());
	CHECK(cap.shots[0].victim == d.b->getID());
	CHECK(cap.shots[0].weapon->getName() == "BowWeapon");
	REQUIRE(cap.shots[0].warhead != nullptr);
	CHECK(cap.shots[0].warhead->getName() == "BowWarhead");
	CHECK(d.w.combat().inFlight() == 0);
	d.w.frames(10);
	CHECK(d.w.health(d.b) == 100.0f); // the launcher owns the flight: nothing lands by itself
	d.w.combat().setProjectileLauncher(nullptr);
}

TEST_CASE("combat weapons: a weapon lock keeps the slot, releasing it gives the choice back")
{
	Duel d("Upgradable", "Dummy", 12.0f);
	d.ow->setWeaponSetFlag(CombatNames::weaponSetBit("PLAYER_UPGRADE"), true);
	CHECK_FALSE(d.ow->isCurWeaponLocked());
	CHECK(d.ow->setWeaponLock(SECONDARY_WEAPON, LOCKED_TEMPORARILY));
	CHECK(d.ow->isCurWeaponLocked());
	CHECK(d.ow->curSlot() == SECONDARY_WEAPON);
	d.ow->releaseWeaponLock(LOCKED_TEMPORARILY);
	CHECK_FALSE(d.ow->isCurWeaponLocked());
}

TEST_CASE("combat weapons: the best weapon for a victim - a weapon that does no damage to it is not chosen")
{
	Duel d;
	CHECK(d.ow->canAttackObject(*d.b, CMD_FROM_PLAYER, false));
	CHECK(d.ow->estimateWeaponDamage(*d.ow->currentWeapon(), *d.b) == doctest::Approx(5.0f));
}

// lane DECOMP-1 (S-1582): RotWK's PREFER_MOST_DAMAGE scores a weapon 1 when it can damage the victim (RW 0x6CDBF3) and 0 otherwise, not by its damage; slots are tried 5 .. 0
// with `<=`, so of two ready weapons that both hurt the lower slot wins. ZH's estimate picked the 30-damage SECONDARY over the 10-damage PRIMARY
TEST_CASE("combat weapons (DECOMP-1): two ready weapons that both hurt the victim - the lower slot wins (RW 0x6C8A4E scores canDamage, not the damage)")
{
	Duel d("TwoSwords", "Dummy", 12.0f);
	REQUIRE(d.ow->weaponInSlot(SECONDARY_WEAPON));
	CHECK(d.ow->estimateWeaponDamage(*d.ow->weaponInSlot(SECONDARY_WEAPON), *d.b) > d.ow->estimateWeaponDamage(*d.ow->weaponInSlot(PRIMARY_WEAPON), *d.b));
	CHECK(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_AI));
	CHECK(d.ow->curSlot() == PRIMARY_WEAPON);
	// PREFER_LONGEST_RANGE: the same reach (11.5): `>` keeps the first one met, SECONDARY (slots run 5 .. 0)
	CHECK(d.ow->chooseBestWeaponForTarget(d.b, PREFER_LONGEST_RANGE, CMD_FROM_AI));
	CHECK(d.ow->curSlot() == SECONDARY_WEAPON);
}

// lane DECOMP-1: DamageNugget's isApplicable (RW 0x90E855) asks the victim's estimate of the hit; a hit the armour reduces to 0 is not delivered at all (no attemptDamage:
// the body records no damage frame nor damager). The port delivered it as a 0-damage hit
TEST_CASE("combat weapons (DECOMP-1): a hit that the victim's armour reduces to 0 is not delivered, and the weapon cannot be chosen against it")
{
	Duel d("Swordsman", "SlashProof", 12.0f);
	ActiveBody *body = dynamic_cast<ActiveBody *>(d.b->getBodyModule());
	REQUIRE(body);
	const WeaponTemplate *sword = TheWeaponStore->findWeaponTemplate("SwordWeapon");
	REQUIRE(sword);
	DeliverNuggets(*d.w.logic, d.a->getID(), *sword, WeaponBonus{}, d.b, d.b->getPosition(), false, nullptr);
	CHECK(body->lastDamageFrame() == 0xFFFFFFFFu);
	CHECK(body->lastDamager() == INVALID_ID);
	CHECK_FALSE(d.ow->chooseBestWeaponForTarget(d.b, PREFER_MOST_DAMAGE, CMD_FROM_AI));
	// a hurting hit is delivered (the control)
	Duel e("Swordsman", "Dummy", 12.0f);
	ActiveBody *eb = dynamic_cast<ActiveBody *>(e.b->getBodyModule());
	REQUIRE(eb);
	DeliverNuggets(*e.w.logic, e.a->getID(), *sword, WeaponBonus{}, e.b, e.b->getPosition(), false, nullptr);
	CHECK(eb->lastDamager() == e.a->getID());
}

// lane DECOMP-1: RW 0x6C85CA: an AIRBORNE_TARGET infantry is ANTI_AIRBORNE_INFANTRY (0x20), not ground; a structure is GROUND | STRUCTURE
TEST_CASE("combat weapons (DECOMP-1): the victim anti mask of RW 0x6C85CA")
{
	Duel d;
	CHECK(ObjectWeapons::victimAntiMask(*d.b) == (unsigned)WEAPON_ANTI_GROUND);
	d.b->setStatus((unsigned)CombatNames::status("AIRBORNE_TARGET"), true);
	CHECK(ObjectWeapons::victimAntiMask(*d.b) == (unsigned)WEAPON_ANTI_AIRBORNE_INFANTRY);
}

// lane DECOMP-1 r3 (Sol's review): the selected victim's isApplicable (slot 1 RW 0x90E855) gates the WHOLE damage nugget before its radius (RW 0x6CCED3 .. 0x6CCEF5,
// RW 0x6CB7DD .. 0x6CB806): a Radius 50 SLASH hit aimed at a SLASH-proof victim hurts nobody, not even the soldier beside it. Before r3 the radius branch ran anyway
TEST_CASE("combat weapons (DECOMP-1 r3): an immune selected victim stops the whole damage nugget, its radius included")
{
	Duel d("Swordsman", "SlashProof", 12.0f);
	Object *neighbour = d.w.unit("Dummy", 'B', 312.0f, 320.0f);
	d.w.frames(1);
	const WeaponTemplate *splash = TheWeaponStore->findWeaponTemplate("SlashSplash");
	REQUIRE(splash);
	DeliverNuggets(*d.w.logic, d.a->getID(), *splash, WeaponBonus{}, d.b, d.b->getPosition(), false, nullptr);
	CHECK(d.w.health(neighbour) == 100.0f);
	ObjectWeapons::createAndFireTempWeaponAt(splash, d.a, *d.b); // the fireWeaponTemplate path
	CHECK(d.w.health(neighbour) == 100.0f);
	// the control: aimed at the hurtable neighbour, the radius reaches it
	DeliverNuggets(*d.w.logic, d.a->getID(), *splash, WeaponBonus{}, neighbour, neighbour->getPosition(), false, nullptr);
	CHECK(d.w.health(neighbour) < 100.0f);
}

// lane DECOMP-1 r3 (Sol's review): the delivery takes the FIRING weapon's slot (privateFireWeapon hands its Weapon + 0xC to fireWeaponTemplate, RW 0x6CF126): a
// temporary weapon is PRIMARY even while its source has chosen SECONDARY. Before r3 the shot inherited the source's selected slot
TEST_CASE("combat weapons (DECOMP-1 r3): a temporary weapon's shot carries PRIMARY, not the source's selected slot")
{
	struct Capture : ProjectileLauncher
	{
		std::vector<ProjectileShot> shots;
		void launch(GameLogic &, const ProjectileShot &s) override { shots.push_back(s); }
	};
	Duel d("Upgradable", "Dummy", 100.0f);
	d.ow->setWeaponSetFlag(CombatNames::weaponSetBit("PLAYER_UPGRADE"), true);
	REQUIRE(d.ow->setWeaponLock(SECONDARY_WEAPON, LOCKED_TEMPORARILY));
	REQUIRE(d.ow->curSlot() == SECONDARY_WEAPON);
	Capture cap;
	d.w.combat().setProjectileLauncher(&cap);
	ObjectWeapons::createAndFireTempWeaponAt(TheWeaponStore->findWeaponTemplate("BowWeapon"), d.a, *d.b);
	REQUIRE(cap.shots.size() == 1u);
	CHECK(cap.shots[0].slot == PRIMARY_WEAPON);
	d.w.combat().setProjectileLauncher(nullptr);
}

// lane DECOMP-1 r3 (Sol's review): every stored input of a damage-over-time record is state: ShouldPlayUnderAttackEva and FXTrigger change the hash
TEST_CASE("combat weapons (DECOMP-1 r3): the DOT record's ShouldPlayUnderAttackEva and FXTrigger are in the state hash")
{
	Duel d;
	auto hashWith = [&](bool eva, int fx) {
		DOTManager m(*d.w.logic);
		DOTManager::Record r;
		r.info.m_input.m_amount = 5.0f;
		r.info.m_input.m_shouldPlayUnderAttackEva = eva;
		r.info.m_input.m_fxTrigger = fx;
		r.endFrame = 100;
		r.interval = 5;
		r.nextFrame = 5;
		m.add(d.b->getID(), r);
		StateHasher h;
		m.crc(h);
		return h.value();
	};
	const std::uint32_t base = hashWith(true, 0);
	CHECK(hashWith(false, 0) != base);
	CHECK(hashWith(true, 1) != base);
	CHECK(hashWith(true, 0) == base);
}

// lane DECOMP-1 r4 (Sol's review): with no victim a damage nugget goes through the upgrade test and slot 6 at the position (RW 0x90DEF0, max(Radius, 1.0)): a Radius 0
// warhead that lands where its victim stands (a projectile without HitStoredTarget) hurts it. Before r4 the position path ran only for a non-zero Radius
TEST_CASE("combat weapons (DECOMP-1 r4): a Radius 0 warhead delivered at a position hurts what stands there")
{
	Duel d("Swordsman", "Dummy", 12.0f);
	const WeaponTemplate *sword = TheWeaponStore->findWeaponTemplate("SwordWeapon");
	REQUIRE(sword);
	const Coord3D at = *d.b->getPosition();
	DeliverNuggets(*d.w.logic, d.a->getID(), *sword, WeaponBonus{}, nullptr, &at, true, nullptr);
	CHECK(d.w.health(d.b) < 100.0f);
}
