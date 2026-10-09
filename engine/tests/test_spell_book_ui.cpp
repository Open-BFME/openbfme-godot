// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-2, retail: the spell book screens' engine side. The store (RotWK AptSpellStore) of every faction lists its PurchaseScienceCommandSetMP
// buttons with the MP costs; a click makes an affordable science pending against the proxy's points, Reset clears, the close sends the purchases
// through the dispatcher; the cast bar shows the bought power ready, a press starts the targeting of a NEED_TARGET_POS power and the ground click casts
// it through MSG_DO_SPECIAL_POWER_AT_LOCATION; a no-target power casts at once.

#include "doctest.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/SpellBookUI.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
const char *const kFactions[7] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
const char *const kCitadels[7] = { "MenFortressCitadel", "ElvenCitadel", "DwarvenFortressCitadel", "IsengardFortressCitadel", "MordorFortressCitadel",
	"WildFortressCitadel", "AngmarFortressCitadel" };

struct UiGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	spelltest::GameAI ai; // lane SPELL-2: the effect objects fire weapons (combat geometry)
	SpellCommands spell;
	starttest::Shared &s;

	explicit UiGame(starttest::Shared &shared)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		for (int i = 0; i < 7; ++i)
		{
			SkirmishPlayer p;
			p.name = "u" + std::to_string(i);
			p.faction = kFactions[i];
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		logic->random().seedRandom(5);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		ai.attach(shared, *logic);
		spell.registerHandlers(*dispatch);
		SpecialPowerModules::installScienceHooks(*logic);
		for (int i = 0; i < 7; ++i)
		{
			const ThingTemplate *t = s.world->things().findTemplate(kCitadels[i]);
			REQUIRE(t);
			Object *o = logic->newObject(t, player(i)->getDefaultTeam(), ObjectStatusMaskType{});
			Coord3D pos{ 1100.0f * (float)i + 500.0f, 300.0f, 0.0f };
			o->setPosition(&pos);
			o->friend_onBuildComplete();
			REQUIRE(SpecialPowerModules::createSpellBook(*logic, *player(i)));
		}
	}
	~UiGame() { logic->reset(); } // the objects leave the pathfinder while the AIWorld exists
	Player *player(int i) { return players.findPlayerWithName("u" + std::to_string(i)); }
	void send(const std::vector<GameMessage> &msgs)
	{
		for (const GameMessage &m : msgs)
		{
			dispatch->dispatch(m);
		}
	}
};
} // namespace

TEST_CASE("SPELL-2 retail: each faction's spell store lists its purchase buttons with MP costs; clicks are pending until the store closes")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	UiGame g(*shared);
	for (int i = 0; i < 7; ++i)
	{
		INFO(std::string(kFactions[i]));
		Player &p = *g.player(i);
		SpellStoreModel store;
		std::string err;
		REQUIRE_MESSAGE(store.open(shared->world->commands(), *g.logic, p, &err), err);
		CHECK(store.commandSetName().find("SpellStoreCommandSet") != std::string::npos);
		REQUIRE(!store.buttons().empty());
		CHECK(store.buttons().size() <= (size_t)SpellStoreModel::MAX_BUTTONS);
		int tier1 = 0;
		for (const SpellStoreModel::Button &b : store.buttons())
		{
			CHECK(b.science != SCIENCE_INVALID);
			CHECK(!b.image.empty());
			CHECK((b.cost == 5 || b.cost == 10 || b.cost == 15 || b.cost == 25));
			tier1 += b.cost == 5 ? 1 : 0;
		}
		CHECK(tier1 >= 3);
		// the rank-1 points (5): the tier-1 buttons are available, every dearer one locked
		CHECK(store.points() == p.science().getSciencePurchasePoints());
		for (const SpellStoreModel::Button &b : store.buttons())
		{
			CHECK((b.state == SpellStoreModel::STATE_AVAILABLE) == (b.cost <= store.points() && b.cost == 5));
		}
		p.science().addSciencePurchasePoints(12 - p.science().getSciencePurchasePoints());
		store.refresh();
		int firstAvailable = -1, tier2 = -1;
		for (const SpellStoreModel::Button &b : store.buttons())
		{
			if (b.state == SpellStoreModel::STATE_AVAILABLE && firstAvailable < 0)
			{
				firstAvailable = b.index;
			}
			if (b.cost == 10 && tier2 < 0)
			{
				tier2 = b.index;
			}
		}
		REQUIRE(firstAvailable >= 0);
		REQUIRE(tier2 >= 0);
		CHECK(store.points() == 12);
		CHECK(store.click(firstAvailable));
		CHECK(store.points() == 7);
		CHECK_FALSE(store.click(firstAvailable)); // pending already (the proxy owns it)
		// a tier-2 power needs a tier-1 prerequisite and 10 points: 7 are left
		CHECK_FALSE(store.click(tier2));
		store.reset();
		CHECK(store.points() == 12);
		CHECK(store.pending().empty());
		CHECK(store.click(firstAvailable));
		const ScienceType bought = store.pending().front();
		const std::vector<GameMessage> msgs = store.close();
		REQUIRE(msgs.size() == 1u);
		CHECK_FALSE(p.science().hasScience(bought));
		g.send(msgs);
		CHECK(p.science().hasScience(bought));
		CHECK(p.science().getSciencePurchasePoints() == 7);
		CHECK_FALSE(store.isOpen());
	}
}

TEST_CASE("SPELL-2 retail: the cast bar shows a bought power ready; a NEED_TARGET_POS press targets, the ground click casts; the timer runs")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	UiGame g(*shared);
	Player &men = *g.player(0);
	InGameSpellBookModel bar;
	REQUIRE(bar.refresh(shared->world->commands(), *g.logic, men));
	REQUIRE(!bar.buttons().empty());
	const InGameSpellBookModel::Button *heal = nullptr;
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		CHECK_FALSE(b.owned);
		if (b.power && b.power->getName() == "SpellBookHeal")
		{
			heal = &b;
		}
	}
	REQUIRE(heal);
	const int healIndex = heal->index;
	CHECK(heal->needsPosition);
	CHECK(heal->radius > 0.0f);
	std::vector<GameMessage> out;
	CHECK_FALSE(bar.press(healIndex, men.getPlayerIndex(), out)); // not owned
	men.science().grantScience(TheScienceStore->getScienceFromInternalName("SCIENCE_Heal"));
	REQUIRE(bar.refresh(shared->world->commands(), *g.logic, men));
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		if (b.index == healIndex)
		{
			CHECK(b.owned);
			CHECK(b.ready);
			CHECK(b.percent == 1.0f);
		}
	}
	CHECK(bar.press(healIndex, men.getPlayerIndex(), out));
	CHECK(out.empty());
	REQUIRE(bar.targeting());
	CHECK(bar.targetButton()->radiusCursor == "HealRadiusCursor");
	const unsigned long long casts = g.spell.casts();
	CHECK(bar.clickWorld(Coord3D{ 600.0f, 600.0f, 0.0f }, men.getPlayerIndex(), out));
	CHECK_FALSE(bar.targeting());
	REQUIRE(out.size() == 1u);
	g.send(out);
	CHECK(g.spell.casts() == casts + 1);
	REQUIRE(bar.refresh(shared->world->commands(), *g.logic, men));
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		if (b.index == healIndex)
		{
			CHECK_FALSE(b.ready);
			CHECK(b.percent == 0.0f);
		}
	}
	for (int f = 0; f < 450; ++f)
	{
		g.logic->runLogicFrame();
	}
	REQUIRE(bar.refresh(shared->world->commands(), *g.logic, men));
	for (const InGameSpellBookModel::Button &b : bar.buttons())
	{
		if (b.index == healIndex)
		{
			CHECK(b.percent > 0.45f);
			CHECK(b.percent < 0.55f);
			MESSAGE("SPELL-2 cast bar: Heal recharge at " << b.percent << " after 450 of 900 frames");
		}
	}
	// a no-target power (Wild's Scavenger) casts at once
	Player &wild = *g.player(5);
	InGameSpellBookModel wbar;
	wild.science().grantScience(TheScienceStore->getScienceFromInternalName("SCIENCE_Scavenger"));
	REQUIRE(wbar.refresh(shared->world->commands(), *g.logic, wild));
	int scav = -1;
	for (const InGameSpellBookModel::Button &b : wbar.buttons())
	{
		if (b.power && b.power->getName() == "SpellBookScavenger")
		{
			scav = b.index;
			CHECK_FALSE(b.needsPosition);
		}
	}
	REQUIRE(scav >= 0);
	std::vector<GameMessage> now;
	CHECK(wbar.press(scav, wild.getPlayerIndex(), now));
	CHECK_FALSE(wbar.targeting());
	REQUIRE(now.size() == 1u);
	g.send(now);
	CHECK(wild.getBountyPercent() > 0.0f);
}
