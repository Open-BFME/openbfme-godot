// OpenBFME tests: APT-3, the Godot-independent half of the menu renderer (render list -> canvas operations), the string table, the
// font substitution file and the stage mapping.  Retail-gated tests print SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.
// GPL-3.0.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/FontSubstitution.h"
#include "GameClient/GameText.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

using namespace apttest;

namespace
{

std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

// A bottom-up 32-bit TGA (descriptor 8, like the corpus): pixels are given in file order (row 0 = the bottom row of the image).
std::vector<std::uint8_t> makeTga(int w, int h, const std::vector<std::array<std::uint8_t, 4>> &bgraFileOrder)
{
	std::vector<std::uint8_t> out(18, 0);
	out[2] = 2;
	out[12] = (std::uint8_t)(w & 255);
	out[13] = (std::uint8_t)(w >> 8);
	out[14] = (std::uint8_t)(h & 255);
	out[15] = (std::uint8_t)(h >> 8);
	out[16] = 32;
	out[17] = 8;
	for (const auto &p : bgraFileOrder)
	{
		out.insert(out.end(), p.begin(), p.end());
	}
	return out;
}

} // namespace

// ---- GameText ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("GameText: the STR grammar - labels, comments, escapes, multi-line strings, speech names, END, case-insensitive lookup")
{
	const std::string str =
		"// String file\r\n"
		"\r\n"
		"APT:Quit\r\n"
		"// Context: a button\r\n"
		"\"QUIT\"\r\n"
		"END\r\n"
		"\r\n"
		"GUI:Two\r\n"
		"\"  first   line\r\n"
		"second \\\"quoted\\\" and \\\\ and \\n newline\" speech_one\r\n"
		"END\r\n"
		"Misc:Cp\r\n"
		"\"caf\xE9 \x92 \x85\"\r\n"
		"end\r\n";
	GameTextTable t;
	std::string error;
	REQUIRE_MESSAGE(t.parse(bytesOf(str), &error), error);
	CHECK(t.size() == 3);
	std::string v;
	REQUIRE(t.lookup("apt:QUIT", v)); // case-insensitive
	CHECK(v == "QUIT");
	REQUIRE(t.lookup("GUI:Two", v));
	// ZH stripSpaces: leading and repeated spaces go, the newline inside the quotes is a space, \n is a newline, spaces around it go
	CHECK(v == "first line second \"quoted\" and \\ and\nnewline");
	REQUIRE(t.lookup("Misc:Cp", v));
	CHECK(v == "caf\xC3\xA9 \xE2\x80\x99 \xE2\x80\xA6"); // Windows-1252 0xE9, 0x92, 0x85 -> UTF-8
	CHECK_FALSE(t.lookup("APT:Nothing", v));
}

TEST_CASE("GameText: a duplicate label keeps the first string and is reported; a string that never closes and a label with no END are errors")
{
	GameTextTable t;
	std::string error;
	REQUIRE(t.parse(bytesOf("A\n\"one\"\nEND\nA\n\"two\"\nEND\n"), &error));
	std::string v;
	REQUIRE(t.lookup("A", v));
	CHECK(v == "one");
	REQUIRE(t.duplicateLabels().size() == 1);
	CHECK(t.duplicateLabels()[0] == "A");
	GameTextTable bad;
	CHECK_FALSE(bad.parse(bytesOf("A\n\"never closed\nEND\n"), &error));
	CHECK(error.find("not closed") != std::string::npos);
	GameTextTable noEnd;
	CHECK_FALSE(noEnd.parse(bytesOf("A\n\"x\"\n"), &error));
	CHECK(error.find("no END") != std::string::npos);
}

TEST_CASE("Apt text: $LABEL becomes APT:LABEL unless it holds a colon; &dropShadow is cut and flagged; other text is shown as it is; a missing label is the label")
{
	GameTextTable t;
	std::string error;
	REQUIRE(t.parse(bytesOf("APT:Quit\n\"QUIT\"\nEND\nGUI:Other\n\"Other text\"\nEND\n"), &error));
	AptTextResolution r = ResolveAptText("$Quit", t);
	CHECK(r.wasLabel);
	CHECK(r.found);
	CHECK(r.label == "APT:Quit");
	CHECK(r.text == "QUIT");
	CHECK_FALSE(r.dropShadow);
	r = ResolveAptText("$GUI:Other&dropShadow", t);
	CHECK(r.found);
	CHECK(r.text == "Other text");
	CHECK(r.dropShadow);
	r = ResolveAptText("$Quit&DROPSHADOW", t);
	CHECK(r.dropShadow);
	CHECK(r.text == "QUIT");
	r = ResolveAptText("$Quit&dropShadowX", t); // only the exact suffix cuts
	CHECK_FALSE(r.dropShadow);
	CHECK_FALSE(r.found);
	r = ResolveAptText("Plain \x93text\x94", t);
	CHECK_FALSE(r.wasLabel);
	CHECK(r.text == "Plain \xE2\x80\x9Ctext\xE2\x80\x9D");
	r = ResolveAptText("$Missing", t);
	CHECK(r.wasLabel);
	CHECK_FALSE(r.found);
	CHECK(r.text == "APT:Missing"); // visible, and reported by the canvas builder
}

TEST_CASE("retail data/lotr.str: parses, APT:Quit is QUIT, 36 labels are defined twice (stop S-131)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(source.readFile("data/lotr.str", bytes, &error), error);
	GameTextTable t;
	REQUIRE_MESSAGE(t.parse(bytes, &error), error);
	CHECK(t.size() > 10000);
	std::string v;
	REQUIRE(t.lookup("APT:Quit", v));
	CHECK(v == "QUIT");
	std::string dup;
	for (const std::string &d : t.duplicateLabels())
	{
		dup += d + " ";
	}
	// retail data: 36 label definitions repeat an earlier label (S-131: which one the binary keeps was not read; the first is kept here)
	CHECK_MESSAGE(t.duplicateLabels().size() == 36, t.duplicateLabels().size() << " duplicate labels: " << dup);
}

// ---- FontSubstitution -------------------------------------------------------------------------------------------------------------

TEST_CASE("FontSubstitution: the examples of the file's own comment (9 -> 10 Arial, 15 -> 13 Arial, 22 -> 17 Arial, 24 -> 18 Times, 72 -> 36 Times)")
{
	FontSubstitution f;
	std::string error;
	REQUIRE_MESSAGE(f.parse(
		"; comment\n"
		"FontSubstitution \"Albertus MT\"   ; trailing comment\n"
		"  Size 10 = 10 Arial\n"
		"  Size 20 = 16 Arial\n"
		"  Size 24 = 18 \"Times New Roman\"\n"
		"  Size 48 = 36 \"Times New Roman\"\n"
		"End\n"
		"FontSubstitution Bold\n  Size 8 = 12 +BOLD Arial\n  Size 9 = 13 -BOLD \"Omnia LT Std\"\nEnd\n",
		&error), error);
	auto check = [&](float size, const char *name, float outSize) {
		FontRequestResult r = f.resolve("albertus mt", size);
		CHECK(r.substituted);
		CHECK(r.name == name);
		CHECK(r.size == doctest::Approx(outSize));
	};
	check(9, "Arial", 10);
	check(10, "Arial", 10);
	check(15, "Arial", 13);
	check(22, "Arial", 17);
	check(24, "Times New Roman", 18);
	check(72, "Times New Roman", 36);
	FontRequestResult none = f.resolve("Unlisted", 14);
	CHECK_FALSE(none.substituted);
	CHECK(none.name == "Unlisted");
	CHECK(none.size == 14);
	CHECK(f.resolve("Bold", 8).bold == 1);
	CHECK(f.resolve("Bold", 9).bold == -1);
	FontSubstitution broken;
	CHECK_FALSE(broken.parse("FontSubstitution X\n Size ten = 10 Arial\nEnd\n", &error));
	CHECK(error.find("line 2") != std::string::npos);
	CHECK_FALSE(broken.parse("FontSubstitution X\n Size 10 = 10 Arial\n", &error));
}

TEST_CASE("retail fontsubstitution.ini: SachaWynter is drawn as Omnia LT Std (8 -> 12, 40 -> 70)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(source.readFile("data/ini/fontsubstitution.ini", bytes, &error), error);
	FontSubstitution f;
	REQUIRE_MESSAGE(f.parse(std::string(bytes.begin(), bytes.end()), &error), error);
	CHECK(f.blockCount() == 1);
	FontRequestResult r = f.resolve("SachaWynter", 8);
	CHECK(r.name == "Omnia LT Std");
	CHECK(r.size == doctest::Approx(12.0f));
	r = f.resolve("SachaWynter", 40);
	CHECK(r.size == doctest::Approx(70.0f));
	r = f.resolve("SachaWynter", 24); // 12 + (16/32) * 58 = 41
	CHECK(r.size == doctest::Approx(41.0f));
	CHECK_FALSE(f.resolve("Albertus MT", 14).substituted);
}

// ---- stage mapping -------------------------------------------------------------------------------------------------------------------

TEST_CASE("stage mapping: stretch scales x and y independently (donor setScaledTransform007833E0); fit is uniform and centred; both invert")
{
	AptStageMapping m;
	m.stageW = 1024;
	m.stageH = 768;
	m.windowW = 1280;
	m.windowH = 720;
	CHECK(m.scaleX() == doctest::Approx(1.25f));
	CHECK(m.scaleY() == doctest::Approx(0.9375f));
	CHECK(m.uniformScale() == doctest::Approx(0.9375f));
	float wx, wy;
	m.stageToWindow(1024, 768, wx, wy);
	CHECK(wx == doctest::Approx(1280.0f));
	CHECK(wy == doctest::Approx(720.0f));
	float x, y;
	m.windowToStage(640, 360, x, y);
	CHECK(x == doctest::Approx(512.0f));
	CHECK(y == doctest::Approx(384.0f));
	m.mode = AptStageMapping::Mode::Fit;
	CHECK(m.scaleX() == doctest::Approx(0.9375f));
	CHECK(m.scaleY() == doctest::Approx(0.9375f));
	CHECK(m.offsetX() == doctest::Approx((1280.0f - 960.0f) / 2));
	CHECK(m.offsetY() == doctest::Approx(0.0f));
	m.stageToWindow(0, 0, wx, wy);
	CHECK(wx == doctest::Approx(160.0f));
	m.windowToStage(160 + 480, 360, x, y);
	CHECK(x == doctest::Approx(512.0f));
	CHECK(y == doctest::Approx(384.0f));
	AptStageMapping zero;
	zero.stageW = 0;
	zero.stageH = 0;
	CHECK(zero.scaleX() == 1.0f); // donor: scale 1 when the stage dimension is 0
}

// ---- the canvas builder on synthetic movies -------------------------------------------------------------------------------------------

namespace
{

struct CanvasFx
{
	PlayerFx fx;
	AptTextureStore textures;
	GameTextTable strings;
	CanvasFx() : textures(fx.source) {}

	AptCanvasList build(float windowW = 1024, float windowH = 768, bool merge = true, const FontSubstitution *fonts = nullptr)
	{
		AptRenderList rl;
		fx.apt->buildRenderList(rl);
		AptCanvasInputs in;
		in.mapping.windowW = windowW;
		in.mapping.windowH = windowH;
		in.textures = &textures;
		in.text = &strings;
		in.fonts = fonts;
		AptCanvasList out;
		BuildAptCanvas(rl, in, out, merge);
		return out;
	}
};

void addFile(CanvasFx &c, const std::string &name, const std::string &content)
{
	c.fx.source.files[MemorySource::lower(name)] = bytesOf(content);
}

} // namespace

TEST_CASE("canvas: a solid fill is window-space triangles with the fill colour times the multiplier; the additive term rides on the op")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place p = placeChar(shapeId, 1, "s");
	p.flags |= APT_PLACE_HASMATRIX | APT_PLACE_HASCOLORTRANSFORM;
	p.matrix[0] = 2;
	p.matrix[3] = 3;
	p.translation[0] = 100;
	p.translation[1] = 200;
	p.tint[0] = 127; // the file bytes are B, G, R, A: the blue multiplier
	p.tint[1] = 255;
	p.tint[2] = 255;
	p.tint[3] = 128;
	p.additive[1] = 51;
	m.setRootFrames({ { m.addPlaceItem(p) } });
	CanvasFx c;
	addFile(c, "A_geometry/1.ru", "s s:100:200:50:255\nt 0:0:10:0:10:10\nt 0:0:10:10:0:10\nc\n");
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build(2048, 1536); // window twice the stage
	CHECK(list.errors.empty());
	REQUIRE(list.ops.size() == 1);
	const AptCanvasOp &op = list.ops[0];
	CHECK(op.kind == AptCanvasOp::Kind::Mesh);
	CHECK(op.triangleCount() == 2);
	CHECK(list.triangles == 2);
	REQUIRE(op.positions.size() == 12);
	// vertex (10, 0) -> stage (2*10 + 100, 200) = (120, 200) -> window (240, 400)
	CHECK(op.positions[2] == doctest::Approx(240.0f));
	CHECK(op.positions[3] == doctest::Approx(400.0f));
	// vertex (10, 10) -> stage (120, 230) -> window (240, 460)
	CHECK(op.positions[4] == doctest::Approx(240.0f));
	CHECK(op.positions[5] == doctest::Approx(460.0f));
	// RotWK's vertex colour (RW 0x4A8AD5): trunc((c / 255 * mul + add / 255) * 255) per channel, the additive term inside it
	CHECK(op.colors[0] == doctest::Approx(100.0f / 255.0f));
	CHECK(op.colors[1] == doctest::Approx(251.0f / 255.0f)); // 200 + 51
	CHECK(op.colors[2] == doctest::Approx(24.0f / 255.0f));  // 50 * 127 / 255 = 24.9, truncated
	CHECK(op.colors[3] == doctest::Approx(128.0f / 255.0f));
	CHECK(op.uvs.empty());
	CHECK(list.unverified == std::vector<std::string>{ "stage-mapping" });
}

TEST_CASE("canvas: a textured fill takes UV = tc matrix * vertex / texture size, with rows flipped so v counts from the top; a missing texture is an error and draws nothing")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place p = placeChar(shapeId, 1, "s");
	TestMovie::Place q = placeChar(shapeId, 2, "t");
	m.setRootFrames({ { m.addPlaceItem(p), m.addPlaceItem(q) } });
	CanvasFx c;
	// image 3 of movie A is atlas texture 1; the tc matrix is identity with a translation of (2, 1) texture pixels
	addFile(c, "A.dat", "; Created by AptToBigc.\n3->1\n");
	addFile(c, "A_geometry/1.ru", "s tc:255:255:255:255:3:1:0:0:1:2:1\nt 0:0:4:0:4:2\nc\n");
	// a 4x2 bottom-up TGA: file row 0 (the bottom row) is blue, file row 1 (the top row) is red.  BGRA bytes.
	std::vector<std::array<std::uint8_t, 4>> px;
	for (int i = 0; i < 4; ++i)
	{
		px.push_back({ 255, 0, 0, 255 }); // blue
	}
	for (int i = 0; i < 4; ++i)
	{
		px.push_back({ 0, 0, 255, 255 }); // red
	}
	c.fx.source.files["art/textures/apt_a_1.tga"] = makeTga(4, 2, px);
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build();
	CHECK(list.errors.empty());
	REQUIRE(list.ops.size() == 1); // the two placements share state: one merged op
	const AptCanvasOp &op = list.ops[0];
	CHECK(op.texture == "apt_A_1.tga");
	CHECK(op.triangleCount() == 2);
	REQUIRE(op.uvs.size() == 12);
	// vertex (4, 0): u = (4 + 2) / 4, v = (0 + 1) / 2
	CHECK(op.uvs[2] == doctest::Approx(1.5f));
	CHECK(op.uvs[3] == doctest::Approx(0.5f));
	CHECK(op.colors[0] == doctest::Approx(1.0f));
	const AptTextureStore::Entry &tex = c.textures.get("apt_A_1.tga");
	REQUIRE(tex.ok);
	CHECK(tex.width == 4);
	CHECK(tex.height == 2);
	// row 0 of the stored pixels is the top: red (the file's last row)
	CHECK(tex.rgba[0] == 255);
	CHECK(tex.rgba[2] == 0);
	// row 1 is blue
	CHECK(tex.rgba[4 * 4 + 0] == 0);
	CHECK(tex.rgba[4 * 4 + 2] == 255);
	CHECK(std::count(list.unverified.begin(), list.unverified.end(), "tga-origin") == 1);
	CHECK(std::count(list.unverified.begin(), list.unverified.end(), "uv-matrix-order") == 1);

	// the same movie without the texture: the failure is reported once and nothing is drawn in its place
	CanvasFx d;
	addFile(d, "A.dat", "3->1\n");
	addFile(d, "A_geometry/1.ru", "s tc:255:255:255:255:3:1:0:0:1:2:1\nt 0:0:4:0:4:2\nc\n");
	REQUIRE(d.fx.load(0, "A", m));
	AptCanvasList missing = d.build();
	CHECK(missing.ops.empty());
	REQUIRE(missing.errors.size() == 1);
	CHECK(missing.errors[0].find("apt_A_1.tga") != std::string::npos);
}

TEST_CASE("canvas: an image id with no .dat entry is the render list's error and draws nothing")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	CanvasFx c;
	addFile(c, "A.dat", "4->1\n");
	addFile(c, "A_geometry/1.ru", "s tc:255:255:255:255:3:1:0:0:1:0:0\nt 0:0:4:0:4:2\nc\n");
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build();
	CHECK(list.ops.empty());
	REQUIRE(list.errors.size() == 1);
	CHECK(list.errors[0].find("image 3 has no .dat entry") != std::string::npos);
}

TEST_CASE("canvas: a line style becomes quads of the line width (at least one window pixel)")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	m.setRootFrames({ { m.addPlaceItem(placeChar(shapeId, 1, "s")) } });
	CanvasFx c;
	addFile(c, "A_geometry/1.ru", "s l:4:255:255:255:255\nl 0:0:10:0\nc\ns l:0:0:0:0:255\nl 0:5:10:5\nc\n");
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build();
	REQUIRE(list.ops.size() == 1);
	CHECK(list.ops[0].triangleCount() == 4); // two segments, two triangles each
	const std::vector<float> &p = list.ops[0].positions;
	// the first segment: a horizontal quad 4 px high around y = 0
	float minY = 1e9f, maxY = -1e9f;
	for (int v = 0; v < 6; ++v)
	{
		minY = std::min(minY, p[v * 2 + 1]);
		maxY = std::max(maxY, p[v * 2 + 1]);
	}
	CHECK(maxY - minY == doctest::Approx(4.0f));
	// the second: hairline, one pixel
	minY = 1e9f;
	maxY = -1e9f;
	for (int v = 6; v < 12; ++v)
	{
		minY = std::min(minY, p[v * 2 + 1]);
		maxY = std::max(maxY, p[v * 2 + 1]);
	}
	CHECK(maxY - minY == doctest::Approx(1.0f));
	CHECK(std::count(list.unverified.begin(), list.unverified.end(), "line-width") == 1);
}

TEST_CASE("canvas: mask brackets are balanced, the shapes of the mask are drawn opaque and untextured, and draw order is the render list order")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place mask = placeChar(shapeId, 1, "mask");
	mask.flags |= APT_PLACE_HASCLIPDEPTH;
	mask.clipDepth = 3;
	TestMovie::Place faded = placeChar(shapeId, 2, "in2");
	faded.flags |= APT_PLACE_HASCOLORTRANSFORM;
	faded.tint[3] = 100;
	m.setRootFrames({ { m.addPlaceItem(mask), m.addPlaceItem(faded), m.addPlaceItem(placeChar(shapeId, 3, "in3")), m.addPlaceItem(placeChar(shapeId, 4, "out4")) } });
	CanvasFx c;
	addFile(c, "A_geometry/1.ru", "s s:10:20:30:40\nt 0:0:10:0:10:10\nc\n");
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build();
	CHECK(list.errors.empty());
	using K = AptCanvasOp::Kind;
	std::vector<K> kinds;
	for (const AptCanvasOp &op : list.ops)
	{
		kinds.push_back(op.kind);
	}
	// merging joins the two clipped shapes (they differ in vertex colour only); the mask shape stays apart
	CHECK(kinds == std::vector<K>{ K::MaskBegin, K::Mesh, K::MaskContent, K::Mesh, K::MaskEnd, K::Mesh });
	CHECK(list.ops[1].maskShape);
	CHECK(list.ops[1].colors[3] == doctest::Approx(1.0f)); // the fill alpha of 40 does not matter for a mask
	CHECK(list.ops[1].colors[0] == doctest::Approx(1.0f));
	CHECK_FALSE(list.ops[3].maskShape);
	CHECK(list.ops[3].triangleCount() == 2);
	CHECK(list.ops[3].colors[3] == doctest::Approx(15.0f / 255.0f)); // trunc(40 * 100 / 255 = 15.7), RW 0x4A8AD5
	CHECK(std::count(list.unverified.begin(), list.unverified.end(), "mask-alpha") == 1);
	CHECK(std::count(list.unverified.begin(), list.unverified.end(), "clip-layer") == 1);
	// merging changes the op count, never the triangles
	AptCanvasList unmerged = c.build(1024, 768, false);
	CHECK(unmerged.triangles == list.triangles);
	CHECK(unmerged.ops.size() > list.ops.size());
}

TEST_CASE("canvas: only filled shapes define a mask; a text inside the mask range of a clip layer is not part of the mask")
{
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	std::uint32_t font = m.addFont("Albertus MT", {});
	m.addCharacter(font);
	std::uint32_t shapeId = m.addCharacter(shape);
	std::uint32_t textId = m.addCharacter(m.addEditText("hello", "", 0, 14.0f));
	// a sprite clip layer whose content is a shape and a text: the text must not reach the mask
	std::uint32_t maskSprite = m.addSprite({ { m.addPlaceItem(placeChar(shapeId, 1, "s")), m.addPlaceItem(placeChar(textId, 2, "t")) } });
	std::uint32_t maskSpriteId = m.addCharacter(maskSprite);
	TestMovie::Place mask = placeChar(maskSpriteId, 1, "mask");
	mask.flags |= APT_PLACE_HASCLIPDEPTH;
	mask.clipDepth = 2;
	m.setRootFrames({ { m.addPlaceItem(mask), m.addPlaceItem(placeChar(shapeId, 2, "content")) } });
	CanvasFx c;
	addFile(c, "A_geometry/1.ru", "s s:10:20:30:255\nt 0:0:10:0:10:10\nc\n");
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build();
	CHECK(list.errors.empty());
	using K = AptCanvasOp::Kind;
	std::vector<K> kinds;
	for (const AptCanvasOp &op : list.ops)
	{
		kinds.push_back(op.kind);
	}
	CHECK(kinds == std::vector<K>{ K::MaskBegin, K::Mesh, K::MaskContent, K::Mesh, K::MaskEnd });
	CHECK(list.count(K::Text) == 0);
}

TEST_CASE("canvas: text is resolved through the string table and fontsubstitution.ini; a missing label is reported and shown as the label")
{
	TestMovie m;
	std::uint32_t font = m.addFont("Albertus MT", {});
	m.addCharacter(font);
	std::uint32_t a = m.addCharacter(m.addEditText("$Quit", "", 2, 14.0f));
	std::uint32_t b = m.addCharacter(m.addEditText("$Gone&dropShadow", "", 0, 20.0f));
	std::uint32_t d = m.addCharacter(m.addEditText("", "", 0, 20.0f)); // empty: nothing to draw
	m.setRootFrames({ { m.addPlaceItem(placeChar(a, 1, "a")), m.addPlaceItem(placeChar(b, 2, "b")), m.addPlaceItem(placeChar(d, 3, "d")) } });
	CanvasFx c;
	std::string error;
	REQUIRE(c.strings.parse(bytesOf("APT:Quit\n\"QUIT\"\nEND\n"), &error));
	FontSubstitution fonts;
	REQUIRE(fonts.parse("FontSubstitution \"Albertus MT\"\n Size 10 = 10 Arial\n Size 20 = 16 Arial\nEnd\n", &error));
	REQUIRE(c.fx.load(0, "A", m));
	AptCanvasList list = c.build(2048, 768, true, &fonts);
	CHECK(list.errors.empty());
	REQUIRE(list.ops.size() == 2);
	CHECK(list.ops[0].kind == AptCanvasOp::Kind::Text);
	CHECK(list.ops[0].text == "QUIT");
	CHECK(list.ops[0].fontName == "Albertus MT");
	CHECK(list.ops[0].drawFont == "Arial");
	CHECK(list.ops[0].drawSize == doctest::Approx(12.0f)); // 14 between (10 -> 10) and (20 -> 16): 10 + 0.4 * 6 = 12.4, truncated
	CHECK(list.ops[0].alignment == 2);
	CHECK(list.ops[0].scaleX == doctest::Approx(2.0f));
	CHECK(list.ops[0].scaleY == doctest::Approx(1.0f));
	CHECK(list.ops[1].text == "APT:Gone");
	CHECK(list.ops[1].dropShadow);
	CHECK(list.missingLabels == std::vector<std::string>{ "APT:Gone" });
	REQUIRE(list.fontSubstitutions.size() == 2);
	CHECK(list.fontSubstitutions[0] == "Albertus MT 14 -> Arial 12");
}

namespace
{
class GadgetHost : public AptStubHost
{
public:
	bool isComponentSymbol(const std::string &movie, const std::string &name) override { return movie == "Lib" && name == "Gadget"; }
};
} // namespace

TEST_CASE("canvas: a component placeholder keeps its bounds and symbol; it draws no triangles")
{
	TestMovie lib;
	std::uint32_t shape = lib.addShape(0, 0, 40, 20, 1);
	lib.addCharacter(0);
	std::uint32_t libShape = lib.addCharacter(shape);
	std::uint32_t sprite = lib.addSprite({ { lib.addPlaceItem(placeChar(libShape, 1)) } });
	lib.addExport("Gadget", lib.addCharacter(sprite));
	TestMovie user;
	user.addCharacter(0);
	user.addCharacter(0);
	user.addImport("Lib", "Gadget", 1);
	user.setRootFrames({ { user.addPlaceItem(placeChar(1, 1, "g")) } });
	MemorySource source;
	GadgetHost host;
	Apt apt(source, host);
	source.add("Lib", lib);
	source.add("User", user);
	source.files["lib_geometry/1.ru"] = bytesOf("s s:1:2:3:255\nt 0:0:1:0:1:1\nc\n");
	std::string error;
	REQUIRE_MESSAGE(apt.loadMovie(0, "User", &error), error);
	AptRenderList rl;
	apt.buildRenderList(rl);
	AptTextureStore textures(source);
	GameTextTable strings;
	AptCanvasInputs in;
	in.textures = &textures;
	in.text = &strings;
	AptCanvasList list;
	BuildAptCanvas(rl, in, list);
	REQUIRE(list.ops.size() == 1);
	CHECK(list.ops[0].kind == AptCanvasOp::Kind::Placeholder);
	CHECK(list.ops[0].symbolName == "Gadget");
	CHECK(list.ops[0].bounds[2] == doctest::Approx(40.0f));
	CHECK(list.triangles == 0);
}

// ---- text colour byte order ---------------------------------------------------------------------------------------------------------------

TEST_CASE("text colour: the file dword is 0xAARRGGBB (bytes B, G, R, A), so the render list and the canvas carry r, g, b, a")
{
	TestMovie m;
	std::uint32_t font = m.addFont("Albertus MT", {});
	m.addCharacter(font);
	// addEditText writes the colour dword 0xFF0000FF: blue 0xFF, green 0, red 0, alpha 0xFF
	std::uint32_t id = m.addCharacter(m.addEditText("Text", "", 0, 14.0f));
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "t")) } });
	CanvasFx c;
	REQUIRE(c.fx.load(0, "A", m));
	AptRenderList rl;
	c.fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 1);
	CHECK(rl.commands[0].textColor[0] == 0);
	CHECK(rl.commands[0].textColor[1] == 0);
	CHECK(rl.commands[0].textColor[2] == 255);
	CHECK(rl.commands[0].textColor[3] == 255);
	AptCanvasList list = c.build();
	REQUIRE(list.ops.size() == 1);
	CHECK(list.ops[0].textColor[0] == doctest::Approx(0.0f));
	CHECK(list.ops[0].textColor[2] == doctest::Approx(1.0f));
}

TEST_CASE("retail Palantir: the placeholder numbers of the resource and power counters are gold 0xFFCC00, not cyan")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::string error;
	std::shared_ptr<const AptFile> f = loader.loadMovie("Palantir", &error);
	REQUIRE_MESSAGE(f, error);
	std::size_t seen = 0;
	for (const AptCharacter &ch : f->characters)
	{
		if (ch.type == APT_CHAR_EDITTEXT && ch.text && (ch.text->initialText == "999999" || ch.text->initialText == "x99" || ch.text->initialText == "999/999"))
		{
			++seen;
			CHECK(ch.text->color[0] == 255);
			CHECK(ch.text->color[1] == 204);
			CHECK(ch.text->color[2] == 0);
			CHECK(ch.text->color[3] == 255);
		}
	}
	CHECK(seen == 3);
}

TEST_CASE("retail place objects: the alpha fades of colour transforms are in file byte 3 (0xAARRGGBB, bytes B, G, R, A)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	std::size_t total = 0;
	std::size_t notFull[4] = { 0, 0, 0, 0 };
	for (const std::string &movie : source.listMovies())
	{
		std::string error;
		std::shared_ptr<const AptFile> f = loader.loadMovie(movie, &error);
		REQUIRE_MESSAGE(f, movie << ": " << error);
		auto visit = [&](const std::vector<AptFrame> &frames) {
			for (const AptFrame &frame : frames)
			{
				for (const AptFrameItem &item : frame.items)
				{
					if (item.type == APT_ITEM_PLACEOBJECT && item.place && (item.place->flags & APT_PLACE_HASCOLORTRANSFORM))
					{
						++total;
						for (int k = 0; k < 4; ++k)
						{
							notFull[k] += item.place->tint[k] != 255 ? 1 : 0;
						}
					}
				}
			}
		};
		visit(f->frames);
		for (const AptCharacter &c : f->characters)
		{
			if (c.type == APT_CHAR_SPRITE)
			{
				visit(c.frames);
			}
		}
	}
	// the numbers are retail data facts (the same in every run): the colour bytes 0-2 are rarely tinted, byte 3 almost always
	CHECK(total == 26119);
	CHECK(notFull[0] == 1003);
	CHECK(notFull[1] == 1004);
	CHECK(notFull[2] == 1003);
	CHECK(notFull[3] == 24841);
}

// ---- retail: the main menu --------------------------------------------------------------------------------------------------------------

namespace
{

struct RetailMenu
{
	AptArchiveFileSource source;
	AptStubHost host;
	std::unique_ptr<Apt> apt;
	AptTextureStore textures;
	GameTextTable strings;
	FontSubstitution fonts;

	explicit RetailMenu(AptRetail &mount) : source(mount.fs), textures(source)
	{
		host.setExternValue("MainMenuUnlockBonusCampaign", "0");
		apt = std::make_unique<Apt>(source, host);
		std::string error;
		REQUIRE_MESSAGE(apt->loadMovie(0, "AptLevel0", &error), error);
		REQUIRE_MESSAGE(apt->loadMovie(1, "MainMenu", &error), error);
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(source.readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(strings.parse(bytes, &error), error);
		REQUIRE_MESSAGE(source.readFile("data/ini/fontsubstitution.ini", bytes, &error), error);
		REQUIRE_MESSAGE(fonts.parse(std::string(bytes.begin(), bytes.end()), &error), error);
	}
	void tick(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			apt->update(33);
		}
	}
	AptCanvasList canvas(float w = 1024, float h = 768, bool merge = true)
	{
		AptRenderList rl;
		apt->buildRenderList(rl);
		AptCanvasInputs in;
		in.mapping.windowW = w;
		in.mapping.windowH = h;
		in.textures = &textures;
		in.text = &strings;
		in.fonts = &fonts;
		AptCanvasList out;
		BuildAptCanvas(rl, in, out, merge);
		return out;
	}
};

std::size_t renderListTriangles(const AptRenderList &rl)
{
	std::size_t n = 0;
	for (const AptRenderCommand &c : rl.commands)
	{
		for (const AptRenderFill &f : c.fills)
		{
			if (!f.style)
			{
				continue;
			}
			if (f.kind == APT_STYLE_LINE)
			{
				n += f.style->lines.size() / 4 * 2;
			}
			else if (!(f.kind == APT_STYLE_TEXTURED && !f.imageResolved))
			{
				n += f.style->triangles.size() / 6;
			}
		}
	}
	return n;
}

} // namespace

TEST_CASE("retail MainMenu canvas: per frame the triangle count equals the render list's, every texture loads, every label resolves, masks balance")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailMenu menu(mount);
	std::string error;
	std::size_t lastOps = 0;
	std::size_t maxTriangles = 0;
	std::set<std::string> textures;
	for (int frame = 0; frame < 90; ++frame)
	{
		menu.tick(1);
		if (frame == 10)
		{
			REQUIRE_MESSAGE(menu.apt->invoke(menu.apt->level(1), "ShowMainMenu", {}, nullptr, &error), error);
		}
		AptRenderList rl;
		menu.apt->buildRenderList(rl);
		AptCanvasList merged = menu.canvas();
		AptCanvasList plain = menu.canvas(1024, 768, false);
		INFO("frame " << frame);
		CHECK(merged.errors.empty());
		CHECK(merged.triangles == renderListTriangles(rl));
		CHECK(plain.triangles == merged.triangles);
		CHECK(merged.ops.size() <= plain.ops.size());
		CHECK(merged.count(AptCanvasOp::Kind::MaskBegin) == merged.count(AptCanvasOp::Kind::MaskEnd));
		CHECK(merged.count(AptCanvasOp::Kind::MaskBegin) == merged.count(AptCanvasOp::Kind::MaskContent));
		maxTriangles = std::max(maxTriangles, merged.triangles);
		lastOps = merged.ops.size();
		for (const AptCanvasOp &op : merged.ops)
		{
			if (!op.texture.empty())
			{
				textures.insert(op.texture);
			}
		}
		CHECK_MESSAGE(merged.missingLabels.empty(), "missing label " << (merged.missingLabels.empty() ? "" : merged.missingLabels[0]));
	}
	CHECK(lastOps > 0);
	CHECK(maxTriangles == 28); // frames 0-8: 28 triangles; the menu is light (the 3D shell background is a View3D placeholder, S-136)
	CHECK_FALSE(textures.empty());
	// the Quit label is the string table's QUIT
	AptCanvasList list = menu.canvas();
	bool quit = false;
	for (const AptCanvasOp &op : list.ops)
	{
		if (op.kind == AptCanvasOp::Kind::Text && op.path == "_level1.QuitMainMenu.instance3.Text")
		{
			quit = true;
			CHECK(op.text == "QUIT");
			CHECK(op.fontName == "Albertus MT");
			CHECK(op.drawFont == "Albertus MT"); // no substitution entry for it
		}
	}
	CHECK(quit);
}

TEST_CASE("retail MainMenu: the clip the script tags _type = RenderImage is one reported Placeholder, not magenta art (stop S-136)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	RetailMenu menu(mount);
	menu.tick(10);
	std::string error;
	REQUIRE_MESSAGE(menu.apt->invoke(menu.apt->level(1), "ShowMainMenu", {}, nullptr, &error), error);
	menu.tick(60);
	AptCanvasList list = menu.canvas();
	std::vector<const AptCanvasOp *> placeholders;
	for (const AptCanvasOp &op : list.ops)
	{
		if (op.kind == AptCanvasOp::Kind::Placeholder)
		{
			placeholders.push_back(&op);
		}
		// nothing of the authored magenta box (204, 0, 255) is drawn
		if (op.kind == AptCanvasOp::Kind::Mesh)
		{
			CHECK_FALSE((std::abs(op.colors[0] - 0.8f) < 0.01f && op.colors[1] < 0.01f && std::abs(op.colors[2] - 1.0f) < 0.01f));
		}
	}
	REQUIRE(placeholders.size() == 1);
	CHECK(placeholders[0]->path == "_level1.Image");
	CHECK(placeholders[0]->nativeTag);
	CHECK(placeholders[0]->symbolName == "RenderImage");
	CHECK(placeholders[0]->symbolMovie.empty());
	CHECK(placeholders[0]->bounds[2] - placeholders[0]->bounds[0] == doctest::Approx(100.0f)); // bounded by the authored art (its own space)
	CHECK(placeholders[0]->bounds[3] - placeholders[0]->bounds[1] == doctest::Approx(100.0f));
}

TEST_CASE("TextField.textColor: the instance starts with the file colour; the setter keeps the low 24 bits and forces alpha 255; the getter returns the low 24 bits (BFME2 0x00AEEB20, 0x00AEFE89)")
{
	TestMovie m;
	std::uint32_t font = m.addFont("Albertus MT", {});
	m.addCharacter(font);
	// file dword 0xFF0000FF: B = 0xFF (blue), alpha 0xFF
	std::uint32_t id = m.addCharacter(m.addEditText("Text", "", 0, 14.0f));
	// a second root frame's script: b.textColor = 0x7FAABB with a stray top byte 0x12 that must not survive (asymmetric channels)
	std::uint32_t go = program(m, [&](Asm &a) {
		a.getStringVar("b").pushString("textColor").pushLong((int)0x127FAABBu);
		a.op(APT_OP_SETMEMBER);
	});
	m.setRootFrames({ { m.addPlaceItem(placeChar(id, 1, "a")), m.addPlaceItem(placeChar(id, 2, "b")) }, { m.addActionItem(go) } });
	CanvasFx c;
	REQUIRE(c.fx.load(0, "A", m));
	// the script has not run yet: both keep the file colour
	CHECK(static_cast<AptTextInst *>(c.fx.at("b"))->colorArgb == 0xFF0000FFu);
	c.fx.step();
	CHECK(static_cast<AptTextInst *>(c.fx.at("b"))->colorArgb == 0xFF7FAABBu); // set by the VM, alpha forced
	static_cast<AptTextInst *>(c.fx.at("b"))->colorArgb = 0xFF0000FFu;
	AptTextInst *a = static_cast<AptTextInst *>(c.fx.at("a"));
	AptTextInst *b = static_cast<AptTextInst *>(c.fx.at("b"));
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->colorArgb == 0xFF0000FFu);
	AptValue v;
	REQUIRE(a->getOwn("textColor", v));
	CHECK(v.toInteger() == 0x0000FF);
	a->setOwn("TEXTCOLOR", AptValue::number((float)0x7FAABB));
	REQUIRE(a->getOwn("textColor", v));
	CHECK(v.toInteger() == 0x7FAABB);
	CHECK(a->colorArgb == 0xFF7FAABBu);
	a->setOwn("textColor", AptValue::number(0.0f)); // black is a colour: alpha 255, not "unset"
	CHECK(a->colorArgb == 0xFF000000u);
	a->setOwn("textColor", AptValue::number((float)0x7FAABB));
	// the render list follows the instance; the other instance and the shared character keep the file colour
	AptRenderList rl;
	c.fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 2);
	CHECK(rl.commands[0].textColor[0] == 0x7F);
	CHECK(rl.commands[0].textColor[1] == 0xAA);
	CHECK(rl.commands[0].textColor[2] == 0xBB);
	CHECK(rl.commands[0].textColor[3] == 255);
	CHECK(rl.commands[1].textColor[0] == 0);
	CHECK(rl.commands[1].textColor[2] == 255);
	CHECK(b->charRef().character->text->color[2] == 255);
}

TEST_CASE("retail Skirmish: SetPageTitle assigns the title's textColor (0x7FAABB, blue-grey), so the title is not the file's red")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptStubHost host;
	host.setExternValue("AptMapPreview::GameMapType", "OpenPlay");
	Apt apt(source, host);
	std::string error;
	REQUIRE_MESSAGE(apt.loadMovie(0, "AptLevel0", &error), error);
	REQUIRE_MESSAGE(apt.loadMovie(1, "Skirmish", &error), error);
	for (int f = 0; f < 100; ++f)
	{
		apt.update(33);
	}
	AptRenderList rl;
	apt.buildRenderList(rl);
	const AptRenderCommand *title = nullptr;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Text && c.path.find(".Title.Title_Text") != std::string::npos)
		{
			title = &c;
		}
	}
	REQUIRE(title);
	CHECK(title->textColor[0] == 127);
	CHECK(title->textColor[1] == 170);
	CHECK(title->textColor[2] == 187);
	CHECK(title->textColor[3] == 255);
}

TEST_CASE("retail: every apt_*.tga texture the .dat maps of the corpus name decodes (type 2, 24/32 bit) and is flipped to top-first")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptTextureStore store(source);
	std::size_t decoded = 0;
	std::string failures;
	for (const std::string &movie : source.listMovies())
	{
		AptImageMap map;
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!source.readFile(movie + ".dat", bytes, &error))
		{
			continue; // a movie without textures
		}
		REQUIRE_MESSAGE(AptImageMap::parse(bytes, map, &error), movie << ": " << error);
		for (const AptImageMapEntry &e : map.entries)
		{
			const std::string name = "apt_" + movie + "_" + std::to_string(e.isRect ? e.imageId : e.textureId) + ".tga";
			const AptTextureStore::Entry &t = store.get(name);
			if (!t.ok)
			{
				failures += name + ": " + t.error + "; ";
			}
			else
			{
				++decoded;
				CHECK((t.pixelsReleased || t.rgba.size() == (std::size_t)t.width * (std::size_t)t.height * 4));
			}
			store.releasePixels(name);
		}
	}
	CHECK_MESSAGE(failures.empty(), failures);
	CHECK(decoded > 100);
}

TEST_CASE("retail MainMenu: the atlas pixels sit where the tc matrix says (v counts from the top of the flipped rows)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptTextureStore store(source);
	// MainMenu image 41 is a chain strip: shape 44's `tc` places it at atlas pixels x 438..486, y 1..432.  In the flipped (top-first)
	// pixels those rows hold the chain; nothing is below it.
	const AptTextureStore::Entry &t = store.get("apt_MainMenu_1.tga");
	REQUIRE(t.ok);
	CHECK(t.width == 1024);
	CHECK(t.height == 512);
	auto alphaAt = [&](int x, int y) { return t.rgba[((std::size_t)y * t.width + x) * 4 + 3]; };
	auto rowHasPixels = [&](int y) {
		for (int x = 438; x < 486; ++x)
		{
			if (alphaAt(x, y) > 0)
			{
				return true;
			}
		}
		return false;
	};
	int chainRows = 0, emptyRows = 0;
	for (int y = 1; y <= 432; ++y)
	{
		chainRows += rowHasPixels(y) ? 1 : 0;
	}
	for (int y = 440; y < 512; ++y)
	{
		emptyRows += rowHasPixels(y) ? 0 : 1;
	}
	CHECK(chainRows >= 400);
	CHECK(emptyRows >= 70);
}

// What this pins (stop S-139): over the 86 movies after 100 frames each, every texture the shapes name loads, and the only render
// errors are image ids that no `.dat` maps - 12 shapes in 9 movies.  Those images are not in the movie's texture list; the
// shapes are the movies' placeholders for pictures the engine supplies at run time (inference: nothing in the movie fills them), so the
// Godot renderer draws nothing for them and reports the error.  The labels the string table lacks (`APT:MyPowerLevel_3` ...) are
// the keys the engine sets or builds at run time (spec 2.6: 179 of 232 `$` constants resolve); they are shown as the label.
TEST_CASE("retail corpus canvas: every movie translates; textures load; the render errors are the unmapped image ids (stop S-139)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	GameTextTable strings;
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(source.readFile("data/lotr.str", bytes, &error), error);
	REQUIRE_MESSAGE(strings.parse(bytes, &error), error);
	AptTextureStore textures(source);
	FontSubstitution fontTable;
	REQUIRE_MESSAGE(source.readFile("data/ini/fontsubstitution.ini", bytes, &error), error);
	REQUIRE_MESSAGE(fontTable.parse(std::string(bytes.begin(), bytes.end()), &error), error);
	std::set<std::string> errors;
	std::set<std::string> missing;
	std::set<std::string> unverified;
	std::set<std::string> drawFonts;
	std::size_t movies = 0, withContent = 0;
	for (const std::string &name : source.listMovies())
	{
		AptStubHost host;
		Apt apt(source, host);
		REQUIRE_MESSAGE(apt.loadMovie(1, name, &error), name << ": " << error);
		for (int f = 0; f < 100; ++f)
		{
			apt.update(33);
		}
		AptRenderList rl;
		apt.buildRenderList(rl);
		AptCanvasInputs in;
		in.textures = &textures;
		in.text = &strings;
		in.fonts = &fontTable;
		AptCanvasList list;
		BuildAptCanvas(rl, in, list);
		++movies;
		withContent += list.ops.empty() ? 0 : 1;
		for (const std::string &e : list.errors)
		{
			errors.insert(e);
		}
		for (const std::string &l : list.missingLabels)
		{
			missing.insert(name + ": " + l);
		}
		for (const std::string &u : list.unverified)
		{
			unverified.insert(u);
		}
		for (const AptCanvasOp &op : list.ops)
		{
			if (op.kind == AptCanvasOp::Kind::Text)
			{
				drawFonts.insert(op.drawFont);
			}
			if (op.kind == AptCanvasOp::Kind::Mesh)
			{
				CHECK(op.positions.size() % 6 == 0);
				CHECK(op.colors.size() == op.positions.size() * 2);
				CHECK((op.uvs.empty() || op.uvs.size() == op.positions.size()));
			}
		}
	}
	CHECK(movies == 86);
	CHECK(withContent > 60);
	const std::set<std::string> expected = {
		"cahpowers shape 134: image 35 has no .dat entry",
		"cahpowers shape 36: image 35 has no .dat entry",
		"mpgamesetup shape 373: image 372 has no .dat entry",
		"onlinequickmatch shape 2: image 1 has no .dat entry",
		"options shape 133: image 86 has no .dat entry",
		"options shape 87: image 86 has no .dat entry",
		"playertribute shape 45: image 18 has no .dat entry",
		"scorescreen shape 162: image 161 has no .dat entry",
		"scorescreen shape 34: image 33 has no .dat entry",
		"scorescreen shape 37: image 36 has no .dat entry",
		"skirmish shape 40: image 39 has no .dat entry",
		"timeline shape 71: image 70 has no .dat entry",
	};
	std::string got;
	for (const std::string &e : errors)
	{
		got += "\n  " + e;
	}
	CHECK_MESSAGE(errors == expected, "render errors:" << got);
	// the fonts the 100-frame screens draw (stop S-132): the corpus ships the first two files; Times New Roman is a system font it does not ship
	std::string fontList;
	for (const std::string &f : drawFonts)
	{
		fontList += f + "; ";
	}
	CHECK_MESSAGE(drawFonts == (std::set<std::string>{ "Albertus MT", "Omnia LT Std" }), "fonts drawn: " << fontList);
	// 329 before lane HUD-1: a clip converts to its target path (AptCharacterInst::displayString), so the labels the movies build from it (`$_level1.<clip>_ProductionCount`) are 84 more pairs the engine supplies at run time
	CHECK_MESSAGE(missing.size() == 413, missing.size() << " movie/label pairs the string table lacks");
	const std::set<std::string> expectedUnverified = { "clip-layer", "font-size", "line-width", "mask-alpha", "rect-image", "stage-mapping", "static-text-not-drawn", "text-layout", "tga-origin", "uv-matrix-order" };
	std::string uv;
	for (const std::string &u : unverified)
	{
		uv += u + " ";
	}
	CHECK_MESSAGE(unverified == expectedUnverified, "unverified: " << uv);
}

// ---- the registry -------------------------------------------------------------------------------------------------------------------

TEST_CASE("stops S-130..S-139 are registered in docs/STOPS.md with the identities the renderer reports at run time")
{
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md", std::ios::binary);
	REQUIRE_MESSAGE(in, "cannot open docs/STOPS.md");
	std::stringstream buffer;
	buffer << in.rdbuf();
	const std::string doc = buffer.str();
	struct Row
	{
		const char *id;
		std::vector<const char *> identities;
	};
	const std::vector<Row> rows = {
		{ "S-130", { "stage-mapping" } },
		{ "S-131", { "duplicate_labels", "missing_labels" } },
		{ "S-132", { "font-size", "font_fallbacks" } },
		{ "S-133", { "text-layout", "textColor" } },
		{ "S-134", { "uv-matrix-order", "tga-origin", "rect-image" } },
		{ "S-135", { "line-width", "mask-alpha", "AptRetailVertexColour" } },
		{ "S-136", { "placeholders", "static-text-not-drawn", "RenderImage", "LogoWithShadow" } },
		{ "S-137", { "key-codes", "mouse-wheel", "click-through" } },
		{ "S-138", { "SHELL push" } },
		{ "S-139", { "has no .dat entry" } },
	};
	for (const Row &row : rows)
	{
		const std::string key = std::string("| ") + row.id + " |";
		const std::size_t at = doc.find(key);
		REQUIRE_MESSAGE(at != std::string::npos, row.id << " is not a row of docs/STOPS.md");
		std::size_t end = doc.find('\n', at);
		const std::string line = doc.substr(at, end == std::string::npos ? std::string::npos : end - at);
		for (const char *identity : row.identities)
		{
			CHECK_MESSAGE(line.find(identity) != std::string::npos, row.id << " does not name `" << identity << "`");
		}
		CHECK_MESSAGE(line.find("| code |") != std::string::npos, row.id << " is not a code stop");
	}
}
