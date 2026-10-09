// OpenBFME. GPL-3.0.
//
// CastleBehavior and CastleMemberBehavior (lane BUILD-1): the starting fortress, the camps and the castles of the maps. A CastleBehavior object (the "castle centre":
// MenFortress is one, with `Model = None`) unpacks a base layout from Bases.big around itself: the keep (the Citadel), the build plots, the walls and the gates; every
// piece is a CastleMemberBehavior object owned by the castle's player.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly):
//   * registry: CastleBehavior create RW 0x64A74F, data RW 0x64A78A, mask 9 (UPDATE | CREATE), module size 0xAC, ctor RW 0x79A901 which derives from FoundationAIUpdate's
//     constructor (RW 0x858217); CastleMemberBehavior create RW 0x64A7DB, data RW 0x64A813, mask 4 (DAMAGE), CastleUpgrade (RW 0x65036C, 0x655608, mask 0x84).
//   * CastleBehaviorModuleData field table (RW 0xC312F8, offsets): CastleToUnpackForFaction (list of "<faction> <base>" pairs, parser RW 0x79BD2F, +0x68), FilterValidOwnedEntries
//     (ObjectFilter +0x30), FilterCrew (+0x34), FactionDecal (+0x5C), PreBuiltList (list of "<template> <pad index>", RW 0x79CA18, +0x50), PreBuiltPlyr (string +0x10), DecalName
//     (+0x14), DecalSize (real +0x18), FadeTime (real +0x1C), UnpackDelayTime (real +0x20), BuildTime (real +0x24), ScanDistance (real +0x28), MaxCastleRadius (real +0x2C),
//     CrewPrepareTime (duration +0x38), InstantUnpack (bool +0x3C), KeepDeathKillsEverything (+0x3D), CrewReleaseFX / CrewPrepareFX (FXList +0x48 / +0x44), CrewPrepareInterval
//     (duration +0x40), DisableStructureRotation (+0x74), EvaEnemyCastleSightedEvent (+0x4C), Summoned (+0x3E), TransferFoundationHealthToCastleUponUnpack (+0x75).
//     CastleMemberBehaviorModuleData (RW 0xC30A68): CampDestroyedOwnerEvaEvent / AllyEvaEvent / AttackerEvaEvent (+8 / +0xC / +0x10), BeingBuiltSound (+0x14), StoreUpgradePrice
//     (bool +0x18), CountsForEvaCastleBreached (bool +0x19).
//   * onBuildComplete of the create interface (RW 0x798238): when InstantUnpack is set, m_needInstantBuild = 1 (module + 0x3C); the object gets the model condition
//     INVULNERABLE and the status UNATTACKABLE (60). The update (RW 0x79CF2A, state at module + 0x34): state 0 calls RW 0x79C265 (below) and goes to state 4 when it
//     returns true; the states 1 (RW 0x79BE6A, an unpack), 2, 3, 5, 6 are the timed unpack / pack of a castle a player captured.
//   * RW 0x79C265 (instant unpack): when m_needInstantBuild: the owner is PreBuiltPlyr's player when the string is set, else the object's controlling player; with a team
//     the object takes it (setTeam RW 0x69954A), leaves and re-enters the world (RW 0x68C18F / 0x68E31F); then unpack(true) (RW 0x79BE6A) and one prebuilt object per
//     PreBuiltList entry (RW 0x79A5EC); the flag clears, the object gets the model condition JUST_BUILT (word + 0x124 bit 26) and the function returns true.
//   * unpack (RW 0x79BE6A): the castle's own footprint leaves the pathfinder map (RW 0x6E85FB); the layout of the template named by CastleToUnpackForFaction for the owner's
//     faction (RW 0x798F70) is read entry by entry from the CastleTemplates store (RW 0xDE77A0, RW 0x72D72F) and each entry makes an owned object (RW 0x7987EE):
//       - the template is looked up by the entry's name; one with KindOf OPTIMIZED_PROP is skipped (RW 0x798850); WALK_ON_TOP_OF_WALL sets module + 0x44;
//       - the team is the castle's when the template passes FilterValidOwnedEntries (RW 0x763543, no players), else the neutral player's default team (RW 0x798885);
//       - the position is the castle's transform applied to the entry's position (rotation by the castle's orientation, translation), the angle is the entry's plus the
//         castle's, normalised (RW 0x798899 ff); a template that is not a COMMANDCENTER must pass the placement legality with options CLEAR_PATH | NO_OBJECT_OVERLAP (RW 0x797A96
//         with 5), else the entry is skipped;
//       - the object is made by the creation interface at module + 0x20 (RW 0x859198) and recorded in the castle's owned list; the CastleMemberBehavior of every owned object
//         learns its castle (RW 0x79BFC9: module + 0x14 / + 0x18);
//     then the castle centre gets the status UNSELECTABLE (RW 0x79C07E), its drawable is hidden, the model flags / pathfinder footprint are updated, and the state is 4.
// INFERENCE (stop S-303): the owned objects' creation interface RW 0x859198 beyond placing the object (position, angle, team, the structure notifications of the starting base),
// the faction decal, the damage evacuation of the member (EvacuateDamage is its own module), CastleUpgrade (UPGRADE-1) are not ported. The castle after its unpack is lane
// CASTLE-1's (below; stops S-950 .. S-952).
//
// Lane CASTLE-1 (TARGET FACTS, RotWK game.dat, S-001 caveat; static disassembly; the module's fields are offsets of the CastleBehavior module, `this` of RW 0x79CF2A is module + 0x10):
//   * the data constructor RW 0x79C543: DecalSize 1.0, FadeTime 2.0, UnpackDelayTime 2.0, BuildTime 5.0, ScanDistance 100.0, MaxCastleRadius 0, CrewPrepareTime 18 frames, CrewPrepareInterval
//     10 frames, TransferFoundationHealthToCastleUponUnpack true, FilterCrew NONE (RW 0x763D11), FilterValidOwnedEntries ANY +STRUCTURE +WALK_ON_TOP_OF_WALL +BASE_FOUNDATION
//     +BASE_DEFENSE_FOUNDATION +TACTICAL_MARKER (RW 0x79C643 ff).
//   * registerOwnedObject RW 0x79AC19 (after every object the unpack makes, RW 0x79B903, and the structures built on the castle itself, RW 0x79B98B): the member learns its castle
//     (+ 0x18); KindOf DO_NOT_PICK_ME_WHEN_BUILDING sets the status DO_NOT_PICK_ME; then by KindOf: CASTLE_KEEP: the first is the keep (+ 0x38, the castle's own foundation occupant
//     RW 0x8582DE, its member's + 0x14 / + 0x24), a second one is destroyed; BASE_DEFENSE_FOUNDATION: list + 0x74; BASE_FOUNDATION or WALL_UPGRADE: list + 0x50 (a WALL_UPGRADE loses the
//     status NO_ATTACK); else FilterCrew and FilterValidOwnedEntries both accept (with the castle's player): the crew list + 0x68 (RW 0x798397 prepares it); else list + 0x5C.
//   * update RW 0x79CF2A: the timer (+ 0x40, seconds) first loses 1 / LOGICFRAMES_PER_SECOND (SSE, single precision) while above 0; at or below 0 it is set to 0 and `expired` is
//     set. State 0: the instant unpack (RW 0x79C265, then state 4) and the capture scan RW 0x79B3C4; the module sleeps 5 frames. State 4: RW 0x799ACB decides; true -> abandon
//     RW 0x79CB47, false -> the crew (RW 0x79B0F1, a no-op with no crew). State 5 on `expired`: state 0, pack RW 0x79CCF2, timer = UnpackDelayTime. States 1 .. 3 are the timed
//     unpack a player starts (initiateUnpack RW 0x79C17D, the MSG dispatcher RW 0x77C32E): not ported (S-951). Every state but 0 sleeps 1 frame.
//   * RW 0x799ACB: false before frame ftol(5 * SecondsBeforeBaseCheckActive) (the VictoryConditions delay). The keep is gone (+ 0x3D) when it no longer exists, or is effectively dead,
//     or has the model condition AWAITING_CONSTRUCTION or POST_COLLAPSE, or PARTIALLY_CONSTRUCTED with its builder (+ 0x7C) gone or dead and a DOZER. A keep that no longer exists or
//     is dead with KeepDeathKillsEverything kills everything instead (RW 0x7999E2 twice: RW 0x797F16 kills (UNRESISTABLE, NORMAL) every member that is not a BASE_FOUNDATION and the
//     occupant of every one that is, in the order: the castle, its occupant, lists + 0x74, + 0x50, + 0x68, + 0x5C; RW 0x797F49 destroys every BASE_FOUNDATION of the same walk, the
//     castle included), sets + 0x3D and destroys the castle: the castle then counts for nothing (VictoryConditions sees no object). Otherwise, with the keep gone, the castle is
//     abandoned when no foundation of the lists + 0x50 and + 0x74 that is not a WALL_UPGRADE holds an occupant (FoundationAIUpdate interface slot 0xC: occupant + 0x28 != 0).
//   * abandon RW 0x79CB47: the camp-destroyed EVA (RW 0x799440, client), state 5, the units near the castle are sent out (RW 0x79C78C), the members fade (client), the castle loses
//     JUST_BUILT, gets PACKING instead of UNPACKING and loses UNSELECTABLE, the keep and the list + 0x5C get the status NO_ATTACK (RW 0x79A017), timer = FadeTime.
//   * pack RW 0x79CCF2: every object of the lists + 0x50, + 0x5C (that first loses the status 83), + 0x74 and the keep is destroyed and the lists cleared, + 0x44 cleared, the crew
//     released (RW 0x79A237(1)), the castle joins PlyrCivilian's default team (RW 0x69954A), the units are sent out again, its upgrades reset (RW 0x68E083) and its footprint returns
//     to the pathfinder (RW 0x6E85E9).
//   * the instant unpack RW 0x79C265 in full: the owner is PreBuiltPlyr's player (the first of the list with that name; none: nothing unpacks) or the castle's; the castle joins its
//     team, unpacks, and each PreBuiltList entry whose template exists builds INSTANT (RW 0x79A5EC: the BASE_DEFENSE_FOUNDATION pads for an FS_BASE_DEFENSE template when there are
//     any, else the foundations; index -2: the first pad that takes it, else the index clamped to the last pad with an unsigned compare; RW 0x79A518: the pad exists, is no
//     WALL_UPGRADE, its foundation holds nothing, then the creation interface RW 0x859198 with the pad's position, angle 0 and the pad's player -> construct RW 0x858701); then JUST_BUILT
//     and the keep / members lose DO_NOT_PICK_ME (RW 0x79A017(0x4F, 0)).
//   * capture scan RW 0x79B3C4 (state 0, no instant build pending, ScanDistance > 0, InstantUnpack off): the live objects within ScanDistance of KindOf INFANTRY / CAVALRY /
//     MONSTER / MACHINE and not NO_BASE_CAPTURE, not PlyrCivilian's: an owner that is not ALLIES with an owner met before contests the castle; each owner scores w = 2 for a human,
//     1 for a computer (Player + 0x5C), the first object 2w + w * CommandPoints, the next w + w * CommandPoints; the best score (the lowest index on a tie) of an uncontested scan
//     takes the castle when its team differs and CastleToUnpackForFaction names its side (RW 0x7998FF); else, after frame 5, the castle returns to PlyrCivilian.
//   * unpack RW 0x79BE6A (both forms): the castle's footprint leaves the pathfinder, + 0x9C / + 0x44 reset, the layout (or the explicit object of + 0x98) is made, every member
//     learns the keep (member + 0x14 = the keep's id, RW 0x7990D9: BUILD-1's "occupant" reading of that field is wrong, S-952), + 0x48 = the frame, the castle gets UNPACKING
//     instead of PACKING and UNSELECTABLE, the walk-on-wall members' footprints are added (RW 0x799E3F), and the keep takes the castle's price (truncated) and script name
//     (ScriptEngine RW 0x759467) while the castle is renamed "No Name". RW 0x683955 (when RW 0xDE4690 is set) and RW 0x79B881 (the layout's second list) were not read.
// The partition queries run on ThePartitionManager (lane MODULES-2): the capture scan RW 0x79B4D2 is distance type 0 unsorted, the push out RW 0x79C826 type 3 unsorted.
// INFERENCE / NOT PORTED (stop S-951, S-952): the push out's geometry overlap filter (RW 0x67C5F3, vtable RW 0xC112C8 -> RW 0xAD2CE0) and the AI's slot 0x1C8 test are not ported; the occupant of a plot is the port's CastleMemberBehavior occupant that still exists
// (retail clears the plot's + 0x28 in the plot's own update, RW 0x8584DB, one frame later); the crew (RW 0x79B0F1 / 0x798397 / 0x798344), the castle's alert map (+ 0xA0), the stored
// price (+ 0x4C), RW 0x625E0A and the client parts (EVA, fades, decals) are not ported.
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DamageModule.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ObjectFilter;
class ModuleFactory;
class Player;
class ThingTemplate;

class CastleBehaviorModuleData : public FoundationAIUpdateModuleData
{
public:
	std::vector<std::pair<std::string, std::string>> m_castleToUnpackForFaction; // +0x68: (faction, base name)
	std::shared_ptr<const ObjectFilter> m_filterValidOwnedEntries;                // +0x30
	std::shared_ptr<const ObjectFilter> m_filterCrew;                             // +0x34
	std::string m_factionDecal;                                                   // +0x5C (parser RW 0x79CA81, retail data does not use it: kept as the line)
	std::vector<std::pair<std::string, int>> m_preBuiltList;                      // +0x50: (template, pad index; -2: the first free pad)
	std::string m_preBuiltPlyr;                                                   // +0x10
	std::string m_decalName;                                                      // +0x14
	// defaults: the data constructor RW 0x79C543 (lane CASTLE-1)
	float m_decalSize = 1.0f, m_fadeTime = 2.0f, m_unpackDelayTime = 2.0f, m_buildTime = 5.0f, m_scanDistance = 100.0f, m_maxCastleRadius = 0.0f; // +0x18 .. +0x2C
	std::uint32_t m_crewPrepareTime = 18, m_crewPrepareInterval = 10;             // +0x38, +0x40 (frames)
	bool m_instantUnpack = false, m_keepDeathKillsEverything = false, m_summoned = false; // +0x3C, +0x3D, +0x3E
	std::string m_crewReleaseFX, m_crewPrepareFX;                                 // +0x48, +0x44
	bool m_disableStructureRotation = false;                                      // +0x74
	std::string m_evaEnemyCastleSightedEvent;                                     // +0x4C
	bool m_transferFoundationHealthToCastleUponUnpack = true;                     // +0x75
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CastleBehavior : public FoundationAIUpdate, public CreateModuleInterface
{
public:
	// RW +0x34 of the module
	enum State
	{
		STATE_IDLE = 0,       // the starting state (RW 0x79A901); a packed castle waits here for a capture
		STATE_PACKED = 1,     // initiateUnpack(false) (RW 0x79C17D)
		STATE_UNPACKING = 2,  // RW 0x79CF2A state 2
		STATE_BUILDING = 3,   // RW 0x79CF2A state 3
		STATE_UNPACKED = 4,   // after an unpack
		STATE_ABANDONED = 5   // RW 0x79CB47: fading out until FadeTime runs out, then packed (RW 0x79CCF2)
	};
	CastleBehavior(Thing *thing, const CastleBehaviorModuleData *data);
	UpdateSleepTime update() override;
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override; // RW 0x798238
	// lane SCRIPT-2: RW 0x798F2B (module slot 0x24): the castle's new owner takes the keep (FoundationAIUpdate RW 0x858BCB: the occupant) and every object of the
	// lists + 0x50, + 0x5C, + 0x68, + 0x74 (RW 0x798EAD each): setTeam(the new owner's default team). The skirmish AI's base notifications of both players
	// (RW 0x6C778A / 0x6C7779) are not ported (S-1186)
	void onCapture(Player *oldOwner, Player *newOwner) override;
	void crc(StateHasher &hasher) const override;

	const CastleBehaviorModuleData *data() const { return m_data; }
	int state() const { return m_state; }
	bool needsInstantBuild() const { return m_needInstantBuild; }
	// every object the unpack made, in creation order
	const std::vector<ObjectID> &ownedObjects() const { return m_owned; }
	// RW + 0x38 the keep; + 0x50 the foundations (BASE_FOUNDATION / WALL_UPGRADE), + 0x5C the other members, + 0x68 the crew, + 0x74 the BASE_DEFENSE_FOUNDATIONs
	ObjectID keepId() const { return m_keepId; }
	const std::vector<ObjectID> &foundations() const { return m_foundations; }
	const std::vector<ObjectID> &members() const { return m_members; }
	const std::vector<ObjectID> &crew() const { return m_crew; }
	const std::vector<ObjectID> &defenseFoundations() const { return m_defenseFoundations; }
	bool keepGone() const { return m_keepGone; }
	float timer() const { return m_timer; }
	// RW 0x79BE6A: makes the layout's objects (see the file comment); false + the reason in lastError()
	bool unpack();
	const std::string &lastError() const { return m_lastError; }
	// the base name of the owner's faction (RW 0x798F70); empty when the data has none for it
	std::string baseNameFor(const Player &owner) const;
	// RW 0x7998FF: CastleToUnpackForFaction names the player's side
	bool isPlayerAllowedToCapture(const Player &player) const;
	// RW 0x79AC19
	void registerOwnedObject(Object &obj);
	// lane CASTLE-1: the stop lines S-950 .. S-952 (in GameLogic::report through BuildStops)
	static std::vector<std::string> stopLines();

private:
	// RW 0x7987EE
	Object *createOwnedObject(const struct CastleTemplateEntry &entry, Player &owner);
	// RW 0x79C265
	bool tryInstantUnpack();
	// RW 0x79A5EC / 0x79A518: a PreBuiltList entry
	Object *preBuild(const ThingTemplate &tt, int index, bool instant);
	Object *buildOnPad(ObjectID padId, const ThingTemplate &tt, bool instant);
	// RW 0x799ACB (may kill everything and destroy the castle: KeepDeathKillsEverything)
	bool shouldAbandon();
	// RW 0x7999E2 with RW 0x797F16 (kill = true) or RW 0x797F49 (kill = false)
	void forEachMember(bool kill);
	// RW 0x79CB47
	void abandon();
	// RW 0x79CCF2
	void pack();
	// RW 0x79C78C
	void pushUnitsOut();
	// RW 0x79B3C4
	void scanForCapture();
	// RW 0x79A017
	void setMembersStatus(int status, bool on);
	// RW 0x79A237
	void releaseCrew(bool kill);
	// the occupant of a foundation (the castle's: the keep; a plot's: its member's occupant) that still exists, or null
	Object *occupantOf(const Object &foundation) const;

	const CastleBehaviorModuleData *m_data;
	int m_state = STATE_IDLE;
	bool m_needInstantBuild = false;   // RW + 0x3C
	bool m_keepGone = false;           // RW + 0x3D
	bool m_hasWalkOnWall = false;      // RW + 0x44
	float m_timer = 0.0f;              // RW + 0x40 (seconds)
	ObjectID m_keepId = INVALID_ID;    // RW + 0x38
	UnsignedInt m_unpackFrame = 0;     // RW + 0x48 (the frame of the last unpack: the crew's clock)
	std::vector<ObjectID> m_owned;
	std::vector<ObjectID> m_foundations, m_members, m_crew, m_defenseFoundations; // RW + 0x50, + 0x5C, + 0x68, + 0x74
	std::string m_lastError;
};

class CastleMemberBehaviorModuleData : public ModuleData
{
public:
	std::string m_campDestroyedOwnerEvaEvent, m_campDestroyedAllyEvaEvent, m_campDestroyedAttackerEvaEvent; // +8, +0xC, +0x10
	std::string m_beingBuiltSound;                                                                           // +0x14
	bool m_storeUpgradePrice = false;                                                                        // +0x18
	bool m_countsForEvaCastleBreached = false;                                                               // +0x19
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CastleMemberBehavior : public BehaviorModule, public DamageModuleInterface
{
public:
	CastleMemberBehavior(Thing *thing, const CastleMemberBehaviorModuleData *data);
	const CastleMemberBehaviorModuleData *data() const { return m_data; }
	void crc(StateHasher &hasher) const override;
	// RW module + 0x18: the object id of the castle this piece belongs to (INVALID_ID: none); RW + 0x14: the occupant of a plot (the structure being built on it)
	ObjectID castleId() const { return m_castleId; }
	void setCastleId(ObjectID id) { m_castleId = id; }
	ObjectID occupantId() const { return m_occupantId; }
	void setOccupantId(ObjectID id) { m_occupantId = id; }
	// RW 0x79A09D (the plot is taken): an occupant is recorded and it is alive
	bool plotIsTaken() const;
	// lane COMBAT-2: the DAMAGE interface (RW vtable 0xC308BC: onDamage RW 0x79B757, onBodyDamageStateChange RW 0x79A0ED). onDamage (the amount dealt > 0) tells the castle behaviour of the
	// member the frame it was hit (RW 0x79B374: the castle's alert / crew bookkeeping, a map of frames at castle + 0xA0: not ported, counted); a member that reaches RUBBLE (state 3) sets
	// its breached flag (module + 0x15) and, when CountsForEvaCastleBreached, records a breach event (CombatState::castleBreaches: the client plays the EVA when the owner is its local player); a state
	// below REALLYDAMAGED (2) clears the flag (a repaired member)
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;
	void onBodyDamageStateChange(BodyDamageType oldState, BodyDamageType newState) override;
	bool breached() const { return m_breached; }

private:
	bool m_breached = false; // RW + 0x15
	const CastleMemberBehaviorModuleData *m_data;
	ObjectID m_castleId = INVALID_ID;
	ObjectID m_occupantId = INVALID_ID;
};

namespace CastleModules
{
void registerAll(ModuleFactory &modules);
}
