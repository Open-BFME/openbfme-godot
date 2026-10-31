// OpenBFME unit tests, lane SIEGE-1: the shared retail fixture of the siege attack tests. GPL-3.0.
//
// A skirmish on a fortress map (Helm's Deep or Minas Tirith): the computer player owns the fortress at start 0 (Men), the human is Mordor at start 1; the
// human's map is revealed (an order may not target a fogged object, S-565). siegeAttack makes one unit `d` outside a target (toward the human's start), orders
// it to attack the target and runs `frames` frames, recording the first damage. SKIPs loudly without the installs (OPENBFME_REQUIRE_START).

#pragma once

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace siege1test
{
inline int templateIndex(starttest::Shared &s, const std::string &name)
{
	for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// slot 0: the computer at start 0 (the fortress, Men), slot 1: the human at start 1 (Mordor)
// `aiDefender` false (test_siege1_gate): the fortress side is a second human slot that never gives an order, so no skirmish AI moves its porters or builds:
// the gate runs depend only on the siege weapon and the structures, not on the AI's own draws of the game's random numbers or its builders' paths
inline NewGameMessage fortressMessage(starttest::Shared &s, const std::string &map, bool aiDefender = true)
{
	NewGameMessage m;
	m.game.mapName = map;
	m.game.seed = 9;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 && aiDefender ? SLOT_EASY_AI : SLOT_PLAYER;
		sl.name = i == 0 ? (aiDefender ? u"Easy" : u"Defender") : u"Host";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(s, factions[i]);
		REQUIRE(sl.playerTemplate >= 0); // tolerance: the index of a found template (-1: not found)
		sl.startPos = i;
		sl.color = i;
	}
	return m;
}

struct FortressGame
{
	starttest::Shared &s;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	FortressGame(starttest::Shared &sh, const std::string &map, bool aiDefender = true) : s(sh), source(*sh.mount->fs), assets(source)
	{
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		REQUIRE_MESSAGE(NewGame::prepareNewGame(fortressMessage(s, map, aiDefender), s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			game->advance(0.2);
		}
	}
	Object *named(const std::string &name)
	{
		for (Object *o = game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	}
	// the player of the faction (the attacker of test_siege1_gate is FactionMordor's: with aiDefender false both slots are human)
	Player *ofFaction(const std::string &faction)
	{
		for (int i = 0; i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->getName() == faction)
			{
				return p;
			}
		}
		return nullptr;
	}
	Player *human()
	{
		for (int i = 0; i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			if (p && p->getPlayerType() == PLAYER_HUMAN)
			{
				return p;
			}
		}
		return nullptr;
	}
};

inline float healthOf(const Object *o)
{
	return o && o->getBodyModule() ? o->getBodyModule()->getHealth() : -1.0f;
}

inline float dist2(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

struct AttackResult
{
	bool ordered = false;
	int firstHit = -1;        // the frame of the first damage (or the target's death), -1: none
	int slotAtHit = -1;       // the attacker's current weapon slot then
	float distAtHit = -1.0f;  // the attacker's 2D distance from the target then
	float healthBefore = 0.0f, healthAfterFirst = 0.0f, healthAfter = 0.0f;
	bool dead = false;
	int deathFrame = -1;
	std::string target, victimAfter;
};

// `targetName` empty: the map object named `gateName`; else the WALK_ON_TOP_OF_WALL piece nearest it that is no gate. `aiDefender`: the fortress side's
// slot (SIEGEFIX-1's default false: a human slot that gives no order; test_siege1_attack.cpp keeps #64's Easy AI defender, true)
inline AttackResult siegeAttack(starttest::Shared &s, const std::string &map, const std::string &gateName, const std::string &unit, bool wall, float d, int frames,
								bool aiDefender = false)
{
	AttackResult res;
	FortressGame g(s, map, aiDefender);
	g.frames(2);
	Object *gate = g.named(gateName);
	REQUIRE(gate);
	Player *me = g.ofFaction("FactionMordor");
	REQUIRE(me);
	g.game->shroud().revealMapForPlayerPermanently(me->getPlayerIndex());
	const Waypoint *w = g.game->logic().terrain()->findWaypointByName("Player_2_Start");
	REQUIRE(w);
	Object *target = gate;
	if (wall)
	{
		float best = 1e9f;
		for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o != gate && o->isKindOfName("WALK_ON_TOP_OF_WALL") && !o->isKindOfName("BLOCKING_GATE") && o->getBodyModule())
			{
				const float dd = dist2(*o->getPosition(), *gate->getPosition());
				if (dd < best && dd > 50.0f)
				{
					best = dd;
					target = o;
				}
			}
		}
	}
	res.target = target->getTemplate()->getName();
	const Coord3D tp = *target->getPosition();
	float dx = w->location.x - tp.x, dy = w->location.y - tp.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	dx /= len;
	dy /= len;
	std::string error;
	Object *o = g.game->createObject(unit, me->getPlayerIndex(), Coord3D{ tp.x + dx * d, tp.y + dy * d, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(o, error);
	g.frames(5);
	AIUpdateInterface *ai = o->getAIUpdateInterface();
	res.healthBefore = healthOf(target);
	res.ordered = ai && ai->aiAttackObject(target, CMD_FROM_PLAYER);
	const ObjectID tid = target->getID();
	for (int f = 0; f < frames; ++f)
	{
		g.frames(1);
		Object *t = g.game->logic().findObjectByID(tid);
		if (res.firstHit < 0 && (!t || healthOf(t) < res.healthBefore))
		{
			res.firstHit = f + 1;
			res.healthAfterFirst = t ? healthOf(t) : 0.0f;
			res.distAtHit = dist2(*o->getPosition(), tp);
			if (o->getWeapons())
			{
				o->getWeapons()->currentWeapon(&res.slotAtHit);
			}
		}
		if (res.deathFrame < 0 && (!t || t->isEffectivelyDead()))
		{
			res.deathFrame = f + 1;
		}
	}
	Object *t = g.game->logic().findObjectByID(tid);
	res.healthAfter = t ? healthOf(t) : 0.0f;
	res.dead = !t || t->isEffectivelyDead();
	res.victimAfter = ai && ai->currentVictim() ? ai->currentVictim()->getTemplate()->getName() : std::string("none");
	return res;
}
} // namespace siege1test
