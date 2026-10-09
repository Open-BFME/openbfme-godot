// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2: the client work on the client job pool gives what the single thread gave. ParticleDrawBuilder::build builds the sprite / quad / streak
// geometry of many systems on JobSystem::client(): the batches (order, contents) and the stop notes (order) must be the same with 1 thread and with N.

#include "FxTestUtil.h"

#include "Common/JobSystem.h"
#include "GameClient/ParticleDraw.h"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace FXParticleSystem;
using namespace fxtest;

namespace
{
std::string withDraw(const std::string &name, const std::string &system, const std::string &modules, const std::string &draw)
{
	return "FXParticleSystem " + name + "\n  System\n    Priority = ALWAYS_RENDER\n" + system + "  End\n" + modules + draw + "End\n";
}

// every field of every batch, in order, as one string (floats by their bits)
std::string describe(const std::vector<DrawBatch> &batches)
{
	std::string out;
	auto bits = [&out](float f) {
		std::uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		out += std::to_string(u) + ",";
	};
	for (const DrawBatch &b : batches)
	{
		out += b.templateName + "|" + std::to_string((int)b.kind) + "|" + b.texture + "|" + std::to_string(b.shader) + "|" + std::to_string(b.sortLevel) +
			(b.deferred ? "D" : "-") + (b.ribbon ? "R" : "-") + "|";
		for (const SpriteInstance &s : b.sprites)
		{
			bits(s.x), bits(s.y), bits(s.z), bits(s.size), bits(s.r), bits(s.g), bits(s.b), bits(s.a);
			out += std::to_string(s.angleByte) + ";";
		}
		for (const MeshVertex &v : b.vertices)
		{
			bits(v.x), bits(v.y), bits(v.z), bits(v.a);
		}
		for (std::uint32_t i : b.indices)
		{
			out += std::to_string(i) + ",";
		}
		out += "\n";
	}
	return out;
}
} // namespace

TEST_CASE("perf2 client: the particle batches of 60 systems (sprites, quads, streaks, deferred, skipped kinds) and the stop notes are the same with 1 and N "
		  "client threads")
{
	const std::string sys = "    ParticleName = P.tga\n    Lifetime = 50 50\n    Size = 2 3\n    BurstCount = 6 6\n    BurstDelay = 100 100\n";
	const std::string mods = std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:128 B:0 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n"
		"  Update = DefaultUpdate\n    AngleZ = 0.5 1.5\n  End\n";
	const std::string quadMods = std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n    Alpha1 = 1 1 0\n  End\n"
		"  Update = DefaultUpdate\n    Rotation = ROTATE_Z\n    AngleZ = 0.5 1.5\n  End\n";
	const std::string streakMods = "  EmissionVolume = LineEmissionVolume\n    StartPoint = X:0 Y:0 Z:0\n    EndPoint = X:30 Y:0 Z:0\n  End\n"
		"  EmissionVelocity = OrthoEmissionVelocity\n  End\n  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n  Alpha = DefaultAlpha\n"
		"    Alpha1 = 1 1 0\n  End\n  Update = DefaultUpdate\n  End\n";
	std::string ini;
	std::vector<std::string> names;
	for (int i = 0; i < 60; ++i)
	{
		const std::string n = "Sys" + std::to_string(i);
		names.push_back(n);
		switch (i % 6)
		{
		case 0: ini += withDraw(n, sys, mods, "  Draw = DefaultDraw\n  End\n"); break;
		case 1: ini += withDraw(n, sys + "    SortLevel = 1\n", mods, "  Draw = DefaultDraw\n  End\n"); break;
		case 2: ini += withDraw(n, sys, quadMods, "  Draw = QuadDraw\n  End\n"); break;
		case 3: ini += withDraw(n, sys + "    Type = STREAK\n", streakMods, "  Draw = StreakDraw\n  End\n"); break;
		case 4: ini += withDraw(n, sys + "    Type = SMUDGE\n", std::string(kPointOrtho), "  Draw = DefaultDraw\n  End\n"); break;
		default: ini += withDraw(n, sys + "    Type = DRAWABLE\n", std::string(kPointOrtho), "  Draw = RenderObjectDraw\n  End\n"); break;
		}
	}
	Sim sim(ini);
	for (const std::string &n : names)
	{
		sim.spawn(n.c_str());
	}
	sim.step();
	sim.step();
	DrawCamera cam;
	cam.position = Coord3D{ 15.0f, -100.0f, 100.0f };
	const int before = JobSystem::client().threadCount();
	std::string want;
	std::vector<std::string> wantNotes;
	std::vector<int> counts = { 1, 2, 4 };
	if ((int)std::thread::hardware_concurrency() > 4)
	{
		counts.push_back((int)std::thread::hardware_concurrency());
	}
	for (int threads : counts)
	{
		JobSystem::client().setThreadCount(threads);
		for (int run = 0; run < 5; ++run)
		{
			ParticleDrawBuilder builder(sim.env.rng);
			const std::string got = describe(builder.build(*sim.mgr, cam));
			INFO(threads << " threads, run " << run);
			if (want.empty())
			{
				want = got;
				wantNotes = builder.unverified();
				CHECK(want.find("Sys1|") != std::string::npos);
			}
			CHECK(got == want);
			CHECK(builder.unverified() == wantNotes);
		}
	}
	// each stop noted once, in the list order of the first system that hit it: Sys1 (sprites: S-1440, lane FX-3), Sys3 (streak: S-199), Sys4 (smudge: S-194),
	// Sys5 (render object: S-195)
	REQUIRE(wantNotes.size() == 4u);
	CHECK(wantNotes[0].compare(0, 6, "S-1440") == 0);
	CHECK(wantNotes[1].compare(0, 5, "S-199") == 0);
	CHECK(wantNotes[2].compare(0, 5, "S-194") == 0);
	CHECK(wantNotes[3].compare(0, 5, "S-195") == 0);
	JobSystem::client().setThreadCount(before);
}
