// OpenBFME. COMBAT-1 tests: the combat state is part of the deterministic world hash. Each case changes ONE field group of an otherwise identical world and requires the hash
// to change (a mutation test, like test_logic_hash.cpp): a field the hash misses would let two peers of a lockstep game desync silently. And two identical worlds fight
// identically: the same hash in every frame. Synthetic data, no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"

using namespace combattest;

namespace
{
struct HashWorld : CombatWorld
{
	Object *sword = nullptr, *dummy = nullptr, *archer = nullptr, *hordeA = nullptr, *hordeB = nullptr;
	HashWorld()
	{
		combat().setAutoAcquireEnabled(false);
		sword = unit("Swordsman", 'A', 300, 300);
		dummy = unit("Dummy", 'B', 330, 300);
		archer = unit("Archer", 'A', 200, 600);
		hordeA = unit("SwordHorde", 'A', 600, 600);
		hordeB = unit("SwordHorde", 'B', 720, 600);
		frames(14);
	}
};

#define MUTATE(NAME, STATEMENT)                                                                                                                           \
	do                                                                                                                                                    \
	{                                                                                                                                                     \
		HashWorld WORLD;                                                                                                                                  \
		const std::uint32_t before = WORLD.hash();                                                                                                        \
		{                                                                                                                                                 \
			HashWorld &w = WORLD;                                                                                                                         \
			(void)w;                                                                                                                                      \
			STATEMENT;                                                                                                                                    \
		}                                                                                                                                                 \
		CHECK_MESSAGE(WORLD.hash() != before, NAME);                                                                                                      \
	} while (0)
} // namespace

TEST_CASE("combat hash: two identical worlds have the same hash and an untouched world does not drift")
{
	HashWorld a, b;
	CHECK(a.hash() == b.hash());
	const std::uint32_t h = a.hash();
	CHECK(a.hash() == h);
}

TEST_CASE("combat hash: the object fields - the dead flag, the armor set flags, the weapon bonus conditions, the pending damage")
{
	MUTATE("dead flag", w.dummy->kill(DEATH_NORMAL));
	MUTATE("armor set flag", w.sword->setArmorSetFlag(CombatNames::armorSetBit("PLAYER_UPGRADE"), true));
	MUTATE("weapon bonus condition", w.sword->setWeaponBonusCondition(1, true));
	{
		DamageInfo d;
		d.m_input.m_amount = 10.0f;
		d.m_input.m_damageType = DAMAGE_SLASH;
		d.m_input.m_delay = 5.0f;
		MUTATE("a pending hit", w.dummy->attemptDamage(d));
	}
	{
		HashWorld a, b;
		DamageInfo d1, d2;
		d1.m_input.m_amount = 10.0f;
		d1.m_input.m_damageType = DAMAGE_SLASH;
		d1.m_input.m_delay = 5.0f;
		d2 = d1;
		d2.m_input.m_amount = 11.0f; // only the amount differs
		a.dummy->attemptDamage(d1);
		b.dummy->attemptDamage(d2);
		CHECK(a.hash() != b.hash());
		d2 = d1;
		d2.m_input.m_damageType = DAMAGE_PIERCE;
		HashWorld c;
		c.dummy->attemptDamage(d2);
		CHECK(a.hash() != c.hash());
		d2 = d1;
		d2.m_input.m_delay = 6.0f;
		HashWorld e;
		e.dummy->attemptDamage(d2);
		CHECK(a.hash() != e.hash());
	}
}

TEST_CASE("combat hash: the body - health, the damage scalar, the last damage")
{
	MUTATE("health", {
		DamageInfo d;
		d.m_input.m_amount = 10.0f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		w.dummy->attemptDamage(d);
	});
	MUTATE("damage scalar", dynamic_cast<ActiveBody *>(w.dummy->findModule("ActiveBody"))->setBodyDamageScalar(0.5f));
	{
		HashWorld a, b;
		DamageInfo d1;
		d1.m_input.m_amount = 10.0f;
		d1.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		d1.m_input.m_sourceID = a.sword->getID();
		DamageInfo d2 = d1;
		d2.m_input.m_sourceID = a.archer->getID(); // only the damager differs
		a.dummy->attemptDamage(d1);
		b.dummy->attemptDamage(d2);
		CHECK(a.hash() != b.hash());
	}
}

TEST_CASE("combat hash: the weapons - set flags, the lock, ammo and timers after a shot, the shot counters")
{
	MUTATE("weapon set flag", w.sword->getWeapons()->setWeaponSetFlag(CombatNames::weaponSetBit("PLAYER_UPGRADE"), true));
	MUTATE("weapon lock", w.sword->getWeapons()->setWeaponLock(PRIMARY_WEAPON, LOCKED_PERMANENTLY));
	MUTATE("a shot fired", {
		ObjectWeapons *ow = w.sword->getWeapons();
		ow->chooseBestWeaponForTarget(w.dummy, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER);
		ow->preFireCurrentWeapon(w.dummy, nullptr);
	});
	{
		HashWorld a, b;
		for (HashWorld *h : { &a, &b })
		{
			ObjectWeapons *ow = h->sword->getWeapons();
			ow->chooseBestWeaponForTarget(h->dummy, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER);
			ow->preFireCurrentWeapon(h->dummy, nullptr);
			h->frames(3);
		}
		CHECK(a.hash() == b.hash());
		a.sword->getWeapons()->fireCurrentWeapon(a.dummy, nullptr);
		CHECK(a.hash() != b.hash()); // the shot changed the weapon timers, the victim's health and the counters
	}
}

TEST_CASE("combat hash: the combat state - the idle scan switch, the counters, projectiles in flight")
{
	MUTATE("the idle scan switch", w.combat().setAutoAcquireEnabled(true));
	MUTATE("counters", w.combat().counters().unportedNuggets += 1);
	MUTATE("a projectile in flight", {
		// the archer stands about 327 from the dummy, beyond its AttackRange 200: RW 0x6CC915's range gate would drop the shot (lane PHYS-1); it steps within 150
		Coord3D near = *w.dummy->getPosition();
		near.y += 150.0f;
		w.archer->setPosition(&near);
		ObjectWeapons *ow = w.archer->getWeapons();
		ow->chooseBestWeaponForTarget(w.dummy, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER);
		ow->preFireCurrentWeapon(w.dummy, nullptr);
		w.frames(2);
		ow->fireCurrentWeapon(w.dummy, nullptr);
		CHECK(w.combat().inFlight() == 1);
	});
}

TEST_CASE("combat hash: the AI - the victim, the attack-move, the attack states")
{
	MUTATE("an attack order (victim and states)", w.sword->getAIUpdateInterface()->aiAttackObject(w.dummy, CMD_FROM_PLAYER));
	MUTATE("an attack-move", w.sword->getAIUpdateInterface()->armAttackMove(Coord3D{ 500.0f, 300.0f, 0.0f }));
	{
		HashWorld a, b;
		a.sword->getAIUpdateInterface()->armAttackMove(Coord3D{ 500.0f, 300.0f, 0.0f });
		b.sword->getAIUpdateInterface()->armAttackMove(Coord3D{ 500.0f, 301.0f, 0.0f });
		CHECK(a.hash() != b.hash()); // the goal of the march
	}
	{
		// two attacks that differ by one frame of progress through the attack machine
		HashWorld a, b;
		a.sword->getAIUpdateInterface()->aiAttackObject(a.dummy, CMD_FROM_PLAYER);
		b.sword->getAIUpdateInterface()->aiAttackObject(b.dummy, CMD_FROM_PLAYER);
		a.frames(1);
		b.frames(2);
		CHECK(a.hash() != b.hash());
		a.frames(1);
		CHECK(a.hash() == b.hash()); // the same attack after the same number of frames is the same state
	}
}

TEST_CASE("combat hash: the horde - the melee engagement, the cache of the readiness rule, the member records, the released ranks")
{
	MUTATE("the melee flag of the horde contain", w.hordeOf(w.hordeA)->setMeleeEngaged(true));
	MUTATE("an engaged target", dynamic_cast<HordeAIUpdate *>(w.hordeA->getAIUpdateInterface())->beginMelee(*w.hordeB));
	MUTATE("the readiness cache", dynamic_cast<HordeAIUpdate *>(w.hordeA->getAIUpdateInterface())->isMeleeTargetReady(*w.hordeB));
	{
		HashWorld a, b;
		dynamic_cast<HordeAIUpdate *>(a.hordeA->getAIUpdateInterface())->beginMelee(*a.hordeB);
		dynamic_cast<HordeAIUpdate *>(b.hordeA->getAIUpdateInterface())->beginMelee(*b.hordeB);
		CHECK(a.hash() == b.hash());
		dynamic_cast<HordeAIUpdate *>(a.hordeA->getAIUpdateInterface())->meleeTick(*a.hordeB);
		CHECK(a.hash() != b.hash()); // the member records and the counters of the melee behaviour
	}
}

TEST_CASE("combat hash: death - the slow death's timers and the dead AI")
{
	HashWorld a, b;
	const ObjectID swordId = a.sword->getID();
	a.sword->kill(DEATH_NORMAL);
	b.sword->kill(DEATH_NORMAL);
	a.frames(2);
	b.frames(4);
	CHECK(a.hash() != b.hash());
	// the slow death moves the corpse down: its position is part of the hash as well, the timers are checked here through the frame they destroy the object
	a.frames(30);
	CHECK(a.byId(swordId) == nullptr);
}

namespace
{
std::uint32_t moduleCrc(Object *o, const char *cls)
{
	UpdateModule *m = dynamic_cast<UpdateModule *>(o->findModule(cls));
	REQUIRE(m != nullptr);
	StateHasher h;
	m->crc(h);
	return h.value();
}
} // namespace

TEST_CASE("combat hash: SlowDeathBehavior's own fields - activation and the frames it plans")
{
	HashWorld a, b;
	const ObjectID swordId = a.sword->getID();
	const std::uint32_t before = moduleCrc(a.sword, "SlowDeathBehavior");
	a.sword->kill(DEATH_NORMAL);
	const std::uint32_t after = moduleCrc(a.sword, "SlowDeathBehavior");
	CHECK(after != before); // activated, with sink / midpoint / destruction frames
	b.frames(1);
	b.sword->kill(DEATH_NORMAL);
	CHECK(moduleCrc(b.sword, "SlowDeathBehavior") != after); // killed a frame later: the planned frames move
	// after the sink delay the module has passed its midpoint: another state
	const std::uint32_t planned = moduleCrc(a.sword, "SlowDeathBehavior");
	a.frames(6);
	if (Object *s = a.byId(swordId))
	{
		CHECK(moduleCrc(s, "SlowDeathBehavior") != planned);
	}
}

TEST_CASE("combat hash: the same fight twice gives the same hash in every frame")
{
	std::vector<std::uint32_t> run[2];
	for (int r = 0; r < 2; ++r)
	{
		CombatWorld w;
		Object *a = w.unit("SwordHorde", 'A', 300, 300);
		Object *b = w.unit("SwordHorde", 'B', 420, 300);
		Object *arch = w.unit("ArcherHorde", 'A', 300, 500);
		Object *tgt = w.unit("Dummy", 'B', 420, 500);
		w.frames(12);
		a->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
		arch->getAIUpdateInterface()->aiAttackObject(tgt, CMD_FROM_PLAYER);
		for (int f = 0; f < 300; ++f)
		{
			w.frames(1);
			run[r].push_back(w.hash());
		}
	}
	REQUIRE(run[0].size() == run[1].size());
	for (size_t i = 0; i < run[0].size(); ++i)
	{
		REQUIRE_MESSAGE(run[0][i] == run[1][i], "frame " << i);
	}
	CHECK(run[0].front() != run[0].back());
}
