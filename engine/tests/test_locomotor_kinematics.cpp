// OpenBFME unit tests: Locomotor kinematics (getters, moveForward, rotateTowardsPosition, movers, dispatcher).
// GPL-3.0. Lane HORDE-1, spec horde-and-movement.md 2.13 and checklist step 4.
// Expected values are closed-form from the formulas of the spec / the disassembly (cited in
// GameLogic/Object/LocomotorMove.cpp): per-frame speed = Speed * 0.2, acceleration = speed / AccelerationFrames,
// braking distance = ((v/b) + 1) * (v/2) * 1.05, turn rate = 2*pi / TurnTimeFrames.

#include "doctest.h"
#include "IniTestUtil.h"

#include "GameLogic/BitFlags.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/WeaponState.h"

#include <cmath>
#include <map>
#include <set>

using namespace initest;

namespace
{
const float kPi = 3.14159265f;

int mcIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	FAIL("unknown model condition " << name);
	return -1;
}

int statusIndex(const char *name)
{
	for (int i = 0; TheObjectStatusNames[i]; ++i)
	{
		if (std::string(TheObjectStatusNames[i]) == name)
		{
			return i;
		}
	}
	FAIL("unknown status " << name);
	return -1;
}

struct MockPath : LocomotorPath
{
	std::vector<Coord3D> points; // polyline
	size_t closest = 0;
	bool nearEnd = false;
	bool explicitZ = false;
	int specialNode = 0;
	float remaining = 0.0f;

	LocomotorPathPoint computePointAhead(float distance) override
	{
		if (distance < 0.1f)
		{
			distance = 0.1f;
		}
		// walk `distance` along the polyline from the closest segment's start
		Coord3D p = points[closest];
		size_t i = closest;
		float left = distance;
		while (i + 1 < points.size())
		{
			const Coord3D &a = points[i];
			const Coord3D &b = points[i + 1];
			const float len = std::hypot(b.x - a.x, b.y - a.y);
			if (left <= len)
			{
				const float f = len > 0 ? left / len : 0.0f;
				p = Coord3D{ a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z };
				LocomotorPathPoint r;
				r.position = p;
				return r;
			}
			left -= len;
			++i;
			p = points[i];
		}
		LocomotorPathPoint r;
		r.position = points.back();
		return r;
	}
	void updateClosestSegment(const Coord3D &pos) override
	{
		size_t best = closest;
		float bestD = 1e30f;
		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			const float d = std::hypot(points[i].x - pos.x, points[i].y - pos.y);
			if (d < bestD)
			{
				bestD = d;
				best = i;
			}
		}
		closest = best;
	}
	bool isNearPathEnd() const override { return nearEnd; }
	bool hasExplicitZ() const override { return explicitZ; }
	float remainingDistanceFrom(const LocomotorPathPoint &) const override { return remaining; }
	int currentSpecialNodeType() const override { return specialNode; }
};

struct MockHost : LocomotorHost
{
	Coord3D pos{ 0, 0, 0 };
	float angle = 0.0f;
	float boundingRadius = 10.0f;
	LocomotorMatrix transform = LocomotorMatrix::identity();
	bool pendingValid = false;
	Coord3D pending{ 0, 0, 0 };
	float setSpeed = 55.0f;
	int damage = 0;
	int penaltyState = 3;
	float crew = 1.0f;
	bool attrib = false;
	float attribValue = 1.0f;
	bool river = false;
	bool turnLimited = false;
	bool chargeOrdered = false;
	bool motionDisabled = false;
	bool physics = true; // obj+0x264 (lane EXIT-1: maintainCurrentPosition)
	bool zSuppressed = false;
	unsigned frame = 100;
	bool containerBack = false;
	float groundZ = 0.0f;
	MockPath *path = nullptr;
	std::set<int> status;
	std::set<int> mc;
	std::vector<std::string> events;
	bool contain = false;
	bool formationReady = true;
	bool thingWaits = false;
	int wheelsStopped = 0;
	int reformBegin = 0, reformEnd = 0;
	std::vector<LocomotorMatrix> applied;

	MockHost() { syncTransform(); }

	void place(float x, float y, float a)
	{
		pos = Coord3D{ x, y, 0 };
		angle = a;
		syncTransform();
	}
	void syncTransform()
	{
		transform = LocomotorMatrix::identity();
		const float c = std::cos(angle), s = std::sin(angle);
		transform.m[0][0] = c;
		transform.m[0][1] = -s;
		transform.m[1][0] = s;
		transform.m[1][1] = c;
		transform.m[0][3] = pos.x;
		transform.m[1][3] = pos.y;
		transform.m[2][3] = pos.z;
	}

	Coord3D getPosition() const override { return pos; }
	float getAngle() const override { return angle; }
	Coord2D getUnitDirectionVector2D() const override { return Coord2D{ std::cos(angle), std::sin(angle) }; }
	float getBoundingRadius() const override { return boundingRadius; }
	LocomotorMatrix getTransform() const override { return transform; }
	void setTransform(const LocomotorMatrix &m) override
	{
		transform = m;
		pos = Coord3D{ m.m[0][3], m.m[1][3], m.m[2][3] };
		angle = std::atan2(m.m[1][0], m.m[0][0]);
		applied.push_back(m);
	}
	bool hasPendingPosition() const override { return pendingValid; }
	Coord3D getPendingPosition() const override { return pending; }
	void setPendingPosition(const Coord3D &p) override
	{
		pending = p;
		pendingValid = true;
	}
	float locomotorSetSpeed() const override { return setSpeed; }
	int damageState() const override { return damage; }
	int movementPenaltyDamageState() const override { return penaltyState; }
	float crewPowerMultiplier() const override { return crew; }
	bool speedAttributeModifier(float &out) const override
	{
		out = attribValue;
		return attrib;
	}
	bool isInRiver(float, float) const override { return river; }
	bool isTurnLimited() const override { return turnLimited; }
	bool isChargeOrdered() const override { return chargeOrdered; }
	bool physicsMotionDisabled() const override { return motionDisabled; }
	bool hasPhysicsModule() const override { return physics; }
	bool zMotionSuppressed() const override { return zSuppressed; }
	unsigned logicFrame() const override { return frame; }
	bool containerAllowsBackingUp() const override { return containerBack; }
	float groundHeightAt(float, float) const override { return groundZ; }
	LocomotorPath *getPath() override { return path; }
	bool testObjectStatus(int bit) const override { return status.count(bit) != 0; }
	bool testModelCondition(int bit) const override { return mc.count(bit) != 0; }
	void setModelCondition(int bit, bool value) override
	{
		if (value)
		{
			mc.insert(bit);
		}
		else
		{
			mc.erase(bit);
		}
		events.push_back(std::string(value ? "+" : "-") + TheModelConditionNames[bit]);
	}
	void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) override
	{
		for (int c : clear)
		{
			mc.erase(c);
		}
		for (int s : set)
		{
			mc.insert(s);
		}
		events.push_back("clearAndSet");
	}
	void notifyWheelsStopped() override { ++wheelsStopped; }
	bool hasHordeContain() const override { return contain; }
	void hordeBeginReform() override { ++reformBegin; }
	void hordeEndReform() override { ++reformEnd; }
	bool hordeFormationReady(float) const override { return formationReady; }
	bool thingWaitsForFormation() const override { return thingWaits; }
	// lane SHIP-1
	bool underwater = false;
	float waterZ = 0.0f;
	bool held = false;
	bool submarine = false;
	bool isUnderwater(float, float, float *z) const override
	{
		if (underwater && z)
		{
			*z = waterZ;
		}
		return underwater;
	}
	bool isDisabledHeld() const override { return held; }
	bool isKindOfSubmarine() const override { return submarine; }
	bool weapon = false;
	int weaponStatus = 0;
	int statusCalls = 0;
	bool stateActive = true;
	bool hasCurrentWeapon() const override { return weapon; }
	int currentWeaponStatusWriteBack() override
	{
		++statusCalls;
		return weaponStatus;
	}
	bool aiStateActive() const override { return stateActive; }
};

// A store holding one locomotor parsed from INI text; returns the template.
struct LocoFixture
{
	Fixture fx;
	LocomotorStore store;
	LocomotorStore *saved;
	LocoFixture(const std::string &body)
	{
		saved = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		const std::string err = loadError(fx.env, "k.ini", "Locomotor K\n" + body + "\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~LocoFixture() { TheLocomotorStore = saved; }
	const LocomotorTemplate *tmpl() { return store.findLocomotorTemplate("K"); }
};

const char *kHuman = "  Surfaces = GROUND RUBBLE\n  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
					 "  Appearance = TWO_LEGS\n  StickToGround = Yes\n";

bool nearly(float a, double b, double eps = 1e-4)
{
	return std::fabs((double)a - b) <= eps;
}
}

TEST_CASE("Locomotor getters: Speed 55 is 11 per frame; acceleration and braking are speed / frames; turn rate is 2*pi / TurnTime")
{
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.setSpeed = 55.0f; // LocomotorSet Speed, raw distance per second
	CHECK(loco.getMaxSpeedForCondition(h) == 11.0f);                // 55 * 0.2
	CHECK(nearly(loco.getMaxAcceleration(h), 11.0 / 3.0, 1e-6));       // 510 ms = 3 frames
	CHECK(nearly(loco.getBraking(h), 11.0 / 3.0, 1e-6));
	CHECK(nearly(loco.getMaxTurnRate(h), 2.0 * kPi / 3.0, 1e-6));      // 500 ms = 3 frames
	// spec 2.2 step 4: the member snap threshold min(accel, 0.2 * maxSpeed) is 2.2 for a GondorFighter
	const float t = std::fmin(loco.getMaxAcceleration(h), 0.2f * loco.getMaxSpeedForCondition(h));
	CHECK(nearly(t, 2.2, 1e-5));

	LocoFixture horde("  TurnTime = 2000\n  Acceleration = 500\n  Braking = 500\n  Appearance = HORDE\n  MaxTurnWithoutReform = 45\n  TurnWhileMoving = No\n  WaitForFormation = Yes\n");
	Locomotor hl(horde.tmpl());
	h.setSpeed = 50.0f; // NORMAL_FOOT_MED_HORDE_SPEED in 2.01
	CHECK(hl.getMaxSpeedForCondition(h) == 10.0f);
	CHECK(nearly(hl.getMaxTurnRate(h), 2.0 * kPi / 10.0, 1e-6)); // 2000 ms = 10 frames
}

TEST_CASE("Locomotor getters: damage state, charge, crew, attribute, river and caps follow RW 0x5E3F49")
{
	LocoFixture f(std::string(kHuman) + "  SpeedDamaged = 50%\n  ChargeSpeed = 200%\n  ChargeAvailable = Yes\n  RiverModifier = 50%\n  CrewPowered = Yes\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.setSpeed = 100.0f;
	CHECK(loco.getMaxSpeedForCondition(h) == 20.0f);
	h.damage = 3; // >= MovementPenaltyDamageState
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 10.0, 1e-5)); // SpeedDamaged 50%
	h.damage = 0;
	h.status.insert(statusIndex("IS_ATTACKING"));
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 40.0, 1e-5)); // charging: ChargeSpeed 200%
	CHECK(loco.isCharging(h));
	h.status.clear();
	h.chargeOrdered = true;
	h.mc.insert(mcIndex("CHARGING"));
	CHECK(loco.isCharging(h));
	h.mc.clear();
	h.chargeOrdered = false;
	CHECK_FALSE(loco.isCharging(h));
	h.crew = 0.5f;
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 10.0, 1e-5));
	h.crew = 1.0f;
	h.attrib = true;
	h.attribValue = 1.5f;
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 30.0, 1e-5));
	h.attrib = false;
	h.river = true;
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 10.0, 1e-5));
	h.river = false;
	loco.setMaxSpeedCap(5.0f);
	CHECK(loco.getMaxSpeedForCondition(h) == 5.0f);
	loco.setMaxSpeedCap(99999.0f);
	loco.setTemporarySpeedCap(7.0f);
	CHECK(loco.getMaxSpeedForCondition(h) == 7.0f);
	loco.setTemporarySpeedCap(1000.0f); // above the speed: no effect
	CHECK(nearly(loco.getMaxSpeedForCondition(h), 20.0, 1e-5));
	// ChargeIgnoresCondition keeps the undamaged speed
	LocoFixture g(std::string(kHuman) + "  SpeedDamaged = 50%\n  ChargeIgnoresCondition = Yes\n");
	Locomotor l2(g.tmpl());
	h.setSpeed = 100.0f;
	h.damage = 3;
	CHECK(nearly(l2.getMaxSpeedForCondition(h), 20.0, 1e-5));
	// acceleration and braking caps
	loco.setTemporarySpeedCap(-1.0f);
	loco.setAccelerationCap(1.0f);
	loco.setBrakingCap(2.0f);
	loco.setTurnRateCap(0.5f);
	CHECK(loco.getMaxAcceleration(h) == 1.0f);
	CHECK(loco.getBraking(h) == 2.0f);
	CHECK(loco.getMaxTurnRate(h) == 0.5f);
}

TEST_CASE("moveForward: a unit on a straight line reaches 11 per frame in 3 frames and keeps it (checklist step 4)")
{
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	const Coord3D goal{ 1000, 0, 0 };
	const float accel = 11.0f / 3.0f;
	float expectX = 0.0f;
	for (int frame = 1; frame <= 6; ++frame)
	{
		loco.locomotorMoveTowardsPosition(h, goal, 1000.0f - h.pos.x, 11.0f);
		if (frame == 1)
		{
			CHECK(nearly(loco.speed(), accel, 1e-5));
		}
		if (frame == 2)
		{
			CHECK(nearly(loco.speed(), 2 * accel, 1e-5));
		}
		if (frame >= 3)
		{
			CHECK(loco.speed() == 11.0f);
		}
		expectX += loco.speed(); // no path: the step is max(speed, 2.0)
		CHECK(nearly(h.pos.x, expectX, 1e-3));
		CHECK(nearly(h.pos.y, 0.0, 1e-6));
	}
	CHECK(nearly(h.pos.x, accel + 2 * accel + 11.0 * 4, 1e-3));
	CHECK(loco.unverified().size() >= 1);
}

TEST_CASE("moveForward: braking starts at the stopping distance ((v/b + 1) * v/2 * 1.05) and the unit stops at the goal")
{
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	const Coord3D goal{ 300, 0, 0 };
	// get to full speed first
	for (int i = 0; i < 6; ++i)
	{
		loco.locomotorMoveTowardsPosition(h, goal, goal.x - h.pos.x, 11.0f);
	}
	REQUIRE(loco.speed() == 11.0f);
	CHECK((loco.flags() & LOCOMOTOR_FLAG_BRAKING) == 0);
	// stopping distance of 11 per frame with b = 11/3: ((11/(11/3)) + 1) * 5.5 * 1.05 = 4 * 5.5 * 1.05 = 23.1
	const double stopping = ((11.0 / (11.0 / 3.0)) + 1.0) * (11.0 * 0.5) * 1.05;
	CHECK(nearly((float)stopping, 23.1, 1e-4));
	float firstBrakeRemaining = -1.0f;
	float lastRemaining = 1e9f;
	int frames = 0;
	while (h.pos.x < goal.x - 1e-4f && frames < 100)
	{
		const float remaining = goal.x - h.pos.x;
		const float speedBefore = loco.speed();
		loco.locomotorMoveTowardsPosition(h, goal, remaining, 11.0f);
		if ((loco.flags() & LOCOMOTOR_FLAG_BRAKING) && firstBrakeRemaining < 0)
		{
			firstBrakeRemaining = remaining;
			// braking began because the stopping distance at the speed BEFORE this frame exceeded the remaining distance
			const double st = ((speedBefore / (11.0 / 3.0)) + 1.0) * (speedBefore * 0.5) * 1.05;
			CHECK(st > remaining);
		}
		CHECK(loco.speed() <= 11.0f);
		lastRemaining = goal.x - h.pos.x;
		++frames;
	}
	CHECK(firstBrakeRemaining > 0.0f);
	CHECK(firstBrakeRemaining <= 23.1f + 1e-3f);
	CHECK(nearly(h.pos.x, goal.x, 1e-3)); // the step is clamped to the remaining distance: it stops on the goal
	CHECK(lastRemaining <= 1e-3f);
	CHECK(frames < 40);
	// no overshoot
	CHECK(h.pos.x <= goal.x + 1e-3f);
}

TEST_CASE("rotateTowardsPosition: the turn per frame is capped at 2*pi / TurnTime (Legs, no slow-down for TWO_LEGS)")
{
	LocoFixture f("  TurnTime = 2000\n  Acceleration = 510\n  Braking = 510\n  Appearance = TWO_LEGS\n  TurnPivotOffset = 0\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	const double rate = 2.0 * kPi / 10.0;
	const Coord3D goal{ 0, 500, 0 }; // straight to the left (+y)
	float lastAngle = 0.0f;
	for (int frame = 1; frame <= 4; ++frame)
	{
		loco.locomotorMoveTowardsPosition(h, goal, 500.0f, 11.0f);
		const double expected = std::fmin(rate * frame, kPi / 2.0);
		CHECK(nearly(h.angle, expected, 2e-4));
		CHECK(h.angle >= lastAngle - 1e-6f);
		lastAngle = h.angle;
		if (frame == 1)
		{
			CHECK(loco.turnDirection() == 1); // turning left, above the 15 degree threshold
		}
	}
	// the other way
	MockHost h2;
	h2.place(0, 0, 0);
	Locomotor l2(f.tmpl());
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 0, -500, 0 }, 500.0f, 11.0f);
	CHECK(nearly(h2.angle, -rate, 2e-4));
	CHECK(l2.turnDirection() == -1);
	// TWO_LEGS do not slow down while turning: the first frame already accelerates at full rate
	CHECK(nearly(l2.speed(), 11.0 / 3.0, 1e-4));
}

TEST_CASE("Legs: HUGE legs slow down while turning (RW 0x5E66E1), TWO_LEGS do not")
{
	LocoFixture two(std::string(kHuman) + "  TurnTime = 2000\n");
	LocoFixture huge("  TurnTime = 2000\n  Acceleration = 510\n  Braking = 510\n  Appearance = HUGE_TWO_LEGS\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
	MockHost a, b;
	a.place(0, 0, 0);
	b.place(0, 0, 0);
	Locomotor l1(two.tmpl()), l2(huge.tmpl());
	// goal 90 degrees to the left: |rel| * 4/pi = 2 -> capped factor 0.9: speed target = 0.1 * desired
	l1.moveTowardsPositionLegs(a, Coord3D{ 0, 500, 0 }, 500.0f, 11.0f);
	l2.moveTowardsPositionLegs(b, Coord3D{ 0, 500, 0 }, 500.0f, 11.0f);
	CHECK(nearly(l1.speed(), 11.0 / 3.0, 1e-4));
	// the second unit's target is 1.1 (0.1 * 11): its first step accelerates by 11/3 but is cut back to the target
	CHECK(nearly(l2.speed(), 1.1, 1e-3));
}

TEST_CASE("Legs: backing up when afraid (CanMoveBackwards + EMOTION_TERROR) aims away from the goal")
{
	LocoFixture f(std::string(kHuman) + "  CanMoveBackwards = Yes\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	h.mc.insert(mcIndex("EMOTION_TERROR"));
	// goal behind-left; the unit is stationary: backing is switched on and the speed target is halved
	loco.moveTowardsPositionLegs(h, Coord3D{ -100, 0, 0 }, 100.0f, 11.0f);
	CHECK((loco.flags() & LOCOMOTOR_FLAG_BACKING_UP) != 0);
	CHECK(h.mc.count(mcIndex("BACKING_UP")) == 1);
	// without the emotion the flag is cleared again
	h.mc.erase(mcIndex("EMOTION_TERROR"));
	loco.moveTowardsPositionLegs(h, Coord3D{ -100, 0, 0 }, 100.0f, 11.0f);
	CHECK((loco.flags() & LOCOMOTOR_FLAG_BACKING_UP) == 0);
	CHECK(h.mc.count(mcIndex("BACKING_UP")) == 0);
}

TEST_CASE("DownhillOnly: a goal above the unit is refused (RW 0x5E6415)")
{
	LocoFixture f(std::string(kHuman) + "  DownhillOnly = Yes\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	loco.moveTowardsPositionLegs(h, Coord3D{ 100, 0, 5 }, 100.0f, 11.0f);
	CHECK(loco.speed() == 0.0f);
	loco.moveTowardsPositionLegs(h, Coord3D{ 100, 0, -5 }, 100.0f, 11.0f);
	CHECK(loco.speed() > 0.0f);
}

TEST_CASE("Wheels: p = 0 above a quarter of the maximum speed, 2*rel at low speed; turn slow-down 1 - min(|p|*4/pi, 1)")
{
	LocoFixture f("  TurnTime = 1500\n  Acceleration = 800\n  Braking = 1500\n  Appearance = FOUR_WHEELS\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  CanMoveBackwards = No\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.setSpeed = 100.0f;
	h.place(0, 0, 0);
	// stationary, goal 45 degrees to the left: p = 2 * rel = pi/2, factor min(2, 1) = 1, so the target is 0
	loco.moveTowardsPositionWheels(h, Coord3D{ 100, 100, 0 }, 141.0f, 20.0f);
	CHECK(loco.speed() == 0.0f);
	// straight ahead: factor 0, full target
	MockHost h2;
	h2.setSpeed = 100.0f;
	h2.place(0, 0, 0);
	Locomotor l2(f.tmpl());
	l2.moveTowardsPositionWheels(h2, Coord3D{ 100, 0, 0 }, 100.0f, 20.0f);
	CHECK(l2.speed() > 0.0f);
	// a unit that is stopped with nothing to move to reports it
	MockHost h3;
	h3.setSpeed = 0.0f; // maximum speed 0
	h3.place(0, 0, 0);
	Locomotor l3(f.tmpl());
	l3.moveTowardsPositionWheels(h3, Coord3D{ 100, 0, 0 }, 100.0f, 20.0f);
	CHECK(h3.wheelsStopped == 1);
}

TEST_CASE("Wheels: a goal behind the unit starts backing up when CanMoveBackwards is set and the unit is close")
{
	LocoFixture f("  TurnTime = 1500\n  Acceleration = 800\n  Braking = 1500\n  Appearance = FOUR_WHEELS\n  CanMoveBackwards = Yes\n  BackingUpDistanceMin = 0\n"
				  "  BackingUpDistanceMax = 100\n  BackingUpAngle = 0.5\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.setSpeed = 100.0f;
	h.place(0, 0, 0);
	loco.setPreferredPoint(Coord3D{ 0, 0, 0 });
	loco.moveTowardsPositionWheels(h, Coord3D{ -30, 0, 0 }, 30.0f, 20.0f); // |rel| = pi > 0.5 * pi
	CHECK((loco.flags() & LOCOMOTOR_FLAG_BACKING_UP) != 0);
	CHECK(h.mc.count(mcIndex("BACKING_UP")) == 1);
	// far away in both senses: backing is cancelled
	Locomotor l2(f.tmpl());
	MockHost h2;
	h2.setSpeed = 100.0f;
	h2.place(0, 0, 0);
	l2.setPreferredPoint(Coord3D{ 500, 0, 0 });
	l2.moveTowardsPositionWheels(h2, Coord3D{ -300, 0, 0 }, 300.0f, 20.0f);
	CHECK((l2.flags() & LOCOMOTOR_FLAG_BACKING_UP) == 0);
}

TEST_CASE("HORDE mover: reform instead of wheel beyond MaxTurnWithoutReform; x0.1 while the formation waits (RW 0x5E6D4F)")
{
	LocoFixture f("  TurnTime = 2000\n  Acceleration = 500\n  Braking = 500\n  Appearance = HORDE\n  MaxTurnWithoutReform = 45\n  TurnWhileMoving = No\n  WaitForFormation = Yes\n"
				  "  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.setSpeed = 50.0f;
	h.contain = true;
	h.thingWaits = true;
	h.formationReady = true;
	h.place(0, 0, 0);
	// the goal is 90 degrees to the left (beyond 45): the horde snaps its orientation and begins/ends a reform
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 0, 500, 0 }, 500.0f, 10.0f);
	CHECK(h.reformBegin == 1);
	CHECK(h.reformEnd == 1);
	CHECK(nearly(h.angle, kPi / 2.0, 1e-4)); // instant snap (TurnWhileMoving = No: no gradual rotation)
	// a 30 degree turn wheels (no reform)
	MockHost h2;
	h2.setSpeed = 50.0f;
	h2.contain = true;
	h2.place(0, 0, 0);
	Locomotor l2(f.tmpl());
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 500, 288.675f, 0 }, 577.0f, 10.0f);
	CHECK(h2.reformBegin == 0);
	CHECK(nearly(h2.angle, 0.0, 1e-6)); // TurnWhileMoving = No: the horde does not rotate while moving
	// short remaining path: no reform even for a big turn
	MockHost h3;
	h3.setSpeed = 50.0f;
	h3.contain = true;
	h3.place(0, 0, 0);
	Locomotor l3(f.tmpl());
	l3.locomotorMoveTowardsPosition(h3, Coord3D{ 0, 15, 0 }, 15.0f, 10.0f);
	CHECK(h3.reformBegin == 0);
	// formation not ready and the horde is slow: the desired speed is multiplied by 0.1
	MockHost h4;
	h4.setSpeed = 50.0f;
	h4.contain = true;
	h4.thingWaits = true;
	h4.formationReady = false;
	h4.place(0, 0, 0);
	Locomotor l4(f.tmpl());
	// goal straight ahead so no reform; target = 0.1 * 10 = 1.0 < acceleration 3.33: speed ends at 1.0
	l4.locomotorMoveTowardsPosition(h4, Coord3D{ 500, 0, 0 }, 500.0f, 10.0f);
	CHECK(nearly(l4.speed(), 1.0, 1e-5));
	// ready: full acceleration
	MockHost h5;
	h5.setSpeed = 50.0f;
	h5.contain = true;
	h5.thingWaits = true;
	h5.formationReady = true;
	h5.place(0, 0, 0);
	Locomotor l5(f.tmpl());
	l5.locomotorMoveTowardsPosition(h5, Coord3D{ 500, 0, 0 }, 500.0f, 10.0f);
	CHECK(nearly(l5.speed(), 10.0 / 3.0, 1e-4));
}

TEST_CASE("HORDE mover: formation path nodes (types 2, 3, 7, 8) are a reported stop; node 4 and plain nodes take the plain branch")
{
	LocoFixture f("  TurnTime = 2000\n  Acceleration = 500\n  Braking = 500\n  Appearance = HORDE\n  TurnWhileMoving = No\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
	MockPath path;
	path.points = { Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 } };
	path.remaining = 100.0f;
	for (int node : { 2, 3, 7, 8 })
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 50.0f;
		h.path = &path;
		path.specialNode = node;
		CHECK_THROWS_WITH_AS(loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 10.0f), doctest::Contains("S-084"), std::logic_error);
		CHECK(loco.unverified().size() >= 1);
	}
	for (int node : { 0, 1, 4 })
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 50.0f;
		h.path = &path;
		path.specialNode = node;
		CHECK_NOTHROW(loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 10.0f));
		CHECK(loco.speed() > 0.0f);
	}
}

TEST_CASE("moveForward on a path: the position is the point ahead; the pending position and the matrix translation are recorded (RW 0x5E5A04)")
{
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	MockPath path;
	path.points = { Coord3D{ 0, 0, 0 }, Coord3D{ 1000, 0, 0 } };
	path.remaining = 1000.0f;
	h.path = &path;
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 11.0f);
	// speed after the frame is 11/3; the point ahead is that far along the polyline
	CHECK(nearly(loco.speed(), 11.0 / 3.0, 1e-5));
	CHECK(nearly(h.pos.x, 11.0 / 3.0, 1e-4));
	REQUIRE(h.pendingValid);
	CHECK(h.applied.size() == 1);
}

TEST_CASE("dispatcher: a stationary-cap frame, status refusals, appearance table and the unported cases")
{
	LocoFixture f(kHuman);
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		// logic frame 0: the temporary desired-speed cap (RW +0x5C / +0x60, both 0 at construction) zeroes the speed
		h.frame = 0;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(loco.speed() == 0.0f);
		h.frame = 1;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(loco.speed() > 0.0f);
		loco.setDesiredSpeedCap(2.0f, 1000);
		MockHost h2;
		h2.place(0, 0, 0);
		Locomotor l2(f.tmpl());
		l2.setDesiredSpeedCap(1.0f, 1000);
		l2.locomotorMoveTowardsPosition(h2, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(l2.speed() == 1.0f); // accel 3.67 is cut back to the target 1.0
	}
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		h.motionDisabled = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(loco.speed() == 0.0f);
		CHECK(h.applied.empty());
		MockHost h2;
		h2.place(0, 0, 0);
		h2.status.insert(statusIndex("CONTESTING_BUILDING"));
		Locomotor l2(f.tmpl());
		l2.locomotorMoveTowardsPosition(h2, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(l2.speed() == 0.0f);
	}
	{
		// lane SHIP-1: HOVER and SHIP run their movers (RW 0x5E742E / 0x5E613F); their tests are the "SHIP mover" / "HOVER mover" cases
		MockHost h;
		LocoFixture bird("  Appearance = GIANT_BIRD\n");
		Locomotor l3(bird.tmpl());
		h.place(0, 0, 0);
		CHECK_NOTHROW(l3.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f)); // GIANT_BIRD: no mover runs (RW 0x5E9180)
		CHECK(l3.speed() == 0.0f);
	}
	{
		LocoFixture z(std::string(kHuman) + "  ZAxisBehavior = FIXED_SURFACE_RELATIVE_HEIGHT\n"); // lane SHIP-1 ported SURFACE_RELATIVE_HEIGHT
		Locomotor loco(z.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		CHECK_THROWS_WITH_AS(loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f), doctest::Contains("S-084"), std::logic_error);
	}
	{
		LocoFixture sw(std::string(kHuman) + "  ScalesWalls = Yes\n");
		Locomotor loco(sw.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		bool noted = false;
		for (const std::string &s : loco.unverified())
		{
			noted = noted || s.rfind("S-084", 0) == 0;
		}
		CHECK(noted);
	}
}

TEST_CASE("NO_Z_MOTIVE_FORCE: z is the terrain height; the pending position is the previous effective position with 2z - z0")
{
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	h.groundZ = 7.5f;
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
	CHECK(nearly(h.pos.z, 7.5, 1e-6));
	CHECK(nearly(h.pending.z, 2 * 7.5 - 0.0, 1e-5)); // 2 * z - the object's z before the transform was applied
	// a suppressed z (thrown projectile) keeps the z of the transform
	MockHost h2;
	h2.place(0, 0, 0);
	h2.groundZ = 9.0f;
	h2.zSuppressed = true;
	Locomotor l2(f.tmpl());
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
	CHECK(nearly(h2.pos.z, 0.0, 1e-6));
	// a path with an explicit Z skips handleBehaviorZ
	MockHost h3;
	h3.place(0, 0, 0);
	h3.groundZ = 4.0f;
	MockPath path;
	path.points = { Coord3D{ 0, 0, 0 }, Coord3D{ 100, 0, 0 } };
	path.explicitZ = true;
	h3.path = &path;
	Locomotor l3(f.tmpl());
	l3.locomotorMoveTowardsPosition(h3, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
	CHECK(nearly(h3.pos.z, 0.0, 1e-6));
}

TEST_CASE("model conditions: ACCELERATE then WALKING near the goal, DECELERATE while braking (RW 0x5E5BA7)")
{
	LocoFixture f(std::string(kHuman) + "  AccDecTrigger = 0.5\n  WalkDistance = 20\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	// far from the goal: speeding up from rest sets ACCELERATE
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 11.0f);
	CHECK(h.mc.count(mcIndex("ACCELERATE")) == 1);
	CHECK(h.mc.count(mcIndex("WALKING")) == 0);
	// inside WalkDistance the walking condition replaces it
	Locomotor l2(f.tmpl());
	MockHost h2;
	h2.place(0, 0, 0);
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 10, 0, 0 }, 10.0f, 11.0f);
	CHECK(h2.mc.count(mcIndex("WALKING")) == 1);
	CHECK(h2.mc.count(mcIndex("ACCELERATE")) == 0);
}

TEST_CASE("turn model conditions: TURN_LEFT / TURN_RIGHT and the high speed pair follow the turn direction (RW 0x5E5603)")
{
	LocoFixture f("  TurnTime = 2000\n  Acceleration = 510\n  Braking = 510\n  Appearance = TWO_LEGS\n  TurnPivotOffset = 0\n  EnableHighSpeedTurnModelconditions = Yes\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 0, 500, 0 }, 500.0f, 11.0f); // turning left, standing start: low speed
	CHECK(loco.turnDirection() == 1);
	CHECK(h.mc.count(mcIndex("TURN_LEFT")) == 1);
	CHECK(h.mc.count(mcIndex("TURN_RIGHT")) == 0);
	// a turn to the right replaces it
	Locomotor l2(f.tmpl());
	MockHost h2;
	h2.place(0, 0, 0);
	h2.mc.insert(mcIndex("TURN_LEFT"));
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 0, -500, 0 }, 500.0f, 11.0f);
	CHECK(h2.mc.count(mcIndex("TURN_RIGHT")) == 1);
	CHECK(h2.mc.count(mcIndex("TURN_LEFT")) == 0);
	// a turn-limited object plays no turn animation
	Locomotor l3(f.tmpl());
	MockHost h3;
	h3.place(0, 0, 0);
	h3.turnLimited = true;
	l3.locomotorMoveTowardsPosition(h3, Coord3D{ 0, 500, 0 }, 500.0f, 11.0f);
	CHECK(h3.mc.count(mcIndex("TURN_LEFT")) == 0);
	// turn-limited halves the turn: 0.5 * 2pi/10
	CHECK(nearly(h3.angle, 0.5 * 2.0 * kPi / 10.0, 2e-4));
}

TEST_CASE("Locomotor stops S-081 and S-084 are reported and pinned")
{
	const std::vector<std::string> stops = Locomotor::allStops();
	REQUIRE(stops.size() == 3);
	CHECK(stops[0].rfind("S-081:", 0) == 0);
	CHECK(stops[0].find("x87 and SSE") != std::string::npos);
	CHECK(stops[1].rfind("S-084:", 0) == 0);
	CHECK(stops[1].find("HOVER") == std::string::npos); // lane SHIP-1: the HOVER and SHIP movers are ported
	CHECK(stops[1].find("FLOATING_Z, SEA_LEVEL, SURFACE_RELATIVE_HEIGHT and ABSOLUTE_HEIGHT (the last three: lane SHIP-1;") != std::string::npos);
	CHECK(stops[1].find("GIANT_BIRD movers are GiantBirdAIUpdate's") != std::string::npos); // lane FLY-1
	CHECK(stops[1].find("ScalesWalls") != std::string::npos);
	CHECK(stops[2].rfind("S-1750:", 0) == 0);
	CHECK(stops[2].find("RW 0x5E7CC7") != std::string::npos);
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
	REQUIRE_FALSE(loco.unverified().empty());
	CHECK(loco.unverified()[0] == stops[0]); // every move reports S-081
	CHECK_THROWS_AS(Locomotor(nullptr), std::logic_error);
}

TEST_CASE("maintainCurrentPosition: a goal-less locomotor drops the walk's model conditions and its speed by appearance (RW 0x5E7CC7, lane EXIT-1)")
{
	const char *walk[] = { "TURN_LEFT", "TURN_RIGHT", "TURN_LEFT_HIGH_SPEED", "TURN_RIGHT_HIGH_SPEED", "ACCELERATE", "DECELERATE", "WALKING" };
	// legs: MOVING is cleared, the speed is zeroed, no constant calling; the turn / walk conditions only go while MOVING is already off (RW 0x5E7D30 tests
	// MOVING before the appearance switch clears it), so a second call takes them
	LocoFixture f(kHuman);
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(5, 6, 0);
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 11.0f);
	REQUIRE(loco.speed() > 0.0f);
	h.mc.insert(mcIndex("MOVING"));
	for (const char *c : walk)
	{
		h.mc.insert(mcIndex(c));
	}
	CHECK_FALSE(loco.locomotorMaintainCurrentPosition(h));
	CHECK(h.mc.count(mcIndex("MOVING")) == 0);
	CHECK(h.mc.count(mcIndex("TURN_LEFT")) == 1);
	CHECK(loco.speed() == 0.0f);
	CHECK((loco.flags() & (unsigned)LOCOMOTOR_FLAG_BIT2) != 0);
	CHECK((loco.flags() & (unsigned)LOCOMOTOR_FLAG_BRAKING) == 0);
	CHECK_FALSE(loco.locomotorMaintainCurrentPosition(h));
	for (const char *c : walk)
	{
		CHECK_MESSAGE(h.mc.count(mcIndex(c)) == 0, c);
	}
	REQUIRE(loco.unverified().size() >= 1);
	CHECK(loco.unverified().back() == Locomotor::allStops()[2]);
	// no physics module (obj+0x264 null): the conditions still go, MOVING and the speed stay, constant calling
	Locomotor l2(f.tmpl());
	MockHost h2;
	h2.physics = false;
	h2.place(0, 0, 0);
	l2.locomotorMoveTowardsPosition(h2, Coord3D{ 1000, 0, 0 }, 1000.0f, 11.0f);
	h2.mc.insert(mcIndex("MOVING"));
	CHECK(l2.locomotorMaintainCurrentPosition(h2));
	CHECK(h2.mc.count(mcIndex("MOVING")) == 1);
	CHECK(l2.speed() > 0.0f);
	// HOVER: the speed is zeroed, MOVING stays, constant calling (RW 0x5E7E26)
	LocoFixture fh("  Surfaces = GROUND\n  Appearance = HOVER\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
	Locomotor l3(fh.tmpl());
	MockHost h3;
	h3.place(0, 0, 0);
	h3.mc.insert(mcIndex("MOVING"));
	CHECK(l3.locomotorMaintainCurrentPosition(h3));
	CHECK(h3.mc.count(mcIndex("MOVING")) == 1);
	// TREADS: MOVING is cleared, the speed is kept (RW 0x5E7DFD enters the shared clear after the speed reset of the legged cases)
	LocoFixture ft("  Surfaces = GROUND\n  Appearance = TREADS\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Acceleration = 510\n  Braking = 510\n  TurnTime = 500\n");
	Locomotor l4(ft.tmpl());
	MockHost h4;
	h4.place(0, 0, 0);
	h4.setSpeed = 50.0f;
	l4.locomotorMoveTowardsPosition(h4, Coord3D{ 1000, 0, 0 }, 1000.0f, 11.0f);
	const float treadsSpeed = l4.speed();
	h4.mc.insert(mcIndex("MOVING"));
	CHECK_FALSE(l4.locomotorMaintainCurrentPosition(h4));
	CHECK(h4.mc.count(mcIndex("MOVING")) == 0);
	CHECK(l4.speed() == treadsSpeed);
}

// ---- lane SHIP-1: the SHIP and HOVER movers and the SEA_LEVEL z (RW 0x5E613F, 0x5E742E, 0x5E76AC case 1) ----------------------------------------------
namespace
{
// retail TransportShipLocomotor's numbers (data\ini\locomotor.ini: TurnTime 10000, Acceleration / Braking 6000, MinTurnSpeed 10%, SEA_LEVEL, SHIP)
const char *kShip = "  Surfaces = WATER DEEP_WATER\n  Speed = 100%\n  SpeedDamaged = 50%\n  SlowTurnRadius = 50.0\n  FastTurnRadius = 70.0\n  TurnTime = 10000\n"
					"  TurnTimeDamaged = 10500\n  TurnThreshold = 5\n  Acceleration = 6000\n  Braking = 6000\n  MinTurnSpeed = 10%\n  ZAxisBehavior = SEA_LEVEL\n"
					"  Appearance = SHIP\n  CanMoveBackwards = No\n";
}

TEST_CASE("SHIP mover: the turn slows the ship by 1 - min(|2 rel| / (3.5 x turn rate), 0.9) (RW 0x5E613F), not by the Wheels' 4/pi factor")
{
	LocoFixture f(kShip);
	// LocomotorSet speed 30: maximum 6 per frame, acceleration 6 / 30 frames = 0.2, turn rate 2 pi / 50 frames
	{
		// at rest, the goal 45 degrees to the left: 2 rel = pi / 2, |2 rel| / (3.5 x 2 pi / 50) = 3.57 is capped at 0.9: the target is 0.1 of 1.0 (the Wheels
		// mover's min(|2 rel| x 4 / pi, 1) = 1 would leave the ship at 0)
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 100, 0 }, 141.0f, 1.0f);
		CHECK(loco.speed() == 1.0f - 0.9f);
		MockHost hw;
		hw.setSpeed = 30.0f;
		hw.place(0, 0, 0);
		LocoFixture wheels(std::string(kShip) + "  Appearance = FOUR_WHEELS\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
		Locomotor lw(wheels.tmpl());
		lw.locomotorMoveTowardsPosition(hw, Coord3D{ 100, 100, 0 }, 141.0f, 1.0f);
		CHECK(lw.speed() == 0.0f);
	}
	{
		// the goal at (100, 10): rel = atan2(10, 100), doubled at rest; f = |2 rel| / (3.5 x 2 pi / 50) = 0.4532; the target 0.1 x (1 - f), below the
		// acceleration, is the speed after one frame
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 10, 0 }, 100.5f, 0.1f);
		const double rel2 = 2.0 * std::atan2(10.0, 100.0);
		const double fexp = rel2 / (3.5 * (2.0 * kPi / 50.0));
		CHECK(nearly(loco.speed(), (1.0 - fexp) * 0.1, 1e-8));
		CHECK(nearly(loco.speed(), 0.0546779, 1e-7)); // the value by hand: 0.1 x (1 - 0.199337 / 0.439823)
	}
	{
		// straight ahead: no slow-down, the speed is the acceleration (6 / 30 frames)
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(nearly(loco.speed(), 0.2, 1e-7));
	}
	{
		// a ship whose maximum speed is 0 does nothing more after the turn (no wheels-stopped notice, RW 0x5E6337)
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 0.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 1.0f);
		CHECK(h.wheelsStopped == 0);
		CHECK(loco.speed() == 0.0f);
	}
}

TEST_CASE("SEA_LEVEL: a ship floats at the water height, on dry ground at the ground height; a SUBMARINE dives by its speed; DISABLED_HELD keeps z (RW 0x5E781F)")
{
	LocoFixture f(kShip);
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		h.waterZ = 12.5f;
		h.groundZ = -3.0f;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(h.pos.z == 12.5f);
		CHECK(h.pending.z == 25.0f); // 2 x 12.5 - the z before (0)
		CHECK(loco.unverified().size() == 1); // S-081 alone: SEA_LEVEL is ported
	}
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = false;
		h.groundZ = 4.0f;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(h.pos.z == 4.0f);
	}
	{
		// SUBMARINE with PreferredHeight -10: (speed / (maxSpeed + 0.1) + 1) x -10 + water; at rest the speed after the frame is 0.2: (0.2 / 6.1 + 1) x -10
		LocoFixture sub(std::string(kShip) + "  PreferredHeight = -10\n");
		Locomotor loco(sub.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		h.waterZ = 20.0f;
		h.submarine = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(nearly(h.pos.z, (0.2 / 6.1 + 1.0) * -10.0 + 20.0, 1e-5));
		MockHost h2;
		h2.setSpeed = 30.0f;
		h2.place(0, 0, 0);
		h2.underwater = true;
		h2.waterZ = 20.0f;
		Locomotor l2(sub.tmpl());
		l2.locomotorMoveTowardsPosition(h2, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(h2.pos.z == 10.0f); // not a SUBMARINE: the preferred height is added as it is
	}
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 30.0f;
		h.place(0, 0, 0);
		h.underwater = true;
		h.waterZ = 12.5f;
		h.held = true;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 1000, 0, 0 }, 1000.0f, 6.0f);
		CHECK(h.pos.z == 0.0f);
	}
}

TEST_CASE("HOVER mover: the OTHER mover, then OVER_WATER follows the water under the object (RW 0x5E742E)")
{
	LocoFixture f("  Surfaces = AIR\n  TurnTime = 500\n  Acceleration = 2000\n  Braking = 2000\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Appearance = HOVER\n");
	LocoFixture other("  Surfaces = AIR\n  TurnTime = 500\n  Acceleration = 2000\n  Braking = 2000\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Appearance = OTHER\n");
	Locomotor loco(f.tmpl());
	Locomotor lo(other.tmpl());
	MockHost h, ho;
	h.place(0, 0, 0);
	ho.place(0, 0, 0);
	h.underwater = true;
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 50, 0 }, 112.0f, 11.0f);
	lo.locomotorMoveTowardsPosition(ho, Coord3D{ 100, 50, 0 }, 112.0f, 11.0f);
	// the same kinematics as OTHER
	CHECK(h.pos.x == ho.pos.x);
	CHECK(h.pos.y == ho.pos.y);
	CHECK(h.angle == ho.angle);
	CHECK(loco.speed() == lo.speed());
	CHECK(h.mc.count(mcIndex("OVER_WATER")) == 1);
	CHECK((loco.flags() & LOCOMOTOR_FLAG_OVER_WATER) == (unsigned)LOCOMOTOR_FLAG_OVER_WATER);
	// the condition changes only with the flag: cleared by hand, it is not set again while the flag stays
	h.mc.erase(mcIndex("OVER_WATER"));
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 50, 0 }, 100.0f, 11.0f);
	CHECK(h.mc.count(mcIndex("OVER_WATER")) == 0);
	// off the water: flag and condition go
	h.mc.insert(mcIndex("OVER_WATER"));
	h.underwater = false;
	loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 50, 0 }, 100.0f, 11.0f);
	CHECK(h.mc.count(mcIndex("OVER_WATER")) == 0);
	CHECK((loco.flags() & LOCOMOTOR_FLAG_OVER_WATER) == 0);
}

TEST_CASE("SURFACE_RELATIVE_HEIGHT / ABSOLUTE_HEIGHT: a tenth of the damped way to the preferred height above the surface each frame; CLIMBING above 0.5 (RW 0x5E7AF6)")
{
	// CrebainLocomotor's height fields: PreferredHeight 50, PreferredAttackHeight 25 (no CirclingRadius: the attack height never applies), damping 1
	LocoFixture f("  Surfaces = AIR\n  TurnTime = 500\n  Acceleration = 2000\n  Braking = 2000\n  PreferredHeight = 50\n  PreferredAttackHeight = 25\n"
				  "  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT\n  Appearance = HOVER\n");
	{
		Locomotor loco(f.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		h.groundZ = 10.0f;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		// aim 50 + 10, step (60 - 0) / 10 = 6
		CHECK(h.pos.z == 6.0f);
		CHECK(h.pending.z == 12.0f);
		CHECK(h.mc.count(mcIndex("CLIMBING")) == 1);
		// over water the surface is the water's height
		MockHost hw;
		hw.place(0, 0, 0);
		hw.groundZ = -20.0f;
		hw.underwater = true;
		hw.waterZ = 5.0f;
		Locomotor lw(f.tmpl());
		lw.locomotorMoveTowardsPosition(hw, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(hw.pos.z == 5.5f); // (55 - 0) / 10
		// at the preferred height: no step, CLIMBING goes
		MockHost h2;
		h2.place(0, 0, 0);
		h2.pos.z = 60.0f;
		h2.syncTransform();
		h2.groundZ = 10.0f;
		h2.mc.insert(mcIndex("CLIMBING"));
		Locomotor l2(f.tmpl());
		l2.locomotorMoveTowardsPosition(h2, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(h2.pos.z == 60.0f);
		CHECK(h2.mc.count(mcIndex("CLIMBING")) == 0);
	}
	{
		// ABSOLUTE_HEIGHT: the surface is 0
		LocoFixture a("  PreferredHeight = 30\n  ZAxisBehavior = ABSOLUTE_HEIGHT\n  Appearance = HOVER\n");
		Locomotor loco(a.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		h.groundZ = 10.0f;
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(h.pos.z == 3.0f);
		// BoatLocomotor: PreferredHeight 0 and no flag 8: the height is left alone
		LocoFixture b("  PreferredHeight = 0\n  ZAxisBehavior = ABSOLUTE_HEIGHT\n  Appearance = HOVER\n");
		Locomotor lb(b.tmpl());
		MockHost hb;
		hb.place(0, 0, 0);
		hb.pos.z = 7.0f;
		hb.syncTransform();
		lb.locomotorMoveTowardsPosition(hb, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(hb.pos.z == 7.0f);
	}
	{
		// PreferredHeightDamping 0.5: a twentieth of the way
		LocoFixture d("  PreferredHeight = 40\n  PreferredHeightDamping = 0.5\n  ZAxisBehavior = ABSOLUTE_HEIGHT\n  Appearance = HOVER\n");
		Locomotor loco(d.tmpl());
		MockHost h;
		h.place(0, 0, 0);
		loco.locomotorMoveTowardsPosition(h, Coord3D{ 100, 0, 0 }, 100.0f, 11.0f);
		CHECK(h.pos.z == 2.0f);
	}
}

TEST_CASE("calcPreferredHeight: the attack-height states 0 -> 1 -> 2 -> 0, and the weapon's status asked in state 1 alone (RW 0x5E5D9A)")
{
	// PreferredHeight 50, PreferredAttackHeight 25, CirclingRadius 100; the goal 50 away (m = 0.5)
	LocoFixture f("  PreferredHeight = 50\n  PreferredAttackHeight = 25\n  CirclingRadius = 100\n  ZAxisBehavior = ABSOLUTE_HEIGHT\n  Appearance = HOVER\n");
	Locomotor loco(f.tmpl());
	MockHost h;
	h.place(0, 0, 0);
	h.pos.z = 30.0f;
	h.weapon = true;
	const Coord3D goal{ 50, 0, 0 };
	// state 0: the goal inside the circling radius with an active AI state starts the descent; the preferred height, no status asked
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 1);
	CHECK(h.statusCalls == 0);
	// state 1: 0.5 x (50 - 25) + 25; the status is asked (READY_TO_FIRE: stay)
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 37.5f);
	CHECK(loco.attackHeightState() == 1);
	CHECK(h.statusCalls == 1);
	// a weapon that is not ready ends the descent: state 2
	h.weaponStatus = WEAPON_RELOADING_CLIP;
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 37.5f);
	CHECK(loco.attackHeightState() == 2);
	CHECK(h.statusCalls == 2);
	// state 2: min(0.5 x 2, 1) = 1: the preferred height; below it (30 above the surface) the state stays, no status asked
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 2);
	CHECK(h.statusCalls == 2);
	// at the preferred height the climb ends: state 0
	h.pos.z = 50.0f;
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 0);
	CHECK(h.statusCalls == 2);
	// an inactive AI state does not start the descent; no weapon resets the state; neither asks the status
	h.stateActive = false;
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 0);
	h.stateActive = true;
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 1);
	h.weapon = false;
	CHECK(loco.calcPreferredHeight(h, goal, 0.0f) == 50.0f);
	CHECK(loco.attackHeightState() == 0);
	CHECK(h.statusCalls == 2);
}

TEST_CASE("ambient flight: idle wings circle in both directions and climb without moving perched birds")
{
	for (int direction : { -1, 1 })
	{
		const std::string ini = "  Surfaces = AIR\n  Appearance = WINGS\n  MinSpeed = 50%\n  TurnTime = 1500\n  Acceleration = 1000\n  Braking = 1000\n  PreferredHeight = 81\n  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT\n  CirclingRadius = " + std::to_string(direction * 161) + "\n";
		LocoFixture f(ini.c_str());
		Locomotor loco(f.tmpl());
		MockHost h;
		h.setSpeed = 150;
		h.pos.z = 81;
		h.syncTransform();
		float swept = 0;
		float previous = 0;
		for (int frame = 0; frame < 180; ++frame)
		{
			CHECK(loco.locomotorMaintainCurrentPosition(h));
			const float a = std::atan2(h.pos.y, h.pos.x);
			if (frame > 10)
			{
				float step = a - previous;
				if (step > kPi) step -= 2 * kPi;
				if (step < -kPi) step += 2 * kPi;
				swept += step;
			}
			previous = a;
			CHECK(h.pos.z == 81);
		}
		// tolerance: 36 seconds at 75 world units/sec must traverse more than one full orbit of radius 161.
		CHECK(std::abs(swept) > 2 * kPi);
		// tolerance: the sign of accumulated angular travel selects the authored circling direction.
		CHECK((swept < 0) == (direction > 0));
		CHECK(std::find(loco.unverified().begin(), loco.unverified().end(), Locomotor::allStops()[2]) == loco.unverified().end());
		MockHost grounded;
		Locomotor climb(f.tmpl());
		CHECK(climb.locomotorMaintainCurrentPosition(grounded));
		CHECK(grounded.pos.x == 0);
		CHECK(grounded.pos.y == 0);
		// tolerance: the held-position height worker must start climbing from terrain on its first update.
		CHECK(grounded.pos.z > 0);
	}
	LocoFixture perched("  Surfaces = AIR\n  Appearance = WINGS\n  PreferredHeight = 0\n  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT\n");
	MockHost h;
	Locomotor idle(perched.tmpl());
	for (int frame = 0; frame < 30; ++frame) idle.locomotorMaintainCurrentPosition(h);
	CHECK(h.pos.x == 0);
	CHECK(h.pos.y == 0);
	CHECK(h.pos.z == 0);
}
