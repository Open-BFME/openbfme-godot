// OpenBFME tests for Port 12 / issue #67: RotWK's footprint edge margin, supply geometry border, and sample-cell INERT condition.
// Everything is synthetic; no retail game assets are required. GPL-3.0.

#include "doctest.h"

#include "Common/StateHash.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "MoveTestUtil.h"

using namespace movetest;

namespace
{
const char kPort12Objects[] =
	"Object Port12PlainBuild\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"End\n"
	"Object Port12SupplySensitive\n"
	"  KindOf = STRUCTURE IMMOBILE CANNOT_BUILD_NEAR_SUPPLIES\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"End\n"
	"Object Port12SupplySource\n"
	"  KindOf = STRUCTURE IMMOBILE SUPPLY_SOURCE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"End\n"
	"Object Port12InertObstacle\n"
	"  KindOf = STRUCTURE IMMOBILE INERT\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"End\n"
	"Object Port12HeroObstacle\n"
	"  KindOf = STRUCTURE IMMOBILE HERO\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"End\n"
	"Object Port12Dozer\n"
	"  KindOf = DOZER\n"
	"End\n";

const char kBuildRules[] =
	"GameData\n"
	"  ForceModelsToFollowTimeOfDay = Yes\n"
	"  ForceModelsToFollowWeather = No\n"
	"  UnitDamagedThreshold = 0.5\n"
	"  UnitReallyDamagedThreshold = 0.25\n"
	"  DefaultStartingCash = 700\n"
	"  AllowedHeightVariationForBuilding = 12.5\n"
	"  SupplyBuildBorder = 20.0\n"
	"  MaxLineBuildObjects = 50\n"
	"  MinDistFromEdgeOfMapForBuild = 30.0\n"
	"End\n";

struct Port12World : MoveWorld
{
	WorldHeightMap heightMap;
	MapChunks chunks;
	TerrainLogic boundaryTerrain;

	Port12World() : MoveWorld(200, 200, kPort12Objects)
	{
		heightMap.m_width = 200;
		heightMap.m_height = 200;
		heightMap.m_data.assign(200u * 200u, 0);
		heightMap.m_boundaries = { ICoord2D{ 100, 100 }, ICoord2D{ 200, 200 } };
		boundaryTerrain.init(heightMap, chunks);
		logic->setTerrain(&boundaryTerrain);
		logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);
		logic->settings().buildRulesLoaded = true;
		logic->settings().allowedHeightVariationForBuilding = 100.0f;
		logic->settings().supplyBuildBorder = 20.0f;
		logic->settings().minDistFromEdgeOfMapForBuild = 30.0f;
		buildMap();
	}

	~Port12World()
	{
		// Object teardown calls back into the pathfinder and terrain; do it while both fixtures are still alive.
		logic->reset();
		ai.reset();
		logic->setTerrain(nullptr);
	}

	const ThingTemplate &thing(const char *name)
	{
		const ThingTemplate *tt = w.get(name);
		REQUIRE_MESSAGE(tt != nullptr, "missing synthetic template " << name);
		return *tt;
	}

	Object *spawnPathObject(const char *name, float x, float y)
	{
		Object *obj = spawn(name, x, y);
		logic->partition().update();
		ai->addObjectToPathfindMap(*obj);
		return obj;
	}

	LegalBuildCode legal(const char *templateName, float x, float y, unsigned options = LLF_TERRAIN_RESTRICTIONS, Object *builder = nullptr)
	{
		return BuildPlacement::isLocationLegalToBuild(*logic, Coord3D{ x, y, 0.0f }, thing(templateName), 0.0f, options, builder, nullptr);
	}
};
} // namespace

TEST_CASE("PORT-12: parse MinDistFromEdgeOfMapForBuild and require it with the other build rules")
{
	GameLogicSettings loaded;
	std::string error;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(kBuildRules, loaded, &error), error);
	CHECK(loaded.minDistFromEdgeOfMapForBuild == 30.0f);
	CHECK(loaded.supplyBuildBorder == 20.0f);
	CHECK(loaded.allowedHeightVariationForBuilding == 12.5f);
	CHECK(loaded.maxLineBuildObjects == 50);
	CHECK(loaded.buildRulesLoaded);

	GameLogicSettings missing;
	const std::string withoutMargin =
		"GameData\n"
		"  ForceModelsToFollowTimeOfDay = Yes\n"
		"  ForceModelsToFollowWeather = No\n"
		"  UnitDamagedThreshold = 0.5\n"
		"  UnitReallyDamagedThreshold = 0.25\n"
		"  DefaultStartingCash = 700\n"
		"  AllowedHeightVariationForBuilding = 12.5\n"
		"  SupplyBuildBorder = 20.0\n"
		"  MaxLineBuildObjects = 50\n"
		"End\n";
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(withoutMargin, missing, &error), error);
	CHECK_FALSE(missing.buildRulesLoaded); // do not silently assume a missing edge margin is zero

	// review lane PRTRLW (Sol note): GameData rows are additive, so a later scan without the margin row keeps an earlier complete scan's readiness
	GameLogicSettings twice;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(kBuildRules, twice, &error), error);
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(withoutMargin, twice, &error), error);
	CHECK(twice.buildRulesLoaded);
	CHECK(twice.minDistFromEdgeOfMapForBuild == 30.0f);

	GameLogicSettings laterAIData;
	laterAIData.buildRulesLoaded = true;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\nEnd\n", laterAIData, &error), error);
	CHECK(laterAIData.buildRulesLoaded); // a separate AIData pass must not erase GameData readiness
}

TEST_CASE("PORT-12: placement margins are always lockstep-hashed, including zero")
{
	const auto hashWithMargins = [](float edgeMargin, float supplyBorder) {
		GameLogicSettings settings;
		settings.minDistFromEdgeOfMapForBuild = edgeMargin;
		settings.supplyBuildBorder = supplyBorder;
		StateHasher hasher;
		settings.crc(hasher);
		return hasher.value();
	};
	const std::uint32_t zeroHash = hashWithMargins(0.0f, 0.0f);
	// Negative values are test-only sentinels: they prove neither field is conditionally omitted for non-positive values.
	CHECK(hashWithMargins(-1.0f, 0.0f) != zeroHash);
	CHECK(hashWithMargins(30.0f, 0.0f) != zeroHash);
	CHECK(hashWithMargins(0.0f, -1.0f) != zeroHash);
	CHECK(hashWithMargins(0.0f, 20.0f) != zeroHash);
	CHECK(hashWithMargins(30.0f, 20.0f) != hashWithMargins(30.0f, 0.0f));
}

TEST_CASE("PORT-12: edge margin checks footprint samples on every side of the active boundary, with equality allowed")
{
	Port12World world;
	const auto legalPlain = [&](float x, float y) { return world.legal("Port12PlainBuild", x, y); };

	// Boundary 0 is 1000 x 1000; the independent pathfind/grid boundary 1 is 2000 x 2000.
	// A 20-unit footprint at centre 39 samples x/y=29, outside the 30-unit margin; centre 40 reaches it exactly.
	CHECK(legalPlain(39.0f, 500.0f) == LBC_RESTRICTED_TERRAIN);
	CHECK(legalPlain(40.0f, 500.0f) == LBC_OK);
	CHECK(legalPlain(961.0f, 500.0f) == LBC_RESTRICTED_TERRAIN);
	CHECK(legalPlain(960.0f, 500.0f) == LBC_OK);
	CHECK(legalPlain(500.0f, 39.0f) == LBC_RESTRICTED_TERRAIN);
	CHECK(legalPlain(500.0f, 40.0f) == LBC_OK);
	CHECK(legalPlain(500.0f, 961.0f) == LBC_RESTRICTED_TERRAIN);
	CHECK(legalPlain(500.0f, 960.0f) == LBC_OK);
	CHECK(legalPlain(1500.0f, 500.0f) == LBC_RESTRICTED_TERRAIN); // outside active boundary 0, but inside max pathfind extent
}

TEST_CASE("PORT-12: a zero edge margin disables the playable-edge restriction without shrinking the pathfind extent")
{
	Port12World world;
	world.logic->settings().minDistFromEdgeOfMapForBuild = 0.0f;
	CHECK(world.legal("Port12PlainBuild", 20.0f, 500.0f) == LBC_OK);   // inside the active map, in the former margin band
	CHECK(world.legal("Port12PlainBuild", 1500.0f, 500.0f) == LBC_OK); // retained maximum pathfind/grid extent
	CHECK(world.legal("Port12PlainBuild", 2001.0f, 500.0f) == LBC_RESTRICTED_TERRAIN);
}

TEST_CASE("PORT-12: SupplyBuildBorder expands the closest supply geometry only for flagged builders and precedes the DOZER path gate")
{
	Port12World world;
	Object *supply = world.spawnPathObject("Port12SupplySource", 500.0f, 500.0f);
	REQUIRE(supply != nullptr);

	CHECK(world.legal("Port12SupplySensitive", 500.0f, 500.0f) == LBC_TOO_CLOSE_TO_SUPPLIES);
	CHECK(world.legal("Port12SupplySensitive", 545.0f, 500.0f) == LBC_OK); // within search radius, but beyond expanded-shape overlap
	CHECK(world.legal("Port12PlainBuild", 500.0f, 500.0f) == LBC_OK); // the same unflagged builder does not apply the supply rule

	Object *dozer = world.make("Port12Dozer", world.teamOf("Alice"));
	REQUIRE(dozer != nullptr);
	CHECK(world.legal("Port12SupplySensitive", 500.0f, 500.0f, LLF_TERRAIN_RESTRICTIONS | LLF_CLEAR_PATH, dozer) == LBC_TOO_CLOSE_TO_SUPPLIES);

	world.logic->settings().supplyBuildBorder = 0.0f;
	CHECK(world.legal("Port12SupplySensitive", 500.0f, 500.0f) == LBC_OK);
}

TEST_CASE("PORT-12: type-4 sample cells reject INERT obstacles but not HERO or ordinary obstacles")
{
	Port12World inertWorld;
	Object *inert = inertWorld.spawnPathObject("Port12InertObstacle", 500.0f, 500.0f);
	REQUIRE(inert != nullptr);
	const Coord3D inertSample{ 500.0f, 500.0f, 0.0f };
	const PathfindCell *inertCell = inertWorld.ai->pathfinder().getCell(LAYER_GROUND, &inertSample);
	REQUIRE(inertCell != nullptr);
	REQUIRE(inertCell->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(inertCell->getObstacleID() == inert->getID());
	CHECK(inertWorld.legal("Port12PlainBuild", 500.0f, 500.0f) == LBC_RESTRICTED_TERRAIN);

	Port12World missingWorld;
	const Coord3D missingSample{ 500.0f, 500.0f, 0.0f };
	PathfindCell *missingCell = missingWorld.ai->pathfinder().getCell(LAYER_GROUND, &missingSample);
	REQUIRE(missingCell != nullptr);
	missingCell->setType(PathfindCell::CELL_OBSTACLE);
	CHECK(missingCell->getObstacleID() == PATHFIND_INVALID_ID);
	CHECK(missingWorld.legal("Port12PlainBuild", 500.0f, 500.0f) == LBC_RESTRICTED_TERRAIN);

	// RW tests template +0x113 with mask 2 (INERT / KindOf 89), not HERO's mask 4 (KindOf 90).
	Port12World heroWorld;
	Object *hero = heroWorld.spawnPathObject("Port12HeroObstacle", 500.0f, 500.0f);
	REQUIRE(hero != nullptr);
	const Coord3D heroSample{ 500.0f, 500.0f, 0.0f };
	const PathfindCell *heroCell = heroWorld.ai->pathfinder().getCell(LAYER_GROUND, &heroSample);
	REQUIRE(heroCell != nullptr);
	REQUIRE(heroCell->getType() == PathfindCell::CELL_OBSTACLE);
	CHECK(heroCell->getObstacleID() == hero->getID());
	CHECK(heroWorld.legal("Port12PlainBuild", 500.0f, 500.0f) == LBC_OK);

	Port12World ordinaryWorld;
	Object *supply = ordinaryWorld.spawnPathObject("Port12SupplySource", 500.0f, 500.0f);
	REQUIRE(supply != nullptr);
	ordinaryWorld.logic->settings().supplyBuildBorder = 0.0f;
	CHECK(ordinaryWorld.legal("Port12PlainBuild", 500.0f, 500.0f) == LBC_OK);
}
