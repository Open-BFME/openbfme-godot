// OpenBFME unit tests: ThePartitionManager (lane MODULES-2).
//
// Every expectation is derived by hand from the RotWK binary's algorithm (GameLogic/Object/PartitionManager.h cites the addresses): the cell arithmetic of
// RW 0xA3AD30 / 0xA3AF50, the head insertion of RW 0xA3B220, the dirty update of RW 0xA3B4E0, the layer order and the pre-order walk of RW 0xA3C4E0 /
// 0xA3A860, the STLport introsort of RW 0xA3A6B0 and the closest-object search of RW 0xA3BDB0.

#include "doctest.h"
#include "LogicTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Peg\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 3\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"Object Big\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 50\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"Object Mine\n"
	"  KindOf = MINE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 3\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"Object Bolt\n"
	"  KindOf = PROJECTILE\n"
	"End\n"
	"Object Ghost\n"
	"  KindOf = INERT\n"
	"End\n"
	"Object Ball\n"
	"  Geometry = SPHERE\n"
	"  GeometryMajorRadius = 4\n"
	"  GeometryOffset = X:0 Y:0 Z:6\n"
	"End\n";

struct Fx : LogicWorld
{
	Fx()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		// a 1280 x 1280 map: 128 cells of 10 units
		logic->partition().setRegion(0.0f, 0.0f, 1280.0f, 1280.0f);
	}
	Object *at(const char *name, float x, float y, const char *owner = "Alice")
	{
		Object *o = make(name, teamOf(owner));
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	std::vector<ObjectID> ids(const std::vector<PartitionHit> &hits)
	{
		std::vector<ObjectID> out;
		for (const PartitionHit &h : hits)
		{
			out.push_back(h.object->getID());
		}
		return out;
	}
	std::vector<ObjectID> scan(float x, float y, float r, IterOrderType order = ITER_FASTEST, DistanceCalculationType type = FROM_CENTER_2D)
	{
		return ids(logic->partition().iterateObjectsInRange(Coord3D{ x, y, 0.0f }, r, type, {}, order));
	}
};
} // namespace

TEST_CASE("partition: an object's cells and size mask follow RW 0xA3AF50 (bounding circle, highest differing bit cleared); its layer is its player (RW 0x68EE53)")
{
	Fx f;
	PartitionManager &pm = f.logic->partition();
	Object *o = f.at("Peg", 105.0f, 207.0f);
	f.logic->runLogicFrame(); // the transform change re-links in the partition row
	int layer = -2;
	std::uint32_t x = 0, y = 0, mask = 0;
	REQUIRE(pm.entryCells(*o, layer, x, y, mask));
	// x: 102 / 108 -> cells 10 / 10; y: 204 / 210 -> 20 / 21; mask = 20 ^ 21 = 1, bit 0 cleared from both corners
	CHECK(x == 10);
	CHECK(y == 20);
	CHECK(mask == 1);
	CHECK(layer == f.players.findPlayerWithName("Alice")->getPlayerIndex());
	// a MINE is in layer -1; INERT and PROJECTILE templates never enter (RW 0x68E31F)
	Object *m = f.at("Mine", 300.0f, 300.0f);
	CHECK(pm.layerOf(*m) == -1);
	CHECK_FALSE(pm.isRegistered(*f.at("Bolt", 10.0f, 10.0f)));
	CHECK_FALSE(pm.isRegistered(*f.at("Ghost", 10.0f, 10.0f)));
	CHECK(pm.entryCount() == 2);
	// geometry + 0x10 / + 0x14 / + 0x20 of the sphere lifted by 6 (RW 0xAD2860 / 0xAD2040): circle 4, sphere 6 + 4 = 10, centre height (0 + 10) / 2
	const PartitionManager::Geometry &g = pm.geometryOf(*f.at("Ball", 50.0f, 50.0f));
	CHECK(g.circle == doctest::Approx(4.0f));
	CHECK(g.sphere == doctest::Approx(10.0f));
	CHECK(g.centerZ == doctest::Approx(5.0f));
}

TEST_CASE("partition: a node's list is newest first; a move re-links only in the partition update and only when the cells change (RW 0xA3B4E0)")
{
	Fx f;
	PartitionManager &pm = f.logic->partition();
	Object *a = f.at("Peg", 405.0f, 405.0f);
	Object *b = f.at("Peg", 405.0f, 405.0f);
	Object *c = f.at("Peg", 405.0f, 405.0f);
	f.logic->runLogicFrame();
	// made at the origin, then moved: marked a, b, c; the update takes c first (the dirty list's head), so a is linked last, at the head
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ a->getID(), b->getID(), c->getID() });
	// a move inside the same cells: marked, not re-linked
	const Coord3D same{ 406.0f, 404.0f, 0.0f };
	a->setPosition(&same);
	CHECK(pm.dirtyCount() == 1);
	f.logic->runLogicFrame();
	CHECK(pm.dirtyCount() == 0);
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ a->getID(), b->getID(), c->getID() });
	// away and back within one frame: the cells end where they were, nothing moves
	const Coord3D far{ 1000.0f, 1000.0f, 0.0f };
	const Coord3D back{ 405.0f, 405.0f, 0.0f };
	a->setPosition(&far);
	a->setPosition(&back);
	f.logic->runLogicFrame();
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ a->getID(), b->getID(), c->getID() });
	// away for one update and back: re-linked at the head of its node
	b->setPosition(&far);
	f.logic->runLogicFrame();
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ a->getID(), c->getID() });
	b->setPosition(&back);
	// before the update the tree still has the old cell (the query's cell rectangle does not reach it)
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ a->getID(), c->getID() });
	f.logic->runLogicFrame();
	CHECK(f.scan(405.0f, 405.0f, 50.0f) == std::vector<ObjectID>{ b->getID(), a->getID(), c->getID() });
	// the dirty list is taken from its head: b then c marked, c is re-linked first, so b ends at the head
	const Coord3D other{ 805.0f, 805.0f, 0.0f };
	b->setPosition(&other);
	c->setPosition(&other);
	f.logic->runLogicFrame();
	CHECK(f.scan(805.0f, 805.0f, 50.0f) == std::vector<ObjectID>{ b->getID(), c->getID() });
}

TEST_CASE("partition: the layers are walked in order (layer -1, then players 0 .. 19), each tree in pre-order with the children 0, 1, 2, 3 (RW 0xA3C4E0 / 0xA3A860)")
{
	Fx f;
	// four objects in the four quadrants around the map's centre, made in the reverse order of the walk
	Object *q3 = f.at("Peg", 690.0f, 690.0f);
	Object *q2 = f.at("Peg", 590.0f, 690.0f);
	Object *q1 = f.at("Peg", 690.0f, 590.0f);
	Object *q0 = f.at("Peg", 590.0f, 590.0f);
	// a big object on the centre (cells 59 .. 69: the highest differing bit is 64, so it stays in the root's list)
	Object *big = f.at("Big", 640.0f, 640.0f);
	// a mine (layer -1) and an object of the other player (a later layer), made first
	Object *mine = f.at("Mine", 650.0f, 650.0f);
	Object *bob = f.at("Peg", 630.0f, 630.0f, "Bob");
	f.logic->runLogicFrame();
	int layer = 0;
	std::uint32_t x = 0, y = 0, mask = 0;
	REQUIRE(f.logic->partition().entryCells(*big, layer, x, y, mask));
	CHECK(mask == 126); // 59 ^ 69: the highest bit is 64
	const int alice = f.players.findPlayerWithName("Alice")->getPlayerIndex();
	const int bobIndex = f.players.findPlayerWithName("Bob")->getPlayerIndex();
	REQUIRE(alice < bobIndex);
	CHECK(f.scan(640.0f, 640.0f, 200.0f) ==
	      std::vector<ObjectID>{ mine->getID(), big->getID(), q0->getID(), q1->getID(), q2->getID(), q3->getID(), bob->getID() });
	// a filter mask without Alice's layer skips her tree (RW 0xA39490); the mine's layer -1 is always walked
	struct OnlyBob : PartitionFilter
	{
		std::uint32_t bit;
		bool allow(Object &) override { return true; }
		std::uint32_t playerLayerMask() const override { return bit; }
	} onlyBob;
	onlyBob.bit = 1u << bobIndex;
	CHECK(f.ids(f.logic->partition().iterateObjectsInRange(Coord3D{ 640.0f, 640.0f, 0.0f }, 200.0f, FROM_CENTER_2D, { &onlyBob }, ITER_FASTEST)) ==
	      std::vector<ObjectID>{ mine->getID(), bob->getID() });
	// a team change marks the entry: the object moves to its new owner's tree in the next update (RW 0x69954A)
	q0->setTeam(f.teamOf("Bob"));
	f.logic->runLogicFrame();
	CHECK(f.scan(640.0f, 640.0f, 200.0f) ==
	      std::vector<ObjectID>{ mine->getID(), big->getID(), q1->getID(), q2->getID(), q3->getID(), q0->getID(), bob->getID() });
}

TEST_CASE("partition: the range test keeps distance <= radius^2 on the live position; the bounding types subtract the radius (RW 0xDBDAF8)")
{
	Fx f;
	Object *near = f.at("Peg", 500.0f, 500.0f);
	Object *edge = f.at("Peg", 530.0f, 540.0f); // 50 from (500, 500)
	Object *out = f.at("Peg", 530.0f, 541.0f);
	f.logic->runLogicFrame();
	// near's node is the 4-cell block 48 .. 51, edge's the block 52 .. 55: child 0 before child 3 of their common parent
	CHECK(f.scan(500.0f, 500.0f, 50.0f) == std::vector<ObjectID>{ near->getID(), edge->getID() });
	(void)out;
	// FROM_BOUNDINGSPHERE_2D: (50.99 - 3)^2 <= 50^2 takes `out` too; `near` is inside its own circle: -(3^2)
	const std::vector<PartitionHit> hits =
		f.logic->partition().iterateObjectsInRange(Coord3D{ 500.0f, 500.0f, 0.0f }, 50.0f, FROM_BOUNDINGSPHERE_2D, {}, ITER_SORTED_NEAR_TO_FAR);
	REQUIRE(hits.size() == 3);
	CHECK(hits[0].object == near);
	CHECK(hits[0].distSqr == doctest::Approx(-9.0f));
	CHECK(hits[1].object == edge);
	CHECK(hits[1].distSqr == doctest::Approx(47.0f * 47.0f));
	CHECK(hits[2].object == out);
	// type 4 needs RW 0xAD2CE0: refused with the stop
	CHECK_THROWS(f.logic->partition().iterateObjectsInRange(Coord3D{ 500.0f, 500.0f, 0.0f }, 50.0f, FROM_GEOMETRY_OVERLAP, {}, ITER_FASTEST));
}

TEST_CASE("partition: the range walk's double-precision pre-reject changes no hit, order or distance (points packed on the boundaries, every type)")
{
	Fx f;
	PartitionManager &pm = f.logic->partition();
	const Coord3D c{ 640.0f, 640.0f, 2.0f };
	const float radius = 97.3f;
	std::vector<Object *> objs;
	unsigned seed = 12345u;
	auto next = [&]() {
		seed = seed * 1103515245u + 12345u;
		return (double)((seed >> 8) & 0xFFFFu) / 65536.0;
	};
	// the boundaries of the four types: the radius, plus the bounding circle (3) or the bounding sphere, each with relative offsets of a few float ulps
	Object *probe = f.at("Peg", 0.0f, 0.0f);
	f.logic->runLogicFrame();
	const double sphere = pm.geometryOf(*probe).sphere;
	const double centerZ = pm.geometryOf(*probe).centerZ;
	const double bounds[] = { radius, radius + 3.0, radius + sphere, radius * 0.5 };
	for (int k = 0; k < 480; ++k)
	{
		const double a = 6.283185307179586 * next();
		const double rel = ((double)(k % 41) - 20.0) * 3.0e-8;
		const double dist = bounds[k % 4] * (1.0 + rel);
		// dz 0 for the centre types, dz + centerZ 0 for the bounding sphere, else a random height
		const double z = ((k / 4) % 3 == 0) ? 0.0 : ((k / 4) % 3 == 1) ? -centerZ : (next() - 0.5) * 8.0;
		Object *o = f.at("Peg", (float)(640.0 + dist * std::cos(a)), (float)(640.0 + dist * std::sin(a)));
		const Coord3D p{ o->getPosition()->x, o->getPosition()->y, (float)(2.0 + z) };
		o->setPosition(&p);
		objs.push_back(o);
	}
	f.logic->runLogicFrame();
	const double r2 = (double)SimMath::pc24Mul(radius, radius);
	auto exact = [&](DistanceCalculationType type, const Object &o) {
		const Coord3D *p = o.getPosition();
		const PartitionManager::Geometry &g = pm.geometryOf(o);
		switch (type)
		{
			case FROM_CENTER_3D:
			{
				const double dz = SimMath::pc24SubW((double)p->z, (double)c.z);
				const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
				const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
				return SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)), SimMath::pc24MulW(dz, dz));
			}
			case FROM_BOUNDINGSPHERE_2D:
			{
				const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
				const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
				const float s = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)));
				const double d = SimMath::pc24SubW(SimMath::sqrtPC24((double)s), (double)g.circle);
				const double sq = SimMath::pc24MulW(d, d);
				return d < 0.0 ? -sq : sq;
			}
			case FROM_BOUNDINGSPHERE_3D:
			{
				const float dx = SimMath::pc24Sub(p->x, c.x);
				const float dy = SimMath::pc24Sub(p->y, c.y);
				const float dz = SimMath::pc24Add(g.centerZ, SimMath::pc24Sub(p->z, c.z));
				double s = SimMath::pc24AddW(SimMath::pc24MulW((double)dy, (double)dy), SimMath::pc24MulW((double)dx, (double)dx));
				s = SimMath::pc24AddW(s, SimMath::pc24MulW((double)dz, (double)dz));
				const double d = SimMath::pc24SubW(SimMath::sqrtPC24(s), (double)g.sphere);
				const double sq = SimMath::pc24MulW(d, d);
				return d < 0.0 ? -sq : sq;
			}
			default:
			{
				const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
				const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
				return SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy));
			}
		}
	};
	for (DistanceCalculationType type : { FROM_CENTER_2D, FROM_CENTER_3D, FROM_BOUNDINGSPHERE_2D, FROM_BOUNDINGSPHERE_3D })
	{
		size_t nearBoundary = 0;
		for (Object *o : objs)
		{
			const double v = exact(type, *o);
			nearBoundary += (v > r2 * 0.9999 && v < r2 * 1.0001) ? 1u : 0u;
		}
		for (IterOrderType order : { ITER_FASTEST, ITER_SORTED_NEAR_TO_FAR })
		{
			pm.setPreReject(false);
			const std::vector<PartitionHit> off = pm.iterateObjectsInRange(c, radius, type, {}, order);
			pm.setPreReject(true);
			const std::vector<PartitionHit> on = pm.iterateObjectsInRange(c, radius, type, {}, order);
			INFO("type " << (int)type << " order " << (int)order << ": " << off.size() << " hits, " << nearBoundary << " candidates within 1e-4 of the boundary");
			CHECK(nearBoundary > 20);
			REQUIRE(on.size() == off.size());
			for (size_t i = 0; i < on.size(); ++i)
			{
				CHECK(on[i].object == off[i].object);
				CHECK(std::memcmp(&on[i].distSqr, &off[i].distSqr, sizeof(float)) == 0);
			}
		}
	}
}

TEST_CASE("partition: the STLport introsort (RW 0xA3A6B0): up to 16 equal keys keep their order, 17 or more are reversed by the Hoare partition")
{
	auto run = [](int n, IterOrderType order) {
		std::vector<PartitionHit> hits;
		std::vector<Object *> fake((size_t)n);
		for (int i = 0; i < n; ++i)
		{
			hits.push_back(PartitionHit{ reinterpret_cast<Object *>(static_cast<std::uintptr_t>(0x1000 + 16 * i)), 7.0f });
		}
		PartitionManager::sortHits(hits, order);
		std::vector<int> idx;
		for (const PartitionHit &h : hits)
		{
			idx.push_back((int)((reinterpret_cast<std::uintptr_t>(h.object) - 0x1000) / 16));
		}
		return idx;
	};
	std::vector<int> up16(16), down17(17), down20(20);
	for (int i = 0; i < 16; ++i)
	{
		up16[(size_t)i] = i;
	}
	for (int i = 0; i < 17; ++i)
	{
		down17[(size_t)i] = 16 - i;
	}
	for (int i = 0; i < 20; ++i)
	{
		down20[(size_t)i] = 19 - i;
	}
	CHECK(run(16, ITER_SORTED_NEAR_TO_FAR) == up16);
	CHECK(run(17, ITER_SORTED_NEAR_TO_FAR) == down17);
	CHECK(run(20, ITER_SORTED_FAR_TO_NEAR) == down20);
	CHECK(run(20, ITER_FASTEST) != down20);
	// a real sort: ascending, then descending
	std::vector<PartitionHit> hits;
	const float keys[] = { 5, 1, 9, 3, 3, 8, 2, 7, 6, 4, 0, 11, 10, 12, 15, 14, 13, 19, 18, 17, 16, 3 };
	for (float k : keys)
	{
		hits.push_back(PartitionHit{ nullptr, k });
	}
	PartitionManager::sortHits(hits, ITER_SORTED_NEAR_TO_FAR);
	CHECK(std::is_sorted(hits.begin(), hits.end(), [](const PartitionHit &a, const PartitionHit &b) { return a.distSqr < b.distSqr; }));
	PartitionManager::sortHits(hits, ITER_SORTED_FAR_TO_NEAR);
	CHECK(std::is_sorted(hits.begin(), hits.end(), [](const PartitionHit &a, const PartitionHit &b) { return a.distSqr > b.distSqr; }));
}

TEST_CASE("partition: getClosestObject (RW 0xA3BDB0) finds the nearest accepted object; an equal distance later in a node's list wins (fcomp <=)")
{
	Fx f;
	PartitionManager &pm = f.logic->partition();
	Object *a = f.at("Peg", 300.0f, 305.0f);
	Object *b = f.at("Peg", 300.0f, 295.0f); // the same distance (5) from (300, 300) and the same node: the first update links b, then a at the head
	Object *c = f.at("Peg", 360.0f, 300.0f);
	Object *d = f.at("Peg", 900.0f, 300.0f, "Bob");
	f.logic->runLogicFrame();
	float dist = 0.0f;
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, {}, nullptr, &dist) == b);
	CHECK(dist == doctest::Approx(25.0f));
	PartitionFilterFn notAB([&](Object &o) { return &o != a && &o != b; });
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, { &notAB }) == c);
	PartitionFilterFn onlyBob([&](Object &o) { return o.getControllingPlayer() == f.players.findPlayerWithName("Bob"); });
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, { &onlyBob }) == nullptr);
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 700.0f, FROM_CENTER_2D, { &onlyBob }) == d);
	// a distance below 1e-5 finds nothing (RW 0xA3BF21)
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 305.0f, 0.0f }, 0.0f, FROM_CENTER_2D, {}) == nullptr);
	// a rectangle restricts the positions
	PartitionRegion r{ 340.0f, 280.0f, 400.0f, 320.0f };
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, {}, &r) == c);
}

TEST_CASE("partition: without a map's region getClosestObject's range clamps to 0, yet a coincident or overlapping candidate is still returned (S-1020, Sol review)")
{
	LogicWorld w;
	REQUIRE_MESSAGE(w.loadError.empty(), w.loadError);
	const std::string err = w.w.load(kObjects);
	REQUIRE_MESSAGE(err.empty(), err);
	auto at = [&](const char *name, float x, float y) {
		Object *o = w.make(name, w.teamOf("Alice"));
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	};
	Object *peg = at("Peg", 300.0f, 300.0f);
	Object *big = at("Big", 600.0f, 300.0f);
	at("Peg", 900.0f, 300.0f);
	w.logic->runLogicFrame();
	PartitionManager &pm = w.logic->partition();
	// the empty region's scale is 1 / 0: the 32766-cell clamp makes the range 0 (RW 0xA3BF3C), and a candidate is skipped only when its distance is above it
	CHECK(pm.getClosestObject(Coord3D{ 300.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, {}) == peg);
	CHECK(pm.getClosestObject(Coord3D{ 305.0f, 300.0f, 0.0f }, 500.0f, FROM_CENTER_2D, {}) == nullptr);
	// the bounding types subtract the radius: Big (50) 30 away overlaps, a Peg (3) 30 away does not
	CHECK(pm.getClosestObject(Coord3D{ 630.0f, 300.0f, 0.0f }, 500.0f, FROM_BOUNDINGSPHERE_2D, {}) == big);
	CHECK(pm.getClosestObject(Coord3D{ 330.0f, 300.0f, 0.0f }, 500.0f, FROM_BOUNDINGSPHERE_2D, {}) == nullptr);
}

TEST_CASE("partition: the state hash covers the link order; a region change re-inserts newest first; unregistering leaves nothing (RW 0xA3B450 / 0xA3B610)")
{
	Fx f;
	PartitionManager &pm = f.logic->partition();
	Object *a = f.at("Peg", 405.0f, 405.0f);
	Object *b = f.at("Peg", 405.0f, 405.0f);
	f.logic->runLogicFrame();
	CHECK(f.scan(405.0f, 405.0f, 20.0f) == std::vector<ObjectID>{ a->getID(), b->getID() });
	StateHasher h1;
	pm.crc(h1);
	// the same objects re-linked in the other order hash differently
	const Coord3D far{ 1000.0f, 1000.0f, 0.0f };
	const Coord3D back{ 405.0f, 405.0f, 0.0f };
	b->setPosition(&far);
	f.logic->runLogicFrame();
	b->setPosition(&back);
	f.logic->runLogicFrame();
	CHECK(f.scan(405.0f, 405.0f, 20.0f) == std::vector<ObjectID>{ b->getID(), a->getID() });
	StateHasher h2;
	pm.crc(h2);
	CHECK(h1.value() != h2.value());
	// a new region: every entry re-inserted in the entry list's order (newest first: b, then a), so a ends at the head
	pm.setRegion(0.0f, 0.0f, 2560.0f, 2560.0f);
	CHECK(f.scan(405.0f, 405.0f, 20.0f) == std::vector<ObjectID>{ a->getID(), b->getID() });
	pm.setRegion(0.0f, 0.0f, 1280.0f, 1280.0f);
	StateHasher h3;
	pm.crc(h3);
	CHECK(h1.value() == h3.value());
	f.logic->destroyObject(a);
	f.logic->runLogicFrame();
	CHECK(pm.entryCount() == 1);
	CHECK(f.scan(405.0f, 405.0f, 20.0f) == std::vector<ObjectID>{ b->getID() });
}

TEST_CASE("partition: stop S-1020 is in the logic report (template geometry, no defector layer, distance type 4 refused, the empty region without a map)")
{
	Fx f;
	const GameLogic::Report r = f.logic->report();
	bool found = false;
	for (const std::string &s : r.stops)
	{
		found = found || (s.rfind("[S-1020] partition:", 0) == 0 && s.find("RW 0xAD2CE0") != std::string::npos);
	}
	CHECK(found);
	CHECK(PartitionManager::stopLines().size() == 1);
}
