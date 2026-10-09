// OpenBFME. GPL-3.0.
//
// ParticleDrawBuilder: turns the simulator's state into draw batches, the half of W3DParticleSystemManager::doParticles and the
// seven Draw modules that does not depend on a graphics API (Godot uploads the batches and applies the blend states). Lane FX-1.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; notes fx-draw.md, fx-draw-gpu.md): doParticles RW 0x44c84a walks the systems in
// list order and calls each system's Draw module (RW vtable +0x10); DefaultDraw 0x961dfc, StreakDraw 0x9624b9, QuadDraw 0x963481,
// ButterflyDraw 0x963da0, LightningDraw 0x962a31, RenderObjectDraw 0x964c00, GpuDraw 0x9658ba. Systems with SortLevel == 1 are
// deferred and drawn last in REVERSE list order; every other level draws in list order. Each CPU module gathers at most 0x200
// visible particles per system, skips Particle::isInvisible, and takes size from the Update module (0 without one), colour from the
// Color module (black without one) and alpha from the Alpha module (0 without one). Vertex colour is a clamped, truncated 8-bit
// RGBA. TERRAIN_PARTICLE systems (Type 6) are drawn by the terrain pass, which this lane does not own; SMUDGE systems need the screen
// space distortion pass (stop S-194); DRAWABLE (RenderObjectDraw) systems move W3D render objects (stop S-195 until the
// instancer integration lands). IsEmitAboveGroundOnly / UseMaximumHeight / IsParticleUpTowardsEmitter never reach a draw module.
//
// All positions are SAGE world space (x east, y north, z up), as the simulator keeps them; the Godot layer converts to (x, z, -y).

#pragma once

#include "GameClient/ParticleSys.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "Libraries/WWVegas/WW3D2/part_emt.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FXParticleSystem
{

// The camera as the draw modules need it, in SAGE world space.
struct DrawCamera
{
	Coord3D position;
	Coord3D right{ 1.0f, 0.0f, 0.0f };
	Coord3D up{ 0.0f, 0.0f, 1.0f };
	Coord3D forward{ 0.0f, 1.0f, 0.0f };
};

// DefaultDraw instance (RW arrays pos / rgba / size / angle byte, RW 0x961dfc).
struct SpriteInstance
{
	float x, y, z;
	float size;
	std::uint8_t angleByte; ///< (uint8)ftol(angle * 40.5845f), RW const 0x4222568a
	float r, g, b, a;
};

// GPU particle vertex record, written once at spawn (RW 0x7b1141..0x7b12cc): POSITION (pos, life), TEXCOORD0 (vel, birth), TEXCOORD1 (rand, corner).
struct GpuInstance
{
	float x, y, z, life;
	float vx, vy, vz, birth;
	float random;
};

struct MeshVertex
{
	float x, y, z;
	float u, v;
	float r, g, b, a;
};

// The values the SAS callbacks bind to gpuparticle.fxo (notes fx-draw-gpu.md 1.4), per draw.
struct GpuDrawParams
{
	float framesPerRow = 1.0f, totalFrames = 1.0f, speedMultiplier = 1.0f;
	float colorKeys[4][4] = {};     ///< Color1..4 rgb, Alpha1..4 sampled per draw in w
	float timeKeys[4] = {};         ///< frames of Color1..4
	float colorScale[2] = {};       ///< min, max in 0..1 units
	int shaderType = 1;
	float gravity[3] = {};
	float drift[3] = {};
	float velocityDamping[2] = { 1.0f, 1.0f };
	float size[2] = { 0.0f, 0.0f };
	float sizeRate[2] = {};
	float sizeRateDamping[2] = { 1.0f, 1.0f };
	float xyRotation[2] = {}, xyRotationRate[2] = {}, xyRotationDamping[2] = { 1.0f, 1.0f };
	float zRotation[2] = {}, zRotationRate[2] = {}, zRotationDamping[2] = { 1.0f, 1.0f };
	float now = 0.0f; ///< Time * 30 (float frames, RW 0x7b1160)
};

struct DrawBatch
{
	enum Kind
	{
		SPRITES,
		GPU,
		MESH
	};
	Kind kind = SPRITES;
	std::string texture;       ///< ParticleName as written in the INI
	int shader = PARTICLE_SHADER_ADDITIVE;
	int sortLevel = 0;
	bool deferred = false;     ///< SortLevel == 1: drawn after everything else, unsorted
	bool groundAligned = false;///< DefaultDraw Set_Billboard(!IsGroundAligned)
	bool volume = false;       ///< VOLUME_PARTICLE (RenderVolumeParticle depth 6)
	ParticleSystemID system = INVALID_PARTICLE_SYSTEM_ID;
	std::string templateName;
	ModuleClassId drawClass = MODULE_DEFAULT_DRAW;
	std::vector<SpriteInstance> sprites;
	std::vector<GpuInstance> gpu;
	GpuDrawParams gpuParams;
	std::vector<MeshVertex> vertices;
	std::vector<std::uint32_t> indices;
	bool ribbon = false;       ///< a Streak / Lightning strip (cull enabled; the others draw both faces)
};

class ParticleDrawBuilder
{
public:
	// `render` is the stream the draw modules sample with (GpuDraw alpha keys, Lightning re-rolls). It must NOT be the simulation's client
	// stream: the number of draws per step must not depend on the render rate.
	explicit ParticleDrawBuilder(W3DDrawRandom &render) : m_render(render) {}

	// Batches in draw order: list-order systems, then the deferred SortLevel 1 systems in reverse list order.
	std::vector<DrawBatch> build(const ParticleSystemManager &manager, const DrawCamera &camera);

	// Draw-side acceptance-stop lines (S-194 smudge, S-195 drawable particles).
	std::vector<std::string> unverified() const { return m_unverified; }

	static std::uint8_t angleByte(float angle);

private:
	struct LightningState
	{
		std::uint32_t cachedFrame = 0xffffffffu;
		int numLines = 1;
		std::vector<std::vector<Coord3D>> offsets; ///< per line, per control point (RW this+0x484, 30 points per line)
	};
	void buildSprites(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b);
	void buildGpu(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b);
	void buildQuads(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b);
	void buildButterflies(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b);
	void buildStreak(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b, const DrawCamera &camera);
	void buildLightning(const ParticleSystem &s, const ParticleSystemManager &m, DrawBatch &b, const DrawCamera &camera);
	void note(const char *code, const char *text);

	W3DDrawRandom &m_render;
	std::map<ParticleSystemID, LightningState> m_lightning;
	std::vector<std::string> m_unverified;
};

// W3D model emitters (ParticleBufferClass::Render_Particles -> PointGroupClass::Render, donor ZH pointgr.cpp, RW stop S-001 caveat): one MESH batch per
// emitter instance with its particles as billboards in view space. RenderMode 0 draws the point-group triangle (vertices (0,-2), (-1.732,1),
// (1.732,1) x size, UVs (0.5,0), (0,0.866), (1,0.866) of the frame cell), RenderMode 1 the quad (+-0.5 x size); the orientation byte indexes the
// 256 step rotation table (angle = byte * 2pi / 256) and FrameMode N selects a (2^N x 2^N) atlas, frame row-major. The blend state is taken from
// the emitter's W3dShaderStruct; a combination the port cannot map is reported as stop S-196 (never silently drawn as something else).
// Render modes 2..4 (line / line groups; no retail emitter uses them) are reported as S-197. Returns the lines of the stops hit.
std::vector<std::string> BuildEmitterBatches(W3DEmitterWorld &world, const DrawCamera &camera, std::vector<DrawBatch> &out);

// The particle shader id (PARTICLE_SHADER_*) for a W3dShaderStruct, or -1 when the combination is not one the port implements.
int ParticleShaderForW3dShader(const W3dShaderStruct &shader, bool *approximated);

// ---- acceptance-stop texts and ordering rules shared by the builder, the Godot device layer and the tests ------------------------------------
const char *RibbonApproximationText();  ///< S-199
const char *SortApproximationText();    ///< S-198 (sorting)
const char *FogOmissionText();          ///< S-198 (fog)
const char *SpriteCompositeText();      ///< S-1440 (lane FX-3: the sprite composite is derived, not compared with a retail capture)

// Where a merged batch sits in the compositing order: `rank` is its position in the builder's draw order, `count` the number of merged batches.
// The result is strictly monotone in (priority, sortOffset) for every count, including counts above the number of material priorities.
struct BatchOrder
{
	int priority;
	float sortOffset;
};
BatchOrder OrderForBatch(size_t rank, size_t count);

// Whether a merged sprite batch may be reordered back to front: only non-deferred ALPHA batches. Deferred (SortLevel 1) systems bypass the sorting
// renderer in retail (RW 0x44cbed) and keep their list order.
bool SpriteBatchDepthSorted(int shader, bool deferred);

// A camera-facing ribbon through `points` with per-point half widths and colours (the StreakLine renderer of RW 0xb55xxx, donor ZH
// streakRender.cpp RenderStreak, simplified to one quad per segment joined by averaged edge directions). `vOfPoint` is the V coordinate
// of each point (personality & 1 for Streak, the segment index for Lightning).
void BuildRibbon(const std::vector<Coord3D> &points, const std::vector<float> &widths, const std::vector<std::array<float, 4>> &colours, const std::vector<float> &vOfPoint,
	const DrawCamera &camera, std::vector<MeshVertex> &vertices, std::vector<std::uint32_t> &indices);

} // namespace FXParticleSystem
