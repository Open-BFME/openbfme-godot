// OpenBFME unit tests: lane BRIDGE-1, bridges. RotWK's and BFME2's bridges are WALK_ON_TOP_OF_WALL objects (RivBridge, ElvenBridge, GenericTreeBridge, ...):
// a WallBoundsMesh deck that ClassifyWall (RW 0x935FAA) puts on a wall layer and two RampMesh ends on the ramp layer (lane WALL-1's code, community PR #63).
// These cases cross retail bridges on retail maps; they SKIP loudly without the installs. GPL-3.0.
#include "doctest.h"
#include "CampaignTestUtil.h"
#include "StartTestUtil.h"
#include "GameClient/HudInput.h"
#include "GameClient/HudObjects.h"

#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameLogic/NewGame/NewGame.h"

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
using campaigntest::Mission;

Player *humanOf(Mission &m)
{
	for (int i = 0; i < m.game->players().getPlayerCount(); ++i)
	{
		Player *p = m.game->players().getNthPlayer(i);
		if (p && p->getPlayerType() == PLAYER_HUMAN)
		{
			return p;
		}
	}
	return nullptr;
}

struct Walk
{
	bool arrived = false;
	bool sawRamp = false;
	bool sawWall = false;
	float maxZOverGround = 0.0f;
	int frames = 0;
};

// orders `o` to `to` (a player's move) and steps until it stands within 30 of it or `maxFrames` pass; records the layers it took
Walk walkTo(Mission &m, Object &o, const Coord3D &to, int maxFrames)
{
	Walk w;
	AIUpdateInterface *ai = o.getAIUpdateInterface();
	REQUIRE(ai);
	ai->aiMoveToPosition(to, CMD_FROM_PLAYER);
	for (; w.frames < maxFrames; ++w.frames)
	{
		m.step();
		const PathfindLayerEnum l = ai->adapter().getLayer();
		w.sawRamp = w.sawRamp || l == LAYER_RAMP;
		w.sawWall = w.sawWall || isWallLayer(l);
		const Coord3D p = *o.getPosition();
		const float over = p.z - m.logic().getGroundHeight(p.x, p.y);
		w.maxZOverGround = over > w.maxZOverGround ? over : w.maxZOverGround;
		const float dx = p.x - to.x, dy = p.y - to.y;
		if (dx * dx + dy * dy < 900.0f && !ai->isMoving())
		{
			w.arrived = true;
			break;
		}
	}
	return w;
}
} // namespace

TEST_CASE("bridgeclick1 retail: a right click reaches the river bridge or Celduin boardwalk deck")
{
	OPENBFME_REQUIRE_START(s);
	const char *map = "map good rivendell";
	const char *pickedTemplate = "RivBridge";
	Coord3D start{ 3040.0f, 1520.0f, 0.0f };
	Coord3D target{ 2869.0f, 1703.0f, 0.0f };
	SUBCASE("Rivendell river bridge") {}
	SUBCASE("Celduin dock segment A")
	{
		map = "map good celduin";
		pickedTemplate = "CelduinDockSegmentA";
		start = { 2828.0f, 1445.0f, 0.0f };
		// The segment origin overlaps Outpost 1799; click the clear deck beside its footprint.
		target = { 2760.5f, 1450.5f, 0.0f };
	}
	Mission m;
	m.load(*s, map);
	m.step(2);
	Player *me = humanOf(m);
	REQUIRE(me);
	m.game->players().setLocalPlayer(me);
	m.logic().combat().setAutoAcquireEnabled(false);
	Pathfinder &pf = m.game->ai().pathfinder();
	target.z = pf.getHighestLayerHeight(target.x, target.y);
	start.z = pf.getHighestLayerHeight(start.x, start.y);
	const PathfindLayerEnum layer = pf.getLayerAt(target.x, target.y, target.z);
	INFO(map << " deck=" << target.x << "," << target.y << "," << target.z << " layer=" << (int)layer);
	if (std::string(map) == "map good rivendell")
	{
		REQUIRE(isWallLayer(layer));
	}
	else
	{
		// CelduinDockSegmentA is NO_COLLIDE scenery over mapped terrain, without a wall or raised mesh.
		REQUIRE(layer == LAYER_GROUND);
		const PathfindCell *cell = pf.getCell(layer, &target);
		REQUIRE(cell);
		REQUIRE(cell->getType() == PathfindCell::CELL_CLEAR);
		REQUIRE(cell->getObstacleID() == PATHFIND_INVALID_ID);
	}
	std::string error;
	Object *hero = m.game->createObject("ElvenGlorfindel", me->getPlayerIndex(), start, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	hero->getBodyModule()->setIndestructible(true);
	m.step(2);
	MouseSettings mouse;
	REQUIRE_MESSAGE(MouseSettings::load(*s->mount->fs, mouse, &error), error);
	INIEnvironment env;
	env.fileSystem = s->mount->fs.get();
	MetaMap meta;
	meta.registerBlocks(env);
	REQUIRE_MESSAGE(meta.load(env, "Data\\INI\\CommandMap.ini", &error), error);
	REQUIRE_MESSAGE(meta.load(env, "CommandMap.ini", &error), error);
	PinholeView view({ target.x, target.y - 420.0f, target.z + 520.0f }, target, 1024, 768, 0.7f);
	HudInput input(m.logic(), &m.game->ai(), view, m.game->commands(), mouse, meta);
	input.commandTranslator().setUseAlternateMouse(true); // The device applies retail's right-click-orders setting.
	std::vector<GameMessage> moves;
	input.setLogicMessageObserver([&](const GameMessage &msg) {
		if (msg.getType() == MSG_DO_MOVETO)
		{
			moves.push_back(msg);
		}
	});
	ICoord2D unitPixel;
	REQUIRE(view.worldToScreen(*hero->getPosition(), unitPixel));
	input.mouseButton(HudInput::Button::Left, true, unitPixel.x, unitPixel.y, 0, 900);
	input.update();
	input.mouseButton(HudInput::Button::Left, false, unitPixel.x, unitPixel.y, 0, 920);
	input.update();
	m.step();
	REQUIRE(input.ui().selected() == std::vector<ObjectID>{ hero->getID() });
	REQUIRE(me->selection() == input.ui().selected());
	// RW 0x81E60B: evaluating a plain location move does not exempt a locally controlled drawable or emit a command.
	CHECK(input.commandTranslator().evaluateContextCommand(hero, hero->getPosition(), EvaluateType::EvaluateOnly) == MSG_DO_MOVETO);
	CHECK(moves.empty());
	ICoord2D pixel;
	REQUIRE(view.worldToScreen(target, pixel));
	Coord3D picked;
	REQUIRE(view.screenToTerrain(pixel, m.logic(), picked));
	Object *hit = HudObjects::pickObject(input.context(), pixel);
	REQUIRE(hit);
	REQUIRE(hit->getTemplate()->getName() == pickedTemplate);
	CHECK(hit->isKindOfName("WALK_ON_TOP_OF_WALL") == isWallLayer(layer));
	CHECK(hit->isKindOfName("NO_COLLIDE") == (layer == LAYER_GROUND));
	CHECK(pf.getLayerAt(picked.x, picked.y, picked.z) == layer);
	MESSAGE("bridgeclick pick=" << picked.x << "," << picked.y << "," << picked.z << " object=" << hit->getTemplate()->getName());
	input.mouseMove(pixel.x, pixel.y);
	input.update();
	CHECK(input.ui().cursor() == MouseCursorName::Move);
	input.mouseButton(HudInput::Button::Right, true, pixel.x, pixel.y, 0, 1000);
	input.update();
	input.mouseButton(HudInput::Button::Right, false, pixel.x, pixel.y, 0, 1020);
	input.update();
	REQUIRE(moves.size() == 1);
	REQUIRE(moves.front().getArgumentCount() == 1); // RW 0x81DB0C emits XYZ, not a separate destination layer.
	const GameMessageArgument *location = moves.front().getArgument(0);
	REQUIRE(location->type == ARGUMENTDATATYPE_LOCATION);
	CHECK(location->location.x == picked.x);
	CHECK(location->location.y == picked.y);
	CHECK(location->location.z == picked.z);
	m.step();
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	const Coord3D goal = ai->stateMachine().goalPosition();
	CHECK(pf.getLayerAt(goal.x, goal.y, goal.z) == layer);
	MESSAGE("bridgeclick goal=" << goal.x << "," << goal.y << "," << goal.z);
	bool arrived = false;
	for (int frame = 0; frame < 1500; ++frame)
	{
		m.step();
		const Coord3D at = *hero->getPosition();
		const float dx = at.x - picked.x, dy = at.y - picked.y;
		// tolerance: the requested arrival radius is one pathfinding cell (10 world units), on the clicked surface, at rest.
		if (dx * dx + dy * dy <= 100.0f && ai->adapter().getLayer() == layer && !ai->isMoving())
		{
			arrived = true;
			break;
		}
	}
	const Coord3D at = *hero->getPosition();
	MESSAGE("bridgeclick finish=" << at.x << "," << at.y << "," << at.z << " layer=" << (int)ai->adapter().getLayer());
	CHECK(arrived);
}

TEST_CASE("bridge1 retail: on MAP GOOD Rivendell a hero walks over the river on RivBridge (ramp, deck, ramp) and on to the far bank")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map good rivendell");
	m.step(2);
	Pathfinder &pf = m.game->ai().pathfinder();
	int bridges = 0;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == "RivBridge")
		{
			++bridges;
			const PathfindWallGeometry *geo = m.game->ai().adapterFor(*o).getWallGeometry();
			REQUIRE(geo);
			MESSAGE("RivBridge at " << o->getPosition()->x << ", " << o->getPosition()->y << ": " << geo->walls.size() << " deck(s), " << geo->ramps.size() << " ramp(s)");
		}
	}
	CHECK(bridges == 1);
	for (const std::string &e : m.game->report().wallMeshProblems)
	{
		MESSAGE("wall mesh problem: " << e);
	}
	Player *me = humanOf(m);
	REQUIRE(me);
	// the near end of the bridge, where the campaign test's units stopped (about (3040, 1520)); the far bank beyond the far ramp's foot (the campaign
	// test's far end (2760, 1810) is still on the ramp, layer 0x10). The deck's centre is near (2869, 1702)
	const Coord3D nearEnd{ 3040.0f, 1520.0f, 0.0f };
	const Coord3D farBank{ 2690.0f, 1880.0f, 0.0f };
	REQUIRE(pf.getLayerAt(farBank.x, farBank.y) == LAYER_GROUND);
	REQUIRE(pf.getLayerAt(nearEnd.x, nearEnd.y) == LAYER_GROUND);
	std::string error;
	Object *hero = m.game->createObject("ElvenGlorfindel", me->getPlayerIndex(), nearEnd, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	hero->getBodyModule()->setIndestructible(true);
	m.step(2);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	Coord3D from = *hero->getPosition();
	Coord3D to = farBank;
	bool partial = true;
	Path *p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
	REQUIRE(p);
	std::string layers;
	for (const PathNode *n = p->getFirstNode(); n; n = n->getNext())
	{
		layers += " " + std::to_string((int)n->getLayer());
	}
	MESSAGE("path layers:" << layers << std::string(partial ? " (partial)" : ""));
	CHECK_FALSE(partial);
	delete p;
	const Walk w = walkTo(m, *hero, farBank, 1500);
	const Coord3D at = *hero->getPosition();
	INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " after " << w.frames << " steps, on layer " << (int)ai->adapter().getLayer());
	CHECK(w.arrived);
	CHECK(w.sawRamp);
	CHECK(w.sawWall);
	CHECK(w.maxZOverGround > 5.0f); // tolerance: up on the deck over the river bed, not wading through it
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK(at.z == m.logic().getGroundHeight(at.x, at.y));
	// and back over the river
	const Walk back = walkTo(m, *hero, nearEnd, 1500);
	CHECK(back.arrived);
	CHECK(back.sawWall);
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
}

TEST_CASE("bridge1 retail: on MAP GOOD Rivendell the mission's own starting elves (PlyrElves/Starting Units), put at the bridge's near end, cross RivBridge")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map good rivendell");
	m.step(2);
	const Team *starting = ScriptConditions::team(m.engine(), "PlyrElves/Starting Units");
	REQUIRE(starting != nullptr);
	std::vector<Object *> units = ScriptConditions::members(*starting);
	REQUIRE(!units.empty());
	// the team starts far from the river (about (300, 950)); put it at the bridge's near end, where the campaign test's units stopped
	const Coord3D nearEnd{ 3040.0f, 1520.0f, 0.0f };
	const Coord3D farBank{ 2620.0f, 1950.0f, 0.0f }; // on the bank, past the far ramp's foot (the ramp reaches about (2745, 1885))
	for (Object *o : units)
	{
		if (o->getBodyModule())
		{
			o->getBodyModule()->setIndestructible(true);
		}
		if (!o->getContainedBy())
		{
			m.place(*o, nearEnd);
		}
	}
	m.step(2);
	auto near = [&](const Object &o) {
		const Coord3D p = *o.getPosition();
		const float dx = p.x - farBank.x, dy = p.y - farBank.y;
		return dx * dx + dy * dy < 150.0f * 150.0f; // tolerance: a horde's members stand in its formation around the goal
	};
	REQUIRE(m.game->ai().pathfinder().getLayerAt(farBank.x, farBank.y) == LAYER_GROUND);
	bool sawWall = false;
	const bool done = m.until(
		[&] {
			bool all = true;
			for (Object *o : units)
			{
				AIUpdateInterface *ai = o->getAIUpdateInterface();
				if (o->isEffectivelyDead() || o->getContainedBy() || !ai)
				{
					continue;
				}
				sawWall = sawWall || isWallLayer(ai->adapter().getLayer());
				if (ai->isIdle() && !near(*o) && m.frame() % 10 == 0)
				{
					ai->aiMoveToPosition(farBank, CMD_FROM_PLAYER);
				}
				all = all && near(*o);
				if (o->getContain())
				{
					const Coord3D q = *o->getPosition();
					const float hx = q.x - farBank.x, hy = q.y - farBank.y;
					all = all && hx * hx + hy * hy < 80.0f * 80.0f; // tolerance: the horde's centre within its close-enough distance of the goal
				}
			}
			return all;
		},
		1500);
	for (Object *o : units)
	{
		const Coord3D p = *o->getPosition();
		MESSAGE(o->getTemplate()->getName() << " at " << p.x << ", " << p.y << ", " << p.z);
	}
	CHECK(done);
	CHECK(sawWall);
	// a few more frames for the formation to settle, then every unit, the horde and each of its soldiers, stands on the ground of the far bank (not on the
	// far ramp, layer 0x10)
	m.step(30);
	Pathfinder &pf = m.game->ai().pathfinder();
	int soldiers = 0;
	for (Object *o : units)
	{
		std::vector<Object *> all{ o };
		if (o->getContain() && o->getContain()->getContainedItemsList())
		{
			all.insert(all.end(), o->getContain()->getContainedItemsList()->begin(), o->getContain()->getContainedItemsList()->end());
		}
		for (Object *u : all)
		{
			if (u->isEffectivelyDead())
			{
				continue;
			}
			soldiers += u != o ? 1 : 0;
			const Coord3D p = *u->getPosition();
			INFO(u->getTemplate()->getName() << " " << u->getID() << " at " << p.x << ", " << p.y << ", " << p.z);
			AIUpdateInterface *ai = u->getAIUpdateInterface();
			REQUIRE(ai);
			CHECK(ai->adapter().getLayer() == LAYER_GROUND);
			CHECK(pf.getLayerAt(p.x, p.y) == LAYER_GROUND);
			CHECK(near(*u));
		}
	}
	CHECK(soldiers >= 12); // tolerance: the starting horde's soldiers (12 when the test was written)
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// retail skirmish maps: every walk-on bridge with a deck and two ramps is crossed from one ramp's foot to the other's
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
using namespace starttest;

struct SkirmishGame
{
	Shared &s;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	SkirmishGame(Shared &sh, const std::string &mapName) : s(sh), source(*sh.mount->fs), assets(source)
	{
		NewGameMessage m;
		m.game.mapName = mapName;
		m.game.seed = 11;
		m.game.startingCash = 1000;
		const char *factions[2] = { "FactionMen", "FactionMordor" };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishGameSlot &sl = m.game.slots[i];
			sl.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
			sl.name = i == 0 ? u"Host" : u"Easy";
			sl.accepted = true;
			sl.playerTemplate = -1;
			for (int t = 0; t < s.world->playerTemplates().getPlayerTemplateCount(); ++t)
			{
				const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(t);
				if (pt && pt->getName() == factions[i])
				{
					sl.playerTemplate = t;
				}
			}
			REQUIRE(sl.playerTemplate >= 0); // tolerance: the index of a found template (-1: not found)
			sl.startPos = i;
			sl.color = i;
		}
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			game->advance(0.2);
		}
	}
	Player *human()
	{
		for (int i = 0; i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			if (p && p->getPlayerType() == PLAYER_HUMAN)
			{
				return p;
			}
		}
		return nullptr;
	}
};

// the foot of a ramp: its lowest vertex, moved `out` further from the bridge's centre
Coord3D rampFoot(const PathfindWallSurface &ramp, const Coord3D &centre, float out)
{
	Coord3D low = ramp.triangles.front().v[0];
	for (const PathfindWallTriangle &t : ramp.triangles)
	{
		for (const Coord3D &v : t.v)
		{
			low = v.z < low.z ? v : low;
		}
	}
	const float dx = low.x - centre.x, dy = low.y - centre.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	return Coord3D{ low.x + dx / len * out, low.y + dy / len * out, 0.0f };
}

struct Crossing
{
	std::string name;
	Coord3D a, b;
};

// the bridges of the map with a deck and two ramps of their own, and their two ends
std::vector<Crossing> crossingsOf(SkirmishGame &g)
{
	std::vector<Crossing> out;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName().find("Bridge") == std::string::npos || !o->isKindOfName("WALK_ON_TOP_OF_WALL"))
		{
			continue;
		}
		const PathfindWallGeometry *geo = g.game->ai().adapterFor(*o).getWallGeometry();
		if (!geo || geo->walls.empty() || geo->ramps.size() != 2)
		{
			continue;
		}
		const Coord3D c = *o->getPosition();
		out.push_back({ o->getTemplate()->getName(), rampFoot(geo->ramps[0], c, 30.0f), rampFoot(geo->ramps[1], c, 30.0f) });
	}
	return out;
}

void crossBridges(const std::string &mapName, size_t expected)
{
	OPENBFME_REQUIRE_START(s);
	SkirmishGame g(*s, mapName);
	g.frames(2);
	Player *me = g.human();
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	const std::vector<Crossing> crossings = crossingsOf(g);
	CHECK(crossings.size() == expected);
	for (const std::string &e : g.game->report().wallMeshProblems)
	{
		MESSAGE("wall mesh problem: " << e);
	}
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		const PathfindWallGeometry *geo = o->getTemplate()->getName().find("Bridge") != std::string::npos ? g.game->ai().adapterFor(*o).getWallGeometry() : nullptr;
		if (!geo)
		{
			continue;
		}
		const Coord3D c = *o->getPosition();
		std::string d = o->getTemplate()->getName() + " at " + std::to_string(c.x) + ", " + std::to_string(c.y) + " z " + std::to_string(c.z) + " terrain " +
			std::to_string(g.game->logic().getGroundHeight(c.x, c.y)) + " cell layer " + std::to_string((int)pf.getLayerAt(c.x, c.y));
		for (const PathfindWallSurface &w : geo->walls)
		{
			d += " | deck z " + std::to_string(w.height) + " box " + std::to_string(w.loX) + ".." + std::to_string(w.hiX) + " x " + std::to_string(w.loY) + ".." + std::to_string(w.hiY);
		}
		for (const PathfindWallSurface &r : geo->ramps)
		{
			const Coord3D f = rampFoot(r, c, 0.0f);
			d += " | ramp top " + std::to_string(r.height) + " foot " + std::to_string(f.x) + ", " + std::to_string(f.y) + " terrain there " +
				std::to_string(g.game->logic().getGroundHeight(f.x, f.y));
			float z = 0.0f;
			if (r.heightAt(f.x, f.y, z))
			{
				d += " ramp z there " + std::to_string(z);
			}
		}
		MESSAGE(d);
	}
	for (const Crossing &c : crossings)
	{
		INFO(mapName << ": " << c.name << " from " << c.a.x << ", " << c.a.y << " to " << c.b.x << ", " << c.b.y);
		CHECK(pf.getLayerAt(c.a.x, c.a.y) == LAYER_GROUND);
		CHECK(pf.getLayerAt(c.b.x, c.b.y) == LAYER_GROUND);
		std::string error;
		Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), c.a, 0.0f, &error);
		REQUIRE_MESSAGE(hero, error);
		hero->getBodyModule()->setIndestructible(true);
		g.frames(2);
		AIUpdateInterface *ai = hero->getAIUpdateInterface();
		REQUIRE(ai);
		{
			Coord3D from = *hero->getPosition(), to = c.b;
			bool partial = true;
			Path *p = pf.findPath(&ai->adapter(), ai->locomotorInfo(), &from, &to, &partial);
			std::string layers;
			for (const PathNode *n = p ? p->getFirstNode() : nullptr; n; n = n->getNext())
			{
				layers += " " + std::to_string((int)n->getLayer()) + "@" + std::to_string((int)n->getPosition()->x) + "," + std::to_string((int)n->getPosition()->y);
			}
			MESSAGE(c.name << " path:" << layers << std::string(partial ? " (partial)" : ""));
			delete p;
		}
		ai->aiMoveToPosition(c.b, CMD_FROM_PLAYER);
		bool sawWall = false, arrived = false;
		for (int f = 0; f < 600 && !arrived; ++f)
		{
			g.frames(1);
			sawWall = sawWall || isWallLayer(ai->adapter().getLayer());
			const Coord3D p = *hero->getPosition();
			const float dx = p.x - c.b.x, dy = p.y - c.b.y;
			arrived = dx * dx + dy * dy < 900.0f && !ai->isMoving();
		}
		const Coord3D at = *hero->getPosition();
		INFO("the hero stands at " << at.x << ", " << at.y << ", " << at.z << " on layer " << (int)ai->adapter().getLayer());
		CHECK(arrived);
		CHECK(sawWall);
		CHECK(ai->adapter().getLayer() == LAYER_GROUND);
		g.game->logic().destroyObject(hero);
		g.frames(1);
	}
}
} // namespace

TEST_CASE("bridge1 retail: MAP WOR Rivendell: a hero crosses each RivBridge from one ramp's foot to the other's, over the deck")
{
	crossBridges("maps/map wor rivendell/map wor rivendell.map", 2);
}

TEST_CASE("bridge1 retail: MAP WOR Erebor: a hero crosses each EreborEdgeBridge from one ramp's foot to the other's, over the deck")
{
	crossBridges("maps/map wor erebor/map wor erebor.map", 2);
}

TEST_CASE("bridge1 retail: MAP WOR Erebor: DaleBridge (meshes, no WALK_ON_TOP_OF_WALL) is no wall top or ramp: RW 0x936B7D calls ClassifyWall only for the KindOf")
{
	OPENBFME_REQUIRE_START(s);
	SkirmishGame g(*s, "maps/map wor erebor/map wor erebor.map");
	g.frames(2);
	Pathfinder &pf = g.game->ai().pathfinder();
	int dale = 0;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() != "DaleBridge")
		{
			continue;
		}
		++dale;
		CHECK_FALSE(o->isKindOfName("WALK_ON_TOP_OF_WALL"));
		const PathfindWallGeometry *geo = g.game->ai().adapterFor(*o).getWallGeometry();
		REQUIRE(geo); // the meshes are there (WallBoundsMesh P1, RampMesh1 R1, RampMesh2 R2) ...
		for (const PathfindWallSurface &w : geo->walls)
		{
			const float cx = (w.loX + w.hiX) * 0.5f, cy = (w.loY + w.hiY) * 0.5f;
			const PathfindCell *c = pf.cellAt((int)(cx / 10.0f), (int)(cy / 10.0f));
			REQUIRE(c);
			CHECK(c->getLayer() == LAYER_GROUND); // ... but the deck's cells stay on the ground layer
			CHECK(pf.getLayerAt(cx, cy) == LAYER_GROUND);
		}
	}
	CHECK(dale == 7);
	for (float h : pf.wallHeights())
	{
		CHECK(h > 302.0f); // tolerance: no wall height at DaleBridge's deck (300.6); EreborEdgeBridge's is 303.6
	}
}

TEST_CASE("bridge1 retail: MAP WOR Iron Hills: a hero crosses each IronHillsBridge01 from one ramp's foot to the other's")
{
	crossBridges("maps/map wor iron hills/map wor iron hills.map", 2);
}

// a point on the deck of `bridge` whose cell is a wall-top cell of that piece, and its layer (searched from the deck box's centre outward)
bool deckPoint(Pathfinder &pf, const PathfindWallSurface &deck, PathfindObjectID piece, Coord3D &out, PathfindLayerEnum &layer)
{
	const float cx = (deck.loX + deck.hiX) * 0.5f, cy = (deck.loY + deck.hiY) * 0.5f;
	for (int r = 0; r < 20; ++r)
	{
		for (int dx = -r; dx <= r; ++dx)
		{
			for (int dy = -r; dy <= r; ++dy)
			{
				const Coord3D p{ cx + (float)dx * 10.0f, cy + (float)dy * 10.0f, 0.0f };
				float z = 0.0f;
				const PathfindCell *c = pf.cellAt((int)(p.x / 10.0f), (int)(p.y / 10.0f));
				if (deck.heightAt(p.x, p.y, z) && c && isWallLayer(c->getLayer()) && c->getType() == PathfindCell::CELL_CLEAR && pf.wallPieceAt(p) == piece)
				{
					out = p;
					layer = c->getLayer();
					return true;
				}
			}
		}
	}
	return false;
}

TEST_CASE("bridge1 retail: on MAP MP Anfalas a GenericTreeBridge survives a kill (InactiveBody); deleted with a hero on its deck, the hero ends on the ground under it, alive")
{
	OPENBFME_REQUIRE_START(s);
	SkirmishGame g(*s, "maps/map mp anfalas/map mp anfalas.map");
	g.frames(2);
	Player *me = g.human();
	REQUIRE(me);
	Pathfinder &pf = g.game->ai().pathfinder();
	Object *bridge = nullptr;
	for (Object *o = g.game->logic().getFirstObject(); o && !bridge; o = o->getNextObject())
	{
		bridge = o->getTemplate()->getName() == "GenericTreeBridge" ? o : nullptr;
	}
	REQUIRE(bridge);
	const ObjectID bridgeID = bridge->getID();
	const PathfindWallGeometry *geo = g.game->ai().adapterFor(*bridge).getWallGeometry();
	REQUIRE(geo);
	REQUIRE(!geo->walls.empty());
	Coord3D onDeck;
	PathfindLayerEnum deckLayer = LAYER_INVALID;
	REQUIRE(deckPoint(pf, geo->walls.front(), (PathfindObjectID)bridgeID, onDeck, deckLayer));
	std::string error;
	Object *hero = g.game->createObject("GondorBoromir", me->getPlayerIndex(), onDeck, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	// on the deck's surface (createObject puts it on the terrain; a map's units on walls carry their height)
	onDeck.z = pf.getLayerHeight(onDeck.x, onDeck.y, deckLayer);
	hero->setPosition(&onDeck);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	ai->aiIdle(CMD_FROM_AI); // not the guard of its creation point on the terrain under the deck
	g.frames(3);
	const Coord3D before = *hero->getPosition();
	MESSAGE("the hero on the deck at " << before.x << ", " << before.y << ", " << before.z << " on layer " << (int)ai->adapter().getLayer() << " (terrain "
		<< g.game->logic().getGroundHeight(before.x, before.y) << ")");
	REQUIRE(ai->adapter().getLayer() == deckLayer);
	REQUIRE(before.z > g.game->logic().getGroundHeight(before.x, before.y) + 5.0f); // tolerance: up on the deck
	const float healthBefore = hero->getBodyModule()->getHealth();
	// a kill does nothing to it (RotWK's InactiveBody constructor sets the object's dead bit, RW 0x8C1A06 -> 0x68D950, and doAttemptDamage RW 0x697E50 skips
	// the body of a dead object): retail removes such a bridge only by deleting it (a script's delete, GameLogic::destroyObject)
	bridge->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	g.frames(150);
	REQUIRE(g.game->logic().findObjectByID(bridgeID) == bridge);
	CHECK(ai->adapter().getLayer() == deckLayer);
	g.game->logic().destroyObject(bridge);
	g.frames(1);
	CHECK(g.game->logic().findObjectByID(bridgeID) == nullptr);
	g.frames(5);
	const Coord3D after = *hero->getPosition();
	INFO("the hero at " << after.x << ", " << after.y << ", " << after.z << " on layer " << (int)ai->adapter().getLayer());
	CHECK_FALSE(hero->isEffectivelyDead());
	CHECK(hero->getBodyModule()->getHealth() == healthBefore);
	CHECK(ai->adapter().getLayer() == LAYER_GROUND);
	CHECK(after.z == g.game->logic().getGroundHeight(after.x, after.y));
	CHECK(pf.cellAt((int)(onDeck.x / 10.0f), (int)(onDeck.y / 10.0f))->getLayer() == LAYER_GROUND);
}
