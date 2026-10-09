// OpenBFME. GPL-3.0.
// Lane GARRISON-1: GameWorld's garrison commands (see GodotDevice/GodotGameWorld.h): MSG_ENTER / MSG_EVACUATE through the lockstep command path and a read-only
// report of a container for the garrison viewer. Lane GARRISON-2: the crew of a siege engine in that report, a turret's report and a debug weapon set toggle
// (the Rohirrim's bow mode: the command button's MSG_WEAPONSET_TOGGLE is not a ported logic message) for the transport viewer.

#include "GodotDevice/GodotGameWorld.h"

#include "Common/Player.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void GameWorld::bindGarrisonMethods()
{
	ClassDB::bind_method(D_METHOD("order_garrison", "ids", "container"), &GameWorld::order_garrison);
	ClassDB::bind_method(D_METHOD("order_evacuate", "container"), &GameWorld::order_evacuate);
	ClassDB::bind_method(D_METHOD("get_garrison", "container"), &GameWorld::get_garrison);
	ClassDB::bind_method(D_METHOD("get_turret", "id"), &GameWorld::get_turret);
	ClassDB::bind_method(D_METHOD("debug_set_weapon_set_flag", "id", "flag", "on"), &GameWorld::debug_set_weapon_set_flag);
	ClassDB::bind_method(D_METHOD("debug_ai_attack", "id", "target"), &GameWorld::debug_ai_attack);
	ClassDB::bind_method(D_METHOD("debug_ai_move", "id", "x", "y"), &GameWorld::debug_ai_move);
	ClassDB::bind_method(D_METHOD("order_toggle_formation", "horde"), &GameWorld::order_toggle_formation); // lane COMBAT-3
}

// lane COMBAT-3: MSG_HORDE_TOGGLE_FORMATION (1107, argument 0 the horde's id; the dispatcher case RW 0x77BE83) through the lockstep command path, as the horde's
// formation button sends it (the porcupine formation of the pikemen)
Dictionary GameWorld::order_toggle_formation(int64_t horde)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	const ::Object *o = m_game ? m_game->logic().findObjectByID((::ObjectID)horde) : nullptr;
	if (!o || o->isDestroyed() || !o->getControllingPlayer())
	{
		r["error"] = "no owned object with that id";
		return r;
	}
	::GameMessage m(MSG_HORDE_TOGGLE_FORMATION, o->getControllingPlayer()->getPlayerIndex());
	m.appendObjectIDArgument(o->getID());
	m_game->commands().append(m);
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::debug_ai_move(int64_t id, double x, double y)
{
	// a viewer-scenario hook only: it changes the simulation without a command, so never in a started skirmish, a network game or under the logic worker
	if (!m_game || m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		Dictionary refused;
		refused["ok"] = false;
		refused["error"] = "debug hook refused: no loaded viewer game, or a started / network game or a logic worker is running";
		return refused;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	::Object *o = m_game ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
	if (!ai)
	{
		r["error"] = "no object with an AI";
		return r;
	}
	ai->aiMoveToPosition(Coord3D{ (float)x, (float)y, 0.0f }, CMD_FROM_AI); // the AI's own move (a horde's members keep their targets: no group order)
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::debug_ai_attack(int64_t id, int64_t target)
{
	// a viewer-scenario hook only: it changes the simulation without a command, so never in a started skirmish, a network game or under the logic worker
	if (!m_game || m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		Dictionary refused;
		refused["ok"] = false;
		refused["error"] = "debug hook refused: no loaded viewer game, or a started / network game or a logic worker is running";
		return refused;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	::Object *t = m_game->logic().findObjectByID((::ObjectID)target);
	AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
	if (!ai || !t)
	{
		r["error"] = "no object with an AI or no target";
		return r;
	}
	r["ok"] = ai->aiAttackObject(t, CMD_FROM_AI); // the AI's own order (no player command path, no shroud test: S-565)
	return r;
}

Dictionary GameWorld::get_turret(int64_t id) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
	const TurretAI *t = ai ? ai->turret() : nullptr;
	if (!t)
	{
		return d;
	}
	d["ok"] = true;
	d["angle"] = t->getTurretAngle();
	d["state"] = (int64_t)t->currentStateId();
	d["shots"] = (int64_t)t->stats().shots;
	d["turn_frames"] = (int64_t)t->stats().turnFrames;
	d["on_turret"] = t->isOwnersCurWeaponOnTurret();
	return d;
}

Dictionary GameWorld::debug_set_weapon_set_flag(int64_t id, const String &flag, bool on)
{
	// a viewer-scenario hook only: it changes the simulation without a command, so never in a started skirmish, a network game or under the logic worker
	if (!m_game || m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		Dictionary refused;
		refused["ok"] = false;
		refused["error"] = "debug hook refused: no loaded viewer game, or a started / network game or a logic worker is running";
		return refused;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	if (!o)
	{
		r["error"] = "no object";
		return r;
	}
	const int bit = CombatNames::weaponSetBit(flag.utf8().get_data());
	int64_t changed = 0;
	std::vector<::Object *> targets = { o };
	if (ContainModuleInterface *c = o->getContain())
	{
		if (c->getHordeContainInterface() && c->getContainedItemsList())
		{
			targets.insert(targets.end(), c->getContainedItemsList()->begin(), c->getContainedItemsList()->end());
		}
	}
	for (::Object *t : targets)
	{
		if (ObjectWeapons *w = t->getWeapons())
		{
			w->setWeaponSetFlag(bit, on);
			++changed;
		}
	}
	r["ok"] = changed > 0;
	r["objects"] = changed;
	return r;
}

Dictionary GameWorld::order_garrison(const Array &ids, int64_t container)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	const ::Object *c = m_game->logic().findObjectByID((::ObjectID)container);
	if (!c || c->isDestroyed() || !c->getContain())
	{
		r["error"] = "no container";
		return r;
	}
	std::vector<::ObjectID> objects;
	int player = -1;
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		const ::Object *o = m_game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (!o || o->isDestroyed() || !o->getAIUpdateInterface())
		{
			continue;
		}
		objects.push_back(o->getID());
		if (player < 0 && o->getControllingPlayer())
		{
			player = o->getControllingPlayer()->getPlayerIndex();
		}
	}
	if (objects.empty() || player < 0)
	{
		r["error"] = "no object with an AI and an owner among the ids";
		return r;
	}
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (::ObjectID id : objects)
	{
		sel.appendObjectIDArgument(id);
	}
	m_game->commands().append(sel);
	::GameMessage enter(MSG_ENTER, player);
	enter.appendObjectIDArgument(objects.front());
	enter.appendObjectIDArgument(c->getID());
	m_game->commands().append(enter);
	r["ok"] = true;
	r["player"] = (int64_t)player;
	return r;
}

Dictionary GameWorld::order_evacuate(int64_t container)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	const ::Object *c = m_game->logic().findObjectByID((::ObjectID)container);
	if (!c || c->isDestroyed() || !c->getContain() || !c->getControllingPlayer())
	{
		r["error"] = "no owned container";
		return r;
	}
	const int player = c->getControllingPlayer()->getPlayerIndex();
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(c->getID());
	m_game->commands().append(sel);
	m_game->commands().append(::GameMessage(MSG_EVACUATE, player));
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::get_garrison(int64_t container) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	::Object *c = m_game->logic().findObjectByID((::ObjectID)container);
	if (!c || !c->getContain())
	{
		return d;
	}
	ContainModuleInterface *contain = c->getContain();
	d["ok"] = true;
	d["garrisonable"] = contain->isGarrisonable();
	d["count"] = (int64_t)contain->getContainCount();
	Array riders;
	int64_t hidden = 0, members = 0, entering = 0;
	bool hordeGarrisoned = false;
	if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
	{
		for (::Object *o : *items)
		{
			riders.push_back((int64_t)o->getID());
			if (ContainModuleInterface *h = o->getContain())
			{
				if (HordeContainInterface *hi = h->getHordeContainInterface())
				{
					hordeGarrisoned = hordeGarrisoned || hi->isGarrisoned();
					entering += (int64_t)hi->membersEntering().size();
				}
				if (const ContainModuleInterface::ContainedItemsList *m = h->getContainedItemsList())
				{
					for (::Object *mo : *m)
					{
						++members;
						hidden += mo->isDrawableHidden() ? 1 : 0;
					}
				}
			}
		}
	}
	d["riders"] = riders;
	d["members"] = members;
	d["members_hidden"] = hidden;
	d["entering"] = entering;
	d["horde_garrisoned"] = hordeGarrisoned;
	if (const OpenContain *open = dynamic_cast<const OpenContain *>(contain))
	{
		if (const ContainModuleInterface::ContainedItemsList *crew = open->crewList()) // lane GARRISON-2: a siege engine's crew
		{
			d["crew"] = (int64_t)crew->size();
		}
	}
	d["crew_power"] = contain->getCrewPowerMultiplier();
	if (const GarrisonContain *g = dynamic_cast<const GarrisonContain *>(contain))
	{
		d["max"] = (int64_t)g->getContainMax();
		d["points_in_use"] = (int64_t)g->pointsInUse();
		Array counts;
		for (int k = 0; k < GarrisonContain::GARRISON_POINT_CONDITIONS; ++k)
		{
			counts.push_back((int64_t)g->pointCount(k));
		}
		d["point_counts"] = counts;
	}
	return d;
}
