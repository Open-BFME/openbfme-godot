// OpenBFME. GPL-3.0.
// BFME1 1.03 AI mood scan 0x00279A50; later editions retain RW 0x66844A.
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "GameLogic/Module/ActiveBody.h"
#include "Common/NumericState.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/SimMath.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
// BFME1 optional human filter 0x26FA80; paired with RW 0x662E2B.
// AI +0x32F is allowedToChase (setter 0x2B4300, AssaultTransport update
// 0x2B4C00 / final orders 0x2B4950). Object +0x1F5 is the construction-time
// cache of SpecialAbilityUpdate +0xE4 (0x1D3829); its constructor clears that
// byte, so it remains false for the supported retail module constructors.
class MoodTargetFilter : public PartitionFilter
{
public:
	explicit MoodTargetFilter(Object &me)
		: m_me(me)
	{
	}
	bool allow(Object &obj) override
	{
		const ObjectWeapons *w = m_me.getWeapons();
		const Weapon *cur = w ? w->currentWeapon() : nullptr;
		if (!cur)
		{
			return true;
		}
		if (w->isWithinAttackRange(obj, 0.0f, true))
		{
			return true;
		}
		if (cur->getTemplate() && (cur->getTemplate()->m_meleeWeapon || cur->getTemplate()->m_chaseWeapon))
		{
			return true;
		}
		const Player *p = m_me.getControllingPlayer();
		if (p && p->getPlayerType() != PLAYER_HUMAN)
		{
			return true;
		}
		auto *ai=m_me.getAIUpdateInterface();
		return !ai || ai->nativeMeleeState().allowedToChase;
	}

private:
	Object &m_me;
};

bool teamAttacksCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget");
}
} // namespace

Object *AIUpdateInterface::getNextBfme1MoodTarget(bool calledByAI, bool calledDuringIdle)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	if (!logic.combat().autoAcquireEnabled())
	{
		return nullptr; // the port's switch for the movement sweeps (not retail state)
	}
	static const int kAbilityBusy = CombatNames::status("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING");
	static const int kNoAutoAcquire = CombatNames::status("NO_AUTO_ACQUIRE");
	static const int kHordeMember = CombatNames::status("HORDE_MEMBER");
	static const int kIgnoreAi = CombatNames::status("IGNORE_AI_COMMAND");
	static const int kStandGround = CombatNames::status("STAND_GROUND");
	static const int kFleeOffMap = CombatNames::status("FLEE_OFF_MAP");
	static const int kContesting = CombatNames::status("CONTESTING_BUILDING");
	static const int kHidden = CombatNames::status("HIDDEN");
	static const int kInsideGarrison = CombatNames::status("INSIDE_GARRISON");
	static const int kJustBuilt = CombatNames::modelCondition("JUST_BUILT");
	static const int kComingOut = CombatNames::modelCondition("COMING_OUT_OF_FACTORY");
	static const int kDozer = CombatNames::kindOf("DOZER");
	static const int kHorde = CombatNames::kindOf("HORDE");
	static const int kAttackNeedsLos = CombatNames::kindOf("ATTACK_NEEDS_LINE_OF_SIGHT");
	const unsigned kCloseRange = 7; // TheWeaponConditionNames (RW 0xDA1328) CLOSE_RANGE
	if (obj.isEffectivelyDead() || obj.isOffMap() || obj.testStatus((unsigned)kAbilityBusy) || hasTemporaryStateMachine() || obj.testStatus((unsigned)kNoAutoAcquire) || obj.testStatus((unsigned)kHordeMember) ||
		obj.testStatus((unsigned)kIgnoreAi) || obj.testModelCondition(kJustBuilt) || obj.testModelCondition(kComingOut))
	{
		return nullptr;
	}
	const Player *player = obj.getControllingPlayer();
	const bool needsReach = obj.testStatus((unsigned)kStandGround) || (player && player->getPlayerType() == PLAYER_HUMAN);
	if (obj.isKindOf((unsigned)kDozer))
	{
		if (const DozerAIUpdate *dozer = dynamic_cast<const DozerAIUpdate *>(this))
		{
			if (dozer->getCurrentTask() != -1 || dozer->isAnyTaskPending())
			{
				return nullptr;
			}
		}
	}
	if (m_orderedMeleeTarget != INVALID_ID)
	{
		Object *t = logic.findObjectByID(m_orderedMeleeTarget);
		if (t && !t->isEffectivelyDead() && !t->testStatus((unsigned)kFleeOffMap))
		{
			return t;
		}
		m_orderedMeleeTarget = INVALID_ID;
	}
	if (calledDuringIdle && !(m_combat.autoAcquire & 1u))
	{
		return nullptr;
	}
	if (isStateActive() && (m_combat.autoAcquire & 8u))
	{
		return nullptr;
	}
	if (const Object *container = obj.getContainedBy())
	{
		const ContainModuleInterface *own = obj.getContain();
		const bool refused = !m_combat.canAttackWhileContained && !obj.testStatus((unsigned)kContesting) && own && !own->moodScanWhileContained();
		(void)container;
		if (refused)
		{
			return nullptr;
		}
	}
	if (calledDuringIdle && InvisibilityManager::isStealthedAndUndetected(obj, nullptr) && !(m_combat.autoAcquire & 2u))
 {
  // BFME1 0x279C42: the parent's rider-status mask, bit CAN_ATTACK.
  const Object *parent=obj.getContainedBy();
  const auto *contain=parent?parent->getContain():nullptr;
  const auto *mask=contain?contain->getObjectStatusOfContainedFor(&obj):nullptr;
  const auto bit=(unsigned)CombatNames::status("CAN_ATTACK");
  if(!mask || !((*mask)[bit>>5] & (1u<<(bit&31))))return nullptr;
 }
	if (obj.testStatus((unsigned)kHidden))
	{
		return nullptr;
	}
	if (calledByAI)
	{
			Team *team = obj.getTeam();
		if (teamAttacksCommonTarget(team))
		{
			if (Object *teamVictim = AIHuntState::teamTarget(*team, logic))
			{
				if (attitude() >= 0)
				{
					return teamVictim;
				}
			}
		}
		const unsigned now = frame();
		if (now < m_nextMoodCheck)
		{
			return nullptr;
		}
		const int rate = (int)m_combat.moodAttackCheckRate;
		m_nextMoodCheck = now + (unsigned)rate;
		if (m_randomlyOffsetMoodCheck)
		{
			const int half = rate >> 1;
			m_nextMoodCheck = (unsigned)((int)m_nextMoodCheck + logic.random().getValue(-half, half, "AIUpdate.cpp", 0x1D67));
			m_randomlyOffsetMoodCheck = false;
		}
	}
	float range = AI::adjustedVisionRange(obj, 3u);
	if (!(range > 0.0f))
	{
		return nullptr;
	}
	if (const Object *container = obj.getContainedBy())
	{
		range = SimMath::pc24Add(CombatQueries::boundingCircleRadius(*container), range);
	}
	const unsigned mood = moodMatrixValue();
	if ((mood & 2u) && (mood & 0x200u))
	{
		const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule());
		return body ? logic.findObjectByID(body->lastDamager()) : nullptr;
	}
	if (mood & 0x2000u)
	{
		return nullptr;
	}
	if (obj.isKindOf((unsigned)kHorde) && obj.testStatus((unsigned)kInsideGarrison))
	{
		ContainModuleInterface *c = obj.getContain();
		HordeContainInterface *h = c ? c->getHordeContainInterface() : nullptr;
		if (h && !h->allMembersEntered())
		{
			return nullptr;
		}
	}
	const GameLogicSettings &settings = logic.settings();
	unsigned q = AI::CAN_ATTACK;
	if (settings.attackUsesLineOfSight && obj.isKindOf((unsigned)kAttackNeedsLos))
	{
		q |= AI::CAN_SEE;
	}
	if (settings.attackIgnoreInsignificantBuildings)
	{
		q |= AI::IGNORE_INSIGNIFICANT_BUILDINGS;
	}
	if (m_combat.autoAcquire & 0x10u)
	{
		q |= AI::ATTACK_BUILDINGS;
	}
	if (calledByAI && player && player->getPlayerType() == PLAYER_HUMAN)
	{
		q |= AI::UNFOGGED;
	}
	q |= AI::AUTO_ACQUIRE;
	if (needsReach)
	{
		q |= AI::WITHIN_ATTACK_RANGE;
	}
	if (player && player->getPlayerType() == PLAYER_COMPUTER)
	{
		q |= AI::COMPUTER_CREEPS;
	}
 Object *target=nullptr;
 // BFME1 0x279E88: a player's stop retains the last attacker.
 if(m_nativeMelee.stopRetaliation && calledByAI && calledDuringIdle) {
  const auto *body=dynamic_cast<const ActiveBody*>(obj.getBodyModule());
  Object *attacker=body?logic.findObjectByID(body->lastDamager()):nullptr;
  const auto *weapons=obj.getWeapons();
  if(attacker && weapons && weapons->currentWeapon()) {
   if(CombatQueries::edgeDistanceSquared2D(obj,*obj.getPosition(),*attacker,*attacker->getPosition())>1.0f) {
    m_nativeMelee.stopRetaliation=false;target=attacker;
   }
   if(weapons->isWithinAttackRange(*attacker))target=attacker;
  }
 }
 // BFME1 0x1BE3D0 / 0x1B2D80 consumes the firing tracker's target once.
 if(!target) {
  if(Object *remembered=logic.findObjectByID(obj.takeBfme1FiringTarget())) {
   remembered=AI::adjustVictim(*remembered,&obj,true,0);
   if(remembered) {
    const auto &p=*obj.getPosition(),&t=*remembered->getPosition();
    const auto x=SimMath::pc24SubW(t.x,p.x),y=SimMath::pc24SubW(t.y,p.y);
    if(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y)) < SimMath::pc24MulW(range,range))target=remembered;
   }
  }
  if(!target) {
   MoodTargetFilter moodFilter(obj);
   target=AI::findClosestEnemy(obj,range,q,attackInfo(),player && player->getPlayerType()==PLAYER_HUMAN?&moodFilter:nullptr,0);
  }
 }
	if (target && !obj.isKindOf((unsigned)kHorde) && target->isKindOf((unsigned)kHorde))
	{
		target = AI::adjustVictim(*target, &obj, false, 0);
	}
	if ((calledByAI && needsReach && target && !AI::canAttackFromHere(obj, *target)) || !obj.isAbleToAttack())
	{
		return nullptr;
	}
	if (!target)
	{
		return nullptr;
	}
	if ((obj.getWeaponSetFlags()[kCloseRange >> 5] & (1u << (kCloseRange & 31))) && !AI::canAttackFromHere(obj, *target) && m_combat.holdGroundCloseRangeDistance > 0.0f)
	{
		const float d = (float)SimMath::sqrtPC24(SimMath::sumSquares3(SimMath::pc24Sub(target->getPosition()->x, obj.getPosition()->x), SimMath::pc24Sub(target->getPosition()->y, obj.getPosition()->y), SimMath::pc24Sub(target->getPosition()->z, obj.getPosition()->z)));
		if (m_combat.holdGroundCloseRangeDistance < d)
		{
			return nullptr;
		}
	}
	if (obj.isKindOf((unsigned)kHorde))
	{
		if (Object *c = target->getContainedBy())
		{
			if (c->isKindOf((unsigned)kHorde))
			{
				target = c;
			}
		}
	}
	return target;
}
