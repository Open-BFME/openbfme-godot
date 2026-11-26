// OpenBFME. GPL-3.0. W3DTornadoDraw: both editions, RW 0x004D102E / BFME2 0x004D181D. Synthetic INI and W3D only; no retail files.
#include "doctest.h"
#include "ObjectTestUtil.h"
#include "W3dSynth.h"

#include "Common/BfmeGame.h"
#include "Common/GameEdition.h"
#include "Common/INI.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Common/Thing/Thing.h"
#include "GameClient/Drawable.h"
#include "GameClient/SelectionDecals.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTornadoDraw.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
const char kTornadoIni[] =
    "Object TestTornadoObj\n"
    "  Draw = W3DTornadoDraw ModuleTag_TornadoDraw\n"
    "    DecalTemplate\n"
    "      Texture = EXWhirlwindground\n"
    "      Style = SHADOW_ALPHA_DECAL\n"
    "      OpacityMin = 5%\n"
    "      OpacityMax = 40%\n"
    "      MaxRadius = 100.0\n"
    "      RotationsPerMinute = 0.5\n"
    "      SpiralAcceleration = 1.0\n"
    "    End\n"
    "    DecalCount = 7\n"
    "    DecalMaxRadius = 300.0\n"
    "  End\n"
    "End\n";
} // namespace

TEST_CASE("w3d tornado draw: module data defaults and field table (both editions: RW 0x004D0EC5 / 0x00BE4130, BFME2 0x004D16B4 / 0x00BCE150)")
{
	W3DTornadoDrawModuleData d;
	CHECK(d.m_decalCount == 0u);
	CHECK(d.m_decalMaxRadius == 200.0f);
	CHECK(d.m_decalTemplate.style == 0x20u); // SHADOW_ALPHA_DECAL

	const FieldParse *t = W3DTornadoDrawTables::tornado();
	const char *const expected[] = { "DecalTemplate", "DecalCount", "DecalMaxRadius" };
	int n = 0;
	for (; t[n].token; ++n)
	{
		// tolerance: bounded table enumeration check
		REQUIRE(n < 3);
		CHECK(std::strcmp(t[n].token, expected[n]) == 0);
	}
	CHECK(n == 3);
}

TEST_CASE("w3d tornado draw: parsing INI with DecalTemplate, DecalCount, DecalMaxRadius")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));
		objtest::World w;
		W3DTornadoDraw::registerClass(w.modules);
		REQUIRE(w.load(kTornadoIni).empty());

		const ThingTemplate *tmpl = w.get("TestTornadoObj");
		REQUIRE(tmpl != nullptr);
		const auto &n = tmpl->drawModules().nuggets();
		REQUIRE(n.size() == 1u);
		const auto *data = dynamic_cast<const W3DTornadoDrawModuleData *>(n[0].data.get());
		REQUIRE(data != nullptr);
		CHECK(data->m_decalCount == 7u);
		CHECK(data->m_decalMaxRadius == 300.0f);
		CHECK(data->m_decalTemplate.maxRadius == 100.0f);
	}
}

TEST_CASE("w3d tornado draw: runtime creates decals, follows transform, and xfers (both editions)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));
		objtest::World w;
		W3DTornadoDraw::registerClass(w.modules);
		REQUIRE(w.load(kTornadoIni).empty());

		const ThingTemplate *tmpl = w.get("TestTornadoObj");
		REQUIRE(tmpl != nullptr);
		const auto &n = tmpl->drawModules().nuggets();
		REQUIRE(n.size() == 1u);
		const ModuleData *md = n[0].data.get();
		REQUIRE(md != nullptr);

		Thing thing(tmpl);
		const Coord3D p1{ 150.0f, 250.0f, 0.0f };
		thing.setPosition(&p1);

		W3DTornadoDraw tornado(&thing, md);
		CHECK(tornado.decalCount() == 7u);
		REQUIRE(tornado.decals().size() == 7u);
		CHECK(tornado.decals()[0].position.x == 150.0f);
		CHECK(tornado.decals()[0].position.y == 250.0f);

		const Coord3D p2{ 300.0f, 400.0f, 10.0f };
		thing.setPosition(&p2);
		tornado.reactToTransformChange(nullptr, 0.0f);
		CHECK(tornado.decals()[0].position.x == 300.0f);
		CHECK(tornado.decals()[0].position.y == 400.0f);
		CHECK(tornado.decals()[6].position.x == 300.0f);
		CHECK(tornado.decals()[6].position.y == 400.0f);

		// Snapshot save and load
		XferSave save;
		tornado.xfer(save);

		W3DTornadoDraw restored(&thing, md);
		restored.clearDecals();
		CHECK(restored.decalCount() == 0u);

		const auto &buffer = save.data();
		XferLoad load(buffer.data(), buffer.size());
		restored.xfer(load);
		CHECK(restored.decalCount() == 7u);
		CHECK(restored.decals()[0].position.x == 300.0f);
		CHECK(restored.decals()[0].position.y == 400.0f);
		CHECK(restored.decals()[6].radius == tornado.decals()[6].radius);
		CHECK(restored.decals()[6].hasShadow == tornado.decals()[6].hasShadow);
	}
}

TEST_CASE("w3d tornado draw: a drawable makes the runtime, binds it to its object and the i = 0 ring has no shadow decal (RW 0x4D120A / 0x732DE7)")
{
	objtest::World w;
	W3DTornadoDraw::registerClass(w.modules);
	REQUIRE(w.load(kTornadoIni).empty());
	w3dsynth::MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);
	std::vector<std::string> problems;
	const ThingTemplate *tpl = w.get("TestTornadoObj");
	REQUIRE(tpl != nullptr);

	Drawable d(services, tpl, (DrawableID)21, (ObjectID)201, {}, problems);
	REQUIRE(problems.empty());
	REQUIRE(d.entries().size() == 1u);
	REQUIRE(d.entries()[0].tornadoDraw != nullptr);
	CHECK(d.hasAnimatedEntry());
	const Coord3D pos{ 300.0f, 400.0f, 0.0f };
	d.setPosition(&pos);
	d.reactToTransformChange(nullptr, 0.0f);
	d.friend_boundToObject();

	W3DTornadoDraw *td = d.entries()[0].tornadoDraw.get();
	CHECK(td->decalCount() == 7u);
	REQUIRE(td->decals().size() == 7u);
	CHECK(td->decals()[0].position.x == 300.0f);
	CHECK_FALSE(td->decals()[0].hasShadow);
	CHECK(td->decals()[1].hasShadow);

	std::vector<SelectionDecal> out;
	td->appendSelectionDecals(201, 21, true, 0xFFFFFFu, 10, true, out);
	CHECK(out.size() == 6u);
	for (const SelectionDecal &sd : out)
	{
		CHECK(sd.size > 0.0f); // tolerance: the ring size follows the per-frame spiral step; only that it is a positive diameter is pinned
		CHECK(sd.angle > -3.1416f); // tolerance: normalized into (-pi, pi]
		CHECK(sd.angle <= 3.1416f); // tolerance: normalized into (-pi, pi]
	}

	// OnlyVisibleToOwningPlayer (the default): a decal of another player's tornado is not drawn for the local player (RW 0x732E2D)
	std::vector<SelectionDecal> foreign;
	td->appendSelectionDecals(201, 21, false, 0xFFFFFFu, 11, true, foreign);
	CHECK(foreign.empty());
}
