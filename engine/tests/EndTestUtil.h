// OpenBFME unit tests. GPL-3.0.
// The helpers of the end-of-game tests (lanes END-1 / END-2): two human slots on Evendim, the peers of one game (a LiveGame each, fed the same inputs) with
// the client's end sequence, and the deterministic test inputs that destroy a player.

#pragma once

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace endtest
{
inline int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// two human slots on Evendim (start positions 0 and 1, enemies)
inline NewGameMessage humanVersus(const starttest::Shared &s, const std::string &a, const std::string &b, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const std::string factions[2] = { a, b };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Gimli" : u"Gothmog";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	return m;
}

inline const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

inline Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

// One peer of a game: its LiveGame (single-threaded), the end sequence the client runs over the presented frames.
struct Peer
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	EndGameController end;
	std::vector<EndGameRequest> requests;
	unsigned lastFrame = 0;

	void load(starttest::Shared &s, const NewGameMessage &message, int localSlot)
	{
		std::string error;
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
		start.localSlot = localSlot;
		source.reset(new ArchiveW3DFileSource(*s.mount->fs));
		assets.reset(new WW3DAssetManager(*source));
		game.reset(new LiveGame(*s.world, *s.mount->fs, *assets, s.options));
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
		end.start(game->logic().economy().context().gameMode, game->logic().economy().context().gameKind);
		REQUIRE_MESSAGE(end.scripts().load(*s.mount->fs, "Multiplayer_Human", &error), error);
		end.setScriptsLoaded(true);
	}
	// one logic frame (the 5 Hz step), then the client's end sequence over the presented snapshot; `nowMs` is the client clock
	void step(double nowMs)
	{
		game->advance(0.2);
		const std::shared_ptr<const LogicSnapshot> snap = game->presentedSnapshot();
		const EndGameView *view = snap && snap->endGame && snap->frame != lastFrame ? snap->endGame.get() : nullptr;
		if (view)
		{
			lastFrame = snap->frame;
		}
		end.update(view, lastFrame, nowMs);
		for (EndGameRequest &r : end.takeRequests())
		{
			requests.push_back(r);
		}
	}
	GameLogic &logic() { return game->logic(); }
	bool requested(int kind, const std::string &text) const
	{
		return std::any_of(requests.begin(), requests.end(), [&](const EndGameRequest &r) { return r.kind == kind && (text.empty() || r.text == text); });
	}
};

// every object of `victim` takes an unresistable hit of `source` (the kill credit is the source's: Object::scoreTheKill); the order is the object list's
inline int destroyAll(GameLogic &logic, Player &victim, Object &source)
{
	std::vector<Object *> list;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &victim && !o->isEffectivelyDead() && !o->isDestroyed() && o->getBodyModule())
		{
			list.push_back(o);
		}
	}
	for (Object *o : list)
	{
		if (o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_deathType = DEATH_NORMAL;
		info.m_input.m_sourceID = source.getID();
		info.m_input.m_amount = 1.0e6f;
		o->getBodyModule()->attemptDamage(info);
	}
	return (int)list.size();
}

inline Object *firstObjectOf(GameLogic &logic, const Player &p, bool structure)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &p && !o->isEffectivelyDead() && o->isKindOfName("STRUCTURE") == structure)
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace endtest
