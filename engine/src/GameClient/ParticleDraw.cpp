// OpenBFME. GPL-3.0. See ParticleDraw.h for the target / donor facts. Addresses are RotWK game.dat (RW), stop S-001 caveat.

#include "GameClient/ParticleDraw.h"

#include "Common/JobSystem.h"

#include <algorithm>
#include <cmath>

namespace FXParticleSystem
{

namespace
{

enum
{
	MAX_PARTICLES_PER_DRAW = 0x200 // every CPU draw module stops at 0x200 particles per system (RW scratch arrays)
};

const float kTo01 = 0.003921568859f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// the vertex colour as D3D stores it: clamp, then truncate x * 255 (RW 0x57d640 / 0x577ee3), here back in 0..1
float quantize(float v) { return (float)(int)(clamp01(v) * 255.0f) / 255.0f; }

Coord3D sub(const Coord3D &a, const Coord3D &b) { return Coord3D{ a.x - b.x, a.y - b.y, a.z - b.z }; }
Coord3D cross(const Coord3D &a, const Coord3D &b) { return Coord3D{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float dot(const Coord3D &a, const Coord3D &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Coord3D normalize(const Coord3D &v)
{
	const float l = std::sqrt(dot(v, v));
	return l > 0.0f ? Coord3D{ v.x / l, v.y / l, v.z / l } : Coord3D{ 0.0f, 0.0f, 0.0f };
}

// Per-particle values the modules read: size (Update module, 0 without one), RGB (Color module, black without), alpha (Alpha module, 0 without).
struct ParticleLook
{
	float size, r, g, b, a, angle;
};
ParticleLook lookOf(const Particle &p)
{
	ParticleLook l;
	l.size = p.update.present ? p.update.size[0] : 0.0f;
	l.angle = p.update.present ? p.update.angleZ : 0.0f;
	l.r = p.color.present ? p.color.color[0] : 0.0f;
	l.g = p.color.present ? p.color.color[1] : 0.0f;
	l.b = p.color.present ? p.color.color[2] : 0.0f;
	l.a = p.alpha.present ? p.alpha.alpha : 0.0f;
	return l;
}

std::string textureName(const ParticleSystem &s) { return s.info().m_particleTypeName; }

bool isSmudge(const ParticleSystem &s)
{
	const std::string &n = s.info().m_particleTypeName;
	return s.info().m_particleType == PARTICLE_TYPE_SMUDGE || (n.size() >= 4 && n.compare(0, 4, "SMUD") == 0); // RW 0x44c9ab: "SMUD" prefix
}

void rotateAboutAxis(const float angle, Coord3D axis, float out[3][3])
{
	axis = normalize(axis);
	const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
	out[0][0] = t * axis.x * axis.x + c;
	out[0][1] = t * axis.x * axis.y - s * axis.z;
	out[0][2] = t * axis.x * axis.z + s * axis.y;
	out[1][0] = t * axis.x * axis.y + s * axis.z;
	out[1][1] = t * axis.y * axis.y + c;
	out[1][2] = t * axis.y * axis.z - s * axis.x;
	out[2][0] = t * axis.x * axis.z - s * axis.y;
	out[2][1] = t * axis.y * axis.z + s * axis.x;
	out[2][2] = t * axis.z * axis.z + c;
}

} // namespace

std::uint8_t ParticleDrawBuilder::angleByte(float angle)
{
	return (std::uint8_t)(int)(angle * 40.5845f); // RW 0x4222568a = 255 / (2 pi); ftol truncates, the cast wraps
}

const char *RibbonApproximationText()
{
	return "Streak / Lightning ribbons use averaged joint directions and an eye-facing perpendicular, not the donor StreakRender silhouette-plane intersections "
		   "(parallel factor 0.9, merge-abort factor 1.5); bends and points near the eye differ from the target";
}

const char *SortApproximationText()
{
	return "non-deferred ALPHA sprite batches are sorted back to front by camera distance per merged batch; retail sorts every non-deferred sorting-enabled "
		   "triangle together in the sorting renderer (RW 0x44c982 / 0x44cbed defer only SortLevel 1), so interleaving between batches of different state differs; "
		   "ALPHA_TEST batches draw in Godot's opaque pass, before every blended batch, instead of in list order";
}

// lane FX-3 (QA-1 U7, the construction dust): every factor of a DefaultDraw sprite is read from the binary - the texture (ParticleName, its alpha used:
// gradient MODULATE, RW 0x5789E0), the billboard of edge `size` (PointGroup corners +-0.5, RW 0x57DAB0), the size update (RW 0x96AF99), the truncated vertex
// colour (RW 0x577EE3), the ShaderClass word (ALPHA 0x1180B3 at RW 0xD9B310: depth compare LEQUAL, no depth write, SRC_ALPHA / INV_SRC_ALPHA, alpha-test field 0; implicit GREATEREQUAL test with reference 1 (RW 0x537188-0x5371BF))
// and the SortLevel 1 deferral (RW 0x44C84A). The straight lines where a puff meets the ground are that depth test: retail has no soft-particle path (the
// CPU modules use the fixed-function word; Shaders.big holds no particle effect besides GpuDraw's gpuparticle.fxo). What no capture confirms is the composite.
const char *SpriteCompositeText()
{
	return "the composite of DefaultDraw sprites (overlap, the straight cuts where a billboard meets the ground or a model: depth test LEQUAL without depth "
		   "writes, ShaderClass word RW 0xD9B310) is derived from the binary and has not been compared with a retail frame capture";
}

const char *FogOmissionText()
{
	// lane RENDER-4: the fog itself is applied since S-1651 (GodotFXPlayer, W3DHardwareFog); the remainder is the GPU particles' non-alpha blends
	return "the CPU particles fog as RW 0x9620b7 -> ShaderClass::Enable_Fog (0x537ce0) does (additive toward black, alpha and opaque toward the fog colour, "
		   "multiply toward white; lane RENDER-4, S-1651); GPU particles fog only with the alpha blend (gpuparticle.fxo ps_2_0 blob 7), the other GPU "
		   "blends are drawn without fog";
}

BatchOrder OrderForBatch(size_t rank, size_t count)
{
	const size_t levels = 120; // material priorities -128 .. -9
	BatchOrder o;
	if (count <= levels)
	{
		o.priority = (int)rank - 128;
		o.sortOffset = 0.0f;
		return o;
	}
	// more batches than distinct priorities: the priority buckets stay monotone and the rank breaks ties inside a bucket through the transparent
	// sort offset (the camera distance of the batch is tiny against the 1e5 step)
	o.priority = (int)((rank * levels) / count) - 128;
	o.sortOffset = (float)rank * 100000.0f;
	return o;
}

bool SpriteBatchDepthSorted(int shader, bool deferred)
{
	return !deferred && (shader == PARTICLE_SHADER_ALPHA || shader == PARTICLE_SHADER_ALPHA_NO_DEPTH_TEST);
}

void ParticleDrawBuilder::note(const char *code, const char *text)
{
	const std::string line = std::string(code) + ": " + text;
	for (const std::string &u : m_unverified)
	{
		if (u == line)
		{
			return;
		}
	}
	m_unverified.push_back(line);
}

void ParticleDrawBuilder::buildSprites(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b)
{
	b.kind = DrawBatch::SPRITES;
	b.groundAligned = s.info().m_isGroundAligned; // Set_Billboard(!IsGroundAligned)
	b.volume = s.info().m_particleType == PARTICLE_TYPE_VOLUME_PARTICLE;
	int n = 0;
	for (int p = s.firstParticle(); p >= 0 && n < MAX_PARTICLES_PER_DRAW; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		if (s.isParticleInvisible(pp))
		{
			continue;
		}
		const ParticleLook l = lookOf(pp);
		SpriteInstance i;
		i.x = pp.pos.x;
		i.y = pp.pos.y;
		i.z = pp.pos.z;
		i.size = l.size;
		i.angleByte = angleByte(l.angle);
		i.r = quantize(l.r);
		i.g = quantize(l.g);
		i.b = quantize(l.b);
		i.a = quantize(l.a);
		b.sprites.push_back(i);
		++n;
	}
}

void ParticleDrawBuilder::buildGpu(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b)
{
	b.kind = DrawBatch::GPU;
	for (int p = s.firstParticle(); p >= 0; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		GpuInstance g;
		g.x = pp.pos.x;
		g.y = pp.pos.y;
		g.z = pp.pos.z;
		g.life = (float)pp.lifetime;
		g.vx = pp.vel.x;
		g.vy = pp.vel.y;
		g.vz = pp.vel.z;
		g.birth = pp.gpuBirth;
		g.random = pp.random;
		b.gpu.push_back(g);
	}
	// the SAS callbacks (RW 0x5f61b1 / 0x5f6bd5 / 0x5f6f40) bind the module values of the system being drawn
	GpuDrawParams &gp = b.gpuParams;
	gp.now = s.gpuNow();
	gp.shaderType = s.info().m_shaderType;
	if (const GpuDrawModuleData *d = static_cast<const GpuDrawModuleData *>(s.drawModule()))
	{
		gp.framesPerRow = (float)d->m_framesPerRow;
		gp.totalFrames = (float)d->m_totalFrames;
		gp.speedMultiplier = d->m_speedMultiplier;
	}
	if (const DefaultColorModuleData *c = s.colorModule())
	{
		for (int i = 0; i < 4; ++i)
		{
			gp.colorKeys[i][0] = c->m_colorKey[i].color.red;
			gp.colorKeys[i][1] = c->m_colorKey[i].color.green;
			gp.colorKeys[i][2] = c->m_colorKey[i].color.blue;
			gp.timeKeys[i] = (float)c->m_colorKey[i].frame;
		}
		gp.colorScale[0] = c->m_colorScale.m_low * kTo01;
		gp.colorScale[1] = c->m_colorScale.m_high * kTo01;
	}
	if (const DefaultAlphaModuleData *a = s.alphaModule())
	{
		for (int i = 0; i < 4; ++i)
		{
			gp.colorKeys[i][3] = a->m_alphaKey[i].var.getValue(m_render); // sampled at bind time, once per draw (RW 0x5f66b7)
		}
	}
	if (const DefaultPhysicsModuleData *ph = s.physicsModule())
	{
		gp.gravity[2] = ph->m_gravity;
		gp.drift[0] = ph->m_driftVelocity.x;
		gp.drift[1] = ph->m_driftVelocity.y;
		gp.drift[2] = ph->m_driftVelocity.z;
		gp.velocityDamping[0] = ph->m_velocityDamping.m_low;
		gp.velocityDamping[1] = ph->m_velocityDamping.m_high;
	}
	gp.size[0] = s.info().m_size.m_low;
	gp.size[1] = s.info().m_size.m_high;
	if (const DefaultUpdateModuleData *u = s.updateModule())
	{
		gp.sizeRate[0] = u->m_sizeRate.m_low;
		gp.sizeRate[1] = u->m_sizeRate.m_high;
		gp.sizeRateDamping[0] = u->m_sizeRateDamping.m_low;
		gp.sizeRateDamping[1] = u->m_sizeRateDamping.m_high;
		gp.xyRotation[0] = u->m_angleXY.m_low;
		gp.xyRotation[1] = u->m_angleXY.m_high;
		gp.xyRotationRate[0] = u->m_angularRateXY.m_low;
		gp.xyRotationRate[1] = u->m_angularRateXY.m_high;
		gp.xyRotationDamping[0] = u->m_angularDampingXY.m_low;
		gp.xyRotationDamping[1] = u->m_angularDampingXY.m_high;
		gp.zRotation[0] = u->m_angleZ.m_low;
		gp.zRotation[1] = u->m_angleZ.m_high;
		gp.zRotationRate[0] = u->m_angularRateZ.m_low;
		gp.zRotationRate[1] = u->m_angularRateZ.m_high;
		gp.zRotationDamping[0] = u->m_angularDamping.m_low;
		gp.zRotationDamping[1] = u->m_angularDamping.m_high;
	}
}

// QuadDraw (RW 0x963481)
void ParticleDrawBuilder::buildQuads(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b)
{
	b.kind = DrawBatch::MESH;
	int n = 0;
	static const float kUv[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }; // RW 0x5782bc default 4-cycle
	for (int p = s.firstParticle(); p >= 0 && n < MAX_PARTICLES_PER_DRAW; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		if (s.isParticleInvisible(pp))
		{
			continue;
		}
		const ParticleLook l = lookOf(pp);
		const float sz = l.size;
		const float corners[4][3] = { { -sz, 0, +sz }, { +sz, 0, +sz }, { -sz, 0, -sz }, { +sz, 0, -sz } };
		float R[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		const int rot = pp.update.present ? pp.update.rotation : 0;
		const float a = l.angle;
		const float c = std::cos(a), sn = std::sin(a);
		if (rot == PARTICLE_ROTATION_Z)
		{
			const float M[3][3] = { { c, -sn, 0 }, { sn, c, 0 }, { 0, 0, 1 } };
			std::copy(&M[0][0], &M[0][0] + 9, &R[0][0]);
		}
		else if (rot == PARTICLE_ROTATION_X)
		{
			const float M[3][3] = { { 1, 0, 0 }, { 0, c, -sn }, { 0, sn, c } };
			std::copy(&M[0][0], &M[0][0] + 9, &R[0][0]);
		}
		else if (rot == PARTICLE_ROTATION_Y)
		{
			const float M[3][3] = { { c, 0, sn }, { 0, 1, 0 }, { -sn, 0, c } };
			std::copy(&M[0][0], &M[0][0] + 9, &R[0][0]);
		}
		else if (rot == PARTICLE_ROTATION_V)
		{
			rotateAboutAxis(a, Coord3D{ pp.vel.x, -pp.vel.y, pp.vel.z }, R);
		}
		const std::uint32_t base = (std::uint32_t)b.vertices.size();
		for (int k = 0; k < 4; ++k)
		{
			MeshVertex v;
			v.x = R[0][0] * corners[k][0] + R[0][1] * corners[k][1] + R[0][2] * corners[k][2] + pp.pos.x;
			v.y = R[1][0] * corners[k][0] + R[1][1] * corners[k][1] + R[1][2] * corners[k][2] + pp.pos.y;
			v.z = R[2][0] * corners[k][0] + R[2][1] * corners[k][1] + R[2][2] * corners[k][2] + pp.pos.z;
			v.u = kUv[k][0];
			v.v = kUv[k][1];
			v.r = quantize(l.r);
			v.g = quantize(l.g);
			v.b = quantize(l.b);
			v.a = quantize(l.a);
			b.vertices.push_back(v);
		}
		const std::uint32_t idx[6] = { base, base + 1, base + 2, base + 1, base + 3, base + 2 };
		b.indices.insert(b.indices.end(), idx, idx + 6);
		++n;
	}
}

// ButterflyDraw (RW 0x963da0)
void ParticleDrawBuilder::buildButterflies(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b)
{
	b.kind = DrawBatch::MESH;
	int n = 0;
	static const float kUv[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
	for (int p = s.firstParticle(); p >= 0 && n < 256; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		if (s.isParticleInvisible(pp))
		{
			continue;
		}
		const ParticleLook l = lookOf(pp);
		const float sz = l.size, a = l.angle;
		const float sx = 2.0f * sz * std::sin(a);
		const float cx = std::max(2.0f * sz * std::cos(a), -sz);
		const float wings[2][4][3] = { { { 0, +sz, 0 }, { sx, +sz, cx }, { 0, -sz, 0 }, { sx, -sz, cx } }, { { 0, +sz, 0 }, { -sx, +sz, cx }, { 0, -sz, 0 }, { -sx, -sz, cx } } };
		float heading = 0.0f;
		const bool rotate = std::fabs(pp.vel.x) + std::fabs(pp.vel.y) > 1e-4f; // |v.xy| > 1e-4 (RW 0xbd19e0)
		if (rotate)
		{
			heading = std::atan2(-pp.vel.y, pp.vel.x) - 1.5707964f;
		}
		const float ch = std::cos(heading), sh = std::sin(heading);
		for (int w = 0; w < 2; ++w)
		{
			const std::uint32_t base = (std::uint32_t)b.vertices.size();
			for (int k = 0; k < 4; ++k)
			{
				MeshVertex v;
				const float x = wings[w][k][0], y = wings[w][k][1], z = wings[w][k][2];
				v.x = ch * x - sh * y + pp.pos.x;
				v.y = sh * x + ch * y + pp.pos.y;
				v.z = z + pp.pos.z;
				v.u = kUv[k][0];
				v.v = kUv[k][1];
				v.r = quantize(l.r);
				v.g = quantize(l.g);
				v.b = quantize(l.b);
				v.a = quantize(l.a);
				b.vertices.push_back(v);
			}
			const std::uint32_t idx[6] = { base, base + 1, base + 2, base + 1, base + 3, base + 2 };
			b.indices.insert(b.indices.end(), idx, idx + 6);
		}
		++n;
	}
}

void BuildRibbon(const std::vector<Coord3D> &points, const std::vector<float> &widths, const std::vector<std::array<float, 4>> &colours, const std::vector<float> &vOfPoint,
	const DrawCamera &camera, std::vector<MeshVertex> &vertices, std::vector<std::uint32_t> &indices)
{
	const size_t n = points.size();
	if (n < 2)
	{
		return;
	}
	const std::uint32_t base = (std::uint32_t)vertices.size();
	for (size_t i = 0; i < n; ++i)
	{
		// the segment direction at a joint is the average of its two segments
		Coord3D dir;
		if (i == 0)
		{
			dir = sub(points[1], points[0]);
		}
		else if (i + 1 == n)
		{
			dir = sub(points[i], points[i - 1]);
		}
		else
		{
			const Coord3D a = normalize(sub(points[i], points[i - 1])), c = normalize(sub(points[i + 1], points[i]));
			dir = Coord3D{ a.x + c.x, a.y + c.y, a.z + c.z };
		}
		const Coord3D toEye = sub(camera.position, points[i]);
		Coord3D side = normalize(cross(dir, toEye));
		if (side.x == 0.0f && side.y == 0.0f && side.z == 0.0f)
		{
			side = camera.right;
		}
		const float w = widths[i];
		for (int e = 0; e < 2; ++e)
		{
			const float sgn = e == 0 ? -1.0f : 1.0f;
			MeshVertex v;
			v.x = points[i].x + side.x * w * sgn;
			v.y = points[i].y + side.y * w * sgn;
			v.z = points[i].z + side.z * w * sgn;
			v.u = (float)e;
			v.v = vOfPoint[i];
			v.r = colours[i][0];
			v.g = colours[i][1];
			v.b = colours[i][2];
			v.a = colours[i][3];
			vertices.push_back(v);
		}
	}
	for (size_t i = 0; i + 1 < n; ++i)
	{
		const std::uint32_t a = base + (std::uint32_t)i * 2;
		const std::uint32_t idx[6] = { a, a + 1, a + 2, a + 1, a + 3, a + 2 };
		indices.insert(indices.end(), idx, idx + 6);
	}
}

// StreakDraw (RW 0x9624b9): the ribbon through the visible particles in list order, widths = sizes, V = personality & 1
void ParticleDrawBuilder::buildStreak(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b, const DrawCamera &camera)
{
	b.kind = DrawBatch::MESH;
	b.ribbon = true;
	std::vector<Coord3D> pts;
	std::vector<float> widths, vs;
	std::vector<std::array<float, 4>> cols;
	for (int p = s.firstParticle(); p >= 0 && (int)pts.size() < MAX_PARTICLES_PER_DRAW; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		if (s.isParticleInvisible(pp))
		{
			continue;
		}
		const ParticleLook l = lookOf(pp);
		pts.push_back(pp.pos);
		widths.push_back(l.size);
		cols.push_back({ quantize(l.r), quantize(l.g), quantize(l.b), quantize(l.a) });
		vs.push_back((float)(pp.particleID & 1u));
	}
	if (pts.size() < 2)
	{
		return;
	}
	cols[0] = { 0.0f, 0.0f, 0.0f, 0.0f }; // rgba[0] = 0 kills the trailing edge (RW 0x9624b9)
	BuildRibbon(pts, widths, cols, vs, camera, b.vertices, b.indices);
}

// LightningDraw (RW 0x962a31)
void ParticleDrawBuilder::buildLightning(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b, const DrawCamera &camera)
{
	b.kind = DrawBatch::MESH;
	b.ribbon = true;
	const LightningDrawModuleData *d = static_cast<const LightningDrawModuleData *>(s.drawModule());
	std::vector<Coord3D> pts;
	std::vector<float> sizes;
	std::vector<std::array<float, 4>> cols;
	std::uint32_t headFrame = 0;
	for (int p = s.firstParticle(); p >= 0 && (int)pts.size() < MAX_PARTICLES_PER_DRAW; p = m.particle(p).sysNext)
	{
		const Particle &pp = m.particle(p);
		if (s.isParticleInvisible(pp))
		{
			continue;
		}
		if (pts.empty())
		{
			headFrame = pp.createFrame;
		}
		const ParticleLook l = lookOf(pp);
		pts.push_back(pp.pos);
		sizes.push_back(l.size);
		cols.push_back({ quantize(l.r), quantize(l.g), quantize(l.b), quantize(l.a) });
	}
	if (pts.size() < 2 || pts.size() > 30)
	{
		return; // up to 30 control points per line (RW this+0x484 + 0x168 * line)
	}
	LightningState &st = m_lightning[s.id()];
	const size_t n = pts.size();
	if (st.cachedFrame != headFrame || st.offsets.empty() || st.offsets[0].size() != n)
	{
		// re-roll: line count by MultiChance, offsets re-sampled for every point of every line
		st.cachedFrame = headFrame;
		const float chance = m_render.real(0.0f, 1.0f);
		st.numLines = (d->m_multiChance <= chance) ? 1 : 2;
		st.offsets.assign((size_t)st.numLines, std::vector<Coord3D>(n));
		const Coord3D dir = normalize(sub(pts.back(), pts.front()));
		const Coord3D R{ 0.0f - dir.y, dir.x, 0.0f };
		const Coord3D U{ (0.0f - dir.x) * dir.z, (0.0f - dir.y) * dir.z, dir.x * dir.x + dir.y * dir.y };
		for (int line = 0; line < st.numLines; ++line)
		{
			for (size_t i = 0; i < n; ++i)
			{
				if (i == 0 || i + 1 == n)
				{
					st.offsets[(size_t)line][i] = Coord3D{ 0.0f, 0.0f, 0.0f };
					continue;
				}
				const float oz = d->m_offsetZ.getValue(m_render), ox = d->m_offsetX.getValue(m_render), oy = d->m_offsetY.getValue(m_render);
				st.offsets[(size_t)line][i] = Coord3D{ oz * dir.x + ox * R.x + oy * U.x, oz * dir.y + ox * R.y + oy * U.y, oz * dir.z + ox * R.z + oy * U.z };
			}
		}
	}
	const float step = d->m_tileTexture ? 1.0f : 1.0f / (float)(n - 1);
	for (int line = 0; line < st.numLines; ++line)
	{
		std::vector<Coord3D> lp(n);
		std::vector<float> vs(n);
		for (size_t i = 0; i < n; ++i)
		{
			const Coord3D &o = st.offsets[(size_t)line][i];
			lp[i] = Coord3D{ pts[i].x + o.x, pts[i].y + o.y, pts[i].z + o.z };
			vs[i] = (float)i * step;
		}
		BuildRibbon(lp, sizes, cols, vs, camera, b.vertices, b.indices);
	}
}

std::vector<DrawBatch> ParticleDrawBuilder::build(const ParticleSystemManager &manager, const DrawCamera &camera)
{
	// lane PERF-2: three passes. 1: the systems in list order on this thread: the skipped kinds and their notes, the batch headers, and the draws that
	// sample the render stream (GpuDraw's alpha keys, LightningDraw's re-rolls and its per-system state) built here, so the stream is drawn in list order
	// as before. 2: the sprite / quad / butterfly / streak geometry (a function of the system's particles and the camera; each job writes only its own
	// batch) on the client job pool. 3: the batches in list order: the empty ones dropped, the ribbon note, the deferred ones last, as before.
	struct Slot
	{
		const ParticleSystem *system;
		DrawBatch batch;
		int drawClass;
	};
	std::vector<Slot> slots;
	std::vector<size_t> parallel; // the slots built in pass 2
	// pass 1's outcomes in list order (a note, or a slot): pass 3 replays them, so the notes keep the order of the single pass
	struct Event
	{
		const char *code, *text; ///< a note
		size_t slot;             ///< or a slot (code == nullptr)
	};
	std::vector<Event> events;
	auto deferNote = [&events](const char *code, const char *text) { events.push_back(Event{ code, text, 0 }); };
	manager.forEachSystem([&](const ParticleSystem &s) {
		const int type = s.particleType();
		if (type == PARTICLE_TYPE_TERRAIN_PARTICLE)
		{
			// RW 0x44cd2d draws type 6 in a sibling terrain pass this lane does not own and the port has no such pass: reported, never silent
			deferNote("S-195", "TERRAIN_PARTICLE (type 6) systems are drawn by RW's terrain particle pass (0x44cd2d), which is not ported; they are not drawn");
			return;
		}
		if (isSmudge(s))
		{
			deferNote("S-194", "SMUDGE particle systems need the screen-space heat-distortion pass (RW smudge manager); they are not drawn");
			return;
		}
		const ModuleData *draw = s.drawModule();
		if (!draw)
		{
			return;
		}
		DrawBatch b;
		b.texture = textureName(s);
		b.shader = s.shaderType();
		b.sortLevel = (int)s.info().m_sortLevel;
		b.deferred = b.sortLevel > 0 && b.sortLevel < 2; // RW 0x44c97d: only level 1 is ever deferred
		b.system = s.id();
		b.templateName = s.getTemplate().getName();
		b.drawClass = draw->classId;
		const bool gpuType = type == PARTICLE_TYPE_GPU_PARTICLE || type == PARTICLE_TYPE_GPU_TERRAINFIRE;
		switch (draw->classId)
		{
		case MODULE_GPU_DRAW:
			if (type != PARTICLE_TYPE_GPU_PARTICLE)
			{
				return;
			}
			buildGpu(s, manager, b);
			break;
		case MODULE_LIGHTNING_DRAW:
			if (gpuType)
			{
				return;
			}
			buildLightning(s, manager, b, camera);
			break;
		case MODULE_DEFAULT_DRAW:
		case MODULE_QUAD_DRAW:
		case MODULE_BUTTERFLY_DRAW:
		case MODULE_STREAK_DRAW:
			if (gpuType)
			{
				return; // the CPU modules return 0 for GPU types (RW 0x961eac)
			}
			parallel.push_back(slots.size());
			break;
		case MODULE_RENDER_OBJECT_DRAW:
			deferNote("S-195", "DRAWABLE particle systems (RenderObjectDraw) move one W3D render object per particle; the render-object bridge is not part of the batch builder");
			return;
		default:
			return;
		}
		events.push_back(Event{ nullptr, nullptr, slots.size() });
		slots.push_back(Slot{ &s, std::move(b), draw->classId });
	});
	JobSystem::client().parallelFor(parallel.size(), 4, [&](size_t, size_t begin, size_t end) {
		for (size_t k = begin; k < end; ++k)
		{
			Slot &slot = slots[parallel[k]];
			switch (slot.drawClass)
			{
			case MODULE_DEFAULT_DRAW: buildSprites(*slot.system, manager, slot.batch); break;
			case MODULE_QUAD_DRAW: buildQuads(*slot.system, manager, slot.batch); break;
			case MODULE_BUTTERFLY_DRAW: buildButterflies(*slot.system, manager, slot.batch); break;
			case MODULE_STREAK_DRAW: buildStreak(*slot.system, manager, slot.batch, camera); break;
			default: break;
			}
		}
	});
	std::vector<DrawBatch> normal, deferred;
	for (const Event &ev : events)
	{
		if (ev.code)
		{
			note(ev.code, ev.text);
			continue;
		}
		DrawBatch &b = slots[ev.slot].batch;
		const bool empty = b.sprites.empty() && b.gpu.empty() && b.vertices.empty();
		if (empty)
		{
			continue;
		}
		if (b.ribbon)
		{
			note("S-199", RibbonApproximationText());
		}
		if (b.kind == DrawBatch::SPRITES)
		{
			note("S-1440", SpriteCompositeText()); // lane FX-3
		}
		(b.deferred ? deferred : normal).push_back(std::move(b));
	}
	// the deferred (SortLevel 1) systems draw last, in reverse list order (RW 0x44cc8c)
	std::reverse(deferred.begin(), deferred.end());
	for (DrawBatch &d : deferred)
	{
		normal.push_back(std::move(d));
	}
	return normal;
}

// ---- W3D model emitters ----------------------------------------------------------------------------------------------------

int ParticleShaderForW3dShader(const W3dShaderStruct &sh, bool *approximated)
{
	if (approximated)
	{
		*approximated = false;
	}
	const bool test = sh.AlphaTest == W3DSHADER_ALPHATEST_ENABLE;
	if (sh.SrcBlend == W3DSHADER_SRCBLENDFUNC_ONE && sh.DestBlend == W3DSHADER_DESTBLENDFUNC_ONE)
	{
		return test ? PARTICLE_SHADER_ADDITIVE_ALPHA_TEST : PARTICLE_SHADER_ADDITIVE;
	}
	if (sh.SrcBlend == W3DSHADER_SRCBLENDFUNC_SRC_ALPHA && sh.DestBlend == W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_ALPHA)
	{
		// SRCALPHA / INVSRCALPHA. With the alpha test on and the depth mask enabled (E_MumaFlies) the particle shader ids have no "blend + test +
		// depth write" member; ALPHA (blend, ref-1 test, no depth write) is the closest and the difference is reported.
		if (test && approximated)
		{
			*approximated = true;
		}
		return PARTICLE_SHADER_ALPHA;
	}
	if (sh.SrcBlend == W3DSHADER_SRCBLENDFUNC_ONE && sh.DestBlend == W3DSHADER_DESTBLENDFUNC_ZERO)
	{
		return test ? PARTICLE_SHADER_ALPHA_TEST : -1;
	}
	if (sh.SrcBlend == W3DSHADER_SRCBLENDFUNC_ZERO && sh.DestBlend == W3DSHADER_DESTBLENDFUNC_SRC_COLOR)
	{
		return PARTICLE_SHADER_MULTIPLY;
	}
	return -1;
}

std::vector<std::string> BuildEmitterBatches(W3DEmitterWorld &world, const DrawCamera &cam, std::vector<DrawBatch> &out)
{
	static const float kTri[3][2] = { { 0.0f, -2.0f }, { -1.732f, 1.0f }, { 1.732f, 1.0f } };
	static const float kTriUv[3][2] = { { 0.5f, 0.0f }, { 0.0f, 0.866f }, { 1.0f, 0.866f } };
	static const float kQuad[4][2] = { { -0.5f, 0.5f }, { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f } };
	static const float kQuadUv[4][2] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };
	std::vector<std::string> stops;
	auto note = [&](const std::string &line) {
		for (const std::string &s : stops)
		{
			if (s == line)
			{
				return;
			}
		}
		stops.push_back(line);
	};
	std::vector<ParticleEmitterInstance::Visual> visuals;
	for (const std::unique_ptr<ParticleEmitterInstance> &up : world.emitters())
	{
		ParticleEmitterInstance &e = *up;
		const unsigned mode = e.renderMode();
		if (mode > 1)
		{
			note("S-197: W3D emitter " + e.name() + " uses render mode " + std::to_string(mode) + " (line / line group); only the point-group modes 0 and 1 are drawn");
			continue;
		}
		if (e.emitPeriodPrecisionSensitive())
		{
			note("S-196: W3D emitter " + e.name() + " emits every " + std::to_string(e.emitRateMs()) + " ms with the PC24 float-rounded period used here and every " +
				std::to_string(e.emitRateExtendedMs()) + " ms under extended precision; the x87 precision state of the retail constructor (RW 0x5a07e8) was not established");
		}
		bool approx = false;
		const int shader = ParticleShaderForW3dShader(e.shader(), &approx);
		if (shader < 0)
		{
			note("S-196: W3D emitter " + e.name() + " has a shader combination the port does not map (src " + std::to_string(e.shader().SrcBlend) + ", dst " +
				std::to_string(e.shader().DestBlend) + ", alpha test " + std::to_string(e.shader().AlphaTest) + "); not drawn");
			continue;
		}
		if (approx)
		{
			note("S-196: W3D emitter " + e.name() + " blends SRC_ALPHA/INV_SRC_ALPHA with alpha test and depth write; drawn with the ALPHA particle shader (no depth write)");
		}
		e.collectVisuals(visuals);
		if (visuals.empty())
		{
			continue;
		}
		const unsigned rowsLog2 = e.frameMode() > 4 ? 4 : e.frameMode();
		const unsigned rows = 1u << rowsLog2;
		const unsigned frameMask = (1u << (2 * rowsLog2)) - 1;
		DrawBatch b;
		b.kind = DrawBatch::MESH;
		b.texture = e.textureName();
		b.shader = shader;
		b.templateName = e.name();
		const int per = mode == 0 ? 3 : 4;
		b.vertices.reserve(visuals.size() * per);
		for (const ParticleEmitterInstance::Visual &v : visuals)
		{
			const float ang = (float)v.orientation * (6.28318530718f / 256.0f);
			const float c = std::cos(ang), s = std::sin(ang);
			const unsigned cell = (unsigned)v.frame & frameMask;
			const float u0 = (float)(cell % rows) / (float)rows, v0 = (float)(cell / rows) / (float)rows, sc = 1.0f / (float)rows;
			const float cr = (float)(unsigned)(v.color.X * 255.0f) / 255.0f, cg = (float)(unsigned)(v.color.Y * 255.0f) / 255.0f,
				cb = (float)(unsigned)(v.color.Z * 255.0f) / 255.0f, ca = (float)(unsigned)(v.alpha * 255.0f) / 255.0f;
			const std::uint32_t base = (std::uint32_t)b.vertices.size();
			for (int k = 0; k < per; ++k)
			{
				const float lx = mode == 0 ? kTri[k][0] : kQuad[k][0], ly = mode == 0 ? kTri[k][1] : kQuad[k][1];
				const float qx = (lx * c - ly * s) * v.size, qy = (lx * s + ly * c) * v.size;
				MeshVertex mv;
				mv.x = v.position.X + cam.right.x * qx + cam.up.x * qy;
				mv.y = v.position.Y + cam.right.y * qx + cam.up.y * qy;
				mv.z = v.position.Z + cam.right.z * qx + cam.up.z * qy;
				mv.u = u0 + (mode == 0 ? kTriUv[k][0] : kQuadUv[k][0]) * sc;
				mv.v = v0 + (mode == 0 ? kTriUv[k][1] : kQuadUv[k][1]) * sc;
				mv.r = cr;
				mv.g = cg;
				mv.b = cb;
				mv.a = ca;
				b.vertices.push_back(mv);
			}
			b.indices.push_back(base);
			b.indices.push_back(base + 1);
			b.indices.push_back(base + 2);
			if (mode == 1)
			{
				b.indices.push_back(base + 2);
				b.indices.push_back(base + 3);
				b.indices.push_back(base);
			}
		}
		out.push_back(std::move(b));
	}
	return stops;
}

} // namespace FXParticleSystem
