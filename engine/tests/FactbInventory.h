// OpenBFME unit tests (lane FACT-B). The inventory of one faction, walked from its PlayerTemplate through the retail data the way a player reaches it:
// the starting building and units, the buildable (and ring) heroes, the spell book and the spell store; every CommandSet of a reached template (its own
// CommandSet and the ones its CommandSetUpgrade modules switch to; a horde's InitialPayload members too) and every button of those sets (all 33 slots,
// visible range or not). A UNIT_BUILD / DOZER_CONSTRUCT / FOUNDATION_CONSTRUCT button reaches its template, an upgrade button its upgrade, a special power
// button its power, a science button its sciences; every other button is listed as a command. Nothing here is hard-coded per faction: the walk starts
// from the PlayerTemplate's name (PLAN rule 6: retail facts live in the tests' expectations only).

#pragma once

#include "Common/BuildAssistant.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace factb
{
enum class Kind
{
	Structure,
	Unit,
	Horde,
	Hero,
	Upgrade,
	Power,
	Science,
	Command
};

inline const char *kindName(Kind k)
{
	switch (k)
	{
		case Kind::Structure: return "structure";
		case Kind::Unit: return "unit";
		case Kind::Horde: return "horde";
		case Kind::Hero: return "hero";
		case Kind::Upgrade: return "upgrade";
		case Kind::Power: return "power";
		case Kind::Science: return "science";
		case Kind::Command: return "command";
	}
	return "?";
}

struct Entry
{
	Kind kind = Kind::Command;
	std::string name;
	std::set<std::string> via; ///< "CommandSet:slot:Button" (or "PlayerTemplate:Field") that reach it
	std::string detail;
};

struct Inventory
{
	std::string faction;
	std::map<std::pair<int, std::string>, Entry> items; ///< (kind, name)
	std::vector<std::string> commandSets;              ///< every set walked, in walk order
	std::map<std::string, std::vector<std::string>> setsOfTemplate; ///< template -> its sets
	std::vector<std::string> problems;                 ///< a set / template / upgrade / power the data names that does not exist

	size_t count(Kind k) const
	{
		size_t n = 0;
		for (const auto &kv : items)
		{
			n += kv.first.first == (int)k ? 1 : 0;
		}
		return n;
	}
	std::vector<const Entry *> of(Kind k) const
	{
		std::vector<const Entry *> out;
		for (const auto &kv : items)
		{
			if (kv.first.first == (int)k)
			{
				out.push_back(&kv.second);
			}
		}
		return out;
	}
	const Entry *find(Kind k, const std::string &name) const
	{
		auto it = items.find({ (int)k, name });
		return it == items.end() ? nullptr : &it->second;
	}
};

inline bool hasKindOf(const ThingTemplate &tt, const char *bit)
{
	const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt.getFinalOverride());
	const int i = ObjectTemplateInfoBuilder::kindOfIndex(bit);
	return i >= 0 && MaskTest(info.kindOf, (unsigned)i);
}

inline const ThingTemplate::Nugget *nugget(const ThingTemplate &tt, const char *cls)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == cls)
		{
			return &n;
		}
	}
	return nullptr;
}

inline Kind templateKind(const ThingTemplate &tt)
{
	if (hasKindOf(tt, "STRUCTURE"))
	{
		return Kind::Structure;
	}
	if (hasKindOf(tt, "HERO"))
	{
		return Kind::Hero;
	}
	return nugget(tt, "HordeContain") ? Kind::Horde : Kind::Unit;
}

inline std::string stringFieldOf(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	const std::string *s = v ? std::get_if<std::string>(v) : nullptr;
	return s ? *s : std::string();
}

// the template's sets: its CommandSet field, then the CommandSetUpgrade modules' sets in module order
inline std::vector<std::string> commandSetsOf(const ThingTemplate &tt)
{
	std::vector<std::string> out;
	const std::string own = stringFieldOf(tt, "CommandSet");
	if (!own.empty() && own != "None")
	{
		out.push_back(own);
	}
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (const CommandSetUpgradeModuleData *d = dynamic_cast<const CommandSetUpgradeModuleData *>(n.data.get()))
		{
			if (!d->m_commandSet.empty() && std::find(out.begin(), out.end(), d->m_commandSet) == out.end())
			{
				out.push_back(d->m_commandSet);
			}
		}
	}
	return out;
}

inline std::string templateDetail(const ThingTemplate &tt0)
{
	const ThingTemplate &tt = *tt0.getFinalOverride();
	char buf[160];
	std::snprintf(buf, sizeof(buf), "cost %u, time %gs, CP %d", BuildAssistant::buildCost(tt), (double)BuildAssistant::buildTime(tt), BuildAssistant::commandPoints(tt));
	std::string out = buf;
	// the behaviour that makes it more than a body with a weapon: the special power modules (with their power), the upgrade-triggered modules
	std::map<std::string, int> powers, upgrades;
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (const SpecialPowerModuleData *sp = dynamic_cast<const SpecialPowerModuleData *>(n.data.get()))
		{
			++powers[n.name + "(" + sp->m_specialPowerTemplateName + ")"];
		}
		else if (const UpgradeModuleData *u = dynamic_cast<const UpgradeModuleData *>(n.data.get()))
		{
			// the skirmish AI's difficulty modifiers (Upgrade_<level>AI<kind>Player) are on every object: not listed
			if (n.name == "AttributeModifierUpgrade" && u->m_triggeredBy.size() == 1 && u->m_triggeredBy[0].find("AIMultiPlayer") != std::string::npos)
			{
				continue;
			}
			if (n.name == "AttributeModifierUpgrade" && u->m_triggeredBy.size() == 1 && u->m_triggeredBy[0].find("AISinglePlayer") != std::string::npos)
			{
				continue;
			}
			std::string t;
			for (const std::string &s : u->m_triggeredBy)
			{
				t += (t.empty() ? "" : "|") + s;
			}
			++upgrades[n.name + "<-" + t];
		}
	}
	if (!powers.empty())
	{
		out += "; powers:";
		for (const auto &kv : powers)
		{
			out += " " + kv.first;
		}
	}
	if (!upgrades.empty())
	{
		out += "; upgrade modules:";
		for (const auto &kv : upgrades)
		{
			out += " " + kv.first;
		}
	}
	return out;
}

// `extraSeeds`: (template, via) pairs the caller found another way (the objects the starting castle unpacks: the expansion pads, the keep)
inline Inventory build(RetailObjectWorld &world, const std::string &faction, const std::vector<std::pair<std::string, std::string>> &extraSeeds = {})
{
	Inventory inv;
	inv.faction = faction;
	const PlayerTemplate *pt = world.playerTemplates().findPlayerTemplate(faction);
	if (!pt)
	{
		inv.problems.push_back("no PlayerTemplate " + faction);
		return inv;
	}
	const CommandStore &store = world.commands();
	std::deque<std::string> templates;
	std::set<std::string> seenTemplates, seenSets;
	auto addTemplate = [&](const std::string &name, const std::string &via) {
		if (name.empty() || name == "None")
		{
			return;
		}
		const ThingTemplate *tt = world.things().findTemplate(name);
		if (!tt)
		{
			inv.problems.push_back("template " + name + " (" + via + ") does not exist");
			return;
		}
		Entry &e = inv.items[{ (int)templateKind(*tt), name }];
		e.kind = templateKind(*tt);
		e.name = name;
		e.via.insert(via);
		if (seenTemplates.insert(name).second)
		{
			e.detail = templateDetail(*tt);
			templates.push_back(name);
		}
	};
	auto addEntry = [&](Kind k, const std::string &name, const std::string &via, const std::string &detail) {
		Entry &e = inv.items[{ (int)k, name }];
		e.kind = k;
		e.name = name;
		e.via.insert(via);
		if (e.detail.empty())
		{
			e.detail = detail;
		}
	};
	auto walkSet = [&](const std::string &setName) {
		if (!seenSets.insert(setName).second)
		{
			return;
		}
		const CommandSet *set = store.findCommandSet(setName);
		if (!set)
		{
			inv.problems.push_back("command set " + setName + " does not exist");
			return;
		}
		inv.commandSets.push_back(setName);
		for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
		{
			const CommandButton *b0 = set->getCommandButton(slot);
			if (!b0)
			{
				continue;
			}
			const CommandButton &b = *b0->getFinalOverride();
			const std::string via = setName + ":" + std::to_string(slot + 1) + ":" + b.m_name;
			switch (b.m_command)
			{
				case GUI_COMMAND_UNIT_BUILD:
				case GUI_COMMAND_SECONDARY_UNIT_BUILD:
				case GUI_COMMAND_DOZER_CONSTRUCT:
				case GUI_COMMAND_FOUNDATION_CONSTRUCT:
				case GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT:
					if (b.getThingTemplate())
					{
						addTemplate(b.getThingTemplate()->getName(), via);
					}
					else if (!b.m_objectName.empty())
					{
						addTemplate(b.m_objectName, via);
					}
					break;
				case GUI_COMMAND_PLAYER_UPGRADE:
				case GUI_COMMAND_OBJECT_UPGRADE:
				case GUI_COMMAND_CASTLE_UPGRADE:
				{
					const UpgradeTemplate *u = world.upgrades().findUpgrade(b.m_upgradeName);
					if (!u)
					{
						inv.problems.push_back("upgrade " + b.m_upgradeName + " (" + via + ") does not exist");
						break;
					}
					char buf[120];
					std::snprintf(buf, sizeof(buf), "%s, cost %d, time %gs", u->getUpgradeType() == UPGRADE_TYPE_PLAYER ? "PLAYER" : "OBJECT", u->getBuildCost(),
								  (double)u->getBuildTimeSeconds());
					addEntry(Kind::Upgrade, b.m_upgradeName, via, buf);
					break;
				}
				case GUI_COMMAND_SPECIAL_POWER:
				case GUI_COMMAND_SPECIAL_POWER_FROM_COMMAND_CENTER:
				case GUI_COMMAND_SPECIAL_POWER_TOGGLE:
				case GUI_COMMAND_SPELL_BOOK:
				{
					const SpecialPowerTemplate *sp = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
					if (!sp)
					{
						inv.problems.push_back("special power " + b.m_specialPowerName + " (" + via + ") does not exist");
						break;
					}
					char buf[160];
					std::snprintf(buf, sizeof(buf), "%s, type %d, reload %u frames, options 0x%X", GUICommandName(b.m_command), sp->getSpecialPowerType(), sp->getReloadTime(),
								  b.m_options);
					addEntry(Kind::Power, b.m_specialPowerName, via, buf);
					break;
				}
				case GUI_COMMAND_PURCHASE_SCIENCE:
					for (const std::string &s : b.m_science)
					{
						addEntry(Kind::Science, s, via, "PURCHASE_SCIENCE");
					}
					break;
				default:
					addEntry(Kind::Command, b.m_name, via, GUICommandName(b.m_command));
					break;
			}
		}
	};

	addTemplate(pt->m_startingBuilding, faction + ":StartingBuilding");
	for (int i = 0; i < PlayerTemplate::NUM_STARTING_UNITS; ++i)
	{
		addTemplate(pt->m_startingUnit[i], faction + ":StartingUnit" + std::to_string(i));
	}
	for (const std::string &h : pt->m_buildableHeroesMP)
	{
		if (h != "CreateAHero") // the placeholder of the Create-A-Hero record (HERO-2), not a template
		{
			addTemplate(h, faction + ":BuildableHeroesMP");
		}
	}
	for (const std::string &h : pt->m_buildableRingHeroesMP)
	{
		addTemplate(h, faction + ":BuildableRingHeroesMP");
	}
	addTemplate(pt->m_spellBookMp, faction + ":SpellBookMp");
	for (const auto &seed : extraSeeds)
	{
		addTemplate(seed.first, seed.second);
	}
	walkSet(pt->m_purchaseScienceCommandSetMP);
	while (!templates.empty())
	{
		const std::string name = templates.front();
		templates.pop_front();
		const ThingTemplate *tt = world.things().findTemplate(name);
		inv.setsOfTemplate[name] = commandSetsOf(*tt);
		for (const std::string &s : inv.setsOfTemplate[name])
		{
			walkSet(s);
		}
		if (const ThingTemplate::Nugget *n = nugget(*tt, "HordeContain"))
		{
			if (const HordeContainBehaviorData *hd = dynamic_cast<const HordeContainBehaviorData *>(n->data.get()))
			{
				for (const InitialPayloadEntry &p : hd->horde.m_transport.m_initialPayload)
				{
					addTemplate(p.name, name + ":InitialPayload");
				}
			}
		}
	}
	return inv;
}

// the behaviour module classes of the inventory's templates (members included) that have no runtime of their own: the UnportedModule (missing or
// parsed only) or the base AIUpdateInterface standing in for an AI class (S-222). class -> the templates using it
inline std::map<std::string, std::set<std::string>> unportedModules(RetailObjectWorld &world, const Inventory &inv)
{
	std::map<std::string, std::set<std::string>> out;
	const ModuleFactory::ModuleTemplate *ai = world.modules().findModuleTemplate("AIUpdateInterface", MODULETYPE_BEHAVIOR);
	for (const auto &kv : inv.items)
	{
		if (kv.first.first > (int)Kind::Hero)
		{
			continue;
		}
		const ThingTemplate *tt = world.things().findTemplate(kv.second.name);
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
		{
			const ModuleFactory::ModuleTemplate *mt = world.modules().findModuleTemplate(n.name, MODULETYPE_BEHAVIOR);
			if (!mt)
			{
				continue;
			}
			const bool baseAi = mt->portedModule && ai && mt->isAiModuleData && mt->name != "AIUpdateInterface" && mt->name != "HordeAIUpdate"
								&& mt->newModule.target_type() == ai->newModule.target_type();
			if (!mt->portedModule || baseAi)
			{
				out[n.name + (baseAi ? " (base AI)" : mt->typed ? " (parsed only)" : "")].insert(kv.second.name);
			}
		}
	}
	return out;
}

// the inventory as a markdown section (docs/FACTB.md is generated from it)
inline std::string markdown(const Inventory &inv)
{
	std::string out = "## " + inv.faction + "\n\n";
	char buf[200];
	std::snprintf(buf, sizeof(buf), "%zu structures, %zu units, %zu hordes, %zu heroes, %zu upgrades, %zu powers, %zu sciences, %zu other commands; %zu command sets.\n\n",
				  inv.count(Kind::Structure), inv.count(Kind::Unit), inv.count(Kind::Horde), inv.count(Kind::Hero), inv.count(Kind::Upgrade), inv.count(Kind::Power),
				  inv.count(Kind::Science), inv.count(Kind::Command), inv.commandSets.size());
	out += buf;
	for (Kind k : { Kind::Structure, Kind::Horde, Kind::Unit, Kind::Hero, Kind::Upgrade, Kind::Power, Kind::Science, Kind::Command })
	{
		out += std::string("### ") + kindName(k) + "\n\n| name | data | reached from |\n|---|---|---|\n";
		for (const Entry *e : inv.of(k))
		{
			std::string via;
			size_t n = 0;
			for (const std::string &v : e->via)
			{
				if (n++ < 3)
				{
					via += (via.empty() ? "" : "<br>") + v;
				}
			}
			if (e->via.size() > 3)
			{
				via += "<br>(+" + std::to_string(e->via.size() - 3) + ")";
			}
			out += "| " + e->name + " | " + e->detail + " | " + via + " |\n";
		}
		out += "\n";
	}
	return out;
}
} // namespace factb
