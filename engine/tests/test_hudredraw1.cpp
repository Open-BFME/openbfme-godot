// OpenBFME unit tests. GPL-3.0.
// Lane HUDREDRAW-1: the device draws each canvas segment (an op, or a mask bracket) under an item of its own and draws again only the segments whose ops
// changed, matched level by level (AptCanvasSegments / AptCanvasPlanRedraw / AptCanvasSameOps, GameClient/AptCanvas.h); a level's ops never merge into
// another's (BuildAptCanvas); notes a movie makes every frame are kept once per kind and detail (Apt::noteOnce, WindowManager::noteOnce), so the lists stay
// bounded over a long game.

#include "doctest.h"
#include "AptPlayerTestUtil.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace apttest;

namespace
{

AptCanvasOp opAt(int level, AptCanvasOp::Kind kind = AptCanvasOp::Kind::Mesh)
{
	AptCanvasOp op;
	op.level = level;
	op.kind = kind;
	op.path = "_level" + std::to_string(level) + ".x";
	return op;
}

std::vector<std::string> segmentsText(const AptCanvasList &list)
{
	std::vector<std::string> out;
	for (const AptCanvasSegment &r : AptCanvasSegments(list))
	{
		out.push_back(std::to_string(r.firstLevel) + "-" + std::to_string(r.lastLevel) + ":" + std::to_string(r.begin) + "-" + std::to_string(r.end));
	}
	return out;
}

// the plan as "<old index>=" (kept) / "<old index>*" (drawn again over it) / "new" per segment, or "full"
std::string planText(const AptCanvasList &now, const AptCanvasList &drawn)
{
	const AptCanvasRedrawPlan plan = AptCanvasPlanRedraw(now, AptCanvasSegments(now), drawn, AptCanvasSegments(drawn));
	if (plan.full)
	{
		return "full";
	}
	std::string out;
	for (size_t j = 0; j < plan.old.size(); ++j)
	{
		out += (out.empty() ? "" : " ") + (plan.old[j] < 0 ? std::string("new") : std::to_string(plan.old[j]) + (plan.same[j] ? "=" : "*"));
	}
	return out;
}

std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace

TEST_CASE("hudredraw1: a canvas list's segments are its ops one by one, a mask bracket with what it clips as one")
{
	using K = AptCanvasOp::Kind;
	AptCanvasList list;
	CHECK(segmentsText(list).empty());
	list.ops = { opAt(0), opAt(0), opAt(3), opAt(3, K::Text), opAt(7) };
	CHECK(segmentsText(list) == std::vector<std::string>{ "0-0:0-1", "0-0:1-2", "3-3:2-3", "3-3:3-4", "7-7:4-5" });
	// a balanced mask: MaskBegin up to its MaskEnd (nested ones inside it) is one segment
	list.ops = { opAt(0), opAt(0, K::MaskBegin), opAt(0), opAt(0, K::MaskContent), opAt(0, K::MaskBegin), opAt(0, K::MaskContent), opAt(0), opAt(0, K::MaskEnd),
		opAt(0, K::MaskEnd), opAt(1) };
	CHECK(segmentsText(list) == std::vector<std::string>{ "0-0:0-1", "0-0:1-9", "1-1:9-10" });
	// a mask still open at the level's end (an unbalanced list: the device nests the next level's ops under it) runs on until it closes
	list.ops = { opAt(0, K::MaskBegin), opAt(0), opAt(0, K::MaskContent), opAt(2), opAt(2, K::MaskEnd), opAt(2), opAt(5) };
	CHECK(segmentsText(list) == std::vector<std::string>{ "0-2:0-5", "2-2:5-6", "5-5:6-7" });
	// a mask end with none open is ignored (drawCanvas does not pop the segment's own item): it is a segment of its own
	list.ops = { opAt(0, K::MaskEnd), opAt(0, K::MaskBegin), opAt(1), opAt(2) };
	CHECK(segmentsText(list) == std::vector<std::string>{ "0-0:0-1", "0-2:1-4" });
}

TEST_CASE("hudredraw1: the redraw plan keeps the segments that draw the same, matched level by level")
{
	using K = AptCanvasOp::Kind;
	AptCanvasList drawn;
	drawn.ops = { opAt(1), opAt(1, K::Text), opAt(1), opAt(2), opAt(2) };
	drawn.ops[1].text = "45";
	AptCanvasList now = drawn;
	CHECK(planText(now, drawn) == "0= 1= 2= 3= 4=");
	now.ops[1].text = "46"; // one text of level 1
	now.ops[4].colors = { 0.5f }; // one op of level 2
	CHECK(planText(now, drawn) == "0= 1* 2= 3= 4*");
	// a level with another segment count: its segments are all drawn anew, the other levels keep theirs
	now = drawn;
	now.ops.insert(now.ops.begin() + 4, opAt(2));
	CHECK(planText(now, drawn) == "0= 1= 2= new new new");
	now = drawn;
	now.ops.erase(now.ops.begin() + 1);
	CHECK(planText(now, drawn) == "new new 3= 4=");
	// another level, a level more or less, or the levels in another order: everything
	now = drawn;
	now.ops.push_back(opAt(4));
	CHECK(planText(now, drawn) == "full");
	now = drawn;
	now.ops.erase(now.ops.begin() + 3, now.ops.end());
	CHECK(planText(now, drawn) == "full");
	now = drawn;
	for (AptCanvasOp &op : now.ops)
	{
		op.level = op.level == 1 ? 2 : 1;
	}
	CHECK(planText(now, drawn) == "full");
	// a segment that now runs into another level (a mask left open) is drawn anew, as is the one it replaces
	drawn.ops = { opAt(1, K::MaskBegin), opAt(1, K::MaskContent), opAt(1, K::MaskEnd), opAt(2) };
	now.ops = { opAt(1, K::MaskBegin), opAt(1, K::MaskContent), opAt(2), opAt(2, K::MaskEnd) };
	CHECK(planText(now, drawn) == "full"); // level 2 starts no segment of its own now
	now.ops.push_back(opAt(2));
	CHECK(planText(now, drawn) == "new 1=");
}

TEST_CASE("hudredraw1: removed decorative segments and equal-count replacements invalidate only their level")
{
	using K = AptCanvasOp::Kind;
	AptCanvasList drawn;
	drawn.ops = { opAt(1), opAt(3), opAt(3, K::MaskBegin), opAt(3, K::MaskContent), opAt(3, K::MaskEnd), opAt(3, K::Placeholder), opAt(4) };
	drawn.ops[1].path = "CommandButtons.Command1.content.ring";
	drawn.ops[3].path = "CommandButtons.Command1.content.AutoAbilityOverlay";
	drawn.ops[5].path = "CommandButtons.Command1.content.TimerOverlay";
	AptCanvasList now;
	now.ops = { opAt(1), opAt(3), opAt(4) };
	now.ops[1].path = "CommandButtons.Command1._static";
	const AptCanvasRedrawPlan removed = AptCanvasPlanRedraw(now, AptCanvasSegments(now), drawn, AptCanvasSegments(drawn));
	REQUIRE_FALSE(removed.full);
	REQUIRE(removed.old.size() == 3);
	CHECK(removed.same[0]);
	CHECK(removed.old[1] == -1);
	CHECK_FALSE(removed.same[1]);
	CHECK(removed.old[2] == 4);
	CHECK(removed.same[2]);

	AptCanvasList replacement = drawn;
	replacement.ops[1].path = "CommandButtons.Command1.content";
	const AptCanvasRedrawPlan replaced = AptCanvasPlanRedraw(replacement, AptCanvasSegments(replacement), drawn, AptCanvasSegments(drawn));
	REQUIRE_FALSE(replaced.full);
	REQUIRE(replaced.old.size() == 5);
	CHECK(replaced.same[0]);
	CHECK(replaced.old[1] == 1);
	CHECK_FALSE(replaced.same[1]);
	CHECK(replaced.same[2]);
	CHECK(replaced.same[3]);
	CHECK(replaced.same[4]);

	const std::vector<AptCanvasSegment> emptySegments = AptCanvasSegments(now);
	const AptCanvasRedrawPlan stable = AptCanvasPlanRedraw(now, emptySegments, now, emptySegments);
	REQUIRE_FALSE(stable.full);
	CHECK(stable.old == std::vector<std::ptrdiff_t>{ 0, 1, 2 });
	CHECK(stable.same == std::vector<bool>{ true, true, true });
}

TEST_CASE("hudredraw1: two op ranges draw the same only when every op is the same, bit for bit")
{
	AptCanvasList a, b;
	a.ops = { opAt(0), opAt(4), opAt(4) };
	a.ops[1].positions = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
	a.ops[2].text = "100";
	b = a;
	CHECK(AptCanvasSameOps(a, 1, 3, b, 1, 3));
	CHECK(AptCanvasSameOps(a, 0, 1, b, 0, 1));
	CHECK_FALSE(AptCanvasSameOps(a, 1, 3, b, 1, 2)); // a different count
	b.ops[2].text = "105";
	CHECK_FALSE(AptCanvasSameOps(a, 1, 3, b, 1, 3));
	CHECK(AptCanvasSameOps(a, 0, 1, b, 0, 1)); // the other level's range still draws the same
	b = a;
	b.ops[1].positions[5] = std::nextafter(6.0f, 7.0f); // one vertex moved by one float step
	CHECK_FALSE(AptCanvasSameOps(a, 1, 3, b, 1, 3));
	b = a;
	b.ops[1].level = 5; // the same drawing on another level is another range's
	CHECK_FALSE(AptCanvasSameOp(a.ops[1], b.ops[1]));
	// the fields the device reads (one of each kind of field)
	b = a;
	b.ops[2].textColor[3] = 0.5f;
	CHECK_FALSE(AptCanvasSameOp(a.ops[2], b.ops[2]));
	b = a;
	b.ops[2].nativeVars = { { "_imageMap", "x" } };
	CHECK_FALSE(AptCanvasSameOp(a.ops[2], b.ops[2]));
	b = a;
	b.ops[2].matrix.tx = 1.0f;
	CHECK_FALSE(AptCanvasSameOp(a.ops[2], b.ops[2]));
	// Sol r1: a native component's cumulative colour (the HUD hook tints its image with it: a fade-in) is part of what it draws
	b = a;
	b.ops[2].kind = AptCanvasOp::Kind::Placeholder;
	AptCanvasOp faded = b.ops[2];
	faded.placeholderColor[3] = 0.4f;
	CHECK_FALSE(AptCanvasSameOp(b.ops[2], faded));
}

TEST_CASE("hudredraw1: BuildAptCanvas stamps each op with its level and never merges the same fill across two levels")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")), m.addPlaceItem(placeChar(shapeId, 2, "t")) } });
	PlayerFx fx;
	fx.source.files[MemorySource::lower("A_geometry/1.ru")] = bytesOf("s s:100:200:50:255\nt 0:0:10:0:10:10\nc\n");
	REQUIRE(fx.load(0, "A", m));
	REQUIRE(fx.load(3, "A", m));
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 4);
	AptTextureStore textures(fx.source);
	GameTextTable strings;
	AptCanvasInputs in;
	in.mapping.windowW = 1024;
	in.mapping.windowH = 768;
	in.textures = &textures;
	in.text = &strings;
	AptCanvasList out;
	BuildAptCanvas(rl, in, out, true);
	// the two placements of a level share their state and merge (one op per level); the levels stay apart
	REQUIRE(out.ops.size() == 2);
	CHECK(out.ops[0].level == 0);
	CHECK(out.ops[1].level == 3);
	CHECK(out.ops[0].triangleCount() == 2);
	CHECK(out.ops[1].triangleCount() == 2);
	CHECK(segmentsText(out) == std::vector<std::string>{ "0-0:0-1", "3-3:1-2" });
	AptCanvasList plain;
	BuildAptCanvas(rl, in, plain, false);
	REQUIRE(plain.ops.size() == 4);
	CHECK(plain.ops[1].level == 0);
	CHECK(plain.ops[2].level == 3);
}

TEST_CASE("hudredraw1: the fills of one shape on a level above 0 merge as on level 0 (Sol r1: the level is set as each op is made)")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	PlayerFx fx;
	// two fill styles of the same state (untextured): two triangle runs of one shape command
	fx.source.files[MemorySource::lower("A_geometry/1.ru")] = bytesOf("s s:100:200:50:255\nt 0:0:10:0:10:10\ns s:10:20:30:255\nt 0:0:10:10:0:10\nc\n");
	REQUIRE(fx.load(0, "A", m));
	REQUIRE(fx.load(4, "A", m));
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 2);
	AptTextureStore textures(fx.source);
	GameTextTable strings;
	AptCanvasInputs in;
	in.textures = &textures;
	in.text = &strings;
	AptCanvasList out;
	BuildAptCanvas(rl, in, out, true);
	REQUIRE(out.ops.size() == 2); // one op per level: both fills of each shape in it
	CHECK(out.ops[0].level == 0);
	CHECK(out.ops[1].level == 4);
	CHECK(out.ops[0].triangleCount() == 2);
	CHECK(out.ops[1].triangleCount() == 2);
}

TEST_CASE("hudredraw1: 1000 duplicateMovieClip calls note the unread copy rule once (S-104), a list that no longer grows each HUD frame")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1)) } });
	std::uint32_t id = m.addCharacter(sprite);
	std::uint32_t go = program(m, [&](Asm &a) {
		for (int i = 0; i < 1000; ++i)
		{
			callMethod(a, "c", "duplicateMovieClip", { "d" + std::to_string(i) }, { 3 + i });
		}
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "c")), m.addActionItem(go) } });
	PlayerFx fx;
	REQUIRE(fx.load(0, "A", m));
	REQUIRE(fx.at("d999"));
	CHECK(fx.at("d999")->depth() == 1002 + 0x4000);
	CHECK(fx.apt->noteCount("duplicate-copies-placement-only") == 1);
	std::size_t copies = 0;
	for (const AptNote &n : fx.apt->notes())
	{
		copies += n.kind == "duplicate-copies-placement-only" ? 1 : 0;
	}
	CHECK(copies == 1);
	// noteOnce keeps one entry per kind AND detail: another detail is a note of its own
	fx.apt->noteOnce("duplicate-copies-placement-only", "other");
	fx.apt->noteOnce("duplicate-copies-placement-only", "other");
	CHECK(fx.apt->noteCount("duplicate-copies-placement-only") == 2);
}

TEST_CASE("hudredraw1: the window manager notes an unhandled command, extern read / write and its stand-in random once per name, not per call")
{
	MemorySource source;
	RecordingShellServices services;
	WindowManager wm(source, services);
	const std::size_t before = wm.notes().size();
	std::string value = "x";
	for (int i = 0; i < 1000; ++i)
	{
		CHECK(wm.getExtern("NoSuchProvider", value) == AptExternResult::NoProvider);
		CHECK_FALSE(wm.setExtern("NoSuchProvider", "1"));
		CHECK_FALSE(wm.invokeCallback("NoSuchCommand", "arg"));
		wm.random();
	}
	CHECK(wm.noteCount("extern-read-unhandled") == 1);
	CHECK(wm.noteCount("extern-write-unhandled") == 1);
	CHECK(wm.noteCount("command-unhandled") == 1);
	CHECK(wm.noteCount("apt-random-unwired") == 1);
	CHECK(wm.notes().size() == before + 4);
	// another name is noted on its own
	CHECK(wm.getExtern("OtherProvider", value) == AptExternResult::NoProvider);
	CHECK_FALSE(wm.invokeCallback("NoSuchCommand", "other arg"));
	CHECK(wm.noteCount("extern-read-unhandled") == 2);
	CHECK(wm.noteCount("command-unhandled") == 2);
}
