// OpenBFME retail tests for the pathfinder on real maps (lane PATH-2). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP). They mount pure
// RotWK 2.01 + BFME2 1.06, build the pathfinder of real maps with lakes, order real units across them in a started skirmish, and check the multi-shape
// templates of the retail data. Expectations are rules of the binary (water cells never take a ground unit, RW table 0xDA2444 / classifyMapCell 0x934A85),
// never values read back from this engine.

#include "CreepTestUtil.h"
#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapPathfindObjects.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "MapCorpusUtil.h"
#include "PathfindTestUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

bool isWet(PathfindCell::CellType t)
{
	return t == PathfindCell::CELL_WATER || t == PathfindCell::CELL_DEEP_WATER;
}

struct NullWorld : PathfindWorld
{
	PathfindObject *findObjectByID(PathfindObjectID) const override { return nullptr; }
	unsigned getFrame() const override { return 100; }
};

// a map's pathfinder with the map's structures (the map-only view, as PATH-1's corpus tests build it)
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

void buildMap(starttest::Shared &s, const PathfindConfig &config, const std::string &path, BuiltMap &out)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(path, bytes, &err), err);
	const std::string key = mapcorpus::slashes(mapcorpus::lowerStr(path), '/');
	MapReadOptions opt;
	REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, out.map, &err), err);
	std::vector<std::string> problems;
	out.terrain.init(out.map.heightMap, out.map.chunks, &problems);
	out.source = std::make_unique<TerrainPathfindSource>(out.terrain, out.map.heightMap, out.map.chunks);
	const std::string dir = path.substr(0, path.find_last_of("/\\"));
	const RetailObjectWorld::MapIniResult mi = s.world->applyMapIni(mapcorpus::slashes(dir, '\\'));
	REQUIRE(mi.errors.empty());
	MapObjectCreation::build(out.map, key, s.world->things(), out.terrain, s.options, out.drawables);
	MapPathfindObjects::build(out.drawables, out.objects);
	REQUIRE_MESSAGE(out.objects.errors.empty(), out.objects.errors.front());
	const std::vector<PathfindObject *> ptrs = out.objects.pointers();
	out.pf = std::make_unique<Pathfinder>(config, &out.world);
	out.pf->newMap(*out.source, &ptrs);
	REQUIRE(out.pf->isMapReady());
}

// the cells of the straight line between two cells (Bresenham, both ends included)
template <typename F> void lineCells(ICoord2D a, ICoord2D b, F f)
{
	int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
	int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
	int e = dx + dy;
	for (;;)
	{
		f(a.x, a.y);
		if (a.x == b.x && a.y == b.y) break;
		const int e2 = 2 * e;
		if (e2 >= dy) { e += dy; a.x += sx; }
		if (e2 <= dx) { e += dx; a.y += sy; }
	}
}

Coord3D centreOf(int x, int y)
{
	return Coord3D{ (float)x * 10.0f + 5.0f, (float)y * 10.0f + 5.0f, 0.0f };
}

ICoord2D cellOf(const Coord3D &p)
{
	return ICoord2D{ (int)std::floor(p.x / 10.0f), (int)std::floor(p.y / 10.0f) };
}

starttest::Shared *sharedWithConfig(PathfindConfig &config)
{
	starttest::Shared *s = starttest::shared();
	if (!s)
	{
		return nullptr;
	}
	std::string err;
	REQUIRE_MESSAGE(PathfindConfigLoader::load(*s->mount->fs, config, &err), err);
	return s;
}

NewGameMessage evendimMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 5000;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_EASY_AI;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMordor");
	c.startPos = 4;
	c.color = 1;
	return m;
}

// one started Evendim skirmish (the VIS-1 setup: Men in slot 0 at start 0, an easy Mordor computer at start 4): `templateName` of the human player is made
// 150 / -150 from Player_1_Start, then ordered (MSG_DO_MOVETO through the selected group, the HUD's path) to `goal`
struct LakeRun
{
	std::vector<Coord3D> positions; // every frame after the order
	std::vector<int> cellTypes;
	std::vector<std::uint32_t> hashes;
	float wadeDepth = 0.0f;
	float lastDepth = -1.0f;        // water z - ground z at the last position (-1: no water there)
};

void runLake(starttest::Shared &s, const std::string &templateName, const Coord3D &goalIn, int frames, LakeRun &out,
	const std::function<void(LiveGame &, int human)> &afterLoad = nullptr)
{
	static std::vector<MapCacheEntry> cache;
	std::string err;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &err), err);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(evendimMessage(s, 4711), s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &err), err);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	const Player *slot0 = game.players().findPlayerWithName(game.report().startSlotPlayers[0]);
	REQUIRE(slot0);
	const int human = slot0->getPlayerIndex();
	if (afterLoad)
	{
		afterLoad(game, human);
	}
	const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(w);
	Coord3D at = w->location;
	at.x += 150.0f;
	at.y -= 150.0f;
	creeptest::removeCreeps(game.logic()); // lane MOD-4: Evendim's creeps would attack the soldier on its way
	Object *obj = game.createObject(templateName, human, at, 0.0f, &err);
	REQUIRE_MESSAGE(obj, err);
	const ObjectID id = obj->getID();
	for (int f = 0; f < 20; ++f)
	{
		game.logic().runLogicFrame();
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, human);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(id);
	game.commands().append(sel);
	GameMessage mv(MSG_DO_MOVETO, human);
	Coord3D goal = goalIn;
	goal.z = game.logic().getGroundHeight(goal.x, goal.y);
	mv.appendLocationArgument(goal);
	game.commands().append(mv);
	Pathfinder &pf = game.ai().pathfinder();
	out.wadeDepth = pf.config().wadeWaterDepth;
	// S-610: the registration stop is reported once by the live game's AI world
	int registration = 0;
	for (const std::string &line : game.ai().stops())
	{
		registration += line == AIWorld::registrationStop() ? 1 : 0;
	}
	CHECK(registration == 1);
	CHECK(AIWorld::registrationStop().rfind("S-610 ", 0) == 0);
	for (int f = 0; f < frames; ++f)
	{
		game.logic().runLogicFrame();
		Object *u = game.logic().findObjectByID(id);
		REQUIRE(u);
		const Coord3D p = *u->getPosition();
		const PathfindCell *cell = pf.getCell(LAYER_GROUND, &p);
		out.positions.push_back(p);
		out.cellTypes.push_back(cell ? (int)cell->getType() : -1);
		out.hashes.push_back(game.logic().computeStateHash());
	}
	const Coord3D last = out.positions.back();
	float waterZ = 0.0f;
	if (game.logic().terrain()->getStandingWaterHeight(last.x, last.y, waterZ))
	{
		out.lastDepth = waterZ - game.logic().getGroundHeight(last.x, last.y);
	}
}
} // namespace

TEST_CASE("path2 retail: ground paths go around the lakes of Evendim, Umbar and Ettenmoors; a flyer and a ship may cross")
{
	PathfindConfig config;
	starttest::Shared *s = sharedWithConfig(config);
	if (!s)
	{
		retailtest::printSkip("path2 retail lakes");
		return;
	}
	REQUIRE_MESSAGE(s->world != nullptr, s->error);
	const char *maps[] = { "maps\\map mp evendim\\map mp evendim.map", "maps\\map mp umbar\\map mp umbar.map", "maps\\map good ettenmoors\\map good ettenmoors.map" };
	for (const char *path : maps)
	{
		BuiltMap b;
		buildMap(*s, config, path, b);
		Pathfinder &pf = *b.pf;
		ICoord2D lo, hi;
		pf.getLogicalExtent(lo, hi);
		// candidates: CLEAR, unpinched cells on a 12-cell lattice; deep water cells for the ship
		std::vector<ICoord2D> land, deep;
		for (int x = lo.x + 2; x <= hi.x - 2; x += 12)
		{
			for (int y = lo.y + 2; y <= hi.y - 2; y += 12)
			{
				const PathfindCell *c = pf.cellAt(x, y);
				if (c->getType() == PathfindCell::CELL_CLEAR && !c->getPinched())
				{
					land.push_back({ x, y });
				}
				else if (c->getType() == PathfindCell::CELL_DEEP_WATER)
				{
					deep.push_back({ x, y });
				}
			}
		}
		REQUIRE(land.size() > 50);
		REQUIRE(deep.size() > 5);
		// land pairs at most 2500 apart whose straight line crosses water and that a ground unit can connect (zones): the one crossing the most water cells
		// whose search completes within MaxCellsFindPathLimit (a longer detour is a partial path in retail too) must walk around
		PathfindLocomotorInfo ground;
		ground.validSurfaces = LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_RUBBLE; // HumanLocomotor Surfaces = GROUND RUBBLE
		struct Pair
		{
			int wet;
			size_t a, b;
		};
		std::vector<Pair> pairs;
		for (size_t i = 0; i < land.size(); ++i)
		{
			for (size_t j = i + 1; j < land.size(); ++j)
			{
				if (std::hypot((float)(land[j].x - land[i].x), (float)(land[j].y - land[i].y)) > 250.0f)
				{
					continue;
				}
				int wet = 0;
				lineCells(land[i], land[j], [&](int x, int y) { wet += isWet(pf.cellAt(x, y)->getType()) ? 1 : 0; });
				if (wet >= 10)
				{
					pairs.push_back({ wet, i, j });
				}
			}
		}
		std::stable_sort(pairs.begin(), pairs.end(), [](const Pair &x, const Pair &y) { return x.wet > y.wet; });
		size_t bestA = 0, bestB = 0;
		int bestWet = 0, tries = 0;
		std::unique_ptr<Path> p;
		bool partial = false;
		for (const Pair &c : pairs)
		{
			Coord3D f = centreOf(land[c.a].x, land[c.a].y), g = centreOf(land[c.b].x, land[c.b].y);
			if (!pf.clientSafeQuickDoesPathExist(ground, &f, &g))
			{
				continue;
			}
			if (++tries > 60)
			{
				break;
			}
			p.reset(pf.findPath(nullptr, ground, &f, &g, &partial));
			if (p && !partial)
			{
				bestWet = c.wet;
				bestA = c.a;
				bestB = c.b;
				break;
			}
		}
		REQUIRE(bestWet >= 10);
		const Coord3D from = centreOf(land[bestA].x, land[bestA].y), to = centreOf(land[bestB].x, land[bestB].y);
		REQUIRE(p != nullptr);
		CHECK(!partial);
		// every cell of every optimised segment is dry, and the path is longer than the straight line it could not take
		int wetCells = 0, segments = 0;
		float length = 0.0f;
		std::vector<Coord3D> pts;
		for (PathNode *n = p->getFirstNode(); n; n = n->getNextOptimized())
		{
			pts.push_back(*n->getPosition());
		}
		REQUIRE(pts.size() >= 2);
		for (size_t i = 0; i + 1 < pts.size(); ++i)
		{
			++segments;
			length += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
			lineCells(cellOf(pts[i]), cellOf(pts[i + 1]), [&](int x, int y) { wetCells += isWet(pf.cellAt(x, y)->getType()) ? 1 : 0; });
		}
		for (PathNode *n = p->getFirstNode(); n; n = n->getNext())
		{
			const PathfindCell *c = pf.getCell(LAYER_GROUND, n->getPosition());
			REQUIRE(c);
			wetCells += isWet(c->getType()) ? 1 : 0;
		}
		const float straight = std::hypot(to.x - from.x, to.y - from.y);
		CHECK(wetCells == 0);
		CHECK(length > straight * 1.02f);
		// the same search twice gives the same nodes (no state leaks between searches)
		std::unique_ptr<Path> again(pf.findPath(nullptr, ground, &from, &to, &partial));
		REQUIRE(again != nullptr);
		size_t k = 0;
		for (PathNode *n = again->getFirstNode(); n; n = n->getNextOptimized(), ++k)
		{
			REQUIRE(k < pts.size());
			CHECK(n->getPosition()->x == pts[k].x);
			CHECK(n->getPosition()->y == pts[k].y);
		}
		CHECK(k == pts.size());
		// a flyer: every cell of the straight line is a valid position for an AIR surface (RW table 0xDA2444 has AIR in every type a lake has)
		PathfindLocomotorInfo air;
		air.validSurfaces = LOCOMOTORSURFACE_AIR;
		const PathfindMovement airMv = Pathfinder::makeMovement(nullptr, air);
		const PathfindMovement groundMv = Pathfinder::makeMovement(nullptr, ground);
		int airBlocked = 0, groundBlocked = 0;
		lineCells(land[bestA], land[bestB], [&](int x, int y) {
			airBlocked += pf.validMovementPosition(airMv, pf.cellAt(x, y)) ? 0 : 1;
			groundBlocked += pf.validMovementPosition(groundMv, pf.cellAt(x, y)) ? 0 : 1;
		});
		CHECK(airBlocked == 0);
		CHECK(groundBlocked >= bestWet);
		// a ship (WATER DEEP_WATER) between the two deep cells farthest apart that the zones connect: its path stays in the water
		PathfindLocomotorInfo ship;
		ship.validSurfaces = LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER;
		size_t sa = 0, sb = 0;
		float far = 0.0f;
		for (size_t i = 0; i < deep.size(); ++i)
		{
			for (size_t j = i + 1; j < deep.size(); ++j)
			{
				Coord3D f = centreOf(deep[i].x, deep[i].y), g = centreOf(deep[j].x, deep[j].y);
				const float d = std::hypot(g.x - f.x, g.y - f.y);
				if (d > far && pf.clientSafeQuickDoesPathExist(ship, &f, &g))
				{
					far = d;
					sa = i;
					sb = j;
				}
			}
		}
		REQUIRE(far > 300.0f);
		const Coord3D sf = centreOf(deep[sa].x, deep[sa].y), sg = centreOf(deep[sb].x, deep[sb].y);
		std::unique_ptr<Path> sp(pf.findPath(nullptr, ship, &sf, &sg, &partial));
		REQUIRE(sp != nullptr);
		int dryShipCells = 0;
		for (PathNode *n = sp->getFirstNode(); n; n = n->getNext())
		{
			dryShipCells += isWet(pf.getCell(LAYER_GROUND, n->getPosition())->getType()) ? 0 : 1;
		}
		CHECK(dryShipCells == 0);
		std::printf("  info: %s: ground path around the lake (%.0f, %.0f) -> (%.0f, %.0f): straight %.0f, path %.0f (%d segments, %d water cells crossed by the straight line); "
					"ship path %.0f long\n",
			path, from.x, from.y, to.x, to.y, straight, length, segments, bestWet, far);
	}
}

TEST_CASE("path2 retail: the VIS-1 soldier ordered to the Evendim island stops at the shore, wading no deeper than WadeWaterDepth; two runs agree, another goal changes the world")
{
	OPENBFME_REQUIRE_START(s);
	// the island in the middle of the lake: no ground path reaches it (VIS-1's video: a soldier ordered to the map centre stood in the lake)
	const Coord3D island{ 2500.0f, 2500.0f, 0.0f };
	LakeRun a, b, c;
	runLake(*s, "GondorFighter", island, 400, a);
	runLake(*s, "GondorFighter", island, 400, b);
	int wet = 0;
	for (int t : a.cellTypes)
	{
		wet += isWet((PathfindCell::CellType)t) ? 1 : 0;
	}
	CHECK(wet == 0);
	CHECK(a.cellTypes.back() == PathfindCell::CELL_CLEAR);
	// it moved, and it stopped: the last 100 frames stand still
	CHECK(std::hypot(a.positions.back().x - a.positions.front().x, a.positions.back().y - a.positions.front().y) > 1000.0f);
	CHECK(a.positions.back().x == a.positions[a.positions.size() - 100].x);
	CHECK(a.positions.back().y == a.positions[a.positions.size() - 100].y);
	// where it stands the lake is at most WadeWaterDepth (5.0, default/aidata.ini) above the ground: retail's classifyMapCell (RW 0x934A85) keeps such a cell CLEAR,
	// so a ground unit may wade there (as the computer player's soldier does, 4 deep, in a probe of this lane); VIS-1's video showed such a stop at the end of a
	// partial path (the island is unreachable), with the standing water drawn over the shallow shore
	CHECK(a.lastDepth <= a.wadeDepth);
	std::printf("  info: VIS-1 soldier: stopped at (%.1f, %.1f) in water %.2f deep (WadeWaterDepth %.1f)\n", a.positions.back().x, a.positions.back().y, a.lastDepth, a.wadeDepth);
	// determinism: the same orders give the same state hash every frame
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE(a.hashes[i] == b.hashes[i]);
	}
	// mutation: a goal on the near shore gives another world
	runLake(*s, "GondorFighter", Coord3D{ 1000.0f, 3000.0f, 0.0f }, 400, c);
	CHECK(c.hashes.back() != a.hashes.back());
}

TEST_CASE("path2 retail: the multi-shape structures of the retail data: every active, low shape enters the grid, the bounding circle covers every shape")
{
	OPENBFME_REQUIRE_START(s);
	int multi = 0, checked = 0;
	std::string examples;
	for (const ThingTemplate *tt : s->world->things().templates())
	{
		if (!tt || !tt->findField("Geometry"))
		{
			continue;
		}
		std::vector<ObjectGeometry::Shape> shapes;
		try
		{
			shapes = ObjectGeometry::shapesOf(*tt);
		}
		catch (const std::exception &e)
		{
			FAIL("geometry of " << tt->getName() << ": " << std::string(e.what()));
		}
		int lowActive = 0;
		for (const ObjectGeometry::Shape &sh : shapes)
		{
			lowActive += (sh.active && sh.offsetZ <= 10.0f) ? 1 : 0;
		}
		if (lowActive < 2)
		{
			continue;
		}
		++multi;
		if (checked >= 12)
		{
			continue;
		}
		++checked;
		if (examples.size() < 200)
		{
			examples += tt->getName() + " ";
		}
		// on a flat synthetic grid: the template's shapes against shape 0 alone
		pathtest::SyntheticTerrain terrain(200, 200);
		pathtest::TestWorld world;
		Pathfinder pf(pathtest::testConfig(), &world);
		pf.newMap(terrain);
		pathtest::TestObject full;
		full.id = 1;
		full.structure = true;
		full.pos = Coord3D{ 1000.0f, 1000.0f, 0.0f };
		ObjectGeometry::fillPathfindGeometry(*tt, full.geometry);
		world.objects[full.id] = &full;
		pf.addObjectToPathfindMap(full);
		int fullCells = 0, firstCells = 0;
		for (int x = 0; x < 200; ++x)
		{
			for (int y = 0; y < 200; ++y)
			{
				fullCells += pf.cellAt(x, y)->getObstacleID() == full.id ? 1 : 0;
			}
		}
		// every active low shape's centre cell is an obstacle of the object, unless that shape is too small to cover a cell
		for (const PathfindShape &sh : full.geometry.shapes)
		{
			if (!sh.active || sh.offsetZ > 10.0f || sh.majorRadius < 5.0f || (sh.type == PATHFIND_GEOMETRY_BOX && sh.minorRadius < 5.0f))
			{
				continue;
			}
			Coord3D c = full.pos;
			PathfindGeometry::applyShapeOffset(sh, 0.0f, c);
			const PathfindCell *cell = pf.getCell(LAYER_GROUND, &c);
			REQUIRE(cell);
			CHECK_MESSAGE(cell->getObstacleID() == full.id, tt->getName());
		}
		pf.removeObjectFromPathfindMap(full);
		pathtest::TestObject first = full;
		first.id = 2;
		first.geometry.shapes.resize(1);
		world.objects[first.id] = &first;
		pf.addObjectToPathfindMap(first);
		for (int x = 0; x < 200; ++x)
		{
			for (int y = 0; y < 200; ++y)
			{
				firstCells += pf.cellAt(x, y)->getObstacleID() == first.id ? 1 : 0;
			}
		}
		CHECK_MESSAGE(fullCells >= firstCells, tt->getName());
		CHECK(full.geometry.boundingCircleRadius() >= first.geometry.boundingCircleRadius());
	}
	std::printf("  info: retail templates with two or more active low shapes: %d (checked %d: %s)\n", multi, checked, examples.c_str());
	CHECK(multi > 0);
}

namespace
{
int obstacleCellsOf(Pathfinder &pf, PathfindObjectID id)
{
	int n = 0;
	const ICoord2D *e = pf.getExtent();
	for (int x = 0; x <= e->x; ++x)
	{
		for (int y = 0; y <= e->y; ++y)
		{
			n += pf.cellAt(x, y)->getObstacleID() == id ? 1 : 0;
		}
	}
	return n;
}
} // namespace

TEST_CASE("path2 retail: placed structures register their footprints as they are made: the starting fortress, and a barracks a script / tool places (RW 0x629EC9, 0x62C98D)")
{
	OPENBFME_REQUIRE_START(s);
	LakeRun run;
	int fortressCells = -1, placedCells = -1, placedAfterDeath = -1;
	runLake(*s, "GondorFighter", Coord3D{ 1000.0f, 3000.0f, 0.0f }, 5, run, [&](LiveGame &game, int human) {
		Pathfinder &pf = game.ai().pathfinder();
		// the starting base (RW 0x629EC9): the human player's structures, placed before the first frame; the largest footprint among them
		int structures = 0;
		const Player *slot0 = game.players().getNthPlayer(human);
		REQUIRE(slot0);
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == slot0 && o->isKindOfName("STRUCTURE"))
			{
				++structures;
				fortressCells = std::max(fortressCells, obstacleCellsOf(pf, o->getID()));
			}
		}
		REQUIRE(structures > 0);
		// a barracks put down on open ground east of the start (the createObject path of the tools and scripts)
		const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
		REQUIRE(w);
		Coord3D at = w->location;
		at.x += 400.0f;
		std::string err;
		Object *b = game.createObject("GondorBarracks", human, at, 0.0f, &err);
		REQUIRE_MESSAGE(b, err);
		placedCells = obstacleCellsOf(pf, b->getID());
		const ObjectID id = b->getID();
		game.logic().destroyObject(b);
		game.logic().runLogicFrame();
		placedAfterDeath = obstacleCellsOf(pf, id);
	});
	CHECK(fortressCells > 0);
	CHECK(placedCells > 0);
	CHECK(placedAfterDeath == 0);
	std::printf("  info: footprints: largest starting structure %d cells, placed GondorBarracks %d cells\n", fortressCells, placedCells);
}

TEST_CASE("path2 retail: a barracks placed on a legal site (its footprint registered) produces a horde that walks out to the rally point, every member on clear ground")
{
	OPENBFME_REQUIRE_START(s);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 1;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	const Player *p1 = game.players().findPlayerWithName("Player_1");
	REQUIRE(p1);
	const int pi = p1->getPlayerIndex();
	Coord3D site{ 0, 0, 0 };
	bool found = false;
	const ThingTemplate *bt = s->world->things().findTemplate("GondorBarracks");
	for (int r = 0; r <= 600 && !found; r += 50)
	{
		for (int k = 0; k < 16 && !found; ++k)
		{
			Coord3D c{ 2500.0f + (float)r * (float)std::cos(k * 0.3927), 2500.0f + (float)r * (float)std::sin(k * 0.3927), 0.0f };
			c.z = game.logic().getGroundHeight(c.x, c.y);
			Coord3D rpt{ c.x, c.y - 300.0f, 0.0f };
			const PathfindCell *rc = game.ai().pathfinder().getCell(LAYER_GROUND, &rpt);
			if (BuildPlacement::isLocationLegalToBuild(game.logic(), c, *bt->getFinalOverride(), 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, nullptr, nullptr) == LBC_OK &&
				rc && rc->getType() == PathfindCell::CELL_CLEAR)
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE(found);
	Object *rb = game.createObject("GondorBarracks", pi, site, 0.0f, &err);
	REQUIRE(rb);
	game.advance(0.2);
	const ThingTemplate *tt = s->world->things().findTemplate("GondorFighterHorde");
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(rb->getID());
	game.commands().append(sel);
	GameMessage m(MSG_QUEUE_UNIT_CREATE, pi);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)tt->getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	game.commands().append(m);
	GameMessage r(MSG_SET_RALLY_POINT, pi);
	r.appendObjectIDArgument(rb->getID());
	Coord3D rp{ site.x, site.y - 300.0f, 0.0f };
	rp.z = game.logic().getGroundHeight(rp.x, rp.y);
	r.appendLocationArgument(rp);
	r.appendBooleanArgument(false);
	r.appendObjectIDArgument(INVALID_ID);
	game.commands().append(r);
	for (int k = 0; k < 160; ++k)
	{
		game.advance(0.5);
	}
	Pathfinder &pf = game.ai().pathfinder();
	CHECK(obstacleCellsOf(pf, rb->getID()) > 0);
	int members = 0, settled = 0;
	for (Object *ob = game.logic().getFirstObject(); ob; ob = ob->getNextObject())
	{
		if (ob->getTemplate()->getName() == "GondorFighter" && ob->getControllingPlayer() == p1)
		{
			++members;
			const Coord3D q = *ob->getPosition();
			const PathfindCell *c = pf.getCell(LAYER_GROUND, &q);
			settled += (c && c->getType() == PathfindCell::CELL_CLEAR && std::hypot(q.x - rp.x, q.y - rp.y) < 120.0f) ? 1 : 0;
		}
	}
	CHECK(members > 5);
	CHECK(settled == members);
}
