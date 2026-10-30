// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIClosestEnemy.h.

#include "GameLogic/AI/AIClosestEnemy.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/NumericState.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatLineOfSight.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponState.h"

#include <vector>

namespace
{
const char *const kApparentPlayerStop = "[S-326] target search: the reject-buildings filter (RW 0x661869) met a contained structure with no controlling player; its "
                                        "contain's apparent controlling player (contain vslot 0x4C) is not ported: refused";
const char *const kCrowdStop = "[S-326] target search: the friendly-fire crowd test (RW 0x6CB9F7 radius, the geometry overlap query RW 0xA3C7D0 -> RW 0xAD2CE0, "
                               "S-1020) cannot run; with ArtilleryMaxFriendlyFire below 1.0 or a target below 1 max health it is taken as passing";
const char *const kNoPathfinderStop = "[S-326] target search: no pathfinder (TheAI + 0x10) for the pathability test RW 0x6FB754: the candidate is taken as unreachable";

struct Names
{
	int noAutoAcquire, contestingBuilding, hordeMember, fleeOffMap, destroyed, doNotPickMe, standGround;
	int horde, unattackable, structure, mine, fsBaseDefense, notAutoAcquirable, rockVendor, rebuildHole, blockingGate, wallSegment, canAttack, creep;
	int stunnedFlailing, freefall, emotionDissident;
	Names()
	{
		noAutoAcquire = CombatNames::status("NO_AUTO_ACQUIRE");          // 0x17
		contestingBuilding = CombatNames::status("CONTESTING_BUILDING"); // 0x25
		hordeMember = CombatNames::status("HORDE_MEMBER");               // 0x26
		fleeOffMap = CombatNames::status("FLEE_OFF_MAP");                // 0x32
		destroyed = CombatNames::status("DESTROYED");                    // 0
		doNotPickMe = CombatNames::status("DO_NOT_PICK_ME");             // 0x4F
		standGround = CombatNames::status("STAND_GROUND");               // 0x44
		horde = CombatNames::kindOf("HORDE");                            // 109 (+ 0x115 bit 5)
		unattackable = CombatNames::kindOf("UNATTACKABLE");              // 54
		structure = CombatNames::kindOf("STRUCTURE");                    // 7
		mine = CombatNames::kindOf("MINE");                              // 55
		fsBaseDefense = CombatNames::kindOf("FS_BASE_DEFENSE");          // 64
		notAutoAcquirable = CombatNames::kindOf("NOT_AUTOACQUIRABLE");   // 130
		rockVendor = CombatNames::kindOf("ROCK_VENDOR");                 // 136
		rebuildHole = CombatNames::kindOf("REBUILD_HOLE");               // 38
		blockingGate = CombatNames::kindOf("BLOCKING_GATE");             // 137
		wallSegment = CombatNames::kindOf("WALL_SEGMENT");               // 189
		canAttack = CombatNames::kindOf("CAN_ATTACK");                   // 3
		creep = CombatNames::kindOf("CREEP");                            // 174
		stunnedFlailing = CombatNames::modelCondition("STUNNED_FLAILING"); // 127
		freefall = CombatNames::modelCondition("FREEFALL");                // 72
		emotionDissident = CombatNames::modelCondition("EMOTION_DISSIDENT"); // 206
	}
};
const Names &names()
{
	static const Names n;
	return n;
}

bool kind(const Object &o, int bit)
{
	return o.isKindOf((unsigned)bit);
}
bool status(const Object &o, int bit)
{
	return o.testStatus((unsigned)bit);
}

// RW 0x68DAE6 (KindOf FS_POWER, FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY or FS_CASH_PRODUCER)
bool isFactionStructure(const Object &obj)
{
	static const int kinds[5] = { CombatNames::kindOf("FS_POWER"), CombatNames::kindOf("FS_FACTORY"), CombatNames::kindOf("FS_BASE_DEFENSE"),
		CombatNames::kindOf("FS_TECHNOLOGY"), CombatNames::kindOf("FS_CASH_PRODUCER") };
	for (int k : kinds)
	{
		if (obj.isKindOf((unsigned)k))
		{
			return true;
		}
	}
	return false;
}

// RW 0x6A950B: the player's skirmish AI (TheSkirmishAIManager's groups), null for none
const AISkirmishPlayer *skirmishAIOf(const GameLogic &logic, const Player *p)
{
	return p ? logic.skirmishAI().findAI(p->getPlayerIndex()) : nullptr;
}

// ---- the filters of RW 0x7006D7 -----------------------------------------------------------------------------------------------------------------------------

// vtable 0xC1D688, allow RW 0x6FF7A4, mask RW 0x6FF76F
class EnemiesFilter : public PartitionFilter
{
public:
	explicit EnemiesFilter(Object &me)
		: m_me(me)
	{
		const Names &n = names();
		const Player *p = me.getControllingPlayer();
		if (!p || me.testModelCondition(n.emotionDissident))
		{
			m_mask = 0xFFFFFFFFu;
			return;
		}
		// RW 0x6A8695(index, 4, 0): the players whose default team the player counts as ENEMIES
		m_mask = 0;
		const PlayerList &players = me.logic().players();
		for (int i = 0; i < players.getPlayerCount(); ++i)
		{
			const Player *q = players.getNthPlayer(i);
			if (q && q != p && q->getPlayerIndex() >= 0 && q->getPlayerIndex() < 32 && p->getRelationship(q->getDefaultTeam()) == ENEMIES)
			{
				m_mask |= 1u << q->getPlayerIndex();
			}
		}
	}
	bool allow(Object &obj) override
	{
		const Names &n = names();
		if (obj.isEffectivelyDead()) // + 0x458 bit 0 (bit 3: never set in the port)
		{
			return false;
		}
		// RW 0x792973 on the PhysicsBehavior (+ 0x264): its object's STUNNED_FLAILING or FREEFALL
		if ((obj.testModelCondition(n.stunnedFlailing) || obj.testModelCondition(n.freefall)) && obj.findModule("PhysicsBehavior"))
		{
			return false;
		}
		if (kind(obj, n.unattackable) || kind(obj, n.horde))
		{
			return false;
		}
		return m_me.getRelationship(obj) == ENEMIES; // RW 0x68D7AB
	}
	std::uint32_t playerLayerMask() const override { return m_mask; }

private:
	Object &m_me;
	std::uint32_t m_mask = 0xFFFFFFFFu;
};

// vtable 0xC10E20, RW 0x660E71
class AliveFilter : public PartitionFilter
{
public:
	bool allow(Object &obj) override { return !obj.isEffectivelyDead(); }
};

// vtable 0xC0F1A8, ctor RW 0x660EC6, allow RW 0x661869
class RejectBuildingsFilter : public PartitionFilter
{
public:
	explicit RejectBuildingsFilter(Object &me)
		: m_me(me)
	{
		const Player *p = me.getControllingPlayer();
		m_meComputer = p && p->getPlayerType() == PLAYER_COMPUTER;
	}
	bool allow(Object &obj) override
	{
		const Names &n = names();
		if (!kind(obj, n.structure) || kind(obj, n.mine))
		{
			return true;
		}
		if (!m_me.getControllingPlayer())
		{
			return false;
		}
		if (!obj.getControllingPlayer())
		{
			if (obj.getContain())
			{
				obj.logic().noteStop(kApparentPlayerStop);
			}
			return false;
		}
		if (m_meComputer || kind(obj, n.fsBaseDefense))
		{
			return true;
		}
		return obj.getContain() && obj.isAbleToAttack(); // RW 0x691269
	}

private:
	Object &m_me;
	bool m_meComputer = false;
};

// AUTO_ACQUIRE: vtable 0xC1D6AC (RW 0x6FF67C, allow RW 0x660DD6) for a player with a skirmish AI: REBUILD_HOLE passes, else NOT_AUTOACQUIRABLE / ROCK_VENDOR
// are refused; vtable 0xBD88B0 (RW 0x445139 with KINDOFMASK_NONE as the must mask, allow RW 0x660DB2) otherwise: NOT_AUTOACQUIRABLE / ROCK_VENDOR refused
class AutoAcquireKindFilter : public PartitionFilter
{
public:
	explicit AutoAcquireKindFilter(bool skirmishAI)
		: m_skirmishAI(skirmishAI)
	{
	}
	bool allow(Object &obj) override
	{
		const Names &n = names();
		if (m_skirmishAI && kind(obj, n.rebuildHole))
		{
			return true;
		}
		return !kind(obj, n.notAutoAcquirable) && !kind(obj, n.rockVendor);
	}

private:
	bool m_skirmishAI;
};

// vtable 0xC1D694, allow RW 0x6FF9D8 -> RW 0x6FF7FA
class WithinAttackRangeFilter : public PartitionFilter
{
public:
	explicit WithinAttackRangeFilter(Object &me)
		: m_me(me)
	{
	}
	bool allow(Object &obj) override { return AI::canAttackFromHere(m_me, obj); }

private:
	Object &m_me;
};

// vtable 0xC1D620, allow RW 0x6616AC (lane LOS-1: CombatLineOfSight::filterAllows)
class LineOfSightFilter : public PartitionFilter
{
public:
	explicit LineOfSightFilter(Object &me)
		: m_me(me)
	{
	}
	bool allow(Object &obj) override { return CombatLineOfSight::filterAllows(m_me, obj); }

private:
	Object &m_me;
};

// vtable 0xC1D63C, allow RW 0x661027: RW 0x68D8F7(obj, player index) == CLEAR (no shroud record or ALWAYS_VISIBLE answers CLEAR there: TARGET 0x68D914)
class FreeOfFogFilter : public PartitionFilter
{
public:
	explicit FreeOfFogFilter(int playerIndex)
		: m_player(playerIndex)
	{
	}
	bool allow(Object &obj) override
	{
		ShroudManager *shroud = obj.logic().shroud();
		if (!shroud)
		{
			return true; // no shroud manager, no record (+ 0x4E4): CLEAR
		}
		const ObjectShroudStatus s = shroud->getObjectStatus(obj, m_player);
		return s == OBJECTSHROUD_CLEAR;
	}

private:
	int m_player;
};

// vtable 0xC1D630 (+ 8 allowNonBuildings 1, + 9 allowInsignificant 0), allow RW 0x660F1A
class InsignificantBuildingsFilter : public PartitionFilter
{
public:
	bool allow(Object &obj) override
	{
		if (!kind(obj, names().structure) || isFactionStructure(obj)) // RW 0x68DAD6 / 0x68DB19
		{
			return true;
		}
		if (obj.getAIUpdateInterface())
		{
			return true;
		}
		const ContainModuleInterface *c = obj.getContain();
		if (!c)
		{
			return true;
		}
		return c->isGarrisonable() && c->getContainCount() != 0; // contain vslot 0x10 / 0x114(0) (INFERENCE on the names)
	}
};

// vtable 0xC0F19C, ctor RW 0x660D2C(me, 0), allow RW 0x660D71: visible (RW 0x694CCC: EmotionModules' port of it, S-1022), or a computer player's search
// and the object is firing (RW 0x68C89B)
class StealthFilter : public PartitionFilter
{
public:
	explicit StealthFilter(const Object &me)
		: m_player(me.getControllingPlayer())
	{
	}
	bool allow(Object &obj) override
	{
		bool v = EmotionModules::visibleToScans(obj);
		if (!v && m_player && m_player->getPlayerType() == PLAYER_COMPUTER && InvisibilityManager::isFiring(obj))
		{
			v = true;
		}
		return v;
	}

private:
	const Player *m_player;
};

// vtable 0xC1D6A0, ctor RW 0x6FF651({FLEE_OFF_MAP}, {DESTROYED}), allow RW 0x661484: refused when FLEE_OFF_MAP is set and DESTROYED clear
class FleeOffMapFilter : public PartitionFilter
{
public:
	bool allow(Object &obj) override
	{
		const Names &n = names();
		return !(status(obj, n.fleeOffMap) && !status(obj, n.destroyed));
	}
};

// vtable 0xC1D66C (+ 0xC 1, + 0x10 the range), allow RW 0x6612AC: Object::canSee(obj, range) (RW 0x68FA3D, EmotionModules' port)
class CanSeeFilter : public PartitionFilter
{
public:
	CanSeeFilter(Object &me, float range)
		: m_me(me)
		, m_range(range)
	{
	}
	bool allow(Object &obj) override { return EmotionModules::canSeeObject(m_me, obj, m_range); }

private:
	Object &m_me;
	float m_range;
};

// vtable 0xC0F368 (+ 0xC ATTACK_NEW_TARGET, + 0x10 CMD_FROM_AI... pushed as RW 0x68D6A6(type 0, obj, source 2)), allow RW 0x660CA4: possible or possible
// after moving. ObjectWeapons::canAttackObject stands for RW 0x68D6A6 as everywhere in the port (S-325)
class PossibleToAttackFilter : public PartitionFilter
{
public:
	explicit PossibleToAttackFilter(Object &me)
		: m_me(me)
	{
	}
	bool allow(Object &obj) override
	{
		const ObjectWeapons *w = m_me.getWeapons();
		return w && w->canAttackObject(obj, CMD_FROM_AI, false);
	}

private:
	Object &m_me;
};

// RW 0x6FB754(TheAI + 0x10, me, pos, weapon, 1): no AI -> false; the range max(150, the weapon's attack range RW 0x6CA8BD), MeleeWeapon (+ 0x125) from it
bool canApproach(Object &me, const Coord3D &pos, const Weapon *weapon)
{
	AIUpdateInterface *ai = me.getAIUpdateInterface();
	if (!ai)
	{
		return false;
	}
	AIWorld *world = me.logic().aiWorld();
	if (!world || !world->mapReady())
	{
		me.logic().noteStop(kNoPathfinderStop);
		return false;
	}
	float range = 0.0f;
	bool melee = false;
	if (weapon)
	{
		const ObjectWeapons *w = me.getWeapons();
		range = w ? w->currentAttackRange() : 0.0f; // the weapon is the current one (RW 0x68B58C(0))
		melee = weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon;
	}
	return ai->world().pathfinder().canApproachToTarget(ai->adapter(), ai->locomotorInfo(), pos, range, melee, true);
}

// RW 0x6FF29B (the contain iteration's callback): the highest priority among the contained objects
float containedPriority(const AttackPriorityInfo &info, Object &me, const Object &container, float p)
{
	const ContainModuleInterface *c = container.getContain();
	const ContainModuleInterface::ContainedItemsList *list = c ? c->getContainedItemsList() : nullptr;
	if (!list)
	{
		return p;
	}
	for (Object *o : *list) // contain vslot 0x110 (the contain list order)
	{
		if (!o)
		{
			continue;
		}
		const float q = info.getPriority(me, *o, true);
		if (q > p) // fcomip
		{
			p = q;
		}
	}
	return p;
}

// RW 0x792973 / + 0x458: see EnemiesFilter. The creep player's name (RW 0xBFD514)
const char *const kCreepPlayer = "PlyrCreeps";
} // namespace

namespace AI
{
float adjustedVisionRange(const Object &obj, unsigned flags)
{
	// RW 0x6FF412
	AIUpdateInterface *ai = const_cast<Object &>(obj).getAIUpdateInterface();
	if (!ai)
	{
		return 0.0f; // RW 0xC1B594
	}
	const float vision = AIUpdateInterface::objectVisionRangeOf(const_cast<Object &>(obj)); // RW 0x68E43B
	const float range = SimMath::addf32(CombatQueries::boundingCircleRadius(obj), vision);   // + 0xB8
	const unsigned mood = ai->moodMatrixValue();                                              // RW 0x664D7E
	if (!(flags & 2u) || (mood & 1u))
	{
		return range;
	}
	switch (mood & 0x1F00u)
	{
	case 0x100u: return 0.0f;
	case 0x800u: return SimMath::mulf32(obj.logic().settings().alertRangeModifier, range);
	case 0x1000u: return SimMath::mulf32(obj.logic().settings().aggressiveRangeModifier, range);
	default: return range;
	}
}

bool canAttackFromHere(Object &source, Object &victim)
{
	// RW 0x6FF7FA
	AIUpdateInterface *ai = source.getAIUpdateInterface();
	ObjectWeapons *w = source.getWeapons();
	if (!ai || !w)
	{
		return false;
	}
	const Names &n = names();
	const bool standing = status(source, n.standGround); // or the AI byte + 0x3CC, which the port never sets (S-326)
	if (kind(source, n.horde) && !source.getContain())
	{
		return false; // RW 0x6FF8A7: a HORDE without its contain
	}
	for (int slot = 0; slot < (int)WEAPONSLOT_COUNT; ++slot)
	{
		Weapon *weapon = w->weaponInSlot(slot); // RW 0x6C81B0
		if (!weapon)
		{
			continue;
		}
		if (w->isSlotWithinAttackRange(slot, victim, 0.0f, true)) // RW 0x6CC653
		{
			return true;
		}
		if (weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon) // RW 0x441B59
		{
			if (standing)
			{
				return false;
			}
			// the layers (RW 0x68BBE0) are the same (one layer in the port, S-161: the SIEGE_TOWER branch of a HORDE for other layers cannot run)
			if (EmotionModules::canSeeObject(source, victim, AIUpdateInterface::objectVisionRangeOf(source))) // RW 0x68FA3D, RW 0x68E43B
			{
				return true;
			}
		}
	}
	return false;
}

float friendlyFireRadius(const Weapon &weapon, const Object &owner)
{
	// RW 0x6CB9F7
	const WeaponTemplate *t = weapon.getTemplate();
	if (!t || !TheWeaponStore)
	{
		return 0.0f;
	}
	for (const std::shared_ptr<WeaponNugget> &n : t->m_nuggets)
	{
		if (!n || n->kind() != NUGGET_PROJECTILE || !WeaponNuggetUpgradeAllows(*n, &owner)) // RW 0x90D6F5; vslot 0x2C: only ProjectileNugget has a warhead
		{
			continue;
		}
		const WeaponTemplate *warhead = TheWeaponStore->findWeaponTemplate(static_cast<const ProjectileNugget &>(*n).m_warheadTemplateName);
		if (!warhead || !(warhead->m_affectsMask & WEAPON_AFFECTS_ALLIES)) // + 0x110 & 2
		{
			continue;
		}
		for (const std::shared_ptr<WeaponNugget> &m : warhead->m_nuggets)
		{
			const DamageNugget *d = dynamic_cast<const DamageNugget *>(m.get()); // vslot 0x1C true and vslot 0x28 (asDamageNugget): the damage nuggets
			if (d && WeaponNuggetUpgradeAllows(*m, &owner))
			{
				return d->m_radius; // + 0x150
			}
		}
	}
	return 0.0f;
}

Object *adjustVictim(Object &victim, Object *attacker, bool attackRange, int arg)
{
	// RW 0x68D30E
	(void)arg; // the generator choice of RW 0x86FA87 (0: the logic generator): every caller passes 0
	if (!attacker)
	{
		return nullptr;
	}
	Object *v = &victim;
	while (Object *c = v->getContainedBy())
	{
		ContainModuleInterface *cc = c->getContain();
		if (!cc || !cc->getHordeContainInterface()) // RW 0x68C866
		{
			break;
		}
		v = c;
	}
	ContainModuleInterface *contain = v->getContain();
	HordeContain *horde = contain && contain->getHordeContainInterface() ? dynamic_cast<HordeContain *>(contain) : nullptr; // + 0x258 vslot 0x7C
	if (!horde)
	{
		return &victim;
	}
	float range = 0.0f;
	if (!attackRange)
	{
		range = 99999.0f; // RW 0xBDF350
	}
	else if (!kind(*attacker, names().horde))
	{
		const ObjectWeapons *w = attacker->getWeapons();
		const Weapon *cw = w ? w->currentWeapon() : nullptr; // RW 0x68B58C(0)
		if (cw && cw->getTemplate() && !cw->getTemplate()->m_meleeWeapon)
		{
			range = w->currentAttackRangeNoTarget(); // RW 0x6CA935
		}
	}
	if (horde->countWithEntering() == 0) // slot 0x180(0), RW 0x8706DF
	{
		return nullptr;
	}
	Object *member = horde->pickMemberNear(false, *attacker->getPosition(), range, attacker); // slot 0x48
	if (!member)
	{
		member = horde->pickMemberNoPosition(false); // slot 0x48(0, 0, 0, 0, arg)
	}
	return member;
}

Object *findClosestEnemy(Object &me, float range, unsigned qualifiers, const AttackPriorityInfo *info, PartitionFilter *optionalFilter, int arg)
{
	return findClosestEnemy(me, *me.getPosition(), range, qualifiers, info, optionalFilter, arg); // RW 0x701443
}

Object *findClosestEnemy(Object &me, const Coord3D &pos, float range, unsigned q, const AttackPriorityInfo *info, PartitionFilter *optionalFilter, int arg)
{
	const Names &n = names();
	GameLogic &logic = me.logic();
	if ((q & CAN_ATTACK) && !me.isAbleToAttack()) // RW 0x691269
	{
		return nullptr;
	}
	if ((q & AUTO_ACQUIRE) && status(me, n.noAutoAcquire))
	{
		return nullptr;
	}
	// the filters, linked in RW's order (RW 0x700729 .. 0x7009E8)
	const Player *mePlayer = me.getControllingPlayer();
	EnemiesFilter enemies(me);
	AliveFilter alive;
	RejectBuildingsFilter rejectBuildings(me);
	AutoAcquireKindFilter kindFilter(skirmishAIOf(logic, mePlayer) != nullptr);
	WithinAttackRangeFilter withinRange(me);
	LineOfSightFilter lineOfSight(me);
	FreeOfFogFilter fog(mePlayer ? mePlayer->getPlayerIndex() : -1);
	InsignificantBuildingsFilter insignificant;
	StealthFilter stealth(me);
	FleeOffMapFilter fleeOffMap;
	CanSeeFilter canSee(me, range);
	PossibleToAttackFilter possible(me);
	// performance: a fixed array (13 filters at most), no allocation per search
	PartitionFilter *links[16];
	size_t count = 0;
	struct Chain
	{
		PartitionFilter **data;
		size_t &n;
		void push_back(PartitionFilter *f) { data[n++] = f; }
	} chain{ links, count };
	chain.push_back(&enemies);
	chain.push_back(&alive);
	if (!(q & ATTACK_BUILDINGS))
	{
		chain.push_back(&rejectBuildings);
	}
	if (q & AUTO_ACQUIRE)
	{
		chain.push_back(&kindFilter);
	}
	if (q & WITHIN_ATTACK_RANGE)
	{
		chain.push_back(&withinRange);
	}
	if (q & CAN_SEE)
	{
		chain.push_back(&lineOfSight);
	}
	if (q & UNFOGGED)
	{
		chain.push_back(&fog);
	}
	if (q & IGNORE_INSIGNIFICANT_BUILDINGS)
	{
		chain.push_back(&insignificant);
	}
	chain.push_back(&stealth);
	chain.push_back(&fleeOffMap);
	if (!(q & NO_VISION_TEST))
	{
		chain.push_back(&canSee);
	}
	// vtable 0xC1D678 needs me's AI in state 0x21 (AIAttackMoveToState), which the port's attack-move does not enter (S-328): never linked
	if (q & CAN_ATTACK)
	{
		chain.push_back(&possible);
	}
	// the CONTESTING_BUILDING branch (contain vslot 0xDC) has no setter of the status in the port

	if (!info)
	{
		Object *found = logic.partition().getClosestObject(pos, range, FROM_CENTER_3D, PartitionFilterList(links, count)); // RW 0xA39090
		if (found && (q & CAN_ATTACK))
		{
			found = adjustVictim(*found, &me, true, arg);
		}
		return found;
	}

	const PartitionHits hits = 9000.0f > range ? logic.partition().iterateObjectsInRange(pos, range, FROM_CENTER_3D, PartitionFilterList(links, count), ITER_FASTEST)
	                                           : logic.partition().iterateAllObjects(PartitionFilterList(links, count)); // RW 0xA39340 / 0xA39420
	const ObjectWeapons *myWeapons = me.getWeapons();
	const Weapon *weapon = myWeapons ? myWeapons->currentWeapon() : nullptr; // RW 0x68B58C(0), once
	const float modifier = logic.settings().attackPriorityDistanceModifier;     // AIData + 0x54
	Object *best = nullptr;
	float bestScore = 0.0f, bestP = 0.0f;
	bool bestPathable = false;
	for (const PartitionHit &h : hits)
	{
		Object &hit = *h.object;
		float p = info->getPriority(me, hit, true); // fstp dword
		if (status(hit, n.hordeMember))
		{
			if (Object *horde = hit.getHordeObject(false)) // RW 0x693A1A(0)
			{
				if (!horde->getContainedBy())
				{
					const float hp = info->getPriority(me, *horde, true);
					p = p > hp ? p : hp; // fcomip; ja
				}
				else
				{
					p = 0.0f;
				}
			}
		}
		if (p == 0.0f) // RW 0xC1B594, ucomiss (a NaN goes on)
		{
			continue;
		}
		if (status(hit, n.doNotPickMe))
		{
			continue;
		}
		if (kind(hit, n.blockingGate))
		{
			const GateOpenAndCloseBehavior *gate = dynamic_cast<const GateOpenAndCloseBehavior *>(hit.findModule("GateOpenAndCloseBehavior")); // RW 0x5487EC / 0x68BDA5
			if (gate && gate->isOpen()) // slot 6
			{
				continue;
			}
		}
		if (kind(hit, n.wallSegment) && !kind(hit, n.canAttack))
		{
			continue;
		}
		if (hit.getContain())
		{
			const float c = containedPriority(*info, me, hit, p); // vslot 0x110 with RW 0x6FF29B
			if (c > p)
			{
				p = c;
			}
		}
		const Coord3D &hp = *hit.getPosition();
		const float d2 = CombatQueries::edgeDistanceSquared2D(me, pos, hit, hp);                // RW 0x6634BF
		const float d = NumericState::fstpDword(NumericState::sqrtPC24((double)d2));            // RW 0xA3CF96 (MSVCR71 sqrt), fstp dword
		float score = SimMath::sseSub(SimMath::sseAdd(p, 100.0f), SimMath::sseDiv(d, modifier)); // addss RW 0xBD88D8; divss; subss
		if (q & COMPUTER_CREEPS)
		{
			const Player *hp2 = hit.getControllingPlayer();
			if ((hp2 && hp2->getPlayerName() == kCreepPlayer) || kind(hit, n.creep))
			{
				score = 1.0f; // RW 0xBD1908
			}
		}
		const bool better = score > bestScore || (score == bestScore && p > bestP);
		if (!better && bestPathable)
		{
			continue;
		}
		const bool pathable = canApproach(me, hp, weapon); // RW 0x6FB754
		if (!pathable && bestPathable)
		{
			continue;
		}
		if (weapon)
		{
			// RW 0x6CB9F7 and the crowd query (S-1020): see the header
			const float artillery = logic.settings().artilleryMaxFriendlyFire;
			const BodyModuleInterface *body = hit.getBodyModule();
			const float maxHealth = body ? body->getMaxHealth() : 0.0f;
			if ((!(artillery >= 1.0f) || !(maxHealth >= 1.0f)) && friendlyFireRadius(*weapon, me) > 0.0f)
			{
				logic.noteStop(kCrowdStop);
			}
		}
		if (!better && !(pathable && !bestPathable))
		{
			continue;
		}
		if (optionalFilter && !optionalFilter->allow(hit))
		{
			continue;
		}
		if (!pathable && bestPathable && best)
		{
			continue;
		}
		bestScore = score;
		bestP = p;
		best = &hit;
		bestPathable = pathable;
	}
	if (best && !bestPathable && weapon && weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon) // RW 0x441B59 (RW 0x9188EB: false)
	{
		best = nullptr;
	}
	if (best && (q & CAN_ATTACK) && !(q & WITHIN_ATTACK_RANGE))
	{
		best = adjustVictim(*best, &me, true, arg);
	}
	return best;
}
} // namespace AI
