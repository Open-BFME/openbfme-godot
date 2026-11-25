// OpenBFME. GPL-3.0.
//
// DynamicShroudClearingRangeUpdate (fork issue 285, LOCAL-35; both editions: BFME2 1.06 ctor 0x0048B067, xfer 0x0048B505,
// animateGridDecals 0x0048B256).
// The growing / shrinking shroud-clearing reveal of a map ping (MapRevealPing in system.ini): the update drives the
// object's shroud clearing range through Object::setShroudClearingRange over the frames the module data gives, and the
// grid decal visual (client RadiusDecal) that retail also animates is not drawn here (stop S-7100).
//
// TARGET FACTS (RotWK game.dat, read through tools/mod/rw.py dis):
// - name() RW 0x88E7D5 ("DynamicShroudClearingRangeUpdate" @ 0xC0B7CC), getModuleNameKey() RW 0x88E7DB.
// - ModuleData buildFieldParse RW 0x88E6C7 (table RW 0xC62648): ShrinkDelay +0x08, ShrinkTime +0x0C, GrowDelay +0x10,
//   GrowTime +0x14 (parseDurationUnsignedInt RW 0x73A429), FinalVision +0x18 (parseReal RW 0x42ED00), ChangeInterval
//   +0x1C, GrowInterval +0x20 (parseDurationUnsignedInt), GridDecalTemplate +0x28 (parseRadiusDecalTemplate RW 0x7327BC).
// - ctor RW 0x88E6D8: m_changeIntervalCountdown=0, m_visionChangePerInterval=0, m_stateCountDown=ShrinkDelay+ShrinkTime,
//   m_totalFrames=max(1,stateCountDown), m_shrinkStartDeadline=stateCountDown-ShrinkDelay,
//   m_growStartDeadline=stateCountDown-GrowDelay, m_sustainDeadline=growStartDeadline-GrowTime,
//   m_doneForeverFrame=TheGameLogic->getFrame()+stateCountDown, m_decalsCreated=false, m_nativeClearingRange=200.0f,
//   m_currentClearingRange=0.0f, m_state=NOT_STARTED_YET; if the object has a controlling player,
//   m_nativeClearingRange = Object::getShroudClearingRange (RW 0x68E4E2).
// - update() RW 0x88E9B0: state machine below; m_state is recomputed every frame from the deadlines, so it is not saved.
// - xfer RW 0x88EB76: UpdateModule::xfer, light-CRC early-out, Version1, then the 11 fields (not m_state).
// - killGridDecals RW 0x88E6AF, createGridDecals RW 0x88E87F, animateGridDecals RW 0x88E8C7: the client decal visual.
//
// DONOR BFME2 1.06 (workspace/reference/open-bfme-2, GameLogic/Object/Update/DynamicShroudClearingRangeUpdate*.cpp):
// the same class, ctor 0x0048B067, update 0x0048B33F, xfer 0x0048B505, ModuleData ctor 0x0048B5C6. The behaviour matches RW.
// Local 171: RW 0x64CAE2 (DynamicShroudClearingRangeUpdate newInstance), RW 0x64CB1D (DynamicShroudClearingRangeUpdate newData)

#pragma once

#include "GameClient/RadiusCursor.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;

// Local 173: RW 0x88EC37 (DynamicShroudClearingRangeUpdateModuleData ctor)
class DynamicShroudClearingRangeUpdateModuleData : public ModuleData
{
public:
	std::uint32_t m_shrinkDelay = 0;   ///< +0x08 ShrinkDelay (frames)
	std::uint32_t m_shrinkTime = 0;    ///< +0x0C ShrinkTime (frames)
	std::uint32_t m_growDelay = 0;     ///< +0x10 GrowDelay (frames)
	std::uint32_t m_growTime = 0;      ///< +0x14 GrowTime (frames)
	float m_finalVision = 0.0f;        ///< +0x18 FinalVision
	std::uint32_t m_changeInterval = 0; ///< +0x1C ChangeInterval (frames)
	std::uint32_t m_growInterval = 0;  ///< +0x20 GrowInterval (frames)
	RadiusDecalTemplate m_gridDecalTemplate; ///< +0x28 GridDecalTemplate (the client grid decal; not drawn, S-7100)

	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x0088EC1B (scalar deleting dtor -> RW 0x0088E820)
class DynamicShroudClearingRangeUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DynamicShroudClearingRangeUpdate)

	DynamicShroudClearingRangeUpdate(Thing *thing, const DynamicShroudClearingRangeUpdateModuleData *data);

	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	static void registerClass(ModuleFactory &modules);

private:
	enum State
	{
		NOT_STARTED_YET = 0,
		GROWING = 1,
		SUSTAINING = 2,
		SHRINKING = 3,
		DONE_FOREVER = 4,
		SLEEPING = 5
	};

	// The client grid decal visual (RW killGridDecals 0x88E6AF / createGridDecals 0x88E87F / animateGridDecals 0x88E8C7)
	// is not drawn in this build (stop S-7100); the state flag is kept so the logic and the save format match retail.
	void killGridDecals() {}
	void createGridDecals(const RadiusDecalTemplate &, float, const Coord3D &) {}
	void animateGridDecals() {}

	const DynamicShroudClearingRangeUpdateModuleData *m_data = nullptr;
	State m_state = NOT_STARTED_YET;        ///< +0x20 (recomputed each update; not saved)
	std::int32_t m_stateCountDown = 0;      ///< +0x24
	std::int32_t m_totalFrames = 0;         ///< +0x28
	std::uint32_t m_growStartDeadline = 0;  ///< +0x2C
	std::uint32_t m_sustainDeadline = 0;    ///< +0x30
	std::uint32_t m_shrinkStartDeadline = 0;///< +0x34
	std::uint32_t m_doneForeverFrame = 0;   ///< +0x38
	std::uint32_t m_changeIntervalCountdown = 0; ///< +0x3C
	bool m_decalsCreated = false;           ///< +0x40
	float m_visionChangePerInterval = 0.0f; ///< +0x44 (set 0, saved; no reader in this binary)
	float m_nativeClearingRange = 200.0f;   ///< +0x48
	float m_currentClearingRange = 0.0f;    ///< +0x4C
};
