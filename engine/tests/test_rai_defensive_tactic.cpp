// OpenBFME unit tests. GPL-3.0.
// Lane RAI-DEF: the skirmish AI's DEFENSIVE target heuristic (RW 0x97D74F) and the SimpleDefense tactic it feeds (RW 0x90C436 /
// 0x9B4F8F / 0x9B4EF0 / 0x9B4EB7). A retail 1 v 1 on Tournament Udun: an idle human Mordor player at start 0, a Medium computer Men
// player at start 1. The AI's own structures feed its player info record's position sampler; an enemy hero next to two outlying AI
// structures is the attacked base the heuristic sees.
//
// The expectations follow the RW functions, recomputed here from the game's state (the sampler's groups, the army list), never read
// back from the port.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "PeImage.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
std::uint32_t callTarget(const retailtest::PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> d;
	if (!pe.read(va, 5, &d) || d[0] != 0xE8)
	{
		return 0;
	}
	return va + 5u + pe.u32At(va + 1);
}

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

struct DefenseGame
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *human = nullptr;
	Player *computer = nullptr;
	AISkirmishPlayer *ai = nullptr;
};

void loadUdun(starttest::Shared &s, DefenseGame &g)
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
	// no advance: the test places its structures and the enemy before the AI's first update, so the DEFENSIVE target is the AI's first
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
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

Object *makeBuilding(DefenseGame &g, const char *name, float x, float y, float z)
{
	std::string error;
	const Coord3D pos{ x, y, z };
	Object *o = g.game->createObject(name, g.computer->getPlayerIndex(), pos, 0.0f, &error);
	REQUIRE_MESSAGE(o, error);
	o->setPosition(&pos); // createObject snaps z to the ground; the test's exact geometry uses this z for both structures
	return o;
}

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

float dist2(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return dy * dy + dx * dx;
}
} // namespace

TEST_CASE("rai-def binary facts: the DEFENSIVE heuristic and the type-1 dispatch (RW 0x97D74F / 0x90C436 / 0x9B4EF0 / 0x9B4EB7)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("rai-def binary facts (RW_GAME_DAT unset)");
		return;
	}
	// RW 0x97D74F: the player info record (+ 0x10 the sampler), the two KindOf masks, the two draws and the 200 radius
	CHECK(callTarget(*pe, 0x97D76B) == 0x6A9999u);
	CHECK(pe->hex(0x97D77E, 3) == "8b7610");       // mov esi, [esi + 0x10] (the sampler)
	CHECK(pe->hex(0x97D7DD, 4) == "834d8008");     // or dword [ebp - 0x80], 8: CAN_ATTACK (3)
	CHECK(pe->hex(0x97D7E1, 3) == "097d88");       // or dword [ebp - 0x78], edi: HERO (90)
	CHECK(pe->hex(0x97D7E4, 3) == "097d98");       // or dword [ebp - 0x68], edi: SUPPORT (218)
	CHECK(pe->hex(0x97D7E7, 4) == "804da240");     // or byte [ebp - 0x5e], 0x40: UNATTACKABLE (54)
	CHECK(pe->hex(0x97D7EB, 4) == "804db380");     // or byte [ebp - 0x4d], 0x80: SHIP (191)
	CHECK(pe->hex(0x97D7EF, 4) == "804db540");     // or byte [ebp - 0x4b], 0x40: PASS_EXPERIENCE_TO_CONTAINER (206)
	CHECK(pe->hex(0x97D8F0, 2) == "6a57");         // push 0x57 (the pick)
	CHECK(pe->hex(0x97D92A, 2) == "6a60");         // push 0x60 (the small-group gate)
	CHECK(pe->hex(0x97D93B, 4) == "83f81e0f");     // cmp eax, 30
	CHECK(pe->hex(0x97D915, 6) == "c7402c020000"); // target + 0x2C = 2 (Large)
	CHECK(pe->hex(0x97D91E, 6) == "c7402c010000"); // target + 0x2C = 1 (Med)
	CHECK(pe->hex(0x97D945, 8) == "d9057041be00518b"); // fld [0xBE4170] = 200.0f, the target radius
	// RW 0x90C436: the defensive prototype list, the draw at AITacticsGenerator.cpp 0x177, the start and the defensive active list
	CHECK(pe->hex(0x90C458, 6) == "8b5e1ceb2c8b"); // mov ebx, [esi + 0x1C]; jmp
	CHECK(pe->hex(0x90C489, 6) == "3b5e2075cf8b"); // cmp ebx, [esi + 0x20]; jne
	CHECK(pe->hex(0x90C49A, 5) == "6877010000");   // push 0x177
	CHECK(pe->hex(0x90C4CF, 3) == "8d4e28");       // lea ecx, [esi + 0x28] (the active list)
	CHECK(callTarget(*pe, 0x90C4C6) == 0x8F21F7u);
	// RW 0x9B4EF0 (SimpleDefense setup): the recruit origin (+ 0x324 / + 0x328), the sizes 1, 3 and half the army
	CHECK(callTarget(*pe, 0x9B4F01) == 0x79FD4Du);
	CHECK(pe->hex(0x9B4F09, 6) == "8b402c83e800"); // mov eax, [eax + 0x2C]; sub eax, 0
	CHECK(pe->hex(0x9B4F4E, 6) == "8987d0020000"); // mov [edi + 0x2D0], eax (half the army)
	CHECK(pe->hex(0x9B4F56, 10) == "c787d002000003000000"); // mov [edi + 0x2D0], 3
	CHECK(pe->hex(0x9B4F62, 10) == "c787d002000001000000"); // mov [edi + 0x2D0], 1
	// RW 0x9B4EB7 (SimpleDefense update): team 0, all teams idle, the target's inactive flag
	CHECK(callTarget(*pe, 0x9B4EBE) == 0x8F128Bu);
	CHECK(callTarget(*pe, 0x9B4ED2) == 0x8F175Au);
	CHECK(pe->hex(0x9B4EDB, 3) == "8b4620"); // mov eax, [esi + 0x20] (the target)
}

TEST_CASE("rai-def defensive tactic: an enemy at two outlying AI structures gives a DEFENSIVE target and a SimpleDefense team that attack-moves to it "
		  "(RW 0x97D74F / 0x90C436 / 0x9B4EF0 / 0x9B4EB7)")
{
	OPENBFME_REQUIRE_START(s);
	DefenseGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer &ai = *g.ai;
	const AIStatCollector &record = AITacticalAI::recordOf(logic, g.computer->getPlayerIndex());

	// two AI structures 900 from the AI's base towards the human form a group of two: the heuristic's defense size 1 (team maximum 3) and no
	// 30% gate; the AI's three extra hordes wait at its base, out of the enemy's reach, so they have no victims and can be recruited
	const Waypoint *humanStart = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(humanStart != nullptr);
	const Coord3D aiStart = ai.build.startPosition;
	float ux = humanStart->location.x - aiStart.x, uy = humanStart->location.y - aiStart.y;
	const float ulen = std::sqrt(ux * ux + uy * uy);
	REQUIRE(ulen > 0.0f); // tolerance: a precondition (the two start points differ)
	ux /= ulen;
	uy /= ulen;
	const float px = -uy, py = ux;
	const float farX = aiStart.x + ux * 900.0f, farY = aiStart.y + uy * 900.0f;
	const float z = logic.getGroundHeight(farX, farY);
	for (int i = 0; i < 3; ++i)
	{
		std::string error;
		const Coord3D at{ aiStart.x + px * (float)(i * 40), aiStart.y + py * (float)(i * 40), logic.getGroundHeight(aiStart.x, aiStart.y) };
		Object *unit = g.game->createObject("GondorFighterHorde", g.computer->getPlayerIndex(), at, 0.0f, &error);
		REQUIRE_MESSAGE(unit, error);
	}
	Object *a = makeBuilding(g, "GondorBarracks", farX, farY, z);
	Object *b = makeBuilding(g, "GondorBarracks", farX + 100.0f, farY, z);
	if (AIStatCollector *live = logic.skirmishAI().record(g.computer->getPlayerIndex()))
	{
		live->update(logic); // the sampler add is deferred until the object's position is final
	}
	logic.partition().update();
	const AIPositionSampler::Group *group = nullptr;
	for (const AIPositionSampler::Group &grp : record.positions().groups())
	{
		if (std::find(grp.members.begin(), grp.members.end(), a->getID()) != grp.members.end())
		{
			group = &grp;
		}
	}
	REQUIRE(group != nullptr);
	REQUIRE(group->members.size() == 2u); // the two new structures only: far from every base structure
	CHECK(group->radius == g.ai->store->data().defenseTreeNodeRadius + 50.0f);
	CHECK(group->center.x == farX + 50.0f);
	CHECK(group->center.y == farY);

	// the enemy hero at that group: the attacked base the heuristic sees
	Team *enemyTeam = enemyTeamOf(logic, *g.computer, *g.human);
	REQUIRE(enemyTeam != nullptr);
	const ThingTemplate *hero = logic.things().findTemplate("MordorWitchKing");
	REQUIRE(hero != nullptr);
	Object *raider = logic.newObject(hero, enemyTeam, ObjectStatusMaskType{});
	REQUIRE(raider != nullptr);
	const Coord3D raiderPos{ farX + 50.0f, farY + 10.0f, z };
	raider->setPosition(&raiderPos);
	logic.partition().update();
	REQUIRE(g.computer->getRelationship(raider->getTeam()) == ENEMIES);

	int targetFrame = -1, tacticFrame = -1, startedFrame = -1, endedFrame = -1;
	int targetIndex = -1;
	bool killed = false;
	Coord3D chosenPos{ 0.0f, 0.0f, 0.0f };
	std::vector<ObjectID> members;
	for (int f = 0; f < 1500 && endedFrame < 0; ++f)
	{
		g.game->advance(0.2);
		const int frame = (int)logic.getFrame();
		for (size_t i = 0; i < ai.brain.targets.size(); ++i)
		{
			const AITarget &t = ai.brain.targets[i];
			if (t.type != 1 || t.inactive || targetFrame >= 0)
			{
				continue;
			}
			targetFrame = frame;
			targetIndex = (int)i;
			chosenPos = t.position;
			CHECK(t.object == raider->getID());
			CHECK(t.position.x == raider->getPosition()->x);
			CHECK(t.position.y == raider->getPosition()->y);
			CHECK(t.radius == 200.0f); // RW 0xBE4170
			CHECK(t.teams == 1); // the dispatch in this same brain update took it
			CHECK(t.frame == logic.getFrame());
		}
		for (const AITactic &tac : ai.brain.tactics)
		{
			if (tac.kind != "SimpleDefense")
			{
				continue;
			}
			if (tacticFrame < 0)
			{
				tacticFrame = frame;
				REQUIRE(tac.teams.size() == 1u);
				CHECK(tac.teams[0].type == 1); // the AITARGET DEFENSIVE
				CHECK(tac.teams[0].minimum == 1);
				CHECK(tac.teams[0].maximum == 3); // defense size 1 (two structures), RW 0x9B4F56
				CHECK(tac.teams[0].index == 0);
				CHECK(tac.targetIndex == targetIndex);
			}
			static int lastMembers = -1;
			if ((int)tac.teams[0].members.size() != lastMembers || tac.ended)
			{
				lastMembers = (int)tac.teams[0].members.size();
				MESSAGE("frame " << frame << ": SimpleDefense members " << tac.teams[0].members.size() << " wants " << tac.teams[0].wantsMembers << " handed "
					<< tac.teams[0].handedOver << " ready " << tac.ready << " started " << tac.started << " ended " << tac.ended
					<< "; army " << record.army().size() << ", target teams "
					<< (tac.targetIndex >= 0 && tac.targetIndex < (int)ai.brain.targets.size() ? ai.brain.targets[(size_t)tac.targetIndex].teams : -1));
			}
			if (tac.started && startedFrame < 0)
			{
				startedFrame = frame;
				// tolerance: the exact count is the AI's state; the rule is that the launch issued the attack order
				CHECK(ai.brain.attacksLaunched >= 1u);
				REQUIRE(tac.teams.size() == 1u);
				members = tac.teams[0].members;
				REQUIRE_FALSE(members.empty());
				// tolerance: the DEFENSIVE hand-over fires at the minimum (one unit); how many of the maximum (three) the army had by then is its state
				CHECK(members.size() <= 3u);
				// RW 0x9A2CA5: a defensive team recruits the army units nearest its origin first
				for (size_t i = 1; i < members.size(); ++i)
				{
					const Object *prev = logic.findObjectByID(members[i - 1]), *cur = logic.findObjectByID(members[i]);
					REQUIRE(prev != nullptr);
					REQUIRE(cur != nullptr);
					// tolerance: the rule is nearest-first; equal squared distances keep the registration order
					CHECK(dist2(*prev->getPosition(), chosenPos) <= dist2(*cur->getPosition(), chosenPos));
				}
			}
			if (tac.ended)
			{
				endedFrame = frame;
				CHECK(tac.success); // RW 0x9B4EB7: all teams idle or the target inactive => end(1, 0)
			}
		}
		// once the team is on its way, killing the enemy makes the target inactive and the tactic end with success
		if (startedFrame >= 0 && !killed)
		{
			killed = true;
			raider->kill(DAMAGE_UNRESISTABLE, 0);
		}
	}
	// tolerance: a precondition (the target was chosen); the frame is the game state
	REQUIRE(targetFrame >= 0);
	// tolerance: a precondition (the tactic was started)
	REQUIRE(tacticFrame >= 0);
	CHECK(tacticFrame == targetFrame); // RW 0x6C74C2: the chooser, the best target and the dispatch in one brain update
	// tolerance: a precondition (the tactic reached its launch)
	REQUIRE(startedFrame >= 0);
	// tolerance: the launch follows its start; the exact gap is the game state
	CHECK(startedFrame >= tacticFrame);
	// tolerance: a precondition (the tactic ended)
	REQUIRE(endedFrame >= 0);
	MESSAGE("DEFENSIVE target on frame " << targetFrame << " at (" << chosenPos.x << ", " << chosenPos.y << "); SimpleDefense on " << tacticFrame
			<< ", started " << startedFrame << ", ended " << endedFrame << "; members " << members.size());
}

// review probe (PR #231, prtrkt): RW 0x6C74C2 passes the brain's own player (brain + 8, RW 0x6C74F6) to the dispatch RW 0x90CB9F -> 0x90C436; the start
// RW 0x8F21F7 stores it at the tactic's + 0x24 and SimpleDefense's setup RW 0x9B4EF0 sizes a large-target team from THAT player's record (RW 0x9B4F17), i.e.
// the allied AI that recruits the team, not the group's first AI that owns the target. Two allied computers, the chooser with 2 extra hordes and its ally
// with 8; three chooser structures under attack (defense size 2); the DEFENSIVE target allows two teams (a mod's MaxTeamsPerTarget; retail data has 1)
TEST_CASE("rai-def review probe: an allied AI's SimpleDefense team is sized from its own army (RW 0x6C74F6 / 0x8F21F7 / 0x9B4F17)")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	const char *factions[3] = { "FactionMordor", "FactionMen", "FactionMen" };
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Human" : i == 1 ? u"Chooser" : u"Ally";
		slot.playerTemplate = factionIndex(s->world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: a precondition (the faction exists)
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i == 0 ? 0 : 1;
	}
	DefenseGame g;
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s->mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s->world, *s->mount->fs, *g.assets, s->options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	GameLogic &logic = g.game->logic();
	AISkirmishPlayer *chooser = nullptr, *ally = nullptr;
	for (const auto &grp : logic.skirmishAI().groups())
	{
		for (auto &kv : grp->players)
		{
			(kv.second->brain.chooser ? chooser : ally) = kv.second.get();
		}
	}
	REQUIRE(chooser != nullptr);
	REQUIRE(ally != nullptr);
	REQUIRE(ally->groupFirst == chooser);
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == 0)
		{
			g.human = p;
		}
	}
	REQUIRE(g.human != nullptr);
	g.computer = logic.players().getNthPlayer(chooser->playerIndex);
	REQUIRE(g.computer != nullptr);
	g.ai = chooser;
	// the extra hordes wait at each AI's base: 2 for the chooser, 8 for the ally
	for (AISkirmishPlayer *who : { chooser, ally })
	{
		const Coord3D base = who->build.startPosition;
		for (int i = 0; i < (who == chooser ? 2 : 8); ++i)
		{
			const Coord3D at{ base.x + (float)(i * 40), base.y, logic.getGroundHeight(base.x + (float)(i * 40), base.y) };
			REQUIRE_MESSAGE(g.game->createObject("GondorFighterHorde", who->playerIndex, at, 0.0f, &error), error);
		}
	}
	// three chooser structures 900 from its base towards the human: one group of three (defense size 2, the half-army maximum)
	const Waypoint *humanStart = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(humanStart != nullptr);
	const Coord3D aiStart = chooser->build.startPosition;
	float ux = humanStart->location.x - aiStart.x, uy = humanStart->location.y - aiStart.y;
	const float ulen = std::sqrt(ux * ux + uy * uy);
	REQUIRE(ulen > 0.0f); // tolerance: a precondition (the two start points differ)
	ux /= ulen;
	uy /= ulen;
	const float farX = aiStart.x + ux * 900.0f, farY = aiStart.y + uy * 900.0f;
	const float z = logic.getGroundHeight(farX, farY);
	for (int i = 0; i < 3; ++i)
	{
		makeBuilding(g, "GondorBarracks", farX + (float)(i * 100), farY, z);
	}
	for (AISkirmishPlayer *who : { chooser, ally })
	{
		if (AIStatCollector *live = logic.skirmishAI().record(who->playerIndex))
		{
			live->update(logic);
		}
	}
	logic.partition().update();
	// the chooser makes its targets on its first brain update; then the DEFENSIVE one allows two teams
	for (int f = 0; f < 600 && !chooser->brain.chooserInitialised; ++f)
	{
		g.game->advance(0.2);
	}
	REQUIRE(chooser->brain.chooserInitialised);
	for (AITarget &t : chooser->brain.targets)
	{
		if (t.type == 1)
		{
			t.maxTeams = 2;
		}
	}
	Team *enemyTeam = enemyTeamOf(logic, *g.computer, *g.human);
	REQUIRE(enemyTeam != nullptr);
	const ThingTemplate *hero = logic.things().findTemplate("MordorWitchKing");
	REQUIRE(hero != nullptr);
	Object *raider = logic.newObject(hero, enemyTeam, ObjectStatusMaskType{});
	REQUIRE(raider != nullptr);
	const Coord3D raiderPos{ farX + 100.0f, farY + 10.0f, z };
	raider->setPosition(&raiderPos);
	logic.partition().update();
	bool seen[2] = { false, false };
	for (int f = 0; f < 1500 && !(seen[0] && seen[1]); ++f)
	{
		g.game->advance(0.2);
		for (int k = 0; k < 2; ++k)
		{
			AISkirmishPlayer *who = k == 0 ? chooser : ally;
			for (const AITactic &tac : who->brain.tactics)
			{
				if (seen[k] || tac.kind != "SimpleDefense" || tac.teams.empty())
				{
					continue;
				}
				seen[k] = true;
				// the frame the tactic started: its setup read the record's army list in this frame's brain update
				const size_t own = AITacticalAI::recordOf(logic, who->playerIndex).army().size();
				const size_t first = AITacticalAI::recordOf(logic, chooser->playerIndex).army().size();
				MESSAGE(std::string(k == 0 ? "chooser" : "ally") << " SimpleDefense on frame " << logic.getFrame() << ": maximum " << tac.teams[0].maximum
						<< ", own army " << own << ", chooser army " << first);
				REQUIRE(own > 1u); // tolerance: a precondition (the half-army branch, RW 0x9B4F2C)
				CHECK(tac.teams[0].maximum == (int)(own / 2u)); // RW 0x9B4F17: the recruiting AI's own record
			}
		}
	}
	const int checked = (seen[0] ? 1 : 0) + (seen[1] ? 1 : 0);
	REQUIRE(checked == 2); // tolerance: a precondition (both allies started a SimpleDefense on the shared target)
}

TEST_CASE("rai-pow-2 review probe: an enemy acquire asks every other skirmish AI for its enemy, units or not (RW 0x8F46C7 / 0x6AA5B3 / 0x6AA5F5)")
{
	// RW 0x8F457D's inner loop gates the other player only on Player + 0x2FC's vslot 0x2C (RW 0x6AA5B3; 1 for a skirmish AI, RW 0x8BD372), then calls its
	// getEnemy (RW 0x6AA5F5 -> 0x8F47EB), which refreshes that AI's due cache. A computer that lost every unit but keeps its buildings is still asked
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	const char *factions[3] = { "FactionMordor", "FactionMen", "FactionMen" };
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Human" : i == 1 ? u"Asker" : u"Unitless";
		slot.playerTemplate = factionIndex(s->world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: a precondition (the faction exists)
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i; // three sides, all enemies
	}
	DefenseGame g;
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s->mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s->world, *s->mount->fs, *g.assets, s->options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	GameLogic &logic = g.game->logic();
	std::vector<AISkirmishPlayer *> ais;
	for (const auto &grp : logic.skirmishAI().groups())
	{
		for (auto &kv : grp->players)
		{
			ais.push_back(kv.second.get());
		}
	}
	REQUIRE(ais.size() == 2);
	std::sort(ais.begin(), ais.end(), [](const AISkirmishPlayer *a, const AISkirmishPlayer *b) { return a->playerIndex < b->playerIndex; });
	AISkirmishPlayer *asker = ais[0], *unitless = ais[1];
	const Player *unitlessPlayer = logic.players().getNthPlayer(unitless->playerIndex);
	REQUIRE(unitlessPlayer != nullptr);
	// the second computer loses every non-structure object; its buildings stay
	std::vector<ObjectID> doomed;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == unitlessPlayer && !o->isKindOfName("STRUCTURE"))
		{
			doomed.push_back(o->getID());
		}
	}
	REQUIRE(!doomed.empty()); // tolerance: a precondition (the computer starts with a builder)
	for (ObjectID id : doomed)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.destroyObject(o);
		}
	}
	g.game->advance(0.2);
	bool hasStructure = false;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == unitlessPlayer && !o->isEffectivelyDead())
		{
			REQUIRE(o->isKindOfName("STRUCTURE")); // tolerance: a precondition (only buildings are left)
			hasStructure = true;
		}
	}
	REQUIRE(hasStructure);
	// both caches due and empty; the asker acquires
	asker->enemyIndex = -1;
	asker->enemyRefreshFrame = 0;
	unitless->enemyIndex = -1;
	unitless->enemyRefreshFrame = 0;
	const std::uint32_t now = logic.getFrame();
	REQUIRE(AITacticalAI::enemyPlayer(logic, *asker) != nullptr);
	CHECK(asker->enemyRefreshFrame == now + 25);
	// retail asked the unitless computer too: its cache refreshed in the asker's acquire
	CHECK(unitless->enemyRefreshFrame == now + 25);
	CHECK(unitless->enemyIndex >= 0); // tolerance: which of its two enemies is nearer depends on the map; the point is that it acquired one
}
