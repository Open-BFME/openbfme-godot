// OpenBFME unit tests. GPL-3.0.
// Lane FACT-C: the shared part of the Goblins (FactionWild) / Angmar sweep tests. The inventory walks the faction's content the way a player reaches it: from the
// PlayerTemplate's starting building, starting units, buildable heroes and spell store command set, every CommandSet an object can show (its own and the ones its
// CommandSetUpgrade modules switch to) and every button of those sets (DOZER_CONSTRUCT / UNIT_BUILD lead to more objects). The HUD rig is the HUD-1 Rig with an
// InGameHud on it: selection by a click, presses of the control bar's buttons and the target click of a GUI command mode, as a player does it.

#pragma once

#include "HudTestUtil.h"

#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/ExperienceTracker.h"

#include <map>
#include <set>
#include <sstream>

namespace factc
{
// one button of a command set an object of the faction shows
struct Offer
{
	std::string object, set, button;
	int slot = -1;
	const CommandButton *cb = nullptr;
};

struct Inventory
{
	std::string faction;
	std::map<std::string, std::vector<std::string>> setsOf;       // object -> the command sets it can show
	std::set<std::string> structures, units, heroes;               // DOZER_CONSTRUCT targets (+ the starting building), UNIT_BUILD targets, BuildableHeroesMP
	std::vector<Offer> offers;                                      // every (object, set, slot) button
	std::set<std::string> objectUpgrades, playerUpgrades, powers, sciences;
	std::vector<std::string> problems;                              // references that do not resolve
};

inline const std::string *stringField(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	return v ? std::get_if<std::string>(v) : nullptr;
}

// the template's CommandSet and the CommandSetUpgrade modules' CommandSet
inline std::vector<std::string> commandSetsOf(const ThingTemplate &tt)
{
	std::vector<std::string> out;
	auto add = [&](const std::string &n) {
		if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end())
		{
			out.push_back(n);
		}
	};
	if (const std::string *own = stringField(tt, "CommandSet"))
	{
		add(*own);
	}
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (const CommandSetUpgradeModuleData *d = dynamic_cast<const CommandSetUpgradeModuleData *>(n.data.get()))
		{
			add(d->m_commandSet);
		}
	}
	return out;
}

// `seeds`: more objects to start from (the pieces a started castle unpacks)
inline Inventory crawl(RetailObjectWorld &w, const std::string &faction, const std::vector<std::string> &seeds = {})
{
	Inventory inv;
	inv.faction = faction;
	const PlayerTemplate *pt = nullptr;
	for (int i = 0; i < w.playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *p = w.playerTemplates().getNthPlayerTemplate(i);
		pt = (p && p->getName() == faction) ? p : pt;
	}
	REQUIRE_MESSAGE(pt != nullptr, faction);
	std::vector<std::string> objects;
	std::vector<std::pair<std::string, std::string>> sets; // (object, set)
	auto addObject = [&](const std::string &n) {
		if (!n.empty() && !inv.setsOf.count(n))
		{
			inv.setsOf[n];
			objects.push_back(n);
		}
	};
	inv.structures.insert(pt->m_startingBuilding);
	addObject(pt->m_startingBuilding);
	for (const std::string &u : pt->m_startingUnit)
	{
		addObject(u);
	}
	for (const std::string &h : pt->m_buildableHeroesMP)
	{
		if (h != "CreateAHero")
		{
			inv.heroes.insert(h);
			addObject(h);
		}
	}
	addObject(pt->m_spellBookMp);
	for (const std::string &o : seeds)
	{
		addObject(o);
	}
	sets.push_back({ "<spell store>", pt->m_purchaseScienceCommandSetMP });
	std::set<std::pair<std::string, std::string>> seen;
	while (!objects.empty() || !sets.empty())
	{
		if (!objects.empty())
		{
			const std::string o = objects.back();
			objects.pop_back();
			const ThingTemplate *tt = w.things().findTemplate(o);
			if (!tt)
			{
				inv.problems.push_back("object " + o + " is no template");
				continue;
			}
			inv.setsOf[o] = commandSetsOf(*tt);
			for (const std::string &s : inv.setsOf[o])
			{
				sets.push_back({ o, s });
			}
			continue;
		}
		const auto os = sets.back();
		sets.pop_back();
		if (!seen.insert(os).second)
		{
			continue;
		}
		const CommandSet *set = w.commands().findCommandSet(os.second);
		if (!set)
		{
			inv.problems.push_back("command set " + os.second + " of " + os.first + " is unknown");
			continue;
		}
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b = set->getCommandButton(i);
			if (!b)
			{
				continue;
			}
			inv.offers.push_back({ os.first, os.second, b->m_name, i, b });
			const ThingTemplate *t = b->getThingTemplate();
			switch (b->m_command)
			{
			case GUI_COMMAND_DOZER_CONSTRUCT:
			case GUI_COMMAND_FOUNDATION_CONSTRUCT:
				if (t)
				{
					inv.structures.insert(t->getName());
					addObject(t->getName());
				}
				break;
			case GUI_COMMAND_UNIT_BUILD:
				if (t)
				{
					inv.units.insert(t->getName());
					addObject(t->getName());
				}
				break;
			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_CASTLE_UPGRADE:
				(b->m_command == GUI_COMMAND_PLAYER_UPGRADE ? inv.playerUpgrades : inv.objectUpgrades).insert(b->m_upgradeName);
				if (!w.upgrades().findUpgrade(b->m_upgradeName))
				{
					inv.problems.push_back("button " + b->m_name + ": upgrade " + b->m_upgradeName + " is unknown");
				}
				break;
			case GUI_COMMAND_SPECIAL_POWER:
			case GUI_COMMAND_SPECIAL_POWER_TOGGLE:
			case GUI_COMMAND_SPECIAL_POWER_FROM_COMMAND_CENTER:
				inv.powers.insert(b->m_specialPowerName);
				if (!w.spellStores().specialPowers().findSpecialPowerTemplate(b->m_specialPowerName))
				{
					inv.problems.push_back("button " + b->m_name + ": special power " + b->m_specialPowerName + " is unknown");
				}
				break;
			case GUI_COMMAND_PURCHASE_SCIENCE:
				for (const std::string &sc : b->m_science)
				{
					inv.sciences.insert(sc);
				}
				break;
			default:
				break;
			}
		}
	}
	return inv;
}

inline std::string describe(const Inventory &inv)
{
	std::ostringstream out;
	auto list = [&](const char *what, const std::set<std::string> &s) {
		out << what << " (" << s.size() << "):";
		for (const std::string &n : s)
		{
			out << ' ' << n;
		}
		out << '\n';
	};
	list("structures", inv.structures);
	list("units", inv.units);
	list("heroes", inv.heroes);
	list("object upgrades", inv.objectUpgrades);
	list("player upgrades", inv.playerUpgrades);
	list("powers", inv.powers);
	list("sciences", inv.sciences);
	return out.str();
}

// the HUD on a HUD-1 rig of `faction` (against Mordor)
struct HudRig
{
	hudtest::Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	HudRig(hudtest::SharedWorld &s, const char *faction, const char *map = "map mp fall back 4p") : rig(s, map, faction)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
		// RotWK's default mouse setup (RW 0x642A4B, lane PLAY-1): the right click orders, a GUI command's target is a left click
		hud->input().commandTranslator().setUseAlternateMouse(true);
	}
	~HudRig() { hud.reset(); }
	GameLogic &logic() { return rig.logic(); }
	// money accounting that the economy's income (mines, the castle) does not disturb: the score keeper's money spent, else the bank's change
	struct Purse
	{
		std::uint32_t money = 0, spent = 0;
	};
	Purse purse() const { return { rig.local->getMoney()->countMoney(), rig.local->getScoreKeeper().moneySpent() }; }
	int paidSince(const Purse &p) const
	{
		const std::uint32_t spent = rig.local->getScoreKeeper().moneySpent() - p.spent;
		return spent != 0 ? (int)spent : (int)(p.money - rig.local->getMoney()->countMoney());
	}
	// an object as a finished build makes it (the create modules' onBuildComplete, as the starting base's placeObjectAtPosition RW 0x629DD9 does: a castle
	// unpacks its layout)
	Object *makeBuilt(const std::string &name, float x, float y, Player *owner = nullptr)
	{
		Object *o = rig.make(name, x, y, owner);
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if (CreateModuleInterface *create = m->getCreate())
			{
				create->onBuildComplete();
			}
		}
		return o;
	}
	// n logic frames (one HUD update each)
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.2);
			rig.game->advance(0.2);
		}
	}
	// no GUI command mode, nothing selected (a player's Escape and a click on empty ground)
	// PLAYER upgrades a run granted (or researched): taken back before the next run, so one item's grants do not decide another's button state
	std::vector<const UpgradeTemplate *> granted;
	void reset()
	{
		for (const UpgradeTemplate *u : granted)
		{
			rig.local->removeUpgrade(u);
		}
		granted.clear();
		hud->input().ui().setGUICommand(nullptr);
		hud->input().ui().deselectAll();
		frames(1);
	}
	void clickAt(const Coord3D &p, bool right = false)
	{
		const ICoord2D px = rig.screenOf(p);
		const HudInput::Button b = right ? HudInput::Button::Right : HudInput::Button::Left;
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(b, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(b, false, px.x, px.y, 0, rig.timeMs += 40);
	}
	// a left click on the object (the selection goes to the logic as MSG_CREATE_SELECTED_GROUP)
	bool select(Object *o)
	{
		// a horde is picked through a member; members that move (a wolf pack) may slip from under the pointer: up to four tries, each on another member
		std::vector<Object *> picks{ o };
		if (o->getContain() && o->getContain()->getContainedItemsList() && !o->getContain()->getContainedItemsList()->empty() && o->findModule("HordeContain"))
		{
			picks.assign(o->getContain()->getContainedItemsList()->begin(), o->getContain()->getContainedItemsList()->end());
		}
		for (size_t k = 0; k < picks.size() && k < 4; ++k)
		{
			Object *pick = picks[k];
			rig.lookAt(*pick->getPosition());
			clickAt({ pick->getPosition()->x, pick->getPosition()->y, pick->getPosition()->z + 8.0f });
			frames(2);
			const std::vector<ObjectID> &sel = hud->input().ui().selected();
			if (std::find(sel.begin(), sel.end(), o->getID()) != sel.end() || std::find(sel.begin(), sel.end(), pick->getID()) != sel.end())
			{
				return true;
			}
		}
		// a structure with something standing on it (the turret an ObjectCreationUpgrade put on an expansion, RW 0x8B8464): clicks on its sides
		for (const Coord3D &d : { Coord3D{ 35.0f, 0.0f, 0.0f }, Coord3D{ -35.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 35.0f, 0.0f }, Coord3D{ 0.0f, -35.0f, 0.0f } })
		{
			rig.lookAt(*o->getPosition());
			clickAt({ o->getPosition()->x + d.x, o->getPosition()->y + d.y, o->getPosition()->z + 8.0f });
			frames(2);
			const std::vector<ObjectID> &sel = hud->input().ui().selected();
			if (std::find(sel.begin(), sel.end(), o->getID()) != sel.end())
			{
				return true;
			}
		}
		// a box drag around the first member (a pack that keeps moving under the pointer)
		Object *pick = picks.front();
		rig.lookAt(*pick->getPosition());
		const ICoord2D c = rig.screenOf({ pick->getPosition()->x, pick->getPosition()->y, pick->getPosition()->z });
		hud->mouseMove(c.x - 60, c.y - 60);
		hud->mouseButton(HudInput::Button::Left, true, c.x - 60, c.y - 60, 0, rig.timeMs += 500);
		for (int i = 1; i <= 6; ++i)
		{
			hud->mouseMove(c.x - 60 + 20 * i, c.y - 60 + 20 * i);
		}
		hud->mouseButton(HudInput::Button::Left, false, c.x + 60, c.y + 60, 0, rig.timeMs += 200);
		frames(2);
		const std::vector<ObjectID> &sel = hud->input().ui().selected();
		return std::find(sel.begin(), sel.end(), o->getID()) != sel.end();
	}
	std::vector<ControlBarButton> allButtons()
	{
		std::vector<ControlBarButton> out;
		for (const auto *v : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const ControlBarButton &b : *v)
			{
				bool dup = false;
				for (const ControlBarButton &o : out)
				{
					dup = dup || o.slot == b.slot;
				}
				if (b.button && !dup)
				{
					out.push_back(b);
				}
			}
		}
		return out;
	}
	// the shown button `name`, on the root page of the set or on a page a PUSH_VISIBLE_COMMAND_RANGE button of the root opens (each tried in turn; a
	// POP_VISIBLE_COMMAND_RANGE button, or a fresh click on the source, goes back)
	bool findButton(const std::string &name, ControlBarButton &out)
	{
		return findButtonIf([&](const ControlBarButton &b) { return b.button->m_name == name; }, out);
	}
	template <class Pred> bool findButtonIf(Pred pred, ControlBarButton &out)
	{
		auto look = [&]() {
			for (const ControlBarButton &b : allButtons())
			{
				if (pred(b))
				{
					out = b;
					return true;
				}
			}
			return false;
		};
		if (look())
		{
			return true;
		}
		std::vector<int> pushSlots;
		for (const ControlBarButton &b : allButtons())
		{
			if (b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
			{
				pushSlots.push_back(b.slot);
			}
		}
		for (int slot : pushSlots)
		{
			bool pressed = false;
			for (const ControlBarButton &b : allButtons())
			{
				if (!pressed && b.slot == slot && b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
				{
					pressed = hud->controlBar().pressButton(b.slot, b.inPalantir);
				}
			}
			frames(1);
			if (pressed && look())
			{
				return true;
			}
			bool popped = false;
			for (const ControlBarButton &b : allButtons())
			{
				if (!popped && b.button->m_command == GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE)
				{
					popped = hud->controlBar().pressButton(b.slot, b.inPalantir);
				}
			}
			frames(1);
		}
		return false;
	}
};

inline const char *stateName(ButtonState s)
{
	switch (s)
	{
	case ButtonState::Hidden: return "hidden";
	case ButtonState::Enabled: return "enabled";
	case ButtonState::Restricted: return "restricted";
	case ButtonState::CantAfford: return "cant-afford";
	case ButtonState::NotReady: return "not-ready";
	case ButtonState::Active: return "active";
	case ButtonState::Purchased: return "purchased";
	}
	return "?";
}

// every object of the horde (the horde and its members) or the object
inline std::vector<Object *> withMembers(Object *o)
{
	std::vector<Object *> out{ o };
	if (o->getContain() && o->getContain()->getContainedItemsList())
	{
		for (Object *m : *o->getContain()->getContainedItemsList())
		{
			out.push_back(m);
		}
	}
	return out;
}

inline std::vector<SpecialPowerModuleInterface *> powerModules(Object *o, const std::string &power)
{
	std::vector<SpecialPowerModuleInterface *> out;
	for (Object *x : withMembers(o))
	{
		for (const auto &m : x->modules())
		{
			SpecialPowerModuleInterface *sp = m->getSpecialPower();
			if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == power)
			{
				out.push_back(sp);
			}
		}
	}
	return out;
}
} // namespace factc
