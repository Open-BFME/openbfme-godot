// OpenBFME. GPL-3.0.
//
// AISpecialPowerUpdate's decisions ported by lane AIPOW-1 (stop S-1421's 39 kinds): the slot 0x1C of the classes RW 0x992724 makes (the files
// SkirmishAI\AISpecialPowers\AISpecialPowerInstances\*.cpp, RW 0xC8E600 names one). Ported from the RotWK binary (caveat S-001); Open-BFME-2 has no matching
// source. Every float operation names its retail instruction class (SSE scalar or x87 under precision control 24); the scans are the partition manager's
// range query sorted near to far (RW 0xA39340 order 1), so their order is deterministic.

#include "GameLogic/Module/AISpecialPowerUpdate.h"

#include "Common/BuildAssistant.h"
#include "Common/GameCommon.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/RandomValue.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Module/AISpecialPowerInternal.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Weapon.h"

#include <algorithm>
#include <stdexcept>

using namespace AISpecialPowerDetail;

namespace
{
// SSE: (a - b) per axis, then dz * dz + dy * dy, + dx * dx (RW 0x9EA347 and its siblings: subss / mulss / addss in that order)
float distSqr3Sse(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y), dz = SimMath::subf32(a.z, b.z);
	return SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
}

// TARGET: Coord3D::GetLengthEstimate, RW 0x4035AC: |the larger of x, y| + 0.25 * |the smaller|, then the same with that result and |z| (the 0.25 is
// RW 0xBD1904; every step is stored as a float32, the x87 runs at 24-bit precision)
float lengthEstimate3D(float x, float y, float z)
{
	const auto absf = [](float a) { return a < 0.0f ? SimMath::subf32(0.0f, a) : a; };
	const auto estimate = [](float a, float b) { return a > b ? SimMath::addf32(a, SimMath::mulf32(b, 0.25f)) : SimMath::addf32(b, SimMath::mulf32(a, 0.25f)); };
	return estimate(estimate(absf(x), absf(y)), absf(z));
}

// the rank of the object's ExperienceTracker (Object + 0x26C, + 0x24). INFERENCE: every retail object has a tracker (ZH Object's constructor makes one); a
// missing one reads as rank 0
int rankOf(const Object &o)
{
	const ExperienceTracker *x = o.getExperienceTracker();
	return x ? x->getRank() : 0;
}
} // namespace

bool AISpecialPowerUpdate::decideElendil(Object &obj)
{
	// RW 0x9EC22C: a goal; the body ratio (x87, wide) below 0.7 (RW 0xC8E78C); the ALLIES and ENEMIES (mask 6) within the radius + 0x18: an enemy (Object
	// RW 0x68D7AB == ENEMIES) counts when its rank (+ 0x26C -> + 0x24) is below 2 and its template is not BANNER (template + 0x11C bit 4); every other hit
	// counts on the other side. Yes when the others are fewer than the counted enemies.
	if (!goalObject(obj))
	{
		return false;
	}
	if (!(bodyRatioWide(obj) < (double)0.7f))
	{
		return false;
	}
	static const int kBanner = kind("BANNER");
	unsigned enemies = 0, others = 0;
	for (const PartitionHit &hit : scan(obj, m_decision->radius, 6u))
	{
		if (obj.getRelationship(*hit.object) == ENEMIES)
		{
			if (rankOf(*hit.object) < 2 && !hasKind(*hit.object, kBanner))
			{
				++enemies;
			}
		}
		else
		{
			++others;
		}
	}
	return others < enemies;
}

bool AISpecialPowerUpdate::decideSelfAoeHealHeros(Object &obj)
{
	// RW 0x9EBCBB: t = (float)(GameLogicRandomValueReal(-0.1, 0.1) "AISpecialPowerSelfAoEHealHeros.cpp" line 0x24 + 0.5) (x87 fadd, fstp dword); the
	// object's health (body slot 0x10, stored float) over its max health (slot 0x1C, x87 fdiv under PC24): below t -> yes; else the ALLIES (mask 2) within
	// the radius + 0x18, near to far: a HERO other than the object whose ratio is below t -> yes.
	GameLogic &logic = obj.logic();
	const float roll = logic.random().getValueReal(-0.1f, 0.1f, "AISpecialPowerSelfAoEHealHeros.cpp", 0x24);
	const float t = SimMath::fstpDword(SimMath::pc24AddW((double)roll, (double)0.5f));
	auto ratio = [](const Object &o) {
		const BodyModuleInterface *body = o.getBodyModule();
		return body ? SimMath::pc24DivW((double)body->getHealth(), (double)body->getMaxHealth()) : 0.0; // INFERENCE: retail calls through the body (every unit has one)
	};
	if (!((double)t <= ratio(obj)))
	{
		return true;
	}
	static const int kHero = kind("HERO");
	for (const PartitionHit &hit : scan(obj, m_decision->radius, 2u))
	{
		if (hit.object == &obj || !hasKind(*hit.object, kHero))
		{
			continue;
		}
		if (!((double)t <= ratio(*hit.object)))
		{
			return true;
		}
	}
	return false;
}

bool AISpecialPowerUpdate::decideToggleWeaponSet(Object &obj, bool siege)
{
	// RW 0x9EA2E1: the object's current weapon (RW 0x68B58C: WeaponSet + 0x354 [+ 0x36C]; none -> no), an AI and a goal; the weapon template's MeleeWeapon
	// (+ 0x125, RW 0x9EA2C6) clear -> slot 0x20, set -> slot 0x24.
	const ObjectWeapons *w = obj.getWeapons();
	const Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !obj.getAIUpdateInterface())
	{
		return false;
	}
	Object *goal = goalObject(obj);
	if (!goal)
	{
		return false;
	}
	static const int kStructure = kind("STRUCTURE");
	const float d2 = distSqr3Sse(*goal->getPosition(), *obj.getPosition());
	if (!weapon->getTemplate()->m_meleeWeapon)
	{
		if (siege)
		{
			// RW 0x9EA201: a goal that is not STRUCTURE (template + 0x108 bit 7) within 150 (squared 22500.0, RW 0xC8E390)
			return !hasKind(*goal, kStructure) && d2 <= 22500.0f;
		}
		return d2 <= 2500.0f; // RW 0x9EA347: within 50 (RW 0xC8E3C4)
	}
	if (siege)
	{
		return hasKind(*goal, kStructure); // RW 0x9EA280: the goal is a STRUCTURE
	}
	return 22500.0f <= d2; // RW 0x9EA3BA: at least 150 away (RW 0xC8E3C8)
}

bool AISpecialPowerUpdate::decideCharge(Object &obj)
{
	// RW 0x9EA0A1 (an object decision, target at + 0x20): with a goal (RW 0x9930A3) the target is cleared and the ENEMIES (mask 4) within the range + 0x14,
	// near to far, are read: no target yet -> the hit becomes it; else the hit's ratio (body slot 0x14, fstp dword) against the target's (wide): the first hit
	// whose ratio is below the target's becomes the target and ends the scan. Yes when the target (by id, RW 0x8DBACE) exists.
	GameLogic &logic = obj.logic();
	if (goalObject(obj))
	{
		m_decision->target = INVALID_ID; // RW 0xA027D5(0)
		for (const PartitionHit &hit : scan(obj, m_decision->range, 4u))
		{
			Object *current = logic.findObjectByID(m_decision->target);
			if (!current)
			{
				m_decision->target = hit.object->getID();
				continue;
			}
			const float candidate = SimMath::fstpDword(bodyRatioWide(*hit.object));
			if (bodyRatioWide(*current) <= (double)candidate)
			{
				continue;
			}
			m_decision->target = hit.object->getID();
			break;
		}
	}
	return logic.findObjectByID(m_decision->target) != nullptr;
}

bool AISpecialPowerUpdate::decideDominate(Object &obj, bool atLocation)
{
	// RW 0x9E9633 (DOMINATE_TROLL, the target object) / RW 0x9E9880 (DOMINATE_ENEMY / TAME_THE_BEAST, a location): the ENEMIES and NEUTRAL (mask 0xC) within
	// the range + 0x14, near to far; fewer than 2 hits (the count as a float against 2.0, RW 0xC8E2B8 / 0xC8E30C) -> no. Each hit that is a HORDE or is not
	// a horde's member (RW 0x693A1A(0)) counts; by the variant (the constructor's argument): 0 any but DOZER, HERO, STRUCTURE, ARMY_OF_DEAD and MOVE_ONLY;
	// 1 the templates named "IsengardWargRiderHorde" or "NeutralWarg" (BINARY FACT: the names are the binary's, RW 0xC8E2E8 / 0xC8E2DC); 2 TROLL; then a
	// ratio (body slot 0x14, x87) above 0.5 (RW 0xC8E2B4 / 0xC8E308) and a cost (RW 0x73C25F without a player: the template's BuildCost, cvtsi2ss) above
	// the best so far -> the best. At the end, at least 2 counted and a best: the target (object) / RW 0xA0229F at the best's position (location).
	static const int kHorde = kind("HORDE"), kDozer = kind("DOZER"), kHero = kind("HERO"), kStructure = kind("STRUCTURE"), kArmyOfDead = kind("ARMY_OF_DEAD"),
					 kMoveOnly = kind("MOVE_ONLY"), kTroll = kind("TROLL");
	const PartitionHits hits = scan(obj, m_decision->range, 0xCu);
	if (hits.size() < 2)
	{
		return false;
	}
	unsigned counted = 0;
	float best = 0.0f;
	Object *bestObject = nullptr;
	for (const PartitionHit &hit : hits)
	{
		Object &c = *hit.object;
		if (!hasKind(c, kHorde) && c.getHordeObject(false))
		{
			continue;
		}
		++counted;
		bool candidate = false;
		switch (m_decision->variant)
		{
		case 0:
			candidate = !hasKind(c, kDozer) && !hasKind(c, kHero) && !hasKind(c, kStructure) && !hasKind(c, kArmyOfDead) && !hasKind(c, kMoveOnly);
			break;
		case 1:
			candidate = c.getTemplate()->getName() == "IsengardWargRiderHorde" || c.getTemplate()->getName() == "NeutralWarg";
			break;
		case 2:
			candidate = hasKind(c, kTroll);
			break;
		default:
			break;
		}
		if (!candidate || !(bodyRatioWide(c) > (double)0.5f))
		{
			continue;
		}
		const float cost = (float)BuildAssistant::calcCostToBuild(*c.getTemplate(), nullptr, nullptr, -1); // cvtsi2ss (exact below 2^24)
		if (!(cost > best))
		{
			continue;
		}
		best = cost;
		bestObject = &c;
	}
	if (counted < 2 || !bestObject)
	{
		return false;
	}
	if (atLocation)
	{
		return setLocationIfValid(obj, *bestObject->getPosition());
	}
	m_decision->target = bestObject->getID(); // RW 0xA027D5
	return true;
}

// ---- the location base (RW 0xA01C6B) ----------------------------------------------------------------------------------------------------------------------

bool AISpecialPowerUpdate::locationValid(Object &obj, const Coord3D &loc)
{
	// RW 0xA021D8: the ActionManager's test (RW 0x75CF21(button, obj, 0, loc, 0)); with SpellMakesAStructure (+ 0x1E) no live STRUCTURE (KindOf bit 7, RW
	// 0x444D39(0, 7) / 0x445139) within 150 (RW 0xC041F8) of the location (distance type 0, near to far)
	if (!m_decision->button || !ActionManager::canDoSpecialPowerAtLocation(obj, *m_decision->button, loc))
	{
		return false;
	}
	if (m_decision->makesStructure)
	{
		static const int kStructure = kind("STRUCTURE");
		PartitionFilterFn structure([](Object &o) { return hasKind(o, kStructure); });
		PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
		if (!obj.logic().partition().iterateObjectsInRange(loc, 150.0f, FROM_CENTER_2D, { &structure, &alive }, ITER_SORTED_NEAR_TO_FAR).empty())
		{
			return false;
		}
	}
	return true;
}

bool AISpecialPowerUpdate::setLocationIfValid(Object &obj, const Coord3D &loc)
{
	// RW 0xA0229F
	m_proposedForTest = loc;
	if (locationValid(obj, loc))
	{
		m_decision->location = loc;
		return true;
	}
	if (!m_decision->randomize) // + 0x1D
	{
		return false;
	}
	// RW 0xA01C9C: two GameLogicRandomValueReal(-1, 1) "AISPecialPowerTargetAoE.cpp" lines 0x96 / 0x97 (fstp dword), z 0, normalised (RW 0x403175)
	GameLogic &logic = obj.logic();
	Coord3D dir;
	dir.x = logic.random().getValueReal(-1.0f, 1.0f, "AISPecialPowerTargetAoE.cpp", 0x96);
	dir.y = logic.random().getValueReal(-1.0f, 1.0f, "AISPecialPowerTargetAoE.cpp", 0x97);
	dir.z = 0.0f;
	ObjectKnockback::normalize(dir);
	// d = 15, 30, .. 150 (RW 0xBDC6CC steps up to RW 0xC041F8, SSE): loc + dir * d (mulss, addss per axis)
	for (float d = 15.0f; d <= 150.0f; d = SimMath::addf32(d, 15.0f))
	{
		const Coord3D p{ SimMath::addf32(loc.x, SimMath::mulf32(dir.x, d)), SimMath::addf32(loc.y, SimMath::mulf32(dir.y, d)),
			             SimMath::addf32(loc.z, SimMath::mulf32(dir.z, d)) };
		if (locationValid(obj, p))
		{
			m_decision->location = p;
			return true;
		}
	}
	return false;
}

unsigned AISpecialPowerUpdate::clusterScan(Object &obj, float offset, unsigned mask, ClusterCount what, bool validate, size_t *hitsTotal)
{
	// the base direction (1, 1, 0) normalised (RW 0x403175), times the offset (mulss); the four points (x, y), (-x, y), (-x, -y), (x, -y) (0 - v, subss),
	// each plus the object's position (addss per axis)
	Coord3D d{ 1.0f, 1.0f, 0.0f };
	ObjectKnockback::normalize(d);
	const float x = SimMath::mulf32(d.x, offset), y = SimMath::mulf32(d.y, offset), z = SimMath::mulf32(d.z, offset);
	const float nx = SimMath::subf32(0.0f, x), ny = SimMath::subf32(0.0f, y);
	const Coord3D offsets[4] = { { x, y, z }, { nx, y, 0.0f }, { nx, ny, 0.0f }, { x, ny, 0.0f } };
	const Coord3D *pos = obj.getPosition();
	static const int kArcher = kind("ARCHER");
	unsigned best = 0;
	if (hitsTotal)
	{
		*hitsTotal = 0;
	}
	for (const Coord3D &o : offsets)
	{
		const Coord3D p{ SimMath::addf32(o.x, pos->x), SimMath::addf32(o.y, pos->y), SimMath::addf32(o.z, pos->z) };
		const PartitionHits hits = scanAt(obj.logic(), obj.getControllingPlayer(), p, offset, mask);
		if (hitsTotal)
		{
			*hitsTotal += hits.size(); // RW 0x5F1B20: every hit of every point
		}
		unsigned n = 0;
		for (const PartitionHit &h : hits)
		{
			switch (what)
			{
			case COUNT_ALL:
				++n;
				break;
			case COUNT_ARCHERS:
				n += hasKind(*h.object, kArcher) ? 1u : 0u;
				break;
			case COUNT_HORDES_OR_LOOSE:
				n += (h.object->getHordeObject(false) == h.object || !h.object->getContainedBy()) ? 1u : 0u;
				break;
			}
		}
		if (hits.empty() || !(best < n))
		{
			continue;
		}
		if (!validate)
		{
			best = n;
			m_decision->location = p;
		}
		else if (setLocationIfValid(obj, p))
		{
			best = n;
		}
	}
	return best;
}

bool AISpecialPowerUpdate::decideRangedAoe(Object &obj, float range, bool unitsOnly)
{
	// RW 0x9EB53B (RANGED_AOE_ATTACK, the range + 0x14) / RW 0x9EADC8 (LEGOLAS_ARROWWIND, the range times 2.0, mulss) / RW 0x9EB36E (RANGED_AOE_ATTACK_UNITS:
	// also KindOf CAN_ATTACK or HERO (RW 0x46E72F(0, 3, 0x5A) -> filter RW 0xC309B4: any of) and neither STRUCTURE nor IGNORES_SELECT_ALL (RW 0x46E72F(0, 7,
	// 0x5B) -> filter RW 0xC110A4: none of)): the nearest live ENEMY (mask 4) within the range; none, or nearer than 0.8 of the range ((d.z^2 + d.y^2) + d.x^2
	// below (range * 0.8)^2, SSE, RW 0xBDE8D8) -> no; else RW 0xA0229F at its position
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	static const int kCanAttack = kind("CAN_ATTACK"), kHero = kind("HERO"), kStructure = kind("STRUCTURE"), kIgnoresSelectAll = kind("IGNORES_SELECT_ALL");
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, 4u); });
	PartitionFilterFn anyOf([&](Object &o) { return !unitsOnly || hasKind(o, kCanAttack) || hasKind(o, kHero); });
	PartitionFilterFn noneOf([&](Object &o) { return !unitsOnly || !(hasKind(o, kStructure) || hasKind(o, kIgnoresSelectAll)); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj.getPosition(), range, FROM_CENTER_2D, { &relation, &anyOf, &noneOf, &alive },
	                                                                   ITER_SORTED_NEAR_TO_FAR);
	if (hits.empty())
	{
		return false;
	}
	const Object &h = *hits[0].object;
	const float r = SimMath::mulf32(range, 0.8f);
	if (distSqr3Sse(*h.getPosition(), *obj.getPosition()) < SimMath::mulf32(r, r))
	{
		return false;
	}
	return setLocationIfValid(obj, *h.getPosition());
}

bool AISpecialPowerUpdate::decideAoeAndBuff(Object &obj)
{
	// RW 0x9EB0B8: the live ALLIES (mask 2) within the range with the unit filters of RANGED_AOE_ATTACK_UNITS; fewer than 2 (as a float against 2.0, RW
	// 0xC8E514) -> no; then the RANGED_AOE_ATTACK_UNITS test (the nearest live enemy unit at least 0.8 of the range away -> RW 0xA0229F at its position)
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	static const int kCanAttack = kind("CAN_ATTACK"), kHero = kind("HERO"), kStructure = kind("STRUCTURE"), kIgnoresSelectAll = kind("IGNORES_SELECT_ALL");
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, 2u); });
	PartitionFilterFn anyOf([&](Object &o) { return hasKind(o, kCanAttack) || hasKind(o, kHero); });
	PartitionFilterFn noneOf([&](Object &o) { return !(hasKind(o, kStructure) || hasKind(o, kIgnoresSelectAll)); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	if (logic.partition().iterateObjectsInRange(*obj.getPosition(), m_decision->range, FROM_CENTER_2D, { &relation, &anyOf, &noneOf, &alive }, ITER_SORTED_NEAR_TO_FAR)
	        .size() < 2)
	{
		return false;
	}
	return decideRangedAoe(obj, m_decision->range, true);
}

// ---- the spell book decisions that need no AIPlayer state -------------------------------------------------------------------------------------------------

bool AISpecialPowerUpdate::decideCaptureCreep(Object &obj)
{
	// RW 0x9E86BE: the first player of ThePlayerList (index order, RW 0x6A844E) that is not a playable side (RW 0x6AAC66) and holds the object's player as
	// ENEMIES (RW 0x6ACEAF == 0); its default team (Player + 0x30C, none -> no): the first member in retail's order (newest first, Team::getLastMember) that
	// is a STRUCTURE whose position the object's player does not see SHROUDED (ShroudManager RW 0xB4D9A0 != 2) -> RW 0xA0229F at its position; else no
	GameLogic &logic = obj.logic();
	const Player *mine = obj.getControllingPlayer();
	const Player *creep = nullptr;
	for (int i = 0; i < logic.players().getPlayerCount() && !creep; ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && !(p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide) && p->getRelationship(mine) == ENEMIES)
		{
			creep = p;
		}
	}
	if (!creep || !creep->getDefaultTeam() || !mine || !logic.shroud())
	{
		return false;
	}
	static const int kStructure = kind("STRUCTURE");
	for (Object *m = creep->getDefaultTeam()->getLastMember(); m; m = m->friend_teamPrev())
	{
		if (!hasKind(*m, kStructure))
		{
			continue;
		}
		if ((int)logic.shroud()->getStatusAt(mine->getPlayerIndex(), m->getPosition()->x, m->getPosition()->y) != CELLSHROUD_SHROUDED)
		{
			return setLocationIfValid(obj, *m->getPosition());
		}
	}
	return false;
}

bool AISpecialPowerUpdate::decideShroudReveal(Object &obj)
{
	// RW 0x9E7CA3 (only after logic frame LOGICFRAMES_PER_SECOND * 10 = 50: the bound RW 0xDEC148, written by the startup initializer RW 0xBCA357 that
	// RW 0xD8AB88 registers; `jbe` skips while the frame is not above it, before any draw): a random start waypoint (GameLogicRandomValue(0, n - 1)
	// "AISpellBookShroudReveal.cpp" line 0x11D, TheTerrainLogic vslot 0x8C by id); the object's player not seeing it CLEAR (RW 0xB4D9A0 != 0) and a live
	// ENEMY (mask 4) within 300 of it (RW 0xC8DFB0) -> the waypoint plus a random direction (RW 0xA01C9C) times GameLogicRandomValueReal(30, 180) line 0x12D
	// (SSE: mulss, then addss to the waypoint) -> RW 0xA0229F there
	GameLogic &logic = obj.logic();
	if (logic.getFrame() <= 10u * LOGICFRAMES_PER_SECOND)
	{
		return false;
	}
	const std::vector<int> &ids = m_decision->waypointIds;
	const int pick = logic.random().getValue(0, (int)ids.size() - 1, "AISpellBookShroudReveal.cpp", 0x11D);
	if (pick < 0 || pick >= (int)ids.size() || !logic.terrain())
	{
		throw std::logic_error("AISpecialPowerUpdate SPELLBOOK_SHROUD_REVEAL (RW 0x9E7CA3): the map has no Player_1_Start waypoint (retail reads past its list)");
	}
	const Waypoint *w = logic.terrain()->findWaypointById(ids[(size_t)pick]);
	const Player *mine = obj.getControllingPlayer();
	if (!w || !mine || !logic.shroud())
	{
		throw std::logic_error("AISpecialPowerUpdate SPELLBOOK_SHROUD_REVEAL (RW 0x9E7CA3): no waypoint, player or shroud");
	}
	if ((int)logic.shroud()->getStatusAt(mine->getPlayerIndex(), w->location.x, w->location.y) == CELLSHROUD_CLEAR)
	{
		return false;
	}
	PartitionFilterFn relation([&](Object &o) { return relationIn(mine, o, 4u); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	if (logic.partition().iterateObjectsInRange(w->location, 300.0f, FROM_CENTER_2D, { &relation, &alive }, ITER_FASTEST).empty())
	{
		return false;
	}
	Coord3D dir;
	dir.x = logic.random().getValueReal(-1.0f, 1.0f, "AISPecialPowerTargetAoE.cpp", 0x96);
	dir.y = logic.random().getValueReal(-1.0f, 1.0f, "AISPecialPowerTargetAoE.cpp", 0x97);
	dir.z = 0.0f;
	ObjectKnockback::normalize(dir);
	const float mag = logic.random().getValueReal(30.0f, 180.0f, "AISpellBookShroudReveal.cpp", 0x12D);
	const Coord3D p{ SimMath::addf32(SimMath::mulf32(dir.x, mag), w->location.x), SimMath::addf32(SimMath::mulf32(dir.y, mag), w->location.y),
		             SimMath::addf32(SimMath::mulf32(dir.z, mag), w->location.z) };
	return setLocationIfValid(obj, p);
}

// ---- the spell book decisions over the computer's army and structure lists (lane AIPOW-1 r2) ---------------------------------------------------------------
//
// The lists are the skirmish AI's player info record (TheSkirmishAIManager + 0xA3C by player index, RW 0x6A9999): + 0 -> + 4 the army, + 8 -> + 4 the
// structures, read through AITacticalAI::recordOf (lane DECOMP-3: the ported record, filled by the object events in RW's order).

namespace
{
bool playableSide(const Player *p)
{
	return p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide; // RW 0x6AAC66: template + 0x151
}

// RW 0xA0241D (BFME2 decomp Rva005EEA20Find.cpp, tier B edited: RotWK adds the last test): the first army unit in list order that is not a STRUCTURE,
// not a HERO unless `allowHero`, whose AI's current victim exists, belongs to a playable side, is not a STRUCTURE unless `allowStructureVictim` and, with
// `requireLiveVictim`, is not effectively dead (Object + 0x458 bit 0)
Object *firstBusyArmyUnit(GameLogic &logic, const Player &player, bool allowHero, bool allowStructureVictim, bool requireLiveVictim)
{
	static const int kStructure = kind("STRUCTURE"), kHero = kind("HERO");
	for (ObjectID id : AITacticalAI::recordOf(logic, player.getPlayerIndex()).army())
	{
		Object *o = logic.findObjectByID(id);
		if (!o || hasKind(*o, kStructure) || (!allowHero && hasKind(*o, kHero)))
		{
			continue;
		}
		Object *victim = goalObject(*o);
		if (!victim || !playableSide(victim->getControllingPlayer()))
		{
			continue;
		}
		if ((!allowStructureVictim && hasKind(*victim, kStructure)) || (requireLiveVictim && victim->isEffectivelyDead()))
		{
			continue;
		}
		return o;
	}
	return nullptr;
}

// RW 0xA0260A: around `unit`, within `radius`, its controlling player's ALLIES and ENEMIES (mask 6) alive (unsorted, RW 0xA39340 order 0) that are not
// STRUCTURE and are CAN_ATTACK, HERO or SUPPORT: those the unit holds as ENEMIES (RW 0x68D7AB) count in `enemies` and add their ratio (a horde member's or a
// horde's: the horde contain's member average, slot 0x264; else the body's slot 0x14; x87 fadd, fstp dword) to `enemyRatio`, the others to `others` /
// `otherRatio`; each sum then divided by its count (fild of the unsigned count, fdivr, fstp) when the count is not 0
struct BattleStats
{
	unsigned others = 0;
	float otherRatio = 0.0f;
	unsigned enemies = 0;
	float enemyRatio = 0.0f;
};

BattleStats battleStats(GameLogic &logic, Object &unit, float radius)
{
	static const int kStructure = kind("STRUCTURE"), kCanAttack = kind("CAN_ATTACK"), kHero = kind("HERO"), kSupport = kind("SUPPORT");
	BattleStats st;
	for (const PartitionHit &h : scanAtUnsorted(logic, unit.getControllingPlayer(), *unit.getPosition(), radius, 6u))
	{
		Object &c = *h.object;
		if (hasKind(c, kStructure) || !(hasKind(c, kCanAttack) || hasKind(c, kHero) || hasKind(c, kSupport)))
		{
			continue;
		}
		double ratio;
		if (Object *horde = c.getHordeObject(false)) // RW 0x693A1A(0)
		{
			const HordeContain *hc = dynamic_cast<const HordeContain *>(horde->getContain());
			if (!hc)
			{
				throw std::logic_error("AISpecialPowerUpdate RW 0xA0260A: a horde without a horde contain");
			}
			ratio = (double)hc->averageMemberHealthRatio(); // slot 0x264
		}
		else
		{
			ratio = bodyRatioWide(c);
		}
		if (unit.getRelationship(c) == ENEMIES)
		{
			++st.enemies;
			st.enemyRatio = SimMath::fstpDword(SimMath::pc24AddW(ratio, (double)st.enemyRatio));
		}
		else
		{
			++st.others;
			st.otherRatio = SimMath::fstpDword(SimMath::pc24AddW(ratio, (double)st.otherRatio));
		}
	}
	if (st.enemies != 0)
	{
		st.enemyRatio = SimMath::fstpDword(SimMath::pc24DivW((double)st.enemyRatio, SimMath::fildU32(st.enemies)));
	}
	if (st.others != 0)
	{
		st.otherRatio = SimMath::fstpDword(SimMath::pc24DivW((double)st.otherRatio, SimMath::fildU32(st.others)));
	}
	return st;
}

// RW 0x9E880B (BFME2 decomp AISpellBookAssistBattle.cpp, tier B same-shape): the player's entry of the timer list (a new one is { index, now }); when its
// frame has come: the next frame is now + GameLogicRandomValue(15, 50) (RW 0xDEC14C / 0xDEC150: LOGICFRAMES_PER_SECOND * 3.0 / * 10.0 at startup, RW
// 0xBCA3B3 / 0xBCA3CA) "AISpellBookAssistBattle.cpp" line 0x50, and the answer is yes
bool assistTimerReady(GameLogic &logic, const Player &player)
{
	std::vector<std::pair<int, unsigned>> &timers = logic.skirmishAI().spellBookAssistTimers();
	std::pair<int, unsigned> *found = nullptr;
	for (std::pair<int, unsigned> &t : timers)
	{
		if (t.first == player.getPlayerIndex())
		{
			found = &t;
			break;
		}
	}
	if (!found)
	{
		timers.push_back({ player.getPlayerIndex(), logic.getFrame() });
		found = &timers.back();
	}
	if (found->second > logic.getFrame())
	{
		return false;
	}
	const int r = logic.random().getValue(3 * LOGICFRAMES_PER_SECOND, 10 * LOGICFRAMES_PER_SECOND, "AISpellBookAssistBattle.cpp", 0x50);
	found->second = logic.getFrame() + (unsigned)r;
	return true;
}
} // namespace

bool AISpecialPowerUpdate::decideAssistBattle(Object &obj)
{
	// RW 0x9E896E (BUFF: variant 0, DEBUFF: variant 1): the first busy army unit (RW 0xA0241D(player, DEBUFF, BUFF, no)); its battle stats within 350 (RW
	// 0xC53B00); with enemies there and its own side's average ratio below 0.99 (RW 0xC8E0B8, x87 compare) and the timer ready (RW 0x9E880B): + 0x38 the
	// unit, + 0x3C the unit (BUFF) or its victim (DEBUFF), and RW 0xA0229F at that one's position
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const bool buff = m_decision->variant == 0;
	Object *unit = firstBusyArmyUnit(logic, *player, !buff, buff, false);
	if (!unit)
	{
		return false;
	}
	const BattleStats st = battleStats(logic, *unit, 350.0f);
	if (st.enemies == 0 || !(st.otherRatio < 0.99f) || !assistTimerReady(logic, *player))
	{
		return false;
	}
	m_decision->assistUnit = unit->getID();
	Object *at = buff ? unit : goalObject(*unit);
	if (!at)
	{
		throw std::logic_error("AISpecialPowerUpdate RW 0x9E896E: the busy unit lost its victim");
	}
	m_decision->assistTarget = at->getID();
	return setLocationIfValid(obj, *at->getPosition());
}

bool AISpecialPowerUpdate::decideBuffTerrain(Object &obj)
{
	// RW 0x9E77EE: the first busy army unit (RW 0xA0241D(player, 1, 1, 0)) that is not a HERO -> RW 0xA0229F at its position
	const Player *player = obj.getControllingPlayer();
	static const int kHero = kind("HERO");
	Object *unit = player ? firstBusyArmyUnit(obj.logic(), *player, true, true, false) : nullptr;
	if (!unit || hasKind(*unit, kHero))
	{
		return false;
	}
	return setLocationIfValid(obj, *unit->getPosition());
}

bool AISpecialPowerUpdate::decideRebuild(Object &obj)
{
	// RW 0x9E7744: the first structure of the player's list (record + 8) whose ratio (body slot 0x14, x87) is below 0.5 (RW 0xBD869C) -> RW 0xA0229F there
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	for (ObjectID id : AITacticalAI::recordOf(logic, player->getPlayerIndex()).structures())
	{
		Object *o = logic.findObjectByID(id);
		if (o && bodyRatioWide(*o) < (double)0.5f)
		{
			return setLocationIfValid(obj, *o->getPosition());
		}
	}
	return false;
}

bool AISpecialPowerUpdate::decideEnshroudingMist(Object &obj)
{
	// RW 0x9E7869 with RW 0xA0258F: the army units that are not STRUCTURE whose victim belongs to a playable side; the first of them whose template's AIKindOf
	// (+ 0x530) is 1 (ARCHER) -> RW 0xA0229F at its position
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	static const int kStructure = kind("STRUCTURE");
	for (ObjectID id : AITacticalAI::recordOf(logic, player->getPlayerIndex()).army())
	{
		Object *o = logic.findObjectByID(id);
		if (!o || hasKind(*o, kStructure))
		{
			continue;
		}
		Object *victim = goalObject(*o);
		if (!victim || !playableSide(victim->getControllingPlayer()))
		{
			continue;
		}
		if (AIThreatFinder::threatCategory(*o->getTemplate()) == 1)
		{
			return setLocationIfValid(obj, *o->getPosition());
		}
	}
	return false;
}

bool AISpecialPowerUpdate::decideCallTheHorde(Object &obj)
{
	// RW 0x9E71B0: the player's money (Player + 0x94, signed) above 1000 and 1 - usage / limit of its command points (Player + 0x60: + 8 the usage, RW
	// 0x6A7B9F the limit; cvtsi2ss, divss, subss) above 0.25 (RW 0xBD1904)
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const int money = (int)player->getMoney()->countMoney();
	const float used = (float)player->commandPoints().getUsage();
	const float limit = (float)logic.economy().commandPointLimit(*player);
	if (money <= 1000)
	{
		return false;
	}
	return SimMath::subf32(1.0f, SimMath::divf32(used, limit)) > 0.25f;
}

// ---- the economy debuffs (lane RAI-POW-2) --------------------------------------------------------------------------------------------------------------------

namespace
{
// RW 0x6AA5F5 (Player + 0x2FC's vslot 0x30) -> RW 0x8F47EB: the controlling player's AIPlayer enemy; null when the player has no skirmish AI
const Player *aiEnemy(GameLogic &logic, const Player &player)
{
	AISkirmishPlayer *ai = logic.skirmishAI().findAI(player.getPlayerIndex());
	return ai ? AITacticalAI::enemyPlayer(logic, *ai) : nullptr;
}

// RW 0xB4D9A0: the position is SHROUDED to the viewer
bool shroudedFor(const GameLogic &logic, const Player &viewer, const Coord3D &position)
{
	return !logic.shroud() || (int)logic.shroud()->getStatusAt(viewer.getPlayerIndex(), position.x, position.y) == CELLSHROUD_SHROUDED;
}
} // namespace

bool AISpecialPowerUpdate::decideDebuffEconomy(Object &obj)
{
	// RW 0x9E74B7: the controlling player's AIPlayer enemy (RW 0x6AA5F5 -> RW 0x8F47EB) and its record's economy list (record + 0xC, RW 0x72B229); each
	// object that resolves, that the caster's player does not see SHROUDED and that is not UNDER_CONSTRUCTION (RW 0x44DDEC(object, 2)) weighs its
	// PRODUCTION attribute product (RW 0x68C82D(object, 0xD, out = 1.0, 0, 1)); the largest weight (strictly greater) whose position RW 0xA0229F accepts
	// wins. The answer is whether one was accepted
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const Player *enemy = aiEnemy(logic, *player);
	if (!enemy)
	{
		return false;
	}
	const AIStatCollector *record = logic.skirmishAI().record(enemy->getPlayerIndex()); // RW 0x6A9999
	if (!record)
	{
		return false; // INFERENCE: every playable player has a record (RW 0x8E46D9)
	}
	static const int kUnderConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	float best = 0.0f;
	const Object *bestObject = nullptr;
	for (ObjectID id : record->economy()) // RW 0x72B229
	{
		Object *o = logic.findObjectByID(id); // RW 0x449681
		if (!o)
		{
			continue;
		}
		if (shroudedFor(logic, *player, *o->getPosition()))
		{
			continue;
		}
		if (o->testStatus((unsigned)kUnderConstruction)) // RW 0x44DDEC(object, 2)
		{
			continue;
		}
		float weight = 1.0f; // RW 0xBD1908
		o->attributeModifierProduct(0xD, nullptr, true, weight); // RW 0x68C82D(0xD, &weight, 0, 1)
		if (!(weight > best))
		{
			continue;
		}
		if (!setLocationIfValid(obj, *o->getPosition())) // RW 0xA0229F
		{
			continue;
		}
		best = weight;
		bestObject = o;
	}
	return bestObject != nullptr;
}

bool AISpecialPowerUpdate::decideDebuffProduction(Object &obj)
{
	// RW 0x9E726A: the same enemy (RW 0x6AA5F5) and its record's structure list (record + 8); each visible (RW 0xB4D9A0 != 2) FS_FACTORY (template + 0x10F
	// bit 7, KindOf 63) whose position RW 0xA0229F accepts; the weight is the count of the caster's player's ENEMIES within 200 (RW 0xC8DE70) that are alive
	// and FS_FACTORY (the query's KindOf mask); the largest count (strictly greater) wins
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const Player *enemy = aiEnemy(logic, *player);
	if (!enemy)
	{
		return false;
	}
	const AIStatCollector *record = logic.skirmishAI().record(enemy->getPlayerIndex());
	if (!record)
	{
		return false; // INFERENCE: every playable player has a record (RW 0x8E46D9)
	}
	static const int kFsFactory = kind("FS_FACTORY");
	unsigned best = 0;
	const Object *bestObject = nullptr;
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, 4u); }); // RW 0xC1676C mask 4
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });        // RW 0xC10E20
	PartitionFilterFn factory([](Object &o) { return hasKind(o, kFsFactory); });       // the query's KindOf mask
	for (ObjectID id : record->structures())
	{
		Object *o = logic.findObjectByID(id);
		if (!o || !hasKind(*o, kFsFactory))
		{
			continue;
		}
		if (shroudedFor(logic, *player, *o->getPosition()))
		{
			continue;
		}
		const unsigned count = (unsigned)logic.partition()
		                           .iterateObjectsInRange(*o->getPosition(), 200.0f, FROM_CENTER_2D, { &relation, &alive, &factory }, ITER_FASTEST)
		                           .size(); // RW 0xC8DE70
		if (count <= best)
		{
			continue;
		}
		if (!setLocationIfValid(obj, *o->getPosition())) // RW 0xA0229F
		{
			continue;
		}
		best = count;
		bestObject = o;
	}
	return bestObject != nullptr;
}

bool AISpecialPowerUpdate::decideBuffEconomy(Object &obj)
{
	// RW 0x9E7671: the player info record's economy list (record + 0xC, RW 0x99E738; add RW 0x99E84E -> filter RW 0x99E818: neither LINKED_TO_FLAG nor
	// UNATTACKABLE, a STRUCTURE, ECONOMY_STRUCTURE or SUPPLY_GATHERING_CENTER), in order (lane DECOMP-3: the ported record's list): the first that resolves and
	// is not yet in the decision's set (+ 0x28, a std::set of ids) where RW 0xA0229F accepts its position joins the set -> yes
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	std::vector<Object *> economy;
	for (ObjectID id : AITacticalAI::recordOf(logic, player->getPlayerIndex()).economy())
	{
		if (Object *o = logic.findObjectByID(id))
		{
			economy.push_back(o);
		}
	}
	for (Object *o : economy)
	{
		if (m_decision->buffedIds.count(o->getID()) == 0 && setLocationIfValid(obj, *o->getPosition()))
		{
			m_decision->buffedIds.insert(o->getID());
			return true;
		}
	}
	return false;
}

// ---- SOUL_FREEZE: the team's special-power registry (lane RAI-POW) ------------------------------------------------------------------------------------------

bool AISpecialPowerUpdate::decideSoulFreeze(Object &obj)
{
	// RW 0x9E9B37: the caster's team (Object + 0x31C) and the decision's power template (+ 0x8) are looked up in the team's registry (RW 0x7A3EF5,
	// null object); a record of another caster answers no. Then the ALLIES and ENEMIES (relationship mask 6) within the range (+ 0x14), near to far,
	// that are alive and not STRUCTURE / IGNORES_SELECT_ALL but CAN_ATTACK or HERO; no hit answers no. A disabled caster (Object + 0x1C8, RW 0x9325B4)
	// skips every hit's accounting, which leaves the enemy total 0.
	Team *team = obj.getTeam();
	const Player *player = obj.getControllingPlayer();
	if (!team || !player)
	{
		return false;
	}
	Object *registered = team->findSpecialPowerRecord(m_decision->powerType, nullptr, obj.logic());
	if (registered && registered != &obj)
	{
		return false;
	}
	static const int kCanAttack = kind("CAN_ATTACK"), kHero = kind("HERO"), kStructure = kind("STRUCTURE"), kIgnoresSelectAll = kind("IGNORES_SELECT_ALL");
	const float radius = m_decision->range;
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, 6u); });
	PartitionFilterFn anyOf([&](Object &o) { return hasKind(o, kCanAttack) || hasKind(o, kHero); });
	PartitionFilterFn noneOf([&](Object &o) { return !(hasKind(o, kStructure) || hasKind(o, kIgnoresSelectAll)); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	const PartitionHits hits = obj.logic().partition().iterateObjectsInRange(*obj.getPosition(), radius, FROM_CENTER_2D, { &relation, &anyOf, &noneOf, &alive },
	                                                                       ITER_SORTED_NEAR_TO_FAR);
	if (hits.empty() || obj.getDisabledMask() != 0)
	{
		return false;
	}
	// the cell width: __ftol2(x87(radius * the double 2/3 = 0x3FE5555555555555, RW 0xC8E360)), not a float32 2/3
	const int width = (int)SimMath::ftol2(SimMath::pc24MulW((double)radius, 0.66666666666666663));
	if (width == 0)
	{
		throw std::logic_error("AISpecialPowerUpdate SOUL_FREEZE (RW 0x9E9B37): the range " + std::to_string(radius) + " gives a zero cell width (retail divides by zero)");
	}
	const Coord3D origin{ SimMath::subf32(obj.getPosition()->x, radius), SimMath::subf32(obj.getPosition()->y, radius), obj.getPosition()->z };
	// retail's two 3x3 grids are 0x24 bytes apart and index x * 3 + y without a bound: a hit at the far edge bins x == 3, so the enemies' indexes 9 ..
	// 11 land in the allies' cells 0 .. 2 (RW 0x9E9D79); the allies' own 9 .. 11 land in the partition iterator's dead alive filter, so they are
	// dropped here. Both max searches read only cells 0 .. 8 of each grid.
	int otherCell[9] = {};
	int alliedCell[9] = {};
	unsigned alliedTotal = 0, otherTotal = 0;
	float metric = 0.0f; // the allies' summed body ratio, never averaged
	for (const PartitionHit &hit : hits)
	{
		Object &o = *hit.object;
		const Coord3D &p = *o.getPosition();
		const int gx = SimMath::cvttss2si(SimMath::subf32(p.x, origin.x)) / width;
		const int gy = SimMath::cvttss2si(SimMath::subf32(p.y, origin.y)) / width;
		const int index = gx * 3 + gy;
		if (index < 0 || index > 11)
		{
			throw std::logic_error("AISpecialPowerUpdate SOUL_FREEZE (RW 0x9E9B37): a hit bins outside retail's " + std::to_string(index) + " (the scan's boundary)");
		}
		const unsigned cost = (unsigned)BuildAssistant::calcCostToBuild(*o.getTemplate(), nullptr, nullptr, -1); // RW 0x73C25F(template, 0, 0, -1): the unsigned short BuildCost
		const bool support = hasKind(o, kind("SUPPORT")); // template + 0x123 bit 2
		if (player->getRelationship(o.getTeam()) == ALLIES) // Player::getRelationship(Object*) RW 0x6ADC43
		{
			metric = SimMath::fstpDword(SimMath::pc24AddW(bodyRatioWide(o), (double)metric));
			if (index < 9)
			{
				alliedCell[index] = (int)((unsigned)alliedCell[index] + cost);
			}
			if (support)
			{
				alliedTotal = (std::uint32_t)SimMath::ftol2Low32(SimMath::pc24AddW(SimMath::pc24MulW((double)(int)cost, 0.5), SimMath::fildU32(alliedTotal)));
			}
			else
			{
				alliedTotal += cost;
			}
		}
		else
		{
			if (index < 9)
			{
				otherCell[index] = (int)((unsigned)otherCell[index] + cost);
			}
			else
			{
				alliedCell[index - 9] = (int)((unsigned)alliedCell[index - 9] + cost); // retail's spill over the grid boundary
			}
			if (support)
			{
				otherTotal = (std::uint32_t)SimMath::ftol2Low32(SimMath::pc24AddW(SimMath::pc24MulW((double)(int)cost, 0.5), SimMath::fildU32(otherTotal)));
			}
			else
			{
				otherTotal += cost;
			}
		}
	}
	if (otherTotal == 0)
	{
		return false;
	}
	// RW 0x9E9E55: the enemy total above twice the ally total (x87, unsigned fild), or the caster has a current victim
	bool useDifference = SimMath::fildU32(otherTotal) > SimMath::pc24AddW(SimMath::fildU32(alliedTotal), SimMath::fildU32(alliedTotal));
	if (!useDifference)
	{
		useDifference = goalObject(obj) != nullptr; // RW 0x9930A3
	}
	int x = 1, y = 1;
	if (useDifference)
	{
		// RW 0x9E9E91: the largest (enemies - allies) cost, x outer then y, strict >, the centre when none
		int best = 0;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				const int d = otherCell[i * 3 + j] - alliedCell[i * 3 + j];
				if (d > best)
				{
					best = d;
					x = i;
					y = j;
				}
			}
		}
		// RW 0x9E9ED7: the winner counts when its enemies are at least twice its allies (cvtsi2ss, mulss 2.0, comiss)
		const float enemies = SimMath::sseFromInt32(otherCell[x * 3 + y]);
		const float allies = SimMath::mulf32(SimMath::sseFromInt32(alliedCell[x * 3 + y]), 2.0f); // RW 0xC8E338
		useDifference = !(enemies < allies);
	}
	if (!useDifference)
	{
		// RW 0x9E9F68: the allies' summed ratio below 0.65 (RW 0xC8E33C, ordered: a NaN metric fails), then the largest (enemies + allies) cost
		if (!(metric < 0.65f))
		{
			return false;
		}
		int best = 0;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				const int s = otherCell[i * 3 + j] + alliedCell[i * 3 + j];
				if (s > best)
				{
					best = s;
					x = i;
					y = j;
				}
			}
		}
	}
	// RW 0x9E9F68: the point at the cell's index times the RANGE (not the bin width) from the origin (cvtsi2ss, mulss, addss per axis)
	const Coord3D target{ SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(x), radius), origin.x),
		                  SimMath::addf32(SimMath::mulf32(SimMath::sseFromInt32(y), radius), origin.y), origin.z };
	if (!setLocationIfValid(obj, target))
	{
		return false;
	}
	// RW 0x9EA028: the five-word record joins the team's registry after the location was accepted
	team->addSpecialPowerRecord(m_decision->powerType, obj.logic().getFrame(), obj.getID());
	return true;
}

// RW 0x9E691D: ATTACK_HEAL_AOE (the Angmar Necromancer's Well of Souls, INI `AI_SPECIAL_POWER_ATTACK_HEAL_AOE`). The team's registry (RW 0x7A3EF5,
// "+ 0x140") must hold no record of the power at all. The first of the team's orders (Team + 0x13C) whose command is 0x11 or 0x31 and whose payload
// gives a position (bit 0x80 of node + 0x13, the three words at node + 0x18) or an object id (bit 0x40, node + 0x24; a dead id does NOT fall back to
// the position) is the spot. RW 0x68B3AB reserves that goal (the pathfinder's updateGoal), then the estimated 3D length from the caster to it
// (Coord3D::GetLengthEstimate, RW 0x4035AC) must be below distanceLimit (+ 0x18) * 0.9 (RW 0xBDE198). The first enemy (relationship mask 4, alive,
// near to far) within the range (+ 0x14) around the CASTER (not the order) must pass RW 0xA0229F; only then the order is erased (RW 0x7A1C5E).
bool AISpecialPowerUpdate::decideAttackHealAoe(Object &obj)
{
	GameLogic &logic = obj.logic();
	Team *team = obj.getTeam();
	const Player *player = obj.getControllingPlayer();
	// RW 0x9E6930..0x9E6941 uses the persisted power type; the template cache is not needed after snapshot load.
	if (!team || !player)
	{
		return false;
	}
	if (team->findSpecialPowerRecord(m_decision->powerType, nullptr, logic))
	{
		return false; // RW 0x9E6941: any record of the power rejects, the caster's own included
	}
	Coord3D position{};
	size_t found = team->orders().size();
	for (size_t i = 0; i < team->orders().size(); ++i)
	{
		const Team::Order &order = team->orders()[i];
		if (order.command != 0x11 && order.command != 0x31)
		{
			continue;
		}
		if (order.hasTarget() && order.target != INVALID_ID) // RW 0x9E6991: zero falls back to the position payload
		{
			Object *target = logic.findObjectByID(order.target); // RW 0x9E69A2 -> RW 0x449681
			if (!target)
			{
				continue; // no position fallback (RW 0x9E69B0)
			}
			position = *target->getPosition();
		}
		else if (order.hasPosition()) // RW 0x79F922
		{
			position = order.position;
		}
		else
		{
			continue;
		}
		found = i;
		break; // only the first usable order
	}
	if (found == team->orders().size())
	{
		return false;
	}
	// RW 0x68B3AB: the goal reservation before the distance test; the wrapper resolves the point's layer (RW 0x680A75) and reserves the goal
	if (AIUpdateInterface *ai = obj.getAIUpdateInterface())
	{
		Pathfinder &pf = ai->world().pathfinder();
		pf.updateGoal(ai->pathfindObject(), &position, pf.getLayerAt(ai->pathfindObject(), position)); // RW 0x8E28AC: RW 0x680A75(object, point)
	}
	const Coord3D &p = *obj.getPosition();
	const float dx = SimMath::subf32(p.x, position.x), dy = SimMath::subf32(p.y, position.y), dz = SimMath::subf32(p.z, position.z);
	const float limit = SimMath::mulf32(m_decision->radius, 0.9f); // RW 0xBDE198
	if (!(lengthEstimate3D(dx, dy, dz) < limit))
	{
		return false;
	}
	// RW 0x9E6A64 .. 0x9E6AD7: the first alive enemy (mask 4) within the range + 0x14 of the caster, near to far
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, 4u); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	const PartitionHits hits = logic.partition().iterateObjectsInRange(p, m_decision->range, FROM_CENTER_2D, { &relation, &alive }, ITER_SORTED_NEAR_TO_FAR);
	if (hits.empty())
	{
		return false;
	}
	if (!setLocationIfValid(obj, *hits[0].object->getPosition())) // RW 0x9E6AE2 -> RW 0xA0229F
	{
		return false;
	}
	team->eraseOrder(found); // RW 0x9E6AFD -> RW 0x7A1C5E
	return true;
}

// ---- the structure breakers: the enemy record's position sampler (lane RAI-POW-3b) ---------------------------------------------------------------

namespace
{
// RW 0xA024DA (AISpellBookBase::findDenseStructureCenter): the enemy Player's record (RW 0x6A9999) and its position sampler (record + 0x10); in cluster
// order the first cluster whose object count (RW 0x97D700: (members.end - members.begin) / 4) is above the best so far (strictly) and whose centre
// (RW 0x9E7071: the group + 0x08) the viewer does not see SHROUDED (RW 0xB4D9A0 != 2) wins. `out` is zeroed first, as retail does; false when no
// cluster has a member. The groups come from AIPositionSampler (the defence-records PR): its Group carries the retail group's + 0x08 centre and its
// + 0x1C / + 0x20 members; the + 0x18 "used" byte is set and read only by RW 0x9E7095 / 0x9E70A6, which none of the three decisions calls.
bool findDenseStructureCenter(Object &caster, const Player &recordPlayer, Coord3D &out)
{
	GameLogic &logic = caster.logic();
	const Player *viewer = caster.getControllingPlayer();
	out = Coord3D{};
	if (!viewer || !logic.shroud())
	{
		return false;
	}
	AIStatCollector *record = logic.skirmishAI().record(recordPlayer.getPlayerIndex());
	if (!record)
	{
		return false; // INFERENCE: retail reads through a null record; every playable player has one (RW 0x8E46D9)
	}
	unsigned best = 0;
	bool found = false;
	Coord3D bestPosition{};
	for (const AIPositionSampler::Group &cluster : record->positions().groups())
	{
		const unsigned count = (unsigned)cluster.members.size();
		if (count <= best)
		{
			continue;
		}
		if ((int)logic.shroud()->getStatusAt(viewer->getPlayerIndex(), cluster.center.x, cluster.center.y) == CELLSHROUD_SHROUDED)
		{
			continue;
		}
		best = count;
		bestPosition = cluster.center;
		found = true;
	}
	if (found)
	{
		out = bestPosition;
	}
	return found;
}

// RW 0x6C7807: the AI's current enemy player; null when it has none
const Player *enemyPlayerOf(GameLogic &logic, const AISkirmishPlayer &ai)
{
	const int enemy = AITacticalAI::enemyOfGroup(ai);
	return enemy >= 0 ? logic.players().getNthPlayer(enemy) : nullptr;
}
} // namespace

bool AISpecialPowerUpdate::decideStructureBreaker(Object &obj)
{
	// RW 0x9E7A98: the controlling player's AI (RW 0x6A950B) and its current enemy (RW 0x6C7807); the enemy record's densest structure cluster
	// (RW 0xA024DA) -> RW 0xA0229F at its centre
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const AISkirmishPlayer *ai = logic.skirmishAI().findAI(player->getPlayerIndex());
	if (!ai)
	{
		return false;
	}
	const Player *enemy = enemyPlayerOf(logic, *ai);
	if (!enemy)
	{
		return false;
	}
	Coord3D position{};
	if (!findDenseStructureCenter(obj, *enemy, position))
	{
		return false;
	}
	return setLocationIfValid(obj, position); // RW 0xA0229F
}

bool AISpecialPowerUpdate::decideStructureBreakerWalls(Object &obj)
{
	// RW 0x9E7942: the first busy army unit (RW 0xA0241D(player, hero, any victim, live)); its player's ENEMIES (mask 4) within 500 (RW 0xC8DF5C),
	// alive, of KindOf WALK_ON_TOP_OF_WALL (60), DEFENSIVE_WALL (61), WALL_UPGRADE (150), WALL_HUB (156) or WALL_SEGMENT (189) (RW 0x9E7985's mask),
	// near to far: the first -> RW 0xA0229F. No busy unit or no wall falls back to RW 0x9E7A98 (RW 0x9E7A5F)
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	Object *unit = firstBusyArmyUnit(logic, *player, true, true, false);
	if (unit)
	{
		static const int kWalkOnTopOfWall = kind("WALK_ON_TOP_OF_WALL"), kDefensiveWall = kind("DEFENSIVE_WALL"), kWallUpgrade = kind("WALL_UPGRADE"),
		                 kWallHub = kind("WALL_HUB"), kWallSegment = kind("WALL_SEGMENT");
		for (const PartitionHit &hit : scanAt(logic, player, *unit->getPosition(), 500.0f, 4u)) // RW 0xC8DF5C
		{
			const Object &wall = *hit.object;
			if (hasKind(wall, kWalkOnTopOfWall) || hasKind(wall, kDefensiveWall) || hasKind(wall, kWallUpgrade) || hasKind(wall, kWallHub) ||
			    hasKind(wall, kWallSegment))
			{
				return setLocationIfValid(obj, *wall.getPosition()); // RW 0xA0229F
			}
		}
	}
	return decideStructureBreaker(obj); // RW 0x9E7A5F
}

bool AISpecialPowerUpdate::decideStructureBaseKill(Object &obj)
{
	// RW 0x9E6EF4: the first busy army unit (RW 0xA0241D(player, hero, any victim, live)); its player's ENEMIES (mask 4) within 500 (RW 0xC8DDF8),
	// alive, KindOf DOZER_FACTORY (194) (RW 0x9E6F36's mask), near to far: the first -> RW 0xA0229F. None (or no busy unit) then asks RW 0x9E6E8E
	// whether the enemy record's structures still list a DOZER_FACTORY: if so no, else the densest structure cluster (RW 0x9E7A98)
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const AISkirmishPlayer *ai = logic.skirmishAI().findAI(player->getPlayerIndex());
	if (!ai)
	{
		return false;
	}
	static const int kDozerFactory = kind("DOZER_FACTORY");
	Object *unit = firstBusyArmyUnit(logic, *player, true, true, false);
	if (unit)
	{
		for (const PartitionHit &hit : scanAt(logic, player, *unit->getPosition(), 500.0f, 4u)) // RW 0xC8DDF8
		{
			if (hasKind(*hit.object, kDozerFactory))
			{
				return setLocationIfValid(obj, *hit.object->getPosition()); // RW 0xA0229F
			}
		}
	}
	// RW 0x9E6E8E: the current enemy (RW 0x6C7807) and its record's structure list (record + 8)
	if (const Player *enemy = enemyPlayerOf(logic, *ai))
	{
		const AIStatCollector *record = logic.skirmishAI().record(enemy->getPlayerIndex());
		if (!record)
		{
			return false; // INFERENCE: every playable player has a record (RW 0x8E46D9)
		}
		for (ObjectID id : record->structures())
		{
			const Object *structure = logic.findObjectByID(id);
			if (structure && hasKind(*structure, kDozerFactory))
			{
				return false;
			}
		}
	}
	return decideStructureBreaker(obj); // RW 0x9E7010
}

// ---- CITADEL (lane RAI-POW-2, community #234) -------------------------------------------------------------------------------------------------------------------------------

bool AISpecialPowerUpdate::decideCitadel(Object &obj)
{
	// RW 0x9E6CCB: the controlling player's AI (RW 0x6A950B), its base position (RW 0x8F0284: the base builder's start) and its current enemy
	// (RW 0x6C7807); the enemy record's structure list (record + 8): the first DOZER_FACTORY (template + 0x120 bit 2) is the target. Two draws of
	// GameLogicRandomValueReal (RW 0x6D332C) "AISpellBookCitadel.cpp" (the binary stores the full build path): line 0x49, range (0, 1), places a point along
	// the base -> target line; line 0x51, range (-1, 1) (the low bound is RW 0xBD19DC = -1.0f), offsets it by the cross product of the base -> target
	// vector with (0, 0, t2) (RW 0x9E6DE0 .. 0x9E6E44, SSE, the zero products kept); then RW 0xA0229F
	GameLogic &logic = obj.logic();
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return false;
	}
	const AISkirmishPlayer *ai = logic.skirmishAI().findAI(player->getPlayerIndex());
	if (!ai)
	{
		return false;
	}
	const int enemyIndex = AITacticalAI::enemyOfGroup(*ai);
	if (enemyIndex < 0)
	{
		return false;
	}
	const AIStatCollector *record = logic.skirmishAI().record(enemyIndex); // RW 0x6A9999
	if (!record)
	{
		return false; // INFERENCE: every playable player has a record (RW 0x8E46D9)
	}
	static const int kDozerFactory = kind("DOZER_FACTORY");
	const Object *target = nullptr;
	for (ObjectID id : record->structures())
	{
		const Object *o = logic.findObjectByID(id);
		if (o && hasKind(*o, kDozerFactory))
		{
			target = o;
			break;
		}
	}
	if (!target)
	{
		return false; // RW 0x9E6D3A
	}
	const Coord3D &base = ai->build.startPosition; // RW 0x8F0284 -> RW 0x90CD45
	const Coord3D &goal = *target->getPosition();
	const float dx = SimMath::subf32(goal.x, base.x), dy = SimMath::subf32(goal.y, base.y), dz = SimMath::subf32(goal.z, base.z);
	const float t1 = logic.random().getValueReal(0.0f, 1.0f, "AISpellBookCitadel.cpp", 0x49);
	const Coord3D point{ SimMath::addf32(base.x, SimMath::mulf32(dx, t1)), SimMath::addf32(base.y, SimMath::mulf32(dy, t1)),
		                 SimMath::addf32(base.z, SimMath::mulf32(dz, t1)) };
	const float t2 = logic.random().getValueReal(-1.0f, 1.0f, "AISpellBookCitadel.cpp", 0x51);
	// x += t2 * dy - dz * 0; y += dz * 0 - t2 * dx; z += dx * 0 - dy * 0
	const float zdz = SimMath::mulf32(dz, 0.0f);
	const Coord3D offset{ SimMath::addf32(point.x, SimMath::subf32(SimMath::mulf32(t2, dy), zdz)),
		                  SimMath::addf32(SimMath::subf32(zdz, SimMath::mulf32(t2, dx)), point.y),
		                  SimMath::addf32(SimMath::subf32(SimMath::mulf32(dx, 0.0f), SimMath::mulf32(dy, 0.0f)), point.z) };
	return setLocationIfValid(obj, offset); // RW 0xA0229F
}

// RW 0x9E70B6: the terrain's tree clusters (TheTerrainLogic + 0x584, see TerrainTrees::clusters): in cluster order the first cluster whose
// "used" byte (RW 0x9E70A6, + 0x18) is clear and whose member count (RW 0x97D700) is above the best so far (strictly) and whose centre
// (RW 0x9E7071, + 0x08) the caster's player does not see SHROUDED (RW 0xB4D9A0 == 2) wins. On success the used byte is set (RW 0x9E7095)
// BEFORE RW 0xA0229F accepts the centre, so a refused location still burns the cluster; the decision answers RW 0xA0229F's answer.
// Both editions: BFME2 1.06's AISpellBookTreeKiller::shouldActivate (game.dat 0x5D7766, Open-BFME-2 AISpellBookTreeKiller.cpp) reads the same
// TheTerrainLogic + 0x584 table with a shroud check (the donor's evidence line; its body was not compared field by field).
bool AISpecialPowerUpdate::decideTreeKiller(Object &obj)
{
	GameLogic &logic = obj.logic();
	const Player *viewer = obj.getControllingPlayer();
	if (!viewer || !logic.shroud())
	{
		return false; // INFERENCE: retail reads through the shroud pointer unchecked; every game has one
	}
	std::vector<TerrainTrees::Cluster> &clusters = logic.trees().clusters();
	unsigned best = 0;
	size_t bestCluster = clusters.size();
	Coord3D bestPosition{};
	for (size_t i = 0; i < clusters.size(); ++i)
	{
		const TerrainTrees::Cluster &cluster = clusters[i];
		if (cluster.used)
		{
			continue;
		}
		const unsigned count = (unsigned)cluster.members.size();
		if (count <= best)
		{
			continue;
		}
		if ((int)logic.shroud()->getStatusAt(viewer->getPlayerIndex(), cluster.center.x, cluster.center.y) == CELLSHROUD_SHROUDED)
		{
			continue;
		}
		best = count;
		bestCluster = i;
		bestPosition = cluster.center;
	}
	if (bestCluster == clusters.size())
	{
		return false;
	}
	clusters[bestCluster].used = true; // RW 0x9E7095, before the location base runs
	return setLocationIfValid(obj, bestPosition); // RW 0xA0229F
}
