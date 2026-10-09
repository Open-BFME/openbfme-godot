// OpenBFME. GPL-3.0.
//
// Locomotor data: the Locomotor INI block, the template store and LocomotorSet parsing. Port of ZH
// GameEngine/Include/GameLogic/Locomotor.h and Source/GameLogic/Object/Locomotor.cpp
// (LocomotorTemplate, LocomotorStore) as changed by RotWK. Lane HORDE-1 (spec horde-and-movement.md
// 1.5, 2.13, checklist step 2). The kinematic `Locomotor` instance is in the same header (step 4).
//
// TARGET FACTS (RotWK game.dat; stop S-001 caveat applies to every address):
//   * the Locomotor block parser is RW 0x5E8276 (registered under "Locomotor", node RW 0xD9E034);
//     the field table is RW 0xBF4478 with 88 rows; the template is 0x154 bytes, constructed at
//     RW 0x5E4326 and post-processed after the field parse at RW 0x5E3143.
//   * TheLocomotorStore is RW 0xDE369C. A NULL store throws INIException(3, "TheLocomotorStore==NULL")
//     (RW 0x5E8290-0x5E82AC).
//   * load type handling for an existing name (RW 0x5E82E7-0x5E83EE): CREATE_OVERRIDES makes a new
//     override of the final override (RW 0x5E4A74); RELOAD (5) replaces it with a fresh template;
//     any other load type RETURNS WITHOUT READING THE BLOCK, so its lines then reach the top-level
//     dispatcher ("Unknown block ...") exactly as in retail. A new name under CREATE_OVERRIDES is
//     marked as an override (+8).
//   * template names are case-sensitive (NameKeyGenerator hash h = 33*h + c, strcmp compare:
//     RW 0x548538 / 0x5487EC).
//   * LocomotorSet (RW 0x73BF6B, ThingTemplate field): an entry {Locomotor, Condition, Speed} is
//     parsed with the RW 0xBF4BE0 table (Condition and Locomotor are AsciiStrings, Speed a Real),
//     then (RW 0x5E9AB1): the object must have an AIUpdate-type module (error code 3 "Attempted
//     to specify a locomotor for object %s without an AIUpdate\tblock."), Condition is resolved
//     with scanIndexList over the SET_* names RW 0xDA0530 (an empty Condition is the index list's
//     own error: the entry has NO default condition in RotWK), the Locomotor name is looked up in
//     TheLocomotorStore (an unknown name is NOT an error: a null template is stored), a condition
//     already specified in the AIUpdate's map is an error unless the load type is CREATE_OVERRIDES
//     or CHILD_OBJECT ("re-specifying a LocomotorSet\tis no longer allowed"), then the template list
//     of that condition is replaced by the one template and the speed stored. Spec 1.5 says the
//     Condition default is SET_NORMAL: that is wrong for the RotWK parser (RW 0x5E9B90 clears
//     both strings to "" and 0x5E9ACB..0x5E9B0E passes the empty string to scanIndexList).
//
// DONOR FACTS (ZH Locomotor.cpp): LocomotorTemplate / LocomotorStore shape, newOverride,
// getFinalOverride, the percent / velocity / duration field kinds. ZH stores a Locomotor's speed in
// the template; RotWK takes it from the LocomotorSet entry (spec 2.13).

#pragma once

#include "Common/INI.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ---- name registries (the binary's full lists) -----------------------------------------------------
// RW 0xD9E008. Bit i of Surfaces is name i.
extern const char *const TheLocomotorSurfaceNames[];
// RW 0xD9DF1C
extern const char *const TheLocomotorZAxisNames[];
// RW 0xD9DEEC
extern const char *const TheLocomotorAppearanceNames[];
// RW 0xD9DFCC
extern const char *const TheLocomotorFormationPriorityNames[];
// RW 0xDA0530: the SET_* names a LocomotorSet Condition (and ForcedLocomotorSet) is an index into.
extern const char *const TheLocomotorSetNames[];

enum LocomotorSurfaceType
{
	LOCOMOTORSURFACE_GROUND = 1 << 0,
	LOCOMOTORSURFACE_WATER = 1 << 1,
	LOCOMOTORSURFACE_CLIFF = 1 << 2,
	LOCOMOTORSURFACE_AIR = 1 << 3,
	LOCOMOTORSURFACE_RUBBLE = 1 << 4,
	LOCOMOTORSURFACE_OBSTACLE = 1 << 5,
	LOCOMOTORSURFACE_IMPASSABLE = 1 << 6,
	LOCOMOTORSURFACE_DEEP_WATER = 1 << 7,
	LOCOMOTORSURFACE_WALL_RAILING = 1 << 8
};

enum LocomotorZAxisBehavior
{
	Z_NO_Z_MOTIVE_FORCE = 0,
	Z_SEA_LEVEL,
	Z_SURFACE_RELATIVE_HEIGHT,
	Z_ABSOLUTE_HEIGHT,
	Z_FIXED_SURFACE_RELATIVE_HEIGHT,
	Z_FIXED_ABSOLUTE_HEIGHT,
	Z_FIXED_RELATIVE_TO_GROUND_AND_BUILDINGS,
	Z_RELATIVE_TO_HIGHEST_LAYER,
	Z_FLOATING_Z,
	Z_SCALING_WALLS
};

enum LocomotorAppearance
{
	LOCO_LEGS_TWO = 0,
	LOCO_WHEELS_FOUR = 1,
	LOCO_HOVER = 2,
	LOCO_WINGS = 3,
	LOCO_LEGS_FOUR_HUGE = 4,
	LOCO_GIANT_BIRD = 5,
	LOCO_HORDE = 6,
	LOCO_LEGS_TWO_HUGE = 7,
	LOCO_TREADS = 8,
	LOCO_SHIP = 9,
	LOCO_OTHER = 10
};

enum LocomotorFormationPriority
{
	FORMATION_NO_FORMATION = 0,
	FORMATION_CAVALRY1, FORMATION_CAVALRY2, FORMATION_CAVALRY3,
	FORMATION_MELEE1, FORMATION_MELEE2, FORMATION_MELEE3,
	FORMATION_RANGED1, FORMATION_RANGED2, FORMATION_RANGED3,
	FORMATION_ARTILLERY1, FORMATION_ARTILLERY2, FORMATION_ARTILLERY3,
	FORMATION_UNUSED
};

enum LocomotorSetType
{
	LOCOMOTORSET_NORMAL = 0,
	LOCOMOTORSET_NORMAL_UPGRADED,
	LOCOMOTORSET_FREEFALL,
	LOCOMOTORSET_WANDER,
	LOCOMOTORSET_PANIC,
	LOCOMOTORSET_TAXIING,
	LOCOMOTORSET_SUPERSONIC,
	LOCOMOTORSET_MOUNTED,
	LOCOMOTORSET_ENRAGED,
	LOCOMOTORSET_SCARED,
	LOCOMOTORSET_CONTAINED,
	LOCOMOTORSET_COMBO,
	LOCOMOTORSET_COMBO2,
	LOCOMOTORSET_COMBO3,
	LOCOMOTORSET_WALL_SCALING,
	LOCOMOTORSET_CHANGING_FIRINGARC,
	LOCOMOTORSET_BURNINGDEATH,
	LOCOMOTORSET_COUNT
};

// ---- LocomotorTemplate -------------------------------------------------------------------------------
// One Locomotor INI block (RW 0x154 bytes). Plain standard-layout struct: the FieldParse offsets are
// offsetof() of these members, in the table order of RW 0xBF4478. Times are frames (5/s), speeds and
// accelerations per frame, angles radians (the INI parsers convert; spec 2.13).
struct LocomotorTemplate
{
	LocomotorTemplate(); // RW 0x5E4326 defaults

	// ---- bookkeeping (RW +4, +8, +0x10) -----
	std::string m_name;                          // +0x10 (set by the block parser before the fields)
	std::shared_ptr<LocomotorTemplate> m_override; // +4: the next override in the chain
	bool m_isOverride = false;                   // +8

	// ---- fields, in RW table order (RW offset in the comment) -----
	unsigned m_surfaces;               // 14 Surfaces: bit set over TheLocomotorSurfaceNames
	float m_maxSpeed;                  // 20 Speed (VelocityReal; ground movement does not read it, spec 2.13)
	float m_lookAheadMult;             // 18 LookAheadMult
	bool m_nonDirtyTransform;          // 1C NonDirtyTransform
	float m_maxSpeedDamaged;           // 24 SpeedDamaged (percent)
	unsigned m_turnTime;               // 2C TurnTime (frames)
	unsigned m_turnTimeDamaged;        // 30 TurnTimeDamaged (frames; 0 after the parse means "= TurnTime")
	float m_slowTurnRadius;            // 34 SlowTurnRadius
	float m_fastTurnRadius;            // 38 FastTurnRadius
	float m_turnThreshold;             // 3C TurnThreshold (angle, radians)
	float m_turnThresholdHighSpeed;    // 40 TurnThresholdHS (angle)
	unsigned m_acceleration;           // 44 Acceleration (frames)
	float m_lift;                      // 48 Lift (percent)
	float m_liftDamaged;               // 4C LiftDamaged (percent; < 0 after the parse means "= Lift")
	unsigned m_braking;                // 50 Braking (frames)
	float m_minSpeed;                  // 28 MinSpeed (percent)
	float m_minTurnSpeed;              // 54 MinTurnSpeed (percent)
	float m_preferredHeight;           // 58 PreferredHeight
	float m_preferredHeightDamping;    // 60 PreferredHeightDamping
	float m_preferredAttackHeight;     // 5C PreferredAttackHeight
	float m_circlingRadius;            // 64 CirclingRadius
	float m_speedLimitZ;               // 68 SpeedLimitZ (VelocityReal)
	float m_maxThrustAngle;            // 6C MaxThrustAngle (angle)
	int m_zAxisBehavior;               // 70 ZAxisBehavior (LocomotorZAxisBehavior)
	int m_appearance;                  // 74 Appearance (LocomotorAppearance)
	int m_formationPriority;           // 78 FormationPriority (LocomotorFormationPriority)
	float m_accDecTrigger;             // 7C AccDecTrigger
	float m_walkDistance;              // 80 WalkDistance
	float m_maxOverlappedHeight;       // A4 MaxOverlappedHeight
	float m_maxTurnWithoutReform;      // 84 MaxTurnWithoutReform (angle)
	float m_accelerationPitchLimit;    // 88 AccelerationPitchLimit (angle)
	float m_bounceAmount;              // 8C BounceAmount (angular velocity)
	float m_pitchStiffness;            // 90
	float m_rollStiffness;             // 94
	float m_pitchDamping;              // 98
	float m_rollDamping;               // 9C
	float m_pitchInDirectionOfZVelFactor; // A0
	float m_forwardVelocityPitchFactor;   // A8
	float m_lateralVelocityRollFactor;    // AC
	float m_forwardAccelerationPitchFactor; // B0
	float m_lateralAccelerationRollFactor;  // B4
	float m_uniformAxialDamping;       // B8
	float m_turnPivotOffset;           // BC
	bool m_apply2DFrictionWhenAirborne; // D2
	bool m_downhillOnly;               // D3
	bool m_allowAirborneMotiveForce;   // D1
	bool m_locomotorWorksWhenDead;     // D0
	int m_airborneTargetingHeight;     // C0 (Int)
	bool m_stickToGround;              // D4
	int m_canMoveBackwards;            // D8: 0, 1 (Yes) or a 1..4 value (RW 0x5E3183)
	bool m_hasSuspension;              // DC
	float m_frontWheelTurnAngle;       // E8 (angle)
	float m_maximumWheelExtension;     // E0
	float m_maximumWheelCompression;   // E4
	float m_closeEnoughDist;           // C4
	bool m_closeEnoughDist3D;          // C8
	float m_slideIntoPlaceTime;        // CC (DurationReal, frames, no ceil)
	bool m_crewPowered;                // EC
	bool m_useTerrainSmoothing;        // ED
	float m_wanderWidthFactor;         // F0
	float m_wanderLengthFactor;        // F4
	float m_wanderAboutPointRadius;    // F8
	float m_burningDeathRadius;        // FC
	bool m_burningDeathIsCavalry;      // 100
	float m_chargeSpeed;               // 104 (percent)
	bool m_chargeAvailable;            // 108
	bool m_chargeIgnoresCondition;     // 109
	bool m_enableHighSpeedTurnModelconditions; // 10A
	bool m_waitForFormation;           // 10B
	float m_rudderCorrectionDegree;    // 10C
	float m_rudderCorrectionRate;      // 110
	float m_elevatorCorrectionDegree;  // 114
	float m_elevatorCorrectionRate;    // 118
	float m_aeleronCorrectionDegree;   // 11C
	float m_aeleronCorrectionRate;     // 120
	float m_swoopStandoffRadius;       // 124
	float m_swoopStandoffHeight;       // 128
	float m_swoopTerminalVelocity;     // 12C
	float m_swoopAccelerationRate;     // 130
	float m_swoopSpeedTuningFactor;    // 134
	float m_backingUpSpeed;            // 138 (percent)
	bool m_backingUpStopWhenTurning;   // 13C
	float m_backingUpDistanceMin;      // 140
	float m_backingUpDistanceMax;      // 144
	float m_backingUpAngle;            // 148 (Real; multiplied by pi at use, spec 1.5)
	float m_riverModifier;             // 14C (percent)
	bool m_scalesWalls;                // 150
	bool m_turnWhileMoving;            // 151

	// RW 0x5E3143, run after the field parse.
	void applyParseDefaults();

	// The field table (RW 0xBF4478): terminated by a NULL row. 88 rows.
	static const FieldParse *getFieldParse();
	static void parseCanMoveBackwards(INI *ini, void *instance, void *store, const void *userData);

	// ZH Overridable::getFinalOverride.
	const LocomotorTemplate *getFinalOverride() const;
	LocomotorTemplate *getFinalOverride();
};

// ---- LocomotorStore (ZH LocomotorStore; global TheLocomotorStore, RW 0xDE369C) ---------------------------
class LocomotorStore
{
public:
	// RW 0x5E67CC: nullptr when absent. Returns the BASE template (not its final override), like
	// the binary; use getFinalOverride() when reading values.
	const LocomotorTemplate *findLocomotorTemplate(const std::string &name) const;
	LocomotorTemplate *findLocomotorTemplate(const std::string &name);

	// RW 0x5E8276. Reads the rest of one `Locomotor <name> ... End` block (header already read).
	void parseLocomotorTemplateDefinition(INI *ini);
	static void parseLocomotorTemplateDefinitionGlobal(INI *ini); // throws code 3 when TheLocomotorStore is null

	size_t size() const { return m_templates.size(); }
	// Names in lexical order (tests, diagnostics).
	std::vector<std::string> names() const;
	// Templates replaced by a RELOAD, kept alive like the retail delete list (RW 0x90BE00).
	size_t reloadedCount() const { return m_reloaded.size(); }

private:
	LocomotorTemplate *newOverride(LocomotorTemplate *base);

	std::map<std::string, std::shared_ptr<LocomotorTemplate>> m_templates;
	std::vector<std::shared_ptr<LocomotorTemplate>> m_reloaded;
};

extern thread_local LocomotorStore *TheLocomotorStore;

// ---- LocomotorSet ---------------------------------------------------------------------------------------
// What an object's `LocomotorSet` blocks produce: per SET_* condition the template list (one entry
// per parse; unresolved names stay null like retail) and the Speed.
class LocomotorSetTemplate
{
public:
	struct Slot
	{
		std::vector<const LocomotorTemplate *> locomotors; // RW ThingTemplate+0x3AC map
		float speed = 0.0f;                                // RW ThingTemplate+0x3A0 map
		bool hasSpeed = false;
	};

	const Slot *find(int condition) const;
	bool specified(int condition) const;
	size_t conditionCount() const { return m_slots.size(); }
	const std::map<int, Slot> &slots() const { return m_slots; }
	// Names that did not resolve in the store (retail stores null silently; this is the report).
	const std::vector<std::string> &unresolvedLocomotors() const { return m_unresolved; }

	// RW 0x73FB3F + 0x73ED7E: clear the condition's list, push the template, store the speed.
	void set(int condition, const LocomotorTemplate *locomotor, float speed);
	void noteUnresolved(const std::string &name) { m_unresolved.push_back(name); }

private:
	std::map<int, Slot> m_slots;
	std::vector<std::string> m_unresolved;
};

// The object being parsed, as the LocomotorSet parser sees it (OBJ-1's ThingTemplate implements it).
class LocomotorSetOwner
{
public:
	virtual ~LocomotorSetOwner() {}
	virtual const std::string &locomotorSetObjectName() const = 0;
	// RW 0x73CD38: the AIUpdate-type module exists.
	virtual bool hasAIUpdateModule() const = 0;
	// RW 0x5E983D on the AIUpdate module data's map: that condition already holds a template list.
	virtual bool aiUpdateHasLocomotorsFor(int condition) const = 0;
	virtual LocomotorSetTemplate &locomotorSet() = 0;
};

// The ThingTemplate field parse proc for `LocomotorSet` (RW 0x73BF6B): `instance` must point at a
// LocomotorSetOwner (as that base subobject). Reads the entry block up to End.
void parseLocomotorSet(INI *ini, void *instance, void *store, const void *userData);

// =====================================================================================================
// Locomotor kinematics (spec horde-and-movement.md 2.13, checklist step 4). Lane HORDE-1.
//
// The retail `Locomotor` is a per-object instance (RW constructor 0x5E3CFx: template pointer = the final
// override, speed caps 99999, the temporary speed cap -1, a working 3x4 matrix at +0x68 that the movers
// rotate and translate, flags at +0x44). It moves an object by REWRITING ITS TRANSFORM each logic frame; there
// is no force physics for ground units (spec 2.13 / 2.16). Every read or write of the object, its AI, its
// path, the terrain or the model condition flags goes through LocomotorHost (a port boundary: the runtime
// Object integration implements it; the tests use a small mock).
//
// Ported (target = RotWK game.dat; S-001 caveat):
//   getMaxSpeedForCondition 0x5E3F49, getMaxAcceleration 0x5E40AD, getBraking 0x5E40F2, getMaxTurnRate
//   0x5E372D, isCharging 0x5E3EF7, moveForward 0x5E586F, rotateTowardsPosition 0x5E4F44 (ground branch) with
//   its wrapper 0x5E60C7, Legs 0x5E63F3, Wheels 0x5E83FD (also TREADS 0x5E8827), HORDE 0x5E6D4F (plain path
//   only), OTHER / WINGS 0x5E6835, the dispatcher 0x5E8865 (appearance table RW 0x5E91BA) and handleBehaviorZ
//   0x5E76AC for NO_Z_MOTIVE_FORCE.
// NOT ported (reported through Locomotor::unverified(), acceptance stops S-081, S-084):
//   x87/SSE parity of the CRT trigonometry, HOVER / SHIP / GIANT_BIRD movers, the ScalesWalls look-ahead of the
//   dispatcher (RW 0x5E8956-0x5E8FF8), every ZAxisBehavior other than NO_Z_MOTIVE_FORCE, the cliff / wading
//   condition updates of RW 0x5E5785, and the HORDE mover's formation path nodes (types 2, 3, 7, 8).
// =====================================================================================================

// RW +0x68: three rows of four floats (3x3 rotation, translation in column 3).
struct LocomotorMatrix
{
	float m[3][4];
	static LocomotorMatrix identity();
};

// RW 0x766173 returns a 16-byte node: {layer, x, y, z}.
struct LocomotorPathPoint
{
	int layer = 0;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
};

// The path an object's AI is following (RW: obj+0x260 -> +0x140, 0x5E3884).
class LocomotorPath
{
public:
	virtual ~LocomotorPath() {}
	// RW 0x765F31: the point `distance` ahead of the closest segment (the retail code never asks for less than
	// 0.1; callers pass the locomotor's current speed, RW 0x766173 falls back to 40.0 without a locomotor).
	virtual LocomotorPathPoint computePointAhead(float distance) = 0;
	// RW 0x765598(&pos, 1): move the closest segment to the one nearest `pos`.
	virtual void updateClosestSegment(const Coord3D &pos) = 0;
	// RW 0x5E2DBA: the path has a node and the distance to its end (via RW 0x765819) is below 10.0.
	virtual bool isNearPathEnd() const = 0;
	// RW 0x5E2DF4: the path's current node carries a fixed Z (field +0x20 != 0x7FFFFFFF): handleBehaviorZ is skipped.
	virtual bool hasExplicitZ() const = 0;
	// RW 0x765972: the path length remaining from a point returned by computePointAhead (the HORDE mover's
	// reform threshold).
	virtual float remainingDistanceFrom(const LocomotorPathPoint &point) const = 0;
	// The type of the special node the HORDE mover is on (RW 0x5E6E20 reads node+0x60 of the node the path
	// reports through RW 0x5E2D74; 0 when there is none). Types 2, 3, 7 and 8 start the formation machinery and
	// are not ported (S-084); every other type, 4 included, takes the plain branch (RW 0x5E6E42 / 0x5E6FFF).
	virtual int currentSpecialNodeType() const = 0;
};

// Everything the movers touch on the object. Names follow ZH where the call exists there; RW addresses give
// the binary offset or function.
class LocomotorHost
{
public:
	virtual ~LocomotorHost() {}

	// ---- transform ----
	virtual Coord3D getPosition() const = 0;                 // obj+0x38
	virtual float getAngle() const = 0;                      // obj+0x44 (radians)
	virtual Coord2D getUnitDirectionVector2D() const = 0;    // RW 0x70B9E0: (cos angle, sin angle)
	virtual float getBoundingRadius() const = 0;             // obj+0xB8: scales TurnPivotOffset (RW 0x5E4FBB)
	virtual LocomotorMatrix getTransform() const = 0;        // obj+0x08..0x37, copied into the locomotor first
	virtual void setTransform(const LocomotorMatrix &m) = 0; // RW 0x70BA76 setTransformMatrix
	// obj+0x198 / +0x1A6: the "previous / pending position" the movers record (RW 0x5E3BD1 reads it).
	virtual bool hasPendingPosition() const = 0;
	virtual Coord3D getPendingPosition() const = 0;
	virtual void setPendingPosition(const Coord3D &p) = 0;   // also raises the valid flag

	// ---- AI / body / game state ----
	virtual float locomotorSetSpeed() const = 0;             // ai+0x1F8, raw distance per second
	virtual int damageState() const = 0;                     // body module vfunc 0x24
	virtual int movementPenaltyDamageState() const = 0;      // GameLogic+0xB3C (GameData MovementPenaltyDamageState)
	virtual float crewPowerMultiplier() const = 0;           // RW 0x68BF11 (1.0 without the module)
	virtual bool speedAttributeModifier(float &out) const = 0; // RW 0x68C82D(obj, 8, &out, 0, 1)
	virtual bool isInRiver(float x, float y) const = 0;      // TheTerrainLogic vfunc 0x4C(x, y, 0, 0, &flag), flag set
	virtual bool isTurnLimited() const = 0;                  // ai vfunc 0x228 (halves turns)
	virtual bool isChargeOrdered() const = 0;                // obj+0x251
	virtual bool physicsMotionDisabled() const = 0;          // RW 0x5E3A1B: obj+0x264 module byte +0x5C
	virtual bool hasPhysicsModule() const = 0;               // obj+0x264 != 0 (the object's PhysicsBehavior; lane EXIT-1, RW 0x5E7DE0)
	virtual bool zMotionSuppressed() const = 0;              // RW 0x5E774A: obj+0x1C8 bit 3 or THROWN_PROJECTILE
	virtual unsigned logicFrame() const = 0;                 // TheGameLogic+0x40
	virtual bool containerAllowsBackingUp() const = 0;       // RW 0x5E6530-0x5E655A (HORDE_MEMBER inside a container whose template flag +0xD8 is set)
	virtual float groundHeightAt(float x, float y) const = 0; // terrain height for NO_Z_MOTIVE_FORCE
	virtual LocomotorPath *getPath() = 0;                    // RW 0x5E3884
	// obj+0x94 ObjectStatus bits and obj+0x10C model condition bits (by index into the registries).
	virtual bool testObjectStatus(int bit) const = 0;
	virtual bool testModelCondition(int bit) const = 0;       // (RW 0x46E918; 0x694154 also asks a linked object)
	virtual void setModelCondition(int bit, bool value) = 0; // set or clear, notifying on change (RW 0x68B53C)
	// RW 0x68D607: clear the first list and set the second in one notification.
	virtual void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) = 0;
	// RW 0x5E821A via ai+0x20 with argument 2: the wheels mover stops an idle member.
	virtual void notifyWheelsStopped() = 0;
	// The HORDE mover's contain hooks (HordeContain vtable +0x3C / +0x40 reform begin / end, +0x1C0 "formation ready").
	virtual bool hasHordeContain() const = 0;
	virtual void hordeBeginReform() = 0;
	virtual void hordeEndReform() = 0;
	virtual bool hordeFormationReady(float angle) const = 0;
	// ThingTemplate+0x109 bit 2 (RW 0x5E73AA): the HORDE mover waits for the formation.
	virtual bool thingWaitsForFormation() const = 0;
	// RW 0x5E719F: a HORDE locomotor with ScalesWalls asks its contain (vtable +0x240 = RW 0x86BC78) to adjust the desired speed. HordeContain returns it unchanged
	// while its wall-scaling counter (H+0x1D0) is 0, which is every state this port can reach (wall scaling itself is not ported, S-084)
	virtual float hordeWallScalingSpeed(float desiredSpeed) const { return desiredSpeed; }
	// RW 0x694BF8 + vtable +0x238 (ZAxisBehavior SCALING_WALLS, RW 0x5E76D3): the object is on a wall; never true here (S-084)
	virtual bool isScalingWall() const { return false; }
};

// Locomotor instance flag bits (RW +0x44).
enum LocomotorFlags
{
	LOCOMOTOR_FLAG_BRAKING = 0x01,      // RW 0x5E5970 sets, 0x5E5911 clears
	LOCOMOTOR_FLAG_BIT2 = 0x04,         // cleared by the dispatcher on entry (RW 0x5E8875); set by maintainCurrentPosition with the held position (RW 0x5E7D2A, lane EXIT-1)
	LOCOMOTOR_FLAG_NO_BRAKE = 0x10,     // moveForward does not start braking while it is set (setter not located)
	LOCOMOTOR_FLAG_BACKING_UP = 0x80    // Legs / Wheels / HORDE movers
};

class Locomotor
{
public:
	// RW 0x5E3D0x: `tmpl` is the template found in the store (its final override is used).
	explicit Locomotor(const LocomotorTemplate *tmpl);

	const LocomotorTemplate &getTemplate() const { return *m_template; }

	// ---- getters (per-frame units) ----
	float getMaxSpeedForCondition(const LocomotorHost &host) const; // RW 0x5E3F49
	float getMaxAcceleration(const LocomotorHost &host) const;      // RW 0x5E40AD
	float getBraking(const LocomotorHost &host) const;              // RW 0x5E40F2
	float getMaxTurnRate(const LocomotorHost &host) const;          // RW 0x5E372D
	bool isCharging(const LocomotorHost &host) const;               // RW 0x5E3EF7
	// RW 0x5E4CA7: the object is faster than a quarter of its maximum speed.
	bool isFasterThanQuarterSpeed(const LocomotorHost &host) const;

	// ---- the entry point (RW 0x5E8865) ----
	void locomotorMoveTowardsPosition(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed);
	// RW 0x5E98D6 (lane SMOOTH-3): the angle goal (AIMover goal type 3) turns at this locomotor's rate instead of being set at once
	void locomotorMoveTowardsAngle(LocomotorHost &host, float angle);
	// RW 0x5E7CC7 (lane EXIT-1): the locomotor of a unit without a goal (AIMover goal type 0, RW 0x669A60) holds its position: the walk's model conditions
	// go (MOVING for the legged / wheeled / horde / treads appearances); returns whether the locomotor wants to be called every frame (RW's result)
	bool locomotorMaintainCurrentPosition(LocomotorHost &host);

	// ---- the movers, public for tests ----
	void moveTowardsPositionLegs(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed);   // RW 0x5E63F3
	void moveTowardsPositionWheels(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed); // RW 0x5E83FD
	void moveTowardsPositionHorde(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed);  // RW 0x5E6D4F
	void moveTowardsPositionOther(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed);  // RW 0x5E6835
	void moveForward(LocomotorHost &host, const Coord3D &goal, float onPathDistToGoal, float desiredSpeed);              // RW 0x5E586F
	// RW 0x5E4F44 (ground branch): turns the working matrix by at most `maxTurnRate` radians towards `goal`.
	void rotateTowardsPosition(LocomotorHost &host, const Coord3D &goal, float maxTurnRate, float *outRelAngle);
	// RW 0x5E60C7: copy the object's transform into the working matrix, then rotateTowardsPosition with this
	// locomotor's turn rate.
	void rotateTowardsWrapper(LocomotorHost &host, const Coord3D &goal, float *outRelAngle);
	void handleBehaviorZ(LocomotorHost &host, const Coord3D &goal);                                                       // RW 0x5E76AC
	// RW 0x5E3A81: the working matrix becomes a rotation of `angle` about Z, translation kept.
	void setMatrixAngle(float angle);

	// ---- state (RW offsets) ----
	float speed() const { return m_speed; }                 // +0x40 current speed per frame
	void setSpeed(float s) { m_speed = s; }
	// RW 0x5E4C40 (lane EXIT-1): the speed moves toward `v`, up by at most the acceleration (x87 fadd, stored), down at once, then within [0, the maximum speed]
	void setSpeedTowards(const LocomotorHost &host, float v);
	unsigned flags() const { return m_flags; }              // +0x44
	void setFlags(unsigned f) { m_flags = f; }
	const LocomotorMatrix &matrix() const { return m_matrix; } // +0x68
	LocomotorMatrix &matrix() { return m_matrix; }
	int turnDirection() const { return m_turnDirection; }   // +0xA8: -1 right, 0 none, 1 left
	void setMaxSpeedCap(float v) { m_maxSpeedCap = v; }     // +0x28
	void setTemporarySpeedCap(float v) { m_temporarySpeedCap = v; } // +0x2C (-1 = none)
	void setAccelerationCap(float v) { m_accelerationCap = v; }     // +0x30
	void setBrakingCap(float v) { m_brakingCap = v; }               // +0x34
	void setTurnRateCap(float v) { m_turnRateCap = v; }             // +0x38
	void setDesiredSpeedCap(float cap, unsigned untilFrame) { m_desiredSpeedCap = cap; m_desiredSpeedCapUntilFrame = untilFrame; } // +0x5C / +0x60
	// RW 0x5E4137 (lane HORDE-2): the speed cap set by setDesiredSpeedCap while its frame has not passed (`cmp +0x60, frame; jae`), else getMaxSpeedForCondition
	float getCurrentMaxSpeed(const LocomotorHost &host, unsigned frame) const { return m_desiredSpeedCapUntilFrame >= frame ? m_desiredSpeedCap : getMaxSpeedForCondition(host); }
	void setPreferredPoint(const Coord3D &p) { m_preferredPoint = p; } // +0x14: the Wheels mover's back-up reference point

	// the OpenBFME state hash of the instance (MOVE-1): every field the movers carry from one frame to the next
	void crc(class StateHasher &hasher) const;

	// Acceptance-stop lines raised by the moves made so far (S-081, S-084); empty when none applied.
	const std::vector<std::string> &unverified() const { return m_unverified; }
	// The stops this component can raise at all (tests pin the exact text).
	static std::vector<std::string> allStops();

private:
	void noteStop(const char *line);

	const LocomotorTemplate *m_template;
	float m_lookAheadMult = 1.0f;       // +0x20
	float m_maxSpeedCap = 99999.0f;     // +0x28
	float m_temporarySpeedCap = -1.0f;  // +0x2C
	float m_accelerationCap = 99999.0f; // +0x30
	float m_brakingCap = 99999.0f;      // +0x34
	float m_turnRateCap = 99999.0f;     // +0x38
	float m_closeEnoughDist;            // +0x3C
	float m_speed = 0.0f;               // +0x40
	unsigned m_flags = 0;               // +0x44
	float m_desiredSpeedCap = 0.0f;     // +0x5C
	unsigned m_desiredSpeedCapUntilFrame = 0; // +0x60
	LocomotorMatrix m_matrix;           // +0x68
	Coord3D m_maintainPosition{ 0.0f, 0.0f, 0.0f }; // +0x08: the position held while there is no goal (flag 4; RW 0x5E7D10, lane EXIT-1)
	bool m_byte98 = false;              // +0x98 (set by moveForward's animation part, cleared by the movers)
	bool m_nearPathEnd = false;         // +0x99: the path is within 10 of its end (dispatcher, RW 0x5E8925)
	bool m_byte9a = false;              // +0x9A (HORDE mover formation state)
	int m_turnDirection = 0;            // +0xA8
	Coord3D m_preferredPoint{ 0.0f, 0.0f, 0.0f }; // +0x14
	std::vector<std::string> m_unverified;
};
