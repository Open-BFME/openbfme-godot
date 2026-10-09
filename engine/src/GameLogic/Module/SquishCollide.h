// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SquishCollide (lane HORDE-2): the crush / trample of BFME. A unit with SquishCollide is crushed by a crusher (a cavalry rider, a monster) that runs into it. Port of the
// RotWK body; the ZH file is Source/GameLogic/Object/Collide/SquishCollide.cpp (a tank kills infantry outright). Also the Object crush helpers the collide uses.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * SquishCollide::onCollide RW 0x8BFBAE (self = the module's object, other = the object that touched it), in order:
//       1. return when other is null or dead (+0x458 bit 0), other is KindOf HORDE (a horde object never crushes), self is dead, or self's physics motion is disabled
//          (self + 0x264 module byte + 0x5C);
//       2. return unless crusherLevel(other) > crushableLevel(self) (RW 0x695070 / 0x68D4D0);
//       3. the RotWK exemptions of other's AI (vslot 0x188 / the goal object) and ZH's hijacker / TNT exemptions of self's AI (stop S-580: not ported, the classes do not exist);
//       4. other must move toward self: facing(other) . (self - other) >= 0, or facing(other) . (self - recorded position of other) >= 0 (RW 0x70B9E0: (cos, sin, 0) of the
//          angle; RW 0x6725C9: the transform recorded for the previous frame when there is one);
//       5. unless other is RAMPAGING (status 57) or FLEE_OFF_MAP (50), has the model condition CHARGING (132) or its template has CrushAllies (+0x624), other must regard
//          self as ENEMIES (RW 0x68D7AB);
//       6. the shapes must touch with self's geometry shrunk to radius 5.0 (RW 0xBDAE58, both radii; geomCollidesWithGeom RW 0xAD2CE0);
//       7. canCrush(other) (RW 0x68D524) fails: RW 0x696800 (the "bump", lane COMBAT-3: the crusher's desired speed held at its current maximum for
//          LOGICFRAMES_PER_SECOND frames, outside a container only against an ENEMIES victim, whose contact attack RW 0x6962DB is S-1600) and return;
//       8. self's body may refuse (body slot 0x9C(other): ActiveBody RW 0x9B501B always yes; PorcupineFormationBodyModule RW 0x8C62BB is gated by RW 0x8C6244, which
//          returns false in 2.01, so it always says yes too);
//       9. other.onCrush(self) (RW 0x69320D, below); a 0-damage shockwave hit when other's RamPower (+0x618) > 0 (its handler RW 0x6968BC: S-1600); then other's CrushWeapon (Object + 0x3B8) fires at self
//          (RW 0x6CF328), or without one self takes 999999 CRUSH damage with DeathType CRUSHED from other; then self's CrushRevengeWeapon (+0x3BC) fires at other unless
//          self is flanked by other (RW 0x68FB63);
//   * crushableLevel RW 0x68D4D0: MountedCrushableLevel (+0x60C) when it is not 0xFF and the object has the model condition MOUNTED (214), else CrushableLevel (+0x60A),
//     plus the CRUSHABLE_LEVEL attribute modifier sum (type 0x19, truncated, added as a byte; lane COMBAT-3);
//   * crusherLevel RW 0x695070: 0 when the template has CrushOnlyWhileCharging (+0x60E) and neither the object nor its container has CHARGING (RW 0x694154);
//     MountedCrusherLevel (+0x60B) when not 0xFF and MOUNTED, else CrusherLevel (+0x609), plus the CRUSHER_LEVEL sum (0x17; a negative byte is 0; lane COMBAT-3);
//   * canCrush RW 0x68D524: the object, or the HORDE that contains it (walked up), must have an AI; MinCrushVelocityPercent (+0x51C) of THAT object <= 0 passes, else
//     its locomotor's current speed (loco + 0x40) must be at least MinCrushVelocityPercent * its maximum speed (RW 0x6624C9);
//   * onCrush RW 0x69320D: a horde member hands it to its horde; CrushKnockback (+0x524) > 0 knocks the victim back (lane COMBAT-3: Object::doKnockback RW 0x692223
//     with the angle (crusher angle + 0.3 * min(relativeAngle2D(victim), pi/2)) in degrees, CrushKnockback and CrushZFactor (+0x528)); with an AI the percentage
//     CrushDecelerationPercent (+0x520) is multiplied by the crusher's CRUSH_DECELERATE (9) and the victim's CRUSHED_DECELERATE (0x1A) products; above 0 the
//     locomotor slows: limit = currentMaxSpeed (RW 0x5E4137) - maxSpeed * percent (divided by the member count of a horde with more than one; with a
//     MINIMUM_CRUSH_VELOCITY (0x12) product m, scaled by (1 - p) / (1 - p * m), p = MinCrushVelocityPercent, both clamped to [0.05, 0.95]), clamped at 0; the speed
//     drops to the limit when above it and the limit is set as the desired speed cap for LOGICFRAMES_PER_SECOND frames (RW 0x5E39E6);
//   * the template defaults (ThingTemplate ctor RW 0x73FE16 / 0x7401AE): CrusherLevel 0, CrushableLevel 127, Mounted* 0xFF, UseCrushAttack yes, MinCrushVelocityPercent
//     0.01, CrushDecelerationPercent / CrushKnockback / RamPower 0, CrushZFactor 1, CrushAllies no; the weapons are made in initObject (RW 0x693E19-0x693E79).
// INFERENCE (stop S-580): the shape test is planar (cylinders / spheres as circles, boxes as oriented rectangles; RW 0xAD2CE0's height test is not read); the
// crush and revenge weapons fire through the object's ObjectWeapons (an object without a weapon set cannot fire them: reported).

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CollideModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class ThingTemplate;

// the crush fields of a template, read once per call from the template's field slots
struct CrushTemplateInfo
{
	int crusherLevel = 0;
	int crushableLevel = 127;
	int mountedCrusherLevel = 0xFF;
	int mountedCrushableLevel = 0xFF;
	bool useCrushAttack = true;
	bool crushOnlyWhileCharging = false;
	bool crushAllies = false;
	float minCrushVelocityPercent = 0.01f;
	float crushDecelerationPercent = 0.0f;
	float crushKnockback = 0.0f;
	float crushZFactor = 1.0f;
	float ramPower = 0.0f;
	float ramZMult = 0.0f;
	std::string crushWeapon;
	std::string crushRevengeWeapon;
	std::vector<std::string> errors; ///< fields whose value does not read
	static CrushTemplateInfo of(const ThingTemplate &tt);
	// the object's final-override template's, read once per game (GameLogic::crushInfo)
	static const CrushTemplateInfo &cached(const Object &obj);
};

namespace ObjectCrush
{
int crushableLevel(const Object &obj); // RW 0x68D4D0
int crusherLevel(const Object &obj);   // RW 0x695070
bool canCrush(const Object &obj);      // RW 0x68D524
// RW 0x69320D: `crusher` crushed `victim` (the horde of a member slows down; CrushKnockback flings the victim)
void onCrush(Object &crusher, Object &victim);
// RW 0x696800: `crusher` touched `victim` too slowly to crush it
void onBump(Object &crusher, Object &victim);
// the planar shape test of RW 0xAD2CE0 with `self`'s radii replaced by `selfRadius` (both)
bool shapesTouch(const Object &self, float selfRadius, const Object &other);
// the stop line S-580
const char *stopLine();
// the stop lines S-1600 / S-1601 (lane COMBAT-3)
std::vector<std::string> combat3StopLines();
} // namespace ObjectCrush

class SquishCollide : public BehaviorModule, public CollideModuleInterface
{
public:
	SquishCollide(Thing *thing, const ModuleData *data)
		: BehaviorModule(thing, data)
	{
	}
	static void registerClass(ModuleFactory &modules);
	CollideModuleInterface *getCollide() override { return this; }
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8BFBAE
};

// HordeMemberCollide (lane HORDE-2): physical contact keeps a horde's melee alive. RW onCollide 0x8C0518 (create RW 0x650E45, the shared collide data, no fields):
//   1. self's container's HordeContainInterface (contain vslot 0x7C) and its melee target id (slot 0x16C) must exist;
//   2. touching the melee target itself -> refresh (slot 0x164, RW 0x86BE9A: readiness for 3 * LOGICFRAMES_PER_SECOND frames);
//   3. nothing more when the target is already "in current melee" (slot 0x160, RW 0x870180);
//   4. touching a member of the target's container -> refresh with that member;
//   5. touching a member of (or) a HORDE that self regards as ALLIES (RW 0x8C05FA `cmp eax, 2`; the horde spec says enemy: the binary says allied) whose own horde is in
//      current melee with our target (its slot 0x160) and has a melee-attacking member (slot 0x15C) -> refresh with our target.
// The melee target id / readiness live in the port's HordeAIUpdate (stop S-586).
class HordeMemberCollide : public BehaviorModule, public CollideModuleInterface
{
public:
	HordeMemberCollide(Thing *thing, const ModuleData *data)
		: BehaviorModule(thing, data)
	{
	}
	static void registerClass(ModuleFactory &modules);
	CollideModuleInterface *getCollide() override { return this; }
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8C0518
};
