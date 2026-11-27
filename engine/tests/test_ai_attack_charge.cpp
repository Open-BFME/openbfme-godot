// OpenBFME. PORT-133 (fork issue 335, P-64 "Erkenbrand's Time to Strike does not work"): RotWK's attack kind 8, the charge attack of a weapon with
// CanFireWhileCharging (AIAttackState::onEnter RW 0x74D01B .. 0x74D02A -> the machine RW 0x74523F with its one state AIChargeTargetState, vtable RW 0xC29708:
// onEnter RW 0x75491F, update RW 0x7518D8, onExit RW 0x74D892). Synthetic data, no game files: the objects below copy the shape of the mod's chain - the
// command's weapon (PRIMARY, CanFireWhileCharging, a token DamageNugget, a long DelayBetweenShots as the recharge) and the FireWeaponUpdate with
// ChargingModeTrigger whose weapon does the damage - with numbers of the tests' own. The expectations are RotWK's instructions (the addresses at each check),
// tested through what the game shows: the model condition CHARGING (0x84), where the unit runs, the weapon lock, the victims' health.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackCharge.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/WeaponState.h"

#include <cstdio>
#include <cstring>

using namespace combattest;

namespace
{
const char kP133Weapons[] =
	// the command's weapon: AttackRange 60, the recharge 60000 ms = 300 frames (5 logic frames per second)
	"Weapon P133ChargeWeapon\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  AttackRange = 60.0\n"
	"  DelayBetweenShots = 60000\n"
	"  FiringDuration = 0\n"
	"  PreAttackType = PER_SHOT\n"
	"  CanFireWhileCharging = Yes\n"
	"  AcceptableAimDelta = 15\n"
	"  DamageNugget\n"
	"    Damage = 1.0\n"
	"    Radius = 1.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// the same weapon without the flag: the neighbouring attack kinds
	"Weapon P133PlainWeapon\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  AttackRange = 60.0\n"
	"  DelayBetweenShots = 2000\n"
	"  FiringDuration = 0\n"
	"  PreAttackType = PER_SHOT\n"
	"  AcceptableAimDelta = 15\n"
	"  DamageNugget\n"
	"    Damage = 1.0\n"
	"    Radius = 1.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// what hurts on the way (FireWeaponUpdate, ChargingModeTrigger)
	"Weapon P133ChargeEffect\n"
	"  RadiusDamageAffects = ENEMIES NEUTRALS\n"
	"  AttackRange = 20.0\n"
	"  DelayBetweenShots = 500\n"
	"  DamageNugget\n"
	"    Damage = 50\n"
	"    Radius = 15.0\n"
	"    DamageType = CRUSH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

#define P133_BODY(weapon) \
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n" \
	"  VisionRange = 300\n" \
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
	"  Behavior = FireWeaponUpdate ModuleTag_Charge\n" \
	"    FireWeaponNugget\n" \
	"      WeaponName = P133ChargeEffect\n" \
	"      FireDelay = 0\n" \
	"      OneShot = No\n" \
	"    End\n" \
	"    ChargingModeTrigger = Yes\n" \
	"    AliveOnly = Yes\n" \
	"  End\n" \
	"  LocomotorSet\n" \
	"    Locomotor = WalkerLoco\n" \
	"    Condition = SET_NORMAL\n" \
	"    Speed = 55\n" \
	"  End\n"

const char kP133Objects[] =
	"Object P133Charger\n" P133_BODY("P133ChargeWeapon") "End\n"
	"Object P133Plain\n" P133_BODY("P133PlainWeapon") "End\n"
	"Object P133Victim\n"
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
	"    MaxHealth = 100000\n"
	"  End\n"
	"End\n";

struct ChargeWorld : CombatWorld
{
	ChargeWorld() : CombatWorld()
	{
		combat().setAutoAcquireEnabled(false);
		SpellEffectModules::registerAll(w.modules); // FireWeaponUpdate
		std::string err = w.load(kP133Weapons, INI_LOAD_OVERWRITE, "p133weapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kP133Objects, INI_LOAD_OVERWRITE, "p133.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	static bool charging(const Object *o) { return o->testModelCondition(CombatNames::modelCondition("CHARGING")); }
	static float health(const Object *o) { return o->getBodyModule()->getHealth(); }
	unsigned state(Object *o) { return o->getAIUpdateInterface()->currentStateId(); }
	// what the FIRE_WEAPON command does (MSG_DO_WEAPON_AT_OBJECT, RW 0x77A394 .. 0x77A3C1): the slot locked LOCKED_TEMPORARILY, then the attack order
	void order(Object *who, Object *victim)
	{
		REQUIRE(ObjectWeapons::setObjectWeaponLock(*who, PRIMARY_WEAPON, LOCKED_TEMPORARILY));
		REQUIRE(who->getAIUpdateInterface()->aiAttackObject(victim, CMD_FROM_PLAYER));
	}
	// runs until the attack state is left (at most `limit` frames); the frame CHARGING first showed in (-1: never) and the largest x reached
	struct Run
	{
		int frames = 0, chargeFrame = -1, chargeEndFrame = -1;
		float chargeX = 0.0f, maxX = 0.0f, distAtCharge = 0.0f;
		bool lockedBeforeCharge = false;
	};
	Run run(Object *who, Object *victim, int limit)
	{
		Run r;
		r.maxX = who->getPosition()->x;
		for (; r.frames < limit; ++r.frames)
		{
			frames(1);
			const bool c = charging(who);
			if (r.frames == 0 && !c)
			{
				r.lockedBeforeCharge = who->getWeapons()->isCurWeaponLocked();
			}
			if (c && r.chargeFrame < 0)
			{
				r.chargeFrame = r.frames;
				r.chargeX = who->getPosition()->x;
				r.distAtCharge = victim->getPosition()->x - who->getPosition()->x;
			}
			if (!c && r.chargeFrame >= 0 && r.chargeEndFrame < 0)
			{
				r.chargeEndFrame = r.frames;
			}
			r.maxX = who->getPosition()->x > r.maxX ? who->getPosition()->x : r.maxX;
			if (state(who) != (unsigned)AI_ATTACK_OBJECT)
			{
				break;
			}
		}
		return r;
	}
};
} // namespace

// RW 0x74D01B .. 0x74D02A (kind 8) -> RW 0x7518D8: out of the weapon's range the unit only walks to the victim (no CHARGING); within AttackRange (RW 0x74D8EF)
// with the weapon ready (RW 0x7519CB) CHARGING goes on (RW 0x7519E0), the goal becomes the point 50 behind the victim (RW 0x748348) and the unit runs through
TEST_CASE("PORT-133 charge: a CanFireWhileCharging weapon walks into range, shows CHARGING and runs on 50 past the victim (RW 0x7518D8 / 0x748348)")
{
	ChargeWorld w;
	Object *charger = w.unit("P133Charger", 'A', 300, 300);
	Object *victim = w.unit("P133Victim", 'B', 500, 300);
	w.frames(2);
	REQUIRE_FALSE(ChargeWorld::charging(charger));
	w.order(charger, victim);
	const ChargeWorld::Run r = w.run(charger, victim, 600);
	std::printf("  info: PORT-133 charge: CHARGING from frame %d (x %.1f, %.1f from the victim) to frame %d, attack over after %d frames, max x %.1f, final x %.1f\n",
		r.chargeFrame, r.chargeX, r.distAtCharge, r.chargeEndFrame, r.frames, r.maxX, charger->getPosition()->x);
	REQUIRE(r.chargeFrame > 0); // tolerance: RW gives no frame number, only "not at the order" (200 away, AttackRange 60); the frame is the locomotor's
	CHECK(r.distAtCharge < 60.0f); // tolerance: RW 0x74D98E is itself the bound (strictly inside AttackRange 60)
	CHECK(r.distAtCharge > 40.0f); // tolerance: the first frame inside the range; one locomotor step (under 20 per frame here) is the port's movement, not this state's
	CHECK(r.lockedBeforeCharge);         // RW 0x754961: setWeaponLock(PRIMARY, LOCKED_TEMPORARILY) (the shot itself releases it, RW 0x692127)
	CHECK(r.frames < 600); // tolerance: 600 is the loop's limit: the attack ended by itself (the frame count is the locomotor's)
	CHECK(r.maxX > 540.0f); // tolerance: RW 0x748348 puts the goal at x 550 (victim 500 + 50); the internal move stops within a locomotor step of it
	CHECK(r.maxX < 556.0f); // tolerance: the same goal from the other side
	// onExit RW 0x74D892: CHARGING off, the temporary lock released (RW 0x68DF11(1))
	CHECK_FALSE(ChargeWorld::charging(charger));
	CHECK_FALSE(charger->getWeapons()->isCurWeaponLocked());
	CHECK(w.state(charger) != (unsigned)AI_ATTACK_OBJECT);
}

// the mod's chain: the damage is the FireWeaponUpdate's (ChargingModeTrigger), which fires only while CHARGING shows; RW 0x74D9CE fires the command's weapon at
// the unit's own position (RW 0x6920AD(object + 0x38)), which starts its reload
TEST_CASE("PORT-133 charge: the victims on the way are hurt by the ChargingModeTrigger weapon only once CHARGING shows; the command's weapon reloads (RW 0x74D9CE)")
{
	ChargeWorld w;
	Object *charger = w.unit("P133Charger", 'A', 300, 300);
	Object *victim = w.unit("P133Victim", 'B', 500, 300);
	Object *far = w.unit("P133Victim", 'B', 500, 420); // 120 off the run: outside the effect's radius
	w.frames(2);
	const float h0 = ChargeWorld::health(victim), f0 = ChargeWorld::health(far);
	w.order(charger, victim);
	float healthAtCharge = -1.0f;
	int statusAtCharge = -1;
	for (int f = 0; f < 600 && w.state(charger) == (unsigned)AI_ATTACK_OBJECT; ++f)
	{
		const bool before = ChargeWorld::charging(charger);
		const float hBefore = ChargeWorld::health(victim);
		w.frames(1);
		if (!before && ChargeWorld::charging(charger))
		{
			healthAtCharge = hBefore;
			ObjectWeapons *ow = charger->getWeapons();
			statusAtCharge = (int)ow->weaponStatus(*ow->currentWeapon());
		}
	}
	std::printf("  info: PORT-133 effect: victim %.0f -> %.0f (at the charge %.0f), far %.0f -> %.0f, weapon status at the charge %d\n", h0, ChargeWorld::health(victim),
		healthAtCharge, f0, ChargeWorld::health(far), statusAtCharge);
	CHECK(healthAtCharge == h0);                    // the walk to the victim hurts nobody
	CHECK(ChargeWorld::health(victim) <= h0 - 50.0f); // tolerance: run over: one or more shots of the effect weapon (50 each, every 500 ms while in its radius; how many is the run's timing)
	CHECK(ChargeWorld::health(far) == f0);
	CHECK(statusAtCharge == (int)WEAPON_BETWEEN_FIRING_SHOTS); // RW 0x6920AD: the shot was made, the weapon is between shots
}

// the recharge: RW 0x7519CB asks the weapon's status (RW 0x6CD142) to be READY_TO_FIRE: an order inside DelayBetweenShots only walks (no CHARGING, nobody
// hurt); an order once the weapon is ready charges again
TEST_CASE("PORT-133 charge: an order inside DelayBetweenShots does not charge; after the reload the next order charges again (RW 0x7519CB)")
{
	ChargeWorld w;
	Object *charger = w.unit("P133Charger", 'A', 300, 300);
	Object *victim = w.unit("P133Victim", 'B', 400, 300);
	Object *second = w.unit("P133Victim", 'B', 450, 480);
	w.frames(2);
	w.order(charger, victim);
	const ChargeWorld::Run first = w.run(charger, victim, 600);
	REQUIRE_FALSE(first.chargeFrame == -1); // -1: CHARGING never showed
	REQUIRE_FALSE(first.frames == 600); // 600: the loop's limit
	ObjectWeapons *ow = charger->getWeapons();
	Weapon *weapon = ow->weaponInSlot(PRIMARY_WEAPON);
	REQUIRE(weapon);
	REQUIRE(ow->weaponStatus(*weapon) != WEAPON_READY_TO_FIRE); // DelayBetweenShots 60000 ms = 300 frames, the run took far less
	// the second order, at once, at another victim 180 away
	const float s0 = ChargeWorld::health(second);
	w.order(charger, second);
	bool chargedEarly = false;
	int notReady = 0;
	for (; notReady < 100; ++notReady) // long enough to walk the 180 to the second victim several times over
	{
		w.frames(1);
		chargedEarly = chargedEarly || ChargeWorld::charging(charger);
	}
	CHECK_FALSE(chargedEarly);
	CHECK(ChargeWorld::health(second) == s0);
	REQUIRE(ow->weaponStatus(*weapon) != WEAPON_READY_TO_FIRE); // still inside DelayBetweenShots
	int reload = 0;
	for (; reload < 2000 && ow->weaponStatus(*weapon) != WEAPON_READY_TO_FIRE; ++reload)
	{
		w.frames(1);
	}
	REQUIRE(ow->weaponStatus(*weapon) == WEAPON_READY_TO_FIRE);
	// the weapon is ready: a new order at the first victim charges
	charger->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	w.order(charger, victim);
	const ChargeWorld::Run again = w.run(charger, victim, 600);
	std::printf("  info: PORT-133 recharge: first charge at frame %d, over after %d; %d + %d more frames of reload without CHARGING; the next order charges at frame %d\n",
		first.chargeFrame, first.frames, notReady, reload, again.chargeFrame);
	CHECK_FALSE(again.chargeFrame == -1); // -1: CHARGING never showed
	CHECK_FALSE(ChargeWorld::charging(charger));
}

// the neighbour: the same unit and weapon without CanFireWhileCharging keeps its attack kind (RW 0x74D021 `je`): no CHARGING, no run past the victim
TEST_CASE("PORT-133 neighbour: the same weapon without CanFireWhileCharging never shows CHARGING and does not run past the victim (RW 0x74D021)")
{
	ChargeWorld w;
	Object *plain = w.unit("P133Plain", 'A', 300, 300);
	Object *victim = w.unit("P133Victim", 'B', 500, 300);
	w.frames(2);
	const float h0 = ChargeWorld::health(victim);
	w.order(plain, victim);
	bool charged = false;
	float maxX = 0.0f;
	for (int f = 0; f < 300; ++f)
	{
		w.frames(1);
		charged = charged || ChargeWorld::charging(plain);
		maxX = plain->getPosition()->x > maxX ? plain->getPosition()->x : maxX;
	}
	std::printf("  info: PORT-133 neighbour: max x %.1f, victim %.0f -> %.0f, state %u\n", maxX, h0, ChargeWorld::health(victim), w.state(plain));
	CHECK_FALSE(charged);
	CHECK(maxX < 500.0f); // tolerance: it stops at its range (60 before the victim at 500) instead of running through; where exactly is the attack approach's
	CHECK(ChargeWorld::health(victim) < h0); // tolerance: it strikes with the weapon itself (Damage 1 per shot; the shot count is the generic attack's)
	CHECK(ChargeWorld::health(victim) > h0 - 50.0f); // tolerance: never the charge effect (50 per shot)
}

TEST_CASE("PORT-133 xfer: AIChargeTargetState v1/v2, locomotor range validation, and full snapshot round-trip")
{
	ChargeWorld w;
	Object *charger = w.unit("P133Charger", 'A', 300, 300);
	Object *victim = w.unit("P133Victim", 'B', 500, 300);
	Object *plain = w.unit("P133Plain", 'A', 300, 400);
	const ObjectID chargerId = charger->getID();
	const ObjectID plainId = plain->getID();
	w.frames(2);
	w.order(charger, victim);
	w.order(plain, victim);
	w.frames(3);

	auto *chargeAttack = dynamic_cast<AIAttackState *>(charger->getAIUpdateInterface()->stateMachine().currentState());
	REQUIRE(chargeAttack);
	REQUIRE(chargeAttack->attackMachine());
	REQUIRE(chargeAttack->attackMachine()->currentStateId() == CHARGE_TARGET);

	// Full snapshot round-trip preserves both MADE_CHARGE and existing MADE_GENERIC attacks with identical state hash
	const auto hashBefore = w.logic->computeStateHash();
	std::vector<std::uint8_t> snap;
	std::string err;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(snap, &err), err);
	w.frames(2);
	REQUIRE_MESSAGE(w.logic->loadSnapshot(snap, &err), err);
	CHECK(w.logic->computeStateHash() == hashBefore);
	charger = w.logic->findObjectByID(chargerId);
	plain = w.logic->findObjectByID(plainId);
	REQUIRE(charger);
	REQUIRE(plain);
	chargeAttack = dynamic_cast<AIAttackState *>(charger->getAIUpdateInterface()->stateMachine().currentState());
	REQUIRE(chargeAttack);
	REQUIRE(chargeAttack->attackMachine());
	CHECK(chargeAttack->attackMachine()->currentStateId() == CHARGE_TARGET);
	auto *plainAttack = dynamic_cast<AIAttackState *>(plain->getAIUpdateInterface()->stateMachine().currentState());
	REQUIRE(plainAttack);
	REQUIRE(plainAttack->attackMachine());

	// Direct AIChargeTargetState::xfer checks: v1 payload (no m_setBeforeCharge), invalid locomotor set, and version 3 refusal
	AIUpdateInterface &ai = *charger->getAIUpdateInterface();
	auto m = makeChargeAttackMachine(ai);
	auto *st = dynamic_cast<AIChargeTargetState *>(m->findState(CHARGE_TARGET));
	REQUIRE(st);

	// Craft a v1 payload: version=1, goal=(10,20,30), done=false, count=1, hasMove=false
	XferSave v1Save;
	{
		XferVersion v{ 1, 1 };
		v1Save.xferVersion(v);
		float gx = 10.0f, gy = 20.0f, gz = 30.0f;
		v1Save.xferReal(&gx);
		v1Save.xferReal(&gy);
		v1Save.xferReal(&gz);
		bool done = false;
		int count = 1;
		bool hasMove = false;
		v1Save.xferBool(&done);
		v1Save.xferInt(&count);
		v1Save.xferBool(&hasMove);
	}
	XferLoad v1Load(v1Save.data());
	st->xfer(v1Load);
	CHECK(v1Load.atEnd());
	CHECK(st->goal().x == 10.0f);
	CHECK(st->goal().y == 20.0f);
	CHECK(st->goal().z == 30.0f);

	// v2 round-trip and out-of-range m_setBeforeCharge refusal
	XferSave v2Save;
	st->xfer(v2Save);
	std::vector<std::uint8_t> bytes = v2Save.data();
	REQUIRE(bytes.front() == 2);
	{
		XferLoad okLoad(bytes);
		st->xfer(okLoad);
		CHECK(okLoad.atEnd());
	}
	// Version 3 must throw XferException
	{
		std::vector<std::uint8_t> badVer = bytes;
		badVer.front() = 3;
		XferLoad badVerLoad(badVer);
		CHECK_THROWS_AS(st->xfer(badVerLoad), XferException);
	}
	// Out-of-range m_setBeforeCharge (-2 and LOCOMOTORSET_COUNT) must throw XferException
	// Layout: 1 (ver) + 12 (goal) + 1 (done) + 4 (count) = offset 18 for m_setBeforeCharge
	for (std::int32_t badSet : { -2, (std::int32_t)LOCOMOTORSET_COUNT })
	{
		std::vector<std::uint8_t> bad = bytes;
		std::memcpy(bad.data() + 18, &badSet, sizeof(badSet));
		XferLoad badLoad(bad);
		CHECK_THROWS_AS(st->xfer(badLoad), XferException);
	}
}
