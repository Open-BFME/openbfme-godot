// OpenBFME unit tests. GPL-3.0. W3DSailModelDraw on the AttachToBoneInAnotherModule slot (Drawable::entryTransform).
// Synthetic INI and W3D only; no retail files. RW 0x4B6A77/0x4B6AA2/0x4B6B26 dispatches +0x104
// ONLY after another module's bone is found. RW 0x4D00CD..0x4D033B supplies the fixed wind,
// angle limit, first-call flag, damping and Z turn; RW 0x9F3A3C is the non-sail no-op.

#include "doctest.h"

#include "ObjectTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"
#include "Common/AsciiString.h"

#include "GameClient/Drawable.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DSailModelDraw.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace w3dsynth;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct World : objtest::World
{
	World() { W3DDrawModules::registerTypedDrawModuleData(modules); }
};

// The common drawtest::Harness parses scripted-model bodies. The sail variant
// uses the same INI multi-table parser as its typed ModuleFactory binding; it
// needs its own harness so parsing it cannot release the hull's module data.
struct SailHarness
{
	INIEnvironment env;
	std::unique_ptr<W3DSailModelDrawModuleData> data;
	std::string error;

	SailHarness()
	{
		env.blocks.registerBlock("TestSail", [this](INI *ini) {
			data = std::make_unique<W3DSailModelDrawModuleData>();
			ini->initFromINIMultiProc(data.get(), &W3DSailModelDrawModuleData::buildFieldParse);
			while (!ini->isEOF()) ini->readLine();
		});
	}

	W3DSailModelDrawModuleData *parse(const std::string &body)
	{
		data.reset();
		error.clear();
		const std::string text = "TestSail\n" + body;
		INI ini(env);
		try
		{
			ini.loadMemory("sail.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			error = e.message();
			data.reset();
		}
		return data.get();
	}
};

struct Rig
{
	World world;
	MemoryFileSource files;
	WW3DAssetManager assets;
	DrawServices services;
	std::vector<std::string> problems;

	Rig() : assets(files), services(assets, world.modules, W3DLuaDrawScriptHost::Options{})
	{
		// Two independently owned modules per object: the parent hull has a sail bone at
		// (3,0,2); the sail's own skeleton has no such bone. ThingTemplate owns a distinct
		// Nugget/module-data object for EACH draw (no shared, reparsed Harness/dangling data).
		files.Add("art\\w3d\\sa\\sailt_hull_skl.w3d",
			hierarchyChunk("SAILT_HULL_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("SAIL_BONE01", 0, 3, 0, 2) }));
		files.Add("art\\w3d\\sa\\sailt_skl.w3d", hierarchyChunk("SAILT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0) }));
		for (const char *name : { "SAILT_HULL", "SAILT_SKN" })
		{
			SynthMesh mesh;
			mesh.Name = "BODY";
			mesh.Container = name;
			mesh.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
			mesh.Triangles = { 0, 1, 2 };
			std::vector<std::uint8_t> model;
			append(model, meshChunk(mesh));
			const char *skeleton = std::string(name) == "SAILT_HULL" ? "SAILT_HULL_SKL" : "SAILT_SKL";
			append(model, hlodChunk(name, skeleton, { { std::string(name) + ".BODY", 0 } }));
			files.Add(std::string("art\\w3d\\sa\\") + AsciiStringUtil::lowered(name) + ".w3d", model);
		}
		std::string ini;
		auto appendObject = [&](const char *name, const char *drawClass, const char *bone, const char *maxDegrees) {
			ini += std::string("Object ") + name + "\n"
				"  Draw = W3DScriptedModelDraw ModuleTag_Hull\n"
				"    DefaultModelConditionState\n      Model = SAILT_HULL\n      Skeleton = SAILT_HULL_SKL\n    End\n"
				"  End\n";
			ini += std::string("  Draw = ") + drawClass + " ModuleTag_Test\n";
			if (bone)
			{
				ini += std::string("    AttachToBoneInAnotherModule = ") + bone + "\n";
			}
			if (maxDegrees)
			{
				ini += std::string("    MaxRotationDegrees = ") + maxDegrees + "\n"
					"    AboutDamping = 0.1\n    BlowingThresholdDegrees = 179\n"; // parsed-unused in RW 2.01, not a wind gate
			}
			ini += "    DefaultModelConditionState\n      Model = SAILT_SKN\n      Skeleton = SAILT_SKL\n    End\n"
				"  End\nEnd\n";
		};
		appendObject("SailNarrow", "W3DSailModelDraw", "SAIL_BONE01", "30");
		appendObject("SailWide", "W3DSailModelDraw", "SAIL_BONE01", "90");
		appendObject("SailFree", "W3DSailModelDraw", nullptr, "90");
		appendObject("SailMissing", "W3DSailModelDraw", "NO_SUCH_BONE", "90");
		appendObject("Plain", "W3DScriptedModelDraw", "SAIL_BONE01", nullptr);
		appendObject("Truck", "W3DTruckDraw", "SAIL_BONE01", nullptr);
		const std::string error = world.load(ini);
		REQUIRE_MESSAGE(error.empty(), error);
	}

	std::unique_ptr<Drawable> make(const char *name, DrawableID id)
	{
		const ThingTemplate *tt = world.get(name);
		REQUIRE(tt != nullptr);
		REQUIRE(tt->drawModules().nuggets().size() == 2);
		REQUIRE(tt->drawModules().nuggets()[0].data.get() != tt->drawModules().nuggets()[1].data.get());
		auto drawable = std::make_unique<Drawable>(services, tt, id, 1, std::vector<ClientEvent::ClientModule>{}, problems);
		const std::string error = problems.empty() ? "" : problems.front();
		REQUIRE_MESSAGE(problems.empty(), error);
		REQUIRE(drawable->entries().size() == 2);
		for (const DrawEntry &e : drawable->entries())
		{
			REQUIRE(e.draw != nullptr);
			REQUIRE_MESSAGE(e.draw->errors().empty(), e.draw->errors().front());
		}
		return drawable;
	}
};

void checkBasisTurn(const Matrix3D &matrix, double theta, float x, float y, float z)
{
	// Identity * Rotate_Z(theta) is [cos, -sin; sin, cos] (RW 0x4D028D..0x4D02C5).
	CHECK(matrix.Row[0][0] == doctest::Approx(std::cos(theta)).epsilon(1e-5)); // tolerance: float radians and trigonometric functions round differently from the reference double angle.
	CHECK(matrix.Row[0][1] == doctest::Approx(-std::sin(theta)).epsilon(1e-5)); // tolerance: float radians and trigonometric functions round differently from the reference double angle.
	CHECK(matrix.Row[1][0] == doctest::Approx(std::sin(theta)).epsilon(1e-5)); // tolerance: float radians and trigonometric functions round differently from the reference double angle.
	CHECK(matrix.Row[1][1] == doctest::Approx(std::cos(theta)).epsilon(1e-5)); // tolerance: float radians and trigonometric functions round differently from the reference double angle.
	CHECK(matrix.Row[2][2] == 1.0f);
	CHECK(matrix.Row[0][3] == x);
	CHECK(matrix.Row[1][3] == y);
	CHECK(matrix.Row[2][3] == z);
}
} // namespace

TEST_CASE("w3d sail: two-module Harness: independently parsed hull and sail survive through bone lookup")
{
	Rig rig;
	drawtest::Harness hullHarness;
	SailHarness sailHarness; // a separate INI/Harness for the second module: no reparsing/freed hull data
	const W3DModelDrawModuleData *hull = hullHarness.parse(
		"DefaultModelConditionState\n  Model = SAILT_HULL\n  Skeleton = SAILT_HULL_SKL\nEnd\nEnd\n");
	REQUIRE_MESSAGE(hull != nullptr, hullHarness.error);
	const W3DSailModelDrawModuleData *sail = sailHarness.parse(
		"AttachToBoneInAnotherModule = SAIL_BONE01\n"
		"MaxRotationDegrees = 30\nAboutDamping = 0.1\n"
		"DefaultModelConditionState\n  Model = SAILT_SKN\n  Skeleton = SAILT_SKL\nEnd\nEnd\n");
	REQUIRE_MESSAGE(sail != nullptr, sailHarness.error);
	CHECK(static_cast<const void *>(hull) != static_cast<const void *>(sail));
	CHECK(AsciiStringUtil::compareNoCase(sail->m_attachToDrawableBone, "SAIL_BONE01") == 0);

	ThingTemplate tmpl;
	tmpl.friend_setTemplateName("SailHarnessShip");
	std::vector<std::string> problems;
	auto d = std::make_unique<Drawable>(rig.services, &tmpl, 9, 1, std::vector<ClientEvent::ClientModule>{}, problems);
	REQUIRE(problems.empty());
	DrawEntry parent;
	parent.className = "W3DScriptedModelDraw";
	parent.kind = W3D_DRAWKIND_MODEL;
	parent.draw = std::make_unique<W3DScriptedModelDraw>(*hull, rig.services.drawAssets, rig.services.random);
	REQUIRE(parent.draw->errors().empty());
	d->entries().push_back(std::move(parent));
	DrawEntry child;
	child.className = "W3DSailModelDraw";
	child.kind = W3D_DRAWKIND_MODEL;
	child.attachToBone = sail->m_attachToDrawableBone;
	child.draw = std::make_unique<W3DSailModelDraw>(*sail, rig.services.drawAssets, rig.services.random);
	REQUIRE(child.draw->errors().empty());
	d->entries().push_back(std::move(child));
	CHECK(d->entries()[0].draw->frame().modelName == "SAILT_HULL");
	CHECK(d->entries()[1].draw->frame().modelName == "SAILT_SKN");
	const Coord3D at{ 12, -8, 3 };
	d->setPosition(&at);
	Matrix3D placed;
	REQUIRE(d->entryTransform(d->entries()[1], placed));
	checkBasisTurn(placed, kPi / 6.0, 15, -8, 5); // parent bone at (3,0,2); +45 wind capped at +30
}

TEST_CASE("w3d sail: sail on SAIL_BONE01: the attached slot turns from fixed wind, limited by MaxRotationDegrees")
{
	Rig rig;
	auto narrow = rig.make("SailNarrow", 1);
	auto wide = rig.make("SailWide", 2);
	const Coord3D at{ 12, -8, 3 };
	narrow->setPosition(&at);
	wide->setPosition(&at);
	DrawEntry &n = narrow->entries()[1];
	DrawEntry &w = wide->entries()[1];
	REQUIRE(dynamic_cast<W3DSailModelDraw *>(n.draw.get()) != nullptr);
	REQUIRE(dynamic_cast<W3DSailModelDraw *>(w.draw.get()) != nullptr);
	CHECK(AsciiStringUtil::compareNoCase(n.attachToBone, "SAIL_BONE01") == 0);
	CHECK(AsciiStringUtil::compareNoCase(w.attachToBone, "SAIL_BONE01") == 0);
	CHECK(n.draw->frame().modelName == "SAILT_SKN");
	Matrix3D nMatrix, wMatrix;
	REQUIRE(narrow->entryTransform(n, nMatrix));
	REQUIRE(wide->entryTransform(w, wMatrix));
	checkBasisTurn(nMatrix, kPi / 6.0, 15, -8, 5); // +45 fixed wind capped at 30, bone's nonzero offset retained
	checkBasisTurn(wMatrix, kPi / 4.0, 15, -8, 5); // first call takes +45, not a +0.1 damping step
}

TEST_CASE("w3d sail: sail on bone: first angle is immediate; subsequent attached frames step AboutDamping, even while stationary")
{
	Rig rig;
	auto ship = rig.make("SailWide", 3);
	auto other = rig.make("SailWide", 4);
	const Coord3D at{ 4, 9, 2 };
	ship->setPosition(&at);
	other->setPosition(&at);
	DrawEntry &sail = ship->entries()[1];
	ship->setOrientation(static_cast<float>(kPi / 2.0));
	// Facing +Y: dot(wind,X)=+sqrt(1/2), dot(wind,Y)=-sqrt(1/2).
	// First angle -pi/4. Turn the hull back to +X, then keep it STATIONARY:
	// the attached matrix still changes once per draw by AboutDamping = 0.1.
	Matrix3D first;
	REQUIRE(ship->entryTransform(sail, first));
	CHECK(first.Row[0][0] == doctest::Approx(-std::sin(-kPi / 4.0)).epsilon(1e-5)); // tolerance: orientation and trigonometric values are rounded to floats.
	CHECK(first.Row[1][0] == doctest::Approx(std::cos(-kPi / 4.0)).epsilon(1e-5)); // tolerance: orientation and trigonometric values are rounded to floats.
	CHECK(first.Row[1][3] == doctest::Approx(12.0).epsilon(1e-5)); // tolerance: float pi/2 produces a small nonzero cosine in the bone offset.
	ship->setOrientation(0.0f);
	Matrix3D second, third;
	REQUIRE(ship->entryTransform(sail, second));
	REQUIRE(ship->entryTransform(sail, third));
	checkBasisTurn(second, -kPi / 4.0 + 0.1, 7, 9, 4);
	checkBasisTurn(third, -kPi / 4.0 + 0.2, 7, 9, 4);
	// Angle/valid are properties of EACH runtime, not a shared static wind state.
	Matrix3D independent;
	REQUIRE(other->entryTransform(other->entries()[1], independent));
	checkBasisTurn(independent, kPi / 4.0, 7, 9, 4);
}

TEST_CASE("w3d sail: sail without a resolvable bone: no slot call; attachment later still starts undamped")
{
	Rig rig;
	const Coord3D at{ -3, 8, 6 };
	for (const char *name : { "SailFree", "SailMissing" })
	{
		auto d = rig.make(name, name[4] == 'F' ? 5 : 6);
		d->setPosition(&at);
		d->setOrientation(static_cast<float>(kPi / 2.0));
		DrawEntry &e = d->entries()[1];
		REQUIRE(dynamic_cast<W3DSailModelDraw *>(e.draw.get()) != nullptr);
		Matrix3D unplaced;
		CHECK_FALSE(d->entryTransform(e, unplaced));
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				CHECK(unplaced.Row[r][c] == d->getBasis()[r * 3 + c]); // the default object basis, not a turned sail
			}
		}
		CHECK(unplaced.Row[0][3] == -3);
		CHECK(unplaced.Row[1][3] == 8);
		CHECK(unplaced.Row[2][3] == 6);
		// If the missing-bone/unattached branch had called +0x104 above, its saved
		// target would be -45. A subsequent first successful +45 call would damp.
		d->setOrientation(0.0f);
		e.attachToBone = "SAIL_BONE01";
		Matrix3D nowAttached;
		REQUIRE(d->entryTransform(e, nowAttached));
		checkBasisTurn(nowAttached, kPi / 4.0, 0, 8, 8);
	}
}

TEST_CASE("w3d sail: neighbor: attached plain and truck draws keep the bone matrix and their model pose")
{
	Rig rig;
	for (const char *name : { "Plain", "Truck" })
	{
		auto d = rig.make(name, name[0] == 'P' ? 7 : 8);
		const Coord3D at{ -1, 4, 11 };
		d->setPosition(&at);
		d->setInstanceScale(2.0f);
		DrawEntry &e = d->entries()[1];
		CHECK(dynamic_cast<W3DSailModelDraw *>(e.draw.get()) == nullptr);
		CHECK(AsciiStringUtil::compareNoCase(e.attachToBone, "SAIL_BONE01") == 0);
		Matrix3D matrix;
		REQUIRE(d->entryTransform(e, matrix));
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				CHECK(matrix.Row[r][c] == (r == c ? 2.0f : 0.0f)); // base RW 0x9F3A3C: no matrix mutation
			}
		}
		CHECK(matrix.Row[0][3] == 5); // bone at x=3, scaled by 2
		CHECK(matrix.Row[1][3] == 4);
		CHECK(matrix.Row[2][3] == 15); // bone z=2, scaled by 2
		CHECK(e.draw->frame().modelName == "SAILT_SKN");
		CHECK(e.draw->frame().model != nullptr);
	}
}

TEST_CASE("w3d sail: xfer round-trips the angle and the valid flag into a fresh draw (RW 0x4D0009)")
{
	Rig rig;
	auto ship = rig.make("SailWide", 10);
	const Coord3D at{ 0, 0, 0 };
	ship->setPosition(&at);
	ship->setOrientation(static_cast<float>(kPi / 2.0));
	W3DSailModelDraw *sail = dynamic_cast<W3DSailModelDraw *>(ship->entries()[1].draw.get());
	REQUIRE(sail != nullptr);
	CHECK_FALSE(sail->hasLastAngle());
	Matrix3D m;
	REQUIRE(ship->entryTransform(ship->entries()[1], m));
	REQUIRE(sail->hasLastAngle());
	CHECK(sail->lastAngle() == doctest::Approx(-kPi / 4.0).epsilon(1e-5)); // tolerance: float radians against the double reference
	XferSave save;
	sail->xfer(save);
	auto other = rig.make("SailWide", 11);
	W3DSailModelDraw *fresh = dynamic_cast<W3DSailModelDraw *>(other->entries()[1].draw.get());
	REQUIRE(fresh != nullptr);
	REQUIRE_FALSE(fresh->hasLastAngle());
	XferLoad load(save.data().data(), save.data().size());
	fresh->xfer(load);
	CHECK(fresh->hasLastAngle());
	CHECK(fresh->lastAngle() == sail->lastAngle());
}
