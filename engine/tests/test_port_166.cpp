// OpenBFME. PORT-166 (fork issue 395, P-64 "Erkenbrand's Time to Strike: the cooldown starts on the order, no charge, slow afterwards"): what
// fireCurrentWeapon does with the ordered slot lock after the shot that starts the reload. The RotWK mechanism this lane read:
//   * the order (MSG_DO_WEAPON_AT_OBJECT, RW 0x77A34C): AIGroup::setWeaponLockForGroup(slot, LOCKED_TEMPORARILY) (RW 0x76FAB7), and the attack state
//     records the locked slot on enter (m_lockedSlotOnEnter);
//   * the shot (privateFireWeapon, RW 0x6CEF6D, called from fireCurrentWeapon RW 0x6920AD): the release of the lock (releaseWeaponLock = RW 0x68DF11(1)
//     at RW 0x69212B) is gated ONLY by that call's reloaded answer (`cmp [ebp-1], bl; je` at RW 0x692122). The port carried a ZH-era
//     `|| IdleAfterFiringDelay == -1` clause: for any weapon with the field unset (default 0xFFFFFFFF, Weapon.h) the shot released the ordered lock at
//     once;
//   * the next update: chooseWeapon re-picks a ready SECONDARY while the fired slot counts its DelayBetweenShots, and the attack leaves at the
//     "the locked weapon changed" gate (RW 0x751482 .. RW 0x751494) - one shot, then the attack is dead (fork issue 395's "the charge does not happen").
// ClipSize 0 is unlimited (0x7FFFFFFF at reloadWithBonus), so a clipless weapon never answers reloaded and keeps its ordered lock until a state
// releases it. The scenario is synthetic (no game files): a two-weapon hero orders PRIMARY at a victim in range; the shot starts the reload
// (WEAPON_BETWEEN_FIRING_SHOTS) and does not release the ordered lock - that release is fireCurrentWeapon's and it is gated on reloaded. The
// neighbour is the same order with a single weapon: nothing can flip to, so its two shots and the kill are the same before and after the fix.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"

#include <cstdio>

using namespace combattest;

namespace
{
const char kP166Locomotor[] =
	"Locomotor P166FootLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 500\n"
	"  TurnTimeDamaged = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n";

const char kP166Weapons[] =
	// the ordered slot: the mod's ErkenbrandChargeWeapon shape (AttackRange 300, DelayBetweenShots 40000, FiringDuration 0, PER_SHOT) with the reload
	// window shortened to 2000 ms (10 logic frames) the test can afford; no ClipSize: the clipless weapon never answers reloaded (RW 0x692122)
	"Weapon P166PrimaryWeapon\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  AttackRange = 300.0\n"
	"  DelayBetweenShots = 2000\n"
	"  FiringDuration = 0\n"
	"  PreAttackType = PER_SHOT\n"
	"  AcceptableAimDelta = 15\n"
	"  DamageNugget\n"
	"    Damage = 10.0\n"
	"    Radius = 1.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// the flip target: a ready weapon the next chooseWeapon picks while the fired one reloads (the fork chain's SECONDARY)
	"Weapon P166SecondaryWeapon\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  AttackRange = 300.0\n"
	"  DelayBetweenShots = 2000\n"
	"  FiringDuration = 0\n"
	"  PreAttackType = PER_SHOT\n"
	"  AcceptableAimDelta = 15\n"
	"  DamageNugget\n"
	"    Damage = 3.0\n"
	"    Radius = 1.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

#define P166_BODY(weapon) \
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n" \
	"  VisionRange = 400\n" \
	"  Geometry = CYLINDER\n" \
	"  GeometryMajorRadius = 8\n" \
	"  GeometryMinorRadius = 8\n" \
	"  GeometryHeight = 20\n" \
	"  ArmorSet\n" \
	"    Conditions = None\n" \
	"    Armor = PlainArmor\n" \
	"  End\n" \
	"  WeaponSet\n" \
	"    Conditions = None\n" \
	"    Weapon = PRIMARY " weapon "\n" \
	"  End\n" \
	"  Body = ActiveBody ModuleTag_Body\n" \
	"    MaxHealth = 600\n" \
	"  End\n" \
	"  Behavior = AIUpdateInterface ModuleTag_AI\n" \
	"    AutoAcquireEnemiesWhenIdle = No\n" \
	"  End\n" \
	"  LocomotorSet\n" \
	"    Locomotor = P166FootLoco\n" \
	"    Condition = SET_NORMAL\n" \
	"    Speed = 50\n" \
	"  End\n"

const char kP166Objects[] =
	// the flip case: PRIMARY reloads after the first shot and SECONDARY is ready to be picked while it does
	"Object P166Hero\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 400\n"
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
	"    Weapon = PRIMARY P166PrimaryWeapon\n"
	"    Weapon = SECONDARY P166SecondaryWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 600\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = P166FootLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	// the neighbour: one weapon, nothing to flip to
	"Object P166SoloHero\n" P166_BODY("P166PrimaryWeapon") "End\n"
	// MaxHealth 15: two Damage 10 shots end it (15 -> 5 -> dead), one does not
	"Object P166Victim\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 15\n"
	"  End\n"
	"End\n";

struct LockWorld : CombatWorld
{
	LockWorld() : CombatWorld()
	{
		combat().setAutoAcquireEnabled(false);
		WeaponCommands::registerHandlers(dispatcher); // MSG_DO_WEAPON_AT_OBJECT (the FIRE_WEAPON button's), which MoveWorld does not register
		std::string err = w.load(kP166Locomotor, INI_LOAD_OVERWRITE, "p166loco.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kP166Weapons, INI_LOAD_OVERWRITE, "p166weapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kP166Objects, INI_LOAD_OVERWRITE, "p166.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	// the player's game path: the selection and MSG_DO_WEAPON_AT_OBJECT (RW 0x77A34C), the press's slot and shots as the ControlBar sends them
	void orderPrimaryAt(Object *who, Object *victim)
	{
		const int alice = playerIndex("Alice");
		select(alice, { who });
		GameMessage m(MSG_DO_WEAPON_AT_OBJECT, alice);
		m.appendIntegerArgument(PRIMARY_WEAPON);
		m.appendObjectIDArgument(victim->getID());
		m.appendIntegerArgument(0x7FFFFFFF);
		send(m);
	}
	unsigned state(Object *o) { return o->getAIUpdateInterface()->currentStateId(); }
	bool locked(Object *o) { return o->getWeapons()->isCurWeaponLocked(); }
	int weaponStatus(Object *o)
	{
		ObjectWeapons *ow = o->getWeapons();
		Weapon *w = ow ? ow->currentWeapon() : nullptr;
		return w ? (int)ow->weaponStatus(*w) : -1;
	}
	// runs frames(1) until the victim's health is `target`, at most `limit` frames; the frame it landed on (-1: never)
	int runUntilHealth(Object *victim, float target, int limit)
	{
		for (int f = 0; f < limit; ++f)
		{
			frames(1);
			if (health(victim) == target)
			{
				return f;
			}
		}
		return -1;
	}
};
} // namespace

// the victim's MaxHealth 15 shows the shots (15 -> 5 -> dead); one frame after the order the dispatch has run
TEST_CASE("PORT-166 the ordered slot lock survives the shot that starts the reload (RW 0x6920AD .. 0x69212B)")
{
	LockWorld w;
	Object *hero = w.unit("P166Hero", 'A', 300, 300);
	Object *victim = w.unit("P166Victim", 'B', 450, 300); // 150 apart, both weapons' AttackRange 300: the shot fires without a walk
	w.orderPrimaryAt(hero, victim);
	const int shotFrame = w.runUntilHealth(victim, 5.0f, 20); // 15 - the first shot's Damage 10
	std::printf("  info: PORT-166 shot frame %d, status %d, locked %d, state %u\n", shotFrame, w.weaponStatus(hero), w.locked(hero) ? 1 : 0,
		w.state(hero));
	REQUIRE(shotFrame >= 0); // tolerance: the dispatch frame can shift by one on another compiler's float rounding (PER_SHOT in range)
	CHECK(w.health(victim) == 5.0f);                                // exactly one PRIMARY hit landed
	CHECK(w.weaponStatus(hero) == (int)WEAPON_BETWEEN_FIRING_SHOTS); // the reload window runs from the shot (RW 0x6920AD), not from the order
	// the mechanism under test: the shot does NOT release the ordered lock. The port's ZH-era `|| IdleAfterFiringDelay == -1` released it here
	// (the field defaults to 0xFFFFFFFF), and that release is what let the next chooseWeapon flip the slot into the "locked weapon changed" gate
	CHECK(w.locked(hero));
	// what happens after this frame is another mechanism's: AIWaitUntilFinishedFiringState::onExit (AIAttack.cpp) releases the same lock when the
	// firing wait ends - a ZH-derived line with no RW address of its own. Whether RW's generic attack keeps the lock across the reload is unverified
	// (INFERENCE, S-7326's neighbour question): the fork's charge path never enters that state and keeps the lock to its own exit (RW 0x74D8D2),
	// which is the chain this fix is the vanilla part of. So only the shot's own release - fireCurrentWeapon's, the RW-read one - is asserted here.
	w.frames(60); // run whatever the attack does out before teardown (a mid-attack teardown is a harness hazard, not this mechanism's)
}

TEST_CASE("PORT-166 neighbour: a single-weapon order is the same before and after - nothing can flip to")
{
	LockWorld w;
	Object *hero = w.unit("P166SoloHero", 'A', 300, 300);
	Object *victim = w.unit("P166Victim", 'B', 450, 300);
	w.orderPrimaryAt(hero, victim);
	const int shotFrame = w.runUntilHealth(victim, 5.0f, 20);
	std::printf("  info: PORT-166 neighbour: shot frame %d, status %d, state %u\n", shotFrame, w.weaponStatus(hero), w.state(hero));
	REQUIRE(shotFrame >= 0); // tolerance: the dispatch frame can shift by one on another compiler's float rounding (PER_SHOT in range)
	CHECK(w.health(victim) == 5.0f);
	CHECK(w.weaponStatus(hero) == (int)WEAPON_BETWEEN_FIRING_SHOTS); // the reload at the shot, the same as the fix's case
	w.frames(3);
	CHECK(w.state(hero) == (unsigned)AI_ATTACK_OBJECT); // with one weapon the released lock flips nothing: the attack was never in question
	const int deathFrame = w.runUntilHealth(victim, 0.0f, 60);
	std::printf("  info: PORT-166 neighbour: death frame %d\n", deathFrame);
	CHECK(deathFrame >= 0); // tolerance: 60 is the catch-all; the exact count is the path's (the reload is 10 frames)
}
