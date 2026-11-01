// OpenBFME unit tests. GPL-3.0.
// Lane TREES1: the map's trees as RotWK 2.01 runs them beyond the lumber mill's harvest (LUMBER1). A unit that is a crusher (crusher level 2 or more) or
// a MACHINE topples the trees it walks into (RW 0x6858BE / 0x6854FD); a structure a worker places removes the trees, shrubs and props under it (RW
// 0x684BCA); and the client's tree buffer sinks a tree
// whose lumber is gone where it lies (slot 0x7C, RW 0x4EA068), drops one a construction clears at once (slot 0x58) and plays the bounce effect 27 units up
// the trunk (RW 0xBE4CC4). Retail data on a retail map ("map mp fall back 4p", its trees); the rules are the binary's (GameLogic/Map/TerrainTrees.h,
// GameClient/TreeTopple.h).

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/TreeTopple.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
// a standing tree of the map with no other standing tree within `alone` and open ground `side` units to its west (the walker's start)
const TerrainTrees::Record *loneTree(hudtest::Rig &rig, float alone, float side)
{
	const std::vector<TerrainTrees::Record> &records = rig.logic().trees().records();
	for (const TerrainTrees::Record &t : records)
	{
		if (t.clientId == 0 || t.toppled)
		{
			continue;
		}
		bool lone = true;
		for (const TerrainTrees::Record &o : records)
		{
			if (&o != &t && o.clientId != 0 && std::hypot(o.position.x - t.position.x, o.position.y - t.position.y) < alone)
			{
				lone = false;
				break;
			}
		}
		if (lone && rig.clearGround(t.position.x - side, t.position.y, 20.0f) && rig.clearGround(t.position.x + side, t.position.y, 20.0f))
		{
			return &t;
		}
	}
	return nullptr;
}

size_t eventsOf(const TerrainTrees &trees, TerrainTrees::Event::Kind kind, std::uint32_t clientId)
{
	size_t n = 0;
	for (const TerrainTrees::Event &e : trees.events())
	{
		n += (e.kind == kind && (clientId == 0 || e.clientId == clientId)) ? 1 : 0;
	}
	return n;
}

bool kindOf(const Object &o, const char *name)
{
	const int k = ObjectTemplateInfoBuilder::kindOfIndex(name);
	REQUIRE(k >= 0); // tolerance: kindOfIndex answers -1 for a name it does not know, any other value is the bit
	return o.isKindOf((unsigned)k);
}

// RW 0x6858BE's rule for an object that reaches it
bool crushesTrees(const Object &o)
{
	return !kindOf(o, "IMMOBILE") && (ObjectCrush::crusherLevel(o) > 1 || kindOf(o, "MACHINE")) && !kindOf(o, "ARMY_OF_DEAD");
}

// walks `o` in 5-unit steps from 60 west of the tree to 60 east of it (each step is a transform change: Object's cell test RW 0x69264D)
void walkThrough(Object *o, const Coord3D &tree, hudtest::Rig &rig)
{
	for (float dx = -60.0f; dx <= 60.0f; dx += 5.0f)
	{
		Coord3D p{ tree.x + dx, tree.y, 0.0f };
		p.z = rig.logic().getGroundHeight(p.x, p.y);
		o->setPosition(&p);
	}
}
} // namespace

TEST_CASE("TREES1 retail: a crusher (crusher level 2 or more) or a MACHINE walking into a tree topples it away from itself; other units walk past")
{
	if (!hudtest::haveWorld("TREES1 crush"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	// retail units: orc and uruk infantry (crusher level 0), the mountain troll (2), the oliphaunt (4), siege engines (MACHINE: the catapult's crusher level
	// is 0, the battering ram's not set)
	const char *const names[] = { "MordorFighter", "IsengardFighter", "MordorMountainTroll", "MordorMumakil", "MordorCatapult", "IsengardBatteringRam" };
	int toppled = 0, walkedPast = 0;
	for (const char *name : names)
	{
		const TerrainTrees::Record *tree = loneTree(rig, 60.0f, 70.0f);
		REQUIRE_MESSAGE(tree, "no lone standing tree with open ground beside it is left on the map");
		const std::uint32_t id = tree->clientId;
		const Coord3D at = tree->position;
		// counted before the unit is made: the oliphaunt's footprint (its bounding circle) already reaches the tree where it is placed
		const size_t before = eventsOf(rig.logic().trees(), TerrainTrees::Event::TOPPLE, id);
		const int value = tree->value;
		Object *o = rig.make(name, at.x - 60.0f, at.y);
		REQUIRE_MESSAGE(o, name);
		const bool crusher = crushesTrees(*o);
		MESSAGE(std::string(name) << ": crusher level " << ObjectCrush::crusherLevel(*o) << ", MACHINE " << kindOf(*o, "MACHINE") << " -> topples trees " << crusher);
		walkThrough(o, at, rig);
		const TerrainTrees::Record *after = nullptr;
		for (const TerrainTrees::Record &t : rig.logic().trees().records())
		{
			after = t.clientId == id ? &t : after;
		}
		REQUIRE(after);
		CHECK_MESSAGE(after->toppled == crusher, name);
		CHECK(eventsOf(rig.logic().trees(), TerrainTrees::Event::TOPPLE, id) - before == (crusher ? 1u : 0u));
		if (crusher)
		{
			// RW 0x6849F8 with the source: the direction is the tree minus the object's position when it touched (west of the tree: it falls east)
			CHECK(after->toppleDirection.x > 0.0f); // tolerance: the walker touches the tree at whichever 5-unit step its geometry reaches it, always west of it
			CHECK(after->toppleDirection.y == 0.0f);
			CHECK(after->toppleDirection.z == 0.0f);
			CHECK(after->value == value); // a felled tree keeps its lumber (a worker may still take it)
			++toppled;
		}
		else
		{
			++walkedPast;
		}
		rig.logic().destroyObject(o);
		rig.frame(1);
	}
	CHECK(toppled == 4);    // the troll, the oliphaunt, the catapult, the battering ram
	CHECK(walkedPast == 2); // the orc and the uruk
}

TEST_CASE("TREES1 retail: an orc walking through a tree changes nothing; the logic hash covers a tree an oliphaunt crushed")
{
	if (!hudtest::haveWorld("TREES1 crush hash"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	const TerrainTrees::Record *tree = loneTree(rig, 60.0f, 70.0f);
	REQUIRE(tree);
	const Coord3D at = tree->position;
	CHECK_FALSE(rig.logic().trees().anyChanged());
	Object *orc = rig.make("MordorFighter", at.x - 60.0f, at.y);
	REQUIRE(orc);
	walkThrough(orc, at, rig);
	CHECK_FALSE(rig.logic().trees().anyChanged()); // nothing changed: the hash of a game without crushers is the one it had
	Object *mumak = rig.make("MordorMumakil", at.x - 60.0f, at.y);
	REQUIRE(mumak);
	REQUIRE(crushesTrees(*mumak));
	walkThrough(mumak, at, rig);
	CHECK(rig.logic().trees().anyChanged());
}

TEST_CASE("TREES1 retail: a structure a worker places on a tree removes it at once (no fall, no lumber) and clears the shrubs and props under it")
{
	if (!hudtest::haveWorld("TREES1 construction"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	rig.game->shroud().setDisplayed(false);
	const ThingTemplate *tt = rig.logic().things().findTemplate("MordorLumberMill");
	REQUIRE(tt);
	// a tree on ground where the mill may stand (the worker's legality test, flags 0x15)
	Object *worker = nullptr;
	Object *structure = nullptr;
	std::uint32_t treeId = 0;
	Coord3D site{};
	for (const TerrainTrees::Record &t : rig.logic().trees().records())
	{
		if (t.clientId == 0 || !rig.clearGround(t.position.x + 120.0f, t.position.y, 20.0f))
		{
			continue;
		}
		if (!worker)
		{
			worker = rig.make("MordorWorker", t.position.x + 120.0f, t.position.y);
			REQUIRE(worker);
		}
		else
		{
			Coord3D p{ t.position.x + 120.0f, t.position.y, rig.logic().getGroundHeight(t.position.x + 120.0f, t.position.y) };
			worker->setPosition(&p);
		}
		DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(worker->getAIUpdateInterface());
		REQUIRE(dozer);
		site = t.position;
		site.z = rig.logic().getGroundHeight(site.x, site.y);
		structure = dozer->construct(*tt->getFinalOverride(), site, 0.0f, *rig.local);
		if (structure)
		{
			treeId = t.clientId;
			break;
		}
	}
	REQUIRE_MESSAGE(structure, "no tree of the map is on ground where the lumber mill may be built");
	const ObjectID sid = structure->getID();
	CHECK(eventsOf(rig.logic().trees(), TerrainTrees::Event::REMOVE_NOW, treeId) == 0u); // the order alone removes nothing (RW 0x88C217)
	bool placed = false;
	for (int f = 0; f < 600 && !placed; ++f)
	{
		rig.frame(1);
		placed = eventsOf(rig.logic().trees(), TerrainTrees::Event::CLEAR_FOOTPRINT, 0) > 0;
	}
	REQUIRE_MESSAGE(placed, "the worker never placed the structure");
	REQUIRE(rig.logic().findObjectByID(sid));
	CHECK(eventsOf(rig.logic().trees(), TerrainTrees::Event::REMOVE_NOW, treeId) == 1u);
	CHECK(eventsOf(rig.logic().trees(), TerrainTrees::Event::TOPPLE, treeId) == 0u);
	CHECK(eventsOf(rig.logic().trees(), TerrainTrees::Event::REMOVE, treeId) == 0u);
	for (const TerrainTrees::Record &t : rig.logic().trees().records())
	{
		CHECK(t.clientId != treeId); // cleared (RW 0x67D3C2): no query finds it again
	}
	// the queries no longer see a tree at the site
	const TerrainTrees::Record *near = rig.logic().trees().findClosestTree(site, 1.0f, false, 0);
	CHECK((near == nullptr || near->clientId != treeId));
	// the footprint event carries the structure's own geometry at its position and angle (TheTerrainVisual slot 0x48)
	const TerrainTrees::Event *fp = nullptr;
	for (const TerrainTrees::Event &e : rig.logic().trees().events())
	{
		fp = e.kind == TerrainTrees::Event::CLEAR_FOOTPRINT ? &e : fp;
	}
	REQUIRE(fp);
	CHECK(fp->shapes.size() == ObjectGeometry::shapesOf(*tt->getFinalOverride()).size());
	CHECK(fp->position.x == structure->getPosition()->x);
	CHECK(fp->position.y == structure->getPosition()->y);
}

TEST_CASE("TREES1: removeTreesForConstruction takes every tree whose cylinder (35 high, radius 14) the geometry overlaps, in list order, and no other")
{
	if (!hudtest::haveWorld("TREES1 construction geometry"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared(), "map mp fall back 4p", "FactionMordor");
	auto ctx = hudtest::shared().world->enterContext();
	TerrainTrees &trees = rig.logic().trees();
	const TerrainTrees::Record *tree = loneTree(rig, 60.0f, 70.0f);
	REQUIRE(tree);
	const std::uint32_t id = tree->clientId;
	const Coord3D at = tree->position;
	std::vector<ObjectGeometry::Shape> box(1);
	box[0].type = ObjectGeometry::SHAPE_BOX;
	box[0].height = 10.0f;
	box[0].majorRadius = 10.0f;
	box[0].minorRadius = 10.0f;
	// a 20 x 20 box whose edge is 25 from the trunk: 25 > 14, no overlap
	CHECK(trees.removeTreesForConstruction(Coord3D{ at.x + 35.0f, at.y, at.z }, box, 0.0f) == 0);
	// 23 from the trunk: within the tree's radius 14 + the box's half size 10
	CHECK(trees.removeTreesForConstruction(Coord3D{ at.x + 23.0f, at.y, at.z }, box, 0.0f) == 1);
	CHECK(eventsOf(trees, TerrainTrees::Event::REMOVE_NOW, id) == 1u);
	CHECK(trees.anyChanged());
}

TEST_CASE("TREES1: the stop line reaches the report and STOPS")
{
	const std::string line = TerrainTrees::trees1StopLine();
	CHECK(line.rfind("[S-5260]", 0) == 0);
	bool found = false;
	for (const std::string &s : SpellBookPowers::stopLines())
	{
		found = found || s == line;
	}
	CHECK(found);
}

// ---- the client's tree buffer (no game data) -------------------------------------------------------------------------------------------------------------

namespace
{
W3DTreeDrawModuleData treeData()
{
	W3DTreeDrawModuleData d;
	d.m_initialVelocityPercent = 0.2f;
	d.m_initialAccelPercent = 0.01f;
	d.m_bounceVelocityPercent = 0.3f;
	d.m_minimumToppleSpeed = 0.5f;
	d.m_sinkFrames = 10; // SinkTime 2 s: 10 logic frames
	d.m_sinkDistance = 20.0f;
	d.m_bounceFX = "FX_TreeBounce";
	d.m_toppleFX = "FX_TreeTopple";
	return d;
}

void step(TreeTopple &tt, int updates)
{
	for (int i = 0; i < updates; ++i)
	{
		tt.advance(TreeTopple::kUpdateSeconds);
	}
}
} // namespace

TEST_CASE("TREES1 tree buffer: the lumber-gone call sinks a standing tree where it stands, SinkDistance / SinkTime per update for SinkTime x 6 updates")
{
	const W3DTreeDrawModuleData d = treeData();
	TreeTopple tt;
	tt.add(7, Coord3D{ 100.0f, 100.0f, 10.0f }, &d);
	tt.remove(7);
	CHECK(tt.trees()[0].state == TreeTopple::UPRIGHT); // no fall (RW 0x4EA068 starts the sink only)
	CHECK(tt.trees()[0].sinking);
	step(tt, 1);
	CHECK(tt.trees()[0].angle == 0.0f);
	CHECK(tt.trees()[0].sink == 2.0f); // 20 / 10
	step(tt, 59);                                       // 10 frames x 6 client frames per logic frame = 60 updates of sinking
	CHECK(tt.trees()[0].state == TreeTopple::UPRIGHT);
	step(tt, 1);
	CHECK(tt.trees()[0].state == TreeTopple::GONE);
	CHECK(tt.animating() == 0u);
}

TEST_CASE("TREES1 tree buffer: a felled tree whose lumber goes while it falls keeps falling and sinks; the construction call drops a tree at once")
{
	const W3DTreeDrawModuleData d = treeData();
	TreeTopple tt;
	tt.add(1, Coord3D{ 0.0f, 0.0f, 0.0f }, &d);
	tt.add(2, Coord3D{ 50.0f, 0.0f, 0.0f }, &d);
	REQUIRE(tt.topple(1, Coord3D{ 1.0f, 0.0f, 0.0f }, 0.0f));
	step(tt, 2);
	// MinimumToppleSpeed 0.5: velocity 0.1, acceleration 0.005; two steps
	const float v0 = 0.5f * 0.2f, acc = 0.5f * 0.01f;
	const float a = v0 + (v0 + acc);
	CHECK(tt.trees()[0].angle == a);
	tt.remove(1);
	step(tt, 1);
	CHECK(tt.trees()[0].angle == a + ((v0 + acc) + acc));
	CHECK(tt.trees()[0].sink == 2.0f);
	tt.removeNow(2);
	CHECK(tt.trees()[1].state == TreeTopple::GONE);
	CHECK_FALSE(tt.topple(2, Coord3D{ 1.0f, 0.0f, 0.0f }, 0.0f));
}

TEST_CASE("TREES1 tree buffer: the logic's topple speed is used when above 0, else MinimumToppleSpeed; the bounce effect plays 27 up the trunk")
{
	const W3DTreeDrawModuleData d = treeData();
	TreeTopple tt;
	tt.add(1, Coord3D{ 0.0f, 0.0f, 0.0f }, &d);
	tt.add(2, Coord3D{ 0.0f, 0.0f, 0.0f }, &d);
	REQUIRE(tt.topple(1, Coord3D{ 0.0f, 1.0f, 0.0f }, 0.1f)); // Devastation's 0.1 (below the minimum 0.5: still 0.1)
	REQUIRE(tt.topple(2, Coord3D{ 0.0f, 1.0f, 0.0f }, 0.0f));
	CHECK(tt.trees()[0].angularVelocity == 0.1f * 0.2f);
	CHECK(tt.trees()[1].angularVelocity == 0.5f * 0.2f);
	std::vector<TreeTopple::FX> fx = tt.takeFX();
	REQUIRE(fx.size() == 2u);
	CHECK(fx[0].name == "FX_TreeTopple");
	bool bounced = false;
	for (int i = 0; i < 400 && tt.trees()[1].state == TreeTopple::FALLING; ++i)
	{
		step(tt, 1);
		for (const TreeTopple::FX &f : tt.takeFX())
		{
			if (f.name == "FX_TreeBounce" && !bounced)
			{
				bounced = true;
				// at the limit angle PI/2 - PI/64, 27 along the trunk lying towards +y
				// tolerance: the accumulated angle reaches the limit through float sums (one ulp either side)
				CHECK(f.position.y == doctest::Approx(27.0f * std::sin(TreeTopple::kAngularLimit)));
				// tolerance: as above
				CHECK(f.position.z == doctest::Approx(27.0f * std::cos(TreeTopple::kAngularLimit)));
			}
		}
	}
	CHECK(bounced);
	CHECK(tt.trees()[1].state == TreeTopple::DOWN);
}
