// OpenBFME. GPL-3.0.
// See PlayerCommands.h.

#include "GameLogic/PlayerCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>

namespace
{
const char *const kLane = "HUD-1";

Player *issuer(GameLogic &logic, const GameMessage &m)
{
	return logic.players().getNthPlayer(m.getPlayerIndex());
}

// lane INPUT-1 (review r1): a squad's members as Squad::getLiveObjects (RW 0x8DB103) hands them out: the object exists and Object::isSelectable
// (RW 0x68DE58) passes. The membership itself is kept (a garrisoned or unselectable member comes back when it is selectable again).
bool live(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	return o && PlayerCommands::isSelectable(*o);
}

// RW 0x8DB0AB (getLiveObjects(true), the logic's select / add handlers RW 0x6AD5CA / 0x6AD677): a member whose object no longer exists leaves the squad
void pruneMissing(GameLogic &logic, std::vector<ObjectID> &squad)
{
	squad.erase(std::remove_if(squad.begin(), squad.end(), [&](ObjectID id) { return logic.findObjectByID(id) == nullptr; }), squad.end());
}

bool createTeam(GameLogic &logic, const GameMessage &m, int n)
{
	Player *p = issuer(logic, m);
	if (!p)
	{
		return false;
	}
	std::vector<ObjectID> &squad = p->hotkeySquad(n);
	squad.clear();
	for (size_t i = 0; i < m.getArgumentCount(); ++i)
	{
		const ObjectID id = m.getArgument(i)->objectID;
		if (!logic.findObjectByID(id))
		{
			continue;
		}
		for (int s = 0; s < Player::NUM_HOTKEY_SQUADS; ++s) // removeObjectFromHotkeySquad
		{
			std::vector<ObjectID> &other = p->hotkeySquad(s);
			other.erase(std::remove(other.begin(), other.end(), id), other.end());
		}
		squad.push_back(id);
	}
	return true;
}

// RW 0x6AD722 (lane INPUT-1, the dispatcher's case 1138 .. 1147 at RW 0x77CDDB): Player::processCreateTeamGameMessage without the clear: each named
// object leaves every squad (RW 0x6AABE0) and joins squad n (RW 0x8DB030)
bool addToTeam(GameLogic &logic, const GameMessage &m, int n)
{
	Player *p = issuer(logic, m);
	if (!p)
	{
		return false;
	}
	for (size_t i = 0; i < m.getArgumentCount(); ++i)
	{
		const ObjectID id = m.getArgument(i)->objectID;
		if (!logic.findObjectByID(id))
		{
			continue;
		}
		for (int s = 0; s < Player::NUM_HOTKEY_SQUADS; ++s)
		{
			std::vector<ObjectID> &other = p->hotkeySquad(s);
			other.erase(std::remove(other.begin(), other.end(), id), other.end());
		}
		p->hotkeySquad(n).push_back(id);
	}
	return true;
}

// RW 0x77BCA4 (lane INPUT-1 r2): MSG_CHANGE_ORDERMODE (1129): argument 0 is the new mode; with a second argument the change happens only while the
// player's mode is that value (RW 0x77BCB7 .. 0x77BCCF); Player::setOrderMode RW 0x6AAC78
bool changeOrderMode(GameLogic &logic, const GameMessage &m)
{
	Player *p = issuer(logic, m);
	const GameMessageArgument *mode = m.getArgument(0);
	if (!p || !mode || mode->type != ARGUMENTDATATYPE_INTEGER)
	{
		return false;
	}
	if (m.getArgumentCount() > 1)
	{
		const GameMessageArgument *from = m.getArgument(1);
		if (!from || from->type != ARGUMENTDATATYPE_INTEGER || from->integer != p->orderMode())
		{
			return true; // RW 0x77BCCF: not the expected mode: nothing changes
		}
	}
	p->setOrderMode(mode->integer);
	return true;
}

bool selectTeam(GameLogic &logic, const GameMessage &m, int n)
{
	Player *p = issuer(logic, m);
	if (!p)
	{
		return false;
	}
	std::vector<ObjectID> &sel = p->selection();
	sel.clear();
	pruneMissing(logic, p->hotkeySquad(n));
	for (ObjectID id : p->hotkeySquad(n))
	{
		if (live(logic, id))
		{
			sel.push_back(id);
		}
	}
	return true;
}

bool addTeam(GameLogic &logic, const GameMessage &m, int n)
{
	Player *p = issuer(logic, m);
	if (!p)
	{
		return false;
	}
	std::vector<ObjectID> &sel = p->selection();
	pruneMissing(logic, p->hotkeySquad(n));
	for (ObjectID id : p->hotkeySquad(n))
	{
		if (live(logic, id) && std::find(sel.begin(), sel.end(), id) == sel.end())
		{
			sel.push_back(id);
		}
	}
	return true;
}
} // namespace

bool PlayerCommands::isSelectable(const Object &o)
{
	// RW 0x68DE58 (Object::isSelectable): KindOf ALWAYS_SELECTABLE (template + 0x10F bit 2) always; else not while the status UNSELECTABLE (3) is set,
	// not when effectively dead (object + 0x458 bit 0) unless the template's KeepSelectableWhenDead (+ 0x642, field table row RW 0xDA4858), not for KindOf
	// DRONE (+ 0x111 bit 1); then the object's own selectable flag (+ 0x454, the script's). NOT PORTED (S-280): RotWK also lets a WALK_ON_TOP_OF_WALL
	// object pass while the InGameUI's GUI command is of type 0x18 (the client's state, read from the logic in retail).
	if (o.isDestroyed())
	{
		return false;
	}
	if (o.isKindOfName("ALWAYS_SELECTABLE"))
	{
		return true;
	}
	static const int unselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	if (unselectable >= 0 && o.testStatus((unsigned)unselectable))
	{
		return false;
	}
	if (o.isEffectivelyDead())
	{
		bool keep = false;
		if (const ThingTemplate *tt = static_cast<const ThingTemplate *>(o.getTemplate()))
		{
			if (const FieldValue *v = tt->getFinalOverride()->findField("KeepSelectableWhenDead"))
			{
				if (const bool *b = std::get_if<bool>(v))
				{
					keep = *b;
				}
			}
		}
		if (!keep)
		{
			return false;
		}
	}
	if (o.isKindOfName("DRONE"))
	{
		return false;
	}
	return o.isScriptSelectable();
}

void PlayerCommands::registerHandlers(GameLogicDispatch &d)
{
	for (int n = 0; n < Player::NUM_HOTKEY_SQUADS; ++n)
	{
		d.registerHandler(MSG_CREATE_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return createTeam(l, m, n); });
		d.registerHandler(MSG_SELECT_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return selectTeam(l, m, n); });
		d.registerHandler(MSG_ADD_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return addTeam(l, m, n); });
		d.registerHandler(MSG_ADD_TO_TEAM0 + n, "INPUT-1", [n](GameLogic &l, const GameMessage &m) { return addToTeam(l, m, n); });
	}
	d.registerHandler(MSG_CHANGE_ORDERMODE, "INPUT-1", [](GameLogic &l, const GameMessage &m) { return changeOrderMode(l, m); });
	d.registerHandler(MSG_AREA_SELECTION, kLane, [](GameLogic &, const GameMessage &) { return true; });
}
