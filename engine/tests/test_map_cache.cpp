// OpenBFME unit tests: RotWK's mapcache.ini, the .scb script libraries and loose-file contamination. GPL-3.0.
//
// Synthetic tests for the MapCache scanner and the ObjectNameScan run everywhere. The retail tests run
// only when ROTWK_INSTALL and BFME2_INSTALL are set (SKIP otherwise) and use the shared mount. Their
// expected values come from the retail mapcache.ini itself (fileSize, extentMax, Player_N_Start) and from
// tests/data/map-survey.json (the independent oracle, tools/maps/map_survey.py).

#include "doctest.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"

#include "Common/LooseFileScan.h"
#include "Common/MiniJson.h"
#include "GameClient/MapCache.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Object/ObjectNameIndex.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

using namespace mapcorpus;

// ------------------------------------------------------------------------------------------------
// synthetic
// ------------------------------------------------------------------------------------------------

TEST_CASE("MapCache: escaped block names, UTF-16 labels, coordinates, Player_N_Start")
{
	const char *text =
		"; FILE: Maps\\MapCache.ini\n"
		"\n"
		"MapCache maps_5Cmap_20mp_20a_5Fb_5Cmap_20mp_20a_5Fb_2Emap\n"
		"  fileSize = 6294\n"
		"  fileCRC = 3783827778\n"
		"  timestampLo = -996901888\n"
		"  timestampHi = 29802434\n"
		"  isOfficial = yes\n"
		"  isMultiplayer = yes\n"
		"  isScenarioMP = no\n"
		"  numPlayers = 2\n"
		"  extentMin = X:0.00 Y:0.00 Z:0.00\n"
		"  extentMax = X:1000.00 Y:2500.50 Z:0.00\n"
		"  displayName = _24_00M_00a_00p_00_3A_00X_00\n"
		"  description = M_00a_00p_00_3A_00X_00_2F_00D_00e_00s_00c_00\n"
		"  InitialCameraPosition = X:541.58 Y:297.66 Z:0.00\n"
		"  Player_1_Start = X:10.00 Y:20.50 Z:0.00\n"
		"  Player_12_Start = X:-3.25 Y:4.00 Z:0.00\n"
		"END\n"
		"\n"
		"MapCache maps_5Cb_5Cb_2Emap\n"
		"  numPlayers = 1\n"
		"END\n";
	std::vector<MapCacheEntry> e;
	std::string err;
	REQUIRE_MESSAGE(MapCache::parse(text, e, &err), err);
	REQUIRE(e.size() == 2);
	CHECK(e[0].name == "maps/map mp a_b/map mp a_b.map");
	CHECK(e[0].fileSize == 6294);
	CHECK(e[0].fileCRC == 3783827778u);
	CHECK(e[0].timestampLo == -996901888);
	CHECK(e[0].isOfficial);
	CHECK(e[0].isMultiplayer);
	CHECK_FALSE(e[0].isScenarioMP);
	CHECK(e[0].numPlayers == 2);
	CHECK(e[0].extentMax.x == 1000.0f);
	CHECK(e[0].extentMax.y == 2500.5f);
	CHECK(e[0].hasInitialCamera);
	CHECK(e[0].initialCamera.y == 297.66f);
	CHECK(e[0].displayName == std::u16string(u"$Map:X"));
	CHECK(e[0].description == std::u16string(u"Map:X/Desc"));
	REQUIRE(e[0].startPositions.size() == 2);
	CHECK(e[0].startPositions.at(1).y == 20.5f);
	CHECK(e[0].startPositions.at(12).x == -3.25f);
	CHECK(e[1].name == "maps/b/b.map");
	CHECK_FALSE(e[1].hasInitialCamera);

	CHECK(MapCache::unescape("a_5Fb_2e_zz_2") == "a_b._zz_2"); // only a full _XX hex pair is an escape
}

TEST_CASE("MapCache: unknown keys, bad values and unterminated blocks are errors")
{
	std::vector<MapCacheEntry> e;
	std::string err;
	CHECK_FALSE(MapCache::parse("MapCache a\n  bogus = 1\nEND\n", e, &err));
	CHECK(err.find("unknown key 'bogus'") != std::string::npos);
	CHECK_FALSE(MapCache::parse("MapCache a\n  fileSize = twelve\nEND\n", e, &err));
	CHECK(err.find("bad value for 'fileSize'") != std::string::npos);
	CHECK_FALSE(MapCache::parse("MapCache a\n  isOfficial = maybe\nEND\n", e, &err));
	CHECK(err.find("bad value for 'isOfficial'") != std::string::npos);
	CHECK_FALSE(MapCache::parse("MapCache a\n  extentMax = 1 2 3\nEND\n", e, &err));
	CHECK(err.find("bad value for 'extentMax'") != std::string::npos);
	CHECK_FALSE(MapCache::parse("MapCache a\n  numPlayers = 1\n", e, &err));
	CHECK(err.find("missing END") != std::string::npos);
	CHECK_FALSE(MapCache::parse("fileSize = 1\n", e, &err));
	CHECK(err.find("expected 'MapCache <name>'") != std::string::npos);
}

TEST_CASE("ObjectNameScan: Object / ChildObject / ObjectReskin names, case-insensitive keywords, roads, comments")
{
	const char *text =
		"; Object Commented\n"
		"Object GondorFighter\n"
		"  Draw = X\n"
		"End\n"
		"  ChildObject  Child_Of_Fighter   GondorFighter\n"
		"OBJECTRESKIN Reskinned GondorFighter ; trailing comment\n"
		"ObjectCreationList NotAnObject\n"
		"Objective Foo\n"
		"Object\n"
		"\tObject\tTabbed\n";
	std::set<std::string> names;
	ObjectNameScan::scanText(text, names);
	CHECK(names == (std::set<std::string>{ "gondorfighter", "child_of_fighter", "reskinned", "tabbed" }));

	std::set<std::string> roads;
	ObjectNameScan::scanRoadText("Road ForestPath\n  Texture = x\nEnd\nBridge OldBridge\nEnd\nRoadSign Nope\n", roads);
	CHECK(roads == (std::set<std::string>{ "forestpath", "oldbridge" }));

	ObjectNameIndex idx;
	idx.objectNames = { "gondorfighter" };
	idx.roadNames = { "forestpath" };
	idx.mapIniObjectNames["maps\\m\\map.ini"] = { "mapthing" };
	CHECK(idx.resolve("GondorFighter", "") == ObjectNameIndex::GlobalObject);
	CHECK(idx.resolve("ForestPath", "") == ObjectNameIndex::RoadName);
	CHECK(idx.resolve("*Waypoints/Waypoint", "") == ObjectNameIndex::SpecialName);
	// the "*" exemption is a list of evidenced names, not a wildcard: a typo is reported, not exempt
	CHECK(idx.resolve("*TypoMissing", "") == ObjectNameIndex::Unresolved);
	CHECK(idx.resolve("*", "") == ObjectNameIndex::Unresolved);
	CHECK(idx.resolve("*waypoints/waypoint", "") == ObjectNameIndex::Unresolved); // spelled as the maps spell it
	CHECK(idx.resolve("MapThing", "maps\\m\\map.ini") == ObjectNameIndex::MapIniObject);
	CHECK(idx.resolve("MapThing", "maps\\other\\map.ini") == ObjectNameIndex::Unresolved);
	CHECK(idx.resolve("Nope", "") == ObjectNameIndex::Unresolved);
	MapObject scorch;
	scorch.m_objectName = "Scorch";
	scorch.m_isScorch = true;
	CHECK(idx.resolveObject(scorch, "") == ObjectNameIndex::SpecialName);
	scorch.m_isScorch = false;
	CHECK(idx.resolveObject(scorch, "") == ObjectNameIndex::Unresolved);
}

// ------------------------------------------------------------------------------------------------
// retail
// ------------------------------------------------------------------------------------------------

TEST_CASE("map corpus: mapcache.ini lists the 122 official maps; each resolves and parses, and the cache agrees with the map")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("map corpus mapcache");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<unsigned char> surveyBytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/map-survey.json", surveyBytes, &err), err);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(surveyBytes.begin(), surveyBytes.end()), survey, &err), err);

	// RotWK's shipped cache (in _patch201maps.big, which wins over maps.big)
	const std::string cachePath = "maps\\mapcache.ini";
	REQUIRE(mount->fs->doesFileExist(cachePath));
	CHECK(archiveLabel(mount->fs->getArchiveFilenameForFile(cachePath)) == "rotwk:_patch201maps.big");
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(mount->fs->readFile(cachePath, bytes, &err), err);
	std::vector<MapCacheEntry> cache;
	REQUIRE_MESSAGE(MapCache::parse(std::string(bytes.begin(), bytes.end()), cache, &err), err);
	REQUIRE(cache.size() == 122);

	// the same 122 names the independent oracle read from the file
	std::set<std::string> cacheNames, oracleNames;
	for (const MapCacheEntry &e : cache) cacheNames.insert(e.name);
	for (const auto &kv : J(survey, "mapcache").object) oracleNames.insert(kv.first);
	CHECK(cacheNames == oracleNames);

	int multiplayer = 0, official = 0, scenarioMp = 0;
	std::vector<std::string> sizeMismatches;
	int mpChecked = 0, spChecked = 0, spWithStarts = 0, scenarioEqWorld = 0;
	for (const MapCacheEntry &e : cache)
	{
		INFO("cache entry " << e.name);
		multiplayer += e.isMultiplayer;
		official += e.isOfficial;
		scenarioMp += e.isScenarioMP;
		const std::string path = slashes(e.name, '\\');
		REQUIRE_MESSAGE(mount->fs->doesFileExist(path), "cache entry does not resolve in the mount: " << path);
		REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &err), err);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(bytes, e.name, opt, m, &err), err);

		// MapCache fileSize is the stored file size
		if ((size_t)e.fileSize != bytes.size())
		{
			sizeMismatches.push_back(e.name + " cache " + std::to_string(e.fileSize) + " archive " + std::to_string(bytes.size()));
		}
		// extents: (width - 2*border) * 10 on both axes, in every one of the 122 entries
		const WorldHeightMap &w = m.heightMap;
		CHECK(e.extentMin.x == 0.0f);
		CHECK(e.extentMin.y == 0.0f);
		CHECK(e.extentMax.x == (float)((w.m_width - 2 * w.m_borderSize) * 10));
		CHECK(e.extentMax.y == (float)((w.m_height - 2 * w.m_borderSize) * 10));
		// Player_N_Start waypoints: the cache stores their positions at 2 decimals
		std::map<int, Coord3D> starts;
		for (const MapObject &o : m.chunks.objects)
		{
			std::string wn = o.getWaypointName();
			int idx = 0;
			char tail = 0;
			if (o.isWaypoint() && std::sscanf(wn.c_str(), "Player_%d_Star%c", &idx, &tail) == 2 && tail == 't' && idx >= 1)
			{
				starts[idx] = o.m_location;
			}
		}
		// every position the cache lists is the position of that Player_N_Start waypoint (2 decimals)
		for (const auto &sp : e.startPositions)
		{
			auto it = starts.find(sp.first);
			REQUIRE_MESSAGE(it != starts.end(), "Player_" << sp.first << "_Start waypoint missing");
			CHECK(std::fabs(it->second.x - sp.second.x) < 0.006f);
			CHECK(std::fabs(it->second.y - sp.second.y) < 0.006f);
		}
		if (e.isMultiplayer)
		{
			// multiplayer entries list every Player_N_Start waypoint and numPlayers counts them
			CHECK(starts.size() == e.startPositions.size());
			CHECK((int)starts.size() == e.numPlayers);
			++mpChecked;
		}
		else
		{
			// single-player entries: one player; the cinematics list no start position, some campaign maps do
			CHECK(e.numPlayers == 1);
			++spChecked;
			spWithStarts += !e.startPositions.empty();
		}
		bool worldScenario = m.chunks.worldInfo.getBool("isScenarioMultiplayer");
		scenarioEqWorld += worldScenario == e.isScenarioMP;
		// display names are string-table labels ("$Map:...") for gameplay maps; cinematics use plain text.
		// The description is "<label>/Desc".
		CHECK_FALSE(e.displayName.empty());
		REQUIRE(e.description.size() > 5);
		CHECK(e.description.compare(e.description.size() - 5, 5, u"/Desc") == 0);
		CHECK(e.isOfficial);
	}
	std::printf("  info: %d official, %d multiplayer (start positions and numPlayers checked against the Player_N_Start waypoints), %d single-player (%d list start positions), %d scenario-MP; isScenarioMP == worldInfo flag in %d of 122\n",
		official, multiplayer, spChecked, spWithStarts, scenarioMp, scenarioEqWorld);
	CHECK(official == 122);
	CHECK(multiplayer == 72); // 22 "map mp *" + 50 "map wor *" (spec 0.2)
	CHECK(mpChecked == 72);
	CHECK(spChecked == 50);
	CHECK(scenarioEqWorld == 122);
	for (const std::string &s : sizeMismatches)
	{
		std::printf("  finding: cache fileSize != stored size: %s\n", s.c_str());
	}
	// 121 of 122 equal the archive's stored size; 'map good celduin' is off by one byte in the shipped cache
	CHECK(sizeMismatches.size() == 1);
	if (sizeMismatches.size() == 1)
	{
		CHECK(sizeMismatches[0].find("maps/map good celduin/map good celduin.map cache 539458 archive 539459") == 0);
	}
}

TEST_CASE("map corpus: .scb script libraries, and loose map files are reported as contamination")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("map corpus scb/loose");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<unsigned char> surveyBytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/map-survey.json", surveyBytes, &err), err);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(surveyBytes.begin(), surveyBytes.end()), survey, &err), err);

	FilenameList scb;
	mount->fs->getFileListInDirectory("", "", "*.scb", scb, true);
	const JsonValue &want = J(survey, "scb");
	CHECK(scb.size() == 5);
	CHECK(scb.size() == want.object.size());
	std::vector<std::uint8_t> bytes;
	for (const std::string &path : scb)
	{
		const std::string key = slashes(lowerStr(path), '/');
		INFO("scb " << key);
		const JsonValue *w = want.get(key);
		REQUIRE(w != nullptr);
		REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &err), err);
		CHECK(archiveLabel(mount->fs->getArchiveFilenameForFile(path)) == J(*w, "src").string);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, m, &err), err);
		CHECK(m.envelope == "raw"); // .scb files are stored uncompressed
		CHECK((std::int64_t)m.storedSize == N(J(*w, "stored")));
		const auto &top = J(*w, "top").array;
		std::vector<const ChunkStat *> topStats;
		for (const ChunkStat &cs : m.chunkLog)
		{
			if (cs.depth == 0) topStats.push_back(&cs);
		}
		REQUIRE(topStats.size() == top.size());
		for (size_t i = 0; i < top.size(); ++i)
		{
			CHECK(topStats[i]->label == top[i].array[0].string);
			CHECK(topStats[i]->version == N(top[i].array[1]));
			CHECK(topStats[i]->size == N(top[i].array[2]));
		}
		CHECK(m.hasPlayerScripts);
		CHECK(m.chunks.scb.hasImportSize);
		CHECK(m.chunks.scb.hasScriptsPlayers);
		CHECK(m.chunks.scb.hasScriptTeams);
	}

	// PLAN rule 7: loose map files in the install folders are contamination, reported and never read.
	// The same scan the production mount runs (RetailFileSystem::mount_retail -> LooseFileScan::scanInstalls),
	// whose findings every map build reports as stop S-039.
	std::vector<LooseFileFinding> findings;
	REQUIRE_MESSAGE(LooseFileScan::scanInstalls({ { "rotwk", std::getenv("ROTWK_INSTALL") }, { "bfme2", std::getenv("BFME2_INSTALL") } }, findings, &err), err);
	size_t zeroByte = 0;
	for (const LooseFileFinding &f : findings)
	{
		std::printf("  %s\n", f.line.c_str());
		zeroByte += f.file.size == 0;
	}
	std::printf("  info: %zu loose .map/.scb file(s), %zu of them 0 bytes; the loader never reads them\n", findings.size(), zeroByte);
	// Whatever the install holds (a clean install has none; the developer's Windows install had two 0-byte
	// libraries): every finding carries the S-039 line built by the production formatter for exactly that file.
	// Detection itself is proven without any install by the synthetic "LooseFileScan::scanInstalls" test
	// (test_map_render.cpp); here the scan of the real install must agree with its own findings.
	for (const LooseFileFinding &f : findings)
	{
		CHECK((f.install == "rotwk" || f.install == "bfme2"));
		CHECK(f.line == LooseFileScan::describeFinding(f.install, f.file));
		CHECK(f.line.rfind("S-039 contamination (PLAN rule 7): loose " + f.install + " file " + f.file.relativePath + " (", 0) == 0);
		CHECK(f.line.find("(" + std::to_string(f.file.size) + " byte") != std::string::npos);
		// the loader never reads a loose file: a path the archives hold is served from the archive, whatever the loose copy holds
		std::string native = f.file.relativePath;
		for (char &c : native) c = c == '/' ? '\\' : c;
		if (mount->fs->doesFileExist(native.c_str()))
		{
			REQUIRE_MESSAGE(mount->fs->readFile(native.c_str(), bytes, &err), err);
			if (f.file.size == 0) CHECK_MESSAGE(bytes.size() > 0, "a 0-byte loose " << f.file.relativePath << " would have hidden the archive copy");
		}
	}
	// the library maps the Windows install shadowed are served from the archives and parse, loose files or not
	for (const char *lib : { "libraries\\lib_end_mission\\lib_end_mission.map", "libraries\\lib_gollumspawn\\lib_gollumspawn.map" })
	{
		REQUIRE_MESSAGE(mount->fs->doesFileExist(lib), lib);
		REQUIRE(mount->fs->readFile(lib, bytes, &err));
		CHECK(bytes.size() > 8);
		LoadedMap m;
		MapReadOptions opt;
		CHECK_MESSAGE(MapReader::load(bytes, lib, opt, m, &err), err);
	}
}
