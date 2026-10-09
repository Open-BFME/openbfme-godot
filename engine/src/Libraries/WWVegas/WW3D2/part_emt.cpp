// OpenBFME. GPL-3.0. See part_emt.h for the target / donor facts. Addresses are RotWK game.dat (RW), stop S-001 caveat.
//
// Every float operation is performed in the order the RW code performs it (notes fx-w3d-emitters.md section 10). Compiled with
// -ffp-contract=off (cmake/Fx.cmake).

#include "Libraries/WWVegas/WW3D2/part_emt.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{

const float kOOIntMax = 1.0f / (float)INT_MAX; // Vector3Randomizer::OOIntMax: (float)INT_MAX rounds to 2^31, so exactly 2^-31 (RW 0xd08840)
const unsigned kMaxRandomEntries = 32;         // MAX_RANDOM_ENTRIES

const std::int32_t kMix1[20] = { (std::int32_t)0x0baa96887, 0x01e17d32c, 0x003bcdc3c, (std::int32_t)0x00f33d1b2, 0x076a6491d, (std::int32_t)0x0c570d85d,
	(std::int32_t)0x0e382b1e3, 0x078db4362, 0x07439a9d4, (std::int32_t)0x09cea8ac5, (std::int32_t)0x089537c5c, 0x02588f55d, 0x0415b5e1d, 0x0216e3d95,
	(std::int32_t)0x085c662e7, 0x05e8ab368, 0x03ea5cc8c, (std::int32_t)0x0d26a0f74, (std::int32_t)0x0f3a9222b, 0x048aad7e4 };
const std::int32_t kMix2[20] = { 0x04b0f3b58, (std::int32_t)0x0e874f0c3, 0x06955c5a6, 0x055a7ca46, 0x04d9a9d86, (std::int32_t)0x0fe28a195, (std::int32_t)0x0b1ca7865,
	0x06b235751, (std::int32_t)0x09a997a61, (std::int32_t)0x0aa6e95c8, (std::int32_t)0x0aaa98ee1, 0x05af9154c, (std::int32_t)0x0fc8e2263, 0x0390f5e8c,
	0x058ffd802, (std::int32_t)0x0ac0a5eba, (std::int32_t)0x0ac4874f6, (std::int32_t)0x0a9df0913, (std::int32_t)0x086be4c74, (std::int32_t)0x0ed2c123b };

Vector3 add(const Vector3 &a, const Vector3 &b) { return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
Vector3 mulf(const Vector3 &a, float s) { return Vector3(a.X * s, a.Y * s, a.Z * s); }

unsigned findPOT(unsigned n)
{
	unsigned p = 1;
	while (p < n)
	{
		p <<= 1;
	}
	return p;
}

unsigned gcdU(unsigned a, unsigned b)
{
	while (b)
	{
		const unsigned t = a % b;
		a = b;
		b = t;
	}
	return a;
}

} // namespace

// ---- Random3Class (RW 0xa2cff0) ---------------------------------------------------------------------------------------
int Random3Class::operator()()
{
	std::int32_t loword = (std::int32_t)Seed;
	std::int32_t hiword = (std::int32_t)Index++;
	for (int i = 0; i < 4; ++i)
	{
		const std::int32_t hihold = hiword;
		const std::uint32_t temp = (std::uint32_t)hihold ^ (std::uint32_t)kMix1[i];
		const std::int32_t itmpl = (std::int32_t)(temp & 0xffff);
		const std::int32_t itmph = (std::int32_t)temp >> 16;
		const std::uint32_t t2 = (std::uint32_t)itmpl * (std::uint32_t)itmpl + ~((std::uint32_t)itmph * (std::uint32_t)itmph);
		const std::int32_t s = (std::int32_t)t2;
		const std::uint32_t r = (std::uint32_t)(s >> 16) | ((std::uint32_t)s << 16); // the arithmetic shift fills the high half for a negative value
		hiword = (std::int32_t)((std::uint32_t)loword ^ (((r ^ (std::uint32_t)kMix2[i]) + (std::uint32_t)(itmpl * itmph))));
		loword = hihold;
	}
	return hiword;
}

// ---- Random4Class (RW 0xa2d0f0 / 0xa2d140) ----------------------------------------------------------------------------
Random4Class::Random4Class(std::uint32_t seed)
{
	if (!seed)
	{
		seed = 4375;
	}
	mt[0] = seed;
	for (mti = 1; mti < 624; ++mti)
	{
		mt[mti] = 69069u * mt[mti - 1];
	}
}

int Random4Class::operator()()
{
	const int N = 624, M = 397;
	static const std::uint32_t mag01[2] = { 0x0, 0x9908b0dfu };
	std::uint32_t y;
	if (mti >= N)
	{
		int kk;
		for (kk = 0; kk < N - M; ++kk)
		{
			y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
			mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		for (; kk < N - 1; ++kk)
		{
			y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
			mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		y = (mt[N - 1] & 0x80000000u) | (mt[0] & 0x7fffffffu);
		mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1];
		mti = 0;
	}
	y = mt[mti++];
	y ^= (y >> 11);
	y ^= (y << 7) & 0x9d2c5680u;
	y ^= (y << 15) & 0xefc60000u;
	y ^= (y >> 18);
	return (int)y;
}

// ---- Inv_Sqrt (RW 0x441c56, the Intel version of ZH wwmath.h) ----------------------------------------------------------
float WWMath_Inv_Sqrt(float a)
{
	std::uint32_t abits;
	std::memcpy(&abits, &a, 4);
	const std::uint32_t r0bits = (0xbe6eb508u - abits) >> 1;
	std::uint32_t y0bits = abits - 0x800000u; // a / 2
	float r0, y0;
	std::memcpy(&r0, &r0bits, 4);
	std::memcpy(&y0, &y0bits, 4);
	const float threeHalves = 1.5f;
	float x = r0;
	float y = (r0 * r0) * y0;
	float r = threeHalves - y;
	y = y * r;
	y = y * r;
	x = x * r;
	r = threeHalves - y;
	y = y * r;
	x = x * r;
	y = y * r;
	r = threeHalves - y;
	return x * r;
}

// ---- quaternions -------------------------------------------------------------------------------------------------------
W3DQuaternion Build_Quaternion(const Matrix3D &mat)
{
	static const int nxt[3] = { 1, 2, 0 };
	float q[4];
	const float tr = mat.Row[0][0] + mat.Row[1][1] + mat.Row[2][2];
	if (tr > 0.0f)
	{
		float s = (float)std::sqrt((double)tr + 1.0);
		q[3] = (float)((double)s * 0.5);
		s = (float)(0.5 / (double)s);
		q[0] = (mat.Row[2][1] - mat.Row[1][2]) * s;
		q[1] = (mat.Row[0][2] - mat.Row[2][0]) * s;
		q[2] = (mat.Row[1][0] - mat.Row[0][1]) * s;
	}
	else
	{
		int i = 0;
		if (mat.Row[1][1] > mat.Row[0][0])
		{
			i = 1;
		}
		if (mat.Row[2][2] > mat.Row[i][i])
		{
			i = 2;
		}
		const int j = nxt[i], k = nxt[j];
		float s = (float)std::sqrt((double)((mat.Row[i][i] - (mat.Row[j][j] + mat.Row[k][k]))) + 1.0);
		q[i] = (float)((double)s * 0.5);
		if (s != 0.0f)
		{
			s = (float)(0.5 / (double)s);
		}
		q[3] = (mat.Row[k][j] - mat.Row[j][k]) * s;
		q[j] = (mat.Row[j][i] + mat.Row[i][j]) * s;
		q[k] = (mat.Row[k][i] + mat.Row[i][k]) * s;
	}
	W3DQuaternion out;
	out.X = q[0];
	out.Y = q[1];
	out.Z = q[2];
	out.W = q[3];
	return out;
}

Vector3 Rotate_Vector(const W3DQuaternion &q, const Vector3 &v)
{
	const float X = q.X, Y = q.Y, Z = q.Z, W = q.W;
	const float x = W * v.X + (Y * v.Z - v.Y * Z);
	const float y = W * v.Y - (X * v.Z - v.X * Z);
	const float z = W * v.Z + (X * v.Y - v.X * Y);
	const float w = -(X * v.X + Y * v.Y + Z * v.Z);
	return Vector3(w * (-X) + W * x + (y * (-Z) - (-Y) * z), w * (-Y) + W * y - (x * (-Z) - (-X) * z), w * (-Z) + W * z + (x * (-Y) - (-X) * y));
}

namespace
{
struct SlerpInfo
{
	bool Linear = true, Flip = false;
	float Theta = 0.0f, SinT = 0.0f;
};

// RW 0xb2ba20: the dot product is summed w, z, y, x
SlerpInfo slerpSetup(const W3DQuaternion &p, const W3DQuaternion &q)
{
	SlerpInfo s;
	float cos_t = p.W * q.W + p.Z * q.Z + p.Y * q.Y + p.X * q.X;
	if (cos_t < 0.0f)
	{
		cos_t = -cos_t;
		s.Flip = true;
	}
	if (1.0f - cos_t < 0.001f)
	{
		s.Linear = true;
	}
	else
	{
		s.Linear = false;
		s.Theta = (float)std::acos((double)cos_t);
		s.SinT = (float)std::sin((double)s.Theta);
	}
	return s;
}

// RW 0xb2bae0: both weights are divided by Theta (the ZH quirk)
W3DQuaternion cachedSlerp(const W3DQuaternion &p, const W3DQuaternion &q, float alpha, const SlerpInfo &s)
{
	float beta;
	if (s.Linear)
	{
		beta = 1.0f - alpha;
	}
	else
	{
		const float oo = 1.0f / s.Theta;
		beta = (float)std::sin((double)(s.Theta - alpha * s.Theta)) * oo;
		alpha = (float)std::sin((double)(alpha * s.Theta)) * oo;
	}
	if (s.Flip)
	{
		alpha = -alpha;
	}
	W3DQuaternion r;
	r.X = beta * p.X + alpha * q.X;
	r.Y = beta * p.Y + alpha * q.Y;
	r.Z = beta * p.Z + alpha * q.Z;
	r.W = beta * p.W + alpha * q.W;
	return r;
}
} // namespace

// ---- randomisers (WWMath v3_rnd.cpp; draw orders RW 0xb37340 / 0xb37450 / 0xb37570 / 0xb37680) ------------------------------
float Vector3Randomizer::Get_Random_Float_Minus1_To_1(Random3Class &rng)
{
	return (float)(int)rng() * kOOIntMax; // RW 0xb372c0
}

namespace
{
class SolidBox : public Vector3Randomizer
{
public:
	explicit SolidBox(const Vector3 &e) : Extents(std::max(e.X, 0.0f), std::max(e.Y, 0.0f), std::max(e.Z, 0.0f)) {}
	void Get_Vector(Random3Class &rng, Vector3 &v) override
	{
		v.X = Get_Random_Float_Minus1_To_1(rng) * Extents.X;
		v.Y = Get_Random_Float_Minus1_To_1(rng) * Extents.Y;
		v.Z = Get_Random_Float_Minus1_To_1(rng) * Extents.Z;
	}
	void Scale(float s) override
	{
		s = std::max(s, 0.0f);
		Extents.X *= s;
		Extents.Y *= s;
		Extents.Z *= s;
	}
	std::unique_ptr<Vector3Randomizer> Clone() const override { return std::make_unique<SolidBox>(*this); }
	Vector3 Extents;
};

class SolidSphere : public Vector3Randomizer
{
public:
	explicit SolidSphere(float r) : Radius(std::max(r, 0.0f)) {}
	void Get_Vector(Random3Class &rng, Vector3 &v) override
	{
		const float rad2 = Radius * Radius;
		for (;;)
		{
			v.X = Get_Random_Float_Minus1_To_1(rng) * Radius;
			v.Y = Get_Random_Float_Minus1_To_1(rng) * Radius;
			v.Z = Get_Random_Float_Minus1_To_1(rng) * Radius;
			if ((v.X * v.X + v.Y * v.Y) + v.Z * v.Z <= rad2)
			{
				break;
			}
		}
	}
	void Scale(float s) override { Radius *= std::max(s, 0.0f); }
	std::unique_ptr<Vector3Randomizer> Clone() const override { return std::make_unique<SolidSphere>(*this); }
	float Radius;
};

class HollowSphere : public Vector3Randomizer
{
public:
	explicit HollowSphere(float r) : Radius(std::max(r, 0.0f)) {}
	void Get_Vector(Random3Class &rng, Vector3 &v) override
	{
		float l2;
		for (;;)
		{
			v.X = Get_Random_Float_Minus1_To_1(rng);
			v.Y = Get_Random_Float_Minus1_To_1(rng);
			v.Z = Get_Random_Float_Minus1_To_1(rng);
			l2 = (v.X * v.X + v.Y * v.Y) + v.Z * v.Z;
			if (l2 <= 1.0f && l2 > 0.0f)
			{
				break;
			}
		}
		const float scale = Radius * WWMath_Inv_Sqrt(l2);
		v.X *= scale;
		v.Y *= scale;
		v.Z *= scale;
	}
	void Scale(float s) override { Radius *= std::max(s, 0.0f); }
	std::unique_ptr<Vector3Randomizer> Clone() const override { return std::make_unique<HollowSphere>(*this); }
	float Radius;
};

class SolidCylinder : public Vector3Randomizer
{
public:
	SolidCylinder(float e, float r) : Extent(std::max(e, 0.0f)), Radius(std::max(r, 0.0f)) {}
	void Get_Vector(Random3Class &rng, Vector3 &v) override
	{
		v.X = Get_Random_Float_Minus1_To_1(rng) * Extent;
		float a, b;
		const float rad2 = Radius * Radius;
		for (;;)
		{
			a = Get_Random_Float_Minus1_To_1(rng) * Radius;
			b = Get_Random_Float_Minus1_To_1(rng) * Radius;
			if (a * a + b * b <= rad2)
			{
				break;
			}
		}
		v.Y = a;
		v.Z = b;
	}
	void Scale(float s) override
	{
		s = std::max(s, 0.0f);
		Extent *= s;
		Radius *= s;
	}
	std::unique_ptr<Vector3Randomizer> Clone() const override { return std::make_unique<SolidCylinder>(*this); }
	float Extent, Radius;
};

// RW 0x5af760: ClassID 0 SolidBox, 1 SolidSphere, 2 HollowSphere, 3 SolidCylinder; anything else NULL
std::unique_ptr<Vector3Randomizer> createRandomizer(const W3dVolumeRandomizerStruct &v)
{
	switch (v.ClassID)
	{
	case 0: return std::make_unique<SolidBox>(Vector3(v.Value1, v.Value2, v.Value3));
	case 1: return std::make_unique<SolidSphere>(v.Value1);
	case 2: return std::make_unique<HollowSphere>(v.Value1);
	case 3: return std::make_unique<SolidCylinder>(v.Value1, v.Value2);
	default: return nullptr;
	}
}
} // namespace

// ---- world ----------------------------------------------------------------------------------------------------------------
W3DEmitterWorld::W3DEmitterWorld() = default;
W3DEmitterWorld::~W3DEmitterWorld() = default;

ParticleEmitterInstance *W3DEmitterWorld::create(const ParticleEmitterDefClass &def, const Matrix3D &transform)
{
	m_emitters.push_back(std::make_unique<ParticleEmitterInstance>(*this, def, transform));
	return m_emitters.back().get();
}

void W3DEmitterWorld::remove(ParticleEmitterInstance *e)
{
	m_emitters.erase(std::remove_if(m_emitters.begin(), m_emitters.end(), [e](const std::unique_ptr<ParticleEmitterInstance> &p) { return p.get() == e; }), m_emitters.end());
}

void W3DEmitterWorld::update()
{
	for (const std::unique_ptr<ParticleEmitterInstance> &e : m_emitters)
	{
		e->onFrameUpdate();
	}
}

// ---- the instance -----------------------------------------------------------------------------------------------------------
ParticleEmitterInstance::ParticleEmitterInstance(W3DEmitterWorld &world, const ParticleEmitterDefClass &def, const Matrix3D &transform)
    : m_world(world), m_name(def.Name), m_texture(def.Info.TextureFilename), m_shader(def.InfoV2.Shader), m_renderMode(def.InfoV2.RenderMode), m_frameMode(def.InfoV2.FrameMode),
      m_transform(transform)
{
	if (def.Props.ColorKeyframes == 0 || def.Props.OpacityKeyframes == 0 || def.Props.SizeKeyframes == 0)
	{
		throw std::runtime_error("emitter " + def.Name + " has an empty property table (RW expects the start key in every one)");
	}
	// RW 0x5a07a0 constructor
	const float emitRate = def.Info.EmissionRate;
	m_emitRate = emitRate > 0.0f ? (std::uint32_t)(1000.0f / emitRate) : 1000u;
	// RW 0x5a07e8 divides with `fld 1000.0; fdiv emit_rate` before the constructor touches the x87 control word, so the quotient is rounded to the
	// ambient precision (24 bits under PC24, 64 under the default extended state); the integer is the same unless the float-rounded quotient crosses
	// an integer. The ambient state of a retail run was not established (stop S-196): the PC24 value is used and the extended one recorded.
	m_emitRateExtended = emitRate > 0.0f ? (std::uint32_t)(1000.0L / (long double)emitRate) : 1000u;
	m_burstSize = def.InfoV2.BurstSize ? def.InfoV2.BurstSize : 1;
	m_posRand = createRandomizer(def.InfoV2.CreationVolume);
	m_velRand = createRandomizer(def.InfoV2.VelRandom);
	if (m_velRand)
	{
		m_velRand->Scale(0.001f); // per millisecond (RW 0x5a0989)
	}
	m_baseVel = Vector3(def.Info.Velocity.X * 0.001f, def.Info.Velocity.Y * 0.001f, def.Info.Velocity.Z * 0.001f);
	m_outwardVel = def.InfoV2.OutwardVel * 0.001f;
	m_velInherit = def.InfoV2.VelInherit;
	m_prevQ = Build_Quaternion(m_transform);
	m_prevOrig = m_transform.Get_Translation();
	m_particlesLeft = m_maxParticles = (int)def.Info.MaxEmissions;
	float maxAge = def.Info.Lifetime;
	if (!(maxAge > 0.0f))
	{
		maxAge = 1.0f;
	}
	// buffer capacity: operands are the rate per second, not the period (RW 0x5a0991-0x5a09d1)
	int maxNum = (int)(((maxAge + 1.0f) * (float)m_burstSize) * emitRate);
	if (m_maxParticles > 0)
	{
		maxNum = std::min(maxNum, m_maxParticles);
	}
	m_maxNum = (unsigned)std::max(maxNum, 2);
	m_accel = Vector3(def.Info.Acceleration.X * 1e-6f, def.Info.Acceleration.Y * 1e-6f, def.Info.Acceleration.Z * 1e-6f); // RW 0x358637bd
	m_hasAccel = m_accel.X != 0.0f || m_accel.Y != 0.0f || m_accel.Z != 0.0f;
	// buffer constructor (RW 0x5ac020)
	m_maxAge = (std::uint32_t)(1000.0f * maxAge);
	m_lastUpdateTime = world.syncTime();
	m_queue.resize(m_maxNum);
	m_position.resize(m_maxNum);
	m_velocity.resize(m_maxNum);
	m_timeStamp.resize(m_maxNum);
	m_groupIds.resize(m_maxNum);
	// Reset_Colors, Reset_Opacity, Reset_Size, Reset_Rotations, Reset_Frames, Reset_Blur_Times: the rand_gen draw order
	resetColors(def);
	buildScalarTables(def);
	resetRotations(def);
	m_active = true; // Start() when the emitter is added to a visible scene (RW 0x5a1210)
	m_firstTime = false;
	++m_groupId;
}

void ParticleEmitterInstance::setHidden(bool hidden)
{
	m_hidden = hidden;
	if (!hidden && !m_active)
	{
		m_active = true; // Start (RW 0x5a1210)
		m_prevQ = Build_Quaternion(m_transform);
		m_prevOrig = m_transform.Get_Translation();
		if (m_isComplete)
		{
			m_particlesLeft = m_maxParticles;
			m_isComplete = false;
		}
		++m_groupId;
	}
	else if (hidden && m_active)
	{
		m_active = false;
	}
}

bool ParticleEmitterInstance::isComplete() const
{
	return m_isComplete && m_nonNewNum == 0 && m_newNum == 0;
}

// colours: the first stored key is the start value (times ignored); RW Reset_Colors 0x5a81a0
void ParticleEmitterInstance::resetColors(const ParticleEmitterDefClass &def)
{
	const std::vector<W3dEmitterColorKeyframeStruct> &keys = def.ColorKeyframes;
	auto rgb = [](const W3dRGBAStruct &c) { return Vector3((float)c.R / 255.0f, (float)c.G / 255.0f, (float)c.B / 255.0f); };
	const Vector3 start = rgb(keys[0].Color);
	Vector3 rand = rgb(def.Props.ColorRandom);
	// the loader keeps the quirk: a black last key with a non-zero random becomes -ColorRandom
	const size_t numKeys = keys.size() - 1;
	std::vector<float> kt(numKeys);
	std::vector<Vector3> kv(numKeys);
	for (size_t i = 0; i < numKeys; ++i)
	{
		kt[i] = keys[i + 1].Time;
		kv[i] = rgb(keys[i + 1].Color);
	}
	if (numKeys > 0 && kv[numKeys - 1].X == 0.0f && kv[numKeys - 1].Y == 0.0f && kv[numKeys - 1].Z == 0.0f && (rand.X > 0.0f || rand.Y > 0.0f || rand.Z > 0.0f))
	{
		kv[numKeys - 1] = Vector3(-rand.X, -rand.Y, -rand.Z);
	}
	const float eps = 0.0038f;
	const bool randZero = std::fabs(rand.X) < eps && std::fabs(rand.Y) < eps && std::fabs(rand.Z) < eps;
	W3DPropertyTable &t = m_color;
	if (randZero && numKeys == 0)
	{
		t.constant = true;
		t.cvalues = { start };
		t.randomMask = 0;
		return;
	}
	t.constant = false;
	std::uint32_t prev = 0;
	size_t ckey = 0;
	for (; ckey < numKeys; ++ckey)
	{
		const std::uint32_t cur = (std::uint32_t)(kt[ckey] * 1000.0f);
		if (cur >= m_maxAge)
		{
			break;
		}
		prev = cur;
	}
	(void)prev;
	const bool constantAtEnd = (ckey == numKeys);
	const size_t n = ckey + 1;
	t.times.assign(n, 0);
	t.cvalues.assign(n, start);
	t.cdeltas.assign(n, Vector3());
	for (size_t i = 1; i < n; ++i)
	{
		t.times[i] = (std::uint32_t)(kt[i - 1] * 1000.0f);
		t.cvalues[i] = kv[i - 1];
	}
	for (size_t i = 0; i + 1 < n; ++i)
	{
		// colour deltas multiply by the reciprocal of the time difference (RW 0x5a8564)
		const float inv = 1.0f / (float)(t.times[i + 1] - t.times[i]);
		t.cdeltas[i] = Vector3((t.cvalues[i + 1].X - t.cvalues[i].X) * inv, (t.cvalues[i + 1].Y - t.cvalues[i].Y) * inv, (t.cvalues[i + 1].Z - t.cvalues[i].Z) * inv);
	}
	const size_t last = n - 1;
	if (constantAtEnd)
	{
		t.cdeltas[last] = Vector3(0, 0, 0);
	}
	else
	{
		const float inv = 1.0f / (kt[last] * 1000.0f - (float)t.times[last]);
		t.cdeltas[last] = Vector3((kv[last].X - t.cvalues[last].X) * inv, (kv[last].Y - t.cvalues[last].Y) * inv, (kv[last].Z - t.cvalues[last].Z) * inv);
	}
	if (randZero)
	{
		t.crandom = { Vector3(0, 0, 0) };
		t.randomMask = 0;
	}
	else
	{
		const unsigned entries = std::min(findPOT(m_maxNum), kMaxRandomEntries);
		t.randomMask = entries - 1;
		t.crandom.resize(entries);
		const float rscale = rand.X * kOOIntMax, gscale = rand.Y * kOOIntMax, bscale = rand.Z * kOOIntMax;
		for (unsigned j = 0; j < entries; ++j)
		{
			// MSVC evaluates the three draws in the order Y, X, Z (RW 0x5a8790-0x5a87ff)
			const float g = (float)m_world.randGen()() * gscale;
			const float r = (float)m_world.randGen()() * rscale;
			const float b = (float)m_world.randGen()() * bscale;
			t.crandom[j] = Vector3(r, g, b);
		}
	}
}

// scalar properties: alpha (RW 0x5a8820), size (0x5a8cd0), frame (0x5a9910)
void ParticleEmitterInstance::resetScalar(W3DPropertyTable &t, const std::vector<float> &kt, const std::vector<float> &kv, float start, float rand, float eps, bool isSize)
{
	(void)isSize;
	const size_t numKeys = kt.size();
	const bool randZero = std::fabs(rand) < eps;
	if (randZero && numKeys == 0)
	{
		t.constant = true;
		t.values = { start };
		t.randomMask = 0;
		return;
	}
	t.constant = false;
	size_t key = 0;
	for (; key < numKeys; ++key)
	{
		const std::uint32_t cur = (std::uint32_t)(kt[key] * 1000.0f);
		if (cur >= m_maxAge)
		{
			break;
		}
	}
	const bool constantAtEnd = (key == numKeys);
	const size_t n = key + 1;
	t.times.assign(n, 0);
	t.values.assign(n, start);
	t.deltas.assign(n, 0.0f);
	for (size_t i = 1; i < n; ++i)
	{
		t.times[i] = (std::uint32_t)(kt[i - 1] * 1000.0f);
		t.values[i] = kv[i - 1];
	}
	for (size_t i = 0; i + 1 < n; ++i)
	{
		t.deltas[i] = (t.values[i + 1] - t.values[i]) / (float)(t.times[i + 1] - t.times[i]);
	}
	const size_t last = n - 1;
	t.deltas[last] = constantAtEnd ? 0.0f : (kv[last] - t.values[last]) / (kt[last] * 1000.0f - (float)t.times[last]);
	if (randZero)
	{
		t.random = { 0.0f };
		t.randomMask = 0;
	}
	else
	{
		const unsigned entries = std::min(findPOT(m_maxNum), kMaxRandomEntries);
		t.randomMask = entries - 1;
		t.random.resize(entries);
		const float scale = rand * kOOIntMax;
		for (unsigned j = 0; j < entries; ++j)
		{
			t.random[j] = (float)m_world.randGen()() * scale;
		}
	}
}

void ParticleEmitterInstance::buildScalarTables(const ParticleEmitterDefClass &def)
{
	// opacity: first key = start
	{
		std::vector<float> kt, kv;
		for (size_t i = 1; i < def.OpacityKeyframes.size(); ++i)
		{
			kt.push_back(def.OpacityKeyframes[i].Time);
			kv.push_back(def.OpacityKeyframes[i].Opacity);
		}
		resetScalar(m_alpha, kt, kv, def.OpacityKeyframes[0].Opacity, def.Props.OpacityRandom, 0.0038f, false);
	}
	{
		std::vector<float> kt, kv;
		for (size_t i = 1; i < def.SizeKeyframes.size(); ++i)
		{
			kt.push_back(def.SizeKeyframes[i].Time);
			kv.push_back(def.SizeKeyframes[i].Size);
		}
		resetScalar(m_size, kt, kv, def.SizeKeyframes[0].Size, def.Props.SizeRandom, 1.0e-12f, true);
	}
}

void ParticleEmitterInstance::resetRotations(const ParticleEmitterDefClass &def)
{
	// rotation (RW 0x5a9250), then frames (RW 0x5a9910); blur times are not used by the point modes
	std::vector<float> kt, kv;
	float start = 0.0f, rand = 0.0f, orient = 0.0f;
	if (def.HasRotation)
	{
		start = def.RotationKeyframes[0].Rotation;
		rand = def.RotationHeader.Random;
		orient = def.RotationHeader.OrientationRandom;
		for (size_t i = 1; i < def.RotationKeyframes.size(); ++i)
		{
			kt.push_back(def.RotationKeyframes[i].Time);
			kv.push_back(def.RotationKeyframes[i].Rotation);
		}
	}
	const float eps = 2.77777778e-4f;
	const bool orientRandZero = std::fabs(orient) < eps;
	const bool rotRandZero = std::fabs(rand) < eps;
	if (orientRandZero && rotRandZero && kt.empty() && std::fabs(start) < eps)
	{
		m_hasOrientation = false;
	}
	else
	{
		m_hasOrientation = true;
		size_t key = 0;
		for (; key < kt.size(); ++key)
		{
			if ((std::uint32_t)(kt[key] * 1000.0f) >= m_maxAge)
			{
				break;
			}
		}
		const bool constantAtEnd = (key == kt.size());
		const size_t n = key + 1;
		m_rotTimes.assign(n, 0);
		m_rotValues.assign(n, 0.0f);
		m_halfRotDeltas.assign(n, 0.0f);
		m_orientKeys.assign(n, 0.0f);
		m_rotValues[0] = start * 0.001f;
		for (size_t i = 1; i < n; ++i)
		{
			m_rotTimes[i] = (std::uint32_t)(kt[i - 1] * 1000.0f);
			m_rotValues[i] = kv[i - 1] * 0.001f;
		}
		for (size_t i = 0; i + 1 < n; ++i)
		{
			m_halfRotDeltas[i] = 0.5f * ((m_rotValues[i + 1] - m_rotValues[i]) / (float)(m_rotTimes[i + 1] - m_rotTimes[i]));
		}
		const size_t last = n - 1;
		m_halfRotDeltas[last] = constantAtEnd ? 0.0f : 0.5f * (kv[last] * 0.001f - m_rotValues[last]) / (kt[last] * 1000.0f - (float)m_rotTimes[last]);
		m_orientKeys[0] = 0.0f;
		for (size_t i = 1; i < n; ++i)
		{
			const float dt = (float)(m_rotTimes[i] - m_rotTimes[i - 1]);
			m_orientKeys[i] = m_orientKeys[i - 1] + dt * (m_rotValues[i - 1] + m_halfRotDeltas[i - 1] * dt);
		}
		const unsigned entries = std::min(findPOT(m_maxNum), kMaxRandomEntries);
		if (rotRandZero)
		{
			m_randRotation = { 0.0f };
			m_randRotationMask = 0;
		}
		else
		{
			m_randRotationMask = entries - 1;
			m_randRotation.resize(entries);
			const float scale = rand * 0.001f * kOOIntMax;
			for (unsigned j = 0; j < entries; ++j)
			{
				m_randRotation[j] = (float)m_world.randGen()() * scale;
			}
		}
		if (orientRandZero)
		{
			m_randOrientation = { 0.0f };
			m_randOrientationMask = 0;
		}
		else
		{
			m_randOrientationMask = entries - 1;
			m_randOrientation.resize(entries);
			const float scale = orient * kOOIntMax;
			for (unsigned j = 0; j < entries; ++j)
			{
				m_randOrientation[j] = (float)m_world.randGen()() * scale;
			}
		}
	}
	// frames
	std::vector<float> ft, fv;
	float fstart = 0.0f, frand = 0.0f;
	if (def.HasFrames)
	{
		fstart = def.FrameKeyframes[0].Frame;
		frand = def.FrameHeader.Random;
		for (size_t i = 1; i < def.FrameKeyframes.size(); ++i)
		{
			ft.push_back(def.FrameKeyframes[i].Time);
			fv.push_back(def.FrameKeyframes[i].Frame);
		}
	}
	resetScalar(m_frame, ft, fv, fstart, frand, 0.1f, false);
	m_constantFrame = fstart;
}

// ---- emission (RW 0x5a2060 / 0x5a1d60 / 0x5a14d0) ----------------------------------------------------------------------------
void ParticleEmitterInstance::onFrameUpdate()
{
	updateKinematic(); // the culling pass runs first (Update_Bounding_Box calls Update_Kinematic)
	if (m_active && !m_isComplete)
	{
		const W3DQuaternion q = Build_Quaternion(m_transform);
		const Vector3 o = m_transform.Get_Translation();
		createNewParticles(q, o);
		m_prevQ = q;
		m_prevOrig = o;
	}
	else
	{
		m_prevQ = Build_Quaternion(m_transform);
		m_prevOrig = m_transform.Get_Translation();
	}
}

ParticleEmitterInstance::NewParticle &ParticleEmitterInstance::addUninitializedNewParticle()
{
	NewParticle &p = m_queue[m_queueEnd];
	m_queueEnd = (m_queueEnd + 1) % m_maxNum;
	if (++m_queueCount == (int)m_maxNum + 1)
	{
		m_queueStart = (m_queueStart + 1) % m_maxNum; // the oldest queued particle is dropped
		--m_queueCount;
	}
	return p;
}

void ParticleEmitterInstance::createNewParticles(const W3DQuaternion &currQ, const Vector3 &currOrig)
{
	std::uint32_t frametime = m_world.syncTime() - m_world.previousSyncTime();
	if (frametime > 100u * m_emitRate)
	{
		const unsigned g = gcdU(m_maxNum, m_burstSize);
		const std::uint32_t cycle = m_emitRate * (m_maxNum / g);
		frametime = (cycle > 1) ? frametime % cycle : 1;
	}
	m_emitRemain += frametime;
	if (!(m_emitRemain > m_emitRate))
	{
		// no burst this frame: the remainder carries; the inherited velocity does not matter without a burst
		return;
	}
	const float rec = 1.0f / (float)frametime;
	float alpha = 1.0f - (float)m_emitRemain * rec;
	const float dAlpha = (float)m_emitRate * rec;
	const SlerpInfo slerp = slerpSetup(m_prevQ, currQ);
	if (m_velInherit != 0.0f)
	{
		const float k = rec * m_velInherit;
		m_world.inheritedWorldSpaceEmitterVel = Vector3((currOrig.X - m_prevOrig.X) * k, (currOrig.Y - m_prevOrig.Y) * k, (currOrig.Z - m_prevOrig.Z) * k);
	}
	else
	{
		m_world.inheritedWorldSpaceEmitterVel = Vector3(0, 0, 0);
	}
	while (m_emitRemain > m_emitRate)
	{
		m_emitRemain -= m_emitRate;
		alpha += dAlpha;
		const W3DQuaternion quat = cachedSlerp(m_prevQ, currQ, alpha, slerp);
		const Vector3 orig((currOrig.X - m_prevOrig.X) * alpha + m_prevOrig.X, (currOrig.Y - m_prevOrig.Y) * alpha + m_prevOrig.Y,
			(currOrig.Z - m_prevOrig.Z) * alpha + m_prevOrig.Z);
		const std::uint32_t ageStamp = m_world.syncTime() - m_emitRemain;
		unsigned burst = m_burstSize;
		if (m_oneTimeBurst)
		{
			burst = m_oneTimeBurstSize;
			m_oneTimeBurst = false;
		}
		if (m_particlesLeft > 0)
		{
			if ((int)burst > m_particlesLeft)
			{
				burst = (unsigned)m_particlesLeft;
				m_particlesLeft = 0;
			}
			else
			{
				m_particlesLeft -= (int)burst;
			}
			if (m_particlesLeft <= 0)
			{
				m_isComplete = true;
			}
		}
		for (unsigned i = 0; i < burst; ++i)
		{
			initializeParticle(addUninitializedNewParticle(), ageStamp, quat, orig);
		}
		if (m_isComplete)
		{
			break;
		}
	}
}

void ParticleEmitterInstance::initializeParticle(NewParticle &np, std::uint32_t ts, const W3DQuaternion &q, const Vector3 &orig)
{
	np.ts = ts;
	Vector3 randPos(0, 0, 0);
	if (m_posRand)
	{
		m_posRand->Get_Vector(m_world.randomizer(), randPos);
	}
	np.pos = add(Rotate_Vector(q, randPos), orig);
	Vector3 randVel(0, 0, 0);
	if (m_velRand)
	{
		m_velRand->Get_Vector(m_world.randomizer(), randVel);
	}
	if (m_outwardVel != 0.0f)
	{
		const float l2 = randPos.X * randPos.X + randPos.Z * randPos.Z + randPos.Y * randPos.Y; // sum order x, z, y (RW 0x5a16fa)
		Vector3 outwards;
		if (l2 != 0.0f)
		{
			outwards = mulf(randPos, m_outwardVel * WWMath_Inv_Sqrt(l2));
		}
		else
		{
			outwards = Vector3(m_outwardVel, 0, 0);
		}
		randVel = add(randVel, outwards);
	}
	randVel = add(m_baseVel, randVel);
	np.vel = add(Rotate_Vector(q, randVel), m_world.inheritedWorldSpaceEmitterVel);
	np.groupId = m_groupId;
}

// ---- kinematics (RW 0x5ae3f0 and helpers) -------------------------------------------------------------------------------------
void ParticleEmitterInstance::updateKinematic()
{
	const std::uint32_t elapsed = m_world.syncTime() - m_lastUpdateTime;
	if (elapsed == 0)
	{
		return;
	}
	getNewParticles();
	killOldParticles();
	if (m_nonNewNum > 0)
	{
		updateNonNewParticles(elapsed);
	}
	m_end = m_newEnd;
	m_nonNewNum += m_newNum;
	m_newNum = 0;
	m_lastUpdateTime = m_world.syncTime();
}

void ParticleEmitterInstance::getNewParticles()
{
	// the queue is initialised so that NewEnd == End at the start of every update
	m_newEnd = m_end;
	while (m_queueCount > 0)
	{
		const NewParticle &np = m_queue[m_queueStart];
		m_queueStart = (m_queueStart + 1) % m_maxNum;
		--m_queueCount;
		m_timeStamp[m_newEnd] = np.ts;
		const std::uint32_t age = m_world.syncTime() - np.ts;
		if (age >= m_maxAge)
		{
			continue;
		}
		const float fa = (float)age;
		if (m_hasAccel)
		{
			const Vector3 half = mulf(m_accel, 0.5f);
			const Vector3 v = add(np.vel, mulf(half, fa));
			m_position[m_newEnd] = add(np.pos, mulf(v, fa));
			m_velocity[m_newEnd] = add(np.vel, mulf(m_accel, fa));
		}
		else
		{
			m_position[m_newEnd] = add(np.pos, mulf(np.vel, fa));
			m_velocity[m_newEnd] = np.vel;
		}
		m_groupIds[m_newEnd] = np.groupId;
		m_newEnd = (m_newEnd + 1) % m_maxNum;
		++m_newNum;
		if (m_newNum + m_nonNewNum == (int)m_maxNum + 1)
		{
			m_start = (m_start + 1) % m_maxNum;
			--m_nonNewNum;
			if (m_nonNewNum == -1)
			{
				m_end = (m_end + 1) % m_maxNum;
				m_nonNewNum = 0;
				--m_newNum;
			}
		}
	}
}

void ParticleEmitterInstance::killOldParticles()
{
	const std::uint32_t now = m_world.syncTime();
	unsigned sub1End, sub2Start;
	if (m_start < m_end || (m_start == m_end && m_nonNewNum == 0))
	{
		sub1End = m_end;
		sub2Start = m_end;
	}
	else
	{
		sub1End = m_maxNum;
		sub2Start = 0;
	}
	unsigned i;
	for (i = m_start; i < sub1End; ++i)
	{
		if (now - m_timeStamp[i] < m_maxAge)
		{
			break;
		}
		--m_nonNewNum;
	}
	if (i == sub1End && sub1End != m_end)
	{
		for (i = sub2Start; i < m_end; ++i)
		{
			if (now - m_timeStamp[i] < m_maxAge)
			{
				break;
			}
			--m_nonNewNum;
		}
	}
	else if (i == sub1End)
	{
		// sub range 1 ended at End: nothing left in range 2
	}
	m_start = i % m_maxNum;
	if (m_nonNewNum < 0)
	{
		m_nonNewNum = 0;
	}
}

void ParticleEmitterInstance::updateNonNewParticles(std::uint32_t elapsed)
{
	const float dt = (float)elapsed;
	unsigned sub1End, sub2Start;
	if (m_start < m_end || (m_start == m_end && m_nonNewNum == 0))
	{
		sub1End = m_end;
		sub2Start = m_end;
	}
	else
	{
		sub1End = m_maxNum;
		sub2Start = 0;
	}
	// only the non-new particles [Start, End) are integrated (new ones were placed at their exact age by getNewParticles)
	auto step = [&](unsigned i) {
		if (m_hasAccel)
		{
			const Vector3 dv = mulf(m_accel, dt);
			const Vector3 ap = mulf(m_accel, (dt * dt) * 0.5f);
			const Vector3 vdt = mulf(m_velocity[i], dt);
			m_position[i] = add(m_position[i], add(vdt, ap));
			m_velocity[i] = add(m_velocity[i], dv);
		}
		else
		{
			m_position[i] = add(m_position[i], mulf(m_velocity[i], dt));
		}
	};
	for (unsigned i = m_start; i < sub1End; ++i)
	{
		step(i);
	}
	for (unsigned i = sub2Start; i < m_end; ++i)
	{
		step(i);
	}
}

// ---- visual state (RW 0x5a9e40) ----------------------------------------------------------------------------------------------
void ParticleEmitterInstance::collectVisuals(std::vector<Visual> &out)
{
	out.clear();
	updateKinematic();
	const std::uint32_t now = m_world.syncTime();
	unsigned sub1End, sub2Start;
	if (m_start < m_end || (m_start == m_end && m_nonNewNum == 0))
	{
		sub1End = m_end;
		sub2Start = m_end;
	}
	else
	{
		sub1End = m_maxNum;
		sub2Start = 0;
	}
	auto keyOf = [](const std::vector<std::uint32_t> &times, std::uint32_t age) {
		size_t k = times.size() - 1;
		while (age < times[k])
		{
			--k;
		}
		return k;
	};
	auto one = [&](unsigned part) {
		Visual v;
		const std::uint32_t age = now - m_timeStamp[part];
		v.position = m_position[part];
		// colour: SSE order (V + D * dt) + R
		if (m_color.constant)
		{
			v.color = m_color.cvalues[0];
		}
		else
		{
			const size_t k = keyOf(m_color.times, age);
			const float dtf = (float)(age - m_color.times[k]);
			const Vector3 &R = m_color.crandom[part & m_color.randomMask];
			v.color = Vector3((m_color.cvalues[k].X + m_color.cdeltas[k].X * dtf) + R.X, (m_color.cvalues[k].Y + m_color.cdeltas[k].Y * dtf) + R.Y,
				(m_color.cvalues[k].Z + m_color.cdeltas[k].Z * dtf) + R.Z);
		}
		// alpha, size, frame: x87 order ((dt * D) + R) + V
		auto scalar = [&](const W3DPropertyTable &t) {
			if (t.constant)
			{
				return t.values[0];
			}
			const size_t k = keyOf(t.times, age);
			const float dtf = (float)(age - t.times[k]);
			return ((dtf * t.deltas[k]) + t.random[part & t.randomMask]) + t.values[k];
		};
		v.alpha = scalar(m_alpha);
		v.size = scalar(m_size);
		if (!(v.size >= 0.0f))
		{
			v.size = 0.0f;
		}
		const float frame = scalar(m_frame);
		v.frame = (std::uint8_t)(((int)frame) & 0xFF);
		v.orientation = 0;
		if (m_hasOrientation)
		{
			const size_t k = keyOf(m_rotTimes, age);
			const float dtf = (float)(age - m_rotTimes[k]);
			const float t = (((m_orientKeys[k] + (m_rotValues[k] + m_halfRotDeltas[k] * dtf) * dtf) + m_randRotation[part & m_randRotationMask] * (float)age) +
				m_randOrientation[part & m_randOrientationMask]);
			v.orientation = (std::uint8_t)(((int)(t * 256.0f)) & 0xFF);
		}
		// Combine_Color_And_Alpha: clamp to [0, 1]
		auto c01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
		v.color = Vector3(c01(v.color.X), c01(v.color.Y), c01(v.color.Z));
		v.alpha = c01(v.alpha);
		out.push_back(v);
	};
	for (unsigned part = m_start; part < sub1End; ++part)
	{
		one(part);
	}
	for (unsigned part = sub2Start; part < m_end; ++part)
	{
		one(part);
	}
}
