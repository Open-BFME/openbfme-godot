// OpenBFME. GPL-3.0.
//
// Narrow tests for lane LOCAL-28 (issue #190): W3DLightDraw as retail:
// object lights (Radius, Ambient, Diffuse, AttachToBoneInAnotherModule).
//
// Target facts:
//   RotWK 2.01:
//     - parse table:            RW 0x00BE3638 / 0x00BDBB08
//     - friend_newModuleInstance: RW 0x004645EE
//     - friend_newModuleData:     RW 0x00464626
//     - ModuleData ctor:          RW 0x004CF515 (Radius 20.0f, Intensity 1.0f)
//     - W3DLightDraw ctor:        RW 0x004CF121 (sets Ambient, Diffuse, Radius, FrameFade 0,0)
//     - ~W3DLightDraw:            RW 0x004CF246 (disables light and releases ref)
//     - doDrawModule:             RW 0x004CF2B5 (steps flicker/pulse, writes Intensity at +0xD0)
//     - reactToTransformChange:   RW 0x004CF5EE (reads AttachToBoneInAnotherModule +0x44, calls Drawable::0x672B5B)
//     - light fields:             RW 0x004CF18E keeps Ambient (+0xD4) and Diffuse (+0xE0) apart; the light this module
//                                 contributes is the diffuse one (Diffuse * Intensity, +0xD0). Retail's separate use of
//                                 Ambient (RW 0x004CF1A0 -> LightEnvironmentClass Init_From_Point RW 0x0053F90C) is lane
//                                 LOCAL-38 and is NOT ported here: Ambient is parsed and kept, never used as a fallback.
//   BFME2 1.06:
//     - parse table:            BFME2 0x00BCD918
//     - friend_newModuleInstance: BFME2 0x00464DF8
//     - friend_newModuleData:     BFME2 0x00464E2F
//     - ModuleData ctor:          BFME2 0x000CFB3E
//     - W3DLightDraw ctor:        BFME2 0x004CEF80
//     - ~W3DLightDraw:            BFME2 0x000CFA42
//     - doDrawModule:             BFME2 0x000CFC0C
//     - reactToTransformChange:   BFME2 0x000CFDFB

#include "doctest.h"
#include "ObjectTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "Common/GameEdition.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/Thing.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ClientEffects.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXPlayback.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace drawtest;
using namespace w3dsynth;

namespace
{
const char *const kLightObjectIni =
	"Object TorchTestObj\n"
	"  KindOf = SELECTABLE\n"
	"  Draw = W3DLightDraw ModuleTag_LightDraw\n"
	"    Ambient = R:10 G:5 B:0\n"
	"    Diffuse = R:255 G:128 B:32\n"
	"    Radius = 45.0\n"
	"    Intensity = 2.0\n"
	"    FlickerAmplitude = 0.5\n"
	"    FlickerFrequency = 10.0\n"
	"    PulseAmplitude = 0.2\n"
	"    PulseFrequency = 1.5\n"
	"    AttachToBoneInAnotherModule = TorchBone\n"
	"  End\n"
	"End\n";

const char *const kAmbientOnlyObjectIni =
	"Object AmbientLightObj\n"
	"  KindOf = SELECTABLE\n"
	"  Draw = W3DLightDraw ModuleTag_LightDraw\n"
	"    Ambient = R:50 G:100 B:200\n"
	"    Diffuse = R:0 G:0 B:0\n"
	"    Radius = 30.0\n"
	"    Intensity = 1.5\n"
	"  End\n"
	"End\n";

// A fixed scripted random: the middle of every range (the synthetic torch bearer has no random draw of its own).
class FixedRandom : public W3DDrawRandom
{
public:
	int value(int lo, int hi) override { return lo == hi ? lo : (lo + hi) / 2; }
	float real(float lo, float hi) override { return (lo + hi) * 0.5f; }
};

// A synthetic soldier whose skeleton carries a torch bone and whose walk animation moves that bone: the drawable-side
// bone query (RW 0x672B5B) must answer from the animated pose, not from the pristine (base) one.
struct SyntheticAnimatedTorchBearer
{
	MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	Harness harness;
	FixedRandom random;
	std::unique_ptr<W3DScriptedModelDraw> draw;

	SyntheticAnimatedTorchBearer()
	{
		// Skeleton: ROOTTRANSFORM (0) -> HIP (1, base (0,0,1)) -> TORCHBONE (2, base (0,2,0))
		fs.Add("art\\w3d\\tb\\tbunit_skl.w3d",
			hierarchyChunk("TBUNIT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("TORCHBONE", 1, 0, 2, 0) }));
		std::vector<std::uint8_t> bytes;
		SynthMesh body;
		body.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
		body.Triangles = { 0, 1, 2, 0, 2, 3 };
		body.Container = "TBUNIT_SKN";
		body.Name = "BODY";
		append(bytes, meshChunk(body));
		append(bytes, hlodChunk("TBUNIT_SKN", "TBUNIT_SKL", { { "TBUNIT_SKN.BODY", 1 } }));
		fs.Add("art\\w3d\\tb\\tbunit_skn.w3d", bytes);

		// TBUNIT_WALK: 11 frames, 30 fps. TORCHBONE (pivot 2) moves along Z: frame 0 -> 0.0f, frame 5 -> 10.0f
		std::vector<float> zVals(11, 0.0f);
		for (int f = 0; f < 11; ++f)
		{
			zVals[(size_t)f] = (float)f * 2.0f;
		}
		std::vector<SynthChannel> channels;
		channels.push_back(SynthChannel{ ANIM_CHANNEL_Z, 2, 0, 1, zVals });
		std::vector<std::uint8_t> anim = rawAnimChunk("TBUNIT_WALK", "TBUNIT_SKL", 11, 30, channels);
		fs.Add("art\\w3d\\tb\\tbunit_walk.w3d", anim);

		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));

		std::string ini =
			"DefaultModelConditionState\n  Model = TBUNIT_SKN\n  Skeleton = TBUNIT_SKL\nEnd\n"
			"IdleAnimationState\n"
			"  StateName = STATE_Idle\n"
			"  Animation = WALK\n"
			"    AnimationName = TBUNIT_WALK\n"
			"    AnimationMode = LOOP\n"
			"  End\n"
			"End\n"
			"End\n";
		W3DModelDrawModuleData *data = harness.parse(ini);
		REQUIRE(data != nullptr);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		draw.reset(new W3DScriptedModelDraw(*data, *drawAssets, random, nullptr, o));
		REQUIRE(draw->errors().empty());
	}
};
} // namespace

TEST_CASE("LOCAL-28: W3DLightDrawModuleData field defaults and parsing (Radius, Ambient, Diffuse, AttachToBoneInAnotherModule)")
{
	W3DLightDrawModuleData d;
	CHECK(d.m_ambient.red == 0.0f);
	CHECK(d.m_ambient.green == 0.0f);
	CHECK(d.m_ambient.blue == 0.0f);
	CHECK(d.m_diffuse.red == 0.0f);
	CHECK(d.m_diffuse.green == 0.0f);
	CHECK(d.m_diffuse.blue == 0.0f);
	CHECK(d.m_radius == 20.0f);
	CHECK(d.m_intensity == 1.0f);
	CHECK(d.m_attachToBone == "");

	objtest::World w;
	W3DLightDraw::registerClass(w.modules);
	REQUIRE(w.load(kLightObjectIni).empty());

	const ThingTemplate *tt = w.get("TorchTestObj");
	REQUIRE(tt != nullptr);

	const auto &nuggets = tt->drawModules().nuggets();
	REQUIRE(nuggets.size() == 1u);
	const auto *data = dynamic_cast<const W3DLightDrawModuleData *>(nuggets[0].data.get());
	REQUIRE(data != nullptr);

	CHECK(data->m_ambient.red == doctest::Approx(10.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_ambient.green == doctest::Approx(5.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_diffuse.red == doctest::Approx(255.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_diffuse.green == doctest::Approx(128.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_diffuse.blue == doctest::Approx(32.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_radius == 45.0f);
	CHECK(data->m_intensity == 2.0f);
	CHECK(data->m_flickerAmplitude == 0.5f);
	CHECK(data->m_flickerFrequency == 10.0f);
	CHECK(data->m_pulseAmplitude == 0.2f);
	CHECK(data->m_pulseFrequency == 1.5f);
	CHECK(data->m_attachToBone == "TorchBone");
}

TEST_CASE("LOCAL-28: persistent dynamic lights in LightPulsePool (RW 0x004CF121 / DONOR BFME2 0x004CEF80)")
{
	LightPulsePool pool;

	// Retail ctor sets FarAttenStart = 1.0f, FarAttenEnd = radius.
	// Default radius is 20.0f: unlike transient pulses (which require radius >= 21.0f),
	// persistent lights allow 20.0f without being refused.
	int handle0 = pool.addPersistent(Coord3D{ 100.0f, 200.0f, 10.0f }, RGBColor{ 1.0f, 0.5f, 0.2f }, 20.0f);
	REQUIRE(handle0 == 0);
	CHECK(pool.lights().size() == 1u);
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].farAttenStart == 1.0f);
	CHECK(pool.lights()[0].farAttenEnd == 20.0f);
	CHECK(pool.lights()[0].range() == 20.0f);
	CHECK(pool.lights()[0].colorNow().red == 1.0f);
	CHECK(pool.lights()[0].colorNow().green == 0.5f);

	// Step 50 times: persistent lights have increaseFrames = 0, decreaseFrames = 0
	// so they never decay or expire over time.
	for (int i = 0; i < 50; ++i)
	{
		pool.step();
	}
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].scale() == 1.0f);
	CHECK(pool.lights()[0].range() == 20.0f);
	CHECK(pool.enabledCount() == 1u);

	// Update persistent light position, color, radius
	pool.updatePersistent(handle0, Coord3D{ 150.0f, 250.0f, 15.0f }, RGBColor{ 0.8f, 0.4f, 0.1f }, 35.0f, true);
	CHECK(pool.lights()[0].position.x == 150.0f);
	CHECK(pool.lights()[0].position.y == 250.0f);
	CHECK(pool.lights()[0].position.z == 15.0f);
	CHECK(pool.lights()[0].range() == 35.0f);
	CHECK(pool.lights()[0].colorNow().red == 0.8f);

	// Remove persistent light disables the slot
	pool.removePersistent(handle0);
	CHECK_FALSE(pool.lights()[0].enabled);
	CHECK(pool.enabledCount() == 0u);

	// Reusing slot on new addPersistent
	int handle1 = pool.addPersistent(Coord3D{ 0.0f, 0.0f, 0.0f }, RGBColor{ 0.0f, 1.0f, 0.0f }, 50.0f);
	CHECK(handle1 == handle0); // slot 0 reused
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].range() == 50.0f);
}

TEST_CASE("LOCAL-28: slot isolation prevents aliasing between persistent lights and transient pulses")
{
	LightPulsePool pool;
	int persistentHandle = pool.addPersistent(Coord3D{ 10.0f, 20.0f, 30.0f }, RGBColor{ 1.0f, 0.5f, 0.2f }, 40.0f);
	REQUIRE(persistentHandle == 0);
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].isPersistent);

	// Disable persistent light: enabled becomes false, but isPersistent remains true
	pool.updatePersistent(persistentHandle, Coord3D{ 10.0f, 20.0f, 30.0f }, RGBColor{ 1.0f, 0.5f, 0.2f }, 40.0f, false);
	CHECK_FALSE(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].isPersistent);

	// Add transient FX pulse: must NOT reuse slot 0 because isPersistent == true
	REQUIRE(pool.add(Coord3D{ 50.0f, 60.0f, 70.0f }, RGBColor{ 0.1f, 0.2f, 0.9f }, 30.0f, 2, 4));
	REQUIRE(pool.lights().size() == 2u);
	CHECK(pool.lights()[1].enabled);
	CHECK_FALSE(pool.lights()[1].isPersistent);
	CHECK(pool.lights()[1].position.x == 50.0f);

	// Re-enable persistent light: slot 1 (transient pulse) is untouched
	pool.updatePersistent(persistentHandle, Coord3D{ 15.0f, 25.0f, 35.0f }, RGBColor{ 1.0f, 0.5f, 0.2f }, 40.0f, true);
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.lights()[0].position.x == 15.0f);
	CHECK(pool.lights()[1].enabled);
	CHECK(pool.lights()[1].position.x == 50.0f);
	CHECK(pool.lights()[1].color.blue == 0.9f);
}

TEST_CASE("LOCAL-28: W3DLightDraw registers, flickers, and updates dynamic light pool")
{
	LightPulsePool pool;

	objtest::World w;
	W3DLightDraw::registerClass(w.modules);
	REQUIRE(w.load(kLightObjectIni).empty());

	const ThingTemplate *tt = w.get("TorchTestObj");
	REQUIRE(tt != nullptr);
	const auto *data = dynamic_cast<const W3DLightDrawModuleData *>(tt->drawModules().nuggets()[0].data.get());
	REQUIRE(data != nullptr);

	Thing thing(tt);
	Coord3D pos{ 10.0f, 20.0f, 5.0f };
	thing.setPosition(&pos);

	W3DLightDraw light(&thing, data);
	light.setLightPool(&pool);

	// Light registered into pool
	REQUIRE(light.lightHandle() == 0);
	CHECK(pool.enabledCount() == 1u);
	CHECK(pool.lights()[0].position.x == 10.0f);
	CHECK(pool.lights()[0].position.y == 20.0f);
	CHECK(pool.lights()[0].position.z == 5.0f);
	CHECK(pool.lights()[0].range() == 45.0f);

	// Effective color: Diffuse * Intensity (255/255 * 2.0 = 2.0)
	const RGBColor initialCol = light.lightColor();
	CHECK(initialCol.red == 2.0f);
	CHECK(initialCol.green == doctest::Approx(2.0f * (128.0f / 255.0f))); // tolerance: floating-point evaluation

	// Step flicker: phase and height change, updating pool light
	light.stepFlicker(0.1f);
	CHECK(light.phase() == doctest::Approx(0.1f)); // tolerance: floating-point evaluation
	const RGBColor flickeredCol = pool.lights()[0].colorNow();
	CHECK(flickeredCol.red == doctest::Approx(light.lightColor().red)); // tolerance: floating-point evaluation

	// Position change
	Coord3D pos2{ 30.0f, 40.0f, 15.0f };
	thing.setPosition(&pos2);
	light.reactToTransformChange(nullptr, 0.0f);
	CHECK(pool.lights()[0].position.x == 30.0f);
	CHECK(pool.lights()[0].position.y == 40.0f);

	// Disabling light
	light.setEnabled(false);
	CHECK_FALSE(pool.lights()[0].enabled);
	CHECK(pool.enabledCount() == 0u);

	light.setEnabled(true);
	CHECK(pool.lights()[0].enabled);
	CHECK(pool.enabledCount() == 1u);
}

TEST_CASE("LOCAL-28: Ambient is kept apart from Diffuse, no fallback for a black Diffuse (RW 0x4CF18E)")
{
	LightPulsePool pool;

	objtest::World w;
	W3DLightDraw::registerClass(w.modules);
	REQUIRE(w.load(kAmbientOnlyObjectIni).empty());

	const ThingTemplate *tt = w.get("AmbientLightObj");
	REQUIRE(tt != nullptr);
	const auto *data = dynamic_cast<const W3DLightDrawModuleData *>(tt->drawModules().nuggets()[0].data.get());
	REQUIRE(data != nullptr);
	// Ambient is stored as the INI wrote it (RW 0x4CF1A0 writes module data +0xD4): 50/255
	CHECK(data->m_ambient.red == doctest::Approx(50.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_ambient.green == doctest::Approx(100.0f / 255.0f)); // tolerance: floating-point evaluation
	CHECK(data->m_ambient.blue == doctest::Approx(200.0f / 255.0f)); // tolerance: floating-point evaluation

	Thing thing(tt);
	Coord3D pos{ 5.0f, 5.0f, 0.0f };
	thing.setPosition(&pos);

	W3DLightDraw light(&thing, data);
	light.setLightPool(&pool);

	REQUIRE(light.lightHandle() == 0);
	CHECK(light.lightRadius() == 30.0f);
	// RW 0x4CF18E keeps Ambient (+0xD4) and Diffuse (+0xE0) apart: the light the module contributes is the diffuse one, so a black
	// Diffuse is a black light - the port does not substitute Ambient for it. Retail uses Ambient too (RW 0x4CF1A0 -> Init_From_Point
	// RW 0x53F90C); that is lane LOCAL-38's rule and is not part of this port, so it is reported, never guessed.
	const RGBColor c = light.lightColor();
	CHECK(c.red == 0.0f);
	CHECK(c.green == 0.0f);
	CHECK(c.blue == 0.0f);
	CHECK(pool.lights()[0].colorNow().red == 0.0f);
	CHECK(pool.lights()[0].colorNow().green == 0.0f);
	CHECK(pool.lights()[0].colorNow().blue == 0.0f);
}

TEST_CASE("LOCAL-28: W3DLightDraw with no module data creates no light (RW 0x004CF2EC)")
{
	LightPulsePool pool;
	Thing thing(nullptr);

	W3DLightDraw lightNoData(&thing, nullptr);
	lightNoData.setLightPool(&pool);

	CHECK(lightNoData.lightRadius() == 0.0f);
	CHECK(lightNoData.lightHandle() == -1);
	CHECK(pool.lights().empty());
	CHECK(pool.enabledCount() == 0u);
}

TEST_CASE("LOCAL-28: animated bone tracking for AttachToBoneInAnotherModule (RW 0x004CF5EE -> RW 0x00672B5B -> RW 0x004B2F19)")
{
	SyntheticAnimatedTorchBearer soldier;

	objtest::World w;
	DrawServices services(*soldier.assets, w.modules, W3DLuaDrawScriptHost::Options{});
	ThingTemplate tmpl;
	tmpl.friend_setTemplateName("TorchBearerTemplate");
	std::vector<std::string> problems;
	auto d = std::make_unique<Drawable>(services, &tmpl, 1, 1, std::vector<ClientEvent::ClientModule>{}, problems);
	Coord3D thingPos{ 100.0f, 200.0f, 50.0f };
	d->setPosition(&thingPos);

	DrawEntry entry;
	entry.className = "W3DScriptedModelDraw";
	entry.kind = W3D_DRAWKIND_MODEL;
	entry.draw = std::move(soldier.draw);
	d->entries().push_back(std::move(entry));

	// Frame 0: TORCHBONE is at root(50) + HIP(1) + TORCHBONE base(0) + anim(0) = 51.0f
	Coord3D bonePos0;
	REQUIRE(d->boneWorldPosition("TORCHBONE", bonePos0));
	CHECK(bonePos0.x == doctest::Approx(100.0f)); // tolerance: floating-point evaluation
	CHECK(bonePos0.y == doctest::Approx(202.0f)); // tolerance: floating-point evaluation
	CHECK(bonePos0.z == doctest::Approx(51.0f)); // tolerance: floating-point evaluation

	// Advance animation by 5 frames (5 * 33.34 ms ~ 166.7 ms)
	// TBUNIT_WALK moves TORCHBONE by +10.0f in Z at frame 5
	d->entries()[0].draw->advance(166.7);

	Coord3D bonePos5;
	REQUIRE(d->boneWorldPosition("TORCHBONE", bonePos5));
	CHECK(bonePos5.x == doctest::Approx(100.0f)); // tolerance: floating-point evaluation
	CHECK(bonePos5.y == doctest::Approx(202.0f)); // tolerance: floating-point evaluation
	CHECK(bonePos5.z == doctest::Approx(61.0f)); // tolerance: floating-point evaluation
}

TEST_CASE("LOCAL-28: neighbouring behaviour (transient pulses from bldfx1 and draw modules notDrawnClasses)")
{
	LightPulsePool pool;
	// bldfx1 transient pulse: radius 12 < 21 is refused
	CHECK_FALSE(pool.add(Coord3D{ 0, 0, 0 }, RGBColor{ 1.0f, 0.5f, 0.0f }, 12.0f, 1, 5));
	CHECK(pool.refusedRange() == 1);

	// Valid transient pulse
	REQUIRE(pool.add(Coord3D{ 10.0f, 20.0f, 30.0f }, RGBColor{ 0.5f, 0.25f, 0.0f }, 40.0f, 1, 2));
	REQUIRE(pool.lights().size() == 1u);
	pool.step(); // increase frame
	CHECK(pool.lights()[0].enabled);
	pool.step(); // decrease frame 1
	pool.step(); // decrease frame 2 (expires)
	CHECK_FALSE(pool.lights()[0].enabled);

	// W3DDrawModules classification: W3DLightDraw is classified and listed in notDrawnClasses
	const W3DDrawClassInfo *ci = W3DDrawModules::find("W3DLightDraw");
	REQUIRE(ci != nullptr);
	CHECK(ci->kind == W3D_DRAWKIND_NOT_DRAWN);

	const auto notDrawn = W3DDrawModules::notDrawnClasses();
	CHECK(notDrawn.size() == 10u);
	bool foundLight = false;
	for (const std::string &line : notDrawn)
	{
		if (line.rfind("W3DLightDraw:", 0) == 0)
		{
			foundLight = true;
		}
	}
	CHECK(foundLight);
}
