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
#include "GameLogic/AI/AIWorld.h"
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

unsigned AIUpdateInterface::moodCheckInterval() const
{
	const Object &obj = *getObject();
	const CombatNames::Status &st = CombatNames::statuses();
	if (!obj.logic().combat().autoAcquireEnabled() || m_combat.autoAcquire == 0 || (m_combat.autoAcquire & 4u) || !obj.getWeapons() || obj.testStatus((unsigned)st.hordeMember))
	{
		return 0; // no AutoAcquire flag, "No", no weapon, or a horde member (its horde decides)
	}
	return m_combat.moodAttackCheckRate ? m_combat.moodAttackCheckRate : 3u;
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

// ZH getNextMoodTarget (the vision range scan): the nearest attackable enemy within the template's VisionRange
Object *AIUpdateInterface::nextMoodTarget()
{
	Object &obj = *getObject();
	if (moodCheckInterval() == 0)
	{
		return nullptr;
	}
	// lane IDLE-1 (community FB-0001), RW 0x66844A's gate before the scan (read with Ghidra, RW 0x6685B6 .. 0x6685F9): a contained object (+ 0x27C) looks for
	// a target only when its AI module's CanAttackWhileContained (data + 0x25) is set, it is CONTESTING_BUILDING (status 0x25) or its own contain (+ 0x258, the
	// ContainModuleInterface subobject) answers vslot 0xB8 true, and its container is not JUST_BUILT (+ 0x124 bit 26). vslot 0xB8 is RW 0x9188EB (false) in
	// OpenContain's and HordeContain's contain vtables (HordeContain: the subobject at + 0x20, vtable 0xC5B480; RW 0x871A90 is in the vtable at + 0x11C, another
	// interface) and RW 0x87EDA9 (CrewAllowedToFire) in SiegeEngineContain's. Before, a battering ram's crew (AutoAcquireEnemiesWhenIdle Yes, a sword, no
	// CanAttackWhileContained) acquired a target from its bones, its attack state walked it away from the ram it still belonged to and it stood with MOVING
	// where the ram's redeploy put it back: the QA matrix's IsengardRamCrew / MordorRamCrew treadmill. (ZH's Object::isAbleToAttack has the same container
	// rule for passengers, isPassengerAllowedToFire.)
	if (const Object *container = obj.getContainedBy())
	{
		static const int contesting = CombatNames::status("CONTESTING_BUILDING");
		static const int justBuilt = CombatNames::modelCondition("JUST_BUILT");
		const ContainModuleInterface *own = obj.getContain();
		const bool allowed = m_combat.canAttackWhileContained || (contesting >= 0 && obj.testStatus((unsigned)contesting)) || (own && own->moodScanWhileContained());
		if (!allowed || (justBuilt >= 0 && container->testModelCondition(justBuilt)))
		{
			return nullptr;
		}
	}
	// lane STEALTH-1 (RW 0x66844A, the idle branch RW 0x6685F7 .. 0x668676): a CAMOUFLAGE object scans unless its StancesBehavior's stance class is 3 (RW 0x861D8E); any other
	// object that is stealthed and undetected (RW 0x694C0D, no viewer) scans only with AutoAcquireEnemiesWhenIdle STEALTHED (flag 2); the container's slot 0xB0 exception
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
	// the range first (RW 0x668716 .. 0x668726, before the mood branches; review r1)
	float range = 0.0f;
	if (const FieldValue *v = obj.getTemplate()->getFinalOverride()->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			range = *f;
		}
	}
	if (range <= 0.0f)
	{
		return nullptr;
	}
	// lane SCRIPT-3 (RW 0x668748 .. 0x668787): a computer player's PASSIVE unit answers only its last attacker (the body's last damage source,
	// vslot 0x40 + 8, by id); attitude -3 (mood 0x2000) looks for nothing
	const unsigned mood = moodMatrixValue();
	if ((mood & 2u) && (mood & 0x200u))
	{
		const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule());
		return body ? obj.logic().findObjectByID(body->lastDamager()) : nullptr;
	}
	if (mood & 0x2000u)
	{
		return nullptr;
	}
	unsigned flags = 0;
	if (m_combat.autoAcquire & 16u)
	{
		flags |= TargetFinder::ALLOW_STRUCTURES;
	}
	flags |= obj.isKindOf((unsigned)CombatNames::kinds().horde) ? TargetFinder::HORDES_ONLY : TargetFinder::MEMBERS_ONLY;
	return obj.logic().combat().targets().findClosestEnemy(obj, range, flags, CMD_FROM_AI);
}

// ZH groupAttackMoveToPosition (stop S-328)
bool AIUpdateInterface::armAttackMove(const Coord3D &goal, CommandSourceType source)
{
	if (!acceptCommand(source, 0xF))
	{
		return false; // lane MODULES-3 r2: RW 0x667174 refuses the order before it changes anything
	}
	m_attackMoveActive = true;
	m_attackMoveGoal = goal;
	m_nextMoodCheck = 0;
	return true;
}

// the attack-move's scan: the nearest attackable enemy within the vision range, buildings included (the AutoAcquire flags are for idle units)
Object *AIUpdateInterface::attackMoveTarget()
{
	Object &obj = *getObject();
	if (!obj.getWeapons())
	{
		return nullptr;
	}
	float range = 0.0f;
	if (const FieldValue *v = obj.getTemplate()->getFinalOverride()->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			range = *f;
		}
	}
	if (range <= 0.0f)
	{
		return nullptr;
	}
	unsigned flags = TargetFinder::ALLOW_STRUCTURES;
	flags |= obj.isKindOf((unsigned)CombatNames::kinds().horde) ? TargetFinder::HORDES_ONLY : TargetFinder::MEMBERS_ONLY;
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember))
	{
		return nullptr; // a horde member marches with its horde
	}
	return obj.logic().combat().targets().findClosestEnemy(obj, range, flags, CMD_FROM_AI);
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
