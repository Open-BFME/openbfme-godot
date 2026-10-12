// OpenBFME tests for lane BUGS-1: the skirmish AI's HeroBuildOrder names have name keys of their own (DECOMP-2's review: the Men computer requested
// RohanFrodo and stalled). The unit builder keys its factory map by name (RW 0x9A0838 for the army members, RW 0x9A08E8 .. 0x9A0975 for the heroes); the
// port created keys only for the members, so every hero name looked up NAMEKEY_INVALID, the citadel the Men heroes registered under that one key was
// RohanFrodo's factory too, and FactionMen cannot recruit Frodo (BuildableHeroesMP has no RohanFrodo): canMakeUnit answered 1 for ever.
#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct Loader
{
	NameKeyGenerator keys;
	SkirmishAIStore store{ keys };
	INIEnvironment env;
	Loader()
	{
		keys.init();
		store.registerBlocks(env.blocks);
	}
	void load(const std::string &text)
	{
		INI ini(env);
		ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
	}
};
} // namespace

TEST_CASE("bugs1: an ArmyDefinition's HeroBuildOrder names get their own keys at load; a name without a key is an error, not NAMEKEY_INVALID")
{
	Loader l;
	l.load("ArmyDefinition A\n Side = Men\n ArmyMemberDefinition X\n  Unit = U1\n  PercentageOfArmyPhase1 = 100\n End\n HeroBuildOrder = H1 H2 U1\nEnd\n");
	const NameKeyType u1 = l.store.keyOf("U1"), h1 = l.store.keyOf("H1"), h2 = l.store.keyOf("H2");
	CHECK(u1 != NAMEKEY_INVALID);
	CHECK(h1 != NAMEKEY_INVALID);
	CHECK(h2 != NAMEKEY_INVALID);
	CHECK(h1 != h2);
	CHECK(h1 != u1);
	CHECK(h1 == l.keys.findKey("H1"));
	CHECK_THROWS_AS(l.store.keyOf("H3"), std::logic_error);
}

TEST_CASE("bugs1 retail: every HeroBuildOrder name has a key of its own; MenOfTheWestArmy orders RohanFrodo, whom FactionMen cannot recruit")
{
	OPENBFME_REQUIRE_START(s);
	const SkirmishAIStore &ai = s->world->skirmishAI();
	std::set<NameKeyType> keys;
	size_t names = 0;
	for (const char *side : { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor" })
	{
		INFO(side);
		const ArmyDefinition *army = ai.findArmy(side);
		REQUIRE(army != nullptr);
		CHECK(!army->heroBuildOrder.empty());
		std::set<std::string> distinct(army->heroBuildOrder.begin(), army->heroBuildOrder.end()); // the order repeats some (ElvenLegolas three times)
		for (const std::string &n : distinct)
		{
			const NameKeyType k = ai.keyOf(n);
			CHECK(k != NAMEKEY_INVALID);
			keys.insert(k);
			++names;
		}
	}
	// the ring heroes are in several armies' orders: a name has one key wherever it appears
	std::set<std::string> all;
	for (const char *side : { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor" })
	{
		const ArmyDefinition *army = ai.findArmy(side);
		all.insert(army->heroBuildOrder.begin(), army->heroBuildOrder.end());
	}
	CHECK(keys.size() == all.size());
	CHECK(names >= all.size());
	// the data fact behind the stall (RotWK 2.01 data\ini\playertemplate.ini overrides the default one): Frodo is only FactionTutorial's
	const ArmyDefinition *men = ai.findArmy("Men");
	CHECK(std::find(men->heroBuildOrder.begin(), men->heroBuildOrder.end(), "RohanFrodo") != men->heroBuildOrder.end());
	const PlayerTemplate *fm = s->world->playerTemplates().findPlayerTemplate("FactionMen");
	REQUIRE(fm != nullptr);
	CHECK(std::find(fm->m_buildableHeroesMP.begin(), fm->m_buildableHeroesMP.end(), "RohanFrodo") == fm->m_buildableHeroesMP.end());
	CHECK(std::find(fm->m_buildableHeroesMP.begin(), fm->m_buildableHeroesMP.end(), "RohanEowyn") != fm->m_buildableHeroesMP.end());
}
