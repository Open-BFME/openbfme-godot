// OpenBFME. GPL-3.0.
//
// Lane WALL-2: the portal modules of RotWK, DynamicPortalBehaviour (the postern gates, the castle elevators and stairs, the siege ladders and towers) and
// FakePathfindPortalBehaviour (a skirmish AI player's own closed gate counts as a way through). They put portal waypoints into the pathfinder through its
// portal API (GameLogic/AI/AIPathfind.h, "THE PORTAL API"); the pathfinder's searches cross them and the mover walks them (AIPathfindPortals.cpp, AIMove.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 WorldBuilder decompile names the functions):
// DynamicPortalBehaviour (module constructor RW 0x863C29 over the UpgradeModule base RW 0x863B8F; vtables: primary RW 0xC590AC, behaviour interface + 0xC RW
// 0xC58FF0, mux + 0x10 RW 0xC58FA8, die + 0x1C RW 0xC58FA0, create + 0x20 RW 0xC58F90):
//   * the data (constructor RW 0x86450D; the UpgradeModule table RW 0x8D26E0 first, then RW 0xC59220): BonePrefix (parseAsciiString, + 0x13C), NumberOfBones
//     (parseInt RW 0x42EC5E, + 0x138, 0), WayPoint (RW 0x86441E, + 0x140: `Index:<int> Type:<Walk|Climb|PreClimb>` appended as {index, type 2 / 3 / 4}; another
//     type name leaves the type uninitialised: S-4853), Link (RW 0x864794, + 0x14C: `From:<n> [Via:<n>]* To:<n>`, appended as the list from, vias..., to; the
//     label of the last pair is not checked), WallBoundsMesh (parseAsciiString, + 0x158), GenerateNow (parseBool, + 0x15C, false), AboveWall
//     (parseUnsignedInt RW 0x42ECB2, + 0x160, -1), TopAttackPos (parseCoord3D, + 0x164, 0), TopAttackRadius (parseReal, + 0x170, 5.0), AllowEnemies
//     (parseBool, + 0x15D, false), ActivationDelaySeconds (parseReal, + 0x174, 0.0), ObjectFilter (RW 0x76392F, + 0x178, the default filter RW 0x76406F);
//   * the module: six waypoint slots (+ 0x24 .. + 0x38), created (+ 0x3C) and disabled (+ 0x3D), all clear;
//   * createWaypoints (RW 0x86404D): when not created: the bones BonePrefix01 .. (NumberOfBones of them, RW 0x68C650 getMultiLogicalBonePosition into a
//     16-slot array, to the world); the activation frame = _ftol2(fild LOGICFRAMES_PER_SECOND (5) * ActivationDelaySeconds) + the logic frame when the delay
//     is above 0.0 (x87 under precision control 24), else none; for every WayPoint entry in order, slot i gets a new waypoint (RW 0x6829B0 with the id
//     0x7FFFFFFE, name "#dynamicportal_wp") at bone[index] of the entry's type, owner = the object's id, the filter (+ 0xB8 = 1, + 0xBC = the data's
//     filter), AllowEnemies (+ 0xA8), the activation frame (+ 0xB4) when above 0; then created. Then, created before or not, for every Link: when it was not
//     created before this call, addLink(slot[from], slot[to]) (RW 0x67262C); always AddPortal(slot[from]) (RW 0x6EA42D); when not created before, the vias:
//     for i in 0 .. n - 3: slot[list[i + 1]] is no portal (+ 0x48 = 0) and slot[list[i]]->next (+ 0x44) = slot[list[i + 1]];
//   * the gate (RW 0x864314): createWaypoints when not disabled and (GenerateNow or created);
//   * the gate runs from the behaviour interface's slot 45 (+ 0xB4, RW 0x864341: unless TheGameLogic + 0x6E, a save being loaded; Object RW 0x694692
//     broadcasts the slot after the map objects are placed, RW 0x62F36B / 0x62F8EA, and after FoundationAIUpdate places a structure, RW 0x858A94), from
//     loadPostProcess (primary slot 1, RW 0x864331) and from the create interface's onBuildComplete (RW 0x8643EC, which clears disabled first);
//   * onDie (the die interface's slot 0, RW 0x863B28): clearPortals, then disabled;
//   * upgradeImplementation (mux slot 10, RW 0x864357): the custom anim on (RW 0x8D28F1); with a drawable and a WallBoundsMesh: the drawable takes it (RW
//     0x672F68) and the object leaves and enters the pathfinder's map (RW 0x6E85FB / 0x6E85E9); createWaypoints (NOT the gate); then, with an AI (+ 0x260),
//     the object's FoundationAIUpdate interface (RW 0x68C3C3) takes the object's id through its slot 2 (RW 0x8582DE, the occupant);
//   * the removal (mux slot 8, RW 0x863D3A): mux slot 9 (not executed), clearPortals, the custom anim off (RW 0x8D28F9), then with an AI the FoundationAIUpdate
//     interface's slot 2 with 0;
//   * clearPortals (RW 0x8639EF): when created: every slot's waypoint that is a portal (RW 0x6E82FD) is removed (RemovePortal RW 0x6EA48A), then deleted;
//     the pathfinder's RW 0x6E8454 (paths through a removed portal); created cleared. The destructor (RW 0x863CD5) runs it too;
//   * isEndPortalAboveWall(pos) (RW 0x863EB3, the siege lane's): AboveWall outside 0 .. WayPoint count - 1: pos = the object's position, true; else the bones
//     again, pos = bone[WayPoint[AboveWall].index], the answer is Pathfinder RW 0x6EA857(pos, 0) (the ground cell's layer is a wall);
//   * topAttackPosition (RW 0x863D77) and TopAttackRadius (RW 0x863D30) are read by the pathfinder RW 0x6EDBE7 (a deployed siege engine's top): the siege
//     lane's;
//   * the module of an object (RW 0x863983: the first module whose name key is "DynamicPortalBehaviour").
// FakePathfindPortalBehaviour (module constructor RW 0x864A27 over the UpdateModule base RW 0x653114; vtables: primary RW 0xC59348, update + 0x10 RW 0xC5933C,
// die + 0x20 RW 0xC59338, create + 0x24 RW 0xC59328):
//   * the data (RW 0x864A05: the UpgradeModule table first, then RW 0xC592F8): AllowEnemies (parseBool, + 0x138), AllowNonSkirmishAIUnits (parseBool, + 0x139);
//   * the module: two waypoints (+ 0x28 / + 0x2C), created (+ 0x30), dead (+ 0x31), registered (+ 0x32), the gate (+ 0x34);
//   * the update (RW 0x864EAA): no skirmish AI runs the controlling player (TheSkirmishAIManager RW 0x6A950B): sleep forever. The gate (RW 0x89BEE0, the
//     object's GateOpenAndCloseBehavior) found once (none: sleep forever); not created: the creation (RW 0x864E98: unless dead or created); then registered
//     and the gate open (its slot 6): unregistered (RW 0x8649B7: RemovePortal both, RW 0x6E8454); not registered and the gate not open: registered (RW
//     0x86498E: AddPortal both); sleep 1 frame;
//   * the creation (RW 0x864D02): d = 10.0 (RW 0xDAE904) + (the local bounds' min x (RW 0xAD2040 over the active shapes) when above 0.0, else their max x);
//     c = (float)cos(angle), s = (float)sin(angle) (CRT); the offset (cos * d - sin * 0, d * sin + cos * 0) (SSE); the waypoints (RW 0x864B29: type 7, name
//     "#fakepathfindportal_wp", owner = the object, AllowEnemies, AllowNonSkirmishAIUnits) at the position + the offset and the position - the offset (0 -
//     the offset), each at the terrain's height there (TheTerrainLogic vslot 0x18); each links to the other; registered; created;
//   * onDie (RW 0x8649ED): the clear (RW 0x86491E: as clearPortals, for the two), dead; onBuildComplete (RW 0x864A00): not dead.
// The mover's side of a type 7 waypoint: the path is dropped and the unit idles (RW 0x662A0E, AIMove.cpp); the pathfinder's: the waypoint refuses a unit
// without an AI or inside a move-away search (RW 0x68230B, AIPathfindPortals.cpp).
// INFERENCE / NOT PORTED (stops S-4853 .. S-4855, docs/STOPS.md): the order of the map placement broadcast (RW 0x62DCE4 sorts the objects: the creation order
// here); saves (loadPostProcess, TheGameLogic + 0x6E); the drawable's WallBoundsMesh change (no retail DynamicPortalBehaviour sets WallBoundsMesh: reported);
// the FoundationAIUpdate slot 2 call (RW 0x8582DE's statuses and drawable calls are not ported: reported); a WayPoint index without its bone (retail reads an
// uninitialised slot: reported, nothing is made); more than six WayPoint entries (retail writes past its six slots: reported, nothing is made).

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/SiegePortalInterface.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
class PathfindObject;
class Object;
struct ObjectFilter;

class DynamicPortalBehaviourModuleData : public UpgradeModuleData
{
public:
	struct WayPointEntry
	{
		int index = 0; ///< the bone
		int type = 0;  ///< PathfindWaypointType (2 Walk, 3 Climb, 4 PreClimb; 0: an unknown type name, S-4853)
	};
	int m_numberOfBones = 0;                    ///< + 0x138
	std::string m_bonePrefix;                   ///< + 0x13C
	std::vector<WayPointEntry> m_wayPoints;     ///< + 0x140
	std::vector<std::vector<int>> m_links;      ///< + 0x14C: from, the vias, to
	std::string m_wallBoundsMesh;               ///< + 0x158
	bool m_generateNow = false;                 ///< + 0x15C
	bool m_allowEnemies = false;                ///< + 0x15D
	int m_aboveWall = -1;                       ///< + 0x160
	Coord3D m_topAttackPos{ 0.0f, 0.0f, 0.0f }; ///< + 0x164
	float m_topAttackRadius = 5.0f;             ///< + 0x170
	float m_activationDelaySeconds = 0.0f;      ///< + 0x174
	std::shared_ptr<const ObjectFilter> m_objectFilter; ///< + 0x178; null: the default filter (RW 0x76406F)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DynamicPortalBehaviour : public UpgradeModule, public CreateModuleInterface, public DieModuleInterface, public SiegePortalInterface
{
public:
	OBFME_MODULE_XFER(DynamicPortalBehaviour) // the save (SAVE-4b): UpgradeModule's, the portal waypoints' ids and the made / disabled flags (the waypoints are the pathfinder's, obfme:AIWorld)
	enum { MAX_WAYPOINTS = 6 };
	DynamicPortalBehaviour(Thing *thing, const DynamicPortalBehaviourModuleData *data);
	~DynamicPortalBehaviour() override;
	CreateModuleInterface *getCreate() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override;                 // RW 0x8643EC
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x863B28
	void onMapPlacement(const Dict *properties) override; // behaviour interface slot 45, RW 0x864341
	void onDelete() override;
	void crc(StateHasher &hasher) const override;

	// ---- the siege lane's API (SiegeDeploySpecialPowerUpdate: RW 0x8CA18E creates, RW 0x8C9BDA clears) ----
	// RW 0x86404D: the waypoints from the bones (once) and the links' portals; false when nothing could be made (no pathfinder, a bone missing: S-4853)
	bool createWaypoints();
	// RW 0x8639EF: the portals removed, the waypoints deleted, the paths through them told
	void clearPortals();
	// RW 0x863EB3: the end waypoint (AboveWall) stands on a wall's cell; `pos` (may be null) receives its position (the object's without AboveWall)
	bool isEndPortalAboveWall(Coord3D *pos) const;
	// RW 0x863D30
	float topAttackRadius() const override { return m_data->m_topAttackRadius; }
	// ---- SiegePortalInterface (lane SIEGE-1's deploy asks the ladder's / tower's portal through it; the merge of WALL-2 and SIEGE-1 connects them) ----
	bool isEndPortalAboveWall(Coord3D *out) override { return static_cast<const DynamicPortalBehaviour *>(this)->isEndPortalAboveWall(out); }
	void createPortalWaypoints() override { createWaypoints(); }
	void clearPortalWaypoints() override { clearPortals(); }
	// RW 0x863D77: TopAttackPos's X and Y scaled by the length of the object's planar X axis normalised by its larger component, Z as is, through the
	// object's transform (RW 0x70BFD1). INFERENCE (S-4940): the arithmetic order of RW 0x863D77 was taken from lane SIEGE-1's reading, not re-read
	Coord3D topAttackPosition() override;
	const DynamicPortalBehaviourModuleData *portalData() const { return m_data; }
	bool created() const { return m_created; }
	bool disabled() const { return m_disabled; }
	// the waypoint ids of the six slots (PathfindWaypoint::NO_ID when empty)
	const std::array<int, MAX_WAYPOINTS> &waypointIds() const { return m_waypoints; }
	// RW 0x863983
	static DynamicPortalBehaviour *of(const Object &obj);
	// the unit filter of this module's waypoints (+ 0xB8 / + 0xBC: RW 0x7640C1(the data's filter, unit, 0) in Waypoint::isValidUnitType)
	std::function<bool(const PathfindObject &)> waypointFilter() const;

	static void registerClass(ModuleFactory &modules);

protected:
	void upgradeImplementation() override;  // RW 0x864357
	void processUpgradeRemoval() override;  // RW 0x863D3A

private:
	void gate(); // RW 0x864314
	// the bones (RW 0x68C650 to the world); the count found
	int bones(std::vector<Coord3D> &out) const;
	void foundationOccupant(bool set);

	const DynamicPortalBehaviourModuleData *m_data;
	std::array<int, MAX_WAYPOINTS> m_waypoints; ///< + 0x24 .. + 0x38 (the ids)
	bool m_created = false;  ///< + 0x3C
	bool m_disabled = false; ///< + 0x3D
};

class FakePathfindPortalBehaviourModuleData : public UpgradeModuleData
{
public:
	bool m_allowEnemies = false;            ///< + 0x138
	bool m_allowNonSkirmishAIUnits = false; ///< + 0x139
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FakePathfindPortalBehaviour : public UpdateModule, public CreateModuleInterface, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(FakePathfindPortalBehaviour) // the save (SAVE-4b): UpdateModule's, the two waypoint ids and the flags
	FakePathfindPortalBehaviour(Thing *thing, const FakePathfindPortalBehaviourModuleData *data);
	~FakePathfindPortalBehaviour() override;
	CreateModuleInterface *getCreate() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override { m_dead = false; } // RW 0x864A00
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x8649ED
	void onDelete() override;
	UpdateSleepTime update() override; // RW 0x864EAA
	void crc(StateHasher &hasher) const override;

	bool created() const { return m_created; }
	bool registered() const { return m_registered; }
	int waypoint(int i) const { return m_waypoints[(size_t)i]; }

	static void registerClass(ModuleFactory &modules);

private:
	void create();     // RW 0x864D02
	void doRegister(); // RW 0x86498E
	void unregister(); // RW 0x8649B7
	void clear();      // RW 0x86491E
	int makeWaypoint(const Coord3D &pos); // RW 0x864B29

	const FakePathfindPortalBehaviourModuleData *m_data;
	std::array<int, 2> m_waypoints; ///< + 0x28 / + 0x2C (the ids)
	bool m_created = false;    ///< + 0x30
	bool m_dead = false;       ///< + 0x31
	bool m_registered = false; ///< + 0x32
	bool m_gateFound = false;  ///< + 0x34 (the gate pointer, found once)
};

namespace PortalModules
{
void registerAll(ModuleFactory &modules);
// the stop lines the modules note at runtime (GameLogic::noteStop), in id order
std::vector<std::string> stopLines();
} // namespace PortalModules
