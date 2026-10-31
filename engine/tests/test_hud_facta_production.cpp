// OpenBFME retail tests for lane FACT-A: every unit, horde and upgrade of Men, Elves and Dwarves bought from its building's control bar as a player does
// (the button's press, the HUD's message, the logic's queue). Per item: the button is shown and enabled, the price the INI states (BuildCost of the unit or
// horde, of the upgrade) is taken at the press, the unit appears near its building after five frames per whole second of BuildTime (RW 0x73C39E) or the
// upgrade completes after five frames per second of its BuildTime (RW 0x66F1A8), and the command points the unit's template states are counted.
// SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"
#include "ProductionRateTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace facta;

namespace
{
int frameOfSeconds(float seconds) { return 5 * (int)seconds; } // RW 0x73C39E: whole seconds, five frames each

// what the building's queue charged for the unit or upgrade (the entry's cost, RW 0x8A1B9F / 0x8A10C8); 0 when nothing was queued
unsigned chargedFor(Object *building, const ThingTemplate *unit, const UpgradeTemplate *upgrade)
{
	const ProductionUpdateInterface *pu = building->getProductionUpdate();
	unsigned cost = 0;
	for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; e; e = pu->nextProduction(e))
	{
		if ((unit && e->objectToProduce && BuildAssistant::isEquivalentTo(e->objectToProduce, unit)) || (upgrade && e->upgradeToResearch == upgrade))
		{
			cost = (unsigned)e->cost;
		}
	}
	return cost;
}

struct Outcome
{
	std::vector<std::string> problems;
	size_t units = 0, upgrades = 0, producers = 0;
};

bool isStructure(const ThingTemplate &tt)
{
	static const int bit = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	return MaskTest(ObjectTemplateInfoBuilder::build(*tt.getFinalOverride()).kindOf, (unsigned)bit);
}

std::set<ObjectID> ownIds(FactRig &r)
{
	std::set<ObjectID> ids;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		ids.insert(o->getID());
	}
	return ids;
}

// presses the UNIT_BUILD button `b` of the selected building and waits for the unit
void buildUnit(FactRig &r, Object *building, const CommandButton &b, Outcome &out, const std::string &tag)
{
	const ThingTemplate &unit = *b.getThingTemplate()->getFinalOverride();
	const ControlBarButton *cb = r.find(b.m_name);
	if (!cb)
	{
		out.problems.push_back(tag + ": the button is not on the bar");
		return;
	}
	if (cb->state != ButtonState::Enabled)
	{
		out.problems.push_back(tag + ": the button is not enabled (state " + std::to_string((int)cb->state) + ")");
		return;
	}
	const std::set<ObjectID> before = ownIds(r);
	const unsigned money = r.money();
	const int cpBefore = r.bar().commandPointsUsed();
	if (!r.bar().pressButton(cb->slot, cb->inPalantir))
	{
		out.problems.push_back(tag + ": the press was refused");
		return;
	}
	std::vector<float> rates{ prodrate::rateNow(*building) }; // lane PRODUCTION1: the producer's PRODUCTION rate of every frame (RW 0x8A1EBC)
	r.frames(1);
	// the charge: the production entry's stored cost (the money is also moved by the farms' income in the same frames)
	const unsigned cost = BuildAssistant::buildCost(unit);
	const unsigned charged = chargedFor(building, &unit, nullptr);
	if (charged != cost)
	{
		out.problems.push_back(tag + ": charged " + std::to_string(charged) + " (money " + std::to_string((int)(money - r.money())) + "), BuildCost " + std::to_string(cost));
	}
	const int expect = frameOfSeconds(BuildAssistant::buildTime(unit));
	Object *made = nullptr;
	int waited = 1, complete = -1;
	for (; waited < expect + 90 && !made; ++waited)
	{
		rates.push_back(prodrate::rateNow(*building));
		r.frames(1);
		const ProductionUpdateInterface *pu = building->getProductionUpdate();
		const ProductionEntry *e = pu ? pu->firstProduction() : nullptr;
		if (complete < 0 && (!e || e->percentComplete >= 100.0f))
		{
			complete = waited; // the build time is over: the door opens, the unit (a horde: its members one by one) comes out
		}
		for (Object *o = r.logic().getFirstObject(); o && !made; o = o->getNextObject())
		{
			// the template or one of its BuildVariations (RW 0x73D5C2 ThingTemplate::isEquivalentTo: the Ent moot's generic Ent becomes a birch, a fir, ...)
			if (!before.count(o->getID()) && BuildAssistant::isEquivalentTo(o->getTemplate()->getFinalOverride(), &unit) && o->getControllingPlayer() == r.local()
				&& !o->getContainedBy())
			{
				made = o;
			}
		}
	}
	if (!made)
	{
		const ProductionUpdateInterface *pu = building->getProductionUpdate();
		const ProductionEntry *e = pu ? pu->firstProduction() : nullptr;
		std::string near;
		for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (!before.count(o->getID()))
			{
				near += " " + o->getTemplate()->getName();
			}
		}
		out.problems.push_back(tag + ": nothing came out after " + std::to_string(waited) + " frames (BuildTime " + std::to_string(expect) + " frames; queue "
			+ std::to_string(pu ? pu->getProductionCount() : -1) + (e && e->objectToProduce ? " " + e->objectToProduce->getName() + " " + std::to_string((int)e->percentComplete) + "%" : std::string())
			+ "; new objects" + near + ")");
		return;
	}
	// the build completes after BuildTime (whole seconds, five frames each; one frame of slack for the press frame) at rate 1.0; lane PRODUCTION1: a level 2 / 3
	// building is faster (its BuildSpeedModLvl<n>, RW 0x8A1EBC)
	const int rated = prodrate::expectedFrames(rates, expect);
	if (complete < rated - 1 || complete > rated + 2)
	{
		out.problems.push_back(tag + ": built in " + std::to_string(complete) + " frames, BuildTime gives " + std::to_string(expect) + " (" + std::to_string(rated) +
			" at the producer's PRODUCTION rate)");
	}
	const float dx = made->getPosition()->x - building->getPosition()->x, dy = made->getPosition()->y - building->getPosition()->y;
	if (dx * dx + dy * dy > 400.0f * 400.0f)
	{
		out.problems.push_back(tag + ": came out " + std::to_string((int)std::sqrt(dx * dx + dy * dy)) + " from its building");
	}
	r.frames(40); // a horde's members come out one per exit delay
	// the unit has left the building (its entry leaves the queue once the exit ends; removing a unit that is still walking out makes production replace it)
	for (int f = 0; f < 300; ++f)
	{
		const ProductionUpdateInterface *pu = building->getProductionUpdate();
		if (!pu || pu->getProductionCount() == 0)
		{
			break;
		}
		r.frames(1);
	}
	const int cp = BuildAssistant::commandPoints(*made->getTemplate()->getFinalOverride());
	if (r.bar().commandPointsUsed() - cpBefore != cp)
	{
		out.problems.push_back(tag + ": command points +" + std::to_string(r.bar().commandPointsUsed() - cpBefore) + ", CommandPoints " + std::to_string(cp));
	}
	++out.units;
	if (ContainModuleInterface *c = made->getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			const std::vector<Object *> members(items->begin(), items->end());
			for (Object *m : members)
			{
				r.logic().destroyObject(m);
			}
		}
	}
	r.logic().destroyObject(made);
	r.frames(10);
}

// presses the OBJECT_UPGRADE / PLAYER_UPGRADE button `b` of the selected building and waits for the upgrade
void buyUpgrade(FactRig &r, Object *building, const CommandButton &b, Outcome &out, const std::string &tag)
{
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
	if (!u)
	{
		out.problems.push_back(tag + ": no upgrade " + b.m_upgradeName);
		return;
	}
	const bool player = b.m_command == GUI_COMMAND_PLAYER_UPGRADE;
	auto has = [&]() { return player ? r.local()->hasUpgradeComplete(u) : building->hasUpgrade(u); };
	if (has())
	{
		return; // granted already (an earlier button, the start)
	}
	const ControlBarButton *cb = r.find(b.m_name);
	if (!cb)
	{
		out.problems.push_back(tag + ": the button is not on the bar (the bar shows " + r.bar().commandSetName() + " of object " + std::to_string((int)r.bar().sourceObject())
			+ ", the building " + std::to_string((int)building->getID()) + " has " + building->getCommandSetName() + ", HUD selection "
			+ std::to_string(r.hud->input().ui().selected().size()) + ")");
		return;
	}
	if (cb->state != ButtonState::Enabled)
	{
		out.problems.push_back(tag + ": the button is not enabled (state " + std::to_string((int)cb->state) + ")");
		return;
	}
	const unsigned money = r.money();
	if (!r.bar().pressButton(cb->slot, cb->inPalantir))
	{
		out.problems.push_back(tag + ": the press was refused");
		return;
	}
	std::vector<float> rates{ prodrate::rateNow(*building) }; // lane PRODUCTION1: the producer's PRODUCTION rate of every frame (RW 0x8A1EBC)
	r.frames(1);
	const unsigned charged = chargedFor(building, nullptr, u);
	if (charged != (unsigned)u->getBuildCost())
	{
		out.problems.push_back(tag + ": charged " + std::to_string(charged) + " (money " + std::to_string((int)(money - r.money())) + "), BuildCost " + std::to_string(u->getBuildCost()));
	}
	const int expect = (int)(5.0f * u->getBuildTimeSeconds());
	int waited = 1;
	for (; waited < expect + 30 && !has(); ++waited)
	{
		rates.push_back(prodrate::rateNow(*building));
		r.frames(1);
	}
	const int rated = prodrate::expectedFrames(rates, expect);
	if (!has())
	{
		out.problems.push_back(tag + ": not complete after " + std::to_string(waited) + " frames (BuildTime " + std::to_string(expect) + " frames)");
		return;
	}
	if (waited < rated - 1 || waited > rated + 3)
	{
		out.problems.push_back(tag + ": complete after " + std::to_string(waited) + " frames, BuildTime gives " + std::to_string(expect) + " (" + std::to_string(rated) +
			" at the producer's PRODUCTION rate)");
	}
	++out.upgrades;
}

Outcome runFaction(SharedWorld &s, const std::string &faction)
{
	Outcome out;
	FactRig r(s, faction.c_str());
	r.local()->depositMoney(5000000);
	// command points as a player raises them: four of the faction's first building with a CommandPointBonus (the farms, mallorn trees, mine shafts)
	for (const std::string &name : reachableTemplates(r))
	{
		const ThingTemplate *tt = r.logic().things().findTemplate(name);
		if (Economy::templateInt(*tt->getFinalOverride(), "CommandPointBonus") > 0)
		{
			for (int i = 0; i < 4; ++i)
			{
				r.make(name, r.spot(3000.0f + 300.0f * (float)i, 1000.0f, 300.0f));
			}
			break;
		}
	}
	r.frames(5);
	std::printf("facta production %s: command point limit %d\n", faction.c_str(), r.bar().commandPointsLimit());
	float x = 1300.0f;
	// the start's own buildings (the citadel) last: their player upgrades change what the other buildings' units cost (cost modifiers), measured before
	std::vector<std::string> order = reachableTemplates(r);
	const std::vector<std::string> startOnes = factaStartTemplates(faction);
	std::stable_partition(order.begin(), order.end(), [&](const std::string &n) { return std::find(startOnes.begin(), startOnes.end(), n) == startOnes.end(); });
	for (const std::string &name : order)
	{
		const ThingTemplate *tt = r.logic().things().findTemplate(name);
		if (!isStructure(*tt))
		{
			continue;
		}
		// a wall segment's upgrades replace it (ReplaceSelfUpgrade: the gates, towers, hubs): the fortress test buys them on a span
		static const int wallSegment = ObjectTemplateInfoBuilder::kindOfIndex("WALL_SEGMENT");
		if (MaskTest(ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()).kindOf, (unsigned)wallSegment))
		{
			continue;
		}
		const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*tt));
		bool any = false;
		for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
		{
			const CommandButton *b = set->getCommandButton(slot);
			any = any || (b && (b->m_command == GUI_COMMAND_UNIT_BUILD || b->m_command == GUI_COMMAND_OBJECT_UPGRADE || b->m_command == GUI_COMMAND_PLAYER_UPGRADE));
		}
		if (!any)
		{
			continue;
		}
		++out.producers;
		const Coord3D at = r.spot(x, 1700.0f, 320.0f);
		x = x > 3400.0f ? 1300.0f : x + 400.0f;
		// the castle's keep (the citadel) is made as the start makes it: the PlayerTemplate's starting building unpacked around it, so that what the keep's
		// CastleUpgrade grants reaches the castle's members (RW 0x8BAAC0, RW 0x79986B) and the buttons that need it (Stonework before the Ivory Tower)
		Object *building = nullptr, *castleShell = nullptr;
		if (std::find(startOnes.begin(), startOnes.end(), name) != startOnes.end())
		{
			const ThingTemplate *startTt = r.logic().things().findTemplate(r.local()->getPlayerTemplate()->m_startingBuilding);
			Object *shell = startTt ? r.make(startTt->getName(), r.spot(2400.0f, 1300.0f, 400.0f)) : nullptr;
			CastleBehavior *castle = shell ? dynamic_cast<CastleBehavior *>(shell->findModule("CastleBehavior")) : nullptr;
			if (castle && castle->unpack())
			{
				r.frames(30);
				Object *keep = r.logic().findObjectByID(castle->keepId());
				if (keep && keep->getTemplate()->getName() == name)
				{
					building = keep;
					castleShell = shell;
				}
			}
			if (!castleShell && shell)
			{
				for (ObjectID id : castle ? castle->ownedObjects() : std::vector<ObjectID>())
				{
					if (Object *o = r.logic().findObjectByID(id))
					{
						r.logic().destroyObject(o);
					}
				}
				r.logic().destroyObject(shell);
				r.frames(2);
			}
		}
		if (!building)
		{
			building = r.make(name, at);
		}
		r.frames(3);
		if (!r.select(building))
		{
			out.problems.push_back(faction + " " + name + ": a click does not select it");
		}
		// the set may change as the building levels up (CommandSetUpgrade): walk the set the building shows, again after every upgrade
		std::set<std::string> done;
		std::map<std::string, std::string> pending; // restricted buttons: button -> tag
		for (int round = 0; round < 4; ++round)
		{
			const CommandSet *now = TheCommandStore->findCommandSet(building->getCommandSetName());
			bool progressed = false;
			for (int slot = 0; now && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = now->getCommandButton(slot);
				if (!b || done.count(b->m_name))
				{
					continue;
				}
				const std::string tag = faction + " " + name + " " + b->m_name;
				const bool buys = (b->m_command == GUI_COMMAND_UNIT_BUILD && b->getThingTemplate()) || b->m_command == GUI_COMMAND_OBJECT_UPGRADE
					|| b->m_command == GUI_COMMAND_PLAYER_UPGRADE;
				const ControlBarButton *shownNow = buys ? r.find(b->m_name) : nullptr;
				if (shownNow && shownNow->state == ButtonState::Restricted)
				{
					pending[b->m_name] = tag; // a later level of the building may unlock it
					continue;
				}
				pending.erase(b->m_name);
				if (b->m_command == GUI_COMMAND_UNIT_BUILD && b->getThingTemplate())
				{
					done.insert(b->m_name);
					buildUnit(r, building, *b, out, tag);
					progressed = true;
				}
				else if (b->m_command == GUI_COMMAND_OBJECT_UPGRADE || b->m_command == GUI_COMMAND_PLAYER_UPGRADE)
				{
					done.insert(b->m_name);
					buyUpgrade(r, building, *b, out, tag);
					progressed = true;
				}
			}
			if (!progressed)
			{
				break;
			}
		}
		for (const auto &kv : pending)
		{
			const ControlBarButton *cb = r.find(kv.first);
			// an upgrade the player or the building already has is restricted by design
			const CommandButton *b = TheCommandStore->findCommandButton(kv.first);
			const UpgradeTemplate *u = b && TheUpgradeCenter && !b->m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b->m_upgradeName) : nullptr;
			if (u && (building->hasUpgrade(u) || r.local()->hasUpgradeComplete(u)))
			{
				continue;
			}
			const int make = b && b->getThingTemplate() ? (int)BuildAssistant::canMakeUnit(*building, b->getThingTemplate(), -1) : -1;
			out.problems.push_back(kv.second + ": restricted at every level of the building (state " + std::to_string(cb ? (int)cb->state : -1) + ", canMakeUnit "
				+ std::to_string(make) + ", command points " + std::to_string(r.bar().commandPointsUsed()) + " / " + std::to_string(r.bar().commandPointsLimit()) + ", set "
				+ building->getCommandSetName() + ")");
		}
		if (castleShell)
		{
			const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(castleShell->findModule("CastleBehavior"));
			for (ObjectID id : castle->ownedObjects())
			{
				if (Object *o = r.logic().findObjectByID(id))
				{
					r.logic().destroyObject(o);
				}
			}
			r.logic().destroyObject(castleShell);
		}
		else
		{
			r.logic().destroyObject(building);
		}
		r.frames(2);
	}
	return out;
}
} // namespace

TEST_CASE("FACT-A production: every unit, horde and building upgrade of Men, Elves and Dwarves bought from its control bar; price, time, command points")
{
	if (!hudtest::haveWorld("facta production"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	for (const std::string &f : factions())
	{
		const Outcome o = runFaction(s, f);
		std::printf("facta production %s: %zu buildings, %zu units / hordes built, %zu upgrades bought, %zu problems\n", f.c_str(), o.producers, o.units, o.upgrades,
			o.problems.size());
		for (const std::string &p : o.problems)
		{
			std::printf("facta production problem: %s\n", p.c_str());
		}
		CHECK(o.units > 5);
		CHECK(o.problems.empty());
	}
}

