// OpenBFME retail tests (lane FACT-B): the inventory of Isengard and Mordor (FactbInventory.h) from the retail data. It shares the HUD tests' retail
// world (the file name sorts with the test_hud_* files) and SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset. With FACTB_INVENTORY_OUT set, the
// inventory is written there as markdown (docs/FACTB.md is that file plus the per-item results).

#include "doctest.h"
#include "FactbRig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

using namespace hudtest;

namespace
{
using factb::inventory;

void report(const factb::Inventory &inv)
{
	std::printf("factb inventory %s: %zu structures, %zu units, %zu hordes, %zu heroes, %zu upgrades, %zu powers, %zu sciences, %zu commands, %zu sets\n",
				inv.faction.c_str(), inv.count(factb::Kind::Structure), inv.count(factb::Kind::Unit), inv.count(factb::Kind::Horde), inv.count(factb::Kind::Hero),
				inv.count(factb::Kind::Upgrade), inv.count(factb::Kind::Power), inv.count(factb::Kind::Science), inv.count(factb::Kind::Command), inv.commandSets.size());
	for (const std::string &p : inv.problems)
	{
		std::printf("  problem: %s\n", p.c_str());
	}
}
} // namespace

TEST_CASE("factb inventory: Isengard and Mordor reach every structure, unit, hero, upgrade and power from the PlayerTemplate (retail data)")
{
	if (!haveWorld("factb inventory"))
	{
		return;
	}
	std::string md;
	for (const char *f : { "FactionIsengard", "FactionMordor" })
	{
		const factb::Inventory &inv = inventory(f);
		report(inv);
		CHECK_MESSAGE(inv.problems.empty(), f);
		md += factb::markdown(inv);
		const auto gaps = factb::unportedModules(*shared().world, inv);
		md += "### behaviour modules without a runtime\n\n| module class | templates |\n|---|---|\n";
		for (const auto &kv : gaps)
		{
			std::string names;
			for (const std::string &t : kv.second)
			{
				names += (names.empty() ? "" : ", ") + t;
			}
			std::printf("  unported module %s: %s\n", kv.first.c_str(), names.c_str());
			md += "| " + kv.first + " | " + names + " |\n";
		}
		md += "\n";
		// the pins (stops S-1993 .. S-1995, docs/STOPS.md): the behaviour module classes of the faction that run as the UnportedModule (it says so
		// at runtime) or as the base AI. A class ported later leaves the list: move the pin (lane FACT-C ported ObjectCreationUpgrade, SUMMON-1 DoCommandUpgrade, lane FX-4 TransitionDamageFX, lane MODCOMPAT-MOD1 PickupStuffUpdate, AutoPickUpUpdate, BoredUpdate)
		std::set<std::string> classes;
		for (const auto &kv : gaps)
		{
			classes.insert(kv.first);
		}
		const std::set<std::string> isengard = { "AnnounceBirthAndDeathBehavior", "BaseUpgrade",
			"EvacuateDamage", "FlingPassengerSpecialAbilityUpdate" }; // lane MODCOMPAT-MOD1 ported GeometryUpgrade, SlaveWatcherBehavior and PickupStuffUpdate; lane FX-4 ported TransitionDamageFX; lane SIEGE-1 SiegeDeployHordeSpecialPower; lane LUMBER1: SupplyCenterCreate / SupplyCenterDockUpdate ported (S-1993)
		const std::set<std::string> mordor = { "AimWeaponBehavior", "AnnounceBirthAndDeathBehavior", "BaseUpgrade",
			"EvacuateDamage", // lane SUMMON-1 ported DoCommandUpgrade; lane MODCOMPAT-MOD1 ClickReactionBehavior; lane MODCOMPAT-MOD1 AudioLoopUpgrade
			"FlingPassengerSpecialAbilityUpdate", "HordeTransportContainDamage", // lane MODCOMPAT-MOD1 ported GeometryUpgrade and GrabPassengerSpecialPower
			"RunOffMapBehavior", "SlaughterHordeContain" }; // lane MODCOMPAT-MOD1 ported SlaveWatcherBehavior, PickupStuffUpdate, AutoPickUpUpdate and BoredUpdate; lane FX-4 ported TransitionDamageFX; lane SIEGE-1 SiegeDeployHordeSpecialPower; lane LUMBER1: SupplyCenterCreate / SupplyCenterDockUpdate ported (S-1993)
		CHECK(classes == (std::string(f) == "FactionIsengard" ? isengard : mordor));
	}
	if (const char *out = std::getenv("FACTB_INVENTORY_OUT"))
	{
		std::ofstream(out, std::ios::binary) << md;
	}
}
