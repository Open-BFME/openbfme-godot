// OpenBFME unit tests: the particle draw batches (lane FX-1). GPL-3.0.
//
// Expectations are derived by hand from the INI text and the draw rules of the lane notes (fx-draw.md, fx-draw-gpu.md): doParticles order
// (RW 0x44c84a: SortLevel 1 deferred, reverse list order), the DefaultDraw arrays (RW 0x961dfc: size / colour / alpha / angle byte), the
// vertex colour quantisation (RW 0x57d640), ribbons (RW 0x9624b9), quads (RW 0x963481) and the stops S-194 / S-195.

#include "FxTestUtil.h"

#include "GameClient/ParticleDraw.h"

using namespace FXParticleSystem;
using namespace fxtest;
using doctest::Approx;

namespace
{

std::string withDraw(const std::string &name, const std::string &system, const std::string &modules, const std::string &draw)
{
	return "FXParticleSystem " + name + "\n  System\n    Priority = ALWAYS_RENDER\n" + system + "  End\n" + modules + draw + "End\n";
}

const char *kDefaultDraw = "  Draw = DefaultDraw\n  End\n";

} // namespace

TEST_CASE("draw: a DefaultDraw sprite carries size, quantised colour, alpha and the angle byte (RW 0x961dfc)")
{
	Sim sim(withDraw("Sprite", "    Shader = ALPHA\n    ParticleName = Foo.tga\n    Lifetime = 50 50\n    Size = 10 10\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:51 B:0 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 0.5 0.5 0\n  End\n"
		"  Update = DefaultUpdate\n    AngleZ = 1.5707964 1.5707964\n  End\n", kDefaultDraw));
	sim.spawn("Sprite");
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 1u);
	const DrawBatch &b = batches[0];
	CHECK(b.kind == DrawBatch::SPRITES);
	CHECK(b.texture == "Foo.tga");
	CHECK(b.shader == PARTICLE_SHADER_ALPHA);
	REQUIRE(b.sprites.size() == 1u);
	const SpriteInstance &s = b.sprites[0];
	CHECK(s.size == Approx(10.0f));
	CHECK(s.angleByte == 63);                       // (uint8)(int)(pi/2 * 40.5845) = 63
	CHECK(s.r == 1.0f);
	CHECK(s.g == Approx(51.0f / 255.0f).epsilon(1e-6)); // 0.2 * 255 = 51 (truncated)
	CHECK(s.b == 0.0f);
	CHECK(s.a == Approx(127.0f / 255.0f));          // 0.5 * 255 = 127.5 truncated to 127 (D3DCOLOR truncation, RW 0x577ee3)
	CHECK(ParticleDrawBuilder::angleByte(0.0f) == 0);
	CHECK(ParticleDrawBuilder::angleByte(6.2831855f) == 254); // 255 / (2 pi) * 2 pi = 254.99 truncates to 254
}

TEST_CASE("draw: SortLevel 1 systems are deferred and drawn last in reverse list order (RW 0x44c84a, 0x44cc8c)")
{
	const std::string common = "    Lifetime = 50 50\n    Size = 1 1\n    BurstCount = 1 1\n    BurstDelay = 100 100\n";
	const std::string mods = std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n"
		"  Update = DefaultUpdate\n  End\n";
	Sim sim(withDraw("A", common + "    SortLevel = 1\n", mods, kDefaultDraw) + withDraw("B", common, mods, kDefaultDraw) + withDraw("C", common + "    SortLevel = 1\n", mods, kDefaultDraw) +
		withDraw("D", common + "    SortLevel = 2\n", mods, kDefaultDraw));
	for (const char *n : { "A", "B", "C", "D" })
	{
		sim.spawn(n);
	}
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 4u);
	// list order A B C D; level 1 (A, C) is deferred and reversed; level 2 is never deferred (RW tests 0 < level < 2)
	CHECK(batches[0].templateName == "B");
	CHECK(batches[1].templateName == "D");
	CHECK(batches[2].templateName == "C");
	CHECK(batches[3].templateName == "A");
	CHECK(batches[2].deferred);
	CHECK(!batches[1].deferred);
}

TEST_CASE("draw: invisible particles are skipped; the stops S-194 (smudge) and S-195 (drawable) are reported")
{
	const std::string sys = "    Lifetime = 50 50\n    Size = 1 1\n    BurstCount = 1 1\n    BurstDelay = 100 100\n";
	Sim sim(withDraw("Smudge", sys + "    Type = SMUDGE\n", std::string(kPointOrtho), kDefaultDraw) +
		withDraw("Model", sys + "    Type = DRAWABLE\n    ParticleName = XXXXModel\n", std::string(kPointOrtho), "  Draw = RenderObjectDraw\n  End\n") +
		withDraw("Black", sys + "    Shader = ADDITIVE\n", std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:0 G:0 B:0 0\n  End\n", kDefaultDraw));
	sim.spawn("Smudge");
	sim.spawn("Model");
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	CHECK(batches.empty()); // the smudge and the render-object system draw nothing here
	const std::vector<std::string> u = builder.unverified();
	REQUIRE(u.size() == 2u);
	CHECK(u[0].compare(0, 5, "S-194") == 0);
	CHECK(u[1].compare(0, 5, "S-195") == 0);
}

TEST_CASE("draw: GPU particles carry the spawn record and the GpuDraw parameters (RW 0x7b1141, 0x5f61b1)")
{
	Sim sim(withDraw("Gpu", "    Type = GPU_PARTICLE\n    ParticleName = Fire.tga\n    Lifetime = 40 40\n    Size = 5 9\n    BurstCount = 2 2\n    BurstDelay = 100 100\n",
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n    Z = 3 3\n  End\n  Physics = DefaultPhysics\n    Gravity = -0.5\n  End\n"
		"  Color = DefaultColor\n    Color1 = R:255 G:0 B:0 0\n    Color2 = R:0 G:255 B:0 10\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n    Alpha2 = 0.5 0.5 10\n  End\n",
		"  Draw = GpuDraw\n    FramesPerRow = 4\n    TotalFrames = 16\n  End\n"));
	const ParticleSystemID id = sim.spawn("Gpu");
	(void)id;
	sim.step();
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 1u);
	const DrawBatch &b = batches[0];
	CHECK(b.kind == DrawBatch::GPU);
	REQUIRE(b.gpu.size() == 2u);
	CHECK(b.gpu[0].life == 40.0f);
	CHECK(b.gpu[0].vz == 3.0f);              // the spawn velocity: the GPU path never integrates on the CPU
	CHECK(b.gpu[0].z == 0.0f);               // the spawn position too
	CHECK(b.gpu[0].random >= 0.0f);
	CHECK(b.gpu[0].random <= 15.0f);
	// birth = (float)(frame * 33 * 30) * 0.001: the first burst was created in update 1 (frame 1): 33 * 30 * 0.001 = 0.99
	CHECK(b.gpu[0].birth == Approx(0.99f));
	CHECK(b.gpuParams.framesPerRow == 4.0f);
	CHECK(b.gpuParams.totalFrames == 16.0f);
	CHECK(b.gpuParams.gravity[2] == -0.5f);
	CHECK(b.gpuParams.colorKeys[0][0] == 1.0f);
	CHECK(b.gpuParams.colorKeys[1][1] == 1.0f);
	CHECK(b.gpuParams.timeKeys[1] == 10.0f);
	CHECK(b.gpuParams.colorKeys[1][3] == 0.5f);
	CHECK(b.gpuParams.size[1] == 9.0f);
	CHECK(b.gpuParams.now == Approx(2 * 0.99f)); // two client frames
	// 128 slots: more than that cannot be created at once (RW 0x5f32ff / 0x7b0f2f)
}

TEST_CASE("draw: the GPU storage has 128 slots; expired entries are dropped when expiry < now (RW 0x7b12f4)")
{
	Sim sim(withDraw("GpuMany", "    Type = GPU_PARTICLE\n    Lifetime = 3 3\n    BurstCount = 200 200\n    BurstDelay = 1000 1000\n", std::string(kPointOrtho),
		"  Draw = GpuDraw\n  End\n"));
	const ParticleSystemID id = sim.spawn("GpuMany");
	sim.step();
	CHECK(sim.particles(id).size() == 128u);
	// birth 0.99, life 3: expiry 3.99; now after step k = 0.99 k: gone when 0.99 k > 3.99, i.e. at k = 5
	sim.step(); sim.step(); sim.step();
	CHECK(sim.particles(id).size() == 128u);
	sim.step();
	CHECK(sim.particles(id).empty());
}

TEST_CASE("draw: StreakDraw builds one ribbon through the particles; the first point is transparent (RW 0x9624b9)")
{
	Sim sim(withDraw("Streak", "    Type = STREAK\n    ParticleName = Trail.tga\n    Lifetime = 50 50\n    Size = 2 2\n    BurstCount = 4 4\n    BurstDelay = 100 100\n",
		"  EmissionVolume = LineEmissionVolume\n    StartPoint = X:0 Y:0 Z:0\n    EndPoint = X:30 Y:0 Z:0\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n"
		"  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n  Update = DefaultUpdate\n  End\n",
		"  Draw = StreakDraw\n  End\n"));
	sim.spawn("Streak");
	sim.step();
	DrawCamera cam;
	cam.position = Coord3D{ 15.0f, -100.0f, 100.0f };
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, cam);
	REQUIRE(batches.size() == 1u);
	const DrawBatch &b = batches[0];
	CHECK(b.kind == DrawBatch::MESH);
	CHECK(b.ribbon);
	CHECK(b.vertices.size() == 8u);   // 4 points, two edge vertices each
	CHECK(b.indices.size() == 18u);   // 3 segments, two triangles each
	CHECK(b.vertices[0].a == 0.0f);   // rgba[0] = 0 kills the trailing edge
	CHECK(b.vertices[2].a == 1.0f);
	// the ribbon is symmetric about its points: edge vertices are one half width (size 2) away along the camera-facing side
	const MeshVertex &l = b.vertices[2], &r = b.vertices[3];
	CHECK(std::sqrt((l.x - r.x) * (l.x - r.x) + (l.y - r.y) * (l.y - r.y) + (l.z - r.z) * (l.z - r.z)) == Approx(4.0f).epsilon(1e-4));
}

TEST_CASE("draw: QuadDraw quads lie in the local XZ plane, rotated by the particle rotation type (RW 0x963481)")
{
	Sim sim(withDraw("Quad", "    ParticleName = Q.tga\n    Lifetime = 50 50\n    Size = 5 5\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n"
		"  Update = DefaultUpdate\n    Rotation = ROTATE_Z\n    AngleZ = 1.5707964 1.5707964\n  End\n", "  Draw = QuadDraw\n  End\n"));
	sim.spawn("Quad");
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 1u);
	const DrawBatch &b = batches[0];
	REQUIRE(b.vertices.size() == 4u);
	// corner 0 is (-s, 0, +s); rotated by pi/2 about Z it becomes (0, -s, +s)
	CHECK(std::fabs(b.vertices[0].x) < 1e-4f);
	CHECK(b.vertices[0].y == Approx(-5.0f));
	CHECK(b.vertices[0].z == Approx(5.0f));
	CHECK(b.vertices[3].z == Approx(-5.0f)); // corner 3 is (+s, 0, -s)
}

TEST_CASE("draw: ground-aligned and volume flags reach the batch; GPU types are not drawn by the CPU modules")
{
	Sim sim(withDraw("Ground", "    IsGroundAligned = Yes\n    Type = VOLUME_PARTICLE\n    Lifetime = 50 50\n    Size = 4 4\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Update = DefaultUpdate\n  End\n", kDefaultDraw));
	sim.spawn("Ground");
	sim.step();
	ParticleDrawBuilder builder(sim.env.rng);
	const std::vector<DrawBatch> batches = builder.build(*sim.mgr, DrawCamera());
	REQUIRE(batches.size() == 1u);
	CHECK(batches[0].groundAligned);
	CHECK(batches[0].volume);
}
