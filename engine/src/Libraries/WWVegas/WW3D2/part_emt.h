// OpenBFME. GPL-3.0.
//
// W3D model emitters: ParticleEmitterClass and ParticleBufferClass of WW3D2 (ZH part_emt.cpp / part_buf.cpp, WWLib random.cpp, WWMath
// v3_rnd.cpp / quat.cpp) as RotWK runs them, merged into one simulation object per emitter instance. Lane FX-1, spec step 19.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; derivation in the lane notes fx-w3d-emitters.md, sections 3-10; every RW address below
// was matched against the BFME2 export names with a masked byte pattern and the bodies are identical to the ZH source apart from the
// numeric quirks listed):
//   * time: the system advances in integer milliseconds, `SyncTime += 33 * (client frames since the last draw)` (WW3D::Sync RW 0x516e20,
//     W3DDisplay::draw 0x44b84b; TheW3DFrameLengthInMsec = 1000 / 30 = 33). Emission math is integer ms; the emitter never reads a clock.
//   * two private client-side random streams, never the game's: `Vector3Randomizer::Randomizer`, a Random3Class(0, 0) (RW 0xe02dc8) shared
//     by every creation-volume / velocity randomiser, and the Mersenne twister `rand_gen` seeded 4357 (RW 0xde2930) used only when a buffer is
//     constructed (the per-property random tables). The tables therefore depend on how many buffers were created before; W3DEmitterWorld owns
//     both streams so a test can start a fresh process state.
//   * float32 rounding after every operation (SSE and x87 at PC_24 behave alike); no FMA. The documented quirks are kept: velocity-randomiser
//     extents are scaled to per-millisecond, acceleration is multiplied by 1e-6f, key times are `(uint)(t * 1000.0f)`, colour deltas use
//     reciprocal-multiply, the colour table draws Y, X, Z, Cached_Slerp divides by Theta.
//   * particles are always born in world space; only the emitter transform follows its bone (no local-space mode exists in RW).
//   * the buffer is a ring of MaxNum slots; overflow overwrites the oldest particle.

#pragma once

#include "Libraries/WWVegas/WW3D2/part_ldr.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ---- random streams -------------------------------------------------------------------------------------------------
// WWLib random.cpp Random3Class (RW 0xa2cff0): a hash of (Seed, Index++).
class Random3Class
{
public:
	Random3Class(std::uint32_t seed1 = 0, std::uint32_t seed2 = 0) : Seed(seed1), Index(seed2) {}
	int operator()();

private:
	std::uint32_t Seed;
	std::uint32_t Index;
};

// WWLib random.cpp Random4Class (RW 0xa2d0f0 / 0xa2d140): the 624-word Mersenne twister with the old Knuth seeding.
class Random4Class
{
public:
	explicit Random4Class(std::uint32_t seed = 4375);
	int operator()();

private:
	std::uint32_t mt[624];
	int mti;
};

// WWMath wwmath.h Inv_Sqrt (RW 0x441c56): Intel's fast inverse square root with three Newton steps.
float WWMath_Inv_Sqrt(float a);

// ---- quaternions ----------------------------------------------------------------------------------------------------
struct W3DQuaternion
{
	float X = 0.0f, Y = 0.0f, Z = 0.0f, W = 1.0f;
};
W3DQuaternion Build_Quaternion(const Matrix3D &m); // ZH quat.cpp:663 (RW 0xb2bd10)
Vector3 Rotate_Vector(const W3DQuaternion &q, const Vector3 &v); // ZH quat.h:246

// ---- the world the emitters live in -----------------------------------------------------------------------------------
class ParticleEmitterInstance;

class W3DEmitterWorld
{
public:
	W3DEmitterWorld();
	~W3DEmitterWorld();

	// WW3D::Sync (RW 0x516e20): PreviousSyncTime = SyncTime; SyncTime = t. The caller passes 33 * the client frame.
	void sync(std::uint32_t syncTimeMs)
	{
		m_prevSync = m_sync;
		m_sync = syncTimeMs;
	}
	std::uint32_t syncTime() const { return m_sync; }
	std::uint32_t previousSyncTime() const { return m_prevSync; }
	static const std::uint32_t kFrameLengthMs = 33; // TheW3DFrameLengthInMsec (RW 0xdc7a8c, 1000 / LOGICFRAMES_PER_SECOND 30)

	Random3Class &randomizer() { return m_random3; }
	Random4Class &randGen() { return m_randGen; }
	// RW globals 0xde2918..: the emitter velocity inherited by new particles (set per Create_New_Particles)
	Vector3 inheritedWorldSpaceEmitterVel;

	// Creates an emitter + its buffer from a definition (Create_From_Definition RW 0x5a19e0). The texture name is taken from the definition.
	ParticleEmitterInstance *create(const ParticleEmitterDefClass &def, const Matrix3D &transform);
	void remove(ParticleEmitterInstance *e);
	// Scene pass: for every emitter, the buffer's kinematic update (the culling pass), then On_Frame_Update (Emit) (RW 0x5a5e60).
	void update();

	const std::vector<std::unique_ptr<ParticleEmitterInstance>> &emitters() const { return m_emitters; }
	void clear() { m_emitters.clear(); }

private:
	std::uint32_t m_sync = 0, m_prevSync = 0;
	Random3Class m_random3{ 0, 0 };
	Random4Class m_randGen{ 4357 };
	std::vector<std::unique_ptr<ParticleEmitterInstance>> m_emitters;
};

// ---- creation-volume randomisers (WWMath v3_rnd.cpp) -------------------------------------------------------------------
class Vector3Randomizer
{
public:
	virtual ~Vector3Randomizer() = default;
	virtual void Get_Vector(Random3Class &rng, Vector3 &out) = 0;
	virtual void Scale(float scale) = 0;
	virtual std::unique_ptr<Vector3Randomizer> Clone() const = 0;
	static float Get_Random_Float_Minus1_To_1(Random3Class &rng);
};

// One particle property of the buffer with its key-frame table (Reset_Colors / Reset_Opacity / Reset_Size / Reset_Frames).
struct W3DPropertyTable
{
	bool constant = true;                  ///< no per-particle array: only values[0]
	std::vector<std::uint32_t> times;      ///< ms, times[0] = 0
	std::vector<float> values;             ///< scalar
	std::vector<float> deltas;
	std::vector<Vector3> cvalues;          ///< colour values
	std::vector<Vector3> cdeltas;
	std::vector<float> random;             ///< random table (size 1 or POT <= 32)
	std::vector<Vector3> crandom;
	unsigned randomMask = 0;
};

// One emitter and its buffer. The sync-time driven simulation of ParticleEmitterClass + ParticleBufferClass.
class ParticleEmitterInstance
{
public:
	ParticleEmitterInstance(W3DEmitterWorld &world, const ParticleEmitterDefClass &def, const Matrix3D &transform);

	const std::string &name() const { return m_name; }
	const std::string &textureName() const { return m_texture; }
	const W3dShaderStruct &shader() const { return m_shader; }
	unsigned renderMode() const { return m_renderMode; }
	unsigned frameMode() const { return m_frameMode; }

	// ParticleEmitterClass::Set_Transform: the bone world matrix every frame
	void setTransform(const Matrix3D &m) { m_transform = m; }
	const Matrix3D &transform() const { return m_transform; }
	// Update_On_Visibilty / Start (RW 0x5a1950 / 0x5a1210): bone or animation visibility starts and stops emission
	void setHidden(bool hidden);
	bool isActive() const { return m_active; }
	bool isComplete() const;

	// Emit (RW 0x5a2060) after the buffer's kinematic update (RW 0x5ae3f0): one scene frame.
	void onFrameUpdate();
	void updateKinematic();

	// Update_Visual_Particle_State (RW 0x5a9e40) at the current sync time, then the particles in ring order (oldest first).
	struct Visual
	{
		Vector3 position;
		Vector3 color;   ///< clamped 0..1 (Combine_Color_And_Alpha)
		float alpha;     ///< clamped 0..1
		float size;
		std::uint8_t orientation;
		std::uint8_t frame;
	};
	void collectVisuals(std::vector<Visual> &out);
	unsigned particleCount() const { return (unsigned)(m_nonNewNum + m_newNum); }
	unsigned maxParticles() const { return m_maxNum; }
	std::uint32_t emitRateMs() const { return m_emitRate; }
	// The period an extended-precision (PC64) division would give; differs from emitRateMs() only for rates such as E_MumaFlies' 83.3333359 (stop S-196).
	std::uint32_t emitRateExtendedMs() const { return m_emitRateExtended; }
	bool emitPeriodPrecisionSensitive() const { return m_emitRateExtended != m_emitRate; }
	std::uint32_t maxAgeMs() const { return m_maxAge; }

	// test access to the derived tables
	const W3DPropertyTable &colorTable() const { return m_color; }
	const W3DPropertyTable &alphaTable() const { return m_alpha; }
	const W3DPropertyTable &sizeTable() const { return m_size; }
	const W3DPropertyTable &frameTable() const { return m_frame; }
	Vector3 baseVelocity() const { return m_baseVel; }
	Vector3 acceleration() const { return m_accel; }
	// the raw particle slots, for tests
	Vector3 slotPosition(unsigned slot) const { return m_position[slot]; }
	Vector3 slotVelocity(unsigned slot) const { return m_velocity[slot]; }
	std::uint32_t slotTimeStamp(unsigned slot) const { return m_timeStamp[slot]; }
	unsigned ringStart() const { return m_start; }
	unsigned ringEnd() const { return m_end; }
	std::uint32_t emitRemain() const { return m_emitRemain; }

private:
	struct NewParticle
	{
		Vector3 pos, vel;
		std::uint32_t ts;
		std::uint16_t groupId;
	};
	void createNewParticles(const W3DQuaternion &q, const Vector3 &orig);
	void initializeParticle(NewParticle &np, std::uint32_t ts, const W3DQuaternion &q, const Vector3 &orig);
	NewParticle &addUninitializedNewParticle();
	void getNewParticles();
	void killOldParticles();
	void updateNonNewParticles(std::uint32_t elapsed);
	void resetColors(const ParticleEmitterDefClass &def);
	void resetScalar(W3DPropertyTable &t, const std::vector<float> &keyTimesSec, const std::vector<float> &values, float start, float rand, float eps, bool isSize);
	void resetRotations(const ParticleEmitterDefClass &def);
	void buildScalarTables(const ParticleEmitterDefClass &def);

	W3DEmitterWorld &m_world;
	std::string m_name, m_texture;
	W3dShaderStruct m_shader{};
	unsigned m_renderMode = 0, m_frameMode = 0;
	Matrix3D m_transform;

	// emitter (RW object offsets in the comments)
	std::uint32_t m_emitRate = 1000;       // +0xc4 ms between bursts
	std::uint32_t m_emitRateExtended = 1000; // the same under extended precision (not used for emission)
	unsigned m_burstSize = 1;              // +0xc8
	unsigned m_oneTimeBurstSize = 1;       // +0xcc
	bool m_oneTimeBurst = false;           // +0xd0
	std::unique_ptr<Vector3Randomizer> m_posRand, m_velRand; // +0xd4, +0xe4
	Vector3 m_baseVel;                     // +0xd8 (per ms)
	float m_outwardVel = 0.0f;             // +0xe8 (per ms)
	float m_velInherit = 0.0f;             // +0xec
	std::uint32_t m_emitRemain = 0;        // +0xf0
	W3DQuaternion m_prevQ;                 // +0xf4
	Vector3 m_prevOrig;                    // +0x104
	bool m_active = false;                 // +0x110
	bool m_firstTime = true;               // +0x111
	int m_particlesLeft = 0, m_maxParticles = 0; // +0x114, +0x118
	bool m_isComplete = false;             // +0x11c
	std::uint16_t m_groupId = 0;
	bool m_hidden = false;

	// buffer
	unsigned m_maxNum = 2;
	std::uint32_t m_maxAge = 1000;
	std::uint32_t m_lastUpdateTime = 0;
	Vector3 m_accel;                       // per ms^2 (accel * 1e-6f)
	bool m_hasAccel = false;
	unsigned m_start = 0, m_end = 0, m_newEnd = 0;
	int m_nonNewNum = 0, m_newNum = 0;
	std::vector<NewParticle> m_queue;
	unsigned m_queueStart = 0, m_queueEnd = 0;
	int m_queueCount = 0;
	std::vector<Vector3> m_position, m_velocity;
	std::vector<std::uint32_t> m_timeStamp;
	std::vector<std::uint16_t> m_groupIds;
	W3DPropertyTable m_color, m_alpha, m_size, m_frame;
	// rotation / orientation
	bool m_hasOrientation = false;
	std::vector<std::uint32_t> m_rotTimes;
	std::vector<float> m_rotValues, m_halfRotDeltas, m_orientKeys;
	std::vector<float> m_randRotation, m_randOrientation;
	unsigned m_randRotationMask = 0, m_randOrientationMask = 0;
	float m_constantFrame = 0.0f;
};
