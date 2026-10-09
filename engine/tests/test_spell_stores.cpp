// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-1: the Science, Rank and SpecialPower blocks (Common/Science.h, GameLogic/RankInfo.h, Common/SpecialPower.h). Synthetic cases pin each
// parser rule of the binary; the retail case loads the pure 2.01 install and checks the counts and sample values against the INI text.

#include "doctest.h"
#include "IniTestUtil.h"
#include "StartTestUtil.h"

#include "Common/INIException.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SpellStores.h"
#include "GameLogic/SpellStops.h"

#include <algorithm>

#include <string>

namespace
{
struct SpellWorld
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	SpellStores stores;
	SpellWorld()
		: stores(keys)
	{
		keys.init();
		stores.install();
		SpellStores::registerBlocks(fx.env.blocks);
	}
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE) { return initest::loadError(fx.env, "t.ini", text, type); }
	ScienceType sci(const char *name) const { return stores.sciences().getScienceFromInternalName(name); }
};

bool contains(const std::string &s, const char *part)
{
	return s.find(part) != std::string::npos;
}

struct Owner : ScienceOwner
{
	ScienceVec have;
	int points = 0;
	bool hasScience(ScienceType st) const override
	{
		for (ScienceType h : have)
		{
			if (h == st)
			{
				return true;
			}
		}
		return false;
	}
	int getSciencePurchasePoints() const override { return points; }
};

const char *kBase = "Science A\n  SciencePurchasePointCost = 0\nEnd\n"
					"Science B\nEnd\n"
					"Science C\nEnd\n";
} // namespace

TEST_CASE("SPELL-1 Science: fields, defaults and the MP / SP cost (RW 0x5FF7DA, 0x5FEC64)")
{
	SpellWorld w;
	REQUIRE(w.load(std::string(kBase) + "Science D\n  PrerequisiteSciences = A B OR C\n  SciencePurchasePointCost = 7\n  SciencePurchasePointCostMP = 3\n  IsGrantable = No\n  DisplayName = SCIENCE:D\nEnd\n") == "");
	const ScienceStore &s = w.stores.sciences();
	CHECK(s.size() == 4);
	const ScienceInfo *b = s.findScienceInfo(w.sci("B"));
	REQUIRE(b);
	CHECK(b->m_grantable); // RW 0x5FF70C: IsGrantable defaults to true
	CHECK(b->m_prereqSciences.empty());
	const ScienceInfo *d = s.findScienceInfo(w.sci("D"));
	REQUIRE(d);
	REQUIRE(d->m_prereqSciences.size() == 2);
	CHECK(d->m_prereqSciences[0] == ScienceVec{ w.sci("A"), w.sci("B") });
	CHECK(d->m_prereqSciences[1] == ScienceVec{ w.sci("C") });
	CHECK(s.getSciencePurchaseCost(w.sci("D"), false) == 7);
	CHECK(s.getSciencePurchaseCost(w.sci("D"), true) == 3);
	CHECK(s.getSciencePurchaseCost(w.sci("B"), true) == 0);
	CHECK_FALSE(s.isScienceGrantable(w.sci("D")));
	CHECK(d->m_name == "SCIENCE:D");
}

TEST_CASE("SPELL-1 Science: the PrerequisiteSciences grammar of RW 0x73BCBF")
{
	SpellWorld w;
	REQUIRE(w.load(kBase) == "");
	struct Case
	{
		const char *text;
		size_t groups;
	} cases[] = {
		{ "None", 0 }, { "A", 1 }, { "A OR B", 2 }, { "A or B", 2 }, { "A OR", 0 }, { "A OR OR B", 0 }, { "A B None", 0 }, { "NONE A", 0 }, { "OR A", 0 },
	};
	int n = 0;
	for (const Case &c : cases)
	{
		const std::string name = "P" + std::to_string(n++);
		INFO(c.text);
		REQUIRE(w.load("Science " + name + "\n  PrerequisiteSciences = " + c.text + "\nEnd\n") == "");
		const ScienceInfo *si = w.stores.sciences().findScienceInfo(w.sci(name.c_str()));
		REQUIRE(si);
		CHECK(si->m_prereqSciences.size() == c.groups);
	}
	CHECK(contains(w.load("Science Q\n  PrerequisiteSciences = A Bogus\nEnd\n"), "Science name Bogus not known! (Did you define it in Science.ini?)"));
}

TEST_CASE("SPELL-1 Science: duplicates, map.ini overrides and the load type 5 replacement")
{
	SpellWorld w;
	REQUIRE(w.load(kBase) == "");
	CHECK(contains(w.load("Science B\nEnd\n"), "duplicate science B!"));
	// load type 2: the override copies the final override and is found in its place
	REQUIRE(w.load("Science B\n  SciencePurchasePointCostMP = 9\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == "");
	CHECK(w.stores.sciences().size() == 3);
	CHECK(w.stores.sciences().getSciencePurchaseCost(w.sci("B"), true) == 9);
	REQUIRE(w.load("Science B\n  SciencePurchasePointCost = 4\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == "");
	CHECK(w.stores.sciences().getSciencePurchaseCost(w.sci("B"), true) == 9); // copied from the previous override
	CHECK(w.stores.sciences().getSciencePurchaseCost(w.sci("B"), false) == 4);
	// a new science in map.ini is itself an override: it goes with the reset
	REQUIRE(w.load("Science NEW\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == "");
	CHECK(w.stores.sciences().size() == 4);
	w.stores.resetOverrides();
	CHECK(w.stores.sciences().size() == 3);
	CHECK(w.stores.sciences().getSciencePurchaseCost(w.sci("B"), true) == 0);
	CHECK(w.sci("NEW") == SCIENCE_INVALID);
	// load type 5: the old one is retired, a fresh one (defaults, not a copy) takes its place at the end
	REQUIRE(w.load("Science A\n  SciencePurchasePointCostMP = 2\nEnd\n", INI_LOAD_RELOAD) == "");
	CHECK(w.stores.sciences().replacedCount() == 1);
	const auto list = w.stores.sciences().sciences();
	REQUIRE(list.size() == 3);
	CHECK(list.back()->m_science == w.sci("A"));
	CHECK(list.back()->m_replaceFlag == 0);
}

TEST_CASE("SPELL-1 Science: the prerequisite and purchase tests (RW 0x5FED05, 0x5FED5B)")
{
	SpellWorld w;
	REQUIRE(w.load(std::string(kBase) + "Science D\n  PrerequisiteSciences = A B OR C\n  SciencePurchasePointCostMP = 3\nEnd\n") == "");
	const ScienceStore &s = w.stores.sciences();
	Owner p;
	CHECK(s.playerHasPrereqsForScience(p, w.sci("B")));  // no group: true (RotWK; BFME1's decompile says false)
	CHECK_FALSE(s.playerHasPrereqsForScience(p, w.sci("D")));
	p.have = { w.sci("A") };
	CHECK_FALSE(s.playerHasPrereqsForScience(p, w.sci("D")));
	p.have = { w.sci("A"), w.sci("B") };
	CHECK(s.playerHasPrereqsForScience(p, w.sci("D")));
	p.have = { w.sci("C") };
	CHECK(s.playerHasPrereqsForScience(p, w.sci("D")));
	CHECK_FALSE(s.playerHasRootPrereqsAndCanPurchase(p, w.sci("D"), true));
	p.points = 3;
	CHECK(s.playerHasRootPrereqsAndCanPurchase(p, w.sci("D"), true));
	CHECK_FALSE(s.playerHasPrereqsForScience(p, 123456)); // unknown science
}

TEST_CASE("SPELL-1 Rank: monotonic ranks, per-side counts and overrides (RW 0x5FFCAA, 0x5FFA44)")
{
	SpellWorld w;
	REQUIRE(w.load(std::string(kBase) + "Rank 1\n  SkillPointsNeededDefault = 0\n  SciencePurchasePointsGranted = 5\nEnd\n"
										 "Rank 2\n  SkillPointsNeededDefault = 60\n  SkillPointsNeededCampaign = 100\n  SkillPointsNeededMordor = 40\n  SciencesGranted = A B\n  SciencePurchasePointsGranted = 1\nEnd\n")
		  == "");
	const RankInfoStore &r = w.stores.ranks();
	REQUIRE(r.getRankLevelCount() == 2);
	CHECK(r.getRankInfo(0) == nullptr);
	CHECK(r.getRankInfo(3) == nullptr);
	const PlayerRankInfo *r2 = r.getRankInfo(2);
	REQUIRE(r2);
	CHECK(r2->getSkillPointsNeeded("Mordor", false) == 40);
	CHECK(r2->getSkillPointsNeeded("Men", false) == 60);    // -1 column: the default
	CHECK(r2->getSkillPointsNeeded("mordor", false) == 60); // the side compare is case-sensitive
	CHECK(r2->getSkillPointsNeeded("Mordor", true) == 100); // campaign
	CHECK(r.getRankInfo(1)->getSkillPointsNeeded("Mordor", true) == 0); // campaign -1: the default
	CHECK(r2->m_sciencesGranted == ScienceVec{ w.sci("A"), w.sci("B") });
	CHECK(r2->m_sciencePurchasePointsGranted == 1u);
	CHECK(contains(w.load("Rank 4\nEnd\n"), "Ranks must increase monotonically"));
	CHECK(contains(w.load("Rank 2\nEnd\n"), "Ranks must increase monotonically"));
	CHECK(contains(w.load("Rank 3\nEnd\n", INI_LOAD_CREATE_OVERRIDES), "Rank not found in map.ini"));
	REQUIRE(w.load("Rank 2\n  SkillPointsNeededDefault = 10\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == "");
	CHECK(r.getRankInfo(2)->getSkillPointsNeeded("Men", false) == 10);
	CHECK(r.getRankInfo(2)->getSkillPointsNeeded("Mordor", false) == 40); // copied
	w.stores.resetOverrides();
	CHECK(r.getRankInfo(2)->getSkillPointsNeeded("Men", false) == 60);
}

TEST_CASE("SPELL-1 SpecialPower: fields, ids, DefaultSpecialPower and duplicates (RW 0x7B212F)")
{
	SpellWorld w;
	REQUIRE(w.load(std::string(kBase) + "SpecialPower P1\n  Enum = SPECIAL_SPELL_BOOK_HEAL\n  ReloadTime = 240000\n  RequiredSciences = A\n  Flags = NEEDS_TARGET LIMIT_DISTANCE\n"
										 "  RadiusCursorRadius = 120\n  SharedSyncedTimer = Yes\n  PreventActivationConditions = HIDDEN\nEnd\n")
		  == "");
	const SpecialPowerStore &s = w.stores.specialPowers();
	const SpecialPowerTemplate *p1 = s.findSpecialPowerTemplate("P1");
	REQUIRE(p1);
	CHECK(p1->getID() == 1u);
	CHECK(std::string(SpecialPowerStore::specialPowerTypeNames()[p1->getSpecialPowerType()]) == "SPECIAL_SPELL_BOOK_HEAL");
	CHECK(p1->getReloadTime() == 1200u); // 240 s at 5 frames per second
	CHECK(p1->getRequiredSciences() == ScienceVec{ w.sci("A") });
	CHECK(p1->getFlags() == (SPF_NEEDS_TARGET | SPF_LIMIT_DISTANCE));
	CHECK(p1->isSharedNSync());
	CHECK(p1->m_detectionTime == 50u);
	CHECK(p1->m_preventActivationConditions != std::array<std::uint32_t, 4>{});
	CHECK(s.findSpecialPowerTemplate("p1") == nullptr); // case-sensitive
	CHECK(contains(w.load("SpecialPower P1\nEnd\n"), "Special power 'P1' already defined"));
	CHECK(contains(w.load("SpecialPower P1\nEnd\n", INI_LOAD_RELOAD), "Special power 'P1' already defined"));
	// a DefaultSpecialPower block seeds every LATER template
	REQUIRE(w.load("SpecialPower DefaultSpecialPower\n  ReloadTime = 1000\n  RadiusCursorRadius = 7\nEnd\nSpecialPower P2\n  RadiusCursorRadius = 9\nEnd\n") == "");
	const SpecialPowerTemplate *p2 = s.findSpecialPowerTemplate("P2");
	REQUIRE(p2);
	CHECK(p2->getID() == 3u);
	CHECK(p2->getName() == "P2");
	CHECK(p2->getReloadTime() == 5u);
	CHECK(p2->getRadiusCursorRadius() == 9.0f);
	CHECK(s.findSpecialPowerTemplate("P1")->getReloadTime() == 1200u);
	// map.ini override keeps the id
	REQUIRE(w.load("SpecialPower P1\n  ReloadTime = 2000\nEnd\n", INI_LOAD_CREATE_OVERRIDES) == "");
	CHECK(s.findSpecialPowerTemplate("P1")->getReloadTime() == 10u);
	CHECK(s.findSpecialPowerTemplate("P1")->getID() == 1u);
	CHECK(s.findSpecialPowerTemplateByID(1)->getReloadTime() == 10u);
	w.stores.resetOverrides();
	CHECK(s.findSpecialPowerTemplate("P1")->getReloadTime() == 1200u);
	CHECK(w.load("SpecialPower P8\n  Enum = SPECIAL_NOT_A_THING\nEnd\n") != "");
	CHECK(w.load("SpecialPower P7\n  RequiredSciences = Nope\nEnd\n") != "");
}

TEST_CASE("SPELL-1 retail: the Science, Rank and SpecialPower stores of the pure 2.01 install")
{
	OPENBFME_REQUIRE_START(shared);
	const SpellStores &st = shared->world->spellStores();
	for (const auto &e : shared->world->report().errors)
	{
		INFO(e.file << ": " << e.message);
		CHECK((e.message.find("Science") == std::string::npos && e.message.find("pecial power") == std::string::npos && e.message.find("Rank") == std::string::npos));
	}
	// data\ini\science.ini: 75 Science blocks; rank.ini: the Rank blocks; specialpower.ini 274 + createaherospecialpowers.ini 147
	CHECK(st.sciences().size() == 75);
	CHECK(st.ranks().getRankLevelCount() == 160);
	CHECK(st.specialPowers().getNumSpecialPowers() == 422);
	const ScienceStore &s = st.sciences();
	const ScienceType heal = s.getScienceFromInternalName("SCIENCE_Heal");
	REQUIRE(heal != SCIENCE_INVALID);
	CHECK(s.getSciencePurchaseCost(heal, true) == 5);
	CHECK(s.getSciencePurchaseCost(heal, false) == 5); // GOOD_RANK_1_COST
	CHECK(s.isScienceGrantable(heal));
	CHECK(s.findScienceInfo(heal)->m_prereqSciences.size() == 5); // SCIENCE_GOOD OR SCIENCE_MEN OR SCIENCE_ELVES OR SCIENCE_DWARVES OR SCIENCE_ARNOR
	const ScienceType men = s.getScienceFromInternalName("SCIENCE_MEN");
	REQUIRE(men != SCIENCE_INVALID);
	CHECK_FALSE(s.isScienceGrantable(men));
	// rank.ini: rank 1 grants 5 points; rank n needs PLAYER_SKILL_POINTS_DELTA_DEFAULT (60) * (n - 1); every later rank grants 1
	const PlayerRankInfo *r1 = st.ranks().getRankInfo(1);
	const PlayerRankInfo *r2 = st.ranks().getRankInfo(2);
	const PlayerRankInfo *r3 = st.ranks().getRankInfo(3);
	REQUIRE((r1 && r2 && r3));
	CHECK(r1->m_sciencePurchasePointsGranted == 5u);
	CHECK(r2->m_sciencePurchasePointsGranted == 1u);
	CHECK(r2->getSkillPointsNeeded("Men", false) == 60);
	CHECK(r3->getSkillPointsNeeded("Men", false) == 120);
	CHECK(r2->getSkillPointsNeeded("Men", true) == 100);
	// specialpower.ini SpellBookHeal
	const SpecialPowerTemplate *h = st.specialPowers().findSpecialPowerTemplate("SpellBookHeal");
	REQUIRE(h);
	CHECK(std::string(SpecialPowerStore::specialPowerTypeNames()[h->getSpecialPowerType()]) == "SPECIAL_SPELL_BOOK_HEAL");
	CHECK(h->getRequiredSciences() == ScienceVec{ heal });
	CHECK(h->getReloadTime() > 0u);
	MESSAGE("SPELL-1 retail: " << st.sciences().size() << " sciences, " << st.ranks().getRankLevelCount() << " ranks, " << st.specialPowers().getNumSpecialPowers()
							   << " special powers; SpellBookHeal reload " << h->getReloadTime() << " frames");
}

TEST_CASE("SPELL-1 stops: S-520 .. S-532 are reported, once each, by the lane and by the retail world")
{
	const std::vector<std::string> lines = SpellStops::lines();
	REQUIRE(lines.size() == 13);
	for (int i = 0; i < 13; ++i)
	{
		const std::string id = "[S-5" + std::to_string(20 + i) + "]";
		CHECK(lines[(size_t)i].rfind(id, 0) == 0);
	}
	OPENBFME_REQUIRE_START(shared);
	const std::vector<std::string> all = shared->world->acceptanceStops();
	for (const std::string &l : lines)
	{
		CHECK(std::count(all.begin(), all.end(), l) == 1);
	}
}

TEST_CASE("SPELL-1 SpecialPower Flags: names past bit 31 wrap like the retail shl (normal, + and - rows)")
{
	SpellWorld w;
	const char *const *names = SpecialPowerStore::specialPowerFlagNames();
	REQUIRE(std::string(names[31]) == "COMMAND_POINT_BONUS");
	REQUIRE(std::string(names[32]) == "CRUSHABLE_LEVEL");
	REQUIRE(std::string(names[34]) == "INVULNERABLE");
	REQUIRE(w.load("SpecialPower F1\n  Flags = COMMAND_POINT_BONUS CRUSHABLE_LEVEL INVULNERABLE\nEnd\n"
				   "SpecialPower F2\n  Flags = +INVULNERABLE\nEnd\n"
				   "SpecialPower F3\n  Flags = +NEEDS_TARGET +WATER_OK +NEEDS_OBJECT_FILTER -CRUSHABLE_LEVEL\nEnd\n"
				   "SpecialPower F4\n  Flags = WATER_OK\nEnd\n")
		  == "");
	const SpecialPowerStore &s = w.stores.specialPowers();
	CHECK(s.findSpecialPowerTemplate("F1")->getFlags() == (0x80000000u | 1u | 4u)); // 31, 32 -> 0, 34 -> 2
	CHECK(s.findSpecialPowerTemplate("F2")->getFlags() == 4u);                       // + 34 -> bit 2
	CHECK(s.findSpecialPowerTemplate("F3")->getFlags() == 6u);                       // bits 0 1 2, then - 32 clears bit 0
	CHECK(s.findSpecialPowerTemplate("F4")->getFlags() == 2u);
}
