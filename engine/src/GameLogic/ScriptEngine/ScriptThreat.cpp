// OpenBFME. GPL-3.0.
// Lane THREATLEVEL-1: the map scripts' cached threat (RW 0x697B41 / 0x69609F, RW 0x7A6A20 / 0x7A5B03; BFME2 0x695844 / 0x7A2DBB). See ScriptThreat.h
// for the facts of both editions.

#include "GameLogic/ScriptEngine/ScriptThreat.h"

#include "Common/GameCommon.h"
#include "Common/GameEdition.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"

namespace
{
// RW 0x68C7C1 / 0x79FE73 (BFME2 0x68C0F2 / 0x79D8D3): (ms / 1000) * LOGICFRAMES_PER_SECOND + frame, unsigned integer arithmetic (div esi = 1000; imul)
constexpr unsigned kRefreshMs = 5000;
unsigned deadlineAfter(const GameLogic &logic, unsigned ms)
{
	return (ms / 1000u) * (unsigned)LOGICFRAMES_PER_SECOND + logic.getFrame();
}

// RW 0x69609F / 0x7A5B03's query and sum (BFME2 0x695844 / 0x7A2DBB): the hits of `reference`'s enemies within `radius` of `center`, 2D, unsorted
float sumThreat(GameLogic &logic, const Object &reference, const Coord3D &center, float radius)
{
	// RW 0xC11DC0 (mask 1: the reference's relationship to the hit is ENEMIES; flag 0) -> RW 0xC10E20 (not effectively dead) -> RW 0xC0F374 (the
	// reference's + 0x458 bit 3, never set: passes every hit, as LiveScripting.cpp's ObjectCountNearbyEnemies reads it)
	PartitionFilterFn enemies([&reference](Object &o) { return reference.getRelationship(o) == ENEMIES; });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	const PartitionHits hits = logic.partition().iterateObjectsInRange(center, radius, FROM_CENTER_2D, { &enemies, &alive }, ITER_FASTEST);
	float total = 0.0f; // xorps xmm0; movss [ebp+8], xmm0
	if (logic.edition() == GameEdition::BFME2_106)
	{
		// BFME2 0x695844 / 0x7A2DBB: total += hit template + 0x51C (addss, the template's ThreatLevel)
		for (const PartitionHit &h : hits)
		{
			total = SimMath::sseAdd(total, AIThreatFinder::templateThreatLevel(*h.object));
		}
		return total;
	}
	// RotWK RW 0x696135 .. 0x69613F (and RW 0x7A5BBA .. 0x7A5BC2): call RW 0x68F0EC; fadd float [ebp+8]; fstp float [ebp+8] (PC24)
	for (const PartitionHit &h : hits)
	{
		total = SimMath::fstpDword(SimMath::pc24AddW(AIThreatFinder::threatValueWide(*h.object), (double)total));
	}
	return total;
}
} // namespace

float ScriptThreat::getThreatInRange(GameLogic &logic, Object &obj, float radius)
{
	// RW 0x697B44 .. 0x697B53: cmp [esi + 0x468], frame; jnc (unsigned): refresh only once the deadline is behind the frame
	if (obj.scriptThreatDeadline() < logic.getFrame())
	{
		// RW 0x69609F: around the object's own position (+ 0x38), the object itself the filters' reference
		const float total = sumThreat(logic, obj, *obj.getPosition(), radius);
		obj.setScriptThreatCache(total, deadlineAfter(logic, kRefreshMs)); // + 0x464, then RW 0x68C7C1(5000) on + 0x468
	}
	return obj.scriptThreatCache(); // fld float [esi + 0x464]
}

float ScriptThreat::getThreatInRange(GameLogic &logic, Team &team, float radius)
{
	// RW 0x7A6A20: the same test on + 0x124
	if (team.scriptThreatDeadline() < logic.getFrame())
	{
		// RW 0x7A5B03: the centre first (RW 0x7A0ECD, every member), then the first member (RW 0x66362D): none, return with the cache untouched
		Coord3D center{};
		const bool any = ScriptConditions::teamCentreAllMembers(team, center);
		const Object *leader = team.getFirstMember();
		if (any && leader)
		{
			const float total = sumThreat(logic, *leader, center, radius);
			team.setScriptThreatCache(total, deadlineAfter(logic, kRefreshMs)); // + 0x120, then RW 0x79FE73(5000) on + 0x124
		}
	}
	return team.scriptThreatCache(); // fld float [ecx + 0x120]
}
