// OpenBFME unit tests. GPL-3.0.
// Lane PATHPERF-1: the cost of a big move order. N hordes (or single infantry) of one player stand in a block on a large retail map; one
// MSG_CREATE_SELECTED_GROUP + MSG_DO_MOVETO (the right click on the ground) sends them across the map; every logic frame after it is timed (thread CPU).
// The bench is skipped by default (run it with --no-skip):
//   openbfme_tests -tc="pathperf1 bench*" --no-skip
// PATHPERF1_CASES="10,50,150,i300" picks the cases (default all four; iN = N single infantry), PATHPERF1_MAP the map (default map mp brown lands),
// PATHPERF1_FRAMES the frames after the order (default 60), PATHPERF1_SETTLE the frames before it (default 30).
#include "doctest.h"
#include "HudTestUtil.h"
#include "PerfBenchUtil.h"

#include "Common/Player.h"
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
