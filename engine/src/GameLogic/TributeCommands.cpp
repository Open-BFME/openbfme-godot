// OpenBFME. GPL-3.0.
// See GameLogic/TributeCommands.h. Lane PLAY-1.

#include "GameLogic/TributeCommands.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"

#include <algorithm>

void TributeCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_GIVE_MONEY, "PLAY-1", [](GameLogic &l, const GameMessage &m) { return giveMoney(l, m); });
}

// the dispatcher's case 0x466 (RW 0x779A3D): three integers
bool TributeCommands::giveMoney(GameLogic &logic, const GameMessage &msg)
{
	const GameMessageArgument *a0 = msg.getArgument(0), *a1 = msg.getArgument(1), *a2 = msg.getArgument(2);
	if (!a0 || !a1 || !a2 || a0->type != ARGUMENTDATATYPE_INTEGER || a1->type != ARGUMENTDATATYPE_INTEGER || a2->type != ARGUMENTDATATYPE_INTEGER)
	{
		return false;
	}
	callPlayerGiveMoney(logic, a0->integer, a1->integer, (unsigned)a2->integer);
	return true;
}

// RW 0x6264E1
bool TributeCommands::callPlayerGiveMoney(GameLogic &logic, int fromIndex, int toIndex, unsigned amount)
{
	// RW 0x626087: frame >= minutes * LOGICFRAMES_PER_SECOND * 60 (32-bit products, unsigned compare)
	const unsigned minutes = (unsigned)logic.economy().settings().numMinutesBeforePlayersCanTransferMoney;
	if (logic.getFrame() < minutes * (unsigned)LOGICFRAMES_PER_SECOND * 60u)
	{
		return false;
	}
	if (fromIndex < 0 || fromIndex >= 20 || toIndex < 0 || toIndex >= 20)
	{
		return false;
	}
	Player *giver = nullptr, *receiver = nullptr;
	for (int i = 0; i < 20; ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (!p)
		{
			continue;
		}
		if (p->getPlayerIndex() == fromIndex)
		{
			giver = p;
		}
		else if (p->getPlayerIndex() == toIndex)
		{
			receiver = p;
		}
		if (giver && receiver)
		{
			break;
		}
	}
	if (!giver || !receiver || giver->isDefeated() || receiver->isDefeated()) // RW 0x6AAC4B (+ 0x754, S-1922)
	{
		return false;
	}
	const unsigned cash = giver->getMoney()->countMoney();
	const unsigned request = std::min(cash, amount);
	const unsigned taken = giver->getMoney()->withdraw(request, &giver->getScoreKeeper(), true); // RW 0x7B17EF
	receiver->getMoney()->deposit(taken, &receiver->getScoreKeeper(), true);                    // RW 0x7B18B8
	giver->getScoreKeeper().addMoneyGivenToAllies((std::int32_t)taken);                           // RW 0x79DD01
	receiver->getScoreKeeper().addMoneyReceivedFromAllies((std::int32_t)taken);                   // RW 0x79DCE9
	return taken != 0;
}
