// OpenBFME. GPL-3.0.
// See GameLogic/CastleCommands.h for the sources of every rule.

#include "GameLogic/CastleCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"

#include <string>

namespace
{
const char kLane[] = "BACKLOG2";
CastleCommands::Stats g_stats;

// RW 0x779A21 then RW 0x68BDA5(CastleBehavior's key): the castle module of the first selected object the player controls
CastleBehavior *selectedCastle(GameLogic &logic, const GameMessage &m)
{
	Player *player = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!player)
	{
		return nullptr;
	}
	for (ObjectID id : player->selection())
	{
		Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && o->getControllingPlayer() == player)
		{
			return dynamic_cast<CastleBehavior *>(o->findModule("CastleBehavior"));
		}
	}
	return nullptr;
}

// a message whose arguments are not the layout the client builds is refused and named in the game's report, before anything of the world is read or changed
bool malformed(GameLogic &logic, const GameMessage &m, const char *expected)
{
	++g_stats.malformed;
	logic.reportError(std::string(GameMessageTypeName(m.getType())) + " from player " + std::to_string(m.getPlayerIndex()) + ": malformed (" + expected + " expected, " +
					  std::to_string(m.getArgumentCount()) + " argument(s) given): refused");
	return false;
}

bool pack(GameLogic &logic, const GameMessage &m)
{
	// RW 0x77C265 (the client appends the message without arguments, RW 0x940AAC)
	if (m.getArgumentCount() != 0)
	{
		return malformed(logic, m, "no argument");
	}
	CastleBehavior *castle = selectedCastle(logic, m);
	if (!castle || !castle->isUnpacked() || !castle->isPlayerAllowedToPackOrUnpack(castle->getObject()->getControllingPlayer()))
	{
		++g_stats.refused;
		return true;
	}
	castle->abandonByPlayer(); // RW 0x77C2B7
	++g_stats.packs;
	return true;
}

bool unpack(GameLogic &logic, const GameMessage &m)
{
	// RW 0x77C2C1 (the client appends the message without arguments, RW 0x940AB6)
	if (m.getArgumentCount() != 0)
	{
		return malformed(logic, m, "no argument");
	}
	CastleBehavior *castle = selectedCastle(logic, m);
	const Player *controller = castle ? castle->getObject()->getControllingPlayer() : nullptr;
	if (!castle || !castle->canUnpack(true) || !castle->isPlayerAllowedToPackOrUnpack(controller) || !castle->canAffordUnpack(controller))
	{
		++g_stats.refused;
		return true;
	}
	castle->initiateUnpack(false, nullptr); // RW 0x77C32E
	++g_stats.unpacks;
	return true;
}

bool unpackExplicit(GameLogic &logic, const GameMessage &m)
{
	// RW 0x77C338: argument 0's low 16 bits name the template (a closed check: anything but an integer is malformed)
	const GameMessageArgument *a = m.getArgument(0);
	if (!a || a->type != ARGUMENTDATATYPE_INTEGER || m.getArgumentCount() != 1)
	{
		return malformed(logic, m, "one integer template id");
	}
	CastleBehavior *castle = selectedCastle(logic, m);
	const ThingTemplate *tt = logic.things().findByTemplateID((unsigned short)(a->integer & 0xFFFF)); // RW 0x77C352 / 0x6CFE6C
	const Player *controller = castle ? castle->getObject()->getControllingPlayer() : nullptr;
	if (!tt)
	{
		// RW 0x77C376: nothing happens; the port names the refusal (an id no template has is no message a retail client builds)
		++g_stats.refused;
		logic.reportError("MSG_CASTLE_UNPACK_EXPLICIT_OBJECT from player " + std::to_string(m.getPlayerIndex()) + ": template id " + std::to_string(a->integer & 0xFFFF) +
						  " names no template: refused");
		return true;
	}
	if (!castle || !castle->canUnpack(true) || !castle->isPlayerAllowedToPackOrUnpack(controller) || !castle->canAffordUnpackOf(controller, tt))
	{
		++g_stats.refused;
		return true;
	}
	castle->initiateUnpack(false, tt); // RW 0x77C3C7 -> 0x77C32B
	++g_stats.explicitUnpacks;
	return true;
}
} // namespace

void CastleCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_CASTLE_UNPACK, kLane, unpack);
	d.registerHandler(MSG_CASTLE_PACK, kLane, pack);
	d.registerHandler(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, kLane, unpackExplicit);
}

const CastleCommands::Stats &CastleCommands::stats()
{
	return g_stats;
}
