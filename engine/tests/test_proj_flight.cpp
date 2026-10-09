// OpenBFME. PROJ-1 tests: a projectile object in the world - launch, flight frame by frame, the logic random draws, scatter, speed scaling, bounces, the warhead (hit stored target,
// radius damage and friendly fire) and the removal of the object. Synthetic data, no retail files.
#include "doctest.h"

#include <algorithm>
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Weapon.h"

#include <cmath>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
std::string shooter(const char *name, const char *weapon)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
		"  VisionRange = 250\n"
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
		"    Weapon = PRIMARY " + weapon + "\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 60\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"  Behavior = DestroyDie ModuleTag_Destroy\n"
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n";
}

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

struct Range
{
	CombatWorld w;
	Object *a = nullptr, *b = nullptr;
	ObjectWeapons *ow = nullptr;
	Range(const char *shooterName, const char *weapon, float gap, const std::string &extra = "")
		: w((shooter(shooterName, weapon) + extra).c_str())
	{
		w.combat().setAutoAcquireEnabled(false);
		a = w.unit(shooterName, 'A', 300, 300);
		b = w.unit("Dummy", 'B', 300 + gap, 300);
		w.frames(2);
		ow = a->getWeapons();
	}
	unsigned frame() { return w.logic->getFrame(); }
	// ready the weapon on the victim and fire one shot; the projectile (if any) exists on return
	void fire(Object *victim)
	{
		REQUIRE(ow->chooseBestWeaponForTarget(victim, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
		while (ow->currentStatus() != WEAPON_READY_TO_FIRE && frame() < 60)
		{
			w.frames(1);
		}
		REQUIRE(ow->currentStatus() == WEAPON_READY_TO_FIRE);
		ow->preFireCurrentWeapon(victim, nullptr);
		int waited = 0;
		while (ow->currentStatus() == WEAPON_PRE_ATTACK && waited < 10)
		{
			w.frames(1);
			++waited;
		}
		ow->fireCurrentWeapon(victim, nullptr);
	}
	Object *projectile()
	{
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (bezierOf(o))
			{
				return o;
			}
		}
		return nullptr;
	}
};

int countLine(const std::vector<GameLogicRandom::Call> &log, int line)
{
	int n = 0;
	for (const GameLogicRandom::Call &c : log)
	{
		n += (c.line == line && c.file == "Weapon.cpp") ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("proj flight: the arrow sits on its path point after point and the object is gone after the update that detonates it")
{
	Range r("Bowman", "BowWeapon", 100.0f);
	r.fire(r.b);
	Object *arrowObj = r.projectile();
	REQUIRE(arrowObj != nullptr);
	BezierProjectileBehavior *arrow = bezierOf(arrowObj);
	REQUIRE(arrow != nullptr);
	CHECK(arrowObj->getProducerID() == r.a->getID());
	CHECK(arrowObj->getControllingPlayer() == r.a->getControllingPlayer());
	CHECK(arrow->victimID() == r.b->getID());
	CHECK(arrow->projectileGetLauncherID() == r.a->getID());
	CHECK(arrow->projectileIsArmed());
	CHECK(arrowObj->testModelCondition(154)); // THROWN_PROJECTILE
	CHECK(arrowObj->testStatus((unsigned)CombatNames::statuses().noAttack));
	const std::vector<Coord3D> path = arrow->flightPath();
	const int n = arrow->segments();
	REQUIRE((int)path.size() == n);
	const ObjectID id = arrowObj->getID();
	// the launch took points 0 and 1 at once (RW 0x85EF34, lane PROJ-2: update, recordTransform, update); the victim does not move: the end of the path stays;
	// the update of frame L + i - 1 sets the position to point i
	CHECK(arrowObj->getPosition()->x == path[1].x);
	CHECK(arrowObj->getPosition()->z == path[1].z);
	CHECK(arrowObj->getRecordedPosition().x == path[0].x);
	CHECK(arrowObj->getRecordedPosition().z == path[0].z);
	CHECK(arrow->currentStep() == 2u);
	for (int i = 2; i < n; ++i)
	{
		r.w.frames(1);
		Object *now = r.w.byId(id);
		REQUIRE(now != nullptr);
		INFO("step " << i);
		CHECK(now->getPosition()->x == path[(size_t)i].x);
		CHECK(now->getPosition()->y == path[(size_t)i].y);
		CHECK(now->getPosition()->z == path[(size_t)i].z);
		// OrientToFlightPath: the X axis of the basis points along the path (towards the next point)
		if (i > 0 && i + 1 < n)
		{
			const float *b = now->getBasis();
			const float dx = path[(size_t)i + 1].x - path[(size_t)(i > 0 ? i - 1 : 0)].x;
			const float dz = path[(size_t)i + 1].z - path[(size_t)(i > 0 ? i - 1 : 0)].z;
			CHECK(b[0] > 0.0f);
			CHECK(std::fabs(b[6] - dz / std::sqrt(dx * dx + dz * dz)) < 0.05f);
		}
		CHECK(r.w.health(r.b) == 100.0f);
	}
	r.w.frames(1); // the update that finds the path used up
	CHECK(r.w.byId(id) == nullptr);
	CHECK(r.w.health(r.b) == doctest::Approx(100.0f - 15.0f * 0.8f));
	CHECK(r.w.combat().counters().projectilesDetonated == 1);
	CHECK(r.w.combat().counters().projectileGroundHits == 1);
}

TEST_CASE("proj flight: the logic random draws of a shot at a victim - the fire FX block's aim draw, one aim draw at the launch and one per update while the arrow follows its victim")
{
	Range r("Bowman", "BowWeapon", 100.0f);
	r.w.logic->random().enableCallLog(true);
	r.fire(r.b);
	Object *arrowObj = r.projectile();
	REQUIRE(arrowObj != nullptr);
	const int n = bezierOf(arrowObj)->segments();
	const std::vector<GameLogicRandom::Call> &log = r.w.logic->random().callLog();
	// lane FX-2 review (S-680): fireWeaponTemplate's fire FX block asks RW 0x6CB85A(victim, flag 1) at 0x6CCABD..0x6CCAC7, before the hit roll (0x6CCCB5) and
	// the launch; the BowWeapon has no PreferredTargetBone, so it draws (Weapon.cpp:1749). The launch's own aim query draws the second one, and the two updates the
	// launch runs at once (RW 0x85EF34, lane PROJ-2) draw one each while they follow the victim
	CHECK(countLine(log, 1749) == 4);
	for (const GameLogicRandom::Call &c : log)
	{
		if (c.line == 1749)
		{
			CHECK_FALSE(c.real);
			CHECK(c.lo == 0);
			CHECK(c.hi == 12345678);
		}
	}
	r.w.frames(n + 1);
	CHECK(countLine(log, 1749) == 2 + n); // every update with a victim and FlightPathAdjustDistPerSecond draws once before it moves the arrow (n updates, two of them in the launch); the detonating update draws none
}

TEST_CASE("proj flight: a missed shot (HitPercentage 0) scatters, flies to the scattered position, hurts nobody and draws only the fire FX block's aim value")
{
	Range r("ScatterArcher", "ScatterBow", 100.0f);
	r.w.logic->random().enableCallLog(true);
	r.fire(r.b);
	const std::vector<GameLogicRandom::Call> &log = r.w.logic->random().callLog();
	std::vector<int> lines;
	for (const GameLogicRandom::Call &c : log)
	{
		if (c.file == "Weapon.cpp")
		{
			lines.push_back(c.line);
		}
	}
	REQUIRE(lines.size() >= 3);
	CHECK(lines[0] == 1749);       // lane FX-2 review (S-680): the fire FX block's aim query (RW 0x6CCABD) precedes the miss rolls
	CHECK(lines[1] == 1489);       // the hit roll
	CHECK(std::count(lines.begin(), lines.end(), 1749) == 1); // the scattered projectile has no victim: no launch aim draw
	CHECK(lines.back() == 1639);   // the scatter angle is the last draw before the launch
	Object *arrowObj = r.projectile();
	REQUIRE(arrowObj != nullptr);
	BezierProjectileBehavior *arrow = bezierOf(arrowObj);
	CHECK(arrow->victimID() == INVALID_ID); // a scattered shot has no victim, it flies to a position
	const Coord3D end = arrow->flightEnd();
	const Coord3D vp = *r.b->getPosition();
	const float dx = end.x - vp.x, dy = end.y - vp.y;
	CHECK(std::sqrt(dx * dx + dy * dy) <= 20.0f + 0.01f); // ScatterRadius 20
	CHECK(std::sqrt(dx * dx + dy * dy) > 0.0f);
	const size_t before = log.size();
	r.w.frames(arrow->segments() + 2);
	CHECK(log.size() == before); // no victim: no aim draw in flight
	CHECK(r.w.health(r.b) == 100.0f);
	CHECK(r.w.combat().counters().projectilesDetonated == 1);
}

TEST_CASE("proj flight: ScaleWeaponSpeed - the binary subtracts the minimum range from the SQUARED distance, so a mid range lob already runs at MaxWeaponSpeed")
{
	Range r("Lobber", "Lob", 120.0f);
	r.fire(r.b);
	Object *arrowObj = r.projectile();
	REQUIRE(arrowObj != nullptr);
	// WeaponSpeed 300 / 5 = 60, MinWeaponSpeed 30, MaxWeaponSpeed 80: ((14400 - 17.5) / (300 - 17.5)) * 30 + 30 is far above 80
	CHECK(bezierOf(arrowObj)->flightSpeed() == doctest::Approx(80.0f));
}

TEST_CASE("proj flight: a bouncing stone - three paths (the bounce curves run at half speed), a ground hit and a bounce weapon per landing, the warhead only at the last")
{
	Range r("Catapult", "StoneBounce", 150.0f);
	r.fire(r.b);
	Object *stoneObj = r.projectile();
	REQUIRE(stoneObj != nullptr);
	BezierProjectileBehavior *stone = bezierOf(stoneObj);
	const ObjectID id = stoneObj->getID();
	CHECK(stone->flightSpeed() == doctest::Approx(20.0f));
	CHECK(stone->bounceIndex() == 0);
	int guard = 0;
	std::vector<int> segments{ stone->segments() };
	int lastBounce = 0;
	while (r.w.byId(id) && guard < 200)
	{
		r.w.frames(1);
		++guard;
		if (r.w.byId(id) && stone->bounceIndex() != lastBounce)
		{
			lastBounce = stone->bounceIndex();
			segments.push_back(stone->segments());
			CHECK(stone->flightSpeed() == doctest::Approx(20.0f)); // the speed field keeps WeaponSpeed; the path length uses half of it on a bounce
		}
	}
	REQUIRE(r.w.byId(id) == nullptr);
	CHECK(segments.size() == 3);
	CHECK(segments[1] < segments[0]);
	CHECK(r.w.combat().counters().projectileBounces == 2);
	CHECK(r.w.combat().counters().projectileGroundHits == 3);
	CHECK(r.w.combat().counters().projectileFxUnplayed == 3); // GroundHitFX once, GroundBounceFX twice
	CHECK(r.w.combat().counters().projectilesDetonated == 1);
	CHECK(r.w.combat().counters().projectilesLaunched == 1);
}

TEST_CASE("proj flight: a stone's warhead is a radius damage at the landing spot - friendly fire follows RadiusDamageAffects")
{
	for (int enemiesOnly = 0; enemiesOnly < 2; ++enemiesOnly)
	{
		INFO("enemies only: " << enemiesOnly);
		Range r("Catapult", enemiesOnly ? "StoneThrowEnemies" : "StoneThrow", 150.0f);
		Object *friendUnit = r.w.unit("Dummy", 'A', 300 + 150 + 5, 300 + 6); // next to the target, on the shooter's side
		Object *target = r.b;
		r.w.frames(2);
		r.fire(target);
		Object *stoneObj = r.projectile();
		REQUIRE(stoneObj != nullptr);
		BezierProjectileBehavior *stone = bezierOf(stoneObj);
		const Coord3D aim = stone->flightEnd();
		CHECK(aim.x == doctest::Approx(450.0f).epsilon(0.02));
		r.w.runUntil([&] { return r.w.combat().counters().projectilesDetonated == 1; }, 300);
		CHECK(r.w.combat().counters().projectilesDetonated == 1);
		// the stone lands on the aim point (the target's centre): a radius damage of 40 PIERCE (80% armour = 32) within 20 of it
		CHECK(r.w.health(target) == doctest::Approx(100.0f - 32.0f));
		if (enemiesOnly)
		{
			CHECK(r.w.health(friendUnit) == 100.0f);
		}
		else
		{
			CHECK(r.w.health(friendUnit) == doctest::Approx(100.0f - 32.0f));
		}
	}
}

// S-360..S-365 reach the run report of the game (GameLogic::report().stops), once each, and the counters line carries the projectile counters.
TEST_CASE("proj stops: S-360 .. S-364 are in GameLogic::report().stops exactly once, with the projectile counters")
{
	CombatWorld w;
	const GameLogic::Report report = w.logic->report();
	const char *expect[] = { "projectile launch", "flight path numerics", "aim and terrain", "projectile behaviour", "warhead", "projectile drawing" };
	for (int id = 360; id <= 365; ++id)
	{
		const std::string prefix = "[S-" + std::to_string(id) + "] ";
		int matches = 0;
		for (const std::string &line : report.stops)
		{
			if (line.rfind(prefix, 0) == 0)
			{
				++matches;
				CHECK_MESSAGE(line.find(expect[id - 360]) != std::string::npos, line.substr(0, 60));
			}
		}
		CHECK_MESSAGE(matches == 1, prefix << "appears " << matches << " times");
	}
	Range r("Bowman", "BowWeapon", 100.0f);
	r.fire(r.b);
	r.w.frames(12);
	std::string counters;
	for (const std::string &line : r.w.logic->report().stops)
	{
		if (line.rfind("[S-320..S-328 counters]", 0) == 0)
		{
			counters = line;
		}
	}
	CHECK(counters.find("projectiles launched 1 detonated 1") != std::string::npos);
}

// inFlight() counts live projectile modules with a path that have not detonated; an arrow removed another way (jammed, destroyed by a defeat cleanup) is not in the air
TEST_CASE("proj flight: inFlight() drops when an arrow is jammed or destroyed directly, not only when it detonates")
{
	for (int how = 0; how < 3; ++how)
	{
		INFO("removal " << how << " (0 jammed, 1 destroyObject, 2 detonates)");
		Range r("Bowman", "BowWeapon", 100.0f);
		r.fire(r.b);
		Object *arrowObj = r.projectile();
		REQUIRE(arrowObj != nullptr);
		BezierProjectileBehavior *arrow = bezierOf(arrowObj);
		CHECK(r.w.combat().inFlight() == 1);
		r.w.frames(1);
		CHECK(r.w.combat().inFlight() == 1);
		const unsigned long long launched = r.w.combat().counters().projectilesLaunched;
		const unsigned long long detonated = r.w.combat().counters().projectilesDetonated;
		if (how == 0)
		{
			arrow->projectileNowJammed(); // kill: the object is effectively dead at once
			CHECK(r.w.combat().inFlight() == 0);
		}
		else if (how == 1)
		{
			r.w.logic->destroyObject(arrowObj);
			CHECK(r.w.combat().inFlight() == 0);
		}
		else
		{
			r.w.frames(arrow->segments() + 1);
			CHECK(r.w.combat().inFlight() == 0);
		}
		r.w.frames(3);
		CHECK(r.w.combat().inFlight() == 0);
		CHECK(r.projectile() == nullptr);
		// the historical counters are not touched by a removal that is not a detonation
		CHECK(r.w.combat().counters().projectilesLaunched == launched);
		CHECK(r.w.combat().counters().projectilesDetonated == (how == 2 ? detonated + 1 : detonated));
	}
}
