// OpenBFME retail tests, lane HUD-5 (the owner's report: clicking a gate on Helm's Deep does not open it): the gate's Toggle button. GPL-3.0.
//
// The player at Helm's Deep's start 1 owns the main gate (RW 0x62AD24); selecting it shows its command set's TOGGLE_GATE button, active (RW 0x9436E9); a press
// sends MSG_CLOSE_GATE for the open gate, then (settled again) MSG_OPEN_GATE (processCommandUI RW 0x9410D7), and the logic moves the gate. The tests SKIP loudly
// without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct GateRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit GateRig(SharedWorld &s, const char *map = "map wor helms deep") : rig(s, map, "FactionMen")
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~GateRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	Object *gate()
	{
		for (Object *o = rig.game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "RBHelmsDeepGateDoorBig" || o->getTemplate()->getName() == "MinisGateDoor")
			{
				return o;
			}
		}
		return nullptr;
	}
	const ControlBarButton *toggle()
	{
		for (const std::vector<ControlBarButton> *list : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button && b.button->m_command == GUI_COMMAND_TOGGLE_GATE)
				{
					return &b;
				}
			}
		}
		return nullptr;
	}
	// the movie's own press of the button's arc position (the callback its clip calls on a click; test_build_hud does the same)
	bool pressInMovie(const ControlBarButton &b)
	{
		const std::string name = "_level" + std::to_string(hud->palantir()->level()) + ".CommandButtons." + std::to_string(b.position) + ".content_OnPress";
		return hud->windows().hasCommand(name) && hud->windows().invokeCallback(name, "");
	}
	// frames until the gate is settled (at most `limit`); the frames it took
	int settle(GateOpenAndCloseBehavior &b, int limit)
	{
		frames(2);
		int n = 2;
		while (!b.isSettled() && n < limit)
		{
			frames(1);
			++n;
		}
		return n;
	}
	int count(const std::string &prefix)
	{
		int n = 0;
		for (const std::string &l : hud->input().messageLog())
		{
			n += l.rfind(prefix, 0) == 0 ? 1 : 0;
		}
		return n;
	}
};
} // namespace

TEST_CASE("hud5 gate button retail: the selected main gate's TOGGLE_GATE closes it (MSG_CLOSE_GATE), pressed again opens it (MSG_OPEN_GATE)")
{
	if (!haveWorld("hud5 gate button"))
	{
		return;
	}
	GateRig g(shared());
	g.frames(10);
	Object *gate = g.gate();
	REQUIRE(gate);
	REQUIRE(gate->getControllingPlayer() == g.rig.local);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	CHECK(b->isOpen());
	CHECK(b->isSettled());
	// the selection a click on the door makes (InGameHudNode::select_object does the same)
	g.hud->input().ui().deselectAll(true);
	g.hud->input().ui().selectObject(gate->getID());
	g.frames(5);
	const ControlBarButton *t = g.toggle();
	REQUIRE(t);
	CHECK((int)t->state == (int)ButtonState::Active);
	REQUIRE(t->inPalantir);
	// the clip shows the enabled state (RW 0x943C7C / 0x9D2BFA), not _visuallyEnabled, whose clip takes no click
	CHECK(std::string(AptPalantir::commandStateName(t->state, t->button->m_options)) == "_up");
	REQUIRE(g.pressInMovie(*t));
	g.frames(5);
	CHECK(g.count("MSG_CLOSE_GATE") == 1);
	CHECK(g.settle(*b, 2000) < 2000);
	CHECK(b->state() == GateOpenAndCloseBehavior::CLOSED);
	g.frames(3);
	t = g.toggle();
	REQUIRE(t);
	CHECK((int)t->state == (int)ButtonState::Active);
	REQUIRE(g.hud->controlBar().pressButton(t->slot, t->inPalantir));
	g.frames(5);
	CHECK(g.count("MSG_OPEN_GATE") == 1);
	CHECK(g.settle(*b, 2000) < 2000);
	CHECK(b->isOpen());
}


// RW 0x9D2BEE / the populate RW 0x943C04: every command button state's clip state; an enabled or active NONPRESSABLE button is _static
TEST_CASE("hud5 command button states: the clip state of each button state, and NONPRESSABLE's _static (RW 0x9D2BEE, RW 0x943C04)")
{
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Enabled, 0)) == "_up");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Active, 0)) == "_up");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Enabled, COMMAND_OPTION_NONPRESSABLE)) == "_static");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Active, COMMAND_OPTION_NONPRESSABLE)) == "_static");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Restricted, 0)) == "_disabled");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Restricted, COMMAND_OPTION_NONPRESSABLE)) == "_disabled");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::CantAfford, 0)) == "_cantAfford");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::NotReady, 0)) == "_notReady");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Hidden, 0)) == "_unused");
}

TEST_CASE("hud5 command button states retail: the retail buttons with NONPRESSABLE parse it as option index 28")
{
	if (!haveWorld("hud5 nonpressable"))
	{
		return;
	}
	const CommandStore &store = shared().world->commands();
	int nonpressable = 0;
	for (const std::string &name : store.setNames())
	{
		const CommandSet *set = store.findCommandSet(name);
		for (int i = 0; set && i < (int)CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b = set->getCommandButton(i);
			nonpressable += b && b->hasOption(COMMAND_OPTION_NONPRESSABLE) ? 1 : 0;
		}
	}
	MESSAGE("command set slots holding a NONPRESSABLE button: " << nonpressable);
	CHECK(nonpressable > 0);
}


// lane HUD-5 (Sol's review): the owner's click on the door. HelmsDeepGatehouseCenter (STRUCTURE, WALK_ON_TOP_OF_WALL, not SELECTABLE) stands around the door at
// its position; the pick (RW 0x48AB28) takes the nearest hit, so where the door's face is in front the click selects the gate
TEST_CASE("hud5 gate pick retail: a click on Helm's Deep's main gate selects the gate, not the gatehouse around it (RW 0x4B583C, RW 0x48AB28)")
{
	if (!haveWorld("hud5 gate pick"))
	{
		return;
	}
	GateRig g(shared());
	g.frames(10);
	Object *gate = g.gate();
	REQUIRE(gate);
	Object *house = nullptr;
	for (Object *o = g.rig.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		house = o->getTemplate()->getName() == "HelmsDeepGatehouseCenter" ? o : house;
	}
	REQUIRE(house);
	CHECK_FALSE(house->isKindOfName("SELECTABLE"));
	CHECK(house->isKindOfName("WALK_ON_TOP_OF_WALL")); // RW 0x4B583C gives it the selectable type all the same: the pick can meet it, the selection refuses it
	CHECK(HudObjects::collisionTypeOf(g.hud->input().context(), *house) == (HudObjects::PICK_TYPE_SELECTABLE | HudObjects::PICK_TYPE_OWN));
	CHECK(HudObjects::collisionTypeOf(g.hud->input().context(), *gate) == (HudObjects::PICK_TYPE_SELECTABLE | HudObjects::PICK_TYPE_OWN));
	const Coord3D p = *gate->getPosition();
	g.rig.lookAt(p, 420.0f, 520.0f);
	g.frames(3);
	const ICoord2D px = g.rig.screenOf(p);
	CHECK(HudObjects::pickForSelection(g.hud->input().context(), px) == gate);
	// the input path: a left click there selects the gate and shows its Toggle button
	g.hud->input().mouseMove(px.x, px.y, 0);
	g.hud->input().update();
	g.hud->input().mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 5000);
	g.hud->input().update();
	g.hud->input().mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 5020);
	g.hud->input().update();
	g.frames(5);
	CHECK(g.hud->input().ui().isSelected(gate->getID()));
	CHECK(g.toggle() != nullptr);
}

// RW 0x48AB28's order among hits with each caller's pick types (Sol's r2 review): the point selection adds OWN 0x100 (RW 0x485CB8: an own hero, then an own object,
// then a hero, then an enemy), the order click keeps RW 0x71083F's types (a hero, then an enemy before a nearer own unit), the double click picks with SELECTABLE
// only (RW 0x81F7C5), the hover always with FORCEATTACKABLE (RW 0x83CC13), and a drag box ranks nothing (RW 0x485B79's region path)
TEST_CASE("hud5 pick retail: each caller's pick types: selection prefers own objects, the order click and the double click an enemy behind, a drag ranks nothing")
{
	if (!haveWorld("hud5 pick callers"))
	{
		return;
	}
	SharedWorld &sh = shared();
	Rig rig(sh);
	const Coord3D c = rig.freeSpot(2400, 1800, 300.0f);
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	auto make = [&](const char *t, Player *p, float x, float y) {
		std::string error;
		Object *o = rig.game->createObject(t, p->getPlayerIndex(), Coord3D{ x, y, rig.logic().getGroundHeight(x, y) }, 0.0f, &error);
		REQUIRE_MESSAGE(o, error);
		return o;
	};
	const HudContext &ctx = rig.input->context();
	// the masks
	CHECK(HudObjects::pickTypesForContext(ctx, false) == HudObjects::PICK_TYPE_SELECTABLE);
	CHECK(HudObjects::pickTypesForContext(ctx, true) == (HudObjects::PICK_TYPE_SELECTABLE | HudObjects::PICK_TYPE_FORCEATTACKABLE));
	// the camera looks north: the nearer object stands south of the farther one on the same ray
	Object *own = make("MenPorter", rig.local, c.x, c.y);
	Object *foe = make("MenPorter", enemy, c.x, c.y + 60.0f);
	rig.lookAt({ c.x, c.y + 30.0f, rig.logic().getGroundHeight(c.x, c.y) }, 300.0f, 40.0f);
	rig.frame(2);
	const ICoord2D px = rig.screenOf(Coord3D{ c.x, c.y, rig.logic().getGroundHeight(c.x, c.y) + 8.0f });
	REQUIRE(HudObjects::pickHits(ctx, px, HudObjects::PICK_TYPE_SELECTABLE).size() >= 2);
	// an enemy behind an own unit: the selection takes the own unit (0x104), the order click and the double click the enemy (0x14)
	CHECK(HudObjects::pickForSelection(ctx, px) == own);
	CHECK(HudObjects::pickObject(ctx, px) == foe);
	CHECK(HudObjects::pickForDoubleClick(ctx, px) == foe);
	CHECK(HudObjects::pickForHover(ctx, px) == foe);
	// the input path: a left click selects the own unit
	rig.input->ui().deselectAll(true);
	rig.leftClick(px.x, px.y);
	rig.frame(2);
	CHECK(rig.input->ui().isSelected(own->getID()));
	CHECK_FALSE(rig.input->ui().isSelected(foe->getID()));
	// a drag box over both returns both (no ranking); the drag selection keeps the own one only
	const ICoord2D pf = rig.screenOf(*foe->getPosition());
	const ICoord2D po = rig.screenOf(*own->getPosition());
	IRegion2D box;
	box.lo = { std::min(pf.x, po.x) - 30, std::min(pf.y, po.y) - 30 };
	box.hi = { std::max(pf.x, po.x) + 30, std::max(pf.y, po.y) + 30 };
	const std::vector<Object *> inBox = HudObjects::objectsInRegion(ctx, box);
	CHECK(std::find(inBox.begin(), inBox.end(), own) != inBox.end());
	CHECK(std::find(inBox.begin(), inBox.end(), foe) != inBox.end());
	// an own hero behind the own unit: the selection and the order click take the hero (0x184 / 0x84)
	rig.logic().destroyObject(foe);
	rig.frame(2);
	Object *hero = make("GondorBoromir", rig.local, c.x, c.y + 60.0f);
	rig.frame(2);
	CHECK(HudObjects::pickForSelection(ctx, px) == hero);
	CHECK(HudObjects::pickObject(ctx, px) == hero);
	// an enemy hero behind the own unit: the selection still takes the own unit (0x104 before 0x84), the order click the hero
	rig.logic().destroyObject(hero);
	rig.frame(2);
	Object *foeHero = make("MordorGothmog", enemy, c.x, c.y + 60.0f);
	rig.frame(2);
	CHECK(HudObjects::pickForSelection(ctx, px) == own);
	CHECK(HudObjects::pickObject(ctx, px) == foeHero);
	rig.logic().destroyObject(foeHero);
	rig.frame(2);
	CHECK(HudObjects::pickForSelection(ctx, px) == own);
	CHECK(HudObjects::pickObject(ctx, px) == own);
}

// Sol's review: Minas Tirith's gate, friend and enemy. Its owner (the player at start 1) selects it: TOGGLE_GATE is active and closes it through the message path;
// seen by the player at start 2 (an enemy), the selected gate offers no command (the control bar shows only the local player's own objects' command sets)
TEST_CASE("hud5 gate button retail: Minas Tirith's gate toggles for its owner; an enemy's selection of it offers no Toggle")
{
	if (!haveWorld("hud5 minas tirith gate button"))
	{
		return;
	}
	GateRig g(shared(), "map wor minas tirith");
	g.frames(10);
	Object *gate = g.gate();
	REQUIRE(gate);
	REQUIRE(gate->getTemplate()->getName() == "MinisGateDoor");
	REQUIRE(gate->getControllingPlayer() == g.rig.local);
	GateOpenAndCloseBehavior *b = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(b);
	g.hud->input().ui().deselectAll(true);
	g.hud->input().ui().selectObject(gate->getID());
	g.frames(5);
	const ControlBarButton *t = g.toggle();
	REQUIRE(t);
	CHECK((int)t->state == (int)ButtonState::Active);
	const bool wasOpen = b->isOpen();
	REQUIRE(g.hud->controlBar().pressButton(t->slot, t->inPalantir));
	g.frames(5);
	CHECK(g.count(wasOpen ? "MSG_CLOSE_GATE" : "MSG_OPEN_GATE") == 1);
	CHECK(g.settle(*b, 2000) < 2000);
	CHECK(b->isOpen() != wasOpen);
	// the enemy's view
	Player *enemy = g.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	g.rig.game->players().setLocalPlayer(enemy);
	g.hud->input().ui().deselectAll(true);
	g.hud->input().ui().selectObject(gate->getID());
	g.frames(5);
	CHECK(g.toggle() == nullptr);
	g.rig.game->players().setLocalPlayer(g.rig.local);
}
