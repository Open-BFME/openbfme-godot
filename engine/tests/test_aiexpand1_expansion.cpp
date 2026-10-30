// OpenBFME unit tests. GPL-3.0.
// Lane AIEXPAND-1: the skirmish AI's EXPANSION target heuristic (GameLogic/SkirmishAI/AITacticalAI.cpp heuristicExpansion: RotWK RW 0x97D191 with its gate
// RW 0x97D035, decomp Open-BFME-2 AITargetHeuristicExpansionBase.cpp:221-283). A retail 1 v 1 on Tournament Udun (four start waypoints): the idle human at
// start 0, a Medium computer at start 1, starts 3 and 4 empty. The expectations follow the RW functions (the gate's order, the one draw at line 0x3F, the
// waypoint walk and the occupied test), recomputed here from the map's waypoints and objects, never read back from the port.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
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

struct ExpansionGame
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *human = nullptr;
	Player *computer = nullptr;
	AISkirmishPlayer *ai = nullptr;
};

void loadUdun(starttest::Shared &s, ExpansionGame &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMordor", "FactionMen" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Human" : u"Computer";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: a precondition (the faction exists; its index is the store's)
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	GameLogic &logic = g.game->logic();
	for (int i = 0; i < 4; ++i)
	{
		g.game->advance(0.2);
	}
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		// the slots' players: the first playable side at each start (later non-playable players report start 0 too)
		Player *p = logic.players().getNthPlayer(i);
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (!g.human && p->getMultiplayerStartIndex() == 0)
		{
			g.human = p;
		}
		if (!g.computer && p->getMultiplayerStartIndex() == 1)
		{
			g.computer = p;
		}
	}
	REQUIRE(g.human != nullptr);
	REQUIRE(g.computer != nullptr);
	for (const auto &grp : logic.skirmishAI().groups())
	{
		auto it = grp->players.find(g.computer->getPlayerIndex());
		if (it != grp->players.end())
		{
			g.ai = it->second.get();
		}
	}
	REQUIRE(g.ai != nullptr);
	REQUIRE(g.ai->store != nullptr);
}

// the team of one of the human's starting objects that the computer has as ENEMIES (the scan's relationship filter is by the object's team)
Team *enemyTeamOf(GameLogic &logic, const Player &me, const Player &them)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &them && o->getTeam() && me.getRelationship(o->getTeam()) == ENEMIES)
		{
			return o->getTeam();
		}
	}
	return nullptr;
}

Object *placeAt(GameLogic &logic, const char *name, Team *team, const Coord3D &at)
{
	REQUIRE(team != nullptr);
	Object *o = logic.newObject(logic.things().findTemplate(name), team, {});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D pos{ at.x, at.y, logic.getGroundHeight(at.x, at.y) };
	o->setPosition(&pos);
	return o;
}

float dist2(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return dy * dy + dx * dx;
}

// RW 0x97D191's occupied test, recomputed over the game's object list: a living object within R (2D) that the computer has as ENEMIES or ALLIES and that is
// either its ALLY or not an ECONOMY_STRUCTURE
bool occupied(GameLogic &logic, const Player &me, const Coord3D &at, float r)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isEffectivelyDead() || dist2(*o->getPosition(), at) > r * r)
		{
			continue;
		}
		const Relationship rel = me.getRelationship(o->getTeam());
		if (rel != ENEMIES && rel != ALLIES)
		{
			continue;
		}
		if (me.getRelationship(o->getControllingPlayer()) == ALLIES || !o->isKindOfName("ECONOMY_STRUCTURE"))
		{
			return true;
		}
	}
	return false;
}

// the expected pick: Player_<i + 1>_Start for i = 0 .. 19 without the player's own index, up to the first missing one; the nearest free one to the base (a
// tie keeps the first), none when the best distance is not above 0
bool expectedPick(GameLogic &logic, const AISkirmishPlayer &ai, const Player &me, Coord3D &out)
{
	const float r = ai.store->data().defaultTargetThreatRadius;
	float best = -1.0f;
	for (int i = 0; i < 20; ++i)
	{
		if (i == me.getPlayerIndex())
		{
			continue;
		}
		const Waypoint *wp = logic.terrain()->findWaypointByName("Player_" + std::to_string(i + 1) + "_Start");
		if (!wp)
		{
			break;
		}
		const float d = dist2(wp->location, ai.build.startPosition);
		if ((best < 0.0f || d < best) && !occupied(logic, me, wp->location, r))
		{
			best = d;
			out = wp->location;
		}
	}
	return 0.0f < best;
}

int expansionDraws(const GameLogicRandom &rng)
{
	int n = 0;
	for (const GameLogicRandom::Call &c : rng.callLog())
	{
		n += c.real && c.file == "AITargetHeuristicExpansionBase.cpp" && c.line == 0x3F && c.rlo == 0.0f && c.rhi == 1.0f ? 1 : 0;
	}
	return n;
}

// the gate passes the frame counter, money and phase; then calls the heuristic until its draw says yes (each call one draw), at most 64 times
bool callUntilChosen(GameLogic &logic, AISkirmishPlayer &ai, AITarget &t, int &calls)
{
	for (calls = 1; calls <= 64; ++calls)
	{
		logic.skirmishAI().expansionHeuristicFrames() = 301;
		logic.random().clearCallLog();
		AITacticalAI::expansionHeuristicForTest(logic, ai, t);
		REQUIRE(expansionDraws(logic.random()) == 1);
		REQUIRE(logic.random().callLog().size() == 1);
		if (logic.random().callLog().back().rresult <= 0.5f)
		{
			return true;
		}
		REQUIRE(t.inactive); // a draw above 0.5 chooses nothing
	}
	return false;
}
} // namespace

TEST_CASE("aiexpand1 expansion gate: the frame counter, 3000 money, the game phase, then one draw <= 0.5 (RW 0x97D035)")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer &ai = *g.ai;
	std::uint32_t &frames = logic.skirmishAI().expansionHeuristicFrames();
	logic.random().enableCallLog(true);
	AITarget t;
	t.type = 3;

	// the counter at [RW 0xDEBD74] = 5 * 60 (or below): + 1, no further test, no draw
	frames = 300;
	logic.random().clearCallLog();
	AITacticalAI::expansionHeuristicForTest(logic, ai, t);
	CHECK(frames == 301);
	CHECK(logic.random().callLog().empty());
	CHECK(t.inactive);

	// above it: the counter restarts at 0 whatever follows; 1500 money (< 3000) refuses before the draw
	REQUIRE(g.computer->getMoney()->countMoney() < 3000u); // tolerance: a precondition (the AI has spent some of its 1500 by now)
	AITacticalAI::expansionHeuristicForTest(logic, ai, t);
	CHECK(frames == 0);
	CHECK(logic.random().callLog().empty());
	CHECK(t.inactive);

	// 3000 money, the Rush phase (0): refused before the draw
	g.computer->getMoney()->deposit(3000u - g.computer->getMoney()->countMoney(), nullptr, false);
	REQUIRE(g.computer->getMoney()->countMoney() == 3000u);
	ai.build.phase = 0;
	frames = 301;
	AITacticalAI::expansionHeuristicForTest(logic, ai, t);
	CHECK(frames == 0);
	CHECK(logic.random().callLog().empty());
	CHECK(t.inactive);

	// the MidGame phase: exactly one draw GetGameLogicRandomValueReal(0, 1) at line 0x3F, and a target only when it is <= 0.5
	ai.build.phase = 1;
	int calls = 0;
	REQUIRE(callUntilChosen(logic, ai, t, calls));
	CHECK(frames == 0);
	CHECK_FALSE(t.inactive);
	MESSAGE("chosen on call " << calls);
}

TEST_CASE("aiexpand1 expansion target: the nearest empty start position to the AI's base, as a position target with DefaultTargetThreatRadius (RW 0x97D191)")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer &ai = *g.ai;
	const float r = ai.store->data().defaultTargetThreatRadius;
	REQUIRE(r > 0.0f); // tolerance: a precondition (the data's DefaultTargetThreatRadius; the checks below use its exact value)
	for (int n = 1; n <= 4; ++n)
	{
		REQUIRE(logic.terrain()->findWaypointByName("Player_" + std::to_string(n) + "_Start") != nullptr);
	}
	// the human's start holds its starting structures: never a pick
	const Waypoint *humanStart = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(occupied(logic, *g.computer, humanStart->location, r));
	Coord3D expected;
	REQUIRE(expectedPick(logic, ai, *g.computer, expected));
	CHECK_FALSE(occupied(logic, *g.computer, expected, r));

	g.computer->getMoney()->deposit(5000u, nullptr, false);
	ai.build.phase = 1;
	logic.random().enableCallLog(true);
	AITarget t;
	t.type = 3;
	t.object = 77; // RW 0x6C6975 writes the object argument: 0 here
	int calls = 0;
	REQUIRE(callUntilChosen(logic, ai, t, calls));
	CHECK(t.position.x == expected.x);
	CHECK(t.position.y == expected.y);
	CHECK(t.position.z == expected.z);
	CHECK(t.object == INVALID_ID);
	CHECK(t.radius == r);
	CHECK(t.finderPosition.x == expected.x);
	CHECK(t.frame == logic.getFrame());
	CHECK(t.teams == 0);
	CHECK_FALSE(t.dropped);
	CHECK(t.threat == 0.0f); // nothing of the human's stands there
}

TEST_CASE("aiexpand1 expansion target: an enemy army at an empty start makes it occupied; with every start occupied the draw is made and nothing is chosen")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer &ai = *g.ai;
	const float r = ai.store->data().defaultTargetThreatRadius;
	Coord3D first;
	REQUIRE(expectedPick(logic, ai, *g.computer, first));
	// a human horde on the first pick
	Team *enemy = enemyTeamOf(logic, *g.computer, *g.human);
	placeAt(logic, "MordorFighterHorde", enemy, first);
	g.game->advance(0.2); // the partition takes the new object
	REQUIRE(occupied(logic, *g.computer, first, r));
	g.computer->getMoney()->deposit(5000u, nullptr, false);
	ai.build.phase = 1;
	logic.random().enableCallLog(true);
	Coord3D second;
	const bool another = expectedPick(logic, ai, *g.computer, second);
	if (another)
	{
		AITarget t;
		t.type = 3;
		int calls = 0;
		REQUIRE(callUntilChosen(logic, ai, t, calls));
		CHECK_FALSE((t.position.x == first.x && t.position.y == first.y));
		CHECK(t.position.x == second.x);
		CHECK(t.position.y == second.y);
		placeAt(logic, "MordorFighterHorde", enemy, second);
		g.game->advance(0.2);
	}
	Coord3D none;
	REQUIRE_FALSE(expectedPick(logic, ai, *g.computer, none));
	// every other start is occupied: each call that passes the gate still draws once, and the target stays inactive
	AITarget t;
	t.type = 3;
	for (int i = 0; i < 8; ++i)
	{
		logic.skirmishAI().expansionHeuristicFrames() = 301;
		logic.random().clearCallLog();
		AITacticalAI::expansionHeuristicForTest(logic, ai, t);
		CHECK(expansionDraws(logic.random()) == 1);
		CHECK(t.inactive);
	}
}

TEST_CASE("aiexpand1 expansion target: an enemy ECONOMY_STRUCTURE alone leaves a start free; the computer's own unit there occupies it (RW 0x97D191's count)")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer &ai = *g.ai;
	Coord3D first;
	REQUIRE(expectedPick(logic, ai, *g.computer, first));
	g.computer->getMoney()->deposit(5000u, nullptr, false);
	ai.build.phase = 1;
	logic.random().enableCallLog(true);
	// an enemy mine shaft (ECONOMY_STRUCTURE, an enemy: not counted) on the pick: still the pick
	Team *enemy = enemyTeamOf(logic, *g.computer, *g.human);
	Object *mine = placeAt(logic, "DwarvenMineShaft", enemy, first);
	REQUIRE(mine->isKindOfName("ECONOMY_STRUCTURE"));
	g.game->advance(0.2);
	REQUIRE(g.computer->getRelationship(mine->getTeam()) == ENEMIES);
	{
		AITarget t;
		t.type = 3;
		int calls = 0;
		REQUIRE(callUntilChosen(logic, ai, t, calls));
		CHECK(t.position.x == first.x);
		CHECK(t.position.y == first.y);
	}
	// the computer's own horde there (its own team: ALLIES, counted whatever its kind): no longer the pick
	Object *own = placeAt(logic, "GondorFighterHorde", g.computer->getDefaultTeam(), first);
	g.game->advance(0.2);
	REQUIRE(g.computer->getRelationship(own->getTeam()) == ALLIES);
	{
		AITarget t;
		t.type = 3;
		Coord3D next;
		const bool another = expectedPick(logic, ai, *g.computer, next);
		if (another)
		{
			int calls = 0;
			REQUIRE(callUntilChosen(logic, ai, t, calls));
			CHECK_FALSE((t.position.x == first.x && t.position.y == first.y));
			CHECK(t.position.x == next.x);
		}
		else
		{
			logic.skirmishAI().expansionHeuristicFrames() = 301;
			AITacticalAI::expansionHeuristicForTest(logic, ai, t);
			CHECK(t.inactive);
		}
	}
}

TEST_CASE("aiexpand1 expansion: the heuristic's frame counter is hashed and saved (manager crc, xfer version 4)")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	std::uint32_t &frames = logic.skirmishAI().expansionHeuristicFrames();
	frames = 17;
	StateHasher a;
	logic.skirmishAI().crc(a);
	frames = 18;
	StateHasher b;
	logic.skirmishAI().crc(b);
	CHECK(a.value() != b.value());
	// the obfme:SkirmishAI block carries it: saved at 17, loaded back over 5
	frames = 17;
	XferSave save;
	logic.skirmishAI().xfer(save);
	frames = 5;
	XferLoad load(save.data());
	logic.skirmishAI().xfer(load);
	CHECK(logic.skirmishAI().expansionHeuristicFrames() == 17u);
}

TEST_CASE("aiexpand1 expansion in a game: the Medium Men computer kept above 3000 money picks an empty start as its EXPANSION target and sends a SimpleExpansion there (RW 0x90C50C / 0x9B52BD)")
{
	OPENBFME_REQUIRE_START(s);
	ExpansionGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer *aiPtr = g.ai;
	REQUIRE(aiPtr->build.bases.size() == 1u);
	bool roundTrip = false, roundTripDone = false;
	int chosenFrame = -1, tacticFrame = -1, requestFrame = -1, inProgressFrame = -1, doneFrame = -1, baseFrame = -1;
	int lastState = -1;
	Coord3D chosenAt;
	std::string requestTemplate;
	bool endedTripDone = false;
	for (int f = 0; f < 6000 && !(endedTripDone && baseFrame != -1); ++f)
	{
		if (g.computer->getMoney()->countMoney() < 5000u)
		{
			g.computer->getMoney()->deposit(5000u, nullptr, false);
		}
		g.game->advance(0.2);
		const int frame = (int)logic.getFrame();
		const AISkirmishPlayer &ai = *aiPtr;
		const AIBrainState &b = ai.brain;
		for (const AITarget &t : b.targets)
		{
			if (t.type == 3 && !t.inactive && chosenFrame == -1)
			{
				chosenFrame = frame;
				chosenAt = t.position;
				CHECK(t.object == INVALID_ID);
				CHECK(ai.build.phase != 0); // tolerance: MidGame (1) or EndGame (2), never the Rush phase (0): the gate tests phase >= 1
			}
		}
		for (const AITactic &tac : b.tactics)
		{
			if (tac.kind != "SimpleExpansion")
			{
				continue;
			}
			if (tacticFrame == -1)
			{
				tacticFrame = frame;
				REQUIRE(tac.teams.size() == 1u);
				CHECK(tac.teams[0].maximum == 2); // RW 0x9B5081: GetGameLogicRandomValue(2, 2)
				CHECK(tac.teams[0].minimum == 1);
				CHECK(tac.teams[0].type == 3);
			}
			if (tac.request)
			{
				if (requestFrame == -1)
				{
					requestFrame = frame;
					requestTemplate = tac.request->templateName;
					CHECK(tac.request->plain);
					CHECK(tac.request->priority == 3000.0f);
					CHECK(tac.request->offset.x == chosenAt.x);
					CHECK(tac.request->offset.y == chosenAt.y);
					CHECK_FALSE(tac.request->requeueOnProducerDeath);
				}
				lastState = tac.request->state;
				if (lastState == 1 && inProgressFrame == -1)
				{
					inProgressFrame = frame;
				}
				if (lastState == 1 && !roundTrip && tac.step == 2)
				{
					roundTrip = true; // Sol r2: a save while the AI expands (stage 2, its request in progress) loads back
				}
				if (lastState == 2 && doneFrame == -1)
				{
					doneFrame = frame;
				}
			}
		}
		// Sol r3: the tactic that ended in this update (its cleanup ran; it is dropped at the next update) is saved and loaded too
		bool endedNow = false;
		for (const AITactic &tac : b.tactics)
		{
			endedNow = endedNow || (tac.kind == "SimpleExpansion" && tac.ended && tac.step > 0); // ended past stage 0: its request already let go
		}
		if (endedNow && !endedTripDone)
		{
			endedTripDone = true;
			std::vector<std::uint8_t> bytes;
			logic.gameState().saveBlocks({ "obfme:SkirmishAI" }, bytes);
			const std::uint32_t before = logic.computeStateHash();
			logic.gameState().loadBlocks({ "obfme:SkirmishAI" }, bytes);
			CHECK(logic.computeStateHash() == before);
			MESSAGE("the ended SimpleExpansion reloaded on frame " << frame);
			aiPtr = nullptr; // the load made the AIs again
			for (const auto &grp : logic.skirmishAI().groups())
			{
				auto it = grp->players.find(g.computer->getPlayerIndex());
				if (it != grp->players.end())
				{
					aiPtr = it->second.get();
				}
			}
			REQUIRE(aiPtr != nullptr);
			continue;
		}
		if (roundTrip && !roundTripDone)
		{
			roundTripDone = true;
			std::vector<std::uint8_t> bytes;
			logic.gameState().saveBlocks({ "obfme:SkirmishAI" }, bytes);
			const std::uint32_t before = logic.computeStateHash();
			logic.gameState().loadBlocks({ "obfme:SkirmishAI" }, bytes);
			CHECK(logic.computeStateHash() == before);
			std::vector<std::uint8_t> again;
			logic.gameState().saveBlocks({ "obfme:SkirmishAI" }, again);
			CHECK(again == bytes);
			aiPtr = nullptr; // the load made the AIs again
			for (const auto &grp : logic.skirmishAI().groups())
			{
				auto it = grp->players.find(g.computer->getPlayerIndex());
				if (it != grp->players.end())
				{
					aiPtr = it->second.get();
				}
			}
			REQUIRE(aiPtr != nullptr);
			bool found = false;
			for (const AITactic &tac : aiPtr->brain.tactics)
			{
				if (tac.kind == "SimpleExpansion")
				{
					found = true;
					CHECK(tac.step == 2);
					REQUIRE(tac.request.get() != nullptr);
					CHECK(tac.request->plain);
					CHECK(tac.request->state == 1);
					// the tactic's request is the one in the AI's in-progress queue (one request table entry)
					CHECK(std::find(aiPtr->build.inProgress.begin(), aiPtr->build.inProgress.end(), tac.request) != aiPtr->build.inProgress.end());
				}
			}
			CHECK(found);
			continue;
		}
		if (ai.build.bases.size() == 2u && baseFrame == -1)
		{
			baseFrame = frame;
			CHECK(ai.build.bases[1].position.x == chosenAt.x);
			CHECK(ai.build.bases[1].position.y == chosenAt.y);
			CHECK(ai.build.bases[1].index == 1);
		}
	}
	REQUIRE(chosenFrame != -1);
	bool isStart = false;
	for (int n = 3; n <= 4; ++n)
	{
		const Waypoint *wp = logic.terrain()->findWaypointByName("Player_" + std::to_string(n) + "_Start");
		isStart = isStart || (wp && wp->location.x == chosenAt.x && wp->location.y == chosenAt.y);
	}
	CHECK(isStart);
	CHECK(requestTemplate == "MenFortress");
	CHECK(roundTripDone);
	CHECK(endedTripDone);
	// engine pins (Udun, seed 1234, measured on lane AIEXPAND-1's tree): a later simulation change moves them; re-measure and say why
	// merge queue q29 (AIEXPAND-1 joins after nine simulation items; measured on the merged tree, a CI host, not split per item): the other items move this
	// AI game, so the heuristic's gate first passes later: the same start is chosen at 2425 (was 1519), its request at 2428 (1964), in progress at 2429 (1965),
	// complete with the second base at 3169 (2736)
	CHECK(chosenFrame == 2425);
	CHECK(tacticFrame == 2425);
	CHECK(requestFrame == 2428);
	CHECK(inProgressFrame == 2429);
	CHECK(doneFrame == 3169);
	CHECK(baseFrame == 3169);
	MESSAGE("EXPANSION target on frame " << chosenFrame << " at (" << chosenAt.x << ", " << chosenAt.y << "); SimpleExpansion on frame " << tacticFrame
		<< ", its request on " << requestFrame << " (last state " << lastState << ", in progress on " << inProgressFrame << ", complete on " << doneFrame
		<< "), the second base on " << baseFrame << "; bases " << aiPtr->build.bases.size() << ", tactics started " << aiPtr->brain.tacticsStarted
		<< ", unported dispatches " << aiPtr->brain.unportedTypes);
}
