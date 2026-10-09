// OpenBFME. GPL-3.0.
// See GameLogic/Module/UpdateModule.h.

#include "GameLogic/Module/UpdateModule.h"

#include "Common/StateHash.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

void UpdateModule::setWakeFrame(Object *obj, UpdateSleepTime wakeDelay)
{
	GameLogic &logic = obj->logic();
	const UnsignedInt now = logic.getFrame();
	// ZH UpdateModule.cpp:85-89: GameLogic::friend_awakenUpdateModule(obj, this, now + delay); FOREVER overflows the clamp harmlessly
	logic.friend_awakenUpdateModule(obj, this, now + (UnsignedInt)wakeDelay);
}

void UpdateModule::crc(StateHasher &hasher) const
{
	hasher.addU32(m_nextCallFrame);
	hasher.addI32(m_indexInLogic);
	hasher.addI32(m_phaseInLogic);
}
