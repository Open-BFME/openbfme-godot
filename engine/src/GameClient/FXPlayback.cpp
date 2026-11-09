// OpenBFME. GPL-3.0. See FXPlayback.h.

#include "GameClient/FXPlayback.h"

#include "Common/Audio/AudioEntryPoints.h"

#include <algorithm>
#include <cstdio>

class FXPlayback::Environment : public FXParticleSystem::ParticleEnvironment
{
public:
	explicit Environment(FXPlayback &owner) : m_owner(owner) {}
	W3DDrawRandom &clientRandom() override { return m_owner.m_random; }
	std::uint32_t clientFrame() const override { return m_owner.m_frame; }
	float groundHeight(float x, float y) override { return m_owner.m_world ? m_owner.m_world->groundHeight(x, y) : m_owner.m_groundZ; }
	int shroudStatusAt(const Coord3D &p) override { return m_owner.m_world ? m_owner.m_world->shroudStatusAt(p) : 0; }
	FXParticleSystem::ParticleAttachInfo attachedDrawable(std::uint32_t id, const std::string &bone) override
	{
		return m_owner.m_world ? m_owner.m_world->attachedDrawable(id, bone) : FXParticleSystem::ParticleAttachInfo();
	}
	FXParticleSystem::ParticleAttachInfo attachedObject(std::uint32_t id, const std::string &bone, int player) override
	{
		return m_owner.m_world ? m_owner.m_world->attachedObject(id, bone, player) : FXParticleSystem::ParticleAttachInfo();
	}
	void playFXList(const std::string &name, const Coord3D &pos, const Matrix3D *mtx) override;
	bool hasFXList(const std::string &name) override { return m_owner.m_lists.findFXList(name) != nullptr; }

private:
	FXPlayback &m_owner;
};

class FXPlayback::Services : public FXServices
{
public:
	explicit Services(FXPlayback &owner) : m_owner(owner) {}

	W3DDrawRandom &clientRandom() override { return m_owner.m_random; }
	// without a world: 30 client frames per second, 5 logic frames per second
	std::uint32_t logicFrame() const override { return m_owner.m_world ? m_owner.m_world->logicFrame() : m_owner.m_frame / 6; }
	int shroudStatusAt(const Coord3D &p) override { return m_owner.m_world ? m_owner.m_world->shroudStatusAt(p) : FX_SHROUD_CLEAR; }
	bool objectShroudedForLocalPlayer(const FXObject &o) override { return m_owner.m_world && m_owner.m_world->objectShroudedForLocalPlayer(o); }
	void playSound(const std::string &name, const Coord3D *pos, int player) override
	{
		if (!m_owner.m_world || !m_owner.m_world->playSound(name, pos, player))
		{
			note("sound", name, pos);
		}
	}
	void playEva(const std::string &name) override
	{
		// lane FEAT-BLDFX-1: FXList EvaEvent -> TheEva (RW 0x5DD9EE reportEva by name, AUDIO-2's installed Eva); without audio the call is noted
		if (!AudioApi::reportEva(name, nullptr))
		{
			note("eva", name, nullptr);
		}
	}
	void viewShake(const Coord3D &pos, int type) override
	{
		// lane FX-4, RW 0x486D86: W3DView::shake draws GameClientRandomValueReal(0, 2 pi = 6.2831855f, RW 0xBDD38C) first, whatever it does next
		const float angle = m_owner.m_random.real(0.0f, 6.2831855f);
		if (!m_owner.m_world || !m_owner.m_world->viewShake(pos, type, angle))
		{
			note("viewShake", std::to_string(type), &pos);
		}
	}
	void cameraShaker(const Coord3D &pos, float radius, float seconds, float amplitude) override
	{
		// lane FEAT-BLDFX-1: RW 0x5DFE90 / 0x5DFECE -> TheTacticalView vslot 0xA8 (Add_Camera_Shake, DONOR BFME2 0x65DAD), taken by the view (TacticalCamera) with the client frame
		CameraShakerFx s;
		s.position = pos;
		s.radius = radius;
		s.durationSeconds = seconds;
		s.amplitudeRadians = amplitude * 3.14159265358979323846f / 180.0f; // TARGET RW 0x5DFE90: the nugget's degrees -> radians
		if (m_owner.m_pendingShakers.size() >= CameraShakerQueue::kMaxShakers)
		{
			m_owner.m_pendingShakers.erase(m_owner.m_pendingShakers.begin());
		}
		m_owner.m_pendingShakers.push_back(s);
	}
	void lightPulse(const Coord3D &pos, const RGBColor &color, float radius, std::uint32_t increaseFrames, std::uint32_t decreaseFrames) override
	{
		// lane FEAT-BLDFX-1: RW 0x5DFAA3 -> display vslot 0xBC (createLightPulse, DONOR BFME2 0x4423C) into the dynamic light pool
		m_owner.m_lightPulses.add(pos, color, radius, increaseFrames, decreaseFrames);
	}
	void addScorch(const Coord3D &pos, float radius, int type) override
	{
		// lane FEAT-BLDFX-1: RW 0x5E0045 -> TheGameClient vslot 0x58 (addScorch, DONOR BFME2 0x67EA3)
		m_owner.m_scorches.add(pos, radius, type);
	}
	float groundHeight(float x, float y, const Coord3D &) override { return m_owner.m_world ? m_owner.m_world->groundHeight(x, y) : m_owner.m_groundZ; }
	bool isWater(float x, float y) override { return m_owner.m_world && m_owner.m_world->isWater(x, y); }
	bool particleSystemTemplateExists(const std::string &name) override { return m_owner.m_templates.findTemplate(name) != nullptr; }
	std::unique_ptr<FXParticleSystemRef> createParticleSystem(const std::string &name) override
	{
		const FXParticleSystem::ParticleSystemTemplate *t = m_owner.m_templates.findTemplate(name);
		const FXParticleSystem::ParticleSystemID id = m_owner.m_manager->createParticleSystem(t, true);
		if (id == FXParticleSystem::INVALID_PARTICLE_SYSTEM_ID)
		{
			return nullptr;
		}
		return std::make_unique<FXParticleSystem::ParticleSystemHandle>(*m_owner.m_manager, id);
	}
	bool boneWorldMatrix(const FXObject &o, const std::string &bone, Matrix3D &out) override { return m_owner.m_world && m_owner.m_world->boneWorldMatrix(o, bone, out); }
	std::vector<FXBoneTransform> boneWorldTransforms(const FXObject &o, const std::string &bone, int start, int max) override
	{
		return m_owner.m_world ? m_owner.m_world->boneWorldTransforms(o, bone, start, max) : std::vector<FXBoneTransform>();
	}
	void tintDrawable(const FXObject &, const RGBColor &, unsigned, unsigned, unsigned, float, float) override { note("tint", "", nullptr); }
	void attachModel(const FXObject &, const std::string &model, bool, int) override { note("attachModel", model, nullptr); }
	void addBuff(const FXObject &o, int type, const std::string *tmpl, int life, const RGBColor &, float) override
	{
		// lane SPELL-4: a non-complex buff (a template) goes to the world; the complex (glow outline) kind is unsupported in retail too (MakeGlowOutlineBuff asserts)
		if (!tmpl || !m_owner.m_world || !m_owner.m_world->turnOnBuff(o, type, *tmpl, life))
		{
			note("addBuff", std::to_string(type) + (tmpl ? " " + *tmpl : std::string()), nullptr);
		}
	}
	void removeBuff(const FXObject &o, int type) override
	{
		if (!m_owner.m_world || !m_owner.m_world->turnOffBuff(o, type))
		{
			note("removeBuff", std::to_string(type), nullptr);
		}
	}
	void createDecal(const FXDecalDesc &d) override { m_owner.addDecal(d); }
	void createRayEffect(const Coord3D &a, const Coord3D &, const std::string &t) override { note("rayEffect", t, &a); }
	void cursorParticles(const std::string &name, unsigned, const GameClientRandomVariable &, const GameClientRandomVariable &, const GameClientRandomVariable &,
		const GameClientRandomVariable &) override { note("cursorParticles", name, nullptr); }
	void laser(const std::string &name, const Coord3D *a, const Coord3D *, bool, const FXObject *, const FXObject *) override { note("laser", name, a); }
	// lane WEATHER-1: the live world's map weather; the flat test world of FX-1 is NORMAL
	int weather() const override { return m_owner.m_world ? m_owner.m_world->mapWeather() : 0; }

private:
	void note(const char *kind, const std::string &detail, const Coord3D *pos)
	{
		Event e;
		e.kind = kind;
		e.detail = detail;
		if (pos)
		{
			e.pos = *pos;
		}
		e.frame = m_owner.m_frame;
		m_owner.m_events.push_back(std::move(e));
	}
	FXPlayback &m_owner;
};

void FXPlayback::Environment::playFXList(const std::string &name, const Coord3D &pos, const Matrix3D *mtx)
{
	if (const FXList *fx = m_owner.m_lists.findFXList(name))
	{
		FXList::doFXPos(fx, *m_owner.m_services, m_owner.m_lists, &pos, mtx, 0.0f, nullptr);
	}
}

FXPlayback::FXPlayback(ArchiveFileSystem &fs, RandomAlgorithm random) : m_fs(fs), m_random(random)
{
	m_iniEnv.fileSystem = &fs;
	m_env = std::make_unique<Environment>(*this);
	m_services = std::make_unique<Services>(*this);
	m_manager = std::make_unique<FXParticleSystem::ParticleSystemManager>(m_templates, *m_env);
	m_iniEnv.blocks.registerBlock("FXParticleSystem", [this](INI *ini) {
		FXParticleSystem::FXParticleSystemTemplateStore *saved = FXParticleSystem::TheFXParticleSystemTemplateStore;
		FXParticleSystem::TheFXParticleSystemTemplateStore = &m_templates;
		try
		{
			FXParticleSystem::ParseFXParticleSystemDefinitionGlobal(ini);
		}
		catch (...)
		{
			FXParticleSystem::TheFXParticleSystemTemplateStore = saved;
			throw;
		}
		FXParticleSystem::TheFXParticleSystemTemplateStore = saved;
	});
	m_iniEnv.blocks.registerBlock("FXList", [this](INI *ini) {
		FXListStore *saved = TheFXListStore;
		TheFXListStore = &m_lists;
		try
		{
			ParseFXListDefinitionGlobal(ini);
		}
		catch (...)
		{
			TheFXListStore = saved;
			throw;
		}
		TheFXListStore = saved;
	});
}

FXPlayback::~FXPlayback() = default;

FXServices &FXPlayback::services()
{
	return *m_services;
}

std::vector<std::string> FXPlayback::loadRetailData()
{
	std::vector<std::string> errors;
	auto run = [&](const char *what, auto &&fn) {
		try
		{
			fn();
		}
		catch (const INIException &e)
		{
			errors.push_back(std::string(what) + ": " + e.message());
		}
		catch (const std::exception &e)
		{
			errors.push_back(std::string(what) + ": " + e.what());
		}
	};
	// retail defines its #define macros in gamedata.ini before any other content file loads (spec ini-and-object-model 3.4)
	run("GameData.ini macros", [&] {
		INI ini(m_iniEnv);
		ini.preprocessFile("Data\\INI\\GameData.ini", INI_LOAD_OVERWRITE);
	});
	run("FXParticleSystem.ini", [&] {
		INI ini(m_iniEnv);
		ini.load("Data\\INI\\FXParticleSystem.ini", INI_LOAD_OVERWRITE);
	});
	run("Default\\FXList.ini", [&] {
		INI ini(m_iniEnv);
		ini.load("Data\\INI\\Default\\FXList.ini", INI_LOAD_OVERWRITE);
	});
	run("FXList.ini", [&] {
		INI ini(m_iniEnv);
		ini.load("Data\\INI\\FXList.ini", INI_LOAD_OVERWRITE);
	});
	return errors;
}

std::vector<std::string> FXPlayback::loadIniText(const std::string &name, const std::string &text)
{
	std::vector<std::string> errors;
	try
	{
		INI ini(m_iniEnv);
		ini.loadMemory(name, std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
	}
	catch (const INIException &e)
	{
		errors.push_back(name + ": " + e.message());
	}
	return errors;
}

FXParticleSystem::ParticleSystemID FXPlayback::playParticleSystem(const std::string &name, const Coord3D &pos)
{
	const FXParticleSystem::ParticleSystemTemplate *t = m_templates.findTemplate(name);
	const FXParticleSystem::ParticleSystemID id = m_manager->createParticleSystem(t, true);
	if (FXParticleSystem::ParticleSystem *s = m_manager->findParticleSystemByID(id))
	{
		s->setPosition(pos);
	}
	return id;
}

bool FXPlayback::playFXList(const std::string &name, const Coord3D &pos, bool asObject)
{
	const FXList *fx = m_lists.findFXList(name);
	if (!fx)
	{
		return false;
	}
	if (asObject)
	{
		FXDummyObject obj;
		obj.pos = pos;
		obj.mtx.Set_Translation(Vector3(pos.x, pos.y, pos.z));
		FXList::doFXObj(fx, *m_services, m_lists, &obj, nullptr);
	}
	else
	{
		Matrix3D m;
		m.Set_Translation(Vector3(pos.x, pos.y, pos.z));
		FXList::doFXPos(fx, *m_services, m_lists, &pos, &m, 0.0f, nullptr);
	}
	return true;
}

void FXPlayback::step()
{
	++m_frame;
	// lane FEAT-BLDFX-1: the dynamic lights' On_Frame_Update (DONOR BFME2 0x6DE3D) and the expired decals (DONOR BFME2 0x10C785's release loop)
	m_lightPulses.step();
	m_decals.erase(std::remove_if(m_decals.begin(), m_decals.end(), [this](const DecalFx &d) { return d.expired(m_frame); }), m_decals.end());
	m_manager->update(true, 0);
	m_emitterWorld.sync(W3DEmitterWorld::kFrameLengthMs * m_frame); // WW3D::Sync (RW 0x516e20): 33 ms per client frame
	m_emitterWorld.update();
}

std::uint32_t FXPlayback::addDecal(const FXDecalDesc &desc)
{
	// lane FEAT-BLDFX-1: DynamicDecal's projected decal (RW 0x5E05AB -> TheProjectedShadowManager::addDecal, DONOR BFME2 0x1E0E43); the opacity envelope's
	// frames are relative to the client frame it starts on (DONOR BFME2 0x330A37)
	DecalFx d;
	d.desc = desc;
	d.startFrame = m_frame;
	m_decals.push_back(d);
	return m_frame;
}

void FXPlayback::clearClientEffects()
{
	m_scorches.clear();
	m_decals.clear();
	m_lightPulses.clear();
	m_pendingShakers.clear();
}

ParticleEmitterInstance *FXPlayback::playW3DEmitter(const std::string &stem, const Coord3D &pos, std::string *error)
{
	auto fail = [&](const std::string &text) -> ParticleEmitterInstance * {
		if (error)
		{
			*error = text;
		}
		return nullptr;
	};
	std::shared_ptr<W3DFileContents> &file = m_emitterFiles[stem];
	if (!file)
	{
		ArchiveW3DFileSource source(m_fs);
		std::vector<std::uint8_t> bytes;
		std::string err;
		if (!source.Read(W3D_Asset_Path(stem, source.FlatAssetLayout()), bytes, &err))
		{
			m_emitterFiles.erase(stem);
			return fail(err);
		}
		auto contents = std::make_shared<W3DFileContents>();
		if (!Load_W3D_File(bytes.data(), bytes.size(), *contents, &err))
		{
			m_emitterFiles.erase(stem);
			return fail(W3D_Asset_Path(stem, source.FlatAssetLayout()) + ": " + err);
		}
		if (contents->Emitters.size() != 1)
		{
			m_emitterFiles.erase(stem);
			return fail(W3D_Asset_Path(stem, source.FlatAssetLayout()) + " holds " + std::to_string(contents->Emitters.size()) + " emitters, expected 1");
		}
		file = contents;
	}
	Matrix3D xf;
	xf.Set_Translation(Vector3(pos.x, pos.y, pos.z));
	try
	{
		return m_emitterWorld.create(file->Emitters[0], xf);
	}
	catch (const std::exception &e)
	{
		return fail(e.what());
	}
}

std::vector<std::string> FXPlayback::unverified() const
{
	std::vector<std::string> out = m_random.unverified();
	for (const std::string &s : m_lists.unverified())
	{
		out.push_back(s);
	}
	for (const std::string &s : m_manager->unverified())
	{
		out.push_back(s);
	}
	return out;
}

// lane VISUAL-2 (scripts, proof captures): the live systems whose position is within `radius` of (x, y), and their particles
void FXPlayback::particlesNear(float x, float y, float radius, long long &systems, long long &particles) const
{
	systems = particles = 0;
	m_manager->forEachSystem([&](const FXParticleSystem::ParticleSystem &ps) {
		const float dx = ps.position().x - x, dy = ps.position().y - y;
		if (!ps.isDestroyed() && dx * dx + dy * dy <= radius * radius)
		{
			++systems;
			particles += (long long)ps.particleCount();
		}
	});
}
