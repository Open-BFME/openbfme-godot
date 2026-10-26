// OpenBFME. GPL-3.0.
// AIUpdateInterface, lane COMBAT-1 part: the attack commands, the victim, the mood scan, death. See GameLogic/Module/AIUpdate.h and GameLogic/AI/AIAttack.h.

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
std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}

// "Name = tok tok ; comment" -> name and tokens (the comment markers of the INI: ';' and '//')
bool splitField(const std::string &line, std::string &name, std::vector<std::string> &tokens)
{
	std::string text = line;
	const size_t semi = text.find(';');
	if (semi != std::string::npos)
	{
		text.erase(semi);
	}
	const size_t slashes = text.find("//");
	if (slashes != std::string::npos)
	{
		text.erase(slashes);
	}
	const size_t eq = text.find('=');
	if (eq == std::string::npos)
	{
		return false;
	}
	name = trim(text.substr(0, eq));
	tokens.clear();
	std::string cur;
	for (char c : text.substr(eq + 1))
	{
		if (std::isspace((unsigned char)c))
		{
			if (!cur.empty())
			{
				tokens.push_back(cur);
				cur.clear();
			}
		}
		else
		{
			cur += c;
		}
	}
	if (!cur.empty())
	{
		tokens.push_back(cur);
	}
	return !name.empty();
}

bool ieq(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && !b[i];
}
} // namespace

// the AI module fields the mood scan reads, from the raw module data (the typed data of the AI classes is not ported, stop S-326: only these rows are read; their parsers
// are the INI's: parseDurationUnsignedInt RW 0x73A429 (ms -> ceil(ms * 0.005f)), the index list of RW 0xDA1404 for AutoAcquireEnemiesWhenIdle, parseReal, parseBool)
void AIUpdateInterface::readCombatSettings(const ModuleData *data)
{
	m_combat = CombatSettings();
	m_attackInfoGeneration = 0xFFFFFFFFu; // lane TARGET-1: AI + 0x70 is looked up again from the (new) name
	m_minCowerTime = m_maxCowerTime = 0; // lane MODULES-3: RW ctor 0x66E8FF (+ 0x34 / + 0x30 = 0)
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data);
	if (!raw)
	{
		return;
	}
	for (const RawModuleData::Line &l : raw->lines())
	{
		std::string name;
		std::vector<std::string> tok;
		if (!splitField(l.text, name, tok) || tok.empty())
		{
			continue;
		}
		if (ieq(name, "MoodAttackCheckRate"))
		{
			m_combat.moodAttackCheckRate = NumericState::ceilScaled((std::uint32_t)std::strtoul(tok[0].c_str(), nullptr, 10), 0.005f);
		}
		else if (ieq(name, "AutoAcquireEnemiesWhenIdle"))
		{
			unsigned flags = 0;
			for (const std::string &t : tok)
			{
				if (ieq(t, "Yes"))
					flags |= 1u;
				else if (ieq(t, "STEALTHED"))
					flags |= 2u;
				else if (ieq(t, "No"))
					flags |= 4u;
				else if (ieq(t, "NOTWHILEATTACKING"))
					flags |= 8u;
				else if (ieq(t, "ATTACK_BUILDINGS"))
					flags |= 16u;
			}
			m_combat.autoAcquire = flags;
		}
		else if (ieq(name, "MinCowerTime") || ieq(name, "MaxCowerTime"))
		{
			// lane MODULES-3: the field table RW 0xC0F530, parseDurationUnsignedInt (+ 0x34 / + 0x30), read by the panic cower state (RW 0x749693)
			const unsigned frames = NumericState::ceilScaled((std::uint32_t)std::strtoul(tok[0].c_str(), nullptr, 10), 0.005f);
			(ieq(name, "MinCowerTime") ? m_minCowerTime : m_maxCowerTime) = frames;
		}
		else if (ieq(name, "StopChaseDistance"))
		{
			m_combat.stopChaseDistance = strtofPortable(tok[0].c_str(), nullptr); // retail: sscanf "%f" (lane WIN-1)
		}
		else if (ieq(name, "HoldGroundCloseRangeDistance"))
		{
			m_combat.holdGroundCloseRangeDistance = strtofPortable(tok[0].c_str(), nullptr); // lane TARGET-1: + 0x28, parseReal (sscanf "%f")
		}
		else if (ieq(name, "StandGround"))
		{
			m_combat.standGround = ieq(tok[0], "Yes") || ieq(tok[0], "True") || tok[0] == "1";
		}
		else if (ieq(name, "CanAttackWhileContained"))
		{
			m_combat.canAttackWhileContained = ieq(tok[0], "Yes") || ieq(tok[0], "True") || tok[0] == "1"; // lane IDLE-1: as StandGround (the same parseBool RW 0x42E558)
		}
		else if (ieq(name, "AttackPriority"))
		{
			m_combat.attackPriority = tok[0];
		}
		else if (ieq(name, "SpecialContactPoints"))
		{
			for (const std::string &t : tok)
			{
				m_combat.specialContactPoints.push_back(t);
			}
		}
	}
}

bool AIUpdateInterface::findNearestLabeledContactPointOnTarget(const Object &target, Coord3D &out, const Coord3D &callerPos, bool preferred) const
{
	// RW 0x667C76: the labels of module data + 0x58 in order, the first success wins
	for (const std::string &label : m_combat.specialContactPoints)
	{
		if (ObjectGeometry::worldspaceBestContactPoint(target, callerPos, label, preferred, out))
		{
			return true;
		}
	}
	return false;
}

void AIUpdateInterface::setCurrentVictim(Object *victim)
{
	m_currentVictim = victim ? victim->getID() : (ObjectID)INVALID_ID;
}

bool AIUpdateInterface::isAttacking() const
{
	const unsigned id = currentStateId();
	return id == (unsigned)AI_ATTACK_OBJECT || id == (unsigned)AI_FORCE_ATTACK_OBJECT || id == (unsigned)AI_ATTACK_POSITION;
}

Object *AIUpdateInterface::currentVictim() const
{
	return m_currentVictim == INVALID_ID ? nullptr : getObject()->logic().findObjectByID(m_currentVictim);
}

// a lone unit ordered against a horde fights the horde's nearest member (the horde object itself has an ImmortalBody: nothing to kill); a horde fights the horde object (its
// members decide whom they hit, HordeAttack.cpp)
static Object *nearestMemberOf(Object &hordeObj, const Object &from)
{
	ContainModuleInterface *c = hordeObj.getContain();
	if (!c || !c->getHordeContainInterface() || !c->getContainedItemsList())
	{
		return nullptr;
	}
	Object *best = nullptr;
	float bestD = 0.0f;
	for (Object *m : *c->getContainedItemsList())
	{
		if (!CombatQueries::isAlive(*m))
		{
			continue;
		}
		const float d = CombatQueries::centerDistanceSquared2D(*m->getPosition(), *from.getPosition());
		if (!best || d < bestD || (d == bestD && m->getID() < best->getID()))
		{
			best = m;
			bestD = d;
		}
	}
	return best;
}

Object *AIUpdateInterface::replacementVictim()
{
	Object *obj = getObject();
	Object *horde = m_targetHorde == INVALID_ID ? nullptr : obj->logic().findObjectByID(m_targetHorde);
	if (!horde || !CombatQueries::isAlive(*horde))
	{
		return nullptr;
	}
	return nearestMemberOf(*horde, *obj);
}

// B1 AIUpdate.cpp privateAttackObject: the unit must be able to attack, then the machine restarts in the attack state
bool AIUpdateInterface::aiAttackObject(Object *victim, CommandSourceType source, int maxShotsToFire)
{
	if (!acceptCommand(source, 0xB, victim))
	{
		return false; // lane MODULES-3: RW 0x667174
	}
	{
		RoutedCommand c; // lane FLY-1
		c.type = 0xB;
		c.maxShots = maxShotsToFire;
		c.target = victim;
		c.source = source;
		if (routeCommand(c))
		{
			return true;
		}
	}
	m_targetHorde = INVALID_ID;
	if (victim && !getObject()->isKindOf((unsigned)CombatNames::kinds().horde) && victim->getContain() && victim->getContain()->getHordeContainInterface())
	{
		m_targetHorde = victim->getID();
		victim = nearestMemberOf(*victim, *getObject());
	}
	m_world->noteStop("S-325 attack commands: aiAttackObject / aiForceAttackObject; guard, hunt, attack position / area / squad and the weapon fire commands are not executed");
	Object *obj = getObject();
	if (!victim || m_mover->isAiDead() || !CombatQueries::isAlive(*obj) || !CombatQueries::isAlive(*victim))
	{
		return false;
	}
	ObjectWeapons *w = obj->getWeapons();
	if (!w || !w->canAttackObject(*victim, source, false))
	{
		return false;
	}
	m_machine->clear();
	m_machine->setGoalObject(victim->getID());
	m_machine->setGoalPosition(*victim->getPosition());
	setLastCommandSource(source);
	m_currentVictim = victim->getID();
	m_machine->setState(AI_ATTACK_OBJECT);
	// lane PLAY-2 (Sol r1): the command's shot limit goes on the current weapon after the state's entry lifted it (RW 0x74D0FC; ZH privateAttackObject's
	// setMaxShotCount); privateAttackPosition's ContinueAttackRange redirect passes its limit here (RW 0x66DF02 .. 0x66DF0C)
	if (Weapon *cur = w->currentWeapon())
	{
		cur->setMaxShotCount(maxShotsToFire);
		// lane TARGET-1, RW 0x66BE7D .. 0x66BE99: a player's or a script's order (source 0 / 1) with a MeleeWeapon current weapon (RW 0x441B59) remembers its
		// target (AI + 0x1A4): the mood scan answers it while it lives (RW 0x66854F)
		if ((source == CMD_FROM_PLAYER || source == CMD_FROM_SCRIPT) && cur->getTemplate() && cur->getTemplate()->m_meleeWeapon)
		{
			m_orderedMeleeTarget = victim->getID();
		}
	}
	wakeUpNow();
	return true;
}

bool AIUpdateInterface::aiForceAttackObject(Object *victim, CommandSourceType source)
{
	if (!acceptCommand(source, 0xC, victim))
	{
		return false; // lane MODULES-3: RW 0x667174
	}
	{
		RoutedCommand c; // lane FLY-1
		c.type = 0xC;
		c.target = victim;
		c.source = source;
		if (routeCommand(c))
		{
			return true;
		}
	}
	m_targetHorde = INVALID_ID;
	if (victim && !getObject()->isKindOf((unsigned)CombatNames::kinds().horde) && victim->getContain() && victim->getContain()->getHordeContainInterface())
	{
		m_targetHorde = victim->getID();
		victim = nearestMemberOf(*victim, *getObject());
	}
	Object *obj = getObject();
	if (!victim || m_mover->isAiDead() || !CombatQueries::isAlive(*obj) || !CombatQueries::isAlive(*victim))
	{
		return false;
	}
	ObjectWeapons *w = obj->getWeapons();
	if (!w || !w->canAttackObject(*victim, source, true))
	{
		return false;
	}
	m_machine->clear();
	m_machine->setGoalObject(victim->getID());
	m_machine->setGoalPosition(*victim->getPosition());
	setLastCommandSource(source);
	m_currentVictim = victim->getID();
	m_machine->setState(AI_FORCE_ATTACK_OBJECT);
	wakeUpNow();
	return true;
}

bool AIUpdateInterface::aiAttackPosition(const Coord3D &pos, int maxShotsToFire, CommandSourceType source)
{
	if (!acceptCommand(source, 0xE))
	{
		return false; // RW 0x667174, AICommandType 0xE (RW 0x6961F1 -> slot 40)
	}
	{
		RoutedCommand c; // lane FLY-1
		c.type = 0xE;
		c.maxShots = maxShotsToFire;
		c.position = pos;
		c.source = source;
		if (routeCommand(c))
		{
			return false;
		}
	}
	Object *obj = getObject();
	// RW 0x66DE17 .. 0x66DE2C: a PLAYER order to a unit whose weapon set flags (Object + 0x38C, RW 0x68BE7D) hold bit 8 (RAMPAGE) is ignored
	static const int kRampage = CombatNames::weaponSetBit("RAMPAGE");
	if (source == CMD_FROM_PLAYER && BitFlagsTest(obj->getWeaponSetFlags(), (size_t)kRampage))
	{
		return false;
	}
	// RW 0x66DE32 .. 0x66DE3C: a unit outside the playable area (Object + 0x458 bit 3) ignores it; the port never sets that bit (S-920, S-2480)
	if (m_mover->isAiDead() || !CombatQueries::isAlive(*obj))
	{
		return false;
	}
	ObjectWeapons *w = obj->getWeapons();
	Coord3D localPos = pos;
	// RW 0x66DE5D .. 0x66DF19: the current weapon (RW 0x68B58C(0)) has a ContinueAttackRange (template + 0x148, > 0.0 RW 0xC1B594): the source is IGNORING_STEALTH
	// (status 0x1B) for a search of the closest object within that range of the spot (FROM_CENTER_2D) that is on the same side of the map edge (filter
	// RW 0xC0F374) and that the unit could attack as a new target (filter RW 0xC0F368); found: the order becomes an attack on it (RW 0x66C536, AICommandType 0xB)
	// with the same limit. Not found: the limit becomes ONE shot
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	const float continueRange = weapon && weapon->getTemplate() ? weapon->getTemplate()->m_continueAttackRange : 0.0f;
	if (continueRange > 0.0f)
	{
		static const int kIgnoringStealth = CombatNames::status("IGNORING_STEALTH");
		obj->setStatus((unsigned)kIgnoringStealth, true);
		// INFERENCE (S-2480): PartitionFilterPossibleToAttack(ATTACK_NEW_TARGET) is the non-forced canAttackObject; the off-map filter keeps everything (the bit is never set)
		PartitionFilterFn possible([&](Object &o) { return w->canAttackObject(o, source, false); });
		Object *victim = obj->logic().partition().getClosestObject(localPos, continueRange, FROM_CENTER_2D, { &possible });
		obj->setStatus((unsigned)kIgnoringStealth, false);
		if (victim)
		{
			return aiAttackObject(victim, source, maxShotsToFire); // RW 0x66DF02: the limit is forwarded
		}
		maxShotsToFire = 1;
	}
	// RW 0x66DF20 .. 0x66DFA1: the contact weapon test (RW 0x9188EB) always answers false in RotWK, so the spot is never moved to a reachable one
	// RW 0x66DFA3 .. 0x66E006: already attacking (state 9) a goal within 0.0001 (RW 0xBD19E0 / 0xBD19E4) on each axis: nothing changes
	if (currentStateId() == (unsigned)AI_ATTACK_POSITION)
	{
		const Coord3D &g = m_machine->goalPosition();
		const float dx = SimMath::subf32(g.x, localPos.x), dy = SimMath::subf32(g.y, localPos.y), dz = SimMath::subf32(g.z, localPos.z);
		if (dx < 0.0001f && dx > -0.0001f && dy < 0.0001f && dy > -0.0001f && dz < 0.0001f && dz > -0.0001f)
		{
			return false;
		}
	}
	// RW 0x66E00D .. 0x66E04E: clear, destroy the path (RW 0x66276B), the clipped goal (RW 0x6654AD), the source (+ 0x48), state 9, no goal object, then the
	// current weapon's shot limit (+ 0x34) and its shot counter (+ 0x20, INFERENCE: the port's weapon keeps no such counter apart from the timing fields)
	m_machine->clear();
	m_mover->destroyPath();
	Coord3D goal;
	goalPositionClipped(localPos, source, goal);
	m_machine->setGoalPosition(goal);
	setLastCommandSource(source);
	m_targetHorde = INVALID_ID;
	m_currentVictim = INVALID_ID;
	m_machine->setState(AI_ATTACK_POSITION);
	m_machine->setGoalObject(INVALID_ID);
	if (Weapon *cur = w ? w->currentWeapon() : nullptr)
	{
		cur->setMaxShotCount(maxShotsToFire);
	}
	// RW 0x66E051 .. 0x66E060: the attack voice for a PLAYER or SCRIPT order (RW 0x66B271) is the client's (UnitVoiceResponse on the message)
	wakeUpNow();
	return true;
}

// RW 0x698F06 (AI part): the unit stops everything it was doing and is marked dead; the path and the locomotor goal go
void AIUpdateInterface::onDie()
{
	if (m_mover->isAiDead())
	{
		return;
	}
	m_mover->markAiDead();
	if (m_machine)
	{
		m_machine->clearTemporaryState(); // lane MODULES-3 INFERENCE (S-1027): a dying unit leaves a locked emotion state (RW's dead path through the lock is not read)
		m_machine->clear();
		m_machine->setState(AI_DEAD);
	}
	m_mover->destroyPath();
	m_mover->endingMove();
	m_currentVictim = INVALID_ID;
	getObject()->setModelConditionState(modelConditionBit("MOVING"), false);
}

// ---- lane SCRIPT-3: the attitude ----

void AIUpdateInterface::setAttitude(int attitude)
{
	m_attitude = attitude;
	if (attitude != -3)
	{
		return;
	}
	if (currentVictim())
	{
		m_machine->setGoalObject(INVALID_ID); // RW 0x66E145: the machine's goal object (vslot 0x38(0)) is cleared before the command's gate (review r1)
		aiIdle(CMD_FROM_AI);                  // RW 0x66E154 (refused while a temporary state is locked: the goal stays cleared)
	}
	setCurrentVictim(nullptr);
}

int AIUpdateInterface::attitude() const
{
	const AIUpdateInterface *ai = this;
	for (int depth = 0; depth < 8; ++depth)
	{
		const Object *o = ai->getObject();
		if (!o || !o->testStatus((unsigned)CombatNames::statuses().hordeMember))
		{
			break;
		}
		const Object *horde = o->getHordeObject(false);
		const AIUpdateInterface *hai = horde && horde != o ? const_cast<Object *>(horde)->getAIUpdateInterface() : nullptr;
		if (!hai)
		{
			break;
		}
		ai = hai;
	}
	return ai->m_attitude;
}

unsigned AIUpdateInterface::moodMatrixValue() const
{
	const Player *p = getObject()->getControllingPlayer();
	if (!p)
	{
		return 0;
	}
	if (p->getPlayerType() == PLAYER_HUMAN)
	{
		return 1;
	}
	switch (attitude())
	{
	case -3: return 0x2002;
	case -2: return 0x102;
	case -1: return 0x202;
	case 0: return 0x402;
	case 1: return 0x802;
	case 2: return 0x1002;
	default: return 0x402;
	}
}

unsigned AIUpdateInterface::moodAdjustment(int action) const
{
	const Object &obj = *getObject();
	static const int infantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY");
	static const int ignoredInGui = ObjectTemplateInfoBuilder::kindOfIndex("IGNORED_IN_GUI");
	if (infantry >= 0 && ignoredInGui >= 0 && obj.isKindOf((unsigned)infantry) && obj.isKindOf((unsigned)ignoredInGui))
	{
		return 1;
	}
	const unsigned value = moodMatrixValue();
	if (value & 1u)
	{
		return 1;
	}
	const unsigned mood = value & 0x1F00u;
	switch (action)
	{
	case 0:
		return mood == 0x100u ? 0x11u : mood == 0x200u ? 0x21u : mood == 0x800u ? 0x41u : mood == 0x1000u ? 0x81u : 1u;
	case 1:
		return mood == 0x100u ? 0x12u : mood == 0x200u ? 0x21u : mood == 0x800u ? 0x44u : mood == 0x1000u ? 0x84u : 1u;
	case 2:
		return mood == 0x100u || mood == 0x2000u ? 0x12u : 1u;
	case 3:
		return mood == 0x100u || mood == 0x2000u ? 0x12u : mood == 0x800u ? 0x41u : mood == 0x1000u ? 0x81u : 1u;
	default:
		return 1u;
	}
}

// ---- lane TARGET-1: the mood scan, RotWK's getNextMoodTarget (RW 0x66844A; ZH AIUpdate.cpp getNextMoodTarget is the donor of the shape) ----

const AttackPriorityInfo *AIUpdateInterface::attackInfo()
{
	// AI + 0x70: the AI constructor's getAttackInfo of the module data's AttackPriority (+ 0x44, "DefaultAttackPriority" from the module data constructor
	// RW 0x66E8FF), null for an empty name (RW 0x66ECFE); looked up again when the script engine's table was reset (the entries are made again with the
	// same values)
	ScriptEngine &se = getObject()->logic().scriptEngine();
	if (m_attackInfoGeneration != se.attackPriorities().generation())
	{
		m_attackInfoGeneration = se.attackPriorities().generation();
		m_attackInfo = m_combat.attackPriority.empty() ? nullptr : se.attackPriorities().getAttackInfo(m_combat.attackPriority);
	}
	return m_attackInfo;
}

// RW 0x662DD9 (ZH resetNextMoodCheckTime, RotWK's: never earlier): AIData ForceIdleMSEC frames from now unless the check is later already, and the next check
// randomly offset
void AIUpdateInterface::resetNextMoodCheckTime()
{
	const unsigned at = getObject()->logic().settings().forceIdleFrames + frame();
	if (m_nextMoodCheck < at)
	{
		m_nextMoodCheck = at;
	}
	m_randomlyOffsetMoodCheck = true;
}

// RW 0x662DB3 (ZH wakeUpAndAttemptToTarget): an idle unit (AI vslot 0x1B8) checks now, randomly offset
void AIUpdateInterface::wakeUpAndAttemptToTarget()
{
	if (isIdle())
	{
		m_nextMoodCheck = frame();
		m_randomlyOffsetMoodCheck = true;
	}
}

namespace
{
// the mood scan's optional filter (vtable 0xC0F3AC, allow RW 0x662E2B; passed for a human player's unit): the current weapon (RW 0x68B58C(0)) reaches the
// candidate now (RW 0x6CC653(me, it, 0, 1)), or it is a MeleeWeapon (+ 0x125) or a ChaseWeapon (+ 0x126), or the player is not human, or the unit has no AI,
// the AI byte + 0x3C1, or the charge order (Object + 0x251, Locomotor's isChargeOrdered); no weapon: every candidate. Only the scored search uses it.
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
		// the AI's + 0x3C1 byte (written by no ported code) and the charge order (Locomotor + 0x251 for the object) are not modelled here: false (S-326)
		return !m_me.getAIUpdateInterface();
	}

private:
	Object &m_me;
};

bool teamAttacksCommonTarget(const Team *t)
{
	return t && t->getPrototype() && t->getPrototype()->getDict().getBool("teamAttackCommonTarget"); // prototype + 0x30 -> + 0x216
}
} // namespace

Object *AIUpdateInterface::getNextMoodTarget(bool calledByAI, bool calledDuringIdle)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	if (!logic.combat().autoAcquireEnabled())
	{
		return nullptr; // the port's switch for the movement sweeps (not retail state)
	}
	static const int kAbilityBusy = CombatNames::status("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING"); // 0x46
	static const int kNoAutoAcquire = CombatNames::status("NO_AUTO_ACQUIRE");                           // 0x17
	static const int kHordeMember = CombatNames::status("HORDE_MEMBER");                                // 0x26
	static const int kIgnoreAi = CombatNames::status("IGNORE_AI_COMMAND");                              // 0x4A
	static const int kStandGround = CombatNames::status("STAND_GROUND");                                // 0x44
	static const int kFleeOffMap = CombatNames::status("FLEE_OFF_MAP");                                 // 0x32
	static const int kContesting = CombatNames::status("CONTESTING_BUILDING");                          // 0x25
	static const int kHidden = CombatNames::status("HIDDEN");                                           // 0x10
	static const int kInsideGarrison = CombatNames::status("INSIDE_GARRISON");                          // 0x3A
	static const int kJustBuilt = CombatNames::modelCondition("JUST_BUILT");                            // + 0x124 bit 26
	static const int kComingOut = CombatNames::modelCondition("COMING_OUT_OF_FACTORY");                 // + 0x134 bit 13
	static const int kDozer = CombatNames::kindOf("DOZER");                                             // + 0x109 bit 6
	static const int kHorde = CombatNames::kindOf("HORDE");                                             // + 0x115 bit 5
	static const int kAttackNeedsLos = CombatNames::kindOf("ATTACK_NEEDS_LINE_OF_SIGHT");               // + 0x10F bit 3
	const unsigned kCloseRange = 7; // TheWeaponConditionNames (RW 0xDA1328) CLOSE_RANGE
	// RW 0x66849D .. 0x6684EC: dead or OFF_MAP (+ 0x458 bits 0 / 3; lane CAMPUI-1: OFF_MAP from the map's extent, RW 0x69355D: MAP ANG Angmar's
	// counterattack wolf riders wait below the map's edge until the scripts send them in), SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING, the AI's temporary state
	// machine (+ 0x34: BeginTemporaryStateMachine RW 0x6630A9, which nothing calls in the port, S-1222), NO_AUTO_ACQUIRE, HORDE_MEMBER, IGNORE_AI_COMMAND,
	// the model conditions JUST_BUILT (lane SPELL-3: a castle member fading in) and COMING_OUT_OF_FACTORY: nothing
	if (obj.isEffectivelyDead() || obj.isOffMap() || obj.testStatus((unsigned)kAbilityBusy) || obj.testStatus((unsigned)kNoAutoAcquire) || obj.testStatus((unsigned)kHordeMember) ||
		obj.testStatus((unsigned)kIgnoreAi) || obj.testModelCondition(kJustBuilt) || obj.testModelCondition(kComingOut))
	{
		return nullptr;
	}
	const Player *player = obj.getControllingPlayer();
	// RW 0x6684F2 .. 0x668515: STAND_GROUND, or a human player's unit, needs its target in reach (the qualifier 0x10 and the final test below)
	const bool needsReach = obj.testStatus((unsigned)kStandGround) || (player && player->getPlayerType() == PLAYER_HUMAN);
	// RW 0x668517 .. 0x668546: a DOZER busy with a task (its dozer interface, AI vslot 0x174: slot 0x24 the current task, slot 0x20 any pending) looks for nothing
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
	// RW 0x66854F .. 0x668589: the target a player's or script's attack order gave a melee weapon (AI + 0x1A4, RW 0x66BD33) is answered while it lives and
	// is not FLEE_OFF_MAP; else it is forgotten
	if (m_orderedMeleeTarget != INVALID_ID)
	{
		Object *t = logic.findObjectByID(m_orderedMeleeTarget);
		if (t && !t->isEffectivelyDead() && !t->testStatus((unsigned)kFleeOffMap))
		{
			return t;
		}
		m_orderedMeleeTarget = INVALID_ID;
	}
	// RW 0x66858C .. 0x6685F5 (the module data, AI + 4): during idle only with AutoAcquireEnemiesWhenIdle Yes (+ 0x1C bit 0); NOTWHILEATTACKING (bit 3) while
	// the current state is an attack (AI vslot 0x1BC, RW 0x662B3C: the state's slot 0x24); lane IDLE-1: a contained object only with CanAttackWhileContained
	// (+ 0x25), CONTESTING_BUILDING or its own contain's vslot 0xB8, and a container that is not JUST_BUILT
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
		const bool allowed = m_combat.canAttackWhileContained || obj.testStatus((unsigned)kContesting) || (own && own->moodScanWhileContained());
		if (!allowed || container->testModelCondition(kJustBuilt))
		{
			return nullptr;
		}
	}
	if (calledDuringIdle)
	{
		// lane STEALTH-1 (RW 0x6685F7 .. 0x668676): a CAMOUFLAGE object scans unless its StancesBehavior's stance class is 3 (RW 0x861D8E); any other object that
		// is stealthed and undetected (RW 0x694C0D, no viewer) scans only with AutoAcquireEnemiesWhenIdle STEALTHED (bit 1); the container's slot 0xB0 exception
		// (RW 0x668653) is not ported (S-1040)
		if (InvisibilityManager::invisibilityType(obj) == InvisibilityNugget::CAMOUFLAGE)
		{
			if (const StancesBehavior *sb = dynamic_cast<const StancesBehavior *>(obj.findModule("StancesBehavior")))
			{
				if (StancesBehavior::stanceClass(sb->getStance()) == 3)
				{
					return nullptr;
				}
			}
		}
		else if (InvisibilityManager::isStealthedAndUndetected(obj, nullptr) && !(m_combat.autoAcquire & 2u))
		{
			return nullptr;
		}
	}
	// RW 0x668678: HIDDEN looks for nothing
	if (obj.testStatus((unsigned)kHidden))
	{
		return nullptr;
	}
	if (calledByAI)
	{
		// RW 0x66868F .. 0x6686C4: a team that attacks a common target and has one, for an attitude of at least normal (RW 0x664CD6)
		Team *team = obj.getTeam();
		if (teamAttacksCommonTarget(team))
		{
			if (Object *teamVictim = AIHuntState::teamTarget(*team, logic)) // RW 0x7A3E36
			{
				if (attitude() >= 0)
				{
					return teamVictim;
				}
			}
		}
		// RW 0x6686C6 .. 0x66870F: the cadence (AI + 0x21C): MoodAttackCheckRate (module data + 0x18) from now, offset once by GameLogicRandomValue(-half,
		// half) (line 0x2079) when AI + 0x3BC is set
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
			m_nextMoodCheck = (unsigned)((int)m_nextMoodCheck + logic.random().getValue(-half, half, "AIUpdate.cpp", 0x2079));
			m_randomlyOffsetMoodCheck = false;
		}
	}
	// RW 0x668716: the range, RW 0x6FF412(obj, OWNERTYPE | MOOD), plus the container's bounding circle (+ 0xB8)
	float range = AI::adjustedVisionRange(obj, 3u);
	if (!(range > 0.0f))
	{
		return nullptr;
	}
	if (const Object *container = obj.getContainedBy())
	{
		range = SimMath::addf32(CombatQueries::boundingCircleRadius(*container), range);
	}
	// lane SCRIPT-3 (RW 0x668748 .. 0x668787): a computer player's PASSIVE unit answers only its last attacker (the body's last damage info, vslot 0x40 + 8);
	// attitude -3 (mood 0x2000) looks for nothing
	const unsigned mood = moodMatrixValue(); // RW 0x664D7E
	if ((mood & 2u) && (mood & 0x200u))
	{
		const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule());
		return body ? logic.findObjectByID(body->lastDamager()) : nullptr;
	}
	if (mood & 0x2000u)
	{
		return nullptr;
	}
	// RW 0x66878D .. 0x6687B5: a HORDE INSIDE_GARRISON whose members are still on their way (horde interface slot 0xF4) looks for nothing
	if (obj.isKindOf((unsigned)kHorde) && obj.testStatus((unsigned)kInsideGarrison))
	{
		ContainModuleInterface *c = obj.getContain();
		HordeContainInterface *h = c ? c->getHordeContainInterface() : nullptr;
		if (h && !h->allMembersEntered())
		{
			return nullptr;
		}
	}
	// RW 0x6687BB .. 0x668843: the qualifiers
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
	// RW 0x668843 .. 0x6688EA (AI + 0x3CC, the player's stop order RW 0x771F49, and the body's last attacker): the port never sets + 0x3CC (S-326).
	// RW 0x6688EC .. 0x668919: the firing tracker's take-once object (Object + 0x248 -> + 0x38, RW 0x68B666 / 0x8E2E0A), its writer not identified and never
	// set in the port (S-326).
	MoodTargetFilter moodFilter(obj);
	Object *target = AI::findClosestEnemy(obj, range, q, attackInfo(), player && player->getPlayerType() == PLAYER_HUMAN ? &moodFilter : nullptr, 0); // RW 0x701443
	// RW 0x6689F0: a lone unit's HORDE target becomes one of its members (RW 0x68D30E(me, 0, 0))
	if (target && !obj.isKindOf((unsigned)kHorde) && target->isKindOf((unsigned)kHorde))
	{
		target = AI::adjustVictim(*target, &obj, false, 0);
	}
	// RW 0x668A26 .. 0x668A4A: a unit that needs its target in reach and cannot reach it, or that cannot attack at all (RW 0x691269), answers nothing
	if ((calledByAI && needsReach && target && !AI::canAttackFromHere(obj, *target)) || !obj.isAbleToAttack())
	{
		return nullptr;
	}
	if (!target)
	{
		return nullptr;
	}
	// RW 0x668A57 .. 0x668AB6: a CLOSE_RANGE weapon set (Object + 0x38C bit 7, RW 0x68BE7D) out of reach with HoldGroundCloseRangeDistance (module data
	// + 0x28) above 0 drops a target farther than it (Coord3D::length of the 3D difference)
	if ((obj.getWeaponSetFlags()[kCloseRange >> 5] & (1u << (kCloseRange & 31))) && !AI::canAttackFromHere(obj, *target) && m_combat.holdGroundCloseRangeDistance > 0.0f)
	{
		const float d = (float)SimMath::length3d(SimMath::subf32(target->getPosition()->x, obj.getPosition()->x), SimMath::subf32(target->getPosition()->y, obj.getPosition()->y),
			SimMath::subf32(target->getPosition()->z, obj.getPosition()->z));
		if (m_combat.holdGroundCloseRangeDistance < d)
		{
			return nullptr;
		}
	}
	// RW 0x668ABC .. 0x668AE5: a HORDE's target held by a HORDE container is that container
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

// ZH groupAttackMoveToPosition (stop S-328)
bool AIUpdateInterface::armAttackMove(const Coord3D &goal, CommandSourceType source)
{
	if (!acceptCommand(source, 0xF))
	{
		return false; // lane MODULES-3 r2: RW 0x667174 refuses the order before it changes anything
	}
	{
		RoutedCommand c; // lane FLY-1
		c.type = 0xF;
		c.position = goal;
		c.source = source;
		if (routeCommand(c))
		{
			return true;
		}
	}
	m_attackMoveActive = true;
	m_attackMoveGoal = goal;
	// lane TARGET-1: the attack-move machine starts in its idle state (ZH AIAttackMoveToState::onEnter setState(AI_IDLE); RotWK's state 0x21 runs its sub
	// machine at + 0x54, RW 0x755798), whose entry pushes the mood check back (RW 0x7489B6 -> 0x662DD9); the earlier stand-in zeroed its own clock here
	resetNextMoodCheckTime();
	return true;
}

// the attack-move's scan: RotWK's AIAttackMoveToState (RW 0x755798) asks getNextMoodTarget(calledByAI unless its attack just ended, not during idle) while its
// sub machine is idle; the port's march (S-328) asks it with calledByAI (its cadence is the mood scan's)
Object *AIUpdateInterface::attackMoveTarget()
{
	return getNextMoodTarget(true, false);
}

bool AIUpdateInterface::resumeAttackMove()
{
	if (!m_attackMoveActive)
	{
		return false;
	}
	const Coord3D &p = *getObject()->getPosition();
	if (CombatQueries::centerDistanceSquared2D(p, m_attackMoveGoal) < 900.0f)
	{
		m_attackMoveActive = false; // arrived
		return false;
	}
	return aiMoveToPositionFromAttackMove(m_attackMoveGoal); // refused (gate): the march stays armed and the unit stays where it is
}
