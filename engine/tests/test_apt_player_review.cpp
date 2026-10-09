// OpenBFME unit tests: regression pins for the review of the Apt player (lane APT-2, review round 1).  Every test here failed
// before its fix: a crash on an invalid getBounds target, the member-handler queue order, the timer walk, a double Unload, script
// depth collisions, stale removal, silent render-resource failures and unreported assumptions.
//
// Binary facts (BFME2 1.06 game.dat; RotWK counterparts carry the S-001 caveat) are cited at each test.  GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include <set>

using namespace apttest;

namespace
{

std::vector<std::string> noteDetails(PlayerFx &fx, const std::string &kind)
{
	std::vector<std::string> out;
	for (const AptNote &n : fx.apt->notes())
	{
		if (n.kind == kind)
		{
			out.push_back(n.detail);
		}
	}
	return out;
}

std::size_t countOf(const std::string &text, const std::string &what)
{
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
	{
		++n;
	}
	return n;
}

// A clip `c` (a 10 x 10 shape inside a sprite) at (50, 50) on the root, and the sprite exported as "Gizmo".
struct ClipRoot
{
	TestMovie m;
	std::uint32_t spriteId = 0;
	ClipRoot()
	{
		std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
		m.addCharacter(0);
		std::uint32_t shapeId = m.addCharacter(shape);
		std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1)) } });
		spriteId = m.addCharacter(sprite);
		m.addExport("Gizmo", spriteId);
	}
	TestMovie::Place placeC()
	{
		TestMovie::Place p = placeChar(spriteId, 1, "c");
		p.flags |= APT_PLACE_HASMATRIX;
		p.translation[0] = 50;
		p.translation[1] = 50;
		return p;
	}
};

} // namespace

// ---- P1: getBounds validates its target -----------------------------------------------------------------------------------------

TEST_CASE("review: getBounds with a target that is not a live clip reports it and returns undefined instead of crashing")
{
	ClipRoot r;
	std::uint32_t go = program(r.m, [&](Asm &a) {
		// c.getBounds(<arg>) for a plain object, an array, a number and a native object
		a.pushByte(0).op(APT_OP_INITOBJECT);
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(0).op(APT_OP_INITARRAY);
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(5);
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("Key");
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		// and the legal forms: no argument, null (PushNull and PushUndefined push the same singleton, 0x00B05320: the clip's own
		// space), and a live clip as the coordinate space
		a.op(APT_OP_EA_PUSHNULL);
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(0).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("c");
		a.pushByte(1).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		fscmd(a, "done");
	});
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()), r.m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	CHECK(fx.commandsText() == "done");
	CHECK(countOf(fx.errorsText(), "getBounds: the target space is not a live clip") == 4);
	CHECK(fx.errorsText().size() == 4 * std::string("getBounds: the target space is not a live clip; ").size());
}

TEST_CASE("review: getBounds of a clip that was removed is reported, not computed")
{
	ClipRoot r;
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	AptCharacterInst *c = fx.at("c");
	REQUIRE(c);
	AptValue native;
	REQUIRE(c->getMember("getBounds", native));
	fx.root()->removeObject(c->depth()); // c is now a dead instance
	AptValue result = fx.apt->vm().callFunction(native, AptValue::object(c), std::vector<AptValue>{ AptValue::object(c) });
	CHECK(result.isUndefined());
	CHECK(countOf(fx.errorsText(), "getBounds: the target space is not a live clip") == 1);
}

// ---- P1: member handlers queue order (AptCIH::fire 0x00AE2010) -------------------------------------------------------------------

TEST_CASE("review: a member onEnterFrame handler is queued at the FRONT of the pool, ahead of the frame actions of the same advance")
{
	// BFME2 0x00AE231D..0x00AE2349: every member handler except RollOver (0x2000) / RollOut (0x4000) goes through 0x00AE3810 (prepend);
	// RotWK 0x00AF65ED onwards (counterparts of 0x00AE3810 / 0x00AE3740 are 0x00AF7B10 / 0x00AF7A40)
	TestMovie m;
	std::uint32_t c1 = program(m, [&](Asm &a) { fscmd(a, "c1"); });
	std::uint32_t c0 = program(m, [&](Asm &a) { fscmd(a, "c0"); });
	std::uint32_t sprite = m.addSprite({ { m.addActionItem(c0) }, { m.addActionItem(c1) } });
	m.addCharacter(0);
	std::uint32_t id = m.addCharacter(sprite);
	std::uint32_t assign = program(m, [&](Asm &a) {
		a.getStringVar("c").pushString("onEnterFrame");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "enter"); });
		a.op(APT_OP_SETMEMBER);
	});
	std::uint32_t r1 = program(m, [&](Asm &a) { fscmd(a, "r1"); });
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "c")), m.addActionItem(assign) }, { m.addActionItem(r1) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.commands();
	fx.step();
	// the pool of the step: r1 (root, queued first), c1 (child), the handler pushed to the front
	CHECK(fx.commandsText() == "enter r1 c1");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("review: an inherited onLoad is queued at the front and once more at the back (0x00AE2412..0x00AE242F)")
{
	TestMovie m;
	std::uint32_t c0 = program(m, [&](Asm &a) { fscmd(a, "c0"); });
	std::uint32_t sprite = m.addSprite({ { m.addActionItem(c0) } });
	m.addCharacter(0);
	std::uint32_t id = m.addCharacter(sprite);
	std::uint32_t assign = program(m, [&](Asm &a) {
		a.getStringVar("MovieClip").pushString("prototype").op(APT_OP_GETMEMBER).pushString("onLoad");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "protoLoad"); });
		a.op(APT_OP_SETMEMBER);
	});
	std::uint32_t r1 = program(m, [&](Asm &a) { fscmd(a, "r1"); });
	m.setRootFrames({ { m.addActionItem(assign) }, { m.addPlaceItem(placeChar(id, 1, "c")), m.addActionItem(r1) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.commands();
	fx.step();
	// pool: r1, c0 (queued by the advance), then the Load handler: front once, back once
	CHECK(fx.commandsText() == "protoLoad r1 c0 protoLoad");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("review: member handlers of Press go to the front, of RollOver to the back, around the place object's clip event programs")
{
	// the clip event programs of Press / RollOver are queued at the back (0x00AE2149); the member handler of Press at the front
	// (0x00AE3810), of RollOver at the back (0x00AE3740)
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1)) } });
	std::uint32_t spriteId = m.addCharacter(sprite);
	TestMovie::Place p = placeChar(spriteId, 1, "c");
	p.flags |= APT_PLACE_HASMATRIX | APT_PLACE_HASCLIPACTION;
	p.translation[0] = 50;
	p.translation[1] = 50;
	p.events = { { APT_CLIP_ROLLOVER, 0, program(m, [&](Asm &a) { fscmd(a, "progOver"); }) }, { APT_CLIP_PRESS, 0, program(m, [&](Asm &a) { fscmd(a, "progPress"); }) } };
	std::uint32_t assign = program(m, [&](Asm &a) {
		a.getStringVar("c").pushString("onRollOver");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "memberOver"); });
		a.op(APT_OP_SETMEMBER);
		a.getStringVar("c").pushString("onPress");
		a.defineFunction("", {}, [&](Asm &f) { fscmd(f, "memberPress"); });
		a.op(APT_OP_SETMEMBER);
	});
	m.setRootFrames({ { m.addPlaceItem(p), m.addActionItem(assign) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	fx.commands();
	fx.apt->input().postMouseMove(55, 55);
	fx.step(2);
	CHECK(fx.commandsText() == "progOver memberOver");
	fx.apt->input().postMouseButton(true);
	fx.step(2);
	CHECK(fx.commandsText() == "memberPress progPress");
	CHECK(fx.errorsText().empty());
}

// ---- P1: timers (AptTimerFunc 0x00AE4150) -------------------------------------------------------------------------------------

namespace
{
// root frame 0 defines `spawn` (an fscommand, then setInterval(spawn, 33)) and starts the first timer
std::uint32_t spawnProgram(TestMovie &m)
{
	return program(m, [&](Asm &a) {
		a.defineFunction("spawn", {}, [&](Asm &f) {
			fscmd(f, "spawn");
			f.pushShort(33).getStringVar("spawn").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
		});
		a.pushShort(33).getStringVar("spawn").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
	});
}
} // namespace

TEST_CASE("review: the timer walk is bounded by the active-entry count it started with: self-spawning timers double per step, they do not run away")
{
	// 0x00AE4156 loads the active-entry count, 0x00AE4160 keeps it, 0x00AE434E counts down once per active entry handled and
	// 0x00AE4352 leaves the loop at zero (RotWK 0x00AF8466 / 0x00AF8470 / 0x00AF865E)
	TestMovie m;
	m.setRootFrames({ { m.addActionItem(spawnProgram(m)) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.commands();
	fx.step();
	CHECK(fx.commandsText() == "spawn");
	fx.step();
	CHECK(fx.commandsText() == "spawn spawn"); // the two timers that were active; the two they created wait
	fx.step();
	CHECK(fx.commandsText() == "spawn spawn spawn spawn");
	fx.step();
	CHECK(countOf(fx.commandsText(), "spawn") == 8);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("review: a callback may clear a timer that has not run yet in this step; a cleared slot is reused")
{
	// A (slot 0) clears B (slot 1) and itself, then starts C, which takes slot 0.  B must not run; C's remaining time is the
	// initial interval plus the interval 0x00AE4319 adds back to the slot after the call, so it fires two steps later.
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.defineFunction("fnB", {}, [&](Asm &f) { fscmd(f, "B"); });
		a.defineFunction("fnC", {}, [&](Asm &f) { fscmd(f, "C"); });
		a.defineFunction("fnA", {}, [&](Asm &f) {
			fscmd(f, "A");
			f.getStringVar("idB").pushByte(1).pushString("clearInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			f.getStringVar("idA").pushByte(1).pushString("clearInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			f.pushShort(33).getStringVar("fnC").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
		});
		a.pushString("idA").pushShort(33).getStringVar("fnA").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_SETVARIABLE);
		a.pushString("idB").pushShort(33).getStringVar("fnB").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_SETVARIABLE);
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	fx.commands();
	fx.step();
	CHECK(fx.commandsText() == "A");
	fx.step();
	CHECK(fx.commandsText().empty());
	fx.step();
	CHECK(fx.commandsText() == "C");
	CHECK(fx.errorsText().empty());
}

TEST_CASE("review: a timer appended by a callback waits; a timer that takes the slot of a cleared timer ahead of the walk is visited and consumes the budget")
{
	// the budget is a count (0x00AE4156 / 0x00AE434E), not a snapshot of identities.
	// Case 1: A (slot 0) creates C, which is appended behind B (slot 1): budget 2 is used up by A and B, C waits.
	{
		TestMovie m;
		std::uint32_t go = program(m, [&](Asm &a) {
			a.defineFunction("fnB", {}, [&](Asm &f) { fscmd(f, "B"); });
			a.defineFunction("fnC", {}, [&](Asm &f) { fscmd(f, "C"); });
			a.defineFunction("fnA", {}, [&](Asm &f) {
				fscmd(f, "A");
				f.pushShort(33).getStringVar("fnC").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			});
			a.pushShort(33).getStringVar("fnA").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			a.pushShort(33).getStringVar("fnB").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
		});
		m.setRootFrames({ { m.addActionItem(go) } });
		PlayerFx fx;
		REQUIRE(fx.load(0, "A", m));
		fx.commands();
		fx.step();
		CHECK(fx.commandsText() == "A B");
	}
	// Case 2: A (slot 0) clears B (slot 1) and creates C, which takes B's slot: the budget is still 2, so after A the walk visits
	// slot 1, which is C, and C runs in this step; B never runs.
	{
		TestMovie m;
		std::uint32_t go = program(m, [&](Asm &a) {
			a.defineFunction("fnB", {}, [&](Asm &f) { fscmd(f, "B"); });
			a.defineFunction("fnC", {}, [&](Asm &f) { fscmd(f, "C"); });
			a.defineFunction("fnA", {}, [&](Asm &f) {
				fscmd(f, "A");
				f.getStringVar("idB").pushByte(1).pushString("clearInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
				f.pushShort(33).getStringVar("fnC").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			});
			a.pushShort(33).getStringVar("fnA").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_POP);
			a.pushString("idB").pushShort(33).getStringVar("fnB").pushByte(2).pushString("setInterval").op(APT_OP_CALLFUNCTION).op(APT_OP_SETVARIABLE);
		});
		m.setRootFrames({ { m.addActionItem(go) } });
		PlayerFx fx;
		REQUIRE(fx.load(0, "A", m));
		fx.commands();
		fx.step();
		CHECK(fx.commandsText() == "A C");
		CHECK(fx.errorsText().empty());
	}
}

// ---- P1: teardown is idempotent -----------------------------------------------------------------------------------------------

namespace
{
// A removed sprite P holds a (depth 1) and b (depth 2), both with an Unload program.  a's removes `removeWhat` first.
struct TeardownMovie
{
	TestMovie m;
	explicit TeardownMovie(bool aRemovesParent)
	{
		std::uint32_t aProg = program(m, [&](Asm &a) {
			fscmd(a, "aUnload");
			a.pushByte(0);
			a.getStringVar("this").pushString("_parent").op(APT_OP_GETMEMBER);
			if (!aRemovesParent)
			{
				a.pushString("b").op(APT_OP_GETMEMBER);
			}
			a.pushString("removeMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		});
		std::uint32_t bProg = program(m, [&](Asm &a) { fscmd(a, "bUnload"); });
		std::uint32_t leaf = m.addSprite({ {} });
		m.addCharacter(0);
		std::uint32_t leafId = m.addCharacter(leaf);
		TestMovie::Place pa = placeChar(leafId, 1, "a");
		pa.flags |= APT_PLACE_HASCLIPACTION;
		pa.events = { { APT_CLIP_UNLOAD, 0, aProg } };
		TestMovie::Place pb = placeChar(leafId, 2, "b");
		pb.flags |= APT_PLACE_HASCLIPACTION;
		pb.events = { { APT_CLIP_UNLOAD, 0, bProg } };
		std::uint32_t parent = m.addSprite({ { m.addPlaceItem(pa), m.addPlaceItem(pb) } });
		std::uint32_t parentId = m.addCharacter(parent);
		m.setRootFrames({ { m.addPlaceItem(placeChar(parentId, 1, "p")) }, { m.addRemoveItem(1) } });
	}
};
} // namespace

TEST_CASE("review: a sibling removed by an Unload callback during a teardown gets its Unload once")
{
	TeardownMovie t(false);
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", t.m));
	fx.commands();
	fx.step();
	CHECK(fx.commandsText() == "aUnload bUnload");
	CHECK(fx.errorsText().empty());
	CHECK(fx.at("p") == nullptr);
}

TEST_CASE("review: an Unload callback that removes the clip already being torn down does not unload anything twice")
{
	// the parent was unlinked before its teardown began, so removeMovieClip sees a stale reference and reports it
	TeardownMovie t(true);
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", t.m));
	fx.commands();
	fx.step();
	CHECK(fx.commandsText() == "aUnload bUnload");
	CHECK(countOf(fx.errorsText(), "removeMovieClip: the target is not a live clip of its parent (a stale reference)") == 1);
}

// ---- P2: one instance per depth for script-created clips ---------------------------------------------------------------------

namespace
{
std::size_t childrenAtDepth(AptSpriteInst *s, int depth)
{
	std::size_t n = 0;
	for (AptCharacterInst *c : s->children())
	{
		n += (c->depth() == depth) ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("review: duplicateMovieClip and attachMovie over an occupied script depth replace the occupant (one instance per depth)")
{
	ClipRoot r;
	std::uint32_t go = program(r.m, [&](Asm &a) {
		callMethod(a, "c", "duplicateMovieClip", { "d1" }, { 3 });
		callMethod(a, "c", "duplicateMovieClip", { "d2" }, { 3 });
		// this.attachMovie("Gizmo", "g1", 4) twice, the second under another name
		a.pushByte(4).pushString("g1").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(4).pushString("g2").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
	});
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()), r.m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	AptSpriteInst *root = fx.root();
	CHECK(childrenAtDepth(root, 3 + 0x4000) == 1);
	CHECK(childrenAtDepth(root, 4 + 0x4000) == 1);
	CHECK(fx.at("d1") == nullptr);
	CHECK(fx.at("g1") == nullptr);
	REQUIRE(fx.at("d2"));
	REQUIRE(fx.at("g2"));
	CHECK(root->childAtDepth(3 + 0x4000) == fx.at("d2"));
	CHECK(root->childAtDepth(4 + 0x4000) == fx.at("g2"));
	CHECK(noteDetails(fx, "script-depth-occupied") == std::vector<std::string>{ "depth 3", "depth 4" });
	CHECK(fx.errorsText().empty());
}

TEST_CASE("review: createEmptyMovieClip over an occupied script depth removes the occupant, reports it, and the old reference is dead")
{
	TestMovie m;
	m.setRootFrames({ { } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	AptSpriteInst *e1 = fx.root()->createEmptyClip("e", 7 + 0x4000);
	AptSpriteInst *e2 = fx.root()->createEmptyClip("e", 7 + 0x4000);
	REQUIRE(e1);
	REQUIRE(e2);
	CHECK_FALSE(e1->defined());
	CHECK(childrenAtDepth(fx.root(), 7 + 0x4000) == 1);
	CHECK(fx.at("e") == e2); // the name points at the live occupant
	CHECK(noteDetails(fx, "script-depth-occupied") == std::vector<std::string>{ "depth 7" });
}

// ---- P2: removal needs the live occupant -------------------------------------------------------------------------------------

TEST_CASE("review: removeMovieClip on a stale reference does not remove the clip that replaced it")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushByte(5).pushString("e1").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushString("old").getStringVar("this").pushString("e1").op(APT_OP_GETMEMBER).op(APT_OP_SETVARIABLE);
		a.pushByte(5).pushString("e2").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		callMethod(a, "old", "removeMovieClip");
		fscmd(a, "done");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "done");
	REQUIRE(fx.at("e2"));
	CHECK(fx.at("e2")->defined());
	CHECK(fx.root()->childAtDepth(5 + 0x4000) == fx.at("e2"));
	CHECK(countOf(fx.errorsText(), "removeMovieClip: the target is not a live clip of its parent (a stale reference)") == 1);
}

TEST_CASE("review: removeMovieClip twice, and after a depth change, removes exactly the live clip")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushByte(5).pushString("e").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushString("keep").getStringVar("this").pushString("e").op(APT_OP_GETMEMBER).op(APT_OP_SETVARIABLE);
		callMethod(a, "e", "swapDepths", {}, { 9 }); // depth change: the reference stays live
		callMethod(a, "keep", "removeMovieClip");
		callMethod(a, "keep", "removeMovieClip"); // already removed: stale
		fscmd(a, "done");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "done");
	CHECK(fx.at("e") == nullptr);
	CHECK(fx.root()->childAtDepth(5 + 0x4000) == nullptr);
	CHECK(fx.root()->childAtDepth(9 + 0x4000) == nullptr);
	CHECK(countOf(fx.errorsText(), "(a stale reference)") == 1);
}

// ---- P2: render resource failures are reported on every build -----------------------------------------------------------------

TEST_CASE("review: a missing shape geometry is reported by every render list build, not only the first")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx; // no `a_geometry/1.ru`
	REQUIRE(fx.load(0, "A", m));
	AptRenderList first, second, third;
	fx.apt->buildRenderList(first);
	fx.apt->buildRenderList(second);
	fx.apt->buildRenderList(third);
	REQUIRE(first.errors.size() == 1);
	CHECK(first.errors[0].find("A shape") == 0);
	CHECK(first.errors[0] == "A shape 1: A_geometry/1.ru: file not found: A_geometry/1.ru");
	CHECK(second.errors == first.errors);
	CHECK(third.errors == first.errors);
	REQUIRE(first.commands.size() == 1);
	CHECK(first.commands[0].geometry == nullptr);
}

TEST_CASE("review: a text field whose font does not resolve, or is not a font, reports it")
{
	TestMovie m;
	m.addCharacter(m.addShape(0, 0, 10, 10, 1)); // id 0: a shape, not a font
	std::uint32_t wrongType = m.addEditText("x", "", 0, 12.0f, 0);
	std::uint32_t wrongTypeId = m.addCharacter(wrongType);
	std::uint32_t unresolved = m.addEditText("y", "", 0, 12.0f, 99);
	std::uint32_t unresolvedId = m.addCharacter(unresolved);
	m.setRootFrames({ { m.addPlaceItem(placeChar(wrongTypeId, 1, "t1")), m.addPlaceItem(placeChar(unresolvedId, 2, "t2")) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	for (int pass = 0; pass < 2; ++pass)
	{
		AptRenderList rl;
		fx.apt->buildRenderList(rl);
		REQUIRE(rl.commands.size() == 2);
		CHECK(rl.commands[0].fontName.empty());
		CHECK(rl.commands[1].fontName.empty());
		REQUIRE(rl.errors.size() == 2);
		CHECK(rl.errors[0].find("(_level0.t1): font 0 is a character of type") != std::string::npos);
		CHECK(rl.errors[0].find(", not a font") != std::string::npos);
		CHECK(rl.errors[1].find("(_level0.t2): font 99 does not resolve") != std::string::npos);
	}
}

TEST_CASE("stops S-109: a .dat rectangle image reaches the fill unchanged and the list reports rect-image; an id with no entry is an error")
{
	// The `.dat` forms are `N->T` (image N is texture T) and `N=x y w h` (a rectangle of texture N; spec 2.5, OpenSAGE leaves the
	// rectangle branch TODO).  The player passes the numbers through and names the assumption; it does not crop.
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx;
	std::string ru = "s tc:255:255:255:255:7:1:0:0:1:0:0\nt 0:0:10:0:10:10\nc\ns tc:255:255:255:255:8:1:0:0:1:0:0\nt 0:0:10:0:10:10\nc\ns tc:255:255:255:255:9:1:0:0:1:0:0\nt 0:0:10:0:10:10\n";
	fx.source.files["a_geometry/1.ru"] = std::vector<std::uint8_t>(ru.begin(), ru.end());
	std::string dat = "7=12 34 56 78\n8->3\n"; // 9 has no entry
	fx.source.files["a.dat"] = std::vector<std::uint8_t>(dat.begin(), dat.end());
	REQUIRE(fx.load(0, "A", m));
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 1);
	REQUIRE(rl.commands[0].fills.size() == 3);
	const AptRenderFill &rect = rl.commands[0].fills[0];
	CHECK(rect.imageResolved);
	CHECK(rect.imageIsRect);
	CHECK(rect.imageRect[0] == 12);
	CHECK(rect.imageRect[1] == 34);
	CHECK(rect.imageRect[2] == 56);
	CHECK(rect.imageRect[3] == 78);
	CHECK(rect.textureName == "apt_A_7.tga");
	const AptRenderFill &plain = rl.commands[0].fills[1];
	CHECK(plain.imageResolved);
	CHECK_FALSE(plain.imageIsRect);
	CHECK(plain.textureName == "apt_A_3.tga");
	CHECK_FALSE(rl.commands[0].fills[2].imageResolved);
	REQUIRE(rl.errors.size() == 1);
	CHECK(rl.errors[0] == "A shape 1: image 9 has no .dat entry");
	CHECK(rl.unverified == std::vector<std::string>{ "rect-image" });
}

// ---- P2: assumptions are reported when they are executed (S-104, S-106, S-108) ----------------------------------------------------

TEST_CASE("stops S-104: every unread MovieClip and Color native reports itself when it runs")
{
	ClipRoot r;
	std::uint32_t go = program(r.m, [&](Asm &a) {
		a.pushByte(7).pushString("e").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(5).pushString("g").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(0).getStringVar("c").pushString("getBounds").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(0).op(APT_OP_INITOBJECT);
		a.pushByte(1).getStringVar("c").pushString("localToGlobal").op(APT_OP_EA_CALLMETHODPOP);
		callMethod(a, "c", "swapDepths", {}, { 9 });
		callMethod(a, "c", "getDepth");
		callMethod(a, "e", "removeMovieClip");
		a.pushString("col").getStringVar("c").pushByte(1).pushString("Color").op(APT_OP_CALLFUNCTION).op(APT_OP_SETVARIABLE);
		callMethod(a, "col", "setRGB", {}, { 255 });
		callMethod(a, "col", "getRGB");
		a.pushByte(0).op(APT_OP_INITOBJECT);
		a.pushByte(1).getStringVar("col").pushString("setTransform").op(APT_OP_EA_CALLMETHODPOP);
		callMethod(a, "col", "getTransform");
	});
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()), r.m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	CHECK(noteDetails(fx, "movieclip-native-unread")
		  == std::vector<std::string>{ "createEmptyMovieClip", "attachMovie", "getBounds", "localToGlobal", "swapDepths", "getDepth", "removeMovieClip", "Color",
			  "Color.setRGB", "Color.getRGB", "Color.setTransform", "Color.getTransform" });
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-106: creating an interval reports the id and first-call assumption each time")
{
	TestMovie m;
	m.setRootFrames({ { m.addActionItem(spawnProgram(m)) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.apt->noteCount("setInterval-id-and-first-call") == 1);
	fx.step(2); // the first timer fires and creates a second one, then two fire and create two more
	CHECK(fx.apt->noteCount("setInterval-id-and-first-call") == 4);
	CHECK(fx.errorsText().empty());
}

TEST_CASE("stops S-108: the Key and Mouse natives and the key constants report themselves when used")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushString("l").pushByte(0).op(APT_OP_INITOBJECT).op(APT_OP_DEFINELOCAL);
		a.getStringVar("l").pushByte(1).getStringVar("Key").pushString("addListener").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("l").pushByte(1).getStringVar("Key").pushString("removeListener").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("l").pushByte(1).getStringVar("Mouse").pushString("addListener").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("l").pushByte(1).getStringVar("Mouse").pushString("removeListener").op(APT_OP_EA_CALLMETHODPOP);
		callMethod(a, "Key", "isDown", {}, { 37 });
		callMethod(a, "Key", "getCode");
		a.getStringVar("Key").pushString("LEFT").op(APT_OP_GETMEMBER).op(APT_OP_POP);
		a.getStringVar("Key").pushString("ESCAPE").op(APT_OP_GETMEMBER).op(APT_OP_POP);
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(noteDetails(fx, "key-mouse-native-unread")
		  == std::vector<std::string>{ "Key.addListener", "Key.removeListener", "Mouse.addListener", "Mouse.removeListener", "Key.isDown", "Key.getCode", "Key.LEFT",
			  "Key.ESCAPE" });
	CHECK(fx.errorsText().empty());
}

// ---- S-102 census (static, retail) --------------------------------------------------------------------------------------------

TEST_CASE("retail census S-102: every placement of a character over an occupied depth in a timeline, in linear file order, is shape over shape")
{
	// The 13 swaps the player executed in the 100-frame corpus runs (12 in timeline, 1 in saveload) are a subset of the static
	// census: scanning every root and sprite timeline frame by frame in file order (a RemoveObject frees its depth, a place with a
	// character over an occupied depth is one record) finds 62 records in 21 movies, all of them shape over shape.
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::size_t records = 0, shapeOverShape = 0;
	std::set<std::string> movies;
	for (const std::string &name : source.listMovies())
	{
		std::string err;
		std::shared_ptr<const AptFile> f = loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		auto scan = [&](const std::vector<AptFrame> &frames) {
			std::map<std::int32_t, std::uint32_t> occupant; // depth -> character id
			for (const AptFrame &frame : frames)
			{
				for (const AptFrameItem &item : frame.items)
				{
					if (item.type == APT_ITEM_REMOVEOBJECT)
					{
						occupant.erase(item.removeDepth);
					}
					else if (item.type == APT_ITEM_PLACEOBJECT && (item.place->flags & APT_PLACE_HASCHARACTER))
					{
						auto it = occupant.find(item.place->depth);
						const std::uint32_t id = (std::uint32_t)item.place->characterId;
						if (it != occupant.end())
						{
							REQUIRE(id < f->characters.size());
							REQUIRE(it->second < f->characters.size());
							++records;
							movies.insert(AptPropertyMap::foldKey(name));
							shapeOverShape += (f->characters[id].type == APT_CHAR_SHAPE && f->characters[it->second].type == APT_CHAR_SHAPE) ? 1 : 0;
						}
						occupant[item.place->depth] = id;
					}
				}
			}
		};
		scan(f->frames);
		for (const AptCharacter &ch : f->characters)
		{
			if (ch.type == APT_CHAR_SPRITE)
			{
				scan(ch.frames);
			}
		}
	}
	CHECK(records == 62);
	CHECK(shapeOverShape == records);
	CHECK(movies
		  == std::set<std::string>{ "cahclass", "cahnewfeatures", "connections", "ingamechat", "lanlobby", "mainmenu", "messenger", "mpgamesetup", "onlineshell",
			  "palantir", "playertribute", "saveload", "skirmishstrategic", "stats", "strategicarmyunitswapper", "strategicbattleprompt", "strategicdetailsarmyretinue",
			  "strategicpalantir", "strategicplayerstatus", "strategicregionaward", "timeline" });
}

// ---- round 2: swapDepths validates before it mutates ---------------------------------------------------------------------------

namespace
{
AptValue callNative(PlayerFx &fx, AptCharacterInst *self, const char *name, const std::vector<AptValue> &args)
{
	AptValue fn;
	REQUIRE(self->getMember(name, fn));
	return fx.apt->vm().callFunction(fn, AptValue::object(self), args);
}

// every clip of the tree with its parent and depth, to prove that nothing moved
std::string treeShape(AptCharacterInst *c)
{
	std::string s = c->instName() + "@" + std::to_string(c->depth()) + (c->defined() ? "" : "!dead");
	if (AptSpriteInst *sp = c->asSprite())
	{
		s += "(";
		for (AptCharacterInst *k : sp->children())
		{
			REQUIRE(k->parent() == sp);
			s += treeShape(k) + ",";
		}
		s += ")";
	}
	return s;
}
} // namespace

TEST_CASE("review r2: swapDepths with a root, ancestor, descendant, foreign-parent or non-clip target is reported and changes nothing")
{
	ClipRoot r;
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	AptCharacterInst *c = fx.at("c");
	AptSpriteInst *e = fx.root()->createEmptyClip("e", 5 + 0x4000);
	AptSpriteInst *inner = e->createEmptyClip("inner", 1 + 0x4000);
	REQUIRE((c && e && inner));
	const std::string before = treeShape(fx.root());
	const std::size_t errorsBefore = fx.host.errors.size();
	CHECK(callNative(fx, inner, "swapDepths", { AptValue::object(e) }).isUndefined());              // ancestor (its parent)
	CHECK(callNative(fx, inner, "swapDepths", { AptValue::object(fx.root()) }).isUndefined());      // the level root
	CHECK(callNative(fx, e, "swapDepths", { AptValue::object(inner) }).isUndefined());              // descendant
	CHECK(callNative(fx, inner, "swapDepths", { AptValue::object(c) }).isUndefined());              // foreign parent
	CHECK(callNative(fx, c, "swapDepths", { AptValue::object(fx.root()) }).isUndefined());          // _root: the old parent cycle
	CHECK(callNative(fx, c, "swapDepths", { AptValue::object(fx.apt->vm().newObject()) }).isUndefined()); // not a clip
	CHECK(fx.host.errors.size() == errorsBefore + 6);
	CHECK(countOf(fx.errorsText(), "swapDepths: the target is not a live sibling clip") == 6);
	CHECK(treeShape(fx.root()) == before);
	CHECK(fx.root()->parent() == nullptr);
	// the display tree is still a tree: matrices compose without looping
	(void)c->globalMatrix();
	(void)inner->globalMatrix();
}

TEST_CASE("review r2: swapDepths through a retained reference to a removed clip does not put it back")
{
	ClipRoot r;
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	AptSpriteInst *e = fx.root()->createEmptyClip("e", 5 + 0x4000);
	fx.root()->removeObject(5 + 0x4000);
	REQUIRE_FALSE(e->defined());
	const std::string before = treeShape(fx.root());
	CHECK(callNative(fx, e, "swapDepths", { AptValue::integer(9) }).isUndefined());
	CHECK(callNative(fx, e, "swapDepths", { AptValue::object(fx.at("c")) }).isUndefined());
	CHECK(countOf(fx.errorsText(), "swapDepths: the receiver is not a live clip of its parent (a stale reference)") == 2);
	CHECK(treeShape(fx.root()) == before);
	CHECK(fx.root()->childAtDepth(9 + 0x4000) == nullptr);
	// a live receiver still swaps with a live sibling, by object and by depth
	AptSpriteInst *a = fx.root()->createEmptyClip("a", 1 + 0x4000);
	AptSpriteInst *b = fx.root()->createEmptyClip("b", 2 + 0x4000);
	callNative(fx, a, "swapDepths", { AptValue::object(b) });
	CHECK(a->depth() == 2 + 0x4000);
	CHECK(b->depth() == 1 + 0x4000);
	callNative(fx, a, "swapDepths", { AptValue::integer(7) });
	CHECK(a->depth() == 7 + 0x4000);
	CHECK(fx.root()->childAtDepth(7 + 0x4000) == a);
}

// ---- round 2: a depth refilled by an Unload callback is not filled twice -------------------------------------------------------------

TEST_CASE("review r2: createEmptyMovieClip over an occupant whose Unload refills the depth declines and reports")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushByte(3).pushString("old").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("old").pushString("onUnload");
		a.defineFunction("", {}, [&](Asm &f) {
			f.pushByte(3).pushString("nested").pushByte(2).getStringVar("this").pushString("_parent").op(APT_OP_GETMEMBER).pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		});
		a.op(APT_OP_SETMEMBER);
		a.pushByte(3).pushString("next").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		fscmd(a, "done");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "done");
	CHECK(childrenAtDepth(fx.root(), 3 + 0x4000) == 1);
	REQUIRE(fx.at("nested"));
	CHECK(fx.root()->childAtDepth(3 + 0x4000) == fx.at("nested"));
	CHECK(fx.at("next") == nullptr);
	CHECK(fx.at("old") == nullptr);
	CHECK(countOf(fx.errorsText(), "createEmptyMovieClip: an Unload callback refilled depth 3; nothing was inserted") == 1);
	CHECK(noteDetails(fx, "script-depth-reentrant") == std::vector<std::string>{ "an Unload callback refilled depth 3" });
	CHECK(noteDetails(fx, "script-depth-occupied") == std::vector<std::string>{ "depth 3" }); // `next` over `old`; the nested clip went into the emptied depth
}

TEST_CASE("review r2: duplicateMovieClip and attachMovie over an occupant whose Unload refills the depth decline and report")
{
	ClipRoot r;
	std::uint32_t go = program(r.m, [&](Asm &a) {
		callMethod(a, "c", "duplicateMovieClip", { "old" }, { 3 });
		a.getStringVar("old").pushString("onUnload");
		a.defineFunction("", {}, [&](Asm &f) {
			f.pushByte(3).pushString("nested").pushByte(2).getStringVar("this").pushString("_parent").op(APT_OP_GETMEMBER).pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		});
		a.op(APT_OP_SETMEMBER);
		callMethod(a, "c", "duplicateMovieClip", { "next" }, { 3 });
	});
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()), r.m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	CHECK(childrenAtDepth(fx.root(), 3 + 0x4000) == 1);
	CHECK(fx.root()->childAtDepth(3 + 0x4000) == fx.at("nested"));
	CHECK(fx.at("next") == nullptr);
	CHECK(countOf(fx.errorsText(), "attachMovie / duplicateMovieClip: an Unload callback refilled depth 3; nothing was inserted") == 1);
	CHECK(noteDetails(fx, "script-depth-reentrant") == std::vector<std::string>{ "an Unload callback refilled depth 3" });
}

TEST_CASE("review r2: an Unload callback that destroys the parent while its occupant is replaced declines the insertion")
{
	TestMovie m;
	std::uint32_t go = program(m, [&](Asm &a) {
		a.pushByte(1).pushString("p").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushByte(3).pushString("old").pushByte(2).getStringVar("p").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.getStringVar("p").pushString("old").op(APT_OP_GETMEMBER).pushString("onUnload");
		a.defineFunction("", {}, [&](Asm &f) {
			f.pushByte(0).getStringVar("this").pushString("_parent").op(APT_OP_GETMEMBER).pushString("removeMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		});
		a.op(APT_OP_SETMEMBER);
		a.pushByte(3).pushString("next").pushByte(2).getStringVar("p").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		fscmd(a, "done");
	});
	m.setRootFrames({ { m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	CHECK(fx.commandsText() == "done");
	CHECK(fx.at("p") == nullptr);
	CHECK(countOf(fx.errorsText(), "createEmptyMovieClip: the clip was destroyed while the occupant of the depth was removed; nothing was inserted") == 1);
	CHECK(noteDetails(fx, "script-depth-reentrant") == std::vector<std::string>{ "the parent was destroyed by an Unload callback" });
}

// ---- round 2: script depth arithmetic is checked ------------------------------------------------------------------------------------

TEST_CASE("review r2: a script depth whose display-list depth does not fit 32 bits is reported and declined by every native; the boundaries are exact")
{
	// The binary's natives add 0x4000 in 32 bits; what they store for an overflowing request (a wrap) was not read, so the request is
	// declined and registered (S-104) instead of invoking signed-overflow undefined behaviour.
	ClipRoot r;
	const std::int32_t kMax = 2147483647;
	std::uint32_t go = program(r.m, [&](Asm &a) {
		a.pushLong(kMax).pushString("o1").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushLong(kMax).pushString("o2").pushString("Gizmo").pushByte(3).getStringVar("this").pushString("attachMovie").op(APT_OP_EA_CALLMETHODPOP);
		a.pushLong(kMax).pushString("o3").pushByte(2).getStringVar("c").pushString("duplicateMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushLong(kMax).pushByte(1).getStringVar("c").pushString("swapDepths").op(APT_OP_EA_CALLMETHODPOP);
		// the boundary: kMax - 0x4000 is the largest script depth, kMax - 0x4000 + 1 the first rejected one
		a.pushLong(kMax - 0x4000).pushString("edge").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		a.pushLong(kMax - 0x4000 + 1).pushString("past").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		// the most negative depth is representable
		a.pushLong((std::int32_t)(-2147483647 - 1)).pushString("low").pushByte(2).getStringVar("this").pushString("createEmptyMovieClip").op(APT_OP_EA_CALLMETHODPOP);
		fscmd(a, "done");
	});
	r.m.setRootFrames({ { r.m.addPlaceItem(r.placeC()), r.m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", r.m));
	CHECK(fx.commandsText() == "done");
	for (const char *n : { "o1", "o2", "o3", "past" })
	{
		CHECK_MESSAGE(fx.at(n) == nullptr, n);
	}
	AptCharacterInst *edge = fx.at("edge");
	REQUIRE(edge);
	CHECK(edge->depth() == kMax);
	AptCharacterInst *low = fx.at("low");
	REQUIRE(low);
	CHECK(low->depth() == (std::int32_t)(-2147483647 - 1) + 0x4000);
	CHECK(fx.at("c")->depth() == 1); // swapDepths(kMax) did nothing
	CHECK(noteDetails(fx, "script-depth-out-of-range")
		  == std::vector<std::string>{ "createEmptyMovieClip: 2147483647", "attachMovie: 2147483647", "duplicateMovieClip: 2147483647", "swapDepths: 2147483647",
			  "createEmptyMovieClip: 2147467264" });
	CHECK(countOf(fx.errorsText(), "does not fit a 32-bit display-list depth; nothing was changed") == 5);
	// the reverse conversion is defined for every depth
	CHECK(Apt::scriptDepthOf(kMax) == kMax - 0x4000);
	CHECK(Apt::scriptDepthOf(5) == 5 - 0x4000);
	CHECK(Apt::scriptDepthOf((std::int32_t)(-2147483647 - 1)) == (std::int32_t)((std::uint32_t)0x80000000u - 0x4000u));
}
