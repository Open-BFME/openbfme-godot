// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-1: a player's rank, skill points, purchase points and sciences (Common/PlayerScience.h, RW Player + 8 / + 0x310).

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SpellStores.h"

#include <string>

namespace
{
// rank.ini's shape: rank 1 grants 5, every later rank 1 point at 60 skill points per rank (Mordor: 40)
std::string ranks(int count)
{
	std::string s = "Rank 1\n  SkillPointsNeededDefault = 0\n  SciencePurchasePointsGranted = 5\nEnd\n";
	for (int r = 2; r <= count; ++r)
	{
		s += "Rank " + std::to_string(r) + "\n  SkillPointsNeededDefault = " + std::to_string(60 * (r - 1)) + "\n  SkillPointsNeededMordor = " + std::to_string(40 * (r - 1))
			 + "\n  SciencePurchasePointsGranted = 1\n";
		if (r == 3)
		{
			s += "  SciencesGranted = S_RANK3\n";
		}
		s += "End\n";
	}
	return s;
}

struct World
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	SpellStores stores;
	PlayerTemplate men;
	ScoreKeeper score;
	World()
		: stores(keys)
	{
		keys.init();
		stores.install();
		SpellStores::registerBlocks(fx.env.blocks);
		const std::string sciences = "Science S_MEN\n  SciencePurchasePointCost = 0\n  IsGrantable = No\nEnd\n"
									 "Science S_GOOD\n  IsGrantable = No\nEnd\n"
									 "Science S_RANK3\nEnd\n"
									 "Science S_HEAL\n  PrerequisiteSciences = S_GOOD OR S_MEN\n  SciencePurchasePointCost = 4\n  SciencePurchasePointCostMP = 5\nEnd\n"
									 "Science S_T2\n  PrerequisiteSciences = S_MEN S_HEAL\n  SciencePurchasePointCostMP = 2\nEnd\n"
									 "Science S_FREE\n  PrerequisiteSciences = None\nEnd\n";
		const std::string err = initest::loadError(fx.env, "t.ini", sciences + ranks(10));
		REQUIRE(err == "");
		men.m_side = "Men";
		men.m_intrinsicSciences = { "S_GOOD" };
		men.m_intrinsicSciencesMP = { "S_MEN" };
		men.m_maxLevelMP = 6;
		men.m_maxLevelSP = 4;
	}
	ScienceType sci(const char *n) const { return stores.sciences().getScienceFromInternalName(n); }
};
} // namespace

TEST_CASE("SPELL-1 PlayerScience: reset gives rank 1, its points and the intrinsic sciences of the mode (RW 0x782774, 0x6AED4B)")
{
	World w;
	PlayerScience mp;
	mp.bind(&w.men, SpellGameMode{ true, false }, &w.score);
	mp.reset();
	CHECK(mp.getRankLevel() == 1);
	CHECK(mp.getSciencePurchasePoints() == 5);
	CHECK(mp.getSkillPoints() == 0.0f);
	CHECK(mp.getSkillPointsLevelUp() == 60);
	CHECK(mp.getSkillPointsLevelDown() == 0);
	CHECK(mp.sciences() == ScienceVec{ w.sci("S_MEN") });
	CHECK(mp.getMaxRankLevel() == 6);
	PlayerScience sp;
	sp.bind(&w.men, SpellGameMode{ false, false }, &w.score);
	sp.reset();
	CHECK(sp.sciences() == ScienceVec{ w.sci("S_GOOD") });
	CHECK(sp.getMaxRankLevel() == 4);
	w.men.m_intrinsicSciencePurchasePoints = 3;
	sp.reset();
	CHECK(sp.getSciencePurchasePoints() == 8);
	PlayerScience none;
	none.bind(nullptr, SpellGameMode{ true, false }, nullptr);
	none.reset();
	CHECK(none.getSciencePurchasePoints() == 5);
	CHECK(none.getMaxRankLevel() == 10);
	CHECK(none.sciences().empty());
	// the Mordor column
	PlayerTemplate mordor;
	mordor.m_side = "Mordor";
	mordor.m_maxLevelMP = 10;
	PlayerScience m;
	m.bind(&mordor, SpellGameMode{ true, false }, nullptr);
	m.reset();
	CHECK(m.getSkillPointsLevelUp() == 40);
}

TEST_CASE("SPELL-1 PlayerScience: skill points climb the ranks, grant points and sciences, and stop at the max rank (RW 0x782AA4, 0x782865)")
{
	World w;
	PlayerScience p;
	p.bind(&w.men, SpellGameMode{ true, false }, &w.score);
	p.reset();
	CHECK_FALSE(p.addSkillPoints(0.0f, true));
	CHECK_FALSE(p.addSkillPoints(59.5f, true));
	CHECK(p.getRankLevel() == 1);
	CHECK(p.addSkillPoints(0.5f, true)); // floor(60.0) >= 60
	CHECK(p.getRankLevel() == 2);
	CHECK(p.getSciencePurchasePoints() == 6);
	CHECK(p.getSkillPointsLevelUp() == 120);
	CHECK(p.getSkillPointsLevelDown() == 60);
	CHECK(w.score.sciencePurchasePointsEarned() == 1u);
	// two ranks at once; rank 3 grants S_RANK3
	CHECK(p.addSkillPoints(130.0f, true));
	CHECK(p.getRankLevel() == 4);
	CHECK(p.getSciencePurchasePoints() == 8);
	CHECK(p.hasScience(w.sci("S_RANK3")));
	// the max rank (MaxLevelMP 6) caps the points at rank 6's count (300)
	CHECK(p.addSkillPoints(10000.0f, true));
	CHECK(p.getRankLevel() == 6);
	CHECK(p.getSkillPoints() == 300.0f);
	CHECK_FALSE(p.addSkillPoints(5.0f, true));
	CHECK(p.getSciencePurchasePoints() == 10);
	// the modifier multiplies first (mulss)
	PlayerScience q;
	q.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	q.reset();
	q.setSkillPointsModifier(2.0f);
	CHECK(q.addSkillPoints(30.0f, true));
	CHECK(q.getRankLevel() == 2);
	// a rank cap
	q.setRankLevelCap(2);
	CHECK_FALSE(q.addSkillPoints(100.0f, true));
	CHECK(q.getSkillPoints() == 60.0f);
}

TEST_CASE("SPELL-1 PlayerScience: setRankLevel up raises the skill points, down resets first (RW 0x782865)")
{
	World w;
	PlayerScience p;
	p.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	p.reset();
	CHECK(p.setRankLevel(3));
	CHECK(p.getSkillPoints() == 120.0f);
	CHECK(p.getSciencePurchasePoints() == 7);
	CHECK(p.hasScience(w.sci("S_RANK3")));
	CHECK_FALSE(p.setRankLevel(3));
	CHECK(p.setRankLevel(99)); // clamped to the max rank 6
	CHECK(p.getRankLevel() == 6);
	CHECK(p.setRankLevel(2)); // lower: reset (rank 1, 5 points, the intrinsic sciences only), then up to 2
	CHECK(p.getRankLevel() == 2);
	CHECK(p.getSciencePurchasePoints() == 6);
	CHECK(p.getSkillPoints() == 60.0f);
	CHECK_FALSE(p.hasScience(w.sci("S_RANK3")));
	CHECK(p.setRankLevel(0)); // clamped to 1
	CHECK(p.getRankLevel() == 1);
}

TEST_CASE("SPELL-1 PlayerScience: purchase and grant rules (RW 0x6AC8BC, 0x6AE36F, 0x6AE3A7)")
{
	World w;
	PlayerScience p;
	p.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	p.reset();
	CHECK_FALSE(p.isCapableOfPurchasingScience(SCIENCE_INVALID));
	CHECK_FALSE(p.isCapableOfPurchasingScience(w.sci("S_MEN")));  // owned
	CHECK_FALSE(p.isCapableOfPurchasingScience(w.sci("S_FREE"))); // cost 0: not purchasable
	CHECK_FALSE(p.isCapableOfPurchasingScience(w.sci("S_T2")));   // needs S_HEAL
	CHECK(p.isCapableOfPurchasingScience(w.sci("S_HEAL")));       // MP cost 5 <= 5
	CHECK(p.attemptToPurchaseScience(w.sci("S_HEAL")));
	CHECK(p.getSciencePurchasePoints() == 0);
	CHECK(p.hasScience(w.sci("S_HEAL")));
	CHECK_FALSE(p.attemptToPurchaseScience(w.sci("S_HEAL")));
	CHECK_FALSE(p.attemptToPurchaseScience(w.sci("S_T2"))); // 0 < 2
	p.addSciencePurchasePoints(2);
	CHECK(p.attemptToPurchaseScience(w.sci("S_T2")));
	CHECK(p.sciences() == ScienceVec{ w.sci("S_MEN"), w.sci("S_HEAL"), w.sci("S_T2") });
	p.addSciencePurchasePoints(-50);
	CHECK(p.getSciencePurchasePoints() == 0);
	CHECK_FALSE(p.grantScience(w.sci("S_GOOD"))); // IsGrantable = No
	CHECK(p.grantScience(w.sci("S_FREE")));
	CHECK(p.droppedScienceNotifications() == 3u); // no special power hook installed
	int told = 0;
	p.setScienceAddedHook([&](ScienceType) { ++told; });
	CHECK(p.grantScience(w.sci("S_RANK3")));
	CHECK(told == 1);
	// SP costs
	PlayerScience sp;
	sp.bind(&w.men, SpellGameMode{ false, false }, nullptr);
	sp.reset();
	CHECK(sp.attemptToPurchaseScience(w.sci("S_HEAL")));
	CHECK(sp.getSciencePurchasePoints() == 1);
}

TEST_CASE("SPELL-1 PlayerScience: the state is hashed")
{
	World w;
	auto hashOf = [](const PlayerScience &p) {
		StateHasher h;
		p.crc(h);
		return h.value();
	};
	PlayerScience a, b;
	a.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	b.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	a.reset();
	b.reset();
	CHECK(hashOf(a) == hashOf(b));
	b.addSkillPoints(1.0f, true);
	CHECK(hashOf(a) != hashOf(b));
	a.addSkillPoints(1.0f, true);
	CHECK(hashOf(a) == hashOf(b));
	b.attemptToPurchaseScience(w.sci("S_HEAL"));
	CHECK(hashOf(a) != hashOf(b));
	a.attemptToPurchaseScience(w.sci("S_HEAL"));
	CHECK(hashOf(a) == hashOf(b));
	a.addSciencePurchasePoints(1);
	CHECK(hashOf(a) != hashOf(b));
}

TEST_CASE("SPELL-1 PlayerScience: point arithmetic wraps like the 32-bit registers (Sol review r1: no signed overflow)")
{
	World w;
	PlayerScience p;
	p.bind(&w.men, SpellGameMode{ true, false }, nullptr);
	p.reset();
	REQUIRE(p.getSciencePurchasePoints() == 5);
	p.addSciencePurchasePoints(2147483647); // 5 + INT_MAX wraps negative, then the clamp (RW 0x78276B) makes it 0
	CHECK(p.getSciencePurchasePoints() == 0);
	p.addSciencePurchasePoints(2147483647);
	CHECK(p.getSciencePurchasePoints() == 2147483647);
	p.addSciencePurchasePoints(1); // wraps to INT_MIN, clamped
	CHECK(p.getSciencePurchasePoints() == 0);
	p.addSciencePurchasePoints(-2147483647 - 1);
	CHECK(p.getSciencePurchasePoints() == 0);
	// a cost of INT_MIN: the negation stays INT_MIN (neg), the purchase test passes (cost != 0, cost <= points) and the sum is clamped
	REQUIRE(initest::loadError(w.fx.env, "m.ini", "Science S_MIN\n  SciencePurchasePointCostMP = -2147483648\nEnd\n") == "");
	p.addSciencePurchasePoints(5);
	CHECK(p.attemptToPurchaseScience(w.sci("S_MIN")));
	CHECK(p.getSciencePurchasePoints() == 0);
	CHECK(p.hasScience(w.sci("S_MIN")));
	// rank 1 granting 0xFFFFFFFF: the reset's add is not clamped (RW 0x7827BD), so the points are -1; a rank-up's add is clamped
	REQUIRE(initest::loadError(w.fx.env, "o.ini", "Rank 1\n  SciencePurchasePointsGranted = 4294967295\nEnd\nRank 2\n  SciencePurchasePointsGranted = 4294967295\nEnd\n",
				INI_LOAD_CREATE_OVERRIDES) == "");
	p.reset();
	CHECK(p.getSciencePurchasePoints() == -1);
	CHECK(p.setRankLevel(2));
	CHECK(p.getSciencePurchasePoints() == 0); // -1 + -1 = -2, clamped
}
