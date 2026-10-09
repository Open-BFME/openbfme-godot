// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// UpdateModule, BFME layout (spec ini-and-object-model.md 5.2; B1 Source/GameLogic/System/GameLogicAwakenUpdate.cpp:180-200).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): an update module keeps its next call frame (a plain frame, not ZH's frame << 2 | phase;
// +0x14), its index in the scheduler vector (+0x18) and the phase of that vector (+0x1C, -1 = it sits in the sleeping vector). The
// layout is read from RW 0x6250BE (setNextCallFrame: clamp to 0x3FFFFFFF), 0x6250D2 (getIndexInLogic), 0x6250E1 (setIndexInLogic(
// index, phase)). The scheduler is GameLogic's (GameLogic/GameLogic.h): friend_awakenUpdateModule (RW 0x62B921), registerObject, the
// phase loops (RW 0x62E4E8) and processDestroyList (RW 0x62A2C9).
// DONOR: ZH Include/GameLogic/Module/UpdateModule.h, Source/GameLogic/Object/Update/UpdateModule.cpp:85-89 (setWakeFrame).

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/ObjectTypes.h"

class GameLogic;

class UpdateModuleInterface
{
public:
	virtual ~UpdateModuleInterface() = default;
	virtual UpdateSleepTime update() = 0;
	// RW: vslot 1 of the interface, called with the object's disabled mask (spec 5.4 step: `disabled.any() ? disabled & mask`)
	virtual DisabledMaskType getDisabledTypesToProcess() const { return DISABLEDMASK_NONE; }
};

class UpdateModule : public BehaviorModule, public UpdateModuleInterface
{
public:
	UpdateModule(Thing *thing, const ModuleData *moduleData)
		: BehaviorModule(thing, moduleData)
	{
	}

	UpdateModuleInterface *getUpdate() override { return this; }
	UpdateModule *asUpdateModule() override { return this; }

	// ZH UpdateModule::getUpdatePhase: "you should really never specify anything other than PHASE_NORMAL"
	virtual SleepyUpdatePhase getUpdatePhase() const { return PHASE_NORMAL; }

	// ---- the scheduler's fields (friend_ functions: for GameLogic only) --------------------------------------------------
	UnsignedInt friend_getNextCallFrame() const { return m_nextCallFrame; }
	void friend_setNextCallFrame(UnsignedInt frame)
	{
		if (frame > (UnsignedInt)UPDATE_SLEEP_FOREVER)
		{
			frame = (UnsignedInt)UPDATE_SLEEP_FOREVER;
		}
		m_nextCallFrame = frame;
	}
	int friend_getIndexInLogic() const { return m_indexInLogic; }
	int friend_getPhaseInLogic() const { return m_phaseInLogic; }
	// RW 0x6250E1: the sleeping vector (phase -1) form and the phase form
	void friend_setIndexInLogic(int index)
	{
		m_phaseInLogic = -1;
		m_indexInLogic = index;
	}
	void friend_setIndexInLogic(int index, int phase)
	{
		m_phaseInLogic = phase;
		m_indexInLogic = index;
	}
	void friend_setPhaseInLogic(int phase) { m_phaseInLogic = phase; }

	// RW 0x850C4E (the update interface's vslot 8, called by other modules: ActivateModuleSpecialPower RW 0x8D22C4): setWakeFrame(object, delay)
	void wakeFromInterface(UpdateSleepTime delay) { setWakeFrame(getObject(), delay); }
	// the OpenBFME state hash: the scheduler state of the module (frame, index, phase) plus whatever the module adds
	void crc(StateHasher &hasher) const override;

protected:
	// ZH UpdateModule::setWakeFrame: "yes, protected: modules should only wake themselves up". now + delay goes to
	// GameLogic::friend_awakenUpdateModule (clamped there by friend_setNextCallFrame).
	void setWakeFrame(Object *obj, UpdateSleepTime wakeDelay);
	// the frame the module is due (ZH getWakeFrame returns it as a sleep time)
	UnsignedInt getWakeFrame() const { return m_nextCallFrame; }

private:
	UnsignedInt m_nextCallFrame = 0;
	int m_indexInLogic = -1;
	int m_phaseInLogic = -1;
};
