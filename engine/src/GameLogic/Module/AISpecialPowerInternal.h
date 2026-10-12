// OpenBFME. GPL-3.0.
// See GameLogic/Module/AISpecialPowerUpdate.h for the sources and what is inference.
//
// The helpers the decision objects of RW 0x992724 share (lane MOD-4, lane AIPOW-1): the partition scans with the relationship filter RW 0xC1676C and the alive
// filter RW 0xC10E20, the AI's goal object (RW 0x668303), the body's health ratio (body slot 0x14, RW 0x8C1D75) and the KindOf / status lookups by name (the
// engine never names retail content: the names are the binary's own KindOf / ObjectStatus registries).

#pragma once

#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace AISpecialPowerDetail
{
// the SpecialPowerAIType values: the index of the name in RW 0xDB84F8 (TheAISpecialPowerTypeNames)
enum AISpecialPowerType
{
	AISP_BASIC_SELF_BUFF = 0,
	AISP_CAPTURE_BUILDING = 1,
	AISP_ELENDIL = 2,
	AISP_ENEMY_TYPE_KILLER = 3,
	AISP_ENEMY_TYPE_KILLER_RANGED = 4,
	AISP_ENEMY_TYPE_KILLER_STRUCTURES = 5,
	AISP_GANDALF_WIZARD_BLAST = 6,
	AISP_GIVEXP_AOE = 7,
	AISP_RANGED_AOE_ATTACK = 8,
	AISP_TOGGLE_MOUNTED = 9,
	AISP_SELFAOEHEALHEROS = 10,
	AISP_TARGETAOE_SUMMON = 11,
	AISP_LEGOLAS_ARROWWIND = 12,
	AISP_LEGOLAS_TRAINARCHERS = 13,
	AISP_GOBLINKING_BATTLEFRENZY = 14,
	AISP_GOBLINKING_CALLOFTHEDEEP = 15,
	AISP_GOBLINKING_MOUNTED = 16,
	AISP_HEAL_AOE = 17,
	AISP_TOGGLE_MELEE_AND_RANGE = 18,
	AISP_TOGGLE_SIEGE = 19,
	AISP_CHARGE = 20,
	AISP_SPELLBOOK_ALWAYS_FIRE = 21,
	AISP_SPELLBOOK_ASSIST_BATTLE_BUFF = 22,
	AISP_SPELLBOOK_ASSIST_BATTLE_DEBUFF = 23,
	AISP_SPELLBOOK_ARMY_BREAKER = 24,
	AISP_SPELLBOOK_CAPTURE_CREEP = 25,
	AISP_SPELLBOOK_HEAL = 26,
	AISP_SPELLBOOK_STRUCTURE_BREAKER = 27,
	AISP_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS = 28,
	AISP_SPELLBOOK_ENSHROUDINGMIST = 29,
	AISP_SPELLBOOK_BUFFTERRAIN = 30,
	AISP_SPELLBOOK_REBUILD = 31,
	AISP_SPELLBOOK_BUFFECONOMYBUILDING = 32,
	AISP_SPELLBOOK_CALLTHEHORDE = 33,
	AISP_SPELLBOOK_SHROUD_REVEAL = 34,
	AISP_SPELLBOOK_TREE_KILLER = 35,
	AISP_SPELLBOOK_STRUCTURE_BASEKILL = 36,
	AISP_SPELLBOOK_CITADEL = 37,
	AISP_STANCEBATTLE = 38,
	AISP_STANCEAGGRESSIVE = 39,
	AISP_STANCEHOLDGROUND = 40,
	AISP_SPELLBOOK_DEBUFFECONOMYBUILDING = 41,
	AISP_SPELLBOOK_DEBUFFPRODUCTIONBUILDING = 42,
	AISP_AOE_AND_BUFF = 43,
	AISP_SOUL_FREEZE = 44,
	AISP_DOMINATE_ENEMY = 45,
	AISP_DOMINATE_TROLL = 46,
	AISP_TAME_THE_BEAST = 47,
	AISP_BASIC_SELF_DEBUFF = 48,
	AISP_ATTACK_HEAL_AOE = 49,
	AISP_RANGED_AOE_ATTACK_UNITS = 50,
	AISP_MORGUL_BLADE = 51,
	AISP_GOBLIN_POISON = 52,
};

inline int kind(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

inline bool hasKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

inline int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

// body slot 0x14 (RW 0x8C1D75): health / max health in the FPU (PC24), 0 without a positive max health
inline double bodyRatioWide(const Object &o)
{
	const BodyModuleInterface *body = o.getBodyModule();
	if (!body)
	{
		return 0.0;
	}
	const float maxHealth = body->getMaxHealth();
	return maxHealth > 0.0f ? SimMath::pc24DivW((double)body->getHealth(), (double)maxHealth) : 0.0;
}

// RW 0x668303 on the object's AI (+ 0x260): AIUpdate + 0x40 found by id. DONOR: ZH SlavedUpdate::update reads getCurrentVictim at the call site RotWK's
// SlavedUpdate makes with RW 0x668303, so it is the AI's current victim (AI-3 reads it so too; RW 0x6682B1 setCurrentVictim writes it)
inline Object *goalObject(Object &obj)
{
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	return ai ? ai->currentVictim() : nullptr;
}

// RW 0xC1676C: the controlling player's relationship to the candidate's team is in `mask` (ENEMIES 4, NEUTRAL 8, ALLIES 2)
inline bool relationIn(const Player *player, const Object &o, unsigned mask)
{
	if (!player)
	{
		return false;
	}
	switch (player->getRelationship(o.getTeam()))
	{
	case ENEMIES:
		return (mask & 4u) != 0;
	case NEUTRAL:
		return (mask & 8u) != 0;
	case ALLIES:
		return (mask & 2u) != 0;
	}
	return false;
}

// RW 0xC1676C (`player`'s relationship in `mask`) and RW 0xC10E20 (alive) around `center` within `radius`, distance type 0, near to far (RW 0xA39340 order 1)
inline PartitionHits scanAt(GameLogic &logic, const Player *player, const Coord3D &center, float radius, unsigned mask)
{
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, mask); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	return logic.partition().iterateObjectsInRange(center, radius, FROM_CENTER_2D, { &relation, &alive }, ITER_SORTED_NEAR_TO_FAR);
}

// the same, unsorted (RW 0xA39340 order 0)
inline PartitionHits scanAtUnsorted(GameLogic &logic, const Player *player, const Coord3D &center, float radius, unsigned mask)
{
	PartitionFilterFn relation([&](Object &o) { return relationIn(player, o, mask); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	return logic.partition().iterateObjectsInRange(center, radius, FROM_CENTER_2D, { &relation, &alive }, ITER_FASTEST);
}

// the same around the object, for its controlling player (RW 0x68B678)
inline PartitionHits scan(Object &obj, float radius, unsigned mask)
{
	return scanAt(obj.logic(), obj.getControllingPlayer(), *obj.getPosition(), radius, mask);
}
} // namespace AISpecialPowerDetail
