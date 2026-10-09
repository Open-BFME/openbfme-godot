// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-1, retail: the seven skirmish factions of the pure 2.01 install start at rank 1 with 5 purchase points and their MP intrinsic science,
// see exactly the tier-1 sciences science.ini gives them, buy one through MSG_PURCHASE_SCIENCE on the dispatcher, and climb ranks on skill
// points. The expected lists were read off data\ini\science.ini independently of the engine (a group that is exactly SCIENCE_<SIDE>).

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/SpellCommands.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Faction
{
	const char *templateName;
	const char *intrinsic;
	std::vector<std::string> tier1; // SciencePurchasePointCostMP 5, prerequisite group == the side's MP science
};

const Faction kFactions[7] = {
	{ "FactionMen", "SCIENCE_MEN", { "SCIENCE_ElvenGifts", "SCIENCE_Heal", "SCIENCE_RallyingCallMP", "SCIENCE_Rebuild" } },
	{ "FactionElves", "SCIENCE_ELVES", { "SCIENCE_ElvenGifts", "SCIENCE_Heal", "SCIENCE_Farsight", "SCIENCE_RallyingCallMP" } },
	{ "FactionDwarves", "SCIENCE_DWARVES", { "SCIENCE_Heal", "SCIENCE_RallyingCallMP", "SCIENCE_Rebuild" } },
	{ "FactionIsengard", "SCIENCE_ISENGARD", { "SCIENCE_Crebain", "SCIENCE_PalantirVision", "SCIENCE_WarChant" } },
	{ "FactionMordor", "SCIENCE_MORDOR", { "SCIENCE_Taint", "SCIENCE_EyeofSauron", "SCIENCE_WarChant" } },
	{ "FactionWild", "SCIENCE_WILD", { "SCIENCE_Taint", "SCIENCE_CaveBats", "SCIENCE_WarChant" } },
	{ "FactionAngmar", "SCIENCE_ANGMAR", { "SCIENCE_Blight", "SCIENCE_ChillWind", "SCIENCE_WarChant" } },
};

struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	SpellCommands spell;
	std::vector<std::string> errors;

	explicit Game(starttest::Shared &s)
		: players(s.world->nameKeys(), s.world->playerTemplates(), teams)
	{
		SkirmishSetup setup;
		for (int i = 0; i < 7; ++i)
		{
			SkirmishPlayer p;
			p.name = std::string("player") + std::to_string(i);
			p.faction = kFactions[i].templateName;
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		errors = players.setupSkirmish(setup);
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		spell.registerHandlers(*dispatch);
	}
	Player *player(int i) { return players.findPlayerWithName(std::string("player") + std::to_string(i)); }
	void purchase(int issuer, int buyer, ScienceType st)
	{
		GameMessage m(MSG_PURCHASE_SCIENCE, issuer);
		m.appendIntegerArgument(buyer);
		m.appendIntegerArgument(st);
		dispatch->dispatch(m);
	}
	std::uint32_t hash()
	{
		StateHasher h;
		for (int i = 0; i < players.getPlayerCount(); ++i)
		{
			players.getNthPlayer(i)->crc(h);
		}
		return h.value();
	}
};
} // namespace

TEST_CASE("SPELL-1 retail: every skirmish faction starts at rank 1 with 5 points, buys a tier-1 science and ranks up")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	Game g(*shared);
	REQUIRE(g.errors.empty());
	for (int i = 0; i < 7; ++i)
	{
		const Faction &f = kFactions[i];
		INFO(f.templateName);
		Player *p = g.player(i);
		REQUIRE(p);
		PlayerScience &s = p->science();
		CHECK(s.mode().skirmishOrMultiplayer);
		CHECK(s.getRankLevel() == 1);
		CHECK(s.getSciencePurchasePoints() == 5);
		CHECK(s.getSkillPointsLevelUp() == 60);
		CHECK(s.getMaxRankLevel() == 150); // MaxLevelMP = PLAYER_MAX_PURCHASE_POINTS_DEFAULT 150 / PLAYER_PURCHASE_POINTS_GRANTED 1 (< 160 ranks)
		CHECK(s.sciences() == ScienceVec{ store.getScienceFromInternalName(f.intrinsic) });
		std::vector<std::string> purchasable;
		for (const ScienceInfo *si : store.sciences())
		{
			if (s.isCapableOfPurchasingScience(si->m_science))
			{
				purchasable.push_back(store.getInternalNameForScience(si->m_science));
			}
		}
		CHECK(purchasable == f.tier1);
		// buy the first one through the message (the buyer is argument 0; the issuer is the human player)
		const ScienceType first = store.getScienceFromInternalName(f.tier1.front());
		const int index = p->getPlayerIndex();
		g.purchase(1, index, first);
		CHECK(s.hasScience(first));
		CHECK(s.getSciencePurchasePoints() == 0);
		g.purchase(1, index, store.getScienceFromInternalName(f.tier1.back())); // nothing left to pay with
		CHECK_FALSE(s.hasScience(store.getScienceFromInternalName(f.tier1.back())));
		// skill points: rank 2 at 60 (every side's column is -1: the default), one more point; rank 3 at 120
		CHECK(s.addSkillPoints(60.0f, true));
		CHECK(s.getRankLevel() == 2);
		CHECK(s.getSciencePurchasePoints() == 1);
		CHECK(s.addSkillPoints(60.0f, true));
		CHECK(s.getRankLevel() == 3);
		CHECK(s.getSciencePurchasePoints() == 2);
	}
	CHECK(g.spell.purchases() == 7u);
	CHECK(g.spell.malformed() == 0u);
	// malformed and invalid messages
	GameMessage bad(MSG_PURCHASE_SCIENCE, 1);
	g.dispatch->dispatch(bad);
	CHECK(g.spell.malformed() == 1u);
	const std::uint32_t before = g.hash();
	g.purchase(1, 99, store.getScienceFromInternalName("SCIENCE_Heal")); // no such player
	g.purchase(1, 1, SCIENCE_INVALID);
	CHECK(g.hash() == before);
}

TEST_CASE("SPELL-1 retail: two identical purchase sequences hash alike, a different science does not")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	Game a(*shared), b(*shared), c(*shared);
	CHECK(a.hash() == b.hash());
	const int idx = a.player(0)->getPlayerIndex();
	a.purchase(1, idx, store.getScienceFromInternalName("SCIENCE_Heal"));
	b.purchase(1, idx, store.getScienceFromInternalName("SCIENCE_Heal"));
	c.purchase(1, idx, store.getScienceFromInternalName("SCIENCE_Rebuild"));
	CHECK(a.hash() == b.hash());
	CHECK(a.hash() != c.hash());
}
