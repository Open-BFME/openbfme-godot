// OpenBFME unit tests, lane PORT-212 (fork issue 502, PROBLEMS P-133): a build plot's construction path applies no maximum.
//
// RotWK 2.01: a construction button is not a production button. RW 0x942733 (ControlBar's command availability) gives FOUNDATION_CONSTRUCT
// its own arm (arm 1) and DOZER_CONSTRUCT its own arm (0x35): "Buildable gate; template kind flags; ...; allowedToBuild / canBuild tests,
// radial hide, prerequisite and money results" and "Kind flags, controlling player +0x770 == 0, optional dozer slot 0x78,
// allowedToBuild/canBuild/money checks". Neither arm calls BuildAssistant::canMakeUnit,
// so the template's MaxSimultaneousOfType (canMakeUnit RW 0x793ECB, its count at RW 0x794054) never restricts a construction button. The
// message the plot's button sends is checked by its own case, RW 0x77A91B: the template, the builder and the player exist and
// Player::canBuild (RW 0x6AAA2E: allowedToBuild RW 0x6AC856, Buildable, the prerequisites) accepts. That is why a plot structure whose INI
// sets MaxSimultaneousOfType = 1 (KindOf NEED_BASE_FOUNDATION) can be built on every plot in RotWK while the port refused the second one
// (canMakeUnit answered CANMAKE_MAXED_OUT_FOR_PLAYER).
//
// The expectations below come from those two addresses and from a template with the same INI fields, not from the port's code: a structure
// with a limit of 1 is built on a second plot, and the production path (canMakeUnit, arm 3 of the same switch) still refuses a second one.
//
// GPL-3.0.

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "GameLogic/BuildCommands.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"

using namespace prodtest;

namespace
{
// The features that matter: a structure with a limit of 1 that a plot raises (NEED_BASE_FOUNDATION), a CastleMemberBehavior
// (a plot charges only such a structure, RW 0x858AB3) and the GettingBuiltBehavior that raises it.
const char kPort212Objects[] =
	"Object Moot\n"
	"  BuildCost = 100\n"
	"  BuildTime = 20.0\n"
	"  MaxSimultaneousOfType = 1\n"
	"  KindOf = STRUCTURE SELECTABLE NEED_BASE_FOUNDATION\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    MaxHealthDamaged = 500\n"
	"    MaxHealthReallyDamaged = 100\n"
	"  End\n"
	"End\n"
	"Object Plot\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION\n"
	"  CommandSet = PlotSet\n"
	"  Behavior = FoundationAIUpdate ModuleTag_FA\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n";
// the plot's construction button: RW 0x942733 arm 1, the case RW 0x77A91B sends
const char kPort212Commands[] =
	"CommandButton Command_BuildMoot\n  Command = FOUNDATION_CONSTRUCT\n  Object = Moot\nEnd\n"
	"CommandSet PlotSet\n  1 = Command_BuildMoot\nEnd\n";

struct Port212World : ProdWorld
{
	Object *plot1 = nullptr;
	Object *plot2 = nullptr;
	GameLogicDispatch dispatch;
	BuildCommands commands;

	Port212World() : dispatch(*logic)
	{
		load(kPort212Objects);
		load(kPort212Commands);
		logic->settings().buildRulesLoaded = true;
		commands.registerHandlers(dispatch);
		plot1 = make("Plot", teamOf("Alice"));
		plot2 = make("Plot", teamOf("Alice"));
		REQUIRE(plot1 != nullptr);
		REQUIRE(plot2 != nullptr);
		Coord3D a{ 500.0f, 600.0f, 0.0f };
		plot1->setPosition(&a);
		plot1->setOrientation(0.0f);
		Coord3D b{ 900.0f, 600.0f, 0.0f };
		plot2->setPosition(&b);
		plot2->setOrientation(0.0f);
	}
	void select(Object *o)
	{
		alice()->selection().clear();
		alice()->selection().push_back(o->getID());
	}
	// RW 0x77A91B's argument list: the template id, a location, an angle
	void construct(Object *plot, const char *what)
	{
		const ThingTemplate *tt = w.get(what);
		REQUIRE(tt != nullptr);
		select(plot);
		GameMessage m(MSG_FOUNDATION_CONSTRUCT, alice()->getPlayerIndex());
		m.appendIntegerArgument((int)tt->getTemplateID());
		m.appendLocationArgument(*plot->getPosition());
		m.appendRealArgument(0.0f);
		dispatch.dispatch(m);
	}
	int countOf(const char *name) const
	{
		int n = 0;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name && !o->isDestroyed())
			{
				++n;
			}
		}
		return n;
	}
};
} // namespace

TEST_CASE("PORT-212 plot builds: a structure whose MaxSimultaneousOfType is 1 is raised on a second plot (RW 0x942733 arm 1 / RW 0x77A91B: Player::canBuild, never canMakeUnit)")
{
	Port212World f;
	const ThingTemplate *moot = f.w.get("Moot");
	REQUIRE(moot != nullptr);
	REQUIRE(f.w.get("Plot") != nullptr);
	REQUIRE(moot->getFinalOverride() == moot);
	const std::uint32_t money0 = f.alice()->getMoney()->countMoney();

	// the first: the plot is free, the player can build it, the price is paid at the placement (RW 0x858AB3)
	f.construct(f.plot1, "Moot");
	REQUIRE(f.commands.stats().foundations == 1);
	REQUIRE(f.countOf("Moot") == 1);

	// the production path of the same template keeps the limit (RW 0x942733 arm 3 -> canMakeUnit RW 0x793ECB, count RW 0x794054)
	CHECK(BuildAssistant::canMakeUnit(*f.plot1, moot, -1) == CANMAKE_MAXED_OUT_FOR_PLAYER);

	// the second: RW 0x942733 arm 1 asks Buildable + Player::canBuild + money and RW 0x77A91B asks Player::canBuild again, so the limit of
	// the INI does not stand in the way of a construction; the port's dispatcher answered CANMAKE_MAXED_OUT_FOR_PLAYER before this change
	f.construct(f.plot2, "Moot");
	CHECK(f.commands.stats().foundations == 2);
	CHECK(f.commands.refusals().empty());
	CHECK(f.countOf("Moot") == 2);
	CHECK(f.alice()->getMoney()->countMoney() == money0 - 200); // both paid, at the placement (100 each: the template's BuildCost)

	// RW 0x942733 arm 3 still holds the limit: a unit of this template cannot be queued past one through the production arm either
	CHECK(BuildAssistant::maxSimultaneousOfType(*moot) == 1);
}

TEST_CASE("PORT-212 plot builds: the plot's construction case still refuses a player the RW 0x6AAA2E check turns down (allowedToBuild RW 0x6AC856)")
{
	Port212World f;
	const ThingTemplate *moot = f.w.get("Moot");
	REQUIRE(moot != nullptr);
	REQUIRE(f.alice()->canBuildBase());
	// RW 0x6AC856: the player's structures flag is the first gate of allowedToBuild
	f.alice()->setCanBuildBase(false);
	CHECK_FALSE(BuildAssistant::playerCanBuild(*f.alice(), moot, *f.logic));
	f.construct(f.plot1, "Moot");
	CHECK(f.commands.stats().foundations == 0);
	CHECK(f.countOf("Moot") == 0);
	REQUIRE(f.commands.refusals().size() == 1);
	CHECK(f.commands.refusals().front().find("Player::canBuild") != std::string::npos);
}
