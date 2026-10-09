// OpenBFME. GPL-3.0.
// See PlayerCommands.h.

#include "GameLogic/PlayerCommands.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

namespace
{
const char *const kLane = "HUD-1";

Player *issuer(GameLogic &logic, const GameMessage &m)
{
	return logic.players().getNthPlayer(m.getPlayerIndex());
}

bool live(GameLogic &logic, ObjectID id)
{
	Object *o = logic.findObjectByID(id);
	return o && !o->isDestroyed();
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

bool selectTeam(GameLogic &logic, const GameMessage &m, int n)
{
	Player *p = issuer(logic, m);
	if (!p)
	{
		return false;
	}
	std::vector<ObjectID> &sel = p->selection();
	sel.clear();
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

void PlayerCommands::registerHandlers(GameLogicDispatch &d)
{
	for (int n = 0; n < Player::NUM_HOTKEY_SQUADS; ++n)
	{
		d.registerHandler(MSG_CREATE_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return createTeam(l, m, n); });
		d.registerHandler(MSG_SELECT_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return selectTeam(l, m, n); });
		d.registerHandler(MSG_ADD_TEAM0 + n, kLane, [n](GameLogic &l, const GameMessage &m) { return addTeam(l, m, n); });
	}
	d.registerHandler(MSG_AREA_SELECTION, kLane, [](GameLogic &, const GameMessage &) { return true; });
}
