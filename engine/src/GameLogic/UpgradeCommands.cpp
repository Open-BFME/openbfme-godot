// OpenBFME. GPL-3.0.
// See GameLogic/UpgradeCommands.h.

#include "GameLogic/UpgradeCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"

#include <vector>

namespace
{
const char *const kLane = "UPGRADE-1";

// RW 0x6AAB85: the player's selection as a group (objects the player does not control, and destroyed ones, are not in it)
std::vector<Object *> group(GameLogic &logic, int playerIndex)
{
	std::vector<Object *> out;
	Player *player = logic.players().getNthPlayer(playerIndex);
	if (!player)
	{
		return out;
	}
	for (ObjectID id : player->selection())
	{
		Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && o->getControllingPlayer() == player)
		{
			out.push_back(o);
		}
	}
	return out;
}

const UpgradeTemplate *upgradeOfArgument(const GameMessage &m, size_t index)
{
	const GameMessageArgument *a = m.getArgument(index);
	if (!a || a->type != ARGUMENTDATATYPE_INTEGER || !TheUpgradeCenter)
	{
		return nullptr;
	}
	return TheUpgradeCenter->findUpgradeByMaskBit(a->integer); // RW 0x66F218
}
} // namespace

bool UpgradeCommands::queueUpgrade(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *a = m.getArgument(1);
	if (!a || a->type != ARGUMENTDATATYPE_INTEGER)
	{
		return false; // malformed: counted unexecuted by the dispatcher
	}
	const UpgradeTemplate *upgrade = upgradeOfArgument(m, 1);
	if (!upgrade)
	{
		return true; // RW 0x77A715: an unknown bit does nothing
	}
	for (Object *obj : group(logic, m.getPlayerIndex())) // RW 0x76FBFB
	{
		const Player *owner = obj->getControllingPlayer();
		if (!UpgradeCenter::canAffordUpgrade(owner, upgrade, obj))
		{
			continue;
		}
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_OBJECT && (obj->hasUpgrade(upgrade) || !obj->affectedByUpgrade(upgrade)))
		{
			continue;
		}
		ProductionUpdateInterface *pu = obj->getProductionUpdate();
		if (pu && pu->canQueueUpgrade(upgrade) != 4)
		{
			pu->queueUpgrade(upgrade);
		}
	}
	return true;
}

bool UpgradeCommands::cancelUpgrade(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *a = m.getArgument(0);
	if (!a || a->type != ARGUMENTDATATYPE_INTEGER)
	{
		return false;
	}
	const UpgradeTemplate *upgrade = upgradeOfArgument(m, 0);
	if (!upgrade)
	{
		return true;
	}
	for (Object *obj : group(logic, m.getPlayerIndex())) // RW 0x76FC86
	{
		if (ProductionUpdateInterface *pu = obj->getProductionUpdate())
		{
			pu->cancelUpgrade(upgrade);
		}
	}
	return true;
}

void UpgradeCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_QUEUE_UPGRADE, kLane, [](GameLogic &l, const GameMessage &m) { return queueUpgrade(l, m); });
	d.registerHandler(MSG_CANCEL_UPGRADE, kLane, [](GameLogic &l, const GameMessage &m) { return cancelUpgrade(l, m); });
}
