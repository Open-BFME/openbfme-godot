// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-2, retail: the spell book screens' engine side. The store (RotWK AptSpellStore) of every faction lists its PurchaseScienceCommandSetMP
// buttons with the MP costs; a click makes an affordable science pending against the proxy's points, Reset clears, the close sends the purchases
// through the dispatcher; the cast bar shows the bought power ready, a press starts the targeting of a NEED_TARGET_POS power and the ground click casts
// it through MSG_DO_SPECIAL_POWER_AT_LOCATION; a no-target power casts at once.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/SpellBookUI.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
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
			testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
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

TEST_CASE("stops-b S-932 retail: buying passives emits CommandTrigger after the science purchase")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	UiGame g(*shared);
	for (int faction : { 5, 3 })
	{
		const bool scavenger = faction == 5;
		const char *scienceName = scavenger ? "SCIENCE_Scavenger" : "SCIENCE_FueltheFires";
		INFO(scienceName);
		Player &p = *g.player(faction);
		const ScienceType science = TheScienceStore->getScienceFromInternalName(scienceName);
		const ScienceInfo *info = TheScienceStore->findScienceInfo(science);
		REQUIRE(info);
		REQUIRE_FALSE(info->m_prereqSciences.empty());
		for (ScienceType prerequisite : info->m_prereqSciences.front())
		{
			if (!p.science().hasScience(prerequisite))
				REQUIRE(p.science().addScience(prerequisite));
		}
		p.science().addSciencePurchasePoints(100);
		SpellStoreModel store;
		std::string error;
		REQUIRE_MESSAGE(store.open(shared->world->commands(), *g.logic, p, &error), error);
		const SpellStoreModel::Button *purchase = nullptr;
		for (const auto &b : store.buttons())
			if (b.science == science)
				purchase = &b;
		REQUIRE(purchase);
		const std::string availability = std::string("science=") + scienceName + ", points=" + std::to_string(p.science().getSciencePurchasePoints())
			+ ", cost=" + std::to_string(purchase->cost) + ", prerequisites="
			+ std::to_string(TheScienceStore->playerHasPrereqsForScience(p.science(), science)) + ", disabled=" + std::to_string(p.isScienceDisabled(science))
			+ ", hidden=" + std::to_string(p.isScienceHidden(science));
		REQUIRE_MESSAGE(purchase->state == SpellStoreModel::STATE_AVAILABLE, availability);
		const int index = purchase->index;
		REQUIRE(purchase->button);
		REQUIRE(purchase->button->m_commandTrigger.size() == 1);
		const CommandButton *trigger = shared->world->commands().findCommandButton(purchase->button->m_commandTrigger.front());
		REQUIRE(trigger);
		REQUIRE(trigger->hasOption(COMMAND_OPTION_NONPRESSABLE));
		const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(trigger->m_specialPowerName);
		REQUIRE(power);
		Object *book = SpecialPowerModules::findSpellBookObject(*g.logic, p);
		REQUIRE(book);
		auto *sp = dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*book, power));
		REQUIRE(sp);
		const auto *scav = dynamic_cast<ScavengerSpecialPower *>(sp);
		REQUIRE((scavenger ? scav != nullptr : std::string(sp->getModuleClassName()) == "SpecialPowerModule"));
		CHECK(sp->triggers() == 0);
		const auto before = g.logic->computeStateHash();
		REQUIRE(store.click(index));
		CHECK_FALSE(store.click(index));
		store.reset();
		CHECK(store.close().empty());
		CHECK(g.logic->computeStateHash() == before);
		REQUIRE(store.open(shared->world->commands(), *g.logic, p, &error));
		REQUIRE(store.click(index));
		const auto messages = store.close();
		CHECK(g.logic->computeStateHash() == before);
		CHECK_FALSE(p.science().hasScience(science));
		CHECK(sp->triggers() == 0);
		CHECK(messages.size() == 2);
		REQUIRE_FALSE(messages.empty());
		CHECK(messages[0].getType() == MSG_PURCHASE_SCIENCE);
		if (messages.size() == 2)
		{
			const GameMessage &m = messages[1];
			CHECK(m.getType() == MSG_DO_SPELLBOOK_SPECIAL_POWER);
			CHECK(m.getPlayerIndex() == p.getPlayerIndex());
			REQUIRE(m.getArgument(0));
			REQUIRE(m.getArgument(1));
			REQUIRE(m.getArgument(2));
			CHECK(m.getArgument(0)->integer == (int)power->getID());
			CHECK(m.getArgument(1)->integer == (int)trigger->m_options);
			CHECK(m.getArgument(2)->type == ARGUMENTDATATYPE_OBJECTID);
			CHECK(m.getArgument(2)->objectID == INVALID_ID);
		}
		g.send(messages);
		CHECK(p.science().hasScience(science));
		CHECK(sp->triggers() == 1);
		if (scav)
		{
			const auto *data = dynamic_cast<const ScavengerSpecialPowerModuleData *>(sp->getModuleData());
			REQUIRE(data);
			REQUIRE(data->m_bountyPercent == 1.0f);
			CHECK(scav->active());
			CHECK(p.getBountyPercent() == data->m_bountyPercent);
		}
		REQUIRE(store.open(shared->world->commands(), *g.logic, p, &error));
		CHECK_FALSE(store.click(index));
		CHECK(store.close().empty());
		CHECK(sp->triggers() == 1);
	}
}

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

// lane DEVPORT-7: a CommandTrigger that names no button (a mod's typo) is reported by name; the purchase and the valid triggers still go out (retail
// would dereference the missing button in RW 0x940435 case 0x19)
TEST_CASE("stops-b S-932 retail: a CommandTrigger naming no button is reported, the purchase and the other triggers are sent")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	UiGame g(*shared);
	Player &p = *g.player(5);
	const ScienceType science = TheScienceStore->getScienceFromInternalName("SCIENCE_Scavenger");
	const ScienceInfo *info = TheScienceStore->findScienceInfo(science);
	REQUIRE(info);
	REQUIRE_FALSE(info->m_prereqSciences.empty());
	for (ScienceType prerequisite : info->m_prereqSciences.front())
	{
		if (!p.science().hasScience(prerequisite))
			REQUIRE(p.science().addScience(prerequisite));
	}
	p.science().addSciencePurchasePoints(100);
	SpellStoreModel store;
	std::string error;
	REQUIRE_MESSAGE(store.open(shared->world->commands(), *g.logic, p, &error), error);
	const SpellStoreModel::Button *purchase = nullptr;
	for (const auto &b : store.buttons())
		if (b.science == science)
			purchase = &b;
	REQUIRE(purchase);
	REQUIRE(purchase->button);
	auto &triggers = const_cast<CommandButton *>(purchase->button)->m_commandTrigger;
	REQUIRE(triggers.size() == 1);
	triggers.insert(triggers.begin(), "Command_DevPort7NoSuchButton");
	REQUIRE(store.click(purchase->index));
	CHECK(store.errors().empty());
	const auto messages = store.close();
	triggers.erase(triggers.begin()); // the shared world's button as it was
	REQUIRE(messages.size() == 2);
	CHECK(messages[0].getType() == MSG_PURCHASE_SCIENCE);
	CHECK(messages[1].getType() == MSG_DO_SPELLBOOK_SPECIAL_POWER);
	REQUIRE(store.errors().size() == 1);
	CHECK(store.errors()[0] == "spell store: CommandTrigger Command_DevPort7NoSuchButton of " + purchase->button->m_name + " names no special power button");
}
