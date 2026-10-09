// OpenBFME. GPL-3.0. See FXPlayback.h.

#include "GameClient/FXPlayback.h"

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
	void playEva(const std::string &name) override { note("eva", name, nullptr); }
	void viewShake(const Coord3D &pos, int type) override { note("viewShake", std::to_string(type), &pos); }
	void cameraShaker(const Coord3D &pos, float radius, float seconds, float amplitude) override
	{
		note("cameraShaker", "r=" + std::to_string(radius) + " s=" + std::to_string(seconds) + " a=" + std::to_string(amplitude), &pos);
	}
	void lightPulse(const Coord3D &pos, const RGBColor &, float radius, std::uint32_t, std::uint32_t) override { note("lightPulse", "r=" + std::to_string(radius), &pos); }
	void addScorch(const Coord3D &pos, float radius, int type) override { note("scorch", "type=" + std::to_string(type) + " r=" + std::to_string(radius), &pos); }
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
	void addBuff(const FXObject &, int type, const std::string *tmpl, int, const RGBColor &, float) override { note("addBuff", std::to_string(type) + (tmpl ? " " + *tmpl : std::string()), nullptr); }
	void removeBuff(const FXObject &, int type) override { note("removeBuff", std::to_string(type), nullptr); }
	void createDecal(const FXDecalDesc &d) override { note("decal", d.name, &d.position); }
	void createRayEffect(const Coord3D &a, const Coord3D &, const std::string &t) override { note("rayEffect", t, &a); }
	void cursorParticles(const std::string &name, unsigned, const GameClientRandomVariable &, const GameClientRandomVariable &, const GameClientRandomVariable &,
		const GameClientRandomVariable &) override { note("cursorParticles", name, nullptr); }
	void laser(const std::string &name, const Coord3D *a, const Coord3D *, bool, const FXObject *, const FXObject *) override { note("laser", name, a); }
	int weather() const override { return 0; }

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
	m_manager->update(true, 0);
	m_emitterWorld.sync(W3DEmitterWorld::kFrameLengthMs * m_frame); // WW3D::Sync (RW 0x516e20): 33 ms per client frame
	m_emitterWorld.update();
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
		if (!source.Read(W3D_Asset_Path(stem), bytes, &err))
		{
			m_emitterFiles.erase(stem);
			return fail(err);
		}
		auto contents = std::make_shared<W3DFileContents>();
		if (!Load_W3D_File(bytes.data(), bytes.size(), *contents, &err))
		{
			m_emitterFiles.erase(stem);
			return fail(W3D_Asset_Path(stem) + ": " + err);
		}
		if (contents->Emitters.size() != 1)
		{
			m_emitterFiles.erase(stem);
			return fail(W3D_Asset_Path(stem) + " holds " + std::to_string(contents->Emitters.size()) + " emitters, expected 1");
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
