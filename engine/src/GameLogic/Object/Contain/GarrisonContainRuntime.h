// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GarrisonContain and HordeGarrisonContain at runtime (lane GARRISON-1): the contain of the structures that hold units (RotWK: the battle towers, the garrison tower
// expansions, the keeps of Gondor / Arnor, the sentry towers, the Elven battle tower, the Dwarven hall expansion and sentry tower, the Isengard battle tower, the neutral
// ruined towers; 2.01 data: 18 templates use HordeGarrisonContain, 2 GarrisonContain, see test_garrison_retail.cpp). One class ports both: the horde garrison's
// overrides are taken when the module was made for `HordeGarrisonContain`.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). The interfaces: module + 0x20 the contain interface (OpenContain RW 0xC59AF0, GarrisonContain RW 0xC5C698,
// HordeGarrisonContain RW 0xC5C9F0), + 0x10 the update interface (RW 0xC5F800: update RW 0x87C8DF), + 0x28 the die interface (OpenContain::onDie RW 0x867120),
// + 0x30 the exit interface (GarrisonContain RW 0xC5C664, HordeGarrisonContain RW 0xC5DCAC). Ported, each at the cited address:
//   * isValidContainerFor (OpenContain RW 0x86603B -> GarrisonContain RW 0x87B8C5 (its interface wrapper RW 0x87B935: NO_GARRISON, the capacity) -> HordeGarrisonContain RW 0x87D0E0) and the capacity (RW 0x87D252);
//   * addToContain: OpenContain RW 0x8674C2 (the list, the entering player's mask, onContaining, redeploy, the ENCLOSED world exit), the horde garrison's RW 0x87CFEB
//     (a horde: its garrisoned byte, OpenContain's add and the horde's world exit RW 0x990E5F; a member of a horde already inside: RW 0x990DEA, it rejoins its
//     horde and GarrisonContain::onContaining runs for it); GarrisonContain::onContaining RW 0x87BEC8; Object::onContainedBy RW 0x6901AE (the statuses of
//     ObjectStatusOfContained, the container id, the frame);
//   * removeFromContain: OpenContain RW 0x8665BE / 0x865EB6, the horde garrison's RW 0x87D04B (the exit delay), GarrisonContain::onRemoving RW 0x87BFA6, Object::
//     onRemovedFrom RW 0x69024C, OpenContain::addOrRemoveObjFromWorld RW 0x865D3D;
//   * the garrison points: loadGarrisonPoints RW 0x87C2B7 (the PassengerBonePrefix bones `<prefix>01..` of the three damage states through the drawable's pristine
//     bones, RW 0x68C650), redeployOccupants RW 0x87C631, removeInvalidObjectsFromGarrisonPoints RW 0x87C5D8, addValidObjectsToGarrisonPoints RW 0x87BBE8 (the
//     members of a garrisoned horde take the points: with a victim the point nearest to it), putObjectAtBestGarrisonPoint RW 0x87B83E, findClosestFreeGarrisonPointIndex
//     RW 0x87B6E1 with calcDistSqr RW 0x87B6B2 (x87 at 24 bits: ((dz*dz + dy*dy) + dx*dx)), putObjectAtGarrisonPoint RW 0x87B042, removeObjectFromGarrisonPoint RW 0x87B123,
//     findConditionIndex RW 0x87B0F1, trackTargets RW 0x87BD77;
//   * update RW 0x87C8DF (dead riders leave, the points, healObjects RW 0x87BE7C), recalcApparentControllingPlayer's GARRISONED condition RW 0x87C6CE;
//   * onDie RW 0x867120 (the DieMux test, EjectPassengersOnDeath: removeAllContained RW 0x87B4DC; else every rider is destroyed), onObjectCreated RW 0x87BA4C (InitialRoster);
//   * orderAllPassengersToExit (the horde garrison's RW 0x991027: a rider with a horde contain gets slot 0x84, another its AI's aiExit), the exit interface (isExitBusy
//     RW 0x87CE66: the frame of the next exit; exitObjectViaDoor RW 0x87D3CA -> RW 0x87CA2B: the members one by one with ExitDelay, then the horde object, each put at the
//     EntryPosition or beside it and sent along the exit path to the ExitOffset; the horde object takes its members back and re-forms, lane GARRISON-3).
// INFERENCE / NOT PORTED (stop S-1100, docs/STOPS.md, reported by stopLines()): the door model conditions and the exit / enter sounds of OpenContain::onRemoving /
// onContaining (client side: no audio event is played), ModifierToGiveOnExit, recalcApparentControllingPlayer's shroud and player parts (RW 0x87C6CE: the
// apparent controller is the owner), the radar, the passenger damage of DamagePercentToUnits (contain slot 0x148), healSingleObject (HealObjects: no 2.01 garrison
// sets it, reported when one does), MobileGarrison (RW 0x87C273), the killing branch of GarrisonContain::onRemoving (KillPassengersOnDeath, RW 0x87C09A: the fling
// and kill of an ejected rider), the contain's rally point of the exit path (exit slots 0x1C / 0x20, S-1620; the exit itself, RW 0x87CA2B with the cell test
// RW 0x6E8707 and the adjustment RW 0x6F3C87, is lane GARRISON-3's), the exit position search of removeAllContained (RW 0x87B3EC). Determinism: the
// contained list keeps insertion order, the points are an array, the members on the way a set ordered by id.

#pragma once

#include "GameLogic/Object/Contain/OpenContainRuntime.h"

#include <array>
#include <map>
#include <string>
#include <vector>

class ModuleFactory;
struct DieMuxData;
struct GarrisonContainModuleData;
struct HordeGarrisonContainModuleData;
struct OpenContainModuleData;

// lane GARRISON-2: the OpenContain parts are the base class's (OpenContainRuntime.h), as in RotWK (RW 0x87B1A7 calls OpenContain's constructor RW 0x867F56)
class GarrisonContain : public OpenContain
{
public:
	enum
	{
		MAX_GARRISON_POINTS = 40, // RW 0x87C69A: 0x28
		GARRISON_POINT_CONDITIONS = 3 // pristine, damaged, really damaged (RW 0x87C2B7)
	};
	// RW module + 0x100: 40 entries of 0x14 bytes; the port keeps the three fields the code reads (the other two dwords are written by RW 0x87B042 only)
	struct GarrisonPoint
	{
		ObjectID object = INVALID_ID;   // + 0x00
		ObjectID target = INVALID_ID;   // + 0x04 (the victim the point was chosen for)
		UnsignedInt placedFrame = 0;    // + 0x08
	};

	// `horde` is the HordeGarrisonContain data (null for GarrisonContain)
	GarrisonContain(Thing *thing, const ModuleData *data, const GarrisonContainModuleData &garrison, const DieMuxData &dieMux, const HordeGarrisonContainModuleData *horde);
	~GarrisonContain() override;

	// binds the typed data and the runtime of GarrisonContain and HordeGarrisonContain
	static void registerClasses(ModuleFactory &modules);
	// the stop lines of the port (S-1100 .. S-1103)
	static std::vector<std::string> stopLines();

	// ---- BehaviorModule ----
	void onObjectCreated() override; // the constructor's condition (HordeGarrisonContain RW 0x87D334); InitialRoster RW 0x87BA4C runs on the first update
	void onDelete() override;
	void crc(StateHasher &hasher) const override;
	// ---- UpdateModule (RW 0x87C8DF: every frame) ----
	UpdateSleepTime update() override;
	// ---- ContainModuleInterface ----
	bool addToContain(Object *obj) override;
	void removeFromContain(Object *obj) override;
	void containReactToTransformChange() override;
	bool isGarrisonable() const override { return true; }
	bool isDisplayedOnControlBar() const override { return true; } // slot 0xC8 RW 0x8BD372 (lane UI-1)
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override;
	// RW 0x87B8C5: OpenContain's entry test RW 0x86603B, then the container alive, not SOLD, not REALLYDAMAGED unless GARRISONABLE_UNTIL_DESTROYED
	bool garrisonAllows(const Object &obj) const;
	bool getEntryPosition(Coord3D &out) const override;
	bool getEntryOffset(Coord3D &out) const override;
	bool getExitOffset(Coord3D &out) const override;
	void orderAllPassengersToExit(int source) override;
	bool isExitBusy() const override;
	int reserveDoorForExit(const Object &obj) override;
	void exitObjectViaDoor(Object *obj, int door) override;

	// ---- queries (tests, the HUD, the launch) ----
	bool isHordeGarrison() const { return m_horde != nullptr; }
	// RW slot 0x114: the contained count; RW 0x87D252 (the horde garrison's capacity test)
	bool hasRoomFor(const Object &obj) const;
	int getContainMax() const override;
	// RW 0x87B0F1: 0 pristine, 1 damaged, 2 really damaged / rubble, -1 otherwise
	int findConditionIndex() const;
	const std::array<GarrisonPoint, MAX_GARRISON_POINTS> &points() const { return m_points; }
	int pointsInUse() const { return m_pointsInUse; }
	int pointCount(int condition) const { return condition >= 0 && condition < GARRISON_POINT_CONDITIONS ? m_pointCount[condition] : 0; }
	const Coord3D &pointPosition(int condition, int index) const { return m_pointPositions[condition][index]; }
	bool pointsLoaded() const { return m_pointsLoaded; }
	// the garrison point index `obj` holds, -1 (RW 0x87B4.. slot 0x6C getObjectGarrisonPointIndex)
	int getObjectGarrisonPointIndex(ObjectID id) const;
	UnsignedInt nextExitFrame() const { return m_nextExitFrame; }
	// RW 0x87B4DC: every rider leaves (onDie with EjectPassengersOnDeath, the evacuation of a dying building)
	void removeAllContained() override;

	// lane GARRISON-1 counters (reported)
	struct Stats
	{
		unsigned long long membersEntered = 0;   // horde members that rejoined their horde inside (RW 0x990DEA)
		unsigned long long pointsTaken = 0;      // putObjectAtGarrisonPoint
		unsigned long long pointsMoved = 0;      // trackTargets moves
		unsigned long long boneQueriesWithoutProvider = 0; // loadGarrisonPoints without a launch bone provider (no W3D assets): no points
		unsigned long long healUnported = 0;     // HealObjects set: not ported (S-1100)
		unsigned long long mobileUnported = 0;   // MobileGarrison set: not ported
	};
	const Stats &stats() const { return m_stats; }
	// lane GARRISON-3 counters of the exit RW 0x87CA2B (reported)
	struct ExitStats
	{
		unsigned long long exitPaths = 0;              // objects sent along the exit path (AI command 10)
		unsigned long long exitCellRetries = 0;        // the EntryPosition failed the cell test (RW 0x6E8707)
		unsigned long long exitAtContainerPosition = 0; // both fallbacks failed: the container's position
		unsigned long long hordeRegroups = 0;          // a horde object took its members back (slots 0x98 / 0x10)
	};
	const ExitStats &exitStats() const { return m_exitStats; }

protected:
	void onContaining(Object *obj, bool wasSelected) override; // RW 0x87BEC8
	void onRemoving(Object *obj) override;                     // RW 0x87BFA6
	const GarrisonContainModuleData &garrison() const { return *m_garrison; }

private:
	friend class GarrisonContainTestAccess;
	void recalcApparentControllingPlayer() override;           // RW 0x87C6CE (the parts ported)
	void redeployOccupants() override;                         // RW 0x87C631
	void loadGarrisonPoints();                        // RW 0x87C2B7
	void removeInvalidObjectsFromGarrisonPoints();    // RW 0x87C5D8
	void addValidObjectsToGarrisonPoints();           // RW 0x87BBE8
	void trackTargets();                              // RW 0x87BD77
	void putObjectAtBestGarrisonPoint(Object *obj, Object *target, const Coord3D *targetPos); // RW 0x87B83E
	int findClosestFreeGarrisonPointIndex(int condition, const Coord3D *targetPos) const;       // RW 0x87B6E1
	void putObjectAtGarrisonPoint(Object *obj, ObjectID target, int condition, int index);      // RW 0x87B042
	void removeObjectFromGarrisonPoint(Object *obj, int index);                                 // RW 0x87B123
	void exitOneObject(Object *obj, int door);                                                  // RW 0x87CA2B
	bool hordeMemberInside(const Object &obj, Object **horde) const;                            // RW 0x990CB9
	void transformPoint(const Coord3D &local, Coord3D &out) const;                              // RW 0x87CEB3
	bool memberHordeOf(const Object &obj, Object **horde) const;
	void createInitialRoster();                       // RW 0x87BA4C

	const GarrisonContainModuleData *m_garrison;
	const HordeGarrisonContainModuleData *m_horde;
	bool m_rosterCreated = false;
	bool m_hiddenFlag = false;                        // RW + 0x9BD
	std::array<GarrisonPoint, MAX_GARRISON_POINTS> m_points{};
	int m_pointsInUse = 0;                            // RW + 0x420
	std::array<std::array<Coord3D, MAX_GARRISON_POINTS>, GARRISON_POINT_CONDITIONS> m_pointPositions{}; // RW + 0x424
	int m_pointCount[GARRISON_POINT_CONDITIONS] = { 0, 0, 0 }; // RW + 0x9C4
	bool m_pointsLoaded = false;                      // RW + 0x9DC
	UnsignedInt m_nextExitFrame = 0;                  // HordeGarrisonContain RW + 0x9E0
	Stats m_stats;
	ExitStats m_exitStats;
};
