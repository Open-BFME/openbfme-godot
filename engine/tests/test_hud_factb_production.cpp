// OpenBFME retail tests (lane FACT-B): every structure, unit and structure upgrade of Isengard and Mordor through the HUD, as a player reaches it.
//   * build: a Porter is clicked, its build button pressed (the button pages opened with their own buttons), the ghost placed with a click on a legal site;
//     the price is BuildCost (ThingTemplate::calcCostToBuild RW 0x73C25F), the building rises in calcTimeToBuild frames (RW 0x73C39E) and completes with
//     its CommandSet;
//   * train: each producing structure is clicked and every unit button pressed: the price, the frames until the unit leaves (calcTimeToBuild plus the
//     exit), where it appears (by the building), the command points it takes (RW 0x6A7FAA), and that it walks when ordered with a right click;
//   * research: every upgrade button of every structure set is pressed: the price (UpgradeTemplate::calcCostToBuild RW 0x66F2C8), the frames
//     (BuildTime * 5), the upgrade lands on the object (OBJECT) or the player (PLAYER), and the structure's own modules triggered by it run.
// Every failure is collected per item and printed as one table; the test then fails once per failing item. They share the HUD tests' retail world and
// SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "FactbInventory.h"
#include "FactbRig.h"
#include "ProductionRateTestUtil.h"

#include "Common/BuildAssistant.h"
#include "GameLogic/BuildPlacement.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

using namespace hudtest;

namespace
{
struct Results
{
	std::map<std::string, std::vector<std::string>> problems; ///< item -> problems
	std::map<std::string, std::string> passed;                ///< item -> what was measured
	void fail(const std::string &item, const std::string &what) { problems[item].push_back(what); }
	void pass(const std::string &item, const std::string &what)
	{
		if (!problems.count(item))
		{
			passed[item] = what;
		}
	}
	void print(const char *title)
	{
		std::printf("factb %s: %zu passed, %zu with problems\n", title, passed.size(), problems.size());
		for (const auto &kv : passed)
		{
			std::printf("  PASS %s: %s\n", kv.first.c_str(), kv.second.c_str());
		}
		for (const auto &kv : problems)
		{
			for (const std::string &p : kv.second)
			{
				std::printf("  FAIL %s: %s\n", kv.first.c_str(), p.c_str());
			}
		}
	}
	// `known`: the items that fail for a registered stop (item -> stop id). They are reported, not failed; a known item that no longer fails is a
	// failure (the stop's pin must move)
	void check(const std::map<std::string, std::string> &known = {})
	{
		for (const auto &kv : problems)
		{
			std::string all;
			for (const std::string &p : kv.second)
			{
				all += p + "; ";
			}
			auto k = known.find(kv.first);
			// lane FACT-C: a pin "S-xxxx|text" holds only failures that say `text` (each of them): another failure of the item is not the pinned gap
			bool held = k != known.end();
			if (held && k->second.find('|') != std::string::npos)
			{
				for (const std::string &p : kv.second)
				{
					held = held && p.find(k->second.substr(k->second.find('|') + 1)) != std::string::npos;
				}
			}
			if (held)
			{
				std::printf("  KNOWN %s (%s)\n", kv.first.c_str(), k->second.c_str());
				continue;
			}
			CHECK_MESSAGE(false, kv.first << ": " << all);
		}
		for (const auto &kv : known)
		{
			CHECK_MESSAGE(problems.count(kv.first) == 1, kv.first << " passes now: move the pin of " << kv.second);
		}
	}
};

using factb::inventory;

std::string str(const char *fmt, ...)
{
	char buf[400];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

// FACTB_TRACE=1: each item's name before it runs (a crash shows where)
void trace(const std::string &item)
{
	static const bool on = std::getenv("FACTB_TRACE") != nullptr;
	if (on)
	{
		std::fprintf(stderr, "factb trace: %s\n", item.c_str());
		std::fflush(stderr);
	}
}

float dist2d(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

int calcFrames(factb::HudGame &g, const ThingTemplate &tt, const Object *producer)
{
	return BuildAssistant::calcTimeToBuild(*tt.getFinalOverride(), g.rig.local, producer, -1, shared().world->productionSettings(), g.logic());
}

// a spot at least `clear` from every object, on rings around the local player's own fortress (away from the enemy's base and towers)
Coord3D siteFor(factb::HudGame &g, int i, float clear)
{
	const Coord3D home = g.home();
	const float a = 0.9f * (float)i, r = 550.0f + 220.0f * (float)(i / 7);
	return g.rig.freeSpot(home.x + r * std::cos(a), home.y + r * std::sin(a), clear);
}

// ---- build: the Porter's build buttons ----
void buildEveryStructure(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(2000000);
	const PlayerTemplate *pt = s.world->playerTemplates().findPlayerTemplate(faction);
	REQUIRE(pt);
	const std::string builderName = pt->m_startingUnit[0];
	int n = 0;
	for (const factb::Entry *e : inv.of(factb::Kind::Structure))
	{
		// the structures a builder's DOZER_CONSTRUCT button places
		bool byBuilder = false;
		for (const std::string &v : e->via)
		{
			for (const std::string &set : inv.setsOfTemplate.at(builderName))
			{
				byBuilder = byBuilder || v.rfind(set + ":", 0) == 0;
			}
		}
		if (!byBuilder)
		{
			continue;
		}
		const std::string item = "build " + e->name;
		trace(item);
		const ThingTemplate *tt = s.world->things().findTemplate(e->name);
		const Coord3D start = siteFor(g, n++, 300.0f);
		Object *porter = g.rig.make(builderName, start.x, start.y);
		g.step(2);
		if (!g.select(porter))
		{
			res.fail(item, "the builder could not be selected with a click (selection " + g.selectionText() + ")");
			g.logic().destroyObject(porter);
			continue;
		}
		ControlBarButton b;
		if (!g.reveal(porter, [&](const CommandButton &c) { return c.m_command == GUI_COMMAND_DOZER_CONSTRUCT && c.getThingTemplate() && c.getThingTemplate()->getName() == e->name; }, b))
		{
			res.fail(item, "no build button on the builder's bar");
			g.logic().destroyObject(porter);
			continue;
		}
		if (b.state != ButtonState::Enabled)
		{
			res.fail(item, str("the build button is not enabled (state %d)", (int)b.state));
			g.logic().destroyObject(porter);
			continue;
		}
		const unsigned spentBefore = g.spent();
		const ObjectID before = g.highestID();
		if (!g.press(b) || !g.hud->input().ui().isPlacing())
		{
			res.fail(item, "the press did not start the placement");
			g.logic().destroyObject(porter);
			continue;
		}
		// a legal site near the builder: the ghost tells
		bool placed = false;
		std::string codes;
		for (int k = 0; k < 12 && !placed; ++k)
		{
			const Coord3D site = g.rig.freeSpot(start.x + 250.0f + 60.0f * (float)k, start.y + 40.0f * (float)(k % 3), 220.0f);
			const ICoord2D px = g.pointAt(site);
			const int code = g.hud->input().ui().placeLegalCode();
			if (code != (int)LBC_OK)
			{
				codes += " " + std::to_string(code);
				continue;
			}
			g.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, g.rig.timeMs += 500);
			g.hud->input().update();
			g.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, g.rig.timeMs += 40);
			g.hud->input().update();
			placed = !g.hud->input().ui().isPlacing();
		}
		if (!placed)
		{
			res.fail(item, "no legal site accepted the ghost (legality codes" + codes + ")");
			g.hud->input().ui().placeBuildAvailable(std::string(), INVALID_ID);
			g.logic().destroyObject(porter);
			continue;
		}
		Object *bld = nullptr;
		for (int f = 0; f < 50 && !bld; ++f)
		{
			g.step(1);
			bld = g.newestOf(e->name, before);
		}
		if (!bld)
		{
			res.fail(item, "the placement made no building (sent: " + g.lastMessages() + ")");
			g.logic().destroyObject(porter);
			continue;
		}
		const int cost = BuildAssistant::calcCostToBuild(*tt->getFinalOverride(), g.rig.local, porter, -1);
		const unsigned paid = g.spent() - spentBefore;
		if ((int)paid != cost || cost != (int)BuildAssistant::buildCost(*tt))
		{
			res.fail(item, str("paid %u, calcCostToBuild %d, BuildCost %u", paid, cost, BuildAssistant::buildCost(*tt)));
		}
		// the construction: from the first progress to the completion (by id: a building or builder that dies leaves the world)
		const int expect = calcFrames(g, *tt, porter);
		const ObjectID bldId = bld->getID(), porterId = porter->getID();
		int guard = expect * 3 + 1500, firstProgress = -1, frames = 0;
		float percent = 0.0f;
		while ((bld = g.logic().findObjectByID(bldId)) != nullptr && !bld->isEffectivelyDead() && bld->isUnderConstruction() && guard-- > 0)
		{
			percent = bld->getConstructionPercent();
			g.step(1);
			++frames;
			if (firstProgress < 0 && percent > 0.0f)
			{
				firstProgress = frames;
			}
		}
		porter = g.logic().findObjectByID(porterId);
		if (!bld || bld->isEffectivelyDead() || bld->isUnderConstruction())
		{
			res.fail(item, str("not complete after %d frames: the building %s, percent %.1f, the builder %s (calcTimeToBuild %d)%s", frames,
								 !bld ? "left the world" : (bld->isEffectivelyDead() ? "died" : "stands"), percent, porter ? "alive" : "gone", expect,
								 bld && porter ? str("; the building at (%.0f, %.0f, %.0f), the builder at (%.0f, %.0f, %.0f) from (%.0f, %.0f); sent: %s", bld->getPosition()->x, bld->getPosition()->y,
													  bld->getPosition()->z, porter->getPosition()->x, porter->getPosition()->y, porter->getPosition()->z, start.x, start.y,
													  g.lastMessages(6).c_str()).c_str() : ""));
			if (porter)
			{
				g.logic().destroyObject(porter);
			}
			continue;
		}
		const int built = frames - (firstProgress < 0 ? frames : firstProgress) + 1;
		if (std::abs(built - expect) > 2)
		{
			res.fail(item, str("built in %d frames, calcTimeToBuild %d", built, expect));
		}
		const std::string set = factb::stringFieldOf(*tt, "CommandSet");
		if (!set.empty() && set != "None" && bld->getCommandSetName() != set)
		{
			res.fail(item, "command set " + bld->getCommandSetName() + ", the template's " + set);
		}
		res.pass(item, str("cost %d, %d frames", cost, built));
		if (porter)
		{
			g.logic().destroyObject(porter);
		}
		g.logic().destroyObject(bld); // no income from it while the next price is measured, room for the next site
		g.step(1);
	}
}

// the structure's production is idle: nothing exits, every door is closed (as test_prod_retail.cpp's puIdle); a press made earlier would queue behind it
void waitProductionIdle(factb::HudGame &g, Object *bld)
{
	const ObjectID id = bld->getID(); // lane FACT-C: by id (a structure may leave the world meanwhile)
	for (int f = 0; f < 200 && (bld = g.logic().findObjectByID(id)) != nullptr; ++f)
	{
		const ProductionUpdate *pu = dynamic_cast<const ProductionUpdate *>(bld->getProductionUpdate());
		bool idle = pu && pu->exitingObjectID() == INVALID_ID && !pu->firstProduction();
		for (int i = 0; idle && i < DOOR_COUNT_MAX; ++i)
		{
			const ProductionUpdate::DoorInfo &d = pu->door(i);
			idle = !(d.openedFrame || d.waitOpenFrame || d.closedFrame);
		}
		if (idle || !pu)
		{
			return;
		}
		g.step(1);
	}
}

enum class Outcome
{
	Done,      ///< pressed and measured (pass or fail recorded)
	NotEnabled ///< the button is not enabled yet (a level of the structure may enable it): tried again after the next upgrade
};

// one UNIT_BUILD button of the selected structure: the price, the frames until the unit comes, where, the command points, a walk on a right click
Outcome trainOne(factb::HudGame &g, Object *bld, const CommandButton *cb, Results &res, std::string &pending)
{
	const ThingTemplate *utt = cb->getThingTemplate()->getFinalOverride();
	const std::string item = "train " + utt->getName() + " at " + bld->getTemplate()->getName();
	trace(item);
	if (!g.select(bld))
	{
		res.fail(item, "the structure could not be selected with a click (selection " + g.selectionText() + ")");
		return Outcome::Done;
	}
	ControlBarButton b;
	if (!g.reveal(bld, [&](const CommandButton &c) { return &c == cb; }, b))
	{
		// neither InPalantir in the first six windows nor Radial (RW 0x92FF5C / 0x92F082): off the bar in retail too (MordorBase's test upgrades)
		int slot = -1;
		const CommandSet *set = shared().world->commands().findCommandSet(bld->getCommandSetName());
		for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
		{
			slot = set->getCommandButton(i) && set->getCommandButton(i)->getFinalOverride() == cb ? i : slot;
		}
		const ControlBar::Placement place = ControlBar::placementOf(*cb, slot < 0 ? 0 : slot, 0);
		if (!place.arc && !place.side)
		{
			res.pass(item, "off the bar in retail too (" + cb->m_name + ": neither InPalantir in windows 0 .. 5 nor Radial)");
			return Outcome::Done;
		}
		res.fail(item, "the button is not on the bar (" + cb->m_name + ")");
		return Outcome::Done;
	}
	if (b.state != ButtonState::Enabled)
	{
		pending = str("%s not enabled (state %d, canMakeUnit %d, command points used %d available %d, the template's %d%s)", cb->m_name.c_str(), (int)b.state,
					  (int)BuildAssistant::canMakeUnit(*bld, utt, -1), g.rig.local->commandPoints().getUsage(), g.rig.local->commandPointsAvailable(),
					  BuildAssistant::commandPoints(*utt), cb->m_neededUpgrade.empty() ? "" : (", needs " + cb->m_neededUpgrade[0]).c_str());
		return Outcome::NotEnabled;
	}
	const ObjectID bldId = bld->getID();
	waitProductionIdle(g, bld);
	if (!(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world before the press");
		return Outcome::Done;
	}
	const unsigned spentBefore = g.spent();
	const int cpBefore = g.rig.local->commandPoints().getUsage();
	const ObjectID before = g.highestID();
	const unsigned pressFrame = g.frame();
	if (!g.press(b))
	{
		res.fail(item, "the press sent nothing");
		return Outcome::Done;
	}
	std::vector<float> rates{ prodrate::rateNow(*bld) }; // lane PRODUCTION1: the producer's PRODUCTION rate of every frame (RW 0x8A1EBC)
	g.step(1);
	if (!(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world after the press");
		return Outcome::Done;
	}
	const int cost = BuildAssistant::calcCostToBuild(*utt, g.rig.local, bld, -1);
	const unsigned paid = g.spent() - spentBefore;
	const int expect = calcFrames(g, *utt, bld);
	Object *unit = nullptr;
	for (int f = 0; f < expect + 200 && !unit; ++f)
	{
		if (Object *p = g.logic().findObjectByID(bldId))
		{
			rates.push_back(prodrate::rateNow(*p));
		}
		g.step(1);
		unit = g.newestOf(utt->getName(), before);
	}
	const int rated = prodrate::expectedFrames(rates, expect); // `expect` at rate 1; a level 2 / 3 building is faster (its BuildSpeedModLvl<n>)
	if (!unit)
	{
		res.fail(item, str("nothing came after %d frames (calcTimeToBuild %d)", expect + 200, expect));
		return Outcome::Done;
	}
	const int took = (int)(g.frame() - pressFrame);
	std::string measured = str("cost %d, %d frames (calc %d)", cost, took, expect);
	if ((int)paid != cost)
	{
		res.fail(item, str("paid %u, calcCostToBuild %d", paid, cost));
	}
	if (took < rated || took > rated + 30)
	{
		res.fail(item, str("came after %d frames, calcTimeToBuild %d (%d at the producer's PRODUCTION rate)", took, expect, rated));
	}
	if (!(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world during the training");
		return Outcome::Done;
	}
	const float d = dist2d(*unit->getPosition(), *bld->getPosition());
	if (d > 600.0f)
	{
		res.fail(item, str("appeared %.0f from the building", d));
	}
	// a horde's members come one per exit delay: let them out, then the command points
	g.step(40);
	int expectCp = 0;
	for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getID() > before && o->getControllingPlayer() == g.rig.local && !o->isEffectivelyDead())
		{
			expectCp += factb::hasKindOf(*o->getTemplate(), "SELECTABLE") && !factb::hasKindOf(*o->getTemplate(), "STRUCTURE") && !factb::hasKindOf(*o->getTemplate(), "HORDE")
							? BuildAssistant::commandPoints(*o->getTemplate())
							: 0;
		}
	}
	const int cpTaken = g.rig.local->commandPoints().getUsage() - cpBefore;
	if (cpTaken != expectCp)
	{
		res.fail(item, str("took %d command points, its objects' CommandPoints sum to %d", cpTaken, expectCp));
	}
	measured += str(", CP %d", cpTaken);
	// it walks where a right click sends it (what cannot move: a mine)
	if (!factb::hasKindOf(*utt, "IMMOBILE") && factb::hasKindOf(*utt, "SELECTABLE"))
	{
		const Coord3D from0 = *unit->getPosition();
		if (g.select(unit, true))
		{
			// the click may pick another object of the same template that came with it (a lumber mill's spawned workers): that one is followed
			if (Object *picked = g.logic().findObjectByID(g.hud->input().ui().selected().front()))
			{
				unit = picked->getContainedBy() ? picked->getContainedBy() : picked;
			}
			const Coord3D from = *unit->getPosition();
			(void)from0;
			// a straight walk (a slow unit's 100 in 120 frames), 200 from the structures: a click beside one picks its model, not the ground
			const Coord3D to = g.rig.freeSpot(from.x + 300.0f, from.y - 200.0f, 200.0f, false, {}, &from);
			g.clickAt(to); // RotWK's default mouse: a left click on the ground orders the selection there
			g.step(120); // a catapult crawls
			const float moved = dist2d(*unit->getPosition(), from);
			if (moved < 100.0f)
			{
				res.fail(item, str("moved only %.0f in 120 frames after a click %.0f away (sent: %s)", moved, dist2d(to, from), g.lastMessages().c_str()));
			}
			measured += str(", walked %.0f", moved);
		}
		else
		{
			res.fail(item, "the new unit could not be selected with a click (selection " + g.selectionText() + ")");
		}
	}
	res.pass(item, measured);
	// out of the way (command points, room at the door)
	for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getID() > before && o->getControllingPlayer() == g.rig.local)
		{
			g.logic().destroyObject(o);
		}
	}
	g.step(2);
	return Outcome::Done;
}

// one OBJECT / PLAYER upgrade button of the selected structure: the price, the frames, the upgrade lands, the structure's own modules triggered by it run
Outcome researchOne(factb::HudGame &g, Object *bld, const CommandButton *cb, Results &res, std::string &pending)
{
	const UpgradeTemplate *u = shared().world->upgrades().findUpgrade(cb->m_upgradeName);
	const std::string item = "research " + cb->m_upgradeName + " at " + bld->getTemplate()->getName();
	trace(item);
	if (!u)
	{
		res.fail(item, "no such upgrade");
		return Outcome::Done;
	}
	const bool isPlayer = u->getUpgradeType() == UPGRADE_TYPE_PLAYER;
	if (isPlayer ? g.rig.local->hasUpgradeComplete(u) : bld->hasUpgrade(u))
	{
		res.pass(item, "already present (an earlier item or an initial upgrade)");
		return Outcome::Done;
	}
	if (!g.select(bld))
	{
		res.fail(item, "the structure could not be selected with a click (selection " + g.selectionText() + ")");
		return Outcome::Done;
	}
	ControlBarButton b;
	if (!g.reveal(bld, [&](const CommandButton &c) { return &c == cb; }, b))
	{
		// neither InPalantir in the first six windows nor Radial (RW 0x92FF5C / 0x92F082): off the bar in retail too (MordorBase's test upgrades)
		int slot = -1;
		const CommandSet *set = shared().world->commands().findCommandSet(bld->getCommandSetName());
		for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
		{
			slot = set->getCommandButton(i) && set->getCommandButton(i)->getFinalOverride() == cb ? i : slot;
		}
		const ControlBar::Placement place = ControlBar::placementOf(*cb, slot < 0 ? 0 : slot, 0);
		if (!place.arc && !place.side)
		{
			res.pass(item, "off the bar in retail too (" + cb->m_name + ": neither InPalantir in windows 0 .. 5 nor Radial)");
			return Outcome::Done;
		}
		res.fail(item, "the button is not on the bar (" + cb->m_name + ")");
		return Outcome::Done;
	}
	if (b.state != ButtonState::Enabled)
	{
		std::string need;
		for (const std::string &nu : cb->m_neededUpgrade)
		{
			need += " " + nu;
		}
		pending = str("%s not enabled (state %d)%s%s", cb->m_name.c_str(), (int)b.state, need.empty() ? "" : ", needs", need.c_str());
		return Outcome::NotEnabled;
	}
	if (cb->hasOption(COMMAND_OPTION_NONPRESSABLE))
	{
		// the Hall of Twilight's technologies (_patch201 commandbutton.ini Options = NONPRESSABLE): retail draws them _static (RW 0x9D2BFA) and buys them
		// itself through its DoCommandUpgrade modules; the bar refuses their press (ControlBar::pressButton, INFERENCE)
		if (g.press(b))
		{
			res.fail(item, "a NONPRESSABLE button's press sent a message (" + cb->m_name + ")");
		}
		else
		{
			res.pass(item, "NONPRESSABLE: the press is refused (" + cb->m_name + ")");
		}
		return Outcome::Done;
	}
	const ObjectID bldId = bld->getID();
	waitProductionIdle(g, bld);
	if (!(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world before the press");
		return Outcome::Done;
	}
	const unsigned spentBefore = g.spent();
	const int cost = u->calcCostToBuild(g.rig.local, bld);
	const unsigned pressFrame = g.frame();
	if (!g.press(b))
	{
		res.fail(item, "the press sent nothing");
		return Outcome::Done;
	}
	std::vector<float> rates{ prodrate::rateNow(*bld) }; // lane PRODUCTION1: the producer's PRODUCTION rate of every frame (RW 0x8A1EBC)
	g.step(1);
	const unsigned paid = g.spent() - spentBefore;
	const int expect = (int)std::ceil(u->getBuildTimeSeconds() * 5.0f);
	bool got = false;
	for (int f = 0; f < expect + 100 && !got && bld; ++f)
	{
		if (Object *p = g.logic().findObjectByID(bldId))
		{
			rates.push_back(prodrate::rateNow(*p));
		}
		g.step(1);
		bld = g.logic().findObjectByID(bldId);
		got = isPlayer ? g.rig.local->hasUpgradeComplete(u) : bld && bld->hasUpgrade(u);
	}
	if (!bld || !(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world during the research");
		return Outcome::Done;
	}
	if (!got)
	{
		const ProductionUpdateInterface *q = bld->getProductionUpdate();
		const ProductionEntry *first = q ? q->firstProduction() : nullptr;
		res.fail(item, str("not complete after %d frames (BuildTime %gs); the queue: %d entries, the first %s at %.1f%% (+%.3f a frame)", expect + 100,
						   (double)u->getBuildTimeSeconds(), q ? q->getProductionCount() : -1,
						   !first ? "none" : first->upgradeToResearch ? first->upgradeToResearch->getUpgradeName().c_str() : first->objectToProduce ? first->objectToProduce->getName().c_str() : "?",
						   first ? first->percentComplete : -1.0f, first ? first->percentPerFrame : -1.0f));
		return Outcome::Done;
	}
	const int took = (int)(g.frame() - pressFrame);
	if ((int)paid != cost)
	{
		res.fail(item, str("paid %u, calcCostToBuild %d", paid, cost));
	}
	const int rated = prodrate::expectedFrames(rates, expect); // a level 2 building researching its level 3 runs at its BuildSpeedModLvl2
	if (took < rated || took > rated + 5)
	{
		res.fail(item, str("took %d frames, BuildTime * 5 = %d (%d at the producer's PRODUCTION rate)", took, expect, rated));
	}
	// the structure's own modules triggered by it alone ran (a level shows its model and command set, a fortress improvement its weapon / power)
	g.step(3);
	if (!(bld = g.logic().findObjectByID(bldId)))
	{
		res.fail(item, "the structure left the world after the research");
		return Outcome::Done;
	}
	int triggered = 0, ran = 0;
	for (const std::unique_ptr<BehaviorModule> &m : bld->modules())
	{
		UpgradeMux *mux = m->getUpgrade();
		if (!mux)
		{
			continue;
		}
		const std::vector<std::string> &t = mux->muxData()->m_triggeredBy;
		if (std::find(t.begin(), t.end(), cb->m_upgradeName) != t.end() && !mux->muxData()->m_requiresAllTriggers && mux->muxData()->m_conflictsWith.empty())
		{
			++triggered;
			ran += mux->isAlreadyUpgraded() ? 1 : 0;
		}
	}
	if (ran != triggered)
	{
		res.fail(item, str("%d of the %d modules it triggers on the structure ran", ran, triggered));
	}
	res.pass(item, str("cost %d, %d frames, %d modules", cost, took, triggered));
	return Outcome::Done;
}

// every structure of the inventory with a command set: its UNIT_BUILD and upgrade buttons, again after each upgrade (a level switches the set and
// enables buttons), until nothing new is pressed
void operateEveryStructure(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(5000000);
	// the command points of a grown base: the faction's structures with a CommandPointBonus (its castle keep, RW 0x6A7C01), two of each, by the
	// base (a Black Rider horde's 150 points exceed the starting limit: canMakeUnit answers 7, RW 0x6A7F79)
	{
		const std::string &side = g.rig.local->getPlayerTemplate()->m_side;
		const Coord3D home = g.home();
		int k = 0;
		for (const ThingTemplate *t : s.world->things().templates())
		{
			const FieldValue *bonus = t->getFinalOverride()->findField("CommandPointBonus");
			const long long *v = bonus ? std::get_if<long long>(bonus) : nullptr;
			if (v && *v > 0 && factb::stringFieldOf(*t, "Side") == side && factb::hasKindOf(*t, "STRUCTURE"))
			{
				for (int twice = 0; twice < 2; ++twice, ++k)
				{
					const Coord3D at = g.rig.freeSpot(home.x - 700.0f + 500.0f * (float)k, home.y - 900.0f, 400.0f);
					g.rig.make(t->getName(), at.x, at.y);
				}
			}
		}
		g.step(3);
	}
	int n = 0;
	for (const factb::Entry *e : inv.of(factb::Kind::Structure))
	{
		const ThingTemplate *btt = s.world->things().findTemplate(e->name);
		const std::string set = factb::stringFieldOf(*btt, "CommandSet");
		if (set.empty() || set == "None" || !s.world->commands().findCommandSet(set))
		{
			continue;
		}
		const Coord3D site = siteFor(g, n++, 400.0f);
		Object *bld = g.rig.make(e->name, site.x, site.y);
		const ObjectID bldId = bld->getID(); // lane FACT-C: by id (a structure that leaves the world must not be read again)
		g.step(5);
		std::set<const CommandButton *> done;
		std::map<const CommandButton *, std::string> pending;
		for (int round = 0; round < 8 && bld; ++round)
		{
			bool progress = false;
			const CommandSet *cs = s.world->commands().findCommandSet(bld->getCommandSetName());
			for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
			{
				const CommandButton *b0 = cs->getCommandButton(i);
				const CommandButton *cb = b0 ? b0->getFinalOverride() : nullptr;
				if (!(bld = g.logic().findObjectByID(bldId)))
				{
					res.fail(e->name, "the structure left the world during its buttons");
					break;
				}
				if (!cb || done.count(cb))
				{
					continue;
				}
				std::string why;
				Outcome o = Outcome::Done;
				if (cb->m_command == GUI_COMMAND_UNIT_BUILD && cb->getThingTemplate())
				{
					o = trainOne(g, bld, cb, res, why);
				}
				else if (cb->m_command == GUI_COMMAND_OBJECT_UPGRADE || cb->m_command == GUI_COMMAND_PLAYER_UPGRADE)
				{
					o = researchOne(g, bld, cb, res, why);
				}
				else
				{
					continue;
				}
				if (o == Outcome::Done)
				{
					done.insert(cb);
					pending.erase(cb);
					progress = true;
				}
				else
				{
					pending[cb] = why;
				}
				bld = g.logic().findObjectByID(bldId);
				if (!bld || bld->isEffectivelyDead())
				{
					break;
				}
			}
			if (!progress || !bld || bld->isEffectivelyDead())
			{
				break;
			}
		}
		for (const auto &kv : pending)
		{
			const std::string what = kv.first->m_command == GUI_COMMAND_UNIT_BUILD ? "train " + kv.first->getThingTemplate()->getName() : "research " + kv.first->m_upgradeName;
			res.fail(what + " at " + e->name, kv.second + " after every other button of the structure");
		}
		if ((bld = g.logic().findObjectByID(bldId)) != nullptr)
		{
			g.logic().destroyObject(bld);
		}
		g.step(2);
	}
}

// ---- foundations: every FOUNDATION_CONSTRUCT button (a castle's expansion pads: towers, siege expansions, the wall hub expansion), each on a fresh pad ----
void foundEveryExpansion(const std::string &faction, Results &res)
{
	const factb::Inventory &inv = inventory(faction);
	SharedWorld &s = shared();
	factb::HudGame g(s, faction.c_str());
	g.rig.local->depositMoney(5000000);
	int n = 0;
	for (const factb::Entry *e : inv.of(factb::Kind::Structure))
	{
		const ThingTemplate *ptt = s.world->things().findTemplate(e->name);
		const CommandSet *cs = s.world->commands().findCommandSet(factb::stringFieldOf(*ptt, "CommandSet"));
		for (int i = 0; cs && i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b0 = cs->getCommandButton(i);
			const CommandButton *cb = b0 ? b0->getFinalOverride() : nullptr;
			if (!cb || cb->m_command != GUI_COMMAND_FOUNDATION_CONSTRUCT || !cb->getThingTemplate())
			{
				continue;
			}
			const ThingTemplate *tt = cb->getThingTemplate()->getFinalOverride();
			const std::string item = "found " + tt->getName() + " on " + e->name;
			trace(item);
			const Coord3D site = siteFor(g, n++, 300.0f);
			Object *pad = g.rig.make(e->name, site.x, site.y);
			g.step(5);
			if (!g.select(pad))
			{
				res.fail(item, "the pad could not be selected with a click (selection " + g.selectionText() + ")");
				g.logic().destroyObject(pad);
				continue;
			}
			ControlBarButton b;
			if (!g.reveal(pad, [&](const CommandButton &c) { return &c == cb; }, b) || b.state != ButtonState::Enabled)
			{
				res.fail(item, str("%s is not an enabled button of the pad", cb->m_name.c_str()));
				g.logic().destroyObject(pad);
				continue;
			}
			const unsigned spentBefore = g.spent();
			const ObjectID before = g.highestID();
			if (!g.press(b))
			{
				res.fail(item, "the press sent nothing");
				continue;
			}
			Object *built = nullptr;
			for (int f = 0; f < 30 && !built; ++f)
			{
				g.step(1);
				built = g.newestOf(tt->getName(), before);
			}
			if (!built)
			{
				res.fail(item, "the press made no " + tt->getName() + " (sent: " + g.lastMessages() + ")");
				continue;
			}
			const ObjectID id = built->getID();
			const int cost = BuildAssistant::calcCostToBuild(*tt, g.rig.local, nullptr, -1);
			const int expect = calcFrames(g, *tt, nullptr);
			int frames = 0, firstProgress = -1;
			while ((built = g.logic().findObjectByID(id)) != nullptr && built->isUnderConstruction() && frames < expect * 3 + 300)
			{
				const float percent = built->getConstructionPercent();
				g.step(1);
				++frames;
				if (firstProgress < 0 && percent > 0.0f)
				{
					firstProgress = frames;
				}
			}
			if (!built || built->isUnderConstruction())
			{
				res.fail(item, str("not complete after %d frames (calcTimeToBuild %d)", frames, expect));
				continue;
			}
			if ((int)(g.spent() - spentBefore) != cost)
			{
				res.fail(item, str("paid %u, calcCostToBuild %d", g.spent() - spentBefore, cost));
			}
			// the construction proper, from its first progress (the builders walk to the pad first)
			const int rising = frames - (firstProgress < 0 ? frames : firstProgress) + 1;
			if (std::abs(rising - expect) > 2)
			{
				res.fail(item, str("built in %d frames from its first progress, calcTimeToBuild %d", rising, expect));
			}
			res.pass(item, str("cost %d, built in %d frames (calcTimeToBuild %d)", cost, rising, expect));
			for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getID() > before - 1 && o->getControllingPlayer() == g.rig.local)
				{
					g.logic().destroyObject(o);
				}
			}
			if (Object *p = g.logic().findObjectByID(pad->getID()))
			{
				g.logic().destroyObject(p);
			}
			g.step(2);
		}
	}
}
} // namespace

TEST_CASE("factb build: every Isengard structure from the Porter's build buttons")
{
	if (!haveWorld("factb build"))
	{
		return;
	}
	Results r;
	buildEveryStructure("FactionIsengard", r);
	r.print("build FactionIsengard");
	r.check();
}

TEST_CASE("factb build: every Mordor structure from the builder's build buttons")
{
	if (!haveWorld("factb build"))
	{
		return;
	}
	Results r;
	buildEveryStructure("FactionMordor", r);
	r.print("build FactionMordor");
	r.check();
}

TEST_CASE("factb structures: every Isengard unit and structure upgrade from the structures' buttons")
{
	if (!haveWorld("factb structures"))
	{
		return;
	}
	Results r;
	operateEveryStructure("FactionIsengard", r);
	r.print("structures FactionIsengard");
	// the Wizard's Tower needs the Iron Plating the castle hands to its keep (CastleUpgrade through the castle): this sweep's citadel stands alone
	r.check({ { "research Upgrade_IsengardFortressWizardsTower at IsengardFortressCitadel", "the castle path: factb fortress (a citadel made outside a castle has no castle to hand Iron Plating / Morgul Sorcery over)" } });
}

TEST_CASE("factb structures: every Mordor unit and structure upgrade from the structures' buttons")
{
	if (!haveWorld("factb structures"))
	{
		return;
	}
	Results r;
	operateEveryStructure("FactionMordor", r);
	r.print("structures FactionMordor");
	// the catapult leaves the siege works and walks away when ordered: it passes since STANCE-1's follow-path exit clears IS_LEAVING_FACTORY (RW 0x74B77D),
	// which before stayed set (it delays the Battle stance and refuses requests to move aside); before, it barely moved, 19 of 300 in 120 frames
	r.check({ { "research Upgrade_MordorFortressGorgorothSpire at MordorFortressCitadel", "the castle path: factb fortress (a citadel made outside a castle has no castle to hand Iron Plating / Morgul Sorcery over)" } });
}

TEST_CASE("factb found: every Isengard castle expansion from its pad's buttons")
{
	if (!haveWorld("factb found"))
	{
		return;
	}
	Results r;
	foundEveryExpansion("FactionIsengard", r);
	r.print("found FactionIsengard");
	r.check();
}

TEST_CASE("factb found: every Mordor castle expansion from its pad's buttons")
{
	if (!haveWorld("factb found"))
	{
		return;
	}
	Results r;
	foundEveryExpansion("FactionMordor", r);
	r.print("found FactionMordor");
	r.check();
}

// A building that changes owner takes the new owner's faction command set: Object::setTeam (RW 0x69954A) ends with updateUpgradeModules
// (RW 0x699658 -> RW 0x6936FE), so AmonSulKeep's CommandSetUpgrade (TriggeredBy Upgrade_IsengardFaction / Upgrade_MordorFaction) runs against the new
// owner's InitialUpgrades. And those InitialUpgrades are a Player::init grant (RW 0x6AFC92), not a science one: a science reset (initScience) leaves
// a removed one removed
TEST_CASE("factb owner: a captured building takes its new owner's faction command set, a science reset does not regrant the InitialUpgrades")
{
	if (!haveWorld("factb owner"))
	{
		return;
	}
	for (const char *faction : { "FactionIsengard", "FactionMordor" })
	{
		CAPTURE(faction);
		factb::HudGame g(shared(), faction);
		Player *me = g.rig.local;
		const std::string side = std::string(faction) == "FactionIsengard" ? "Isengard" : "Mordor";
		const std::string factionUpgrade = "Upgrade_" + side + "Faction";
		REQUIRE(me->hasUpgradeComplete(factionUpgrade));
		// the first owner: a player of another faction (Player_2 is Mordor on the bare map, the map's neutral player has no faction upgrade)
		Player *enemy = nullptr;
		for (int i = 0; i < g.rig.game->players().getPlayerCount() && !enemy; ++i)
		{
			Player *p = g.rig.game->players().getNthPlayer(i);
			enemy = p && p != me && !p->hasUpgradeComplete(factionUpgrade) ? p : nullptr;
		}
		REQUIRE(enemy);
		const Coord3D at = g.rig.freeSpot(g.home().x + 500.0f, g.home().y, 300.0f);
		Object *keep = g.rig.make("AmonSulKeep", at.x, at.y, enemy);
		const ObjectID id = keep->getID();
		g.step(2);
		keep = g.logic().findObjectByID(id);
		REQUIRE(keep);
		const std::string mine = side + "MonumentFortressRebuildCommandSet";
		const std::string before = keep->getCommandSetName();
		CHECK(before != mine);
		keep->setTeam(me->getDefaultTeam());
		g.step(1);
		CHECK(keep->getControllingPlayer() == me);
		CHECK(keep->getCommandSetName() == mine);

		// the science reset: the faction upgrade removed, the sciences reset as the PlayerList does it: the upgrade stays removed
		me->removeCompletedUpgrade(factionUpgrade);
		REQUIRE(!me->hasUpgradeComplete(factionUpgrade));
		me->initScience(SpellGameMode{ true, false });
		CHECK(!me->hasUpgradeComplete(factionUpgrade));
	}
}

// ---- lane FACT-C: the same sweeps for the Goblins (FactionWild) and Angmar ----------------------------------------------------------------------

TEST_CASE("factc build: every Goblin structure from the builder's build buttons")
{
	if (!haveWorld("factc build"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	buildEveryStructure("FactionWild", r);
	r.print("build FactionWild");
	r.check();
}

TEST_CASE("factc structures: every Goblin unit and structure upgrade from the structures' buttons")
{
	if (!haveWorld("factc structures"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	operateEveryStructure("FactionWild", r);
	r.print("structures FactionWild");
	r.check({ { "research Upgrade_WildFortressDragonNest at WildFortressCitadel", "the castle path: a citadel made outside a castle gets no CastleUpgrade (RW 0x8BAB29); factc castle buttons buys it at the started keep|not enabled (state 2), needs Upgrade_" },
		{ "research Upgrade_HasDragonNestFireDrake at WildFortressCitadel", "the castle path: a citadel made outside a castle gets no CastleUpgrade (RW 0x8BAB29); factc castle buttons buys it at the started keep|not enabled (state 2), needs Upgrade_" } });
}

TEST_CASE("factc found: every Goblin castle expansion from its pad's buttons")
{
	if (!haveWorld("factc found"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	foundEveryExpansion("FactionWild", r);
	r.print("found FactionWild");
	r.check();
}

TEST_CASE("factc build: every Angmar structure from the builder's build buttons")
{
	if (!haveWorld("factc build"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	buildEveryStructure("FactionAngmar", r);
	r.print("build FactionAngmar");
	r.check();
}

TEST_CASE("factc structures: every Angmar unit and structure upgrade from the structures' buttons")
{
	if (!haveWorld("factc structures"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	operateEveryStructure("FactionAngmar", r);
	r.print("structures FactionAngmar");
	r.check({ { "research Upgrade_AngmarFortressSanctum at AngmarFortressCitadel", "the castle path: a citadel made outside a castle gets no CastleUpgrade (RW 0x8BAB29); factc castle buttons buys it at the started keep|not enabled (state 2), needs Upgrade_" },
		{ "train AngmarTrollSling at AngmarForgeWorks", "S-2003|came after" } });
}

TEST_CASE("factc found: every Angmar castle expansion from its pad's buttons")
{
	if (!haveWorld("factc found"))
	{
		return;
	}
	factb::StartedScope started; // the lobby's start: the starting bases and castles (an idle opponent)
	Results r;
	foundEveryExpansion("FactionAngmar", r);
	r.print("found FactionAngmar");
	r.check();
}
