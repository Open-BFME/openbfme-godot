// OpenBFME unit tests: the Apt player core - display list, timeline, action pool, clip events, init actions, timers,
// loadMovie and the MovieClip natives (spec menus-apt.md step A2).
//
// Expected orders come from the BFME2 1.06 binary (addresses in the comments and in AptCharacterInst.h / Apt.h), not from
// what this engine printed.  Movies are assembled byte by byte (AptTestUtil.h); every observation is an fscommand.
// GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"

using namespace apttest;

namespace
{

// root (frames: 2) with a child clip `c` placed on frame 0.  c has `cFrames` frames, each with an fscommand "c<i>".
struct TwoLevelMovie
{
	TestMovie m;
	std::uint32_t cId = 0;
	std::uint32_t r0 = 0, r1 = 0;
	std::vector<std::uint32_t> cActions;

	explicit TwoLevelMovie(int cFrames, const std::function<void(TestMovie &, TestMovie::Place &)> &placeHook = nullptr,
		const std::function<std::uint32_t(TestMovie &, int)> &frameAction = nullptr)
	{
		std::vector<std::vector<std::uint32_t>> frames;
		for (int i = 0; i < cFrames; ++i)
		{
			std::uint32_t code = frameAction ? frameAction(m, i) : program(m, [&](Asm &a) { fscmd(a, "c" + std::to_string(i)); });
			cActions.push_back(code);
			frames.push_back({ m.addActionItem(code) });
		}
		std::uint32_t sprite = m.addSprite(frames);
		m.addCharacter(0); // slot 0 is the movie
		cId = m.addCharacter(sprite);
		TestMovie::Place p = placeChar(cId, 1, "c");
		if (placeHook)
		{
			placeHook(m, p);
		}
		r0 = program(m, [&](Asm &a) { fscmd(a, "r0"); });
		r1 = program(m, [&](Asm &a) { fscmd(a, "r1"); });
		m.setRootFrames({ { m.addPlaceItem(p), m.addActionItem(r0) }, { m.addActionItem(r1) } });
	}
};

} // namespace

TEST_CASE("player: an update steps whole frames of the lowest level's ms/frame and keeps the carry")
{
	// AptUpdate 0x00ACD7A0: the time accumulates until it reaches the movie's ms/frame (+0x24); the remainder stays
	TestMovie m;
	m.setRootFrames({ {}, {}, {} });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->msPerFrame() == 33);
	CHECK(fx.apt->update(32) == 0);
	CHECK(fx.apt->update(1) == 1);
	CHECK(fx.apt->update(100) == 3); // 99 ms of frames, 1 ms carried
	CHECK(fx.apt->update(31) == 0);
	CHECK(fx.apt->update(1) == 1);
	CHECK(fx.apt->frameCount() == 5);
	CHECK(fx.apt->clockMs() == 5 * 33);
}

TEST_CASE("player: frame actions run after the whole display sweep, a clip's frame 0 after its parent's actions")
{
	// 0x00ACD7A0 order: advance every level (0x00AF7A30, parent then children), then run the action pool (0x00AE6540) in
	// queue order.  The load tracker runs the pool right after a load (0x00AD1C58).
	TwoLevelMovie t(2);
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", t.m));
	// frame 0 queued r0 (root) then c0 (child, advanced right after its parent: 0x00AE2D60 ends in 0x00AF7A30); both ran
	CHECK(fx.commandsText() == "r0 c0");
	fx.step();
	CHECK(fx.commandsText() == "r1 c1");
	fx.step();
	// both timelines wrapped to frame 0 through gotoFrame(0): the frame actions of frame 0 again, root first
	CHECK(fx.commandsText() == "r0 c0");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: siblings placed on one frame run their frame 0 actions in depth order; a call into a later sibling finds no function (S-380)")
{
	// The retail order (RotWK game.dat; BFME2 1.06 counterparts in brackets):
	//   advance 0x00AF7030 [0x00AE2D60] of the parent runs its frame controls 0x00B23540 [0x00B0F370] (both siblings are placed and
	//   join the new-instance list), queues ITS frame actions 0x00B23850 [0x00B0F680] at the back of the pool, then tail-jumps into
	//   the display-list sweep 0x00B0BC30 [0x00AF7A30], which advances every child from the head of the list: the first sibling's
	//   frame 0 queues both of its actions, then the second sibling's.  The pool run 0x00AFA850 [0x00AE6540] executes them in queue
	//   order (the new-instance flush 0x00AF86A0 [0x00AE4390] finds both already advanced: frame != -1).
	// So the lower sibling's second action runs before the higher sibling's first one has defined anything: its call into the higher
	// sibling finds no function and calls nothing (CallMethod 0x00B1C220 [0x00B08070]); the higher sibling's call back succeeds.
	// InGameSideCommandBar.apt (Palantir's side command bar) does exactly this: Button<i> frame 0 action 2 calls
	// UpdateFrameState of Button<i+1>, defined by Button<i+1>'s frame 0 action 1.
	TestMovie m;
	auto spriteOf = [&](const char *defines, const char *fsName, const char *other, const char *calls) {
		std::uint32_t define = program(m, [&](Asm &a) {
			a.defineFunction(defines, {}, [&](Asm &b) { fscmd(b, fsName); });
		});
		std::uint32_t call = program(m, [&](Asm &a) {
			a.pushByte(0).getStringVar("_parent").pushString(other).op(APT_OP_GETMEMBER).pushString(calls).op(APT_OP_EA_CALLMETHODPOP);
		});
		return m.addSprite({ { m.addActionItem(define), m.addActionItem(call) } });
	};
	std::uint32_t spriteA = spriteOf("PingA", "a.PingA", "b", "PingB");
	std::uint32_t spriteB = spriteOf("PingB", "b.PingB", "a", "PingA");
	m.addCharacter(0);
	std::uint32_t idA = m.addCharacter(spriteA);
	std::uint32_t idB = m.addCharacter(spriteB);
	std::uint32_t rootAction = program(m, [&](Asm &a) { fscmd(a, "root"); });
	std::uint32_t laterCalls = program(m, [&](Asm &a) {
		callMethod(a, "a", "PingA");
		callMethod(a, "b", "PingB");
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(idA, 1, "a")), m.addPlaceItem(placeChar(idB, 2, "b")), m.addActionItem(rootAction) }, { m.addActionItem(laterCalls) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "Order", m));
	// root first, then a (nothing: b.PingB is not defined yet), then b (a.PingA is defined)
	CHECK(fx.commandsText() == "root a.PingA");
	CHECK(fx.errorsText().empty()); // a call without a function is not a script error: retail skips it silently
	REQUIRE(fx.apt->noteCount("call-without-function") == 1);
	for (const AptNote &n : fx.apt->notes())
	{
		if (n.kind == "call-without-function")
		{
			CHECK(n.detail == "method 'PingB' is not a function on a value of type movieclip [in _level0.a]");
		}
	}
	fx.step();
	CHECK(fx.commandsText() == "a.PingA b.PingB"); // once both frame 0 programs ran, either call works
	CHECK(fx.apt->noteCount("call-without-function") == 1);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-380: a call of a native the port does not implement stays a script error; other calls without a function are notes")
{
	CHECK(AptActionInterpreter::isUnportedRetailNative("createTextField"));
	CHECK(AptActionInterpreter::isUnportedRetailNative("loadVariables"));
	CHECK(AptActionInterpreter::isUnportedRetailNative("registerClass")); // Object.registerClass: RotWK getter 0x00B22160, body 0x00AF1EB0, not ported
	CHECK_FALSE(AptActionInterpreter::isUnportedRetailNative("UpdateFrameState"));
	TestMovie m;
	std::uint32_t code = program(m, [&](Asm &a) {
		callMethod(a, "this", "createTextField");
		callMethod(a, "this", "UpdateFrameState");
		callMethod(a, "nothing", "gotoAndPlay");
		a.pushByte(0).pushString("NoSuchFunction").op(APT_OP_EA_CALLFUNCPOP);
	});
	m.addCharacter(0);
	m.setRootFrames({ { m.addActionItem(code) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "Calls", m));
	REQUIRE(fx.host.errors.size() == 1);
	CHECK(fx.host.errors[0] == "method 'createTextField' is not a function on a value of type movieclip");
	CHECK(fx.apt->noteCount("call-without-function") == 3);
	CHECK(fx.apt->vm().errors().size() == 4); // the interpreter's own list keeps every report
}

TEST_CASE("player: EnterFrame handlers queue at the FRONT of the pool, Load handlers at the back")
{
	// AptCIH::fire 0x00AE2010: mask 2 -> 0x00AE4C70 (front), mask 1 (default arm) -> 0x00AE4B80 (back); the first advance
	// of a sprite fires Load (0x00AE2E79) and not EnterFrame (0x00AE2E34 with bit 24 set)
	std::uint32_t ce = 0, cl = 0;
	TwoLevelMovie t(2, [&](TestMovie &m, TestMovie::Place &p) {
		ce = program(m, [&](Asm &a) { fscmd(a, "enterFrame"); });
		cl = program(m, [&](Asm &a) { fscmd(a, "load"); });
		p.flags |= APT_PLACE_HASCLIPACTION;
		p.events = { { APT_CLIP_ENTERFRAME, 0, ce }, { APT_CLIP_LOAD, 0, cl } };
	});
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", t.m));
	// pool at load: r0 c0 load (Load queued at the back by the child's first advance)
	CHECK(fx.commandsText() == "r0 c0 load");
	fx.step();
	// the first step adds r1 c1 and pushes enterFrame to the FRONT
	CHECK(fx.commandsText() == "enterFrame r1 c1");
	fx.step();
	// the clip no longer needs its load events: the second step only has EnterFrame (front), then the wrapped frames
	CHECK(fx.commandsText() == "enterFrame r0 c0");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: Initialize and Construct run when the character is placed, before its first frame; the name already exists")
{
	// AptDisplayList setup 0x00AF7E00 (called from 0x00AF8CB3): Initialize (0x200) and Construct (0x40000) fire at once
	// (0x00AE2216: the program becomes a script function called with this = the instance); the name was added to the
	// parent's hash first (0x00AF89A3)
	std::uint32_t init = 0, cons = 0;
	TwoLevelMovie t(1, [&](TestMovie &m, TestMovie::Place &p) {
		init = program(m, [&](Asm &a) {
			fscmd(a, "initialize");
			// this._name is visible: the clip action runs as a function; a variable it sets lands on the clip
			a.pushString("seenName").getStringVar("_name").op(APT_OP_SETVARIABLE);
		});
		cons = program(m, [&](Asm &a) { fscmd(a, "construct"); });
		p.flags |= APT_PLACE_HASCLIPACTION;
		p.events = { { APT_CLIP_INITIALIZE, 0, init }, { APT_CLIP_CONSTRUCT, 0, cons } };
	});
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", t.m));
	// Initialize then Construct fired when the clip was placed, before the frame actions of the load-time pool run
	CHECK(fx.commandsText() == "initialize construct r0 c0");
	REQUIRE(fx.at("c"));
	AptValue seen;
	CHECK(fx.at("c")->getMember("seenName", seen));
	CHECK(seen.toString() == "c");
	fx.step();
	CHECK(fx.commandsText() == "r1");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: a frame action queued by advance is dropped when the clip left that frame; a goto queues unguarded actions")
{
	// 0x00AE6619..0x00AE6661: an action with a negative guard runs only while -guard == the sprite's frame.  advance queues
	// with -frame (0x00AE2E21), gotoFrame with +frame (0x00AE2D43).
	TestMovie m;
	std::vector<std::vector<std::uint32_t>> cf;
	for (int i = 0; i < 3; ++i)
	{
		std::uint32_t code = program(m, [&](Asm &a) { fscmd(a, "c" + std::to_string(i)); });
		cf.push_back({ m.addActionItem(code) });
	}
	std::uint32_t sprite = m.addSprite(cf);
	m.addCharacter(0);
	std::uint32_t cId = m.addCharacter(sprite);
	std::uint32_t jump = program(m, [&](Asm &a) {
		fscmd(a, "jump");
		callMethod(a, "c", "gotoAndStop", {}, { 3 }); // one-based: frame index 2
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(cId, 1, "c")) }, { m.addActionItem(jump) }, {} });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "c0");
	fx.step();
	// step 1: root frame 1 queued `jump`, c (frame 1) queued c1 with guard -1.  jump runs first, moves c to frame 2 and
	// queues c2; the stale c1 is dropped.
	CHECK(fx.commandsText() == "jump c2");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: gotoAndStop to the frame the clip is already on does nothing, not even its frame actions")
{
	// AptCIH::gotoFrame 0x00AE2C77: `cmp edi, eax; je` leaves before anything is queued
	TestMovie m;
	std::uint32_t c1 = program(m, [&](Asm &a) { fscmd(a, "c1"); });
	std::uint32_t sprite = m.addSprite({ {}, { m.addActionItem(c1) }, {} });
	m.addCharacter(0);
	std::uint32_t cId = m.addCharacter(sprite);
	std::uint32_t go = program(m, [&](Asm &a) { callMethod(a, "c", "gotoAndStop", {}, { 2 }); });
	std::uint32_t go2 = program(m, [&](Asm &a) { callMethod(a, "c", "gotoAndStop", {}, { 2 }); });
	m.setRootFrames({ { m.addPlaceItem(placeChar(cId, 1, "c")) }, { m.addActionItem(go) }, { m.addActionItem(go2) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText().empty());
	fx.step();
	// step 1 (root frame 1): c advances to its frame 1 and queues c1 (guard -1); `go` sends it to index 1 (already there):
	// no new action.  The queued c1 still runs (the clip is on frame 1).
	CHECK(fx.commandsText() == "c1");
	fx.step();
	// step 2: c is stopped by gotoAndStop, so it stays on frame 1 and queues nothing; go2 asks for index 1 again
	CHECK(fx.commandsText().empty());
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: seeking converges the display list: the same character at a depth is kept, others are removed")
{
	// AptCIH::gotoFrame 0x00AE2C97 builds the placements in effect at the target (0x00B0F040) and reconciles the display
	// list (0x00AF9410): the same character at a depth is updated in place (0x00AF95A5), absent depths are removed when the
	// seek restarted from frame 0, depths >= 0x4000 (script clips) are left alone (0x00AF943F)
	TestMovie m;
	std::uint32_t shapeA = m.addShape(0, 0, 10, 10, 1);
	std::uint32_t idA = m.addCharacter(shapeA);
	std::uint32_t shapeB = m.addShape(0, 0, 20, 20, 2);
	std::uint32_t idB = m.addCharacter(shapeB);
	std::uint32_t sprite = m.addSprite({
		{ m.addPlaceItem(placeChar(idA, 1, "a")) },
		{ m.addPlaceItem(placeChar(idB, 2, "b")) },
		{ m.addRemoveItem(1) },
		{}
	});
	std::uint32_t cId = m.addCharacter(sprite);
	std::uint32_t stop = program(m, [&](Asm &a) { callMethod(a, "c", "stop"); });
	m.setRootFrames({ { m.addPlaceItem(placeChar(cId, 1, "c")), m.addActionItem(stop) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m)); // frame 0 of the root ran its `stop` action right away: c is stopped on frame 0
	AptSpriteInst *c = fx.at("c")->asSprite();
	REQUIRE(c);
	CHECK(c->frame == 0);
	CHECK_FALSE(c->playing);
	AptCharacterInst *a1 = c->childAtDepth(1);
	REQUIRE(a1);
	CHECK(c->childAtDepth(2) == nullptr);
	AptValue marker = AptValue::integer(5);
	a1->setMember("marker", marker);
	auto go = [&](int frame) { c->timelineOp({ AptTimelineOp::GotoFrame, AptValue::integer(frame), AptValue(), AptValue(), false }); };
	go(1); // the next frame: its place item runs directly (0x00AE2C8D)
	CHECK(c->frame == 1);
	CHECK(c->childAtDepth(1) == a1);
	REQUIRE(c->childAtDepth(2) != nullptr);
	go(0); // backward: replayed from frame 0, the same character at depth 1 is kept, depth 2 is not part of frame 0
	CHECK(c->frame == 0);
	CHECK(c->childAtDepth(1) == a1);
	AptValue seen;
	CHECK(a1->getMember("marker", seen));
	CHECK(seen.toInteger() == 5);
	CHECK(c->childAtDepth(2) == nullptr);
	go(2); // forward by two frames: replayed from the current frame inside the seek
	CHECK(c->childAtDepth(1) == nullptr); // removed on frame 2
	CHECK(c->childAtDepth(2) != nullptr);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: a removed clip fires Unload at once and its name leaves the parent")
{
	// instance finalizer 0x00AE2690: Unload (mask 4) runs immediately, then the clip's onUnload member (0x00AE27F4)
	std::uint32_t unload = 0;
	TestMovie m;
	unload = program(m, [&](Asm &a) { fscmd(a, "unload"); });
	std::uint32_t sprite = m.addSprite({ {} });
	m.addCharacter(0);
	std::uint32_t cId = m.addCharacter(sprite);
	TestMovie::Place p = placeChar(cId, 1, "c");
	p.flags |= APT_PLACE_HASCLIPACTION;
	p.events = { { APT_CLIP_UNLOAD, 0, unload } };
	std::uint32_t rem = program(m, [&](Asm &a) { fscmd(a, "remove"); });
	m.setRootFrames({ { m.addPlaceItem(p) }, { m.addRemoveItem(1), m.addActionItem(rem) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.at("c") != nullptr);
	fx.step();
	// the remove item ran during the advance (Unload fired then, before the pool ran), the frame action after
	CHECK(fx.commandsText() == "unload remove");
	CHECK(fx.at("c") == nullptr);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: an init action runs once, when its frame is processed")
{
	// AptMovie 0x00B0F3CD: item type 8 with a stored id >= 0 runs with the sprite as target, then the id is negated in
	// place (0x00B0F441); the second visit of the frame does not run it
	TestMovie m;
	std::uint32_t code = program(m, [&](Asm &a) { fscmd(a, "init"); });
	std::uint32_t frameCode = program(m, [&](Asm &a) { fscmd(a, "frame"); });
	m.setRootFrames({ { m.addInitActionItem(7, code), m.addActionItem(frameCode) }, {} });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "init frame"); // the init action during the frame's controls, then the frame action
	fx.step();
	CHECK(fx.commandsText().empty());
	fx.step(); // wraps to frame 0 through a seek: the frame action again, the init action not
	CHECK(fx.commandsText() == "frame");
}

TEST_CASE("player: setInterval calls its function every interval, one call per step at most; clearInterval stops it")
{
	// AptTimerFunc 0x00AE4150: remaining -= frame time; at <= 0 call and add the interval back
	TestMovie m;
	std::uint32_t tick = program(m, [&](Asm &a) {
		a.defineFunction("tick", {}, [&](Asm &f) { fscmd(f, "tick"); });
	});
	std::uint32_t start2 = program(m, [&](Asm &a) {
		a.pushString("timerId");
		a.pushShort(100).getStringVar("tick").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION);
		a.op(APT_OP_SETVARIABLE);
	});
	std::uint32_t stop = program(m, [&](Asm &a) {
		a.getStringVar("timerId").pushByte(1).pushString("clearInterval").op(APT_OP_CALLFUNCTION);
		a.op(APT_OP_POP);
	});
	std::vector<std::vector<std::uint32_t>> frames;
	frames.push_back({ m.addActionItem(tick), m.addActionItem(start2) });
	for (int i = 0; i < 15; ++i)
	{
		frames.push_back({});
	}
	frames.push_back({ m.addActionItem(stop) });
	for (int i = 0; i < 3; ++i)
	{
		frames.push_back({});
	}
	m.setRootFrames(frames);
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	std::string log;
	for (int i = 0; i < 18; ++i)
	{
		fx.step();
		std::string c = fx.commandsText();
		log += (c.empty() ? "." : "T");
	}
	// created by the load-time action run with 100 ms remaining; steps 1..4 take it 67, 34, 1, -32: the call lands on step 4,
	// the interval is added back (68) and the pattern repeats every 3 steps (68 -> 35 -> 2 -> -31 on step 7, 69 -> 36 -> 3 ->
	// -30 on step 10, ...).  The timers run before the pool: step 16 still calls once, then the action of frame 16 clears it.
	CHECK(log == "...T..T..T..T..T..");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: getURL2 loadMovie replaces a level at the end of the step; _levelN and the loaded root exist")
{
	// 0x00AD17F0: the request completes at the end of AptUpdate; the first frame of the loaded movie runs at once
	TestMovie lib;
	std::uint32_t libFrame = program(lib, [&](Asm &a) { fscmd(a, "libFrame"); });
	lib.setRootFrames({ { lib.addActionItem(libFrame) } });
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushString("Lib.swf").pushString("_level3").op(APT_OP_GETURL2);
		fscmd(a, "after");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	fx.source.add("Lib", lib);
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "after"); // the load-time pool ran `go`; the request waits for the end of the next step
	CHECK(fx.apt->level(3) == nullptr);
	fx.step();
	// the request completed at the end of the step and the pool ran right after (0x00AD1C58): libFrame
	CHECK(fx.commandsText() == "libFrame");
	REQUIRE(fx.apt->level(3) != nullptr);
	CHECK(fx.apt->level(3)->frame == 0);
	REQUIRE(fx.host.loads.size() == 1);
	CHECK(fx.host.loads[0].movie == "Lib");
	CHECK(fx.host.loads[0].target == "_level3");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: attachMovie, createEmptyMovieClip and removeMovieClip use depth + 0x4000 and a new clip runs its first frame after the action")
{
	// AptDisplayList gpPool->nNewInsts (0x00AF8AD4) / flush 0x00AE4390: a clip created by a script has frame -1 until the end
	// of the action that created it; script depths start at 0x4000 (0x00AF943F)
	TestMovie m;
	std::uint32_t first = program(m, [&](Asm &a) { fscmd(a, "attachedFrame0"); });
	std::uint32_t sprite = m.addSprite({ { m.addActionItem(first) } });
	m.addCharacter(0);
	std::uint32_t sid = m.addCharacter(sprite);
	m.addExport("Gizmo", sid);
	std::uint32_t go = program(m, [&](Asm &a) {
		// this.attachMovie("Gizmo", "g", 5)
		a.pushByte(5).pushString("g").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
		fscmd(a, "afterAttach");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	// the new clip's frame 0 action was queued by the flush that follows the action, so `afterAttach` comes first
	CHECK(fx.commandsText() == "afterAttach attachedFrame0");
	AptCharacterInst *g = fx.at("g");
	REQUIRE(g);
	CHECK(g->depth() == 5 + 0x4000);
	CHECK(fx.errorsText().empty());
}

// ---- stops: behaviours the binary reading did not settle are reported at run time and pinned (docs/STOPS.md S-100..S-109) ----

TEST_CASE("stops S-100: a character the parser has no layout for is placed as an inert instance and reported")
{
	TestMovie m;
	std::uint32_t morph = m.addOpaqueCharacter(APT_CHAR_MORPH);
	std::uint32_t morphId = m.addCharacter(morph);
	std::uint32_t text = m.addOpaqueCharacter(APT_CHAR_STATICTEXT);
	std::uint32_t textId = m.addCharacter(text);
	m.setRootFrames({ { m.addPlaceItem(placeChar(morphId, 1, "morph")), m.addPlaceItem(placeChar(textId, 2, "label")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->noteCount("opaque-character") == 2);
	REQUIRE(fx.at("morph"));
	CHECK(fx.at("morph")->type() == AptCharacterInst::Type::Morph);
	CHECK(fx.at("label")->type() == AptCharacterInst::Type::StaticText);
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	CHECK(rl.commands.empty());
	REQUIRE(rl.unverified.size() == 1);
	CHECK(rl.unverified[0] == "static-text-not-drawn");
}

TEST_CASE("stops S-101: the Unload order of a removed clip's descendants is reported")
{
	TestMovie m;
	std::uint32_t inner = program(m, [&](Asm &a) { fscmd(a, "innerUnload"); });
	std::uint32_t innerSprite = m.addSprite({ {} });
	m.addCharacter(0);
	std::uint32_t innerId = m.addCharacter(innerSprite);
	TestMovie::Place ip = placeChar(innerId, 1, "inner");
	ip.flags |= APT_PLACE_HASCLIPACTION;
	ip.events = { { APT_CLIP_UNLOAD, 0, inner } };
	std::uint32_t outerSprite = m.addSprite({ { m.addPlaceItem(ip) } });
	std::uint32_t outerId = m.addCharacter(outerSprite);
	m.setRootFrames({ { m.addPlaceItem(placeChar(outerId, 1, "outer")) }, { m.addRemoveItem(1) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->noteCount("unload-descendant-order") == 0);
	fx.step();
	// the child's Unload ran (once) and the order question was reported
	CHECK(fx.commandsText() == "innerUnload");
	CHECK(fx.apt->noteCount("unload-descendant-order") == 1);
}

TEST_CASE("stops S-102: Move|Char of a shape over a shape swaps the character in place; other characters replace the instance and are reported")
{
	TestMovie m;
	std::uint32_t a = m.addShape(0, 0, 10, 10, 1);
	std::uint32_t idA = m.addCharacter(a);
	std::uint32_t b = m.addShape(0, 0, 20, 20, 2);
	std::uint32_t idB = m.addCharacter(b);
	std::uint32_t emptySprite = m.addSprite({ {} });
	std::uint32_t idS = m.addCharacter(emptySprite);
	TestMovie::Place replace = placeChar(idB, 1);
	replace.flags |= APT_PLACE_MOVE; // Move | HasCharacter: the 62 retail place objects with flags & 3 == 3
	TestMovie::Place toSprite = placeChar(idS, 1);
	toSprite.flags |= APT_PLACE_MOVE;
	m.setRootFrames({ { m.addPlaceItem(placeChar(idA, 1)) }, { m.addPlaceItem(replace) }, { m.addPlaceItem(toSprite) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	AptCharacterInst *first = fx.root()->childAtDepth(1);
	REQUIRE(first);
	CHECK(first->charRef().character->id == idA);
	CHECK(fx.apt->noteCount("place-over-occupied-depth") == 0);
	fx.step();
	// shape -> shape (the shape tween, all 13 retail cases): same instance, new character, no note (0x00AF8B2C)
	AptCharacterInst *second = fx.root()->childAtDepth(1);
	REQUIRE(second);
	CHECK(second == first);
	CHECK(second->charRef().character->id == idB);
	CHECK(fx.apt->noteCount("place-over-occupied-depth") == 0);
	fx.step();
	// shape -> sprite: not traced, the instance is replaced and the case reported
	AptCharacterInst *third = fx.root()->childAtDepth(1);
	REQUIRE(third);
	CHECK(third->charRef().character->id == idS);
	CHECK(fx.apt->noteCount("place-over-occupied-depth") == 1);
}

TEST_CASE("stops S-103: an init action with id 0 keeps its id (negation of 0), and imports named __Packages.* are reported")
{
	// 0x00B0F441 negates the stored id; -0 == 0 so an id of 0 runs every time its frame is processed.  A clip placed twice
	// processes its frame 0 twice.
	TestMovie m;
	std::uint32_t zero = program(m, [&](Asm &a) { fscmd(a, "init0"); });
	std::uint32_t seven = program(m, [&](Asm &a) { fscmd(a, "init7"); });
	std::uint32_t sprite = m.addSprite({ { m.addInitActionItem(0, zero), m.addInitActionItem(7, seven) } });
	m.addCharacter(0);
	std::uint32_t id = m.addCharacter(sprite);
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "c")) }, { m.addRemoveItem(1) }, { m.addPlaceItem(placeChar(id, 1, "c")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "init0 init7");
	fx.step(2);
	CHECK(fx.commandsText() == "init0"); // the second placement: id 7 was negated, id 0 was not
	CHECK(fx.apt->noteCount("init-action-id-zero-reruns") == 2);

	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 1, 1, 1);
	lib.addCharacter(0);
	std::uint32_t libId = lib.addCharacter(shape);
	lib.addExport("__Packages.Util", libId);
	TestMovie user;
	user.addCharacter(0);
	user.addCharacter(0);
	user.addImport("Lib", "__Packages.Util", 1);
	user.setRootFrames({ {} });
	PlayerFx fx2;
	fx2.source.add("Lib", lib);
	REQUIRE(fx2.load(0, "User", user));
	CHECK(fx2.apt->noteCount("package-import-init-actions-not-run") == 1);
}

TEST_CASE("stops S-104: MovieClip natives whose bodies were not read report what they approximate")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1)) } });
	std::uint32_t id = m.addCharacter(sprite);
	std::uint32_t go = program(m, [&](Asm &a) {
		callMethod(a, "c", "duplicateMovieClip", { "d" }, { 3 });
		callMethod(a, "c", "startDrag");
		callMethod(a, "c", "setMask", {}, { 1 });
		// hitTest(x, y, shapeFlag = true)
		a.op(APT_OP_EA_PUSHTRUE).pushByte(5).pushByte(5).pushByte(3).getStringVar("c").pushString("hitTest").op(APT_OP_EA_CALLMETHODPOP);
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "c")), m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->noteCount("duplicate-copies-placement-only") == 1);
	CHECK(fx.apt->noteCount("startDrag-unsupported") == 1);
	CHECK(fx.apt->noteCount("setMask-unsupported") == 1);
	CHECK(fx.apt->noteCount("hitTest-shape-flag-uses-bounds") == 1);
	AptCharacterInst *copy = fx.at("d");
	REQUIRE(copy);
	CHECK(copy->depth() == 3 + 0x4000);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-105: a movie loaded into a clip keeps the level root as _root, and the case is reported")
{
	TestMovie lib;
	std::uint32_t libCode = program(lib, [&](Asm &a) {
		// _root of the loaded movie, read through a variable on its own timeline
		a.pushString("sawRoot").getStringVar("_root").op(APT_OP_SETVARIABLE);
	});
	lib.setRootFrames({ { lib.addActionItem(libCode) } });
	TestMovie m;
	std::uint32_t sprite = m.addSprite({ {} });
	m.addCharacter(0);
	std::uint32_t id = m.addCharacter(sprite);
	std::uint32_t go = program(m, [&](Asm &a) {
		callMethod(a, "c", "loadMovie", { "Lib.swf" });
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "c")), m.addActionItem(go) } });
	PlayerFx fx;
	fx.source.add("Lib", lib);
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	AptCharacterInst *c = fx.at("c");
	REQUIRE(c);
	CHECK(c->type() == AptCharacterInst::Type::Movie);
	AptValue saw;
	REQUIRE(c->getMember("sawRoot", saw));
	CHECK(saw.isObject());
	CHECK(saw.asObject() == fx.root());
	CHECK(fx.apt->noteCount("movie-in-clip-root-is-level-root") == 1);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-106: unknown clearInterval ids and unmodelled clip properties are reported")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushShort(999).pushByte(1).pushString("clearInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
		a.pushString("q").getStringVar("this").pushString("_quality").op(APT_OP_GETMEMBER).op(APT_OP_SETVARIABLE);
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->noteCount("clearInterval-unknown-id") == 1);
	CHECK(fx.apt->noteCount("unmodelled-clip-property") == 1);
	CHECK(fx.errorsText().empty());
}

// ---- the render command list ------------------------------------------------------------------------------------------------

namespace
{
void addShapeFile(PlayerFx &fx, const std::string &movie, int id)
{
	std::string ru = "s s:10:20:30:255\nt 0:0:10:0:10:10\nc\n";
	fx.source.files[MemorySource::lower(movie) + "_geometry/" + std::to_string(id) + ".ru"] = std::vector<std::uint8_t>(ru.begin(), ru.end());
}
} // namespace

TEST_CASE("render list: shapes in depth order, matrices and colour transforms compose down the tree, hidden clips are skipped")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place inner = placeChar(shapeId, 1, "s");
	inner.flags |= APT_PLACE_HASMATRIX | APT_PLACE_HASCOLORTRANSFORM;
	inner.translation[0] = 5;
	inner.translation[1] = 6;
	inner.tint[0] = 127;
	inner.tint[1] = 255;
	inner.tint[2] = 255;
	inner.tint[3] = 128;
	inner.additive[1] = 40;
	std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(inner) } });
	std::uint32_t spriteId = m.addCharacter(sprite);
	TestMovie::Place outerB = placeChar(spriteId, 2, "b"); // higher depth first in the file: the list is by depth
	outerB.flags |= APT_PLACE_HASMATRIX;
	outerB.matrix[0] = 2;
	outerB.matrix[3] = 3;
	outerB.translation[0] = 100;
	outerB.translation[1] = 200;
	TestMovie::Place outerA = placeChar(spriteId, 1, "a");
	outerA.flags |= APT_PLACE_HASMATRIX;
	outerA.translation[0] = 1;
	TestMovie::Place hidden = placeChar(spriteId, 3, "hidden");
	m.setRootFrames({ { m.addPlaceItem(outerB), m.addPlaceItem(outerA), m.addPlaceItem(hidden) } });
	PlayerFx fx;
	addShapeFile(fx, "A", 1);
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	fx.at("hidden")->visible = false;
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	CHECK(rl.errors.empty());
	REQUIRE(rl.commands.size() == 2);
	// depth order: a (depth 1) before b (depth 2)
	CHECK(rl.commands[0].path == "_level0.a.s");
	CHECK(rl.commands[1].path == "_level0.b.s");
	CHECK(rl.commands[0].matrix.tx == doctest::Approx(1.0f + 5.0f));
	// b: scale (2, 3) at (100, 200); the shape at (5, 6) inside it: x = 2*5 + 100, y = 3*6 + 200
	CHECK(rl.commands[1].matrix.a == doctest::Approx(2.0f));
	CHECK(rl.commands[1].matrix.d == doctest::Approx(3.0f));
	CHECK(rl.commands[1].matrix.tx == doctest::Approx(110.0f));
	CHECK(rl.commands[1].matrix.ty == doctest::Approx(218.0f));
	// colour: the place object's tint bytes are B, G, R, A (0xAARRGGBB, BFME2 0x00AF7160): byte 0 = 127 is the blue multiplier
	CHECK(rl.commands[1].color.mul[0] == doctest::Approx(1.0f));
	CHECK(rl.commands[1].color.mul[2] == doctest::Approx(127.0f / 255.0f));
	CHECK(rl.commands[1].color.mul[3] == doctest::Approx(128.0f / 255.0f));
	CHECK(rl.commands[1].color.add[1] == doctest::Approx(40.0f));
	REQUIRE(rl.commands[1].geometry);
	REQUIRE(rl.commands[1].fills.size() == 1);
	CHECK(rl.commands[1].fills[0].kind == APT_STYLE_SOLID);
	CHECK(rl.commands[1].fills[0].rgba[0] == 10.0f);
	CHECK(rl.commands[1].fills[0].style->triangles.size() == 6);
	CHECK(rl.unverified.empty()); // the additive term's unit and use are target facts (lane UI-2: AptCanvas.h AptRetailVertexColour)
}

TEST_CASE("render list: a clip layer emits MaskBegin, the mask shape, MaskContentBegin, the clipped range and MaskEnd")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place mask = placeChar(shapeId, 1, "mask");
	mask.flags |= APT_PLACE_HASCLIPDEPTH;
	mask.clipDepth = 3;
	m.setRootFrames({ { m.addPlaceItem(mask), m.addPlaceItem(placeChar(shapeId, 2, "in2")), m.addPlaceItem(placeChar(shapeId, 3, "in3")),
		m.addPlaceItem(placeChar(shapeId, 4, "out4")) } });
	PlayerFx fx;
	addShapeFile(fx, "A", 1);
	REQUIRE(fx.load(0, "A", m));
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	using K = AptRenderCommand::Kind;
	std::vector<K> kinds;
	for (const AptRenderCommand &c : rl.commands)
	{
		kinds.push_back(c.kind);
	}
	// the clipped range is depths above the mask up to and including its clip depth (2 and 3); depth 4 is outside
	CHECK(kinds == std::vector<K>{ K::MaskBegin, K::Shape, K::MaskContentBegin, K::Shape, K::Shape, K::MaskEnd, K::Shape });
	CHECK(rl.commands[0].clipDepth == 3);
	CHECK(rl.commands[1].path == "_level0.mask");
	CHECK(rl.commands[6].path == "_level0.out4");
	CHECK(rl.unverified == std::vector<std::string>{ "clip-layer" });
}

namespace
{
class ComponentHost : public AptStubHost
{
public:
	bool isComponentSymbol(const std::string &movie, const std::string &name) override { return movie == "Lib" && name == "Gadget"; }
};
} // namespace

TEST_CASE("render list: an instance of a symbol the host calls a component is one Placeholder with its bounds and origin")
{
	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 40, 20, 1);
	lib.addCharacter(0);
	std::uint32_t libShape = lib.addCharacter(shape);
	std::uint32_t sprite = lib.addSprite({ { lib.addPlaceItem(placeChar(libShape, 1)) } });
	std::uint32_t libSprite = lib.addCharacter(sprite);
	lib.addExport("Gadget", libSprite);
	TestMovie user;
	user.addCharacter(0);
	user.addCharacter(0);
	user.addImport("Lib", "Gadget", 1);
	TestMovie::Place p = placeChar(1, 1, "g");
	p.flags |= APT_PLACE_HASMATRIX;
	p.translation[0] = 7;
	p.translation[1] = 8;
	user.setRootFrames({ { user.addPlaceItem(p) } });
	MemorySource source;
	ComponentHost host;
	Apt apt(source, host);
	source.add("Lib", lib);
	source.add("User", user);
	std::string error;
	REQUIRE_MESSAGE(apt.loadMovie(0, "User", &error), error);
	AptRenderList rl;
	apt.buildRenderList(rl);
	REQUIRE(rl.commands.size() == 1);
	const AptRenderCommand &c = rl.commands[0];
	CHECK(c.kind == AptRenderCommand::Kind::Placeholder);
	CHECK(c.symbolMovie == "Lib");
	CHECK(c.symbolName == "Gadget");
	CHECK(c.path == "_level0.g");
	CHECK(c.matrix.tx == doctest::Approx(7.0f));
	CHECK(c.bounds[2] == doctest::Approx(40.0f));
	CHECK(c.bounds[3] == doctest::Approx(20.0f));
}

TEST_CASE("render list: a text field bound to a variable shows the variable's value; its font comes from the font character")
{
	TestMovie m;
	std::uint32_t font = m.addFont("Albertus MT", {});
	m.addCharacter(font); // id 0: the font id the test builder writes into every text character
	std::uint32_t text = m.addEditText("$INITIAL", "caption", 1, 14.0f);
	std::uint32_t textId = m.addCharacter(text);
	std::uint32_t setup = program(m, [&](Asm &a) { a.pushString("caption").pushString("Hello").op(APT_OP_SETVARIABLE); });
	m.setRootFrames({ { m.addPlaceItem(placeChar(textId, 1, "t")), m.addActionItem(setup) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 1);
	CHECK(rl.commands[0].kind == AptRenderCommand::Kind::Text);
	CHECK(rl.commands[0].text == "Hello");
	CHECK(rl.commands[0].variable == "caption");
	CHECK(rl.commands[0].fontHeight == doctest::Approx(14.0f));
	CHECK(rl.commands[0].alignment == 1);
	CHECK(rl.commands[0].fontName == "Albertus MT");
	// and the engine side can set the text directly
	static_cast<AptTextInst *>(fx.at("t"))->text = "set by the engine";
	fx.apt->vm().setMember(AptValue::object(fx.root()), "caption", AptValue());
	AptRenderList rl2;
	fx.apt->buildRenderList(rl2);
	CHECK(rl2.commands[0].text == "set by the engine"); // the variable is undefined again: the field's own text
}

TEST_CASE("host: commands the engine does not know are recorded in order, never thrown or dropped; an extern nobody provides is logged")
{
	// menus-apt.md 3.3: an unregistered command is logged and ignored by WindowManager; the recorded host keeps every one
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.getURL("FSCommand:Totally::Unknown", "arg one");
		a.getURL("FSCommand:Another", "");
		// extern.NoSuchThing read into a variable
		a.pushString("v").getStringVar("extern").pushString("NoSuchThing").op(APT_OP_GETMEMBER).op(APT_OP_SETVARIABLE);
		a.getURL("FSCommand:Last", "z");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	REQUIRE(fx.host.fscommands.size() == 3);
	CHECK(fx.host.fscommands[0].command == "Totally::Unknown");
	CHECK(fx.host.fscommands[0].argument == "arg one");
	CHECK(fx.host.fscommands[1].command == "Another");
	CHECK(fx.host.fscommands[2].command == "Last");
	REQUIRE(fx.host.unknownExterns.size() == 1);
	CHECK(fx.host.unknownExterns[0] == "NoSuchThing");
	REQUIRE(fx.host.errors.size() == 1);
	CHECK(fx.host.errors[0].find("extern.NoSuchThing read") == 0);
	// the stub's four default providers answer
	std::string value;
	CHECK(fx.host.getExtern("InGame", value) == AptExternResult::Value);
	CHECK(value == "1");
	CHECK(fx.host.getExtern("InBetaDemo", value) == AptExternResult::Value);
	CHECK(value == "0");
}

TEST_CASE("player: a script-created clip survives the timeline's own seeks")
{
	// the reconcile of a backward seek leaves depths >= 0x4000 alone (0x00AF943F)
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "timeline")) }, {} });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	AptSpriteInst *e = fx.root()->createEmptyClip("e", 7 + 0x4000);
	REQUIRE(e);
	CHECK(fx.at("e") == e);
	fx.step(4); // two wraps of the 2-frame root: two backward seeks
	CHECK(fx.at("e") == e);
	CHECK(fx.root()->childAtDepth(7 + 0x4000) == e);
	CHECK(fx.at("timeline") != nullptr);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("player: createEmptyMovieClip puts the clip at depth + 0x4000; removeMovieClip removes it and its name")
{
	// removal unlinks the instance and erases the name from the parent's hash (0x00AF96EB)
	TestMovie m;
	std::uint32_t create = program(m, [&](Asm &a) {
		a.pushByte(7).pushString("e").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
	});
	std::uint32_t remove = program(m, [&](Asm &a) {
		callMethod(a, "e", "removeMovieClip");
		fscmd(a, "removed");
	});
	m.setRootFrames({ { m.addActionItem(create) }, {}, { m.addActionItem(remove) }, {} });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	AptCharacterInst *e = fx.at("e");
	REQUIRE(e);
	CHECK(e->depth() == 7 + 0x4000);
	fx.step(2); // root frame 2: the removal action runs in the pool of that step
	CHECK(fx.commandsText() == "removed");
	CHECK(fx.at("e") == nullptr);
	CHECK(fx.root()->childAtDepth(7 + 0x4000) == nullptr);
	CHECK(fx.errorsText().empty());
}
