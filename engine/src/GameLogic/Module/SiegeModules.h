// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The siege ladders' and the siege towers' docking against a wall (lane SIEGE-1): SiegeDockingBehavior (a wall piece's dock points), SiegeDeploySpecialPower
// (the ladder / tower drives to a dock point, lowers onto the wall, stays deployed, raises again), StopSpecialPower (the retract button), SiegeAIUpdate (the
// AI that takes no order but evacuate while deployed) and the AI's face-position state 0x25 the deploy turns with. BFME classes; ZH has none.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). Every function below has the size of its BFME2 1.06 counterpart (Open-BFME-2, Code/GameEngine/Source/GameLogic/
// Object/{Behavior,SpecialPower,Update}/Siege*.cpp: byte-matched C++), and its RotWK body was read in the Ghidra export; the offsets and constants are RotWK's.
//   SiegeDockingBehavior (create RW 0x64AC11 size 0x34, constructor RW 0x85C9C4: wakes next frame (RW 0x850C32(obj, 1)); data RW 0x861DC1, no field; dock
//   interface + 0x20 (vtable RW 0xC57C34), update + 0x10 (RW 0xC57C44); the entry vector + 0x24, "initialised" + 0x30):
//     * update RW 0x85D216: the first call reads the dock points (RW 0x85CE65), later calls refresh the reservations (RW 0x85CBC8); sleeps
//       LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5) frames;
//     * the dock points RW 0x85CE65 (BFME2 0x459E05): Object::getMultiLogicalBonePosition (RW 0x68C650) "SIEGETOWER" then "SIEGELADDER", 10 at most each, in
//       world space: an entry per bone {index, type 0 tower / 1 ladder, the bone's position, the negated X axis of its transform, reserved by 0};
//     * refresh RW 0x85CBC8: an entry reserved by an object that is gone, dead (+ 0x458 bit 0), has no SiegeDeploySpecialPower or whose power's target (+ 0x24
//       interface slot 0x50 = RW 0x7B0F25: module + 0x40) is no longer this wall is freed;
//     * slot 1 RW 0x85CA8A (approach query): the closest free compatible entry's point 30 back along its direction; false when none;
//     * slot 2 RW 0x85CD20 (reserve): the closest (Coord3D::length < 99999, RW 0xBDF350) free compatible entry is reserved for the object; its index, or -1;
//     * slot 3 RW 0x85CCB8 (canDock): some free compatible entry; "compatible": a SIEGE_LADDER object (template + 0x119 bit 3, KindOf 139) takes any entry,
//       anything else (a tower) only type 0;
//     * the point accessors RW 0x85C94A / 0x85C987: entry + 0x08 / + 0x14 (position / direction), the wall's position out of range.
//   SiegeDeploySpecialPower (create LAB_00651F08 size 0x74, constructor RW 0x8C9B49 over the update + special power base RW 0x991660 that
//   WeaponModeSpecialPowerUpdate also has; vtables RW 0xC74910, update + 0x10 RW 0xC74904, special power update + 0x20 RW 0xC748E0, special power + 0x24 RW
//   0xC74878; data RW 0x8C9CA4 (0x30 bytes) parsed by RW 0x8C9B2E: the base table RW 0x9911B3 (SpecialPowerTemplate + 0x08, InitiateSound + 0x0C, StartsPaused
//   + 0x14) and RW 0xC747E8: LowerDelay + 0x18 and RaiseDelay + 0x1C (RW 0x73A429, frames), EvacuatePassengersOnDeploy + 0x20 (default 1), EvacuateCrewOnDeploy
//   + 0x21, SkipAdjustPosition + 0x22, WallSearchDistance + 0x24 (500, RW 0xBDD97C), AwayFromWallWaitDist + 0x28 (200, RW 0xBE4170), ExtraWallDistance + 0x2C;
//   module fields: + 0x38 the phase, + 0x3C the frame counter, + 0x40 the dock (wall) id, + 0x54 the approach point, + 0x60 a toggle, + 0x64 the dock direction,
//   + 0x70 the stop request):
//     * isReady (+ 0x24 slot 1, RW 0x8C9BE0) = not active; isActive (+ 0x20 slot 2, RW 0x8C9BEE) = the phase is neither 0 nor 1; isDeployed (RW 0x8C9C01) = 3;
//       getPercentReady is the base's (RW 0x9913BC: it asks isReady through the vtable); stopSpecialPower (+ 0x24 slot 17, RW 0x8CA2D0): phase 0 / 1 -> 0,
//       2 / 3 -> the stop request;
//     * initiate (+ 0x20 slot 0, RW 0x8CA5C2): nothing while active, without a target or an AI; the target's SiegeDockingBehavior (none: nothing) when it can
//       dock this object (slot 3), else the first object near the target (WallSearchDistance, KindOf WALK_ON_TOP_OF_WALL 60, RW 0xA39340 sorted near to far)
//       whose dock can; with one: the approach point (RW 0x8C9D06) and, docked, the AI ignores it as an obstacle (RW 0x66831A), idles (RW 0x5E821A), moves
//       to the approach point (RW 0x66C4CA, CMD_FROM_AI), the toggle on, phase 1, the dock remembered; not docked: the dock forgotten, the deploy stopped (RW
//       0x8CA2F1: DEPLOYING off, phase 0, the dock 0); without a dock the AI moves to the target pushed AwayFromWallWaitDist along its X axis (when > 0).
//       The module wakes next frame (RW 0x850C32);
//     * the approach point RW 0x8C9D06: the dock's position; DEPLOYING (status 0x40) off, the dock's reservation (slot 2) and, reserved, DEPLOYING on, the
//       entry's position and direction; then (docked, more than 1.0 from the wall, RW 0xBD1908) the offset to the wall x 0.1 (RW 0xBD83D4), adjusted unless
//       SkipAdjustPosition (Pathfinder::adjustMeleeOffset RW 0x6EA0EC), normalised, stepped out by 5 (RW 0xBDAE58) at most 10 times while the point is on a
//       wall (RW 0x6EA857), then ExtraWallDistance further along the direction;
//     * update (+ 0x10 slot 0, RW 0x8CA30F): a dead object: the portal's waypoints go (RW 0x8C9BCB), sleep forever; phase 0: sleep forever; the stop request:
//       phase 4, next frame; no AI: stop, forever; no drawable (RW 0x70E013): forever; a player order since (not idle and the last command source not
//       CMD_FROM_AI, or idle and it is CMD_FROM_PLAYER: AI vslots 0x1B8 / 0x23C): stop, forever; the dock gone or dead: phase 4 (phase 3: also); phase 1: the
//       object faces object + direction (|relAngle| < 0.15, RW 0xBE53C8, a double compare) and either touches the wall (RW 0x68BA2A -> RW 0xAD2CE0) or is idle,
//       and the DynamicPortalBehaviour (RW 0x863983) answers IsEndPortalAboveWall (RW 0x863EB3): the locomotor goal none (AI vslot 0x220), AI command 0x31
//       (busy, RW 0x852E2A), phase 2; otherwise, idle: the dock ignored as an obstacle and the toggle flipped: on, the approach point again and a move there when
//       docked; off, AI command 0x27 (face the point, RW 0x7C821E); phase 2: LowerDelay frames, then phase 3; phase 4: phase 0. Sleep none;
//     * setPhase RW 0x8CA072: leaving 2 clears UNPACKING (96), leaving 3 clears DEPLOYED (status 59) and DEPLOYED (100) and removes the portal's waypoints (RW
//       0x8C9BCB -> RW 0x8639EF), leaving 4 forgets the dock and clears PACKING (94); the frame counter 0; entering 2 sets UNPACKING, the locomotor's speed to 0
//       (RW 0x5E4C40) and UNPACKING on every rider (contain slot 0x118); entering 3 sets status DEPLOYED and DEPLOYED (100), clears MOVING (61), makes the
//       portal's waypoints (RW 0x86404D), EvacuatePassengersOnDeploy: AI command 0x1B (evacuate, RW 0x6AF1AC, CMD_FROM_AI), EvacuateCrewOnDeploy: each crew
//       member (contain slot 0x11C) takes the owner's attitude (RW 0x66E12A) and exits (RW 0x7716C1, CMD_FROM_AI); entering 4 sets PACKING and disables the object
//       (DISABLED_TEMPORARILY_BUSY, RW 0x6907F1) for RaiseDelay frames (its update is not called meanwhile: the update interface's disabled mask RW 0x6530FA is
//       empty).
//   StopSpecialPower (create RW 0x65201A size 0x38, constructor RW 0x8CABF6 over SpecialPowerModule's RW 0x8973EE; data RW 0x8CAD35: SpecialPowerModuleData
//   and StopPowerTemplate + 0x7C, RW 0xC74A78): isReady (RW 0x8CACC6) finds the module of the object whose special power is StopPowerTemplate (kept at + 0x34)
//   and answers its special power update's isActive (true without one; false without a StopPowerTemplate or a module); doSpecialPower: when ready, the found
//   module's stopSpecialPower (slot 17).
//   SiegeAIUpdate (create RW 0x64D169 size 0x4A8, constructor RW 0x893BBA over AIUpdateInterface's RW 0x66ECFE): only isAllowedToRespondToAiCommands (AI
//   vtable RW 0xC63B60 slot 148 = RW 0x893C80): after the base gate (RW 0x667174), with a special power module of type SPECIAL_SIEGEDEPLOY (0x2D, RW
//   0x691526) that is not ready (slot 1) only AI command 0x1B (evacuate) passes.
//   The face state 0x25 (RW 0x753755 defines it with RW 0x7449D4(mode 0), vtable RW 0xC29600; the same class as state 59 AIFaceObjectIdleState in mode 2):
//   onEnter RW 0x74D68C (can turn = a locomotor whose MinSpeed is 0), update RW 0x75638B on the machine's goal position (|relAngle| below 0.035: success; a
//   unit that cannot turn: the explicit goal, vslot 0x210; else the goal angle, vslot 0x21C), onExit RW 0x743DA9; the command (0x27 -> AI vslot 0x118 RW
//   0x667290): mobile (RW 0x690E97): the machine cleared, the clipped goal position (RW 0x6654AD), the last source, state 0x25.
// INFERENCE / NOT PORTED (stops S-4940 .. S-4944, docs/STOPS.md):
//   * S-4940: DynamicPortalBehaviour is lane WALL-2's: the deploy asks it through SiegePortalInterface below (IsEndPortalAboveWall, the waypoints made and
//     removed). Until a DynamicPortalBehaviour runtime implements the interface the phase-1 test cannot pass: the ladder / tower reaches the wall, faces it and
//     waits there (the stop is noted each time the test is asked);
//   * S-4941: Pathfinder::adjustMeleeOffset (RW 0x6EA0EC) is not ported: SkipAdjustPosition No (no retail object) notes the stop and keeps the offset;
//   * S-4942: the AI's + 0x16C / + 0x3B8 the face command clears are not identified; the InitiateSound and the client's selection voices are the client's;
//   * the bones are the drawable's pristine pose for the wall's model condition (DrawableLaunchBones, as GARRISON-1's); without a client (no bone provider) a
//     wall has no dock point.

#pragma once

#include "Common/INI.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>
#include <string>
#include <vector>

class ModuleFactory;
class Object;
class StateHasher;
class SpecialPowerTemplate;

// ---- the WALL-2 integration point -------------------------------------------------------------------------------------------------------------------
// What SiegeDeploySpecialPower asks the siege object's DynamicPortalBehaviour: SiegePortalInterface (its own header; WALL-2's DynamicPortalBehaviour,
// PortalModules.h, implements it)
#include "GameLogic/Module/SiegePortalInterface.h"

// lane SIEGE-1: RW 0x6EDB6F .. 0x6EDC74 (the melee reach's SIEGE_TOWER branch): a unit at `sourcePos` with the bounding radius `sourceRadius` reaches the
// deployed ladder / tower `tower` when the tower's portal answers its top attack point within 20 in height (RW 0xBDBC6C, a double compare of the fabs) and
// within sourceRadius + TopAttackRadius in x and in y. False when the tower is not deployed or has no portal runtime (S-4940)
bool siegeTowerTopReach(Object &tower, const Coord3D &sourcePos, float sourceRadius);

// RW 0x863983 for the siege modules: the object's DynamicPortalBehaviour as the interface above. A DynamicPortalBehaviour module that does not implement it (not
// ported: S-4940) answers null and notes the stop
SiegePortalInterface *findSiegePortal(Object &obj);

// ---- SiegeDockingBehavior ---------------------------------------------------------------------------------------------------------------------------------
class SiegeDockingBehaviorModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p); // no field (RW 0x85C939: the base table only)
};

class SiegeDockingBehavior : public UpdateModule
{
public:
	struct Entry
	{
		int index = 0;            // + 0x00
		int type = 0;             // + 0x04: 0 a SIEGETOWER bone, 1 a SIEGELADDER bone
		Coord3D position{};       // + 0x08
		Coord3D direction{};      // + 0x14 (the bone's X axis negated: away from the wall)
		ObjectID reservedBy = INVALID_ID; // + 0x20
	};
	SiegeDockingBehavior(Thing *thing, const SiegeDockingBehaviorModuleData *data); // RW 0x85C9C4
	OBFME_MODULE_XFER(SiegeDockingBehavior) // the save (SAVE-4b): UpdateModule's, the dock entries and the made flag
	UpdateSleepTime update() override;                                              // RW 0x85D216
	void crc(StateHasher &h) const override;

	// RW 0x85CA8A (dock interface slot 1): the closest free compatible entry's approach point (30 back along its direction); false when there is none
	bool approachPointFor(ObjectID id, Coord3D &out);
	// RW 0x85CD20 (slot 2): the closest free compatible entry reserved for `id`; its index, -1 when none
	int reserveFor(ObjectID id);
	// RW 0x85CCB8 (slot 3): a free compatible entry exists
	bool canDock(ObjectID id);
	// RW 0x85C94A / 0x85C987: entry `index`'s position / direction; the wall's position when out of range
	Coord3D dockPosition(int index) const;
	Coord3D dockDirection(int index) const;

	const std::vector<Entry> &entries() const { return m_entries; }
	bool initialized() const { return m_initialized; }
	// the module of `obj` (RW 0x68BDA5 with the key "SiegeDockingBehavior"), null when it has none
	static SiegeDockingBehavior *of(const Object &obj);

private:
	void initializeBones(); // RW 0x85CE65
	void refresh();         // RW 0x85CBC8
	bool compatible(const Object &obj, const Entry &e) const;
	std::vector<Entry> m_entries; // + 0x24
	bool m_initialized = false;   // + 0x30
};

// ---- the update + special power base RW 0x991660 (SiegeDeploySpecialPower's and SiegeDeployHordeSpecialPower's) ------------------------------------------
// The data's base table RW 0x9911B3 (-> RW 0xC87588): SpecialPowerTemplate + 0x08 (RW 0x73B22F), InitiateSound + 0x0C, StartsPaused + 0x14; the module's timer
// + 0x28 the ready frame, + 0x2C the pause count, + 0x30 the pause frame, + 0x34 the paused percent (as WeaponModeSpecialPowerUpdate's, GameLogic/Module/
// HeroAbilityModules.h, which has the same base)
class SpecialPowerUpdateBaseModuleData : public ModuleData
{
public:
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr; // + 0x08
	std::string m_specialPowerTemplateName;
	std::string m_initiateSound;      // + 0x0C (client)
	std::string m_initiateSound2;     // BFME1 only; retained for the client, like SpecialPowerModuleData
	bool m_startsPaused = false;      // + 0x14
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x9911B3
};

class SpecialPowerUpdateBase : public UpdateModule, public SpecialPowerModuleInterface, public SpecialPowerUpdateInterface
{
public:
	SpecialPowerUpdateBase(Thing *thing, const SpecialPowerUpdateBaseModuleData *data); // RW 0x991660
	SpecialPowerModuleInterface *getSpecialPower() override { return this; }
	void crc(StateHasher &h) const override;
	// ---- the special power update interface (+ 0x20) ----
	bool drivesSpecialPower(const SpecialPowerTemplate *t) const override { return t && t == m_baseData->m_specialPowerTemplate; } // RW 0x89827C
	bool isActive() const override { return false; }               // slot 2: RW 0x9188EB (the subclasses override it)
	bool allowsSpecialPowerUse() const override { return true; }   // slot 8: RW 0x9B501B
	// ---- the special power interface (+ 0x24) ----
	const SpecialPowerTemplate *getSpecialPowerTemplate() const override { return m_baseData->m_specialPowerTemplate; }
	bool isReady() const override { return readyImpl(true); }                 // RW 0x99135D
	float getPercentReady() const override { return percentImpl(true); }      // RW 0x9913BC
	unsigned getReadyFrame() const override;                                  // RW 0x991473
	void onScienceAcquired() override {}                                      // RW 0x63F3BF
	void setReadyFrame(unsigned frame) override { m_readyFrame = frame; }     // RW 0x993440
	void startPowerRecharge(float percentOfCurrent) override;                 // RW 0x991500
	bool requirementsMet() const override { return true; }
	bool isReadyForDisplay() const override { return readyImpl(false); }
	float getPercentReadyForDisplay() const override { return percentImpl(false); }
	void doSpecialPower(unsigned options) override;                               // RW 0x9911FB
	void doSpecialPowerAtObject(Object *target, unsigned options) override;       // RW 0x991231
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x991269
	void pauseCountdown(bool pause) override;                                     // RW 0x99116C

protected:
	// the save of this base (SAVE-4b): UpdateModule's, the ready frame and the pause
	void xferSpecialPowerUpdateBase(Xfer &x);
	// RW 0x99135D: the timer's own readiness (the shared player frame for a SharedSynced template, else not paused and the ready frame reached)
	bool readyImpl(bool insert) const;
	// RW 0x9913BC: isReady through the vtable (a subclass's override), the paused percent, then the timer
	float percentImpl(bool insert) const;
	unsigned now() const;

private:
	const SpecialPowerUpdateBaseModuleData *m_baseData;
	unsigned m_readyFrame = 0;     // + 0x28
	int m_pauseCount = 0;          // + 0x2C
	unsigned m_pauseFrame = 0;     // + 0x30
	float m_pausedPercent = 0.0f;  // + 0x34
};

// ---- SiegeDeploySpecialPower ------------------------------------------------------------------------------------------------------------------------------
class SiegeDeploySpecialPowerModuleData : public SpecialPowerUpdateBaseModuleData
{
public:
	unsigned m_lowerDelay = 0;        // + 0x18 frames
	unsigned m_raiseDelay = 0;        // + 0x1C frames
	bool m_evacuatePassengersOnDeploy = true; // + 0x20 (RW 0x8C9CC9: 1)
	bool m_evacuateCrewOnDeploy = false;      // + 0x21
	bool m_skipAdjustPosition = false;        // + 0x22
	float m_wallSearchDistance = 500.0f;      // + 0x24 (RW 0xBDD97C)
	float m_awayFromWallWaitDist = 200.0f;    // + 0x28 (RW 0xBE4170)
	float m_extraWallDistance = 0.0f;         // + 0x2C
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8C9B2E
};

// lane SIEGE-1: StopSpecialPower's slot 17 (RW 0x8CA2D0 for the deploy)
class SpecialPowerStopInterface
{
public:
	virtual ~SpecialPowerStopInterface() = default;
	virtual void stopSpecialPower() = 0;
};

class SiegeDeploySpecialPower : public SpecialPowerUpdateBase, public SpecialPowerStopInterface
{
public:
	enum Phase
	{
		PHASE_IDLE = 0,      // nothing to do
		PHASE_APPROACH = 1,  // driving to the dock point and facing the wall
		PHASE_LOWERING = 2,  // UNPACKING for LowerDelay frames
		PHASE_DEPLOYED = 3,  // DEPLOYED: on the wall
		PHASE_RAISING = 4    // PACKING, disabled for RaiseDelay frames
	};
	SiegeDeploySpecialPower(Thing *thing, const SiegeDeploySpecialPowerModuleData *data); // RW 0x8C9B49
	OBFME_MODULE_XFER(SiegeDeploySpecialPower) // the save (SAVE-4b): the base's, the phase and its frame, the dock, the approach
	UpdateSleepTime update() override; // RW 0x8CA30F
	void crc(StateHasher &h) const override;

	// ---- the special power update interface (+ 0x20) ----
	void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra) override; // RW 0x8CA5C2
	bool isActive() const override { return m_phase != PHASE_IDLE && m_phase != PHASE_APPROACH; }                             // RW 0x8C9BEE
	// ---- the special power interface (+ 0x24): the base's, isReady RotWK's override ----
	bool isReady() const override { return !isActive(); }   // RW 0x8C9BE0
	bool isReadyForDisplay() const override { return !isActive(); }
	void stopSpecialPower() override;                       // RW 0x8CA2D0

	bool isDeployed() const { return m_phase == PHASE_DEPLOYED; } // RW 0x8C9C01
	int phase() const { return m_phase; }
	ObjectID dockId() const { return m_dockId; }           // + 0x40 (the + 0x24 interface's slot 0x50, RW 0x7B0F25)
	const Coord3D &approachPoint() const { return m_approach; }
	const Coord3D &dockDirection() const { return m_direction; }
	const SiegeDeploySpecialPowerModuleData &data() const { return *m_data; }
	// the module of `obj` (RW 0x68BDA5 with the key "SiegeDeploySpecialPower"), null when it has none
	static SiegeDeploySpecialPower *of(const Object &obj);
	// lane SIEGE-1 tests: a portal the deploy asks instead of the object's DynamicPortalBehaviour (WALL-2's integration point, S-4940); null: the object's
	static void setPortalOverrideForTests(SiegePortalInterface *(*find)(Object &obj));

private:
	void setPhase(int next);  // RW 0x8CA072
	void stopDeploy();        // RW 0x8CA2F1
	void clearPortal();       // RW 0x8C9BCB
	// RW 0x8C9D06: the approach point toward the dock object; `direction` and `docked` receive the dock entry's direction and whether a dock point was reserved
	Coord3D computeApproachPoint(Object &dock, Coord3D &direction, bool &docked);
	bool portalAboveWall(Object &obj);

	const SiegeDeploySpecialPowerModuleData *m_data;
	int m_phase = PHASE_IDLE;      // + 0x38
	unsigned m_frame = 0;          // + 0x3C
	ObjectID m_dockId = INVALID_ID; // + 0x40
	Coord3D m_approach{};          // + 0x54
	bool m_toggle = false;         // + 0x60
	Coord3D m_direction{};         // + 0x64
	bool m_stopRequested = false;  // + 0x70
};

// ---- SiegeDeployHordeSpecialPower ---------------------------------------------------------------------------------------------------------------------------
// TARGET FACTS (BFME2 counterparts in Open-BFME-2 SiegeDeployHord*.cpp, sizes and RotWK bodies matching): create RW 0x651F91 (size 0x4C), constructor RW 0x8CA815
// over the base RW 0x991660 (+ 0x38 the target id, + 0x3C the deploy position, + 0x48 a counter); data RW 0x8CA962 (0x1C bytes) parsed by RW 0x8CA7FA: the base
// table and RW 0xC74958, HordeDeploy + 0x18 (parseBool, default 0); the special power interface (+ 0x24) is the base's (RW 0xC650F0):
//   * initiate (+ 0x20 slot 0, RW 0x8CAA12): without HordeDeploy the module wakes (RW 0x850C32(obj, 1)); with it the horde's first contained SIEGE_TOWER member
//     (contain slot 66) leaves the horde (slot 14), is deselected (the client), idles and gets the power at the target (Object::doSpecialPowerAtObject); every
//     other member (slot 126) idles, gets the power, its own SiegeDeployHordeSpecialPower (if any) takes the leader's id and the target's position, and its AI
//     command 0x31 (busy);
//   * update (+ 0x10 slot 0, RW 0x8CA978): with an AI whose last command is not the player's: a target (+ 0x38) alive: DEPLOYED (status 0x3B) -> after
//     LOGICFRAMES_PER_SECOND frames idle and move to the deploy position, forever; not deployed -> move to the object (RW 0x7549D7), next frame; no target: idle,
//     forever; else forever.
// In 2.01's data no object sets HordeDeploy (the hordes' modules only wake and idle).
class SiegeDeployHordeSpecialPowerModuleData : public SpecialPowerUpdateBaseModuleData
{
public:
	bool m_hordeDeploy = false; // + 0x18
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8CA7FA
};

class SiegeDeployHordeSpecialPower : public SpecialPowerUpdateBase
{
public:
	OBFME_MODULE_XFER(SiegeDeployHordeSpecialPower) // the save (SAVE-4b): the base's, the target, the position, the counter
	SiegeDeployHordeSpecialPower(Thing *thing, const SiegeDeployHordeSpecialPowerModuleData *data); // RW 0x8CA815
	UpdateSleepTime update() override; // RW 0x8CA978
	void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra) override; // RW 0x8CAA12
	void crc(StateHasher &h) const override;
	void setTargetObjectID(ObjectID id) { m_targetId = id; }          // slot 13
	void setTargetPosition(const Coord3D &p) { m_deployPosition = p; } // slot 14 (RW 0x8CA86D)
	ObjectID targetId() const { return m_targetId; }
	static SiegeDeployHordeSpecialPower *of(const Object &obj);

private:
	const SiegeDeployHordeSpecialPowerModuleData *m_data;
	ObjectID m_targetId = INVALID_ID; // + 0x38
	Coord3D m_deployPosition{};       // + 0x3C
	unsigned m_counter = 0;           // + 0x48
};

// ---- StopSpecialPower ---------------------------------------------------------------------------------------------------------------------------------------
class StopSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	std::string m_stopPowerTemplateName;
	const SpecialPowerTemplate *m_stopPowerTemplate = nullptr; // + 0x7C
	static void buildFieldParse(MultiIniFieldParse &p);        // RW 0x8CABDB
};

class StopSpecialPower : public SpecialPowerModule
{
public:
	OBFME_MODULE_XFER(StopSpecialPower) // the save (SAVE-4b): SpecialPowerModule's and the stop count (the found module is looked up again)
	StopSpecialPower(Thing *thing, const StopSpecialPowerModuleData *data); // RW 0x8CABF6
	bool isReady() const override;                           // RW 0x8CACC6
	float getPercentReady() const override { return isReady() ? 1.0f : 0.0f; }
	// the button reads these (the control bar, CommandXlat RW 0x75CC3E): the readiness above, not the recharge timer the base shows (review #64)
	bool isReadyForDisplay() const override { return isReady(); }
	float getPercentReadyForDisplay() const override { return getPercentReady(); }
	void doSpecialPower(unsigned options) override;          // slot 0x28
	void doSpecialPowerAtObject(Object *target, unsigned options) override { (void)target; doSpecialPower(options); }
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override { (void)loc; doSpecialPower(options); }
	unsigned stops() const { return m_stops; }

private:
	BehaviorModule *stopTarget() const;
	const StopSpecialPowerModuleData *m_stopData;
	mutable BehaviorModule *m_found = nullptr; // + 0x34
	unsigned m_stops = 0;
};

// ---- the AI's face-position state 0x25 ----------------------------------------------------------------------------------------------------------------------
enum
{
	AI_FACE_POSITION = 0x25
};

class AIFacePositionState : public AIState
{
public:
	explicit AIFacePositionState(AIStateMachine &m);
	StateReturnType onEnter() override; // RW 0x74D68C
	StateReturnType update() override;  // RW 0x75638B (mode 0)
	void onExit(StateExitType status) override; // RW 0x743DA9
	void crc(class StateHasher &hasher) const override;
	// lane SAVE-4c (obfme:AIWorld): version (1, 1) and + 0x2C (INFERENCE: RotWK's state xfer for 0x25 was not read; its one field)
	void xfer(class Xfer &x) override;

private:
	bool m_canTurn = false; // + 0x2C
};

namespace SiegeModules
{
void registerAll(ModuleFactory &modules);
// [S-4940] .. [S-4942] and [S-4945] (DeployStyleAIUpdate): the stop lines the siege modules note when they are made (they reach GameLogic::report().stops)
std::vector<std::string> stopLines();
const char *portalStopLine();
} // namespace SiegeModules
