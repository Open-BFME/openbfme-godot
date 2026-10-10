// OpenBFME. GPL-3.0.
//
// HordeContainCore: the slot table, the member <-> slot maps, the free list, the slot world positions and
// the rank fill-in of HordeContain, as a standalone deterministic component. Lane HORDE-1 (spec
// horde-and-movement.md 1.3, 2.1, 2.6, checklist step 6). The runtime Object integration (member objects, the
// per-frame member pass, melee, banner carrier spawning) wraps this class in a later lane.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat). H is a HordeContain instance, `D` its HordeContainModuleData.
//   * buildSlots, RW 0x877751. The slot vector (H+0x188, 0x1C bytes per slot) is filled rank by rank in INI
//     order, position by position. Each record is {rank, x, y, x2, y2, angle = 0.0, leaderSlot}. When
//     RandomOffset.x > 0 the x gets GetGameLogicRandomValue((int)-ox, (int)ox, HordeContain.cpp, 1575) added,
//     then, when RandomOffset.y > 0, the y gets one with line 1579 (the retail logic RNG, call order slot by
//     slot, x before y; the bounds are truncated to int). leaderSlot = firstSlotOfRank(leaderRank) +
//     leaderIndex, else -1. The slot index of a position is the number of slots built before it.
//     The free list (H+0x194, a std::list<int>) receives the new slot's index (i.e. size - 1) when the
//     member map holds no member on it and either `fromScratch` is non-zero or H has no members and
//     H+0x174 is 0 (RW 0x87791A-0x877999). After the loop the melee strategy is told the slot count
//     (vslot 1) and the data's banner carrier / leader records are created (not ported here).
//   * rank fill-in, RW 0x873D48 (one step; callers loop until it returns false):
//       empty free list -> false. f = the lowest free index (strict <, first of equals), rf = slot[f].rank;
//       stop = the first value of RanksThatStopAdvance (list order) that is >= rf, else 99. Over the member
//       list in order: i = memberIndex[id] (std::map operator[]: an absent member reads, and INSERTS, 0);
//       a candidate has i > f, slot[i].rank <= stop and slot[i].rank > rf; its score is the squared distance
//       between slot[i] and slot[f] (record x, y at +4 / +8; dy*dy + dx*dx); a strictly smaller score than the
//       best so far (start 99999.0) wins AND `stop` becomes that slot's rank, so later candidates must have a
//       rank <= the chosen one (a retail quirk the spec does not list). No winner -> false. Otherwise
//       memberIndex[winner] = f, the melee strategy hears about it (vslot 12), f leaves the free list, the
//       winner's old index is appended, the dirty flag (H+0x120) is set, result true.
//   * member add by UnitType, RW 0x873F30: the first free-list entry (list order) whose rank's UnitType matches
//     the new member's template (RW 0x73D5C2, template equivalence; a matcher is injected here) takes the
//     member: memberIndex[id] = index, entry erased, id added to the registered set (H+0x170).
//   * member removal, RW 0x87370B: unless the member is special (RW 0x86C4BE: its id is H+0x264 or H+0x26C, or
//     its ThingTemplate+0x109 has bit 3), its slot index is appended to the free list; the map entry and the
//     registered-set entry go; then `while (fillIn())`. (The EVA last-member and model condition parts are not
//     ported.) The same release sequence (0x87393E) differs only in not reading the template flag.
//   * slot world position, RW 0x875847 (the member form; RW 0x86D720 is the by-index form using the
//     +0xC / +0x10 copy): index = memberIndex[id] (absent reads 0); outside 0..count-1 the result is the owner's
//     position; the banner carrier (id == H+0x26C) alone in the contain list (count == 1) uses the offset (0, 0);
//     else the offset is the record's (x, y) at +4 / +8. RotWK ROTATES the offset by sin / cos of the owner's
//     angle (RW 0x86BFB5: x' = ox*cos - oy*sin, y' = ox*sin + oy*cos, SSE float, sin and cos from `fsincos`)
//     and adds the owner's position. This differs from the spec's BFME1-derived text (atan2 of the offset,
//     its length, angle + orientation): no atan2 and no length are involved. outAngle is the record's +0x14
//     (0.0 unless written). NarrowPassageScale is not applied (spec gap 4.9, S-082).
//   * random free slot unit type, RW 0x86E0CB: r = GetGameLogicRandomValue(0, freeCount - 1, HordeContain.cpp,
//     1191); the r-th free-list entry's rank's UnitType (used when the horde replenishes).
//
// DONOR: Open-BFME-1 `Object/Contain/HordeContain/Rva00245010RecordBuild.cpp:137-188` (build),
// `MemberIndex00240F10.cpp:43-72` (add by UnitType), `Rva002458B0FormationPosition.cpp` (position, polar form).

#pragma once

#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameLogic/Module/HordeContain.h"

#include <functional>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

class HordeContainCore
{
public:
	typedef std::uint32_t ObjectId;

	// RW 0x1C-byte slot record.
	struct Slot
	{
		int rank = 0;
		float x = 0.0f, y = 0.0f;   // +4, +8
		float x2 = 0.0f, y2 = 0.0f; // +0xC, +0x10 (equal to x, y at build; the writer that changes them is unknown)
		float angle = 0.0f;         // +0x14
		int leaderSlot = -1;        // +0x18
	};

	struct Member
	{
		ObjectId id;
		std::string templateName;
	};

	// Where the horde object is (RW H+8 -> obj+0x38 position, obj+0x44 angle).
	struct Placement
	{
		Coord3D position{ 0.0f, 0.0f, 0.0f };
		float angle = 0.0f;
	};

	// RW 0x73D5C2: does a member of this template belong in a slot of this UnitType?
	typedef std::function<bool(const std::string &slotUnitType, const std::string &memberTemplateName)> UnitTypeMatcher;
	// RW 0x873EEE: the melee strategy's per-member notification (vslot 12).
	typedef std::function<void(ObjectId member, int newSlot)> MemberSlotChanged;

	// `data` and `rng` must outlive the core. A null matcher is a logic error (no default matching rule).
	HordeContainCore(const HordeContainModuleData &data, GameLogicRandom &rng, UnitTypeMatcher matcher);

	void setMemberSlotChanged(MemberSlotChanged cb) { m_slotChanged = std::move(cb); }
	void setBannerCarrierId(ObjectId id) { m_bannerCarrierId = id; }   // H+0x26C
	void setOtherNonSlotId(ObjectId id) { m_otherNonSlotId = id; }     // H+0x264 (identity unproven)

	// RW 0x877751. Rebuilds the slot table (retail does this when the contain is created and when the formation
	// is swapped); existing member indices are kept.
	void buildSlots(bool fromScratch);

	// RW 0x873F30: returns the slot index taken, or -1 when no free slot of a matching rank exists (the member
	// is then in the contain list without a slot).
	int addMember(ObjectId id, const std::string &templateName);
	// RW 0x87370B / 0x87393E. `templateIsSpecial` is ThingTemplate+0x109 bit 3.
	void removeMember(ObjectId id, bool templateIsSpecial);
	// lane GARRISON-3: a member released through HordeContainInterface slot 0xA8 (RW 0x86EBF4 -> OpenContain's removal RW 0x8665BE / 0x865EB6 ->
	// HordeContain::onRemoving RW 0x86CF2A) leaves the contain list only: its memberIndex entry (H+0x17C) stays, no slot is freed, no fill-in runs.
	void releaseMember(ObjectId id);
	// lane GARRISON-3: HordeContain::onContaining (RW 0x872D0A) assigns and places only a member memberIndex does not hold (RW 0x872D87: the map's find
	// against its end); a member that kept its slot rejoins the contain list as it is. True when the member held a slot (and rejoined), false otherwise.
	bool rejoinMember(ObjectId id, const std::string &templateName);
	// RW 0x873D48: one fill-in step.
	bool fillInLowestFreeSlot();
	// the `while (fillIn());` loop; returns the number of steps taken
	int fillInAll();

	// RW 0x875847 and 0x86D720.
	Coord3D getSlotWorldPos(ObjectId member, const Placement &owner, float *outAngle);
	Coord3D getSlotWorldPosByIndex(int slot, const Placement &owner) const;

	// RW 0x86E0CB: the UnitType of a randomly chosen free slot, "" when there is none.
	std::string chooseRandomFreeSlotUnitType();

	// ---- MOVE-1: the reform's reassignment (RW 0x877E12, the HordeContain vtable +0x40 "end reform") ----
	// the member's slot becomes `slot` (RW 0x7871fc: the member-index map's operator[] assignment); the free list is not touched
	void setMemberSlot(ObjectId id, int slot);
	// RW 0x877F7F-0x877FB7: a slot whose rank is unknown to the data accepts anything; else the rank's UnitType must match the member's template (RW 0x73D5C2)
	bool slotAccepts(int slot, const std::string &memberTemplateName) const;

	const std::vector<Slot> &slots() const { return m_slots; }
	const std::list<int> &freeList() const { return m_freeList; }
	const std::vector<Member> &members() const { return m_members; }
	// The index memberIndex holds for an id, or -1 when the map has no entry (no insertion).
	int slotOf(ObjectId id) const;
	// RW 0x86DEAF (lane MOVE-3): the member's slot index from the member map (H + 0x17C), 0 when the member is not in it
	int slotIndexOrZero(ObjectId id) const { const int s = slotOf(id); return s < 0 ? 0 : s; }
	bool dirty() const { return m_dirty; }
	void clearDirty() { m_dirty = false; }
	const std::set<ObjectId> &registeredMembers() const { return m_registered; }
	// RW 0x877918: the contain list is empty and H+0x174 is 0 (the second input of the free-list rule).
	void setReformationFlag174(bool v) { m_flag174 = v; }

	// The complete mutable state in explicit order, size prefixed (lane LOGIC-1, deterministic state hash): every slot record field, the member <-> slot
	// map, the free list in list order, the member list, the registered set, the special ids, and the dirty / reformation / built flags. The module data
	// and the matcher are configuration and are not hashed.
	void crc(StateHasher &hasher) const;

	// Acceptance stop report (S-082): the unread leader links and the second coordinate copy.
	std::vector<std::string> unverified() const;

private:
	const RankInfo *findRank(int rank) const;
	bool isSpecial(ObjectId id, bool templateFlag) const;

	const HordeContainModuleData &m_data;
	GameLogicRandom &m_rng;
	UnitTypeMatcher m_matcher;
	MemberSlotChanged m_slotChanged;
	std::vector<Slot> m_slots;                 // H+0x188
	std::map<ObjectId, int> m_memberIndex;     // H+0x17C
	std::list<int> m_freeList;                 // H+0x194
	std::vector<Member> m_members;             // the contain list (H+0x54), insertion order
	std::set<ObjectId> m_registered;           // H+0x170
	ObjectId m_bannerCarrierId = 0;            // H+0x26C
	ObjectId m_otherNonSlotId = 0;             // H+0x264
	bool m_dirty = false;                      // H+0x120
	bool m_flag174 = false;                    // H+0x174
	bool m_built = false;                      // H+0x198
};
