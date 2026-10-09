// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PhysicsBehavior (lane PHYS-1): RotWK's knockback / fling. Unlike ZH's PhysicsBehavior (mass, forces, friction, a collide module that pushes), RotWK's module is a
// dormant update that replays a precomputed Bezier flight once something flings the object. The ZH file name is kept (Source/GameLogic/Object/Behavior/
// PhysicsUpdate.cpp); nothing of the ZH body is used.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * registry: create RW 0x64E424 (0x68 bytes, constructor RW 0x792B69), data RW 0x64E45C -> RW 0x792559, field table RW 0xC30538, interface mask 1 (update only:
//     NOT a collide module, so it never pushes units apart); vtables 0xC306C4 (module), 0xC306B8 (update: RW 0x79350E);
//   * module data (RW 0x792559 defaults): FirstHeight +0x08 (1.3), SecondHeight +0x0C (1.3), FirstPercentIndent +0x10 (0.33), SecondPercentIndent +0x14 (0.66),
//     ShockStunnedTimeLow +0x18 (LOGICFRAMES_PER_SECOND), ShockStunnedTimeHigh +0x1C (2 * LOGICFRAMES_PER_SECOND), ShockStandingTime +0x20 (LOGICFRAMES_PER_SECOND),
//     BounceCount +0x24 (2), an unnamed float +0x28 (5.0), BounceFirstHeight +0x2C (1.3), BounceSecondHeight +0x30 (1.3), BounceFirstPercentIndent +0x34 (0.33),
//     BounceSecondPercentIndent +0x38 (0.66), CurveFlattenMinDist +0x3C (0), TumbleRandomly +0x40, OrientToFlightPath +0x41, IgnoreTerrainHeight +0x42,
//     FirstPercentHeight +0x44 (0.33), SecondPercentHeight +0x48 (0.66), GravityMult +0x4C (1.0), GroundHitFX +0x50, GroundBounceFX +0x54, AllowBouncing +0x58,
//     KillWhenRestingOnGround +0x59;
//   * the module (RW 0x792B69): the flight points (vector of Coord3D, +0x20), the start +0x2C, the landing +0x38, the speed +0x44, the apex +0x48, the point count +0x4C,
//     the step +0x50, the bounce index +0x54, the stun timer +0x58, the stunned flag +0x5C, AllowBouncing copied at +0x5D, two flags +0x5E / +0x5F, two words +0x60 /
//     +0x64 (the fling's extra arguments); asleep until a fling;
//   * fling RW 0x792DBD (velocity, a, b): see PhysicsBehavior::fling;
//   * flyTo RW 0x792997 (landing, apex, speed, a, b): see PhysicsBehavior::flyTo;
//   * the curve RW 0x79261A (recount, apex): see PhysicsBehavior::buildCurve;
//   * update RW 0x79350E: one point per frame (the object is put on point `step`, step + 1), the end of the flight RW 0x793372, the sleep RW 0x792A41 (1 while points
//     remain, else asleep).
//   * the shock stun (lane COMBAT-3): stun RW 0x792A69 (flag): the stunned flag +0x5C = flag unless the object's or its container's template ShockwaveResistance
//     (+0x620) is at least 100.0 (RW 0xBD88D8); a cleared flag also clears STUNNED_STANDING_UP (163), STUNNED_FLAILING (127) and STUNNED (128) and the timer +0x58;
//     standUp RW 0x792AFF (the shockwave handler's second hit on a stunned unit): STUNNED, the flag, the timer = ShockStandingTime, awake next frame;
//   * the update RW 0x79350E before the point step: the bounce flag +0x5E cleared; stunned with a timer above 0 and alive: --timer; below 1: with STUNNED it goes,
//     STUNNED_STANDING_UP comes and the timer is ShockStandingTime; without it STUNNED_STANDING_UP goes and stun(false);
//   * the end of the flight RW 0x793372 (points left): GroundHitFX at the object (RW 0x494615); FREEFALL (72) -> SPLATTED (122), FREEFALL and STUNNED_FLAILING
//     off; stunned with the timer below 1 (or dead): STUNNED_FLAILING and FREEFALL off, then alive: STUNNED and the timer GameLogicRandomValue(ShockStunnedTimeLow,
//     ShockStunnedTimeHigh) (RW 0x6D328E "PhysicsUpdate.cpp" 0x2F0), dead: DYING (62) and SPLATTED; then the bounce RW 0x793224 when AllowBouncing (+0x5D or the
//     data's) and the bounce index is below BounceCount, else the rest RW 0x79308D(1);
//   * the rest RW 0x79308D (final): the points, the step and the bounce index cleared; KillWhenRestingOnGround (or +0x5F): SPLATTED and Object::kill(8, 0)
//     (RW 0x698EC3); else, unless final, asleep; the fling's extra word +0x64 is fired with +0x60 at the object (RW 0x6CF590) and both are cleared;
//   * the bounce RW 0x793224 (final): awake next frame unless final, ++bounce index; fewer than 2 points: the rest; else the direction of the last curve step
//     (SSE, scaled by RW 0x441C56 WWMath::Inv_Sqrt on the x87), the next landing 0.5 * |landing - start| (planar, RW 0x403111) along it from the object, on the
//     ground; flyTo(that, apex * 0.35, speed * 0.85) (RW 0xC30378 / 0xC3037C) and the bounce flag;
//   * the sleep RW 0x792A41: awake while points remain or while stunned with a timer above 0;
//   * flyTo's transform record between its two updates (RW 0x6260E1) and the flight step's next position obj + 0x198 / + 0x1A6 (RW 0x793707 .. 0x793786);
//   * the fling's AI halt RW 0x792E0E .. 0x792E32: an AI whose machine's current state does not answer State slot 0x38 is idled (RW 0x5E821A(2), CMD_FROM_AI);
//   * Object::doKnockback RW 0x692223 (angle in degrees, power, z factor, projectile type): see ObjectKnockback::apply.
// NOT PORTED (stop S-782, reported by PhysicsBehavior::stopLine the first time a fling runs): OrientToFlightPath / TumbleRandomly (the look-at transform
// RW 0xB26610 / 0x70BA76: the position is set, the angle is not; one retail template), the pathfinder's landing clip RW 0x6EF8A8 and per step update RW 0x6F0741,
// RW 0x6630D7 (the AI's second machine + 0x34 put back: the port's AI has one machine), the status 70 call
// RW 0x693919, the drawable refresh RW 0x67449C after a stun change and the drawable's projectile type (+0x360: client),
// RW 0x6CF590 (no ported caller passes the fling's extra words), the shockwave handler RW 0x6968BC (weapons' ShockWave*, RamPower: S-1600) and the other fling
// callers (slow death, weapons, ...: their lanes). INFERENCE: State slot 0x38 answers false for every state the port runs (RW 0x9188EB for the idle, follow-path
// and busy states read here).
// INFERENCE: the highest terrain along the flight line (the terrain's vslot 0x40) is PROJ-1's partition-cell scan (stop S-362).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class StateHasher;
struct MultiIniFieldParse;

struct PhysicsBehaviorModuleData : public ModuleData
{
	float m_firstHeight = 1.3f;                 // +0x08
	float m_secondHeight = 1.3f;                // +0x0C
	float m_firstPercentIndent = 0.33f;         // +0x10
	float m_secondPercentIndent = 0.66f;        // +0x14
	unsigned m_shockStunnedTimeLow = 0;         // +0x18 (LOGICFRAMES_PER_SECOND, set in the constructor)
	unsigned m_shockStunnedTimeHigh = 0;        // +0x1C
	unsigned m_shockStandingTime = 0;           // +0x20
	int m_bounceCount = 2;                      // +0x24
	float m_unnamed28 = 5.0f;                   // +0x28 (no field row)
	float m_bounceFirstHeight = 1.3f;           // +0x2C
	float m_bounceSecondHeight = 1.3f;          // +0x30
	float m_bounceFirstPercentIndent = 0.33f;   // +0x34
	float m_bounceSecondPercentIndent = 0.66f;  // +0x38
	float m_curveFlattenMinDist = 0.0f;         // +0x3C
	bool m_tumbleRandomly = false;              // +0x40
	bool m_orientToFlightPath = false;          // +0x41
	bool m_ignoreTerrainHeight = false;         // +0x42
	float m_firstPercentHeight = 0.33f;         // +0x44
	float m_secondPercentHeight = 0.66f;        // +0x48
	float m_gravityMult = 1.0f;                 // +0x4C
	std::string m_groundHitFX;                  // +0x50
	std::string m_groundBounceFX;               // +0x54
	bool m_allowBouncing = false;               // +0x58
	bool m_killWhenRestingOnGround = false;     // +0x59

	PhysicsBehaviorModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PhysicsBehavior : public UpdateModule
{
public:
	PhysicsBehavior(Thing *thing, const PhysicsBehaviorModuleData *data);
	static void registerClass(ModuleFactory &modules);
	// the object's PhysicsBehavior, or null
	static PhysicsBehavior *find(Object &obj);
	static const char *stopLine();

	UpdateSleepTime update() override; // RW 0x79350E
	void crc(StateHasher &hasher) const override;

	// RW 0x792DBD: fling the object with `velocity` (per frame); `a` / `b` are kept (+0x60 / +0x64) when not 0
	void fling(const Coord3D &velocity, unsigned a = 0, unsigned b = 0);
	// RW 0x792997: fly to `landing` over a curve of height `apex` at `speed` per frame
	void flyTo(const Coord3D &landing, float apex, float speed, unsigned a = 0, unsigned b = 0);
	// RW 0x792A69: the shock stun on (unless the object resists shockwaves) or off (its model conditions cleared)
	void setStunned(bool stunned);
	// RW 0x792AFF: a stunned object stands up (STUNNED, ShockStandingTime)
	void standUp();
	// the stunned flag (+0x5C): RW 0x5E3A1B, the locomotor's "physics motion disabled"
	bool isStunned() const { return m_stunned; }
	int stunTimer() const { return m_stunTimer; }
	int bounceIndex() const { return m_bounceIndex; }
	// the object's or its container's template ShockwaveResistance (+0x620) is at least 100 (RW 0x792A69 / 0x792AFF)
	static bool resistsShockwave(const Object &obj, bool askContainer);

	const PhysicsBehaviorModuleData *data() const { return m_data; }
	const std::vector<Coord3D> &flightPoints() const { return m_points; }
	size_t step() const { return m_step; }
	const Coord3D &start() const { return m_start; }
	const Coord3D &landing() const { return m_landing; }
	float speed() const { return m_speed; }
	float apexHeight() const { return m_apex; }
	int pointCount() const { return m_pointCount; }
	bool isFlying() const { return !m_points.empty(); }

private:
	friend struct PhysicsTestAccess; // the hash mutation tests change one field at a time
	bool buildCurve(bool recount, float apex); // RW 0x79261A
	void endOfFlight();                         // RW 0x793372
	void rest(bool final);                      // RW 0x79308D
	void bounce(bool final);                    // RW 0x793224
	void setCondition(int bit, bool on);        // the object's model condition, the AI told (RW 0x68B53C -> AI vslot 0x264)
	UpdateSleepTime sleepTime() const;          // RW 0x792A41

	const PhysicsBehaviorModuleData *m_data;
	std::vector<Coord3D> m_points; // +0x20
	Coord3D m_start{};             // +0x2C
	Coord3D m_landing{};           // +0x38
	float m_speed = 0.0f;          // +0x44
	float m_apex = 0.0f;           // +0x48
	int m_pointCount = 0;          // +0x4C
	unsigned m_step = 0;           // +0x50
	int m_bounceIndex = 0;         // +0x54
	unsigned m_extraA = 0;         // +0x60
	unsigned m_extraB = 0;         // +0x64
	int m_stunTimer = 0;           // +0x58
	bool m_stunned = false;        // +0x5C
	bool m_allowBouncing = false;  // +0x5D
	bool m_bounced = false;        // +0x5E
	bool m_killOnRest = false;     // +0x5F (no ported caller sets it)
};

// Object::doKnockback (RW 0x692223, Object.cpp; lane COMBAT-3)
namespace ObjectKnockback
{
// `obj` is flung along `angleDegrees` with `power` per frame (z: power * zFactor), stunned and STUNNED_FLAILING; nothing without a PhysicsBehavior, while stunned
// or with a ShockwaveResistance of at least 100. `projectileType` (NONE, CATAPULT_ROCK, TREBUCHET_ROCK) only reaches the drawable (client). True when flung.
bool apply(Object &obj, float angleDegrees, float power, float zFactor, const char *projectileType = "NONE");
// RW 0x4B3D8D Object::relativeAngle2D: the signed angle from the object's facing to `point` (0 at the object's own position)
float relativeAngle2D(const Object &obj, const Coord3D &point);
} // namespace ObjectKnockback
