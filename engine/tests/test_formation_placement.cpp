// OpenBFME. GPL-3.0. BFME2 FormationTranslator gesture facts, independent of retail data.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "MoveTestUtil.h"
#include "GameLogic/SimMath.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/FormationAssistant.h"
#include "GameClient/FormationPlacement.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameClient/HudInput.h"
#include <cmath>
#include <limits>

namespace
{
ClientMessage button(int type, int x, int y, int time)
{
	ClientMessage m(type);
	m.appendPixel({x, y});
	m.appendInteger(123);
	m.appendInteger(time);
	return m;
}
struct FlatView : TacticalView
{
	ICoord2D size() const override
	{
		return {1280, 720};
	}
	bool worldToScreen(const Coord3D &p, ICoord2D &out) const override
	{
		out = {(int)p.x, (int)p.y};
		return true;
	}
	bool screenToRay(const ICoord2D &p, Coord3D &o, Coord3D &d) const override
	{
		o = {(float)p.x, (float)p.y, 1000};
		d = {0, 0, -1};
		return true;
	}
	Coord3D position() const override
	{
		return {};
	}
	void lookAt(const Coord3D &) override
	{
	}
};
struct Rig
{
	combattest::CombatWorld world;
	FlatView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*world.logic, world.ai.get(), view, ui, stream, nullptr, {}};
	FormationAssistant formations;
	FormationPlacement placement{ctx};
	Rig()
	{
		std::string error;
		REQUIRE(formations.scan("FormationAssistant\n UnitDefinition Unit\n ObjectFilter = ALL\n End\n FormationTemplate Line\n Rows\n Row = Unit\n End\n "
								"End\n FormationSelection Line\n MaxDragLength = 400\n MaxUnitsSelected = 10\n End\nEnd\n",
								&error));
		ctx.formations = &formations;
		Object *unit = world.unit("Slasher", 'A', 100, 100);
		world.logic->players().setLocalPlayer(unit->getControllingPlayer());
		ui.selectObject(unit->getID());
		stream.attachTranslator(&placement, 35);
	}
	std::vector<ClientMessage> send(const ClientMessage &m)
	{
		stream.append(m.type()) = m;
		return stream.propagate();
	}
};
} // namespace

TEST_CASE("formation chord: BFME2 RVA 0x431BDD requires opposite buttons, <500 ms and <5 pixels on each axis")
{
	const auto first = button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 1000);
	CHECK(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 104, 96, 1499)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100, 1500)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 105, 100, 1001)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 95, 1001)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 1001)));
	CHECK(FormationPlacement::isStartOfDrag(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100, -20), button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 20)));
	CHECK_FALSE(FormationPlacement::withinChordDistance({std::numeric_limits<int>::min(), 0}, {std::numeric_limits<int>::max(), 0}));
}

TEST_CASE("formation chord: native first-press anchor, either release commits once, both resulting clicks are eaten")
{
	for (bool rightFirst : {false, true})
	{
		Rig r;
		int first = rightFirst ? CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN : CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN;
		int second = rightFirst ? CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN : CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN;
		CHECK(r.send(button(first, 200, 200, 1000)).empty());
		CHECK_FALSE(r.ui.formationActive);
		CHECK(r.send(button(second, 204, 204, 1020)).empty());
		REQUIRE(r.ui.formationActive);
		REQUIRE(r.ui.formationSlots.size() == 1);
		CHECK(r.ui.formationSlots[0].position.x == 200.f);
		CHECK(r.ui.formationSlots[0].position.y == 200.f);
		auto out = r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 204, 204, 1040));
		REQUIRE(out.size() == 1);
		CHECK(out[0].type() == MSG_DO_MOVETO_FORMATION);
		CHECK(out[0].arg(2).integer == 0);
		CHECK_FALSE(out[0].arg(3).boolean);
		CHECK_FALSE(r.ui.formationActive);
		CHECK(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_UP, 204, 204, 1060)).empty());
		CHECK(r.send(ClientMessage(CMSG_MOUSE_LEFT_CLICK)).empty());
		CHECK(r.send(ClientMessage(CMSG_MOUSE_RIGHT_CLICK)).empty());
	}
}

TEST_CASE("formation chord: failed chord replays the original press before the event, preserving modifiers and timestamp")
{
	for (int failure : {0, 1, 2, 3})
	{
		Rig r;
		auto first = button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000);
		REQUIRE(r.send(first).empty());
		ClientMessage next = failure == 0	? button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1500)
							 : failure == 1 ? button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 205, 200, 1020)
							 : failure == 2 ? button(CMSG_RAW_MOUSE_POSITION, 205, 200, 1020)
											: button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 200, 200, 1020);
		auto out = r.send(next);
		REQUIRE(out.size() == 2);
		CHECK(out[0].type() == first.type());
		CHECK(out[0].arg(0).pixel.x == 200);
		CHECK(out[0].arg(1).integer == 123);
		CHECK(out[0].arg(2).integer == 1000);
		CHECK(out[1].type() == next.type());
		CHECK_FALSE(r.ui.formationActive);
	}
}

TEST_CASE("formation chord: failed placement replays both buttons; reset discards a pending first press")
{
	Rig r;
	const ObjectID unit = r.ui.selected()[0];
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	// the selection empties between the presses: placement cannot start, both presses go on in their order
	r.ui.deselectAll(false);
	auto out = r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1020));
	REQUIRE(out.size() == 2);
	CHECK(out[0].type() == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN);
	CHECK(out[1].type() == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN);
	CHECK_FALSE(r.ui.formationActive);
	r.placement.cancel();
	r.ui.selectObject(unit);
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 2000)).empty());
	r.placement.cancel();
	out = r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 200, 200, 2020));
	REQUIRE(out.size() == 1);
	CHECK(out[0].type() == CMSG_RAW_MOUSE_LEFT_BUTTON_UP);
}

TEST_CASE("formation chord: S-4464 explicitly reports the remaining reconstructed layout and commands")
{
	const std::string expected = "[S-4464] Two-button formation: BFME2 gesture, INI cell filters, repeated rows, selection criteria, preview textures and "
								 "four-argument formation orders are ported. "
								 "RotWK parity, native formation-group pathing/slot optimization and firing-arc AI state parity remain unverified; current "
								 "execution uses per-slot movement and locomotor facing.";
	CHECK(FormationPlacement::stopLine() == expected);
	const auto stops = HudInput::acceptanceStops();
	CHECK(std::count(stops.begin(), stops.end(), expected) == 1);
}

TEST_CASE("formation chord: ordinary clicks and box drags still pass through the complete HUD pipeline")
{
	combattest::CombatWorld world;
	FlatView view;
	MetaMap meta;
	CommandList commands;
	Object *unit = world.unit("Slasher", 'A', 100, 100);
	world.logic->players().setLocalPlayer(unit->getControllingPlayer());
	HudInput input(*world.logic, world.ai.get(), view, commands, MouseSettings{15, 5, 150}, meta);
	input.commandTranslator().setUseAlternateMouse(true);
	auto down = [&](int x, int y, int time)
	{
		input.mouseButton(HudInput::Button::Left, true, x, y, 0, time);
		input.update();
	};
	auto up = [&](int x, int y, int time)
	{
		input.mouseButton(HudInput::Button::Left, false, x, y, 0, time);
		input.update();
	};
	down(100, 100, 1000);
	up(100, 100, 1020);
	CHECK(input.ui().selected() == std::vector<ObjectID>{unit->getID()});
	input.ui().deselectAll(false);
	down(70, 70, 2000);
	input.mouseMove(130, 130, 0);
	input.update();
	up(130, 130, 2200);
	CHECK(input.ui().selected() == std::vector<ObjectID>{unit->getID()});
	CHECK_FALSE(input.ui().formationActive);
}

TEST_CASE("formation chord: after a failed drag the next down starts a new wait even while the other button is held")
{
	Rig r;
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	CHECK(r.send(button(CMSG_RAW_MOUSE_POSITION, 205, 200, 1020)).size() == 2);
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1040)).empty());
	CHECK(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 300, 1060)).size() == 1);
	CHECK(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 300, 1080)).empty());
	CHECK(r.ui.formationActive);
	REQUIRE(r.ui.formationSlots.size() == 1);
	CHECK(r.ui.formationSlots[0].position.x == 300.f);
	CHECK(r.ui.formationSlots[0].position.y == 300.f);
}

TEST_CASE("formation assistant: INI selection limits, repeated ordered rows and preview descriptors")
{
	Rig r;
	std::string error;
	REQUIRE(
		r.formations.scan("Object Preview\n FormationPreviewDecal\n Texture = UnitTexture\n Width = 32\n Height = 16\n End\nEnd\nFormationAssistant\n "
						  "RowPadding = 10\n ColumnPadding = 10\n UnitDefinition Unit\n ObjectFilter = ALL\n PreviewObject = Preview\n End\n FormationTemplate "
						  "Two\n Rows\n Row = Unit Unit\n End\n End\n FormationSelection Two\n MaxDragLength = 80\n MaxUnitsSelected = 4\n End\nEnd\n",
						  &error));
	CHECK(r.formations.choose(80, 4) == 0);
	CHECK(r.formations.choose(800, 4) == 0);
	REQUIRE(r.formations.decal("Preview") != nullptr);
	CHECK(r.formations.decal("Preview")->texture == "UnitTexture");
	CHECK(r.formations.decal("Preview")->width == 32);
	CHECK(r.formations.decal("Preview")->height == 16);
	auto *b = r.world.unit("Slasher", 'A', 100, 110), *c = r.world.unit("Slasher", 'A', 100, 120);
	const auto slots = r.formations.layout(*r.world.logic, {r.ui.selected()[0], b->getID(), c->getID()}, 0, {100, 100, 0});
	REQUIRE(slots.size() == 3);
	CHECK(slots[0].offset.x == -21);
	CHECK(slots[1].offset.x == 21);
	CHECK(slots[2].offset.x == 0);
	CHECK(slots[2].offset.y == 27);
	CHECK(slots[0].preview == "Preview");
	CHECK_FALSE(r.formations.scan("FormationAssistant\n FormationTemplate Bad\n Rows\n Row = A B\n Row = A\n End\n End\nEnd\n", &error));
	CHECK(error.find("row widths differ") != std::string::npos);
}

TEST_CASE("formation chord: attack-move payload and release cleanup")
{
	Rig r;
	r.ui.setAttackMoveToMode(true);
	r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000));
	r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1020));
	const auto messages = r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_UP, 250, 200, 1040));
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].type() == MSG_DO_MOVETO_FORMATION);
	CHECK(messages[0].arg(3).boolean);
	CHECK_FALSE(r.ui.isInAttackMoveToMode());
	CHECK(r.ui.formationExpires == r.ui.clientFrame() + 45);
}

TEST_CASE("formation assistant: execution excludes the same dead and filtered selections as preview")
{
 Rig r;
 auto *dead=r.world.unit("Slasher",'A',100,101);
 auto *filtered=r.world.unit("Dummy",'A',100,102);
 dead->friend_setEffectivelyDead(true);
 std::string error;
 INIEnvironment environment;
 environment.blocks.registerBlock("Filter",[&](INI *ini){const FieldParse fields[]={{"Value",ParseObjectFilter,nullptr,0},{nullptr,nullptr,nullptr,0}};ini->initFromINI(&r.formations.valid,fields);});
 INI ini(environment);
 const std::string text="Filter\n Value = ALL -Dummy\nEnd\n";
 ini.loadMemory("formation-filter.ini",std::vector<std::uint8_t>(text.begin(),text.end()),INI_LOAD_OVERWRITE);
 const auto eligible=r.formations.layout(*r.world.logic,{r.ui.selected()[0]},0,{200,200,0});
 const auto mixed=r.formations.layout(*r.world.logic,{dead->getID(),filtered->getID(),r.ui.selected()[0]},0,{200,200,0});
 REQUIRE(eligible.size()==1);
 REQUIRE(mixed.size()==1);
 CHECK(mixed[0].object==eligible[0].object);
 CHECK(mixed[0].offset.x==eligible[0].offset.x);
 CHECK(mixed[0].offset.y==eligible[0].offset.y);
}

TEST_CASE("formation assistant: the occupied rank centre is the horde object's "
		  "origin until a casualty moves it")
{
	movetest::MoveWorld world;
	auto *horde = world.spawn("Horde", 305, 305);
	auto *walker = world.spawn("Walker", 505, 305);
	const auto offsets = FormationAssistant::rankOffsets(*horde);
	REQUIRE(offsets.size() == 15);
	// community #73: RotWK centres the slot table on the horde (RW 0x86C4EA), so the full ranks' centroid is the object's origin
	const auto center = FormationAssistant::rankCenter(*horde);
	CHECK(std::fabs(center.x) < 1e-4f); // tolerance: the float residue of the centring's sum, not a retail value
	CHECK(center.y == 0);
	CHECK(FormationAssistant::rankOffsets(*walker).empty());
	CHECK(FormationAssistant::rankCenter(*walker).x == 0);
	CHECK(FormationAssistant::rankCenter(*walker).y == 0);
	// A casualty changes the occupied centroid; do not substitute the first N
	// slots.
	auto *contain = world.hordeOf(horde);
	const auto *members = contain->getContainedItemsList();
	REQUIRE_FALSE(members->empty());
	const int deadSlot = contain->getMemberSlot(members->front());
	REQUIRE(deadSlot >= 0); // tolerance: any slot of the table (-1 = no slot)
	const float deadX = contain->core().slots()[deadSlot].x;
	CHECK(deadX != 0); // tolerance: the claim is only that the casualty stood off the centre line, its slot is the table's
	members->front()->friend_setEffectivelyDead(true);
	const auto reduced = FormationAssistant::rankOffsets(*horde);
	CHECK(reduced.size() == 14);
	CHECK(FormationAssistant::rankCenter(*horde).x == doctest::Approx((15.0f * center.x - deadX) / 14.0f)); // tolerance: the float sums run in member order
}

TEST_CASE("formation assistant: hero sprite footprint and unequal row depths "
		  "leave a gap")
{
	movetest::MoveWorld world;
	auto *horde = world.spawn("Horde", 305, 305);
	auto *hero = world.spawn("Walker", 505, 305);
	FormationAssistant formations;
	std::string error;
	REQUIRE(formations.scan("Object HeroPreview\n FormationPreviewItemDecal\n "
							"Texture = Hero\n Width = 128\n Height = 128\n End\nEnd\n"
							"FormationAssistant\n RowPadding = 10\n UnitDefinition "
							"Battalion\n ObjectFilter = NONE +Horde\n End\n"
							"UnitDefinition Hero\n ObjectFilter = NONE +Walker\n "
							"PreviewObject = HeroPreview\n End\n"
							"FormationTemplate Mixed\n Rows\n Row = Battalion\n Row "
							"= Hero\n End\n End\nEnd\n",
							&error));
	const auto slots = formations.layout(*world.logic, {horde->getID(), hero->getID()}, 0, {305, 305, 0});
	REQUIRE(slots.size() == 2);
	CHECK(slots[0].object == horde->getID());
	CHECK(slots[1].object == hero->getID());
	CHECK(slots[0].offset.y == 0);
	// Half the horde's 64-unit depth + half the 128-unit decal + 10 padding.
	CHECK(slots[1].offset.y == 106);
}

TEST_CASE("formation assistant: movement targets the occupied rank centre at "
		  "each cardinal facing")
{
	for (float angle : {0.0f, 1.5707964f, 3.1415927f})
	{
		movetest::MoveWorld world;
		world.buildMap();
		auto *horde = world.spawn("Horde", 305, 305);
		world.frames(2);
		FormationAssistant formations;
		std::string error;
		REQUIRE(formations.scan("FormationAssistant\n UnitDefinition Unit\n ObjectFilter = ALL\n End\n "
								"FormationTemplate Line\n Rows\n Row = Unit\n End\n End\nEnd\n",
								&error));
		world.aiCommands.setFormationAssistant(&formations);
		const auto player = world.playerIndex("Alice");
		world.select(player, {horde});
		GameMessage message(MSG_DO_MOVETO_FORMATION, player);
		message.appendLocationArgument({605, 605, 0});
		message.appendRealArgument(angle);
		message.appendIntegerArgument(0);
		message.appendBooleanArgument(false);
		world.send(message);
		world.frames(100);
		const auto *contain = world.hordeOf(horde);
		Coord2D center{};
		for (const auto *member : *contain->getContainedItemsList())
		{
			center.x += member->getPosition()->x;
			center.y += member->getPosition()->y;
		}
		center.x /= 15;
		center.y /= 15;
		const auto dx = center.x - 605, dy = center.y - 605;
		// tolerance: 10-unit grid snapping plus 1-unit locomotor stopping distance.
		CHECK(dx * dx + dy * dy < 225);
	}
}

// PR #88 review fix 2: the first press is held back only when a formation could start; otherwise selection and the LookAt translator see it
// in the same frame (test_camera's right-press arming, test_build5_placement's scroll while placing)
TEST_CASE("formation chord: a first press that cannot start a formation is not held back")
{
	for (int reason : { 0, 1, 2, 3, 4, 5 })
	{
		Rig r;
		CommandButton other;
		other.m_command = GUI_COMMAND_SPECIAL_POWER;
		if (reason == 0)
			r.ui.deselectAll(false); // no eligible selection
		else if (reason == 1)
			r.ui.placeBuildAvailable("Slasher", r.ui.selected()[0]); // placing a building
		else if (reason == 2)
			r.ui.setGUICommand(&other); // a GUI command other than attack-move waits for its target
		else if (reason == 3)
			r.ctx.formations = nullptr; // no formation data
		else if (reason == 4)
			r.formations.templates.clear(); // formation data without templates (BFME1-style data)
		else
			r.ui.setInputEnabled(false);
		for (int type : { CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN })
		{
			auto out = r.send(button(type, 200, 200, 1000));
			REQUIRE(out.size() == 1);
			CHECK(out[0].type() == type);
		}
		CHECK_FALSE(r.ui.formationActive);
	}
	// an eligible selection with formation data: the press waits for the second button
	Rig r;
	CHECK(r.placement.canStart({ 200, 200 }));
	CHECK(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1000)).empty());
}

// PR #88 review fix 1: after the order the gesture is over once both buttons are up; only the trailing release and clicks are swallowed, the
// HUD no longer cancels the LookAt pointer (camera scrolling works again)
TEST_CASE("formation chord: the gesture stays live until both buttons are released, then only the trailing clicks are eaten")
{
	Rig r;
	CHECK_FALSE(r.placement.gestureLive());
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1020)).empty());
	CHECK(r.placement.gestureLive());
	auto out = r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 250, 200, 1040));
	REQUIRE(out.size() == 1);
	CHECK(out[0].type() == MSG_DO_MOVETO_FORMATION);
	CHECK_FALSE(r.ui.formationActive);
	CHECK(r.placement.gestureLive()); // the right button is still down
	CHECK(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_UP, 250, 200, 1060)).empty());
	CHECK_FALSE(r.placement.gestureLive());
	CHECK(r.placement.consumed());
	CHECK(r.send(ClientMessage(CMSG_MOUSE_RIGHT_CLICK)).empty());
	// pointer motion and keys pass (the camera's edge and key scrolling read them)
	out = r.send(button(CMSG_RAW_MOUSE_POSITION, 900, 600, 1080));
	REQUIRE(out.size() == 1);
	CHECK(out[0].type() == CMSG_RAW_MOUSE_POSITION);
	CHECK_FALSE(r.placement.gestureLive());
}

// PR #88 review fix 3: an order that took the pending attack-move button leaves the GUI command mode, so the next left click is a plain click
TEST_CASE("formation chord: a formation order made with the attack-move button clears the GUI command")
{
	Rig r;
	CommandButton attackMove;
	attackMove.m_command = GUI_COMMAND_ATTACK_MOVE;
	r.ui.setGUICommand(&attackMove);
	REQUIRE(r.placement.canStart({ 200, 200 }));
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1020)).empty());
	REQUIRE(r.ui.formationActive);
	const auto out = r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_UP, 250, 200, 1040));
	REQUIRE(out.size() == 1);
	CHECK(out[0].type() == MSG_DO_MOVETO_FORMATION);
	CHECK(out[0].arg(3).boolean);
	CHECK(r.ui.getGUICommand() == nullptr);
	CHECK_FALSE(r.ui.isInAttackMoveToMode());
}
