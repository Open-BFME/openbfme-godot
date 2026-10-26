// OpenBFME retail tests of the HUD input (lane HUD-1): synthesised mouse and keyboard events through HudInput into the lockstep command list and the logic.
// Retail cases SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset; synthetic HUD cases need no game files. GPL-3.0.

#include "HudTestUtil.h"
#include "SyntheticHudTestUtil.h"
#include "AptPlayerTestUtil.h"
#include "CombatTestUtil.h"
#include "GameClient/FormationAssistant.h"
#include "GameClient/TacticalCamera.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include "GameClient/HudObjects.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <algorithm>

using namespace hudtest;

namespace
{
Coord3D center(Rig &r)
{
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	return Coord3D{ mx * 0.5f, my * 0.5f, 0.0f };
}

bool logHas(const HudInput &in, const std::string &prefix)
{
	for (const std::string &l : in.messageLog())
	{
		if (l.compare(0, prefix.size(), prefix) == 0)
		{
			return true;
		}
	}
	return false;
}

size_t logCount(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		if (l.compare(0, prefix.size(), prefix) == 0)
		{
			++n;
		}
	}
	return n;
}

std::vector<ObjectID> selectionOf(Rig &r)
{
	return r.local->selection();
}
} // namespace

TEST_CASE("hud: the CommandMap of the install parses and carries the retail bindings")
{
	if (!haveWorld("hud commandmap"))
	{
		return;
	}
	SharedWorld &s = shared();
	// 94 names in the language file; the default file adds nothing new (SAVE_VIEW1 is in both)
	// 94 blocks in the language file plus RELOAD / REFRESH_RAPID_ITERATION_FEATURE from the default file
	CHECK(s.meta.records().size() == 96);
	const MetaMapRec *stop = s.meta.find(CMSG_META_STOP);
	REQUIRE(stop != nullptr);
	CHECK(stop->key == KEY_S);
	CHECK(stop->modState == MOD_NONE);
	CHECK(stop->usableIn == COMMANDUSABLE_GAME);
	const MetaMapRec *create1 = s.meta.find(CMSG_META_CREATE_TEAM1);
	REQUIRE(create1 != nullptr);
	CHECK(create1->key == KEY_1);
	CHECK(create1->modState == MOD_CTRL);
	const MetaMapRec *select1 = s.meta.find(CMSG_META_SELECT_TEAM1);
	REQUIRE(select1 != nullptr);
	CHECK(select1->key == KEY_1);
	CHECK(select1->modState == MOD_NONE);
	const MetaMapRec *prefer = s.meta.find(CMSG_META_BEGIN_PREFER_SELECTION);
	REQUIRE(prefer != nullptr);
	CHECK(prefer->key == KEY_NONE); // a modifier-only record
	CHECK(prefer->modState == MOD_SHIFT);
}

TEST_CASE("hud input: a click selects a unit, the message reaches the logic and the selection is the player's (hashed)")
{
	if (!haveWorld("hud input"))
	{
		return;
	}
	SharedWorld &s = shared();
	REQUIRE(!templates(s).infantry.empty());
	Rig r(s);
	const Coord3D c = r.freeSpot(center(r).x, center(r).y);
	Object *unit = r.make(templates(s).infantry.front(), c.x, c.y);
	r.lookAt(c);
	r.frame(2);
	const Coord3D at = *unit->getPosition();
	const ICoord2D px = r.screenOf({ at.x, at.y, at.z + 6.0f });
	const std::uint32_t before = r.logic().computeStateHash();
	r.leftClick(px.x, px.y);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
	CHECK(logHas(*r.input, "MSG_CREATE_SELECTED_GROUP"));
	CHECK(selectionOf(r).empty()); // nothing reaches the logic before its next frame
	r.frame(1);
	CHECK(selectionOf(r) == std::vector<ObjectID>{ unit->getID() });
	CHECK(r.logic().computeStateHash() != before);
	// a click on empty ground in the standard setup is an order: the left click that selects nothing is a move
	r.input->commandTranslator().setUseAlternateMouse(false);
	const ICoord2D ground = r.screenOf({ c.x + 120.0f, c.y + 80.0f, 0.0f });
	r.leftClick(ground.x, ground.y);
	CHECK(logHas(*r.input, "MSG_DO_MOVETO"));
	// the right click deselects in the standard setup
	r.rightClick(ground.x, ground.y);
	CHECK(r.input->ui().selected().empty());
	CHECK(logHas(*r.input, "MSG_DESTROY_SELECTED_GROUP"));
}

TEST_CASE("hud input: a drag box selects the units inside, shift adds and toggles, a double click selects the units of the type on the screen")
{
	if (!haveWorld("hud input drag"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = center(r);
	r.lookAt(c0);
	// three units of one type in a row, a fourth of another type apart (positions free of map objects)
	const std::string a = templates(s).infantry.front();
	std::string other = a;
	for (const std::string &t : templates(s).infantry)
	{
		if (t != a)
		{
			other = t;
			break;
		}
	}
	std::vector<Object *> row;
	Coord3D base = r.freeSpot(c0.x, c0.y, 200.0f);
	for (int i = 0; i < 3; ++i)
	{
		row.push_back(r.make(a, base.x + 40.0f * (float)i, base.y));
	}
	Object *apart = r.make(other, base.x + 40.0f * 8.0f, base.y);
	r.lookAt({ base.x + 160.0f, base.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, o->getPosition()->z + 6.0f }); };
	// drag a box around the row
	const ICoord2D p0 = px(row[0]), p2 = px(row[2]);
	r.leftDrag(p0.x - 25, p0.y - 40, p2.x + 25, p2.y + 40);
	CHECK(r.input->ui().getSelectCount() == 3);
	CHECK(r.input->ui().isSelected(row[0]->getID()));
	CHECK(r.input->ui().isSelected(row[2]->getID()));
	CHECK(!r.input->ui().isSelected(apart->getID()));
	r.frame(1);
	CHECK(selectionOf(r).size() == 3);
	// shift + click on the apart unit adds it; the same click again removes it
	r.setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	CHECK(r.input->ui().isInPreferSelectionMode());
	const ICoord2D pa = px(apart);
	r.leftClick(pa.x, pa.y);
	CHECK(r.input->ui().getSelectCount() == 4);
	r.leftClick(pa.x, pa.y);
	CHECK(r.input->ui().getSelectCount() == 3);
	CHECK(logHas(*r.input, "MSG_REMOVE_FROM_SELECTED_GROUP"));
	r.setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	CHECK(!r.input->ui().isInPreferSelectionMode());
	r.frame(1);
	CHECK(selectionOf(r).size() == 3);
	// a plain click on a unit of the row replaces the selection; a double click on it takes the whole row (the units of its type on the screen)
	const ICoord2D p1 = px(row[1]);
	r.leftClick(p1.x, p1.y);
	CHECK(r.input->ui().getSelectCount() == 1);
	r.leftClick(p1.x, p1.y, true);
	CHECK(r.input->ui().getSelectCount() >= 3);
	CHECK_FALSE(logHas(*r.input, "MSG_AREA_SELECTION"));
	r.frame(1);
	CHECK(selectionOf(r).size() == r.input->ui().selected().size());
}

TEST_CASE("hud input: the orders of the selection go through the command list and the logic runs them (alternate setup: right click orders)")
{
	if (!haveWorld("hud input orders"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(center(r).x, center(r).y, 200.0f);
	Object *unit = r.make(templates(s).infantry.front(), c0.x, c0.y);
	r.lookAt(c0);
	r.frame(2);
	const ICoord2D pu = r.screenOf({ c0.x, c0.y, 6.0f });
	r.leftClick(pu.x, pu.y);
	r.frame(1);
	REQUIRE(selectionOf(r).size() == 1);
	r.input->commandTranslator().setUseAlternateMouse(true);
	const Coord3D target{ c0.x + 150.0f, c0.y + 100.0f, 0.0f };
	const ICoord2D pg = r.screenOf(target);
	const Coord3D start = *unit->getPosition();
	for (int i = 0; i < 41; ++i)
	{
		r.input->ui().advanceClientFrame(); // RW 0x48EDED draws no hint while the client frame is below 41
	}
	r.rightClick(pg.x, pg.y);
	CHECK(logCount(*r.input, "MSG_DO_MOVETO") == 1);
	CHECK(selectionOf(r).size() == 1); // the right click ordered, it did not deselect
	// lane PLAY-1: the order leaves a move hint at the clicked ground point (ZH HintSpy -> InGameUI::createMoveHint), alive for 40 client frames
	{
		const InGameUI &ui = r.input->ui();
		REQUIRE(ui.liveMoveHintCount() == 1);
		CHECK(ui.moveHintsMade() == 1);
		REQUIRE(ui.moveHintDrawn(0));
		const InGameUI::MoveHint &h = ui.moveHints()[0];
		CHECK(std::hypot(h.pos.x - target.x, h.pos.y - target.y) < 8.0f);
		for (unsigned i = 0; i < InGameUI::MOVE_HINT_FRAMES; ++i)
		{
			r.input->ui().advanceClientFrame();
		}
		CHECK(ui.liveMoveHintCount() == 1); // age 40: still drawn (ZH: elapsed <= 40)
		r.input->ui().advanceClientFrame();
		CHECK(ui.liveMoveHintCount() == 0);
	}
	r.frame(30);
	const Coord3D now = *unit->getPosition();
	const float d0 = std::hypot(start.x - target.x, start.y - target.y), d1 = std::hypot(now.x - target.x, now.y - target.y);
	CHECK(d1 < d0 - 40.0f); // the logic executed the move: the unit walked toward the point
	// stop (the S key of the CommandMap) and a waypoint (shift modifier is the prefer-selection mode; the waypoint mode is its own key in retail: ORDERMODE_WAYPOINT)
	r.pressKey(KEY_S);
	CHECK(logCount(*r.input, "MSG_DO_STOP") == 1);
	r.frame(2);
	CHECK(r.game->dispatch().unhandled().count(MSG_DO_STOP) == 0);
}

TEST_CASE("hud input: control groups (ctrl+digit creates, the digit selects, shift+digit adds) and the squads are hashed state")
{
	if (!haveWorld("hud input groups"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(center(r).x, center(r).y, 200.0f);
	std::vector<Object *> us;
	for (int i = 0; i < 2; ++i)
	{
		us.push_back(r.make(templates(s).infantry.front(), c0.x + 50.0f * (float)i, c0.y));
	}
	r.lookAt({ c0.x + 25.0f, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, 6.0f }); };
	r.leftClick(px(us[0]).x, px(us[0]).y);
	r.pressKey(KEY_1, KEY_STATE_LCONTROL); // CREATE_TEAM1
	CHECK(logHas(*r.input, "MSG_CREATE_TEAM1"));
	const std::uint32_t h0 = r.logic().computeStateHash();
	r.frame(1);
	CHECK(r.local->hotkeySquad(1) == std::vector<ObjectID>{ us[0]->getID() });
	CHECK(r.logic().computeStateHash() != h0); // the squad is in the hash
	// deselect (right click in the standard setup), then the digit selects the group again
	r.rightClick(px(us[0]).x + 200, px(us[0]).y + 150);
	r.frame(1);
	CHECK(r.local->selection().empty());
	r.pressKey(KEY_1);
	CHECK(logHas(*r.input, "MSG_SELECT_TEAM1"));
	CHECK(r.input->ui().isSelected(us[0]->getID()));
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ us[0]->getID() });
	// a second unit selected with a click plus shift, group 2 made, group 1 added to the selection of group 2
	r.leftClick(px(us[1]).x, px(us[1]).y);
	r.pressKey(KEY_2, KEY_STATE_LCONTROL);
	r.frame(1);
	CHECK(r.local->hotkeySquad(2) == std::vector<ObjectID>{ us[1]->getID() });
	r.pressKey(KEY_2);
	r.pressKey(KEY_1, KEY_STATE_LSHIFT); // ADD_TEAM1
	r.frame(1);
	CHECK(r.local->selection().size() == 2);
	CHECK(r.game->dispatch().unhandled().empty());
}

TEST_CASE("hud input: a control group made and read again before the next logic frame selects the same units in the HUD and the logic (review r1 #2)")
{
	if (!haveWorld("hud input group prediction"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(center(r).x, center(r).y, 200.0f);
	Object *u = r.make(templates(s).infantry.front(), c0.x, c0.y);
	r.lookAt({ c0.x, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	const ICoord2D p = r.screenOf({ u->getPosition()->x, u->getPosition()->y, 6.0f });
	r.leftClick(p.x, p.y);
	REQUIRE(r.input->ui().isSelected(u->getID()));
	r.pressKey(KEY_1, KEY_STATE_LCONTROL); // CREATE_TEAM1, queued
	r.rightClick(p.x + 200, p.y + 150);    // deselect (standard setup)
	CHECK(r.input->ui().selected().empty());
	r.pressKey(KEY_1); // SELECT_TEAM1 before the logic frame ran the create: the group is the predicted one
	CHECK(r.input->ui().isSelected(u->getID()));
	r.frame(2);
	CHECK(r.local->hotkeySquad(1) == std::vector<ObjectID>{ u->getID() });
	CHECK(r.local->selection() == std::vector<ObjectID>{ u->getID() });
	CHECK(r.input->ui().selected() == r.local->selection()); // HUD == logic
	// the prediction is gone once the frame ran: a group the logic changed is read from the logic
	r.rightClick(p.x + 200, p.y + 150);
	r.frame(1);
	r.pressKey(KEY_1);
	r.frame(1);
	CHECK(r.input->ui().selected() == r.local->selection());
}

TEST_CASE("hud input: a batch of raw events gives the messages the same events drained one by one give (review r1 #1)")
{
	if (!haveWorld("hud input batch"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto session = [&](bool batch) {
		Rig r(s);
		const Coord3D c0 = r.freeSpot(center(r).x, center(r).y, 200.0f);
		Object *u = r.make(templates(s).infantry.front(), c0.x, c0.y);
		r.lookAt({ c0.x, c0.y, 0.0f }, 300.0f, 380.0f);
		r.frame(2);
		const ICoord2D a = r.screenOf({ u->getPosition()->x, u->getPosition()->y, 6.0f });
		const ICoord2D b{ a.x + 120, a.y + 90 };
		auto ev = [&](auto &&fn) {
			fn();
			if (!batch)
			{
				r.input->update();
			}
		};
		// select the unit, then order a move: every event is queued before one update when `batch`
		r.timeMs += 20;
		ev([&] { r.input->mouseMove(a.x, a.y, 0); });
		ev([&] { r.input->mouseButton(HudInput::Button::Left, true, a.x, a.y, 0, r.timeMs); });
		r.timeMs += 40;
		ev([&] { r.input->mouseButton(HudInput::Button::Left, false, a.x, a.y, 0, r.timeMs); });
		r.timeMs += 400;
		ev([&] { r.input->mouseMove(b.x, b.y, 0); });
		ev([&] { r.input->mouseButton(HudInput::Button::Left, true, b.x, b.y, 0, r.timeMs); });
		r.timeMs += 40;
		ev([&] { r.input->mouseButton(HudInput::Button::Left, false, b.x, b.y, 0, r.timeMs); });
		r.input->update();
		return r.input->messageLog();
	};
	const std::vector<std::string> one = session(false);
	const std::vector<std::string> all = session(true);
	bool moved = false;
	for (const std::string &l : one)
	{
		moved = moved || l.compare(0, 14, "MSG_DO_MOVETO ") == 0 || l.compare(0, 13, "MSG_DO_MOVETO") == 0;
	}
	CHECK(moved);
	CHECK(all == one);
}

TEST_CASE("stops S-280 .. S-288: the HUD input reports what it does not port, and an unported meta command is counted, never dropped")
{
	const std::vector<std::string> stops = HudInput::acceptanceStops();
	REQUIRE(stops.size() == 14);
	// the control bar, radar and Palantir report S-289 .. S-294 (their own test: test_hud_palantir.cpp)
	for (size_t i = 0; i < 9; ++i)
	{
		CHECK(stops[i].compare(0, 7, "[S-" + std::to_string(280 + (int)i) + "]") == 0);
	}
	CHECK(stops[9].compare(0, 8, "[S-1770]") == 0); // lane QA2-FIX: the wall line build (PlaceEventTranslator::stopLines)
	CHECK(stops[10].compare(0, 8, "[S-1920]") == 0); // lane PLAY-1: the move hint
	CHECK(stops[11].compare(0, 8, "[S-1954]") == 0); // lane HUD-5: RotWK's pick types
	CHECK(stops[13] == FormationPlacement::stopLine());
	CHECK(stops[12].compare(0, 8, "[S-3302]") == 0); // lane PLAY-1: the placement ghost (renumbered by PLAY-3)
	if (!haveWorld("hud stops"))
	{
		return;
	}
	Rig r(shared());
	r.input->ui(); // a game with the input stack
	r.pressKey(KEY_N, KEY_STATE_NONE); // not bound: nothing
	// lane HUD-4: every meta command RotWK's handler (RW 0x81F8D8) executes is ported; SELECT_NEXT_WORKER (Shift+Up) has no handler in RotWK: its press is counted
	// as a retail no-op (S-1673), never dropped silently
	const MetaMapRec *worker = shared().meta.find(CMSG_META_SELECT_NEXT_WORKER);
	REQUIRE(worker != nullptr);
	r.pressKey(worker->key, worker->modState & MOD_CTRL ? KEY_STATE_LCONTROL : (worker->modState & MOD_SHIFT ? KEY_STATE_LSHIFT : 0));
	CHECK(r.input->commandTranslator().retailNoOpMeta().count("SELECT_NEXT_WORKER") == 1);
	CHECK(r.input->commandTranslator().unportedMeta().empty());
	bool met = false;
	for (const std::string &l : r.input->stops())
	{
		met = met || l.find("met (no retail handler): SELECT_NEXT_WORKER x1") != std::string::npos;
	}
	CHECK(met);
}

namespace
{
// one scripted session: select by a click, order a move, make a control group, queue a unit with the building's button path (message level), a drag box
struct Session
{
	std::vector<std::string> log;
	std::vector<std::uint32_t> hashes;
};

Session runSession(SharedWorld &s)
{
	Rig r(s);
	Session out;
	const Coord3D c = r.freeSpot(center(r).x, center(r).y, 220.0f);
	std::vector<Object *> us;
	for (int i = 0; i < 3; ++i)
	{
		us.push_back(r.make(templates(s).infantry.front(), c.x + 45.0f * (float)i, c.y));
	}
	r.lookAt({ c.x + 45.0f, c.y, 0 }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, 6.0f }); };
	auto stepHash = [&]() { out.hashes.push_back(r.logic().computeStateHash()); };
	r.leftClick(px(us[0]).x, px(us[0]).y);
	r.frame(1);
	stepHash();
	r.pressKey(KEY_1, KEY_STATE_LCONTROL);
	r.frame(1);
	stepHash();
	const ICoord2D p0 = px(us[0]), p2 = px(us[2]);
	r.leftDrag(p0.x - 25, p0.y - 40, p2.x + 25, p2.y + 40);
	r.frame(1);
	stepHash();
	r.input->commandTranslator().setUseAlternateMouse(false);
	const ICoord2D g = r.screenOf({ c.x + 160.0f, c.y + 90.0f, 0.0f });
	r.leftClick(g.x, g.y);
	for (int i = 0; i < 25; ++i)
	{
		r.frame(1);
		stepHash();
	}
	r.pressKey(KEY_S);
	r.frame(3);
	stepHash();
	r.pressKey(KEY_1);
	r.frame(2);
	stepHash();
	out.log = r.input->messageLog();
	return out;
}
} // namespace

TEST_CASE("hud determinism: the same input twice gives the same message stream and the same state hash in every frame")
{
	if (!haveWorld("hud determinism"))
	{
		return;
	}
	SharedWorld &s = shared();
	const Session a = runSession(s), b = runSession(s);
	REQUIRE(!a.log.empty());
	CHECK(a.log == b.log);
	REQUIRE(a.hashes.size() == b.hashes.size());
	CHECK(a.hashes == b.hashes);
	// the stream carries the whole session: selection, group, area / order, stop
	size_t creates = 0, moves = 0, stops = 0, teams = 0;
	for (const std::string &l : a.log)
	{
		creates += l.compare(0, 24, "MSG_CREATE_SELECTED_GROU") == 0;
		moves += l.compare(0, 11, "MSG_DO_MOVE") == 0;
		stops += l.compare(0, 11, "MSG_DO_STOP") == 0;
		teams += l.compare(0, 10, "MSG_CREATE") == 0 && l.find("TEAM") != std::string::npos;
	}
	CHECK(creates >= 2);
	CHECK(moves == 1);
	CHECK(stops == 1);
	CHECK(teams >= 1);
	// the hashes moved with the input (a different session would differ): the first and the last differ
	CHECK(a.hashes.front() != a.hashes.back());
}

TEST_CASE("hud input: a drag box over a horde selects the horde object, not its members")
{
	if (!haveWorld("hud horde select"))
	{
		return;
	}
	SharedWorld &s = shared();
	REQUIRE(!templates(s).hordes.empty());
	Rig r(s);
	const Coord3D c = r.freeSpot(center(r).x, center(r).y, 250.0f);
	Object *horde = r.make(templates(s).hordes.front(), c.x, c.y);
	r.lookAt({ c.x, c.y, 0 }, 300.0f, 380.0f);
	r.frame(4);
	REQUIRE(horde->getContain() != nullptr);
	CHECK(horde->getContain()->getContainCount() > 3);
	const ICoord2D p = r.screenOf({ c.x, c.y, 6.0f });
	r.leftDrag(p.x - 120, p.y - 100, p.x + 120, p.y + 100);
	REQUIRE(r.input->ui().getSelectCount() == 1);
	CHECK(r.input->ui().selected().front() == horde->getID());
	// a click on a member picks the horde too
	r.input->ui().deselectAll();
	r.frame(1);
	const Object *member = horde->getContain()->getContainedItemsList()->front();
	const ICoord2D pm = r.screenOf({ member->getPosition()->x, member->getPosition()->y, member->getPosition()->z + 6.0f });
	r.leftClick(pm.x, pm.y);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ horde->getID() });
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ horde->getID() });
}

TEST_CASE("rotwk-c pick retail: a structure double click does not expand selection")
{
	if (!haveWorld("rotwk-c pick"))
	{
		return;
	}
	Rig r(shared());
	const Coord3D c = r.freeSpot(center(r).x, center(r).y, 600.0f);
	Object *a = r.make("GondorBarracks", c.x, c.y);
	Object *b = r.make("GondorBarracks", c.x + 180.0f, c.y);
	r.lookAt({ c.x + 90.0f, c.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	const ICoord2D px = r.screenOf({ a->getPosition()->x, a->getPosition()->y, a->getPosition()->z + 6.0f });
	r.leftClick(px.x, px.y);
	REQUIRE(r.input->ui().isSelected(a->getID()));
	REQUIRE_FALSE(r.input->ui().isSelected(b->getID()));
	r.leftClick(px.x, px.y, true);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ a->getID() });
	r.frame(1);
	CHECK(selectionOf(r) == std::vector<ObjectID>{ a->getID() });
}

TEST_CASE("rotwk-c pick retail: double click expands current selection, not the picked type, and ignores Alt")
{
	if (!haveWorld("rotwk-c current selection"))
	{
		return;
	}
	Rig r(shared());
	const Coord3D c = r.freeSpot(center(r).x, center(r).y, 600.0f);
	Object *seed = r.make("GondorFighter", c.x, c.y);
	Object *match = r.make("GondorFighter", c.x + 80.0f, c.y);
	Object *picked = r.make("GondorArcher", c.x + 200.0f, c.y);
	Object *far = r.make("GondorFighter", c.x + 2000.0f, c.y);
	r.lookAt({ c.x + 100.0f, c.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto pixel = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, o->getPosition()->z + 6.0f }); };
	const ICoord2D seedPixel = pixel(seed), pickedPixel = pixel(picked);
	REQUIRE(HudObjects::pickForDoubleClick(r.input->context(), pickedPixel) == picked);
	r.leftClick(seedPixel.x, seedPixel.y);
	r.frame();
	REQUIRE(r.input->ui().selected() == std::vector<ObjectID>{ seed->getID() });
	std::vector<int> types;
	r.input->setLogicMessageObserver([&](const GameMessage &m) { types.push_back(m.getType()); });
	auto doubleClick = [&]() {
		ClientMessage &m = r.input->stream().append(CMSG_MOUSE_LEFT_DOUBLE_CLICK);
		m.appendPixelRegion({ pickedPixel, pickedPixel });
		m.appendInteger(KEY_STATE_ALT);
		r.input->update();
	};
	r.input->ui().setForceAttackMode(true);
	doubleClick();
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ seed->getID() });
	r.input->ui().setForceAttackMode(false);
	doubleClick();
	CHECK(r.input->ui().isSelected(seed->getID()));
	CHECK(r.input->ui().isSelected(match->getID()));
	CHECK_FALSE(r.input->ui().isSelected(picked->getID()));
	CHECK_FALSE(r.input->ui().isSelected(far->getID()));
	const std::vector<int> expected{ MSG_CREATE_SELECTED_GROUP_NO_SOUND };
	CHECK(types == expected); // no replace-selection or MSG_AREA_SELECTION
	r.frame();
	CHECK(std::find(r.local->selection().begin(), r.local->selection().end(), match->getID()) != r.local->selection().end());
	r.input->ui().deselectAll(false);
	types.clear();
	doubleClick();
	CHECK(r.input->ui().selected().empty());
	CHECK(types.empty());
	r.input->setLogicMessageObserver({});
}

TEST_CASE("rotwk-c pick retail: special-power wall selectability overrides only the script flag")
{
	if (!haveWorld("rotwk-c selectability"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep");
	Object *wall = nullptr;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == "HelmsDeepGatehouseCenter")
		{
			wall = o;
			break;
		}
	}
	REQUIRE(wall);
	REQUIRE(wall->isKindOfName("WALK_ON_TOP_OF_WALL"));
	CHECK_FALSE(wall->isScriptSelectable()); // RW 0x69A648: the initial flag comes from SELECTABLE, not a universal true
	wall->setScriptSelectable(true);
	CHECK(HudObjects::isSelectable(*wall)); // an explicit script override can select a non-SELECTABLE template
	CommandButton command;
	command.m_command = GUI_COMMAND_SPECIAL_POWER;
	wall->setScriptSelectable(false);
	CHECK_FALSE(HudObjects::isSelectable(*wall));
	CHECK(HudObjects::isSelectable(*wall, &command));
	wall->setStatus(3, true);
	CHECK_FALSE(HudObjects::isSelectable(*wall, &command));
	wall->setStatus(3, false);
	command.m_command = GUI_COMMAND_ATTACK_MOVE;
	CHECK_FALSE(HudObjects::isSelectable(*wall, &command));
}

#include "GameClient/PlayerMessageLog.h"
#include "GameClient/RadiusCursor.h"
TEST_CASE("player message log: native INI settings and six alternating entries")
{
	RadiusCursorTemplates parsed;
	std::string error;
	REQUIRE(parsed.scan("InGameUI\n MessageColor1 = R:255 G:255 B:0\n MessageColor2 = R:255 G:255 B:255\n MessagePosition = X:-10 Y:20\n MessageFont = Arial\n MessagePointSize = 14\n MessageBold = Yes\n MessageDelayMS = 5000\nEnd\n", &error));
	PlayerMessageLog log;
	log.settings = parsed.messageUi();
	CHECK(log.settings.color1 == 0xffffff00);
	CHECK(log.settings.color2 == 0xffffffff);
	CHECK(log.settings.position.x == -10);
	CHECK(log.settings.position.y == 20);
	CHECK(log.settings.pointSize == 14);
	CHECK(log.settings.bold);
	for (unsigned i = 0; i != 7; ++i) log.add(std::u16string(1, char16_t('0' + i)), 0);
	REQUIRE(log.entries().size() == 6);
	CHECK(log.entries().front().text == u"6");
	CHECK(log.entries().back().text == u"1");
	CHECK(log.entries()[0].color == 0xffffff00);
	CHECK(log.entries()[1].color == 0xffffffff);
	log.update(99);
	CHECK(log.entries()[0].color == 0xffffff00);
	log.update(100);
	CHECK(log.entries()[0].color == 0xfeffff00);
	log.update(25500);
	CHECK(log.entries().empty());
	log.add(u"again", 25500);
	CHECK(log.entries()[0].color == 0xffffff00);
}

TEST_CASE("player message log: batching client ticks preserves fade")
{
	PlayerMessageLog batched, individual;
	batched.add(u"invalid site", 0);
	individual.add(u"invalid site", 0);
	batched.advanceClientTicks(100, 0);
	CHECK(batched.entries()[0].color == 0xffffffff);
	batched.advanceClientTicks(100, 4);
	for (unsigned i = 0; i != 4; ++i) individual.advanceClientTicks(100, 1);
	CHECK(batched.entries()[0].color == 0xfbffffff);
	CHECK(batched.entries()[0].color == individual.entries()[0].color);
}

TEST_CASE("synthetic HUD: click replaces selection, Shift adds and toggles")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *b = r.place("TestArcher", 200, 100);
	r.click(100, 100); r.selected({ a->getID() });
	r.click(200, 100); r.selected({ b->getID() });
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	CHECK(r.input->ui().isInPreferSelectionMode());
	r.click(100, 100); r.selected({ a->getID(), b->getID() });
	r.click(200, 100); r.selected({ a->getID() });
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	CHECK_FALSE(r.input->ui().isInPreferSelectionMode());
	r.click(500, 500); r.selected({});
}

TEST_CASE("synthetic HUD: drag selection filters enemies and Shift adds to it")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *b = r.place("TestArcher", 200, 100);
	Object *c = r.place("TestFighter", 400, 100);
	r.place("TestFighter", 150, 100, "Bob");
	r.drag(80, 80, 220, 120); r.selected({ a->getID(), b->getID() });
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	r.drag(420, 120, 380, 80); r.selected({ a->getID(), b->getID(), c->getID() });
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
}

TEST_CASE("synthetic HUD: double click selects visible same-template owned units")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *b = r.place("TestFighter", 200, 100);
	r.place("TestArcher", 300, 100);
	r.place("TestFighter", 400, 100, "Bob");
	r.place("TestFighter", 1200, 100);
	r.click(100, 100); // OS double clicks have an initial ordinary click; the expansion uses the current selection.
	r.click(100, 100, HudInput::Button::Left, true);
	r.selected({ a->getID(), b->getID() });
}

TEST_CASE("synthetic HUD: horde members select their horde, not the soldier")
{
	synthetichud::Rig r;
	Object *horde = r.place("TestHorde", 800, 600);
	Object *a = r.place("TestMember", 100, 100);
	Object *b = r.place("TestMember", 200, 100);
	a->friend_setContainedBy(horde); b->friend_setContainedBy(horde);
	r.click(100, 100); r.selected({ horde->getID() });
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	r.click(200, 100); r.selected({});
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	r.click(200, 100); r.selected({ horde->getID() });
}

TEST_CASE("synthetic HUD finding: horde area selection uses the horde location, not scattered members")
{
	synthetichud::Rig r;
	Object *horde = r.place("TestHorde", 800, 600);
	r.place("TestMember", 100, 100)->friend_setContainedBy(horde);
	r.place("TestMember", 200, 100)->friend_setContainedBy(horde);
	r.drag(80, 80, 220, 120);
	r.selected({});
	r.drag(780, 580, 820, 620);
	r.selected({ horde->getID() });
}

TEST_CASE("synthetic HUD: right click emits move location or enemy attack ID")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *enemy = r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.selected({ a->getID() });
	r.click(300, 300, HudInput::Button::Right);
	r.locationCommand(MSG_DO_MOVETO, 300, 300); r.commands.reset();
	r.click(400, 100, HudInput::Button::Right);
	const GameMessage &m = r.onlyCommand(MSG_DO_ATTACK_OBJECT);
	REQUIRE(m.getArgumentCount() == 1);
	CHECK(m.getArgument(0)->type == ARGUMENTDATATYPE_OBJECTID);
	CHECK(m.getArgument(0)->objectID == enemy->getID());
	r.commands.reset();
	r.selected({ a->getID() });
}

TEST_CASE("synthetic HUD: an unarmed selection or no selection cannot issue an enemy attack")
{
	synthetichud::Rig r;
	r.place("TestUnarmed", 100, 100);
	r.place("TestFighter", 400, 100, "Bob");
	SUBCASE("empty selection") { r.click(400, 100, HudInput::Button::Right); CHECK(r.commands.messages().empty()); }
	SUBCASE("unarmed selection")
	{
		r.click(100, 100); r.drain();
		r.click(400, 100, HudInput::Button::Right);
		CHECK(r.commands.messages().empty());
	}
}

TEST_CASE("synthetic HUD: attack-move targeting and current right-click cancellation")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	r.click(100, 100); r.drain();
	CommandButton attackMove;
	attackMove.m_command = GUI_COMMAND_ATTACK_MOVE;
	r.input->ui().setGUICommand(&attackMove);
	SUBCASE("world left click issues attack move")
	{
		r.click(300, 300);
		r.locationCommand(MSG_DO_ATTACKMOVETO, 300, 300);
	}
	SUBCASE("finding: world right click clears the target mode but also moves")
	{
		// SPELLRC-1's spellbook cancel hook is separate: this GUI attack-move behaviour remains on main.
		r.click(300, 300, HudInput::Button::Right);
		r.locationCommand(MSG_DO_MOVETO, 300, 300);
	}
	CHECK(r.input->ui().getGUICommand() == nullptr);
}

TEST_CASE("synthetic HUD finding: modifier-only bindings match the whole chord, not independent bits")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	Object *enemy = r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.drain();
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	// Characterisation, not a retail-fidelity assertion: adding Shift ends the Ctrl-only binding.
	CHECK_FALSE(r.input->ui().isInForceAttackMode());
	CHECK_FALSE(r.input->ui().isInPreferSelectionMode());
	r.click(400, 100, HudInput::Button::Right);
	const GameMessage &m = r.onlyCommand(MSG_DO_ATTACK_OBJECT);
	REQUIRE(m.getArgumentCount() == 1);
	CHECK(m.getArgument(0)->objectID == enemy->getID());
	r.commands.reset();
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, false);
	CHECK_FALSE(r.input->ui().isInForceAttackMode());
	CHECK(r.input->ui().isInPreferSelectionMode());
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	CHECK_FALSE(r.input->ui().isInPreferSelectionMode());
	r.click(300, 300, HudInput::Button::Right);
	r.locationCommand(MSG_DO_MOVETO, 300, 300);
}

TEST_CASE("synthetic HUD: Alt waypoint targets the ground even over an enemy")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.drain();
	r.modifier(KEY_LALT, KEY_STATE_LALT, true);
	CHECK(r.input->ui().isInWaypointMode());
	r.click(400, 100, HudInput::Button::Right);
	r.locationCommand(MSG_ADD_WAYPOINT, 400, 100);
}

TEST_CASE("synthetic HUD: stop and formation hotkeys send exactly one logic command")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	r.click(100, 100); r.drain();
	SUBCASE("stop") { r.press(KEY_S); CHECK(r.onlyCommand(MSG_DO_STOP).getArgumentCount() == 0); }
	SUBCASE("formation")
	{
		r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
		r.press(KEY_F);
		CHECK(r.onlyCommand(MSG_CREATE_FORMATION).getArgumentCount() == 0);
	}
}

TEST_CASE("synthetic HUD: Ctrl group creation, Ctrl Shift addition and unmodified recall")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *b = r.place("TestFighter", 200, 100);
	r.click(100, 100); r.selected({ a->getID() });
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
	r.press(KEY_1); r.drain();
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, false);
	r.click(200, 100); r.selected({ b->getID() });
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
	r.press(KEY_1); r.drain();
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, false);
	r.press(KEY_1); r.selected({ a->getID(), b->getID() });
}

TEST_CASE("synthetic HUD: command-label hotkeys run on release, never under Ctrl or Alt")
{
	synthetichud::Rig r;
	int presses = 0;
	HotKeyTranslator::Entry e;
	e.key = U'A'; e.availability = HotKeyTranslator::Availability::Enabled;
	e.run = [&]() { ++presses; return true; };
	r.input->hotKeyTranslator().setEntries({ e });
	r.input->key(KEY_Q, KEY_STATE_DOWN, U'a'); r.input->update();
	CHECK(presses == 0);
	r.input->key(KEY_Q, KEY_STATE_UP, U'a'); r.input->update();
	CHECK(presses == 1);
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true); r.press(KEY_Q, U'a');
	CHECK(presses == 2);
	r.modifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true); r.press(KEY_Q, U'a');
	CHECK(presses == 2);
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, false);
	r.modifier(KEY_LALT, KEY_STATE_LALT, true); r.press(KEY_Q, U'a');
	CHECK(presses == 2);
}

TEST_CASE("synthetic HUD: GUI-owned presses keep their release when the pointer leaves")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *b = r.place("TestFighter", 400, 100);
	r.click(100, 100); r.selected({ a->getID() });
	for (HudInput::Button button : { HudInput::Button::Left, HudInput::Button::Right })
	{
		CAPTURE((int)button);
		r.button(button, true, 900, 700, false, true);
		r.move(400, 100);
		r.button(button, false, 400, 100);
		CHECK(r.commands.messages().empty());
		r.selected({ a->getID() });
	}
	r.click(300, 300, HudInput::Button::Right);
	r.locationCommand(MSG_DO_MOVETO, 300, 300); r.commands.reset();
	r.click(400, 100); r.selected({ b->getID() });
}

TEST_CASE("synthetic HUD: disabled input blocks selection, orders and hotkeys")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.selected({ a->getID() });
	r.input->ui().setInputEnabled(false); r.drain();
	r.click(400, 100); r.click(300, 300, HudInput::Button::Right); r.press(KEY_S);
	CHECK(r.commands.messages().empty());
	r.selected({ a->getID() });
	r.input->ui().setInputEnabled(true); r.drain();
	r.click(300, 300, HudInput::Button::Right); r.locationCommand(MSG_DO_MOVETO, 300, 300);
}

TEST_CASE("synthetic HUD finding: the in-game chat metas are no input gate (everyone / allies ported by CHAT-1, buddies still reported as unported)")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	r.click(100, 100); r.selected({ a->getID() });
	r.bind(CMSG_META_CHAT_EVERYONE, KEY_Y, MOD_NONE);
	r.bind(CMSG_META_CHAT_ALLIES, KEY_U, MOD_NONE);
	r.bind(CMSG_META_CHAT_BUDDIES, KEY_I, MOD_NONE);
	r.press(KEY_Y); r.press(KEY_U); r.press(KEY_I);
	const auto &unported = r.input->commandTranslator().unportedMeta();
	CHECK(unported.count("CHAT_EVERYONE") == 0);
	CHECK(unported.count("CHAT_ALLIES") == 0);
	CHECK(unported.at("CHAT_BUDDIES") == 1);
	CHECK(r.commands.messages().empty());
	CHECK(r.input->ui().getInputEnabled());
	r.selected({ a->getID() });
	r.click(300, 300, HudInput::Button::Right);
	r.locationCommand(MSG_DO_MOVETO, 300, 300);
}

TEST_CASE("synthetic HUD: device aggregate modifier repair drives the actual command modes")
{
	synthetichud::Rig r;
	ModifierTracker tracker;
	auto sync = [&](bool ctrl, bool shift, bool alt) {
		for (const auto &t : tracker.sync(ctrl, shift, alt))
		{
			// GodotInGameHud::syncModifiers injects these transitions before the pointer event.
			r.input->key(t.key, tracker.state() | (t.down ? KEY_STATE_DOWN : KEY_STATE_UP));
		}
		r.modifiers = tracker.state(); r.input->update();
	};
	sync(true, false, false);
	CHECK(r.input->ui().isInForceAttackMode()); CHECK_FALSE(r.input->ui().isInPreferSelectionMode());
	sync(false, false, false);
	CHECK_FALSE(r.input->ui().isInForceAttackMode());
	sync(false, true, false);
	CHECK_FALSE(r.input->ui().isInForceAttackMode()); CHECK(r.input->ui().isInPreferSelectionMode());
	r.input->resetKeys(); r.input->update();
	CHECK_FALSE(r.input->isKeyDown(KEY_LSHIFT));
	// resetKeys clears the keyboard table, not the meta modes: the next aggregate event repairs those.
	CHECK(r.input->ui().isInPreferSelectionMode());
	sync(false, false, false);
	CHECK_FALSE(r.input->ui().isInPreferSelectionMode());
	CHECK_FALSE(r.input->ui().isInForceAttackMode());
}

TEST_CASE("synthetic HUD: camera scroll, middle rotate and aimed-power cancel need no mounted map")
{
	synthetichud::Rig r;
	CameraSettings s;
	s.loaded = true; s.defaultMinHeight = 120; s.defaultMaxHeight = 300; s.defaultPitchAngle = 37.5f;
	s.defaultScrollSpeedScalar = 1; s.horizontalScrollSpeedFactor = 0.4f; s.verticalScrollSpeedFactor = 0.5f;
	s.screenEdgeScrollSpeedFactor = 1; s.screenEdgeScrollRampTimeMs = 250;
	s.keyboardScrollSpeedFactor = 1;
	TacticalCamera camera(s);
	camera.setViewport(1024, 768);
	WorldHeightMap heights;
	heights.m_width = 128; heights.m_height = 128; heights.m_borderSize = 1;
	heights.m_data.assign(128 * 128, 0);
	heights.m_dataSize = 128 * 128;
	camera.startMap(*r.logic, heights, nullptr, { 512, 384, 0 });
	r.input->attachCamera(camera);
	auto &look = *r.input->lookAt();
	SUBCASE("edge scrolling can be disabled and stops on leaving the edge")
	{
		look.setEdgeScrollEnabled(false); r.move(1023, 400); CHECK_FALSE(look.isScrolling());
		look.setEdgeScrollEnabled(true); r.move(500, 400);
		look.setTime(1000); r.move(1023, 400);
		CHECK(look.scrollType() == LookAtTranslator::SCROLL_SCREENEDGE);
		CHECK(look.isScrolling());
		r.input->cameraFrame(1250);
		CHECK(look.offsetX() == 40.0f);
		r.move(500, 400); CHECK_FALSE(look.isScrolling());
	}
	SUBCASE("middle drag rotates and focus cancellation stops it")
	{
		r.move(500, 400); r.button(HudInput::Button::Middle, true, 500, 400);
		CHECK(look.isRotating()); r.move(600, 400);
		// tolerance: the camera normalises the float angle through pi, with roundoff.
		CHECK(camera.getAngle() == doctest::Approx(0.5f).epsilon(1e-5));
		r.input->cancelHeld(); CHECK_FALSE(look.isRotating());
		r.move(650, 400);
		// tolerance: the same normalised angle remains after a cancelled drag.
		CHECK(camera.getAngle() == doctest::Approx(0.5f).epsilon(1e-5));
	}
	SUBCASE("SPELLRC-1 aimed-power hook: right drag pans, click cancels, next click moves")
	{
		Object *unit = r.place("TestFighter", 100, 100);
		r.click(100, 100); r.selected({ unit->getID() });
		// RW 0x83D7AD / 0x83AC4A (tier A): click-vs-drag gating and RMB pan.
		// SPELLRC-1's InGameHud hook is a port seam, not a claim of retail spellbook parity.
		bool aimed = true;
		int cancels = 0;
		r.input->guiCommandTranslator().setHudTargetCancel([&]() {
			if (!aimed || !r.input->ui().getInputEnabled()) return false;
			aimed = false; ++cancels; return true;
		});
		r.move(300, 300);
		r.button(HudInput::Button::Right, true, 300, 300);
		CHECK(aimed);
		r.move(500, 300);
		CHECK(look.isScrolling());
		CHECK(look.scrollType() == LookAtTranslator::SCROLL_RMB);
		r.button(HudInput::Button::Right, false, 500, 300, false, false, 200);
		CHECK_FALSE(look.isScrolling());
		CHECK(aimed); CHECK(cancels == 0);
		CHECK(r.commands.messages().empty());
		r.input->ui().setInputEnabled(false); r.drain();
		r.click(500, 300, HudInput::Button::Right);
		CHECK(aimed); CHECK(cancels == 0);
		CHECK(r.commands.messages().empty());
		r.input->ui().setInputEnabled(true); r.drain();
		r.click(500, 300, HudInput::Button::Right);
		CHECK_FALSE(aimed); CHECK(cancels == 1);
		CHECK(r.commands.messages().empty());
		r.selected({ unit->getID() });
		r.click(500, 300, HudInput::Button::Right);
		r.locationCommand(MSG_DO_MOVETO, 500, 300);
		r.commands.reset();
	}
	CHECK(r.commands.messages().empty());
}

TEST_CASE("synthetic HUD: an open nonmodal synthetic Apt panel owns only its hit mesh")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	Object *enemy = r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.selected({ a->getID() });
	// The real movies cannot boot without the install. Exercise their Apt hit-test / HudInput ownership seam with a generated movie.
	apttest::TestMovie movie;
	movie.addCharacter(0);
	const auto shape = movie.addCharacter(movie.addShape(0, 0, 200, 250, 1));
	const auto button = movie.addCharacter(movie.addButton({ { 1, shape }, { 8, shape } }, {}));
	auto placement = apttest::placeChar(button, 1, "panel");
	placement.flags |= APT_PLACE_HASMATRIX;
	placement.matrix[0] = 20; placement.matrix[3] = 25;
	placement.translation[0] = 800; placement.translation[1] = 500;
	movie.setRootFrames({ { movie.addPlaceItem(placement) } });
	apttest::PlayerFx fx;
	REQUIRE(fx.load(0, "synthetic_panel", movie)); fx.step();
	auto over = [&](int x, int y) { return fx.apt->input().hitTestButtons((float)x, (float)y) != nullptr; };
	REQUIRE(over(900, 600)); REQUIRE_FALSE(over(300, 300));
	for (HudInput::Button b : { HudInput::Button::Left, HudInput::Button::Right })
	{
		CAPTURE((int)b);
		r.button(b, true, 900, 600, false, over(900, 600));
		r.move(300, 300);
		r.button(b, false, 300, 300, false, over(300, 300));
		CHECK(r.commands.messages().empty()); r.selected({ a->getID() });
	}
	// Powers/build submenus that are merely open must not be represented as a fullscreen modal gate.
	r.button(HudInput::Button::Right, true, 300, 300, false, over(300, 300));
	r.button(HudInput::Button::Right, false, 300, 300, false, over(300, 300));
	r.locationCommand(MSG_DO_MOVETO, 300, 300); r.commands.reset();
	r.button(HudInput::Button::Right, true, 400, 100, false, over(400, 100));
	r.button(HudInput::Button::Right, false, 400, 100, false, over(400, 100));
	const GameMessage &attack = r.onlyCommand(MSG_DO_ATTACK_OBJECT);
	REQUIRE(attack.getArgumentCount() == 1);
	REQUIRE(attack.getArgument(0)->type == ARGUMENTDATATYPE_OBJECTID);
	CHECK(attack.getArgument(0)->objectID == enemy->getID());
	r.commands.reset();
	fx.root()->visible = false;
	CHECK_FALSE(over(900, 600));
	r.button(HudInput::Button::Right, true, 900, 600, false, over(900, 600));
	r.button(HudInput::Button::Right, false, 900, 600, false, over(900, 600));
	r.locationCommand(MSG_DO_MOVETO, 900, 600);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("synthetic HUD: Ctrl force attack targets an object or ground in the current port")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	Object *enemy = r.place("TestFighter", 400, 100, "Bob");
	r.click(100, 100); r.drain();
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
	CHECK(r.input->ui().isInForceAttackMode());
	SUBCASE("object")
	{
		r.click(400, 100, HudInput::Button::Right);
		const GameMessage &m = r.onlyCommand(MSG_DO_FORCE_ATTACK_OBJECT);
		REQUIRE(m.getArgumentCount() == 2);
		CHECK(m.getArgument(0)->type == ARGUMENTDATATYPE_OBJECTID);
		CHECK(m.getArgument(0)->objectID == enemy->getID());
		CHECK(m.getArgument(1)->type == ARGUMENTDATATYPE_LOCATION);
		CHECK(m.getArgument(1)->location.x == 400.0f);
		CHECK(m.getArgument(1)->location.y == 100.0f);
	}
	SUBCASE("ground")
	{
		r.click(300, 300, HudInput::Button::Right);
		r.locationCommand(MSG_DO_FORCE_ATTACK_GROUND, 300, 300);
	}
}

TEST_CASE("synthetic HUD: a right-only drag without formation data is not a formation order")
{
	synthetichud::Rig r;
	Object *a = r.place("TestFighter", 100, 100);
	r.click(100, 100); r.selected({ a->getID() });
	r.drag(300, 300, 500, 350, HudInput::Button::Right, 200);
	CHECK(r.commands.messages().empty());
	r.selected({ a->getID() });
	CHECK(r.input->commandTranslator().clickOutcomes().at("right-not-a-click") == 1);
	r.click(500, 350, HudInput::Button::Right);
	r.locationCommand(MSG_DO_MOVETO, 500, 350);
}

TEST_CASE("synthetic HUD: Ctrl force-attack mode suppresses drag selection with the synthetic bindings")
{
	synthetichud::Rig r;
	r.place("TestFighter", 100, 100);
	r.place("TestFighter", 200, 100);
	r.modifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
	r.drag(80, 80, 220, 120);
	r.selected({});
	CHECK(r.commands.messages().empty());
}

TEST_CASE("synthetic HUD: two-button hold previews formation without camera pan and either release commits once")
{
	// RW 0x83DDE1 / 0x83DED2 / 0x83E3AD / 0x83E18A (tier A): either-button
	// chord within 500ms, pointer preview, and either release with click suppression.
	combattest::CombatWorld world;
	synthetichud::PixelView view;
	MetaMap meta;
	CommandList commands;
	FormationAssistant formations;
	std::string error;
	REQUIRE_MESSAGE(formations.scan(
		"FormationAssistant\n UnitDefinition Unit\n ObjectFilter = ALL\n End\n"
		" FormationTemplate Line\n Rows\n Row = Unit\n End\n End\n"
		" FormationSelection Line\n MaxDragLength = 400\n MaxUnitsSelected = 10\n End\nEnd\n", &error), error);
	Object *unit = world.unit("Slasher", 'A', 100, 100);
	world.logic->players().setLocalPlayer(unit->getControllingPlayer());
	CameraSettings settings;
	settings.loaded = true;
	TacticalCamera camera(settings);
	camera.setViewport(1024, 768);
	HudInput input(*world.logic, world.ai.get(), view, commands, MouseSettings{ 15, 5, 150 }, meta);
	input.context().formations = &formations;
	input.commandTranslator().setUseAlternateMouse(true);
	input.attachCamera(camera);
	input.lookAt()->setEdgeScrollEnabled(false);
	int time = 1000;
	auto button = [&](HudInput::Button b, bool down, int x, int y) {
		input.mouseButton(b, down, x, y, 0, time += 40);
		input.update();
	};
	button(HudInput::Button::Left, true, 100, 100);
	button(HudInput::Button::Left, false, 100, 100);
	REQUIRE(input.ui().selected() == std::vector<ObjectID>{ unit->getID() });
	commands.reset();
	bool rightFirst = true, rightRelease = true, attackMove = false;
	SUBCASE("right then left, right release") {}
	SUBCASE("right then left, left release") { rightRelease = false; }
	SUBCASE("left then right, right release") { rightFirst = false; }
	SUBCASE("left then right, left release") { rightFirst = false; rightRelease = false; }
	SUBCASE("attack-move button survives the chord and clears on commit") { attackMove = true; }
	CommandButton attack;
	attack.m_command = GUI_COMMAND_ATTACK_MOVE;
	if (attackMove) input.ui().setGUICommand(&attack);
	time += 300;
	button(rightFirst ? HudInput::Button::Right : HudInput::Button::Left, true, 300, 300);
	CHECK_FALSE(input.ui().formationActive);
	CHECK_FALSE(input.lookAt()->isScrolling());
	CHECK(commands.messages().empty());
	button(rightFirst ? HudInput::Button::Left : HudInput::Button::Right, true, 300, 300);
	REQUIRE(input.ui().formationActive);
	input.mouseMove(400, 300, 0); input.update();
	CHECK_FALSE(input.lookAt()->isScrolling());
	CHECK(commands.messages().empty());
	CHECK(input.ui().selected() == std::vector<ObjectID>{ unit->getID() });
	REQUIRE(input.ui().formationSlots.size() == 1);
	CHECK(input.ui().formationSlots[0].object == unit->getID());
	CHECK(input.ui().formationSlots[0].position.x == 300.0f);
	CHECK(input.ui().formationSlots[0].position.y == 300.0f);
	CHECK(input.ui().formationDragLength == 100.0f);
	button(rightRelease ? HudInput::Button::Right : HudInput::Button::Left, false, 400, 300);
	CHECK_FALSE(input.ui().formationActive);
	REQUIRE(commands.messages().size() == 1);
	button(rightRelease ? HudInput::Button::Left : HudInput::Button::Right, false, 400, 300);
	REQUIRE(commands.messages().size() == 1);
	const GameMessage &order = commands.messages().front();
	CHECK(order.getType() == MSG_DO_MOVETO_FORMATION);
	CHECK(order.getPlayerIndex() == unit->getControllingPlayer()->getPlayerIndex());
	REQUIRE(order.getArgumentCount() == 4);
	REQUIRE(order.getArgument(0)->type == ARGUMENTDATATYPE_LOCATION);
	CHECK(order.getArgument(0)->location.x == 300.0f);
	CHECK(order.getArgument(0)->location.y == 300.0f);
	CHECK(order.getArgument(0)->location.z == 0.0f);
	REQUIRE(order.getArgument(1)->type == ARGUMENTDATATYPE_REAL);
	CHECK(order.getArgument(1)->real == 0.0f);
	REQUIRE(order.getArgument(2)->type == ARGUMENTDATATYPE_INTEGER);
	CHECK(order.getArgument(2)->integer == 0);
	REQUIRE(order.getArgument(3)->type == ARGUMENTDATATYPE_BOOLEAN);
	CHECK(order.getArgument(3)->boolean == attackMove);
	CHECK(input.ui().getGUICommand() == nullptr);
	CHECK(input.ui().formationExpires == input.ui().clientFrame() + 45);
	CHECK(input.ui().selected() == std::vector<ObjectID>{ unit->getID() });
	commands.reset();
	time += 300;
	button(HudInput::Button::Right, true, 500, 300);
	button(HudInput::Button::Right, false, 500, 300);
	REQUIRE(commands.messages().size() == 1);
	CHECK(commands.messages().front().getType() == MSG_DO_MOVETO);
}
