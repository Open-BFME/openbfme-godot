// OpenBFME unit tests: the particle simulator (lane FX-1). GPL-3.0.
//
// Expectations are derived by hand from the INI text and the RW rules recorded in the lane notes (fx-system-runtime.md,
// fx-modules-maths.md): emission counts and burst timing (RW 0x5f3ccf), lifetimes (RW 0x5fa0e6), alpha / colour keyframes
// (RW 0x969a94 / 0x96a0f9), physics (RW 0x96aad2), the consolidated random-draw order (notes 7), slave systems, the particle cap
// and its return-value quirk (RW 0x5f3f18), and determinism for a given client seed.

#include "FxTestUtil.h"

using namespace FXParticleSystem;
using namespace fxtest;
using doctest::Approx;

TEST_CASE("particle emission: a burst of 3 every (BurstDelay + 1) updates, first burst on the first update (RW 0x5f3ccf)")
{
	Sim sim(systemText("Burst", "    Lifetime = 100 100\n    BurstCount = 3 3\n    BurstDelay = 2 2\n", kPointOrtho));
	const ParticleSystemID id = sim.spawn("Burst");
	// update 1 emits (delay left 0); the delay then counts 2 -> 1 -> 0 over updates 2 and 3; update 4 emits again
	sim.step();
	CHECK(sim.particles(id).size() == 3u);
	sim.step();
	sim.step();
	CHECK(sim.particles(id).size() == 3u);
	sim.step();
	CHECK(sim.particles(id).size() == 6u);
	sim.step();
	sim.step();
	sim.step();
	CHECK(sim.particles(id).size() == 9u);
	CHECK(sim.mgr->particleCount() == 9u);
}

TEST_CASE("particle emission: IsOneShot fires only the first burst; InitialDelay freezes the system (RW 0x5fa319, 0x5f3ccf)")
{
	{
		Sim sim(systemText("One", "    Lifetime = 100 100\n    BurstCount = 2 2\n    BurstDelay = 3 3\n    IsOneShot = Yes\n", kPointOrtho));
		const ParticleSystemID id = sim.spawn("One");
		for (int i = 0; i < 10; ++i)
		{
			sim.step();
		}
		// the first burst sets the delay to 3; every later update takes the else branch, which pins a one-shot's delay at 1
		CHECK(sim.particles(id).size() == 2u);
		// retail quirk (RW 0x5f3ccf): the pin only happens when the delay is not 0, so a one-shot with BurstDelay 0 bursts every update
		Sim zero(systemText("Zero", "    Lifetime = 100 100\n    BurstCount = 1 1\n    BurstDelay = 0 0\n    IsOneShot = Yes\n", kPointOrtho));
		const ParticleSystemID zid = zero.spawn("Zero");
		for (int i = 0; i < 4; ++i)
		{
			zero.step();
		}
		CHECK(zero.particles(zid).size() == 4u);
	}
	{
		Sim sim(systemText("Delayed", "    Lifetime = 100 100\n    BurstCount = 1 1\n    BurstDelay = 0 0\n    InitialDelay = 5 5\n", kPointOrtho));
		const ParticleSystemID id = sim.spawn("Delayed");
		for (int i = 0; i < 5; ++i)
		{
			sim.step(); // updates 1..5 only count the delay down (the fifth sets it to 0)
		}
		CHECK(sim.particles(id).empty());
		sim.step();
		CHECK(sim.particles(id).size() == 1u);
		CHECK(sim.mgr->findParticleSystemByID(id)->startFrame() == 5u); // reset to the frame when the delay expired
	}
}

TEST_CASE("particle lifetime: a particle with lifetime 3 is updated in its birth update and dies in the third update (RW 0x5fa0e6)")
{
	Sim sim(systemText("Life", "    Lifetime = 3 3\n    BurstCount = 1 1\n    BurstDelay = 100 100\n", kPointOrtho));
	const ParticleSystemID id = sim.spawn("Life");
	sim.step(); // born, lifetimeLeft 3 -> 2
	REQUIRE(sim.particles(id).size() == 1u);
	CHECK(sim.particles(id)[0]->lifetimeLeft == 2u);
	sim.step(); // -> 1
	CHECK(sim.particles(id)[0]->lifetimeLeft == 1u);
	sim.step(); // -> 0: dies
	CHECK(sim.particles(id).empty());
	CHECK(sim.mgr->particleCount() == 0u);
}

TEST_CASE("alpha keyframes: linear to the key, snapped on reaching its frame, then constant (RW 0x969b39, 0x969a94)")
{
	Sim sim(systemText("Alpha", "    Shader = ALPHA\n    Lifetime = 20 20\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Alpha = DefaultAlpha\n    Alpha1 = 0 0 0\n    Alpha2 = 1 1 4\n  End\n"));
	const ParticleSystemID id = sim.spawn("Alpha");
	// rate = (1 - 0) / (4 - 0) = 0.25 per update
	sim.step();
	CHECK(sim.particles(id)[0]->alpha.alpha == Approx(0.25f));
	CHECK(sim.particles(id)[0]->alpha.rate == Approx(0.25f));
	sim.step();
	CHECK(sim.particles(id)[0]->alpha.alpha == Approx(0.5f));
	sim.step();
	sim.step();
	CHECK(sim.particles(id)[0]->alpha.alpha == Approx(1.0f));
	CHECK(sim.particles(id)[0]->alpha.targetKey == 1); // age (3) has not reached frame 4 yet
	sim.step(); // age 4 >= 4: the alpha snaps to the key value and the next key has frame 0, so the rate becomes 0
	CHECK(sim.particles(id)[0]->alpha.alpha == 1.0f);
	CHECK(sim.particles(id)[0]->alpha.targetKey == 2);
	CHECK(sim.particles(id)[0]->alpha.rate == 0.0f);
	sim.step();
	CHECK(sim.particles(id)[0]->alpha.alpha == 1.0f);
}

TEST_CASE("alpha: an additive system does not update alpha (RW 0x969a94 shader 1 / 2)")
{
	Sim sim(systemText("AddAlpha", "    Shader = ADDITIVE\n    Lifetime = 20 20\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Alpha = DefaultAlpha\n    Alpha1 = 0 0 0\n    Alpha2 = 1 1 4\n  End\n  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n"));
	const ParticleSystemID id = sim.spawn("AddAlpha");
	sim.step();
	sim.step();
	CHECK(sim.particles(id)[0]->alpha.alpha == 0.0f);
}

TEST_CASE("colour keyframes: the colour drifts toward the next key and is not snapped (RW 0x96a314, 0x96a0f9)")
{
	Sim sim(systemText("Color", "    Shader = ADDITIVE\n    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:0 B:0 0\n    Color2 = R:0 G:255 B:0 10\n  End\n"));
	const ParticleSystemID id = sim.spawn("Color");
	// rate = (key2 - key1) / 10 frames = (-0.1, +0.1, 0)
	const Particle *p;
	sim.step();
	p = sim.particles(id)[0];
	CHECK(p->color.rate[0] == Approx(-0.1f));
	CHECK(p->color.rate[1] == Approx(0.1f));
	CHECK(p->color.color[0] == Approx(0.9f)); // first update already applies the rate
	sim.step();
	sim.step();
	p = sim.particles(id)[0];
	CHECK(p->color.color[0] == Approx(0.7f));
	CHECK(p->color.color[1] == Approx(0.3f));
}

TEST_CASE("physics: gravity overwrites accel.z, velocity damps after integration, position adds drift + velocity (RW 0x96aad2)")
{
	Sim sim(systemText("Phys", "    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n",
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n    X = 1 1\n  End\n"
		"  Physics = DefaultPhysics\n    Gravity = -0.5\n    VelocityDamping = 0.5 0.5\n    DriftVelocity = X:0 Y:0 Z:1\n  End\n"));
	const ParticleSystemID id = sim.spawn("Phys");
	sim.step();
	const Particle *p = sim.particles(id)[0];
	// update 1: vel = (1,0,0) + (0,0,-0.5) = (1,0,-0.5); damped by 0.5: (0.5,0,-0.25); pos = (drift + vel) + 0 = (0.5, 0, 0.75)
	CHECK(p->vel.x == Approx(0.5f));
	CHECK(p->vel.z == Approx(-0.25f));
	CHECK(p->pos.x == Approx(0.5f));
	CHECK(p->pos.z == Approx(0.75f));
	sim.step();
	p = sim.particles(id)[0];
	// update 2: vel = (0.5,0,-0.75) -> (0.25,0,-0.375); pos = (0.5+0.25, 0, 0.75 + (1 - 0.375)) = (0.75, 0, 1.375)
	CHECK(p->pos.x == Approx(0.75f));
	CHECK(p->pos.z == Approx(1.375f));
}

TEST_CASE("update module: size, size rate with damping, angle (RW 0x96b2ec, 0x96af99)")
{
	Sim sim(systemText("Size", "    Lifetime = 50 50\n    Size = 10 10\n    StartSizeRate = 2 2\n    BurstCount = 2 2\n    BurstDelay = 100 100\n",
		std::string(kPointOrtho) + "  Update = DefaultUpdate\n    SizeRate = 1 1\n    SizeRateDamping = 0.5 0.5\n    AngleZ = 0.25 0.25\n    AngularRateZ = 0.1 0.1\n"
		"    AngularDamping = 1 1\n  End\n"));
	const ParticleSystemID id = sim.spawn("Size");
	sim.step();
	const std::vector<const Particle *> ps = sim.particles(id);
	REQUIRE(ps.size() == 2u);
	// StartSizeRate accumulates per particle: the first gets +2, the second +4 (RW 0x5f4c6b); size = Size * ParticleScale * 1 + bonus
	// then the first update adds sizeRate (1): 10 + 2 + 1 = 13 and 10 + 4 + 1 = 15; the rate halves
	CHECK(ps[0]->update.size[0] == Approx(13.0f));
	CHECK(ps[1]->update.size[0] == Approx(15.0f));
	CHECK(ps[0]->update.sizeRate[0] == Approx(0.5f));
	CHECK(ps[0]->update.angleZ == Approx(0.35f)); // 0.25 + 0.1
	sim.step();
	CHECK(sim.particles(id).size() == 2u);
	CHECK(sim.particles(id)[0]->update.size[0] == Approx(13.5f));
}

TEST_CASE("slave systems: created with the master, listed before it, positioned by SlavePosOffset (RW 0x5fbe28, 0x5f3ccf)")
{
	// SlavePosOffset is read from the SLAVE (RW 0x5f3ccf adds the slave's own +0x6c to the master's position)
	Sim sim(std::string(systemText("Slave", "    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n    SlavePosOffset = X:5 Y:6 Z:7\n", kPointOrtho)) +
		systemText("Master", "    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n    SlaveSystem = Slave\n", kPointOrtho));
	const ParticleSystemID master = sim.spawn("Master");
	REQUIRE(sim.mgr->systemCount() == 2u);
	const ParticleSystem *m = sim.mgr->findParticleSystemByID(master);
	REQUIRE(m->slaveID() != INVALID_PARTICLE_SYSTEM_ID);
	CHECK(m->slaveID() == master + 1); // the slave is created inside the master's constructor
	std::vector<ParticleSystemID> order;
	sim.mgr->forEachSystem([&](const ParticleSystem &s) { order.push_back(s.id()); });
	CHECK(order[0] == master + 1); // appended to the manager list before its master
	CHECK(order[1] == master);
	sim.mgr->findParticleSystemByID(master)->setPosition(Coord3D{ 100.0f, 0.0f, 0.0f });
	sim.step();
	// the master's m_pos stays (0,0,0) (setPosition writes the local transform only); the slave's pos = master m_pos + offset
	const ParticleSystem *s = sim.mgr->findParticleSystemByID(master + 1);
	CHECK(s->position().x == 5.0f);
	CHECK(s->position().y == 6.0f);
	CHECK(s->position().z == 7.0f);
	CHECK(sim.particles(master + 1).size() == 1u); // the slave emitted from the master's emit loop
	CHECK(sim.particles(master).size() == 1u);
}

TEST_CASE("system lifetime and destroy: a finite system dies once its lifetime ends and its particles are gone (RW 0x5f332e)")
{
	Sim sim(systemText("Finite", "    Lifetime = 2 2\n    SystemLifetime = 3\n    BurstCount = 1 1\n    BurstDelay = 0 0\n", kPointOrtho));
	const ParticleSystemID id = sim.spawn("Finite");
	for (int i = 0; i < 3; ++i)
	{
		sim.step(); // emits on updates 1..3 (systemLifetimeLeft 3 > 0 at the emit test)
	}
	CHECK(sim.mgr->findParticleSystemByID(id) != nullptr);
	sim.step(); // lifetime exhausted: no emission; the remaining particles age out
	sim.step();
	sim.step();
	CHECK(sim.mgr->findParticleSystemByID(id) == nullptr);
	CHECK(sim.mgr->systemCount() == 0u);
	CHECK(sim.mgr->particleCount() == 0u);

	Sim sim2(systemText("Forever", "    Lifetime = 5 5\n    BurstCount = 1 1\n    BurstDelay = 0 0\n", kPointOrtho));
	const ParticleSystemID id2 = sim2.spawn("Forever");
	sim2.step(); // the only particle: lifetime 5 -> 4
	sim2.mgr->destroyParticleSystemByID(id2);
	for (int i = 0; i < 3; ++i)
	{
		sim2.step(); // destroyed: no more emission; the particle is still alive (4 -> 1)
		CHECK(sim2.mgr->findParticleSystemByID(id2) != nullptr);
	}
	sim2.step(); // the particle dies, the destroyed system with no particles ends
	CHECK(sim2.mgr->findParticleSystemByID(id2) == nullptr);
}

TEST_CASE("particle cap: removeOldestParticles returns n + 1 when all n were removed (RW 0x5f3f18)")
{
	Sim sim(systemText("Many", "    Priority = HIGH_OR_ABOVE\n    Lifetime = 1000 1000\n    BurstCount = 10 10\n    BurstDelay = 1000 1000\n", kPointOrtho));
	sim.mgr->settings().maxParticleCount = 100;
	const ParticleSystemID id = sim.spawn("Many");
	sim.step();
	REQUIRE(sim.mgr->particleCount() == 10u);
	// priority cap 3: priorities 1 and 2 are removable; HIGH_OR_ABOVE is 2
	CHECK(sim.mgr->removeOldestParticles(4, 3) == 5);  // all 4 removed: n + 1
	CHECK(sim.mgr->particleCount() == 6u);
	CHECK(sim.mgr->removeOldestParticles(0, 3) == 1);  // count 0 returns 1
	// asking for more than exist: 6 particles, 9 requested: six iterations remove one each, the seventh decrements the count
	// (9 - 7 = 2 left) and breaks out on the empty list: the result is orig - count = 7
	const int r = sim.mgr->removeOldestParticles(9, 3);
	CHECK(r == 7);
	CHECK(sim.mgr->particleCount() == 0u);
	(void)id;
}

TEST_CASE("particle cap: a system at the cap loses roughly every second creation (the return-value quirk)")
{
	Sim sim(systemText("Cap", "    Priority = HIGH_OR_ABOVE\n    Lifetime = 1000 1000\n    BurstCount = 20 20\n    BurstDelay = 1000 1000\n", kPointOrtho));
	sim.mgr->settings().maxParticleCount = 10;
	sim.spawn("Cap");
	sim.step();
	// creation 1..10 fill the cap; from then on excess = count - 10 = 0 passes (> 0 is false): the 11th is created (count 11),
	// the 12th sees excess 1, removes one (returns 2 != 1) and fails, the 13th sees excess 0 again, ...
	CHECK(sim.mgr->particleCount() <= 11u);
	CHECK(sim.mgr->particleCount() >= 10u);
}

TEST_CASE("random draws: the consolidated emission order of RW (notes fx-modules-maths.md section 7)")
{
	Sim sim(std::string("FXParticleSystem Draws\n  System\n    Priority = ALWAYS_RENDER\n    Lifetime = 10 20\n    StartSizeRate = 1 2\n    Size = 4 8\n"
		"    BurstCount = 1 1\n    BurstDelay = 5 5\n  End\n"
		"  EmissionVolume = CylinderEmissionVolume\n    Radius = 5\n    Length = 4\n  End\n"
		"  EmissionVelocity = OrthoEmissionVelocity\n    X = 0 1\n    Z = 2 3\n  End\n"
		"  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n  End\n"
		"  Alpha = DefaultAlpha\n    Alpha1 = 0.5 0.7 0\n  End\n"
		"  Update = DefaultUpdate\n    SizeRate = 1 2\n    AngleZ = 0 6\n  End\n"
		"  Physics = DefaultPhysics\n    VelocityDamping = 0.9 1\n  End\n"
		"  Wind = DefaultWind\n    WindMotion = PingPong\n  End\n"
		"  Draw = DefaultDraw\n  End\nEnd\n"));
	const ParticleSystemID id = sim.spawn("Draws");
	const size_t creationCalls = sim.env.rng.calls.size();
	// creation: InitialDelay CONSTANT 0 0 (no call); Wind draws lower, upper, angle (3 real calls)
	REQUIRE(creationCalls == 3u);
	CHECK(sim.env.rng.calls[0].lo == 0.0f);
	CHECK(sim.env.rng.calls[0].hi == Approx(0.785398f));
	CHECK(sim.env.rng.calls[1].lo == Approx(5.49779f));
	CHECK(sim.env.rng.calls[1].hi == Approx(6.28319f));
	CHECK(sim.env.rng.calls[2].lo == sim.env.rng.calls[0].result);
	CHECK(sim.env.rng.calls[2].hi == sim.env.rng.calls[1].result);
	sim.step();
	const std::vector<RecordingRandom::Call> &c = sim.env.rng.calls;
	size_t i = creationCalls;
	auto expectReal = [&](float lo, float hi) {
		REQUIRE(i < c.size());
		INFO("call index " << i);
		CHECK(c[i].real);
		CHECK(c[i].lo == Approx(lo));
		CHECK(c[i].hi == Approx(hi));
		++i;
	};
	expectReal(1.0f, 1.0f);       // BurstCount
	expectReal(0.0f, 6.2831855f); // cylinder: angle first
	expectReal(0.0f, 5.0f);       // radius (not hollow)
	expectReal(-2.0f, 2.0f);      // z over +-Length/2
	expectReal(0.0f, 1.0f);       // ortho X
	expectReal(0.0f, 0.0f);       // ortho Y
	expectReal(2.0f, 3.0f);       // ortho Z
	expectReal(10.0f, 20.0f);     // Lifetime
	expectReal(1.0f, 2.0f);       // StartSizeRate
	expectReal(0.0f, 0.0f);       // Color: ColorScale (CONSTANT 0 0 rescaled to a UNIFORM 0 0): the per-key draws are skipped, one scale draw
	for (int k = 0; k < 8; ++k)   // Alpha: one value draw per keyframe
	{
		expectReal(k == 0 ? 0.5f : 0.0f, k == 0 ? 0.7f : 0.0f);
	}
	expectReal(4.0f, 8.0f);       // Update: Size
	expectReal(1.0f, 2.0f);       // SizeRate
	expectReal(1.0f, 1.0f);       // SizeRateDamping
	expectReal(0.0f, 6.0f);       // AngleZ
	expectReal(0.0f, 0.0f);       // AngularRateZ
	expectReal(1.0f, 1.0f);       // AngularDamping
	expectReal(0.0f, 0.0f);       // AngleXY
	expectReal(0.0f, 0.0f);       // AngularRateXY
	expectReal(1.0f, 1.0f);       // AngularDampingXY
	expectReal(0.9f, 1.0f);       // Physics VelocityDamping
	expectReal(0.7f, 1.3f);       // Wind randomness
	expectReal(5.0f, 5.0f);       // BurstDelay, after the particles
	CHECK(i == c.size());
	(void)id;
}

TEST_CASE("determinism: the same client seed gives the same particles; a different seed does not")
{
	const std::string text = systemText("Det", "    Lifetime = 30 60\n    Size = 1 9\n    BurstCount = 4 4\n    BurstDelay = 2 2\n",
		"  EmissionVolume = SphereEmissionVolume\n    Radius = 10\n  End\n  EmissionVelocity = SphericalEmissionVelocity\n    Speed = 1 3\n  End\n"
		"  Update = DefaultUpdate\n    SizeRate = 0 1\n  End\n  Physics = DefaultPhysics\n    Gravity = -0.1\n    VelocityDamping = 0.9 1\n  End\n");
	auto run = [&](std::uint32_t seed) {
		Sim sim(text, seed);
		const ParticleSystemID id = sim.spawn("Det");
		std::vector<float> out;
		for (int i = 0; i < 20; ++i)
		{
			sim.step();
		}
		for (const Particle *p : sim.particles(id))
		{
			out.push_back(p->pos.x);
			out.push_back(p->pos.y);
			out.push_back(p->pos.z);
			out.push_back(p->update.size[0]);
		}
		return out;
	};
	const std::vector<float> a = run(77), b = run(77), c = run(78);
	REQUIRE(!a.empty());
	CHECK(a == b);
	CHECK(a != c);
}

TEST_CASE("events: TerrainCollision fires the FXList when the particle reaches the ground, orients by the velocity heading (RW 0x96c01b)")
{
	Sim sim(std::string("FXParticleSystem Rain\n  System\n    Priority = ALWAYS_RENDER\n    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n  End\n"
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n    Z = -1 -1\n  End\n"
		"  Physics = DefaultPhysics\n    VelocityDamping = 1 1\n    DriftVelocity = X:0 Y:0 Z:0\n  End\n"
		"  Event = TerrainCollision\n    EventFX = FX_Splash\n    HeightOffset = 2 2\n    PerParticle = Yes\n    KillAfterEvent = Yes\n  End\n  Draw = DefaultDraw\n  End\nEnd\n"));
	sim.env.fxLists.insert("FX_Splash");
	const ParticleSystemID id = sim.spawn("Rain");
	sim.mgr->findParticleSystemByID(id)->setPosition(Coord3D{ 0.0f, 0.0f, 2.5f });
	sim.step(); // z: 2.5 -> 1.5
	CHECK(sim.env.plays.empty());
	sim.step(); // 0.5
	CHECK(sim.env.plays.empty());
	sim.step(); // -0.5 <= ground (0): fires at the particle position + HeightOffset
	REQUIRE(sim.env.plays.size() == 1u);
	CHECK(sim.env.plays[0].first == "FX_Splash");
	CHECK(sim.env.plays[0].second.z == Approx(-0.5f + 2.0f));
	CHECK(sim.particles(id).empty()); // KillAfterEvent: lifetimeLeft := 1 -> dies in the same update
}

TEST_CASE("events: an event whose FXList does not exist never fires and never kills (RW: armed && fx)")
{
	Sim sim(std::string("FXParticleSystem Rain\n  System\n    Priority = ALWAYS_RENDER\n    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n  End\n"
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n    Z = -1 -1\n  End\n"
		"  Physics = DefaultPhysics\n    VelocityDamping = 1 1\n  End\n"
		"  Event = TerrainCollision\n    EventFX = FX_Missing\n  End\n  Draw = DefaultDraw\n  End\nEnd\n"));
	const ParticleSystemID id = sim.spawn("Rain");
	for (int i = 0; i < 5; ++i)
	{
		sim.step();
	}
	CHECK(sim.env.plays.empty());
	CHECK(sim.particles(id).size() == 1u);
}

TEST_CASE("wind: a constant push along (cos a, sin a) inside the zero-strength distance; FullStrengthDist is never read (RW 0x966dff)")
{
	Sim sim(std::string("FXParticleSystem Wind\n  System\n    Priority = ALWAYS_RENDER\n    Lifetime = 50 50\n    BurstCount = 1 1\n    BurstDelay = 100 100\n  End\n"
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n"
		"  Physics = DefaultPhysics\n    VelocityDamping = 1 1\n  End\n"
		"  Wind = DefaultWind\n    WindMotion = Circular\n    WindStrength = 1\n    WindZeroStrengthDist = 100\n    WindFullStrengthDist = 1\n"
		"    WindPingPongStartAngleMin = 0\n    WindPingPongStartAngleMax = 0\n    WindPingPongEndAngleMin = 1\n    WindPingPongEndAngleMax = 1\n"
		"    WindAngleChangeMin = 0.5\n    WindAngleChangeMax = 0.5\n  End\n  Draw = DefaultDraw\n  End\nEnd\n"));
	const ParticleSystemID id = sim.spawn("Wind");
	sim.step();
	const Particle *p = sim.particles(id)[0];
	// system creation: lower 0, upper 1, angle = R(0,1) (unknown); the Circular step adds the rate 0.15 (constructor default) once per
	// update: the system update (before the particle update) moved the angle by 0.15
	const float angle = sim.mgr->findParticleSystemByID(id)->transform().Row[0][3]; // (unused: position stays at the origin)
	(void)angle;
	const float strength = 1.0f * p->wind.windRandomness; // WindStrength * R(0.7, 1.3), no falloff
	const float r = std::sqrt(p->pos.x * p->pos.x + p->pos.y * p->pos.y);
	CHECK(r == Approx(strength).epsilon(1e-4));
	CHECK(p->wind.windRandomness >= 0.7f);
	CHECK(p->wind.windRandomness <= 1.3f);
}

TEST_CASE("lifetime keyframes: Particle::isInvisible kills a faded additive particle (RW 0x5f449f, 0x96a26a: sum < 0.03)")
{
	Sim sim(systemText("Fade", "    Shader = ADDITIVE\n    Lifetime = 100 100\n    BurstCount = 1 1\n    BurstDelay = 1000 1000\n",
		std::string(kPointOrtho) + "  Color = DefaultColor\n    Color1 = R:255 G:255 B:255 0\n    Color2 = R:0 G:0 B:0 5\n  End\n"));
	const ParticleSystemID id = sim.spawn("Fade");
	// rate = -1/5 per update: the colour is 0 after update 5, but the key (frame 5) is still pending until the age reaches 5, and a pending
	// key keeps the particle visible; update 6 reaches the key (the next key has frame 0) and the black particle is invisible: it dies
	for (int i = 0; i < 5; ++i)
	{
		sim.step();
		CHECK(sim.particles(id).size() == 1u);
	}
	sim.step();
	CHECK(sim.particles(id).empty());
}

TEST_CASE("stops S-191 / S-192: behaviour RW has that the port does not reproduce is reported, not hidden")
{
	{
		Sim sim("FXParticleSystem Fire\n  System\n    Type = GPU_TERRAINFIRE\n    Priority = ALWAYS_RENDER\n    BurstCount = 1 1\n  End\n  EmissionVolume = TerrainFireEmission\n  End\n  Draw = GpuDraw\n  End\nEnd\n");
		sim.spawn("Fire");
		sim.step();
		const std::vector<std::string> u = sim.mgr->unverified();
		REQUIRE(u.size() == 1u);
		CHECK(u[0].find("S-191") == 0u);
		CHECK(sim.mgr->particleCount() == 0u);
	}
	{
		Sim sim(std::string("FXParticleSystem Bolt\n  System\n    Priority = ALWAYS_RENDER\n    BurstCount = 40 40\n    BurstDelay = 100 100\n    Lifetime = 10 10\n  End\n"
			"  EmissionVolume = LightningEmission\n    EndPoint = X:10 Y:0 Z:0\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n  Draw = LightningDraw\n  End\nEnd\n"));
		sim.spawn("Bolt");
		sim.step();
		const std::vector<std::string> u = sim.mgr->unverified();
		REQUIRE(u.size() == 1u);
		CHECK(u[0].find("S-192") == 0u);
	}
}

TEST_CASE("stop S-193: Swirly physics reports that the object influence record is not provided")
{
	Sim sim(std::string("FXParticleSystem Swirl\n  System\n    Priority = ALWAYS_RENDER\n    BurstCount = 1 1\n    BurstDelay = 100 100\n    Lifetime = 10 10\n  End\n"
		"  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n    X = 1 1\n  End\n"
		"  Physics = DefaultPhysics\n    Swirly = Yes\n    VelocityDamping = 1 1\n  End\n  Draw = DefaultDraw\n  End\nEnd\n"));
	sim.env.objectId = 5;
	const ParticleSystemID id = sim.spawn("Swirl");
	sim.mgr->findParticleSystemByID(id)->attachToObject(5);
	sim.step();
	const std::vector<std::string> u = sim.mgr->unverified();
	REQUIRE(u.size() == 1u);
	CHECK(u[0].find("S-193") == 0u);
}
