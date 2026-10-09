// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (ZH's DumbProjectileBehavior is the model; RotWK's BezierProjectileBehavior is the
// class its projectile objects use: 346 retail templates, no DumbProjectileBehavior and no MissileAIUpdate in the 2.01 data).
//
// BezierProjectileBehavior (RotWK ModuleFactory name "BezierProjectileBehavior", create proc RW 0x64AE35, data create proc RW 0x64AE70, data size 0xC0, module size 0x88, field
// table RW 0xC58368, interface mask 0x11 = UPDATE | the 0x10 bit; lane PROJ-1): a projectile object that flies a Bezier curve from the launch bone to the aim point, one path point per
// logic frame, and detonates its warhead weapon where the path ends (or a collision stops it).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; B1 Open-BFME-1 BezierProjectileBehavior*.cpp are the decompiled twins, the field names are the table's):
//   * data (ctor RW 0x85E55C): FirstHeight +8, SecondHeight +0xC (reals), FirstPercentIndent +0x10, SecondPercentIndent +0x14 (percent), CrushStyle +0x18, DieOnImpact +0x19 (bools), BounceCount
//     +0x1C (int), BounceDistance +0x20, BounceFirstHeight +0x24, BounceSecondHeight +0x28, BounceFirstPercentIndent +0x2C, BounceSecondPercentIndent +0x30, CurveFlattenMinDist +0x34
//     (reals), FinalStuckTime +0x38, PreLandingStateTime +0x3C (durations), InvisibleFrames +0x40, FadeInTime +0x44 (unsigned), TumbleRandomly +0x48 (false), OrientToFlightPath +0x49 (TRUE),
//     DetonateCallsKill +0x4A, GarrisonHitKillCount +0x4C, GarrisonHitKillRequiredKindOf +0x50, GarrisonHitKillForbiddenKindOf +0x6C, GarrisonHitKillFX +0x88, FlightPathAdjustDistPerSecond +0x8C
//     (velocity), IgnoreTerrainHeight +0x90, FirstPercentHeight +0x94 (0.33), SecondPercentHeight +0x98 (0.66), GroundHitFX +0x9C, GroundBounceFX +0xA0, GroundHitWeapon +0xA4,
//     GroundBounceWeapon +0xA8, PreLandingEmotion +0xAC (-1), PreLandingEmotionRadius +0xB0, PostLandingStateTime +0xB4, PostLandingEmotion +0xB8 (-1), PostLandingEmotionRadius +0xBC. There is no
//     MaxLifespan: a projectile lives until its path ends.
//   * module (ctor RW 0x85F899): +0x28 launcher id, +0x2C origin position (the launcher's position when it was launched), +0x38 victim id, +0x3C the FIRING weapon template (ctx + 4 of the
//     nugget: WeaponSpeed, MinWeaponSpeed, MaxWeaponSpeed, ScaleWeaponSpeed and the ranges are read from it), +0x40 the warhead template, +0x44 the path (12 byte points), +0x50 path start, +0x5C
//     path end, +0x68 path speed, +0x6C segment count, +0x70 current step, +0x74 the launcher's weapon bonus condition mask, +0x78 the bounce (alternate curve) index, +0x7C the ids hit by a CrushStyle
//     projectile, +0x80 hasDetonated, +0x84 the height scale; the constructor puts the update to sleep (UPDATE_SLEEP_FOREVER) until it is launched.
//   * the projectile interface (vtable RW 0xC585E0): slot 0 launch RW 0x85E99A, slot 1 projectileIsArmed (true, RW 0x8BD372), slot 2 getLauncherID (RW 0x9F9F16), slot 3 handleCollision RW
//     0x85FDB7, slot 4 projectile can collide RW 0x85EF6A, slot 5 projectileNowJammed RW 0x85E54D (kill, damage 8 / death 0).
//   * launch RW 0x85E99A -> positionProjectileForLaunch (RW 0x6CB490) -> projectileFireAtObjectOrPosition RW 0x85EBEE (the aim point, the speed, the flight path) -> model condition 154
//     THROWN_PROJECTILE and object status 5 (NO_ATTACK) on the projectile. update RW 0x85F28A: the step that follows the path (orientation from the neighbouring points RW 0xB26610), the
//     pre-landing state (model condition 155 ABOUT_TO_HIT, PreLandingStateTime frames before the end), FlightPathAdjustDistPerSecond (the end follows the victim), the victim proximity test
//     (RW 0x85F737..0x85F783 compares the length of the victim's WORLD position, not its distance from the projectile, with radius + speed: the binary's own quirk, kept), the end of the path
//     calls handleCollision(null). detonate RW 0x85F08A. calcFlightPath RW 0x85E658. bounce RW 0x85FCBB, landed RW 0x85F9BE.
// DONOR: ZH DumbProjectileBehavior.cpp (the shape of launch / update / detonate / calcFlightPath), B1 BezierProjectileBehavior*.cpp.
//
// WHAT IS INFERENCE / NOT PORTED (stops S-360 .. S-366, docs/STOPS.md): see ProjectileStops.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
class WeaponTemplate;
class StateHasher;

// the projectile update interface of RW vtable 0xC585E0 (ZH ProjectileUpdateInterface)
class ProjectileUpdateInterface
{
public:
	virtual ~ProjectileUpdateInterface() = default;
	// RW 0x85E99A. `launcher` may be null (the projectile is then destroyed by the positioning step, like the binary)
	virtual void projectileLaunchAtObjectOrPosition(const Object *victim, const Coord3D *victimPos, Object *launcher, int wslot, int barrel, const WeaponTemplate *weapon,
		const WeaponTemplate *warhead) = 0;
	virtual bool projectileIsArmed() const = 0;
	virtual ObjectID projectileGetLauncherID() const = 0;
	// RW 0x85FDB7: the projectile hit `other` (null: the ground or the end of the path)
	virtual bool projectileHandleCollision(Object *other) = 0;
	// RW 0x85EF6A (the weapon's ProjectileCollidesWith test is not ported, S-364)
	virtual bool projectileCanCollideWith(const Object *other) const = 0;
	virtual void projectileNowJammed() = 0;

	// lane PROJ-2, read by the client snapshot only (GameClient/LogicSnapshot) between frames:
	// RW 0x85F6E7 .. 0x85F71E: the update leaves the drawable's look-ahead point on the object (+0x198, flag +0x1A6, cleared by every recordTransform):
	// true when it was set during logic frame `frame`
	virtual bool projectilePendingPositionOfFrame(unsigned frame, Coord3D &out) const = 0;
	// RW 0x85EE00 .. 0x85EF31 (the drawable's fade in projectileFireAtObjectOrPosition): the logic frame of the last launch or bounce (0: never), the launcher
	// (+0x28), the end of the flight (+0x5C) and its segment count (+0x6C)
	struct ClientInfo
	{
		unsigned fireFrame = 0;
		ObjectID launcher = INVALID_ID;
		Coord3D end{ 0.0f, 0.0f, 0.0f };
		int segments = 0;
	};
	virtual ClientInfo projectileClientInfo() const = 0;
};

class BezierProjectileBehaviorModuleData : public ModuleData
{
public:
	float m_firstHeight = 0.0f;               // +0x08
	float m_secondHeight = 0.0f;              // +0x0C
	float m_firstPercentIndent = 0.0f;        // +0x10
	float m_secondPercentIndent = 0.0f;       // +0x14
	bool m_crushStyle = false;                // +0x18
	bool m_dieOnImpact = false;               // +0x19
	int m_bounceCount = 0;                    // +0x1C
	float m_bounceDistance = 0.0f;            // +0x20
	float m_bounceFirstHeight = 0.0f;         // +0x24
	float m_bounceSecondHeight = 0.0f;        // +0x28
	float m_bounceFirstPercentIndent = 0.0f;  // +0x2C
	float m_bounceSecondPercentIndent = 0.0f; // +0x30
	float m_curveFlattenMinDist = 0.0f;       // +0x34
	unsigned m_finalStuckTime = 0;            // +0x38 frames
	unsigned m_preLandingStateTime = 0;       // +0x3C frames
	unsigned m_invisibleFrames = 0;           // +0x40
	unsigned m_fadeInTime = 0;                // +0x44
	bool m_tumbleRandomly = false;            // +0x48
	bool m_orientToFlightPath = true;         // +0x49
	bool m_detonateCallsKill = false;         // +0x4A
	unsigned m_garrisonHitKillCount = 0;      // +0x4C
	KindOfMaskType m_garrisonHitKillRequiredKindOf{}; // +0x50
	KindOfMaskType m_garrisonHitKillForbiddenKindOf{}; // +0x6C
	std::string m_garrisonHitKillFX;          // +0x88
	float m_flightPathAdjustDistPerFrame = 0.0f; // +0x8C
	bool m_ignoreTerrainHeight = false;       // +0x90
	float m_firstPercentHeight = 0.33f;       // +0x94
	float m_secondPercentHeight = 0.66f;      // +0x98
	std::string m_groundHitFX;                // +0x9C
	std::string m_groundBounceFX;             // +0xA0
	std::string m_groundHitWeapon;            // +0xA4 (a weapon template name; "None" or unknown: none)
	std::string m_groundBounceWeapon;         // +0xA8
	int m_preLandingEmotion = -1;             // +0xAC
	float m_preLandingEmotionRadius = 0.0f;   // +0xB0
	unsigned m_postLandingStateTime = 0;      // +0xB4
	int m_postLandingEmotion = -1;            // +0xB8
	float m_postLandingEmotionRadius = 0.0f;  // +0xBC

	static void buildFieldParse(MultiIniFieldParse &p);
};

class BezierProjectileBehavior : public UpdateModule, public ProjectileUpdateInterface
{
public:
	BezierProjectileBehavior(Thing *thing, const BezierProjectileBehaviorModuleData *data);
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	// ProjectileUpdateInterface
	void projectileLaunchAtObjectOrPosition(const Object *victim, const Coord3D *victimPos, Object *launcher, int wslot, int barrel, const WeaponTemplate *weapon,
		const WeaponTemplate *warhead) override;
	bool projectileIsArmed() const override { return true; }
	ObjectID projectileGetLauncherID() const override { return m_launcherID; }
	bool projectileHandleCollision(Object *other) override;
	bool projectileCanCollideWith(const Object *other) const override;
	void projectileNowJammed() override;
	bool projectilePendingPositionOfFrame(unsigned frame, Coord3D &out) const override;
	ClientInfo projectileClientInfo() const override;
	ProjectileUpdateInterface *getProjectileUpdateInterface() override { return this; }

	const BezierProjectileBehaviorModuleData *data() const { return m_data; }
	// the state the tests and the draw side read
	const std::vector<Coord3D> &flightPath() const { return m_flightPath; }
	size_t currentStep() const { return (size_t)m_currentFlightPathStep; }
	int segments() const { return m_flightPathSegments; }
	float flightSpeed() const { return m_flightPathSpeed; }
	const Coord3D &flightStart() const { return m_flightPathStart; }
	const Coord3D &flightEnd() const { return m_flightPathEnd; }
	ObjectID victimID() const { return m_victimID; }
	const WeaponTemplate *warhead() const { return m_warhead; }
	bool hasDetonated() const { return m_hasDetonated; }
	int bounceIndex() const { return m_altCurve; }
	float heightScale() const { return m_heightScale; }

	// RW 0x85EBEE
	void projectileFireAtObjectOrPosition(const Object *victim, const Coord3D *victimPos);
	// RW 0x85E658: false never happens in the binary (kept for the shape)
	bool calcFlightPath(bool recalcNumSegments);
	// RW 0x85F08A
	void detonate();

private:
	friend struct BezierProjectileTestAccess; // the mutation tests of the state hash change one field at a time
	void bounce(bool flag);   // RW 0x85FCBB
	void landed();            // RW 0x85F9BE
	void positionForLaunch(Object *launcher, int wslot, int barrel); // RW 0x6CB490
	void calcLaunchTransform(const Object &launcher, int wslot, int barrel, Coord3D &pos, float basis[9]); // RW 0x6CAB85
	Coord3D aimPosition(const Object &victim);                       // RW 0x6CB85A with flag 1
	UpdateSleepTime sleepTime() const;                               // RW 0x85EBAF
	bool alreadyHit(ObjectID id) const;

	const BezierProjectileBehaviorModuleData *m_data;
	ObjectID m_launcherID = INVALID_ID;       // +0x28
	Coord3D m_originPos{ 0.0f, 0.0f, 0.0f };  // +0x2C
	ObjectID m_victimID = INVALID_ID;         // +0x38
	const WeaponTemplate *m_weapon = nullptr; // +0x3C
	const WeaponTemplate *m_warhead = nullptr; // +0x40
	std::vector<Coord3D> m_flightPath;        // +0x44
	Coord3D m_flightPathStart{ 0.0f, 0.0f, 0.0f }; // +0x50
	Coord3D m_flightPathEnd{ 0.0f, 0.0f, 0.0f };   // +0x5C
	float m_flightPathSpeed = 0.0f;           // +0x68
	int m_flightPathSegments = 0;             // +0x6C
	int m_currentFlightPathStep = 0;          // +0x70
	unsigned m_bonusFlags = 0;                // +0x74
	int m_altCurve = 0;                       // +0x78
	std::vector<ObjectID> m_hitList;          // +0x7C
	bool m_hasDetonated = false;              // +0x80
	float m_heightScale = 1.0f;               // +0x84
	// lane PROJ-2: the object's look-ahead point (RW object +0x198) and the frame it was set (the +0x1A6 flag of that frame), the frame of the last launch
	Coord3D m_pendingPosition{ 0.0f, 0.0f, 0.0f };
	unsigned m_pendingFrame = 0;
	bool m_pendingValid = false;
	unsigned m_fireFrame = 0;
};

namespace ProjectileModules
{
void registerAll(ModuleFactory &modules);
// the stop lines of docs/STOPS.md S-360 .. S-379 that apply to a game
std::vector<std::string> stops();
} // namespace ProjectileModules

// WWMath::Inv_Sqrt (RW 0x441C56): the magic-constant guess and three x87 Newton steps at 24 bits; `value` must not be 0
float ProjectileInvSqrt(float value);
// The highest terrain along the line from a to b (the terrain's vslot 0x40; INFERENCE S-362: PROJ-1's partition-cell scan); lane PHYS-1's fling curve uses it too
class GameLogic;
float ProjectileHighestAlongLine(const GameLogic &logic, const Coord3D &a, const Coord3D &b);
