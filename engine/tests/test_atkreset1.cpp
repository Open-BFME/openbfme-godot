// OpenBFME. ATKRESET-1 tests: every attack command ends the current weapon's FiringDuration lock (Weapon + 0x20 = 0) after its state change, as RotWK's
// privateAttackObject (RW 0x66BE77), privateForceAttackObject (RW 0x66C01A / 0x66C01E) and privateAttackPosition (RW 0x66E04E) do (BFME2 decomp
// AIUpdateInterfaceAttackObjectCommands.cpp:136 / :152, both editions); AIAttackFireWeaponState::onExit (RW 0x74C8BE) writes the weapon status back and
// clears IS_FIRING_WEAPON, IGNORING_STEALTH and END_FIRE_STATE. Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Combat/CombatNames.h"

using namespace combattest;

namespace
{
// LockBow: a FiringDuration much longer than its DelayBetweenShots (0), as retail's horse archer bows (1500 ms) and Sauron's mace (3700 ms); SlowBow: the
// shot delay is the longer one
const char kLockObjects[] =
	"Weapon LockBow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  FiringDuration = 6000\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon SlowBow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 6000\n"
	"  FiringDuration = 2000\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object LockArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY LockBow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object SlowArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SlowBow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

struct LockWorld : CombatWorld
{
	LockWorld() : CombatWorld(kLockObjects) { combat().setAutoAcquireEnabled(false); }
	int status(Object *o) { return o->getWeapons()->weaponStatus(*o->getWeapons()->currentWeapon()); }
	Weapon *bow(Object *o) { return o->getWeapons()->currentWeapon(); }
	// the unit fires its first shot at `first`, one more frame runs: the weapon is inside its FiringDuration lock
	void firstShot(Object *archer, Object *first)
	{
		REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(first, CMD_FROM_PLAYER));
		runUntil([&] { return bow(archer)->lastFireFrame() != 0; }, 200);
		REQUIRE(bow(archer)->lastFireFrame() == 5); // the aim takes frames 3 and 4
		frames(1);
		REQUIRE(status(archer) == WEAPON_FIRING);
	}
	// frames until the weapon fires again
	int framesToNextShot(Object *archer)
	{
		const std::uint32_t last = bow(archer)->lastFireFrame();
		return runUntil([&] { return bow(archer)->lastFireFrame() != last; }, 1000);
	}
};
} // namespace

TEST_CASE("atkreset1 aiAttackObject: a new attack order ends the old shot's FiringDuration lock (RW 0x66BE77); the unit fires at the new target at once")
{
	LockWorld w;
	Object *archer = w.unit("LockArcher", 'A', 300, 300);
	Object *first = w.unit("Dummy", 'B', 400, 300);
	Object *second = w.unit("Dummy", 'B', 300, 400);
	w.frames(2);
	w.firstShot(archer, first);
	Weapon *bow = w.bow(archer);
	const std::uint32_t lockEnd = bow->whenFiringEnds();
	REQUIRE(lockEnd == 35); // the shot at frame 5 + FiringDuration 30 frames (6000 ms at the test logic's 5 frames per second)
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(second, CMD_FROM_PLAYER));
	CHECK(bow->whenFiringEnds() == 0);
	CHECK(w.status(archer) == WEAPON_READY_TO_FIRE);
	// the second shot waits only for the turn and the aim, well before the old lock would have ended
	w.framesToNextShot(archer);
	CHECK(bow->lastFireFrame() == 10); // the old rule waited for the lock: frame 37
	CHECK(archer->getAIUpdateInterface()->currentVictimId() == second->getID());
}

TEST_CASE("atkreset1 aiForceAttackObject and aiAttackPosition end the lock too (RW 0x66C01A / 0x66E04E) and put the order's shot limit on the weapon")
{
	SUBCASE("force attack")
	{
		LockWorld w;
		Object *archer = w.unit("LockArcher", 'A', 300, 300);
		Object *first = w.unit("Dummy", 'B', 400, 300);
		Object *second = w.unit("Dummy", 'B', 300, 400);
		w.frames(2);
		w.firstShot(archer, first);
		REQUIRE(archer->getAIUpdateInterface()->aiForceAttackObject(second, CMD_FROM_PLAYER, 3));
		CHECK(archer->getWeapons()->currentWeapon()->whenFiringEnds() == 0);
		CHECK(archer->getWeapons()->currentWeapon()->maxShotCount() == 3); // RW 0x66C01E
		CHECK(w.status(archer) == WEAPON_READY_TO_FIRE);
	}
	SUBCASE("attack position")
	{
		LockWorld w;
		Object *archer = w.unit("LockArcher", 'A', 300, 300);
		Object *first = w.unit("Dummy", 'B', 400, 300);
		w.frames(2);
		w.firstShot(archer, first);
		const Coord3D spot{ 300.0f, 420.0f, w.logic->getGroundHeight(300.0f, 420.0f) };
		REQUIRE(archer->getAIUpdateInterface()->aiAttackPosition(spot, 2, CMD_FROM_PLAYER));
		CHECK(archer->getWeapons()->currentWeapon()->whenFiringEnds() == 0);
		CHECK(archer->getWeapons()->currentWeapon()->maxShotCount() == 2);
		CHECK(w.status(archer) == WEAPON_READY_TO_FIRE);
	}
}

TEST_CASE("atkreset1: a shot delay longer than the FiringDuration still holds after a new order (only + 0x20 is cleared)")
{
	LockWorld w;
	Object *archer = w.unit("SlowArcher", 'A', 300, 300);
	Object *first = w.unit("Dummy", 'B', 400, 300);
	Object *second = w.unit("Dummy", 'B', 300, 400);
	w.frames(2);
	w.firstShot(archer, first);
	const std::uint32_t canFireAgain = w.bow(archer)->whenWeCanFireAgain();
	REQUIRE(canFireAgain == 35); // frame 5 + the 30-frame delay
	REQUIRE(w.bow(archer)->whenFiringEnds() == 15); // frame 5 + the 10-frame FiringDuration
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(second, CMD_FROM_PLAYER));
	CHECK(w.bow(archer)->whenFiringEnds() == 0);
	CHECK(w.status(archer) == WEAPON_BETWEEN_FIRING_SHOTS);
	// the next shot comes when the delay ends, not earlier
	w.framesToNextShot(archer);
	CHECK(w.bow(archer)->lastFireFrame() == 36);
}

TEST_CASE("atkreset1 AIAttackFireWeaponState::onExit (RW 0x74C8BE): a cancelled pre-attack ends and the status is written back; the fire statuses are cleared")
{
	LockWorld w;
	Object *archer = w.unit("Archer", 'A', 300, 300); // BowWeapon: PreAttackDelay 400 PER_POSITION
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_PLAYER));
	Weapon *bow = archer->getWeapons()->currentWeapon();
	w.runUntil([&] { return w.status(archer) == WEAPON_PRE_ATTACK; }, 200);
	REQUIRE(w.status(archer) == WEAPON_PRE_ATTACK);
	REQUIRE(bow->lastFireFrame() == 0);
	REQUIRE(bow->storedStatus() != WEAPON_READY_TO_FIRE); // PRE_ATTACK is never stored (getStatus answers it invalid): the cache is the old value
	const unsigned isFiring = (unsigned)CombatNames::status("IS_FIRING_WEAPON");
	const unsigned ignoringStealth = (unsigned)CombatNames::status("IGNORING_STEALTH");
	const unsigned endFire = (unsigned)CombatNames::status("END_FIRE_STATE");
	archer->setStatus(ignoringStealth, true);
	archer->setStatus(endFire, true);
	archer->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	CHECK(bow->whenPreAttackFinished() == 0);
	CHECK(bow->storedStatus() == WEAPON_READY_TO_FIRE); // the second write-back
	CHECK(w.status(archer) == WEAPON_READY_TO_FIRE);
	CHECK_FALSE(archer->testStatus(isFiring));
	CHECK_FALSE(archer->testStatus(ignoringStealth));
	CHECK_FALSE(archer->testStatus(endFire));
}

