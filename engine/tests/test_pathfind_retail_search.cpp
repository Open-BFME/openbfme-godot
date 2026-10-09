// OpenBFME retail tests for the pathfinder with the structures of the retail maps and the A* on the largest map (lane PATH-1). Run only
// when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP). The geometric invariants checked here are independent of the
// footprint rasteriser: an obstacle cell must lie near the structure that owns it.

#include "doctest.h"

#include "Common/AsciiString.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapPathfindObjects.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

using namespace mapcorpus;

namespace
{
std::string dirOf(const std::string &path)
{
	return path.substr(0, path.find_last_of("/\\"));
}

struct NullWorld : PathfindWorld
{
	PathfindObject *findObjectByID(PathfindObjectID) const override { return nullptr; }
	unsigned getFrame() const override { return 100; }
};

struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
	MapObjectOptions options;
	PathfindConfig config;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
			else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error) ||
				!PathfindConfigLoader::load(*s.mount->fs, s.config, &s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

bool haveWorld(const char *what)
{
	Shared &s = shared();
	if (!s.mount)
	{
		retailtest::printSkip(what);
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

struct BuiltMap
{
	LoadedMap map;
	TerrainLogic terrain;
	std::unique_ptr<TerrainPathfindSource> source;
	MapObjectDrawables drawables;
	MapPathfindObjectSet objects;
	NullWorld world;
	std::unique_ptr<Pathfinder> pf;
};

void buildMap(const std::string &path, BuiltMap &out)
{
	Shared &s = shared();
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(path, bytes, &err), err);
	const std::string key = slashes(lowerStr(path), '/');
	MapReadOptions opt;
	REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, out.map, &err), err);
	std::vector<std::string> problems;
	out.terrain.init(out.map.heightMap, out.map.chunks, &problems);
	out.source = std::make_unique<TerrainPathfindSource>(out.terrain, out.map.heightMap, out.map.chunks);
	const RetailObjectWorld::MapIniResult mi = s.world->applyMapIni(slashes(dirOf(path), '\\'));
	REQUIRE(mi.errors.empty());
	MapObjectCreation::build(out.map, key, s.world->things(), out.terrain, s.options, out.drawables);
	MapPathfindObjects::build(out.drawables, out.objects);
	REQUIRE_MESSAGE(out.objects.errors.empty(), out.objects.errors.front());
	const std::vector<PathfindObject *> ptrs = out.objects.pointers();
	out.pf = std::make_unique<Pathfinder>(s.config, &out.world);
	out.pf->newMap(*out.source, &ptrs);
}

Coord3D cellCentre(int x, int y)
{
	Coord3D c;
	c.x = (float)x * 10.0f + 5.0f;
	c.y = (float)y * 10.0f + 5.0f;
	c.z = 0.0f;
	return c;
}
} // namespace

TEST_CASE("pathfind retail: every obstacle cell of a map with structures lies near the structure that owns it")
{
	if (!haveWorld("pathfind retail structures"))
	{
		return;
	}
	Shared &s = shared();
	FilenameList list;
	s.mount->fs->getFileListInDirectory("", "", "*.map", list, true);
	REQUIRE(list.size() == 181);
	std::vector<std::string> sample;
	for (const std::string &p : list)
	{
		const std::string k = lowerStr(p);
		if (k.find("grey mountains") != std::string::npos || k.find("evendim") != std::string::npos || k.find("helms deep") != std::string::npos ||
			k.find("minas tirith") != std::string::npos)
		{
			sample.push_back(p);
		}
	}
	REQUIRE(!sample.empty());
	size_t totalStructures = 0, totalObstacleCells = 0;
	for (const std::string &path : sample)
	{
		INFO("map " << path);
		BuiltMap b;
		buildMap(path, b);
		totalStructures += b.objects.structures;
		std::map<PathfindObjectID, const MapPathfindObject *> byId;
		for (const auto &o : b.objects.objects)
		{
			byId[o->id] = o.get();
		}
		const PathfindGridStats st = b.pf->gridStats();
		size_t obstacleCells = 0, orphans = 0, far = 0;
		for (int x = 0; x < st.width; ++x)
		{
			for (int y = 0; y < st.height; ++y)
			{
				const PathfindCell *c = b.pf->cellAt(x, y);
				if (c->getType() != PathfindCell::CELL_OBSTACLE)
				{
					continue;
				}
				++obstacleCells;
				auto it = byId.find(c->getObstacleID());
				if (it == byId.end())
				{
					++orphans;
					continue;
				}
				const MapPathfindObject &o = *it->second;
				const float dx = o.position.x - ((float)x * 10.0f + 5.0f), dy = o.position.y - ((float)y * 10.0f + 5.0f);
				// a fence is a long thin box: its extent is the geometry's half length; every shape lies inside its bounding circle
				// plus one cell diagonal plus the footprint margin (2 cells)
				const float reach = o.geometry.boundingCircleRadius() + std::max(o.fenceWidth, o.geometry.majorRadius) + 35.0f;
				if (std::hypot(dx, dy) > reach)
				{
					++far;
				}
			}
		}
		CHECK_MESSAGE(orphans == 0, orphans << " obstacle cells carry an id no structure has");
		CHECK_MESSAGE(far == 0, far << " obstacle cells are farther from their structure than its bounding circle allows");
		totalObstacleCells += obstacleCells;
		if (b.objects.structures > 0)
		{
			CHECK(obstacleCells > 0);
		}
	}
	std::printf("  info: pathfind structures: %zu maps, %zu structures, %zu obstacle cells\n", sample.size(), totalStructures, totalObstacleCells);
	CHECK(totalStructures > 0);
}

TEST_CASE("pathfind retail: a long path across the largest map is found, valid and stable; timing reported")
{
	if (!haveWorld("pathfind retail long path"))
	{
		return;
	}
	Shared &s = shared();
	BuiltMap b;
	buildMap("maps\\map mp grey mountains\\map mp grey mountains.map", b);
	Pathfinder &pf = *b.pf;
	const PathfindGridStats st = pf.gridStats();
	ICoord2D lo, hi;
	pf.getLogicalExtent(lo, hi);
	struct Cand
	{
		int x, y;
	};
	std::vector<Cand> cands;
	for (int x = lo.x + 2; x <= hi.x - 2; x += 3)
	{
		for (int y = lo.y + 2; y <= hi.y - 2; y += 3)
		{
			const PathfindCell *c = pf.cellAt(x, y);
			if (c->getType() == PathfindCell::CELL_CLEAR && !c->getPinched())
			{
				cands.push_back({ x, y });
			}
		}
	}
	REQUIRE(cands.size() > 100);
	PathfindLocomotorInfo loco;
	loco.validSurfaces = LOCOMOTORSURFACE_GROUND;
	// anchors: the 8 clear cells nearest each of the four corners of the playable area (by x + y and by x - y)
	std::vector<Cand> anchors;
	auto addNearest = [&](int cornerX, int cornerY) {
		std::vector<Cand> v = cands;
		std::sort(v.begin(), v.end(), [&](const Cand &a, const Cand &b2) {
			const int da = std::abs(a.x - cornerX) + std::abs(a.y - cornerY), db = std::abs(b2.x - cornerX) + std::abs(b2.y - cornerY);
			return da != db ? da < db : (a.x != b2.x ? a.x < b2.x : a.y < b2.y);
		});
		for (int i = 0; i < 8; ++i) anchors.push_back(v[(size_t)i]);
	};
	addNearest(lo.x, lo.y);
	addNearest(hi.x, hi.y);
	addNearest(lo.x, hi.y);
	addNearest(hi.x, lo.y);
	// of all connected pairs (by the zone test) the one whose search examines the most cells: a path that needs real searching
	const Cand *start = nullptr, *goal = nullptr;
	int bestCells = -1;
	for (size_t i = 0; i < anchors.size(); ++i)
	{
		for (size_t j = i + 1; j < anchors.size(); ++j)
		{
			Coord3D f = cellCentre(anchors[i].x, anchors[i].y), g = cellCentre(anchors[j].x, anchors[j].y);
			if (std::hypot(g.x - f.x, g.y - f.y) < 2000.0f || !pf.clientSafeQuickDoesPathExist(loco, &f, &g))
			{
				continue;
			}
			const int before = pf.cumulativeCellsAllocated();
			bool pp = false;
			std::unique_ptr<Path> p(pf.findPath(nullptr, loco, &f, &g, &pp));
			const int used = pf.cumulativeCellsAllocated() - before;
			if (p && !pp && used > bestCells)
			{
				bestCells = used;
				start = &anchors[i];
				goal = &anchors[j];
			}
		}
	}
	REQUIRE(start != nullptr);
	const Coord3D from = cellCentre(start->x, start->y), to = cellCentre(goal->x, goal->y);
	const float straight = std::hypot(to.x - from.x, to.y - from.y);

	std::vector<double> ms;
	std::vector<Coord3D> first;
	bool partial = false;
	int cellsExamined = 0;
	for (int run = 0; run < 5; ++run)
	{
		const int before = pf.cumulativeCellsAllocated();
		const auto t0 = std::chrono::steady_clock::now();
		std::unique_ptr<Path> p(pf.findPath(nullptr, loco, &from, &to, &partial));
		ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		REQUIRE(p != nullptr);
		cellsExamined = pf.cumulativeCellsAllocated() - before;
		std::vector<Coord3D> pts;
		for (PathNode *n = p->getFirstNode(); n; n = n->getNextOptimized())
		{
			pts.push_back(*n->getPosition());
		}
		if (run == 0)
		{
			first = pts;
			REQUIRE(pts.size() >= 2);
			CHECK(!partial);
			CHECK(pts.front().x == from.x);
			CHECK(pts.front().y == from.y);
			CHECK(std::hypot(pts.back().x - to.x, pts.back().y - to.y) <= 10.0f);
			float length = 0.0f;
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				length += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
				// the optimiser only keeps segments the unit may walk. The A* itself enters pinched cells (they cost 14 more, RW 0x934602) and the
				// optimiser line-tests only its shortcuts, so a segment of single-cell steps may cross pinched cells: they are allowed here (lane PATH-2:
				// the footprints with the retail post region (RW 0xAD1D60) changed the grid, and the chosen pair now runs through a pinched corridor)
				CHECK(pf.isLinePassable(nullptr, LOCOMOTORSURFACE_GROUND, LAYER_GROUND, pts[i], pts[i + 1], false, true));
			}
			CHECK(length >= straight - 0.01f);
			CHECK(length < 3.0f * straight);
			std::printf("  info: long path on grey mountains: straight %.0f, path %.0f, %zu optimised nodes, %d cells examined\n", straight, length, pts.size(), cellsExamined);
		}
		else
		{
			REQUIRE(pts.size() == first.size());
			for (size_t i = 0; i < pts.size(); ++i)
			{
				CHECK(pts[i].x == first[i].x);
				CHECK(pts[i].y == first[i].y);
			}
		}
	}
	std::sort(ms.begin(), ms.end());
	std::printf("  info: findPath timing over 5 runs: min %.2f ms, median %.2f ms, max %.2f ms; grid %dx%d\n", ms.front(), ms[ms.size() / 2], ms.back(), st.width, st.height);
	(void)s;
}
