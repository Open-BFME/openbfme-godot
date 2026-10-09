// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SiegeEngineContain and HordeSiegeEngineContain at runtime (lane GARRISON-2): the siege engines with a crew. RotWK 2.01 data: SiegeEngineContain 4 templates
// (IsengardBatteringRam, MordorBatteringRam, IsengardSiegeLadder, MordorGrond), HordeSiegeEngineContain 1 (MordorSiegeTower). The crew (CrewFilter, CrewMax,
// InitialCrew) is a second list beside the riders; the riders are TransportContain's (a horde for the siege tower).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): SiegeEngineContain (0x138 bytes, constructor RW 0x87F4E0 after TransportContain's RW 0x86B67D: the crew list at + 0x11C,
// its count + 0x120, the MINE passenger byte + 0x124, the exit block byte + 0x124 of the exit interface's view); HordeSiegeEngineContain (0x144 bytes, RW 0x8804D7
// after HordeTransportContain's RW 0x87A286; the crew list + 0x128, count + 0x12C). Contain interface RW 0xC5D668 / 0xC5DA10, main RW 0xC5D7F0 / 0xC5DB98:
//   * isValidContainerFor RW 0x87ECC7 / 0x87FD0F: a CrewFilter object of the container's player (RW 0x7640C1) with CrewMax >= 1 is crew while the crew is below
//     CrewMax (or without the capacity test); anything else is TransportContain's RW 0x86A002 (the horde siege's: HordeTransportContain's RW 0x87A48C for a
//     non-crew object);
//   * addToContainList RW 0x87F341 / 0x880377 (crew: the list, the count, the CONTAINED weapon set and WEAPONSTATE_CONTAINED, the siege engine's crew walks with
//     LOCOMOTORSET_CONTAINED (AI slot 0x238 RW 0x6680B2, 10); a rider: SIEGE_CONTAIN, OpenContain's list, a MINE marks the byte), removeFromContainList (main slot
//     0x34 RW 0x87EFF8 / 0x87FE19: the reverse), isContainedHere (slot 0xE8 RW 0x87FEDA), the crew's ObjectStatusOfCrew (slot 0xB0 RW 0x87ED55);
//   * the crew on their bones first (RW 0x87F5B4), the crew's speed (slot 0xD4 RW 0x87ED45: count x SpeedPercentPerCrew, read by RW 0x68BF11), removeAllContained
//     (RW 0x87F099 / 0x87FEA4: the crew, then OpenContain's), onRemoving (RW 0x87FDAD: SIEGE_CONTAIN off, TransportContain's), onDelete (RW 0x87EE17 / 0x87FDDD:
//     OpenContain's, then every crew member destroyed), the exit block (main slot 0x6C RW 0x87EBF1);
//   * the payload RW 0x87EE53 / 0x87FFC7: InitialCrew objects for the owner's default team (added when valid; JUST_BUILT for BuildFadeInOnCreateTime x 5 frames;
//     named "<container name><i>" when the container has a name), then TransportContain's payload;
//   * the exit RW 0x87F7B4 / 0x880B9C -> 0x8807CA: a crew member steps 50 away from the engine (the exit path, CMD_FROM_AI); a rider of a deployed engine
//     (SiegeDeploySpecialPower) goes onto the wall: the ladder's bone "Ladder04" or the tower's front (the geometry's major radius, 20 further, the walk 40 on, the
//     wall layer 0x11); otherwise OpenContain's (HordeTransportContain's) exit.
// NOT PORTED (stop S-1105): SiegeDeploySpecialPower / SiegeAIUpdate / DynamicPortalBehaviour (the docking: the deployed branch of the exit is not reached), the wall
// layer (S-164), the selection transfer (TransferSelection, client side), the crew's team change on capture (contain slots 0x108 / 0x10C / 0x110).

#pragma once

#include "GameLogic/Module/TransportContainBehavior.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"

class ModuleFactory;

template <class Base, bool Horde>
class SiegeEngineContainT : public Base
{
public:
	SiegeEngineContainT(Thing *thing, const ModuleData *data, const SiegeEngineContainModuleData &siege, const DieMuxData &dieMux);

	void crc(StateHasher &hasher) const override;
	void onDelete() override;
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override;
	void exitObjectViaDoor(Object *obj, int door) override;
	float getCrewPowerMultiplier() const override;
	void removeAllContained() override;
	const ContainModuleInterface::ContainedItemsList *crewList() const override { return &m_crew; }
	bool isSpecificRiderFreeToExit(const Object &rider) const override;
	void createPayload() override;

	const SiegeEngineContainModuleData &siegeData() const { return *m_siege; }
	int crewCount() const { return m_crewCount; }
	bool hasMinePassenger() const { return m_hasMinePassenger; }
	// RW 0x7640C1(obj, the container's player) with CrewMax >= 1: the object would be crew
	bool isCrewCandidate(const Object &obj) const;

protected:
	void addToContainList(Object *obj) override;
	void removeFromContainList(Object *obj) override;
	bool isContainedHere(const Object &obj) const override;
	const ObjectStatusMaskType &statusMaskFor(const Object *obj) const override;
	void redeployOccupants() override;
	void onRemoving(Object *obj) override;
	// RW 0x87F7B4's / 0x8807CA's body for one object
	void exitOne(Object *obj, int door);

	const SiegeEngineContainModuleData *m_siege;
	ContainModuleInterface::ContainedItemsList m_crew; // RW + 0x11C (+ 0x128)
	int m_crewCount = 0;                               // RW + 0x120 (+ 0x12C)
	bool m_hasMinePassenger = false;                   // RW + 0x124 (+ 0x130)
	bool m_exitBlocked = false;                        // RW 0x87EBF1's byte (never set by the ported parts: SiegeDeploySpecialPower's)
};

using SiegeEngineContain = SiegeEngineContainT<TransportContain, false>;
using HordeSiegeEngineContain = SiegeEngineContainT<HordeTransportContain, true>;

namespace SiegeEngineContainRegistry
{
void registerClasses(ModuleFactory &modules);
std::vector<std::string> stopLines();
} // namespace SiegeEngineContainRegistry
