// OpenBFME. GPL-3.0.
//
// StrafeAreaUpdate: aircraft strafes a ground area in sinusoidal sweeps and fires a weapon (lane MODGAP-2, stop S-6026).
//
// Both editions:
//   RotWK 2.01:
//     - friend_newModuleInstance: RW 0x0064D3D8
//     - friend_newModuleData:     RW 0x0064D410
//     - buildFieldParse:          RW 0x008A38E0, table RW 0x00BF7394
//     - ModuleData ctor:          RW 0x008A38F0, vtable RW 0x00C31958
//     - StrafeAreaUpdate ctor:    RW 0x008A3950, vtable RW 0x00C319D8
//     - ~StrafeAreaUpdate:        RW 0x008A39D0
//     - initiateStrafe:           RW 0x008A3A20
//     - update:                   RW 0x008A3AC0
//     - xfer:                     RW 0x007A8160
//   BFME2 1.06:
//     - friend_newModuleInstance: BFME2 0x0024D6FF
//     - friend_newModuleData:     BFME2 0x0024D737
//     - buildFieldParse:          BFME2 0x003A4D12, table BFME2 0x00C1B1F8
//     - ModuleData ctor:          BFME2 0x003A4E5A, vtable BFME2 0x00C1B268
//     - StrafeAreaUpdate ctor:    BFME2 0x0089F9F0 (decomp 0x003A4D23), vtable BFME2 0x00C1B164
//     - initiateStrafe:           BFME2 0x003A4F21
//     - update:                   BFME2 0x003A4FDA
//     - xfer:                     BFME2 0x003A4DC0

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/StateHash.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/SnapshotXfer.h"

#include <string>

class ModuleFactory;

class StrafeAreaUpdateModuleData : public ModuleData
{
public:
	StrafeAreaUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_weaponName;              ///< +0x08 WeaponName (default "")
	float m_strafeAreaRadius = 150.0f;     ///< +0x0C StrafeAreaRadius (default 150.0f)
	float m_sweepFrequency = 0.4f;         ///< +0x10 SweepFrequency (default 0.4f)
	float m_sweepAmplitude = 100.0f;       ///< +0x14 SweepAmplitude (default 100.0f)
	float m_slope = 50.0f;                 ///< +0x18 Slope (default 50.0f)
	float m_initialSweepPhase = 0.0f;      ///< +0x1C InitialSweepPhase (default 0.0f)
};

namespace StrafeAreaUpdateTables
{
const FieldParse *strafe(); ///< RW 0x00BF7394 / BFME2 0x00C1B1F8
}

// RW 0x007A8055 (getModuleName), RW 0x007A805B (getModuleNameKey), RW 0x007A8132 (scalar deleting dtor -> 0x007A803C).
// RW 0x007A803C (dtor -> RW 0x0064A492)
class StrafeAreaUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(StrafeAreaUpdate)

	StrafeAreaUpdate(Thing *thing, const ModuleData *moduleData);
	~StrafeAreaUpdate() override = default;

	bool initiateStrafe(const Coord3D *from, const Coord3D *to);
	UpdateSleepTime update() override;

	void crc(StateHasher &hasher) const override;

	const StrafeAreaUpdateModuleData *getData() const { return m_data; }
	int state() const { return m_state; }
	bool started() const { return m_started; }
	float sweepPhase() const { return m_sweepPhase; }
	const Coord3D &strafeTarget() const { return m_strafeTarget; }
	const Coord3D &strafeStart() const { return m_strafeStart; }
	unsigned nextFireFrame() const { return m_nextFireFrame; }

	static const char *stopLine();
	static void registerClass(ModuleFactory &modules);

private:
	const StrafeAreaUpdateModuleData *m_data = nullptr;

	Coord3D m_strafeTarget{};              ///< +0x20
	Coord3D m_strafeStart{};               ///< +0x2C
	float m_sweepPhase = 0.0f;             ///< +0x38
	int m_state = 0;                       ///< +0x3C (0: approach, 1: strafe/fire, 2: exit/kill)
	bool m_started = false;                ///< +0x40
	unsigned m_nextFireFrame = 0;          ///< +0x44
};
