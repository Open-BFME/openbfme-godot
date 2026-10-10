// OpenBFME retail tests of the HUD input (lane HUD-1): synthesised mouse and keyboard events through HudInput into the lockstep command list and the logic.
// They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

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
	CHECK(logHas(*r.input, "MSG_AREA_SELECTION"));
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
	REQUIRE(stops.size() == 13);
	// the control bar, radar and Palantir report S-289 .. S-294 (their own test: test_hud_palantir.cpp)
	for (size_t i = 0; i < 9; ++i)
	{
		CHECK(stops[i].compare(0, 7, "[S-" + std::to_string(280 + (int)i) + "]") == 0);
	}
	CHECK(stops[9].compare(0, 8, "[S-1770]") == 0); // lane QA2-FIX: the wall line build (PlaceEventTranslator::stopLines)
	CHECK(stops[10].compare(0, 8, "[S-1920]") == 0); // lane PLAY-1: the move hint
	CHECK(stops[11].compare(0, 8, "[S-1954]") == 0); // lane HUD-5: RotWK's pick types
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
