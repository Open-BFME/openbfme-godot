// OpenBFME retail tests for lane FACT-A: the inventory of Men (Gondor), Elves (Lorien / Rivendell) and Dwarves as the data offers it to a player. From the
// player's start (the fortress, its pads, the builders) and the faction's PlayerTemplate (buildable heroes, the ring hero), every command set is walked: what a
// button builds is made and its own command set walked in turn; so are the command sets an upgrade switches to (CommandSetUpgrade) and the objects the
// modules make or lay (ObjectCreationUpgrade, ReplaceSelfUpgrade: the gates and towers, WallHubBehavior: the wall segments). Each object is selected through the HUD and every button of its set looked for on the control
// bar (the arc, the side bar, the pages). SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"

#include "Common/Thing/ThingTemplate.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace facta;

namespace
{
struct Item
{
	std::string name;
	std::string set;
	unsigned cost = 0;
	float time = 0;
	int cp = 0;
	std::vector<std::string> unported;
	std::vector<std::string> upgradeSets; // the sets a CommandSetUpgrade switches to: their buttons as data (the upgrade is not bought here)
	std::vector<std::string> linked;      // the objects its modules make or lay (ObjectCreationUpgrade, ReplaceSelfUpgrade, WallHubBehavior)
	bool selectable = false;
	struct Btn
	{
		int slot;
		std::string name, command, target;
		ButtonState state;
		bool shown;
	};
	std::vector<Btn> buttons;
};

const char *stateName(ButtonState s)
{
	switch (s)
	{
		case ButtonState::Hidden: return "hidden";
		case ButtonState::Enabled: return "enabled";
		case ButtonState::Restricted: return "restricted";
		case ButtonState::CantAfford: return "cantafford";
		case ButtonState::NotReady: return "notready";
		case ButtonState::Active: return "active";
		case ButtonState::Purchased: return "purchased";
	}
	return "?";
}

std::vector<Item> walk(SharedWorld &s, const std::string &faction)
{
	FactRig r(s, faction.c_str());
	r.local()->depositMoney(1000000);
	std::deque<std::string> todo;
	std::set<std::string> seen;
	auto push = [&](const std::string &n) {
		if (!n.empty() && seen.insert(n).second)
		{
			todo.push_back(n);
		}
	};
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == r.local())
		{
			push(o->getTemplate()->getName());
		}
	}
	const PlayerTemplate *pt = r.local()->getPlayerTemplate();
	REQUIRE(pt);
	push(pt->m_startingBuilding); // the test map's start places no base: the PlayerTemplate names it
	for (const std::string &n : factaStartTemplates(faction)) // what a real skirmish start gives the player (the castle unpacked: citadel, pads)
	{
		push(n);
	}
	for (const std::string &u : pt->m_startingUnit)
	{
		push(u);
	}
	for (const std::string &h : pt->m_buildableHeroesMP)
	{
		push(h);
	}
	for (const std::string &h : pt->m_buildableRingHeroesMP)
	{
		push(h);
	}
	std::vector<Item> items;
	const CommandStore &store = s.world->commands();
	float x = 1200.0f;
	while (!todo.empty())
	{
		const std::string name = todo.front();
		todo.pop_front();
		const ThingTemplate *tt = r.logic().things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Item it;
		it.name = name;
		it.cost = BuildAssistant::buildCost(*tt);
		it.time = BuildAssistant::buildTime(*tt);
		it.cp = BuildAssistant::commandPoints(*tt);
		const Coord3D at = r.spot(x, 1500.0f, 260.0f);
		x = x > 3600.0f ? 1200.0f : x + 300.0f;
		std::string err;
		Object *o = r.rig.game->createObject(name, r.local()->getPlayerIndex(), at, 0.0f, &err);
		REQUIRE_MESSAGE(o, std::string(name + ": " + err));
		r.frames(3);
		// what the object made around itself (a fortress's expansion pads) is reachable too
		for (Object *m = r.logic().getFirstObject(); m; m = m->getNextObject())
		{
			if (m != o && m->getControllingPlayer() == r.local() && m->getID() > o->getID())
			{
				push(m->getTemplate()->getName());
			}
		}
		it.unported = unportedModules(*o);
		it.set = o->getCommandSetName();
		const CommandSet *set = it.set.empty() ? nullptr : store.findCommandSet(it.set);
		if (set)
		{
			it.selectable = r.select(o);
			for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (!b)
				{
					continue;
				}
				Item::Btn bt{ slot, b->m_name, commandName(b->m_command), std::string(), ButtonState::Hidden, false };
				if (b->getThingTemplate())
				{
					bt.target = b->getThingTemplate()->getName();
				}
				else if (!b->m_upgradeName.empty())
				{
					bt.target = b->m_upgradeName;
				}
				else if (!b->m_specialPowerName.empty())
				{
					bt.target = b->m_specialPowerName;
				}
				if (const ControlBarButton *cb = r.find(b->m_name))
				{
					bt.shown = true;
					bt.state = cb->state;
				}
				it.buttons.push_back(bt);
				if ((b->m_command == GUI_COMMAND_UNIT_BUILD || b->m_command == GUI_COMMAND_DOZER_CONSTRUCT || b->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT)
					&& b->getThingTemplate())
				{
					push(b->getThingTemplate()->getName());
				}
			}
		}
		// what the modules lead to: the upgrade's command sets (their build buttons' products are reached too) and the objects made or laid
		const ModuleLinks links = linkedByModules(*tt);
		it.upgradeSets = links.sets;
		it.linked = links.templates;
		for (const std::string &setName : links.sets)
		{
			const CommandSet *us = store.findCommandSet(setName);
			for (int slot = 0; us && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = us->getCommandButton(slot);
				if (b && b->getThingTemplate()
					&& (b->m_command == GUI_COMMAND_UNIT_BUILD || b->m_command == GUI_COMMAND_DOZER_CONSTRUCT || b->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT))
				{
					push(b->getThingTemplate()->getName());
				}
			}
		}
		for (const std::string &t : links.templates)
		{
			if (r.logic().things().findTemplate(t))
			{
				push(t);
			}
		}
		r.logic().destroyObject(o);
		r.frames(2);
		items.push_back(it);
	}
	return items;
}
} // namespace

TEST_CASE("FACT-A inventory: Men, Elves and Dwarves, every object a player reaches from the start and every button of its command set, as the HUD shows it")
{
	if (!hudtest::haveWorld("facta inventory"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	for (const std::string &f : factions())
	{
		const std::vector<Item> items = walk(s, f);
		size_t buttons = 0, upgradeButtons = 0;
		std::map<std::string, size_t> byCommand;
		for (const Item &it : items)
		{
			std::string unp;
			for (const std::string &u : it.unported)
			{
				unp += " " + u;
			}
			std::printf("facta %s %s cost %u time %.1f cp %d set %s%s unported [%s ]\n", f.c_str(), it.name.c_str(), it.cost, it.time, it.cp, it.set.c_str(),
				it.selectable ? "" : " (not selected by a click)", unp.c_str());
			for (const Item::Btn &b : it.buttons)
			{
				++buttons;
				++byCommand[b.command];
				std::printf("facta   %2d %-55s %-24s %-40s %s\n", b.slot + 1, b.name.c_str(), b.command.c_str(), b.target.c_str(), b.shown ? stateName(b.state) : "NOT-SHOWN");
			}
			for (const std::string &us : it.upgradeSets)
			{
				const CommandSet *set = s.world->commands().findCommandSet(us);
				std::printf("facta   upgrade set %s\n", us.c_str());
				for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
				{
					if (const CommandButton *b = set->getCommandButton(slot))
					{
						++upgradeButtons;
						std::printf("facta   %2d %-55s %-24s %s\n", slot + 1, b->m_name.c_str(), commandName(b->m_command),
							b->getThingTemplate() ? b->getThingTemplate()->getName().c_str() : (!b->m_upgradeName.empty() ? b->m_upgradeName.c_str() : b->m_specialPowerName.c_str()));
					}
				}
			}
			for (const std::string &l : it.linked)
			{
				std::printf("facta   makes or lays %s\n", l.c_str());
			}
		}
		std::printf("facta %s: %zu objects, %zu buttons, %zu buttons of upgrade command sets\n", f.c_str(), items.size(), buttons, upgradeButtons);
		for (const auto &kv : byCommand)
		{
			std::printf("facta %s   %s x%zu\n", f.c_str(), kv.first.c_str(), kv.second);
		}
		CHECK(items.size() > 20);
	}
}
