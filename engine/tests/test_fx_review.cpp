// OpenBFME unit tests: the FX-1 review round (compositing order rules, particle texture resolution, reported approximations). GPL-3.0.

#include "FxTestUtil.h"

#include "GameClient/ParticleDraw.h"
#include "GameClient/ParticleTexture.h"

#include <set>

using namespace FXParticleSystem;
using namespace fxtest;

namespace
{
std::string withDraw2(const std::string &name, const std::string &system, const std::string &modules, const std::string &draw)
{
	return "FXParticleSystem " + name + "\n  System\n    Priority = ALWAYS_RENDER\n" + system + "  End\n" + modules + draw + "End\n";
}
} // namespace

TEST_CASE("review: batch order is strictly monotone for every batch count, including more batches than material priorities")
{
	for (size_t count : { (size_t)1, (size_t)2, (size_t)120, (size_t)121, (size_t)122, (size_t)255, (size_t)256, (size_t)257, (size_t)1000, (size_t)5000 })
	{
		BatchOrder prev = OrderForBatch(0, count);
		for (size_t rank = 1; rank < count; ++rank)
		{
			const BatchOrder o = OrderForBatch(rank, count);
			const bool later = o.priority > prev.priority || (o.priority == prev.priority && o.sortOffset > prev.sortOffset);
			REQUIRE_MESSAGE(later, "rank " << rank << " of " << count << " is not after rank " << rank - 1);
			REQUIRE(o.priority >= -128);
			REQUIRE(o.priority <= 127);
			prev = o;
		}
	}
	// up to 120 batches every rank has its own priority (no saturation): rank 119 differs from rank 118 and no offset is needed
	CHECK(OrderForBatch(118, 120).priority + 1 == OrderForBatch(119, 120).priority);
	CHECK(OrderForBatch(119, 120).sortOffset == 0.0f);
}

TEST_CASE("review: only non-deferred ALPHA sprite batches are reordered back to front (deferred systems bypass the sorting renderer, RW 0x44cbed)")
{
	CHECK(SpriteBatchDepthSorted(PARTICLE_SHADER_ALPHA, false));
	CHECK(SpriteBatchDepthSorted(PARTICLE_SHADER_ALPHA_NO_DEPTH_TEST, false));
	CHECK(!SpriteBatchDepthSorted(PARTICLE_SHADER_ALPHA, true));
	CHECK(!SpriteBatchDepthSorted(PARTICLE_SHADER_ADDITIVE, false));
	CHECK(!SpriteBatchDepthSorted(PARTICLE_SHADER_ALPHA_TEST, false));
}

TEST_CASE("review: particle texture resolution tries DDS, TGA, JPG in the compiled folder; PNG is auxiliary after a JPG only (RW 0x530d29)")
{
	std::set<std::string> files;
	auto exists = [&](const std::string &p) { return files.count(p) != 0; };
	// a TGA name backed only by a JPG resolves to the JPG
	files = { "art\\compiledtextures\\fo\\foo.jpg" };
	ParticleTextureResolution r = ResolveParticleTexture("Foo.tga", exists);
	REQUIRE(r.Found);
	CHECK(r.Kind == ParticleTextureResolution::JPG);
	CHECK(r.Path == "art\\compiledtextures\\fo\\foo.jpg");
	CHECK(r.AuxPngPath.empty());
	// the PNG next to a resolved JPG is the auxiliary image
	files = { "art\\compiledtextures\\fo\\foo.jpg", "art\\compiledtextures\\fo\\foo.png" };
	r = ResolveParticleTexture("foo.tga", exists);
	CHECK(r.Kind == ParticleTextureResolution::JPG);
	CHECK(r.AuxPngPath == "art\\compiledtextures\\fo\\foo.png");
	// a PNG alone is NOT a main candidate
	files = { "art\\compiledtextures\\fo\\foo.png" };
	r = ResolveParticleTexture("foo.tga", exists);
	CHECK(!r.Found);
	// order: DDS before TGA before JPG
	files = { "art\\compiledtextures\\fo\\foo.jpg", "art\\compiledtextures\\fo\\foo.tga" };
	CHECK(ResolveParticleTexture("foo.dds", exists).Kind == ParticleTextureResolution::TGA);
	files.insert("art\\compiledtextures\\fo\\foo.dds");
	CHECK(ResolveParticleTexture("FOO.TGA", exists).Kind == ParticleTextureResolution::DDS);
	// apt_ names and one-character names live in Art\Textures
	files = { "art\\textures\\apt_logo.tga", "art\\textures\\x.tga" };
	r = ResolveParticleTexture("apt_logo.tga", exists);
	CHECK(r.Found);
	CHECK(r.Path == "art\\textures\\apt_logo.tga");
	CHECK(ResolveParticleTexture("x.tga", exists).Path == "art\\textures\\x.tga");
	// nothing found: not Found, every candidate recorded
	files.clear();
	r = ResolveParticleTexture("nothing.tga", exists);
	CHECK(!r.Found);
	CHECK(r.Tried.size() == 3u);
}

TEST_CASE("review: TERRAIN_PARTICLE (type 6) draws nothing and is reported (S-195); ribbons are reported as an approximation (S-199)")
{
	const std::string sys = "    Lifetime = 50 50\n    Size = 1 1\n    BurstCount = 4 4\n    BurstDelay = 100 100\n";
	Sim sim(withDraw2("Terrain", sys + "    Type = TERRAIN_PARTICLE\n", std::string(kPointOrtho), "  Draw = DefaultDraw\n  End\n") +
		withDraw2("Streak", sys + "    Type = STREAK\n    ParticleName = Trail.tga\n",
			"  EmissionVolume = LineEmissionVolume\n    StartPoint = X:0 Y:0 Z:0\n    EndPoint = X:30 Y:0 Z:0\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n"
			"  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n  Update = DefaultUpdate\n  End\n",
			"  Draw = StreakDraw\n  End\n"));
	sim.spawn("Terrain");
	sim.spawn("Streak");
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 1u); // only the streak
	CHECK(batches[0].ribbon);
	const std::vector<std::string> u = builder.unverified();
	REQUIRE(u.size() == 2u);
	CHECK(u[0].compare(0, 5, "S-195") == 0);
	CHECK(u[0].find("TERRAIN_PARTICLE") != std::string::npos);
	CHECK(u[1] == std::string("S-199: ") + RibbonApproximationText());
}

TEST_CASE("review: the sorting and fog approximations carry their S-198 text")
{
	CHECK(std::string(SortApproximationText()).find("RW 0x44c982 / 0x44cbed") != std::string::npos);
	CHECK(std::string(FogOmissionText()).find("0x9620b7") != std::string::npos);
}
