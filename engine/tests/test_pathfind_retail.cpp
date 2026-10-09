// OpenBFME retail tests for the pathfinder grid (lane PATH-1). Run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise
// they print SKIP). They mount the pure RotWK 2.01 + BFME2 1.06 archives and build the pathfind grid of every one of the 181 retail
// maps. The expected cell counts are NEVER this engine's own output: tests/data/pathfind-survey.json is written by
// tools/path/path_oracle.py, which reads the map files itself (BIG reader, RefPack decoder and chunk walker of
// tools/maps/oracle, its own leaf parsers and classifier) and shares no code with the engine.

#include "doctest.h"

#include "Common/MiniJson.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MapPathfindObjects.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"

#include <chrono>
#include <cstdio>
#include <map>

using namespace mapcorpus;

namespace
{
struct NullWorld : PathfindWorld
{
	PathfindObject *findObjectByID(PathfindObjectID) const override { return nullptr; }
	unsigned getFrame() const override { return 100; }
};
} // namespace

TEST_CASE("pathfind retail: the configuration values come from the mounted INI files")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("pathfind retail config");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	PathfindConfig c;
	std::string err;
	REQUIRE_MESSAGE(PathfindConfigLoader::load(*mount->fs, c, &err), err);
	// data\ini\default\aidata.ini: WadeWaterDepth 5.0, DeepWaterDepth 6.0; data\ini\gamedata.ini (INI.big and _patch201ini.big agree):
	// MaxPathfindCellsPerFrame 4000, MaxCellsAdjustDestination 400, MaxCellsAdjustHordeMeleeDestination 200, MaxCellsPatchPath 2000,
	// MaxCellsFindPathLimit 15000 (values read from the archives by tools/path, not from this engine)
	CHECK(c.wadeWaterDepth == 5.0f);
	CHECK(c.deepWaterDepth == 6.0f);
	CHECK(c.cellsPerFrame == 4000);
	CHECK(c.adjustDestinationLimit == 400);
	CHECK(c.adjustHordeMeleeLimit == 200);
	CHECK(c.patchPathLimit == 2000);
	CHECK(c.findPathLimit == 15000);
	CHECK(c.findAttackPathLimit == 2500);
	CHECK(c.adjustToMeleeLimit == 400);      // lane PHYS-1: MaxCellsAdjustToMeleeDestination (GameData +0x1200)
	CHECK(c.findMeleeEngagementLimit == 50); // lane PHYS-1: MaxCellsFindMeleeEngagementLocation (GameData +0x11EC) // lane PHYS-1: MaxCellsFindAttackPath (GameData +0x1214)
	CHECK(c.meleeApproachDist == 48.0f);
	CHECK(c.meleeApproachTolerance == 20.0f); // lane PHYS-1: AIData MeleeApproachTolerance (+0x90)  // lane PHYS-1: AIData MeleeApproachDist (+0x94)
	CHECK(c.hordesWaitForHordes);          // lane PHYS-1: AIData HordesWaitForHordes (+0xB9), default/aidata.ini Yes
	CHECK(c.castleSiegeStandBackDistance == 500.0f); // lane PHYS-1 round 6: AIData CastleSiegeStandBackDistance (+0xD4)
	CHECK(c.adjustToPossibleLimit == 400);
	CHECK(c.examineTowardsGoalLimit == 25000);
	CHECK(c.zoneBlockSize == 16);
	CHECK(c.slopeLimits[0] == 0.0f);
}

TEST_CASE("pathfind retail: the terrain grid of all 181 maps matches the independent classifier")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("pathfind retail grids");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	PathfindConfig config;
	std::string err;
	REQUIRE_MESSAGE(PathfindConfigLoader::load(*mount->fs, config, &err), err);

	std::vector<unsigned char> surveyBytes;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/pathfind-survey.json", surveyBytes, &err), err);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(surveyBytes.begin(), surveyBytes.end()), survey, &err), err);
	CHECK(J(survey, "wade_water_depth").number == (double)config.wadeWaterDepth);
	CHECK(J(survey, "deep_water_depth").number == (double)config.deepWaterDepth);
	const JsonValue &surveyMaps = J(survey, "maps");

	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "*.map", list, true);
	REQUIRE(list.size() == 181);

	NullWorld world;
	size_t totals[8] = {};
	size_t cells = 0, pinched = 0, plane18 = 0, plane21 = 0;
	size_t largest = 0;
	std::string largestMap;
	double seconds = 0.0;
	std::vector<std::uint8_t> bytes;
	for (const std::string &path : list)
	{
		const std::string key = slashes(lowerStr(path), '/');
		INFO("map " << key);
		const JsonValue *sv = surveyMaps.get(key);
		REQUIRE_MESSAGE(sv != nullptr, "survey has no entry for " << key);
		REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &err), err);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, m, &err), err);
		TerrainLogic terrain;
		std::vector<std::string> problems;
		terrain.init(m.heightMap, m.chunks, &problems);
		TerrainPathfindSource source(terrain, m.heightMap, m.chunks);

		const auto t0 = std::chrono::steady_clock::now();
		Pathfinder pf(config, &world);
		pf.newMap(source);
		seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		REQUIRE(pf.isMapReady());
		CHECK(!pf.pool().exhaustedOnce());
		const PathfindGridStats s = pf.gridStats();
		CHECK(s.width == (int)N(J(*sv, "width")));
		CHECK(s.height == (int)N(J(*sv, "height")));
		static const char *names[8] = { "clear", "water", "cliff", "rubble", "obstacle", "bridge_impassable", "impassable", "deep_water" };
		for (int t = 0; t < 8; ++t)
		{
			CHECK_MESSAGE(s.types[t] == (int)N(J(*sv, names[t])), names[t]);
			totals[t] += (size_t)s.types[t];
		}
		CHECK(s.pinched == (int)N(J(*sv, "pinched")));
		CHECK(s.impassableToPlayers == (int)N(J(*sv, "impassable_to_players")));
		CHECK(s.extraPass == (int)N(J(*sv, "extra_pass")));
		cells += (size_t)s.width * (size_t)s.height;
		pinched += (size_t)s.pinched;
		plane18 += (size_t)s.impassableToPlayers;
		plane21 += (size_t)s.extraPass;
		if ((size_t)s.width * (size_t)s.height > largest)
		{
			largest = (size_t)s.width * (size_t)s.height;
			largestMap = key;
		}
	}
	std::printf("  info: pathfind grids of 181 maps: %zu cells, clear %zu water %zu cliff %zu deep %zu obstacle %zu, pinched %zu, bit18 %zu bit21 %zu, build %.2f s, largest %s (%zu cells)\n",
		cells, totals[0], totals[1], totals[2], totals[7], totals[4], pinched, plane18, plane21, seconds, largestMap.c_str(), largest);
}
