// OpenBFME. GPL-3.0.
//
// HordeContainCore: see HordeContainCore.h for the target facts and their addresses. Lane HORDE-1.

#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/SnapshotXfer.h"

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
	// the slots no rank owns (RW 0x877669: a record of rank 0 whose two coordinate copies are both the position, appended without a free-list entry):
	// RW 0x877A17 .. 0x877A34, with LeadersAllowed (data + 0x1F4), one at LeaderPosition (+ 0x200), its index kept at H + 0x268 (not kept here: nothing
	// ported reads it; the 2.01 data sets no LeadersAllowed); RW 0x8776BB, with BannerCarriersAllowed (+ 0x218), one per BannerCarrierPosition entry (+ 0x20C)
	auto append = [this](float x, float y) {
		Slot s;
		s.x = x;
		s.y = y;
		s.x2 = x;
		s.y2 = y;
		m_slots.push_back(s);
	};
	if (!m_data.m_leadersAllowed.empty())
	{
		append(m_data.m_leaderPosition.x, m_data.m_leaderPosition.y);
	}
	if (!m_data.m_bannerCarriersAllowed.empty())
	{
		for (const BannerCarrierPositionEntry &e : m_data.m_bannerCarrierPosition)
		{
			append(e.x, e.y);
		}
	}
	// RW 0x86C4EA: the formation is centred on the horde. The reference is the first slot of the first rank whose UnitType is a MACHINE (RW 0x6D1305,
	// ThingTemplate + 0x109 bit 3), else the mean of every slot, the leader's and the banner carriers' included; it is taken from both coordinate copies
	// (the sums in slot order, x + sum; then times 1 / n). Measured in RotWK (the Horde Spacing Lab of bfme2-map-studio, updateGoal RW 0x8E24D3 traced):
	// a GondorArcherHorde's members reserve their goals at X 17.5 / -2.5 / -22.5 from the horde's, its ranks being X 50 / 30 / 10 and its banner
	// carrier X 70 (the mean of 16 slots, 32.5); an ElvenMirkwoodArcherHorde's at 7.73 / 2.73 / -12.27 (ranks 0 / 20 / 15, banner 40: 11 slots, 12.27)
	int reference = -1;
	{
		int before = 0;
		for (const RankInfo &r : m_data.m_rankInfo)
		{
			if (m_isMachine && m_isMachine(r.unitType))
			{
				reference = before;
				break;
			}
			before += (int)r.positions.size();
		}
	}
	float cx = 0.0f, cy = 0.0f;
	int n = 0;
	for (size_t i = 0; i < m_slots.size(); ++i)
	{
		cx = SimMath::addf32(m_slots[i].x, cx);
		cy = SimMath::addf32(m_slots[i].y, cy);
		++n;
		if ((int)i == reference)
		{
			cx = m_slots[i].x;
			cy = m_slots[i].y;
			n = 1;
			break;
		}
	}
	if (n != 0)
	{
		const float inv = SimMath::divf32(1.0f, (float)n);
		cx = SimMath::mulf32(cx, inv);
		cy = SimMath::mulf32(cy, inv);
	}
	for (Slot &s : m_slots)
	{
		s.x = SimMath::subf32(s.x, cx);
		s.y = SimMath::subf32(s.y, cy);
		s.x2 = s.x;
		s.y2 = s.y;
	}
	m_built = true;
}

int HordeContainCore::bannerSlot(size_t entry) const
{
	if (m_data.m_bannerCarriersAllowed.empty() || entry >= m_data.m_bannerCarrierPosition.size())
	{
		return -1;
	}
	size_t index = m_data.m_leadersAllowed.empty() ? 0 : 1;
	for (const RankInfo &r : m_data.m_rankInfo)
	{
		index += r.positions.size();
	}
	index += entry;
	return index < m_slots.size() ? (int)index : -1;
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

bool HordeContainCore::hasFreeSlotFor(const std::string &templateName) const
{
	for (int idx : m_freeList)
	{
		const RankInfo *rank = findRank(m_slots[(size_t)idx].rank);
		if (rank && m_matcher(rank->unitType, templateName))
		{
			return true;
		}
	}
	return false;
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
		const float c = owner.hasTrig ? owner.cosAngle : SimMath::cosf32(owner.angle);
		const float s = owner.hasTrig ? owner.sinAngle : SimMath::sinf32(owner.angle);
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
	const float c = owner.hasTrig ? owner.cosAngle : SimMath::cosf32(owner.angle);
	const float sn = owner.hasTrig ? owner.sinAngle : SimMath::sinf32(owner.angle);
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
				  "the centring (RW 0x86C4EA) writes both alike, the writer that makes them differ is unknown, and NarrowPassageScale (RotWK offset scaling) is not applied");
	return out;
}

// ---- lane SAVE-4b: the snapshot (see HordeContainCore.h) -----------------------------------------------------------------------------------------------
void HordeContainCore::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	// a load reads into temporaries, validates them, then replaces the core's state (a refused stream leaves the core as it was)
	const bool loading = x.isLoading();
	std::vector<Slot> slots = loading ? std::vector<Slot>() : m_slots;
	std::map<ObjectId, int> memberIndex = loading ? std::map<ObjectId, int>() : m_memberIndex;
	std::list<int> freeList = loading ? std::list<int>() : m_freeList;
	std::vector<Member> members = loading ? std::vector<Member>() : m_members;
	std::set<ObjectId> registered = loading ? std::set<ObjectId>() : m_registered;
	ObjectId bannerCarrier = loading ? 0 : m_bannerCarrierId, otherNonSlot = loading ? 0 : m_otherNonSlotId;
	bool dirty = !loading && m_dirty, flag174 = !loading && m_flag174, built = !loading && m_built;
	SnapshotXfer::each(x, slots, 28, [&](Slot &s) {
		x.xferInt(&s.rank);
		x.xferReal(&s.x);
		x.xferReal(&s.y);
		x.xferReal(&s.x2);
		x.xferReal(&s.y2);
		x.xferReal(&s.angle);
		x.xferInt(&s.leaderSlot);
	});
	SnapshotXfer::map(x, memberIndex, 8, "a horde's member slots", [&](ObjectId &id) { x.xferObjectID(&id); }, [&](int &slot) { x.xferInt(&slot); });
	SnapshotXfer::each(x, freeList, 4, [&](int &f) { x.xferInt(&f); });
	// the indices are slots of this horde's slot list (they index it unchecked later): a leader slot, a member's slot (-1: none), a free slot
	if (loading)
	{
		const std::int64_t last = (std::int64_t)slots.size() - 1;
		for (const Slot &s : slots)
		{
			SnapshotXfer::checkRange(s.leaderSlot, -1, last, "a horde slot's leader slot");
		}
		for (const auto &kv : memberIndex)
		{
			SnapshotXfer::checkRange(kv.second, -1, last, "a horde member's slot");
		}
		for (int f : freeList) // a slot may be listed again (releaseMember appends the member's slot, RW's list does the same)
		{
			SnapshotXfer::checkRange(f, 0, last, "a horde's free slot");
		}
	}
	SnapshotXfer::each(x, members, 5, [&](Member &m) {
		x.xferObjectID(&m.id);
		x.xferAsciiString(&m.templateName);
	});
	SnapshotXfer::ids(x, registered);
	x.xferObjectID(&bannerCarrier);
	x.xferObjectID(&otherNonSlot);
	x.xferBool(&dirty);
	x.xferBool(&flag174);
	x.xferBool(&built);
	if (loading)
	{
		m_slots.swap(slots);
		m_memberIndex.swap(memberIndex);
		m_freeList.swap(freeList);
		m_members.swap(members);
		m_registered.swap(registered);
		m_bannerCarrierId = bannerCarrier;
		m_otherNonSlotId = otherNonSlot;
		m_dirty = dirty;
		m_flag174 = flag174;
		m_built = built;
	}
}
