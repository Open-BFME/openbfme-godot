// OpenBFME. GPL-3.0.
//
// Water mesh geometry (no Godot dependency): ear-clipping triangulation of standing-water polygons and the
// river ribbon built from cross-section lines. Spec maps-and-terrain.md 3.6 ("triangulate each polygon (ear
// clipping; points may be non-convex) at z = waterHeight"). The river strip, its UVs and its vertex colour are
// those of WaterRenderObjClass::drawRiverWater (BFME2 1.06 game.dat RVA 0x00100A0B, reconstructed in Open-BFME-2's
// W3DWaterDrawRiverWater.cpp), which draws them with RiverWater.fx (river.gdshader).

#pragma once

#include "GameClient/MapChunks.h"

#include <cstdint>
#include <array>
#include <vector>
#include <functional>

struct MeshShaderMaterialDef;

namespace WaterGeometry
{
// Defaults from retail watershader.fxo. The authored technique index names the technique FXShaderSetup::Load_W3D asks for; which one
// runs is resolveTechnique's answer for the player's WaterLOD. Reflections remain S-035.
struct StandingMaterial
{
	unsigned technique = 0;
	float reflectionStrength = 0.2f;                         // fxo default; BumpSpecular's PS blends the lit colour toward WaterReflectionTexture by it
	bool useMySpecularDirection = false;                     // fxo default 0
	float mySpecularDirection[2] = { 30.0f, 60.0f };         // fxo default, degrees
	float gerstner[3][4] = {{1, .1f, .1f, .5f}, { .00001f, .00001f, .00001f, .00001f}, { .00001f, .00001f, .00001f, .00001f}};
	float pcaUV[2] = { 1.5f, 3.0f }, pcaWaveUV[2] = { 0.01f, 0.0075f };
	float specular[3] = { 0.8f, 0.8f, 0.8f };
	float reflectionPerturbation = 0.005f;
	std::string pcaTexture[3], pcaNoiseTexture, pcaFrothTexture;
	float diffuse[4] = { 0.15f, 0.3f, 0.35f, 0.5f };
	float dimming = 0.8f, bumpScale = 0.2f, specularExponent = 50.0f;
	float windAngle = 0.0f, windSpeed = 0.02f;
	float waveAngle = 0.2f, waveSpeed = 0.0f;
	float bumpUV[2] = { 7.0f, 7.0f }, envUV[2] = { 0.2f, 0.2f }, waveUV[2] = { 7.0f, 7.0f };
	bool blendTerrainAlpha = true;
	std::string bumpTexture, envTexture, waveTexture;
};
bool readStandingMaterial(const MeshShaderMaterialDef &source, StandingMaterial &out, std::string *error = nullptr);

// BFME2 donor StandingWaterAreaLoadPCA.cpp (0x3093F6) and
// Rva003093F6BasesSamplers.cpp (0x30822F..0x308509). Presentation only.
using PcaVector = std::array<float, 4>;
using PcaFrame = std::array<PcaVector, 10>; // mean, then X/Y/Z for groups 0..2
struct PcaBasis
{
	float speed = 0.0f;
	std::vector<PcaFrame> frames;
};
bool readPcaBasis(const std::string &text, PcaBasis &out, std::string *error);
PcaFrame samplePcaBasis(const PcaBasis &basis, float clientTime);
// fx_2_0 technique names in file order. Bounds-checked; no baked content indices.
bool readEffectTechniques(const std::vector<std::uint8_t> &bytes, std::vector<std::string> &names, std::string *error);

// ---- WaterLOD: which file, which technique ----
// WaterLOD is the static detail level's (GameLODManager + 0x1784, RW 0xD9E700: Low, Medium, High, UltraHigh = 0..3).
constexpr int kWaterLodCount = 4;

// StandingWaterArea::loadFXShaderSetup (BFME2 0x308C31, StandingWaterAreaLoadFXShaderSetup.cpp): `base` is the area's shader file
// name without its extension. From the player's WaterLOD up to 3 the loop tries base + {"L", "M", "", ""}[lod] and takes the first
// that exists (so Low reads <name>L, then <name>M, then <name>; Medium <name>M, then <name>; High and UltraHigh <name>); none:
// `base` alone (WaterNoShaderSuffix), whose load then reports the missing file.
std::string standingShaderStem(const std::string &base, int waterLod, const std::function<bool(const std::string &)> &exists);

// FXShaderSetup::InitializeShader (BFME2 0x15288F, fxshadersetup.cpp:605-672) as the water makes its second call
// (rva152d1c(WaterShaderLODs[level]), so lodLimit = the WaterLOD): `authored` is the technique name Load_W3D stored (the effect's
// technique at the W3D's index), `effect` the effect's technique names in file order. The authored name's LOD suffix
// ({"_L", "_M", "", "_U"}) gives its own level (a bare name is High, 2; UltraHigh raises a bare name to 3); the level is capped by
// lodLimit; the search goes down from there (name + suffix[i]), then up. `lod` is the suffix index of the technique found.
struct ResolvedTechnique
{
	std::string name;
	int lod = 0;
};
bool resolveTechnique(const std::vector<std::string> &effect, const std::string &authored, int lodLimit, ResolvedTechnique &out, std::string *error);

// What the port draws for a resolved technique; null for a technique it does not port (DefaultTechnique, the _L techniques, PCA_M,
// DepthOnly: the builder reports them, S-035). Blob numbers are watershader.fxo's shader blobs in file order
// (tools/render/d3d9_disasm.py), pinned by tools/render/test_water_binary_facts.py.
struct StandingShaderTraits
{
	const char *technique;
	int pcaMode;                  // 0 the BumpSpecular family (bump texture normal), 1 PCA, 2 PCA_U
	bool bumpHigh;                // BumpSpecular: ps blob 4 reflection blend, SpecularColor highlight, direction through oD1 (clamped to 0..1)
	bool clampDiffuse;            // vs_2_0 clamps oD0 (2*Dimming*Diffuse) to 0..1; PCA_U's vs_3_0 output does not
	float waveSteepness;          // PCA VS 13 0.15, PCA_U VS 14 0.33 (the Gerstner steepness); 0 for the bump family
	int basisTextures;            // WaterPCATexture1..N the pixel shader reads: PCA 2, PCA_U 3
	bool noiseFlattening;         // PCA_U's PCANoiseSampler term
	int pixelBlob, vertexBlob;    // the first pixel blob (the shadow variant is +1, none for blob 8) and the vertex blob
};
const StandingShaderTraits *standingShaderTraits(const std::string &technique);

// The specular direction the shader's half-vector uses: DirectionalLight[1].Direction (the VS bases c14 / c13 and PS bases c7 / c12
// put it at c17 / c16 / c10 / c15) or, for the bump family when UseMySpecularDirection is set, the authored angles (VS blob 19/9:
// (cos x cos y, sin x cos y, -sin y), degrees). BumpSpecular sends it through oD1, which D3D9 clamps to 0..1; the others keep it unclamped.
void specularDirection(const StandingMaterial &material, const StandingShaderTraits &traits, const float toLight1[3], float out[3]);

// Triangulates a simple polygon (either winding, non-convex allowed). Returns false for degenerate input
// (fewer than 3 points, zero area, or a polygon the ear search cannot finish). indices are triangle triples.
bool triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices);

// BFME2 W3DWater.cpp AllocateAndFillBuffers (0x8304A), high-detail grid.
// Wave query follows W3DWaterStandingWaveQuery.cpp (0x8026B).
struct StandingVertex
{
	Point2F point, nearest = {100000,100000}, direction = {1,0};
	float ground = 0, halfLength = 0;
	int waveArea = -1; // diagnostic provenance; -1 means no selected authored wave
};
struct StandingGrid
{
	std::vector<StandingVertex> vertices;
	std::vector<std::uint32_t> indices;
	float spacing = 20;
};
bool buildStandingGrid(const StandingWaterArea &area, const std::vector<StandingWaveArea> &waves,
	const std::function<float(float,float)> &ground, StandingGrid &out);

// Signed area (positive = counter-clockwise in a y-up frame).
float signedArea(const std::vector<Point2F> &polygon);

struct RiverStrip
{
	std::vector<float> position; // xyz SAGE, 2 vertices per cross-section line: its second end, then its first
	std::vector<float> uv0;      // (u1, v): the river and sparkle textures, u1 0.5 at the second end and 0 at the first
	std::vector<float> uv1;      // (u2, v): the alpha edge texture, u2 1 at the second end and 0 at the first
	std::vector<std::uint32_t> index;
};

// The strip drawRiverWater draws, at z = river.waterHeight. Its points run round the river's outline: the first line's two
// ends, then along the lines' second ends to the last line and back along their first ends. v advances the same per line,
// however far apart the lines are: the outline's length less its closing edge, halved, less the first line's width, in
// first-line widths, shared out over the quads, so one repeat is about as long as the first line is wide. v here is before
// the scroll and wobble, which depend on time (river.gdshader). Which stored end of a line starts the outline is not known
// (its builder, RVA 0x00082D6A, is not reconstructed); the first is taken. False for fewer than two lines.
bool buildRiverStrip(const RiverArea &river, RiverStrip &out);

// The colour every vertex of a river gets (RGBA 0..1). A white river (every retail one) takes the terrain's ambient (light 0)
// plus each terrain light's diffuse times its downward component (-lightPos.z, the direction as stored), times
// waterDiffuse (the time of day's WaterSet DiffuseColor, MapWeather.h) with its red and blue trading places (retail packs the colour
// A<<24|R<<16|G<<8|B and reads the low byte as red: RW 0x500400..0x500530), whose alpha is the vertex alpha; a black one is
// white; any other colour is used as it is. `tod` null: no terrain light.
void riverVertexColour(const RiverArea &river, const TimeOfDayLights *tod, const float waterDiffuse[4], float out[4]);
} // namespace WaterGeometry
