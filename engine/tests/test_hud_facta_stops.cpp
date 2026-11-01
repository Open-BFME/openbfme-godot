// OpenBFME retail tests for lane FACT-A: the lane's stops S-1980 .. S-1986 are reported at runtime by the code that meets them (GameLogic::report().stops,
// the unported module census of S-140). SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace facta;

namespace
{
bool reported(GameLogic &logic, const char *id)
{
	for (const std::string &s : logic.report().stops)
	{
		if (s.find(id) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

void selectInLogic(FactRig &r, Object *o)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, r.local()->getPlayerIndex());
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(o->getID());
	r.rig.game->commands().append(m);
	r.frames(2);
}
} // namespace

TEST_CASE("FACT-A stops: S-1980 .. S-1986 are reported where they apply")
{
	if (!hudtest::haveWorld("facta stops"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	// lane MAPRESET-1: one live game per world (S-3808): each rig ends before the next one starts
	{
		FactRig r(s, "FactionMen");
		// the lines exist with their ids
		const std::vector<std::string> sell = BuildAssistantSell::acceptanceStops(), weapon = WeaponCommands::acceptanceStops();
		REQUIRE(sell.size() == 2);
		REQUIRE(weapon.size() == 2);
		CHECK(sell[0].rfind("[S-1980]", 0) == 0);
		CHECK(sell[1].rfind("[S-1981]", 0) == 0);
		CHECK(weapon[0].rfind("[S-1982]", 0) == 0);
		CHECK(weapon[1].rfind("[S-1983]", 0) == 0);

		// S-1980: a sold structure with a contain (its riders are ordered out as BFME2's OpenContain does)
		CHECK_FALSE(reported(r.logic(), "[S-1980]"));
		Object *withContain = nullptr;
		for (const std::string &name : reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(name);
			if (!setButton(templateCommandSet(*tt), GUI_COMMAND_SELL))
			{
				continue;
			}
			Object *o = r.make(name, r.spot(2000.0f, 1500.0f, 300.0f));
			r.frames(2);
			if (o->getContain() && o->isKindOfName("STRUCTURE"))
			{
				withContain = o;
				break;
			}
			r.logic().destroyObject(o);
			r.frames(2);
		}
		REQUIRE(withContain);
		r.logic().sellList().sellObject(withContain);
		r.frames(3);
		CHECK(reported(r.logic(), "[S-1980]"));

		// S-1981: SellPercentage not loaded
		r.logic().settings().sellPercentageLoaded = false;
		Object *farm = r.make("GondorFarm", r.spot(2600.0f, 1500.0f, 300.0f));
		r.frames(2);
		r.logic().sellList().sellObject(farm);
		r.frames(3);
		CHECK(reported(r.logic(), "[S-1981]"));
		r.logic().settings().sellPercentageLoaded = true;

		// S-1982: a fire-weapon order on the ground; S-1983: the hold-fire toggle
		Object *treb = r.make("GondorTrebuchet", r.spot(2000.0f, 1300.0f, 300.0f));
		r.frames(5);
		selectInLogic(r, treb);
		GameMessage fire(MSG_DO_WEAPON_AT_LOCATION, r.local()->getPlayerIndex());
		fire.appendIntegerArgument(0);
		fire.appendLocationArgument(Coord3D{ treb->getPosition()->x + 300.0f, treb->getPosition()->y, 0.0f });
		fire.appendIntegerArgument(0x7FFFFFFF);
		r.rig.game->commands().append(fire);
		r.frames(2);
		CHECK(reported(r.logic(), "[S-1982]"));
		GameMessage hold(MSG_TOGGLE_NO_AUTO_ACQUIRE, r.local()->getPlayerIndex());
		hold.appendObjectIDArgument(treb->getID());
		r.rig.game->commands().append(hold);
		r.frames(2);
		CHECK(reported(r.logic(), "[S-1983]"));
		CHECK(treb->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE")));

		// S-1984: the Demolisher's deploy modules are ported since lane SIEGE-1 (their remaining stop S-4945 reaches the report)
	}
	// (the Men rig has ended)
	{
		FactRig d(s, "FactionDwarves");
		Object *demo = d.make("DwarvenDemolisher", d.spot(2000.0f, 1300.0f, 300.0f));
		d.frames(2);
		const std::vector<std::string> unp = unportedModules(*demo);
		CHECK(std::find(unp.begin(), unp.end(), "ToggleDeploySpecialAbilityUpdate") == unp.end());
		CHECK(std::find(unp.begin(), unp.end(), "DeployStyleAIUpdate") == unp.end());
		CHECK(d.logic().report().unportedModules.count("ToggleDeploySpecialAbilityUpdate") == 0);
		CHECK(reported(d.logic(), "[S-4945]"));

	}
	// ObjectCreationUpgrade runs (the Elven citadel's Crystal Moat appears one frame after its Delay of 0, RW 0x8B8464); since merge q2 the module is
	// FACT-C's port and its stop is S-2007 (S-1985's SlaveWatcherBehavior hand-over is part of it)
	const std::vector<std::string> ocu = ObjectCreationUpgrade::acceptanceStops();
	REQUIRE(ocu.size() == 1);
	CHECK(ocu[0].rfind("[S-2007]", 0) == 0);
	const std::vector<std::string> rsu = ReplaceSelfUpgrade::stopLines(); // S-1986: the wall links (the fortress test replaces the segments)
	REQUIRE(rsu.size() == 1);
	CHECK(rsu[0].rfind("[S-1986]", 0) == 0);
	FactRig e(s, "FactionElves");
	Object *citadel = e.make("ElvenCitadel", e.spot(2400.0f, 1300.0f, 400.0f));
	e.frames(2);
	const std::vector<std::string> citadelUnported = unportedModules(*citadel);
	CHECK(std::find(citadelUnported.begin(), citadelUnported.end(), "ObjectCreationUpgrade") == citadelUnported.end());
	auto moats = [&]() {
		int n = 0;
		for (Object *o = e.logic().getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate()->getName() == "ElvenFortressCrystalMoat" && o->getControllingPlayer() == e.local() ? 1 : 0;
		}
		return n;
	};
	CHECK(moats() == 0);
	citadel->giveUpgrade("Upgrade_ElvenFortressCrystalMoat");
	e.frames(3);
	CHECK(moats() == 1);
}
