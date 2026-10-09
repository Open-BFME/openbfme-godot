// OpenBFME unit tests. GPL-3.0.
// Lane AI-3 (QA-1 follow-up U22): long skirmish games between computer players with an idle human on Tournament Udun (2 v 2). Retail's AI keeps attacking and
// such games end; the expectations here are outcomes (tactics end, attacks keep coming, the idle human is attacked), never this engine's frame-exact output.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <variant>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int u22FactionIndex(const PlayerTemplateStore &store, const std::string &name)
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

// QA-1's U22 setup: slot 0 an idle human (team 0), slot 1 its computer ally (team 0), slots 2 and 3 computer enemies (team 1); `difficulty` SLOT_EASY_AI + 0..3
NewGameMessage udunMessage(const starttest::Shared &s, const std::vector<std::string> &factions, int difficulty, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : (SlotState)(SLOT_EASY_AI + difficulty);
		slot.name = i == 0 ? u"Player0" : u"Player" + std::u16string(1, (char16_t)(u'0' + i));
		slot.playerTemplate = u22FactionIndex(s.world->playerTemplates(), factions[(size_t)i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i < 2 ? 0 : 1;
	}
	return m;
}

const Player *u22PlayerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

struct U22Run
{
	int defeatFrame[4] = { -1, -1, -1, -1 };
	int firstHumanHit = -1;              ///< the first frame an object of the idle human lost health
	unsigned long long attacks[4] = {};  ///< tactics launched per start position
	unsigned long long ended[4] = {};
	int longestTactic = 0;               ///< the longest a tactic stayed in the active list (frames)
	std::string longestTacticKind;
	std::string log;
};

std::string tacticLine(GameLogic &logic, const AITactic &t)
{
	std::ostringstream os;
	os << " [" << t.kind << "#" << t.id << (t.started ? " S" : "") << " step " << t.step << "/" << t.step2;
	for (const AITacticTeam &team : t.teams)
	{
		os << " team" << team.index << " n" << team.members.size() << " idle " << team.idleFrames;
		for (ObjectID id : team.members)
		{
			const Object *m = logic.findObjectByID(id);
			if (!m)
			{
				continue;
			}
			AIUpdateInterface *u = const_cast<Object *>(m)->getAIUpdateInterface();
			const Object *v = u ? logic.findObjectByID(u->currentVictimId()) : nullptr;
			os << " (" << m->getTemplate()->getName() << " " << (int)m->getPosition()->x << "," << (int)m->getPosition()->y << " st "
			   << (u && u->stateMachine().currentState() ? u->stateMachine().currentState()->name() : std::string("-")) << " idle " << (u ? (int)u->isIdle() : -1)
			   << " v " << (v ? v->getTemplate()->getName() + (v->isEffectivelyDead() ? "(dead)" : "") : std::string("-"));
			if (v)
			{
				const float dx = v->getPosition()->x - m->getPosition()->x, dy = v->getPosition()->y - m->getPosition()->y;
				os << " hp " << (v->getBodyModule() ? (int)v->getBodyModule()->getHealth() : -1) << " d " << (int)std::sqrt(dx * dx + dy * dy);
			}
			os << ")";
		}
	}
	os << "]";
	return os.str();
}

void runUdun(starttest::Shared &s, const NewGameMessage &message, int frames, U22Run &out, int logEvery)
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
	const Player *players[4];
	for (int k = 0; k < 4; ++k)
	{
		players[k] = u22PlayerAtStart(logic, k);
		REQUIRE(players[k] != nullptr);
	}
	std::map<ObjectID, float> humanHealth;
	std::map<std::pair<int, std::uint32_t>, std::uint32_t> firstSeen; // (start, tactic id) -> frame
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		const std::uint32_t frame = logic.getFrame();
		for (int k = 0; k < 4; ++k)
		{
			if (players[k]->isDefeated() && out.defeatFrame[k] < 0)
			{
				out.defeatFrame[k] = (int)frame;
			}
			const AISkirmishPlayer *ai = logic.skirmishAI().findAI(players[k]->getPlayerIndex());
			if (!ai)
			{
				continue;
			}
			out.attacks[k] = ai->brain.attacksLaunched;
			out.ended[k] = ai->brain.tacticsEnded;
			for (const AITactic &t : ai->brain.tactics)
			{
				if (t.targetless)
				{
					continue;
				}
				auto it = firstSeen.emplace(std::make_pair(k, t.id), frame).first;
				const int age = (int)(frame - it->second);
				if (age > out.longestTactic)
				{
					out.longestTactic = age;
					out.longestTacticKind = t.kind;
				}
			}
		}
		if (out.firstHumanHit < 0)
		{
			for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
			{
				if (x->getControllingPlayer() != players[0] || !x->getBodyModule())
				{
					continue;
				}
				const float hp = x->getBodyModule()->getHealth();
				auto it = humanHealth.find(x->getID());
				if (it != humanHealth.end() && hp < it->second)
				{
					out.firstHumanHit = (int)frame;
				}
				humanHealth[x->getID()] = hp;
			}
		}
		if (logEvery > 0 && (i % logEvery) == 0)
		{
			out.log += "frame " + std::to_string(frame) + "\n";
			for (int k = 1; k < 4; ++k)
			{
				const AISkirmishPlayer *ai = logic.skirmishAI().findAI(players[k]->getPlayerIndex());
				int structures = 0, units = 0;
				for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
				{
					if (x->getControllingPlayer() == players[k] && !x->isEffectivelyDead())
					{
						structures += x->isKindOfName("STRUCTURE") ? 1 : 0;
						units += !x->isKindOfName("STRUCTURE") && x->isKindOfName("CAN_ATTACK") ? 1 : 0;
					}
				}
				out.log += "  P" + std::to_string(k) + " " + players[k]->getPlayerTemplate()->getName() + " structures " + std::to_string(structures) + " units " +
					std::to_string(units) + (ai ? " enemy " + std::to_string(ai->brain.currentEnemy) + " attacks " + std::to_string(ai->brain.attacksLaunched) + " ended " +
							std::to_string(ai->brain.tacticsEnded) + " retreats " + std::to_string(ai->brain.retreats) + " selfrel " + std::to_string((int)players[k]->getRelationship(players[k])) : std::string());
				if (ai)
				{
					out.log += " gate " + std::to_string(ai->brain.gateRefusals) + " threatgate " + std::to_string(ai->brain.gateDraws) + "/" +
						std::to_string(ai->brain.gateRejects) + " idlearmy " + std::to_string((int)ai->brain.idleArmy.v[0]) + " best " + std::to_string(ai->brain.bestTarget);
					for (const AITarget &t : ai->brain.targets)
					{
						out.log += " thr " + std::to_string((int)t.threat);
					}
					for (const AITarget &t : ai->brain.targets)
					{
						const Object *x = logic.findObjectByID(t.object);
						out.log += " <T" + std::to_string(t.type) + " " + std::to_string(t.teams) + "/" + std::to_string(t.maxTeams) + (t.inactive ? " I" : "") +
							(t.dropped ? " D" : "") + " " + (x ? x->getTemplate()->getName() : std::string("-")) + ">";
					}
					for (const AITactic &t : ai->brain.tactics)
					{
						if (!t.targetless)
						{
							out.log += tacticLine(logic, t);
						}
					}
				}
				out.log += "\n";
			}
		}
		int alive[2] = { 0, 0 };
		for (int k = 0; k < 4; ++k)
		{
			alive[k < 2 ? 0 : 1] += players[k]->isDefeated() ? 0 : 1;
		}
		if (alive[0] == 0 || alive[1] == 0)
		{
			break;
		}
	}
	out.log += "end frame " + std::to_string(logic.getFrame()) + "\n";
}

std::vector<std::string> u22Factions(const char *env, std::uint32_t &seed, int &frames)
{
	std::vector<std::string> f = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	if (!env)
	{
		return f;
	}
	std::vector<std::string> parts;
	std::stringstream ss(env);
	std::string part;
	while (std::getline(ss, part, ','))
	{
		parts.push_back(part);
	}
	if (parts.size() >= 6)
	{
		f = { parts[0], parts[1], parts[2], parts[3] };
		seed = (std::uint32_t)std::stoul(parts[4]);
		frames = std::stoi(parts[5]);
	}
	return f;
}
} // namespace

TEST_CASE("skirmish ai U22: diagnostic Udun 2v2 with an idle human (OPENBFME_AI3_U22=<f0>,<f1>,<f2>,<f3>,<seed>,<frames>)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	std::uint32_t seed = 1;
	int frames = 9000;
	const std::vector<std::string> f = u22Factions(std::getenv("OPENBFME_AI3_U22"), seed, frames);
	U22Run run;
	runUdun(*s, udunMessage(*s, f, 2, seed), frames, run, 150);
	std::ostringstream os;
	for (int k = 0; k < 4; ++k)
	{
		os << "P" << k << " defeated " << run.defeatFrame[k] << " attacks " << run.attacks[k] << " ended " << run.ended[k] << "\n";
	}
	MESSAGE(run.log << os.str() << "first hit on the human " << run.firstHumanHit << ", longest tactic " << run.longestTactic << " (" << run.longestTacticKind << ")");
}

namespace
{
// Evendim with two idle humans (Men at start 0, Mordor at start 1): a world to place objects in by hand
struct U22HandGame
{
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *men = nullptr, *mordor = nullptr;
};

void loadU22HandGame(starttest::Shared &s, U22HandGame &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 2u;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = u22FactionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	static NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	g.men = const_cast<Player *>(u22PlayerAtStart(g.game->logic(), 0));
	g.mordor = const_cast<Player *>(u22PlayerAtStart(g.game->logic(), 1));
	REQUIRE(g.men != nullptr);
	REQUIRE(g.mordor != nullptr);
}

Object *u22PlaceAt(GameLogic &logic, const char *name, Player *p, float x, float y)
{
	Object *o = logic.newObject(logic.things().findTemplate(name), p->getDefaultTeam(), {});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D pos{ x, y, logic.getGroundHeight(x, y) };
	o->setPosition(&pos);
	return o;
}
} // namespace

TEST_CASE("skirmish ai threat finder: an object's threat value and the totals by relationship (RW 0x68F0EC / 0x7EE057 / 0x7EE166 / 0x6C650B)")
{
	OPENBFME_REQUIRE_START(s);
	U22HandGame g;
	loadU22HandGame(*s, g);
	GameLogic &logic = g.game->logic();
	for (int i = 0; i < 2; ++i)
	{
		g.game->advance(0.2);
	}
	const float x = 1600.0f, y = 600.0f;
	const Coord3D at{ x, y, logic.getGroundHeight(x, y) };
	// nothing of either side stands there before
	CHECK(AIThreatFinder::threatTotal(logic, at, 300.0f, true, *g.men, 0) == 0.0f);
	CHECK(AIThreatFinder::threatTotal(logic, at, 300.0f, true, *g.men, 1) == 0.0f);
	Object *m1 = u22PlaceAt(logic, "GondorFighterHorde", g.men, x, y);
	Object *o1 = u22PlaceAt(logic, "MordorFighterHorde", g.mordor, x + 20.0f, y);
	Object *o2 = u22PlaceAt(logic, "MordorMountainTroll", g.mordor, x - 20.0f, y);
	for (Object *o : { m1, o1, o2 })
	{
		REQUIRE(o->isKindOfName("CAN_ATTACK"));
		REQUIRE(!o->isKindOfName("STRUCTURE"));
		REQUIRE(o->getBodyModule() != nullptr);
	}
	g.game->advance(0.2); // the partition takes the new positions; the hordes make their members (not CAN_ATTACK / HERO / SUPPORT: they do not count)
	// a placed object paid nothing: the template's ThreatLevel * (1 * 0.2 + 0.8) * the health ratio
	const FieldValue *level = m1->getTemplate()->getFinalOverride()->findField("ThreatLevel");
	REQUIRE(level != nullptr);
	REQUIRE(std::get_if<float>(level) != nullptr);
	CHECK(*std::get_if<float>(level) > 0.0f);
	CHECK(AIThreatFinder::threatValue(*m1) == *std::get_if<float>(level));
	m1->setBuildCostPaid(300.0f);
	o1->setBuildCostPaid(250.0f);
	o2->setBuildCostPaid(200.0f);
	CHECK(AIThreatFinder::threatValue(*m1) == 300.0f);
	o2->getBodyModule()->setInitialHealth(50);
	const float ratio = o2->getBodyModule()->getHealth() / o2->getBodyModule()->getMaxHealth();
	CHECK(ratio < 1.0f);
	CHECK(AIThreatFinder::threatValue(*o2) == SimMath::fstpDword(SimMath::pc24MulW((double)ratio, 200.0)));
	// Men's view: the enemies are Mordor's two, the allies Men's own one (a player is its own ally)
	REQUIRE(g.men->getRelationship(g.men) == ALLIES);
	const float enemies = AIThreatFinder::threatTotal(logic, at, 300.0f, true, *g.men, 0);
	const float allies = AIThreatFinder::threatTotal(logic, at, 300.0f, true, *g.men, 1);
	CHECK(enemies == SimMath::addf32(AIThreatFinder::threatValue(*o1), AIThreatFinder::threatValue(*o2)));
	CHECK(allies == 300.0f);
	CHECK(AIThreatFinder::relativeThreat(logic, at, *g.men) == SimMath::subf32(allies, enemies));
	CHECK(AIThreatFinder::relativeThreat(logic, at, *g.men) < 0.0f);
	// Mordor's view: the relative threat is the other way round
	CHECK(AIThreatFinder::relativeThreat(logic, at, *g.mordor) == SimMath::subf32(enemies, allies));
	// outside the radius (2D centre distance) nothing counts; without own units the relative threat is 0, never negative (RW 0x6C6606)
	const Coord3D far{ x + 2000.0f, y, at.z };
	CHECK(AIThreatFinder::threatTotal(logic, far, 300.0f, true, *g.men, 0) == 0.0f);
	Object *lone = u22PlaceAt(logic, "MordorFighterHorde", g.mordor, far.x, far.y);
	lone->setBuildCostPaid(200.0f);
	g.game->advance(0.2);
	CHECK(AIThreatFinder::threatTotal(logic, far, 300.0f, true, *g.men, 0) == 200.0f);
	CHECK(AIThreatFinder::relativeThreat(logic, far, *g.men) == 0.0f);
	// the ThreatBreakdown category (template + 0x530, the block's AIKindOf) and the constructor's -1 without one: RW 0x7EDB72 then adds the value to the total twice
	CHECK(AIThreatFinder::threatCategory(*m1->getTemplate()) == 0);  // INFANTRY
	CHECK(AIThreatFinder::threatCategory(*o2->getTemplate()) == 7);  // SIEGEWEAPON
	const Coord3D hero{ x, y + 1500.0f, at.z };
	Object *boromir = u22PlaceAt(logic, "GondorBoromir", g.men, hero.x, hero.y);
	REQUIRE(boromir->isKindOfName("HERO"));
	CHECK(AIThreatFinder::threatCategory(*boromir->getTemplate()) == -1);
	boromir->setBuildCostPaid(500.0f);
	g.game->advance(0.2);
	CHECK(AIThreatFinder::threatValue(*boromir) == 500.0f);
	CHECK(AIThreatFinder::threatTotal(logic, hero, 300.0f, true, *g.mordor, 0) == 1000.0f);
}

TEST_CASE("skirmish ai threat finder: the counter table (RW 0x7EDC2F / 0x7ED963): pikemen count a quarter against cavalry, and cavalry half again against infantry")
{
	OPENBFME_REQUIRE_START(s);
	U22HandGame g;
	loadU22HandGame(*s, g);
	GameLogic &logic = g.game->logic();
	g.game->advance(0.2);
	const float x = 1600.0f, y = 600.0f;
	const Coord3D at{ x, y, logic.getGroundHeight(x, y) };
	// Men's knights (CAVALRY, category 3) against Mordor's fighters (INFANTRY, category 0): an enemy INFANTRY value meets allied CAVALRY (RW 0x7EDA94: the
	// allies' cavalry slot loses value * 0.25 and the value counts * 0.25)
	Object *knights = u22PlaceAt(logic, "GondorKnightHorde", g.men, x, y);
	Object *orcs = u22PlaceAt(logic, "MordorFighterHorde", g.mordor, x + 30.0f, y);
	REQUIRE(AIThreatFinder::threatCategory(*knights->getTemplate()) == 3);
	REQUIRE(AIThreatFinder::threatCategory(*orcs->getTemplate()) == 0);
	knights->setBuildCostPaid(400.0f);
	orcs->setBuildCostPaid(800.0f);
	g.game->advance(0.2);
	float allies = -1.0f, counters = -1.0f;
	const double r = AIThreatFinder::counteredThreat(logic, at, *g.men, allies, counters);
	CHECK(allies == 400.0f);
	CHECK(counters == 200.0f); // 800 * 0.25
	CHECK(r == 200.0);
	// Mordor's view: its infantry against Men's cavalry: CAVALRY (3) has no PIKEMAN (2) to meet, then INFANTRY (0) is there: 400 * 1.5 (RW 0x7EDA34)
	allies = counters = -1.0f;
	const double r2 = AIThreatFinder::counteredThreat(logic, at, *g.mordor, allies, counters);
	CHECK(allies == 800.0f);
	CHECK(counters == 600.0f);
	CHECK(r2 == 200.0);
	// without allies the result is 0 and the outputs are untouched (RW 0x6C6737)
	const Coord3D empty{ x + 2000.0f, y + 2000.0f, at.z };
	allies = counters = -1.0f;
	CHECK(AIThreatFinder::counteredThreat(logic, empty, *g.men, allies, counters) == 0.0);
	CHECK(allies == -1.0f);
	CHECK(counters == -1.0f);
}

TEST_CASE("skirmish ai threat finder: the tactic idle check's bit 0x70 at object + 0x10C is the model condition CAPTURING (RW 0x8F13FC)")
{
	CHECK(CombatNames::modelCondition("CAPTURING") == 0x70);
}

TEST_CASE("skirmish ai U22: Hard 2v2 on Tournament Udun with an idle human: the AIs keep attacking, games end, the idle human is attacked (long)" * doctest::skip())
{
	// QA-1 U22: before lane AI-3 nobody was defeated in 30 minutes (a defeated AI's tactics never ran again, so its partner's shared target kept its team count
	// and the partner never attacked again; no retreat, no team group step, no merge, no threat gate). Outcome expectations measured on this engine (not retail
	// values): in each game every computer player attacks, and one still alive at the end attacked again after its first; at least two of the three games end with a side out within 30 game
	// minutes, and in those the idle human is attacked. The idle human is attacked only once its side's computer ally is out: both enemies chose the ally as
	// their enemy (RW 0x9B304A: the nearest base, or the second within 1000 at 50%; the choice changes only when that enemy is defeated).
	OPENBFME_REQUIRE_START(s);
	struct Config
	{
		std::vector<std::string> factions;
		std::uint32_t seed;
	};
	const Config configs[] = { { { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" }, 1u },
		{ { "FactionMordor", "FactionMen", "FactionDwarves", "FactionAngmar" }, 2u }, { { "FactionMordor", "FactionWild", "FactionIsengard", "FactionElves" }, 3u } };
	int ended = 0, humanAttackedWhenEnded = 0;
	std::ostringstream os;
	for (const Config &c : configs)
	{
		U22Run run;
		runUdun(*s, udunMessage(*s, c.factions, 2, c.seed), 9000, run, 0);
		os << "seed " << c.seed << ":";
		for (int k = 0; k < 4; ++k)
		{
			os << " P" << k << " " << c.factions[(size_t)k] << " defeated " << run.defeatFrame[k] << " attacks " << run.attacks[k] << ";";
		}
		os << " first hit on the human " << run.firstHumanHit << ", longest tactic " << run.longestTactic << " (" << run.longestTacticKind << ")\n";
		const bool sideOut = (run.defeatFrame[0] >= 0 && run.defeatFrame[1] >= 0) || (run.defeatFrame[2] >= 0 && run.defeatFrame[3] >= 0);
		ended += sideOut ? 1 : 0;
		humanAttackedWhenEnded += sideOut && run.firstHumanHit >= 0 ? 1 : 0;
		for (int k = 1; k < 4; ++k)
		{
			// every AI attacks; one that is still alive at the end attacked again after its first
			CHECK_MESSAGE(run.attacks[k] >= (run.defeatFrame[k] < 0 ? 2u : 1u), "seed " << c.seed << " P" << k);
		}
	}
	MESSAGE(os.str());
	CHECK(ended >= 2);
	CHECK(humanAttackedWhenEnded == ended);
}

TEST_CASE("skirmish ai U22: AI-vs-AI pairings of all 7 factions on Evendim, Medium, 30 game-minutes (report; long)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	const char *factions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
	std::ostringstream os;
	int ended = 0;
	for (int i = 0; i < 7; ++i)
	{
		const char *a = factions[i], *b = factions[(i + 1) % 7];
		NewGameMessage m;
		m.game.mapName = "maps/map mp evendim/map mp evendim.map";
		m.game.seed = 2u;
		m.game.startingCash = 1500;
		const char *f[2] = { a, b };
		for (int k = 0; k < 2; ++k)
		{
			SkirmishGameSlot &slot = m.game.slots[k];
			slot.state = (SlotState)(SLOT_EASY_AI + 1);
			slot.name = k == 0 ? u"Player0" : u"Player1";
			slot.playerTemplate = u22FactionIndex(s->world->playerTemplates(), f[k]);
			slot.startPos = k;
			slot.color = k;
			slot.teamNumber = k;
		}
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		ArchiveW3DFileSource source(*s->mount->fs);
		WW3DAssetManager assets(source);
		LiveGame game(*s->world, *s->mount->fs, assets, s->options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game.load(o, &error), error);
		GameLogic &logic = game.logic();
		const Player *p[2] = { u22PlayerAtStart(logic, 0), u22PlayerAtStart(logic, 1) };
		REQUIRE(p[0] != nullptr);
		REQUIRE(p[1] != nullptr);
		int loser = -1, frame = -1;
		for (int f2 = 0; f2 < 9000 && loser < 0; ++f2)
		{
			game.advance(0.2);
			for (int k = 0; k < 2; ++k)
			{
				if (loser < 0 && p[k]->isDefeated())
				{
					loser = k;
					frame = (int)logic.getFrame();
				}
			}
		}
		unsigned long long attacks[2] = {}, retreats[2] = {}, merges[2] = {};
		for (int k = 0; k < 2; ++k)
		{
			if (const AISkirmishPlayer *ai = logic.skirmishAI().findAI(p[k]->getPlayerIndex()))
			{
				attacks[k] = ai->brain.attacksLaunched;
				retreats[k] = ai->brain.retreats;
				merges[k] = ai->brain.merges;
			}
		}
		ended += loser >= 0 ? 1 : 0;
		os << a << " vs " << b << ": " << (loser < 0 ? std::string("open at 9000") : std::string(f[loser]) + " defeated at " + std::to_string(frame)) << " (attacks "
		   << attacks[0] << " / " << attacks[1] << ", retreats " << retreats[0] << " / " << retreats[1] << ", merges " << merges[0] << " / " << merges[1] << ")\n";
	}
	MESSAGE(os.str() << ended << " of 7 ended");
	CHECK(ended >= 1);
}

TEST_CASE("skirmish ai attacked-at records: a fight within 600 of a record replaces it keeping its id, at most 20 are kept (RW 0x6C720A / 0x6C6F01)")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	// an idle human and a computer on Evendim: the computer's brain holds the list
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 2u;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : (SlotState)(SLOT_EASY_AI + 1);
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = u22FactionIndex(s->world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	game.advance(0.2);
	const Player *me = u22PlayerAtStart(logic, 1);
	REQUIRE(me != nullptr);
	AISkirmishPlayer *ai = const_cast<AISkirmishPlayer *>(logic.skirmishAI().findAI(me->getPlayerIndex()));
	REQUIRE(ai != nullptr);
	ai->brain.attackedAt.clear();
	const std::uint32_t id0 = logic.skirmishAI().attackedAtIds();
	AITacticalAI::addAttackedAtForTest(logic, *ai, Coord3D{ 1000.0f, 1000.0f, 0.0f });
	REQUIRE(ai->brain.attackedAt.size() == 1);
	CHECK(ai->brain.attackedAt[0].id == id0 + 1);
	CHECK(ai->brain.attackedAt[0].radius == 300.0f);
	// 599 away (599^2 < 360000): the old record goes, a new one at the new place keeps its id (the running id still advances)
	AITacticalAI::addAttackedAtForTest(logic, *ai, Coord3D{ 1599.0f, 1000.0f, 0.0f });
	REQUIRE(ai->brain.attackedAt.size() == 1);
	CHECK(ai->brain.attackedAt[0].id == id0 + 1);
	CHECK(ai->brain.attackedAt[0].position.x == 1599.0f);
	CHECK(logic.skirmishAI().attackedAtIds() == id0 + 2);
	// 600 away (not below 360000): a second record
	AITacticalAI::addAttackedAtForTest(logic, *ai, Coord3D{ 2199.0f, 1000.0f, 0.0f });
	REQUIRE(ai->brain.attackedAt.size() == 2);
	CHECK(ai->brain.attackedAt[1].id == id0 + 3);
	// at most 20: the 21st drops the first
	for (int i = 0; i < 25; ++i)
	{
		AITacticalAI::addAttackedAtForTest(logic, *ai, Coord3D{ 100.0f + 700.0f * (float)i, 4000.0f, 0.0f });
	}
	CHECK(ai->brain.attackedAt.size() == 20);
	CHECK(ai->brain.attackedAt.back().position.x == 100.0f + 700.0f * 24.0f);
}
