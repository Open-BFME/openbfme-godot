// OpenBFME. GPL-3.0.
// See GameLogic/AI/GarrisonCommands.h.

#include "GameLogic/AI/GarrisonCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char *const kLane = "GARRISON-1";

const GameMessageArgument *objectArg(const GameMessage &m, size_t i)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == ARGUMENTDATATYPE_OBJECTID ? a : nullptr;
}

GarrisonCommands::Stats &stats()
{
	static GarrisonCommands::Stats s;
	return s;
}

// MSG_ENTER (RW 0x77AC26): argument 1 is the container; the player's group (RW 0x76FB64 releases its weapon locks: not ported) enters it through RW 0x774EDF: when
// the container has a contain, every unit of the group gets AI command 0x42 (RW 0x774897 with the target in the parameters' slot 3 -> RW 0x770EDE). The group
// manager branch (GameData + 0x11CA, RW 0x75754C) is S-223's.
bool enter(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *target = objectArg(m, 1);
	if (!target || !logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		++stats().rejected;
		return false;
	}
	++stats().enters;
	Object *container = logic.findObjectByID(target->objectID);
	if (!container || container->isDestroyed() || !container->getContain())
	{
		return true; // RW 0x77AC40 / 0x774EEC: nothing happens
	}
	AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
	for (Object *o : group.members())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			stats().enterOrders += ai->aiEnter(container, CMD_FROM_PLAYER) ? 1u : 0u;
		}
	}
	return true;
}

// MSG_EVACUATE (RW 0x77AD4E -> RW 0x7725DF groupEvacuate): every unit of the group: one without an AI that is a STRUCTURE with a contain orders its riders out
// (contain slot 0x80); one with an AI gets AI command 0x1B (RW 0x6AF1AC -> RW 0x66E0DC: the contain's slot 0x80 too). The flying branch (KindOf bit 12 with
// status 6, RW 0x7725FD) is S-1103's.
bool evacuate(GameLogic &logic, const GameMessage &m)
{
	if (!logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		++stats().rejected;
		return false;
	}
	++stats().evacuates;
	AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
	for (Object *o : group.members())
	{
		if (ContainModuleInterface *c = o->getContain())
		{
			c->orderAllPassengersToExit((int)CMD_FROM_PLAYER);
		}
	}
	return true;
}

// MSG_EXIT (RW 0x77AC92): argument 0 the rider, argument 1 the container (it must have a contain). With a rider: it must belong to the issuing player and be in the
// container (contain slot 0xE8); a HORDE rider gets aiHordeExit, another aiExit (source 0). Without a rider: the contain's slot 0x84 (RW 0x867DD1: the first
// rider with an AI gets aiExit with source 1).
bool exitOne(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *riderArg = objectArg(m, 0);
	const GameMessageArgument *containerArg = objectArg(m, 1);
	Player *player = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!containerArg || !player)
	{
		++stats().rejected;
		return false;
	}
	++stats().exits;
	Object *container = logic.findObjectByID(containerArg->objectID);
	if (!container || !container->getContain())
	{
		return true;
	}
	ContainModuleInterface *contain = container->getContain();
	Object *rider = riderArg ? logic.findObjectByID(riderArg->objectID) : nullptr;
	if (!rider)
	{
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			if (!items->empty() && items->front()->getAIUpdateInterface())
			{
				items->front()->getAIUpdateInterface()->aiExit(container, CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (rider->getControllingPlayer() != player || rider->getContainedBy() != container || !rider->getAIUpdateInterface())
	{
		return true;
	}
	static const int horde = CombatNames::kindOf("HORDE");
	if (rider->isKindOf((unsigned)horde))
	{
		rider->getAIUpdateInterface()->aiHordeExit(container, CMD_FROM_PLAYER); // RW 0x77AD2B: RW 0x775AFB
	}
	else
	{
		rider->getAIUpdateInterface()->aiExit(container, CMD_FROM_PLAYER); // RW 0x77AD35: RW 0x7716C1
	}
	return true;
}
} // namespace

void GarrisonCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_ENTER, kLane, enter);
	d.registerHandler(MSG_EVACUATE, kLane, evacuate);
	d.registerHandler(MSG_EXIT, kLane, exitOne);
}

const GarrisonCommands::Stats &GarrisonCommands::stats()
{
	return ::stats();
}
