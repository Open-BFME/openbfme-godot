// OpenBFME tests of the Palantir's PlayerMagic button (lane HUD-3): the Evenstar / Ring art of its `_up` frame (EnablePlayerMagicButton) drawn where the movie
// places it, and the text layout of RotWK's Apt display string draw (PlaceAptText, RW 0x4A8F95) for the rank number. The expected values come from Palantir.apt
// itself (placements read with AptFile) and the retail arithmetic. The retail cases SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cmath>
#include <memory>

using namespace hudtest;

TEST_CASE("hud3 text layout: RotWK's Apt display string places a string by its alignment, centres a single-line field vertically and squeezes a wide one")
{
	// a box 100 x 40 at (10.5, 20.25), a string 30 x 16
	AptTextPlacement p = PlaceAptText(10.5f, 20.25f, 110.5f, 60.25f, 30.0f, 16.0f, 0, false, false);
	CHECK(p.x == 10.0f);                                  // left: the box's left edge, truncated
	CHECK(p.y == (float)(int)(20.25f + (40.0f - 16.0f) * 0.5f)); // centred vertically: 32
	CHECK(p.y == 32.0f);
	CHECK(p.centredVertically);
	CHECK(p.squeezeX == 1.0f);
	p = PlaceAptText(10.5f, 20.25f, 110.5f, 60.25f, 30.0f, 16.0f, 1, false, false);
	CHECK(p.x == 80.0f); // right: 110.5 - 30, truncated
	p = PlaceAptText(10.5f, 20.25f, 110.5f, 60.25f, 30.0f, 16.0f, 2, false, false);
	CHECK(p.x == 45.0f); // centre: 10.5 + (100 - 30) / 2 = 45.5, truncated
	p = PlaceAptText(10.5f, 20.25f, 110.5f, 60.25f, 30.0f, 16.0f, 3, false, false);
	CHECK(p.x == 10.0f); // justify is drawn at the left (RW 0x4A914B: only 1 and 2 move the string)
	// multiline alone or word wrap alone: still one centred line; both: from the top
	CHECK(PlaceAptText(0, 0, 100, 40, 30, 16, 0, true, false).centredVertically);
	CHECK(PlaceAptText(0, 0, 100, 40, 30, 16, 0, false, true).centredVertically);
	p = PlaceAptText(0, 0, 100, 40, 30, 16, 0, true, true);
	CHECK_FALSE(p.centredVertically);
	CHECK(p.y == 0.0f);
	// wider than the box: squeezed to the box, so a centred string starts at the left edge
	p = PlaceAptText(0, 0, 50, 40, 80, 16, 2, false, false);
	CHECK(p.squeezeX == doctest::Approx(50.0f / 80.0f));
	CHECK(p.x == 0.0f);
}

namespace
{
std::vector<std::uint8_t> readRetail(SharedWorld &s, const std::string &name)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(name, bytes, &err), err);
	return bytes;
}

// the last placement named `name` (or of `character` at `depth` when name is empty) in a sprite's timeline up to and including frame `frame`
const AptPlaceObject *placement(const AptFile &f, const std::vector<AptFrame> &frames, const std::string &name, int depth, int frame)
{
	const AptPlaceObject *found = nullptr;
	for (int i = 0; i <= frame && i < (int)frames.size(); ++i)
	{
		for (const AptFrameItem &it : frames[(size_t)i].items)
		{
			if (it.type == APT_ITEM_PLACEOBJECT && it.place && ((!name.empty() && it.place->name == name) || (name.empty() && it.place->depth == depth)))
			{
				found = it.place.get();
			}
		}
	}
	return found;
}

int labelFrame(const std::vector<AptFrame> &frames, const std::string &label)
{
	for (size_t i = 0; i < frames.size(); ++i)
	{
		for (const AptFrameItem &it : frames[i].items)
		{
			if (it.type == APT_ITEM_FRAMELABEL && it.label == label)
			{
				return (int)i;
			}
		}
	}
	return -1;
}

const AptCharacter *character(const AptFile &f, std::uint32_t id)
{
	for (const AptCharacter &c : f.characters)
	{
		if (c.id == id)
		{
			return &c;
		}
	}
	return nullptr;
}

// a 2x3 affine matrix (a b c d tx ty), parent * child
struct M
{
	double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
M mul(const M &p, const AptPlaceObject &o)
{
	M r;
	r.a = p.a * o.matrix[0] + p.c * o.matrix[1];
	r.b = p.b * o.matrix[0] + p.d * o.matrix[1];
	r.c = p.a * o.matrix[2] + p.c * o.matrix[3];
	r.d = p.b * o.matrix[2] + p.d * o.matrix[3];
	r.tx = p.a * o.translation[0] + p.c * o.translation[1] + p.tx;
	r.ty = p.b * o.translation[0] + p.d * o.translation[1] + p.ty;
	return r;
}

struct PalantirRig3
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	PalantirRig3(SharedWorld &s, const char *localName) : rig(s)
	{
		Player *p = rig.game->players().findPlayerWithName(localName);
		REQUIRE(p != nullptr);
		rig.game->players().setLocalPlayer(p);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		for (int i = 0; i < 60; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
};

void checkButton(SharedWorld &s, const char *localName, const char *label, std::uint32_t upImage)
{
	// Palantir.apt, read on its own
	auto apt = std::make_shared<std::vector<std::uint8_t>>(readRetail(s, "Palantir.apt"));
	AptConstFile cf;
	std::string err;
	REQUIRE_MESSAGE(AptConstFile::parse(readRetail(s, "Palantir.const"), cf, &err), err);
	AptFile f;
	REQUIRE_MESSAGE(AptFile::parse("Palantir", apt, cf, f, &err), err);

	PalantirRig3 h(s, localName);
	AptRenderList rl;
	h.hud->apt().buildRenderList(rl);

	// the chain of placements down to the PlayerMagic button: root PalantirButtons -> Buttons (its _show) -> PlayerMagic (_double) -> ButtonClip (`label`)
	const AptPlaceObject *pb = placement(f, f.frames, "PalantirButtons", 0, (int)f.frames.size() - 1);
	REQUIRE(pb != nullptr);
	const AptCharacter *pbc = character(f, (std::uint32_t)pb->characterId);
	REQUIRE(pbc != nullptr);
	const AptPlaceObject *buttons = placement(f, pbc->frames, "Buttons", 0, labelFrame(pbc->frames, "_show"));
	REQUIRE(buttons != nullptr);
	const AptCharacter *bc = character(f, (std::uint32_t)buttons->characterId);
	const AptPlaceObject *magic = placement(f, bc->frames, "PlayerMagic", 0, labelFrame(bc->frames, "_double"));
	REQUIRE(magic != nullptr);
	const AptCharacter *mc = character(f, (std::uint32_t)magic->characterId);
	const AptPlaceObject *clip = placement(f, mc->frames, "ButtonClip", 0, labelFrame(mc->frames, label));
	REQUIRE(clip != nullptr);
	const AptCharacter *cc = character(f, (std::uint32_t)clip->characterId);
	// in the clip's `_up` frame: the art shape whose bitmap is the `_up` image (depth 3)
	const AptPlaceObject *art = placement(f, cc->frames, "", 3, labelFrame(cc->frames, "_up"));
	REQUIRE(art != nullptr);
	const AptCharacter *shape = character(f, (std::uint32_t)art->characterId);
	REQUIRE(shape != nullptr);
	REQUIRE(shape->type == APT_CHAR_SHAPE);
	const M expected = mul(mul(mul(mul(mul(M(), *pb), *buttons), *magic), *clip), *art);

	// the movie draws that shape with the `_up` image (EnablePlayerMagicButton("1")), at the matrix of the chain and over the shape's bounds
	const AptRenderCommand *drawn = nullptr;
	bool flat = false;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind != AptRenderCommand::Kind::Shape || c.path.find("PalantirButtons.Buttons.PlayerMagic.ButtonClip") == std::string::npos)
		{
			continue;
		}
		for (const AptRenderFill &fill : c.fills)
		{
			if (fill.imageId == (std::int32_t)upImage)
			{
				drawn = &c;
			}
			flat = flat || fill.imageId == 162 || fill.imageId == 176; // the `_disabled` art
		}
	}
	REQUIRE_MESSAGE(drawn != nullptr, "the `_up` art " << upImage << " is not drawn");
	CHECK_FALSE(flat);
	CHECK(drawn->shapeCharacterId == shape->id);
	CHECK(drawn->matrix.a == doctest::Approx(expected.a));
	CHECK(drawn->matrix.d == doctest::Approx(expected.d));
	CHECK(drawn->matrix.tx == doctest::Approx(expected.tx));
	CHECK(drawn->matrix.ty == doctest::Approx(expected.ty));
	// the shape's bounds (0, 0, 37, 37) at that matrix: the emblem's rectangle in stage pixels
	CHECK(shape->bounds[2] - shape->bounds[0] == doctest::Approx(37.0f));
	CHECK(expected.a * (shape->bounds[2] - shape->bounds[0]) == doctest::Approx(37.0));

	// the rank number: eight edit texts (an outline of seven and the white one) in rankText; alignment 2, single line, the box from the field's bounds
	const AptPlaceObject *rank = placement(f, mc->frames, "rankText", 0, 0);
	REQUIRE(rank != nullptr);
	const AptCharacter *rc = character(f, (std::uint32_t)rank->characterId);
	const AptPlaceObject *white = placement(f, rc->frames, "", 8, 0);
	REQUIRE(white != nullptr);
	const AptCharacter *field = character(f, (std::uint32_t)white->characterId);
	REQUIRE(field != nullptr);
	REQUIRE(field->type == APT_CHAR_EDITTEXT);
	const AptTextInfo &ti = *field->text;
	CHECK(ti.alignment == 2);
	CHECK_FALSE(ti.multiline);
	CHECK(ti.fontHeight == 26.0f);
	const M m = mul(mul(mul(mul(M(), *pb), *buttons), *magic), *rank);
	const M mw = mul(m, *white);
	const AptRenderCommand *text = nullptr;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Text && c.path.find("PlayerMagic.rankText") != std::string::npos && c.textColor[0] == 255)
		{
			text = &c;
		}
	}
	REQUIRE(text != nullptr);
	CHECK(text->alignment == 2);
	CHECK(text->matrix.tx == doctest::Approx(mw.tx));
	CHECK(text->matrix.ty == doctest::Approx(mw.ty));
	// laid out as retail: a 12 x 31 string (Omnia's "1" at 26 pixels) centred in the box both ways
	const double bx0 = mw.a * ti.bounds[0] + mw.tx, by0 = mw.d * ti.bounds[1] + mw.ty, bx1 = mw.a * ti.bounds[2] + mw.tx, by1 = mw.d * ti.bounds[3] + mw.ty;
	const AptTextPlacement p = PlaceAptText((float)bx0, (float)by0, (float)bx1, (float)by1, 12.0f, 31.0f, text->alignment, text->multiline, text->wordWrap);
	CHECK(p.centredVertically);
	CHECK(p.x == (float)(int)((float)((bx1 - bx0 - 12.0) * 0.5 + bx0)));
	CHECK(p.y == (float)(int)((float)((by1 - by0 - 31.0) * 0.5 + by0)));
	// the string's centre is the button's: the field centre within 2 stage pixels of the art's centre
	const double artCx = expected.tx + expected.a * 18.5, artCy = expected.ty + expected.d * 18.5;
	CHECK(std::fabs((bx0 + bx1) * 0.5 - artCx) < 2.5);
	CHECK(std::fabs((by0 + by1) * 0.5 - artCy) < 2.5);
}
} // namespace

TEST_CASE("hud3 palantir: the good side's PlayerMagic button shows the `_up` Evenstar where the movie places it, the rank number is centred on it")
{
	if (!haveWorld("hud3 evenstar"))
	{
		return;
	}
	checkButton(shared(), "Player_1", "_evenstar", 205);
}

TEST_CASE("hud3 palantir: the evil side's PlayerMagic button shows the `_up` Ring where the movie places it, the rank number is centred on it")
{
	if (!haveWorld("hud3 ring"))
	{
		return;
	}
	checkButton(shared(), "Player_2", "_ring", 219);
}

// ---- round 2: the PlayerMagic progress wedge and the bitmap-fill texture coordinates ----

#include "GameClient/GameText.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptLoad.h"

TEST_CASE("hud3 palantir: the clips an engine call creates first advance in the next step, so PlayerMagic.ProgressBar stops on frame 0 (no 1% wedge)")
{
	if (!haveWorld("hud3 progress"))
	{
		return;
	}
	PalantirRig3 h(shared(), "Player_1");
	AptCharacterInst *c = h.hud->apt().resolvePath(h.hud->apt().level(h.hud->palantir()->level()), "PalantirButtons.Buttons.PlayerMagic.ProgressBar");
	REQUIRE(c != nullptr);
	REQUIRE(c->asSprite() != nullptr);
	// SetPalantirFrameState / SetPlayerButtonsState create PlayerMagic and its ProgressBar inside an engine call (BFME2 0x00ACCB80 does not flush new
	// instances or run the pool): the next step advances the ProgressBar to frame 0 and runs its frame-0 Stop there. Retail's progress (RW 0x6D5C89) is 1 at
	// the start, equal to the cached 1 (RW 0x6D68xx +0x88), so SetPlayerMagicProgress is not sent and the bar stays on frame 0.
	CHECK(c->asSprite()->frame == 0);
	CHECK_FALSE(c->asSprite()->playing);
	// on frame 0 the ring piece (char 190 at depth 2) is unrotated: the radial wipe shows nothing of it
	AptRenderList rl;
	h.hud->apt().buildRenderList(rl);
	bool ring = false;
	for (const AptRenderCommand &cmd : rl.commands)
	{
		if (cmd.kind == AptRenderCommand::Kind::Shape && cmd.path.find("PlayerMagic.ProgressBar.instance2") != std::string::npos)
		{
			ring = true;
			CHECK(cmd.matrix.b == 0.0f);
			CHECK(cmd.matrix.c == 0.0f);
		}
	}
	CHECK(ring);
}

TEST_CASE("hud3 apt canvas: a bitmap fill's texture coordinates are the fill matrix applied to the vertex over the texture size (shape 206 of Palantir.apt)")
{
	if (!haveWorld("hud3 uv"))
	{
		return;
	}
	SharedWorld &s = shared();
	// Palantir_geometry/206.ru: `s tc:255:255:255:255:205:1:0:0:1:487:188` (the fill's matrix a b c d tx ty = 1 0 0 1 487 188 over image 205 in
	// apt_Palantir_1.tga, Palantir.dat `205->1`), triangles over (0, 0) - (37, 37). The rule: uv = (a x + c y + tx, b x + d y + ty) / (texture width, height),
	// no inset (the fill matrix maps shape units to texels).
	PalantirRig3 h(s, "Player_1");
	AptArchiveFileSource source(*s.mount->fs);
	AptTextureStore textures(source);
	GameTextTable strings;
	AptRenderList rl;
	h.hud->apt().buildRenderList(rl);
	// the `_up` Evenstar art (shape 206, the same record form as 163) as a list of its own
	AptRenderList one;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Shape && c.shapeCharacterId == 206)
		{
			one.commands.push_back(c); // the `_up` Evenstar: `tc:...:205:1:0:0:1:487:188`
		}
	}
	REQUIRE(one.commands.size() == 1);
	AptCanvasInputs in;
	in.mapping.windowW = 1024;
	in.mapping.windowH = 768;
	in.textures = &textures;
	in.text = &strings;
	AptCanvasList out;
	BuildAptCanvas(one, in, out, false);
	REQUIRE(out.ops.size() == 1);
	const AptCanvasOp &op = out.ops[0];
	CHECK(op.texture == "apt_Palantir_1.tga");
	const AptTextureStore::Entry &e = textures.get(op.texture);
	REQUIRE(e.ok);
	CHECK(e.width == 1024);
	CHECK(e.height == 512);
	const AptMatrix &m = one.commands[0].matrix;
	REQUIRE(!op.uvs.empty());
	for (size_t v = 0; v < op.positions.size() / 2; ++v)
	{
		// back from the window position to the shape's own units (a 1024 x 768 window is the stage), then the retail rule
		const float sx = op.positions[v * 2], sy = op.positions[v * 2 + 1];
		const float lx = (sx - m.tx) / m.a, ly = (sy - m.ty) / m.d;
		CHECK(op.uvs[v * 2] == doctest::Approx((lx + 487.0f) / 1024.0f).epsilon(1e-5));
		CHECK(op.uvs[v * 2 + 1] == doctest::Approx((ly + 188.0f) / 512.0f).epsilon(1e-5));
	}
}
