// OpenBFME. GPL-3.0.
//
// DOTManager (lane DECOMP-1). See DOTManager.h for the target facts.

#include "GameLogic/System/DOTManager.h"

#include "Common/StateHash.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <vector>

DOTManager::DOTManager(GameLogic &logic)
	: m_logic(logic)
{
}

const char *DOTManager::stopLine()
{
	return "[S-1959] damage over time (DECOMP-1): DOTNugget's records run in TheGameLogic + 0x174's manager (RW 0x911407 slot 14, add RW 0x821073, the stronger test RW 0x8208D4, "
		   "update RW 0x820EF0 in phase 1 after the weather); NOT ported: the drawable's poisoned look (RW 0x820A2B / 0x820A57 -> Drawable + 0x118 bit 2, client) and the "
		   "records' save / load (no saved games)";
}

// RW 0x8208D4 (stdcall current, incoming; x87 under PC24): fild of (end - now) as an unsigned value, times the record's amount (D + 0x20), each product kept in
// the register; `fcomip` of the incoming product against the current one, `jbe` to false: the incoming record wins only when strictly greater (unordered: no)
bool DOTManager::stronger(const Record &current, const Record &incoming, std::uint32_t now)
{
	const double in = SimMath::pc24MulW(SimMath::fildU32(incoming.endFrame - now), (double)incoming.info.m_input.m_amount);
	const double cur = SimMath::pc24MulW(SimMath::fildU32(current.endFrame - now), (double)current.info.m_input.m_amount);
	return in > cur;
}

// RW 0x821073
void DOTManager::add(ObjectID victim, const Record &record)
{
	auto it = m_entries.find(victim);
	if (it == m_entries.end())
	{
		m_entries.emplace(victim, record); // RW 0x821017 (make the entry) then RW 0x82092E (copy the record)
		++m_counters.added;
		return;
	}
	if (stronger(it->second, record, m_logic.getFrame()))
	{
		it->second = record; // RW 0x82092E; RW 0x820A2B (the drawable's poisoned look) is the client's: S-1959
		++m_counters.replaced;
	}
	else
	{
		++m_counters.kept;
	}
}

// RW 0x820EF0
void DOTManager::update()
{
	const std::uint32_t now = m_logic.getFrame();
	std::vector<ObjectID> drop; // RW: a local vector of keys, erased after the walk (RW 0x820EE0)
	for (auto &kv : m_entries)
	{
		Object *obj = m_logic.findObjectByID(kv.first);
		if (!obj)
		{
			drop.push_back(kv.first);
			continue;
		}
		Record &r = kv.second;
		if (r.endFrame == 0)
		{
			drop.push_back(kv.first); // + RW 0x820A57 (client)
		}
		else if (r.nextFrame != 0 && now >= r.nextFrame)
		{
			obj->attemptDamage(r.info); // RW 0x698E7D with the stored record (its output half is rewritten by the hit)
			r.nextFrame = r.interval + now;
			++m_counters.ticks;
		}
		if (obj->isEffectivelyDead()) // + 0x458 bit 0
		{
			drop.push_back(kv.first);
		}
		else if (r.endFrame != 0 && now >= r.endFrame)
		{
			drop.push_back(kv.first); // + RW 0x820A57 (client)
			++m_counters.expired;
		}
	}
	for (ObjectID id : drop)
	{
		m_entries.erase(id); // a key listed twice is erased once
	}
}

void DOTManager::reset()
{
	m_entries.clear();
	m_counters = Counters();
}

void DOTManager::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_entries.size());
	for (const auto &kv : m_entries)
	{
		const DamageInfoInput &in = kv.second.info.m_input;
		h.addU32(kv.first);
		h.addU32(in.m_sourceID);
		h.addU32(in.m_sourcePlayerMask);
		h.addI32(in.m_damageType);
		h.addI32(in.m_damageFXOverride);
		h.addI32(in.m_damageSubType);
		h.addI32(in.m_deathType);
		h.addFloat(in.m_amount);
		h.addBool(in.m_kill);
		h.addBool(in.m_shouldPlayUnderAttackEva); // lane DECOMP-1 r3: every stored input the ticks hand to attemptDamage
		h.addI32(in.m_fxTrigger);
		h.addFloat(in.m_delay);
		h.addU32(in.m_shockWaveSourceID);
		h.addFloat(in.m_shockWaveVector.x);
		h.addFloat(in.m_shockWaveVector.y);
		h.addFloat(in.m_shockWaveVector.z);
		h.addFloat(in.m_shockWaveAmount);
		h.addFloat(in.m_shockWaveRadius);
		h.addFloat(in.m_shockWaveTaperOff);
		h.addFloat(in.m_shockWaveZMult);
		h.addBool(in.m_shockWaveClearRadius);
		h.addFloat(in.m_shockWaveClearMult);
		h.addFloat(in.m_shockWaveClearFlingHeight);
		h.addFloat(in.m_shockWaveClearCenter.x);
		h.addFloat(in.m_shockWaveClearCenter.y);
		h.addFloat(in.m_shockWaveClearCenter.z);
		h.addFloat(in.m_cyclonicFactor);
		h.addU32(kv.second.interval);
		h.addU32(kv.second.endFrame);
		h.addU32(kv.second.nextFrame);
	}
}
