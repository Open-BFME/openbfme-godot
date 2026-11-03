// OpenBFME. GPL-3.0.
// See GameLogic/ScriptEngine/AttackPriority.h.

#include "GameLogic/ScriptEngine/AttackPriority.h"

#include <algorithm>

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
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
	m_edited = false;
}

AttackPriorityInfo *AttackPriorityStore::findOrCreate(const std::string &name)
{
	// RW 0x6058B8: entries 1 .. by name (StringBase compare: exact)
	for (size_t i = 1; i < m_entries.size(); ++i)
	{
		if (m_entries[i].m_name == name)
		{
			m_edited = true; // the caller edits it
			return &m_entries[i];
		}
	}
	if (m_entries.size() > 0xFF) // RW 0x6058F4: 0xFF < count -> null
	{
		return nullptr;
	}
	m_entries.emplace_back();
	m_entries.back().m_name = name; // RW 0x605906: the slot's name; its default and table as constructed
	m_edited = true;
	return &m_entries.back();
}

std::vector<AttackPriorityStore::SavedEntry> AttackPriorityStore::saved() const
{
	std::vector<SavedEntry> out;
	for (size_t i = 1; i < m_entries.size(); ++i)
	{
		SavedEntry e;
		e.name = m_entries[i].m_name;
		e.defaultPriority = m_entries[i].m_defaultPriority;
		for (const auto &kv : m_entries[i].m_priorities)
		{
			e.priorities[kv.first->getName()] = kv.second; // the key is a final override: its name finds it again
		}
		out.push_back(std::move(e));
	}
	// by name (review m3): an unedited table's AIData entries are made in the order the AIs first use them, which a loaded game may not repeat; the
	// lookups go by name, so the order is no state (only the 0xFF limit counts the entries)
	std::sort(out.begin(), out.end(), [](const SavedEntry &a, const SavedEntry &b) { return a.name < b.name; });
	return out;
}

bool AttackPriorityStore::restore(const std::vector<SavedEntry> &entries, std::string *why)
{
	std::deque<AttackPriorityInfo> made;
	made.emplace_back();
	for (const SavedEntry &e : entries)
	{
		made.emplace_back();
		AttackPriorityInfo &info = made.back();
		info.m_name = e.name;
		info.m_defaultPriority = e.defaultPriority;
		for (const auto &kv : e.priorities)
		{
			const ThingTemplate *t = m_logic.things().findTemplate(kv.first);
			if (!t)
			{
				if (why)
				{
					*why = "the attack priority set '" + e.name + "' names the thing '" + kv.first + "', which this game lacks";
				}
				return false;
			}
			info.setPriority(t, kv.second); // keyed by its final override, as RW 0x759E24
		}
	}
	m_entries = std::move(made);
	m_edited = true;
	++m_generation;
	return true;
}

void AttackPriorityStore::crc(StateHasher &h) const
{
	if (!m_edited)
	{
		return;
	}
	h.addU32(0x19200u);
	for (const SavedEntry &e : saved())
	{
		h.addString(e.name);
		h.addI32(e.defaultPriority);
		h.addU32((std::uint32_t)e.priorities.size());
		for (const auto &kv : e.priorities)
		{
			h.addString(kv.first);
			h.addI32(kv.second);
		}
	}
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
