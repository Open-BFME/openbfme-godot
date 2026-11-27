// OpenBFME. GPL-3.0.
//
// OneRingPenaltyUpdate (lane MODGAP-1, MODULE_GAPS.md group 9; retail 2.01 has it only commented out, Ennorath's Frodo uses it): wearing the One Ring too long calls the Eye of
// Sauron, which roams around the wearer; when it finishes the ring comes off and cannot be used for a while. Ported from the RotWK binary (caveat S-001); the BFME2 decompile
// Object/Update/OneRingPenaltyUpdate.cpp and OneRingPenaltyModuleDataView.h name the fields.
//
// TARGET FACTS (RotWK game.dat, 2026-11-08 correction):
//   create RW 0x64E0DC, 0x34 bytes, constructor RW 0x89D21F (zeroes, wake next frame RW 0x850C32(1)); table RW 0xC66B18 (16-byte rows): SpecialObjectName + 0x08,
//   RingTimeBeforeSpawning + 0x0C, TimeSpentRoamingAround + 0x10, TimeRingPowerSuppressed + 0x14, StartingDistanceFromMe + 0x18, TimeFrozenFromPenalty + 0x1C (row RW 0xC66B68)
//   (parser RW 0x73A429 duration), DiscoveredSound + 0x20 (RW 0x73B217); defaults RW 0x89D1C1 zeroes + 0x1C. Module: + 0x24 eye id, + 0x28 ring frame, + 0x2C angle, + 0x30 penalty frame. Mask 3: update and die.
//   * update RW 0x89D647 (answers 5 frames, RW 0xD9F608): worn = (invisible RW 0x68FC2F or STEALTHED) and StealthUpdate ring on (+0x31). Penalty older than TimeRingPowerSuppressed
//     (frame > penalty + time) ends RW 0x89D3E4 (+0x30=0, CAN_STEALTH on). Else no ring frame: worn keeps frame. Else not worn: frame clears and eye dies. Else with eye moves (RW 0x89D4D2),
//     without eye a frame past ring+RingTimeBeforeSpawning spawns it (RW 0x89D2AC);
//   * spawn RW 0x89D2AC: template by name (none: nothing), made on neutral default team, empty mask; angle draws GameLogicRandomValueReal(-PI,PI) (line 0xDA); pos = obj + (cos,sin)*StartingDistance;
//   * move RW 0x89D4D2: start=ring+RingTimeBeforeSpawning, end=start+TimeSpentRoamingAround: frame>=end puts eye on object and pays penalty. Else idle eye adds (-PI/2,PI/2) draw (line 0xB8) and walks to obj+(cos,sin)*radius;
//   * penalty RW 0x89D41B: ring frame clears, +0x30=frame, worn ring toggles off (RW 0x776B50), CAN_STEALTH off (RW 0x62684D(0x12,0)), emotion request (5,none,1) RW 0x68F37F, DiscoveredSound, then eye killed RW 0x89D3F5;
//   * die RW 0x89D4C7 kills eye.
//   TimeFrozenFromPenalty (+0x1C) is read by none of these functions: ctor RW 0x89D21F, vtables RW 0xC669C8/C669B8, update RW 0x89D647 and helpers RW 0x89D0E3..0x89D712 read +0xC,+0x10,+0x14,+0x18 and never +0x1C (checked 2026-11-08). RW 0x8CA12A in RW 0x8CA072 is not this field: that function belongs to a deploy module (ctor RW 0x8C9B5C, vtable RW 0xC74910, table RW 0xC747E8 LowerDelay +0x18 / RaiseDelay +0x1C) reading RaiseDelay. No other code looks up OneRingPenaltyUpdate module data by name or by interface to read +0x1C (searched .text refs to its table/maker and to +0x1C loads via module lookup; none). Verdict: RotWK does not read TimeFrozenFromPenalty. Stop S-5604 updated. Snapshot OpenBFME native (S-4190).
// Local 171: RW 0x64E114 (OneRingPenaltyUpdate newData)

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;
class StateHasher;

class OneRingPenaltyUpdateModuleData : public ModuleData
{
public:
	std::string m_specialObjectName;     ///< + 0x08
	unsigned m_ringTimeBeforeSpawning = 0; ///< + 0x0C (frames)
	unsigned m_timeSpentRoamingAround = 0; ///< + 0x10
	unsigned m_timeRingPowerSuppressed = 0; ///< + 0x14
	float m_startingDistanceFromMe = 0.0f; ///< + 0x18
	unsigned m_timeFrozenFromPenalty = 0;  ///< + 0x1C (read by nothing in RW's module)
	std::string m_discoveredSound;       ///< + 0x20
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x0089D103 (getModuleName), RW 0x0089D15B (getModuleNameKey), RW 0x0089D290 (scalar deleting dtor -> RW 0x0089D0E3)
class OneRingPenaltyUpdate : public UpdateModule, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(OneRingPenaltyUpdate)
	OneRingPenaltyUpdate(Thing *thing, const OneRingPenaltyUpdateModuleData *data); // RW 0x89D21F
	DieModuleInterface *getDie() override { return this; }
	UpdateSleepTime update() override;                                  // RW 0x89D647
	void onDie(const DieModuleInterface::Event &event) override;        // RW 0x89D4C7
	void crc(StateHasher &hasher) const override;

	ObjectID eye() const { return m_eye; }
	UnsignedInt ringFrame() const { return m_ringFrame; }
	UnsignedInt penaltyFrame() const { return m_penaltyFrame; }
	static const char *stopLine();
	static void registerClass(ModuleFactory &modules);

private:
	void endPenalty();   // RW 0x89D3E4
	void killEye();      // RW 0x89D3F5
	void payPenalty();   // RW 0x89D41B
	void spawnEye();     // RW 0x89D2AC
	void moveEye();      // RW 0x89D4D2

	const OneRingPenaltyUpdateModuleData *m_data;
	ObjectID m_eye = INVALID_ID;     ///< + 0x24
	UnsignedInt m_ringFrame = 0;     ///< + 0x28
	float m_angle = 0.0f;            ///< + 0x2C
	UnsignedInt m_penaltyFrame = 0;  ///< + 0x30
};
