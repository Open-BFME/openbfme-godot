// OpenBFME. GPL-3.0.
//
// HordeContainCore: see HordeContainCore.h for the target facts and their addresses. Lane HORDE-1.

#include "GameLogic/Object/Contain/HordeContainCore.h"

#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace
{
// The retail call sites pass __FILE__ and the line; the logic RNG records only call order and bounds here.
const char *kHordeContainFile = "HordeContain.cpp";
const int kLineRandomOffsetX = 1575; // RW 0x877864 push 0x627
const int kLineRandomOffsetY = 1579; // RW 0x8778A2 push 0x62B
const int kLineRandomFreeSlot = 1191; // RW 0x86E0E7 push 0x4A7
}

HordeContainCore::HordeContainCore(const HordeContainModuleData &data, GameLogicRandom &rng, UnitTypeMatcher matcher)
	: m_data(data)
	, m_rng(rng)
	, m_matcher(std::move(matcher))
{
	if (!m_matcher)
	{
		throw std::logic_error("HordeContainCore: a UnitType matcher is required (no default matching rule, S-082)");
	}
}

const RankInfo *HordeContainCore::findRank(int rank) const
{
	// RW 0x86C3E7: the first RankInfo with that number
	for (const RankInfo &r : m_data.m_rankInfo)
	{
		if (r.rankNumber == rank)
		{
			return &r;
		}
	}
	return nullptr;
}

void HordeContainCore::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_slots.size());
	for (const Slot &s : m_slots)
	{
		h.addI32(s.rank);
		h.addFloat(s.x);
		h.addFloat(s.y);
		h.addFloat(s.x2);
		h.addFloat(s.y2);
		h.addFloat(s.angle);
		h.addI32(s.leaderSlot);
	}
	h.addU32((std::uint32_t)m_memberIndex.size()); // ordered by id
	for (const auto &kv : m_memberIndex)
	{
		h.addU32(kv.first);
		h.addI32(kv.second);
	}
	h.addU32((std::uint32_t)m_freeList.size()); // list order is state
	for (int f : m_freeList)
	{
		h.addI32(f);
	}
	h.addU32((std::uint32_t)m_members.size());
	for (const Member &m : m_members)
	{
		h.addU32(m.id);
		h.addString(m.templateName);
	}
	h.addU32((std::uint32_t)m_registered.size());
	for (ObjectId id : m_registered)
	{
		h.addU32(id);
	}
	h.addU32(m_bannerCarrierId);
	h.addU32(m_otherNonSlotId);
	h.addBool(m_dirty);
	h.addBool(m_flag174);
	h.addBool(m_built);
}

void HordeContainCore::setMemberSlot(ObjectId id, int slot)
{
	m_memberIndex[id] = slot;
}

bool HordeContainCore::slotAccepts(int slot, const std::string &memberTemplateName) const
{
	if (slot < 0 || (size_t)slot >= m_slots.size())
	{
		return false;
	}
	const RankInfo *rank = findRank(m_slots[(size_t)slot].rank);
	if (!rank)
	{
		return true;
	}
	return m_matcher(rank->unitType, memberTemplateName);
}

int HordeContainCore::slotOf(ObjectId id) const
{
	const auto it = m_memberIndex.find(id);
	return it == m_memberIndex.end() ? -1 : it->second;
}

bool HordeContainCore::isSpecial(ObjectId id, bool templateFlag) const
{
	// RW 0x86C4BE
	return id == m_otherNonSlotId || id == m_bannerCarrierId || templateFlag;
}

// RW 0x877751
void HordeContainCore::buildSlots(bool fromScratch)
{
	m_slots.clear();
	const float jitterX = m_data.m_randomOffset.x; // [ebp-0x30]
	const float jitterY = m_data.m_randomOffset.y; // [ebp-0x2c]
	std::map<int, int> firstSlotOfRank;            // [ebp-0x44]
	size_t total = 0;
	for (const RankInfo &r : m_data.m_rankInfo)
	{
		total += r.positions.size();
	}
	m_slots.reserve(total);

	for (const RankInfo &r : m_data.m_rankInfo)
	{
		firstSlotOfRank[r.rankNumber] = (int)m_slots.size();
		for (const HordeRankPosition &p : r.positions)
		{
			Slot s;
			s.rank = r.rankNumber;
			float x = p.x;
			float y = p.y;
			if (jitterX > 0.0f)
			{
				const int rx = m_rng.getValue(SimMath::truncToInt32(SimMath::subf32(0.0f, jitterX)), (int)jitterX, kHordeContainFile, kLineRandomOffsetX);
				x = SimMath::addf32((float)rx, x);
			}
			if (jitterY > 0.0f)
			{
				const int ry = m_rng.getValue(SimMath::truncToInt32(SimMath::subf32(0.0f, jitterY)), (int)jitterY, kHordeContainFile, kLineRandomOffsetY);
				y = SimMath::addf32((float)ry, y);
			}
			s.x = x;
			s.y = y;
			s.x2 = x;
			s.y2 = y;
			s.angle = 0.0f;
			if (p.leaderRank != -1)
			{
				// RW 0x8778E2: map[leaderRank] (inserted as 0 when the rank was never built) + leaderIndex
				s.leaderSlot = firstSlotOfRank[p.leaderRank] + p.leaderIndex;
			}
			else
			{
				s.leaderSlot = -1;
			}
			m_slots.push_back(s);

			// free list (RW 0x877914-0x87799E)
			const bool noMembers = m_members.empty() && !m_flag174;
			const bool collect = fromScratch || noMembers;
			if (collect)
			{
				const int last = (int)m_slots.size() - 1;
				bool occupied = false;
				for (const auto &kv : m_memberIndex)
				{
					if (kv.second == last)
					{
						occupied = true;
						break;
					}
				}
				if (!occupied)
				{
					m_freeList.push_back(last);
				}
			}
		}
	}
	m_built = true;
}

// RW 0x873F30
int HordeContainCore::addMember(ObjectId id, const std::string &templateName)
{
	m_members.push_back(Member{ id, templateName });
	for (auto it = m_freeList.begin(); it != m_freeList.end(); ++it)
	{
		const int idx = *it;
		const RankInfo *rank = findRank(m_slots[(size_t)idx].rank);
		if (!rank)
		{
			continue;
		}
		if (m_matcher(rank->unitType, templateName))
		{
			m_memberIndex[id] = idx;
			m_freeList.erase(it);
			m_registered.insert(id);
			return idx;
		}
	}
	return -1;
}

// RW 0x87370B
void HordeContainCore::removeMember(ObjectId id, bool templateIsSpecial)
{
	if (!isSpecial(id, templateIsSpecial))
	{
		// RW 0x873749: memberIndex[id] (operator[]: an absent member reads 0) goes back on the free list
		m_freeList.push_back(m_memberIndex[id]);
	}
	m_memberIndex.erase(id);
	m_registered.erase(id);
	for (size_t i = 0; i < m_members.size(); ++i)
	{
		if (m_members[i].id == id)
		{
			m_members.erase(m_members.begin() + (long)i);
			break;
		}
	}
	fillInAll();
}

void HordeContainCore::releaseMember(ObjectId id)
{
	for (size_t i = 0; i < m_members.size(); ++i)
	{
		if (m_members[i].id == id)
		{
			m_members.erase(m_members.begin() + (long)i);
			break;
		}
	}
}

bool HordeContainCore::rejoinMember(ObjectId id, const std::string &templateName)
{
	if (m_memberIndex.find(id) == m_memberIndex.end())
	{
		return false;
	}
	for (const Member &m : m_members)
	{
		if (m.id == id)
		{
			return true;
		}
	}
	m_members.push_back(Member{ id, templateName });
	return true;
}

int HordeContainCore::fillInAll()
{
	int steps = 0;
	while (fillInLowestFreeSlot())
	{
		++steps;
	}
	return steps;
}

// RW 0x873D48
bool HordeContainCore::fillInLowestFreeSlot()
{
	if (m_freeList.empty())
	{
		return false;
	}
	const int slotCount = (int)m_slots.size();
	int lowest = slotCount;
	std::list<int>::iterator lowestNode = m_freeList.end();
	for (auto it = m_freeList.begin(); it != m_freeList.end(); ++it)
	{
		if (*it < lowest)
		{
			lowest = *it;
			lowestNode = it;
		}
	}
	if ((unsigned)lowest >= (unsigned)slotCount)
	{
		return false;
	}
	const int f = lowest;
	const int rf = m_slots[(size_t)f].rank;
	int stop = 99;
	for (int v : m_data.m_ranksThatStopAdvance)
	{
		if (v >= rf)
		{
			stop = v;
			break;
		}
	}
	float best = 99999.0f; // RW 0xBDF350
	const Member *winner = nullptr;
	for (const Member &m : m_members)
	{
		const int i = m_memberIndex[m.id]; // operator[]: inserts 0 for an unregistered member
		if (i > f && (unsigned)i < (unsigned)slotCount)
		{
			const Slot &cand = m_slots[(size_t)i];
			if (cand.rank <= stop && cand.rank > rf)
			{
				const Slot &target = m_slots[(size_t)f];
				const float dx = SimMath::subf32(target.x, cand.x);
				const float dy = SimMath::subf32(target.y, cand.y);
				const float sx = SimMath::mulf32(dx, dx);
				float d2 = SimMath::mulf32(dy, dy);
				d2 = SimMath::addf32(d2, sx);
				if (best > d2)
				{
					winner = &m;
					stop = cand.rank; // RW 0x873E88: the bound follows the chosen slot's rank
					best = d2;
				}
			}
		}
	}
	if (!winner)
	{
		return false;
	}
	const ObjectId id = winner->id;
	const int oldIndex = m_memberIndex[id];
	m_memberIndex.erase(id);
	m_memberIndex[id] = f;
	if (m_slotChanged)
	{
		m_slotChanged(id, f);
	}
	m_freeList.erase(lowestNode);
	m_freeList.push_back(oldIndex);
	m_dirty = true;
	return true;
}

// RW 0x86BFB5 + 0x875847
Coord3D HordeContainCore::getSlotWorldPos(ObjectId member, const Placement &owner, float *outAngle)
{
	const int idx = m_memberIndex[member]; // operator[]: an absent member reads (and gets) 0
	Coord3D result = owner.position;
	if (idx >= 0 && (unsigned)idx < (unsigned)m_slots.size())
	{
		float ox, oy;
		if (member == m_bannerCarrierId && m_members.size() == 1)
		{
			ox = 0.0f;
			oy = 0.0f;
		}
		else
		{
			ox = m_slots[(size_t)idx].x;
			oy = m_slots[(size_t)idx].y;
		}
		const float c = SimMath::cosf32(owner.angle);
		const float s = SimMath::sinf32(owner.angle);
		const float rx = SimMath::subf32(SimMath::mulf32(ox, c), SimMath::mulf32(oy, s));
		const float ry = SimMath::addf32(SimMath::mulf32(ox, s), SimMath::mulf32(oy, c));
		if (outAngle)
		{
			*outAngle = m_slots[(size_t)idx].angle;
		}
		result.x = SimMath::addf32(owner.position.x, rx);
		result.y = SimMath::addf32(ry, owner.position.y);
	}
	return result;
}

// RW 0x86D720: the by-index form, using the second coordinate copy.
Coord3D HordeContainCore::getSlotWorldPosByIndex(int slot, const Placement &owner) const
{
	const Slot &s = m_slots.at((size_t)slot);
	const float c = SimMath::cosf32(owner.angle);
	const float sn = SimMath::sinf32(owner.angle);
	const float rx = SimMath::subf32(SimMath::mulf32(s.x2, c), SimMath::mulf32(s.y2, sn));
	const float ry = SimMath::addf32(SimMath::mulf32(s.x2, sn), SimMath::mulf32(s.y2, c));
	return Coord3D{ SimMath::addf32(owner.position.x, rx), SimMath::addf32(ry, owner.position.y), owner.position.z };
}

// RW 0x86E0CB
std::string HordeContainCore::chooseRandomFreeSlotUnitType()
{
	if (m_freeList.empty())
	{
		return std::string();
	}
	int r = m_rng.getValue(0, (int)m_freeList.size() - 1, kHordeContainFile, kLineRandomFreeSlot);
	auto it = m_freeList.begin();
	while (r > 0)
	{
		++it;
		--r;
	}
	const RankInfo *rank = findRank(m_slots[(size_t)*it].rank);
	return rank ? rank->unitType : std::string();
}

std::vector<std::string> HordeContainCore::unverified() const
{
	std::vector<std::string> out;
	bool leaders = false;
	for (const Slot &s : m_slots)
	{
		leaders = leaders || s.leaderSlot != -1;
	}
	if (leaders)
	{
		out.push_back("S-082: slot leader links (record +0x18) are built but no reader was found in the RotWK binary; the formation does not use them");
	}
	out.push_back("S-082: slot records keep two coordinate copies (+0x4 / +0x8 and +0xC / +0x10); the member form of getSlotWorldPos reads the first, the by-index form the second; "
				  "the writer that makes them differ is unknown, and NarrowPassageScale (RotWK offset scaling) is not applied");
	return out;
}
