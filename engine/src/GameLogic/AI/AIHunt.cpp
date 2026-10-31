// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIHunt.h.

#include "GameLogic/AI/AIHunt.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatLineOfSight.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cfloat>

namespace
{

const unsigned kEnemyScanRate = 3u * (unsigned)LOGICFRAMES_PER_SECOND; // RW DAT_00D9F608 * 3
const float kHuntRange = 9999.9f;                                       // RW 0xC1D8E4

// the hunt machine's state 0 (RW 0x741812(machine, 1)): the idle state that does not look for targets
class HuntIdleState : public AIState
{
public:
	explicit HuntIdleState(AIStateMachine &m) : AIState(m, "AIIdleState") {}
	// RW 0x748A4F (the idle state's onEnter, AIStates.cpp line 0x94A): the initial sleep offset draws the logic generator on every entry (review r1)
	StateReturnType onEnter() override
	{
		owner().logic().random().getValue(0, 2 * (int)LOGICFRAMES_PER_SECOND, "AIStates.cpp", 0x94A);
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	bool isIdle() const override { return true; }
	void xfer(Xfer &x) override { xferNoFields(x); } // lane SAVE-4c: nothing is kept (the draw is made at every entry)
};

bool kindOf(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

bool attackCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget");
}

// RW 0x6AA61B: the player's AI's difficulty, else the script engine's (INFERENCE: a side's AI difficulty is its skirmish level)
int playerDifficulty(const Player &p, GameLogic &logic)
{
	return p.getSkirmishDifficulty() >= 0 ? p.getSkirmishDifficulty() : logic.scriptEngine().gameDifficulty();
}

} // namespace

const char *huntStopLine()
{
	return "[S-1188] AI hunt (TEAM_HUNT / NAMED_HUNT / PLAYER_HUNT, RW 0x741133): the crate pick-up (AI + 0x238) is not part of the port (lane TARGET-1: the "
		   "attack priority info AI + 0x70 and RotWK's closest enemy search are); PLAYER_HUNT (RW 0x6AF4B1, lane DECOMP-2) walks the player's objects in "
		   "object list order";
}

bool AIUpdateInterface::aiHunt(CommandSourceType source)
{
	if (!acceptCommand(source, 0x12))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (!GarrisonRules::isMobile(obj) || obj.isKindOf((unsigned)CombatNames::kinds().projectile) ||
		obj.testStatus((unsigned)CombatNames::statuses().hordeMember))
	{
		return false; // RW 0x66450F
	}
	m_machine->clear();
	setLastCommandSource(source);
	m_machine->setState(AI_HUNT);
	wakeUpNow();
	return true;
}

AIHuntState::AIHuntState(AIStateMachine &m)
	: AIState(m, "AIHuntState")
{
}

AIHuntState::~AIHuntState()
{
	if (m_huntMachine)
	{
		m_huntMachine->halt();
	}
}

Object *AIHuntState::teamTarget(Team &team, GameLogic &logic)
{
	if (team.teamTargetId() == 0)
	{
		return nullptr;
	}
	Object *o = logic.findObjectByID(team.teamTargetId());
	if (o && InvisibilityManager::isStealthedAndUndetected(*o, team.getControllingPlayer()))
	{
		o = nullptr;
	}
	if (o && (o->isEffectivelyDead() || o->getContainedBy()))
	{
		o = nullptr;
	}
	if (!o)
	{
		team.setTeamTargetId(0);
	}
	return o;
}

void AIHuntState::setTeamTarget(Team &team, Object *victim, GameLogic &logic)
{
	if (!victim)
	{
		team.setTeamTargetId(0);
		return;
	}
	const Player *p = team.getControllingPlayer();
	if (p && p->getPlayerType() == PLAYER_COMPUTER && playerDifficulty(*p, logic) != 0)
	{
		team.setTeamTargetId(victim->getID());
	}
}

namespace
{
// the machine's vslot 0x24 (lane FLY-1: a giant bird's swoop-then-idle machine), else the base attack-then-idle machine (the hunt and attack-area states')
std::unique_ptr<AIStateMachine> makeAttackThenIdleMachineFor(AIUpdateInterface &ai)
{
	std::unique_ptr<AIStateMachine> m = ai.makeAttackThenIdleMachine(AI_ATTACK_OBJECT);
	if (!m)
	{
		m = std::make_unique<AIStateMachine>(ai, "AIAttackThenIdleStateMachine");
		m->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, false), AI_IDLE, AI_IDLE);
		m->defineState(AI_IDLE, std::make_unique<HuntIdleState>(*m), AI_IDLE, AI_IDLE);
	}
	return m;
}
} // namespace

std::unique_ptr<AIStateMachine> AIHuntState::makeHuntMachine()
{
	return makeAttackThenIdleMachineFor(ai());
}

// lane SAVE-4c (obfme:AIWorld): OpenBFME layout (AIHuntState has no retail xfer in docs/SAVES-RETAIL.md's table by that name; stop S-4220)
void AIHuntState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_nextEnemyScanTime);
	bool has = m_huntMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_huntMachine)
		{
			m_huntMachine->xfer(x);
		}
		return;
	}
	std::unique_ptr<AIStateMachine> loaded; // made and loaded complete, then attached
	if (has)
	{
		loaded = makeHuntMachine();
		loaded->xfer(x);
	}
	m_huntMachine = std::move(loaded);
}

StateReturnType AIHuntState::onEnter()
{
	m_huntMachine = makeHuntMachine();
	GameLogic &logic = owner().logic();
	m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)kEnemyScanRate, "AIStates.cpp", 0x3C92);
	return m_huntMachine->initDefaultState();
}

void AIHuntState::onExit(StateExitType)
{
	m_keptMachines.drop(m_huntMachine); // halted, and kept while its update runs (review of #64)
	if (ObjectWeapons *w = owner().getWeapons())
	{
		w->releaseWeaponLock(LOCKED_TEMPORARILY);
	}
}

bool AIHuntState::isActive() const
{
	return m_huntMachine && m_huntMachine->currentState() && !m_huntMachine->currentState()->isIdle();
}

Object *AIHuntState::findVictim(Object &obj)
{
	// RW 0x748069 .. 0x74816E (lane TARGET-1: the attack priority info AI + 0x70 and RotWK's search)
	GameLogic &logic = obj.logic();
	Team *team = obj.getTeam();
	const bool common = attackCommonTarget(team);
	Object *teamVictim = common ? teamTarget(*team, logic) : nullptr;
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	const AttackPriorityInfo *info = ai ? ai->attackInfo() : nullptr;
	if (teamVictim && !info)
	{
		return teamVictim; // RW 0x7480AE
	}
	const unsigned q = AI::CAN_ATTACK | AI::ATTACK_BUILDINGS | AI::AUTO_ACQUIRE; // 0x4A
	Object *victim = AI::findClosestEnemy(obj, kHuntRange, q, info, nullptr, 0);   // RW 0x701443
	const Player *p = obj.getControllingPlayer();
	if (!victim && p && p->unitsShouldHunt())
	{
		victim = AI::findClosestEnemy(obj, kHuntRange, q, nullptr, nullptr, 0); // RW 0x7480E5: again without the info
	}
	if (common)
	{
		if (teamVictim && info)
		{
			// RW 0x748123 .. 0x74815E: the team's target keeps it unless the new victim's priority is higher (x87: the team target's priority stored as a float,
			// the victim's compared wide)
			if (!victim)
			{
				victim = teamVictim;
			}
			const float teamPriority = info->getPriority(obj, *teamVictim, true);
			const float victimPriority = info->getPriority(obj, *victim, true);
			if (victimPriority <= teamPriority)
			{
				victim = teamVictim;
			}
		}
		setTeamTarget(*team, victim, logic); // RW 0x748169
	}
	return victim;
}

StateReturnType AIHuntState::update()
{
	if (!m_huntMachine)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	GameLogic &logic = obj.logic();
	// (1) RW 0x747EFE: the parent machine's goal object
	if (machine().goalObjectID() != INVALID_ID && machine().goalObjectID() != m_huntMachine->goalObjectID())
	{
		if (Object *goal = machine().goalObject())
		{
			m_huntMachine->setGoalObject(goal->getID());
			if (m_huntMachine->currentStateId() == AI_IDLE)
			{
				m_huntMachine->setState(AI_ATTACK_OBJECT);
			}
			m_nextEnemyScanTime = logic.getFrame() + kEnemyScanRate;
		}
		else
		{
			machine().setGoalObject(INVALID_ID);
		}
	}
	const unsigned now = logic.getFrame();
	// (2) RW 0x747F6D: no scan while fighting in melee
	bool busy = obj.testStatus((unsigned)CombatNames::statuses().isMeleeAttacking);
	if (!busy && kindOf(obj, "HORDE"))
	{
		Object *goal = m_huntMachine->goalObject();
		HordeAIUpdate *horde = dynamic_cast<HordeAIUpdate *>(obj.getAIUpdateInterface());
		if (horde && goal && !goal->isKindOf((unsigned)CombatNames::kinds().structure) && horde->isInCurrentMelee(goal))
		{
			busy = true;
		}
	}
	if (!busy && now >= m_nextEnemyScanTime)
	{
		// (3) RW 0x747FE2
		ObjectWeapons *w = obj.getWeapons();
		if (w && w->isOutOfAmmo() && !obj.isKindOf((unsigned)CombatNames::kinds().projectile))
		{
			return STATE_FAILURE;
		}
		bool rescan = true;
		if (kindOf(obj, "GIANT_BIRD")) // AI vslot 0x168 (GiantBirdAIUpdate RW 0x8BD372)
		{
			const AIState *cur = m_huntMachine->currentState();
			const bool selfLoop = !cur || (cur->successId() == cur->id() && cur->failureId() == cur->id());
			if (!selfLoop && !cur->isIdle())
			{
				rescan = false;
			}
		}
		m_nextEnemyScanTime = now + kEnemyScanRate;
		if (rescan)
		{
			Object *victim = findVictim(obj);
			const ObjectID victimId = victim ? victim->getID() : (ObjectID)INVALID_ID;
			if (victimId != m_huntMachine->goalObjectID())
			{
				m_huntMachine->setGoalObject(victimId);
				m_huntMachine->setState(AI_ATTACK_OBJECT); // RW 0x7481C8 (RW 0x66B1D0 before it, for a first victim, is the client's notification)
			}
			else if ((!m_huntMachine->currentState() || m_huntMachine->currentState()->isIdle()) && victim)
			{
				m_huntMachine->setState(AI_ATTACK_OBJECT);
			}
			const Player *p = obj.getControllingPlayer();
			const bool unitsShouldHunt = p && p->unitsShouldHunt(); // Player + 0x35D (PLAYER_HUNT, lane DECOMP-2)
			if (p && !unitsShouldHunt && m_huntMachine->currentStateId() == AI_IDLE && !victim)
			{
				return STATE_SUCCESS;
			}
		}
	}
	// (4) RW 0x74820F
	machine().lock();
	StateReturnType ret;
	{
		KeptSubMachines<AIStateMachine>::Run run(m_keptMachines); // an attack inside can end this state (onExit below)
		ret = m_huntMachine->updateStateMachine();
	}
	machine().unlock();
	if (IS_STATE_SLEEP(ret))
	{
		ret = STATE_CONTINUE;
	}
	return ret;
}

void AIHuntState::crc(StateHasher &h) const
{
	h.addU32(m_nextEnemyScanTime);
	h.addBool(m_huntMachine != nullptr);
	if (m_huntMachine)
	{
		m_huntMachine->crc(h);
	}
}

// ---- lane BFME2CAMP1: the attack-area state (AIHunt.h) ----------------------------------------------------------------------------------------------------

namespace
{
const unsigned kAttackAreaScanRate = (unsigned)LOGICFRAMES_PER_SECOND; // RW DAT_00D9F608 (5), read as is by RW 0x743D0F / 0x748230
} // namespace

bool AIUpdateInterface::aiAttackArea(const TriggerArea &area, CommandSourceType source)
{
	if (!acceptCommand(source, 0x23))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (!GarrisonRules::isMobile(obj) || obj.isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return false; // RW 0x66C272
	}
	const int index = obj.logic().scriptEngine().triggerIndex(&area);
	if (index < 0)
	{
		return false; // not a trigger of the game (retail keeps the polygon's pointer: every area a script names is one of the map's)
	}
	m_areaToGuard = index;   // RW 0x66C27E: AI + 0x6C
	m_machine->clear();      // RW 0x66C284
	setLastCommandSource(source);
	m_machine->setState(AI_ATTACK_AREA); // RW 0x66C294
	// RW 0x66C2A0 .. 0x66C2B1: a PLAYER or SCRIPT order's attack voice at the area's centre (RW 0x66B271) is the client's
	wakeUpNow();
	return true;
}

const TriggerArea *AIUpdateInterface::areaToGuard() const
{
	if (m_areaToGuard < 0)
	{
		return nullptr;
	}
	const ScriptEngine &e = getObject()->logic().scriptEngine();
	return (size_t)m_areaToGuard < e.triggerAreaCount() ? &e.triggerAreaAt((size_t)m_areaToGuard) : nullptr;
}

AIAttackAreaState::AIAttackAreaState(AIStateMachine &m)
	: AIState(m, "AIAttackAreaState")
{
}

AIAttackAreaState::~AIAttackAreaState()
{
	if (m_attackMachine)
	{
		m_attackMachine->halt();
	}
}

StateReturnType AIAttackAreaState::onEnter()
{
	m_attackMachine = makeAttackThenIdleMachineFor(ai()); // RW 0x743D18: the machine's vslot 0x24
	GameLogic &logic = owner().logic();
	m_nextEnemyScanTime = logic.getFrame() + (unsigned)logic.random().getValue(0, (int)kAttackAreaScanRate, "AIStates.cpp", 0x3DB8);
	return m_attackMachine->initDefaultState(); // RW 0x743D4C: vslot 0x1C
}

void AIAttackAreaState::onExit(StateExitType)
{
	m_keptMachines.drop(m_attackMachine); // RW 0x743D4F: deleted (kept while its own update runs, review of #64)
}

bool AIAttackAreaState::isActive() const
{
	return m_attackMachine && m_attackMachine->currentState() && !m_attackMachine->currentState()->isIdle();
}

StateReturnType AIAttackAreaState::update()
{
	if (!m_attackMachine)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	GameLogic &logic = obj.logic();
	const unsigned now = logic.getFrame();
	if (now >= m_nextEnemyScanTime) // RW 0x74823D: jb skips the scan
	{
		ObjectWeapons *w = obj.getWeapons();
		if (w && w->isOutOfAmmo() && !obj.isKindOf((unsigned)CombatNames::kinds().projectile))
		{
			return STATE_FAILURE; // RW 0x748281
		}
		m_nextEnemyScanTime = now + kAttackAreaScanRate; // RW 0x74826A
		const TriggerArea *area = ai().areaToGuard();   // AI vslot 0x1E4
		if (!area)
		{
			return STATE_FAILURE;
		}
		Object *victim = AI::findClosestEnemyInArea(*area, obj, ai().attackInfo()); // RW 0x74829E: RW 0x700F52(area, owner, AI + 0x70)
		if (victim && victim->isKindOf((unsigned)CombatNames::kinds().horde))       // RW 0x7482AC: + 0x115 bit 5
		{
			victim = AI::adjustVictim(*victim, &obj, true, 0); // RW 0x7482BC
		}
		m_attackMachine->setGoalObject(victim ? victim->getID() : (ObjectID)INVALID_ID); // RW 0x7482C9: vslot 0x38
		if (m_attackMachine->currentStateId() == AI_IDLE && victim)
		{
			m_attackMachine->setState(AI_ATTACK_OBJECT); // RW 0x7482EC: 10
		}
		if (!victim)
		{
			return STATE_SUCCESS; // RW 0x7482F3
		}
	}
	machine().lock(); // RW 0x7482FB: + 0x38
	StateReturnType ret;
	{
		KeptSubMachines<AIStateMachine>::Run run(m_keptMachines);
		ret = m_attackMachine->updateStateMachine(); // vslot 0x10
	}
	machine().unlock();
	if (IS_STATE_SLEEP(ret)) // RW 0x748309: a positive answer (a sleep) is a continue
	{
		ret = STATE_CONTINUE;
	}
	return ret;
}

void AIAttackAreaState::crc(StateHasher &h) const
{
	h.addU32(m_nextEnemyScanTime);
	h.addBool(m_attackMachine != nullptr);
	if (m_attackMachine)
	{
		m_attackMachine->crc(h);
	}
}

void AIAttackAreaState::xfer(Xfer &x)
{
	// OpenBFME layout, the hunt state's (slot 0xC is shared in RotWK: RW 0x743CA3)
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_nextEnemyScanTime);
	bool has = m_attackMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_attackMachine)
		{
			m_attackMachine->xfer(x);
		}
		return;
	}
	std::unique_ptr<AIStateMachine> loaded;
	if (has)
	{
		loaded = makeAttackThenIdleMachineFor(ai());
		loaded->xfer(x);
	}
	m_attackMachine = std::move(loaded);
}

// ---- lane BFME2CAMP1: the squad attack (AIHunt.h) ----

void AISquad::squadFromTeam(const Team &team, bool clearSquadFirst)
{
	if (clearSquadFirst)
	{
		objectIDs.clear(); // RW 0x98B95B
	}
	for (Object *o = team.getFirstMember(); o; o = o->friend_teamNext()) // RW 0x66362D: the team's member order
	{
		objectIDs.push_back(o->getID()); // RW 0x5487BB
	}
}

std::vector<Object *> AISquad::getLiveObjects(GameLogic &logic)
{
	std::vector<Object *> live;
	for (auto it = objectIDs.begin(); it != objectIDs.end();) // RW 0x8DB0AB
	{
		Object *o = logic.findObjectByID(*it);
		if (!o)
		{
			it = objectIDs.erase(it); // RW 0x8FF64D
			continue;
		}
		live.push_back(o);
		++it;
	}
	live.erase(std::remove_if(live.begin(), live.end(), [](const Object *o) { return !PlayerCommands::isSelectable(*o); }), live.end()); // RW 0x8DB103
	return live;
}

bool AIUpdateInterface::aiAttackTeam(const Team &team, int maxShotsToFire, CommandSourceType source)
{
	if (!acceptCommand(source, 0xD))
	{
		return false; // RW 0x667174
	}
	// RW 0x66C03D privateAttackTeam
	Object &obj = *getObject();
	static const int kRampage = CombatNames::weaponSetBit("RAMPAGE");
	if (BitFlagsTest(obj.getWeaponSetFlags(), (size_t)kRampage))
	{
		return false; // RW 0x66C043 .. 0x66C04F: Object + 0x38C bit 8
	}
	m_machine->clear();                  // RW 0x66C05C: vslot 0x14
	m_machine->setGoalSquadFromTeam(team); // RW 0x66C066
	setLastCommandSource(source);        // RW 0x66C072: AI + 0x48
	m_machine->setState(AI_ATTACK_SQUAD); // RW 0x66C079: 0x17
	if (ObjectWeapons *w = obj.getWeapons())
	{
		if (Weapon *cur = w->currentWeapon()) // RW 0x66C081: RW 0x68B58C(0)
		{
			cur->clearWhenFiringEnds();          // + 0x20 = 0
			cur->setMaxShotCount(maxShotsToFire); // + 0x34
		}
	}
	// RW 0x66C098 .. 0x66C0AD: a goal object with a PLAYER or SCRIPT source -> RW 0x66B1D0, the client's attack notification
	wakeUpNow();
	return true;
}

AIAttackSquadState::AIAttackSquadState(AIStateMachine &m)
	: AIState(m, "AIAttackSquadState")
{
}

AIAttackSquadState::~AIAttackSquadState()
{
	if (m_attackMachine)
	{
		m_attackMachine->halt();
	}
}

bool AIAttackSquadState::canChase(Object &owner)
{
	static const int kImmobile = CombatNames::kindOf("IMMOBILE"); // template + 0x108 bit 2
	if (owner.isKindOf((unsigned)kImmobile))
	{
		return false; // RW 0x7459BA
	}
	Object *outer = owner.getContainedBy(); // RW 0x7459C3 .. 0x7459EB: the outermost container (+ 0x27C)
	while (outer && outer->getContainedBy())
	{
		outer = outer->getContainedBy();
	}
	if (outer && outer->isKindOf((unsigned)kImmobile))
	{
		return false;
	}
	const ObjectWeapons *w = owner.getWeapons();
	if (const Weapon *cur = w ? w->currentWeapon() : nullptr) // RW 0x68B58C(0)
	{
		if (cur->getTemplate() && (cur->getTemplate()->m_meleeWeapon || cur->getTemplate()->m_chaseWeapon))
		{
			return true; // RW 0x441B59 (+ 0x125) / RW 0x6CA144 (+ 0x126)
		}
	}
	// RW 0x745A18 .. 0x745A5C: a human player's unit with an AI whose machine is not in state 0x3E (not defined in the port), whose last command came from
	// the AI (vslot 0x23C), without the AI byte + 0x3C1 and not charge-ordered (Object + 0x251) cannot chase (S-1186: the byte and the charge order are
	// written by no ported code)
	const Player *p = owner.getControllingPlayer();
	const AIUpdateInterface *ai = owner.getAIUpdateInterface();
	if (p && p->getPlayerType() == PLAYER_HUMAN && ai && ai->lastCommandSource() == CMD_FROM_AI)
	{
		return false;
	}
	return true;
}

bool AIAttackSquadState::canTargetSquadMember(Object &member)
{
	// RW 0x747AFE
	static const int kFlailing = CombatNames::modelCondition("STUNNED_FLAILING"), kFreefall = CombatNames::modelCondition("FREEFALL");
	if (PhysicsBehavior::find(member) && (member.testModelCondition(kFlailing) || member.testModelCondition(kFreefall)))
	{
		return false; // + 0x264 and RW 0x792973
	}
	static const int kUnattackable = CombatNames::kindOf("UNATTACKABLE"); // template + 0x10E bit 6
	if (member.isKindOf((unsigned)kUnattackable) || InvisibilityManager::isInvisible(member))
	{
		return false; // RW 0x68FC2F
	}
	static const int kStealthed = CombatNames::status("STEALTHED"), kDetected = CombatNames::status("DETECTED");
	return !(member.testStatus((unsigned)kStealthed) && !member.testStatus((unsigned)kDetected)); // RW 0x44DDEC(0xF), (0x11)
}

Object *AIAttackSquadState::chooseVictim()
{
	AISquad *squad = machine().goalSquad(); // RW 0x752796: machine + 0x4C
	if (!squad)
	{
		return nullptr;
	}
	Object &obj = owner();
	GameLogic &logic = obj.logic();
	const unsigned mood = ai().moodMatrixValue(); // RW 0x7527B4
	// RW 0x7527B9 .. 0x752801: the remembered victim (Object + 0x4A8) is never set in the port (S-1186): nothing kept
	if ((mood & 2u) && (mood & 0x2100u))
	{
		return nullptr; // RW 0x75280D: a computer player's, byte 1 & 0x21
	}
	if ((mood & 2u) && (mood & 0x200u))
	{
		const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule()); // RW 0x752819 .. 0x752837: + 0x25C, vslot 0x40, + 8
		return body ? logic.findObjectByID(body->lastDamager()) : nullptr;
	}
	{
		ObjectWeapons *w = obj.getWeapons();
		Weapon *weapon = w ? w->currentWeapon() : nullptr; // RW 0x75284B
		std::vector<Object *> live = squad->getLiveObjects(logic); // RW 0x752858
		float best = FLT_MAX;                             // RW 0xBD1910
		Object *victim = nullptr;
		bool inRange = false;
		static const int kNeedsLos = CombatNames::kindOf("ATTACK_NEEDS_LINE_OF_SIGHT"); // template + 0x10F bit 3
		static const int kOverWalls = CombatNames::kindOf("CAN_SHOOT_OVER_WALLS");       // template + 0x122 bit 6
		for (Object *o : live)
		{
			if (!canTargetSquadMember(*o))
			{
				continue;
			}
			if (!w || !w->canAttackObject(*o, CMD_FROM_AI, false) || o->isEffectivelyDead())
			{
				continue; // the filter 0xC0F368 (RW 0x660CA4), + 0x458 bit 0
			}
			// RW 0x66137C (x87 at PC24): (owner - it) in x and y, dy * dy + dx * dx, stored as a float (RW 0x7528DE)
			const double dx = SimMath::pc24SubW((double)obj.getPosition()->x, (double)o->getPosition()->x);
			const double dy = SimMath::pc24SubW((double)obj.getPosition()->y, (double)o->getPosition()->y);
			const float d = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)));
			float score = d;
			if (weapon && w->isWithinAttackRange(*o, 0.0f, true)) // RW 0x6CC653(owner, it, 0, 1)
			{
				const bool melee = weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon; // RW 0x441B59 (RW 0x9188EB after it answers false)
				// RW 0x744AAA: a MeleeWeapon, or an owner neither ATTACK_NEEDS_LINE_OF_SIGHT nor CAN_SHOOT_OVER_WALLS, or the line of sight filter
				const bool sight = melee || !(obj.isKindOf((unsigned)kNeedsLos) || obj.isKindOf((unsigned)kOverWalls)) || CombatLineOfSight::filterAllows(obj, *o);
				if (!melee && !sight && !canChase(obj))
				{
					continue;
				}
				score = SimMath::sseMul(d, 0.25f); // RW 0x752938: mulss by RW 0xBD1904
				inRange = true;
			}
			if (score < best)
			{
				best = score;
				victim = o;
			}
		}
		if (m_engaged && !inRange && weapon)
		{
			// RW 0x75297A .. 0x752AFB: the pathfinder's nearby query (RW 0x6F3C72 -> RW 0x6F2799) is not ported (S-1186): no further candidates
			logic.noteStop("[S-1186] AIAttackSquadState::chooseVictim: the pathfinder's nearby query (RW 0x6F2799) of an engaged unit is not ported");
		}
		return victim;
	}
}

bool AIAttackSquadState::mayEngage(Object &obj, Object *victim)
{
	ObjectWeapons *w = obj.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	if (!weapon || !victim || w->isWithinAttackRange(*victim, 0.0f, true))
	{
		return true;
	}
	return canChase(obj); // and not the AI byte + 0x3CC (S-583: never set)
}

StateReturnType AIAttackSquadState::onEnter()
{
	Object &obj = owner();
	m_attackMachine = makeAttackThenIdleMachineFor(ai()); // RW 0x753475: the machine's vslot 0x24
	Object *victim = chooseVictim();
	if (!mayEngage(obj, victim))
	{
		return STATE_FAILURE; // RW 0x7534C2
	}
	const ObjectID id = victim ? victim->getID() : (ObjectID)INVALID_ID;
	m_attackMachine->setGoalObject(id); // vslot 0x38
	ai().setCurrentVictim(victim);      // RW 0x6682B1
	machine().setGoalObject(id);        // vslot 0x38, then RW 0x6627F0 (the same machine)
	m_engaged = false;                  // + 0x24
	return m_attackMachine->initDefaultState(); // vslot 0x1C
}

void AIAttackSquadState::onExit(StateExitType)
{
	ai().setCurrentVictim(nullptr); // RW 0x742F88: RW 0x6682B1(0); Object + 0x4A8 = 0 (never set here, S-1186)
	m_keptMachines.drop(m_attackMachine);
}

bool AIAttackSquadState::isActive() const
{
	return m_attackMachine && m_attackMachine->currentState() && !m_attackMachine->currentState()->isIdle();
}

StateReturnType AIAttackSquadState::update()
{
	if (!m_attackMachine)
	{
		return STATE_FAILURE; // RW 0x75350B
	}
	Object &obj = owner();
	static const int kMeleeAttacking = CombatNames::status("IS_MELEE_ATTACKING");
	if (obj.testStatus((unsigned)kMeleeAttacking))
	{
		m_engaged = true; // RW 0x753525
	}
	Object *goalObj = machine().goalObject(); // RW 0x75353A: RW 0x8DBACE (the goal id looked up, RW 0x449681)
	if (goalObj != m_attackMachine->goalObject())
	{
		m_attackMachine->setGoalObject(goalObj ? goalObj->getID() : (ObjectID)INVALID_ID); // vslot 0x38
	}
	StateReturnType ret;
	{
		KeptSubMachines<AIStateMachine>::Run run(m_keptMachines);
		ret = m_attackMachine->updateStateMachine(); // RW 0x75355B: vslot 0x10 (not under the parent's lock)
	}
	if (IS_STATE_SLEEP(ret))
	{
		ret = STATE_CONTINUE; // RW 0x753560
	}
	if (!m_attackMachine)
	{
		return STATE_CONTINUE; // RW 0x753569
	}
	if (m_attackMachine->currentStateId() != AI_IDLE)
	{
		return ret; // RW 0x753582: a state that is not idle (none counts as 999999)
	}
	ObjectWeapons *w = obj.getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr; // RW 0x75358B
	if (!weapon)
	{
		return STATE_FAILURE;
	}
	GameLogic &logic = obj.logic();
	if (goalObj && !goalObj->isEffectivelyDead())
	{
		// RW 0x7535BA: canApproachToTarget(owner, the goal's position, the weapon, 0)
		if (!ai().world().mapReady())
		{
			logic.noteStop("[S-326] AIAttackSquadState: no pathfinder for the approach test RW 0x6FB754: the goal is taken as unreachable");
			return STATE_FAILURE;
		}
		const bool melee = weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon;
		if (!ai().world().pathfinder().canApproachToTarget(ai().adapter(), ai().locomotorInfo(), *goalObj->getPosition(), w->currentAttackRange(), melee, false))
		{
			return STATE_FAILURE;
		}
	}
	static const int kHordeMember = CombatNames::status("HORDE_MEMBER");
	if (obj.testStatus((unsigned)kHordeMember) && obj.getContainedBy())
	{
		return STATE_SUCCESS; // RW 0x7535C7 .. 0x7535DB
	}
	// RW 0x7535E1 .. 0x7535F7: the AI's crate (RW 0x66810B) -> state 0x27: no crate in the port (S-1188)
	Object *victim = chooseVictim();
	if (!victim)
	{
		return STATE_SUCCESS; // RW 0x753607
	}
	static const int kBloodthirsty = CombatNames::status("BLOODTHIRSTY");
	static const int kMine = CombatNames::kindOf("MINE");     // template + 0x10E bit 7
	static const int kWebbed = CombatNames::kindOf("WEBBED"); // + 0x11B bit 3
	static const int kTree = CombatNames::kindOf("TREE");     // + 0x113 bit 6
	if (!obj.testStatus((unsigned)kBloodthirsty) && !victim->isKindOf((unsigned)kMine) && !victim->isKindOf((unsigned)kWebbed) && !victim->isKindOf((unsigned)kTree) &&
		victim->getControllingPlayer() == obj.getControllingPlayer())
	{
		// RW 0x753648 .. 0x753663: the AI machine's, the machine's and the attack machine's goals cleared
		machine().setGoalObject(INVALID_ID);
		m_attackMachine->setGoalObject(INVALID_ID);
		return STATE_SUCCESS;
	}
	if (!mayEngage(obj, victim))
	{
		return STATE_FAILURE; // RW 0x753698
	}
	m_attackMachine->setGoalObject(victim->getID()); // RW 0x7536A3
	ai().setCurrentVictim(victim);                   // RW 0x7536A9
	machine().setGoalObject(victim->getID());        // RW 0x7536B4, RW 0x6627F0
	m_attackMachine->setState(AI_ATTACK_OBJECT);     // RW 0x7536C6: 10
	return STATE_CONTINUE;
}

void AIAttackSquadState::crc(StateHasher &h) const
{
	h.addBool(m_engaged);
	h.addBool(m_attackMachine != nullptr);
	if (m_attackMachine)
	{
		m_attackMachine->crc(h);
	}
}

void AIAttackSquadState::xfer(Xfer &x)
{
	// RW 0x742EF5's fields (version 1: the "has machine" bool, then the machine) and + 0x24, OpenBFME layout
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_engaged);
	bool has = m_attackMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_attackMachine)
		{
			m_attackMachine->xfer(x);
		}
		return;
	}
	std::unique_ptr<AIStateMachine> loaded;
	if (has)
	{
		loaded = makeAttackThenIdleMachineFor(ai());
		loaded->xfer(x);
	}
	m_attackMachine = std::move(loaded);
}
