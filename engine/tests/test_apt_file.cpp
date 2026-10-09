// OpenBFME unit tests: Apt .const/.apt/.dat/.ru parsing and the bytecode decoder (spec step A0).
// Synthetic movies only; the retail corpus lives in test_apt_corpus.cpp.  GPL-3.0.

#include "doctest.h"
#include "AptTestUtil.h"

#include <algorithm>
#include <array>
#include <set>

using namespace apttest;

namespace
{

// The opcode census of the RotWK corpus: spec workspace/rebuild/specs/menus-apt.md section 2.4
// (86 distinct values).
const std::uint8_t kCensus[] = { 0x00, 0x04, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x12, 0x13, 0x17, 0x18, 0x1C, 0x1D, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x30,
	0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x55, 0x59, 0x5A,
	0x5B, 0x5D, 0x5E, 0x60, 0x64, 0x67, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x81, 0x83, 0x87, 0x88, 0x8C, 0x8E, 0x96, 0x99, 0x9A, 0x9B, 0x9D, 0x9F,
	0xA1, 0xA2, 0xA4, 0xA5, 0xA6, 0xA7, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7 };

std::shared_ptr<const AptCodeBlock> decodeOne(TestMovie &m, Asm &a, std::string *err = nullptr)
{
	AptFile f;
	std::string e;
	std::uint32_t off = m.commit(a);
	m.setRootFrames({ { m.addActionItem(off) } });
	REQUIRE_MESSAGE(m.parse(f, &e), e);
	std::shared_ptr<const AptCodeBlock> b = f.codeAt(off, &e);
	if (err)
	{
		*err = e;
	}
	return b;
}

} // namespace

// ---- .const -------------------------------------------------------------------------------

TEST_CASE("const file: typed records parse; string offsets are into the .const file")
{
	TestMovie m;
	m.constString("hello");
	m.constRegister(3);
	m.constInt(-5);
	m.constFloat(2.5f);
	m.constBool(true);
	m.constNone();
	std::vector<std::uint8_t> a, c;
	m.build(a, c);
	AptConstFile cf;
	std::string err;
	REQUIRE_MESSAGE(AptConstFile::parse(c, cf, &err), err);
	REQUIRE(cf.entries.size() == 6);
	CHECK(cf.entries[0].type == APT_CONST_STRING);
	CHECK(cf.entries[0].text == "hello");
	CHECK(cf.entries[1].type == APT_CONST_REGISTER);
	CHECK(cf.entries[1].raw == 3);
	CHECK((std::int32_t)cf.entries[2].raw == -5);
	CHECK(cf.entries[3].type == APT_CONST_FLOAT);
	CHECK(cf.entries[4].raw == 1);
	CHECK(cf.entries[5].type == APT_CONST_NONE);
}

TEST_CASE("const file: malformed input is rejected loudly")
{
	TestMovie m;
	m.constString("x");
	std::vector<std::uint8_t> a, c;
	m.build(a, c);
	AptConstFile cf;
	std::string err;

	std::vector<std::uint8_t> bad = c;
	bad[0] = 'X';
	CHECK_FALSE(AptConstFile::parse(bad, cf, &err));
	CHECK(err.find("magic") != std::string::npos);

	bad = c;
	bad[28] = 31; // header size must be 32
	CHECK_FALSE(AptConstFile::parse(bad, cf, &err));
	CHECK(err.find("header size") != std::string::npos);

	bad = c;
	bad.resize(36); // record table truncated
	CHECK_FALSE(AptConstFile::parse(bad, cf, &err));

	bad = c;
	bad[32] = 9; // unknown type
	CHECK_FALSE(AptConstFile::parse(bad, cf, &err));
	CHECK(err.find("unknown type") != std::string::npos);

	bad = c;
	bad[36] = 0xFF; // string offset out of range
	bad[37] = 0xFF;
	CHECK_FALSE(AptConstFile::parse(bad, cf, &err));

	TestMovie m2;
	m2.constTyped(APT_CONST_BOOLEAN, 2);
	m2.build(a, c);
	CHECK_FALSE(AptConstFile::parse(c, cf, &err));
	TestMovie m3;
	m3.constTyped(APT_CONST_NONE, 1);
	m3.build(a, c);
	CHECK_FALSE(AptConstFile::parse(c, cf, &err));
}

// ---- .apt ---------------------------------------------------------------------------------

TEST_CASE("apt file: movie header, characters, frames and items round-trip")
{
	TestMovie m(6);
	m.setSize(1024, 768, 33);
	std::uint32_t shapeRec = m.addShape(1, 2, 3, 4, 77);
	std::uint32_t imgRec = m.addImage(5);
	std::uint32_t fontRec = m.addFont("Albertus MT", { 7, 8 });
	std::uint32_t textRec = m.addEditText("$HELLO", "_timer", 1, 14.0f);
	std::uint32_t code = 0;
	{
		Asm a = m.program();
		a.op(APT_OP_STOP).op(APT_OP_END);
		code = m.commit(a);
	}
	std::uint32_t btnRec = m.addButton({ { 1, 1 }, { 2, 1 } }, { { 0x01, 0, code }, { 0, 13, code } });
	TestMovie::Place pl;
	pl.flags = APT_PLACE_HASCHARACTER | APT_PLACE_HASNAME | APT_PLACE_HASMATRIX;
	pl.depth = 4;
	pl.characterId = 1;
	pl.name = "Child";
	pl.translation[0] = 10;
	std::uint32_t spriteRec = m.addSprite({ { m.addActionItem(code) }, { m.addLabelItem("_over", 0, 1), m.addPlaceItem(pl) } });
	m.addCharacter(0 /*placeholder for the root slot, patched below*/);
	m.addCharacter(shapeRec);
	m.addCharacter(imgRec);
	m.addCharacter(fontRec);
	m.addCharacter(textRec);
	m.addCharacter(btnRec);
	m.addCharacter(spriteRec);
	m.addCharacter(0); // import slot
	m.addImport("Lib", "Thing", 7);
	m.addExport("Exp", 6);
	m.setRootFrames({ { m.addActionItem(code), m.addBackgroundItem(1, 2, 3, 4) }, { m.addRemoveItem(9) } });
	AptFile f;
	std::string err;
	REQUIRE_MESSAGE(m.parse(f, &err), err);

	CHECK(f.version == 6);
	CHECK(f.width == 1024);
	CHECK(f.height == 768);
	CHECK(f.msPerFrame == 33);
	REQUIRE(f.characters.size() == 8);
	CHECK(f.characters[0].type == APT_CHAR_NULL); // slot 0 was left null by this test
	CHECK(f.characters[1].type == APT_CHAR_SHAPE);
	CHECK(f.characters[1].geometryId == 77);
	CHECK(f.characters[1].bounds[2] == 3.0f);
	CHECK(f.characters[2].textureId == 5);
	CHECK(f.characters[3].fontName == "Albertus MT");
	REQUIRE(f.characters[3].glyphs.size() == 2);
	CHECK(f.characters[3].glyphs[1] == 8);
	REQUIRE(f.characters[4].text);
	CHECK(f.characters[4].text->initialText == "$HELLO");
	CHECK(f.characters[4].text->variableName == "_timer");
	CHECK(f.characters[4].text->alignment == 1);
	CHECK(f.characters[4].text->fontHeight == 14.0f);
	CHECK(f.characters[4].text->readOnly);
	REQUIRE(f.characters[5].button);
	CHECK(f.characters[5].button->records.size() == 2);
	CHECK(f.characters[5].button->records[1].stateMask == 2);
	REQUIRE(f.characters[5].button->actions.size() == 2);
	CHECK(f.characters[5].button->actions[0].transitionMask == 0x01);
	CHECK(f.characters[5].button->actions[1].keyCode == 13); // key-only action: transition mask 0
	CHECK(f.characters[5].button->vertices.size() == 8);
	CHECK(f.characters[5].button->triangles.size() == 6);
	REQUIRE(f.characters[6].frames.size() == 2);
	CHECK(f.characters[6].frames[1].items[0].type == APT_ITEM_FRAMELABEL);
	CHECK(f.characters[6].frames[1].items[0].label == "_over");
	REQUIRE(f.characters[6].frames[1].items[1].place);
	CHECK(f.characters[6].frames[1].items[1].place->name == "Child");
	CHECK(f.characters[6].frames[1].items[1].place->depth == 4);
	CHECK(f.characters[6].frames[1].items[1].place->translation[0] == 10.0f);
	CHECK(f.characters[7].type == APT_CHAR_NULL);
	REQUIRE(f.imports.size() == 1);
	CHECK(f.imports[0].movie == "Lib");
	CHECK(f.imports[0].name == "Thing");
	CHECK(f.imports[0].characterId == 7);
	REQUIRE(f.frames.size() == 2);
	CHECK(f.frames[0].items[1].type == APT_ITEM_BACKGROUNDCOLOR);
	CHECK(f.frames[0].items[1].color[3] == 4);
	CHECK(f.frames[1].items[0].removeDepth == 9);
}

TEST_CASE("apt file: place object clip events, flagged-null clip actions and init actions")
{
	TestMovie m;
	Asm a = m.program();
	a.op(APT_OP_END);
	std::uint32_t code1 = m.commit(a);
	Asm b = m.program();
	b.op(APT_OP_STOP).op(APT_OP_END);
	std::uint32_t code2 = m.commit(b);

	TestMovie::Place p1;
	p1.flags = APT_PLACE_HASCHARACTER | APT_PLACE_HASCLIPACTION;
	p1.events = { { APT_CLIP_INITIALIZE, 0, code1 }, { APT_CLIP_KEYPRESS, 13, code2 } };
	TestMovie::Place p2;
	p2.flags = APT_PLACE_HASCHARACTER | APT_PLACE_HASCLIPACTION;
	p2.nullClipActions = true;
	std::uint32_t i1 = m.addPlaceItem(p1);
	std::uint32_t i2 = m.addPlaceItem(p2);
	std::uint32_t init = m.addInitActionItem(8, code2);
	m.setRootFrames({ { i1, i2, init, m.addActionItem(code1) } });
	AptFile f;
	std::string err;
	REQUIRE_MESSAGE(m.parse(f, &err), err);
	const AptFrame &fr = f.frames[0];
	REQUIRE(fr.items[0].place);
	REQUIRE(fr.items[0].place->clipEvents.size() == 2);
	CHECK(fr.items[0].place->clipEvents[1].keyCode == 13);
	CHECK(fr.items[0].place->clipEvents[1].codeOffset == code2);
	CHECK(fr.items[1].place->clipActionsFlagged);
	CHECK(fr.items[1].place->clipActionsNull);
	CHECK(fr.items[1].place->clipEvents.empty());
	CHECK(fr.items[2].type == APT_ITEM_INITACTION);
	CHECK(fr.items[2].spriteId == 8);        // the field the archived importer mistook for the code pointer
	CHECK(fr.items[2].codeOffset == code2);
	// distinct program offsets in discovery order
	std::vector<std::uint32_t> progs = f.programOffsets();
	REQUIRE(progs.size() == 2);
	CHECK(progs[0] == code1);
	CHECK(progs[1] == code2);
}

TEST_CASE("apt file: structural errors are reported")
{
	TestMovie m;
	m.setRootFrames({});
	std::vector<std::uint8_t> a, c;
	m.build(a, c);
	AptConstFile cf;
	std::string err;
	REQUIRE(AptConstFile::parse(c, cf, &err));
	AptFile f;
	auto parseBytes = [&](const std::vector<std::uint8_t> &bytes) {
		return AptFile::parse("T", std::make_shared<const std::vector<std::uint8_t>>(bytes), cf, f, &err);
	};
	REQUIRE(parseBytes(a));

	std::vector<std::uint8_t> bad = a;
	bad[0] = 'B';
	CHECK_FALSE(parseBytes(bad));
	CHECK(err.find("magic") != std::string::npos);

	bad = a;
	bad[9] = '5';
	CHECK_FALSE(parseBytes(bad));
	CHECK(err.find("version") != std::string::npos);

	bad = a;
	std::uint32_t entry = cf.aptDataEntryOffset;
	patch32(bad, entry + 4, 0xDEADBEEF); // root signature
	CHECK_FALSE(parseBytes(bad));

	bad = a;
	bad.resize(entry + 10); // truncated root
	CHECK_FALSE(parseBytes(bad));
}

TEST_CASE("apt file: export lookup is case-insensitive and flags conflicting duplicates")
{
	TestMovie m;
	std::uint32_t s = m.addShape(0, 0, 1, 1, 1);
	m.addCharacter(0);
	m.addCharacter(s);
	m.addCharacter(s);
	m.addExport("Button", 1);
	m.addExport("BUTTON", 1);  // same id: not ambiguous
	m.addExport("Other", 1);
	m.addExport("other", 2);   // different id: ambiguous
	AptFile f;
	std::string err;
	REQUIRE_MESSAGE(m.parse(f, &err), err);
	std::uint32_t id = 99;
	bool amb = true;
	REQUIRE(f.findExport("bUtToN", id, &amb));
	CHECK(id == 1);
	CHECK_FALSE(amb);
	REQUIRE(f.findExport("OTHER", id, &amb));
	CHECK(amb);
	CHECK_FALSE(f.findExport("missing", id, &amb));
}

// ---- decoder ------------------------------------------------------------------------------

TEST_CASE("decoder accepts exactly the 86-opcode census and rejects 0x1A and its neighbours loudly")
{
	const std::vector<std::uint8_t> &ops = AptActionDecoder::supportedOpcodes();
	std::vector<std::uint8_t> expected(std::begin(kCensus), std::end(kCensus));
	REQUIRE(expected.size() == 86);
	CHECK(ops == expected);
	for (int op = 0; op < 256; ++op)
	{
		CHECK(AptActionDecoder::isSupportedOpcode((std::uint8_t)op) == std::binary_search(expected.begin(), expected.end(), (std::uint8_t)op));
	}

	// Opcode 0x1A (SWF StringLess in older tag sets; not in the census) at offset 10 of a program.
	TestMovie m;
	Asm a = m.program();
	a.op(APT_OP_POP).op(0x1A).op(APT_OP_END);
	std::string err;
	std::shared_ptr<const AptCodeBlock> b = decodeOne(m, a, &err);
	CHECK_FALSE(b);
	CHECK(err.find("0x1a") != std::string::npos);
	CHECK(err.find("not supported") != std::string::npos);
}

TEST_CASE("decoder: operand widths, signedness and 4-byte alignment")
{
	TestMovie m;
	Asm a = m.program();
	a.pushByte(-3).pushShort(-300).pushLong(-70000).pushFloat(2.5f).pushString("abc").getStringVar("v").op(APT_OP_END);
	std::shared_ptr<const AptCodeBlock> b = decodeOne(m, a);
	REQUIRE(b);
	REQUIRE(b->instructions.size() == 7);
	CHECK(b->instructions[0].intOperand == -3);      // PushByte is signed (BFME1 Rva008CB260AppendToken.cpp: `char c`)
	CHECK(b->instructions[1].intOperand == -300);    // PushShort is signed (Bfme5PushShort8CB2A0.cpp: `short`)
	CHECK(b->instructions[2].intOperand == -70000);  // PushLong is a little-endian i32
	CHECK(b->instructions[3].floatOperand == 2.5f);
	CHECK(b->instructions[4].text == "abc");
	CHECK(b->instructions[4].nextOffset % 4 == 0);   // operand ends on a 4-byte boundary
	CHECK(b->instructions[5].text == "v");
}

TEST_CASE("decoder: nonzero alignment padding and a missing End are errors")
{
	TestMovie m;
	Asm a = m.program();
	a.op(APT_OP_POP).pushString("zz").op(APT_OP_END);
	std::string err;
	std::shared_ptr<const AptCodeBlock> good = decodeOne(m, a, &err);
	REQUIRE(good);
	// corrupt the pad byte after the PushString opcode
	{
		TestMovie m2;
		Asm a2 = m2.program();
		a2.op(APT_OP_POP).pushString("zz").op(APT_OP_END);
		std::uint32_t off = m2.commit(a2);
		m2.setRootFrames({ { m2.addActionItem(off) } });
		std::vector<std::uint8_t> apt, cst;
		m2.build(apt, cst);
		apt[off + 2] = 0x55; // PushString at off+1; opcode, then pad at off+2..off+3
		AptConstFile cf;
		REQUIRE(AptConstFile::parse(cst, cf, &err));
		AptFile f;
		REQUIRE_MESSAGE(AptFile::parse("T", std::make_shared<const std::vector<std::uint8_t>>(apt), cf, f, &err), err);
		CHECK_FALSE(f.codeAt(off, &err));
		CHECK(err.find("alignment") != std::string::npos);
	}
	// no End
	TestMovie m3;
	Asm a3 = m3.program();
	a3.op(APT_OP_POP).op(APT_OP_POP);
	std::shared_ptr<const AptCodeBlock> b3 = decodeOne(m3, a3, &err);
	// the stream runs into zero bytes, which decode as End, so the program ends at the first zero
	REQUIRE(b3);
	CHECK(b3->instructions.back().opcode == APT_OP_END);
}

TEST_CASE("decoder: PushData and ConstantPool resolve typed constants")
{
	TestMovie m;
	std::uint32_t cs = m.constString("name");
	std::uint32_t cr = m.constRegister(2);
	std::uint32_t ci = m.constInt(7);
	Asm a = m.program();
	a.constantPoolIdx({ cs }).pushDataIdx({ cr, ci, cs }).op(APT_OP_END);
	std::shared_ptr<const AptCodeBlock> b = decodeOne(m, a);
	REQUIRE(b);
	REQUIRE(b->instructions[0].constants.size() == 1);
	CHECK(b->instructions[0].constants[0].text == "name");
	REQUIRE(b->instructions[1].constants.size() == 3);
	CHECK(b->instructions[1].constants[0].type == APT_CONST_REGISTER);
	CHECK(b->instructions[1].constants[0].raw == 2);
	CHECK(b->instructions[1].constants[1].type == APT_CONST_INTEGER);
	CHECK(b->instructions[1].constants[2].text == "name");
}

TEST_CASE("decoder: branches resolve to instruction indices; a branch into the middle of an instruction is an error")
{
	TestMovie m;
	Asm a = m.program();
	a.branchIfTrue("skip").op(APT_OP_POP).label("skip").op(APT_OP_END);
	std::shared_ptr<const AptCodeBlock> b = decodeOne(m, a);
	REQUIRE(b);
	CHECK(b->instructions[0].branchTarget == 2);   // the End instruction
	TestMovie m2;
	Asm a2 = m2.program();
	a2.rawBranch(APT_OP_BRANCHALWAYS, 5).op(APT_OP_END).op(APT_OP_END); // lands past every instruction boundary
	std::string err;
	CHECK_FALSE(decodeOne(m2, a2, &err));
	CHECK(err.find("targets non-instruction") != std::string::npos);
	// a backward branch to the program start
	TestMovie m3;
	Asm a3 = m3.program();
	a3.label("top").op(APT_OP_POP).branchAlways("top").op(APT_OP_END);
	std::shared_ptr<const AptCodeBlock> b3 = decodeOne(m3, a3);
	REQUIRE(b3);
	CHECK(b3->instructions[1].branchTarget == 0);
}

TEST_CASE("decoder: DefineFunction and DefineFunction2 nest, report their parameters and count flat instructions")
{
	TestMovie m;
	Asm a = m.program();
	a.defineFunction("outer", { "p", "q" }, [](Asm &f) {
		f.op(APT_OP_POP);
		f.defineFunction2("", 4, APT_FN2_PRELOAD_ROOT | APT_FN2_SUPPRESS_THIS, { { 2, "x" } }, [](Asm &g) { g.op(APT_OP_PUSHDUPLICATE); });
	});
	a.op(APT_OP_END);
	std::shared_ptr<const AptCodeBlock> b = decodeOne(m, a);
	REQUIRE(b);
	REQUIRE(b->instructions.size() == 2);
	const AptFunctionDef *outer = b->instructions[0].function.get();
	REQUIRE(outer);
	CHECK(outer->name == "outer");
	CHECK_FALSE(outer->isV2);
	REQUIRE(outer->params.size() == 2);
	CHECK(outer->params[1].name == "q");
	REQUIRE(outer->body);
	REQUIRE(outer->body->instructions.size() == 2);
	const AptFunctionDef *inner = outer->body->instructions[1].function.get();
	REQUIRE(inner);
	CHECK(inner->isV2);
	CHECK(inner->registerCount == 4);
	CHECK(inner->flags == (APT_FN2_PRELOAD_ROOT | APT_FN2_SUPPRESS_THIS));
	CHECK(inner->params[0].reg == 2);
	CHECK(inner->params[0].name == "x");
	CHECK(b->flatInstructionCount() == 5); // DefineFunction, End + Pop, DefineFunction2 + PushDuplicate
	std::uint32_t counts[256] = {};
	b->countOpcodes(counts);
	CHECK(counts[APT_OP_PUSHDUPLICATE] == 1);
	CHECK(counts[APT_OP_DEFINEFUNCTION2] == 1);
	CHECK(counts[APT_OP_END] == 1);
}

TEST_CASE("decoder: a function body that overruns its declared size is an error")
{
	TestMovie m;
	Asm a = m.program();
	a.defineFunction("f", {}, [](Asm &f) { f.pushShort(1); });
	a.op(APT_OP_END);
	std::uint32_t off = m.commit(a);
	m.setRootFrames({ { m.addActionItem(off) } });
	std::vector<std::uint8_t> apt, cst;
	m.build(apt, cst);
	// DefineFunction at off: opcode, pad to 4, then name(4) count(4) table(4) size(4): shrink size by one
	std::uint32_t operandAt = (off + 1 + 3) & ~3u;
	std::uint32_t sizeAt = operandAt + 12;
	std::uint32_t size = apt[sizeAt];
	apt[sizeAt] = (std::uint8_t)(size - 1);
	AptConstFile cf;
	std::string err;
	REQUIRE(AptConstFile::parse(cst, cf, &err));
	AptFile f;
	REQUIRE_MESSAGE(AptFile::parse("T", std::make_shared<const std::vector<std::uint8_t>>(apt), cf, f, &err), err);
	CHECK_FALSE(f.codeAt(off, &err));
	CHECK(err.find("bounded body ended") != std::string::npos);
}

// ---- .dat and .ru -------------------------------------------------------------------------

TEST_CASE("image map: arrow and rectangle lines, comments and errors")
{
	std::string text = "; Created by AptToBigc.\r\n46->1\r\n3=0 0 112 5\r\n\r\n";
	AptImageMap map;
	std::string err;
	REQUIRE_MESSAGE(AptImageMap::parse(std::vector<std::uint8_t>(text.begin(), text.end()), map, &err), err);
	REQUIRE(map.entries.size() == 2);
	CHECK(map.entries[0].imageId == 46);
	CHECK_FALSE(map.entries[0].isRect);
	CHECK(map.entries[0].textureId == 1);
	CHECK(map.entries[1].isRect);
	CHECK(map.entries[1].rect[2] == 112);
	CHECK(map.entries[1].rect[3] == 5);

	std::string dup = "1->1\n1->2\n";
	CHECK_FALSE(AptImageMap::parse(std::vector<std::uint8_t>(dup.begin(), dup.end()), map, &err));
	std::string junk = "hello\n";
	CHECK_FALSE(AptImageMap::parse(std::vector<std::uint8_t>(junk.begin(), junk.end()), map, &err));
	std::string badrect = "2=0 0 0 5\n";
	CHECK_FALSE(AptImageMap::parse(std::vector<std::uint8_t>(badrect.begin(), badrect.end()), map, &err));
}

TEST_CASE("geometry: styles, triangles and lines parse; records outside a style are errors")
{
	std::string text = "c\r\ns s:255:0:0:255\r\nt 0:0:10:0:0:10\r\nc\r\ns tc:255:255:255:255:103:1:0:0:1:347:82\r\nt 0:17:0:0:25:0\r\nc\r\ns l:2:0:0:0:255\r\nl 0:0:5:5\r\n";
	AptGeometry g;
	std::string err;
	REQUIRE_MESSAGE(AptGeometry::parse(std::vector<std::uint8_t>(text.begin(), text.end()), g, &err), err);
	CHECK(g.clearCount == 3);
	REQUIRE(g.styles.size() == 3);
	CHECK(g.styles[0].kind == APT_STYLE_SOLID);
	CHECK(g.styles[0].triangles.size() == 6);
	CHECK(g.styles[1].kind == APT_STYLE_TEXTURED);
	CHECK(g.styles[1].imageId == 103);
	CHECK(g.styles[1].uv[4] == 347.0f);
	CHECK(g.styles[1].uv[5] == 82.0f);
	CHECK(g.styles[2].kind == APT_STYLE_LINE);
	CHECK(g.styles[2].lineWidth == 2.0f);
	CHECK(g.styles[2].lines.size() == 4);

	std::string stray = "t 0:0:1:1:2:2\n";
	CHECK_FALSE(AptGeometry::parse(std::vector<std::uint8_t>(stray.begin(), stray.end()), g, &err));
	std::string lineInTri = "c\ns s:0:0:0:0\nl 0:0:1:1\n";
	CHECK_FALSE(AptGeometry::parse(std::vector<std::uint8_t>(lineInTri.begin(), lineInTri.end()), g, &err));
	std::string unknown = "z 1\n";
	CHECK_FALSE(AptGeometry::parse(std::vector<std::uint8_t>(unknown.begin(), unknown.end()), g, &err));
	std::string badStyle = "s q:1:2\n";
	CHECK_FALSE(AptGeometry::parse(std::vector<std::uint8_t>(badStyle.begin(), badStyle.end()), g, &err));
}

// ---- loader -------------------------------------------------------------------------------

namespace
{
class MapSource : public AptFileSource
{
public:
	std::map<std::string, std::vector<std::uint8_t>> files; // lower-cased path
	bool readFile(const std::string &p, std::vector<std::uint8_t> &out, std::string *error) override
	{
		std::string k = p;
		for (char &c : k)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		auto it = files.find(k);
		if (it == files.end())
		{
			if (error)
			{
				*error = "file not found: " + p;
			}
			return false;
		}
		out = it->second;
		return true;
	}
	bool fileExists(const std::string &p) override
	{
		std::vector<std::uint8_t> o;
		return readFile(p, o, nullptr);
	}
	std::vector<std::string> listMovies() override { return {}; }
	void add(const std::string &name, TestMovie &m)
	{
		std::vector<std::uint8_t> a, c;
		m.build(a, c);
		std::string k = name;
		for (char &ch : k)
		{
			ch = (char)std::tolower((unsigned char)ch);
		}
		files[k + ".apt"] = a;
		files[k + ".const"] = c;
	}
};
} // namespace

TEST_CASE("loader: imports resolve to the exporting movie's character; missing and ambiguous exports fail")
{
	MapSource src;
	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 1, 1, 1);
	lib.addCharacter(0);
	lib.addCharacter(shape);
	lib.addCharacter(shape);
	lib.addExport("Gadget", 1);
	lib.addExport("Twin", 1);
	lib.addExport("twin", 2);
	src.add("Lib", lib);

	TestMovie user;
	user.addCharacter(0);
	user.addCharacter(0);
	user.addCharacter(0);
	user.addCharacter(0); // slot 3 is the destination of the third import
	user.addImport("LIB", "gadget", 1);
	user.addImport("Lib", "Twin", 2);
	user.addImport("Lib", "Nope", 3);
	src.add("User", user);

	AptLoader loader(src);
	std::string err;
	std::shared_ptr<const AptFile> u = loader.loadMovie("User", &err);
	REQUIRE_MESSAGE(u, err);
	REQUIRE(u->imports.size() == 3);
	AptResolvedImport r;
	REQUIRE_MESSAGE(loader.resolveImport(*u, u->imports[0], r, &err), err);
	CHECK(r.characterId == 1);
	CHECK(r.localSlot == 1);
	CHECK(r.movie->name == "LIB"); // loaded under the first spelling requested
	CHECK_FALSE(loader.resolveImport(*u, u->imports[1], r, &err));
	CHECK(err.find("several different characters") != std::string::npos);
	CHECK_FALSE(loader.resolveImport(*u, u->imports[2], r, &err));
	CHECK(err.find("does not export") != std::string::npos);
	std::vector<AptResolvedImport> all;
	CHECK_FALSE(loader.resolveImports(*u, all, &err));
	CHECK_FALSE(loader.loadMovie("Nowhere", &err));
	CHECK(err.find("Nowhere") != std::string::npos);
}

namespace
{
// A movie with `slots` null characters plus the given imports and exports (no shapes).
void addNullMovie(MapSource &src, const char *name, std::uint32_t slots, const std::vector<std::array<std::string, 3>> &imports,
	const std::vector<std::pair<std::string, std::uint32_t>> &exports)
{
	TestMovie m;
	for (std::uint32_t i = 0; i < slots; ++i)
	{
		m.addCharacter(0);
	}
	for (const auto &imp : imports)
	{
		m.addImport(imp[0], imp[1], (std::uint32_t)std::stoul(imp[2]));
	}
	for (const auto &e : exports)
	{
		m.addExport(e.first, e.second);
	}
	src.add(name, m);
}

std::string importError(MapSource &src, const char *importerName)
{
	AptLoader loader(src);
	std::string err;
	std::shared_ptr<const AptFile> u = loader.loadMovie(importerName, &err);
	REQUIRE_MESSAGE(u, err);
	REQUIRE(u->imports.size() == 1);
	AptResolvedImport r;
	std::string out;
	CHECK_FALSE(loader.resolveImport(*u, u->imports[0], r, &out));
	return out;
}
} // namespace

TEST_CASE("loader: an import needs an in-range null destination slot in the importer")
{
	MapSource src;
	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 1, 1, 1);
	lib.addCharacter(0);
	lib.addCharacter(shape);
	lib.addExport("Gadget", 1);
	src.add("Lib", lib);

	// destination slot 0xffffffff (the review's case): outside the importer's character table
	addNullMovie(src, "FarSlot", 2, { { "Lib", "Gadget", "4294967295" } }, {});
	CHECK(importError(src, "FarSlot").find("destination character slot 4294967295 is outside") != std::string::npos);
	// destination slot just past the table
	addNullMovie(src, "PastEnd", 2, { { "Lib", "Gadget", "2" } }, {});
	CHECK(importError(src, "PastEnd").find("is outside the importer's 2 characters") != std::string::npos);
	// destination slot holding a real character instead of a placeholder
	TestMovie occupied;
	std::uint32_t occShape = occupied.addShape(0, 0, 1, 1, 1);
	occupied.addCharacter(occShape);
	occupied.addImport("Lib", "Gadget", 0);
	src.add("Occupied", occupied);
	CHECK(importError(src, "Occupied").find("is not a null placeholder") != std::string::npos);
}

TEST_CASE("loader: an exported slot must resolve to a definition; re-exports are followed; cycles are errors")
{
	MapSource src;
	TestMovie base;
	std::uint32_t shape = base.addShape(0, 0, 1, 1, 1);
	base.addCharacter(0);
	base.addCharacter(shape);
	base.addExport("Real", 1);
	src.add("Base", base);

	// a re-export: Mid's export "Thing" is its null slot 1, which Mid imports from Base::Real
	addNullMovie(src, "Mid", 2, { { "Base", "Real", "1" } }, { { "Thing", 1 } });
	addNullMovie(src, "Chain", 2, { { "Mid", "Thing", "1" } }, {});
	{
		AptLoader loader(src);
		std::string err;
		std::shared_ptr<const AptFile> u = loader.loadMovie("Chain", &err);
		REQUIRE_MESSAGE(u, err);
		AptResolvedImport r;
		REQUIRE_MESSAGE(loader.resolveImport(*u, u->imports[0], r, &err), err);
		CHECK(r.movie->name == "Base"); // the definition lives in Base, not in Mid
		CHECK(r.characterId == 1);
		CHECK(r.localSlot == 1);
	}

	// an export of a null slot with nothing behind it
	addNullMovie(src, "Hole", 2, {}, { { "Void", 1 } });
	addNullMovie(src, "UsesHole", 2, { { "Hole", "Void", "1" } }, {});
	CHECK(importError(src, "UsesHole").find("no import behind it") != std::string::npos);

	// two movies that export each other's imports: P::X -> Q::Y -> P::X
	addNullMovie(src, "P", 2, { { "Q", "Y", "1" } }, { { "X", 1 } });
	addNullMovie(src, "Q", 2, { { "P", "X", "1" } }, { { "Y", 1 } });
	addNullMovie(src, "UsesCycle", 2, { { "P", "X", "1" } }, {});
	CHECK(importError(src, "UsesCycle").find("import cycle") != std::string::npos);
}

TEST_CASE("parser: aggregate resource budgets stop aliased frame tables before they allocate")
{
	// One real item, a 65536-entry item table pointing at it, and a 65536-entry frame table whose every
	// record points at that same item table: the file is under 1 MiB but names 65536 * 65536 items.
	TestMovie m;
	std::uint32_t item = m.addActionItem(0);
	m.setRootFrames({ { item } });
	std::vector<std::uint8_t> a, c;
	m.build(a, c);
	std::uint32_t entry = (std::uint32_t)c[20] | ((std::uint32_t)c[21] << 8) | ((std::uint32_t)c[22] << 16) | ((std::uint32_t)c[23] << 24);
	auto put = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			a.push_back((std::uint8_t)(v >> (8 * i)));
		}
	};
	auto patch = [&](std::uint32_t at, std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			a[at + i] = (std::uint8_t)(v >> (8 * i));
		}
	};
	std::uint32_t itemTable = (std::uint32_t)a.size();
	for (int i = 0; i < 65536; ++i)
	{
		put(item);
	}
	std::uint32_t frameTable = (std::uint32_t)a.size();
	for (int i = 0; i < 65536; ++i)
	{
		put(65536);
		put(itemTable);
	}
	patch(entry + 8, 65536);
	patch(entry + 12, frameTable);
	AptConstFile cf;
	std::string err;
	REQUIRE_MESSAGE(AptConstFile::parse(c, cf, &err), err);
	AptFile out;
	CHECK_FALSE(AptFile::parse("Alias", std::make_shared<const std::vector<std::uint8_t>>(a), cf, out, &err));
	CHECK(err.find("frame items in total") != std::string::npos);
}

TEST_CASE("parser: aliased font records are refused by the aggregate glyph budget")
{
	// One font with 65536 glyphs and 65536 character pointers aliasing it: about 0.5 MiB of file, 2^32 glyph entries
	TestMovie m;
	std::vector<std::uint32_t> glyphs(65536, 1);
	std::uint32_t font = m.addFont("Aliased", glyphs);
	for (int i = 0; i < 65536; ++i)
	{
		m.addCharacter(font);
	}
	AptFile out;
	std::string err;
	CHECK_FALSE(m.parse(out, &err));
	CHECK(err.find("font glyphs in total") != std::string::npos);
}

TEST_CASE("parser: aliased clip-action tables, button records and repeated strings hit their aggregate budgets")
{
	// 17 place-object items aliasing one place object whose clip-action table holds 65536 events: 17 * 65536 > 2^20
	{
		TestMovie m;
		TestMovie::Place place;
		place.flags = APT_PLACE_HASCHARACTER | APT_PLACE_HASCLIPACTION;
		for (int i = 0; i < 65536; ++i)
		{
			place.events.push_back({ APT_CLIP_KEYPRESS, 0, 0 });
		}
		std::uint32_t item = m.addPlaceItem(place);
		m.setRootFrames({ std::vector<std::uint32_t>(17, item) });
		AptFile out;
		std::string err;
		CHECK_FALSE(m.parse(out, &err));
		CHECK(err.find("clip events in total") != std::string::npos);
	}
	// 17 character pointers aliasing one button with 65536 records
	{
		TestMovie m;
		std::vector<std::pair<std::uint32_t, std::uint32_t>> records(65536, { 1, 0 });
		std::uint32_t button = m.addButton(records, {});
		for (int i = 0; i < 17; ++i)
		{
			m.addCharacter(button);
		}
		AptFile out;
		std::string err;
		CHECK_FALSE(m.parse(out, &err));
		CHECK(err.find("button elements in total") != std::string::npos);
	}
	// 20000 items aliasing one place object named by a 4000-byte string: 80 MB of names against the 64 MiB budget
	{
		TestMovie m;
		TestMovie::Place place;
		place.flags = APT_PLACE_HASCHARACTER | APT_PLACE_HASNAME;
		place.name = std::string(4000, 'n');
		std::uint32_t item = m.addPlaceItem(place);
		m.setRootFrames({ std::vector<std::uint32_t>(20000, item) });
		AptFile out;
		std::string err;
		CHECK_FALSE(m.parse(out, &err));
		CHECK(err.find("string bytes in total") != std::string::npos);
	}
}

TEST_CASE("parser: the aggregate FRAME budget is exercised by empty frames, not by items")
{
	// five characters aliasing one sprite of 65536 EMPTY frames: 327,680 frames against the 2^18 cap, 0 items
	TestMovie m;
	std::uint32_t sprite = m.addSprite(std::vector<std::vector<std::uint32_t>>(65536));
	for (int i = 0; i < 5; ++i)
	{
		m.addCharacter(sprite);
	}
	AptFile out;
	std::string err;
	CHECK_FALSE(m.parse(out, &err));
	CHECK(err.find("frames in total") != std::string::npos);
	CHECK(err.find("frame items") == std::string::npos);
}

TEST_CASE("decoder: ConstantPool string copies are bounded in total (resource bound S-009)")
{
	auto decode = [](int instructions, int entries, int stringBytes, std::string &err) {
		TestMovie m;
		std::uint32_t big = m.constString(std::string((std::size_t)stringBytes, 'x'));
		std::uint32_t table = m.structAt();
		for (int i = 0; i < entries; ++i)
		{
			m.emit32(big); // one 4096-entry index table, shared by every instruction
		}
		Asm a = m.program();
		for (int i = 0; i < instructions; ++i)
		{
			a.constantPoolAt((std::uint32_t)entries, table);
		}
		a.op(APT_OP_END);
		std::uint32_t off = m.commit(a);
		m.setRootFrames({ { m.addActionItem(off) } });
		AptFile f;
		REQUIRE_MESSAGE(m.parse(f, &err), err);
		return f.codeAt(off, &err);
	};
	std::string err;
	// 300 instructions x 4096 entries = 1.2M references: refused by the reference total
	CHECK_FALSE(decode(300, 4096, 4, err));
	CHECK(err.find("constants in total") != std::string::npos);
	// 6 instructions x 4096 entries of a 4000-byte string = 98 MB: refused by the byte total
	CHECK_FALSE(decode(6, 4096, 4000, err));
	CHECK(err.find("bytes of constant strings in total") != std::string::npos);
	// a modest pool decodes
	CHECK(decode(4, 16, 100, err));
}

TEST_CASE("loader: .swf urls name movies (EA DispatchLiteral008C5840: suffix test is case-insensitive)")
{
	std::string name;
	CHECK(AptLoader::movieNameFromSwf("SkirmishOpenPlay.swf", name));
	CHECK(name == "SkirmishOpenPlay");
	CHECK(AptLoader::movieNameFromSwf("Y.SWF", name));
	CHECK(name == "Y");
	CHECK_FALSE(AptLoader::movieNameFromSwf("Y.swf2", name));
	CHECK_FALSE(AptLoader::movieNameFromSwf("swf", name));
	CHECK_FALSE(AptLoader::movieNameFromSwf("FSCommand:Foo", name));
}
