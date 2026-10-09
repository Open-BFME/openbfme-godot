// OpenBFME unit tests: Apt input - hit meshes, button state transitions, clip mouse events, key actions, listeners and focus
// (spec menus-apt.md step A3).  Expected behaviour is the BFME2 1.06 AptInput.cpp (0x00AFA020..0x00AFB910, cited in
// AptInput.h); movies are assembled byte by byte, every observation is an fscommand or an instance property.
// GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"

using namespace apttest;

namespace
{

// A root with one button `b` at (100, 100): Up / Over / Down shapes and a 10 x 10 Hit shape, one action per transition.
struct ButtonMovie
{
	TestMovie m;
	std::uint32_t buttonId = 0;
	std::map<std::string, std::uint32_t> codes;

	ButtonMovie()
	{
		std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
		m.addCharacter(0);
		std::uint32_t shapeId = m.addCharacter(shape);
		struct T
		{
			const char *name;
			std::uint8_t mask;
		};
		const T transitions[] = { { "rollOver", 0x01 }, { "rollOut", 0x02 }, { "press", 0x04 }, { "release", 0x08 }, { "dragOut", 0x10 }, { "dragOver", 0x20 },
			{ "releaseOutside", 0x40 } };
		std::vector<TestMovie::ButtonAction> actions;
		for (const T &t : transitions)
		{
			codes[t.name] = program(m, [&](Asm &a) {
				fscmd(a, t.name);
				// the program's `this` is the button's PARENT: record its name
				a.pushString("parentName").getStringVar("_name").op(APT_OP_SETVARIABLE);
			});
			actions.push_back({ t.mask, 0, codes[t.name] });
		}
		std::uint32_t button = m.addButton({ { 1, shapeId }, { 2, shapeId }, { 4, shapeId }, { 8, shapeId } }, actions);
		buttonId = m.addCharacter(button);
		TestMovie::Place p = placeChar(buttonId, 1, "b");
		p.flags |= APT_PLACE_HASMATRIX;
		p.translation[0] = 100;
		p.translation[1] = 100;
		m.setRootFrames({ { m.addPlaceItem(p) } });
	}
};

} // namespace

TEST_CASE("input: hovering the hit mesh moves the button through Over and runs the rollover action inside the update")
{
	// 0x00AFA420 finds the button, 0x00AFAA20 sets state Over (2) and 0x00AFA100 queues the actions and runs the pool at once
	// (0x00AFA314), so the fscommand fires during the update that drained the mouse event
	ButtonMovie bm;
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	AptButtonInst *b = fx.at("b")->asButton();
	REQUIRE(b);
	CHECK(b->state() == AptButtonInst::State::Up); // set by the new-instance flush (0x00AE43FA)
	fx.apt->input().postMouseMove(105, 105);
	CHECK(fx.commandsText().empty()); // queued, not processed yet
	fx.step();
	CHECK(fx.commandsText() == "rollOver");
	CHECK(b->state() == AptButtonInst::State::Over);
	CHECK(fx.apt->input().currentButton() == b);
	// the action's `this` is the button's parent: the level root, named _level0
	AptValue who;
	REQUIRE(fx.root()->getMember("parentName", who));
	CHECK(who.toString() == "_level0");
	// leaving: back to Up, rollout
	fx.apt->input().postMouseMove(150, 150);
	fx.step();
	CHECK(fx.commandsText() == "rollOut");
	CHECK(b->state() == AptButtonInst::State::Up);
	CHECK(fx.apt->input().currentButton() == nullptr);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: press and release inside the button")
{
	// 0x00AFB03E: down -> state Down (4), transition OverUpToOverDown (4); up with the button Down -> state Over, transition
	// OverDownToOverUp (8, the release)
	ButtonMovie bm;
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	AptButtonInst *b = fx.at("b")->asButton();
	fx.apt->input().postMouseMove(105, 105);
	fx.step();
	fx.commands();
	fx.apt->input().postMouseButton(true);
	fx.step();
	CHECK(fx.commandsText() == "press");
	CHECK(b->state() == AptButtonInst::State::Down);
	CHECK(fx.apt->input().mouseDown());
	fx.apt->input().postMouseButton(false);
	fx.step();
	CHECK(fx.commandsText() == "release");
	CHECK(b->state() == AptButtonInst::State::Over);
	CHECK_FALSE(fx.apt->input().mouseDown());
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: dragging out of a pressed button and releasing outside")
{
	// 0x00AFAA83: moving away while pressed sets Over and runs DragOut (0x10); 0x00AFB09E: releasing with the button not Down
	// sets Up and runs OutDownToIdle (0x40, release outside), then the current button is re-armed as Over (rollover, 0x00AFB0DD)
	ButtonMovie bm;
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	AptButtonInst *b = fx.at("b")->asButton();
	fx.apt->input().postMouseMove(105, 105);
	fx.apt->input().postMouseButton(true);
	fx.step();
	fx.commands();
	fx.apt->input().postMouseMove(300, 300);
	fx.step();
	CHECK(fx.commandsText() == "dragOut");
	CHECK(b->state() == AptButtonInst::State::Over);
	fx.apt->input().postMouseMove(106, 106);
	fx.step();
	CHECK(fx.commandsText() == "dragOver"); // OutDownToOverDown
	CHECK(b->state() == AptButtonInst::State::Down);
	fx.apt->input().postMouseMove(300, 300);
	fx.step();
	fx.commands();
	fx.apt->input().postMouseButton(false);
	fx.step();
	CHECK(fx.commandsText() == "releaseOutside rollOver");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: a member handler the script assigned runs before the button's own actions")
{
	// 0x00AFA2A6 pushes the onRelease member to the FRONT (0x00AE3810) after the actions went to the back
	ButtonMovie bm;
	// root frame 0 additionally assigns b.onRelease
	std::uint32_t assign = program(bm.m, [&](Asm &a) {
		a.getStringVar("b").pushString("onRelease");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "memberRelease"); });
		a.op(APT_OP_SETMEMBER);
	});
	// rebuild the root frames with the extra action
	TestMovie::Place p = placeChar(bm.buttonId, 1, "b");
	p.flags |= APT_PLACE_HASMATRIX;
	p.translation[0] = 100;
	p.translation[1] = 100;
	bm.m.setRootFrames({ { bm.m.addPlaceItem(p), bm.m.addActionItem(assign) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	fx.apt->input().postMouseMove(105, 105);
	fx.apt->input().postMouseButton(true);
	fx.step();
	fx.commands();
	fx.apt->input().postMouseButton(false);
	fx.step();
	CHECK(fx.commandsText() == "memberRelease release");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: a disabled button is not hit; a hidden button is not hit")
{
	ButtonMovie bm;
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	AptButtonInst *b = fx.at("b")->asButton();
	b->enabled = false;
	fx.apt->input().postMouseMove(105, 105);
	fx.step();
	CHECK(fx.commandsText().empty());
	CHECK(fx.apt->input().currentButton() == nullptr);
	b->enabled = true;
	b->visible = false;
	fx.apt->input().postMouseMove(106, 106);
	fx.step();
	CHECK(fx.apt->input().currentButton() == nullptr);
	b->visible = true;
	fx.apt->input().postMouseMove(107, 107);
	fx.step();
	CHECK(fx.apt->input().currentButton() == b);
}

TEST_CASE("input: the hit test follows the placement matrix of the button")
{
	// 0x00AFA420 multiplies the button's matrix with the Hit record's: scale 2 doubles the hit area
	ButtonMovie bm;
	TestMovie::Place p = placeChar(bm.buttonId, 1, "b");
	p.flags |= APT_PLACE_HASMATRIX;
	p.matrix[0] = 2;
	p.matrix[3] = 2;
	p.translation[0] = 100;
	p.translation[1] = 100;
	bm.m.setRootFrames({ { bm.m.addPlaceItem(p) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	CHECK(fx.apt->input().hitTestButtons(115, 115) == fx.at("b")); // inside the doubled square (100..120)
	CHECK(fx.apt->input().hitTestButtons(121, 105) == nullptr);
	CHECK(fx.apt->input().hitTestButtons(99, 105) == nullptr);
}

TEST_CASE("input: a key-only button action is queued with the button's parent as target and runs on the next update")
{
	// 0x00AFB429..0x00AFB583: the key code sits in bits 9..15 of the condition word; the program goes to the back of the pool
	// (0x00AE4B80) and is not run by the input call
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	std::uint32_t code = program(m, [&](Asm &a) { fscmd(a, "enter"); });
	std::uint32_t button = m.addButton({ { 1, shapeId }, { 8, shapeId } }, { { 0, (std::uint16_t)(13 << 1), code } });
	std::uint32_t buttonId = m.addCharacter(button);
	m.setRootFrames({ { m.addPlaceItem(placeChar(buttonId, 1, "k")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	fx.apt->input().postKey(13, true);
	fx.step();
	CHECK(fx.commandsText().empty());
	fx.step();
	CHECK(fx.commandsText() == "enter");
	CHECK(fx.apt->input().isKeyDown(13));
	fx.apt->input().postKey(13, false);
	fx.step();
	CHECK_FALSE(fx.apt->input().isKeyDown(13));
	CHECK(fx.errorsText().empty());
}

namespace
{
// A sprite `c` at (50, 50) holding a 10 x 10 shape, with clip actions for the given events.
struct ClipMovie
{
	TestMovie m;
	ClipMovie(const std::vector<std::pair<std::uint32_t, std::string>> &events)
	{
		std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
		m.addCharacter(0);
		std::uint32_t shapeId = m.addCharacter(shape);
		std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1)) } });
		std::uint32_t spriteId = m.addCharacter(sprite);
		TestMovie::Place p = placeChar(spriteId, 1, "c");
		p.flags |= APT_PLACE_HASMATRIX | APT_PLACE_HASCLIPACTION;
		p.translation[0] = 50;
		p.translation[1] = 50;
		for (const auto &e : events)
		{
			std::string name = e.second;
			p.events.push_back({ e.first, 0, program(m, [&](Asm &a) { fscmd(a, name); }) });
		}
		m.setRootFrames({ { m.addPlaceItem(p) } });
	}
};
} // namespace

TEST_CASE("input: clip mouse events - rollover, press, release, rollout on the topmost handler under the cursor")
{
	// 0x00AFA7E0 / 0x00AFB120: the events go through AptCIH::fire, so the program is queued at the back and runs on the NEXT
	// update's pool; the bounding rectangle (0x00AF9FC0) decides the hit
	ClipMovie cm({ { APT_CLIP_ROLLOVER, "rollOver" }, { APT_CLIP_ROLLOUT, "rollOut" }, { APT_CLIP_PRESS, "press" }, { APT_CLIP_RELEASE, "release" },
		{ APT_CLIP_RELEASEOUTSIDE, "releaseOutside" } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", cm.m));
	fx.step();
	fx.commands();
	fx.apt->input().postMouseMove(55, 55);
	fx.step(2);
	CHECK(fx.commandsText() == "rollOver");
	CHECK(fx.apt->input().hoverClip() == fx.at("c"));
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	CHECK(fx.commandsText() == "press");
	CHECK(fx.apt->input().pressedClip() == fx.at("c"));
	fx.apt->input().postMouseButton(false);
	fx.step(2);
	CHECK(fx.commandsText() == "release");
	CHECK(fx.apt->input().pressedClip() == nullptr);
	fx.apt->input().postMouseMove(200, 200);
	fx.step(2);
	CHECK(fx.commandsText() == "rollOut");
	// press inside, release outside
	fx.apt->input().postMouseMove(55, 55);
	fx.step(2);
	fx.commands();
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	fx.commands();
	fx.apt->input().postMouseMove(200, 200);
	fx.step();
	fx.apt->input().postMouseButton(false);
	fx.step(2);
	CHECK(fx.commandsText() == "releaseOutside");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: MouseDown, MouseUp and MouseMove reach every clip that handles them, hit or not")
{
	// 0x00AFB27F / 0x00AFB2BF / 0x00AFB2E4: fire(0x10 / 0x20 / 0x8) on all instances of the input set
	ClipMovie cm({ { APT_CLIP_MOUSEDOWN, "down" }, { APT_CLIP_MOUSEUP, "up" }, { APT_CLIP_MOUSEMOVE, "move" } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", cm.m));
	fx.step();
	fx.commands();
	fx.apt->input().postMouseMove(500, 500);
	fx.step(2);
	CHECK(fx.commandsText() == "move");
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	CHECK(fx.commandsText() == "down");
	fx.apt->input().postMouseButton(false);
	fx.step(2);
	CHECK(fx.commandsText() == "up");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: Mouse and Key listener objects get their on* calls at the back of the pool")
{
	// 0x00AFB5B0 -> 0x00AFAB40: for a listener without a CIH the member function is queued (0x00AE3740)
	TestMovie m;
	std::uint32_t setup = program(m, [&](Asm &a) {
		a.pushString("l").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_DEFINELOCAL);
		a.getStringVar("l").pushString("onMouseDown");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "listenerDown"); });
		a.op(APT_OP_SETMEMBER);
		a.getStringVar("l").pushString("onKeyDown");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "listenerKey"); });
		a.op(APT_OP_SETMEMBER);
		a.getStringVar("l").pushByte(1).getStringVar("Mouse").pushString("addListener").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("l").pushByte(1).getStringVar("Key").pushString("addListener").op(APT_OP_EA_CALLMETHODPOP);
	});
	m.setRootFrames({ { m.addActionItem(setup) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	fx.commands();
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	CHECK(fx.commandsText() == "listenerDown");
	fx.apt->input().postKey(65, true);
	fx.step(2);
	CHECK(fx.commandsText() == "listenerKey");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("input: the focus is the current button: it follows the pointer and the press")
{
	// gApt+0x6C (0x00AFAB02) is the current button; there is no separate focus object in the EA input
	ButtonMovie bm;
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	CHECK(fx.apt->input().focus() == nullptr);
	fx.apt->input().postMouseMove(105, 105);
	fx.step();
	CHECK(fx.apt->input().focus() == fx.at("b"));
	fx.apt->input().postMouseButton(true);
	fx.step();
	CHECK(fx.apt->input().focus() == fx.at("b"));
	fx.apt->input().postMouseButton(false);
	fx.apt->input().postMouseMove(400, 400);
	fx.step();
	CHECK(fx.apt->input().focus() == nullptr);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-107: a mouse event several clips handle is reported, the delivery order is the tree's")
{
	// 0x00AFB160 walks the input set in registration order; the port walks the display tree (documented in AptInput.h)
	TestMovie m;
	std::uint32_t c1 = program(m, [&](Asm &a) { fscmd(a, "first"); });
	std::uint32_t c2 = program(m, [&](Asm &a) { fscmd(a, "second"); });
	std::uint32_t sprite = m.addSprite({ {} });
	m.addCharacter(0);
	std::uint32_t id = m.addCharacter(sprite);
	TestMovie::Place p1 = placeChar(id, 1, "one");
	p1.flags |= APT_PLACE_HASCLIPACTION;
	p1.events = { { APT_CLIP_MOUSEDOWN, 0, c1 } };
	TestMovie::Place p2 = placeChar(id, 2, "two");
	p2.flags |= APT_PLACE_HASCLIPACTION;
	p2.events = { { APT_CLIP_MOUSEDOWN, 0, c2 } };
	m.setRootFrames({ { m.addPlaceItem(p1), m.addPlaceItem(p2) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	CHECK(fx.apt->noteCount("input-delivery-order") == 0);
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	CHECK(fx.commandsText() == "first second");
	CHECK(fx.apt->noteCount("input-delivery-order") == 1);
}

TEST_CASE("stops S-108: directional focus members of a button are reported when a key arrives; they do not move the focus")
{
	// 0x00AFAD29..0x00AFAD35 resolve the `_up/_down/_left/_right` members of the current button; the port does not navigate
	ButtonMovie bm;
	std::uint32_t assign = program(bm.m, [&](Asm &a) {
		a.getStringVar("b").pushString("_right").pushString("other").op(APT_OP_SETMEMBER);
	});
	TestMovie::Place p = placeChar(bm.buttonId, 1, "b");
	p.flags |= APT_PLACE_HASMATRIX;
	p.translation[0] = 100;
	p.translation[1] = 100;
	bm.m.setRootFrames({ { bm.m.addPlaceItem(p), bm.m.addActionItem(assign) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", bm.m));
	fx.step();
	fx.apt->input().postMouseMove(105, 105);
	fx.step();
	CHECK(fx.apt->noteCount("focus-navigation-member-ignored") == 0);
	fx.apt->input().postKey(39, true);
	fx.step();
	CHECK(fx.apt->noteCount("focus-navigation-member-ignored") == 1);
	CHECK(fx.apt->input().focus() == fx.at("b"));
}
