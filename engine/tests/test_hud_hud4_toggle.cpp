// OpenBFME retail tests for lane HUD-4: the weapon set toggle (MSG_WEAPONSET_TOGGLE, RW 0x77B529; Object::setWeaponSetFlag RW 0x691059 / 0x691106). They share the
// HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/WeaponSetToggle.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("hud4 toggle");
}

// one player on a flat arena with the retail data (the HORDE-2 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	explicit Arena(SharedWorld &s)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
};

bool toggled(const Object &o)
{
	const WeaponConditionFlags &f = o.getWeaponSetFlags();
	return ((f[WeaponSetToggle::WEAPONSET_TOGGLE_1 >> 5] >> (WeaponSetToggle::WEAPONSET_TOGGLE_1 & 31)) & 1u) != 0;
}
} // namespace

TEST_CASE("hud4 toggle: the weapon set bit -> model condition table (RW 0xC16958)")
{
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(0) == 12);    // VETERAN -> WEAPONSET_VETERAN
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(24) == 301);  // WEAPONSET_TOGGLE_1
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(28) == 253);  // WEAPONSET_ONE_RING_MODE -> ONE_RING
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(62) == -1);   // HIDDEN .. ARMORSET_CREATE_A_HERO_10: none
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(103) == 584);
	CHECK(WeaponSetToggle::modelConditionForWeaponSetBit(104) == -1);
}

// The Rohirrim (RohanRohirrimHorde, HorseHordeContain, MonitorConditionUpdate WeaponSetFlags = WEAPONSET_TOGGLE_1 -> RohirrimHordeBowCommandSet):
// the message from the control bar's object sets WEAPONSET_TOGGLE_1 on every member and on the horde, the members take the model condition, the horde's
// command set swaps to the bow set, the state hash moves; a second message clears it again.
TEST_CASE("hud4 toggle: MSG_WEAPONSET_TOGGLE switches a Rohirrim horde's weapon set and back (RW 0x77B529), hashed")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *h = a.place("RohanRohirrimHorde", 0, 600.0f, 600.0f);
	for (int i = 0; i < 10; ++i)
	{
		a.logic.runLogicFrame();
	}
	REQUIRE(h->getContain());
	REQUIRE(h->getContain()->getContainCount() > 0);
	CHECK(h->getCommandSetName() == "RohirrimHordeCommandSet");
	const Object *member = h->getContain()->getContainedItemsList()->front();
	REQUIRE(member->getWeapons() != nullptr);
	const WeaponTemplateSet *spear = member->getWeapons()->templateSet();
	CHECK_FALSE(toggled(*h));
	GameLogicDispatch dispatch(a.logic);
	WeaponSetToggle::registerHandlers(dispatch);
	a.player(0)->selection() = { h->getID() };
	auto send = [&](ObjectID ref) {
		GameMessage m(MSG_WEAPONSET_TOGGLE, a.player(0)->getPlayerIndex());
		m.appendObjectIDArgument(ref);
		dispatch.dispatch(m);
	};
	const std::uint32_t before = a.logic.computeStateHash();
	send(h->getID());
	CHECK(toggled(*h));
	for (const Object *m : *h->getContain()->getContainedItemsList())
	{
		CHECK(toggled(*m));
		CHECK(m->testModelCondition(301)); // WEAPONSET_TOGGLE_1
	}
	CHECK(h->testModelCondition(301));
	CHECK(member->getWeapons()->templateSet() != spear); // the bow set
	CHECK(a.logic.computeStateHash() != before);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	CHECK(h->getCommandSetName() == "RohirrimHordeBowCommandSet"); // MonitorConditionUpdate reads the horde's Object flags
	// the second press: the reference is set, so the wanted state is clear
	send(h->getID());
	CHECK_FALSE(toggled(*h));
	CHECK_FALSE(toggled(*member));
	CHECK_FALSE(member->testModelCondition(301));
	CHECK(member->getWeapons()->templateSet() == spear);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	CHECK(h->getCommandSetName() == "RohirrimHordeCommandSet");
	// a horde the player has not selected does not toggle
	a.player(0)->selection().clear();
	send(h->getID());
	CHECK_FALSE(toggled(*h));
	// without a reference each object decides for itself (RW 0x77B546: the flag at [ebp + 0xF])
	a.player(0)->selection() = { h->getID() };
	send(INVALID_ID);
	CHECK(toggled(*h));
}

TEST_CASE("hud4 toggle: two runs of the same toggles give the same hashes (determinism)")
{
	if (!haveWorld())
	{
		return;
	}
	auto run = [&]() {
		std::vector<std::uint32_t> hashes;
		Arena a(shared());
		Object *h = a.place("RohanRohirrimHorde", 0, 600.0f, 600.0f);
		GameLogicDispatch dispatch(a.logic);
		WeaponSetToggle::registerHandlers(dispatch);
		a.player(0)->selection() = { h->getID() };
		for (int i = 0; i < 30; ++i)
		{
			if (i % 7 == 3)
			{
				GameMessage m(MSG_WEAPONSET_TOGGLE, a.player(0)->getPlayerIndex());
				m.appendObjectIDArgument(h->getID());
				dispatch.dispatch(m);
			}
			a.logic.runLogicFrame();
			hashes.push_back(a.logic.computeStateHash());
		}
		return hashes;
	};
	CHECK(run() == run());
}
