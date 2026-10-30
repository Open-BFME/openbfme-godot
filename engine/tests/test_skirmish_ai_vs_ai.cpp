// OpenBFME unit tests. GPL-3.0.
// Lane AI-2: whole skirmish games between computer players on Evendim (GameLogic/SkirmishAI). The games run the retail data through LiveGame; the
// expectations are outcomes retail's AI reaches (a victory within 30 game-minutes), never this engine's own frame-exact output.

#include "doctest.h"

#include "GameLogic/Module/AISpecialPowerUpdate.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
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
	// lane BACKLOG5 (Sol's review: "open at 9000" alone does not exclude a late stall): per 1000 frames, each player's attacks launched, units and structures built
	// and destroyed (the score keeper's counters, cumulative) in one line per window
	std::string progress;
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
		// lane AI5: the upgrade builders (records made, handed to the queue) and the spell book (purchase points, sciences owned)
		os << " unitUpgrades " << ai->build.unitUpgrader.registered << "/" << ai->build.unitUpgrader.submitted << "/" << ai->build.unitUpgrader.declined
		   << " structureUpgrades " << ai->build.scienceBuilder.registered << "/" << ai->build.scienceBuilder.upgradesSubmitted << " expansions "
		   << ai->build.scienceBuilder.expansionsSubmitted;
		os << " pp " << p->science().getSciencePurchasePoints() << " sciences";
		for (ScienceType st : p->science().sciences())
		{
			os << " " << (TheScienceStore ? TheScienceStore->getInternalNameForScience(st) : std::to_string((int)st));
		}
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


// lane AIPOW-1: the computer players' power use of a run (AISpecialPowerUpdate's diagnostics: the buttons run per SpecialPowerAIType)
std::string powerUseLine()
{
	const AISpecialPowerUpdate::Stats &st = AISpecialPowerUpdate::stats();
	std::ostringstream o;
	o << "AI powers: asked " << st.asked << ", yes " << st.yes << ", run " << st.executed << ", unported decisions " << st.unportedDecisions << ";";
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		if (st.executedByType[(size_t)t] != 0)
		{
			o << " " << (TheAISpecialPowerTypeNames[t] + 3) << " " << st.executedByType[(size_t)t];
		}
	}
	o << "; unported asked:";
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		if (st.unportedByType[(size_t)t] != 0)
		{
			o << " " << (TheAISpecialPowerTypeNames[t] + 3) << " " << st.unportedByType[(size_t)t];
		}
	}
	return o.str();
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
	// lane FLANK-1: the first frame each of the lane's mechanisms acted (a projectile contact hit, a pre-landing emotion, a landing, a flank), for the pin notes
	int firstContactHit = -1, firstPreLanding = -1, firstLanding = -1, firstFlank = -1;
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		{
			const CombatState::Counters &cc = logic.combat().counters();
			const int fr = (int)logic.getFrame();
			firstContactHit = firstContactHit < 0 && cc.projectileContactHits > 0 ? fr : firstContactHit;
			firstPreLanding = firstPreLanding < 0 && cc.projectilePreLandingEmotions > 0 ? fr : firstPreLanding;
			firstLanding = firstLanding < 0 && cc.projectilesLanded > 0 ? fr : firstLanding;
			firstFlank = firstFlank < 0 && cc.flanks > 0 ? fr : firstFlank;
		}
		if (hashEveryFrame)
		{
			out.hashes.push_back(logic.computeStateHash());
		}
		if (logEvery > 0 && (i % logEvery) == 0)
		{
			out.log += "frame " + std::to_string(logic.getFrame()) + "\n  " + summary(logic, players[0]) + "\n  " + summary(logic, players[1]) + "\n";
		}
		if ((logic.getFrame() % 1000) == 0)
		{
			out.progress += "frame " + std::to_string(logic.getFrame()) + ":";
			for (int k = 0; k < 2; ++k)
			{
				const ScoreKeeper &sk = players[k]->getScoreKeeper();
				const AISkirmishPlayer *ai = logic.skirmishAI().findAI(players[k]->getPlayerIndex());
				out.progress += " [" + std::to_string(k) + " attacks " + std::to_string(ai ? ai->brain.attacksLaunched : 0ull) + " built " + std::to_string(sk.unitsBuilt()) + "u/" +
					std::to_string(sk.structuresBuilt()) + "s destroyed " + std::to_string(sk.totalUnitsDestroyed()) + "u/" + std::to_string(sk.totalStructuresDestroyed()) + "s]";
			}
			out.progress += "\n";
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
	{
		const CombatState::Counters &cc = logic.combat().counters();
		out.log += "  flank1: projectile contacts " + std::to_string(cc.projectileContacts) + " hits " + std::to_string(cc.projectileContactHits) + " (first frame " +
			std::to_string(firstContactHit) + "), pre-landing emotions " + std::to_string(cc.projectilePreLandingEmotions) + " (" + std::to_string(firstPreLanding) +
			"), landings " + std::to_string(cc.projectilesLanded) + " (" + std::to_string(firstLanding) + "), flank tests " + std::to_string(cc.flankTests) + " flanked " +
			std::to_string(cc.flanks) + " (" + std::to_string(firstFlank) + "), crushes " + std::to_string(cc.crushes) + "\n";
	}
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
	AISpecialPowerUpdate::stats() = AISpecialPowerUpdate::Stats{};
	// lane AIPOW-1: OPENBFME_AIPOW1_MOD4_ONLY=1 answers the decisions AIPOW-1 ported with no (the power use before the lane)
	AISpecialPowerUpdate::mod4DecisionsOnlyForTest() = std::getenv("OPENBFME_AIPOW1_MOD4_ONLY") != nullptr;
	runVersus(*s, versusMessage(*s, a, 1, b, 1, seed), frames, run, false, 300);
	AISpecialPowerUpdate::mod4DecisionsOnlyForTest() = false;
	MESSAGE(run.log << "defeat frame " << run.defeatFrame << " loser " << run.loser);
	MESSAGE(powerUseLine());
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
			d->kill(DAMAGE_UNRESISTABLE, 0);
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
	// RW 0x8A4214 .. 0x8A424F; unclamped line ends RW 0x6E8CE6), re-measured on the reference PC: seed 2 is open at 9000, seed 7 is open at 9000, seed 1234 ends with
	// Men's defeat (start position 1) at 8249. Engine pins, not retail values.
	// Merge of merge/play2 (MOVE-3, CAMP-2, INPUT-1, CAH-2, DOCS-1 on 22e29980) into DECOMP-1 r3 (Sol's fixes: the victim gate of a damage nugget, the firing
	// weapon's slot, the turret aim gate of the weapon choice, the SHROUD_CLEARING refresh), re-measured on the merged tree with remote-measure.sh: seed 2 is open at
	// 9000, seed 7 ends with Mordor's defeat (start position 1) at 3869, seed 1234 with Men's defeat (start position 1) at 7528. Engine pins, not retail values.
	// Merge INTEG-3 (PLAY-1's tribute and settings hash, ARCHER-1's spread fire of a ranged horde RW 0x875221 / 0x86FA87 with LockWeaponSlot forwarded to the
	// members, on DECOMP-1's weapon choice and delivery; UI-3 / UI-4 / RELTEST-1 / PLAY-3 change no logic), re-measured on the merged tree 22902969 with
	// remote-measure.sh (a CI host): seed 2 ends with Men's defeat (start position 1) at 7205, seed 7 with Mordor's defeat (start position 1) at 4206, seed 1234 with
	// Men's defeat (start position 1) at 5519. Engine pins, not retail values. Lane GAMEMODES-1: a skirmish is judged by rule 3 (RW 0x808A93: TheGameLogic + 0x114
	// == 3), under which a side's last structure decides and its builders no longer keep it alive: seed 7 ended at 3831, seed 1234 at 5443 (same losers) on its own tree.
	// Lane DECOMP-2 (the team builder's recruit test RW 0x9A24FC and threat hand-over RW 0x9A26A1, RotWK's idle dispatch RW 0x9A3040, the AI's heroes RW 0x9A0993,
	// PLAYER_HUNT), merged with archive ce2fa4e8 and re-measured with remote-measure.sh (a CI host) on 30db6133: seed 2 ends with Men's defeat (start position 1) at
	// 8853, seed 7 with Mordor's defeat (start position 1) at 5670, seed 1234 is open at 9000. Engine pins, not retail values.
	// Lane AIPOW-1 (the computer's heroes, units and spell book use 35 of the 53 AISpecialPowerUpdate decisions, with the ActionManager's location test): seed 2
	// is open at 9000, seed 7 ends with Mordor's defeat (start position 1) at 6155, seed 1234 with Men's defeat at 5419. With the lane's decisions answering no
	// (OPENBFME_AIPOW1_MOD4_ONLY, the diagnostic run) seed 2 ends at 8208 again. AIPOW-1 r2 (SHROUD_REVEAL waits until after logic frame 50, RW
	// 0xDEC148): seed 7 now ends at 8660 (6155 before), the other two are unchanged. AIPOW-1 r2 part 2 (the spell book's ASSIST_BATTLE, BUFFTERRAIN, REBUILD,
	// ENSHROUDINGMIST, CALLTHEHORDE and BUFFECONOMYBUILDING decisions, on the archive 78ee4874 merged in): seed 2 still open, seed 7 Mordor at 6949, seed
	// 1234 Men at 8319. Engine pins, not retail values.
	// Merge of the archive 27f57575 (v0.3.0-preview.1) into AIPOW-1 r2 part 2, re-measured on the reference PC: seed 2 is open at 9000, seed 7 is open at 9000,
	// seed 1234 ends with Men's defeat (start position 1) at 7399. Engine pins, not retail values.
	// Merge of the archive ce2fa4e8 into AIPOW-1 r2 part 2, re-measured on the merged tree with remote-measure.sh (a CI host): seeds 2, 7 and 1234 are all open at
	// 9000. Engine pins, not retail values.
	// Merge INTEG-6 (v0.4.0-alpha.1: DECOMP-2's team builder, heroes and PLAYER_HUNT, GAMEMODES-1's victory rule 3, AIPOW-1's 42 of 53 decisions; MENU-1, BUILD-5
	// and ONLINE-1 change no logic), re-measured on the merged tree 2185a023 with remote-measure.sh (a CI host): seed 2 is open at 9000, seed 7 ends with Men's defeat
	// (start position 0) at 4511, seed 1234 is open at 9000. Engine pins, not retail values.
	// Merge queue q1 (SPELL-3's isAbleToAttack mood gate and Flood, FACT-B's upgrade-button / initial-upgrade rules and CastleUpgrade, FACT-A's FIRE_WEAPON /
	// NO_AUTO_ACQUIRE / ObjectCreationUpgrade / ReplaceSelfUpgrade; CRASH-2, UI-6 and the Android roadmap change no logic), re-measured on the merged tree
	// 0704c597 with remote-measure.sh (a CI host): seed 7 ends with Men's defeat (start position 0) at 4792 (4511 before), seeds 2 and 1234 stay open at 9000; no stalls.
	// Merge queue q2 (FACT-C: ObjectCreationUpgrade RW 0x8B8464 [the moats, the free wall catapults, the delayed grants], ArmorUpgrade's default ArmorSetFlag
	// PLAYER_UPGRADE RW 0x6542D8, HordeDispatchSpecialPower; BUGS-1's production exit fix; GLIBC-1 / DEP-1 change no logic; PATHPERF-1 is not in this batch),
	// re-measured on the merged tree 336b3e4e with remote-measure.sh (a CI host): seed 7 is now open at 9000 (Men's defeat at 4792 before), seeds 2 and 1234 stay
	// open at 9000; no stalls. Engine pins, not retail values.
	// Lane DECOMP-3 (the player info record AIStatCollector RW 0x8E46D9 filled by the object events: event order instead of id order, dying units listed until
	// destroyed, contained units unlisted; Object::isAbleToAttack RW 0x691269 in the group step), (on bb053f01: seed 2 open, seed 7 Mordor at 7421, seed 1234 open), merged with the archive cb2ae2c7 and re-measured with remote-measure.sh (a CI host) on cba967b4: seeds 2, 7 and 1234 are all open at 9000. Engine pins, not retail values.
	// Lane PLAY-4 (the stances' guard machine RW 0x949A10: idle units in Battle guard their spot, chase and walk back, with the guard's random draws; the melee
	// back-off's CanApproachToTarget RW 0x6FB754 lets lone melee units engage hordes; production disabled while a structure is built, RW 0x856434; a plot's
	// self-build starting at the placement, RW 0x6AAF3B; the damage visual skipped under construction, RW 0x8C3290): re-measured on the lane's tree (Deck,
	// measure.sh's three runs): seed 2 is open at 9000, seed 7 ends with Men's defeat (start position 0) at 4466, seed 1234 with Men's defeat (start position 1)
	// at 6474. Engine pins, not retail values.
	// Lane PLAY-4 r1 (RW 0x6CC07C's melee reach of a moving unit is RW 0x6ED843's footprint test, not the edge distance: melee units engage from further away;
	// the idle scan's gate RW 0x66849D: NO_AUTO_ACQUIRE / SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING / IGNORE_AI_COMMAND / JUST_BUILT / COMING_OUT_OF_FACTORY
	// scan nothing; the line callback RW 0x6F61BD completed): seed 2 is open at 9000, seed 7 ends with Men's defeat (start position 0) at 6315, seed 1234 is
	// open at 9000. Engine pins.
	// Merge of the archive fc42d9ee (merge queue q3: ONLINE-2, SELPERF-1, UBER-1) into PLAY-4 r2, re-measured on the merged tree 967f63ed with
	// remote-measure.sh (a CI host): seeds 2, 7 and 1234 are all open at 9000. Engine pins, not retail values.
	// Merge of the archive b84bd75d (q4b: DECOMP-3's player info record and isAbleToAttack, DEP-2's deterministic libm; q5) into PLAY-4 r3, re-measured on
	// the merged tree 94360a28 with remote-measure.sh (a CI host) and on the Deck: seed 7 ends with Men's defeat (start position 0) at 6019, seeds 2 and 1234 are
	// open at 9000. Engine pins, not retail values.
	// Merge queue q8 (PATHPERF-1's hierarchical corridor, RW 0x6F8601: the routes change; ONLINE-4 and DEPLOY-2 change no logic), re-measured on the merged tree
	// ea5eba29 with remote-measure.sh (a CI host): seed 2 now ends with Mordor's defeat (start position 0) at 8210, seed 7 with Men's defeat (start position 0) at
	// 8373 (6019 before), seed 1234 stays open at 9000. Engine pins, not retail values.
	// Lane BACKLOG-1 r2 (state 26's exit RW 0x74949C destroys the move-away path: a unit that made room for an ally walks on to its destination instead of
	// stopping where the move-away path ended; with those three lines of AIMoveOutOfTheWayState::onExit removed the q8 pins come back), measured on the lane's
	// tree 4908c2fd (Deck): seed 2 is open at 9000 (Mordor's defeat at 8210 before), seed 7 ends with Men's defeat (start position 0) at 5029 (8373 before),
	// seed 1234 with Mordor's defeat (start position 0) at 8634 (open before). Engine pins, not retail values.
	// Lane ATTACK-1 (blockedBy's crush exemption RW 0x66D16E asks RW 0x69519A with the unit's live crusher level, RW 0x695070: mounted heroes ride through
	// the hordes they may crush, units whose live level or policy refuses the crush are blocked), re-measured on the merged tree 785dbd65 (the archive
	// 2b841363) with remote-measure.sh (a CI host): seed 2 ends with Mordor's defeat (start position 0) earlier, at 7842, seed 7 is unchanged (8373), seed 1234 now
	// ends with Mordor's defeat (start position 0) at 8656. Engine pins, not retail values.
	// Merge queue q11 (BACKLOG-1's garrison / entering / exiting ports and ATTACK-1's crush test both moved the AI-vs-AI seeds on older bases; BACKLOG-1's
	// PERF-1 and Uber vectors already match the merged tree), re-measured once on the merged tree 3631817a with remote-measure.sh (a CI host): seeds 2 and 1234
	// are open at 9000 (still fighting, no stall), seed 7 ends with Men's defeat (start position 0) at 5029, as BACKLOG-1 pinned. ATTACK-1's 7842 (seed 2)
	// and 8656 (seed 1234) are superseded. Engine pins, not retail values.
	// Lane SAVE-4c r5 (the unit builder's factory map filled and emptied by the object events, RW 0x8F06BF -> 0x9A0838 and RW 0x8F0C69 -> 0x9A051D, instead of
	// a scan of completed structures: a factory is listed from its placement), on the archive 98750dad, measured on the Deck: seed 2 is open at 9000, seed 7
	// ends with Men's defeat (start position 0) at 3479, seed 1234 stays open at 9000; merged with the archive de94ba60 (q11) and measured on the Deck: seed 2
	// open, seed 7 Men's defeat (start position 0) at 3479, seed 1234 Men's defeat (start position 1) at 8193. Merged with the archive 593e83cd (q12 to q14:
	// BACKLOG-4's teleport / production exits / defection, BACKLOG-6's skirmish AI, BACKLOG-3) and measured on the merged tree (Deck): seed 2 open, seed 7 at
	// 3479, seed 1234 Men's defeat (start position 1) at 8981. Engine pins, not retail values.
	// Lane BACKLOG5 (computePath's patch for a blocked unit, pathfinder slot 0x10 RW 0x6FC9DA, where the port searched anew; the closest-path fallback only
	// for an invalid destination or a failed patch; internalFindPath's goal check with the info radius 1, RW 0x6FD341, and the start check with the flags 0,
	// RW 0x6FD8DF), merged with the archive de94ba60 (q11), re-measured on 344ce545 with remote-measure.sh (a CI host) and on the Deck (the same results): seed 2 is
	// open at 9000, seed 7 ends with Mordor's defeat (start position 1) at 3313 (Men's at 5029 before), seed 1234 with Mordor's defeat (start position 0) at 5581
	// (open before). Seed 2's progress per 1000 frames is pinned below: both sides keep attacking, building and destroying through the last window. Engine
	// pins, not retail values.
	// Merge queue q16 (SAVE-4c r5's factory-map unit builder + BACKLOG-5's move path, both changing the AI's early build and move timing, so the two
	// lanes' own pins no longer apply): re-measured once on the merged tree b53981be (remote-measure.sh, a CI host): the losers are unchanged, seed 7 (Mordor,
	// start position 1) now ends at 5593 (3313 on BACKLOG-5's tree, 3479 on SAVE-4c's), seed 1234 (Mordor, start position 0) at 5192 (5581 / 8981), seed 2
	// stays open at 9000 with a new progress table (frame 1000: 82 units built by side 0, 59 before). Engine pins, not retail values.
	// Merge queue q18 (DEVPORT-3C's castle keep gate GettingBuiltBehavior::castleBlocks RW 0x8561C0 / 0x79A09D and Horde recent-damage provider RW 0x86EE52 /
	// 0x87370B; the lane measured each alone on its own base, 6225 for seed 7 and 6939 for seed 1234; OWNERFIX-1 is client / HUD only and moves nothing),
	// re-measured once on the merged tree 0b09f00f (remote-measure.sh, a CI host): the losers are unchanged, seed 7 (Mordor, start position 1) now ends at 5879
	// (5593 before), seed 1234 (Mordor, start position 0) at 5289 (5192), seed 2 stays open at 9000 with the progress table changed from the 6000 window on
	// (the first five windows are identical). The lane's numbers differ from these because its base lacked q15-q17. Engine pins, not retail values.
	// Merge queue q19 (BACKLOG-8's formation march RW 0x774897 -> 0x7747DE / 0x77348C with its final-leg adjustment RW 0x75040D, the formation / march settings rows in the
	// settings hash, and DEVPORT-5's caster / spectator client, which moves nothing in the simulation), re-measured once on the merged tree 80d88977 (the archive q18 0b09f00f
	// + both lanes; remote-measure.sh and CI test runs): seed 7 (Mordor, start position 1) now ends at 6763 (5879 before), seed 1234 (Mordor, start position 0) is unchanged
	// at 5289, seed 2 stays open at 9000 with the progress table changed from the 6000 window on (the first five windows are identical). The lane's own pins (7433 for seed 7,
	// seeds 2 and 1234 open) differ because its base lacked q15-q18. Engine pins, not retail values.
	// Lane DEVPORT-7 (the Devin branch owner-fixes-1: BloodthirstyUpdate on every Mordor / Isengard horde, whose retail update slots reorder the other modules'
	// updates, RW 0x62B921 / 0x85123F, plus AutoHealBehavior's all-bits KindOf and replenishment, S-853 / S-261 / S-635), merged with the archive 0b941a37 (q20)
	// and measured once on the merged tree (remote-measure.sh on a CI host; the messages by another host's run of the same build): the losers are unchanged, seed 7 (Mordor,
	// start position 1) ends at 4103 (6763), seed 1234 (Mordor, start position 0) at 5701 (5289), seed 2 stays open at 9000 with the table changed from the
	// 2000 window on. Engine pins, not retail values.
	// Lane STEALTHFLY1 (a giant bird's exit path: GiantBirdAIUpdate's follow path state 0x3F6, RW 0x76B491): in seed 2 Mordor recruits MorgomirFellBeast at
	// frame 7145, which now leaves the fortress flying its exit path instead of running the ground path state; the table is unchanged up to the 7000 window
	// and changes from the 8000 window on. Seeds 7 and 1234 are unchanged (no bird leaves a building before their ends). Engine pins, not retail values.
	// Lane SUMMON-1 (the CreateObject nuggets' retail random draws and dispositions, DoCommandUpgrade's rock-throw toggle on every troll, ent and giant
	// scripts.lua grants Upgrade_SwitchToRockThrowing at creation, the Thrall Master's summons), measured on the lane's tree (Deck, the test alone): the losers
	// are unchanged, seed 7 (Mordor, start position 1) ends at 3790 (4103), seed 1234 (Mordor, start position 0) at 4095 (5701), seed 2 stays open at 9000
	// with the table changed from the 2000 window on. Merged with the archive 5f7c6635b (STEALTHFLY-1's bird exit path among it) and measured on the merged
	// tree (Deck, the test file alone, twice): the same three results. Engine pins, not retail values.
	// Lane AI5 (the AI's upgrade purchases: AIUnitUpgrader, AIUpgradeScienceBuilder with the plot expansions, the upgrade heuristics, and the money saver /
	// reserve RW 0x8F0502: new draws at registration and decision, money spent on upgrades and saved for heroes), measured on the lane's tree (Deck): seed 2
	// stays open at 9000 with a new progress table (changed from the first window), seed 7 is now open at 9000 (Mordor's defeat at 4103 before), seed 1234
	// ends with Mordor's defeat (start position 0) at 4768 (5701 before). Engine pins, not retail values.
	// Merge queue q22 (SUMMON-1 + FX-4 + AI5 + NUGGET-1: SUMMON-1's OCL draws / rock-throw toggles together with AI5's upgrade draws, purchases and money
	// saver; each lane alone measured the values above), measured once on the merged tree (srv1, the test file alone, twice): seed 2 stays open at 9000 with a new
	// progress table (changed from the first window), seed 7 ends with Mordor's defeat (start position 1) at 5693 (3790 before), seed 1234 is open at 9000
	// (Mordor's defeat at 4095 before). Engine pins, not retail values.
	// Lane TARGET-1 (RotWK's target choice: the combat chains' priorities, the reachability test, MoodAttackCheckRate's 2-second default, the one-off
	// check offsets drawing the logic generator, an attack-move order pushing the mood check back), measured on a CI host (the laptop): seed 2 stays open at
	// 9000 with a new table (below), seed 7 ends with Mordor's defeat (start position 1) at 6459 (4103), seed 1234 with Mordor's defeat (start position 0)
	// at 5072 (5701). Engine pins, not retail values.
	// Lane ATTACKPATH-1 (RotWK's findAttackPath with its view test, own corridor and ranked fallback; the attack path's failure branch; the horde approach goal's
	// helpers RW 0x6F2F8F / 0x746783 / 0x6FB67A; the towards-goal line's corridor term RW 0x6F6D57 in every path search), measured on the lane's tree bb49bd6a6
	// (measure.sh's three runs on a CI host, the laptop): all three games are open at 9000 with both armies still fighting (seed 2's table below: every counter
	// rises through the last window); seed 2's table changed from the 1000 window on. Engine pins, not retail values.
	// Lane FLANK-1 (projectiles that meet objects in flight RW 0x85E52B / 0x6CC79C, a landed thrown unit busy or idle RW 0x85FAA4, the flank test at fire time
	// RW 0x6CCB4D, the crush contact RW 0xAD2CE0 with heights, the charge state's exits, the miss scatter RW 0x6CC399), measured on the lane's tree (dev): seed 2 is no
	// longer open: Men (start position 1) are defeated at 8035; its progress is identical through the 1000 window and differs from the 2000 window (the first
	// fire-time flank at frame 1481, the first landing of a thrown unit at 3123, the first in-flight hit at 3174). Seed 7 (Mordor, start position 1) ends at 4027 (4103;
	// first in-flight hit 2174, first flank 1849). Seed 1234 (Mordor, start position 0) ends at 6317 (5701) with no projectile contact or landing in the game, so its move
	// comes from the flank timing (first flank 1999), the crush contact or the scatter. The `flank1:` line of each run's log has the counts. Engine pins, not retail values.
	// Lane FLANK-1 alone (dev): seed 2 Men defeated at 8035, seed 7 Mordor at 4027, seed 1234 Mordor at 6317. Merge queue q23 (TARGET-1's target choice and
	// mood scan, ATTACKPATH-1's attack paths and horde approach, FLANK-1's in-flight hits / fire-time flank / crush contact / scatter, PRODUCTION1's levelled
	// build speed, LUMBER1's evil lumber mills, on q22's tree; each lane alone measured the results above), measured once on the merged tree (the test file
	// alone, the Mac and the laptop): all three games are open at 9000 with both armies still fighting; seed 2's table below. Engine pins, not retail values.
	// Lane CAMPUI-1 (OFF_MAP, Object + 0x458 bit 3 from the map's extent, RW 0x69355D: the mood scan RW 0x66845E and the closest-enemy filter RW 0x6FF7A4
	// skip an object outside the active boundary; the lane's only change a skirmish runs), measured once on the lane (a Linux CI host): seed 2's table is
	// unchanged up to the 7000 window and changes from the 8000 window on (an object first stands outside the boundary after frame 7000; which one is not
	// named here). Seeds 7 and 1234 are unchanged (4103 / 5701). Engine pins, not retail values.
	// Merge queue q24 (CAMPUI-1's OFF_MAP gates, OFFMAP-1's retail idle-state start RW 0x745A64 (no ZH frame <= 1 snap: every start unit stays where it was
	// made), SCRIPT-4, MODS-3's DoCommandUpgrade / PLAYER_UPGRADE buttons and library teams, and the amoeba reach that skips an enclosed occupant, on q23's
	// tree; measured once on the merged tree, a CI host, the test file alone, twice): seed 2 stays open at 9000 with a new table (changed from the first window),
	// seed 7 ends with the Men's defeat (start position 0) at 6368, seed 1234 stays open at 9000. Engine pins, not retail values.
	// Merge queue q26 (community #73 / #74's centred formations and spiral, BANNER-1's refills, #101's SPEED modifiers, SIEGE-1 (#64) and the rest on the
	// archive's tree; measured once on the merged tree, two CI hosts): seed 2 stays open at 9000 with a new table (changed from the first window), seed 7 ends
	// with the Men's defeat (start position 0) at 4890 (was 6368), seed 1234 stays open at 9000. Engine pins, not retail values.
	// Merge queue q27 (LOS-1's terrain line of sight with #105's trees, RADIUSHEIGHT-1, GATEPUSH-1, HEROCAST-1 and #111 on the archive's tree; measured once
	// on the merged tree and at each merge point, a CI host): seed 2 stays open at 9000 with a new table (changed from the first window), seed 7 ends with
	// the Men's defeat (start position 0) at 6354 (was 4890), seed 1234 stays open at 9000; LOS-1 alone gives these three. Engine pins, not retail values.
	// Merge queue q28 (#96 / #97 / #98, #117's fortress module in the update schedule, #124 and the archive's plain batch; measured once on the merged tree
	// and at each merge point, a CI host): seed 2 stays open at 9000 with a new table (changed from the first window), seed 7 ends with the Men's defeat
	// (start position 0) at 4533 (was 6354), seed 1234 stays open at 9000. Engine pins, not retail values.
	// Merge queue q29 (measured once on the merged tree and at each merge point, a CI host): seed 2 stays open at 9000 with a new table (changed from the first
	// window), seed 7 is now open at 9000 (the Men's defeat at 4533 before), seed 1234 stays open at 9000. At the merge points seed 7's defeat moved to 8399
	// (DEVPORT-SIM), the game stayed open from PATHPRIO-1 on (as that lane measured), community #113 ended seed 1234 at 7647, IDLEMOTION-1 seed 7 at 7924
	// (1234 open again); with FBSIM-1 all three are open. Engine pins, not retail values.
	for (const Game &g : { Game{ "FactionMordor", "FactionMen", 2u, -1, -1 }, Game{ "FactionMen", "FactionMordor", 7u, -1, -1 }, Game{ "FactionMordor", "FactionMen", 1234u, -1, -1 } })
	{
		INFO(g.a << " vs " << g.b << " seed " << g.seed);
		VersusRun run;
		AISpecialPowerUpdate::stats() = AISpecialPowerUpdate::Stats{};
		runVersus(*s, versusMessage(*s, g.a, 1, g.b, 1, g.seed), 9000, run, false, 0);
		MESSAGE(run.log << "defeat frame " << run.defeatFrame << " loser " << run.loser);
		MESSAGE(powerUseLine());
		MESSAGE("progress, seed " << g.seed << ":\n" << run.progress);
		CHECK(run.attacks[0] + run.attacks[1] >= (g.seed == 2u ? 2u : 4u)); // DECOMP-1 r3: the seed 2 game launches two (see above)
		// SMOOTH-2 (the pending position cleared every frame, RW 0x62618F, feeds the crush warning's velocity RW 0x68EF58) ends seed 1234 with Men's defeat:
		// 5275 on SMOOTH-2's base, 5186 on the merged tree (with GARRISON-1 / STEALTH-2 / END-2); seeds 2 and 7 stay open. Lane SMOOTH-3 (RotWK's member hub near arm RW 0x874F09, the angle goal RW 0x5E98D6 turning at the locomotor's rate): seed 1234 is open at 9000 as well. Engine pins, not retail values.
		// SCRIPT-2 (merged with AUDIO-3 / GARRISON-2 / SMOOTH-3): the AI libraries' scripts now evaluate the conditions they use (PLAYER_HAS_OBJECT_COMPARISON ...
		// were false before; switching only those back to unported restored the old runs on SCRIPT-2's base): seed 2 ends with Men's defeat at 6171, seeds 7 and
		// 1234 are open at 9000. Engine pins, not retail values.
		// Merge queue q12 (BACKLOG-4 special power runtime / teleport / defection, BACKLOG-6 skirmish AI), re-measured once on the merged tree 5d90b467 (remote-measure.sh, a CI host):
		// seed 7 still ends with Men's defeat, now at 4697 (was 5029); seeds 2 and 1234 are still open at 9000 with the armies fighting. Engine pins, not retail values.
		CHECK(run.loser == g.loser);
		CHECK(run.defeatFrame == g.defeatFrame);
		if (g.seed == 2u)
		{
			// lane BANNER-1 (the banner carrier's refill as RotWK: replacements at the horde's experience and not counted as built, RW 0x89A40E; refills after
			// RW 0x86EE52's windows): seed 2's table moves (fewer units built, other fights); the three games stay open at 9000
			// merge queue q29: seed 2's table (both sides keep attacking, building and destroying through the windows; a sample every 1000 frames, cumulative)
			CHECK(run.progress ==
		"frame 1000: [0 attacks 0 built 72u/8s destroyed 25u/2s] [1 attacks 0 built 75u/8s destroyed 35u/1s]\n"
		"frame 2000: [0 attacks 1 built 260u/11s destroyed 116u/3s] [1 attacks 1 built 169u/10s destroyed 138u/4s]\n"
		"frame 3000: [0 attacks 1 built 408u/12s destroyed 208u/4s] [1 attacks 2 built 306u/11s destroyed 337u/4s]\n"
		"frame 4000: [0 attacks 4 built 595u/14s destroyed 301u/5s] [1 attacks 2 built 365u/14s destroyed 501u/4s]\n"
		"frame 5000: [0 attacks 5 built 788u/16s destroyed 457u/5s] [1 attacks 3 built 512u/15s destroyed 673u/5s]\n"
		"frame 6000: [0 attacks 7 built 986u/16s destroyed 573u/5s] [1 attacks 3 built 653u/15s destroyed 919u/6s]\n"
		"frame 7000: [0 attacks 10 built 1186u/16s destroyed 630u/5s] [1 attacks 5 built 723u/18s destroyed 1175u/6s]\n"
		"frame 8000: [0 attacks 11 built 1317u/17s destroyed 771u/6s] [1 attacks 5 built 833u/19s destroyed 1258u/6s]\n"
		"frame 9000: [0 attacks 12 built 1418u/18s destroyed 849u/10s] [1 attacks 5 built 947u/22s destroyed 1385u/6s]\n");
		}
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

TEST_CASE("skirmish ai targetless: the generator's FarmKillSquad draw at creation, the per-frame pick and the squads' counters (RW 0x9BB3CA / 0x90C5E2 / 0x9BC39F / 0x9BB1C4 / 0x9BB479)")
{
	// community #113 (FlagCaptureSquad ported, RW 0x9BC39F / 0x9BC1A0): FarmKillSquad is no longer the only targetless tactic that can apply. FlagCaptureSquad is
	// the first prototype in RW 0x90C0EE's order and applies with no squad running and its next-run frame due (both counters start as 0, RW 0x6C7EDD) while a
	// non-allied CAPTUREFLAG object stands (Evendim's neutral buildings); FarmKillSquad applies with its early flag set (RW 0x9BB1C4). Measured (engine pins, a
	// CI host): at frame 10 both apply, the pick draws 0 of 0 .. 1 and starts the FlagCaptureSquad; at frame 11 it is running, so the pick draws among the
	// FarmKillSquad alone (0 .. 0) and starts it
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
	auto started = [&](const char *kind) {
		auto it = ai->brain.tacticsByKind.find(kind);
		return it == ai->brain.tacticsByKind.end() ? 0 : it->second;
	};
	auto counter = [&](const char *name) {
		auto it = ai->brain.namedCounters.find(name);
		return it == ai->brain.namedCounters.end() ? -1 : it->second;
	};
	int firstStart = -1, farmKillStart = -1;
	for (int i = 0; i < 2400 && farmKillStart < 0; ++i)
	{
		game.advance(0.2);
		if (firstStart < 0 && ai->brain.targetlessStarted > 0)
		{
			firstStart = (int)logic.getFrame();
			MESSAGE("first targetless start at frame " << firstStart << ": FlagCaptureSquad " << started("FlagCaptureSquad") << ", FarmKillSquad "
													 << started("FarmKillSquad") << ", picks " << ai->brain.targetlessPicks << ", unported applicability checks "
													 << ai->brain.targetlessUnported);
			CHECK(firstStart == 10);
			CHECK(started("FlagCaptureSquad") == 1);
			CHECK(started("FarmKillSquad") == 0);
			CHECK(counter("AIFlagCaptureSquad_IsRunning") == 1);
			CHECK(ai->brain.targetlessUnported == 9); // the other 9 prototypes' applicability, once per pick frame (S-893)
		}
		if (started("FarmKillSquad") > 0)
		{
			farmKillStart = (int)logic.getFrame();
		}
	}
	MESSAGE("FarmKillSquad early flag " << ai->brain.farmKillEarly << ", first targetless start at frame " << firstStart << ", FarmKillSquad's at "
									  << farmKillStart << ", picks " << ai->brain.targetlessPicks << ", started " << ai->brain.targetlessStarted
									  << " (FlagCaptureSquad " << started("FlagCaptureSquad") << "), unported applicability checks " << ai->brain.targetlessUnported);
	REQUIRE(firstStart > 0);
	REQUIRE(farmKillStart == 11);
	// RW 0x90C5E2: the two always-started prototypes never apply here, so every start is one pick's draw
	CHECK(ai->brain.targetlessPicks == ai->brain.targetlessStarted);
	CHECK(ai->brain.targetlessStarted == 2);
	CHECK(started("FlagCaptureSquad") == 1);
	CHECK(started("FarmKillSquad") == 1);
	CHECK(counter("FarmKillSquad::IsRunning") == 1);
	CHECK(ai->brain.targetlessUnported == 18);
	// RW 0x90C0EE constructs the prototype (its draw); each pick draws among the pool (AITacticsGenerator.cpp 0x1BC: 0 .. pool size - 1); a FlagCaptureSquad
	// team draws its maximum (AIFlagCaptureSquad.cpp 0xEF, RW 0x9BC1A0) and its end its next run (0xD5, RW 0x9BBFDC); RW 0x9BB43D constructs the FarmKillSquad
	// clone before copying its prototype flag, and RW 0x9BB479 sets it up (0x267)
	std::vector<GameLogicRandom::Call> draws;
	for (const auto &c : logic.random().callLog())
	{
		if (c.file == "AIFarmKillSquad.cpp" || c.file == "AIFlagCaptureSquad.cpp" || (c.file == "AITacticsGenerator.cpp" && c.line == 0x1BC))
		{
			draws.push_back(c);
		}
	}
	struct Draw
	{
		const char *file;
		int line, lo, hi;
	};
	const Draw expected[] = { { "AIFarmKillSquad.cpp", 0x39, 1, 100 }, // the prototype at creation
		{ "AITacticsGenerator.cpp", 0x1BC, 0, 1 },                       // frame 10: FlagCaptureSquad and FarmKillSquad apply
		{ "AIFlagCaptureSquad.cpp", 0xEF, 1, 3 },                        // the FlagCaptureSquad team's maximum
		{ "AITacticsGenerator.cpp", 0x1BC, 0, 0 },                       // frame 11: FarmKillSquad alone
		{ "AIFarmKillSquad.cpp", 0x39, 1, 100 },                         // its clone's constructor
		{ "AIFarmKillSquad.cpp", 0x267, ai->build.phase == 0 ? 1 : 2, 3 } };
	REQUIRE(draws.size() == 6);
	for (size_t i = 0; i < draws.size(); ++i)
	{
		INFO("draw " << i);
		CHECK(draws[i].file == expected[i].file);
		CHECK(draws[i].line == expected[i].line);
		CHECK(draws[i].lo == expected[i].lo);
		CHECK(draws[i].hi == expected[i].hi);
		CHECK(draws[i].drewNumber);
	}
	CHECK(draws[1].result == 0); // the first in the pool: FlagCaptureSquad
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
	// Lane SUMMON-1 (the OCLs' retail random draws, DoCommandUpgrade's rock-throw toggles): none of these twelve hands a Pincer team its waypoints within 2600
	// frames; a probe of 16 seeds on the lane's tree (Deck) found 53 (two distances), 71, 101 and 113 (one each)
	for (std::uint32_t seed : { 1234u, 2u, 7u, 11u, 13u, 17u, 19u, 31u, 29u, 67u, 37u, 41u, 53u, 71u })
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
	// lane TARGET-1: the attack-move scan is RotWK's mood scan (RW 0x66844A(1, 0)), which keeps its own cadence (AI + 0x21C): it is made due now
	ai->setNextMoodCheckTime(logic.getFrame());
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
