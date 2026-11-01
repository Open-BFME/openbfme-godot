// OpenBFME. GPL-3.0.
//
// lane B2SPELL-1 (the owner, 10-30: "in the first mission for good campaign you cannot select the spellbook powers, they are blank"): the logic side of
// MAP GOOD Rivendell's powers. The client side (the store movie's layout, the cause) is test_ownerfix1_input.cpp's "b2spell1 campaign powers played".
// Retail facts: PlyrElves is FactionElves, SpellBook GoodSpellBook, PurchaseScienceCommandSet GoodSpellStoreCommandSet (data\ini\playertemplate.ini):
// 16 powers, the first four GOOD_RANK_1_COST 5 (science.ini); a campaign starts at rank 1 with 5 points (Rank.ini)

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/Module/SpecialPowerModules.h"

TEST_CASE("b2spell1 MAP GOOD Rivendell: the local player has GoodSpellBook, 5 points and GoodSpellStoreCommandSet's 16 powers, the first four for sale")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.loadCampaignMission(*s, "GOOD_CAMPAIGN", 0);
	m.step(5);
	Player *p = m.logic().players().getLocalPlayer();
	REQUIRE(p != nullptr);
	CHECK(p->getPlayerName() == "PlyrElves");
	REQUIRE(p->getPlayerTemplate() != nullptr);
	CHECK(p->getPlayerTemplate()->getName() == "FactionElves");
	PlayerScience &sc = p->science();
	CHECK(sc.mode().campaign);
	CHECK_FALSE(sc.mode().skirmishOrMultiplayer);
	CHECK(sc.getRankLevel() == 1);
	CHECK(sc.getSciencePurchasePoints() == 5);
	Object *book = SpecialPowerModules::findSpellBookObject(m.logic(), *p);
	REQUIRE(book != nullptr);
	CHECK(book->getTemplate()->getName() == "GoodSpellBook");
	SpellStoreModel store;
	std::string error;
	REQUIRE_MESSAGE(store.open(s->world->commands(), m.logic(), *p, &error), error);
	CHECK(store.commandSetName() == "GoodSpellStoreCommandSet");
	REQUIRE(store.buttons().size() == 16u);
	for (const SpellStoreModel::Button &b : store.buttons())
	{
		CHECK_FALSE(b.image.empty());
		CHECK(b.state == (b.index < 4 ? SpellStoreModel::STATE_AVAILABLE : SpellStoreModel::STATE_LOCKED)); // tolerance: exact per button; the < only picks the expected state (the first four powers are for sale)
	}
	CHECK(store.buttons()[2].scienceName == "SCIENCE_Heal");
	CHECK(store.buttons()[2].cost == 5);
	InGameSpellBookModel bar;
	REQUIRE(bar.refresh(s->world->commands(), m.logic(), *p));
	CHECK(bar.buttons().size() == 25u); // GoodSpellBookCommandSet; none owned before a purchase
}
