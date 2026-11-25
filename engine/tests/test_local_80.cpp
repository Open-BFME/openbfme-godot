// OpenBFME unit tests. GPL-3.0.
// LOCAL-80 (fork issue 704): script rank actions (276 PLAYER_ADD_SKILLPOINTS, 277 PLAYER_ADD_RANKLEVEL, 278 PLAYER_SET_RANKLEVEL,
// 279 PLAYER_SET_RANKLEVELLIMIT) and condition 145 PLAYER_COMPARE_RANK.
//
// Binary verification against RotWK 2.01 game.dat (md5 daed3668006cd90f01c34e5a7da1901f):
//   * Action 276 PLAYER_ADD_SKILLPOINTS {SIDE, INT}: switch RW 0x7CDF75 -> RW 0x7BD063:
//       mask RW 0x758F7C(side, 0), loop RW 0x6A85EE(&mask): cvtsi2ss (RW 0x7BD090), PlayerScience::addSkillPoints((float)delta, false) (RW 0x782AA4).
//   * Action 277 PLAYER_ADD_RANKLEVEL {SIDE, INT}: switch RW 0x7CDFB4 -> RW 0x7BD0AF:
//       mask RW 0x758F7C, loop RW 0x6A85EE: vslot 1 setRankLevel([player+0x1C] + delta) (RW 0x6AA9C2 -> RW 0x782865),
//       then RW 0x71C311([player+0x1C]) writes m_scalarBaseRank (+0x18).
//   * Action 278 PLAYER_SET_RANKLEVEL {SIDE, INT}: switch RW 0x7CDFD8 -> RW 0x7BD107:
//       mask RW 0x758F7C, loop RW 0x6A85EE: vslot 1 setRankLevel(level) (RW 0x6AA9C2 -> RW 0x782865),
//       then RW 0x71C311([player+0x1C]) writes m_scalarBaseRank (+0x18).
//   * Action 279 PLAYER_SET_RANKLEVELLIMIT {INT}: switch RW 0x7CE020 -> RW 0x7BD1A8 -> RW 0x7BB04E (PORT-43): max(arg, 1).
//   * Condition 145 PLAYER_COMPARE_RANK {SIDE, COMPARISON, INT}: switch RW 0x7ECFF6 (condType - 5 = 140) -> RW 0x7E5EC8:
//       mask RW 0x758F7C(side, 0) -> RW 0x6A85B6 (playerOfMask: exact single-player mask, else false);
//       compares m_rankLevel ([player+0x1C], not skillPoints) with op 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 != (other -> false).

#include "doctest.h"

#include "IniTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/RankInfo.h"
#include "Common/Science.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SpellStores.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

struct SyntheticRankFixture
{
	initest::Fixture iniFx;
	NameKeyGenerator keys;
	SpellStores stores;
	PlayerTemplate tmpl;

	SyntheticRankFixture()
		: stores(keys)
	{
		keys.init();
		stores.install();
		SpellStores::registerBlocks(iniFx.env.blocks);

		const char *ranksIni =
			"Rank 1\n  SkillPointsNeededDefault = 0\n  SciencePurchasePointsGranted = 5\nEnd\n"
			"Rank 2\n  SkillPointsNeededDefault = 60\n  SciencePurchasePointsGranted = 1\nEnd\n"
			"Rank 3\n  SkillPointsNeededDefault = 120\n  SciencePurchasePointsGranted = 2\nEnd\n"
			"Rank 4\n  SkillPointsNeededDefault = 180\n  SciencePurchasePointsGranted = 3\nEnd\n"
			"Rank 5\n  SkillPointsNeededDefault = 240\n  SciencePurchasePointsGranted = 1\nEnd\n"
			"Rank 6\n  SkillPointsNeededDefault = 300\n  SciencePurchasePointsGranted = 1\nEnd\n";
		REQUIRE(initest::loadError(iniFx.env, "ranks.ini", ranksIni).empty());
		REQUIRE(TheRankInfoStore != nullptr);
		REQUIRE(TheRankInfoStore->getRankLevelCount() == 6);

		tmpl.m_side = "Synthetic";
		tmpl.m_maxLevelMP = 5;
		tmpl.m_maxLevelSP = 5;
	}

	void bindPlayers(scripttest::ScriptGame &g)
	{
		SpellGameMode mode;
		mode.skirmishOrMultiplayer = true;
		mode.campaign = false;
		for (int i = 1; i <= 2; ++i)
		{
			Player *p = g.lw.players.getNthPlayer(i);
			REQUIRE(p != nullptr);
			p->science().bind(&tmpl, mode, &p->getScoreKeeper());
			p->science().reset();
		}
	}
};

} // namespace

TEST_CASE("LOCAL-80 synthetic: PLAYER_ADD_RANKLEVEL, PLAYER_SET_RANKLEVEL, PLAYER_ADD_SKILLPOINTS, PLAYER_SET_RANKLEVELLIMIT "
		  "(RW 0x7BD063, 0x7BD0AF, 0x7BD107, 0x7BD1A8)")
{
	using namespace scripttest;

	SyntheticRankFixture fx;
	ScriptGame g;
	fx.bindPlayers(g);

	Player *alice = g.lw.players.getNthPlayer(1);
	Player *bob = g.lw.players.getNthPlayer(2);
	REQUIRE(alice->science().getRankLevel() == 1);
	REQUIRE(alice->science().getSciencePurchasePoints() == 5);
	REQUIRE(alice->science().getScalarBaseRank() == 1);

	// Step 1: PLAYER_ADD_RANKLEVEL(<This Player>, 2) on Alice -> rank 3, PP 5+1+2=8, skillPoints 120, scalarBaseRank 3 (RW 0x7BD0AF -> 0x782865, 0x71C311).
	// Step 2: PLAYER_SET_RANKLEVELLIMIT(4) -> GameLogic +0x118 = 4 (RW 0x7BD1A8 -> 0x7BB04E).
	g.lists[1].items.push_back(item(script("Add2", { { cond("CONDITION_TRUE", {}) } },
		{ act("PLAYER_ADD_RANKLEVEL", { sp(11, "<This Player>"), ip(0, 2) }), act("PLAYER_SET_RANKLEVELLIMIT", { ip(0, 4) }) }, {}, true)));
	g.start();
	g.frame();

	CHECK(g.engine().stats().unportedActions.empty());
	CHECK(alice->science().getRankLevel() == 3);
	CHECK(alice->science().getSciencePurchasePoints() == 8);
	CHECK(alice->science().getSkillPoints() == 120.0f);
	CHECK(alice->science().getScalarBaseRank() == 3);
	CHECK(g.lw.logic->getRankLevelLimit() == 4);
	// Bob is untouched.
	CHECK(bob->science().getRankLevel() == 1);
	CHECK(bob->science().getSciencePurchasePoints() == 5);

	// Clamp to getMaxRankLevel() (MaxLevelMP = 5, even though RankInfoStore has 6 ranks): RW 0x78289A .. 0x7828A1.
	{
		ScriptGame g2;
		fx.bindPlayers(g2);
		Player *a2 = g2.lw.players.getNthPlayer(1);
		g2.lists[1].items.push_back(item(script("Add100", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_ADD_RANKLEVEL", { sp(11, "<This Player>"), ip(0, 100) }) }, {}, true)));
		g2.start();
		g2.frame();
		CHECK(a2->science().getRankLevel() == 5);
		CHECK(a2->science().getSciencePurchasePoints() == 12); // 5 + 1 + 2 + 3 + 1
		CHECK(a2->science().getSkillPoints() == 240.0f);
		CHECK(a2->science().getScalarBaseRank() == 5);
	}

	// PLAYER_SET_RANKLEVEL(2) after reaching 5 resets (RW 0x7828B8) and climbs to 2; then PLAYER_SET_RANKLEVEL(0) clamps to 1 (RW 0x782875) and resets.
	{
		ScriptGame g3;
		fx.bindPlayers(g3);
		Player *a3 = g3.lw.players.getNthPlayer(1);
		a3->science().setRankLevel(5);
		REQUIRE(a3->science().getRankLevel() == 5);
		REQUIRE(a3->science().getSciencePurchasePoints() == 12);

		g3.lists[1].items.push_back(item(script("Set2", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_SET_RANKLEVEL", { sp(11, "<This Player>"), ip(0, 2) }) }, {}, true)));
		g3.start();
		g3.frame();
		CHECK(a3->science().getRankLevel() == 2);
		CHECK(a3->science().getSciencePurchasePoints() == 6); // 5 + 1
		CHECK(a3->science().getSkillPoints() == 60.0f);
		CHECK(a3->science().getScalarBaseRank() == 2);

		ScriptGame g4;
		fx.bindPlayers(g4);
		Player *a4 = g4.lw.players.getNthPlayer(1);
		a4->science().setRankLevel(3);
		g4.lists[1].items.push_back(item(script("Set0", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_SET_RANKLEVEL", { sp(11, "<This Player>"), ip(0, 0) }) }, {}, true)));
		g4.start();
		g4.frame();
		// RW 0x78286F .. 0x78287A: level < 1 clamps to 1; since 1 < 3, reset() runs and takes points back to rank 1.
		CHECK(a4->science().getRankLevel() == 1);
		CHECK(a4->science().getSciencePurchasePoints() == 5);
		CHECK(a4->science().getSkillPoints() == 0.0f);
		CHECK(a4->science().getScalarBaseRank() == 1);

		// At rank 1, PLAYER_SET_RANKLEVEL(0) clamps to 1 == 1 -> no-op.
		ScriptGame g5;
		fx.bindPlayers(g5);
		Player *a5 = g5.lw.players.getNthPlayer(1);
		a5->science().addSciencePurchasePoints(4); // 9 total while still at rank 1
		g5.lists[1].items.push_back(item(script("Set0At1", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_SET_RANKLEVEL", { sp(11, "<This Player>"), ip(0, 0) }) }, {}, true)));
		g5.start();
		g5.frame();
		CHECK(a5->science().getRankLevel() == 1);
		CHECK(a5->science().getSciencePurchasePoints() == 9); // untouched because setRankLevel(1) returned false
	}

	// PLAYER_ADD_SKILLPOINTS(<This Player>, 65) (RW 0x7BD063 -> RW 0x782AA4(65.0f, false)):
	// raises rank 1 -> 2, leaves scalarBaseRank at 1 (RW 0x7BD063 does NOT call RW 0x71C311).
	{
		ScriptGame g6;
		fx.bindPlayers(g6);
		Player *a6 = g6.lw.players.getNthPlayer(1);
		g6.lists[1].items.push_back(item(script("AddSP65", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_ADD_SKILLPOINTS", { sp(11, "<This Player>"), ip(0, 65) }) }, {}, true)));
		g6.start();
		g6.frame();
		CHECK(g6.engine().stats().unportedActions.empty());
		CHECK(a6->science().getRankLevel() == 2);
		CHECK(a6->science().getSkillPoints() == 65.0f);
		CHECK(a6->science().getSciencePurchasePoints() == 6);
		CHECK(a6->science().getScalarBaseRank() == 1);
	}
}

TEST_CASE("LOCAL-80 synthetic: PLAYER_COMPARE_RANK boundaries, rank-not-skillpoints, and exact single-player mask "
		  "(RW 0x7ECFF6 -> 0x7E5EC8, RW 0x6A85B6)")
{
	using namespace scripttest;

	SyntheticRankFixture fx;
	ScriptGame g;
	fx.bindPlayers(g);

	Player *alice = g.lw.players.getNthPlayer(1);
	alice->science().setRankLevel(2);
	alice->science().addSkillPoints(30.0f, false); // skillPoints = 90.0f, still rank 2
	REQUIRE(alice->science().getRankLevel() == 2);
	REQUIRE(alice->science().getSkillPoints() == 90.0f);

	auto evalRank = [&](const char *who, int op, int rhs, const char *counterName)
	{
		g.lists[1].items.push_back(item(script(counterName,
			{ { cond("PLAYER_COMPARE_RANK", { sp(11, who), ip(6, op), ip(0, rhs) }) } },
			{ act("SET_COUNTER", { sp(4, counterName), ip(0, 1) }) },
			{ act("SET_COUNTER", { sp(4, counterName), ip(0, 0) }) }, true)));
	};

	// All 6 operators at boundaries around rank 2 (0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=) plus invalid op 6.
	evalRank("<This Player>", 0, 3, "lt_t");
	evalRank("<This Player>", 0, 2, "lt_f");
	evalRank("<This Player>", 1, 2, "le_t");
	evalRank("<This Player>", 1, 1, "le_f");
	evalRank("<This Player>", 2, 2, "eq_t");
	evalRank("<This Player>", 2, 90, "eq_sp_f"); // compares rank level (2), not skillPoints (90)
	evalRank("<This Player>", 3, 2, "ge_t");
	evalRank("<This Player>", 3, 3, "ge_f");
	evalRank("<This Player>", 4, 1, "gt_t");
	evalRank("<This Player>", 4, 2, "gt_f");
	evalRank("<This Player>", 5, 1337, "ne_t");
	evalRank("<This Player>", 5, 2, "ne_f");
	evalRank("<This Player>", 6, 2, "bad_op_f");
	// Exact single-player mask (RW 0x6A85B6): <Local Player> is Alice (single bit -> true), <All Players> has multiple bits -> false.
	evalRank("<Local Player>", 5, 1337, "local_ne_t");
	evalRank("<All Players>", 5, 1337, "all_mask_f");

	g.lw.players.setLocalPlayer(alice);
	g.start();
	g.frame();

	CHECK(g.engine().stats().unportedConditions.empty());
	CHECK(g.counter("Alice/lt_t") == 1);
	CHECK(g.counter("Alice/lt_f") == 0);
	CHECK(g.counter("Alice/le_t") == 1);
	CHECK(g.counter("Alice/le_f") == 0);
	CHECK(g.counter("Alice/eq_t") == 1);
	CHECK(g.counter("Alice/eq_sp_f") == 0);
	CHECK(g.counter("Alice/ge_t") == 1);
	CHECK(g.counter("Alice/ge_f") == 0);
	CHECK(g.counter("Alice/gt_t") == 1);
	CHECK(g.counter("Alice/gt_f") == 0);
	CHECK(g.counter("Alice/ne_t") == 1);
	CHECK(g.counter("Alice/ne_f") == 0);
	CHECK(g.counter("Alice/bad_op_f") == 0);
	CHECK(g.counter("Alice/local_ne_t") == 1);
	CHECK(g.counter("Alice/all_mask_f") == 0);
}

