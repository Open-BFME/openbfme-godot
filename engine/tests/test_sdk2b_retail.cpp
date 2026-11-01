// OpenBFME unit tests: lane SDK-2b, a save of a modded game on the player's RotWK 2.01: the SDK sample mod (tools/sdk/samples/income-banner) keeps its
// paydays in sdk.state; a skirmish saved at frame 120 and loaded into a new game runs to frame 300 with the uninterrupted game's state hash every
// frame. GPL-3.0. Needs ROTWK_INSTALL and BFME2_INSTALL (SKIP otherwise); the mod is the committed sample.

#include "doctest.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "Common/ModMount.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RetailArchivePolicy.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string env(const char *name)
{
	const char *v = std::getenv(name);
	return v ? v : "";
}

std::string joined(const std::vector<std::string> &v)
{
	std::string out;
	for (const std::string &s : v)
		out += s + "\n";
	return out;
}

fs::path sample()
{
	// engine/tests/data -> the repository
	return fs::u8path(retailtest::dataDir()).parent_path().parent_path().parent_path() / "tools" / "sdk" / "samples" / "income-banner";
}

// the 2.01 archives with the sample mod over them, and the world a LiveGame needs (a mod changes the tree: not the shared pure mount)
struct ModdedWorld
{
	Win32BIGFileSystem fsys;
	ModMountReport report;
	std::unique_ptr<RetailObjectWorld> world;
	MapObjectOptions options;
	GameLogicSettings settings;
	std::vector<MapCacheEntry> maps;

	ModdedWorld()
	{
		std::vector<RetailInstall> installs(2);
		installs[0].label = "rotwk";
		installs[0].root = env("ROTWK_INSTALL");
		installs[1].label = "bfme2";
		installs[1].root = env("BFME2_INSTALL");
		std::string error;
		REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", installs[0].policy, &error), error);
		REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("bfme2-106", installs[1].policy, &error), error);
		RetailMountOptions mountOptions;
		mountOptions.md5CachePath = (fs::temp_directory_path() / "openbfme-sdk2b-retail-md5.tsv").u8string();
		const RetailMountReport retail = MountRetailArchives(fsys, installs, mountOptions);
		REQUIRE_MESSAGE(retail.ok, joined(retail.errors));
		ModOperand op;
		REQUIRE_MESSAGE(ParseModArgument(sample().u8string(), std::string(), op, &error), error);
		ModMountOptions modOptions;
		modOptions.rotwkInstallRoot = env("ROTWK_INSTALL");
		report = MountRetailMod(fsys, op, modOptions);
		REQUIRE_MESSAGE(report.ok, joined(report.errors));
		world = std::make_unique<RetailObjectWorld>(fsys);
		REQUIRE_MESSAGE(world->load(&error), error);
		REQUIRE_MESSAGE(MapObjectGameData::load(fsys, options, &error), error);
		REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(fsys, options, &error), error);
		REQUIRE_MESSAGE(MapCreationHooks::load(fsys, options.creationScripts, &error), error);
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(fsys, settings, &error), error);
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(fsys, maps, &error), error);
	}
};

int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
		if (store.getNthPlayerTemplate(i)->getName() == name)
			return i;
	return -1;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

// two Medium AIs (Men, Mordor) on Evendim
void startGame(ModdedWorld &w, Game &out)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 7;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = SLOT_MED_AI;
		sl.name = u"Computer";
		sl.playerTemplate = templateIndex(w.world->playerTemplates(), factions[i]);
		REQUIRE(sl.playerTemplate != -1);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, w.world->playerTemplates(), w.settings, w.maps, RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(w.fsys);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*w.world, w.fsys, *out.assets, w.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

void step(Game &g)
{
	REQUIRE(g.game->advance(0.2) == 1);
	g.game->drainFrames();
}

} // namespace

TEST_CASE("SDK-2b retail: the sample mod's skirmish saved at frame 120 and loaded into a new game runs to frame 300 as the uninterrupted game")
{
	if (env("ROTWK_INSTALL").empty() || env("BFME2_INSTALL").empty())
	{
		retailtest::printSkip("SDK-2b retail (a modded skirmish saved and loaded)");
		return;
	}
	ModdedWorld w;
	REQUIRE(!w.report.sdkSimFingerprint.empty());
	std::vector<std::uint8_t> saved;
	std::uint32_t savedHash = 0;
	std::vector<std::uint32_t> hashes;
	std::vector<std::string> reportsA;
	{
		Game a;
		startGame(w, a);
		while (a.game->logic().getFrame() < 120)
			step(a);
		std::string error;
		REQUIRE_MESSAGE(a.game->logic().saveSnapshot(saved, &error), error);
		savedHash = a.game->logic().computeStateHash();
		while (a.game->logic().getFrame() < 300)
		{
			step(a);
			hashes.push_back(a.game->logic().computeStateHash());
		}
		reportsA = a.game->takeSdkReports();
	}
	// one live game per world (S-3808): the second game starts after the first is gone, from the same START, and loads the save
	Game b;
	startGame(w, b);
	std::string error;
	REQUIRE_MESSAGE(b.game->loadSnapshot(saved, &error, nullptr), error);
	CHECK(b.game->logic().getFrame() == 120);
	CHECK(b.game->logic().computeStateHash() == savedHash);
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		step(b);
		REQUIRE(b.game->logic().computeStateHash() == hashes[i]);
	}
	const std::vector<std::string> reportsB = b.game->takeSdkReports();
	MESSAGE("A:\n" << joined(reportsA) << "B:\n" << joined(reportsB));
	CHECK(reportsA.empty());
	// paydays at frames 50 and 100 before the save
	CHECK(std::find(reportsB.begin(), reportsB.end(), "SDK Income Banner: loaded with 2 paydays so far") != reportsB.end());
}
