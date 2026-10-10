// OpenBFME retail tests, lane HUD-5 (the owner's report: clicking a gate on Helm's Deep does not open it). GPL-3.0.
//
// GateOpenAndCloseBehavior (RW 0x89CC29), MSG_OPEN_GATE / MSG_CLOSE_GATE (RW 0x77BF7B / 0x77C037) and AIGateUpdate (RW 0x8B4D34) on the retail fortress maps:
// the gate's states and timing, its geometry in the pathfinder's map, the model conditions, the computer player's automatic opening for its own units, and the
// state hash (two runs agree, an opened gate changes it). The tests SKIP loudly without the installs.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <functional>
#include <string>
#include <vector>

namespace ModelCondition
{
int indexOf(const std::string &name);
}

using namespace starttest;

namespace
{
int templateIndex(Shared &s, const std::string &name)
{
	for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// slot 0 at start 0 (the fortress), slot 1 at start 1; `aiAtFortress`: slot 0 is a computer player, slot 1 the human
NewGameMessage gateMessage(Shared &s, const std::string &map, bool aiAtFortress)
{
	NewGameMessage m;
	m.game.mapName = map;
	m.game.seed = 9;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		const bool ai = aiAtFortress ? i == 0 : i == 1;
		sl.state = ai ? SLOT_EASY_AI : SLOT_PLAYER;
		sl.name = ai ? u"Easy" : u"Host";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(s, factions[i]);
		REQUIRE(sl.playerTemplate >= 0);
		sl.startPos = i;
		sl.color = i;
	}
	return m;
}

struct GateGame
{
	Shared &s;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	GateGame(Shared &sh, const NewGameMessage &message) : s(sh), source(*sh.mount->fs), assets(source)
	{
		std::string error;
		static std::vector<MapCacheEntry> cache;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
		}
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
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
	Object *named(const std::string &name)
	{
		for (Object *o = game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	}
	void send(int type, int player, ObjectID id)
	{
		GameMessage m(type, player);
		m.appendObjectIDArgument(id);
		game->commands().append(m);
	}
	// the gate's centre cell carries the gate as its obstacle
	bool blocksCentre(Object &gate)
	{
		Pathfinder &pf = game->ai().pathfinder();
		PathfindCell *cell = pf.getCell(LAYER_GROUND, gate.getPosition());
		return cell && cell->isObstaclePresent((PathfindObjectID)gate.getID());
	}
	bool shapeActive(Object &gate, const std::string &name)
	{
		const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*gate.getTemplate());
		for (size_t i = 0; i < shapes.size(); ++i)
		{
			if (shapes[i].name == name)
			{
				return i < gate.geometryActive().size() ? gate.geometryActive()[i] != 0 : shapes[i].active;
			}
		}
		return false;
	}
};
} // namespace

TEST_CASE("hud5 gates retail: Helm's Deep's main gate closes and opens by MSG_CLOSE_GATE / MSG_OPEN_GATE: states, timing, model conditions, geometry, pathfinder")
{
	OPENBFME_REQUIRE_START(s);
	GateGame g(*s, gateMessage(*s, "maps/map wor helms deep/map wor helms deep.map", false));
	g.frames(2);
	Object *gate = g.named("Main Gate");
	REQUIRE(gate);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	Player *owner = gate->getControllingPlayer();
	REQUIRE(owner);
	CHECK_FALSE(owner->isSkirmishAI()); // the human at start 1: AIGateUpdate stays off
	const int doorOpening = ModelCondition::indexOf("DOOR_1_OPENING");
	const int doorClosing = ModelCondition::indexOf("DOOR_1_CLOSING");
	// RBHelmsDeepGateDoorBig: OpenByDefault = Yes, ResetTimeInMilliseconds = 12200 (61 frames), TimeBeforePlaying*Sound = 6000 (30 frames)
	CHECK(b->state() == GateOpenAndCloseBehavior::OPEN);
	CHECK(b->isSettled());
	CHECK(b->geometryState() == GateOpenAndCloseBehavior::GEOMETRY_OPEN);
	CHECK(gate->testModelCondition(doorOpening));
	CHECK_FALSE(gate->testModelCondition(doorClosing));
	CHECK_FALSE(g.shapeActive(*gate, "Closed"));
	CHECK(g.shapeActive(*gate, "OpenLeft"));
	CHECK(g.shapeActive(*gate, "OpenRight"));
	CHECK_FALSE(g.blocksCentre(*gate));
	// MSG_OPEN_GATE on an open gate changes nothing
	g.send(MSG_OPEN_GATE, owner->getPlayerIndex(), gate->getID());
	g.frames(2);
	CHECK(b->state() == GateOpenAndCloseBehavior::OPEN);
	// close: closing for 61 frames, the closed geometry past 100 - PercentOpenForPathing (50 %)
	g.send(MSG_CLOSE_GATE, owner->getPlayerIndex(), gate->getID());
	g.frames(2);
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSING);
	CHECK_FALSE(b->isSettled());
	CHECK(gate->testModelCondition(doorClosing));
	CHECK_FALSE(gate->testModelCondition(doorOpening));
	g.send(MSG_OPEN_GATE, owner->getPlayerIndex(), gate->getID()); // ignored while it moves (not settled)
	int frames = 2;
	bool sawOpenGeometryLate = false;
	while (b->state() != GateOpenAndCloseBehavior::CLOSED && frames < 200)
	{
		g.frames(1);
		++frames;
		if (b->state() == GateOpenAndCloseBehavior::CLOSING && b->percent() > 50.0f && b->geometryState() != GateOpenAndCloseBehavior::GEOMETRY_CLOSED)
		{
			sawOpenGeometryLate = true;
		}
	}
	CHECK_FALSE(sawOpenGeometryLate);
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSED);
	CHECK(frames >= 61);
	CHECK(frames <= 64);
	g.frames(1);
	CHECK(b->isSettled());
	CHECK(b->geometryState() == GateOpenAndCloseBehavior::GEOMETRY_CLOSED);
	CHECK(g.shapeActive(*gate, "Closed"));
	CHECK_FALSE(g.shapeActive(*gate, "OpenLeft"));
	CHECK(g.blocksCentre(*gate));
	CHECK(gate->testModelCondition(doorClosing));
	// the sounds the retail data names (stop S-1942: recorded, not played)
	CHECK(b->soundRequests() == std::vector<std::string>{ "closing", "closed" });
	// open again
	g.send(MSG_OPEN_GATE, owner->getPlayerIndex(), gate->getID());
	g.frames(70);
	CHECK(b->state() == GateOpenAndCloseBehavior::OPEN);
	CHECK_FALSE(g.blocksCentre(*gate));
	// no owner test in retail's message case: another player's MSG_CLOSE_GATE closes it too
	Player *other = g.game->players().findPlayerWithName("Player_2");
	REQUIRE(other);
	g.send(MSG_CLOSE_GATE, other->getPlayerIndex(), gate->getID());
	g.frames(3);
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSING);
}

TEST_CASE("hud5 gates retail: the gate is in the state hash; two runs of the same commands agree, a closing gate differs from an open one")
{
	OPENBFME_REQUIRE_START(s);
	// the state hash without its "skirmish AI" section: that section hashes the world's SkirmishAIStore, which every game on this map changes (its own hash
	// differs after each game of the process, with or without a computer player: a finding for the AI lane, not the gates'); the gates are in the objects
	auto run = [&](bool close, std::uint32_t &objects) {
		GateGame g(*s, gateMessage(*s, "maps/map wor helms deep/map wor helms deep.map", false));
		g.frames(2);
		Object *gate = g.named("Main Gate");
		REQUIRE(gate);
		if (close)
		{
			g.send(MSG_CLOSE_GATE, gate->getControllingPlayer()->getPlayerIndex(), gate->getID());
		}
		g.frames(10);
		std::vector<GameLogic::StateHashSection> sec;
		g.game->logic().computeStateHashBreakdown(sec, nullptr);
		StateHasher h;
		objects = 0;
		for (const GameLogic::StateHashSection &x : sec)
		{
			if (x.name != "skirmish AI")
			{
				h.addU32(x.value);
			}
			if (x.name.find("object") != std::string::npos)
			{
				objects = x.value;
			}
		}
		return h.value();
	};
	std::uint32_t o1 = 0, o2 = 0, o3 = 0, o4 = 0;
	const std::uint32_t c1 = run(false, o1);
	const std::uint32_t c2 = run(false, o2);
	const std::uint32_t a = run(true, o3);
	const std::uint32_t b = run(true, o4);
	CHECK(c1 == c2);
	CHECK(a == b);
	CHECK(a != c1);
	CHECK(o3 != o1); // the gate's state is in the objects' section
}

TEST_CASE("hud5 gates retail: a computer player's gate (Helm's Deep at start 1) closes with none of its units in the AIGateUpdate box and opens for them")
{
	OPENBFME_REQUIRE_START(s);
	GateGame g(*s, gateMessage(*s, "maps/map wor helms deep/map wor helms deep.map", true));
	Object *gate = g.named("Main Gate");
	REQUIRE(gate);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	AIGateUpdate *u = dynamic_cast<AIGateUpdate *>(gate->findModule("AIGateUpdate"));
	REQUIRE(u);
	Player *ai = gate->getControllingPlayer();
	REQUIRE(ai);
	CHECK(ai->isSkirmishAI());
	// open by default, nobody inside: RotWK's update closes it (RW 0x8B4DA6)
	g.frames(70);
	CHECK(u->triggerIndex() >= 0); // the 450 x 225 box joined the trigger list
	REQUIRE(u->alliesInside() == 0); // the computer's own units stayed away (a deterministic game: this holds for this seed)
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSED);
	// one of the computer's units in the box: the gate opens
	std::string error;
	Coord3D p = *gate->getPosition();
	p.x += 60.0f;
	Object *unit = g.game->createObject("MenPorter", ai->getPlayerIndex(), p, 0.0f, &error);
	REQUIRE_MESSAGE(unit, error);
	p.x += 3.0f;
	unit->setPosition(&p); // a move to another integer cell: the trigger update (RW 0x69264D)
	g.frames(2);
	CHECK(u->alliesInside() == 1);
	CHECK(b->state() == GateOpenAndCloseBehavior::OPENING);
	g.frames(65);
	CHECK(b->state() == GateOpenAndCloseBehavior::OPEN);
	// it leaves: the gate closes. The exit is reported at the object's next integer-cell move outside (RW 0x69264D tests the previous cell), as a walking unit does
	p.x += 2000.0f;
	unit->setPosition(&p);
	p.x += 3.0f;
	unit->setPosition(&p);
	g.frames(2);
	CHECK(u->alliesInside() == 0);
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSING);
}

// Sol's review (HUD-5 r1): Minas Tirith's gate with a computer owner. Its only gate (MinisGateDoor, object "Gate") has a GateOpenAndCloseBehavior and no
// AIGateUpdate, so nothing opens or closes it for the computer (RW 0x8B4D34 is AIGateUpdate's): an enemy at the gate and then one of the owner's units at it
// leave its state as it was; a message still moves it (the dispatcher's case RW 0x77BF7B has no owner test)
TEST_CASE("hud5 gates retail: Minas Tirith's computer-owned gate has no AIGateUpdate: an enemy or an ally at it changes nothing; the gate message still moves it")
{
	OPENBFME_REQUIRE_START(s);
	GateGame g(*s, gateMessage(*s, "maps/map wor minas tirith/map wor minas tirith.map", true));
	Object *gate = g.named("Gate");
	REQUIRE(gate);
	CHECK(gate->getTemplate()->getName() == "MinisGateDoor");
	CHECK(gate->findModule("AIGateUpdate") == nullptr);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	Player *ai = gate->getControllingPlayer();
	REQUIRE(ai);
	CHECK(ai->isSkirmishAI());
	Player *human = nullptr;
	for (int i = 0; i < g.game->players().getPlayerCount(); ++i)
	{
		Player *p = g.game->players().getNthPlayer(i);
		human = p && p->getPlayerType() == PLAYER_HUMAN ? p : human;
	}
	REQUIRE(human);
	g.frames(70);
	const GateOpenAndCloseBehavior::State start = b->state();
	MESSAGE("Minas Tirith's gate state after 70 frames: " << (int)start);
	CHECK(b->isSettled());
	std::string error;
	const Coord3D at = *gate->getPosition();
	Object *enemy = g.game->createObject("MenPorter", human->getPlayerIndex(), Coord3D{ at.x + 10.0f, at.y, at.z }, 0.0f, &error);
	REQUIRE_MESSAGE(enemy, error);
	g.frames(100);
	CHECK(b->state() == start); // an enemy at the gate
	Object *ally = g.game->createObject("MenPorter", ai->getPlayerIndex(), Coord3D{ at.x - 10.0f, at.y, at.z }, 0.0f, &error);
	REQUIRE_MESSAGE(ally, error);
	g.frames(100);
	CHECK(b->state() == start); // and one of the owner's units: no AIGateUpdate, nothing changes
	// the message (whoever sends it: the handler does not test the owner)
	g.send(b->isOpen() ? MSG_CLOSE_GATE : MSG_OPEN_GATE, ai->getPlayerIndex(), gate->getID());
	g.frames(3);
	CHECK(b->state() == (start == GateOpenAndCloseBehavior::OPEN ? GateOpenAndCloseBehavior::CLOSING : GateOpenAndCloseBehavior::OPENING));
}

TEST_CASE("hud5 gates: the acceptance stops are reported")
{
	const std::vector<std::string> stops = GateModules::acceptanceStops();
	REQUIRE(stops.size() == 3);
	CHECK(stops[0].rfind("[S-1940]", 0) == 0);
	CHECK(stops[1].rfind("[S-1941]", 0) == 0);
	CHECK(stops[2].rfind("[S-1942]", 0) == 0);
}
