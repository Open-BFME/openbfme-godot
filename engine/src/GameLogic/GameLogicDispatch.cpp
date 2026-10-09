// OpenBFME. GPL-3.0.
// See GameLogic/GameLogicDispatch.h.

#include "GameLogic/GameLogicDispatch.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <stdexcept>

void GameLogicDispatch::attach(CommandList &list)
{
	// RW 0x62E8D4 .. 0x62E905: every pending command goes to the dispatcher, then the list resets
	const bool installed = m_logic.installPhaseWork("commandList", [this, &list]() {
		const std::vector<GameMessage> messages = list.messages();
		list.reset();
		for (const GameMessage &m : messages)
		{
			dispatch(m);
		}
	});
	if (!installed)
	{
		throw std::logic_error("GameLogicDispatch: GameLogic has no `commandList` phase row to install into");
	}
}

bool GameLogicDispatch::executedHere(int type)
{
	switch (type)
	{
		case MSG_CREATE_SELECTED_GROUP:
		case MSG_CREATE_SELECTED_GROUP_NO_SOUND:
		case MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE:
		case MSG_REMOVE_FROM_SELECTED_GROUP:
		case MSG_DESTROY_SELECTED_GROUP:
		case MSG_QUEUE_UNIT_CREATE:
		case MSG_CANCEL_UNIT_CREATE:
		case MSG_SET_RALLY_POINT:
			return true;
		default:
			return false;
	}
}

void GameLogicDispatch::registerHandler(int type, const char *lane, Handler handler)
{
	if (executedHere(type) || m_handlers.count(type) != 0)
	{
		throw std::logic_error(std::string("GameLogicDispatch: message ") + GameMessageTypeName(type) + " already has an executor (" + handlerLane(type) + ")");
	}
	m_handlers[type] = Registered{ lane, std::move(handler) };
}

std::string GameLogicDispatch::handlerLane(int type) const
{
	if (executedHere(type))
	{
		return "PROD-1";
	}
	auto it = m_handlers.find(type);
	return it == m_handlers.end() ? std::string() : it->second.lane;
}

void GameLogicDispatch::error(const GameMessage &m, const std::string &text)
{
	m_errors.push_back(std::string(GameMessageTypeName(m.getType())) + " from player " + std::to_string(m.getPlayerIndex()) + ": " + text);
}

std::vector<ObjectID> GameLogicDispatch::selection(int playerIndex) const
{
	std::vector<ObjectID> out;
	if (const Player *player = m_logic.players().getNthPlayer(playerIndex))
	{
		for (ObjectID id : player->selection())
		{
			if (const Object *o = m_logic.findObjectByID(id))
			{
				if (!o->isDestroyed())
				{
					out.push_back(id);
				}
			}
		}
	}
	return out;
}

// RW 0x779A21: the first object of the player's selection after RW 0x76ED16 removed the objects the player does not control
Object *GameLogicDispatch::firstSelected(int playerIndex)
{
	Player *player = m_logic.players().getNthPlayer(playerIndex);
	if (!player)
	{
		return nullptr;
	}
	for (ObjectID id : selection(playerIndex))
	{
		Object *o = m_logic.findObjectByID(id);
		if (o && o->getControllingPlayer() == player)
		{
			return o;
		}
	}
	return nullptr;
}

void GameLogicDispatch::createSelectedGroup(const GameMessage &m)
{
	const GameMessageArgument *flag = m.getArgument(0);
	if (!flag || flag->type != ARGUMENTDATATYPE_BOOLEAN)
	{
		error(m, "the first argument must be the createNewGroup flag");
		return;
	}
	Player *player = m_logic.players().getNthPlayer(m.getPlayerIndex());
	if (!player)
	{
		error(m, "no such player");
		return;
	}
	std::vector<ObjectID> &sel = player->selection();
	if (flag->boolean)
	{
		sel.clear();
	}
	for (size_t i = 1; i < m.getArgumentCount(); ++i)
	{
		const GameMessageArgument *a = m.getArgument(i);
		if (a->type == ARGUMENTDATATYPE_OBJECTID && std::find(sel.begin(), sel.end(), a->objectID) == sel.end())
		{
			sel.push_back(a->objectID);
		}
	}
}

// RW 0x77A147: the objects named by the arguments leave the player's selection
void GameLogicDispatch::removeFromSelectedGroup(const GameMessage &m)
{
	Player *player = m_logic.players().getNthPlayer(m.getPlayerIndex());
	if (!player)
	{
		error(m, "no such player");
		return;
	}
	std::vector<ObjectID> &sel = player->selection();
	for (size_t i = 0; i < m.getArgumentCount(); ++i)
	{
		const GameMessageArgument *a = m.getArgument(i);
		if (a && a->type == ARGUMENTDATATYPE_OBJECTID)
		{
			sel.erase(std::remove(sel.begin(), sel.end(), a->objectID), sel.end());
		}
	}
}

void GameLogicDispatch::dispatch(const GameMessage &m)
{
	switch (m.getType())
	{
		case MSG_CREATE_SELECTED_GROUP:
		case MSG_CREATE_SELECTED_GROUP_NO_SOUND:
		case MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE:
			createSelectedGroup(m);
			break;
		case MSG_REMOVE_FROM_SELECTED_GROUP:
			removeFromSelectedGroup(m);
			break;
		case MSG_DESTROY_SELECTED_GROUP:
			if (Player *p = m_logic.players().getNthPlayer(m.getPlayerIndex()))
			{
				p->selection().clear();
			}
			else
			{
				error(m, "no such player");
			}
			break;
		case MSG_QUEUE_UNIT_CREATE:
			queueUnitCreate(m);
			break;
		case MSG_CANCEL_UNIT_CREATE:
			cancelUnitCreate(m);
			break;
		case MSG_SET_RALLY_POINT:
			setRallyPoint(m);
			break;
		default:
		{
			auto it = m_handlers.find(m.getType());
			if (it != m_handlers.end())
			{
				if (!it->second.handler(m_logic, m))
				{
					++m_unhandled[m.getType()]; // malformed (the lane counted why): seen but not executed
				}
			}
			else
			{
				++m_unhandled[m.getType()];
			}
			break;
		}
	}
}

// RW 0x77A764
void GameLogicDispatch::queueUnitCreate(const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1), *a2 = m.getArgument(2), *a3 = m.getArgument(3), *a4 = m.getArgument(4);
	if (!a0 || !a1 || !a2 || !a3 || !a4 || a0->type != ARGUMENTDATATYPE_BOOLEAN || a1->type != ARGUMENTDATATYPE_INTEGER || a2->type != ARGUMENTDATATYPE_INTEGER ||
		a3->type != ARGUMENTDATATYPE_BOOLEAN || a4->type != ARGUMENTDATATYPE_BOOLEAN)
	{
		error(m, "expected { bool fromBuildIndex, int id, int value30, bool batch, bool secondary }");
		return;
	}
	Object *producer = firstSelected(m.getPlayerIndex());
	int buildIndex = -1;
	const ThingTemplate *tmpl = nullptr;
	if (a0->boolean)
	{
		buildIndex = a1->integer;
	}
	else
	{
		tmpl = m_logic.things().findByTemplateID((unsigned short)a1->integer);
		if (tmpl)
		{
			tmpl = tmpl->getFinalOverride();
		}
	}
	if (!producer || (!tmpl && buildIndex == -1))
	{
		return;
	}
	ProductionUpdateInterface *pu = nullptr;
	if (a4->boolean)
	{
		for (const std::unique_ptr<BehaviorModule> &mod : producer->modules())
		{
			if (ProductionUpdateInterface *p = mod->getProductionUpdateInterface())
			{
				if (p->isSecondaryQueue())
				{
					pu = p;
					break;
				}
			}
		}
	}
	if (!pu)
	{
		pu = producer->getProductionUpdate();
	}
	if (!pu)
	{
		return;
	}
	const ProductionID id = pu->requestUniqueUnitID();
	pu->queueCreateUnit(tmpl, buildIndex, id, a2->integer, a3->boolean, std::string(), false);
}

// RW 0x77A844
void GameLogicDispatch::cancelUnitCreate(const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1), *a2 = m.getArgument(2);
	if (!a0 || !a1 || !a2 || a0->type != ARGUMENTDATATYPE_BOOLEAN || a1->type != ARGUMENTDATATYPE_INTEGER || a2->type != ARGUMENTDATATYPE_BOOLEAN)
	{
		error(m, "expected { bool byIndex, int id, bool all }");
		return;
	}
	Object *producer = firstSelected(m.getPlayerIndex());
	if (!producer || producer->getControllingPlayer() != m_logic.players().getNthPlayer(m.getPlayerIndex()))
	{
		return;
	}
	ProductionUpdateInterface *pu = producer->getProductionUpdate();
	if (!pu)
	{
		return;
	}
	if (a0->boolean)
	{
		pu->cancelUnitCreateByBuildIndex(a1->integer); // lane HERO-1: RW 0x77A888 -> slot 12 (RW 0x8A047B)
		return;
	}
	const ThingTemplate *tmpl = m_logic.things().findByTemplateID((unsigned short)a1->integer); // not the final override (RW 0x77A8A9)
	pu->cancelUnitCreateByType(tmpl, a2->boolean);
}

// RW 0x77A26C / 0x779544 / 0x7798D5
void GameLogicDispatch::setRallyPoint(const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1), *a2 = m.getArgument(2), *a3 = m.getArgument(3);
	if (!a0 || !a1 || !a2 || !a3 || a0->type != ARGUMENTDATATYPE_OBJECTID || a1->type != ARGUMENTDATATYPE_LOCATION || a2->type != ARGUMENTDATATYPE_BOOLEAN ||
		a3->type != ARGUMENTDATATYPE_OBJECTID)
	{
		error(m, "expected { object id, location, bool global, object id target }");
		return;
	}
	Object *obj = m_logic.findObjectByID(a0->objectID);
	Object *target = a3->objectID != INVALID_ID ? m_logic.findObjectByID(a3->objectID) : nullptr;
	if (!obj)
	{
		return;
	}
	auto apply = [&](Object &o, Coord3D dest, Object *tgt) {
		ExitInterface *exit = o.getObjectExitInterface();
		if (!exit)
		{
			return;
		}
		if (tgt)
		{
			dest = *tgt->getPosition();
		}
		exit->setRallyPoint(&dest); // RW 0x779544 step 4 (the pathfinder validation of the destination is not ported, S-202)
	};
	if (a2->boolean && !target)
	{
		// RW 0x7798D5: the global rally point: every AUTO_RALLYPOINT object of the player (retail: of the LOCAL player; the message's player is the one that
		// is the local player on the machine that sent it)
		static const int autoRally = ObjectTemplateInfoBuilder::kindOfIndex("AUTO_RALLYPOINT");
		Player *player = m_logic.players().getNthPlayer(m.getPlayerIndex());
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == player && autoRally >= 0 && o->isKindOf((unsigned)autoRally))
			{
				apply(*o, a1->location, nullptr);
			}
		}
		return;
	}
	apply(*obj, a1->location, target);
}

std::vector<std::string> GameLogicDispatch::acceptanceStops() const
{
	std::string types;
	for (const auto &kv : m_unhandled)
	{
		types += std::string(types.empty() ? "" : ", ") + GameMessageTypeName(kv.first) + " x" + std::to_string(kv.second);
	}
	return { "[S-208] the command dispatcher executes MSG_QUEUE_UNIT_CREATE, MSG_CANCEL_UNIT_CREATE, MSG_SET_RALLY_POINT and the selection group messages "
			 "(CREATE_SELECTED_GROUP, REMOVE_FROM_SELECTED_GROUP, DESTROY_SELECTED_GROUP) itself and the types other lanes registered a handler for (the movement lane: "
			 "move, force move, attack move as a move, waypoint, stop, formation move, move and orientate); every other logic message is counted and not executed; unexecuted so far: " + (types.empty() ? std::string("none") : types) };
}
