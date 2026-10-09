// OpenBFME unit tests. GPL-3.0.
// Lane AI-1, step 4: the skirmish AI's tactical layer (GameLogic/SkirmishAI/AITacticalAI.h): the enemy manager, the target chooser, the SimpleAttack tactic and
// its team. Retail skirmishes on Evendim: each faction as the Medium computer against an idle human sends a team that reaches the human base; two runs agree on
// every frame hash. The expectations follow the RW functions named in AITacticalAI.h, never this engine's own output.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <map>
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

NewGameMessage attackMessage(const starttest::Shared &s, const std::string &aiFaction, const std::string &otherFaction, bool otherIsAI, std::uint32_t seed = 1234)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = otherIsAI ? SLOT_MED_AI : SLOT_PLAYER;
	h.name = otherIsAI ? u"Computer2" : u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), otherFaction);
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_MED_AI;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), aiFaction);
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	return m;
}

struct AttackRun
{
	std::vector<std::uint32_t> hashes;
	int firstAttackFrame = -1;     // the frame the first SimpleAttack team was sent
	int arrivalFrame = -1;         // the first frame an AI army unit stood within kArrival of the human start
	unsigned long long targetsSet = 0, tacticsStarted = 0, attacksLaunched = 0, tacticsEnded = 0;
	std::map<std::string, int> tacticKinds; // the kinds the generator picked (live at the end; ended ones are counted by the brain)
	int humanStructuresLost = 0;
	std::string lostTemplates; // the other player's starting structures that are gone at the end
	float humanStructureHealthLost = 0.0f; // the other player's structures' max health minus health, summed at the end (destroyed ones count their max)   // the human player's structures at the start minus those alive at the end
	std::string firstTarget;
	size_t farmSites = 0;
	int farmsBuilt = 0, farmsInFlight = 0, farmsCompletedPending = 0;
	unsigned long long farmRequests = 0, farmRefused = 0, farmNoSite = 0;
	std::map<std::string, int> economyStructures; // the AI's finished structures of its ArmyDefinition's economy template
	std::uint32_t aiMoney = 0;
	bool humanDefeated = false, aiDefeated = false;
	int defeatFrame = -1;
};

constexpr float kArrival = 450.0f;

void runAttackGame(starttest::Shared &s, const NewGameMessage &message, int frames, AttackRun &out, bool hashEveryFrame, bool stopAtDefeat = false)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	const SkirmishAIManager &m = logic.skirmishAI();
	// slot 0 is the "other" player, slot 1 the AI under test
	const AISkirmishPlayer *ai = nullptr;
	const Player *other = nullptr;
	for (const auto &g : m.groups())
	{
		for (const auto &kv : g->players)
		{
			const Player *p = logic.players().getNthPlayer(kv.first);
			if (p && p->getMultiplayerStartIndex() == 1)
			{
				ai = kv.second.get();
			}
		}
	}
	REQUIRE(ai != nullptr);
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == 0)
		{
			other = p;
		}
	}
	REQUIRE(other != nullptr);
	const Waypoint *wp = logic.terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(wp != nullptr);
	const Coord3D home = wp->location;
	auto structuresOf = [&](const Player *p) {
		int n = 0;
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			n += x->getControllingPlayer() == p && !x->isEffectivelyDead() && x->isKindOfName("STRUCTURE") ? 1 : 0;
		}
		return n;
	};
	const int structuresAtStart = structuresOf(other);
	std::map<ObjectID, std::string> startStructures;
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == other && !x->isEffectivelyDead() && x->isKindOfName("STRUCTURE"))
		{
			startStructures[x->getID()] = x->getTemplate()->getName();
		}
	}
	const Player *aiPlayer = logic.players().getNthPlayer(ai->playerIndex);
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		if (hashEveryFrame)
		{
			out.hashes.push_back(logic.computeStateHash());
		}
		if (out.firstAttackFrame < 0 && ai->brain.attacksLaunched > 0)
		{
			out.firstAttackFrame = (int)logic.getFrame();
			for (const AITarget &t : ai->brain.targets)
			{
				if (const Object *x = !t.inactive ? logic.findObjectByID(t.object) : nullptr)
				{
					out.firstTarget = x->getTemplate()->getName();
				}
			}
		}
		if (out.arrivalFrame < 0 && (i % 5) == 0)
		{
			for (ObjectID id : ai->brain.lists.army)
			{
				const Object *x = logic.findObjectByID(id);
				if (!x)
				{
					continue;
				}
				const float dx = x->getPosition()->x - home.x, dy = x->getPosition()->y - home.y;
				if (std::sqrt(dx * dx + dy * dy) < kArrival)
				{
					out.arrivalFrame = (int)logic.getFrame();
					break;
				}
			}
		}
		if (other->isDefeated() || aiPlayer->isDefeated())
		{
			out.humanDefeated = other->isDefeated();
			out.aiDefeated = aiPlayer->isDefeated();
			if (out.defeatFrame < 0)
			{
				out.defeatFrame = (int)logic.getFrame();
			}
			if (stopAtDefeat)
			{
				break;
			}
		}
	}
	out.farmSites = ai->farmSites ? ai->farmSites->size() : 0;
	out.farmsBuilt = ai->build.economy.built;
	out.farmsInFlight = ai->build.economy.inFlight;
	// A request executed after completion polling can already have a completed object; the AI acknowledges it next frame.
	for (const auto &r : ai->build.economy.requests)
	{
		const Object *x = logic.findObjectByID(r->built);
		if (r->state == 1 && x && x->getControllingPlayer() == aiPlayer && !x->isEffectivelyDead() && !x->isUnderConstruction() && ai->army &&
			x->getTemplate()->getName() == ai->army->economyTemplate)
		{
			++out.farmsCompletedPending;
		}
	}
	out.farmRequests = ai->build.economy.made;
	out.farmRefused = ai->build.economy.refusedByDifficulty;
	out.farmNoSite = ai->build.economy.noSite;
	out.aiMoney = aiPlayer->getMoney()->countMoney();
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == aiPlayer && !x->isEffectivelyDead() && !x->isUnderConstruction() && ai->army &&
			x->getTemplate()->getName() == ai->army->economyTemplate)
		{
			++out.economyStructures[x->getTemplate()->getName()];
		}
	}
	out.tacticKinds = ai->brain.tacticsByKind;
	out.targetsSet = ai->brain.targetsSet;
	out.tacticsStarted = ai->brain.tacticsStarted;
	out.attacksLaunched = ai->brain.attacksLaunched;
	out.tacticsEnded = ai->brain.tacticsEnded;
	out.humanStructuresLost = structuresAtStart - structuresOf(other);
	for (const auto &kv : startStructures)
	{
		const Object *x = logic.findObjectByID(kv.first);
		if (!x || x->isEffectivelyDead())
		{
			out.lostTemplates += kv.second + " ";
		}
	}
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == other && x->isKindOfName("STRUCTURE") && x->getBodyModule())
		{
			out.humanStructureHealthLost += x->getBodyModule()->getMaxHealth() - x->getBodyModule()->getHealth();
		}
	}
}
} // namespace

TEST_CASE("skirmish ai attack: the brain hash covers the tactical state (each mutation moves it)")
{
	AIBrainState b;
	b.enemies.emplace_back(0, 10.0f);
	b.targets.push_back(AITarget{});
	AITactic tac;
	tac.kind = "SimpleAttack";
	tac.teams.push_back(AITacticTeam{});
	b.tactics.push_back(tac);
	auto hash = [&] {
		StateHasher h;
		AITacticalAI::crc(b, h);
		return h.value();
	};
	auto moved = [&](auto mutate) {
		const std::uint32_t h0 = hash();
		mutate();
		return hash() != h0;
	};
	CHECK(moved([&] { b.currentEnemy = 0; }));
	CHECK(moved([&] { b.enemies[0].second = 11.0f; }));
	CHECK(moved([&] { b.targets[0].inactive = false; }));
	CHECK(moved([&] { b.targets[0].dropped = true; }));
	CHECK(moved([&] { b.targets[0].teams = 1; }));
	CHECK(moved([&] { b.targets[0].position.y = 5.0f; }));
	CHECK(moved([&] { b.targets[0].object = 7; }));
	CHECK(moved([&] { b.targets[0].frame = 3; }));
	CHECK(moved([&] { b.bestTarget = 0; }));
	CHECK(moved([&] { b.tactics[0].started = true; }));
	CHECK(moved([&] { b.tactics[0].readyFrame = 9; }));
	CHECK(moved([&] { b.tactics[0].teams[0].minimum = 4; }));
	CHECK(moved([&] { b.tactics[0].teams[0].members.push_back(12); }));
	CHECK(moved([&] { b.tactics[0].teams[0].handedOver = true; }));
	CHECK(moved([&] { b.lists.army.push_back(3); }));
	CHECK(moved([&] { b.lists.structures.push_back(4); }));
	CHECK(moved([&] { b.attacksLaunched += 1; }));
	CHECK(moved([&] { b.tactics[0].teams[0].maximum = 6; }));
	CHECK(moved([&] { b.tactics[0].kindIndex = 3; }));
	CHECK(moved([&] { b.tacticsByKind["FlankAttack"] = 1; }));
	CHECK(moved([&] { b.tacticsByKind["FlankAttack"] = 2; }));
	CHECK(moved([&] { b.tacticsByKind["PincerAttack"] = 1; }));
	CHECK(moved([&] { b.tacticsStarted += 1; }));
	CHECK(moved([&] { b.tacticsEnded += 1; }));
	CHECK(moved([&] { b.gateRefusals += 1; }));
	CHECK(moved([&] { b.unportedTypes += 1; }));
	CHECK(moved([&] { b.targetsSet += 1; }));
	CHECK(moved([&] { b.nextTacticId += 1; }));
	CHECK(moved([&] { b.nextTargetId += 1; }));
	CHECK(moved([&] { b.tactics[0].targetOwner = 2; }));
	CHECK(moved([&] { b.tactics[0].success = true; }));
	CHECK(moved([&] { b.lists.dozers.push_back(8); }));
}

TEST_CASE("skirmish ai attack: each of the 7 factions as the Medium computer on Evendim attacks an idle human within 8 minutes")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		AttackRun run;
		runAttackGame(*s, attackMessage(*s, faction, "FactionMen", false), 2400, run, false);
		MESSAGE(faction << ": targets set " << run.targetsSet << " (first '" << run.firstTarget << "'), tactics " << run.tacticsStarted << ", attacks " << run.attacksLaunched
						<< " (first at frame " << run.firstAttackFrame << "), ended " << run.tacticsEnded << ", arrival frame " << run.arrivalFrame
						<< ", human structures lost " << run.humanStructuresLost << " (" << run.lostTemplates << "), health lost " << run.humanStructureHealthLost << ", kinds " << [&] {
			std::string k;
			for (const auto &kv : run.tacticKinds)
			{
				k += kv.first + "x" + std::to_string(kv.second) + " ";
			}
			return k;
		}());
		CHECK(run.targetsSet >= 1);
		CHECK(run.attacksLaunched >= 1);
		CHECK(run.arrivalFrame > 0);
		CHECK(run.arrivalFrame <= 2400);
	}
}

TEST_CASE("skirmish ai attack: two runs of the same AI skirmish agree on every frame hash while it attacks (Mordor, 2000 frames)")
{
	OPENBFME_REQUIRE_START(s);
	AttackRun a, b;
	runAttackGame(*s, attackMessage(*s, "FactionMordor", "FactionMen", false), 2000, a, true);
	runAttackGame(*s, attackMessage(*s, "FactionMordor", "FactionMen", false), 2000, b, true);
	CHECK(a.hashes == b.hashes);
	CHECK(a.attacksLaunched >= 1);
	CHECK(a.firstAttackFrame == b.firstAttackFrame);
}

TEST_CASE("skirmish ai attack: the tactical layer's stops are reported (S-417 not ported, S-418 inference)")
{
	int s417 = 0, s418 = 0, s890 = 0, s891 = 0, s892 = 0, s893 = 0, s894 = 0, s895 = 0;
	for (const std::string &l : SkirmishAIManager::stopLines())
	{
		s417 += l.rfind("[S-417]", 0) == 0 ? 1 : 0;
		s418 += l.rfind("[S-418]", 0) == 0 ? 1 : 0;
		s890 += l.rfind("[S-890]", 0) == 0 ? 1 : 0;
		s891 += l.rfind("[S-891]", 0) == 0 ? 1 : 0;
		s892 += l.rfind("[S-892]", 0) == 0 ? 1 : 0;
		s893 += l.rfind("[S-893]", 0) == 0 ? 1 : 0;
		s894 += l.rfind("[S-894]", 0) == 0 ? 1 : 0;
		s895 += l.rfind("[S-895]", 0) == 0 ? 1 : 0;
	}
	CHECK(s417 == 1);
	CHECK(s418 == 1);
	CHECK(s890 == 1);
	CHECK(s892 == 1);
	CHECK(s893 == 1);
	CHECK(s894 == 1);
	CHECK(s895 == 1);
	CHECK(s891 == 1);
	// lane AI-3: + S-1300 (threat finder), S-1301 (team group step), S-1302 (best target threat gate)
	int s1300 = 0, s1301 = 0, s1302 = 0;
	for (const std::string &l : SkirmishAIManager::stopLines())
	{
		s1300 += l.rfind("[S-1300]", 0) == 0 ? 1 : 0;
		s1301 += l.rfind("[S-1301]", 0) == 0 ? 1 : 0;
		s1302 += l.rfind("[S-1302]", 0) == 0 ? 1 : 0;
	}
	CHECK(s1300 == 1);
	CHECK(s1301 == 1);
	CHECK(s1302 == 1);
	CHECK(SkirmishAIManager::stopLines().size() == 22); // S-410 (with S-411), S-412, S-415, S-416, S-413, S-414, S-417, S-419, S-420, S-418, S-890, S-891, S-892, S-1500, S-1501, S-1502 (lane MOVE-2), S-893, S-894, S-895, S-1300, S-1301, S-1302
}

TEST_CASE("skirmish ai economy: each of the 7 factions as the Medium computer on Evendim builds its farms from the map's FarmTemplate sites (RW 0x8EEF8F)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		AttackRun run;
		runAttackGame(*s, attackMessage(*s, faction, "FactionMen", false), 1500, run, false);
		int farms = 0;
		for (const auto &kv : run.economyStructures)
		{
			farms += kv.second;
		}
		MESSAGE(faction << ": farm sites " << run.farmSites << ", farm requests " << run.farmRequests << " (difficulty refusals " << run.farmRefused << ", no site "
						<< run.farmNoSite << "), built " << run.farmsBuilt << ", in flight " << run.farmsInFlight << ", finished farms " << farms << ", money " << run.aiMoney);
		CHECK(run.farmSites > 0);
		CHECK(run.farmsBuilt >= 1);
		// lane AI-2: >= since a base layout may also place the economy template (the base builder's entries are not the economy builder's requests)
		CHECK(farms >= run.farmsBuilt + run.farmsCompletedPending);
		CHECK(run.farmsCompletedPending <= run.farmsInFlight);
		CHECK(run.farmsBuilt + run.farmsInFlight >= 4); // Wild's dozers are still busy with its larger base: 1 finished, 3 under way at frame 1500
	}
}

TEST_CASE("skirmish ai attack: AI vs AI on Evendim (Medium Mordor against Medium Men): both build, train and attack; no side is beaten in the first 2400 frames (S-420)")
{
	// S-420: with seed 2 (Mordor's "Rush Base") both AIs develop and Mordor attacks; nobody is beaten in the first 8 minutes (2400 frames; the full 30-minute
	// game is pinned in "skirmish ai vs ai: Evendim, 30 game-minutes"); with seed 1234 Mordor takes "Tech Up Base" and (since PATH-2's geometry) keeps its dozers,
	// farms and attacks; a lost dozer is still never replaced (no dozer production, no request failure on a dozer's death: S-413 / S-414). The test pins both outcomes.
	OPENBFME_REQUIRE_START(s);
	{
		AttackRun run;
		runAttackGame(*s, attackMessage(*s, "FactionMordor", "FactionMen", true, 2u), 2400, run, false, true);
		MESSAGE("AI vs AI (seed 2): defeat frame " << run.defeatFrame << ", Mordor attacks " << run.attacksLaunched << ", tactics " << run.tacticsStarted
												 << ", Mordor farms " << run.farmsBuilt);
		CHECK(run.attacksLaunched >= 1);
		CHECK(run.farmsBuilt >= 2);
		CHECK(run.defeatFrame == -1); // not within 2400 frames (engine pin)
	}
	{
		AttackRun run;
		runAttackGame(*s, attackMessage(*s, "FactionMordor", "FactionMen", true, 1234u), 1600, run, false, true);
		MESSAGE("AI vs AI (seed 1234): Mordor farms " << run.farmsBuilt << " in flight " << run.farmsInFlight << ", attacks " << run.attacksLaunched);
		// before PATH-2 the two dozers were killed at their 0 % foundations and the base stalled; with PATH-2's multi-shape footprints and the
		// cylinder bounding sphere (RW 0xAD2770) the defenders' reach and the build sites change, the dozers survive and Mordor develops
		// (engine pin, not a retail value: S-420)
		CHECK(run.farmsBuilt >= 1);
		CHECK(run.attacksLaunched >= 1);
	}
}
