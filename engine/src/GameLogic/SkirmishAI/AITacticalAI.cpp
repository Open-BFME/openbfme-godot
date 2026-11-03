// OpenBFME. GPL-3.0.
// See GameLogic/SkirmishAI/AITacticalAI.h for the RW facts, the inferences (S-418) and what is not ported (S-417).

#include "GameLogic/SkirmishAI/AITacticalAI.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/SkirmishAIEditionRules.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <stdexcept>
#include <map>

namespace
{
constexpr float kSecondEnemyWithin = 1000.0f;   // RW 0xC893D8
constexpr int kLogicFramesPerSecond = 5;        // RW 0xD9F608
// TheAITargetHeuristicLibrary's heuristic types in library order (RW 0x82EF7B)
constexpr int kHeuristicTypes[] = { 1, 0, 3, 2, 2, 0 };
constexpr int kEnemyStructure = 0, kDefensive = 1, kOpportunity = 2, kExpansion = 3;
// the generator's active lists in RW's order (RW 0x90B8C7 updates, RW 0x90BA94 drops, RW 0x8F157F searches them so): + 0x10 offensive, + 0x28 defensive (not
// ported: never filled), + 0x40 expansion, + 0x58 targetless; the port keeps one list and walks it once per RW list
constexpr int kTacticLists[] = { 0, 2, 3 };
constexpr int kSimpleExpansionIndex = 8; // AITactic::kindIndex of SimpleExpansion (OffensiveKind SIMPLE_EXPANSION)
int tacticList(const AITactic &t)
{
	return t.targetless ? 3 : t.kindIndex == kSimpleExpansionIndex ? 2 : 0;
}
constexpr float kAttackedAtRadius = 300.0f;      // RW 0xC16764
constexpr float kAttackedAtNearSq = 90000.0f;    // RW 0xC16768
constexpr float kAttackedAtMergeSq = 360000.0f;  // RW 0xC16784
// the offensive prototypes of the generator (RW 0x90BE31); only SimpleAttack is ported (S-417)

// the KindOf bits the AI reads (TheKindOfNames RW 0xDA0E68; the names are the binary's, looked up once)
struct Kinds
{
	int unattackable, immobile, structure, ship, canAttack, hero, support, dozer, expansionPad, notAutoAcquirable, rebuildHole, economyStructure, linkedToFlag,
		baseSite, ignoresSelectAll, dozerFactory, walkOnTopOfWall, defensiveWall, wallUpgrade, ignoreForVictory, wallGate, baseFoundation, wallHub, wallSegment;
};

const Kinds &kinds()
{
	static const Kinds k = { KindOfTokens::indexOf("UNATTACKABLE"), KindOfTokens::indexOf("IMMOBILE"), KindOfTokens::indexOf("STRUCTURE"),
		KindOfTokens::indexOf("SHIP"), KindOfTokens::indexOf("CAN_ATTACK"), KindOfTokens::indexOf("HERO"), KindOfTokens::indexOf("SUPPORT"),
		KindOfTokens::indexOf("DOZER"), KindOfTokens::indexOf("EXPANSION_PAD"), KindOfTokens::indexOf("NOT_AUTOACQUIRABLE"), KindOfTokens::indexOf("REBUILD_HOLE"),
		KindOfTokens::indexOf("ECONOMY_STRUCTURE"), KindOfTokens::indexOf("LINKED_TO_FLAG"), KindOfTokens::indexOf("BASE_SITE"),
		KindOfTokens::indexOf("IGNORES_SELECT_ALL"), KindOfTokens::indexOf("DOZER_FACTORY"), KindOfTokens::indexOf("WALK_ON_TOP_OF_WALL"),
		KindOfTokens::indexOf("DEFENSIVE_WALL"), KindOfTokens::indexOf("WALL_UPGRADE"), KindOfTokens::indexOf("IGNORE_FOR_VICTORY"), KindOfTokens::indexOf("WALL_GATE"),
		KindOfTokens::indexOf("BASE_FOUNDATION"), KindOfTokens::indexOf("WALL_HUB"), KindOfTokens::indexOf("WALL_SEGMENT") };
	return k;
}

bool is(const Object &o, int kind)
{
	return kind >= 0 && o.isKindOf((unsigned)kind);
}

bool alive(const Object *o)
{
	return o && !o->isEffectivelyDead() && !o->isDestroyed(); // + 0x458 bit 0, + 0x94 bit 0
}

Player *playerOf(GameLogic &logic, int index)
{
	return logic.players().getNthPlayer(index);
}

// RW 0x9B2B04's position of a player's base: an AI's base builder start position (RW 0x8F0284 -> 0x90CD45), else its start waypoint (RW 0x90CE76)
Coord3D basePositionOf(GameLogic &logic, int playerIndex)
{
	if (const AISkirmishPlayer *ai = logic.skirmishAI().findAI(playerIndex))
	{
		return ai->build.startPosition;
	}
	const Player *p = playerOf(logic, playerIndex);
	const TerrainLogic *terrain = logic.terrain();
	Coord3D pos{ 0.0f, 0.0f, 0.0f };
	if (p && terrain)
	{
		if (const Waypoint *wp = terrain->findWaypointByName("Player_" + std::to_string(p->getMultiplayerStartIndex() + 1) + "_Start"))
		{
			pos = wp->location;
			pos.z = logic.getGroundHeight(pos.x, pos.y);
		}
	}
	return pos;
}

// RW 0x9B2B04: the 3D distance between the bases (x87 sum (dz^2 + dy^2 + dx^2), fsqrt; S-418: the CRT-equal double root of the float sum)
float enemyScore(GameLogic &logic, const AISkirmishPlayer &ai, int enemy)
{
	const Coord3D e = basePositionOf(logic, enemy);
	if (e.x == 0.0f && e.y == 0.0f && e.z == 0.0f)
	{
		return 0.0f; // RW 0xC1B594
	}
	const Coord3D &m = ai.build.startPosition;
	const float dz = SimMath::subf32(m.z, e.z), dy = SimMath::subf32(m.y, e.y), dx = SimMath::subf32(m.x, e.x);
	const float sum = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
	return (float)SimMath::sqrtd((double)sum);
}

// RW 0x9B2EB9
void initEnemies(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b)
{
	const Player *me = playerOf(logic, ai.playerIndex);
	for (int i = 0; me && i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = playerOf(logic, i);
		if (!p || p == me || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (me->getRelationship(p) == ENEMIES) // RW 0x6ACEAF == 0
		{
			b.enemies.emplace_back(i, 0.0f);
		}
	}
}

// RW 0x9B304A
void updateEnemies(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b)
{
	bool choose = b.currentEnemy == -1;
	for (size_t i = 0; i < b.enemies.size();)
	{
		const Player *p = playerOf(logic, b.enemies[i].first);
		if (!p || p->isDefeated()) // RW 0x6AAC4B (Player + 0x754)
		{
			choose = choose || b.enemies[i].first == b.currentEnemy;
			b.enemies.erase(b.enemies.begin() + (long)i);
		}
		else
		{
			++i;
		}
	}
	if (b.enemies.empty())
	{
		b.currentEnemy = -1;
		return;
	}
	if (!choose)
	{
		return;
	}
	for (auto &e : b.enemies)
	{
		e.second = enemyScore(logic, ai, e.first);
	}
	// RW 0x9B3007: ascending; below 16 entries the MSVC sort is an insertion sort (stable)
	std::stable_sort(b.enemies.begin(), b.enemies.end(), [](const std::pair<int, float> &a, const std::pair<int, float> &c) { return a.second < c.second; });
	if (b.enemies.size() < 2 || logic.random().getValue(1, 100, "AIEnemyManager.cpp", 0x7A) > 50 ||
		SimMath::subf32(b.enemies[1].second, b.enemies[0].second) > kSecondEnemyWithin)
	{
		b.currentEnemy = b.enemies[0].first;
	}
	else
	{
		b.currentEnemy = b.enemies[1].first;
	}
}

// RW 0x6C6975 (after the reset RW 0x6C63F0); `owner` is the target's + 0 (the chooser AI's player). Lane AI-3: the threat finder moves to the target (position
// and radius) and the target's threat (+ 0x28) is the enemies' total there (RW 0x6C69E6: RW 0x7EE166(out, owner, 0), the finder made with "no structures")
// (pos, radius, object): the position form; the object heuristics pass the object's position and id, the EXPANSION one a waypoint and no object (RW 0x97D3D7)
void setTargetAt(GameLogic &logic, AIBrainState &b, AITarget &t, const Coord3D &pos, ObjectID object, float radius, const Player &owner)
{
	t.frame = logic.getFrame();
	t.teams = 0;
	t.dropped = false;
	t.inactive = false;
	t.position = pos;
	t.object = object;
	t.radius = radius;
	t.finderPosition = t.position;
	t.threat = AIThreatFinder::threatTotal(logic, t.finderPosition, t.radius, true, owner, 0);
	++b.targetsSet;
}

void setTarget(GameLogic &logic, AIBrainState &b, AITarget &t, const Object &o, float radius, const Player &owner)
{
	setTargetAt(logic, b, t, *o.getPosition(), o.getID(), radius, owner);
}

// RW 0x97D425 / 0x97CC29: alive and (a fortress-like A kind, or none of the B kinds)
bool structureFilter(const Object *o, bool fortressExcluded)
{
	if (!alive(o))
	{
		return false;
	}
	const Kinds &k = kinds();
	if (!fortressExcluded && is(*o, k.dozerFactory))
	{
		return true;
	}
	for (int kind : { k.walkOnTopOfWall, k.defensiveWall, k.wallUpgrade, k.ignoreForVictory, k.wallGate, k.baseFoundation, k.wallHub, k.wallSegment, k.linkedToFlag })
	{
		if (is(*o, kind))
		{
			return false;
		}
	}
	return !(fortressExcluded && is(*o, k.dozerFactory));
}

// RW 0x97D4E6 (heuristic RW 0xC85684)
void heuristicNearestStructure(GameLogic &logic, const AISkirmishPlayer &ai, AIBrainState &b, AITarget &t, int enemy)
{
	const Player *me = playerOf(logic, ai.playerIndex);
	const Player *them = playerOf(logic, enemy);
	if (!me || !them)
	{
		return;
	}
	// RW 0x97D483: in the Rush phase an EASY or NORMAL AI does not take the fortress (B gets DOZER_FACTORY), else it may (A)
	const bool fortressExcluded = ai.build.phase == 0 && me->getSkirmishDifficulty() <= 1;
	const AIStatCollector &theirs = AITacticalAI::recordOf(logic, enemy); // RW 0x97D519 -> + 8 (the enemy's structures)
	std::map<ObjectID, int> counts;
	for (ObjectID unitId : AITacticalAI::recordOf(logic, ai.playerIndex).army()) // RW 0x97D502 -> + 0 (the AI's army; a dead unit counts, RW reads no status)
	{
		const Object *unit = logic.findObjectByID(unitId);
		if (!unit)
		{
			continue;
		}
		ObjectID nearest = INVALID_ID;
		float best = -1.0f; // RW 0xBD19DC
		for (ObjectID sid : theirs.structures())
		{
			const Object *s = logic.findObjectByID(sid);
			if (!s || !structureFilter(s, fortressExcluded))
			{
				continue;
			}
			const float dx = SimMath::subf32(s->getPosition()->x, unit->getPosition()->x);
			const float dy = SimMath::subf32(s->getPosition()->y, unit->getPosition()->y);
			const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
			if (best < 0.0f || d < best)
			{
				best = d;
				nearest = sid;
			}
		}
		if (nearest != INVALID_ID)
		{
			++counts[nearest];
		}
	}
	if (counts.empty())
	{
		return;
	}
	ObjectID pick = INVALID_ID;
	int most = 0;
	for (const auto &kv : counts)
	{
		if (most < kv.second)
		{
			most = kv.second;
			pick = kv.first;
		}
	}
	if (const Object *o = logic.findObjectByID(pick))
	{
		setTarget(logic, b, t, *o, ai.store->data().defaultTargetThreatRadius, *me); // manager + 0x990
	}
}

// RW 0x97CCBE (heuristic RW 0xC85568)
void heuristicDozers(GameLogic &logic, const AISkirmishPlayer &ai, AIBrainState &b, AITarget &t, int enemy)
{
	const Player *them = playerOf(logic, enemy);
	if (!them)
	{
		return;
	}
	const AIStatCollector &theirs = AITacticalAI::recordOf(logic, enemy);
	for (ObjectID sid : theirs.structures())
	{
		if (structureFilter(logic.findObjectByID(sid), false))
		{
			return;
		}
	}
	const Player *me = playerOf(logic, ai.playerIndex);
	for (ObjectID did : theirs.dozers())
	{
		if (const Object *d = logic.findObjectByID(did); d && me)
		{
			setTarget(logic, b, t, *d, ai.store->data().defaultTargetThreatRadius, *me);
		}
	}
}

// RW 0x97CE90 (OPPORTUNITY heuristic RW 0xC85584, the capture flags) is NOT ported (S-417): it walks the manager's + 0xA68 list, which RW 0x6A9D9C fills when
// an object with KindOf CAPTUREFLAG is created and which then holds objects that have since lost that KindOf (a captured flag); a scan of the current
// flags cannot reproduce it (retail needs the historical registration)

// RW 0x97CDA0 (OPPORTUNITY heuristic RW 0xC85574): in the EndGame phase, while the player lacks the upgrade "Upgrade_RingHero" (the name is the binary's,
// RW 0xC795B0), the first object of the game object list with KindOf NEUTRALGOLLUM or ONE_RING (template + 0x120 & 0x82000)
void heuristicRing(GameLogic &logic, const AISkirmishPlayer &ai, AIBrainState &b, AITarget &t)
{
	static const int gollum = KindOfTokens::indexOf("NEUTRALGOLLUM"), ring = KindOfTokens::indexOf("ONE_RING");
	const Player *me = playerOf(logic, ai.playerIndex);
	if (!me || ai.build.phase <= 1 || me->hasUpgradeComplete("Upgrade_RingHero"))
	{
		return;
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (is(*o, gollum) || is(*o, ring))
		{
			setTarget(logic, b, t, *o, ai.store->data().defaultTargetThreatRadius, *me);
			return;
		}
	}
}

// RW 0x97D035 (AITargetHeuristicExpansionBase::wantsToExpand; decomp Open-BFME-2 AITargetHeuristicExpansionBase.cpp:221, tier B same logic): the player count
// (ThePlayerList + 0x14, RW 0x6A88C5) below 19 and the heuristic's frame counter (+ 8, unsigned) above [RW 0xDEBD74] (= LOGICFRAMES_PER_SECOND * 60, set by the
// static initialiser at RW 0xBC9439), else the counter + 1 and no; then the counter is 0, the player's money (+ 0x94, unsigned) at least 3000, the AI's game
// phase (AISkirmishPlayer + 0x16C, the record RW 0x6A950B) at least 1, and GetGameLogicRandomValueReal(0, 1) (file line 0x3F) <= 0.5 (RW 0xC85594)
bool wantsToExpand(GameLogic &logic, const AISkirmishPlayer &ai, const Player &me)
{
	std::uint32_t &frames = logic.skirmishAI().expansionHeuristicFrames();
	if (!(logic.players().getPlayerCount() < 0x13) || frames <= (std::uint32_t)(kLogicFramesPerSecond * 60))
	{
		++frames;
		return false;
	}
	frames = 0;
	if (me.getMoney()->countMoney() < 3000u)
	{
		return false;
	}
	if (ai.build.phase < 1)
	{
		return false;
	}
	return logic.random().getValueReal(0.0f, 1.0f, "AITargetHeuristicExpansionBase.cpp", 0x3F) <= 0.5f;
}

// RW 0x97D191 (EXPANSION heuristic RW 0xC85598, ctor RW 0x97D00D; decomp AITargetHeuristicExpansionBase.cpp:256): when RW 0x97D035 says so, of the waypoints
// "Player_<i + 1>_Start" for i = 0 .. 19 (i equal to the player's index (Player + 0x54) skipped, the first missing one ends the walk), the one nearest the AI's
// base (RW 0x8F0284; squared 2D, SSE) that is nearer than the best so far (-1 = none, RW 0xBD19DC) and round which the scan ThePartitionManager->
// iterateObjectsInRange(waypoint, R, FROM_CENTER_2D, { the player's ENEMIES or ALLIES by the object's team (RW 0xC1676C, mask 6), alive (RW 0xC10E20) })
// finds no object that the player has as ALLIES (RW 0x6ADC43: its controlling player; none NEUTRAL) or whose template lacks ECONOMY_STRUCTURE (+ 0x120 bit
// 0x10, KindOf 196); a best distance above 0 sets the target there with radius R and no object (RW 0x6C6975). R = DefaultTargetThreatRadius (manager + 0x990)
void heuristicExpansion(GameLogic &logic, const AISkirmishPlayer &ai, AIBrainState &b, AITarget &t)
{
	Player *me = playerOf(logic, ai.playerIndex);
	if (!me || !wantsToExpand(logic, ai, *me))
	{
		return;
	}
	const TerrainLogic *terrain = logic.terrain();
	const float radius = ai.store->data().defaultTargetThreatRadius;
	const Coord3D &base = ai.build.startPosition;
	Coord3D best{ 0.0f, 0.0f, 0.0f };
	float bestD = -1.0f;
	static const int economy = KindOfTokens::indexOf("ECONOMY_STRUCTURE");
	for (int i = 0; i < 20 && terrain; ++i)
	{
		if (i == me->getPlayerIndex())
		{
			continue;
		}
		const Waypoint *wp = terrain->findWaypointByName("Player_" + std::to_string(i + 1) + "_Start");
		if (!wp)
		{
			break;
		}
		const Coord3D &pos = wp->location;
		const float dx = SimMath::subf32(pos.x, base.x), dy = SimMath::subf32(pos.y, base.y);
		const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (!(bestD < 0.0f || d < bestD))
		{
			continue;
		}
		PartitionFilterFn relation([&](Object &c) {
			const Relationship rel = me->getRelationship(c.getTeam());
			return rel == ENEMIES || rel == ALLIES;
		});
		PartitionFilterFn living([](Object &c) { return !c.isEffectivelyDead(); });
		const PartitionHits hits = logic.partition().iterateObjectsInRange(pos, radius, FROM_CENTER_2D, { &living, &relation }, ITER_SORTED_NEAR_TO_FAR);
		int count = 0;
		for (const PartitionHit &hit : hits)
		{
			const Object &o = *hit.object;
			if (me->getRelationship(o.getControllingPlayer()) == ALLIES || !is(o, economy))
			{
				++count;
			}
		}
		if (count == 0)
		{
			best = pos;
			bestD = d;
		}
	}
	if (0.0f < bestD)
	{
		setTargetAt(logic, b, t, best, INVALID_ID, radius, *me);
	}
}

// RW 0x90B1F4
bool needsChoice(GameLogic &logic, const AISkirmishPlayer &ai, AITarget &t)
{
	if (t.dropped || t.inactive)
	{
		return true;
	}
	if (t.object != INVALID_ID && !logic.findObjectByID(t.object))
	{
		return true;
	}
	const float elapsed = (float)(int)(logic.getFrame() - t.frame);
	if (SimMath::mulf32((float)kLogicFramesPerSecond, ai.army->secondsTillTargetsCanExpire) <= elapsed)
	{
		const float r = logic.random().getValueReal(0.0f, 1.0f, "AITargetChooser.cpp", 0x81);
		if (ai.army->chanceForTargetToExpire <= r)
		{
			return true;
		}
		t.frame = logic.getFrame();
	}
	return false;
}

// RW 0x90B4E7 + 0x9B2EB9 (the chooser's init, RW 0x90B89A)
void initChooser(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b)
{
	b.chooserInitialised = true;
	initEnemies(logic, ai, b);
	const ArmyDefinition &army = *ai.army;
	for (size_t i = 0; i < army.tacticalAITargets.size(); ++i)
	{
		AITarget t;
		t.type = army.tacticalAITargets[i];
		t.maxTeams = i < army.maxTeamsPerTarget.size() ? army.maxTeamsPerTarget[i] : 1;
		t.id = b.nextTargetId++;
		b.targets.push_back(t);
	}
}

// RW 0x90B5A9
void updateChooser(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b)
{
	for (AITarget &t : b.targets)
	{
		if (!needsChoice(logic, ai, t))
		{
			continue;
		}
		if (t.teams >= 1)
		{
			t.dropped = true;
			continue;
		}
		if (b.currentEnemy == -1)
		{
			continue;
		}
		std::vector<int> heuristics;
		for (int i = 0; i < (int)(sizeof(kHeuristicTypes) / sizeof(kHeuristicTypes[0])); ++i)
		{
			if (kHeuristicTypes[i] == t.type)
			{
				heuristics.push_back(i);
			}
		}
		if (heuristics.empty())
		{
			continue;
		}
		const int h = heuristics[(size_t)logic.random().getValue(0, (int)heuristics.size() - 1, "AITargetChooser.cpp", 0xB8)];
		if (h == 1)
		{
			heuristicNearestStructure(logic, ai, b, t, b.currentEnemy);
		}
		else if (h == 5)
		{
			heuristicDozers(logic, ai, b, t, b.currentEnemy);
		}
		else if (h == 3)
		{
			++b.unportedTypes; // the capture flag rule RW 0x97CE90: S-417
		}
		else if (h == 4)
		{
			heuristicRing(logic, ai, b, t);
		}
		else if (h == 2)
		{
			heuristicExpansion(logic, ai, b, t);
		}
		else
		{
			++b.unportedTypes; // DEFENSIVE (RW 0x97D74F): S-417
		}
	}
}

// RW 0x6C6F01 (lane AI-3): a new attacked-at record at `pos` with radius 300 (RW 0xC16764): the manager's running id (RW 0xDE4A08, incremented first), the frame,
// and the scan ThePartitionManager->iterateObjectsInRange(pos, 300, FROM_CENTER_2D, { the player's ENEMIES or ALLIES by the object's team (RW 0xC1676C, mask 6,
// flag 1), alive (RW 0xC10E20) }, ITER_FASTEST): an object with any of CAN_ATTACK, HERO, SUPPORT adds its threat value (RW 0x68F0EC, stored) to the enemies'
// record (+ 0x1C) when the player's relationship to its player is ENEMIES (RW 0x6ADC43), else to the other record (+ 0x64), with its category (RW 0x6C7019 ..)
AIAttackedAt makeAttackedAt(GameLogic &logic, const Player &player, const Coord3D &pos)
{
	const Kinds &k = kinds();
	AIAttackedAt r;
	r.id = ++logic.skirmishAI().attackedAtIds();
	r.position = pos;
	r.radius = kAttackedAtRadius;
	r.frame = logic.getFrame();
	PartitionFilterFn relation([&](Object &c) {
		const Relationship rel = player.getRelationship(c.getTeam());
		return rel == ENEMIES || rel == ALLIES;
	});
	PartitionFilterFn alive([](Object &c) { return !c.isEffectivelyDead(); });
	const PartitionHits hits = logic.partition().iterateObjectsInRange(pos, r.radius, FROM_CENTER_2D, { &relation, &alive }, ITER_FASTEST);
	for (const PartitionHit &hit : hits)
	{
		const Object &o = *hit.object;
		if (!(is(o, k.canAttack) || is(o, k.hero) || is(o, k.support)) || !o.getTemplate())
		{
			continue;
		}
		const float v = AIThreatFinder::threatValue(o);
		const Player *op = o.getControllingPlayer();
		const bool enemy = op && player.getRelationship(op) == ENEMIES; // RW 0x6ADC43: no player answers NEUTRAL
		AIThreatFinder::accumulate(enemy ? r.enemies : r.others, AIThreatFinder::threatCategory(*o.getTemplate()), v);
	}
	return r;
}

// RW 0x6C720A(pos, id): with 20 records the oldest (the first) goes; the record nearest to `pos` (squared 2D, dy^2 + dx^2, the first of equal ones; -1 as "none",
// RW 0xBD19DC) within 600 (below 360000, RW 0xC16784) is removed with every record of its id (RW 0x6C6D76) and the call repeats with that id; else a new record
// (RW 0x6C6F01) takes `id` unless it is -1, and joins the end of the list
void addAttackedAt(GameLogic &logic, AIBrainState &b, const Player &player, const Coord3D &pos, std::int64_t id)
{
	if (b.attackedAt.size() > 0x13)
	{
		b.attackedAt.erase(b.attackedAt.begin());
	}
	if (!b.attackedAt.empty())
	{
		std::int64_t best = -1;
		float bestD = -1.0f;
		for (const AIAttackedAt &e : b.attackedAt)
		{
			const float dy = SimMath::subf32(e.position.y, pos.y), dx = SimMath::subf32(e.position.x, pos.x);
			const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
			if (d < bestD || bestD < 0.0f)
			{
				best = e.id;
				bestD = d;
			}
		}
		if (bestD < kAttackedAtMergeSq && bestD != -1.0f)
		{
			if (best != -1)
			{
				b.attackedAt.erase(std::remove_if(b.attackedAt.begin(), b.attackedAt.end(), [&](const AIAttackedAt &e) { return (std::int64_t)e.id == best; }),
					b.attackedAt.end());
				addAttackedAt(logic, b, player, pos, best);
			}
			return;
		}
	}
	AIAttackedAt r = makeAttackedAt(logic, player, pos);
	if (id != -1)
	{
		r.id = (std::uint32_t)id;
	}
	b.attackedAt.push_back(r);
}

// RW 0x6C7344 (lane AI-3, the brain's first step): records older than LOGICFRAMES_PER_SECOND * 4 frames (RW 0xDE4A0C, set at RW 0xBC362D) go; then for each of the
// player's teams other than its default team (S-1302: the port's tactic teams, the offensive tactics before the targetless ones in list order, each by team index),
// the first member (team order) with any of CAN_ATTACK, HERO, SUPPORT whose AI's goal object is not a STRUCTURE, not of the ThreatBreakdown category 4 (CREEP)
// and within 300 (squared 2D <= 90000, RW 0xC16768) adds an attacked-at record at the goal's position (RW 0x6C720A(pos, -1))
void updateAttackedAt(GameLogic &logic, AISkirmishPlayer &ai)
{
	AIBrainState &b = ai.brain;
	const std::uint32_t frame = logic.getFrame();
	b.attackedAt.erase(std::remove_if(b.attackedAt.begin(), b.attackedAt.end(),
		[&](const AIAttackedAt &e) { return !(frame - e.frame < (std::uint32_t)(kLogicFramesPerSecond * 4)); }), b.attackedAt.end());
	const Player *player = playerOf(logic, ai.playerIndex);
	if (!player)
	{
		return;
	}
	const Kinds &k = kinds();
	for (int list : kTacticLists)
	{
		for (size_t ti = 0; ti < b.tactics.size(); ++ti)
		{
			if (tacticList(b.tactics[ti]) != list)
			{
				continue;
			}
			for (size_t team = 0; team < b.tactics[ti].teams.size(); ++team)
			{
				const std::vector<ObjectID> &members = b.tactics[ti].teams[team].members;
				for (ObjectID id : members)
				{
					const Object *m = logic.findObjectByID(id);
					if (!m || !(is(*m, k.canAttack) || is(*m, k.hero) || is(*m, k.support)))
					{
						continue;
					}
					const AIUpdateInterface *u = m->getAIUpdateInterface();
					const Object *goal = u && u->currentVictimId() != INVALID_ID ? logic.findObjectByID(u->currentVictimId()) : nullptr;
					if (!goal || is(*goal, k.structure) || !goal->getTemplate() || AIThreatFinder::threatCategory(*goal->getTemplate()) == 4)
					{
						continue;
					}
					const float dy = SimMath::subf32(goal->getPosition()->y, m->getPosition()->y), dx = SimMath::subf32(goal->getPosition()->x, m->getPosition()->x);
					if (SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)) <= kAttackedAtNearSq)
					{
						addAttackedAt(logic, b, *player, *goal->getPosition(), -1);
						break;
					}
				}
			}
		}
	}
}

// RW 0x6C6743 (this = the target): the owner's attacked-at record with the largest enemies' total (strictly above the running maximum from 0) when that total is
// above the target's threat (+ 0x28), else the target finder's scan of the owner's ENEMIES (RW 0x6C67AB; S-1302: a scan the finder already made this frame is
// taken as equal to a new one)
AIThreatFinder::ThreatRecord enemyRecord(GameLogic &logic, const AISkirmishPlayer &chooser, const Player &owner, const AITarget &t)
{
	float most = 0.0f;
	const AIAttackedAt *pick = nullptr;
	for (const AIAttackedAt &e : chooser.brain.attackedAt)
	{
		if (e.enemies.v[0] > most)
		{
			most = e.enemies.v[0];
			pick = &e;
		}
	}
	return pick && most > t.threat ? pick->enemies : AIThreatFinder::scanSum(logic, t.finderPosition, t.radius, true, owner, 0);
}

// RW 0x90B2A0 (lane AI-3): dropped or inactive refuses; DEFENSIVE accepts; else the enemy record RW 0x6C6743 (the chooser's attacked-at record with the largest
// enemies' total (strictly above the running maximum from 0) when that total is above the target's threat (+ 0x28), else the target finder's scan of the owner's
// ENEMIES) against the chooser player's idle army record (RW 0x99E63B): RW 0x7EDC2F(this = the enemy record, the army record); a value not above 1000
// (RW 0xBD4388) accepts, else GetGameLogicRandomValue(0, 450) (AITargetChooser.cpp 0xF7) accepts only at 0
bool targetAcceptable(GameLogic &logic, AISkirmishPlayer &chooser, const AITarget &t)
{
	AIBrainState &b = chooser.brain;
	if (t.dropped || t.inactive)
	{
		return false;
	}
	if (t.type == kDefensive)
	{
		return true;
	}
	const Player *owner = playerOf(logic, chooser.playerIndex);
	if (!owner)
	{
		return false;
	}
	AIThreatFinder::ThreatRecord enemy = enemyRecord(logic, chooser, *owner, t);
	const double value = AIThreatFinder::counterDifference(enemy, b.idleArmy, nullptr, nullptr);
	if (1000.0 >= value)
	{
		return true;
	}
	++b.gateDraws;
	if (logic.random().getValue(0, 450, "AITargetChooser.cpp", 0xF7) != 0)
	{
		++b.gateRejects;
		return false;
	}
	return true;
}

// RW 0x90B342: RW 0x90B2A0 is asked first for every target of both passes: the first acceptable target with no team and a positive maximum, else the first
// acceptable one below its maximum
int bestTarget(GameLogic &logic, AISkirmishPlayer &chooser)
{
	AIBrainState &b = chooser.brain;
	for (size_t i = 0; i < b.targets.size(); ++i)
	{
		if (targetAcceptable(logic, chooser, b.targets[i]) && b.targets[i].teams == 0 && b.targets[i].maxTeams > 0)
		{
			return (int)i;
		}
	}
	for (size_t i = 0; i < b.targets.size(); ++i)
	{
		if (targetAcceptable(logic, chooser, b.targets[i]) && b.targets[i].teams < b.targets[i].maxTeams)
		{
			return (int)i;
		}
	}
	return -1;
}

// RW 0x993373
bool offensiveAllowed(GameLogic &logic, const AISkirmishPlayer &ai, const Player &player)
{
	const int d = player.getSkirmishDifficulty() < 0 ? 0 : player.getSkirmishDifficulty();
	if (d == 0 && ai.build.phase == 0)
	{
		return false;
	}
	const AIProbability &p = ai.store->data().difficultyTuning[d < SkirmishAI::DIFFICULTY_COUNT ? d : SkirmishAI::DIFFICULTY_COUNT - 1].offensiveTactic;
	if (1.0f <= SimMath::divf32((float)p.numerator, (float)p.denominator)) // RW 0xBD1908
	{
		return true;
	}
	return logic.random().getValue(0, p.denominator - 1, "AIDifficulty.cpp", 0x5B) < p.numerator;
}

bool inAnyTeamOf(const AIBrainState &b, ObjectID id)
{
	for (const AITactic &tac : b.tactics)
	{
		for (const AITacticTeam &team : tac.teams)
		{
			if (std::find(team.members.begin(), team.members.end(), id) != team.members.end())
			{
				return true;
			}
		}
	}
	return false;
}

// the offensive prototypes of the generator in RW 0x90BE31's order
enum OffensiveKind
{
	SIMPLE_ATTACK,      // RW 0x9B4E2B "SimpleAttack" (vtable RW 0xC89848)
	FORMATION_ATTACK,   // RW 0x9B4B3F "FormationAttack" (RW 0xC89800)
	FLANK_ATTACK,       // RW 0x9B4582 "FlankAttack" (RW 0xC89720)
	PINCER_ATTACK,      // RW 0x9B3F8A "PincerAttack" (RW 0xC89634)
	FEINT_ATTACK,       // RW 0x9B3884 "FeintAttack" (RW 0xC8954C)
	BASE_PENETRATION,   // RW 0x9B36E5 "AIBasePenetrationTroopsTactic" (RW 0xC894F4)
	SIMPLE_SIEGE,       // RW 0x9B33B2 "SimpleSiege" (RW 0xC894B0)
	SIEGE_GATES,        // RW 0x9B3282 "SiegeGates" (RW 0xC89460)
	// lane AIEXPAND-1: the generator's one EXPANSION prototype (+ 0x34, made by RW 0x90C09E), not an offensive one
	SIMPLE_EXPANSION    // RW 0x9B5190 "SimpleExpansion" (RW 0xC898D4)
};
constexpr OffensiveKind kOffensivePrototypes[] = { SIMPLE_ATTACK, SIMPLE_ATTACK, FORMATION_ATTACK, FORMATION_ATTACK, FLANK_ATTACK, PINCER_ATTACK, FEINT_ATTACK,
	BASE_PENETRATION, SIMPLE_SIEGE, SIEGE_GATES };
const char *const kOffensiveNames[] = { "SimpleAttack", "FormationAttack", "FlankAttack", "PincerAttack", "FeintAttack", "AIBasePenetrationTroopsTactic",
	"SimpleSiege", "SiegeGates", "SimpleExpansion" };
const char *const kExpansionBuilding = "MenFortress"; // RW 0x9B51E9: the literal RW 0xC899B4, whatever the AI's side
constexpr float kExpansionPriority = 3000.0f;       // RW 0x9B51E9: + 4 = [RW 0xC1EE14]
static_assert(SIMPLE_EXPANSION == kSimpleExpansionIndex, "AITactic::kindIndex of SimpleExpansion");
constexpr float kFlankMinDistanceSq = 2000000.0f; // RW 0xC89630

// RW 0x9EF9AF (AITactic's common check): a unit of the AI's army list (CAN_ATTACK or SUPPORT, not IMMOBILE, not IGNORES_SELECT_ALL) that can path to the target
// (S-418: taken as reachable); the tactic's vslot 0x34 (true for the siege tactics RW 0x8BD372, false for the others RW 0x9188EB) equal to that answer refuses;
// an EASY AI in the Rush phase refuses a living DOZER_FACTORY target
bool commonApplies(GameLogic &logic, const AISkirmishPlayer &ai, const AIBrainState &b, const AITarget &t, const Player &player, bool siegeTactic)
{
	const Kinds &k = kinds();
	bool reachable = false;
	const Object *last = nullptr;
	for (ObjectID id : AITacticalAI::recordOf(logic, ai.playerIndex).army())
	{
		const Object *o = logic.findObjectByID(id);
		last = o;
		if (o && (is(*o, k.canAttack) || is(*o, k.support)) && !is(*o, k.immobile) && !is(*o, k.ignoresSelectAll))
		{
			reachable = true; // RW 0x6F5BB0 / 0x6F5F65 (S-418)
			break;
		}
	}
	if (last && siegeTactic == reachable)
	{
		return false;
	}
	const Object *target = t.object != INVALID_ID ? logic.findObjectByID(t.object) : nullptr;
	if (ai.build.phase == 0 && player.getSkirmishDifficulty() == 0 && target && is(*target, k.dozerFactory) && alive(target))
	{
		return false;
	}
	return true;
}

// RW 0x9B3734: a living SIEGEENGINE of the AI's army list that is still in the player's own team (not in a tactic team) and not IGNORES_SELECT_ALL
bool hasSiegeEngine(GameLogic &logic, const AISkirmishPlayer &ai, const AIBrainState &b)
{
	static const int siege = KindOfTokens::indexOf("SIEGEENGINE");
	const Kinds &k = kinds();
	for (ObjectID id : AITacticalAI::recordOf(logic, ai.playerIndex).army())
	{
		const Object *o = logic.findObjectByID(id);
		if (o && is(*o, siege) && alive(o) && !is(*o, k.ignoresSelectAll) && !inAnyTeamOf(b, id))
		{
			return true;
		}
	}
	return false;
}

// the base-to-target test of FlankAttack / PincerAttack: the target object's owner's Player_N_Start waypoint (RW 0x90CE35 + 0x628C29; its + 0x60 == 5 refuses:
// the field is not identified, S-418: taken as not 5), then the squared 3D distance from the AI's base (RW 0x8F0284) to the target below 2,000,000 refuses
bool farEnough(const AISkirmishPlayer &ai, const AITarget &t)
{
	const Coord3D &m = ai.build.startPosition;
	const float dx = SimMath::subf32(m.x, t.position.x), dy = SimMath::subf32(m.y, t.position.y), dz = SimMath::subf32(m.z, t.position.z);
	const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
	return !(kFlankMinDistanceSq > d);
}

// vslot 1 of each offensive prototype
bool tacticApplies(GameLogic &logic, OffensiveKind kind, const AISkirmishPlayer &ai, const AIBrainState &b, const AITarget &t, const Player &player)
{
	const Kinds &k = kinds();
	const Object *target = t.object != INVALID_ID ? logic.findObjectByID(t.object) : nullptr;
	const bool enemyKnown = ai.groupFirst ? ai.groupFirst->brain.currentEnemy >= 0 : b.currentEnemy >= 0; // RW 0x6C7807
	switch (kind)
	{
	case SIMPLE_ATTACK:
	case FORMATION_ATTACK:
		return commonApplies(logic, ai, b, t, player, false); // RW 0x9B49BD
	case FLANK_ATTACK: // RW 0x9B43E9: a target object must belong to a human player (RW 0x68B68A: Player + 0x5C == 0)
		if (target && (!target->getControllingPlayer() || target->getControllingPlayer()->getPlayerType() != PLAYER_HUMAN))
		{
			return false;
		}
		return farEnough(ai, t) && commonApplies(logic, ai, b, t, player, false);
	case PINCER_ATTACK: // RW 0x9B3D8B: not a LINKED_TO_FLAG target
		if (target && is(*target, k.linkedToFlag))
		{
			return false;
		}
		return farEnough(ai, t) && commonApplies(logic, ai, b, t, player, false);
	case FEINT_ATTACK: // RW 0x9B3849
		return commonApplies(logic, ai, b, t, player, false) && t.type != kOpportunity;
	case BASE_PENETRATION: // RW 0x9B37A7
		return commonApplies(logic, ai, b, t, player, false) && ai.build.phase >= 1 && target && is(*target, k.dozerFactory) && alive(target) &&
			hasSiegeEngine(logic, ai, b);
	case SIMPLE_SIEGE: // RW 0x9B333D
		return enemyKnown && commonApplies(logic, ai, b, t, player, true) && t.type == kEnemyStructure && hasSiegeEngine(logic, ai, b);
	case SIEGE_GATES: // RW 0x9B313C
		return enemyKnown && commonApplies(logic, ai, b, t, player, true) && t.type == kEnemyStructure && !hasSiegeEngine(logic, ai, b);
	case SIMPLE_EXPANSION: // RW 0x9B501B: mov al, 1
		return true;
	}
	return false;
}

// vslot 4: the number of teams (RW 0x490AC4 = 1, RW 0x64DFDA = 2)
int teamCountOf(OffensiveKind kind)
{
	return kind == PINCER_ATTACK || kind == FEINT_ATTACK || kind == SIMPLE_SIEGE ? 2 : 1;
}

// a KindOf bit by its binary name (TheKindOfNames RW 0xDA0E68); the setups below cite the byte and bit RW sets
int kindBit(const char *name)
{
	const int bit = KindOfTokens::indexOf(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("AITacticalAI: no KindOf named ") + name);
	}
	return bit;
}

// vslot 3: a team's sizes (+ 0x2D4 minimum, + 0x2D0 maximum, -1 = no maximum) and its KindOf masks (lane DECOMP-2: + 0x2E0 required, + 0x2FC forbidden, the
// masks the recruit test RW 0x9A24FC applies)
void setupTeam(GameLogic &logic, OffensiveKind kind, int index, const AITarget &t, AITacticTeam &team)
{
	switch (kind)
	{
	case SIMPLE_ATTACK:
	case FLANK_ATTACK:
		if (t.type == kEnemyStructure)
		{
			team.minimum = logic.random().getValue(3, 5, "AITactic.cpp", 0x1C7); // RW 0x8F121F
		}
		break;
	case FORMATION_ATTACK: // RW 0x9B49CE
		team.minimum = 2;
		team.maximum = 6;
		break;
	case PINCER_ATTACK: // RW 0x9B3CBE (both teams)
		team.minimum = 2;
		team.maximum = 4;
		break;
	case FEINT_ATTACK: // RW 0x9B38DB
		if (index == 0)
		{
			team.forbiddenKinds = { kindBit("CAVALRY"), kindBit("DOZER") }; // RW 0x9B38EF: + 0x2FD |= 0x42 (bits 9, 14)
			team.minimum = 3;
			team.maximum = 6;
		}
		else
		{
			team.forbiddenKinds = { kindBit("MACHINE"), kindBit("HERO"), kindBit("DOZER") }; // RW 0x9B3919 .. 0x9B3921: + 0x2FD |= 8, + 0x307 |= 4, + 0x2FD |= 0x40
			team.minimum = 1;
			team.maximum = 2;
		}
		break;
	case BASE_PENETRATION: // RW 0x9B3684
		team.minimum = 6;
		team.maximum = 10;
		break;
	case SIMPLE_SIEGE: // RW 0x9B35EF
		if (index == 0)
		{
			team.requiredKinds = { kindBit("SIEGEENGINE") }; // RW 0x9B35FA: + 0x2FB |= 0x10 (bit 220)
			team.minimum = 1;
			team.maximum = 2;
		}
		else
		{
			team.minimum = 3;
			team.maximum = 6;
		}
		break;
	case SIEGE_GATES: // RW 0x9B3307
		team.forbiddenKinds = { kindBit("DOZER"), kindBit("ARCHER") }; // RW 0x9B3311 / 0x9B3315: + 0x30C |= 0x20 (bit 133), + 0x2FD |= 0x40 (bit 14)
		team.minimum = 5;
		team.maximum = 8;
		break;
	case SIMPLE_EXPANSION: // RW 0x9B5081: + 0x2D0 = GetGameLogicRandomValue(2, 2) (AISimpleExpansionTactic.cpp line 0xA0); the minimum stays the pool's 1
		team.maximum = logic.random().getValue(2, 2, "AISimpleExpansionTactic.cpp", 0xA0);
		break;
	}
}

const char *targetTypeName(int type)
{
	return type >= 0 && type < SkirmishAI::AI_TARGET_COUNT ? SkirmishAI::TheAITargetNames[type] : "INVALID";
}

// RW 0x8F2006 (the parts ported)
void endTactic(AITactic &tac, AIBrainState &owner, bool success)
{
	if (tac.ended)
	{
		return;
	}
	tac.ended = true;
	tac.success = success;
	if (tac.targetIndex >= 0 && tac.targetIndex < (int)owner.targets.size())
	{
		AITarget &t = owner.targets[(size_t)tac.targetIndex];
		if (success)
		{
			t.inactive = true; // RW 0x6C6380(1)
			t.dropped = true;
		}
		t.teams -= 1;
	}
	tac.targetIndex = -1;
	tac.started = false;
	for (AITacticTeam &team : tac.teams)
	{
		// RW 0x7A12F4: the team is released where it stands (the merge into another team, RW 0x8F1D88, ran before: mergeTeams)
		team.members.clear();
	}
}

// RW 0x8F21F7
void startTactic(GameLogic &logic, AITactic &tac, AIBrainState &owner, int targetIndex)
{
	AITarget &t = owner.targets[(size_t)targetIndex];
	if (t.teams >= t.maxTeams)
	{
		tac.ended = true;
		return;
	}
	tac.targetIndex = targetIndex;
	const OffensiveKind kind = (OffensiveKind)tac.kindIndex;
	const int teamCount = teamCountOf(kind);
	for (int i = 0; i < teamCount; ++i)
	{
		AITacticTeam team;
		team.name = std::string(targetTypeName(t.type)) + "_" + tac.kind + "_" + std::to_string(tac.id) + "_" + std::to_string(i); // "%s_%s_%u_%u"
		team.type = t.type;
		team.index = i;
		setupTeam(logic, kind, i, t, team);
		tac.teams.push_back(team);
	}
	t.teams += 1;
}

// RW 0x90C349
void enemyStructureTactic(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AIBrainState &owner, int targetIndex, int ownerIndex, const Player &player)
{
	if (!offensiveAllowed(logic, ai, player))
	{
		++b.gateRefusals;
		return;
	}
	const AITarget &t = owner.targets[(size_t)targetIndex];
	std::vector<OffensiveKind> applicable;
	for (OffensiveKind kind : kOffensivePrototypes)
	{
		if (tacticApplies(logic, kind, ai, b, t, player))
		{
			applicable.push_back(kind);
		}
	}
	if (applicable.empty())
	{
		return;
	}
	const OffensiveKind kind = applicable[(size_t)logic.random().getValue(0, (int)applicable.size() - 1, "AITacticsGenerator.cpp", 0x158)];
	AITactic tac;
	tac.kindIndex = (int)kind;
	tac.kind = kOffensiveNames[kind];
	tac.id = b.nextTacticId++;
	tac.targetOwner = ownerIndex;
	startTactic(logic, tac, owner, targetIndex);
	++b.tacticsByKind[tac.kind];
	b.tactics.push_back(tac);
	++b.tacticsStarted;
}

// RW 0x90C50C (lane AIEXPAND-1): the EXPANSION prototypes of the generator (+ 0x34: one SimpleExpansion, RW 0x90C09E) that apply (vslot 1) are collected (no
// difficulty gate); GetGameLogicRandomValue(0, n - 1) (AITacticsGenerator.cpp line 0x196) picks one; its clone (vslot 0x30, RW 0x9B528B) starts on the target
// (RW 0x8F21F7) and joins the active list
void expansionTactic(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AIBrainState &owner, int targetIndex, int ownerIndex, const Player &player)
{
	const AITarget &t = owner.targets[(size_t)targetIndex];
	std::vector<OffensiveKind> applicable;
	for (OffensiveKind kind : { SIMPLE_EXPANSION })
	{
		if (tacticApplies(logic, kind, ai, b, t, player))
		{
			applicable.push_back(kind);
		}
	}
	if (applicable.empty())
	{
		return;
	}
	const OffensiveKind kind = applicable[(size_t)logic.random().getValue(0, (int)applicable.size() - 1, "AITacticsGenerator.cpp", 0x196)];
	AITactic tac;
	tac.kindIndex = (int)kind;
	tac.kind = kOffensiveNames[kind];
	tac.id = b.nextTacticId++;
	tac.targetOwner = ownerIndex;
	startTactic(logic, tac, owner, targetIndex);
	++b.tacticsByKind[tac.kind];
	b.tactics.push_back(tac);
	++b.tacticsStarted;
}

// RW 0x8F17EE: the team attack-moves to the target (AIGroup::groupAttackMoveToPosition(pos, 0x7FFFFFFF, CMD_FROM_PLAYER), lane COMBAT-1's attack-move)
void attackMove(GameLogic &logic, const std::vector<ObjectID> &members, const Coord3D &pos, CommandSourceType source = CMD_FROM_PLAYER)
{
	AIGroup group(logic, members);
	group.groupMoveToPosition(pos, false, source, false, 0.0f, true); // lane BACKLOG-8 r2: no formation march for an attack-move (its attack march is S-4463)
	for (Object *o : group.members())
	{
		if (AIUpdateInterface *u = o->getAIUpdateInterface())
		{
			u->armAttackMove(u->stateMachine().goalPosition(), source);
		}
	}
}

bool teamAlive(GameLogic &logic, const AITacticTeam &team)
{
	for (ObjectID id : team.members)
	{
		if (alive(logic.findObjectByID(id)))
		{
			return true;
		}
	}
	return false;
}

// ---- lane AI-2 r3: the targetless tactics (generator + 0x4C, made by RW 0x90C0EE; the pick RW 0x90C5E2) --------------------------------------------------------------
constexpr int kTargetlessType = 4; // AITARGET TARGETLESS (TheAITargetNames RW 0xDA1208): the team type RW 0x8F2357 stores (+ 0x2CC = 4)
constexpr int kTargetlessBase = 100; // port only: AITactic::kindIndex of targetless prototype i is kTargetlessBase + i
// RW 0x90C0EE's order; vslot 0x34 true = "always" (RW 0x8BD372: ReturnTheRing, AIWoTRForfeitTactic), false = the pool one is drawn from
const char *const kTargetlessNames[] = { "FlagCaptureSquad", "FarmKillSquad", "AIRingHeroTactic", "StructureCreep", "LumberMillBuild", "ReturnTheRing",
	"AIStartWoTRBattleTactic", "AIRoamingDefenseTactic", "AIWoTRForfeitTactic", "WallTactic", "FarmBuilder" };
constexpr bool kTargetlessAlways[] = { false, false, false, false, false, true, false, false, true, false, false };
constexpr int kTargetlessCount = 11;
constexpr int TL_FLAG_CAPTURE = 0;
constexpr int TL_FARM_KILL = 1;
const char *const kFlagCaptureFile = "AIFlagCaptureSquad.cpp";
const char *const kFlagCaptureRunning = "AIFlagCaptureSquad_IsRunning";
const char *const kFlagCaptureNextFrame = "AIFlagCaptureSquad_FrameNextRun";
const char *const kTargetlessFile = "AITacticsGenerator.cpp";  // RW 0xC7AD48
const char *const kFarmKillFile = "AIFarmKillSquad.cpp";        // RW 0xC8A218
// the FarmKillSquad counters (RW keys DAT_00DEBEF8 / DAT_00DEBEFC, AsciiStrings whose text is set by an initialiser that is not read: the port's names)
const char *const kFarmKillRunning = "FarmKillSquad::IsRunning";
const char *const kFarmKillNextFrame = "FarmKillSquad::NextLogicFrame";
constexpr int kFarmKillDelayMinSeconds = 10, kFarmKillDelayMaxSeconds = 60; // RW 0xBC9CBE / 0xBC9CCC: DAT_00DEBF04 = FPS * 10, DAT_00DEBF08 = FPS * 60

// RW 0x6C7EDD: the named value, a missing name is stored as 0 (RW 0x6C7EC5(name, 0))
int namedCounter(AIBrainState &b, const char *name)
{
	auto it = b.namedCounters.find(name);
	if (it == b.namedCounters.end())
	{
		b.namedCounters[name] = 0;
		return 0;
	}
	return it->second;
}

int enemyOfGroup(const AISkirmishPlayer &ai)
{
	return ai.groupFirst ? ai.groupFirst->brain.currentEnemy : ai.brain.currentEnemy; // RW 0x6C7807
}

// vslot 1 (applies) of targetless prototype `index` (the prototype's + 0x24 player is the AI's: RW 0x90C623)
bool targetlessApplies(GameLogic &logic, AISkirmishPlayer &ai, int index)
{
	AIBrainState &b = ai.brain;
	if (index == TL_FLAG_CAPTURE)
	{
		// RW 0x9BC39F / 0x9BC303: one squad at a time, its cooldown due, and a
		// non-allied flag still carrying CAPTUREFLAG. Registry/relationship
		// reference: Open-BFME-2 AITargetHeuristicTechBuilding::findBestTarget.
		if (namedCounter(b, kFlagCaptureRunning) != 0 || (unsigned)namedCounter(b, kFlagCaptureNextFrame) > logic.getFrame())
		{
			return false;
		}
		const Player *me = playerOf(logic, ai.playerIndex);
		for (ObjectID id : logic.skirmishAI().captureFlags())
		{
			const Object *flag = logic.findObjectByID(id);
			if (flag && me && flag->isKindOfName("CAPTUREFLAG") && me->getRelationship(flag->getTeam()) != ALLIES)
			{
				return true;
			}
		}
		return false;
	}
	if (index == TL_FARM_KILL)
	{
		// RW 0x9BB1C4
		if (!b.farmKillEarly && !(0 < ai.build.phase))
		{
			return false;
		}
		const int running = namedCounter(b, kFarmKillRunning);
		const unsigned next = (unsigned)namedCounter(b, kFarmKillNextFrame);
		return running == 0 && next <= logic.getFrame() && enemyOfGroup(ai) >= 0;
	}
	// the other 9 prototypes' vslot 1 (AIRingHeroTactic RW 0x9BAF6C, StructureCreep RW 0x9BA3C2, LumberMillBuild RW 0x9B9E06,
	// ReturnTheRing RW 0x9B89CF, AIStartWoTRBattleTactic RW 0x9B8451, AIRoamingDefenseTactic RW 0x9B7F56, AIWoTRForfeitTactic RW 0x9B71B7, WallTactic RW 0x9B6F70,
	// FarmBuilder RW 0x9B53C6) are not ported: taken as "does not apply" and counted (S-893)
	++b.targetlessUnported;
	return false;
}

// vslot 2 of the prototype, called by the end RW 0x8F2006 unless its third argument is set
void targetlessCleanup(GameLogic &logic, AISkirmishPlayer &ai, const AITactic &tac)
{
	if (tac.kindIndex == kTargetlessBase + TL_FLAG_CAPTURE)
	{
		// RW 0x9BBFDC; startup RW 0xBC9D10 / 0xBC9D1E: 20..90 seconds, draw at
		// source line 0xD5.
		ai.brain.namedCounters[kFlagCaptureRunning] = 0;
		const int delay = logic.random().getValue(kLogicFramesPerSecond * 20, kLogicFramesPerSecond * 90, kFlagCaptureFile, 0xD5);
		ai.brain.namedCounters[kFlagCaptureNextFrame] = (int)(logic.getFrame() + (unsigned)delay);
	}
	if (tac.kindIndex == kTargetlessBase + TL_FARM_KILL)
	{
		// RW 0x9BB226: not running; the next start no earlier than frame + GetGameLogicRandomValue(FPS * 10, FPS * 60) (AIFarmKillSquad.cpp line 0x244)
		ai.brain.namedCounters[kFarmKillRunning] = 0;
		const int delay = logic.random().getValue(kLogicFramesPerSecond * kFarmKillDelayMinSeconds, kLogicFramesPerSecond * kFarmKillDelayMaxSeconds, kFarmKillFile, 0x244);
		ai.brain.namedCounters[kFarmKillNextFrame] = (int)(logic.getFrame() + (unsigned)delay);
	}
}

// RW 0x7A0ECD: the average position of the team's members (SSE sums in member order, times 1 / n)
bool teamCentre(GameLogic &logic, const AITacticTeam &team, Coord3D &out)
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	int n = 0;
	for (ObjectID id : team.members)
	{
		if (const Object *o = logic.findObjectByID(id))
		{
			x = SimMath::addf32(o->getPosition()->x, x);
			y = SimMath::addf32(o->getPosition()->y, y);
			z = SimMath::addf32(o->getPosition()->z, z);
			++n;
		}
	}
	if (n == 0)
	{
		return false; // RW divides by 0 (the team is dropped before: RW 0x8F28DD keeps only teams with a living member)
	}
	const float f = SimMath::divf32(1.0f, (float)n); // RW 0xBD1908
	out = Coord3D{ SimMath::mulf32(f, x), SimMath::mulf32(f, y), SimMath::mulf32(f, z) };
	return true;
}

// RW 0x7A03AB: a member whose AI has a current victim (AIUpdate + 0x40 through getCurrentVictim RW 0x668303). TARGET (lane DECOMP-2, closes S-891): + 0x40 is written only
// by setCurrentVictim RW 0x6682B1 (the victim's id, Object + 0x74; null clears it after the old victim's AI drops this object as a targeter, AI vslot 0x204), called by the
// attack states (RW 0x74CED6 onEnter, 0x74D25D onExit, 0x74AD8E pursue, 0x749D46 approach, 0x7520D5 fire during approach, ...) and AIIdleState (RW 0x745A64); donor: BFME2
// decomp AIUpdateInterface_setCurrentVictim.cpp / _getCurrentVictim.cpp (tier B same-shape / tier A for RW 0x6682B1 / 0x668303), ZH AIUpdate.cpp setCurrentVictim
bool teamEngaged(GameLogic &logic, const AITacticTeam &team)
{
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		const AIUpdateInterface *u = o ? o->getAIUpdateInterface() : nullptr;
		if (u && u->currentVictimId() != INVALID_ID && logic.findObjectByID(u->currentVictimId()))
		{
			return true;
		}
	}
	return false;
}

// object + 0x27C: the HORDE that holds the object (a horde member: HORDE_MEMBER status and a container, as AIAttackMelee.cpp)
bool heldByHorde(const Object &o)
{
	return o.testStatus((unsigned)CombatNames::statuses().hordeMember) && o.getContainedBy() != nullptr;
}

// RW 0x8F13FC: object + 0x10C is the model condition flags (Object.h); bit 0x70 is CAPTURING in TheModelConditionNames (lane AI-3, pinned by a test)
constexpr int kCapturingCondition = 0x70;

bool teamCapturing(GameLogic &logic, const AITacticTeam &team)
{
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		if (o && o->testModelCondition(kCapturingCondition))
		{
			return true;
		}
	}
	return false;
}

// RW 0x8F135E: each team's idle counter: the centre moved more than TeamIdleCheckRadius (manager + 0x97C) since the stored one -> stored, 0; a member with the
// model condition 0x70 (CAPTURING, RW 0x46E918 on + 0x10C) or an engaged team -> 0; else + 1
void updateIdleCounters(GameLogic &logic, const AISkirmishPlayer &ai, AITactic &tac)
{
	const float r = ai.store->data().teamIdleCheckRadius;
	for (AITacticTeam &team : tac.teams)
	{
		Coord3D c;
		if (!teamCentre(logic, team, c))
		{
			continue;
		}
		const float dx = SimMath::subf32(c.x, team.lastPosition.x), dy = SimMath::subf32(c.y, team.lastPosition.y);
		const bool moved = SimMath::mulf32(r, r) < SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (moved)
		{
			team.lastPosition = c;
		}
		if (!moved && !teamCapturing(logic, team) && !teamEngaged(logic, team))
		{
			team.idleFrames += 1;
		}
		else
		{
			team.idleFrames = 0;
		}
	}
}

// RW 0x8F27AD: with teams (vslot 0x10 != 0) the tactic's position (+ 0x38) is the average of the team centres (x87 sums in team order, times 1 / n)
void updateTacticPosition(GameLogic &logic, AITactic &tac)
{
	std::vector<Coord3D> centres;
	for (const AITacticTeam &team : tac.teams)
	{
		Coord3D c{ 0.0f, 0.0f, 0.0f };
		teamCentre(logic, team, c);
		centres.push_back(c);
	}
	float x = 0.0f, y = 0.0f, z = 0.0f;
	for (const Coord3D &c : centres)
	{
		x = SimMath::addf32(x, c.x);
		y = SimMath::addf32(c.y, y);
		z = SimMath::addf32(c.z, z);
	}
	const float f = SimMath::divf32(1.0f, (float)(int)centres.size());
	tac.position = Coord3D{ SimMath::mulf32(f, x), SimMath::mulf32(f, y), SimMath::mulf32(f, z) };
}

// RW 0x8F175A / 0x8F1725: every team is gone or idle for TeamTimeUntilConsideredIdle seconds (manager + 0x980; (float)LOGICFRAMES_PER_SECOND * it <= counter)
bool allTeamsDone(const AISkirmishPlayer &ai, const AITactic &tac)
{
	const float limit = SimMath::mulf32((float)kLogicFramesPerSecond, ai.store->data().teamTimeUntilConsideredIdle);
	for (const AITacticTeam &team : tac.teams)
	{
		if (!(limit <= (float)team.idleFrames))
		{
			return false;
		}
	}
	return true;
}

// ---- lane AI-2 r5: the offensive tactic bodies (vslot 0x18 launch, vslot 0x1C started update) ----------------------------------------------------------------------------
// RW 0x403175 Coord3D::normalize (as WallSpan.cpp): len = (float)sqrt of the float sum of squares; unless 0, each component times 1 / len (SSE)
void normalize3(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, len);
	v.x = SimMath::mulf32(v.x, inv);
	v.y = SimMath::mulf32(v.y, inv);
	v.z = SimMath::mulf32(v.z, inv);
}

// RW 0x405406 Coord2D::toAngle (as WallSpan.cpp): acos of the clamped x / len, negated for y < 0; 0 for a zero vector (CRT acos: S-167)
float toAngle2(float x, float y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = SimMath::divf32(x, len);
	if (-1.0f > c)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const float a = (float)SimMath::acosDet((double)c);
	return 0.0f <= y ? a : -a;
}

// RW 0x8F128B / 0x8F12C3: the team whose index (+ 0x2DC) is `index`
AITacticTeam *teamByIndex(AITactic &tac, int index)
{
	for (AITacticTeam &team : tac.teams)
	{
		if (team.index == index)
		{
			return &team;
		}
	}
	return nullptr;
}

// RW 0x7A02E1: the average position of the team's living, not DESTROYED members ((0, 0, 0) without one; SSE sums in member order, times 1 / n)
Coord3D livingCentre(GameLogic &logic, const AITacticTeam &team)
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	int n = 0;
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		if (alive(o))
		{
			x = SimMath::addf32(x, o->getPosition()->x);
			y = SimMath::addf32(o->getPosition()->y, y);
			z = SimMath::addf32(o->getPosition()->z, z);
			++n;
		}
	}
	if (n > 0)
	{
		const float f = SimMath::divf32(1.0f, (float)n);
		x = SimMath::mulf32(f, x);
		y = SimMath::mulf32(f, y);
		z = SimMath::mulf32(f, z);
	}
	return Coord3D{ x, y, z };
}

// RW 0x8F17EE(index, pos): the team attack-moves (AIGroup RW 0x774E2F, S-895) and its idle counter restarts
void teamAttackMove(GameLogic &logic, AITactic &tac, int index, const Coord3D &pos)
{
	if (AITacticTeam *team = teamByIndex(tac, index))
	{
		attackMove(logic, team->members, pos);
		team->idleFrames = 0;
	}
}

// RW 0x8F1848(index, pos): an EASY player's team attack-moves (RW 0x8F17EE); any other difficulty's team makes a formation move (AIGroup RW 0x774897 with no attack,
// mode 0: lane BACKLOG-8 r2 ported its formation march, RW 0x7747DE / 0x94FCBA / 0x77348C, which the group move offers the team first); the idle counter restarts
void teamMoveFormation(GameLogic &logic, const Player &player, AITactic &tac, int index, const Coord3D &pos, bool haveAngle = false, float angle = 0.0f)
{
	AITacticTeam *team = teamByIndex(tac, index);
	if (!team)
	{
		return;
	}
	if (player.getSkirmishDifficulty() <= 0 && !haveAngle) // RW 0x6AA61B == 0
	{
		attackMove(logic, team->members, pos);
	}
	else
	{
		AIGroup group(logic, team->members);
		// RW 0x8F18AA .. 0x8F18C3: command source 0 (the player's), no formation type; FormationAttack's formation move RW 0x9B4D64 passes source 2 (the AI's;
		// lane BACKLOG6) with a formation type and the facing
		group.groupMoveToPosition(pos, false, haveAngle ? CMD_FROM_AI : CMD_FROM_PLAYER, haveAngle, angle);
	}
	team->idleFrames = 0;
}

// RW 0x8F1725(index): the team is gone or idle for TeamTimeUntilConsideredIdle seconds
bool teamDone(const AISkirmishPlayer &ai, AITactic &tac, int index)
{
	const AITacticTeam *team = teamByIndex(tac, index);
	const float limit = SimMath::mulf32((float)kLogicFramesPerSecond, ai.store->data().teamTimeUntilConsideredIdle);
	return !team || limit <= (float)team->idleFrames;
}

// RW 0x7A090C: every living member with an AI is idle
bool teamAllIdle(GameLogic &logic, const AITacticTeam &team)
{
	for (ObjectID id : team.members)
	{
		Object *o = logic.findObjectByID(id);
		AIUpdateInterface *u = o && alive(o) ? o->getAIUpdateInterface() : nullptr;
		if (u && !u->isIdle())
		{
			return false;
		}
	}
	return true;
}

const char *const kFlankFile = "AIFlankAttackTactic.cpp";   // RW 0xC89768
const char *const kPincerFile = "AIPincerAttackTactic.cpp"; // RW 0xC89680
const char *const kFeintFile = "AIFeintAttackTactic.cpp";   // RW 0xC89590
constexpr float kFlankOffsetMax = 400.0f;    // RW 0xBDBCA0
constexpr float kPincerOffsetMax = 150.0f;   // RW 0xC041F8
constexpr float kFeintOffsetMax = 200.0f;    // RW 0xBE4170
constexpr float kFeintFallbackMax = 400.0f;  // RW 0xBDBCA0
// the distance bases each draw is added to before the ftol (lane AI-2 r7, Sol review): `fadd dword ptr [base]` right after RW 0x6D332C returns, at PC24
constexpr float kFlankOffsetBase = 600.0f;   // RW 0x9B4647: fadd [0xBF24F8]
constexpr float kPincerOffsetBase = 450.0f;  // RW 0x9B406C: fadd [0xC2E44C]
constexpr float kFeintOffsetBase = 300.0f;   // RW 0x9B3992: fadd [0xBD9E90]
constexpr float kFeintFallbackBase = 600.0f; // RW 0x9B3A5F: fadd [0xBF24F8]
constexpr std::uint32_t kFeintMainDelay = 50; // RW 0x9B3BE2: frame - launch > 0x32

// ftol of a GetGameLogicRandomValueReal result (RW 0xA3CFA4 on the x87 value)
int realToInt(float r)
{
	return (int)SimMath::ftol2((double)r);
}

// RW 0x9B461D: FlankAttack's waypoints from the team centre `start` to `target`
void flankWaypoints(GameLogic &logic, AITactic &tac, const AITarget &t, const Coord3D &start, const Coord3D &target)
{
	const int d = realToInt(SimMath::pc24Add(logic.random().getValueReal(0.0f, kFlankOffsetMax, kFlankFile, 0x4E), kFlankOffsetBase));
	Coord3D dir{ SimMath::subf32(target.x, start.x), SimMath::subf32(target.y, start.y), SimMath::subf32(target.z, start.z) };
	normalize3(dir);
	const float fd = (float)d; // cvtsi2ss of a non-negative int
	dir = Coord3D{ SimMath::mulf32(dir.x, fd), SimMath::mulf32(fd, dir.y), SimMath::mulf32(dir.z, fd) };
	const Coord3D beyond{ SimMath::addf32(target.x, dir.x), SimMath::addf32(target.y, dir.y), SimMath::addf32(target.z, dir.z) };
	// RW 0x9B436B: the target's threat finder position (+ 0x24 -> + 0x5A4) when its squared 2D distance (SSE, dy^2 + dx^2) from the target's position (+ 0xC) is
	// at least 160000 (RW 0xC8971C), else the target position argument (lane AI-3: the finder is ported, GameLogic/SkirmishAI/AIThreatFinder.h)
	const float fx = SimMath::subf32(t.finderPosition.x, t.position.x), fy = SimMath::subf32(t.finderPosition.y, t.position.y);
	Coord3D side = SimMath::addf32(SimMath::mulf32(fy, fy), SimMath::mulf32(fx, fx)) < 160000.0f ? target : t.finderPosition;
	const float s = logic.random().getValue(0, 1, kFlankFile, 0x5B) == 1 ? -1.0f : 1.0f; // RW 0xBD19DC / 0xBD1908
	Coord3D perp{ SimMath::subf32(SimMath::mulf32(dir.y, s), SimMath::mulf32(dir.z, 0.0f)), SimMath::subf32(SimMath::mulf32(dir.z, 0.0f), SimMath::mulf32(dir.x, s)),
		SimMath::subf32(SimMath::mulf32(dir.x, 0.0f), SimMath::mulf32(dir.y, 0.0f)) };
	normalize3(perp);
	side = Coord3D{ SimMath::addf32(side.x, SimMath::mulf32(fd, perp.x)), SimMath::addf32(side.y, SimMath::mulf32(perp.y, fd)),
		SimMath::addf32(side.z, SimMath::mulf32(perp.z, fd)) };
	tac.waypoints = { start, side, beyond, target };
	tac.step = 0;
}

// RW 0x9B4041: PincerAttack's waypoints: the start, the two sides of the target, the target
void pincerWaypoints(GameLogic &logic, AITactic &tac, const Coord3D &start, const Coord3D &target)
{
	const int d = realToInt(SimMath::pc24Add(logic.random().getValueReal(0.0f, kPincerOffsetMax, kPincerFile, 0x87), kPincerOffsetBase));
	Coord3D dir{ SimMath::subf32(target.x, start.x), SimMath::subf32(target.y, start.y), SimMath::subf32(target.z, start.z) };
	normalize3(dir);
	Coord3D perp{ SimMath::subf32(dir.y, SimMath::mulf32(dir.z, 0.0f)), SimMath::subf32(SimMath::mulf32(dir.z, 0.0f), dir.x),
		SimMath::subf32(SimMath::mulf32(dir.x, 0.0f), SimMath::mulf32(dir.y, 0.0f)) };
	normalize3(perp);
	const float fd = (float)d;
	const float ox = SimMath::mulf32(fd, perp.x), oy = SimMath::mulf32(perp.y, fd), oz = SimMath::mulf32(perp.z, fd);
	const Coord3D left{ SimMath::addf32(target.x, ox), SimMath::addf32(oy, target.y), SimMath::addf32(oz, target.z) };
	const Coord3D right{ SimMath::subf32(target.x, ox), SimMath::subf32(target.y, oy), SimMath::subf32(target.z, oz) };
	tac.waypoints = { start, left, right, target };
	tac.step = 0;
	tac.step2 = 0;
}

// RW 0x9B396A: the feint team's destination: beside the enemy's first DOZER_FACTORY structure (its record's structure list), else beside the target object, else the
// feint team's centre; the side drawn at 0x7A
Coord3D feintPosition(GameLogic &logic, const AISkirmishPlayer &ai, AITactic &tac, const AITarget *t)
{
	static const int dozerFactory = KindOfTokens::indexOf("DOZER_FACTORY");
	int d = realToInt(SimMath::pc24Add(logic.random().getValueReal(0.0f, kFeintOffsetMax, kFeintFile, 0x43), kFeintOffsetBase));
	AITacticTeam *feint = teamByIndex(tac, 1);
	if (!feint)
	{
		return Coord3D{ 0.0f, 0.0f, 0.0f };
	}
	const Coord3D centre = livingCentre(logic, *feint);
	const Object *ref = nullptr;
	const int enemy = enemyOfGroup(ai);
	if (enemy >= 0)
	{
		if (const Player *them = playerOf(logic, enemy))
		{
			for (ObjectID id : AITacticalAI::recordOf(logic, them->getPlayerIndex()).structures())
			{
				const Object *o = logic.findObjectByID(id);
				if (o && is(*o, dozerFactory))
				{
					ref = o;
					break;
				}
			}
		}
	}
	Coord3D refPos;
	if (!ref)
	{
		d = realToInt(SimMath::pc24Add(logic.random().getValueReal(0.0f, kFeintFallbackMax, kFeintFile, 0x66), kFeintFallbackBase));
		ref = t ? logic.findObjectByID(t->object) : nullptr; // RW 0x6C6A24
		if (!ref)
		{
			if (!t)
			{
				return centre;
			}
			refPos = t->position;
		}
	}
	if (ref)
	{
		refPos = *ref->getPosition();
	}
	Coord3D dir{ SimMath::subf32(refPos.x, centre.x), SimMath::subf32(refPos.y, centre.y), SimMath::subf32(refPos.z, centre.z) };
	normalize3(dir);
	const float s = logic.random().getValue(0, 1, kFeintFile, 0x7A) == 1 ? -1.0f : 1.0f;
	Coord3D perp{ SimMath::subf32(SimMath::mulf32(dir.y, s), SimMath::mulf32(dir.z, 0.0f)), SimMath::subf32(SimMath::mulf32(dir.z, 0.0f), SimMath::mulf32(dir.x, s)),
		SimMath::subf32(SimMath::mulf32(dir.x, 0.0f), SimMath::mulf32(dir.y, 0.0f)) };
	normalize3(perp);
	const float fd = (float)d;
	return Coord3D{ SimMath::addf32(refPos.x, SimMath::mulf32(fd, perp.x)), SimMath::addf32(SimMath::mulf32(perp.y, fd), refPos.y),
		SimMath::addf32(SimMath::mulf32(perp.z, fd), refPos.z) };
}

// RW 0x8F28DD (the tactic's update) + RW 0x8F11DA (ready / launch / the started update)
// vslot 0x10 (the number of teams): the offensive prototypes' (teamCountOf), FarmKillSquad's RW 0x490AC4 = 1
int tacticTeamCount(const AITactic &tac)
{
	if (tac.targetless)
	{
		return tac.kindIndex == kTargetlessBase + TL_FARM_KILL || tac.kindIndex == kTargetlessBase + TL_FLAG_CAPTURE ? 1 : 0;
	}
	return teamCountOf((OffensiveKind)tac.kindIndex);
}

// RW 0x7A05A0: a team's strength, the threat values (RW 0x68F0EC) of its members with a template in member order (x87: each sum stored)
float teamStrength(GameLogic &logic, const AITacticTeam &team)
{
	float s = 0.0f;
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		if (o && o->getTemplate())
		{
			s = SimMath::fstpDword(SimMath::pc24AddW(AIThreatFinder::threatValueWide(*o), (double)s));
		}
	}
	return s;
}

// RW 0x68B36F: the object's goal object (RW 0x668303) when its AI's current state is active (AIUpdate slot 0x1BC, RW 0x662B3C)
const Object *activeGoal(GameLogic &logic, const Object &o)
{
	const AIUpdateInterface *u = o.getAIUpdateInterface();
	if (!u || !u->isStateActive() || u->currentVictimId() == INVALID_ID)
	{
		return nullptr;
	}
	return logic.findObjectByID(u->currentVictimId());
}

// RW 0x8F1A49 -> RW 0x8F157F: the tactic of the AI holding the team that holds `id` (the offensive list before the targetless one; within a tactic the teams by
// index from 0 while one exists, RW 0x8F128B), and that team
AITactic *tacticHolding(AIBrainState &b, ObjectID id, AITacticTeam **teamOut)
{
	for (int list : kTacticLists)
	{
		for (AITactic &t : b.tactics)
		{
			if (tacticList(t) != list)
			{
				continue;
			}
			for (int i = 0;; ++i)
			{
				AITacticTeam *team = teamByIndex(t, i);
				if (!team)
				{
					break;
				}
				if (std::find(team->members.begin(), team->members.end(), id) != team->members.end())
				{
					*teamOut = team;
					return &t;
				}
			}
		}
	}
	return nullptr;
}

// RW 0x8F1D88 (lane AI-3): the team the ending tactic's teams merge into: among the HORDEs of the AI's army list (the player info record + 0 -> + 4, S-418 order)
// that are in a team other than the player's default team and the first team, the first whose active goal (RW 0x68B36F) lies within 1500 (squared 2D SSE
// distance below 2250000, RW 0xC795A4) of the first team's centre (RW 0x7A0ECD) and whose tactic (slot 0x28 false: every offensive prototype, RW 0x9188EB;
// FarmKillSquad's is true, RW 0x8BD372) answers RW 0x8F1499 at the goal's position: with a target, RW 0x6C6623 (the target owner's countered threat: allies a,
// counters c, result r); r + strength > 0 merges, else GetGameLogicRandomValueReal(0, c) (AITactic.cpp 0x3FC) below a + strength merges
AITacticTeam *findMergeTeam(GameLogic &logic, AISkirmishPlayer &ai, AITactic &tac, AITacticTeam *first, float strength, Coord3D &goalOut)
{
	static const int horde = KindOfTokens::indexOf("HORDE");
	AIBrainState &b = ai.brain;
	Coord3D centre;
	if (!first || !teamCentre(logic, *first, centre))
	{
		return nullptr; // RW: no first team; a team without members divides by 0 (a NaN centre: no candidate is near)
	}
	std::vector<ObjectID> candidates;
	for (ObjectID id : AITacticalAI::recordOf(logic, ai.playerIndex).army())
	{
		const Object *o = logic.findObjectByID(id);
		AITacticTeam *team = nullptr;
		if (o && is(*o, horde) && tacticHolding(b, id, &team) && team != first)
		{
			candidates.push_back(id);
		}
	}
	for (ObjectID id : candidates)
	{
		const Object *o = logic.findObjectByID(id);
		const Object *goal = o ? activeGoal(logic, *o) : nullptr;
		if (!goal)
		{
			continue;
		}
		const Coord3D pos = *goal->getPosition();
		const float dx = SimMath::subf32(pos.x, centre.x), dy = SimMath::subf32(pos.y, centre.y);
		if (!(2250000.0f > SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy))))
		{
			continue;
		}
		AITacticTeam *team = nullptr;
		AITactic *tac2 = tacticHolding(b, id, &team);
		if (!tac2 || tac2->targetless) // slot 0x28: true for FarmKillSquad (also FlagCaptureSquad)
		{
			continue;
		}
		const AISkirmishPlayer *targetAI = logic.skirmishAI().findAI(tac2->targetOwner);
		const AITarget *t2 = targetAI && tac2->targetIndex >= 0 && tac2->targetIndex < (int)targetAI->brain.targets.size() ? &targetAI->brain.targets[(size_t)tac2->targetIndex] : nullptr;
		const Player *targetOwner = playerOf(logic, tac2->targetOwner);
		float a = 0.0f, c = 0.0f; // RW 0x8F14A3: both zeroed
		double r = 0.0;
		if (t2 && targetOwner)
		{
			r = AIThreatFinder::counteredThreat(logic, pos, *targetOwner, a, c);
		}
		else
		{
			++b.mergeTargetlessUnported; // RW 0x8F14C1 .. 0x8F1576 (the teams' strengths against their cached threat RW 0x7A6A20): not reached by an offensive tactic
			continue;
		}
		if (SimMath::pc24AddW(r, (double)strength) > 0.0) // RW 0x8F1EE3 .. 0x8F1EEE
		{
			goalOut = pos;
			return team;
		}
		const float draw = logic.random().getValueReal(0.0f, c, "AITactic.cpp", 0x3FC); // RW 0x8F1F08
		if (SimMath::pc24AddW((double)a, (double)strength) > (double)draw)
		{
			goalOut = pos;
			return team;
		}
	}
	return nullptr;
}

// RW 0x8F2006's team part (lane AI-3): the first team and, unless retreated, the summed strength (RW 0x7A05A0); the merge team (RW 0x8F1D88); then each team, unless
// retreated and when a merge team exists, moves to the merge goal (RW 0x7A0555 below 2 members: RW 0x774E2F(pos, INT_MAX, CMD_FROM_PLAYER); else RW 0x774F9C, the
// formation attack-move whose formation group manager is S-895: the port's group attack-move) and joins that team (RW 0x7A0F74, member order); then it is released
// (RW 0x7A12F4: endTactic clears it)
void mergeTeams(GameLogic &logic, AISkirmishPlayer &ai, AITactic &tac)
{
	AITacticTeam *first = nullptr;
	float strength = 0.0f;
	for (AITacticTeam &team : tac.teams)
	{
		if (!first)
		{
			first = &team;
		}
		if (!tac.retreated)
		{
			strength = SimMath::pc24Add(teamStrength(logic, team), strength); // RW 0x8F2084 .. 0x8F2097
		}
	}
	Coord3D goal;
	AITacticTeam *into = findMergeTeam(logic, ai, tac, first, strength, goal);
	if (!into || tac.retreated)
	{
		return;
	}
	for (AITacticTeam &team : tac.teams)
	{
		attackMove(logic, team.members, goal);
		if (&team != into && !team.members.empty()) // RW 0x7A0F74: a team does not join itself
		{
			into->members.insert(into->members.end(), team.members.begin(), team.members.end());
			team.members.clear();
			++ai.brain.merges;
		}
	}
}

// RW 0x8F2006(success, 0): vslot 2 first (the targetless prototypes' cleanup), then the common part (endTactic)
void endAnyTactic(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner, bool success)
{
	if (tac.ended)
	{
		return;
	}
	if (tac.targetless)
	{
		targetlessCleanup(logic, ai, tac);
	}
	else if (tac.kindIndex == SIMPLE_EXPANSION)
	{
		// RW 0x9B5042 (SimpleExpansion's vslot 2): an unfinished request (RW 0x8EDA5E) leaves the tactic player's queues (RW 0x9618E3 -> 0x8F0602); the
		// request is deleted (+ 0x58 = 0)
		if (tac.request)
		{
			if (!tac.request->finished())
			{
				AIBaseBuilder::leaveQueues(ai.build, tac.request.get());
			}
			tac.request.reset();
		}
	}
	mergeTeams(logic, ai, tac);
	// RW 0x8F2179 .. 0x8F21B6 (lane AI-3): a failed end (second argument 0, third 0) of a tactic with teams (vslot 0x10) moves the target's threat finder to the
	// tactic's position (+ 0x38) when the enemies' threat there (with the finder's radius) is above the target's threat (+ 0x28), which takes it (RW 0x6C644C);
	// else the finder stays. Ordered as RW: after the teams were merged / released, before the target's team count drops
	if (!success && tacticTeamCount(tac) != 0 && tac.targetIndex >= 0 && tac.targetIndex < (int)owner.targets.size())
	{
		AITarget &t = owner.targets[(size_t)tac.targetIndex];
		const Player *targetOwner = playerOf(logic, tac.targetOwner);
		if (targetOwner)
		{
			const float there = AIThreatFinder::threatTotal(logic, tac.position, t.radius, true, *targetOwner, 0);
			if (!(there <= t.threat))
			{
				t.finderPosition = tac.position;
				t.threat = there;
			}
		}
	}
	endTactic(tac, owner, success);
	++b.tacticsEnded;
}

// RW 0x8F1792(0, target): team 0 attacks the object (AIGroup RW 0x77208A(0, target, INT_MAX, 0)) and its idle counter restarts
void teamAttackObject(GameLogic &logic, AITacticTeam &team, Object &target)
{
	for (ObjectID id : team.members)
	{
		Object *o = logic.findObjectByID(id);
		AIUpdateInterface *u = o && alive(o) ? o->getAIUpdateInterface() : nullptr;
		if (u)
		{
			u->aiAttackObject(&target, CMD_FROM_AI);
		}
	}
	team.idleFrames = 0;
}

// FlagCaptureSquad update RW 0x9BC3F4, nearest flag RW 0x9BC213, command RW 0x8F1933.
void updateFlagCapture(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner)
{
	AITacticTeam *team = teamByIndex(tac, 0);
	const Player *me = playerOf(logic, ai.playerIndex);
	if (!team || !me)
	{
		endAnyTactic(logic, ai, b, tac, owner, true);
		return;
	}
	bool canCapture = false, canAttack = false, replacementPending = false;
	for (ObjectID id : team->members)
	{
		const Object *unit = logic.findObjectByID(id);
		if (!unit)
		{
			continue;
		}
		// RW 0x9BC0EE rejects status 0x66 (SUMMONING_REPLACEMENT).
		replacementPending = replacementPending || unit->testStatus((unsigned)CombatNames::status("SUMMONING_REPLACEMENT"));
		for (const auto &module : unit->modules())
		{
			const SpecialPowerModuleInterface *power = module->getSpecialPower();
			canCapture = canCapture || (power && power->getSpecialPowerTemplate() && power->getSpecialPowerTemplate()->getSpecialPowerType() == 0x1D);
		}
		canAttack = canAttack || (alive(unit) && unit->isKindOfName("CAN_ATTACK"));
	}
	if (tac.squadTarget != INVALID_ID && !allTeamsDone(ai, tac))
	{
		const Object *target = logic.findObjectByID(tac.squadTarget);
		if (target && me->getRelationship(target->getTeam()) != ALLIES && canCapture)
		{
			return;
		}
		tac.squadTarget = INVALID_ID;
	}
	else if (canCapture && canAttack && !replacementPending)
	{
		Coord3D centre{};
		teamCentre(logic, *team, centre);
		Object *best = nullptr;
		float distance = 0.0f;
		for (ObjectID id : logic.skirmishAI().captureFlags())
		{
			Object *flag = logic.findObjectByID(id);
			if (!flag || !flag->isKindOfName("CAPTUREFLAG") || me->getRelationship(flag->getTeam()) == ALLIES)
			{
				continue;
			}
			const float dx = SimMath::subf32(flag->getPosition()->x, centre.x), dy = SimMath::subf32(flag->getPosition()->y, centre.y);
			const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
			if (!best || d < distance)
			{
				best = flag;
				distance = d;
			}
		}
		if (best)
		{
			const CommandButton *button = TheCommandStore ? TheCommandStore->findCommandButton("Command_CaptureBuilding") : nullptr;
			if (!button)
			{
				throw std::logic_error("FlagCaptureSquad: missing Command_CaptureBuilding");
			}
			for (ObjectID id : team->members)
			{
				if (Object *unit = logic.findObjectByID(id))
				{
					unit->doCommandButtonAtObject(button, best, CMD_FROM_PLAYER, false);
				}
			}
			team->idleFrames = 0;
			tac.squadTarget = best->getID();
			return;
		}
	}
	endAnyTactic(logic, ai, b, tac, owner, true);
}

void updateFarmKill(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner)
{
	static const int canAttack = KindOfTokens::indexOf("CAN_ATTACK"), economy = KindOfTokens::indexOf("ECONOMY_STRUCTURE");
	// RW 0x9BB27E: no team with index 0 (RW 0x8F128B(0)) ends it (0, 0)
	if (tac.teams.empty())
	{
		endAnyTactic(logic, ai, b, tac, owner, false);
		return;
	}
	if (tac.squadTarget != INVALID_ID && !allTeamsDone(ai, tac))
	{
		// RW 0x9BBE8B ..: a gone, DESTROYED or dead target is dropped; a BLOCKING_GATE target is also dropped when its GateOpenAndCloseBehavior (vslot 0x18) says so (the
		// gate behaviour is not ported: a gate is kept, S-893)
		const Object *t = logic.findObjectByID(tac.squadTarget);
		if (!alive(t))
		{
			tac.squadTarget = INVALID_ID;
		}
		return;
	}
	// RW 0x9BB578: the squad's object (+ 0x5C) or the first living CAN_ATTACK member of team 0
	const Object *unit = nullptr;
	for (ObjectID id : tac.teams[0].members)
	{
		const Object *o = logic.findObjectByID(id);
		if (alive(o) && is(*o, canAttack))
		{
			unit = o;
			break;
		}
	}
	const int enemy = enemyOfGroup(ai);
	if (!unit || enemy < 0)
	{
		endAnyTactic(logic, ai, b, tac, owner, true); // RW 0x9BBFB8 / 0x9BBFBA: (1, 0)
		return;
	}
	// RW 0x9BBF4C ..: the farm choice RW 0x9BBA51 (a weighted pick among the enemy's farms by path and distance, draws AIFarmKillSquad.cpp 0x203 / 0x20A / 0x21D),
	// GetGameLogicRandomValue(0, 4) (line 0x8B) between it and RW 0x9BB5E4 (draws 0x186 / 0x18D / 0x1A0) is NOT ported (S-893). Stand-in: the enemy's living
	// ECONOMY_STRUCTURE nearest to the squad's unit (2D, object list order on ties); no draw
	const Player *them = playerOf(logic, enemy);
	Object *target = nullptr;
	float best = 0.0f;
	for (Object *o = logic.getFirstObject(); o && them; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != them || !alive(o) || !is(*o, economy))
		{
			continue;
		}
		const float dx = SimMath::subf32(o->getPosition()->x, unit->getPosition()->x), dy = SimMath::subf32(o->getPosition()->y, unit->getPosition()->y);
		const float d = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
		if (!target || d < best)
		{
			target = o;
			best = d;
		}
	}
	if (!target)
	{
		endAnyTactic(logic, ai, b, tac, owner, true); // RW 0x9BBF94: (1, 0)
		return;
	}
	tac.squadTarget = target->getID();
	teamAttackObject(logic, tac.teams[0], *target);
}

// vslot 0x18 (the launch) of the offensive prototypes
void launchOffensive(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner, AITarget *t)
{
	const Player *player = playerOf(logic, ai.playerIndex);
	switch ((OffensiveKind)tac.kindIndex)
	{
	case SIMPLE_ATTACK: // RW 0x9B4DDA -> RW 0x8F19E9: every team attack-moves to the target position
		if (t)
		{
			for (AITacticTeam &team : tac.teams)
			{
				attackMove(logic, team.members, t->position); // RW 0x8F17EE (which clears the team's idle counter)
				team.idleFrames = 0;
			}
			++b.attacksLaunched;
		}
		return;
	case SIMPLE_EXPANSION: // vslot 0x18 = RW 0x63F3BF: nothing (the stages of its started update send the team)
		return;
	case FORMATION_ATTACK: // RW 0x9B4AA8: no team 0 ends it; else the state remembers team 0's centre and the target position (no command yet)
	case FLANK_ATTACK:     // RW 0x9B48F2 -> RW 0x9B461D
	case PINCER_ATTACK:    // RW 0x9B42BE -> RW 0x9B4041
	{
		AITacticTeam *team0 = teamByIndex(tac, 0);
		if (!team0 || !t)
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
			return;
		}
		const Coord3D start = livingCentre(logic, *team0);
		if (tac.kindIndex == FORMATION_ATTACK)
		{
			tac.step = 0;
			tac.gatherPos = start; // RW 0x9B4A79
			tac.aimPos = t->position;
		}
		else if (tac.kindIndex == FLANK_ATTACK)
		{
			flankWaypoints(logic, tac, *t, start, t->position);
		}
		else
		{
			pincerWaypoints(logic, tac, start, t->position);
		}
		++b.attacksLaunched;
		return;
	}
	case FEINT_ATTACK: // RW 0x9B3C94: the feint team (1) goes to the feint position (RW 0x8F1848), the launch frame is kept
	{
		const Coord3D pos = feintPosition(logic, ai, tac, t);
		if (player)
		{
			teamMoveFormation(logic, *player, tac, 1, pos);
		}
		tac.launchFrame = logic.getFrame();
		++b.attacksLaunched;
		return;
	}
	case BASE_PENETRATION: // RW 0x9B36BD: every team goes to the target object (RW 0x8F1A19 -> RW 0x8F1848)
	{
		const Object *o = t ? logic.findObjectByID(t->object) : nullptr;
		if (o && player)
		{
			for (size_t i = 0; i < tac.teams.size(); ++i)
			{
				teamMoveFormation(logic, *player, tac, tac.teams[i].index, *o->getPosition());
			}
			++b.attacksLaunched;
		}
		return;
	}
	case SIMPLE_SIEGE: // RW 0x9B3380 (nothing) and RW 0x9B31C5: never reached, the siege tactics do not apply in the port (RW 0x9EF9AF's path check, S-418)
	case SIEGE_GATES:
		return;
	}
}

// vslot 0x1C (the started update) of the offensive prototypes
void updateOffensive(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner, AITarget *t)
{
	const bool targetGone = !t || t->inactive; // the target's + 0x18
	switch ((OffensiveKind)tac.kindIndex)
	{
	case FORMATION_ATTACK: // RW 0x9B4C32
		if (!allTeamsDone(ai, tac) && !targetGone)
		{
			return;
		}
		if (tac.step == 3 || targetGone)
		{
			endAnyTactic(logic, ai, b, tac, owner, true);
			return;
		}
		if (!teamByIndex(tac, 0))
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
			return;
		}
		if (tac.step == 0)
		{
			// RW 0x9B4A2E: state 1, the destination is the gathering centre; a formation move facing the target (RW 0x9B4B0E: toAngle(aim - destination)), AIGroup
			// RW 0x774897 mode 2 (S-895: the formation layout is not ported)
			tac.step = 1;
			const float angle = toAngle2(SimMath::subf32(tac.aimPos.x, tac.gatherPos.x), SimMath::subf32(tac.aimPos.y, tac.gatherPos.y));
			const Player *player = playerOf(logic, ai.playerIndex);
			if (player)
			{
				teamMoveFormation(logic, *player, tac, 0, tac.gatherPos, true, angle);
			}
		}
		else
		{
			tac.step = tac.step == 1 ? 2 : 3; // RW 0x9B4A2E: 1 -> 2, then 3; the destination is the target position kept at the launch
			teamAttackMove(logic, tac, 0, tac.aimPos);
		}
		return;
	case FLANK_ATTACK: // RW 0x9B450C: on every idle, the next waypoint for every team (RW 0x8F19E9); after the last, the end (1)
		if (!allTeamsDone(ai, tac) && !targetGone)
		{
			return;
		}
		if (!targetGone && tac.step != (int)tac.waypoints.size())
		{
			const Coord3D pos = tac.waypoints[(size_t)tac.step++];
			for (const AITacticTeam &team : tac.teams)
			{
				teamAttackMove(logic, tac, team.index, pos);
			}
			return;
		}
		endAnyTactic(logic, ai, b, tac, owner, true);
		return;
	case PINCER_ATTACK: // RW 0x9B3EAB with RW 0x9B3D02: team 0 waypoints 0, 1, 3; team 1 waypoints 0, 2, 3
	{
		if (!allTeamsDone(ai, tac) && !targetGone)
		{
			return;
		}
		const int count = (int)tac.waypoints.size();
		if ((tac.step == count && tac.step2 == count) || targetGone)
		{
			endAnyTactic(logic, ai, b, tac, owner, true);
			return;
		}
		if (!teamByIndex(tac, 0) || !teamByIndex(tac, 1))
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
			return;
		}
		const int i0 = tac.step, i1 = tac.step2;
		tac.step = i0 == 0 ? 1 : (i0 != 1 ? 4 : 3);
		teamAttackMove(logic, tac, 0, tac.waypoints[(size_t)(i0 < count ? i0 : count - 1)]);
		tac.step2 = i1 == 0 ? 2 : (i1 != 2 ? 4 : 3);
		teamAttackMove(logic, tac, 1, tac.waypoints[(size_t)(i1 < count ? i1 : count - 1)]);
		return;
	}
	case FEINT_ATTACK: // RW 0x9B3BCB
	{
		bool goTail = true;
		if (!tac.mainLaunched && logic.getFrame() - tac.launchFrame > kFeintMainDelay)
		{
			tac.mainLaunched = true; // + 0x58: the main team (0) attack-moves to the target 10 seconds after the feint left
			if (t)
			{
				teamAttackMove(logic, tac, 0, t->position);
			}
		}
		else if (!targetGone)
		{
			AITacticTeam *team0 = teamByIndex(tac, 0);
			if (!team0)
			{
				endAnyTactic(logic, ai, b, tac, owner, false);
			}
			else if (teamAllIdle(logic, *team0) && tac.mainLaunched)
			{
				endAnyTactic(logic, ai, b, tac, owner, true);
			}
		}
		else
		{
			endAnyTactic(logic, ai, b, tac, owner, true);
		}
		// RW 0x9B3C61: the feint team, once done, goes to a new feint position
		if (goTail && teamByIndex(tac, 1) && teamDone(ai, tac, 1))
		{
			const Coord3D pos = feintPosition(logic, ai, tac, t);
			if (const Player *player = playerOf(logic, ai.playerIndex))
			{
				teamMoveFormation(logic, *player, tac, 1, pos);
			}
		}
		return;
	}
	case SIMPLE_EXPANSION:
	{
		// RW 0x9B52BD (lane AIEXPAND-1), the stage + 0x5C: past stage 0 a failed request (state 3) ends it (0, 0); 0: the request (RW 0x9B51E9), then 1; 1: once
		// the request is in progress (state 1) every team attack-moves to the target (RW 0x9B4DDA), then 2; 2: every team done (RW 0x8F175A) makes it 3; 2 and
		// 3: a complete request (state 2) adds a base at the target (RW 0x9B5020), then 4; 4: it ends (1, 0)
		AIBuildRequest *req = tac.request.get();
		if (tac.step > 0 && !req)
		{
			throw std::logic_error("AITacticalAI: a SimpleExpansion past its first stage has no request"); // RW reads + 0x58 -> + 0x10 unchecked
		}
		if (tac.step > 0 && req->state == 3)
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
			return;
		}
		if (!t)
		{
			throw std::logic_error("AITacticalAI: a started SimpleExpansion has no target"); // RW keeps the target (+ 0x20) until the end
		}
		switch (tac.step)
		{
		case 0:
		{
			// RW 0x9B51E9: a plain structure request (0x40 bytes, ctor RW 0x97DA21) for "MenFortress" at the target position (RW 0x97D986: z = the ground
			// height there), priority 3000, + 0x20 = 0 (a dozer's death does not send it back to the queue), started for the tactic's player (vslot 0x18
			// (player, 0) = RW 0x9618BA)
			auto r = std::make_shared<AIBuildRequest>();
			r->plain = true;
			r->serial = ai.build.nextSerial++;
			r->templateName = kExpansionBuilding;
			r->offset = t->position;
			r->offset.z = logic.getGroundHeight(r->offset.x, r->offset.y);
			r->priority = kExpansionPriority;
			r->requeueOnProducerDeath = false;
			AIBaseBuilder::startPlainRequest(ai.build, r);
			tac.request = r;
			tac.step = 1;
			return;
		}
		case 1:
			if (req->state == 1)
			{
				for (AITacticTeam &team : tac.teams) // RW 0x9B4DDA, SimpleAttack's launch
				{
					attackMove(logic, team.members, t->position);
					team.idleFrames = 0;
				}
				++b.attacksLaunched;
				tac.step = 2;
			}
			return;
		case 2:
			if (allTeamsDone(ai, tac))
			{
				tac.step = 3;
			}
			break;
		case 3:
			break;
		case 4:
			endAnyTactic(logic, ai, b, tac, owner, true);
			return;
		default:
			return;
		}
		if (req->state == 2)
		{
			// RW 0x9B5020: the tactic player's base builder (RW 0x6A950B + 4) adds a base at the target's position (RW 0x90D3C7)
			SkirmishAIManager &m = logic.skirmishAI();
			AIBaseBuilder::addBase(logic, ai, t->position, m.mapFileName(), ai.store->data().anyTypeTemplateDisabledSlots, m.errors());
			tac.step = 4;
		}
		return;
	}
	case BASE_PENETRATION: // RW 0x9B3646: done or the target inactive -> (1, 0); no team 0 -> (0, 0)
		if (allTeamsDone(ai, tac) || targetGone)
		{
			endAnyTactic(logic, ai, b, tac, owner, true);
		}
		if (!teamByIndex(tac, 0))
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
		}
		return;
	case SIMPLE_ATTACK:
	case SIMPLE_SIEGE: // never started in the port (S-418)
	case SIEGE_GATES:
	default:
		// SimpleAttack RW 0x9B4DE7: every team done (RW 0x8F175A) or the target inactive ends it with success
		if (allTeamsDone(ai, tac) || targetGone)
		{
			endAnyTactic(logic, ai, b, tac, owner, true);
		}
		return;
	}
}

// RW 0x774F2B (lane AI-3), a tactic team's AIGroup update from RW 0x8F144C: when not every member with an AI is idle or dead (RW 0x76FCBF: AI slot 0x1B8, + 0x458
// bit 0), RW 0x77005F(1) (the group's speed matching: NOT ported, stop S-1301) and RW 0x7728E8: the goal object (RW 0x668303) of the first member not held by a
// horde (+ 0x27C) whose AI has one; then every member not held by a horde that can attack (Object::isAbleToAttack RW 0x691269, lane DECOMP-3), whose AI
// has no goal object and is idle (slot 0x1B8), attack-moves to that object's position with INT_MAX shots from the AI (RW 0x696266(pos, 0x7FFFFFFF, 2):
// AI command 0xF, the attack-move to a position)
void updateTeamGroup(GameLogic &logic, const AITacticTeam &team)
{
	bool allIdle = true;
	for (ObjectID id : team.members)
	{
		Object *o = logic.findObjectByID(id);
		AIUpdateInterface *u = o ? o->getAIUpdateInterface() : nullptr;
		if (u && !u->isIdle() && !o->isEffectivelyDead())
		{
			allIdle = false;
			break;
		}
	}
	if (allIdle)
	{
		return;
	}
	const Object *goal = nullptr;
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		const AIUpdateInterface *u = o && !heldByHorde(*o) ? o->getAIUpdateInterface() : nullptr;
		if (u && u->currentVictimId() != INVALID_ID)
		{
			goal = logic.findObjectByID(u->currentVictimId());
			if (goal)
			{
				break;
			}
		}
	}
	if (!goal)
	{
		return;
	}
	const Coord3D pos = *goal->getPosition();
	for (ObjectID id : team.members)
	{
		Object *o = logic.findObjectByID(id);
		if (!o || heldByHorde(*o) || !o->isAbleToAttack())
		{
			continue;
		}
		AIUpdateInterface *u = o->getAIUpdateInterface();
		if (u && !(u->currentVictimId() != INVALID_ID && logic.findObjectByID(u->currentVictimId())) && u->isIdle())
		{
			attackMove(logic, std::vector<ObjectID>{ id }, pos, CMD_FROM_AI);
		}
	}
}

// RW 0x8F2784 (vslot 0x2C of every offensive prototype; lane AI-3): no member of any team engaged (RW 0x8F12FF: an AI with a goal object) and the target's
// relative threat at the tactic's position (RW 0x8F1251 -> 0x6C650B: allies minus enemies within 400 for the target's owner; 0 without a target) below 0 ->
// RW 0x8F243A: unless already retreated or the target is DEFENSIVE (type 1), every team (indices 0 .. n - 1, RW 0x8F1A19) goes to the origin (+ 0x44) by
// RW 0x8F1848, the tactic is marked retreated (+ 0x50) and ends (0, 0)
bool checkRetreat(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner, AITarget *t)
{
	if (!t)
	{
		return false;
	}
	for (const AITacticTeam &team : tac.teams)
	{
		if (teamEngaged(logic, team))
		{
			return false;
		}
	}
	const Player *targetOwner = playerOf(logic, tac.targetOwner);
	if (!targetOwner || !(AIThreatFinder::relativeThreat(logic, tac.position, *targetOwner) < 0.0f))
	{
		return false;
	}
	if (tac.retreated || t->type == kDefensive)
	{
		return false;
	}
	const Player *player = playerOf(logic, ai.playerIndex);
	const int n = (int)tac.teams.size();
	for (int i = 0; player && i < n; ++i)
	{
		teamMoveFormation(logic, *player, tac, i, tac.origin);
	}
	tac.retreated = true;
	++b.retreats;
	endAnyTactic(logic, ai, b, tac, owner, false);
	return true;
}

// RW 0x8F28DD (the tactic's update) + RW 0x8F11DA (ready / launch / the started update)
void updateTactic(GameLogic &logic, AISkirmishPlayer &ai, AIBrainState &b, AITactic &tac, AIBrainState &owner)
{
	// RW 0x8F28DD prunes active teams (+0x14), not pending prototypes (+4, RW 0x8F232F).
	// This port stores both together: a team waiting for recruitment must survive until handover (RW 0x6C779B).
	tac.teams.erase(std::remove_if(tac.teams.begin(), tac.teams.end(), [&](const AITacticTeam &team) {
		return team.handedOver && !teamAlive(logic, team);
	}), tac.teams.end());
	AITarget *t = !tac.targetless && tac.targetIndex >= 0 && tac.targetIndex < (int)owner.targets.size() ? &owner.targets[(size_t)tac.targetIndex] : nullptr;
	if (!tac.started)
	{
		if (t && t->dropped)
		{
			endAnyTactic(logic, ai, b, tac, owner, false);
			return;
		}
	}
	else if (tacticTeamCount(tac) != 0 && tac.teams.empty())
	{
		endAnyTactic(logic, ai, b, tac, owner, false);
		return;
	}
	else
	{
		// RW 0x8F2955 ..: the idle counters, the position, the origin (first position), vslot 0x20 (RW 0x63F3BF: nothing), RW 0x8F144C (each team's AIGroup
		// update RW 0x774F2B, lane AI-3), vslot 0x2C = RW 0x8F2784 (every offensive prototype's slot 0x2C, lane AI-3)
		updateIdleCounters(logic, ai, tac);
		updateTacticPosition(logic, tac);
		if (tac.origin.x == 0.0f && tac.origin.y == 0.0f && tac.origin.z == 0.0f)
		{
			tac.origin = tac.position;
		}
		for (const AITacticTeam &team : tac.teams)
		{
			updateTeamGroup(logic, team);
		}
		if (checkRetreat(logic, ai, b, tac, owner, t))
		{
			return;
		}
	}
	// RW 0x8F11DA
	tac.ready = !tac.teams.empty() && std::all_of(tac.teams.begin(), tac.teams.end(), [](const AITacticTeam &team) { return team.handedOver; });
	if (!tac.ready || tac.ended)
	{
		return;
	}
	if (tac.started)
	{
		if (tac.targetless)
		{
			if (tac.kindIndex == kTargetlessBase + TL_FLAG_CAPTURE) { updateFlagCapture(logic, ai, b, tac, owner); }
			else { updateFarmKill(logic, ai, b, tac, owner); }
			return;
		}
		updateOffensive(logic, ai, b, tac, owner, t); // vslot 0x1C (lane AI-2 r5: every offensive body)
		return;
	}
	if (tac.readyFrame == 0)
	{
		tac.readyFrame = logic.getFrame();
		return;
	}
	// RW 0xDE9F08 frames (0 in the image) after the ready frame
	tac.started = true;
	if (tac.targetless)
	{
		return; // FarmKillSquad / FlagCaptureSquad vslot 0x18 is RW 0x63F3BF (nothing)
	}
	launchOffensive(logic, ai, b, tac, owner, t); // vslot 0x18
}

// RW 0x8F232F (the start of a targetless clone, the AI as + 0x24): one pooled team per vslot 0x10, type TARGETLESS, named "TARGETLESS_<tactic>_<id>_<n>",
// set up by vslot 3; a team that is not set up ends the tactic (0, 0)
void startTargetless(GameLogic &logic, AISkirmishPlayer &ai, AITactic &tac)
{
	const int count = tacticTeamCount(tac);
	for (int i = 0; i < count; ++i)
	{
		AITacticTeam team;
		team.name = std::string("TARGETLESS_") + tac.kind + "_" + std::to_string(tac.id) + "_" + std::to_string(i);
		team.type = kTargetlessType;
		team.index = i;
		if (tac.kindIndex == kTargetlessBase + TL_FLAG_CAPTURE)
		{
			// RW 0x9BC1A0: forbid HERO/MACHINE/DOZER, priority 30, one to three units
			// (draw source line 0xEF).
			team.forbiddenKinds = { kindBit("HERO"), kindBit("MACHINE"), kindBit("DOZER") };
			team.priority = 30;
			team.minimum = 1;
			team.maximum = logic.random().getValue(1, 3, kFlagCaptureFile, 0xEF);
			ai.brain.namedCounters[kFlagCaptureRunning] = 1;
		}
		if (tac.kindIndex == kTargetlessBase + TL_FARM_KILL)
		{
			// RW 0x9BB479: RW 0x8F121F (no draw for a TARGETLESS team); the forbidden KindOf mask (RW 0x9BB491 .. 0x9BB499: + 0x307 |= 4, + 0x30C |= 0x20,
			// + 0x2FD |= 0x48), + 0x21C = 500, the minimum 1 in the Rush phase else 2, the maximum GetGameLogicRandomValue(minimum, 3) (AIFarmKillSquad.cpp line
			// 0x267), the squad marked running. Lane BFME2MODE-0: the priority is the edition's (SkirmishAIEditionRules.h: RW 0x9BB49D 500, BFME2 0x9AD00B 30)
			team.forbiddenKinds = { kindBit("HERO"), kindBit("ARCHER"), kindBit("MACHINE"), kindBit("DOZER") };
			team.priority = SkirmishAIEditionRules::farmKillPriority(logic.edition());
			team.minimum = ai.build.phase == 0 ? 1 : 2;
			team.maximum = logic.random().getValue(team.minimum, 3, kFarmKillFile, 0x267);
			ai.brain.namedCounters[kFarmKillRunning] = 1;
		}
		tac.teams.push_back(team);
	}
}

// RW 0x90C5E2
void pickTargetless(GameLogic &logic, AISkirmishPlayer &ai)
{
	AIBrainState &b = ai.brain;
	std::vector<int> always, pool;
	for (int i = 0; i < kTargetlessCount; ++i)
	{
		if (targetlessApplies(logic, ai, i))
		{
			(kTargetlessAlways[i] ? always : pool).push_back(i); // vslot 0x34
		}
	}
	if (!pool.empty())
	{
		++b.targetlessPicks;
		always.push_back(pool[(size_t)logic.random().getValue(0, (int)pool.size() - 1, kTargetlessFile, 0x1BC)]);
	}
	for (int index : always)
	{
		AITactic tac; // vslot 0x30 (the clone), RW 0x8F232F, then the active targetless list (generator + 0x58)
		tac.targetless = true;
		tac.kindIndex = kTargetlessBase + index;
		tac.kind = kTargetlessNames[index];
		tac.id = b.nextTacticId++;
		tac.targetOwner = ai.playerIndex;
		// RW 0x9BB43D clones through RW 0x9BB3CA, then overwrites the eligibility flag: the discarded constructor result still consumes a draw.
		if (index == TL_FARM_KILL)
		{
			(void)logic.random().getValue(1, 100, kFarmKillFile, 0x39);
		}
		startTargetless(logic, ai, tac);
		++b.tacticsByKind[tac.kind];
		++b.targetlessStarted;
		b.tactics.push_back(tac);
	}
}

bool anyKind(const Object &o, const std::vector<int> &bits)
{
	for (int bit : bits)
	{
		if (is(o, bit))
		{
			return true;
		}
	}
	return false;
}

// RW 0x9A24FC (lane DECOMP-2, BFME2 decomp AITeamBuilder.cpp AITeamBuilder::rva0059A01C, tier B same-shape): not effectively dead (+ 0x458 bit 0), not
// IGNORES_SELECT_ALL (template + 0x113 bit 3), in the player's default team (+ 0x31C == player + 0x30C), one of the required KindOf bits when there are any (+ 0x2E0),
// none of the forbidden ones (+ 0x2FC), at least LOGICFRAMES_PER_SECOND * 10 frames old (RW 0xDEBE5C, set at RW 0xBC9849; frame - creation frame + 0x4E0,
// unsigned) and no current victim (AI + 0x260 -> RW 0x668303). INFERENCE (S-418): "in the default team" is "in no team of the AI's tactics"; RW's two ways to
// take a unit from another team (that team active with a lower priority than this team's + 0x21C, or a DEFENSIVE target within 1000) are not ported; the port
// also refuses a unit without an AI, under construction or inside a container (the army list's readings)
bool recruitable(GameLogic &logic, const AIBrainState &b, const AITacticTeam &team, const Object &o)
{
	const Kinds &k = kinds();
	if (!alive(&o) || is(o, k.ignoresSelectAll) || inAnyTeamOf(b, o.getID()))
	{
		return false;
	}
	if (!team.requiredKinds.empty() && !anyKind(o, team.requiredKinds))
	{
		return false;
	}
	if (!team.forbiddenKinds.empty() && anyKind(o, team.forbiddenKinds))
	{
		return false;
	}
	if (logic.getFrame() - o.getCreationFrame() < (std::uint32_t)(kLogicFramesPerSecond * 10))
	{
		return false;
	}
	const AIUpdateInterface *u = o.getAIUpdateInterface();
	if (!u || u->currentVictim())
	{
		return false;
	}
	return !o.isUnderConstruction() && !o.getContainedBy();
}

// RW 0x9A24A6 (AITeamBuilder::getCurNumUnits): the team's members with any of CAN_ATTACK, HERO, SUPPORT (template + 0x108 bit 3, + 0x110 bit 26, + 0x120 bit 26).
// INFERENCE: RW's team list keeps a member until it is destroyed, so a dying member still counts
std::uint32_t teamUnitCount(GameLogic &logic, const AITacticTeam &team)
{
	const Kinds &k = kinds();
	std::uint32_t n = 0;
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && (is(*o, k.canAttack) || is(*o, k.hero) || is(*o, k.support)))
		{
			++n;
		}
	}
	return n;
}

// RW 0x7A0555 (Team::rva0039DC9E, tier A): the members whose KindOf has all of `set` and none of `clear`
int teamMembersWith(GameLogic &logic, const AITacticTeam &team, const std::vector<int> &set, const std::vector<int> &clear)
{
	int n = 0;
	for (ObjectID id : team.members)
	{
		const Object *o = logic.findObjectByID(id);
		if (!o || o->isDestroyed() || !o->getTemplate())
		{
			continue;
		}
		bool ok = true;
		for (int bit : set)
		{
			ok = ok && is(*o, bit);
		}
		for (int bit : clear)
		{
			ok = ok && !is(*o, bit);
		}
		n += ok ? 1 : 0;
	}
	return n;
}

// RW 0x9A26A1 (RotWK-only, the team builder's threat test): the allied group's target of the team's prototype (+ 0x2C4: the target id the start wrote, RW 0x8F22A0;
// -1 for a targetless team; RW 0x6A95CD -> 0x8EDC45) answers true when there is none; else the enemy record of the target (RW 0x6C6743) against the members with
// any of CAN_ATTACK, HERO, SUPPORT: sum = fstp(RW 0x7ED963(the record, the member's ThreatBreakdown category, its threat value RW 0x68F0EC stored) + sum)
// (x87 at PC24), true when sum >= the record's total (comiss / jb: NaN fails). The members in the team list's order (ZH Object.cpp: setTeam prepends, so the
// newest first: INFERENCE for RotWK's RW 0x69954A)
bool teamMeetsThreat(GameLogic &logic, const AITactic &tac, const AITacticTeam &team)
{
	if (tac.targetless || tac.targetIndex < 0)
	{
		return true;
	}
	const AISkirmishPlayer *chooser = logic.skirmishAI().findAI(tac.targetOwner);
	const Player *owner = playerOf(logic, tac.targetOwner);
	if (!chooser || !owner || tac.targetIndex >= (int)chooser->brain.targets.size())
	{
		return true;
	}
	AIThreatFinder::ThreatRecord enemy = enemyRecord(logic, *chooser, *owner, chooser->brain.targets[(size_t)tac.targetIndex]);
	const Kinds &k = kinds();
	float sum = 0.0f;
	for (auto it = team.members.rbegin(); it != team.members.rend(); ++it)
	{
		const Object *o = logic.findObjectByID(*it);
		if (!o || o->isDestroyed() || !o->getTemplate() || !(is(*o, k.canAttack) || is(*o, k.hero) || is(*o, k.support)))
		{
			continue;
		}
		const double c = AIThreatFinder::counterValue(enemy, AIThreatFinder::threatCategory(*o->getTemplate()), AIThreatFinder::threatValue(*o));
		sum = SimMath::fstpDword(SimMath::pc24AddW(c, (double)sum)); // RW 0x9A2731: fadd dword; fstp dword
	}
	return sum >= enemy.v[0];
}
} // namespace

void AITacticalAI::flankWaypointsForTest(GameLogic &logic, AITactic &tac, const AITarget &t, const Coord3D &start)
{
	flankWaypoints(logic, tac, t, start, t.position);
}

void AITacticalAI::expansionHeuristicForTest(GameLogic &logic, AISkirmishPlayer &ai, AITarget &t)
{
	heuristicExpansion(logic, ai, ai.brain, t);
}

void AITacticalAI::addAttackedAtForTest(GameLogic &logic, AISkirmishPlayer &ai, const Coord3D &pos)
{
	if (const Player *p = playerOf(logic, ai.playerIndex))
	{
		addAttackedAt(logic, ai.brain, *p, pos, -1);
	}
}

void AITacticalAI::pincerWaypointsForTest(GameLogic &logic, AITactic &tac, const Coord3D &start, const Coord3D &target)
{
	pincerWaypoints(logic, tac, start, target);
}

const AIStatCollector &AITacticalAI::recordOf(GameLogic &logic, int playerIndex)
{
	static const AIStatCollector kNone(-1);
	const AIStatCollector *r = logic.skirmishAI().record(playerIndex);
	return r ? *r : kNone;
}

namespace
{
// RW 0x9A3040 (RotWK-only, the team builder's last step; lane DECOMP-2): the units of the player info record's army list (INFERENCE: the list RW 0x6A9999 ->
// record + 0 -> + 4 is the army list, S-418) in order that are in the default team (+ 0x31C == player + 0x30C; INFERENCE: in no team of the tactics), have any
// of CAN_ATTACK, HERO, SUPPORT and not STRUCTURE (template + 0x108 bit 7), are not IS_LEAVING_FACTORY (status 0x5A), were never sent (+ 0x18) and are 10 s old
// (RW 0xDEBE5C) are each sent once (+ 0x18 keeps them until they are gone) to an attacked-at record of the brain (+ 0x164 -> + 0x20; none: nothing): the
// last record whose enemies hold the category the unit's ThreatBreakdown category counters (0: PIKEMAN + 0x28; 1: INFANTRY + 0x20 or PIKEMAN; 2: CAVALRY + 0x2C;
// 3: INFANTRY or category 1 + 0x24; 7: categories 6 / 9 / 11, + 0x38 / + 0x44 / + 0x4C), else the nearest record (squared 2D distance RW 0x66137C, x87; the first
// of equal ones); the unit attack-moves to its position with INT_MAX shots from the PLAYER (RW 0x9A328E: RW 0x696266(pos, 0x7FFFFFFF, 0), AI command 0xF)
void dispatchIdleUnits(GameLogic &logic, AISkirmishPlayer &ai, const std::vector<ObjectID> &army)
{
	AIBrainState &b = ai.brain;
	const Kinds &k = kinds();
	static const int leavingFactory = CombatNames::status("IS_LEAVING_FACTORY");
	std::vector<Object *> units;
	for (ObjectID id : army)
	{
		Object *o = logic.findObjectByID(id);
		if (!o || inAnyTeamOf(b, id) || !(is(*o, k.canAttack) || is(*o, k.hero) || is(*o, k.support)) || is(*o, k.structure))
		{
			continue;
		}
		if ((leavingFactory >= 0 && o->testStatus((unsigned)leavingFactory)) || b.dispatched.count(id) != 0)
		{
			continue;
		}
		if (logic.getFrame() - o->getCreationFrame() < (std::uint32_t)(kLogicFramesPerSecond * 10))
		{
			continue;
		}
		units.push_back(o);
	}
	if (units.empty() || b.attackedAt.empty())
	{
		return;
	}
	for (Object *o : units)
	{
		b.dispatched.insert(o->getID()); // RW 0x9A318D
		const int cat = o->getTemplate() ? AIThreatFinder::threatCategory(*o->getTemplate()) : -1;
		const AIAttackedAt *nearest = nullptr, *match = nullptr;
		float best = -1.0f; // RW 0xBD19DC
		for (const AIAttackedAt &r : b.attackedAt)
		{
			// RW 0x66137C: (o.y - p.y)^2 + (o.x - p.x)^2 at PC24, compared in the register, stored with fst
			const double dx = SimMath::pc24SubW((double)o->getPosition()->x, (double)r.position.x);
			const double dy = SimMath::pc24SubW((double)o->getPosition()->y, (double)r.position.y);
			const double d = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx));
			if ((double)best > d || 0.0f > best) // RW 0x9A31E4 (fcompi, ja) / 0x9A31EA (comiss 0, best; jbe)
			{
				best = SimMath::fstpDword(d);
				nearest = &r;
			}
			const float *v = r.enemies.v;
			bool counters = false;
			switch (cat)
			{
			case 0:
				counters = v[3] > 0.0f;
				break;
			case 1:
				counters = v[1] > 0.0f || v[3] > 0.0f;
				break;
			case 2:
				counters = v[4] > 0.0f;
				break;
			case 3:
				counters = v[1] > 0.0f || v[2] > 0.0f;
				break;
			case 7:
				counters = v[7] > 0.0f || v[10] > 0.0f || v[12] > 0.0f;
				break;
			default:
				break;
			}
			if (counters)
			{
				match = &r;
			}
		}
		const AIAttackedAt *goal = match ? match : nearest;
		attackMove(logic, std::vector<ObjectID>{ o->getID() }, goal->position, CMD_FROM_PLAYER);
		++b.idleDispatches;
	}
}
} // namespace

void AITacticalAI::updateTeamBuilder(GameLogic &logic, AISkirmishPlayer &ai)
{
	AIBrainState &b = ai.brain;
	const Player *player = playerOf(logic, ai.playerIndex);
	if (!player || ai.store->data().disableTeamBuilding)
	{
		return;
	}
	const std::vector<ObjectID> army = recordOf(logic, ai.playerIndex).army(); // the record + 0 -> + 4 (a copy: the loops below give orders)
	const Kinds &k = kinds();
	static const std::vector<int> supportHorde = { k.support, kindBit("HORDE") };
	for (AITactic &tac : b.tactics)
	{
		for (AITacticTeam &team : tac.teams)
		{
			if (!team.wantsMembers)
			{
				continue;
			}
			// RW 0x9A3373 .. 0x9A3392: an OPPORTUNITY team (+ 0x2CC == 2) first clamps its maximum to 2 (RW 0x9A3345, unsigned), then RW 0x9A32C1 -> RW 0x9A2E06
			if (team.type == kOpportunity && (std::uint32_t)team.maximum > 2u)
			{
				team.maximum = 2;
			}
			const std::uint32_t maximum = (std::uint32_t)team.maximum;
			std::uint32_t count = teamUnitCount(logic, team); // RW 0x9A32C1 -> RW 0x9A24A6
			// RW 0x9A2E06: the army list in order while count < maximum (unsigned; -1: no maximum): a CAN_ATTACK or SUPPORT unit that RW 0x9A24FC accepts joins
			// (Object::setTeam RW 0x69954A); a SUPPORT unit only when the team holds a member that is neither SUPPORT nor HORDE, its maximum is above 1 and it holds
			// no SUPPORT HORDE (RW 0x9A2F0A .. 0x9A2FA4, RW 0x7A0555 twice)
			for (ObjectID id : army)
			{
				if (!(count < maximum))
				{
					break;
				}
				const Object *o = logic.findObjectByID(id);
				if (!o || !(is(*o, k.canAttack) || is(*o, k.support)) || !recruitable(logic, b, team, *o))
				{
					continue;
				}
				if (is(*o, k.support))
				{
					if (teamMembersWith(logic, team, {}, supportHorde) == 0 || !(maximum > 1u) || teamMembersWith(logic, team, supportHorde, {}) >= 1)
					{
						continue;
					}
				}
				team.members.push_back(id);
				++count;
			}
			// RW 0x9A2FC0: an ENEMY_STRUCTURE or DEFENSIVE team (+ 0x2CC 0 / 1) takes every HERO (template + 0x113 bit 2) RW 0x9A24FC accepts, with no maximum
			if (team.type == kEnemyStructure || team.type == kDefensive)
			{
				for (ObjectID id : army)
				{
					const Object *o = logic.findObjectByID(id);
					if (o && is(*o, k.hero) && recruitable(logic, b, team, *o))
					{
						team.members.push_back(id);
					}
				}
			}
		}
	}
	// RW 0x9A33A7 .. 0x9A3447: a team that wants members is handed over (RW 0x6C779B) when its count (RW 0x9A24A6) is at least its minimum (unsigned) and it is
	// DEFENSIVE (+ 0x2CC == 1), or has reached its maximum, or meets its target's threat (RW 0x9A26A1); RW 0x79FB50 (the unit-info counts of a scripted team) is
	// 0 for every tactical team (no info)
	for (AITactic &tac : b.tactics)
	{
		for (AITacticTeam &team : tac.teams)
		{
			if (!team.wantsMembers)
			{
				continue;
			}
			const std::uint32_t count = teamUnitCount(logic, team);
			if (count >= (std::uint32_t)team.minimum &&
				(team.type == kDefensive || count >= (std::uint32_t)team.maximum || teamMeetsThreat(logic, tac, team)))
			{
				team.wantsMembers = false;
				team.handedOver = true; // RW 0x6C779B
			}
		}
	}
	// RW 0x9A3462 .. 0x9A34A3: the sent units whose object is gone leave the set; then RW 0x9A3040 (RW 0x9A34AC)
	for (auto it = b.dispatched.begin(); it != b.dispatched.end();)
	{
		it = logic.findObjectByID(*it) ? std::next(it) : b.dispatched.erase(it);
	}
	dispatchIdleUnits(logic, ai, army);
}

void AITacticalAI::updateBrain(GameLogic &logic, AISkirmishPlayer &ai, AISkirmishPlayer *first)
{
	AIBrainState &b = ai.brain;
	Player *player = playerOf(logic, ai.playerIndex);
	if (!player || !ai.army || !ai.store || ai.store->data().disableTacticalAI)
	{
		return;
	}
	AISkirmishPlayer *owner = b.chooser ? &ai : first;
	updateAttackedAt(logic, ai); // RW 0x6C7344 (lane AI-3)
	if (b.chooser)
	{
		if (!b.chooserInitialised)
		{
			initChooser(logic, ai, b);
		}
		updateEnemies(logic, ai, b);
		updateChooser(logic, ai, b);
	}
	// RW 0x90CB85: RW 0x90BA94 (ended tactics leave), RW 0x90B8C7 (each tactic: the offensive list + 0x10, the defensive + 0x28 (empty: S-417), the expansion
	// + 0x40 (lane AIEXPAND-1), the targetless + 0x58), RW 0x90C5E2 (the targetless pick, lane AI-2 r3)
	b.tactics.erase(std::remove_if(b.tactics.begin(), b.tactics.end(), [](const AITactic &t) { return t.ended; }), b.tactics.end());
	for (int list : kTacticLists)
	{
		for (size_t i = 0; i < b.tactics.size(); ++i)
		{
			AITactic &tac = b.tactics[i];
			if (tacticList(tac) != list)
			{
				continue;
			}
			AISkirmishPlayer *targetOwner = tac.targetless || tac.targetOwner == ai.playerIndex ? &ai : first;
			if (targetOwner)
			{
				updateTactic(logic, ai, b, tac, targetOwner->brain);
			}
		}
	}
	pickTargetless(logic, ai);
	if (!owner)
	{
		return;
	}
	if (b.chooser)
	{
		b.bestTarget = bestTarget(logic, ai);
	}
	const int best = owner->brain.bestTarget;
	if (best < 0 || best >= (int)owner->brain.targets.size())
	{
		return;
	}
	// RW 0x90CB9F
	const int type = owner->brain.targets[(size_t)best].type;
	if (type == kEnemyStructure || type == kOpportunity)
	{
		enemyStructureTactic(logic, ai, b, owner->brain, best, owner->playerIndex, *player);
	}
	else if (type == kExpansion)
	{
		expansionTactic(logic, ai, b, owner->brain, best, owner->playerIndex, *player); // RW 0x90C50C (lane AIEXPAND-1)
	}
	else
	{
		++b.unportedTypes; // DEFENSIVE RW 0x90C436: S-417
	}
	// RW 0x90CCCB: not ported (S-417)
}

void AITacticalAI::updateIdleArmy(GameLogic &logic, AISkirmishPlayer &ai)
{
	// RW 0x99E63B: the record (+ 0x10) cleared (RW 0x7ED8C1), then each object of the army list (+ 4; dropped ids already gone, RW 0x65BD07) whose team is the
	// player's default team (+ 0x30C; here: in no tactic team) adds its threat value (RW 0x7EDB72: only a positive one, with its category)
	ai.brain.idleArmy = AIThreatFinder::ThreatRecord{};
	const Player *player = playerOf(logic, ai.playerIndex);
	if (!player)
	{
		return;
	}
	for (ObjectID id : recordOf(logic, ai.playerIndex).army())
	{
		const Object *o = logic.findObjectByID(id);
		if (!o || !o->getTemplate() || inAnyTeamOf(ai.brain, id))
		{
			continue;
		}
		const float v = AIThreatFinder::threatValue(*o);
		if (0.0f < v)
		{
			AIThreatFinder::accumulate(ai.brain.idleArmy, AIThreatFinder::threatCategory(*o->getTemplate()), v);
		}
	}
}

void AITacticalAI::makeGenerator(GameLogic &logic, AISkirmishPlayer &ai)
{
	// RW 0x90CADA: the offensive (RW 0x90BE31), defensive (RW 0x90C04E), expansion (RW 0x90C09E) and targetless (RW 0x90C0EE) prototypes; of their constructors only
	// FarmKillSquad's (RW 0x9BB3CA) draws: + 0x60 = GetGameLogicRandomValue(1, 100) (AIFarmKillSquad.cpp line 0x39) < 95
	ai.brain.generatorMade = true;
	ai.brain.farmKillEarly = logic.random().getValue(1, 100, kFarmKillFile, 0x39) < 95;
}

void AITacticalAI::crc(const AIBrainState &b, StateHasher &h)
{
	h.addU32(0x41494252u); // "AIBR"
	h.addBool(b.chooser);
	h.addBool(b.chooserInitialised);
	h.addU32((std::uint32_t)b.enemies.size());
	for (const auto &e : b.enemies)
	{
		h.addI32(e.first);
		h.addFloat(e.second);
	}
	h.addI32(b.currentEnemy);
	h.addU32((std::uint32_t)b.targets.size());
	for (const AITarget &t : b.targets)
	{
		h.addI32(t.type);
		h.addU32(t.frame);
		h.addFloat(t.position.x);
		h.addFloat(t.position.y);
		h.addFloat(t.position.z);
		h.addBool(t.inactive);
		h.addBool(t.dropped);
		h.addI32(t.teams);
		h.addI32(t.maxTeams);
		h.addFloat(t.radius);
		h.addFloat(t.finderPosition.x);
		h.addFloat(t.finderPosition.y);
		h.addFloat(t.finderPosition.z);
		h.addFloat(t.threat);
		h.addU32(t.object);
		h.addU32(t.id);
	}
	h.addU32(b.nextTargetId);
	h.addI32(b.bestTarget);
	h.addU32((std::uint32_t)b.tactics.size());
	for (const AITactic &tac : b.tactics)
	{
		h.addString(tac.kind);
		h.addI32(tac.kindIndex);
		h.addU32(tac.id);
		h.addI32(tac.targetIndex);
		h.addI32(tac.targetOwner);
		h.addBool(tac.started);
		h.addBool(tac.ended);
		h.addBool(tac.ready);
		h.addU32(tac.readyFrame);
		h.addBool(tac.success);
		for (const Coord3D *c : { &tac.position, &tac.origin })
		{
			h.addFloat(c->x);
			h.addFloat(c->y);
			h.addFloat(c->z);
		}
		h.addBool(tac.retreated);
		h.addI32(tac.step);
		h.addI32(tac.step2);
		h.addU32((std::uint32_t)tac.waypoints.size());
		for (const Coord3D &c : tac.waypoints)
		{
			h.addFloat(c.x);
			h.addFloat(c.y);
			h.addFloat(c.z);
		}
		for (const Coord3D *c : { &tac.gatherPos, &tac.aimPos })
		{
			h.addFloat(c->x);
			h.addFloat(c->y);
			h.addFloat(c->z);
		}
		h.addBool(tac.mainLaunched);
		h.addU32(tac.launchFrame);
		if (tac.kindIndex == SIMPLE_EXPANSION && !tac.targetless)
		{
			h.addBool(tac.request != nullptr); // lane AIEXPAND-1: SimpleExpansion's request (+ 0x58)
			if (tac.request)
			{
				AIBaseBuilder::crcRequest(*tac.request, h);
			}
		}
		h.addU32((std::uint32_t)tac.teams.size());
		for (const AITacticTeam &team : tac.teams)
		{
			h.addString(team.name);
			h.addI32(team.type);
			h.addI32(team.minimum);
			h.addI32(team.maximum);
			h.addU32((std::uint32_t)team.requiredKinds.size());
			for (int bit : team.requiredKinds)
			{
				h.addI32(bit);
			}
			h.addU32((std::uint32_t)team.forbiddenKinds.size());
			for (int bit : team.forbiddenKinds)
			{
				h.addI32(bit);
			}
			h.addI32(team.priority);
			h.addBool(team.wantsMembers);
			h.addBool(team.handedOver);
			h.addFloat(team.lastPosition.x);
			h.addFloat(team.lastPosition.y);
			h.addFloat(team.lastPosition.z);
			h.addI32(team.idleFrames);
			h.addI32(team.index);
			h.addU32((std::uint32_t)team.members.size());
			for (ObjectID id : team.members)
			{
				h.addU32(id);
			}
		}
	}
	h.addU32(b.nextTacticId);
	h.addU32((std::uint32_t)b.tacticsByKind.size());
	for (const auto &kv : b.tacticsByKind)
	{
		h.addString(kv.first);
		h.addI32(kv.second);
	}
	h.addU64(b.targetsSet);
	h.addU64(b.tacticsStarted);
	h.addU64(b.attacksLaunched);
	h.addU64(b.tacticsEnded);
	h.addU64(b.gateRefusals);
	h.addU64(b.unportedTypes);
	h.addBool(b.generatorMade);
	h.addBool(b.farmKillEarly);
	h.addU32((std::uint32_t)b.namedCounters.size());
	for (const auto &kv : b.namedCounters)
	{
		h.addString(kv.first);
		h.addI32(kv.second);
	}
	h.addU64(b.retreats);
	h.addU32((std::uint32_t)b.dispatched.size());
	for (ObjectID id : b.dispatched)
	{
		h.addU32(id);
	}
	h.addU64(b.idleDispatches);
	h.addU32((std::uint32_t)b.attackedAt.size());
	for (const AIAttackedAt &e : b.attackedAt)
	{
		h.addU32(e.id);
		h.addFloat(e.position.x);
		h.addFloat(e.position.y);
		h.addFloat(e.position.z);
		h.addFloat(e.radius);
		h.addU32(e.frame);
		for (const AIThreatFinder::ThreatRecord *r : { &e.enemies, &e.others })
		{
			for (float f : r->v)
			{
				h.addFloat(f);
			}
		}
	}
	for (float f : b.idleArmy.v)
	{
		h.addFloat(f);
	}
	h.addU64(b.gateDraws);
	h.addU64(b.gateRejects);
	h.addU64(b.merges);
	h.addU64(b.mergeTargetlessUnported);
	h.addU64(b.targetlessPicks);
	h.addU64(b.targetlessStarted);
	h.addU64(b.targetlessUnported);
	for (const AITactic &tac : b.tactics)
	{
		h.addBool(tac.targetless);
		h.addU32(tac.squadTarget);
	}
}
