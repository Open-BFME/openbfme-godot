// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// OpenContain at runtime (lane GARRISON-2, moved out of GARRISON-1's GarrisonContain): the base of every ported contain class, as in RotWK, where GarrisonContain
// (RW constructor 0x87B1A7) and TransportContain (RW 0x86B67D, the base of HordeTransportContain RW 0x87A286, SiegeEngineContain RW 0x87F4E0 and
// HordeSiegeEngineContain RW 0x8804D7) call OpenContain's constructor RW 0x867F56 first. The contain interface sits at module + 0x20 (OpenContain's table RW 0xC59AF0);
// the derived classes override its slots and the main table's (TransportContain RW 0xC5A690 / 0xC5A808, see TransportContainRuntime.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001), each at the cited address:
//   * the entry test RW 0x86603B (isValidContainerFor's OpenContain part: destroyed, TEMPORARILY_DEFECTED, HOLDING_THE_RING, the enabled byte, COMBO_HORDE, the
//     PassengerFilter for the container's player, a MOUNTED object against a filter excluding CAVALRY, the relationship and the Allow* bytes);
//   * addToContain RW 0x8674C2 (the list RW 0x866987 with the STEALTH_GARRISON count, the entering player's mask, the contain slot 0x58 onContaining, the main slot
//     0x48 redeploy, Object::onContainedBy RW 0x6901AE, the apparent controller, the ENCLOSED world exit RW 0x865D3D);
//   * removeFromContain RW 0x8665BE -> 0x865EB6 (NO_ATTACK, PORTER_TAGGED, TAGGED, the list, the ENCLOSED world entry, the slot 0x5C onRemoving, Object::onRemovedFrom
//     RW 0x69024C); removeAllContained RW 0x866675;
//   * the die interface RW 0x867120 (the DieMux test; EjectPassengersOnDeath: slot 0xA8 removeAllContained; else every rider hidden and destroyed);
//   * orderAllPassengersToExit RW 0x867D6A (every rider with an AI: aiExit RW 0x7716C1); onObjectWantsToEnterOrExit (ZH);
//   * the exit interface (module + 0x30, RW 0xC59ABC): isExitBusy RW 0x9188EB (never), reserveDoorForExit RW 0x88B20C (DOOR_1), exitObjectViaDoor RW 0x8682D9.
// The derived classes' hooks (virtual, RW slot in the comment): onContaining (contain 0x58), onRemoving (0x5C), redeployOccupants (main 0x48), the apparent
// controller (main 0x50 / 0x54), isEnclosingContainerFor (the ENCLOSED status of the data), addToContainList (contain 0xA0), removeAllContained (0xA8).
// NOT PORTED (stop S-1100): the enter / exit sounds and the door model conditions of RW 0x865F9A / 0x8680F4 (client side), ModifierToGiveOnExit, the radar,
// DamagePercentToUnits' passenger damage (contain slot 0x148), the exposed stealth units' detection (RW 0x865EE7). Determinism: the list keeps insertion order.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <map>
#include <string>
#include <vector>

class AIUpdateInterface;
struct DieMuxData;
struct OpenContainModuleData;

class OpenContain : public UpdateModule, public ContainModuleInterface, public DieModuleInterface
{
public:
	OpenContain(Thing *thing, const ModuleData *data, const OpenContainModuleData &open, const DieMuxData &dieMux);
	~OpenContain() override;

	// ---- BehaviorModule ----
	ContainModuleInterface *getContain() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	void onDelete() override; // main slot 0x20 RW 0x86708F
	void crc(StateHasher &hasher) const override;
	// ---- UpdateModule (RW 0x86702D: every frame) ----
	UpdateSleepTime update() override;
	// ---- ContainModuleInterface ----
	const ContainedItemsList *getContainedItemsList() const override { return &m_contained; }
	unsigned getContainCount() const override { return (unsigned)m_contained.size(); }
	bool addToContain(Object *obj) override;    // RW 0x8674C2
	void removeFromContain(Object *obj) override; // RW 0x8665BE
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override; // RW 0x86603B
	const ObjectStatusMaskType *getObjectStatusOfContained() const override;
	void orderAllPassengersToExit(int source) override; // RW 0x867D6A
	void onObjectWantsToEnterOrExit(Object *obj, int wants) override;
	unsigned getStealthUnitsContained() const override { return m_stealthUnitsContained; }
	bool allowAlliesInside() const override;
	bool isExitBusy() const override { return false; }                                 // RW 0x9188EB
	int reserveDoorForExit(const Object &obj) override { (void)obj; return 0; }        // RW 0x88B20C
	void exitObjectViaDoor(Object *obj, int door) override;                            // RW 0x8682D9
	// contain slots 0x158 / 0x15C / 0x160 (RW 0x8657D1 / 0x8657B6 / 0x867ACD): where an entering unit walks to and where a rider leaves: the container's position;
	// a SHIP in water enters at the pathfinder's shore spot (RW 0x6EFBB8 from slot 0x158; slot 0x15C asks slot 0x158), not ported (counted: shipEntriesUnported,
	// S-1104: the ship's position stands)
	bool getEntryPosition(Coord3D &out) const override;
	bool getEntryOffset(Coord3D &out) const override;
	bool getExitOffset(Coord3D &out) const override;
	unsigned long long shipEntriesUnported() const { return m_shipEntriesUnported; }
	// a SHIP standing in water (TheTerrainLogic slot 0x4C at its position): RW's shore-spot branches (RW 0x6EFBB8) apply
	static bool isShipOnWater(const Object &container);
	// RW 0x6E8707 (ZH Pathfinder::validMovementTerrain) on the ground layer for `ai`'s current locomotor at `at`: an obstacle or impassable cell (types 4 / 5)
	// passes, a clear cell takes the surface test (see TransportContain::isSpecificRiderFreeToExit); false off the map. The caller tests the locomotor.
	static bool validMovementTerrain(AIUpdateInterface &ai, const Coord3D &at);
	// ---- DieModuleInterface (RW 0x867120) ----
	void onDie(const DieModuleInterface::Event &event) override;

	const OpenContainModuleData &openData() const { return *m_open; }
	// RW module + 0xDE (interface + 0xBE): the data's Enabled, read by the control bar's transport inventory (RW 0x94251F, lane UI-1)
	bool isEnabled() const { return m_enabled; }
	// contain slot 0x70 (OpenContain RW 0x86584C: the data's ContainMax; TransportContain RW 0x869F86: Slots)
	virtual int getContainMax() const;
	void containReactToTransformChange() override { redeployOccupants(); } // ZH OpenContain::containReactToTransformChange
	// the PassengerBonePrefix bone a rider stands on (RW 0x868A70's map at + 0x3C), "" without one
	std::string riderBone(ObjectID id) const;
	// RW 0x86603B: OpenContain's part of every contain's entry test
	bool openContainAllows(const Object &obj) const;
	// RW 0x6CAC1F's question: ObjectStatusOfContained has ENCLOSED (a rider leaves the world, its projectiles leave from the container)
	bool isEnclosingContainerFor(const Object &obj) const;
	bool ridersFireFromContainer() const;
	// slot 0xA8 RW 0x866675: every rider through RW 0x865EB6 until the list is empty (the derived classes add their own parts)
	virtual void removeAllContained();
	// RW 0x865D3D OpenContain::addOrRemoveObjFromWorld: a rider of an ENCLOSED container leaves the world and its drawable hides; back in, it is teleported to
	// the container (RW 0x696E63) and shown; the rider's own riders follow when this contain encloses them
	void addOrRemoveObjFromWorld(Object *obj, bool add);
	// contain slot 0x148 RW 0x867242 (DamagePercentToUnits on death): the riders and then the crew through RW 0x866B69
	void processDamageToContained();
	// main slot 0x64 (OpenContain RW 0x63F3BF: nothing; TransportContain RW 0x86B05F)
	virtual void killRidersWhoAreNotFreeToExit() {}
	// contain slot 0x11C: the second list RW 0x867242 walks (SiegeEngineContain's crew); null for the others
	virtual const ContainedItemsList *crewList() const { return nullptr; }
	unsigned long long shipExitsUnported() const { return m_shipExitsUnported; }
	unsigned long long turretBonesUnported() const { return m_turretBonesUnported; }

	struct OpenStats
	{
		unsigned long long entered = 0;          // objects added (addToContain)
		unsigned long long left = 0;             // removals
		unsigned long long ridersDestroyedOnDeath = 0;
		unsigned long long ejectedOnDeath = 0;
		unsigned long long exitedViaDoor = 0;    // RW 0x8682D9
	};
	const OpenStats &openStats() const { return m_openStats; }

protected:
	// ---- the hooks of the derived classes ----
	virtual void onContaining(Object *obj, bool wasSelected); // contain slot 0x58 (OpenContain RW 0x865F9A: the EnterSound, client side)
	virtual void onRemoving(Object *obj);                     // contain slot 0x5C (OpenContain RW 0x8680F4: the ExitSound, ModifierToGiveOnExit, client / S-1100)
	virtual void redeployOccupants();                         // main slot 0x48 (OpenContain RW 0x8671F5: the riders on their bones, RW 0x868A70)
	virtual bool forceOrientationContainer() const { return true; } // main slot 0x38 (OpenContain RW 0x9B501B; TransportContain RW 0x869F97: the data's)
	// contain slot 0x6C: a rider was placed (RW 0x868A70), `hidden` its drawable's state (OpenContain RW 0x8851E4: nothing)
	virtual void onRiderPlaced(Object *rider, bool hidden) { (void)rider; (void)hidden; }
	// RW 0x868A70: each rider (not RIDER_IS_PILOT) on the next free bone of its PassengerBonePrefix (`<prefix>01` .. through the drawable's pristine bones, RW 0x68C650,
	// else the single bone `<prefix>`, RW 0x68C5AF); a rider keeps its bone; one without a bone stands on the container (hidden) and, in a container whose riders may
	// fire (CAN_ATTACK, not ENCLOSED), takes NO_ATTACK; ForceOrientationContainer turns it with the bone
	void putRidersAtBones(const std::vector<Object *> &riders);
	// RW 0x868F0B .. 0x868F4B (lane COMBAT-3): PASSENGER_VARIATION_* off, then the BoneSpecificConditionState flags of bone number `boneNumber`
	void applyBoneSpecificConditionState(Object &rider, unsigned boneNumber);
	// main slot 0x44 RW 0x865BA6: a change of the container's model condition words redeploys the riders
	void monitorConditionChanges();
	// RW 0x68C5AF Object::getSingleLogicalBonePosition through the launch bone provider, then RW 0x70BCE7 (the object's transform); false without a provider or
	// without exactly one such bone
	static bool boneWorldPosition(Object &obj, const std::string &bone, Coord3D &out);
	virtual void recalcApparentControllingPlayer() {}         // main slots 0x50 / 0x54 / 0x58 (the apparent controller is the owner, S-1100)
	virtual void addToContainList(Object *obj);               // contain slot 0xA0 (RW 0x866987)
	virtual void removeFromContainList(Object *obj);          // main slot 0x34 (RW 0x865E78: the list erase)
	// contain slot 0xE8 (OpenContain RW 0x8662D0): `obj` is one of this contain's (SiegeEngineContain RW 0x87FEDA: its crew too)
	virtual bool isContainedHere(const Object &obj) const;
	// contain slot 0xB0(obj) (OpenContain RW 0x865C15: the data's ObjectStatusOfContained; SiegeEngineContain RW 0x87ED55: a crew member's ObjectStatusOfCrew)
	virtual const ObjectStatusMaskType &statusMaskFor(const Object *obj) const;
	// RW 0x8674C2's body (main slot 0x74 of the horde contains calls it directly)
	void openContainAdd(Object *obj);
	// RW 0x8665BE -> 0x865EB6
	void openContainRemove(Object *obj);
	void objectOnContainedBy(Object *rider); // RW 0x6901AE
	void objectOnRemovedFrom(Object *rider); // RW 0x69024C
	void processDamageToRider(Object *rider); // RW 0x866B69
	const DieMuxData &dieMux() const { return *m_dieMux; }
	OpenStats &openStatsMutable() { return m_openStats; }

	const OpenContainModuleData *m_open;
	const DieMuxData *m_dieMux;
	ContainedItemsList m_contained;           // RW interface + 0x34 (insertion order)
	unsigned m_stealthUnitsContained = 0;     // RW interface + 0x48 (KindOf STEALTH_GARRISON riders)
	std::uint32_t m_playerEnteredMask = 0;    // RW interface + 0x58
	std::map<ObjectID, int> m_wanters;        // RW OpenContain + 0x50 (ZH m_objectEnterExitInfo: 0 enter, 1 exit)
	bool m_enabled = true;                    // RW interface + 0xBE (from the data's Enabled)
	bool m_dying = false;                     // RW interface + 0xB8 (onDie)
	bool m_killingRiders = true;              // RW + 0xE1 (1 at construction, RW 0x867FD8; read with KillPassengersOnDeath, RW 0x86588C; cleared by RW 0x86561F)
	std::map<ObjectID, int> m_riderBoneIndex;          // RW + 0xD0 (the rider's bone index, RW 0x7871FC)
	std::map<ObjectID, std::string> m_riderBoneName;   // RW + 0x3C (RW 0x8689EC)
	std::array<std::uint32_t, 19> m_watchedConditions{}; // RW + 0x84 (19 words, zero at construction: RW 0x867FF3 memset)
	unsigned m_doorOpenFrames = 0;            // RW module + 0x70 (DoorOpenTime countdown, RW 0x8682F8 / 0x86702D)
	unsigned long long m_shipExitsUnported = 0;
	unsigned long long m_turretBonesUnported = 0;
	bool m_deleting = false;                  // RW + 0xDF (onDelete)
	int m_nextExitPath = 1;                   // RW exit interface + 0x3C (the ExitStart / ExitEnd bone number of the next exit, NumberOfExitPaths > 1)
	OpenStats m_openStats;
	mutable unsigned long long m_shipEntriesUnported = 0; // a statistic, not simulation state
};
