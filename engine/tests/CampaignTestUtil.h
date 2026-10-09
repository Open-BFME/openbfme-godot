// OpenBFME unit tests: the campaign mission driver shared by the SCRIPT-2 / SCRIPT-3 mission tests (a LiveGame on a retail campaign map, stepped
// a logic frame at a time; helpers where a player would fight or walk). GPL-3.0.
#pragma once

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace campaigntest
{

struct Mission
{
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	std::vector<ScriptClientRequest> requests;

	void load(starttest::Shared &s, const std::string &mapName = "map ang angmar")
	{
		source.reset(new ArchiveW3DFileSource(*s.mount->fs));
		assets.reset(new WW3DAssetManager(*source));
		game.reset(new LiveGame(*s.world, *s.mount->fs, *assets, s.options));
		LiveGame::Options o;
		o.mapName = mapName;
		o.campaign = true;
		std::string error;
		REQUIRE_MESSAGE(game->load(o, &error), error);
		take();
	}
	void take()
	{
		for (ScriptClientRequest &r : game->takeScriptRequests())
		{
			requests.push_back(std::move(r));
		}
	}
	GameLogic &logic() { return game->logic(); }
	ScriptEngine &engine() { return logic().scriptEngine(); }
	unsigned frame() { return logic().getFrame(); }
	void step(int frames = 1)
	{
		for (int i = 0; i < frames; ++i)
		{
			game->advance(0.2);
			take();
		}
	}
	// steps until `done` holds (true) or `maxFrames` passed (false)
	bool until(const std::function<bool()> &done, int maxFrames)
	{
		for (int i = 0; i < maxFrames; ++i)
		{
			if (done())
			{
				return true;
			}
			step();
		}
		return done();
	}
	bool flag(const std::string &q)
	{
		const bool *f = engine().findFlag(q);
		return f && *f;
	}
	Object *unit(const std::string &n) { return engine().getUnitNamed(n); }
	Coord3D centreOf(const std::string &trigger)
	{
		const TriggerArea *t = engine().findTrigger(trigger);
		REQUIRE_MESSAGE(t != nullptr, trigger);
		Coord3D c{ 0.0f, 0.0f, 0.0f };
		for (const Point2F &p : t->points)
		{
			c.x += p.x / (float)t->points.size();
			c.y += p.y / (float)t->points.size();
		}
		c.z = logic().getGroundHeight(c.x, c.y);
		return c;
	}
	Coord3D waypoint(const std::string &n)
	{
		const Waypoint *w = logic().terrain()->findWaypointByName(n);
		REQUIRE_MESSAGE(w != nullptr, n);
		return w->location;
	}
	void place(Object &o, Coord3D p)
	{
		p.z = logic().getGroundHeight(p.x, p.y);
		o.setPosition(&p);
	}
	// kills an object (a horde through its members: its ImmortalBody does not die, the horde goes with its last member)
	void kill(Object &o)
	{
		if (o.isEffectivelyDead() || o.isDestroyed())
		{
			return;
		}
		if (o.isKindOfName("HORDE") && o.getContain() && o.getContain()->getContainedItemsList())
		{
			const std::vector<Object *> members(o.getContain()->getContainedItemsList()->begin(), o.getContain()->getContainedItemsList()->end());
			for (Object *m : members)
			{
				kill(*m);
			}
			return;
		}
		if (o.getBodyModule())
		{
			o.getBodyModule()->setIndestructible(false);
		}
		o.kill(DEATH_NORMAL);
	}
	void killNamed(const std::string &n)
	{
		if (Object *o = unit(n))
		{
			kill(*o);
		}
	}
	void killTeam(const std::string &n)
	{
		Team *t = ScriptConditions::team(engine(), n);
		REQUIRE_MESSAGE(t != nullptr, n);
		for (Object *o : ScriptConditions::members(*t))
		{
			kill(*o);
		}
	}
	bool requested(const std::string &action, const std::string &param0 = std::string()) const
	{
		for (const ScriptClientRequest &r : requests)
		{
			if (r.action == action && (param0.empty() || (!r.params.empty() && r.params[0].stringValue == param0)))
			{
				return true;
			}
		}
		return false;
	}
};


} // namespace campaigntest
