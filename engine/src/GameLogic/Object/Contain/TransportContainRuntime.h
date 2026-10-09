// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TransportContain and HordeTransportContain at runtime (lane GARRISON-2): the contain of the units that carry others. RotWK 2.01 data (test_hud_transport_retail.cpp
// pins the census): TransportContain 23 templates (the trolls that carry a tree or a rock, the fell beasts and their riders, the Dwarven battle wagon, Gwaihir, the
// Isengard ballista, Sharku, the Watcher), HordeTransportContain 13 (the mumakil with their Haradrim archer horde, the transport and battle ships).
//
// TARGET FACTS (RotWK game.dat, caveat S-001), each at the cited address. The module (TransportContain 0x11C bytes, constructor RW 0x86B67D after OpenContain's
// RW 0x867F56; HordeTransportContain 0x128 bytes, RW 0x87A286): the contain interface at + 0x20 (TransportContain RW 0xC5A690, HordeTransportContain RW 0xC5C370), the
// main table (RW 0xC5A808 / 0xC5C4E8), the update interface at + 0x10 (RW 0xC5D7E0 / 0xC5DB88), the exit interface at + 0x30 (RW 0xC5D220 / 0xC5C33C).
//   TransportContain:
//   * isValidContainerFor RW 0x86A002 (TEMPORARILY_DEFECTED, ConditionForEntry, OpenContain's RW 0x86603B, CanGrabStructure with a CHUNK_VENDOR, the object's
//     TransportSlotCount RW 0x69029B (template + 0x601): none refuses, the capacity count + the extra slots + it <= Slots); getContainMax RW 0x869F86 (Slots);
//   * onContaining RW 0x86A943 (held, TRANSPORT_MOVING cleared while the transport moves, the extra slots, LOADED, the rider's PASSENGER / firing passenger
//     condition, the WEAPONSTATE_ONE / TWO / THREE condition of the Type*ForWeaponState masks RW 0x86A43E, the rider's bone RW 0x86B77B, ShouldThrowOutPassengers'
//     frame); onRemoving RW 0x86ABDC (no longer held, ExitBone, OrientLikeContainerOnExit, the extra slots, LOADED, PASSENGER, a dead container scatters the rider
//     RW 0x867AD4, ResetMoodCheckTimeOnExit, ExitDelay, the WEAPONSTATE conditions cleared RW 0x86A4E4);
//   * the rider on its PassengerBonePrefix bone (OpenContain RW 0x868A70 through RW 0x8671F5) with ForceOrientationContainer (main slot 0x38 RW 0x869F97) and the
//     rider's drawable hidden by the ENCLOSED status (slot 0x6C RW 0x86B77B);
//   * update RW 0x86B86D (ShouldThrowOutPassengers' throw, HealthRegen%PerSec, UpgradeCreationTrigger RW 0x86B169, the MOVING mirror onto the riders'
//     TRANSPORT_MOVING, OpenContain's update, the container's PASSENGER_TYPE_ONE / TWO weapon set flags RW 0x86A731);
//   * the payload RW 0x86A1FA with the creator RW 0x86A104 (the container's team, the create modules' onBuildComplete, added when valid);
//   * the exit interface: isExitBusy RW 0x869E73 (ExitDelay), reserveDoorForExit RW 0x869E5D (isSpecificRiderFreeToExit RW 0x86A2D0: an AIR locomotor, else a
//     passable cell under the container for the rider's locomotor), exitObjectViaDoor OpenContain's RW 0x8682D9; killRidersWhoAreNotFreeToExit RW 0x86B05F
//     (DestroyRidersWhoAreNotFreeToExit: destroyed, else killed); the die interface OpenContain's RW 0x867120 (DamagePercentToUnits RW 0x867242 / 0x866B69).
//   HordeTransportContain:
//   * isValidContainerFor RW 0x87A48C (OpenContain's test, a HORDE_MEMBER whose horde is inside RW 0x990CB9, the capacity Slots >= count + extra + slots);
//   * addToContain RW 0x87A677 (a horde: its garrisoned byte, OpenContain's add, its world exit RW 0x990E5F, the CONTAINED weapon set; a member: RW 0x990DEA, the
//     CONTAINED weapon set and WEAPONSTATE_CONTAINED, TransportContain::onContaining; then the redeploy), removeFromContain RW 0x87A6D3 (RW 0x87A361 / 0x87A606),
//     the redeploy RW 0x87A7B4 (a horde stands at the transport's position, its members on the bones), slot 0x6C RW 0x87A3C2 (RW 0x990C6E: a hidden member leaves
//     the world, a shown one enters it), orderAllPassengersToExit RW 0x87A559 -> 0x991027, the exit RW 0x87A89E (the members first, then the horde);
//   * the die interface RW 0x87AE0A: EjectPassengersOnDeath throws every member off its bone (GameLogicRandomValue(5, 10) up and along its facing) and kills it
//     (RW 0x87AA2C); else KillPassengersOnDeath kills the riders (RW 0x990F0A); no DieMux test;
//   * the update RW 0x87AD94: once slot 0xF8 armed it (RW 0x87A321), the riders fall off one at a time (GameLogicRandomValue(3, 5), then (0, 4) frames apart:
//     RW 0x87AC9A / 0x87A72F / main slot 0x3C RW 0x87AE43).
// INFERENCE / NOT PORTED (stop S-1104, reported by stopLines()): WHEN the payload is made (RW's caller of main slot 0x70 is not located: here at onObjectCreated, as
// lane LOGIC-1's HordeContain does, S-149); the client parts (the fades, the drawable bone attachment, the sounds); GrabWeapon and CanGrabStructure (the trolls);
// ShouldThrowOutPassengers' throw (RW 0x86A530, the physics shock RW 0x792A69); GoAggressiveOnExit (the AI attitude, RW 0x66E12A, is not in the port); the AI
// slot 0x1A8 of isSpecificRiderFreeToExit; the exits and entries of a SHIP in water (the pathfinder's shore spot RW 0x6EFBB8: a SHIP on dry ground takes the ground branches); an indestructible container's payload (body slots 0x8C / 0x88); PassengersInTurret's turret bone
// (RW 0x68E807); the payload creator's CREATE_DRAWABLE_WITH_LOW_DETAIL status (client); who arms the falling riders (slot 0xF8: no caller located).

#pragma once

#include "GameLogic/Object/Contain/OpenContainRuntime.h"

#include <set>
#include <string>
#include <vector>

class ModuleFactory;
class ThingTemplate;
struct TransportContainModuleData;

class TransportContain : public OpenContain
{
public:
	TransportContain(Thing *thing, const ModuleData *data, const TransportContainModuleData &transport, const DieMuxData &dieMux);
	~TransportContain() override;

	// binds the typed data and the runtime of TransportContain, HordeTransportContain, SiegeEngineContain and HordeSiegeEngineContain
	static void registerClasses(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

	// ---- BehaviorModule ----
	void onObjectCreated() override; // the payload (main slot 0x70, INFERENCE S-1103: RW's caller is not located)
	void crc(StateHasher &hasher) const override;
	// ---- UpdateModule ----
	UpdateSleepTime update() override; // RW 0x86B86D
	// ---- ContainModuleInterface ----
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override; // RW 0x86A002
	int getContainMax() const override;                                                           // RW 0x869F86
	bool isDisplayedOnControlBar() const override { return true; }                                // slot 0xC8 RW 0x8BD372 (lane UI-1)
	bool isExitBusy() const override;                                                             // RW 0x869E73
	int reserveDoorForExit(const Object &obj) override;                                           // RW 0x869E5D

	// main slot 0x6C RW 0x86A2D0
	virtual bool isSpecificRiderFreeToExit(const Object &rider) const;
	// main slot 0x70 RW 0x86A1FA: every InitialPayload entry through the creator RW 0x86A104
	virtual void createPayload();
	// main slot 0x64 RW 0x86B05F
	void killRidersWhoAreNotFreeToExit() override;
	// RW 0x69029B: the object's TransportSlotCount (template + 0x601)
	static int transportSlotCount(const Object &obj);

	const TransportContainModuleData &transportData() const { return *m_transport; }
	int extraSlotsInUse() const { return m_extraSlotsInUse; }
	UnsignedInt frameExitNotBusy() const { return m_frameExitNotBusy; }
	bool payloadCreated() const { return m_payloadCreated; }

	struct Stats
	{
		unsigned long long payloadObjects = 0;     // objects the payload creator made
		unsigned long long payloadRefused = 0;     // made but refused by isValidContainerFor (left where they were made, as RW does)
		unsigned long long upgradeObjects = 0;     // UpgradeCreationTrigger objects added
		unsigned long long healed = 0;             // HealthRegen%PerSec heals
		unsigned long long scattered = 0;          // RW 0x867AD4
		unsigned long long ridersKilledNotFree = 0;
		unsigned long long unported = 0;           // GrabWeapon / throw out / GoAggressiveOnExit / the indestructible payload (S-1103)
	};
	const Stats &stats() const { return m_stats; }

protected:
	void onContaining(Object *obj, bool wasSelected) override; // RW 0x86A943
	void onRemoving(Object *obj) override;                     // RW 0x86ABDC
	bool forceOrientationContainer() const override;           // RW 0x869F97
	void onRiderPlaced(Object *rider, bool hidden) override;    // RW 0x86B77B
	// the creator RW 0x86A104: one object of `t` for the container's team, its create modules' onBuildComplete, added through `contain` when valid
	Object *createPayloadObject(const ThingTemplate &t, ContainModuleInterface &contain, bool checkCapacity);
	// RW 0x86A43E (contain slot 0xEC): the container's WEAPONSTATE_ONE / TWO / THREE of the rider's KindOf; RW 0x86A4E4 (slot 0xF0) clears them
	void setWeaponStateOfRider(const Object &rider);
	void clearWeaponStates();
	// RW 0x86A731 (main slot 0x78): the container's PASSENGER_TYPE_ONE / TWO weapon set flags from its riders' KindOf
	void letRidersUpgradeWeaponSet();
	// RW 0x86B169: the UpgradeCreationTrigger list
	void processUpgradeCreationTriggers();
	// RW 0x867AD4 OpenContain::scatterToNearbyPosition
	void scatterToNearbyPosition(Object *rider);

	const TransportContainModuleData *m_transport;
	int m_extraSlotsInUse = 0;                 // RW + 0x100
	UnsignedInt m_frameExitNotBusy = 0;        // RW + 0x104
	UnsignedInt m_throwOutFrame = 0;           // RW + 0x108
	bool m_moving = false;                     // RW + 0x10C (the container's MOVING, mirrored onto the riders' TRANSPORT_MOVING)
	bool m_payloadCreated = false;
	std::vector<std::string> m_pendingUpgradeTriggers; // RW + 0x110 (the UpgradeCreationTrigger upgrade names, RW 0x86B6DC .. 0x86B728)
	Stats m_stats;
};

// RotWK's HordeTransportContain (RW 0x87A286): a horde rides (the horde object out of the world, its members on the bones)
class HordeTransportContain : public TransportContain
{
public:
	HordeTransportContain(Thing *thing, const ModuleData *data, const TransportContainModuleData &transport, const DieMuxData &dieMux);

	UpdateSleepTime update() override; // RW 0x87AD94
	void crc(StateHasher &hasher) const override;
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override; // RW 0x87A48C
	bool addToContain(Object *obj) override;                                                      // RW 0x87A677
	void removeFromContain(Object *obj) override;                                                 // RW 0x87A6D3
	void orderAllPassengersToExit(int source) override;                                           // RW 0x87A559 -> 0x991027
	void exitObjectViaDoor(Object *obj, int door) override;                                       // RW 0x87A89E
	void onDie(const DieModuleInterface::Event &event) override;                                  // RW 0x87AE0A
	// slot 0xF8 RW 0x87A321: the riders start to fall off (the update's timers)
	void armRidersFalling() { m_ridersFalling = true; }
	bool ridersFalling() const { return m_ridersFalling; }

protected:
	void redeployOccupants() override;                      // RW 0x87A7B4
	void onRiderPlaced(Object *rider, bool hidden) override; // RW 0x87A3C2
	// RW 0x87A51F: a horde rider stands at the transport's position
	void putHordeAtContainer(Object *horde);
	// RW 0x87AA2C / main slot 0x3C RW 0x87AE43: a member thrown off its bone and killed; `boneIndex` < 0: the member's recorded bone
	// returns the bone index it used (the given one, reset to 0 when the bone count is not above it: RW 0x87AB4F keeps cycling from that value)
	int throwMemberOff(Object *member, HordeContainInterface *horde, int boneIndex, int lo, int hi, int line);
	bool memberInsideHorde(const Object &obj, Object **horde) const; // RW 0x990CB9

	int m_fallTimer = -1000;                      // RW + 0x110 (-1000: not drawn yet)
	bool m_ridersFalling = false;                 // RW + 0x114
};
