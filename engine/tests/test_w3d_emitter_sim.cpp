// OpenBFME unit tests: the W3D model emitter simulation (lane FX-1, WW3D2 part_emt / part_buf as RotWK runs them). GPL-3.0.
//
// Expected values come from the lane notes fx-w3d-emitters.md (sections 6, 7, 11, 12): the machine-validated integer streams, the derived tables
// and hand-computable traces of the four retail emitters (E_Fire_SM, E_FireSparks_SM, E_MumaFlies, E_Smoke_SM). The retail definitions live
// only in BFME2's w3d.big, so the retail cases run when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise they print SKIP). The synthetic
// cases build a ParticleEmitterDefClass by hand and need no files.

#include "doctest.h"

#include "GameClient/ParticleDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/part_emt.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

#include <cmath>
#include <cstring>

using doctest::Approx;

namespace
{

bool near(float a, float b, float rel = 2e-6f, float abs = 1e-9f)
{
	return std::fabs(a - b) <= std::max(abs, rel * std::max(std::fabs(a), std::fabs(b)));
}

#define CHECK_NEAR(a, b) CHECK_MESSAGE(near((float)(a), (float)(b)), (a) << " vs " << (b))

// A one-key-per-property emitter, 10 particles per second, lifetime 1 s, no randomness.
ParticleEmitterDefClass simpleDef()
{
	ParticleEmitterDefClass d;
	d.Name = "T";
	std::strcpy(d.Info.TextureFilename, "x.tga");
	d.Info.Lifetime = 1.0f;
	d.Info.EmissionRate = 10.0f;
	d.Info.Velocity = { 0.0f, 0.0f, 1000.0f };
	d.InfoV2.BurstSize = 1;
	d.Props.ColorKeyframes = 1;
	d.Props.OpacityKeyframes = 1;
	d.Props.SizeKeyframes = 1;
	W3dEmitterColorKeyframeStruct ck = {};
	ck.Color = { 255, 255, 255, 255 };
	d.ColorKeyframes.push_back(ck);
	W3dEmitterOpacityKeyframeStruct ok = {};
	ok.Opacity = 1.0f;
	d.OpacityKeyframes.push_back(ok);
	W3dEmitterSizeKeyframeStruct sk = {};
	sk.Size = 2.0f;
	d.SizeKeyframes.push_back(sk);
	return d;
}

Matrix3D identity()
{
	return Matrix3D();
}

} // namespace

TEST_CASE("W3D emitter: Random3Class (RW 0xa2cff0) fresh-stream outputs")
{
	Random3Class r(0, 0);
	const int expected[12] = { -1389450728, 1718636987, -1095223203, 311666390, -1709427683, 1214822812, -2143479856, 1453024687, -1218269987, -183037834,
		-2025860084, 790543895 };
	for (int i = 0; i < 12; ++i)
	{
		CHECK(r() == expected[i]);
	}
}

TEST_CASE("W3D emitter: Random4Class seeded 4357 (RW 0xa2d140) fresh-stream outputs")
{
	Random4Class r(4357);
	const int expected[8] = { -784561419, -4033406, -2103011957, 564929546, 152112058, -32343104, -1607568878, 268830360 };
	for (int i = 0; i < 8; ++i)
	{
		CHECK(r() == expected[i]);
	}
}

TEST_CASE("W3D emitter: Inv_Sqrt is within 1e-6 relative of 1/sqrt")
{
	for (float x : { 0.25f, 1.0f, 2.0f, 3.5f, 100.0f, 0.0123f })
	{
		CHECK(std::fabs(WWMath_Inv_Sqrt(x) * std::sqrt(x) - 1.0f) < 1e-6f);
	}
}

TEST_CASE("W3D emitter: constant emitter, one particle per period, ring bookkeeping and analytic motion")
{
	// EmissionRate 10 -> EmitRate 100 ms; MaxNum = (int)((1+1)*1*10) = 20; MaxAge 1000; velocity 1000 u/s = 1 u/ms.
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(simpleDef(), identity());
	CHECK(e->emitRateMs() == 100u);
	CHECK(e->maxParticles() == 20u);
	CHECK(e->maxAgeMs() == 1000u);
	CHECK_NEAR(e->baseVelocity().Z, 1.0f);
	CHECK(e->colorTable().constant);
	CHECK(e->sizeTable().constant);
	// frames 1..3: EmitRemain 33, 66, 99 -> nothing; frame 4: 132 > 100 -> one particle with ts = sync - 32
	for (std::uint32_t f = 1; f <= 3; ++f)
	{
		world.sync(33 * f);
		world.update();
		CHECK(e->particleCount() == 0u);
		CHECK(e->emitRemain() == 33 * f);
	}
	world.sync(33 * 4);
	world.update();
	CHECK(e->emitRemain() == 32u);
	// the particle sits in the queue until the next kinematic update
	world.sync(33 * 5);
	world.update();
	CHECK(e->particleCount() == 1u);
	CHECK(e->slotTimeStamp(0) == 33u * 4 - 32u);
	// born at age 33 * 5 - 100 = 65 ms: position = vel * age
	CHECK_NEAR(e->slotPosition(0).Z, 65.0f);
	std::vector<ParticleEmitterInstance::Visual> v;
	e->collectVisuals(v);
	REQUIRE(v.size() == 1u);
	CHECK(v[0].size == 2.0f);
	CHECK(v[0].alpha == 1.0f);
	CHECK(v[0].color.X == 1.0f);
}

TEST_CASE("W3D emitter: acceleration is multiplied by 1e-6f and integrated analytically ((A*0.5)*age ordering)")
{
	ParticleEmitterDefClass d = simpleDef();
	d.Info.Velocity = { 0.0f, 0.0f, 0.0f };
	d.Info.Acceleration = { 0.0f, 0.0f, 2000.0f }; // 2000 u/s^2 = 0.002 u/ms^2
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	CHECK(e->acceleration().Z == 2000.0f * 1e-6f);
	for (std::uint32_t f = 1; f <= 5; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	// the particle (ts 100) is created at sync 165 with age 65: pos = (v + A*0.5*age) * age with v = 0, vel = A * age
	CHECK_NEAR(e->slotPosition(0).Z, (2000.0f * 1e-6f * 0.5f * 65.0f) * 65.0f);
	CHECK_NEAR(e->slotVelocity(0).Z, 2000.0f * 1e-6f * 65.0f);
	// one more step of 33 ms: pos += vel*dt + A*(dt*dt*0.5)
	const float p0 = e->slotPosition(0).Z, v0 = e->slotVelocity(0).Z, a = 2000.0f * 1e-6f;
	world.sync(33 * 6);
	world.update();
	CHECK_NEAR(e->slotPosition(0).Z, p0 + (v0 * 33.0f + a * ((33.0f * 33.0f) * 0.5f)));
	CHECK_NEAR(e->slotVelocity(0).Z, v0 + a * 33.0f);
}

TEST_CASE("W3D emitter: particles die when age >= MaxAge and MaxEmissions completes the emitter")
{
	ParticleEmitterDefClass d = simpleDef();
	d.Info.MaxEmissions = 2.0f;
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	CHECK(e->maxParticles() == 2u); // min(20, MaxEmissions)
	std::uint32_t f = 1;
	for (; f <= 60; ++f)
	{
		world.sync(33 * f);
		world.update();
		if (e->isComplete())
		{
			break;
		}
	}
	CHECK(f > 30u);
	CHECK(e->particleCount() == 0u);
	CHECK(!e->isActive() == false); // emission stopped by IsComplete, the Start flag stays
}

TEST_CASE("W3D emitter: key tables - times chop, a key at or after MaxAge only defines the last slope")
{
	ParticleEmitterDefClass d = simpleDef();
	d.Props.SizeKeyframes = 3;
	W3dEmitterSizeKeyframeStruct k1 = {}, k2 = {};
	k1.Time = 0.5f;
	k1.Size = 4.0f;
	k2.Time = 1.0f; // == lifetime: not a key frame, defines the slope after key 1
	k2.Size = 0.0f;
	d.SizeKeyframes.push_back(k1);
	d.SizeKeyframes.push_back(k2);
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	const W3DPropertyTable &t = e->sizeTable();
	REQUIRE(!t.constant);
	REQUIRE(t.times.size() == 2u);
	CHECK(t.times[1] == 500u);
	CHECK_NEAR(t.values[1], 4.0f);
	CHECK_NEAR(t.deltas[0], (4.0f - 2.0f) / 500.0f);
	CHECK_NEAR(t.deltas[1], (0.0f - 4.0f) / (1.0f * 1000.0f - 500.0f));
}

TEST_CASE("W3D emitter: creation volume draws x, y, z from the shared Random3 stream; rotation uses the emitter quaternion")
{
	ParticleEmitterDefClass d = simpleDef();
	d.Info.Velocity = { 0.0f, 0.0f, 0.0f };
	d.InfoV2.CreationVolume.ClassID = 0; // SolidBox
	d.InfoV2.CreationVolume.Value1 = 2.0f;
	d.InfoV2.CreationVolume.Value2 = 4.0f;
	d.InfoV2.CreationVolume.Value3 = 8.0f;
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	for (std::uint32_t f = 1; f <= 5; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	const float s = 1.0f / 2147483648.0f;
	CHECK_NEAR(e->slotPosition(0).X, (float)-1389450728 * s * 2.0f);
	CHECK_NEAR(e->slotPosition(0).Y, (float)1718636987 * s * 4.0f);
	CHECK_NEAR(e->slotPosition(0).Z, (float)-1095223203 * s * 8.0f);
}

TEST_CASE("W3D emitter retail: E_Fire_SM tables and first particles (notes section 11.1)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("W3D emitter retail");
		return;
	}
	ArchiveW3DFileSource src(*mount->fs);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(src.Read(W3D_Asset_Path("e_fire_sm"), bytes, &error), error);
	W3DFileContents contents;
	REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), contents, &error), error);
	REQUIRE(contents.Emitters.size() == 1u);
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(contents.Emitters[0], identity());
	CHECK(e->emitRateMs() == 33u);
	CHECK(!e->emitPeriodPrecisionSensitive());
	CHECK(e->maxParticles() == 90u);
	CHECK(e->maxAgeMs() == 2000u);
	const W3DPropertyTable &c = e->colorTable();
	REQUIRE(c.times.size() == 4u);
	CHECK(c.times[1] == 317u);
	CHECK(c.times[2] == 685u);
	CHECK(c.times[3] == 1978u);
	CHECK_NEAR(c.cdeltas[0].X, 0.00200408255f);
	CHECK_NEAR(c.cdeltas[1].Y, -0.00130008534f);
	CHECK(e->alphaTable().constant);
	const W3DPropertyTable &sz = e->sizeTable();
	REQUIRE(sz.times.size() == 3u);
	CHECK_NEAR(sz.deltas[0], 0.0030511159f);
	CHECK_NEAR(sz.deltas[2], -0.00143697078f);
	REQUIRE(sz.random.size() == 32u);
	CHECK_NEAR(sz.random[0], -0.0182669945f);
	CHECK_NEAR(sz.random[1], -9.39100501e-05f);
	CHECK_NEAR(sz.random[2], -0.0489645638f);
	CHECK_NEAR(sz.random[3], 0.0131532913f);
	// emission trace: frame 1 nothing, later frames one particle each
	world.sync(33);
	world.update();
	CHECK(e->emitRemain() == 33u);
	world.sync(66);
	world.update();
	world.sync(99);
	world.update();
	world.sync(132);
	world.update();
	CHECK(e->particleCount() >= 2u);
	CHECK(e->slotTimeStamp(0) == 33u);
	CHECK(e->slotTimeStamp(1) == 66u);
	// the first particle was created at sync 99 with age 66: pos(t0) + vel * 66
	CHECK_NEAR(e->slotVelocity(0).X, 0.0015f);
	CHECK_NEAR(e->slotVelocity(0).Y, -0.000895516539f);
	CHECK_NEAR(e->slotVelocity(0).Z, 0.00721602049f);
	CHECK_NEAR(e->slotPosition(0).X, -0.181972504f + 0.0015f * 99.0f);
	// visual state at age 0 of slot 0 (sync 33 + 0): run to the particle's own birth time is not possible, so check the derived mid values instead
}

namespace
{

bool loadRetailEmitter(const char *stem, W3DFileContents &contents)
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		return false;
	}
	ArchiveW3DFileSource src(*mount->fs);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(src.Read(W3D_Asset_Path(stem), bytes, &error), error);
	REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), contents, &error), error);
	REQUIRE(contents.Emitters.size() == 1u);
	return true;
}

// Runs 8 client frames (sync 33..264) then moves the sync clock to first-particle-birth + age and returns the oldest particle's visual state.
ParticleEmitterInstance::Visual visualAt(const ParticleEmitterDefClass &def, std::uint32_t firstTs, std::uint32_t age)
{
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(def, identity());
	for (std::uint32_t f = 1; f <= 8; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	REQUIRE(e->particleCount() >= 1u);
	REQUIRE(e->slotTimeStamp(e->ringStart()) == firstTs);
	world.sync(firstTs + age);
	std::vector<ParticleEmitterInstance::Visual> v;
	e->collectVisuals(v);
	REQUIRE(!v.empty());
	return v[0];
}

} // namespace

TEST_CASE("W3D emitter retail: E_Fire_SM visual state of the oldest particle (notes 11.1)")
{
	W3DFileContents c;
	if (!loadRetailEmitter("e_fire_sm", c))
	{
		retailtest::printSkip("W3D emitter retail");
		return;
	}
	const ParticleEmitterDefClass &d = c.Emitters[0];
	ParticleEmitterInstance::Visual v = visualAt(d, 33, 500);
	CHECK_NEAR(v.color.X, 0.461732745f);
	CHECK_NEAR(v.color.Y, 0.35816282f);
	CHECK_NEAR(v.color.Z, 0.236572891f);
	CHECK_NEAR(v.size, 1.34120452f);
	CHECK(v.frame == 5);
	v = visualAt(d, 33, 1000);
	CHECK_NEAR(v.color.X, 0.216532469f);
	CHECK_NEAR(v.color.Y, 0.0889859423f);
	CHECK_NEAR(v.size, 1.36870384f);
	v = visualAt(d, 33, 1900);
	CHECK_NEAR(v.color.X, 0.0172694623f);
	CHECK_NEAR(v.color.Y, 0.0070970431f);
	CHECK_NEAR(v.size, 0.0754301548f);
	v = visualAt(d, 33, 1999);
	CHECK(v.color.X == 0.0f);
	CHECK(v.size == 0.0f);
}

TEST_CASE("W3D emitter retail: E_FireSparks_SM (additive, outward velocity, frame 13; notes 11.2)")
{
	W3DFileContents c;
	if (!loadRetailEmitter("e_firesparks_sm", c))
	{
		retailtest::printSkip("W3D emitter retail");
		return;
	}
	const ParticleEmitterDefClass &d = c.Emitters[0];
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	CHECK(e->emitRateMs() == 100u);
	CHECK(e->maxParticles() == 40u);
	CHECK(e->maxAgeMs() == 3000u);
	CHECK(e->colorTable().times.size() == 1u);
	CHECK_NEAR(e->colorTable().cdeltas[0].X, -0.000333333301f);
	CHECK_NEAR(e->alphaTable().deltas[0], -0.000333333301f);
	REQUIRE(e->sizeTable().times.size() == 2u);
	CHECK(e->sizeTable().times[1] == 2978u);
	CHECK_NEAR(e->sizeTable().deltas[0], -1.67897924e-05f);
	// emission at frames 4, 7, 10: the first particles carry ts 100, 200, 300
	for (std::uint32_t f = 1; f <= 11; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	REQUIRE(e->particleCount() == 3u);
	CHECK(e->slotTimeStamp(0) == 100u);
	CHECK(e->slotTimeStamp(1) == 200u);
	CHECK(e->slotTimeStamp(2) == 300u);
	CHECK_NEAR(e->slotVelocity(0).X, 0.000647924258f);
	CHECK_NEAR(e->slotVelocity(0).Y, 0.000435488997f);
	CHECK_NEAR(e->slotVelocity(0).Z, 0.00417960063f);
	ParticleEmitterInstance::Visual v = visualAt(d, 100, 1000);
	CHECK_NEAR(v.color.X, 0.666666687f);
	CHECK_NEAR(v.alpha, 0.666666687f);
	CHECK_NEAR(v.size, 0.114943221f);
	CHECK(v.frame == 13);
}

TEST_CASE("W3D emitter retail: E_MumaFlies (alpha blend, acceleration, rotation, sphere volumes, velocity inheritance; notes 11.3)")
{
	W3DFileContents c;
	if (!loadRetailEmitter("e_mumaflies", c))
	{
		retailtest::printSkip("W3D emitter retail");
		return;
	}
	const ParticleEmitterDefClass &d = c.Emitters[0];
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	CHECK(e->maxParticles() == 250u);
	CHECK(e->maxAgeMs() == 2000u);
	// R1: the emit period is 12 ms with float-rounded division (PC_24), 11 ms under extended precision; the port follows PC_24 and reports the
	// unestablished precision state as stop S-196 (the other three emitters are insensitive)
	CHECK(e->emitRateMs() == 12u);
	CHECK(e->emitRateExtendedMs() == 11u);
	CHECK(e->emitPeriodPrecisionSensitive());
	{
		std::vector<FXParticleSystem::DrawBatch> batches;
		const std::vector<std::string> stops = FXParticleSystem::BuildEmitterBatches(world, FXParticleSystem::DrawCamera(), batches);
		bool found = false;
		for (const std::string &s : stops)
		{
			found = found || (s.compare(0, 5, "S-196") == 0 && s.find("every 12 ms") != std::string::npos && s.find("every 11 ms") != std::string::npos);
		}
		CHECK_MESSAGE(found, "the E_MumaFlies emit-period stop line is missing");
	}
	CHECK_NEAR(e->acceleration().X, 7.0000001e-06f);
	CHECK_NEAR(e->acceleration().Y, 4.99999987e-06f);
	CHECK_NEAR(e->acceleration().Z, 3.99999999e-06f);
	CHECK_NEAR(e->baseVelocity().X, 0.00900000427f);
	REQUIRE(e->alphaTable().times.size() == 2u);
	CHECK_NEAR(e->alphaTable().deltas[0], -0.000257961801f);
	CHECK_NEAR(e->alphaTable().deltas[1], -0.00110573135f);
	REQUIRE(e->sizeTable().times.size() == 2u);
	CHECK_NEAR(e->sizeTable().deltas[0], 0.000819672074f);
	CHECK_NEAR(e->sizeTable().deltas[1], -3.30687908e-05f);
	CHECK_NEAR(e->sizeTable().random[0], -0.00365339871f);
	// emission trace: frame 1 emits 2 (ts 12, 24), frame 2 emits 3 (ts 36, 48, 60)
	world.sync(33);
	world.update();
	CHECK(e->emitRemain() == 9u);
	world.sync(66);
	world.update();
	CHECK(e->emitRemain() == 6u);
	world.sync(99);
	world.update();
	REQUIRE(e->particleCount() >= 5u);
	CHECK(e->slotTimeStamp(0) == 12u);
	CHECK(e->slotTimeStamp(1) == 24u);
	CHECK(e->slotTimeStamp(2) == 36u);
	// first particle at birth: pos + vel*age is how it was placed; back out the acceleration term to compare with the notes
	const float age = 99.0f - 12.0f;
	const Vector3 a = e->acceleration();
	const Vector3 p = e->slotPosition(0);
	const Vector3 vel = e->slotVelocity(0);
	CHECK_NEAR(vel.X - a.X * age, 0.00919580366f);
	CHECK(std::fabs(p.X - ((0.00919580366f + a.X * 0.5f * age) * age + 3.19288158f)) < 2e-3f);
	ParticleEmitterInstance::Visual v = visualAt(d, 12, 500);
	CHECK_NEAR(v.alpha, 0.871019125f);
	CHECK_NEAR(v.size, 0.495949775f);
	CHECK(v.orientation == 21);
	v = visualAt(d, 12, 1000);
	CHECK_NEAR(v.alpha, 0.74203819f);
	CHECK_NEAR(v.size, 0.479415387f);
	CHECK(v.orientation == 115);
	v = visualAt(d, 12, 1999);
	CHECK_NEAR(v.alpha, 0.00110572577f);
	CHECK_NEAR(v.size, 0.446379662f);
	CHECK(v.orientation == 241);
}

TEST_CASE("W3D emitter retail: E_Smoke_SM (alpha blend, outward velocity, colour keys, rotation tables; notes 11.4)")
{
	W3DFileContents c;
	if (!loadRetailEmitter("e_smoke_sm", c))
	{
		retailtest::printSkip("W3D emitter retail");
		return;
	}
	const ParticleEmitterDefClass &d = c.Emitters[0];
	W3DEmitterWorld world;
	ParticleEmitterInstance *e = world.create(d, identity());
	CHECK(e->emitRateMs() == 200u);
	CHECK(e->maxParticles() == 35u);
	CHECK(e->maxAgeMs() == 6000u);
	REQUIRE(e->colorTable().times.size() == 2u);
	CHECK(e->colorTable().times[1] == 3530u);
	CHECK_NEAR(e->colorTable().cdeltas[0].X, -0.00021218686f);
	CHECK_NEAR(e->colorTable().cdeltas[1].Z, -2.06398345e-05f);
	CHECK_NEAR(e->alphaTable().deltas[0], 0.000574328995f);
	CHECK_NEAR(e->sizeTable().deltas[0], 0.000833332946f);
	// first emission at frame 7 (ts 200), the second at frame 13 (ts 400)
	for (std::uint32_t f = 1; f <= 14; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	REQUIRE(e->particleCount() == 2u);
	CHECK(e->slotTimeStamp(0) == 200u);
	CHECK(e->slotTimeStamp(1) == 400u);
	CHECK_NEAR(e->slotVelocity(0).X, 0.000936679309f);
	CHECK_NEAR(e->slotVelocity(0).Y, 0.000696781615f);
	CHECK_NEAR(e->slotVelocity(0).Z, 0.0065559675f);
	ParticleEmitterInstance::Visual v = visualAt(d, 200, 1000);
	CHECK_NEAR(v.color.X, 0.787813127f);
	CHECK_NEAR(v.color.Y, 0.615386307f);
	CHECK_NEAR(v.alpha, 0.320845276f);
	CHECK_NEAR(v.size, 2.83333302f);
	CHECK(v.frame == 4);
	v = visualAt(d, 200, 5799);
	CHECK(v.alpha > 0.0f);
}


TEST_CASE("W3D emitter draw: shader mapping (stop S-196)")
{
	using namespace FXParticleSystem;
	W3dShaderStruct s = {};
	s.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE;
	s.DestBlend = W3DSHADER_DESTBLENDFUNC_ONE;
	bool approx = true;
	CHECK(ParticleShaderForW3dShader(s, &approx) == PARTICLE_SHADER_ADDITIVE);
	CHECK(!approx);
	s.AlphaTest = W3DSHADER_ALPHATEST_ENABLE;
	CHECK(ParticleShaderForW3dShader(s, &approx) == PARTICLE_SHADER_ADDITIVE_ALPHA_TEST);
	s.AlphaTest = 0;
	s.SrcBlend = W3DSHADER_SRCBLENDFUNC_SRC_ALPHA;
	s.DestBlend = W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_ALPHA;
	CHECK(ParticleShaderForW3dShader(s, &approx) == PARTICLE_SHADER_ALPHA);
	CHECK(!approx);
	s.AlphaTest = W3DSHADER_ALPHATEST_ENABLE;
	CHECK(ParticleShaderForW3dShader(s, &approx) == PARTICLE_SHADER_ALPHA);
	CHECK(approx); // the stop line
	s.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE_MINUS_SRC_ALPHA;
	CHECK(ParticleShaderForW3dShader(s, &approx) == -1);
}

TEST_CASE("W3D emitter draw: render modes (stop S-197), tri point geometry and the frame atlas cell")
{
	using namespace FXParticleSystem;
	DrawCamera cam; // right +x, up +z
	// a mode 0 (triangle) emitter with FrameMode 3 and a constant frame 5: one particle -> 3 vertices in cell u = 5/8, v = 0
	ParticleEmitterDefClass d = simpleDef();
	d.Info.Velocity = { 0.0f, 0.0f, 0.0f };
	d.InfoV2.Shader.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE;
	d.InfoV2.Shader.DestBlend = W3DSHADER_DESTBLENDFUNC_ONE;
	d.InfoV2.FrameMode = 3;
	d.HasFrames = true;
	d.FrameHeader.KeyframeCount = 0;
	W3dEmitterFrameKeyframeStruct fk = {};
	fk.Frame = 5.0f;
	d.FrameKeyframes.push_back(fk);
	W3DEmitterWorld world;
	world.create(d, identity());
	for (std::uint32_t f = 1; f <= 5; ++f)
	{
		world.sync(33 * f);
		world.update();
	}
	std::vector<DrawBatch> batches;
	std::vector<std::string> stops = BuildEmitterBatches(world, cam, batches);
	CHECK(stops.empty());
	REQUIRE(batches.size() == 1u);
	const DrawBatch &b = batches[0];
	CHECK(b.kind == DrawBatch::MESH);
	CHECK(b.shader == PARTICLE_SHADER_ADDITIVE);
	REQUIRE(b.vertices.size() == 3u);
	// size 2: apex (0,-2)*2 = (0,-4) in (right, up) = (x, z); UV apex (0.5, 0) in cell (5, 0)
	CHECK_NEAR(b.vertices[0].x, 0.0f + 0.0f);
	CHECK_NEAR(b.vertices[0].z, -4.0f);
	CHECK_NEAR(b.vertices[0].u, 5.0f / 8.0f + 0.5f / 8.0f);
	CHECK_NEAR(b.vertices[0].v, 0.0f);
	CHECK_NEAR(b.vertices[1].x, -1.732f * 2.0f);
	CHECK_NEAR(b.vertices[1].u, 5.0f / 8.0f);
	CHECK_NEAR(b.vertices[1].v, 0.866f / 8.0f);
	// a line-mode emitter is reported, not drawn
	d.InfoV2.RenderMode = 2;
	W3DEmitterWorld w2;
	w2.create(d, identity());
	batches.clear();
	stops = BuildEmitterBatches(w2, cam, batches);
	CHECK(batches.empty());
	REQUIRE(stops.size() == 1u);
	CHECK(stops[0].find("S-197") == 0);
}
