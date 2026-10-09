// OpenBFME. GPL-3.0.
// Lane STEALTH-1: GameWorld's stealth helpers for the stealth viewer (see GodotDevice/GodotGameWorld.h): the viewing player, a stance, the logic's terrain trees and
// what the InvisibilityManager says about an object. Scenario helpers outside the command path (like create_object).

#include "GodotDevice/GodotGameWorld.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/StealthLook.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void GameWorld::bindStealthMethods()
{
	ClassDB::bind_method(D_METHOD("set_local_player", "player"), &GameWorld::set_local_player);
	ClassDB::bind_method(D_METHOD("set_stance", "ids", "stance"), &GameWorld::set_stance);
	ClassDB::bind_method(D_METHOD("get_terrain_trees", "max"), &GameWorld::get_terrain_trees);
	ClassDB::bind_method(D_METHOD("get_invisibility", "id", "enemy"), &GameWorld::get_invisibility, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("cast_object_power_at", "player", "id", "power", "x", "y"), &GameWorld::cast_object_power_at);
	ClassDB::bind_method(D_METHOD("order_one_ring", "player", "ids"), &GameWorld::order_one_ring);
	ClassDB::bind_method(D_METHOD("debug_stealth_scenario", "id", "action", "name"), &GameWorld::debug_stealth_scenario, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_stealth_state", "id"), &GameWorld::get_stealth_state);
}

bool GameWorld::set_local_player(int64_t player)
{
	if (!m_game)
	{
		return false;
	}
	::Player *p = m_game->players().getNthPlayer((int)player);
	if (!p)
	{
		return false;
	}
	m_game->players().setLocalPlayer(p);
	return true;
}

int64_t GameWorld::set_stance(const Array &ids, int64_t stance)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || stance < 0 || stance >= STANCE_COUNT)
	{
		return 0;
	}
	int64_t n = 0;
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		::Object *o = m_game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (StancesBehavior *sb = o ? dynamic_cast<StancesBehavior *>(o->findModule("StancesBehavior")) : nullptr)
		{
			n += sb->setStance((StanceType)stance) ? 1 : 0;
		}
	}
	return n;
}

PackedVector3Array GameWorld::get_terrain_trees(int64_t max) const
{
	PackedVector3Array out;
	if (!m_game)
	{
		return out;
	}
	for (const Coord3D &t : m_game->logic().invisibility().trees())
	{
		if (max >= 0 && out.size() >= max)
		{
			break;
		}
		out.push_back(Vector3(t.x, t.y, t.z));
	}
	return out;
}

Dictionary GameWorld::get_invisibility(int64_t id, int64_t enemy) const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	const ::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	if (!o)
	{
		return d;
	}
	d["ok"] = true;
	d["type"] = (int64_t)InvisibilityManager::invisibilityType(*o);
	d["detected"] = InvisibilityManager::isDetected(*o);
	d["look"] = (int64_t)InvisibilityManager::clientLook(*o, m_game->players().getLocalPlayer());
	const ::Player *e = enemy >= 0 ? m_game->players().getNthPlayer((int)enemy) : nullptr;
	d["stealthed_for_enemy"] = e ? InvisibilityManager::isStealthedAndUndetected(*o, e) : false;
	// the opacity factor the client draws now (GameClient/StealthLook on the presented snapshot)
	const std::shared_ptr<const LogicSnapshot> presented = m_game->presentedSnapshot();
	const ObjectSnapshot *rec = presented ? presented->find(o->getID()) : nullptr;
	d["draw_opacity"] = rec ? (double)StealthLook::opacity(*rec, *presented, m_stealthClockMs) : 1.0;
	return d;
}

// ---- lane STEALTH-2 ------------------------------------------------------------------------------------------------------------------
bool GameWorld::cast_object_power_at(int64_t player, int64_t id, const String &power, double x, double y)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || !TheSpecialPowerStore || id <= 0)
	{
		return false;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power.utf8().get_data());
	if (!t || !m_game->logic().findObjectByID((::ObjectID)id))
	{
		return false;
	}
	GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, (int)player); // {id, location, target, options, source}
	m.appendIntegerArgument((int)t->getID());
	m.appendLocationArgument(Coord3D{ (float)x, (float)y, 0.0f });
	m.appendObjectIDArgument(INVALID_ID);
	m.appendIntegerArgument(0);
	m.appendObjectIDArgument((::ObjectID)id);
	m_game->commands().append(m);
	return true;
}

bool GameWorld::order_one_ring(int64_t player, const Array &ids)
{
	if (!m_game || ids.is_empty())
	{
		return false;
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, (int)player);
	sel.appendBooleanArgument(true);
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		sel.appendObjectIDArgument((::ObjectID)(int64_t)ids[i]);
	}
	m_game->commands().append(sel);
	m_game->commands().append(GameMessage(MSG_ONE_RING, (int)player));
	return true;
}

bool GameWorld::debug_stealth_scenario(int64_t id, const String &action, const String &name)
{
	if (!m_game || m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		return false;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return false;
	}
	if (action == String("unpause"))
	{
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if (SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get()))
			{
				while (sp->pauseCount() > 0)
				{
					sp->pauseCountdown(false);
				}
				sp->setReadyFrame(m_game->logic().getFrame());
			}
		}
		return true;
	}
	if (action == String("object_upgrade"))
	{
		const UpgradeTemplate *u = ::Player::resolveUpgrade(name.utf8().get_data(), false);
		if (!u)
		{
			return false;
		}
		o->giveUpgrade(u);
		return true;
	}
	if (action == String("ring_times"))
	{
		StealthUpdate *st = StealthUpdate::of(*o);
		if (!st)
		{
			return false;
		}
		StealthUpdateModuleData *d = const_cast<StealthUpdateModuleData *>(st->data());
		d->m_ringAnimTimeOn = 5;
		d->m_ringAnimTimeOff = 5;
		d->m_ringDelayAfterRemoving = 20;
		return true;
	}
	return false;
}

Dictionary GameWorld::get_stealth_state(int64_t id) const
{
	Dictionary d;
	d["ok"] = false;
	const ::Object *o = m_game && id > 0 ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return d;
	}
	d["ok"] = true;
	d["hidden"] = o->testStatus((unsigned)CombatNames::status("HIDDEN"));
	d["stealthed"] = o->testStatus((unsigned)CombatNames::status("STEALTHED"));
	const StealthUpdate *st = StealthUpdate::of(*o);
	d["ring"] = st ? st->ringWorn() : false;
	d["disguise"] = st && st->disguiseTemplate() ? String(st->disguiseTemplate()->getName().c_str()) : String();
	d["disguise_player"] = st ? (int64_t)st->disguisePlayerIndex() : (int64_t)-1;
	d["disguise_shown"] = st ? st->disguiseShown() : false;
	return d;
}
