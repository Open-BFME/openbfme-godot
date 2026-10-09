// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeAIUpdate.h.

#include "GameLogic/Module/HordeAIUpdate.h"

#include "Common/GameCommon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

void HordeAIUpdate::updateBeforeBase()
{
	Object *horde = getObject();
	ContainModuleInterface *contain = horde->getContain();
	HordeContainInterface *hc = contain ? contain->getHordeContainInterface() : nullptr;
	if (!hc)
	{
		return;
	}
	// B1: `if (!isMoving() && refreshFrame < frame - 5) updateFormation(1)`
	if (!isMoving())
	{
		const unsigned frame = horde->logic().getFrame();
		const unsigned last = hc->formationRefreshFrame();
		if (frame > (unsigned)LOGICFRAMES_PER_SECOND && last < frame - (unsigned)LOGICFRAMES_PER_SECOND)
		{
			hc->updateFormation();
		}
	}
}

// ---- lane MOVE-2 r2: the command hand-off (RW 0x89E169, read with Ghidra) ----
bool HordeAIUpdate::commandEndsMemberOrders(int command)
{
	switch (command)
	{
	case 0x00: case 0x01: case 0x03: case 0x04: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x0C: case 0x0E: case 0x0F: case 0x17: case 0x18:
	case 0x24: case 0x32: case 0x33: case 0x34: case 0x36: case 0x38: case 0x41: case 0x42: case 0x4E:
	case AIUpdateInterface::kCommandUnidentifiedMove:
		return true;
	default:
		return false;
	}
}

void HordeAIUpdate::commandAccepted(CommandSourceType source, int command, Object *target)
{
	(void)source;
	// RW 0x89E185 .. 0x89E205: the gate (vslot 0x250) has passed (acceptCommand). The porcupine / stance branch before the switch (contain vslots 0xF0 / 0x5C / 0x60,
	// RW 0x89DE81 / 0x89DF11) is not read here (S-1501). Then for the hand-off commands: unless the horde stands in a container whose contain answers vslot 0x10
	// (RW 0x89E19E; INFERENCE S-1501: taken as "the horde is contained"), IS_LEAVING_FACTORY (status 0x5A) is cleared on the horde and the contain's slot 0x14 runs
	// with the command's object (commands 0xB / 0xC: cmd + 0x14) or null
	if (!commandEndsMemberOrders(command))
	{
		return;
	}
	Object *horde = getObject();
	if (horde->getContainedBy())
	{
		return;
	}
	ContainModuleInterface *contain = horde->getContain();
	HordeContainInterface *hc = contain ? contain->getHordeContainInterface() : nullptr;
	if (!hc)
	{
		return;
	}
	static const int leavingFactory = ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY");
	if (leavingFactory >= 0 && horde->testStatus((unsigned)leavingFactory))
	{
		horde->setStatus((unsigned)leavingFactory, false); // RW 0x62684D(0x5A, 0)
	}
	endMelee(); // slot 0x14 starts with slot 0x138 (RW 0x86C0F9) when the horde melees: the port keeps the melee runtime in this AI
	hc->prepareMembersForCommand(command == 0x0B || command == 0x0C ? target : nullptr);
}
