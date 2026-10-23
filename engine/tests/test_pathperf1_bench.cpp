// OpenBFME unit tests. GPL-3.0.
// Lane PATHPERF-1: the cost of a big move order. N hordes (or single infantry) of one player stand in a block on a large retail map; one
// MSG_CREATE_SELECTED_GROUP + MSG_DO_MOVETO (the right click on the ground) sends them across the map; every logic frame after it is timed (thread CPU).
// The bench is skipped by default (run it with --no-skip):
//   openbfme_tests -tc="pathperf1 bench*" --no-skip
// PATHPERF1_CASES="10,50,150,i300" picks the cases (default all four; iN = N single infantry), PATHPERF1_MAP the map (default map mp brown lands),
// PATHPERF1_FRAMES the frames after the order (default 60), PATHPERF1_SETTLE the frames before it (default 30), PATHPERF1_PROFILE=uber the Uber profile.
#include "doctest.h"
#include "HudTestUtil.h"
#include "PerfBenchUtil.h"
#include "PathfindTestUtil.h"

#include "Common/Player.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

struct OrderRun
{
	std::vector<double> ms;            ///< thread CPU of each logic frame after the order (index 0: the order frame)
	std::vector<std::uint32_t> hashes; ///< the state hash after each of them
	std::vector<int> cells;            ///< the path cells processPathfindQueue counted in each of them
	std::vector<int> served;           ///< the path requests taken off the queue in each of them (queue length before + new - after, approximated by the drop)
	size_t units = 0;
	size_t pathsQueuedMax = 0;
};

GameMessage selectMsg(int player, const std::vector<ObjectID> &ids)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	for (ObjectID id : ids)
	{
		m.appendObjectIDArgument(id);
	}
	return m;
}

// `count` objects of `templ` in a block of rows around `from`, rows facing `to`; one order moves them all to `to`
// PATHPERF1_PERF_CTL=<fifo>: "enable" / "disable" to a `perf record -D -1 --control=fifo:<fifo>` around the timed frames (the load is not profiled)
void perfControl(const char *cmd)
{
	if (const char *ctl = std::getenv("PATHPERF1_PERF_CTL"))
	{
		if (FILE *f = std::fopen(ctl, "w"))
		{
			std::fprintf(f, "%s\n", cmd);
			std::fclose(f);
		}
	}
}

// `shortcuts` false: the pathfinder's search memo and unit index off (Pathfinder::setSearchShortcuts); `after` (may be empty) runs on the game after the frames
OrderRun runOrder(const char *mapName, const char *templ, int count, float spacing, int frames, bool hashes, bool shortcuts = true,
	const std::function<void(LiveGame &, const std::vector<ObjectID> &, const Coord3D &)> &after = {})
{
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = mapName;
	o.seed = 4711;
	// lane PATHPERF-4: PATHPERF1_PROFILE=uber runs the order in the Uber profile (its larger path queue and budget)
	if (const char *profile = std::getenv("PATHPERF1_PROFILE"))
	{
		o.simulationProfile = std::string(profile) == "uber" ? SimulationProfile::Uber : SimulationProfile::Faithful;
	}
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *pa = game.players().findPlayerWithName("Player_1");
	Player *pb = game.players().findPlayerWithName("Player_2");
	REQUIRE(pa != nullptr);
	REQUIRE(pb != nullptr);
	GameLogic &logic = game.logic();
	REQUIRE(logic.aiWorld() != nullptr);
	logic.aiWorld()->pathfinder().setSearchShortcuts(shortcuts);
	// the march: from in front of Player_1_Start to in front of Player_2_Start
	const Waypoint *wa = logic.terrain() ? logic.terrain()->findWaypointByName("Player_1_Start") : nullptr;
	const Waypoint *wb = logic.terrain() ? logic.terrain()->findWaypointByName("Player_2_Start") : nullptr;
	REQUIRE(wa != nullptr);
	REQUIRE(wb != nullptr);
	const float ax = wa->location.x, ay = wa->location.y, bx = wb->location.x, by = wb->location.y;
	float dx = bx - ax, dy = by - ay;
	const float len = std::sqrt(dx * dx + dy * dy);
	REQUIRE(len > 1000.0f);
	dx /= len, dy /= len;
	// the block's centre 450 in front of Player_1's base; rows across the march direction
	const float cx = ax + dx * 450.0f, cy = ay + dy * 450.0f;
	const int cols = std::max(1, (int)std::ceil(std::sqrt((double)count)));
	const float angle = std::atan2(dy, dx);
	std::vector<ObjectID> ids;
	for (int i = 0; i < count; ++i)
	{
		const int r = i / cols, c = i % cols;
		const float across = ((float)c - (float)(cols - 1) * 0.5f) * spacing;
		const float back = (float)r * spacing;
		const float x = cx - dx * back - dy * across, y = cy - dy * back + dx * across;
		Object *u = game.createObject(templ, pa->getPlayerIndex(), Coord3D{ x, y, 0.0f }, angle, &err);
		REQUIRE_MESSAGE(u, err);
		ids.push_back(u->getID());
	}
	// settle (the hordes form up) past the first 25 logic frames, whose path budget processPathfindQueue multiplies by 100, then one order
	// (PATHPERF1_SETTLE: another settle length, e.g. 10 to order inside the first 25 frames)
	const char *settleEnv = std::getenv("PATHPERF1_SETTLE");
	const int settle = settleEnv ? std::atoi(settleEnv) : 30;
	for (int i = 0; i < settle; ++i)
	{
		REQUIRE(game.advance(0.2) == 1);
	}
	const Coord3D dest{ bx - dx * 450.0f, by - dy * 450.0f, 0.0f };
	game.commands().append(selectMsg(pa->getPlayerIndex(), ids));
	GameMessage mv(MSG_DO_MOVETO, pa->getPlayerIndex());
	mv.appendLocationArgument(dest);
	game.commands().append(mv);
	OrderRun out;
	out.units = ids.size();
	perfControl("enable");
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = perfbench::nowMs();
		REQUIRE(game.advance(0.2) == 1);
		out.ms.push_back(perfbench::nowMs() - t0);
		if (hashes)
		{
			out.hashes.push_back(logic.computeStateHash());
		}
		if (AIWorld *ai = logic.aiWorld())
		{
			out.pathsQueuedMax = std::max(out.pathsQueuedMax, ai->pathfinder().queuedRequests());
			out.cells.push_back(ai->pathfinder().cumulativeCellsAllocated());
			out.served.push_back((int)ai->pathfinder().queuedRequests());
		}
	}
	perfControl("disable");
	if (after)
	{
		after(game, ids, dest);
	}
	return out;
}

std::vector<std::string> cases()
{
	const char *env = std::getenv("PATHPERF1_CASES");
	std::string list = env ? env : "10,50,150,i300";
	std::vector<std::string> out;
	std::stringstream ss(list);
	std::string item;
	while (std::getline(ss, item, ','))
	{
		if (!item.empty())
		{
			out.push_back(item);
		}
	}
	return out;
}
} // namespace

TEST_CASE("pathperf1 bench: one move order for 10 / 50 / 150 hordes and 300 infantry on a large map - logic time of the order frame and the march" * doctest::skip())
{
	SharedWorld &s = shared();
	if (!s.mount || !s.world)
	{
		MESSAGE("SKIP: " << s.error);
		return;
	}
	const char *mapEnv = std::getenv("PATHPERF1_MAP");
	const char *mapName = mapEnv ? mapEnv : "map mp brown lands";
	const char *framesEnv = std::getenv("PATHPERF1_FRAMES");
	const int frames = framesEnv ? std::atoi(framesEnv) : 60;
	for (const std::string &c : cases())
	{
		const bool infantry = c[0] == 'i';
		const int n = std::atoi(c.c_str() + (infantry ? 1 : 0));
		const OrderRun r = runOrder(mapName, infantry ? "GondorFighter" : "GondorFighterHorde", n, infantry ? 14.0f : 70.0f, frames, false);
		std::vector<double> march(r.ms.begin() + 1, r.ms.end());
		const perfbench::FrameStats m = perfbench::stats(march);
		const double worst = *std::max_element(r.ms.begin(), r.ms.end());
		const int worstAt = (int)(std::max_element(r.ms.begin(), r.ms.end()) - r.ms.begin());
		std::printf("  info: pathperf1 %s %s x%zu: order frame %.1f ms; worst frame %.1f ms (frame %d after the order); march mean %.2f ms, p95 %.2f ms, max %.2f ms; "
		            "most queued path requests %zu\n",
			mapName, infantry ? "infantry" : "hordes", r.units, r.ms[0], worst, worstAt, m.mean, m.p95, m.max, r.pathsQueuedMax);
		if (std::getenv("PATHPERF1_FRAMES_DUMP"))
		{
			std::printf("    frames:");
			for (size_t i = 0; i < r.ms.size(); ++i)
			{
				std::printf(" %.1f", r.ms[i]);
				if (i < r.cells.size())
				{
					std::printf("[%dc q%d]", r.cells[i], r.served[i]);
				}
			}
			std::printf("\n");
		}
	}
}

TEST_CASE("pathperf1: a 30 horde move order across a large map runs twice with the same state hash in every frame, and every request leaves the path queue")
{
	SharedWorld &s = shared();
	if (!s.mount || !s.world)
	{
		MESSAGE("SKIP: " << s.error);
		return;
	}
	const OrderRun a = runOrder("map mp brown lands", "GondorFighterHorde", 30, 70.0f, 25, true);
	const OrderRun b = runOrder("map mp brown lands", "GondorFighterHorde", 30, 70.0f, 25, true);
	REQUIRE(a.hashes.size() == b.hashes.size());
	int firstDiff = -1;
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		if (a.hashes[i] != b.hashes[i] && firstDiff < 0)
		{
			firstDiff = (int)i;
		}
	}
	CHECK_MESSAGE(firstDiff < 0, "first differing frame after the order: " << firstDiff);
	CHECK(a.pathsQueuedMax > 0);
	REQUIRE(!a.served.empty());
	CHECK(a.served.back() == 0); // the queue drained (the budget spreads the requests over frames, none is lost)
}

namespace
{
// one search's result: every node's position and layer, and the partial flag
struct SearchResult
{
	bool found = false, partial = false;
	std::vector<float> nodes;
	bool operator==(const SearchResult &o) const { return found == o.found && partial == o.partial && nodes == o.nodes; }
};

SearchResult search(Pathfinder &pf, PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &to)
{
	SearchResult r;
	const Coord3D from = obj.getPosition();
	Path *p = pf.findPath(&obj, loco, &from, &to, &r.partial);
	r.found = p != nullptr;
	for (const PathNode *n = p ? p->getFirstNode() : nullptr; n; n = n->getNext())
	{
		r.nodes.push_back(n->getPosition()->x);
		r.nodes.push_back(n->getPosition()->y);
		r.nodes.push_back(n->getPosition()->z);
		r.nodes.push_back((float)n->getLayer());
	}
	delete p;
	return r;
}
} // namespace

TEST_CASE("pathperf1: the search memo and the unit index change nothing: a 60 horde order hashes the same in every frame with them off, and 60 searches "
		  "from the marching hordes give the same nodes and partial flags both ways")
{
	SharedWorld &s = shared();
	if (!s.mount || !s.world)
	{
		MESSAGE("SKIP: " << s.error);
		return;
	}
	const OrderRun on = runOrder("map mp brown lands", "GondorFighterHorde", 60, 70.0f, 20, true, true);
	size_t searches = 0, partials = 0, corridors = 0;
	const OrderRun off = runOrder("map mp brown lands", "GondorFighterHorde", 60, 70.0f, 20, true, false,
		[&](LiveGame &game, const std::vector<ObjectID> &ids, const Coord3D &dest) {
			// the marching state, the crowd's reservations in place: every horde to the order's goal, to a point beside its own block and to the map's
			// middle, with the shortcuts on, off and on again (a search leaves no state the next one reads but the counters)
			GameLogic &logic = game.logic();
			AIWorld &ai = *logic.aiWorld();
			Pathfinder &pf = ai.pathfinder();
			for (size_t i = 0; i < ids.size(); i += 3)
			{
				Object *o = logic.findObjectByID(ids[i]);
				REQUIRE(o != nullptr);
				AIUpdateInterface *u = o->getAIUpdateInterface();
				REQUIRE(u != nullptr);
				PathfindObject &obj = ai.adapterFor(*o);
				const PathfindLocomotorInfo loco = u->locomotorInfo();
				const Coord3D p = *o->getPosition();
				for (const Coord3D &to : { dest, Coord3D{ p.x + 160.0f, p.y - 120.0f, 0.0f }, Coord3D{ (p.x + dest.x) * 0.5f, (p.y + dest.y) * 0.5f, 0.0f } })
				{
					pf.setSearchShortcuts(true);
					const SearchResult a = search(pf, obj, loco, to);
					pf.setSearchShortcuts(false);
					const SearchResult b = search(pf, obj, loco, to);
					pf.setSearchShortcuts(true);
					const SearchResult c = search(pf, obj, loco, to);
					CHECK(a == b);
					CHECK(a == c);
					++searches;
					partials += a.partial ? 1 : 0;
					corridors += pf.hierarchicalLastIndex() >= 0 ? 1 : 0;
				}
			}
		});
	REQUIRE(on.hashes.size() == off.hashes.size());
	int firstDiff = -1;
	for (size_t i = 0; i < on.hashes.size(); ++i)
	{
		if (on.hashes[i] != off.hashes[i] && firstDiff < 0)
		{
			firstDiff = (int)i;
		}
	}
	CHECK_MESSAGE(firstDiff < 0, "first differing frame after the order: " << firstDiff);
	MESSAGE(searches << " searches compared, " << partials << " partial, " << corridors << " with a corridor");
	CHECK(searches == 60u);
	CHECK(corridors > 0u);
}

// lane PATHPERF-4 with upstream's SAVE-4: an Uber game's path queue is longer than retail's ring (kUberQueueLength, not PATHFIND_QUEUE_LEN), its
// cell info pool grows and a mass order starts a burst; the pathfinder's snapshot keeps all three (version 2: the burst latch)
TEST_CASE("pathperf4 save: an Uber game saved while a mass order's path burst drains, its request ring past retail's 512 slots, loads into itself "
		  "and the next 100 frames hash as before")
{
	SharedWorld &s = shared();
	if (!s.mount || !s.world)
	{
		MESSAGE("SKIP: " << s.error);
		return;
	}
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp brown lands";
	o.seed = 4711;
	o.simulationProfile = SimulationProfile::Uber;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string error;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	Player *pa = game.players().findPlayerWithName("Player_1");
	REQUIRE(pa != nullptr);
	GameLogic &logic = game.logic();
	REQUIRE(logic.aiWorld() != nullptr);
	const Waypoint *wa = logic.terrain() ? logic.terrain()->findWaypointByName("Player_1_Start") : nullptr;
	const Waypoint *wb = logic.terrain() ? logic.terrain()->findWaypointByName("Player_2_Start") : nullptr;
	REQUIRE(wa != nullptr);
	REQUIRE(wb != nullptr);
	// 300 soldiers in a block in front of Player_1's base, ordered back and forth across the map
	const Coord3D home{ wa->location.x + (wb->location.x - wa->location.x) * 0.15f, wa->location.y + (wb->location.y - wa->location.y) * 0.15f, 0.0f };
	const Coord3D away{ wa->location.x + (wb->location.x - wa->location.x) * 0.5f, wa->location.y + (wb->location.y - wa->location.y) * 0.5f, 0.0f };
	std::vector<ObjectID> ids;
	for (int i = 0; i < 300; ++i)
	{
		Object *u = game.createObject("GondorFighter", pa->getPlayerIndex(), Coord3D{ home.x + (float)(i % 20) * 15.0f, home.y + (float)(i / 20) * 15.0f, 0.0f }, 0.0f,
			&error);
		REQUIRE_MESSAGE(u, error);
		ids.push_back(u->getID());
	}
	for (int i = 0; i < 30; ++i)
	{
		REQUIRE(game.advance(0.2) == 1);
	}
	Pathfinder &pf = logic.aiWorld()->pathfinder();
	REQUIRE(pf.uberLimits());
	auto order = [&](const Coord3D &to) {
		game.commands().append(selectMsg(pa->getPlayerIndex(), ids));
		GameMessage mv(MSG_DO_MOVETO, pa->getPlayerIndex());
		mv.appendLocationArgument(to);
		game.commands().append(mv);
	};
	// three orders, each drained: more than 512 requests went through the ring, so its head is past retail's length
	for (int k = 0; k < 3; ++k)
	{
		order(k % 2 == 0 ? away : home);
		for (int f = 0; f < 120 && (f < 2 || pf.queuedRequests() > 0); ++f)
		{
			REQUIRE(game.advance(0.2) == 1);
		}
		REQUIRE(pf.queuedRequests() == 0u);
	}
	CHECK(pf.requestRing().length() == (int)Pathfinder::kUberQueueLength);
	MESSAGE("request ring head after three orders: " << pf.requestRing().head());
	REQUIRE(pf.requestRing().head() >= (int)PATHFIND_QUEUE_LEN); // tolerance: a precondition (past retail's ring; the exact head is the orders' own count)
	// a fourth order; saved after the first frame that latched the burst, with requests still waiting
	order(away);
	bool latched = false;
	for (int f = 0; f < 10 && !latched; ++f)
	{
		REQUIRE(game.advance(0.2) == 1);
		latched = pf.uberBurst() && pf.queuedRequests() > 0;
	}
	REQUIRE(latched);
	std::vector<std::uint8_t> snapshot;
	REQUIRE_MESSAGE(logic.saveSnapshot(snapshot, &error), error);
	const UnsignedInt savedFrame = logic.getFrame();
	const std::uint32_t savedHash = logic.computeStateHash();
	const size_t savedQueued = pf.queuedRequests();
	std::vector<std::uint32_t> hashes;
	for (int i = 0; i < 100; ++i)
	{
		REQUIRE(game.advance(0.2) == 1);
		hashes.push_back(logic.computeStateHash());
	}
	REQUIRE_MESSAGE(game.loadSnapshot(snapshot, &error), error);
	Pathfinder &loaded = game.logic().aiWorld()->pathfinder();
	CHECK(game.logic().getFrame() == savedFrame);
	CHECK(game.logic().computeStateHash() == savedHash);
	CHECK(loaded.uberBurst());
	CHECK(loaded.queuedRequests() == savedQueued);
	int same = 0;
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		REQUIRE(game.advance(0.2) == 1);
		same += game.logic().computeStateHash() == hashes[i] ? 1 : 0;
	}
	CHECK(same == (int)hashes.size());
}

// review (#63): the pathfinder's configuration hash (a snapshot of another configuration is refused) is the value earlier builds wrote: they hashed the
// pool's capacity, then always the configured size; the configured size is hashed now, so a grown Uber pool keeps it too. The literal was measured with
// upstream main's Pathfinder::configHash (7f034123 .. 5a68dad4) on this terrain and configuration
TEST_CASE("pathperf4 save: the pathfinder's configuration hash is the one earlier builds wrote, with the Uber limits on as well")
{
	pathtest::SyntheticTerrain terrain{ 40, 20 };
	pathtest::TestWorld world;
	Pathfinder pf(pathtest::testConfig(), &world);
	pf.newMap(terrain);
	CHECK(pf.configHash() == 56852104u);
	pf.setUberLimits(true);
	CHECK(pf.configHash() == 56852104u);
}

// review of #62 (Greptile P1 on 210a92a82): the cell-info pool grows only with the Uber limits on (Pathfinder::setUberLimits). A Faithful pathfinder keeps retail's fixed pool
// (gamedata 60,000 records, PathfindConfig::cellInfoPoolSize): the 60,001st info fails; an Uber one (or a Faithful one switched to Uber) adds a block of
// half the capacity (PathfindCellInfoPool::get). The snapshot keeps the limits: an Uber pathfinder's 60,001 infos load into a fresh Faithful game (the
// snapshot's flag turns the growth on), a Faithful one's load with the pool still fixed, and a snapshot claiming Faithful with more infos is refused
TEST_CASE("pathperf4: a Faithful pathfinder's cell-info pool never grows past its 60,000 records, an Uber one's does, and the snapshot keeps the limit")
{
	pathtest::SyntheticTerrain terrain{ 300, 300 }; // 90,000 cells
	pathtest::TestWorld world;
	PathfindConfig config = pathtest::testConfig();
	config.cellInfoPoolSize = 60000; // gamedata.ini's (AIPathfindConfig.cpp)
	// a cell info on every cell in index order until the pool runs out or `limit` are taken; the count taken
	auto fill = [](Pathfinder &pf, int limit) {
		int taken = 0;
		for (int x = 0; x < 300 && taken < limit; ++x)
		{
			for (int y = 0; y < 300 && taken < limit; ++y)
			{
				PathfindCell *c = pf.getCell(LAYER_GROUND, x, y);
				REQUIRE(c != nullptr);
				if (c->hasInfo())
				{
					continue;
				}
				ICoord2D pos;
				pos.x = x;
				pos.y = y;
				if (!c->allocateInfo(pf.pool(), pos))
				{
					return taken;
				}
				++taken;
			}
		}
		return taken;
	};
	auto save = [](Pathfinder &pf) {
		XferSave xs;
		pf.xfer(xs);
		return xs.data();
	};
	auto load = [&](const std::vector<std::uint8_t> &bytes, std::string *error) {
		auto pf = std::make_unique<Pathfinder>(config, &world);
		pf->newMap(terrain);
		try
		{
			XferLoad xl(bytes);
			pf->xfer(xl);
		}
		catch (const XferException &e)
		{
			*error = e.what();
		}
		return pf;
	};

	Pathfinder faithful(config, &world);
	faithful.newMap(terrain);
	REQUIRE(!faithful.uberLimits());
	REQUIRE(faithful.pool().freeCount() == 60000);
	CHECK(fill(faithful, 90000) == 60000);
	CHECK(faithful.pool().capacity() == 60000);
	CHECK(faithful.pool().growthLimit() == 0);
	CHECK(faithful.pool().growths() == 0);
	CHECK(faithful.pool().exhaustedOnce());
	CHECK(fill(faithful, 90000) == 0); // still full: the search fails, the reservation is dropped
	const std::vector<std::uint8_t> faithfulBytes = save(faithful);

	Pathfinder uber(config, &world);
	uber.newMap(terrain);
	uber.setUberLimits(true);
	CHECK(uber.pool().growthLimit() == (int)Pathfinder::kCellInfoPoolMax);
	CHECK(fill(uber, 60001) == 60001);
	CHECK(uber.pool().capacity() == 90000); // one block of half the capacity
	CHECK(uber.pool().growths() == 1);
	CHECK(!uber.pool().exhaustedOnce());
	const std::vector<std::uint8_t> uberBytes = save(uber);

	// a Faithful game switched to Uber grows from then on; switched back, it grows no further
	faithful.setUberLimits(true);
	CHECK(fill(faithful, 1) == 1);
	CHECK(faithful.pool().capacity() == 90000);
	faithful.setUberLimits(false);
	CHECK(faithful.pool().growthLimit() == 0);

	std::string error;
	std::unique_ptr<Pathfinder> fromFaithful = load(faithfulBytes, &error);
	REQUIRE_MESSAGE(error.empty(), error);
	CHECK(!fromFaithful->uberLimits());
	CHECK(fromFaithful->pool().capacity() == 60000);
	CHECK(fromFaithful->pool().growthLimit() == 0);
	CHECK(fill(*fromFaithful, 90000) == 0); // the loaded Faithful pool is full and stays fixed
	CHECK(fromFaithful->pool().growths() == 0);

	std::unique_ptr<Pathfinder> fromUber = load(uberBytes, &error);
	REQUIRE_MESSAGE(error.empty(), error);
	CHECK(fromUber->uberLimits());
	CHECK(fromUber->pool().growthLimit() == (int)Pathfinder::kCellInfoPoolMax);
	CHECK(fromUber->pool().capacity() == 90000);
	CHECK(fromUber->pool().freeCount() == 90000 - 60001);
	std::vector<std::uint8_t> again = save(*fromUber);
	CHECK(again == uberBytes);

	// the Uber snapshot with its limits flag cleared (the flag and the two ring lengths, 16,384 each, follow the cell infos): refused, a Faithful pool
	// holds no 60,001 infos
	std::vector<std::uint8_t> claimsFaithful = uberBytes;
	const std::uint8_t tail[] = { 1, 0x00, 0x40, 0, 0, 0x00, 0x40, 0, 0 };
	auto at = std::search(claimsFaithful.begin(), claimsFaithful.end(), std::begin(tail), std::end(tail));
	REQUIRE(at != claimsFaithful.end());
	REQUIRE(std::search(at + 1, claimsFaithful.end(), std::begin(tail), std::end(tail)) == claimsFaithful.end());
	*at = 0;
	std::unique_ptr<Pathfinder> refused = load(claimsFaithful, &error);
	CHECK(error == "pathfinder: the snapshot holds 60001 cell infos, more than the pool of 60000");
}

// review (#64): a game started Faithful that switched to Uber (a loaded save handed over to the player's profile, SAVE-2) and moved its request ring past
// retail's 512 slots saves the rings' lengths, so the snapshot loads into a game whose rings are retail's again
TEST_CASE("pathperf4 save: a game that switched from Faithful to Uber and moved its request ring past 512 loads into a Faithful game")
{
	SharedWorld &s = shared();
	if (!s.mount || !s.world)
	{
		MESSAGE("SKIP: " << s.error);
		return;
	}
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp brown lands";
	o.seed = 4711;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string error;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	Player *pa = game.players().findPlayerWithName("Player_1");
	REQUIRE(pa != nullptr);
	GameLogic &logic = game.logic();
	const Waypoint *wa = logic.terrain() ? logic.terrain()->findWaypointByName("Player_1_Start") : nullptr;
	const Waypoint *wb = logic.terrain() ? logic.terrain()->findWaypointByName("Player_2_Start") : nullptr;
	REQUIRE(wa != nullptr);
	REQUIRE(wb != nullptr);
	const Coord3D home{ wa->location.x + (wb->location.x - wa->location.x) * 0.15f, wa->location.y + (wb->location.y - wa->location.y) * 0.15f, 0.0f };
	const Coord3D away{ wa->location.x + (wb->location.x - wa->location.x) * 0.5f, wa->location.y + (wb->location.y - wa->location.y) * 0.5f, 0.0f };
	std::vector<ObjectID> ids;
	for (int i = 0; i < 300; ++i)
	{
		Object *u = game.createObject("GondorFighter", pa->getPlayerIndex(), Coord3D{ home.x + (float)(i % 20) * 15.0f, home.y + (float)(i / 20) * 15.0f, 0.0f }, 0.0f,
			&error);
		REQUIRE_MESSAGE(u, error);
		ids.push_back(u->getID());
	}
	for (int i = 0; i < 30; ++i)
	{
		REQUIRE(game.advance(0.2) == 1);
	}
	Pathfinder &pf = logic.aiWorld()->pathfinder();
	REQUIRE(pf.requestRing().length() == (int)PATHFIND_QUEUE_LEN);
	logic.setSimulationProfile(SimulationProfile::Uber); // as a handed-over save switches it
	REQUIRE(game.advance(0.2) == 1);
	REQUIRE(pf.requestRing().length() == (int)Pathfinder::kUberQueueLength);
	for (int k = 0; k < 3; ++k)
	{
		game.commands().append(selectMsg(pa->getPlayerIndex(), ids));
		GameMessage mv(MSG_DO_MOVETO, pa->getPlayerIndex());
		mv.appendLocationArgument(k % 2 == 0 ? away : home);
		game.commands().append(mv);
		for (int f = 0; f < 120 && (f < 2 || pf.queuedRequests() > 0); ++f)
		{
			REQUIRE(game.advance(0.2) == 1);
		}
	}
	REQUIRE(pf.requestRing().head() >= (int)PATHFIND_QUEUE_LEN); // tolerance: a precondition (past retail's ring; the exact head is the orders' own count)
	std::vector<std::uint8_t> snapshot;
	REQUIRE_MESSAGE(logic.saveSnapshot(snapshot, &error), error);
	const std::uint32_t savedHash = logic.computeStateHash();
	// the game goes back to Faithful (retail's rings) before the load, as a fresh game of the original setup would be
	logic.setSimulationProfile(SimulationProfile::Faithful);
	REQUIRE(game.advance(0.2) == 1);
	REQUIRE(pf.requestRing().length() == (int)PATHFIND_QUEUE_LEN);
	REQUIRE_MESSAGE(game.loadSnapshot(snapshot, &error), error);
	CHECK(game.logic().computeStateHash() == savedHash);
	CHECK(game.logic().aiWorld()->pathfinder().requestRing().length() == (int)Pathfinder::kUberQueueLength);
}
