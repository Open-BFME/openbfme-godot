// OpenBFME. GPL-3.0.
// See Common/CommandPoints.h for the target facts and the addresses.

#include "Common/CommandPoints.h"

#include "GameLogic/ObjectFilterMatch.h"

void CommandPoints::reset()
{
	// RW 0x6A8183
	m_usage = 0;
	m_bonus = 0;
	m_base = 0x64;
	m_territoryGood = 0;
	m_territoryEvil = 0;
	m_records.clear();
	m_scriptSet = false;
	m_initialized = false;
}

void CommandPoints::init(int playerIndex, const CommandPointsSource &source)
{
	m_initialized = true;
	// RW 0x6A7C86: the index must be 0..19 (the caller checks); +0x14 = the index
	m_playerIndex = playerIndex;
	const int base = CpMath::add(CpMath::add(source.start, source.territoryGoodTerm), source.territoryEvilTerm); // RW 0x6A7ECA: edx + counters' products
	// RW 0x6A7DC7 .. 0x6A7DE2: the cap and the base are stored when no script set them or when they are larger than what a script left
	if (!m_scriptSet || source.cap > m_cap)
	{
		m_cap = source.cap;
	}
	if (!m_scriptSet || base > m_base)
	{
		m_base = base;
	}
	// RW 0x6A7DE2 .. 0x6A7E07: TheGameInfo exists and GameLogic + 0x114 == 3: cap = cap * TheGameInfo[+0x6C] / 100 (32-bit imul, idiv by 100)
	if (source.applyLobbyPercent)
	{
		const std::int32_t product = CpMath::mul(source.lobbyPercent, m_cap); // imul: 32-bit, wraps
		m_cap = product / 100;                                                                                   // cdq; idiv 100: truncates toward zero
	}
}

void CommandPoints::setFromScript(int base, int cap)
{
	// RW 0x6A7ACD
	m_base = base;
	m_cap = cap;
	m_scriptSet = true;
}

int CommandPoints::getLimit(const std::function<bool(const ObjectFilter &)> &recordFilterSatisfied) const
{
	// RW 0x6A7B9F
	int total = CpMath::add(m_bonus, m_base);
	for (const Record &r : m_records)
	{
		if (!ObjectFilterMatch::isValid(r.filter.get()) || recordFilterSatisfied(*r.filter))
		{
			total = CpMath::add(total, r.value);
		}
	}
	return total > m_cap ? m_cap : total; // cmovg
}

bool CommandPoints::canAfford(int templateCommandPoints, bool armyOfDead, int limit) const
{
	// RW 0x6A7F79
	if (templateCommandPoints == 0)
	{
		return true;
	}
	const bool ok = CpMath::add(m_usage, templateCommandPoints) <= limit; // signed compare of the wrapped sum
	return armyOfDead ? true : ok;
}

void CommandPoints::addRecord(int value, std::uint32_t objectId, std::shared_ptr<const ObjectFilter> filter)
{
	Record r;
	r.value = value;
	r.objectId = objectId;
	r.filter = std::move(filter);
	m_records.push_back(std::move(r)); // RW 0x6A81AF push_back
}

bool CommandPoints::removeRecord(int value, std::uint32_t objectId)
{
	// RW 0x6A8033: the first record whose object id (+4) and value (+0) match
	for (size_t i = 0; i < m_records.size(); ++i)
	{
		if (m_records[i].objectId == objectId && m_records[i].value == value)
		{
			m_records.erase(m_records.begin() + (std::ptrdiff_t)i);
			return true;
		}
	}
	return false;
}

void CommandPoints::crc(StateHasher &h) const
{
	h.addI32(m_base);
	h.addI32(m_usage);
	h.addI32(m_bonus);
	h.addI32(m_cap);
	h.addI32(m_playerIndex);
	h.addI32(m_territoryGood);
	h.addI32(m_territoryEvil);
	h.addBool(m_scriptSet);
	h.addBool(m_initialized);
	h.addU32((std::uint32_t)m_records.size());
	for (const Record &r : m_records)
	{
		h.addI32(r.value);
		h.addU32(r.objectId);
		ObjectFilterMatch::crc(h, r.filter.get());
	}
}
