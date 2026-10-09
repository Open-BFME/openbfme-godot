// OpenBFME unit tests: roads, water geometry, weather keys and the stop register of the maps lane. GPL-3.0.
// Expected values are hand-computed from the cited ZH formulas (W3DRoadBuffer.cpp) and geometry.

#include "doctest.h"

#include "Common/LooseFileScan.h"
#include "GameClient/MapStops.h"
#include "GameClient/MapWeather.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/WaterGeometry.h"
#include "GameClient/MapUtil.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef OPENBFME_DOCS_DIR
#define OPENBFME_DOCS_DIR "../docs"
#endif

namespace
{

WorldHeightMap flatMap(int w, int h, int border, int raw)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = border;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, (std::uint16_t)raw);
	return m;
}

MapObject roadPoint(const char *name, float x, float y, int flags)
{
	MapObject o;
	o.m_objectName = name;
	o.m_location.x = x;
	o.m_location.y = y;
	o.m_flags = flags;
	return o;
}

float polygonArea(const std::vector<Point2F> &p, const std::vector<std::uint32_t> &tri)
{
	double a = 0;
	for (size_t i = 0; i + 2 < tri.size(); i += 3)
	{
		const Point2F &A = p[tri[i]], &B = p[tri[i + 1]], &C = p[tri[i + 2]];
		a += std::fabs(((double)(B.x - A.x) * (C.y - A.y) - (double)(B.y - A.y) * (C.x - A.x)) * 0.5);
	}
	return (float)a;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Roads
// ------------------------------------------------------------------------------------------------

TEST_CASE("TerrainRoads: roads.ini Road and Bridge blocks; commented-out blocks do not count")
{
	const char *ini =
		";Road Hidden\n"
		";  Texture = X.tga\n"
		";End\n"
		"Road FourLaneDirt\n"
		"  Texture = TRFourLaneDirt.tga\n"
		"  RoadWidth = 70\n"
		"  RoadWidthInTexture = 0.9\n"
		"End\n"
		"Bridge TEST_Bridge\n"
		"  Texture = B.tga\n"
		"  BridgeScale = 0.6\n"
		"End\n";
	RoadTypeIndex idx;
	TerrainRoads::scanText(ini, idx);
	CHECK(idx.byName.size() == 2);
	const RoadType *r = idx.find("fourlanedirt");
	REQUIRE(r != nullptr);
	CHECK(r->texture == "TRFourLaneDirt.tga");
	CHECK(r->roadWidth == 70.0f);
	CHECK(r->roadWidthInTexture == 0.9f);
	CHECK_FALSE(r->isBridge);
	REQUIRE(idx.find("TEST_Bridge") != nullptr);
	CHECK(idx.find("TEST_Bridge")->isBridge);
	CHECK(idx.find("Hidden") == nullptr);
}

TEST_CASE("TerrainRoads::buildStrips: one straight segment, columns every 10 units, ZH UVs, flat z above the cell maximum")
{
	// flat map at raw 256 = 10 world units, 10x10 grid, border 0
	WorldHeightMap map = flatMap(10, 10, 0, 256);
	map.m_data[(size_t)(0 * 10 + 1)] = 512; // cell (0,0) gets a corner at 20 world units
	RoadTypeIndex types;
	RoadType t;
	t.name = "R";
	t.texture = "TR.tga";
	t.roadWidth = 20.0f;
	t.roadWidthInTexture = 0.5f;
	types.byName["r"] = t;

	std::vector<MapObject> objs = { roadPoint("R", 0, 0, FLAG_ROAD_POINT1), roadPoint("R", 30, 0, FLAG_ROAD_POINT2) };
	std::vector<RoadStrip> strips;
	RoadBuildReport rep;
	TerrainRoads::buildStrips(objs, types, map, 0.5f, strips, rep);
	CHECK(rep.pairs == 1);
	REQUIRE(strips.size() == 1);
	const RoadStrip &s = strips[0];
	CHECK(s.texture == "TR.tga");
	// half width = RoadWidth * RoadWidthInTexture / 2 = 5; length 30 -> uCount = 30/10 + 1 = 4 columns, 2 vertices each
	REQUIRE(s.position.size() == 4 * 2 * 3);
	CHECK(s.index.size() == 3 * 6);
	// column 0 bottom/top at x = 0, y = -5 / +5 (normal = (-dy, dx) = (0, 1))
	CHECK(s.position[0] == 0.0f);  CHECK(s.position[1] == -5.0f);
	CHECK(s.position[3] == 0.0f);  CHECK(s.position[4] == 5.0f);
	CHECK(s.position[6] == 10.0f); CHECK(s.position[7] == -5.0f);
	CHECK(s.position[18] == 30.0f);
	// z = max cell height under the column + lift: column 0 touches cell (0,0) whose corner is 20 units: 20 + 0.5
	CHECK(s.position[2] == 20.5f);
	CHECK(s.position[5] == 20.5f);
	// a column over a flat cell (x = 30 -> cell (3,0)): 10 + 0.5
	CHECK(s.position[20] == 10.5f);
	// uv: u = U/(scale*4), v = 85/512 - V/(scale*4); scale = RoadWidth = 20 -> divisor 80
	CHECK(s.uv[0] == 0.0f);
	CHECK(s.uv[1] == doctest::Approx(85.0f / 512.0f + 5.0f / 80.0f).epsilon(1e-6));  // bottom: V = -5
	CHECK(s.uv[3] == doctest::Approx(85.0f / 512.0f - 5.0f / 80.0f).epsilon(1e-6));  // top: V = +5
	CHECK(s.uv[4] == doctest::Approx(10.0f / 80.0f).epsilon(1e-6));
	// first quad: (p0,p1,c1),(p0,c1,c0) with p0..p1 column 0 and c0..c1 column 1
	const std::uint32_t want[6] = { 0, 1, 3, 0, 3, 2 };
	for (int i = 0; i < 6; ++i) CHECK(s.index[(size_t)i] == want[i]);
}

TEST_CASE("TerrainRoads::buildStrips pairing: ZH pairs by flags only, nudges zero-length segments, drops duplicates, counts orphans and unknown types")
{
	WorldHeightMap map = flatMap(20, 20, 0, 0);
	RoadTypeIndex types;
	RoadType t;
	t.name = "R";
	t.texture = "TR.tga";
	t.roadWidth = 10.0f;
	t.roadWidthInTexture = 1.0f;
	types.byName["r"] = t;
	const int P1 = FLAG_ROAD_POINT1, P2 = FLAG_ROAD_POINT2;
	std::vector<MapObject> objs = {
		roadPoint("R", 0, 0, P1), roadPoint("R", 20, 0, P2),               // pair 1
		roadPoint("R", 20, 0, P1), roadPoint("R", 0, 0, P2),               // the same segment reversed: a duplicate
		roadPoint("R", 5, 5, P1 | FLAG_ROAD_CORNER_TIGHT), roadPoint("R", 5, 5, P2), // zero length: nudged by 0.25
		roadPoint("R", 50, 50, P1), roadPoint("Other", 60, 50, 0),         // POINT1 not followed by POINT2: orphan
		roadPoint("R", 70, 50, P2),                                        // POINT2 with no POINT1: orphan
		roadPoint("Nope", 80, 80, P1), roadPoint("Nope", 90, 80, P2),      // a pair whose type is not in roads.ini
		roadPoint("R", 100, 100, P1), roadPoint("Mismatch", 110, 100, P2), // names differ: still a pair (ZH ignores the 2nd name)
		roadPoint("R", 150, 150, P1),                                      // trailing POINT1: orphan
	};
	std::vector<RoadStrip> strips;
	RoadBuildReport rep;
	TerrainRoads::buildStrips(objs, types, map, 0.0f, strips, rep);
	CHECK(rep.pairs == 5);
	CHECK(rep.duplicateSegments == 1);
	CHECK(rep.nudgedZeroLength == 1);
	CHECK(rep.orphanPoints == 3);
	CHECK(rep.unknownRoadTypes == 1);
	REQUIRE(rep.unknownNames.size() == 1);
	CHECK(rep.unknownNames[0] == "Nope");
	CHECK(strips.size() == 3); // pair 1, the nudged one, and the mismatched-name pair
	CHECK(rep.strips == 3);
	// the nudged segment is 0.25 long: 2 columns (uCount < 2 -> 2)
	CHECK(strips[1].position.size() == 2 * 2 * 3);
	CHECK(strips[1].position[6] == 5.25f); // second column x

	CHECK(TerrainRoads::getMaxCellHeight(map, 0, 0) == 0.0f);
}

TEST_CASE("TerrainRoads::getMaxCellHeight takes the maximum of the cell's four corners, clamped to the map, in world units")
{
	WorldHeightMap map = flatMap(6, 6, 1, 0);
	// cell (iX=2, iY=2) in index space = world (10,10) with border 1
	map.m_data[(size_t)(2 + 2 * 6)] = 256;
	map.m_data[(size_t)(3 + 3 * 6)] = 768;
	CHECK(TerrainRoads::getMaxCellHeight(map, 10.0f, 10.0f) == 768 * (10.0f / 256.0f));
	CHECK(TerrainRoads::getMaxCellHeight(map, 19.9f, 19.9f) == 768 * (10.0f / 256.0f));
	CHECK(TerrainRoads::getMaxCellHeight(map, 20.0f, 20.0f) == 768 * (10.0f / 256.0f)); // the next cell shares the raised corner (3,3)
	CHECK(TerrainRoads::getMaxCellHeight(map, 30.0f, 30.0f) == 0.0f);
	// far outside: clamped to the last cell
	CHECK(TerrainRoads::getMaxCellHeight(map, 1e6f, 1e6f) == 0.0f);
	CHECK(TerrainRoads::getMaxCellHeight(map, -1e6f, -1e6f) == 0.0f);
}

// ------------------------------------------------------------------------------------------------
// Water geometry
// ------------------------------------------------------------------------------------------------

TEST_CASE("WaterGeometry::triangulate: convex, non-convex and either winding give n-2 triangles that cover the polygon")
{
	std::vector<std::uint32_t> tri;
	std::vector<Point2F> square = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
	REQUIRE(WaterGeometry::triangulate(square, tri));
	CHECK(tri.size() == 6);
	CHECK(polygonArea(square, tri) == doctest::Approx(100.0f));
	CHECK(WaterGeometry::signedArea(square) == 100.0f);

	std::vector<Point2F> cw(square.rbegin(), square.rend());
	CHECK(WaterGeometry::signedArea(cw) == -100.0f);
	REQUIRE(WaterGeometry::triangulate(cw, tri));
	CHECK(tri.size() == 6);
	CHECK(polygonArea(cw, tri) == doctest::Approx(100.0f));

	// an L shape (area 20*10 + 10*10 = 300): 6 points -> 4 triangles, none may cover the notch
	std::vector<Point2F> L = { { 0, 0 }, { 20, 0 }, { 20, 10 }, { 10, 10 }, { 10, 20 }, { 0, 20 } };
	REQUIRE(WaterGeometry::triangulate(L, tri));
	CHECK(tri.size() == 12);
	CHECK(polygonArea(L, tri) == doctest::Approx(300.0f));

	// a comb (two teeth of width 3): area 9*3 + 2*(3*7) = 69
	std::vector<Point2F> comb = { { 0, 0 }, { 9, 0 }, { 9, 10 }, { 6, 10 }, { 6, 3 }, { 3, 3 }, { 3, 10 }, { 0, 10 } };
	REQUIRE(WaterGeometry::triangulate(comb, tri));
	CHECK(tri.size() == 18);
	CHECK(polygonArea(comb, tri) == doctest::Approx(9.0f * 3.0f + 2.0f * 3.0f * 7.0f));

	// degenerate input is refused, not invented
	std::vector<Point2F> line = { { 0, 0 }, { 5, 0 }, { 10, 0 } };
	CHECK_FALSE(WaterGeometry::triangulate(line, tri));
	CHECK_FALSE(WaterGeometry::triangulate({ { 0, 0 }, { 1, 1 } }, tri));
}

TEST_CASE("WaterGeometry::buildRiverStrip: two vertices per cross-section line, UV v by distance along the centre line")
{
	RiverArea r;
	r.waterHeight = 290;
	r.lines = { { 0, 0, 0, 10 }, { 30, 0, 30, 10 }, { 30, 40, 30, 50 } };
	WaterGeometry::RiverStrip s;
	REQUIRE(WaterGeometry::buildRiverStrip(r, 80.0f, s));
	REQUIRE(s.position.size() == 6 * 3);
	CHECK(s.position[0] == 0.0f);  CHECK(s.position[1] == 0.0f);  CHECK(s.position[2] == 290.0f);
	CHECK(s.position[3] == 0.0f);  CHECK(s.position[4] == 10.0f);
	CHECK(s.index.size() == 12);
	// centres (0,5) -> (30,5) -> (30,45): along = 0, 30, 70
	CHECK(s.uv0[1] == 0.0f);
	CHECK(s.uv0[5] == 30.0f / 80.0f);
	CHECK(s.uv0[9] == 70.0f / 80.0f);
	CHECK(s.uv0[0] == 0.0f);
	CHECK(s.uv0[2] == 1.0f);
	CHECK(s.uv1[1] == 0.5f);
	// one line is not a river
	RiverArea one;
	one.lines = { { 0, 0, 1, 1 } };
	CHECK_FALSE(WaterGeometry::buildRiverStrip(one, 80.0f, s));
}

// ------------------------------------------------------------------------------------------------
// Weather
// ------------------------------------------------------------------------------------------------

TEST_CASE("MapWeather: hardware fog and cloud keys of the Weather block; map.ini overrides weather.ini; other keys ignored")
{
	MapWeather w;
	MapWeatherScan::applyText(
		"; defaults\n"
		"Weather\n"
		"  SnowEnabled = yes\n"
		"  CloudTextureSize = X:660.0 Y:660.0 ; comment\n"
		"  CloudOffsetPerSecond = X:-0.012 Y:-0.018\n"
		"End\n",
		w);
	CHECK(w.cloudSize[0] == 660.0f);
	CHECK(w.cloudOffsetPerSecond[1] == -0.018f);
	CHECK_FALSE(w.fogEnabled);
	CHECK_FALSE(w.fogKeysSeen);

	MapWeatherScan::applyText(
		"Object Foo\n"
		"  HardwareFogEnable = Yes\n" // outside a Weather block: ignored
		"End\n"
		"Weather\n"
		"  HardwareFogColor = R:220 G:200 B:100\n"
		"  HardwareFogEnable = Yes\n"
		"  HardwareFogStart = 500\n"
		"  HardwareFogEnd = 1700\n"
		"  CloudTextureSize = X:1000 Y:500\n"
		"End\n",
		w);
	CHECK(w.fogEnabled);
	CHECK(w.fogKeysSeen);
	CHECK(w.fogColor[0] == 220.0f / 255.0f);
	CHECK(w.fogColor[2] == 100.0f / 255.0f);
	CHECK(w.fogStart == 500.0f);
	CHECK(w.fogEnd == 1700.0f);
	CHECK(w.cloudSize[0] == 1000.0f);
	CHECK(w.cloudSize[1] == 500.0f);
	CHECK(w.cloudOffsetPerSecond[0] == -0.012f); // untouched by the second block

	MapWeather off;
	MapWeatherScan::applyText("Weather\n HardwareFogEnable = No\nEnd\n", off);
	CHECK_FALSE(off.fogEnabled);
	CHECK(off.fogKeysSeen);
}

// ------------------------------------------------------------------------------------------------
// Loose files (stop S-039): the scan the production mount runs
// ------------------------------------------------------------------------------------------------

TEST_CASE("LooseFileScan::scanInstalls: loose .map/.scb files are findings carrying the S-039 line; a missing install is an error")
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "openbfme_loose_scan_test";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "libraries" / "lib_a");
	fs::create_directories(root / "maps");
	auto put = [](const fs::path &p, const std::string &data) {
		std::ofstream(p, std::ios::binary) << data;
	};
	put(root / "libraries" / "lib_a" / "lib_a.map", "");        // 0 bytes: would hide an archive copy
	put(root / "maps" / "Extra.SCB", "abc");                    // case-insensitive extension, 3 bytes
	put(root / "maps" / "notes.txt", "not a map");              // ignored
	put(root / "game.dat", "x");                                // ignored

	std::vector<LooseFileFinding> found;
	std::string err;
	REQUIRE_MESSAGE(LooseFileScan::scanInstalls({ { "testinstall", root.string() } }, found, &err), err);
	REQUIRE(found.size() == 2);
	CHECK(found[0].install == "testinstall");
	CHECK(found[0].file.relativePath == "libraries/lib_a/lib_a.map");
	CHECK(found[0].file.size == 0);
	CHECK(found[0].line == "S-039 contamination (PLAN rule 7): loose testinstall file libraries/lib_a/lib_a.map (0 bytes) is not part of retail; "
		"it is never read, the archive copy is used (a 0-byte loose map would hide that archive copy if loose files were mounted)");
	CHECK(found[1].file.relativePath == "maps/Extra.SCB");
	CHECK(found[1].file.size == 3);
	CHECK(found[1].line.find("(3 bytes)") != std::string::npos);
	CHECK(found[1].line.find("0-byte") == std::string::npos);

	// nothing is skipped silently: an unreadable install is an error and yields no partial result
	CHECK_FALSE(LooseFileScan::scanInstalls({ { "ok", root.string() }, { "gone", (root / "missing").string() } }, found, &err));
	CHECK(err.find("not a directory") != std::string::npos);
	CHECK(found.empty());
	fs::remove_all(root, ec);
}

// ------------------------------------------------------------------------------------------------
// The stop register
// ------------------------------------------------------------------------------------------------

TEST_CASE("Stops S-030..S-039 and S-060 are all in code, unique, and registered in docs/STOPS.md with the same area text")
{
	const std::vector<MapStop> &stops = MapStops::all();
	REQUIRE(stops.size() == 11);
	std::set<std::string> ids;
	for (size_t i = 0; i < stops.size(); ++i)
	{
		char want[8];
		// this lane owns S-030 to S-039, in order, and S-060 (the lane's extended range S-060..S-069)
		std::snprintf(want, sizeof(want), "S-%03d", i < 10 ? 30 + (int)i : 60);
		CHECK(std::string(stops[i].id) == want); // this lane owns S-030 to S-039, in order
		ids.insert(stops[i].id);
		CHECK(std::string(stops[i].area).size() > 3);
		CHECK(std::string(stops[i].gap).size() > 20);
	}
	CHECK(ids.size() == 11);
	CHECK(MapStops::find("S-034") != nullptr);
	CHECK(MapStops::find("S-099") == nullptr);
	CHECK(MapStops::terrainRender().size() == 7); // S-030..S-036
	CHECK(MapStops::mapLoad().size() == 3);       // S-037..S-039
	REQUIRE(MapStops::terrainLogic().size() == 1);
	CHECK(std::string(MapStops::terrainLogic()[0]->id) == "S-060");

	// the register
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md (OPENBFME_DOCS_DIR = " << OPENBFME_DOCS_DIR << ")");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string doc = ss.str();
	for (const MapStop &s : stops)
	{
		// | ID | Kind | Area | Gap | ...: every stop of this lane is a `code` stop with this area and gap text
		const std::string row = std::string("| ") + s.id + " | code | " + s.area + " | " + s.gap + " | ";
		CHECK_MESSAGE(doc.find(row) != std::string::npos, "docs/STOPS.md lacks the row '" << row << "'");
		// the gap text of the register row is the code's text
		CHECK_MESSAGE(doc.find(s.gap) != std::string::npos, "docs/STOPS.md gap text of " << s.id << " differs from the code");
	}

	// the load-time stops (every LoadedMap reports S-037 and S-038, asserted in test_map_chunks.cpp)
	CHECK(std::string(MapStops::mapLoad()[0]->id) == "S-037");
	CHECK(std::string(MapStops::mapLoad()[1]->id) == "S-038");
	CHECK(std::string(MapStops::mapLoad()[2]->id) == "S-039");
}
