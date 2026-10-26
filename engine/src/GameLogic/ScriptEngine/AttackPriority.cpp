// OpenBFME. GPL-3.0.
// See GameLogic/ScriptEngine/AttackPriority.h.

#include "GameLogic/ScriptEngine/AttackPriority.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

namespace
{
// RW 0x6A92EC (AICombatChain's constructor, through SkirmishAIData's RW 0x6A9807): TargetTypes -1, modifiers 0. The data TheSkirmishAIManager holds
// before (or without) a SkirmishAIData block: what RW 0x758B68 reads when the world has no SkirmishAIData store
const SkirmishAIData &constructedSkirmishData()
{
	static const SkirmishAIData data;
	return data;
}

// RW 0x6A94A6: the modifier of the first TargetTypes entry equal to the target's AIKindOf, else 0.0 (RW 0xC1B594)
float combatChainModifier(const AICombatChain &chain, int targetCategory)
{
	for (int i = 0; i < SkirmishAI::AI_KINDOF_COUNT; ++i)
	{
		if (chain.targetTypes[i] == targetCategory)
		{
			return chain.targetPriorityModifiers[i];
		}
	}
	return 0.0f;
}
} // namespace

float AttackPriorityInfo::getPriority(const Object &hunter, const Object &target, bool adjust) const
{
	int priority = m_defaultPriority; // + 8
	const ThingTemplate *t = target.getTemplate();
	if (!t)
	{
		return (float)priority; // RW 0x759800 -> 0x759854: fild, no adjustment
	}
	if (!m_priorities.empty())
	{
		const ThingTemplate *key = t->getFinalOverride(); // RW 0x465AF4
		const auto it = m_priorities.find(key ? key : t);  // RW 0x603AF6
		if (it != m_priorities.end())
		{
			priority = it->second;
		}
	}
	if (adjust)
	{
		return doModPriorityByCombatChain(SimMath::sseFromInt32(priority), hunter, target); // cvtsi2ss, RW 0x758B68
	}
	return (float)priority; // fild (exact)
}

float AttackPriorityInfo::doModPriorityByCombatChain(float priority, const Object &hunter, const Object &target) const
{
	// RW 0x758B68: the hunter's ThreatBreakdown AIKindOf (template + 0x530)
	const int chain = AIThreatFinder::threatCategory(*hunter.getTemplate());
	if (chain < 0 || chain >= SkirmishAI::AI_KINDOF_COUNT)
	{
		return priority;
	}
	GameLogic &logic = hunter.logic();
	const SkirmishAIStore *store = logic.skirmishAI().store();
	const SkirmishAIData &data = store ? store->data() : constructedSkirmishData();
	float m = combatChainModifier(data.combatChains[chain], AIThreatFinder::threatCategory(*target.getTemplate()));
	if (m == -1.0f) // RW 0xBD19DC, fucomip equal (ordered)
	{
		return 0.0f;
	}
	// RW 0x68B678 + 0x6A950B: the hunter's player's skirmish AI; its current enemy (RW 0x6C7807) controls the target -> + 100 (RW 0xBD88D8, addss)
	const Player *hunterPlayer = hunter.getControllingPlayer();
	if (const AISkirmishPlayer *ai = hunterPlayer ? logic.skirmishAI().findAI(hunterPlayer->getPlayerIndex()) : nullptr)
	{
		const int enemy = ai->groupFirst ? ai->groupFirst->brain.currentEnemy : ai->brain.currentEnemy;
		const Player *enemyPlayer = enemy >= 0 ? logic.players().getNthPlayer(enemy) : nullptr;
		if (enemyPlayer == target.getControllingPlayer())
		{
			m = SimMath::sseAdd(m, 100.0f);
		}
	}
	// x87: fld m; fdiv dword [AIData + 0x54]; fadd dword priority (the caller's fstp dword narrows)
	return NumericState::fstpDword(NumericState::pc24AddW(NumericState::pc24DivW((double)m, (double)logic.settings().attackPriorityDistanceModifier), (double)priority));
}

void AttackPriorityInfo::setPriority(const ThingTemplate *tmpl, int priority)
{
	if (!tmpl)
	{
		return;
	}
	const ThingTemplate *key = tmpl->getFinalOverride();
	m_priorities[key ? key : tmpl] = priority; // RW 0x759E24: the map made on demand, a later value overwrites
}

AttackPriorityStore::AttackPriorityStore(GameLogic &logic)
	: m_logic(logic)
{
	reset();
}

void AttackPriorityStore::reset()
{
	m_entries.clear();
	m_entries.emplace_back(); // RW 0x603269: + 0x19200, default 1, no map
	++m_generation;
}

const AttackPriorityInfo *AttackPriorityStore::getAttackInfo(const std::string &name)
{
	// RW 0x605938: the entries from index 1 (the default info is never found by name)
	for (size_t i = 1; i < m_entries.size(); ++i)
	{
		if (m_entries[i].m_name == name)
		{
			return &m_entries[i];
		}
	}
	for (const GameLogicSettings::AttackPriorityDef &def : m_logic.settings().attackPriorityDefs)
	{
		if (def.name != name)
		{
			continue;
		}
		if (m_entries.size() > 0xFF) // RW 0x6058B8: count <= 0xFF to append; the full table answers no info and the lookup falls to the error below
		{
			break;
		}
		m_entries.emplace_back();
		AttackPriorityInfo &info = m_entries.back();
		info.m_name = name;
		info.m_defaultPriority = def.defaultPriority;
		for (auto it = def.targets.rbegin(); it != def.targets.rend(); ++it) // RW keeps the targets front-inserted: back to front of the file
		{
			info.setPriority(m_logic.things().findTemplate(it->first), it->second); // RW 0x6D1305; none: skipped
		}
		return &info;
	}
	++m_undefinedLookups; // RW: "Attempting to use an undefined attack priority - '%s'" (a debug line only)
	return getDefaultAttackInfo();
}
