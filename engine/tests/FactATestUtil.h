// OpenBFME unit tests. GPL-3.0.
// Lane FACT-A (Men / Elves / Dwarves, every unit, hero, upgrade and power works as in retail): the rig. A retail skirmish (HudTestUtil's Rig) with the in-game
// HUD booted on it; objects are selected and their command buttons pressed through the control bar exactly as a player's click does (ControlBar::pressButton,
// the HUD's message stream, the translators, the lockstep command list, the logic's dispatch), targets are clicked on the tactical view. The faction's content
// is found by walking the data from the player's start (no template list in the tests: the inventory is what the data offers).
// The retail tests SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#pragma once

#include "HudTestUtil.h"
#include "FactAStart.h"

#include "Common/BuildAssistant.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include <cmath>
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/WallSpan.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace facta
{
using hudtest::SharedWorld;

// the three factions of the lane, as the player picks them in the lobby
// (FACTA_ONLY=<faction> keeps one, for a focused run)
inline const std::vector<std::string> &factions()
{
	static const std::vector<std::string> f = []() {
		std::vector<std::string> all = { "FactionMen", "FactionElves", "FactionDwarves" };
		if (const char *only = std::getenv("FACTA_ONLY"))
		{
			return std::vector<std::string>{ only };
		}
		return all;
	}();
	return f;
}

// the started game of FactAStart.cpp runs in its own world and switches the process-wide stores while it runs: it is made before the rig
struct FactStartWarm
{
	explicit FactStartWarm(const char *faction) { (void)factaStartTemplates(faction); }
};

struct FactRig
{
	FactStartWarm warm;
	hudtest::Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	std::unique_ptr<RetailObjectWorld::ContextScope> context;

	FactRig(SharedWorld &s, const char *faction) : warm(faction), rig(s, "map mp fall back 4p", faction)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
		context = hud->enterContext();
	}
	~FactRig()
	{
		context.reset();
		hud.reset();
	}
	GameLogic &logic() { return rig.logic(); }
	Player *local() { return rig.local; }
	// n logic frames (a client tick of 0.2 s is one logic frame), the HUD updated before each
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			(void)rig.game->logic(); // waits for the logic worker: the HUD skips its update while a frame runs (SMOOTH-1)
			hud->update(0.1);        // sends what the input queued
			rig.game->advance(0.2);
			(void)rig.game->logic();
			hud->update(0.1); // the control bar shows the frame's result
		}
	}
	// the selection of the HUD and of the logic (the group message the selection translator would send)
	// where a player clicks to hit the object: a horde draws no model of its own, its soldiers do (its first member's position)
	static Coord3D clickPoint(const Object &o)
	{
		const Object *body = &o;
		if (ContainModuleInterface *c = o.getContain())
		{
			if (c->getHordeContainInterface() && c->getContainedItemsList() && !c->getContainedItemsList()->empty())
			{
				body = c->getContainedItemsList()->front();
			}
		}
		Coord3D p = *body->getPosition();
		p.z += 8.0f; // the body, not the ground under its feet
		return p;
	}
	// true when both the HUD's and the logic's selection are the object alone (a stealthed or unselectable object is not picked). The click aims at the body
	// (clickPoint), then a little beside it (a model with a gap at its centre, a catapult's frame)
	bool select(Object *o)
	{
		static const float offsets[][2] = { { 0, 0 }, { 18, 0 }, { -18, 0 }, { 0, 18 }, { 0, -18 }, { 35, 35 }, { -35, 35 }, { 35, -35 }, { -35, -35 } };
		for (const auto &d : offsets)
		{
			// a left click on the object, as the player selects it (the selection translator sends the group message)
			hud->input().ui().deselectAll();
			Coord3D at = clickPoint(*o);
			at.x += d[0];
			at.y += d[1];
			rig.lookAt(at);
			frames(1);
			const ICoord2D p = rig.screenOf(at);
			hud->mouseMove(p.x, p.y, 0);
			rig.timeMs += 300;
			hud->mouseButton(HudInput::Button::Left, true, p.x, p.y, 0, rig.timeMs);
			rig.timeMs += 40;
			hud->mouseButton(HudInput::Button::Left, false, p.x, p.y, 0, rig.timeMs);
			rig.timeMs += 300;
			frames(2);
			const std::vector<ObjectID> &ui = hud->input().ui().selected();
			const std::vector<ObjectID> &logicSel = rig.local->selection();
			if (ui.size() == 1 && ui.front() == o->getID() && logicSel.size() == 1 && logicSel.front() == o->getID())
			{
				resetRange();
				return true;
			}
		}
		return false;
	}
	void resetRange()
	{
		for (int i = 0; i < 4; ++i)
		{
			const ControlBarButton *pop = nullptr;
			for (const auto *list : { &bar().palantirButtons(), &bar().sideButtons() })
			{
				for (const ControlBarButton &b : *list)
				{
					pop = (!pop && b.button && b.button->m_command == GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE) ? &b : pop;
				}
			}
			if (!pop || bar().visibleRangeStart() == 0)
			{
				return;
			}
			bar().pressButton(pop->slot, pop->inPalantir);
			frames(1);
		}
	}
	ControlBar &bar() { return hud->controlBar(); }
	// every button the bar shows now (arc, side bar, off the bar)
	std::vector<ControlBarButton> shown()
	{
		std::vector<ControlBarButton> out;
		for (const auto *list : { &bar().palantirButtons(), &bar().sideButtons(), &bar().offBarButtons() })
		{
			out.insert(out.end(), list->begin(), list->end());
		}
		return out;
	}
	// the shown button whose CommandButton is named `name`, opening the set's pages (PUSH_VISIBLE_COMMAND_RANGE) when it is on one; null when no page shows it
	const ControlBarButton *find(const std::string &name)
	{
		auto look = [&]() -> const ControlBarButton * {
			for (const auto *list : { &bar().palantirButtons(), &bar().sideButtons(), &bar().offBarButtons() })
			{
				for (const ControlBarButton &b : *list)
				{
					if (b.button && b.button->m_name == name)
					{
						return &b;
					}
				}
			}
			return nullptr;
		};
		resetRange();
		if (const ControlBarButton *b = look())
		{
			return b;
		}
		std::vector<std::pair<int, bool>> pushes;
		for (const auto *list : { &bar().palantirButtons(), &bar().sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button && b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
				{
					pushes.push_back({ b.slot, b.inPalantir });
				}
			}
		}
		for (const auto &p : pushes)
		{
			resetRange();
			bar().pressButton(p.first, p.second);
			frames(1);
			if (const ControlBarButton *b = look())
			{
				return b;
			}
		}
		resetRange();
		return nullptr;
	}
	// a press of the shown button `name` as the player's click does it; false when not shown or the bar refused it
	bool press(const std::string &name)
	{
		const ControlBarButton *b = find(name);
		if (!b)
		{
			return false;
		}
		const bool ok = bar().pressButton(b->slot, b->inPalantir);
		frames(1);
		return ok;
	}
	// a left click on the world point (the target of a GUI command, a placement)
	void clickWorld(const Coord3D &p)
	{
		rig.lookAt(p);
		frames(1);
		const ICoord2D s = rig.screenOf(p);
		hud->mouseMove(s.x, s.y, 0);
		rig.timeMs += 40;
		hud->mouseButton(HudInput::Button::Left, true, s.x, s.y, 0, rig.timeMs);
		rig.timeMs += 40;
		hud->mouseButton(HudInput::Button::Left, false, s.x, s.y, 0, rig.timeMs);
		frames(1);
	}
	// the nearest point to (x, y) at least `clear` from every object and at least 200 inside the map's playable area (the pathfinder's logical extent:
	// a move beyond it is clamped to its edge, so a unit made outside it never reaches its goal)
	Coord3D spot(float x, float y, float clear)
	{
		ICoord2D lo{ 0, 0 }, hi{ 0, 0 };
		if (logic().aiWorld())
		{
			logic().aiWorld()->pathfinder().getLogicalExtent(lo, hi);
		}
		const float x0 = (float)lo.x * 10.0f + 200.0f, y0 = (float)lo.y * 10.0f + 200.0f, x1 = (float)(hi.x + 1) * 10.0f - 200.0f, y1 = (float)(hi.y + 1) * 10.0f - 200.0f;
		for (int ring = 0; ring < 60; ++ring)
		{
			const int per = ring == 0 ? 1 : 8 * ring;
			for (int k = 0; k < per; ++k)
			{
				const float a = 6.2831853f * (float)k / (float)per;
				const float px = x + std::cos(a) * 50.0f * (float)ring, py = y + std::sin(a) * 50.0f * (float)ring;
				if (px < x0 || px > x1 || py < y0 || py > y1)
				{
					continue;
				}
				bool ok = true;
				for (Object *o = logic().getFirstObject(); o && ok; o = o->getNextObject())
				{
					const float dx = o->getPosition()->x - px, dy = o->getPosition()->y - py;
					ok = dx * dx + dy * dy >= clear * clear;
				}
				if (ok)
				{
					return Coord3D{ px, py, 0.0f };
				}
			}
		}
		std::string near;
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			const float dx = o->getPosition()->x - x, dy = o->getPosition()->y - y;
			if (dx * dx + dy * dy < 1500.0f * 1500.0f)
			{
				near += " " + o->getTemplate()->getName() + "#" + std::to_string((unsigned)o->getID());
			}
		}
		FAIL("no free spot inside the map near the point; within 1500:" << near);
		return Coord3D{};
	}
	Object *make(const std::string &name, const Coord3D &at, Player *owner = nullptr) { return rig.make(name, at.x, at.y, owner); }
	Player *enemy() { return rig.game->players().findPlayerWithName("Player_2"); }
	unsigned money() { return rig.local->getMoney()->countMoney(); }
	// the messages the logic saw without executing them (no executor, or malformed), by type
	std::map<int, unsigned long long> unhandled() { return rig.game->dispatch().unhandled(); }
};

// the names of the classes of the object's modules that are not ported (S-140)
inline std::vector<std::string> unportedModules(const Object &o)
{
	std::vector<std::string> out;
	for (const auto &m : o.modules())
	{
		if (m && m->isUnported())
		{
			out.push_back(m->getModuleClassName());
		}
	}
	return out;
}

inline const char *commandName(int c) { return GUICommandName(c); }

// the template's CommandSet field ("" none)
inline std::string templateCommandSet(const ThingTemplate &tt)
{
	if (const FieldValue *v = tt.getFinalOverride()->findField("CommandSet"))
	{
		if (const std::string *s = std::get_if<std::string>(v))
		{
			return *s;
		}
	}
	return std::string();
}

// what a template's modules lead to besides its command set: the command sets a CommandSetUpgrade switches to (a building's levels, a hero's ring), the
// objects an ObjectCreationUpgrade makes (the Crystal Moat, the free wall catapult), the pieces a ReplaceSelfUpgrade puts in a segment's place (gates,
// towers, hubs, emplacements) and the wall pieces a WallHubBehavior lays (the segments, the hub cap, the cliff cap)
struct ModuleLinks
{
	std::vector<std::string> templates;
	std::vector<std::string> sets;
};
inline ModuleLinks linkedByModules(const ThingTemplate &tt)
{
	ModuleLinks out;
	auto add = [](std::vector<std::string> &v, const std::string &n) {
		if (!n.empty() && std::find(v.begin(), v.end(), n) == v.end())
		{
			v.push_back(n);
		}
	};
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		const ModuleData *d = n.data.get();
		if (const auto *c = dynamic_cast<const CommandSetUpgradeModuleData *>(d))
		{
			add(out.sets, c->m_commandSet);
		}
		else if (const auto *o = dynamic_cast<const ObjectCreationUpgradeModuleData *>(d))
		{
			add(out.templates, o->m_thingToSpawn);
		}
		else if (const auto *r = dynamic_cast<const ReplaceSelfUpgradeModuleData *>(d))
		{
			for (const std::string &p : r->m_pieces)
			{
				add(out.templates, p);
			}
		}
		else if (const auto *w = dynamic_cast<const WallHubBehaviorModuleData *>(d))
		{
			for (const std::string &p : w->m_segmentTemplateNames)
			{
				add(out.templates, p);
			}
			for (const std::string *p : { &w->m_hubCapTemplateName, &w->m_defaultSegmentTemplateName, &w->m_cliffCapTemplateName, &w->m_shoreCapTemplateName,
					 &w->m_borderCapTemplateName, &w->m_elevatedSegmentTemplateName })
			{
				add(out.templates, *p);
			}
		}
	}
	return out;
}

// The faction's content as the data offers it: from the local player's start objects and its PlayerTemplate's buildable heroes, every template that a
// UNIT_BUILD / DOZER_CONSTRUCT / FOUNDATION_CONSTRUCT button of a reached template's command set builds, in discovery order
inline std::vector<std::string> reachableTemplates(FactRig &r)
{
	std::deque<std::string> todo;
	std::set<std::string> seen;
	std::vector<std::string> out;
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
	for (const std::string &u : pt->m_startingUnit)
	{
		push(u);
	}
	// what a real skirmish start gives the player: the castle unpacked into its citadel and expansion pads, the builders (FactAStart.cpp)
	for (const std::string &n : factaStartTemplates(r.local()->getPlayerTemplate()->getName()))
	{
		push(n);
	}
	for (const std::string &h : pt->m_buildableHeroesMP) // the heroes the fortress's REVIVE buttons recruit
	{
		push(h);
	}
	for (const std::string &h : pt->m_buildableRingHeroesMP)
	{
		push(h);
	}
	while (!todo.empty())
	{
		const std::string name = todo.front();
		todo.pop_front();
		out.push_back(name);
		const ThingTemplate *tt = r.logic().things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		const ModuleLinks links = linkedByModules(*tt);
		std::vector<std::string> sets{ templateCommandSet(*tt) };
		sets.insert(sets.end(), links.sets.begin(), links.sets.end());
		for (const std::string &setName : sets)
		{
			const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(setName) : nullptr;
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
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
	}
	return out;
}

// the first button of the set named `setName` whose command is `command`, or null
inline const CommandButton *setButton(const std::string &setName, int command)
{
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(setName) : nullptr;
	for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
	{
		const CommandButton *b = set->getCommandButton(slot);
		if (b && b->m_command == command)
		{
			return b;
		}
	}
	return nullptr;
}
} // namespace facta
