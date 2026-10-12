// OpenBFME unit tests. GPL-3.0.
// Lane FACT-A: what a player of a faction owns right after a real skirmish start (START-1's new-game path: NewGame::prepareNewGame, the starting base
// placed and its castle unpacked into the citadel and the expansion pads), the seeds of the FACT-A content walk. One started game per faction, cached.

#include "FactAStart.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include "doctest.h"

#include <map>
#include <memory>
#include <set>

using namespace starttest;

std::vector<std::string> factaStartTemplates(const std::string &faction)
{
	static std::map<std::string, std::vector<std::string>> cache;
	auto it = cache.find(faction);
	if (it != cache.end())
	{
		return it->second;
	}
	std::vector<std::string> out;
	Shared *s = starttest::shared();
	if (!s || !s->world)
	{
		return out;
	}
	auto context = s->world->enterContext();
	int templateIndex = -1, enemyIndex = -1;
	for (int i = 0; i < s->world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s->world->playerTemplates().getNthPlayerTemplate(i);
		templateIndex = pt && pt->getName() == faction ? i : templateIndex;
		enemyIndex = pt && pt->getName() == "FactionMordor" ? i : enemyIndex;
	}
	REQUIRE(templateIndex >= 0);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = 7;
	m.game.startingCash = 1000;
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = i == 0 ? u"Host" : u"Easy";
		sl.accepted = true;
		sl.playerTemplate = i == 0 ? templateIndex : enemyIndex;
		sl.startPos = i;
		sl.color = i;
	}
	std::string error;
	std::vector<MapCacheEntry> mapCache;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, mapCache, &error), error);
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, mapCache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	o.mapScripts = 0;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	for (int i = 0; i < 10; ++i)
	{
		game.advance(0.2);
	}
	std::set<std::string> seen;
	for (Object *obj = game.logic().getFirstObject(); obj; obj = obj->getNextObject())
	{
		Player *owner = obj->getControllingPlayer();
		const PlayerTemplate *pt = owner ? owner->getPlayerTemplate() : nullptr;
		if (pt && pt->getName() == faction && owner->getPlayerType() == PLAYER_HUMAN && !obj->getContainedBy()
			&& seen.insert(obj->getTemplate()->getName()).second)
		{
			out.push_back(obj->getTemplate()->getName());
		}
	}
	game.logic().reset();
	cache[faction] = out;
	return out;
}
