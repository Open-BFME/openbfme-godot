// OpenBFME. PROJ-1 tests: the projectile state is part of the deterministic world hash. Each case changes ONE field of a projectile in flight (a mutation test, like
// test_combat_hash.cpp) and requires the hash to change; and two identical worlds that shoot identically have the same hash in every frame. Synthetic data, no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Weapon.h"

using namespace combattest;

struct BezierProjectileTestAccess
{
	static ObjectID &launcher(BezierProjectileBehavior &b) { return b.m_launcherID; }
	static Coord3D &origin(BezierProjectileBehavior &b) { return b.m_originPos; }
	static ObjectID &victim(BezierProjectileBehavior &b) { return b.m_victimID; }
	static const WeaponTemplate *&weapon(BezierProjectileBehavior &b) { return b.m_weapon; }
	static const WeaponTemplate *&warhead(BezierProjectileBehavior &b) { return b.m_warhead; }
	static std::vector<Coord3D> &path(BezierProjectileBehavior &b) { return b.m_flightPath; }
	static Coord3D &start(BezierProjectileBehavior &b) { return b.m_flightPathStart; }
	static Coord3D &end(BezierProjectileBehavior &b) { return b.m_flightPathEnd; }
	static float &speed(BezierProjectileBehavior &b) { return b.m_flightPathSpeed; }
	static int &segments(BezierProjectileBehavior &b) { return b.m_flightPathSegments; }
	static int &step(BezierProjectileBehavior &b) { return b.m_currentFlightPathStep; }
	static unsigned &bonus(BezierProjectileBehavior &b) { return b.m_bonusFlags; }
	static int &alt(BezierProjectileBehavior &b) { return b.m_altCurve; }
	static std::vector<ObjectID> &hits(BezierProjectileBehavior &b) { return b.m_hitList; }
	static bool &detonated(BezierProjectileBehavior &b) { return b.m_hasDetonated; }
	static float &heightScale(BezierProjectileBehavior &b) { return b.m_heightScale; }
	static Coord3D &pending(BezierProjectileBehavior &b) { return b.m_pendingPosition; } // lane PROJ-2
	static unsigned &pendingFrame(BezierProjectileBehavior &b) { return b.m_pendingFrame; }
	static bool &pendingValid(BezierProjectileBehavior &b) { return b.m_pendingValid; }
	static unsigned &fireFrame(BezierProjectileBehavior &b) { return b.m_fireFrame; }
};

namespace
{
BezierProjectileBehavior *bezierOf(Object *o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return b;
		}
	}
	return nullptr;
}

struct HashWorld : CombatWorld
{
	Object *archer = nullptr, *dummy = nullptr, *arrowObj = nullptr;
	BezierProjectileBehavior *arrow = nullptr;
	HashWorld()
	{
		combat().setAutoAcquireEnabled(false);
		archer = unit("Archer", 'A', 300, 300);
		dummy = unit("Dummy", 'B', 400, 300);
		frames(2);
		arrowObj = spawn("Arrow", 10.0f, 10.0f, 0.0f, teamA());
		arrow = bezierOf(arrowObj);
		arrow->projectileLaunchAtObjectOrPosition(dummy, nullptr, archer, 0, 0, TheWeaponStore->findWeaponTemplate("BowWeapon"), TheWeaponStore->findWeaponTemplate("BowWarhead"));
		frames(2); // the arrow is mid air
	}
};

#define MUTATE(NAME, STATEMENT)                                                                                                                           \
	do                                                                                                                                                    \
	{                                                                                                                                                     \
		HashWorld WORLD;                                                                                                                                  \
		const std::uint32_t before = WORLD.hash();                                                                                                        \
		{                                                                                                                                                 \
			HashWorld &w = WORLD;                                                                                                                         \
			using A = BezierProjectileTestAccess;                                                                                                         \
			(void)w;                                                                                                                                      \
			STATEMENT;                                                                                                                                    \
		}                                                                                                                                                 \
		CHECK_MESSAGE(WORLD.hash() != before, NAME);                                                                                                      \
	} while (0)
} // namespace

TEST_CASE("proj hash: two identical worlds with an arrow in the air have the same hash and the hash sees the arrow")
{
	HashWorld a, b;
	CHECK(a.hash() == b.hash());
	HashWorld c;
	c.frames(1);
	CHECK(c.hash() != a.hash());
}

TEST_CASE("proj hash: every field of the projectile module is in the hash")
{
	MUTATE("launcher id", A::launcher(*w.arrow) += 1);
	MUTATE("origin x", A::origin(*w.arrow).x += 1.0f);
	MUTATE("origin y", A::origin(*w.arrow).y += 1.0f);
	MUTATE("origin z", A::origin(*w.arrow).z += 1.0f);
	MUTATE("victim id", A::victim(*w.arrow) += 1);
	MUTATE("firing weapon", A::weapon(*w.arrow) = TheWeaponStore->findWeaponTemplate("Arc20"));
	MUTATE("warhead", A::warhead(*w.arrow) = TheWeaponStore->findWeaponTemplate("StoneWarhead"));
	MUTATE("path length", A::path(*w.arrow).pop_back());
	MUTATE("a path point x", A::path(*w.arrow)[3].x += 1.0f);
	MUTATE("a path point y", A::path(*w.arrow)[3].y += 1.0f);
	MUTATE("a path point z", A::path(*w.arrow)[3].z += 1.0f);
	MUTATE("path start", A::start(*w.arrow).y += 1.0f);
	MUTATE("path end", A::end(*w.arrow).y += 1.0f);
	MUTATE("path speed", A::speed(*w.arrow) += 1.0f);
	MUTATE("segment count", A::segments(*w.arrow) += 1);
	MUTATE("current step", A::step(*w.arrow) += 1);
	MUTATE("weapon bonus flags", A::bonus(*w.arrow) += 1);
	MUTATE("bounce index", A::alt(*w.arrow) += 1);
	MUTATE("hit list", A::hits(*w.arrow).push_back(77));
	MUTATE("detonated", A::detonated(*w.arrow) = true);
	MUTATE("height scale", A::heightScale(*w.arrow) += 0.5f);
	MUTATE("look-ahead point x", A::pending(*w.arrow).x += 1.0f); // lane PROJ-2
	MUTATE("look-ahead point y", A::pending(*w.arrow).y += 1.0f);
	MUTATE("look-ahead point z", A::pending(*w.arrow).z += 1.0f);
	MUTATE("look-ahead frame", A::pendingFrame(*w.arrow) += 1);
	MUTATE("look-ahead valid", A::pendingValid(*w.arrow) = !A::pendingValid(*w.arrow));
	MUTATE("fire frame", A::fireFrame(*w.arrow) += 1);
	{
		HashWorld a, b;
		BezierProjectileTestAccess::hits(*a.arrow).push_back(5);
		BezierProjectileTestAccess::hits(*b.arrow).push_back(6);
		CHECK(a.hash() != b.hash()); // the ids of the list, not only its size
	}
}

TEST_CASE("proj hash: the projectile counters of the combat state are in the hash")
{
	MUTATE("projectiles launched", w.combat().counters().projectilesLaunched += 1);
	MUTATE("projectiles detonated", w.combat().counters().projectilesDetonated += 1);
	MUTATE("projectiles landed", w.combat().counters().projectilesLanded += 1);
	MUTATE("ground hits", w.combat().counters().projectileGroundHits += 1);
	MUTATE("bounces", w.combat().counters().projectileBounces += 1);
	MUTATE("fx unplayed", w.combat().counters().projectileFxUnplayed += 1);
	MUTATE("emotions unported", w.combat().counters().projectileEmotionsUnported += 1);
	MUTATE("launch bones found", w.combat().counters().launchBonesFound += 1); // RENDER-2
	MUTATE("garrison launches unported", w.combat().counters().garrisonLaunchUnported += 1);
	MUTATE("launches without bones", w.combat().counters().launchesWithoutBones += 1);
}

// RENDER-2 review r1: the launch-bone inputs the logic owns are in the object hash (the instance scale, the placement's model condition bits)
TEST_CASE("proj hash: the launch-bone inputs of an object (instance scale, placement condition bits) are in the hash")
{
	MUTATE("instance scale", w.archer->setInstanceScale(w.archer->getInstanceScale() * 2.0f));
	MUTATE("placement condition bits", ([&] { Object::ModelConditionBits b{}; b[2] = 4u; w.archer->setPlacementConditionBits(b); })());
}

TEST_CASE("proj hash: the same volleys give the same hash in every frame, run twice")
{
	std::vector<std::uint32_t> first, second;
	for (int run = 0; run < 2; ++run)
	{
		CombatWorld w;
		w.combat().setAutoAcquireEnabled(false);
		w.logic->random().seedRandom(11);
		Object *archer = w.unit("Archer", 'A', 300, 300);
		Object *dummy = w.unit("Dummy", 'B', 410, 300);
		w.frames(2);
		ObjectWeapons *ow = archer->getWeapons();
		REQUIRE(ow->chooseBestWeaponForTarget(dummy, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
		std::vector<std::uint32_t> &out = run == 0 ? first : second;
		for (int f = 0; f < 60; ++f)
		{
			if (ow->currentStatus() == WEAPON_READY_TO_FIRE)
			{
				ow->preFireCurrentWeapon(dummy, nullptr);
			}
			else if (ow->currentStatus() == WEAPON_PRE_ATTACK)
			{
			}
			w.frames(1);
			if (ow->currentStatus() == WEAPON_READY_TO_FIRE)
			{
				ow->fireCurrentWeapon(dummy, nullptr);
			}
			out.push_back(w.hash());
		}
		CHECK(w.combat().counters().projectilesLaunched >= 2);
	}
	CHECK(first == second);
	bool moved = false;
	for (size_t i = 1; i < first.size(); ++i)
	{
		moved = moved || first[i] != first[i - 1];
	}
	CHECK(moved);
}
