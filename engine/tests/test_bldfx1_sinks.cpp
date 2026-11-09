// OpenBFME. Lane FEAT-BLDFX-1 tests: the FXList nugget sinks (ClientEffects.h) and the client effect state they leave behind.
//
// Every expected value is derived from the retail body the port cites: the scorch buffer (TARGET call RW 0x5E0045; DONOR BFME2 0x67EA3, BaseHeightMapAddScorch.cpp),
// the DynamicDecal opacity envelope (TARGET call RW 0x5E05AB; DONOR BFME2 0x10C785, Rva0010C785TimedDecals.cpp), the dynamic light's FrameFade (DONOR BFME2 0x6DE3D)
// (W3DDynamicLightSetFrameFade.cpp) and the camera shaker's Compute_Rotations (TARGET call RW 0x5DFE90; DONOR BFME2 0x65B09) (camerashakesystem_shaker.cpp). No random expected
// value is read back from the port's generator: the scorch random type's exact draw is asserted as one of the four NORMAL cells the retail data
// names (X:0 .. 3), and the camera shaker is asserted through properties (zero outside the radius, deterministic, expiring).

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/NumericState.h"

#include "GameClient/CameraShaker.h"
#include "GameClient/ClientEffects.h"
#include "GameClient/FXList.h"
#include "GameClient/FXPlayback.h"

#include <cmath>
#include <memory>
#include <string>

namespace
{

// a dummy object that has a drawable, for the object-only sinks (TintDrawable, AttachedModel, Laser)
class DrawableDummy : public FXDummyObject
{
public:
	bool hasDrawable() const override { return true; }
};

const char *kSinkProbe = "FXList SinkProbe\n"
						 "  TerrainScorch\n"
						 "    Type = RANDOM\n"
						 "    Weather = NORMAL\n"
						 "    RandomRange = X:0 Y:3\n"
						 "    Radius = 24\n"
						 "  End\n"
						 "  LightPulse\n"
						 "    Color = R:128 G:64 B:0\n"
						 "    Radius = 40\n"
						 "    IncreaseTime = 200\n"
						 "    DecreaseTime = 1000\n"
						 "  End\n"
						 "  CameraShakerVolume\n"
						 "    Radius = 300\n"
						 "    Duration_Seconds = 1.5\n"
						 "    Amplitude_Degrees = 3\n"
						 "  End\n"
						 "  DynamicDecal\n"
						 "    DecalName = EXCloud01\n"
						 "    Size = 100\n"
						 "    Color = R:255 G:0 B:0\n"
						 "    StartingDelay = 100\n"
						 "    Lifetime = 1000\n"
						 "    OpacityStart = 10\n"
						 "    OpacityFadeTimeOne = 200\n"
						 "    OpacityPeak = 255\n"
						 "    OpacityPeakTime = 100\n"
						 "    OpacityFadeTimeTwo = 300\n"
						 "    OpacityEnd = 0\n"
						 "  End\n"
						 "  ViewShake\n"
						 "    Type = STRONG\n"
						 "  End\n"
						 "End\n"
						 "FXList ObjectProbe\n"
						 "  TintDrawable\n"
						 "    Color = R:180 G:0 B:0\n"
						 "    PreColorTime = 10\n"
						 "    PostColorTime = 10\n"
						 "    SustainedColorTime = 880\n"
						 "  End\n"
						 "  AttachedModel\n"
						 "    Modelname = EXTorch\n"
						 "    RandomlyRotate = No\n"
						 "    ExpireTimer = 40\n"
						 "  End\n"
						 "  Laser\n"
						 "    LaserName = LaserProbe\n"
						 "    LaserBackwards = No\n"
						 "  End\n"
						 "End\n"
						 "FXList SmallLightPulse\n"
						 "  LightPulse\n"
						 "    Color = R:128 G:64 B:0\n"
						 "    Radius = 12\n"
						 "    IncreaseTime = 200\n"
						 "    DecreaseTime = 1000\n"
						 "  End\n"
						 "End\n";

struct Rig
{
	initest::Fixture fixture;
	std::unique_ptr<FXPlayback> fx;
	explicit Rig(const char *text = kSinkProbe) : fx(std::make_unique<FXPlayback>(fixture.fsys, RandomAlgorithm::RotWK_GameDat_LCG))
	{
		const std::vector<std::string> errors = fx->loadIniText("bldfx1.ini", text);
		const std::string firstError = errors.empty() ? std::string() : errors.front();
		REQUIRE_MESSAGE(errors.empty(), firstError);
		fx->seedClientRandom(2026);
	}
};

bool eventKind(const FXPlayback &fx, const std::string &kind)
{
	for (const FXPlayback::Event &e : fx.events())
	{
		if (e.kind == kind)
		{
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("bldfx1 scorches: addScorch retires a duplicate within radius/4 and the 500-entry buffer replaces the oldest (RW 0x5E0045 / DONOR BFME2 0x67EA3)")
{
	ScorchBuffer b;
	b.add(Coord3D{ 100.0f, 200.0f, 0.0f }, 24.0f, 2);
	b.add(Coord3D{ 400.0f, 200.0f, 0.0f }, 24.0f, 2);
	CHECK(b.entries().size() == 2);
	CHECK(b.additions() == 2);
	// |dx| = 3 < 24 * 0.25 = 6, the same radius and type: the entry already there stays (its flag is cleared), nothing is added
	const std::uint64_t version = b.version();
	b.add(Coord3D{ 103.0f, 200.0f, 0.0f }, 24.0f, 2);
	CHECK(b.entries().size() == 2);
	CHECK(b.duplicates() == 1);
	CHECK(b.additions() == 2);
	CHECK(b.entries()[0].position.x == 100.0f);
	CHECK(b.version() == version); // nothing to redraw
	// the same point with another type is not a duplicate (DONOR BFME2 0x67EA3 compares the kind too)
	b.add(Coord3D{ 103.0f, 200.0f, 0.0f }, 24.0f, 3);
	CHECK(b.entries().size() == 3);
	// the buffer holds 500: the 501st replaces the oldest
	ScorchBuffer ring;
	for (int i = 0; i < ScorchBuffer::kMaxScorches; ++i)
	{
		ring.add(Coord3D{ (float)i * 1000.0f, 0.0f, 0.0f }, 10.0f, 1);
	}
	REQUIRE(ring.entries().size() == (size_t)ScorchBuffer::kMaxScorches);
	CHECK(ring.entries().front().position.x == 0.0f);
	ring.add(Coord3D{ 900000.0f, 0.0f, 0.0f }, 10.0f, 1);
	REQUIRE(ring.entries().size() == (size_t)ScorchBuffer::kMaxScorches);
	CHECK(ring.evictions() == 1);
	CHECK(ring.entries().front().position.x == 1000.0f);
	CHECK(ring.entries().back().position.x == 900000.0f);
}

TEST_CASE("bldfx1 decals: the opacity envelope is DONOR BFME2 0x10C785's (delay, fade one, peak hold, fade two, lifetime)")
{
	DecalFx d;
	d.startFrame = 0;
	d.desc.startDelayFrames = 3;
	d.desc.fadeOneFrames = 2;
	d.desc.opacityStart = 10;
	d.desc.opacityPeak = 200;
	d.desc.peakFrames = 2;
	d.desc.fadeTwoFrames = 4;
	d.desc.opacityEnd = 0;
	d.desc.lifetimeFrames = 20;
	CHECK(d.opacityAt(0) == 0);   // before the delay
	CHECK(d.opacityAt(2) == 0);
	CHECK(d.opacityAt(3) == 10);  // the first fade-one frame: ceil(10 + 0 / 2 * 190)
	CHECK(d.opacityAt(4) == 105); // ceil(10 + 1 / 2 * 190)
	CHECK(d.opacityAt(5) == 200); // the peak hold starts at delay + fade one
	CHECK(d.opacityAt(6) == 200);
	CHECK(d.opacityAt(7) == 200); // the first fade-two frame: ceil(200 + 0 / 4 * -200)
	CHECK(d.opacityAt(8) == 150);
	CHECK(d.opacityAt(9) == 100);
	CHECK(d.opacityAt(10) == 50);
	CHECK(d.opacityAt(11) == 0); // the fade-two end
	CHECK_FALSE(d.expired(19));
	CHECK(d.expired(20));
	// a -1 lifetime never expires (the retail unsigned conversion is not a finite frame count)
	d.desc.lifetimeFrames = -1;
	CHECK_FALSE(d.expired(100000));
}

TEST_CASE("bldfx1 lights: createLightPulse's range floor, FrameFade ramp and decay (RW 0x5DFAA3 / DONOR BFME2 0x4423C / 0x6DF81 / 0x6DE3D)")
{
	LightPulsePool pool;
	// the range is inner radius 1 + the nugget's radius; below 21 no light is made (RW 0x5DFAA3 / DONOR BFME2 0x4423C)
	CHECK_FALSE(pool.add(Coord3D{ 0, 0, 0 }, RGBColor{ 0.5f, 0.25f, 0.0f }, 12.0f, 1, 5));
	CHECK(pool.refusedRange() == 1);
	CHECK(pool.lights().empty());
	REQUIRE(pool.add(Coord3D{ 10.0f, 20.0f, 30.0f }, RGBColor{ 0.5f, 0.25f, 0.0f }, 40.0f, 1, 5));
	REQUIRE(pool.lights().size() == 1);
	const LightPulseFx &l = pool.lights()[0];
	CHECK(l.enabled);
	CHECK(l.farAttenStart == 1.0f);
	CHECK(l.farAttenEnd == 41.0f);
	// On_Frame_Update: the increase ramp first (one frame: 1 / 1), then the decay's 5 frames
	pool.step();
	CHECK(pool.lights()[0].scale() == 1.0f);
	CHECK(pool.lights()[0].range() == 41.0f);
	pool.step();
	CHECK(pool.lights()[0].scale() == 0.8f);
	CHECK(pool.lights()[0].range() == (4.0f / 5.0f) * 41.0f); // DONOR BFME2 0x6DE3D: scale * FarAttenEnd
	CHECK(pool.lights()[0].colorNow().red == 0.5f * (4.0f / 5.0f));
	pool.step();
	pool.step();
	pool.step();
	CHECK(pool.lights()[0].scale() == 0.2f);
	pool.step(); // the decay counter reaches 0: the light goes out (DONOR BFME2 0x6DE3D)
	CHECK_FALSE(pool.lights()[0].enabled);
	CHECK(pool.lights().size() == 1); // the expired light stays as a reusable slot ...
	CHECK(pool.enabledCount() == 0);  // ... and is not counted as lit
}

TEST_CASE("bldfx1 camera shaker: Compute_Rotations' distance falloff, determinism and expiry (RW 0x5DFE90 / DONOR BFME2 0x65B09)")
{
	CameraShakerQueue a, b;
	a.add(Coord3D{ 0.0f, 0.0f, 0.0f }, 100.0f, 1.0f, 3.0f); // degrees -> radians, RW 0x5DFE90 / DONOR BFME2 0x65DAD
	b.add(Coord3D{ 0.0f, 0.0f, 0.0f }, 100.0f, 1.0f, 3.0f);
	CHECK(a.shakers().size() == 1);
	CHECK(a.shakers()[0].amplitudeRadians == 3.0f * 3.14159265358979323846f / 180.0f);
	float atA[3], atB[3], outside[3];
	a.anglesAt(Coord3D{ 0.0f, 0.0f, 0.0f }, atA);
	b.anglesAt(Coord3D{ 0.0f, 0.0f, 0.0f }, atB);
	a.anglesAt(Coord3D{ 200.0f, 0.0f, 0.0f }, outside); // outside the radius: nothing
	CHECK(atA[0] == atB[0]); // the private client generator is fixed-seeded (INFERENCE, S-687)
	CHECK(atA[1] == atB[1]);
	CHECK(atA[2] == atB[2]);
	CHECK(outside[0] == 0.0f);
	CHECK(outside[1] == 0.0f);
	CHECK(outside[2] == 0.0f);
	// the shakers expire after their duration (1 s at the client's 1/30 s frame)
	for (int i = 0; i < 31; ++i)
	{
		a.step(1.0f / 30.0f);
	}
	CHECK(a.shakers().empty());
	CHECK(a.additions() == 1);
}

TEST_CASE("bldfx1 dispatch: the SinkProbe FXList fires TerrainScorch, LightPulse, CameraShakerVolume and DynamicDecal with the nugget's values")
{
	Rig r;
	const Coord3D at{ 500.0f, 600.0f, 10.0f };
	REQUIRE(r.fx->playFXList("SinkProbe", at, false));
	// TerrainScorch: Type RANDOM -> GameClientRandomValue(0, 3) (RW 0x5E0045), radius 24 (the NORMAL row; the flat test world's weather is NORMAL)
	REQUIRE(r.fx->scorches().entries().size() == 1);
	const ScorchFx &s = r.fx->scorches().entries()[0];
	CHECK(s.position.x == at.x);
	CHECK(s.position.y == at.y);
	CHECK(s.position.z == at.z);
	CHECK(s.radius == 24.0f);
	CHECK((s.type == 0 || s.type == 1 || s.type == 2 || s.type == 3));
	// LightPulse: color R:128 G:64 B:0 (the parser's 0 .. 255 -> 0 .. 1), inner 1 + radius 40 -> 41, 200 ms / 1000 ms -> 1 / 5 frames
	REQUIRE(r.fx->lightPulses().lights().size() == 1);
	const LightPulseFx &l = r.fx->lightPulses().lights()[0];
	CHECK(l.position.x == at.x);
	CHECK(l.color.red == NumericState::pc24Mul(128.0f, 1.0f / 255.0f));
	CHECK(l.color.green == NumericState::pc24Mul(64.0f, 1.0f / 255.0f));
	CHECK(l.farAttenEnd == 41.0f);
	CHECK(l.increaseFrames == 1);
	CHECK(l.decreaseFrames == 5);
	// CameraShakerVolume: the FXPlays' queue keeps it for the view (RW 0x5DFE90 / DONOR BFME2 0x65DAD)
	std::vector<CameraShakerFx> shakers = r.fx->takeCameraShakers();
	REQUIRE(shakers.size() == 1);
	CHECK(shakers[0].position.x == at.x);
	CHECK(shakers[0].radius == 300.0f);
	CHECK(shakers[0].durationSeconds == 1.5f);
	CHECK(shakers[0].amplitudeRadians == 3.0f * 3.14159265358979323846f / 180.0f);
	CHECK(r.fx->takeCameraShakers().empty()); // taken once
	// DynamicDecal: the frames are trunc(ms * 0.03): delay 3, fade one 6, peak time 3, fade two 9, lifetime 30
	REQUIRE(r.fx->decals().size() == 1);
	const DecalFx &d = r.fx->decals()[0];
	CHECK(d.desc.name == "EXCloud01");
	CHECK(d.desc.size == 100.0f);
	CHECK(d.desc.color == 0xFF0000u);
	CHECK(d.desc.startDelayFrames == 3);
	CHECK(d.desc.fadeOneFrames == 6);
	CHECK(d.desc.peakFrames == 3);
	CHECK(d.desc.fadeTwoFrames == 9);
	CHECK(d.desc.lifetimeFrames == 30);
	CHECK(d.startFrame == 0);
	// ViewShake is the view's own path: with no world it is noted as an event (the live game's LiveFX takes it)
	CHECK(eventKind(*r.fx, "viewShake"));
}

TEST_CASE("bldfx1 dispatch: a light pulse below the 21-unit floor makes no light (RW 0x5DFAA3 / DONOR BFME2 0x4423C)")
{
	Rig r;
	REQUIRE(r.fx->playFXList("SmallLightPulse", Coord3D{ 0.0f, 0.0f, 0.0f }, false));
	CHECK(r.fx->lightPulses().lights().empty());
	CHECK(r.fx->lightPulses().refusedRange() == 1);
}

TEST_CASE("bldfx1 dispatch: TintDrawable, AttachedModel and Laser are recorded, not presented (stop S-687)")
{
	Rig r;
	DrawableDummy obj;
	obj.pos = Coord3D{ 10.0f, 20.0f, 30.0f };
	const FXList *fx = r.fx->fxLists().findFXList("ObjectProbe");
	REQUIRE(fx != nullptr);
	FXList::doFXObj(fx, r.fx->services(), r.fx->fxLists(), &obj, nullptr);
	CHECK(eventKind(*r.fx, "tint"));
	CHECK(eventKind(*r.fx, "attachModel"));
	CHECK(eventKind(*r.fx, "laser"));
	// their presentation state is not invented: no scorch, light or decal was made
	CHECK(r.fx->scorches().entries().empty());
	CHECK(r.fx->lightPulses().lights().empty());
	CHECK(r.fx->decals().empty());
}

TEST_CASE("bldfx1 dispatch: a damage-state transition's FXList dispatches through the same sinks (TransitionDamageFX, S-686)")
{
	// The transition's own event ordering is pinned by test_fx4_transition.cpp (RW 0x8BE077); this case pins that the FXList the transition
	// emits plays the sinks of this lane, at the object's position, with the same values a POSITION_FX play has.
	Rig r;
	const Coord3D at{ -100.0f, 50.0f, 4.0f };
	REQUIRE(r.fx->playFXList("SinkProbe", at, false));
	CHECK(r.fx->scorches().additions() == 1);
	CHECK(r.fx->lightPulses().additions() == 1);
	CHECK(r.fx->decals().size() == 1);
	CHECK(r.fx->takeCameraShakers().size() == 1);
}
