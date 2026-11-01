// OpenBFME. GPL-3.0.
//
// Water mesh geometry (no Godot dependency): the grid retail fills from a standing-water polygon and the
// river ribbon built from cross-section lines. Standing water is not triangulated: RenderableStandingWaterArea::
// AllocateAndFillBuffers (RW 0x482A16) keeps the grid cells whose centre is inside the polygon (buildStandingGrid),
// so a polygon that touches itself, as Adorn River's does, needs no special case. The river strip, its UVs and its vertex colour are
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
// Defaults from retail watershader.fxo. Authored technique indices select the
// supported PCA geometry/normal or bump paths; reflections remain S-035.
struct StandingMaterial
{
	unsigned technique = 0;
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
// StandingWaterArea.cpp (0x30822F..0x308509). Presentation only.
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
// RotWK RenderableStandingWaterArea::AllocateAndFillBuffers (RW 0x482A16; BFME2 W3DWater.cpp 0x8304A), high-detail grid.
// False (and an empty grid) for fewer than 3 points, non-finite or unrepresentable coordinates, or a polygon whose
// grid keeps no cell: the caller reports it.
bool buildStandingGrid(const StandingWaterArea &area, const std::vector<StandingWaveArea> &waves,
	const std::function<float(float,float)> &ground, StandingGrid &out);

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
// waterDiffuse (the time of day's WaterSet DiffuseColor, MapWeather.h), whose alpha is the vertex alpha; a black one is
// white; any other colour is used as it is. `tod` null: no terrain light.
void riverVertexColour(const RiverArea &river, const TimeOfDayLights *tod, const float waterDiffuse[4], float out[4]);
} // namespace WaterGeometry
