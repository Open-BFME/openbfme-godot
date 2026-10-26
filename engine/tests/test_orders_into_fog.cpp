// OpenBFME unit tests. GPL-3.0.
// Lane ANTICHEAT-1: OrdersIntoFog (GameNetwork/OrdersIntoFog.h), Palantír detector D1 "orders on objects the player could not see", on synthetic games:
// a human Alice with an AI ally Carol against Bob, the retail shroud rules of lane VIS-1 (PartitionCellSize 40, UnlookPersistDuration 1, the map
// explored as UseShroud = No leaves it), and synthetic command streams: a cheater-like stream (orders on a never-seen fort and on units moving in fog)
// and normal play (orders on what a scout sees, the run-ahead delay, a revealer, an ally's vision, a remembered building). No retail files.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/CommandStreamChecks.h"
#include "GameNetwork/FogCheckDriver.h"
#include "GameNetwork/OrdersIntoFog.h"
#include "GameNetwork/ViewChecks.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Scout\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  ShroudClearingRange = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"Object Grunt\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n"
	"Object Fort\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n"
	// a revealer power's object (the Palantír / Eye of Sauron reveals spawn a short-lived looking object): an ordinary looker for its owner
	"Object Revealer\n"
	"  KindOf = IMMOBILE\n"
	"  ShroudClearingRange = 120\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 1\n"
	"End\n";

// LogicWorld's two players, re-set as three: Alice (human, team 0), Carol (computer, team 0: Alice's ally), Bob (computer, team 1)
struct Game : LogicWorld
{
	std::unique_ptr<ShroudManager> shroud;
	std::unique_ptr<OrdersIntoFog> check;
	std::vector<GameMessage> pending;
	UnsignedInt batch = 0;
	int alice = -1, bob = -1, carol = -1;

	explicit Game(bool withCheck = true)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		logic.reset();
		SkirmishSetup setup;
		setup.players.push_back({ "Alice", "FactionA", true, 0, 0, 0 });
		setup.players.push_back({ "Bob", "FactionB", false, 1, 0, 1 });
		setup.players.push_back({ "Carol", "FactionA", false, 0, 0, 2 });
		setup.defaultStartingCash = 2000;
		const auto errors = players.setupSkirmish(setup);
		REQUIRE(errors.empty());
		logic = std::make_unique<GameLogic>(w.things, w.modules, players, RandomAlgorithm::ZH_CarryChain);
		logic->random().seedRandom(1);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		shroud = std::make_unique<ShroudManager>(*logic);
		shroud->init(40.0f, 1);
		shroud->setExtent(0.0f, 0.0f, 2000.0f, 2000.0f);
		shroud->attach();
		alice = players.findPlayerWithName("Alice")->getPlayerIndex();
		bob = players.findPlayerWithName("Bob")->getPlayerIndex();
		carol = players.findPlayerWithName("Carol")->getPlayerIndex();
		REQUIRE(players.getNthPlayer(alice)->getRelationship(players.getNthPlayer(bob)) == ENEMIES);
		REQUIRE(players.getNthPlayer(alice)->getRelationship(players.getNthPlayer(carol)) == ALLIES);
		// retail multiplayer: UseShroud = No, the whole map starts explored (fogged)
		shroud->applyNewGameShroud(false, { alice, bob, carol }, {});
		if (withCheck)
		{
			check = std::make_unique<OrdersIntoFog>(*logic, std::vector<int>{ alice });
		}
	}
	~Game()
	{
		check.reset();
		shroud->detach();
	}
	Object *place(const char *tmpl, const char *owner, float x, float y)
	{
		Object *o = make(tmpl, teamOf(owner));
		moveTo(o, x, y);
		return o;
	}
	void moveTo(Object *o, float x, float y)
	{
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
	}
	// one logic frame: the check sees the batch's commands on the state they run on, then the frame runs
	void frame()
	{
		if (check)
		{
			check->beforeFrame(batch, pending);
		}
		pending.clear();
		logic->runLogicFrame();
		++batch;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			frame();
		}
	}
	void attack(int player, const Object *target)
	{
		GameMessage m(MSG_DO_ATTACK_OBJECT, player);
		m.appendObjectIDArgument(target->getID());
		pending.push_back(m);
	}
	void power(int player, const Object *target)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_OBJECT, player);
		m.appendIntegerArgument(7);
		m.appendObjectIDArgument(target->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(INVALID_ID);
		pending.push_back(m);
	}
	OrdersIntoFog::PlayerResult aliceResult() const
	{
		for (const auto &r : check->results())
		{
			if (r.player == alice)
			{
				return r;
			}
		}
		FAIL("no result for Alice");
		return {};
	}
};
} // namespace

TEST_CASE("orders into fog: normal play - orders on what the player's scout sees, also when they execute a run-ahead later, are not flagged")
{
	Game g;
	Object *scout = g.place("Scout", "Alice", 400.0f, 400.0f);
	Object *grunt = g.place("Grunt", "Bob", 460.0f, 400.0f); // inside the scout's look
	g.frames(5);
	REQUIRE(g.shroud->clientObjectStatus(*grunt, g.alice) == OBJECTSHROUD_CLEAR);
	g.attack(g.alice, grunt);
	g.frames(1);
	// the click happened while visible; the grunt walks into fog and the order executes 20 frames later (inside the 27-frame window)
	g.moveTo(grunt, 1500.0f, 1500.0f);
	g.frames(20);
	REQUIRE(g.shroud->clientObjectStatus(*grunt, g.alice) == OBJECTSHROUD_SHROUDED);
	g.attack(g.alice, grunt);
	g.frames(1);
	const auto r = g.aliceResult();
	CHECK(r.judged == 2);
	CHECK(r.events.empty());
	CHECK(r.band == "none");
	CHECK(r.score == 0.0);
	(void)scout;
}

TEST_CASE("orders into fog: a building clicked through the fog that the player never saw is flagged never_seen with its frame, object and position")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1600.0f);
	g.frames(10);
	// UseShroud = No: the fort's cells are explored (FOGGED) but an enemy building never seen is hidden (RW 0x68EDD0) and its seen byte is 0
	REQUIRE(g.shroud->clientObjectStatus(*fort, g.alice) == OBJECTSHROUD_SHROUDED);
	REQUIRE_FALSE(g.shroud->hasSeen(*fort, g.alice));
	g.attack(g.alice, fort);
	g.frames(1);
	const auto r = g.aliceResult();
	REQUIRE(r.events.size() == 1);
	const auto &e = r.events[0];
	CHECK(e.frame == 10);
	CHECK(e.reason == OrdersIntoFog::Reason::NeverSeen);
	CHECK(e.target == fort->getID());
	CHECK(e.targetTemplate == "Fort");
	CHECK(e.targetOwner == g.bob);
	CHECK(e.lastVisibleFrame == -1);
	CHECK(e.x == 1600.0f);
	CHECK(e.confidence == 0.97);
	CHECK(r.band == "review"); // one encounter never alerts
}

TEST_CASE("orders into fog: a remembered building (seen once, drawn as a ghost in fog) is not flagged; a unit moving in fog is flagged hidden_unit")
{
	Game g;
	Object *scout = g.place("Scout", "Alice", 400.0f, 400.0f);
	Object *fort = g.place("Fort", "Bob", 460.0f, 400.0f);
	Object *grunt = g.place("Grunt", "Bob", 400.0f, 460.0f);
	g.frames(3);
	REQUIRE(g.shroud->hasSeen(*fort, g.alice));
	g.moveTo(scout, 1800.0f, 200.0f); // the scout leaves: both go into fog
	g.moveTo(grunt, 1000.0f, 1000.0f);
	g.frames(100);
	REQUIRE(g.shroud->clientObjectStatus(*fort, g.alice) == OBJECTSHROUD_FOGGED);   // the ghost
	REQUIRE(g.shroud->clientObjectStatus(*grunt, g.alice) == OBJECTSHROUD_SHROUDED); // hidden
	g.attack(g.alice, fort);
	g.attack(g.alice, grunt);
	g.frames(1);
	const auto r = g.aliceResult();
	CHECK(r.judged == 2);
	CHECK(r.remembered == 1);
	REQUIRE(r.events.size() == 1);
	CHECK(r.events[0].reason == OrdersIntoFog::Reason::HiddenUnit);
	CHECK(r.events[0].target == grunt->getID());
	// last seen on the state batch 3 ran on (the move is set between frames; the shroud follows it in batch 3's update); the order at batch 103 is
	// 103 - 27 - 3 = 73 frames beyond the window
	CHECK(r.events[0].lastVisibleFrame == 3);
	CHECK(r.events[0].confidence == 0.6 + 0.35 * (73.0 / 150.0)); // the detector's own formula, the same operations
}

TEST_CASE("orders into fog: a revealer power's look and an ally's vision count as the player's own sight")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	Object *grunt = g.place("Grunt", "Bob", 1000.0f, 1000.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1000.0f);
	g.frames(5);
	REQUIRE(g.shroud->clientObjectStatus(*grunt, g.alice) == OBJECTSHROUD_SHROUDED);
	// Alice's revealer over the grunt for 10 frames, then it ends; Alice orders the attack 15 frames after it ended (inside the window)
	Object *rev = g.place("Revealer", "Alice", 1000.0f, 1000.0f);
	g.frames(10);
	REQUIRE(g.shroud->clientObjectStatus(*grunt, g.alice) == OBJECTSHROUD_CLEAR);
	g.logic->destroyObject(rev);
	g.frames(15);
	g.attack(g.alice, grunt);
	// Carol's scout (an ally) sees the fort: Alice's cells include it (RW 0x694A45's mask), so the order on it is fine
	g.place("Scout", "Carol", 1560.0f, 1000.0f);
	g.frames(3);
	REQUIRE(g.shroud->clientObjectStatus(*fort, g.alice) == OBJECTSHROUD_CLEAR);
	g.power(g.alice, fort);
	g.frames(1);
	const auto r = g.aliceResult();
	CHECK(r.judged == 2);
	CHECK(r.events.empty());
}

TEST_CASE("orders into fog: a cheater-like stream (orders on never-seen and hidden objects in four fights over minutes) reaches the alert band")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	std::vector<Object *> forts, grunts;
	for (int i = 0; i < 4; ++i)
	{
		forts.push_back(g.place("Fort", "Bob", 1200.0f + 150.0f * (float)i, 1700.0f));
		grunts.push_back(g.place("Grunt", "Bob", 1200.0f + 150.0f * (float)i, 1400.0f));
	}
	g.frames(10);
	for (int fight = 0; fight < 4; ++fight)
	{
		g.attack(g.alice, forts[(size_t)fight]); // never seen
		g.attack(g.alice, grunts[(size_t)fight]);
		g.frames(1);
		g.power(g.alice, grunts[(size_t)fight]); // the same fight: one encounter
		g.frames(200);                          // 40 s to the next fight (first to last fight: 603 frames)
	}
	const auto r = g.aliceResult();
	CHECK(r.events.size() == 12);
	CHECK(r.encounters == 4);
	CHECK(r.strongEncounters == 4);
	CHECK(r.band == "alert");
	double clean = 1.0;
	for (int i = 0; i < 4; ++i)
	{
		clean *= 1.0 - 0.97;
	}
	CHECK(r.score == 1.0 - clean); // four never-seen encounters
	// the JSON the Orthanc queue reads
	const std::string j = g.check->json("0123456789abcdef", "match", g.batch);
	CHECK(j.find("\"detector\":\"D1-orders-into-fog\"") != std::string::npos);
	CHECK(j.find("\"band\":\"alert\"") != std::string::npos);
	CHECK(j.find("\"reason\":\"never_seen\"") != std::string::npos);
	CHECK(j.find("\"command\":\"MSG_DO_ATTACK_OBJECT\"") != std::string::npos);
	CHECK(j.find("\"game_id\":\"0123456789abcdef\"") != std::string::npos);
}

TEST_CASE("orders into fog: the alert needs three strong encounters spread over two minutes; closer together they stay in review")
{
	auto run = [](int gap) {
		Game g;
		g.place("Scout", "Alice", 200.0f, 200.0f);
		std::vector<Object *> forts;
		for (int i = 0; i < 3; ++i)
		{
			forts.push_back(g.place("Fort", "Bob", 1200.0f + 150.0f * (float)i, 1700.0f));
		}
		g.frames(10);
		for (int i = 0; i < 3; ++i)
		{
			g.attack(g.alice, forts[(size_t)i]);
			g.frames(gap);
		}
		return g.aliceResult();
	};
	// 151 frames apart: three encounters (each over the 150-frame encounter), first to last 302 frames (one minute): review
	const auto close = run(151);
	CHECK(close.encounters == 3);
	CHECK(close.strongEncounters == 3);
	CHECK(close.band == "review");
	// 300 frames apart: first to last 600 frames (two minutes): alert
	const auto spread = run(300);
	CHECK(spread.encounters == 3);
	CHECK(spread.band == "alert");
	// two encounters never alert, however strong
	CHECK(OrdersIntoFog::Settings{}.alertEncounters == 3);
}

TEST_CASE("orders into fog: an abstained game (the re-simulation did not reproduce the recorded hashes) reports no band but abstained")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1600.0f);
	g.frames(3);
	g.attack(g.alice, fort);
	g.frames(1);
	g.check->abstain("crc mismatch at frame 100");
	const auto r = g.aliceResult();
	CHECK(r.events.size() == 1);
	CHECK(r.band == "abstained");
	const std::string j = g.check->json("", "mismatch", g.batch);
	CHECK(j.find("\"status\":\"abstained\"") != std::string::npos);
	CHECK(j.find("\"abstain_reason\":\"crc mismatch at frame 100\"") != std::string::npos);
	CHECK(j.find("\"game_id\":null") != std::string::npos);
}

TEST_CASE("orders into fog: the check reads the shroud without writing it - the state hash of a game with the check equals the one without")
{
	auto run = [](bool withCheck) {
		Game g(withCheck);
		Object *scout = g.place("Scout", "Alice", 400.0f, 400.0f);
		Object *grunt = g.place("Grunt", "Bob", 900.0f, 900.0f);
		Object *fort = g.place("Fort", "Bob", 1300.0f, 900.0f);
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 40; ++i)
		{
			g.moveTo(scout, 400.0f + 25.0f * (float)i, 400.0f + 20.0f * (float)i);
			g.attack(g.alice, i % 2 ? grunt : fort);
			g.frame();
			hashes.push_back(g.logic->computeStateHash());
		}
		return hashes;
	};
	CHECK(run(true) == run(false));
}

TEST_CASE("orders into fog: orders on allies, stale ids and other players' orders are not judged; position commands are out of scope")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	Object *ally = g.place("Grunt", "Carol", 1500.0f, 1500.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1600.0f);
	g.frames(3);
	g.attack(g.alice, ally);     // an ally: not judged
	g.attack(g.bob, fort);       // Bob's orders are not checked (not a judged player)
	GameMessage stale(MSG_DO_ATTACK_OBJECT, g.alice);
	stale.appendObjectIDArgument(999999);
	g.pending.push_back(stale);  // no such object
	GameMessage move(MSG_DO_MOVETO, g.alice);
	move.appendLocationArgument(Coord3D{ 1600.0f, 1600.0f, 0.0f });
	g.pending.push_back(move);   // a blind move onto the fort: a position, not judged
	g.frames(1);
	const auto r = g.aliceResult();
	CHECK(r.judged == 0);
	CHECK(r.stale == 1);
	CHECK(r.events.empty());
	CHECK(OrdersIntoFog::targetArgument(MSG_DO_MOVETO) == -1);
	CHECK(OrdersIntoFog::targetArgument(MSG_DO_WEAPON_AT_OBJECT) == 1);
}

// ---- owner 2026-10-21: selections of hidden enemies (D1), the input rate (D5), commands a clean client never sends (D6) ----------------------------

TEST_CASE("orders into fog: a selection naming an enemy the player never saw is flagged like an order; selecting seen enemies and own units is not")
{
	Game g;
	Object *scout = g.place("Scout", "Alice", 400.0f, 400.0f);
	Object *seen = g.place("Grunt", "Bob", 460.0f, 400.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1600.0f);
	g.frames(5);
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, g.alice);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(scout->getID()); // own: not judged
	sel.appendObjectIDArgument(seen->getID());  // a seen enemy: judged, fine
	sel.appendObjectIDArgument(fort->getID());  // never seen: flagged
	g.pending.push_back(sel);
	g.frames(1);
	const auto r = g.aliceResult();
	CHECK(r.judged == 2);
	REQUIRE(r.events.size() == 1);
	CHECK(r.events[0].messageType == MSG_CREATE_SELECTED_GROUP);
	CHECK(r.events[0].target == fort->getID());
	CHECK(r.events[0].reason == OrdersIntoFog::Reason::NeverSeen);
	CHECK(OrdersIntoFog::isSelection(MSG_CREATE_SELECTED_GROUP_NO_SOUND));
	CHECK_FALSE(OrdersIntoFog::isSelection(MSG_REMOVE_FROM_SELECTED_GROUP));
}

namespace
{
std::vector<GameMessage> burst(int player, int n, int type = MSG_DO_MOVETO)
{
	std::vector<GameMessage> out;
	for (int i = 0; i < n; ++i)
	{
		GameMessage m(type, player);
		m.appendLocationArgument(Coord3D{ 10.0f, 10.0f, 0.0f });
		out.push_back(m);
	}
	return out;
}
} // namespace

TEST_CASE("input rate (D5): more than 100 commands in 5 s of game time is an episode; exactly 100, a slower rate and MSG_LOGIC_CRC are not")
{
	Game g(false);
	auto run = [&](const std::vector<std::vector<GameMessage>> &frames) {
		InputRateCheck c(*g.logic, { g.alice });
		UnsignedInt f = 10;
		for (const auto &batch : frames)
		{
			c.beforeFrame(f++, batch);
		}
		return c.results()[0];
	};
	// 100 commands in 25 frames (4 a frame): not over the threshold
	std::vector<std::vector<GameMessage>> hundred(25, burst(g.alice, 4));
	CHECK(run(hundred).episodes.empty());
	CHECK(run(hundred).peak == 100);
	// one more in the same window: an episode with its peak, frame and most frequent type
	auto over = hundred;
	over[24].push_back(burst(g.alice, 1, MSG_DO_ATTACKMOVETO)[0]);
	const auto r = run(over);
	REQUIRE(r.episodes.size() == 1);
	CHECK(r.episodes[0].peak == 101);
	CHECK(r.episodes[0].peakFrame == 34);
	CHECK(r.episodes[0].startFrame == 34);
	CHECK(r.episodes[0].topCommand == MSG_DO_MOVETO);
	CHECK(r.commands == 101);
	// 101 commands over 26 frames: never 101 inside one window of 25
	std::vector<std::vector<GameMessage>> slow(26, burst(g.alice, 4));
	slow[0].pop_back();
	slow[0].pop_back();
	slow[0].pop_back();
	CHECK(run(slow).episodes.empty());
	// the client's own checksum is not the player's input; another player's commands are not Alice's
	std::vector<std::vector<GameMessage>> crcs(25, burst(g.alice, 5, MSG_LOGIC_CRC));
	crcs[0] = burst(g.bob, 200);
	CHECK(run(crcs).commands == 0);
	// two bursts a minute apart: two episodes
	std::vector<std::vector<GameMessage>> two(400);
	for (int i = 0; i < 5; ++i)
	{
		two[(size_t)i] = burst(g.alice, 30);
		two[(size_t)(300 + i)] = burst(g.alice, 30);
	}
	const auto t = run(two);
	CHECK(t.episodes.size() == 2);
	CHECK(t.peak == 150);
	InputRateCheck c(*g.logic, { g.alice });
	c.beforeFrame(1, two[0]);
	const std::string j = c.json("0123456789abcdef", "match", 2);
	CHECK(j.find("\"detector\":\"D5-input-rate\"") != std::string::npos);
	CHECK(j.find("\"band\":\"none\"") != std::string::npos);
}

TEST_CASE("fog replay driver: recorded hashes outside Uber checkpoints are requested and mismatches are reported")
{
	Game g;
	ReplayFile replay;
	replay.header.game.game.simulationProfile = SimulationProfile::Uber;
	replay.hashes = { { 1, 0x1234u }, { 5, 0x5678u }, { 7, 0x9ABCu } };
	replay.finalFrame = 7;
	ReplayPlayback playback(replay);
	FogCheckDriver fog(playback, replay, *g.check);
	LiveGameFrameDriver &driver = fog;
	CHECK(driver.capture().hashEveryFrame);
	CHECK(driver.hashWanted(1));
	CHECK(driver.hashWanted(5));
	CHECK(driver.hashWanted(7));
	CHECK_FALSE(driver.hashWanted(0));
	CHECK_FALSE(driver.hashWanted(2));
	CHECK_FALSE(driver.hashWanted(8));

	for (const auto &entry : replay.hashes)
	{
		FrameCompletion c;
		c.frame = entry.first;
		c.hashed = driver.hashWanted(c.frame);
		c.hash = entry.second;
		if (c.frame == 7)
		{
			c.hash ^= 1u; // a divergence on a recorded frame between five-frame checkpoints
		}
		driver.completed(c);
	}
	CHECK(playback.hashesCompared() == 3u);
	CHECK(playback.mismatches() == 1u);
	REQUIRE(playback.hasMismatch());
	CHECK(playback.firstMismatch().frame == 7u);
	CHECK(playback.firstMismatch().recorded == 0x9ABCu);
	CHECK(playback.firstMismatch().played == 0x9ABDu);
}

TEST_CASE("command integrity (D6): unknown message types, malformed arguments and another player's object named as the issuer are listed")
{
	Game g(false);
	Object *mine = g.place("Fort", "Alice", 200.0f, 200.0f);
	Object *theirs = g.place("Fort", "Bob", 1600.0f, 1600.0f);
	CommandIntegrityCheck c(*g.logic, { g.alice });
	std::vector<GameMessage> batch;
	batch.emplace_back(1200, g.alice); // no such message type
	GameMessage badSel(MSG_CREATE_SELECTED_GROUP, g.alice);
	badSel.appendIntegerArgument(1); // the flag must be a boolean
	batch.push_back(badSel);
	GameMessage badAttack(MSG_DO_ATTACK_OBJECT, g.alice);
	badAttack.appendLocationArgument(Coord3D{ 1.0f, 2.0f, 0.0f }); // the victim must be an object
	batch.push_back(badAttack);
	auto rally = [&](const Object *building) {
		GameMessage m(MSG_SET_RALLY_POINT, g.alice);
		m.appendObjectIDArgument(building->getID());
		m.appendLocationArgument(Coord3D{ 5.0f, 5.0f, 0.0f });
		m.appendBooleanArgument(false);
		m.appendObjectIDArgument(INVALID_ID);
		return m;
	};
	batch.push_back(rally(mine));   // fine
	batch.push_back(rally(theirs)); // another player's building as the issuer
	GameMessage fine(MSG_CREATE_SELECTED_GROUP, g.alice);
	fine.appendBooleanArgument(true);
	fine.appendObjectIDArgument(theirs->getID()); // selecting an enemy is something the client does
	batch.push_back(fine);
	batch.emplace_back(1200, g.bob); // not a judged player
	c.beforeFrame(7, batch);
	const auto r = c.results()[0];
	CHECK(r.commands == 6);
	REQUIRE(r.events.size() == 4);
	CHECK(r.events[0].reason == "unknown_type");
	CHECK(r.events[1].reason == "malformed");
	CHECK(r.events[1].argument == 0);
	CHECK(r.events[2].reason == "malformed");
	CHECK(r.events[2].messageType == MSG_DO_ATTACK_OBJECT);
	CHECK(r.events[3].reason == "foreign_object");
	CHECK(r.events[3].object == theirs->getID());
	CHECK(r.encounters == 1);
	const std::string j = c.json("0123456789abcdef", "match", 8);
	CHECK(j.find("\"detector\":\"D6-command-integrity\"") != std::string::npos);
	CHECK(j.find("\"command\":\"MSG_UNKNOWN_1200\"") != std::string::npos);
	CHECK(j.find("\"band\":\"review\"") != std::string::npos);
	CHECK(CommandIntegrityCheck::issuerArgument(MSG_DO_AUTO_ABILITY) == 1);
}

TEST_CASE("view focus (D2): the pointer resting 3 s over a hidden enemy is an episode; shorter, over a seen enemy, over one seen moments ago, or stale views are not")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	Object *fort = g.place("Fort", "Bob", 1600.0f, 1600.0f);      // never seen
	Object *seen = g.place("Grunt", "Bob", 260.0f, 200.0f);       // in the scout's sight
	Object *walker = g.place("Grunt", "Bob", 260.0f, 240.0f);     // seen (72 from the scout, which sees 100), then it walks into fog
	std::vector<int> slots{ g.alice, -1, -1, -1, -1, -1, -1, -1 };
	std::vector<ReplayFile::View> views;
	auto look = [&](UnsignedInt from, UnsignedInt to, float x, float y, std::uint8_t flags = 1, UnsignedInt every = 2) {
		for (UnsignedInt f = from; f < to; f += every)
		{
			ReplayFile::View v;
			v.frame = f;
			v.slot = 0;
			v.cameraX = x;
			v.cameraY = y;
			v.cursorX = x + 5.0f;
			v.cursorY = y;
			v.flags = flags;
			views.push_back(v);
		}
	};
	look(10, 30, 1600.0f, 1600.0f);          // 20 frames over the never-seen fort: an episode
	look(60, 64, 1600.0f, 1600.0f);          // two reports (frames 60, 62), standing until 72: 13 frames, too short
	look(100, 130, 260.0f, 200.0f);          // over a seen enemy: nothing hidden there
	look(150, 180, 1000.0f, 1000.0f, 0);     // the pointer over the HUD (flags 0): no ground point
	look(200, 202, 1600.0f, 1600.0f);        // one report, then silence: stale after 10 frames
	// the walker goes into fog at frame 230 and the pointer follows it at once for 28 frames: it was seen moments ago, so that is no episode
	look(232, 260, 1200.0f, 1000.0f);
	ViewCheck vc(*g.logic, views, slots, *g.check);
	for (UnsignedInt f = 0; f < 300; ++f)
	{
		if (f == 230)
		{
			g.moveTo(walker, 1200.0f, 1000.0f);
		}
		g.check->beforeFrame(g.batch, g.pending);
		vc.beforeFrame(g.batch);
		g.pending.clear();
		g.logic->runLogicFrame();
		++g.batch;
	}
	const auto r = vc.results()[0];
	REQUIRE(r.events.size() == 1);
	CHECK(r.events[0].frame == 10);
	CHECK(r.events[0].frames == 29); // frames 10 .. 38: the last view (frame 28) stands for the pointer until it is stale (10 frames later)
	CHECK(r.events[0].target == fort->getID());
	CHECK(r.events[0].targetTemplate == "Fort");
	CHECK(r.views == views.size());
	CHECK(r.encounters == 1);
	const std::string j = vc.json("0123456789abcdef", "match", g.batch);
	CHECK(j.find("\"detector\":\"D2-view-focus\"") != std::string::npos);
	CHECK(j.find("\"reason\":\"hover_hidden\"") != std::string::npos);
	(void)seen;
}

TEST_CASE("view focus (D2): views whose values are not finite numbers are counted and ignored; the report stays valid JSON (Sol r3)")
{
	Game g;
	g.place("Scout", "Alice", 200.0f, 200.0f);
	g.place("Fort", "Bob", 1600.0f, 1600.0f);
	std::vector<int> slots{ g.alice, -1, -1, -1, -1, -1, -1, -1 };
	std::vector<ReplayFile::View> views;
	const float bad[3] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 3.0e38f };
	for (UnsignedInt f = 0; f < 60; f += 2)
	{
		ReplayFile::View v;
		v.frame = f;
		v.slot = 0;
		v.flags = 1;
		v.cameraX = v.cameraY = 100.0f;
		v.cursorX = bad[(f / 2) % 3];
		v.cursorY = 1600.0f;
		views.push_back(v);
	}
	ViewCheck vc(*g.logic, views, slots, *g.check);
	for (UnsignedInt f = 0; f < 80; ++f)
	{
		g.check->beforeFrame(g.batch, g.pending);
		vc.beforeFrame(g.batch);
		g.logic->runLogicFrame();
		++g.batch;
	}
	const auto r = vc.results()[0];
	CHECK(r.views == 30);
	CHECK(r.invalidViews == 30);
	CHECK(r.events.empty());
	const std::string j = vc.json("0123456789abcdef", "match", g.batch);
	CHECK(j.find("nan") == std::string::npos);
	CHECK(j.find("inf") == std::string::npos);
	CHECK(j.find("\"invalid_views\":30") != std::string::npos);
}
