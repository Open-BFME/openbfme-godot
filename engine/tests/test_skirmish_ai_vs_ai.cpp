// OpenBFME unit tests. GPL-3.0.
// Lane AI-2: whole skirmish games between computer players on Evendim (GameLogic/SkirmishAI). The games run the retail data through LiveGame; the
// expectations are outcomes retail's AI reaches (a victory within 30 game-minutes), never this engine's own frame-exact output.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
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

// slot 0 / slot 1 on Evendim's start positions 0 / 1, each a computer of `difficulty` (SLOT_EASY_AI + 0..3) or a human (-1: an idle player)
NewGameMessage versusMessage(const starttest::Shared &s, const std::string &a, int difficultyA, const std::string &b, int difficultyB, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const std::string factions[2] = { a, b };
	const int difficulty[2] = { difficultyA, difficultyB };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = difficulty[i] < 0 ? SLOT_PLAYER : (SlotState)(SLOT_EASY_AI + difficulty[i]);
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	return m;
}

struct VersusRun
{
	int defeatFrame = -1;
	int loser = -1; // start position of the defeated player
	std::vector<std::uint32_t> hashes;
	std::string log;
	unsigned long long attacks[2] = { 0, 0 };
	unsigned long long dozersQueued[2] = { 0, 0 };
	float worstSpread = 0.0f;      // the largest 2D distance of a living member from its horde at the end (both players)
	std::string worstSpreadHorde;
};

const Player *playerAtStart(GameLogic &logic, int start)
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

std::string summary(GameLogic &logic, const Player *p)
{
	const AISkirmishPlayer *ai = logic.skirmishAI().findAI(p->getPlayerIndex());
	int units = 0, structures = 0, dozers = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != p || o->isEffectivelyDead())
		{
			continue;
		}
		structures += o->isKindOfName("STRUCTURE") ? 1 : 0;
		dozers += o->isKindOfName("DOZER") ? 1 : 0;
		units += !o->isKindOfName("STRUCTURE") && o->isKindOfName("CAN_ATTACK") ? 1 : 0;
	}
	std::ostringstream os;
	os << p->getPlayerTemplate()->getName() << ": money " << p->getMoney()->countMoney() << " structures " << structures << " units " << units << " dozers " << dozers;
	if (ai)
	{
		os << " phase " << ai->build.phase << " pending " << ai->build.pending.size() << " inProgress " << ai->build.inProgress.size() << " free " << ai->build.freeDozers.size()
		   << " tactics " << ai->brain.tactics.size() << " started " << ai->brain.tacticsStarted << " attacks " << ai->brain.attacksLaunched << " ended " << ai->brain.tacticsEnded
		   << " dozersQueued " << ai->build.dozerManager.queued;
		os << " cp " << p->commandPoints().getUsage() << "/" << p->commandPointLimit();
		const AISkirmishPlayer *owner = ai->brain.chooser ? ai : ai->groupFirst;
		for (const AITarget &t : owner ? owner->brain.targets : ai->brain.targets)
		{
			const Object *x = logic.findObjectByID(t.object);
			os << " <T" << t.type << " " << t.teams << "/" << t.maxTeams << (t.inactive ? " I" : "") << (t.dropped ? " D" : "") << " " << (x ? x->getTemplate()->getName() : "-") << ">";
		}
		for (const AITactic &t : ai->brain.tactics)
		{
			os << " [" << t.kind << (t.started ? " S" : "") << " teams";
			for (const AITacticTeam &team : t.teams)
			{
				os << " " << team.members.size() << "/" << team.minimum << " idle " << team.idleFrames;
				for (ObjectID id : team.members)
				{
					if (const Object *m = logic.findObjectByID(id))
					{
						AIUpdateInterface *u = const_cast<Object *>(m)->getAIUpdateInterface();
						const Object *v = u ? logic.findObjectByID(u->currentVictimId()) : nullptr;
						os << " (" << m->getTemplate()->getName() << " " << (int)m->getPosition()->x << "," << (int)m->getPosition()->y << " hp "
						   << (m->getBodyModule() ? (int)m->getBodyModule()->getHealth() : -1) << " victim " << (v ? v->getTemplate()->getName() : "-") << " "
						   << (v && v->getBodyModule() ? (int)v->getBodyModule()->getHealth() : -1) << " state " << (u ? (int)u->stateMachine().currentStateId() : -1);
						if (const ContainModuleInterface *c = m->getContain())
						{
							int hp = 0, fighting = 0, n = 0, spread = 0;
							std::string straylog;
							if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
							{
								for (const Object *x : *items)
								{
									++n;
									hp += x->getBodyModule() ? (int)x->getBodyModule()->getHealth() : 0;
									AIUpdateInterface *xu = const_cast<Object *>(x)->getAIUpdateInterface();
									fighting += xu && xu->currentVictimId() != INVALID_ID ? 1 : 0;
									const float sx = x->getPosition()->x - m->getPosition()->x, sy = x->getPosition()->y - m->getPosition()->y;
									spread = std::max(spread, (int)std::sqrt(sx * sx + sy * sy));
									if (std::sqrt(sx * sx + sy * sy) > 300.0f && straylog.size() < 300)
									{
										const Object *xv = xu ? logic.findObjectByID(xu->currentVictimId()) : nullptr;
										straylog += " {stray " + std::to_string((int)x->getPosition()->x) + "," + std::to_string((int)x->getPosition()->y) + " st " +
											std::to_string(xu ? (int)xu->stateMachine().currentStateId() : -1) + " v " + (xv ? xv->getTemplate()->getName() : std::string("-")) +
											(xv ? " @" + std::to_string((int)xv->getPosition()->x) + "," + std::to_string((int)xv->getPosition()->y) : std::string()) + " cmd " +
											std::to_string(xu ? (int)xu->lastCommandSource() : -1) + " goal " + std::to_string(xu ? (int)xu->mover().goalType() : -1) + " @" +
											(xu ? std::to_string((int)xu->mover().goalPosition().x) + "," + std::to_string((int)xu->mover().goalPosition().y) : std::string()) +
											" moving " + std::to_string(xu ? (int)xu->mover().isMoving() : -1) + " path " + std::to_string(xu && xu->mover().path() ? 1 : 0) + [&] {
												std::string gp = " gpath";
												if (xu)
												{
													for (const Coord3D &c : xu->stateMachine().goalPath())
													{
														gp += " " + std::to_string((int)c.x) + "," + std::to_string((int)c.y);
													}
												}
												const Object *pr = logic.findObjectByID(x->getProducerID());
												gp += pr ? " prod " + pr->getTemplate()->getName() + "@" + std::to_string((int)pr->getPosition()->x) + "," + std::to_string((int)pr->getPosition()->y) : std::string(" prod -");
												return gp;
											}() + "}";
									}
								}
							}
							os << " members " << n << " hp " << hp << " fighting " << fighting << " spread " << spread << straylog;
						}
						os << ")";
					}
				}
			}
			os << "]";
		}
	}
	return os.str();
}

void runVersus(starttest::Shared &s, const NewGameMessage &message, int frames, VersusRun &out, bool hashEveryFrame, int logEvery)
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
	const Player *players[2] = { playerAtStart(logic, 0), playerAtStart(logic, 1) };
	REQUIRE(players[0] != nullptr);
	REQUIRE(players[1] != nullptr);
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		if (hashEveryFrame)
		{
			out.hashes.push_back(logic.computeStateHash());
		}
		if (logEvery > 0 && (i % logEvery) == 0)
		{
			out.log += "frame " + std::to_string(logic.getFrame()) + "\n  " + summary(logic, players[0]) + "\n  " + summary(logic, players[1]) + "\n";
		}
		for (int k = 0; k < 2; ++k)
		{
			if (players[k]->isDefeated() && out.defeatFrame < 0)
			{
				out.defeatFrame = (int)logic.getFrame();
				out.loser = k;
			}
		}
		if (out.defeatFrame >= 0)
		{
			break;
		}
	}
	out.log += "end frame " + std::to_string(logic.getFrame()) + "\n  " + summary(logic, players[0]) + "\n  " + summary(logic, players[1]) + "\n";
	for (Object *h = logic.getFirstObject(); h; h = h->getNextObject())
	{
		const ContainModuleInterface *c = h->getContain();
		if (!h->isKindOfName("HORDE") || !c || h->isEffectivelyDead() || (h->getControllingPlayer() != players[0] && h->getControllingPlayer() != players[1]))
		{
			continue;
		}
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			for (const Object *x : *items)
			{
				const float dx = x->getPosition()->x - h->getPosition()->x, dy = x->getPosition()->y - h->getPosition()->y;
				const float d = std::sqrt(dx * dx + dy * dy);
				if (!x->isEffectivelyDead() && d > out.worstSpread)
				{
					out.worstSpread = d;
					out.worstSpreadHorde = h->getTemplate()->getName();
				}
			}
		}
	}
	for (int k = 0; k < 2; ++k)
	{
		std::map<std::string, int> left;
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			if (x->getControllingPlayer() == players[k] && !x->isEffectivelyDead())
			{
				++left[x->getTemplate()->getName() + (x->isKindOfName("STRUCTURE") ? "[S]" : "")];
				if (x->getTemplate()->getName().find("NoSelect") != std::string::npos)
				{
					const Object *pr = logic.findObjectByID(x->getProducerID());
					AIUpdateInterface *xu = x->getAIUpdateInterface();
					out.log += "  worker " + std::to_string(x->getID()) + " @" + std::to_string((int)x->getPosition()->x) + "," + std::to_string((int)x->getPosition()->y) +
						" producer " + (pr ? pr->getTemplate()->getName() + (pr->isEffectivelyDead() ? "(dead)" : "") + " builder " + std::to_string(pr->getBuilderID()) : std::string("gone")) +
						" idle " + std::to_string(xu ? (int)xu->isIdle() : -1) + " created " + std::to_string(x->getCreationFrame()) + "\n";
				}
			}
		}
		out.log += "  left " + std::to_string(k) + ":";
		for (const auto &kv : left)
		{
			out.log += " " + kv.first + "x" + std::to_string(kv.second);
		}
		out.log += "\n";
	}
	// the structures still standing and the enemy horde nearest to each (its AI state, attack-move flag and victim): why a stalled game does not end
	for (int k = 0; logEvery > 0 && k < 2; ++k)
	{
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			if (x->getControllingPlayer() != players[k] || x->isEffectivelyDead() || !x->isKindOfName("STRUCTURE"))
			{
				continue;
			}
			const Object *near = nullptr;
			float best = 0.0f;
			for (Object *h = logic.getFirstObject(); h; h = h->getNextObject())
			{
				if (h->getControllingPlayer() != players[1 - k] || h->isEffectivelyDead() || !h->isKindOfName("HORDE"))
				{
					continue;
				}
				const float dx = h->getPosition()->x - x->getPosition()->x, dy = h->getPosition()->y - x->getPosition()->y;
				if (!near || dx * dx + dy * dy < best)
				{
					near = h;
					best = dx * dx + dy * dy;
				}
			}
			out.log += "  structure " + std::to_string(k) + " " + x->getTemplate()->getName() + " #" + std::to_string(x->getID()) + " @" + std::to_string((int)x->getPosition()->x) + "," +
				std::to_string((int)x->getPosition()->y);
			if (near)
			{
				AIUpdateInterface *nu = const_cast<Object *>(near)->getAIUpdateInterface();
				const Object *v = nu ? nu->currentVictim() : nullptr;
				out.log += " nearest enemy horde " + near->getTemplate()->getName() + " #" + std::to_string(near->getID()) + " at " + std::to_string((int)std::sqrt(best)) + " state " +
					(nu && nu->stateMachine().currentState() ? nu->stateMachine().currentState()->name() : std::string("-")) + " attackMove " + std::to_string(nu ? (int)nu->attackMoveActive() : -1) +
					" victim " + (v ? v->getTemplate()->getName() : std::string("-"));
				if (ObjectWeapons *w = const_cast<Object *>(near)->getWeapons())
				{
					Object *amt = nu ? nu->attackMoveTarget() : nullptr;
					float vr = -1.0f;
					if (const FieldValue *fv = near->getTemplate()->getFinalOverride()->findField("VisionRange"))
					{
						if (const float *f = std::get_if<float>(fv))
						{
							vr = *f;
						}
					}
					out.log += " vision " + std::to_string((int)vr) + " edge " + std::to_string((int)(std::sqrt(best) - CombatQueries::boundingCircleRadius(*x))) + " rel " + std::to_string((int)near->getRelationship(*x));
					out.log += " attackable " + std::to_string((int)CombatQueries::isAttackable(*x));
					out.log += " [construction " + std::to_string((int)x->getConstructionPercent()) + " underCons " + std::to_string((int)x->testStatus((unsigned)CombatNames::statuses().underConstruction)) +
						" unattStatus " + std::to_string((int)x->testStatus((unsigned)CombatNames::statuses().unattackable)) + " unattKind " + std::to_string((int)x->isKindOf((unsigned)CombatNames::kinds().unattackable)) +
						" inert " + std::to_string((int)x->isKindOf((unsigned)CombatNames::kinds().inert)) + " builder " + std::to_string(x->getBuilderID()) + "]";
					{
						std::vector<Object *> nearby;
						logic.combat().targets().collectInRadius(*near->getPosition(), 255.0f, nearby);
						out.log += " collected " + std::to_string((int)(std::find(nearby.begin(), nearby.end(), x) != nearby.end())) + "/" + std::to_string(nearby.size());
						Object *any = logic.combat().targets().findClosestEnemy(*near, 175.0f, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI);
						out.log += " closestNoHordeFlag " + (any ? any->getTemplate()->getName() : std::string("-"));
					}
					out.log += " canAttack(AI) " + std::to_string((int)w->canAttackObject(*x, CMD_FROM_AI, false)) + " canAttack(player) " +
						std::to_string((int)w->canAttackObject(*x, CMD_FROM_PLAYER, false)) + " attackMoveScan " + (amt ? amt->getTemplate()->getName() : std::string("-"));
				}
				else
				{
					out.log += " no weapons";
				}
			}
			out.log += "\n";
		}
	}
	for (int k = 0; k < 2; ++k)
	{
		if (const AISkirmishPlayer *ai = logic.skirmishAI().findAI(players[k]->getPlayerIndex()))
		{
			out.attacks[k] = ai->brain.attacksLaunched;
			out.dozersQueued[k] = ai->build.dozerManager.queued;
		}
	}
}
} // namespace

TEST_CASE("skirmish ai vs ai: diagnostic run (OPENBFME_AI2_DIAG=<factionA>,<factionB>,<seed>,<frames>)" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const char *env = std::getenv("OPENBFME_AI2_DIAG");
	std::string a = "FactionMordor", b = "FactionMen";
	std::uint32_t seed = 2;
	int frames = 9000;
	if (env)
	{
		std::vector<std::string> parts;
		std::stringstream ss(env);
		std::string part;
		while (std::getline(ss, part, ','))
		{
			parts.push_back(part);
		}
		if (parts.size() >= 4)
		{
			a = parts[0];
			b = parts[1];
			seed = (std::uint32_t)std::stoul(parts[2]);
			frames = std::stoi(parts[3]);
		}
	}
	VersusRun run;
	runVersus(*s, versusMessage(*s, a, 1, b, 1, seed), frames, run, false, 300);
	MESSAGE(run.log << "defeat frame " << run.defeatFrame << " loser " << run.loser);
}

TEST_CASE("skirmish ai dozers: an AI that loses its dozers queues a new one at its fortress (RW 0x9A23A4 / 0x9A221E / 0x9A19E0, lost flag RW 0x9A1BC0)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMordor", "FactionMen", "FactionElves" })
	{
		INFO(std::string(faction));
		static std::vector<MapCacheEntry> cache;
		std::string error;
		if (cache.empty())
		{
			REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
		}
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(versusMessage(*s, "FactionMen", -1, faction, 1, 1234u), s->world->playerTemplates(), s->settings, cache,
							RandomAlgorithm::ZH_CarryChain, start, &error),
			error);
		ArchiveW3DFileSource source(*s->mount->fs);
		WW3DAssetManager assets(source);
		LiveGame game(*s->world, *s->mount->fs, assets, s->options);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game.load(o, &error), error);
		GameLogic &logic = game.logic();
		const Player *me = playerAtStart(logic, 1);
		REQUIRE(me != nullptr);
		const AISkirmishPlayer *ai = logic.skirmishAI().findAI(me->getPlayerIndex());
		REQUIRE(ai != nullptr);
		// RW 0x6A9E48: the starting dozers and the fortress are registered by newGame
		CHECK(ai->build.dozerManager.factories.size() == 1);
		const size_t startDozers = ai->build.freeDozers.size();
		CHECK(startDozers >= 1);
		for (int i = 0; i < 200; ++i)
		{
			game.advance(0.2);
		}
		CHECK_FALSE(ai->build.dozerManager.lostDozer);
		std::vector<Object *> dozers;
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			if (x->getControllingPlayer() == me && x->isKindOfName("DOZER") && !x->isEffectivelyDead())
			{
				dozers.push_back(x);
			}
		}
		REQUIRE(!dozers.empty());
		std::vector<ObjectID> killedIds; // lane BUILD-3: ids, not pointers (a killed dozer's memory can hold the new dozer)
		for (Object *d : dozers)
		{
			killedIds.push_back(d->getID());
			d->kill(0);
		}
		// the dozer must be affordable (an AI without dozers or money stays stuck in RW too): the test pays for it
		const_cast<Player *>(me)->getMoney()->deposit(5000, false);
		int newDozerFrame = -1;
		for (int i = 0; i < 1200 && newDozerFrame < 0; ++i)
		{
			game.advance(0.2);
			for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
			{
				// lane BUILD-3: the AI's own dozer template (a site its killed dozer left unfinished now rises from 1 hit point, RW 0x88D44F, so its GettingBuiltBehavior
				// sends a worker, also a DOZER, RW 0x857A04)
				if (x->getControllingPlayer() == me && x->isKindOfName("DOZER") && !x->isEffectivelyDead() && std::find(killedIds.begin(), killedIds.end(), x->getID()) == killedIds.end() &&
					x->getTemplate()->getName() == ai->dozerTemplate)
				{
					newDozerFrame = (int)logic.getFrame();
				}
			}
		}
		MESSAGE(faction << ": start dozers " << startDozers << ", killed " << dozers.size() << ", lost flag " << ai->build.dozerManager.lostDozer << ", queued "
						<< ai->build.dozerManager.queued << " (refused " << ai->build.dozerManager.refused << ")" << ", requeued requests " << ai->build.requeuedForDozerDeath << ", new dozer at frame " << newDozerFrame);
		CHECK(ai->build.dozerManager.lostDozer);
		CHECK(ai->build.dozerManager.queued >= 1);
		CHECK(newDozerFrame > 0);
	}
}

TEST_CASE("skirmish ai vs ai: the lane AI-2 state joins the hash (each mutation moves it)")
{
	AIBrainState b;
	AITactic tac;
	tac.kind = "SimpleAttack";
	tac.teams.push_back(AITacticTeam{});
	b.tactics.push_back(tac);
	AIBuildState s;
	auto request = std::make_shared<AIBuildRequest>();
	s.pending.push_back(request);
	auto hash = [&] {
		StateHasher h;
		AITacticalAI::crc(b, h);
		AIBaseBuilder::crc(s, h);
		AIBaseBuilder::crcRequest(*request, h);
		return h.value();
	};
	auto moved = [&](auto mutate) {
		const std::uint32_t h0 = hash();
		mutate();
		return hash() != h0;
	};
	CHECK(moved([&] { b.tactics[0].teams[0].idleFrames = 4; }));
	CHECK(moved([&] { b.tactics[0].teams[0].lastPosition.x = 3.0f; }));
	CHECK(moved([&] { b.tactics[0].position.y = 2.0f; }));
	CHECK(moved([&] { b.tactics[0].origin.z = 1.0f; }));
	CHECK(moved([&] { b.tactics[0].retreated = true; }));
	CHECK(moved([&] { s.dozerManager.factories.push_back(5); }));
	CHECK(moved([&] { s.dozerManager.lostDozer = true; }));
	CHECK(moved([&] { s.dozerManager.queued += 1; }));
	CHECK(moved([&] { s.dozerManager.refused += 1; }));
	CHECK(moved([&] { s.rebuilds += 1; }));
	CHECK(moved([&] { s.requeuedForDozerDeath += 1; }));
	CHECK(moved([&] { request->requeueOnProducerDeath = false; }));
	CHECK(moved([&] { request->reserveMoney = true; }));
	CHECK(moved([&] { b.generatorMade = true; }));
	CHECK(moved([&] { b.farmKillEarly = true; }));
	CHECK(moved([&] { b.namedCounters["FarmKillSquad::IsRunning"] = 1; }));
	CHECK(moved([&] { b.namedCounters["FarmKillSquad::IsRunning"] = 0; }));
	CHECK(moved([&] { b.targetlessPicks += 1; }));
	CHECK(moved([&] { b.targetlessStarted += 1; }));
	CHECK(moved([&] { b.targetlessUnported += 1; }));
	CHECK(moved([&] { b.tactics[0].targetless = true; }));
	CHECK(moved([&] { b.tactics[0].squadTarget = 9; }));
}

TEST_CASE("skirmish ai vs ai: two runs of Medium Mordor against Medium Men agree on every frame hash (1500 frames)")
{
	OPENBFME_REQUIRE_START(s);
	VersusRun a, b;
	runVersus(*s, versusMessage(*s, "FactionMordor", 1, "FactionMen", 1, 2u), 1500, a, true, 0);
	runVersus(*s, versusMessage(*s, "FactionMordor", 1, "FactionMen", 1, 2u), 1500, b, true, 0);
	REQUIRE(a.hashes.size() == b.hashes.size());
	CHECK(a.hashes == b.hashes);
	CHECK(a.attacks[0] + a.attacks[1] >= 1);
}

TEST_CASE("skirmish ai vs ai: Evendim, 30 game-minutes: every pairing ends - Mordor beats Men (seeds 2 and 1234), Mordor beats Men as the second player (seed 7) (S-420 engine pins)")
{
	// S-420 (lane AI-2 r6/r7): CASTLE-1's fortress fall, a structure's spawned NoSelect worker fading away with its structure (RW 0x85750D -> 0x85730B) and unfinished
	// structures being targets (ZH AI::findClosestEnemy) remove the stalls. History of these pins: r6 ended seed 2 at 6422 and seed 7 at 2468; r7's retail waypoint
	// distance bases (S-895) left seed 2 open; the merge of PHYS-1's per-soldier melee machine and the weapon delivery range gate (archive 0cc8dbcd) changes every
	// fight, and seed 7 is now open as well (both fortresses and armies standing at 9000, fighting in the field), while seed 1234, open before, ends with Men's
	// defeat at 6508. Lane MODULES-2 merge: the emotion trackers (a QuarrelProbability draw per tracker and frame, RW 0x8B5DF3), the group bonus (the first wake's draw
	// RW 0x8937B5 and the Mordor hordes' damage modifier while a horde has its neighbours) and the other new modules change every fight: seed 2 now ends with Men's
	// defeat at 8432, seed 1234 is open at 9000 (with the three new module classes unregistered all three old pins, 6508 included, come back unchanged). Lane MODULES-3: the emotion AI states (Terror / FearIdle / Taunt / HeroCheer move and turn hordes, PreventPlayerCommands and locked states refuse
	// orders), the taunt scan and the crush warning's draws (NotifyTargetsOfImminentProbableCrushingMux.cpp 0x3F): seed 2 ends at 8464 (with the AI hook, the gate, the taunt scan and
	// the crush warning disabled the three MODULES-2 pins come back unchanged). MODULES-3 r2 (Sol's fixes: the temporary state's resume callback RW 0x751D9A / 0x740C97, the
	// tracker stop when a temporary state is cleared RW 0x8B4FA1, the gate before the group move's reservations and the attack-move's arming / continuation, the
	// taunt filter's candidate layer RW 0x8B5228): seed 2 is open at 9000 as well (with those five changes disabled it ends at 8464 again). An open game is not a stall: the diag run shows the armies fighting, not standing next to a structure. Lane END-1 moved VictoryConditions::update to its binary position (phase 5 after the destroy list, RW 0x62EBCE): a loss is seen one frame earlier. Engine pins, not retail values.
	// Lane GARRISON-1: the AI module processes DISABLED_HELD (RW 0x855830: a held unit's AI runs, e.g. a unit held at a production exit), the stealth detector reads
	// the contain's slot 0x10 (a horde member's detector is no longer taken as contained in a garrison) and the range host reads INSIDE_GARRISON / CONTESTING_BUILDING:
	// seed 2 is open at 9000 too (with the HELD processing reverted seed 2 ends at 8432 again). Engine pins, not retail values.
	// GARRISON-1 r2: a held object is not doing ground movement (RW 0x667144 tests object + 0x1C8 bit 3; a unit held at a production exit is one): seed 2 ends
	// with Men's defeat at 7636 (with that one test reverted seed 2 is open at 9000 again; 7635 with END-1's phase-5 victory check, one frame earlier as for 8432 -> 8431);
	// seeds 7 and 1234 stay open.
	// Merge of GARRISON-1 r2 and MODULES-3 r2 (each side's pins: GARRISON-1 seed 2 at 7635, MODULES-3 all open): all three seeds are open at 9000, MODULES-3's pins.
	// The move from GARRISON-1's 7635 is MODULES-3's: on the merged tree, switching GARRISON-1's held ground-movement test (RW 0x667144) off leaves seed 2 open, so it
	// no longer decides the game; MODULES-3's temporary-state rework and emotion states are what changed seed 2 (switching off only its AI-state hook, gate, taunt scan,
	// crush warning, lock / leave hooks and the temporary-state update rework does not bring 7635 back: seed 2 stays open and seed 7 then ends at 7745, so the
	// remaining MODULES-3 code (the machine's goal restore, the safe path, the horde pass) also moves the fights). GARRISON-1's command gate on its enter / exit
	// orders does not run in these games (no AI garrisons).
	// INTEG-2 (STEALTH-2 + PERF-1 archive 4a846214 merged with GARRISON-1 48857a58): re-measured, all three seeds still open at 9000 (no change: STEALTH-2's
	// partition-ordered detector scans and GARRISON-1's world removal do not decide these games).
	OPENBFME_REQUIRE_START(s);
	struct Game
	{
		const char *a, *b;
		std::uint32_t seed;
		int loser, defeatFrame;
	};
	// GARRISON-2 (TransportContain / HordeTransportContain / SiegeEngineContain / HordeSiegeEngineContain / TunnelContain run instead of being unported modules: the
	// Mordor trolls' grab contain, the mumak's archers, the siege engines' crews; TurretAI's draws): seed 2 ends with Men's defeat at 4686, seeds 7 and 1234 are
	// open at 9000. With those five classes unregistered (all else of the merged tree kept) the three pins above come back unchanged (open, open, 5186); with only
	// the turrets or only SlowDeathBehavior's disabled-type processing switched off the new pins stay. Engine pins, not retail values.
	// Merge of GARRISON-2 with SMOOTH-3 (re-measured on the merged tree): all three games now end: seed 2 Men defeated at 8697, seed 7 Men (start position 0) defeated
	// at 4773, seed 1234 Men defeated at 6284. Engine pins, not retail values.
	// Merge of HERO-2 (DualWeaponBehavior, RW 0x85DF88: the archers' and heroes' close range weapon switch, 59 templates) with SCRIPT-2's tree (re-measured on the
	// merged tree): seed 2 ends with Men's defeat at 7054, seed 7 is open at 9000, seed 1234 ends with Mordor's defeat (position 0) at 8704. With only
	// DualWeaponBehavior unregistered SCRIPT-2's pins below come back unchanged (6171, open, open). Engine pins, not retail values.
	// Lane AI-3 (QA-1 U22): the threat finder's retreat (RW 0x8F2784 / 0x8F243A), each team's AIGroup step (RW 0x774F2B's idle helpers), the CAPTURING test of the
	// idle counter and the failed end's threat move (RW 0x6C644C) change the fights: seed 2 ends with Men's defeat at 5771, seeds 7 and 1234 stay open (with those
	// four switched off the pins 6171 / open / open come back; the retreat alone or the group step alone each move all three games). Engine pins, not retail values.
	// AI-3 r2: the end's merge into a nearby team (RW 0x8F1D88 with its draw AITactic.cpp 0x3FC) and the ThreatBreakdown categories (an object without one counts
	// twice in the finder's total, RW 0x7EDB72; a template without ThreatLevel has the constructor's 1.0) leave seed 2 open at 9000 as well (with both switched
	// off 5771 comes back; either alone leaves it open). AI-3 r3 (the best-target threat gate RW 0x90B2A0 with the attacked-at records RW 0x6C7344 and the idle
	// army record RW 0x99E63B): all three stay open at 9000. Engine pins, not retail values.
	// Lane BUILD-3 (RotWK's dozer machines: dock positions at the contact points or FindPositionAround with its random start angle, porters inside the structures
	// they build, sites rising from 1 hit point, the one-node path that now arrives): seed 2 ends with Men's defeat at 8857, seed 7 stays open, seed 1234 ends with
	// Mordor's defeat at 7633 (on SCRIPT-2's merge: 6171, open, open). Engine pins, not retail values.
	// Merge of AI-3 with BUILD-3 (re-measured on the merged tree): seed 2 ends with Men's defeat at 7679, seeds 7 and 1234 stay open. Engine pins.
	// Merge of HERO-2 (DualWeaponBehavior, ShareExperienceBehavior and the other hero modules change the fights) with AI-3 / BUILD-3, re-measured on the merged
	// tree: seed 7 ends with Mordor's defeat (start position 1) at 6309, seeds 2 and 1234 stay open. Engine pins.
	// Merge COMBAT-3 (+ HUD-4, RENDER-4) on MOD-4 / AUDIO-4 / ANIM-1 (all change the simulation on purpose; COMBAT-3: crush knockback, slow-down, pike formation
	// modifiers): re-measured on the merged tree: seed 2 ends with Mordor's defeat (start position 0) at 7706, seed 7 with Mordor's defeat (start position 1) at
	// 6119, seed 1234 with Men's defeat (start position 1) at 7147. Engine pins, not retail values.
	// Lane CAMP-1H: the computer players' objects receive retail's AI difficulty upgrade (Object::initObject RW 0x693D63 -> Player::
	// applyDifficultyBonusesForObject RW 0x6AC32D: Upgrade_MediumAIMultiPlayer -> MediumAIMultiPlayer_Bonus, DAMAGE_MULT 100% = 1.0) and the teams update their
	// state (Team::updateState RW 0x7A208C): the three pins are unchanged. (Measured on the way: while ModifierList read a percent macro as 100 instead of
	// 1.0, RW 0x8062A8 expands before looking for '%', these games ended at open / 2153 / 1913.) Engine pins.
	// Merge EXIT-1 + MOVE-2 (+ PERF-3, QA2-FIX): the horde members' busy rule and steps (EXIT-1), the melee member pass, the jitter fixes and the DamageArc
	// test for radius damage (MOVE-2) change the fights: seed 2 ends with Mordor's defeat at 8421, seed 7 with Mordor's defeat at 6075, seed 1234 with
	// Men's defeat at 7852 (the same losers). Engine pins, not retail values.
	// Lane COMBAT-4 (shockwaves: MetaImpactNugget, RamPower's hit and DamageDealtAtSelfPosition throw units; RotWK's SlowDeathBehavior): re-measured on the lane's
	// tree: seed 2 ends with Men's defeat (start position 1) at 7132, seed 7 is open at 9000, seed 1234 ends with Men's defeat at 5681. Engine pins, not retail values.
	// COMBAT-4 r2 (HitPercentage rolls for a shot with no victim, RW 0x6CCCA7; no retail weapon has both DamageDealtAtSelfPosition and HitPercentage below 100%, and the
	// pins, the PERF-1 vectors and the structure death frames were re-measured unchanged on the lane's tree after it). Engine pins, not retail values.
	// Merge COMBAT-4 (+ r2) on EXIT-1 / MOVE-2 / CAMP-1H: shockwaves throw units, RotWK's slow death and the HitPercentage draw change the fights:
	// seed 2 ends with Mordor's defeat at 8208, seed 7 now with Men's defeat (start position 0) at 6498, seed 1234 with Men's defeat at 7694. Engine pins.
	// Lane DECOMP-1 (DOTNugget, AttributeModifierNugget, RotWK's weapon choice RW 0x6C8A4E and DamageNugget's isApplicable RW 0x90E855, the SHROUD_CLEARING modifier,
	// the melee contact's horde resolution): re-measured on the lane's tree: seeds 2 and 7 unchanged (8208, 6498), seed 1234 ends with Men's defeat earlier, at 6189.
	// Engine pins, not retail values.
	// Lane MOVE-3 (the hordes' own footprint RW 0x6ED071, the horde goal's member reservation RW 0x86EF13, the blocked unit's path patch RW 0x6631BF / 0x6F7938,
	// the group manager's move order RW 0x75748C, the rally point adjustment RW 0x8A4189) change the fights: seed 2 ends with Mordor's defeat at 8741, seeds 7
	// and 1234 are open at 9000. Engine pins, not retail values.
	// Merge COMBAT-4 into MOVE-3 with MOVE-3 r2 (the exit's moveAlliesAwayFromDestination RW 0x6F85A6 and the move-away handler's horde forwarding RW 0x66DADB):
	// seed 2 is open at 9000, seed 7 ends with Mordor's defeat (start position 1) at 6536, seed 1234 is open at 9000. Engine pins, not retail values.
	// MOVE-3 r3 (review fixes: a horde's line test with radius 1 RW 0x6EE12D, the goal slot's own angle RW 0x8E28DB, the patch cost at the candidate cell RW 0x6ED46C,
	// the closest raw segment's NaN rule RW 0x765598, ProductionUpdate's moveAlliesAwayFromDestination RW 0x8A291D): seed 2 is open at 9000, seed 7 ends with
	// Men's defeat (start position 0) at 7427, seed 1234 with Men's defeat (start position 1) at 4851. Engine pins, not retail values.
	// Lane IDLE-1: a contained object's idle mood scan keeps RW 0x66844A's container gate (CanAttackWhileContained, CONTESTING_BUILDING, the own contain's
	// vslot 0xB8, the container not JUST_BUILT): the battering rams' crews no longer leave their bones to fight, so the rams batter on. Re-measured on the lane's
	// tree: seed 2 ends with Mordor's defeat at 6471, seed 7 is open at 9000, seed 1234 ends with Men's defeat at 5531. Engine pins, not retail values.
	// Lane IDLE-1 r2: the AI updates run in updates[0] and HordeContain in updates[1] (RW 0x851E97 / 0x490AC4, the scheduler RW 0x62E982: every member's own update
	// before its horde's member pass), and the hub leaves a member whose physics motion is disabled without an order (RW 0x874724). Re-measured on the lane's
	// tree: seed 2 is open at 9000, seed 7 ends with Mordor's defeat (start position 1) at 6009, seed 1234 is open at 9000. Engine pins, not retail values.
	// Merge of DECOMP-1 r2 (the 12 disabled types and their special power pause, ParalyzeNugget, the full temporary weapon of death weapons and HitStoredTarget
	// warheads, the nugget applicability bodies) with IDLE-1 / PLAY-1 / CAMP-2 (archive 22e29980), re-measured on the merged tree with remote-measure.sh: seeds 2 and
	// 1234 stay open at 9000, seed 7 ends with Mordor's defeat (start position 1) earlier, at 4947. The launched attacks are 2 / 27 / 43 for seeds 2 / 7 / 1234
	// (r3 correction after Sol's replay: it is the open seed 2 game that launches only two, not seed 7; the bound below is two for seed 2, four for the others).
	// Engine pins, not retail values.
	// Merge of IDLE-1 / PLAY-1 / AUDIO-5 / INPUT-1 / WINCRASH-1 / CAMP-2 (22e29980) into MOVE-3 with MOVE-3 r4 (the queue exit's clearing after its exit command,
	// RW 0x8A4214 .. 0x8A424F; unclamped line ends RW 0x6E8CE6), re-measured on JonathanPC: seed 2 is open at 9000, seed 7 is open at 9000, seed 1234 ends with
	// Men's defeat (start position 1) at 8249. Engine pins, not retail values.
	// Merge of merge/play2 (MOVE-3, CAMP-2, INPUT-1, CAH-2, DOCS-1 on 22e29980) into DECOMP-1 r3 (Sol's fixes: the victim gate of a damage nugget, the firing
	// weapon's slot, the turret aim gate of the weapon choice, the SHROUD_CLEARING refresh), re-measured on the merged tree with remote-measure.sh: seed 2 is open at
	// 9000, seed 7 ends with Mordor's defeat (start position 1) at 3869, seed 1234 with Men's defeat (start position 1) at 7528. Engine pins, not retail values.
	for (const Game &g : { Game{ "FactionMordor", "FactionMen", 2u, -1, -1 }, Game{ "FactionMen", "FactionMordor", 7u, 1, 3869 }, Game{ "FactionMordor", "FactionMen", 1234u, 1, 7528 } })
	{
		INFO(g.a << " vs " << g.b << " seed " << g.seed);
		VersusRun run;
		runVersus(*s, versusMessage(*s, g.a, 1, g.b, 1, g.seed), 9000, run, false, 0);
		MESSAGE(run.log << "defeat frame " << run.defeatFrame << " loser " << run.loser);
		CHECK(run.attacks[0] + run.attacks[1] >= (g.seed == 2u ? 2u : 4u)); // DECOMP-1 r3: the seed 2 game launches two (see above)
		// SMOOTH-2 (the pending position cleared every frame, RW 0x62618F, feeds the crush warning's velocity RW 0x68EF58) ends seed 1234 with Men's defeat:
		// 5275 on SMOOTH-2's base, 5186 on the merged tree (with GARRISON-1 / STEALTH-2 / END-2); seeds 2 and 7 stay open. Lane SMOOTH-3 (RotWK's member hub near arm RW 0x874F09, the angle goal RW 0x5E98D6 turning at the locomotor's rate): seed 1234 is open at 9000 as well. Engine pins, not retail values.
		// SCRIPT-2 (merged with AUDIO-3 / GARRISON-2 / SMOOTH-3): the AI libraries' scripts now evaluate the conditions they use (PLAYER_HAS_OBJECT_COMPARISON ...
		// were false before; switching only those back to unported restored the old runs on SCRIPT-2's base): seed 2 ends with Men's defeat at 6171, seeds 7 and
		// 1234 are open at 9000. Engine pins, not retail values.
		CHECK(run.loser == g.loser);
		CHECK(run.defeatFrame == g.defeatFrame);
	}
}

TEST_CASE("skirmish ai dozers: the factory sort is retail's (RW 0x9A21DB: 16 equal keys keep their order, 17 and 32 are reversed; always a descending permutation)")
{
	auto fake = [](size_t i) { return reinterpret_cast<Object *>((std::uintptr_t)(i + 1) * 16u); }; // never dereferenced
	for (size_t n : { (size_t)16, (size_t)17, (size_t)32 })
	{
		std::vector<std::pair<float, Object *>> a;
		for (size_t i = 0; i < n; ++i)
		{
			a.emplace_back(0.0f, fake(i));
		}
		AIBaseBuilder::retailSortDozerFactories(a);
		bool ok = true;
		for (size_t i = 0; i < n; ++i)
		{
			ok = ok && a[i].second == fake(n == 16 ? i : n - 1 - i);
		}
		INFO("n = " << n);
		CHECK(ok);
	}
	std::uint32_t seed = 12345u;
	for (size_t n = 1; n <= 300; ++n)
	{
		std::vector<std::pair<float, Object *>> a;
		for (size_t i = 0; i < n; ++i)
		{
			seed = seed * 1103515245u + 12345u;
			a.emplace_back((float)((seed >> 16) % 101u), fake(i));
		}
		AIBaseBuilder::retailSortDozerFactories(a);
		bool sorted = true;
		std::vector<bool> seen(n, false);
		for (size_t i = 0; i < n; ++i)
		{
			sorted = sorted && (i == 0 || a[i - 1].first >= a[i].first);
			const size_t id = (size_t)(reinterpret_cast<std::uintptr_t>(a[i].second) / 16u) - 1;
			sorted = sorted && id < n && !seen[id];
			if (id < n)
			{
				seen[id] = true;
			}
		}
		INFO("n = " << n);
		CHECK(sorted);
	}
}

TEST_CASE("skirmish ai vs ai: produced members catch up with their horde (RW 0x87468B makes a member walking its own exit path busy)")
{
	// before lane AI-2 r2, Mordor's archers produced after their horde had left stayed at the Orc Pit in AI_FOLLOW_EXITPRODUCTION_PATH (up to 1100 from the horde);
	// engine pin: every living member is within 300 of its horde after 2400 frames of Medium Mordor against Medium Men (seed 2)
	OPENBFME_REQUIRE_START(s);
	VersusRun run;
	runVersus(*s, versusMessage(*s, "FactionMordor", 1, "FactionMen", 1, 2u), 2400, run, false, 0);
	MESSAGE("worst member spread " << run.worstSpread << " (" << run.worstSpreadHorde << ")");
	CHECK(run.worstSpread < 300.0f);
}

TEST_CASE("skirmish ai targetless: the generator's FarmKillSquad draw at creation, the per-frame pick and the squad's counters (RW 0x9BB3CA / 0x90C5E2 / 0x9BB1C4 / 0x9BB479)")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(versusMessage(*s, "FactionMen", -1, "FactionMordor", 1, 1234u), s->world->playerTemplates(), s->settings, cache,
						RandomAlgorithm::ZH_CarryChain, start, &error),
		error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	o.logRandomCalls = true;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	const Player *me = playerAtStart(logic, 1);
	REQUIRE(me != nullptr);
	const AISkirmishPlayer *ai = logic.skirmishAI().findAI(me->getPlayerIndex());
	REQUIRE(ai != nullptr);
	CHECK(ai->brain.generatorMade);
	int firstStart = -1;
	for (int i = 0; i < 2400 && firstStart < 0; ++i)
	{
		game.advance(0.2);
		if (ai->brain.targetlessStarted > 0)
		{
			firstStart = (int)logic.getFrame();
		}
	}
	MESSAGE("FarmKillSquad early flag " << ai->brain.farmKillEarly << ", first targetless start at frame " << firstStart << ", picks " << ai->brain.targetlessPicks
									  << ", unported applicability checks " << ai->brain.targetlessUnported);
	REQUIRE(firstStart > 0);
	// RW 0x9BB1C4: a FarmKillSquad needs the Rush phase over unless its + 0x60 flag (94%) is set; only it is ported, so every pick drew among exactly one candidate
	CHECK(ai->brain.targetlessPicks == ai->brain.targetlessStarted);
	CHECK(ai->brain.tacticsByKind.count("FarmKillSquad") == 1);
	auto running = ai->brain.namedCounters.find("FarmKillSquad::IsRunning");
	REQUIRE(running != ai->brain.namedCounters.end());
	CHECK(running->second == 1);
	CHECK(ai->brain.targetlessUnported >= 10);
	// RW 0x90C0EE constructs the prototype; RW 0x90C5E2 picks it even from a singleton pool,
	// then RW 0x9BB43D constructs the clone before copying its prototype flag, and RW 0x9BB479 sets it up.
	std::vector<GameLogicRandom::Call> draws;
	for (const auto &c : logic.random().callLog())
	{
		if (c.file == "AIFarmKillSquad.cpp" || (c.file == "AITacticsGenerator.cpp" && c.line == 0x1BC))
		{
			draws.push_back(c);
		}
	}
	REQUIRE(draws.size() == 4);
	CHECK(draws[0].line == 0x39);
	CHECK(draws[0].lo == 1);
	CHECK(draws[0].hi == 100);
	CHECK(draws[1].line == 0x1BC);
	CHECK(draws[1].lo == 0);
	CHECK(draws[1].hi == 0);
	CHECK(draws[2].line == 0x39);
	CHECK(draws[2].lo == 1);
	CHECK(draws[2].hi == 100);
	CHECK(draws[3].line == 0x267);
	CHECK(draws[3].lo == (ai->build.phase == 0 ? 1 : 2));
	CHECK(draws[3].hi == 3);
	for (const auto &c : draws)
	{
		CHECK(c.drewNumber);
	}
}

TEST_CASE("skirmish ai tactics: the offensive bodies make retail's draws in order and walk their waypoints (Flank RW 0x9B461D, Pincer RW 0x9B4041, Feint RW 0x9B396A, Formation RW 0x9B4C32)")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	std::map<std::string, int> launched;
	int flankChecked = 0, pincerChecked = 0, formationSteps = 0;
	int flankDistances = 0, pincerDistances = 0;
	// seeds 13 and 17 (lane MODULES-2 merge): the retail draws of the emotion trackers and the group bonus shift every game's random stream, and with them the
	// four original seeds launch no Pincer within 2600 frames; seed 13 launches one Pincer (and a Flank), seed 17 three. Lane BUILD-3 (RotWK's dozer machines: a
	// dock position without a contact point draws its random start angle, RW 0x68592D, and the builds start and end at other frames): none of the six hands a
	// Pincer team its waypoints within 2600 frames any more; seeds 19 and 31 do (1 and 2 distances; 29, 37 and 67 would too)
	// Merge MOD-4 + AUDIO-4 + ANIM-1: the random stream moved again and none of these eight hands a Pincer team its waypoints within 2600 frames; a probe of
	// 18 seeds on the merged tree found seeds 29 and 67 (one Pincer each)
	// Merge of IDLE-1 (update phases) into MOVE-3 r4: none of these ten hands a Pincer team its waypoints within 2600 frames (1234 and 11 start one); a probe of
	// 15 seeds on the merged tree found seeds 37 and 41
	for (std::uint32_t seed : { 1234u, 2u, 7u, 11u, 13u, 17u, 19u, 31u, 29u, 67u, 37u, 41u })
	{
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(versusMessage(*s, "FactionMen", -1, "FactionMordor", 2, seed), s->world->playerTemplates(), s->settings, cache,
							RandomAlgorithm::ZH_CarryChain, start, &error),
			error);
		ArchiveW3DFileSource source(*s->mount->fs);
		WW3DAssetManager assets(source);
		LiveGame game(*s->world, *s->mount->fs, assets, s->options);
		LiveGame::Options o;
		o.start = &start;
		o.logRandomCalls = true;
		REQUIRE_MESSAGE(game.load(o, &error), error);
		GameLogic &logic = game.logic();
		const Player *me = playerAtStart(logic, 1);
		REQUIRE(me != nullptr);
		const AISkirmishPlayer *ai = logic.skirmishAI().findAI(me->getPlayerIndex());
		REQUIRE(ai != nullptr);
		std::vector<float> flankOffsets, pincerOffsets; // per launch: the distance of the offset waypoints from the target (waypoint 3)
		std::vector<Coord3D> seenLaunch;
		auto dist2 = [](const Coord3D &a, const Coord3D &b) { return (float)std::sqrt((double)(a.x - b.x) * (a.x - b.x) + (double)(a.y - b.y) * (a.y - b.y) + (double)(a.z - b.z) * (a.z - b.z)); };
		for (int i = 0; i < 2600; ++i)
		{
			game.advance(0.2);
			for (const AITactic &t : ai->brain.tactics)
			{
				if (!t.started)
				{
					continue;
				}
				if ((t.kind == "FlankAttack" || t.kind == "PincerAttack") && t.waypoints.size() == 4)
				{
					// waypoint 0 is team 0's centre at the launch, the last the target position
					const AITarget *tg = t.targetIndex >= 0 && t.targetIndex < (int)ai->brain.targets.size() ? &ai->brain.targets[(size_t)t.targetIndex] : nullptr;
					if (tg && !tg->inactive)
					{
						CHECK(t.waypoints[3].x == tg->position.x);
						CHECK(t.waypoints[3].y == tg->position.y);
					}
					(t.kind == "FlankAttack" ? flankChecked : pincerChecked) += 1;
					// one record per launch (waypoint 1 identifies it)
					bool seen = false;
					for (const Coord3D &w : seenLaunch)
					{
						seen = seen || (w.x == t.waypoints[1].x && w.y == t.waypoints[1].y);
					}
					if (!seen)
					{
						seenLaunch.push_back(t.waypoints[1]);
						if (t.kind == "FlankAttack")
						{
							// waypoint 2 lies beyond the target by d along the approach (RW 0x9B461D)
							flankOffsets.push_back(dist2(t.waypoints[2], t.waypoints[3]));
						}
						else
						{
							// waypoints 1 and 2 lie d to either side of the target (RW 0x9B4041)
							pincerOffsets.push_back(dist2(t.waypoints[1], t.waypoints[3]));
							CHECK(std::fabs(dist2(t.waypoints[2], t.waypoints[3]) - pincerOffsets.back()) < 0.05f);
						}
					}
				}
				if (t.kind == "FormationAttack")
				{
					formationSteps = std::max(formationSteps, t.step);
				}
			}
		}
		for (const auto &kv : ai->brain.tacticsByKind)
		{
			launched[kv.first] += kv.second;
		}
		// the draws of the bodies, in call order
		const auto &calls = logic.random().callLog();
		// the waypoint distances (Sol review r6): d = ftol(base + draw), the base added by retail's fadd before the ftol: 600 for Flank (RW 0x9B4647), 450 for Pincer
		// (RW 0x9B406C). Each launch's offset is the distance of its draw, in launch order
		std::vector<int> flankD, pincerD;
		for (const auto &c : calls)
		{
			if (c.file == "AIFlankAttackTactic.cpp" && c.line == 0x4E)
			{
				flankD.push_back((int)SimMath::ftol2((double)SimMath::pc24Add(c.rresult, 600.0f)));
			}
			if (c.file == "AIPincerAttackTactic.cpp" && c.line == 0x87)
			{
				pincerD.push_back((int)SimMath::ftol2((double)SimMath::pc24Add(c.rresult, 450.0f)));
			}
		}
		REQUIRE(flankOffsets.size() <= flankD.size());
		for (size_t k = 0; k < flankOffsets.size(); ++k)
		{
			CHECK(flankD[k] >= 600);
			CHECK(flankD[k] <= 1000);
			CHECK(std::fabs(flankOffsets[k] - (float)flankD[k]) < 0.05f);
			++flankDistances;
		}
		REQUIRE(pincerOffsets.size() <= pincerD.size());
		for (size_t k = 0; k < pincerOffsets.size(); ++k)
		{
			CHECK(pincerD[k] >= 450);
			CHECK(pincerD[k] <= 600);
			CHECK(std::fabs(pincerOffsets[k] - (float)pincerD[k]) < 0.05f);
			++pincerDistances;
		}
		for (size_t i = 0; i < calls.size(); ++i)
		{
			const auto &c = calls[i];
			if (c.file == "AIFlankAttackTactic.cpp" && c.line == 0x4E)
			{
				CHECK(c.real);
				CHECK(c.rlo == 0.0f);
				CHECK(c.rhi == 400.0f);
				REQUIRE(i + 1 < calls.size());
				CHECK(calls[i + 1].file == "AIFlankAttackTactic.cpp"); // the side, straight after (RW 0x9B4679)
				CHECK(calls[i + 1].line == 0x5B);
				CHECK(calls[i + 1].lo == 0);
				CHECK(calls[i + 1].hi == 1);
			}
			if (c.file == "AIPincerAttackTactic.cpp")
			{
				CHECK(c.line == 0x87);
				CHECK(c.real);
				CHECK(c.rhi == 150.0f);
			}
			if (c.file == "AIFeintAttackTactic.cpp" && c.line == 0x43)
			{
				CHECK(c.rhi == 200.0f);
				REQUIRE(i + 1 < calls.size());
				size_t j = i + 1;
				if (calls[j].file == "AIFeintAttackTactic.cpp" && calls[j].line == 0x66)
				{
					CHECK(calls[j].rhi == 400.0f); // no enemy fortress: the second offset
					++j;
				}
				if (j < calls.size() && calls[j].file == "AIFeintAttackTactic.cpp")
				{
					CHECK(calls[j].line == 0x7A);
					CHECK(calls[j].hi == 1);
				}
			}
		}
	}
	std::string kinds;
	for (const auto &kv : launched)
	{
		kinds += kv.first + "x" + std::to_string(kv.second) + " ";
	}
	MESSAGE("tactics started: " << kinds << "; flank frames checked " << flankChecked << ", pincer " << pincerChecked << ", formation reached step " << formationSteps
								 << "; waypoint distances pinned: flank " << flankDistances << ", pincer " << pincerDistances);
	CHECK(launched.size() >= 3);
	CHECK(pincerDistances >= 1); // (no FlankAttack team is handed over within 2600 frames in these games: the next test pins its distance directly)
}

namespace
{
// a live two-human game on Evendim (no AI): the S-420 regressions below place objects by hand
struct HandGame
{
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *men = nullptr, *mordor = nullptr;
};

void loadHandGame(starttest::Shared &s, HandGame &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	static NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(versusMessage(s, "FactionMen", -1, "FactionMordor", -1, 2u), s.world->playerTemplates(), s.settings, cache,
						RandomAlgorithm::ZH_CarryChain, start, &error),
		error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	g.men = const_cast<Player *>(playerAtStart(g.game->logic(), 0));
	g.mordor = const_cast<Player *>(playerAtStart(g.game->logic(), 1));
	REQUIRE(g.men != nullptr);
	REQUIRE(g.mordor != nullptr);
}

Object *placeAt(GameLogic &logic, const char *name, Player *p, float x, float y)
{
	Object *o = logic.newObject(logic.things().findTemplate(name), p->getDefaultTeam(), {});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D pos{ x, y, logic.getGroundHeight(x, y) };
	o->setPosition(&pos);
	return o;
}
} // namespace

TEST_CASE("skirmish ai vs ai (S-420): a structure's spawned NoSelect worker fades away when the structure is deleted (RW 0x85750D -> RW 0x85730B)")
{
	OPENBFME_REQUIRE_START(s);
	HandGame g;
	loadHandGame(*s, g);
	GameLogic &logic = g.game->logic();
	Object *structure = nullptr;
	GettingBuiltBehavior *gb = nullptr;
	for (const char *name : { "GondorFarm", "GondorBarracks", "GondorArcherRange", "GondorStable", "GondorForge", "GondorWorkshop", "GondorMarketPlace" })
	{
		Object *o = placeAt(logic, name, g.men, 1600.0f, 600.0f);
		GettingBuiltBehavior *m = dynamic_cast<GettingBuiltBehavior *>(o->findModule("GettingBuiltBehavior"));
		if (m && !m->data()->m_workerName.empty())
		{
			structure = o;
			gb = m;
			break;
		}
		logic.destroyObject(o);
	}
	REQUIRE_MESSAGE(gb != nullptr, "no Men structure with a GettingBuiltBehavior WorkerName");
	g.game->advance(0.2);
	gb->spawnWorkerOrStart(true); // RW 0x857A19
	REQUIRE(gb->workerSpawned());
	const ObjectID workerId = structure->getBuilderID();
	Object *worker = logic.findObjectByID(workerId);
	REQUIRE(worker != nullptr);
	MESSAGE(structure->getTemplate()->getName() << " spawned " << worker->getTemplate()->getName());
	CHECK(worker->getProducerID() == structure->getID());
	CHECK(worker->isKindOfName("DOZER"));
	logic.destroyObject(structure); // a sold / removed structure: deleted, not killed
	g.game->advance(0.2);
	worker = logic.findObjectByID(workerId);
	// UNRESISTABLE, FADED, the worker's max health (RW 0x698EC3(8, 0x16)): dead at once (its fade death may keep the object for a while)
	CHECK((worker == nullptr || worker->isEffectivelyDead()));
}

TEST_CASE("skirmish ai vs ai (S-420): an unfinished structure whose builder is gone is a target for an attack-move scan (ZH AI::findClosestEnemy has no construction filter)")
{
	OPENBFME_REQUIRE_START(s);
	HandGame g;
	loadHandGame(*s, g);
	GameLogic &logic = g.game->logic();
	Object *cage = placeAt(logic, "MordorTrollCage", g.mordor, 1600.0f, 600.0f);
	cage->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	cage->setConstructionPercent(83.0f);
	Object *knights = placeAt(logic, "GondorKnightHorde", g.men, 1600.0f, 700.0f);
	g.game->advance(0.2);
	REQUIRE(cage->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION));
	CHECK(CombatQueries::isAttackable(*cage));
	AIUpdateInterface *ai = knights->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	CHECK(ai->attackMoveTarget() == cage);
}

TEST_CASE("skirmish ai tactics: the waypoint distances carry retail's bases (Flank 600 + Real(0, 400) RW 0x9B4647, Pincer 450 + Real(0, 150) RW 0x9B406C)")
{
	OPENBFME_REQUIRE_START(s);
	HandGame g;
	loadHandGame(*s, g);
	GameLogic &logic = g.game->logic();
	logic.random().enableCallLog(true);
	const Coord3D start{ 1000.0f, 1000.0f, 0.0f }, target{ 3000.0f, 1000.0f, 0.0f };
	AITarget t;
	t.position = target;
	t.finderPosition = target; // RW 0x6C6975: a set target's threat finder stands on it (lane AI-3; RW 0x9B436B reads it)
	auto dist = [](const Coord3D &a, const Coord3D &b) { return (float)std::sqrt((double)(a.x - b.x) * (a.x - b.x) + (double)(a.y - b.y) * (a.y - b.y)); };
	for (int k = 0; k < 8; ++k)
	{
		logic.random().clearCallLog();
		AITactic flank;
		AITacticalAI::flankWaypointsForTest(logic, flank, t, start);
		const auto &fc = logic.random().callLog();
		REQUIRE(fc.size() == 2);
		REQUIRE(fc[0].line == 0x4E);
		// d = ftol(fadd(draw, 600)): x87 at PC24, then the truncating ftol
		const int d = (int)SimMath::ftol2((double)SimMath::pc24Add(fc[0].rresult, 600.0f));
		CHECK(d >= 600);
		CHECK(d <= 1000);
		REQUIRE(flank.waypoints.size() == 4);
		CHECK(flank.waypoints[0].x == start.x);
		CHECK(flank.waypoints[3].x == target.x);
		// beyond the target along the approach (+x): exactly d further
		CHECK(flank.waypoints[2].x == target.x + (float)d);
		CHECK(flank.waypoints[2].y == target.y);
		// beside the target: d to the side drawn at 0x5B
		CHECK(std::fabs(dist(flank.waypoints[1], target) - (float)d) < 0.01f);
		CHECK(flank.waypoints[1].x == target.x);

		logic.random().clearCallLog();
		AITactic pincer;
		AITacticalAI::pincerWaypointsForTest(logic, pincer, start, target);
		const auto &pc = logic.random().callLog();
		REQUIRE(pc.size() == 1);
		REQUIRE(pc[0].line == 0x87);
		const int p = (int)SimMath::ftol2((double)SimMath::pc24Add(pc[0].rresult, 450.0f));
		CHECK(p >= 450);
		CHECK(p <= 600);
		REQUIRE(pincer.waypoints.size() == 4);
		// the two sides of the target, p either way across the approach
		CHECK(pincer.waypoints[1].x == target.x);
		CHECK(pincer.waypoints[2].x == target.x);
		CHECK(std::fabs(pincer.waypoints[1].y - target.y) == (float)p);
		CHECK(std::fabs(pincer.waypoints[2].y - target.y) == (float)p);
		CHECK(pincer.waypoints[1].y != pincer.waypoints[2].y);
	}
	logic.random().enableCallLog(false);
}
