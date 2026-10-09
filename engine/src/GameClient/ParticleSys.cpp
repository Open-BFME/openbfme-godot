// OpenBFME. GPL-3.0. See ParticleSys.h for the target / donor facts. Addresses are RotWK game.dat (RW), stop S-001 caveat.
//
// Every float operation is performed in the order the RW code performs it (notes fx-modules-maths.md section 1.2: x87 at 24-bit
// precision is strict float; transcendental CRT calls are evaluated in double and rounded when stored). This file is compiled with
// -ffp-contract=off (cmake/Fx.cmake); no fused multiply-add may appear.

#include "GameClient/ParticleSys.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace FXParticleSystem
{

namespace
{

const float kTwoPi = 6.2831855f;      // RW 0xbdd38c
const float kPi = 3.1415927f;         // RW 0xbdd388
const float kEpsilon = 1.1920929e-7f; // RW 0xbf7b2c / 0xbf7b30
const float kMaxSizeBonus = 50.0f;    // RW 0xbd88c4
const float kTo01 = 0.003921568859f;  // RW 0x3b808081 (colour byte scale)

enum
{
	GPU_SLOT_COUNT = 0x80 // RW 0x5f32ff: type 7 storage has 128 slots
};

float realDraw(W3DDrawRandom &r, float lo, float hi) { return r.real(lo, hi); }

Coord3D normalized(Coord3D v)
{
	// RW 0x403175: len = (float)sqrt((x*x + y*y) + z*z); inv = 1/len; scale only when len != 0
	const float s = (v.x * v.x + v.y * v.y) + v.z * v.z;
	const float len = (float)std::sqrt((double)s);
	if (len != 0.0f)
	{
		const float inv = 1.0f / len;
		v.x *= inv;
		v.y *= inv;
		v.z *= inv;
	}
	return v;
}

// RW 0x7b0614 (fxpsemittervolumemodule.cpp lines 31-33): a normalised uniform cube sample, retried only on the exact zero vector
Coord3D fillUnitVector(W3DDrawRandom &r)
{
	Coord3D v;
	do
	{
		v.x = realDraw(r, -1.0f, 1.0f);
		v.y = realDraw(r, -1.0f, 1.0f);
		v.z = realDraw(r, -1.0f, 1.0f);
	} while (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
	return normalized(v);
}

// RW 0x403312 / 0x4032dd
Coord3D normalized2D(float x, float y)
{
	const float len = (float)std::sqrt((double)(x * x + y * y));
	if (len != 0.0f)
	{
		const float inv = 1.0f / len;
		x *= inv;
		y *= inv;
	}
	return Coord3D{ x, y, 0.0f };
}

// RW 0x4035ac
float approxLength3D(const Coord3D &v)
{
	auto A = [](float a, float b) {
		const float fa = std::fabs(a), fb = std::fabs(b);
		const float mx = fa > fb ? fa : fb, mn = fa > fb ? fb : fa;
		return mx + 0.25f * mn;
	};
	const float l2 = A(v.x, v.y);
	const float az = std::fabs(v.z);
	const float mx = l2 > az ? l2 : az, mn = l2 > az ? az : l2;
	return mx + 0.25f * mn;
}

float clamp01(float v)
{
	if (v < 0.0f)
	{
		return 0.0f;
	}
	if (v > 1.0f)
	{
		return 1.0f;
	}
	return v;
}

std::uint32_t floatBits(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

// `_ftol` (RW 0xa3cfa4): truncation toward zero
int ftol(float f) { return (int)f; }

// ---- RW Matrix3D post-multiplied rotations (ZH matrix3d.h Rotate_X/Y/Z, used by rotateLocalTransform*) ------------------
void rotateX(Matrix3D &m, float theta)
{
	const float s = (float)std::sin((double)theta), c = (float)std::cos((double)theta);
	for (int r = 0; r < 3; ++r)
	{
		const float a = m.Row[r][1], b = m.Row[r][2];
		m.Row[r][1] = c * a + s * b;
		m.Row[r][2] = -s * a + c * b;
	}
}
void rotateY(Matrix3D &m, float theta)
{
	const float s = (float)std::sin((double)theta), c = (float)std::cos((double)theta);
	for (int r = 0; r < 3; ++r)
	{
		const float a = m.Row[r][0], b = m.Row[r][2];
		m.Row[r][0] = c * a - s * b;
		m.Row[r][2] = s * a + c * b;
	}
}
void rotateZ(Matrix3D &m, float theta)
{
	const float s = (float)std::sin((double)theta), c = (float)std::cos((double)theta);
	for (int r = 0; r < 3; ++r)
	{
		const float a = m.Row[r][0], b = m.Row[r][1];
		m.Row[r][0] = c * a + s * b;
		m.Row[r][1] = -s * a + c * b;
	}
}

float lodScale(const ParticleSettings &s) { return (s.particleScale + 1.0f) * 0.5f; }

} // namespace

// ============================================================================================================================
// ParticleSystem
// ============================================================================================================================
ParticleSystem::ParticleSystem(ParticleSystemManager &manager, const ParticleSystemTemplate *tmpl, ParticleSystemID id)
    : m_manager(manager), m_template(tmpl), m_id(id), m_info(tmpl->info())
{
	// RW 0x5fbe28 constructor order (notes fx-system-runtime.md 2.2)
	m_isLocalIdentity = true;
	m_isIdentity = true;
	m_isFirstPos = true;
	W3DDrawRandom &rng = manager.environment().clientRandom();
	// step 5: RANDOM DRAW #1 InitialDelay (+0x120 = ftol(...)), then the start frame
	m_initialDelayLeft = (std::uint32_t)ftol(m_info.m_initialDelay.getValue(rng));
	m_startFrame = manager.environment().clientFrame();
	// step 6
	m_systemLifetimeLeft = m_info.m_systemLifetime;
	m_isForever = (m_info.m_systemLifetime == 0);

	m_color = static_cast<const DefaultColorModuleData *>(tmpl->module(MODULE_CATEGORY_COLOR));
	m_alpha = static_cast<const DefaultAlphaModuleData *>(tmpl->module(MODULE_CATEGORY_ALPHA));
	if (const ModuleData *u = tmpl->module(MODULE_CATEGORY_UPDATE))
	{
		if (u->classId == MODULE_RENDER_OBJECT_UPDATE)
		{
			m_updateRenderObject = static_cast<const RenderObjectUpdateModuleData *>(u);
		}
		else
		{
			m_updateDefault = static_cast<const DefaultUpdateModuleData *>(u);
		}
	}
	m_physics = static_cast<const DefaultPhysicsModuleData *>(tmpl->module(MODULE_CATEGORY_PHYSICS));
	m_velocity = tmpl->module(MODULE_CATEGORY_EMISSION_VELOCITY);
	m_volume = tmpl->module(MODULE_CATEGORY_EMISSION_VOLUME);
	m_draw = tmpl->module(MODULE_CATEGORY_DRAW);
	m_wind = static_cast<const DefaultWindModuleData *>(tmpl->module(MODULE_CATEGORY_WIND));

	// step 8: slave system (the recursion runs the slave's whole constructor first)
	if (!m_info.m_slaveSystemName.empty())
	{
		if (const ParticleSystemTemplate *slaveTemplate = manager.templates().findTemplate(m_info.m_slaveSystemName))
		{
			const ParticleSystemID slave = manager.createParticleSystem(slaveTemplate, true);
			if (slave != INVALID_PARTICLE_SYSTEM_ID)
			{
				m_slaveID = slave;
				if (ParticleSystem *s = manager.findParticleSystemByID(slave))
				{
					s->m_masterID = m_id;
				}
			}
		}
	}

	if (m_info.m_particleType == PARTICLE_TYPE_GPU_TERRAINFIRE)
	{
		manager.noteUnverified("S-191", "GPU_TERRAINFIRE systems emit nothing: RW walks the terrain fire cell list (RW 0x7b13a5, global 0xde46a8), which the terrain lane has not ported");
	}
	// step 10: system-level module state. Only Wind and the Event modules consume randomness.
	if (m_wind)
	{
		// RW 0x966c5d: lower = R(startMin,startMax), upper = R(endMin,endMax), angle = R(lower,upper); the rate is the template
		// word at +0x20 (constructor default 0.15, not an INI field); movingToEnd defaults to true
		m_windState.mode = m_wind->m_windMotion;
		m_windState.lower = realDraw(rng, m_wind->m_windPingPongStartAngleMin, m_wind->m_windPingPongStartAngleMax);
		m_windState.upper = realDraw(rng, m_wind->m_windPingPongEndAngleMin, m_wind->m_windPingPongEndAngleMax);
		m_windState.angle = realDraw(rng, m_windState.lower, m_windState.upper);
		m_windState.rate = 0.15f;
		m_windState.movingToEnd = true;
	}
	for (const std::shared_ptr<ModuleData> &e : tmpl->events())
	{
		EventSystemState st;
		if (e->classId == MODULE_LIFE_EVENT)
		{
			st.eventTime = (std::uint32_t)ftol(static_cast<const LifeEventModuleData *>(e.get())->m_eventTime.getValue(rng)); // RW 0x96baae
		}
		else
		{
			// RW 0x96c1d6: one HeightOffset draw whose value is unused
			(void)static_cast<const TerrainCollisionModuleData *>(e.get())->m_heightOffset.getValue(rng);
		}
		m_eventStates.push_back(st);
	}
}

void ParticleSystem::destroy()
{
	// RW 0x5f3c65: marks this system and walks the slave chain
	ParticleSystem *s = this;
	for (int guard = 0; s && guard < 64; ++guard)
	{
		s->m_isDestroyed = true;
		s = s->m_slaveID != INVALID_PARTICLE_SYSTEM_ID ? m_manager.findParticleSystemByID(s->m_slaveID) : nullptr;
	}
}

void ParticleSystem::setPosition(const Coord3D &pos)
{
	// RW 0x5f2f2d: writes only the local transform translation (m_pos stays 0)
	m_localTransform.Row[0][3] = pos.x;
	m_localTransform.Row[1][3] = pos.y;
	m_localTransform.Row[2][3] = pos.z;
	m_isLocalIdentity = false;
}

void ParticleSystem::setLocalTransform(const Matrix3D &m)
{
	m_localTransform = m;
	m_isLocalIdentity = false;
}

void ParticleSystem::rotateLocalTransformX(float radians)
{
	rotateX(m_localTransform, radians);
	m_isLocalIdentity = false;
}
void ParticleSystem::rotateLocalTransformY(float radians)
{
	rotateY(m_localTransform, radians);
	m_isLocalIdentity = false;
}
void ParticleSystem::rotateLocalTransformZ(float radians)
{
	rotateZ(m_localTransform, radians);
	m_isLocalIdentity = false;
}

// RW 0x5fa2ce
bool ParticleSystem::update(int localPlayerIndex)
{
	ParticleEnvironment &env = m_manager.environment();
	if (!m_manager.settings().useFX)
	{
		return false;
	}
	if (m_initialDelayLeft != 0)
	{
		if (--m_initialDelayLeft == 0)
		{
			m_startFrame = env.clientFrame();
		}
		return true;
	}
	bool isHidden = false;
	bool isShrouded = false;
	if (m_masterID == INVALID_PARTICLE_SYSTEM_ID) // slaves skip the whole block
	{
		const Matrix3D *parent = nullptr;
		ParticleAttachInfo attach;
		if (m_attachedDrawableID != 0)
		{
			attach = env.attachedDrawable(m_attachedDrawableID, m_attachedBone);
			if (attach.found)
			{
				isHidden = attach.hidden;
				isShrouded = attach.shrouded;
				parent = &attach.transform;
				// for a Drawable the position is taken before the bone local transform
				m_lastPos = m_pos;
				m_pos = Coord3D{ parent->Row[0][3], parent->Row[1][3], parent->Row[2][3] };
				if (!m_attachedBone.empty() && attach.hasBone)
				{
					setLocalTransform(attach.boneTransform);
				}
			}
			else
			{
				m_attachedDrawableID = 0;
				destroy();
			}
		}
		else if (m_attachedObjectID != 0)
		{
			attach = env.attachedObject(m_attachedObjectID, m_attachedBone, localPlayerIndex);
			if (attach.found)
			{
				isShrouded = attach.shrouded;
				isHidden = attach.hidden;
				parent = &attach.transform;
				if (!m_attachedBone.empty() && attach.hasBone)
				{
					setLocalTransform(attach.boneTransform);
				}
				// for an Object the position is taken after the bone local transform
				m_lastPos = m_pos;
				m_pos = Coord3D{ parent->Row[0][3], parent->Row[1][3], parent->Row[2][3] };
			}
			else
			{
				m_attachedObjectID = 0;
				destroy();
			}
		}
		if (parent)
		{
			if (m_skipParentXfrm)
			{
				m_transform = m_localTransform;
			}
			else if (!m_isLocalIdentity)
			{
				Matrix3D::Multiply(*parent, m_localTransform, &m_transform); // RW inlines Matrix3D::mul(parent, local)
			}
			else
			{
				m_transform = *parent;
			}
			m_isIdentity = false;
		}
		else if (!m_isLocalIdentity)
		{
			m_transform = m_localTransform;
			m_isIdentity = false;
		}
		else
		{
			m_isIdentity = true; // the transform is NOT touched
		}
		if (m_controlParticle >= 0)
		{
			const Particle &c = m_manager.particle(m_controlParticle);
			m_transform.Row[0][3] = c.pos.x;
			m_transform.Row[1][3] = c.pos.y;
			m_transform.Row[2][3] = c.pos.z;
			m_isIdentity = false;
			m_lastPos = m_pos;
			m_pos = c.pos;
		}
		if (!m_isDestroyed && (m_isForever || m_systemLifetimeLeft > 0) && !isHidden && !m_isStopped && !isShrouded)
		{
			emit(m_pos, m_info.m_priority, m_isIdentity, m_transform);
		}
	}
	// the per-system module chain (RW 0x5f9f6d): Color..Draw have no per-system update; Wind and Event do
	updateWindSystemState();
	updateSystemEvents();
	return finish();
}

// RW 0x9669d8 (the BFME1 fxpswindmodule.cpp 0x5FE480 state machine)
void ParticleSystem::updateWindSystemState()
{
	if (!m_wind)
	{
		return;
	}
	W3DDrawRandom &rng = m_manager.environment().clientRandom();
	WindSystemState &w = m_windState;
	switch (w.mode)
	{
	case WIND_MOTION_CIRCULAR:
		if (w.rate == 0.0f)
		{
			w.rate = realDraw(rng, m_wind->m_windAngleChangeMin, m_wind->m_windAngleChangeMax);
		}
		w.angle += w.rate;
		if (w.angle > kTwoPi)
		{
			w.angle -= kTwoPi;
		}
		else if (w.angle < 0.0f)
		{
			w.angle += kTwoPi;
		}
		break;
	case WIND_MOTION_PING_PONG:
	{
		const float half = (w.upper - w.lower) * 0.5f;
		float speed = (1.0f - (float)std::fabs((double)((half - w.angle) + w.lower)) / half) * w.rate;
		if (0.005f > speed)
		{
			speed = 0.005f; // RW 0x3ba3d70a
		}
		if (w.movingToEnd)
		{
			w.angle += speed;
			if (w.angle >= w.upper)
			{
				w.movingToEnd = false;
				w.rate = realDraw(rng, m_wind->m_windAngleChangeMin, m_wind->m_windAngleChangeMax);
				w.lower = realDraw(rng, m_wind->m_windPingPongStartAngleMin, m_wind->m_windPingPongStartAngleMax);
				w.upper = realDraw(rng, m_wind->m_windPingPongEndAngleMin, m_wind->m_windPingPongEndAngleMax);
			}
		}
		else
		{
			w.angle -= speed;
			if (w.angle <= w.lower)
			{
				w.movingToEnd = true;
				w.rate = realDraw(rng, m_wind->m_windAngleChangeMin, m_wind->m_windAngleChangeMax);
				w.lower = realDraw(rng, m_wind->m_windPingPongStartAngleMin, m_wind->m_windPingPongStartAngleMax);
				w.upper = realDraw(rng, m_wind->m_windPingPongEndAngleMin, m_wind->m_windPingPongEndAngleMax);
			}
		}
		break;
	}
	default:
		break;
	}
}

// RW 0x96b8e5 (LifeEvent) / 0x96bed6 (TerrainCollision), system level
void ParticleSystem::updateSystemEvents()
{
	ParticleEnvironment &env = m_manager.environment();
	const std::vector<std::shared_ptr<ModuleData>> &events = m_template->events();
	for (size_t i = 0; i < events.size(); ++i)
	{
		const EventModuleData *e = static_cast<const EventModuleData *>(events[i].get());
		EventSystemState &st = m_eventStates[i];
		if (!st.armed || e->m_eventFX.empty() || !env.hasFXList(e->m_eventFX))
		{
			continue;
		}
		const Coord3D p{ m_transform.Row[0][3], m_transform.Row[1][3], m_transform.Row[2][3] }; // 0x5f2eee
		if (e->classId == MODULE_LIFE_EVENT)
		{
			if (env.clientFrame() - m_startFrame >= st.eventTime)
			{
				env.playFXList(e->m_eventFX, p, nullptr);
				st.armed = false;
				if (e->m_killAfterEvent)
				{
					m_systemLifetimeLeft = 1;
					m_isForever = false;
				}
			}
		}
		else
		{
			const float gz = env.groundHeight(p.x, p.y);
			if (!(gz < p.z))
			{
				env.playFXList(e->m_eventFX, p, nullptr);
				st.armed = false;
				if (e->m_killAfterEvent)
				{
					m_systemLifetimeLeft = 1;
					m_isForever = false;
				}
			}
		}
	}
}


// RW 0x5f332e
bool ParticleSystem::finish()
{
	updateStorage(); // storage->vf10 (RW 0x7b09a1)
	if (m_info.m_particleType == PARTICLE_TYPE_GPU_TERRAINFIRE)
	{
		return true; // never self-terminates
	}
	if (m_isDestroyed)
	{
		if (m_particleCount == 0)
		{
			return false;
		}
	}
	if (!m_isForever)
	{
		if (m_systemLifetimeLeft != 0)
		{
			--m_systemLifetimeLeft;
		}
		if (m_particleCount == 0 && m_systemLifetimeLeft == 0)
		{
			return false;
		}
	}
	return true;
}

float ParticleSystem::gpuNowFrames() const
{
	// RW 0x7b1160: (float)(SyncTime_ms * 30) * 0.001f. SyncTime advances by the frame length (33 ms, RW default of
	// TheW3DFrameLengthInMsec: INFERENCE for the retail value, see notes fx-w3d-emitters) per client frame.
	return (float)((std::uint64_t)m_manager.environment().clientFrame() * 33u * 30u) * 0.001f;
}

void ParticleSystem::updateStorage()
{
	if (m_info.m_particleType == PARTICLE_TYPE_GPU_PARTICLE)
	{
		// RW 0x7b12f4: the expiry queue pops entries whose expiry is behind `now`; nothing else happens on the CPU
		const float now = gpuNowFrames();
		for (int p = m_firstParticle; p >= 0;)
		{
			const int next = m_manager.particle(p).sysNext;
			if (m_manager.particle(p).gpuExpiry < now)
			{
				m_manager.deleteParticle(p);
			}
			p = next;
		}
		return;
	}
	if (m_info.m_particleType == PARTICLE_TYPE_GPU_TERRAINFIRE)
	{
		return; // needs the terrain fire cell list (stop S-191)
	}
	// CPU storage: oldest first; particles created earlier in this system update are included
	for (int p = m_firstParticle; p >= 0;)
	{
		const int next = m_manager.particle(p).sysNext;
		if (!updateParticle(p))
		{
			m_manager.deleteParticle(p);
		}
		p = next;
	}
}

// ---- emit (RW 0x5f3ccf) -----------------------------------------------------------------------------------------------
void ParticleSystem::emit(const Coord3D &posIn, int priority, bool isIdentity, const Matrix3D &xf)
{
	ParticleEnvironment &env = m_manager.environment();
	W3DDrawRandom &rng = env.clientRandom();
	ParticleSystem *sys = this;
	Coord3D pos = posIn;
	for (int guard = 0; sys && guard < 64; ++guard)
	{
		if (sys->m_masterID != INVALID_PARTICLE_SYSTEM_ID) // slave systems
		{
			sys->m_isIdentity = isIdentity;
			if (!isIdentity)
			{
				sys->m_transform = xf;
			}
			sys->m_pos = pos;
			sys->m_pos.x += sys->m_info.m_slavePosOffset.x;
			sys->m_pos.y += sys->m_info.m_slavePosOffset.y;
			sys->m_pos.z += sys->m_info.m_slavePosOffset.z;
		}
		if (sys->m_burstDelayLeft == 0)
		{
			const int n = ftol(sys->m_info.m_burstCount.getValue(rng)); // RANDOM DRAW, even without a storage module
			sys->createParticles(priority, (int)((float)n * sys->m_burstCountCoeff));
			const std::uint32_t u = (std::uint32_t)ftol(sys->m_info.m_burstDelay.getValue(rng)); // RANDOM DRAW after the particles
			sys->m_burstDelayLeft = (std::uint32_t)((float)u * sys->m_burstDelayCoeff);
		}
		else
		{
			if (sys->m_info.m_isOneShot)
			{
				sys->m_burstDelayLeft = 1; // one-shot: never counts down, so only the first burst ever fires
			}
			else
			{
				--sys->m_burstDelayLeft;
			}
		}
		if (sys->m_slaveID == INVALID_PARTICLE_SYSTEM_ID)
		{
			break;
		}
		pos = sys->m_pos;
		sys = m_manager.findParticleSystemByID(sys->m_slaveID);
	}
}

// RW 0x5f9319
void ParticleSystem::createParticles(int priority, int count)
{
	const ParticleSystemTemplate *attached = nullptr;
	if (!m_info.m_attachedSystemName.empty())
	{
		attached = m_manager.templates().findTemplate(m_info.m_attachedSystemName);
	}
	ParticleEnvironment &env = m_manager.environment();
	const bool gpu = m_info.m_particleType == PARTICLE_TYPE_GPU_PARTICLE;
	for (int i = 0; i < count; ++i)
	{
		if (gpu && m_particleCount >= (std::uint32_t)GPU_SLOT_COUNT)
		{
			break; // canCreate: a free slot (RW 0x7b0f2f)
		}
		if (m_info.m_particleType == PARTICLE_TYPE_GPU_TERRAINFIRE)
		{
			break; // RW 0x7b13a5 walks the terrain fire cells (stop S-191)
		}
		ParticleInfo info;
		generateParticleInfo(info, i, count);
		if (m_info.m_isEmitAboveGroundOnly)
		{
			const float h = env.groundHeight(info.pos.x, info.pos.y);
			if (info.pos.z + 0.01f < h) // RW 0xbe5600
			{
				continue; // the draws of generateParticleInfo stay consumed
			}
		}
		createParticle(info, priority, attached, false);
	}
}

Coord3D ParticleSystem::computeParticlePosition(int particleNum, int particleCount)
{
	W3DDrawRandom &rng = m_manager.environment().clientRandom();
	const float s = lodScale(m_manager.settings());
	Coord3D out;
	if (!m_volume)
	{
		return Coord3D{ 0.0f, 0.0f, 0.0f };
	}
	switch (m_volume->classId)
	{
	case MODULE_POINT_EMISSION_VOLUME:
		break; // (0,0,0), no draws
	case MODULE_LINE_EMISSION_VOLUME: // RW 0x967e69
	{
		const LineEmissionVolumeModuleData *v = static_cast<const LineEmissionVolumeModuleData *>(m_volume);
		const float t = realDraw(rng, 0.0f, 1.0f);
		out.x = t * (v->m_endPoint.x - v->m_startPoint.x) + v->m_startPoint.x;
		out.y = t * (v->m_endPoint.y - v->m_startPoint.y) + v->m_startPoint.y;
		out.z = t * (v->m_endPoint.z - v->m_startPoint.z) + v->m_startPoint.z;
		break;
	}
	case MODULE_BOX_EMISSION_VOLUME: // RW 0x968152
	{
		const BoxEmissionVolumeModuleData *v = static_cast<const BoxEmissionVolumeModuleData *>(m_volume);
		const float hx = v->m_halfSize.x, hy = v->m_halfSize.y, hz = v->m_halfSize.z;
		if (!v->m_isHollow)
		{
			out.x = realDraw(rng, 0.0f - hx, hx);
			out.y = realDraw(rng, 0.0f - hy, hy);
			out.z = realDraw(rng, 0.0f - hz, hz);
		}
		else
		{
			const int side = rng.value(0, 6);
			switch (side % 3)
			{
			case 0:
				out.x = realDraw(rng, 0.0f - hx, hx);
				out.y = realDraw(rng, 0.0f - hy, hy);
				out.z = (side == 0) ? 0.0f - hz : hz;
				break;
			case 1:
				out.y = realDraw(rng, 0.0f - hy, hy);
				out.z = realDraw(rng, 0.0f - hz, hz);
				out.x = (side == 1) ? 0.0f - hx : hy; // RW reads +hy here (retail typo preserved)
				break;
			default:
				out.x = realDraw(rng, 0.0f - hx, hx);
				out.z = realDraw(rng, 0.0f - hz, hz);
				out.y = (side == 2) ? 0.0f - hy : hy;
				break;
			}
		}
		break;
	}
	case MODULE_SPHERE_EMISSION_VOLUME: // RW 0x96854e
	{
		const SphereEmissionVolumeModuleData *v = static_cast<const SphereEmissionVolumeModuleData *>(m_volume);
		const float radius = v->m_isHollow ? v->m_radius : realDraw(rng, 0.0f, v->m_radius);
		const Coord3D u = fillUnitVector(rng);
		out.x = u.x * radius;
		out.y = u.y * radius;
		out.z = u.z * radius;
		break;
	}
	case MODULE_CYLINDER_EMISSION_VOLUME: // RW 0x9687eb
	{
		const CylinderEmissionVolumeModuleData *v = static_cast<const CylinderEmissionVolumeModuleData *>(m_volume);
		const float a = realDraw(rng, 0.0f, kTwoPi);
		const float radius = v->m_isHollow ? v->m_radius : realDraw(rng, 0.0f, v->m_radius);
		out.x = (float)(std::cos((double)a) * (double)radius) + v->m_offset.x;
		out.y = (float)(std::sin((double)a) * (double)radius) + v->m_offset.y;
		const float half = v->m_length * 0.5f;
		out.z = realDraw(rng, 0.0f - half, half) + v->m_offset.z;
		break;
	}
	case MODULE_TERRAIN_FIRE_EMISSION: // RW 0x9696be
	{
		const TerrainFireEmissionModuleData *v = static_cast<const TerrainFireEmissionModuleData *>(m_volume);
		out.x = v->m_xOffset.getValue(rng);
		out.y = v->m_yOffset.getValue(rng);
		out.z = v->m_zOffset.getValue(rng);
		break;
	}
	case MODULE_LIGHTNING_EMISSION: // RW 0x968e59
	{
		const LightningEmissionModuleData *v = static_cast<const LightningEmissionModuleData *>(m_volume);
		float(&P)[30][3] = m_manager.lightningPoints();
		if (!(1 < particleCount && particleCount < 30))
		{
			// RW returns uninitialised stack words here; zero is the only defensible value
			m_manager.noteUnverified("S-192", "LightningEmission with a burst count outside 2..29 reads uninitialised stack words in RW (RW 0x968e59); the port returns the origin");
			return Coord3D{ 0.0f, 0.0f, 0.0f };
		}
		if (particleNum == 0)
		{
			Coord3D start = v->m_startPoint, end = v->m_endPoint;
			if (m_hasTarget)
			{
				const Matrix3D &M = m_localTransform;
				start = Coord3D{ 0.0f, 0.0f, 0.0f };
				const Coord3D T{ M.Row[0][3], M.Row[1][3], M.Row[2][3] };
				const Coord3D d{ m_target.x - T.x, m_target.y - T.y, m_target.z - T.z };
				end.x = (M.Row[2][0] * d.z + M.Row[1][0] * d.y) + M.Row[0][0] * d.x;
				end.y = (M.Row[2][1] * d.z + M.Row[1][1] * d.y) + M.Row[0][1] * d.x;
				end.z = (M.Row[2][2] * d.z + M.Row[1][2] * d.y) + M.Row[0][2] * d.x;
			}
			const Coord3D delta{ end.x - start.x, end.y - start.y, end.z - start.z };
			float amp[3], freq[3], ph[3];
			for (int k = 0; k < 3; ++k) // Amp1,Freq1,Phase1, Amp2,... in this order
			{
				amp[k] = v->m_amplitude[k].getValue(rng);
				freq[k] = v->m_frequency[k].getValue(rng);
				ph[k] = v->m_phase[k].getValue(rng);
			}
			const Coord3D d = normalized(delta);
			const Coord3D perp{ 0.0f - d.y, d.x, 0.0f };
			const float step = 1.0f / (float)(particleCount - 1);
			float t = 0.0f;
			for (int k = 0; k < particleCount; ++k, t += step)
			{
				const float w = 0.5f - (float)std::fabs((double)(0.5f - t));
				float sn[3];
				for (int j = 0; j < 3; ++j)
				{
					sn[j] = ((float)std::sin((double)(t * freq[j] + ph[j])) * w) * amp[j];
				}
				P[k][0] = (((t * delta.x + start.x) + perp.x * sn[0]) + perp.x * sn[1]) + perp.x * sn[2];
				P[k][1] = (((t * delta.y + start.y) + perp.y * sn[0]) + perp.y * sn[1]) + perp.y * sn[2];
				P[k][2] = (((t * delta.z + start.z) + perp.z * sn[0]) + perp.z * sn[1]) + perp.z * sn[2];
			}
		}
		out = Coord3D{ P[particleNum][0], P[particleNum][1], P[particleNum][2] };
		break;
	}
	default:
		break;
	}
	out.x = out.x * s;
	out.y = out.y * s;
	out.z = out.z * s;
	return out;
}

// RW 0x5f4b88: needs both modules; the volume supplies the Outward interface (RW module +0x18)
Coord3D ParticleSystem::computeParticleVelocity(const Coord3D &pos)
{
	if (!m_velocity || !m_volume)
	{
		return Coord3D{ 0.0f, 0.0f, 0.0f };
	}
	W3DDrawRandom &rng = m_manager.environment().clientRandom();
	const float s = lodScale(m_manager.settings());
	Coord3D v;
	switch (m_velocity->classId)
	{
	case MODULE_ORTHO_EMISSION_VELOCITY: // RW 0x96747a
	{
		const OrthoEmissionVelocityModuleData *m = static_cast<const OrthoEmissionVelocityModuleData *>(m_velocity);
		v.x = m->m_x.getValue(rng);
		v.y = m->m_y.getValue(rng);
		v.z = m->m_z.getValue(rng);
		break;
	}
	case MODULE_SPHERICAL_EMISSION_VELOCITY: // RW 0x9676e9 (Hemispherical runs the same code)
	case MODULE_HEMISPHERICAL_EMISSION_VELOCITY:
	{
		const SphericalEmissionVelocityModuleData *m = static_cast<const SphericalEmissionVelocityModuleData *>(m_velocity);
		const float speed = m->m_speed.getValue(rng);
		const Coord3D u = fillUnitVector(rng);
		v.x = u.x * speed;
		v.y = u.y * speed;
		v.z = u.z * speed;
		break;
	}
	case MODULE_CYLINDRICAL_EMISSION_VELOCITY: // RW 0x96782d
	{
		const CylindricalEmissionVelocityModuleData *m = static_cast<const CylindricalEmissionVelocityModuleData *>(m_velocity);
		const float radial = m->m_radial.getValue(rng);
		const float angle = realDraw(rng, 0.0f, kTwoPi);
		v.x = (float)(std::cos((double)angle) * (double)radial);
		v.y = (float)(std::sin((double)angle) * (double)radial);
		v.z = m->m_normal.getValue(rng);
		break;
	}
	case MODULE_OUTWARD_EMISSION_VELOCITY: // RW 0x9679a2: Speed, OtherSpeed, then the volume's interface draws
	{
		const OutwardEmissionVelocityModuleData *m = static_cast<const OutwardEmissionVelocityModuleData *>(m_velocity);
		const float speed = m->m_speed.getValue(rng);
		const float other = m->m_otherSpeed.getValue(rng);
		switch (m_volume->classId)
		{
		case MODULE_POINT_EMISSION_VOLUME: // RW 0x967b29
		{
			const Coord3D u = fillUnitVector(rng);
			v = Coord3D{ u.x * speed, u.y * speed, u.z * speed };
			break;
		}
		case MODULE_CYLINDER_EMISSION_VOLUME: // RW 0x968730
		{
			const Coord3D n = normalized2D(pos.x, pos.y);
			v = Coord3D{ n.x * speed, n.y * speed, other };
			break;
		}
		case MODULE_LINE_EMISSION_VOLUME: // RW 0x968c76 (shared with Lightning)
		case MODULE_LIGHTNING_EMISSION:
		{
			Coord3D a, b;
			if (m_volume->classId == MODULE_LINE_EMISSION_VOLUME)
			{
				a = static_cast<const LineEmissionVolumeModuleData *>(m_volume)->m_startPoint;
				b = static_cast<const LineEmissionVolumeModuleData *>(m_volume)->m_endPoint;
			}
			else
			{
				a = static_cast<const LightningEmissionModuleData *>(m_volume)->m_startPoint;
				b = static_cast<const LightningEmissionModuleData *>(m_volume)->m_endPoint;
			}
			const Coord3D d = normalized(Coord3D{ b.x - a.x, b.y - a.y, b.z - a.z });
			const Coord3D perp{ 0.0f - d.y, d.x, 0.0f };
			const Coord3D up2{ (0.0f - d.x) * d.z, (0.0f - d.y) * d.z, d.x * d.x + d.y * d.y };
			v = Coord3D{ perp.x * speed + up2.x * other, perp.y * speed + up2.y * other, perp.z * speed + up2.z * other };
			break;
		}
		default: // Box, Sphere, TerrainFire: RW 0x968483
		{
			const Coord3D n = normalized(pos);
			v = Coord3D{ n.x * speed, n.y * speed, n.z * speed };
			break;
		}
		}
		break;
	}
	default:
		return Coord3D{ 0.0f, 0.0f, 0.0f };
	}
	return Coord3D{ (m_velCoeff.x * v.x) * s, (m_velCoeff.y * v.y) * s, (m_velCoeff.z * v.z) * s };
}

// RW 0x5f4c6b
void ParticleSystem::generateParticleInfo(ParticleInfo &info, int particleNum, int particleCount)
{
	if (particleCount == 0)
	{
		return;
	}
	W3DDrawRandom &rng = m_manager.environment().clientRandom();
	info.pos = computeParticlePosition(particleNum, particleCount);
	info.vel = computeParticleVelocity(info.pos);
	if (!m_isIdentity)
	{
		if (m_isFirstPos)
		{
			m_lastPos = m_pos;
			m_isFirstPos = false;
		}
		const float k = 1.0f - (float)particleNum / (float)particleCount;
		const Coord3D adj{ k * (m_pos.x - m_lastPos.x), k * (m_pos.y - m_lastPos.y), k * (m_pos.z - m_lastPos.z) };
		const Matrix3D &M = m_transform;
		const float x = info.pos.x, y = info.pos.y, z = info.pos.z;
		const float px = (((M.Row[0][2] * z + M.Row[0][1] * y) + M.Row[0][0] * x) + M.Row[0][3]) - adj.x;
		const float py = (((M.Row[1][2] * z + M.Row[1][1] * y) + M.Row[1][0] * x) + M.Row[1][3]) - adj.y;
		const float pz = (((M.Row[2][2] * z + M.Row[2][1] * y) + M.Row[2][0] * x) + M.Row[2][3]) - adj.z;
		info.pos = Coord3D{ px, py, pz };
		const float vx = info.vel.x, vy = info.vel.y, vz = info.vel.z;
		info.vel = Coord3D{ (M.Row[0][2] * vz + M.Row[0][1] * vy) + M.Row[0][0] * vx, (M.Row[1][2] * vz + M.Row[1][1] * vy) + M.Row[1][0] * vx,
			(M.Row[2][2] * vz + M.Row[2][1] * vy) + M.Row[2][0] * vx };
	}
	info.lifetime = (std::uint32_t)ftol(m_info.m_lifetime.getValue(rng));
	m_accumulatedSizeBonus += m_info.m_startSizeRate.getValue(rng);
	if (m_accumulatedSizeBonus != 0.0f)
	{
		m_accumulatedSizeBonus = std::min(m_accumulatedSizeBonus, kMaxSizeBonus);
	}
	// RW evaluates ((row sums) * 0.0f) + translation: the translation (NaN-propagating, equal otherwise)
	info.emitterPos = Coord3D{ m_transform.Row[0][3], m_transform.Row[1][3], m_transform.Row[2][3] };
	info.particleUpTowardsEmitter = m_info.m_isParticleUpTowardsEmitter;
}

// RW 0x5fc39a
bool ParticleSystem::createParticle(const ParticleInfo &info, int priority, const ParticleSystemTemplate *attached, bool force)
{
	ParticleSystemManager &mgr = m_manager;
	ParticleSettings &set = mgr.settings();
	if (!force)
	{
		if (!set.useFX)
		{
			return false;
		}
		if (m_info.m_shroudEmitter)
		{
			if (mgr.environment().shroudStatusAt(info.pos) > 1)
			{
				return false;
			}
		}
		if (priority < set.minDynamicParticlePriority)
		{
			return false;
		}
		if (priority < set.minDynamicParticleSkipPriority)
		{
			++set.numParticleGenerations;
			if ((set.numParticleGenerations & set.dynamicParticleSkipMask) != set.dynamicParticleSkipMask)
			{
				return false; // isParticleSkipped (RW 0x5f2d13)
			}
		}
		if (priority != PARTICLE_PRIORITY_ALWAYS_RENDER)
		{
			const int excess = (int)(mgr.particleCount() - set.maxParticleCount);
			if (excess > 0 && mgr.removeOldestParticles((std::uint32_t)excess, priority) != excess)
			{
				return false;
			}
			if (set.maxParticleCount == 0)
			{
				return false;
			}
		}
	}
	const int type = m_info.m_particleType;
	int index = -1;
	if (type == PARTICLE_TYPE_GPU_TERRAINFIRE)
	{
		index = -1; // no particle objects
	}
	else
	{
		index = mgr.allocParticle();
		Particle &p = mgr.m_pool[(size_t)index];
		p.vel = info.vel;
		p.pos = info.pos;
		p.emitterPos = info.emitterPos;
		p.lifetime = info.lifetime;
		p.lifetimeLeft = info.lifetime;
		p.upTowardsEmitter = info.particleUpTowardsEmitter;
		p.createFrame = mgr.environment().clientFrame();
		p.system = this;
		p.systemID = m_id;
		mgr.addParticleToGlobal(index, m_info.m_priority); // RW 0x5f3904
		// the system's list (RW storage addParticle 0x7b08d2 / GPU 0x7b10b4)
		p.sysPrev = m_lastParticle;
		p.sysNext = -1;
		if (m_lastParticle >= 0)
		{
			mgr.m_pool[(size_t)m_lastParticle].sysNext = index;
		}
		else
		{
			m_firstParticle = index;
		}
		m_lastParticle = index;
		++m_particleCount;
		if (type == PARTICLE_TYPE_GPU_PARTICLE)
		{
			Particle &g = mgr.m_pool[(size_t)index];
			g.gpuBirth = gpuNowFrames();
			g.random = realDraw(mgr.environment().clientRandom(), 0.0f, 15.0f); // RW 0x7b119f
			g.gpuExpiry = g.gpuBirth + (float)g.lifetime;
		}
		else
		{
			p.particleID = m_nextParticleID++;
			initParticleModules(mgr.m_pool[(size_t)index]);
		}
	}
	if (attached)
	{
		const ParticleSystemID child = mgr.createParticleSystem(attached, true);
		if (ParticleSystem *c = mgr.findParticleSystemByID(child))
		{
			c->m_controlParticle = index;
			if (index >= 0)
			{
				mgr.m_pool[(size_t)index].controlledSystem = child;
			}
		}
	}
	return true;
}

// ---- per-particle module state constructors (RW 0x5fbb54 chain; draw order Color, Alpha, Update, Physics, Wind, Events) ----
void ParticleSystem::computeColorRate(ColorState &c) const
{
	const int t = c.targetKey;
	// the key array has 8 entries; index 8 reads the words that follow it: {colorScale, r, g, bits(b)} (retail quirk)
	float tr, tg, tb;
	std::uint32_t tframe, prevFrame;
	if (t < MAX_KEYFRAMES)
	{
		tr = c.key[t][0];
		tg = c.key[t][1];
		tb = c.key[t][2];
		tframe = c.keyFrame[t];
	}
	else
	{
		tr = c.colorScale;
		tg = c.color[0];
		tb = c.color[1];
		tframe = floatBits(c.color[2]);
	}
	prevFrame = c.keyFrame[t - 1];
	if (tframe == 0)
	{
		c.rate[0] = c.rate[1] = c.rate[2] = 0.0f;
		return;
	}
	const float d = (float)(std::uint32_t)(tframe - prevFrame);
	const float inv = 1.0f / d;
	c.rate[0] = (tr - c.color[0]) * inv;
	c.rate[1] = (tg - c.color[1]) * inv;
	c.rate[2] = (tb - c.color[2]) * inv;
}

void ParticleSystem::computeAlphaRate(AlphaState &a) const
{
	const int t = a.targetKey;
	float tvalue;
	std::uint32_t tframe;
	if (t < MAX_KEYFRAMES)
	{
		tvalue = a.value[t];
		tframe = a.frame[t];
	}
	else
	{
		tvalue = a.alpha;
		tframe = floatBits(a.rate);
	}
	if (tframe == 0)
	{
		a.rate = 0.0f;
		return;
	}
	a.rate = (tvalue - a.alpha) / (float)(std::uint32_t)(tframe - a.frame[t - 1]);
}

void ParticleSystem::initParticleModules(Particle &p)
{
	W3DDrawRandom &rng = m_manager.environment().clientRandom();
	// 1. Color (RW 0x96a314)
	if (m_color)
	{
		ColorState &c = p.color;
		c.present = true;
		for (int i = 0; i < MAX_KEYFRAMES; ++i)
		{
			c.key[i][0] = m_color->m_colorKey[i].color.red;
			c.key[i][1] = m_color->m_colorKey[i].color.green;
			c.key[i][2] = m_color->m_colorKey[i].color.blue;
			c.keyFrame[i] = m_color->m_colorKey[i].frame;
		}
		// ColorScale is in 0..255 units: the system-level module rescales it and forces UNIFORM (RW 0x96a536)
		GameClientRandomVariable scale(GameClientRandomVariable::UNIFORM, m_color->m_colorScale.m_low * kTo01, m_color->m_colorScale.m_high * kTo01);
		for (int i = 1; i <= 6; ++i)
		{
			if (scale.m_low != 0.0f || scale.m_high != 0.0f)
			{
				for (int ch = 0; ch < 3; ++ch)
				{
					if (c.key[i][ch] != 0.0f)
					{
						c.key[i][ch] = clamp01(c.key[i][ch] + scale.getValue(rng));
					}
				}
			}
		}
		c.colorScale = scale.getValue(rng);
		c.color[0] = c.key[0][0];
		c.color[1] = c.key[0][1];
		c.color[2] = c.key[0][2];
		c.targetKey = 1;
		computeColorRate(c);
	}
	// 2. Alpha (RW 0x969b39)
	if (m_alpha)
	{
		AlphaState &a = p.alpha;
		a.present = true;
		for (int i = 0; i < MAX_KEYFRAMES; ++i)
		{
			a.value[i] = m_alpha->m_alphaKey[i].var.getValue(rng);
			a.frame[i] = m_alpha->m_alphaKey[i].frame;
		}
		a.alpha = a.value[0];
		a.targetKey = 1;
		computeAlphaRate(a);
	}
	// 3. Update (RW 0x96b2ec / 0x966227)
	const float S = m_manager.settings().particleScale;
	const float K = m_sizeCoeff;
	if (m_updateDefault)
	{
		UpdateState &u = p.update;
		u.present = true;
		u.isRenderObject = false;
		u.size[0] = (m_info.m_size.getValue(rng) * S) * K;
		u.sizeRate[0] = (m_updateDefault->m_sizeRate.getValue(rng) * S) * K;
		u.sizeDamping[0] = m_updateDefault->m_sizeRateDamping.getValue(rng);
		u.size[0] += m_accumulatedSizeBonus;
		u.size[1] = u.size[2] = u.size[0];
		u.sizeRate[1] = u.sizeRate[2] = u.sizeRate[0];
		u.sizeDamping[1] = u.sizeDamping[2] = u.sizeDamping[0];
		u.angleZ = m_updateDefault->m_angleZ.getValue(rng);
		u.angularRateZ = m_updateDefault->m_angularRateZ.getValue(rng);
		u.angularDamping = m_updateDefault->m_angularDamping.getValue(rng);
		u.rotation = m_updateDefault->m_rotation;
		u.angleXY = m_updateDefault->m_angleXY.getValue(rng);
		u.angularRateXY = m_updateDefault->m_angularRateXY.getValue(rng);
		u.angularDampingXY = m_updateDefault->m_angularDampingXY.getValue(rng);
	}
	else if (m_updateRenderObject)
	{
		const RenderObjectUpdateModuleData *m = m_updateRenderObject;
		UpdateState &u = p.update;
		u.present = true;
		u.isRenderObject = true;
		u.size[0] = m->m_startSizeX.getValue(rng);
		u.size[1] = m->m_startSizeY.getValue(rng) * m_sizeCoeffY;
		u.size[2] = m->m_startSizeZ.getValue(rng);
		u.sizeRate[0] = m->m_sizeRateX.getValue(rng);
		u.sizeRate[1] = m->m_sizeRateY.getValue(rng);
		u.sizeRate[2] = m->m_sizeRateZ.getValue(rng);
		u.sizeDamping[0] = m->m_sizeDampingX.getValue(rng);
		u.sizeDamping[1] = m->m_sizeDampingY.getValue(rng);
		u.sizeDamping[2] = m->m_sizeDampingZ.getValue(rng);
		u.size[0] += m_accumulatedSizeBonus;
		u.size[1] += m_accumulatedSizeBonus;
		u.size[2] += m_accumulatedSizeBonus;
		u.angleZ = m->m_angleZ.getValue(rng);
		u.angularRateZ = m->m_angularRateZ.getValue(rng);
		u.angularDamping = m->m_angularDamping.getValue(rng);
		u.rotation = m->m_rotation;
	}
	// 4. Physics (RW 0x96acea)
	if (m_physics)
	{
		p.physics.present = true;
		p.physics.velocityDamping = m_physics->m_velocityDamping.getValue(rng);
		p.physics.swirly = m_physics->m_swirly;
		p.physics.attachToBone = m_physics->m_particlesAttachToBone;
	}
	// 5. Wind (RW 0x966d80)
	if (m_wind)
	{
		p.wind.present = true;
		p.wind.windRandomness = realDraw(rng, 0.7f, 1.3f);
	}
	// 6. Events: only PerParticle modules get a per-particle instance, in vector order (RW 0x5f85f5)
	const std::vector<std::shared_ptr<ModuleData>> &events = m_template->events();
	for (size_t i = 0; i < events.size(); ++i)
	{
		const EventModuleData *e = static_cast<const EventModuleData *>(events[i].get());
		if (!e->m_perParticle)
		{
			continue;
		}
		EventParticleState st;
		st.moduleIndex = (int)i;
		st.kill = e->m_killAfterEvent;
		st.armed = true;
		if (e->classId == MODULE_LIFE_EVENT)
		{
			st.eventTime = (std::uint32_t)ftol(static_cast<const LifeEventModuleData *>(e)->m_eventTime.getValue(rng));
		}
		else
		{
			const TerrainCollisionModuleData *tc = static_cast<const TerrainCollisionModuleData *>(e);
			st.heightOffset = tc->m_heightOffset.getValue(rng);
			st.orient = tc->m_orientFXToTerrain;
		}
		p.events.push_back(st);
	}
}

// ---- Particle::update (RW 0x5fa0e6) -----------------------------------------------------------------------------------
void ParticleSystem::updateColor(Particle &p)
{
	ColorState &c = p.color;
	c.color[0] += c.rate[0];
	c.color[1] += c.rate[1];
	c.color[2] += c.rate[2];
	const int t = c.targetKey;
	if (t < MAX_KEYFRAMES && c.keyFrame[t] != 0)
	{
		const std::uint32_t age = p.lifetime - p.lifetimeLeft;
		if (age >= c.keyFrame[t])
		{
			c.targetKey = t + 1; // the colour is NOT snapped to the key colour (RW 0x96a0f9)
			computeColorRate(c);
		}
	}
	else
	{
		c.rate[0] = c.rate[1] = c.rate[2] = 0.0f;
	}
	for (int i = 0; i < 3; ++i)
	{
		c.color[i] = clamp01(c.color[i]);
	}
}

void ParticleSystem::updateAlpha(Particle &p)
{
	AlphaState &a = p.alpha;
	const int shader = m_info.m_shaderType;
	if (shader == PARTICLE_SHADER_ADDITIVE || shader == PARTICLE_SHADER_ADDITIVE_ALPHA_TEST)
	{
		return; // alpha is not updated (RW 0x969a94)
	}
	a.alpha += a.rate;
	const int t = a.targetKey;
	if (t < MAX_KEYFRAMES && a.frame[t] != 0)
	{
		const std::uint32_t age = p.lifetime - p.lifetimeLeft;
		if (age >= a.frame[t])
		{
			a.alpha = a.value[t]; // alpha snaps to the key value
			a.targetKey = t + 1;
			computeAlphaRate(a);
		}
	}
	else
	{
		a.rate = 0.0f;
	}
	a.alpha = clamp01(a.alpha);
}

void ParticleSystem::updatePhysics(Particle &p)
{
	ParticleEnvironment &env = m_manager.environment();
	if (p.physics.attachToBone)
	{
		p.pos = Coord3D{ m_transform.Row[0][3], m_transform.Row[1][3], m_transform.Row[2][3] }; // RW 0x5f2eee
		return;
	}
	p.accel.z = m_physics ? m_physics->m_gravity : 0.0f; // OVERWRITES accel.z (RW 0x96aad2)
	p.vel.x = p.vel.x + p.accel.x;
	p.vel.y = p.vel.y + p.accel.y;
	p.vel.z = p.vel.z + p.accel.z;
	const float damp = p.physics.velocityDamping;
	p.vel.x = damp * p.vel.x;
	p.vel.y = p.vel.y * damp;
	p.vel.z = damp * p.vel.z;
	if (m_physics)
	{
		const Coord3D &drift = m_physics->m_driftVelocity;
		p.pos.x = (drift.x + p.vel.x) + p.pos.x;
		p.pos.y = (drift.y + p.vel.y) + p.pos.y;
		p.pos.z = (drift.z + p.vel.z) + p.pos.z;
		if (p.physics.swirly && m_attachedObjectID != 0)
		{
			const ParticleAttachInfo inf = env.swirlInfluence(m_attachedObjectID, p.pos);
			if (!inf.hasInfluence)
			{
				m_manager.noteUnverified("S-193", "Swirly physics needs the attached object's influence control points (RW 0x5f2ebc, obj +0x45c); the environment returned none");
			}
			if (inf.hasInfluence)
			{
				const float s = approxLength3D(p.vel) * 4.0f;
				p.pos.x = p.pos.x + inf.swirlTangent.x * s;
				p.pos.y = p.pos.y + inf.swirlTangent.y * s;
				p.pos.z = p.pos.z + inf.swirlTangent.z * s;
			}
		}
	}
	p.accel = Coord3D{ 0.0f, 0.0f, 0.0f };
}

void ParticleSystem::updateWind(Particle &p)
{
	ParticleEnvironment &env = m_manager.environment();
	const int mode = m_wind ? m_windState.mode : 0;
	if (mode == WIND_MOTION_NOT_USED)
	{
		return;
	}
	const float angle = m_wind ? m_windState.angle : 0.0f;
	Coord3D sysPos{ m_transform.Row[0][3], m_transform.Row[1][3], m_transform.Row[2][3] };
	// RW adds the attached entity's position on top of the (already attached) transform translation
	if (m_attachedDrawableID != 0)
	{
		const ParticleAttachInfo d = env.attachedDrawable(m_attachedDrawableID, std::string());
		if (d.found)
		{
			sysPos.x += d.position.x;
			sysPos.y += d.position.y;
			sysPos.z += d.position.z;
		}
	}
	else if (m_attachedObjectID != 0)
	{
		const ParticleAttachInfo o = env.attachedObject(m_attachedObjectID, std::string(), 0);
		if (o.found)
		{
			sysPos.x += o.position.x;
			sysPos.y += o.position.y;
			sysPos.z += o.position.z;
		}
	}
	const Coord3D v{ p.pos.x - sysPos.x, p.pos.y - sysPos.y, p.pos.z - sysPos.z };
	const float distSq = (v.z * v.z + v.y * v.y) + v.x * v.x;
	const float zero = m_wind ? m_wind->m_windZeroStrengthDist : 0.0f;
	if (zero * zero > distSq)
	{
		float strength = (m_wind ? m_wind->m_windStrength : 0.0f) * p.wind.windRandomness;
		const float full = zero; // retail bug: WindFullStrengthDist is never read (RW 0x966dff calls the zero-distance getter twice)
		if (distSq > full * full)
		{
			strength *= 1.0f - ((float)std::sqrt((double)distSq) - full) / (zero - full); // unreachable
		}
		p.pos.x = p.pos.x + (float)std::cos((double)angle) * strength;
		p.pos.y = p.pos.y + (float)std::sin((double)angle) * strength;
		if (m_wind && m_wind->m_turbulenceAmplitude > 0.0f)
		{
			const float phase = p.pos.z * m_wind->m_turbulenceFrequency + (float)0; // Particle +0x88 is never written
			const float t = (p.update.present ? p.update.size[0] : 0.0f) * m_wind->m_turbulenceAmplitude;
			p.pos.x = p.pos.x + (float)std::cos((double)phase) * t;
			p.pos.x = p.pos.x + (float)std::sin((double)phase) * t; // both terms add to x in retail
		}
	}
}

void ParticleSystem::updateParticleEvents(Particle &p)
{
	ParticleEnvironment &env = m_manager.environment();
	const std::vector<std::shared_ptr<ModuleData>> &events = m_template->events();
	for (EventParticleState &st : p.events)
	{
		const EventModuleData *e = static_cast<const EventModuleData *>(events[(size_t)st.moduleIndex].get());
		if (!st.armed || e->m_eventFX.empty() || !env.hasFXList(e->m_eventFX))
		{
			continue;
		}
		if (e->classId == MODULE_LIFE_EVENT)
		{
			const std::uint32_t age = env.clientFrame() - p.createFrame;
			if (age >= st.eventTime)
			{
				env.playFXList(e->m_eventFX, p.pos, nullptr);
				st.armed = false;
				if (st.kill)
				{
					p.lifetimeLeft = 1;
				}
			}
		}
		else
		{
			const float gz = env.groundHeight(p.pos.x, p.pos.y);
			if (!(gz < p.pos.z))
			{
				Matrix3D M;
				if (st.orient)
				{
					const float a = (float)std::atan2((double)p.vel.y, (double)p.vel.x);
					const float c = (float)std::cos((double)a), s = (float)std::sin((double)a);
					M.Row[0][0] = c;
					M.Row[0][1] = -s;
					M.Row[1][0] = s;
					M.Row[1][1] = c;
				}
				const Coord3D pos{ p.pos.x, p.pos.y, p.pos.z + st.heightOffset };
				env.playFXList(e->m_eventFX, pos, &M);
				st.armed = false;
				if (st.kill)
				{
					p.lifetimeLeft = 1;
				}
			}
		}
	}
}

// RW 0x5f449f
bool ParticleSystem::isParticleInvisible(const Particle &p) const
{
	const int shader = m_info.m_shaderType;
	if (shader <= 0 || shader >= 8)
	{
		return true;
	}
	if (shader == PARTICLE_SHADER_ADDITIVE || shader == PARTICLE_SHADER_ADDITIVE_ALPHA_TEST || shader == PARTICLE_SHADER_MULTIPLY || shader == PARTICLE_SHADER_ADDITIVE_NO_DEPTH_TEST)
	{
		if (!p.color.present)
		{
			return false;
		}
		const ColorState &c = p.color;
		const std::uint32_t frame = c.targetKey < MAX_KEYFRAMES ? c.keyFrame[c.targetKey] : floatBits(c.color[2]);
		if (shader == PARTICLE_SHADER_MULTIPLY)
		{
			if (frame != 0)
			{
				return false;
			}
			return (c.color[2] * c.color[1]) * c.color[0] > 0.97f; // RW 0xc83be0
		}
		if (frame != 0)
		{
			return false;
		}
		return (c.color[0] + c.color[1] + c.color[2]) < 0.03f; // RW 0xbdc540
	}
	// ALPHA, ALPHA_TEST, ALPHA_NO_DEPTH_TEST
	if (!p.alpha.present)
	{
		return false;
	}
	if (shader == PARTICLE_SHADER_ALPHA_TEST)
	{
		return false;
	}
	const AlphaState &a = p.alpha;
	const std::uint32_t frame = a.targetKey < MAX_KEYFRAMES ? a.frame[a.targetKey] : floatBits(a.rate);
	if (frame != 0)
	{
		return false;
	}
	return a.alpha < 0.01f; // RW 0xbe5600
}

bool ParticleSystem::updateParticle(int index)
{
	Particle &p = m_manager.m_pool[(size_t)index];
	// the per-particle module chain: Color, Alpha, Update, Physics, Wind, Events
	if (p.color.present)
	{
		updateColor(p);
	}
	if (p.alpha.present)
	{
		updateAlpha(p);
	}
	if (p.update.present)
	{
		UpdateState &u = p.update;
		if (u.isRenderObject)
		{
			// RW 0x965da7
			u.size[0] += u.sizeRate[0];
			u.size[1] += u.sizeRate[1];
			u.size[2] += u.sizeRate[2];
			u.sizeRate[0] = u.sizeDamping[0] * u.sizeRate[0];
			u.sizeRate[1] = u.sizeDamping[1] * u.sizeRate[1];
			u.sizeRate[2] = u.sizeDamping[2] * u.sizeRate[2];
			u.angleZ += u.angularRateZ;
			u.angularRateZ = u.angularDamping * u.angularRateZ;
		}
		else
		{
			// RW 0x96af99
			u.size[0] += u.sizeRate[0];
			u.sizeRate[0] = u.sizeDamping[0] * u.sizeRate[0];
			u.angleZ += u.angularRateZ;
			u.angularRateZ = u.angularDamping * u.angularRateZ;
			u.angleXY += u.angularRateXY;
			u.angularRateXY = u.angularDampingXY * u.angularRateXY;
		}
	}
	if (p.physics.present)
	{
		updatePhysics(p);
	}
	else
	{
		// no Physics module: the chain slot is empty; velocity is not integrated at all
	}
	if (p.wind.present)
	{
		updateWind(p);
	}
	updateParticleEvents(p);

	if (p.upTowardsEmitter && p.update.present)
	{
		const float dy = p.pos.y - p.emitterPos.y;
		const float dx = p.pos.x - p.emitterPos.x;
		float angle;
		if (-kEpsilon < dy && dy < kEpsilon)
		{
			angle = (dx > 0.0f) ? kTwoPi : kPi;
		}
		else
		{
			const float len = (float)std::sqrt((double)(dx * dx + dy * dy));
			if (len < kEpsilon)
			{
				angle = kPi;
			}
			else
			{
				const float a = (float)std::acos((double)(dy / len));
				angle = (dx > 0.0f) ? a + kPi : kPi - a;
			}
		}
		p.update.angleZ = angle;
	}
	if (p.lifetimeLeft != 0)
	{
		if (--p.lifetimeLeft == 0)
		{
			return false;
		}
	}
	return !isParticleInvisible(p);
}

// ============================================================================================================================
// ParticleSystemManager
// ============================================================================================================================
ParticleSystemManager::ParticleSystemManager(const FXParticleSystemTemplateStore &templates, ParticleEnvironment &env) : m_templates(templates), m_env(env)
{
	for (int i = 0; i < 7; ++i)
	{
		m_head[i] = m_tail[i] = -1;
	}
	std::memset(m_lightning, 0, sizeof(m_lightning));
}

void ParticleSystemManager::noteUnverified(const char *code, const char *text)
{
	for (const auto &u : m_unverified)
	{
		if (u.first == code)
		{
			return;
		}
	}
	m_unverified.emplace_back(code, text);
}

std::vector<std::string> ParticleSystemManager::unverified() const
{
	std::vector<std::string> out;
	for (const auto &u : m_unverified)
	{
		out.push_back(u.first + ": " + u.second);
	}
	return out;
}

ParticleSystemManager::~ParticleSystemManager()
{
	reset();
}

int ParticleSystemManager::allocParticle()
{
	int index;
	if (!m_free.empty())
	{
		index = m_free.back();
		m_free.pop_back();
		m_pool[(size_t)index] = Particle();
	}
	else
	{
		index = (int)m_pool.size();
		m_pool.emplace_back();
	}
	m_pool[(size_t)index].inUse = true;
	return index;
}

void ParticleSystemManager::freeParticle(int index)
{
	m_pool[(size_t)index].inUse = false;
	m_pool[(size_t)index].events.clear();
	m_free.push_back(index);
}

// RW 0x5f3904: append to the tail of the priority list
void ParticleSystemManager::addParticleToGlobal(int index, int priority)
{
	const int prio = std::max(0, std::min(6, priority));
	Particle &p = m_pool[(size_t)index];
	p.globPrev = m_tail[prio];
	p.globNext = -1;
	if (m_tail[prio] >= 0)
	{
		m_pool[(size_t)m_tail[prio]].globNext = index;
	}
	else
	{
		m_head[prio] = index;
	}
	m_tail[prio] = index;
	++m_particleCount;
}

// RW 0x5f3ebb
void ParticleSystemManager::removeParticleFromGlobal(int index)
{
	Particle &p = m_pool[(size_t)index];
	const int prio = p.system ? std::max(0, std::min(6, p.system->priority())) : 0;
	if (p.globPrev >= 0)
	{
		m_pool[(size_t)p.globPrev].globNext = p.globNext;
	}
	else
	{
		m_head[prio] = p.globNext;
	}
	if (p.globNext >= 0)
	{
		m_pool[(size_t)p.globNext].globPrev = p.globPrev;
	}
	else
	{
		m_tail[prio] = p.globPrev;
	}
	--m_particleCount;
}

// RW Particle dtor 0x5f42d9
void ParticleSystemManager::deleteParticle(int index)
{
	Particle &p = m_pool[(size_t)index];
	ParticleSystem *sys = p.system;
	if (sys)
	{
		if (p.sysPrev >= 0)
		{
			m_pool[(size_t)p.sysPrev].sysNext = p.sysNext;
		}
		else
		{
			sys->m_firstParticle = p.sysNext;
		}
		if (p.sysNext >= 0)
		{
			m_pool[(size_t)p.sysNext].sysPrev = p.sysPrev;
		}
		else
		{
			sys->m_lastParticle = p.sysPrev;
		}
		--sys->m_particleCount;
	}
	if (p.controlledSystem != INVALID_PARTICLE_SYSTEM_ID)
	{
		if (ParticleSystem *child = findParticleSystemByID(p.controlledSystem))
		{
			child->m_controlParticle = -1;
			child->destroy();
		}
	}
	removeParticleFromGlobal(index);
	freeParticle(index);
}

ParticleSystemID ParticleSystemManager::createParticleSystem(const ParticleSystemTemplate *tmpl, bool createSlaves)
{
	if (!tmpl)
	{
		return INVALID_PARTICLE_SYSTEM_ID;
	}
	const ParticleSystemID id = ++m_uniqueSystemID;
	std::unique_ptr<ParticleSystem> sys(new ParticleSystem(*this, tmpl, id));
	(void)createSlaves; // the slave chain is built by the constructor (the flag is always true for the callers ported so far)
	ParticleSystem *raw = sys.get();
	m_byID[id] = raw;
	m_systems.push_back(std::move(sys)); // RW 0x5f806c: appended at the end of the constructor (slaves therefore precede masters)
	return id;
}

ParticleSystemID ParticleSystemManager::createAttachedParticleSystemID(const ParticleSystemTemplate *tmpl, std::uint32_t objectId, bool createSlaves)
{
	const ParticleSystemID id = createParticleSystem(tmpl, createSlaves);
	if (ParticleSystem *s = findParticleSystemByID(id))
	{
		s->attachToObject(objectId);
	}
	return id;
}

ParticleSystem *ParticleSystemManager::findParticleSystemByID(ParticleSystemID id)
{
	auto it = m_byID.find(id);
	return it == m_byID.end() ? nullptr : it->second;
}

const ParticleSystem *ParticleSystemManager::findParticleSystemByID(ParticleSystemID id) const
{
	auto it = m_byID.find(id);
	return it == m_byID.end() ? nullptr : it->second;
}

void ParticleSystemManager::destroyParticleSystemByID(ParticleSystemID id)
{
	if (ParticleSystem *s = findParticleSystemByID(id))
	{
		s->destroy();
	}
}

void ParticleSystemManager::destroyAttachedSystems(std::uint32_t objectId)
{
	for (const std::unique_ptr<ParticleSystem> &s : m_systems)
	{
		if (s->m_attachedObjectID == objectId)
		{
			s->destroy();
		}
	}
}

// RW 0x5fb4a2: the destructor deletes every particle, clears the master/slave links
void ParticleSystemManager::deleteSystem(ParticleSystem *sys)
{
	while (sys->m_firstParticle >= 0)
	{
		deleteParticle(sys->m_firstParticle);
	}
	if (sys->m_masterID != INVALID_PARTICLE_SYSTEM_ID)
	{
		if (ParticleSystem *m = findParticleSystemByID(sys->m_masterID))
		{
			m->m_slaveID = INVALID_PARTICLE_SYSTEM_ID;
		}
	}
	if (sys->m_slaveID != INVALID_PARTICLE_SYSTEM_ID)
	{
		if (ParticleSystem *s = findParticleSystemByID(sys->m_slaveID))
		{
			s->m_masterID = INVALID_PARTICLE_SYSTEM_ID;
		}
	}
	// a controlled child keeps no pointer to this system's particles any more (they were deleted above)
	m_byID.erase(sys->m_id);
}

// RW 0x5f5123
void ParticleSystemManager::update(bool gameRunning, int localPlayerIndex)
{
	for (auto it = m_systems.begin(); it != m_systems.end();)
	{
		ParticleSystem *sys = it->get();
		auto next = std::next(it);
		if (sys->m_updateWhilePaused || gameRunning)
		{
			if (!sys->update(localPlayerIndex))
			{
				deleteSystem(sys);
				m_systems.erase(it);
			}
		}
		it = next;
	}
}

void ParticleSystemManager::reset()
{
	while (!m_systems.empty())
	{
		ParticleSystem *sys = m_systems.front().get();
		deleteSystem(sys);
		m_systems.pop_front();
	}
	for (int i = 0; i < 7; ++i)
	{
		m_head[i] = m_tail[i] = -1;
	}
	m_pool.clear();
	m_free.clear();
	m_byID.clear();
	m_particleCount = 0;
	m_uniqueSystemID = 0;
}

// RW 0x5f3f18, including its return-value quirk (n + 1 when all n were removed)
int ParticleSystemManager::removeOldestParticles(std::uint32_t count, int priorityCap)
{
	const std::uint32_t orig = count;
	if (count == 0)
	{
		count = (std::uint32_t)-1;
		return (int)(orig - count);
	}
	bool brokeOut = false;
	do
	{
		--count;
		if (m_particleCount == 0)
		{
			brokeOut = true; // jumps past the final extra decrement
			break;
		}
		for (int i = 1; i < priorityCap && i < 7; ++i)
		{
			const int head = m_head[i];
			if (head >= 0)
			{
				const ParticleSystem *sys = m_pool[(size_t)head].system;
				const int type = sys ? sys->particleType() : 0;
				if (type != PARTICLE_TYPE_GPU_PARTICLE && type != PARTICLE_TYPE_GPU_TERRAINFIRE)
				{
					deleteParticle(head);
					break;
				}
			}
		}
	} while (count != 0);
	if (!brokeOut)
	{
		--count; // RW 0x5f3f83: count becomes 0xFFFFFFFF
	}
	return (int)(orig - count);
}

} // namespace FXParticleSystem
