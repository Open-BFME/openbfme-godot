// OpenBFME unit tests: shared fixtures for the FX tests (lane FX-1). GPL-3.0.
// A recording client stream, a test environment (flat ground at z = 0, FXLists by name) and a Sim that loads FXParticleSystem INI text.

#pragma once

#include "doctest.h"

#include "GameClient/ParticleSys.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "IniTestUtil.h"

#include <set>

namespace fxtest
{

using namespace FXParticleSystem;

// A W3DDrawRandom that logs every call (including degenerate ones that draw nothing) and forwards to a real client stream.
class RecordingRandom : public W3DDrawRandom
{
public:
	struct Call
	{
		bool real;
		float lo, hi;
		float result;
	};
	explicit RecordingRandom(std::uint32_t seed) : m_inner(RandomAlgorithm::RotWK_GameDat_LCG) { m_inner.seed(seed); }
	int value(int lo, int hi) override
	{
		const int r = m_inner.value(lo, hi);
		calls.push_back(Call{ false, (float)lo, (float)hi, (float)r });
		return r;
	}
	float real(float lo, float hi) override
	{
		const float r = m_inner.real(lo, hi);
		calls.push_back(Call{ true, lo, hi, r });
		return r;
	}
	std::vector<Call> calls;

private:
	W3DClientRandom m_inner;
};

class TestEnv : public ParticleEnvironment
{
public:
	explicit TestEnv(std::uint32_t seed = 1234) : rng(seed) {}
	W3DDrawRandom &clientRandom() override { return rng; }
	std::uint32_t clientFrame() const override { return frame; }
	float groundHeight(float, float) override { return ground; }
	int shroudStatusAt(const Coord3D &) override { return 0; }
	ParticleAttachInfo attachedDrawable(std::uint32_t, const std::string &) override { return ParticleAttachInfo(); }
	ParticleAttachInfo attachedObject(std::uint32_t id, const std::string &, int) override
	{
		ParticleAttachInfo i;
		if (id == objectId)
		{
			i.found = true;
			i.position = Coord3D{ 1.0f, 2.0f, 3.0f };
		}
		return i;
	}
	void playFXList(const std::string &name, const Coord3D &pos, const Matrix3D *) override { plays.push_back({ name, pos }); }
	bool hasFXList(const std::string &name) override { return fxLists.count(name) != 0; }

	RecordingRandom rng;
	std::uint32_t frame = 0;
	std::uint32_t objectId = 0;
	float ground = 0.0f;
	std::set<std::string> fxLists;
	std::vector<std::pair<std::string, Coord3D>> plays;
};

struct Sim
{
	initest::Fixture fixture;
	FXParticleSystemTemplateStore store;
	TestEnv env;
	std::unique_ptr<ParticleSystemManager> mgr;

	explicit Sim(const std::string &ini, std::uint32_t seed = 1234) : env(seed)
	{
		fixture.env.blocks.registerBlock("FXParticleSystem", [this](INI *i) {
			FXParticleSystemTemplateStore *saved = TheFXParticleSystemTemplateStore;
			TheFXParticleSystemTemplateStore = &store;
			ParseFXParticleSystemDefinitionGlobal(i);
			TheFXParticleSystemTemplateStore = saved;
		});
		const std::string err = initest::loadError(fixture.env, "p.ini", ini);
		REQUIRE_MESSAGE(err.empty(), err);
		mgr = std::make_unique<ParticleSystemManager>(store, env);
	}
	ParticleSystemID spawn(const char *name)
	{
		const ParticleSystemTemplate *t = store.findTemplate(name);
		REQUIRE(t != nullptr);
		return mgr->createParticleSystem(t, true);
	}
	// one client step: the frame advances, then the manager updates (the engine loop, RW 0x632431 / 0x449d48)
	void step()
	{
		++env.frame;
		mgr->update();
	}
	std::vector<const Particle *> particles(ParticleSystemID id)
	{
		std::vector<const Particle *> out;
		const ParticleSystem *s = mgr->findParticleSystemByID(id);
		if (s)
		{
			for (int p = s->firstParticle(); p >= 0; p = mgr->particle(p).sysNext)
			{
				out.push_back(&mgr->particle(p));
			}
		}
		return out;
	}
};

// a minimal always-visible system: point volume, ortho velocity, DefaultDraw; every number in `system` is deterministic
inline std::string systemText(const std::string &name, const std::string &systemBody, const std::string &modules)
{
	return "FXParticleSystem " + name + "\n  System\n    Priority = ALWAYS_RENDER\n" + systemBody + "  End\n" + modules +
		"  Draw = DefaultDraw\n  End\nEnd\n";
}

inline const char *kPointOrtho = "  EmissionVolume = PointEmissionVolume\n  End\n  EmissionVelocity = OrthoEmissionVelocity\n  End\n";

} // namespace fxtest
