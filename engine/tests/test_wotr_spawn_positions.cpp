// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"

#include "GameLogic/NewGame/ArmySpawnPositions.h"
#include "RetailTestMount.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <map>

namespace
{
struct SpawnMap
{
	std::map<std::string, Coord3D> points;
	ArmySpawnPositions::Lookup lookup() const
	{
		return [this](const std::string &name) -> const Coord3D * {
			auto it = points.find(name);
			return it == points.end() ? nullptr : &it->second;
		};
	}
	void region(const std::string &name, const Coord3D &spawn, const Coord3D &gather)
	{
		points["WOTRSpawnPoint_" + name + "_1"] = spawn;
		points["WOTRGatherPoint_" + name + "_1"] = gather;
	}
};
} // namespace

// RW 0x781A85 / 0x781B17 / 0x781C9F; BFME2 ArmyPlacer.cpp RVAs
// 0x37F59B / 0x37F62D / 0x37F7B6. A missing walk-on gather is NOT a
// start-position fallback: the whole pair fails and selection continues.
TEST_CASE("wotr battle placement: waypoint pairs require a gather; a start without a rally offsets its spawn")
{
	SpawnMap map;
	map.points["WOTRSpawnPoint_A_1"] = { 100, 200, 3 };
	map.points["Player_2_Start"] = { 400, 500, 6 };
	ArmySpawnPositions::Positions out;
	CHECK_FALSE(ArmySpawnPositions::walkOn(map.lookup(), "A", out));
	REQUIRE(ArmySpawnPositions::start(map.lookup(), 1, out));
	CHECK(out.spawn.x == 410);
	CHECK(out.spawn.y == 500);
	CHECK(out.spawn.z == 6);
	CHECK(out.gather.x == 400);
	CHECK(out.gather.y == 500);
	CHECK(out.gather.z == 6);
	const auto names = out.names;
	out = {};
	REQUIRE(ArmySpawnPositions::namedPair(map.lookup(), names, out));
	CHECK(out.spawn.x == 410); // same choice after a reinforcement snapshot round-trip
	CHECK(out.gather.x == 400);
	map.points["Player_2_Start_Rally"] = { 900, 700, 8 };
	REQUIRE(ArmySpawnPositions::start(map.lookup(), 1, out));
	CHECK(out.spawn.x == 400);
	CHECK(out.gather.x == 900);
	CHECK(out.gather.z == 8);
}

// RW 0x7824FD / 0x781D95; BFME2 ArmyPlacer.cpp RVA 0x380014.
TEST_CASE("wotr battle placement: neutral armies use the farthest gather pair and the last equal pair in connection order")
{
	SpawnMap map;
	// Spawn distances would choose A/B; gather distances choose A/C.
	map.region("A", { 0, 0, 0 }, { 0, 0, 0 });
	map.region("B", { 10000, 0, 0 }, { 5, 0, 0 });
	map.region("C", { 1, 0, 0 }, { 10, 0, 0 });
	const std::vector<std::string> connections{ "Missing", "A", "B", "C" };
	ArmySpawnPositions::Positions out;
	REQUIRE(ArmySpawnPositions::farthestPair(map.lookup(), connections, 0, out));
	CHECK(out.names.first == "WOTRSpawnPoint_A_1");
	REQUIRE(ArmySpawnPositions::farthestPair(map.lookup(), connections, 1, out));
	CHECK(out.names.first == "WOTRSpawnPoint_C_1");
	CHECK_FALSE(ArmySpawnPositions::farthestPair(map.lookup(), connections, 2, out));
	map.region("D", { 7, 0, 0 }, { 10, 0, 0 });
	REQUIRE(ArmySpawnPositions::farthestPair(map.lookup(), { "A", "C", "D" }, 1, out));
	CHECK(out.names.first == "WOTRSpawnPoint_D_1");
	map.region("A", { 0, 0, 0 }, { 10, 0, 0 });
	REQUIRE(ArmySpawnPositions::farthestPair(map.lookup(), { "A", "C", "D" }, 0, out));
	CHECK(out.names.first == "WOTRSpawnPoint_C_1"); // the final zero-distance pair C/D is valid
}

// RW 0x781E6E; BFME2 ArmyPlacerFarthestSpawnPoint.cpp RVA 0x37F985.
TEST_CASE("wotr battle placement: hostile armies maximize gather distance from the defender rally; zero distance fails")
{
	SpawnMap map;
	map.points["Player_1_Start"] = { 0, 0, 0 };
	map.points["Player_1_Start_Rally"] = { 100, 0, 0 };
	map.region("A", { 1000, 0, 0 }, { 0, 0, 0 });
	map.region("B", { 0, 0, 0 }, { 100, 0, 0 });
	map.region("C", { 0, 0, 0 }, { 200, 0, 0 });
	ArmySpawnPositions::Positions out;
	REQUIRE(ArmySpawnPositions::farthestFromStart(map.lookup(), { "A", "B", "C" }, out));
	CHECK(out.names.first == "WOTRSpawnPoint_C_1"); // A/C equal; last wins
	CHECK_FALSE(ArmySpawnPositions::farthestFromStart(map.lookup(), { "B" }, out));
	map.points.erase("WOTRGatherPoint_C_1");
	REQUIRE(ArmySpawnPositions::farthestFromStart(map.lookup(), { "A", "B", "C" }, out));
	CHECK(out.names.first == "WOTRSpawnPoint_A_1");
}

// RotWK-specific retry RW 0x80F8CB..0x80F8FC: BFME2
// ArmySummarySpawnPositions.cpp RVA 0x40CCC6 falls directly to start 1.
TEST_CASE("wotr battle placement: a missing non-normal origin retries the owner-dependent choice before start two")
{
	using P = LivingWorldArmySpawnPlan;
	SpawnMap map;
	map.points["Player_1_Start"] = { 100, 0, 0 };
	map.points["Player_2_Start"] = { 900, 0, 0 };
	map.region("Distant", { 20, 0, 0 }, { 0, 0, 0 });
	map.region("DefenderReinforcements", { 30, 0, 0 }, { 40, 0, 0 });
	P plan;
	plan.method = P::WALK_ON;
	plan.originRegion = "Missing";
	plan.connections = { "Distant" };
	ArmySpawnPositions::Positions out;
	REQUIRE(ArmySpawnPositions::resolve(plan, true, map.lookup(), out));
	CHECK(out.names.first == "WOTRSpawnPoint_Distant_1");
	plan.friendlyOwner = true;
	REQUIRE(ArmySpawnPositions::resolve(plan, true, map.lookup(), out));
	CHECK(out.names.first == "WOTRSpawnPoint_DefenderReinforcements_1");
	map.points.erase("WOTRGatherPoint_DefenderReinforcements_1");
	REQUIRE(ArmySpawnPositions::resolve(plan, true, map.lookup(), out));
	CHECK(out.names.first == "Player_2_Start");
	CHECK(out.spawn.x == 910);
	CHECK(out.gather.x == 900);
}

// RW 0x80F907..0x80F9CB: initial placement moves walk-on armies to gather;
// start positions and reinforcements keep the original spawn coordinate.
TEST_CASE("wotr battle placement: an initial army uses gather while a reinforcement uses spawn")
{
	using P = LivingWorldArmySpawnPlan;
	SpawnMap map;
	map.region("Origin", { 100, 200, 7 }, { 100, 204, 7 });
	P plan;
	plan.method = P::WALK_ON;
	plan.originRegion = "Origin";
	ArmySpawnPositions::Positions out;
	REQUIRE(ArmySpawnPositions::resolve(plan, true, map.lookup(), out));
	CHECK(out.spawn.y == 200);
	CHECK(out.gather.y == 204);
	REQUIRE(ArmySpawnPositions::resolve(plan, false, map.lookup(), out));
	CHECK(out.spawn.x == 100);
	CHECK(out.spawn.y == 204);
	CHECK(out.spawn.z == 7);
	CHECK(out.gather.x == 100);
	CHECK(out.gather.z == 7);
	// Target's fast inverse square root is already pinned by its own tests;
	// this pins the complete PC24/SSE sequence for the four-unit y direction.
	CHECK(out.gather.y == 214.0f);
}

// Original RotWK MAP WOR Rohan waypoint coordinates were independently read
// with tools/maps/oracle/fullparse.py. This tests selectors against the loaded
// original map; the binary controls the selection rules, not these coordinates.
TEST_CASE("wotr battle placement retail: Rohan neutral sides use Helms Deep and Brown Lands; the hostile army uses Gap of Rohan")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("War of the Ring spawn selection on original Rohan data");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::string error;
	std::vector<std::uint8_t> bytes;
	const std::string path = "maps/map wor rohan/map wor rohan.map";
	REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &error), error);
	LoadedMap map;
	REQUIRE_MESSAGE(MapReader::load(bytes, path, MapReadOptions(), map, &error), error);
	TerrainLogic terrain;
	terrain.init(map.heightMap, map.chunks);
	const ArmySpawnPositions::Lookup lookup = [&](const std::string &name) -> const Coord3D * {
		const Waypoint *waypoint = terrain.findWaypointByName(name);
		return waypoint ? &waypoint->location : nullptr;
	};
	const std::vector<std::string> connections{ "Cair_Andros", "Fangorn", "Gap_Of_Rohan", "Helms_Deep", "The_Brown_Lands" };
	ArmySpawnPositions::Positions out;
	REQUIRE(ArmySpawnPositions::farthestPair(lookup, connections, 0, out));
	CHECK(out.names.first == "WOTRSpawnPoint_Helms_Deep_1");
	CHECK(out.spawn.x == -200.0f);
	CHECK(out.gather.x == 400.0f);
	CHECK(out.gather.y == 460.0f);
	REQUIRE(ArmySpawnPositions::farthestPair(lookup, connections, 1, out));
	CHECK(out.names.first == "WOTRSpawnPoint_The_Brown_Lands_1");
	CHECK(out.gather.x == 2600.0f);
	CHECK(out.gather.y == 2220.0f);
	REQUIRE(ArmySpawnPositions::farthestFromStart(lookup, connections, out));
	CHECK(out.names.first == "WOTRSpawnPoint_Gap_Of_Rohan_1");
	CHECK(out.spawn.x == -200.0f);
	CHECK(out.spawn.y == 2300.0f);
	CHECK(out.gather.x == 400.0f);
	CHECK(out.gather.y == 2300.0f);
}
