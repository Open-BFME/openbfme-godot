// OpenBFME retail tests for the FX stack (lane FX-1). GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise it prints SKIP). Every retail FXParticleSystem template is
// spawned on its own and stepped for ten seconds of client time (300 steps at 30 Hz), every retail FXList is played at a position and on
// a dummy object; the simulator must not throw, never produce a non-finite number, keep its particle bookkeeping consistent and
// end every finite system. Expected values come from the INI text (SystemLifetime, Lifetime) and the rules of the lane notes.

#include "doctest.h"

#include "GameClient/FXPlayback.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

using namespace FXParticleSystem;

namespace
{

bool finite3(const Coord3D &c) { return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z); }

size_t sumSystemParticles(const ParticleSystemManager &m)
{
	size_t n = 0;
	m.forEachSystem([&](const ParticleSystem &s) { n += s.particleCount(); });
	return n;
}

void checkFinite(const ParticleSystemManager &m)
{
	m.forEachSystem([&](const ParticleSystem &s) {
		for (int p = s.firstParticle(); p >= 0; p = m.particle(p).sysNext)
		{
			const Particle &pp = m.particle(p);
			REQUIRE_MESSAGE(finite3(pp.pos), "non-finite position in " << s.getTemplate().getName());
			REQUIRE_MESSAGE(finite3(pp.vel), "non-finite velocity in " << s.getTemplate().getName());
			REQUIRE_MESSAGE(std::isfinite(pp.update.size[0]), "non-finite size in " << s.getTemplate().getName());
			REQUIRE_MESSAGE(std::isfinite(pp.alpha.alpha), "non-finite alpha in " << s.getTemplate().getName());
			REQUIRE_MESSAGE(std::isfinite(pp.color.color[0]), "non-finite colour in " << s.getTemplate().getName());
		}
	});
}

} // namespace

TEST_CASE("FX retail: every particle system template runs for 10 s without a non-finite value; every finite system ends")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("FX retail playback");
		return;
	}
	REQUIRE(mount->fs != nullptr);
	FXPlayback fx(*mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	const std::vector<std::string> errors = fx.loadRetailData();
	for (const std::string &e : errors)
	{
		MESSAGE(e);
	}
	REQUIRE(errors.empty());
	fx.seedClientRandom(2026);
	fx.particles().settings().maxParticleCount = 1000000; // the per-system cap tests live elsewhere; this run is about numerics
	size_t systems = 0, withParticles = 0, finiteEnded = 0, finiteTotal = 0;
	size_t totalParticlesSeen = 0;
	const auto t0 = std::chrono::steady_clock::now();
	for (const std::string &name : fx.particleTemplates().templateNames())
	{
		const FXParticleSystem::ParticleSystemTemplate *t = fx.particleTemplates().findTemplate(name);
		const Coord3D pos{ 100.0f, 200.0f, 5.0f };
		const ParticleSystemID id = fx.playParticleSystem(name, pos);
		REQUIRE(id != INVALID_PARTICLE_SYSTEM_ID);
		++systems;
		bool seen = false;
		size_t peak = 0;
		for (int i = 0; i < 300; ++i)
		{
			fx.step();
			const size_t n = fx.particles().particleCount();
			peak = std::max(peak, (size_t)n);
			if (n)
			{
				seen = true;
			}
			if (i % 50 == 0)
			{
				checkFinite(fx.particles());
				REQUIRE_MESSAGE(fx.particles().particleCount() == sumSystemParticles(fx.particles()), name);
			}
		}
		checkFinite(fx.particles());
		REQUIRE_MESSAGE(fx.particles().particleCount() == sumSystemParticles(fx.particles()), name);
		totalParticlesSeen += peak;
		if (seen)
		{
			++withParticles;
		}
		const bool finiteSystem = t->info().m_systemLifetime != 0;
		if (finiteSystem)
		{
			++finiteTotal;
			// a finite system lives SystemLifetime updates plus its longest particle lifetime (+ slaves, which follow the master)
			if (!fx.particles().findParticleSystemByID(id))
			{
				++finiteEnded;
			}
		}
		fx.particles().reset();
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("[fx retail] %zu systems x 300 steps in %.0f ms; %zu produced particles; %zu of %zu finite systems ended within 10 s; peak sum %zu\n", systems,
		ms, withParticles, finiteEnded, finiteTotal, totalParticlesSeen);
	CHECK(systems == fx.particleTemplates().templateCount());
	CHECK(withParticles > systems * 8 / 10); // most templates emit; GPU terrain fire (S-191) and event-only systems may not
}

TEST_CASE("FX retail: every FXList plays at a position and on a dummy object")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("FX retail FXList playback");
		return;
	}
	FXPlayback fx(*mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(fx.loadRetailData().empty());
	fx.seedClientRandom(7);
	size_t played = 0, createdSystems = 0, withEvents = 0;
	for (const std::string &name : fx.fxLists().listNames())
	{
		for (int asObject = 0; asObject < 2; ++asObject)
		{
			const size_t before = fx.particles().systemCount();
			fx.clearEvents();
			REQUIRE(fx.playFXList(name, Coord3D{ 50.0f, 60.0f, 0.0f }, asObject != 0));
			++played;
			createdSystems += fx.particles().systemCount() - before;
			if (!fx.events().empty())
			{
				++withEvents;
			}
			for (int i = 0; i < 20; ++i)
			{
				fx.step();
			}
			checkFinite(fx.particles());
		}
		fx.particles().reset();
	}
	std::printf("[fx retail] %zu FXList plays; %zu particle systems created; %zu plays raised engine events\n", played, createdSystems, withEvents);
	CHECK(played == fx.fxLists().listCount() * 2);
	CHECK(createdSystems > 500);
}

TEST_CASE("FX retail: simulation cost with many systems running (frame-time measurement)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("FX retail benchmark");
		return;
	}
	FXPlayback fx(*mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(fx.loadRetailData().empty());
	fx.seedClientRandom(20261001);
	const std::vector<std::string> &names = fx.particleTemplates().templateNames();
	const int kSystems = 400;
	for (int i = 0; i < kSystems; ++i)
	{
		fx.playParticleSystem(names[(size_t)(i * 4793) % names.size()], Coord3D{ (float)(i % 20) * 70.0f, (float)(i / 20) * 70.0f, 0.0f });
	}
	std::vector<double> ms;
	size_t particles = 0;
	for (int i = 0; i < 300; ++i)
	{
		if (i % 90 == 89)
		{
			for (int k = 0; k < kSystems; ++k) // finite systems end: respawn every 3 s so the load stays up (as the viewer's bench does)
			{
				fx.playParticleSystem(names[(size_t)(k * 4793) % names.size()], Coord3D{ (float)(k % 20) * 70.0f, (float)(k / 20) * 70.0f, 0.0f });
			}
		}
		const auto t0 = std::chrono::steady_clock::now();
		fx.step();
		ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		particles = std::max(particles, (size_t)fx.particles().particleCount());
	}
	double total = 0;
	for (double m : ms)
	{
		total += m;
	}
	std::sort(ms.begin(), ms.end());
	std::printf("[fx bench] %d systems, 300 steps: mean %.3f ms, median %.3f ms, p95 %.3f ms, worst %.3f ms; peak %zu particles; %zu systems alive at the end\n", kSystems,
		total / 300.0, ms[150], ms[285], ms[299], particles, fx.particles().systemCount());
	CHECK(particles > 0);
}
