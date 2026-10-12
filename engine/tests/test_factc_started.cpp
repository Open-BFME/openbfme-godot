// OpenBFME retail tests (lane FACT-C): the Goblins (FactionWild) and Angmar in a skirmish started as the lobby starts it (FACT-B's harness, FactbRig.h:
// NewGame::prepareNewGame, every slot's starting base placed and its castle unpacked): the inventory reached from the started base (the keep, the
// expansion pads, the wall hubs, the starting builders), and the fortress improvements through the castle (CastleUpgrade, RW 0x8BAAC0), bought at the
// started keep's buttons (CASTLE_UPGRADE, RW 0x942733 / 0x940435 case 8). The other sweeps
// of the two factions run FACT-B's sweep functions (test_hud_factb_units.cpp / test_hud_factb_production.cpp, the "factc ..." cases). They SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"
#include "FactbInventory.h"
#include "FactbRig.h"

#include "Common/BuildAssistant.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/CastleUpgrade.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

using namespace hudtest;

TEST_CASE("factc inventory: the Goblins and Angmar reach every structure, unit, hero, upgrade and power from the started skirmish (retail data)")
{
	if (!haveWorld("factc inventory"))
	{
		return;
	}
	std::string md;
	for (const char *f : { "FactionWild", "FactionAngmar" })
	{
		const factb::Inventory &inv = factb::inventory(f);
		std::printf("factc inventory %s: %zu structures, %zu units, %zu hordes, %zu heroes, %zu upgrades, %zu powers, %zu sciences, %zu commands, %zu sets\n", f,
					inv.count(factb::Kind::Structure), inv.count(factb::Kind::Unit), inv.count(factb::Kind::Horde), inv.count(factb::Kind::Hero), inv.count(factb::Kind::Upgrade),
					inv.count(factb::Kind::Power), inv.count(factb::Kind::Science), inv.count(factb::Kind::Command), inv.commandSets.size());
		for (const std::string &p : inv.problems)
		{
			std::printf("  problem: %s\n", p.c_str());
		}
		CHECK_MESSAGE(inv.problems.empty(), f);
		CHECK(inv.count(factb::Kind::Structure) >= 8);
		CHECK(inv.count(factb::Kind::Hero) >= 4);
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
	}
	if (const char *out = std::getenv("FACTC_INVENTORY_OUT"))
	{
		std::ofstream(out, std::ios::binary) << md;
	}
}

// The fortress improvements bought the way a player buys them: the started skirmish's own keep (a castle member, so CastleUpgrade reaches it), each
// upgrade button of its command set pressed through the control bar until no further button becomes enabled. Each bought upgrade must land on the keep
// (or on the player for a player upgrade); a trigger upgrade must bring its CastleUpgrade's upgrade to the keep and to the side's wall hub put inside
// WallUpgradeRadius; the buttons that need an improvement (the Dragon Nest after the Web Cocoon, the Fire Drake after the nest, the Sanctum after the Ice
// Walls) must become enabled by it. A button that stays disabled fails with its state
TEST_CASE("factc castle buttons: every Goblin and Angmar fortress improvement bought at the started keep's buttons reaches the keep, the walls and what needs it")
{
	if (!haveWorld("factc castle buttons"))
	{
		return;
	}
	for (const char *faction : { "FactionWild", "FactionAngmar" })
	{
		CAPTURE(faction);
		factb::HudGame g(shared(), faction, true);
		g.rig.local->depositMoney(5000000);
		Object *keep = nullptr;
		for (Object *o = g.logic().getFirstObject(); o && !keep; o = o->getNextObject())
		{
			const CommandSet *cs = o->getControllingPlayer() == g.rig.local ? shared().world->commands().findCommandSet(o->getCommandSetName()) : nullptr;
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS && !keep; ++i)
			{
				keep = cs->getCommandButton(i) && cs->getCommandButton(i)->getFinalOverride()->m_command == GUI_COMMAND_REVIVE ? o : nullptr;
			}
		}
		REQUIRE(keep);
		const ObjectID keepId = keep->getID();
		// a wall hub of the side inside the castle's smallest WallUpgradeRadius (a started base has none: the builder puts them); a castle whose
		// CastleUpgrades reach walls must get its hub there
		float radius = 0.0f;
		for (const ThingTemplate::Nugget &nu : keep->getTemplate()->getFinalOverride()->behaviorModules().nuggets())
		{
			if (const CastleUpgradeModuleData *d = dynamic_cast<const CastleUpgradeModuleData *>(nu.data.get()); d && d->m_wallUpgradeRadius > 0.0f)
			{
				radius = radius == 0.0f ? d->m_wallUpgradeRadius : std::min(radius, d->m_wallUpgradeRadius);
			}
		}
		ObjectID hubId = INVALID_ID;
		for (const ThingTemplate *t : shared().world->things().templates())
		{
			if (radius > 0.0f && hubId == INVALID_ID && factb::hasKindOf(*t, "WALL_HUB") && factb::stringFieldOf(*t, "Side") == g.rig.local->getPlayerTemplate()->m_side &&
				!factb::hasKindOf(*t, "CASTLE_FOUNDATION") && t->getName().find("Hub") != std::string::npos)
			{
				for (int k = 0; k < 16 && hubId == INVALID_ID; ++k)
				{
					const float a = 0.4f * (float)k, r = radius * 0.6f;
					const Coord3D at = g.rig.freeSpot(keep->getPosition()->x + r * std::cos(a), keep->getPosition()->y + r * std::sin(a), 60.0f);
					const float dx = at.x - keep->getPosition()->x, dy = at.y - keep->getPosition()->y;
					if (dx * dx + dy * dy < radius * radius * 0.81f)
					{
						if (Object *hub = g.rig.make(t->getName(), at.x, at.y))
						{
							hubId = hub->getID();
							std::printf("factc castle buttons %s: wall hub %s %.0f from the keep (WallUpgradeRadius %.0f)\n", faction, t->getName().c_str(),
										std::sqrt(dx * dx + dy * dy), radius);
						}
					}
				}
			}
		}
		REQUIRE_MESSAGE((radius == 0.0f || hubId != INVALID_ID), faction << ": no wall hub of the side could be put inside WallUpgradeRadius " << radius);
		g.step(3);
		std::set<const CommandButton *> done;
		std::map<std::string, std::string> pending;
		std::vector<std::string> bought;
		for (int round = 0; round < 6; ++round)
		{
			bool progress = false;
			const CommandSet *cs = shared().world->commands().findCommandSet(g.logic().findObjectByID(keepId)->getCommandSetName());
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *cb = cs->getCommandButton(i) ? cs->getCommandButton(i)->getFinalOverride() : nullptr;
				if (!cb || done.count(cb) ||
					(cb->m_command != GUI_COMMAND_OBJECT_UPGRADE && cb->m_command != GUI_COMMAND_PLAYER_UPGRADE && cb->m_command != GUI_COMMAND_CASTLE_UPGRADE))
				{
					continue;
				}
				keep = g.logic().findObjectByID(keepId);
				REQUIRE(keep);
				const std::string upName = cb->m_upgradeName.substr(0, cb->m_upgradeName.find_first_of(" \t"));
				const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(upName);
				REQUIRE_MESSAGE(u, upName);
				ControlBarButton b;
				if (!g.select(keep) || !g.reveal(keep, [&](const CommandButton &c) { return c.getFinalOverride() == cb; }, b))
				{
					pending[cb->m_name] = "not a visible button of the selected keep (selection " + g.selectionText() + ")";
					continue;
				}
				if (b.state != ButtonState::Enabled)
				{
					pending[cb->m_name] = "state " + std::to_string((int)b.state);
					continue;
				}
				const unsigned spent0 = g.spent();
				if (!g.press(b))
				{
					pending[cb->m_name] = "the press did nothing";
					continue;
				}
				const int frames = u->calcTimeToBuild(g.rig.local) + 30; // step() advances one logic frame (0.2 s)
				bool landed = false;
				for (int f = 0; f < frames && !landed; ++f)
				{
					g.step(1);
					keep = g.logic().findObjectByID(keepId);
					landed = keep && (u->getUpgradeType() == UPGRADE_TYPE_PLAYER ? g.rig.local->hasUpgradeComplete(u) : keep->hasUpgrade(u));
				}
				const ProductionUpdateInterface *queue = keep ? keep->getProductionUpdate() : nullptr;
				const ProductionEntry *first = queue ? queue->firstProduction() : nullptr;
				CHECK_MESSAGE(landed, faction << " " << cb->m_name << ": " << upName << " never reached the keep or the player after " << frames << " steps (the keep's queue: "
														   << (queue ? queue->getProductionCount() : -1) << " entries, the first at " << (first ? first->percentComplete : -1.0f)
														   << "%, sent: " << g.lastMessages() << ")");
				g.step(3); // the upgrade modules the grant triggers run
				keep = g.logic().findObjectByID(keepId);
				REQUIRE(keep);
				CHECK_MESSAGE(g.spent() - spent0 == (unsigned)u->calcCostToBuild(g.rig.local, g.logic().findObjectByID(keepId)), cb->m_name << " paid " << (g.spent() - spent0));
				done.insert(cb);
				pending.erase(cb->m_name);
				progress = true;
				std::string line = cb->m_name + " -> " + upName;
				for (const ThingTemplate::Nugget &n : keep->getTemplate()->getFinalOverride()->behaviorModules().nuggets())
				{
					const CastleUpgradeModuleData *d = dynamic_cast<const CastleUpgradeModuleData *>(n.data.get());
					if (!d || d->m_triggeredBy.size() != 1 || d->m_triggeredBy[0] != upName)
					{
						continue;
					}
					const UpgradeTemplate *granted = TheUpgradeCenter->findUpgrade(d->m_upgradeName);
					REQUIRE(granted);
					CHECK_MESSAGE(keep->hasUpgrade(granted), d->m_upgradeName << " (CastleUpgrade of " << upName << ") did not reach the keep");
					line += " -> " + d->m_upgradeName + (keep->hasUpgrade(granted) ? " on the keep" : " MISSING on the keep");
					if (d->m_wallUpgradeRadius > 0.0f)
					{
						Object *hub = g.logic().findObjectByID(hubId);
						REQUIRE_MESSAGE(hub, d->m_upgradeName << ": the wall hub left the world");
						CHECK_MESSAGE(hub->hasUpgrade(granted), d->m_upgradeName << " did not reach the wall hub inside WallUpgradeRadius " << d->m_wallUpgradeRadius);
						line += hub->hasUpgrade(granted) ? " and the wall hub" : " NOT the wall hub";
					}
				}
				// an ObjectCreationUpgrade of the keep it triggers (the spike moat, the Sanctum's readiness): after its Delay, its ThingToSpawn stands and
				// its GrantUpgrade is on the keep (RW 0x8B8464)
				for (const std::unique_ptr<BehaviorModule> &m : keep->modules())
				{
					const ObjectCreationUpgrade *ocu = dynamic_cast<const ObjectCreationUpgrade *>(m.get());
					const ObjectCreationUpgradeModuleData *od = ocu ? dynamic_cast<const ObjectCreationUpgradeModuleData *>(ocu->muxData()) : nullptr;
					if (!od || std::find(od->m_triggeredBy.begin(), od->m_triggeredBy.end(), upName) == od->m_triggeredBy.end())
					{
						continue;
					}
					const ObjectID keepNow = keep->getID();
					bool done = false;
					// UseBuildingProduction (the Fire Drake): the keep's queue makes it, after its build time
					for (int f = 0; f < (int)od->m_delay + (od->m_useBuildingProduction ? 400 : 60) && !done; ++f)
					{
						g.step(1);
						keep = g.logic().findObjectByID(keepNow);
						REQUIRE(keep);
						const UpgradeTemplate *gu = od->m_grantUpgrade.empty() ? nullptr : TheUpgradeCenter->findUpgrade(od->m_grantUpgrade);
						const Object *made = ocu->spawned() != INVALID_ID ? g.logic().findObjectByID(ocu->spawned()) : nullptr;
						for (Object *o = g.logic().getFirstObject(); o && !made && od->m_useBuildingProduction; o = o->getNextObject())
						{
							made = o->getControllingPlayer() == g.rig.local && o->getTemplate()->getName() == od->m_thingToSpawn ? o : nullptr;
						}
						done = (od->m_thingToSpawn.empty() || (made && made->getTemplate()->getName() == od->m_thingToSpawn)) && (!gu || keep->hasUpgrade(gu));
					}
					const ProductionUpdateInterface *kq = keep ? keep->getProductionUpdate() : nullptr;
					const ProductionEntry *kf = kq ? kq->firstProduction() : nullptr;
					const int canMake = keep && od->m_useBuildingProduction ? (int)BuildAssistant::canMakeUnit(*keep, g.logic().things().findTemplate(od->m_thingToSpawn), -1) : -1;
					if (od->m_useBuildingProduction)
					{
						// S-2009: the keep's queue refuses the Fire Drake (canMakeUnit: no prerequisites; the producer's module-offered templates, RW 0x794F69,
						// are not ported, S-207): pinned until it is made
						CHECK_MESSAGE((!done && canMake == (int)CANMAKE_NO_PREREQUISITES), cb->m_name << ": the Fire Drake is made now: move the pin of S-2009");
						std::printf("factc castle buttons %s: KNOWN %s (S-2009: canMakeUnit %d, queued %u)\n", faction, cb->m_name.c_str(), canMake, ocu->queued());
					}
					else
					{
						CHECK_MESSAGE(done, cb->m_name << ": the ObjectCreationUpgrade's " << (od->m_thingToSpawn.empty() ? std::string() : od->m_thingToSpawn + " / ") << od->m_grantUpgrade
																  << " did not show after its Delay (the keep's queue " << (kq ? kq->getProductionCount() : -1) << " entries, the first "
																  << (kf && kf->objectToProduce ? kf->objectToProduce->getName() : std::string("-")) << ")");
					}
					line += " -> " + (od->m_thingToSpawn.empty() ? std::string() : od->m_thingToSpawn + (done ? " made, " : " NOT made, ")) + (od->m_grantUpgrade.empty() ? std::string() : od->m_grantUpgrade + " granted");
				}
				bought.push_back(line);
			}
			if (!progress)
			{
				break;
			}
		}
		for (const std::string &l : bought)
		{
			std::printf("factc castle buttons %s: %s\n", faction, l.c_str());
		}
		for (const auto &kv : pending)
		{
			std::printf("factc castle buttons %s: NOT BOUGHT %s: %s\n", faction, kv.first.c_str(), kv.second.c_str());
			CHECK_MESSAGE(false, faction << " " << kv.first << ": " << kv.second);
		}
		CHECK(bought.size() >= 4);
	}
}
