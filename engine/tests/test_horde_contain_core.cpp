// OpenBFME unit tests: HordeContainCore (slot table, free list, member maps, rank fill-in, slot world positions).
// GPL-3.0. Lane HORDE-1, spec horde-and-movement.md 1.3, 2.1, 2.6 and checklist step 6.
// Expected values are worked by hand from the formulas cited in GameLogic/Object/Contain/HordeContainCore.h
// (RW 0x877751, 0x873D48, 0x873F30, 0x87370B, 0x875847); the RNG sequences come from a second GameLogicRandom
// instance driven the way the retail build loop drives it.

#include "doctest.h"
#include "IniTestUtil.h"
#include "HordeTestUtil.h"

#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"

#include <cmath>
#include <map>

using namespace initest;

namespace
{
HordeContainModuleData parseHorde(const std::string &body)
{
	HordeContainModuleData data;
	Fixture fx;
	fx.env.blocks.registerBlock("Body", [&data](INI *ini) { data.parseFromINI(ini); });
	const std::string err = loadError(fx.env, "core.ini", "Body\n" + body + "\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	return data;
}

// the GondorFighterHorde slot table (3 ranks of 5, spec 1.3)
const char *kGondor =
	"  RankInfo = RankNumber:1 UnitType:GondorFighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 Position:X:50 Y:40 Position:X:50 Y:-40\n"
	"  RankInfo = RankNumber:2 UnitType:GondorFighter Position:X:30 Y:0 Leader 1 0 Position:X:30 Y:20 Leader 1 1 Position:X:30 Y:-20 Leader 1 2 Position:X:30 Y:40 Leader 1 3 Position:X:30 Y:-40 Leader 1 4\n"
	"  RankInfo = RankNumber:3 UnitType:GondorFighter Position:X:10 Y:0 Leader 2 0 Position:X:10 Y:20 Leader 2 1 Position:X:10 Y:-20 Leader 2 2 Position:X:10 Y:40 Leader 2 3 Position:X:10 Y:-40 Leader 2 4\n";

bool sameName(const std::string &slot, const std::string &member)
{
	return slot == member;
}

bool nearly(float a, double b, double eps = 1e-4)
{
	return std::fabs((double)a - b) <= eps;
}

// a full horde: member id 100 + k in slot k
void fill(HordeContainCore &core, int count = 15)
{
	for (int k = 0; k < count; ++k)
	{
		REQUIRE(core.addMember(100 + k, "GondorFighter") == k);
	}
}
}

TEST_CASE("HordeContainCore: the 3x5 slot table, flat slot order, leader slots and the free list (RW 0x877751)")
{
	const HordeContainModuleData d = parseHorde(kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	rng.enableCallLog(true);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	REQUIRE(core.slots().size() == 15);
	const float ys[5] = { 0, 20, -20, 40, -40 };
	const float xs[3] = { 50, 30, 10 };
	for (int i = 0; i < 15; ++i)
	{
		const HordeContainCore::Slot &s = core.slots()[(size_t)i];
		CHECK(s.rank == i / 5 + 1);
		CHECK(s.x == xs[i / 5]);
		CHECK(s.y == ys[i % 5]);
		CHECK(s.x2 == s.x);
		CHECK(s.y2 == s.y);
		CHECK(s.angle == 0.0f);
		// leaderSlot = firstSlotOfRank(leaderRank) + leaderIndex
		CHECK(s.leaderSlot == (i < 5 ? -1 : (i < 10 ? i - 5 : i - 5)));
	}
	CHECK(core.slots()[5].leaderSlot == 0);
	CHECK(core.slots()[9].leaderSlot == 4);
	CHECK(core.slots()[10].leaderSlot == 5);
	CHECK(core.slots()[14].leaderSlot == 9);
	// the free list holds every slot, in order
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 });
	// RandomOffset 0 draws nothing
	CHECK(rng.callLog().empty());
	CHECK(core.unverified().size() == 2); // leader links built but unread, and the two coordinate copies
}

TEST_CASE("HordeContainCore: RandomOffset jitter draws x then y per slot, in slot order, with the retail bounds and call sites")
{
	const HordeContainModuleData d = parseHorde(std::string("  RandomOffset = X:3.9 Y:2\n") + kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(777, -1);
	rng.enableCallLog(true);
	GameLogicRandom reference(RandomAlgorithm::RotWK_GameDat_LCG);
	reference.initGameLogicRandom(777, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	REQUIRE(rng.callLog().size() == 30);
	const float ys[5] = { 0, 20, -20, 40, -40 };
	const float xs[3] = { 50, 30, 10 };
	for (int i = 0; i < 15; ++i)
	{
		const int rx = reference.getValue(-3, 3, "r", 0); // (int)-3.9 = -3, (int)3.9 = 3 (truncated bounds)
		const int ry = reference.getValue(-2, 2, "r", 0);
		const HordeContainCore::Slot &s = core.slots()[(size_t)i];
		CHECK(s.x == xs[i / 5] + (float)rx);
		CHECK(s.y == ys[i % 5] + (float)ry);
		const GameLogicRandom::Call &cx = rng.callLog()[(size_t)(2 * i)];
		const GameLogicRandom::Call &cy = rng.callLog()[(size_t)(2 * i + 1)];
		CHECK(cx.lo == -3);
		CHECK(cx.hi == 3);
		CHECK(cx.line == 1575);
		CHECK(cy.lo == -2);
		CHECK(cy.hi == 2);
		CHECK(cy.line == 1579);
		CHECK_FALSE(cx.real);
	}
	// only X: no y draws
	GameLogicRandom rng2(RandomAlgorithm::ZH_CarryChain);
	rng2.initGameLogicRandom(5, -1);
	rng2.enableCallLog(true);
	const HordeContainModuleData dx = parseHorde(std::string("  RandomOffset = X:4 Y:0\n") + kGondor);
	HordeContainCore c2(dx, rng2, sameName);
	c2.buildSlots(true);
	CHECK(rng2.callLog().size() == 15);
	for (const GameLogicRandom::Call &c : rng2.callLog())
	{
		CHECK(c.line == 1575);
	}
}

TEST_CASE("HordeContainCore: members take the first free slot of a rank with their UnitType; others stay slotless (RW 0x873F30)")
{
	const HordeContainModuleData d = parseHorde(
		"  RankInfo = RankNumber:1 UnitType:Spear Position:X:50 Y:0 Position:X:50 Y:20\n"
		"  RankInfo = RankNumber:2 UnitType:Sword Position:X:30 Y:0 Position:X:30 Y:20\n");
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	CHECK(core.addMember(1, "Sword") == 2);  // the first free slot whose rank is Sword
	CHECK(core.addMember(2, "Sword") == 3);
	CHECK(core.addMember(3, "Sword") == -1); // no free Sword slot left
	CHECK(core.addMember(4, "Spear") == 0);
	CHECK(core.addMember(5, "Archer") == -1);
	CHECK(core.slotOf(1) == 2);
	CHECK(core.slotOf(3) == -1);
	CHECK(core.slotOf(5) == -1);
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 1 });
	CHECK(core.registeredMembers() == std::set<HordeContainCore::ObjectId>{ 1, 2, 4 });
	CHECK(core.members().size() == 5);
	CHECK_THROWS_AS(HordeContainCore(d, rng, nullptr), std::logic_error);
}

TEST_CASE("HordeContainCore: killing rank-1 slot 0 of a 3x5 horde moves slot 5 to 0, slot 10 to 5 and leaves slot 10 free (spec 2.6)")
{
	const HordeContainModuleData d = parseHorde(kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	rng.enableCallLog(true);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	fill(core);
	core.clearDirty();
	std::vector<std::pair<int, int>> changes;
	core.setMemberSlotChanged([&changes](HordeContainCore::ObjectId id, int slot) { changes.push_back({ (int)id, slot }); });
	const size_t rngCalls = rng.callLog().size();

	core.removeMember(100, false);
	// cascade: the nearest rank-2 man (slot 5 at (30,0), 400 away from (50,0)) moves up, then the nearest rank-3 man (slot 10)
	CHECK(core.slotOf(105) == 0);
	CHECK(core.slotOf(110) == 5);
	CHECK(core.slotOf(100) == -1);
	for (int k : { 1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14 })
	{
		CHECK(core.slotOf(100 + k) == k);
	}
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 10 });
	CHECK(changes == std::vector<std::pair<int, int>>{ { 105, 0 }, { 110, 5 } });
	CHECK(core.dirty());
	CHECK(core.members().size() == 14);
	CHECK(core.registeredMembers().count(100) == 0);
	CHECK(rng.callLog().size() == rngCalls); // fill-in draws nothing
	// a second death in the same spot cascades again: slot 1 -> 5? no: the next lowest free is 1 (rank 1): nearest rank-2 man
	core.removeMember(101, false);
	// free list now {10, 1}; lowest 1 (rank 1 at (50,20)): rank-2 candidates: slot 5 man (id 110: (30,0): 400+400=800)
	// vs slot 6 (30,20): 400; the nearest is slot 6
	CHECK(core.slotOf(106) == 1);
}

TEST_CASE("HordeContainCore: RanksThatStopAdvance bounds the advance (first listed value >= the free slot's rank, default 99)")
{
	const HordeContainModuleData d = parseHorde(std::string("  RanksThatStopAdvance = 2\n") + kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	fill(core);
	core.removeMember(100, false);
	// stop for rank 1 is 2: the rank-2 man moves up; then the free slot is rank 2 and stop is 2 again: nothing may move into it
	CHECK(core.slotOf(105) == 0);
	CHECK(core.slotOf(110) == 10);
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 5 });
	// stop = 1 for the rank-1 hole: the candidate window (1, 1] is empty, nobody advances
	const HordeContainModuleData d2 = parseHorde(std::string("  RanksThatStopAdvance = 1\n") + kGondor);
	HordeContainCore c2(d2, rng, sameName);
	c2.buildSlots(true);
	fill(c2);
	c2.removeMember(100, false);
	CHECK(c2.slotOf(105) == 5);
	CHECK(std::vector<int>(c2.freeList().begin(), c2.freeList().end()) == std::vector<int>{ 0 });
	// values below the hole's rank are skipped: [1 3] with a rank-2 hole gives 3, so rank 3 may advance into it
	const HordeContainModuleData d3 = parseHorde(std::string("  RanksThatStopAdvance = 1 3\n") + kGondor);
	HordeContainCore c3(d3, rng, sameName);
	c3.buildSlots(true);
	fill(c3);
	c3.removeMember(105, false); // slot 5, rank 2
	CHECK(c3.slotOf(110) == 5);
}

TEST_CASE("HordeContainCore: the candidate bound follows the chosen rank, so the member list order decides (retail quirk, RW 0x873E88)")
{
	// rank 1: slot 0 at (0,0); rank 2: slot 1 far (100,0); rank 3: slot 2 near (10,0)
	const HordeContainModuleData d = parseHorde(
		"  RankInfo = RankNumber:1 UnitType:A Position:X:0 Y:0\n"
		"  RankInfo = RankNumber:2 UnitType:B Position:X:100 Y:0\n"
		"  RankInfo = RankNumber:3 UnitType:C Position:X:10 Y:0\n");
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	{
		HordeContainCore core(d, rng, sameName);
		core.buildSlots(true);
		core.addMember(1, "A");
		core.addMember(2, "B"); // listed before the rank-3 man
		core.addMember(3, "C");
		core.removeMember(1, false);
		// B (rank 2, 10000 away) is taken first and narrows the bound to 2: the nearer rank-3 man is never a candidate
		CHECK(core.slotOf(2) == 0);
		// (the next fill-in step then moves the rank-3 man into B's old slot)
		CHECK(core.slotOf(3) == 1);
		CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 2 });
	}
	{
		HordeContainCore core(d, rng, sameName);
		core.buildSlots(true);
		core.addMember(1, "A");
		core.addMember(3, "C"); // the rank-3 man first
		core.addMember(2, "B");
		core.removeMember(1, false);
		// C (100 away) is taken first; B (rank 2 <= 3, 10000 away) does not beat it
		CHECK(core.slotOf(3) == 0);
		CHECK(core.slotOf(2) == 1);
		// slot 2's old index was freed and nothing deeper exists: free list {2}
		CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 2 });
	}
}

TEST_CASE("HordeContainCore: special members (banner carrier, template flag) free no slot; replenishing finds the freed slot")
{
	const HordeContainModuleData d = parseHorde(kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	fill(core, 14); // slot 14 stays free
	core.setBannerCarrierId(999);
	CHECK(core.addMember(999, "GondorInfantryBanner") == -1);
	core.removeMember(999, false);
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 14 });
	// a template flagged special (ThingTemplate+0x109 bit 3) frees nothing
	CHECK(core.addMember(500, "Other") == -1);
	core.removeMember(500, true);
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 14 });
	// a regular member whose slot was the last one: removal frees it, fill-in has nothing deeper to move
	core.removeMember(113, false);
	CHECK(std::vector<int>(core.freeList().begin(), core.freeList().end()) == std::vector<int>{ 14, 13 });
	// a replenishing member takes the first free-list entry whose rank's UnitType matches: 14 (list order, not lowest)
	CHECK(core.addMember(600, "GondorFighter") == 14);
	CHECK(core.addMember(601, "GondorFighter") == 13);
}

TEST_CASE("HordeContainCore: slot world positions rotate the offset by the owner's angle (RW 0x875847 / 0x86BFB5, not the spec's polar form)")
{
	const HordeContainModuleData d = parseHorde(kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	fill(core);
	HordeContainCore::Placement owner;
	owner.position = Coord3D{ 100.0f, 200.0f, 5.0f };
	owner.angle = 0.0f;
	float outAngle = 123.0f;
	Coord3D p = core.getSlotWorldPos(101, owner, &outAngle); // slot 1 = (50, 20)
	CHECK(nearly(p.x, 150.0));
	CHECK(nearly(p.y, 220.0));
	CHECK(nearly(p.z, 5.0));
	CHECK(outAngle == 0.0f);
	owner.angle = 1.57079633f; // 90 degrees: (ox, oy) -> (-oy, ox)
	p = core.getSlotWorldPos(101, owner, nullptr);
	CHECK(nearly(p.x, 100.0 - 20.0, 1e-3));
	CHECK(nearly(p.y, 200.0 + 50.0, 1e-3));
	owner.angle = 3.14159265f; // 180 degrees: (ox, oy) -> (-ox, -oy)
	p = core.getSlotWorldPos(101, owner, nullptr);
	CHECK(nearly(p.x, 50.0, 1e-3));
	CHECK(nearly(p.y, 180.0, 1e-3));
	// by index uses the second copy (equal here)
	const Coord3D q = core.getSlotWorldPosByIndex(1, owner);
	CHECK(nearly(q.x, p.x, 1e-6));
	CHECK(nearly(q.y, p.y, 1e-6));
	CHECK(nearly(q.z, 5.0));
	// an unregistered member reads index 0 (std::map operator[]) -> slot 0
	owner.angle = 0.0f;
	p = core.getSlotWorldPos(4242, owner, nullptr);
	CHECK(nearly(p.x, 150.0));
	CHECK(nearly(p.y, 200.0));
	CHECK(core.slotOf(4242) == 0); // and the lookup inserted it
	// the banner carrier alone in the contain list stands at the centre
	GameLogicRandom r2(RandomAlgorithm::RotWK_GameDat_LCG);
	r2.initGameLogicRandom(1, -1);
	HordeContainCore solo(d, r2, sameName);
	solo.buildSlots(true);
	solo.setBannerCarrierId(7);
	CHECK(solo.addMember(7, "GondorFighter") == 0);
	p = solo.getSlotWorldPos(7, owner, nullptr);
	CHECK(nearly(p.x, 100.0));
	CHECK(nearly(p.y, 200.0));
	solo.addMember(8, "GondorFighter");
	p = solo.getSlotWorldPos(7, owner, nullptr);
	CHECK(nearly(p.x, 150.0)); // two members: the banner carrier uses its slot offset again
	// no slots: the owner's position
	const HordeContainModuleData empty = parseHorde("");
	HordeContainCore none(empty, r2, sameName);
	none.buildSlots(true);
	p = none.getSlotWorldPos(1, owner, nullptr);
	CHECK(nearly(p.x, 100.0));
	CHECK(nearly(p.y, 200.0));
}

TEST_CASE("HordeContainCore: rebuilding with members keeps their slots and frees only the unoccupied ones (RW 0x877918-0x877999)")
{
	const HordeContainModuleData d = parseHorde(kGondor);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(1, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	fill(core, 10);
	// the formation is swapped (fromScratch = false): members present, so nothing is added to the free list
	core.buildSlots(false);
	CHECK(core.slots().size() == 15);
	CHECK(core.slotOf(105) == 5);
	// ...but a forced rebuild adds the unoccupied indices (10..14) once more to the old list
	const size_t before = core.freeList().size();
	core.buildSlots(true);
	CHECK(core.freeList().size() == before + 5);
}

TEST_CASE("HordeContainCore: the random free slot unit type draws (0, freeCount - 1) at line 1191 and walks the free list")
{
	const HordeContainModuleData d = parseHorde(
		"  RankInfo = RankNumber:1 UnitType:Spear Position:X:50 Y:0 Position:X:50 Y:20\n"
		"  RankInfo = RankNumber:2 UnitType:Sword Position:X:30 Y:0 Position:X:30 Y:20\n");
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.initGameLogicRandom(31337, -1);
	rng.enableCallLog(true);
	GameLogicRandom reference(RandomAlgorithm::RotWK_GameDat_LCG);
	reference.initGameLogicRandom(31337, -1);
	HordeContainCore core(d, rng, sameName);
	core.buildSlots(true);
	const int r = reference.getValue(0, 3, "x", 0);
	const std::string expected = r < 2 ? "Spear" : "Sword"; // free list {0,1,2,3}: ranks 1,1,2,2
	CHECK(core.chooseRandomFreeSlotUnitType() == expected);
	REQUIRE(rng.callLog().size() == 1);
	CHECK(rng.callLog()[0].lo == 0);
	CHECK(rng.callLog()[0].hi == 3);
	CHECK(rng.callLog()[0].line == 1191);
	// nothing free: no draw
	core.addMember(1, "Spear");
	core.addMember(2, "Spear");
	core.addMember(3, "Sword");
	core.addMember(4, "Sword");
	const size_t calls = rng.callLog().size();
	CHECK(core.chooseRandomFreeSlotUnitType().empty());
	CHECK(rng.callLog().size() == calls);
}

TEST_CASE("HordeContainCore retail: the slot table of every HordeContain / HorseHordeContain module of the pure 2.01 data builds")
{
	using namespace horde1;
	Corpus &c = corpus();
	if (!c.available)
	{
		retailtest::printSkip("HordeContainCore retail");
		return;
	}
	const std::vector<ModuleBody> bodies = collectHordeBodies(c);
	REQUIRE(bodies.size() == 142);
	size_t totalSlots = 0, jitterDraws = 0, expectedDraws = 0, withJitter = 0, leaderLinks = 0, modulesWithRanks = 0;
	for (const ModuleBody &b : bodies)
	{
		const ParsedModule p = parseRetailModule(c, b);
		REQUIRE_MESSAGE(p.error.empty(), b.file << ":" << b.headerLine << ": " << p.error);
		GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
		rng.initGameLogicRandom(2024, -1);
		rng.enableCallLog(true);
		HordeContainCore core(p.data, rng, sameName);
		core.buildSlots(true);
		size_t positions = 0;
		for (const RankInfo &r : p.data.m_rankInfo)
		{
			positions += r.positions.size();
		}
		CHECK(core.slots().size() == positions);
		CHECK(core.freeList().size() == positions);
		modulesWithRanks += positions > 0 ? 1 : 0;
		const bool jx = p.data.m_randomOffset.x > 0.0f;
		const bool jy = p.data.m_randomOffset.y > 0.0f;
		withJitter += (jx || jy) ? 1 : 0;
		expectedDraws += positions * ((jx ? 1 : 0) + (jy ? 1 : 0));
		jitterDraws += rng.callLog().size();
		for (const HordeContainCore::Slot &s : core.slots())
		{
			CHECK(s.leaderSlot >= -1);
			CHECK(s.leaderSlot < (int)core.slots().size());
			leaderLinks += s.leaderSlot != -1 ? 1 : 0;
			// the jitter stays inside the bounds
			CHECK(std::fabs(s.x - s.x2) == 0.0f);
		}
		totalSlots += core.slots().size();
		// every slot gets a member of its own rank's UnitType from a matching horde: fill by unit type
		HordeContainCore::ObjectId id = 1;
		size_t placed = 0;
		for (const RankInfo &r : p.data.m_rankInfo)
		{
			for (size_t k = 0; k < r.positions.size(); ++k)
			{
				placed += core.addMember(id++, r.unitType) >= 0 ? 1 : 0;
			}
		}
		CHECK(placed == positions); // every slot finds a member of its UnitType
		CHECK(core.freeList().empty());
	}
	CHECK(totalSlots == 1558);          // the independent python tally of Position entries
	CHECK(leaderLinks == 594);          // and of `Leader` links
	CHECK(jitterDraws == expectedDraws); // one draw per positive RandomOffset axis per slot
	CHECK(withJitter == 87);            // independent python scan: modules with a positive RandomOffset axis
	CHECK(jitterDraws == 1802);         // and the number of build-time draws (Position count x positive axes)
	CHECK(modulesWithRanks == 142 - 0); // every one of these 142 modules lists at least one RankInfo position
}
