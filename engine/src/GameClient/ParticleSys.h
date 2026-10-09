// OpenBFME. GPL-3.0.
//
// The particle simulator: ParticleSystem, Particle and ParticleSystemManager of RotWK 2.01. Port of ZH
// GameEngine/Source/GameClient/System/ParticleSys.cpp as BFME2 / RotWK rebuilt it around FXParticleSystem module templates
// (per-category module instances, per-particle module state, storage modules). Lane FX-1.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; derivation in the lane notes fx-system-runtime.md and
// fx-modules-maths.md, spec fx-and-particles.md; RW addresses are given at each routine in ParticleSys.cpp):
//   * time unit: every counter (Lifetime, SystemLifetime, BurstDelay, InitialDelay, keyframe frames, LifeEvent times) is
//     decremented once per ParticleSystemManager::update, which the engine calls once per Display::draw (RW 0x449d48) under
//     the 30 FPS limit; the client frame counter (TheGameClient->getFrame, RW +0x10) advances once per engine loop while in
//     game. There is no delta time. The simulator is therefore stepped at a fixed 30 Hz by its host (`step()` of the host:
//     frame++ then manager.update()); it never reads a clock and is independent of the 5 Hz logic rate.
//   * every random draw uses the CLIENT stream (RW generator 0xda1c74), never the logic stream; the draw order is the
//     one recorded in the notes (section 7 of fx-modules-maths.md) and is reproduced draw for draw.
//   * IsGroundAligned, UseMaximumHeight and the Draw modules are consumed by the renderer, not by the simulation.
//
// The engine services the simulator needs (terrain height, shroud, attached object/drawable lookup, FXList playback,
// GlobalData and LOD values, the client random stream and frame counter) are the ParticleEnvironment interface; this lane
// creates no Object or Drawable classes.

#pragma once

#include "GameClient/FXList.h"
#include "GameClient/FXParticleSystem.h"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class W3DDrawRandom;

namespace FXParticleSystem
{

typedef std::uint32_t ParticleSystemID;
enum
{
	INVALID_PARTICLE_SYSTEM_ID = 0
};

// RW GlobalData and GameLOD values the simulator reads (RW 0x5fc39a, 0x5fa2ce, 0x96b2ec). Defaults are the RW constructor / retail
// gamedata.ini values; the host overwrites them from GameData / StaticGameLOD / DynamicGameLOD when those are loaded.
struct ParticleSettings
{
	bool useFX = true;                        ///< GlobalData +0x9af: update() and createParticle refuse when off
	float particleScale = 1.0f;               ///< GlobalData +0x9ec ParticleScale
	std::uint32_t maxParticleCount = 2500;    ///< GlobalData +0xac8 MaxParticleCount (StaticGameLOD overwrites it)
	int minDynamicParticlePriority = 0;       ///< GameLOD +0x17a0: a particle of a lower priority is refused
	int minDynamicParticleSkipPriority = 0;   ///< GameLOD +0x17a4: priorities below it are thinned by the skip mask
	int dynamicParticleSkipMask = 0;          ///< GameLOD +0x1790
	int numParticleGenerations = 0;           ///< GameLOD +0x178c (mutated by createParticle)
};

// What an attached object or drawable looks like to a particle system (RW 0x5fa2ce).
struct ParticleAttachInfo
{
	bool found = false;
	Matrix3D transform;        ///< the drawable / object transform (RW 0x6765b9 / object +8)
	Coord3D position;          ///< the drawable / object position (wind adds it, RW 0x966dff)
	bool hidden = false;       ///< drawable byte +0x441 == 0
	bool shrouded = false;     ///< drawable byte +0x440, or object getShroudedStatus >= FOGGED
	bool hasBone = false;      ///< getPristineBonePositions found the bone
	Matrix3D boneTransform;
	bool hasInfluence = false; ///< Swirly: the nearest object control point (unit tangent), see DefaultPhysics
	Coord3D swirlTangent;
};

class ParticleEnvironment
{
public:
	virtual ~ParticleEnvironment() = default;
	virtual W3DDrawRandom &clientRandom() = 0;
	virtual std::uint32_t clientFrame() const = 0;                       ///< TheGameClient->getFrame() (RW +0x10)
	virtual float groundHeight(float x, float y) = 0;                    ///< TheTerrainLogic vtable +0x18
	virtual int shroudStatusAt(const Coord3D &pos) = 0;                  ///< RW 0xb4d990: 0 clear, 1 fogged, 2 shrouded for the local player
	// attachment (RW 0x5fa2ce): drawable first, then object
	virtual ParticleAttachInfo attachedDrawable(std::uint32_t drawableId, const std::string &boneName) = 0;
	virtual ParticleAttachInfo attachedObject(std::uint32_t objectId, const std::string &boneName, int localPlayerIndex) = 0;
	virtual ParticleAttachInfo swirlInfluence(std::uint32_t objectId, const Coord3D &pos) { (void)objectId; (void)pos; return ParticleAttachInfo(); }
	// Event modules fire an FXList by name at a position (RW FXList::doFXPos static wrapper 0x494615)
	virtual void playFXList(const std::string &fxListName, const Coord3D &pos, const Matrix3D *mtx) = 0;
	virtual bool hasFXList(const std::string &fxListName) = 0;           ///< RW 0x5e20a2 found a list (an event whose list is unknown never fires)
};

// ZH ParticleInfo as RW's generateParticleInfo fills it (RW 0x40-byte struct, vtable 0xbf76e0).
struct ParticleInfo
{
	Coord3D vel;          // +0x10
	Coord3D pos;          // +0x1c
	Coord3D emitterPos;   // +0x28
	std::uint32_t lifetime = 0; // +0x34
	bool particleUpTowardsEmitter = false; // +0x38
};

// ---- per-particle module state (RW Particle +0x94..+0xb4; created in this order: Color, Alpha, Update, Physics, Wind, Events) ----
struct ColorState // RW 0xb0 bytes, ctor 0x96a314
{
	bool present = false;
	float key[MAX_KEYFRAMES][3];
	std::uint32_t keyFrame[MAX_KEYFRAMES];
	float colorScale = 0.0f; // drawn, never applied
	float color[3];
	float rate[3];
	int targetKey = 1;
};

struct AlphaState // RW 0x5c bytes, ctor 0x969b39
{
	bool present = false;
	float value[MAX_KEYFRAMES];
	std::uint32_t frame[MAX_KEYFRAMES];
	float alpha = 0.0f;
	float rate = 0.0f;
	int targetKey = 1;
};

struct UpdateState // DefaultUpdate (RW 0x38) or RenderObjectUpdate
{
	bool present = false;
	bool isRenderObject = false;
	float size[3];       // DefaultUpdate uses [0]
	float sizeRate[3];
	float sizeDamping[3];
	float angleZ = 0.0f, angularRateZ = 0.0f, angularDamping = 0.0f;
	int rotation = 0;
	float angleXY = 0.0f, angularRateXY = 0.0f, angularDampingXY = 0.0f;
};

struct PhysicsState // RW 0x18 bytes, ctor 0x96acea
{
	bool present = false;
	float velocityDamping = 0.0f;
	bool swirly = false;
	bool attachToBone = false;
};

struct WindParticleState
{
	bool present = false;
	float windRandomness = 0.0f;
};

struct EventParticleState
{
	std::uint32_t eventTime = 0;
	bool armed = true;
	bool kill = true;
	float heightOffset = 0.0f; // TerrainCollision
	bool orient = false;
	int moduleIndex = 0;       // index into the template's event list
};

class ParticleSystem;

struct Particle
{
	// RW Particle layout (CPU): accel +0x04, vel +0x10, pos +0x1c, emitterPos +0x28, lifetime +0x34, flag +0x38
	Coord3D accel;
	Coord3D vel;
	Coord3D pos;
	Coord3D emitterPos;
	std::uint32_t lifetime = 0;
	bool upTowardsEmitter = false;
	std::uint32_t lifetimeLeft = 0;  // +0x54 (0 = infinite)
	std::uint32_t createFrame = 0;   // +0x58
	std::uint32_t particleID = 0;    // +0x88
	// module states
	ColorState color;
	AlphaState alpha;
	UpdateState update;
	PhysicsState physics;
	WindParticleState wind;
	std::vector<EventParticleState> events;
	// bookkeeping
	ParticleSystem *system = nullptr; ///< the owning system (RW +0x3c handle)
	ParticleSystemID systemID = INVALID_PARTICLE_SYSTEM_ID;
	ParticleSystemID controlledSystem = INVALID_PARTICLE_SYSTEM_ID; ///< PerParticleAttachedSystem child (RW +0x78)
	int sysPrev = -1, sysNext = -1;  ///< per-system list (RW +0x64 / +0x68), oldest first
	int globPrev = -1, globNext = -1;///< per-priority global list (RW +0x6c / +0x70)
	bool inUse = false;
	float random = 0.0f;             ///< GPU particles: GetGameClientRandomValueReal(0, 15) stamped at creation (RW 0x7b10b4)
	float gpuBirth = 0.0f;           ///< GPU particles: the sync time in float frames at creation (RW 0x7b1141)
	float gpuExpiry = 0.0f;          ///< GPU particles: birth + life; the entry is dropped once expiry < now
};

class ParticleSystemManager;
class ParticleSystem;

// RW ParticleSystem (object 0x1dc bytes, vtable 0xbf7b48).
class ParticleSystem
{
public:
	ParticleSystemID id() const { return m_id; }
	const ParticleSystemTemplate &getTemplate() const { return *m_template; }
	const ParticleSystemInfo &info() const { return m_info; }
	int priority() const { return m_info.m_priority; }
	int shaderType() const { return m_info.m_shaderType; }
	int particleType() const { return m_info.m_particleType; }
	bool isDestroyed() const { return m_isDestroyed; }
	bool isStopped() const { return m_isStopped; }
	std::uint32_t particleCount() const { return m_particleCount; }
	const Matrix3D &transform() const { return m_transform; }
	const Coord3D &position() const { return m_pos; }
	std::uint32_t startFrame() const { return m_startFrame; }
	std::uint32_t systemLifetimeLeft() const { return m_systemLifetimeLeft; }
	std::uint32_t initialDelayLeft() const { return m_initialDelayLeft; }
	std::uint32_t burstDelayLeft() const { return m_burstDelayLeft; }
	ParticleSystemID masterID() const { return m_masterID; }
	ParticleSystemID slaveID() const { return m_slaveID; }
	bool isIdentity() const { return m_isIdentity; }
	const std::string &attachedBone() const { return m_attachedBone; }
	std::uint32_t attachedDrawableID() const { return m_attachedDrawableID; } ///< lane FX-3 (tests)
	// the first / next particle in this system's list, oldest first (-1 at the end)
	int firstParticle() const { return m_firstParticle; }

	// RW 0x5f3c65, 0x5f2ede, 0x5f2ee6, 0x5f2f2d, 0x5f2f55, 0x5f2fc2/0x5f30b9/0x5f31b0, 0x5f32d7, 0x5f32b4, 0x5f32f4, 0x5f3397, 0x5f33b5
	void destroy();
	void start() { m_isStopped = false; }
	void stop() { m_isStopped = true; }
	void setPosition(const Coord3D &pos);
	void setLocalTransform(const Matrix3D &m);
	void rotateLocalTransformX(float radians);
	void rotateLocalTransformY(float radians);
	void rotateLocalTransformZ(float radians);
	void attachToObject(std::uint32_t objectId) { m_attachedObjectID = objectId; }
	void attachToDrawable(std::uint32_t drawableId) { m_attachedDrawableID = drawableId; }
	void attachToBone(const std::string &bone) { m_attachedBone = bone; }
	void setSkipParentXfrm(bool skip) { m_skipParentXfrm = skip; }
	void setLifetimeRange(float low, float high) { m_info.m_lifetime.setRange(low, high, GameClientRandomVariable::UNIFORM); }
	void setTarget(const Coord3D &p) { m_target = p; m_hasTarget = true; } ///< RW +0x188 / +0x194 (the lightning target)
	void setSystemLife(int frames) { m_systemLifetimeLeft = (std::uint32_t)frames; m_isForever = (frames == 0) ? m_isForever : false; }
	void setInitialDelayFrames(int frames) { m_initialDelayLeft = (std::uint32_t)frames; }
	void setUpdateWhilePaused(bool b) { m_updateWhilePaused = b; }
	void setSizeCoeff(float k) { m_sizeCoeff = k; }

	// RW Particle::isInvisible 0x5f449f (the draw modules skip such particles too)
	bool isParticleInvisible(const Particle &p) const;
	// the module data a draw / simulation consumer needs
	const ModuleData *drawModule() const { return m_draw; }
	const DefaultColorModuleData *colorModule() const { return m_color; }
	const DefaultAlphaModuleData *alphaModule() const { return m_alpha; }
	const DefaultUpdateModuleData *updateModule() const { return m_updateDefault; }
	const RenderObjectUpdateModuleData *renderObjectUpdateModule() const { return m_updateRenderObject; }
	const DefaultPhysicsModuleData *physicsModule() const { return m_physics; }
	float gpuNow() const { return gpuNowFrames(); }

private:
	friend class ParticleSystemManager;
	ParticleSystem(ParticleSystemManager &manager, const ParticleSystemTemplate *tmpl, ParticleSystemID id);

	// RW 0x5fa2ce
	bool update(int localPlayerIndex);
	// RW 0x5f3ccf
	void emit(const Coord3D &pos, int priority, bool isIdentity, const Matrix3D &xf);
	// RW 0x5f9319 / 0x5f4c6b / 0x5fc39a
	void createParticles(int priority, int count);
	void generateParticleInfo(ParticleInfo &info, int particleNum, int particleCount);
	bool createParticle(const ParticleInfo &info, int priority, const ParticleSystemTemplate *attached, bool force);
	// RW 0x5f4c02 / 0x5f4b88
	Coord3D computeParticlePosition(int particleNum, int particleCount);
	Coord3D computeParticleVelocity(const Coord3D &pos);
	// RW 0x5f332e
	bool finish();
	void updateStorage();
	bool updateParticle(int index);
	void initParticleModules(Particle &p);
	void updateColor(Particle &p);
	void updateAlpha(Particle &p);
	void updatePhysics(Particle &p);
	void updateWind(Particle &p);
	void updateParticleEvents(Particle &p);
	void computeColorRate(ColorState &c) const;
	void computeAlphaRate(AlphaState &a) const;
	float gpuNowFrames() const;
	void updateWindSystemState();
	void updateSystemEvents();

	ParticleSystemManager &m_manager;
	const ParticleSystemTemplate *m_template;
	ParticleSystemID m_id;
	ParticleSystemInfo m_info; // the per-instance copy of the template's info (RW copies +0x04.. and the RVs)

	// cached module data (null when the template block had none)
	const DefaultColorModuleData *m_color = nullptr;
	const DefaultAlphaModuleData *m_alpha = nullptr;
	const DefaultUpdateModuleData *m_updateDefault = nullptr;
	const RenderObjectUpdateModuleData *m_updateRenderObject = nullptr;
	const DefaultPhysicsModuleData *m_physics = nullptr;
	const ModuleData *m_velocity = nullptr;
	const ModuleData *m_volume = nullptr;
	const ModuleData *m_draw = nullptr;
	const DefaultWindModuleData *m_wind = nullptr;

	// RW instance fields
	bool m_updateWhilePaused = false;                 // +0x98
	std::uint32_t m_attachedDrawableID = 0;           // +0xb0
	std::uint32_t m_attachedObjectID = 0;             // +0xb4
	std::string m_attachedBone;                       // +0xb8
	Matrix3D m_localTransform;                        // +0xbc
	Matrix3D m_transform;                             // +0xec
	std::uint32_t m_burstDelayLeft = 0;               // +0x11c
	std::uint32_t m_initialDelayLeft = 0;             // +0x120
	std::uint32_t m_startFrame = 0;                   // +0x124
	std::uint32_t m_systemLifetimeLeft = 0;           // +0x128
	std::uint32_t m_nextParticleID = 0;               // +0x12c
	Coord3D m_velCoeff{ 1.0f, 1.0f, 1.0f };           // +0x130
	float m_burstCountCoeff = 1.0f;                   // +0x13c
	float m_burstDelayCoeff = 1.0f;                   // +0x140
	Coord3D m_pos;                                    // +0x144
	Coord3D m_lastPos;                                // +0x150
	ParticleSystemID m_slaveID = INVALID_PARTICLE_SYSTEM_ID;  // +0x15c
	ParticleSystemID m_masterID = INVALID_PARTICLE_SYSTEM_ID; // +0x16c
	float m_sizeCoeff = 1.0f;                         // +0x17c
	float m_accumulatedSizeBonus = 0.0f;              // +0x180
	float m_sizeCoeffY = 1.0f;                        // +0x184
	Coord3D m_target;                                 // +0x188
	bool m_hasTarget = false;                         // +0x194
	int m_controlParticle = -1;                       // +0x19c
	bool m_isLocalIdentity = true;                    // +0x1a0
	bool m_isIdentity = true;                         // +0x1a1
	bool m_isForever = true;                          // +0x1a2
	bool m_isStopped = false;                         // +0x1a3
	bool m_isDestroyed = false;                       // +0x1a4
	bool m_isFirstPos = true;                         // +0x1a5
	bool m_skipParentXfrm = false;                    // +0x1a8

	// system-level module state: DefaultWind (RW 0x966c5d), per event module (RW 0x96baae / 0x96c1d6)
	struct WindSystemState
	{
		int mode = 0;
		float angle = 0.0f, rate = 0.0f, lower = 0.0f, upper = 0.0f;
		bool movingToEnd = true;
	} m_windState;
	struct EventSystemState
	{
		std::uint32_t eventTime = 0;
		bool armed = true;
	};
	std::vector<EventSystemState> m_eventStates;

	// storage module (CPU): the system's particle list, oldest first
	int m_firstParticle = -1, m_lastParticle = -1;
	std::uint32_t m_particleCount = 0;
};

// RW ParticleSystemManager (vtable 0xbf7a00; update = slot 10 0x5f5123).
class ParticleSystemManager
{
public:
	ParticleSystemManager(const FXParticleSystemTemplateStore &templates, ParticleEnvironment &env);
	~ParticleSystemManager();

	ParticleSettings &settings() { return m_settings; }
	const ParticleSettings &settings() const { return m_settings; }
	ParticleEnvironment &environment() { return m_env; }
	const FXParticleSystemTemplateStore &templates() const { return m_templates; }

	// RW 0x5f526a: createParticleSystem(template, createSlaves). Null template -> INVALID id.
	ParticleSystemID createParticleSystem(const ParticleSystemTemplate *tmpl, bool createSlaves);
	// RW 0x5f52a4
	ParticleSystemID createAttachedParticleSystemID(const ParticleSystemTemplate *tmpl, std::uint32_t objectId, bool createSlaves);
	// RW 0x5f530a / 0x5f5379
	ParticleSystem *findParticleSystemByID(ParticleSystemID id);
	const ParticleSystem *findParticleSystemByID(ParticleSystemID id) const;
	void destroyParticleSystemByID(ParticleSystemID id);
	// RW 0x5f53be
	void destroyAttachedSystems(std::uint32_t objectId);
	// RW 0x5f5123: one particle step. `gameRunning` = (TheGameClient +0xc8 in-game) && !paused.
	void update(bool gameRunning = true, int localPlayerIndex = 0);
	// RW 0x5f5b6c
	void reset();

	std::uint32_t particleCount() const { return m_particleCount; }
	size_t systemCount() const { return m_systems.size(); }
	// RW 0x5f3f18 (including its return-value quirk: n + 1 on full success)
	int removeOldestParticles(std::uint32_t count, int priorityCap);

	// iteration for the renderer and the tests (creation order, slaves before masters)
	template <class F>
	void forEachSystem(F &&f) const
	{
		for (const std::unique_ptr<ParticleSystem> &s : m_systems)
		{
			f(*s);
		}
	}
	const Particle &particle(int index) const { return m_pool[(size_t)index]; }

	// Acceptance-stop lines for behaviour RW has that this port does not reproduce, raised the first time a system hits them (S-191, S-192).
	std::vector<std::string> unverified() const;
	void noteUnverified(const char *code, const char *text);

	// the LightningEmission polyline buffer shared by all systems (RW static 0xdebba8)
	float (&lightningPoints())[30][3] { return m_lightning; }

private:
	friend class ParticleSystem;
	int allocParticle();
	void freeParticle(int index);
	void addParticleToGlobal(int index, int priority);
	void removeParticleFromGlobal(int index);
	void deleteParticle(int index);
	void deleteSystem(ParticleSystem *sys);

	const FXParticleSystemTemplateStore &m_templates;
	ParticleEnvironment &m_env;
	ParticleSettings m_settings;
	std::list<std::unique_ptr<ParticleSystem>> m_systems; // creation order
	std::unordered_map<ParticleSystemID, ParticleSystem *> m_byID; // lookup only (never iterated)
	std::vector<Particle> m_pool;
	std::vector<int> m_free;
	int m_head[7];
	int m_tail[7];
	std::uint32_t m_particleCount = 0;
	ParticleSystemID m_uniqueSystemID = 0;
	float m_lightning[30][3];
	std::vector<std::pair<std::string, std::string>> m_unverified;
};

// A handle that forwards to a live system by id and is a harmless no-op once the system is gone (RW TrackingPtr: a dead handle
// resolves to the null system, 0xde37a0). This is what an FXList nugget receives.
class ParticleSystemHandle : public FXParticleSystemRef
{
public:
	ParticleSystemHandle(ParticleSystemManager &manager, ParticleSystemID id) : m_manager(manager), m_id(id) {}
	ParticleSystemID id() const { return m_id; }
	void setLocalTransform(const Matrix3D &m) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->setLocalTransform(m); }
	void rotateLocalX(float r) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->rotateLocalTransformX(r); }
	void rotateLocalY(float r) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->rotateLocalTransformY(r); }
	void rotateLocalZ(float r) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->rotateLocalTransformZ(r); }
	void setPosition(const Coord3D &p) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->setPosition(p); }
	void attachToObject(std::uint32_t objectId) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->attachToObject(objectId); }
	void attachToBone(const std::string &bone) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->attachToBone(bone); }
	void setTarget(const Coord3D &p) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->setTarget(p); }
	void setSystemLife(int frames) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->setSystemLife(frames); }
	void setInitialDelayFrames(int frames) override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->setInitialDelayFrames(frames); }
	void destroy() override { if (ParticleSystem *s = m_manager.findParticleSystemByID(m_id)) s->destroy(); }

private:
	ParticleSystemManager &m_manager;
	ParticleSystemID m_id;
};

} // namespace FXParticleSystem
