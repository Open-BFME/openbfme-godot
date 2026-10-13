// OpenBFME. PLAY-4: the Discord tester's "troops can be recruited while the structure is still being built" (v0.3.0-preview.6), with the retail data.
// What RotWK does (GettingBuiltBehavior.h / Construction.h, lane PLAY-4):
//   * the dozer makes its structure PENDING_CONSTRUCTION (RW 0x88D29D), so the structure's GettingBuiltBehavior disables its production when it is created
//     (RW 0x856434 -> production slot 0x60(1)): canMakeUnit answers FACTORY_IS_DISABLED and a queue order does nothing;
//   * Player::onStructureCreated (RW 0x6AAF3B) sets the structure's + 0x36 flag for a DOZER builder, so the control bar shows the under-construction context
//     (RW 0x71EE8A -> populateUnderConstruction RW 0x944EFB: only Command_CancelConstruction), not the command set;
//   * the completion (RW 0x6AA72B -> finishConstruction RW 0x8569B5) enables the production again; the flag clears with GettingBuiltBehavior's completion check.
// The tester's item 11 (construction and damaged animations missing): the structure's model states. A rising structure has the construction states and,
// although its body starts at 1 hit point (RW 0x85895D), not REALLYDAMAGED / DAMAGED: internalChangeHealth skips the visual change while the object is
// UNDER_CONSTRUCTION (RW 0x8C3290 -> vslot 0x94); once complete the damage states show (the models _D1 / _D2 of the INI's DAMAGED / REALLYDAMAGED).
// Needs the retail install (SKIPs loudly without it).

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

Object *firstOf(LiveGame &live, Player *player, const char *kind, bool needDozerAI)
{
	for (Object *o = live.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName(kind) && (!needDozerAI || dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface())))
		{
			return o;
		}
	}
	return nullptr;
}

// the objects of `t` the player has (made, alive)
int countOf(GameLogic &logic, const Player *player, const ThingTemplate &t)
{
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		n += (o->getControllingPlayer() == player && o->getTemplate()->getFinalOverride() == &t && !o->isEffectivelyDead()) ? 1 : 0;
	}
	return n;
}

// a real in-game HUD on the game (the control bar the player sees), as the HUD tests boot it
struct Hud
{
	MouseSettings mouse;
	std::unique_ptr<INIEnvironment> metaEnv;
	MetaMap meta;
	RecordingShellServices services;
	PinholeView view{ { 0, -400, 500 }, { 0, 0, 0 }, 1024, 768, 0.7f };
	std::unique_ptr<InGameHud> hud;
	Hud(starttest::Shared &s, LiveGame &live)
	{
		std::string error;
		REQUIRE_MESSAGE(MouseSettings::load(*s.mount->fs, mouse, &error), error);
		metaEnv = std::make_unique<INIEnvironment>();
		metaEnv->fileSystem = s.mount->fs.get();
		meta.registerBlocks(*metaEnv);
		REQUIRE_MESSAGE(meta.load(*metaEnv, "Data\\INI\\CommandMap.ini", &error), error);
		REQUIRE_MESSAGE(meta.load(*metaEnv, "CommandMap.ini", &error), error);
		InGameHud::Config cfg{ live, *s.world, *s.mount->fs, services, view, mouse, meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	// the names of the buttons the control bar shows for the selected object (the Palantir's and the side bar's)
	std::vector<std::string> buttonsOf(ObjectID id)
	{
		const auto ctx = hud->enterContext();
		hud->input().ui().selectObject(id);
		ControlBar &bar = hud->controlBar();
		bar.update();
		std::vector<std::string> out;
		for (const std::vector<ControlBarButton> *list : { &bar.palantirButtons(), &bar.sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button)
				{
					out.push_back(b.button->m_name);
				}
			}
		}
		return out;
	}
};

bool has(const Object &o, const char *condition)
{
	return o.testModelCondition(Construction::modelConditionIndex(condition));
}

// a hit of `amount` (the damage pipeline: armour, the body's damage state, its model conditions)
void hit(Object &o, float amount)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_amount = amount;
	o.getBodyModule()->attemptDamage(info);
}

GameMessage queueMsg(int player, const ThingTemplate &t)
{
	// MSG_QUEUE_UNIT_CREATE, RotWK layout (RW 0x77A764): { byte fromBuildIndex, int templateId, int value30, byte batch, byte secondary }
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}
} // namespace

TEST_CASE("play4 construction retail: a barracks a builder puts up recruits nothing and shows only the cancel button until it is complete")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player);
	player->getMoney()->deposit(20000, false);
	Object *dozerObj = firstOf(live, player, "DOZER", true);
	Object *fortress = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		fortress = (o && o->getControllingPlayer() == player) ? o : fortress;
	}
	REQUIRE(dozerObj);
	REQUIRE(fortress);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(dozerObj->getAIUpdateInterface());
	const ThingTemplate *barracks = logic.things().findTemplate("GondorBarracks")->getFinalOverride();
	const ThingTemplate *soldiers = logic.things().findTemplate("GondorFighterHorde")->getFinalOverride();
	Coord3D site;
	bool found = false;
	for (float r = 300.0f; r <= 600.0f && !found; r += 40.0f)
	{
		for (int k = 0; k < 24 && !found; ++k)
		{
			Coord3D c = *fortress->getPosition();
			c.x += (float)(r * std::cos(k * 0.2617993877991494));
			c.y += (float)(r * std::sin(k * 0.2617993877991494));
			c.z = logic.getGroundHeight(c.x, c.y);
			found = dozer->siteIsLegal(*barracks, c, 0.0f, *player);
			site = c;
		}
	}
	REQUIRE(found);
	const int pi = player->getPlayerIndex();
	live.commands().append(selectMsg(pi, dozerObj->getID()));
	GameMessage order(MSG_DOZER_CONSTRUCT, pi);
	order.appendIntegerArgument((int)barracks->getTemplateID());
	order.appendLocationArgument(site);
	order.appendRealArgument(0.0f);
	live.commands().append(order);
	logic.runLogicFrame();
	Object *structure = logic.findObjectByID(dozer->unplacedStructure());
	REQUIRE(structure);
	const ObjectID id = structure->getID();
	const GettingBuiltBehavior *gb = dynamic_cast<const GettingBuiltBehavior *>(structure->findModule("GettingBuiltBehavior"));
	REQUIRE(gb);
	ProductionUpdateInterface *pu = structure->getProductionUpdate();
	REQUIRE(pu);
	// the order: PENDING_CONSTRUCTION at creation disabled the production; the dozer builder set + 0x36
	CHECK(pu->isFactoryDisabled());
	CHECK(gb->forceComplete());
	CHECK(ControlBar::isUnderConstructionContext(*structure));
	CHECK(BuildAssistant::canMakeUnit(*structure, soldiers, -1) == CANMAKE_FACTORY_IS_DISABLED);
	// the control bar the player sees for it: the cancel button and nothing else (both lists)
	Hud hud(*s, live);
	{
		const std::vector<std::string> shown = hud.buttonsOf(id);
		CHECK(shown == std::vector<std::string>{ "Command_CancelConstruction", "Command_CancelConstruction" });
	}
	CHECK(countOf(logic, player, *soldiers) == 0);
	// a queue order while it rises does nothing
	int frames = 0;
	bool sawPlacedRising = false;
	bool sawConstructionStates = false, sawDamageWhileRising = false, madeWhileRising = false;
	while (structure->isUnderConstruction() && frames < 5 * 600)
	{
		sawConstructionStates = sawConstructionStates || (has(*structure, "PARTIALLY_CONSTRUCTED") && has(*structure, "ACTIVELY_BEING_CONSTRUCTED"));
		sawDamageWhileRising = sawDamageWhileRising || has(*structure, "DAMAGED") || has(*structure, "REALLYDAMAGED");
		madeWhileRising = madeWhileRising || countOf(logic, player, *soldiers) != 0;
		if (frames == 40)
		{
			const std::uint32_t money = player->getMoney()->countMoney();
			live.commands().append(selectMsg(pi, id));
			live.commands().append(queueMsg(pi, *soldiers));
			logic.runLogicFrame();
			++frames;
			CHECK(pu->firstProduction() == nullptr);
			CHECK(player->getMoney()->countMoney() == money);
			sawPlacedRising = dozer->unplacedStructure() == INVALID_ID; // the dozer placed it (PENDING cleared) and it still rises
			const bool pendingAfterPlacement = structure->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION")) && sawPlacedRising;
			CHECK_FALSE(pendingAfterPlacement);
			CHECK(ControlBar::isUnderConstructionContext(*structure));
			continue;
		}
		logic.runLogicFrame();
		++frames;
	}
	MESSAGE("GondorBarracks complete after " << frames << " frames");
	REQUIRE_FALSE(structure->isUnderConstruction());
	CHECK(sawConstructionStates);
	CHECK_FALSE(madeWhileRising); // no GondorFighterHorde at any frame while it rose
	CHECK_FALSE(sawDamageWhileRising); // item 11: RW 0x8C3290 (it rose from 1 hit point through the REALLYDAMAGED / DAMAGED health bands)
	CHECK(has(*structure, "CONSTRUCTION_COMPLETE"));
	// the completion enabled the production; GettingBuiltBehavior's completion check clears + 0x36 within its one-second sleep
	CHECK_FALSE(pu->isFactoryDisabled());
	for (int i = 0; i < 10; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK_FALSE(gb->forceComplete());
	CHECK_FALSE(ControlBar::isUnderConstructionContext(*structure));
	CHECK(BuildAssistant::canMakeUnit(*structure, soldiers, -1) == CANMAKE_OK);
	CHECK(countOf(logic, player, *soldiers) == 0); // the order given while it rose left nothing queued
	// complete: the bar shows the barracks' command set (its GondorFighterHorde button) and a queue order makes the horde
	{
		const std::vector<std::string> shown = hud.buttonsOf(id);
		CHECK(std::find(shown.begin(), shown.end(), "Command_CancelConstruction") == shown.end());
		bool soldiersButton = false;
		const auto ctx = hud.hud->enterContext();
		for (const ControlBarButton &b : hud.hud->controlBar().palantirButtons())
		{
			soldiersButton = soldiersButton || (b.button && b.button->getThingTemplate() && b.button->getThingTemplate()->getFinalOverride() == soldiers);
		}
		CHECK(soldiersButton);
	}
	live.commands().append(selectMsg(pi, id));
	live.commands().append(queueMsg(pi, *soldiers));
	logic.runLogicFrame();
	CHECK(pu->firstProduction() != nullptr);
	int made = 0;
	for (int f = 0; f < 3000 && made == 0; ++f)
	{
		logic.runLogicFrame();
		made = countOf(logic, player, *soldiers);
	}
	CHECK(made == 1);
}

TEST_CASE("play4 construction retail: a castle pad's tower rises in its build variation's construction states, then shows DAMAGED and REALLYDAMAGED")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player);
	Object *pad = nullptr;
	for (Object *o = logic.getFirstObject(); o && !pad; o = o->getNextObject())
	{
		pad = (o->getControllingPlayer() == player && o->getTemplate()->getName() == "MenFortressExpansionPadSide") ? o : nullptr;
	}
	REQUIRE(pad);
	const ThingTemplate *tower = logic.things().findTemplate("MenArrowTowerExpansion")->getFinalOverride(); // MenFortressExpansionPadSideCommandSet
	const int pi = player->getPlayerIndex();
	live.commands().append(selectMsg(pi, pad->getID()));
	GameMessage m(MSG_FOUNDATION_CONSTRUCT, pi);
	m.appendIntegerArgument((int)tower->getTemplateID());
	m.appendLocationArgument(*pad->getPosition());
	m.appendRealArgument(0.0f);
	live.commands().append(m);
	logic.runLogicFrame();
	Object *t = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		t = o->getTemplate() == tower ? o : t;
	}
	REQUIRE(t);
	CHECK(t->getProducerID() == pad->getID());
	bool variation = false, construction = true, damaged = false;
	int frames = 0;
	while (t->isUnderConstruction() && frames < 3000)
	{
		variation = variation || has(*t, "BUILD_VARIATION_ONE") || has(*t, "BUILD_VARIATION_TWO");
		construction = construction && has(*t, "PARTIALLY_CONSTRUCTED") && has(*t, "ACTIVELY_BEING_CONSTRUCTED");
		damaged = damaged || has(*t, "DAMAGED") || has(*t, "REALLYDAMAGED");
		logic.runLogicFrame();
		++frames;
	}
	MESSAGE("MenArrowTowerExpansion complete after " << frames << " frames");
	REQUIRE_FALSE(t->isUnderConstruction());
	CHECK(variation);
	CHECK(construction);
	CHECK_FALSE(damaged);
	CHECK(has(*t, "CONSTRUCTION_COMPLETE"));
	// complete: the damage states show (ActiveBody's thresholds; the INI's BUILD_VARIATION_x DAMAGED / REALLYDAMAGED states)
	const float max = t->getBodyModule()->getMaxHealth();
	bool sawDamaged = false, sawReally = false, exclusive = true;
	for (int k = 0; k < 18 && !sawReally; ++k)
	{
		hit(*t, max * 0.05f);
		sawDamaged = sawDamaged || has(*t, "DAMAGED");
		sawReally = sawReally || has(*t, "REALLYDAMAGED");
		exclusive = exclusive && !(has(*t, "DAMAGED") && has(*t, "REALLYDAMAGED"));
	}
	MESSAGE("health " << t->getBodyModule()->getHealth() << " / " << max << " damage state " << (int)t->getBodyModule()->getDamageState());
	CHECK(sawDamaged);
	CHECK(sawReally);
	CHECK(exclusive);
}
