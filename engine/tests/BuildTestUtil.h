// OpenBFME unit tests. GPL-3.0.
// Shared by the BUILD-1 retail tests: a skirmish started from a NewGameMessage built in code (the lobby's clicks are START-1's own test), the retail object world and
// the live game kept alive for the checks.  SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset (StartTestUtil).

#pragma once

#include "StartGolden.h"
#include "StartTestUtil.h"

#include "Common/ArchiveFileSystem.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>

namespace buildtest
{
inline int templateIndex(starttest::Shared &s, const std::string &name)
{
	const PlayerTemplateStore &store = s.world->playerTemplates();
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i) && store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

struct Game
{
	NewGameStart start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> live;
	Game() : start(RandomAlgorithm::ZH_CarryChain) {}
	~Game()
	{
		if (live)
		{
			live->logic().reset();
		}
	}
};

// the lobby's result for slot 0 = `faction0` (the human) and slot 1 = `faction1` (an Easy AI) on Evendim, start positions 0 and 4, colours 0 and 1
// lane BUILD-3: `mapName` / `start0` / `start1` choose another map and start positions (the defaults are BUILD-1's Evendim game); `allied` puts both on team 0
inline bool startGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, std::uint32_t seed, Game &out, std::string *error,
                      const char *mapName = "maps/map mp evendim/map mp evendim.map", int start0 = 0, int start1 = 4, bool allied = false)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	const int t0 = templateIndex(s, faction0), t1 = templateIndex(s, faction1);
	if (t0 < 0 || t1 < 0)
	{
		*error = "no player template " + faction0 + " / " + faction1;
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = t0;
	a.color = 0;
	a.start = start0;
	b.tmpl = t1;
	b.color = 1;
	b.start = start1;
	if (allied)
	{
		a.team = 0;
		b.team = 0;
	}
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.mapName = mapName;
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

// lane BUILD-2, an independent model of the dozer's build accumulation (RW 0x88DE72..0x88DE99: cvtsi2ss T, divss 100 / T, addss into the percent, comiss >= 100): the frames
// of work a structure with calcTimeToBuild T needs, starting at percent 0 (binary32 arithmetic, no FMA: the test target is built with -ffp-contract=off)
inline int framesToAccumulate(int buildFrames)
{
	if (buildFrames <= 0)
	{
		return 1;
	}
	volatile float step = 100.0f / (float)buildFrames;
	volatile float percent = 0.0f;
	int n = 0;
	while (percent < 100.0f && n < 1000000)
	{
		percent = percent + step;
		++n;
	}
	return n;
}
} // namespace buildtest
