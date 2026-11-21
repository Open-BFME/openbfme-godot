// OpenBFME unit tests. GPL-3.0.
// Lane PORT-39 (issue fork issue 124): AIData's `SideInfo` block and the gatherer counts it carries — ResourceGatherersEasy / Normal / Hard — and what the AI does
// with them. No game files: the AIData text below is synthetic, shaped like retail data\ini\default\aidata.ini (its SideInfo blocks hold exactly those
// three rows) and like retail's (same three rows, RW 0x6C7C27's SideInfo walk).
//
// The binary facts each expectation comes from (RotWK game.dat, caveat S-001; read with tools/mod/rw.py):
//   * the block's field table RW 0xC1D7D8 .. 0xC1D7F8: `ResourceGatherersEasy` (name RW 0xC1D7BC) + 0x8, `ResourceGatherersNormal` (RW 0xC1D7A4) + 0xC,
//     `ResourceGatherersHard` (RW 0xC1D78C) + 0x10, all parseInt RW 0x42EC5E; the AIData row `SideInfo` RW 0xC1CCF0 calls RW 0x70011F;
//     the entry's ctor RW 0x6FFF8C stores 0 / 1 / 2 in those three fields (RW 0x6FFFB2 / 0x6FFFB5 / 0x6FFFBC) and 0x1C0 is its size (RW 0x42F6E0).
//   * RW 0x70011F: the name (RW 0x42DC9F -> an AsciiString) is `strcmp`ed (RW 0x4065AA) against the list at `[0xDE4B40] + 0x18` (TheAI's data) `+ 0xF4`,
//     each entry's + 0x1BC the next; on a match that entry is parsed into, else a new one is pushed at the list head. Then RW 0x42DB80 (initFromINI)
//     reads the block against the table above.
//   * RW 0x8F736E(ai, object): difficulty `[eax + 0x30]` = the player's AIPlayer + 0x30 (RW 0x6AA61B); 0 / 1 / 2 read the entry's + 0x8 / + 0xC / + 0x10
//     (RW 0x8F7407 / 0x8F740F / 0x8F7417), any other difficulty reads + 0x8; `[ai + 0x46] = 1` (RW 0x8F73D2 / 0x8F7429), `[ai + 0x7C] = -1`
//     (RW 0x8F7424) and `[ai + 0x78] = count + 1` (RW 0x8F7428 / 0x8F742D). A side with no entry runs no loop body: + 0x46 stays clear.
//   * RW 0x8F858E(AI): `[ai + 0x46]` clear -> out (RW 0x8F867A); `[ai + 0x7C] < [ai + 0x78]` -> count and order (RW 0x8F868C / 0x8F88F7); the count is
//     every member of every team of `[player + 0x34C]` with the object status bit 1 (`[obj + 0x10A] & 1`, RW 0x8F8859) whose AI (`[obj + 0x260]`,
//     RW 0x8F8862) answers vslot 0x17C, the supply truck interface (RW 0x8F886E) — the result goes to `[ai + 0x7C]` (RW 0x8F88EF); `[ai + 0x78] * 3 <=
//     counted` ends the pass (RW 0x8F89EF), else `[ai + 0x7C]++` per order (RW 0x8F8B69).
// What stays unported (stop S-4194) and is therefore NOT tested here: the object RW 0x8F5347 makes for the order (the actual unit request — the base
// builder's lane, fork issue 104), the preferred-dock filter of the count (RW 0x8F8882: the AIPlayer's economy sub system is not ported) and RW 0x8F736E's
// difficulty source outside a skirmish game.

#include "doctest.h"
#include "MoveTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>
#include <vector>

namespace
{
// retail aidata.ini shape: one SideInfo block per side, exactly the three gatherer rows. Rohan 2 / 3 / 5 and Mordor 1 / 3 / 5 are its values;
// Isengard's block repeats Rohan's numbers under another name.
const char kSideInfo[] =
	"AIData\n"
	"  SideInfo Rohan\n"
	"    ResourceGatherersEasy     = 2\n"
	"    ResourceGatherersNormal   = 3\n"
	"    ResourceGatherersHard     = 5\n"
	"  End\n"
	"  SideInfo Mordor\n"
	"    ResourceGatherersEasy     = 1\n"
	"    ResourceGatherersNormal   = 3\n"
	"    ResourceGatherersHard     = 5\n"
	"  End\n"
	"End\n";

// a supply truck (WorkerAIUpdate implements the supply truck interface, RW 0x8AA349 / 0x82C61B) and a plain soldier
const char kObjects[] =
	"Object Porter\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
	"  Behavior = WorkerAIUpdate W\n"
	"    MaxBoxes = 1\n"
	"  End\n"
	"End\n"
	"Object Soldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface A\n  End\n"
	"End\n";

// the two skirmish player templates the fixture loads are FactionA (side Alpha) and FactionB (side Beta); the AI's side is what a SideInfo names
struct Port39World : movetest::MoveWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;

	Port39World(const char *sideInfo, const char *aiData = nullptr, int difficulty = 1)
		: movetest::MoveWorld(100, 100, kObjects)
	{
		if (sideInfo)
		{
			std::string err;
			REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(sideInfo, logic->settings(), &err), err);
		}
		store.registerBlocks(env.blocks);
		if (aiData)
		{
			INI ini(env);
			const std::string text = aiData;
			ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		}
		buildMap(); // the pathfinder first: the trucks spawned below are AI objects on the map
		logic->skirmishAI().setStore(&store);
		bob()->setSkirmishDifficulty(difficulty);
		REQUIRE(logic->skirmishAI().newGame(true) == 1);
	}
	~Port39World() { logic->skirmishAI().reset(); } // the world hooks go before the objects do (the group's AIs read the player list)
	Player *alice() { return players.findPlayerWithName("Alice"); }
	Player *bob() { return players.findPlayerWithName("Bob"); }
	AISkirmishPlayer &ai() { return *logic->skirmishAI().findAI(bob()->getPlayerIndex()); }
	// a truck of Bob's, on the map
	Object *truck(float x = 300.0f)
	{
		Object *o = spawn("Porter", x, 300.0f, 0.0f, teamOf("Bob"));
		REQUIRE(o->getAIUpdateInterface());
		REQUIRE(o->getAIUpdateInterface()->getSupplyTruckAIInterface());
		return o;
	}
};

// the AI's side needs an ArmyDefinition, or the manager disables it (RW 0x6A9F96) and no AI init runs; the gatherer counts live in AIData's SideInfo, not here
const char kAIData[] = "SkirmishAIData TheSkirmishAIData\n DisableWallBuilding = No\nEnd\n"
					   "ArmyDefinition BetaArmy\n Side = Beta\nEnd\n";
} // namespace

TEST_CASE("PORT-39 settings: the AIData SideInfo rows are read into the side's entry, not skipped by the catch-all (RW 0xC1D7D8, RW 0x70011F)")
{
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(kSideInfo, g, &err), err);
	// the block used to be skipped whole by the catch-all; now its gatherer rows are read, and the block stays listed for what it carries beyond them
	// (BaseDefenseStructure1 + 0x1B8, SkillSet1 .. 5: S-4194), so the retail report and the S-152 tests still see it
	CHECK(g.unappliedFields.count("AIData.SideInfo") == 1);
	// a row this lane does not carry is consumed and recorded, never the loader's unknown-field error: retail and retail data may set one, and the
	// gatherer counts must still come out of the block
	GameLogicSettings extra;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(
		"AIData\n  SideInfo Rohan\n    ResourceGatherersEasy = 2\n    BaseDefenseStructure1 = RohanTower\n  End\nEnd\n", extra, &err), err);
	CHECK(extra.unappliedFields.count("AIData.SideInfo.BaseDefenseStructure1") == 1);
	REQUIRE(extra.sideInfos.size() == 1u);
	CHECK(extra.sideInfos[0].resourceGatherers[0] == 2);
	REQUIRE(g.sideInfos.size() == 2u);
	// each new block is pushed at the list head (RW 0x70011F's tail insert), so the file's last SideInfo is first
	CHECK(g.sideInfos[0].side == "Mordor");
	CHECK(g.sideInfos[1].side == "Rohan");
	CHECK(g.sideInfos[1].resourceGatherers[0] == 2); // + 0x8 Easy
	CHECK(g.sideInfos[1].resourceGatherers[1] == 3); // + 0xC Normal
	CHECK(g.sideInfos[1].resourceGatherers[2] == 5); // + 0x10 Hard
	CHECK(g.sideInfos[0].resourceGatherers[0] == 1);

	// a row a block leaves out keeps the entry constructor's value (RW 0x6FFF8C: + 0x8 = 0, + 0xC = 1, + 0x10 = 2)
	GameLogicSettings h;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  SideInfo Rohan\n    ResourceGatherersHard = 7\n  End\nEnd\n", h, &err), err);
	REQUIRE(h.sideInfos.size() == 1u);
	CHECK(h.sideInfos[0].resourceGatherers[0] == 0);
	CHECK(h.sideInfos[0].resourceGatherers[1] == 1);
	CHECK(h.sideInfos[0].resourceGatherers[2] == 7);

	// a second block of a side re-parses that side's entry (RW 0x70011F's `strcmp` match, RW 0x4065AA): one entry, the later block's rows
	GameLogicSettings t;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(std::string(kSideInfo) +
		"AIData\n  SideInfo Rohan\n    ResourceGatherersEasy = 4\n  End\nEnd\n", t, &err), err);
	REQUIRE(t.sideInfos.size() == 2u);
	int rohan = -1;
	for (size_t i = 0; i < t.sideInfos.size(); ++i)
	{
		if (t.sideInfos[i].side == "Rohan") { rohan = (int)i; }
	}
	REQUIRE(rohan >= 0); // tolerance: index found in sideInfos
	CHECK(t.sideInfos[rohan].resourceGatherers[0] == 4); // re-read
	CHECK(t.sideInfos[rohan].resourceGatherers[1] == 3); // kept
	CHECK(t.sideInfos[rohan].resourceGatherers[2] == 5); // kept

	// the name is compared exactly (RW 0x4065AA is strcmp): `Rohan` and `rohan` are two entries
	GameLogicSettings c;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  SideInfo Rohan\n    ResourceGatherersEasy = 2\n  End\n"
		"  SideInfo rohan\n    ResourceGatherersEasy = 9\n  End\nEnd\n", c, &err), err);
	CHECK(c.sideInfos.size() == 2u);
}

TEST_CASE("PORT-39 settings: the side's gatherer counts are part of the settings hash, and a game without them keeps its hash")
{
	// the counts are simulation input (the AI's + 0x78 comes from them, RW 0x8F742D): a peer with other data must diverge
	movetest::MoveWorld same(100, 100, kObjects);
	movetest::MoveWorld other(100, 100, kObjects);
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(kSideInfo, same.logic->settings(), &err), err);
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(std::string(kSideInfo) +
		"AIData\n  SideInfo Rohan\n    ResourceGatherersHard = 6\n  End\nEnd\n", other.logic->settings(), &err), err);
	StateHasher a, b;
	same.logic->settings().crc(a);
	other.logic->settings().crc(b);
	CHECK(a.value() != b.value());

	// no SideInfo at all: the block's absence contributes nothing, so a game whose data has none hashes as it did before this lane
	movetest::MoveWorld plain(100, 100, kObjects);
	movetest::MoveWorld plain2(100, 100, kObjects);
	StateHasher p, q;
	plain.logic->settings().crc(p);
	plain2.logic->settings().crc(q);
	CHECK(p.value() == q.value());
}

TEST_CASE("PORT-39 ai: a synthetic AI side wants the RotWK number of gatherers at each difficulty (RW 0x8F736E)")
{
	// retail Rohan numbers (Easy 2, Normal 3, Hard 5) under the AI's own side, Beta -> `[ai + 0x78]` = count + 1 (RW 0x8F7428 / 0x8F742D)
	const char *beta = "AIData\n  SideInfo Beta\n"
					   "    ResourceGatherersEasy = 2\n    ResourceGatherersNormal = 3\n    ResourceGatherersHard = 5\n  End\nEnd\n";
	{
		Port39World w(beta, kAIData, 0); // difficulty 0 = Easy (`[eax + 0x30]` == 0 -> + 0x8, RW 0x8F7407)
		CHECK(w.ai().needsResourceGatherers);   // + 0x46 = 1 (RW 0x8F73D2)
		CHECK(w.ai().hasSideInfo);
		CHECK(w.ai().resourceGatherersWanted == 3);  // 2 + 1
		CHECK(w.ai().resourceGatherersHave == -1);   // + 0x7C = -1 (RW 0x8F7424)
	}
	{
		Port39World w(beta, kAIData, 1); // Normal: + 0xC (RW 0x8F740F)
		CHECK(w.ai().resourceGatherersWanted == 4); // 3 + 1
	}
	{
		Port39World w(beta, kAIData, 2); // Hard: + 0x10 (RW 0x8F7417)
		CHECK(w.ai().resourceGatherersWanted == 6); // 5 + 1
	}
	{
		// a difficulty above 2 is not 1 and not 2, so the walk keeps + 0x8, the Easy count (RW 0x8F7407 / 0x8F740A / 0x8F7412 only test 1 and 2).
		// Difficulty 3 is GameDifficulty BRUTAL (RW 0x64501B): the BRUTAL AI wants the Easy number of gatherers.
		Port39World w(beta, kAIData, 3);
		CHECK(w.ai().resourceGatherersWanted == 3); // Easy 2 + 1
	}
	{
		// a side with no SideInfo block (only Alpha, and Bob's side is Beta): `[ai + 0x46]` stays clear, as RW does when its walk matches nothing
		Port39World w("AIData\n  SideInfo Alpha\n    ResourceGatherersEasy = 2\n  End\nEnd\n", kAIData, 1);
		CHECK_FALSE(w.ai().needsResourceGatherers);
		CHECK_FALSE(w.ai().hasSideInfo);
		CHECK(w.ai().resourceGatherersWanted == 0);
		CHECK(w.ai().resourceGatherersHave == -1);
	}
	{
		// the two sides' blocks select by the AI's own side, not by the first block of the file: Bob is FactionB (side Beta)
		Port39World w("AIData\n  SideInfo Alpha\n    ResourceGatherersNormal = 9\n  End\n"
					  "  SideInfo Beta\n    ResourceGatherersNormal = 4\n  End\nEnd\n", kAIData, 1);
		CHECK(w.ai().resourceGatherersWanted == 5); // Beta's 4 + 1
	}
}

TEST_CASE("PORT-39 ai: the AI counts its live supply trucks and asks for one more until it has the number (RW 0x8F858E)")
{
	// Normal 12 -> `[ai + 0x78] = 13` (RW 0x8F742D): the count can grow past `[ai + 0x7C]` without the reserve check (`[ai + 0x78] * 3`) firing first
	Port39World w("AIData\n  SideInfo Beta\n"
				  "    ResourceGatherersEasy = 2\n    ResourceGatherersNormal = 12\n    ResourceGatherersHard = 5\n  End\nEnd\n", kAIData, 1);
	AISkirmishPlayer &ai = w.ai();
	REQUIRE(ai.resourceGatherersWanted == 13); // Normal 12 + 1
	SkirmishAIManager &m = w.logic->skirmishAI();

	// no truck yet: the count is 0 and the AI asks for one. The port makes no unit (S-4194), so `[ai + 0x7C]` stays RW's value for a pass with no maker —
	// 0 — and the ask is tallied
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 0); // RW 0x8F88EF
	CHECK(ai.gathererRequests == 1u);     // RW 0x8F8B69
	// the AI is still short (`[ai + 0x7C] < [ai + 0x78]`, RW 0x8F868C), so every pass asks again
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 0);
	CHECK(ai.gathererRequests == 2u);

	// four live trucks of Bob's: the count is 4, `[ai + 0x7C] = 4` (RW 0x8F88EF); 4 < 13 * 3, so the reserve check lets the ask through
	Object *t1 = w.truck(300.0f);
	w.truck(320.0f);
	w.truck(340.0f);
	w.truck(360.0f);
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 4);
	CHECK(ai.gathererRequests == 3u);

	// Alice's truck is not Bob's: it is not counted (RW 0x8F8859 tests the controlling player, the port its player index)
	Object *aliceTruck = w.spawn("Porter", 400.0f, 300.0f, 0.0f, w.teamOf("Alice"));
	REQUIRE(aliceTruck->getAIUpdateInterface());
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 4);
	CHECK(ai.gathererRequests == 4u);
	// a destroyed truck is not counted either (`[obj + 0x10A] & 1`: the status bit a destroy sets)
	w.logic->destroyObject(t1);
	w.logic->processDestroyList();
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 3);
	CHECK(ai.gathererRequests == 5u);

	// a soldier has no supply truck interface (AI vslot 0x17C, RW 0x8F886E): it never counts
	w.spawn("Soldier", 420.0f, 300.0f, 0.0f, w.teamOf("Bob"));
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 3);
	CHECK(ai.gathererRequests == 6u);

	// five more: eight live trucks, still far from 13 * 3 = 39, so the ask goes out
	for (int i = 0; i < 5; ++i)
	{
		w.truck(500.0f + 20.0f * (float)i);
	}
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 8);
	CHECK(ai.gathererRequests == 7u);

	// the number is reached: this pass still counts and still asks (RW 0x8F89EF only compares the reserve), and `[ai + 0x7C]` is now the wanted count
	for (int i = 0; i < 5; ++i)
	{
		w.truck(700.0f + 20.0f * (float)i);
	}
	CHECK(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 13);
	CHECK(ai.gathererRequests == 8u);
	// the next pass leaves at RW 0x8F868C (`[ai + 0x7C] < [ai + 0x78]` is false): no count, no ask — a truck lost now is not noticed until the count drops
	Object *lost = w.truck(900.0f);
	w.logic->destroyObject(lost);
	w.logic->processDestroyList();
	CHECK_FALSE(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 13); // untouched
	CHECK(ai.gathererRequests == 8u);
}

TEST_CASE("PORT-39 ai: a third of the wanted count in reserve ends the pass before the order (RW 0x8F89EF)")
{
	// Hard 1 -> `[ai + 0x78] = 2` (RW 0x8F742D): six trucks is `2 * 3 <= 6`, so RW 0x8F89EF leaves for RW 0x8F8B56 without the order — while the count
	// itself has already happened (RW 0x8F88EF is before the comparison)
	Port39World w("AIData\n  SideInfo Beta\n"
				  "    ResourceGatherersEasy = 2\n    ResourceGatherersNormal = 12\n    ResourceGatherersHard = 1\n  End\nEnd\n", kAIData, 2);
	AISkirmishPlayer &ai = w.ai();
	REQUIRE(ai.resourceGatherersWanted == 2); // Hard 1 + 1
	SkirmishAIManager &m = w.logic->skirmishAI();
	for (int i = 0; i < 6; ++i)
	{
		w.truck(300.0f + 20.0f * (float)i);
	}
	CHECK_FALSE(m.updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == 6); // RW 0x8F88EF
	CHECK(ai.gathererRequests == 0u);     // RW 0x8F8B69 was never reached
	// and the next pass takes the early out (`[ai + 0x7C] >= [ai + 0x78]`)
	CHECK_FALSE(m.updateResourceGatherers(ai));
	CHECK(ai.gathererRequests == 0u);
}

TEST_CASE("PORT-39 ai: an AI whose side has no SideInfo entry neither counts nor asks (RW 0x8F867A's early out)")
{
	Port39World w(nullptr, kAIData, 1);
	AISkirmishPlayer &ai = w.ai();
	REQUIRE_FALSE(ai.needsResourceGatherers);
	w.truck(); // a truck of Bob's exists, but the AI has no count to compare
	CHECK_FALSE(w.logic->skirmishAI().updateResourceGatherers(ai));
	CHECK(ai.resourceGatherersHave == -1); // untouched
	CHECK(ai.gathererRequests == 0u);
}
